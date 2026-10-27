/*
 * RTL8814Native — jiffies + delay shim for XNU
 *
 * jiffies are emulated with millisecond ticks derived from the kext
 * clock KPI (clock_get_uptime — kern/clock.h, same timebase as
 * mach_absolute_time). Sleeping uses IOSleep, busy-wait delays
 * use IODelay (microseconds) — never call mdelay with large values
 * in non-atomic context (same rule as Linux).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_JIFFIES_H_RTL8814
#define _LINUX_JIFFIES_H_RTL8814

#include <linux/types.h>
#include <mach/mach_time.h>
#include <libkern/OSTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long jiffies_t;
#define jiffies  rtl_compat_jiffies()   /* NOTE: () — not (void)! */

/*
 * One "jiffy" == 1 ms. The vendor driver's HZ-dependent code is
 * compiled with HZ defined in include/autoconf.h; we force HZ=1000
 * in the build so msecs_to_jiffies is identity.
 */
#define HZ 1000UL
#define MSEC_PER_SEC   1000L
#define MAX_JIFFY_OFFSET ((unsigned long)~0UL >> 1)

unsigned long rtl_compat_jiffies(void);

#define time_after(a, b)       ((long)(b) - (long)(a) < 0)
#define time_before(a, b)      time_after(b, a)
#define time_after_eq(a, b)    ((long)(a) - (long)(b) >= 0)
#define time_before_eq(a, b)   time_after_eq(b, a)
#define time_in_range(a, b, c) (time_after_eq(a, b) && time_before_eq(a, c))

#define msecs_to_jiffies(m)  ((unsigned long)(m))
#define jiffies_to_msecs(j)  ((unsigned int)(j))
#define jiffies_to_usecs(j)  ((unsigned int)(j) * 1000)

/* Deadline helper shared by the wait/timer shims */
uint64_t rtl_ms_to_abs_deadline(unsigned long ms);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_JIFFIES_H_RTL8814 */
