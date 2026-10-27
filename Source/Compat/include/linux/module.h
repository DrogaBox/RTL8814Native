/* module.h — kexts have no LKM metadata; all no-ops */
#ifndef _LINUX_MODULE_H_RTL8814
#define _LINUX_MODULE_H_RTL8814

#include <linux/types.h>

#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_VERSION(x)
#define MODULE_ALIAS(x)
#define MODULE_SUPPORTED_DEVICE(x)
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_SYMBOL_GPL_FUTURE(x)
#define THIS_MODULE   NULL
#define MODULE_INFO(tag, info)

/* module params exist as plain globals in the vendor tree; the
 * registration machinery is not needed. */
#define module_param(name, type, perm) \
	static int __rtl_unused_##name __unused
#define module_param_named(name, value, type, perm) \
	static int __rtl_unused_##name __unused
#define module_param_string(name, string, size, perm) \
	static int __rtl_unused_##name __unused
#define MODULE_PARM_DESC(name, desc)

#endif
