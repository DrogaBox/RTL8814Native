# Plan of stages 2 to 4 of AWDL in RTL8814AirLayer

Work plan for the stages that follow stage 1 (`awdl0` created and accepted by `wifip2pd`,
without peers or real datapath). All content is derived exclusively from the documents
`Docs/AWDL_*.md` already verified against kernelcache 25G241 (md5 `f489ff8be80e6013a1bf47561dcaef45`)
and against `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`); nothing in this document comes
from a new verification. Everything the source documents do not close is marked `?`.

Numbering: this document reorders the stages of `AWDL_PROTOTYPE_DESIGN.md` §4–§6. There stage 2
was synchronization/election and stage 3 the action frames; here **stage 2** is the real
transmission and reception datapath (§6 of the prototype), **stage 3** groups synchronization, action frames and
events toward the daemon (§4 and §5 of the prototype: peer discovery requires the
synchronization engine as a prerequisite), and **stage 4** is the `llw0` companion and low latency (§6 of
the prototype and §8 of the stage 1 spec).

## 0. Starting point: what stage 1 leaves

- `awdl0` is born and the daemon accepts it: `CARD_CAPABILITIES` gate (byte +0x08 bit 0), `RawSet` 94 with
  role 6, `RawGet` 97 with the parent's name, `SIOCGIFMEDIA` with `(ifm_current & 0xE0) == 0x80`,
  standard ifnet ioctls and event monitoring registration returning 0
  (`AWDL_WIFIP2PD_POST_CREATE.md` §2.6; `AWDL_PROTOTYPE_DESIGN.md` §3).
- Queues: 1 TX submission + 1 TX completion + 1 RX completion get registered and started on a role-6
  interface; the netif only rejects 0 TX queues and requires 4 if the information qword carries the WMM
  bit (`0x8`) (`AWDL_STAGE1_QUEUE_SHAPE.md` §1–§3). The TX submission action is a no-op that
  returns the batch through the TX completion (`AWDL_STAGE1_CODE_FIXES.md` §1.2).
- AWDL surface: the pure slots (490 and 507–569) go as stubs; type 293 is not implemented
  and the dispatch returns 102 without touching `awdl0` (`AWDL_LLW_COMPANION.md` §4).
- Radio: no PSF, no availability windows, no channel hopping, no peers. Nothing of the AWDL
  protocol is on the air.

## 1. Stage 2 — Real transmission and reception datapath

### 1.1 Objective

Move real packets through `awdl0`: the stack enqueues into the TxSubmissionQueues, the consumer kext
takes them to the USB radio and transmits them; the radio delivers the received frames and the kext
publishes them into the registered RxCompletionQueue. The MTU 0x594 (1428 usable after the 64 headroom)
and headroom set in stage 1 become functional (`AWDL_PROTOTYPE_DESIGN.md` §6;
`AWDL_PROXIMITY_DATAPATH.md` §2). No peers yet: the datapath's validity is local (TX goes out through
the radio, RX enters the stack).

### 1.2 What the daemon requires to consider it up and running

The daemon does not exercise the datapath: in steady state it emits no AWDL GET/SET — its 4
`RawGet` call sites (types 12 and 97) and 2 of `RawSet` (94 and 293) are all of them
(`AWDL_PROTOTYPE_DESIGN.md` §1.5; `AWDL_STRUCTS.md` §2). No event is a health condition and
recovery is triggered by `RawGet`/`RawSet`/ioctl failures, not by event silence
(`AWDL_EVENTS.md` §5). The judge of this stage is the stack, not the daemon:

1. `awdl0` comes up with `ifconfig` and supports the standard ifnet ioctl path (`SIOCSIFFLAGS`,
   `SIOCSIFLLADDR`, `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI`)
   (`AWDL_WIFIP2PD_POST_CREATE.md` §2.6).
2. Real traffic (e.g. IPv6/ND) crosses `awdl0` without panic; in stage 1 the no-op action already
   consumed the batch through the TX completion (analog of test T3.5 of
   `AWDL_STAGE1_IMPL_SPEC.md` §14), but without radio.
3. The daemon keeps its handle: absence of the re-creation loop (`No AWDL interface found.
   Creating AWDL interface` repeated; `AWDL_LLW_COMPANION.md` §1.3) and of `Failed to recover event
   monitoring for %s` (`AWDL_EVENTS.md` §5).

### 1.3 RTL8814AU capabilities required

| Capability | State in the RTL8814 path | Reference |
|---|---|---|
| Data TX per AC | The standard data path exists and works for `en2`: MAC queues `QSLT_BK/BE/VI/VO`, three bulk OUT, 32 B descriptor (`Docs/ARCHITECTURE.md:20` cited there) | `AWDL_RTL8814_HW_SUPPORT.md` §4.3 — **feasible** |
| Data RX of arbitrary unicast/multicast | The RCR with `AAP/AM/AB/AMF` and `RXFLTMAP0 = 0xFFFF` alive accepts frames from unknown senders; the software discard is in the management path, not the data one | `AWDL_RTL8814_HW_SUPPORT.md` §6.1 — **feasible** |
| RX delivery toward AirLayer | Today the only thing leaving the core toward IOKit is data traffic converted to Ethernet toward `en2` (`rtl_netif_rx`); the equivalent channel toward the AirLayer does not exist and the integration point is still to be decided | `AWDL_RTL8814_HW_SUPPORT.md` §6.4, §8 — `?` |
| Frames addressed to the AWDL MAC | The hardware passes them (`RCR_AAP`); which software filter applies to a data frame whose A1 is the AWDL MAC is not described | `AWDL_RTL8814_HW_SUPPORT.md` §6.2 — `?` |
| Reserving the 64 B headroom in the radio RX path | The consumer of `RegistrationInfo+0x3C` is closed (`IOSkywalkNetworkInterface::getTxDataOffset`, slot 0x9C0), but how the vendor core RX path reserves that space is not described | `AWDL_STAGE1_PENDING_CHECKS.md` §1; `AWDL_RTL8814_HW_SUPPORT.md` §7 row 6.1 — `?` |
| Channel and concurrency with the infra station | A single channel resource shared by software; for stage 2 without peers the channel is fixed and sharing does not intervene | `AWDL_RTL8814_HW_SUPPORT.md` §2.3, §5.3 — **sufficient for this stage** |
| Scheduled TX / timing interrupts | They do not exist (neither TX timestamp nor usable TBTT/early IRQ on USB); not needed until stage 3 | `AWDL_RTL8814_HW_SUPPORT.md` §3.3, §4.4 — **not required here** |

### 1.4 Family slots and APIs involved

- **Queue shape.** Decide between keeping 1 TX submission (no WMM bit) or moving to 4 TX submissions
  (one per WME AC, map {0,1,2,3}) with the WMM bit: both paths register; the netif requires the 4
  only if the WMM bit is set (`AWDL_STAGE1_QUEUE_SHAPE.md` §2–§4). The 4×AC are the Broadcom
  choice for its WMM model and the AC→queue-id map used by `getTxSubQueue`
  (`AWDL_PROXIMITY_DATAPATH.md` §2, §7). Concrete choice for `awdl0`: `?`.
- **TX.** Non-null action of the TxSubmissionQueue (requirement of `initWithQueues`/`initWithPool`,
  `0xffffff8002b0caac`; `AWDL_STAGE1_CODE_FIXES.md` §1) turned into the real consumer: enable
  (`IOEventSource` enable), `requestDequeue(void*, uint)`, process the batch and return the packets
  through the TX completion. Broadcom reference pattern: `dequeuePackets` with enabled-interface and
  chip-ready checks (`AWDL_PROXIMITY_DATAPATH.md` §6, §9).
- **RX.** Delivery into the registered RxCompletionQueue with `enqueuePackets` (batches smaller than
  capacity, packets of the same pool as the queue) and kick-off with `requestEnqueue(0, 0)`
  (`AWDL_PROXIMITY_DATAPATH.md` §10, §14).
- **Pool.** `IOSkywalkPacketBufferPool::initWithName` with the same `PoolOptions` the layer already
  uses (`maxBuffersPerPacket = 1`, flags = 1); own selectable size respecting the invariants
  (`AWDL_PROXIMITY_DATAPATH.md` §5, §14).
- **Enablement.** `enable(unsigned)`/`disable(unsigned)` → `enableDatapath`/`disableDatapath`:
  enable TX completion, RX completion and TX submissions, fire `requestDequeue(0,0)` /
  `requestEnqueue(0,0)` (`AWDL_PROXIMITY_DATAPATH.md` §8).
- **Datapath getters.** `getRxCompQueue` (+0xD70), `getTxCompQueue` (+0xD78), `getTxSubQueue`
  (+0xD80), `getTxPacketPool` (+0xD88); `getRxPacketPool` (+0xD90) stays inherited from the family and its
  behavior for a non-Broadcom interface was not decompiled (`AWDL_PROXIMITY_DATAPATH.md` §7, §15).
- **`RegistrationInfo`.** MTU `+0x48/+0x4C = 0x594`; headroom `+0x3C = 64` read by
  `getTxDataOffset` (slot 0x9C0) (`AWDL_INTERFACE_BIRTH.md` §2.2;
  `AWDL_STAGE1_PENDING_CHECKS.md` §1).
- **Pending and flush.** `stagePacket`/`freePendingPackets`/`flushPackets` (+0x340/+0x348/+0x330) and
  `sendPendingPacketsToStack` (+0xF30) are slots added by the Broadcom subclasses over the
  family's vtable; the equivalent on the family classes does not appear in the documents — `?`
  (`AWDL_PROXIMITY_DATAPATH.md` §6, §11).
- **AWDL multicast.** Original+duplicate pair (`stashMulticastPacketPair`, `duplicatePacket`) and
  multicast queue (`triggerAWDLMulticastTx`, `p2pMulticastTx`): the specifically AWDL part of the datapath;
  it can be deferred to stage 3 together with the second social channel — `?`
  (`AWDL_PROXIMITY_DATAPATH.md` §9, §11).

### 1.5 Risks

- Queue shape change (1 → 4) with no precedent in the RTL tree; netif revalidation with the
  WMM bit and the AC→queue-id map are new ground (`AWDL_STAGE1_QUEUE_SHAPE.md` §2–§4).
- The RX integration point (vendor core → AirLayer) is not decided; it is the biggest gap
  (`AWDL_RTL8814_HW_SUPPORT.md` §6.4, §8).
- Behavior of the inherited slot `getRxPacketPool` for a custom interface
  (`AWDL_PROXIMITY_DATAPATH.md` §15).
- The concurrency defects detected in the stage 1 review (`_awdlLock` ↔ work loop inversion,
  stop ↔ slot 94 race) raise their probability with real traffic
  (`AWDL_STAGE1_CODE_REVIEW_HOOK.md`).
- Coexistence of the USB pipes between the `en2` datapath and that of `awdl0` (MGT shares a pipe with
  the high ACs; the real priority is set by the MAC EDCA) — `?`
  (`AWDL_RTL8814_HW_SUPPORT.md` §4.3).

### 1.6 Hardware test and log pattern

Precondition: tests T1–T7 of stage 1 pass (`AWDL_STAGE1_IMPL_SPEC.md` §14).

| # | Action | Expected | Log pattern |
|---:|---|---|---|
| E2.1 | Load the kext and create `awdl0` (`rtlawdl=1`) | Same as T2/T3: clean birth | Kext ring: `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: born'`; absence of the ten birth-failure literals (`AWDL_STAGE1_TEST_LOG_PATTERNS.md` §4) |
| E2.2 | `ifconfig awdl0 up` and IPv6/ND traffic | No panic; the real TX action consumes the batch | `?` — no log literal of this stage exists yet in the sources; add own `LOG` lines of the kext (design `?`), readable in the ring of `RTLAirLayerLog` (`AWDL_STAGE1_TEST_LOG_PATTERNS.md` §1) |
| E2.3 | TX toward the radio without a peer (e.g. NDP multicast) | The frame goes out the USB pipe (verifiable with the TX counter of the descriptor) | `?` — visible counters: `netstat -in awdl0` (not documented `?`); the frame does not come back through RX |
| E2.4 | RX of a frame addressed to the AWDL MAC (injection from another adapter in monitor, or echo of the local chip `?`) | The frame reaches the RxCompletionQueue and the stack | `tcpdump -i awdl0` (standard ifnet BPF, not documented in the AWDL docs `?`); kext `LOG` line `?` |
| E2.5 | With the daemon alive during all of the above | No re-creation nor recovery | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found|Failed to recover event monitoring'` → `0` (`AWDL_LLW_COMPANION.md` §1.3; `AWDL_EVENTS.md` §5) |

## 2. Stage 3 — Peer discovery events and state exposed to the daemon

### 2.1 Objective

That real AWDL peers appear before the daemon: the synchronization/election engine works, the action
frames are exchanged on the social channels and the daemon receives the events feeding its
state. The plan of the documents is **to reuse the kernel's `IO80211AWDLPeerManager`** instead
of reimplementing the protocol: the `createPeerManager` factory is inheritable and for role 6 builds
`IO80211AWDLPeerManager::withInterface`, with all the election, MI/UMI templates, TLV parsing,
peer table and 14 timers on the kernel side (`AWDL_PEERMANAGER_SOFTWARE.md` §2.1, §10). All the
necessary symbols are exported by the entry `com.apple.iokit.IO80211Family`
(`AWDL_KEXT_LINKAGE.md` §0, §1.2).

### 2.2 What the daemon requires to consider it up and running

- The monitoring registration must keep returning 0; an error chains the recovery that
  re-creates the interface (`AWDL_EVENTS.md` §5).
- The daemon consumes over `awdl0` the ids (switch `sub_1000388D0`; `AWDL_EVENTS.md` §4.2):

| id | Daemon validation | Result |
|---:|---|---|
| 48 `AWDL_ACTION_FRAME_RX` | `len >= 0x48`; decodes the 72 B `apple80211_act_frm_hdr` header and the rest with `BinaryDecoder` | `DriverEvent.AWDL` tag 1 — peer discovery |
| 47 `PEER_PRESENCE` | Payload 0x44 B; gate: discards if byte @68 equals 1 or u32 @8 ≠ 1; reads MAC @0 and u8 @4 | tag 8 — presence/absence |
| 46 `AWDL_SYNC_STATE_CHANGED` | No offset reads | tag 11 — sync state |
| 65 `AWDL_STATISTICS` | No reads | tag 13 |
| 195 `AWDL_CHAN_SEQ_CHANGED` | No reads | tag 12 |
| 149 `P2P_AIRPLAY_STATISTICS` | No reads | tag 2 |
| 243 `AWDL_RTG_STATISTICS` | Reads u16 @62, u8 @63, u32 @68, u8 @70, u8 @71&1, u32 @69, u16 @73; copies 56 B | tag 3 |
| 68 `REALTIME_MODE_END`, 235 `IDLE_SESSION` | No reads | tags 0 and 14 |

- No event is by itself a health condition and there is no silence timeout (`AWDL_EVENTS.md` §5); what
  the daemon does with the `DriverEvent.AWDL` tags beyond the switch is not in the documents
  (`?`).
- On the kernel side, `monitorTimeline` validates the payload of event 41
  (`{u16 AwNumber +0, master MAC +8, topMaster MAC +14, u32 FwAwStartTime +20}`) against the programmed
  master; an incoherent emitter makes the peer become "unsynchronized" (deviation ≥ 6554 µs
  with arithmetic modulo 2^20 µs) (`AWDL_PEERMANAGER_SOFTWARE.md` §5;
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2–§2.3; `AWDL_OPEN_QUESTIONS_1.md` §2.2–§2.3).

### 2.3 RTL8814AU capabilities required

| Capability | State in the RTL8814 path | Reference |
|---|---|---|
| RX of action frames from unknown peers | The hardware already accepts them (`RCR`/`RXFLTMAP0`); whoever discards them is the vendor core (`mgt_dispatcher` requires own A1 or broadcast; `OnAction` ignores unknown categories). Intervening before the A1 check has precedent (PMF/SAE hooks) | `AWDL_RTL8814_HW_SUPPORT.md` §6.1–§6.3 — **feasible**; the delivery point toward AirLayer is `?` (§6.4, §8) |
| TX of sync frames and action frames | Standard management frame: queue `QSLT_MGNT`, fixed rate (driver analog of `awdl_min_rate` with `update_mgnt_tx_rate`), retry 6/12; no scheduled TX or per-AW repetition: each send is a software enqueue anchored to a host timer | `AWDL_RTL8814_HW_SUPPORT.md` §4.1–§4.4, §7 row 6.2(b3b) — **partial** |
| Availability windows | TSF readable/writable by port (`REG_TSFTR`/`REG_TSFTR1`); no TBTT/early interrupt on USB → host software timers; `REG_TXPAUSE` as window gate | `AWDL_RTL8814_HW_SUPPORT.md` §3.1–§3.3, §7 row 6.2(b1) — **partial**; jitter precision `?` |
| Channel hopping per the sequence | Synchronous change without IQK on the hot path, ~100 ms/channel measured in scanning; full band switching 2.4↔5 GHz; social channels 6/44/149 without DFS `?` | `AWDL_RTL8814_HW_SUPPORT.md` §5, §7 row 6.2(b3a) — **feasible** with timing reserve |
| Master election | 100 % kernel side (PeerManager); the driver only reflects the role (ioctl 134) on the radio. Per-frame RSSI available in `attrib->phy_info` if the manager queries it | `AWDL_PEERMANAGER_SOFTWARE.md` §3, §10; `AWDL_RTL8814_HW_SUPPORT.md` §7 row 6.2(b2) — **feasible** |
| TX confirmation (`awdl_oob_af`) | `CONFIG_XMIT_ACK` is commented out; the degraded wait is a 50 ms sleep. No equivalent today | `AWDL_RTL8814_HW_SUPPORT.md` §4.2, §7 row 6.3(c3) — `?` |
| CCA statistics / `awdl_stats` | PhyDM CCX is not in the build list; counters in software or additional modules | `AWDL_RTL8814_HW_SUPPORT.md` §7 last row — **missing**; declare unsupported via `awdl_cap` (`AWDL_PEERMANAGER_SOFTWARE.md` §10 point 8; `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.3) |

### 2.4 Family slots and APIs involved

**Manager creation (all exported, `AWDL_KEXT_LINKAGE.md` §1.2):** inherited `createPeerManager`
(slot 355, vptr+0xB08) → `IO80211AWDLPeerManager::withInterface`
(`0xffffff80021c14bc`); registration on the controller at +400. The linkage uses
`OSBundleLibraries → com.apple.iokit.IO80211Family` as today (`AWDL_KEXT_LINKAGE.md` §1.1).

**Driver obligations, in order of criticality** (`AWDL_PEERMANAGER_SOFTWARE.md` §10):

1. Repeat the sync frame template: receive the internal ioctl 151 (`0x97802869C8`, 512 B
   template) and transmit it every `syncFramePeriod` (ioctl 116, `0x74802869C8`) aligned with the project's own
   timeline — the role Broadcom firmware plays (`AWDL_PEERMANAGER_SOFTWARE.md` §4, §9.2;
   `AWDL_FIRMWARE_DUTIES.md` §6.2).
2. Timeline events: publish 41/42/43 with the format
   `{AwNumber, master, topMaster, FwAwStartTime}` through `reportDataPathEvents`
   (`0xffffff80021dd8f4`, cell 83, vptr+0x298) or through `postMessage` → `postMessageSync`
   (`0xffffff80022f8abc`) (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2; `AWDL_KEXT_LINKAGE.md` §2.2;
   `AWDL_EVENTS.md` §1.1).
3. Dwell of the channel sequence (ioctl 129, `0x81802869C8`) and social channels (132/205,
   `0x84802869C8`/`0xCD802869C8`) (`AWDL_PEERMANAGER_SOFTWARE.md` §6, §9.2).
4. RX of action frames: capture on the social channels and call `actionFrameInput` (cell 71,
   vptr+0x238, `0xffffff800220e1ae`) with an `IO80211Buffer` (`__ZTV13IO80211Buffer`
   `0xffffff8002473e80`); alternative `bpfTapInput` (`0xffffff8002331070`). The buffer starts at the
   category; 16 B fixed header validated (OUI `00:17:F2`, type `0x08`, version ≠ 0, subtype 0/3
   at +6) and TLVs from +16 with 3 B header (`AWDL_PEERMANAGER_DRIVER_ABI.md` §1;
   `AWDL_FRAME_WIRE_FORMAT.md` §2–§3).
5. Data TX pull: serve `dequeueTxPacket` (cell 85, vptr+0x2A8),
   `getUnicastPackets`/`getMulticastPackets` (cells 46/45) during the windows
   (`AWDL_PEERMANAGER_SOFTWARE.md` §8.3, §9.1).
6. Sync state: accept ioctl 134 (`0x86802869C8`) and reflect it on the radio; emit event
   46 with `postSyncStateChanged` (`0xffffff8002332054`, id 46, len 0) or the 16 B payload in
   Broadcom style (`AWDL_EVENTS.md` §3.1, §3.3).
7. Pure slots: the types with GET/SET slot (general 1/4/9/13 at +0xFC8..+0xFE0 and the specific
   ones 116–467 listed in `AWDL_PROTOTYPE_DESIGN.md` §4; per-type contracts in
   `AWDL_APPLE80211_DISPATCH.md` §5 and `AWDL_STRUCTS.md` §4). The 27 PeerManager-only types are
   resolved by the family (`AWDL_PROTOTYPE_DESIGN.md` §4).

Event emission split with the reused manager: the **driver** emits 41/42/43 (its own
window clock) and 46 (after the 134/election); the **kernel** emits 48 (`postAFToP2pd`
`0xffffff800220fc2c`, UMIs only), 47 (`removePeer`/`cleanUpForPeerRemovedFromNDP`), 195
(`setChannelSequenceUnconditional`), 65/142/149/243 (`post*` helpers with fixed length) — the driver
does not emit those ids (`AWDL_EVENTS.md` §3.2, §3.3). Emitter of 47 on the addition of a peer: not documented `?`.

### 2.5 Risks

- Registration of a third-party subclass in the AWDL hierarchy (metaclass) and use of the
  inherited `createPeerManager`: no verified precedent (`AWDL_STAGE1_IMPL_SPEC.md` §15.10).
- Exact route of the manager's internal ioctls toward the kext slots: the encoding
  `0x802869C8` with the type in the high dword is observed, not documented by Apple
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.1; `AWDL_PEERMANAGER_SOFTWARE.md` §9.2).
- Builders of TLV 6/8/10/11 of the template and exact content of the TX pull left open
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11).
- UMI condition (subtype + channel) deciding which action frame reaches the daemon as 48: open
  (`AWDL_FRAME_WIRE_FORMAT.md` §2; `AWDL_OPEN_QUESTIONS_1.md` §1.1).
- Precision of the host timers against the AWDL cadence (64–128 TU) and unit of the guard time:
  `?` (`AWDL_RTL8814_HW_SUPPORT.md` §3.2; `AWDL_FIRMWARE_DUTIES.md` §6.2).
- Coherence of the layer's own clock for `AwNumber`/`FwAwStartTime` (modulo 2^20 µs) required by
  `monitorTimeline`: `?` (`AWDL_OPEN_QUESTIONS_1.md` §2).
- RX cycle limited by channel sharing with the infra station (listen duty cycle on the
  social channels): `?` (`AWDL_RTL8814_HW_SUPPORT.md` §5.3).
- Test environment without an Apple peer (only a Samsung A53 as reference peer): the verification
  of real discovery stays bounded (`HANDOFF_AWDL.md` §1).

### 2.6 Hardware test and log pattern

Precondition: stage 2 passed. Without an Apple peer the plan splits into synthetic validation
(kernel → daemon chain) and real validation (`?` available).

| # | Action | Expected | Log pattern |
|---:|---|---|---|
| E3.1 | With `awdl0` created, the kernel manager stays instantiated for role 6 | The manager exists and arms its template | `?` — manager literals (`Setting Election Params to Self`, `I am Root Node`; `AWDL_PEERMANAGER_SOFTWARE.md` §3.2) are family-kext `IOLog`; their visibility in `log show` of 26.7.1 is not verified `?` |
| E3.2 | Synthetic injection: the driver injects through `actionFrameInput` a locally built action frame (16 B header + valid TLVs) | The manager parses and classifies it; if it is a UMI, it emits 48 toward the daemon | wifip2pd: `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'nan_event:'` — the `nan_event: %s APPLE80211_M_…` prefix of the switch is documented (`AWDL_EVENTS.md` §4.2); the literal per case of 48 was not transcribed `?` |
| E3.3 | Deliberately wrong header (different OUI) | Discard with `BAD OUI in AF` and dump | Kernel literal `BAD OUI in AF` (`AWDL_FRAME_WIRE_FORMAT.md` §2) — visibility `?` (E3.1) |
| E3.4 | Emit 41/42/43 from the driver's own clock | `monitorTimeline` validates them without marking desynchronization | `?` — the validation is internal (flag `+10304`, `AWDL_PEERMANAGER_SOFTWARE.md` §5); observable through the absence of the deviation trace `?` |
| E3.5 | Data traffic during the project's own window | `dequeueTxPacket`/`getUnicastPackets` serve and the radio transmits | Kext ring: own `LOG` lines (design `?`); `tcpdump -i awdl0` for the on-air result `?` |
| E3.6 | Daemon alive during all of the above | No recovery, no re-creation | `log show … \| grep -cE 'Failed to recover event monitoring|No AWDL interface found'` → `0` (`AWDL_EVENTS.md` §5; `AWDL_LLW_COMPANION.md` §1.3) |
| E3.7 | (With an Apple peer, if one exists) bring a Mac/iOS close in AWDL mode | Peers at the daemon: real events 48/47/46 | `?` — same `nan_event:` probe of E3.2; the event pipe ring must not fill (`Eventpipe is full, drop message[%d][%s], dataLen %lu`; `AWDL_EVENTS.md` §1.1) |

## 3. Stage 4 — `llw0` companion and low latency

### 3.1 Objective

Implement type 293 (`COMPANION_SKYWALK_IF_CREATE`) so the daemon obtains its low latency companion
interface `llw0`, and — only if the low latency datapath is wanted for real — the
packet movement and the associated statistics. Creating `llw0` is pipeline work:
a second Skywalk interface with prefix `llw` and role publishing
`IO80211InterfaceRole = "LowLatency"` (`AWDL_LLW_COMPANION.md` §3.2–§3.4, §4).

### 3.2 What the daemon requires to consider it up and running

Closed contract (`AWDL_LLW_COMPANION.md` §1–§4):

- **Single emission:** the `RawSet` 293 is sent against the handle of `awdl0` (already created and accepted) when
  building the `AWDLInterface`, without feature gate; 24 B payload `{u32 code @0 (the kernel does
  not read it, `?`), u32 op @4, output name @8}`; only caller `0x100048061`, emitter
  `sub_10004B300` (`RawSet` at `0x10004b3e6`).
- **Success:** write the BSD name at payload+8 (max 16 B); the daemon validates with
  `if_nametoindex("llw0")` (literal `0x30776C6C` at `0x10004b814`). If the companion already exists,
  return 0 with the existing one's name.
- **Destruction:** `op = 0`; without a companion it returns `0xE00002C0`. Ops other than 0/1: no-op that
  returns 0.
- **Absorbed failure:** two errors (`Failed to create low latency interface for %s because %@` at
  `0x10004b50f`; `Failed to create interface %s on %s with error %@` at `0x100048fb5`), the init absorbs them
  with `Failed to create the companion low latency interface for AWDL: %@` (`0x100417ead`) and
  continues: `awdl0` intact, no teardown or retry.
- **The 30 s trap:** answering success without creating anything costs a 30 s wait per attempt
  (`0x100048b13`) and the timeout `Timed out while waiting for the low latency interface with parent
  %s` (`0x100048beb`) — avoid.
- **IORegistry property:** `IO80211InterfaceRole = "LowLatency"` (role 2, table
  `0xffffff800246bd88[1]`) is what the discoverer uses to signal the role 4 semaphore.
- The daemon **does not open an Apple80211 channel toward `llw0`**: the discoverer signals without creating a handle
  (`0x100045297`); witness uses of the companion `CoreP2P.WiFiInterface` object (e.g. ioctls
  `'i'` 130/131) unresolved — `?` (`AWDL_LLW_COMPANION.md` §3.5).
- Low latency statistics consumed by the daemon over `awdl0`: 142 (`AWDL_HPP_STATISTICS`,
  0x658 B), 149 (0xD0 B), 243 (0x4C B) (`AWDL_EVENTS.md` §4.2); who triggers them on a stack without
  firmware — `?`.

### 3.3 RTL8814AU capabilities required

| Capability | State in the RTL8814 path | Reference |
|---|---|---|
| Create `llw0` as second Skywalk interface | Requires no new radio: Broadcom creates it on the same chip as a role-2 Skywalk interface; the pool/queues/registration mechanism is that of stage 2 | `AWDL_LLW_COMPANION.md` §3.3–§3.4; `AWDL_PROXIMITY_DATAPATH.md` §3, §14 — **feasible** |
| Real low latency datapath (concurrent traffic) | Requires second port/concurrency: the hardware has 5 MAC/BSSID pairs and MBSSID CAM of 8, but the build is single-interface (`CONFIG_CONCURRENT_MODE` commented out) and port 1 on the USB path (BCNQ1, port IRQ) is untested | `AWDL_RTL8814_HW_SUPPORT.md` §1, §2.1–§2.3 — hardware **feasible**, driver `?` |
| Low latency TX queue | Broadcom picks capacity 1024 when `isLowLatencyEnabled`; the choice for the RTL layer is free | `AWDL_PROXIMITY_DATAPATH.md` §4 — `?` |
| `hp2p` / `LLW_PARAMS` (type 344) | On Broadcom it is a firmware iovar; the slots +0x1098/+0x1190 of the AWDL vtable operate on the PeerManager (`llwTxDelayThr`/… via `IO80211NANPeerManager`), not on `llw0` | `AWDL_LLW_COMPANION.md` §3.5; `AWDL_FIRMWARE_DUTIES.md` §3.4 — delegate to the manager `?` |
| Statistics 142/149/243 | They are emitted by the manager's helpers (kernel); the driver takes no part unless the counters are its own | `AWDL_EVENTS.md` §3.2, §3.3 — `?` on the trigger without firmware |

### 3.4 Family slots and APIs involved

- **Dispatch of the 293:** `gSetHandlerTable[292]` (`0xffffff8002460810`) → handler
  `0xffffff800228b4b8` (requires `len == 24` and `data != NULL`, else returns 22; tries the WCL
  path and falls to the glue) → `safeMetaCast` to `IO80211AWDLProtocol` → **slot +0x1180** of the AWDL vtable
  (`AWDL_LLW_COMPANION.md` §3.1).
- **Override in `RTLAwdInterface`:** `setCOMPANION_SKYWALK_IF_CREATE` at +0x1180: op at payload+4,
  name at payload+8 (`__strncpy_chk(..., 0x10)`), guard of already-existing companion (own ivar in
  the style of Broadcom `ivars+120`), error without writing the name (`AWDL_LLW_COMPANION.md` §3.2).
- **Class of the LL interface:** own Skywalk subclass with role 2 (`setInterfaceRole(2)` publishes
  `IO80211InterfaceRole = "LowLatency"`), `getBSDNamePrefix` → `"llw"` (slot +0x9E8), `RegistrationInfo`
  with the prefix at +0x30, flags `|= 4`, `setLowLatencyEnabled(true)`, `setInterfaceEnable` →
  `reportLinkStatus(3, 0x80)`, `processBSDCommand` rejecting the apple80211-per-ioctl constants
  (`0xC02069C8`, `0xC02069C9`, `0xC02869C9`, `0x802069C8`, `0x802869C8`) — model
  `AppleBCMWLANLowLatencyInterface` (`AWDL_LLW_COMPANION.md` §3.3–§3.4). Whether that interface derives from
  the infra chain (like Broadcom) or from another base is a `?` decision.
- **Companion links:** `IO80211SkywalkInterface::setCompanionInterfaceId`
  (`0xffffff80022f952a`) and `IO80211VirtualInterface::setCompanionLowLatencySkywalkInterface`
  (`0xffffff8002334c6e`, ivar +216). The export of the second does not appear in the selection of
  `AWDL_KEXT_LINKAGE.md` §1.2 (the doc warns that table is a selection) — `?`.
- **flushFlowQueues** delegating to the companion: null-safe (with a null companion it calls only the
  base), no panic risk from absence (`AWDL_LLW_COMPANION.md` §3.5;
  `AWDL_PROXIMITY_DATAPATH.md` §11).
- **Destruction:** (`wifip2pd` emits no `op = 0` in this build, see `AWDL_STAGE4_LLW_CLOSURE.md` §3.1) through `op = 0` with the kext's own teardown (Broadcom uses
  `destroySkywalkInterface` + slots 0x600/0x90) (`AWDL_LLW_COMPANION.md` §3.2).

### 3.5 Risks

- The 30 s trap: a kext answering success without creating the interface pays 30 s per attempt and
  ends in the same logical failure; create or return an error, never an empty success
  (`AWDL_LLW_COMPANION.md` §2.2, §4).
- Semantics of the payload's `code` (the kernel does not read it; the value on the AWDL path was not determined):
  `?` (`AWDL_LLW_COMPANION.md` §1.1, §5).
- Real low latency datapath over USB port 1: no tested precedent in this tree — `?`
  (`AWDL_RTL8814_HW_SUPPORT.md` §2.3).
- Runtime use of the daemon's companion object (ioctls `'i'` 130/131): closed in `AWDL_STAGE4_LLW_CLOSURE.md` §2 (IPv6 socket; the 131 returns 45 if nobody serves it); exact destination `llw0`: `?`
  (`AWDL_LLW_COMPANION.md` §3.5).
- Creation order: the request arrives only after `awdl0` is complete; a `llw0` creation that
  fails must not touch the already registered datapath of `awdl0` (own rollback, same discipline as
  §11 of `AWDL_STAGE1_IMPL_SPEC.md`).

### 3.6 Hardware test and log pattern

Precondition: stage 1 passed (it does not require stages 2/3 for the creation part).

| # | Action | Expected | Log pattern |
|---:|---|---|---|
| E4.1 | With `awdl0` accepted, wait for the post-create sequence | The daemon sends the 293 and the kext creates `llw0` | wifip2pd: `grep -E 'Creating companion low latency interface'` present (`0x10041b48b`; `AWDL_LLW_COMPANION.md` §1.3); `grep -cE 'Failed to create low latency interface|Timed out while waiting for the low latency interface'` → `0` (`AWDL_LLW_COMPANION.md` §1.1, §2.2) |
| E4.2 | `ifconfig llw0` | It exists, with live BSD index | `if_nametoindex("llw0")` ≠ 0 — the daemon's exact validation (`AWDL_LLW_COMPANION.md` §1.1) |
| E4.3 | IORegistry | Entry of the LL interface with `IO80211InterfaceRole = "LowLatency"`, `IOInterfaceName = "llw0"`, prefix `llw` | `ioreg -r -l \| grep -E 'LowLatency|llw0'` (`AWDL_LLW_COMPANION.md` §3.3; `AWDL_KERNEL_ROLE_TABLE.md` §1–§2) |
| E4.4 | Link with `awdl0` | `flushFlowQueues` of `awdl0` delegates without panic; companion id set | No panic under traffic (manual test); `?` without literal |
| E4.5 | Destruction (`op = 0`) | `llw0` disappears without panic; `awdl0` intact | `?` — no documented daemon literal for destruction (`AWDL_LLW_COMPANION.md` §3.2) |
| E4.6 | Regression: force the creation failure (gate closed for the 293) | Two errors and continuation with `awdl0` intact | `grep -E 'Failed to create the companion low latency interface'` present and `grep -cE 'Timed out while waiting for the low latency interface'` → `0` (`AWDL_LLW_COMPANION.md` §2.1–§2.3); inverse of T6 of `AWDL_STAGE1_IMPL_SPEC.md` §14 |

## 4. Dependency graph

```
        E1: awdl0 is born and wifip2pd accepts it
            (done; AWDL_STAGE1_IMPL_SPEC)
              │
        ┌─────┴──────────────────────┐
        ▼                            ▼
  E2: real TX/RX datapath      E4a: 293 → llw0 created
  (queues, pool, radio)        (Skywalk registration only,
        │                       no new radio)
        ▼                            │
  E3: synchronization + action       │
      frames + events to daemon      │
   (kernel PeerManager;              │
    AF RX/TX uses E2)                │
        │                            │
        ▼                            ▼
  E4b: real low latency datapath and its
       statistics (142/149/243)  `?`
```

- **E4a depends only on E1:** the 293 request always arrives after `awdl0` is accepted and its creation is
  pipeline, without radio (`AWDL_LLW_COMPANION.md` §1.2–§1.3). It can be implemented at any point
  after stage 1; the prototype reserved it for stage 4 (`AWDL_PROTOTYPE_DESIGN.md` §3.6).
- **E3 depends on E2:** the sync frame repetition, the TX pull and the RX delivery of action
  frames reuse the queues, the pool and the radio integration point of stage 2.
- **E4b depends on E3** (windows/synchronization for LL scheduling) **and on second-port
  concurrency** (`?`, `AWDL_RTL8814_HW_SUPPORT.md` §2.3).
- Cross-cutting: everything verified belongs to build 25G241; every version jump requires
  re-verifying tables, slots and the symbol table of the IO80211Family entry, with the existing
  per-build gate as mitigation (`AWDL_PROTOTYPE_DESIGN.md` §7; `AWDL_KEXT_LINKAGE.md` §3).

## 5. Open questions (`?`) blocking each stage

| Stage | Question | Source |
|---|---|---|
| E2 | RX integration point of the vendor core toward AirLayer (frames with A1 = AWDL MAC and data) | `AWDL_RTL8814_HW_SUPPORT.md` §6.4, §8 |
| E2 | Final queue shape (1 TX without WMM vs 4×AC with WMM) and its effect on stack traffic | `AWDL_STAGE1_QUEUE_SHAPE.md` §2–§4 |
| E2 | Behavior of the inherited slot `getRxPacketPool` (+0xD90) for a custom interface | `AWDL_PROXIMITY_DATAPATH.md` §7, §15 |
| E2 | Equivalent on the family classes of `stagePacket`/`freePendingPackets`/`flushPackets` (Broadcom-only slots) | `AWDL_PROXIMITY_DATAPATH.md` §6, §11 |
| E2 | Reserving the 64 B headroom in the vendor core RX path | `AWDL_STAGE1_PENDING_CHECKS.md` §1; `AWDL_RTL8814_HW_SUPPORT.md` §7 |
| E3 | Registration of the third-party subclass in the AWDL hierarchy (metaclass) and inherited `createPeerManager` at runtime | `AWDL_STAGE1_IMPL_SPEC.md` §15.10 |
| E3 | Exact route of the manager's internal ioctls (`0x802869C8` + type in high dword) toward the kext slots | `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.1 |
| E3 | Builders of TLV 6/8/10/11 and exact content of the TX pull | `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11 |
| E3 | UMI condition (subtype + channel) deciding which AF reaches the daemon as 48 | `AWDL_FRAME_WIRE_FORMAT.md` §2; `AWDL_OPEN_QUESTIONS_1.md` §1.1 |
| E3 | Precision of the host timers against the AWDL cadence; unit of the guard time | `AWDL_RTL8814_HW_SUPPORT.md` §3.2; `AWDL_FIRMWARE_DUTIES.md` §6.2 |
| E3 | Exact relation of the layer's own clock with `AwNumber`/`FwAwStartTime` (modulo 2^20 µs) required by `monitorTimeline` | `AWDL_OPEN_QUESTIONS_1.md` §2 |
| E3 | Emitter of event 47 on the addition of a peer (only the removal is documented) and payload of the 195 | `AWDL_EVENTS.md` §3.2 |
| E3 | Visibility of the family's `IOLog` in `log show` of 26.7.1 (affects the test log patterns) | `AWDL_STAGE1_TEST_LOG_PATTERNS.md` §1 |
| E4 | Semantics of the `code` field of the 293 | `AWDL_LLW_COMPANION.md` §1.1, §5 |
| E4 | Export of `IO80211VirtualInterface::setCompanionLowLatencySkywalkInterface` and use of the family link | `AWDL_LLW_COMPANION.md` §3.5; `AWDL_KEXT_LINKAGE.md` §1.2 (selection) |
| E4 | Real low latency datapath: port 1 on the USB path of the 8814AU | `AWDL_RTL8814_HW_SUPPORT.md` §2.3 |
| E4 | Queue shape of `llw0` (LL capacity, WMM, pool shared with `awdl0`) | `AWDL_PROXIMITY_DATAPATH.md` §4 |
| E4 | Runtime use of the daemon's companion object (ioctls `'i'` 130/131 over `llw0`) | `AWDL_LLW_COMPANION.md` §3.5 |
| E4 | Trigger of the statistics 142/149/243 on a stack without firmware | `AWDL_EVENTS.md` §3.2 |

## 6. Source correspondence

| Section | Documents it derives from |
|---|---|
| §0 | `AWDL_STAGE1_IMPL_SPEC.md`, `AWDL_STAGE1_QUEUE_SHAPE.md`, `AWDL_STAGE1_CODE_FIXES.md`, `AWDL_WIFIP2PD_POST_CREATE.md`, `AWDL_LLW_COMPANION.md` |
| §1 | `AWDL_PROTOTYPE_DESIGN.md` §6, `AWDL_PROXIMITY_DATAPATH.md`, `AWDL_STAGE1_QUEUE_SHAPE.md`, `AWDL_STAGE1_PENDING_CHECKS.md`, `AWDL_INTERFACE_BIRTH.md`, `AWDL_RTL8814_HW_SUPPORT.md`, `AWDL_STAGE1_CODE_REVIEW_HOOK.md` |
| §2 | `AWDL_PEERMANAGER_SOFTWARE.md`, `AWDL_PEERMANAGER_DRIVER_ABI.md`, `AWDL_KEXT_LINKAGE.md`, `AWDL_EVENTS.md`, `AWDL_FRAME_WIRE_FORMAT.md`, `AWDL_TLV_BODIES.md`, `AWDL_OPEN_QUESTIONS_1.md`, `AWDL_FIRMWARE_DUTIES.md`, `AWDL_RTL8814_HW_SUPPORT.md`, `AWDL_APPLE80211_DISPATCH.md`, `AWDL_STRUCTS.md`, `AWDL_PROTOTYPE_DESIGN.md` §4–§5 |
| §3 | `AWDL_LLW_COMPANION.md`, `AWDL_EVENTS.md`, `AWDL_PROXIMITY_DATAPATH.md`, `AWDL_KERNEL_ROLE_TABLE.md`, `AWDL_RTL8814_HW_SUPPORT.md`, `AWDL_FIRMWARE_DUTIES.md`, `AWDL_STAGE1_IMPL_SPEC.md` §8 |
| §4–§5 | Synthesis of all the above; global state in `HANDOFF_AWDL.md` §4 |

No conclusion of this document implies a test on RTL8814 hardware; the stages are validated in
the indicated order precisely to bound the blast radius of a failure (`AWDL_PROTOTYPE_DESIGN.md` §7).
