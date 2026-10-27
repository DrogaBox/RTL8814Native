/*
 * RTL8814Native — Linux types shim for XNU (macOS kernel)
 *
 * Provides the basic Linux kernel types over libkern/XNU primitives.
 * Part of the Compat layer that lets the Realtek 8814au Linux driver
 * (GPL-2.0, morrownr/8814au) compile inside a macOS kext.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_TYPES_H_RTL8814
#define _LINUX_TYPES_H_RTL8814

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Darwin already provides mode_t (unsigned short!), pid_t, off_t,
 * ssize_t... via sys/types.h with ITS OWN underlying types. Redefining
 * them with Linux shapes is a hard error, so we reuse Darwin's and
 * only typedef what the kernel SDK does not provide. */
#if defined(__APPLE__)
#include <sys/types.h>
#include <libkern/libkern.h>
#include <IOKit/system.h>
#include <IOKit/IOLocks.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

typedef uint8_t   __u8;
typedef uint16_t  __u16;
typedef uint32_t  __u32;
typedef uint64_t  __u64;

/* Byte-order annotated types: x86_64/arm64 are little-endian,
 * so the conversion macros are identity. */
typedef uint16_t  __le16;
typedef uint32_t  __le32;
typedef uint64_t  __le64;
typedef uint16_t  __be16;
typedef uint32_t  __be32;
typedef uint64_t  __be64;

typedef unsigned int   uint;
typedef unsigned long  ulong;
typedef long           llong;
typedef unsigned long long ullong;

typedef size_t         __kernel_size_t;
typedef ssize_t        __kernel_ssize_t;
typedef off_t          loff_t;

struct seq_file;
struct ifreq;

typedef unsigned int gfp_t;          /* allocation flags — see slab.h */
typedef uintptr_t    dma_addr_t;     /* no real DMA mapping on USB path */
#if !defined(__APPLE__)              /* Darwin: sys/types.h */
typedef int          pid_t;
#endif

/* Attribute no-ops — defined BEFORE first use */
#ifndef __bitwise
#define __bitwise
#endif
#ifndef __user
#define __user
#endif
#ifndef __kernel
#define __kernel
#endif
#define __init
#define __exit
#define __read_mostly
#define __aligned(x)    __attribute__((aligned(x)))
#define __packed        __attribute__((packed))
#define __iomem
#define __must_check    __attribute__((warn_unused_result))
#define __force
#define __maybe_unused  __attribute__((unused))
#ifdef __always_inline
#undef __always_inline
#endif
#define __always_inline inline __attribute__((always_inline))
#define __nocast
#define __percpu
#define __rcu
#define __printf(a, b)  __attribute__((format(printf, a, b)))
#ifndef __unused                     /* Darwin: sys/cdefs.h */
#define __unused     __attribute__((unused))
#endif
#ifndef __used
#define __used       __attribute__((used))
#endif

#if !defined(__APPLE__)              /* Darwin: mode_t = unsigned short */
typedef unsigned int  __bitwise      mode_t;
#endif

typedef u16 __bitwise __le16_t;
typedef u32 __bitwise __le32_t;

/* container_of — used everywhere by the vendor driver.
 * __typeof__ (NOT typeof): the underscore spelling is the only one
 * valid in BOTH C and strict -std=c++17 — Apple clang rejects plain
 * typeof in the C++ Driver translation units. */
#define container_of(ptr, type, member) ({                  \
        const __typeof__(((type *)0)->member) *__mptr = (ptr);      \
        (type *)((char *)__mptr - offsetof(type, member)); })

/* min/max/clamp helpers used by core/ and hal/.
 * IOKit's IOLib.h also defines min/max — whichever header comes first
 * wins; both bodies are semantically identical. */
#ifndef min
#define min(x, y)        ((x) < (y) ? (x) : (y))
#endif
#ifndef max
#define max(x, y)        ((x) > (y) ? (x) : (y))
#endif
#define min_t(t, x, y)   ((t)(x) < (t)(y) ? (t)(x) : (t)(y))
#define max_t(t, x, y)   ((t)(x) > (t)(y) ? (t)(x) : (t)(y))
#define clamp(val, lo, hi) min((__typeof__(val))max(val, lo), hi)
#define ARRAY_SIZE(arr)  (sizeof(arr) / sizeof((arr)[0]))
#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define round_up(x, y)   ((((x) + ((y) - 1)) / (y)) * (y))
#define round_down(x, y) ((x) & ~((__typeof__(x))(y) - 1))
#define is_power_of_2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))
#define swap(a, b) do { __typeof__(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

#ifndef NULL   /* stddef.h / IOLib chains define it first */
#define NULL ((void *)0)
#endif
#define BITS_PER_LONG   64
#define BITS_PER_BYTE   8
#define BIT(nr)         (1UL << (nr))
#define BIT_ULL(nr)     (1ULL << (nr))
#define GENMASK(h, l)   (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_TYPES_H_RTL8814 */
