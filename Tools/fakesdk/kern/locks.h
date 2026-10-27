/* fakesdk kern/locks.h — XNU lock KPI (pure C, no libkern/locks.h!)
 *
 * SIGNATURE-CRITICAL: the kext SDK does NOT declare the try-acquire
 * variants lck_mtx_try_lock / lck_spin_try_lock (KERNEL_PRIVATE) —
 * only the rwlock family is fully exported. Mirror that here. */
#ifndef _FAKE_KERN_LOCKS_H
#define _FAKE_KERN_LOCKS_H
#include <stdint.h>
struct _lck_mtx_t_; typedef struct _lck_mtx_t_ *lck_mtx_t;
struct _lck_spin_t_; typedef struct _lck_spin_t_ *lck_spin_t;
struct _lck_rw_t_;   typedef struct _lck_rw_t_ *lck_rw_t;
struct _lck_grp_t_;  typedef struct _lck_grp_t_ *lck_grp_t;
struct _lck_attr_t_; typedef struct _lck_attr_t_ *lck_attr_t;
#define LCK_GRP_ATTR_NULL ((lck_grp_attr_t *)0)
typedef struct _lck_grp_attr_t_ *lck_grp_attr_t;
#define LCK_ATTR_NULL ((lck_attr_t *)0)
lck_grp_t  *lck_grp_alloc_init(const char *, lck_grp_attr_t *);
void        lck_grp_free(lck_grp_t *);
lck_mtx_t  *lck_mtx_alloc_init(lck_grp_t *, lck_attr_t *);
void        lck_mtx_free(lck_mtx_t *, lck_grp_t *);
void        lck_mtx_lock(lck_mtx_t *);
void        lck_mtx_unlock(lck_mtx_t *);
/* NO lck_mtx_try_lock — KERNEL_PRIVATE, absent from kext SDK */
lck_spin_t *lck_spin_alloc_init(lck_grp_t *, lck_attr_t *);
void        lck_spin_free(lck_spin_t *, lck_grp_t *);
void        lck_spin_lock(lck_spin_t *);
void        lck_spin_unlock(lck_spin_t *);
/* NO lck_spin_try_lock — KERNEL_PRIVATE, absent from kext SDK */
lck_rw_t   *lck_rw_alloc_init(lck_grp_t *, lck_attr_t *);
void        lck_rw_free(lck_rw_t *, lck_grp_t *);
void        lck_rw_lock_shared(lck_rw_t *);
void        lck_rw_unlock_shared(lck_rw_t *);
void        lck_rw_lock_exclusive(lck_rw_t *);
void        lck_rw_unlock_exclusive(lck_rw_t *);
typedef enum {
        LCK_RW_TYPE_SHARED    = 1,
        LCK_RW_TYPE_EXCLUSIVE = 2,
} lck_rw_type_t;
#define LCK_RW_TYPE_WRITE LCK_RW_TYPE_EXCLUSIVE
#define LCK_RW_TYPE_READ  LCK_RW_TYPE_SHARED
int lck_rw_try_lock(lck_rw_t *, lck_rw_type_t);
#endif
