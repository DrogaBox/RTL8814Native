/* fakesdk sys/errno.h — DARWIN values (EAGAIN=35 etc. differ from Linux;
 * proves the #undef+redefine strategy in linux/errno.h) */
#ifndef _FAKE_SYS_ERRNO_H
#define _FAKE_SYS_ERRNO_H
#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define EAGAIN 35
#define ENOMEM 12
#define EBUSY 16
#define EEXIST 17
#define EINVAL 22
#define ENFILE 23
#define ENOSPC 28
#define EPIPE 32
#define ERANGE 34
#define ENOMSG 35
#define ENODEV 19
#define EOPNOTSUPP 102
#define ENETDOWN 50
#define ETIMEDOUT 60
#define EINPROGRESS 36
#define EALREADY 37
#define ENOTCONN 57
#define ECONNREFUSED 61
#endif
