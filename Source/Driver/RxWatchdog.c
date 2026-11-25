/*
 * RTL8814Native — stuck bulk IN detector (RxWatchdog.h)
 *
 * One thread call every RTL_RXWD_TICK_MS samples the RX counters, the ring
 * and the link state, all from memory, and feeds rtl_rxwd_step. A stall is
 * counted, published (RTLRxStalls, RTLRxLastStall) and logged in one line
 * with the counters. With rtlrxwd=2 or 3 the tick also runs the recovery
 * the step asks for, only when no AWDL hop or visit, channel set or join
 * holds the radio (the gate of StatusLed.c), else at a later tick.
 *
 * The life cycle is the LED's: started at the end of probe and wake,
 * stopped before remove, sleep and shutdown touch the device; stop waits
 * for a tick still running, recovery included, so nothing tears the radio
 * down under it. rtlrxwd=0 at boot allocates no thread call.
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
#include "RTLRadioABI.h"
#include "VendorGlue.hpp"
#include "RxWatchdog.h"

extern int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_arg);
extern _adapter *rtl8814native_get_primary_adapter(void);
extern u64 rtl8814native_uptime_us(void);
extern int rtl8814native_disconnect(void);
extern volatile uint32_t g_rx_urb_submit_ok;
extern volatile uint32_t g_rx_urb_submit_err;
extern volatile uint32_t g_rx_urb_complete_ok;
extern volatile uint32_t g_rx_urb_complete_err;
extern volatile uint32_t g_rx_frames_decoded;
extern u32 rtl8814native_awdl_hop_seq;     /* AWDLTx.c */

static IOLock *volatile g_rxwd_lock;    /* kext lifetime */

static struct {
	/* under g_rxwd_lock */
	thread_call_t call;
	int running;                    /* between start and stop */
	int stopping;                   /* no more arming */
	int busy;                       /* a tick is in rtl_rxwd_run */
	int parsed;                     /* mode set from the boot-arg or a knob */
	/* any thread */
	volatile uint32_t mode;
	/* the tick (busy) */
	struct rtl_rxwd st;
	/* kext lifetime, also across sleep and re-probe */
	uint32_t stalls;
	uint64_t last_stall_s;
} g_rxwd = { .mode = RTL_RXWD_LOG };

static IOLock *rtl_rxwd_lock(void)
{
	if (!g_rxwd_lock) {
		IOLock *l = IOLockAlloc();
		if (l && !OSCompareAndSwapPtr(NULL, l, (void *volatile *)&g_rxwd_lock))
			IOLockFree(l);
	}
	return g_rxwd_lock;
}

/* g_rxwd_lock held */
static void rtl_rxwd_arm(uint32_t ms)
{
	uint64_t deadline;

	if (g_rxwd.stopping || !g_rxwd.running || !g_rxwd.call)
		return;
	clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
	thread_call_enter_delayed(g_rxwd.call, deadline);
}

/* The radio is up and its RX ring was started; no I/O */
static int rtl_rxwd_ready(_adapter *a)
{
	if (!a || !a->bup || RTW_CANNOT_RUN(a) || !a->HalData || !rtw_is_hw_init_completed(a) ||
	    !a->recvpriv.precv_buf)
		return 0;
	return GET_HAL_DATA(a)->usb_intf_start == _TRUE;
}

/* recv_bufs with a bulk IN URB submitted and not completed */
static uint32_t rtl_rxwd_pending(_adapter *a)
{
	struct recv_buf *b = (struct recv_buf *)a->recvpriv.precv_buf;
	uint32_t n = 0;
	int i;

	for (i = 0; i < NR_RECVBUFF; i++, b++)
		if (*(volatile u8 *)&b->irp_pending)
			n++;
	return n;
}

/* Both channel locks tried, never waited for, and the radio on its channel
 * with no join: StatusLed.c's rtl_led_gate_enter */
static int rtl_rxwd_gate_enter(_adapter *a)
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

static void rtl_rxwd_gate_exit(_adapter *a)
{
	mutex_unlock(&adapter_to_dvobj(a)->setch_mutex);
	mutex_unlock(&adapter_to_rfctl(a)->offch_mutex);
}

static const char *rtl_rxwd_act_name(uint32_t act)
{
	switch (act) {
	case RTL_RXWD_A_REARM:     return "re-arm bulk IN";
	case RTL_RXWD_A_RESTART:   return "restart the radio";
	case RTL_RXWD_A_GIVE_UP:   return "give up, drop the link";
	case RTL_RXWD_A_EXHAUSTED: return "re-arm tries used up, no more recovery";
	}
	return "log only";
}

/* 1 when act ran (or has nothing left to run), 0 to try at the next tick */
static int rtl_rxwd_act(_adapter *a, uint32_t act)
{
	int rc;

	switch (act) {
	case RTL_RXWD_A_REARM:
		if (!rtl_rxwd_gate_enter(a))
			return 0;
		rc = rtl8814native_rx_ring_restart(a);
		rtl_rxwd_gate_exit(a);
		if (rc < 0)
			rtl_printk("RTL8814Native: RX watchdog: re-arm failed (%d), bulk IN left disabled\n", rc);
		else
			rtl_printk("RTL8814Native: RX watchdog: bulk IN re-armed, %d of %d URBs submitted\n",
				   rc, NR_RECVBUFF);
		return 1;
	case RTL_RXWD_A_RESTART:
		/* only tried: the restart takes offch_mutex itself once the
		 * command thread is stopped (it may wait for that lock) */
		if (!rtl_rxwd_gate_enter(a))
			return 0;
		rtl_rxwd_gate_exit(a);
		rc = rtl8814native_radio_restart();
		rtl_printk("RTL8814Native: RX watchdog: radio restart %s (%d)\n", rc ? "FAILED" : "done", rc);
		return 1;
	case RTL_RXWD_A_GIVE_UP:
		rtl_printk("RTL8814Native: RX watchdog: GIVING UP after %d radio restarts, bulk IN still "
			   "delivers nothing: dropping the link\n", RTL_RXWD_TRIES);
		rtl8814native_disconnect();
		return 1;
	case RTL_RXWD_A_EXHAUSTED:
		rtl_printk("RTL8814Native: RX watchdog: %d re-arms did not bring bulk IN back, no more "
			   "recovery until RX moves (rtlrxwd=3 also restarts the radio)\n", RTL_RXWD_TRIES);
		return 1;
	}
	return 1;
}

static void rtl_rxwd_run(void)
{
	_adapter *a = rtl8814native_get_primary_adapter();
	struct rtl_rxwd_in in;
	struct rtl_rxwd_out out;
	uint32_t act;

	_rtw_memset(&in, 0, sizeof(in));
	in.mode = g_rxwd.mode;
	in.ring = NR_RECVBUFF;
	in.ready = rtl_rxwd_ready(a);
	if (in.ready) {
		struct mlme_priv *m = &a->mlmepriv;

		in.pending = rtl_rxwd_pending(a);
		in.comp = g_rx_urb_complete_ok + g_rx_urb_complete_err;
		in.scans = rtl8814native_rx_scans_done();
		in.scanning = check_fwstate(m, _FW_UNDER_SURVEY) ? 1 : 0;
		in.linked_home = check_fwstate(m, _FW_LINKED) && !in.scanning &&
		                 !check_fwstate(m, _FW_UNDER_LINKING) &&
		                 adapter_to_rfctl(a)->offch_state == OFFCHS_NONE;
	}

	act = rtl_rxwd_step(&g_rxwd.st, &in, &out);

	if (out.stall) {
		g_rxwd.stalls++;
		g_rxwd.last_stall_s = rtl8814native_uptime_us() / 1000000ull;
		rtl_printk("RTL8814Native: RX stall %u (%s): no bulk IN completion for %u s, %u/%u URBs "
			   "out, linked %d, scans %u; urb submit ok %u err %u, done ok %u err %u; rx frames "
			   "%u, beacons %u; tx ok %u err %u (last %d); hops %u; rtlrxwd %u: %s\n",
			   g_rxwd.stalls, out.reason == RTL_RXWD_R_EMPTY ? "ring empty" : "ring stuck",
			   out.ticks * (RTL_RXWD_TICK_MS / 1000), in.pending, in.ring,
			   check_fwstate(&a->mlmepriv, _FW_LINKED) ? 1 : 0, in.scans,
			   g_rx_urb_submit_ok, g_rx_urb_submit_err, g_rx_urb_complete_ok,
			   g_rx_urb_complete_err, g_rx_frames_decoded, g_trx_diag.rx_beacon,
			   g_trx_diag.tx_write_ok, g_trx_diag.tx_write_err, g_trx_diag.tx_last_err,
			   __atomic_load_n(&rtl8814native_awdl_hop_seq, __ATOMIC_RELAXED), in.mode,
			   rtl_rxwd_act_name(act));
		rtl8814native_publish_rx_stalls(g_rxwd.stalls, g_rxwd.last_stall_s);
	}

	if (act != RTL_RXWD_A_NONE && in.ready && rtl_rxwd_act(a, act))
		rtl_rxwd_done(&g_rxwd.st);
}

static void rtl_rxwd_tick(thread_call_param_t p0, thread_call_param_t p1)
{
	IOLock *l = g_rxwd_lock;

	(void)p0;
	(void)p1;
	if (!l)
		return;
	IOLockLock(l);
	/* a second invocation in flight (an arm while one ran) leaves it to the first */
	if (g_rxwd.stopping || !g_rxwd.running || g_rxwd.busy) {
		IOLockUnlock(l);
		return;
	}
	g_rxwd.busy = 1;
	IOLockUnlock(l);

	rtl_rxwd_run();

	IOLockLock(l);
	g_rxwd.busy = 0;
	if (g_rxwd.mode != RTL_RXWD_OFF)
		rtl_rxwd_arm(RTL_RXWD_TICK_MS);
	IOLockUnlock(l);
}

/* The mode from rtlrxwd, once per kext load unless a knob set it first */
static void rtl_rxwd_parse(void)
{
	uint32_t v = RTL_RXWD_LOG;

	if (g_rxwd.parsed)
		return;
	if (PE_parse_boot_argn("rtlrxwd", &v, sizeof(v)) && v > RTL_RXWD_RESTART)
		v = RTL_RXWD_LOG;
	g_rxwd.mode = v;
	g_rxwd.parsed = 1;
}

void rtl8814native_rxwd_start(void)
{
	IOLock *l = rtl_rxwd_lock();

	if (!l)
		return;
	IOLockLock(l);
	rtl_rxwd_parse();
	if (g_rxwd.running) {
		IOLockUnlock(l);
		return;
	}
	g_rxwd.running = 1;
	g_rxwd.stopping = 0;
	_rtw_memset(&g_rxwd.st, 0, sizeof(g_rxwd.st));
	/* rtlrxwd=0: no thread call either, until a knob asks for one */
	if (g_rxwd.mode != RTL_RXWD_OFF && !g_rxwd.call)
		g_rxwd.call = thread_call_allocate_with_priority(rtl_rxwd_tick, NULL,
		                                                 THREAD_CALL_PRIORITY_KERNEL);
	rtl_rxwd_arm(RTL_RXWD_TICK_MS);
	IOLockUnlock(l);
	rtl8814native_publish_rx_stalls(g_rxwd.stalls, g_rxwd.last_stall_s);
}

void rtl8814native_rxwd_stop(void)
{
	IOLock *l = g_rxwd_lock;
	thread_call_t call;

	if (!l)
		return;
	IOLockLock(l);
	if (!g_rxwd.running) {
		IOLockUnlock(l);
		return;
	}
	g_rxwd.stopping = 1;
	call = g_rxwd.call;
	IOLockUnlock(l);

	/* Nothing arms it any more: cancel a pending tick, wait for a running one */
	if (call) {
		for (;;) {
			thread_call_cancel_wait(call);
			if (!thread_call_isactive(call))
				break;
			IOSleep(1);
		}
	}

	IOLockLock(l);
	g_rxwd.running = 0;
	g_rxwd.call = NULL;
	IOLockUnlock(l);
	if (call)
		thread_call_free(call);
}

int rtl8814native_rxwd_set_mode(uint32_t mode)
{
	IOLock *l = rtl_rxwd_lock();

	if (mode > RTL_RXWD_RESTART || !l)
		return -1;
	IOLockLock(l);
	g_rxwd.mode = mode;
	g_rxwd.parsed = 1;
	if (mode != RTL_RXWD_OFF && g_rxwd.running && !g_rxwd.stopping && !g_rxwd.call)
		g_rxwd.call = thread_call_allocate_with_priority(rtl_rxwd_tick, NULL,
		                                                 THREAD_CALL_PRIORITY_KERNEL);
	if (mode != RTL_RXWD_OFF)
		rtl_rxwd_arm(RTL_RXWD_TICK_MS);
	IOLockUnlock(l);
	return 0;
}

uint32_t rtl8814native_rxwd_mode(void)
{
	IOLock *l = rtl_rxwd_lock();
	uint32_t m;

	if (!l)
		return RTL_RXWD_LOG;
	IOLockLock(l);
	rtl_rxwd_parse();
	m = g_rxwd.mode;
	IOLockUnlock(l);
	return m;
}
