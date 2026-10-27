/*
 * RTL8814Native — kext principal driver
 *
 * IOService subclass matched by Info.plist against IOUSBHostInterface
 * for the RTL8814AU family. Owns the RTLUsbDevice transport and the
 * RTLEthernetAdapter interface, and drives the vendor glue
 * (rtl8814native_vendor_probe/remove) through the port phases.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_NATIVE_DRIVER_H
#define _RTL8814_NATIVE_DRIVER_H

#include <IOKit/IOService.h>

class RTLUsbDevice;
class RTLEthernetAdapter;
class RTLUserClient;

class RTL8814Native : public IOService {
        OSDeclareDefaultStructors(RTL8814Native)

public:
        virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
        virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
        virtual bool handleOpen(IOService *forClient, IOOptionBits options,
                                void *arg) APPLE_KEXT_OVERRIDE;
        virtual void handleClose(IOService *forClient, IOOptionBits options)
                                APPLE_KEXT_OVERRIDE;
        /* Modern SDK form — the old IOUserClient*-returning variant was
         * removed from IOService; kexts implement this one */
        virtual IOReturn newUserClient(task_t owningTask, void *securityID,
                                        UInt32 type, IOUserClient **handler)
                                        APPLE_KEXT_OVERRIDE;
        /* Power-plane member only so IOKit calls this on power-off/restart */
        virtual void systemWillShutdown(IOOptionBits specifier) APPLE_KEXT_OVERRIDE;
        /* Sleep (state 0) powers the radio off; wake (state 1) re-inits it */
        virtual IOReturn setPowerState(unsigned long stateNumber,
                                       IOService *whatDevice) APPLE_KEXT_OVERRIDE;
        /* Device gone: release the radio and close the USB interface so
         * IOKit can finish terminating us and call stop() */
        virtual bool didTerminate(IOService *provider, IOOptionBits options,
                                  bool *defer) APPLE_KEXT_OVERRIDE;

        /* Radio ABI entry for another kext (RTLRadioABI.h) */
        virtual IOReturn callPlatformFunction(const OSSymbol *functionName,
                                              bool waitForFunction,
                                              void *param1, void *param2,
                                              void *param3, void *param4)
                                              APPLE_KEXT_OVERRIDE;
        using IOService::callPlatformFunction;

        RTLUsbDevice *usbDevice() { return _usb; }
        RTLEthernetAdapter *adapter() { return _adapter; }

        /* Set by RTLUserClient creations so the app can attach */
        void setUserClient(RTLUserClient *uc) { _userClient = uc; }

        /* Lifecycle lock held: release the radio if we own it and close the
         * USB interface (didTerminate and its deferred helper) */
        void releaseAndCloseLocked();

private:
        bool startLocked(IOService *provider);
        void stopLocked(IOService *provider);
        void releaseVendorLocked();

        RTLUsbDevice       *_usb;
        RTLEthernetAdapter *_adapter;
        RTLUserClient      *_userClient;
        bool                _providerOpened;
        bool                _pmInited;
};

#endif /* _RTL8814_NATIVE_DRIVER_H */
