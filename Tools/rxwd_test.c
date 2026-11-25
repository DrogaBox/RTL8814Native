/*
 * Host test of the RX watchdog's decision (rtl_rxwd_step / rtl_rxwd_done in
 * Source/Driver/RxWatchdog.h): legitimate silence never counts, the two
 * stall kinds do, one count per silent run, the recovery ladder per mode.
 * clang -std=c11 -Wall -Wextra -ISource/Driver Tools/rxwd_test.c -o Build/tests/rxwd_test
 */
#include <stdio.h>
#include <string.h>
#include "RxWatchdog.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

#define RING 8

/* one radio, ticked once a second */
struct sim {
	struct rtl_rxwd st;
	struct rtl_rxwd_in in;
	int stalls;             /* counted so far */
	int acts[8];            /* recoveries asked for, by RTL_RXWD_A_* */
	int last_reason;
	int gate_closed;        /* the caller cannot run a recovery now */
};

static void sim_init(struct sim *s, uint32_t mode)
{
	memset(s, 0, sizeof(*s));
	s->in.mode = mode;
	s->in.ready = 1;
	s->in.ring = RING;
	s->in.pending = RING;
}

/* one tick; frames > 0 moves the completion counter. Returns the action. */
static uint32_t tick(struct sim *s, int frames)
{
	struct rtl_rxwd_out out;
	uint32_t act;

	s->in.comp += (uint32_t)frames;
	act = rtl_rxwd_step(&s->st, &s->in, &out);
	if (out.stall) {
		s->stalls++;
		s->last_reason = (int)out.reason;
		CHECK(out.ticks >= RTL_RXWD_STALL_TICKS);
	}
	if (act != RTL_RXWD_A_NONE && !s->gate_closed) {
		s->acts[act]++;
		rtl_rxwd_done(&s->st);
	}
	return act;
}

static void ticks(struct sim *s, int n, int frames)
{
	while (n--)
		tick(s, frames);
}

int main(void)
{
	struct sim s;
	int i;

	/* 1. linked and healthy: ~11 completions a second (measured 2026-11-24) */
	sim_init(&s, RTL_RXWD_LOG);
	s.in.linked_home = 1;
	ticks(&s, 120, 11);
	CHECK(s.stalls == 0);
	printf("%-44s stalls %d\n", "linked, beacons arriving", s.stalls);

	/* 2. linked on the BSS channel, every URB out, nothing completes:
	 *    counted after 2 silent ticks, once, however long it lasts */
	sim_init(&s, RTL_RXWD_LOG);
	s.in.linked_home = 1;
	ticks(&s, 5, 11);
	tick(&s, 0);
	CHECK(s.stalls == 0);
	tick(&s, 0);
	CHECK(s.stalls == 1 && s.last_reason == RTL_RXWD_R_STUCK);
	ticks(&s, 300, 0);
	CHECK(s.stalls == 1);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 0 && s.acts[RTL_RXWD_A_RESTART] == 0);
	/* RX back ends the run; a new silence counts again */
	ticks(&s, 3, 5);
	ticks(&s, 2, 0);
	CHECK(s.stalls == 2);
	printf("%-44s stalls %d\n", "linked, stuck ring (log only)", s.stalls);

	/* 3. linked but sampled on an AWDL slot every tick: no beacon due, no stall */
	sim_init(&s, RTL_RXWD_LOG);
	ticks(&s, 3, 11);
	s.in.linked_home = 0;
	ticks(&s, 60, 0);
	CHECK(s.stalls == 0);
	/* on the BSS channel at 2 of the silent ticks: counted then */
	s.in.linked_home = 1;
	tick(&s, 0);
	CHECK(s.stalls == 0);
	s.in.linked_home = 0;
	tick(&s, 0);
	CHECK(s.stalls == 0);
	s.in.linked_home = 1;
	tick(&s, 0);
	CHECK(s.stalls == 1);
	printf("%-44s stalls %d\n", "linked, AWDL away at the samples", s.stalls);

	/* 4. unlinked and parked, Wi-Fi off or a quiet channel: silence is legit */
	sim_init(&s, RTL_RXWD_RESTART);
	ticks(&s, 3, 2);
	ticks(&s, 600, 0);
	CHECK(s.stalls == 0);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 0);
	printf("%-44s stalls %d\n", "unlinked, parked, no scan", s.stalls);

	/* 5. unlinked, a whole scan inside the silent run: deaf (2026-11-11) */
	sim_init(&s, RTL_RXWD_LOG);
	ticks(&s, 3, 2);
	tick(&s, 0);
	s.in.scanning = 1;
	ticks(&s, 3, 0);
	CHECK(s.stalls == 0);
	s.in.scanning = 0;
	s.in.scans++;
	tick(&s, 0);
	CHECK(s.stalls == 1 && s.last_reason == RTL_RXWD_R_STUCK);
	printf("%-44s stalls %d\n", "unlinked, a full scan heard nothing", s.stalls);

	/* 6. a scan already running when the silence began does not count:
	 *    part of it may have been heard. The next whole one does. */
	sim_init(&s, RTL_RXWD_LOG);
	s.in.scanning = 1;
	ticks(&s, 3, 2);
	ticks(&s, 2, 0);
	s.in.scanning = 0;
	s.in.scans++;
	ticks(&s, 5, 0);
	CHECK(s.stalls == 0);
	s.in.scanning = 1;
	ticks(&s, 4, 0);
	s.in.scanning = 0;
	s.in.scans++;
	tick(&s, 0);
	CHECK(s.stalls == 1);
	printf("%-44s stalls %d\n", "scan running at the start of the silence", s.stalls);

	/* 7. a scan that ends in the first silent second: counted at the second */
	sim_init(&s, RTL_RXWD_LOG);
	ticks(&s, 3, 2);
	s.in.scans++;
	tick(&s, 0);
	CHECK(s.stalls == 0);
	tick(&s, 0);
	CHECK(s.stalls == 1);
	printf("%-44s stalls %d\n", "scan ends in the first silent tick", s.stalls);

	/* 8. empty ring (an in-place wake with DF_RX_BIT left set): no frame
	 *    can arrive, no evidence needed, unlinked */
	sim_init(&s, RTL_RXWD_LOG);
	ticks(&s, 3, 2);
	s.in.pending = 0;
	tick(&s, 0);
	CHECK(s.stalls == 0);
	tick(&s, 0);
	CHECK(s.stalls == 1 && s.last_reason == RTL_RXWD_R_EMPTY);
	ticks(&s, 100, 0);
	CHECK(s.stalls == 1);
	printf("%-44s stalls %d\n", "empty ring", s.stalls);

	/* 9. some URBs out: still receives, never judged, even linked */
	sim_init(&s, RTL_RXWD_RESTART);
	s.in.linked_home = 1;
	ticks(&s, 3, 2);
	s.in.pending = 5;
	ticks(&s, 100, 0);
	CHECK(s.stalls == 0);
	printf("%-44s stalls %d\n", "partial ring", s.stalls);

	/* 10. not ready and mode 0: nothing; the first ready tick only samples */
	sim_init(&s, RTL_RXWD_OFF);
	s.in.pending = 0;
	ticks(&s, 50, 0);
	CHECK(s.stalls == 0);
	sim_init(&s, RTL_RXWD_LOG);
	s.in.ready = 0;
	s.in.pending = 0;
	ticks(&s, 50, 0);
	CHECK(s.stalls == 0);
	s.in.ready = 1;
	tick(&s, 0);
	tick(&s, 0);
	CHECK(s.stalls == 0);
	tick(&s, 0);
	CHECK(s.stalls == 1);
	printf("%-44s stalls %d\n", "off, not ready, then ready", s.stalls);

	/* 11. rtlrxwd=2: three re-arms, then no more recovery and no more counts */
	sim_init(&s, RTL_RXWD_REARM);
	s.in.linked_home = 1;
	ticks(&s, 3, 11);
	ticks(&s, 60, 0);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 3);
	CHECK(s.acts[RTL_RXWD_A_EXHAUSTED] == 1);
	CHECK(s.acts[RTL_RXWD_A_RESTART] == 0 && s.acts[RTL_RXWD_A_GIVE_UP] == 0);
	CHECK(s.stalls == 4);
	printf("%-44s stalls %d, re-arms %d\n", "rtlrxwd=2 ladder", s.stalls, s.acts[RTL_RXWD_A_REARM]);

	/* 12. rtlrxwd=3: re-arm, 3 restarts, give up; then quiet */
	sim_init(&s, RTL_RXWD_RESTART);
	s.in.linked_home = 1;
	ticks(&s, 3, 11);
	ticks(&s, 120, 0);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 1);
	CHECK(s.acts[RTL_RXWD_A_RESTART] == RTL_RXWD_TRIES);
	CHECK(s.acts[RTL_RXWD_A_GIVE_UP] == 1);
	CHECK(s.stalls == 5);
	printf("%-44s stalls %d, restarts %d\n", "rtlrxwd=3 ladder", s.stalls,
	       s.acts[RTL_RXWD_A_RESTART]);

	/* 13. a re-arm that works: RX back, the tries stay until RX held for
	 *     RTL_RXWD_HEALTHY_TICKS, so a quick relapse goes on up the ladder */
	sim_init(&s, RTL_RXWD_RESTART);
	s.in.linked_home = 1;
	ticks(&s, 3, 11);
	ticks(&s, 2, 0);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 1);
	ticks(&s, 10, 11);
	ticks(&s, 3, 0);
	CHECK(s.acts[RTL_RXWD_A_RESTART] == 1);
	ticks(&s, RTL_RXWD_HEALTHY_TICKS + 1, 11);
	ticks(&s, 3, 0);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 2 && s.acts[RTL_RXWD_A_RESTART] == 1);
	printf("%-44s re-arms %d, restarts %d\n", "relapse and recovery", s.acts[RTL_RXWD_A_REARM],
	       s.acts[RTL_RXWD_A_RESTART]);

	/* 14. the gate closed (a hop, a channel set): the action waits, the
	 *     stall is not counted again; done once the gate opens */
	sim_init(&s, RTL_RXWD_REARM);
	s.in.linked_home = 1;
	ticks(&s, 3, 11);
	s.gate_closed = 1;
	tick(&s, 0);
	CHECK(tick(&s, 0) == RTL_RXWD_A_REARM);
	for (i = 0; i < 10; i++)
		CHECK(tick(&s, 0) == RTL_RXWD_A_REARM);
	CHECK(s.stalls == 1 && s.acts[RTL_RXWD_A_REARM] == 0);
	s.gate_closed = 0;
	tick(&s, 0);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 1);
	printf("%-44s stalls %d, re-arms %d\n", "gate closed, then open", s.stalls,
	       s.acts[RTL_RXWD_A_REARM]);

	/* 15. knob to log only while a recovery waits: dropped */
	sim_init(&s, RTL_RXWD_RESTART);
	s.in.linked_home = 1;
	ticks(&s, 3, 11);
	s.gate_closed = 1;
	ticks(&s, 2, 0);
	s.in.mode = RTL_RXWD_LOG;
	CHECK(tick(&s, 0) == RTL_RXWD_A_NONE);
	s.gate_closed = 0;
	ticks(&s, 20, 0);
	CHECK(s.acts[RTL_RXWD_A_REARM] == 0 && s.stalls == 1);
	printf("%-44s stalls %d\n", "mode lowered while waiting", s.stalls);

	/* 16. counters wrap */
	sim_init(&s, RTL_RXWD_LOG);
	s.in.linked_home = 1;
	s.in.comp = 0xFFFFFFF0u;
	s.in.scans = 0xFFFFFFFFu;
	ticks(&s, 10, 11);
	CHECK(s.stalls == 0);
	s.in.linked_home = 0;
	tick(&s, 0);
	s.in.scans++;
	tick(&s, 0);
	CHECK(s.stalls == 1);
	printf("%-44s stalls %d\n", "counter wrap", s.stalls);

	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
