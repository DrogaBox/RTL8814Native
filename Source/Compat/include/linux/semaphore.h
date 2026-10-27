/*
 * RTL8814Native — semaphore + completion shims for XNU
 *
 * Both built on the wait-queue primitive from wait.h.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_SEMAPHORE_H_RTL8814
#define _LINUX_SEMAPHORE_H_RTL8814

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/jiffies.h>    /* rtl_ms_to_abs_deadline */
#include <linux/errno.h>      /* -ETIME                 */

#ifdef __cplusplus
extern "C" {
#endif

struct semaphore {
	wait_queue_head_t wq;
	volatile int count;
};

void rtl_sema_down(struct semaphore *sem);
int  rtl_sema_down_timeout(struct semaphore *sem, unsigned long ms);
int  rtl_sema_down_trylock(struct semaphore *sem);
void rtl_sema_up(struct semaphore *sem);

static inline void sema_init(struct semaphore *sem, int val)
{
	rtl_waitqueue_init(&sem->wq);
	sem->count = val;
}

/* returns 0 on success, -ETIME on timeout, -EINTR is impossible here */
static inline int down_timeout(struct semaphore *sem, unsigned long ms)
{
	return rtl_sema_down_timeout(sem, ms);
}

static inline void down(struct semaphore *sem)
{
	rtl_sema_down(sem);
}

static inline int down_trylock(struct semaphore *sem)
{
	return rtl_sema_down_trylock(sem);
}

static inline int down_interruptible(struct semaphore *sem)
{
	rtl_sema_down(sem);
	return 0;
}

static inline void up(struct semaphore *sem)
{
	rtl_sema_up(sem);
}

static inline void rtl_sema_destroy(struct semaphore *sem)
{
	rtl_waitqueue_destroy(&sem->wq);
}

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_SEMAPHORE_H_RTL8814 */

#ifndef _LINUX_COMPLETION_H_RTL8814
#define _LINUX_COMPLETION_H_RTL8814

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/errno.h>

#ifdef __cplusplus
extern "C" {
#endif

struct completion {
	wait_queue_head_t wq;
	volatile unsigned int done;
};

void rtl_complete(struct completion *c);
void rtl_complete_all(struct completion *c);
void rtl_wait_for_completion(struct completion *c);
unsigned long rtl_wait_for_completion_timeout(struct completion *c, unsigned long ms);

static inline void init_completion(struct completion *c)
{
	rtl_waitqueue_init(&c->wq);
	c->done = 0;
}

static inline void reinit_completion(struct completion *c)
{
	c->done = 0;
}

static inline void complete(struct completion *c)
{
	rtl_complete(c);
}

static inline void complete_all(struct completion *c)
{
	rtl_complete_all(c);
}

static inline void wait_for_completion(struct completion *c)
{
	rtl_wait_for_completion(c);
}

/* returns ms remaining, 0 if timed out — Linux ABI the driver checks */
static inline unsigned long wait_for_completion_timeout(struct completion *c,
                                                        unsigned long ms)
{
	return rtl_wait_for_completion_timeout(c, ms);
}

static inline void rtl_completion_destroy(struct completion *c)
{
	rtl_waitqueue_destroy(&c->wq);
}

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_COMPLETION_H_RTL8814 */
