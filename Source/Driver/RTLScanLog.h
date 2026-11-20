/*
 * Scan step log: when each state of a scan began, the channels it visited and
 * the AWDL slots taken in its returns, read through kRTLUC_GetScanLog
 * (Tools/test_uc scanlog). Shared with test_uc. Written from the scan's
 * command thread and from the AWDL switch; an entry being written while it is
 * read can come out mixed.
 */
#ifndef RTL_SCAN_LOG_H
#define RTL_SCAN_LOG_H

#include <stdint.h>

#define RTL_SLOG_N 480

enum {
	SL_STATE = 1,   /* a scan state set: a: state (SCAN_*), b: channel index */
	SL_HDL,         /* the scan's command handler ran, not in a back-op's polls:
	                 * a: state on entry, b: channel index */
	SL_CH,          /* a channel's dwell starts: a: channel, b: dwell ms */
	SL_TXBUF,       /* HW_VAR_CHECK_TXBUF done: a: 0 at the scan's start, 1 on
	                 * leaving a return, b: time it took in units of 100 us */
	SL_OFFCH,       /* AWDL switch during a scan or in the second after it: a:
	                 * channel (0 = home), b: result (0 or the negated errno) */
	SL_CLOSE_WAIT,  /* a return is over but an AWDL slot still runs: b: ms into it */
	SL_STEP,        /* decided at the scan's start: a: one channel per return,
	                 * b: the unlinked hold */
	SL_MARK,        /* a step inside SCAN_BACKING_OP done: a: 1 channel set,
	                 * 2 MSR, 3 survey register, 4 IGI, 5 MAC IDs awake, 6 PM null */
	SL_PHY          /* a part of the HAL's channel set during a scan: a: 0 the set
	                 * begins (b: channel | width << 8), 1 channel, 2 width, 3 TX
	                 * power, 4 IQK; b: time it took in units of 10 us */
};

struct rtl_slog_ev {
	uint32_t t_us;  /* uptime in us, low 32 bits */
	uint8_t  ev;
	uint8_t  a;
	uint16_t b;
};

struct rtl_slog {
	uint32_t next;  /* events logged so far; slot = index % RTL_SLOG_N */
	uint32_t pad;
	struct rtl_slog_ev e[RTL_SLOG_N];
};

#endif
