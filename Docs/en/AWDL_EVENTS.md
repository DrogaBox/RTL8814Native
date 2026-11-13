# Kernel events toward userspace on the AWDL side (awdl0) and their consumption in wifip2pd

Complete inventory of what `AppleBCMWLANProximityInterface`, `IO80211AWDLProtocol`,
`IO80211VirtualInterface`, `IO80211SkywalkInterface` and the rest of `IO80211Family` send
toward userspace over the Apple80211 channel family (`postMessage`, apple80211 event
ids, the P2P channel, AWDL frames and BPF) once `awdl0` exists, and what
`wifip2pd` consumes from all of it. Closes the open question in `AWDL_WIFIP2PD_P2P_EVENTS.md` §3
(where the event id switch is). Base: `BootKernelExtensions.kc` (md5
`f489ff8be80e6013a1bf47561dcaef45`) and `wifip2pd.x86_64` (md5
`a120cfccfa2783303604ce5be9e1763a`), analysis with IDA 9.4 headless (idat -A -S) over
copies of both IDBs in `Build/ida_awdl/task_awdl_events/kernel/` and `/p2pd/` (scripts
`k_probe*.py`/`w_probe*.py`, outputs `k_out*.txt`/`w_out*.txt`). Anything unverified goes
as `?`. Addresses are VAs of the indicated binary (kernel unless noted).

**Summary in four lines:**

1. The main channel is the IO80211Family *event pipe*: each event is a pair
   (apple80211 id, payload ≤ 0x1000 B) that `IO80211SkywalkInterface::postMessageSync`
   writes into a ring shared with the client (`IO80211IOUCEventPipe::writePipe`)
   with a fixed 48 B header and notifies with `sendAsyncResult64`. The canonical
   names of the ids are in the kernel table `gAppleMIndexToString`.
2. AWDL frames (action frames) travel over TWO paths: as event
   `APPLE80211_M_AWDL_ACTION_FRAME_RX` (id 48) with payload
   `{apple80211_act_frm_hdr[72], frame[]}` over the awdl0 interface
   (`IO80211AWDLPeerManager::postAFToP2pd`), and as BPF taps
   (`AppleBCMWLANProximityInterface::handleActionFrame_rx` → `bpfTapInput`, DLT 0xC).
3. `wifip2pd` registers monitoring with `Apple80211RawP2PEventMonitoringInit` (awdl0,
   role 1) or `Apple80211RawEventMonitoringInit` + 16× `Apple80211StartMonitoringEvent`
   (role 0), and consumes the events in a single master switch (`sub_1000388D0`,
   0x8e9e B) with 58 cases over raw ids; the switch builds a `DriverEvent` enum
   and delivers the event to the `eventHandler` of the `InterfaceHandle`.
4. To consider the AWDL interface healthy, wifip2pd needs no event at
   creation time (the 30 s semaphore is released by IORegistry, closed in
   `AWDL_WIFIP2PD_POST_CREATE.md`); what it does need is the monitoring registration
   without error, and in operation it consumes ids 46/47/48/65/142/195/234/235/243 among
   others. The ids the Broadcom kext emits over the AWDL interface and that wifip2pd
   ignores (41/42/43, 64, 69, 88, 94, 135, 170, 183, 204, …) are optional.

## 1) Kernel delivery channels

### 1.1 General Apple80211 channel (event pipe per interface)

Delivery chain, verified by decompiling each link:

| Step | Function | Address |
|---|---|---|
| Broadcom emission (controller) | `IO80211Controller::postMessage(iface, id, data, len, bool)` | `0xffffff8002299fae` |
| Controller delegation | `CommonGlue::sendMessage(CommonGlueMessage&)` (slot 288 of the vtable of the object in the controller ivar) | `0xffffff8002186c3c` |
| CommonGlue reception | `IO80211Glue::receiveMessageInternal` | `0xffffff8002194204` |
| IO80211Family emission (interface) | `IO80211SkywalkInterface::postMessage` (10 B) → `postMessageInternal` | `0xffffff80022f85f6` → `0xffffff80022f8600` |
| WCL async path | `IO80211Glue::routeEventToWcl` → `addEventToPendingQueue` | `0xffffff80021941c0` → `0xffffff8002194094` |
| Queue draining | `IO80211Glue::processPendingEventQueue` → `sendEventAndFilter` | `0xffffff8002193ff4` → `0xffffff8002193d5c` |
| Synchronous delivery | `IO80211SkywalkInterface::postMessageSync` | `0xffffff80022f8abc` |
| Fan-out to clients | `IO80211SkywalkInterface::postMessageIOUCE` (by BSDName, iterates the pipes) | `0xffffff80022f66a2` |
| Ring write | `IO80211IOUCEventPipe::writePipe` | `0xffffff80022a7e7c` |
| Entry reservation | `IO80211IOUCEventPipe::reserveRingEntry` | `0xffffff80022a776e` |
| Client notification | `IO80211IOUCEventPipe::notifyUser` → `IOUserClient::sendAsyncResult64` | `0xffffff80022a76ee` |

- `postMessageInternal` (`0xffffff80022f8600`): if the `sendToWcl` flag is active and there
  is a Glue, delivers via `routeEventToWcl` (queue); if not, directly via
  `postMessageSync`. `IO80211Controller::postMessage` delegates to the object in the controller
  ivar and lands on the same `postMessageSync` via `sendEventAndFilter`
  (`0xffffff8002193e59`), which delivers on the primary interface.
- **Queue entry** (`postMessageQueueEntry`, built in `addEventToPendingQueue`
  `0xffffff8002194094`, `IOMallocZeroData(len+32)`, panic `"Failed to alloc
  postMessageQueueEntry\n" @IO80211Glue.cpp:336`): list links `+0`/`+8`, **u32 id
  `@+16`**, **u32 len `@+20`**, u64 timestamp µs `@+24`, **payload copied `@+32`**
  (`memmove(entry+32, data, len)`). `sendEventAndFilter` reads exactly those fields
  (`+16`, `+20`, `+24`, `+32` at `0xffffff8002193d5c`), calls the filter (slot 288 of the
  CommonGlue vtable) and if it does not filter, `postMessageSync`.
- **Payload in the ring** (`reserveRingEntry` `0xffffff80022a776e`): 48 B header
  `{u32 marker 82 ('R') `@+0` (written after `_mm_mfence`), u16 `@+4` (=0), u16
  serial number `@+6`, u64 epoch secs `@+8`, u64 epoch µsecs `@+16`, u16 payload length
  `@+24`, u16 padding `@+26`, u32 event id `@+28`, char ifname[16] `@+32`
  (`strlcpy(..., 0x10)`)}; the payload is copied behind (`@+48` + aligned padding). In
  the wrap fill the marker 80 ('P') is written. `writePipe` rejects payloads
  `> 0x1000` and entries without space (returns -536870210, "Eventpipe is full, drop
  message[%d][%s], dataLen %lu" in `postMessageSync` `0xffffff80022f8abc`, with the
  id name taken from `gAppleMIndexToString`).
- **Subscription filter**: `IO80211APIUserClient::extSubscribeEvent` (selector 2 of
  `sMethods`, table at `0xffffff8002478ed0`, function `0xffffff8002352486`) copies a
  64 B filter from the argument to `client+304..+367` and sets the "subscribed" flag
  `@client+368`. `writePipe` checks the flag `@+368`, then the per-id bit in the
  bitmap `@client+417+((id-1)>>3)` bit `(id-1)&7`, unless the bypass flag
  `@client+416` is active (in which case everything passes). The exact setter of the per-id bit
  (what ends up calling `Apple80211StartMonitoringEvent`) and of the bypass flag was not
  located (`?`); the read accessor is `IO80211APIUserClient::isSubscribedToMsg`
  (`0xffffff80023556ba`). `extEventMonitorInit` (selector 3, `0xffffff800235256e`) creates
  the pipe (`IO80211SkywalkInterface::createEventPipe` `0xffffff80022f6bf2` →
  `IO80211IOUCEventPipe::withClient` `0xffffff80022a71d8`); `extEventMonitorExit`
  (selector 4, `0xffffff80023525aa`) destroys it.
- Parallel kev channel (interface notifications to agents): global function
  `IO80211InterfacePostMessage(ifname, id, data, len, kev_msg*)` (`0xffffff800229b746`)
  builds `{0x600000001, 1, id, 16, ifname, len|ptr, ctrl}` and calls
  `IO80211QueueCall::kev_post_msg` (`0xffffff800236a11a`); it only notifies if
  `len != 0` and (`len < 0xFF1` or `data == NULL`) — `"IO80211InterfacePostMessage: bad
  message data or message too long\n"` — and never the ids 12 and 58.

### 1.2 P2P channel: forwarding of infra events to the P2P managers

`IO80211InfraInterface::postMessage` (`0xffffff80023614b8`, 0x1375 B) handles
each event internally (giant switch over the id: SSID 2, BSSID 3, link 4, RSSI 39,
CCA 60, IP 66, channel 82 with len 12, chan outage 125 with len 803, LQM 145–194, SIB 192,
LQMData 224–233, …) and at the end always calls
`IO80211InfraInterface::routeToP2PInterface(id, data, len)` (`0xffffff800236364e`).
That router **filters by whitelist** and forwards to two consumers:

- The NAN manager (meta-cast to `IO80211AWDLPeerManager`/NAN and call by vtable,
  slot `?` at `0xffffff800236364e`); the pattern matches
  `IO80211NANPeerManager::postMessage` (`0xffffff80022adfce`), which does
  `postMessageSync` on the NAN interface (manager ivar `+1376`).
- `IO80211AWDLPeerManager::reportDataPathEvents(id, data, len)`
  (`0xffffff80021dd8f4`, 0x322a B, resolved by the manager vtable `__ZTV22…` at
  `0xffffff800245a858`, vptr+664) — the AWDL consumer of the forwarded events.

**P2P router whitelist** (comparisons at `0xffffff800236364e`): ids **2, 3, 4,
9, 66, 80, 125, 126, 127, 139, 160, 198, 242**. Nothing else reaches the P2P managers by
this path.

### 1.3 AWDL frames toward wifip2pd

- **Event channel**: `IO80211AWDLPeerManager::postAFToP2pd(IO80211Buffer*,
  apple80211_act_frm_hdr*)` (`0xffffff800220fc2c`): allocates `frameLen+72` B, copies the
  `apple80211_act_frm_hdr` header (72 B) `@+0` and the raw frame `@+72`
  (`buffer->getLength()` vtable+304, `buffer->getBytes()` vtable+344), and emits it with
  the virtual `postMessage` call (vptr+0xB18 slot) with **id 48** and WCL flag=0 over
  the manager interface (ivar `+1376` = awdl0). Increments the counter
  `ivars->fAWDLActionFrames` (reporter key `0x4966416650325064` = "IfAfP2Pd"). The
  allocation failure is logged with `"-------- %s[%d] ------- Could not allocate AR
  BUFFER for AF message"` (IOLog on the error path of `0xffffff800220fc2c`,
  exact site `?`).
- **BPF channel**: `AppleBCMWLANProximityInterface::handleEvent` (fw id 75/98.1) →
  `handleActionFrame_rx` (`0xffffff800171795c`): rebuilds the frame into a management pool
  buffer (`getMgmtFramePool`), prepends a 72 B (0x48) header of type
  `wl_event_rx_frame_data` — int `@+0` from cursor+24, u16 `@+4` from cursor+28,
  MAC/chan fields with byteswap from cursor+52/56/60 (v1 variant, when the u16 of the
  header != 512) or cursor+56/64/60 + 4 B from cursor+68 (v2 variant, u16 == 512), and
  channel converted with `AppleBCMWLANChanSpec::getAppleChannelSpec` +
  `ChanSpecConvToApple80211Channel` — and calls
  `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*, uint, void*, ulong)`
  (`0xffffff8002331070`) with DLT 0xC, site `0xffffff8001717f9d`;
  `handleProbeResponse_rx` also taps (site `0xffffff80017183d6`). The generic path is
  `IO80211InfraInterface::bpfTapInternal` (`0xffffff8002367126`).

## 2) Canonical names of the ids: `gAppleMIndexToString`

The table `__ZL20gAppleMIndexToString` (`0xffffff8002483e80`) of the IO80211Family kext
maps id → string; entries 1–246 are the `APPLE80211_M_*` names (the
`postMessageSync` function uses it for logging, `0xffffff80022f8abc`); entries 247–259 are
no longer events but NAN/AWDL interface role strings ("Discovery", "Data + InfraData",
…). Names of the ids relevant to AWDL, read from the table:

| id | name | id | name |
|---:|---|---:|---|
| 1 | `APPLE80211_M_POWER_CHANGED` | 48 | `APPLE80211_M_AWDL_ACTION_FRAME_RX` |
| 2 | `APPLE80211_M_SSID_CHANGED` | 49 | `APPLE80211_M_RESET_INTERFACE` |
| 3 | `APPLE80211_M_BSSID_CHANGED` | 55 | `APPLE80211_M_DRIVER_AVAILABLE` |
| 4 | `APPLE80211_M_LINK_CHANGED` | 59 | `APPLE80211_M_MAC_ADDRESS_CHANGED` |
| 9 | `APPLE80211_M_ASSOC_DONE` | 65 | `APPLE80211_M_AWDL_STATISTICS` |
| 10 | `APPLE80211_M_SCAN_DONE` | 66 | `APPLE80211_M_ASSOC_READY` |
| 11 | `APPLE80211_M_COUNTRY_CODE_CHANGED` | 67/68 | `…_AWDL_REALTIME_MODE_START/END` |
| 22 | `APPLE80211_M_P2P_PEER_DETECTED` | 74 | `APPLE80211_M_AUTH` |
| 41 | `…_AWDL_AVAILABILITY_WINDOW_START` | 78 | `APPLE80211_M_ASSOC` |
| 42 | `…_AWDL_AVAILABILITY_WINDOW_EXTENSION_START` | 82 | `APPLE80211_M_SET_INFRA_CHANNEL` |
| 43 | `…_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_END` | 95–129 | `APPLE80211_M_NAN_*` |
| 46 | `…_AWDL_SYNC_STATE_CHANGED` | 125/126 | `…_INFRA_SCAN_START/COMPLETE` |
| 47 | `APPLE80211_M_PEER_PRESENCE` | 127 | `APPLE80211_M_INFRA_ASSOC_START` |
| 64 | `…_AWDL_TXCALIBRATION_REQUEST` | 160 | `APPLE80211_M_SDB_MODE_CHANGED` |
| 69 | `…_AWDL_UNICAST_AF_STATUS` | 215 | `…_INFRA_PEER_REQUIRES_IP_RES` |
| 77 | `…_AWDL_ADVERTISERS_LIST` | 234/235 | `…_AWDL_IDLE_DETECT/SESSION` |
| 88/94 | `…_AWDL_DFS_CSA/_COMPLETE` | 195 | `…_AWDL_CHAN_SEQ_CHANGED` |
| 135 | `…_AWDL_SDB_TRANSITION` | 200 | `…_AWDL_STATS_P2P_FAILURE` |
| 142 | `…_AWDL_HPP_STATISTICS` | 243 | `…_AWDL_RTG_STATISTICS` |
| 149 | `…_P2P_AIRPLAY_STATISTICS` | 246 | `…_NAN_RADIO_SCHED_CHANGED` |

The complete list 1–246 is dumped in `Build/ida_awdl/task_awdl_events/kernel/k_out3.txt`.

## 3) Kernel emitters (matrix id → emitter → payload)

The call sites were extracted by xrefs (probe 1) and by sweeping virtual calls
`call [vptr+0xB18]` with the id immediate in the argument register
(probe 5); the ids are all confirmed by the emitter pseudocode unless noted. Payload = what the
emitter builds; "over" indicates the interface whose event pipe receives it.

### 3.1 `AppleBCMWLANProximityInterface` (Broadcom kext; fw events → `IO80211Controller::postMessage`)

`handleEvent(IO80211BufferCursor const&)` (`0xffffff8001715ece`, 0x1a09 B) is the
event dispatcher of firmware for role 6: reads the fw id at cursor`+4` and the payload
at cursor`+48` (len cursor`+20`). Emissions toward userspace (all via
`IO80211Controller::postMessage`, cited call site):

| id | emitter (function `@` site) | payload |
|---:|---|---|
| 41 `AWDL_AVAILABILITY_WINDOW_START` | `handleEvent` fw96 → `handleAvailabilityWindowStart` `0xffffff80017188d1` | ~24 B on the stack from the 28 B fw event (u8 fw+12, u32 fw+0, u16 fw+8/10, u32 fw+22, converted channel); exact field-by-field mapping `?` |
| 42 `…_EXTENSION_START` | `handleAvailabilityWindowExtension` `0xffffff80017189ac` | 8 B: u8 fw+12 `@0`, u32 fw+0 `@4` |
| 43 `…_EXTENSIONS_END` | `handleAvailabilityWindowEnd` `0xffffff8001718649` | ≥16 B: u8 `@0`, u32 `@4`, u8 `@8`, u32 `@12`; with the core keep-alive flag adds u16 `@14`, u16 `@4`, qwords `@16/24` (keep-alive) |
| 46 `AWDL_SYNC_STATE_CHANGED` | `handleMasterEvent` `0xffffff8001718a7e` / `handleSlaveEvent` `0xffffff8001718b6c` / `handleNonElectionMasterEvent` `0xffffff8001718c6e` / `bringdownLink` `0xffffff8001719d66` | 16 B: u32 state `@0` (1=master, 2=slave, 3=non-election master, 0=down), rest to 0/0xAA |
| 64 `AWDL_TXCALIBRATION_REQUEST` | `handleEvent` fw98.2/3, shared site `0xffffff8001715fd4` (id in register; pseudocode `v11 = 64`; the visible immediate at that site corresponds to the fw80/94 path) | 2 B: u16 read from the fw |
| 69 `AWDL_UNICAST_AF_STATUS` | `handleOobAfIovarAsyncCallBack` `0xffffff800171d4da` (direct); `handleEvent` fw98.9 via the shared tail of `LABEL_145` (id in register, `?` individual site) | 8 B: u32 `@0`, u16 `@4`, u8 `@6` (0xDA) |
| 88 `AWDL_DFS_CSA` | `handleEvent` fw98.0x12 (id in register by pseudocode `v11 = 88`, `?` individual site) | passthrough of the fw payload (≥4 B) |
| 94 `AWDL_DFS_CSA_COMPLETE` | `handleEvent` fw80, site `0xffffff8001715fd4` (shared with the 64 path) | NULL |
| 125 `INFRA_SCAN_START` | `handleEvent` fw98.0, site `0xffffff80017173e9` | **0x322 B (802)** buffer of zeros with the last byte at 1 (`bzero(&v95, 0x322)` + `v98 = 1`) |
| 126 `INFRA_SCAN_COMPLETE` | `handleEvent` fw98.0, site `0xffffff80017172d5` | 2 B: u16 `0x0100` |
| 135 `AWDL_SDB_TRANSITION` | `handleEvent` fw152, site `0xffffff8001716c23` | 8 B: u32 map from fw+8 `@0`, u32 map from fw+12 `@4` |
| 170 `RETRY_CHAN_SEQ` | `handleProxSlotBSSCompletion` `0xffffff8001720a14` | NULL (with status -16/-25 of the offload) |
| 183 `DFSP_SUSPEND_RESUME` | `handleEvent` fw98.0x13/0x14 via `LABEL_145` (id in register `v36 = 183`, `?` individual site) | 4 B: u32 `0x10000` (STA disappears) / `0x1000000` |
| 204 `DWELL_START` | `handleEvent` fw98.0x15 via `LABEL_145` (id in register `v36 = 204`, `?` individual site) | 4 B: u32 `0x10000` |
| 208 `WCL_REASSOC_LOW_LATENCY_TRAFFIC_CHANGED` | `setLowLatencyTrafficState` `0xffffff800172031d` | 2 B: u8, u8 — over the **infra interface** (`getInterface` ivar 11328), not over awdl0 |
| 84 `RANGING_AUTHENTICATION_DONE` | `handleEvent` fw150 (id in register, `?` individual site) | 4 B: u32 fw+8 — over the infra interface |

In `handleEvent` the only call/jump sites to
`IO80211Controller::postMessage` with a verifiable immediate are
`0xffffff8001715fd4` (94), `0xffffff8001716c23` (135), `0xffffff80017172d5` (126) and
`0xffffff80017173e9` (125) (probe 8, `k_out8b.txt`); the other cases converge in
those same sites with the id loaded in a register (pseudocode of `k_out2b.txt`).

### 3.2 `IO80211AWDLPeerManager` / `IO80211PeerManager` (IO80211Family kext)

| id | emitter (function `@` site) | payload | over |
|---:|---|---|---|
| 47 `PEER_PRESENCE` | `IO80211PeerManager::removePeer` `0xffffff8002254248`; `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` `0xffffff8002214d6d` | **0x44 B (68)**: MAC 6 B `@0` (peer+88/92), u32 `@8` = 1 (absence/presence mark, exact semantics `?`), rest 0 | the manager interface (awdl0) |
| 48 `AWDL_ACTION_FRAME_RX` | `postAFToP2pd` `0xffffff800220fce2` | `frameLen+72`: `apple80211_act_frm_hdr` 72 B `@0`, frame `@72` | awdl0 |
| 65 `AWDL_STATISTICS` | `awdlStatsPostMetrics` `0xffffff80021fbe63`; helper `postAwdlStatistics` `0xffffff8002332024` | NULL, len 0 (only if role == 6) | awdl0 |
| 86 `AWDL_NEARBY_LOG_TRIGGER` | `nearbyLogTriggerReceived` `0xffffff8002220a52` | `?` | awdl0 |
| 142 `AWDL_HPP_STATISTICS` | `sidecarStatsPostMetrics` `0xffffff8002225306`; helper `postAwdlHppStatsEvent` `0xffffff80023345ba` | `apple80211_awdl_low_latency_statistics_reduced`, **0x658 B (1624)** | awdl0 |
| 149 `P2P_AIRPLAY_STATISTICS` | `airplayStatsPostMetrics` `0xffffff80022045d1`; helper `0xffffff800233460c` | `apple80211_p2p_airplay_statistics`, **0xD0 B (208)** | awdl0 |
| 157 `LOWLATENCY_STATISTICS` | `IO80211NANPeerManager::lowlatencyStatsPostMetrics` `0xffffff80022acf9f`; helper `postLowlatencyStatistics` `0xffffff80023345e0` | `apple80211_nan_low_latency_peer_statistics`, **0xF20 B (3872)** (roles 8/9 only) | NAN interface |
| 172 `SOFTAP_CH_CHANGED_EVENT` | `validateForHostapAWDLConcurrency` `0xffffff80021d8b40`; helper `postHostapChannelChanged` `0xffffff8002334658` | `apple80211_hostap_state`, 0x10 B | awdl0 |
| 182 `P2P_THREAD_COEX_EVENT` | `notifyThreadCoexEvent` `0xffffff80022013eb`; `P2PSupervisor::setAWDLToAdoptNANChannelSequence` `0xffffff8002391044`; helper `postP2pThreadCoexEvent` `0xffffff800233467e` | `apple80211_p2p_thread_coex_event`, 2 B | awdl0 |
| 195 `AWDL_CHAN_SEQ_CHANGED` | `setChannelSequenceUnconditional` `0xffffff80021cc29b` (via virtual +0xB18) | `?` (the len is not a visible immediate) | awdl0 |
| 200 `AWDL_STATS_P2P_FAILURE` | `postP2pFailureStatistics` `0xffffff80021f3700` | `?` | awdl0 |
| 234 `AWDL_IDLE_DETECT` | `AWDLStateDump` `0xffffff800222b4b5` | `?` | awdl0 |
| 235 `AWDL_IDLE_SESSION` | `disableAWDL` `0xffffff80021c6c9c` | `?` | awdl0 |
| 243 `AWDL_RTG_STATISTICS` | `rtgStatsPostMetrics` `0xffffff800222a550`; helper `postAWDLRtgStatistics` `0xffffff8002334632` | `apple80211_awdl_rtg_statistics`, **0x4C B (76)** | awdl0 |
| 210 `P2P_PEER_COUNTRY_CODE` | `disableAWDL` `0xffffff80021c6f32`; `PeerManager::monitorCountryCode` `0xffffff800225b1f9` | `?` | infra |
| 140 `BSS_STEERING_REQUEST_EVENT` | `forceRoamToPeersAP` `0xffffff80021f96c6`; `disassocInfra` `0xffffff80021fab51`; steering managers `0xffffff80022c57da`/`0xffffff80022f1bf1`/`0xffffff80022f2342` | `?` | infra |
| 215 `INFRA_PEER_REQUIRES_IP_RES` | `IO80211InfraPeer::printStats` `0xffffff800223e6f7` | `?` | infra |
| 68 `AWDL_REALTIME_MODE_END` | `clearAirPlayingState` `0xffffff80021f31e9` | `?` | awdl0 |
| 67 `AWDL_REALTIME_MODE_START` | `notifyRealTimeModeOnOff` `0xffffff80021f1d58` (via virtual, immediate not visible) `?` | `?` | awdl0 |
| 246 `NAN_RADIO_SCHED_CHANGED` | `IO80211NANRadioResourceManager::setDeviceChannelAvailability` `0xffffff80022fedfe` | `?` | NAN |
| 197 `NAN_PREFERRED_CHANNELS_CHANGED` | `IO80211NANDataPathManager` `0xffffff800230779f`/`0xffffff800230e67a`; `P2PSupervisor::notifyPreferredChannelChange` `0xffffff800239210a` | `?` | NAN |
| 159 `NAN_OOB_AF_RX` | `IO80211NANPeer::handleOobActionFrameRx` `0xffffff80022b8dd2` / `handlePasnActionFrameRx` `0xffffff80022b8f9f` | `?` | NAN |

### 3.3 Generic helpers of `IO80211VirtualInterface` (emit only in role 6/9, checking `*(ivar36+88)`)

The `post*` helpers (probe 5) set the id and length; the caller provides the buffer:

| helper | address | id | len |
|---|---|---:|---:|
| `postSyncStateChanged` | `0xffffff8002332054` | 46 | 0 |
| `postAwdlStatistics` | `0xffffff8002332024` | 65 | 0 |
| `postAwdlHppStatsEvent` | `0xffffff80023345ba` | 142 | 0x658 |
| `postLowlatencyStatistics` | `0xffffff80023345e0` | 157 | 0xF20 |
| `postP2PAirplayStatistics` | `0xffffff800233460c` | 149 | 0xD0 |
| `postAWDLRtgStatistics` | `0xffffff8002334632` | 243 | 0x4C |
| `postHostapChannelChanged` | `0xffffff8002334658` | 172 | 0x10 |
| `postP2pThreadCoexEvent` | `0xffffff800233467e` | 182 | 2 |
| `postHostAPBandUpdate` | `0xffffff80023346a4` | 225 | 4 |

`IO80211VirtualInterface::postMessage` (`0xffffff8002333192`, virtual +0xB18) additionally
intercepts in kernel the ids 4 (hostap state), 84 (ranging auth, len 20) and 85 (ranging
result, len 448) before forwarding through the vtable+2848 slot.

## 4) Consumption in wifip2pd

### 4.1 Monitoring registration (closes `AWDL_WIFIP2PD_P2P_EVENTS.md` §3)

`sub_100041B60` (`0x100041b60`) — refined relative to the previous doc: **both paths are
Raw**; the distinction is the API and the flag:

- `role == 0 && NSObject queue` → `sub_100041EB0` (`0x100041eb0`) →
  `sub_100041F40(…, &unk_100515D20, Apple80211RawEventMonitoringInit)`, and **also**
  (flag 1 only) a loop of 16 ids from `0x100515CA0` with
  `Apple80211StartMonitoringEvent` (ids: 1, 2, 3, 9, 11, 55, 59, 66, 74, 78, 82, 125,
  126, 127, 160, 215; loop `0x100041e42–0x100041e76`; aborts at the first error).
- Any other case (role 1 = awdl0) → `sub_100041F20` (`0x100041f20`) →
  `sub_100041F40(…, &unk_100515D48, Apple80211RawP2PEventMonitoringInit)` — without an
  id loop.
- `sub_100041F40` (`0x100041f40`) builds the ObjC block
  `{isa=_NSConcreteStackBlock, flags=0x42000000, invoke=sub_100041ED0, desc, ctx}`
  with the Swift closure at block`+32` and its context at block`+40`, and calls
  `Raw*EventMonitoringInit(reference, queue, _Block_copy(block))`.
- The registered C callback is `sub_1000685A0` (`0x1000685a0`, with `self` = Swift ctx of
  0x30 B metadata `stru_100515C68`, fields `[2]`=closure fn, `[3]`=closure ctx,
  `[4]`=eventBuffer, `[5]`=eventBuffer+0x100): forwards to `sub_100041FE0`.
- `sub_100041FE0` (`0x100041fe0`, dispatcher): if id == 0 or data == NULL delivers empty
  `Data`; if `eventBuffer != NULL` and the payload fits in 0x100 B, it does
  `memcpy(eventBuffer, data, len)` and builds the `Data` **over the preallocated
  buffer** with deallocator `.none` (`sub_10026BFE0`), without a copy; if not, it copies to
  a new `Data` (`sub_10027C790`). It invokes the captured closure
  `(event_id, Data)`.
- Block invoke: `sub_100041ED0` (`0x100041ed0`): `fn = *(block+32)`,
  `ctx = *(block+40)`, `fn(id, data, len)` — identical to what
  `AWDL_WIFIP2PD_P2P_EVENTS.md` §2 describes.

Chain of closures up to the switch: `sub_1000697B0` (`0x1000697b0`, 10 B thunk) →
`sub_100063E80` (`0x100063e80`) → `sub_1000387F0(a2, a3, a4, ctx[2], ctx[3])`
(`0x1000387f0`) → decodes and fans out. The handler installers:
`sub_1000421E0` (`0x1000421e0`, recovery, passes `sub_1000697B0` and stores the handler in
the `eventHandler` ivar of `InterfaceHandle`), `sub_100049F20` (`0x100049f20`, passes
`sub_100063E80`; callers: thunk `sub_10004D140` with a witness data-xref at
`0x100514e48`, and `sub_100126180` ← `sub_1001263F0`), `sub_10004A0C0` (`0x10004a0c0`,
no code callers). The witness-table resolution of the AWDL path remains unnamed
(`?`, same as in `AWDL_WIFIP2PD_POST_CREATE.md` §2.1).

### 4.2 The master switch `sub_1000388D0` (0x1000388d0, 0x8e9e B)

Function receiving `(id, Data.w0, Data.w1)`, validating the payload with helpers
(`sub_100062B50`, `sub_1000631F0`, `sub_1000632E0`, `sub_100063370`, `sub_1000629F0`,
`sub_1000626D0`, `sub_100068600`, `sub_100068700`, `BinaryDecoder` `sub_1001C3F40`, …),
logging with the prefix `"nan_event: %s APPLE80211_M_…"` and building a value of the
`DriverEvent` enum (cases `DriverEvent.AWDL` with numeric tag; case names not recovered
from the Swift metadata, `?`), which it delivers to the handler (common tail
`LABEL_149`/`LABEL_204`; validation failures → `LABEL_235` = discard). The switch
over the raw id has **58 cases**; the ids and what each case reads from the payload
(offsets over `__dst` = payload start):

| id | offsets read | log / visible behavior |
|---:|---|---|
| 1, 2, 3, 9, 11 | — | cases present; handling not broken out in detail (`?`) |
| 46 | — | `DriverEvent.AWDL` tag 11 with payload `(1, 2)` |
| 47 | u8 `@68`, u32 `@8` (==1), u32 `@0`, u8 `@4` | `DriverEvent.AWDL` tag 8; gate: if the byte `@68` is 1 or `@8` != 1 discards; matches the 0x44 B kernel payload (MAC `@0`, u32 `@8`) |
| 48 | rep `Data` (0/1/2), len ≥ 0x48 | copies 64+ B, validates the 72 B header (`AWDLActionFrame.Header`), decodes the rest with `BinaryDecoder` (`sub_1001C3F40`) → `AWDLActionFrame`; `DriverEvent.AWDL` tag 1 |
| 55 | u8 `@8`, q `@16` | log `"nan_event: %s APPLE80211_M_DRIVER_AVAILABLE with powerOn false %d"` |
| 65 | — | `DriverEvent.AWDL` tag 13 |
| 66 | `@8`, `@24`, `@40` | — |
| 67/68 | — | 68 → `DriverEvent.AWDL` tag 0 |
| 74 | `@4`, `@104` | — |
| 78, 82 | — | — |
| 95 | `@0`, `@8` | log `APPLE80211_M_NAN_DEVICE_ROLE_CHANGED role=%u sync=%u hopCount=%hhu` |
| 96 | `@19`, `@35`, `@40`, `@48`, `@52` | `APPLE80211_M_NAN_MASTER_CHANGED immediateMaster=%@ rank=%hhu:%hhu anchorMaster=%@ … flags=%u` |
| 97 | `@2`–`@5` | `APPLE80211_M_NAN_CLUSTER_CHANGED` |
| 98 | `@8`, `@16`–`@32`, `@41`, `@44`, `@60`, `@64`, `@72`, `@73` | — |
| 99 | `@8` | `APPLE80211_M_NAN_FOLLOW_UP_TX_COMPLETE` |
| 100 | MAC `@0`–`@8`, `@16` | `APPLE80211_M_NAN_DISCOVERY_RESULT_RECEIVED from %s` |
| 102 | `@0`–`@8`, `@16` | `APPLE80211_M_NAN_PUBLISH_REPLIED_PRIVATE` |
| 104/105 | `@0`, `@8`, `@16` | `…_PUBLISH/SUBSCRIBE_TERMINATED` |
| 106 | `@2`–`@8`, `@16` | `APPLE80211_M_NAN_FOLLOW_UP_RECEIVED` |
| 112 | `@2`, `@6`, `@7`, `@11`–`@16` | `APPLE80211_M_NAN_DP_REQ_RECVD` |
| 113 | `@8`, `@12`–`@18`, `@24` | `APPLE80211_M_NAN_DP_RSP_RECVD` |
| 114 | `@8`, `@16` | `APPLE80211_M_NAN_DP_CONFIRM_RECVD` |
| 117 | — | `APPLE80211_M_NAN_DP_ESTABLISHED` |
| 118 | `@0`–`@13` | `APPLE80211_M_NAN_DP_END` |
| 122 | `@8`, `@16`, `@24`, `@28`–`@29`, `@48`–`@72` | — |
| 124 | `@8`–`@32`, `@48`, `@64`, `@80`, `@96`, `@112` | — |
| 125 | copies 0x323 B; gate `sub_100068600` | builds the event with u16 marker `0x100` `@+40` of the struct |
| 126 | u16 via `sub_1000632E0`; gate `(v & 0xFF0000) == 0x10000` | u16 marker `0x200` `@+40` |
| 127 | via `sub_100063370` + `sub_1000D3040` | u16 marker `0x300` `@+40` |
| 129 | — | `APPLE80211_M_NAN_DP_HOST_ASSIST_REQUEST` |
| 142 | — | — |
| 149 | — | `DriverEvent.AWDL` tag 2 |
| 157 | `@4`–`@8`, `@16`–`@20`, `@28`, `@32`, `@36`–`@37` | — |
| 158 | u8 `@2` | `APPLE80211_M_NAN_OOB_AF_TX_STATUS` |
| 159 | — | `APPLE80211_M_NAN_OOB_AF_RX` |
| 160 | — | `APPLE80211_M_SDB_MODE_CHANGED received mode:%hhu` |
| 172 | — | — |
| 179 | `@4`, `@8`, `@16`–`@48`, `@256` | log `"Failed to parse event[%ld]: %s"`, context "AirPlay over Infrastructure" / "Service Registration" |
| 182 | — | — |
| 195 | — | `DriverEvent.AWDL` tag 12 |
| 197 | — | `APPLE80211_M_NAN_PREFERRED_CHANNELS_CHANGED count=%ld channels=%s`; `"Invalid channel_band in preferred channel list"` |
| 200 | `@16`, `@32`, `@40` | — |
| 215 | — | — |
| 224 | — | — |
| 225 | u8 `@0` | — |
| 234 | — | — |
| 235 | — | `DriverEvent.AWDL` tag 14 |
| 243 | u16 `@62`, u8 `@63`, u32 `@68`, u8 `@70`, u8 `@71&1`, u32 `@69`, u16 `@73`; copies 56 B `@0` | repack + `DriverEvent.AWDL` tag 3 (storeEnumTagMultiPayload) |
| 246 | copies 0xF3/0xF2 B | `APPLE80211_M_NAN_RADIO_SCHED_CHANGED band24G=%s band5G=%s` |
| default | — | discarded (empty array) |

The cases without a field-by-field breakdown in the table exist and compile the same pattern
(validate → log → `DriverEvent`); their fine detail was not transcribed (`?` over the
individual detail, not over the consumption).

### 4.3 Kernel ↔ wifip2pd correlation

- **What wifip2pd consumes and the kernel emits over awdl0**: 46, 47, 48, 65, 68, 142,
  149, 172, 182, 195, 200, 234, 235, 243 (emitters in §3.2/§3.3) — all with a case in
  `sub_1000388D0`.
- **What the kernel emits over awdl0 and wifip2pd ignores** (no case → `default`):
  41, 42, 43 (availability windows), 64, 69, 77, 86, 88, 94, 135, 170, 183, 204.
- **P2P channel of the infra router** (§1.2): wifip2pd consumes {2, 3, 9, 66, 125, 126,
  127, 160} of the whitelist (it also registers them with StartMonitoringEvent in role 0);
  the ids 4, 80, 139, 198, 242 are forwarded to the P2P managers but wifip2pd does not
  handle them in the switch (4 without case `?` — it does not appear in the case list).
- **The 16 ids of `Apple80211StartMonitoringEvent`** (role 0): all but 4 have a
  case; 1, 2, 3, 9, 11, 55, 59, 66, 74, 78, 82, 125, 126, 127, 160, 215 do.

## 5) Classification: necessary for the health of awdl0 vs optional

- **Registration (blocking if it fails)**: the call to `Apple80211RawP2PEventMonitoringInit`
  (role 1) or `Apple80211RawEventMonitoringInit` + the 16 `Apple80211StartMonitoringEvent`
  (role 0) must return 0; an error chains `sub_1000421E0` (`0x1000421e0`) → log
  `"Failed to recover event monitoring for %s"` (exact site `?`, inside
  `sub_1000421E0`) → Swift error → recovery `sub_100046E30` (`0x100046e30`,
  "Attempting recovery for %s[%s] with %s on %s", exact site `?`) which re-creates the
  interface (`sub_100047660`). For the kext this means: the monitoring overrides
  (whatever the registration ioctl/user client serves) must answer success.
- **Operation**: no event is a health condition by itself — there is no timer
  that declares the interface dead due to event silence (`?`; the consumer contains no
  timeout logic, and recovery is triggered by RawGet/RawSet/ioctl failures, not by event
  absence). That said, the events that feed the state wifip2pd exposes are, per the switch
  content: **48** (AWDL frames = peer discovery), **47** (peer presence/absence),
  **46** (sync state), **195** (channel sequence), **65/142/149/243**
  (statistics), **125/126** (infra scan), and they are the ones a custom kext should
  emit for equivalent behavior; the other cases are accounting and logs.
- **Optional (the Broadcom kext emits them, wifip2pd discards them)**: 41, 42, 43, 64, 69,
  77, 86, 88, 94, 135, 170, 183, 204, 208, 210, 140, 215 (`?` for 215: it has a case,
  but its handling was not broken out).
- The 30 s semaphore after `VIRTUAL_IF_CREATE` **does not** depend on any event
  (IORegistry `IOServiceMatched`; closed in `AWDL_WIFIP2PD_POST_CREATE.md` §Part 1).

## 6) Method and limits

- IDA 9.4 headless (`idat -A -S`) over copies in
  `Build/ida_awdl/task_awdl_events/kernel/` (BootKernelExtensions.i64) and `/p2pd/`
  (wifip2pd.i64). The old IDB `~/re_cache/ida/boot.kc.i64` **crashes Hex-Rays**
  (internal error 1228, reproducible in `k_run2*.log`); the whole kernel side was done
  over a copy of the newer IDB `Build/ida_awdl/BootKernelExtensions.i64`
  (same KC, container md5 `f489ff8b…`), also as a local copy. Probes:
  `k_probe1` (tables+xrefs), `k_probe2a/2b` (plumbing and Broadcom emitters),
  `k_probe3` (matrix over registers + `gAppleMIndexToString` + user
  client selectors), `k_probe4` (routeToP2PInterface, extSubscribeEvent, action frames, removePeer),
  `k_probe5` (post* helpers, sweep of +0xB18 virtual calls), `k_probe6`
  (postAFToP2pd, reportDataPathEvents), `k_probe7` (subscription bitmap);
  `w_probe1–5` (closure chain and master switch).
- The id matrix per call site was extracted from the immediate loaded in the
  argument register (edx for `IO80211Controller::postMessage`, esi for the rest)
  walking backward from the `call`; the sites marked STALE (an intermediate call
  between the load and the `call`) are high-confidence only when the id matches the
  function (e.g. GAS → 219/220); those that do not match were not listed.
- Explicit `?`: kernel setter of the per-id bit of the subscription bitmap and of the
  bypass flag `@+416`; case names of the Swift enums `DriverEvent`/`DriverEvent.AWDL`
  (numeric tags only); payloads of 67, 86, 140, 195, 200, 210, 215, 234, 235, 246 and
  of all the NAN ones; field-by-field mapping of the 41 payload; semantics of the u32 `@8` of
  the 47 payload (absence vs presence); the installer of the `eventHandler` of role 1
  (dispatch by witness `0x100514e48` unresolved); absence of silence-based timeouts
  (inference from absence, not an exhaustive search).
- No repository file was modified outside `Docs/AWDL_EVENTS.md` and
  `Build/ida_awdl/task_awdl_events/`.
