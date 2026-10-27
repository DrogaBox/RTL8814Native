/* fakesdk stddef.h — C++ mode uses __null like the real clang SDK
 * (implicit conversion to any pointer type, no -fpermissive errors) */
#ifndef _FAKE_STDDEF_H
#define _FAKE_STDDEF_H
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#undef NULL
#ifdef __cplusplus
#define NULL __null
#else
#define NULL ((void*)0)
#endif
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
