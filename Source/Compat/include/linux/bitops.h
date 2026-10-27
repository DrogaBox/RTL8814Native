/*
 * RTL8814Native — bitops shim for XNU
 *
 * The vendor driver manipulates per-adapter state bitmaps. These are
 * almost always touched under a lock, so plain memory ops suffice;
 * test_and_set/test_and_clear use OSCompareAndSwap for safety.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_BITOPS_H_RTL8814
#define _LINUX_BITOPS_H_RTL8814

#include <linux/types.h>
#if defined(__APPLE__)
#include <libkern/libkern.h>   /* fls()/ffs() from the kernel library */
#else
/* no host versions off-Darwin — provide the Linux semantics inline */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BITS_TO_LONGS(nr)  DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

static inline void set_bit(int nr, volatile unsigned long *addr)
{
        __sync_or_and_fetch(&addr[nr / BITS_PER_LONG], 1UL << (nr % BITS_PER_LONG));
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
        __sync_and_and_fetch(&addr[nr / BITS_PER_LONG], ~(1UL << (nr % BITS_PER_LONG)));
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
        __sync_xor_and_fetch(&addr[nr / BITS_PER_LONG], 1UL << (nr % BITS_PER_LONG));
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
        return (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1UL;
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
        unsigned long mask = 1UL << (nr % BITS_PER_LONG);
        /* __sync_fetch_and_* are the same atomics the kernel's own
         * set_bit uses — full 64-bit words, no Darwin signature games */
        unsigned long old = __sync_fetch_and_or(&addr[nr / BITS_PER_LONG], mask);
        return (old & mask) != 0;
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
        unsigned long mask = 1UL << (nr % BITS_PER_LONG);
        unsigned long old = __sync_fetch_and_and(&addr[nr / BITS_PER_LONG], ~mask);
        return (old & mask) != 0;
}

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
        unsigned long mask = 1UL << (nr % BITS_PER_LONG);
        unsigned long old = __sync_fetch_and_xor(&addr[nr / BITS_PER_LONG], mask);
        return (old & mask) != 0;
}

#if !defined(__APPLE__)
/* On Darwin libkern.h exports ffs()/fls() (unsigned int args — same
 * 1-based semantics, 0 for 0); redefining them is a hard SDK clash. */
static inline int fls(int x)
{
        return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}
#endif

static inline int fls64(u64 x)
{
        return x ? 64 - __builtin_clzll(x) : 0;
}

#if !defined(__APPLE__)
static inline int ffs(int x)
{
        return x ? __builtin_ctz(x) + 1 : 0;
}
#endif

static inline unsigned long __ffs(unsigned long word)
{
        return __builtin_ctzl(word);
}

static inline unsigned long hweight8(u8 w)
{
        unsigned int res = w - ((w >> 1) & 0x55);
        res = (res & 0x33) + ((res >> 2) & 0x33);
        return (res + (res >> 4)) & 0x0F;
}

static inline unsigned long hweight32(u32 w)
{
        w -= (w >> 1) & 0x55555555;
        w =  (w & 0x33333333) + ((w >> 2) & 0x33333333);
        w =  (w + (w >> 4)) & 0x0F0F0F0F;
        return (w * 0x01010101) >> 24;
}

#define for_each_set_bit(bit, addr, size) \
        for ((bit) = 0; (bit) < (size); (bit)++) \
                if (test_bit((bit), (addr)))

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_BITOPS_H_RTL8814 */
