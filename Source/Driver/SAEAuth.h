/*
 * RTL8814Native — SAE authentication frame sender (vendor-tree side)
 *
 * SAE.c holds the OS-free exchange; this half turns a serialized commit or
 * confirm body into an IEEE 802.11 authentication frame (algorithm 3) and
 * hands it to the vendor management TX path. Nothing calls it until the SAE
 * join state machine is connected (Docs/SAE_HANDOFF.md).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL8814_SAE_AUTH_H
#define RTL8814_SAE_AUTH_H

#include <drv_types.h>
#include <hal_data.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transmit one SAE authentication frame to the BSS being joined. body is the
 * already-serialized SAE body (group+scalar+element for a commit, counter+
 * verifier for a confirm); body_len may be 0 for a pure status frame such as
 * an anti-clogging or rejected-exchange reply. Returns 0 when the frame was
 * queued for TX, -1 on allocation failure or while the channel is blocked.
 */
int rtl_sae_issue_auth(_adapter *padapter, uint16_t seq, uint16_t status,
                       const uint8_t *body, size_t body_len);

#ifdef __cplusplus
}
#endif

#endif /* RTL8814_SAE_AUTH_H */
