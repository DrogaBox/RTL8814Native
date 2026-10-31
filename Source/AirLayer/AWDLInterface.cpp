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

/* airLog() of RTLAirLayer.cpp, exposed by the hook patch so these lines land
 * in the RTLAirLayerLog array of the RTL8814Native node. */
extern void rtlAirLog(const char *fmt, ...) __printflike(1, 2);
#define LOG(fmt, ...) rtlAirLog(fmt, ##__VA_ARGS__)

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
	uint32_t rx = 1;
	PE_parse_boot_argn("rtlawdl_rx", &rx, sizeof(rx));
	_rxAllowed = rx != 0;
	_rxLock = IOLockAlloc();
	_rxDrain = thread_call_allocate(&RTLAwdInterface::rxDrainEntry, this);
	if (!_rxLock || !_rxDrain) {
		LOG("awdl: rx alloc failed");
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
	                             (void *)&RTLAwdInterface::txNoop, nullptr, 0);
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
 * TX submission action, stage 1: nothing is transmitted. The packets go back
 * through the TX completion queue, which completes them under its gate and
 * returns them to the pool. legacyDequeue advances its ring by the returned
 * count, so returning 0 would stall the queue: consume the whole batch.
 */
IOReturn RTLAwdInterface::txNoop(OSObject *owner, void *, void *const *pkts,
                                 uint32_t count, void *)
{
	RTLAwdInterface *me = OSDynamicCast(RTLAwdInterface, owner);
	if (!me || !count || !me->_txCompQ)
		return 0;
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
	setRadioRx(false);
	teardownDatapath();
	IO80211VirtualInterface::stop(provider);
}

void RTLAwdInterface::free()
{
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
	return kIOReturnSuccess;
}

uintptr_t RTLAwdInterface::disable(unsigned int opts)
{
	LOG("awdl: disable 0x%x", opts);
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
 * actionFrameInput (Docs/AWDL_PEERMANAGER_DRIVER_ABI.md §1.5). Data frames are
 * only counted: their path to the RX completion queue is not written yet.
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
	LOG("awdl: rx %s, af %llu data %llu other %llu dropped %llu", on ? "attached" : "detached",
	    _rxAf, _rxData, _rxOther, _rxDropped);
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
		if (type == 2)
			_rxData++;
		else
			_rxOther++;
		mbuf_freem(m);
		return;
	}
	_rxAf++;
	if ((_rxAf & 0xFF) == 0)
		LOG("awdl: rx af %llu data %llu other %llu dropped %llu", _rxAf, _rxData, _rxOther,
		    _rxDropped);

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
AWDL_ACK(setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, apple80211_awdl_sync_channel_sequence)
AWDL_ACK(setAWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
AWDL_ACK(setAWDL_SYNC_FRAME_TEMPLATE, apple80211_awdl_sync_frame_template)
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
