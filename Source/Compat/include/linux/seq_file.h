/*
 * RTL8814Native — seq_file shim for XNU
 *
 * macOS kernel extensions do not support /proc seq_file.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_SEQ_FILE_H_RTL8814
#define _LINUX_SEQ_FILE_H_RTL8814

#include <linux/types.h>

struct seq_file;
struct seq_operations;
struct file;

static inline int seq_printf(struct seq_file *m, const char *fmt, ...)
{
	(void)m; (void)fmt;
	return 0;
}

#endif /* _LINUX_SEQ_FILE_H_RTL8814 */
