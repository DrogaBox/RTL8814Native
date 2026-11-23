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
 * social channels) are never taken for it; the BSS's own are served on the BSS
 * (awdlShareBss).
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

/*
 * The BSS's own channel b in the 129's sequence. The family puts the infra
 * channel in some slots of its plan (2026-11-22, BSS on 153: 149 149 149 149
 * 149 149 153 153 6 149 149 149 149 149 153 153), and a peer associated to
 * the same router sends AirDrop data in its router-channel slots as well. While
 * the radio sits on the BSS those slots are served there: the ones the
 * sequence gives b, never an idle one (dense presence takes those for the
 * social channel) and never one of socMask, the social channel's.
 */
static inline uint16_t awdlShareBss(const uint8_t *seq, uint8_t b, uint16_t socMask)
{
	uint16_t m = 0;
	if (!b)
		return 0;
	for (int i = 0; i < AWDL_SHARE_SLOTS; i++)
		if (seq[i] == b)
			m |= (uint16_t)(1u << i);
	return (uint16_t)(m & ~socMask);
}

/*
 * The channel a tick serves slot on: ch when the radio is on a social channel
 * for it (0 = none), else b when the radio is on the BSS (home) and the slot is
 * one of bssMask, else 0.
 */
static inline uint8_t awdlShareSlotCh(uint8_t ch, uint8_t b, uint16_t bssMask, uint32_t slot, int home)
{
	if (ch)
		return ch;
	return b && home && slot < AWDL_SHARE_SLOTS && ((bssMask >> slot) & 1) ? b : 0;
}

/* The slots a peer's plan (16 channels, notePeerSeq) puts on ch. */
static inline uint16_t awdlSharePeerMask(const uint8_t *plan, uint8_t ch)
{
	uint16_t m = 0;
	for (int i = 0; i < AWDL_SHARE_SLOTS; i++)
		if (ch && plan[i] == ch)
			m |= (uint16_t)(1u << i);
	return m;
}

/*
 * Whether data may go in slot, from the masks (awdlSharePeerMask) of the n
 * recent peers it is for: the destination, or every peer for a group address.
 * On a social channel not knowing never stops the data: yes with no peer on
 * ch, or no slot common to them all. strict, off the social channels (the
 * BSS's own), needs the opposite: at least one peer with ch in its plan, and
 * slot common to every such peer. A peer that only uses 149 (another router,
 * or none) never hears a frame sent on the BSS's channel, and each one costs
 * the air of all its tries.
 */
static inline int awdlSharePeerOn(const uint16_t *masks, uint32_t n, uint32_t slot, int strict)
{
	uint16_t common = 0xFFFF;
	int any = 0;
	for (uint32_t i = 0; i < n; i++)
		if (masks[i]) {
			common &= masks[i];
			any = 1;
		}
	int here = slot < AWDL_SHARE_SLOTS && ((common >> slot) & 1);
	return strict ? any && here : !any || !common || here;
}

/*
 * The plan TLV 4 and 18 announce, per slot a channel and its operating class:
 * c1 with oc1 in the slots of m1, c2 with oc2 in those of m2, 0 elsewhere. A
 * slot in both masks goes to c1. Frames sent on either channel carry the same
 * plan, so a peer sees every slot served.
 */
static inline void awdlSharePlan(uint8_t ch[AWDL_SHARE_SLOTS], uint8_t oc[AWDL_SHARE_SLOTS],
                                 uint8_t c1, uint8_t oc1, uint16_t m1,
                                 uint8_t c2, uint8_t oc2, uint16_t m2)
{
	for (int i = 0; i < AWDL_SHARE_SLOTS; i++) {
		int a = c1 && ((m1 >> i) & 1), b = c2 && ((m2 >> i) & 1);
		ch[i] = a ? c1 : b ? c2 : 0;
		oc[i] = a ? oc1 : b ? oc2 : 0;
	}
}

#endif
