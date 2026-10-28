/*
 * RTL8814Native — user client for the RTLWifiTahoe app
 *
 * ABI: scalar in/out externalMethod calls, one selector per control
 * operation. The app replaces its RtWlanU OID UserClient with this
 * interface; scan/join/status map to the vendor MLME instead of OIDs.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_USER_CLIENT_H
#define _RTL8814_USER_CLIENT_H

#include <IOKit/IOUserClient.h>

class RTL8814Native;

/* Selector ABI (keep in sync with the app) */
enum {
        kRTLUC_GetVersion    = 0,   /* out: u32 version, u32 phase       */
        kRTLUC_GetLinkStatus = 1,   /* out: u32 state, u32 rssi          */
        kRTLUC_TriggerScan   = 2,   /* in: none (phase 2)                */
        kRTLUC_GetScanResult = 3,   /* out: entries (phase 2)            */
        kRTLUC_JoinNetwork   = 4,   /* in: ssid/psk struct (phase 2)     */
        kRTLUC_Disconnect    = 5,   /* in: none                          */
        kRTLUC_GetRadioInfo  = 6,   /* out: probe_rc, sys_cfg, chip/cut, mac_hi, mac_lo, fw_ready */
        kRTLUC_RegAccess     = 7,   /* in: op, width, addr, value; out: value */
        kRTLUC_GetRxLog      = 8,   /* out: struct rtl8814_rx_log        */
        kRTLUC_RadioCall     = 9,   /* in: op (RTL_UC_RADIO_*), struct in/out: radio ABI call through callPlatformFunction */
        kRTLUC_GetJoinLog    = 10,  /* out: struct rtl_jlog (RTLJoinLog.h) */
        kRTLUC_SetKnob       = 11,  /* in: knob id, value (live experiments, VendorProbe.c) */
        kRTLUC_SelCount
};

/* kRTLUC_RadioCall ops (scalarInput[0]) */
enum {
        RTL_UC_RADIO_ABI = 0,        /* scalar out[0]: RTL_RADIO_ABI */
        RTL_UC_RADIO_SCAN_START = 1,
        RTL_UC_RADIO_SCAN_COPY = 2,  /* struct out: rtl_radio_bss[]; scalar out[0]: count */
        RTL_UC_RADIO_CHANNELS = 3,   /* struct out: rtl_radio_chan[]; scalar out[0]: count */
        RTL_UC_RADIO_JOIN = 4,       /* struct in: rtl_radio_join */
        RTL_UC_RADIO_STATUS = 5,     /* struct out: rtl_radio_status */
        RTL_UC_RADIO_LEAVE = 6,
        RTL_UC_RADIO_MAC = 7,        /* struct out: 6 bytes */
};

struct rtl_uc_join_request {
        char ssid[33];
        char psk[64];      /* empty = open network */
        uint32_t auth_mode;    /* 0 open, 1 wpa2-psk, (2 sae — phase 4) */
};

class RTLUserClient : public IOUserClient {
        OSDeclareDefaultStructors(RTLUserClient)

public:
        static RTLUserClient *withTask(task_t task, RTL8814Native *driver,
                                       UInt32 type);

        virtual bool initWithTask(task_t owningTask, void *securityToken,
                                  UInt32 type) APPLE_KEXT_OVERRIDE;
        virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
        virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
        virtual void free() APPLE_KEXT_OVERRIDE;
        virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
        virtual IOReturn clientDied(void) APPLE_KEXT_OVERRIDE;

        virtual IOReturn externalMethod(uint32_t selector,
                                        IOExternalMethodArguments *args,
                                        IOExternalMethodDispatch *dispatch,
                                        OSObject *target, void *reference)
                                        APPLE_KEXT_OVERRIDE;

private:
        IOReturn radioCall(IOExternalMethodArguments *args);
        RTL8814Native *_driver;
        task_t         _task;
};

#endif /* _RTL8814_USER_CLIENT_H */
