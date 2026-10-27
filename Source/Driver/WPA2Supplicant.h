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

/* Configure securitypriv & PMK before calling rtw_set_802_11_ssid() */
void rtl8814_wpa2_configure(_adapter *padapter, const char *ssid, u32 ssid_len,
                            const char *psk, u32 auth_mode);

/* Reset WPA2 state on disconnect */
void rtl8814_wpa2_reset(_adapter *padapter);

/* Returns current WPA2 state (enum rtl8814_wpa2_state) */
int rtl8814_wpa2_get_state(void);

/* Intercepts incoming Ethernet frames; returns 1 if EAPOL (0x888E) consumed */
int rtl8814_wpa2_rx_eapol(_adapter *padapter, struct sk_buff *skb);

#ifdef __cplusplus
}
#endif

#endif /* _RTL8814_WPA2_SUPPLICANT_H */
