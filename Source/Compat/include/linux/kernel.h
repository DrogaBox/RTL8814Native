/*
 * RTL8814Native — printk/kernel shim for XNU
 *
 * printk → IOLog (visible in Console.app / `log show` with the
 * kext logging filter). The vendor driver's RTW_INFO/RTW_ERR macros
 * funnel through printk, so this is where all driver logging lands.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_KERNEL_H_RTL8814
#define _LINUX_KERNEL_H_RTL8814

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Yield the CPU — only legal in thread context */
#define schedule()  rtl_msleep(1)

#define printk(fmt, ...)     rtl_printk(fmt, ##__VA_ARGS__)
#define vprintk(fmt, args)   rtl_vprintk(fmt, args)
#define pr_info(fmt, ...)    rtl_printk("info: " fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)     rtl_printk("error: " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)    rtl_printk("warning: " fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)   rtl_printk("debug: " fmt, ##__VA_ARGS__)

#define KERN_INFO
#define KERN_ERR
#define KERN_WARNING
#define KERN_DEBUG
#define KERN_ALERT
#define KERN_NOTICE
#define KERN_EMERG
#define KERN_CONT

#if !defined(__APPLE__)
#define panic(fmt, ...)  do {                                      \
        rtl_printk("PANIC: " fmt, ##__VA_ARGS__);                  \
} while (0)   /* never call XNU panic() from a wifi driver */
#endif

#define WARN_ON(cond)   ({ int __r = (cond); if (__r)          \
        rtl_printk("WARN_ON at %s:%d\n", __func__, __LINE__); 0; })
#define WARN_ON_ONCE(cond) WARN_ON(cond)
#define BUG_ON(cond)    WARN_ON(cond)
#define BUG()           do { } while (0)
#define BUILD_BUG_ON(cond)  typedef char __bbo[(cond) ? -1 : 1] __unused
#define might_sleep()   do { } while (0)
#define uninitialized_var(x) x

/* Round helpers */
#define DIV_ROUND_CLOSEST(x, divisor) (((x) + ((divisor) / 2)) / (divisor))

/* abs helpers — libkern chains may define abs first */
#ifndef abs
#define abs(x)   ((x) < 0 ? -(x) : (x))
#endif
#define abs64(x) ({ s64 __x = (x); (__x < 0) ? -__x : __x; })

#define strict_strtoul(cp, base, res)  ({ *(res) = 0; -EINVAL; })
#define simple_strtoul  strtoul_noop
static inline unsigned long strtoul_noop(const char *a, char **b, int c)
{ (void)a; (void)b; (void)c; return 0; }

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_KERNEL_H_RTL8814 */
