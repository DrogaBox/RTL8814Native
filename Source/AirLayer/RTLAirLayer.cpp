/*
 * RTL8814AirLayer — see RTLAirLayer.hpp.
 *
 * All struct offsets and event IDs below were measured on macOS 26.x
 * (Darwin 25.6.0) from AppleBCMWLANCore / IO80211Family; see
 * Docs/PHASE5_IO80211.md, "RE results". Written from scratch, GPL-2.0.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "RTLAirLayer.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <libkern/OSByteOrder.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <net/ethernet.h>

/*
 * IOLog does not reach `log show` on this system and dmesg rotates within
 * minutes, so every line is also appended to the RTL8814Native node as the
 * RTLAirLayerLog array (read with `ioreg -r -c RTL8814Native -d 1`). The
 * strings live in the provider and survive this kext failing and unloading.
 */
#define kLogProp    "RTLAirLayerLog"
#define kLogMax     200
static IOService *gLogTarget;
static IOLock *gLogLock;

static void airLog(const char *fmt, ...) __printflike(1, 2);
static void airLog(const char *fmt, ...)
{
	char buf[192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	IOLog("RTL8814AirLayer: %s\n", buf);
	if (!gLogTarget || !gLogLock)
		return;
	OSString *line = OSString::withCString(buf);
	if (!line)
		return;
	IOLockLock(gLogLock);
	OSArray *old = OSDynamicCast(OSArray, gLogTarget->getProperty(kLogProp));
	OSArray *arr = old ? OSArray::withArray(old, kLogMax + 1) : OSArray::withCapacity(16);
	if (arr) {
		while (arr->getCount() >= kLogMax)
			arr->removeObject(0);
		arr->setObject(line);
		gLogTarget->setProperty(kLogProp, arr);
		arr->release();
	}
	IOLockUnlock(gLogLock);
	line->release();
}

#define LOG(fmt, ...) airLog(fmt, ##__VA_ARGS__)

static uint64_t uptimeMs()
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000000ULL;
}

/* Trace for the 0.8 radio stall: the first call of each IO80211 stub is
 * recorded lock-free (stubs may run in any context) and written to the log
 * by the nub's flush thread call, with the radio STATUS call count. */
#define kStubTraceMax 256
static const char *gStubSeen[kStubTraceMax];
static uint64_t gStubSeenMs[kStubTraceMax];
static volatile SInt32 gStubN, gStubFlushed;
static volatile SInt32 gStatusCalls;
static volatile uint32_t gPowerSets, gPowerLast;

static void stubSeen(const char *fn)
{
	SInt32 i = OSIncrementAtomic(&gStubN);
	if (i < kStubTraceMax) {
		gStubSeenMs[i] = uptimeMs();
		gStubSeen[i] = fn;
	}
}

#define STUB_ENTER() do { static volatile UInt8 seen_; \
	if (!OSTestAndSet(0, &seen_)) stubSeen(__func__); } while (0)

static void flushTrace()
{
	SInt32 n = gStubN;
	if (n > kStubTraceMax)
		n = kStubTraceMax;
	while (gStubFlushed < n) {
		SInt32 i = gStubFlushed;
		if (!gStubSeen[i])
			break;          /* slot claimed, pointer not stored yet */
		airLog("@%llu stub %s", gStubSeenMs[i], gStubSeen[i]);
		gStubFlushed = i + 1;
	}
}

/* ---- IO80211 / Skywalk entry points that are not virtual (25.6.0 names) ---- */
extern "C" {
bool rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
	__asm("__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm");
IOReturn rtl_infra_registerEth(void *iface, void *reg, void **queues, uint32_t n,
                               void *txPool, void *rxPool)
	__asm("__ZN21IO80211InfraInterface30registerInfraEthernetInterfaceEPN26IOSkywalkEthernetInterface16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_");
void rtl_sky_setRole(void *iface, uint32_t role)
	__asm("__ZN23IO80211SkywalkInterface16setInterfaceRoleEj");
void rtl_sky_setId(void *iface, uint32_t id)
	__asm("__ZN23IO80211SkywalkInterface14setInterfaceIdEj");
void *rtl_sky_getWorkQueue(void *iface)
	__asm("__ZN23IO80211SkywalkInterface12getWorkQueueEv");
uintptr_t rtl_ctrl_postMessage(void *ctrl, void *iface, uint32_t msg, void *data,
                               unsigned long len, bool toWcl)
	__asm("__ZN17IO80211Controller11postMessageEP23IO80211SkywalkInterfacejPvmb");
void *rtl_pool_withName(const char *name, OSObject *owner, uint32_t type, const void *opts)
	__asm("__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE");
void *rtl_txsub_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                         void *action, void *refCon, uint32_t opts)
	__asm("__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j");
void *rtl_txcomp_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                          void *action, void *refCon, uint32_t opts)
	__asm("__ZN26IOSkywalkTxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
void *rtl_rxcomp_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                          void *action, void *refCon, uint32_t opts)
	__asm("__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
}

/* CoreCapture (com.apple.driver.corecapture) and IO80211FaultReporter, 25.6.0 names. */
extern "C" {
void *rtl_cc_pipe(IOService *owner, const char *ownerName, const char *name, const void *opts)
	__asm("__ZN6CCPipe21withOwnerNameCapacityEP9IOServicePKcS3_PK13CCPipeOptions");
void *rtl_cc_stream(void *pipe, const char *name, const void *opts)
	__asm("__ZN8CCStream15withPipeAndNameEP6CCPipePKcPK15CCStreamOptions");
void rtl_cc_startPipe(void *pipe) __asm("__ZN6CCPipe9startPipeEv");
void *rtl_cc_faultReporter(void *dataStream, IOWorkLoop *wl)
	__asm("__ZN15CCFaultReporter18withStreamWorkloopEP12CCDataStreamP10IOWorkLoop");
void rtl_cc_addOwner(void *fr, const char *owner) __asm("__ZN15CCFaultReporter8addOwnerEPKc");
void *rtl_io_faultReporter(void *ccFaultReporter)
	__asm("__ZN20IO80211FaultReporter15allocWithParamsEP15CCFaultReporter");
void *rtl_io_workQueue(void) __asm("__ZN16IO80211WorkQueue9workQueueEv");
}

/* Created once and never released: IO80211WorkQueue::workQueue stores the first
 * queue in the family global IO80211DefaultWorkQueue, which would dangle. */
static IO80211WorkQueue *gWorkQueue;

/* Packet accessors. IOSkywalkNetworkPacket overrides none of these (25.6.0). */
extern "C" {
uint64_t rtl_pkt_base(void *pkt) __asm("__ZNK15IOSkywalkPacket21getDataVirtualAddressEv");
uint16_t rtl_pkt_off(void *pkt) __asm("__ZNK15IOSkywalkPacket13getDataOffsetEv");
uint32_t rtl_pkt_len(void *pkt) __asm("__ZNK15IOSkywalkPacket13getDataLengthEv");
uint32_t rtl_pkt_bufsize(void *pkt) __asm("__ZNK15IOSkywalkPacket13getBufferSizeEv");
uint32_t rtl_pkt_bufcount(void *pkt) __asm("__ZNK15IOSkywalkPacket20getPacketBufferCountEv");
IOReturn rtl_pkt_prepare(void *pkt, void *queue, uint64_t dir, uint32_t opts)
	__asm("__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj");
IOReturn rtl_pkt_setOffLen(void *pkt, uint16_t off, uint32_t len)
	__asm("__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj");
void rtl_netpkt_setLinkHdrLen(void *pkt, uint8_t len)
	__asm("__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh");
IOReturn rtl_pool_alloc(void *pool, void **pkt, uint32_t opts)
	__asm("__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj");
void rtl_pool_free(void *pool, void *pkt)
	__asm("__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket");
IOReturn rtl_rxcomp_enqueue(void *q, void *const *pkts, uint32_t n, uint32_t opts)
	__asm("__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
IOReturn rtl_txcomp_enqueue(void *q, void *const *pkts, uint32_t n, uint32_t opts)
	__asm("__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
}

/* IOSkywalkPacketBufferPool::PoolOptions, as AppleEthernetRL::startInterface fills it */
struct rtl_pool_options {
	uint32_t packetCount;
	uint32_t bufferCount;
	uint32_t bufferSize;
	uint32_t maxBuffersPerPacket;
	uint32_t reserved0;
	uint32_t poolFlags;
	void    *dmaSpec;          /* NULL: no DMA, the radio copies */
	uint64_t reserved1;
};

/* IOSkywalkEthernetInterface::RegistrationInfo (0x130 bytes) */
#define REGINFO_SIZE        0x130
#define REGINFO_ROLE        0x10    /* u32, 1 for the STA role */
#define REGINFO_SUBFAMILY   0x0c    /* u32, 3 = IFNET_SUBFAMILY_WIFI */
#define REGINFO_UNK14       0x14    /* u32, Apple sets 2 */
#define REGINFO_UNK24       0x24    /* u64, Apple sets 0x8000000080 */
#define REGINFO_HEADROOM    0x3c    /* u16, Apple sets 64 */
#define REGINFO_FLAGS       0x40
#define REGINFO_MAC         0x108

/* driver -> WCL events (IO80211Controller::postMessage) */
enum {
	kMsgScanDone       = 10,    /* APPLE80211_M_SCAN_DONE, u32 status */
	kMsgBeacon         = 201,   /* apple80211_beacon_msg, 68 + ie_len */
	kMsgAuthAssoc      = 211,   /* 28 */
	kMsgSetSsid        = 212,   /* 20 */
	kMsgConnectDone    = 213,   /* 164 */
	kMsgLink           = 216,   /* 16 */
	kMsgWclScanDone    = 237,   /* u32 status */
	kMsgLqm            = 39,    /* apple80211_lqm_event_data, 476 */
	kMsgReassoc        = 73,    /* u32 status, u32 reason (AppleBCMWLANCore::handleReassocEvent) */
	kMsgRoamDone       = 80,    /* roam status, 168, u32 status first */
};

#define BEACON_HDR          68
#define LQM_LEN             476
#define kLqmTicks           5       /* polls (1 s) per LQM report */
#define BEACON_MAX_IES      2048
#define kScanMax            64
#define kRxQueueMax         512     /* frames waiting for the drain thread call */
#define kRxBatch            64      /* per RxCompletion enqueue (must be < capacity) */
#define kPoolNoSleep        1       /* allocatePacket option: kern_pbufpool_alloc_nosleep */
#define kIfaceId            1
#define kRoleSta            1

#define ERR_NOT_ASSOCIATED  ((IOReturn)0xE0821803)   /* what AppleBCMWLANCore::getRATE returns */

/* Broadcom d11ac chanspec, which AppleChannelSpec appears to follow (unverified). */
static uint16_t chanspec(uint8_t chan, uint8_t band, uint8_t bw)
{
	uint16_t bwBits = bw == RTL_RADIO_BW_80 ? 0x2000 : bw == RTL_RADIO_BW_40 ? 0x1800 : 0x1000;
	return (uint16_t)(chan | bwBits | (band == RTL_RADIO_BAND_5G ? 0xC000 : 0x0000));
}

/* Classic apple80211_channel flags */
static uint32_t chanFlags(uint8_t band, uint8_t bw)
{
	uint32_t f = band == RTL_RADIO_BAND_5G ? 0x10 : 0x08;
	f |= bw == RTL_RADIO_BW_80 ? 0x400 : bw == RTL_RADIO_BW_40 ? 0x04 : 0x02;
	return f;
}

/* PHY rate in Mbps for a DESC_RATE* code (rounded; SGI included). */
static uint32_t descRateMbps(uint8_t rate, uint8_t bw, uint8_t sgi)
{
	static const uint16_t legacy[12] = { 10, 20, 55, 110, 60, 90, 120, 180, 240, 360, 480, 540 };
	/* x10 Mbps, one stream, long GI, MCS0..9 at 20/40/80 MHz */
	static const uint16_t vht[3][10] = {
		{ 65, 130, 195, 260, 390, 520, 585, 650, 780, 867 },
		{ 135, 270, 405, 540, 810, 1080, 1215, 1350, 1620, 1800 },
		{ 293, 585, 878, 1170, 1755, 2340, 2633, 2925, 3510, 3900 },
	};
	uint32_t x10;
	if (rate < 12)
		return legacy[rate] / 10;
	if (bw > RTL_RADIO_BW_80)
		bw = RTL_RADIO_BW_20;
	if (rate < 0x2C) {                       /* HT MCS0..31 */
		uint8_t mcs = rate - 0x0C;
		x10 = vht[bw][mcs % 8] * (mcs / 8 + 1);
	} else {                                 /* VHT nSS MCSm */
		uint8_t idx = rate - 0x2C;
		x10 = vht[bw][idx % 10] * (idx / 10 + 1);
	}
	if (sgi)
		x10 = x10 * 10 / 9;
	return (x10 + 5) / 10;
}

static void putStr(uint8_t *vd, const char *s)
{
	/* apple80211_version_data: +4 u16 len, +6 string (max 383) */
	size_t n = strlen(s);
	if (n > 383)
		n = 383;
	memcpy(vd + 6, s, n);
	vd[6 + n] = 0;
	*(uint16_t *)(vd + 4) = (uint16_t)n;
}

/* =========================================================================
 * Interface
 * ========================================================================= */
OSDefineMetaClassAndAbstractStructors(RTLAirInterfaceStubs, IO80211InfraProtocol)
#define STUB_CLASS RTLAirInterfaceStubs
#include "IO80211_25_6_0_infra_stubs.inc"
#undef STUB_CLASS

OSDefineMetaClassAndStructors(RTLAirInterface, RTLAirInterfaceStubs)

/*
 * Order of AppleBCMWLANSkywalkInterface::initWithProviderAndParameters:
 * init(), role/id, attach(controller). init(controller, mac) then re-runs
 * the IO80211SkywalkInterface ivar setup and stores the link address.
 */
bool RTLAirInterface::initWithController(RTLAirController *ctrl, const uint8_t mac[6])
{
	_ctrl = ctrl;
	if (!init()) {
		LOG("interface init() failed");
		return false;
	}
	ether_addr ea;
	memcpy(ea.octet, mac, 6);
	if (!IO80211SkywalkInterface::init(ctrl, &ea)) {
		LOG("IO80211SkywalkInterface::init(ctrl, mac) failed");
		return false;
	}
	rtl_sky_setRole(this, kRoleSta);
	rtl_sky_setId(this, kIfaceId);
	if (!attach(ctrl)) {
		LOG("interface attach failed");
		return false;
	}
	return true;
}

/* Order of AppleBCMWLANSkywalkInterface::start(AppleBCMWLANCore*, RegistrationInfo*). */
bool RTLAirInterface::startWithRegistration(RTLAirController *ctrl, void *regInfo)
{
	if (!IO80211InfraInterface::start(ctrl)) {
		LOG("IO80211InfraInterface::start failed");
		return false;
	}
	LOG("IO80211InfraInterface::start ok");
	uint8_t builtIn = 0;
	setProperty("built-in", &builtIn, sizeof(builtIn));
	if (!createDatapath(regInfo))
		return false;
	registerService(kIOServiceAsynchronous);
	setRunningState(false);
	return true;
}

bool RTLAirInterface::createDatapath(void *regInfo)
{
	rtl_pool_options o;
	bzero(&o, sizeof(o));
	o.packetCount = 512;
	o.bufferCount = 512;
	o.bufferSize = 2048;
	o.maxBuffersPerPacket = 1;
	o.poolFlags = 1;
	_txPool = rtl_pool_withName("rtlair:tx", this, 1, &o);
	_rxPool = rtl_pool_withName("rtlair:rx", this, 1, &o);
	if (!_txPool || !_rxPool) {
		LOG("packet pool alloc failed");
		return false;
	}
	_txSubQ = rtl_txsub_withPool(_txPool, 256, 0, this, (void *)&RTLAirInterface::txAction, nullptr, 0);
	_txCompQ = rtl_txcomp_withPool(_txPool, 256, 0, this, nullptr, nullptr, 0);
	_rxCompQ = rtl_rxcomp_withPool(_rxPool, 256, 0, this, nullptr, nullptr, 0);
	if (!_txSubQ || !_txCompQ || !_rxCompQ) {
		LOG("packet queue alloc failed");
		return false;
	}
	/* Apple adds each queue through IO80211WorkQueue vtable +0x140, which is
	 * IOWorkLoop::addEventSource (checked by Tools/re/check_airlayer.sh). */
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (!wq) {
		LOG("no work queue");
		return false;
	}
	wq->addEventSource((IOEventSource *)_txSubQ);
	wq->addEventSource((IOEventSource *)_txCompQ);
	wq->addEventSource((IOEventSource *)_rxCompQ);

	void *queues[3] = { _txSubQ, _txCompQ, _rxCompQ };
	IOReturn r = rtl_infra_registerEth(this, regInfo, queues, 3, _txPool, _rxPool);
	LOG("registerInfraEthernetInterface = 0x%x", r);
	return r == kIOReturnSuccess;
}

/*
 * TX (TxSubmissionQueue action, on the work queue): copy each frame into an
 * mbuf for the radio, then hand every packet straight back through the TX
 * completion queue, as AppleEthernetRL does once its ring is done.
 * The data pointer is buffer base + data offset (AppleEthernetRL::dequeueTx...).
 */
IOReturn RTLAirInterface::txAction(OSObject *owner, void *, void *const *pkts,
                                   uint32_t count, void *)
{
	RTLAirInterface *me = OSDynamicCast(RTLAirInterface, owner);
	if (!me || !count)
		return 0;
	for (uint32_t i = 0; i < count; i++) {
		void *pkt = pkts[i];
		if (!me->_ctrl->dataPathEnabled())
			continue;
		if (!rtl_pkt_bufcount(pkt))             /* no buffer attached: nothing to read */
			continue;
		uint32_t len = rtl_pkt_len(pkt);
		const uint8_t *data = (const uint8_t *)(uintptr_t)(rtl_pkt_base(pkt) + rtl_pkt_off(pkt));
		mbuf_t m = nullptr;
		if (len < 14 || len > 2304 ||
		    mbuf_allocpacket(MBUF_DONTWAIT, len, nullptr, &m) != 0)
			continue;
		if (mbuf_copyback(m, 0, len, data, MBUF_DONTWAIT) != 0) {
			mbuf_freem(m);
			continue;
		}
		mbuf_pkthdr_setlen(m, len);
		me->_ctrl->transmit(m);                /* consumes m */
	}
	/* enqueuePackets wants count < queue capacity: hand back in chunks */
	for (uint32_t done = 0; done < count; done += 64)
		rtl_txcomp_enqueue(me->_txCompQ, pkts + done, count - done > 64 ? 64 : count - done, 0);
	return (IOReturn)count;
}

/*
 * RX (on the controller's drain thread call, never under the radio's lock):
 * copy each Ethernet frame into a pool packet and push them up the RX
 * completion queue in batches; the queue takes its gate itself. Allocation
 * does not sleep, so a full pool drops frames instead of stalling.
 */
void RTLAirInterface::rxFrames(mbuf_t chain)
{
	void *pkts[kRxBatch];
	uint32_t n = 0;
	while (chain) {
		mbuf_t m = chain;
		chain = mbuf_nextpkt(m);
		mbuf_setnextpkt(m, nullptr);
		size_t len = mbuf_pkthdr_len(m);
		void *pkt = nullptr;
		if (len >= 14 && _rxPool && rtl_pool_alloc(_rxPool, &pkt, kPoolNoSleep) == kIOReturnSuccess && pkt) {
			/* A fresh packet has no buffer table until prepare (prepareWithQueue
			 * walks the buflets); getDataVirtualAddress on it faulted at
			 * NULL+0x58 (0.15 panic). No source queue: the RX enqueue
			 * completes the packet and calls the source queue's
			 * packetCompletion, which only submission queues implement
			 * (0.16 panicked in the default one). */
			if (rtl_pkt_prepare(pkt, nullptr, 0, 0) == kIOReturnSuccess &&
			    rtl_pkt_bufcount(pkt) &&
			    len <= rtl_pkt_bufsize(pkt) &&
			    mbuf_copydata(m, 0, len, (void *)(uintptr_t)rtl_pkt_base(pkt)) == 0 &&
			    rtl_pkt_setOffLen(pkt, 0, (uint32_t)len) == kIOReturnSuccess) {
				rtl_netpkt_setLinkHdrLen(pkt, 14);
				pkts[n++] = pkt;
				_ctrl->countRx(1);
			} else {
				rtl_pool_free(_rxPool, pkt);
			}
		}
		mbuf_freem(m);
		if (n && (n == kRxBatch || !chain)) {
			if (rtl_rxcomp_enqueue(_rxCompQ, pkts, n, 0) != kIOReturnSuccess)
				for (uint32_t i = 0; i < n; i++)
					rtl_pool_free(_rxPool, pkts[i]);
			n = 0;
		}
	}
}

/*
 * As AppleBCMWLANSkywalkInterface::stop: on the work queue, disable each
 * packet queue, take it off the work queue and drop it, then the superclass.
 */
IOReturn RTLAirInterface::removeQueues(OSObject *owner, void *, void *, void *, void *)
{
	RTLAirInterface *me = (RTLAirInterface *)owner;
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(me);
	void **qs[3] = { &me->_txSubQ, &me->_txCompQ, &me->_rxCompQ };
	for (int i = 0; i < 3; i++) {
		IOEventSource *q = (IOEventSource *)*qs[i];
		if (!q)
			continue;
		q->disable();
		if (wq)
			wq->removeEventSource(q);
		q->release();
		*qs[i] = nullptr;
	}
	return kIOReturnSuccess;
}

/*
 * Interface up / down. The family never enables a driver's packet queues:
 * AppleBCMWLANSkywalkInterface::enable calls the superclass, then, on the
 * work queue, its own enableDatapath, which enables the queues (and disable
 * the reverse). Without this the RX completion queue refuses every packet.
 */
IOReturn RTLAirInterface::switchQueues(OSObject *owner, void *on, void *, void *, void *)
{
	RTLAirInterface *me = (RTLAirInterface *)owner;
	void *qs[3] = { me->_txSubQ, me->_txCompQ, me->_rxCompQ };
	for (int i = 0; i < 3; i++) {
		IOEventSource *q = (IOEventSource *)qs[i];
		if (!q)
			continue;
		if (on)
			q->enable();
		else
			q->disable();
	}
	return kIOReturnSuccess;
}

uintptr_t RTLAirInterface::enable(unsigned int opts)
{
	STUB_ENTER();
	uintptr_t r = RTLAirInterfaceStubs::enable(opts);
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (wq)
		wq->runAction(&RTLAirInterface::switchQueues, this, (void *)1);
	LOG("interface enable (0x%x), queues on", opts);
	return r;
}

uintptr_t RTLAirInterface::disable(unsigned int opts)
{
	STUB_ENTER();
	uintptr_t r = RTLAirInterfaceStubs::disable(opts);
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (wq)
		wq->runAction(&RTLAirInterface::switchQueues, this, nullptr);
	LOG("interface disable (0x%x), queues off", opts);
	return r;
}

void RTLAirInterface::stop(IOService *provider)
{
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (wq)
		wq->runAction(&RTLAirInterface::removeQueues, this);
	else
		removeQueues(this, nullptr, nullptr, nullptr, nullptr);
	RTLAirInterfaceStubs::stop(provider);
}

void RTLAirInterface::free()
{
	OSSafeReleaseNULL(*(OSObject **)&_txSubQ);
	OSSafeReleaseNULL(*(OSObject **)&_txCompQ);
	OSSafeReleaseNULL(*(OSObject **)&_rxCompQ);
	OSSafeReleaseNULL(*(OSObject **)&_txPool);
	OSSafeReleaseNULL(*(OSObject **)&_rxPool);
	RTLAirInterfaceStubs::free();
}

/* ---- WCL requests ---- */

/* apple80211ScanRequest: +88 channel entries (12 B), +84 count. The radio
 * scans its whole channel plan, so the list is only logged. */
IOReturn RTLAirInterface::setWCL_SCAN_REQ(apple80211ScanRequest *req)
{
	STUB_ENTER();
	if (!req)
		return kIOReturnBadArgument;
	LOG("WCL scan request, %u channels", *(uint32_t *)((uint8_t *)req + 84));
	return _ctrl->startScan();
}

/* apple80211AssocCandidates, see Docs/PHASE5_IO80211.md for the offsets. */
IOReturn RTLAirInterface::setWCL_ASSOCIATE(apple80211AssocCandidates *cand)
{
	STUB_ENTER();
	const uint8_t *c = (const uint8_t *)cand;
	if (!c)
		return kIOReturnBadArgument;
	if (!_ctrl->joinAllowed()) {
		LOG("associate refused: rtlair=1 is scan only (rtlair=2 to join)");
		return kIOReturnUnsupported;
	}
	struct rtl_radio_join j;
	bzero(&j, sizeof(j));
	uint32_t ssidLen = *(const uint32_t *)(c + 28);
	if (ssidLen == 0 || ssidLen > 32)
		return kIOReturnBadArgument;
	j.ssid_len = (uint8_t)ssidLen;
	memcpy(j.ssid, c + 32, ssidLen);

	const uint8_t *key = c + 64;                 /* embedded apple80211_key */
	uint32_t keyLen = *(const uint32_t *)(key + 4);
	if (keyLen == 32) {
		j.has_pmk = 1;
		memcpy(j.pmk, key + 16, 32);
	} else if (keyLen != 0) {
		LOG("associate: key length %u is not a PMK, unsupported", keyLen);
		return kIOReturnUnsupported;
	}
	if (*(const uint32_t *)(c + 532) > 0)
		memcpy(j.bssid, c + 540, 6);             /* first candidate */
	LOG("associate: ssid_len %u, auth %u/%u, cipher %u, %u candidates", ssidLen,
	    *(const uint32_t *)(c + 16), *(const uint32_t *)(c + 20),
	    *(const uint32_t *)(key + 8), *(const uint32_t *)(c + 532));
	IOReturn r = _ctrl->startJoin(&j);
	bzero(&j, sizeof(j));
	return r;
}

/* The radio runs the 4-way handshake itself from the PMK: nothing to install. */
IOReturn RTLAirInterface::setCIPHER_KEY(apple80211_key *key)
{
	STUB_ENTER();
	if (key)
		LOG("cipher key: type %u, len %u (handled by the radio)",
		    *(uint32_t *)((uint8_t *)key + 8), *(uint32_t *)((uint8_t *)key + 4));
	return kIOReturnSuccess;
}

IOReturn RTLAirInterface::setWCL_LEAVE_NETWORK(apple80211_leave_network *leave)
{
	STUB_ENTER();
	if (!leave)
		return EINVAL;
	_ctrl->leave(true);
	return kIOReturnSuccess;
}

IOReturn RTLAirInterface::setWCL_JOIN_ABORT(apple80211_wcl_abort_join *)
{
	STUB_ENTER();
	_ctrl->leave(false);
	return kIOReturnSuccess;
}

/*
 * apple80211_reassoc (ioctl 0x1AA, 188 bytes), as WCLNetManager::sendReassocCommand
 * fills it: chanspecs at +0, BSSIDs at +100 (count at +144), channel count at
 * +148, current SSID at +155 (length at +187). airportd's "best connected roam"
 * sends one broadcast BSSID: any BSS of this network.
 */
IOReturn RTLAirInterface::setWCL_REASSOC(apple80211_reassoc *req)
{
	STUB_ENTER();
	const uint8_t *r = (const uint8_t *)req;
	if (!r)
		return kIOReturnBadArgument;
	if (!_ctrl->joinAllowed())
		return kIOReturnUnsupported;
	uint32_t count = *(const uint32_t *)(r + 144);
	if (count > 7)
		count = 7;
	return _ctrl->startRoam(r + 100, count);
}

/*
 * WCL aborts its own scan before a roam (setROAM) and fails the roam if this
 * fails. The radio cannot cut a survey short: it runs to the end, and a join
 * issued meanwhile waits for it (rtw_set_802_11_connect sets to_join).
 */
IOReturn RTLAirInterface::setWCL_SCAN_ABORT(void *)
{
	STUB_ENTER();
	return kIOReturnSuccess;
}

/* ---- link details (what AppleBCMWLANCore reads from the "nrate" iovar) ---- */

/* DESC_RATE*: 0x0C + n = HT MCSn, 0x2C + 10 * (nss - 1) + m = VHT nss MCSm */
static bool rateIsVht(uint8_t rate) { return rate >= 0x2C; }
static uint8_t rateMcs(uint8_t rate)
{
	if (rateIsVht(rate))
		return (rate - 0x2C) % 10;
	return rate >= 0x0C ? rate - 0x0C : 0;      /* HT: the full MCS index */
}
static uint8_t rateNss(uint8_t rate)
{
	if (rateIsVht(rate))
		return (rate - 0x2C) / 10 + 1;
	return rate >= 0x0C ? (rate - 0x0C) / 8 + 1 : 1;
}

/* apple80211_mcs_data (8): +4 MCS index */
IOReturn RTLAirInterface::getMCS(apple80211_mcs_data *md)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	if (!md)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	if (st.tx_rate >= 0x0C)
		*(uint32_t *)((uint8_t *)md + 4) = rateMcs(st.tx_rate);
	return kIOReturnSuccess;
}

/* apple80211_mcs_vht_data (20): +4 MCS, +8 NSS, +12 width (MHz), +16 GI (ns);
 * left alone for a non-VHT rate, as Broadcom does. */
IOReturn RTLAirInterface::getMCS_VHT(apple80211_mcs_vht_data *md)
{
	STUB_ENTER();
	static const uint32_t mhz[3] = { 20, 40, 80 };
	struct rtl_radio_status st;
	if (!md)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	if (rateIsVht(st.tx_rate)) {
		uint8_t *p = (uint8_t *)md;
		*(uint32_t *)(p + 4) = rateMcs(st.tx_rate);
		*(uint32_t *)(p + 8) = rateNss(st.tx_rate);
		*(uint32_t *)(p + 12) = mhz[st.bss_bw <= RTL_RADIO_BW_80 ? st.bss_bw : 0];
		*(uint32_t *)(p + 16) = st.tx_sgi ? 400 : 800;
	}
	return kIOReturnSuccess;
}

/* apple80211_btcoex_max_nss_for_ap_data (8): +4 streams usable with this AP
 * (Broadcom: btc_nss, the BT coex limit). No BT coex here: the negotiated NSS. */
IOReturn RTLAirInterface::getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *nd)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	if (!nd)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	*(uint32_t *)((uint8_t *)nd + 4) = st.nss ? st.nss : rateNss(st.tx_rate);
	return kIOReturnSuccess;
}

/* apple80211_beacon_info_t (2056): +0 frame length, +4 the current BSS's
 * beacon frame (<= 2048), +2052 HE beacon. Rebuilt from the scan entry. */
IOReturn RTLAirInterface::getBEACON_INFO(apple80211_beacon_info_t *bi)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	uint8_t *p = (uint8_t *)bi;
	if (!p)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	uint32_t len = _ctrl->copyBeaconFrame(st.bssid, p + 4, 2048);
	if (!len)
		return kIOReturnNotFound;
	*(uint32_t *)p = len;
	p[2052] = 0;
	return kIOReturnSuccess;
}

/*
 * apple80211_wcl_traffic_counters (7 x u64), as AppleBCMWLANCore fills it:
 * [0] infra packets, [1] firmware counters, [2] infra total, [3] per-AC
 * total, [4]/[5] NAN, [6] timestamp (ns). WCL polls it every 5 s for
 * activity; only the infra packet counts exist here.
 */
IOReturn RTLAirInterface::getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *tc)
{
	STUB_ENTER();
	uint64_t *q = (uint64_t *)tc;
	if (!q)
		return kIOReturnBadArgument;
	uint64_t pkts = _ctrl->trafficPkts();
	q[0] = pkts;
	q[1] = 0;
	q[2] = pkts;
	q[3] = 0;
	q[4] = 0;
	q[5] = 0;
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	q[6] = ns;
	return kIOReturnSuccess;
}

/* APPLE80211_IOC_ROAM_PROFILE (384): airportd sets the per-band roam profile
 * and reads it back; the roam decisions here are the layer's own (startRoam). */
IOReturn RTLAirInterface::getROAM_PROFILE(apple80211_roam_profile_all_bands *rp)
{
	STUB_ENTER();
	if (!rp)
		return kIOReturnBadArgument;
	memcpy(rp, _ctrl->_roamProfile, sizeof(_ctrl->_roamProfile));
	return kIOReturnSuccess;
}

IOReturn RTLAirInterface::setROAM_PROFILE(apple80211_roam_profile_all_bands *rp)
{
	STUB_ENTER();
	if (!rp)
		return kIOReturnBadArgument;
	memcpy(_ctrl->_roamProfile, rp, sizeof(_ctrl->_roamProfile));
	return kIOReturnSuccess;
}

IOReturn RTLAirInterface::setBTCOEX_PROFILE(apple80211_btcoex_profile *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setWCL_QOS_PARAMS(apple80211_wcl_qos_params *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setOS_FEATURE_FLAGS(apple80211_feature_flags *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) { STUB_ENTER(); return kIOReturnSuccess; }

/* apple80211_fw_hot_channels (AppleBCMWLANNetAdapter::getFWHotChannels): +0
 * u16 chanspec[7], +16 u32 count. Broadcom answers the firmware's roam
 * hotlist; here, the channels the joined SSID was last seen on. */
IOReturn RTLAirInterface::getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *hc)
{
	STUB_ENTER();
	uint8_t *p = (uint8_t *)hc;
	if (!p)
		return kIOReturnBadArgument;
	memset(p, 0, 20);
	*(uint32_t *)(p + 16) = _ctrl->hotChannels((uint16_t *)p, 7);
	return kIOReturnSuccess;
}
IOReturn RTLAirInterface::setWCL_LINK_UP_DONE(void *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setPOWERSAVE(apple80211_powersave_data *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setINFRA_ENUMERATED(apple80211_infra_enumerated *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setIE(apple80211_ie_data *) { STUB_ENTER(); return kIOReturnSuccess; }
IOReturn RTLAirInterface::setBSS_BLACKLIST(bss_blacklist *) { STUB_ENTER(); return kIOReturnSuccess; }

/*
 * WCLJoinManager::handleJoinRequest reads this first (ioctl 0x1AB, 4 bytes)
 * and drops the join on any error. Broadcom fills three low latency flags;
 * none apply here.
 */
IOReturn RTLAirInterface::getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *info)
{
	STUB_ENTER();
	if (!info)
		return kIOReturnBadArgument;
	bzero(info, 4);
	return kIOReturnSuccess;
}

/*
 * After the join WCLNetManager::setCurrentBSS reads this (ioctl 0x1CC, 532
 * bytes: rate set, MCS sets at +188/+204/+212, MLO context at +220, the
 * associated RSN IE at +275) and leaves the network on any error. Broadcom
 * fills it from the firmware; zeros mean no MLO and nothing extra.
 */
IOReturn RTLAirInterface::getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *info)
{
	STUB_ENTER();
	if (!info)
		return kIOReturnBadArgument;
	bzero(info, 0x214);
	return kIOReturnSuccess;
}

/* setCurrentBSS's next step (0x1F6, 2 bytes): also fatal on error. No QoS fast lane here. */
IOReturn RTLAirInterface::setWCL_UPDATE_FAST_LANE(apple80211_fastlane *)
{
	STUB_ENTER();
	return kIOReturnSuccess;
}

/*
 * IO80211MacAddressAgent::setMacAddress (APPLE80211_IOC_SET_MAC_ADDRESS,
 * sent by WCL before every join: the per-network private address) hands the
 * new address plus three spares; on success it moves the interface to it.
 * The radio follows in startJoin, where it is idle.
 */
uintptr_t RTLAirInterface::setMacAddress(mloAddrArray &addrs)
{
	STUB_ENTER();
	const uint8_t *m = (const uint8_t *)&addrs;
	LOG("set MAC %02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
	return 0;
}

/* ---- getters ---- */

IOReturn RTLAirInterface::getCHANNEL(apple80211_channel_data *cd)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	if (!cd)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	uint8_t *p = (uint8_t *)cd;
	*(uint32_t *)(p + 0) = 1;
	*(uint32_t *)(p + 4) = 1;                    /* apple80211_channel.version */
	*(uint32_t *)(p + 8) = st.channel;
	*(uint32_t *)(p + 12) = chanFlags(st.band, st.bss_bw);
	return kIOReturnSuccess;
}

/* apple80211_rssi_data: +0 version, +4 radios, +12 rssi[], +28 aggregate */
IOReturn RTLAirInterface::getRSSI(apple80211_rssi_data *rd)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	if (!rd)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	uint8_t *p = (uint8_t *)rd;
	bzero(p + 4, 48);
	*(uint32_t *)(p + 0) = 1;
	*(uint32_t *)(p + 4) = 1;
	*(int32_t *)(p + 12) = st.rssi_dbm;
	*(int32_t *)(p + 28) = st.rssi_dbm;
	return kIOReturnSuccess;
}

/* apple80211_rate_data: +8 Mbps */
IOReturn RTLAirInterface::getRATE(apple80211_rate_data *rd)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	if (!rd)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	*(uint32_t *)((uint8_t *)rd + 0) = 1;
	*(uint32_t *)((uint8_t *)rd + 4) = 1;        /* radios */
	*(uint32_t *)((uint8_t *)rd + 8) = descRateMbps(st.tx_rate, st.bss_bw, st.tx_sgi);
	return kIOReturnSuccess;
}

/* apple80211_opmode_data: +0 version, +4 op mode (1 = STA) */
IOReturn RTLAirInterface::getOP_MODE(apple80211_opmode_data *od)
{
	STUB_ENTER();
	if (!od)
		return EINVAL;
	*(uint32_t *)((uint8_t *)od + 0) = 1;
	*(uint32_t *)((uint8_t *)od + 4) = 1;
	return kIOReturnSuccess;
}

IOReturn RTLAirInterface::getNSS(apple80211_nss_data *nd)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	if (!nd)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	*(uint32_t *)((uint8_t *)nd + 0) = 1;
	*(uint32_t *)((uint8_t *)nd + 4) = st.nss ? st.nss : 1;
	return kIOReturnSuccess;
}

/*
 * apple80211_sup_channel_data (as AppleBCMWLANCore::getSupportedChannelsMatching):
 * +0 version, +4 count, +8 apple80211_channel[400] (version/channel/flags),
 * +4808 include mask (0 = all), +4812 exclude mask. One 20 MHz entry per
 * channel; ACTIVE (0x80) unless passive, DFS (0x100) on 5 GHz 52-144.
 */
#define SUPCH_MAX       400
#define SUPCH_INCLUDE   4808
#define SUPCH_EXCLUDE   4812

IOReturn RTLAirInterface::getSUPPORTED_CHANNELS(apple80211_sup_channel_data *cd)
{
	STUB_ENTER();
	if (!cd)
		return EINVAL;
	uint8_t *p = (uint8_t *)cd;
	uint32_t inc = *(uint32_t *)(p + SUPCH_INCLUDE);
	uint32_t exc = *(uint32_t *)(p + SUPCH_EXCLUDE);
	if (inc == 0)
		inc = 0xFFFFFFFF;

	struct rtl_radio_chan ch[128];
	uint32_t n = 128;
	IOReturn r = _ctrl->radio(RTL_RADIO_FN_CHANNELS, ch, &n);
	if (r != kIOReturnSuccess)
		return r;
	uint32_t out = 0;
	for (uint32_t i = 0; i < n && out < SUPCH_MAX; i++) {
		uint32_t f = chanFlags(ch[i].band, RTL_RADIO_BW_20);
		if (!ch[i].passive)
			f |= 0x80;
		if (ch[i].band == RTL_RADIO_BAND_5G && ch[i].channel >= 52 && ch[i].channel <= 144)
			f |= 0x100;
		if (!(f & inc) || (f & exc))
			continue;
		uint8_t *e = p + 8 + 12 * out++;
		*(uint32_t *)(e + 0) = 1;
		*(uint32_t *)(e + 4) = ch[i].channel;
		*(uint32_t *)(e + 8) = f;
	}
	*(uint32_t *)(p + 0) = 1;
	*(uint32_t *)(p + 4) = out;
	return kIOReturnSuccess;
}

IOReturn RTLAirInterface::getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *cd)
{
	STUB_ENTER();
	return getSUPPORTED_CHANNELS(cd);
}

/*
 * apple80211_channels_info (10016), struct of arrays for up to 400 entries
 * (AppleBCMWLANCore::handleChanInfoTimer, IO80211AWDLPeerManager::isValidChannel):
 * +8 country (3 chars), +12 u16 count, +14 u16 Broadcom chanspec[], +814 primary
 * channel, +1214 indoor only, +1614 radar, +2014 passive, +2414 40 MHz entry,
 * +2814 80 MHz entry, +3216 u32 chanspec, +4816 u32 per-channel flags, +6416
 * 160 MHz entry, +6816 6 GHz. One entry per 20 MHz channel, then one per
 * 40/80 MHz chanspec that the 20 MHz channels cover, keyed by primary channel.
 */
#define CHI_MAX         400
#define CHI_COUNT       12
#define CHI_SPEC        14
#define CHI_NUM         814
#define CHI_RADAR       1614
#define CHI_PASSIVE     2014
#define CHI_40          2414
#define CHI_80          2814
#define CHI_SPEC32      3216
#define CHI_FLAGS       4816
#define CHI_SIZE        10016

/* Broadcom per_chan_info bits */
#define BCM_CHAN_VALID  0x03
#define BCM_CHAN_5G     0x04
#define BCM_CHAN_RADAR  0x08
#define BCM_CHAN_PASSIVE 0x20

static void chiAdd(uint8_t *p, uint32_t &n, uint16_t spec, uint8_t prim, uint8_t bw, uint32_t flags)
{
	if (n >= CHI_MAX)
		return;
	*(uint16_t *)(p + CHI_SPEC + 2 * n) = spec;
	p[CHI_NUM + n] = prim;
	p[CHI_RADAR + n] = (flags & BCM_CHAN_RADAR) != 0;
	p[CHI_PASSIVE + n] = (flags & BCM_CHAN_PASSIVE) != 0;
	p[CHI_40 + n] = bw == 40;
	p[CHI_80 + n] = bw == 80;
	*(uint32_t *)(p + CHI_SPEC32 + 4 * n) = spec;
	*(uint32_t *)(p + CHI_FLAGS + 4 * n) = flags;
	n++;
}

IOReturn RTLAirInterface::getCHANNELS_INFO(apple80211_channels_info *ci)
{
	STUB_ENTER();
	uint8_t *p = (uint8_t *)ci;
	if (!p)
		return kIOReturnBadArgument;

	struct rtl_radio_chan ch[128];
	uint32_t cnt = 128;
	IOReturn r = _ctrl->radio(RTL_RADIO_FN_CHANNELS, ch, &cnt);
	if (r != kIOReturnSuccess)
		return r;

	memset(p, 0, CHI_SIZE);
	char cc[2] = { 'X', 'Z' };
	_ctrl->countryCode(cc);
	p[8] = (uint8_t)cc[0];
	p[9] = (uint8_t)cc[1];

	uint32_t n = 0;
	uint8_t flags5[256] = {};      /* 5 GHz channel -> per-channel flags, 0 = absent */
	for (uint32_t i = 0; i < cnt; i++) {
		uint8_t c = ch[i].channel;
		bool g5 = ch[i].band == RTL_RADIO_BAND_5G;
		uint8_t f = BCM_CHAN_VALID | (g5 ? BCM_CHAN_5G : 0);
		if (ch[i].passive)
			f |= BCM_CHAN_PASSIVE;
		if (g5 && c >= 52 && c <= 144)
			f |= BCM_CHAN_RADAR;
		chiAdd(p, n, (uint16_t)((g5 ? 0xC000 : 0) | 0x1000 | c), c, 20, f);
		if (g5)
			flags5[c] = f;
	}

	/* 40 and 80 MHz blocks on 5 GHz, only where every 20 MHz channel exists.
	 * Sideband bits count 20 MHz steps from the low edge (0x100 each). */
	static const uint8_t base[] = { 36, 52, 100, 116, 132, 149 };
	for (uint32_t w = 40; w <= 80; w += 40) {
		uint32_t span = w / 20;
		for (uint32_t b = 0; b < sizeof(base); b++)
			for (uint32_t lo = base[b]; lo < base[b] + 16u; lo += 4 * span) {
				uint32_t f = 0;
				bool ok = true;
				for (uint32_t k = 0; k < span && ok; k++) {
					ok = lo + 4 * k < 256 && flags5[lo + 4 * k];
					if (ok)
						f |= flags5[lo + 4 * k];
				}
				if (!ok)
					continue;
				uint8_t center = (uint8_t)(lo + 2 * (span - 1));
				uint16_t bwbits = w == 40 ? 0x1800 : 0x2000;
				for (uint32_t k = 0; k < span; k++)
					chiAdd(p, n, (uint16_t)(0xC000 | bwbits | (k << 8) | center),
					       (uint8_t)(lo + 4 * k), (uint8_t)w, f);
			}
	}
	*(uint16_t *)(p + CHI_COUNT) = (uint16_t)n;
	return kIOReturnSuccess;
}

IOReturn RTLAirInterface::getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *ci)
{
	STUB_ENTER();
	return getCHANNELS_INFO(ci);
}

static void fillBeacon(uint8_t *msg, const struct rtl_radio_bss *b, uint8_t bw)
{
	uint32_t ies = b->ies_len > BEACON_MAX_IES ? BEACON_MAX_IES : b->ies_len;
	bzero(msg, BEACON_HDR);
	*(uint32_t *)(msg + 0) = ies;
	*(uint16_t *)(msg + 4) = chanspec(b->channel, b->band, bw);
	memcpy(msg + 6, b->ssid, b->ssid_len > 32 ? 32 : b->ssid_len);
	msg[38] = b->ssid_len;
	msg[39] = b->channel;
	memcpy(msg + 41, b->bssid, 6);
	*(int32_t *)(msg + 48) = b->rssi_dbm;
	*(int16_t *)(msg + 52) = -95;
	*(uint16_t *)(msg + 56) = b->intval;
	*(uint16_t *)(msg + 58) = b->capinfo;
	*(uint32_t *)(msg + 64) = 2;
	memcpy(msg + BEACON_HDR, b->ies, ies);
}

/* Current BSS as one apple80211_beacon_msg (IEs follow the 68-byte header). */
IOReturn RTLAirInterface::getWCL_BSS_INFO(apple80211_beacon_msg *msg)
{
	STUB_ENTER();
	struct rtl_radio_status st;
	if (!msg)
		return kIOReturnBadArgument;
	if (_ctrl->linkStatus(&st) != kIOReturnSuccess)
		return ERR_NOT_ASSOCIATED;
	return _ctrl->copyBeacon(st.bssid, (uint8_t *)msg) ? kIOReturnSuccess : kIOReturnNotFound;
}

/* =========================================================================
 * Controller
 * ========================================================================= */
OSDefineMetaClassAndAbstractStructors(RTLAirControllerStubs, IO80211Controller)
#define STUB_CLASS RTLAirControllerStubs
#include "IO80211_25_6_0_ctrl_stubs.inc"
#undef STUB_CLASS

OSDefineMetaClassAndStructors(RTLAirController, RTLAirControllerStubs)

IOReturn RTLAirController::radio(const char *fn, void *p1, void *p2)
{
	if (!_radio)
		return kIOReturnNotReady;
	const OSSymbol *sym = OSSymbol::withCString(fn);
	if (!sym)
		return kIOReturnNoMemory;
	IOReturn r = _radio->callPlatformFunction(sym, false, p1, p2, nullptr, nullptr);
	sym->release();
	if (!strcmp(fn, RTL_RADIO_FN_STATUS))
		OSIncrementAtomic(&gStatusCalls);
	else if (strcmp(fn, RTL_RADIO_FN_TX))
		LOG("@%llu radio %s -> 0x%x", uptimeMs(), fn, r);
	return r;
}

IOReturn RTLAirController::radioStatus(struct rtl_radio_status *st)
{
	bzero(st, sizeof(*st));
	return radio(RTL_RADIO_FN_STATUS, st);
}

IOReturn RTLAirController::linkStatus(struct rtl_radio_status *st)
{
	if (_phase == kRoaming) {
		/* WCL still holds the old BSS; a failed query here (WCL_BSS_INFO
		 * from assocTimerAction) would make it leave mid-roam. */
		memcpy(st, &_linkSt, sizeof(*st));
		return kIOReturnSuccess;
	}
	if (_phase != kLinked)
		return ERR_NOT_ASSOCIATED;
	IOReturn r = radioStatus(st);
	if (r != kIOReturnSuccess)
		return r;
	return st->state == RTL_RADIO_S_LINKED ? kIOReturnSuccess : ERR_NOT_ASSOCIATED;
}

void RTLAirController::transmit(mbuf_t m)
{
	if (!_radio || !_symTx) {
		mbuf_freem(m);
		return;
	}
	_radio->callPlatformFunction(_symTx, false, m, nullptr, nullptr, nullptr);
	countTx();
}

/*
 * Runs on the radio's RX thread with the radio's hook lock held, so it only
 * queues the frame. Handing it to Skywalk here would take the IO80211 gate
 * under that lock, while a WCL request holding the gate can detach the hook
 * (setDataPath) and wait for the same lock: a deadlock.
 */
void RTLAirController::rxHook(void *ctx, void *m)
{
	RTLAirController *me = (RTLAirController *)ctx;
	mbuf_t mb = (mbuf_t)m;
	bool kick = false;
	IOLockLock(me->_rxLock);
	if (me->_dataHooked && me->_rxCount < kRxQueueMax) {
		mbuf_setnextpkt(mb, nullptr);
		if (me->_rxTail)
			mbuf_setnextpkt(me->_rxTail, mb);
		else
			me->_rxHead = mb;
		me->_rxTail = mb;
		kick = me->_rxCount++ == 0;
		mb = nullptr;
	}
	IOLockUnlock(me->_rxLock);
	if (mb)
		mbuf_freem(mb);
	else if (kick)
		thread_call_enter(me->_rxDrain);
}

void RTLAirController::rxDrainEntry(thread_call_param_t p0, thread_call_param_t)
{
	RTLAirController *me = (RTLAirController *)p0;
	IOLockLock(me->_rxLock);
	mbuf_t chain = me->_rxHead;
	me->_rxHead = me->_rxTail = nullptr;
	me->_rxCount = 0;
	IOLockUnlock(me->_rxLock);
	if (!chain)
		return;
	if (me->_iface && me->_dataHooked && !me->_stopping)
		me->_iface->rxFrames(chain);
	else
		mbuf_freem_list(chain);
}

void RTLAirController::flushRx()
{
	if (!_rxLock)
		return;
	IOLockLock(_rxLock);
	mbuf_t chain = _rxHead;
	_rxHead = _rxTail = nullptr;
	_rxCount = 0;
	IOLockUnlock(_rxLock);
	if (chain)
		mbuf_freem_list(chain);
}

/* rtlair=2: while linked, the radio's frames go through this interface and en2 goes inactive. */
void RTLAirController::setDataPath(bool on)
{
	if (_armed < 2 || on == _dataHooked)
		return;
	_dataHooked = on;
	radio(RTL_RADIO_FN_DATA_HOOK, on ? (void *)&RTLAirController::rxHook : nullptr, on ? this : nullptr);
	LOG("data path %s", on ? "attached" : "detached");
}

/*
 * IO80211PostOffice::sendMail drops the message (0xE00002E2) unless the
 * caller is inside the IO80211WorkQueue gate, as Broadcom's event handlers
 * are. Our posts come from thread calls, so every 201 / 237 / 10 was lost
 * (0.11, 0.13: airportd's scans all timed out after 20 s).
 */
struct PostArgs {
	uint32_t msg;
	void *data;
	size_t len;
	uintptr_t rc;
};

/* One pointer argument: IO80211WorkQueue::runAction forwards to IOWorkLoop's,
 * and the 4th argument would travel on the stack. */
static IOReturn postAction(OSObject *owner, void *arg, void *, void *, void *)
{
	RTLAirController *self = (RTLAirController *)owner;
	PostArgs *a = (PostArgs *)arg;
	a->rc = rtl_ctrl_postMessage(self, self->staInterface(), a->msg, a->data, a->len, true);
	return kIOReturnSuccess;
}

void RTLAirController::post(uint32_t msg, void *data, size_t len)
{
	/* Not while stopping: quiesce() waits for the poll thread call, which
	 * must not then be blocked on the gate. */
	if (!_iface || _stopping)
		return;
	IOWorkLoop *wq = (IOWorkLoop *)gWorkQueue;
	PostArgs a = { msg, data, len, 0 };
	if (wq && !wq->inGate())
		wq->runAction(postAction, this, &a);
	else
		postAction(this, &a, nullptr, nullptr, nullptr);
	uintptr_t rc = a.rc;
	static volatile UInt8 logged_;
	if ((uint32_t)rc != 0 && !OSTestAndSet(0, &logged_))
		LOG("post %u -> 0x%x", msg, (uint32_t)rc);
}

/* =========================================================================
 * Nub
 * ========================================================================= */
OSDefineMetaClassAndStructors(RTLAirNub, IOService)

/* Writes the trace every second for the first 3 minutes, then every 10 s. */
static thread_call_t gFlushCall;
static uint64_t gFlushStartMs;

static void flushEntry(thread_call_param_t, thread_call_param_t)
{
	static SInt32 lastStatus = -1;
	static uint32_t lastPowerSets;
	flushTrace();
	/* Not on status calls alone: the link poll makes one a second and
	 * pushed everything else out of the trace. */
	if (gPowerSets != lastPowerSets) {
		lastStatus = gStatusCalls;
		lastPowerSets = gPowerSets;
		airLog("@%llu status calls %d, setPOWER x%u (last %u)", uptimeMs(), (int)lastStatus,
		       lastPowerSets, gPowerLast);
	}
	uint64_t deadline;
	clock_interval_to_deadline(uptimeMs() - gFlushStartMs < 180000 ? 1000 : 10000,
	                           kMillisecondScale, &deadline);
	thread_call_enter_delayed(gFlushCall, deadline);
}

bool RTLAirNub::start(IOService *provider)
{
	uint32_t armed = 0;
	if (!gLogLock)
		gLogLock = IOLockAlloc();
	gLogTarget = provider;
	if (!PE_parse_boot_argn("rtlair", &armed, sizeof(armed)) || armed == 0) {
		LOG("not armed (boot-arg rtlair=1, or 2 with data path), staying out");
		return false;
	}
	LOG("nub 0.27, rtlair=%u", armed);
	if (!gWorkQueue)
		gWorkQueue = (IO80211WorkQueue *)rtl_io_workQueue();
	if (!gWorkQueue) {
		LOG("IO80211WorkQueue failed");
		return false;
	}
	if (!IOService::start(provider))
		return false;
	setName("RTLAirNub");
	if (!gFlushCall && (gFlushCall = thread_call_allocate(flushEntry, nullptr))) {
		gFlushStartMs = uptimeMs();
		thread_call_enter(gFlushCall);
	}
	registerService();
	return true;
}

void RTLAirNub::stop(IOService *provider)
{
	if (gFlushCall) {
		thread_call_cancel_wait(gFlushCall);
		thread_call_free(gFlushCall);
		gFlushCall = nullptr;
	}
	IOService::stop(provider);
}

IOWorkLoop *RTLAirNub::getWorkLoop() const
{
	return (IOWorkLoop *)gWorkQueue;
}

/* =========================================================================
 * Controller (provider: RTLAirNub, radio: the nub's RTL8814Native)
 * ========================================================================= */
bool RTLAirController::start(IOService *provider)
{
	uint32_t armed = 0;
	IOService *radioSvc = OSDynamicCast(RTLAirNub, provider) ? provider->getProvider() : nullptr;
	if (!radioSvc || !gLogLock || !PE_parse_boot_argn("rtlair", &armed, sizeof(armed)) || armed == 0)
		return false;
	/* Bisection of the radio stall seen with 0.8 (boot-arg rtlairstep, 0 = all):
	 * 1 nub only, 2 stop before IO80211Controller::start, 3 stop after it (no
	 * interface), 4 interface attached but not started. */
	uint32_t step = 0;
	PE_parse_boot_argn("rtlairstep", &step, sizeof(step));
	/* Everything allocated before IO80211Controller::start is released by free(). */
	_armed = armed;
	LOG("start 0.27, rtlair=%u rtlairstep=%u", armed, step);
	if (step == 1) {
		LOG("rtlairstep 1: nub only");
		return false;
	}
	_symTx = OSSymbol::withCString(RTL_RADIO_FN_TX);
	uint32_t abi = 0;
	_radio = radioSvc;
	if (radio(RTL_RADIO_FN_ABI, &abi) != kIOReturnSuccess || abi != RTL_RADIO_ABI) {
		LOG("radio ABI %u, need %u", abi, RTL_RADIO_ABI);
		_radio = nullptr;
		return false;
	}
	static const uint8_t zero[6] = {};
	if (radio(RTL_RADIO_FN_MAC, _mac) != kIOReturnSuccess || !memcmp(_mac, zero, 6)) {
		LOG("radio has no MAC yet");
		_radio = nullptr;
		return false;
	}
	/* With rtlair=1 en2 (RTLEthernetAdapter) keeps the hardware MAC, and
	 * configd's InterfaceNamer keys its database by MAC: it gave this
	 * interface en2's unit and the naming failed forever (kIOReturnNoSpace),
	 * so no IOInterfaceUnit, no IOSkywalkNetworkBSDClient, nothing for
	 * airportd. A locally administered copy gets a unit of its own. */
	if (_armed < 2)
		_mac[0] |= 0x02;
	_lock = IOLockAlloc();
	_rxLock = IOLockAlloc();
	_poll = thread_call_allocate(&RTLAirController::pollEntry, this);
	_rxDrain = thread_call_allocate(&RTLAirController::rxDrainEntry, this);
	_scan = (struct rtl_radio_bss *)IOMalloc(sizeof(*_scan) * kScanMax);
	_regInfo = (uint8_t *)IOMalloc(REGINFO_SIZE);
	if (!_symTx || !_lock || !_rxLock || !_poll || !_rxDrain || !_scan || !_regInfo) {
		LOG("allocation failed");
		_radio = nullptr;
		return false;
	}
	_power = 1;
	/* CCLogPipe::start adds a timer to the owner's getWorkLoop(): needs the queue. */
	if (!createWorkQueue() || getWorkLoop() != provider->getWorkLoop()) {
		LOG("work loop is not the nub's");
		_radio = nullptr;
		return false;
	}
	LOG("work queue %s", ((OSObject *)gWorkQueue)->getMetaClass()->getClassName());
	if (!createCoreCapture()) {
		_radio = nullptr;
		return false;
	}
	LOG("radio ABI %u, mac %02x:%02x:%02x:%02x:%02x:%02x", abi, _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);
	if (step == 2) {
		LOG("rtlairstep 2: stop before IO80211Controller::start");
		_radio = nullptr;
		return false;
	}

	if (!RTLAirControllerStubs::start(provider)) {
		LOG("IO80211Controller::start failed");
		_radio = nullptr;
		return false;
	}

	if (step == 3) {
		LOG("rtlairstep 3: IO80211Controller::start ok, no interface");
		return true;
	}

	bool attached = false;
	_iface = OSTypeAlloc(RTLAirInterface);
	if (!_iface || !_iface->initWithController(this, _mac)) {
		LOG("interface init failed");
	} else {
		attached = true;
		LOG("IO80211Controller::start ok, interface initialized");
		attachInterface(_iface, this);
		if (step == 4) {
			LOG("rtlairstep 4: interface attached, not started");
			return true;
		}

		/* As AppleBCMWLANCore::createSkywalkInterface fills it. Kept until
		 * free(): Skywalk may hold on to the pointer (YYavicWiFi saw it stored
		 * in the interface's expansion data on 15.8.1). */
		uint8_t *reg = _regInfo;
		bzero(reg, REGINFO_SIZE);
		rtl_sky_initRegInfo(_iface, reg, 1, REGINFO_SIZE);
		*(uint32_t *)(reg + REGINFO_SUBFAMILY) = 3;
		*(uint32_t *)(reg + REGINFO_ROLE) = 1;
		*(uint32_t *)(reg + REGINFO_UNK14) = 2;
		*(uint64_t *)(reg + REGINFO_UNK24) = 0x8000000080ULL;
		*(uint16_t *)(reg + REGINFO_HEADROOM) = 64;
		*(uint32_t *)(reg + REGINFO_FLAGS) = 0;
		memcpy(reg + REGINFO_MAC, _mac, 6);
		if (_iface->startWithRegistration(this, reg)) {
			registerService();
			LOG("started, %02x:%02x:%02x:%02x:%02x:%02x", _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);
			return true;
		}
		LOG("interface start failed");
	}

	/* Undo the half-started state; IOKit does not call stop() after a failed start(). */
	quiesce();
	if (_iface) {
		if (attached)
			_iface->terminate();
		OSSafeReleaseNULL(_iface);
	}
	RTLAirControllerStubs::stop(provider);
	_radio = nullptr;
	return false;
}

/* Stops every path into the radio and the interface: data hook, poll, RX drain. */
void RTLAirController::quiesce()
{
	_stopping = true;
	bzero(&_joinReq, sizeof(_joinReq));
	setDataPath(false);            /* the radio's hook lock: no rxHook after this */
	if (_poll) {
		thread_call_cancel_wait(_poll);
		thread_call_cancel_wait(_poll);    /* a poll that was running may have re-armed */
	}
	if (_rxDrain)
		thread_call_cancel_wait(_rxDrain);
	flushRx();
}

void RTLAirController::stop(IOService *provider)
{
	quiesce();
	if (_iface) {
		_iface->terminate();
		OSSafeReleaseNULL(_iface);
	}
	_radio = nullptr;
	RTLAirControllerStubs::stop(provider);
}

void RTLAirController::free()
{
	if (_poll) {
		thread_call_free(_poll);
		_poll = nullptr;
	}
	if (_rxDrain) {
		thread_call_free(_rxDrain);
		_rxDrain = nullptr;
	}
	if (_scan) {
		IOFree(_scan, sizeof(*_scan) * kScanMax);
		_scan = nullptr;
	}
	if (_regInfo) {
		IOFree(_regInfo, REGINFO_SIZE);
		_regInfo = nullptr;
	}
	if (_lock) {
		IOLockFree(_lock);
		_lock = nullptr;
	}
	if (_rxLock) {
		IOLockFree(_rxLock);
		_rxLock = nullptr;
	}
	OSSafeReleaseNULL(_symTx);
	OSSafeReleaseNULL(_ioFault);
	OSSafeReleaseNULL(_ccFault);
	OSSafeReleaseNULL(_ccDataStream);
	OSSafeReleaseNULL(_ccDataPipe);
	OSSafeReleaseNULL(_ccLogStream);
	OSSafeReleaseNULL(_ccLogPipe);
	OSSafeReleaseNULL(_ccWorkLoop);
	RTLAirControllerStubs::free();
}

/*
 * IO80211Controller::start (25.6.0) stores getLogger() as the family's global
 * CCLogStream and panics ("No ivars->_faultReporter") when
 * getFaultReporterFromDriver() returns NULL. Both are built here before it
 * runs, the way AppleMultiFunctionManagerLogger::initWithProviderAndOptions
 * does on 25.6.0: a log pipe + log stream, and a data pipe + data stream
 * under a CCFaultReporter, wrapped in IO80211FaultReporter as
 * AppleBCMWLANBusInterfacePCIe::deferredStart does.
 * CCPipeOptions is 0x350 bytes, CCStreamOptions 0x358 (offsets from that code).
 */
#define CC_OWNER        "com.DrogaBox.driver.RTL8814AirLayer"
#define CC_PIPE_OPTS    0x350
#define CC_STREAM_OPTS  0x358

static bool isClass(OSObject *o, const char *name)
{
	return o && !strcmp(o->getMetaClass()->getClassName(), name);
}

bool RTLAirController::createCoreCapture()
{
	uint8_t *po = (uint8_t *)IOMallocZero(CC_PIPE_OPTS);
	uint8_t *so = (uint8_t *)IOMallocZero(CC_STREAM_OPTS);
	bool ok = false;
	if (!po || !so)
		goto out;

	/* log pipe (type 0) + log stream */
	*(uint32_t *)(po + 0x08) = 1;
	*(uint64_t *)(po + 0x10) = 0x1000;
	*(uint64_t *)(po + 0x18) = 0x666;
	*(uint32_t *)(po + 0x20) = 1000;
	strlcpy((char *)po + 0x24, "RTLAirLogs", 0x100);
	strlcpy((char *)po + 0x124, "rtlair", 0x100);
	*(uint64_t *)(po + 0x224) = 0x200080000ULL;
	strlcpy((char *)po + 0x244, "WiFi", 0x100);
	_ccLogPipe = (OSObject *)rtl_cc_pipe(this, CC_OWNER, "DriverLogs", po);
	if (!_ccLogPipe) {
		LOG("CoreCapture log pipe failed");
		goto out;
	}
	*(uint32_t *)(so + 0x08) = 5;
	*(uint32_t *)(so + 0x0c) = 1;
	strlcpy((char *)so + 0x58, "rtlair", 0xf0);
	_ccLogStream = (OSObject *)rtl_cc_stream(_ccLogPipe, "loggerstream", so);
	if (!isClass(_ccLogStream, "CCLogStream")) {
		LOG("CoreCapture log stream failed (%s)", _ccLogStream ? _ccLogStream->getMetaClass()->getClassName() : "null");
		goto out;
	}
	rtl_cc_startPipe(_ccLogPipe);

	/* data pipe (type 1) + data stream -> CCFaultReporter -> IO80211FaultReporter */
	bzero(po, CC_PIPE_OPTS);
	*(uint32_t *)(po + 0x00) = 1;
	*(uint32_t *)(po + 0x04) = 2;
	*(uint64_t *)(po + 0x08) = 2;
	*(uint64_t *)(po + 0x10) = 64;
	strlcpy((char *)po + 0x24, "StateSnapshots", 0x100);
	strlcpy((char *)po + 0x124, "0", 0x100);
	strlcpy((char *)po + 0x244, "WiFi", 0x100);
	_ccDataPipe = (OSObject *)rtl_cc_pipe(this, CC_OWNER, "StateSnapshots", po);
	if (!_ccDataPipe) {
		LOG("CoreCapture data pipe failed");
		goto out;
	}
	bzero(so, CC_STREAM_OPTS);
	*(uint32_t *)(so + 0x00) = 1;
	*(int64_t *)(so + 0x08) = -1;
	_ccDataStream = (OSObject *)rtl_cc_stream(_ccDataPipe, "FaultReporter", so);
	if (!isClass(_ccDataStream, "CCDataStream")) {
		LOG("CoreCapture data stream failed (%s)", _ccDataStream ? _ccDataStream->getMetaClass()->getClassName() : "null");
		goto out;
	}
	_ccWorkLoop = IOWorkLoop::workLoop();
	if (!_ccWorkLoop)
		goto out;
	_ccFault = (OSObject *)rtl_cc_faultReporter(_ccDataStream, _ccWorkLoop);
	if (!_ccFault) {
		LOG("CCFaultReporter failed");
		goto out;
	}
	rtl_cc_addOwner(_ccFault, CC_OWNER);
	rtl_cc_addOwner(_ccFault, "com.apple.iokit.IO80211Family");
	rtl_cc_startPipe(_ccDataPipe);
	_ioFault = (OSObject *)rtl_io_faultReporter(_ccFault);
	if (!_ioFault) {
		LOG("IO80211FaultReporter failed");
		goto out;
	}
	LOG("CoreCapture ready: %s, %s", _ccLogStream->getMetaClass()->getClassName(),
	    _ioFault->getMetaClass()->getClassName());
	ok = true;
out:
	if (po)
		IOFree(po, CC_PIPE_OPTS);
	if (so)
		IOFree(so, CC_STREAM_OPTS);
	return ok;
}

void *RTLAirController::getLogger() const
{
	return _ccLogStream;
}

/* As AppleBCMWLANCore::createWorkQueue (wlan.workQueue.create path). */
uintptr_t RTLAirController::createWorkQueue()
{
	if (!gWorkQueue)
		gWorkQueue = (IO80211WorkQueue *)rtl_io_workQueue();
	return gWorkQueue != nullptr;
}

IO80211WorkQueue *RTLAirController::getWorkQueue() const
{
	return gWorkQueue;
}

void *RTLAirController::getFaultReporterFromDriver()
{
	return _ioFault;
}

IOReturn RTLAirController::getHardwareAddress(IOEthernetAddress *addr)
{
	if (!addr)
		return kIOReturnBadArgument;
	memcpy(addr->bytes, _mac, 6);
	return kIOReturnSuccess;
}

bool RTLAirController::isCommandProhibited(int)
{
	STUB_ENTER();
	return false;
}

IOReturn RTLAirController::getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *vd)
{
	STUB_ENTER();
	if (!vd)
		return kIOReturnBadArgument;
	putStr((uint8_t *)vd, "RTL8814AirLayer 0.27 (radio ABI 1)");
	return kIOReturnSuccess;
}

IOReturn RTLAirController::getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *vd)
{
	STUB_ENTER();
	if (!vd)
		return kIOReturnBadArgument;
	putStr((uint8_t *)vd, "Realtek RTL8814AU");
	return kIOReturnSuccess;
}

/*
 * apple80211_capability_data: +0 version, +4..+27 capability bits. Only the
 * bits AppleBCMWLANCore::getCARD_CAPABILITIES sets unconditionally; the ones
 * it derives from Broadcom feature flags are left clear.
 */
IOReturn RTLAirController::getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *cd)
{
	STUB_ENTER();
	if (!cd)
		return kIOReturnBadArgument;
	uint8_t *p = (uint8_t *)cd;
	bzero(p + 4, 24);
	*(uint32_t *)(p + 0) = 1;
	*(uint16_t *)(p + 4) = 0xE66F;
	p[6] = 0x61;
	p[7] = 0x04;
	p[9] = 0x40;
	p[10] = 0x04;
	*(uint16_t *)(p + 12) = 0x0201;
	/*
	 * AWDL: wifip2pd creates awdl0 only when the userspace copy of this data
	 * (IO80211Family drops the 4-byte version) has byte 4 bit 0 set, which is
	 * p[8] bit 0 here. Broadcom sets p[8] and p[7] bit 0 together. NAN (p[13]
	 * bit 0) stays clear. See Docs/ARCHITECTURE.md, "AWDL gate FOUND".
	 */
	p[7] |= 0x01;
	p[8] |= 0x01;
	return kIOReturnSuccess;
}

/* apple80211_power_data: +4 radios, +8 state (as AppleBCMWLANCore::getPOWER) */
IOReturn RTLAirController::getPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd)
{
	STUB_ENTER();
	if (!pd)
		return kIOReturnBadArgument;
	*(uint32_t *)((uint8_t *)pd + 0) = 1;
	*(uint32_t *)((uint8_t *)pd + 4) = 1;
	*(uint32_t *)((uint8_t *)pd + 8) = _power;
	return kIOReturnSuccess;
}

/* Power is only recorded: the radio stays up (RTL8814Native owns it). */
/* apple80211_country_code_data (8): +4 two letters and a NUL. "XZ" is the
 * world default Broadcom reports before it knows the country. */
IOReturn RTLAirController::getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *cd)
{
	STUB_ENTER();
	char cc[2] = { 'X', 'Z' };
	uint8_t *p = (uint8_t *)cd;
	if (!p)
		return kIOReturnBadArgument;
	countryCode(cc);
	p[4] = (uint8_t)cc[0];
	p[5] = (uint8_t)cc[1];
	p[6] = 0;
	return kIOReturnSuccess;
}

/* apple80211_platform_config (AppleBCMWLANCore::getPLATFORM_CONFIG): one
 * flag per byte, +0 6 GHz supported. This radio has none of them. */
IOReturn RTLAirController::getPLATFORM_CONFIG(IO80211SkywalkInterface *, apple80211_platform_config *pc)
{
	STUB_ENTER();
	if (!pc)
		return kIOReturnBadArgument;
	memset(pc, 0, 7);
	return kIOReturnSuccess;
}

IOReturn RTLAirController::setPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd)
{
	STUB_ENTER();
	if (!pd)
		return kIOReturnBadArgument;
	_power = *(uint32_t *)((uint8_t *)pd + 8) ? 1 : 0;
	gPowerSets++;
	gPowerLast = _power;
	if (!_power)
		leave(true);
	return kIOReturnSuccess;
}

/* ---- scan / join state machine ---- */

void RTLAirController::schedulePoll(uint32_t ms)
{
	if (_stopping || !_poll)
		return;
	uint64_t deadline;
	clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
	thread_call_enter_delayed(_poll, deadline);
}

IOReturn RTLAirController::startScan()
{
	struct rtl_radio_status st;
	if (_stopping)
		return kIOReturnNotReady;
	radioStatus(&st);
	IOLockLock(_lock);
	if (_scanPending || _phase == kJoining || _phase == kRoaming) {
		IOLockUnlock(_lock);
		return kIOReturnBusy;
	}
	IOReturn r = radio(RTL_RADIO_FN_SCAN_START);
	if (r == kIOReturnSuccess) {
		_scanPending = true;
		_scanLinked = st.state == RTL_RADIO_S_LINKED;
		_scanTicks = 0;
	}
	IOLockUnlock(_lock);
	if (r == kIOReturnSuccess)
		schedulePoll(500);
	return r;
}

IOReturn RTLAirController::startJoin(const struct rtl_radio_join *j)
{
	if (_stopping)
		return kIOReturnNotReady;
	/* A join while linked replaces the link: data goes back to en2 until the new one is up. */
	setDataPath(false);
	syncMac();
	struct rtl_radio_status st;
	radioStatus(&st);
	IOLockLock(_lock);
	IOReturn r = radio(RTL_RADIO_FN_JOIN, (void *)j);
	if (r == kIOReturnSuccess) {
		_phase = kJoining;
		_joinTicks = 0;
		_joinRetries = 0;
		_joinTrace = ~0u;
		memcpy(_joinBssid, j->bssid, 6);
		memcpy(&_joinReq, j, sizeof(_joinReq));
	}
	IOLockUnlock(_lock);
	LOG("join: radio state %u scanning %u before, rtlRadioJoin 0x%x", st.state, st.scanning, r);
	if (r == kIOReturnSuccess)
		schedulePoll(500);
	return r;
}

/*
 * The interface's address is the one the IO80211MacAddressAgent picked (the
 * private address, not the hardware one): the radio must use it on air, or
 * the AP never sees the stack's frames as coming from the associated station.
 * The radio refuses while linked, so a link of its own (en2's) goes first.
 */
void RTLAirController::syncMac()
{
	uint8_t want[6], have[6];
	OSData *d = _iface ? OSDynamicCast(OSData, _iface->getProperty("IOMACAddress")) : nullptr;
	if (!d || d->getLength() != 6)
		return;
	memcpy(want, d->getBytesNoCopy(), 6);
	if (radio(RTL_RADIO_FN_MAC, have) == kIOReturnSuccess && !memcmp(want, have, 6))
		return;
	IOReturn r = radio(RTL_RADIO_FN_SET_MAC, want);
	if (r == kIOReturnBusy) {
		radio(RTL_RADIO_FN_LEAVE);
		r = radio(RTL_RADIO_FN_SET_MAC, want);
	}
	LOG("radio MAC -> %02x:%02x:%02x:%02x:%02x:%02x: 0x%x", want[0], want[1], want[2],
	    want[3], want[4], want[5], r);
}

/* msg 216 with link down, as AppleBCMWLANCore::sendInternalLinkDownInd (reason 9) */
static void linkDownEvent(uint8_t ev[16])
{
	bzero(ev, 16);
	ev[7] = kIfaceId;
	*(uint32_t *)(ev + 8) = 9;
}

/*
 * Only a link this layer made is torn down. WCL sends leave / power-off at
 * boot and on menu toggles; the radio may then be linked for en2 alone, and
 * dropping that link would cut the machine off.
 */
void RTLAirController::leave(bool notify)
{
	IOLockLock(_lock);
	bool wasLinked = _phase == kLinked || _phase == kRoaming;
	bool owned = _phase != kIdle;
	_phase = kIdle;
	bzero(&_joinReq, sizeof(_joinReq));
	IOLockUnlock(_lock);
	if (!owned)
		return;
	setDataPath(false);
	radio(RTL_RADIO_FN_LEAVE);
	if (notify && wasLinked) {
		uint8_t ev[16];
		linkDownEvent(ev);
		post(kMsgLink, ev, sizeof(ev));
	}
}

/* The radio dropped the link (deauth, beacon loss): tell WCL, and stop the
 * vendor driver's own reconnect so WCL alone decides what to join next. */
void RTLAirController::linkLost()
{
	IOLockLock(_lock);
	bool wasLinked = _phase == kLinked;
	_phase = kIdle;
	bzero(&_joinReq, sizeof(_joinReq));
	IOLockUnlock(_lock);
	if (!wasLinked)
		return;
	LOG("link lost");
	setDataPath(false);
	radio(RTL_RADIO_FN_LEAVE);
	uint8_t ev[16];
	linkDownEvent(ev);
	post(kMsgLink, ev, sizeof(ev));
}

/* Current BSS from the last scan, as one beacon message (20 MHz chanspec:
 * a wider one needs the center channel and sideband, not the primary). */
bool RTLAirController::copyBeacon(const uint8_t bssid[6], uint8_t *msg)
{
	bool found = false;
	IOLockLock(_lock);
	for (uint32_t i = 0; i < _scanCount; i++) {
		if (memcmp(_scan[i].bssid, bssid, 6) == 0) {
			fillBeacon(msg, &_scan[i], RTL_RADIO_BW_20);
			found = true;
			break;
		}
	}
	IOLockUnlock(_lock);
	return found;
}

/* The BSS's beacon as an 802.11 frame: header, zero timestamp, interval,
 * capabilities, then the IEs from the scan. Returns its length, 0 if unknown. */
uint32_t RTLAirController::copyBeaconFrame(const uint8_t bssid[6], uint8_t *f, uint32_t max)
{
	uint32_t len = 0;
	IOLockLock(_lock);
	for (uint32_t i = 0; i < _scanCount; i++) {
		const struct rtl_radio_bss *b = &_scan[i];
		if (memcmp(b->bssid, bssid, 6))
			continue;
		uint32_t ies = b->ies_len;
		if (ies > RTL_RADIO_MAX_IES)
			ies = RTL_RADIO_MAX_IES;
		if (36 + ies > max)
			ies = max - 36;
		bzero(f, 36);
		f[0] = 0x80;                            /* beacon */
		memset(f + 4, 0xff, 6);
		memcpy(f + 10, b->bssid, 6);
		memcpy(f + 16, b->bssid, 6);
		*(uint16_t *)(f + 32) = b->intval;
		*(uint16_t *)(f + 34) = b->capinfo;
		memcpy(f + 36, b->ies, ies);
		len = 36 + ies;
		break;
	}
	IOLockUnlock(_lock);
	return len;
}

/* Country from the APs' Country element (802.11d): the current BSS's if it
 * has one, else the first BSS seen with one. */
static bool ieCountry(const struct rtl_radio_bss *b, char cc[2])
{
	uint32_t n = b->ies_len > RTL_RADIO_MAX_IES ? RTL_RADIO_MAX_IES : b->ies_len;
	for (uint32_t o = 0; o + 2 <= n; o += 2 + b->ies[o + 1]) {
		if (b->ies[o] == 7 && b->ies[o + 1] >= 2 && o + 4 <= n &&
		    b->ies[o + 2] >= 'A' && b->ies[o + 2] <= 'Z' && b->ies[o + 3] >= 'A' && b->ies[o + 3] <= 'Z') {
			cc[0] = (char)b->ies[o + 2];
			cc[1] = (char)b->ies[o + 3];
			return true;
		}
	}
	return false;
}

/* 20 MHz chanspecs of the joined SSID's channels, the current one first. */
static uint16_t chanspec20(uint8_t ch, uint8_t band)
{
	return (uint16_t)((band == RTL_RADIO_BAND_5G ? 0xC000 : 0) | 0x1000 | ch);
}

uint32_t RTLAirController::hotChannels(uint16_t *spec, uint32_t max)
{
	uint32_t n = 0;
	IOLockLock(_lock);
	if ((_phase == kLinked || _phase == kRoaming) && _joinReq.ssid_len && max)
		spec[n++] = chanspec20(_linkSt.channel, _linkSt.band);
	for (uint32_t i = 0; n && i < _scanCount && n < max; i++) {
		const struct rtl_radio_bss *b = &_scan[i];
		if (b->ssid_len != _joinReq.ssid_len || memcmp(b->ssid, _joinReq.ssid, b->ssid_len))
			continue;
		uint16_t s = chanspec20(b->channel, b->band);
		bool dup = false;
		for (uint32_t k = 0; k < n && !dup; k++)
			dup = spec[k] == s;
		if (!dup)
			spec[n++] = s;
	}
	IOLockUnlock(_lock);
	return n;
}

bool RTLAirController::countryCode(char cc[2])
{
	bool found = false;
	IOLockLock(_lock);
	bool linked = _phase == kLinked || _phase == kRoaming;
	for (uint32_t i = 0; linked && i < _scanCount && !found; i++)
		if (!memcmp(_scan[i].bssid, _linkSt.bssid, 6))
			found = ieCountry(&_scan[i], cc);
	for (uint32_t i = 0; i < _scanCount && !found; i++)
		found = ieCountry(&_scan[i], cc);
	IOLockUnlock(_lock);
	return found;
}

void RTLAirController::pollEntry(thread_call_param_t p0, thread_call_param_t)
{
	((RTLAirController *)p0)->poll();
}

/*
 * One poll serves the scan, the join and the link monitor; whichever needs
 * the soonest tick re-arms it.
 */
void RTLAirController::poll()
{
	struct rtl_radio_status st;
	if (_stopping)
		return;
	radioStatus(&st);
	IOLockLock(_lock);
	bool scan = _scanPending;
	bool scanLinked = _scanLinked;
	int phase = _phase;
	uint32_t scanTicks = scan ? ++_scanTicks : 0;
	uint32_t joinTicks = phase == kJoining || phase == kRoaming ? ++_joinTicks : 0;
	IOLockUnlock(_lock);

	uint32_t next = 0;
	if (scan) {
		/* An associated radio stays LINKED while it scans: builds from 4.11 set
		 * st.scanning, older ones get a fixed 2.75 s. 10 s cap either way. */
		bool busy = st.state == RTL_RADIO_S_SCAN || st.scanning || (scanLinked && scanTicks < 10);
		if (busy && scanTicks < 40)
			next = 250;
		else
			finishScan();
	}
	if (phase == kJoining) {
		uint32_t trace = st.state | (uint32_t)st.port_valid << 8 | (uint32_t)st.scanning << 9;
		/* The first join after a disconnect falls back to idle ~1.5 s in. */
		bool dropped = (_joinTrace & 0xFF) == RTL_RADIO_S_JOINING && st.state == RTL_RADIO_S_IDLE;
		if (trace != _joinTrace) {
			_joinTrace = trace;
			LOG("join tick %u: state %u port %u scanning %u", joinTicks, st.state,
			    st.port_valid, st.scanning);
		}
		if (st.state == RTL_RADIO_S_LINKED && st.port_valid) {
			reportJoin(true, &st);
			if (!next)
				next = 1000;
		} else if (joinTicks >= 80) {        /* 20 s, as test_uc join */
			reportJoin(false, &st);
		} else {
			/* rtw_set_802_11_connect reports success without joining while
			 * the radio is still under linking, and a join it deferred to a
			 * scan can be lost: an idle radio 3 s in gets the join again. */
			if ((dropped || joinTicks % 12 == 0) && st.state == RTL_RADIO_S_IDLE &&
			    !st.scanning && _joinRetries < 2)
				retryJoin();
			next = 250;
		}
	} else if (phase == kRoaming) {
		uint32_t trace = st.state | (uint32_t)st.port_valid << 8 | (uint32_t)st.scanning << 9;
		bool dropped = (_joinTrace & 0xFF) == RTL_RADIO_S_JOINING && st.state == RTL_RADIO_S_IDLE;
		if (trace != _joinTrace) {
			_joinTrace = trace;
			LOG("roam tick %u: state %u port %u scanning %u", joinTicks, st.state,
			    st.port_valid, st.scanning);
		}
		if (st.state == RTL_RADIO_S_LINKED && st.port_valid &&
		    !memcmp(st.bssid, _joinReq.bssid, 6)) {
			reportRoam(true, &st);
			if (!next)
				next = 1000;
		} else if (joinTicks >= 80) {
			reportRoam(false, &st);
		} else {
			if ((dropped || joinTicks % 12 == 0) && st.state == RTL_RADIO_S_IDLE &&
			    !st.scanning && _joinRetries < 2)
				retryJoin();
			next = 250;
		}
	} else if (phase == kLinked) {
		if (st.state != RTL_RADIO_S_LINKED) {
			linkLost();
		} else {
			IOLockLock(_lock);
			memcpy(&_linkSt, &st, sizeof(_linkSt));
			IOLockUnlock(_lock);
			if (++_lqmTicks >= kLqmTicks) {
				_lqmTicks = 0;
				reportLqm(&st);
			}
			if (!next)
				next = 1000;
		}
	}
	if (next)
		schedulePoll(next);
}

void RTLAirController::finishScan()
{
	uint32_t n = kScanMax;
	IOLockLock(_lock);                      /* copyBeacon reads _scan */
	if (radio(RTL_RADIO_FN_SCAN_COPY, _scan, &n) != kIOReturnSuccess)
		n = 0;
	_scanCount = n;
	IOLockUnlock(_lock);
	/* Only this thread call writes _scan, so it is read unlocked from here. */
	uint8_t *msg = (uint8_t *)IOMalloc(BEACON_HDR + BEACON_MAX_IES);
	if (msg) {
		for (uint32_t i = 0; i < n; i++) {
			fillBeacon(msg, &_scan[i], RTL_RADIO_BW_20);
			post(kMsgBeacon, msg, BEACON_HDR + *(uint32_t *)msg);
		}
		IOFree(msg, BEACON_HDR + BEACON_MAX_IES);
	}
	uint32_t status = 0;
	post(kMsgWclScanDone, &status, sizeof(status));
	/* What ends airportd's scan: IO80211InfraInterface::postMessage turns
	 * APPLE80211_M_SCAN_DONE into IO80211ScanManager::scanDone, which pulls
	 * the results (from WCL, built from the 201s) into the scan cache.
	 * Without it the ScanManager times out after 20 s with nothing. */
	post(kMsgScanDone, &status, sizeof(status));
	LOG("scan done, %u BSS", n);
	IOLockLock(_lock);
	_scanPending = false;
	IOLockUnlock(_lock);
}

/*
 * The event sequence Broadcom posts for a join (211 auth/assoc, 216 link,
 * 212 SET_SSID, 213 connect complete). WCL then moves the link state.
 * A leave or abort that landed since the poll read the phase wins.
 */
void RTLAirController::retryJoin()
{
	IOLockLock(_lock);
	IOReturn r = kIOReturnNotReady;
	if (_phase == kJoining || _phase == kRoaming) {
		_joinRetries++;
		r = radio(RTL_RADIO_FN_JOIN, &_joinReq);
	}
	uint32_t n = _joinRetries;
	IOLockUnlock(_lock);
	LOG("join retry %u: 0x%x", n, r);
}

/* 5 GHz wins while it is usable; otherwise the stronger signal, with some hysteresis. */
#define kRoamMinRssi        (-75)
#define kRoamMargin         8
static int roamScore(int rssi, uint8_t band)
{
	return rssi + (band == RTL_RADIO_BAND_5G && rssi >= -65 ? 30 : 0);
}

/*
 * WCL_REASSOC: move the link to a better BSS of the same network. The radio
 * has no roam of its own, so this is the menu reconnect run back to back:
 * leave, then join the target with the PMK of the current link. WCL gets the
 * events Broadcom posts for it (73 reassoc status, 80 roam done) and picks
 * the new BSS up through WCL_BSS_INFO on its next assoc timer tick
 * (WCLNetManager::updateBss -> setCurrentBSS).
 */
IOReturn RTLAirController::startRoam(const uint8_t *bssids, uint32_t count)
{
	static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct rtl_radio_status st;
	if (_stopping)
		return kIOReturnNotReady;
	radioStatus(&st);

	bool any = count == 0;
	for (uint32_t i = 0; i < count; i++)
		if (!memcmp(bssids + 6 * i, bcast, 6))
			any = true;

	IOLockLock(_lock);
	if (_phase != kLinked || !_joinReq.ssid_len || st.state != RTL_RADIO_S_LINKED) {
		int phase = _phase;
		IOLockUnlock(_lock);
		LOG("roam refused: phase %d, radio state %u", phase, st.state);
		/* NotReady: WCL keeps the request and tries it again later */
		return kIOReturnNotReady;
	}
	const struct rtl_radio_bss *to = nullptr;
	int best = roamScore(st.rssi_dbm, st.band) + kRoamMargin;
	for (uint32_t i = 0; i < _scanCount; i++) {
		const struct rtl_radio_bss *b = &_scan[i];
		if (b->ssid_len != _joinReq.ssid_len || memcmp(b->ssid, _joinReq.ssid, b->ssid_len) ||
		    !memcmp(b->bssid, st.bssid, 6))
			continue;
		if (!any) {
			/* WCL named the target: take it as long as it is this network */
			for (uint32_t k = 0; k < count && !to; k++)
				if (!memcmp(b->bssid, bssids + 6 * k, 6))
					to = b;
			if (to)
				break;
			continue;
		}
		int score = roamScore(b->rssi_dbm, b->band);
		if (b->rssi_dbm >= kRoamMinRssi && score > best) {
			best = score;
			to = b;
		}
	}
	if (!to) {
		IOLockUnlock(_lock);
		LOG("roam: nothing better than ch %u rssi %d (%u BSS cached)", st.channel,
		    st.rssi_dbm, _scanCount);
		return kIOReturnNotFound;
	}
	memcpy(_joinReq.bssid, to->bssid, 6);
	memcpy(_joinBssid, to->bssid, 6);
	memcpy(&_linkSt, &st, sizeof(_linkSt));
	_phase = kRoaming;
	_joinTicks = 0;
	_joinRetries = 0;
	_joinTrace = ~0u;
	uint8_t toChan = to->channel;
	int toRssi = to->rssi_dbm;
	IOLockUnlock(_lock);
	LOG("roam: ch %u rssi %d -> ch %u rssi %d", st.channel, st.rssi_dbm, toChan, toRssi);

	setDataPath(false);
	radio(RTL_RADIO_FN_LEAVE);
	IOLockLock(_lock);
	IOReturn r = _phase == kRoaming ? radio(RTL_RADIO_FN_JOIN, &_joinReq) : kIOReturnAborted;
	IOLockUnlock(_lock);
	if (r != kIOReturnSuccess) {
		reportRoam(false, &st);
		return r;
	}
	schedulePoll(500);
	return kIOReturnSuccess;
}

void RTLAirController::reportRoam(bool ok, const struct rtl_radio_status *st)
{
	IOLockLock(_lock);
	bool current = _phase == kRoaming;
	if (current) {
		_phase = ok ? kLinked : kIdle;
		_lqmTicks = 0;
		if (ok)
			memcpy(&_linkSt, st, sizeof(_linkSt));
		else
			bzero(&_joinReq, sizeof(_joinReq));
	}
	IOLockUnlock(_lock);
	if (!current)
		return;
	LOG("roam %s after %u ticks, %u retries", ok ? "done" : "failed", _joinTicks, _joinRetries);

	uint32_t status[2] = { ok ? 0u : 0xE0820401u, 0 };
	post(kMsgReassoc, status, sizeof(status));
	if (ok) {
		setDataPath(true);
		uint8_t done[168];
		bzero(done, sizeof(done));
		post(kMsgRoamDone, done, sizeof(done));
		reportLqm(st);
	} else {
		/* The old link is gone too: WCL leaves and airportd joins again. */
		radio(RTL_RADIO_FN_LEAVE);
		uint8_t ev[16];
		linkDownEvent(ev);
		post(kMsgLink, ev, sizeof(ev));
	}
}

/*
 * Link quality report, as AppleBCMWLANLQM::updateLQM posts it every stats
 * period. WCLNetManager::assocTimerAction leaves with "missed beacons timeout"
 * once 60 s pass without WCLNetManager::handleLqmUpdate stamping a beacon
 * (+472/+473/+48 set and the beacon count at +40 non-zero) or traffic. The
 * radio drops the link itself on beacon loss, so while it reports LINKED the
 * beacons are arriving: report what one stats period of them is.
 */
void RTLAirController::reportLqm(const struct rtl_radio_status *st)
{
	uint8_t lqm[LQM_LEN];
	bzero(lqm, sizeof(lqm));
	if (st->rssi_dbm < -10 && st->rssi_dbm > -110) {
		lqm[0] = 1;                                     /* has rssi */
		*(int32_t *)(lqm + 4) = st->rssi_dbm;
	}
	/* Driver 4.16 on: noise floor from the BSS's frames (power - SNR) */
	if (st->noise_dbm < -50 && st->noise_dbm > -110) {
		lqm[14] = 1;                                    /* has noise */
		*(int16_t *)(lqm + 16) = st->noise_dbm;
		if (lqm[0] && st->rssi_dbm > st->noise_dbm) {
			lqm[11] = 1;                            /* has SNR */
			*(int16_t *)(lqm + 12) = (int16_t)(st->rssi_dbm - st->noise_dbm);
		}
	}
	*(uint32_t *)(lqm + 40) = kLqmTicks * 1000 / 102;  /* beacons at 100 TU */
	lqm[48] = 1;                                        /* beacon counters valid */
	lqm[472] = 1;
	lqm[473] = 1;                                       /* stats sample present */
	post(kMsgLqm, lqm, sizeof(lqm));
}

void RTLAirController::reportJoin(bool ok, const struct rtl_radio_status *st)
{
	IOLockLock(_lock);
	bool current = _phase == kJoining;
	if (current)
		_phase = ok ? kLinked : kIdle;
	_lqmTicks = 0;
	if (!ok)
		bzero(&_joinReq, sizeof(_joinReq));
	IOLockUnlock(_lock);
	if (current)
		LOG("join %s after %u ticks, %u retries", ok ? "done" : "failed", _joinTicks, _joinRetries);
	if (!current)
		return;

	uint16_t status = ok ? 0 : 1;          /* IEEE status: 1 = unspecified failure */

	uint8_t aa[28];
	bzero(aa, sizeof(aa));
	*(uint16_t *)(aa + 0) = status;
	post(kMsgAuthAssoc, aa, sizeof(aa));

	if (ok) {
		setDataPath(true);
		uint8_t link[16];
		bzero(link, sizeof(link));
		memcpy(link, st->bssid, 6);
		link[6] = 1;
		link[7] = kIfaceId;
		post(kMsgLink, link, sizeof(link));
	}

	uint8_t ss[20];
	bzero(ss, sizeof(ss));
	*(uint16_t *)(ss + 0) = status;
	memcpy(ss + 4, ok ? st->bssid : _joinBssid, 6);
	*(uint32_t *)(ss + 12) = ok ? 0u : 0xE0820401u;
	post(kMsgSetSsid, ss, sizeof(ss));

	uint8_t cc[164];
	bzero(cc, sizeof(cc));
	*(uint16_t *)(cc + 0) = status;
	post(kMsgConnectDone, cc, sizeof(cc));

	LOG("join %s", ok ? "complete" : "failed");
	if (!ok)
		radio(RTL_RADIO_FN_LEAVE);
}
