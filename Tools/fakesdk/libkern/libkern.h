/* fakesdk libkern.h — pulls the limits chain like the real one */
#ifndef _FAKE_LIBKERN_H
#define _FAKE_LIBKERN_H
#include <stddef.h>
#include <stdarg.h>
#include <i386/limits.h>
#include <libkern/OSTypes.h>
int  vsnprintf(char *, size_t, const char *, va_list);
int  snprintf(char *, size_t, const char *, ...);
void IOSleep(unsigned int ms);
void IODelay(unsigned int us);
/* fls/ffs live here in the Kernel SDK — same 1-based semantics, 0 for 0 */
int  fls(unsigned int);
int  ffs(unsigned int);
#endif
