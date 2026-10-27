/* vmalloc.h — kernel memory is always wired in kexts; alias to slab */
#ifndef _LINUX_VMALLOC_H_RTL8814
#define _LINUX_VMALLOC_H_RTL8814
#include <linux/slab.h>
#define vmalloc(size)          kmalloc((size), GFP_KERNEL)
#define vzalloc(size)          kzalloc((size), GFP_KERNEL)
#define vfree(p)               kfree(p)
#endif
