/*
 * RTL8814Native — the T9UH's blue status LED
 *
 * The LED hangs on GPIO 8, 9, 13 and 14, driven together through
 * REG_GPIO_PIN_CTRL_2 (0x60), as SwLedOn/SwLedOff_8814AU do: on = the four
 * pins as outputs (bits 16, 17, 21, 22) at 0 (bits 8, 9, 13, 14), off = the
 * same outputs at 1. Without the vendor SW LED machinery (CONFIG_RTW_SW_LED
 * stays off) nothing else writes those bits while the radio runs; the power
 * sequences set them at HAL init and card disable.
 *
 * StatusLed.c owns these 8 bits only: one 100 ms thread call, a register
 * write only when the level changes, and none while the radio is off its
 * channel (offch_state), switching channel (setch_mutex), joining, stopping
 * or before HAL init is complete. The tick stops before sleep, shutdown and
 * remove, and the 8 bits go back to what the hardware had.
 *
 * Boot-arg rtlled and knob RTL_KNOB_LED (rtl8814native_set_knob, test_uc knob
 * 3, the RTLLed property): RTL_LED_OFF never touches the register (the
 * behaviour before this file), RTL_LED_STATUS the patterns below (default),
 * RTL_LED_STEADY on while the radio is up.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_STATUS_LED_H
#define _RTL8814_STATUS_LED_H

#include <stdint.h>

#define RTL_LED_OFF        0
#define RTL_LED_STATUS     1
#define RTL_LED_STEADY     2

#define RTL_KNOB_LED       3        /* rtl8814native_set_knob id */

#define RTL_LED_TICK_MS    100
#define RTL_LED_CYCLE      20       /* ticks in the 2 s cycle of the slow blink and the AWDL flash */
#define RTL_LED_FLICK_US   300000   /* at most one traffic flicker this often */

/* REG_GPIO_PIN_CTRL_2 bits of the LED */
#define RTL_LED_REG        0x0060
#define RTL_LED_DIR_BITS   ((1u << 16) | (1u << 17) | (1u << 21) | (1u << 22))
#define RTL_LED_VAL_BITS   ((1u << 8) | (1u << 9) | (1u << 13) | (1u << 14))
#define RTL_LED_BITS       (RTL_LED_DIR_BITS | RTL_LED_VAL_BITS)
#define RTL_LED_ON_BITS    RTL_LED_DIR_BITS
#define RTL_LED_OFF_BITS   (RTL_LED_DIR_BITS | RTL_LED_VAL_BITS)

/*
 * Level of RTL_LED_STATUS at now_us (1 on, 0 off). The cycle is free running,
 * 2 s from uptime, so a skipped tick does not shift it:
 *   interface down (netif 0)  off
 *   AWDL peers with data      on, off, on, off in the first 4 ticks, then the
 *                             base pattern below without the traffic flicker
 *   not associated            on the first second, off the second (scans too)
 *   associated                on; data moved in the last tick and the last
 *                             flicker RTL_LED_FLICK_US or more ago: off for
 *                             this tick, *flick = 1
 * No kernel types, so it can be run on the host.
 */
static inline int rtl_led_status_level(uint64_t now_us, int netif, int linked, uint32_t awdl_peers,
                                       int moved, uint64_t flick_us, int *flick)
{
	uint32_t phase = (uint32_t)((now_us / (RTL_LED_TICK_MS * 1000ull)) % RTL_LED_CYCLE);

	*flick = 0;
	if (netif == 0)
		return 0;
	if (awdl_peers && phase < 4)
		return phase == 0 || phase == 2;
	if (!linked)
		return phase < RTL_LED_CYCLE / 2;
	if (!awdl_peers && moved && (!flick_us || now_us - flick_us >= RTL_LED_FLICK_US)) {
		*flick = 1;
		return 0;
	}
	return 1;
}

#ifdef __cplusplus
extern "C" {
#endif

/* The radio is up: end of vendor probe and of wake. */
void     rtl8814native_led_start(void);
/* Before the radio goes down (remove, sleep, shutdown): waits for a tick
 * still running, then puts the 8 bits back. */
void     rtl8814native_led_stop(void);
/* RTL_LED_*; -1 for another value */
int      rtl8814native_led_set_mode(uint32_t mode);
uint32_t rtl8814native_led_mode(void);
/* AWDL peers with a data session, from the layer (RTL_RADIO_FN_AWDL_PEERS);
 * 0 also when it turns port 1 or the AWDL hook off */
void     rtl8814native_led_awdl_peers(uint32_t n);
/* en2: 1 enabled, 0 disabled, -1 no interface (rtlair=2, or gone) */
void     rtl8814native_led_netif(int state);

#ifdef __cplusplus
}
#endif

#endif /* _RTL8814_STATUS_LED_H */
