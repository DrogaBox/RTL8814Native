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
#include "RTLScanLog.h"

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
u8 rtl8814native_awdl_scan_cnt_max(u8 def);
u16 rtl8814native_awdl_scan_backop_ms(u16 def);
void rtl8814native_awdl_scan_backop_start(void);
u8 rtl8814native_awdl_scan_backop_over(u32 ms, u16 def);
void rtl8814native_slog(u8 ev, u8 a, u16 b);
u8 rtl8814native_awdl_scan_ps_annc(_adapter *padapter, u8 ps);
void rtl8814native_get_scan_log(struct rtl_slog *out);
int rtl8814native_radio_awdl_rate(uint32_t rate);
void rtl8814native_radio_awdl_txstat(struct rtl_radio_awdl_txstat *out);
int rtl8814native_radio_awdl_agg(struct rtl_radio_awdl_agg *cfg);
void rtl8814native_awdl_ccx_report(const u8 *pl, u8 len);
void rtl8814native_awdl_ccx_report_ext(const u8 *pl, u8 len);
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

/* The scan step log (RTLScanLog.h). */
static struct rtl_slog rtl_slog;

/* when the last scan ended (SL_STATE SCAN_DISABLE), 0 = none yet */
static u64 rtl_slog_scan_end_us;

/* Set by site_survey around the channel set of a scan channel that sends no
 * probe request (a passive scan, RTL_RADIO_SCAN_PASSIVE), to that channel:
 * the HAL then skips the TX power load for it (phy_SwChnlAndSetBwMode8814A).
 * The channel, not a flag: an AWDL hop waiting on setch_mutex meanwhile
 * sets another channel and still loads its power. 0 = none. */
u8 rtl8814native_scan_no_txpwr;

/* RTL_OPT_AWDL_FULLPWR (test_uc awdlfullpwr): what an AWDL visit to another
 * 5 GHz block loads of the TX power table (PHY_SetTxPowerLevel8814): 0 = of
 * OFDM 6/12/24 and the data rate on the way out and all OFDM on the return,
 * 1 = the whole table both ways, 2 = all OFDM both ways. */
u8 rtl8814native_awdl_fullpwr;

/* RTL_OPT_AWDL_RETRY (test_uc awdlretry): hardware retry limit of unicast
 * AWDL frames, data and UMIs alike, 1 to 63 (6-bit descriptor field). The
 * vendor gives frames with retry_ctrl 6. WirDrop (Realtek USB, commit
 * cd2e9b4) measured TX to an iPhone going from 23.2 to 35.8 MB/s and TCP
 * retransmissions from 70 to 1 when it raised the limit from 8 to 32. A
 * UMI's CCX report carries the retries used, so against the virtual peer,
 * which never ACKs, every UMI shows this limit. */
u8 rtl8814native_awdl_retry = 16;

/*
 * RTL_OPT_AWDL_FWCHSW (test_uc awdlfwchsw): 1 = an AWDL hop to another 5 GHz
 * block at the same band and width has the firmware do the channel part with
 * H2C 0x1C CHNL_SWITCH_OPER_OFFLOAD (fc_area, RF 0x18 on the four paths,
 * AGC table, TRXPTCL, DATA_SC and the width BB registers, at register speed
 * in the MCU: Docs/AWDL_FW_8814A_CHSW_OFFLOAD.md). The HAL waits for its
 * C2H 0x10 and does spur and TX power itself (phy_SwChnlAndSetBwMode8814A).
 * Three timeouts in a row turn it off. Off by default.
 */
u8 rtl8814native_awdl_fwchsw;
static volatile u32 rtl_fwchsw_seq;     /* C2H 0x10 seen */
static volatile u8 rtl_fwchsw_ch;       /* channel byte of the last one */
static u32 rtl_fwchsw_ok, rtl_fwchsw_timeouts, rtl_fwchsw_timeouts_row;

/* C2H 0x10 from c2h_handler, in the USB RX path: payload[0] is the channel */
void rtl8814native_fw_chsw_done(u8 ch)
{
	rtl_fwchsw_ch = ch;
	__atomic_add_fetch(&rtl_fwchsw_seq, 1, __ATOMIC_RELEASE);
}

/*
 * Sends H2C 0x1C for center at bw (CHANNEL_WIDTH_*) with the 40 and 80 MHz
 * primary offsets (HAL_PRIME_CHNL_OFFSET_*, as the HAL's nCur*MhzPrimeSC) and
 * waits up to 20 ms for its C2H. The emitter of the tree (TDLS only) passes the
 * primary and one SC; the firmware wants the center in byte 0 and both SCs.
 * 0 when the C2H came, *wait_us = from the H2C written to the C2H seen.
 */
int rtl8814native_fw_chsw(void *adapter, u8 center, u8 bw, u8 sc40, u8 sc80, u8 rfe, u32 *wait_us)
{
	u8 buf[4];
	u32 seq = __atomic_load_n(&rtl_fwchsw_seq, __ATOMIC_ACQUIRE);
	u64 t0, now;

	buf[0] = center;
	buf[1] = (bw & 0x3) | ((sc40 & 0x7) << 2) | ((sc80 & 0x7) << 5);
	buf[2] = rfe & 0xF;
	buf[3] = 0;
	if (rtw_hal_fill_h2c_cmd((_adapter *)adapter, H2C_CHNL_SWITCH_OPER_OFFLOAD, sizeof(buf), buf) !=
	    _SUCCESS)
		return -EIO;
	t0 = rtl8814native_uptime_us();
	for (;;) {
		now = rtl8814native_uptime_us();
		if (__atomic_load_n(&rtl_fwchsw_seq, __ATOMIC_ACQUIRE) != seq && rtl_fwchsw_ch == center) {
			if (wait_us)
				*wait_us = (u32)(now - t0);
			rtl_fwchsw_ok++;
			rtl_fwchsw_timeouts_row = 0;
			return 0;
		}
		if (now - t0 >= 20000)
			break;
		rtw_udelay_os(20);
	}
	rtl_fwchsw_timeouts++;
	if (++rtl_fwchsw_timeouts_row >= 3)
		rtl8814native_awdl_fwchsw = 0;
	return -ETIMEDOUT;
}

/* RTL_OPT_AWDL_FWCHSW_STATS: switches done by the firmware (low 16 bits), timeouts (high 16) */
int rtl8814native_fw_chsw_stats(uint32_t *out)
{
	if (out)
		*out = (rtl_fwchsw_ok & 0xFFFF) | (rtl_fwchsw_timeouts << 16);
	return 0;
}

void rtl8814native_slog(u8 ev, u8 a, u16 b)
{
	u32 i = __atomic_fetch_add(&rtl_slog.next, 1, __ATOMIC_RELAXED) % RTL_SLOG_N;
	u64 now = rtl8814native_uptime_us();

	if (ev == SL_STATE && a == SCAN_DISABLE)
		rtl_slog_scan_end_us = now;
	rtl_slog.e[i].t_us = (u32)now;
	rtl_slog.e[i].ev = ev;
	rtl_slog.e[i].a = a;
	rtl_slog.e[i].b = b;
}

void rtl8814native_get_scan_log(struct rtl_slog *out)
{
	_rtw_memcpy(out, &rtl_slog, sizeof(*out));
}

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
static void rtl_awdl_scan_step_decide(_adapter *padapter);

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
	rtl_awdl_scan_step_decide(padapter);
}

u8 rtl8814native_awdl_scan_hold(void)
{
	return rtl_awdl_scan_hold_now;
}

/*
 * A linked scan returns to its BSS every RTW_SCAN_NUM_OF_CH (3) channels, so
 * locationd's 3-channel 2.4 GHz scan, about once a minute and let through
 * even in AWDL real-time mode once that is a minute old
 * (IO80211AWDLPeerManager::deviceAllows2GOnlyScan), took the radio away from
 * AWDL for 661 ms in one piece (2026-11-17, llwchan at 1000/s). With AWDL data
 * in the last rtl_awdl_scan_hold_s seconds such a scan returns after every
 * channel, for less than the 400 ms of a group's return: the hole is one
 * channel's dwell. Decided with the hold, once per scan.
 *
 * A fixed 200 ms return did not always hold an AWDL slot: the layer serves
 * its slots on its own schedule, and with none in a return the hole spanned
 * two channels and the return, about 430 ms (2026-11-17, same test). So a
 * return ended once an AWDL slot had started in it and at least
 * 100 ms had passed, the slot itself then finishing
 * before the scan goes on (sitesurvey_close_awdl_backop); without a slot it
 * ends at RTL_AWDL_SCAN_STEP_MAX_MS.
 *
 * A visit that began in the tail of a slot counted as well: 32 ms on AWDL,
 * the return over at 100 ms, and the hole spanned two channels again, 596 ms
 * (2026-11-17, driver 1404B22B). So a visit counts once it is back home and
 * lasted at least RTL_AWDL_SCAN_STEP_VISIT_US.
 *
 * With visits counted that way the old 100 ms floor on a return only held
 * the radio home: a return that came back 54 ms before the end of an AWDL
 * slot counted at the slot's end, 59 ms into the return, and the scan then
 * waited for its next 50 ms poll on the BSS channel, so two AWDL stalls of
 * 216-217 ms came 46 ms apart (2026-11-17, driver 3F4D71D7). A counted
 * visit now ends the return at once.
 */
#define RTL_AWDL_SCAN_STEP_MAX_MS 600
#define RTL_AWDL_SCAN_STEP_VISIT_US 50000
static u8 rtl_awdl_scan_step_now;
/* AWDL visits of a scan's return long enough to count, and the count when
 * this one began; counted under offch_mutex in rtl8814native_radio_awdl_offch,
 * which notes when each visit began */
static u32 rtl_awdl_backop_slots;
static u32 rtl_awdl_backop_slots_at;
static u64 rtl_awdl_visit_us;

static void rtl_awdl_scan_step_decide(_adapter *padapter)
{
	u64 data = rtl_awdl_data_us;
	u64 now = rtl8814native_uptime_us();
	u64 win = (u64)rtl_awdl_scan_hold_s * 1000000;

	rtl_awdl_scan_step_now = rtl_awdl_scan_hold_s && data && rtl8814native_awdl_port1 &&
	                         check_fwstate(&padapter->mlmepriv, _FW_LINKED) &&
	                         now - data < win;
	rtl8814native_slog(SL_STEP, rtl_awdl_scan_step_now, rtl_awdl_scan_hold_now);
}

u8 rtl8814native_awdl_scan_cnt_max(u8 def)
{
	return rtl_awdl_scan_step_now ? 1 : def;
}

/* the longest a return lasts: the scan timeout counts it */
u16 rtl8814native_awdl_scan_backop_ms(u16 def)
{
	return rtl_awdl_scan_step_now ? RTL_AWDL_SCAN_STEP_MAX_MS : def;
}

/* SCAN_BACKING_OP: a return begins */
void rtl8814native_awdl_scan_backop_start(void)
{
	rtl_awdl_backop_slots_at = __atomic_load_n(&rtl_awdl_backop_slots, __ATOMIC_RELAXED);
}

/* SCAN_BACK_OP: whether the return has lasted long enough, ms into it */
u8 rtl8814native_awdl_scan_backop_over(u32 ms, u16 def)
{
	if (!rtl_awdl_scan_step_now)
		return ms >= def;
	if (ms >= RTL_AWDL_SCAN_STEP_MAX_MS)
		return 1;
	return __atomic_load_n(&rtl_awdl_backop_slots, __ATOMIC_RELAXED) != rtl_awdl_backop_slots_at;
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
/* The UMIs' reports (RTL_RADIO_FN_AWDL_AFSTAT): the receiver of each frame
 * waiting, by report number / 4, and the results. */
struct rtl_awdl_umi_wait {
	u8 mac[ETH_ALEN];
	u8 busy;
};
static struct rtl_awdl_umi_wait rtl_awdl_umi_wait[64];
static u32 rtl_awdl_umi_sn;
static struct rtl_radio_awdl_afstat rtl_awdl_af;
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

/*
 * The way out of an AWDL visit to another 5 GHz block writes, of OFDM, only
 * the TX power of 6/12/24 and of the data rate of that moment
 * (PHY_SetTxPowerLevel8814). A rate set during the visit gets its entry
 * here, for the channel the radio is on: four writes, one a path.
 */
static void rtl_awdl_rate_txpwr(u8 m)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct mlme_ext_priv *ext;
	struct rf_ctl_t *rfctl;
	HAL_DATA_TYPE *hal;
	u8 path;

	if (!IS_OFDM_RATE(m) || m == MGN_6M || m == MGN_12M || m == MGN_24M ||
	    rtl8814native_awdl_fullpwr || !padapter || !padapter->bup || RTW_CANNOT_RUN(padapter))
		return;
	ext = &padapter->mlmeextpriv;
	rfctl = adapter_to_rfctl(padapter);
	hal = GET_HAL_DATA(padapter);
	_enter_critical_mutex(&rfctl->offch_mutex, NULL);
	if (rfctl->offch_state == OFFCHS_AWDL && hal->current_channel > 14 &&
	    hal->current_channel != rtw_get_center_ch(ext->cur_channel, ext->cur_bwmode, ext->cur_ch_offset))
		for (path = RF_PATH_A; path < hal->NumTotalRFPath && path <= RF_PATH_D; path++)
			PHY_SetTxPowerIndexByRateArray(padapter, (enum rf_path)path, hal->current_channel_bw,
			                               hal->current_channel, &m, 1);
	_exit_critical_mutex(&rfctl->offch_mutex, NULL);
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
	if (__atomic_exchange_n(&rtl_awdl_data_mrate, m, __ATOMIC_RELAXED) != m)
		rtl_awdl_rate_txpwr(m);
	return 0;
}

u8 rtl8814native_awdl_data_rate(void)
{
	return __atomic_load_n(&rtl_awdl_data_mrate, __ATOMIC_RELAXED);
}

/* One CCX TX report, of either format. Only frames that ask for one
 * (SPE_RPT) produce it: AWDL data at the ladder's rate, which carries no
 * report number, and the UMIs, which do (rtl_awdl_umi_tag). rpt/n: the
 * report's bytes, kept in rtl_awdl_af.raw; sn: the report number (SW_DEFINE
 * bits 7:2), 0 for data. data = 0: a data report that must not reach the
 * rate ladder's counters. */
static void rtl_awdl_ccx_note(const u8 *rpt, u8 n, int lost, u8 retries, u8 rate, u8 sn, int data)
{
	int k;
	u32 h;

	for (k = 0; k < 8; k++)
		__atomic_store_n(&rtl_awdl_af.raw[k], k < n ? rpt[k] : 0, __ATOMIC_RELAXED);
	if (sn) {
		struct rtl_awdl_umi_wait *w = &rtl_awdl_umi_wait[sn >> 2];
		if (!__atomic_exchange_n(&w->busy, 0, __ATOMIC_ACQUIRE)) {
			__atomic_fetch_add(&rtl_awdl_af.unmatched, 1, __ATOMIC_RELAXED);
			return;
		}
		h = __atomic_load_n(&rtl_awdl_af.head, __ATOMIC_RELAXED);
		_rtw_memcpy(rtl_awdl_af.e[h % RTL_RADIO_AFSTAT_RING].mac, w->mac, ETH_ALEN);
		rtl_awdl_af.e[h % RTL_RADIO_AFSTAT_RING].status = lost ? 1 : 0;
		rtl_awdl_af.e[h % RTL_RADIO_AFSTAT_RING].retries = retries;
		__atomic_store_n(&rtl_awdl_af.head, h + 1, __ATOMIC_RELEASE);
		return;
	}
	if (!data)
		return;
	k = lost ? 3 : retries == 0 ? 0 : retries == 1 ? 1 : 2;
	__atomic_fetch_add(&rtl_awdl_txstat[k], 1, __ATOMIC_RELAXED);
	__atomic_store_n(&rtl_awdl_txstat[4], rate, __ATOMIC_RELAXED);
	__atomic_fetch_add(&rtl_awdl_txstat[5], 1, __ATOMIC_RELAXED);
}

/* From the C2H handler: a report as C2H 0x03 (C2H_CCX_TX_RPT, rtw88's V0
 * layout). Payload byte 0: bit 6 lifetime over, bit 7 retry over; byte 2
 * bits 5:0 data retries; byte 5 final rate; byte 6 bits 7:2 the frame's
 * SW_DEFINE (GET_CCX_REPORT_SEQNUM_V0). The 8814A firmware does not send it
 * (rtl8814native_awdl_ccx_report_ext); kept for a firmware that does. */
void rtl8814native_awdl_ccx_report(const u8 *pl, u8 len)
{
	if (!pl || len < 6)
		return;
	rtl_awdl_ccx_note(pl, len, (pl[0] & 0xC0) != 0, pl[2] & 0x3F, pl[5],
	                  len >= 7 ? pl[6] & 0xFC : 0, 1);
}

/*
 * From the C2H handler: a report as the 8814A firmware sends it, C2H 0xFF
 * (C2H_EXTEND) sub-id 0x0F (rtw88's C2H_CCX_RPT). pl: the payload from the
 * sub-id on, [0] 0x0F, [1] 8 (the length), [2..9] one 8-byte entry of the
 * MAC's TX report buffer, copied as it is: the report drain of firmware
 * v33.6 (IRAM 0x800080A0) takes entries whose byte 0 bits 7:6 are 1 and
 * sends both words untouched (0x80008040), without reading any field. Of
 * the entry, r = pl + 2:
 *   r[0] bits 7:6  report type, 1 = CCX (firmware, proven)
 *   r[6]           SW_DEFINE low byte: bits 7:2 the report number, bit 0 the
 *                  descriptor's fixed rate (rtw88 GET_CCX_REPORT_SEQNUM_V1;
 *                  measured: 0x01 data, 0x09 the UMI tagged 8)
 *   r[7] bits 7:6  TX state, 0 = acknowledged (rtw88
 *                  GET_CCX_REPORT_STATUS_V1, halmac TX_STATE; measured 1 for
 *                  every frame to the virtual peer, which never ACKs)
 *   r[3] bits 6:0  a rate, the descriptor's (0x06 data at 12 Mb/s, 0x04 a
 *                  UMI at 6): halmac calls it the initial rate, so not the
 *                  final one after fallback (unproven)
 *   r[0] bits 5:0  measured 1 on data sent with retry limit 1 and 16 on
 *                  UMIs with limit 16, all unacknowledged: the retries used
 *                  or the limit itself, the two not told apart yet
 * The retry count is therefore not known. The UMI ring takes r[0] bits 5:0
 * as its retries, which the layer only logs; data reports stay out of the
 * rate ladder's counters (rtl_radio_awdl_txstat), whose clean / retried /
 * retried2 split is a retry count: the layer keeps rating by the peers'
 * signal, as it does while no report comes.
 */
void rtl8814native_awdl_ccx_report_ext(const u8 *pl, u8 len)
{
	const u8 *r;

	if (!pl || len < 10 || pl[1] < 8)
		return;
	r = pl + 2;
	rtl_awdl_ccx_note(r, 8, (r[7] & 0xC0) != 0, r[0] & 0x3F, r[3] & 0x7F, r[6] & 0xFC, 0);
}

/* A unicast AWDL action frame (a UMI) about to go out: a report number,
 * 4..252 in steps of 4, and the receiver kept under it until the report. */
static u8 rtl_awdl_umi_tag(const u8 *frame)
{
	u8 sn = (u8)(__atomic_add_fetch(&rtl_awdl_umi_sn, 1, __ATOMIC_RELAXED) % 63 + 1) << 2;
	struct rtl_awdl_umi_wait *w = &rtl_awdl_umi_wait[sn >> 2];

	_rtw_memcpy(w->mac, GetAddr1Ptr(frame), ETH_ALEN);
	__atomic_store_n(&w->busy, 1, __ATOMIC_RELEASE);
	__atomic_fetch_add(&rtl_awdl_af.sent, 1, __ATOMIC_RELAXED);
	return sn;
}

void rtl8814native_radio_awdl_afstat(struct rtl_radio_awdl_afstat *out)
{
	u32 head = __atomic_load_n(&rtl_awdl_af.head, __ATOMIC_ACQUIRE);

	_rtw_memcpy(out, &rtl_awdl_af, sizeof(*out));
	out->head = head;
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
	else if (awdl)
		pattrib->data_tries = __atomic_load_n(&rtl8814native_awdl_retry, __ATOMIC_RELAXED);
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
	/* A UMI (unicast AWDL action frame): its report tells the layer whether
	 * the peer acknowledged it, for the family's event 69. */
	if (awdl && !data && !IS_MCAST(GetAddr1Ptr(pframe)) && len >= WLAN_HDR_A3_LEN + 5 &&
	    pframe[0] == 0xD0 && pframe[24] == 0x7F && pframe[25] == 0x00 && pframe[26] == 0x17 &&
	    pframe[27] == 0xF2 && pframe[28] == 0x08)
		pattrib->rtl_awdl_rpt = rtl_awdl_umi_tag(pframe);
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
 * The scan's power-save announcements while it goes one channel per return
 * (rtl_awdl_scan_step_now). Without CONFIG_XMIT_ACK the ack wait of
 * issue_nulldata is a fixed 50 ms sleep, and the scan waits 50 ms more after
 * the PM null before it leaves (SCAN_PS_ANNC_WAIT): with the PM-clear null of
 * the return, 150 of the about 395 ms each channel kept AWDL off the air, the
 * dwell being 100 (2026-11-17, test_uc scanlog, llwchan at 1000/s). In a step
 * the null goes as the AWDL switch sends it: no ack wait, and with PM set the
 * scan leaves once it is out of the chip, up to 2 ms later. Returns false
 * outside a step, where rtw_ps_annc runs as before; true once the null went,
 * or with no AP to tell.
 */
u8 rtl8814native_awdl_scan_ps_annc(_adapter *padapter, u8 ps)
{
	if (!rtl_awdl_scan_step_now)
		return 0;
	if (MLME_IS_STA(padapter) && is_client_associated_to_ap(padapter) == _TRUE) {
		issue_nulldata(padapter, NULL, ps, 0, 0);
		if (ps)
			rtl_awdl_wait_null_out(padapter);
	}
	return 1;
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
/* the hop itself, with offch_mutex held */
static int rtl_awdl_offch_locked(_adapter *padapter, u8 ch)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	HAL_DATA_TYPE *hal = GET_HAL_DATA(padapter);
	u8 home = pmlmeext->cur_channel, bw, offset, same;
	int rc = 0;

	if (!ch || ch == home) {
		/* back to the BSS; a scan that ran meanwhile already brought it back */
		if (rfctl->offch_state == OFFCHS_AWDL) {
			if (rtl8814native_awdl_backop &&
			    rtl8814native_uptime_us() - rtl_awdl_visit_us >= RTL_AWDL_SCAN_STEP_VISIT_US) {
				__atomic_store_n(&rtl_awdl_backop_slots, rtl_awdl_backop_slots + 1,
				                 __ATOMIC_RELAXED);
				/* The return may end now: run the scan's handler at once
				 * instead of at its next 50 ms poll, which left the radio
				 * home 23-30 ms before the next channel (2026-11-17, driver
				 * 83DAC0F6). */
				set_survey_timer(pmlmeext, 1);
			}
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
		rtl_awdl_visit_us = rtl8814native_uptime_us();
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
	} else if (ch > 14 && home > 14 && bw != CHANNEL_WIDTH_20 &&
	           rtw_get_offset_by_chbw(ch, bw, &offset) &&
	           rtw_chset_is_chbw_valid(adapter_to_chset(padapter), ch, bw, offset)) {
		/* Another 5 GHz block at the BSS width (BSS on 52/80 MHz, AWDL on
		 * 149): band and width stay, so the HAL keeps its width-only
		 * registers (db67bb2) and only the TX power follows the new index.
		 * At 20 MHz every hop and return reprogrammed the width, 6.2-6.8 ms
		 * each way (2026-11-19, driver F9ED9F6A). AWDL frames keep their
		 * own width (rtl_awdl_agg_cfg) on the primary, as inside the BSS's
		 * block. */
		set_channel_bwmode(padapter, ch, offset, bw);
	} else {
		set_channel_bwmode(padapter, ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}
out:
	if (check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY) ||
	    (rtl_slog_scan_end_us && rtl8814native_uptime_us() - rtl_slog_scan_end_us < 1000000))
		rtl8814native_slog(SL_OFFCH, ch, (u16)rc);
	return rc;
}

int rtl8814native_radio_awdl_offch(uint8_t ch)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct rf_ctl_t *rfctl;
	int rc;

	if (!padapter || !padapter->bup || RTW_CANNOT_RUN(padapter))
		return -ENODEV;
	if (ch && rtw_chset_search_ch(adapter_to_chset(padapter), ch) < 0)
		return -EINVAL;
	rfctl = adapter_to_rfctl(padapter);
	_enter_critical_mutex(&rfctl->offch_mutex, NULL);
	rc = rtl_awdl_offch_locked(padapter, ch);
	_exit_critical_mutex(&rfctl->offch_mutex, NULL);
	return rc;
}

/*
 * RTL_OPT_AWDL_TESTHOP (test_uc testhop): one AWDL-style visit to ch and back
 * with offch_mutex held all the way, so the layer's own hops wait. For the hop
 * paths the BSS and the AWDL channel in use do not reach (another 5 GHz block
 * with the BSS in 149's): timings in test_uc scanlog, registers with peek/rf
 * during the dwell. No AWDL frames go out there. *out = the way out in 10 us
 * units (low 16 bits) and the return (high 16).
 */
int rtl8814native_awdl_test_hop(u8 ch, u32 dwell_ms, u32 *out)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct rf_ctl_t *rfctl;
	u64 t0, t1, t2, t3;
	int rc;

	if (!padapter || !padapter->bup || RTW_CANNOT_RUN(padapter))
		return -ENODEV;
	if (!ch || dwell_ms > 2000 || rtw_chset_search_ch(adapter_to_chset(padapter), ch) < 0 ||
	    ch == padapter->mlmeextpriv.cur_channel)
		return -EINVAL;
	rfctl = adapter_to_rfctl(padapter);
	_enter_critical_mutex(&rfctl->offch_mutex, NULL);
	if (rfctl->offch_state != OFFCHS_NONE) {
		_exit_critical_mutex(&rfctl->offch_mutex, NULL);
		return -EBUSY;
	}
	t0 = rtl8814native_uptime_us();
	rc = rtl_awdl_offch_locked(padapter, ch);
	t1 = rtl8814native_uptime_us();
	if (!rc) {
		if (dwell_ms)
			rtw_msleep_os(dwell_ms);
		t2 = rtl8814native_uptime_us();
		rtl_awdl_offch_locked(padapter, 0);
		t3 = rtl8814native_uptime_us();
		if (out) {
			u64 a = (t1 - t0) / 10, b = (t3 - t2) / 10;

			*out = (u32)(a > 0xffff ? 0xffff : a) | ((u32)(b > 0xffff ? 0xffff : b) << 16);
		}
	}
	_exit_critical_mutex(&rfctl->offch_mutex, NULL);
	return rc;
}
