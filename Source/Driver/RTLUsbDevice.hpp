/*
 * RTL8814Native — USB transport over IOUSBHostFamily
 *
 * This class owns the IOKit USB plumbing: endpoint discovery, the
 * control pipe (register I/O for the 8814A HAL) and the bulk in/out
 * pipes (frame TX/RX). The vendor C code never sees IOKit — it calls
 * the linux/usb.h shim, which lands in the extern "C" functions at
 * the bottom of this file.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_USB_DEVICE_H
#define _RTL8814_USB_DEVICE_H

#include <IOKit/IOService.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostPipe.h>

#include <linux/usb.h>

/* URB completion trampoline (defined in RTLUsbDevice.cpp). Declared
 * at file scope so ordinary lookup finds it (a bare friend declaration
 * inside the class is only visible via ADL — VendorGlue.cpp needs this). */
void rtlUrbCompletion(void *owner, void *parameter,
                      IOReturn status, uint32_t bytesTransferred);

class RTLUsbDevice : public OSObject {
        OSDeclareDefaultStructors(RTLUsbDevice)

public:
        static RTLUsbDevice *withInterface(IOUSBHostInterface *interface,
                                           IOService *owner);

        /* OSObject::init() takes no arguments — this is a plain helper,
         * NOT an override (initWithInterface calls super::init() itself) */
        bool initWithInterface(IOUSBHostInterface *interface,
                               IOService *owner);
        void free() APPLE_KEXT_OVERRIDE;

        /* URB completion trampoline — implemented in RTLUsbDevice.cpp,
         * wired into every sidecar by VendorGlue.cpp (usb_alloc_urb) */
        friend void rtlUrbCompletion(void *owner, void *parameter,
                                     IOReturn status, uint32_t bytesTransferred);
        bool openProvider();
        void closeProvider();

        /* Endpoint info discovered at start (for the usb_intf.c port) */
        uint8_t bulkInEndpoint()  const { return _bulkInEp; }
        uint8_t bulkOutEndpoint() const { return _bulkOutEp; }
        uint8_t numBulkOut()      const { return _numBulkOut; }
        uint8_t bulkOutEndpointAt(uint8_t idx) const { return (idx < _numBulkOut) ? _bulkOutEps[idx] : 0; }
        uint16_t maxPacketIn()    const { return _maxPacketIn; }
        uint16_t maxPacketOut()   const { return _maxPacketOut; }

        uint16_t vendorId()  const { return _idVendor; }
        uint16_t productId() const { return _idProduct; }

        /* --- C API consumed by the Compat layer (struct usb_device) --- */
        static RTLUsbDevice *fromHandle(struct usb_device *h) { return (RTLUsbDevice *)h; }
        struct usb_device *toHandle() { return (struct usb_device *)this; }

        int  controlTransfer(uint8_t requestType, uint8_t request,
                             uint16_t value, uint16_t index,
                             void *buffer, uint16_t length,
                             uint32_t timeoutMs);
        int  bulkTransfer(uint8_t endpoint, uint8_t direction,
                          void *buffer, uint32_t length,
                          uint32_t *transferred, uint32_t timeoutMs);
        int  submitUrb(struct urb *urb);
        void killUrb(struct urb *urb);
        void killAllPipes();

        struct usb_endpoint_descriptor *findBulkIn();
        struct usb_endpoint_descriptor *findBulkOut();

private:
        IOService          *_owner;
        IOUSBHostInterface *_interface;
        IOUSBHostDevice    *_device;
        IOUSBHostPipe      *_pipeIn;
        IOUSBHostPipe      *_pipeOut;
        IOUSBHostPipe      *_pipesOut[4];

        uint8_t  _bulkInEp;
        uint8_t  _bulkOutEp;
        uint8_t  _bulkOutEps[4];
        uint8_t  _numBulkOut;
        uint16_t _maxPacketIn;
        uint16_t _maxPacketOut;
        uint16_t _idVendor;
        uint16_t _idProduct;

        struct usb_endpoint_descriptor _epDescIn;
        struct usb_endpoint_descriptor _epDescOut;

        bool discoverEndpoints();
};

#endif /* _RTL8814_USB_DEVICE_H */
