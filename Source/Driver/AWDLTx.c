/*
 * RTL8814Native — raw 802.11 frame sender for the radio ABI
 *
 * Another kext (the AWDL layer) hands a complete management frame, or an AWDL
 * data frame: 802.11 header and body, no FCS. It goes out on the current operating channel
 * through the shared management TX queue, as SAEAuth.c does for its
 * authentication frames. The sequence number is the driver's; everything
 * else is sent as given. Category 127 action frames are not robust, so the
 * PMF hook in dump_mgntframe leaves them alone.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include <drv_types.h>
#include <hal_data.h>

int rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len);
int rtl8814native_radio_awdl_port(const uint8_t *mac);
int rtl8814native_radio_set_channel(uint8_t ch);
extern _adapter *rtl8814native_get_primary_adapter(void);

static const u8 rtl_awdl_bssid[ETH_ALEN] = { 0x00, 0x25, 0x00, 0xFF, 0x94, 0x73 };

int rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct mlme_ext_priv *pmlmeext;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	u8 *pframe;

	if (!frame || len < WLAN_HDR_A3_LEN || len > MAX_XMIT_EXTBUF_SZ - TXDESC_OFFSET)
		return -EINVAL;
	/* Management frames, or AWDL data frames: data or QoS data, To/From DS 0
	 * (3-address header), A3 the AWDL BSSID. The MGNT descriptor does not look
	 * at the frame type, and the PMF hook leaves non-management frames alone. */
	if (GetFrameType(frame) != WIFI_MGT_TYPE &&
	    (GetFrameType(frame) != WIFI_DATA_TYPE || (frame[1] & 0x03) ||
	     _rtw_memcmp(frame + 16, rtl_awdl_bssid, ETH_ALEN) != _TRUE))
		return -EINVAL;
	if (!padapter || !padapter->bup || RTW_CANNOT_TX(padapter))
		return -ENODEV;
	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return -EBUSY;

	pmgntframe = alloc_mgtxmitframe(&padapter->xmitpriv);
	if (pmgntframe == NULL)
		return -ENOMEM;

	pmlmeext = &padapter->mlmeextpriv;
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, TXDESC_OFFSET);
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	_rtw_memcpy(pframe, frame, len);
	SetSeqNum(pframe, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;

	/* group-addressed frames get no ACK: no retries */
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
	if (rtw_set_chbw_cmd(padapter, ch, CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE, 0)
	    != _SUCCESS)
		return -ENOMEM;
	return 0;
}
