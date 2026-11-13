# IO80211AWDLPeerManager: what it implements in software and what it demands from the driver

Complements `AWDL_FIRMWARE_DUTIES.md` (what the Broadcom firmware does in role 6) and
`AWDL_PURE_SLOTS.md` (the pure slots of `IO80211AWDLProtocol`). Here the AWDL peer manager that
lives inside IO80211Family is documented: which computations it does by itself, what it expects
from the driver beneath it and what would happen if a third-party kext with an interface that
subclasses the AWDL classes of IO80211 tried to reuse it instead of re-implementing election and
sync.

Source: kernelcache `BootKernelExtensions.kc` (macOS 25.6.0, md5
`f489ff8be80e6013a1bf47561dcaef45`), IDB copy at `Build/ida_awdl/task_awdl_peermgr/kc.i64`,
Hex-Rays decompilation of ~60 functions of
`IO80211AWDLPeerManager`/`IO80211AWDLPeer`. The function names exist in the IDB (residual
strtab of the kext); all the cited ivar offsets belong to that build and are not frozen by any
ABI. Anything unverified is marked `?`.

## 1. Overview

`IO80211AWDLPeerManager` is a complete software implementation of the "protocol" part of
AWDL: master election with metrics and a sync tree, construction of all the master indication
templates (TLVs), parsing of the peer TLVs, peer table with its database, availability window and
channel sequence state machines, and a dozen timers of its own on the controller workloop. The
only thing it delegates downward is the physical execution: the TX of the sync frames at the
scheduled instants, the channel change, the capture of action frames and the transport of data
packets. The delegation happens over two paths: internal apple80211 ioctls against the interface
(the same types Broadcom today forwards to the firmware) and the cells of its own vtable that the
interface stack invokes (RX of action frames, events, packet pull).

## 2. Instantiation and lifecycle

### 2.1 Who creates it

`IO80211VirtualInterface::createPeerManager` (`0xffffff80023308c2`, vtable slot 355 counting from
the start of `__ZTV` (offset +0xB08 from the vptr; the slots of this document follow that
convention), inheritable: the vtable of `AppleBCMWLANProximityInterface` points to the same
function at +0xB08):

- Reads the role of the interface (`*(interface_ivar+288)+88`; the same value `setInterfaceRole`
  publishes as a property, see `AWDL_INTERFACE_BIRTH.md`) and the local MAC with
  `IO80211SkywalkInterface::getSelfMacAddr` (vtable slot 417).
- Role 6 → `IO80211AWDLPeerManager::withInterface` (`0xffffff80021c14bc`). Role 7 →
  `IO80211PeerManager::withInterface`, roles 8/9 → `IO80211NANPeerManager::withInterface`, role 10
  → `IO80211NANDataInterfacePeerManager::withInterface` (same function, branch by branch).
- The only caller of `withInterface` is `createPeerManager` (xref `0xffffff8002330957`).
- After creating the manager it registers it in the controller: `getController` (slot 375) →
  `*(ctrl+288)+400 = interface` (400 for AWDL; 408 for the generic PeerManager; 944/952 NAN).
  `AWDL_APPLE80211_DISPATCH.md` already described the reverse path (safeMetaCast from the
  controller ivars).

`IO80211AWDLPeerManager::withInterface`: `OSObject_typed_operator_new(&IO80211AWDLPeerManager_ktv, 40)`
(kalloc view at `0xffffff8002493d80`), assigns the vtable `0xffffff800245a868`, registers in the
`gMetaClass` (`0xffffff800248d2e0`) and calls `initWithInterface`. The C++ object itself is small
(40 B); the state lives in two separate blocks that `initWithInterface` mounts.

### 2.2 `initWithInterface` (`0xffffff80021c1540`, 0x1789 B)

- Calls the base `IO80211PeerManager::initWithInterface` (which leaves its ivars at `this+0x18`)
  and allocates the AWDL ivars block with `IOMallocTypeImpl` (view `kalloc_type_view_2098`) at
  `this+0x20`: over 66 KB of state. Fields with a verifiable name through the logs:
  self MAC `+37216`, election block `+37228..+37310` (previous copy at `+37271`), configurable
  election id `+63992`, metric bins `+43888..` (indices 5486–5511, doubles), window
  periods `+62940/+62944/+62948/+62952`, channel sequence `+42864` (u16 length) and `+42868`
  (12 B entries), action frame ring `+16848..+16872`, UMI template `+9048` (1200 B),
  timers `+10312..+10408`.
- Boot-args read: `awdl_disable_dp_umi`, `awdl_enable`, `awdl_oper_mode`, `awdl_debug_tooLong`,
  `enable_forced_roam`, `_discard_airplay_records`, `awdl_mdns_mode`, `awdl_bonjour_offload`,
  `awdl_mdns_direct_path`, `awdl_block_mdns`, `awdl_disable_serviceDiscovery3`, `awdl_log_flags`,
  `awdl_bssid`, `awdl_election_id`, `awdl_fw_election` (→ `+10305`, forces firmware election),
  `sidecar_stats_lat_lim/time_lim`, `disable_p2pBssSteering`, `wlan.splitAWDLAFThread` (→
  `+16880`),
  `awdl_allow_40`, `awdl_max_bw`, `forcecountry`, `dis_assist_disc_timer`, `unifiedcountry`,
  `awdl_sidecar`, `override_awdl_platform_type`.
- Creates 14 `IO80211TimerSource::allocWithParams` on the controller workloop
  (`getController` → vtable `+3176`): `handleInfraMonitorTimer` `+10384`,
  `handleAwdlScanTimer` `+10392`, `handleAwdlLLATimer` `+10400`,
  `handleMasterIndicationTimer` `+10312`, `handleAWDLRecoveryTimer` `+10352`,
  `handleUmiTimer` `+10320`, `handleMulticastTimer` `+10328`,
  `handleAssistedDiscoveryTimedOut` `+10336`, `handleSuspendTimer` `+10344`,
  `handleCcChangeTimer` (base ivars `+2480`), `jumpStartDFSProxy` `+10360` (type 0),
  `discoveryTimeout` `+10368`, `handleLinkChangeTimer` `+10376` and
  `actionFrameProcessThread` `+10408`. Plus two command queues:
  `_serviceRequests` (`IO80211CommandQueue`, capacity 0x100, ivars `+42440`) and
  `_serviceResponsesReceiving` (capacity 1, `+42432`), and packet queues
  `IOPacketQueue` (3 at `+10424`, 3 at `+10448`, capacity 0x19).
- Creates the subordinates: `IO80211PeerBssSteeringManager::withManager(this, 6)` (`+16616`),
  `IO80211P2PSupervisor::withInterface` (`+480`, with backpointer to the manager),
  `IO80211RealTimePeerManager::withInterface` (`+488`), a 6656 B block (`+16840`), and
  `initAwdlPacketDescriptorPool(0x100)`.

### 2.3 Enabling and disabling

- `interfaceStateChange` (`0xffffff80021d8cc8`, vtable cell 55, `+0x1B8`) →
  `enableAWDL` (`0xffffff80021dcf58`); `doDisable` (`0xffffff80021c69cc`) →
  `disableAWDL(int)` (`0xffffff80021c6ae8`); `monitorAWDLState` (`0xffffff80021ed724`, the periodic
  watchdog) can also disable.
- `setAwdlCtl` (`0xffffff80021cde94`, cell 56, `+0x1C0`) is the enable/disable request that
  arrives from the apple80211 dispatch (AWDL_CTL).
- `enableAWDL`: checks link (`vtable+2952` on the interface, "Interface LinkDown"), reads
  capabilities with the internal ioctl of encoding `0x155C02869C9` (payload `awdl_cap`; the bits
  it distinguishes: `SEC_PAYLOAD` `+16576`, `CCA_STATS` `+16577`, `PSF_DWELL_MI` `+16578`,
  `PSF_DWELL_UMI` `+16579`), `resetElectionState`, `updateBroadcastMI`,
  `updateSyncFrameTemplate(true)`, `validateForHostapAWDLConcurrency`, `initFWElectionParams`,
  `setChannelSequenceUnconditional(true)`, the enable proper with the internal ioctl
  `0x75802869C8` (type 117, `setAWDL_SYNC_ENABLED`), `ageoutRootNodeList`,
  `initializeAwdlMetricBins`, arming the watchdog with 5000 ms, `postSyncStateChanged`,
  `setPSFDwell`, and notification to the infrastructure interface
  (`IO80211InfraInterface::notifyAWDLStateChange`).

## 3. Software master election

### 3.1 State

Block `+37228..+37310` (current) and previous copy `+37271..+37298` (compared with a `memcmp` of
0x2B bytes in `electionParamsChanged`, `0xffffff80021e9f6c`):

| Field | Ivars offset | Note |
|---|---:|---|
| parent node | `+37237` (6 B) | previous copy `+37280` |
| relay | `+37235` (6 B) | previous copy `+37279` |
| root | `+37231` (6 B) | previous copy `+37274` |
| rootLFC | `+37243` u32 | frame count of the root |
| hopCount | `+37247` u32 | |
| rootMetric | `+37251` u32 | |
| selfMetric | `+37255` u32 | |
| electionId | `+37259` u32 | seed `+63992` (boot-arg `awdl_election_id`) |
| selfLFC | `+37560` u32 | own counter |

Other flags: `+10304` "master change pending to apply in firmware" (debounce 1001 ms),
`+10306` "election rescheduled" (raised by the Sync Params parsing of the peer,
`parseAwdlSyncParamsTlv`, outF line 808), `+10307` "election fixed" (`setAwdlFixedElectionToSelf`,
`0xffffff80021ed37e`), `+37484` forced master, `+37485` forced master RTG. Root node list
at `+37576`: 100 entries of 48 B `{valid, root MAC +1, parent MAC +7, RLFC u32 +16, lastSeen u64
+24, HC +32, RMet +36, AW u32 +40}` with 1 h expiry (`ageoutRootNodeList`,
`0xffffff80021ebd0e`). Path to the root for the tree TLV at `+37500`.

### 3.2 The algorithm

`runElection` (`0xffffff80021e3e7c`), triggered after processing action frames (see §8):

1. Datapath lock (object at base ivars `+120`, vtable `+280`/`+288`; the methods
   `IO80211PeerManager::lockDataPath/unlockDataPath` are at `0xffffff800224db2a`/`0x224dc1e`).
2. If there is a recent AF from a peer (`isPeerQualifiedForElection` +
   `isPeerSafeForElection`) → `setElectionParamsToPeer` + `updateRootNodeList`. If the AF is old or
   the peer does not exist → "Setting Election Params to Self": parent/relay/root = own MAC,
   rootLFC = selfLFC, rootMetric = selfMetric.
3. `runParentElection` (`0xffffff80021ec0e4`) sweeps the peer table (hash at base ivars
   `+1344`, list head `+56`, counter `+36`) and elects the parent comparing, in order: highest
   rootMetric (peer ivars `+579`); on equality, lowest root MAC (`+559`, 6 B memcmp; see
   `AWDL_OPEN_QUESTIONS_1.md` §1.3); on equality, highest
   rootLFC (`+571`); on equality, lowest hopCount (`+575`). If the local node wins ("I am Root
   Node"), it stays as master: hopCount 0 and selfMetric as rootMetric.
4. `isPeerQualifiedForElection` (`0xffffff80021eacc0`): AF of the peer less than 3000 ms ago (peer
   ivars `+784`), the peer advertises the same electionId (`+587` == ivars `+37259`) and, if the
   peer is an electoral master (peer `+120 == 2`), its RSSI exceeds the thresholds of
   `awdl_election_rssi_thresholds` (ivars `+10272`/`+10276`).
5. Own metric: `monitorElectionMetric` (`0xffffff80021ed47e`) recomputes with
   `setElectionMetricForAirplaySink`/`setElectionMetricForNonAirplaySink` and the bins of
   `initializeAwdlMetricBins` (`0xffffff80021fbc50`: 1.0, 2.0, 3.0, 5.0, 10.0, 255.0, −55.0,
   −65.0, −75.0 as doubles); by default 65 (or 43 with the flag `+66224`), see
   `setElectionMetricsToDefault` (`0xffffff80021cb2cc`). The metric grows with time in the slave
   role: selfLFC `+1` every 2501 ms.
6. If `electionParamsChanged` detected a difference: `updateBroadcastMI` +
   `updateSyncFrameTemplate(true)` + `programChipWithNewMaster` and saving of the previous copy.
   Parent/root changes counted at `+47748`/`+47752`. Hop count ≥ 4 generates a
   diagnostic report (`selfDiagnosticsReport`, "HighHC").

`programChipWithNewMaster` (`0xffffff80021ec4b6`) is the point where the decision goes down to the
driver, always by ioctls (§9): advertisers of the master (type 229), `setSyncFramePeriod`
(`0xffffff80021ed024`, type 116), sync state with the elected master (type 134, payload with
mode `+37263`, master MAC, hopCount, rootMetric, selfMetric) and election parameters (type 155,
payload `{u8 mode +0, u16 electionId +2, u8 hopCount +4, root MAC +6, relay MAC +12, u32 rootMetric
+16, u32 selfMetric +20}`). `initFWElectionParams` (`0xffffff80021ed200`) sends the same 155 when
enabling. After programming, `monitorTimeline` verifies against the driver events that the master
change was applied (§5).

## 4. Sync tree and master indication templates

- The local node keeps the path to the root (`+37500`) and announces it in the tree TLV:
  `buildSyncTreeTlv` (`0xffffff80022112fe`) writes TLV type 20 with
  `len = 3 + 6·min(hopCount+1, 10)`. The receiving side parses it in
  `IO80211AWDLPeer::parseAwdlSyncTreeTLV` (`0xffffff80021b56d0`).
- The peer keeps time sync with a reference peer
  (`setTimeSyncState` `0xffffff8002207e06` → `timeSyncPeer` `+16136`,
  `setTimeSyncChannelAndSchedule` `0xffffff80021f6124`, traffic registration
  `handleTimeSyncTrafficReg` `0xffffff8002207cd4`).
- The templates the master emits are built entirely in software:
  `updateSyncFrameTemplate` (`0xffffff80021dc5b2`) composes 512 B with
  `buildServiceParamTlv` + `buildAWDLElectionParamsVer2` + `buildAwdlDataPathStateTlv` +
  `buildEnhDataRateTlv` (TLV 7) + `buildRangingTlv` + `buildRealTimeGroupTlv` +
  `buildIEEE80211CntnrTlv` (TLV 17) + `buildAWDLVersionTlv` + `buildSyncTreeTlv` +
  `buildNanSyncTlv` + `buildPrefChanTlv`, and pushes them down with the internal ioctl
  `0x97802869C8` (type 151 ?, sync frame template; Broadcom implements
  `setAWDL_SYNC_FRAME_TEMPLATE` at `0xffffff800171da80`).
  `buildMasterIndicationTemplate` (`0xffffff8002211b42`) does the same for the UMIs (adds
  TLV 29 of multi-peer BSS steering, ProMode and AirDrop) over a 1200 B buffer at `+9048`.
  `updateBroadcastMI` (`0xffffff80021dca18`) refreshes the primary payload and, if the
  `SEC_PAYLOAD` capability is declared, the secondary one, with a limit on pending ones
  (`+17004`/`+17008`).

## 5. Availability windows and timeline

- `inAvailabilityWindow` (`0xffffff8002219af4`) returns bit `+772`; the disable cycle clears it
  (outA line 2076) and the driver event path activates it
  (`reportDataPathEvents`, event 41; verified by xref of `monitorTimeline`, the exact writer
  inside that huge function remains unverified `?`).
- Period and length live at `+62940`/`+62944` (`getAvailabilityWindowPeriod/Length`,
  `0xffffff80022355e0`/`0xffffff80022355f0`). The driver receives them via the internal ioctl
  `0x74802869C8` (type 116, `setAWDL_SYNC_PARAMS`): payload `{u32 period +0, u32 length +4,
  u32 ? +8, u32 sync-frame-period +12}` — `setSyncFramePeriod` (`0xffffff80021ed024`) pushes
  there the master indication period (`+62952`). The default values have not been seen
  initialized in the analyzed code; the init writes `0xFDE8` (65000) and `0x105B8` (67000) at
  `+66208`/`+66212` with unit unconfirmed `?`.
- The driver reports every window start: `reportDataPathEvents` (`0xffffff80021dd8f4`,
  cell 83, `+0x298`) consumes the events 41/42/43 from the dispatch and calls
  `monitorTimeline` (`0xffffff80021e11ca`, xref `0x21dfb3d`) and `logAWStart`
  (`0xffffff80021e1b42`, xref `0x21e0286`). The payload `monitorTimeline` expects
  (`apple80211_availability_window_start`): `{u16 AwNumber +0, master MAC +8, topMaster MAC +14,
  u32 FwAwStartTime +20}`; with it, it checks that the programmed master is the one the firmware
  is serving (flag `+10304`) and detects foreign master changes.
- `postSyncStateChanged` (`0xffffff80021e58b4`) publishes the state toward userspace via
  `IO80211VirtualInterface::postSyncStateChanged` (event 46 `AWDL_SYNC_STATE_CHANGED`); the
  numeric state lives at `+17028` (`getSyncState`, `0xffffff800223548c`) and the log translates it
  with `awdlStringForState`. The state values are used as loose constants (0/1/5 in
  `shouldRunElection` and `dequeueTxPacket`); the complete enumeration remains unverified `?`.

## 6. Channel sequence

- The project's own one: `+42864` (u16 length) and `+42868` (12 B entries, `apple80211_channel` format).
  `setMyChanSeq` (`0xffffff8002235302`) validates 6 GHz channels (`is6GChannelALlowedForP2P`).
  `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`) and `recoverFromSetChanSeqFailure`
  (`0xffffff80021f3710`) complete the local choice; `processAWDLDFSChannelSequence`
  (`0xffffff800221a466`) handles the DFS variant.
- Push down to the driver: `setChannelSequenceUnconditional` (`0xffffff80021cb69a`) composes up to
  400 B and sends them with the internal ioctl `0x81802869C8` (type 129,
  `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`; Broadcom `0xffffff8001724436`), and then
  `programMasterChannels` (`0xffffff80021d494a`) fixes the social channels with
  `0x84802869C8` (type 132, `setAWDL_MASTER_CHANNEL`) and `0xCD802869C8` (type 205,
  `setAWDL_SECONDARY_MASTER_CHANNEL`), after updating the list with PSF suppression
  (`updateFWSocialChanListWithPSFSuppressionInfo`, `0xffffff800220db4e`).
- The receiving side parses the sequence of each peer:
  `parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b5a08`),
  `parseAwdlChanSeqFromChanSeqExtTLV` (`0xffffff80021b5e84`),
  `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`) and
  `updateAwdlChanSeq` (`0xffffff80021ab7e4`, with `bounded_ptr<awdl_chan_seq_info2>` and a panic
  trap if out of range), previous validation `isValidChanSeqTlv` (`0xffffff8002214b76`).

## 7. Peer table

- Container: hash table of `IO80211Peer` inherited from `IO80211PeerManager` (base ivars `+1344`,
  list head `+56`, counter `+36`). Add: `addAWDLPeer`
  (`0xffffff800220fe4a`, called by `actionFrameProcessInput` xref `0x220eceb`) →
  `IO80211AWDLPeer::withAddressAndManager` (`0xffffff80021a8dac`) →
  `initWithAddressAndManager` (`0xffffff80021a8e1e`) → `enqueuePeerInOrder`.
  Remove: `removePeer` (`0xffffff800221721c`, cell 79), `removeAllPeers` (`0xffffff800222be56`,
  cell 80), cache `addPeerToCache` (`0xffffff800222dbb2`).
- The peer ivar (QWORD `+0x20`) accumulates what is received: root MAC `+559`, rootLFC `+571`,
  hopCount `+575`, rootMetric `+579`, selfMetric `+583`, electionId u32 `+587`, master mode
  `+120`, time of the last AF `+784`, channel `+716`, presence `+748`, Sync Params fields
  `+756..+904`. Everything filled by `actionFrameReport`.
- Full database for userspace: `getAwdlPeerDatabase` (`0xffffff800221ff6c`, type 203,
  208480 B). Cleanup by absence: `cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`, event 47).

## 8. Action frames

### 8.1 Reception

`actionFrameInput` (`0xffffff800220e1ae`, cell 71, `+0x238`) — the interface is who invokes it
with every captured AWDL action frame:

- IOReport counters, `sendToBpfTap` of the interface (BPF tap of awdl0, see `AWDL_EVENTS.md`).
- If `wlan.splitAWDLAFThread` is active (default 1), enqueues into its own ring (head `+16848`,
  tail `+16852`, size `+16856`, overflow flag `+16868`; `growAFRing`
  `0xffffff800220dd0a` / `shrinkAFRing` `0xffffff800220dfca`) and dispatches with the timer
  `+10408`: `actionFrameProcessThread` (`0xffffff80021c3ebc`). If not, processes inline.

`actionFrameProcessInput` (`0xffffff800220e8b0`, 0x1174 B) validates (`isValidAF`
`0xffffff800220fa64`, OUI with `actionFrameHasCorrectOUI` `0xffffff8002210200`), creates the peer
if new (`addAWDLPeer`) and delegates the body parsing to
`IO80211AWDLPeer::actionFrameReport` (`0xffffff80021af6be`, 0x37f0 B), which walks all the TLVs:
Sync Params (`parseAwdlSyncParamsTlv` `0xffffff80021b4ba6` — raises the re-election flag
`+10306` of the manager), Election Params V1/V2, Chan Seq (+ext), Sync Tree, Data Path State, Pref
Chan, IEEE80211 container, version, NAN sync, ranging, RTG, AirDrop, Pro Mode, BSS steering and
service response; `doMinimalAFProcessing` (`0xffffff80021b3ea6`) and `shouldStopAFProcessing`
(`0xffffff80021b48b8`) bound the work. The UMIs go to `postAFToP2pd` (`0xffffff800220fc2c`)
toward the daemon, and the UMI management chains `isSafeToSendUmiNow` (`0xffffff8002210108`) and
`IO80211AWDLPeer::addPeerToUmiChain` (`0xffffff80021bcb44`).

### 8.2 Transmission

The manager does not schedule sync TX by itself: it builds templates (§4) and pushes them down by
ioctl (151 ?); the chip repeats them in each window according to the period of ioctl 116. For
one-off UMIs, `sendUnicastMI` (`0xffffff8002213c0a`) requires being inside the window (bit
`+772`), builds the peer template and sends it with the internal ioctl `0x95802869C8` (type 149 ?,
payload with own BSSID `+37204/+37208`, destination MAC, option 6 or 14 "UMIOnDwell", template and
length at
`+10248`); `sendRealTimeUMI` (`0xffffff80022020e8`) and `setAFTXMode` (`0xffffff80021f7d06`, ioctl
type 208) complete the family.

### 8.3 Data

Pull model: the driver asks for packets during its windows.

- `dequeueTxPacket(TxPacketRequest*)` (`0xffffff800221675e`, cell 85, `+0x2A8`): validates the
  state, locks the datapath and fills the `mbuf` array of the request; checks
  availability (`+408`), state 5 and data mode (`+17032`).
- `getUnicastPackets` (`0xffffff8002216a4e`, cell 46) and `getMulticastPackets`
  (`0xffffff8002216a2e`, cell 45) by WME class; queues at `+10424`/`+10448`, with
  `stashMulticastPacketPair` (`0xffffff8002213b16`) to duplicate multicast to the second channel
  (the Broadcom PCIe dequeue uses it, see `AWDL_PROXIMITY_DATAPATH.md` §3).
- Data entry: `inputPacket` (cell 70, base) and `formatInputPacket`/`formatOutputPacket`
  (cells 41/40).

## 9. Contract with the driver

### 9.1 PeerManager vtable cells (`0xffffff800245a868`) the upper stack uses

| Cell | Offset | Method | Who triggers it |
|---:|---:|---|---|
| 55 | `+0x1B8` | `interfaceStateChange` | lifecycle of the virtual interface |
| 56 | `+0x1C0` | `setAwdlCtl` (and 64 `getAwdlCtl`) | AWDL_CTL ioctl from userspace |
| 70 | `+0x230` | `inputPacket` | RX data path |
| 71 | `+0x238` | `actionFrameInput` | every received AWDL action frame |
| 45/46 | `+0x168`/`+0x170` | `getMulticastPackets`/`getUnicastPackets` | TX pull by WME class |
| 85 | `+0x2A8` | `dequeueTxPacket` | datapath TX pull |
| 83 | `+0x298` | `reportDataPathEvents` | driver events (41/42/43/47/…, see `AWDL_EVENTS.md` §3.2) |
| 72/73 | `+0x240`/`+0x248` | `doEnable`/`doDisable` | interface state machine |
| 51/52 | `+0x198`/`+0x1A0` | `reset`/`awdlChipReset` | controller reset |
| 57/58 | `+0x1C8`/`+0x1D0` | `setScanningState`/`setJoiningState` | scan subsystem |
| 61/62 | `+0x1E8`/`+0x1F0` | `setInfraTxState`/`notifyHostapState` | infra interface |
| 42/43 | `+0x150`/`+0x158` | `countryCodeStateChanged`/`handleChannelSwitchAnnouncement` | country and CSA |

The exact Skywalk stack path that invokes `dequeueTxPacket`/`getUnicastPackets` in the generic
case remains unverified `?`; in the Broadcom case the dequeue of the PCIe queue interacts with the
manager for multicast (`stashMulticastPacketPair`), per `AWDL_PROXIMITY_DATAPATH.md`.

### 9.2 Internal apple80211 ioctls the manager emits (`IO80211PeerManager::ioctl_internal`)

Observed encoding: command qword with low token `0x802869C8` (`_IOR`/`_IOWR` of group 'i',
number 200, length 40) and the real apple80211 type in the high dword; a variant `0xC02869C9`
appears in the capability read. Identified types:

| Encoding | Type | Command | Observed payload |
|---|---:|---|---|
| `0x75802869C8` | 117 | `setAWDL_SYNC_ENABLED` | enable/disable |
| `0x155C02869C9` | ? | read of `awdl_cap` (bit 0 `SEC_PAYLOAD`, 2 `CCA_STATS`, 8 `PSF_DWELL_MI`, 16 `PSF_DWELL_UMI`) | `?` |
| `0x74802869C8` | 116 | `setAWDL_SYNC_PARAMS` | `{period, length, ?, sync-frame-period}` |
| `0x97802869C8` | 151 ? | sync frame template | 512 B (§4) |
| `0x95802869C8` | 149 ? | UMI send | BSSID, destination, option, template |
| `0x81802869C8` | 129 | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 400 B |
| `0x84802869C8` | 132 | `setAWDL_MASTER_CHANNEL` | u32 social channel |
| `0xCD802869C8` | 205 | `setAWDL_SECONDARY_MASTER_CHANNEL` | u32 social channel |
| `0x86802869C8` | 134 | `setAWDL_SYNC_STATE` | 24 B: mode, master MAC, hopCount, metrics |
| `0x9B802869C8` | 155 | `setAWDL_ELECTION_PARAMS` | 24 B (§3.2) |
| `0xE5802869C8` | 229 | `setAWDL_AWDL_ADVERTISERS` | master advertisers |
| `0xD0802869C8` | 208 | `setAWDL_AF_TX_MODE` | action frame TX mode |

Types 116/117/129/132/134/155/205/208/229 match the table of
`AWDL_APPLE80211_DISPATCH.md`; 149/151 do not appear there and are identified by semantics
(log names and the Broadcom setter) `?`.

### 9.3 What Broadcom answers today (and that a kext without AWDL firmware must emulate)

| Command | Broadcom setter | What the firmware does behind |
|---|---|---|
| `setAWDL_SYNC_PARAMS` 116 | `0xffffff800171ddee` | AWDL timing iovars (`AWDL_FIRMWARE_DUTIES.md` §awdl_sync_params) |
| `setAWDL_SYNC_ENABLED` 117 | `0xffffff800171e42a` | master switch `awdl` |
| `setAWDL_SYNC_STATE` 134 | `0xffffff800171e164` | master/non-electoral/slave role (`WLC_E_AWDL_ROLE` 97) |
| `setAWDL_MASTER_CHANNEL` 132 | `0xffffff800171ea1a` | primary social channel |
| `setAWDL_SECONDARY_MASTER_CHANNEL` 205 | `0xffffff800171f01e` | secondary social channel |
| `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` 129 | `0xffffff8001724436` | dwell along the sequence |
| `setAWDL_ELECTION_PARAMS` 155 | `0xffffff800171c4ca` | iovar `awdl_election_tree` |
| `setAWDL_AWDL_ADVERTISERS` 229 | `0xffffff800171c932` | advertisers in the MI |
| `setAWDL_SYNC_FRAME_TEMPLATE` ? | `0xffffff800171da80` | template the firmware repeats |
| `setAWDL_AF_TX_MODE` 208 | `0xffffff80017199c4` | action frame TX mode |

## 10. Reuse verdict

A third-party kext whose interface subclasses `IO80211VirtualInterface`/`IO80211AWDLProtocol` with
role
6 can use the kernel PeerManager as-is, without re-implementing election or sync:

- The factory is inherited: `createPeerManager` (slot 355) checks role 6 and builds
  `IO80211AWDLPeerManager::withInterface`. There is no need to link PeerManager symbols: the
  class registers via its `gMetaClass`, but `initWithInterface` is not virtual, so the supported
  path is the inheritance of `createPeerManager` (calling `allocClassWithName` and mounting the
  state by hand is not supported by any visible path).
- The whole election machine, the sync tree, the MI/UMI templates, the TLV parsing, the
  peer table and the 14 timers stay on the kernel side: the driver computes no protocol.

What the driver must provide, in order of criticality:

1. **Repetition of the sync frame template.** Receive ioctl 151 ? with the 512 B template and
   transmit it every `syncFramePeriod` (ioctl 116) aligned with the shared timeline —
   on Broadcom the firmware does it; without it, the driver needs its own clock and TX scheduler.
2. **Timeline events.** Publish through `reportDataPathEvents` the events 41/42/43
   (window start, extension, end) with the format of `apple80211_availability_window_start`
   (`{AwNumber, master, topMaster, FwAwStartTime}`) so that `monitorTimeline` validates the state;
   and 47 of peer presence. Without firmware, the driver itself must measure its windows.
3. **Channel change per sequence.** Execute the dwell of ioctl 129 (channel sequence) and set the
   social channels (132/205).
4. **RX of action frames.** Capture the AWDL frames on the social channels and call
   `actionFrameInput` (cell 71) with the buffer and the header; without this there are no peers,
   nor election, nor discovery.
5. **Data TX pull.** During the windows, serve `dequeueTxPacket`/`getUnicastPackets`/
   `getMulticastPackets` and transport the mbufs (the driver decides the instant inside the
   window; the manager decides what).
6. **Pure slots of `IO80211AWDLProtocol`.** The 64 signatures of `AWDL_PURE_SLOTS.md` remain
   mandatory for the interface to construct; the ones the PeerManager touches directly are few
   (getWorkLoop of the controller, getSelfMacAddr 417, link state `+2952`, scan manager 374).
7. **Sync state.** Accept ioctl 134 (master/slave mode and elected master) and reflect it in
   the radio: it is the equivalent of the Broadcom `WLC_E_AWDL_ROLE`.
8. **Honest capabilities.** The `awdl_cap` read decides whether the manager builds secondary
   payload and PSF dwell; declaring without support reduces the driver work.

Risks: the cited ivar offsets belong to build 25.6.0 and move without notice between
versions of IO80211Family (the kext cannot depend on them except for debugging); the
encoding of the internal ioctls and their complete list are observed, not documented by Apple;
and the manager assumes a controller with a workloop (`getController` → `getWorkLoop`) and the
datapath lock of the base `IO80211PeerManager`, which the subclass inherits if the base init runs.
Additionally, subclassing the AWDL classes of IO80211Family requires linking against their symbols
(vtable and `gMetaClass`) without a KDK; AirLayer already does it with `IO80211InfraProtocol`, and
`AWDL_KEXT_LINKAGE.md` checks in the KC symbol table that `IO80211VirtualInterface`,
`IO80211AWDLProtocol` and `IO80211AWDLPeerManager` also export vtable, `gMetaClass`, constructors
and the contract methods (type `0x0f`) on 25G241.
