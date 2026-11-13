/*
 * RTL8814AirLayer — AWDL stage 1: creation and registration of awdl0.
 *
 * Called from RTLAirInterface::setVIRTUAL_IF_CREATE (type 94, role 6) once the
 * boot-arg rtlawdl is set. The object is built with the family's own init and
 * start, registered as a Skywalk ethernet interface and published with
 * registerService(), so the BSD client of IO80211Family composes "awdl0" and
 * wifip2pd finds the interface it asked for. See
 * Docs/AWDL_INTERFACE_BIRTH.md §2.3-§2.4 and §5.
 *
 * Stage 1 has no data path: the three queues are registered but carry no
 * action, and every AWDL request answers kIOReturnUnsupported.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "AWDLInterface.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <net/ethernet.h>
#include <net/kpi_interface.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>

/* airLog() of RTLAirLayer.cpp, exposed by the hook patch so these lines land
 * in the RTLAirLayerLog array of the RTL8814Native node. */
extern void rtlAirLog(const char *fmt, ...) __printflike(1, 2);
#define LOG(fmt, ...) awdlLog(fmt, ##__VA_ARGS__)

/* Each line also goes to a ring of its own, the RTLAwdlLog array of awdl0,
 * which the radio polling in RTLAirLayerLog cannot push out. */
#define AWDL_LOG_MAX 256
static IOLock *gAwdlLogLock;
static OSObject *gAwdlLogTarget;

static void awdlLog(const char *fmt, ...) __printflike(1, 2);
static void awdlLog(const char *fmt, ...)
{
	char buf[192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	rtlAirLog("%s", buf);
	if (!gAwdlLogLock)
		return;
	OSString *line = OSString::withCString(buf);
	if (!line)
		return;
	IOLockLock(gAwdlLogLock);
	IOService *t = (IOService *)gAwdlLogTarget;
	if (t) {
		OSArray *old = OSDynamicCast(OSArray, t->getProperty("RTLAwdlLog"));
		OSArray *arr = old ? OSArray::withArray(old, AWDL_LOG_MAX + 1) : OSArray::withCapacity(64);
		if (arr) {
			while (arr->getCount() >= AWDL_LOG_MAX)
				arr->removeObject(0);
			arr->setObject(line);
			t->setProperty("RTLAwdlLog", arr);
			arr->release();
		}
	}
	IOLockUnlock(gAwdlLogLock);
	line->release();
}

/* ---- IO80211 / Skywalk entry points that are not virtual (25.6.0 names) ---- */
extern "C" {
void rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
	__asm("__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm");
void rtl_sky_setId(void *iface, uint32_t id)
	__asm("__ZN23IO80211SkywalkInterface14setInterfaceIdEj");
void *rtl_sky_getWorkQueue(void *iface)
	__asm("__ZN23IO80211SkywalkInterface12getWorkQueueEv");
const char *rtl_sky_getBSDName(void *iface)
	__asm("__ZN23IO80211SkywalkInterface10getBSDNameEv");
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
bool rtl_vif_init(void *self, IO80211Controller *ctrl, ether_addr *mac, uint32_t role,
                  const char *name)
	__asm("__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc");
IOReturn rtl_txcomp_enqueue(void *q, void *const *pkts, uint32_t n, uint32_t opts)
	__asm("__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
void rtl_pool_free(void *pool, void *pkt)
	__asm("__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket");
bool rtl_vif_start(void *self, IOService *provider)
	__asm("__ZN23IO80211VirtualInterface5startEP9IOService");
IOReturn rtl_ctrl_setAwdlInterface(IO80211Controller *ctrl, void *awdl)
	__asm("__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface");
IOReturn rtl_eth_register(void *self, void *regInfo, void **queues, uint32_t n,
                          void *txPool, void *rxPool, uint32_t flags)
	__asm("__ZN26IOSkywalkEthernetInterface25registerEthernetInterfaceEPKNS_16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_j");
/* Copies the mbuf into a buffer of the controller's management frame pool and
 * hands it to the peer manager's actionFrameInput when dlt is 12; the mbuf
 * stays with the caller (Docs/AWDL_PEERMANAGER_DRIVER_ABI.md §1.5). */
void rtl_vif_bpfTapInput(void *self, mbuf_t m, uint32_t dlt, void *hdr, size_t hdrLen)
	__asm("__ZN23IO80211VirtualInterface11bpfTapInputEP6__mbufjPvm");
/* RX data packets, as the STA path builds them (Source/AirLayer/RTLAirLayer.cpp
 * rxFrames; Docs/AWDL_RX_DATA_TO_SKYWALK.md §4, §7). */
void *rtl_sky_getPeerManager(void *iface) __asm("__ZN23IO80211SkywalkInterface14getPeerManagerEv");
void rtl_pm_preQueueQuietMode(void *pm, const void *eth)
	__asm("__ZN22IO80211AWDLPeerManager28preQueuePacketCheckQuietModeEP12ether_header");
/* IO80211PeerManager's ivars at +0x18 (25G241): the unicast packet counts that
 * skywalkLogTxPacket and skywalkInputPacket keep. monitorAWDLDataMode reads them
 * to leave Low Power and widen the channel sequence; this path calls neither
 * function, so they would stay at 0 (Docs/AWDL_MODO_DATOS.md). */
#define AWDL_PM_IVARS         0x18
#define AWDL_PM_TX_UNICAST    1968
#define AWDL_PM_TX_BYTES      1988
#define AWDL_PM_RX_UNICAST    1948
#define AWDL_PM_RX_BYTES      1992
static inline void pmCount(void *pm, uint32_t countOff, uint32_t bytesOff, uint32_t bytes)
{
	uint8_t *iv = *(uint8_t **)((uint8_t *)pm + AWDL_PM_IVARS);
	OSIncrementAtomic((SInt32 *)(iv + countOff));
	OSAddAtomic((SInt32)bytes, (SInt32 *)(iv + bytesOff));
}
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
IOReturn rtl_rxcomp_enqueue(void *q, void *const *pkts, uint32_t n, uint32_t opts)
	__asm("__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
}

/* IOSkywalkPacketBufferPool::PoolOptions, same shape as the STA path
 * (Source/AirLayer/RTLAirLayer.cpp:196-204). */
struct rtl_awdl_pool_options {
	uint32_t packetCount;
	uint32_t bufferCount;
	uint32_t bufferSize;
	uint32_t maxBuffersPerPacket;
	uint32_t reserved0;
	uint32_t poolFlags;
	void    *dmaSpec;          /* NULL: no DMA, the radio copies */
	uint64_t reserved1;
};

/* IOSkywalkEthernetInterface::RegistrationInfo (0x130 bytes), values of
 * Docs/AWDL_STAGE1_IMPL_SPEC.md §5.3. */
#define AWDL_REGINFO_SIZE       0x130
#define AWDL_REGINFO_SUBFAMILY  0x0c    /* u32, 3 = IFNET_SUBFAMILY_WIFI     */
#define AWDL_REGINFO_ROLE       0x10    /* u32, 6 for the AWDL role          */
#define AWDL_REGINFO_NMEDIA     0x14    /* u32, 2 media words                */
#define AWDL_REGINFO_MEDIA      0x24    /* u32, 0x80 = IFM_IEEE80211         */
#define AWDL_REGINFO_MEDIA_CUR  0x28    /* u32, ifm_current                  */
#define AWDL_REGINFO_PREFIX     0x30    /* ptr to "awdl"                     */
#define AWDL_REGINFO_UNIT       0x38    /* u32, 0                            */
#define AWDL_REGINFO_HEADROOM   0x3c    /* u16, 64; +0x38 is the BSD unit    */
#define AWDL_REGINFO_FLAGS      0x40    /* u32, 0                            */
#define AWDL_REGINFO_MTU        0x48    /* u32 MTU and u32 max MTU (0x594)   */
#define AWDL_REGINFO_MAC        0x108   /* 6 bytes, the MAC of the request   */

#define AWDL_TX_RING            256     /* AWDL data frames waiting for a window */
#define AWDL_TX_AGG_FRAME_US    40      /* one MPDU's share of an A-MPDU's preamble, BlockAck and backoff */
#define AWDL_PUMP_RETRY_US      1000    /* the radio was full: the pump tries again this much later */
#define AWDL_TX_AIR_US          12000   /* data air time per AW tick, of the AW's 16.4 ms (rtlawdl_air) */
#define AWDL_DATA_GUARD_TU      3       /* no data this far into a slot */
#define AWDL_TX_FRAME_US        160     /* preamble, SIFS, ACK and backoff of one data frame */
#define AWDL_TX_LAST_SLACK_US   3000    /* rtlawdl_lastaw: data ends this long before the slot */
#define AWDL_TX_AGE_US          1200000 /* past one 16-slot period a queued data frame is dropped */
#define AWDL_IFACE_ID           2       /* Broadcom: interface id 2 for AWDL */
#define AWDL_ROLE               6       /* IO80211VirtualInterfaceRole AirLink */
#define AWDL_LINK_DOWN          1       /* linkState() values the PeerManager tests */
#define AWDL_LINK_UP            2

OSDefineMetaClassAndStructors(RTLAwdInterface, IO80211AWDLProtocol)

bool RTLAwdInterface::initWithController(RTLAirController *ctrl, const uint8_t mac[6])
{
	_ctrl = ctrl;
	_regInfo = nullptr;
	_txPool = _rxPool = nullptr;
	_txSubQ = _txCompQ = _rxCompQ = nullptr;
	_bsdName[0] = '\0';
	memcpy(_mac, mac, 6);
	if (!init()) {
		LOG("awdl: init() failed");
		return false;
	}
	/* One awdl0 at a time; the lock lives as long as the kext. */
	if (!gAwdlLogLock)
		gAwdlLogLock = IOLockAlloc();
	if (gAwdlLogLock) {
		IOLockLock(gAwdlLogLock);
		gAwdlLogTarget = this;
		IOLockUnlock(gAwdlLogLock);
	}
	uint32_t rx = 1;
	PE_parse_boot_argn("rtlawdl_rx", &rx, sizeof(rx));
	_rxAllowed = rx != 0;
	_rxLock = IOLockAlloc();
	_rxDrain = thread_call_allocate(&RTLAwdInterface::rxDrainEntry, this);
	uint32_t ba = 1;
	PE_parse_boot_argn("rtlawdl_ba", &ba, sizeof(ba));
	_baAllowed = ba != 0;
	_baLock = IOLockAlloc();
	if (!_rxLock || !_rxDrain || !_baLock) {
		LOG("awdl: rx alloc failed");
		return false;
	}
	uint32_t tx = 0;
	PE_parse_boot_argn("rtlawdl_tx", &tx, sizeof(tx));
	_txAllowed = tx != 0;
	uint32_t park = 149;
	PE_parse_boot_argn("rtlawdl_ch", &park, sizeof(park));
	_parkCh = park == 6 || park == 44 || park == 149 ? (uint8_t)park : 0;
	_mccMode = 0;
	PE_parse_boot_argn("rtlawdl_mcc", &_mccMode, sizeof(_mccMode));
	_mccAw = 0;
	if (PE_parse_boot_argn("rtlawdl_mccaw", &_mccAw, sizeof(_mccAw)) &&
	    (_mccAw < 1 || _mccAw > 4))     /* AWDL_PM: the whole slot */
		_mccAw = 4;
	_mccDwell = _mccAw ? _mccAw : 1;
	_mccRun = 3;
	PE_parse_boot_argn("rtlawdl_mccrun", &_mccRun, sizeof(_mccRun));
	if (_mccRun > AWDL_SHARE_SLOTS - 1)
		_mccRun = AWDL_SHARE_SLOTS - 1;
	uint32_t dense = 1;
	PE_parse_boot_argn("rtlawdl_dense", &dense, sizeof(dense));
	_denseAllowed = dense != 0;
	uint32_t air = AWDL_TX_AIR_US, lastAw = 0;
	PE_parse_boot_argn("rtlawdl_air", &air, sizeof(air));
	PE_parse_boot_argn("rtlawdl_lastaw", &lastAw, sizeof(lastAw));
	_txAirUs = air < 4000 ? 4000 : air > 15000 ? 15000 : air;
	_txLastAw = lastAw != 0;
	if (_txAirUs != AWDL_TX_AIR_US || _txLastAw)
		LOG("awdl: data air %u us per AW%s", _txAirUs, _txLastAw ? ", also in the slot's last AW" : "");
	uint32_t rate = 1, rateMax = 5;
	PE_parse_boot_argn("rtlawdl_rate", &rate, sizeof(rate));
	PE_parse_boot_argn("rtlawdl_ratemax", &rateMax, sizeof(rateMax));
	_rateOff = rate == 0;
	_rateMax = (uint8_t)(rateMax < 1 ? 1 : rateMax > 6 ? 6 : rateMax);
	_rateIdx = 1;
	_mccLock = IOLockAlloc();
	_txLock = IOLockAlloc();
	_txTick = thread_call_allocate(&RTLAwdInterface::txTickEntry, this);
	_txPump = thread_call_allocate(&RTLAwdInterface::pumpEntry, this);
	_txRing = (AwdlTxFrame *)IOMallocZero(sizeof(AwdlTxFrame) * AWDL_TX_RING);
	uint32_t pump = 1, agg = 1, aggWd = 1, aggMax = 31, aggDen = 6, aggBw = 80;
	PE_parse_boot_argn("rtlawdl_pump", &pump, sizeof(pump));
	PE_parse_boot_argn("rtlawdl_agg", &agg, sizeof(agg));
	PE_parse_boot_argn("rtlawdl_aggwd", &aggWd, sizeof(aggWd));
	PE_parse_boot_argn("rtlawdl_aggmax", &aggMax, sizeof(aggMax));
	PE_parse_boot_argn("rtlawdl_aggden", &aggDen, sizeof(aggDen));
	PE_parse_boot_argn("rtlawdl_aggbw", &aggBw, sizeof(aggBw));
	_pumpAllowed = pump != 0;
	_aggAllowed = agg != 0;
	bzero(&_bao, sizeof(_bao));
	_bao.watchOff = aggWd == 0;
	_aggMax = aggMax < 1 ? 1 : aggMax > 31 ? 31 : aggMax;
	_aggDensity = aggDen > 7 ? 7 : aggDen;
	_aggBwMax = aggBw >= 80 ? RTL_RADIO_BW_80 : aggBw >= 40 ? RTL_RADIO_BW_40 : RTL_RADIO_BW_20;
	if (!_pumpAllowed || !_aggAllowed || _bao.watchOff)
		LOG("awdl: data pump %s, A-MPDU %s%s", _pumpAllowed ? "on" : "off", _aggAllowed ? "on" : "off",
		    _bao.watchOff ? ", sessions never closed for silence" : "");
	if (!_txLock || !_txTick || !_txPump || !_txRing || !_mccLock) {
		LOG("awdl: tx alloc failed");
		return false;
	}
	/* Sets IO80211VirtualInterfaceRole ("AirLink" for role 6) through
	 * IO80211SkywalkInterface::setInterfaceRole, before any registration. Its
	 * result is not tested: the Proximity path does not branch on it, and the
	 * failures that matter surface in attach() and in the base start(). */
	ether_addr ea;
	memcpy(ea.octet, mac, 6);
	if (!rtl_vif_init(this, ctrl, &ea, AWDL_ROLE, "awdl")) {
		LOG("awdl: base init failed");
		return false;
	}
	rtl_sky_setId(this, AWDL_IFACE_ID);
	if (!attach(ctrl)) {
		LOG("awdl: attach failed");
		return false;
	}
	return true;
}

/*
 * One TX submission, one TX completion and one RX completion queue over one
 * pool per direction, the shape the STA path already registers. No action is
 * attached: stage 1 does not move packets. Each queue joins the interface work
 * queue, the same queue IO80211VirtualInterface::start installs.
 */
/* The completion queues run this from checkForWork when work is pending; a null
 * action would be called unchecked. Nothing is ever pending in stage 1, so it
 * reports zero packets completed. */
static unsigned int rtlAwdIdleAction(OSObject *, void *, void **, unsigned int, void *)
{
	return 0;
}

bool RTLAwdInterface::createDatapath()
{
	rtl_awdl_pool_options o;
	bzero(&o, sizeof(o));
	o.packetCount = 256;
	o.bufferCount = 256;
	o.bufferSize = 2048;
	o.maxBuffersPerPacket = 1;
	o.poolFlags = 1;
	_txPool = rtl_pool_withName("rtlawdl:tx", this, 1, &o);
	_rxPool = rtl_pool_withName("rtlawdl:rx", this, 1, &o);
	if (!_txPool || !_rxPool) {
		LOG("awdl: packet pool alloc failed");
		return false;
	}
	_txSubQ = rtl_txsub_withPool(_txPool, 128, 0, this,
	                             (void *)&RTLAwdInterface::txData, nullptr, 0);
	_txCompQ = rtl_txcomp_withPool(_txPool, 128, 0, this, (void *)&rtlAwdIdleAction, nullptr, 0);
	_rxCompQ = rtl_rxcomp_withPool(_rxPool, 128, 0, this, (void *)&rtlAwdIdleAction, nullptr, 0);
	if (!_txSubQ || !_txCompQ || !_rxCompQ) {
		LOG("awdl: packet queue alloc failed");
		return false;
	}
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (!wq) {
		LOG("awdl: no work queue");
		return false;
	}
	wq->addEventSource((IOEventSource *)_txSubQ);
	wq->addEventSource((IOEventSource *)_txCompQ);
	wq->addEventSource((IOEventSource *)_rxCompQ);
	return true;
}

bool RTLAwdInterface::startWithRegistration(RTLAirController *ctrl)
{
	if (!rtl_vif_start(this, ctrl)) {
		LOG("awdl: base start failed");
		return false;
	}
	_regInfo = (uint8_t *)IOMalloc(AWDL_REGINFO_SIZE);
	if (!_regInfo) {
		LOG("awdl: reginfo alloc failed");
		return false;
	}
	bzero(_regInfo, AWDL_REGINFO_SIZE);
	/* Validates version 1 / size 0x130 and fills the defaults; the Proximity path
	 * overwrites them without looking at the result either. */
	rtl_sky_initRegInfo(this, _regInfo, 1, AWDL_REGINFO_SIZE);
	*(uint32_t *)(_regInfo + AWDL_REGINFO_SUBFAMILY) = 3;
	*(uint32_t *)(_regInfo + AWDL_REGINFO_ROLE) = AWDL_ROLE;
	*(uint32_t *)(_regInfo + AWDL_REGINFO_NMEDIA) = 2;
	*(uint32_t *)(_regInfo + AWDL_REGINFO_MEDIA) = 0x80;
	*(uint32_t *)(_regInfo + AWDL_REGINFO_MEDIA_CUR) = 0x80;
	*(const char **)(_regInfo + AWDL_REGINFO_PREFIX) = "awdl";
	*(uint32_t *)(_regInfo + AWDL_REGINFO_UNIT) = 0;
	*(uint16_t *)(_regInfo + AWDL_REGINFO_HEADROOM) = 64;
	*(uint32_t *)(_regInfo + AWDL_REGINFO_FLAGS) = 0;
	*(uint64_t *)(_regInfo + AWDL_REGINFO_MTU) = 0x59400000594ULL;
	memcpy(_regInfo + AWDL_REGINFO_MAC, _mac, 6);

	if (!createDatapath()) {
		LOG("awdl: datapath failed");
		return false;
	}
	void *queues[3] = { _txSubQ, _txCompQ, _rxCompQ };
	IOReturn r = rtl_eth_register(this, _regInfo, queues, 3, _txPool, _rxPool, 0);
	LOG("awdl: registerEthernetInterface 0x%x", r);
	if (r != kIOReturnSuccess) {
		teardownDatapath();
		return false;
	}

	/* Synchronous: the BSD client of the family composes the name, publishes
	 * it and creates the ifnet before this returns. */
	registerService(kIOServiceSynchronous);
	setRunningState(false);
	/* Same source the family's own handler reads for payload+0x10. */
	const char *name = rtl_sky_getBSDName(this);
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		teardownDatapath();
		return false;
	}
	strlcpy(_bsdName, name, sizeof(_bsdName));
	LOG("awdl: born %s", _bsdName);
	return true;
}

/* As AppleBCMWLANSkywalkInterface::stop: on the work queue, disable each queue,
 * take it off the work queue and drop it. */
IOReturn RTLAwdInterface::removeQueues(OSObject *owner, void *, void *, void *, void *)
{
	RTLAwdInterface *me = (RTLAwdInterface *)owner;
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
 * TX submission action, on awdl0's work queue: each Ethernet II frame (at
 * buffer base + data offset, as the STA path reads it) is turned into an AWDL
 * data frame in the TX ring (queueData), then every packet goes back through
 * the TX completion queue, which completes it under its gate and returns it
 * to the pool. legacyDequeue advances its ring by the returned count, so
 * returning less would stall the queue: the whole batch is consumed.
 */
IOReturn RTLAwdInterface::txData(OSObject *owner, void *, void *const *pkts,
                                 uint32_t count, void *)
{
	RTLAwdInterface *me = OSDynamicCast(RTLAwdInterface, owner);
	if (!me || !count || !me->_txCompQ)
		return 0;
	for (uint32_t i = 0; i < count; i++) {
		void *pkt = pkts[i];
		if (!rtl_pkt_bufcount(pkt))
			continue;
		const uint8_t *eth = (const uint8_t *)(uintptr_t)(rtl_pkt_base(pkt) + rtl_pkt_off(pkt));
		uint32_t len = rtl_pkt_len(pkt);
		/* As Broadcom's role 6 dequeue through logSkywalkTxReqPacket: a
		 * queued packet takes the manager out of quiet mode (and unicast
		 * into data mode 3), Docs/AWDL_TX_HOOKS_POR_PAQUETE.md §2. Same
		 * work-queue context as Broadcom's call. */
		void *pm = me->_txOn && len >= 14 ? rtl_sky_getPeerManager(me) : nullptr;
		if (pm)
			rtl_pm_preQueueQuietMode(pm, eth);
		if (pm && !(eth[0] & 1))
			pmCount(pm, AWDL_PM_TX_UNICAST, AWDL_PM_TX_BYTES, len);
		me->queueData(eth, len);
	}
	/* enqueuePackets requires count < queue capacity (128). */
	for (uint32_t done = 0; done < count; done += 64) {
		uint32_t n = count - done > 64 ? 64 : count - done;
		if (rtl_txcomp_enqueue(me->_txCompQ, pkts + done, n, 0) != kIOReturnSuccess)
			for (uint32_t i = 0; i < n; i++)
				rtl_pool_free(me->_txPool, pkts[done + i]);
	}
	return (IOReturn)count;
}

/*
 * The peer manager's multicast stash, from handleMulticastTimer (25G241
 * 0xffffff80021c38c4, the branch without card capability p[21] bit 6): the
 * return value is the number of packets taken, and it frees the rest with
 * freeMulticastPacket. AppleBCMWLANPCIeSkywalkTxSubmissionQueue::p2pMulticastTx
 * runs them through its TX submission action; txData is ours, so every packet
 * is taken and completed there.
 */
uintptr_t RTLAwdInterface::p2pMulticastTx(IO80211NetworkPacket **pkts, unsigned int count)
{
	if (!pkts || !count)
		return 0;
	if (_txMcast++ == 0)
		LOG("awdl: first multicast batch from the peer manager, %u packets", count);
	return (uintptr_t)txData(this, nullptr, (void *const *)pkts, count, nullptr);
}

/* As the Broadcom PCIe queue: the packet goes back to the pool it came from. */
uintptr_t RTLAwdInterface::freeMulticastPacket(IO80211NetworkPacket *pkt)
{
	if (pkt && _txPool)
		rtl_pool_free(_txPool, pkt);
	return 1;
}

void RTLAwdInterface::teardownDatapath()
{
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (wq)
		wq->runAction(&RTLAwdInterface::removeQueues, this);
	else
		removeQueues(this, nullptr, nullptr, nullptr, nullptr);
}

void RTLAwdInterface::stop(IOService *provider)
{
	setRadioTx(false);
	setRadioRx(false);
	/* The IOKit termination thread, no gate held
	 * (Docs/AWDL_PEERMANAGER_TEARDOWN.md, step 6): wait for a tick or a drain
	 * still running, so neither posts events nor enqueues on _rxCompQ once
	 * the queues are gone. A cancelled pending call drops its reference. */
	if (_txTick && thread_call_cancel_wait(_txTick))
		release();
	if (_txPump && thread_call_cancel_wait(_txPump))
		release();
	if (_rxDrain && thread_call_cancel_wait(_rxDrain))
		release();
	teardownDatapath();
	IO80211VirtualInterface::stop(provider);
}

void RTLAwdInterface::free()
{
	if (gAwdlLogLock) {
		IOLockLock(gAwdlLogLock);
		if (gAwdlLogTarget == this)
			gAwdlLogTarget = nullptr;
		IOLockUnlock(gAwdlLogLock);
	}
	/* A pending drain holds a reference, so none is queued or running here. */
	if (_rxDrain) {
		thread_call_free(_rxDrain);
		_rxDrain = nullptr;
	}
	flushRadioRx();
	baFlushAll(false);
	if (_baLock) {
		IOLockFree(_baLock);
		_baLock = nullptr;
	}
	if (_rxLock) {
		IOLockFree(_rxLock);
		_rxLock = nullptr;
	}
	/* A pending or running tick holds a reference, so none is here. */
	if (_txTick) {
		thread_call_free(_txTick);
		_txTick = nullptr;
	}
	if (_txPump) {
		thread_call_free(_txPump);
		_txPump = nullptr;
	}
	if (_txLock) {
		IOLockFree(_txLock);
		_txLock = nullptr;
	}
	if (_mccLock) {
		IOLockFree(_mccLock);
		_mccLock = nullptr;
	}
	if (_txRing) {
		IOFree(_txRing, sizeof(AwdlTxFrame) * AWDL_TX_RING);
		_txRing = nullptr;
	}
	OSSafeReleaseNULL(*(OSObject **)&_txSubQ);
	OSSafeReleaseNULL(*(OSObject **)&_txCompQ);
	OSSafeReleaseNULL(*(OSObject **)&_rxCompQ);
	OSSafeReleaseNULL(*(OSObject **)&_txPool);
	OSSafeReleaseNULL(*(OSObject **)&_rxPool);
	if (_regInfo) {
		IOFree(_regInfo, AWDL_REGINFO_SIZE);
		_regInfo = nullptr;
	}
	IO80211VirtualInterface::free();
}

/* The family brings the interface up and down through these. The queues are
 * born disabled (IOSkywalkPacketQueue::initWithPool clears IOEventSource::
 * enabled) and packetSubmission refuses a disabled queue, so enable() turns
 * them on, as the Broadcom enableDatapath does, and disable() turns them off. */
static void rtlAwdSetQueues(void *const qs[3], bool on)
{
	for (int i = 0; i < 3; i++) {
		IOEventSource *q = (IOEventSource *)qs[i];
		if (!q)
			continue;
		if (on)
			q->enable();
		else
			q->disable();
	}
}

uintptr_t RTLAwdInterface::enable(unsigned int opts)
{
	LOG("awdl: enable 0x%x", opts);
	void *qs[3] = { _txCompQ, _rxCompQ, _txSubQ };
	rtlAwdSetQueues(qs, true);
	/* linkState() reads 1 or 0 until this: interfaceStateChange stops at "link
	 * is down" and updateLocalRequests answers 6. Going to 2 also makes the
	 * family call IO80211PeerManager::enable, which runs configure(). */
	setLinkState((IO80211LinkState)AWDL_LINK_UP, 1);
	setRadioRx(true);
	setRadioTx(true);
	return kIOReturnSuccess;
}

uintptr_t RTLAwdInterface::disable(unsigned int opts)
{
	LOG("awdl: disable 0x%x", opts);
	setRadioTx(false);
	setRadioRx(false);
	setLinkState((IO80211LinkState)AWDL_LINK_DOWN, 1);
	void *qs[3] = { _txSubQ, _rxCompQ, _txCompQ };
	rtlAwdSetQueues(qs, false);
	return kIOReturnSuccess;
}

/*
 * RX of AWDL frames. The radio passes every frame whose A3 is the AWDL BSSID
 * (Docs/AWDL_RTL8814_RX_ACK.md §1, §4) to radioRxHook on its RX thread, with
 * its hook lock held: the hook only copies the frame behind a 4-byte prefix
 * (RSSI, channel) and queues it. rxDrainEntry hands each AWDL action frame to
 * IO80211VirtualInterface::bpfTapInput on the work queue, the gate Broadcom's
 * event handlers run in; bpfTapInput passes it to the peer manager's
 * actionFrameInput (Docs/AWDL_PEERMANAGER_DRIVER_ABI.md §1.5). Data frames
 * become Ethernet II packets on awdl0's RX completion queue (deliverData).
 * Boot-arg rtlawdl_rx=0 leaves the hook detached.
 */
#define AWDL_RX_META          12      /* rssi (s8), channel, 2 spare, u64 arrival (us) */
#define AWDL_RX_META_TIME     4

static uint64_t awdlNowUs();
#define AWDL_RX_QUEUE_MAX     64
#define AWDL_RX_FRAME_MAX     2048    /* management frame pool buffer size */
#define AWDL_WLAN_HDR         24
#define AWDL_DLT_IEEE802_11   12
#define AWDL_AF_HDR_LEN       0x48    /* apple80211_act_frm_hdr; more traps */
#define AWDL_AF_HDR_CHANNEL   0x0c    /* apple80211_channel: version, channel, flags */
#define AWDL_AF_HDR_RSSI      0x2c    /* s32 dBm, compared with the edge threshold */
#define AWDL_RX_LOG_FIRST     16

void RTLAwdInterface::setRadioRx(bool on)
{
	if (on && !_rxAllowed)
		return;
	if (on == _rxHooked || !_ctrl)
		return;
	/* The radio takes its hook lock to swap the hook, so after a detach
	 * returns radioRxHook is not running and will not run again. */
	IOReturn r = _ctrl->radio(RTL_RADIO_FN_AWDL_HOOK,
	                          on ? (void *)&RTLAwdInterface::radioRxHook : nullptr,
	                          on ? this : nullptr);
	if (on && r != kIOReturnSuccess) {
		LOG("awdl: rx hook refused 0x%x", r);
		return;
	}
	if (_rxLock) {
		IOLockLock(_rxLock);
		_rxHooked = on;
		IOLockUnlock(_rxLock);
	}
	if (!on) {
		flushRadioRx();
		baFlushAll(false);
	}
	LOG("awdl: rx %s, af %llu data %llu (up %llu bad %llu not us %llu) other %llu dropped %llu",
	    on ? "attached" : "detached", _rxAf, _rxData, _rxDataUp, _rxDataBad, _rxDataNotUs,
	    _rxOther, _rxDropped);
}

/* The old radio is terminating: unhook it but remember what was on. */
void RTLAwdInterface::radioGone()
{
	if (_rxHooked)
		_ctrl->radio(RTL_RADIO_FN_AWDL_HOOK, nullptr, nullptr);
	if (_mccLock) {
		IOLockLock(_mccLock);
		_mccCur = 0;            /* a new radio starts on its own channel */
		_mccOff = false;
		IOLockUnlock(_mccLock);
	}
	LOG("awdl: radio leaving, rx %u port1 %u", _rxHooked, _port1On);
}

void RTLAwdInterface::radioBack()
{
	if (_rxHooked) {
		IOReturn r = _ctrl->radio(RTL_RADIO_FN_AWDL_HOOK, (void *)&RTLAwdInterface::radioRxHook, this);
		if (r != kIOReturnSuccess)
			LOG("awdl: rx hook refused by the new radio 0x%x", r);
	}
	if (_port1On) {
		IOReturn r = _ctrl->radio(RTL_RADIO_FN_AWDL_PORT, _mac);
		_port1On = r == kIOReturnSuccess;
		if (!_port1On)
			LOG("awdl: port 1 refused by the new radio 0x%x", r);
	}
	_rateSet = false;           /* a new radio starts at its fixed rate */
	/* and without an A-MPDU rate: the sessions start over, since frames sent
	 * through it before the rate is set again would get the radio's numbers */
	_aggSet = false;
	if (_txLock) {
		IOLockLock(_txLock);
		bzero(_bao.peer, sizeof(_bao.peer));
		IOLockUnlock(_txLock);
	}
	LOG("awdl: radio back, rx %u port1 %u", _rxHooked, _port1On);
}

void RTLAwdInterface::flushRadioRx()
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

void RTLAwdInterface::radioRxHook(void *ctx, const uint8_t *frame, uint32_t len,
                                  int8_t rssi, uint8_t channel)
{
	RTLAwdInterface *me = (RTLAwdInterface *)ctx;
	if (len < AWDL_WLAN_HDR || len > AWDL_RX_FRAME_MAX) {
		me->_rxDropped++;
		return;
	}
	mbuf_t m;
	if (mbuf_allocpacket(MBUF_DONTWAIT, AWDL_RX_META + len, nullptr, &m) != 0) {
		me->_rxDropped++;
		return;
	}
	/* Arrival time on the radio's RX thread: alignClock works from it, not
	 * from when the thread call drains the queue. */
	uint8_t meta[AWDL_RX_META] = { (uint8_t)rssi, channel, 0, 0 };
	uint64_t rxUs = awdlNowUs();
	memcpy(meta + AWDL_RX_META_TIME, &rxUs, sizeof(rxUs));
	if (mbuf_copyback(m, 0, AWDL_RX_META, meta, MBUF_DONTWAIT) != 0 ||
	    mbuf_copyback(m, AWDL_RX_META, len, frame, MBUF_DONTWAIT) != 0) {
		mbuf_freem(m);
		me->_rxDropped++;
		return;
	}
	bool kick = false;
	IOLockLock(me->_rxLock);
	if (me->_rxHooked && me->_rxCount < AWDL_RX_QUEUE_MAX) {
		mbuf_setnextpkt(m, nullptr);
		if (me->_rxTail)
			mbuf_setnextpkt(me->_rxTail, m);
		else
			me->_rxHead = m;
		me->_rxTail = m;
		kick = me->_rxCount++ == 0;
		m = nullptr;
	}
	IOLockUnlock(me->_rxLock);
	if (m) {
		mbuf_freem(m);
		me->_rxDropped++;
	} else if (kick) {
		/* The pending call owns a reference until rxDrainEntry ends. */
		me->retain();
		if (thread_call_enter(me->_rxDrain))
			me->release();
	}
}

void RTLAwdInterface::rxDrainEntry(thread_call_param_t p0, thread_call_param_t)
{
	RTLAwdInterface *me = (RTLAwdInterface *)p0;
	IOLockLock(me->_rxLock);
	mbuf_t chain = me->_rxHead;
	me->_rxHead = me->_rxTail = nullptr;
	me->_rxCount = 0;
	IOLockUnlock(me->_rxLock);
	while (chain) {
		mbuf_t m = chain;
		chain = mbuf_nextpkt(m);
		mbuf_setnextpkt(m, nullptr);
		if (me->_rxHooked)
			me->injectFrame(m);
		else
			mbuf_freem(m);
	}
	/* Gaps that waited too long, also when no frame came (txTick kicks). */
	me->baExpire(awdlNowUs());
	me->release();
}

IOReturn RTLAwdInterface::bpfTapAction(OSObject *owner, void *m, void *hdr, void *, void *)
{
	rtl_vif_bpfTapInput(owner, (mbuf_t)m, AWDL_DLT_IEEE802_11, hdr, AWDL_AF_HDR_LEN);
	return kIOReturnSuccess;
}

void RTLAwdInterface::injectFrame(mbuf_t m)
{
	uint8_t h[AWDL_RX_META + AWDL_WLAN_HDR + 5];
	size_t len = mbuf_pkthdr_len(m);
	if (len < AWDL_RX_META + AWDL_WLAN_HDR ||
	    mbuf_copydata(m, 0, len < sizeof(h) ? len : sizeof(h), h) != 0) {
		_rxDropped++;
		mbuf_freem(m);
		return;
	}
	int8_t rssi = (int8_t)h[0];
	uint8_t channel = h[1];
	const uint8_t *f = h + AWDL_RX_META;
	const uint8_t *body = f + AWDL_WLAN_HDR;
	uint8_t type = (f[0] >> 2) & 3, subtype = f[0] >> 4;
	uint64_t seen = _rxAf + _rxData + _rxOther;

	/* Management action (0/13), category 127, OUI 00:17:F2, type 8: the bytes
	 * isValidAF and actionFrameHasCorrectOUI check (ABI §1.6). */
	bool af = type == 0 && subtype == 13 && len >= sizeof(h) && body[0] == 0x7F &&
	          body[1] == 0x00 && body[2] == 0x17 && body[3] == 0xF2 && body[4] == 0x08;
	if (seen < AWDL_RX_LOG_FIRST)
		LOG("awdl: rx %s %02x:%02x:%02x:%02x:%02x:%02x rssi %d ch %u len %u",
		    af ? "af" : type == 2 ? "data" : "other", f[10], f[11], f[12], f[13], f[14],
		    f[15], rssi, channel, (unsigned)(len - AWDL_RX_META));
	if (!af) {
		if (type == 2) {
			_rxData++;
			if (_aggAllowed && len >= AWDL_RX_META + AWDL_WLAN_HDR) {
				IOLockLock(_txLock);
				awdlBaoHeard(&_bao, f + 10);
				IOLockUnlock(_txLock);
			}
			rxReorder(m);
			return;
		}
		/* Block Ack action frames (category 3): ADDBA request, DELBA. */
		if (type == 0 && subtype == 13 && len >= AWDL_RX_META + AWDL_WLAN_HDR + 2 &&
		    body[0] == 3) {
			_rxOther++;
			handleBlockAck(m);
			return;
		}
		_rxOther++;
		mbuf_freem(m);
		return;
	}
	_rxAf++;
	baExpire(awdlNowUs());
	uint64_t rxUs;
	memcpy(&rxUs, h + AWDL_RX_META_TIME, sizeof(rxUs));
	uint64_t lag = awdlNowUs() - rxUs;
	if (lag > _rxLagMax)
		_rxLagMax = lag;
	if ((_rxAf & 0xFF) == 0) {
		LOG("awdl: rx af %llu data %llu (up %llu bad %llu wrapped %llu) other %llu dropped %llu, "
		    "drain lag max %llu us", _rxAf, _rxData, _rxDataUp, _rxDataBad, _rxDataWrapped,
		    _rxOther, _rxDropped, _rxLagMax);
		if (_baReq)
			LOG("awdl: ba req %llu accept %llu decline %llu delba %llu, total held %llu "
			    "gaps skipped %llu old %llu dup %llu%s", _baReq, _baAccept, _baDecline, _baDelba, _baHeld,
			    _baTimeout, _baOld, _baDup, _baRefuse ? " (refusing)" : "");
		_rxLagMax = 0;
	}

	if (_txOn) {
		alignClock(m, f + 10);
		notePeerSeq(m, f + 10);
	}
	if (memcmp(f + 10, _mac, 6)) {
		uint64_t at = awdlNowUs();
		IOLockLock(_txLock);
		_mccPeerUs = at;
		/* the signal rateByRssi goes by; starts over after 5 s of silence */
		int32_t r16 = (int32_t)rssi * 16;
		_peerRssi16 = _peerRssiUs && at - _peerRssiUs < 5000000 ? _peerRssi16 + (r16 - _peerRssi16) / 8
		                                                        : r16;
		_peerRssiUs = at;
		IOLockUnlock(_txLock);
	}

	/* Source MAC, channel and RSSI: the manager reads +0x00 and +0x2c, and
	 * IO80211Peer::reportRssi keeps the peer's 5 GHz or 2.4 GHz RSSI average
	 * only when the channel flags carry the band bit (0x10 or 0x08); that
	 * average decides isPeerRoutable. wifip2pd maps the band from the same
	 * flags. Broadcom fills them from ChanSpecConvToApple80211Channel: band,
	 * 20 MHz (0x02) and DFS (0x100) for channels 52 to 144. */
	uint8_t hdr[AWDL_AF_HDR_LEN];
	bzero(hdr, sizeof(hdr));
	memcpy(hdr, f + 10, 6);
	uint32_t cflags = (channel > 14 ? 0x10u : 0x08u) | 0x02u;
	if (channel >= 52 && channel <= 144)
		cflags |= 0x100u;
	*(uint32_t *)(hdr + AWDL_AF_HDR_CHANNEL) = 1;
	*(uint32_t *)(hdr + AWDL_AF_HDR_CHANNEL + 4) = channel;
	*(uint32_t *)(hdr + AWDL_AF_HDR_CHANNEL + 8) = cflags;
	*(int32_t *)(hdr + AWDL_AF_HDR_RSSI) = rssi;

	/* The buffer starts at the category byte. */
	mbuf_adj(m, AWDL_RX_META + AWDL_WLAN_HDR);
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (wq)
		wq->runAction(&RTLAwdInterface::bpfTapAction, this, m, hdr);
	mbuf_freem(m);
}

/*
 * One AWDL data frame (Docs/AWDL_RX_DATA_TO_SKYWALK.md §2, §8): 3-address
 * data or QoS data on the AWDL BSSID, unprotected and unfragmented; each MSDU
 * (one, or the A-MSDU subframes) is Apple SNAP AA AA 03 00 17 F2 08 00, the
 * AWDL head 03 04, a sequence number, 00 00 and the real ethertype. Each one
 * goes up as [DA][SA][ethertype][payload] for us or a group address. The
 * peer filter of IO80211VirtualInterface::inputPacket (§6) is not applied.
 * Runs on the RX drain thread call; consumes m.
 */
#define AWDL_DATA_HDR         16      /* SNAP 8 + 03 04 + seq + 00 00 + ethertype */
#define AWDL_MSDU_MAX         16      /* A-MSDU subframes per frame (AWDL-14) */

void RTLAwdInterface::deliverData(mbuf_t m)
{
	static const uint8_t kSnap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x17, 0xF2, 0x08, 0x00 };
	static const uint8_t kRfc1042[6] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00 };
	uint8_t f[AWDL_RX_FRAME_MAX];
	size_t len = mbuf_pkthdr_len(m);
	if (len <= AWDL_RX_META || len - AWDL_RX_META > sizeof(f) ||
	    mbuf_copydata(m, AWDL_RX_META, len - AWDL_RX_META, f) != 0) {
		mbuf_freem(m);
		_rxDataBad++;
		return;
	}
	mbuf_freem(m);
	len -= AWDL_RX_META;

	/* Frame control: data (0x08) or QoS data (0x88), no To/From DS, not
	 * protected, no more fragments; fragment number 0. */
	bool qos = f[0] == 0x88;
	size_t hdr = AWDL_WLAN_HDR + (qos ? 2 : 0);
	if ((f[0] != 0x08 && !qos) || (f[1] & 0x47) || (f[22] & 0x0F) || len < hdr) {
		_rxDataBad++;
		return;
	}
	bool amsdu = qos && (f[AWDL_WLAN_HDR] & 0x80);

	struct { const uint8_t *da, *sa, *msdu; size_t len; } sub[AWDL_MSDU_MAX];
	uint32_t ns = 0;
	if (!amsdu) {
		sub[0] = { f + 4, f + 10, f + hdr, len - hdr };
		ns = 1;
	} else {
		size_t off = hdr;
		while (off + 14 <= len && ns < AWDL_MSDU_MAX) {
			size_t sl = ((size_t)f[off + 12] << 8) | f[off + 13];
			if (off + 14 + sl > len)
				break;
			sub[ns++] = { f + off, f + off + 6, f + off + 14, sl };
			/* padding to a multiple of 4 of the subframe (owly frame.rs:246) */
			off += 14 + sl + (4 - (14 + sl) % 4) % 4;
		}
	}

	void *pkts[AWDL_MSDU_MAX];
	uint32_t n = 0;
	for (uint32_t i = 0; i < ns; i++) {
		const uint8_t *s = sub[i].msdu;
		size_t sl = sub[i].len;
		/* iOS 27 wraps its larger frames (MCS 0/1 QoS data, A-MSDU bit clear)
		 * in an RFC 1042 header whose type field is the length of the AWDL
		 * MSDU that follows: AA AA 03 00 00 00 LL LL. Measured 2026-11-08 over
		 * the air: 620 of 1370 iPhone data frames to the Mac, none up before. */
		if (sl >= 8 + AWDL_DATA_HDR && !memcmp(s, kRfc1042, sizeof(kRfc1042))) {
			size_t wl = ((size_t)s[6] << 8) | s[7];
			if (wl >= AWDL_DATA_HDR && wl <= sl - 8) {
				s += 8;
				sl = wl;
				_rxDataWrapped++;
			}
		}
		if (sl < AWDL_DATA_HDR || memcmp(s, kSnap, sizeof(kSnap)) || s[8] != 0x03 ||
		    s[9] != 0x04) {
			_rxDataBad++;
			continue;
		}
		if (!(sub[i].da[0] & 1) && memcmp(sub[i].da, _mac, 6)) {
			_rxDataNotUs++;
			continue;
		}
		size_t plen = sl - AWDL_DATA_HDR;
		size_t elen = 14 + plen;
		void *pkt = nullptr;
		if (!_rxPool || rtl_pool_alloc(_rxPool, &pkt, 1) != kIOReturnSuccess || !pkt) {
			_rxDropped++;
			continue;
		}
		/* prepare before touching the buffer, no source queue (the STA
		 * path's 0.15 and 0.16 panics, RTLAirLayer.cpp rxFrames). */
		if (rtl_pkt_prepare(pkt, nullptr, 0, 0) != kIOReturnSuccess || !rtl_pkt_bufcount(pkt) ||
		    elen > rtl_pkt_bufsize(pkt)) {
			rtl_pool_free(_rxPool, pkt);
			_rxDropped++;
			continue;
		}
		uint8_t *e = (uint8_t *)(uintptr_t)rtl_pkt_base(pkt);
		memcpy(e, sub[i].da, 6);
		memcpy(e + 6, sub[i].sa, 6);
		e[12] = s[14];
		e[13] = s[15];
		memcpy(e + 14, s + AWDL_DATA_HDR, plen);
		if (rtl_pkt_setOffLen(pkt, 0, (uint32_t)elen) != kIOReturnSuccess) {
			rtl_pool_free(_rxPool, pkt);
			_rxDropped++;
			continue;
		}
		rtl_netpkt_setLinkHdrLen(pkt, 14);
		pkts[n++] = pkt;
	}
	if (!n)
		return;
	/* n <= 16, below the queue's capacity of 128 (enqueuePackets wants n < it). */
	IOReturn r = rtl_rxcomp_enqueue(_rxCompQ, pkts, n, 0);
	if (r != kIOReturnSuccess) {
		for (uint32_t i = 0; i < n; i++)
			rtl_pool_free(_rxPool, pkts[i]);
		_rxDropped += n;
		if (_rxDataFail++ < AWDL_RX_LOG_FIRST)
			LOG("awdl: rx data enqueue -> 0x%x", r);
		return;
	}
	if (_rxDataUp == 0)
		LOG("awdl: first data packet up from %02x:%02x:%02x:%02x:%02x:%02x ethertype %02x%02x",
		    sub[0].sa[0], sub[0].sa[1], sub[0].sa[2], sub[0].sa[3], sub[0].sa[4], sub[0].sa[5],
		    sub[0].msdu[14], sub[0].msdu[15]);
	_rxDataUp += n;
	void *pm = rtl_sky_getPeerManager(this);
	for (uint32_t i = 0; pm && i < ns; i++)
		if (!(sub[i].da[0] & 1))
			pmCount(pm, AWDL_PM_RX_UNICAST, AWDL_PM_RX_BYTES, (uint32_t)sub[i].len);
}

/*
 * TX of AWDL action frames. The manager hands the static part of its sync
 * frame in the template of type 151 (election v2, service params, data path
 * state, version, sync tree...; Docs/AWDL_PEERMANAGER_DRIVER_ABI.md §3.11)
 * and leaves the rest to the driver: the timing in the sync params TLV (4)
 * and the channel sequence (18) are rendered here, as Broadcom's firmware does
 * (Docs/AWDL_CHANNEL_SEQUENCE_CONTROL.md §5.2), plus election v1 (5) from the
 * template's v2. The radio does not hop: the slots the manager's sequence
 * (129) puts on the radio's channel announce it and get a window (events 41
 * and 43), the others announce 0 (AWDL-12 of owly's docs/SPEC.md, the
 * fixed-radio pattern). Off the social channels (6, 44, 149) nothing is sent.
 *
 * The AW clock is our own until an action frame arrives from a node that is
 * its own master: then the slot boundary and the AW count of its TLV 4 are
 * taken (AWDL-7). Timing values follow owly's node: AW 16 TU, 4 AWs per slot,
 * 16 slots, PSF every 110 TU, MIF in the middle of each slot. The MIF carries
 * the manager's MI template, whose ARPA (16) TLV the kernel adds while an
 * ARPA service is registered (Docs/AWDL_PSF_MIF_EMISSION.md, section 3.2).
 */
#define AWDL_TU_US            1024
#define AWDL_AW_TU            16
#define AWDL_PM               4       /* AWs per slot (presence mode) */
#define AWDL_SLOT_TU          (AWDL_AW_TU * AWDL_PM)
#define AWDL_MCC_PEER_HOLD_US 30000000 /* whole served slots this long after a peer's frame */
#define AWDL_SLOTS            16
#define AWDL_PSF_TU           110
#define AWDL_AF_PSF           0
#define AWDL_AF_MIF           3
#define AWDL_AF_VERSION       0x10    /* 1.0, major in the high nibble */
#define AWDL_TLV_SYNC         4
#define AWDL_TLV_ELECTION     5
#define AWDL_TLV_CHANSEQ      18
#define AWDL_TLV_ELECTION_V2  24
#define AWDL_SYNC_LEN         33      /* TLV 4 value up to the channel sequence */
#define AWDL_CHANSEQ_LEN      (6 + 2 * AWDL_SLOTS)
#define AWDL_EV_AW_START      41      /* APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START */
#define AWDL_EV_AW_END        43      /* ..._AVAILABILITY_WINDOW_EXTENSIONS_END */
#define AWDL_EV_PEER_CACHE    51      /* reportDataPathEvents, handleCacheControlMessages */

static const uint8_t kAwdlBssid[6] = { 0x00, 0x25, 0x00, 0xFF, 0x94, 0x73 };

static uint64_t awdlNowUs()
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000;
}

static void putLe16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void putLe32(uint8_t *p, uint32_t v) { putLe16(p, (uint16_t)v); putLe16(p + 2, (uint16_t)(v >> 16)); }
static uint16_t getLe16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t getLe32(const uint8_t *p) { return getLe16(p) | (uint32_t)getLe16(p + 2) << 16; }

/* The TX rate ladder's rungs: descriptor rate index and Mb/s (updateRate). */
static const uint8_t kAwdlRate[7] = { 0x04, 0x06, 0x08, 0x09, 0x0a, 0x0b, 0x13 };
static const uint8_t kAwdlRateMbps[7] = { 6, 12, 24, 36, 48, 54, 65 };

/*
 * Block Ack recipient (Docs/AWDL_BLOCK_ACK.md §6). The family never answers an
 * ADDBA (§1), and before this the layer dropped every one, so the iPhone sent
 * one MPDU per access. The RTL8814AU MAC sends the BlockAck frames itself: the
 * vendor driver programs no recipient session even for its own AP, and still
 * receives A-MPDUs there. What is left to the host is the ADDBA response, the
 * peer's DELBA and the order: a window per (peer, TID) holds frames that came
 * ahead of a gap until the gap is filled, slides past it, or waits too long.
 */
#define AWDL_BA_HOLD_US       100000  /* a gap older than this is skipped (Linux: HZ/10) */
#define AWDL_BA_OUT           66
#define AWDL_BA_DELBA_RUN     4       /* DELBAs in AWDL_BA_DELBA_US turn the answer to 37 */
#define AWDL_BA_DELBA_US      10000000
#define AWDL_BA_REFUSE_US     60000000 /* how long a DELBA storm turns the answer to 37 */

/* Queues an 802.11 management frame for the destination's next window, ahead
 * of the data already waiting. */
bool RTLAwdInterface::queueMgmt(const uint8_t *frame, uint16_t len)
{
	IOLockLock(_txLock);
	if (!_txOn || !_txRing || len < AWDL_WLAN_HDR || len > sizeof(_txRing->frame)) {
		IOLockUnlock(_txLock);
		return false;
	}
	/* At the head: an answer waiting behind a ring of data could age out
	 * there (AWDL_TX_AGE_US), and the peer only retries every 2 s. A full ring
	 * gives up its newest data frame. */
	if (_txRingCount == AWDL_TX_RING) {
		_txRingCount--;
		_txDataDropFull++;
	}
	_txRingHead = (_txRingHead + AWDL_TX_RING - 1) % AWDL_TX_RING;
	AwdlTxFrame *t = &_txRing[_txRingHead];
	memcpy(t->frame, frame, len);
	t->len = len;
	t->tsUs = (uint32_t)awdlNowUs();
	_txRingCount++;
	IOLockUnlock(_txLock);
	return true;
}

/* Moves the frames waiting at the head of the window to out[], in order, and
 * advances the window past them. With skipHoles it also steps over the gaps,
 * until nothing waits. Under _baLock. */
uint32_t RTLAwdInterface::baRelease(AwdlBaSession *s, mbuf_t *out, uint32_t cap, bool skipHoles)
{
	uint32_t n = 0;
	for (uint32_t i = 0; s->held && n < cap && i < 64; i++) {
		mbuf_t *sl = &s->slot[s->ws & 63];
		if (*sl) {
			out[n++] = *sl;
			*sl = nullptr;
			s->held--;
		} else if (!skipHoles) {
			break;
		}
		s->ws = (uint16_t)((s->ws + 1) & 0xFFF);
	}
	if (!s->held)
		s->heldUs = 0;
	return n;
}

/* Ends a session: what it held goes to out[] (or is freed past cap). Under
 * _baLock. */
void RTLAwdInterface::baClose(AwdlBaSession *s, mbuf_t *out, uint32_t *n, uint32_t cap)
{
	for (uint32_t i = 0; i < 64; i++) {
		uint16_t q = (uint16_t)((s->ws + i) & 63);
		if (!s->slot[q])
			continue;
		if (*n < cap)
			out[(*n)++] = s->slot[q];
		else
			mbuf_freem(s->slot[q]);
		s->slot[q] = nullptr;
	}
	s->held = 0;
	s->heldUs = 0;
	s->on = false;
}

/* Skips the gaps that have waited too long. A session is never closed for
 * being idle: the peer still holds the agreement and would go on sending
 * A-MPDUs, which would then go up unordered (seen 2026-11-09 with the virtual
 * peer after 30 s without data). It ends with the peer's DELBA, a new ADDBA
 * or when the least recently used of the eight makes room. Runs on the RX
 * drain with each action frame and when txTick kicks it. */
void RTLAwdInterface::baExpire(uint64_t now)
{
	if (!_baLock)
		return;
	mbuf_t out[AWDL_BA_OUT];
	uint32_t n = 0;
	IOLockLock(_baLock);
	for (uint32_t i = 0; i < kBaSessions; i++) {
		AwdlBaSession *s = &_ba[i];
		if (!s->on)
			continue;
		if (s->held && now - s->heldUs > AWDL_BA_HOLD_US) {
			n += baRelease(s, out + n, AWDL_BA_OUT - n, true);
			_baTimeout++;
		}
	}
	IOLockUnlock(_baLock);
	for (uint32_t i = 0; i < n; i++)
		deliverData(out[i]);
}

/* A session holds a frame past its timeout. */
bool RTLAwdInterface::baExpireDue(uint64_t now)
{
	if (!_baLock)
		return false;
	bool due = false;
	IOLockLock(_baLock);
	for (uint32_t i = 0; i < kBaSessions && !due; i++) {
		AwdlBaSession *s = &_ba[i];
		due = s->on && s->held && now - s->heldUs > AWDL_BA_HOLD_US;
	}
	IOLockUnlock(_baLock);
	return due;
}

/* Closes every session; their frames go up when deliver is set. */
void RTLAwdInterface::baFlushAll(bool deliver)
{
	if (!_baLock)
		return;
	mbuf_t out[AWDL_BA_OUT];
	for (uint32_t i = 0; i < kBaSessions; i++) {
		uint32_t n = 0;
		IOLockLock(_baLock);
		baClose(&_ba[i], out, &n, AWDL_BA_OUT);
		_ba[i].lastUs = 0;
		IOLockUnlock(_baLock);
		for (uint32_t j = 0; j < n; j++) {
			if (deliver)
				deliverData(out[j]);
			else
				mbuf_freem(out[j]);
		}
	}
}

/*
 * A Block Ack action frame from a peer (IEEE 802.11-2020 9.6.4). ADDBA request:
 * category 3, action 0, token, parameter set (A-MSDU bit 0, immediate policy
 * bit 1, TID bits 2-5, buffer size bits 6-15), timeout, starting sequence
 * control. The answer echoes the token, the A-MSDU bit (deliverData splits
 * A-MSDU), the policy and the TID, with a window of at most 64 and the
 * timeout as asked. Delayed Block Ack, a TID above 7 or no port 1 (the MAC
 * would not acknowledge on awdl0's address) get status 37. DELBA: the peer
 * as initiator (bit 11) ends our recipient session for its TID (bits 12-15).
 * Consumes m.
 */
void RTLAwdInterface::handleBlockAck(mbuf_t m)
{
	uint8_t f[AWDL_WLAN_HDR + 9];
	size_t len = mbuf_pkthdr_len(m) - AWDL_RX_META;
	size_t fl = len < sizeof(f) ? len : sizeof(f);
	bool ok = mbuf_copydata(m, AWDL_RX_META, fl, f) == 0;
	mbuf_freem(m);
	if (!ok || fl < AWDL_WLAN_HDR + 2 || memcmp(f + 4, _mac, 6) || (f[10] & 1))
		return;
	const uint8_t *b = f + AWDL_WLAN_HDR;
	uint64_t now = awdlNowUs();
	mbuf_t out[AWDL_BA_OUT];
	uint32_t n = 0;
	if (b[1] == 0 && fl >= AWDL_WLAN_HDR + 9) {
		uint16_t params = getLe16(b + 3), ssc = getLe16(b + 7);
		uint8_t tid = (params >> 2) & 0xF;
		uint16_t win = params >> 6;
		if (win > 64)
			win = 64;
		if (_baRefuse && now - _baRefuseUs > AWDL_BA_REFUSE_US) {
			_baRefuse = false;
			_baDelbaUs = 0;
			LOG("awdl: accepting ADDBA again");
		}
		/* A buffer size of 0 is malformed: an accepted session needs 1 or more. */
		bool accept = _baAllowed && !_baRefuse && _port1On && tid < 8 && (params & 2) && win;
		_baReq++;
		if (accept) {
			IOLockLock(_baLock);
			AwdlBaSession *s = nullptr, *idle = nullptr, *oldest = &_ba[0];
			for (uint32_t i = 0; i < kBaSessions && !s; i++) {
				AwdlBaSession *c = &_ba[i];
				if (c->lastUs && c->tid == tid && !memcmp(c->peer, f + 10, 6))
					s = c;
				else if (!c->lastUs && !idle)
					idle = c;
				else if (c->lastUs < oldest->lastUs)
					oldest = c;
			}
			/* The same request again (our answer was lost or late): the
			 * session and its window stay, or frames it holds would go up
			 * ahead of the one the peer is about to resend. Only a starting
			 * sequence outside the window starts it over. */
			uint16_t ssn = (uint16_t)(ssc >> 4);
			bool keep = s && s->on &&
			            (((ssn - s->ws) & 0xFFF) < s->win || ((s->ws - ssn) & 0xFFF) < s->win);
			if (!s)
				s = idle ? idle : oldest;
			if (!keep) {
				baClose(s, out, &n, AWDL_BA_OUT);
				memcpy(s->peer, f + 10, 6);
				s->tid = tid;
				s->ws = ssn;
				s->win = win;
				s->oldRun = 0;
				s->on = true;
			} else {
				/* The answer must not offer more than this request asks:
				 * the window follows it. Frames held past a smaller one are
				 * still released in order as it slides. */
				s->win = win;
			}
			s->lastUs = now;
			IOLockUnlock(_baLock);
		}
		uint16_t status = accept ? 0 : 37;
		uint8_t r[AWDL_WLAN_HDR + 9];
		bzero(r, sizeof(r));
		r[0] = 0xD0;
		memcpy(r + 4, f + 10, 6);
		memcpy(r + 10, _mac, 6);
		memcpy(r + 16, kAwdlBssid, 6);
		uint8_t *p = r + AWDL_WLAN_HDR;
		p[0] = 3;
		p[1] = 1;
		p[2] = b[2];
		putLe16(p + 3, status);
		putLe16(p + 5, accept ? (uint16_t)((params & 0x3F) | (win << 6)) : params);
		p[7] = b[5];
		p[8] = b[6];
		bool queued = queueMgmt(r, sizeof(r));
		if (accept)
			_baAccept++;
		else
			_baDecline++;
		if (_baReq <= AWDL_RX_LOG_FIRST)
			LOG("awdl: ADDBA from %02x:%02x:%02x:%02x:%02x:%02x tid %u win %u ssn %u amsdu %u -> "
			    "status %u%s", f[10], f[11], f[12], f[13], f[14], f[15], tid, params >> 6,
			    ssc >> 4, params & 1, status, queued ? "" : " (not queued: tx off)");
	} else if (b[1] == 1 && fl >= AWDL_WLAN_HDR + 7) {
		/* The answer to a request of ours (AWDLBaOrig.h). */
		uint16_t status = getLe16(b + 3), params = getLe16(b + 5);
		IOLockLock(_txLock);
		bool on = awdlBaoResponse(&_bao, f + 10, b[2], status, params, now);
		uint64_t count = _bao.accepted + _bao.refused;
		IOLockUnlock(_txLock);
		if (count <= AWDL_RX_LOG_FIRST)
			LOG("awdl: ADDBA response from %02x:%02x:%02x:%02x:%02x:%02x token %u status %u tid %u "
			    "win %u: %s", f[10], f[11], f[12], f[13], f[14], f[15], b[2], status,
			    (params >> 2) & 0xF, params >> 6, on ? "A-MPDU on" : "no session");
	} else if (b[1] == 2 && fl >= AWDL_WLAN_HDR + 6) {
		uint16_t params = getLe16(b + 2);
		uint8_t tid = (uint8_t)(params >> 12);
		_baDelba++;
		if (!(params & 0x0800)) {
			/* The peer closes a session of ours. */
			IOLockLock(_txLock);
			awdlBaoDelba(&_bao, f + 10, tid, now);
			IOLockUnlock(_txLock);
		}
		if (params & 0x0800) {
			IOLockLock(_baLock);
			for (uint32_t i = 0; i < kBaSessions; i++) {
				AwdlBaSession *s = &_ba[i];
				if (s->on && s->tid == tid && !memcmp(s->peer, f + 10, 6)) {
					baClose(s, out, &n, AWDL_BA_OUT);
					s->lastUs = 0;
				}
			}
			/* Sessions torn down again and again mean the peer gets no
			 * BlockAck: back to single MPDUs, as before. */
			if (!_baDelbaUs || now - _baDelbaUs > AWDL_BA_DELBA_US) {
				_baDelbaUs = now;
				_baDelbaRun = 0;
			}
			if (++_baDelbaRun >= AWDL_BA_DELBA_RUN && !_baRefuse) {
				_baRefuse = true;
				_baRefuseUs = now;
				LOG("awdl: %u DELBAs in %u s: declining ADDBA for %u s", _baDelbaRun,
				    AWDL_BA_DELBA_US / 1000000, AWDL_BA_REFUSE_US / 1000000);
			}
			IOLockUnlock(_baLock);
		}
		if (_baDelba <= AWDL_RX_LOG_FIRST)
			LOG("awdl: DELBA from %02x:%02x:%02x:%02x:%02x:%02x tid %u initiator %u reason %u",
			    f[10], f[11], f[12], f[13], f[14], f[15], tid, (params >> 11) & 1, getLe16(b + 4));
	}
	for (uint32_t i = 0; i < n; i++)
		deliverData(out[i]);
}

/*
 * A data frame into its session's window, or straight up: group frames,
 * frames without QoS and frames of a (peer, TID) with no session go to
 * deliverData as they come. Sequence numbers are 12 bits; a frame behind the
 * window is a retry already delivered or skipped, and a run of 64 of them
 * means the peer started over without a new ADDBA. Consumes m.
 */
void RTLAwdInterface::rxReorder(mbuf_t m)
{
	uint8_t f[AWDL_WLAN_HDR + 2];
	size_t len = mbuf_pkthdr_len(m);
	if (!_baLock || len < AWDL_RX_META + sizeof(f) ||
	    mbuf_copydata(m, AWDL_RX_META, sizeof(f), f) != 0 || f[0] != 0x88 ||
	    memcmp(f + 4, _mac, 6)) {
		deliverData(m);
		return;
	}
	uint8_t tid = f[AWDL_WLAN_HDR] & 0x0F;
	uint16_t seq = getLe16(f + 22) >> 4;
	uint64_t now = awdlNowUs();
	mbuf_t out[AWDL_BA_OUT];
	uint32_t n = 0;
	bool direct = true, drop = false;
	IOLockLock(_baLock);
	AwdlBaSession *s = nullptr;
	for (uint32_t i = 0; i < kBaSessions && !s; i++)
		if (_ba[i].on && _ba[i].tid == tid && !memcmp(_ba[i].peer, f + 10, 6))
			s = &_ba[i];
	if (s) {
		direct = false;
		s->lastUs = now;
		uint16_t d = (uint16_t)((seq - s->ws) & 0xFFF);
		if (d >= 2048) {
			_baOld++;
			if (++s->oldRun < 64) {
				drop = true;
			} else {
				n = baRelease(s, out, AWDL_BA_OUT - 2, true);
				s->ws = seq;
				d = 0;
			}
		}
		if (!drop) {
			s->oldRun = 0;
			if (d >= s->win) {
				/* Beyond the window: slide it so this frame is its last. */
				uint16_t nws = (uint16_t)((seq - s->win + 1) & 0xFFF);
				while (s->held && s->ws != nws) {
					mbuf_t *sl = &s->slot[s->ws & 63];
					if (*sl) {
						out[n++] = *sl;
						*sl = nullptr;
						s->held--;
					}
					s->ws = (uint16_t)((s->ws + 1) & 0xFFF);
				}
				s->ws = nws;
				if (!s->held)
					s->heldUs = 0;
				d = (uint16_t)(s->win - 1);
			}
			mbuf_t *sl = &s->slot[seq & 63];
			if (d == 0) {
				out[n++] = m;
				s->ws = (uint16_t)((s->ws + 1) & 0xFFF);
				n += baRelease(s, out + n, AWDL_BA_OUT - n, false);
			} else if (*sl) {
				_baDup++;
				drop = true;
			} else {
				*sl = m;
				if (!s->held++)
					s->heldUs = now;
				_baHeld++;
			}
			if (s->held && now - s->heldUs > AWDL_BA_HOLD_US) {
				n += baRelease(s, out + n, AWDL_BA_OUT - n, true);
				_baTimeout++;
			}
		}
	}
	IOLockUnlock(_baLock);
	if (direct) {
		deliverData(m);
		return;
	}
	if (drop)
		mbuf_freem(m);
	for (uint32_t i = 0; i < n; i++)
		deliverData(out[i]);
}

/* Operating class of a 20 MHz slot as owly announces it: 81 on 2.4 GHz, 128 on 5 GHz. */
static uint8_t awdlOpClass(uint8_t ch) { return ch <= 14 ? 81 : 128; }

/* Channel sequence: 16 slots, opclass encoding, step 3, fill 0xFFFF; ch in
 * the served slots of mask, 0 in the others (AWDL-12). */
static void putChanSeq(uint8_t *p, uint8_t ch, uint16_t mask)
{
	p[0] = AWDL_SLOTS - 1;
	p[1] = 3;
	p[2] = 0;
	p[3] = AWDL_PM - 1;
	putLe16(p + 4, 0xFFFF);
	for (int i = 0; i < AWDL_SLOTS; i++) {
		bool on = (mask >> i) & 1;
		p[6 + 2 * i] = on ? ch : 0;
		p[7 + 2 * i] = on ? awdlOpClass(ch) : 0;
	}
}

/* First TLV of a type in a TLV run, or NULL. */
static const uint8_t *findTlv(const uint8_t *p, size_t len, uint8_t type, uint16_t minLen)
{
	size_t off = 0;
	while (off + 3 <= len) {
		uint16_t l = getLe16(p + off + 1);
		if (off + 3 + l > len)
			break;
		if (p[off] == type && l >= minLen)
			return p + off;
		off += 3 + l;
	}
	return nullptr;
}

IOReturn RTLAwdInterface::setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *arg)
{
	/* {u32 reserved, u32 length, u8 *template} (ABI §3.11) */
	const uint8_t *p = (const uint8_t *)arg;
	uint32_t len = p ? *(const uint32_t *)(p + 4) : 0;
	const uint8_t *tpl = p ? *(const uint8_t *const *)(p + 8) : nullptr;
	if (!tpl || len > sizeof(_tpl)) {
		LOG("awdl: sync frame template refused, len %u", len);
		return kIOReturnSuccess;
	}
	bool first = false;
	if (_txLock) {
		IOLockLock(_txLock);
		first = _tplLen == 0;
		memcpy(_tpl, tpl, len);
		_tplLen = len;
		IOLockUnlock(_txLock);
	}
	if (first) {
		LOG("awdl: sync frame template %u bytes", len);
		/* Once, in hex, to set against the TLVs of real PSFs. */
		static const char hex[] = "0123456789abcdef";
		char line[2 * 64 + 1];
		for (uint32_t off = 0; off < len; off += 64) {
			uint32_t n = len - off < 64 ? len - off : 64;
			for (uint32_t i = 0; i < n; i++) {
				line[2 * i] = hex[tpl[off + i] >> 4];
				line[2 * i + 1] = hex[tpl[off + i] & 0xF];
			}
			line[2 * n] = '\0';
			LOG("awdl: template +%u %s", off, line);
		}
	}
	return kIOReturnSuccess;
}

/*
 * The 56-byte request of the 149 and the 225 (Docs/AWDL_PSF_MIF_EMISSION.md
 * §2.2): destination at +0x04, transmit offset at +0x0c, MI bitmap at +0x10,
 * template length (u16) at +0x28 and the kernel's template at +0x30, rebuilt
 * by the manager before each request. With _txLock held.
 */
bool RTLAwdInterface::copyMiTemplate(const uint8_t *req)
{
	uint16_t len = getLe16(req + 0x28);
	const uint8_t *tpl = *(const uint8_t *const *)(req + 0x30);
	if (!tpl || !len || len > sizeof(_miTpl))
		return false;
	memcpy(_miTpl, tpl, len);
	_miTplLen = len;
	return true;
}

IOReturn RTLAwdInterface::setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *arg)
{
	const uint8_t *p = (const uint8_t *)arg;
	if (!p || !_txLock)
		return kIOReturnSuccess;
	IOLockLock(_txLock);
	bool first = !_miTplLen;
	bool ok = copyMiTemplate(p);
	IOLockUnlock(_txLock);
	if (first || !ok)
		LOG("awdl: MI template %u bytes%s, bitmap 0x%x offset %u us", getLe16(p + 0x28),
		    ok ? "" : " refused", *(const uint32_t *)(p + 0x10), *(const uint32_t *)(p + 0x0c));
	return kIOReturnSuccess;
}

IOReturn RTLAwdInterface::setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *arg)
{
	static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	const uint8_t *p = (const uint8_t *)arg;
	if (!p || !_txLock)
		return kIOReturnSuccess;
	const uint8_t *dst = p + 0x04;
	uint16_t len = getLe16(p + 0x28);
	const uint8_t *tpl = *(const uint8_t *const *)(p + 0x30);
	bool queued = true;
	IOLockLock(_txLock);
	if (!memcmp(dst, bcast, 6)) {
		copyMiTemplate(p);
		_mifNow = true;
	} else {
		/* A unicast 149's template lacks the service TLVs and carries the UMI's
		 * options: it serves that frame only, not the broadcast MIFs. */
		uint8_t i = 0;
		while (i < _umiCount && memcmp(_umi[i].dst, dst, 6))
			i++;
		if ((i == _umiCount && _umiCount == 4) || !tpl || !len || len > sizeof(_umi[i].tpl)) {
			queued = false;
		} else {
			memcpy(_umi[i].dst, dst, 6);
			memcpy(_umi[i].tpl, tpl, len);
			_umi[i].len = len;
			if (i == _umiCount)
				_umiCount++;
		}
	}
	IOLockUnlock(_txLock);
	if (!queued)
		_txUmiDrop++;
	else if (memcmp(dst, bcast, 6) && _txUmi + _umiCount <= AWDL_RX_LOG_FIRST)
		LOG("awdl: umi to %02x:%02x:%02x:%02x:%02x:%02x", dst[0], dst[1], dst[2], dst[3], dst[4],
		    dst[5]);
	return kIOReturnSuccess;
}

/*
 * The manager's own channel sequence (129): 16 entries of 12 bytes from +0x0c,
 * {version, channel, flags} (Docs/AWDL_PEERMANAGER_DRIVER_ABI.md §3.7). A slot
 * is served when its channel is the radio's; the others are announced as 0
 * and get no window (Docs/AWDL_WINDOW_TIMER.md §4).
 */
IOReturn RTLAwdInterface::setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
    apple80211_awdl_sync_channel_sequence *arg)
{
	const uint8_t *p = (const uint8_t *)arg;
	if (!p || !_txLock)
		return kIOReturnSuccess;
	uint8_t n = p[5] < AWDL_SLOTS ? p[5] : AWDL_SLOTS;
	uint8_t s[AWDL_SLOTS];
	IOLockLock(_txLock);
	bool changed = !_seqValid;
	for (int i = 0; i < AWDL_SLOTS; i++) {
		const uint8_t *e = p + 0x0c + 12 * i;
		s[i] = i < n ? (uint8_t)*(const uint32_t *)(e + 4) : 0;
		changed |= s[i] != _seq[i];
		_seq[i] = s[i];
		_seqVer[i] = i < n ? *(const uint32_t *)e : 0;
		_seqFlags[i] = i < n ? *(const uint32_t *)(e + 8) : 0;
	}
	_seqValid = n != 0;
	/* Every change, not only the first: associating moves the infra channel
	 * into the master sequence (Docs/AWDL_CHANNEL_STRATEGY.md §6). */
	bool log = changed && _seqLogs < 32;
	if (log)
		_seqLogs++;
	IOLockUnlock(_txLock);
	if (log)
		LOG("awdl: channel sequence %u slots: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u", n,
		    s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12],
		    s[13], s[14], s[15]);
	return kIOReturnSuccess;
}

void RTLAwdInterface::setRadioTx(bool on)
{
	if (on && (!_txAllowed || !_ctrl || !_ctrl->hasRadio()))
		return;
	if (!_txLock || !_txTick)
		return;
	IOLockLock(_txLock);
	bool was = _txOn;
	_txOn = on;
	if (on && !was) {
		_clkBaseUs = awdlNowUs();
		_clkBaseAw = 0;
		_nextPsfUs = _clkBaseUs;
		_lastMifSlot = UINT32_MAX;
		_lastTickAw = UINT32_MAX;
		_parkReqUs = 0;
		/* a driver swapped since the last refusal gets another try */
		_txDataOff = false;
	}
	_awOpen = false;
	IOLockUnlock(_txLock);
	if (on == was)
		return;
	/* Port 1 answers for awdl0 while it transmits, so the unicast frames peers
	 * send back get a hardware ACK (Docs/AWDL_RTL8814_RX_ACK.md §2.4). */
	if (on) {
		IOReturn r = _ctrl->radio(RTL_RADIO_FN_AWDL_PORT, _mac);
		if (r != kIOReturnSuccess)
			LOG("awdl: port 1 refused 0x%x: no unicast data TX", r);
		_port1On = r == kIOReturnSuccess;
		/* A tick still running from before the last off rearms itself. */
		IOLockLock(_txLock);
		if (_txOn && !_tickBusy)
			armTick(0);
		IOLockUnlock(_txLock);
	} else {
		/* No wait here: disable comes from the BSD client's ioctl, which
		 * holds its gate, and a running tick may be waiting for the
		 * controller's gate in postMessage. A running tick finds _txOn
		 * false and does not rearm; stop() waits for it. */
		IOLockLock(_txLock);
		if (!_txOn && thread_call_cancel(_txTick))
			release();
		_pumpOk = false;
		if (!_txOn && _txPump && thread_call_cancel(_txPump))
			release();
		IOLockUnlock(_txLock);
		_ctrl->radio(RTL_RADIO_FN_AWDL_PORT, nullptr);
		_port1On = false;
		/* After _txOn went false: a tick that switches later sees it under
		 * _mccLock and stays on the BSS */
		mccSwitch(0);
	}
	if (!on)
		flushRadioTx();
	LOG("awdl: tx %s, psf %llu mif %llu umi %llu/%llu fail %llu aligned %llu aw %llu, data queued "
	    "%llu sent %llu dropped full %llu old %llu bad %llu fail %llu", on ? "on" : "off", _txPsf,
	    _txMif, _txUmi, _txUmiDrop, _txFail, _clkAligned, _ev41, _txDataQueued, _txDataSent,
	    _txDataDropFull, _txDataDropOld, _txDataDropBad, _txDataFail);
}

void RTLAwdInterface::txTickEntry(thread_call_param_t p0, thread_call_param_t)
{
	RTLAwdInterface *me = (RTLAwdInterface *)p0;
	me->txTick();
	/* the reference armTick took for this call */
	me->release();
}

/* With _txLock held. The pending call owns a reference until txTickEntry
 * ends, as the RX drain's does. */
void RTLAwdInterface::armTick(uint64_t deadline)
{
	retain();
	if (deadline ? thread_call_enter_delayed(_txTick, deadline) : thread_call_enter(_txTick))
		release();
}

/* AWDL meets peers only on the social channels (AWDL-11 of owly's spec). */
static bool awdlSocial(uint8_t ch)
{
	return ch == 6 || ch == 44 || ch == 149;
}

/*
 * Runs at each AW boundary of the clock. At a slot start it closes the open
 * window (event 43, which also lets the manager run its election) and, when
 * the slot is served, opens the next one (event 41). A PSF goes out every
 * 110 TU and a MIF in the third AW of each served slot, or with time sharing
 * in the last AW the radio is sure to stay for (mccChannel).
 */
void RTLAwdInterface::txTick()
{
	/* One tick at a time: an enter right after an off can start a second
	 * call while the first still runs. */
	IOLockLock(_txLock);
	bool run = _txOn && !_tickBusy;
	if (run)
		_tickBusy = true;
	IOLockUnlock(_txLock);
	if (!run)
		return;

	struct rtl_radio_status st;
	uint8_t ch = 0;
	bool linked = false, mcc = false, shared = false;
	if (_ctrl && _ctrl->radioStatus(&st) == kIOReturnSuccess) {
		linked = st.state == RTL_RADIO_S_LINKED;
		/* the radio belongs to a BSS on another channel, also during its scans */
		shared = linked && !awdlSocial(st.channel);
		/* A scan can last longer than the family keeps a silent peer, so while
		 * one is around the served slots go on in the scan's back-ops. A slot
		 * started there always gets its return: the scan waits for it. */
		bool backop = st.scanning == RTL_RADIO_SCAN_BACKOP && linked && !awdlSocial(st.channel);
		if (backop && !_mccCur) {
			uint64_t at = awdlNowUs();
			IOLockLock(_txLock);
			backop = _mccPeerUs && at < _mccPeerUs + AWDL_MCC_PEER_HOLD_US;
			IOLockUnlock(_txLock);
		}
		/* Without an association the driver brings a scan back to the parked
		 * channel between groups of channels while a peer is around, and
		 * refuses AWDL frames outside those returns: the windows go on there. */
		if (st.scanning == RTL_RADIO_SCAN_BACKOP && st.state == RTL_RADIO_S_SCAN) {
			ch = parkRadio(st.idle_channel, awdlNowUs(), true);
		} else if (st.scanning && !backop) {
			/* The scan's own back-op brings the radio home at its end. In a
			 * back-op it is home already, or waits for the return below. */
			if (_mccLock && st.scanning != RTL_RADIO_SCAN_BACKOP) {
				IOLockLock(_mccLock);
				_mccCur = 0;
				IOLockUnlock(_mccLock);
			}
		} else if (linked) {
			ch = st.channel;
			if (!awdlSocial(ch) && _mccMode && !_mccOff) {
				mcc = true;
				/* A tick armed ahead of a served slot decides as at its start,
				 * so the radio is already there when the peers' frames come. */
				uint64_t at = awdlNowUs();
				IOLockLock(_txLock);
				if (_mccPreUs) {
					if (at < _mccPreUs)
						at = _mccPreUs;
					_mccPreUs = 0;
				}
				IOLockUnlock(_txLock);
				ch = mccChannel(ch, at);
			}
		} else if (st.state == RTL_RADIO_S_IDLE) {
			ch = parkRadio(st.idle_channel, awdlNowUs(), _ctrl->infraBusy());
		}
	}
	if (!linked && _mccCur)
		mccSwitch(0);
	updateRate();
	aggRate(awdlNowUs());
	/* Held frames get their timeout from the RX drain, which only runs when
	 * frames arrive: a quiet peer gets it from here. */
	if (baExpireDue(awdlNowUs()) && _rxDrain) {
		retain();
		if (thread_call_enter(_rxDrain))
			release();
	}
	IOLockLock(_txLock);
	_mccActive = mcc;
	/* dense presence needs windows: none on a BSS's channel without time sharing */
	bool denseChanged = updateDense(awdlNowUs(), shared && !(mcc && _mccRun));
	bool denseNow = _dense;
	IOLockUnlock(_txLock);
	if (denseChanged && _denseChanges <= 64)
		LOG("awdl: dense presence %s%s (rx data %llu, tx data %llu)", denseNow ? "on" : "off",
		    mcc ? ", radio shared with the BSS" : "", _rxData, _txDataSent);
	if (ch && !awdlSocial(ch)) {
		if (ch != _offSocialCh) {
			_offSocialCh = ch;
			LOG("awdl: radio on channel %u (bw %u center %u), not a social channel: no AWDL TX "
			    "or windows", ch, st.bss_bw, st.center);
		}
		ch = 0;
	}

	uint8_t ev41[36], ev43[40];
	bool close = false, open = false, psf = false, mif = false;
	IOLockLock(_txLock);
	if (!_txOn) {
		_tickBusy = false;
		IOLockUnlock(_txLock);
		return;
	}
	/* After the lock, so it is never before a _clkBaseUs set by alignClock. */
	uint64_t now = awdlNowUs();
	/* 500 µs of slack: a tick that fires just before its boundary counts as on it. */
	uint64_t awIdx = (now - _clkBaseUs + 500) / (AWDL_AW_TU * AWDL_TU_US);
	uint32_t aw = (uint16_t)(_clkBaseAw + awIdx);
	uint64_t awStart = _clkBaseUs + awIdx * AWDL_AW_TU * AWDL_TU_US;
	uint32_t slot = (aw / AWDL_PM) % AWDL_SLOTS;
	uint16_t mask = servedMask(ch);
	bool served = (mask >> slot) & 1;
	if (aw != _lastTickAw) {
		_lastTickAw = aw;
		if (aw % AWDL_PM == 0 || !ch) {
			close = _awOpen && _awPosted;
			_awOpen = false;
		}
		if (aw % AWDL_PM == 0 && served) {
			/* A slot only dense presence serves is a window of ours, which the
			 * peers see in TLV 18, but not the family's: its sequence has 0
			 * there, so events 41 and 43 are left out. */
			_awPosted = !_seqValid || _seq[slot] == ch;
			open = _awPosted;
			_awOpen = true;
		}
		/* in the middle of the slot, or in the last AW the time sharing stays for */
		uint32_t mifAw = mcc && _mccDwell <= AWDL_PM / 2 ? _mccDwell - 1 : AWDL_PM / 2;
		if (served && aw % AWDL_PM == mifAw && aw / AWDL_PM != _lastMifSlot) {
			mif = true;
			_lastMifSlot = aw / AWDL_PM;
		}
	}
	if (mask && now >= _nextPsfUs) {
		psf = true;
		while (_nextPsfUs <= now)
			_nextPsfUs += AWDL_PSF_TU * AWDL_TU_US;
	}
	if (open) {
		/* Event 41 (Docs/AWDL_WINDOW_TIMER.md §5.1): AW number, sync state,
		 * master and top master of the template's election v2, slot start in
		 * µs big-endian, and the window's apple80211_channel: version, channel,
		 * flags. The manager checks the flags against the sequence's, and
		 * handleUmiTimer against the peer's before it sends a UMI. */
		const uint8_t *e2 = findTlv(_tpl, _tplLen, AWDL_TLV_ELECTION_V2, 28);
		const uint8_t *top = e2 ? e2 + 3 : _mac;
		const uint8_t *sync = e2 ? e2 + 3 + 6 : _mac;
		bzero(ev41, sizeof(ev41));
		*(uint16_t *)ev41 = (uint16_t)aw;
		*(uint32_t *)(ev41 + 0x04) = memcmp(sync, _mac, 6) ? 2 : 1;
		memcpy(ev41 + 0x08, sync, 6);
		memcpy(ev41 + 0x0e, top, 6);
		*(uint32_t *)(ev41 + 0x14) = OSSwapHostToBigInt32((uint32_t)awStart);
		*(uint32_t *)(ev41 + 0x18) = _seqValid && _seqVer[slot] ? _seqVer[slot] : 1;
		*(uint32_t *)(ev41 + 0x1c) = ch;
		*(uint32_t *)(ev41 + 0x20) = _seqValid && _seqFlags[slot] ? _seqFlags[slot]
		                                                           : (ch <= 14 ? 0x8 : 0x10);
	}
	uint64_t next = _clkBaseUs + (awIdx + 1) * AWDL_AW_TU * AWDL_TU_US;
	/* Data goes only inside a served slot's window, a peer listens on this
	 * channel only in the slots it shares with us, and not in its last AW,
	 * so the MGNT queue does not spill past the slot and delay the next
	 * sync frames. Nor in the slot's first TUs, where a peer may still be
	 * arriving from another channel (Docs/AWDL_A53_SNIFFER.md §16): the frames
	 * wait for a tick armed at the end of the guard. */
	uint64_t guardEnd = awStart + AWDL_DATA_GUARD_TU * AWDL_TU_US;
	bool inGuard = aw % AWDL_PM == 0 && now + 500 < guardEnd;
	bool lastAw = aw % AWDL_PM == AWDL_PM - 1;
	bool dataWindow = ch && served && _awOpen && _txRingCount && (!lastAw || _txLastAw) &&
	                  !inGuard;
	/* rtlawdl_lastaw: in the last AW the batch ends AWDL_TX_LAST_SLACK_US
	 * before the slot does, so the queue is empty when the next slot's sync
	 * frames are due. */
	uint32_t airUs = _txAirUs;
	if (lastAw) {
		uint64_t slotEnd = awStart + AWDL_AW_TU * AWDL_TU_US;
		uint64_t left = slotEnd > now + AWDL_TX_LAST_SLACK_US ? slotEnd - now - AWDL_TX_LAST_SLACK_US : 0;
		if (left < airUs)
			airUs = (uint32_t)left;
		if (!airUs)
			dataWindow = false;
	}
	bool guardWait = ch && served && _awOpen && _txRingCount && inGuard;
	/* What the pump may do in this AW. The air budget starts over with each
	 * AW and when a window opens; a tick that runs again inside one (the end
	 * of the guard, an early wake for a switch) leaves what is left. */
	bool pumpOk = ch && served && _awOpen && (!lastAw || _txLastAw) && !inGuard && airUs;
	if (pumpOk && (!_pumpOk || aw != _pumpAw || _pumpAir > airUs))
		_pumpAir = airUs;
	_pumpOk = pumpOk;
	_pumpStall = false;
	_pumpAw = aw;
	_pumpSlot = slot;
	_pumpCh = ch;
	_pumpEndUs = lastAw ? awStart + AWDL_AW_TU * AWDL_TU_US - AWDL_TX_LAST_SLACK_US : next;
	/* The 149s go out in a window of ours, where peers listen. */
	bool umiNow = false;
	bool mifNow = false;
	if (ch && served && _awOpen) {
		umiNow = _umiCount != 0;
		mifNow = _mifNow;
		_mifNow = false;
	}
	uint8_t pccDst[8][6], pccOp[8];
	uint8_t nPcc = _pccCount;
	if (nPcc) {
		memcpy(pccDst, _pccDst, sizeof(pccDst));
		memcpy(pccOp, _pccOp, sizeof(pccOp));
		_pccCount = 0;
	}
	IOLockUnlock(_txLock);

	/* Events 43 then 41, outside the lock: postMessage takes the controller's
	 * gate (Docs/AWDL_STAGE3_EVENT_EMITTERS.md §3.1). */
	for (uint8_t i = 0; i < nPcc; i++)
		postPeerCache(pccOp[i], pccDst[i]);
	if (close) {
		bzero(ev43, sizeof(ev43));
		postMessage(AWDL_EV_AW_END, ev43, sizeof(ev43), false);
		_ev43++;
	}
	if (open) {
		postMessage(AWDL_EV_AW_START, ev41, sizeof(ev41), false);
		if (_ev41++ < AWDL_RX_LOG_FIRST)
			LOG("awdl: window aw %u slot %u ch %u", aw, slot, ch);
	}
	if (ch && _tplLen) {
		if (psf && sendAf(AWDL_AF_PSF, ch, mask))
			_txPsf++;
		if ((mif || mifNow) && sendAf(AWDL_AF_MIF, ch, mask))
			_txMif++;
		for (uint8_t i = 0; umiNow && i < 4 && sendAf(AWDL_AF_MIF, ch, mask, true); i++)
			_txUmi++;
		if (psf && (_txPsf & 0x3FF) == 0)
			LOG("awdl: tx psf %llu mif %llu fail %llu windows %llu ch %u, data queued %llu "
			    "sent %llu dropped full %llu old %llu bad %llu fail %llu held %llu busy %llu, mcc %llu fail %llu "
			    "max %llu us avg %llu us early %llu dwell %u, mcast batches %llu",
			    _txPsf, _txMif, _txFail, _ev41, ch, _txDataQueued, _txDataSent,
			    _txDataDropFull, _txDataDropOld, _txDataDropBad, _txDataFail, _txDataHeld, _txDataBusy,
			    _mccSwitches, _mccFail, _mccUsMax, _mccSwitches ? _mccUsSum / _mccSwitches : 0,
			    _mccEarly, _mccDwell, _txMcast);
		/* The ladder's view: with frames sent and no reports the radio is
		 * not reporting, and the rate stays where it is. */
		if (psf && (_txPsf & 0x3FF) == 0 && !_rateOff)
			LOG("awdl: tx rate %u Mb/s%s%s, reports %u (clean %u retried %u/%u lost %u), changes %llu, "
			    "dense %u (%llu changes)", kAwdlRateMbps[_rateIdx], _rateSet ? "" : " (not set)",
			    _rateByRssi ? " by signal" : "",
			    _rateLastReports, _rateLast[0], _rateLast[1], _rateLast[2], _rateLast[3],
			    _rateChanges, _dense, _denseChanges);
		if (psf && (_txPsf & 0x3FF) == 0 && _aggAllowed && !_aggOff)
			LOG("awdl: A-MPDU frames %llu at %u Mb/s, sessions asked %llu on %llu refused %llu "
			    "unanswered %llu closed by peer %llu broken %llu idle %llu, pump runs %llu",
			    _txDataAgg, _aggMbps, _bao.requests, _bao.accepted, _bao.refused, _bao.noAnswer,
			    _bao.delbaIn, _bao.broken, _bao.idle, _txPumpRuns);
	}
	if (dataWindow)
		pumpData();

	IOLockLock(_txLock);
	_tickBusy = false;
	if (_txOn) {
		uint64_t t = awdlNowUs();
		/* The switch to a served slot takes milliseconds (rtlRadioAwdlOffch
		 * runs synchronously); started at the boundary it lands after the
		 * frames peers send at the start of the window. When the next AW opens
		 * a served slot the radio is not on, wake that much earlier. */
		uint64_t wake = guardWait ? guardEnd : next;
		uint16_t nextAw = (uint16_t)(_clkBaseAw + awIdx + 1);
		if (mcc && _mccTarget && _mccCur != _mccTarget && nextAw % AWDL_PM == 0 &&
		    (_mccServe >> ((nextAw / AWDL_PM) % AWDL_SLOTS)) & 1) {
			uint64_t lead = _mccUsMax + 1500;
			if (lead < 3000)
				lead = 3000;
			if (lead > 8000)
				lead = 8000;
			if (next > t + lead) {
				wake = next - lead;
				_mccPreUs = next;
				_mccEarly++;
			}
		}
		uint64_t wait = wake > t ? wake - t : 1000;
		uint64_t deadline;
		clock_interval_to_deadline((uint32_t)wait, kMicrosecondScale, &deadline);
		armTick(deadline);
	}
	IOLockUnlock(_txLock);
}

/*
 * Without an association the radio is parked on a social channel
 * (rtlRadioSetChannel): the one the manager's 129 puts in most slots, or
 * _parkCh before any 129, on a tie and when the 129 has none (the default
 * sequence is channel 6 in slots 0 and 8, Docs/AWDL_SIN_ASOCIACION_INFRA.md
 * §3). Returns the channel to serve, or 0 until the radio sits there. A
 * disconnect leaves it on the old BSS channel, so the request is repeated, at
 * most once a second. While infrastructure is busy no request goes out, since
 * an association may follow, but a radio already there keeps serving: otherwise
 * each scan silences AWDL for 3 s more than it lasts
 * (Docs/AWDL_A53_SNIFFER.md §17). Called from txTick only.
 */
uint8_t RTLAwdInterface::parkRadio(uint8_t idleCh, uint64_t nowUs, bool infraBusy)
{
	if (!_parkCh)
		return 0;
	uint8_t target = _parkCh;
	IOLockLock(_txLock);
	if (_seqValid) {
		static const uint8_t social[3] = { 6, 44, 149 };
		int best = 0;
		for (int c = 0; c < 3; c++) {
			int n = 0;
			for (int i = 0; i < AWDL_SLOTS; i++)
				n += _seq[i] == social[c];
			if (n > best || (n == best && n && social[c] == _parkCh)) {
				best = n;
				target = social[c];
			}
		}
	}
	IOLockUnlock(_txLock);
	if (idleCh == target)
		return idleCh;
	if (infraBusy || (_parkReqUs && nowUs - _parkReqUs < 1000000))
		return 0;
	_parkReqUs = nowUs;
	uint32_t want = target;
	IOReturn r = _ctrl->radio(RTL_RADIO_FN_SET_CHANNEL, &want);
	if (r == kIOReturnUnsupported || r == kIOReturnBadArgument) {
		/* an older driver, or a channel outside the plan: asking again
		 * every second would never succeed */
		LOG("awdl: radio cannot park on channel %u (0x%x), AWDL only on an associated social "
		    "channel", want, r);
		_parkCh = 0;
	} else if (r == kIOReturnSuccess) {
		LOG("awdl: parking the radio on channel %u (was %u)", want, idleCh);
	}
	return 0;
}

/* Slots the radio serves on channel ch: those the 129 puts on ch, or all of
 * them before any 129 arrived. Called with _txLock held. */
uint16_t RTLAwdInterface::servedMask(uint8_t ch) const
{
	if (!ch)
		return 0;
	if (!_seqValid)
		return 0xFFFF;
	uint16_t mask = 0, idle = 0;
	for (int i = 0; i < AWDL_SLOTS; i++) {
		if (_seq[i] == ch)
			mask |= (uint16_t)(1u << i);
		else if (!_seq[i])
			idle |= (uint16_t)(1u << i);
	}
	/* shared with a BSS, _mccServe holds the idle slots a transfer takes */
	if (_dense)
		mask |= idle;
	return _mccActive ? mask & _mccServe : mask;
}
/* Tells the radio the rung's rate; a radio without RTL_RADIO_FN_AWDL_RATE
 * turns the ladder off and data stays at its fixed 12 Mb/s. */
bool RTLAwdInterface::setRate(uint8_t idx)
{
	uint32_t r = kAwdlRate[idx];
	IOReturn e = _ctrl ? _ctrl->radio(RTL_RADIO_FN_AWDL_RATE, &r) : kIOReturnNotReady;
	if (e == kIOReturnUnsupported) {
		_rateOff = true;
		LOG("awdl: the radio has no AWDL data rate control: fixed 12 Mb/s");
		return false;
	}
	if (e != kIOReturnSuccess)
		return false;
	_rateIdx = idx;
	_rateSet = true;
	return true;
}
void RTLAwdInterface::updateRate()
{
	if (_rateOff || !_ctrl)
		return;
	struct rtl_radio_awdl_txstat st;
	if (!_rateSet) {
		/* A new radio (or the first tick): its counters are the new base. */
		if (setRate(_rateIdx) && _ctrl->radio(RTL_RADIO_FN_AWDL_TXSTAT, &st) == kIOReturnSuccess) {
			uint32_t base[4] = { st.clean, st.retried, st.retried2, st.lost };
			memcpy(_rateLast, base, sizeof(base));
			_rateLastReports = st.reports;
		}
		return;
	}
	if (_rateByRssi)
		rateByRssi(awdlNowUs());
	/* Reports come after their frames: poll while frames go out and for a
	 * few ticks after, so the last ones of a burst still count. */
	uint64_t sent = _txDataSent - _rateSentAt;
	if (sent) {
		_rateSentAt = _txDataSent;
		_ratePollLeft = 8;
	} else if (!_ratePollLeft) {
		return;
	} else {
		_ratePollLeft--;
	}
	if (_ctrl->radio(RTL_RADIO_FN_AWDL_TXSTAT, &st) != kIOReturnSuccess)
		return;
	if (st.reports == _rateLastReports) {
		/* A radio that reports nothing leaves the ladder blind. */
		if (!st.reports && !_rateByRssi && (_rateNoRpt += (uint32_t)sent) >= 64) {
			_rateByRssi = true;
			LOG("awdl: no TX report from the radio for %u data frames: rate by the peers' signal",
			    _rateNoRpt);
		}
		return;
	}
	if (_rateByRssi) {
		_rateByRssi = false;
		LOG("awdl: TX reports arriving (%u): rate by the ladder", st.reports);
	}
	uint32_t now[4] = { st.clean, st.retried, st.retried2, st.lost };
	bool back = st.reports < _rateLastReports;
	for (int i = 0; i < 4; i++)
		back |= now[i] < _rateLast[i];
	_rateLastReports = st.reports;
	if (back) {
		/* The radio's counters went back to 0 (a reloaded RTL8814Native): a
		 * new base, no decision on a difference that is not one. */
		memcpy(_rateLast, now, sizeof(now));
		return;
	}
	uint32_t d[4];
	for (int i = 0; i < 4; i++) {
		d[i] = now[i] - _rateLast[i];
		_rateLast[i] = now[i];
	}
	uint32_t clean = d[0];
	/* One retry is usually the peer still off channel; two retries, a lost
	 * frame or several single retries in one tick are the rate. */
	bool fail = d[2] || d[3] || d[1] >= 2;
	if (clean)
		_rateEverClean = true;
	_rateHold = _rateHold > clean ? _rateHold - clean : 0;
	uint8_t idx = _rateIdx;
	/* Before any frame was acknowledged (a peer that never ACKs, like the
	 * virtual one) the ladder stays at 12 Mb/s or above. */
	uint8_t floor = _rateEverClean ? 0 : 1;
	if (fail) {
		_rateCleanRun = 0;
		if (_rateProbe) {
			idx = idx > floor ? idx - 1 : idx;
			_rateHold = 256u << _rateBackoff;
			if (_rateBackoff < 4)
				_rateBackoff++;
			_rateProbe = false;
			_rateFailed = false;
		} else if (_rateFailed) {
			idx = idx > floor ? idx - 1 : idx;
			_rateHold = 256;
			_rateFailed = false;
		} else {
			_rateFailed = true;
		}
		_rateOk = 0;
	} else {
		_rateOk += clean;
		_rateCleanRun += clean;
		if (_rateFailed && _rateCleanRun >= 128)
			_rateFailed = false;
		if (_rateProbe && _rateOk >= 32) {
			/* Confirmed. The next climb needs another 32 clean frames here,
			 * so a failure in between counts as a strike, not a probe. */
			_rateProbe = false;
			_rateOk = 0;
			if (_rateBackoff)
				_rateBackoff--;
		}
		if (!_rateProbe && !_rateFailed && _rateOk >= 32 && !_rateHold && idx < _rateMax) {
			idx++;
			_rateProbe = true;
			_rateOk = 0;
		}
	}
	if (idx == _rateIdx)
		return;
	uint8_t was = _rateIdx;
	if (setRate(idx) && _rateChanges++ < 64)
		LOG("awdl: tx rate %u -> %u Mb/s (reports clean %u retried %u/%u lost %u, last final "
		    "rate code 0x%x)", kAwdlRateMbps[was], kAwdlRateMbps[idx], st.clean, st.retried,
		    st.retried2, st.lost, st.rate);
}
/*
 * The rate from the peers' signal, for a radio that sends no TX reports. Each
 * rung has the weakest smoothed RSSI it is used at, about 10 dB over the
 * sensitivity of an OFDM receiver at that rate; going up takes 3 dB more and
 * at most one change per second, going down is immediate. The hardware's rate
 * fallback on retries covers a frame the rung was too fast for. With no peer
 * heard for 5 s the rate is 12 Mb/s. From txTick only.
 */
void RTLAwdInterface::rateByRssi(uint64_t now)
{
	static const int8_t kMin[7] = { -128, -128, -70, -65, -60, -55, -50 };
	IOLockLock(_txLock);
	bool fresh = _peerRssiUs && now - _peerRssiUs < 5000000;
	int rssi = _peerRssi16 / 16;
	IOLockUnlock(_txLock);
	uint8_t up = 1, down = 1;
	for (uint8_t i = 2; fresh && i <= _rateMax; i++) {
		if (rssi >= kMin[i])
			down = i;
		if (rssi >= kMin[i] + 3)
			up = i;
	}
	uint8_t idx = _rateIdx;
	if (down < idx)
		idx = down;
	else if (up > idx && now - _rateRssiUs >= 1000000)
		idx = up;
	if (idx == _rateIdx)
		return;
	uint8_t was = _rateIdx;
	_rateRssiUs = now;
	if (setRate(idx) && _rateChanges++ < 64)
		LOG("awdl: tx rate %u -> %u Mb/s by signal (peers at %d dBm%s)", kAwdlRateMbps[was],
		    kAwdlRateMbps[idx], rssi, fresh ? "" : ", none heard");
}
/*
 * The rate of the Block Ack sessions' A-MPDUs, VHT with two streams: its TX
 * descriptor index, Mb/s at 20 and at 80 MHz with the long guard interval, and
 * the weakest smoothed RSSI it is used at on 20 MHz. A wider channel takes 3 dB
 * more per doubling. The floors sit a few dB over a receiver's sensitivity at
 * each rate, since the radio reports nothing to correct them by; its own
 * fallback on retries covers a frame the rate was too fast for.
 */
static const struct {
	uint8_t desc;
	uint16_t mbps20, mbps80;
	int8_t min20;
} kAwdlAgg[8] = {
	{ 0x36, 13, 58, -128 },  { 0x37, 26, 117, -76 },  { 0x38, 39, 175, -73 },  { 0x39, 52, 234, -70 },
	{ 0x3A, 78, 351, -66 },  { 0x3B, 104, 468, -62 }, { 0x3D, 130, 585, -58 }, { 0x3E, 156, 702, -54 },
};

/*
 * Keeps the radio's A-MPDU rate current and watches the sessions. The rate
 * follows the peers' signal as rateByRssi does: down at once, up with 3 dB to
 * spare, and it is given to the radio again every second, whose answer tells
 * the width the radio is tuned to (the park width may change under it). With
 * no peer heard for 5 s it is the slowest. A session that carried data out
 * while nothing came back is closed with a DELBA. From txTick only.
 */
void RTLAwdInterface::aggRate(uint64_t now)
{
	if (!_aggAllowed || _aggOff || !_ctrl)
		return;
	if (now - _aggWatchUs >= 250000) {
		_aggWatchUs = now;
		uint8_t mac[6];
		IOLockLock(_txLock);
		int act = awdlBaoWatch(&_bao, now, mac);
		IOLockUnlock(_txLock);
		if (act == AWDL_BAO_DO_DELBA) {
			uint8_t d[AWDL_BAO_DELBA_LEN];
			awdlBaoBuildDelba(d, mac, _mac, kAwdlBssid, 39);
			queueMgmt(d, sizeof(d));
			LOG("awdl: Block Ack session to %02x:%02x:%02x:%02x:%02x:%02x carried data and nothing "
			    "came back: closed, single frames for %u s", mac[0], mac[1], mac[2], mac[3], mac[4],
			    mac[5], (unsigned)(AWDL_BAO_BACKOFF_US / 1000000));
		}
	}
	if (_aggSet && now - _aggUs < 1000000)
		return;
	IOLockLock(_txLock);
	bool fresh = _peerRssiUs && now - _peerRssiUs < 5000000;
	int rssi = _peerRssi16 / 16;
	IOLockUnlock(_txLock);
	int wide = _aggBw == RTL_RADIO_BW_80 ? 6 : _aggBw == RTL_RADIO_BW_40 ? 3 : 0;
	uint8_t up = 0, down = 0;
	for (uint8_t i = 1; fresh && i < 8; i++) {
		if (rssi >= kAwdlAgg[i].min20 + wide)
			down = i;
		if (rssi >= kAwdlAgg[i].min20 + wide + 3)
			up = i;
	}
	uint8_t idx = _aggSet ? _aggIdx : down;
	if (down < idx)
		idx = down;
	else if (up > idx)
		idx = up;
	struct rtl_radio_awdl_agg c;
	bzero(&c, sizeof(c));
	c.rate = kAwdlAgg[idx].desc;
	c.bw = _aggBwMax;
	c.max_agg = _aggMax;
	c.density = _aggDensity;
	IOReturn e = _ctrl->radio(RTL_RADIO_FN_AWDL_AGG, &c);
	if (e == kIOReturnUnsupported) {
		_aggOff = true;
		LOG("awdl: the radio has no A-MPDU for AWDL data: single frames only");
		return;
	}
	if (e != kIOReturnSuccess)
		return;
	uint8_t bw = c.radio_bw < _aggBwMax ? (uint8_t)c.radio_bw : (uint8_t)_aggBwMax;
	/* 40 MHz carries 27/13 of 20 MHz */
	uint32_t mbps = bw == RTL_RADIO_BW_80 ? kAwdlAgg[idx].mbps80
	                : bw == RTL_RADIO_BW_40 ? kAwdlAgg[idx].mbps20 * 27u / 13u : kAwdlAgg[idx].mbps20;
	bool changed = !_aggSet || idx != _aggIdx || bw != _aggBw;
	IOLockLock(_txLock);
	_aggMbps = mbps;
	IOLockUnlock(_txLock);
	_aggIdx = idx;
	_aggBw = bw;
	_aggUs = now;
	_aggSet = true;
	if (changed && _aggChanges++ < 64)
		LOG("awdl: A-MPDU rate VHT 2SS MCS %u, %u Mb/s at %u MHz (peers at %d dBm%s)",
		    kAwdlAgg[idx].desc - 0x36, mbps, bw == RTL_RADIO_BW_80 ? 80 : bw == RTL_RADIO_BW_40 ? 40 : 20,
		    rssi, fresh ? "" : ", none heard");
}

/* Samples the data frames every 250 ms; 10 or more in a sample is busy. Two
 * busy samples among the last four (1 s) are a transfer and turn dense
 * presence on; once on, every busy sample keeps it, until 2 s after the last
 * one. A single burst, such as a TLS retry of a few frames (2026-11-09: about
 * 16 frames every few seconds toggled it 595 times in 20 minutes), does not
 * turn it on. Not two in a row: with dense presence off, data moves only in
 * the served slots, 2 and 10 of the 129, 524 ms apart, so the busy samples
 * of a transfer come every second sample or so (2026-11-09: a UDP stream at
 * 200 frames/s never turned it on). _denseRun holds one bit per sample, the
 * newest in bit 0. True when it turned on or off. Under _txLock, from txTick. */
bool RTLAwdInterface::updateDense(uint64_t now, bool off)
{
	if (!_denseAllowed)
		return false;
	uint64_t frames = _rxData + _txDataSent;
	if (now - _denseSampleUs >= 250000) {
		bool busy = frames - _denseFrames >= 10;
		_denseRun = (uint8_t)(((_denseRun << 1) | busy) & 0xF);
		uint8_t n = (_denseRun & 1) + (_denseRun >> 1 & 1) + (_denseRun >> 2 & 1) + (_denseRun >> 3 & 1);
		if (busy && (_dense || n >= 2))
			_denseBusyUs = now;
		_denseFrames = frames;
		_denseSampleUs = now;
	}
	bool want = !off && _denseBusyUs && now - _denseBusyUs < 2000000;
	if (want == _dense)
		return false;
	_dense = want;
	_denseChanges++;
	return true;
}

/*
 * While associated on a non-social channel (Docs/AWDL_MCC_TIEMPO_COMPARTIDO.md
 * §7): the social channel of the BSS's band that the 129 puts in most slots is
 * served in the first slot of each of its runs (rtlawdl_mcc=1) or in all of
 * them (2), and the
 * radio goes there for those slots with rtlRadioAwdlOffch; in the others it
 * stays on the BSS and AWDL is idle. With no data, UMI or MIF waiting and no
 * peer heard for AWDL_MCC_PEER_HOLD_US it comes back after the slot's first AW,
 * where peers are sure to listen; otherwise it stays the whole slot, because
 * the 16 TU of one AW miss a peer's frames for long enough that the family
 * drops it (6 s without an action frame, shouldRemovePeerFromNDP). A whole slot
 * is 65.5 ms off the BSS, the latency the infra link sees in each served slot
 * (Docs/AWDL_MCC_TIEMPO_COMPARTIDO.md). rtlawdl_mccaw fixes the AWs instead.
 * Returns the channel the radio sits on for AWDL now, or 0. Called from txTick
 * only.
 */
uint8_t RTLAwdInterface::mccChannel(uint8_t home, uint64_t nowUs)
{
	static const uint8_t social[3] = { 6, 44, 149 };
	IOLockLock(_txLock);
	uint8_t target = 0;
	uint16_t serve = 0;
	if (_seqValid) {
		int best = 0;
		for (int c = 0; c < 3; c++) {
			int n = 0;
			for (int i = 0; i < AWDL_SLOTS; i++)
				n += _seq[i] == social[c];
			/* only the BSS's band: a band switch is a full retune, not measured */
			if ((social[c] > 14) == (home > 14) && n > best) {
				best = n;
				target = social[c];
			}
		}
		for (int i = 0; target && i < AWDL_SLOTS; i++)
			if (_seq[i] == target &&
			    (_mccMode >= 2 || _seq[(i + AWDL_SLOTS - 1) % AWDL_SLOTS] != target))
				serve |= (uint16_t)(1u << i);
		/* A transfer (dense presence) takes the channel's slots and the idle
		 * ones in runs of _mccRun, a slot for the BSS after each (AWDLShare.h). */
		if (target && _dense && _mccRun) {
			uint16_t more = awdlShareServe(_seq, target, _mccRun);
			serve = _mccMode >= 2 ? serve | more : more;
		}
	}
	/* the slot txTick is about to run, with the same slack */
	uint64_t awIdx = (nowUs - _clkBaseUs + 500) / (AWDL_AW_TU * AWDL_TU_US);
	uint16_t aw = (uint16_t)(_clkBaseAw + awIdx);
	uint32_t slot = (aw / AWDL_PM) % AWDL_SLOTS;
	/* only while still there: a frame queued after the return waits for the next slot */
	_mccDwell = _mccAw ? _mccAw :
	            _mccPeerUs && nowUs < _mccPeerUs + AWDL_MCC_PEER_HOLD_US ? AWDL_PM : 1;
	bool stay = aw % AWDL_PM < _mccDwell ||
	            (_mccCur == target && (_txRingCount || _umiCount || _mifNow));
	_mccServe = serve;
	_mccTarget = target;
	IOLockUnlock(_txLock);
	uint8_t want = (serve >> slot) & 1 && stay ? target : 0;
	if (!mccSwitch(want))
		return 0;
	return want;
}

/*
 * Moves the radio to want, or back to the BSS with 0, unless it is there
 * already. A leave is refused once tx is off: setRadioTx(false) switches back
 * under the same lock after clearing _txOn. Returns whether the radio is
 * where asked.
 */
bool RTLAwdInterface::mccSwitch(uint8_t want)
{
	if (!_mccLock)
		return false;
	IOLockLock(_mccLock);
	IOLockLock(_txLock);
	bool on = _txOn;
	IOLockUnlock(_txLock);
	bool ok = want == _mccCur;
	if (!ok && (on || !want) && !_mccOff) {
		uint32_t c = want;
		uint64_t t0 = awdlNowUs();
		IOReturn r = _ctrl->radio(RTL_RADIO_FN_AWDL_OFFCH, &c);
		uint64_t dt = awdlNowUs() - t0;
		if (r == kIOReturnSuccess) {
			_mccCur = want;
			ok = true;
			if (_mccSwitches++ < 4)
				LOG("awdl: radio to channel %u (0 = the BSS) took %llu us", want, dt);
			_mccUsSum += dt;
			if (dt > _mccUsMax)
				_mccUsMax = dt;
		} else if (r == kIOReturnUnsupported) {
			_mccOff = true;
			LOG("awdl: the radio has no rtlRadioAwdlOffch: no AWDL while associated off the "
			    "social channels");
		} else {
			if (_mccFail++ < 4)
				LOG("awdl: rtlRadioAwdlOffch %u refused 0x%x", want, r);
			/* a refused return leaves nothing to undo on our side */
			if (!want)
				_mccCur = 0;
		}
	}
	IOLockUnlock(_mccLock);
	return ok && want;
}

bool RTLAwdInterface::sendAf(uint8_t subtype, uint8_t channel, uint16_t mask, bool umi)
{
	size_t len = buildAf(_afBuf, sizeof(_afBuf), subtype, channel, mask, umi);
	if (!len)
		return false;
	uint32_t l = (uint32_t)len;
	IOReturn r = _ctrl->radioTx80211(_afBuf, &l);
	if (r != kIOReturnSuccess) {
		if (_txFail++ < AWDL_RX_LOG_FIRST)
			LOG("awdl: tx %s -> 0x%x", umi ? "umi" : subtype == AWDL_AF_MIF ? "mif" : "psf", r);
		return false;
	}
	return true;
}

/*
 * The family's peerToPeerConfigureIfnet, run inside this call, sets the ifnet's
 * MTU to 1500 on a Broadcom controller and to 1484 on any other, over the value
 * in the RegInfo. A larger packet than the TX ring holds would vanish in
 * queueData with no error to the stack (Docs/AWDL_A53_SNIFFER.md §14).
 */
uintptr_t RTLAwdInterface::prepareBSDInterface(__ifnet *ifp, unsigned int unit)
{
	uintptr_t r = IO80211VirtualInterface::prepareBSDInterface(ifp, unit);
	u_int32_t mtu = (u_int32_t)(sizeof(((AwdlTxFrame *)nullptr)->frame) - AWDL_WLAN_HDR - 16);
	errno_t e = ifnet_set_mtu((ifnet_t)ifp, mtu);
	LOG("awdl: prepareBSDInterface 0x%lx, MTU %u -> %d", (unsigned long)r, mtu, e);
	return r;
}

/*
 * One Ethernet II frame from awdl0 into the TX ring as an AWDL data frame
 * (Docs/AWDL_RX_DATA_TO_SKYWALK.md §2, Docs/AWDL_DATA_ENCAP.md §7): data
 * without QoS as owly sends it, A1 = DA, A2 = SA, A3 = the AWDL BSSID, then
 * the Apple SNAP, 03 04, a per-destination sequence number (low 12 bits, no
 * AC nibble), 00 00, the ethertype and the payload. The radio numbers the
 * 802.11 header. A full ring drops its oldest frame.
 */
void RTLAwdInterface::queueData(const uint8_t *eth, uint32_t len)
{
	static const uint8_t kSnap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x17, 0xF2, 0x08, 0x00 };
	if (len < 14 || len - 14 > sizeof(_txRing->frame) - AWDL_WLAN_HDR - 16 ||
	    memcmp(eth + 6, _mac, 6) || eth[12] < 0x06) {
		_txDataDropBad++;
		return;
	}
	uint64_t now = awdlNowUs();
	IOLockLock(_txLock);
	if (!_txOn || _txDataOff || !_txRing) {
		IOLockUnlock(_txLock);
		return;
	}
	/* Unicast needs port 1: it acknowledges the peer's frames, and the MAC
	 * matches the ACKs to ours against it (Docs/AWDL_A53_SNIFFER.md §13). */
	if (!(eth[0] & 1) && !_port1On) {
		IOLockUnlock(_txLock);
		_txDataDropBad++;
		return;
	}
	/* Sequence number of this destination; the least recently used entry
	 * makes room for a new one, which starts at 0. */
	AwdlTxSeq *e = nullptr, *lru = &_txSeqTab[0];
	for (uint32_t i = 0; i < 16 && !e; i++) {
		if (_txSeqTab[i].lastUs && !memcmp(_txSeqTab[i].da, eth, 6))
			e = &_txSeqTab[i];
		else if (_txSeqTab[i].lastUs < lru->lastUs)
			lru = &_txSeqTab[i];
	}
	if (!e) {
		e = lru;
		memcpy(e->da, eth, 6);
		/* not 0: a destination evicted and seen again does not restart */
		e->seq = (uint16_t)(now >> 4);
	}
	uint16_t seq = e->seq & 0xFFF;
	e->seq = (uint16_t)(seq + 1);
	e->lastUs = now;

	if (_txRingCount == AWDL_TX_RING) {
		/* The oldest data frame makes room. A management frame at the head
		 * (queueMgmt) stays and the new frame is the one lost: under a flood
		 * every packet evicted the ADDBA request before the pump took it
		 * (2026-11-11: three requests, none reached the peer). */
		if ((_txRing[_txRingHead].frame[0] & 0x0C) != 0x08) {
			_txDataDropFull++;
			IOLockUnlock(_txLock);
			return;
		}
		_txRingHead = (_txRingHead + 1) % AWDL_TX_RING;
		_txRingCount--;
		_txDataDropFull++;
	}
	AwdlTxFrame *f = &_txRing[(_txRingHead + _txRingCount) % AWDL_TX_RING];
	uint8_t *p = f->frame;
	bzero(p, AWDL_WLAN_HDR);
	p[0] = 0x08;
	memcpy(p + 4, eth, 6);
	memcpy(p + 10, eth + 6, 6);
	memcpy(p + 16, kAwdlBssid, 6);
	p += AWDL_WLAN_HDR;
	memcpy(p, kSnap, sizeof(kSnap));
	p[8] = 0x03;
	p[9] = 0x04;
	putLe16(p + 10, seq);
	p[12] = p[13] = 0;
	p[14] = eth[12];
	p[15] = eth[13];
	memcpy(p + 16, eth + 14, len - 14);
	f->len = (uint16_t)(AWDL_WLAN_HDR + 16 + len - 14);
	f->tsUs = (uint32_t)now;
	_txRingCount++;
	_txDataQueued++;
	/* A run of unicast data to one peer asks it for a Block Ack session. */
	uint8_t req[AWDL_BAO_REQUEST_LEN];
	AwdlBaoPeer *bp = nullptr;
	bool ask = _aggAllowed && _aggSet && !(eth[0] & 1) &&
	           awdlBaoQueued(&_bao, eth, now, &bp) == AWDL_BAO_DO_REQUEST;
	uint64_t asked = _bao.requests;
	if (ask)
		awdlBaoBuildRequest(req, bp, _mac, kAwdlBssid);
	/* Inside a window the frame need not wait for the next AW's tick. */
	if (_pumpOk && !_sendBusy && !_pumpStall)
		armPump(0);
	IOLockUnlock(_txLock);
	if (ask) {
		bool queued = queueMgmt(req, sizeof(req));
		if (asked <= AWDL_RX_LOG_FIRST)
			LOG("awdl: ADDBA request to %02x:%02x:%02x:%02x:%02x:%02x token %u ssn %u%s", eth[0],
			    eth[1], eth[2], eth[3], eth[4], eth[5], req[AWDL_BAO_HDR + 2],
			    getLe16(req + AWDL_BAO_HDR + 7) >> 4, queued ? "" : " (not queued)");
	}
}

void RTLAwdInterface::pumpEntry(thread_call_param_t p0, thread_call_param_t)
{
	RTLAwdInterface *me = (RTLAwdInterface *)p0;
	me->pumpData();
	/* the reference armPump took for this call */
	me->release();
}

/* With _txLock held. The pending call owns a reference until pumpEntry ends. */
void RTLAwdInterface::armPump(uint64_t deadline)
{
	if (!_txPump || !_pumpAllowed)
		return;
	retain();
	if (deadline ? thread_call_enter_delayed(_txPump, deadline) : thread_call_enter(_txPump))
		release();
}

/*
 * Sends queued frames while the window txTick described is open (_pumpOk, to
 * _pumpEndUs), one at a time with _txLock dropped around the radio call. The AW
 * has a budget of air time rather than of frames: a TCP ACK costs a fraction
 * of a full frame, and counting them alike limited the far end's throughput as
 * well. A frame of a Block Ack session goes as QoS data with the session's
 * next sequence number, taken only once the radio has the frame, and costs the
 * air of its share of an A-MPDU at the session rate. A transient refusal puts
 * the frame back at the head and the pump tries again shortly;
 * kIOReturnBadArgument means a driver without AWDL data TX, which turns data TX
 * off. A frame whose peer announced another channel for this slot ends the run
 * and stays at the head: with no ACK it would use all its tries, 7 times the
 * air the budget counts (Docs/AWDL_MODO_DATOS.md §9). From txTick and from the
 * pump's thread call; a second caller leaves at once.
 */
void RTLAwdInterface::pumpData()
{
	AwdlTxFrame f;
	IOLockLock(_txLock);
	if (_sendBusy || !_pumpOk || !_txOn) {
		IOLockUnlock(_txLock);
		return;
	}
	_sendBusy = true;
	uint32_t slot = _pumpSlot;
	uint8_t ch = _pumpCh;
	uint64_t end = _pumpEndUs;
	_txPumpRuns++;
	bool again = false, off = false;
	/* The lock is held at the top of each pass and when the loop ends. */
	for (;;) {
		uint64_t now = awdlNowUs();
		if (!_txOn || !_txRingCount || !_txRing || !_pumpOk || _pumpEndUs != end || now >= end)
			break;
		AwdlTxFrame *h = &_txRing[_txRingHead];
		if (!peerOnChannel(h->frame + 4, slot, ch, now)) {
			_txDataHeld++;
			_pumpStall = true;
			break;
		}
		bool data = (h->frame[0] & 0x0C) == 0x08 && !(h->frame[4] & 1);
		AwdlBaoPeer *bp = data && _aggSet ? awdlBaoSend(&_bao, h->frame + 4) : nullptr;
		/* air time at the session rate, or the ladder's (12 Mb/s without it) */
		uint32_t mbps = bp && _aggMbps ? _aggMbps : _rateOff || !_rateSet ? 12 : kAwdlRateMbps[_rateIdx];
		uint32_t cost = (bp ? AWDL_TX_AGG_FRAME_US : AWDL_TX_FRAME_US) + h->len * 8 / mbps;
		if (cost > _pumpAir) {
			_pumpStall = true;
			break;
		}
		memcpy(&f, h, offsetof(AwdlTxFrame, frame) + h->len);
		_txRingHead = (_txRingHead + 1) % AWDL_TX_RING;
		_txRingCount--;
		_pumpAir -= cost;
		uint16_t seq = bp ? bp->seq : 0;
		IOLockUnlock(_txLock);
		if ((uint32_t)awdlNowUs() - f.tsUs > AWDL_TX_AGE_US) {
			_txDataDropOld++;
			IOLockLock(_txLock);
			_pumpAir += cost;
			continue;
		}
		const uint8_t *out = f.frame;
		uint32_t l = f.len;
		if (bp) {
			size_t n = awdlBaoQos(f.frame, f.len, seq, _txQos, sizeof(_txQos));
			if (n) {
				out = _txQos;
				l = (uint32_t)n;
			} else {
				bp = nullptr;
			}
		}
		IOReturn r = _ctrl->radioTx80211(out, &l);
		if (r == kIOReturnSuccess) {
			if (_txDataSent++ == 0)
				LOG("awdl: first data frame to %02x:%02x:%02x:%02x:%02x:%02x ethertype %02x%02x len %u",
				    f.frame[4], f.frame[5], f.frame[6], f.frame[7], f.frame[8], f.frame[9],
				    f.frame[AWDL_WLAN_HDR + 14], f.frame[AWDL_WLAN_HDR + 15], f.len);
			IOLockLock(_txLock);
			/* The number is used up only if the session is still the one it came from. */
			if (bp) {
				if (bp->state == AWDL_BAO_ACTIVE && bp->seq == seq && !memcmp(bp->mac, f.frame + 4, 6))
					awdlBaoSent(bp, now);
				_txDataAgg++;
			}
			continue;
		}
		if (r == kIOReturnBadArgument) {
			LOG("awdl: radio refused a data frame: AWDL data TX off (driver without it?)");
			IOLockLock(_txLock);
			_txDataOff = true;
			off = true;
			break;
		}
		IOLockLock(_txLock);
		_pumpAir += cost;
		if (r == kIOReturnBusy || r == kIOReturnNoResources || r == kIOReturnNotReady) {
			if (_txOn && _txRingCount < AWDL_TX_RING) {
				_txRingHead = (_txRingHead + AWDL_TX_RING - 1) % AWDL_TX_RING;
				memcpy(&_txRing[_txRingHead], &f, offsetof(AwdlTxFrame, frame) + f.len);
				_txRingCount++;
			} else {
				_txDataDropFull++;
			}
			_txDataBusy++;
			again = true;
			break;
		}
		if (_txDataFail++ < AWDL_RX_LOG_FIRST)
			LOG("awdl: tx data -> 0x%x", r);
	}
	_sendBusy = false;
	if (_txOn && _txRingCount && _pumpOk && !off) {
		uint64_t now = awdlNowUs();
		if (_pumpEndUs != end) {
			/* txTick opened the next AW while this run was sending */
			armPump(0);
		} else if (again && now + AWDL_PUMP_RETRY_US < end) {
			uint64_t deadline;
			clock_interval_to_deadline(AWDL_PUMP_RETRY_US, kMicrosecondScale, &deadline);
			armPump(deadline);
		}
	}
	IOLockUnlock(_txLock);
	if (off)
		flushRadioTx();
}

void RTLAwdInterface::flushRadioTx()
{
	if (!_txLock)
		return;
	IOLockLock(_txLock);
	_txDataDropOld += _txRingCount;
	_txRingHead = _txRingCount = 0;
	IOLockUnlock(_txLock);
}

size_t RTLAwdInterface::buildAf(uint8_t *out, size_t cap, uint8_t subtype, uint8_t channel,
                                uint16_t mask, bool umi)
{
	IOLockLock(_txLock);
	/* Read under the lock: alignClock may have moved _clkBaseUs past an
	 * earlier sample, and nowUs - _clkBaseUs would wrap. */
	uint64_t nowUs = awdlNowUs();
	/* A UMI takes the next one waiting, whose slot stays intact until the lock
	 * drops. A MIF carries the MI template once the manager sent one: it holds
	 * the ARPA and service TLVs the sync template lacks. */
	if (umi && !_umiCount) {
		IOLockUnlock(_txLock);
		return 0;
	}
	const uint8_t *da = umi ? _umi[_umiCount - 1].dst : nullptr;
	bool mi = subtype == AWDL_AF_MIF && _miTplLen;
	const uint8_t *tpl = umi ? _umi[_umiCount - 1].tpl : mi ? _miTpl : _tpl;
	uint32_t tplLen = umi ? _umi[_umiCount - 1].len : mi ? _miTplLen : _tplLen;
	if (umi)
		_umiCount--;
	size_t need = AWDL_WLAN_HDR + 16 + (3 + AWDL_SYNC_LEN + AWDL_CHANSEQ_LEN + 2) + (3 + 21) +
	              (3 + AWDL_CHANSEQ_LEN + 3) + tplLen;
	if (!tplLen || need > cap) {
		IOLockUnlock(_txLock);
		return 0;
	}
	uint64_t tu = (nowUs - _clkBaseUs) / AWDL_TU_US;
	uint16_t aw = (uint16_t)(_clkBaseAw + tu / AWDL_AW_TU);
	uint16_t toSlot = (uint16_t)(AWDL_SLOT_TU - tu % AWDL_SLOT_TU);
	/* What is left of the slot's window, its first AW; 0 in the other three,
	 * as owly (tlv.rs) and the iOS 26 PSFs send it. */
	uint16_t inSlot = (uint16_t)(tu % AWDL_SLOT_TU);
	uint16_t awLeft = inSlot < AWDL_AW_TU ? (uint16_t)(AWDL_AW_TU - inSlot) : 0;

	/* Election v2 of the template: top master, distance and metrics. */
	const uint8_t *e2 = findTlv(_tpl, _tplLen, AWDL_TLV_ELECTION_V2, 28);
	const uint8_t *master = e2 ? e2 + 3 : _mac;

	uint8_t *p = out;
	bzero(out, need);
	/* 802.11: action, broadcast or to the UMI's peer, from awdl0, BSSID of
	 * AWDL; the radio numbers it. */
	p[0] = 0xD0;
	if (da)
		memcpy(p + 4, da, 6);
	else
		memset(p + 4, 0xFF, 6);
	memcpy(p + 10, _mac, 6);
	memcpy(p + 16, kAwdlBssid, 6);
	p += AWDL_WLAN_HDR;

	/* AWDL fixed header (Docs/AWDL_FRAME_WIRE_FORMAT.md §2) */
	p[0] = 0x7F;
	p[1] = 0x00; p[2] = 0x17; p[3] = 0xF2;
	p[4] = 0x08;
	p[5] = AWDL_AF_VERSION;
	p[6] = subtype;
	putLe32(p + 8, (uint32_t)nowUs);
	putLe32(p + 12, (uint32_t)nowUs);
	p += 16;

	/* 4: sync params (Docs/AWDL_TLV_BODIES.md §1.2, value offsets) */
	p[0] = AWDL_TLV_SYNC;
	putLe16(p + 1, AWDL_SYNC_LEN + AWDL_CHANSEQ_LEN + 2);
	uint8_t *v = p + 3;
	v[0] = channel;                 /* next AW channel */
	putLe16(v + 1, toSlot);         /* TU to the next slot boundary */
	v[3] = channel;                 /* master channel */
	v[4] = 0;                       /* guard time */
	putLe16(v + 5, AWDL_AW_TU);
	putLe16(v + 7, AWDL_PSF_TU);
	putLe16(v + 9, 0x1800);
	putLe16(v + 11, AWDL_AW_TU);    /* extended AW length */
	putLe16(v + 13, AWDL_AW_TU);    /* common AW length */
	putLe16(v + 15, awLeft);
	v[17] = v[18] = v[19] = v[20] = 3;
	memcpy(v + 21, master, 6);
	v[27] = AWDL_PM;
	v[28] = 0;
	putLe16(v + 29, aw);
	putLe16(v + 31, 0);             /* AP alignment */
	putChanSeq(v + AWDL_SYNC_LEN, channel, mask);
	p += 3 + AWDL_SYNC_LEN + AWDL_CHANSEQ_LEN + 2;

	/* 5: election v1 from v2 (Docs/AWDL_TLV5_ELECTION.md) */
	p[0] = AWDL_TLV_ELECTION;
	putLe16(p + 1, 21);
	v = p + 3;
	if (e2) {
		uint32_t dist = *(const uint32_t *)(e2 + 3 + 16);
		v[3] = dist > 255 ? 255 : (uint8_t)dist;                 /* distance, saturated as owly */
		memcpy(v + 5, e2 + 3, 6);                               /* top master */
		memcpy(v + 11, e2 + 3 + 20, 4);                         /* top master metric */
		memcpy(v + 15, e2 + 3 + 24, 4);                         /* own metric */
	} else {
		memcpy(v + 5, _mac, 6);
	}
	p += 3 + 21;

	/* 18: channel sequence, 3 bytes of padding */
	p[0] = AWDL_TLV_CHANSEQ;
	putLe16(p + 1, AWDL_CHANSEQ_LEN + 3);
	putChanSeq(p + 3, channel, mask);
	p += 3 + AWDL_CHANSEQ_LEN + 3;

	memcpy(p, tpl, tplLen);
	p += tplLen;
	IOLockUnlock(_txLock);
	return (size_t)(p - out);
}

/*
 * Takes the slot boundary and AW count of a node that announces itself as
 * master in its TLV 4 (master address == source). Only one master is
 * followed: the sync master of the manager's election v2 in the template when
 * it names another node, else the first one taken while it is heard within
 * AWDL_CLK_HOLD_US, so two self-masters in range do not make the clock jump
 * between their schedules. m holds the RX prefix, the 802.11 header and the
 * action frame.
 */
#define AWDL_CLK_HOLD_US      2000000
void RTLAwdInterface::alignClock(mbuf_t m, const uint8_t *src)
{
	uint8_t b[16 + 3 + AWDL_SYNC_LEN + 64];
	size_t len = mbuf_pkthdr_len(m);
	size_t off = AWDL_RX_META + AWDL_WLAN_HDR;
	if (len < off + 16 + 3 + AWDL_SYNC_LEN || !memcmp(src, _mac, 6))
		return;
	size_t n = len - off < sizeof(b) ? len - off : sizeof(b);
	if (mbuf_copydata(m, off, n, b) != 0)
		return;
	const uint8_t *t = findTlv(b + 16, n - 16, AWDL_TLV_SYNC, AWDL_SYNC_LEN);
	if (!t || memcmp(t + 3 + 21, src, 6))
		return;
	uint16_t toSlot = getLe16(t + 3 + 1);
	uint16_t aw = getLe16(t + 3 + 29);
	if (!toSlot || toSlot > AWDL_SLOT_TU)
		return;
	uint64_t now = awdlNowUs();
	/* The master computed toSlot when it built the frame (target TX time,
	 * +12) and queued it later (PHY TX time, +8): the next slot starts
	 * toSlot TU after arrival minus that delay (Stute et al. 2018, eq. 1;
	 * the kernel keeps the same difference as sync_contention). */
	uint64_t rxUs;
	if (mbuf_copydata(m, AWDL_RX_META_TIME, sizeof(rxUs), &rxUs) != 0 || rxUs > now)
		rxUs = now;
	uint32_t phyUs = getLe32(b + 8), targetUs = getLe32(b + 12);
	uint32_t delay = phyUs - targetUs;
	if (phyUs <= targetUs || delay >= AWDL_AW_TU * AWDL_TU_US)
		delay = 0;
	uint64_t at = rxUs - delay;
	IOLockLock(_txLock);
	const uint8_t *e2 = findTlv(_tpl, _tplLen, AWDL_TLV_ELECTION_V2, 28);
	const uint8_t *elected = e2 && memcmp(e2 + 3 + 6, _mac, 6) ? e2 + 3 + 6 : nullptr;
	bool held = _clkMasterUs && now - _clkMasterUs < AWDL_CLK_HOLD_US;
	if (elected ? memcmp(src, elected, 6) != 0 : held && memcmp(src, _clkMaster, 6) != 0) {
		IOLockUnlock(_txLock);
		return;
	}
	memcpy(_clkMaster, src, 6);
	_clkMasterUs = now;
	/* The slot now running started SLOT_TU - toSlot TU ago; aw is the AW
	 * now, so that slot began (SLOT_TU - toSlot) / AW_TU AWs earlier. */
	uint32_t into = AWDL_SLOT_TU - toSlot;
	_clkBaseUs = at - (uint64_t)into * AWDL_TU_US;
	_clkBaseAw = (uint16_t)(aw - into / AWDL_AW_TU);
	_clkAligned++;
	IOLockUnlock(_txLock);
	if (_clkAligned <= 4)
		LOG("awdl: clock follows %02x:%02x:%02x:%02x:%02x:%02x aw %u next slot %u TU, "
		    "drain %llu us, sender delay %u us", src[0], src[1], src[2], src[3], src[4],
		    src[5], aw, toSlot, now - rxUs, delay);
}

/*
 * Keeps the channel sequence (TLV 18) a peer announces in its action frames.
 * Only when the AW number in its TLV 4 lands on our slot, give or take one,
 * since otherwise its slot numbers say nothing about ours. Encodings: 0 is the
 * channel alone, 1 is flags then channel, 3 is channel then operating class.
 * m holds the RX prefix, the 802.11 header and the action frame.
 */
void RTLAwdInterface::notePeerSeq(mbuf_t m, const uint8_t *src)
{
	if (!memcmp(src, _mac, 6))
		return;
	uint8_t seq[AWDL_CHANSEQ_LEN], sync[AWDL_SYNC_LEN];
	bool haveSeq = false, haveSync = false;
	size_t len = mbuf_pkthdr_len(m);
	size_t off = AWDL_RX_META + AWDL_WLAN_HDR + 16;
	while (off + 3 <= len && !(haveSeq && haveSync)) {
		uint8_t h[3];
		if (mbuf_copydata(m, off, sizeof(h), h) != 0)
			return;
		uint16_t l = getLe16(h + 1);
		if (off + 3 + l > len)
			return;
		if (h[0] == AWDL_TLV_CHANSEQ && l >= 6 + AWDL_SLOTS) {
			bzero(seq, sizeof(seq));
			if (mbuf_copydata(m, off + 3, l < sizeof(seq) ? l : sizeof(seq), seq) != 0)
				return;
			haveSeq = true;
		} else if (h[0] == AWDL_TLV_SYNC && l >= AWDL_SYNC_LEN) {
			if (mbuf_copydata(m, off + 3, sizeof(sync), sync) != 0)
				return;
			haveSync = true;
		}
		off += 3 + (size_t)l;
	}
	if (!haveSeq || !haveSync || seq[0] != AWDL_SLOTS - 1)
		return;
	uint8_t ch[AWDL_SLOTS];
	for (int i = 0; i < AWDL_SLOTS; i++) {
		switch (seq[1]) {
		case 0: ch[i] = seq[6 + i]; break;
		case 1: ch[i] = seq[7 + 2 * i]; break;
		case 3: ch[i] = seq[6 + 2 * i]; break;
		default: return;
		}
	}
	uint32_t theirs = ((uint32_t)getLe16(sync + 29) / AWDL_PM) % AWDL_SLOTS;
	uint64_t now = awdlNowUs();
	IOLockLock(_txLock);
	uint32_t ours = (((uint32_t)(uint16_t)(_clkBaseAw + (now - _clkBaseUs) /
	    (AWDL_AW_TU * AWDL_TU_US))) / AWDL_PM) % AWDL_SLOTS;
	uint32_t diff = (theirs + AWDL_SLOTS - ours) % AWDL_SLOTS;
	AwdlPeerSeq *e = nullptr, *lru = &_peerSeq[0];
	for (uint32_t i = 0; i < 8 && !e; i++) {
		if (_peerSeq[i].lastUs && !memcmp(_peerSeq[i].mac, src, 6))
			e = &_peerSeq[i];
		else if (_peerSeq[i].lastUs < lru->lastUs)
			lru = &_peerSeq[i];
	}
	if (!e) {
		e = lru;
		memcpy(e->mac, src, 6);
	}
	memcpy(e->ch, ch, sizeof(e->ch));
	e->lastUs = diff <= 1 || diff == AWDL_SLOTS - 1 ? now : 0;
	IOLockUnlock(_txLock);
}

/*
 * Whether data to da can go in this slot on ch. A group address asks about every
 * peer. With nothing recent from the peer, or no slot of its on ch, or no slot
 * common to them all, the answer is yes: not knowing never stops the data. With
 * _txLock held.
 */
#define AWDL_PEER_SEQ_HOLD_US 3000000
bool RTLAwdInterface::peerOnChannel(const uint8_t *da, uint32_t slot, uint8_t ch, uint64_t now) const
{
	uint16_t common = 0xFFFF;
	bool any = false;
	for (uint32_t i = 0; i < 8; i++) {
		const AwdlPeerSeq *e = &_peerSeq[i];
		if (!e->lastUs || now - e->lastUs > AWDL_PEER_SEQ_HOLD_US)
			continue;
		if (!(da[0] & 1) && memcmp(e->mac, da, 6))
			continue;
		uint16_t mask = 0;
		for (int s = 0; s < AWDL_SLOTS; s++)
			if (e->ch[s] == ch)
				mask |= (uint16_t)(1u << s);
		if (mask) {
			common &= mask;
			any = true;
		}
	}
	return !any || !common || ((common >> slot) & 1);
}

/* Stage 3: see the AWDL_ACK block of AWDLInterface.h. */
#define AWDL_ACK(name, type) \
	IOReturn RTLAwdInterface::name(type *) \
	{ \
		LOG("awdl: %s acked", #name); \
		return kIOReturnSuccess; \
	}
AWDL_ACK(setAWDL_BSSID, apple80211_awdl_bssid)
AWDL_ACK(setAWDL_ELECTION_ID, apple80211_awdl_election_id)
AWDL_ACK(setAWDL_MASTER_CHANNEL, apple80211_awdl_master_channel)
AWDL_ACK(setAWDL_ELECTION_RSSI_THRESHOLDS, apple80211_awdl_election_rssi_thresholds)
AWDL_ACK(setAWDL_MIN_RATE, apple80211_awdl_min_rate)
AWDL_ACK(setAWDL_SYNC_STATE, apple80211_awdl_sync_state)
AWDL_ACK(setAWDL_SYNC_PARAMS, apple80211_awdl_sync_params)
AWDL_ACK(setAWDL_SYNC_ENABLED, apple80211_awdl_sync_enabled)
AWDL_ACK(setAWDL_TXCAL_PERIOD, apple80211_awdl_txcal_period)
AWDL_ACK(setAWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
AWDL_ACK(setAWDL_SECONDARY_MASTER_CHANNEL, apple80211_awdl_secondary_master_channel)
AWDL_ACK(setAWDL_AF_TX_MODE, apple80211_awdl_af_tx_mode)
AWDL_ACK(setAWDL_AWDL_ADVERTISERS, awdl_peer_advert_add)
AWDL_ACK(setAWDL_ELECTION_METRIC, apple80211_awdl_election_metric)
AWDL_ACK(setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS, apple80211_awdl_extension_state_machine_parameter)
AWDL_ACK(setPSF_DWELL, apple80211_awdl_psf_dwell_params)
AWDL_ACK(setAWDL_PEER_TRAFFIC_REGISTRATION, apple80211_awdl_peer_traffic_registration)
#undef AWDL_ACK

/*
 * Presence mode (ioctl 200): IO80211AWDLPeerManager::setPresenceMode stores
 * the mode at +17032 only when the driver accepts it, and configure() resets
 * that byte to 0 first. actionFrameProcessInput counts a sync error for every
 * frame whose AW sequence is past window + mode, where window is the AW of
 * the last event 41 (one per slot). With the old answer the mode stayed 0, so
 * an iPhone's frames from the second AW of each slot on were sync errors and
 * the family never posted it as routable (2026-11-08: SyncErrors climbing,
 * isRTG=No, no neighbour entry, its AirDrop service never reached wifip2pd).
 */
IOReturn RTLAwdInterface::setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *p)
{
	uint32_t mode = p ? ((const uint32_t *)(const void *)p)[1] : 0;
	LOG("awdl: setAWDL_PRESENCE_MODE %u acked (slots here are %u AWs)", mode, AWDL_PM);
	return kIOReturnSuccess;
}

/*
 * apple80211_peer_cache_control as IO80211P2PDataPathManager::sendIoctlToCachePeer
 * (25G241 0xffffff800229e458) fills it: u32 version, u32 operation (0 add,
 * 2 delete; AppleBCMWLANProximityInterface::setPEER_CACHE_CONTROL also takes
 * 6), the peer's MAC at +8.
 */
#define AWDL_PCC_OP     4
#define AWDL_PCC_MAC    8
#define AWDL_PCC_LEN    464     /* the buffer addPeerToFWCache and Broadcom allocate */
#define AWDL_PCC_ADDED  1
#define AWDL_PCC_DELETED 3

/* AppleBCMWLANCore::completePeerCacheControl (25G241 0xffffff800165b8b6): opcode
 * at +4, MAC at +8. handlePeerAddIndication marks the peer cached on it; until
 * then handleUmiTimer sends it no UMI. */
void RTLAwdInterface::postPeerCache(uint8_t op, const uint8_t *mac)
{
	uint8_t ev[AWDL_PCC_LEN];
	bzero(ev, sizeof(ev));
	*(uint32_t *)(ev + AWDL_PCC_OP) = op;
	memcpy(ev + AWDL_PCC_MAC, mac, 6);
	postMessage(AWDL_EV_PEER_CACHE, ev, sizeof(ev), false);
}

IOReturn RTLAwdInterface::setPEER_CACHE_CONTROL(apple80211_peer_cache_control *req)
{
	if (!req)
		return kIOReturnBadArgument;
	const uint8_t *p = (const uint8_t *)req;
	uint32_t op = *(const uint32_t *)(p + AWDL_PCC_OP);
	if (_peerCacheOps++ < AWDL_RX_LOG_FIRST)
		LOG("awdl: peer cache op %u %02x:%02x:%02x:%02x:%02x:%02x", op,
		    p[AWDL_PCC_MAC], p[AWDL_PCC_MAC + 1], p[AWDL_PCC_MAC + 2], p[AWDL_PCC_MAC + 3],
		    p[AWDL_PCC_MAC + 4], p[AWDL_PCC_MAC + 5]);
	uint8_t reply = op == 0 || op == 6 ? AWDL_PCC_ADDED : op == 2 ? AWDL_PCC_DELETED : 0;
	if (!reply)
		return kIOReturnSuccess;
	/* The answer comes after this call, as Broadcom's from the firmware: the
	 * caller may sleep waiting for it, and a wakeup sent before that sleep is
	 * lost. Without a tick to carry it, it goes now and the wait times out. */
	bool queued = false;
	if (_txLock) {
		IOLockLock(_txLock);
		if (_txOn && _pccCount < 8) {
			_pccOp[_pccCount] = reply;
			memcpy(_pccDst[_pccCount++], p + AWDL_PCC_MAC, 6);
			queued = true;
		}
		IOLockUnlock(_txLock);
	}
	if (!queued)
		postPeerCache(reply, p + AWDL_PCC_MAC);
	return kIOReturnSuccess;
}

/* u32 version, u32 limit; getCardCapableMaxPeerCacheSize caps it at 8. */
IOReturn RTLAwdInterface::getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size *out)
{
	if (!out)
		return kIOReturnBadArgument;
	*(uint32_t *)((uint8_t *)out + 4) = 8;
	return kIOReturnSuccess;
}

/*
 * u32 version, u32 flags, u32 enabled. The manager routes the userspace GET
 * 117 here (IO80211AWDLPeerManager::getAwdlSyncEnabled) and only compares the
 * answer with its own state. wifip2pd issues that GET once, when it builds
 * its AWDL interface object, and stores enabled != 0 as the flag that allows
 * it to publish a peer's presence from the peer's action frames (event 48):
 * the awdl0 neighbour entry over the routing socket, then ioctl 415. With the
 * old answer (unsupported) the flag stayed 0, no peer ever got its neighbour
 * entry without a manual ndp -s, and AirDrop never saw the iPhone (2026-11-08).
 * The GET can come before the manager's SET 117, so the answer is 1 while the
 * interface exists: sync runs here.
 */
IOReturn RTLAwdInterface::getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *out)
{
	if (!out)
		return kIOReturnBadArgument;
	uint32_t *p = (uint32_t *)(void *)out;
	p[0] = 1;
	p[1] = 0;
	p[2] = 1;
	LOG("awdl: getAWDL_SYNC_ENABLED -> 1");
	return kIOReturnSuccess;
}

/* Publishes the interface on the controller, so getAwdlInterface() answers it.
 * The handler calls it once the start succeeded; NULL unpublishes. */
IOReturn rtlAwdlPublish(IO80211Controller *ctrl, RTLAwdInterface *awdl)
{
	return rtl_ctrl_setAwdlInterface(ctrl, awdl);
}
