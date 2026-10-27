/*
 * sched.h — scheduler bits. `current` and signal checks are stubs:
 * the vendor paths we port run in kernel threads we own, and no
 * signal delivery applies to XNU kernel threads.
 */
#ifndef _LINUX_SCHED_H_RTL8814
#define _LINUX_SCHED_H_RTL8814

#include <linux/types.h>
#include <linux/kthread.h>

#define TASK_COMM_LEN 32

struct task_struct *rtl_current_task(void);
#define current  rtl_current_task()

#define signal_pending(t)        (0)
#define fatal_signal_pending(t)  (0)
#define freeze_wakeup()          do { } while (0)

#ifndef SIGTERM
#define SIGTERM 15
#endif
#define allow_signal(s)          do { (void)(s); } while (0)
#define disallow_signal(s)       do { (void)(s); } while (0)

#define set_current_state(s)     do { (void)(s); } while (0)
#define __set_current_state(s)   do { (void)(s); } while (0)
#define flush_signals(t)         do { (void)(t); } while (0)
#define dump_stack()             do { } while (0)

#ifndef MAX_SCHEDULE_TIMEOUT
#define MAX_SCHEDULE_TIMEOUT     ((long)(~0UL>>1))
#endif

#endif
