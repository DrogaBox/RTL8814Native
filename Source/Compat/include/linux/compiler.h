/* compiler.h — attribute no-ops not already in types.h */
#ifndef _LINUX_COMPILER_H_RTL8814
#define _LINUX_COMPILER_H_RTL8814
#define __compiletime_warning(msg)
#define __compiletime_assert(cond, msg, prefix, suffix)
#define barrier_data(ptr) __asm__ __volatile__("" : : "r"(ptr) : "memory")
#define lockless_dereference(p) (*(__typeof__(p) volatile *)&(p))
/* __builtin_types_compatible_p is a C-ONLY builtin (gcc 14 rejects it
 * in C++; Apple clang too). No vendor call site uses __same_type
 * (verified: zero references in the 8814au tree), so it stays C-only
 * — a C++ TU that needs it must fail loudly, not answer wrong. */
#ifndef __cplusplus
#define __same_type(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#endif
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif
