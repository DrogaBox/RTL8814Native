/*
 * Sparse AWDL signature declaration for the macOS 25.6.0 KC.
 * IMPORTANT: slots 2–489 and 491–506 are NOT declared here; the instance-ivar
 * layout is unknown. Therefore this is a syntax/signature catalog, not a
 * complete ABI-compatible vtable declaration. Slot comments identify the KC
 * indices documented in Docs/AWDL_PURE_SLOTS.md; C++ cannot preserve those
 * sparse indices without the omitted declarations.
 */
#pragma once

#include "IO80211_25_6_0.h"

/* Apple80211 AWDL parameter structures are intentionally opaque. */
struct apple80211_ssid_data;
struct apple80211_bssid_data;
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
struct apple80211_encrypt;
struct apple80211_awdl_txcal_period;
struct apple80211_awdl_dev_stats;
struct apple80211_awdl_dfsp_config;
struct apple80211_awdl_cca;
struct apple80211_awdl_cap;
struct apple80211_llw_params;
struct apple80211_awdl_bssid;
struct apple80211_peer_cache_control;
struct apple80211_awdl_oob_request;
struct apple80211_awdl_sync_frame_template;
struct apple80211_awdl_peer_traffic_registration;
struct awdl_peer_advert_add;
struct apple80211_awdl_dfsp_ucsa_config;
struct apple80211_companion_skywalk_if_create;
struct apple80211_awdl_af_sec_payload_request;
struct apple80211_tx_rate;
struct apple80211_p2p_coex_protection;
struct apple80211_awdl_psf_dwell_params;
struct apple80211_awdl_config_request;
struct apple80211_rtg_peer_stats_cfg;

/*
 * The KC metaclass registers IO80211VirtualInterface with size 0x130 and
 * IO80211SkywalkInterface as its superclass (awdl_types_probe.log:69–85).
 * Only its pure slot 490 is declared here; all intervening virtual declarations
 * are intentionally omitted as noted above.
 */
class IO80211VirtualInterface : public IO80211SkywalkInterface {
    OSDeclareAbstractStructors(IO80211VirtualInterface)
public:
    virtual uintptr_t setMacAddress(ether_addr&) = 0;  // slot 490
private:
    uint8_t _awdl_abi_size_padding[0x8]; // size observation only; ivar layout unknown
};
static_assert(sizeof(IO80211VirtualInterface) == 0x130, "IO80211VirtualInterface KC metaclass size");

/*
 * IO80211AWDLProtocol derives from IO80211VirtualInterface. Its KC metaclass
 * also declares size 0x130 (awdl_types_probe.log:59–65); no additional ivars
 * are inferred. Return types are not encoded in Itanium mangled names; the
 * Apple80211 handlers use IOReturn consistently in the local protocol header.
 */
class IO80211AWDLProtocol : public IO80211VirtualInterface {
    OSDeclareAbstractStructors(IO80211AWDLProtocol)
public:
    virtual IOReturn getSSID(apple80211_ssid_data*) = 0;  // slot 507
    virtual IOReturn getCHANNEL(apple80211_channel_data*) = 0;  // slot 508
    virtual IOReturn getBSSID(apple80211_bssid_data*) = 0;  // slot 509
    virtual IOReturn getSTATE(apple80211_state_data*) = 0;  // slot 510
    virtual IOReturn getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*) = 0;  // slot 511
    virtual IOReturn getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*) = 0;  // slot 512
    virtual IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*) = 0;  // slot 513
    virtual IOReturn getAWDL_ELECTION_ID(apple80211_awdl_election_id*) = 0;  // slot 514
    virtual IOReturn getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*) = 0;  // slot 515
    virtual IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*) = 0;  // slot 516
    virtual IOReturn getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size*) = 0;  // slot 517
    virtual IOReturn getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*) = 0;  // slot 518
    virtual IOReturn getAWDL_SYNC_STATE(apple80211_awdl_sync_state*) = 0;  // slot 519
    virtual IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*) = 0;  // slot 520
    virtual IOReturn getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*) = 0;  // slot 521
    virtual IOReturn getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*) = 0;  // slot 522
    virtual IOReturn getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities*) = 0;  // slot 523
    virtual IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*) = 0;  // slot 524
    virtual IOReturn getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*) = 0;  // slot 525
    virtual IOReturn getAWDL_MIN_RATE(apple80211_awdl_min_rate*) = 0;  // slot 526
    virtual IOReturn getAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) = 0;  // slot 527
    virtual IOReturn getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) = 0;  // slot 528
    virtual IOReturn getAWDL_DEV_STATS(apple80211_awdl_dev_stats*) = 0;  // slot 529
    virtual IOReturn getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) = 0;  // slot 530
    virtual IOReturn getAWDL_CCA(apple80211_awdl_cca*) = 0;  // slot 531
    virtual IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap*) = 0;  // slot 532
    virtual IOReturn getLLW_PARAMS(apple80211_llw_params*) = 0;  // slot 533
    virtual IOReturn setSSID(apple80211_ssid_data*) = 0;  // slot 534
    virtual IOReturn setCHANNEL(apple80211_channel_data*) = 0;  // slot 535
    virtual IOReturn setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*) = 0;  // slot 536
    virtual IOReturn setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*) = 0;  // slot 537
    virtual IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*) = 0;  // slot 538
    virtual IOReturn setAWDL_ELECTION_ID(apple80211_awdl_election_id*) = 0;  // slot 539
    virtual IOReturn setAWDL_BSSID(apple80211_awdl_bssid*) = 0;  // slot 540
    virtual IOReturn setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*) = 0;  // slot 541
    virtual IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*) = 0;  // slot 542
    virtual IOReturn setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*) = 0;  // slot 543
    virtual IOReturn setAWDL_SYNC_STATE(apple80211_awdl_sync_state*) = 0;  // slot 544
    virtual IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*) = 0;  // slot 545
    virtual IOReturn setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*) = 0;  // slot 546
    virtual IOReturn setPEER_CACHE_CONTROL(apple80211_peer_cache_control*) = 0;  // slot 547
    virtual IOReturn setAWDL_OOB_REQUEST(apple80211_awdl_oob_request*) = 0;  // slot 548
    virtual IOReturn setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template*) = 0;  // slot 549
    virtual IOReturn setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*) = 0;  // slot 550
    virtual IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration*) = 0;  // slot 551
    virtual IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*) = 0;  // slot 552
    virtual IOReturn setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*) = 0;  // slot 553
    virtual IOReturn setAWDL_MIN_RATE(apple80211_awdl_min_rate*) = 0;  // slot 554
    virtual IOReturn setAWDL_ENCRYPTION_KEYS(apple80211_key*) = 0;  // slot 555
    virtual IOReturn setAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) = 0;  // slot 556
    virtual IOReturn setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request*) = 0;  // slot 557
    virtual IOReturn setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) = 0;  // slot 558
    virtual IOReturn setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add*) = 0;  // slot 559
    virtual IOReturn setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) = 0;  // slot 560
    virtual IOReturn setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config*) = 0;  // slot 561
    virtual IOReturn setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create*) = 0;  // slot 562
    virtual IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(apple80211_awdl_af_sec_payload_request*) = 0;  // slot 563
    virtual IOReturn setLLW_PARAMS(apple80211_llw_params*) = 0;  // slot 564
    virtual IOReturn setTX_RATE(apple80211_tx_rate*) = 0;  // slot 565
    virtual IOReturn setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection*) = 0;  // slot 566
    virtual IOReturn setPSF_DWELL(apple80211_awdl_psf_dwell_params*) = 0;  // slot 567
    virtual IOReturn setAWDL_CONFIG(apple80211_awdl_config_request*) = 0;  // slot 568
    virtual IOReturn setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg*) = 0;  // slot 569
};
static_assert(sizeof(IO80211AWDLProtocol) == 0x130, "IO80211AWDLProtocol KC metaclass size");
