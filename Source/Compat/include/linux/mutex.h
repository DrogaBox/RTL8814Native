/*
 * RTL8814Native — mutex shim for XNU
 *
 * Linux struct mutex → IOKit IOLock (the stable kext KPI mutex).
 * Why not lck_mtx_t: the try-acquire variants (lck_mtx_try_lock /
 * lck_spin_try_lock) are KERNEL_PRIVATE and are NOT declared in the
 * kext-facing SDK headers. The IOKit lock family is the exported API
 * with full semantics: IOLockLock/Unlock, IOLockTryLock, plus sleep
 * support (IOLockSleep — the same primitive our wait queues use).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_MUTEX_H_RTL8814
#define _LINUX_MUTEX_H_RTL8814

#include <linux/types.h>
#include <IOKit/IOLocks.h>   /* XNU KPI — IOLock: lock, try-lock, sleep */

#ifdef __cplusplus
extern "C" {
#endif

struct mutex {
        IOLock *lock;
};

/* Positional init: single-member struct, warning-free in strict C++17 */
#define DEFINE_MUTEX(name) \
        struct mutex name = { NULL }

void rtl_mutex_init(struct mutex *m);
void rtl_mutex_destroy(struct mutex *m);

static inline void mutex_lock(struct mutex *m)
{
        if (__builtin_expect(!m->lock, 0)) rtl_mutex_init(m);
        IOLockLock(m->lock);
}

static inline void mutex_unlock(struct mutex *m)
{
        IOLockUnlock(m->lock);
}

/* returns 1 when acquired — IOLockTryLock is boolean_t true on success */
static inline int mutex_trylock(struct mutex *m)
{
        if (__builtin_expect(!m->lock, 0)) rtl_mutex_init(m);
        return IOLockTryLock(m->lock);
}

static inline int mutex_is_locked(struct mutex *m)
{
        if (!m->lock)
                return 0;
        if (IOLockTryLock(m->lock)) {
                IOLockUnlock(m->lock);
                return 0;
        }
        return 1;
}

/* rtw never sleeps uninterruptibly on mutexes in paths we port first */
static inline int mutex_lock_interruptible(struct mutex *m)
{
        mutex_lock(m);
        return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_MUTEX_H_RTL8814 */
