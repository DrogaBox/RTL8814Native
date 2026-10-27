/*
 * RTL8814Native — string functions shim for XNU kernel
 *
 * libkern provides most C string routines; we fill the gaps the
 * vendor driver relies on.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_STRING_H_RTL8814
#define _LINUX_STRING_H_RTL8814

#include <string.h>
#include <sys/types.h>
#include <stdarg.h>         /* va_list — rtl_vsnprintf/kvasprintf          */
#include <linux/types.h>    /* gfp_t + __printf for the snprintf decls    */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef memset
#define memset  memset
#endif
#ifndef memcpy
#define memcpy  memcpy
#endif

/* strlcat/strlcpy are declared by the kernel SDK's own <string.h> —
 * re-declaring them here caused C/C++ linkage conflicts in mixed TUs. */

/* Kernel-safe re-implementations of str APIs not exported to kexts */
int    rtl_strncasecmp(const char *s1, const char *s2, size_t n);
char  *rtl_strsep(char **s, const char *ct);
size_t rtl_strspn(const char *s, const char *accept);
char  *rtl_strpbrk(const char *cs, const char *ct);
int    rtl_snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);

#define strncasecmp rtl_strncasecmp
#define strsep      rtl_strsep
#define strspn      rtl_strspn
#define strpbrk     rtl_strpbrk
#define snprintf    rtl_snprintf   /* routed to vsnprintf from libkern */

int  rtl_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
#define vsnprintf rtl_vsnprintf

static inline char *rtl_strcpy(char *dst, const char *src)
{
        char *r = dst;
        while ((*dst++ = *src++));
        return r;
}
#undef strcpy
#define strcpy rtl_strcpy

/* Simple kernel regex helpers used by rtw_debug (phase-limited stubs) */
static inline void *kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
        (void)gfp; (void)fmt; (void)ap;
        return NULL;    /* TODO phase 2: only needed for verbose debug paths */
}
#define kasprintf(gfp, fmt, ...)  ((void *)0)

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_STRING_H_RTL8814 */
