/*
 * RTL8814AirLayer — native IO80211 layer for RTL8814Native (Phase 5)
 *
 * A separate kext that matches on the RTL8814Native service and publishes an
 * IO80211Controller + IO80211InfraProtocol pair, so airportd / the Wi-Fi menu
 * drive the radio through the WCL requests decoded in Docs/PHASE5_IO80211.md.
 * It talks to the radio only through RTLRadioABI.h (callPlatformFunction).
 *
 * Built against Source/IO80211/IO80211_25_6_0.h: macOS 26.x / Darwin 25.6.0
 * only. start() refuses unless the boot-arg rtlair is set, so the kext can
 * sit in the EFI without touching the running system.
 *
 * rtlair=1: scan and getters only. Join requests are refused, so WCL (or an
 * auto-join at boot) can never move the radio away from en2's link.
 * rtlair=2: joins too, and while linked the data goes through the Skywalk
 * interface instead of en2. Unverified on hardware.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL_AIR_LAYER_HPP
#define _RTL_AIR_LAYER_HPP

#include "IO80211_25_6_0.h"
#include <IOKit/IOLocks.h>
#include <kern/thread_call.h>
#include <sys/kpi_mbuf.h>
#include "RTLRadioABI.h"

class RTLAirController;

/* Matches RTL8814Native and publishes the IO80211WorkQueue as its work loop;
 * RTLAirController matches on it. IONetworkController::start calls
 * getThread() on the controller's work loop unless it equals the provider's,
 * and IO80211WorkQueue::getThread() is NULL (Broadcom's bus nub shares it). */
class RTLAirNub : public IOService {
	OSDeclareDefaultStructors(RTLAirNub)
public:
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
	IOWorkLoop *getWorkLoop() const override;
};

/* Every IO80211InfraProtocol pure virtual, answering kIOReturnUnsupported. */
class RTLAirInterfaceStubs : public IO80211InfraProtocol {
	OSDeclareAbstractStructors(RTLAirInterfaceStubs)
public:
#include "IO80211_25_6_0_infra_decl.inc"
};

class RTLAirInterface : public RTLAirInterfaceStubs {
	OSDeclareDefaultStructors(RTLAirInterface)
public:
	bool initWithController(RTLAirController *ctrl, const uint8_t mac[6]);
	bool startWithRegistration(RTLAirController *ctrl, void *regInfo);
	void stop(IOService *provider) override;
	uintptr_t enable(unsigned int opts) override;
	uintptr_t disable(unsigned int opts) override;
	void rxFrames(mbuf_t chain);

	/* WCL requests */
	IOReturn setWCL_SCAN_REQ(apple80211ScanRequest *req) override;
	IOReturn setWCL_ASSOCIATE(apple80211AssocCandidates *cand) override;
	IOReturn setCIPHER_KEY(apple80211_key *key) override;
	IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network *leave) override;
	IOReturn setWCL_JOIN_ABORT(apple80211_wcl_abort_join *abort) override;
	IOReturn setWCL_REASSOC(apple80211_reassoc *req) override;
	IOReturn setWCL_SCAN_ABORT(void *req) override;
	IOReturn getMCS(apple80211_mcs_data *md) override;
	IOReturn getMCS_VHT(apple80211_mcs_vht_data *md) override;
	IOReturn getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *nd) override;
	IOReturn getBEACON_INFO(apple80211_beacon_info_t *bi) override;
	IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *tc) override;
	IOReturn getROAM_PROFILE(apple80211_roam_profile_all_bands *rp) override;
	IOReturn getCHANNELS_INFO(apple80211_channels_info *ci) override;
	IOReturn getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *ci) override;
	IOReturn setROAM_PROFILE(apple80211_roam_profile_all_bands *rp) override;
	/* Settings with nothing to apply on this radio: accepted, so WCL and
	 * airportd do not retry them or log a failure every few seconds. */
	IOReturn setBTCOEX_PROFILE(apple80211_btcoex_profile *) override;
	IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *) override;
	IOReturn setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *) override;
	IOReturn setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *) override;
	IOReturn setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *) override;
	IOReturn setWCL_QOS_PARAMS(apple80211_wcl_qos_params *) override;
	IOReturn setOS_FEATURE_FLAGS(apple80211_feature_flags *) override;
	IOReturn setWCL_LINK_UP_DONE(void *) override;
	IOReturn setPOWERSAVE(apple80211_powersave_data *) override;
	IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated *) override;
	IOReturn setIE(apple80211_ie_data *) override;
	IOReturn setBSS_BLACKLIST(bss_blacklist *) override;
	IOReturn setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *) override;
	IOReturn setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *) override;
	IOReturn setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) override;
	IOReturn getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *hc) override;
	IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *info) override;
	IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *info) override;
	IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane *fl) override;
	uintptr_t setMacAddress(mloAddrArray &addrs) override;

	/* getters */
	IOReturn getCHANNEL(apple80211_channel_data *cd) override;
	IOReturn getRSSI(apple80211_rssi_data *rd) override;
	IOReturn getRATE(apple80211_rate_data *rd) override;
	IOReturn getOP_MODE(apple80211_opmode_data *od) override;
	IOReturn getNSS(apple80211_nss_data *nd) override;
	IOReturn getSUPPORTED_CHANNELS(apple80211_sup_channel_data *cd) override;
	IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *cd) override;
	IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *msg) override;

private:
	RTLAirController *_ctrl;
	void *_txPool, *_rxPool;
	void *_txSubQ, *_txCompQ, *_rxCompQ;

	bool createDatapath(void *regInfo);
	static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);
	static IOReturn switchQueues(OSObject *owner, void *on, void *, void *, void *);
	static IOReturn txAction(OSObject *owner, void *queue, void *const *pkts,
	                         uint32_t count, void *refCon);

	void free() override;
};

/* Every IO80211Controller pure virtual, answering kIOReturnUnsupported. */
class RTLAirControllerStubs : public IO80211Controller {
	OSDeclareAbstractStructors(RTLAirControllerStubs)
public:
#include "IO80211_25_6_0_ctrl_decl.inc"
};

class RTLAirController : public RTLAirControllerStubs {
	OSDeclareDefaultStructors(RTLAirController)
public:
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
	IOReturn getHardwareAddress(IOEthernetAddress *addr) override;

	bool isCommandProhibited(int cmd) override;
	IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *vd) override;
	IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *vd) override;
	IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *cd) override;
	IOReturn getPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd) override;
	IOReturn setPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd) override;
	IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *cd) override;
	IOReturn getPLATFORM_CONFIG(IO80211SkywalkInterface *, apple80211_platform_config *pc) override;
	void *getLogger() const override;
	void *getFaultReporterFromDriver() override;
	/* The family's defaults return NULL: IONetworkController::start fails in
	 * createWorkLoop (slot 306 -> 395), and getWorkLoop (-> 397) is NULL. */
	uintptr_t createWorkQueue() override;
	IO80211WorkQueue *getWorkQueue() const override;

	/* Radio ABI, routed to the RTL8814Native provider. */
	IOReturn radio(const char *fn, void *p1 = nullptr, void *p2 = nullptr);
	IOReturn radioStatus(struct rtl_radio_status *st);

	/* Driver -> WCL event (IO80211Controller::postMessage on the STA interface). */
	void post(uint32_t msg, void *data, size_t len);

	/* Called by the interface. */
	IOReturn startScan();
	IOReturn startJoin(const struct rtl_radio_join *j);
	IOReturn startRoam(const uint8_t *bssids, uint32_t count);
	void leave(bool notify);
	void syncMac();
	bool copyBeacon(const uint8_t bssid[6], uint8_t *msg);
	uint32_t copyBeaconFrame(const uint8_t bssid[6], uint8_t *frame, uint32_t max);
	bool countryCode(char cc[2]);
	uint32_t hotChannels(uint16_t *spec, uint32_t max);
	void countTx() { __atomic_fetch_add(&_txPkts, 1, __ATOMIC_RELAXED); }
	void countRx(uint32_t n) { __atomic_fetch_add(&_rxPkts, n, __ATOMIC_RELAXED); }
	uint64_t trafficPkts() const { return (uint64_t)(_txPkts + _rxPkts); }
	uint8_t _roamProfile[384];      /* airportd's APPLE80211_IOC_ROAM_PROFILE, read back as set */
	void transmit(mbuf_t m);
	bool dataPathEnabled() const { return _dataHooked; }
	RTLAirInterface *staInterface() const { return _iface; }
	bool joinAllowed() const { return _armed >= 2; }
	/* Radio status, but only while this layer owns the link: the radio may be
	 * linked through en2 alone, which WCL must not see as an association. */
	IOReturn linkStatus(struct rtl_radio_status *st);

	void free() override;

private:
	IOService *_radio;
	RTLAirInterface *_iface;
	uint8_t _mac[6];
	uint32_t _power;
	uint32_t _armed;                /* boot-arg rtlair: 1 control plane, 2 + data path */
	volatile bool _dataHooked;
	const OSSymbol *_symTx;
	uint8_t *_regInfo;              /* IOSkywalkEthernetInterface::RegistrationInfo */

	/* Scan / join / link polling runs on a thread call, outside any IO80211 gate. */
	thread_call_t _poll;
	IOLock *_lock;
	enum { kIdle, kJoining, kLinked, kRoaming } _phase;
	bool _scanPending;
	bool _scanLinked;               /* scan started while associated */
	volatile bool _stopping;
	uint32_t _scanTicks, _joinTicks, _lqmTicks;
	uint8_t _joinBssid[6];
	/* The network joined, to re-issue a join the radio dropped and to roam;
	 * holds the PMK while joining or linked (zeroed on leave or link loss). */
	struct rtl_radio_join _joinReq;
	uint32_t _joinRetries, _joinTrace;
	/* The link as last seen by the poll, answered to WCL while a roam runs. */
	struct rtl_radio_status _linkSt;
	volatile SInt64 _txPkts, _rxPkts;

	/* RX frames from the radio hook, handed to the interface on a thread call
	 * so the hook (under the radio's lock) never waits on the IO80211 gate. */
	thread_call_t _rxDrain;
	IOLock *_rxLock;
	mbuf_t _rxHead, _rxTail;
	uint32_t _rxCount;

	/* CoreCapture objects IO80211Controller::start requires (see createCoreCapture). */
	IOWorkLoop *_ccWorkLoop;
	OSObject *_ccLogPipe, *_ccLogStream;
	OSObject *_ccDataPipe, *_ccDataStream, *_ccFault, *_ioFault;
	bool createCoreCapture();

	struct rtl_radio_bss *_scan;   /* last scan results (kScanMax) */
	uint32_t _scanCount;

	static void pollEntry(thread_call_param_t p0, thread_call_param_t p1);
	void poll();
	void finishScan();
	void reportJoin(bool ok, const struct rtl_radio_status *st);
	void linkLost();
	void reportLqm(const struct rtl_radio_status *st);
	void retryJoin();
	void reportRoam(bool ok, const struct rtl_radio_status *st);
	void schedulePoll(uint32_t ms);
	void setDataPath(bool on);
	void quiesce();
	static void rxHook(void *ctx, void *m);
	static void rxDrainEntry(thread_call_param_t p0, thread_call_param_t p1);
	void flushRx();
};

#endif /* _RTL_AIR_LAYER_HPP */
