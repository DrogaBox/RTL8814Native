/*
 * Host test of the StatusLed pattern (rtl_led_status_level in Source/Driver/StatusLed.h).
 * clang -std=c11 -Wall -Wextra -ISource/Driver Tools/statusled_test.c -o Build/tests/statusled_test
 */
#include <stdio.h>
#include <string.h>
#include "StatusLed.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* simulate the tick: level cache, flicker commit, count writes */
static void run(const char *name, int linked, unsigned peers, int traffic, int ticks, char *out)
{
	int level = -1, writes = 0;
	unsigned long long flick = 0;
	for (int t = 0; t < ticks; t++) {
		unsigned long long now = 1000000ull * 50 + (unsigned long long)t * 100000ull;
		int f;
		int want = rtl_led_status_level(now, -1, linked, peers, traffic, flick, &f);
		if (want != level) { writes++; level = want; if (f) flick = now; }
		out[t] = level ? '#' : '.';
	}
	out[ticks] = 0;
	printf("%-28s %s  writes %d\n", name, out, writes);
}

int main(void)
{
	char buf[128];
	int f;
	run("unlinked", 0, 0, 0, 40, buf);
	CHECK(!strcmp(buf, "##########..........##########.........."));
	run("unlinked traffic", 0, 0, 1, 40, buf);
	CHECK(!strcmp(buf, "##########..........##########.........."));
	run("linked idle", 1, 0, 0, 40, buf);
	CHECK(!strchr(buf, '.'));
	run("linked traffic", 1, 0, 1, 40, buf);
	CHECK(!strncmp(buf, ".##.##.##", 9));
	run("linked + awdl", 1, 1, 1, 40, buf);
	CHECK(!strcmp(buf, "#.#.#################.#.################"));
	run("unlinked + awdl", 0, 2, 0, 40, buf);
	CHECK(!strcmp(buf, "#.#.######..........#.#.######.........."));
	CHECK(rtl_led_status_level(0, 0, 1, 1, 1, 0, &f) == 0 && f == 0);   /* interface down */
	CHECK(RTL_LED_BITS == 0x00636300u);
	CHECK(RTL_LED_ON_BITS == 0x00630000u && RTL_LED_OFF_BITS == 0x00636300u);
	/* idle value read 0x00000002: on = 0x00630002, off = 0x00636302, back = 0x00000002 */
	unsigned v = 0x00000002;
	CHECK(((v & ~RTL_LED_BITS) | RTL_LED_ON_BITS) == 0x00630002u);
	CHECK(((v & ~RTL_LED_BITS) | RTL_LED_OFF_BITS) == 0x00636302u);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
