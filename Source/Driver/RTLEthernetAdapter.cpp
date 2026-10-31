/*
 * RTL8814Native — ethernet adapter implementation + net_device glue
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include "RTLEthernetAdapter.hpp"
#include "RTL8814Native.hpp"
#include "VendorGlue.hpp"

#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IOOutputQueue.h>
#include <IOKit/network/IONetworkStats.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>
#include <net/ethernet.h>
#include <net/kpi_interface.h>
#include <sys/kpi_mbuf.h>
#include <string.h>
#include <libkern/libkern.h>

#define super IOEthernetController
OSDefineMetaClassAndStructors(RTLEthernetAdapter, IOEthernetController)

RTLEthernetAdapter *RTLEthernetAdapter::withDriver(RTL8814Native *driver)
{
        RTLEthernetAdapter *self = OSTypeAlloc(RTLEthernetAdapter);
        if (self && !self->init(nullptr)) {
                self->release();
                return nullptr;
        }
        self->_driver = driver;
        return self;
}

bool RTLEthernetAdapter::start(IOService *provider)
{
        if (!super::start(provider))
                return false;

        _stateLock = IOLockAlloc();

        /* Medium dict so IONetworkStack is happy (autoselect — the exact
         * itlwm pattern; NOTE: getMediumWithType must run BEFORE the
         * dict is released or it's a use-after-free). */
        OSDictionary *mediumDict = OSDictionary::withCapacity(3);
        IONetworkMedium *medium = IONetworkMedium::medium(kIOMediumEthernetAuto,
                                                           1000
                                                           | kIOMediumOptionFullDuplex
                                                           | kIOMediumOptionFlowControl);
        if (mediumDict && medium) {
                IONetworkMedium::addMedium(mediumDict, medium);
                publishMediumDictionary(mediumDict);
                setSelectedMedium(medium);
                medium->release();
        }
        if (mediumDict)
                mediumDict->release();

        /* net_device handle the vendor code will hold (ndev->priv) */
        strlcpy(_netdev.name, "en8814", sizeof(_netdev.name));
        _netdev.mtu = ETHERMTU;
        _netdev.flags = IFF_BROADCAST | IFF_MULTICAST;
        _netdev.mac_adapter = this;

        /* Check if hardware MAC was already read from EFUSE during probe */
        bool has_mac = false;
        for (int i = 0; i < ETH_ALEN; i++) {
                if (_netdev.dev_addr[i] != 0) {
                        has_mac = true;
                        break;
                }
        }

        if (!has_mac) {
                /* Fallback placeholder MAC until EFUSE MAC read lands */
                _netdev.dev_addr[0] = 0x02;      /* locally administered */
                _netdev.dev_addr[1] = 0x11;
                _netdev.dev_addr[2] = 0x22;
                _netdev.dev_addr[3] = 0x33;
                _netdev.dev_addr[4] = 0x44;
                _netdev.dev_addr[5] = 0x55;
        }
        memcpy(_netdev.perm_addr, _netdev.dev_addr, ETH_ALEN);

        /* rtlair=2: RTL8814AirLayer's Skywalk interface carries the data with
         * the hardware MAC, and configd's InterfaceNamer keys units by MAC: a
         * live en2 with the same MAC leaves it unnamed (kIOReturnNoSpace).
         * The adapter stays as the RX / TX path behind the radio ABI. */
        uint32_t rtlair = 0;
        if (PE_parse_boot_argn("rtlair", &rtlair, sizeof(rtlair)) && rtlair >= 2) {
                IOLog("RTL8814Native: rtlair=%u, no ethernet interface\n", rtlair);
                return true;
        }

        /* Register with IONetworkStack: publishes the enX interface */
        if (!attachInterface((IONetworkInterface **)&_netif, true)) {
                IOLog("RTL8814Native: attachInterface failed\n");
                return false;
        }

        IOLog("RTL8814Native: ethernet interface published (%s)\n",
              _netdev.name);
        return true;
}

void RTLEthernetAdapter::stop(IOService *provider)
{
        if (_netif) {
                detachInterface(_netif);
                _netif->release();
                _netif = nullptr;
        }
        if (_stateLock) {
                IOLockFree(_stateLock);
                _stateLock = nullptr;
        }
        super::stop(provider);
}

void RTLEthernetAdapter::free()
{
        if (_stateLock) {
                IOLockFree(_stateLock);
                _stateLock = nullptr;
        }
        super::free();
}

IOReturn RTLEthernetAdapter::enable(IONetworkInterface *netif)
{
        IOLog("RTL8814Native: interface enable\n");
        _enabled = true;
        /* Keep a carrier the core already reported: the re-join after a
         * fresh start (4.6h) can finish before configd brings en2 up, and
         * writing plain LinkValid here left en2 "inactive" while associated */
        setLinkStatus(_carrier ? (kIONetworkLinkValid | kIONetworkLinkActive)
                               : kIONetworkLinkValid,
                      getCurrentMedium());
        /* PHASE 1: netdev_ops->ndo_open(netdev) → rtw_hw_up */
        return kIOReturnSuccess;
}

IOReturn RTLEthernetAdapter::disable(IONetworkInterface *netif)
{
        IOLog("RTL8814Native: interface disable\n");
        /* PHASE 1: netdev_ops->ndo_stop(netdev) → rtw_hw_stop */
        _enabled = false;
        setLinkStatus(0, getCurrentMedium());
        return kIOReturnSuccess;
}

/* IONetworkInterface derives ifnet Ipkts/Opkts/Ierrs/Oerrs from the
 * driver's IONetworkStats deltas (inputPacket / fetchDriverOutputStats),
 * so the counters below must be bumped by us — nobody else does. */
#define NETSTAT_INC(field) \
        do { if (_netStats) OSIncrementAtomic((SInt32 *)&_netStats->field); } while (0)

bool RTLEthernetAdapter::configureInterface(IONetworkInterface *netif)
{
        if (!super::configureInterface(netif))
                return false;

        IONetworkData *data = netif->getNetworkData(kIONetworkStatsKey);
        _netStats = data ? (IONetworkStats *)data->getBuffer() : nullptr;
        if (!_netStats)
                IOLog("RTL8814Native: no IONetworkStats buffer\n");
        return true;
}

UInt32 RTLEthernetAdapter::outputPacket(mbuf_t m, void *param)
{
        (void)param;
        if (!m)
                return kIOReturnOutputDropped;
        if (!_enabled) {
                freePacket(m);
                NETSTAT_INC(outputErrors);
                return kIOReturnOutputDropped;
        }
        if (transmitFrame(m) != kIOReturnSuccess) {
                NETSTAT_INC(outputErrors);
                return kIOReturnOutputDropped;
        }
        NETSTAT_INC(outputPackets);
        return kIOReturnOutputSuccess;
}

/* Ethernet frame -> vendor xmit. Consumes m in all cases. Shared by en2's
 * outputPacket and the radio ABI (rtlRadioTx), which runs with en2 down. */
IOReturn RTLEthernetAdapter::transmitFrame(mbuf_t m)
{
        size_t pkt_len = mbuf_pkthdr_len(m);
        if (pkt_len < 14 || pkt_len > 2048) {
                freePacket(m);
                return kIOReturnBadArgument;
        }

        struct sk_buff *skb = rtl_alloc_skb((unsigned int)pkt_len + 32);
        if (!skb) {
                freePacket(m);
                return kIOReturnNoMemory;
        }

        u8 *dst = skb_put(skb, (unsigned int)pkt_len);
        if (mbuf_copydata(m, 0, pkt_len, dst) != 0) {
                rtl_kfree_skb(skb);
                freePacket(m);
                return kIOReturnError;
        }

        freePacket(m);
        return rtl8814native_vendor_xmit(skb) == 0 ? kIOReturnSuccess : kIOReturnOutputDropped;
}

void RTLEthernetAdapter::setDataHook(void (*fn)(void *ctx, void *mbuf), void *ctx)
{
        if (!_stateLock)
                return;
        IOLockLock(_stateLock);
        _hookCtx = fn ? ctx : nullptr;
        _hookFn = fn;
        IOLockUnlock(_stateLock);
        /* en2 must not look usable while another interface owns the data */
        setLinkStatus((_carrier && !fn) ? (kIONetworkLinkValid | kIONetworkLinkActive)
                                        : kIONetworkLinkValid,
                      getCurrentMedium());
}

void RTLEthernetAdapter::setAwdlHook(rtl_radio_awdl_rx_fn fn, void *ctx)
{
        if (!_stateLock)
                return;
        IOLockLock(_stateLock);
        _awdlCtx = fn ? ctx : nullptr;
        _awdlFn = fn;
        IOLockUnlock(_stateLock);
}

/* RX thread (C glue entry: rtl8814native_awdl_rx). True when a hook took the
 * frame, so the vendor core drops it instead of classifying it. */
bool RTLEthernetAdapter::awdlInput(const uint8_t *frame, uint32_t len, int8_t rssi,
                                   uint8_t channel)
{
        if (!_awdlFn || !_stateLock)
                return false;
        bool taken = false;
        IOLockLock(_stateLock);
        if (_awdlFn) {
                _awdlFn(_awdlCtx, frame, len, rssi, channel);
                taken = true;
        }
        IOLockUnlock(_stateLock);
        return taken;
}

void RTLEthernetAdapter::setMacAddress(const uint8_t *mac)
{
        if (!mac)
                return;
        memcpy(_netdev.dev_addr, mac, ETH_ALEN);
        memcpy(_netdev.perm_addr, mac, ETH_ALEN);
        setProperty("IOMACAddress", (void *)mac, kIOEthernetAddressSize);
        if (_netif) {
                _netif->setProperty("IOMACAddress", (void *)mac, kIOEthernetAddressSize);
        }
}

IOReturn RTLEthernetAdapter::getHardwareAddress(IOEthernetAddress *addr)
{
        if (!addr)
                return kIOReturnBadArgument;
        memcpy(addr->bytes, _netdev.dev_addr, kIOEthernetAddressSize);
        return kIOReturnSuccess;
}

IOReturn RTLEthernetAdapter::setHardwareAddress(const IOEthernetAddress *addr)
{
        if (!addr)
                return kIOReturnBadArgument;
        setMacAddress(addr->bytes);
        return kIOReturnSuccess;
}

IOReturn RTLEthernetAdapter::getPacketFilters(const OSSymbol *group,
                                              UInt32 *filters) const
{
        if (!filters)
                return kIOReturnBadArgument;
        if (group == gIONetworkFilterGroup)
                *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
        else
                *filters = 0;
        return kIOReturnSuccess;
}

IOReturn RTLEthernetAdapter::selectMedium(const IONetworkMedium *medium)
{
        if (medium)
                setSelectedMedium(medium);
        return kIOReturnSuccess;
}

IOReturn RTLEthernetAdapter::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
        return kIOReturnSuccess;
}

IOReturn RTLEthernetAdapter::setMulticastMode(bool active)
{
        return kIOReturnSuccess;
}

IOReturn RTLEthernetAdapter::setMulticastList(IOEthernetAddress *addrs,
                                              UInt32 count)
{
        return kIOReturnSuccess;
}

/* ---------------------------------------------------------------------
 * RX from the vendor driver (C glue entry: rtl_netif_rx)
 * --------------------------------------------------------------------- */
void RTLEthernetAdapter::inputFromDriver(struct sk_buff *skb)
{
        if (skb && _hookFn) {
                /* Radio ABI hook attached: the frame goes there, not to en2 */
                mbuf_t hm = allocatePacket(skb->len);
                if (hm && mbuf_copyback(hm, 0, skb->len, skb->data, MBUF_DONTWAIT) != 0) {
                        freePacket(hm);
                        hm = nullptr;
                }
                rtl_kfree_skb(skb);
                if (!hm)
                        return;
                IOLockLock(_stateLock);
                if (_hookFn)
                        _hookFn(_hookCtx, hm);
                else
                        freePacket(hm);
                IOLockUnlock(_stateLock);
                return;
        }
        if (!skb || !_enabled || !_netif) {
                if (skb) rtl_kfree_skb(skb);
                return;
        }

        /* skb payload → fresh mbuf → network stack
         * (USB workloop context: never block on mbuf allocation) */
        mbuf_t m = allocatePacket(skb->len);
        if (!m) {
                NETSTAT_INC(inputErrors);
                rtl_kfree_skb(skb);
                return;
        }
        if (mbuf_copyback(m, 0, skb->len, skb->data, MBUF_DONTWAIT) != 0) {
                NETSTAT_INC(inputErrors);
                freePacket(m);
                rtl_kfree_skb(skb);
                return;
        }
        /* must precede inputPacket(): the delta is sampled there.
         * NOTE: ifnet Ipkts/Ibytes still read 0 on Tahoe with this path
         * and with queue + flushInputQueue() alike (see Docs/ARCHITECTURE.md 3.2). */
        NETSTAT_INC(inputPackets);
        uint32_t len = skb->len;
        _netif->inputPacket(m, len);
        rtl_kfree_skb(skb);

        /* So bump the BSD counters ourselves, batched: the increment
         * takes the ifnet stats lock. Only the RX thread gets here. */
        _rxPendPkts++;
        _rxPendBytes += len;
        if (_rxPendPkts >= 32) {
                ifnet_t ifp = _netif->getIfnet();
                if (ifp)
                        ifnet_stat_increment_in(ifp, _rxPendPkts, _rxPendBytes, 0);
                _rxPendPkts = 0;
                _rxPendBytes = 0;
        }
}

void RTLEthernetAdapter::setCarrierFromDriver(bool on)
{
        _carrier = on;
        setLinkStatus((on && !_hookFn) ? (kIONetworkLinkValid | kIONetworkLinkActive)
                         : kIONetworkLinkValid,
                      getCurrentMedium());
}

void RTLEthernetAdapter::wakeQueueFromDriver(bool wake)
{
        /* Queue state: the IONetwork family back-pressures outputPacket
         * itself; vendor xmit stalls are handled by dropping + counting
         * until phase 3 wires the real queue control. */
}
