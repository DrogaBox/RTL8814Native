#include <stdio.h>
#include <string.h>
#include "CSA.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
	struct rtl_csa c;
	/* CSA mode 1, ch 149, count 3 + secondary offset above */
	const uint8_t a[] = { 37, 3, 1, 149, 3, 62, 1, 1 };
	/* CSA ch 36 count 0 + wrapper with wide bandwidth (80 MHz, seg0 42) */
	const uint8_t b[] = { 37, 3, 0, 36, 0, 196, 5, 194, 3, 1, 42, 0 };
	/* only an Extended CSA, 2.4 GHz */
	const uint8_t d[] = { 60, 4, 0, 81, 6, 2 };
	/* truncated element must not be read */
	const uint8_t t[] = { 37, 3, 1, 44 };
	const uint8_t none[] = { 0, 4, 'a', 'b', 'c', 'd' };

	rtl_csa_parse(a, sizeof(a), &c);
	CHECK(c.found && c.mode == 1 && c.channel == 149 && c.count == 3 && c.sec_offset == 1);
	CHECK(c.wide_width == RTL_CSA_NO_WIDE);
	CHECK(rtl_csa_pick_bw(&c, 2) == 1);
	CHECK(rtl_csa_pick_bw(&c, 0) == 0);

	rtl_csa_parse(b, sizeof(b), &c);
	CHECK(c.found && c.channel == 36 && c.count == 0 && c.wide_width == 1 && c.wide_seg0 == 42);
	CHECK(rtl_csa_pick_bw(&c, 2) == 2);
	CHECK(rtl_csa_pick_bw(&c, 1) == 1);

	rtl_csa_parse(d, sizeof(d), &c);
	CHECK(c.found && c.channel == 6 && c.count == 2);
	CHECK(rtl_csa_pick_bw(&c, 1) == 0);

	rtl_csa_parse(t, sizeof(t), &c);
	CHECK(!c.found);
	rtl_csa_parse(none, sizeof(none), &c);
	CHECK(!c.found);
	rtl_csa_parse(a, 0, &c);
	CHECK(!c.found);

	/* the IE 37 wins over an extended one, whichever comes first */
	{
		const uint8_t e[] = { 60, 4, 0, 115, 100, 1, 37, 3, 0, 52, 1 };
		rtl_csa_parse(e, sizeof(e), &c);
		CHECK(c.channel == 52 && c.count == 1);
	}
	printf(fails ? "FAILED\n" : "all passed\n");
	return fails != 0;
}
