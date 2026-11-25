/*
 * RTL8814Native — the T9UH's blue status LED (StatusLed.h)
 *
 * One thread call every RTL_LED_TICK_MS works out the level the LED should
 * have and writes REG_GPIO_PIN_CTRL_2 only when it changes: a read and a
 * write of the 8 LED bits, every other bit as read. A tick that cannot write
 * now skips; the next one tries again. The register is not written while:
 * - the radio is not up (bup, drv_stopped, surprise_removed, HAL init),
 * - offch_mutex is held or offch_state is not OFFCHS_NONE: an AWDL hop or
 *   visit, a scan away from its home channel, a thermal calibration,
 * - setch_mutex is held: any channel set (scan, join, park, hop),
 * - a join runs (_FW_UNDER_LINKING).
 * Both mutexes are only tried, never waited for, so no hop or scan waits for
 * the LED longer than the two USB control transfers of a write that already
 * holds them.
 *
 * The 8 bits as first read after a start are put back on stop, on
 * RTL_LED_OFF set at run time, and are never touched with rtlled=0 at boot:
 * no read, no write, no thread call.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include <drv_types.h>
#include <hal_data.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/thread_call.h>
#include <kern/clock.h>
#include <libkern/OSAtomic.h>
#include "StatusLed.h"

extern int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_arg);
extern _adapter *rtl8814native_get_primary_adapter(void);
extern u64 rtl8814native_uptime_us(void);

/* Tries of the restore on stop while a hop or a channel set holds the radio,
 * RTL_LED_STOP_WAIT_MS apart: then the bits stay as they are, which the card
 * disable or the next HAL init overwrites anyway. */
#define RTL_LED_STOP_TRIES   25
#define RTL_LED_STOP_WAIT_MS 2

static IOLock *volatile g_led_lock;    /* kext lifetime */

static struct {
	/* under g_led_lock */
	thread_call_t call;
	int running;                    /* between start and stop */
	int stopping;                   /* no more arming */
	int busy;                       /* a tick is in rtl_led_step */
	int parsed;                     /* mode set from the boot-arg or a knob */
	/* any thread */
	volatile uint32_t mode;
	volatile uint32_t awdl_peers;
	volatile int netif;
	/* the tick (busy), and stop once the call is idle */
	int level;                      /* -1 not written since start */
	int dirty;                      /* the register holds a level of ours */
	int have_base;
	u32 base;                       /* the 8 bits before the first write */
	u64 traffic;                    /* data counters at the last tick */
	u64 flick_us;
} g_led = { .netif = -1, .level = -1 };

static IOLock *rtl_led_lock(void)
{
	if (!g_led_lock) {
		IOLock *l = IOLockAlloc();
		if (l && !OSCompareAndSwapPtr(NULL, l, (void *volatile *)&g_led_lock))
			IOLockFree(l);
	}
	return g_led_lock;
}

/* g_led_lock held */
static void rtl_led_arm(uint32_t ms)
{
	uint64_t deadline;

	if (g_led.stopping || !g_led.running || !g_led.call)
		return;
	clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
	thread_call_enter_delayed(g_led.call, deadline);
}

/* Both channel locks tried, and the radio on its channel with no join */
static int rtl_led_gate_enter(_adapter *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	struct dvobj_priv *dvobj = adapter_to_dvobj(a);

	if (!mutex_trylock(&rfctl->offch_mutex))
		return 0;
	if (rfctl->offch_state != OFFCHS_NONE || check_fwstate(&a->mlmepriv, _FW_UNDER_LINKING)) {
		mutex_unlock(&rfctl->offch_mutex);
		return 0;
	}
	if (!mutex_trylock(&dvobj->setch_mutex)) {
		mutex_unlock(&rfctl->offch_mutex);
		return 0;
	}
	return 1;
}

static void rtl_led_gate_exit(_adapter *a)
{
	mutex_unlock(&adapter_to_dvobj(a)->setch_mutex);
	mutex_unlock(&adapter_to_rfctl(a)->offch_mutex);
}

static int rtl_led_radio_up(_adapter *a)
{
	return a && a->bup && !RTW_CANNOT_RUN(a) && a->HalData && rtw_is_hw_init_completed(a);
}

/*
 * One read and at most one write of the LED bits: level 1 or 0, or -1 for
 * the bits read at the start. 0 when the gate is closed (nothing done).
 */
static int rtl_led_write(_adapter *a, int level)
{
	u32 v, nv;

	if (!rtl_led_gate_enter(a))
		return 0;
	v = rtw_read32(a, RTL_LED_REG);
	if (!g_led.have_base) {
		g_led.base = v & RTL_LED_BITS;
		g_led.have_base = 1;
	}
	nv = (v & ~RTL_LED_BITS) |
	     (level < 0 ? g_led.base : level ? RTL_LED_ON_BITS : RTL_LED_OFF_BITS);
	if (nv != v)
		rtw_write32(a, RTL_LED_REG, nv);
	rtl_led_gate_exit(a);
	g_led.level = level;
	g_led.dirty = level >= 0;
	return 1;
}

static void rtl_led_step(void)
{
	_adapter *a = rtl8814native_get_primary_adapter();
	uint32_t mode = g_led.mode;
	u64 now = rtl8814native_uptime_us(), traffic;
	int want, flick = 0, moved;

	if (!rtl_led_radio_up(a))
		return;
	/* data frames only: tx_pkts counts DATA_FRAMETAG, rx_bytes count_rx_stats */
	traffic = a->xmitpriv.tx_pkts + a->recvpriv.rx_bytes;
	moved = traffic != g_led.traffic;
	g_led.traffic = traffic;

	if (mode == RTL_LED_OFF) {
		if (g_led.dirty)
			rtl_led_write(a, -1);
		return;
	}
	if (mode == RTL_LED_STEADY)
		want = 1;
	else
		want = rtl_led_status_level(now, g_led.netif,
		                            check_fwstate(&a->mlmepriv, _FW_LINKED) ? 1 : 0,
		                            g_led.awdl_peers, moved, g_led.flick_us, &flick);
	if (want == g_led.level)
		return;
	if (rtl_led_write(a, want) && flick)
		g_led.flick_us = now;
}

static void rtl_led_tick(thread_call_param_t p0, thread_call_param_t p1)
{
	IOLock *l = g_led_lock;

	(void)p0;
	(void)p1;
	if (!l)
		return;
	IOLockLock(l);
	/* a second invocation in flight (an arm while one ran) leaves it to the first */
	if (g_led.stopping || !g_led.running || g_led.busy) {
		IOLockUnlock(l);
		return;
	}
	g_led.busy = 1;
	IOLockUnlock(l);

	rtl_led_step();

	IOLockLock(l);
	g_led.busy = 0;
	/* RTL_LED_OFF set at run time: on until the bits are back */
	if (g_led.mode != RTL_LED_OFF || g_led.dirty)
		rtl_led_arm(RTL_LED_TICK_MS);
	IOLockUnlock(l);
}

/* The mode from rtlled, once per kext load unless a knob set it first */
static void rtl_led_parse(void)
{
	uint32_t v = RTL_LED_STATUS;

	if (g_led.parsed)
		return;
	if (PE_parse_boot_argn("rtlled", &v, sizeof(v)) && v > RTL_LED_STEADY)
		v = RTL_LED_STATUS;
	g_led.mode = v;
	g_led.parsed = 1;
}

void rtl8814native_led_start(void)
{
	IOLock *l = rtl_led_lock();

	if (!l)
		return;
	IOLockLock(l);
	rtl_led_parse();
	if (g_led.running) {
		IOLockUnlock(l);
		return;
	}
	g_led.running = 1;
	g_led.stopping = 0;
	g_led.level = -1;
	g_led.dirty = 0;
	g_led.have_base = 0;
	g_led.traffic = 0;
	g_led.flick_us = 0;
	/* rtlled=0: no thread call either, until a knob asks for one */
	if (g_led.mode != RTL_LED_OFF && !g_led.call)
		g_led.call = thread_call_allocate_with_priority(rtl_led_tick, NULL,
		                                                THREAD_CALL_PRIORITY_KERNEL);
	rtl_led_arm(RTL_LED_TICK_MS);
	IOLockUnlock(l);
}

void rtl8814native_led_stop(void)
{
	IOLock *l = g_led_lock;
	thread_call_t call;
	_adapter *a;
	int i;

	if (!l)
		return;
	IOLockLock(l);
	if (!g_led.running) {
		IOLockUnlock(l);
		return;
	}
	g_led.stopping = 1;
	call = g_led.call;
	IOLockUnlock(l);

	/* Nothing arms it any more: cancel a pending tick, wait for a running one. */
	if (call) {
		for (;;) {
			thread_call_cancel_wait(call);
			if (!thread_call_isactive(call))
				break;
			IOSleep(1);
		}
	}

	/* The call is idle: its state is ours. Back to the hardware's bits. */
	a = rtl8814native_get_primary_adapter();
	for (i = 0; g_led.dirty && i < RTL_LED_STOP_TRIES && rtl_led_radio_up(a); i++) {
		if (rtl_led_write(a, -1))
			break;
		IOSleep(RTL_LED_STOP_WAIT_MS);
	}

	IOLockLock(l);
	g_led.running = 0;
	g_led.call = NULL;
	g_led.level = -1;
	g_led.dirty = 0;
	g_led.have_base = 0;
	IOLockUnlock(l);
	if (call)
		thread_call_free(call);
}

int rtl8814native_led_set_mode(uint32_t mode)
{
	IOLock *l = rtl_led_lock();

	if (mode > RTL_LED_STEADY)
		return -1;
	if (!l)
		return -1;
	IOLockLock(l);
	g_led.mode = mode;
	g_led.parsed = 1;
	if (mode != RTL_LED_OFF && g_led.running && !g_led.stopping && !g_led.call)
		g_led.call = thread_call_allocate_with_priority(rtl_led_tick, NULL,
		                                                THREAD_CALL_PRIORITY_KERNEL);
	/* the next tick acts on it, also the one that puts the bits back */
	if (mode != RTL_LED_OFF || g_led.dirty)
		rtl_led_arm(1);
	IOLockUnlock(l);
	return 0;
}

uint32_t rtl8814native_led_mode(void)
{
	IOLock *l = rtl_led_lock();
	uint32_t m;

	if (!l)
		return RTL_LED_STATUS;
	IOLockLock(l);
	rtl_led_parse();
	m = g_led.mode;
	IOLockUnlock(l);
	return m;
}

void rtl8814native_led_awdl_peers(uint32_t n)
{
	g_led.awdl_peers = n;
}

void rtl8814native_led_netif(int state)
{
	g_led.netif = state;
}
