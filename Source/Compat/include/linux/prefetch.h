/* prefetch — no-ops that map to compiler hints */
#ifndef _LINUX_PREFETCH_H_RTL8814
#define _LINUX_PREFETCH_H_RTL8814
#define prefetch(x)       __builtin_prefetch((x), 0, 3)
#define prefetchw(x)      __builtin_prefetch((x), 1, 3)
#endif
