# Map of readers of the card capabilities block (`APPLE80211_IOC_CARD_CAPABILITIES`) on macOS 14.8.9 (23J631) and macOS 15.8.2 (24H232)

## 1. Overview

This document ports the exhaustive map of readers of the card capabilities block
(`APPLE80211_IOC_CARD_CAPABILITIES`) from macOS 26.7.1 (build 25G241, documented in
`Docs/AWDL_CARD_CAPS_RESTANTES.md`) to macOS 15.8.2 (build 24H232) and macOS 14.8.9 (build 23J631),
analyzing the corresponding x86_64 kernel collections with static disassembly and instruction
inspection.

Main conclusions:
1. **Storage architecture and centralization:**
   - On macOS 14 (23J631) **there is no** centralized capabilities buffer inside
     `IO80211Controller`. Each subsystem (`IO80211AWDLPeerManager`, `IO80211InfraInterface`) runs its own
     internal ioctl (`CARD_CAPABILITIES = 12`) and stores a local copy in its ivars or substructures.
     In `IO80211AWDLPeerManager::configure`, the 28-byte copy is stored at `sub + 0xB18` (u32 version)
     and `sub + 0xB1C..+0xB33` (capabilities `p[4..27]`).
   - On macOS 15 (24H232) the block is centralized with `IO80211Controller::loadCardCapabilities` and
     `isCardCapabilitiesSet`, at `*(controller + 0x120) + 0xB20` (version) and
     `+0xB24..+0xB3B` (capabilities).
   - On macOS 26 (25G241), the controller structure grew shifting the base to
     `*(controller + 0x120) + 0xB30..+0xB4B` (`+0xB30` version, `+0xB34..+0xB4B` capabilities).
2. **Constant shift (delta):**
   - Between 25G241 and 24H232: **`offset_24H232 = offset_25G241 - 0x10`** (exact delta of `-0x10`,
     e.g. `p[4]` goes from `+0xB34` to `+0xB24`; `p[7]` from `+0xB37` to `+0xB27`; `p[15]` from
     `+0xB3F` to `+0xB2F`; `p[21]` from `+0xB45` to `+0xB35`).
   - Between 25G241 and 23J631 (for the AWDL readers): **`offset_23J631 = offset_25G241 - 0x18`**
     (exact delta of `-0x18`, e.g. `p[4]` at `+0xB1C`; `p[7]` at `+0xB1F`; `p[15]` at `+0xB27`).
3. **Functional behavior of key gates:**
   - **Dual band (`p[7]` bit `0x20`, capability 29):** exactly the same 4 readers in all three
     versions (`configureValidAWDLChannels`, `handleAssistedDiscoveryTrafficReg`,
     `fillLegacyDataPathPayload` and `printDataPath`, the latter only for a log), with identical effect.
     Without this bit, the 5 GHz social channel stays at 0 and AWDL operates exclusively on channel 6.
   - **SoftAP + AWDL concurrency (`p[13]` bit `0x40`, capability 78):** identical in all three versions
     through `configure`, feeding the state flag for `validateForHostapAWDLConcurrency` and
     `notifyHostapState`.
   - **Pure userspace gates:** `p[8]` bit 0 (AWDL, `wifip2pd`), `p[13]` bit 3 (SAE, `airportd`) and
     capability 72 (NAN, `wifip2pd`) have no readers in the kernel collection in any of the three versions.
   - **HE extensions (`p[21]`, `+0xB45`):** completely absent on macOS 14. On macOS 15, 13 of the 18
     rows of 25G241 exist (`getHeCapable`, metrics in `IO80211Peer`); the remaining 5, AWDL metrics
     ones, are macOS 26 only.
   - **Limited SDB (`p[23]` bit `0x20`), ProMode 5 (`p[24]` bit `0x10`) and `p[24]` bits 1..3 in
     `configure`:** macOS 26 exclusive; they do not exist on macOS 14 or macOS 15.

---

## 2. Storage architecture and capabilities filling

### macOS 26.7.1 (25G241)
- The controller centralizes the copy at `*(controller + 0x120) + 0xB30` (u32 version) and
  `+0xB34..+0xB4B` (24 bytes of capabilities `p[4..27]`).
- Filling: `IO80211Controller::loadCardCapabilities` (`0xffffff800229a202`) requests 28 bytes from the
  driver via `apple80211getCARD_CAPABILITIES` (or WCL `sendIOUCToWcl(0xC02869C9, 12, ...)`).
- Accessor: `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800229a2b2`) consults
  `byte *(card + n/8 + 2868)` with mask `1 << (n & 7)` (`2868 = 0xB34`).

### macOS 15.8.2 (24H232)
- The controller centralizes the copy at `*(controller + 0x120) + 0xB20` (u32 version) and
  `+0xB24..+0xB3B` (24 bytes of capabilities `p[4..27]`).
- Filling: `IO80211Controller::loadCardCapabilities` (`0xffffff800222cdf0`) first asks for the GET
  through the WCL bridge (`IO80211Glue::sendIOUCToWcl(0xC02869C9, 12, …)` with destination
  `*(this + 0x120) + 0xB20` and length `0x18`) and, if WCL does not serve it, calls
  `apple80211getCARD_CAPABILITIES` with the same destination `+0xB20`.
- Accessor: `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800222cea0`) consults
  `byte *(card + n/8 + 0xB24)` (`0xffffff800222ceb8`).
- Shift relation: **`offset_24H232 = offset_25G241 - 0x10`**.

### macOS 14.8.9 (23J631)
- **No centralized controller buffer:** `IO80211Controller::loadCardCapabilities` and
  `isCardCapabilitiesSet` do not exist.
- **Copy in `IO80211AWDLPeerManager`:** during `IO80211AWDLPeerManager::configure`
  (`0xffffff80020b55e6`), the manager calls `IO80211PeerManager::ioctl_internal` with `0xC02869C9`
  and type 12 (`CARD_CAPABILITIES`) and destination `sub + 0xB18` of the substructure
  `sub = *(this + 0x18)`: u32 version at `+0xB18` and capabilities `p[4..27]` at
  `+0xB1C..+0xB33` (the first following test reads `sub + 0xB1C & 4`). All internal readers of the
  AWDL subsystem access through this substructure (`pm + 0x18 -> sub + 0xB1C + k`).
- **Copy in `IO80211InfraInterface`:** during `initCapabilities` (`0xffffff8002204bec`), the GET is
  requested (`apple80211RouteInternal(0xC02869C9, 12, …)` or `apple80211getCARD_CAPABILITIES`,
  depending on the boot-arg `io80211.isNeedToUseIocExternalLegacyRouting`) and 16 bytes
  (`p[4..19]`) are copied to `ivars + 28..43` (`*(this + 0x118) + 28`). The infrastructure methods
  (`updateLinkSpeed`) read from these own ivars.
- **Copy in `IO80211VirtualInterface`:** `attachToBpf` (`0xffffff80021d886c`) does its own GET (the
  same choice between `apple80211RouteInternal` and `apple80211getCARD_CAPABILITIES`) over a local
  buffer to evaluate `p[7] & 1` `?` (the concrete bit was not re-read).
- Shift relation for the AWDL stack: **`offset_23J631 = offset_25G241 - 0x18`**.

---

## 3. Readers table on macOS 15.8.2 (24H232)

The 95 reading sites identified on macOS 26.7.1 (25G241) were verified. Of them, 81 match 1:1 by
symbol and offset (`delta = -0x10`), while 14 correspond to refactored sites, absent ones or
functions that do not exist on macOS 15.

| 25G241 VA | Function | 24H232 VA | Offset | Mask | Same effect (yes/no/?) | Note |
|---|---|---|---|---|:---:|---|
| `0xffffff80021aeaf7` | `IO80211AWDLPeer::getCumSlotDataStats` | `0xffffff800214e6c9` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff80021aeb5d` | `IO80211AWDLPeer::getIntervalSlotDataStats` | `0xffffff800214e72f` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff80021af973` | `IO80211AWDLPeer::actionFrameReport` | `0xffffff800214f579` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff80021b4117` | `IO80211AWDLPeer::doMinimalAFProcessing` | `0xffffff800215396a` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021bd48e` | `IO80211AWDLPeer::processAirDropEnd` | `0xffffff800215c2d6` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021c3989` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Absent on macOS 15: multicast timer does not check HE metrics |
| `0xffffff80021c3a45` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Absent on macOS 15: multicast timer does not check HE metrics |
| `0xffffff80021c56d8` | `IO80211AWDLPeerManager::configure` | `0xffffff8002163f0c` | `+0xB24` | `& 0x04` | yes | Identical (delta -0x10) |
| `0xffffff80021c5804` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164035` | `+0xB29` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021c580d` | `IO80211AWDLPeerManager::configure` | `0xffffff800216403e` | `+0xB2C` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021c585e` | `IO80211AWDLPeerManager::configure` | `0xffffff800216408f` | `+0xB2B` | `& 0x02` | yes | Identical (delta -0x10) |
| `0xffffff80021c58b3` | `IO80211AWDLPeerManager::configure` | `0xffffff80021640e4` | `+0xB2D` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff80021c5905` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164136` | `+0xB2B` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021c5957` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164188` | `+0xB2C` | `& 0x08` | yes | Identical (delta -0x10) |
| `0xffffff80021c59e4` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164215` | `+0xB32` | `& 0x01` | yes | Identical (delta -0x10) |
| `0xffffff80021c5a39` | `IO80211AWDLPeerManager::configure` | `0xffffff800216426a` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021c5a83` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Absent on macOS 15: Limited SDB capability (+0xB47 / +0xB37) does not exist |
| `0xffffff80021c6373` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164a90` | `+0xB34` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff80021c63c4` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Absent on macOS 15: configure does not read p[24] (+0xB48 / +0xB38) |
| `0xffffff80021c63d6` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Refactored on macOS 15: DeviceCannotRoamDuringAWDL governed by +0xB34 & 0x40 (Leaf Mode) |
| `0xffffff80021c63e8` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Refactored on macOS 15: DeviceCannotRoamDuringAWDL governed by +0xB34 & 0x40 (Leaf Mode) |
| `0xffffff80021c88b3` | `IO80211AWDLPeerManager::updatePref6GChannel` | `0xffffff8002166e21` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021cab1e` | `IO80211AWDLPeerManager::configureValidAWDLChannels` | `0xffffff8002169030` | `+0xB27` | `& 0x20` | yes | Identical (delta -0x10) |
| `0xffffff80021cde35` | `IO80211AWDLPeerManager::setAWDLMaxBandwidth` | `0xffffff800216c0f5` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021dcfce` | `IO80211AWDLPeerManager::enableAWDL` | `0xffffff800217a4ee` | `+0xB2F` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff80021e8778` | `IO80211AWDLPeerManager::removePeerFromRealTimeGroup` | `0xffffff8002184f69` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021e9790` | `IO80211AWDLPeerManager::processPeerProModeUMIStatus` | `0xffffff8002185d58` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021ed766` | `IO80211AWDLPeerManager::monitorAWDLState` | `0xffffff8002189b96` | `+0xB2F` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff80021f185b` | `IO80211AWDLPeerManager::monitorProModeState` | `0xffffff800218d9cb` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021f353a` | `IO80211AWDLPeerManager::getScanTimeout` | `0xffffff800218f5d6` | `+0xB2F` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff80021f5fe8` | `IO80211AWDLPeerManager::is6GChannelALlowedForP2P` | — | — | — | no | Refactored on macOS 15 into isValid6GChannelForP2P (without direct card read) |
| `0xffffff80021fa183` | `IO80211AWDLPeerManager::findCommon6GChannel` | `0xffffff8002195c59` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80021faa7a` | `IO80211AWDLPeerManager::setHighThruputState` | `0xffffff8002196514` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002203a10` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Absent on macOS 15: handleEnsembleTrafficReg does not evaluate card capability |
| `0xffffff8002203f52` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Absent on macOS 15: handleEnsembleTrafficReg does not evaluate card capability |
| `0xffffff800220456e` | `IO80211AWDLPeerManager::airplayStatsPostMetrics` | `0xffffff800219fc9c` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002205f0c` | `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` | `0xffffff80021a16a1` | `+0xB27` | `& 0x20` | yes | Identical (delta -0x10) |
| `0xffffff8002206709` | `IO80211AWDLPeerManager::handleMigrationTrafficReg` | `0xffffff80021a1e8c` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002207f4b` | `IO80211AWDLPeerManager::handleAirPlaySinkTrafficReg` | `0xffffff80021a2d27` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002207fca` | `IO80211AWDLPeerManager::isAirPlaySinkEnabled` | `0xffffff80021a2da6` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002208e27` | `IO80211AWDLPeerManager::handleAirDropTrafficReg` | `0xffffff80021a3b94` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff800220bdb8` | `IO80211AWDLPeerManager::setAwdlPeerTrafficRegistration` | `0xffffff80021a67fd` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002210f14` | `IO80211AWDLPeerManager::buildAwdlDataPathStateTlv` | `0xffffff80021ab94f` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff800221144a` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80021abe7a` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80022115f5` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80021abffb` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002212790` | `IO80211AWDLPeerManager::buildRangingTlv` | — | — | — | no | Absent on macOS 15: buildRangingTlv only checks bit 0x08 (RSDB), not the ranging bit 0x10 |
| `0xffffff80022143fb` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80021aebc5` | `+0xB27` | `& 0x20` | yes | Identical (delta -0x10) |
| `0xffffff800221470a` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80021aee8f` | `+0xB35` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002223aa2` | `IO80211AWDLPeerManager::printDataPath` | `0xffffff80021bceb6` | `+0xB27` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002224acc` | `IO80211AWDLPeerManager::awdlStatsCollectCachedPeersStats` | `0xffffff80021bde56` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff800222a4eb` | `IO80211AWDLPeerManager::rtgStatsPostMetrics` | — | — | — | no | Absent on macOS 15: function does not exist on 24H232 |
| `0xffffff800222f9bc` | `IO80211AWDLPeerManager::submitRTGMetricstoCoreAnalytics` | `0xffffff80021c6e0c` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002236d9e` | `IO80211AWDLPeerManager::isProMode5Enabled` | — | — | — | no | Absent on macOS 15: function does not exist on 24H232 (introduced in macOS 26) |
| `0xffffff8002236dfa` | `IO80211AWDLPeerManager::processPeerProModeTlv` | `0xffffff80021cd720` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80022379f2` | `IO80211AWDLPeerManager::is6GChanSeqAllowed` | `0xffffff80021ce248` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff800223827d` | `IO80211AWDLPeerManager::isProModeAllowed` | `0xffffff80021ceada` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002238435` | `IO80211AWDLPeerManager::shouldEnterProMode` | `0xffffff80021cec6f` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80022387c5` | `IO80211AWDLPeerManager::enteringProMode` | `0xffffff80021ced73` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002238b6b` | `IO80211AWDLPeerManager::notifyPeerRemovedFromCache` | `0xffffff80021cf117` | `+0xB2F` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff8002238d54` | `IO80211AWDLPeerManager::logRealTimePrintStats` | — | — | — | no | Absent on macOS 15: function does not check HE capability |
| `0xffffff8002243196` | `IO80211Peer::updateCumDataStats` | `0xffffff80021d93a6` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff8002243204` | `IO80211Peer::updateIntervalDataStats` | `0xffffff80021d9414` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff800224325f` | `IO80211Peer::clearIntervalDataStats` | `0xffffff80021d946f` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff8002244ec4` | `IO80211Peer::logDataStats` | `0xffffff80021db1e1` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff8002245bb3` | `IO80211Peer::getLastQueuePacket` | `0xffffff80021dbc1f` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff800225b74c` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff80021f1204` | `+0xB29` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff800225b755` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff80021f120d` | `+0xB2C` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff800225ef2f` | `IO80211PeerManager::getLastUnicastRxLinkActivity` | `0xffffff80021f473b` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff800225f406` | `IO80211PeerManager::getHeCapable` | `0xffffff80021f4be2` | `+0xB35` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff800225f501` | `IO80211PeerManager::getLastUnicastTxLinkActivity` | `0xffffff80021f4cd9` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff800225f910` | `IO80211PeerManager::is6ECapable` | `0xffffff80021f50e4` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff800225f932` | `IO80211PeerManager::isInfra6ECapable` | `0xffffff80021f5106` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff800225fb9f` | `IO80211PeerManager::getDataPathPeerStats` | `0xffffff80021f5349` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff800229a2ca` | `IO80211Controller::isCardCapabilitiesSet` | `0xffffff800222ceb8` | `+0xB24` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff800229f502` | `IO80211NANLocaleManager::configure` | `0xffffff8002231cc6` | `+0xB29` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff800229f50b` | `IO80211NANLocaleManager::configure` | `0xffffff8002231ccf` | `+0xB2C` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff800229fbcf` | `IO80211NANLocaleManager::isChannelValid` | `0xffffff80022322d3` | `+0xB2B` | `byte ≠ 0` | yes | Identical (delta -0x10) |
| `0xffffff80022a00b9` | `IO80211NANLocaleManager::isCardChannelCLMRestrictedCapable` | `0xffffff80022327c1` | `+0xB2B` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff80022ad3cd` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff800223e289` | `+0xB2B` | `& 0x02` | yes | Identical (delta -0x10) |
| `0xffffff80022ad443` | `IO80211NANPeerManager::initDefaultNANParams` | — | — | — | no | Absent on macOS 15: nan+12514 set to constant 0 on 24H232 |
| `0xffffff80022ad456` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff800223e2fc` | `+0xB2F` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff80022ad468` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff800223e30e` | `+0xB2C` | `& 0x08` | yes | Identical (delta -0x10) |
| `0xffffff80023303c6` | `IO80211VirtualInterface::attachToBpf` | `0xffffff80022a5ec2` | `+0xB27` | `& 0x01` | yes | Identical (delta -0x10) |
| `0xffffff80023489df` | `IO80211RangingManagerExt::getRangingCaps` | `0xffffff80022be485` | `+0xB36` | `& 0x10` | yes | Identical (delta -0x10) |
| `0xffffff800236003a` | `IO80211InfraInterface::prepareBSDInterface` | `0xffffff80022d66f7` | `+0xB27` | `& 0x01` | yes | Identical (delta -0x10) |
| `0xffffff80023610cb` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d79e8` | `+0xB26` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002361211` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d7b30` | `+0xB26` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff80023612f3` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d7c09` | `+0xB29` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002361327` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d7c38` | `+0xB26` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002362d21` | `IO80211InfraInterface::updateChanOutageTime` | `0xffffff80022d9603` | `+0xB2B` | `full byte` | yes | Identical (delta -0x10) |
| `0xffffff8002366555` | `IO80211InfraInterface::createLinkQualityMonitor` | `0xffffff80022dcf91` | `+0xB2E` | `& 0x08` | yes | Identical (delta -0x10) |
| `0xffffff8002366a4e` | `IO80211InfraInterface::createLinkQualityMonitor` | `0xffffff80022dd4a5` | `+0xB30` | `& 0x20` | yes | Identical (delta -0x10) |
| `0xffffff8002367b9e` | `IO80211InfraInterface::updateTxRxLatency` | `0xffffff80022de606` | `+0xB35` | `& 0x40` | yes | Identical (delta -0x10) |
| `0xffffff800237fe49` | `IO80211PeerMonitor::checkForDPS` | `0xffffff80022f6192` | `+0xB2F` | `& 0x10` | yes | Identical (delta -0x10) |
| `0xffffff800238016f` | `IO80211PeerMonitor::recordDPSSymptomsInput` | `0xffffff80022f64ab` | `+0xB2F` | `& 0x08` | yes | Identical (delta -0x10) |

---

## 4. Readers table on macOS 14.8.9 (23J631)

The 95 reading sites were verified against macOS 14.8.9 (23J631). Of them, 51 map directly 1:1 into
the `IO80211AWDLPeerManager` block (`delta = -0x18`), and 44 correspond to capabilities not
implemented on macOS 14 (HE, Limited SDB, Leaf Mode, ProMode 5, p[24]), to the nonexistent
centralized buffer or to readers that consult own infrastructure ivars / local stack.

| 25G241 VA | Function | 23J631 VA | Offset | Mask | Same effect (yes/no/?) | Note |
|---|---|---|---|---|:---:|---|
| `0xffffff80021aeaf7` | `IO80211AWDLPeer::getCumSlotDataStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff80021aeb5d` | `IO80211AWDLPeer::getIntervalSlotDataStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff80021af973` | `IO80211AWDLPeer::actionFrameReport` | `0xffffff80020a12e0` | `+0xB27` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021b4117` | `IO80211AWDLPeer::doMinimalAFProcessing` | `0xffffff80020a5615` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021bd48e` | `IO80211AWDLPeer::processAirDropEnd` | `0xffffff80020ae05c` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c3989` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff80021c3a45` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff80021c56d8` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b585d` | `+0xB1C` | `& 0x04` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c5804` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5971` | `+0xB21` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c580d` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b597a` | `+0xB24` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c585e` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b59bd` | `+0xB23` | `& 0x02` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c58b3` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5a04` | `+0xB25` | `& 0x40` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c5905` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5a48` | `+0xB23` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c5957` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5a8c` | `+0xB24` | `& 0x08` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c59e4` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5b0b` | `+0xB2A` | `& 0x01` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c5a39` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5b52` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021c5a83` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Absent on macOS 14: Limited SDB did not exist on macOS 14 |
| `0xffffff80021c6373` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Absent on macOS 14: Leaf Mode did not exist on macOS 14 |
| `0xffffff80021c63c4` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Absent on macOS 14: capabilities 161..163 are not read on macOS 14 |
| `0xffffff80021c63d6` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Absent on macOS 14: capabilities 161..163 are not read on macOS 14 |
| `0xffffff80021c63e8` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Absent on macOS 14: capabilities 161..163 are not read on macOS 14 |
| `0xffffff80021c88b3` | `IO80211AWDLPeerManager::updatePref6GChannel` | `0xffffff80020b87ab` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021cab1e` | `IO80211AWDLPeerManager::configureValidAWDLChannels` | `0xffffff80020bb433` | `+0xB1F` | `& 0x20` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021cde35` | `IO80211AWDLPeerManager::setAWDLMaxBandwidth` | `0xffffff80020be84f` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021dcfce` | `IO80211AWDLPeerManager::enableAWDL` | `0xffffff80020cc224` | `+0xB27` | `& 0x40` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021e8778` | `IO80211AWDLPeerManager::removePeerFromRealTimeGroup` | `0xffffff80020d6299` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021e9790` | `IO80211AWDLPeerManager::processPeerProModeUMIStatus` | `0xffffff80020d6ec4` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021ed766` | `IO80211AWDLPeerManager::monitorAWDLState` | `0xffffff80020dab10` | `+0xB27` | `& 0x40` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021f185b` | `IO80211AWDLPeerManager::monitorProModeState` | `0xffffff80020de5d3` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021f353a` | `IO80211AWDLPeerManager::getScanTimeout` | `0xffffff80020e0162` | `+0xB27` | `& 0x40` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021f5fe8` | `IO80211AWDLPeerManager::is6GChannelALlowedForP2P` | — | — | — | no | Absent on macOS 14: does not consult the block directly |
| `0xffffff80021fa183` | `IO80211AWDLPeerManager::findCommon6GChannel` | `0xffffff80020e65e7` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80021faa7a` | `IO80211AWDLPeerManager::setHighThruputState` | `0xffffff80020e6eb7` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002203a10` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff8002203f52` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800220456e` | `IO80211AWDLPeerManager::airplayStatsPostMetrics` | `0xffffff80020f75d6` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002205f0c` | `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` | `0xffffff80020f10f9` | `+0xB1F` | `& 0x20` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002206709` | `IO80211AWDLPeerManager::handleMigrationTrafficReg` | `0xffffff80020f1908` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002207f4b` | `IO80211AWDLPeerManager::handleAirPlaySinkTrafficReg` | — | — | — | no | Refactored on macOS 14: delegates to isAirPlaySinkEnabled (+0xB27) |
| `0xffffff8002207fca` | `IO80211AWDLPeerManager::isAirPlaySinkEnabled` | `0xffffff80020f27d2` | `+0xB27` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002208e27` | `IO80211AWDLPeerManager::handleAirDropTrafficReg` | `0xffffff80020f3616` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800220bdb8` | `IO80211AWDLPeerManager::setAwdlPeerTrafficRegistration` | — | — | — | no | Refactored on macOS 14: delegates to isAirPlaySinkEnabled (+0xB27) |
| `0xffffff8002210f14` | `IO80211AWDLPeerManager::buildAwdlDataPathStateTlv` | `0xffffff80020fba01` | `+0xB27` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800221144a` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80020fbf16` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80022115f5` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80020fc07b` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002212790` | `IO80211AWDLPeerManager::buildRangingTlv` | — | — | — | no | Absent on macOS 14: ranging TLV does not consult p[23] |
| `0xffffff80022143fb` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80020fed86` | `+0xB1F` | `& 0x20` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800221470a` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff8002223aa2` | `IO80211AWDLPeerManager::printDataPath` | `0xffffff800210ce82` | `+0xB1F` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002224acc` | `IO80211AWDLPeerManager::awdlStatsCollectCachedPeersStats` | `0xffffff800210dd7e` | `+0xB27` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800222a4eb` | `IO80211AWDLPeerManager::rtgStatsPostMetrics` | — | — | — | no | Absent on macOS 14: function does not exist on 23J631 |
| `0xffffff800222f9bc` | `IO80211AWDLPeerManager::submitRTGMetricstoCoreAnalytics` | `0xffffff8002116be0` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002236d9e` | `IO80211AWDLPeerManager::isProMode5Enabled` | — | — | — | no | Absent on macOS 14: function does not exist on 23J631 |
| `0xffffff8002236dfa` | `IO80211AWDLPeerManager::processPeerProModeTlv` | `0xffffff800211d022` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80022379f2` | `IO80211AWDLPeerManager::is6GChanSeqAllowed` | `0xffffff800211db22` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800223827d` | `IO80211AWDLPeerManager::isProModeAllowed` | `0xffffff800211e39c` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002238435` | `IO80211AWDLPeerManager::shouldEnterProMode` | `0xffffff800211e51d` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80022387c5` | `IO80211AWDLPeerManager::enteringProMode` | `0xffffff800211e605` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002238b6b` | `IO80211AWDLPeerManager::notifyPeerRemovedFromCache` | `0xffffff800211e99f` | `+0xB27` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff8002238d54` | `IO80211AWDLPeerManager::logRealTimePrintStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff8002243196` | `IO80211Peer::updateCumDataStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff8002243204` | `IO80211Peer::updateIntervalDataStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800224325f` | `IO80211Peer::clearIntervalDataStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff8002244ec4` | `IO80211Peer::logDataStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff8002245bb3` | `IO80211Peer::getLastQueuePacket` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800225b74c` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff800214204e` | `+0xB21` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800225b755` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff8002142057` | `+0xB24` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800225ef2f` | `IO80211PeerManager::getLastUnicastRxLinkActivity` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800225f406` | `IO80211PeerManager::getHeCapable` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800225f501` | `IO80211PeerManager::getLastUnicastTxLinkActivity` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800225f910` | `IO80211PeerManager::is6ECapable` | `0xffffff800214587c` | `+0xB27` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800225f932` | `IO80211PeerManager::isInfra6ECapable` | `0xffffff8002145890` | `+0xB27` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800225fb9f` | `IO80211PeerManager::getDataPathPeerStats` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800229a2ca` | `IO80211Controller::isCardCapabilitiesSet` | — | — | — | no | Absent on macOS 14: no centralized card buffer exists in IO80211Controller |
| `0xffffff800229f502` | `IO80211NANLocaleManager::configure` | `0xffffff8002176744` | `+0xB21` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800229f50b` | `IO80211NANLocaleManager::configure` | `0xffffff800217674d` | `+0xB24` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff800229fbcf` | `IO80211NANLocaleManager::isChannelValid` | `0xffffff8002176e76` | `+0xB23` | `byte ≠ 0` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80022a00b9` | `IO80211NANLocaleManager::isCardChannelCLMRestrictedCapable` | `0xffffff8002176f3b` | `+0xB23` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80022ad3cd` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80021808c7` | `+0xB23` | `& 0x02` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80022ad443` | `IO80211NANPeerManager::initDefaultNANParams` | — | — | — | no | Absent on macOS 14: NAN parameters initialized without reading these offsets |
| `0xffffff80022ad456` | `IO80211NANPeerManager::initDefaultNANParams` | — | — | — | no | Absent on macOS 14: NAN parameters initialized without reading these offsets |
| `0xffffff80022ad468` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff8002180928` | `+0xB24` | `full byte` | yes | Identical (delta -0x18 in AWDL substructure) |
| `0xffffff80023303c6` | `IO80211VirtualInterface::attachToBpf` | — | — | — | no | Refactored on macOS 14: attachToBpf consults ioctl_internal(CARD_CAPABILITIES) into a local buffer |
| `0xffffff80023489df` | `IO80211RangingManagerExt::getRangingCaps` | — | — | — | no | Absent on macOS 14: does not consult the card capabilities block |
| `0xffffff800236003a` | `IO80211InfraInterface::prepareBSDInterface` | — | — | — | no | Absent on macOS 14: prepareBSDInterface does not access the card directly |
| `0xffffff80023610cb` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactored on macOS 14: consults own interface ivars (ivars+28..43) |
| `0xffffff8002361211` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactored on macOS 14: consults own interface ivars (ivars+28..43) |
| `0xffffff80023612f3` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactored on macOS 14: consults own interface ivars (ivars+28..43) |
| `0xffffff8002361327` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactored on macOS 14: consults own interface ivars (ivars+28..43) |
| `0xffffff8002362d21` | `IO80211InfraInterface::updateChanOutageTime` | — | — | — | no | Absent on macOS 14: does not read the card block on this path |
| `0xffffff8002366555` | `IO80211InfraInterface::createLinkQualityMonitor` | — | — | — | no | Absent on macOS 14: LQM does not read card capabilities on macOS 14 |
| `0xffffff8002366a4e` | `IO80211InfraInterface::createLinkQualityMonitor` | — | — | — | no | Absent on macOS 14: LQM does not read card capabilities on macOS 14 |
| `0xffffff8002367b9e` | `IO80211InfraInterface::updateTxRxLatency` | — | — | — | no | Absent on macOS 14: HE capability (+0xB45) did not exist on macOS 14 |
| `0xffffff800237fe49` | `IO80211PeerMonitor::checkForDPS` | — | — | — | no | Refactored on macOS 14: DPS evaluates peer+2679 instead of the controller card block |
| `0xffffff800238016f` | `IO80211PeerMonitor::recordDPSSymptomsInput` | — | — | — | no | Refactored on macOS 14: DPS evaluates peer+2679 instead of the controller card block |

---

## 5. Readers on macOS 14 or macOS 15 with no counterpart in the 25G241 document

During the exhaustive sweep of the macOS 14 and 15 bases, a reader belonging to
`IO80211AWDLPeer` was identified that was not listed in the tables of
`Docs/AWDL_CARD_CAPS_RESTANTES.md`:

### `IO80211AWDLPeer::parseAwdlPrefChanTLV`
- **24H232 VA:** `0xffffff8002155c73` (`cmp byte ptr [rdi+0B2Fh], 0`) — `+0x0B2F` (p[15], byte 6E).
- **23J631 VA:** `0xffffff80020a797d` (`cmp byte ptr [rdi+0B27h], 0`) — `+0x0B27` (p[15], byte 6E).
- **25G241 VA:** `0xffffff80021b64fe` (`cmp byte ptr [rdi+0B3Fh], 0`) — `+0x0B3F` (p[15], byte 6E).
- **Behavior:** Checks bit 7 of byte 6E (`p[15] < 0`, capability 95, AWDL 6E). If the host has no
  6E support, it skips processing of TLV 33 (Preferred Channel TLV for 6 GHz bands). The gate
  exists in all three versions with identical effect.

No additional legitimate readers of the card block were detected in any of the analyzed kernel
collections (the remaining accesses to offsets `0xB20..0xB3B` belong to ivars of
`IO80211PeerManager`, `IO80211Controller` or `AppleBCMWLAN*`).

---

## 6. Gates whose meaning or mechanism changes between versions

### 1. `p[7]` bit `0x20` (capability 29, dual band)
- **25G241:** `+0xB37 & 0x20`. 4 readers: `configureValidAWDLChannels` (`0xffffff80021cab1e`),
  `handleAssistedDiscoveryTrafficReg` (`0xffffff8002205f0c`), `fillLegacyDataPathPayload`
  (`0xffffff80022143fb`) and `printDataPath` (`0xffffff8002223aa2`).
- **24H232:** `+0xB27 & 0x20`. Same 4 readers: `0xffffff8002169030`, `0xffffff80021a16a1`,
  `0xffffff80021aebc5`, `0xffffff80021bceb6`.
- **23J631:** `+0xB1F & 0x20`. Same 4 readers: `0xffffff80020bb433`, `0xffffff80020f10f9`,
  `0xffffff80020fed86`, `0xffffff800210ce82`.
- **Effect:** Fully identical in all three versions. Without this bit, the qword `+0x4294`
  (5 GHz social channels 44 and 149) is initialized to zero with the log
  `"Dual band not supported, so assuming 2.4GHz is the basic and should be supported, initialize 5GHz channels to zero"`,
  limiting the sequence to channel 6 only. It is the critical gate that the RTL layer activates with
  `p[7] |= 0x20`.

### 2. `p[8]` bit 0 (capability 32, AWDL)
- **25G241, 24H232 and 23J631:** Zero readers in the kernel collection.
- **Mechanism:** It is a gate consumed exclusively in user space by the daemon `wifip2pd`
  (`VIRTUAL_IF_CREATE`). The RTL layer sets it unconditionally (`p[8] = 0x01`).

### 3. `p[13]` bit 3 (capability 75, SAE / WPA3) and capability 72 (NAN)
- **25G241, 24H232 and 23J631:** Zero readers in the kernel for both capabilities.
- **Mechanism:** Capability 75 (SAE) is consumed by the daemon `airportd` to enable WPA3-Personal.
  Capability 72 (NAN) is consumed by `wifip2pd`. The RTL layer sets SAE (`0x08`) and leaves NAN at zero.

### 4. `p[13]` bit `0x40` (capability 78, SoftAP + AWDL concurrency)
- **25G241:** `+0xB3D & 0x40` in `configure` (`0xffffff80021c58b3`), logs `"Concurrency supported"`
  and sets `state + 0xD066 = 1`.
- **24H232:** `+0xB2D & 0x40` in `configure` (`0xffffff80021640e4`), logs `"Concurrency supported"`
  and sets `state + 43286 = 1` (`0xA916`).
- **23J631:** `+0xB25 & 0x40` in `configure` (`0xffffff80020b5a04`), logs `"Concurrency supported"`
  and sets `state + 42806 = 1` (`0xA736`).
- **Effect:** Identical in all three versions. The resulting state flag is consulted exclusively by
  `validateForHostapAWDLConcurrency` and `notifyHostapState`. It governs coexistence between SoftAP
  and the AWDL interface; it does not affect infrastructure association as a client.

### 5. `p[24]` bits 1 to 3 (capabilities 161, 162 and 163)
- **25G241:** `configure` (`0xffffff80021c63c4`, `0xffffff80021c63d6`, `0xffffff80021c63e8`) reads
  `+0xB48` bits 1, 2 and 3:
  - Bit 1 (cap 161): clears `state + 0x401A` (DFS proxy flag).
  - Bit 2 (cap 162): sets `state + 0x103A0 = 1` (`"DeviceCannotRoamDuringAWDL"`), suspending AWDL or
    forcing channel sequences if the firmware initiates roaming.
  - Bit 3 (cap 163): sets `state + 0x102B1`, selecting whether `gatherInfraStats` consults
    `InterfaceMonitor` counters (0) or the controller vtable slot `+3200` (1).
- **24H232:** `p[24]` is not read at all in `configure`. The flag `DeviceCannotRoamDuringAWDL`
  (`state + 52104`, read in case 127 of `reportDataPathEvents` the same as `state + 0x103A0` on
  25G241) is activated in `configure` when `p[20] & 0x40` (`+0xB34 & 0x40`, Leaf Mode) is set and
  the `enable_awdl_leaf` variable is not 0. That variable is 1 by default and the boot-arg can only
  turn it off. The same branch sets the word `state + 51864` (`257`) to 1, that is, the equivalents
  of `+0x102B0` (Leaf) and `+0x102B1` (infra counters source) of 25G241, and clears
  `state + 16418`; that `gatherInfraStats` reads `state + 51865` was not re-verified `?`. On 25G241
  the Leaf branch still exists (`0xffffff80021c6373`, `+0xB44 & 0x40`, log «Device prefers
  leaf-only operation») but only sets `+0x102B0`; the other two flags moved to `p[24]` bits 2 and 3.
  With the RTL layer (`p[20] = 0`) neither path activates.
- **23J631:** `p[24]` is not read. The AWDL suspension-by-roaming mechanism and the selection of
  alternative sources for infra counters do not exist.

### 6. Byte 6E (`p[15]`: `+0xB3F` on 25 / `+0xB2F` on 24 / `+0xB27` on 23)
- **25G241 and 24H232:** The 6 GHz sequences and pro-mode group contains the same control methods.
  Bit 7 governs `is6ECapable` and `parseAwdlPrefChanTLV`; bit 6 governs `isInfra6ECapable` and scan
  timeouts (35000 vs 25000 ms). On 25G241, bits 3 and 4 additionally governed DPS (`checkForDPS`,
  `recordDPSSymptomsInput`).
- **23J631:** The core 6 GHz sequences and pro-mode methods in `AWDLPeerManager` are present at
  `+0xB27`. However, the DPS functions (`checkForDPS`) read the internal state `peer + 2679`
  instead of the card capabilities block. Additionally, `handleAirPlaySinkTrafficReg` and
  `setAwdlPeerTrafficRegistration` on 23J631 do not read the card directly, but invoke the helper
  `isAirPlaySinkEnabled` (which reads `+0xB27`).
- **Behavior on the RTL layer:** Keeping the byte at zero is the correct configuration for
  RTL8814AU (2.4/5 GHz radio without 6 GHz).

### 7. HE byte (`p[21]`: `+0xB45` on 25 / `+0xB35` on 24 / absent on 23)
- **25G241:** 18 rows of the map read `+0xB45`. Bit 7 in `getHeCapable`, byte != 0 in
  `fillLegacyDataPathPayload` (bit `0x100` in TLV 12), and bit 6 (`& 0x40`) in multiple metric and
  data counter methods.
- **24H232:** 13 of those rows exist with `+0xB35`. Full byte in `getHeCapable`
  (`0xffffff80021f4be2`), byte != 0 in `fillLegacyDataPathPayload` (`0xffffff80021aee8f`), and bit 6
  in the data methods of `IO80211Peer`, `IO80211PeerManager` and `IO80211InfraInterface`. The
  remaining 5 rows (`handleMulticastTimer` x2, `handleEnsembleTrafficReg` x2, `logRealTimePrintStats`)
  did not include the card check on macOS 15.
- **23J631:** Zero readers in the kernel. The 802.11ax (HE) capability did not exist on macOS 14.

### 8. Byte `p[23]` (Limited SDB and Ranging)
- **25G241:** `configure` reads `+0xB47 & 0x20` to activate Limited SDB (`state + 0x103FC = 1`), and
  `buildRangingTlv` reads `+0xB47 & 0x10` for the ranging TLV 30.
- **24H232 and 23J631:** Neither Limited SDB nor the `0x10` bit test in `buildRangingTlv` exist.
  On 24H232, `buildRangingTlv` only evaluates the RSDB flag (`v5 + 32776 == 1` -> bit `0x08`).

---

## 7. Open points

1. **`InterfaceMonitor` channel feeding on macOS 15:** On 24H232, `gatherInfraStats` consults the
   `InterfaceMonitor` channels (`IfTxPkBe`, `IfTxPkBk`, `IfTxPkVo`, `IfTxPkVi`, etc.) by default
   (unless Leaf Mode is configured). Still to verify dynamically whether the macOS 14/15 Skywalk
   subsystem updates these counters during normal traffic of a virtual infrastructure interface
   over third-party drivers `?`.
2. **Polarity of secondary branches in pro-mode (`+0xB2F` / `+0xB27`):** It was verified that any
   non-zero byte activates the pro-mode and 6 GHz sequences paths in all 3 versions; the individual
   internal pro-mode branches have not been exhaustively traced `?` since the RTL layer sets the byte
   to 0x00.
