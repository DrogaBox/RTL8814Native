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

int rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len);
int rtl8814native_radio_awdl_port(const uint8_t *mac);
int rtl8814native_radio_set_channel(uint8_t ch);
int rtl8814native_radio_awdl_offch(uint8_t ch);
extern _adapter *rtl8814native_get_primary_adapter(void);

static const u8 rtl_awdl_bssid[ETH_ALEN] = { 0x00, 0x25, 0x00, 0xFF, 0x94, 0x73 };

int rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct mlme_ext_priv *pmlmeext;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	u8 *pframe;
	bool awdl, data;

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

	/* Data shares the 32 management xmit frames with the sync frames and the
	 * infrastructure's own management TX: it may only use half of them. */
	if (data && padapter->xmitpriv.free_xframe_ext_cnt < NR_XMIT_EXTBUFF / 2)
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
	SetSeqNum(pframe, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;

	/* The descriptor sets HWSEQ_EN for non-QoS frames, so the hardware
	 * overwrites this number on air. Group-addressed frames go out once
	 * because of the descriptor's BMC bit (taken from A1); retry_ctrl only
	 * picks the unicast retry limit. */
	if (IS_MCAST(GetAddr1Ptr(pframe)))
		pattrib->retry_ctrl = _FALSE;
	pattrib->pktlen = len;
	pattrib->last_txcmdsz = len;

	dump_mgntframe(padapter, pmgntframe);
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
u8 rtl8814native_awdl_port1;

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

	if (!padapter || !padapter->bup || RTW_CANNOT_RUN(padapter))
		return -ENODEV;
	if (rtw_chset_search_ch(adapter_to_chset(padapter), ch) < 0)
		return -EINVAL;
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED | _FW_UNDER_LINKING | _FW_UNDER_SURVEY))
		return -EBUSY;
	/* AWDL frames go at OFDM 6 Mb/s on either band, never CCK on 5 GHz */
	update_mgnt_tx_rate(padapter, IEEE80211_OFDM_RATE_6MB);
	if (rtw_set_chbw_cmd(padapter, ch, CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE,
	                     RTW_CMDF_CHBW_IDLE_ONLY)
	    != _SUCCESS)
		return -ENOMEM;
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
 * rtl_radio_status keeps reporting it. -EBUSY unless linked and idle.
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
			set_channel_bwmode(padapter, home, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
			rtw_hal_macid_wakeup_all_used(padapter);
			issue_nulldata(padapter, NULL, 0, 0, 0);
			rfctl->offch_state = OFFCHS_NONE;
			rtw_mi_os_xmit_schedule(padapter);
		}
		goto out;
	}
	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED) ||
	    check_fwstate(&padapter->mlmepriv, _FW_UNDER_LINKING | _FW_UNDER_SURVEY) ||
	    (rfctl->offch_state != OFFCHS_NONE && rfctl->offch_state != OFFCHS_AWDL)) {
		rc = -EBUSY;
		goto out;
	}
	if (rfctl->offch_state == OFFCHS_NONE) {
		rfctl->offch_state = OFFCHS_AWDL;
		rtw_hal_macid_sleep_all_used(padapter);
		issue_nulldata(padapter, NULL, 1, 0, 0);
		/* let the null leave on the BSS channel before the switch */
		rtw_msleep_os(2);
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
