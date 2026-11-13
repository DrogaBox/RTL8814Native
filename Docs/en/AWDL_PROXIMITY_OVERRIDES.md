# Complete inventory of AppleBCMWLANProximityInterface overrides + linkability for a custom subclass

Design input for building a real AWDL interface from a custom kext subclassing `IO80211AWDLProtocol` as `AppleBCMWLANProximityInterface` does. Two parts: (1) full diff of the 572 slots of `__ZTV30AppleBCMWLANProximityInterface` against the 569 of `__ZTV19IO80211AWDLProtocol`, without sampling; (2) availability of the four symbols a subclass needs. Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`) read from the file + rea/IDA attachment + Info.plists on disk. Unverified items go as `?`.

**Corrections with respect to `AWDL_PROXIMITY_VTABLE_HIERARCHY.md`** (this doc replaces those numbers):

- The repeated "stub" `0xffffff8000bb59c0` **has a name**: `___cxa_pure_virtual` (entry in the symtab of the kernel embedded in the KC, n_type `0x0f` external, exact value). The slots AWDL does not implement are **pure virtuals**.
- The 95 "overrides" of that doc are in fact **93**: two (+0x480, +0xba8) were the same function with a different fixup shape (rebase vs bind; the low32 decodes to the same target). With uniform decoding they are 93.
- The "63 new slots" of AWDL are **62 pure-virtual + 1 final NULL** (+0x11C0); additionally AWDL fills with pure-virtual the final NULL that `IO80211VirtualInterface` had at +0xFC8. The 23 slots that doc did not verify individually were all verified here.

## 1) Numbers

| vtable | range | vptr | slots |
|---|---|---|---|
| `__ZTV23IO80211VirtualInterface` | `0x2472b78–0x2473b58` | `0x2472b88` | 506 |
| `__ZTV19IO80211AWDLProtocol` | `0x2456440–0x2457618` | `0x2456450` | 569 |
| `__ZTV30AppleBCMWLANProximityInterface` | `0x17f5b68–0x17f6d58` | `0x17f5b78` | 572 |

- 569 common slots AWDL/Proxi; **93 differ**; **3 new slots** at the end of Proxi.
- **64 pure-virtual slots** in AWDL (62 added +0xFD0–0x11B8, plus +0xF40 and +0xFC8 inherited from the VI region): **Proxi implements all of them — it leaves none** (`tot_stub_proxi = 0`; counted verification, not sampled).
- `IO80211AWDLProtocol` contributes **no** own behavior implementation: only identity (`D1`/`D0`/`getMetaClass`) and 62 new pure-virtuals. It is a 100% abstract-in-behavior protocol class (matches classSize 0x130 = the one of VI: no own ivars).
- `___cxa_pure_virtual` implies **a subclass cannot be instantiated without implementing the 64**: any call to an unimplemented slot traps. That is the minimum surface a custom kext must cover.

## 2) The 93 overrides, one by one

Organizing categories (they do not come from the binary). Column "IO80211AWDLProtocol target": what was in that slot before the override; "pure" = `___cxa_pure_virtual` at `0xffffff8000bb59c0`.

### 2.1 Object identity (3)

| vptr offset (slot) | AWDL target | Proxi (mangled) | Proxi (demangled) | addr |
|---|---|---|---|---|
| +0x0000 (0) | `__ZN19IO80211AWDLProtocolD1Ev` @`0xffffff800239ef24` | `__ZN30AppleBCMWLANProximityInterfaceD1Ev` | AppleBCMWLANProximityInterface::~AppleBCMWLANProximityInterface() | `0xffffff800171595e` |
| +0x0008 (1) | `__ZN19IO80211AWDLProtocolD0Ev` @`0xffffff800239ef2a` | `__ZN30AppleBCMWLANProximityInterfaceD0Ev` | AppleBCMWLANProximityInterface::~AppleBCMWLANProximityInterface() | `0xffffff8001715968` |
| +0x0038 (7) | `__ZNK19IO80211AWDLProtocol12getMetaClassEv` @`0xffffff8002198c2e` | `__ZNK30AppleBCMWLANProximityInterface12getMetaClassEv` | AppleBCMWLANProximityInterface::getMetaClass() const | `0xffffff8001715990` |

### 2.2 IOKit lifecycle (8)

| vptr offset (slot) | AWDL target | Proxi (mangled) | Proxi (demangled) | addr |
|---|---|---|---|---|
| +0x0088 (17) | `__ZN23IO80211SkywalkInterface4initEv` @`0xffffff80022f568e` | `__ZN30AppleBCMWLANProximityInterface4initEv` | AppleBCMWLANProximityInterface::init() | `0xffffff8001718d72` |
| +0x0090 (18) | `__ZN23IO80211VirtualInterface4freeEv` @`0xffffff8002330b8c` | `__ZN30AppleBCMWLANProximityInterface4freeEv` | AppleBCMWLANProximityInterface::free() | `0xffffff8001719582` |
| +0x05c0 (184) | `__ZN23IO80211VirtualInterface5startEP9IOService` @`0xffffff80023308aa` | `__ZN30AppleBCMWLANProximityInterface5startEP9IOService` | AppleBCMWLANProximityInterface::start(IOService*) | `0xffffff8001729348` |
| +0x05c8 (185) | `__ZN23IO80211VirtualInterface4stopEP9IOService` @`0xffffff8002330cee` | `__ZN30AppleBCMWLANProximityInterface4stopEP9IOService` | AppleBCMWLANProximityInterface::stop(IOService*) | `0xffffff80017195e6` |
| +0x0850 (266) | `__ZN23IO80211VirtualInterface6enableEj` @`0xffffff80023308b4` | `__ZN30AppleBCMWLANProximityInterface6enableEj` | AppleBCMWLANProximityInterface::enable(unsigned int) | `0xffffff800172a198` |
| +0x0858 (267) | `__ZN23IO80211SkywalkInterface7disableEj` @`0xffffff80022f9922` | `__ZN30AppleBCMWLANProximityInterface7disableEj` | AppleBCMWLANProximityInterface::disable(unsigned int) | `0xffffff800172a2c2` |
| +0x0930 (294) | `__ZN23IO80211VirtualInterface18setInterfaceEnableEb` @`0xffffff8002331ba6` | `__ZN30AppleBCMWLANProximityInterface18setInterfaceEnableEb` | AppleBCMWLANProximityInterface::setInterfaceEnable(bool) | `0xffffff80017298c2` |
| +0x0ef8 (479) | `__ZN23IO80211VirtualInterface5resetEv` @`0xffffff8002334bea` | `__ZN30AppleBCMWLANProximityInterface5resetEv` | AppleBCMWLANProximityInterface::reset() | `0xffffff80017288a8` |

### 2.3 Skywalk datapath (17)

| vptr offset (slot) | AWDL target | Proxi (mangled) | Proxi (demangled) | addr |
|---|---|---|---|---|
| +0x0a98 (339) | `__ZN23IO80211SkywalkInterface24setPromiscuousModeEnableEbj` @`0xffffff80022f8fdc` | `__ZN30AppleBCMWLANProximityInterface24setPromiscuousModeEnableEbj` | AppleBCMWLANProximityInterface::setPromiscuousModeEnable(bool, unsigned int) | `0xffffff80017298b4` |
| +0x0b78 (367) | `__ZN23IO80211SkywalkInterface20isChipInterfaceReadyEv` @`0xffffff80022f953a` | `__ZN30AppleBCMWLANProximityInterface20isChipInterfaceReadyEv` | AppleBCMWLANProximityInterface::isChipInterfaceReady() | `0xffffff80017231c0` |
| +0x0c58 (395) | `__ZN23IO80211VirtualInterface13printDataPathEP12userPrintCtx` @`0xffffff8002334344` | `__ZN30AppleBCMWLANProximityInterface13printDataPathEP12userPrintCtx` | AppleBCMWLANProximityInterface::printDataPath(userPrintCtx*) | `0xffffff800172a3fc` |
| +0x0d08 (417) | `__ZNK23IO80211SkywalkInterface9getLoggerEv` @`0xffffff80022f9744` | `__ZNK30AppleBCMWLANProximityInterface9getLoggerEv` | AppleBCMWLANProximityInterface::getLogger() const | `0xffffff800172a360` |
| +0x0d70 (430) | `__ZN23IO80211SkywalkInterface14getRxCompQueueEv` @`0xffffff80022f98cc` | `__ZN30AppleBCMWLANProximityInterface14getRxCompQueueEv` | AppleBCMWLANProximityInterface::getRxCompQueue() | `0xffffff800172a3c8` |
| +0x0d78 (431) | `__ZN23IO80211SkywalkInterface14getTxCompQueueEv` @`0xffffff80022f98d4` | `__ZN30AppleBCMWLANProximityInterface14getTxCompQueueEv` | AppleBCMWLANProximityInterface::getTxCompQueue() | `0xffffff800172a3b4` |
| +0x0d80 (432) | `__ZN23IO80211SkywalkInterface13getTxSubQueueE17apple80211_wme_ac` @`0xffffff80022f98dc` | `__ZN30AppleBCMWLANProximityInterface13getTxSubQueueE17apple80211_wme_ac` | AppleBCMWLANProximityInterface::getTxSubQueue(apple80211_wme_ac) | `0xffffff800172a372` |
| +0x0d88 (433) | `__ZN23IO80211SkywalkInterface15getTxPacketPoolEv` @`0xffffff80022f98e4` | `__ZN30AppleBCMWLANProximityInterface15getTxPacketPoolEv` | AppleBCMWLANProximityInterface::getTxPacketPool() | `0xffffff800172a3a0` |
| +0x0d98 (435) | `__ZN23IO80211SkywalkInterface14enableDatapathEv` @`0xffffff80022f98f4` | `__ZN30AppleBCMWLANProximityInterface14enableDatapathEv` | AppleBCMWLANProximityInterface::enableDatapath() | `0xffffff8001729afc` |
| +0x0da0 (436) | `__ZN23IO80211SkywalkInterface15disableDatapathEv` @`0xffffff80022f98fc` | `__ZN30AppleBCMWLANProximityInterface15disableDatapathEv` | AppleBCMWLANProximityInterface::disableDatapath() | `0xffffff8001729ec6` |
| +0x0ed8 (475) | `__ZN23IO80211VirtualInterface15flushFlowQueuesEP10ether_addr` @`0xffffff800233235e` | `__ZN30AppleBCMWLANProximityInterface15flushFlowQueuesEP10ether_addr` | AppleBCMWLANProximityInterface::flushFlowQueues(ether_addr*) | `0xffffff800172ac9e` |
| +0x0f08 (481) | `__ZN23IO80211VirtualInterface19freeMulticastPacketEP20IO80211NetworkPacket` @`0xffffff8002334d5a` | `__ZN30AppleBCMWLANProximityInterface19freeMulticastPacketEP20IO80211NetworkPacket` | AppleBCMWLANProximityInterface::freeMulticastPacket(IO80211NetworkPacket*) | `0xffffff800172a6f6` |
| +0x0f10 (482) | `__ZN23IO80211VirtualInterface22triggerAWDLMulticastTxEjh` @`0xffffff8002334d62` | `__ZN30AppleBCMWLANProximityInterface22triggerAWDLMulticastTxEjh` | AppleBCMWLANProximityInterface::triggerAWDLMulticastTx(unsigned int, unsigned char) | `0xffffff800172a3dc` |
| +0x0f18 (483) | `__ZN23IO80211VirtualInterface14p2pMulticastTxEPP20IO80211NetworkPacketj` @`0xffffff8002334d6a` | `__ZN30AppleBCMWLANProximityInterface14p2pMulticastTxEPP20IO80211NetworkPacketj` | AppleBCMWLANProximityInterface::p2pMulticastTx(IO80211NetworkPacket**, unsigned int) | `0xffffff800172a6d6` |
| +0x0f30 (486) | `__ZN23IO80211VirtualInterface25sendPendingPacketsToStackEPP20IO80211NetworkPacketj` @`0xffffff8002334e68` | `__ZN30AppleBCMWLANProximityInterface25sendPendingPacketsToStackEPP20IO80211NetworkPacketj` | AppleBCMWLANProximityInterface::sendPendingPacketsToStack(IO80211NetworkPacket**, unsigned int) | `0xffffff800172a716` |
| +0x0f38 (487) | `__ZN23IO80211VirtualInterface18freePendingPacketsEPP20IO80211NetworkPacketj` @`0xffffff8002334e88` | `__ZN30AppleBCMWLANProximityInterface18freePendingPacketsEPP20IO80211NetworkPacketj` | AppleBCMWLANProximityInterface::freePendingPackets(IO80211NetworkPacket**, unsigned int) | `0xffffff800172a812` |
| +0x0f40 (488) | pure @`0xffffff8000bb59c0` | `__ZN30AppleBCMWLANProximityInterface13setMacAddressER10ether_addr` | AppleBCMWLANProximityInterface::setMacAddress(ether_addr&) | `0xffffff8001720354` |

### 2.4 apple80211 AWDL getters (25)

| vptr offset (slot) | AWDL target | Proxi (mangled) | Proxi (demangled) | addr |
|---|---|---|---|---|
| +0x0fc8 (505) | pure | `__ZN30AppleBCMWLANProximityInterface7getSSIDEP20apple80211_ssid_data` | AppleBCMWLANProximityInterface::getSSID(apple80211_ssid_data*) | `0xffffff800171a70e` |
| +0x0fd0 (506) | pure | `__ZN30AppleBCMWLANProximityInterface10getCHANNELEP23apple80211_channel_data` | AppleBCMWLANProximityInterface::getCHANNEL(apple80211_channel_data*) | `0xffffff800171a752` |
| +0x0fd8 (507) | pure | `__ZN30AppleBCMWLANProximityInterface8getBSSIDEP21apple80211_bssid_data` | AppleBCMWLANProximityInterface::getBSSID(apple80211_bssid_data*) | `0xffffff800171a702` |
| +0x0fe0 (508) | pure | `__ZN30AppleBCMWLANProximityInterface8getSTATEEP21apple80211_state_data` | AppleBCMWLANProximityInterface::getSTATE(apple80211_state_data*) | `0xffffff8001719944` |
| +0x0fe8 (509) | pure | `__ZN30AppleBCMWLANProximityInterface19getAWDL_SYNC_PARAMSEP27apple80211_awdl_sync_params` | AppleBCMWLANProximityInterface::getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*) | `0xffffff800171b1b8` |
| +0x0ff0 (510) | pure | `__ZN30AppleBCMWLANProximityInterface20getAWDL_SYNC_ENABLEDEP28apple80211_awdl_sync_enabled` | AppleBCMWLANProximityInterface::getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*) | `0xffffff800171b092` |
| +0x0ff8 (511) | pure | `__ZN30AppleBCMWLANProximityInterface42getAWDL_EXTENSION_STATE_MACHINE_PARAMETERSEP49apple80211_awdl_extension_state_machine_parameter` | AppleBCMWLANProximityInterface::getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*) | `0xffffff800171be78` |
| +0x1000 (512) | pure | `__ZN30AppleBCMWLANProximityInterface19getAWDL_ELECTION_IDEP27apple80211_awdl_election_id` | AppleBCMWLANProximityInterface::getAWDL_ELECTION_ID(apple80211_awdl_election_id*) | `0xffffff800171c456` |
| +0x1008 (513) | pure | `__ZN30AppleBCMWLANProximityInterface23getAWDL_ELECTION_METRICEP31apple80211_awdl_election_metric` | AppleBCMWLANProximityInterface::getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*) | `0xffffff800171cd70` |
| +0x1010 (514) | pure | `__ZN30AppleBCMWLANProximityInterface40getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCEEP37apple80211_awdl_sync_channel_sequence` | AppleBCMWLANProximityInterface::getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*) | `0xffffff800172421a` |
| +0x1020 (516) | pure | `__ZN30AppleBCMWLANProximityInterface22getAWDL_MASTER_CHANNELEP30apple80211_awdl_master_channel` | AppleBCMWLANProximityInterface::getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*) | `0xffffff800171f1e6` |
| +0x1028 (517) | pure | `__ZN30AppleBCMWLANProximityInterface18getAWDL_SYNC_STATEEP26apple80211_awdl_sync_state` | AppleBCMWLANProximityInterface::getAWDL_SYNC_STATE(apple80211_awdl_sync_state*) | `0xffffff800171b528` |
| +0x1030 (518) | pure | `__ZN30AppleBCMWLANProximityInterface32getAWDL_ELECTION_RSSI_THRESHOLDSEP40apple80211_awdl_election_rssi_thresholds` | AppleBCMWLANProximityInterface::getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*) | `0xffffff800171d2e4` |
| +0x1038 (519) | pure | `__ZN30AppleBCMWLANProximityInterface21getAWDL_PRESENCE_MODEEP29apple80211_awdl_presence_mode` | AppleBCMWLANProximityInterface::getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*) | `0xffffff800171bd12` |
| +0x1040 (520) | pure | `__ZN30AppleBCMWLANProximityInterface23getAWDL_ELECTION_PARAMSEP31apple80211_awdl_election_params` | AppleBCMWLANProximityInterface::getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*) | `0xffffff800171ce5a` |
| +0x1048 (521) | pure | `__ZN30AppleBCMWLANProximityInterface27getAWDL_DEVICE_CAPABILITIESEP35apple80211_awdl_device_capabilities` | AppleBCMWLANProximityInterface::getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities*) | `0xffffff800171be38` |
| +0x1050 (522) | pure | `__ZN30AppleBCMWLANProximityInterface32getAWDL_SECONDARY_MASTER_CHANNELEP40apple80211_awdl_secondary_master_channel` | AppleBCMWLANProximityInterface::getAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*) | `0xffffff800171ec10` |
| +0x1058 (523) | pure | `__ZN30AppleBCMWLANProximityInterface18getAWDL_AF_TX_MODEEP26apple80211_awdl_af_tx_mode` | AppleBCMWLANProximityInterface::getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*) | `0xffffff8001719b62` |
| +0x1060 (524) | pure | `__ZN30AppleBCMWLANProximityInterface16getAWDL_MIN_RATEEP24apple80211_awdl_min_rate` | AppleBCMWLANProximityInterface::getAWDL_MIN_RATE(apple80211_awdl_min_rate*) | `0xffffff8001728a3c` |
| +0x1068 (525) | pure | `__ZN30AppleBCMWLANProximityInterface23getAWDL_ENCRYPTION_TYPEEP18apple80211_encrypt` | AppleBCMWLANProximityInterface::getAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) | `0xffffff800171f900` |
| +0x1070 (526) | pure | `__ZN30AppleBCMWLANProximityInterface20getAWDL_TXCAL_PERIODEP28apple80211_awdl_txcal_period` | AppleBCMWLANProximityInterface::getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) | `0xffffff8001728cae` |
| +0x1078 (527) | pure | `__ZN30AppleBCMWLANProximityInterface17getAWDL_DEV_STATSEP25apple80211_awdl_dev_stats` | AppleBCMWLANProximityInterface::getAWDL_DEV_STATS(apple80211_awdl_dev_stats*) | `0xffffff800171b698` |
| +0x1080 (528) | pure | `__ZN30AppleBCMWLANProximityInterface19getAWDL_DFSP_CONFIGEP27apple80211_awdl_dfsp_config` | AppleBCMWLANProximityInterface::getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) | `0xffffff8001728fba` |
| +0x1088 (529) | pure | `__ZN30AppleBCMWLANProximityInterface11getAWDL_CCAEP19apple80211_awdl_cca` | AppleBCMWLANProximityInterface::getAWDL_CCA(apple80211_awdl_cca*) | `0xffffff800171ba8e` |
| +0x1090 (530) | pure | `__ZN30AppleBCMWLANProximityInterface41getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIESEP19apple80211_awdl_cap` | AppleBCMWLANProximityInterface::getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap*) | `0xffffff800171ed38` |

### 2.5 apple80211 AWDL setters (30)

| vptr offset (slot) | AWDL target | Proxi (mangled) | Proxi (demangled) | addr |
|---|---|---|---|---|
| +0x10a0 (532) | pure | `__ZN30AppleBCMWLANProximityInterface7setSSIDEP20apple80211_ssid_data` | AppleBCMWLANProximityInterface::setSSID(apple80211_ssid_data*) | `0xffffff8001715a70` |
| +0x10a8 (533) | pure | `__ZN30AppleBCMWLANProximityInterface10setCHANNELEP23apple80211_channel_data` | AppleBCMWLANProximityInterface::setCHANNEL(apple80211_channel_data*) | `0xffffff800171e8d8` |
| +0x10b0 (534) | pure | `__ZN30AppleBCMWLANProximityInterface19setAWDL_SYNC_PARAMSEP27apple80211_awdl_sync_params` | AppleBCMWLANProximityInterface::setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*) | `0xffffff800171ddee` |
| +0x10b8 (535) | pure | `__ZN30AppleBCMWLANProximityInterface20setAWDL_SYNC_ENABLEDEP28apple80211_awdl_sync_enabled` | AppleBCMWLANProximityInterface::setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*) | `0xffffff800171e42a` |
| +0x10c0 (536) | pure | `__ZN30AppleBCMWLANProximityInterface42setAWDL_EXTENSION_STATE_MACHINE_PARAMETERSEP49apple80211_awdl_extension_state_machine_parameter` | AppleBCMWLANProximityInterface::setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*) | `0xffffff800171bfdc` |
| +0x10c8 (537) | pure | `__ZN30AppleBCMWLANProximityInterface19setAWDL_ELECTION_IDEP27apple80211_awdl_election_id` | AppleBCMWLANProximityInterface::setAWDL_ELECTION_ID(apple80211_awdl_election_id*) | `0xffffff800171cace` |
| +0x10d0 (538) | pure | `__ZN30AppleBCMWLANProximityInterface13setAWDL_BSSIDEP21apple80211_awdl_bssid` | AppleBCMWLANProximityInterface::setAWDL_BSSID(apple80211_awdl_bssid*) | `0xffffff800172181a` |
| +0x10d8 (539) | pure | `__ZN30AppleBCMWLANProximityInterface23setAWDL_ELECTION_METRICEP31apple80211_awdl_election_metric` | AppleBCMWLANProximityInterface::setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*) | `0xffffff800171cf76` |
| +0x10e0 (540) | pure | `__ZN30AppleBCMWLANProximityInterface40setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCEEP37apple80211_awdl_sync_channel_sequence` | AppleBCMWLANProximityInterface::setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*) | `0xffffff8001724436` |
| +0x10e8 (541) | pure | `__ZN30AppleBCMWLANProximityInterface22setAWDL_MASTER_CHANNELEP30apple80211_awdl_master_channel` | AppleBCMWLANProximityInterface::setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*) | `0xffffff800171ea1a` |
| +0x10f0 (542) | pure | `__ZN30AppleBCMWLANProximityInterface18setAWDL_SYNC_STATEEP26apple80211_awdl_sync_state` | AppleBCMWLANProximityInterface::setAWDL_SYNC_STATE(apple80211_awdl_sync_state*) | `0xffffff800171e164` |
| +0x10f8 (543) | pure | `__ZN30AppleBCMWLANProximityInterface32setAWDL_ELECTION_RSSI_THRESHOLDSEP40apple80211_awdl_election_rssi_thresholds` | AppleBCMWLANProximityInterface::setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*) | `0xffffff800171d16a` |
| +0x1100 (544) | pure | `__ZN30AppleBCMWLANProximityInterface21setAWDL_PRESENCE_MODEEP29apple80211_awdl_presence_mode` | AppleBCMWLANProximityInterface::setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*) | `0xffffff80017231d8` |
| +0x1110 (546) | pure | `__ZN30AppleBCMWLANProximityInterface19setAWDL_OOB_REQUESTEP27apple80211_awdl_oob_request` | AppleBCMWLANProximityInterface::setAWDL_OOB_REQUEST(apple80211_awdl_oob_request*) | `0xffffff8001726474` |
| +0x1118 (547) | pure | `__ZN30AppleBCMWLANProximityInterface27setAWDL_SYNC_FRAME_TEMPLATEEP35apple80211_awdl_sync_frame_template` | AppleBCMWLANProximityInterface::setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template*) | `0xffffff800171da80` |
| +0x1120 (548) | pure | `__ZN30AppleBCMWLANProximityInterface23setAWDL_ELECTION_PARAMSEP31apple80211_awdl_election_params` | AppleBCMWLANProximityInterface::setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*) | `0xffffff800171c4ca` |
| +0x1128 (549) | pure | `__ZN30AppleBCMWLANProximityInterface33setAWDL_PEER_TRAFFIC_REGISTRATIONEP41apple80211_awdl_peer_traffic_registration` | AppleBCMWLANProximityInterface::setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration*) | `0xffffff800171fcaa` |
| +0x1130 (550) | pure | `__ZN30AppleBCMWLANProximityInterface32setAWDL_SECONDARY_MASTER_CHANNELEP40apple80211_awdl_secondary_master_channel` | AppleBCMWLANProximityInterface::setAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*) | `0xffffff800171f01e` |
| +0x1138 (551) | pure | `__ZN30AppleBCMWLANProximityInterface18setAWDL_AF_TX_MODEEP26apple80211_awdl_af_tx_mode` | AppleBCMWLANProximityInterface::setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*) | `0xffffff80017199c4` |
| +0x1140 (552) | pure | `__ZN30AppleBCMWLANProximityInterface16setAWDL_MIN_RATEEP24apple80211_awdl_min_rate` | AppleBCMWLANProximityInterface::setAWDL_MIN_RATE(apple80211_awdl_min_rate*) | `0xffffff8001728a48` |
| +0x1148 (553) | pure | `__ZN30AppleBCMWLANProximityInterface23setAWDL_ENCRYPTION_KEYSEP14apple80211_key` | AppleBCMWLANProximityInterface::setAWDL_ENCRYPTION_KEYS(apple80211_key*) | `0xffffff800171f480` |
| +0x1150 (554) | pure | `__ZN30AppleBCMWLANProximityInterface23setAWDL_ENCRYPTION_TYPEEP18apple80211_encrypt` | AppleBCMWLANProximityInterface::setAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) | `0xffffff800171fa46` |
| +0x1158 (555) | pure | `__ZN30AppleBCMWLANProximityInterface24setAWDL_OOB_AUTO_REQUESTEP27apple80211_awdl_oob_request` | AppleBCMWLANProximityInterface::setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request*) | `0xffffff8001727c0e` |
| +0x1160 (556) | pure | `__ZN30AppleBCMWLANProximityInterface20setAWDL_TXCAL_PERIODEP28apple80211_awdl_txcal_period` | AppleBCMWLANProximityInterface::setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) | `0xffffff8001728b8c` |
| +0x1168 (557) | pure | `__ZN30AppleBCMWLANProximityInterface24setAWDL_AWDL_ADVERTISERSEP20awdl_peer_advert_add` | AppleBCMWLANProximityInterface::setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add*) | `0xffffff800171c932` |
| +0x1170 (558) | pure | `__ZN30AppleBCMWLANProximityInterface19setAWDL_DFSP_CONFIGEP27apple80211_awdl_dfsp_config` | AppleBCMWLANProximityInterface::setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) | `0xffffff8001728dd0` |
| +0x1178 (559) | pure | `__ZN30AppleBCMWLANProximityInterface24setAWDL_DFSP_UCSA_CONFIGEP32apple80211_awdl_dfsp_ucsa_config` | AppleBCMWLANProximityInterface::setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config*) | `0xffffff800172919c` |
| +0x1188 (561) | pure | `__ZN30AppleBCMWLANProximityInterface31setAWDL_OOB_SEC_PAYLOAD_REQUESTEP38apple80211_awdl_af_sec_payload_request` | AppleBCMWLANProximityInterface::setAWDL_OOB_SEC_PAYLOAD_REQUEST(apple80211_awdl_af_sec_payload_request*) | `0xffffff8001727d56` |
| +0x11b0 (566) | pure | `__ZN30AppleBCMWLANProximityInterface14setAWDL_CONFIGEP30apple80211_awdl_config_request` | AppleBCMWLANProximityInterface::setAWDL_CONFIG(apple80211_awdl_config_request*) | `0xffffff80017215e2` |
| +0x11b8 (567) | pure | `__ZN30AppleBCMWLANProximityInterface29setAWDL_RTG_PEER_STATS_CONFIGEP29apple80211_rtg_peer_stats_cfg` | AppleBCMWLANProximityInterface::setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg*) | `0xffffff800171fb6c` |

### 2.6 Other (10)

| vptr offset (slot) | AWDL target | Proxi (mangled) | Proxi (demangled) | addr |
|---|---|---|---|---|
| +0x09e8 (317) | `0xffffff8002b1231a` — no exact symbol (`?`; nearest-symbol fallback in syms: `__ZZN20IO80211P2PSupervisor4freeEvE20kalloc_type_view_186+0x675c6a`; VI has the same target — probably a shared stub of another kext) | `__ZN30AppleBCMWLANProximityInterface16getBSDNamePrefixEv` | AppleBCMWLANProximityInterface::getBSDNamePrefix() | `0xffffff8001715a7c` |
| +0x1018 (515) | pure | `__ZN30AppleBCMWLANProximityInterface26getPEER_CACHE_MAXIMUM_SIZEEP34apple80211_peer_cache_maximum_size` | AppleBCMWLANProximityInterface::getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size*) | `0xffffff80017233b6` |
| +0x1098 (531) | pure | `__ZN30AppleBCMWLANProximityInterface13getLLW_PARAMSEP21apple80211_llw_params` | AppleBCMWLANProximityInterface::getLLW_PARAMS(apple80211_llw_params*) | `0xffffff800172753e` |
| +0x1108 (545) | pure | `__ZN30AppleBCMWLANProximityInterface21setPEER_CACHE_CONTROLEP29apple80211_peer_cache_control` | AppleBCMWLANProximityInterface::setPEER_CACHE_CONTROL(apple80211_peer_cache_control*) | `0xffffff80017235ce` |
| +0x1180 (560) | pure | `__ZN30AppleBCMWLANProximityInterface30setCOMPANION_SKYWALK_IF_CREATEEP38apple80211_companion_skywalk_if_create` | AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create*) | `0xffffff8001722ef2` |
| +0x1190 (562) | pure | `__ZN30AppleBCMWLANProximityInterface13setLLW_PARAMSEP21apple80211_llw_params` | AppleBCMWLANProximityInterface::setLLW_PARAMS(apple80211_llw_params*) | `0xffffff8001726ac8` |
| +0x1198 (563) | pure | `__ZN30AppleBCMWLANProximityInterface10setTX_RATEEP18apple80211_tx_rate` | AppleBCMWLANProximityInterface::setTX_RATE(apple80211_tx_rate*) | `0xffffff8001720c20` |
| +0x11a0 (564) | pure | `__ZN30AppleBCMWLANProximityInterface22setP2P_COEX_PROTECTIONEP30apple80211_p2p_coex_protection` | AppleBCMWLANProximityInterface::setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection*) | `0xffffff800172a968` |
| +0x11a8 (565) | pure | `__ZN30AppleBCMWLANProximityInterface12setPSF_DWELLEP32apple80211_awdl_psf_dwell_params` | AppleBCMWLANProximityInterface::setPSF_DWELL(apple80211_awdl_psf_dwell_params*) | `0xffffff800171dfc0` |
| +0x11c0 (568) | `NULL` (last slot of AWDL) | `__ZN30AppleBCMWLANProximityInterface10getTX_RATEEP18apple80211_tx_rate` | AppleBCMWLANProximityInterface::getTX_RATE(apple80211_tx_rate*) | `0xffffff8001720ade` |

## 3) The 3 new slots at the tail of Proxi

| vptr offset (slot) | mangled | demangled | addr | raw qword |
|---|---|---|---|---|
| +0x11c8 (569) | `__ZN30AppleBCMWLANProximityInterface4initEP16AppleBCMWLANCoreP10ether_addrjPc` | AppleBCMWLANProximityInterface::init(AppleBCMWLANCore*, ether_addr*, unsigned int, char*) | `0xffffff8001718db0` | `0x0040000001618db0` |
| +0x11d0 (570) | `__ZN30AppleBCMWLANProximityInterface13freeResourcesEv` | AppleBCMWLANProximityInterface::freeResources() | `0xffffff80017194c8` | `0x1000000016194c8` (different fixup shape, same mechanism) |
| +0x11d8 (571) | — | (NULL, final filler) | `0x0` | `0x0` |

## 4) The 64 pure-virtual slots of IO80211AWDLProtocol: overridden vs left-as-stub

**The 64, listed individually and verified one by one (includes the 23 that were missing): all are overridden by Proxi; none remains as pure-virtual.** Proxi does not have a single `___cxa_pure_virtual` slot in its vtable.

Offsets (vptr-relative): `+0xF40`, and the whole range `+0xFC8`–`+0x11B8`: `+0xFC8, +0xFD0, +0xFD8, +0xFE0, +0xFE8, +0xFF0, +0xFF8, +0x1000, +0x1008, +0x1010, +0x1018, +0x1020, +0x1028, +0x1030, +0x1038, +0x1040, +0x1048, +0x1050, +0x1058, +0x1060, +0x1068, +0x1070, +0x1078, +0x1080, +0x1088, +0x1090, +0x1098, +0x10A0, +0x10A8, +0x10B0, +0x10B8, +0x10C0, +0x10C8, +0x10D0, +0x10D8, +0x10E0, +0x10E8, +0x10F0, +0x10F8, +0x1100, +0x1108, +0x1110, +0x1118, +0x1120, +0x1128, +0x1130, +0x1138, +0x1140, +0x1148, +0x1150, +0x1158, +0x1160, +0x1168, +0x1170, +0x1178, +0x1180, +0x1188, +0x1190, +0x1198, +0x11A0, +0x11A8, +0x11B0, +0x11B8` (1 + 63 = 64). The slot-by-slot detail with its implementation is in the tables of §2 (rows with "pure" in the AWDL target column).

Breakdown of the 63 slots AWDL added beyond the 506 of VI: 62 pure-virtual (+0xFD0–0x11B8) + 1 final NULL (+0x11C0, which Proxi fills with `getTX_RATE`); additionally AWDL turned the final NULL of VI (+0xFC8) into pure-virtual, and +0xF40 was already pure-virtual since VI.

**Design consequence:** a custom subclass of `IO80211AWDLProtocol` must implement those 64 functions to be instantiable; in practice the subset the family calls for an active AWDL interface is what Proxi implements (all of them). The 4-argument `init` of `IO80211VirtualInterface` (+0xE68, inherited) and the pattern of a specialized `init` as a new virtual at the tail (+0x11C8 in Proxi) is the mold to replicate.

## 5) Linkability of the four symbols for a third-party kext

Symbols: `__ZTV19IO80211AWDLProtocol`, `__ZN19IO80211AWDLProtocol10gMetaClassE`, `__ZN19IO80211AWDLProtocolC2EPK11OSMetaClass`, `__ZN19IO80211AWDLProtocolD2Ev`.

### 5.1 How IO80211Family exports from this kernelcollection (what the file shows)

- `BootKernelExtensions.kc` is an **MH_FILESET** (filetype 12) with 831 load commands: 621 `LC_SEGMENT_64` (flattened regions of kernel+kexts), 204 `LC_FILESET_ENTRY` (members), `LC_SYMTAB` with **nsyms=0** (empty), empty `LC_DYSYMTAB`, `LC_DYLD_CHAINED_FIXUPS` (dataoff `0x4065028`, 36012 B) and **no `LC_DYLD_EXPORTS_TRIE`** (0x80000033 does not exist in the file; the 204 commands `0x80000035` are fileset entries).
- The **kernel** is embedded as its own Mach-O at fileoff `0xe8000` (ftype 2) with `__LINKEDIT` and a real symtab: `symoff 0x3398000`, **nsyms 23790, all n_type `0x0f` (external, section)** — only external symbols of the kernel itself. It contains no entry of the kexts (the addresses `0xffffff8002456440` and `0xffffff800248cde8` of IO80211Family do not appear as n_value).
- The string table of that symtab (8.75 MB) **does contain the four names** (`__ZTV19IO80211AWDLProtocol` at strx `0x4f4c17`, `…10gMetaClassE` at `0x1dba53`, `…C2EPK11OSMetaClass` at `0x1dbb66`, `…D2Ev` at `0x1dbbce`) but **without any nlist entry pointing at them** — remnants of kext linkedit after the strip; the presence of the name does not prove exportation.
- `__KLDDATA` (fileoff `0x1226000`, 0x3f589 bytes) exists but does not contain the string `IO80211AWDLProtocol` — there is no kext symbol data for this class.
- `SystemKernelExtensions.kc` (369 MB, the second fileset of the system): no symtab in the container, only 8 parseable Mach-Os, none with the four symbols; the dict of `IO80211Family.kext` does not appear in its `__PRELINK_INFO` (`?` — it may be referenced another way).
- `BootKernelExtensions.kc.elides` lists 21 elided kexts (test/manufacturing: AirPort.Brcm4360-MFG, etc.) — `IO80211Family` is **not** elided: its image is inside the BootKC (and everything analyzed in this series comes from there).

### 5.2 What the file DOES prove

1. **The four symbols exist** in the image of IO80211Family inside the BootKC with stable addresses: `__ZTV19IO80211AWDLProtocol` `0xffffff8002456440`, `gMetaClass` `0xffffff800248cde8`, `C2` `0xffffff8002198c04`, `D2` `0xffffff8002198c24`.
2. **Apple's own Broadcom kext consumes them**: they figure as externals (n_type `01`, value 0) in the symbol portion of `AppleBCMWLANCoreMac` (`syms_bcm.txt:7536-7538` and `:8568`), and the reference to the `__ZTV` was **resolved by rebase fixup** at `0xffffff80017de1c0` → `0xffffff8002456440` (verified by raw read). That is: at minimum, the inter-kext surface of Apple resolves them in prelink.
3. The **formal dependency gate is open**: `Info.plist` of `/System/Library/Extensions/IO80211Family.kext` declares **`OSBundleCompatibleVersion = 1.0`** (CFBundleVersion `1200.13.1`) — without that key, OSKext would reject a third party declaring `com.apple.iokit.IO80211Family` in `OSBundleLibraries`; with it, the declaration is valid. `AppleBCMWLANCoreMac.kext` also declares `OSBundleCompatibleVersion = 1.0`. Real example of consumption: `AppleBCMWLANBusInterfacePCIeMac.kext` declares `com.apple.iokit.IO80211Family = 1.5.0` in its `OSBundleLibraries` (dict in the `__PRELINK_INFO` of the BootKC).
4. The symbols are **not private-extern in any available record**: in `syms.txt` they appear with the same treatment as the rest of the externals of IO80211Family (n_type `0f`), and the `0x10` (N_PEXT) only appears in symbols of the Broadcom kext itself (`syms_bcm.txt`, field `1e`) — nuance: that field of syms.txt is not validated against the original symtab (which does not exist in the .kc), so it goes as an indication, not as proof.

### 5.3 What CANNOT be determined from this system (said bluntly)

**It cannot be determined, with the evidence available in the files of this machine, whether `kextlink`/AuxKC will actually expose those four symbols to a third-party kext.** The shipping .kc files carry no symbol lists of kexts (empty symtab in the fileset, symtab of the embedded kernel limited to the kernel externals, no export trie, residual strings without entries), the binaries of the kexts are not on disk (sealed root: only Info.plist), and there is no KDK installed (`/Library/Developer/KDKs` does not exist). The real availability for third parties is decided by the toolchain/policy outside these files. Practical verification if it is to be closed: (a) install a macOS 26 KDK and do `nm -m` of the binary of `IO80211Family.kext` to read the real n_type/n_desc of the four symbols, or (b) build a minimal test kext with `OSBundleLibraries → com.apple.iokit.IO80211Family` referencing `__ZTV19IO80211AWDLProtocol` and see whether `kextlink` resolves it.

## 6) Method and limits

- Vtable diff: raw read of both slot ranges with uniform decoding `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)` and a segment map via `otool -l` (variable delta per segment); names via `syms.txt`/`syms_bcm.txt`; demangle with `c++filt` (verified batch).
- `___cxa_pure_virtual`: identified in the symtab of the embedded kernel (fileoff `0xe8000`, LC_SYMTAB symoff `0x3398000`, nsyms 23790, all n_type `0x0f`).
- KC structure: own parse of the load commands of the fileset (621 segments + 204 fileset entries + empty SYMTAB/DYSYMTAB + chained fixups), scan of embedded magics, parse of `__PRELINK_INFO` (4.5 MB) and of `SystemKernelExtensions.kc`.
- Limits `?`: unnamed target of +0x9E8 (`0xffffff8002b1231a`, shared by VI and AWDL); the ~23 final slots of AWDL now verified but the names of some Proxi targets depend on syms_bcm (provenance of syms.txt/syms_bcm.txt not re-verifiable against a nonexistent symtab — their addresses do match everything read from the file); third-party linkability not determinable (§5.3); the IO80211Family dict in the `__PRELINK_INFO` of the SystemKC not located.
- No other repo file was touched.
