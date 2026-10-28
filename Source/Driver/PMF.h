/*
 * RTL8814Native — protected management frames (IEEE 802.11w), OS-free part
 *
 * Unicast robust management frames (deauthentication, disassociation, action)
 * arrive CCMP-encrypted under the pairwise TK; group-addressed ones carry a
 * BIP-CMAC-128 Management MIC element made with the IGTK. Both checks live
 * here with only standard integer types so they can be tested in userland.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL8814_PMF_H
#define RTL8814_PMF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTL_PMF_MGMT_HDR_LEN 24U
#define RTL_PMF_CCMP_HDR_LEN 8U
#define RTL_PMF_CCMP_MIC_LEN 8U
#define RTL_PMF_MME_LEN 18U

/*
 * Decrypt and authenticate a protected unicast management frame in place.
 * frame is the whole frame from the 802.11 header on (no FCS), len its length.
 * On success the CCMP header and MIC are removed, the Protected bit is cleared,
 * *out_len is the new length and *pn the 48-bit packet number (for the replay
 * check, which the caller owns). Returns 0, or -1 for a malformed frame or a
 * failed MIC; the frame contents are undefined on failure (drop it).
 */
int rtl_pmf_ccmp_decrypt_mgmt(const uint8_t tk[16], uint8_t *frame, size_t len,
                              size_t *out_len, uint64_t *pn);

/*
 * Verify the BIP-CMAC-128 Management MIC element at the end of a
 * group-addressed management frame. Returns 0 when it verifies (then *key_id
 * and *ipn hold its fields), -1 when it is missing or wrong.
 */
int rtl_pmf_bip_verify(const uint8_t igtk[16], const uint8_t *frame, size_t len,
                       unsigned int *key_id, uint64_t *ipn);

#ifdef __cplusplus
}
#endif

#endif /* RTL8814_PMF_H */
