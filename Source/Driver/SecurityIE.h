/* OS-independent bounded parsing for 802.11 security information elements. */
#ifndef RTL8814_SECURITY_IE_H
#define RTL8814_SECURITY_IE_H

#include <stddef.h>
#include <stdint.h>

struct rtl_security_key_data {
    uint8_t gtk[32];
    size_t gtk_len;
    uint8_t gtk_key_id;
    uint8_t igtk[16];
    uint16_t igtk_id;
    uint64_t igtk_ipn;
    int has_gtk;
    int has_igtk;
};

/* Parse an IE sequence and return the RSN group cipher and MFPR bit. */
int rtl_security_parse_rsn(const uint8_t *ies, size_t ies_len,
                           uint8_t *group_cipher, int *pmf_required);

/* Parse GTK/IGTK KDEs. Raw group-key payloads are accepted only when requested. */
int rtl_security_parse_key_data(const uint8_t *data, size_t data_len,
                                int allow_raw_gtk,
                                struct rtl_security_key_data *result);

#endif
