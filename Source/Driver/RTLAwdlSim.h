/*
 * RTL8814Native — virtual AWDL peer port (boot-arg rtlawdl_sim=1)
 *
 * A userspace AWDL node (Tools/awdlsim) stands in for a peer on the air:
 * every AWDL frame the AirLayer hands to rtlRadioTx80211 is copied to a ring
 * the node reads, and every frame the node sends enters the AWDL RX hook as
 * if the radio had received it on the current operating channel. The
 * AirLayer and IO80211Family run unchanged. Off unless the boot-arg is set;
 * the user client selectors need root.
 *
 * Shared between the kext and the tools (test_uc, awdlsim): plain C, no
 * kernel types outside the KERNEL block.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL_AWDL_SIM_H
#define _RTL_AWDL_SIM_H

#include <stdint.h>

/* kRTLUC_AwdlSimCtl ops (scalar in: op, arg0, arg1) */
#define RTL_AWDL_SIM_OP_QUERY     0   /* counters only, any client */
#define RTL_AWDL_SIM_OP_ENABLE    1   /* arg0 channel (6/44/149); resets the ring and counters */
#define RTL_AWDL_SIM_OP_CHANNEL   2   /* owner: arg0 channel */
#define RTL_AWDL_SIM_OP_DISABLE   3   /* owner */
#define RTL_AWDL_SIM_OP_CAPTURE   4   /* frames of both directions go to the ring and
                                       * the radio behaves as with the sim off: channel
                                       * 0, normal data retries. Injection stays, only
                                       * for the owner's check of the RX tap */

/* kRTLUC_AwdlSimCtl scalar out, in this order */
enum {
	RTL_AWDL_SIM_C_ENABLED = 0,
	RTL_AWDL_SIM_C_CHANNEL,
	RTL_AWDL_SIM_C_TAPPED,
	RTL_AWDL_SIM_C_OVERRUNS,
	RTL_AWDL_SIM_C_INJECTED,
	RTL_AWDL_SIM_C_INJ_OFFCH,     /* the radio was on another channel */
	RTL_AWDL_SIM_C_INJ_NOHOOK,    /* no AWDL RX hook attached (awdl0 down, rtlawdl_rx=0) */
	RTL_AWDL_SIM_C_INJ_NOTREADY,  /* radio down, or a start/stop was running */
	RTL_AWDL_SIM_C_COUNT
};

#define RTL_AWDL_SIM_FRAME_MIN    24      /* 3-address 802.11 header */
#define RTL_AWDL_SIM_FRAME_MAX    1536
#define RTL_AWDL_SIM_RING         256
#define RTL_AWDL_SIM_WAIT_MAX_US  20000

/* kRTLUC_AwdlSimRead output: this header, then len frame bytes (802.11
 * header on, no FCS). Native endianness. */
struct rtl_awdl_sim_rec {
	uint64_t t_us;        /* mach_absolute_time in microseconds, at rtlRadioTx80211 */
	uint16_t len;
	uint8_t  channel;     /* operating channel when the frame was submitted */
	uint8_t  flags;       /* RTL_AWDL_SIM_F_* */
	int8_t   rssi;        /* dBm, received frames only */
	uint8_t  reserved[3];
};
#define RTL_AWDL_SIM_F_OVERRUN    0x01    /* records were overwritten before this one */
#define RTL_AWDL_SIM_F_RX         0x02    /* capture: received, not transmitted */
#define RTL_AWDL_SIM_F_TRUNC      0x04    /* capture: only the first RTL_AWDL_SIM_FRAME_MAX bytes */
#define RTL_AWDL_SIM_READ_MIN     (sizeof(struct rtl_awdl_sim_rec) + RTL_AWDL_SIM_FRAME_MAX)

#ifdef KERNEL
#ifdef __cplusplus
extern "C" {
#endif

/* AWDLSim.cpp. rtl_awdl_sim_init reads the boot-arg and allocates the ring
 * once (driver start, lifecycle lock held); nothing is freed afterwards. */
void    rtl_awdl_sim_init(void);
int     rtl_awdl_sim_available(void);
/* TX path (AWDLTx.c, txTick thread call): copies the frame when enabled.
 * Takes the sim lock only; never call it with a spinlock held. */
void    rtl_awdl_sim_tap(const uint8_t *frame, uint32_t len, uint8_t channel);
/* RX path (rtl8814native_awdl_rx, RX thread): copies the frame during a
 * capture. Takes the sim lock only. */
void    rtl_awdl_sim_tap_rx(const uint8_t *frame, uint32_t len, int8_t rssi, uint8_t channel);
/* The sim channel, 0 while disabled and during a capture. */
uint8_t rtl_awdl_sim_channel(void);
int     rtl_awdl_sim_capturing(void);
/* Radio released (stop, takeover): disable and wake every reader. */
void    rtl_awdl_sim_abort(void);

/* AWDLTx.c: the frame enters the AWDL RX hook. 0, -EINVAL (bad frame),
 * -ENODEV (radio down), -EAGAIN (operating channel is not the sim channel),
 * -ENOENT (no hook attached). */
int     rtl8814native_awdl_sim_rx(const uint8_t *frame, uint32_t len, int8_t rssi);

#ifdef __cplusplus
}

#include <IOKit/IOReturn.h>

/* User client side (RTLUserClient.cpp). `client` identifies the connection
 * that enabled the sim; only it may change, read or inject. */
IOReturn rtlAwdlSimCtl(const void *client, uint32_t op, uint64_t arg0,
                       uint64_t counters[RTL_AWDL_SIM_C_COUNT]);
/* One record into out (cap >= RTL_AWDL_SIM_READ_MIN), waiting up to
 * timeoutUs (0 polls). kIOReturnTimeout when none came, kIOReturnAborted
 * when the sim was disabled or the wait interrupted. */
IOReturn rtlAwdlSimRead(const void *client, uint32_t timeoutUs, void *out, uint32_t cap,
                        uint32_t *used);
bool     rtlAwdlSimOwns(const void *client);
void     rtlAwdlSimClientGone(const void *client);
void     rtlAwdlSimCount(unsigned idx);
#endif
#endif /* KERNEL */

#endif /* _RTL_AWDL_SIM_H */
