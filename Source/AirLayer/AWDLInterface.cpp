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

#define AWDL_TX_RING            32      /* AWDL data frames waiting for a window */
#define AWDL_TX_PER_TICK        4       /* data frames per AW tick: ~8 ms of air at 12 Mb/s */
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
	if (!_rxLock || !_rxDrain) {
		LOG("awdl: rx alloc failed");
		return false;
	}
	uint32_t tx = 0;
	PE_parse_boot_argn("rtlawdl_tx", &tx, sizeof(tx));
	_txAllowed = tx != 0;
	uint32_t park = 149;
	PE_parse_boot_argn("rtlawdl_ch", &park, sizeof(park));
	_parkCh = park == 6 || park == 44 || park == 149 ? (uint8_t)park : 0;
	/* Same radio the controller drives (RTLAirController::start). */
	IOService *nub = ctrl->getProvider();
	_radioSvc = nub ? nub->getProvider() : nullptr;
	if (_radioSvc)
		_radioSvc->retain();
	_symTx80211 = OSSymbol::withCString(RTL_RADIO_FN_TX_80211);
	_txLock = IOLockAlloc();
	_txTick = thread_call_allocate(&RTLAwdInterface::txTickEntry, this);
	_txRing = (AwdlTxFrame *)IOMallocZero(sizeof(AwdlTxFrame) * AWDL_TX_RING);
	if (!_symTx80211 || !_txLock || !_txTick || !_txRing) {
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
	if (_rxLock) {
		IOLockFree(_rxLock);
		_rxLock = nullptr;
	}
	/* A pending or running tick holds a reference, so none is here. */
	if (_txTick) {
		thread_call_free(_txTick);
		_txTick = nullptr;
	}
	if (_txLock) {
		IOLockFree(_txLock);
		_txLock = nullptr;
	}
	if (_txRing) {
		IOFree(_txRing, sizeof(AwdlTxFrame) * AWDL_TX_RING);
		_txRing = nullptr;
	}
	OSSafeReleaseNULL(_symTx80211);
	OSSafeReleaseNULL(_radioSvc);
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
#define AWDL_RX_META          4       /* rssi (s8), channel, 2 spare bytes */
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
	if (!on)
		flushRadioRx();
	LOG("awdl: rx %s, af %llu data %llu (up %llu bad %llu not us %llu) other %llu dropped %llu",
	    on ? "attached" : "detached", _rxAf, _rxData, _rxDataUp, _rxDataBad, _rxDataNotUs,
	    _rxOther, _rxDropped);
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
	uint8_t meta[AWDL_RX_META] = { (uint8_t)rssi, channel, 0, 0 };
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
			deliverData(m);
			return;
		}
		_rxOther++;
		mbuf_freem(m);
		return;
	}
	_rxAf++;
	if ((_rxAf & 0xFF) == 0)
		LOG("awdl: rx af %llu data %llu other %llu dropped %llu", _rxAf, _rxData, _rxOther,
		    _rxDropped);

	if (_txOn)
		alignClock(m, f + 10);

	/* Source MAC, channel and RSSI: the manager reads +0x00 and +0x2c; the
	 * channel only travels to the BPF tap (ABI §1.3). */
	uint8_t hdr[AWDL_AF_HDR_LEN];
	bzero(hdr, sizeof(hdr));
	memcpy(hdr, f + 10, 6);
	*(uint32_t *)(hdr + AWDL_AF_HDR_CHANNEL) = 1;
	*(uint32_t *)(hdr + AWDL_AF_HDR_CHANNEL + 4) = channel;
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
 * no ARPA (16) TLV: the host name is not available here (?).
 */
#define AWDL_TU_US            1024
#define AWDL_AW_TU            16
#define AWDL_PM               4       /* AWs per slot (presence mode) */
#define AWDL_SLOT_TU          (AWDL_AW_TU * AWDL_PM)
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
	bool queued = true;
	IOLockLock(_txLock);
	copyMiTemplate(p);
	if (!memcmp(dst, bcast, 6)) {
		_mifNow = true;
	} else {
		uint8_t i = 0;
		while (i < _umiCount && memcmp(_umiDst[i], dst, 6))
			i++;
		if (i == _umiCount && _umiCount < 4)
			memcpy(_umiDst[_umiCount++], dst, 6);
		else if (i == _umiCount)
			queued = false;
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
	IOLockLock(_txLock);
	bool first = !_seqValid;
	for (int i = 0; i < AWDL_SLOTS; i++) {
		const uint8_t *e = p + 0x0c + 12 * i;
		_seq[i] = i < n ? (uint8_t)*(const uint32_t *)(e + 4) : 0;
		_seqFlags[i] = i < n ? *(const uint32_t *)(e + 8) : 0;
	}
	_seqValid = n != 0;
	IOLockUnlock(_txLock);
	if (first)
		LOG("awdl: channel sequence %u slots: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u", n,
		    _seq[0], _seq[1], _seq[2], _seq[3], _seq[4], _seq[5], _seq[6], _seq[7], _seq[8],
		    _seq[9], _seq[10], _seq[11], _seq[12], _seq[13], _seq[14], _seq[15]);
	return kIOReturnSuccess;
}

void RTLAwdInterface::setRadioTx(bool on)
{
	if (on && (!_txAllowed || !_radioSvc))
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
		IOLockUnlock(_txLock);
		_ctrl->radio(RTL_RADIO_FN_AWDL_PORT, nullptr);
		_port1On = false;
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
 * 110 TU and a MIF in the third AW of each served slot.
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
	if (_ctrl && _ctrl->radioStatus(&st) == kIOReturnSuccess && !st.scanning) {
		if (st.state == RTL_RADIO_S_LINKED)
			ch = st.channel;
		else if (st.state == RTL_RADIO_S_IDLE)
			ch = parkRadio(st.idle_channel, awdlNowUs());
	}
	if (ch && !awdlSocial(ch)) {
		if (!_offSocialLogged) {
			_offSocialLogged = true;
			LOG("awdl: radio on channel %u, not a social channel: no AWDL TX or windows", ch);
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
			close = _awOpen;
			_awOpen = false;
		}
		if (aw % AWDL_PM == 0 && served) {
			open = true;
			_awOpen = true;
		}
		if (served && aw % AWDL_PM == AWDL_PM / 2 && aw / AWDL_PM != _lastMifSlot) {
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
		 * µs big-endian, channel flags and channel. */
		const uint8_t *e2 = findTlv(_tpl, _tplLen, AWDL_TLV_ELECTION_V2, 28);
		const uint8_t *top = e2 ? e2 + 3 : _mac;
		const uint8_t *sync = e2 ? e2 + 3 + 6 : _mac;
		bzero(ev41, sizeof(ev41));
		*(uint16_t *)ev41 = (uint16_t)aw;
		*(uint32_t *)(ev41 + 0x04) = memcmp(sync, _mac, 6) ? 2 : 1;
		memcpy(ev41 + 0x08, sync, 6);
		memcpy(ev41 + 0x0e, top, 6);
		*(uint32_t *)(ev41 + 0x14) = OSSwapHostToBigInt32((uint32_t)awStart);
		*(uint32_t *)(ev41 + 0x18) = _seqValid && _seqFlags[slot] ? _seqFlags[slot]
		                                                           : (ch <= 14 ? 0x8 : 0x10);
		*(uint32_t *)(ev41 + 0x1c) = ch;
	}
	uint64_t next = _clkBaseUs + (awIdx + 1) * AWDL_AW_TU * AWDL_TU_US;
	/* Data goes only inside a served slot's window, a peer listens on this
	 * channel only in the slots it shares with us, and not in its last AW,
	 * so the MGNT queue does not spill past the slot and delay the next
	 * sync frames. */
	bool dataWindow = ch && served && _awOpen && _txRingCount && aw % AWDL_PM != AWDL_PM - 1;
	/* The 149s go out in a window of ours, where peers listen. */
	uint8_t umi[4][6];
	uint8_t nUmi = 0;
	bool mifNow = false;
	if (ch && served && _awOpen) {
		nUmi = _umiCount;
		memcpy(umi, _umiDst, sizeof(umi));
		_umiCount = 0;
		mifNow = _mifNow;
		_mifNow = false;
	}
	IOLockUnlock(_txLock);

	/* Events 43 then 41, outside the lock: postMessage takes the controller's
	 * gate (Docs/AWDL_STAGE3_EVENT_EMITTERS.md §3.1). */
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
		for (uint8_t i = 0; i < nUmi; i++)
			if (sendAf(AWDL_AF_MIF, ch, mask, umi[i]))
				_txUmi++;
		if (psf && (_txPsf & 0x3FF) == 0)
			LOG("awdl: tx psf %llu mif %llu fail %llu windows %llu ch %u, data queued %llu "
			    "sent %llu dropped full %llu old %llu bad %llu fail %llu",
			    _txPsf, _txMif, _txFail, _ev41, ch, _txDataQueued, _txDataSent,
			    _txDataDropFull, _txDataDropOld, _txDataDropBad, _txDataFail);
	}
	if (dataWindow)
		sendData();

	IOLockLock(_txLock);
	_tickBusy = false;
	if (_txOn) {
		uint64_t t = awdlNowUs();
		uint64_t wait = next > t ? next - t : 1000;
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
 * most once a second. Called from txTick only.
 */
uint8_t RTLAwdInterface::parkRadio(uint8_t idleCh, uint64_t nowUs)
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
	if (_parkReqUs && nowUs - _parkReqUs < 1000000)
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
	uint16_t mask = 0;
	for (int i = 0; i < AWDL_SLOTS; i++)
		if (_seq[i] == ch)
			mask |= (uint16_t)(1u << i);
	return mask;
}

bool RTLAwdInterface::sendAf(uint8_t subtype, uint8_t channel, uint16_t mask, const uint8_t *da)
{
	size_t len = buildAf(_afBuf, sizeof(_afBuf), subtype, channel, mask, da);
	if (!len)
		return false;
	uint32_t l = (uint32_t)len;
	IOReturn r = _radioSvc->callPlatformFunction(_symTx80211, false, _afBuf, &l, nullptr, nullptr);
	if (r != kIOReturnSuccess) {
		if (_txFail++ < AWDL_RX_LOG_FIRST)
			LOG("awdl: tx %s -> 0x%x", da ? "umi" : subtype == AWDL_AF_MIF ? "mif" : "psf", r);
		return false;
	}
	return true;
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
	 * is assumed (not yet measured) to match the ACKs to ours against it
	 * (Docs/AWDL_RTL8814_RX_ACK.md). */
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
	IOLockUnlock(_txLock);
}

/*
 * Sends queued data frames while the window of a served slot is open, one at
 * a time with _txLock dropped around the radio call. A transient refusal puts
 * the frame back at the head and ends the batch; kIOReturnBadArgument means a
 * driver without AWDL data TX, which turns data TX off. Called from txTick.
 */
void RTLAwdInterface::sendData()
{
	AwdlTxFrame f;
	for (uint32_t n = 0; n < AWDL_TX_PER_TICK; n++) {
		IOLockLock(_txLock);
		if (!_txOn || !_txRingCount || !_txRing) {
			IOLockUnlock(_txLock);
			return;
		}
		memcpy(&f, &_txRing[_txRingHead], offsetof(AwdlTxFrame, frame) + _txRing[_txRingHead].len);
		_txRingHead = (_txRingHead + 1) % AWDL_TX_RING;
		_txRingCount--;
		IOLockUnlock(_txLock);
		if ((uint32_t)awdlNowUs() - f.tsUs > AWDL_TX_AGE_US) {
			_txDataDropOld++;
			continue;
		}
		uint32_t l = f.len;
		IOReturn r = _radioSvc->callPlatformFunction(_symTx80211, false, f.frame, &l, nullptr,
		                                             nullptr);
		if (r == kIOReturnSuccess) {
			if (_txDataSent++ == 0)
				LOG("awdl: first data frame to %02x:%02x:%02x:%02x:%02x:%02x ethertype %02x%02x len %u",
				    f.frame[4], f.frame[5], f.frame[6], f.frame[7], f.frame[8], f.frame[9],
				    f.frame[AWDL_WLAN_HDR + 14], f.frame[AWDL_WLAN_HDR + 15], f.len);
			continue;
		}
		if (r == kIOReturnBadArgument) {
			LOG("awdl: radio refused a data frame: AWDL data TX off (driver without it?)");
			IOLockLock(_txLock);
			_txDataOff = true;
			IOLockUnlock(_txLock);
			flushRadioTx();
			return;
		}
		if (r == kIOReturnBusy || r == kIOReturnNoResources || r == kIOReturnNotReady) {
			IOLockLock(_txLock);
			if (_txOn && _txRingCount < AWDL_TX_RING) {
				_txRingHead = (_txRingHead + AWDL_TX_RING - 1) % AWDL_TX_RING;
				memcpy(&_txRing[_txRingHead], &f, offsetof(AwdlTxFrame, frame) + f.len);
				_txRingCount++;
			} else {
				_txDataDropFull++;
			}
			IOLockUnlock(_txLock);
			return;
		}
		if (_txDataFail++ < AWDL_RX_LOG_FIRST)
			LOG("awdl: tx data -> 0x%x", r);
	}
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
                                uint16_t mask, const uint8_t *da)
{
	IOLockLock(_txLock);
	/* Read under the lock: alignClock may have moved _clkBaseUs past an
	 * earlier sample, and nowUs - _clkBaseUs would wrap. */
	uint64_t nowUs = awdlNowUs();
	/* A MIF or UMI carries the MI template once the manager sent one: it
	 * holds the ARPA and service TLVs the sync template lacks. */
	bool mi = subtype == AWDL_AF_MIF && _miTplLen;
	const uint8_t *tpl = mi ? _miTpl : _tpl;
	uint32_t tplLen = mi ? _miTplLen : _tplLen;
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
	const uint8_t *e2 = findTlv(_tpl, tplLen, AWDL_TLV_ELECTION_V2, 28);
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
	_clkBaseUs = now - (uint64_t)into * AWDL_TU_US;
	_clkBaseAw = (uint16_t)(aw - into / AWDL_AW_TU);
	_clkAligned++;
	IOLockUnlock(_txLock);
	if (_clkAligned <= 4)
		LOG("awdl: clock follows %02x:%02x:%02x:%02x:%02x:%02x aw %u next slot %u TU", src[0],
		    src[1], src[2], src[3], src[4], src[5], aw, toSlot);
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
#undef AWDL_ACK

/* Publishes the interface on the controller, so getAwdlInterface() answers it.
 * The handler calls it once the start succeeded; NULL unpublishes. */
IOReturn rtlAwdlPublish(IO80211Controller *ctrl, RTLAwdInterface *awdl)
{
	return rtl_ctrl_setAwdlInterface(ctrl, awdl);
}
