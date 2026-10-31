/*
 * RTL8814AirLayer — AWDL stage 1: the virtual interface chain and awdl0.
 *
 * Source/IO80211/IO80211_25_6_0.h stops at IO80211SkywalkInterface (slot 460,
 * 0xE60, syncDPSStats). This file continues the same object with the vtables of
 * IO80211VirtualInterface and IO80211AWDLProtocol, declared by offset
 * (offset = 8 * slot, the convention of IO80211_25_6_0.h):
 *
 *   0xE68 - 0xF38  IO80211VirtualInterface, 27 methods
 *   0xF40          setMacAddress(ether_addr&), pure
 *   0xF48 - 0xFC0  16 reserved slots
 *   0xFC8 - 0x11B8 IO80211AWDLProtocol, 63 pure slots
 *
 * Only the AWDL slots are overridden here; the 27 methods above them keep the
 * family implementations, which the loader resolves from the kernel collection.
 * The class sizes (0x130) are checked at the end. This file must not be
 * included together with Source/IO80211/IO80211AWDL_25_6_0.h: that header is a
 * signature catalog, and a declaration that omits slots shifts every offset
 * after it.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL_AWDL_INTERFACE_H
#define RTL_AWDL_INTERFACE_H

#include "RTLAirLayer.hpp"

/* ---- apple80211 payloads used by pointer in the AWDL vtables ---- */
/* Already declared in IO80211_25_6_0.h: channel, state, key, debug command,
 * network packet. The rest are only needed by type. */
struct apple80211_ssid_data;
struct apple80211_bssid_data;
struct apple80211_encrypt;
struct apple80211_tx_rate;
struct apple80211_llw_params;
struct apple80211_awdl_sync_params;
struct apple80211_awdl_sync_enabled;
struct apple80211_awdl_extension_state_machine_parameter;
struct apple80211_awdl_election_id;
struct apple80211_awdl_election_metric;
struct apple80211_awdl_sync_channel_sequence;
struct apple80211_peer_cache_maximum_size;
struct apple80211_awdl_master_channel;
struct apple80211_awdl_sync_state;
struct apple80211_awdl_election_rssi_thresholds;
struct apple80211_awdl_presence_mode;
struct apple80211_awdl_election_params;
struct apple80211_awdl_device_capabilities;
struct apple80211_awdl_secondary_master_channel;
struct apple80211_awdl_af_tx_mode;
struct apple80211_awdl_min_rate;
struct apple80211_awdl_txcal_period;
struct apple80211_awdl_dev_stats;
struct apple80211_awdl_dfsp_config;
struct apple80211_awdl_cca;
struct apple80211_awdl_cap;
struct apple80211_awdl_bssid;
struct apple80211_peer_cache_control;
struct apple80211_awdl_oob_request;
struct apple80211_awdl_sync_frame_template;
struct apple80211_awdl_peer_traffic_registration;
struct apple80211_awdl_dfsp_ucsa_config;
struct apple80211_companion_skywalk_if_create;
struct apple80211_awdl_af_sec_payload_request;
struct apple80211_p2p_coex_protection;
struct apple80211_awdl_psf_dwell_params;
struct apple80211_awdl_config_request;
struct apple80211_rtg_peer_stats_cfg;
struct awdl_peer_advert_add;
struct peerSlotDataStats;
struct packetCounters_t;

/* ---- VIRTUAL_IF_CREATE (type 94), Docs/AWDL_STRUCTS.md §3.2 ---- */
struct apple80211_virt_if_create_data {
	uint32_t f0;             /* +0x00: written by wifip2pd, read not needed  */
	uint8_t  mac[6];         /* +0x04: requested address                     */
	uint8_t  pad0[2];        /* +0x0A                                        */
	uint32_t role;           /* +0x0C: 6 = AWDL                              */
	char     ifname[16];     /* +0x10: output, BSD name of the new interface */
};
static_assert(sizeof(apple80211_virt_if_create_data) == 32, "virt_if_create_data");

/*
 * Slots 0xE68-0xF38. The declarations exist so the AWDL slots land on their
 * real offsets; the bodies live in IO80211Family and are not replaced here.
 */
struct IO80211VirtualInterface : public IO80211SkywalkInterface {
	OSDeclareAbstractStructors(IO80211VirtualInterface)
public:
	/* Overrides of inherited slots: no new vtable entries. */
	virtual void free() override;
	virtual void stop(IOService *) override;
	/* Overrides that IO80211VirtualInterface itself defines (checked against
	 * __ZTV19IO80211AWDLProtocol): without them these cells would resolve to
	 * the IO80211SkywalkInterface or IOService bodies. */
	virtual IOReturn configureReport(IOReportChannelList*, unsigned int, void*, void*) override; /* 0x428 */
	virtual IOReturn updateReport(IOReportChannelList*, unsigned int, void*, void*) override; /* 0x430 */
	virtual bool start(IOService*) override;                                       /* 0x5C0 */
	virtual bool terminate(IOOptionBits options = 0) override;                                 /* 0x600 */
	virtual bool attach(IOService *) override;                                     /* 0x670 */
	virtual void detach(IOService *) override;                                     /* 0x678 */
	virtual IOReturn newUserClient(task*, void*, unsigned int, OSDictionary*, IOUserClient**) override; /* 0x770 */
	virtual const char * stringFromReturn(int) override;                           /* 0x780 */
	virtual uintptr_t prepareBSDInterface(__ifnet*, unsigned int) override;        /* 0x8E8 */
	virtual uintptr_t processBSDCommand(__ifnet*, unsigned int, void*) override;   /* 0x910 */
	virtual uintptr_t setInterfaceEnable(bool) override;                           /* 0x930 */
	virtual uintptr_t getFeatureFlags() override;                                  /* 0x9B8 */
	virtual uintptr_t bpfTap(unsigned int, unsigned int) override;                 /* 0x9E0 */
	virtual uintptr_t setMulticastAddresses(ether_addr const*, unsigned int) override; /* 0xA88 */
	virtual uintptr_t createPeerManager() override;                                /* 0xB08 */
	virtual uintptr_t postMessage(unsigned int, void*, unsigned long, bool) override; /* 0xB18 */
	virtual uintptr_t logTxPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, apple80211_wme_ac, bool) override; /* 0xB38 */
	virtual uintptr_t logTxCompletionPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, int, unsigned int, bool, bool) override; /* 0xB40 */
	virtual uintptr_t inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool) override; /* 0xB50 */
	virtual uintptr_t logSkywalkTxReqPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, bool) override; /* 0xB60 */
	virtual uintptr_t pendingPackets(unsigned char) override;                      /* 0xB68 */
	virtual uintptr_t packetSpace(unsigned char) override;                         /* 0xB70 */
	virtual uintptr_t linkState() override;                                        /* 0xB88 */
	virtual uintptr_t setScanningState(unsigned int, bool, apple80211_scan_data*, int) override; /* 0xB90 */
	virtual uintptr_t updateLinkParameters(apple80211_interface_availability*) override; /* 0xBB0 */
	virtual uintptr_t updateInterfaceCoexRiskPct(unsigned long long) override;     /* 0xBB8 */
	virtual uintptr_t setInterfaceNF(apple80211_channel, long long) override;      /* 0xBE8 */
	virtual uintptr_t setDebugFlags(unsigned long long, unsigned int) override;    /* 0xC00 */
	virtual uintptr_t debugFlags() override;                                       /* 0xC08 */
	virtual uintptr_t setInterfaceChipCounters(apple80211_stat_report*, apple80211_chip_counters_tx*, apple80211_chip_error_counters_tx*, apple80211_chip_counters_rx*) override; /* 0xC10 */
	virtual uintptr_t setInterfaceMIBdot11(apple80211_stat_report*, apple80211_ManagementInformationBasedot11_counters*) override; /* 0xC18 */
	virtual uintptr_t setFrameStats(apple80211_stat_report*, apple80211_frame_counters*) override; /* 0xC20 */
	virtual uintptr_t setInfraSpecificFrameStats(apple80211_stat_report*, apple80211_infra_specific_stats*) override; /* 0xC28 */
	virtual uintptr_t getWmeTxCounters(unsigned long long*) override;              /* 0xC38 */
	virtual uintptr_t setWoWEnabled(bool) override;                                /* 0xC48 */
	virtual uintptr_t wowEnabled() override;                                       /* 0xC50 */
	virtual uintptr_t printDataPath(userPrintCtx*) override;                       /* 0xC58 */
	virtual uintptr_t findOrCreateFlowQueue(IO80211FlowQueueHash) override;        /* 0xC68 */
	virtual uintptr_t findOrCreateFlowQueueWithCache(IO80211FlowQueueHash, bool*) override; /* 0xC70 */
	virtual uintptr_t findExistingFlowQueue(IO80211FlowQueueHash) override;        /* 0xC78 */
	virtual uintptr_t removePacketQueue(IO80211FlowQueueHash const*) override;     /* 0xC80 */
	virtual uintptr_t flushPacketQueues() override;                                /* 0xC88 */
	virtual uintptr_t cachePeer(ether_addr*, unsigned int*) override;              /* 0xC90 */
	virtual uintptr_t shouldLog(unsigned long long) override;                      /* 0xC98 */
	virtual uintptr_t vlogDebug(unsigned long long, char const*, va_list) override; /* 0xCA0 */
	virtual uintptr_t vlogDebugBPF(unsigned long long, char const*, va_list) override; /* 0xCA8 */
	virtual uintptr_t getP2PSkywalkPeerMgr() override;                             /* 0xCC0 */
	virtual uintptr_t findPeer(ether_addr&) override;                              /* 0xCD0 */
	virtual uintptr_t getEventPipeSize() override;                                 /* 0xDB8 */
	virtual uintptr_t createEventPipe(IO80211APIUserClient*) override;             /* 0xDC0 */
	/* Keeps init() and init(IOService*, ether_addr*) visible next to the
	 * overload below. */
	using IO80211SkywalkInterface::init;
	virtual bool init(IO80211Controller *, ether_addr *, uint32_t, const char *); /* 0xE68 */
	virtual uintptr_t setLinkState(IO80211LinkState, unsigned int);                /* 0xE70 */
	virtual uintptr_t forwardPacket(IO80211NetworkPacket *);                       /* 0xE78 */
	virtual uintptr_t handleIoctl(__ifnet *, unsigned long, void *);               /* 0xE80 */
	virtual uintptr_t addPeerToCache(unsigned char *);                             /* 0xE88 */
	virtual uintptr_t deletePeerFromCache(unsigned char *);                        /* 0xE90 */
	virtual uintptr_t handleDebugCmd(apple80211_debug_command *);                  /* 0xE98 */
	virtual uintptr_t getPeerSlotDataStats(ether_addr *, bool,
	                                       peerSlotDataStats *);                   /* 0xEA0 */
	virtual uintptr_t getPeerDataStats(ether_addr *, bool,
	                                   packetCounters_t *);                        /* 0xEA8 */
	virtual uintptr_t clearPeerDataStats(ether_addr *, bool);                      /* 0xEB0 */
	virtual uintptr_t setAwdlCurrentChannelSequenceIndex(unsigned int);            /* 0xEB8 */
	virtual uintptr_t isOutputFlowControlled();                                    /* 0xEC0 */
	virtual uintptr_t setOutputFlowControlled();                                   /* 0xEC8 */
	virtual uintptr_t clearOutputFlowControlled();                                 /* 0xED0 */
	virtual uintptr_t flushFlowQueues(ether_addr *);                               /* 0xED8 */
	virtual uintptr_t configureAQMOutput();                                        /* 0xEE0 */
	virtual uintptr_t attachToBpf();                                               /* 0xEE8 */
	virtual uintptr_t configureIfnet();                                            /* 0xEF0 */
	virtual uintptr_t reset();                                                     /* 0xEF8 */
	virtual uintptr_t dupAndTransmitMcastPacket(IO80211NetworkPacket *,
	                                            unsigned char *,
	                                            unsigned char *);                  /* 0xF00 */
	virtual uintptr_t freeMulticastPacket(IO80211NetworkPacket *);                 /* 0xF08 */
	virtual uintptr_t triggerAWDLMulticastTx(unsigned int, unsigned char);         /* 0xF10 */
	virtual uintptr_t p2pMulticastTx(IO80211NetworkPacket **, unsigned int);       /* 0xF18 */
	virtual uintptr_t p2pMulticastTx(unsigned int, bool);                          /* 0xF20 */
	virtual uintptr_t signalMulticastPackets(unsigned int, unsigned int);          /* 0xF28 */
	virtual uintptr_t sendPendingPacketsToStack(IO80211NetworkPacket **,
	                                            unsigned int);                     /* 0xF30 */
	virtual uintptr_t freePendingPackets(IO80211NetworkPacket **, unsigned int);   /* 0xF38 */
	virtual uintptr_t setMacAddress(ether_addr &) = 0;                             /* 0xF40 pure */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface0();                         /* 0xF48 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface1();                         /* 0xF50 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface2();                         /* 0xF58 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface3();                         /* 0xF60 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface4();                         /* 0xF68 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface5();                         /* 0xF70 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface6();                         /* 0xF78 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface7();                         /* 0xF80 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface8();                         /* 0xF88 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface9();                         /* 0xF90 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface10();                        /* 0xF98 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface11();                        /* 0xFA0 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface12();                        /* 0xFA8 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface13();                        /* 0xFB0 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface14();                        /* 0xFB8 */
	virtual uintptr_t _RESERVEDIO80211VirtualInterface15();                        /* 0xFC0 */
private:
	uint8_t _rtlAwdPad[0x8];   /* kernel collection size 0x130: 0x128 + 0x8 */
};

/* 63 AWDL slots, 0xFC8-0x11B8, in vtable order (Docs/AWDL_PURE_SLOTS.md). */
class IO80211AWDLProtocol : public IO80211VirtualInterface {
	OSDeclareAbstractStructors(IO80211AWDLProtocol)
public:
	virtual IOReturn getSSID(apple80211_ssid_data *) = 0;                            /* 0xFC8 */
	virtual IOReturn getCHANNEL(apple80211_channel_data *) = 0;                      /* 0xFD0 */
	virtual IOReturn getBSSID(apple80211_bssid_data *) = 0;                          /* 0xFD8 */
	virtual IOReturn getSTATE(apple80211_state_data *) = 0;                          /* 0xFE0 */
	virtual IOReturn getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) = 0;         /* 0xFE8 */
	virtual IOReturn getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) = 0;       /* 0xFF0 */
	virtual IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
	    apple80211_awdl_extension_state_machine_parameter *) = 0;                    /* 0xFF8 */
	virtual IOReturn getAWDL_ELECTION_ID(apple80211_awdl_election_id *) = 0;         /* 0x1000 */
	virtual IOReturn getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) = 0; /* 0x1008 */
	virtual IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
	    apple80211_awdl_sync_channel_sequence *) = 0;                                /* 0x1010 */
	virtual IOReturn getPEER_CACHE_MAXIMUM_SIZE(
	    apple80211_peer_cache_maximum_size *) = 0;                                   /* 0x1018 */
	virtual IOReturn getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) = 0;   /* 0x1020 */
	virtual IOReturn getAWDL_SYNC_STATE(apple80211_awdl_sync_state *) = 0;           /* 0x1028 */
	virtual IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(
	    apple80211_awdl_election_rssi_thresholds *) = 0;                             /* 0x1030 */
	virtual IOReturn getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) = 0;     /* 0x1038 */
	virtual IOReturn getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) = 0; /* 0x1040 */
	virtual IOReturn getAWDL_DEVICE_CAPABILITIES(
	    apple80211_awdl_device_capabilities *) = 0;                                  /* 0x1048 */
	virtual IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(
	    apple80211_awdl_secondary_master_channel *) = 0;                             /* 0x1050 */
	virtual IOReturn getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) = 0;           /* 0x1058 */
	virtual IOReturn getAWDL_MIN_RATE(apple80211_awdl_min_rate *) = 0;               /* 0x1060 */
	virtual IOReturn getAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) = 0;              /* 0x1068 */
	virtual IOReturn getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) = 0;       /* 0x1070 */
	virtual IOReturn getAWDL_DEV_STATS(apple80211_awdl_dev_stats *) = 0;             /* 0x1078 */
	virtual IOReturn getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) = 0;         /* 0x1080 */
	virtual IOReturn getAWDL_CCA(apple80211_awdl_cca *) = 0;                         /* 0x1088 */
	virtual IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(
	    apple80211_awdl_cap *) = 0;                                                  /* 0x1090 */
	virtual IOReturn getLLW_PARAMS(apple80211_llw_params *) = 0;                     /* 0x1098 */
	virtual IOReturn setSSID(apple80211_ssid_data *) = 0;                            /* 0x10A0 */
	virtual IOReturn setCHANNEL(apple80211_channel_data *) = 0;                      /* 0x10A8 */
	virtual IOReturn setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) = 0;         /* 0x10B0 */
	virtual IOReturn setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) = 0;       /* 0x10B8 */
	virtual IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
	    apple80211_awdl_extension_state_machine_parameter *) = 0;                    /* 0x10C0 */
	virtual IOReturn setAWDL_ELECTION_ID(apple80211_awdl_election_id *) = 0;         /* 0x10C8 */
	virtual IOReturn setAWDL_BSSID(apple80211_awdl_bssid *) = 0;                     /* 0x10D0 */
	virtual IOReturn setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) = 0; /* 0x10D8 */
	virtual IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
	    apple80211_awdl_sync_channel_sequence *) = 0;                                /* 0x10E0 */
	virtual IOReturn setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) = 0;   /* 0x10E8 */
	virtual IOReturn setAWDL_SYNC_STATE(apple80211_awdl_sync_state *) = 0;           /* 0x10F0 */
	virtual IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(
	    apple80211_awdl_election_rssi_thresholds *) = 0;                             /* 0x10F8 */
	virtual IOReturn setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) = 0;     /* 0x1100 */
	virtual IOReturn setPEER_CACHE_CONTROL(apple80211_peer_cache_control *) = 0;     /* 0x1108 */
	virtual IOReturn setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *) = 0;         /* 0x1110 */
	virtual IOReturn setAWDL_SYNC_FRAME_TEMPLATE(
	    apple80211_awdl_sync_frame_template *) = 0;                                  /* 0x1118 */
	virtual IOReturn setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) = 0; /* 0x1120 */
	virtual IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(
	    apple80211_awdl_peer_traffic_registration *) = 0;                            /* 0x1128 */
	virtual IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(
	    apple80211_awdl_secondary_master_channel *) = 0;                             /* 0x1130 */
	virtual IOReturn setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) = 0;           /* 0x1138 */
	virtual IOReturn setAWDL_MIN_RATE(apple80211_awdl_min_rate *) = 0;               /* 0x1140 */
	virtual IOReturn setAWDL_ENCRYPTION_KEYS(apple80211_key *) = 0;                  /* 0x1148 */
	virtual IOReturn setAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) = 0;              /* 0x1150 */
	virtual IOReturn setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *) = 0;    /* 0x1158 */
	virtual IOReturn setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) = 0;       /* 0x1160 */
	virtual IOReturn setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add *) = 0;           /* 0x1168 */
	virtual IOReturn setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) = 0;         /* 0x1170 */
	virtual IOReturn setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config *) = 0; /* 0x1178 */
	virtual IOReturn setCOMPANION_SKYWALK_IF_CREATE(
	    apple80211_companion_skywalk_if_create *) = 0;                               /* 0x1180 */
	virtual IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(
	    apple80211_awdl_af_sec_payload_request *) = 0;                               /* 0x1188 */
	virtual IOReturn setLLW_PARAMS(apple80211_llw_params *) = 0;                     /* 0x1190 */
	virtual IOReturn setTX_RATE(apple80211_tx_rate *) = 0;                           /* 0x1198 */
	virtual IOReturn setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection *) = 0;   /* 0x11A0 */
	virtual IOReturn setPSF_DWELL(apple80211_awdl_psf_dwell_params *) = 0;           /* 0x11A8 */
	virtual IOReturn setAWDL_CONFIG(apple80211_awdl_config_request *) = 0;           /* 0x11B0 */
	virtual IOReturn setAWDL_RTG_PEER_STATS_CONFIG(
	    apple80211_rtg_peer_stats_cfg *) = 0;                                        /* 0x11B8 */
};

static_assert(sizeof(IO80211SkywalkInterface) == 0x128, "IO80211SkywalkInterface layout");
static_assert(sizeof(IO80211VirtualInterface) == 0x130, "IO80211VirtualInterface layout");
static_assert(sizeof(IO80211AWDLProtocol) == 0x130, "IO80211AWDLProtocol layout");

/*
 * awdl0. Role 6, name "awdl": the numeric role and the BSD prefix are the ones
 * AppleBCMWLANProximityInterface passes to IO80211VirtualInterface::init.
 * Every AWDL slot answers kIOReturnUnsupported in stage 1: the interface exists
 * and is published, it does not run the AWDL state machine yet.
 */
class RTLAwdInterface : public IO80211AWDLProtocol {
	OSDeclareDefaultStructors(RTLAwdInterface)
public:
	bool initWithController(RTLAirController *ctrl, const uint8_t mac[6]);
	bool startWithRegistration(RTLAirController *ctrl);
	void stop(IOService *provider) override;
	void free() override;
	const char *bsdName() const { return _bsdName; }

	uintptr_t setMacAddress(ether_addr &) override { return kIOReturnSuccess; }
	uintptr_t enable(unsigned int opts) override;
	uintptr_t disable(unsigned int opts) override;

	/* Stage 3: the setters IO80211AWDLPeerManager::configure() sends through
	 * ioctl_internal (types 125, 122, 132, 135, 213, 134) and the ones interfaceStateChange()/enableAWDL() send after it (116, 117, 226) and the setters of the channel sequence, election, sync frame template, secondary master, AF TX mode, advertisers, extension state machine and PSF dwell (129, 155, 151, 205, 208, 229, 126, 118, 418). It aborts on the first
	 * one that fails, which leaves _stateFlags bit 0 clear and every peer service
	 * request answered with 6. They only acknowledge: nothing reaches the radio. */
#define AWDL_ACK(name, type) IOReturn name(type *) override;
	AWDL_ACK(setAWDL_BSSID, apple80211_awdl_bssid)
	AWDL_ACK(setAWDL_ELECTION_ID, apple80211_awdl_election_id)
	AWDL_ACK(setAWDL_MASTER_CHANNEL, apple80211_awdl_master_channel)
	AWDL_ACK(setAWDL_ELECTION_RSSI_THRESHOLDS,
	         apple80211_awdl_election_rssi_thresholds)
	AWDL_ACK(setAWDL_MIN_RATE, apple80211_awdl_min_rate)
	AWDL_ACK(setAWDL_SYNC_STATE, apple80211_awdl_sync_state)
	AWDL_ACK(setAWDL_SYNC_PARAMS, apple80211_awdl_sync_params)
	AWDL_ACK(setAWDL_SYNC_ENABLED, apple80211_awdl_sync_enabled)
	AWDL_ACK(setAWDL_TXCAL_PERIOD, apple80211_awdl_txcal_period)
	AWDL_ACK(setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE,
	         apple80211_awdl_sync_channel_sequence)
	AWDL_ACK(setAWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
	/* Also keeps a copy of the template for the action frame TX. */
	AWDL_ACK(setAWDL_SYNC_FRAME_TEMPLATE,
	         apple80211_awdl_sync_frame_template)
	AWDL_ACK(setAWDL_SECONDARY_MASTER_CHANNEL,
	         apple80211_awdl_secondary_master_channel)
	AWDL_ACK(setAWDL_AF_TX_MODE, apple80211_awdl_af_tx_mode)
	AWDL_ACK(setAWDL_AWDL_ADVERTISERS, awdl_peer_advert_add)
	AWDL_ACK(setAWDL_ELECTION_METRIC, apple80211_awdl_election_metric)
	AWDL_ACK(setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS,
	         apple80211_awdl_extension_state_machine_parameter)
	AWDL_ACK(setPSF_DWELL, apple80211_awdl_psf_dwell_params)
	/* The master indication template (Docs/AWDL_PSF_MIF_EMISSION.md §2.2,
	 * §2.3): 149 sends one MIF (broadcast) or UMI (unicast) in the next
	 * window, 225 keeps the template for every MIF. */
	AWDL_ACK(setAWDL_OOB_REQUEST, apple80211_awdl_oob_request)
	AWDL_ACK(setAWDL_OOB_AUTO_REQUEST, apple80211_awdl_oob_request)
#undef AWDL_ACK

#define AWDL_STUB(name, type) \
	IOReturn name(type *) override { return kIOReturnUnsupported; }
	AWDL_STUB(getSSID, apple80211_ssid_data)
	AWDL_STUB(getCHANNEL, apple80211_channel_data)
	AWDL_STUB(getBSSID, apple80211_bssid_data)
	AWDL_STUB(getSTATE, apple80211_state_data)
	AWDL_STUB(getAWDL_SYNC_PARAMS, apple80211_awdl_sync_params)
	AWDL_STUB(getAWDL_SYNC_ENABLED, apple80211_awdl_sync_enabled)
	AWDL_STUB(getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS,
	          apple80211_awdl_extension_state_machine_parameter)
	AWDL_STUB(getAWDL_ELECTION_ID, apple80211_awdl_election_id)
	AWDL_STUB(getAWDL_ELECTION_METRIC, apple80211_awdl_election_metric)
	AWDL_STUB(getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE,
	          apple80211_awdl_sync_channel_sequence)
	AWDL_STUB(getPEER_CACHE_MAXIMUM_SIZE, apple80211_peer_cache_maximum_size)
	AWDL_STUB(getAWDL_MASTER_CHANNEL, apple80211_awdl_master_channel)
	AWDL_STUB(getAWDL_SYNC_STATE, apple80211_awdl_sync_state)
	AWDL_STUB(getAWDL_ELECTION_RSSI_THRESHOLDS,
	          apple80211_awdl_election_rssi_thresholds)
	AWDL_STUB(getAWDL_PRESENCE_MODE, apple80211_awdl_presence_mode)
	AWDL_STUB(getAWDL_ELECTION_PARAMS, apple80211_awdl_election_params)
	AWDL_STUB(getAWDL_DEVICE_CAPABILITIES, apple80211_awdl_device_capabilities)
	AWDL_STUB(getAWDL_SECONDARY_MASTER_CHANNEL,
	          apple80211_awdl_secondary_master_channel)
	AWDL_STUB(getAWDL_AF_TX_MODE, apple80211_awdl_af_tx_mode)
	AWDL_STUB(getAWDL_MIN_RATE, apple80211_awdl_min_rate)
	AWDL_STUB(getAWDL_ENCRYPTION_TYPE, apple80211_encrypt)
	AWDL_STUB(getAWDL_TXCAL_PERIOD, apple80211_awdl_txcal_period)
	AWDL_STUB(getAWDL_DEV_STATS, apple80211_awdl_dev_stats)
	AWDL_STUB(getAWDL_DFSP_CONFIG, apple80211_awdl_dfsp_config)
	AWDL_STUB(getAWDL_CCA, apple80211_awdl_cca)
	AWDL_STUB(getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES, apple80211_awdl_cap)
	AWDL_STUB(getLLW_PARAMS, apple80211_llw_params)
	AWDL_STUB(setSSID, apple80211_ssid_data)
	AWDL_STUB(setCHANNEL, apple80211_channel_data)
	AWDL_STUB(setAWDL_PRESENCE_MODE, apple80211_awdl_presence_mode)
	AWDL_STUB(setPEER_CACHE_CONTROL, apple80211_peer_cache_control)
	AWDL_STUB(setAWDL_PEER_TRAFFIC_REGISTRATION,
	          apple80211_awdl_peer_traffic_registration)
	AWDL_STUB(setAWDL_ENCRYPTION_KEYS, apple80211_key)
	AWDL_STUB(setAWDL_ENCRYPTION_TYPE, apple80211_encrypt)
	AWDL_STUB(setAWDL_DFSP_CONFIG, apple80211_awdl_dfsp_config)
	AWDL_STUB(setAWDL_DFSP_UCSA_CONFIG, apple80211_awdl_dfsp_ucsa_config)
	AWDL_STUB(setCOMPANION_SKYWALK_IF_CREATE,
	          apple80211_companion_skywalk_if_create)
	AWDL_STUB(setAWDL_OOB_SEC_PAYLOAD_REQUEST,
	          apple80211_awdl_af_sec_payload_request)
	AWDL_STUB(setLLW_PARAMS, apple80211_llw_params)
	AWDL_STUB(setTX_RATE, apple80211_tx_rate)
	AWDL_STUB(setP2P_COEX_PROTECTION, apple80211_p2p_coex_protection)
	AWDL_STUB(setAWDL_CONFIG, apple80211_awdl_config_request)
	AWDL_STUB(setAWDL_RTG_PEER_STATS_CONFIG, apple80211_rtg_peer_stats_cfg)
#undef AWDL_STUB

private:
	RTLAirController *_ctrl;
	void *_txPool, *_rxPool;
	void *_txSubQ, *_txCompQ, *_rxCompQ;
	uint8_t *_regInfo;          /* IOSkywalkEthernetInterface::RegistrationInfo */
	uint8_t _mac[6];
	char _bsdName[16];

	bool createDatapath();
	void teardownDatapath();
	static IOReturn txData(OSObject *owner, void *queue, void *const *pkts,
	                       uint32_t count, void *refCon);
	static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);

	/* RX of AWDL frames from the radio (RTL_RADIO_FN_AWDL_HOOK): the hook
	 * queues copies, a thread call hands the action frames to the peer
	 * manager through bpfTapInput on the work queue. */
	IOLock *_rxLock;
	mbuf_t _rxHead, _rxTail;
	uint32_t _rxCount;
	thread_call_t _rxDrain;
	bool _rxHooked;
	bool _rxAllowed;            /* boot-arg rtlawdl_rx, 1 unless set to 0 */
	uint64_t _rxAf, _rxData, _rxOther, _rxDropped;
	uint64_t _rxDataUp, _rxDataBad, _rxDataNotUs, _rxDataFail;

	void setRadioRx(bool on);
	void flushRadioRx();
	void injectFrame(mbuf_t m);
	void deliverData(mbuf_t m);
	static void radioRxHook(void *ctx, const uint8_t *frame, uint32_t len,
	                        int8_t rssi, uint8_t channel);
	static void rxDrainEntry(thread_call_param_t p0, thread_call_param_t);
	static IOReturn bpfTapAction(OSObject *owner, void *m, void *hdr, void *, void *);

	/* TX of AWDL action frames and the availability windows (boot-arg
	 * rtlawdl_tx=1): PSF every 110 TU and a MIF in each served slot, built
	 * from the kernel's sync frame template (151) plus the TLVs 4, 5 and 18
	 * rendered here; events 41/43 at the edges of the served slots. */
	IOService *_radioSvc;       /* the nub's provider, as the controller's */
	const OSSymbol *_symTx80211;
	IOLock *_txLock;            /* template and clock */
	thread_call_t _txTick;
	bool _txAllowed;
	bool _txOn;
	bool _tickBusy;             /* a txTick is running; it rearms itself */
	uint8_t _tpl[512];
	uint32_t _tplLen;
	uint8_t _miTpl[1200];       /* MI template of the last 149/225, the kernel's size */
	uint32_t _miTplLen;
	bool _mifNow;               /* a broadcast 149: one MIF in the next window */
	uint8_t _umiDst[4][6];      /* unicast 149s waiting for the next window */
	uint8_t _umiCount;
	uint64_t _txUmi, _txUmiDrop;
	uint8_t _afBuf[1488];       /* sendAf, tick only: the radio's frame limit */
	uint64_t _clkBaseUs;        /* start of the slot whose first AW is _clkBaseAw */
	uint16_t _clkBaseAw;
	uint64_t _nextPsfUs;
	uint32_t _lastMifSlot;
	uint32_t _lastTickAw;
	uint8_t _seq[16];           /* channel per slot from the 129 */
	uint32_t _seqFlags[16];
	bool _seqValid;
	uint8_t _seqLogs;           /* sequence changes logged so far */
	bool _awOpen;              /* between events 41 and 43 */
	uint8_t _offSocialCh;       /* last non-social channel logged */
	uint8_t _parkCh;            /* preferred social channel while not associated
	                             * (boot-arg rtlawdl_ch, 149 by default), used until
	                             * a 129 names another; 0 = no parking */
	uint64_t _parkReqUs;        /* last rtlRadioSetChannel request */
	uint8_t _clkMaster[6];      /* master the AW clock follows (alignClock) */
	uint64_t _clkMasterUs;      /* last frame from it, 0 = none */

	/* AWDL data TX: frames built from awdl0's TX submission packets wait in a
	 * ring until the window of a served slot is open (txTick). Under _txLock;
	 * the radio is only called with it dropped. */
	struct AwdlTxFrame {
		uint32_t tsUs;          /* low 32 bits of awdlNowUs() when queued */
		uint16_t len;
		uint8_t frame[24 + 16 + 1428];  /* 802.11 + AWDL data header + MTU */
	};
	struct AwdlTxSeq {
		uint8_t da[6];
		uint16_t seq;
		uint64_t lastUs;
	};
	AwdlTxFrame *_txRing;
	uint32_t _txRingHead, _txRingCount;
	AwdlTxSeq _txSeqTab[16];
	bool _txDataOff;            /* the radio refused a data frame (older driver) */
	bool _port1On;              /* rtlRadioAwdlPort took awdl0's MAC */
	uint64_t _txDataQueued, _txDataSent, _txDataDropFull, _txDataDropOld, _txDataDropBad,
	         _txDataFail;

	void queueData(const uint8_t *eth, uint32_t len);
	void sendData();
	void flushRadioTx();
	uint64_t _txPsf, _txMif, _txFail, _clkAligned, _ev41, _ev43;

	void setRadioTx(bool on);
	void txTick();
	void armTick(uint64_t deadline);
	uint8_t parkRadio(uint8_t idleCh, uint64_t nowUs);
	uint16_t servedMask(uint8_t ch) const;
	bool sendAf(uint8_t subtype, uint8_t channel, uint16_t mask, const uint8_t *da = nullptr);
	size_t buildAf(uint8_t *out, size_t cap, uint8_t subtype, uint8_t channel, uint16_t mask,
	               const uint8_t *da);
	bool copyMiTemplate(const uint8_t *req);
	void alignClock(mbuf_t m, const uint8_t *src);
	static void txTickEntry(thread_call_param_t p0, thread_call_param_t);
};

/* Fails to compile if a pure slot is left unanswered, which would put
 * __cxa_pure_virtual in the vtable the family calls. */
static_assert(__is_abstract(IO80211AWDLProtocol), "IO80211AWDLProtocol is abstract");
static_assert(!__is_abstract(RTLAwdInterface), "RTLAwdInterface must override every pure slot");

/* IO80211Controller::setAwdlInterface, wrapped so the handler that creates the
 * interface does not carry the family symbol. NULL unpublishes it. */
IOReturn rtlAwdlPublish(IO80211Controller *ctrl, RTLAwdInterface *awdl);

#endif /* RTL_AWDL_INTERFACE_H */
