/*
 * RTL8814Native — SAE join state machine (vendor-tree side)
 *
 * Ties SAE.c to the vendor MLME: the join starts the exchange, received
 * authentication frames feed it, and the link timer retransmits. All the
 * cryptography runs on a dedicated worker thread (rtl_sae_wq), never on the
 * receive or timer paths. The exchange ends by building the association RSN
 * IE and calling start_clnt_assoc(); the 4-way handshake is not part of this
 * file (Docs/SAE_HANDOFF.md, step 4).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL8814_SAE_JOIN_H
#define RTL8814_SAE_JOIN_H

#include <drv_types.h>
#include <hal_data.h>
#include <stddef.h>
#include <stdint.h>

/* Values of the "a" field of JL_SAE_STEP */
enum {
	RTL_SAE_STEP_PREPARE = 1,   /* b: 0 ok, else errno */
	RTL_SAE_STEP_START,         /* b: 0 ok, else rtl_sae_p256_start result */
	RTL_SAE_STEP_COMMIT_TX,     /* b: TX result */
	RTL_SAE_STEP_TOKEN,         /* b: token length */
	RTL_SAE_STEP_PEER_COMMIT,   /* b: process_commit result */
	RTL_SAE_STEP_CONFIRM_TX,    /* b: TX result */
	RTL_SAE_STEP_PEER_CONFIRM,  /* b: check_confirm result */
	RTL_SAE_STEP_KEYS,          /* b: finish result */
	RTL_SAE_STEP_ASSOC,         /* b: RSN IE length handed to association */
	RTL_SAE_STEP_RETRY,         /* b: retransmitted sequence */
	RTL_SAE_STEP_FAIL,          /* b: status reported for the join */
	RTL_SAE_STEP_STALE,         /* b: 1 work dropped because the join moved on */
	RTL_SAE_STEP_COMPUTE_MS,    /* b: ms taken by the last heavy call */
};

#ifdef __cplusplus
extern "C" {
#endif

/* Kext lifetime: called once after the work queues exist / before they stop. */
void rtl_sae_join_init(void);
void rtl_sae_join_exit(void);

/* Remember the passphrase and set up securitypriv so the join carries an RSN
 * IE slot. Returns 0, or a negative errno (-ENOTSUP when no secure entropy
 * source is bound; the join then fails closed). The caller wipes its copy. */
int rtl_sae_join_prepare(_adapter *padapter, const uint8_t *password, uint32_t password_len);

/* True from prepare until the exchange succeeds, fails or is cancelled. */
int rtl_sae_join_active(_adapter *padapter);

/* start_clnt_auth(): queue the exchange start. */
void rtl_sae_join_start(_adapter *padapter);

/* OnAuthClient(): frame is the authentication body (algorithm field first). */
void rtl_sae_join_rx_auth(_adapter *padapter, const uint8_t *frame, uint32_t len);

/* link_timer_hdl(): the link timer fired while in the auth state. */
void rtl_sae_join_timeout(_adapter *padapter);

/* Leave, or any join that is not SAE: drop the exchange and all secrets. */
void rtl_sae_join_cancel(_adapter *padapter);

/* start_clnt_auth(): the SAE join was cancelled while it was starting; the
 * caller must fail the join instead of sending an open authentication. */
int rtl_sae_join_aborted(_adapter *padapter);

/* A join that is not SAE is starting: forget an earlier abort. */
void rtl_sae_join_clear_abort(void);

/* issue_assocreq(): the RSN IE to send instead of the AP's own, once SAE has
 * produced the PMKID. Returns 1 and fills out/len when it is ready. */
int rtl_sae_join_assoc_rsn(_adapter *padapter, uint8_t *out, uint32_t *len);

#ifdef __cplusplus
}
#endif

#endif /* RTL8814_SAE_JOIN_H */
