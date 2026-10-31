/*
 * RTL8814Native — vendor glue implementation
 *
 * Implements the linux/usb.h and netdevice.h C API over the IOKit
 * objects, plus the (future) vendor probe/remove entry points.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include <IOKit/IOService.h>
#include <libkern/libkern.h>
#include "VendorGlue.hpp"
#include "RTLUsbDevice.hpp"
#include "RTLEthernetAdapter.hpp"

#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <libkern/libkern.h>
#include <IOKit/usb/IOUSBHostPipe.h>

/* ======================================================================
 * Registry of the active transport / netdev
 * ====================================================================== */
static struct usb_device *g_active_usb;
static struct net_device *g_active_netdev;

void rtl8814native_set_usb_device(struct usb_device *h) { g_active_usb = h; }
struct usb_device *rtl8814native_get_usb_device(void)   { return g_active_usb; }
void rtl8814native_set_netdev(struct net_device *n)     { g_active_netdev = n; }
struct net_device *rtl8814native_get_netdev(void)       { return g_active_netdev; }

static RTLUsbDevice *usb(void)
{
        return RTLUsbDevice::fromHandle(g_active_usb);
}

/* ======================================================================
 * URB lifecycle
 * ====================================================================== */
struct urb *usb_alloc_urb(int iso_packets, gfp_t mem_flags)
{
        (void)iso_packets; (void)mem_flags;

        /* C++ TU: kzalloc returns void* — no implicit conversion from
         * void* in C++ (fine in the vendor C files, explicit here). */
        struct urb *urb = (struct urb *)kzalloc(sizeof(*urb), GFP_KERNEL);
        if (!urb)
                return NULL;

        /* Persistent completion sidecar: owner=urb, action=the trampoline
         * declared in RTLUsbDevice.cpp. It survives resubmissions. */
        IOUSBHostCompletion *comp =
                (IOUSBHostCompletion *)kzalloc(sizeof(*comp), GFP_KERNEL);
        if (!comp) {
                kfree(urb);
                return NULL;
        }
        comp->action = rtlUrbCompletion;
        comp->owner = urb;
        comp->parameter = nullptr;
        urb->shim_priv = comp;
        return urb;
}

void usb_free_urb(struct urb *urb)
{
        if (!urb)
                return;
        if (urb->shim_priv) {
                kfree(urb->shim_priv);
                urb->shim_priv = nullptr;
        }
        kfree(urb);
}

int usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
        (void)mem_flags;
        if (!urb)
                return -EINVAL;
        urb->dev = g_active_usb;   /* keep the handle fresh */
        return usb() ? usb()->submitUrb(urb) : -ENODEV;
}

void usb_kill_urb(struct urb *urb)
{
        if (urb && usb())
                usb()->killUrb(urb);
}

int usb_unlink_urb(struct urb *urb)
{
        usb_kill_urb(urb);
        return 0;
}

/* ======================================================================
 * Synchronous transfers
 * ====================================================================== */
int usb_control_msg(struct usb_device *dev, unsigned int pipe, u8 request,
                    u8 requesttype, u16 value, u16 index, void *data,
                    u16 size, int timeout_ms)
{
        RTLUsbDevice *u = dev ? RTLUsbDevice::fromHandle(dev) : usb();
        if (!u)
                return -ENODEV;
        (void)pipe;   /* control requests always go to ep0 */
        return u->controlTransfer(requesttype, request, value, index,
                                  data, size, (uint32_t)timeout_ms);
}

int usb_bulk_msg(struct usb_device *dev, unsigned int pipe, void *data,
                 int len, int *actual_length, int timeout_ms)
{
        RTLUsbDevice *u = dev ? RTLUsbDevice::fromHandle(dev) : usb();
        if (!u)
                return -ENODEV;
        uint32_t done = 0;
        int ret = u->bulkTransfer((uint8_t)(pipe & 0xFF),
                                  (uint8_t)(pipe & 0x80),
                                  data, (uint32_t)len, &done,
                                  (uint32_t)timeout_ms);
        if (actual_length)
                *actual_length = (int)done;
        return ret < 0 ? ret : 0;
}

struct usb_device *interface_to_usbdev(struct usb_interface *intf)
{
        (void)intf;   /* single-device driver: the registry handle */
        return g_active_usb;
}

u16 rtl_usb_get_vendor(struct usb_device *dev)
{
        RTLUsbDevice *u = RTLUsbDevice::fromHandle(dev);
        return u ? u->vendorId() : 0;
}

u16 rtl_usb_get_product(struct usb_device *dev)
{
        RTLUsbDevice *u = RTLUsbDevice::fromHandle(dev);
        return u ? u->productId() : 0;
}

struct usb_endpoint_descriptor *rtl_usb_find_bulk_in(struct usb_device *dev)
{
        RTLUsbDevice *u = RTLUsbDevice::fromHandle(dev);
        return u ? u->findBulkIn() : NULL;
}

struct usb_endpoint_descriptor *rtl_usb_find_bulk_out(struct usb_device *dev)
{
        RTLUsbDevice *u = RTLUsbDevice::fromHandle(dev);
        return u ? u->findBulkOut() : NULL;
}

u8 rtl_usb_get_out_ep_info(struct usb_device *dev, u8 *ep_addrs, u8 max_eps)
{
        RTLUsbDevice *u = RTLUsbDevice::fromHandle(dev);
        if (!u)
                return 0;
        u8 count = u->numBulkOut();
        if (ep_addrs) {
                for (u8 i = 0; i < count && i < max_eps; i++)
                        ep_addrs[i] = u->bulkOutEndpointAt(i);
        }
        return count;
}

/* ======================================================================
 * net_device glue
 *
 * ARCHITECTURAL DEVIATION (documented): on Linux the vendor driver
 * allocates its own netdev with alloc_etherdev_mq() inside
 * rtw_drv_init(). On macOS the IOKit adapter (RTLEthernetAdapter) is
 * created FIRST by RTL8814Native::start, so alloc_etherdev_mq() in
 * the shim returns that pre-built netdev. Everything downstream
 * (ndev->priv, netdev_ops) behaves exactly like Linux.
 * ====================================================================== */
struct net_device *rtl_alloc_etherdev_mqs(int sizeof_priv)
{
        if (!g_active_netdev)
                return NULL;
        g_active_netdev->priv = kzalloc(sizeof_priv, GFP_KERNEL);
        return g_active_netdev;
}

void rtl_free_netdev(struct net_device *dev)
{
        /* The adapter owns the netdev; only release the priv blob */
        if (dev && dev->priv) {
                kfree(dev->priv);
                dev->priv = NULL;
        }
}

int rtl_register_netdev(struct net_device *dev)
{
        /* The IONetworkInterface was already published by the adapter;
         * "registration" here means the vendor init succeeded. */
        IOLog("RTL8814Native: vendor netdev registered (%s)\n",
              dev ? dev->name : "?");
        return 0;
}

int rtl_unregister_netdev(struct net_device *dev)
{
        IOLog("RTL8814Native: vendor netdev unregistered\n");
        return 0;
}

void eth_hw_addr_random(struct net_device *dev)
{
        rtl_get_random_bytes(dev->dev_addr, ETH_ALEN);
        dev->dev_addr[0] &= 0xFE;   /* unicast  */
        dev->dev_addr[0] |= 0x02;   /* local    */
}

/* RX entry from vendor land */
int rtl_netif_rx(struct net_device *dev, struct sk_buff *skb)
{
        if (!dev || !dev->mac_adapter || !skb)
                return NET_RX_DROP;
        RTLEthernetAdapter *a = (RTLEthernetAdapter *)dev->mac_adapter;
        a->inputFromDriver(skb);
        return NET_RX_SUCCESS;
}

/* AWDL RX entry from validate_recv_frame (core/rtw_recv.c): nonzero when the
 * radio ABI AWDL hook took the frame */
extern "C" int rtl8814native_awdl_rx(struct net_device *dev, const uint8_t *frame,
                                     uint32_t len, int8_t rssi, uint8_t channel)
{
        if (!dev || !dev->mac_adapter || !frame)
                return 0;
        RTLEthernetAdapter *a = (RTLEthernetAdapter *)dev->mac_adapter;
        return a->awdlInput(frame, len, rssi, channel) ? 1 : 0;
}

void rtl_netif_carrier(struct net_device *dev, int on)
{
        if (dev && dev->mac_adapter)
                ((RTLEthernetAdapter *)dev->mac_adapter)
                        ->setCarrierFromDriver(on != 0);
}

void rtl_netif_queue(struct net_device *dev, int wake)
{
        if (dev && dev->mac_adapter)
                ((RTLEthernetAdapter *)dev->mac_adapter)
                        ->wakeQueueFromDriver(wake != 0);
}

void rtl_netif_schedule(struct net_device *dev)
{
        rtl_netif_queue(dev, 1);
}


