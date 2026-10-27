/* fakesdk IOLib.h — min/max defined BEFORE our shims (the exact
 * redefinition scenario from the MacOSX26.5 build log) */
#ifndef _FAKE_IOLIB_H
#define _FAKE_IOLIB_H
#include <stddef.h>
#include <stdarg.h>
#include <libkern/OSTypes.h>
#define min(a, b) \
        ((a) < (b) ? (a) : (b))
#define max(a, b) \
        ((a) > (b) ? (a) : (b))
void *IOMalloc(size_t size);
void  IOFree(void *address, size_t size);
void  IOLog(const char *fmt, ...);
#endif
