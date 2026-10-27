/*
 * RTL8814Native — wait queue shim for XNU
 *
 * Linux wait_queue_head_t over an IOLock with IOLockSleep /
 * IOLockSleepDeadline / IOLockWakeup. The wait queue object itself
 * is the sleep event, matching wake_up() semantics.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_WAIT_H_RTL8814
#define _LINUX_WAIT_H_RTL8814

#include <linux/types.h>
#include <linux/jiffies.h>    /* rtl_ms_to_abs_deadline          */
#include <linux/errno.h>      /* -ERESTARTSYS                    */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wait_queue_head {
        void *lock;          /* IOLock* — created lazily */
} wait_queue_head_t;

void rtl_waitqueue_init(wait_queue_head_t *wq);
void rtl_waitqueue_destroy(wait_queue_head_t *wq);

#define init_waitqueue_head(wq)   rtl_waitqueue_init(wq)
/* Positional init (not designated): single-member struct, and the
 * positional form is warning-free in strict -std=c++17 too. */
#define DECLARE_WAIT_QUEUE_HEAD(name) \
        wait_queue_head_t name = { NULL }

/* Core sleep: returns 0 (awake), -ETIME (timeout), -EINTR (interrupted) */
int rtl_wait_on(wait_queue_head_t *wq, uint64_t abs_deadline, int interruptible);
void rtl_wake_up(wait_queue_head_t *wq, int wake_one);

#define wake_up(wq)              rtl_wake_up((wq), 0)
#define wake_up_all(wq)          rtl_wake_up((wq), 0)
#define wake_up_interruptible(wq) rtl_wake_up((wq), 0)
#define wake_up_interruptible_all(wq) rtl_wake_up((wq), 0)

#define THREAD_INTR 1  /* 1 == interruptible, 0 == uninterruptible */

/*
 * wait_event family. The condition is re-checked after every wakeup;
 * sleeps happen under the queue lock so a wake racing the sleep is
 * never lost (classic lost-wakeup guard).
 */
#define __rtl_wait_event(wq, condition, deadline, intr, ret) do {     \
                while (!(condition)) {                                    \
                        (ret) = rtl_wait_on(&(wq), (deadline), (intr));       \
                        if ((ret) != 0)                                       \
                                break; /* timed out or interrupted */            \
                }                                                        \
        } while (0)

#define wait_event(wq, condition)                                        \
        do { int __r = 0; __rtl_wait_event(wq, condition, 0, 0, __r); } while (0)

#define wait_event_timeout(wq, condition, timeout_ms)                    \
        ({ int __r = 0; uint64_t __d = rtl_ms_to_abs_deadline(timeout_ms);  \
           __rtl_wait_event(wq, condition, __d, 0, __r);                     \
           ((condition) ? (int)(timeout_ms) : 0); })

#define wait_event_interruptible(wq, condition)                          \
        ({ int __r = 0; __rtl_wait_event(wq, condition, 0, THREAD_INTR, __r); \
           ((condition) ? 0 : -ERESTARTSYS); })

#define wait_event_interruptible_timeout(wq, condition, timeout_ms)      \
        ({ int __r = 0; uint64_t __d = rtl_ms_to_abs_deadline(timeout_ms);   \
           __rtl_wait_event(wq, condition, __d, THREAD_INTR, __r);           \
           ((condition) ? (int)(timeout_ms) : 0); })

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_WAIT_H_RTL8814 */
