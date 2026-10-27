/* limits.h — numeric limits.
 * INT_MAX/LLONG_MAX/SIZE_MAX are already defined by the kernel SDK
 * (i386/limits.h, stdint.h) — only define them if genuinely absent,
 * and pull types.h in so u8/s64/size_t are always in scope. */
#ifndef _LINUX_LIMITS_H_RTL8814
#define _LINUX_LIMITS_H_RTL8814

#include <linux/types.h>

#define U8_MAX    ((u8)~0U)
#define S8_MAX    ((s8)(U8_MAX >> 1))
#define U16_MAX   ((u16)~0U)
#define S16_MAX   ((s16)(U16_MAX >> 1))
#define U32_MAX   ((u32)~0U)
#define S32_MAX   ((s32)(U32_MAX >> 1))
#define U64_MAX   ((u64)~0ULL)
#define S64_MAX   ((s64)(U64_MAX >> 1))
#ifndef INT_MAX
/* Body spelled EXACTLY like Darwin's i386/limits.h: an identical
 * redefinition is benign under C99 (no warning, clang or gcc), so
 * include order can never produce -Wmacro-redefined noise. */
#define INT_MAX   2147483647
#endif
#ifndef LLONG_MAX
#define LLONG_MAX 0x7fffffffffffffffLL   /* same body as Darwin */
#endif
#ifndef SIZE_MAX
#define SIZE_MAX  ((size_t)~0ULL)
#endif
#define PATH_MAX  1024
#endif
