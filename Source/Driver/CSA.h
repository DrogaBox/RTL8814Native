/*
 * 802.11 channel switch announcement parsing, kept free of kernel types so it can be tested on
 * the host (Tools/csa_test.c).
 */
#ifndef RTL_CSA_H
#define RTL_CSA_H

#include <stdint.h>

#define RTL_CSA_NO_WIDE 0xff

struct rtl_csa {
	uint8_t  found;         /* a Channel Switch Announcement (37) or Extended one (60) was seen */
	uint8_t  mode;          /* 1 = the AP asks stations to stop transmitting until the switch */
	uint8_t  channel;       /* new primary channel */
	uint8_t  count;         /* beacons left on the old channel; 0 = switch at any time */
	uint8_t  sec_offset;    /* Secondary Channel Offset IE: 0 none, 1 above, 3 below */
	uint8_t  wide_width;    /* Wide Bandwidth Channel Switch: 0 20/40, 1 80, 2 160, 3 80+80, or RTL_CSA_NO_WIDE */
	uint8_t  wide_seg0;     /* its new center frequency segment 0 channel */
};

/* Parses the elements of a beacon, probe response or channel switch action frame body. */
void rtl_csa_parse(const uint8_t *ies, uint32_t len, struct rtl_csa *out);

/* Bandwidth to use on the new channel: 0 = 20 MHz, 1 = 40, 2 = 80 (the driver's CHANNEL_WIDTH_*).
 * Never wider than cur_bw. */
uint8_t rtl_csa_pick_bw(const struct rtl_csa *csa, uint8_t cur_bw);

#endif
