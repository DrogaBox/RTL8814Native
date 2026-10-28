/*
 * RTL8814Native — Compat layer implementation (Linux kernel API → XNU)
 *
 * This is the single C translation unit that implements every shim
 * declared under Source/Compat/include/linux/. Porting rule: vendor
 * code calls a Linux symbol, the symbol resolves here, and here only
 * XNU/libkern/IOKit APIs appear.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <mach/mach_time.h>
#include <mach/mach_types.h>
#include <kern/thread.h>
#include <kern/clock.h>         /* clock_get_uptime, absolutetime conversions */
#include <libkern/libkern.h>       /* IOSleep, IODelay, vsnprintf  */
#include <IOKit/IOLib.h>           /* IOMalloc, IOFree, IOLog      */
#include <IOKit/system.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOInterrupts.h>    /* IOLockSleep family           */

/* Real libkern formatters before the macros below hijack the names */
#undef snprintf
#undef vsnprintf

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/skbuff.h>
#include <linux/random.h>

/* The string shim #defines vsnprintf -> rtl_vsnprintf. The printk and
 * snprintf implementations BELOW must call the REAL libkern vsnprintf,
 * not themselves — undef again AFTER the shims or every log call
 * recurses forever and blows the kernel stack. */
#undef vsnprintf
#undef snprintf

/* wait_result_t fallbacks — the kext SDK exports THREAD_AWAKENED and
 * friends through the mach header chain but keeps some of the list
 * (e.g. THREAD_TERMINATED) kernel-private: guard EACH constant on its
 * own so a partial SDK export never hides a missing one. */
#ifndef THREAD_AWAKENED
#define THREAD_AWAKENED     0
#endif
#ifndef THREAD_TIMED_OUT
#define THREAD_TIMED_OUT    1
#endif
#ifndef THREAD_INTERRUPTED
#define THREAD_INTERRUPTED  2
#endif
#ifndef THREAD_RESTART
#define THREAD_RESTART      3
#endif
#ifndef THREAD_TERMINATED
#define THREAD_TERMINATED   4
#endif
#ifndef THREAD_UNINTERRUPTIBLE
#define THREAD_UNINTERRUPTIBLE 0
#endif
#ifndef THREAD_INTERRUPTIBLE
#define THREAD_INTERRUPTIBLE   1
#endif

/* ======================================================================
 * Lock group (shared by the rwlock shims; IOLock/IOSimpleLock take none)
 * ====================================================================== */
static lck_grp_t *g_rtl_lock_group;
lck_grp_t *rtl_compat_lock_group(void)   /* extern — declared in spinlock.h */
{
        if (!g_rtl_lock_group)
                g_rtl_lock_group = lck_grp_alloc_init("com.DrogaBox.RTL8814Native",
                                                      LCK_GRP_ATTR_NULL);
        return g_rtl_lock_group;
}

void rtl_mutex_init(struct mutex *m)
{
        if (!m->lock)
                m->lock = IOLockAlloc();
}
void rtl_mutex_destroy(struct mutex *m)
{
        if (m->lock) { IOLockFree(m->lock); m->lock = NULL; }
}

void rtl_spin_init(spinlock_t *s)
{
        if (!s->lock)
                s->lock = IORecursiveLockAlloc();
}
void rtl_spin_destroy(spinlock_t *s)
{
        if (s->lock) { IORecursiveLockFree(s->lock); s->lock = NULL; }
}

/* ======================================================================
 * Wait queues
 * ====================================================================== */
void rtl_waitqueue_init(wait_queue_head_t *wq) { wq->lock = NULL; }

void rtl_waitqueue_destroy(wait_queue_head_t *wq)
{
        if (wq->lock) { IOLockFree((IOLock *)wq->lock); wq->lock = NULL; }
}

static IOLock *wq_lock(wait_queue_head_t *wq)
{
        if (!wq->lock)
                wq->lock = IOLockAlloc();
        return (IOLock *)wq->lock;
}

int rtl_wait_on(wait_queue_head_t *wq, uint64_t abs_deadline, int interruptible)
{
        IOLock *l = wq_lock(wq);
        int res;
        IOLockLock(l);
        if (abs_deadline)
                res = IOLockSleepDeadline(l, (void *)wq, abs_deadline,
                                          interruptible ? THREAD_INTERRUPTIBLE
                                                        : THREAD_UNINTERRUPTIBLE);
        else
                res = IOLockSleep(l, (void *)wq,
                                  interruptible ? THREAD_INTERRUPTIBLE
                                        : THREAD_UNINTERRUPTIBLE);
        IOLockUnlock(l);
        if (res == THREAD_TIMED_OUT)   return -ETIME;   /* = 60 */
        if (res == THREAD_INTERRUPTED) return -EINTR;   /* = 4  */
        if (res == THREAD_TERMINATED)  return -EINTR;
        return 0;
}

void rtl_wake_up(wait_queue_head_t *wq, int wake_one)
{
        if (!wq->lock)
                return;   /* nobody ever slept here */
        IOLock *l = (IOLock *)wq->lock;
        IOLockLock(l);
        IOLockWakeup(l, (void *)wq, wake_one ? true : false);
        IOLockUnlock(l);
}

/* ======================================================================
 * Semaphores & Completions (lock-safe against lost wakeups)
 * ====================================================================== */
void rtl_sema_down(struct semaphore *sem)
{
        IOLock *l = wq_lock(&sem->wq);
        IOLockLock(l);
        while (sem->count <= 0)
                IOLockSleep(l, (void *)&sem->wq, THREAD_UNINTERRUPTIBLE);
        sem->count--;
        IOLockUnlock(l);
}

int rtl_sema_down_timeout(struct semaphore *sem, unsigned long ms)
{
        uint64_t deadline = rtl_ms_to_abs_deadline(ms);
        IOLock *l = wq_lock(&sem->wq);
        IOLockLock(l);
        while (sem->count <= 0) {
                int res = IOLockSleepDeadline(l, (void *)&sem->wq, deadline,
                                              THREAD_UNINTERRUPTIBLE);
                if (res != THREAD_AWAKENED) {
                        IOLockUnlock(l);
                        return -ETIME;
                }
        }
        sem->count--;
        IOLockUnlock(l);
        return 0;
}

int rtl_sema_down_trylock(struct semaphore *sem)
{
        IOLock *l = wq_lock(&sem->wq);
        int ret = 1;
        IOLockLock(l);
        if (sem->count > 0) {
                sem->count--;
                ret = 0;
        }
        IOLockUnlock(l);
        return ret;
}

void rtl_sema_up(struct semaphore *sem)
{
        IOLock *l = wq_lock(&sem->wq);
        IOLockLock(l);
        sem->count++;
        IOLockWakeup(l, (void *)&sem->wq, true);
        IOLockUnlock(l);
}

void rtl_complete(struct completion *c)
{
        IOLock *l = wq_lock(&c->wq);
        IOLockLock(l);
        c->done++;
        IOLockWakeup(l, (void *)&c->wq, true);
        IOLockUnlock(l);
}

void rtl_complete_all(struct completion *c)
{
        IOLock *l = wq_lock(&c->wq);
        IOLockLock(l);
        c->done = (unsigned int)-1;
        IOLockWakeup(l, (void *)&c->wq, false);
        IOLockUnlock(l);
}

void rtl_wait_for_completion(struct completion *c)
{
        IOLock *l = wq_lock(&c->wq);
        IOLockLock(l);
        while (!c->done)
                IOLockSleep(l, (void *)&c->wq, THREAD_UNINTERRUPTIBLE);
        if (c->done != (unsigned int)-1)
                c->done--;
        IOLockUnlock(l);
}

unsigned long rtl_wait_for_completion_timeout(struct completion *c,
                                              unsigned long ms)
{
        uint64_t deadline = rtl_ms_to_abs_deadline(ms);
        IOLock *l = wq_lock(&c->wq);
        IOLockLock(l);
        while (!c->done) {
                int res = IOLockSleepDeadline(l, (void *)&c->wq, deadline,
                                              THREAD_UNINTERRUPTIBLE);
                if (res != THREAD_AWAKENED) {
                        IOLockUnlock(l);
                        return 0;
                }
        }
        if (c->done != (unsigned int)-1)
                c->done--;
        IOLockUnlock(l);
        return ms ? ms : 1;
}

/* ======================================================================
 * Time: jiffies (ms) + absolute deadlines
 *
 * The kext clock KPI (kern/clock.h) is used instead of mach_timebase_info
 * (userland-only API, not exported to kexts). clock_get_uptime shares
 * the mach_absolute_time timebase.
 * ====================================================================== */
unsigned long rtl_compat_jiffies(void)
{
        uint64_t now, ns;
        clock_get_uptime(&now);
        absolutetime_to_nanoseconds(now, &ns);
        return (unsigned long)(ns / 1000000ULL);
}

uint64_t rtl_ms_to_abs_deadline(unsigned long ms)
{
        uint64_t now, delta;
        clock_get_uptime(&now);
        nanoseconds_to_absolutetime((uint64_t)ms * 1000000ULL, &delta);
        return now + delta;
}

/* ======================================================================
 * Delays
 * ====================================================================== */
void rtl_msleep(unsigned int msecs)      { IOSleep(msecs); }
void rtl_udelay(unsigned long usecs)     { IODelay((UInt32)usecs); }
void rtl_mdelay(unsigned long msecs)
{
        while (msecs--) IODelay(1000);
}

/* ======================================================================
 * Logging
 * ====================================================================== */
void rtl_vprintk(const char *fmt, va_list args)
{
        char buf[256];
        vsnprintf(buf, sizeof(buf), fmt, args);
        IOLog("RTL8814au: %s", buf);
}

void rtl_printk(const char *fmt, ...)
{
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        IOLog("RTL8814au: %s", buf);
}

void rtl_hexdump(const char *prefix, const void *buf, size_t len)
{
        const u8 *p = buf;
        size_t i;
        IOLog("RTL8814au: %s (%zu bytes)\n", prefix, len);
        for (i = 0; i < len && i < 128; i++) {
                IOLog("%02x ", p[i]);
                if ((i & 15) == 15) IOLog("\n");
        }
        if (len > 128) IOLog("... (%zu more)", len - 128);
        IOLog("\n");
}

/* ======================================================================
 * Memory — size-tracked allocations. IOFree() needs the exact byte
 * count, so rtl_malloc prepends a size header. Preserves 16-byte
 * alignment for SIMD/max_align_t safety on x86_64.
 * ====================================================================== */
struct rtl_alloc_hdr {
        size_t total;
        size_t _pad;
};

void *rtl_malloc(size_t size)
{
        struct rtl_alloc_hdr *h;
        if (size == 0) size = 1;
        size_t total = size + sizeof(struct rtl_alloc_hdr);
        h = (struct rtl_alloc_hdr *)IOMalloc(total);
        if (!h) return NULL;
        h->total = total;
        return (void *)(h + 1);
}

void rtl_free(void *p)
{
        if (!p) return;
        struct rtl_alloc_hdr *h = (struct rtl_alloc_hdr *)p - 1;
        IOFree(h, h->total);
}

struct kmem_cache {
        const char *name;
        size_t size;
};

struct kmem_cache *rtl_kmem_cache_create(const char *name, size_t size)
{
        struct kmem_cache *c = rtl_malloc(sizeof(*c));
        if (c) { c->name = name; c->size = size; }
        return c;
}

void rtl_kmem_cache_destroy(struct kmem_cache *c) { rtl_free(c); }

void *rtl_kmem_cache_alloc(struct kmem_cache *c)
{ return c ? rtl_malloc(c->size) : NULL; }

void rtl_kmem_cache_free(struct kmem_cache *c, void *obj)
{ (void)c; rtl_free(obj); }

/* ======================================================================
 * Random — xorshift64* seeded from the timebase.
 * TODO(phase 4): switch to read_random() (BSD KPI) if exported in the
 * target build, for proper entropy in WPA nonces.
 * ====================================================================== */
static u64 g_prng_state;

static u64 prng_next(void)
{
        u64 x = g_prng_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        g_prng_state = x;
        return x * 2685821657736338717ULL;
}

void rtl_get_random_bytes(void *buf, int len)
{
        static bool seeded;
        if (!seeded) {
                g_prng_state = mach_absolute_time() | 1ULL;
                (void)prng_next();
                seeded = true;
        }
        u8 *p = buf;
        while (len-- > 0)
                *p++ = (u8)(prng_next() >> 32);
}

/*
 * Kernel CSPRNG (bsd/sys/random.h, Mach KPI export). The import is weak so
 * the driver still loads on a kernel that does not export it; kxld then
 * binds the name to NULL and rtl_sae_entropy_fill reports failure, keeping
 * SAE unavailable instead of falling back to the xorshift PRNG.
 */
extern void read_random(void *buffer, u_int numbytes) __attribute__((weak_import));

int rtl_sae_entropy_fill(void *context, uint8_t *output, size_t length)
{
        (void)context;
        /* SAE asks for up to 254 bytes at once (the dummy password identifier),
         * so serve long requests in 64-byte pieces. */
        if (!read_random || !output || length == 0 || length > 1024)
                return -1;
        while (length) {
                size_t n = length > 64 ? 64 : length;

                read_random(output, (u_int)n);
                output += n;
                length -= n;
        }
        return 0;
}

/* ======================================================================
 * String helpers missing from libkern (no ctype in kernel: ASCII-only)
 * ====================================================================== */
static inline int rtl_tolower(int c)
{
        return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int rtl_strncasecmp(const char *s1, const char *s2, size_t n)
{
        if (n == 0) return 0;
        while (n-- != 0 && rtl_tolower(*s1) == rtl_tolower(*s2)) {
                if (*s1 == '\0' || n == 0) break;
                s1++; s2++;
        }
        return (unsigned char)rtl_tolower(*s1) - (unsigned char)rtl_tolower(*s2);
}

size_t rtl_strspn(const char *s, const char *accept)
{
        size_t count = 0;
        const char *a;
        for (; *s; s++) {
                for (a = accept; *a; a++)
                        if (*s == *a) break;
                if (!*a) break;
                count++;
        }
        return count;
}

char *rtl_strpbrk(const char *cs, const char *ct)
{
        for (; *cs; cs++)
                for (const char *p = ct; *p; p++)
                        if (*cs == *p) return (char *)cs;
        return NULL;
}

char *rtl_strsep(char **s, const char *ct)
{
        char *sbegin = *s, *end;
        if (!sbegin) return NULL;
        end = rtl_strpbrk(sbegin, ct);
        if (end) { *end++ = '\0'; *s = end; }
        else     { *s = NULL; }
        return sbegin;
}

int rtl_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
        return vsnprintf(buf, size, fmt, args);
}

int rtl_snprintf(char *buf, size_t size, const char *fmt, ...)
{
        va_list ap;
        int r;
        va_start(ap, fmt);
        r = vsnprintf(buf, size, fmt, ap);
        va_end(ap);
        return r;
}

/* ======================================================================
 * Timers
 *
 * thread_call invokes the callback as fn(param0, param1). We register
 * the TIMER POINTER as param0 and unwrap it here — no function-pointer
 * casts, no reliance on the extra-register-argument ABI.
 * ====================================================================== */
void rtl_timer_callout(thread_call_param_t p0, thread_call_param_t p1)
{
        struct timer_list *t = (struct timer_list *)p0;
        (void)p1;
        if (!t)
                return;
        t->pending = 0;
        t->running = 1;
        if (t->function)
                t->function(t->data);
        t->running = 0;
}

/* ======================================================================
 * Workqueues — the system queue plus a dedicated RX queue, each drained
 * by its own kernel thread. Work runs on the system queue unless it was
 * bound to the RX queue with rtl_work_bind_rx().
 * ====================================================================== */
struct rtl_wq {
        struct list_head pending;
        wait_queue_head_t wq;
        struct task_struct *thread;
        volatile int running;
        volatile int stopping;
};

static struct rtl_wq g_system_wq;
static struct rtl_wq g_rx_wq;
/* SAE runs P-256 arithmetic that takes far longer than anything else the
 * driver queues, so it gets a queue of its own. rtl_flush_scheduled_work()
 * deliberately does not wait for it: SAE work may itself end in a join
 * result that flushes the other queues. */
static struct rtl_wq g_sae_wq;

static int rtl_wq_thread(void *arg)
{
        struct rtl_wq *wq = arg;
        struct work_struct *w;
        IOLock *l = wq_lock(&wq->wq);

        IOLockLock(l);
        while (!wq->stopping) {
                while (!list_empty(&wq->pending)) {
                        w = list_first_entry(&wq->pending, struct work_struct, entry);
                        list_del(&w->entry);
                        INIT_LIST_HEAD(&w->entry);
                        w->queued = 0;
                        wq->running = 1;
                        IOLockUnlock(l);
                        if (w->func)
                                w->func(w);
                        IOLockLock(l);
                        wq->running = 0;
                }
                if (!wq->stopping)
                        IOLockSleep(l, (void *)&wq->wq, THREAD_UNINTERRUPTIBLE);
        }
        IOLockUnlock(l);
        return 0;
}

static void rtl_wq_start(struct rtl_wq *wq, const char *name)
{
        /* Never re-create a live queue: its drain thread caches the queue
         * lock, and a second thread with a new lock would pop the same list
         * unsynchronized. */
        if (wq->thread && !wq->thread->finished) {
                rtl_printk("RTL8814Native: %s already running, not restarted\n", name);
                return;
        }
        INIT_LIST_HEAD(&wq->pending);
        rtl_waitqueue_init(&wq->wq);
        (void)wq_lock(&wq->wq);
        wq->stopping = 0;
        wq->running = 0;
        wq->thread = rtl_kthread_create(rtl_wq_thread, wq, name);
}

static void rtl_wq_stop(struct rtl_wq *wq)
{
        IOLock *l = wq_lock(&wq->wq);
        IOLockLock(l);
        wq->stopping = 1;
        /* Drop queued work: it belongs to the dying instance, whose memory
         * is being or has been freed. The threads are kext-lifetime and the
         * queue is re-created on the next start, so an entry left here would
         * be executed later against freed memory (rtl_wq_thread:list_del
         * panic on a sleep/wake that re-enumerated the device). */
        while (!list_empty(&wq->pending)) {
                struct work_struct *w = list_first_entry(&wq->pending,
                                                         struct work_struct,
                                                         entry);
                list_del(&w->entry);
        }
        IOLockWakeup(l, (void *)&wq->wq, false);
        IOLockUnlock(l);
        /* Wait until the drain thread actually finished its function */
        if (wq->thread) {
                while (!wq->thread->finished)
                        rtl_msleep(1);
        }
        INIT_LIST_HEAD(&wq->pending);
        rtl_waitqueue_destroy(&wq->wq);
        if (wq->thread)
                rtl_kthread_destroy(wq->thread);
        wq->thread = NULL;
}

void rtl_workqueue_init(void)
{
        rtl_wq_start(&g_system_wq, "rtl_system_wq");
        rtl_wq_start(&g_rx_wq, "rtl_rx_wq");
        rtl_wq_start(&g_sae_wq, "rtl_sae_wq");
}

void rtl_workqueue_exit(void)
{
        rtl_wq_stop(&g_sae_wq);
        rtl_wq_stop(&g_rx_wq);
        rtl_wq_stop(&g_system_wq);
}

void rtl_work_bind_rx(struct work_struct *w)
{
        if (w)
                w->wq = &g_rx_wq;
}

void rtl_work_bind_sae(struct work_struct *w)
{
        if (w)
                w->wq = &g_sae_wq;
}

int rtl_schedule_work(struct work_struct *w)
{
        if (!w || !w->func) return 0;
        struct rtl_wq *wq = w->wq ? (struct rtl_wq *)w->wq : &g_system_wq;
        if (wq->stopping) return 0;
        IOLock *l = wq_lock(&wq->wq);
        IOLockLock(l);
        if (w->queued) {
                IOLockUnlock(l);
                return 0;
        }
        w->queued = 1;
        list_add_tail(&w->entry, &wq->pending);
        IOLockWakeup(l, (void *)&wq->wq, false);
        IOLockUnlock(l);
        return 1;
}

void rtl_flush_scheduled_work(void)
{
        /* Read both queues under their locks: the drain thread dequeues and
         * sets running=1 inside the same critical section, so an unlocked
         * read could see "empty + idle" while a work is about to execute. */
        for (;;) {
                int busy;
                IOLock *ls = wq_lock(&g_system_wq.wq);
                IOLock *lr = wq_lock(&g_rx_wq.wq);
                IOLockLock(ls);
                IOLockLock(lr);
                busy = !list_empty(&g_system_wq.pending) || g_system_wq.running ||
                       !list_empty(&g_rx_wq.pending) || g_rx_wq.running;
                IOLockUnlock(lr);
                IOLockUnlock(ls);
                if (!busy)
                        return;
                rtl_msleep(1);
        }
}

void rtl_delayed_work_fire(unsigned long arg)
{
        struct delayed_work *dw = (struct delayed_work *)arg;
        rtl_schedule_work(&dw->work);
}

void rtl_tasklet_work_fn(struct work_struct *w)
{
        struct tasklet_struct *t = container_of(w, struct tasklet_struct, work);
        if (t && t->func)
                t->func(t->data);
}

/* ======================================================================
 * Kernel threads
 * ====================================================================== */
#define RTL_MAX_KTHREADS 16
static struct task_struct *g_active_tasks[RTL_MAX_KTHREADS];

void rtl_kthread_trampoline(void *arg)
{
        struct rtl_kthread_args *a = arg;
        int (*fn)(void *) = a->threadfn;
        void *data = a->data;
        struct task_struct *t = a->task;

        t->thread = current_thread();
        for (int i = 0; i < RTL_MAX_KTHREADS; i++) {
                if (!g_active_tasks[i]) {
                        g_active_tasks[i] = t;
                        break;
                }
        }

        /* Linux kthreads run detached; free the args blob here */
        rtl_free(a);
        fn(data);

        for (int i = 0; i < RTL_MAX_KTHREADS; i++) {
                if (g_active_tasks[i] == t) {
                        g_active_tasks[i] = NULL;
                        break;
                }
        }
        rtl_wake_up(&t->wq, 0);
        __sync_synchronize();
        t->finished = 1;
        thread_terminate(current_thread());
        /* NOT REACHED */
}

struct task_struct *rtl_kthread_create(int (*fn)(void *), void *data,
                                       const char *name)
{
        kern_return_t kr;
        struct task_struct *t;
        struct rtl_kthread_args *a;

        t = kzalloc(sizeof(*t), GFP_KERNEL);
        a = kzalloc(sizeof(*a), GFP_KERNEL);
        if (!t || !a) goto fail;

        rtl_waitqueue_init(&t->wq);
        a->threadfn = fn;
        a->data = data;
        a->task = t;

        /* kernel_thread_start(continuation, parameter, new_thread) — 3 args
         * in the kext SDK. The established kext idiom is to cast a
         * fn(void*) continuation: XNU's call_continuation passes our
         * `arg` in the first argument register on both x86_64 and arm64. */
        kr = kernel_thread_start((thread_continue_t)rtl_kthread_trampoline,
                                 a, &t->thread);
        if (kr != KERN_SUCCESS)
                goto fail;

        rtl_printk("kthread %s started (thread %p)\n", name, t->thread);
        return t;

fail:
        rtl_free(t);
        rtl_free(a);
        return NULL;
}

struct task_struct *rtl_current_task(void)
{
        thread_t cur = current_thread();
        for (int i = 0; i < RTL_MAX_KTHREADS; i++) {
                struct task_struct *t = g_active_tasks[i];
                if (t && t->thread == cur)
                        return t;
        }
        return NULL;
}

/* ======================================================================
 * sk_buff
 * ====================================================================== */
struct sk_buff *rtl_alloc_skb(unsigned int size)
{
        struct sk_buff *skb = kzalloc(sizeof(*skb), GFP_KERNEL);
        if (!skb) return NULL;

        skb->head = kmalloc(size + SKB_HEADROOM, GFP_KERNEL);
        if (!skb->head) { rtl_free(skb); return NULL; }

        skb->data = skb->head;
        skb->tail = skb->head;
        skb->end  = skb->head + size + SKB_HEADROOM;
        skb->len  = 0;
        skb->truesize = size + SKB_HEADROOM;
        atomic_set(&skb->users, 1);
        INIT_LIST_HEAD(&skb->list);
        return skb;
}

void rtl_kfree_skb(struct sk_buff *skb)
{
        if (!skb) return;
        if (atomic_dec_and_test(&skb->users) || atomic_read(&skb->users) <= 0) {
                /* detach from any queue before free */
                if (skb->list.next && skb->list.prev) {
                        /* list_del if linked (poison check is skipped for speed) */
                        if (skb->list.next != (void *)0xDEADBEEF)
                                list_del(&skb->list);
                }
                rtl_free(skb->head);
                rtl_free(skb);
        }
}
