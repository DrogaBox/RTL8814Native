/*
 * RTL8814Native — byteorder + compiler bits shim for XNU
 *
 * x86_64 and arm64 are both little-endian, so cpu_to_le* are identity
 * and cpu_to_be* are byte swaps.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_BYTEORDER_LITTLE_H_RTL8814
#define _LINUX_BYTEORDER_LITTLE_H_RTL8814

#include <linux/types.h>
#include <libkern/OSByteOrder.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __LITTLE_ENDIAN     1234
#define __BIG_ENDIAN        4321
#define __BYTE_ORDER        __LITTLE_ENDIAN

static inline u16 __fswab16(u16 x) { return OSSwapInt16(x); }
static inline u32 __fswab32(u32 x) { return OSSwapInt32(x); }
static inline u64 __fswab64(u64 x) { return OSSwapInt64(x); }

#define cpu_to_le16(x)  ((u16)(x))
#define cpu_to_le32(x)  ((u32)(x))
#define cpu_to_le64(x)  ((u64)(x))
#define le16_to_cpu(x)  ((u16)(x))
#define le32_to_cpu(x)  ((u32)(x))
#define le64_to_cpu(x)  ((u64)(x))

#define cpu_to_be16(x)  __fswab16(x)
#define cpu_to_be32(x)  __fswab32(x)
#define cpu_to_be64(x)  __fswab64(x)
#define be16_to_cpu(x)  __fswab16(x)
#define be32_to_cpu(x)  __fswab32(x)
#define be64_to_cpu(x)  __fswab64(x)

#define htons(x)    __fswab16(x)
#define htonl(x)    __fswab32(x)
#define ntohs(x)    __fswab16(x)
#define ntohl(x)    __fswab32(x)

/* Unaligned access helpers — x86_64/arm64 tolerate unaligned loads.
 * __typeof__: valid in C and strict C++ (see types.h note). */
#define get_unaligned(p)        (*(const __typeof__(*(p)) *)(p))
#define get_unaligned_le16(p)   ((u16)((const u8 *)(p))[0] | ((u16)((const u8 *)(p))[1] << 8))
#define get_unaligned_le32(p)   ((u32)get_unaligned_le16(p) | ((u32)get_unaligned_le16((const u8 *)(p) + 2) << 16))
#define put_unaligned(val, p)   (*(__typeof__(*(p)) *)(p) = (val))
#define put_unaligned_le16(val, p) do { \
        ((u8 *)(p))[0] = (u8)(val); ((u8 *)(p))[1] = (u8)((val) >> 8); } while (0)

#define __get_unaligned_le16(p) get_unaligned_le16(p)
#define __get_unaligned_le32(p) get_unaligned_le32(p)

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_BYTEORDER_LITTLE_H_RTL8814 */
