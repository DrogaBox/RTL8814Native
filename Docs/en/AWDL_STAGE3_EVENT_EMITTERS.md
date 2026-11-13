# Stage 3: event emitters, path from the kext and the state `wifip2pd` consumes

Closes the `?`s of stage 3 of `AWDL_ETAPAS_2_A_4.md` §2: who emits each AWDL event, by which API
it reaches the daemon, which method `RTLAwdInterface` has to call and what state `wifip2pd` reads
to consider itself synchronized and advertise a service. Base: `BootKernelExtensions.kc` (md5
`f489ff8be80e6013a1bf47561dcaef45`) and `wifip2pd.x86_64` (md5
`a120cfccfa2783303604ce5be9e1763a`),
IDA 9.4 headless over copies of the IDBs. Kernel addresses in VAs (`0xffffff800…`), daemon ones in
binary VAs (`0x100…`). Functions are cited by verified head (`ida_funcs.get_func`); call
sites are cited separately. What the disassembly does not fix goes as `?`.

## 1. Result

1. **Event 47 has no add emitter.** The only two KC sites sending id 47 are
   `IO80211PeerManager::removePeer` (`0xffffff8002254096`, site `0xffffff8002254248`) and
   `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`, site
   `0xffffff8002214d6d`), both by direct `postMessageSync`, both with the log `postPeerAbsence` and
   the same 68 B payload with `u32@8 = 1`. There is no emitter on add: a peer's presence reaches the
   daemon via event 48 (AWDL frames); the peer table is kernel-internal state.
2. **The path from the kext is a single virtual call**, `postMessage(id, data, length, false)` at
   slot `0xB18` (`IO80211VirtualInterface::postMessage`, `0xffffff8002333192`). That call enters
   `reportDataPathEvents` (slot `0xB20`), passes the controller gate
   (`IONetworkController::executeCommand`, `0xffffff80026f51e0`) and ends in
   `postMessageSync` → `postMessageIOUC` → `IO80211IOUCEventPipe::writePipe`, which is where the
   client subscription applies. There is no interface up/down check on that path.
3. **`wifip2pd` does not read the AWDL state via `apple80211` GET.** It only does `RawGet` 12 and 97
   and `RawSet` 94 and
   293; the state (sync, master, channel sequence, peers) it builds from the events 46/195/47/48
   and publishes via XPC as `WiFiP2PAWDLState`. Advertising a service (AirDrop) is triggered by the
   Bonjour registration, not by a sync check. For stage 3 the minimum is: correct monitoring
   registration (pipe + mask), 46 and 195 — which with the reused manager come out by themselves —
   and accepting the ioctls 134/129; 41/42/43 are consumed by no KC client nor `wifip2pd`.

## 2. Point 1 — emitters per id and API

### 2.1 Emitter matrix

| id | emitter (head) | site | API | payload |
|---:|---|---|---|---|
| 41 `AWDL_AVAILABILITY_WINDOW_START` | `AppleBCMWLANProximityInterface::handleAvailabilityWindowStart` `0xffffff8001718756` | `0xffffff80017188d1` | `IO80211Controller::postMessage` `0xffffff8002299fae` | ~24 B derived from the firmware event |
| 42 `…_EXTENSION_START` | `…::handleAvailabilityWindowExtension` `0xffffff8001718942` | `0xffffff80017189ac` | same | 8 B |
| 43 `…_EXTENSIONS_END` | `…::handleAvailabilityWindowEnd` `0xffffff80017184b6` | `0xffffff8001718649` | same | ≥16 B |
| 46 (firmware) | `…::handleMasterEvent` `0xffffff80017189e4`, `handleSlaveEvent` `0xffffff8001718ad6`, `handleNonElectionMasterEvent` `0xffffff8001718bd4`, `bringdownLink` `0xffffff8001719cec` | `0xffffff8001718a7e`, `0xffffff8001718b6c`, `0xffffff8001718c6e`, `0xffffff8001719d66` | same | 16 B: `u32@0` = 1 master / 2 slave / 3 non-election master / 0 down |
| 46 (family) | `IO80211VirtualInterface::postSyncStateChanged` `0xffffff8002332054` | `0xffffff8002332073` (`mov esi, 2Eh`) | direct `postMessageSync` | 0 B |
| 46 (manager) | `IO80211AWDLPeerManager::postSyncStateChanged` `0xffffff80021e58b4` | `0xffffff80021e594d` (calls the previous helper) | direct call | 0 B |
| 47 `PEER_PRESENCE` | `IO80211PeerManager::removePeer` `0xffffff8002254096` | `0xffffff8002254248` | direct `postMessageSync` | 68 B: MAC `@0..5`, `u32@8 = 1`, rest 0 |
| 47 | `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` `0xffffff8002214cf2` | `0xffffff8002214d6d` | same | identical |
| 48 `AWDL_ACTION_FRAME_RX` | `IO80211AWDLPeerManager::postAFToP2pd` `0xffffff800220fc2c` | `0xffffff800220fce2` | virtual `+0xB18` | 72 B `apple80211_act_frm_hdr` header + frame |
| 195 `AWDL_CHAN_SEQ_CHANGED` | `IO80211AWDLPeerManager::setChannelSequenceUnconditional` `0xffffff80021cb69a` | `0xffffff80021cc29b` | virtual `+0xB18` | **400 B** (buffer `IOMallocZeroData(400)`, `u32@0 = 1`, sequence list at `+12/+20`) |
| 65, 68, 142, 149, 235, 243 | `post*` helpers of `IO80211VirtualInterface` and `IO80211AWDLPeerManager` | see `AWDL_EVENTS.md` §3.2–§3.3 | `postMessageSync` or virtual `+0xB18` | fixed lengths already documented |

The ids 41, 42 and 43 have no other emitter: the Broadcom kext builds them in its firmware event
dispatcher and delivers them via `IO80211Controller::postMessage`, which is a controller path (glue),
not the interface path. On a Mac without that kext they do not exist.

### 2.2 Sweep of the 47

Method: (a) all direct `call`s to `IO80211SkywalkInterface::postMessageSync`
(`0xffffff80022f8abc`) of the KC, reading the previous 32 B for the argument register immediate —
28 sites, exactly two with `esi = 0x2F`; (b) all virtual `call`s to `+0B18h`,
`+0B20h` and `+298h` with the id in the previous 40 B — two with an AWDL-list id (48 and 195) and
none
with 47; (c) functions containing both a `mov esi/edx/r8d, 2Fh` and a virtual call of those —
none on the AWDL side (`IO80211AWDLPeerManager::monitorAWDLState` `0xffffff80021ed724`, 0x1945 B,
enters the
list for an immediate 0x2F that is not an event id and does not call `postMessage` in its whole
body).

Conclusion: the 47 is **absence**. The `u32@8 = 1` is a constant in the two emitters and does not
distinguish presence from absence; the name and the log of the emitting function (`postPeerAbsence`)
do. Presence on add travels as 48 (`postAFToP2pd`, which emits every action frame the manager
accepts and delivers to the daemon); the manager's peer table is kernel-internal state and is not
published as such. `wifip2pd` keeps its table with the switch tags 1 and 8 (`AWDL_EVENTS.md` §4.2).
The byte `@68` the daemon gate reads falls outside the 68 B payload; the daemon event buffer is
0x100 B and is reused
(`sub_100041FE0`), so that byte is not written by the kernel — semantics `?`.

### 2.3 Which API each emitter uses

| API | Who | Effect |
|---|---|---|
| `IO80211VirtualInterface::postMessage` (slot `0xB18`) | manager (`postAFToP2pd`, `setChannelSequenceUnconditional`) | path with controller gate, flag 0 → `postMessageSync` |
| `IO80211SkywalkInterface::postMessageSync` (direct) | 46 helper, 47 (two sites), 65/142/149/243 | path without a gate; the caller is already serialized by its own gate |
| `IO80211Controller::postMessage` | Broadcom kext (41/42/43 and firmware 46) | controller glue → same `postMessageSync` on the manager interface |
| `Apple80211StartMonitoringEvent` / `RawP2PEventMonitoringInit` | `wifip2pd` | creates the pipe (`extEventMonitorInit` `0xffffff800235256e`) and sets the subscription (`extSubscribeEvent` `0xffffff8002352486`); without this `writePipe` drops |

## 3. Point 2 — path from `RTLAwdInterface` to the daemon

### 3.1 Chain, with instructions

| # | Step | Head | Linking instruction |
|---:|---|---|---|
| 1 | `postMessage(id, data, length, false)` | `IO80211VirtualInterface::postMessage` `0xffffff8002333192` (slot `0xB18`; `__ZTV30AppleBCMWLANProximityInterface` `0xffffff80017f5b68` + `0x10` + `0xB18`) | `0xffffff800233334b` `call [rax+0B20h]`, with `r8 = 0` |
| 2 | `reportDataPathEvents(id, data, length, 0)` | `IO80211SkywalkInterface::reportDataPathEvents` `0xffffff80022f8dc8` (slot `0xB20`) | `0xffffff80022f8e07` `call [rax+0BA8h]` (getController); `0xffffff80022f8e2a` `call [r10+850h]` |
| 3 | controller gate | `IONetworkController::executeCommand` `0xffffff80026f51e0` (controller vtable `0xffffff8002461148` + `0x10` + `0x850`) | receives the `reportDataPathEventsGated` callback and five pointers |
| 4 | callback with the gate taken | `IO80211SkywalkInterface::reportDataPathEventsGated` `0xffffff80022f8d1a` | `0xffffff80022f8d98` `call [rax+298h]` (filter at `ivars+0x28`); `0xffffff80022f8db2` `call postMessageInternal` |
| 5 | WCL/sync fan-out | `IO80211SkywalkInterface::postMessageInternal` `0xffffff80022f8600` | `0xffffff80022f861e` `jmp routeEventToWcl` if flag 1 and glue; `0xffffff80022f862a` `call postMessageSync` if not |
| 6 | pipe delivery | `IO80211SkywalkInterface::postMessageSync` `0xffffff80022f8abc` | `0xffffff80022f8aed` `call [rax+0DE0h]` (`isIOUCPipeOpened` `0xffffff80022f6678`); `0xffffff80022f8b15` `call [r9+0DD8h]` (`postMessageIOUCE` `0xffffff80022f66a2`) |
| 7 | fan-out per client | `IO80211SkywalkInterface::postMessageIOUCE` `0xffffff80022f66a2` | walks the pipes of the BSDName and calls `writePipe` |
| 8 | filter and copy | `IO80211IOUCEventPipe::writePipe` `0xffffff80022a7e7c` | `+368` bit 0, `+416` bypass, bitmap `+417`, length ≤ 0x1000, `reserveRingEntry` and `notifyUser` |

With flag 0 (what slot `0xB18` always passes) step 5 goes straight to `postMessageSync`: the WCL
path is only taken if the caller invokes `IO80211SkywalkInterface::postMessage`
(`0xffffff80022f85f6`)
with `true`, which no AWDL emitter does.

Do not confuse the two homonymous methods: `IO80211AWDLPeerManager::reportDataPathEvents(int, void*,
ulong)` (`0xffffff80021dd8f4`) is the **kernel** consumer (it receives the ids forwarded by
`routeToP2PInterface` and feeds `monitorTimeline` `0xffffff80021e11ca`), while
`IO80211SkywalkInterface::reportDataPathEvents(int, void*, ulong, bool)` (`0xffffff80022f8dc8`,
slot
`0xB20`) is the emitter toward **userspace** using the chain of §3.1.

### 3.2 Conditions that are tested (and those that are not)

| Condition | Where | Effect if it fails |
|---|---|---|
| role 6 (`*(ivars+88) == 6`) | only in `postSyncStateChanged` `0xffffff8002332054` | does not emit 46 (no-op) |
| filter at `ivars+0x28` (method `+0x298`, class `?`) | `0xffffff80022f8d98` | if it exists and returns 0, no delivery |
| pipe open (`isIOUCPipeOpened`) | `0xffffff80022f8aed` | `postMessageSync` returns 0 without delivering |
| at least one event client for the interface | `postMessageIOUCE` | returns `0xE00002C7` |
| subscribed client (`+368` bit 0) | `writePipe` | `0xE00002D8`, log `Client not subscribe for receiving event yet` |
| id bit in the bitmap (`+417`, bit `(id-1)&7`) | `writePipe` | `0xE00002E2` if the bit is 0 and there is no `+416` bypass |
| length ≤ 0x1000 | `writePipe` | `0xE00002E1` |
| **not** checked | — | administrative state of the interface, id range, payload content |

The two points that can drop without a visible error toward the caller are the filter `ivars+0x28`
and the
absence of pipe: if the daemon did not complete the monitoring registration (or the user client did
not create the pipe),
`postMessage` returns success and nothing arrives.

### 3.3 What the kext has to do

- Call `postMessage(id, data, length, false)` through slot `0xB18` of the interface. It is a virtual
  call: it needs no exported symbol, only the class definition.
- Do it from workloop context or from an action already enqueued on the controller gate: step
  3 takes `executeCommand`, which waits for the controller gate. Calling it from the RX/USB handler
  in interrupt context may block; `?` the exact behavior of `executeCommand` in that
  context (not tested on hardware).
- For the 46, the short path is `postSyncStateChanged()` (`0xffffff8002332054`): it checks role 6,
  emits
  `0x2E` with length 0 and warns the `IO80211RangingManager`. If not depending on the helper is
  preferred,
  `postMessage(46, NULL, 0, false)` produces the same event in the pipe, without the ranging notice.
- For the 195 and the 48 no own code is needed if the manager is reused: `setChannelSequenceUnconditional`
  and `postAFToP2pd` already emit through slot `0xB18`. The 47 is also the manager's (absence).
- For 41/42/43 there is no consumer in the KC nor in `wifip2pd`; only a direct call from the driver
  to the manager's `reportDataPathEvents` would feed `monitorTimeline` (§5).

## 4. Point 3 — what state `wifip2pd` reads and what triggers the advertisement

### 4.1 apple80211 surface of the daemon

| Request | Type / length | Use |
|---|---|---|
| `RawGet` 12 `CARD_CAPABILITIES` | 12 / 21 | gate before creating the interface |
| `RawSet` 94 `VIRTUAL_IF_CREATE` | 94 / 32 | creates `awdl0` (role 6 at `+0x0C`) |
| `RawGet` 97 `VIRTUAL_IF_PARENT` | 97 / length of the name | parent name |
| `RawSet` 293 `COMPANION_SKYWALK_IF_CREATE` | 293 / 24 | creates `llw0` |
| ioctl `'i'` 130/131 | `0xC0806982` / `0xC1206983` | channel sequence; the kernel of 26.7.1 rejects them (`AWDL_WIFIP2PD_POST_CREATE.md` §2.5) |
| monitoring | `RawP2PEventMonitoringInit` (role 1) | creates the pipe and the subscription |

There is no `RawGet` of `AWDL_SYNC_STATE`, `AWDL_MASTER_CHANNEL`,
`AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`
nor a peer counter. Those values are not requested from the kext: they arrive by events. If another
client asks for them through the `apple80211get*` bridges (slots `0x1028`, `0x1020`, `0x1010`,
`0xFF8`), the coherent value is the one the manager has programmed at that moment; who asks for them
besides `wifip2pd` was not verified (`?`).

### 4.2 The state the daemon publishes

| Piece | Address | What it does |
|---|---|---|
| state client registration | `sub_10040F3A0` | stores the XPC client and, if asked, sends the initial state and the sequence |
| state builder | `sub_10040FAC0` | `WiFiP2PAWDLState initWithInterfaceName:supportsSoloMode:supportsDataTransfer:channelSequence:isEnabled:substate:macAddress:peerMasterChannel:peerPrimaryPreferredChannel:peerSecondaryPreferredChannel:` |
| XPC forwarding | `sub_100443FB0` | `updatedAWDLState:` with the builder's object |
| sync state | event 46 (tag 11 of the switch `sub_1000388D0`) | source of the `substate` the builder consumes (field-by-field link `?`) |
| channel sequence | event 195 (tag 12) and `channelSequenceChangedEvent:` | `WiFiChannel` array of the object |

The object fields come from daemon-internal state (`substate` via witness getter, `isEnabled`
of `self+760`, the sequence from its store), not from a kernel read. It is a mirror: the real
election and sequence are carried by the kernel's `IO80211AWDLPeerManager`.

### 4.3 The service advertisement

`sub_10001D2F0` (Bonjour engine start) logs `Starting advertise for %s (browses: %ld,
advertises: %ld)`. In the decompiled body, before that log the only identified early exit is
the empty service set; no sync, master or sequence check appears (the rest of the body branches were
not broken out, `?`). The AirDrop advertisement is triggered by the service registration
(Bonjour/XPC), not by a sync event. What does depend on the kernel is the manager being
elected and transmitting MIs so the service is visible on the air.

With the manager reused, the sequence is: the driver accepts ioctl 134 and reflects the role in the
radio;
the manager elects, programs the sequence (emits 195) and changes state (emits 46); `wifip2pd`
updates
its mirror and keeps advertising. Without an election there is no MI, even if the daemon is already
advertising on its side.

### 4.4 Minimums of stage 3

| Requirement | Does the reused manager provide it? | Note |
|---|---|---|
| monitoring registration with pipe and subscription | no | user client / `extEventMonitorInit` and `extSubscribeEvent` |
| event 46 after the 134/election | yes (`postSyncStateChanged`) | the driver only reflects the role in the radio |
| event 195 | yes (`setChannelSequenceUnconditional`) | 400 B payload |
| events 48 and 47 | yes (`postAFToP2pd`, `removePeer`) | the 47 is absence |
| events 41/42/43 | not applicable | no consumer on 26.7.1 |
| GET `AWDL_*` | not applicable | `wifip2pd` does not ask for them |
| ioctl 129/134 and `actionFrameInput` | no | driver obligation |

## 5. Corrections to previous documents

| Document | Previous claim | Verified |
|---|---|---|
| `AWDL_EVENTS.md` §1.1 and §3 | slot `+0xB18` is `IO80211SkywalkInterface::postMessage` | it is `IO80211VirtualInterface::postMessage` `0xffffff8002333192`; `+0xB20` is `reportDataPathEvents`, not another `postMessage` |
| `AWDL_EVENTS.md` §3.2 | payload of the 195 `?` | 400 B, `u32@0 = 1`, list at `+12/+20` |
| `AWDL_EVENTS.md` §3.2 | emitter of the 47 on add `?` | it does not exist; both emitters are for removal |
| `AWDL_ETAPAS_2_A_4.md` §2.4 item 2 | the driver must publish 41/42/43 and `monitorTimeline` validates them | `monitorTimeline` is only reached from the 41 branch of
  `IO80211AWDLPeerManager::reportDataPathEvents`; its only caller inside the KC is
  `IO80211InfraInterface::routeToP2PInterface` `0xffffff800236364e`, whose whitelist does not include
  41–43, and `wifip2pd` drops the 41. Only a direct driver call to that manager method would feed
  it; no KC client does |
| `AWDL_ETAPAS_2_A_4.md` §2.4 item 6 | the 46 is emitted «after the 134/election» | right, and the short path is `postSyncStateChanged` `0xffffff8002332054` (role 6, length 0) |

## 6. Method and limits

- IDA 9.4 headless over copies in `/tmp/ida_awdl` of the KC IDB and of
  `Build/ida_awdl/wifip2pd.i64`; the source IDBs were not opened for writing.
- Heads verified with `ida_funcs.get_func` (head = cited address) for all the functions of
  tables §2.1 and §3.1; call sites were read from the disassembly and the pseudocode.
- The sweep of the 47 covers direct calls to `postMessageSync`, virtual calls to
  `+0B18/+0B20/+298`
  and search of the immediate `0x2F` in candidate functions. It does not cover an emitter loading id
  47
  from a variable outside those patterns; none was found in the AWDL range.
- `?` left: exact semantics of the `u32@8` of the 47 payload and of the byte `@68` the daemon reads;
  behavior of `executeCommand` when called in interrupt context; consumption of the GET
  `AWDL_*` by clients other than `wifip2pd` (airportd, wifivelocityd) — not verified here; and
  everything depending on a real election with an Apple peer (frames, sequence, MIs).
