/*
 * test_all.c — fakesdk compile harness for the RTL8814Native Compat layer.
 *
 * Includes every linux shim header in a vendor-like order and exercises
 * the macros/static inlines so their bodies are actually compiled.
 * Built with -nostdinc against scripts/fakesdk, which reproduces the
 * MacOSX26.5 Kernel SDK collision points exactly:
 *
 *   - sys/types.h  : mode_t = unsigned short (fatal typedef clash)
 *   - IOKit/IOLib.h: min/max macros defined before our shims
 *   - i386/limits.h: INT_MAX / LLONG_MAX
 *   - stdint.h     : SIZE_MAX
 *   - stddef.h     : NULL
 *   - IOMemoryDescriptor.h: C++ poison (template) — must never be
 *     reachable from a C translation unit
 *   - NO libkern/locks.h exists (only kern/locks.h)
 *   - OSCompareAndSwap(int, int, volatile int *)
 *
 * PASS = zero errors AND zero warnings.
 */
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/byteorder_little.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/semaphore.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/usb.h>
#include <linux/random.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/prefetch.h>
#include <linux/sem.h>
#include <linux/unistd.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/types.h>
#include <linux/limits.h>

/* ---- static sanity: types must exist and have Linux shapes ---- */
static u8  t_u8;  static u16 t_u16;  static u32 t_u32;  static u64 t_u64;
static s8  t_s8;  static s16 t_s16;  static s32 t_s32;  static s64 t_s64;
static mode_t t_mode;   /* Darwin's mode_t must be accepted as-is */
static pid_t  t_pid;
static gfp_t t_gfp;
static dma_addr_t t_dma;

/* min/max usable (ours or IOLib's — either compiles) */
static int t_minmax(int a, int b) { return min(a, b) + max(a, b); }
static int t_clamp(int v) { return clamp(v, 0, 100); }
static int t_abs(int v) { return abs(v); }

/* swap / round_down / unaligned get+put — remaining __typeof__ sites
 * in types.h and byteorder_little.h */
static unsigned long t_sink;
static void t_typeof_paths(void)
{
        unsigned long a = 5, b = 9;
        u16 buf = 0;
        swap(a, b);
        t_sink = round_down(b, 4);
        put_unaligned((u16)0x1234, &buf);
        t_sink += get_unaligned((const u16 *)&buf);
}

/* module_init/module_exit keep the init fn referenced via __typeof__
 * (invoked WITH semicolons, exactly like the vendor usb_intf.c does) */
static int t_module_fn(void) { return 0; }
module_init(t_module_fn);
module_exit(t_module_fn);

/* lockless_dereference (C++-safe); __same_type uses the C-only
 * __builtin_types_compatible_p — guarded out of the C++ pass */
static int t_ll_data;
static void t_lockless(void) { t_ll_data = lockless_dereference(t_ll_data); }
#ifndef __cplusplus
static int t_same_type(void) { return __same_type(t_ll_data, t_ll_data); }
#endif

/* container_of */
struct foo { int a; struct list_head node; };
static struct foo *t_container(struct list_head *n)
{ return container_of(n, struct foo, node); }

/* list walking — every list_* macro carries a __typeof__ site and must
 * compile in strict C++17 as well (the Driver TUs instantiate these) */
static struct foo t_foos[2];
static int t_list_walk(void)
{
        struct foo *pos, *n;
        struct list_head head;
        int cnt = 0;
        INIT_LIST_HEAD(&head);
        list_add_tail(&t_foos[0].node, &head);
        list_add_tail(&t_foos[1].node, &head);
        list_for_each_entry(pos, &head, node)          cnt += pos->a;
        list_for_each_entry_reverse(pos, &head, node)  cnt += pos->a;
        list_for_each_entry_safe(pos, n, &head, node)  cnt += pos->a;
        list_del(&t_foos[0].node);
        list_del(&t_foos[1].node);
        return cnt;
}

/* hlist walking — statement-expression + __typeof__ macro shape */
struct hfoo { int v; struct hlist_node hnode; };
static struct hfoo t_hfoos[2];
static int t_hlist_walk(void)
{
        struct hfoo *tpos;
        struct hlist_node *pos;
        struct hlist_head head;
        int cnt = 0;
        INIT_HLIST_HEAD(&head);
        INIT_HLIST_NODE(&t_hfoos[0].hnode);
        INIT_HLIST_NODE(&t_hfoos[1].hnode);
        hlist_add_head(&t_hfoos[0].hnode, &head);
        hlist_add_head(&t_hfoos[1].hnode, &head);
        hlist_for_each_entry(tpos, pos, &head, hnode) cnt += tpos->v;
        return cnt;
}

/* bitops */
static unsigned long t_bitmap[4];
static int t_bitops(void)
{
        set_bit(3, t_bitmap);
        clear_bit(4, t_bitmap);
        return test_bit(3, t_bitmap) + test_and_set_bit(5, t_bitmap)
             + test_and_clear_bit(5, t_bitmap) + (int)fls(0x80)
             + ffs(0x10) + (int)hweight32(0xff) + (int)__ffs(0x40);
}

/* atomics */
static atomic_t t_at = ATOMIC_INIT(1);
static atomic_long_t t_atl;
static int t_atomic(void)
{
        atomic_inc(&t_at);
        atomic_dec(&t_at);
        atomic_set(&t_at, 5);
        atomic_long_set(&t_atl, 2);
        return atomic_read(&t_at) + atomic_dec_and_test(&t_at)
             + atomic_add_return(2, &t_at) + (int)atomic_long_read(&t_atl);
}

/* byteorder */
static u32 t_be(u32 v) { return be32_to_cpu(cpu_to_be32(v)); }
static u16 t_le(u16 v) { return le16_to_cpu(cpu_to_le16(v)); }
static u32 t_unaligned(const u8 *p) { return get_unaligned_le32(p); }

/* locks */
static DEFINE_MUTEX(t_mutex);
static spinlock_t t_spin;
static rwlock_t t_rwlock;
static void t_locks(void)
{
        mutex_lock(&t_mutex); mutex_unlock(&t_mutex);
        spin_lock(&t_spin);   spin_unlock(&t_spin);
        unsigned long flags;
        spin_lock_irqsave(&t_spin, &flags); spin_unlock_irqrestore(&t_spin, flags);
        rwlock_init(&t_rwlock); read_lock(&t_rwlock); read_unlock(&t_rwlock);
        /* try-acquire + is_locked paths — the IOKit KPI route that
         * replaced the KERNEL_PRIVATE lck_*_try_lock shims */
        if (mutex_trylock(&t_mutex)) mutex_unlock(&t_mutex);
        if (spin_trylock(&t_spin))   spin_unlock(&t_spin);
        (void)mutex_is_locked(&t_mutex);
        (void)spin_is_locked(&t_spin);
}

/* wait queues + events */
static DECLARE_WAIT_QUEUE_HEAD(t_wq);
static int t_cond;
static void t_wait(void)       { wait_event(t_wq, t_cond); }
static int  t_wait_to(int ms)  { return wait_event_timeout(t_wq, t_cond, ms); }
static int  t_wait_intr(void)  { return wait_event_interruptible(t_wq, t_cond); }

/* semaphore + completion (both halves of the header) */
static struct semaphore t_sema;
static struct completion t_comp;
static void t_sema_comp(void)
{
        sema_init(&t_sema, 1); down(&t_sema); up(&t_sema);
        init_completion(&t_comp); complete(&t_comp); wait_for_completion(&t_comp);
}

/* timers + workqueue */
static void t_timer_fn(unsigned long d) { (void)d; }
static struct timer_list t_timer;
static void t_work_fn(struct work_struct *w) { (void)w; }
static struct work_struct t_work;
static struct delayed_work t_dwork;
static void t_timer_work(void)
{
        setup_timer(&t_timer, t_timer_fn, 0);
        mod_timer(&t_timer, jiffies + 100);
        del_timer_sync(&t_timer);
        INIT_WORK(&t_work, t_work_fn);
        schedule_work(&t_work);
        INIT_DELAYED_WORK(&t_dwork, t_work_fn);
        schedule_delayed_work(&t_dwork, 10);
        flush_scheduled_work();
}

/* skbuff */
static void t_skb(void)
{
        struct sk_buff *s = alloc_skb(1600, GFP_KERNEL);
        if (!s) return;
        skb_reserve(s, 64);
        memcpy(skb_put(s, 10), "hola", 5);
        skb_pull(s, 2);
        skb_push(s, 1);
        if (skb_headroom(s) > 0 && skb_tailroom(s) > 0 && !skb_is_nonlinear(s))
                dev_kfree_skb_any(s);
        else
                kfree_skb(s);
}

/* netdevice glue */
static struct net_device *t_ndev;
static void t_netdev(void)
{
        if (t_ndev) {
                netif_carrier_on(t_ndev);
                netif_stop_queue(t_ndev);
                netif_wake_queue(t_ndev);
        }
}

/* usb */
static void t_urb_fill(void)
{
        struct urb u;
        usb_fill_bulk_urb(&u, NULL, 0x81, NULL, 0, NULL, NULL);
        (void)usb_rcvbulkpipe(NULL, 0x81);
        (void)usb_sndbulkpipe(NULL, 0x01);
}

/* tasklet / misc */
static struct tasklet_struct t_tasklet;
static void t_tasklet_fn(unsigned long d) { (void)d; }
static void t_misc(void)
{
        tasklet_init(&t_tasklet, t_tasklet_fn, 0);
        tasklet_schedule(&t_tasklet);
        tasklet_kill(&t_tasklet);
        msleep(1); udelay(1); mdelay(1);
        (void)jiffies;
        (void)signal_pending(NULL);
}

/* printk */
static void t_printk(void)
{
        pr_info("info %d\n", 1);
        pr_err("err\n");
        printk("plain %s\n", "x");
}

/* errno / IS_ERR */
static void *t_errptr;
static long t_errcheck(void)
{
        if (IS_ERR(t_errptr)) return PTR_ERR(t_errptr);
        return 0;
}

/* exercise main entry (called from nothing; keeps bodies alive) */
void rtl_test_all_touch(void)
{
        t_minmax(1, 2); t_clamp(5); t_abs(-3); t_bitops(); t_atomic();
        t_be(1); t_le(2); t_unaligned((const u8 *)"abcd");
        t_list_walk(); t_hlist_walk(); t_typeof_paths(); t_lockless();
#ifndef __cplusplus
        t_same_type();
#endif
        t_locks(); t_wait(); t_wait_to(10); t_wait_intr();
        t_sema_comp(); t_timer_work(); t_skb(); t_netdev();
        t_urb_fill(); t_misc(); t_printk(); t_errcheck();
        (void)t_container; (void)t_minmax;
        (void)t_u8; (void)t_u16; (void)t_u32; (void)t_u64;
        (void)t_s8; (void)t_s16; (void)t_s32; (void)t_s64;
        (void)t_mode; (void)t_pid; (void)t_gfp; (void)t_dma;
}
