/*
 * RTL8814Native — printk implementation header
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_PRINTK_H_RTL8814
#define _LINUX_PRINTK_H_RTL8814

#include <linux/types.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtl_printk(const char *fmt, ...) __printf(1, 2);
void rtl_vprintk(const char *fmt, va_list args);
void rtl_hexdump(const char *prefix, const void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_PRINTK_H_RTL8814 */
