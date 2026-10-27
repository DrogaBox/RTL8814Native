/*
 * RTL8814Native — atomics shim for XNU
 *
 * Implemented over libkern's OSDecrementAtomic/OSIncrementAtomic,
 * which compile to lock-prefixed instructions on x86_64 and
 * ldaxr/stxr on arm64.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_ATOMIC_H_RTL8814
#define _LINUX_ATOMIC_H_RTL8814

#include <linux/types.h>
#include <libkern/OSAtomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { volatile int counter; } atomic_t;
typedef struct { volatile long counter; } atomic_long_t;

#define ATOMIC_INIT(i) { (i) }

static inline int atomic_read(const atomic_t *v)
{
        return v->counter;
}

static inline void atomic_set(atomic_t *v, int i)
{
        v->counter = i;
}

static inline void atomic_add(int i, atomic_t *v)
{
        OSAddAtomic(i, &v->counter);
}

static inline void atomic_sub(int i, atomic_t *v)
{
        OSAddAtomic(-i, &v->counter);
}

static inline int atomic_add_return(int i, atomic_t *v)
{
        return OSAddAtomic(i, &v->counter) + i;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
        return OSAddAtomic(-i, &v->counter) - i;
}

static inline int atomic_inc_return(atomic_t *v)
{
        return OSIncrementAtomic(&v->counter) + 1;
}

static inline int atomic_dec_return(atomic_t *v)
{
        return OSDecrementAtomic(&v->counter) - 1;
}

static inline void atomic_inc(atomic_t *v)
{
        OSIncrementAtomic(&v->counter);
}

static inline void atomic_dec(atomic_t *v)
{
        OSDecrementAtomic(&v->counter);
}

static inline int atomic_dec_and_test(atomic_t *v)
{
        return OSDecrementAtomic(&v->counter) == 1;
}

static inline int atomic_inc_and_test(atomic_t *v)
{
        return OSIncrementAtomic(&v->counter) == 0;
}

static inline int atomic_sub_and_test(int i, atomic_t *v)
{
        return OSAddAtomic(-i, &v->counter) == i;
}

static inline int atomic_add_unless(atomic_t *v, int a, int u)
{
        int c = v->counter;
        while (c != u) {
                /* __sync_val_compare_and_swap returns the previous value —
                 * the exact Linux cmpxchg semantics, any int width */
                int old = __sync_val_compare_and_swap(&v->counter, c, c + a);
                if (old == c)
                        break;
                c = old;
        }
        return c != u;
}

static inline int atomic_cmpxchg(atomic_t *v, int old, int newv)
{
        return __sync_val_compare_and_swap(&v->counter, old, newv);
}

static inline int atomic_xchg(atomic_t *v, int newv)
{
        return __atomic_exchange_n(&v->counter, newv, __ATOMIC_SEQ_CST);
}

/* atomic_long variants (used by a few vendor paths) */
static inline long atomic_long_read(const atomic_long_t *v) { return v->counter; }
static inline void atomic_long_set(atomic_long_t *v, long i) { v->counter = i; }
static inline void atomic_long_inc(atomic_long_t *v) { OSIncrementAtomic64((int64_t *)&v->counter); }
static inline void atomic_long_dec(atomic_long_t *v) { OSDecrementAtomic64((int64_t *)&v->counter); }

/* Barriers — XNU kexts run on both archs; use full barriers */
#define smp_mb()      OSMemoryBarrier()
#define smp_rmb()     OSMemoryBarrier()
#define smp_wmb()     OSMemoryBarrier()
#define mb()          OSMemoryBarrier()
#define rmb()         OSMemoryBarrier()
#define wmb()         OSMemoryBarrier()
#define barrier()     __asm__ __volatile__("" ::: "memory")

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_ATOMIC_H_RTL8814 */
