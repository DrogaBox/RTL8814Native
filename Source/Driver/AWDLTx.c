/*
 * RTL8814Native — raw 802.11 management frame sender for the radio ABI
 *
 * Another kext (the AWDL layer) hands a complete management frame: 802.11
 * header and body, no FCS. It goes out on the current operating channel
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
extern _adapter *rtl8814native_get_primary_adapter(void);

int rtl8814native_radio_tx_80211(const uint8_t *frame, uint32_t len)
{
	_adapter *padapter = rtl8814native_get_primary_adapter();
	struct mlme_ext_priv *pmlmeext;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	u8 *pframe;

	if (!frame || len < WLAN_HDR_A3_LEN || len > MAX_XMIT_EXTBUF_SZ - TXDESC_OFFSET)
		return -EINVAL;
	/* management frames only: the header is taken as a 3-address one */
	if (GetFrameType(frame) != WIFI_MGT_TYPE)
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
