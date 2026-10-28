/*
 * RTL8814Native — SAE authentication frame sender (vendor-tree side)
 *
 * Mirrors the client branch of issue_auth() but with algorithm 3 and a raw
 * body, so commit and confirm frames can carry the group-19 fields SAE.c
 * serializes. The frame is addressed to the BSS being joined and submitted
 * through the shared management TX queue; no WEP/privacy path is involved.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include <drv_types.h>
#include <hal_data.h>
#include "RTLJoinLog.h"
#include "SAE.h"
#include "SAEAuth.h"

int rtl_sae_issue_auth(_adapter *padapter, uint16_t seq, uint16_t status,
                       const uint8_t *body, size_t body_len)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	struct rtw_ieee80211_hdr *pwlanhdr;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe;
	u16 val16;

	if (body_len > RTL_SAE_COMMIT_MAX_LEN)
		return -1;
	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return -1;

	pmgntframe = alloc_mgtxmitframe(&padapter->xmitpriv);
	if (pmgntframe == NULL)
		return -1;

	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	pwlanhdr->frame_ctl = 0;
	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_AUTH);

	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* algorithm 3 (SAE), transaction sequence, status */
	val16 = cpu_to_le16(3);
	pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (u8 *)&val16, &pattrib->pktlen);
	val16 = cpu_to_le16(seq);
	pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (u8 *)&val16, &pattrib->pktlen);
	val16 = cpu_to_le16(status);
	pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (u8 *)&val16, &pattrib->pktlen);

	if (body_len) {
		_rtw_memcpy(pframe, body, body_len);
		pframe += body_len;
		pattrib->pktlen += (uint)body_len;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	rtl8814native_jlog(JL_SAE_AUTH_TX, seq, status);
	dump_mgntframe(padapter, pmgntframe);
	return 0;
}
