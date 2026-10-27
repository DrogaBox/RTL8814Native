/*
 * RTL8814Native — errno shim for XNU
 *
 * BSD errno values mostly coincide with Linux for the codes the
 * driver actually checks. Anything exotic gets mapped to a sane
 * generic value.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_ERRNO_H_RTL8814
#define _LINUX_ERRNO_H_RTL8814

#include <sys/errno.h>
#include <linux/types.h>   /* __must_check for the PTR_ERR/IS_ERR helpers */

#ifdef __cplusplus
extern "C" {
#endif

/* Linux errno VALUES (the vendor driver compares against these).
 * BSD/macOS headers define some of the same names with different
 * numbers, so undef each one before redefining it. */
#undef EPERM
#undef ENOENT
#undef EINTR
#undef EIO
#undef ENXIO
#undef EAGAIN
#undef ENOMEM
#undef EBUSY
#undef EEXIST
#undef EINVAL
#undef ENFILE
#undef ENOSPC
#undef EPIPE
#undef ERANGE
#undef ENOMSG
#undef ENODEV
#undef EOPNOTSUPP
#undef ENETDOWN
#undef ETIMEDOUT
#undef EINPROGRESS
#undef EALREADY
#undef ENOTCONN
#undef ECONNREFUSED
#undef ETIME

#define EPERM           1
#define ENOENT          2
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define EAGAIN          11
#define ENOMEM          12
#define EBUSY           16
#define EEXIST          17
#define EINVAL          22
#define ENFILE          23
#define ENOSPC          28
#define EPIPE           32
#define ERANGE          34
#define ENOMSG          35
#define ENODEV          19
#define EOPNOTSUPP      102
#define ENETDOWN        50
#define ETIMEDOUT       60
#define EINPROGRESS     36
#define EALREADY        37
#define ENOTCONN        57
#define ECONNREFUSED    61
#define ETIME           62   /* semaphore.h timeout paths */

#define ERESTARTSYS     512   /* seen in vendor retry paths; never returned */

#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
#define MAX_ERRNO       4095

static inline long __must_check PTR_ERR(const void *ptr)
{
        return (long)(intptr_t)ptr;
}
static inline long __must_check IS_ERR(const void *ptr)
{
        return IS_ERR_VALUE((unsigned long)(intptr_t)ptr);
}
static inline long __must_check IS_ERR_OR_NULL(const void *ptr)
{
        return !ptr || IS_ERR_VALUE((unsigned long)(intptr_t)ptr);
}
static inline void *__must_check ERR_PTR(long error)
{
        return (void *)(intptr_t)error;
}
static inline long __must_check PTR_ERR_OR_ZERO(const void *ptr)
{
        return IS_ERR(ptr) ? PTR_ERR(ptr) : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_ERRNO_H_RTL8814 */
