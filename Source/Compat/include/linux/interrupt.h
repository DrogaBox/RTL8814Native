/*
 * interrupt.h — tasklets are scheduled onto the system workqueue thread
 * ("rtl_system_wq") so they run in normal kernel thread context outside
 * the IOUSBHostFamily completion workloop gate.
 */
#ifndef _LINUX_INTERRUPT_H_RTL8814
#define _LINUX_INTERRUPT_H_RTL8814

#include <linux/types.h>
#include <linux/workqueue.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tasklet_struct {
	void (*func)(unsigned long);
	unsigned long data;
	struct work_struct work;
};

void rtl_tasklet_work_fn(struct work_struct *w);

static inline void tasklet_schedule(struct tasklet_struct *t)
{
	if (t && t->func)
		rtl_schedule_work(&t->work);
}

#define tasklet_hi_schedule(t) tasklet_schedule(t)

static inline void tasklet_kill(struct tasklet_struct *t)
{
	if (t)
		cancel_work_sync(&t->work);
}

static inline void tasklet_init(struct tasklet_struct *t,
                                void (*fn)(unsigned long), unsigned long d)
{
	t->func = fn;
	t->data = d;
	INIT_WORK(&t->work, rtl_tasklet_work_fn);
}

#define local_bh_disable()  do { } while (0)
#define local_bh_enable()   do { } while (0)
#define in_interrupt()      (0)
#define in_atomic()         (0)
#define smp_processor_id()  (0)

#ifdef __cplusplus
}
#endif

#endif
