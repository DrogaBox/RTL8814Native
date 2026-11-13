/*
 * RTL8814Native — raw 802.11 frame sender for the radio ABI
 *
 * Another kext (the AWDL layer) hands a complete management frame, or an AWDL
 * data frame: 802.11 header and body, no FCS. It goes out on the current
 * operating channel through the shared management TX queue, as SAEAuth.c
 * does for its authentication frames. The hardware numbers it (HWSEQ_EN for non-QoS
 * frames); everything else is sent as given. Frames on the AWDL BSSID never
 * go at a CCK rate. Category 127 action frames are not robust, so the
 * PMF hook in dump_mgntframe leaves them alone.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include <drv_types.h>
#include <hal_data.h>
#include "RTLAwdlSim.h"
#include "RTLRadioABI.h"

int rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len);
int rtl8814native_radio_awdl_port(const uint8_t *mac);
int rtl8814native_radio_set_channel(uint8_t ch);
int rtl8814native_radio_awdl_offch(uint8_t ch);
int rtl8814native_awdl_set_park_bw(uint32_t mhz, uint32_t *prev);
int rtl8814native_awdl_set_prot(uint32_t mode, uint32_t *prev);
int rtl8814native_awdl_set_scan_hold(uint32_t seconds, uint32_t *prev);
int rtl8814native_awdl_cal_enter(void *adapter);
void rtl8814native_awdl_cal_exit(void *adapter);
int rtl8814native_awdl_cal_stats(uint32_t *out);
void rtl8814native_awdl_note_peer(int data);
void rtl8814native_awdl_scan_hold_decide(_adapter *padapter);
u8 rtl8814native_awdl_scan_hold(void);
int rtl8814native_radio_awdl_rate(uint32_t rate);
void rtl8814native_radio_awdl_txstat(struct rtl_radio_awdl_txstat *out);
int rtl8814native_radio_awdl_agg(struct rtl_radio_awdl_agg *cfg);
void rtl8814native_awdl_tx_report(int lost, u8 retries, u8 rate);
extern _adapter *rtl8814native_get_primary_adapter(void);
extern int rtl8814native_tx_urbs_inflight(void);
extern u64 rtl8814native_uptime_us(void);
extern int rtl8814native_awdl_rx(struct net_device *dev, const u8 *frame, u32 len, s8 rssi,
                                 u8 channel);

static const u8 rtl_awdl_bssid[ETH_ALEN] = { 0x00, 0x25, 0x00, 0xFF, 0x94, 0x73 };

/* A scan is back on its home channel between two groups of channels: the BSS
 * when linked, where it waits for a slot started now to return
 * (sitesurvey_close_awdl_backop), or the parked channel. Changes under
 * offch_mutex. */
u8 rtl8814native_awdl_backop;
u8 rtl8814native_awdl_port1;

/*
 * A scan without an association takes the radio through every channel, about
 * 4 s in which AWDL neither sends nor hears. While an AWDL frame came in
 * during the last rtl_awdl_scan_hold_s seconds, AWDL data moved in that time
 * too and the radio is parked on a social channel, such a scan returns to the
 * parked channel between groups of channels, as a linked scan returns to its
 * BSS (rtw_scan_backop_decision). The scan then lasts twice as long, so with
 * no peer around, or with peers that only keep their sync frames going, it
 * runs as before. Data is a data frame from a peer or a unicast one to a peer.
 * RTL_OPT_AWDL_SCAN_HOLD sets the seconds, 0 turns it off.
 */
#define RTL_AWDL_SCAN_HOLD_MAX_S 60
static u32 rtl_awdl_scan_hold_s = 10;
static u64 rtl_awdl_peer_us;
static u64 rtl_awdl_data_us;

/* RX thread: the AWDL hook took a frame, a data frame when data is set */
void rtl8814native_awdl_note_peer(int data)
{
	u64 now = rtl8814native_uptime_us();

	rtl_awdl_peer_us = now;
	if (data)
		rtl_awdl_data_us = now;
}

/* Decided once for the scan being queued (rtw_scan_timeout_decision), so its
 * timeout counts the returns it will make. */
static u8 rtl_awdl_scan_hold_now;

void rtl8814native_awdl_scan_hold_decide(_adapter *padapter)
{
	u64 at = rtl_awdl_peer_us;
	u64 data = rtl_awdl_data_us;
	u64 now = rtl8814native_uptime_us();
	u64 win = (u64)rtl_awdl_scan_hold_s * 1000000;
	u8 ch = padapter->mlmeextpriv.cur_channel;

	rtl_awdl_scan_hold_now = rtl_awdl_scan_hold_s && at && data && rtl8814native_awdl_port1 &&
	                         (ch == 6 || ch == 44 || ch == 149) &&
	                         now - at < win && now - data < win;
}

u8 rtl8814native_awdl_scan_hold(void)
{
	return rtl_awdl_scan_hold_now;
}

int rtl8814native_awdl_set_scan_hold(uint32_t seconds, uint32_t *prev)
{
	if (seconds > RTL_AWDL_SCAN_HOLD_MAX_S)
		return -EINVAL;
	if (prev)
		*prev = rtl_awdl_scan_hold_s;
	rtl_awdl_scan_hold_s = seconds;
	return 0;
}

/* The AWDL layer's rate for unicast data (RTL_RADIO_FN_AWDL_RATE), as the
 * driver's rate code, 0 = the fixed 12 Mb/s; and the CCX reports of the
 * frames sent at it (RTL_RADIO_FN_AWDL_TXSTAT). */
static u8 rtl_awdl_data_mrate;
static u8 rtl_awdl_park_bw = CHANNEL_WIDTH_20;  /* RTL_OPT_AWDL_BW */
static u32 rtl_awdl_txstat[6];
/* A-MPDU for AWDL QoS data (RTL_RADIO_FN_AWDL_AGG): descriptor rate (0 = off),
 * width, short GI, MPDUs per A-MPDU and density, packed in one word so the TX
 * path reads them together. */
#define RTL_AWDL_MGMT_RESERVE  16      /* management buffers AWDL data never takes */
static u32 rtl_awdl_agg_cfg;
/* Protection of those frames: NONE_VCS, RTS_CTS or CTS_TO_SELF (RTL_OPT_AWDL_PROT). */
static u8 rtl_awdl_agg_prot;
int rtl8814native_radio_awdl_agg(struct rtl_radio_awdl_agg *cfg)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	u32 rate = cfg->rate, num = cfg->max_agg ? cfg->max_agg : 31;
	u8 cur = padapter ? GET_HAL_DATA(padapter)->current_channel_bw : CHANNEL_WIDTH_20;
	if (rate && !((rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
	              (rate >= DESC_RATEVHTSS1MCS0 && rate <= DESC_RATEVHTSS2MCS9)))
		return -EINVAL;
	if (cfg->bw > RTL_RADIO_BW_80 || num > 31 || cfg->density > 7)
		return -EINVAL;
	__atomic_store_n(&rtl_awdl_agg_cfg,
	                 rate | cfg->bw << 8 | (cfg->sgi ? 1u : 0u) << 12 | num << 16 | cfg->density << 24,
	                 __ATOMIC_RELAXED);
	cfg->radio_bw = cur == CHANNEL_WIDTH_80 ? RTL_RADIO_BW_80
	                : cur == CHANNEL_WIDTH_40 ? RTL_RADIO_BW_40 : RTL_RADIO_BW_20;
	return 0;
}

int rtl8814native_radio_awdl_rate(uint32_t rate)
{
	static const u8 ofdm[8] = {
		IEEE80211_OFDM_RATE_6MB, IEEE80211_OFDM_RATE_9MB, IEEE80211_OFDM_RATE_12MB,
		IEEE80211_OFDM_RATE_18MB, IEEE80211_OFDM_RATE_24MB, IEEE80211_OFDM_RATE_36MB,
		IEEE80211_OFDM_RATE_48MB, IEEE80211_OFDM_RATE_54MB
	};
	u8 m;
	if (rate == 0)
		m = 0;
	else if (rate >= DESC_RATE6M && rate <= DESC_RATE54M)
		m = ofdm[rate - DESC_RATE6M];
	else if (rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS7)
		m = (u8)(MGN_MCS0 + (rate - DESC_RATEMCS0));
	else
		return -EINVAL;
	__atomic_store_n(&rtl_awdl_data_mrate, m, __ATOMIC_RELAXED);
	return 0;
}

/* From the C2H handler: one CCX TX report. Only frames that ask for one
 * (SPE_RPT) produce it, and only AWDL data at the ladder's rate does. */
void rtl8814native_awdl_tx_report(int lost, u8 retries, u8 rate)
{
	int k = lost ? 3 : retries == 0 ? 0 : retries == 1 ? 1 : 2;
	__atomic_fetch_add(&rtl_awdl_txstat[k], 1, __ATOMIC_RELAXED);
	__atomic_store_n(&rtl_awdl_txstat[4], rate, __ATOMIC_RELAXED);
	__atomic_fetch_add(&rtl_awdl_txstat[5], 1, __ATOMIC_RELAXED);
}

void rtl8814native_radio_awdl_txstat(struct rtl_radio_awdl_txstat *out)
{
	out->clean = __atomic_load_n(&rtl_awdl_txstat[0], __ATOMIC_RELAXED);
	out->retried = __atomic_load_n(&rtl_awdl_txstat[1], __ATOMIC_RELAXED);
	out->retried2 = __atomic_load_n(&rtl_awdl_txstat[2], __ATOMIC_RELAXED);
	out->lost = __atomic_load_n(&rtl_awdl_txstat[3], __ATOMIC_RELAXED);
	out->rate = __atomic_load_n(&rtl_awdl_txstat[4], __ATOMIC_RELAXED);
	out->reports = __atomic_load_n(&rtl_awdl_txstat[5], __ATOMIC_RELAXED);
}

int rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct mlme_ext_priv *pmlmeext;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	u8 *pframe;
	bool awdl, data;
	u32 agg = 0;

	if (!frame || len < WLAN_HDR_A3_LEN || len > MAX_XMIT_EXTBUF_SZ - TXDESC_OFFSET)
		return -EINVAL;
	/* Management frames, or AWDL data frames: data or QoS data, To/From DS 0
	 * (3-address header), A3 the AWDL BSSID. The MGNT descriptor does not look
	 * at the frame type, and the PMF hook leaves non-management frames alone. */
	awdl = _rtw_memcmp(frame + 16, rtl_awdl_bssid, ETH_ALEN) == _TRUE;
	data = GetFrameType(frame) == WIFI_DATA_TYPE;
	if (GetFrameType(frame) != WIFI_MGT_TYPE && (!data || !awdl || (frame[1] & 0x43)))
		return -EINVAL;
	if (!padapter || !padapter->bup || RTW_CANNOT_TX(padapter))
		return -ENODEV;
	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return -EBUSY;
	/* Unicast data for a peer, sent or waiting: a scan queued from now on
	 * returns to the parked channel (rtl8814native_awdl_scan_hold_decide) */
	if (data && awdl && !IS_MCAST(GetAddr1Ptr(frame)))
		rtl_awdl_data_us = rtl8814native_uptime_us();
	/* A scan without an association is on a foreign channel outside its
	 * returns to the parked one: AWDL frames wait for the return. */
	if (awdl && check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY) &&
	    !check_fwstate(&padapter->mlmepriv, _FW_LINKED) && !rtl8814native_awdl_backop)
		return -EBUSY;

	/* Data shares the management xmit frames with the sync frames and the
	 * infrastructure's own management TX: it leaves 16 of them alone. */
	if (data && padapter->xmitpriv.free_xframe_ext_cnt < RTL_AWDL_MGMT_RESERVE)
		return -ENOMEM;
	pmgntframe = alloc_mgtxmitframe(&padapter->xmitpriv);
	if (pmgntframe == NULL)
		return -ENOMEM;

	pmlmeext = &padapter->mlmeextpriv;
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	/* Never CCK on the AWDL BSSID: while associated on a 2.4 GHz BSS the
	 * management rate is CCK 1 Mb/s. Action frames at OFDM 6, data at 12. */
	if (awdl) {
		pattrib->rate = data ? IEEE80211_OFDM_RATE_12MB : IEEE80211_OFDM_RATE_6MB;
		pattrib->raid = rtw_get_mgntframe_raid(padapter, WIRELESS_11G);
	}

	_rtw_memset(pmgntframe->buf_addr, 0, TXDESC_OFFSET);
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	_rtw_memcpy(pframe, frame, len);
	/* QoS data of a Block Ack session of the layer's: its own sequence
	 * number, the AC queue of its TID, and the descriptor lets the MAC build
	 * A-MPDUs (rtl8814au_xmit.c). */
	if (data && awdl && (frame[0] & 0xF0) == 0x80 && !IS_MCAST(GetAddr1Ptr(pframe)) &&
	    len >= WLAN_HDR_A3_LEN + 2)
		agg = __atomic_load_n(&rtl_awdl_agg_cfg, __ATOMIC_RELAXED);
	if (agg & 0xFF) {
		u8 tid = frame[WLAN_HDR_A3_LEN] & 0x7;
		u8 bw = (agg >> 8) & 0xF;
		pattrib->hdrlen = WLAN_HDR_A3_LEN + 2;
		pattrib->qos_en = _TRUE;
		pattrib->priority = tid;
		pattrib->qsel = tid;
		pattrib->seqnum = GetSequence(frame);
		pattrib->ht_en = _TRUE;
		pattrib->bwmode = bw == RTL_RADIO_BW_80 ? CHANNEL_WIDTH_80
		                  : bw == RTL_RADIO_BW_40 ? CHANNEL_WIDTH_40 : CHANNEL_WIDTH_20;
		pattrib->sgi = (agg >> 12) & 1;
		pattrib->ampdu_en = _TRUE;
		pattrib->ampdu_spacing = (agg >> 24) & 0x7;
		pattrib->rtl_awdl_agg = agg & 0xFF;
		pattrib->rtl_awdl_agg_num = (agg >> 16) & 0x1F;
		pattrib->vcs_mode = rtl_awdl_agg_prot;
	} else {
		SetSeqNum(pframe, pmlmeext->mgnt_seq);
		pmlmeext->mgnt_seq++;
	}

	/* The descriptor sets HWSEQ_EN for non-QoS frames, so the hardware
	 * overwrites this number on air. Group-addressed frames go out once
	 * because of the descriptor's BMC bit (taken from A1); retry_ctrl only
	 * picks the unicast retry limit. */
	if (IS_MCAST(GetAddr1Ptr(pframe)))
		pattrib->retry_ctrl = _FALSE;
	/* The virtual peer has no radio, so it never sends the 802.11 ACK and every
	 * unicast frame would use all its retries. A real peer ACKs the first try:
	 * one try keeps the air time of a test near that of a real link. */
	if (data && rtl_awdl_sim_channel())
		pattrib->data_tries = 1;
	/* Unicast data at the layer's rate: the chip reports each frame and may
	 * step down on retries (rtl8814au_xmit.c). */
	if (data && awdl && !(agg & 0xFF) && !IS_MCAST(GetAddr1Ptr(pframe))) {
		u8 m = __atomic_load_n(&rtl_awdl_data_mrate, __ATOMIC_RELAXED);
		if (m) {
			pattrib->rate = m;
			pattrib->rtl_awdl_rpt = 1;
		}
	}
	pattrib->pktlen = len;
	pattrib->last_txcmdsz = len;

	dump_mgntframe(padapter, pmgntframe);
	/* The virtual peer hears what was submitted, from the caller's copy:
	 * the xmit frame belongs to the queue now. */
	if (awdl)
		rtl_awdl_sim_tap(frame, len, rtw_get_oper_ch(padapter));
	return 0;
}

int rtl8814native_awdl_sim_rx(const uint8_t *frame, uint32_t len, int8_t rssi)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	u8 ch;

	/* What validate_recv_frame hands the hook: no control frames, A3 the
	 * AWDL BSSID, the operating channel. */
	if (!frame || len < RTL_AWDL_SIM_FRAME_MIN || len > RTL_AWDL_SIM_FRAME_MAX ||
	    GetFrameType(frame) == WIFI_CTRL_TYPE ||
	    _rtw_memcmp(frame + 16, rtl_awdl_bssid, ETH_ALEN) != _TRUE)
		return -EINVAL;
	if (!padapter || !padapter->bup || RTW_CANNOT_RX(padapter) || !padapter->pnetdev)
		return -ENODEV;
	/* Off the sim channel the radio would not have heard it. A capture has no
	 * channel: its client injects one frame to check the RX tap. */
	ch = rtw_get_oper_ch(padapter);
	if (ch != rtl_awdl_sim_channel() && !rtl_awdl_sim_capturing())
		return -EAGAIN;
	if (!rtl8814native_awdl_rx(padapter->pnetdev, frame, len, rssi, ch))
		return -ENOENT;
	return 0;
}

/*
 * Port 1 for awdl0 (Docs/AWDL_RTL8814_RX_ACK.md §4, Docs/AWDL_RTL8814_RAW_TX_PORT1.md
 * §2): its MAC, the AWDL BSSID and the ad-hoc network type, so the MAC
 * acknowledges unicast frames addressed to awdl0, as the IBSS cell does for
 * owly. With CONFIG_CONCURRENT_MODE off nothing else writes these registers.
 * REG_BCN_CTRL_1 stays as it is (0x10, beacon function off). While the port
 * is on, rtw_hal_rcr_set_chk_bssid keeps RCR_CBSSID_DATA clear, as it does
 * for AP and mesh, so AWDL data frames reach validate_recv_frame. Register
 * writes go over USB: process context only.
 */

int rtl8814native_radio_awdl_port(const uint8_t *mac)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	u8 val8;
	int i;

	if (!padapter || !padapter->bup || RTW_CANNOT_RUN(padapter))
		return -ENODEV;

	if (mac) {
		for (i = 0; i < ETH_ALEN; i++) {
			rtw_write8(padapter, REG_MACID1 + i, mac[i]);
			rtw_write8(padapter, REG_BSSID1 + i, rtl_awdl_bssid[i]);
		}
		/* REG_CR bits 19:18, port 1 network type (rtw_hal_set_msr, HW_PORT1) */
		val8 = rtw_read8(padapter, MSR) & 0x03;
		rtw_write8(padapter, MSR, val8 | (_HW_STATE_ADHOC_ << 2));
		rtl8814native_awdl_port1 = 1;
	} else {
		val8 = rtw_read8(padapter, MSR) & 0x03;
		rtw_write8(padapter, MSR, val8 | (_HW_STATE_NOLINK_ << 2));
		rtl8814native_awdl_port1 = 0;
	}
	rtw_hal_rcr_set_chk_bssid(padapter, MLME_ACTION_NONE);
	return 0;
}

/*
 * Parks the radio on ch at 20 MHz while it is not associated, so awdl0 can
 * stay on a social channel without a network there. rtw_set_chbw_hdl runs
 * on the command thread, after any scan or join already queued, and sets
 * cur_channel, which survey_done_set_ch_bw returns to after later scans. A
 * join moves the radio to its BSS as usual.
 */
int rtl8814native_radio_set_channel(uint8_t ch)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	u8 bw, off = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if (!padapter || !padapter->bup || RTW_CANNOT_RUN(padapter))
		return -ENODEV;
	if (rtw_chset_search_ch(adapter_to_chset(padapter), ch) < 0)
		return -EINVAL;
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED | _FW_UNDER_LINKING | _FW_UNDER_SURVEY))
		return -EBUSY;
	/* AWDL frames go at OFDM 6 Mb/s on either band, never CCK on 5 GHz */
	update_mgnt_tx_rate(padapter, IEEE80211_OFDM_RATE_6MB);
	/* 20 MHz unless RTL_OPT_AWDL_BW asks for 40 or 80 on 5 GHz, with the
	 * channel as the primary 20 (149: LOWER, centre 155 at 80). The frames
	 * sent here still go at 20 MHz legacy on the primary: the management
	 * descriptor never sets DATA_BW. */
	bw = ch > 14 ? rtl_awdl_park_bw : CHANNEL_WIDTH_20;
	if (bw != CHANNEL_WIDTH_20 && !rtw_get_offset_by_chbw(ch, bw, &off)) {
		bw = CHANNEL_WIDTH_20;
		off = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}
	if (rtw_set_chbw_cmd(padapter, ch, bw, off, RTW_CMDF_CHBW_IDLE_ONLY) != _SUCCESS)
		return -ENOMEM;
	return 0;
}
/*
 * RTL_OPT_AWDL_PROT: 0 none, 1 RTS/CTS, 2 CTS-to-self before the AWDL QoS data
 * of a session. A diagnostic: both are legacy frames a 20 MHz monitor hears
 * when it cannot decode the VHT frames they protect, and their Duration tells
 * how long the frame behind them is.
 */
int rtl8814native_awdl_set_prot(uint32_t mode, uint32_t *prev)
{
	if (mode > CTS_TO_SELF)
		return -EINVAL;
	if (prev)
		*prev = rtl_awdl_agg_prot;
	rtl_awdl_agg_prot = (u8)mode;
	return 0;
}

/*
 * RTL_OPT_AWDL_BW: the width above. A test of whether a peer sends to awdl0
 * at 40 or 80 MHz (Docs/AWDL_RENDIMIENTO_1.md §6). While the radio is idle it
 * is parked again at once on the channel it sits on; a linked radio keeps its
 * BSS width and takes the new one at the next park.
 */
int rtl8814native_awdl_set_park_bw(uint32_t mhz, uint32_t *prev)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	u8 bw = mhz == 80 ? CHANNEL_WIDTH_80 : mhz == 40 ? CHANNEL_WIDTH_40 : CHANNEL_WIDTH_20;
	u8 ch;
	if (mhz != 20 && mhz != 40 && mhz != 80)
		return -EINVAL;
	if (prev)
		*prev = rtl_awdl_park_bw == CHANNEL_WIDTH_80 ? 80 : rtl_awdl_park_bw == CHANNEL_WIDTH_40 ? 40 : 20;
	rtl_awdl_park_bw = bw;
	if (!padapter || !padapter->bup)
		return 0;
	ch = rtw_get_oper_ch(padapter);
	if (ch > 14 && !check_fwstate(&padapter->mlmepriv, _FW_LINKED | _FW_UNDER_LINKING | _FW_UNDER_SURVEY))
		rtl8814native_radio_set_channel(ch);
	return 0;
}

/*
 * Lets the PM null leave on the BSS channel before the switch: its bulk OUT
 * URB (and any data URB before it) completes, so the frame is in the chip,
 * then the management queue drains (REG_TXPKT_EMPTY bit 8). Up to 2 ms, the
 * fixed wait this replaces, which made the way out about 2 ms longer than
 * the way back (Docs/AWDL_CAP29_DOBLE_BANDA_RIESGOS.md §4).
 */
#define RTL_AWDL_NULL_WAIT_US 2000


static void rtl_awdl_wait_null_out(_adapter *padapter)
{
	u64 end = rtl8814native_uptime_us() + RTL_AWDL_NULL_WAIT_US;
	u8 empty = _FALSE;

	while (rtl8814native_tx_urbs_inflight() > 0) {
		if (rtl8814native_uptime_us() >= end)
			return;
		rtw_usleep_os(50);
	}
	for (;;) {
		rtw_hal_get_hwreg(padapter, HW_VAR_CHK_MGQ_CPU_EMPTY, &empty);
		if (empty || RTW_CANNOT_RUN(padapter) || rtl8814native_uptime_us() >= end)
			return;
		rtw_usleep_os(50);
	}
}

/*
 * phydm recalibrates I/Q when the chip's temperature moves (thermal tracking in
 * the watchdog, command thread). The calibration takes hundreds of ms in RF
 * loopback and ends restoring the registers it saved. It already waits for a
 * scan to end; an AWDL slot off the BSS channel is a channel change from the
 * layer's thread it knew nothing about. A hop in the middle is the suspected,
 * not proven, cause of the radio receiving nothing until the next HAL init
 * (2026-11-11, once, 38 s after a join with the layer hopping 157/149).
 * The calibration runs with offch_mutex held, so no hop starts inside it, and
 * not at all while the radio is away on a slot: the thermal value it compares
 * with stays, and the next watchdog tries again.
 */
static u32 rtl_awdl_cal_runs, rtl_awdl_cal_deferred;

int rtl8814native_awdl_cal_enter(void *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl((_adapter *)adapter);

	_enter_critical_mutex(&rfctl->offch_mutex, NULL);
	if (rfctl->offch_state == OFFCHS_AWDL) {
		_exit_critical_mutex(&rfctl->offch_mutex, NULL);
		rtl_awdl_cal_deferred++;
		return 0;
	}
	rtl_awdl_cal_runs++;
	return 1;
}

void rtl8814native_awdl_cal_exit(void *adapter)
{
	_exit_critical_mutex(&adapter_to_rfctl((_adapter *)adapter)->offch_mutex, NULL);
}

/* RTL_OPT_AWDL_CAL: thermal calibrations run in the low 16 bits, put off in the high 16 */
int rtl8814native_awdl_cal_stats(uint32_t *out)
{
	if (out)
		*out = (rtl_awdl_cal_runs & 0xFFFF) | (rtl_awdl_cal_deferred << 16);
	return 0;
}

/*
 * While associated, takes the radio to ch for an AWDL slot, or back to the
 * BSS channel with ch 0 (Docs/AWDL_MCC_TIEMPO_COMPARTIDO.md §7.1): a null
 * frame with PM set tells the AP to hold our traffic, the AP's MAC ID sleeps
 * and offch_state blocks the data queues until the return, which sends PM
 * clear. When ch shares the BSS's 40 or 80 MHz block only the primary
 * sub-channel moves, at the BSS width and without the TX power reload (§4.1);
 * otherwise it goes at 20 MHz. Runs synchronously on the caller's thread:
 * the switch is USB register traffic, a few ms. cur_* stay on the BSS, so
 * rtl_radio_status keeps reporting it. -EBUSY unless linked with no join
 * running and either no scan or a scan in its back-op, which then holds the
 * next scan channel until the return.
 */
int rtl8814native_radio_awdl_offch(uint8_t ch)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct mlme_ext_priv *pmlmeext;
	struct rf_ctl_t *rfctl;
	HAL_DATA_TYPE *hal;
	u8 home, bw, offset, same;
	int rc = 0;

	if (!padapter || !padapter->bup || RTW_CANNOT_RUN(padapter))
		return -ENODEV;
	if (ch && rtw_chset_search_ch(adapter_to_chset(padapter), ch) < 0)
		return -EINVAL;
	pmlmeext = &padapter->mlmeextpriv;
	rfctl = adapter_to_rfctl(padapter);
	hal = GET_HAL_DATA(padapter);
	home = pmlmeext->cur_channel;

	_enter_critical_mutex(&rfctl->offch_mutex, NULL);
	if (!ch || ch == home) {
		/* back to the BSS; a scan that ran meanwhile already brought it back */
		if (rfctl->offch_state == OFFCHS_AWDL) {
			/* the HAL reloads the TX power anyway if the channel index moves */
			hal->awdl_keep_txpwr = 1;
			set_channel_bwmode(padapter, home, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
			hal->awdl_keep_txpwr = 0;
			rtw_hal_macid_wakeup_all_used(padapter);
			issue_nulldata(padapter, NULL, 0, 0, 0);
			rfctl->offch_state = OFFCHS_NONE;
			rtw_mi_os_xmit_schedule(padapter);
		}
		goto out;
	}
	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED) ||
	    check_fwstate(&padapter->mlmepriv, _FW_UNDER_LINKING) ||
	    (check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY) && !rtl8814native_awdl_backop) ||
	    (rfctl->offch_state != OFFCHS_NONE && rfctl->offch_state != OFFCHS_AWDL)) {
		rc = -EBUSY;
		goto out;
	}
	if (rfctl->offch_state == OFFCHS_NONE) {
		rfctl->offch_state = OFFCHS_AWDL;
		rtw_hal_macid_sleep_all_used(padapter);
		/* The management MAC ID stays awake: AWDL session frames carry it in
		 * an AC queue, and asleep the chip held them until the return, about
		 * 300 frames a visit whatever its length (2026-11-11: 1.5 MB/s, 16 MB/s
		 * with the bit cleared by hand). The station's data is the AP's MAC ID. */
		rtw_hal_macid_wakeup(padapter, RTW_DEFAULT_MGMT_MACID);
		issue_nulldata(padapter, NULL, 1, 0, 0);
		rtl_awdl_wait_null_out(padapter);
	}
	bw = pmlmeext->cur_bwmode;
	offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	same = (bw == CHANNEL_WIDTH_40 || bw == CHANNEL_WIDTH_80) &&
	       rtw_get_offset_by_chbw(ch, bw, &offset) &&
	       rtw_get_center_ch(ch, bw, offset) ==
	           rtw_get_center_ch(home, bw, pmlmeext->cur_ch_offset);
	if (same) {
		hal->awdl_keep_txpwr = 1;
		set_channel_bwmode(padapter, ch, offset, bw);
		hal->awdl_keep_txpwr = 0;
	} else {
		set_channel_bwmode(padapter, ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}
out:
	_exit_critical_mutex(&rfctl->offch_mutex, NULL);
	return rc;
}
