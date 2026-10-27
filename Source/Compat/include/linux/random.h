/*
 * RTL8814Native — random + misc small shims
 *
 * get_random_bytes() uses XNU's read_random() when available at link
 * time (BSD KPI); the build falls back to a xorshift64* PRNG seeded
 * from mach_absolute_time (fine for backoff/nonce use during
 * bring-up, revisit before shipping — see ROADMAP phase 4).
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
