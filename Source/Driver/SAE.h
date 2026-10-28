/*
 * RTL8814Native — standalone SAE group-19 API
 *
 * This header intentionally uses only standard integer and size types. The
 * implementation has no operating-system or kernel dependencies.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL8814_SAE_H
#define RTL8814_SAE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTL_SAE_GROUP_P256 19U
#define RTL_SAE_SCALAR_LEN 32U
#define RTL_SAE_ELEMENT_LEN 64U
#define RTL_SAE_PMK_LEN 32U
#define RTL_SAE_PMKID_LEN 16U
#define RTL_SAE_KCK_LEN 32U
#define RTL_SAE_CONFIRM_LEN 34U
#define RTL_SAE_COMMIT_MAX_LEN 512U
#define RTL_SAE_TOKEN_MAX_LEN 256U
#define RTL_SAE_PASSWORD_MAX_LEN 63U
#define RTL_SAE_PASSWORD_ID_MAX_LEN 254U
#define RTL_SAE_RSN_IE_LEN 44U

typedef int (*rtl_sae_random_fn)(void *context, uint8_t *output, size_t length);

enum rtl_sae_state {
    RTL_SAE_IDLE = 0,
    RTL_SAE_COMMITTED = 1,
    RTL_SAE_PEER_COMMITTED = 2
};

/* Public storage keeps the module allocation-free and usable in a kext. */
struct rtl_sae_p256 {
    uint32_t state;
    uint16_t send_confirm;
    uint16_t peer_confirm;
    uint8_t peer_confirm_seen;
    uint8_t confirm_sent;
    uint8_t password_id_len;
    uint8_t password_id[RTL_SAE_PASSWORD_ID_MAX_LEN];
    uint16_t token_len;
    uint8_t token[RTL_SAE_TOKEN_MAX_LEN];
    uint8_t pwe_x[RTL_SAE_SCALAR_LEN];
    uint8_t pwe_y[RTL_SAE_SCALAR_LEN];
    uint8_t rand[RTL_SAE_SCALAR_LEN];
    uint8_t own_scalar[RTL_SAE_SCALAR_LEN];
    uint8_t own_element[RTL_SAE_ELEMENT_LEN];
    uint8_t peer_scalar[RTL_SAE_SCALAR_LEN];
    uint8_t peer_element[RTL_SAE_ELEMENT_LEN];
    uint8_t kck[RTL_SAE_KCK_LEN];
    uint8_t pmk[RTL_SAE_PMK_LEN];
    uint8_t pmkid[RTL_SAE_PMKID_LEN];
};

/*
 * Start a new exchange with caller-supplied entropy. The callback is invoked
 * for PWE blinding and fresh commit scalars; it must be cryptographically
 * secure and return 0 on success. No random generator or kernel API is assumed.
 */
int rtl_sae_p256_start(struct rtl_sae_p256 *sae,
                       const uint8_t *password, size_t password_len,
                       const uint8_t own_address[6],
                       const uint8_t peer_address[6],
                       const uint8_t *password_id, size_t password_id_len,
                       rtl_sae_random_fn random_bytes, void *random_context);

#ifdef RTL_SAE_TESTING
/* Deterministic entry point for the fixed IEEE Annex J.10 test vector. */
int rtl_sae_p256_start_fixed(struct rtl_sae_p256 *sae,
                             const uint8_t *password, size_t password_len,
                             const uint8_t own_address[6],
                             const uint8_t peer_address[6],
                             const uint8_t *password_id, size_t password_id_len,
                             const uint8_t rand[32],
                             const uint8_t mask[32]);
#endif

/* Store the nonempty token from a verified group-19 anti-clogging response. */
int rtl_sae_p256_set_token(struct rtl_sae_p256 *sae,
                           const uint8_t *token, size_t token_len);

/* Encode the group-19 commit body, including any stored token and password ID. */
int rtl_sae_p256_write_commit(const struct rtl_sae_p256 *sae,
                              uint8_t *output, size_t output_capacity,
                              size_t *output_len);

/*
 * Process the peer's commit body. peer_token_len is the token length already
 * determined by the authentication-frame handler; group and point lengths are
 * fixed for group 19. On success, the PMK/KCK/PMKID are ready but remain inside
 * the context until both confirms have been exchanged and finish() is called.
 */
int rtl_sae_p256_process_commit(struct rtl_sae_p256 *sae,
                                const uint8_t *body, size_t body_len,
                                size_t peer_token_len);

/* Write/verify SAE confirm bodies (LE16 counter followed by the verifier). */
int rtl_sae_p256_write_confirm(struct rtl_sae_p256 *sae,
                               uint8_t output[RTL_SAE_CONFIRM_LEN]);
int rtl_sae_p256_check_confirm(struct rtl_sae_p256 *sae,
                               const uint8_t *body, size_t body_len);

/* Copy PMK and PMKID out exactly once, then erase the context. The caller
 * must erase both output buffers after handing the keys to the next stage. */
int rtl_sae_p256_finish(struct rtl_sae_p256 *sae,
                        uint8_t pmk[RTL_SAE_PMK_LEN],
                        uint8_t pmkid[RTL_SAE_PMKID_LEN]);

/* Build an SAE/CCMP association RSN IE with one PMKID and BIP-CMAC-128.
 * group_cipher is the AP's RSN group-data cipher suite type. MFPC is set;
 * MFPR is set when pmf_required is nonzero. */
int rtl_sae_build_rsn_ie(uint8_t group_cipher, int pmf_required,
                         const uint8_t pmkid[RTL_SAE_PMKID_LEN],
                         uint8_t *output, size_t output_capacity);

/* AKM 00-0F-AC:8 key schedule helpers for the 4-way handshake. */

/* PTK = KDF-SHA256(PMK, "Pairwise key expansion", min(AA,SPA) || max(AA,SPA) ||
 * min(ANonce,SNonce) || max(ANonce,SNonce)), 384 bits: KCK, KEK, TK. */
void rtl_sae_derive_ptk(const uint8_t pmk[RTL_SAE_PMK_LEN],
                        const uint8_t aa[6], const uint8_t spa[6],
                        const uint8_t anonce[32], const uint8_t snonce[32],
                        uint8_t ptk[48]);

/* AES-128-CMAC (RFC 4493), used as the EAPOL-Key MIC for this AKM. */
void rtl_aes128_cmac(const uint8_t key[16], const uint8_t *data, size_t length,
                     uint8_t mac[16]);

/* One AES-128 block encryption (used by the CCMP management-frame check). */
void rtl_aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/* Erase the complete exchange state, including password-derived values. */
void rtl_sae_p256_clear(struct rtl_sae_p256 *sae);

#ifdef __cplusplus
}
#endif

#endif /* RTL8814_SAE_H */
