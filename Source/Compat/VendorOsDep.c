/*
 * RTL8814Native — Realtek OS-dependent hooks for Darwin / XNU
 *
 * Implements the vendor OS layer functions (usb_ops, recvbuf/xmitbuf alloc,
 * atomic helpers, and sleep wrappers) that bridge to IOKit / Compat.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include <drv_types.h>
#include <hal_data.h>
#include <rtl8814a_hal.h>
#include <usb_ops.h>
#include <linux/usb.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <kern/clock.h>
#include <libkern/OSAtomic.h>
#include <IOKit/IOLib.h>
#include "VendorGlue.hpp"
#include "WPA2Supplicant.h"

/* VendorProbe (C, vendor types) — the live _adapter or NULL while torn
 * down. URB completions compare their padapter against it and drop stale
 * instances instead of touching freed memory. */
extern _adapter *rtl8814native_get_primary_adapter(void);

/* ======================================================================
 * USB Vendor Request & Control / Bulk Operations
 * ====================================================================== */

int usbctrl_vendorreq(struct intf_hdl *pintfhdl, u8 request, u16 value,
                      u16 index, void *pdata, u16 len, u8 requesttype)
{
	_adapter *padapter = pintfhdl ? pintfhdl->padapter : NULL;
	struct dvobj_priv *pdvobj = padapter ? adapter_to_dvobj(padapter) : NULL;
	struct usb_device *udev = pdvobj ? pdvobj->pusbdev : rtl8814native_get_usb_device();
	if (!udev)
		return -ENODEV;

	u8 reqtype = (requesttype == 0x01) ? REALTEK_USB_VENQT_READ : REALTEK_USB_VENQT_WRITE;
	int ret = usb_control_msg(udev, 0, request, reqtype, value, index, pdata, len, 500);
	return (ret >= 0) ? len : ret;
}

void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	(void)pintfhdl; (void)addr; (void)cnt; (void)rmem;
}

void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	(void)pintfhdl; (void)addr; (void)cnt; (void)wmem;
}

void usb_read_port_cancel(struct intf_hdl *pintfhdl)
{
	if (!pintfhdl || !pintfhdl->padapter)
		return;
	_adapter *padapter = pintfhdl->padapter;
	struct recv_buf *precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;
	if (!precvbuf)
		return;

	for (int i = 0; i < NR_RECVBUFF; i++) {
		if (precvbuf->purb)
			usb_kill_urb(precvbuf->purb);
		precvbuf++;
	}
}

/* Bulk OUT URBs submitted and not yet completed (usb_write_port_cancel waits on it) */
static volatile SInt32 g_tx_urbs_inflight;

void usb_write_port_cancel(struct intf_hdl *pintfhdl)
{
	if (!pintfhdl || !pintfhdl->padapter)
		return;
	_adapter *padapter = pintfhdl->padapter;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmitbuf;

	if (pxmitbuf) {
		for (int i = 0; i < NR_XMITBUFF; i++) {
			for (int j = 0; j < 8; j++) {
				if (pxmitbuf->pxmit_urb[j])
					usb_kill_urb(pxmitbuf->pxmit_urb[j]);
			}
			pxmitbuf++;
		}
	}

	pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmit_extbuf;
	if (pxmitbuf) {
		for (int i = 0; i < NR_XMIT_EXTBUFF; i++) {
			for (int j = 0; j < 8; j++) {
				if (pxmitbuf->pxmit_urb[j])
					usb_kill_urb(pxmitbuf->pxmit_urb[j]);
			}
			pxmitbuf++;
		}
	}

	/* Aborted URBs complete asynchronously and free their xmitbufs; wait so
	 * the caller can tear down xmit_priv safely (bounded: unplug may hang). */
	for (int ms = 0; g_tx_urbs_inflight > 0 && ms < 1000; ms++)
		rtw_msleep_os(1);
}

unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	unsigned int pipe = 0, ep_num = 0;
	struct usb_device *pusbd = pdvobj ? pdvobj->pusbdev : NULL;

	if (!pdvobj || !pusbd)
		return 0;

	if (addr == RECV_BULK_IN_ADDR) {
		pipe = usb_rcvbulkpipe(pusbd, pdvobj->RtInPipe[0]);
	} else if (addr == RECV_INT_IN_ADDR) {
		pipe = usb_rcvbulkpipe(pusbd, pdvobj->RtInPipe[1]);
	} else if (addr < HW_QUEUE_ENTRY) {
		ep_num = pdvobj->Queue2Pipe[addr];
		if (ep_num == 0)
			ep_num = pdvobj->RtOutPipe[0];
		pipe = usb_sndbulkpipe(pusbd, ep_num);
	} else {
		ep_num = pdvobj->RtOutPipe[0];
		pipe = usb_sndbulkpipe(pusbd, ep_num);
	}

	return pipe;
}

int usb_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf)
{
	precvbuf->transfer_len = 0;
	precvbuf->len = 0;
	precvbuf->ref_cnt = 0;
	if (precvbuf->pbuf) {
		precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pbuf;
		precvbuf->pend = precvbuf->pdata + MAX_RECVBUF_SZ;
	}
	return _SUCCESS;
}

volatile uint32_t g_rx_urb_submit_ok = 0;
volatile uint32_t g_rx_urb_submit_err = 0;
volatile uint32_t g_rx_urb_complete_ok = 0;
volatile uint32_t g_rx_urb_complete_err = 0;
volatile uint32_t g_rx_frames_decoded = 0;
struct rtl8814_trx_diag g_trx_diag;
struct rtl8814_rx_log g_rx_log;
struct rtl8814_rate_diag g_rate_diag;   /* RX-side fields only; the rest is filled on read */
volatile uint32_t g_rx_skb_recycle = 1;  /* live option RTL_OPT_RX_SKB_RECYCLE */

static inline uint64_t rx_now_ns(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}

static inline void rx_account_ns(uint64_t *total, uint32_t *max, uint64_t t0)
{
	uint64_t d = rx_now_ns() - t0;
	*total += d;
	if (d > *max)
		*max = (uint32_t)(d > 0xFFFFFFFFull ? 0xFFFFFFFFull : d);
}

/* Bulk IN transfers that end mid-frame are stashed here and completed by the
 * next transfer when g_rx_reassemble is set (live option RTL_OPT_RX_REASSEMBLE).
 * Only touched from usb_recv_tasklet, which runs on the single rtl_rx_wq thread
 * (and from the wake path while RX is stopped). */
#define RX_STAGE_SZ		(MAX_RECVBUF_SZ * 2)
#define RX_STAGE_MAX_AGE	4
#define RX_MAX_PLAUSIBLE_LEN	12000
volatile uint32_t g_rx_reassemble = 0;
static u8 g_rx_stage[RX_STAGE_SZ];
static u32 g_rx_stage_len;
static u32 g_rx_stage_age;

void rtl8814native_rx_stage_reset(void)
{
	g_rx_stage_len = 0;
	g_rx_stage_age = 0;
}

static void rx_log_xfer(const u8 *buf, u32 len)
{
	struct rtl8814_rx_xfer *x = &g_rx_log.xfer[g_rx_log.next_seq % RX_XFER_LOG_N];
	u32 n = len < sizeof(x->head) ? len : (u32)sizeof(x->head);

	x->seq = g_rx_log.next_seq++;
	x->len = len;
	_rtw_memset(x->head, 0, sizeof(x->head));
	_rtw_memset(x->tail, 0, sizeof(x->tail));
	_rtw_memcpy(x->head, buf, n);
	_rtw_memcpy(x->tail, buf + len - n, n);
}

/* Keep the unparsed tail of this transfer for the next one */
static void rx_stash_tail(const u8 *pbuf, s32 left, u16 desc_pkt_len)
{
	if (!g_rx_reassemble || left <= 0 || (u32)left > RX_STAGE_SZ / 2 ||
	    desc_pkt_len > RX_MAX_PLAUSIBLE_LEN || g_rx_stage_age >= RX_STAGE_MAX_AGE) {
		if (left > 0 && g_rx_reassemble)
			g_trx_diag.rx_stage_dropped++;
		g_rx_stage_len = 0;
		g_rx_stage_age = 0;
		return;
	}
	memmove(g_rx_stage, pbuf, (size_t)left);
	g_rx_stage_len = (u32)left;
	g_rx_stage_age++;
	g_trx_diag.rx_carry++;
}

static void usb_read_port_complete_body(struct urb *purb)
{
	struct recv_buf *precvbuf = (struct recv_buf *)purb->context;
	if (!precvbuf || !precvbuf->adapter)
		return;

	_adapter *padapter = (_adapter *)precvbuf->adapter;
	if (padapter != rtl8814native_get_primary_adapter())
		return;   /* completion of a torn-down instance */
	struct recv_priv *precvpriv = &padapter->recvpriv;

	precvbuf->irp_pending = _FALSE;

	if (RTW_CANNOT_RX(padapter)) {
		if (precvbuf->pskb) {
			rtw_skb_free(precvbuf->pskb);
			precvbuf->pskb = NULL;
		}
		return;
	}

	if (purb->status == 0) {
		g_rx_urb_complete_ok++;
		if ((purb->actual_length >= RXDESC_SIZE) && (purb->actual_length <= MAX_RECVBUF_SZ)) {
			rtw_reset_continual_io_error(adapter_to_dvobj(padapter));
			skb_put(precvbuf->pskb, purb->actual_length);
			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);
			if (skb_queue_len(&precvpriv->rx_skb_queue) > g_rate_diag.rx_backlog_max)
				g_rate_diag.rx_backlog_max = skb_queue_len(&precvpriv->rx_skb_queue);

			tasklet_schedule(&precvpriv->recv_tasklet);

			precvbuf->pskb = NULL;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		} else {
			/* Invalid transfer length: reuse skb and resubmit */
			g_trx_diag.urb_bad_len++;
			g_trx_diag.last_bad_urb_len = purb->actual_length;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
	} else {
		g_rx_urb_complete_err++;
		if (rtw_inc_and_chk_continual_io_error(adapter_to_dvobj(padapter)) == _TRUE)
			rtw_set_surprise_removed(padapter);

		if (purb->status == -ENOENT || purb->status == -ECONNRESET || purb->status == -ESHUTDOWN) {
			rtw_set_drv_stopped(padapter);
			if (precvbuf->pskb) {
				rtw_skb_free(precvbuf->pskb);
				precvbuf->pskb = NULL;
			}
		} else {
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
	}
}

static void usb_read_port_complete(struct urb *purb)
{
	uint64_t t0 = rx_now_ns();

	usb_read_port_complete_body(purb);
	g_rate_diag.rx_cb_cnt++;
	rx_account_ns(&g_rate_diag.rx_cb_ns, &g_rate_diag.rx_cb_ns_max, t0);
}

u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	(void)cnt;
	if (!pintfhdl || !pintfhdl->padapter || !rmem)
		return _FAIL;

	_adapter *adapter = (_adapter *)pintfhdl->padapter;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(adapter);
	struct recv_priv *precvpriv = &adapter->recvpriv;
	struct usb_device *pusbd = pdvobj ? pdvobj->pusbdev : rtl8814native_get_usb_device();
	struct recv_buf *precvbuf = (struct recv_buf *)rmem;
	PURB purb = precvbuf->purb;

	if (RTW_CANNOT_RX(adapter) || !pusbd || !purb)
		return _FAIL;

	usb_init_recvbuf(adapter, precvbuf);

	if (precvbuf->pskb == NULL) {
		SIZE_PTR tmpaddr = 0;
		SIZE_PTR alignment = 0;

		precvbuf->pskb = skb_dequeue(&precvpriv->free_recv_skb_queue);
		if (NULL == precvbuf->pskb) {
			precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
			if (precvbuf->pskb == NULL)
				return _FAIL;
			g_rate_diag.rx_skb_allocs++;

			tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
			alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
			skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));
		}
		precvbuf->pskb->dev = adapter->pnetdev;
	}

	precvbuf->phead = precvbuf->pskb->head;
	precvbuf->pdata = precvbuf->pskb->data;
	precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
	precvbuf->pend  = skb_end_pointer(precvbuf->pskb);
	precvbuf->pbuf  = precvbuf->pskb->data;

	unsigned int pipe = ffaddr2pipehdl(pdvobj, addr);
	usb_fill_bulk_urb(purb, pusbd, pipe,
	                  precvbuf->pbuf,
	                  MAX_RECVBUF_SZ,
	                  usb_read_port_complete,
	                  precvbuf);

	int err = usb_submit_urb(purb, GFP_ATOMIC);
	if (err && err != -EPERM) {
		g_rx_urb_submit_err++;
		return _FAIL;
	}

	g_rx_urb_submit_ok++;
	precvbuf->irp_pending = _TRUE;
	return _SUCCESS;
}

/* (g_tx_urbs_inflight: bulk OUT URBs submitted and not yet completed) */

/* Runs on the USB completion workloop: must not block (sctx done, buffer
 * free and tasklet scheduling only), same as the Linux softirq handler. */
static void usb_write_port_complete(struct urb *purb)
{
	_irqL irqL;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)purb->context;
	_adapter *padapter = pxmitbuf->padapter;
	OSDecrementAtomic(&g_tx_urbs_inflight);

	if (padapter != rtl8814native_get_primary_adapter())
		return;   /* stale completion of a torn-down instance */
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (purb->status == 0) {
		g_trx_diag.tx_write_ok++;
	} else {
		g_trx_diag.tx_write_err++;
		g_trx_diag.tx_last_err = purb->status;
		if (purb->status == -ENODEV)
			rtw_set_surprise_removed(padapter);
	}

	_enter_critical(&pxmitpriv->lock_sctx, &irqL);
	rtw_sctx_done_err(&pxmitbuf->sctx,
		purb->status ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS);
	_exit_critical(&pxmitpriv->lock_sctx, &irqL);

	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	if (!RTW_CANNOT_TX(padapter))
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
}

/* Asynchronous like Linux: up to NR_XMITBUFF (+ ext) buffers in flight, so the
 * HAL keeps queueing and aggregating frames while the chip transmits. */
u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	int status;

	if (!pintfhdl || !pintfhdl->padapter || !wmem)
		return _FAIL;

	_adapter *padapter = (_adapter *)pintfhdl->padapter;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)wmem;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	struct usb_device *pusbd = pdvobj ? pdvobj->pusbdev : rtl8814native_get_usb_device();
	PURB purb = pxmitbuf->pxmit_urb[0];

	if (RTW_CANNOT_TX(padapter)) {
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_TX_DENY);
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		return _FAIL;
	}

	u8 *buf_ptr = (pxmitframe && pxmitframe->buf_addr) ? pxmitframe->buf_addr : pxmitbuf->pbuf;
	if (!buf_ptr || !pusbd || !purb) {
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		return _FAIL;
	}

	g_trx_diag.tx_last_ep = addr;
	usb_fill_bulk_urb(purb, pusbd, ffaddr2pipehdl(pdvobj, addr),
	                  buf_ptr, cnt, usb_write_port_complete, pxmitbuf);

	OSIncrementAtomic(&g_tx_urbs_inflight);
	status = usb_submit_urb(purb, GFP_ATOMIC);
	if (status) {
		OSDecrementAtomic(&g_tx_urbs_inflight);
		g_trx_diag.tx_write_err++;
		g_trx_diag.tx_last_err = status;
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);
		if (status == -ENODEV)
			rtw_set_drv_stopped(padapter);
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		return _FAIL;
	}

	return _SUCCESS;
}

int recvbuf2recvframe(_adapter *padapter, _pkt *pskb)
{
	u8 *pbuf;
	u8 pkt_cnt = 0;
	u32 pkt_offset;
	s32 transfer_len;
	u8 *pdata;
	union recv_frame *precvframe = NULL;
	struct rx_pkt_attrib *pattrib = NULL;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &precvpriv->free_recv_queue;
	u32 nparsed = 0;

	_pkt *bound_skb = pskb;
	u32 stage_len = g_rx_stage_len;

	transfer_len = (s32)pskb->len;
	pbuf = pskb->data;

	g_trx_diag.last_urb_len = (u32)transfer_len;
	if (transfer_len >= RXDESC_SIZE)
		_rtw_memcpy(g_trx_diag.last_desc, pbuf, RXDESC_SIZE);
	if (transfer_len > 0)
		rx_log_xfer(pbuf, (u32)transfer_len);

	g_rx_stage_len = 0;
	if (g_rx_reassemble && stage_len) {
		if (stage_len + (u32)transfer_len <= RX_STAGE_SZ) {
			_rtw_memcpy(g_rx_stage + stage_len, pbuf, (u32)transfer_len);
			pbuf = g_rx_stage;
			transfer_len += (s32)stage_len;
			bound_skb = NULL;   /* pkt_offset <= transfer_len bounds the copy */
			g_trx_diag.rx_reassembled++;
		} else {
			g_trx_diag.rx_stage_dropped++;
			g_rx_stage_age = 0;
		}
	}

#ifdef CONFIG_USB_RX_AGGREGATION
	pkt_cnt = GET_RX_STATUS_DESC_DMA_AGG_NUM_8814A(pbuf);
#endif

	g_rate_diag.rx_xfers++;
	g_rate_diag.rx_xfer_bytes += (u32)transfer_len;
	if ((u32)transfer_len > g_rate_diag.rx_xfer_max_len)
		g_rate_diag.rx_xfer_max_len = (u32)transfer_len;

	do {
		/* Aggregation tail padding / stale bytes: not enough for a descriptor.
		 * Not stashed: junk here would corrupt the next transfer's first descriptor. */
		if (transfer_len < RXDESC_SIZE)
			goto _exit_recvbuf2recvframe;

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if (precvframe == NULL) {
			g_trx_diag.drop_no_recvframe++;
			goto _exit_recvbuf2recvframe;
		}

		_rtw_init_listhead(&precvframe->u.hdr.list);
		precvframe->u.hdr.precvbuf = NULL;
		precvframe->u.hdr.len = 0;

		rtl8814_query_rx_desc_status(precvframe, pbuf);

		pattrib = &precvframe->u.hdr.attrib;

		if ((padapter->registrypriv.mp_mode == 0) && ((pattrib->crc_err) || (pattrib->icv_err))) {
			if (pattrib->crc_err)
				g_trx_diag.drop_crc++;
			else
				g_trx_diag.drop_icv++;
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->shift_sz + pattrib->pkt_len;
		if ((pattrib->pkt_len <= 0) || (pkt_offset > (u32)transfer_len)) {
			u16 desc_pkt_len = pattrib->pkt_len;
			g_trx_diag.drop_bad_pktlen++;
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			if (desc_pkt_len > 0)
				rx_stash_tail(pbuf, transfer_len, desc_pkt_len);
			goto _exit_recvbuf2recvframe;
		}

#ifdef CONFIG_RX_PACKET_APPEND_FCS
		if (check_fwstate(&padapter->mlmepriv, WIFI_MONITOR_STATE) == _FALSE)
			if ((pattrib->pkt_rpt_type == NORMAL_RX) && rtw_hal_rcr_check(padapter, RCR_APPFCS)) {
				/* pkt_len is u16: a runt frame would underflow to ~0xFFFD and
				 * memcpy 64KB past the 32KB bulk IN buffer (page fault panic) */
				if (pattrib->pkt_len <= IEEE80211_FCS_LEN) {
					g_trx_diag.drop_runt++;
					rtw_free_recvframe(precvframe, pfree_recv_queue);
					goto _next_pkt;
				}
				pattrib->pkt_len -= IEEE80211_FCS_LEN;
			}
#endif

		pdata = pbuf + RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->shift_sz;
		if (rtw_os_alloc_recvframe(padapter, precvframe, pdata, bound_skb) == _FAIL) {
			g_trx_diag.drop_alloc++;
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		recvframe_put(precvframe, pattrib->pkt_len);
		g_rx_frames_decoded++;
		nparsed++;

		if (pattrib->pkt_rpt_type == NORMAL_RX) {
			u8 fc0 = pdata[0];
			g_trx_diag.rx_normal++;
			switch ((fc0 >> 2) & 0x3) {
			case 0:
				g_trx_diag.rx_mgmt++;
				if (fc0 == 0x80)
					g_trx_diag.rx_beacon++;
				else if (fc0 == 0x50)
					g_trx_diag.rx_probe_rsp++;
				break;
			case 1: g_trx_diag.rx_ctrl++; break;
			case 2:
				g_trx_diag.rx_data++;
				if (!(pdata[4] & 0x01)) {   /* addr1 unicast */
					g_rate_diag.last_rx_rate = pattrib->data_rate;
					g_rate_diag.last_rx_bw = pattrib->bw;
					g_rate_diag.last_rx_sgi = pattrib->sgi;
					g_rate_diag.last_rx_ldpc = pattrib->ldpc;
					g_rate_diag.last_rx_stbc = pattrib->stbc;
				}
				break;
			default: break;
			}
			pre_recv_entry(precvframe, pattrib->physt ? (pbuf + RXDESC_OFFSET) : NULL);
		} else {
			if (pattrib->pkt_rpt_type == C2H_PACKET)
				g_trx_diag.rx_c2h++;
			else
				g_trx_diag.rx_other_rpt++;
			if (pattrib->pkt_rpt_type == C2H_PACKET) {
				u8 cid, cseq, cplen, *cpl;
				if (rtw_hal_c2h_pkt_hdr_parse(padapter, precvframe->u.hdr.rx_data,
							      pattrib->pkt_len, &cid, &cseq, &cplen, &cpl) != _SUCCESS)
					g_rate_diag.c2h_parse_fail++;
				else if (cid < 16)
					g_rate_diag.c2h_id_cnt[cid]++;
				else
					g_rate_diag.c2h_id_other++;
				rtw_hal_c2h_pkt_pre_hdl(padapter, precvframe->u.hdr.rx_data, pattrib->pkt_len);
			}
			rtw_free_recvframe(precvframe, pfree_recv_queue);
		}

_next_pkt:
#ifdef CONFIG_USB_RX_AGGREGATION
		pkt_offset = (u16)_RND8(pkt_offset);
		pkt_cnt--;
		pbuf += pkt_offset;
#endif
		transfer_len -= pkt_offset;
		precvframe = NULL;
	} while (transfer_len > 0);

	/* Every byte consumed: no partial frame is pending */
	g_rx_stage_age = 0;

_exit_recvbuf2recvframe:
	g_rate_diag.rx_xfer_pkts += nparsed;
	if (nparsed > g_rate_diag.rx_xfer_max_pkts)
		g_rate_diag.rx_xfer_max_pkts = nparsed;
	return _SUCCESS;
}

/* Called from phydm_c2h_ra_report_handler() (cmd thread) */
/*
 * Noise floor from the BSS's OFDM frames: power minus SNR on the path with
 * the best SNR, averaged with weight 1/16. dBm x 16, 0 until the first frame.
 * Paths A/B only: on 8814A the C/D "SNR" comes from csi_current.
 */
s32 g_rx_noise_q4;

/* Join event log (Driver/RTLJoinLog.h), read by kRTLUC_GetJoinLog */
struct rtl_jlog g_jlog;
u32 g_jlog_bcn;

/*
 * Set by rtl8814native_radio_join to the cached BSS's channel: rtw_do_join then
 * probes that one channel with the SSID before joining. 4.20 proved this does
 * NOT wake the AP after a leave (the directed probe gets no auth response
 * either); the plumbing stays for A/B experiments.
 */
u8 g_join_probe_ch;

/*
 * Set by rtl8814native_radio_join when the last leave is recent enough that
 * the AP still ignores our auth: rtw_do_join then flushes the scan queue so
 * the join runs the full directed SSID survey (~3.5 s), which is the only
 * path the AP answers on the first try.
 */
u8 g_join_flush_scanq;

void rtl8814native_jlog(u16 ev, s32 a, s32 b)
{
	u32 i = __atomic_fetch_add(&g_jlog.next, 1, __ATOMIC_RELAXED);
	struct rtl_jlog_ev *e = &g_jlog.e[i % RTL_JLOG_N];

	e->t_ms = rtw_systime_to_ms(rtw_get_current_time());
	e->ev = ev;
	e->a = a;
	e->b = b;
}

void rtl8814native_note_rx_phy(u8 rate, const s8 *pwr, const s8 *snr)
{
	int i, best = -1;
	s32 n;

	if (rate < DESC_RATE6M)                 /* CCK reports no SNR */
		return;
	for (i = 0; i < 2; i++)
		if (snr[i] > 0 && (best < 0 || snr[i] > snr[best]))
			best = i;
	if (best < 0)
		return;
	n = ((s32)pwr[best] - snr[best]) * 16;
	if (n > -50 * 16 || n < -110 * 16)
		return;
	g_rx_noise_q4 = g_rx_noise_q4 ? g_rx_noise_q4 + (n - g_rx_noise_q4) / 16 : n;
}

void rtl8814native_note_ra_report(u8 *buf, u8 len)
{
	u8 n = len < 8 ? len : 8;
	g_rate_diag.ra_rpt_handled++;
	g_rate_diag.ra_rpt_last_len = len;
	_rtw_memset(g_rate_diag.ra_rpt_last, 0, sizeof(g_rate_diag.ra_rpt_last));
	if (buf && n)
		_rtw_memcpy(g_rate_diag.ra_rpt_last, buf, n);
}

void usb_recv_tasklet(void *priv)
{
	_pkt *pskb;
	_adapter *padapter = (_adapter *)priv;
	if (!padapter)
		return;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue))) {
		if (RTW_CANNOT_RX(padapter)) {
			rtw_skb_free(pskb);
			continue;
		}
		uint64_t t0 = rx_now_ns();
		recvbuf2recvframe(padapter, pskb);
		rx_account_ns(&g_rate_diag.rx_parse_ns, &g_rate_diag.rx_parse_ns_max, t0);

		/* Recycle like Linux: every frame was copied out by rtw_os_alloc_recvframe(),
		 * so the buffer is free; the cap bounds the pool to roughly one skb per URB. */
		if (g_rx_skb_recycle && skb_queue_len(&precvpriv->free_recv_skb_queue) < NR_RECVBUFF * 2) {
			skb_reset_tail_pointer(pskb);
			pskb->len = 0;
			skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		} else {
			rtw_skb_free(pskb);
		}
	}
}

/* ======================================================================
 * Buffer Resource Allocation & Packet Lifecycle
 * ====================================================================== */

int rtw_os_recvbuf_resource_alloc(_adapter *padapter, struct recv_buf *precvbuf)
{
	(void)padapter;
	precvbuf->irp_pending = _FALSE;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	precvbuf->pskb = NULL;
	precvbuf->pallocated_buf = precvbuf->pbuf = NULL;
	precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pend = NULL;
	precvbuf->transfer_len = 0;
	precvbuf->len = 0;
	return precvbuf->purb ? _SUCCESS : _FAIL;
}

int rtw_os_recvbuf_resource_free(_adapter *padapter, struct recv_buf *precvbuf)
{
	(void)padapter;
	if (precvbuf->purb) {
		usb_free_urb(precvbuf->purb);
		precvbuf->purb = NULL;
	}
	return _SUCCESS;
}

void rtw_os_xmit_complete(_adapter *padapter, struct xmit_frame *pxframe)
{
	(void)padapter;
	if (pxframe && pxframe->pkt) {
		dev_kfree_skb_any((struct sk_buff *)pxframe->pkt);
		pxframe->pkt = NULL;
	}
}

/* ======================================================================
 * Atomic Helpers
 * ====================================================================== */

void ATOMIC_SET(ATOMIC_T *v, int i)        { atomic_set((atomic_t *)v, i); }
int  ATOMIC_READ(ATOMIC_T *v)               { return atomic_read((atomic_t *)v); }
void ATOMIC_ADD(ATOMIC_T *v, int i)        { atomic_add(i, (atomic_t *)v); }
void ATOMIC_SUB(ATOMIC_T *v, int i)        { atomic_sub(i, (atomic_t *)v); }
void ATOMIC_INC(ATOMIC_T *v)               { atomic_inc((atomic_t *)v); }
void ATOMIC_DEC(ATOMIC_T *v)               { atomic_dec((atomic_t *)v); }
int  ATOMIC_ADD_RETURN(ATOMIC_T *v, int i) { return atomic_add_return(i, (atomic_t *)v); }
int  ATOMIC_SUB_RETURN(ATOMIC_T *v, int i) { return atomic_sub_return(i, (atomic_t *)v); }
int  ATOMIC_INC_RETURN(ATOMIC_T *v)         { return atomic_inc_return((atomic_t *)v); }
int  ATOMIC_DEC_RETURN(ATOMIC_T *v)         { return atomic_dec_return((atomic_t *)v); }
bool ATOMIC_INC_UNLESS(ATOMIC_T *v, int u) { return atomic_add_unless((atomic_t *)v, 1, u); }

/* ======================================================================
 * Status Codes & Math
 * ====================================================================== */

int RTW_STATUS_CODE(int error_code)
{
	if (error_code >= 0)
		return _SUCCESS;
	return _FAIL;
}

u64 rtw_modular64(u64 x, u64 y)
{
	return y ? (x % y) : 0;
}

u64 rtw_division64(u64 x, u64 y)
{
	return y ? (x / y) : 0;
}

/* ======================================================================
 * Delays & Sleep
 * ====================================================================== */

void rtw_msleep_os(int ms) { IOSleep((UInt32)ms); }
void rtw_usleep_os(int us) { IODelay((UInt32)us); }
void rtw_mdelay_os(int ms) { while (ms--) IODelay(1000); }
void rtw_udelay_os(int us) { IODelay((UInt32)us); }
void rtw_yield_os(void)    { IOSleep(0); }

/* ======================================================================
 * Globals, Parameters & String Helpers
 * ====================================================================== */

char *rtw_initmac = NULL;
int rtw_mc2u_disable = 0;

s32 rtl8814au_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);

void rtl8814au_xmit_tasklet(void *priv)
{
	_adapter *padapter = (_adapter *)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	while (1) {
		if (RTW_CANNOT_TX(padapter))
			break;
		if (rtw_xmit_ac_blocked(padapter) == _TRUE)
			break;
		if (rtl8814au_xmitframe_complete(padapter, pxmitpriv, NULL) == _FALSE)
			break;
	}
}

BOOLEAN is_null(char c)
{
	return (c == 0) ? _TRUE : _FALSE;
}

BOOLEAN is_all_null(char *c, int len)
{
	for (int i = 0; i < len; i++) {
		if (c[i] != 0) return _FALSE;
	}
	return _TRUE;
}

BOOLEAN is_eol(char c)
{
	return (c == '\r' || c == '\n') ? _TRUE : _FALSE;
}

BOOLEAN is_space(char c)
{
	return (c == ' ' || c == '\t') ? _TRUE : _FALSE;
}

BOOLEAN IsHexDigit(char chTmp)
{
	if ((chTmp >= '0' && chTmp <= '9') ||
	    (chTmp >= 'a' && chTmp <= 'f') ||
	    (chTmp >= 'A' && chTmp <= 'F'))
		return _TRUE;
	return _FALSE;
}

BOOLEAN is_alpha(char chTmp)
{
	if ((chTmp >= 'a' && chTmp <= 'z') ||
	    (chTmp >= 'A' && chTmp <= 'Z'))
		return _TRUE;
	return _FALSE;
}

char alpha_to_upper(char c)
{
	if (c >= 'a' && c <= 'z')
		c = c - ('a' - 'A');
	return c;
}

/* ======================================================================
 * Realtek Queue Helpers
 * ====================================================================== */

void _rtw_init_queue(_queue *pqueue)
{
	_rtw_init_listhead(&(pqueue->queue));
	_rtw_spinlock_init(&(pqueue->lock));
}

void _rtw_deinit_queue(_queue *pqueue)
{
	_rtw_spinlock_free(&(pqueue->lock));
}

u32 _rtw_queue_empty(_queue *pqueue)
{
	return rtw_is_list_empty(&(pqueue->queue));
}

u32 rtw_end_of_queue_search(_list *head, _list *plist)
{
	if (head == plist)
		return _TRUE;
	else
		return _FALSE;
}

/* ======================================================================
 * Mutex & Spinlock Wrappers
 * ====================================================================== */

void _rtw_mutex_init(_mutex *pmutex)       { rtl_mutex_init((struct mutex *)pmutex); }
void _rtw_mutex_free(_mutex *pmutex)       { rtl_mutex_destroy((struct mutex *)pmutex); }
void _rtw_spinlock_init(_lock *plock)      { rtl_spin_init((spinlock_t *)plock); }
void _rtw_spinlock_free(_lock *plock)      { rtl_spin_destroy((spinlock_t *)plock); }
void _rtw_spinlock(_lock *plock)           { spin_lock((spinlock_t *)plock); }
void _rtw_spinunlock(_lock *plock)         { spin_unlock((spinlock_t *)plock); }
void _rtw_spinlock_ex(_lock *plock)        { spin_lock((spinlock_t *)plock); }
void _rtw_spinunlock_ex(_lock *plock)      { spin_unlock((spinlock_t *)plock); }

/* ======================================================================
 * Semaphore Wrappers
 * ====================================================================== */

void _rtw_init_sema(_sema *sema, int init_val) { sema_init(sema, init_val); }
void _rtw_free_sema(_sema *sema)               { rtl_sema_destroy(sema); }
void _rtw_up_sema(_sema *sema)                 { up(sema); }
u32  _rtw_down_sema(_sema *sema)               { return down_interruptible(sema) ? _FAIL : _SUCCESS; }

/* ======================================================================
 * Time & Scheduler Helpers
 * ====================================================================== */

systime _rtw_get_current_time(void)
{
	return (systime)rtl_compat_jiffies();
}

u32 _rtw_systime_to_ms(systime stime)
{
	return (u32)stime;
}

systime _rtw_ms_to_systime(u32 ms)
{
	return (systime)ms;
}

systime _rtw_us_to_systime(u32 us)
{
	return (systime)(us / 1000);
}

s32 _rtw_get_passing_time_ms(systime start)
{
	return (s32)(_rtw_get_current_time() - start);
}

s32 _rtw_get_remaining_time_ms(systime end)
{
	return (s32)(end - _rtw_get_current_time());
}

s32 _rtw_get_time_interval_ms(systime start, systime end)
{
	return (s32)(end - start);
}

bool _rtw_time_after(systime a, systime b)
{
	return ((long)(b) - (long)(a) < 0);
}

void rtw_sleep_schedulable(int ms)
{
	IOSleep((UInt32)ms);
}

/* ======================================================================
 * Bitops & Suspend Locks
 * ====================================================================== */

void rtw_set_bit(int nr, unsigned long *addr)
{
	*addr |= (1UL << nr);
}

void rtw_clear_bit(int nr, unsigned long *addr)
{
	*addr &= ~(1UL << nr);
}

int rtw_test_and_clear_bit(int nr, unsigned long *addr)
{
	unsigned long mask = (1UL << nr);
	int old = (*addr & mask) != 0;
	*addr &= ~mask;
	return old;
}

void rtw_suspend_lock_init(void) {}
void rtw_suspend_lock_uninit(void) {}
void rtw_lock_suspend(void) {}
void rtw_unlock_suspend(void) {}
void rtw_resume_lock_suspend(void) {}
void rtw_resume_unlock_suspend(void) {}
void rtw_lock_suspend_timeout(u32 timeout_ms) { (void)timeout_ms; }
void rtw_lock_traffic_suspend_timeout(u32 timeout_ms) { (void)timeout_ms; }

u32 rtw_random32(void)
{
	u32 val = 0;
	rtl_get_random_bytes(&val, sizeof(val));
	return val;
}

/* ======================================================================
 * List Helpers
 * ====================================================================== */

void _rtw_init_listhead(_list *list)
{
	INIT_LIST_HEAD((struct list_head *)list);
}

u32 rtw_is_list_empty(_list *phead)
{
	return list_empty((struct list_head *)phead) ? _TRUE : _FALSE;
}

void rtw_list_insert_head(_list *plist, _list *phead)
{
	list_add((struct list_head *)plist, (struct list_head *)phead);
}

void rtw_list_insert_tail(_list *plist, _list *phead)
{
	list_add_tail((struct list_head *)plist, (struct list_head *)phead);
}

/* ======================================================================
 * Memory Allocation & Memory Operations
 * ====================================================================== */

void *_rtw_vmalloc(u32 sz)
{
	return rtl_malloc(sz);
}

void *_rtw_zvmalloc(u32 sz)
{
	void *p = rtl_malloc(sz);
	if (p) memset(p, 0, sz);
	return p;
}

void _rtw_vmfree(void *pbuf, u32 sz)
{
	(void)sz;
	rtl_free(pbuf);
}

void *_rtw_malloc(u32 sz)
{
	return rtl_malloc(sz);
}

void *_rtw_zmalloc(u32 sz)
{
	void *p = rtl_malloc(sz);
	if (p) memset(p, 0, sz);
	return p;
}

void _rtw_mfree(void *pbuf, u32 sz)
{
	(void)sz;
	rtl_free(pbuf);
}

void _rtw_memcpy(void *dst, const void *src, u32 sz)
{
	memcpy(dst, src, sz);
}

void _rtw_memmove(void *dst, const void *src, u32 sz)
{
	memmove(dst, src, sz);
}

int _rtw_memcmp(const void *dst, const void *src, u32 sz)
{
	return (memcmp(dst, src, sz) == 0) ? _TRUE : _FALSE;
}

void _rtw_memset(void *pbuf, int c, u32 sz)
{
	memset(pbuf, c, sz);
}

void *rtw_malloc2d(int h, int w, size_t size)
{
	int i;
	void **a = (void **)rtl_malloc(h * sizeof(void *) + h * w * size);
	if (!a) return NULL;
	char *data = (char *)(a + h);
	for (i = 0; i < h; i++)
		a[i] = data + i * w * size;
	return a;
}

void rtw_mfree2d(void *pbuf, int h, int w, int size)
{
	(void)h; (void)w; (void)size;
	rtl_free(pbuf);
}

/* ======================================================================
 * Packet File Operations (for xmit frame construction)
 * ====================================================================== */

uint rtw_remainder_len(struct pkt_file *pfile)
{
	return pfile->buf_len - ((SIZE_PTR)(pfile->cur_addr) - (SIZE_PTR)(pfile->buf_start));
}

void _rtw_open_pktfile(_pkt *pktptr, struct pkt_file *pfile)
{
	pfile->pkt = pktptr;
	pfile->cur_addr = pfile->buf_start = pktptr->data;
	pfile->pkt_len = pfile->buf_len = pktptr->len;
	pfile->cur_buffer = pfile->buf_start;
}

uint _rtw_pktfile_read(struct pkt_file *pfile, u8 *rmem, uint rlen)
{
	uint len = rtw_remainder_len(pfile);
	len = (rlen > len) ? len : rlen;

	if (rmem && pfile->pkt)
		memcpy(rmem, pfile->cur_addr, len);

	pfile->cur_addr += len;
	pfile->pkt_len -= len;
	return len;
}

sint rtw_endofpktfile(struct pkt_file *pfile)
{
	return (pfile->pkt_len == 0) ? _TRUE : _FALSE;
}

/* ======================================================================
 * Xmit OS Resources
 * ====================================================================== */

int rtw_os_xmit_resource_alloc(_adapter *padapter, struct xmit_buf *pxmitbuf, u32 alloc_sz, u8 flag)
{
	(void)padapter;
	if (alloc_sz > 0) {
		pxmitbuf->pallocated_buf = rtw_zmalloc(alloc_sz);
		if (!pxmitbuf->pallocated_buf)
			return _FAIL;
		pxmitbuf->pbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitbuf->pallocated_buf), XMITBUF_ALIGN_SZ);
	}
	if (flag) {
		for (int i = 0; i < 8; i++) {
			pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
			if (!pxmitbuf->pxmit_urb[i])
				return _FAIL;
		}
	}
	return _SUCCESS;
}

void rtw_os_xmit_resource_free(_adapter *padapter, struct xmit_buf *pxmitbuf, u32 free_sz, u8 flag)
{
	(void)padapter;
	if (flag) {
		for (int i = 0; i < 8; i++) {
			if (pxmitbuf->pxmit_urb[i]) {
				usb_free_urb(pxmitbuf->pxmit_urb[i]);
				pxmitbuf->pxmit_urb[i] = NULL;
			}
		}
	}
	if (free_sz > 0 && pxmitbuf->pallocated_buf) {
		rtw_mfree(pxmitbuf->pallocated_buf, free_sz);
		pxmitbuf->pallocated_buf = NULL;
	}
}

void rtw_os_pkt_complete(_adapter *padapter, _pkt *pkt)
{
	(void)padapter;
	if (pkt)
		dev_kfree_skb_any((struct sk_buff *)pkt);
}

void rtw_os_xmit_schedule(_adapter *padapter)
{
	if (!padapter)
		return;
	_irqL irqL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	_enter_critical_bh(&pxmitpriv->lock, &irqL);
	if (rtw_txframes_pending(padapter))
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	_exit_critical_bh(&pxmitpriv->lock, &irqL);
}

netdev_tx_t _rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev)
{
	(void)pnetdev;
	if (!pkt)
		return 0;

	struct dvobj_priv *dvobj = NULL;
	_adapter *padapter = NULL;
	/* Find primary adapter from pkt->dev or global */
	extern _adapter *rtl8814native_get_primary_adapter(void);
	padapter = rtl8814native_get_primary_adapter();
	if (!padapter || rtw_if_up(padapter) == _FALSE) {
		if (padapter)
			padapter->xmitpriv.tx_drop++;
		rtw_os_pkt_complete(padapter, pkt);
		return 0;
	}
	(void)dvobj;

	s32 res = rtw_xmit(padapter, &pkt);
	if (res < 0) {
		padapter->xmitpriv.tx_drop++;
		rtw_os_pkt_complete(padapter, pkt);
	}
	return 0;
}

/* ======================================================================
 * Recv OS Resources
 * ====================================================================== */

void rtw_os_free_recvframe(union recv_frame *precvframe)
{
	if (precvframe && precvframe->u.hdr.pkt) {
		dev_kfree_skb_any((struct sk_buff *)precvframe->u.hdr.pkt);
		precvframe->u.hdr.pkt = NULL;
	}
}

int rtw_os_recv_resource_init(struct recv_priv *precvpriv, _adapter *padapter)
{
	if (!precvpriv)
		return _FAIL;
	tasklet_init(&precvpriv->recv_tasklet,
	             (void (*)(unsigned long))usb_recv_tasklet,
	             (unsigned long)padapter);
	rtl_work_bind_rx(&precvpriv->recv_tasklet.work);
	skb_queue_head_init(&precvpriv->rx_skb_queue);
	skb_queue_head_init(&precvpriv->free_recv_skb_queue);
	return _SUCCESS;
}

int rtw_os_recv_resource_alloc(_adapter *padapter, union recv_frame *precvframe)
{
	(void)padapter;
	if (precvframe)
		precvframe->u.hdr.pkt = NULL;
	return _SUCCESS;
}

void rtw_os_recv_resource_free(struct recv_priv *precvpriv)
{
	if (!precvpriv)
		return;
	rtw_skb_queue_purge(&precvpriv->rx_skb_queue);
	rtw_skb_queue_purge(&precvpriv->free_recv_skb_queue);
}

int rtw_os_alloc_recvframe(_adapter *padapter, union recv_frame *precvframe, u8 *pdata, _pkt *pskb)
{
	int res = _SUCCESS;
	u8 shift_sz = 0;
	u32 skb_len, alloc_sz;
	_pkt *pkt_copy = NULL;
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;

	if (pdata == NULL) {
		precvframe->u.hdr.pkt = NULL;
		return _FAIL;
	}

	/* Never copy past the bytes the bulk IN URB actually delivered */
	if (pskb && (pdata < pskb->data ||
	             pdata + pattrib->pkt_len > pskb->data + pskb->len)) {
		precvframe->u.hdr.pkt = NULL;
		return _FAIL;
	}

	/*Modified by Albert 20101213, 8 bytes alignment for IP header*/
	shift_sz = pattrib->qos ? 6 : 0;

	skb_len = pattrib->pkt_len;
	if (pattrib->mfrag == 1 && pattrib->frag_num == 0) {
		if (skb_len <= 1650)
			alloc_sz = 1664;
		else
			alloc_sz = skb_len + 14;
	} else {
		alloc_sz = skb_len;
		/* 6 is for IP header 8 bytes alignment in QoS packet case.
		 * 8 is for skb->data 4 bytes alignment */
		alloc_sz += 14;
	}

	pkt_copy = rtw_skb_alloc(alloc_sz);
	if (pkt_copy) {
		pkt_copy->dev = padapter->pnetdev;
		pkt_copy->len = skb_len;
		precvframe->u.hdr.pkt = pkt_copy;
		skb_reserve(pkt_copy, 8 - ((SIZE_PTR)(pkt_copy->data) & 7));
		skb_reserve(pkt_copy, shift_sz);
		_rtw_memcpy(pkt_copy->data, pdata, skb_len);
		precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
		precvframe->u.hdr.rx_end = skb_end_pointer(pkt_copy);
	} else {
		precvframe->u.hdr.pkt = NULL;
		res = _FAIL;
	}

	return res;
}

int rtw_recv_indicatepkt(_adapter *padapter, union recv_frame *precv_frame)
{
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	_queue *pfree_recv_queue = &(precvpriv->free_recv_queue);

	if (!precv_frame || precv_frame->u.hdr.pkt == NULL) {
		if (precv_frame)
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
		return _FAIL;
	}

	rtw_os_recv_indicate_pkt(padapter, precv_frame->u.hdr.pkt, precv_frame);

	precv_frame->u.hdr.pkt = NULL;
	rtw_free_recvframe(precv_frame, pfree_recv_queue);
	return _SUCCESS;
}

void rtw_os_read_port(_adapter *padapter, struct recv_buf *precvbuf)
{
	if (!padapter || !precvbuf)
		return;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	precvbuf->ref_cnt--;
	if (precvbuf->pskb) {
		rtw_skb_free(precvbuf->pskb);
		precvbuf->pskb = NULL;
	}
	if (precvbuf->irp_pending == _FALSE)
		rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
}

/* ======================================================================
 * SKB & Timer & Misc Helpers
 * ====================================================================== */

struct sk_buff *_rtw_skb_alloc(u32 sz)
{
	return dev_alloc_skb(sz);
}

void _rtw_skb_free(struct sk_buff *skb)
{
	if (skb) dev_kfree_skb_any(skb);
}

struct sk_buff *_rtw_skb_copy(const struct sk_buff *skb)
{
	return skb ? skb_copy(skb, GFP_KERNEL) : NULL;
}

struct sk_buff *_rtw_skb_clone(struct sk_buff *skb)
{
	return skb ? skb_clone(skb, GFP_KERNEL) : NULL;
}

struct sk_buff *_rtw_pskb_copy(struct sk_buff *skb)
{
	return skb ? skb_clone(skb, GFP_KERNEL) : NULL;
}

void _rtw_skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(list)) != NULL)
		dev_kfree_skb_any(skb);
}

void *_rtw_usb_buffer_alloc(struct usb_device *dev, size_t size, dma_addr_t *dma)
{
	(void)dev;
	if (dma) *dma = 0;
	return rtl_malloc(size);
}

void _rtw_usb_buffer_free(struct usb_device *dev, size_t size, void *addr, dma_addr_t dma)
{
	(void)dev; (void)size; (void)dma;
	rtl_free(addr);
}

int _rtw_netif_rx(_nic_hdl ndev, struct sk_buff *skb)
{
	return rtl_netif_rx(ndev, skb);
}

void rtw_init_timer(_timer *ptimer, void *padapter, void *pfunc, void *ctx)
{
	(void)padapter;
	setup_timer((struct timer_list *)ptimer, (void (*)(unsigned long))pfunc, (unsigned long)ctx);
}

u32 rtw_atoi(u8 *s)
{
	u32 val = 0;
	if (!s) return 0;
	while (*s >= '0' && *s <= '9') {
		val = val * 10 + (*s - '0');
		s++;
	}
	return val;
}

void _rtw_mdelay_os(int ms, const char *func, const int line) { (void)func; (void)line; rtw_mdelay_os(ms); }
void _rtw_udelay_os(int us, const char *func, const int line) { (void)func; (void)line; rtw_udelay_os(us); }

int rtw_is_dir_readable(const char *path) { (void)path; return _FAIL; }
int rtw_is_file_readable(const char *path) { (void)path; return _FAIL; }
int rtw_is_file_readable_with_size(const char *path, u32 *sz) { (void)path; (void)sz; return _FAIL; }
int rtw_readable_file_sz_chk(const char *path, u32 sz) { (void)path; (void)sz; return _FAIL; }
int rtw_retrieve_from_file(const char *path, u8 *buf, u32 sz) { (void)path; (void)buf; (void)sz; return 0; }
int rtw_store_to_file(const char *path, u8 *buf, u32 sz) { (void)path; (void)buf; (void)sz; return 0; }
void rtw_free_netdev(struct net_device *netdev) { (void)netdev; }

/* ======================================================================
 * AP Mode & Station Assoc Stubs (Station Mode Driver)
 * ====================================================================== */

void _update_beacon(_adapter *padapter, u8 ie_id, u8 *oui, u8 tx, u8 flags, const char *tag)
{
	(void)padapter; (void)ie_id; (void)oui; (void)tx; (void)flags; (void)tag;
}

void rtw_ap_update_sta_ra_info(_adapter *padapter, struct sta_info *psta)
{
	(void)padapter; (void)psta;
}

int rtw_ht_operation_update(_adapter *padapter)
{
	(void)padapter;
	return _SUCCESS;
}

void init_mlme_ap_info(_adapter *padapter) { (void)padapter; }
void free_mlme_ap_info(_adapter *padapter) { (void)padapter; }
void start_ap_mode(_adapter *padapter) { (void)padapter; }
void stop_ap_mode(_adapter *padapter) { (void)padapter; }
void rtw_indicate_sta_assoc_event(_adapter *padapter, struct sta_info *psta) { (void)padapter; (void)psta; }
void rtw_indicate_sta_disassoc_event(_adapter *padapter, struct sta_info *psta) { (void)padapter; (void)psta; }

u8 ap_free_sta(_adapter *padapter, struct sta_info *psta, bool active, u16 reason, bool enqueue) { (void)padapter; (void)psta; (void)active; (void)reason; (void)enqueue; return 0; }
void ap_sta_info_defer_update(_adapter *padapter, struct sta_info *psta) { (void)padapter; (void)psta; }
void associated_clients_update(_adapter *padapter, u8 updated, u32 sta_info_type) { (void)padapter; (void)updated; (void)sta_info_type; }
void bss_cap_update_on_sta_join(_adapter *padapter, struct sta_info *psta) { (void)padapter; (void)psta; }
void sta_info_update(_adapter *padapter, struct sta_info *psta) { (void)padapter; (void)psta; }
void expire_timeout_chk(_adapter *padapter) { (void)padapter; }
void start_bss_network(_adapter *padapter, struct createbss_parm *parm) { (void)padapter; (void)parm; }
void update_bmc_sta(_adapter *padapter) { (void)padapter; }
void rtw_start_bss_hdl_after_chbw_decided(_adapter *adapter) { (void)adapter; }
void rtw_add_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index, u8 *data, u8 len) { (void)padapter; (void)pnetwork; (void)index; (void)data; (void)len; }
void rtw_remove_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index) { (void)padapter; (void)pnetwork; (void)index; }
void rtw_ap_restore_network(_adapter *padapter) { (void)padapter; }
void rtw_ap_update_bss_chbw(_adapter *adapter, WLAN_BSSID_EX *bss, u8 ch, u8 bw, u8 offset) { (void)adapter; (void)bss; (void)ch; (void)bw; (void)offset; }
void rtw_macaddr_acl_init(_adapter *adapter, u8 period) { (void)adapter; (void)period; }
void rtw_macaddr_acl_deinit(_adapter *adapter, u8 period) { (void)adapter; (void)period; }
void rtw_process_ht_action_smps(_adapter *padapter, u8 *ta, u8 ctrl_field) { (void)padapter; (void)ta; (void)ctrl_field; }
void rtw_process_public_act_bsscoex(_adapter *padapter, u8 *pframe, uint frame_len) { (void)padapter; (void)pframe; (void)frame_len; }

void rtw_ap_parse_sta_capability(_adapter *adapter, struct sta_info *sta, u8 *cap) { (void)adapter; (void)sta; (void)cap; }
void rtw_ap_parse_sta_ht_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems) { (void)adapter; (void)sta; (void)elems; }
u16 rtw_ap_parse_sta_security_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems) { (void)adapter; (void)sta; (void)elems; return 0; }
u16 rtw_ap_parse_sta_supported_rates(_adapter *adapter, struct sta_info *sta, u8 *tlv_ies, u16 tlv_ies_len) { (void)adapter; (void)sta; (void)tlv_ies; (void)tlv_ies_len; return 0; }
void rtw_ap_parse_sta_vht_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems) { (void)adapter; (void)sta; (void)elems; }
void rtw_ap_parse_sta_wmm_ie(_adapter *adapter, struct sta_info *sta, u8 *tlv_ies, u16 tlv_ies_len) { (void)adapter; (void)sta; (void)tlv_ies; (void)tlv_ies_len; }

/* ======================================================================
 * OS Packet & Netdev State Helpers
 * ====================================================================== */

void rtw_os_pkt_free(_pkt *pkt) { if (pkt) dev_kfree_skb_any((struct sk_buff *)pkt); }
void *rtw_os_pkt_data(_pkt *pkt) { return pkt ? ((struct sk_buff *)pkt)->data : NULL; }
u32 rtw_os_pkt_len(_pkt *pkt) { return pkt ? ((struct sk_buff *)pkt)->len : 0; }

void rtw_os_indicate_connect(_adapter *adapter)
{
	struct net_device *ndev = adapter ? adapter->pnetdev : rtl8814native_get_netdev();
	if (ndev) rtl_netif_carrier(ndev, 1);
}

void rtw_os_indicate_disconnect(_adapter *adapter, u16 reason, u8 locally_generated)
{
	(void)reason; (void)locally_generated;
	struct net_device *ndev = adapter ? adapter->pnetdev : rtl8814native_get_netdev();
	if (ndev) rtl_netif_carrier(ndev, 0);
}

void rtw_os_indicate_scan_done(_adapter *adapter, bool aborted)
{
	(void)adapter; (void)aborted;
}

void rtw_os_recv_indicate_pkt(_adapter *padapter, _pkt *pkt, union recv_frame *rframe)
{
	(void)rframe;
	if (!pkt)
		return;

	struct sk_buff *skb = (struct sk_buff *)pkt;
	/* Intercept 802.1X EAPOL (0x888E) for our in-kernel WPA2-PSK supplicant */
	if (rtl8814_wpa2_rx_eapol(padapter, skb)) {
		dev_kfree_skb_any(skb);
		return;
	}

	struct net_device *ndev = padapter ? padapter->pnetdev : rtl8814native_get_netdev();
	if (ndev)
		rtl_netif_rx(ndev, skb);
	else
		dev_kfree_skb_any(skb);
}

_pkt *rtw_os_alloc_msdu_pkt(union recv_frame *prframe, const u8 *da, const u8 *sa, u8 *msdu, u16 msdu_len)
{
	(void)prframe;
	u16 eth_type;
	u8 *data_ptr;
	_pkt *sub_skb;

	sub_skb = rtw_skb_alloc(msdu_len + 14);
	if (!sub_skb)
		return NULL;

	skb_reserve(sub_skb, 14);
	data_ptr = (u8 *)skb_put(sub_skb, msdu_len);
	_rtw_memcpy(data_ptr, msdu, msdu_len);

	eth_type = RTW_GET_BE16(&sub_skb->data[6]);

	if (sub_skb->len >= 8 &&
	    ((_rtw_memcmp(sub_skb->data, rtw_rfc1042_header, SNAP_SIZE) &&
	      eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
	     _rtw_memcmp(sub_skb->data, rtw_bridge_tunnel_header, SNAP_SIZE))) {
		skb_pull(sub_skb, SNAP_SIZE);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), sa, ETH_ALEN);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), da, ETH_ALEN);
	} else {
		u16 len = htons(sub_skb->len);
		_rtw_memcpy(skb_push(sub_skb, 2), &len, 2);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), sa, ETH_ALEN);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), da, ETH_ALEN);
	}

	return sub_skb;
}

int rtw_os_recvframe_duplicate_skb(_adapter *padapter, union recv_frame *pcloneframe, _pkt *pskb)
{
	(void)padapter;
	if (!pcloneframe || !pskb)
		return _FAIL;

	_pkt *pkt_copy = rtw_skb_clone(pskb);
	if (pkt_copy == NULL)
		return _FAIL;

	pcloneframe->u.hdr.pkt = pkt_copy;
	return _SUCCESS;
}

void rtw_os_wake_queue_at_free_stainfo(_adapter *padapter, int *qcnt_freed)
{
	(void)padapter; (void)qcnt_freed;
}

void rtw_rframe_set_os_pkt(union recv_frame *rframe)
{
	if (!rframe || !rframe->u.hdr.pkt)
		return;
	_pkt *skb = rframe->u.hdr.pkt;
	skb->data = rframe->u.hdr.rx_data;
	skb_set_tail_pointer(skb, rframe->u.hdr.len);
	skb->len = rframe->u.hdr.len;
}

void rtw_handle_tkip_mic_err(_adapter *padapter, struct sta_info *sta, u8 bgroup)
{
	(void)padapter; (void)sta; (void)bgroup;
}

int rtw_recv_monitor(_adapter *padapter, union recv_frame *precv_frame)
{
	(void)padapter; (void)precv_frame;
	return _FAIL;
}

void rtw_set_tx_chksum_offload(_pkt *pkt, struct pkt_attrib *pattrib)
{
	(void)pkt; (void)pattrib;
}

void rtw_intf_start(_adapter *padapter)
{
	if (!padapter)
		return;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(padapter);
	rtw_hal_inirp_init(padapter);
	if (hal)
		hal->usb_intf_start = _TRUE;
}

void rtw_intf_stop(_adapter *padapter)
{
	if (!padapter)
		return;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(padapter);
	rtw_hal_inirp_deinit(padapter);
	rtw_write_port_cancel(padapter);
	if (hal)
		hal->usb_intf_start = _FALSE;
}

u32 rtw_start_drv_threads(_adapter *padapter)
{
	u32 _status = _SUCCESS;
	if (!padapter)
		return _FAIL;

	if (is_primary_adapter(padapter)) {
		if (padapter->cmdThread == NULL) {
			padapter->cmdThread = kthread_run(rtw_cmd_thread, padapter, "RTW_CMD_THREAD");
			if (IS_ERR(padapter->cmdThread)) {
				padapter->cmdThread = NULL;
				_status = _FAIL;
			} else {
				_rtw_down_sema(&padapter->cmdpriv.start_cmdthread_sema);
			}
		}
	}
	rtw_hal_start_thread(padapter);
	return _status;
}

void rtw_stop_drv_threads(_adapter *padapter)
{
	if (!padapter)
		return;
	if (is_primary_adapter(padapter))
		rtw_stop_cmd_thread(padapter);
	rtw_hal_stop_thread(padapter);
}

void rtw_cancel_all_timer(_adapter *padapter)
{
	if (!padapter)
		return;
	_cancel_timer_ex(&padapter->mlmepriv.assoc_timer);
	_cancel_timer_ex(&padapter->mlmepriv.scan_to_timer);
	_cancel_timer_ex(&adapter_to_dvobj(padapter)->dynamic_chk_timer);
	_cancel_timer_ex(&adapter_to_dvobj(padapter)->periodic_tsf_update_end_timer);
	_cancel_timer_ex(&(adapter_to_pwrctl(padapter)->pwr_state_check_timer));
	rtw_hal_dm_deinit(padapter);
}

u8 rtw_reset_drv_sw(_adapter *padapter)
{
	(void)padapter;
	return _SUCCESS;
}

int rtw_suspend_free_assoc_resource(_adapter *padapter)
{
	(void)padapter;
	return _SUCCESS;
}

void rtw_report_sec_ie(_adapter *adapter, u8 authmode, u8 *sec_ie)
{
	(void)adapter; (void)authmode; (void)sec_ie;
}

u8 map_read8(const struct map_t *map, u16 offset)
{
	(void)map; (void)offset;
	return 0;
}

void rtw_buf_free(u8 **buf, u32 *buf_len)
{
	if (!buf || !buf_len) return;
	if (*buf) {
		rtl_free(*buf);
		*buf = NULL;
	}
	*buf_len = 0;
}

void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len)
{
	if (!buf || !buf_len) return;
	if (*buf != src) {
		if (*buf) rtl_free(*buf);
		*buf = NULL;
		*buf_len = 0;
		if (src && src_len > 0) {
			*buf = rtl_malloc(src_len);
			if (*buf) {
				memcpy(*buf, src, src_len);
				*buf_len = src_len;
			}
		}
	}
}

#undef htons
#undef ntohs
unsigned short int htons(unsigned short int x)
{
	return OSSwapHostToBigInt16(x);
}

unsigned short int ntohs(unsigned short int x)
{
	return OSSwapBigToHostInt16(x);
}

void rtw_reset_securitypriv(_adapter *adapter)
{
	if (!adapter) return;
	_irqL irqL;
	_enter_critical_bh(&adapter->security_key_mutex, &irqL);
	struct security_priv *psec_priv = &adapter->securitypriv;
	psec_priv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
	psec_priv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psec_priv->dot11PrivacyKeyIndex = 0;
	psec_priv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psec_priv->dot118021XGrpKeyid = 1;
	psec_priv->ndisauthtype = Ndis802_11AuthModeOpen;
	psec_priv->ndisencryptstatus = Ndis802_11WEPDisabled;
	psec_priv->extauth_status = WLAN_STATUS_UNSPECIFIED_FAILURE;
	_exit_critical_bh(&adapter->security_key_mutex, &irqL);
}

u8 rtw_set_hal_ops(_adapter *padapter)
{
	if (rtw_hal_data_init(padapter) == _FAIL)
		return _FAIL;
#ifdef CONFIG_RTL8814A
	if (rtw_get_chip_type(padapter) == RTL8814A)
		rtl8814au_set_hal_ops(padapter);
#endif
	if (_FAIL == rtw_hal_ops_check(padapter))
		return _FAIL;
	if (hal_spec_init(padapter) == _FAIL)
		return _FAIL;
	return _SUCCESS;
}



