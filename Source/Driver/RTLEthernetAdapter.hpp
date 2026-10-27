/*
 * RTL8814Native — ethernet interface (IOEthernetController)
 *
 * Presents the radio to macOS as an IONetworkInterface (itlwm-style
 * phase 1): ifconfig/airportd-adjacent tools see a standard ethernet
 * port; the RTLWifiTahoe app drives scan/join through the UserClient
 * and the vendor MLME.
 *
 * Also implements the net_device C glue (rtl_netif_rx etc.).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_ETHERNET_ADAPTER_H
#define _RTL8814_ETHERNET_ADAPTER_H

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>

class RTL8814Native;

class RTLEthernetAdapter : public IOEthernetController {
	OSDeclareDefaultStructors(RTLEthernetAdapter)

public:
	static RTLEthernetAdapter *withDriver(RTL8814Native *driver);

	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;

	/* IONetworkController */
	virtual IOReturn enable(IONetworkInterface *netif) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disable(IONetworkInterface *netif) APPLE_KEXT_OVERRIDE;
	virtual UInt32 outputPacket(mbuf_t m, void *param) APPLE_KEXT_OVERRIDE;
	virtual bool configureInterface(IONetworkInterface *netif)
	                                  APPLE_KEXT_OVERRIDE;

	/* IOEthernetController */
	virtual IOReturn getHardwareAddress(IOEthernetAddress *addr)
	                                      APPLE_KEXT_OVERRIDE;
	virtual IOReturn setHardwareAddress(const IOEthernetAddress *addr)
	                                      APPLE_KEXT_OVERRIDE;
	virtual IOReturn setPromiscuousMode(IOEnetPromiscuousMode mode)
	                                      APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastList(IOEthernetAddress *addrs,
	                                  UInt32 count) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getPacketFilters(const OSSymbol *group,
	                                  UInt32 *filters) const
	                                  APPLE_KEXT_OVERRIDE;
	virtual IOReturn selectMedium(const IONetworkMedium *medium)
	                                  APPLE_KEXT_OVERRIDE;

	/* --- vendor-side hooks (phase 1+) --- */

	/* Called by the C glue when the vendor driver pushes a frame */
	void inputFromDriver(struct sk_buff *skb);

	/* Carrier/queue state changes from vendor land */
	void setCarrierFromDriver(bool on);
	void wakeQueueFromDriver(bool wake);

	/* Radio ABI data path (RTLRadioABI.h): RX to a hook instead of en2,
	 * TX of Ethernet frames from another kext */
	void setDataHook(void (*fn)(void *ctx, void *mbuf), void *ctx);
	bool dataHookAttached() const { return _hookFn != nullptr; }
	IOReturn transmitFrame(mbuf_t m);

	/* Update MAC address and notify DLIL / IORegistry */
	void setMacAddress(const uint8_t *mac);

	/* The linux net_device handle owned by this adapter */
	struct net_device *netdev() { return &_netdev; }
	RTL8814Native *driver() { return _driver; }

private:
	RTL8814Native   *_driver;
	IONetworkInterface *_netif;
	IONetworkStats *_netStats;   /* polled by IONetworkInterface for ifnet counters */
	bool _enabled;
	bool _carrier;	/* last netif_carrier_on/off from the vendor core */
	uint32_t _rxPendPkts;	/* ifnet input counts not pushed yet (RX thread only) */
	uint32_t _rxPendBytes;

	struct net_device _netdev;
	IOLock *_stateLock;	/* guards the data hook (attach/detach vs RX) */
	void (*_hookFn)(void *ctx, void *mbuf);
	void *_hookCtx;
};

#endif /* _RTL8814_ETHERNET_ADAPTER_H */
