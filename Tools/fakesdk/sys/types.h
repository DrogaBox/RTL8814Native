/* fakesdk sys/types.h — Darwin's mode_t is UNSIGNED SHORT (the crash) */
#ifndef _FAKE_SYS_TYPES_H
#define _FAKE_SYS_TYPES_H
#include <stdint.h>
#include <stddef.h>
typedef unsigned char   u_char;
typedef unsigned short  u_short;
typedef unsigned int    u_int;
typedef unsigned long   u_long;
typedef int             ssize_t;
typedef long long       off_t;
typedef unsigned short  __darwin_mode_t;
typedef __darwin_mode_t mode_t;      /* ← the exact fatal collision */
typedef int             __darwin_pid_t;
typedef __darwin_pid_t  pid_t;
#endif
