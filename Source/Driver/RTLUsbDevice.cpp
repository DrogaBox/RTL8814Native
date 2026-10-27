/*
 * RTL8814Native — USB transport implementation
 *
 * NOTE FOR THE FIRST COMPILE ON YOUR MAC: the IOUSBHostFamily API
 * surface moved over the years. Every call below uses the modern
 * SDK signatures (IOUSBHostPipe::io with IOUSBHostCompletion,
 * IOUSBHostInterface::deviceRequest). If your SDK names differ, fix
 * here only — nothing else in the project touches IOKit USB.
 *
 * URB LIFECYCLE: usb_alloc_urb() attaches a persistent
 * IOUSBHostCompletion sidecar to the urb (urb->shim_priv). Vendor
 * code reuses preallocated URBs for RX, so the sidecar must survive
 * across submissions; usb_free_urb() releases it.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include "RTLUsbDevice.hpp"
#include "VendorGlue.hpp"

#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/usb/StandardUSB.h>
#include <IOKit/usb/AppleUSBDefinitions.h>
#include <kern/thread.h>
#include <libkern/libkern.h>

#define super OSObject
OSDefineMetaClassAndStructors(RTLUsbDevice, OSObject)

/* wait_result fallback — same per-constant guard as Compat.c: the
 * kext SDK exports the THREAD_* list partially (through the IOKit
 * lock header chain), so never assume the full set is present. */
#ifndef THREAD_UNINTERRUPTIBLE
#define THREAD_UNINTERRUPTIBLE 0
#endif

RTLUsbDevice *RTLUsbDevice::withInterface(IOUSBHostInterface *interface,
                                         IOService *owner)
{
        RTLUsbDevice *self = OSTypeAlloc(RTLUsbDevice);
        if (self && !self->initWithInterface(interface, owner)) {
                self->release();
                return nullptr;
        }
        return self;
}

bool RTLUsbDevice::initWithInterface(IOUSBHostInterface *interface,
                                     IOService *owner)
{
        if (!super::init())
                return false;

        _owner = owner;
        _interface = interface;
        _device = nullptr;
        _pipeIn = nullptr;
        _pipeOut = nullptr;
        for (int i = 0; i < 4; i++) {
                _pipesOut[i] = nullptr;
                _bulkOutEps[i] = 0;
        }
        _bulkInEp = 0;
        _bulkOutEp = 0;
        _numBulkOut = 0;
        _maxPacketIn = 512;
        _maxPacketOut = 512;
        _idVendor = 0;
        _idProduct = 0;
        memset(&_epDescIn, 0, sizeof(_epDescIn));
        memset(&_epDescOut, 0, sizeof(_epDescOut));

        if (!interface)
                return false;

        _interface->retain();
        _device = _interface->getDevice();
        if (_device) {
                const StandardUSB::DeviceDescriptor *dd = _device->getDeviceDescriptor();
                if (dd) {
                        _idVendor = USBToHost16(dd->idVendor);
                        _idProduct = USBToHost16(dd->idProduct);
                }
        }

        IOLog("RTL8814Native: USB device %04x:%04x attached\n",
              _idVendor, _idProduct);
        return true;
}

void RTLUsbDevice::free()
{
        if (_pipeIn) { _pipeIn->release(); _pipeIn = nullptr; }
        for (int i = 0; i < 4; i++) {
                if (_pipesOut[i]) {
                        _pipesOut[i]->release();
                        _pipesOut[i] = nullptr;
                }
        }
        _pipeOut = nullptr;
        if (_interface) {
                _interface->release();
                _interface = nullptr;
        }
        _device = nullptr;
        _owner = nullptr;
        super::free();
}

bool RTLUsbDevice::openProvider()
{
        if (!_interface || !_owner)
                return false;
        if (!_interface->open(_owner))
                return false;
        if (!_device)
                _device = _interface->getDevice();
        if (!discoverEndpoints()) {
                IOLog("RTL8814Native: ERROR - bulk endpoints not found\n");
                _interface->close(_owner);
                return false;
        }
        IOLog("RTL8814Native: endpoints OK (bulk in 0x%02x, %u bulk out eps [first 0x%02x], "
              "mps %u/%u)\n", _bulkInEp, _numBulkOut, _bulkOutEp, _maxPacketIn, _maxPacketOut);
        return true;
}

void RTLUsbDevice::closeProvider()
{
        if (_interface && _owner && _interface->isOpen(_owner))
                _interface->close(_owner);
}

bool RTLUsbDevice::discoverEndpoints()
{
        /*
         * The RTL8814AU USB layout (class vendor-specific):
         *   EP 0x84 IN  - BULK  (RX frames + events)
         *   EP 0x05/0x06/0x08 OUT - BULK (TX queues: High / Normal / Low)
         * We scan the active alternate setting, retain pipes for all Bulk OUT
         * endpoints, and expose linux-shaped descriptors for the HAL.
         */
        const StandardUSB::ConfigurationDescriptor *cd =
                _interface->getConfigurationDescriptor();
        const StandardUSB::InterfaceDescriptor *id =
                _interface->getInterfaceDescriptor();
        if (!cd || !id)
                return false;

        uint32_t speed = _device ? _device->getSpeed() : 0;
        const StandardUSB::EndpointDescriptor *ep = nullptr;

        while ((ep = StandardUSB::getNextEndpointDescriptor(cd, id, (const StandardUSB::Descriptor *)ep)) != nullptr) {
                uint8_t addr = StandardUSB::getEndpointAddress(ep);
                uint8_t type = StandardUSB::getEndpointType(ep);
                uint16_t mps = StandardUSB::getEndpointMaxPacketSize(speed, ep);

                if (type != kIOUSBEndpointTypeBulk)
                        continue;

                if (StandardUSB::getEndpointDirection(ep) == kEndpointDirectionIn) {   /* IN */
                        _bulkInEp = addr;
                        _maxPacketIn = mps;
                        _epDescIn.bLength = ep->bLength;
                        _epDescIn.bDescriptorType = ep->bDescriptorType;
                        _epDescIn.bEndpointAddress = addr;
                        _epDescIn.bmAttributes = ep->bmAttributes;
                        _epDescIn.wMaxPacketSize = mps;
                } else {                                                               /* OUT */
                        if (!_bulkOutEp) {
                                _bulkOutEp = addr;
                                _maxPacketOut = mps;
                                _epDescOut.bLength = ep->bLength;
                                _epDescOut.bDescriptorType = ep->bDescriptorType;
                                _epDescOut.bEndpointAddress = addr;
                                _epDescOut.bmAttributes = ep->bmAttributes;
                                _epDescOut.wMaxPacketSize = mps;
                        }
                        if (_numBulkOut < 4) {
                                IOUSBHostPipe *p = _interface->copyPipe(addr);
                                if (p) {
                                        _pipesOut[_numBulkOut] = p;
                                        _bulkOutEps[_numBulkOut] = addr & 0x0F;
                                        _numBulkOut++;
                                }
                        }
                }
        }

        if (!_bulkInEp || _numBulkOut == 0)
                return false;

        /* Grab the IN pipe for the session (copyPipe retains) */
        _pipeIn = _interface->copyPipe(_bulkInEp);
        _pipeOut = _pipesOut[0];
        if (!_pipeIn || !_pipeOut) {
                if (_pipeIn) { _pipeIn->release(); _pipeIn = nullptr; }
                for (int i = 0; i < 4; i++) {
                        if (_pipesOut[i]) {
                                _pipesOut[i]->release();
                                _pipesOut[i] = nullptr;
                        }
                }
                _pipeOut = nullptr;
                _numBulkOut = 0;
                return false;
        }

        return true;
}

/* ---------------------------------------------------------------------
 * Control transfers — the 8814A register access path (hal_usb.c)
 * --------------------------------------------------------------------- */
int RTLUsbDevice::controlTransfer(uint8_t requestType, uint8_t request,
                                  uint16_t value, uint16_t index,
                                  void *buffer, uint16_t length,
                                  uint32_t timeoutMs)
{
        if (!_interface)
                return -ENODEV;

        StandardUSB::DeviceRequest req;
        req.bmRequestType = requestType;
        req.bRequest = request;
        req.wValue = HostToUSB16(value);
        req.wIndex = HostToUSB16(index);
        req.wLength = HostToUSB16(length);

        uint32_t transferred = 0;
        uint32_t timeout = timeoutMs ? timeoutMs : kUSBHostDefaultControlCompletionTimeoutMS;

        IOReturn err = _interface->deviceRequest(req, buffer, transferred, timeout);
        if (err == kIOReturnSuccess)
                return (transferred > 0 || length == 0) ? (int)transferred : (int)length;
        if (err == kIOReturnNoDevice)
                return -ENODEV;

        IOLog("RTL8814Native: deviceRequest rt=0x%02x req=0x%02x "
              "val=0x%04x len=%u failed 0x%08x\n",
              requestType, request, value, length, err);
        return -EIO;
}

/* ---------------------------------------------------------------------
 * Synchronous bulk (used for 8814A firmware RSVD page download & sync I/O)
 * --------------------------------------------------------------------- */
int RTLUsbDevice::bulkTransfer(uint8_t endpoint, uint8_t direction,
                               void *buffer, uint32_t length,
                               uint32_t *transferred, uint32_t timeoutMs)
{
        bool is_in = (direction & 0x80) != 0;
        IOUSBHostPipe *pipe = is_in ? _pipeIn : _pipeOut;
        if (!is_in) {
                uint8_t epNum = endpoint & 0x0F;
                for (uint8_t i = 0; i < _numBulkOut; i++) {
                        if (_bulkOutEps[i] == epNum && _pipesOut[i]) {
                                pipe = _pipesOut[i];
                                break;
                        }
                }
        }
        if (!pipe)
                return -ENODEV;

        IODirection ioDir = is_in ? kIODirectionIn : kIODirectionOut;
        IOMemoryDescriptor *mem = IOMemoryDescriptor::withAddress(
                buffer, length, ioDir);
        if (!mem)
                return -ENOMEM;

        IOReturn prepErr = mem->prepare(ioDir);
        uint32_t done = 0;
        uint32_t timeout = timeoutMs ? timeoutMs : 1000;
        IOReturn err = pipe->io(mem, length, done, timeout);
        if (prepErr == kIOReturnSuccess)
                mem->complete(ioDir);
        mem->release();

        if (transferred)
                *transferred = done;

        if (err != kIOReturnSuccess) {
                IOLog("RTL8814Native: bulkTransfer ep=0x%02x dir=0x%02x len=%u failed 0x%08x\n",
                      endpoint, direction, length, err);
                return -EIO;
        }
        return (int)done;
}

/* ---------------------------------------------------------------------
 * Async URBs — the real data path
 *
 * The sidecar IOUSBHostCompletion is allocated by usb_alloc_urb()
 * (VendorGlue.cpp) with owner=urb; on transfer completion IOKit
 * invokes rtlUrbCompletion below, which translates status and calls
 * the vendor's urb->complete(urb) on the USB workloop — exactly like
 * a Linux URB callback running in softirq context. Vendor handlers
 * must therefore not sleep (they don't; they enqueue driver work).
 * --------------------------------------------------------------------- */
void rtlUrbCompletion(void *owner, void *parameter,
                      IOReturn status, uint32_t bytesTransferred)
{
        if (parameter) {
                IOMemoryDescriptor *mem = (IOMemoryDescriptor *)parameter;
                mem->complete();
                mem->release();
        }

        struct urb *urb = (struct urb *)owner;
        if (!urb)
                return;

        IOUSBHostCompletion *comp = (IOUSBHostCompletion *)urb->shim_priv;
        if (comp)
                comp->parameter = nullptr;

        urb->actual_length = bytesTransferred;
        switch (status) {
        case kIOReturnSuccess:
                urb->status = 0;
                break;
        case kIOReturnAborted:
                urb->status = -ECONNRESET;   /* vendor checks -ECONNRESET/-ENOENT */
                break;
        case kIOReturnNoDevice:
                urb->status = -ENODEV;
                break;
        default:
                urb->status = -EIO;
                break;
        }

        if (urb->complete)
                urb->complete(urb);

        urb->inflight = 0;
        __sync_synchronize();
        urb->done = 1;   /* killUrb() waits for this */
}

int RTLUsbDevice::submitUrb(struct urb *urb)
{
        if (!urb)
                return -EINVAL;

        bool is_in = (urb->pipe & 0x80) != 0;
        IOUSBHostPipe *pipe = is_in ? _pipeIn : _pipeOut;
        if (!is_in) {
                uint8_t epNum = (uint8_t)(urb->pipe & 0x0F);
                for (uint8_t i = 0; i < _numBulkOut; i++) {
                        if (_bulkOutEps[i] == epNum && _pipesOut[i]) {
                                pipe = _pipesOut[i];
                                break;
                        }
                }
        }
        if (!pipe)
                return -ENODEV;

        IOUSBHostCompletion *comp = (IOUSBHostCompletion *)urb->shim_priv;
        if (!comp) {
                IOLog("RTL8814Native: submitUrb - no completion sidecar\n");
                return -EINVAL;
        }

        IODirection ioDir = is_in ? kIODirectionIn : kIODirectionOut;
        IOMemoryDescriptor *mem = IOMemoryDescriptor::withAddress(
                urb->transfer_buffer,
                urb->transfer_buffer_length,
                ioDir);
        if (!mem) {
                urb->status = -ENOMEM;
                return -ENOMEM;
        }

        mem->prepare(ioDir);
        comp->parameter = mem;
        urb->done = 0;
        __sync_synchronize();
        urb->inflight = 1;
        IOReturn err = pipe->io(mem, urb->transfer_buffer_length, comp, 0);
        if (err != kIOReturnSuccess) {
                urb->inflight = 0;
                mem->complete(ioDir);
                mem->release();
                comp->parameter = nullptr;
                urb->status = -EIO;
                return -EIO;
        }

        return 0;
}

void RTLUsbDevice::killUrb(struct urb *urb)
{
        if (!urb)
                return;
        bool is_in = (urb->pipe & 0x80) != 0;
        IOUSBHostPipe *pipe = is_in ? _pipeIn : _pipeOut;
        if (!is_in) {
                uint8_t epNum = (uint8_t)(urb->pipe & 0x0F);
                for (uint8_t i = 0; i < _numBulkOut; i++) {
                        if (_bulkOutEps[i] == epNum && _pipesOut[i]) {
                                pipe = _pipesOut[i];
                                break;
                        }
                }
        }
        if (pipe)
                pipe->abort(IOUSBHostIOSource::kAbortAsynchronous, kIOReturnAborted, nullptr);
        /* Linux usb_kill_urb() is synchronous: when it returns the completion
         * has run and will not run again. Without the wait, vendor_remove() can
         * free the _adapter while a late completion still dereferences it, and
         * its tasklet/schedule_work lands in the kext-lifetime workqueue
         * pointing at freed memory (kernel panic in rtl_wq_thread:list_del). */
        if (urb->inflight) {
                for (int ms = 0; !urb->done && ms < 1000; ms++)
                        IOSleep(1);
                urb->inflight = 0;
        }
        /* Outstanding completions fire with kIOReturnAborted; rtlUrbCompletion
         * releases the memory descriptor and notifies the vendor complete handler. */
}

void RTLUsbDevice::killAllPipes()
{
        if (_pipeIn)  _pipeIn->abort(IOUSBHostIOSource::kAbortAsynchronous, kIOReturnAborted, nullptr);
        for (uint8_t i = 0; i < _numBulkOut; i++) {
                if (_pipesOut[i])
                        _pipesOut[i]->abort(IOUSBHostIOSource::kAbortAsynchronous, kIOReturnAborted, nullptr);
        }
}

struct usb_endpoint_descriptor *RTLUsbDevice::findBulkIn()
{
        return _bulkInEp ? &_epDescIn : nullptr;
}

struct usb_endpoint_descriptor *RTLUsbDevice::findBulkOut()
{
        return _bulkOutEp ? &_epDescOut : nullptr;
}
