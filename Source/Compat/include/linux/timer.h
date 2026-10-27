/*
 * RTL8814Native — timer shim for XNU
 *
 * Linux timer_list → XNU thread_call. thread_call_enter_delayed runs
 * the callback on the callout subsystem, which mirrors Linux softirq
 * timer semantics: DO NOT SLEEP in timer callbacks (queue rtw_cmd
 * work instead, exactly like the vendor driver does).
 *
 * HZ is forced to 1000, so expires are milliseconds.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_TIMER_H_RTL8814
#define _LINUX_TIMER_H_RTL8814

#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/delay.h>      /* rtl_msleep in del_timer_sync */
#include <kern/thread_call.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timer_list {
        void (*function)(unsigned long);
        unsigned long data;
        unsigned long expires;            /* absolute, in ms-jiffies */
        thread_call_t call;
        volatile int pending;
        volatile int running;
};

/* thread_call_func_t-shaped entry point implemented in Compat.c —
 * receives the timer_list pointer as param0, no casts needed */
void rtl_timer_callout(thread_call_param_t p0, thread_call_param_t p1);

static inline void setup_timer(struct timer_list *t,
                               void (*fn)(unsigned long),
                               unsigned long data)
{
        t->function = fn;
        t->data = data;
        t->expires = 0;
        t->pending = 0;
        t->running = 0;
        t->call = thread_call_allocate(rtl_timer_callout,
                                       (thread_call_param_t)t);
}

/* legacy name used by older vendor paths — mod_timer lazily allocates
 * the thread_call with the shared callout on first arm */
#define init_timer(t)  do { (t)->call = NULL; (t)->pending = 0; \
                            (t)->running = 0; } while (0)

static inline int mod_timer(struct timer_list *t, unsigned long when_ms)
{
        if (!t->call)
                t->call = thread_call_allocate(rtl_timer_callout,
                                               (thread_call_param_t)t);
        t->expires = when_ms;
        t->pending = 1;
        uint64_t deadline = rtl_ms_to_abs_deadline(
                when_ms > rtl_compat_jiffies() ? when_ms - rtl_compat_jiffies() : 0);
        return thread_call_enter_delayed(t->call, deadline);
}

static inline int mod_timer_pending(struct timer_list *t, unsigned long when_ms)
{
        if (t->pending)
                return 0;
        return mod_timer(t, when_ms);
}

/* Arm relative to now — helper for rtw_set_timer style call sites */
static inline void rtl_mod_timer_rel(struct timer_list *t, unsigned long ms_from_now)
{
        mod_timer(t, rtl_compat_jiffies() + ms_from_now);
}

static inline int del_timer(struct timer_list *t)
{
        t->pending = 0;
        return t->call ? thread_call_cancel(t->call) : 0;
}

static inline int del_timer_sync(struct timer_list *t)
{
        if (!t->call)
                return 0;
        /* Cancel pending; spin until any in-flight callback finished. */
        while (thread_call_cancel(t->call) == FALSE && t->running)
                rtl_msleep(1);
        t->pending = 0;
        return 0;
}

static inline int timer_pending(const struct timer_list *t)
{
        return t->pending;
}

static inline void rtl_timer_destroy(struct timer_list *t)
{
        if (t->call) {
                thread_call_cancel(t->call);
                thread_call_free(t->call);
                t->call = NULL;
        }
}

#define time_after_eq64(a, b) ((s64)(a) - (s64)(b) >= 0)

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_TIMER_H_RTL8814 */
