/*
 * RTL8814Native — In-Kernel WPA2-PSK 4-Way EAPOL Supplicant
 *
 * Implements IEEE 802.11i / WPA2-PSK (RSN / AES-CCMP):
 *   - PBKDF2-SHA1 (4096 iterations, passphrase + SSID -> 32-byte PMK)
 *   - PRF-384 ("Pairwise key expansion" -> 48-byte PTK = KCK || KEK || TK)
 *   - EAPOL-Key 4-Way Handshake (Msg 1/4 -> Msg 2/4, Msg 3/4 -> Msg 4/4)
 *   - Group Key Handshake (Msg 1/2 -> Msg 2/2)
 *   - RFC 3394 AES-128 Key Unwrap (extracts GTK KDE from Msg 3/4)
 *   - Hardware CAM installation (rtw_setstakey_cmd + rtw_set_key)
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_WPA2_SUPPLICANT_H
#define _RTL8814_WPA2_SUPPLICANT_H

#include <drv_types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rtl8814_wpa2_state {
	WPA2_STATE_IDLE       = 0,
	WPA2_STATE_WAIT_MSG1  = 1,
	WPA2_STATE_WAIT_MSG3  = 2,
	WPA2_STATE_COMPLETED  = 3,
	WPA2_STATE_FAILED     = 4
};

#define RTL8814_AUTH_SAE 2

/* Configure securitypriv & PMK before calling rtw_set_802_11_ssid(). */
void rtl8814_wpa2_configure(_adapter *padapter, const char *ssid, u32 ssid_len,
                            const char *psk, u32 psk_len, u32 auth_mode);

/* Arm the 4-way handshake for AKM 00-0F-AC:8 once SAE has produced the PMK */
void rtl8814_wpa2_start_sae(_adapter *padapter, const u8 pmk[32],
                            const u8 *rsn_ie, u32 rsn_len, u8 grp_cipher);

/* Reset WPA2 state on disconnect */
void rtl8814_wpa2_reset(_adapter *padapter);

/* Returns current WPA2 state (enum rtl8814_wpa2_state) */
int rtl8814_wpa2_get_state(void);

/* 802.11w filter for a received management frame (SAE links only): decrypts
 * protected unicast deauth/disassoc/action in place (*len shrinks), checks BIP
 * on group-addressed ones. Returns 1 to keep the frame, 0 to drop it. */
int rtl8814_wpa2_pmf_rx(u8 *frame, u32 *len);

/* Protect our own deauth/disassoc to the AP of the last SAE link (CCMP, in place). */
int rtl8814_wpa2_pmf_protect_tx(u8 *frame, u32 *len);
void rtl8814_wpa2_pmf_tx_enable(int on);

/* Intercepts incoming Ethernet frames; returns 1 if EAPOL (0x888E) consumed */
int rtl8814_wpa2_rx_eapol(_adapter *padapter, struct sk_buff *skb);

#ifdef __cplusplus
}
#endif

#endif /* _RTL8814_WPA2_SUPPLICANT_H */
