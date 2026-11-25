/*
 * RTL8814Native — stuck bulk IN detector (RxWatchdog.c)
 *
 * One thread call a second reads counters only (no register, no pipe I/O)
 * and checks that the RX ring still delivers. A tick is silent when no bulk
 * IN URB completed since the last one (g_rx_urb_complete_ok + _err). A run
 * of RTL_RXWD_STALL_TICKS silent ticks or more is a stall when either:
 *
 *  - no URB is in flight (RTL_RXWD_R_EMPTY): an empty ring receives nothing
 *    whatever the air carries. That is the state an in-place wake leaves:
 *    rtw_intf_stop at sleep sets DF_RX_BIT (rtw_read_port_cancel) and the
 *    wake path never clears it, so usb_read_port refuses every URB;
 *  - every URB is in flight (RTL_RXWD_R_STUCK) and frames were due:
 *    - linked, outside a scan and a join, on the BSS channel (no AWDL
 *      visit) at RTL_RXWD_STALL_TICKS of the run's ticks: the AP's beacons
 *      come every ~102 ms (measured 2026-11-24, driver 38FE973D, linked on
 *      161 with AWDL hopping 4 times a second: 10-12 completions a second,
 *      10-11 of them beacons, never 0 in 11 s), or
 *    - a whole scan ran inside the run (none running at its start, one
 *      completed since; two if one was running): probe responses and
 *      beacons of every network around. The deaf radio of 2026-11-11
 *      (Logs/pruebas_awdl/rx_parado_20261111: 8 URBs out, completions frozen
 *      at 496, TX moving, every scan with 0 networks) is this case.
 *
 * Legitimate silence is never counted: unlinked and parked on a quiet
 * channel, Wi-Fi off with the radio up, an AWDL slot or a park away from the
 * BSS, a ring with only some URBs out (it still receives). The one silence
 * misread is a scan in a place with no network at all, or an AP that goes
 * away while linked (the vendor's link check drops it a few seconds later).
 * One stall is counted per silent run; RX that moves again ends it.
 *
 * Boot-arg rtlrxwd and knob RTL_KNOB_RXWD (rtl8814native_set_knob, test_uc
 * knob 4):
 *   RTL_RXWD_OFF      0  no thread call
 *   RTL_RXWD_LOG      1  count, publish RTLRxStalls / RTLRxLastStall, log
 *                        one line per stall (default)
 *   RTL_RXWD_REARM    2  also level 1: abort bulk IN, reset the RX stage,
 *                        submit every recv_buf again; RTL_RXWD_TRIES tries
 *   RTL_RXWD_RESTART  3  level 1 once, then level 2: the radio down and up
 *                        in place (card disable, full HAL init, re-arm);
 *                        after RTL_RXWD_TRIES of those, give up: the link is
 *                        dropped so the layer sees the radio down
 * Recovery does pipe and register I/O, so it waits for a tick with no AWDL
 * hop, no visit, no channel set and no join (the StatusLed.c gate). Tries
 * are forgotten after RTL_RXWD_HEALTHY_TICKS ticks of RX without a stall.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_RX_WATCHDOG_H
#define _RTL8814_RX_WATCHDOG_H

#include <stdint.h>

#define RTL_RXWD_OFF      0
#define RTL_RXWD_LOG      1
#define RTL_RXWD_REARM    2
#define RTL_RXWD_RESTART  3

#define RTL_KNOB_RXWD     4         /* rtl8814native_set_knob id, after RTL_KNOB_LED */

#define RTL_RXWD_TICK_MS        1000
#define RTL_RXWD_STALL_TICKS    2
#define RTL_RXWD_TRIES          3
#define RTL_RXWD_HEALTHY_TICKS  60

/* why a run is a stall */
#define RTL_RXWD_R_EMPTY  1         /* no URB in flight */
#define RTL_RXWD_R_STUCK  2         /* every URB in flight, none completes */

/* what the tick is asked to do */
#define RTL_RXWD_A_NONE       0
#define RTL_RXWD_A_REARM      1     /* level 1 */
#define RTL_RXWD_A_RESTART    2     /* level 2 */
#define RTL_RXWD_A_GIVE_UP    3     /* rtlrxwd=3 out of tries: drop the link */
#define RTL_RXWD_A_EXHAUSTED  4     /* rtlrxwd=2 out of tries: log, stop trying */

/* One tick's view of the radio, from counters and driver state only */
struct rtl_rxwd_in {
	uint32_t mode;          /* RTL_RXWD_* */
	int      ready;         /* up, HAL init done, ring started, not stopping */
	uint32_t pending;       /* RX URBs in flight */
	uint32_t ring;          /* NR_RECVBUFF */
	uint32_t comp;          /* RX URB completions, ok + error, running count */
	uint32_t scans;         /* scans completed (not aborted), running count */
	int      scanning;      /* a scan runs now */
	int      linked_home;   /* linked, no scan, no join, radio on the BSS channel */
};

struct rtl_rxwd_out {
	int      stall;         /* a stall counted at this tick */
	uint32_t reason;        /* RTL_RXWD_R_* of it */
	uint32_t ticks;         /* silent ticks so far */
};

/* Detector state; all zero is the state at start */
struct rtl_rxwd {
	int      primed;        /* comp / scans below hold a sample */
	uint32_t comp;
	uint32_t scans;
	int      scanning;
	uint32_t run;           /* silent ticks in a row, one reason */
	uint32_t reason;
	uint32_t ev_link;       /* ticks of the run with linked_home */
	uint32_t scan0;         /* scans when the run began */
	uint32_t scan_need;     /* scans the run must see complete */
	int      reported;      /* this run is counted */
	uint32_t want;          /* RTL_RXWD_A_* decided, not done yet */
	uint32_t mode;          /* at the last tick */
	uint32_t tries;         /* recoveries done since RX last held */
	uint32_t healthy;       /* ticks with RX since the last stall */
	int      gave_up;
};

/* The recovery a new stall asks for in mode */
static inline uint32_t rtl_rxwd_plan(const struct rtl_rxwd *s, uint32_t mode)
{
	if (s->gave_up || mode < RTL_RXWD_REARM)
		return RTL_RXWD_A_NONE;
	if (mode == RTL_RXWD_REARM)
		return s->tries < RTL_RXWD_TRIES ? RTL_RXWD_A_REARM : RTL_RXWD_A_EXHAUSTED;
	if (s->tries == 0)
		return RTL_RXWD_A_REARM;
	return s->tries <= RTL_RXWD_TRIES ? RTL_RXWD_A_RESTART : RTL_RXWD_A_GIVE_UP;
}

/*
 * One tick. Returns the recovery to run now (RTL_RXWD_A_*); a recovery
 * that could not run yet (the gate was closed) comes back at the next tick
 * until rtl_rxwd_done. No kernel types, so it can be run on the host.
 */
static inline uint32_t rtl_rxwd_step(struct rtl_rxwd *s, const struct rtl_rxwd_in *in,
                                     struct rtl_rxwd_out *out)
{
	uint32_t reason;
	int moved, real;

	out->stall = 0;
	out->reason = 0;
	out->ticks = 0;
	if (in->mode == RTL_RXWD_OFF || !in->ready) {
		/* the next ready tick only samples */
		s->primed = 0;
		s->run = 0;
		s->want = RTL_RXWD_A_NONE;
		return RTL_RXWD_A_NONE;
	}
	if (!s->primed) {
		s->primed = 1;
		s->run = 0;
		goto save;
	}

	moved = in->comp != s->comp;
	reason = in->pending == 0 ? RTL_RXWD_R_EMPTY :
	         in->pending >= in->ring ? RTL_RXWD_R_STUCK : 0;
	if (moved) {
		s->run = 0;
		s->reported = 0;
		s->want = RTL_RXWD_A_NONE;
		if (s->healthy < RTL_RXWD_HEALTHY_TICKS)
			s->healthy++;
		if (s->healthy >= RTL_RXWD_HEALTHY_TICKS) {
			s->tries = 0;
			s->gave_up = 0;
		}
		goto save;
	}
	if (!reason) {
		/* some URBs out and a quiet channel: not judged */
		s->run = 0;
		goto save;
	}
	if (!s->run || reason != s->reason) {
		s->run = 0;
		s->reason = reason;
		s->ev_link = 0;
		s->scan0 = s->scans;
		s->scan_need = s->scanning ? 2 : 1;
	}
	s->run++;
	if (reason == RTL_RXWD_R_STUCK && in->linked_home)
		s->ev_link++;
	real = s->run >= RTL_RXWD_STALL_TICKS &&
	       (reason == RTL_RXWD_R_EMPTY || s->ev_link >= RTL_RXWD_STALL_TICKS ||
	        in->scans - s->scan0 >= s->scan_need);
	if (real && !s->reported) {
		s->reported = 1;
		s->healthy = 0;
		out->stall = 1;
		out->reason = reason;
		out->ticks = s->run;
		s->want = rtl_rxwd_plan(s, in->mode);
	}
save:
	/* a knob changed the mode while a recovery waited for the gate */
	if (s->want != RTL_RXWD_A_NONE && in->mode != s->mode)
		s->want = rtl_rxwd_plan(s, in->mode);
	s->mode = in->mode;
	s->comp = in->comp;
	s->scans = in->scans;
	s->scanning = in->scanning;
	return s->want;
}

/*
 * The recovery rtl_rxwd_step asked for has run. After a re-arm or a
 * restart the next stall counts again and moves up the ladder; after the
 * last one nothing more is tried or counted until RX moves.
 */
static inline void rtl_rxwd_done(struct rtl_rxwd *s)
{
	if (s->want == RTL_RXWD_A_GIVE_UP || s->want == RTL_RXWD_A_EXHAUSTED) {
		s->gave_up = 1;
		s->want = RTL_RXWD_A_NONE;
		return;
	}
	if (s->want == RTL_RXWD_A_NONE)
		return;
	s->tries++;
	s->want = RTL_RXWD_A_NONE;
	s->reported = 0;
	s->run = 0;
	s->primed = 0;          /* sample the counters again after the action */
}

#ifdef __cplusplus
extern "C" {
#endif

/* The radio is up: end of vendor probe and of wake */
void     rtl8814native_rxwd_start(void);
/* Before the radio goes down (remove, sleep, shutdown): no more ticks, and
 * a tick still running (with its recovery) is waited for */
void     rtl8814native_rxwd_stop(void);
/* RTL_RXWD_*; -1 for another value */
int      rtl8814native_rxwd_set_mode(uint32_t mode);
uint32_t rtl8814native_rxwd_mode(void);

/* VendorOsDep.c: level 1 on the adapter, offch_mutex and setch_mutex held.
 * URBs submitted again, or a negative errno */
int      rtl8814native_rx_ring_restart(void *adapter);
/* VendorOsDep.c: scans completed (not aborted) since kext load */
uint32_t rtl8814native_rx_scans_done(void);
/* VendorProbe.c: level 2; 0, or a negative errno with the radio left down */
int      rtl8814native_radio_restart(void);
/* RTL8814Native.cpp: RTLRxStalls / RTLRxLastStall on the owning instance */
void     rtl8814native_publish_rx_stalls(uint32_t count, uint64_t last_s);

#ifdef __cplusplus
}
#endif

#endif /* _RTL8814_RX_WATCHDOG_H */
