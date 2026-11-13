/*
 * Slots AWDL takes from a radio it shares with a BSS while a transfer runs.
 * No kernel dependencies: Tools/share_test.cpp builds it on the host.
 *
 * Outside a transfer the layer serves the first slot of each run of the
 * social channel in the 129's sequence, two slots of sixteen with the idle
 * sequence (measured 2026-11-11: 785 kB/s shared against 7.5 MB/s parked).
 * During one it takes the slots the sequence gives that channel and the idle
 * ones, in runs of at most maxRun, and leaves the slot after each run to the
 * BSS. The slots the sequence gives another channel (the BSS's own, the other
 * social channels) are never taken.
 */
#ifndef AWDL_SHARE_H
#define AWDL_SHARE_H

#include <stdint.h>

#define AWDL_SHARE_SLOTS 16

static inline uint16_t awdlShareServe(const uint8_t *seq, uint8_t target, uint32_t maxRun)
{
	uint16_t cand = 0, serve = 0;
	int start = 0, fixed = 0;
	if (!target || !maxRun)
		return 0;
	for (int i = 0; i < AWDL_SHARE_SLOTS; i++)
		if (seq[i] == target || !seq[i])
			cand |= (uint16_t)(1u << i);
	/* Runs are counted from a slot that stays with the BSS, so none wraps
	 * around the sequence's end. */
	for (int i = 0; i < AWDL_SHARE_SLOTS; i++)
		if (!((cand >> i) & 1)) {
			start = (i + 1) % AWDL_SHARE_SLOTS;
			fixed = 1;
			break;
		}
	uint32_t run = 0;
	for (int n = 0; n < AWDL_SHARE_SLOTS; n++) {
		int i = (start + n) % AWDL_SHARE_SLOTS;
		if (((cand >> i) & 1) && run < maxRun) {
			serve |= (uint16_t)(1u << i);
			run++;
		} else {
			run = 0;
		}
	}
	/* Every slot a candidate: the last run meets the first one, so the
	 * sequence's last slot goes to the BSS when no other does. */
	if (!fixed && run)
		serve &= (uint16_t)~(1u << (AWDL_SHARE_SLOTS - 1));
	return serve;
}

#endif
