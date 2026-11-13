# Block Ack in AWDL (role 6): who handles it and what the RTL8814 layer must implement

Who receives, interprets and answers the ADDBA/DELBA/BAR frames and the A-MPDUs for `awdl0` on
macOS 26.7.1 (25G241, Darwin 25.6.0), and what remains for a driver without Broadcom firmware.
Closes the open question of [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §4 (Block Ack row) with
the data point from
[AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §7 (winawdl: accepting the iPhone's
ADDBA on TID 0 with window 64 raised throughput from 971 kB/s to 1.7 MB/s).

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), macOS 25.6.0,
Hex-Rays decompilation of the filesets `com.apple.driver.AppleBCMWLANCoreMac`,
`com.apple.driver.AppleBCMWLANBusInterfacePCIeMac` and `IO80211Family`. Addresses = KC VAs,
all seen in the disassembly/decompiled output. Anything not observed carries `?`.

**Short summary:**

1. **The family does not do Block Ack.** The two handlers of the request `APPLE80211_IOC_BLOCK_ACK`
   (`apple80211getBLOCK_ACK` `0xffffff80022632f4`, `apple80211setBLOCK_ACK` `0xffffff8002268050`)
   are stubs that always return the error `0xE082280E`, and their address does not appear in any
   handler table of the range `0xffffff8002440000`–`0xffffff8002500000`: selector and handlers are
   dead in 25.6.0. Neither `IO80211AWDLPeerManager` nor `IO80211AWDLPeer` parse or answer
   ADDBA/DELBA/BAR (verified over the decompiled bodies of `actionFrameProcessInput` and
   `doMinimalAFProcessing`: zero references to Block Ack/AMPDU).
2. **On Broadcom the firmware does all the Block Ack.** The host only registers each peer as
   A-MPDU capable via the request `APPLE80211_IOC_PEER_CACHE_CONTROL` (143 B struct,
   flag `ampdu` at +44), which the proximity interface turns into the firmware iovar `awdl_peer_op`.
   The sessions (ADDBA req/resp, DELBA, BAR, window, reordering) are internal to the
   firmware; the iovars `ampdu_tid`, `ampdu_send_addba`, `ampdu_send_delba`,
   `ampdu_tx_ba_window_cfg`, `ampdu_rx_ba_window_cfg`, `ampdu_rx_density`, `ampdu_rx_factor` are its
   only lever.
3. **The per-peer event that opens/closes the A-MPDU is the traffic registration**: wifip2pd emits
   `APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION` (164 B) → `setAwdlPeerTrafficRegistration` →
   for the «High Thruput» type `initiateHighThruputMode` → `addPeerToFWCache` → the same
   `PEER_CACHE_CONTROL`. The High Thruput/RTG mode is a D2D migration, not a BA session.
4. **RX reordering does not exist in the family nor in the Skywalk datapath**: the `RxCompletionQueue`
   delivers as-is whatever the driver puts in. With Broadcom firmware, the BA window (counters
   out-of-window/out-of-sequence/duplicate/stuck) is firmware. The RTL layer must deliver in order.
5. **The TX pull does not limit aggregation**: `TxPacketRequest` (96 B) has no A-MPDU
   fields; the decision belongs to the driver/firmware.
6. **For the RTL8814 layer**: answer the ADDBA itself (the family will never do it),
   process DELBA and BAR, keep a reordering buffer per (peer, TID) and deliver in order to the
   `RxCompletionQueue`. The request `setPEER_CACHE_CONTROL` (in the layer, today it only confirms with event 51,
   `AWDLInterface.h`) is the per-peer switch exactly equivalent to the one Broadcom gives the
   firmware.

## 1. Method

Passes over a copy of the IDB in `Build/ida_awdl/task_blockack/`: (a) global search of
strings and names with patterns `addba`, `delba`, `blockack`, `block_ack`, `ampdu`, `amsdu`,
`reorder`, `ba_window`, `ba_session`, `thruput`, `rtgtx`, `traffic_reg`, `aggregat`;
(b) decompilation of the 30+ cited functions;
(c) call paths via xrefs; (d) reading of the family dispatcher handler table.
The cited VAs are instruction heads or function starts seen in the output.

## 2. IO80211Family Block Ack surface: dead

`apple80211RouteExternal` (`0xffffff800236cd2e`, family dispatcher) dispatches by
`req->req_type` (dword at `apple80211req+16`) with two pairs of tables: the legacy
ones (`_gGetHandlerTableLegacy` `0xffffff8002470d50`, `_gSetHandlerTableLegacy` `0xffffff8002479060`)
for selectors 1–581 and the modern ones for 1–586, in both cases indexing `[selector-1]`; for the
error log it resolves the name with a string table of `APPLE80211_IOC_*` (the literal
`APPLE80211_IOC_BLOCK_ACK` is at `0xffffff800244301d`).

- `apple80211getBLOCK_ACK` (`0xffffff80022632f4`) and `apple80211setBLOCK_ACK`
  (`0xffffff8002268050`): both `return 0xE082280E` unconditionally. No code calls them
  directly and their pointer is in none of the handler tables of the scanned range
  (`0xffffff8002440000`–`0xffffff8002500000`, which includes both legacy ones and the additional table
  starting at `0xffffff800247ce90`): not even the selector can reach them. `?` whether a table
  outside that range might route them; it does not matter: they always return an error.
- The rest of the family's AMPDU surface is **telemetry**, not control:
  `IO80211Controller::setAMPDUstat` (`0xffffff80022954d4`),
  `IO80211InterfaceMonitor::setAMPDUstat` (`0xffffff80023758a4`),
  `IO80211InterfaceMonitor::getAMPDUChannelID` (`0xffffff8002375652`),
  `IO80211LQMCrashTracer::recordRxAmpduUpdate` (`0xffffff80022a3dd4`) and the event message
  `APPLE80211_M_RX_AMPDU_UPDATE` (string `0xffffff80024488d7`): statistics toward userspace.
- The family's only TX aggregation control is infra: `IO80211ApFeatureConfig::
  shouldDisableTxAMPDU` (`0xffffff80022ea884`) and the request `APPLE80211_IOC_WCL_LIMITED_AGGREGATION`
  (string `0xffffff800244675c`). There is no AWDL equivalent.

**Conclusion (1):** neither the family nor `IO80211AWDLPeerManager` nor `IO80211AWDLPeer` receives,
parses or answers ADDBA/DELBA/BAR for `awdl0`. `actionFrameProcessInput`
(`0xffffff800220e8b0`) and `IO80211AWDLPeer::doMinimalAFProcessing` (`0xffffff80021b3ea6`) — the full
path of an action frame arriving at `actionFrameInput` — contain no Block Ack category branch
(category 3) and no AMPDU reference: an ADDBA reaching the manager is discarded with no effect.
In the Broadcom design the Block Ack is the firmware's business; the host only declares which
peers can aggregate (§4).

## 3. Traffic registration and High Thruput: the trigger, not the BA session

Userspace-to-driver chain to mark real-time traffic with an AWDL peer:

```
wifip2pd → APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION (set)
  → glue setAWDL_PEER_TRAFFIC_REGISTRATION(IO80211Controller*, IO80211SkywalkInterface*, …)  0xffffff8002289465
      requires req+24 == 184 and non-null payload; metaCast to IO80211AWDLPeerManager
      → IO80211AWDLPeerManager::setAwdlPeerTrafficRegistration(reg)                          0xffffff800220946a
          → handleAirDropTrafficReg / handleEnsembleTrafficReg / handleMigrationTrafficReg … (0xffffff80022087ac, 0xffffff8002202960, 0xffffff8002206608)
  → per-interface variant: apple80211setAWDL_PEER_TRAFFIC_REGISTRATION                       0xffffff8002268d88
      validates size 164 and calls a virtual method of the interface with the payload
      (the concrete slot was not identified in this pass `?`)
      → AppleBCMWLANProximityInterface::setAWDL_PEER_TRAFFIC_REGISTRATION                    0xffffff800171fcaa
```

- The discrepancy 184 (glue, length of the request) versus 164 (family handler, size of the
  struct `apple80211_awdl_peer_traffic_registration`) and the virtual slot of the second path are
  not explained by the decompiled output `?`.
- Fields the implementations read: service name at +16 (length at +12;
  `"CARPLAY_UUID"`, `"wifid-assisted-discovery"`, `"sidecar"`, `"rtmode"`, `"airplay"`,
  `"ensemble"`, `…Camera`, `"RemoteScreen"`, `"airplay-connectivity-check"` in the Broadcom layer),
  peer MAC at +4 (managed by the manager) and active flag at +168 (u16 extra at +176).
- The Broadcom variant does not touch AMPDU: it turns on/lowers the low-latency traffic state
  (`setLowLatencyTrafficState`), hooks the LLW companion interface and cleans up with
  `clearTrafficRegistrationState` (`0xffffff800172a8a4`, which only clears the state bytes
  CarPlay/AirPlay/SideCar at +100/+102/+103 and +552).
- For the «High Thruput» registration type (`handleMigrationTrafficReg` `0xffffff8002206608`,
  call at `0xffffff80022066de`), the manager enters D2D mode:
  `initiateHighThruputMode(peer)` (`0xffffff80021fabcc`) → `setHighThruputState`
  (`0xffffff80021faa08`, sets `ivars+16176` and notifies `notifyRealTimeModeOnOff("D2DMigration")`)
  → `addPeerToFWCache(peer, true)` → `disassocInfra` + `setDataMode(34, 1)`. The peer exits via
  `stopHighThruputMode` (`0xffffff80021fa86a`) when it receives the UMI with the stop option.
- The UMIs with these options travel in the AWDL unicast action frames:
  `IO80211AWDLPeer::processUnicastAWSF` (`0xffffff80021ba828`, called from
  `parseAwdlDataPathTLVAndTakeAction` `0xffffff80021b92a0`) interprets the bit `0x20000` as
  `kUnicastMIOptionsHighThruputStart` (string `0xffffff80023cbe65`, xref `0xffffff80021bb71b`,
  branch that calls `startHighThruputMode`) and `0x40000` as `…HighThruputStop`
  (string `0xffffff80023cc6a6`, xref `0xffffff80021bc52d`); `initiateHighThruputMode` writes
  `0x20000` into the peer's ivars (+212) to answer. In the same function, with data
  options, the manager also calls `addPeerToFWCache` routinely (log «Waiting for
  caching of Peer…»).
- `setAwdlRTGTxMinRate(bool)` (`0xffffff80021d22de`) only sets the minimum TX rate of the RTG group;
  nothing about BA.

**Conclusion (2):** the only «per-peer» mechanism that opens or closes the A-MPDU is the pair
traffic registration → peer cache (§4). No per-TID request or event exists in the family; the
per-TID BA policy (`changeAggregationPerEDCATID`, §4) is internal to the Broadcom core with no
ioctl path. The relevant payload is that of `PEER_CACHE_CONTROL` (§4).

## 4. The real path: `PEER_CACHE_CONTROL` → iovar `awdl_peer_op` → firmware

```
IO80211P2PDataPathManager::addPeerToFWCache(peer, hiPrio)          0xffffff800229db9c
  LRU limits (_currentPeerCacheSize/_maxPeerCacheSize), cooldown of ~1 s
  (≤999 ms), locks by hot-spot concurrency and delete in progress
  → buffer of 464 B (IOMallocZeroData) → peer vtable+352 = fillInCacheParams(buf)
      IO80211AWDLPeer::fillInCacheParams                            0xffffff80021ac04c
        └ IO80211Peer::fillInCacheParams                            0xffffff8002240ade
  → sendIoctlToCachePeer(buf, peer)                                 0xffffff800229e458
      ioctl_internal with token 0x8F802869C8 (low dword = the SET ioctl 0x802869C8;
      high dword 0x8F = 143, the same size the family validates `?` whether size or type)
      → apple80211setPEER_CACHE_CONTROL                             0xffffff8002268abb
          validates size 143; metaCast to IO80211AWDLProtocol → vtable slot +0x1108
          (or IO80211SapProtocol → +0x1068; otherwise 0xE082280E)
          → AppleBCMWLANProximityInterface::setPEER_CACHE_CONTROL   0xffffff80017235ce
              builds the iovar buffer "awdl_peer_op" and sends it via
              AppleBCMWLANCommander::sendVirtualIOVarSet (asynchronous callback
              handleCacheControlAsyncCallBack); error → "Unable to send awdl_peer_op"
```

Structure `apple80211_peer_cache_control` (exactly as the family fills it and the Broadcom layer
reads it; the userspace request is validated at 143 B, the internal kernel buffer is
464 B — `bzero(a2+4, 0x1CC)` in `IO80211Peer::fillInCacheParams`):

| offset | field | note |
|---|---|---|
| +0 | u32 version = 1 | |
| +4 | u32 opcode | 0 = ADD, 2 = DELETE, 6 = UPDATE (the manager uses UPDATE if the peer is already cached: flags at `peer+600/+601`) |
| +8..+13 | peer MAC | from `peer+88..93` |
| +16..+43 | HT IE (ID 45, len 26) | copied from `peer+444..471`; presence = byte +16 == 45 (and +17 == 26) |
| +44 | u8 **ampdu** | the family writes it **to 1 unconditionally** in `fillInCacheParams` |
| +45 | u8 VHT present | if `peer+487 & 1`; VHT IE at +46..+59 (ID 191, len 12, 12 B body) |
| +60 | u8 HE | `IO80211AWDLPeer::fillInCacheParams` sets it if bit 0 of the peer ivars byte +1248 is 1 |
| +68..+72 | channel sequence header | count at +69 (from `peer+909`); the exact packet of the entries differs between the family view (12 B per entry from +76, from `peer+920`) and the Broadcom read (3 B per entry from +83) `?` |

The Broadcom layer (`setPEER_CACHE_CONTROL` `0xffffff80017235ce`) composes the iovar `awdl_peer_op`
(122 B + channel sequences) from the struct: opcode (ADD/DELETE/UPDATE; the method itself
logs «Opcode %u %s […] ampdu %u ht %u vht %u he %u», string `0xffffff80017cd12a`, ins
`0xffffff8001723fca`, and «adding HT IE, ampdu %u», string `0xffffff80017cd196`, ins
`0xffffff8001724134`; the asynchronous callback `handleCacheControlAsyncCallBack` repeats the «Opcode
%u …» without the HE field, string `0xffffff80017cc27d`, ins `0xffffff8001720870`), MAC, flag
**ampdu** in byte +8
of the buffer — `ampdu = (src[44] != 0) && (core ivar+14372 == 0)` —, HE flag in bit 2, the
HT/VHT/HE IEs copied and the peer's channel sequence as a vendor IE (tag `0xDD`, constant
`0x08F20800`, the same as the SNAP AWDL of the datapath). Names and iovar: strings `awdl_peer_op`
(`0xffffff80017cd203`, `0xffffff80017cd2dd`), «Unable to send awdl_peer_op»
(`0xffffff80017cd2ea`), «WLC_SET_VAR: awdl_peer_op» (`0xffffff80017da673`). The core confirms the
operation with `AppleBCMWLANCore::completePeerCacheControl` (`0xffffff800165b8b6`), which republishes
a 464 B 0x33 message with the mapped opcode (ADD/UPDATE→1, DELETE→3).

The family flag always goes out as 1 (+44), so **every cached AWDL peer is registered with the
firmware as A-MPDU capable**. The only AWDL AMPDU switches are debug ones:
`AppleBCMWLANCore::forceAwdlAmpdu`/`setForceAwdlAmpdu` (`0xffffff80016b09fc`/`0xffffff80016b0a10`,
ivar +14376) and `forceDisableAwdlAmpdu`/`setForceDisableAwdlAmpdu`
(`0xffffff80016b0a24`/`0xffffff80016b0a38`, ivar +14372, the one the peer op byte +8 consults),
fed by the debug commands `a-force-ampdu`/`a-disable-ampdu`
(`0xffffff80017c43df`/`0xffffff80017c43cf`; `AppleBCMWLANCoreDbg::cmdAWDLForceAMPDU`
`0xffffff80016f46a4`, `cmdAWDLDisableAMPDU` `0xffffff80016f45fa`).

The rest of the core's AMPDU knobs are firmware configuration, not host sessions:

| function | iovar | payload |
|---|---|---|
| `AppleBCMWLANCore::changeAggregationPerEDCATID(tid, on)` `0xffffff80016b049a` | `ampdu_tid` (2 B `{tid, on}`; also in `setupFirmware`, xrefs `0xffffff8001614e03`/`0xffffff8001614e6e`) and `ampdu_send_addba`/`ampdu_send_delba` (8 B with the BSSID from `IO80211BssManager::getCurrentBSSID`) | «Block ACK enabled/disabled for TID %d»; no direct callers found `?` (probably via vtable, infra path) |
| `AppleBCMWLANCore::cfgAmpduWindowSize()` `0xffffff8001627628` (caller `linkChangeEvent` `0xffffff8001631884`) | `ampdu_tx_ba_window_cfg` / `ampdu_rx_ba_window_cfg` (strings `0xffffff8001789f17`/`0xffffff8001789fb4`), 40 B | «Setting AMPDU BA window size» |
| `getAMPDURxDensityFromHW` / `getAMPDURxFactorFromHW` `0xffffff8001627b38`/`0xffffff8001627b50`, `updateHTAndVHTCapBasedOnHWSupport` `0xffffff800161f348` | `ampdu_rx_density` / `ampdu_rx_factor` (xrefs `0xffffff800161f488`/`0xffffff800161f4a1`) | capabilities that go into the project's own HT IEs |
| `disableRxAMSDUInAMPDU` `0xffffff80016a9c0e` | `rx_amsdu_in_ampdu` | |
| platform property | `wlan.vo.blockack` (string `0xffffff80017ae72b`) | config |

That the firmware sustains the sessions is shown by its own counters that the host only
publishes: «RX AMPDU … rxaddbareq=%u txaddbaresp=%u rxbar=%u txdelba=%u rxdelba=%u» (string
`0xffffff8001777529`, `AppleBCMWLANLQM::updateLinkQualityMetrics` `0xffffff80015ce95d`) and
`printPeerRxAmpduStats`/`printPeerTxAmpduStats` (`0xffffff80015d6680`/`0xffffff80015d674e`):
the firmware receives the ADDBA reqs, emits the responses, processes BAR and does DELBA.

## 5. RX reordering for `awdl0`

- There is no reordering in the family: the global search for `reorder` only yields TCP/`netem`,
  WCL candidate lists and LQM telemetry; the Skywalk datapath delivers to the `RxCompletionQueue`
  as-is (`rxCompRingDrain` → `enqueuePackets`, [AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) §10).
- On Broadcom the BA window lives in the firmware: the counters the firmware reports include
  out-of-window/out-of-sequence/duplicate/stuck («AMPDU Rx Out-of-Window errors»
  `0xffffff800244e8c9`, «AMPDU Rx Out-of-Sequence errors» `0xffffff800244e8e7`,
  «AMPDU Rx Duplicate errors» `0xffffff800244e907`, «AMPDU Rx Stuck errors» `0xffffff800244e921`,
  and the `rxoow/rxoos/rxdup/rxstuck` of `0xffffff8001777529`); the host receives them already in order.
- Therefore, for `awdl0` the order is guaranteed by whoever receives from the air. The RTL layer must deliver
  the packets to the `RxCompletionQueue` in sequence order: own reordering buffer per
  (peer, TID) of the negotiated window size (64 in the captures), with expiration, or immediate
  in-order delivery if the hardware reorders. The driver tree ships a reusable reorderer:
  `struct recv_reorder_ctrl` (`Source/rtl8814au/include/rtw_recv.h:83`) and
  `recv_indicatepkts_in_order` (`Source/rtl8814au/core/rtw_recv.c:3266`).

**Conclusion (3):** the family does not reorder; whoever delivers to the `RxCompletionQueue` must do it
in order. Without firmware, that is the RTL layer.

## 6. TX aggregation

- The manager's TX pull (`IO80211VirtualInterface::dequeueTxPackets` →
  `IO80211AWDLPeerManager::dequeueTxPacket` `0xffffff800221675e`,
  [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §4) fills a 96 B `TxPacketRequest`
  (flags, counters, 8 mbuf lists): **there are no A-MPDU, density or window fields**;
  the manager does not aggregate nor limit aggregation.
- The per-peer maximum A-MPDU that exists in the Broadcom system travels inside the peer cache HT IE
  (AMPDU parameters byte in the body, log «HT: Info(%02x) AMPDU (%02x)…» in
  `fillInCacheParams` `0xffffff8002240ade`) and the firmware consumes it; the host applies no limits.
- In AWDL there is no `WCL_LIMITED_AGGREGATION` nor `shouldDisableTxAMPDU` (§2: infra only).

**Conclusion (4):** no limitation comes from the manager; the decision to aggregate in TX is the driver's.
Until there is a BA agreement with the peer, the RTL layer must not send A-MPDU.

## 7. What the RTL8814 layer must implement

| piece | decision | why |
|---|---|---|
| Answer ADDBA req (category 3, action 0) | **Yes, the layer itself**. Intercept the frame in the RX diversion ([AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §4, before any other part consumes it) and emit ADDBA response accepting the request parameters (TID 0, window 64) | The family never answers (§2); on Broadcom the firmware does it (§4); winawdl measured 971 kB/s → 1.7 MB/s by accepting it. The peer retransmits each frame up to its retry limit without an agreement |
| Incoming ADDBA response and DELBA (actions 1 and 2) | Process in the layer: confirm/close the session per (peer, TID) | Same session contract the firmware sustains today (§4) |
| BAR | Process: window advance and emission of the Block Ack (control 0x94); if the 8814AU MAC can do the BA in hardware, leave it to the hardware `?` (RX_ACK §5 already points to the equivalent CAM `?`) | Holder of the agreement: the Broadcom firmware is who processes `rxbar`/`txback` (§4) |
| Reordering | Buffer per (peer, TID), window 64, expiration, in-order delivery to `enqueuePackets` of the `RxCompletionQueue` | The family does not reorder (§5); without order, TCP suffers and the peer overruns the window |
| `setPEER_CACHE_CONTROL` | Today it returns 0 and confirms with event 51 (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.6; slot +0x1108): keep that and optionally use opcode ADD/DELETE/UPDATE + flag ampdu (+44) as a per-peer BA session switch, emulated from the userspace signal | It is exactly the Broadcom semantics: register the peer as A-MPDU-capable with «the firmware» (here, the layer itself) |
| `AWDL_PEER_TRAFFIC_REGISTRATION` | No functional change: it does not touch BA (§3) | Its High Thruput effect converges into the peer cache, not into BA sessions |
| TX | No A-MPDU until an agreement exists; if aggregating, respect the HT capabilities the layer announces (20 MHz: the limits of [AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) still apply) | No limit comes from the manager (§6); announcing A-MPDU without supporting it degrades just like in winawdl with 40 MHz |

References from the driver tree for the pieces above: `RTW_WLAN_CATEGORY_BACK = 3`
(`Source/rtl8814au/include/ieee80211.h:1511`), category table with `OnAction_back`
(`Source/rtl8814au/core/rtw_mlme_ext.c:76`), `OnAction_back` with
`RTW_WLAN_ACTION_ADDBA_REQ`/`_ADDBA_RESP`/`_DELBA` and `process_addba_req`
(`Source/rtl8814au/core/rtw_mlme_ext.c:3677` and following), and the reorderer
`recv_reorder_ctrl`/`recv_indicatepkts_in_order` (§5).

## 8. Not verified in this pass

- Selector numbers of `BLOCK_ACK`/`PEER_CACHE_CONTROL`/`AWDL_PEER_TRAFFIC_REGISTRATION`/
  `TRAFFIC_REGISTRATION`: the handlers are in the table starting at `0xffffff800247ce90`
  (get `PEER_CACHE_MAXIMUM_SIZE` at +131; sets of `PEER_CACHE_CONTROL`/`AWDL_PEER_TRAFFIC_REG`/
  `TRAFFIC_REG`/`WCL_LIMITED_AGG` at +730/+751/+927/+1080). If the modern get/set tables of the
  dispatcher were contiguous with 586 entries each, the sets would fall at 145/166/342/495:
  arithmetic hypothesis unconfirmed, no base closes simultaneously with the dispatcher range
  (1–581/586) nor with the name table `?`.
- Exact correspondence between the entry packet of the peer cache channel sequence
  (12 B/entry in the family view) and the 3 B/entry read of the Broadcom layer (§4) `?`.
- Whether the RTL8814AU MAC can answer ADDBA and BAR without host intervention (§7) `?`.
- Whether the Broadcom firmware answers ADDBA for AWDL peers without a traffic registration record,
  or only after the peer op ADD `?` (the cache ampdu flag always goes out as 1, which suggests the
  former after the routine caching of `processUnicastAWSF`).
- The 184/164 discrepancy of the request `AWDL_PEER_TRAFFIC_REGISTRATION` (§3) `?`.
