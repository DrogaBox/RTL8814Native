/*
 * RTL8814Native — user client implementation
 *
 * PHASE 0: version + link status answer; scan/join return
 * kIOReturnUnsupported until the MLME glue lands (phase 2).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include "RTLUserClient.hpp"
#include "RTL8814Native.hpp"
#include "VendorGlue.hpp"
#include "RTLRadioABI.h"
#include "RTLJoinLog.h"

#include <IOKit/IOLib.h>
#include <libkern/libkern.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(RTLUserClient, IOUserClient)

RTLUserClient *RTLUserClient::withTask(task_t task, RTL8814Native *driver,
                                       UInt32 type)
{
        RTLUserClient *self = OSTypeAlloc(RTLUserClient);
        if (self && !self->initWithTask(task, driver, type)) {
                self->release();
                return nullptr;
        }
        return self;
}

bool RTLUserClient::initWithTask(task_t owningTask, void *securityToken,
                                 UInt32 type)
{
        if (!super::initWithTask(owningTask, securityToken, type))
                return false;
        if (!owningTask)
                return false;
        _task = owningTask;
        _driver = nullptr;
        return true;
}

bool RTLUserClient::start(IOService *provider)
{
        _driver = OSDynamicCast(RTL8814Native, provider);
        if (!_driver)
                return false;
        if (!super::start(provider))
                return false;
        IOLog("RTL8814Native: user client attached\n");
        return true;
}

void RTLUserClient::stop(IOService *provider)
{
        super::stop(provider);
}

void RTLUserClient::free()
{
        if (_driver)
                _driver->setUserClient(nullptr);
        super::free();
}

IOReturn RTLUserClient::clientClose(void)
{
        /* Detach this client; the driver itself stays for the device
         * lifetime (OC reload). App reconnects on next use. */
        terminate();
        return kIOReturnSuccess;
}

IOReturn RTLUserClient::clientDied(void)
{
        return clientClose();
}

IOReturn RTLUserClient::externalMethod(uint32_t selector,
                                       IOExternalMethodArguments *args,
                                       IOExternalMethodDispatch *dispatch,
                                       OSObject *target, void *reference)
{
        (void)dispatch; (void)target; (void)reference;

        if (!args || selector >= kRTLUC_SelCount)
                return kIOReturnBadArgument;

        switch (selector) {
        case kRTLUC_GetVersion:
                if (args->scalarOutput && args->scalarOutputCount >= 2) {
                        args->scalarOutput[0] = RTL8814NATIVE_UC_VERSION;
                        args->scalarOutput[1] = RTL8814NATIVE_PHASE;
                        return kIOReturnSuccess;
                }
                return kIOReturnBadArgument;

        case kRTLUC_GetLinkStatus: {
                struct rtl8814_link_status st;
                rtl8814native_get_link_status(&st);

                if (args->scalarOutput && args->scalarOutputCount >= 2) {
                        args->scalarOutput[0] = st.connected;
                        args->scalarOutput[1] = (uint64_t)(int64_t)st.rssi;
                        if (args->scalarOutputCount >= 4) {
                                args->scalarOutput[2] = st.channel;
                                args->scalarOutput[3] = st.wpa2_state;
                        }
                }
                if (args->structureOutput && args->structureOutputSize >= sizeof(st)) {
                        memcpy(args->structureOutput, &st, sizeof(st));
                        args->structureOutputSize = sizeof(st);
                }
                return kIOReturnSuccess;
        }

        case kRTLUC_TriggerScan: {
                int rc = rtl8814native_trigger_scan();
                return (rc == 0) ? kIOReturnSuccess : kIOReturnBusy;
        }

        case kRTLUC_GetScanResult: {
                if (!args->structureOutput || args->structureOutputSize < sizeof(struct rtl_scan_entry))
                        return kIOReturnBadArgument;

                uint32_t max_entries = (uint32_t)(args->structureOutputSize / sizeof(struct rtl_scan_entry));
                if (max_entries > RTL_MAX_SCAN_ENTRIES)
                        max_entries = RTL_MAX_SCAN_ENTRIES;

                struct rtl_scan_entry *entries = (struct rtl_scan_entry *)args->structureOutput;
                uint32_t count = 0;
                rtl8814native_get_scan_results(entries, max_entries, &count);

                if (args->scalarOutput && args->scalarOutputCount >= 1)
                        args->scalarOutput[0] = count;
                return kIOReturnSuccess;
        }

        case kRTLUC_JoinNetwork: {
                if (!args->structureInput || args->structureInputSize < sizeof(struct rtl_uc_join_request))
                        return kIOReturnBadArgument;

                struct rtl_uc_join_request req;
                memcpy(&req, args->structureInput, sizeof(req));
                req.ssid[32] = '\0';
                req.psk[63] = '\0';

                int rc = rtl8814native_join_network(req.ssid, req.psk, req.auth_mode);
                return (rc == 0) ? kIOReturnSuccess : kIOReturnError;
        }

        case kRTLUC_Disconnect: {
                rtl8814native_disconnect();
                return kIOReturnSuccess;
        }

        case kRTLUC_GetRadioInfo: {
                if (args->scalarOutput && args->scalarOutputCount >= 6) {
                        struct rtl8814_radio_telemetry telem;
                        rtl8814native_get_radio_telemetry(&telem);

                        args->scalarOutput[0] = (uint64_t)(int64_t)telem.probe_status;
                        args->scalarOutput[1] = (uint64_t)telem.reg_sys_cfg;
                        args->scalarOutput[2] = ((uint64_t)telem.chip_type) |
                                                (((uint64_t)telem.cut_version) << 8) |
                                                (((uint64_t)(uint32_t)telem.hal_init_status) << 16);
                        uint64_t mac_hi = ((uint64_t)telem.mac[0] << 24) |
                                          ((uint64_t)telem.mac[1] << 16) |
                                          ((uint64_t)telem.mac[2] << 8)  |
                                          ((uint64_t)telem.mac[3]);
                        uint64_t mac_lo = ((uint64_t)telem.mac[4] << 8)  |
                                          ((uint64_t)telem.mac[5]);
                        args->scalarOutput[3] = mac_hi;
                        args->scalarOutput[4] = mac_lo | (((uint64_t)telem.fw_ready) << 32);
                        args->scalarOutput[5] = RTL8814NATIVE_PHASE;
                        return kIOReturnSuccess;
                }
                return kIOReturnBadArgument;
        }

        case kRTLUC_RegAccess: {
                if (!args->scalarInput || args->scalarInputCount < 4 ||
                    !args->scalarOutput || args->scalarOutputCount < 1)
                        return kIOReturnBadArgument;
                uint32_t val = 0;
                int rc = rtl8814native_reg_access((uint32_t)args->scalarInput[0],
                                                  (uint32_t)args->scalarInput[1],
                                                  (uint32_t)args->scalarInput[2],
                                                  (uint32_t)args->scalarInput[3], &val);
                if (rc)
                        return kIOReturnBadArgument;
                args->scalarOutput[0] = val;
                return kIOReturnSuccess;
        }

        case kRTLUC_RadioCall:
                return radioCall(args);

        case kRTLUC_GetRxLog: {
                if (!args->structureOutput || args->structureOutputSize < sizeof(struct rtl8814_rx_log))
                        return kIOReturnBadArgument;
                rtl8814native_get_rx_log((struct rtl8814_rx_log *)args->structureOutput);
                args->structureOutputSize = sizeof(struct rtl8814_rx_log);
                return kIOReturnSuccess;
        }

        case kRTLUC_GetJoinLog: {
                if (!args->structureOutput || args->structureOutputSize < sizeof(struct rtl_jlog))
                        return kIOReturnBadArgument;
                rtl8814native_get_join_log((struct rtl_jlog *)args->structureOutput);
                args->structureOutputSize = sizeof(struct rtl_jlog);
                return kIOReturnSuccess;
        }

        default:
                return kIOReturnUnsupported;
        }
}

/*
 * kRTLUC_RadioCall: exercises the radio ABI from test_uc through the same
 * callPlatformFunction entry the IO80211 layer kext will use. Scan results
 * exceed the inline struct limit, so outputs may arrive as a descriptor.
 */
IOReturn RTLUserClient::radioCall(IOExternalMethodArguments *args)
{
        static const char *const names[] = {
                RTL_RADIO_FN_ABI, RTL_RADIO_FN_SCAN_START, RTL_RADIO_FN_SCAN_COPY,
                RTL_RADIO_FN_CHANNELS, RTL_RADIO_FN_JOIN, RTL_RADIO_FN_STATUS,
                RTL_RADIO_FN_LEAVE, RTL_RADIO_FN_MAC,
        };
        if (!_driver || !args->scalarInput || args->scalarInputCount < 1)
                return kIOReturnBadArgument;
        uint64_t op = args->scalarInput[0];
        if (op >= sizeof(names) / sizeof(names[0]))
                return kIOReturnBadArgument;

        IOMemoryDescriptor *desc = args->structureOutputDescriptor;
        uint32_t outCap = desc ? (uint32_t)args->structureOutputDescriptorSize
                               : args->structureOutputSize;
        if (outCap > 64 * sizeof(struct rtl_radio_bss))
                outCap = 64 * sizeof(struct rtl_radio_bss);
        void *out = NULL;
        if (outCap) {
                out = IOMalloc(outCap);
                if (!out)
                        return kIOReturnNoMemory;
                bzero(out, outCap);
        }

        IOReturn ret;
        uint32_t used = 0, count = 0, abi = 0;
        switch (op) {
        case RTL_UC_RADIO_ABI:
                ret = _driver->callPlatformFunction(names[op], false, &abi, NULL, NULL, NULL);
                count = abi;
                break;
        case RTL_UC_RADIO_SCAN_COPY:
        case RTL_UC_RADIO_CHANNELS: {
                uint32_t esz = op == RTL_UC_RADIO_SCAN_COPY ? sizeof(struct rtl_radio_bss)
                                                            : sizeof(struct rtl_radio_chan);
                count = outCap / esz;
                ret = out ? _driver->callPlatformFunction(names[op], false, out, &count, NULL, NULL)
                          : kIOReturnBadArgument;
                used = ret == kIOReturnSuccess ? count * esz : 0;
                break;
        }
        case RTL_UC_RADIO_JOIN: {
                struct rtl_radio_join j;
                if (!args->structureInput || args->structureInputSize < sizeof(j)) {
                        ret = kIOReturnBadArgument;
                        break;
                }
                memcpy(&j, args->structureInput, sizeof(j));
                ret = _driver->callPlatformFunction(names[op], false, &j, NULL, NULL, NULL);
                bzero(&j, sizeof(j));   /* do not leave the PMK on the stack */
                break;
        }
        case RTL_UC_RADIO_STATUS:
                if (outCap < sizeof(struct rtl_radio_status)) {
                        ret = kIOReturnBadArgument;
                        break;
                }
                ret = _driver->callPlatformFunction(names[op], false, out, NULL, NULL, NULL);
                used = sizeof(struct rtl_radio_status);
                break;
        case RTL_UC_RADIO_MAC:
                if (outCap < 6) {
                        ret = kIOReturnBadArgument;
                        break;
                }
                ret = _driver->callPlatformFunction(names[op], false, out, NULL, NULL, NULL);
                used = 6;
                break;
        default:
                ret = _driver->callPlatformFunction(names[op], false, NULL, NULL, NULL, NULL);
                break;
        }

        if (ret == kIOReturnSuccess && used) {
                if (desc) {
                        if (desc->prepare() == kIOReturnSuccess) {
                                desc->writeBytes(0, out, used);
                                desc->complete();
                                args->structureOutputDescriptorSize = used;
                        } else {
                                ret = kIOReturnVMError;
                        }
                } else {
                        memcpy(args->structureOutput, out, used);
                        args->structureOutputSize = used;
                }
        }
        if (args->scalarOutput && args->scalarOutputCount >= 1)
                args->scalarOutput[0] = count;
        if (out)
                IOFree(out, outCap);
        return ret;
}
