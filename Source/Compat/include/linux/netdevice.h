/*
 * RTL8814Native — net_device shim for XNU
 *
 * struct net_device is a THIN handle: the real network object is the
 * RTLEthernetAdapter (IOEthernetController) in Source/Driver. The
 * vendor code stores the adapter pointer in ndev->priv and installs
 * rtw_netdev_ops; RTLEthernetAdapter invokes those ops from the
 * IOKit side (enable → ndo_open, outputPacket → ndo_start_xmit).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_NETDEVICE_H_RTL8814
#define _LINUX_NETDEVICE_H_RTL8814

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/list.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IFNAMSIZ        16
#define ETH_ALEN        6
#define ETH_HLEN        14
#define ETH_ZLEN        60
#define ETH_DATA_LEN    1500
#define ETH_FCS_LEN     4

#define NET_NAME_UNKNOWN  0
#define NET_NAME_ENUM     1

enum { NETDEV_TX_OK = 0, NETDEV_TX_BUSY = 16 };
typedef int netdev_tx_t;

#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_MULTICAST   0x1000

struct net_device_ops {
        int  (*ndo_open)(struct net_device *dev);
        int  (*ndo_stop)(struct net_device *dev);
        netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb, struct net_device *dev);
        int  (*ndo_set_mac_address)(struct net_device *dev, void *addr);
        int  (*ndo_change_mtu)(struct net_device *dev, int new_mtu);
        void (*ndo_set_multicast_list)(struct net_device *dev);
};

struct net_device {
        char name[IFNAMSIZ];
        unsigned char dev_addr[ETH_ALEN];       /* current MAC            */
        unsigned char perm_addr[ETH_ALEN];
        void *priv;                             /* struct _ADAPTER *      */
        const struct net_device_ops *netdev_ops;
        unsigned int mtu;
        unsigned int flags;
        bool up;

        /* back-pointer to the IOKit-side adapter (set by the glue) */
        void *mac_adapter;
};

static inline void *netdev_priv(const struct net_device *dev) { return dev->priv; }
static inline void SET_NETDEV_DEV(struct net_device *d, void *p) { (void)d; (void)p; }
static inline bool netif_running(const struct net_device *dev) { return dev->up; }
static inline int dev_open(struct net_device *dev) { return dev->netdev_ops->ndo_open(dev); }

/* ---- queue/carrier state -> forwarded to RTLEthernetAdapter ---- */
void rtl_netif_carrier(struct net_device *dev, int on);
void rtl_netif_queue(struct net_device *dev, int wake);
void rtl_netif_schedule(struct net_device *dev);

#define netif_carrier_on(dev)    rtl_netif_carrier((dev), 1)
#define netif_carrier_off(dev)   rtl_netif_carrier((dev), 0)
#define netif_wake_queue(dev)    rtl_netif_queue((dev), 1)
#define netif_start_queue(dev)   rtl_netif_queue((dev), 1)
#define netif_stop_queue(dev)    rtl_netif_queue((dev), 0)
#define netif_device_attach(dev) rtl_netif_queue((dev), 1)
#define netif_device_detach(dev) rtl_netif_queue((dev), 0)
#define netif_tx_schedule_all(dev) rtl_netif_schedule(dev)
static inline int netif_queue_stopped(const struct net_device *dev) { (void)dev; return 0; }

#define NET_RX_SUCCESS  0
#define NET_RX_DROP     1

/* RX from driver into the OS — implemented in RTLEthernetAdapter.cpp
 * (skb payload is copied into an mbuf and pushed to the data link) */
int rtl_netif_rx(struct net_device *dev, struct sk_buff *skb);
static inline int netif_rx(struct sk_buff *skb)
{ return skb->dev ? rtl_netif_rx(skb->dev, skb) : NET_RX_DROP; }
static inline void netif_receive_skb(struct sk_buff *skb) { netif_rx(skb); }
static inline int netif_rx_ni(struct sk_buff *skb) { return netif_rx(skb); }

/* register/unregister — glue creates the IOKit interface instead */
int rtl_register_netdev(struct net_device *dev);
int rtl_unregister_netdev(struct net_device *dev);
#define register_netdev(dev)      rtl_register_netdev(dev)
#define unregister_netdev(dev)    rtl_unregister_netdev(dev)

/* stats: vendor keeps its own counters; the adapter pulls them via glue */
struct net_device_stats { unsigned long rx_packets, tx_packets, rx_bytes, tx_bytes, rx_errors, tx_errors, rx_dropped, tx_dropped; };
static inline struct net_device_stats *rtl_netdev_get_stats(struct net_device *d) { (void)d; return NULL; }
#define dev_get_stats(dev, storage) ({ (void)(dev); (void)(storage); NULL; })

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_NETDEVICE_H_RTL8814 */
