/*
 * RTL8814Native — USB shim for XNU (IOUSBHostFamily)
 *
 * Maps the Linux USB API the driver uses (usb_control_msg for
 * register I/O, async URBs for bulk TX/RX) onto IOUSBHostPipe.
 *
 * struct usb_device / usb_interface are OPAQUE here — they are
 * backed by the C++ classes in Source/Driver (RTLUsbDevice), which
 * implement the rtl_usb_* C entry points. This keeps the vendor C
 * code untouched while the real transport lives in IOKit.
 *
 * Register I/O on RTL8814AU USB goes through vendor control
 * transfers (bmRequest 0xC0/0x40 style) issued by hal_usb.c — those
 * exact request codes are copied verbatim during phase 1; this shim
 * only transports them.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_USB_H_RTL8814
#define _LINUX_USB_H_RTL8814

#include <linux/types.h>
#include <linux/list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- descriptors (subset the driver inspects) ---- */
struct usb_endpoint_descriptor {
        u8  bLength;
        u8  bDescriptorType;
        u8  bEndpointAddress;
        u8  bmAttributes;
        u16 wMaxPacketSize;
        u8  bInterval;
        u8  bRefresh;
        u8  bSynchAddress;
} __packed;

#define USB_ENDPOINT_XFERTYPE_MASK 0x03
#define USB_ENDPOINT_XFER_CONTROL  0
#define USB_ENDPOINT_XFER_ISOC     1
#define USB_ENDPOINT_XFER_BULK     2
#define USB_ENDPOINT_XFER_INT      3
#define USB_DIR_IN                 0x80
#define USB_DIR_OUT                0x00

struct usb_host_interface {
        struct usb_endpoint_descriptor *endpoint;
        void *extra;                    /* altsetting raw desc, if needed */
};
struct usb_host_config { struct usb_host_interface *alt; };
struct usb_device_id {
        u16 idVendor, idProduct;
        unsigned long driver_info;
};

/* ---- opaque handles backed by Driver/RTLUsbDevice.cpp ---- */
struct device {
        void *driver_data;
};

struct usb_device;

struct usb_interface {
        struct usb_host_interface *altsetting;
        struct usb_host_interface *cur_altsetting;
        unsigned num_altsetting;
        struct device dev;
        void *driver_data;
};

struct usb_device_descriptor { u16 idVendor, idProduct; u8 bDeviceClass; };

#define USB_RECIP_DEVICE    0x00
#define USB_RECIP_INTERFACE 0x01
#define USB_TYPE_VENDOR     (0x02 << 5)
#define USB_TYPE_STANDARD   (0x00 << 5)

/* ---- pipes (endpoint addresses) ---- */
static inline unsigned int usb_rcvbulkpipe(struct usb_device *dev, u8 ep)
{ (void)dev; return (unsigned int)(ep | 0x80); }
static inline unsigned int usb_sndbulkpipe(struct usb_device *dev, u8 ep)
{ (void)dev; return (unsigned int)ep; }

/* ---- URB model (async bulk) ---- */
struct urb;                 /* fwd decl — typedef below references it */
typedef void (*usb_complete_t)(struct urb *);

struct urb {
        struct usb_device *dev;         /* our RTLUsbDevice handle      */
        unsigned int pipe;              /* endpoint address             */
        void *transfer_buffer;          /* driver-owned buffer          */
        u32 transfer_buffer_length;
        u32 actual_length;
        int status;                     /* 0 or -errno                  */
        usb_complete_t complete;
        void *context;
        unsigned int transfer_flags;
        void *shim_priv;                /* Compat: IOUSBHostCompletion sidecar */
        volatile int inflight;          /* Compat: io() accepted, completion pending */
        volatile int done;              /* Compat: last completion ran */
};

struct urb *usb_alloc_urb(int iso_packets, gfp_t mem_flags);
void usb_free_urb(struct urb *urb);

/* usb_fill_bulk_urb is an INLINE FUNCTION (not a macro!) — a macro
 * parameter named `dev` would clobber the ->dev field during
 * substitution: (urb)->dev would expand to (urb)->(value). */
static inline void usb_fill_bulk_urb(struct urb *urb,
                                     struct usb_device *dev,
                                     unsigned int pipe, void *buf,
                                     unsigned int len,
                                     usb_complete_t complete_fn,
                                     void *context)
{
        urb->dev = dev;
        urb->pipe = pipe;
        urb->transfer_buffer = buf;
        urb->transfer_buffer_length = len;
        urb->complete = complete_fn;
        urb->context = context;
}

/* Submits the URB on the matching IOUSBHostPipe; completion runs on
 * the USB workloop and is marshalled into urb->complete(urb). */
int usb_submit_urb(struct urb *urb, gfp_t mem_flags);
void usb_kill_urb(struct urb *urb);
int usb_unlink_urb(struct urb *urb);

/* ---- synchronous helpers (register access path) ---- */
int usb_control_msg(struct usb_device *dev, unsigned int pipe, u8 request,
                    u8 requesttype, u16 value, u16 index, void *data,
                    u16 size, int timeout_ms);
int usb_bulk_msg(struct usb_device *dev, unsigned int pipe, void *data,
                 int len, int *actual_length, int timeout_ms);

/* ---- endpoint introspection (usb_intf.c port) ---- */
struct usb_endpoint_descriptor *
rtl_usb_find_bulk_in(struct usb_device *dev);
struct usb_endpoint_descriptor *
rtl_usb_find_bulk_out(struct usb_device *dev);

static inline int usb_endpoint_type(const struct usb_endpoint_descriptor *epd)
{ return epd ? (epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) : 0; }
static inline int usb_endpoint_is_bulk_in(const struct usb_endpoint_descriptor *epd)
{ return epd && ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) && (epd->bEndpointAddress & USB_DIR_IN); }
static inline int usb_endpoint_is_bulk_out(const struct usb_endpoint_descriptor *epd)
{ return epd && ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) && !(epd->bEndpointAddress & USB_DIR_IN); }
static inline int usb_endpoint_num(const struct usb_endpoint_descriptor *epd)
{ return epd ? (epd->bEndpointAddress & 0x0F) : -1; }

/* ---- misc bits referenced by dvobj setup ---- */
static inline struct usb_interface *usb_get_intf(struct usb_interface *i) { return i; }
static inline void usb_put_intf(struct usb_interface *i) { (void)i; }
struct usb_device *interface_to_usbdev(struct usb_interface *intf);
u16 rtl_usb_get_vendor(struct usb_device *dev);
u16 rtl_usb_get_product(struct usb_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_USB_H_RTL8814 */
