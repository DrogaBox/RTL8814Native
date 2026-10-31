/*
 * RTL8814Native — vendor glue: the bridge between the C vendor driver
 * (Source/rtl8814au, ported per phase) and the IOKit C++ side.
 *
 * Every extern "C" symbol below is what the vendor code calls after
 * its os_dep/linux C sources are replaced by os_dep/macos equivalents
 * (or by these shims directly). NOTHING else in Driver/ may be reached
 * from vendor code.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_VENDOR_GLUE_H
#define _RTL8814_VENDOR_GLUE_H

#include <linux/types.h>

#define RTL8814NATIVE_VERSION     "0.3.0-phase3"
#define RTL8814NATIVE_PHASE       3
#define RTL8814NATIVE_UC_VERSION  1

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles (real definitions live in the Compat headers) */
struct usb_device;
struct usb_interface;
struct urb;
struct net_device;
struct sk_buff;

/* ---------------------------------------------------------------------
 * Transport registry — the current RTLUsbDevice handle
 * (struct usb_device is an opaque alias of RTLUsbDevice)
 * --------------------------------------------------------------------- */
void rtl8814native_set_usb_device(struct usb_device *handle);
struct usb_device *rtl8814native_get_usb_device(void);
struct usb_endpoint_descriptor *rtl_usb_find_bulk_in(struct usb_device *dev);
struct usb_endpoint_descriptor *rtl_usb_find_bulk_out(struct usb_device *dev);
u8 rtl_usb_get_out_ep_info(struct usb_device *dev, u8 *ep_addrs, u8 max_eps);

/* Called by RTL8814Native::start before vendor probe so the ethernet
 * adapter's netdev exists when the ported code asks for it. */
void rtl8814native_set_netdev(struct net_device *ndev);
struct net_device *rtl8814native_get_netdev(void);

/* Radio Bring-Up Telemetry */
struct rtl8814_radio_telemetry {
	int      probe_status;
	uint32_t reg_sys_cfg;
	uint8_t  chip_type;
	uint8_t  cut_version;
	uint8_t  mac[6];
	int      hal_init_status;
	uint8_t  fw_ready;
};

void rtl8814native_get_radio_telemetry(struct rtl8814_radio_telemetry *out);

/* Phase 2 & 3: Scan, Join, WPA2 Status, and TX Data Path */
#define RTL_MAX_SCAN_ENTRIES 32

struct rtl_scan_entry {
	char     ssid[33];
	uint8_t  bssid[6];
	int16_t  rssi;
	uint8_t  channel;
	uint8_t  security; /* 0=Open, 1=WEP, 2=WPA, 3=WPA2 */
};

/* RX drop-reason + TX bulk OUT counters (VendorOsDep.c) */
struct rtl8814_trx_diag {
	uint32_t urb_bad_len;       /* completions outside [RXDESC_SIZE, MAX_RECVBUF_SZ] */
	uint32_t last_bad_urb_len;
	uint32_t last_urb_len;
	uint32_t drop_no_recvframe;
	uint32_t drop_crc;
	uint32_t drop_icv;
	uint32_t drop_bad_pktlen;
	uint32_t drop_runt;
	uint32_t drop_alloc;
	uint32_t rx_normal;
	uint32_t rx_c2h;
	uint32_t rx_other_rpt;
	uint32_t rx_mgmt;
	uint32_t rx_beacon;
	uint32_t rx_probe_rsp;
	uint32_t rx_ctrl;
	uint32_t rx_data;
	uint8_t  last_desc[24];     /* first RX descriptor of the last bulk IN transfer */
	uint32_t tx_write_ok;
	uint32_t tx_write_err;
	int32_t  tx_last_err;       /* last negative usb_bulk_msg status */
	uint32_t tx_last_ep;
	uint32_t rx_carry;          /* partial frames stashed for the next transfer */
	uint32_t rx_reassembled;    /* transfers parsed together with a stashed tail */
	uint32_t rx_stage_dropped;  /* stashed tails discarded (overflow / too old) */
};

/* Ring of the last RX_XFER_LOG_N bulk IN transfers, raw head/tail bytes */
#define RX_XFER_LOG_N 16
struct rtl8814_rx_xfer {
	uint32_t seq;
	uint32_t len;
	uint8_t  head[32];
	uint8_t  tail[32];
};
struct rtl8814_rx_log {
	uint32_t next_seq;
	struct rtl8814_rx_xfer xfer[RX_XFER_LOG_N];
};
void rtl8814native_get_rx_log(struct rtl8814_rx_log *out);
struct rtl_jlog;
void rtl8814native_get_join_log(struct rtl_jlog *out);

/* Live driver options (rtl8814native_reg_access op 3, addr = option id) */
enum {
	RTL_OPT_RX_REASSEMBLE = 0,  /* carry partial frames across bulk IN transfers */
	RTL_OPT_RX_SKB_RECYCLE = 1, /* reuse bulk IN skbs (Linux behaviour) instead of a 32KB alloc per URB */
	RTL_OPT_RESET_PERF = 2,     /* zero g_rate_diag RX counters/timings (value ignored) */
};
extern struct rtl8814_trx_diag g_trx_diag;

/* Negotiated PHY rate + RX aggregation (throughput investigation) */
struct rtl8814_rate_diag {
	uint8_t  cur_bw;            /* mlmeext cur_bwmode: CHANNEL_WIDTH_* (0=20, 1=40, 2=80) */
	uint8_t  cur_ch_offset;
	uint8_t  wireless_mode;     /* mlmeext cur_wireless_mode: WIRELESS_11* bits */
	uint8_t  ht_option;
	uint8_t  vht_option;
	uint8_t  vht_sgi_80m;
	uint8_t  vht_highest_rate;  /* DESC_RATE* */
	uint8_t  vht_ampdu_len;
	uint8_t  sta_bw;            /* psta->cmn.bw_mode */
	uint8_t  ra_bw;             /* psta->cmn.ra_info.ra_bw_mode */
	uint8_t  tx_rate;           /* DESC_RATE*, FW rate-adaptation report */
	uint8_t  tx_sgi;
	uint8_t  tx_bw;             /* ra_info.curr_tx_bw */
	uint8_t  rx_rate;           /* psta->curr_rx_rate (unicast data, phy status) */
	uint8_t  hal_tx_nss;
	uint8_t  hal_rx_nss;
	uint8_t  ba_rx_tids;        /* bitmap of TIDs with an RX BA (A-MPDU) session */
	uint8_t  ba_rx_wsize;       /* largest RX BA window */
	uint8_t  ampdu_tx_tids;     /* htpriv.agg_enable_bitmap */
	uint8_t  last_rx_rate;      /* last unicast data frame, from its RX descriptor */
	uint8_t  last_rx_bw;
	uint8_t  last_rx_sgi;
	uint8_t  last_rx_ldpc;
	uint8_t  last_rx_stbc;
	uint32_t cur_rx_tp;         /* Mbps, dvobj traffic_stat (watchdog) */
	uint32_t cur_tx_tp;
	uint32_t rx_xfers;          /* bulk IN transfers parsed */
	uint32_t rx_xfer_max_len;
	uint32_t rx_xfer_max_pkts;
	uint32_t rx_xfer_pkts;      /* frames parsed out of those transfers */
	uint64_t rx_xfer_bytes;
	uint16_t reg_trxdma_ctrl;   /* 0x010C: RXDMA_AGG_EN = bit2 */
	uint16_t reserved;
	uint32_t reg_rxdma_agg_pg_th; /* 0x0280: [7:0] size, [15:8] timeout, bit31 USB agg */
	uint32_t reg_rxdma_mode;      /* 0x0290 */
	/* RX path cost (ns) and reorder health */
	uint32_t rx_skb_recycle;    /* RTL_OPT_RX_SKB_RECYCLE */
	uint32_t rx_skb_allocs;     /* fresh 32KB bulk IN skbs allocated */
	uint32_t rx_cb_cnt;         /* bulk IN completions timed */
	uint32_t rx_cb_ns_max;
	uint64_t rx_cb_ns;          /* completion handler incl. resubmit */
	uint64_t rx_parse_ns;       /* recvbuf2recvframe, per transfer */
	uint32_t rx_parse_ns_max;
	uint32_t rx_backlog_max;    /* rx_skb_queue depth waiting for the tasklet */
	uint32_t ampdu_drop;        /* recv_priv dbg_rx_ampdu_* (truncated) */
	uint32_t ampdu_forced_indicate;
	uint32_t ampdu_loss;
	uint32_t ampdu_window_shift;
	uint32_t rx_drop;
	uint32_t warm_start;        /* probe: SYS_CLKR+1 | CR << 8 | powered_off << 16 */
	/* A-MPDU negotiation state */
	uint32_t num_tx_ok_period;  /* LinkDetectInfo.NumTxOkInPeriod (ADDBA req needs >= 100) */
	uint8_t  sta_ampdu_enable;  /* psta->htpriv.ampdu_enable */
	uint8_t  tx_candidate_tids; /* TX ADDBA requested, no response yet */
	uint8_t  drv_tx_bw_mode;    /* adapter->driver_tx_bw_mode (0x21 = 5G 80 / 2G 40) */
	uint8_t  drv_rx_ampdu_factor; /* 0xFF = HAL default */
	uint8_t  fix_rx_ampdu_accept; /* 0xFF = auto */
	uint8_t  reserved3[3];
	/* Rate ceiling: what the AP offers vs the mask the FW RA gets */
	uint8_t  ap_vht_rx_map[2];  /* vhtpriv.vht_mcs_map_infra_ap (AP RX MCS map) */
	uint8_t  ap_vht_tx_map[2];  /* vhtpriv.vht_mcs_map_tx_infra_ap */
	uint8_t  sta_vht_map[2];    /* psta->vhtpriv.vht_mcs_map (negotiated) */
	uint8_t  ra_rate_id;        /* psta->cmn.ra_info.rate_id */
	uint8_t  sta_mimo_type;     /* psta->cmn.mimo_type: RF_1T1R=0, 2T2R=3, 3T3R=?, see rf_type */
	uint8_t  ap_ht_mcs[4];      /* AP HT cap supp_mcs_set[0..3] (1 byte per SS) */
	uint8_t  reserved4[4];
	uint64_t ra_mask;           /* psta->cmn.ra_info.ramask */
	/* C2H (FW -> driver) reports: the FW RA rate shown above only moves
	 * when an RA report (id 0x0C) reaches phydm */
	uint32_t c2h_id_cnt[16];    /* C2H packets by id (0x00-0x0F) */
	uint32_t c2h_id_other;      /* ids >= 0x10 */
	uint32_t c2h_parse_fail;
	uint32_t ra_rpt_handled;    /* phydm_c2h_ra_report_handler() calls */
	uint8_t  ra_rpt_last[8];    /* its cmd_buf[0..7]: rate, macid, ..., [5] PER, [6] BW */
	uint8_t  ra_rpt_last_len;
	uint8_t  reserved5[7];
};
extern struct rtl8814_rate_diag g_rate_diag;

struct rtl8814_link_status {
	uint32_t connected;  /* 1 if _FW_LINKED */
	uint32_t wpa2_state; /* 0=Idle, 1=WaitMsg1, 2=WaitMsg3, 3=Completed, 4=Failed */
	int32_t  rssi;
	uint32_t channel;
	uint8_t  bssid[6];
	char     ssid[33];
	uint8_t  rfe_type;
	uint8_t  rf_type;
	uint8_t  channel_plan;
	uint8_t  max_chan_nums;
	uint32_t rx_urb_submit_ok;
	uint32_t rx_urb_submit_err;
	uint32_t rx_urb_complete_ok;
	uint32_t rx_urb_complete_err;
	uint32_t rx_frames_decoded;
	uint32_t cmd_issued_cnt;
	uint32_t scan_state;
	uint32_t scanned_bss_cnt;
	struct rtl8814_trx_diag trx;
	uint32_t reg_rcr;
	uint16_t reg_rxfltmap0;     /* mgmt subtype filter */
	uint16_t reg_rxfltmap2;     /* data subtype filter */
	uint32_t usb_speed;         /* RTW_USB_SPEED_*: 1=FS, 2=HS, 3=SS (from bulk IN wMaxPacketSize) */
	uint32_t bulk_in_mps;
	uint32_t rx_reassemble;     /* RTL_OPT_RX_REASSEMBLE */
	uint32_t usb_switch;        /* RTL_USB_SWITCH_* flags */
	struct rtl8814_rate_diag rate;
};

/* USB 2.0 -> 3.0 mode switch (Linux usb_reprobe_switch_usb_mode, switch_usb_mode=1) */
enum {
	RTL_USB_SWITCH_EFUSE_OK     = 1u << 0, /* efuse 0x0e bit4: driver may switch modes */
	RTL_USB_SWITCH_ISSUED       = 1u << 1, /* HS->SS switch + USB disconnect issued (once per kext load) */
	RTL_USB_SWITCH_SS_FINALIZED = 1u << 2, /* enumerated SS, 0x70[1]/0x3e[0] cleared */
};
/* vendor_probe() return value: the chip dropped off the bus to re-enumerate as USB 3.0 */
#define RTL_PROBE_USB3_SWITCH 1

int  rtl8814native_trigger_scan(void);
int  rtl8814native_get_scan_results(struct rtl_scan_entry *entries, uint32_t max_entries, uint32_t *out_count);
int  rtl8814native_join_network(const char *ssid, const char *psk, uint32_t auth_mode);
int  rtl8814native_disconnect(void);
int  rtl8814native_set_knob(uint32_t knob, uint32_t value);

/* Radio ABI backends (RTLRadioABI.h, VendorProbe.c) */
struct rtl_radio_bss;
struct rtl_radio_chan;
struct rtl_radio_join;
struct rtl_radio_status;
int  rtl8814native_radio_scan_copy(struct rtl_radio_bss *out, uint32_t *count);
int  rtl8814native_radio_channels(struct rtl_radio_chan *out, uint32_t *count);
int  rtl8814native_radio_join(struct rtl_radio_join *j);
void rtl_sae_join_exit(void);
void rtl8814native_radio_status(struct rtl_radio_status *st);
void rtl8814native_radio_mac(uint8_t mac[6]);
int  rtl8814native_radio_set_mac(const uint8_t mac[6]);
/* AWDLTx.c: one raw management frame, header included, no FCS */
int  rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len);
/* AWDLTx.c: port 1 answers for this MAC on the AWDL BSSID, NULL = off */
int  rtl8814native_radio_awdl_port(const uint8_t *mac);
void rtl8814native_get_link_status(struct rtl8814_link_status *out);
/* Live register access for diagnostics: op 0=MAC read, 1=MAC write, 2=RF read (addr = path<<8 | reg),
 * 3=set driver option (addr = RTL_OPT_*, out = previous value) */
int  rtl8814native_reg_access(uint32_t op, uint32_t width, uint32_t addr, uint32_t value, uint32_t *out);
int  rtl8814native_vendor_xmit(struct sk_buff *skb);

int rtl8814native_vendor_probe(struct usb_device *udev, unsigned long driver_info);
int rtl8814native_vendor_remove(void);
/* System power-off / restart: power the chip down (card disable) so it does
 * not stay on across the reboot. Mirrors Linux rtw_dev_shutdown(); memory is
 * kept, rtl8814native_vendor_remove() may still run afterwards. */
void rtl8814native_vendor_shutdown(void);

/* System sleep: same card disable as shutdown, but every allocation stays
 * so the wake path can re-run HAL init + firmware on the same _adapter and
 * re-join the last network. Drops stale RX reassembly state. */
int  rtl8814native_vendor_suspend(void);
int  rtl8814native_vendor_resume(void);

/* Re-join the network remembered by the last successful join (no-op if none).
 * Used when a new instance took the radio over from a stale one across sleep. */
void rtl8814native_rejoin_last(void);

/* Clear the USB bulk IN reassembly stage buffer (VendorOsDep). */
void rtl8814native_rx_stage_reset(void);

/* ---------------------------------------------------------------------
 * PHASE 1 INTEGRATION POINTS — the future port of
 * os_dep/linux/usb_intf.c:rtw_drv_init() (line 1499) and
 * os_dep/linux/os_intfs.c (netdev registration).
 *
 * int rtl8814native_vendor_probe(struct usb_device *udev,
 *                                unsigned long driver_info);
 *   → rtw_os_primary_adapter_init():
 *       dvobj_priv setup (include/drv_types.h:1072)
 *       rtw_hal_init()          ← hal/rtl8814a/rtl8814a_hal_init.c
 *       FirmwareDownload8814A() ← hal/rtl8814a/hal8814a_fw.c
 *                                 (array_mp_8814a_fw_nic, 68320 bytes)
 *       start threads: rtw_cmd_thread / rtw_recv_thread /
 *                      rtw_xmit_thread / event_thread
 *                      (os_dep/linux/os_intfs.c:2145-2184)
 * int rtl8814native_vendor_remove(void);
 * --------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* _RTL8814_VENDOR_GLUE_H */
