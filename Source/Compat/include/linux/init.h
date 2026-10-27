/*
 * init.h — module init/exit are no-ops: the kext lifecycle is driven
 * by IOKit start/stop. The macro keeps the function referenced so it
 * is not dead-stripped (the glue calls it explicitly per phase).
 */
#ifndef _LINUX_INIT_H_RTL8814
#define _LINUX_INIT_H_RTL8814
/* typeof → __typeof__: plain typeof is rejected by clang/gcc in
 * strict -std=c++17, and this header is reachable from Driver TUs. */
#define module_init(x)     static __typeof__(x) *__rtl_module_init_ref __unused = (x)
#define module_exit(x)     static __typeof__(x) *__rtl_module_exit_ref __unused = (x)
#define early_initcall(x)  typedef int __rtl_early_dummy __unused
#define __initdata
#define __exitdata
#define __ref
#define __refdata
#endif
