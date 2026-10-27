/*
 * RTL8814Native — spinlock shim for XNU
 *
 * Linux spinlock_t → IOKit IOSimpleLock (the stable kext KPI spin).
 * On x86_64 XNU, IOSimpleLockLock disables interrupts on the local
 * CPU, which matches the spin_lock_irqsave semantics the driver
 * relies on. Why not lck_spin_t: lck_spin_try_lock is KERNEL_PRIVATE
 * (absent from the kext SDK headers) — IOSimpleLockTryLock is the
 * exported try-acquire API.
 *
 * IMPORTANT (porting note): spinlock critical sections in the vendor
 * driver are short and never sleep — the same rule applies on XNU.
 * If a spin_is_locked() assertion fires on macOS, the section is
 * too long and must be converted to a mutex during that phase.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_SPINLOCK_H_RTL8814
#define _LINUX_SPINLOCK_H_RTL8814

#include <linux/types.h>
#include <IOKit/IOLocks.h>       /* XNU KPI — IOSimpleLock: spin + try */
#include <IOKit/IOInterrupts.h>
#include <kern/locks.h>          /* lck_rw_t + lck_grp_t (rwlock_t below) */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
        IORecursiveLock *lock;
} spinlock_t;

extern lck_grp_t *rtl_compat_lock_group(void);

void rtl_spin_init(spinlock_t *s);
void rtl_spin_destroy(spinlock_t *s);

static inline void spin_lock(spinlock_t *s)
{
        if (__builtin_expect(!s->lock, 0)) rtl_spin_init(s);
        IORecursiveLockLock(s->lock);
}

static inline void spin_unlock(spinlock_t *s)
{
        if (s->lock)
                IORecursiveLockUnlock(s->lock);
}

static inline void spin_lock_bh(spinlock_t *s)  { spin_lock(s); }
static inline void spin_unlock_bh(spinlock_t *s) { spin_unlock(s); }

typedef unsigned long rtl_flags_t;

#define spin_lock_irqsave(s, flags) do { \
        (void)(flags); \
        spin_lock(s); \
} while (0)

#define spin_unlock_irqrestore(s, flags) do { \
        (void)(flags); \
        spin_unlock(s); \
} while (0)

static inline void spin_lock_irq(spinlock_t *s) { spin_lock(s); }
static inline void spin_unlock_irq(spinlock_t *s) { spin_unlock(s); }

static inline int spin_is_locked(spinlock_t *s)
{
        if (!s->lock)
                return 0;
        if (IORecursiveLockHaveLock(s->lock))
                return 1;
        if (IORecursiveLockTryLock(s->lock)) {
                IORecursiveLockUnlock(s->lock);
                return 0;
        }
        return 1;
}

/* returns 1 when acquired — IORecursiveLockTryLock is true on success */
static inline int spin_trylock(spinlock_t *s)
{
        if (__builtin_expect(!s->lock, 0)) rtl_spin_init(s);
        return IORecursiveLockTryLock(s->lock);
}

/* read/write locks (used in a few debug paths) */
typedef struct {
        lck_rw_t *lock;
} rwlock_t;

static inline void rwlock_init(rwlock_t *r)
{
        r->lock = lck_rw_alloc_init(rtl_compat_lock_group(), LCK_ATTR_NULL);
}
static inline void read_lock(rwlock_t *r)      { lck_rw_lock_shared(r->lock); }
static inline void read_unlock(rwlock_t *r)    { lck_rw_unlock_shared(r->lock); }
static inline void write_lock(rwlock_t *r)     { lck_rw_lock_exclusive(r->lock); }
static inline void write_unlock(rwlock_t *r)   { lck_rw_unlock_exclusive(r->lock); }

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_SPINLOCK_H_RTL8814 */
