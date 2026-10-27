/*
 * RTL8814Native — kthread shim for XNU
 *
 * The vendor driver spawns four threads via kthread_run():
 *   RTW_XMIT_THREAD, RTW_RECV_THREAD, RTW_CMD_THREAD, RTW_EVENT_THREAD
 * Each thread function loops internally and checks
 * rtw_thread_wait_stop()/kthread_should_stop() — the shim provides
 * the stop flag and a private waitqueue so kthread_stop() can wake
 * and join it.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_KTHREAD_H_RTL8814
#define _LINUX_KTHREAD_H_RTL8814

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <kern/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task_struct {
        thread_t thread;
        volatile int should_stop;
        volatile int finished;
        wait_queue_head_t wq;
};

/* Trampoline that adapts kernel_thread_start's (void*) continuation
 * to the kthread (fn(data)) shape. */
struct rtl_kthread_args {
        int (*threadfn)(void *data);
        void *data;
        struct task_struct *task;
};

struct task_struct *rtl_kthread_create(int (*fn)(void *), void *data,
                                        const char *name);
void rtl_kthread_trampoline(void *arg);

#define kthread_run(fn, data, namefmt, ...) \
        rtl_kthread_create((fn), (data), (namefmt))

struct task_struct *rtl_current_task(void);

/* Frees the task_struct. Callers own their last reference: the trampoline
 * removes the task from the active table and touches nothing after
 * finished = 1, so kthread_stop() may destroy right after joining. */
static inline void rtl_kthread_destroy(struct task_struct *t)
{
        rtl_waitqueue_destroy(&t->wq);
        rtl_free(t);
}

static inline int kthread_should_stop(void)
{
        struct task_struct *t = rtl_current_task();
        return t ? t->should_stop : 0;
}

static inline int kthread_stop(struct task_struct *t)
{
        if (!t)
                return 0;
        t->should_stop = 1;
        rtl_wake_up(&t->wq, 0);
        while (!t->finished)
                rtl_wait_on(&t->wq, 0, 0);
        rtl_kthread_destroy(t);
        return 0;
}

#define set_task_state(t, s)  do { } while (0)
#define TASK_RUNNING          0
#define TASK_INTERRUPTIBLE    1
#define TASK_UNINTERRUPTIBLE  2

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_KTHREAD_H_RTL8814 */
