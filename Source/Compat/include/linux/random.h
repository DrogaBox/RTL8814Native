/*
 * RTL8814Native — random + misc small shims
 *
 * get_random_bytes() is a xorshift64* PRNG seeded from mach_absolute_time:
 * fine for backoff, nonces and other non-secret uses, NOT for key material.
 * rtl_sae_entropy_fill() is the cryptographically secure path for SAE; it
 * draws from XNU's kernel CSPRNG through a weak read_random import and
 * fails closed when the kernel does not provide the symbol.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_RANDOM_H_RTL8814
#define _LINUX_RANDOM_H_RTL8814

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtl_get_random_bytes(void *buf, int len);
#define get_random_bytes(buf, len)  rtl_get_random_bytes((buf), (len))

/*
 * rtl_sae_random_fn-compatible entropy for SAE exchanges (PWE blinding,
 * commit scalars). context is ignored. Returns 0 after filling output, or
 * -1 when the kernel CSPRNG import did not bind or the request is out of
 * range; callers must treat that as SAE unavailable, never as a reason to
 * fall back to a weaker generator.
 */
int rtl_sae_entropy_fill(void *context, uint8_t *output, size_t length);

static inline u32 prandom_u32(void)
{
        u32 v;
        rtl_get_random_bytes(&v, sizeof(v));
        return v;
}

#define get_random_u32  prandom_u32

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_RANDOM_H_RTL8814 */
