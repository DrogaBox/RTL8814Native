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

	printf(fails ? "%d failed\n" : "all passed\n", fails);
	return fails != 0;
}
