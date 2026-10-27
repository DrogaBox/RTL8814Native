/*
 * RTL8814Native — delay shim for XNU
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_DELAY_H_RTL8814
#define _LINUX_DELAY_H_RTL8814

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sleep (schedulable) */
void rtl_msleep(unsigned int msecs);
#define msleep(msecs)        rtl_msleep(msecs)
#define ssleep(secs)         rtl_msleep((secs) * 1000)

/* Busy-wait — only for short delays, same constraints as Linux */
void rtl_udelay(unsigned long usecs);
void rtl_mdelay(unsigned long msecs);
#define udelay(usecs)        rtl_udelay(usecs)
#define mdelay(msecs)        rtl_mdelay(msecs)
#define usleep_range(lo, hi) rtl_udelay(lo)

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_DELAY_H_RTL8814 */
