/*
 * RTL8814Native — kext principal driver implementation
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include "RTL8814Native.hpp"
#include "RTLUsbDevice.hpp"
#include "RTLEthernetAdapter.hpp"
#include "RTLUserClient.hpp"
#include "VendorGlue.hpp"
#include "RTLRadioABI.h"

#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMessage.h>
#include <libkern/OSAtomic.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <libkern/libkern.h>
#include <linux/workqueue.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <stdarg.h>
#include <sys/errno.h>

#define super IOService
OSDefineMetaClassAndStructors(RTL8814Native, IOService)

/* Two states, no real power management yet: joining the power plane is
 * what makes IOKit deliver systemWillShutdown() */
static IOPMPowerState s_power_states[2] = {
        { kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { kIOPMPowerStateVersion1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn,
          0, 0, 0, 0, 0, 0, 0, 0 },
};

/*
 * The vendor radio state (g_padapter / g_dvobj, the compat workqueue) is
 * global, so only one instance may be inside start()/stop() at a time.
 * On an unplug -> replug (or the USB 3.0 re-enumeration) IOKit can start
 * the new instance while the old one's stop() is still tearing down; the
 * old rtl_workqueue_exit() then freed the new instance's queue lock
 * (use-after-free panic in rtl_schedule_work during firmware download).
 * Recursive because a failed start() calls stop() itself. Lives for the
 * kext's lifetime.
 */
static IORecursiveLock *volatile g_lifecycle_lock;

/*
 * The instance that owns the global vendor state and the compat workqueues
 * (under the lifecycle lock). When the dongle re-enumerates across sleep,
 * IOKit can start the new instance BEFORE it stops the old one. Without an
 * owner, the new probe overwrote g_padapter and rtl_workqueue_init() re-ran
 * on live queues: the old drain threads kept their cached queue lock while
 * the new threads and schedulers used a new one, so two threads popped the
 * same list unsynchronized (panic 03:34: rtl_wq_thread called the system
 * queue head as a work item, 1.1 s after wake). The new instance now
 * releases the stale owner first; the old stop() then skips the vendor part.
 */
static RTL8814Native *g_owner;

/* Every power transition, as "RTLPowerTrace" in the registry (phase 5:
 * did RTLAirInterface joining our power plane drive setPowerState?). */
static uint64_t pm_uptime_ms(void)
{
        uint64_t ns;
        absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
        return ns / 1000000ULL;
}

static void pm_trace(IOService *self, const char *fmt, ...)
{
        char line[160];
        int n = snprintf(line, sizeof(line), "@%llu ", pm_uptime_ms());
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line + n, sizeof(line) - n, fmt, ap);
        va_end(ap);

        OSArray *old = OSDynamicCast(OSArray, self->getProperty("RTLPowerTrace"));
        OSArray *arr = old ? OSArray::withArray(old, 0) : OSArray::withCapacity(8);
        OSString *str = OSString::withCString(line);
        if (arr && str) {
                if (arr->getCount() >= 64)
                        arr->removeObject(0);
                arr->setObject(str);
                self->setProperty("RTLPowerTrace", arr);
        }
        OSSafeReleaseNULL(str);
        OSSafeReleaseNULL(arr);
}

static IORecursiveLock *lifecycle_lock(void)
{
        if (!g_lifecycle_lock) {
                IORecursiveLock *l = IORecursiveLockAlloc();
                if (l && !OSCompareAndSwapPtr(nullptr, l, (void * volatile *)&g_lifecycle_lock))
                        IORecursiveLockFree(l);
        }
        return g_lifecycle_lock;
}

bool RTL8814Native::start(IOService *provider)
{
        IORecursiveLock *l = lifecycle_lock();
        if (!l)
                return false;
        IORecursiveLockLock(l);
        bool ok = startLocked(provider);
        IORecursiveLockUnlock(l);
        return ok;
}

/*
 * stop() and didTerminate() run on the IOKit termination thread INSIDE the
 * USB workloop gate (terminateWorker -> runAction(actionStop)). A newer
 * instance's start() holds the lifecycle lock while its USB requests wait
 * for that same gate, so blocking on the lock here deadlocked the whole
 * termination thread (spindump 07:14, 4.6f wake: IOServiceTerminateThread
 * in actionStop -> IORecursiveLockLock, IOConfigThread in our start() ->
 * IOCommandGate::runAction -> closeGate; every re-enumerated USB device
 * stayed "inactive, busy"). The termination side therefore only TRIES the
 * lock and never waits for it.
 */
void RTL8814Native::stop(IOService *provider)
{
        IORecursiveLock *l = lifecycle_lock();
        bool locked = l && IORecursiveLockTryLock(l);
        /* Not the owner: the radio was already released (didTerminate or a
         * newer start()), so only instance-local objects are left */
        if (l && !locked && g_owner == this) {
                IOLog("RTL8814Native: stop: still owns the radio, waiting for the lifecycle lock\n");
                IORecursiveLockLock(l);
                locked = true;
        }
        stopLocked(provider);
        if (locked)
                IORecursiveLockUnlock(l);
}

bool RTL8814Native::startLocked(IOService *provider)
{
        int probe_rc = 0;
        if (!super::start(provider))
                return false;

        IOLog("RTL8814Native: start (phase 0 skeleton build %s)\n",
              RTL8814NATIVE_VERSION);

        /*
         * PHASE 0 (this build): claim the device, discover endpoints,
         * publish a placeholder ethernet interface. The vendor radio
         * init chain is not ported yet — see Docs/ROADMAP.md.
         */

        IOUSBHostInterface *intf = OSDynamicCast(IOUSBHostInterface, provider);
        if (!intf) {
                IOLog("RTL8814Native: provider is not IOUSBHostInterface\n");
                return false;
        }

        _usb = RTLUsbDevice::withInterface(intf, this);
        if (!_usb)
                return false;

        if (!_usb->openProvider()) {
                _usb->release();
                _usb = nullptr;
                return false;
        }
        _providerOpened = true;

        if (g_owner && g_owner != this) {
                IOLog("RTL8814Native: previous instance still owns the radio, releasing it\n");
                g_owner->releaseVendorLocked();
        }

        /* Register the transport so VendorGlue C API can find it */
        rtl8814native_set_usb_device(_usb->toHandle());

        /* System workqueue for the compat layer */
        rtl_workqueue_init();
        g_owner = this;

        /*
         * PHASE 1 integration point (future):
         *   rtl8814native_vendor_probe(...)  — the port of
         *   os_dep/linux/usb_intf.c:rtw_drv_init() line 1499:
         *     dvobj_priv init → endpoint setup → rtw_hw_init →
         *     firmware download (hal8814a_fw.c: array_mp_8814a_fw_nic) →
         *     start RTW_CMD_THREAD / RTW_RECV_THREAD / RTW_XMIT_THREAD
         */

        /* Create the ethernet adapter early so netdev exists for vendor probe */
        _adapter = RTLEthernetAdapter::withDriver(this);
        if (!_adapter) {
                IOLog("RTL8814Native: failed to create ethernet adapter\n");
                goto fail;
        }

        /* Hand the netdev to the glue so vendor probe can populate dev_addr */
        rtl8814native_set_netdev(_adapter->netdev());

        /* Phase 1: Probe and initialize the vendor radio & read hardware MAC */
        probe_rc = rtl8814native_vendor_probe(_usb->toHandle(), 0);
        if (probe_rc == RTL_PROBE_USB3_SWITCH) {
                /* The chip is leaving the bus to come back as USB 3.0; a new
                 * instance will match it. Nothing is attached yet, so drop the
                 * unattached adapter and fail start. */
                IOLog("RTL8814Native: USB 3.0 mode switch issued, waiting for re-enumeration\n");
                rtl8814native_set_netdev(nullptr);
                _adapter->release();
                _adapter = nullptr;
                goto fail;
        }
        if (probe_rc != 0) {
                IOLog("RTL8814Native: vendor radio probe returned %d\n", probe_rc);
        } else {
                IOLog("RTL8814Native: vendor radio probe succeeded\n");
        }

        /* Now attach and start adapter: publishes interface with real factory MAC */
        if (!_adapter->attach(this) || !_adapter->start(this)) {
                _adapter->release();
                _adapter = nullptr;
                goto fail;
        }

        PMinit();
        provider->joinPMtree(this);
        if (registerPowerDriver(this, s_power_states, 2) == kIOReturnSuccess)
                _pmInited = true;
        else
                IOLog("RTL8814Native: registerPowerDriver failed, no shutdown hook\n");
        /* Our own desire keeps the radio on. Without it the state followed the
         * children: RTL8814AirLayer's interface joins our power plane at state
         * 0, PM ran setPowerState(0) -> vendor_suspend 10 ms after it, and the
         * resume 5.6 s later left bulk IN dead (RTLPowerTrace, 4.12). System
         * sleep still lowers us through the power domain. */
        if (_pmInited)
                changePowerStateTo(1);
        pm_trace(this, "registerPowerDriver %s, state %lu",
                 _pmInited ? "ok" : "FAILED", (unsigned long)getPowerState());

        registerService();

        /* A dongle that re-enumerated across sleep (or was replugged) comes
         * back as a new instance, so the old instance's wake path cannot
         * re-join. Not only on a takeover: on 4.6g the old didTerminate
         * released the radio first, took_over stayed false and the link was
         * left DISCONNECTED. No-op until a join succeeded in this kext
         * lifetime, so a cold boot is unaffected. */
        if (probe_rc == 0)
                rtl8814native_rejoin_last();

        IOLog("RTL8814Native: started - device claimed, radio initialized\n");
        return true;

fail:
        stop(provider);
        return false;
}

void RTL8814Native::stopLocked(IOService *provider)
{
        IOLog("RTL8814Native: stop\n");

        if (_pmInited) {
                PMstop();
                _pmInited = false;
        }

        bool owner = (g_owner == this);
        if (owner) {
                rtl8814native_vendor_remove();
                rtl8814native_set_usb_device(nullptr);
        }

        if (_userClient) {
                _userClient = nullptr;
        }

        if (_adapter) {
                _adapter->detach(this);
                _adapter->release();
                _adapter = nullptr;
        }

        if (owner) {
                rtl_sae_join_exit();
                rtl_workqueue_exit();
                g_owner = nullptr;
        }

        if (_usb) {
                if (_providerOpened)
                        _usb->closeProvider();
                _usb->release();
                _usb = nullptr;
        }

        super::stop(provider);
}

/* Tear down the vendor state and workqueues this (stale) instance owns, on
 * its own USB handle, so a newer instance can probe. Lifecycle lock held. */
void RTL8814Native::releaseVendorLocked()
{
        if (g_owner != this)
                return;
        rtl8814native_vendor_remove();
        rtl8814native_set_usb_device(nullptr);
        rtl_sae_join_exit();
        rtl_workqueue_exit();
        g_owner = nullptr;
}

void RTL8814Native::systemWillShutdown(IOOptionBits specifier)
{
        /* A restart keeps the dongle powered; leaving the chip running makes
         * the next boot calibrate a live chip (see Milestone 4.4) */
        if (specifier == kIOMessageSystemWillPowerOff ||
            specifier == kIOMessageSystemWillRestart) {
                IORecursiveLock *l = lifecycle_lock();
                if (l)
                        IORecursiveLockLock(l);
                if (g_owner == this)
                        rtl8814native_vendor_shutdown();
                if (l)
                        IORecursiveLockUnlock(l);
        }
        super::systemWillShutdown(specifier);
}

IOReturn RTL8814Native::setPowerState(unsigned long stateNumber,
                                      IOService *whatDevice)
{
        /* Sleep keeps the dongle powered on this machine (VBUS stays up), so
         * state 0 runs the same clean card-disable as shutdown and state 1
         * re-runs HAL init + firmware + threads, then re-joins. If the device
         * instead vanished across sleep, IOKit terminates this instance and
         * the fresh start()/probe() handles the re-enumerated hardware. */
        IORecursiveLock *l = lifecycle_lock();
        if (l)
                IORecursiveLockLock(l);
        int rc = 0;
        if (g_owner != this)
                rc = 1000; /* a newer instance took the radio over */
        else if (stateNumber == 0)
                rc = rtl8814native_vendor_suspend();
        else if (stateNumber >= 1)
                rc = rtl8814native_vendor_resume();
        if (l)
                IORecursiveLockUnlock(l);
        pm_trace(this, "setPowerState(%lu) by %s -> %d, now %lu",
                 stateNumber, whatDevice ? whatDevice->getMetaClass()->getClassName() : "?",
                 rc, (unsigned long)getPowerState());
        return super::setPowerState(stateNumber, whatDevice);
}

/* Off the termination thread: wait for the lifecycle lock (a newer
 * start() may hold it while it needs the USB gate), then release the radio
 * and close the provider so IOKit can go on to stop() */
static void deferred_terminate(void *arg)
{
        RTL8814Native *self = (RTL8814Native *)arg;
        IORecursiveLock *l = lifecycle_lock();
        IORecursiveLockLock(l);
        self->releaseAndCloseLocked();
        IORecursiveLockUnlock(l);
        self->release();
        thread_terminate(current_thread());
}

void RTL8814Native::releaseAndCloseLocked()
{
        releaseVendorLocked();
        if (_usb && _providerOpened) {
                _usb->closeProvider();
                _providerOpened = false;
        }
}

bool RTL8814Native::didTerminate(IOService *provider, IOOptionBits options,
                                 bool *defer)
{
        /* IOKit only calls stop() once every open on the provider is closed.
         * Without this, a dongle that re-enumerated across sleep left the old
         * instance "inactive, busy 1" forever (seen on the first 4.6c wake):
         * stop() never ran and its objects leaked until reboot. */
        IORecursiveLock *l = lifecycle_lock();
        if (l && !IORecursiveLockTryLock(l)) {
                /* Busy (a newer instance is starting): see stop() for why
                 * this thread must not wait. The provider stays open until
                 * the helper closes it, which is when IOKit calls stop(). */
                thread_t thr;
                retain();
                if (kernel_thread_start((thread_continue_t)deferred_terminate,
                                        this, &thr) == KERN_SUCCESS) {
                        thread_deallocate(thr);
                        IOLog("RTL8814Native: didTerminate: lifecycle lock busy, release deferred\n");
                        return super::didTerminate(provider, options, defer);
                }
                release();
                IORecursiveLockLock(l);
        }
        releaseAndCloseLocked();
        if (l)
                IORecursiveLockUnlock(l);
        return super::didTerminate(provider, options, defer);
}

/* Single-client policy: only the app's user client may open us. */
bool RTL8814Native::handleOpen(IOService *forClient, IOOptionBits options,
                               void *arg)
{
        return (forClient == (IOService *)_userClient)
            ? true
            : super::handleOpen(forClient, options, arg);
}

void RTL8814Native::handleClose(IOService *forClient, IOOptionBits options)
{
        super::handleClose(forClient, options);
}

/*
 * Radio ABI (RTLRadioABI.h). Only the instance that owns the radio answers;
 * a stale instance during a wake/replug takeover returns kIOReturnNotReady.
 */
IOReturn RTL8814Native::callPlatformFunction(const OSSymbol *functionName,
                                             bool waitForFunction,
                                             void *param1, void *param2,
                                             void *param3, void *param4)
{
        if (!functionName)
                return kIOReturnBadArgument;
        const char *fn = functionName->getCStringNoCopy();
        if (strncmp(fn, "rtlRadio", 8) != 0)
                return super::callPlatformFunction(functionName, waitForFunction,
                                                   param1, param2, param3, param4);

        if (functionName->isEqualTo(RTL_RADIO_FN_ABI)) {
                if (!param1)
                        return kIOReturnBadArgument;
                *(uint32_t *)param1 = RTL_RADIO_ABI;
                return kIOReturnSuccess;
        }
        if (g_owner != this || !_adapter)
                return kIOReturnNotReady;

        if (functionName->isEqualTo(RTL_RADIO_FN_SCAN_START))
                return rtl8814native_trigger_scan() == 0 ? kIOReturnSuccess : kIOReturnBusy;
        if (functionName->isEqualTo(RTL_RADIO_FN_SCAN_COPY)) {
                if (!param1 || !param2)
                        return kIOReturnBadArgument;
                return rtl8814native_radio_scan_copy((struct rtl_radio_bss *)param1,
                                                     (uint32_t *)param2) == 0
                       ? kIOReturnSuccess : kIOReturnNotReady;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_CHANNELS)) {
                if (!param1 || !param2)
                        return kIOReturnBadArgument;
                return rtl8814native_radio_channels((struct rtl_radio_chan *)param1,
                                                    (uint32_t *)param2) == 0
                       ? kIOReturnSuccess : kIOReturnNotReady;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_JOIN)) {
                int rc = rtl8814native_radio_join((struct rtl_radio_join *)param1);
                return rc == 0 ? kIOReturnSuccess
                       : rc == -EINVAL ? kIOReturnBadArgument
                       : rc == -EOPNOTSUPP ? kIOReturnUnsupported : kIOReturnError;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_STATUS)) {
                if (!param1)
                        return kIOReturnBadArgument;
                struct rtl_radio_status *st = (struct rtl_radio_status *)param1;
                rtl8814native_radio_status(st);
                st->hook_attached = _adapter->dataHookAttached() ? 1 : 0;
                return kIOReturnSuccess;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_LEAVE)) {
                rtl8814native_disconnect();
                return kIOReturnSuccess;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_MAC)) {
                if (!param1)
                        return kIOReturnBadArgument;
                rtl8814native_radio_mac((uint8_t *)param1);
                return kIOReturnSuccess;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_SET_MAC)) {
                int rc = rtl8814native_radio_set_mac((const uint8_t *)param1);
                if (rc == 0)
                        _adapter->setMacAddress((const uint8_t *)param1);
                return rc == 0 ? kIOReturnSuccess
                       : rc == -EBUSY ? kIOReturnBusy
                       : rc == -EINVAL ? kIOReturnBadArgument : kIOReturnNotReady;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_DATA_HOOK)) {
                _adapter->setDataHook((rtl_radio_rx_fn)param1, param2);
                return kIOReturnSuccess;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_AWDL_HOOK)) {
                _adapter->setAwdlHook((rtl_radio_awdl_rx_fn)param1, param2);
                return kIOReturnSuccess;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_AWDL_PORT))
                return rtl8814native_radio_awdl_port((const uint8_t *)param1) == 0
                       ? kIOReturnSuccess : kIOReturnNotReady;
        if (functionName->isEqualTo(RTL_RADIO_FN_SET_CHANNEL)) {
                if (!param1 || *(const uint32_t *)param1 > 0xFF)
                        return kIOReturnBadArgument;
                int rc = rtl8814native_radio_set_channel((uint8_t)*(const uint32_t *)param1);
                return rc == 0 ? kIOReturnSuccess
                       : rc == -EINVAL ? kIOReturnBadArgument
                       : rc == -EBUSY ? kIOReturnBusy
                       : rc == -ENOMEM ? kIOReturnNoResources : kIOReturnNotReady;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_AWDL_OFFCH)) {
                if (!param1 || *(const uint32_t *)param1 > 0xFF)
                        return kIOReturnBadArgument;
                int rc = rtl8814native_radio_awdl_offch((uint8_t)*(const uint32_t *)param1);
                return rc == 0 ? kIOReturnSuccess
                       : rc == -EINVAL ? kIOReturnBadArgument
                       : rc == -EBUSY ? kIOReturnBusy : kIOReturnNotReady;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_TX_80211)) {
                if (!param1 || !param2)
                        return kIOReturnBadArgument;
                int rc = rtl8814native_radio_tx_80211((const uint8_t *)param1,
                                                      *(const uint32_t *)param2);
                return rc == 0 ? kIOReturnSuccess
                       : rc == -EINVAL ? kIOReturnBadArgument
                       : rc == -EBUSY ? kIOReturnBusy
                       : rc == -ENOMEM ? kIOReturnNoResources : kIOReturnNotReady;
        }
        if (functionName->isEqualTo(RTL_RADIO_FN_TX)) {
                if (!param1)
                        return kIOReturnBadArgument;
                return _adapter->transmitFrame((mbuf_t)param1);
        }
        return kIOReturnUnsupported;
}

IOReturn RTL8814Native::newUserClient(task_t owningTask,
                                      void *securityID,
                                      UInt32 type,
                                      IOUserClient **handler)
{
        (void)securityID;
        RTLUserClient *uc = RTLUserClient::withTask(owningTask, this, type);
        if (uc && !uc->attach(this)) {
                uc->release();
                uc = nullptr;
        }
        if (uc && !uc->start(this)) {
                uc->detach(this);
                uc->release();
                uc = nullptr;
        }
        if (!uc)
                return kIOReturnNoResources;

        _userClient = uc;
        *handler = uc;
        return kIOReturnSuccess;
}
