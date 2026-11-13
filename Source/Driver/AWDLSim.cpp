/*
 * RTL8814Native — virtual AWDL peer port (RTLAwdlSim.h)
 *
 * One ring of the AWDL frames the AirLayer submits, read by the client that
 * enabled the sim. A capture (RTL_AWDL_SIM_OP_CAPTURE) uses the same ring and
 * adds the frames the radio receives, for a test with a real peer. The lock and the ring are allocated once, at the first
 * driver start with rtlawdl_sim set, and never freed: a tap or a reader
 * racing a disable or a USB takeover only ever sees enabled go false.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "RTLAwdlSim.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>

extern "C" uint64_t rtl8814native_uptime_us(void);

struct SimSlot {
	struct rtl_awdl_sim_rec h;
	uint8_t bytes[RTL_AWDL_SIM_FRAME_MAX];
};

static IOLock *s_lock;
static SimSlot *s_ring;
static uint64_t s_next;         /* records written since the enable */
static uint64_t s_read;         /* next record the reader gets */
static bool s_overrun;          /* records lost since the last read */
static bool s_enabled;
static bool s_capture;          /* s_enabled, and the ring also takes RX */
static const void *s_owner;
static uint8_t s_channel;
static uint64_t s_count[RTL_AWDL_SIM_C_COUNT];
static int s_event;             /* wait channel */

static bool simChannelValid(uint64_t ch)
{
	return ch == 6 || ch == 44 || ch == 149;
}

static void disableLocked(void)
{
	s_enabled = false;
	s_capture = false;
	s_owner = nullptr;
	s_channel = 0;
	IOLockWakeup(s_lock, &s_event, false);
}

extern "C" void rtl_awdl_sim_init(void)
{
	if (s_lock)
		return;
	uint32_t on = 0;
	if (!PE_parse_boot_argn("rtlawdl_sim", &on, sizeof(on)) || !on)
		return;
	SimSlot *ring = (SimSlot *)IOMalloc(sizeof(SimSlot) * RTL_AWDL_SIM_RING);
	IOLock *l = IOLockAlloc();
	if (!ring || !l) {
		if (ring)
			IOFree(ring, sizeof(SimSlot) * RTL_AWDL_SIM_RING);
		if (l)
			IOLockFree(l);
		IOLog("RTL8814Native: awdl sim: no memory\n");
		return;
	}
	bzero(ring, sizeof(SimSlot) * RTL_AWDL_SIM_RING);
	s_ring = ring;
	/* Published last: everything else checks s_lock first. */
	__atomic_store_n(&s_lock, l, __ATOMIC_RELEASE);
	IOLog("RTL8814Native: awdl sim port ready (rtlawdl_sim)\n");
}

extern "C" int rtl_awdl_sim_available(void)
{
	return __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE) != nullptr;
}

/* A frame past the slot size only reaches here during a capture. */
static void tapLocked(const uint8_t *frame, uint32_t len, uint8_t channel, uint8_t flags,
                      int8_t rssi, uint64_t t)
{
	if (s_next - s_read >= RTL_AWDL_SIM_RING) {
		s_read++;
		s_overrun = true;
		s_count[RTL_AWDL_SIM_C_OVERRUNS]++;
	}
	if (len > RTL_AWDL_SIM_FRAME_MAX) {
		len = RTL_AWDL_SIM_FRAME_MAX;
		flags |= RTL_AWDL_SIM_F_TRUNC;
	}
	SimSlot *s = &s_ring[s_next % RTL_AWDL_SIM_RING];
	s->h.t_us = t;
	s->h.len = (uint16_t)len;
	s->h.channel = channel;
	s->h.flags = flags;
	s->h.rssi = rssi;
	bzero(s->h.reserved, sizeof(s->h.reserved));
	memcpy(s->bytes, frame, len);
	s_next++;
	s_count[RTL_AWDL_SIM_C_TAPPED]++;
	IOLockWakeup(s_lock, &s_event, false);
}

extern "C" void rtl_awdl_sim_tap(const uint8_t *frame, uint32_t len, uint8_t channel)
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	/* Unlocked peek: the common case (sim off) costs one load. */
	if (!l || !s_enabled || !frame || len < RTL_AWDL_SIM_FRAME_MIN ||
	    (len > RTL_AWDL_SIM_FRAME_MAX && !s_capture))
		return;
	uint64_t t = rtl8814native_uptime_us();
	IOLockLock(l);
	if (s_enabled && (len <= RTL_AWDL_SIM_FRAME_MAX || s_capture))
		tapLocked(frame, len, channel, 0, 0, t);
	IOLockUnlock(l);
}

extern "C" void rtl_awdl_sim_tap_rx(const uint8_t *frame, uint32_t len, int8_t rssi,
                                    uint8_t channel)
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	if (!l || !s_capture || !frame || len < RTL_AWDL_SIM_FRAME_MIN)
		return;
	uint64_t t = rtl8814native_uptime_us();
	IOLockLock(l);
	if (s_capture)
		tapLocked(frame, len, channel, RTL_AWDL_SIM_F_RX, rssi, t);
	IOLockUnlock(l);
}

extern "C" uint8_t rtl_awdl_sim_channel(void)
{
	return s_enabled ? s_channel : 0;
}

extern "C" int rtl_awdl_sim_capturing(void)
{
	return s_capture;
}

extern "C" void rtl_awdl_sim_abort(void)
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	if (!l)
		return;
	IOLockLock(l);
	if (s_enabled) {
		disableLocked();
		IOLog("RTL8814Native: awdl sim: radio released, disabled\n");
	}
	IOLockUnlock(l);
}

IOReturn rtlAwdlSimCtl(const void *client, uint32_t op, uint64_t arg0,
                       uint64_t counters[RTL_AWDL_SIM_C_COUNT])
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	if (!l)
		return kIOReturnUnsupported;
	if ((op == RTL_AWDL_SIM_OP_ENABLE || op == RTL_AWDL_SIM_OP_CHANNEL) &&
	    !simChannelValid(arg0))
		return kIOReturnBadArgument;

	IOReturn ret = kIOReturnSuccess;
	IOLockLock(l);
	switch (op) {
	case RTL_AWDL_SIM_OP_QUERY:
		break;
	case RTL_AWDL_SIM_OP_ENABLE:
	case RTL_AWDL_SIM_OP_CAPTURE:
		if (s_enabled && s_owner != client) {
			ret = kIOReturnExclusiveAccess;
			break;
		}
		s_owner = client;
		s_capture = op == RTL_AWDL_SIM_OP_CAPTURE;
		s_channel = s_capture ? 0 : (uint8_t)arg0;
		s_next = s_read = 0;
		s_overrun = false;
		bzero(s_count, sizeof(s_count));
		s_enabled = true;
		if (s_capture)
			IOLog("RTL8814Native: awdl sim: capture on\n");
		else
			IOLog("RTL8814Native: awdl sim: enabled on channel %u\n", s_channel);
		break;
	case RTL_AWDL_SIM_OP_CHANNEL:
		if (!s_enabled || s_owner != client || s_capture)
			ret = kIOReturnExclusiveAccess;
		else
			s_channel = (uint8_t)arg0;
		break;
	case RTL_AWDL_SIM_OP_DISABLE:
		if (!s_enabled || s_owner != client) {
			ret = kIOReturnExclusiveAccess;
			break;
		}
		disableLocked();
		IOLog("RTL8814Native: awdl sim: disabled, tapped %llu injected %llu\n",
		      s_count[RTL_AWDL_SIM_C_TAPPED], s_count[RTL_AWDL_SIM_C_INJECTED]);
		break;
	default:
		ret = kIOReturnBadArgument;
		break;
	}
	if (counters) {
		memcpy(counters, s_count, sizeof(s_count));
		counters[RTL_AWDL_SIM_C_ENABLED] = s_enabled;
		counters[RTL_AWDL_SIM_C_CHANNEL] = s_channel;
	}
	IOLockUnlock(l);
	return ret;
}

IOReturn rtlAwdlSimRead(const void *client, uint32_t timeoutUs, void *out, uint32_t cap,
                        uint32_t *used)
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	if (!l)
		return kIOReturnUnsupported;
	if (!out || cap < RTL_AWDL_SIM_READ_MIN || !used)
		return kIOReturnBadArgument;
	if (timeoutUs > RTL_AWDL_SIM_WAIT_MAX_US)
		timeoutUs = RTL_AWDL_SIM_WAIT_MAX_US;
	uint64_t deadline = 0;
	if (timeoutUs)
		clock_interval_to_deadline(timeoutUs, kMicrosecondScale, &deadline);

	IOReturn ret;
	IOLockLock(l);
	for (;;) {
		if (!s_enabled || s_owner != client) {
			ret = kIOReturnAborted;
			break;
		}
		if (s_read != s_next) {
			/* Copied under the lock: a tap cannot overwrite it meanwhile. */
			const SimSlot *s = &s_ring[s_read % RTL_AWDL_SIM_RING];
			struct rtl_awdl_sim_rec h = s->h;
			if (s_overrun)
				h.flags |= RTL_AWDL_SIM_F_OVERRUN;
			s_overrun = false;
			memcpy(out, &h, sizeof(h));
			memcpy((uint8_t *)out + sizeof(h), s->bytes, h.len);
			*used = (uint32_t)sizeof(h) + h.len;
			s_read++;
			ret = kIOReturnSuccess;
			break;
		}
		if (!timeoutUs) {
			ret = kIOReturnTimeout;
			break;
		}
		wait_result_t wr = IOLockSleepDeadline(l, &s_event, deadline, THREAD_ABORTSAFE);
		if (wr == THREAD_TIMED_OUT)
			timeoutUs = 0;          /* one last look, then kIOReturnTimeout */
		else if (wr == THREAD_INTERRUPTED) {
			ret = kIOReturnAborted;
			break;
		}
	}
	IOLockUnlock(l);
	return ret;
}

bool rtlAwdlSimOwns(const void *client)
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	if (!l || !client)
		return false;
	IOLockLock(l);
	bool owns = s_enabled && s_owner == client;
	IOLockUnlock(l);
	return owns;
}

void rtlAwdlSimClientGone(const void *client)
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	if (!l || !client)
		return;
	IOLockLock(l);
	if (s_enabled && s_owner == client) {
		disableLocked();
		IOLog("RTL8814Native: awdl sim: owner closed, disabled\n");
	}
	IOLockUnlock(l);
}

void rtlAwdlSimCount(unsigned idx)
{
	IOLock *l = __atomic_load_n(&s_lock, __ATOMIC_ACQUIRE);
	if (!l || idx >= RTL_AWDL_SIM_C_COUNT)
		return;
	IOLockLock(l);
	s_count[idx]++;
	IOLockUnlock(l);
}
