// Host test of AWDLShare.h.
//   clang++ -std=c++17 -Wall -fsanitize=address,undefined Tools/share_test.cpp -o /tmp/share_test && /tmp/share_test
#include <cstdio>
#include <cstdint>
#include "../Source/AirLayer/AWDLShare.h"

static int fails;
static void check(const char *what, bool ok)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		fails++;
	}
}

static int bits(uint16_t m)
{
	int n = 0;
	for (int i = 0; i < 16; i++)
		n += (m >> i) & 1;
	return n;
}

/* longest run of served slots, around the end of the sequence too */
static int longest(uint16_t m)
{
	int best = 0, run = 0;
	for (int n = 0; n < 32; n++) {
		if ((m >> (n % 16)) & 1) {
			if (++run > best)
				best = run;
		} else {
			run = 0;
		}
	}
	return best > 16 ? 16 : best;
}

int main()
{
	/* the idle sequence with the BSS on 153 (2026-11-11) */
	const uint8_t idle[16] = { 153, 0, 149, 0, 0, 0, 0, 0, 6, 0, 149, 0, 0, 0, 0, 0 };
	uint16_t m = awdlShareServe(idle, 149, 3);
	check("idle: 12 slots", bits(m) == 12);
	check("idle: runs of 3", longest(m) == 3);
	check("idle: the BSS's slot stays", !(m & 1));
	check("idle: channel 6's slot stays", !((m >> 8) & 1));
	check("idle: both 149 slots served", (m >> 2) & 1 && (m >> 10) & 1);

	/* the sequence the family gives with data and a BSS on 153 */
	const uint8_t data[16] = { 149, 149, 149, 149, 149, 149, 153, 153, 6, 149, 149, 149, 149, 149, 153, 153 };
	m = awdlShareServe(data, 149, 3);
	check("data: 9 slots", bits(m) == 9);
	check("data: runs of 3", longest(m) == 3);
	check("data: no slot of the BSS or of 6", !(m & ((1u << 6) | (1u << 7) | (1u << 8) | (1u << 14) | (1u << 15))));

	/* every slot a candidate */
	const uint8_t all[16] = { 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149 };
	for (uint32_t run = 1; run <= 20; run++) {
		m = awdlShareServe(all, 149, run);
		check("all: the BSS keeps a slot", bits(m) < 16);
		check("all: no run above the limit", longest(m) <= (int)(run > 15 ? 15 : run));
		check("all: something served", bits(m) > 0);
	}

	/* any sequence: never a slot of another channel, never a run above the limit */
	uint32_t x = 12345;
	for (int t = 0; t < 20000; t++) {
		uint8_t s[16];
		static const uint8_t pick[5] = { 0, 149, 153, 6, 44 };
		for (int i = 0; i < 16; i++) {
			x = x * 1103515245u + 12345u;
			s[i] = pick[(x >> 16) % 5];
		}
		uint32_t run = 1 + (x >> 8) % 6;
		m = awdlShareServe(s, 149, run);
		bool other = false, hasOther = false;
		for (int i = 0; i < 16; i++) {
			if (s[i] && s[i] != 149) {
				hasOther = true;
				if ((m >> i) & 1)
					other = true;
			}
		}
		check("random: no slot of another channel", !other);
		check("random: no run above the limit", longest(m) <= (int)run);
		check("random: the BSS keeps a slot", bits(m) < 16 || hasOther);
	}
	check("no target", awdlShareServe(idle, 0, 3) == 0);
	check("limit 0 turns it off", awdlShareServe(idle, 149, 0) == 0);

	/* the BSS's own slots: 153 in the data sequence, slot 0 in the idle one */
	check("bss: data sequence", awdlShareBss(data, 153, 0) == 0xC0C0);
	check("bss: idle sequence", awdlShareBss(idle, 153, 0) == 0x0001);
	check("bss: no channel", awdlShareBss(data, 0, 0) == 0);
	check("bss: a channel not in the sequence", awdlShareBss(data, 36, 0) == 0);
	check("bss: the social mask wins", awdlShareBss(data, 153, 0x4040) == 0x8080);
	/* the social channel's slots with dense presence never meet the BSS's */
	check("bss: apart from a transfer's slots",
	      !(awdlShareBss(idle, 153, 0) & awdlShareServe(idle, 149, 3)) &&
	      !(awdlShareBss(data, 153, 0) & awdlShareServe(data, 149, 3)));

	/* which channel a tick serves */
	check("slot: the social channel stays", awdlShareSlotCh(149, 153, 0xC0C0, 6, 1) == 149);
	check("slot: the BSS in its slot", awdlShareSlotCh(0, 153, 0xC0C0, 6, 1) == 153);
	check("slot: the BSS in its last slot", awdlShareSlotCh(0, 153, 0xC0C0, 15, 1) == 153);
	check("slot: not while away", awdlShareSlotCh(0, 153, 0xC0C0, 6, 0) == 0);
	check("slot: not in another slot", awdlShareSlotCh(0, 153, 0xC0C0, 5, 1) == 0);
	check("slot: not without a BSS channel", awdlShareSlotCh(0, 0, 0xC0C0, 6, 1) == 0);
	check("slot: not past the sequence", awdlShareSlotCh(0, 153, 0xFFFF, 16, 1) == 0);

	/* the plan: 149 (class 128) in its slots, 153 (128) in the BSS's, 0 elsewhere */
	uint8_t pc[16], po[16];
	uint16_t soc = 0;
	for (int i = 0; i < 16; i++)
		if (data[i] == 149 && data[(i + 15) % 16] != 149)
			soc |= (uint16_t)(1u << i);
	uint16_t bss = awdlShareBss(data, 153, soc);
	awdlSharePlan(pc, po, 149, 128, soc, 153, 128, bss);
	bool planOk = true;
	for (int i = 0; i < 16; i++) {
		uint8_t want = (soc >> i) & 1 ? 149 : (bss >> i) & 1 ? 153 : 0;
		planOk &= pc[i] == want && po[i] == (want ? 128 : 0);
	}
	check("plan: both channels", planOk);
	check("plan: slots 0, 6, 9 and 15", pc[0] == 149 && pc[6] == 153 && pc[9] == 149 && pc[15] == 153 &&
	      pc[1] == 0 && pc[8] == 0);
	/* the same plan from either channel's frames: the arguments swapped */
	uint8_t qc[16], qo[16];
	awdlSharePlan(qc, qo, 153, 128, bss, 149, 128, soc);
	bool same = true;
	for (int i = 0; i < 16; i++)
		same &= qc[i] == pc[i] && qo[i] == po[i];
	check("plan: the same from either side", same);
	/* one channel only, as before: the second pair empty */
	awdlSharePlan(pc, po, 149, 124, 0x0404, 0, 0, 0);
	planOk = true;
	for (int i = 0; i < 16; i++)
		planOk &= pc[i] == (i == 2 || i == 10 ? 149 : 0) && po[i] == (i == 2 || i == 10 ? 124 : 0);
	check("plan: one channel", planOk);
	awdlSharePlan(pc, po, 0, 128, 0xFFFF, 153, 81, 0x0001);
	check("plan: no first channel", pc[0] == 153 && po[0] == 81 && pc[1] == 0 && po[1] == 0);
	awdlSharePlan(pc, po, 149, 128, 0x0001, 153, 81, 0x0001);
	check("plan: the first channel wins a shared slot", pc[0] == 149 && po[0] == 128);

	/* data per slot from the peers' plans: on the BSS's channel only with a
	 * recent plan that has it in the slot, on a social one as before */
	const uint8_t onB[16] = { 149, 149, 149, 149, 149, 149, 153, 153, 6, 149, 149, 149, 149, 149, 153, 153 };
	const uint8_t no153[16] = { 149, 0, 149, 0, 0, 0, 0, 0, 6, 0, 149, 0, 0, 0, 0, 0 };
	uint16_t pm[2];
	check("peer: unknown peer on B -> no", !awdlSharePeerOn(pm, 0, 6, 1));
	pm[0] = awdlSharePeerMask(onB, 153);
	check("peer: B in the slot -> yes", awdlSharePeerOn(pm, 1, 6, 1) && awdlSharePeerOn(pm, 1, 15, 1));
	check("peer: B in another slot -> no", !awdlSharePeerOn(pm, 1, 5, 1) && !awdlSharePeerOn(pm, 1, 0, 1));
	pm[0] = awdlSharePeerMask(no153, 153);
	check("peer: plan without B -> no", !awdlSharePeerOn(pm, 1, 6, 1));
	check("peer: social, unknown peer -> yes", awdlSharePeerOn(pm, 0, 3, 0));
	pm[0] = awdlSharePeerMask(no153, 149);
	check("peer: social, 149 in the slot -> yes", awdlSharePeerOn(pm, 1, 2, 0));
	check("peer: social, 149 in another slot -> no", !awdlSharePeerOn(pm, 1, 3, 0));
	pm[0] = awdlSharePeerMask(no153, 44);
	check("peer: social, plan without the channel -> yes", awdlSharePeerOn(pm, 1, 3, 0));
	/* a group address: every peer with B has it in the slot, and one exists */
	pm[0] = awdlSharePeerMask(onB, 153);
	pm[1] = awdlSharePeerMask(no153, 153);
	check("group: one peer with B, one without -> yes in B's slot", awdlSharePeerOn(pm, 2, 7, 1));
	check("group: ... no in another", !awdlSharePeerOn(pm, 2, 8, 1));
	const uint8_t onB0[16] = { 153, 0, 149, 0, 0, 0, 0, 0, 6, 0, 149, 0, 0, 0, 0, 0 };
	pm[1] = awdlSharePeerMask(onB0, 153);
	check("group: two peers with B in different slots -> no", !awdlSharePeerOn(pm, 2, 6, 1) &&
	      !awdlSharePeerOn(pm, 2, 0, 1));
	pm[0] = awdlSharePeerMask(no153, 153);
	check("group: none with B -> no", !awdlSharePeerOn(pm, 1, 6, 1));
	check("peer: no channel never matches", awdlSharePeerMask(no153, 0) == 0);
	/* social: the same answer as the loop peerOnChannel had before */
	for (int t = 0; t < 20000; t++) {
		uint8_t plans[3][16];
		uint16_t ms[3];
		static const uint8_t pick[4] = { 0, 149, 153, 6 };
		uint32_t n = t % 4;
		for (uint32_t k = 0; k < n; k++) {
			for (int i = 0; i < 16; i++) {
				x = x * 1103515245u + 12345u;
				plans[k][i] = pick[(x >> 16) % 4];
			}
			ms[k] = awdlSharePeerMask(plans[k], 149);
		}
		uint32_t slot = (x >> 4) % 16;
		uint16_t common = 0xFFFF;
		bool any = false;
		for (uint32_t k = 0; k < n; k++) {
			uint16_t mask = 0;
			for (int s = 0; s < 16; s++)
				if (plans[k][s] == 149)
					mask |= (uint16_t)(1u << s);
			if (mask) {
				common &= mask;
				any = true;
			}
		}
		bool old = !any || !common || ((common >> slot) & 1);
		check("random: social answer unchanged", (awdlSharePeerOn(ms, n, slot, 0) != 0) == old);
		check("random: strict only with knowledge",
		      !awdlSharePeerOn(ms, n, slot, 1) || (any && ((common >> slot) & 1)));
	}

	/* any sequence: the BSS's slots hold its channel only, never an idle slot
	 * nor one of the social channel's */
	for (int t = 0; t < 20000; t++) {
		uint8_t s[16];
		static const uint8_t pick[5] = { 0, 149, 153, 6, 44 };
		for (int i = 0; i < 16; i++) {
			x = x * 1103515245u + 12345u;
			s[i] = pick[(x >> 16) % 5];
		}
		uint16_t sm = awdlShareServe(s, 149, 1 + (x >> 8) % 6);
		uint16_t bm = awdlShareBss(s, 153, sm);
		bool bad = (bm & sm) != 0;
		for (int i = 0; i < 16; i++)
			if ((bm >> i) & 1)
				bad |= s[i] != 153;
			else
				bad |= s[i] == 153 && !((sm >> i) & 1);
		check("random: the BSS's slots exactly", !bad);
		awdlSharePlan(pc, po, 149, 128, sm, 153, 128, bm);
		bool p = true;
		for (int i = 0; i < 16; i++)
			p &= pc[i] == ((sm >> i) & 1 ? 149 : (bm >> i) & 1 ? 153 : 0);
		check("random: plan matches the masks", p);
	}

	printf(fails ? "%d failed\n" : "all passed\n", fails);
	return fails != 0;
}
