/*
 * RTL8814Native — macOS osdep_service definitions
 *
 * Replaces osdep_service_linux.h for macOS kernel extension environment.
 * Maps Realtek driver types and synchronization primitives to the
 * Linux-IOKit compatibility layer (Source/Compat/).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef __OSDEP_MACOS_SERVICE_H_
#define __OSDEP_MACOS_SERVICE_H_

#ifndef PLATFORM_MACOS
#define PLATFORM_MACOS 1
#endif

#ifndef PLATFORM_LINUX
#define PLATFORM_LINUX 1
#endif

#include <linux/version.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include <linux/usb.h>
#include <linux/semaphore.h>
#include <linux/wireless.h>

#ifdef ERR_SUCCESS
#undef ERR_SUCCESS
#endif

#ifdef CONFIG_USB_HCI
typedef struct urb *PURB;
#endif

typedef struct semaphore _sema;
typedef spinlock_t       _lock;
typedef struct mutex     _mutex;

struct rtw_timer_list {
	struct timer_list timer;
	void (*function)(void *);
	void *arg;
};

typedef struct rtw_timer_list _timer;
typedef struct completion     _completion;

struct __queue {
	struct list_head queue;
	_lock            lock;
};

typedef struct __queue   _queue;
typedef struct list_head _list;

typedef struct sk_buff   _pkt;
typedef unsigned char    _buffer;

typedef struct hlist_head rtw_hlist_head;
typedef struct hlist_node rtw_hlist_node;

typedef int           _OS_STATUS;
typedef unsigned long _irqL;
typedef struct net_device *_nic_hdl;

typedef void *_thread_hdl_;
typedef int   thread_return;
typedef void *thread_context;

typedef void timer_hdl_return;
typedef void *timer_hdl_context;

typedef struct work_struct   _workitem;
typedef unsigned long        systime;
typedef struct tasklet_struct _tasklet;

static inline void rtw_list_delete(_list *plist)
{
	list_del_init(plist);
}

static inline _list *get_next(_list *list)
{
	return list->next;
}

static inline _list *get_list_head(_queue *queue)
{
	return &(queue->queue);
}

#define LIST_CONTAINOR(ptr, type, member) \
	((type *)((char *)(ptr) - (size_t)(&((type *)0)->member)))

#define rtw_list_first_entry(ptr, type, member) list_first_entry(ptr, type, member)
#define rtw_hlist_for_each_entry(pos, head, member) hlist_for_each_entry(pos, head, member)
#define rtw_hlist_for_each_safe(pos, n, head) hlist_for_each_safe(pos, n, head)
#define rtw_hlist_entry(ptr, type, member) hlist_entry(ptr, type, member)

static inline void _enter_critical(_lock *plock, _irqL *pirqL)
{
	spin_lock_irqsave(plock, *pirqL);
}

static inline void _exit_critical(_lock *plock, _irqL *pirqL)
{
	spin_unlock_irqrestore(plock, *pirqL);
}

static inline void _enter_critical_ex(_lock *plock, _irqL *pirqL)
{
	spin_lock_irqsave(plock, *pirqL);
}

static inline void _exit_critical_ex(_lock *plock, _irqL *pirqL)
{
	spin_unlock_irqrestore(plock, *pirqL);
}

static inline void _enter_critical_bh(_lock *plock, _irqL *pirqL)
{
	(void)pirqL;
	spin_lock_bh(plock);
}

static inline void _exit_critical_bh(_lock *plock, _irqL *pirqL)
{
	(void)pirqL;
	spin_unlock_bh(plock);
}

static inline void enter_critical_bh(_lock *plock)
{
	spin_lock_bh(plock);
}

static inline void exit_critical_bh(_lock *plock)
{
	spin_unlock_bh(plock);
}

static inline int _enter_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
	(void)pirqL;
	return mutex_lock_interruptible(pmutex);
}

static inline int _enter_critical_mutex_lock(_mutex *pmutex, _irqL *pirqL)
{
	(void)pirqL;
	mutex_lock(pmutex);
	return 0;
}

static inline void _exit_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
	(void)pirqL;
	mutex_unlock(pmutex);
}

static inline void timer_hdl(unsigned long cntx)
{
	_timer *ptimer = (_timer *)cntx;
	if (ptimer && ptimer->function)
		ptimer->function(ptimer->arg);
}

static inline void _init_timer(_timer *ptimer, _nic_hdl nic_hdl, void *pfunc, void *cntx)
{
	(void)nic_hdl;
	ptimer->function = (void (*)(void *))pfunc;
	ptimer->arg = cntx;
	init_timer(&ptimer->timer);
	ptimer->timer.function = timer_hdl;
	ptimer->timer.data = (unsigned long)ptimer;
}

static inline void _set_timer(_timer *ptimer, u32 delay_time)
{
	mod_timer(&ptimer->timer, jiffies + msecs_to_jiffies(delay_time));
}

static inline void _cancel_timer(_timer *ptimer, u8 *bcancelled)
{
	*bcancelled = del_timer_sync(&ptimer->timer) == 1 ? 1 : 0;
}

static inline void _init_workitem(_workitem *pwork, void *pfunc, void *cntx)
{
	(void)cntx;
	INIT_WORK(pwork, pfunc);
}

static inline void _set_workitem(_workitem *pwork)
{
	schedule_work(pwork);
}

static inline void _cancel_workitem_sync(_workitem *pwork)
{
	cancel_work_sync(pwork);
}

#define ACQUIRE_GLOBAL_MUTEX(_MutexCounter)                              \
	{                                                               \
		while (atomic_inc_return((atomic_t *)&(_MutexCounter)) != 1) { \
			atomic_dec((atomic_t *)&(_MutexCounter));        \
			msleep(10);                          \
		}                                                           \
	}

#define RELEASE_GLOBAL_MUTEX(_MutexCounter)                              \
	{                                                               \
		atomic_dec((atomic_t *)&(_MutexCounter));        \
	}

static inline int rtw_netif_queue_stopped(struct net_device *pnetdev)
{
	return netif_queue_stopped(pnetdev);
}

static inline void rtw_netif_wake_queue(struct net_device *pnetdev)
{
	netif_wake_queue(pnetdev);
}

static inline void rtw_netif_start_queue(struct net_device *pnetdev)
{
	netif_start_queue(pnetdev);
}

static inline void rtw_netif_stop_queue(struct net_device *pnetdev)
{
	netif_stop_queue(pnetdev);
}

static inline void rtw_netif_device_attach(struct net_device *pnetdev)
{
	netif_device_attach(pnetdev);
}

static inline void rtw_netif_device_detach(struct net_device *pnetdev)
{
	netif_device_detach(pnetdev);
}

static inline void rtw_netif_carrier_on(struct net_device *pnetdev)
{
	netif_carrier_on(pnetdev);
}

static inline void rtw_netif_carrier_off(struct net_device *pnetdev)
{
	netif_carrier_off(pnetdev);
}

static inline int rtw_merge_string(char *dst, int dst_len, const char *src1, const char *src2)
{
	int len = 0;
	len += snprintf(dst + len, dst_len - len, "%s", src1);
	len += snprintf(dst + len, dst_len - len, "%s", src2);
	return len;
}

#define PATH_LENGTH_MAX 1024
#define ATOMIC_T atomic_t

#define rtw_netdev_priv(netdev) (((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv)

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) (adapter->pnetdev ? adapter->pnetdev->name : NULL)
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
#define FUNC_ADPT_ARG(adapter) __func__, (adapter->pnetdev ? adapter->pnetdev->name : NULL)

struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv);
extern struct net_device *rtw_alloc_etherdev(int sizeof_priv);

#define STRUCT_PACKED __attribute__ ((packed))

#endif /* __OSDEP_MACOS_SERVICE_H_ */
