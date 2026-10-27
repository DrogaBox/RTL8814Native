/*
 * RTL8814Native — workqueue shim for XNU
 *
 * A single dedicated kernel thread ("rtl_system_wq") drains a queue
 * of work_struct items, mirroring schedule_work() on the Linux
 * system workqueue. Vendor driver work items are short and
 * non-sleeping in practice, but sleeping is allowed here (kernel
 * thread context), which is a superset of Linux semantics.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_WORKQUEUE_H_RTL8814
#define _LINUX_WORKQUEUE_H_RTL8814

#include <linux/types.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/timer.h>   /* struct delayed_work embeds timer_list BY
                            * VALUE — this header must be self-contained
                            * (the Driver TUs include it directly, with
                            * no timer.h beforehand) */

#ifdef __cplusplus
extern "C" {
#endif

struct work_struct;          /* fwd decl — the typedef below uses it */

typedef void (*work_func_t)(struct work_struct *work);

struct workqueue_struct;   /* opaque — only the system wq exists */

struct work_struct {
        work_func_t func;
        struct list_head entry;
        volatile int queued;
        void *wq;              /* struct rtl_wq *; NULL = rtl_system_wq */
};

struct delayed_work {
        struct work_struct work;
        struct timer_list timer;
};

#define INIT_WORK(w, f) do {                      \
                (w)->func = (f);                      \
                INIT_LIST_HEAD(&(w)->entry);          \
                (w)->queued = 0;                      \
                (w)->wq = NULL;                       \
        } while (0)

#define INIT_DELAYED_WORK(dw, f) do {             \
                INIT_WORK(&(dw)->work, (f));          \
                INIT_LIST_HEAD(&(dw)->work.entry);    \
                setup_timer(&(dw)->timer,             \
                        rtl_delayed_work_fire,            \
                        (unsigned long)(dw));             \
        } while (0)

void rtl_delayed_work_fire(unsigned long arg);
void rtl_workqueue_init(void);      /* called once from kext start */
void rtl_workqueue_exit(void);      /* drains + stops the thread    */
int  rtl_schedule_work(struct work_struct *w);
/* Route @w to the dedicated "rtl_rx_wq" thread (bulk IN processing) so
 * TX work blocking on the system queue cannot starve RX. */
void rtl_work_bind_rx(struct work_struct *w);

static inline int schedule_work(struct work_struct *w)
{
        return rtl_schedule_work(w);
}

static inline int queue_work(struct workqueue_struct *wq,
                             struct work_struct *w)
{
        (void)wq;   /* single system_wq — see phase notes */
        return rtl_schedule_work(w);
}

static inline int schedule_delayed_work(struct delayed_work *dw,
                                        unsigned long delay_ms)
{
        mod_timer(&dw->timer, rtl_compat_jiffies() + delay_ms);
        return 1;
}

static inline int cancel_delayed_work(struct delayed_work *dw)
{
        return del_timer(&dw->timer);
}

/* Flush: block until both work queues are idle (used at teardown) */
void rtl_flush_scheduled_work(void);
static inline void flush_scheduled_work(void) { rtl_flush_scheduled_work(); }
static inline void flush_workqueue(struct workqueue_struct *wq)
{ (void)wq; rtl_flush_scheduled_work(); }

static inline bool cancel_work_sync(struct work_struct *w)
{
        (void)w;
        rtl_flush_scheduled_work();
        return true;
}

static inline bool cancel_delayed_work_sync(struct delayed_work *dw)
{
        del_timer_sync(&dw->timer);
        return cancel_work_sync(&dw->work);
}

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_WORKQUEUE_H_RTL8814 */
