/*
 * RTL8814Native — etherdevice + misc net headers for XNU
 *
 * alloc_etherdev_mqs() creates the net_device handle and wires it to
 * the IOKit adapter (phase 3). MAC helpers are self-contained.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_ETHERDEVICE_H_RTL8814
#define _LINUX_ETHERDEVICE_H_RTL8814

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/string.h>     /* memset/memcpy/memcmp in the helpers */

#ifdef __cplusplus
extern "C" {
#endif

struct net_device *rtl_alloc_etherdev_mqs(int sizeof_priv);
#define alloc_etherdev_mq(sizeof_priv, tx_q, rx_q) \
        rtl_alloc_etherdev_mqs(sizeof_priv)
#define alloc_etherdev(sizeof_priv) rtl_alloc_etherdev_mqs(sizeof_priv)
#define free_netdev(dev) rtl_free_netdev(dev)
void rtl_free_netdev(struct net_device *dev);

void eth_hw_addr_random(struct net_device *dev);

static inline bool is_zero_ether_addr(const u8 *addr)
{ return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]); }

static inline bool is_multicast_ether_addr(const u8 *addr)
{ return 0x01 & addr[0]; }

static inline bool is_broadcast_ether_addr(const u8 *addr)
{ return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff; }

static inline bool is_valid_ether_addr(const u8 *addr)
{ return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr); }

static inline void eth_broadcast_addr(u8 *addr)
{ memset(addr, 0xff, ETH_ALEN); }
static inline void eth_zero_addr(u8 *addr)
{ memset(addr, 0x00, ETH_ALEN); }
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{ memcpy(dst, src, ETH_ALEN); }
static inline bool ether_addr_equal(const u8 *a, const u8 *b)
{ return memcmp(a, b, ETH_ALEN) == 0; }
static inline u32 ether_crc(int length, const u8 *data)
{ (void)length; (void)data; return 0; } /* multicast filter via adapter */

static inline void netdev_reset(struct net_device *dev) { (void)dev; }

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_ETHERDEVICE_H_RTL8814 */
