# Index of the AWDL documents

Inventory of the 128 documents `Docs/AWDL_*.md` (127 plus this index) as of 2026-11-06, grouped
by topic, with the one-line summary and the status of each; `AIRLAYER_PORT_14_15.md` has no
`AWDL_` prefix and is listed all the same. Status base: the title, the first paragraph and the
closing or open-points sections of each document, plus `HANDOFF_AWDL.md` (repo root,
section 4, which collects the deduplicated `?`s). Groups 3, 4 and 5 are split
into subgroups. The Spanish original is [../AWDL_INDICE.md](../AWDL_INDICE.md).

Status convention:

- **Closed and verified** — the key claims are checked in the disassembly or in the
  cited binary; no blocking point remains for the use of the document.
- **Closed with `?`** — verified, with residual field or detail questions noted in
  the document itself; the repo convention is to mark with `?` everything not seen in the sources.
- **Superseded by X** — the document keeps partial value, but another document closes its gap or
  corrects its numbers; which one is cited.

## 1. Daemon behavior (`wifip2pd`)

| Document | Content | Status |
|---|---|---|
| [AWDL_WIFIP2PD_REQUESTS.md](AWDL_WIFIP2PD_REQUESTS.md) | Apple80211 surface of the daemon: the four types it emits when creating `awdl0` (12, 94, 97, 293) and the format of the request | Closed and verified; the issuers of the rest of the types are closed by [AWDL_REQUEST_ISSUERS.md](AWDL_REQUEST_ISSUERS.md) and [AWDL_APPLE80211_DISPATCH.md](AWDL_APPLE80211_DISPATCH.md) |
| [AWDL_WIFIP2PD_DISCOVERY.md](AWDL_WIFIP2PD_DISCOVERY.md) | The 30 s wait after the 94 is not released by a driver event, but by the IORegistry discoverer | Closed and verified |
| [AWDL_WIFIP2PD_POST_CREATE.md](AWDL_WIFIP2PD_POST_CREATE.md) | What releases the semaphore (`IOServiceMatched`) and the ordered list of the whole post-create sequence | Closed and verified |
| [AWDL_WIFIP2PD_IOCTLS.md](AWDL_WIFIP2PD_IOCTLS.md) | The six `ioctl` sites of the daemon and the nine constants of its request table | Closed and verified; the operation of the unnamed ioctls 130/131 is closed by [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) |
| [AWDL_WIFIP2PD_CALLERS.md](AWDL_WIFIP2PD_CALLERS.md) | Caller chain of each ioctl site up to a named Swift function, a log or a constant | Closed and verified; `?` in the witness thunks (indirect dispatch) |
| [AWDL_WIFIP2PD_STRUCTS_SEEN.md](AWDL_WIFIP2PD_STRUCTS_SEEN.md) | Structures the daemon assembles in memory before each ioctl or RawGet/RawSet, offset by offset | Closed and verified |
| [AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md](AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md) | Life cycle of the `AWDLInterface` object: discovery, creation, configuration and absence of destruction | Closed and verified; the segments it left as `?` are closed by [AWDL_WIFIP2PD_POST_CREATE.md](AWDL_WIFIP2PD_POST_CREATE.md) and [AWDL_LLW_COMPANION.md](AWDL_LLW_COMPANION.md) |
| [AWDL_WIFIP2PD_P2P_EVENTS.md](AWDL_WIFIP2PD_P2P_EVENTS.md) | Record of the P2P event monitoring and the 16 infrastructure ids | Closed and verified; its open point (where the id switch is) is closed by [AWDL_EVENTS.md](AWDL_EVENTS.md) |
| [AWDL_WIFIP2PD_LOGSTRINGS.md](AWDL_WIFIP2PD_LOGSTRINGS.md) | Inventory of AWDL log strings of the daemon, grouped by topic | Closed as inventory; consuming functions with `?` |
| [AWDL_WIFIP2PD_PRESENCE_ONCE.md](AWDL_WIFIP2PD_PRESENCE_ONCE.md) | The "presence published" Bool of `AWDLPeer` (one publication per peer) and the byte `DataPathState+64` (`unicastOptions: UMIOptions?`), with names taken from the Swift reflection field descriptors; implications for the driver | Verified in IDA on `wifip2pd` of 25G241; rawValue↔case mapping of `UMIOptions`, cleanups of the interface recovery and origin of the aged-out event `?`; closes the two points of [AWDL_WIFIP2PD_PRESENCE_TRIGGER.md](AWDL_WIFIP2PD_PRESENCE_TRIGGER.md) §7 |

## 2. Kernel surface: dispatch, vtables, structs and life cycle

| Document | Content | Status |
|---|---|---|
| [AWDL_ROLE6_RE.md](AWDL_ROLE6_RE.md) | First analysis of role 6: what `AppleBCMWLANCore` creates and what IO80211Family requires | Closed and verified; foundation of the later dispatch and birth documents |
| [AWDL_KERNEL_ROLE_TABLE.md](AWDL_KERNEL_ROLE_TABLE.md) | Role↔name tables and how role 6 publishes `IO80211VirtualInterfaceRole = AirLink` | Closed and verified |
| [AWDL_PROXIMITY_VTABLE_HIERARCHY.md](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) | Real inheritance `AppleBCMWLANProximityInterface` ↔ `IO80211AWDLProtocol` ↔ `IO80211VirtualInterface` and the qualified call to the base member | Closed and verified; the override counts are corrected by [AWDL_PROXIMITY_OVERRIDES.md](AWDL_PROXIMITY_OVERRIDES.md) |
| [AWDL_PROXIMITY_OVERRIDES.md](AWDL_PROXIMITY_OVERRIDES.md) | Complete diff of the 572/569 vtables without sampling and availability of the symbols a subclass needs | Closed and verified; replaces the numbers of the hierarchy document |
| [AWDL_VTABLE_SLOTS.md](AWDL_VTABLE_SLOTS.md) | Slot-by-slot inventory of `IO80211VirtualInterface` and `IO80211AWDLProtocol` from the saved dumps | Closed in its scope; the Proximity column it left as `?` is completed by [AWDL_PURE_SLOTS.md](AWDL_PURE_SLOTS.md) and [AWDL_PROXIMITY_OVERRIDES.md](AWDL_PROXIMITY_OVERRIDES.md) |
| [AWDL_PURE_SLOTS.md](AWDL_PURE_SLOTS.md) | The 64 pure slots (490 and 507–569) resolved with the Broadcom vtable | Closed and verified |
| [AWDL_IOCTL_SLOT_MAP.md](AWDL_IOCTL_SLOT_MAP.md) | Map of the 63 pure slots to the `apple80211get*/set*` bridges that invoke them | Closed and verified |
| [AWDL_APPLE80211_DISPATCH.md](AWDL_APPLE80211_DISPATCH.md) | The four dispatch routes and the 43 types that reach the AWDL surface (+0xFC8..+0x11C0) | Closed and verified |
| [AWDL_APPLE80211_STRUCTS.md](AWDL_APPLE80211_STRUCTS.md) | Names and parameter types of the AWDL signatures, without layouts | Superseded by [AWDL_STRUCTS.md](AWDL_STRUCTS.md), which closes the layouts field by field |
| [AWDL_STRUCTS.md](AWDL_STRUCTS.md) | Complete C layouts of all the apple80211 payloads an AWDL interface must answer | Closed and verified; fields without observed write in `?` |
| [AWDL_INTERFACE_BIRTH.md](AWDL_INTERFACE_BIRTH.md) | From the handler of the 94 to a published `awdl0`: BSD name, unit, ifnet, properties and `registerService` order | Closed and verified |
| [AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) | Queues, pools, TX/RX and `getBSDNamePrefix` of role 6 on the Broadcom path | Closed and verified; the fine reconstruction of registration carries occasional `?` |
| [AWDL_LLW_COMPANION.md](AWDL_LLW_COMPANION.md) | Companion `llw0` (type 293): emission, consequences of failure and verdict: dispensable in stage 1 | Closed and verified for stage 1; the `code` field of the payload remains `?` (table §3) |
| [AWDL_EVENTS.md](AWDL_EVENTS.md) | Kernel→userspace events over `awdl0`, delivery channels and what the daemon consumes | Closed and verified |
| [AWDL_STAGE3_EVENT_EMITTERS.md](AWDL_STAGE3_EVENT_EMITTERS.md) | Emitters of each AWDL event, path from the kext to the daemon (`postMessage` slot `0xB18`), state consumed by `wifip2pd` and minimums of stage 3 | Function heads and sites of the 46/47 verified in IDA; semantics of the payload of the 47 and consumption by other clients `?` |
| [AWDL_STAGE4_LLW_CLOSURE.md](AWDL_STAGE4_LLW_CLOSURE.md) | Closure of the open points of stage 4: ioctls 130/131 (BSD socket, not apple80211), absence of `op = 0` in `wifip2pd`, destruction path in the kernel and minimal `RTLLlwInterface` class | Function heads, paths of the 293 and of destruction, constants of the LL `processBSDCommand` and `ioctl` sites of the daemon verified in IDA; bridge of the 131 up to `processBSDCommand` and error capture by the daemon `?` |
| [AWDL_STAGE2B_MBUF_TO_PACKET.md](AWDL_STAGE2B_MBUF_TO_PACKET.md) | Stage 2b: from the mbuf of the project's own `if_output` to the packet of `awdl0`; the submission queue does not accept driver packets and the supported pattern is the direct call to the action (like the Broadcom multicast) | Function heads, `p2pMulticastTx`, family `if_output`, `legacyDequeue`, queue vtable and codes `0xE00002DB`/`cmp [r9+3Ch],2` verified in IDA; kernel mbuf→packet copy, writer of bit `+0xCC` and errno in `dlil_output` `?` |
| [AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md) | Hardware result of stage 3: chain of internal ioctls of `configure`, `interfaceStateChange` and `enableAWDL`, which ones are fatal (116 corrected), what remains unimplemented and why events 41/42/43 are missing | Observed on the machine (kernel and `wifip2pd` log) and code read in IDA; real windows, peers and TX/RX untested `?` |
| [AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md) | Status point at 2026-10-30: milestones validated on hardware, work stopped for lack of an Apple peer, why there is no substitute and order of tests when there is a device | Superseded by [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md): the block for lack of a peer was resolved with the virtual peer (2026-11-03); the document keeps the status point of 2026-10-30 and the list of validated milestones |
| [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) | External AWDL projects (owly, ESP32Drop, winawdl, omdrop-awdl, OWL, opendrop): AWDL peer without Apple with an ESP32-S3, contrast with captures of iOS 26 (field names of TLV 4, data header on the air), fixed radio and Realtek hardware lessons; review of 2026-11-08 (owly rules, AWDL in `brcmfmac`, CISPA Airfuzz artifact, OCLP fork) | Measured over the owly captures; what was not tested on hardware with `?` |
| [AWDL_REQUEST_ISSUERS.md](AWDL_REQUEST_ISSUERS.md) | Who emits each AWDL request: the kernel `IO80211AWDLPeerManager` and `IO80211.framework`; airportd the rest | Closed and verified |
| [AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md) | Which action frames reach the daemon as event 48: the gate is `ctx+0x2C == 0` and `peerReachability == 1` at the end of `actionFrameProcessInput`, not the subtype or the channel; what is consumed silently, dedup and rate limit, implications for the driver | Verified in IDA (25G241); closes question E3 of [AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md) §5; counter block of `actionFrameReport`, bit 0 of `priv+88` and the second `ud1` of the thread `?` |
| [AWDL_REF_SIMPLEBCM.md](AWDL_REF_SIMPLEBCM.md) | SimpleBCM (AirPortBrcmNIC on Sequoia/Tahoe without the system IOSkywalkFamily, commit `624fca1`): own user client, struct translation, how `awdl0` comes up and answers for this project | Code reading only (nothing compiled or run); whatever depends on Apple binaries absent from the repository `?` |

## 3. On-the-air protocol, PeerManager, firmware and RTL8814 hardware

### 3.1 Protocol, PeerManager, presence and hardware

| Document | Content | Status |
|---|---|---|
| [AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) | AWDL frame on the air as the kernel parses it: headers, validations and TLVs | Closed and verified; `?` on who strips the 802.11 header and the FCS |
| [AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) | Bodies of the sync params TLV (type 4) and channel sequence TLV (type 18) | Closed and verified; extended by [AWDL_OPEN_QUESTIONS_1.md](AWDL_OPEN_QUESTIONS_1.md); TLV 18 is corrected in two points by [AWDL_TLV18_OPCLASS_PARSER.md](AWDL_TLV18_OPCLASS_PARSER.md) |
| [AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) | Who decides the channel sequence (strategy, state, table of 56×16 slot types), how it goes down to the driver (129, 132/205, 136, 118) and why the driver composes TLVs 4 and 18: a fixed radio can announce channel 0 in the slots it does not serve | Verified; semantics of several channel sources with `?`; two points are corrected by [AWDL_SECUENCIA_CON_INFRA.md](AWDL_SECUENCIA_CON_INFRA.md) §11 |
| [AWDL_WINDOW_TIMER.md](AWDL_WINDOW_TIMER.md) | Slot clock on the host for a radio without AWDL firmware: units (AW 16 TU, slot 64 TU, µs), free-running clock as master, alignment to the master with ioctl 229 and the received TLVs 4, events 41/43 only in served slots, TX guards | Specification over already-verified facts; synchronization with a master was measured afterwards with the virtual peer (maximum offset 1 TU for minutes, [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9.2); precision without a real peer `?` |
| [AWDL_PSF_MIF_EMISSION.md](AWDL_PSF_MIF_EMISSION.md) | PSF and MIF emission contract without firmware: templates 151 (PSF), 149 (MIF/UMI now), 225 (permanent MIF per AW) and 342, on-the-air order with TLVs 4, 5 and 18 of the driver in front, period 110 via 116/418, 802.11 header and list of what the layer builds per frame | Verified; unit of the period and rate with `?`; the election TLV 5 is closed by [AWDL_TLV5_ELECTION.md](AWDL_TLV5_ELECTION.md) |
| [AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) | Source of every field of the TLVs 7 (HT), 17 (VHT), 21 (version `0xA0`, class 1) and 12 the peer manager emits, what the layer answers today (TLVs 7 and 17 do not come out) and which bits to keep at 20 MHz if GETs 154/214 are implemented | Verified; writers of some fields of TLV 12 with `?` |
| [AWDL_OPEN_QUESTIONS_1.md](AWDL_OPEN_QUESTIONS_1.md) | Election TLV v2 (type 24), `monitorPeersTimeline` and the still-unnamed bytes | Closed and verified; the canonical protocol names are not verifiable from the binary |
| [AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md) | What `IO80211AWDLPeerManager` computes in software and what it demands from the driver beneath it | Closed and verified |
| [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) | Binary contract driver↔PeerManager: AF injection, events 41/42/43, internal ioctls and TX pull | Closed with `?` in its core; builders of TLVs 6/8/10/11 and exact content of the unnamed TX pull (stage 2, table §3) |
| [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) | What Broadcom delegates to the firmware for role 6: iovars, commands and event subscription | Closed and verified as inventory; unit of the guard time and fine payload maps in `?` |
| [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) | What the RTL8814AU offers for each firmware obligation: feasible / partial / missing | Closed as static inventory; action frame TX and RX, port 1 and the clock were measured afterwards with the virtual peer ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9); the rest of the classification untested `?` |
| [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) | Reception and ACK for `awdl0`: real addresses of the AWDL frames (A1 broadcast, A3 `00:25:00:FF:94:73`), where the software discards them today, port 1 ad-hoc with the MAC of `awdl0`, `RCR_CBSSID_DATA` and diversion point at `validate_recv_frame` | Measured in code, dump and captures; MAC behavior per port with `?` |
| [AWDL_RTL8814_TX_RECIPE.md](AWDL_RTL8814_TX_RECIPE.md) | How to emit PSF/MIF and data of `awdl0` with the driver: injection pattern of the vendor, intact addresses, rate (VHT MCS9 at 20 MHz of the helper is not valid), retry limit 6/12 and ACK with port 1 | Read from the code; rates and latency untested `?` |
| [AWDL_DATA_ENCAP.md](AWDL_DATA_ENCAP.md) | Format of the packets of `awdl0`: Ethernet II in the TX queue and in the RxCompletionQueue; Broadcom inserts and removes the AWDL SNAP on the host; multicast stash in the manager; what the driver builds and removes | Verified; semantics of the AC nibble of the sequence with `?` |
| [AWDL_BLOCK_ACK.md](AWDL_BLOCK_ACK.md) | Block Ack on `awdl0`: the family does not do it (`BLOCK_ACK` handlers are stubs), Broadcom leaves it to the firmware via `PEER_CACHE_CONTROL` → `awdl_peer_op`; the layer must answer ADDBA, process DELBA/BAR and reorder | Verified; exact selectors with `?` |
| [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) | Absence-only event 47 (`u32@8 = 1`), ioctl 415 that releases the PPP queue of the peer, RX path `inputPacket` → `IO80211AWDLPeer::skywalkInputPacket`, slots +0xF30/+0xF38 the driver must implement and conditions of `setPeerRoutable` | Emitters, payload, queues and slots verified in IDA (25G241); RSSI thresholds (−45/−58 for peers only on 2.4 GHz) and ioctl 135 to the driver; emitters of the 415 in `wifip2pd` located; the trigger in the daemon is closed by [AWDL_WIFIP2PD_PRESENCE_TRIGGER.md](AWDL_WIFIP2PD_PRESENCE_TRIGGER.md) and the prerequisites and thread of the slots by [AWDL_PRESENCE_PREREQS.md](AWDL_PRESENCE_PREREQS.md) |
| [AWDL_WIFIP2PD_PRESENCE_TRIGGER.md](AWDL_WIFIP2PD_PRESENCE_TRIGGER.md) | What the ioctl 415 triggers in `wifip2pd`: the first action frame of each peer (event 48), plus the established datapath, the datapath response and the traffic registration; absence by datapath closing, registration deletion or expiry | Calls, conditions (`0x100305aa7`, peer Bool in `sub_10041C470`), chains and callers of presence and absence verified in IDA; the names of the Bools and the byte `DataPathState+64`, which it left as `?`, are closed by [AWDL_WIFIP2PD_PRESENCE_ONCE.md](AWDL_WIFIP2PD_PRESENCE_ONCE.md) |
| [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) | Creation and publication of the `IO80211AWDLPeerManager` for an external-driver `awdl0`: inherited factory of `0xB08` called from `IO80211SkywalkInterface::start`, pointer at `*(ivars+0x120)+0x28`, read with `getPeerManager`, `getP2PSkywalkPeerMgr` always 0, base `createVirtualInterface` only for role 7 | Call sites, stores, getters and bridges (45/19) verified in IDA; the release of the manager is closed by [AWDL_PEERMANAGER_TEARDOWN.md](AWDL_PEERMANAGER_TEARDOWN.md) and the ivar offsets on 14/15 are ported by [AWDL_PRESENCE_OFFSETS_14_15.md](AWDL_PRESENCE_OFFSETS_14_15.md) |
| [AWDL_IMPLEMENTATION_PLAN.md](AWDL_IMPLEMENTATION_PLAN.md) | Implementation plan in 9 steps with its own boot-arg each: port 1, RX diversion, AF injection, template store, clock and events 41/43, PSF/MIF, data (with `inputPacket` and the PPP queue), multicast and Block Ack; dependency graph and order of tests without an Apple peer | Code and document citations verified over the working tree; 4893 AF and 56 ADDBA of the captures recounted; signatures of the new platform functions `?` |
| [AIRLAYER_PORT_14_15.md](AIRLAYER_PORT_14_15.md) | Port of the layer to 14.8.9 (23J631) and 15.8.2 (24H232): same instance sizes between 14 and 15 (+0x10 on Tahoe from `IOSkywalkNetworkInterface`), but different vtables in 6 of the 9 classes, so one header and one binary per build are needed; slots of each method the layer declares, signature changes and dumps in `Docs/re/io80211_23J631/` and `Docs/re/io80211_24H232/` | Cell counts of the 9 classes reproduced with an independent dump; slots of `start`/`stop`/`inputPacket`/`sendPendingPacketsToStack`/`freePendingPackets`/`init` and writes of `init` checked in IDA; runtime behavior `?` |
| [AWDL_VERSION_SENSITIVE_OFFSETS_14_15.md](AWDL_VERSION_SENSITIVE_OFFSETS_14_15.md) | Table of version-sensitive offsets ported to 23J631 and 24H232 (`Tools/awdl_offsets_23J631.tsv`, `Tools/awdl_offsets_24H232.tsv`): `RegistrationInfo` and `ifnet_init_eparams` intact; output block and startup thread of the `ifnet` resettled in each version; slots `0x8E0`/`0x8E8` to `0x8D0`/`0x8D8` only on 14 | Mechanical checker 58/61 and 60/61 OK with the absences explained; `assert_wait(ifp+0x2A0)` and slot `0x9A8` of `getTxDataOffset` on 23J631 checked in IDA; successor of the netem queue `+0x900` `?` |
| [AWDL_PRESENCE_PREREQS.md](AWDL_PRESENCE_PREREQS.md) | What activates `LLAValid` (`PM+65308`), who writes `AWDLOn` (`PM+16096`) and in which thread, workloop and locks `monitorPeer` and the ioctl 415 run when they call the PPP slots; summary for the RTL8814 AWDL interface | Verified in IDA (25G241); closes the three pending items of [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §6, and the names of the ivars of §1.5 are closed by [AWDL_CHANNEL_STRATEGY.md](AWDL_CHANNEL_STRATEGY.md); emitters of sources 0, 1, 8 and 12 of `StateChangedSource`, lock of each caller of `removeAllPeers` and bit 5 of `PM+16096` `?` |
| [AWDL_PEERMANAGER_TEARDOWN.md](AWDL_PEERMANAGER_TEARDOWN.md) | Teardown of the `IO80211AWDLPeerManager` of an external-driver `awdl0` (role 6): what the manager frees or cancels, order relative to the interface `stop` and the BSD deregistration, paths that call the driver and locks; teardown recipe for the RTL8814 and compatibility across builds | Verified in IDA (25G241); closes the `?` of [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) §8 (who frees `ivars+0x28`); sweep of retainers, `removeEventSource` barrier, seat `+0x190`, `P2PSupervisor` singleton, ageout↔`removeFlushedPeers` serialization and destination of the `flushQueues` packets `?` |
| [AWDL_TLV5_ELECTION.md](AWDL_TLV5_ELECTION.md) | TLV 5 (election v1): on-the-air body, the kernel parser `parseAwdlElectionParamsV1`, field-by-field mapping to the payload of ioctl 155 (36 B) and the capture matrix | Verified in IDA and against captures; closes the `?` of [AWDL_PSF_MIF_EMISSION.md](AWDL_PSF_MIF_EMISSION.md) §5.2; owly's rule "ignore the 5 if the 24 arrives" and origin of the 525 and 65 metrics of the captured masters `?` |
| [AWDL_TLV18_OPCLASS_PARSER.md](AWDL_TLV18_OPCLASS_PARSER.md) | Closure of H8: the kernel `convertOpClassToChannel`, call chain from the TLV 18 parser, effect of a conversion failure, encodings 0/1/3 and acceptance of a peer that advertises only some served slots; consequences for the layer TX | Verified in IDA (25G241); corrects [AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) in two points and closes H8 of [AWDL_TX_FRAME_REVIEW.md](AWDL_TX_FRAME_REVIEW.md); own pending items in its §7 `?` |
| [AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md) | Frames of `RTLAwdInterface` itself (`buildAf`, `putChanSeq`, `queueData`) against the owly iOS 26 captures (4893 action frames, 4424 data frames): TLV 4 (length 73), TLV 5 and 18, data frames | Field-by-field comparison over captures; the real template the manager delivers and the MI template are dumped to `RTLAwdlLog` and still have to be seen on hardware `?` |

### 3.2 Channel sequence, card capabilities and scanning

| Document | Content | Status |
|---|---|---|
| [AWDL_CHANNEL_STRATEGY.md](AWDL_CHANNEL_STRATEGY.md) | The four fields that govern `ivars+4` (channel sequence strategy of the peer manager), conditions of `monitorAWDLState` before returning to `AwdlFollowsInfra`, the "Transitioning to Solo mode" branch of `interfaceStateChange`, userland requests that touch the fields and auxiliary tables | Verified in IDA (25G241); gate `ivars+0x4008 == 8` of `setChannelSequenceStrategy`, MI masks, emission of `AWDL_CTL` (162) and `AWDL_SOCIAL_TIME_SLOTS` (163) on Apple hardware and `setAwdlAutoMode` without xrefs `?` |
| [AWDL_SECUENCIA_129_EMISOR.md](AWDL_SECUENCIA_129_EMISOR.md) | Who emits the 129 on `awdl0`, how the 16 slots are built and which method sits in slot `+0xB88` of the `+0x560` object that `interfaceStateChange` queries | Verified in IDA (25G241); mapping between the internal command `0x81802869C8` and the 129, userspace client that does `RawSet` 129, sequence with a peer and flags 8 and `0xA` `?` |
| [AWDL_SIN_ASOCIACION_INFRA.md](AWDL_SIN_ASOCIACION_INFRA.md) | What the peer manager does with an unassociated primary interface: paths that turn AWDL off, channel sequence without an infrastructure channel, queries on the primary and what `wifip2pd` does in that state; consequences for the prototype | Verified in IDA (25G241); the method of `+0xB88` and the emitter of the 129 without infrastructure are answered by [AWDL_SECUENCIA_129_EMISOR.md](AWDL_SECUENCIA_129_EMISOR.md); auxiliary predicate of `monitorAWDLState` and `wifip2pd` requests on the primary `?` |
| [AWDL_SECUENCIA_CON_INFRA.md](AWDL_SECUENCIA_CON_INFRA.md) | The 16-slot sequence (ioctl 129) with associated infrastructure, case `en2` on 153 (80 MHz, center 155): which channel the peer manager learns and from which request, how it mixes it with the social channels, what changes with a peer, capability bits that alter the result and what a single-radio driver must do | Verified in IDA (25G241); corrects two points of [AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) and [AWDL_CHANNEL_STRATEGY.md](AWDL_CHANNEL_STRATEGY.md) (§11); effect of `+0xB3F` (bit 6, 6E) and `+0xB47` (bit `0x20`), IOUC type of `getDTIMInt` and services `wifip2pd` registers `?` |
| [AWDL_SECUENCIAS_OBSERVADAS.md](AWDL_SECUENCIAS_OBSERVADAS.md) | Explanation of the four sequences (type 129, 16 slots) recorded on real hardware when opening the AirDrop window in Finder: peer manager state, row of `__ZL20_channelSeqSlotTypes`, transitions between S1–S4, driver obligations per slot type and verdict on S4 | Verified in IDA (25G241), with the VAs in §8; open points in its §7 `?` |
| [AWDL_SCAN_COEXISTENCIA.md](AWDL_SCAN_COEXISTENCIA.md) | What limits the infrastructure scans of airportd/WCL with `awdl0` active, what a third-party controller receives, fields that allow returning to the AWDL channel between channels and recommendation for a single-channel USB driver | Verified in IDA (25G241), with addresses checked as function starts; real-time mode ivars, BSSID filter, driver *piggyback scan*, event channel 125/126 and use of type 446 `?` |
| [AWDL_CARD_CAPS_RESTANTES.md](AWDL_CARD_CAPS_RESTANTES.md) | The card capabilities block (`*(ctrl+0x120)+0xB34..+0xB4B`, GET 12): reader of each byte and bit in the 25G241 kernel collection, userspace gates and what `RTLAirController::getCARD_CAPABILITIES` lacks | Verified in IDA (25G241); corrections to earlier documents in §7; semantics of `isAirPlaySinkEnabled`, bits of `p[6]`/`p[8]`/`p[9]` without reader and field `+0xB50` `?` |
| [AWDL_CAP29_DOBLE_BANDA_RIESGOS.md](AWDL_CAP29_DOBLE_BANDA_RIESGOS.md) | Capability 29 (dual band, `p[7] \|= 0x20`): what else reads it before enabling it (kernel and userspace) and the hardware result with build `3699d28e` | Verified and measured on hardware (2026-10-31): bit 29 was the gate of the 5 GHz social channel and the risk of side effects did not materialize on the infrastructure |
| [AWDL_CAP78_CONCURRENCIA.md](AWDL_CAP78_CONCURRENCIA.md) | Capability 78 ("Concurrency supported", `state+0xD066`): it is SoftAP plus AWDL, not infrastructure; the layer must leave it clear, also with `rtlawdl_mcc` | Verified in IDA (25G241); `state+0x103A0`, `state+0x102F1` and `sub+0x6FC` `?` |

### 3.3 Time sharing and RTL8814A firmware

| Document | Content | Status |
|---|---|---|
| [AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) | Sharing the radio between an infrastructure BSS off the social channels and AWDL on 149: mechanism of the vendor tree (linked scan back-op), switch cost, risks, per-slot proposal (`rtlawdl_mcc`) and measurements with the virtual peer (§8.1 adaptive stay, §8.2 slots during the scan back-op, §8.3 data windows) | Source analysis (§1–§7) and measurements with the virtual peer (§8); with a real peer, cost of a peer that loses frames, speeds above 12 Mb/s and which sequence AirDrop produces `?` |
| [AWDL_FW_8814A_MCC.md](AWDL_FW_8814A_MCC.md) | The MCC H2Cs in the RTL8814A firmware (v33.6): none of the seven (0x10, 0x15–0x1A) nor the C2H 0x17 exist, so the firmware MCC route is ruled out with this blob; what the firmware offers that does matter for AWDL | Closed by static inspection of the blob (no hardware); answers the `?` of [AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) §2 |
| [AWDL_FW_8814A_CHSW_OFFLOAD.md](AWDL_FW_8814A_CHSW_OFFLOAD.md) | The firmware channel switch offload (H2C `CHNL_SWITCH_OPER_OFFLOAD` 0x1C and WoWLAN 0x87): argument layout, registers and RF it writes, C2H 0x10, wait loops, comparison with `PHY_HandleSwChnlAndSetBW8814A` over USB, use of the 0x1C to move the primary 153↔149 and what to measure | Verified by static inspection; closes the line left open in [AWDL_FW_8814A_MCC.md](AWDL_FW_8814A_MCC.md) §8; real latency unmeasured `?` |

## 4. Specification, implementation and reviews

### 4.1 Stage 1

| Document | Content | Status |
|---|---|---|
| [AWDL_PROTOTYPE_DESIGN.md](AWDL_PROTOTYPE_DESIGN.md) | Stage-by-stage plan (1–4) turned from hypothesis to plan over verified findings | Closed as plan; the risks of §7 are closed by the later documents except those of stages 2 and 3 (table §3 of this index) |
| [AWDL_STAGE1_IMPL_SPEC.md](AWDL_STAGE1_IMPL_SPEC.md) | Stage 1 specification: capability gate, override of the 94, class of `awdl0`, rollback and tests T1–T7 | Closed and updated with the delta; executed afterwards on hardware (`awdl0` created and published, [AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md) §1) |
| [AWDL_STAGE1_SPEC_DELTA.md](AWDL_STAGE1_SPEC_DELTA.md) | Consistency review of the spec against ten documents and the code; finds the blocking defect of the null action | Closed and verified; its edits are applied to the spec |
| [AWDL_STAGE1_CODE_FIXES.md](AWDL_STAGE1_CODE_FIXES.md) | Before/after of the six defects of the spec, verified in the disassembly | Closed and verified; defects 1, 2, 3 and 6 applied to the code, 4 discarded as harmless, 5 without change |
| [AWDL_STAGE1_CODE_REVIEW_IFACE.md](AWDL_STAGE1_CODE_REVIEW_IFACE.md) | Line-by-line review of `AWDLInterface.{h,cpp}` against the spec and CODE_FIXES | Closed and verified; the findings were applied, closed by [AWDL_STAGE1_PENDING_CHECKS.md](AWDL_STAGE1_PENDING_CHECKS.md) or discarded, and the order of the vtable closed by dump (0 discrepancies) |
| [AWDL_STAGE1_CODE_REVIEW_HOOK.md](AWDL_STAGE1_CODE_REVIEW_HOOK.md) | Review of the hook applied in `RTLAirLayer` (findings A1–A10) | Closed as review; A6 and A7 remain for tests T2.2 and T3, and A10 is still `?` |
| [AWDL_STAGE1_OPEN_POINTS.md](AWDL_STAGE1_OPEN_POINTS.md) | Closes three points of spec §15: cast of the 94, fall without WCL and parent of the 97 | Closed and verified in essence; §4 leaves two points (table §3 of this index) |
| [AWDL_STAGE1_PENDING_CHECKS.md](AWDL_STAGE1_PENDING_CHECKS.md) | Closes four points: reader of `RegInfo+0x3C`, `vi_free` half-initialized, optional dereg and `checkForWork` | Closed and verified in essence; seven fine points of its own (table §3) |
| [AWDL_STAGE1_STUB_VALUES.md](AWDL_STAGE1_STUB_VALUES.md) | The post-create sequence touches no pure slot; recommended stub request by request | Closed and verified; corrected by [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) (row 7) and [AWDL_STAGE1_VIRTUAL_IF_PARENT.md](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) (row 4) |
| [AWDL_STAGE1_QUEUE_SHAPE.md](AWDL_STAGE1_QUEUE_SHAPE.md) | 1 TX submission + 1 TX completion + 1 RX completion suffices; only the WMM bit demands 4 queues | Closed and verified; its order `?` is closed by [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md) |
| [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md) | `setInterface` happens inside the registration, before the `ifnet`; the 22 of the validator does not reach the driver | Closed and verified |
| [AWDL_STAGE1_TXPUSH_LLINK.md](AWDL_STAGE1_TXPUSH_LLINK.md) | There is no jump from the `ifclassq` to `tx_push`; the startup thread runs `_ifp_if_start`, which purges; the first ND/RS only reaches `txNoop` if the family hits the queue after the enqueuing (to decide at T3.5) | Closed and verified; `[ifp+0x900]` and the final step `?` on hardware |
| [AWDL_STAGE1_STACK_TX_ENTRY.md](AWDL_STAGE1_STACK_TX_ENTRY.md) | The stack enters via `if_output` = `_dlil_output_handler` → `_ifnet_enqueue` → `ifclassq`; nothing calls `packetSubmission` afterwards, so the ND does not reach `txNoop` and stage 2 must push the queue | Closed and verified; `ifp+0x78` and the order with the startup thread `?` on hardware |
| [AWDL_STAGE1_STAGE2_PUSH_DESIGN.md](AWDL_STAGE1_STAGE2_PUSH_DESIGN.md) | `ifp+0x78` (`if_refflags`) is 1 and `if_qflush` purges; the order with the startup thread is indeterminate; the design path is replacing the `output` of the `ifnet` with `ifnet_set_output_handler`, which avoids the `ifclassq` | Verified; symbol linkage, order and ring `?` on hardware |
| [AWDL_STAGE1_OUTPUT_HANDLER_LINK.md](AWDL_STAGE1_OUTPUT_HANDLER_LINK.md) | The four `*_output_handler` are in no KPI list: they do not link from a kext; the project's own `output` enters via `params+0x40` in `initBSDInterfaceParameters` (slot `0x8E0`) or via `cmpxchg` at `ifp+0x258` from `prepareBSDInterface` (slot `0x8E8`); no detection API at load | Verified in essence; exports read from the KPI plist `?` pending reconfirmation on another version |
| [AWDL_STAGE1_LIFECYCLE_CHECKS.md](AWDL_STAGE1_LIFECYCLE_CHECKS.md) | Tx data offset: `RegInfo+0x3C` → `eparams+0x144` → `ifnet+0x58C` (not the BSD client); `r15` of `AppleBCMWLANSkywalkInterface::start` is the caller's `RegInfo`; the `uint` of `deregister*` is a dead parameter | Closed and verified; xnu names of the fields and the effect of not subtracting the MTU headroom `?` |
| [AWDL_VERSION_SENSITIVE_OFFSETS.md](AWDL_VERSION_SENSITIVE_OFFSETS.md) | Table of offsets of `RegInfo`, `ifnet_init_eparams`, `ifnet`, `ifclassq`, `netif_llink`, vtable slots and ivars, with a byte pattern per row and a consumption design by the recheck scripts and `Tools/awdl_recheck_offsets.py` (table `Tools/awdl_offsets_25G241.tsv`) | Verified byte by byte on the highest-risk rows (§10); rows `(d)` and versions before 26 `?` |
| [AWDL_STAGE1_SMALL_CHECKS.md](AWDL_STAGE1_SMALL_CHECKS.md) | `IO80211VirtualInterface::init` returns a 1-byte `bool`; the stack `req` of the legacy path of the 94 is born at zero | Closed and verified |
| [AWDL_STAGE1_IFUP_PATH.md](AWDL_STAGE1_IFUP_PATH.md) | What `ifconfig awdl0 up` runs and the first packet: the queues are born disabled and the ND/RS does not reach the no-op | Closed and verified; its §4 is corrected by [AWDL_STAGE1_QSET_STATE.md](AWDL_STAGE1_QSET_STATE.md) |
| [AWDL_STAGE1_QSET_STATE.md](AWDL_STAGE1_QSET_STATE.md) | `qset+0x3C` is `nll_state`; `if_refflags`/`if_eflags` are set by IOSkywalkFamily; the analysis of the first ND/RS is corrected by [AWDL_STAGE1_TXPUSH_LLINK.md](AWDL_STAGE1_TXPUSH_LLINK.md) | Closed and verified; five fine points of its own (§10) |
| [AWDL_STAGE1_ERRNO_TXPATH.md](AWDL_STAGE1_ERRNO_TXPATH.md) | `errnoFromReturn` maps the rejection by disabled queue (`0xE00002D8`) to errno 16 (`EBUSY`); wifip2pd consumes neither `if_link_state` nor `state+113` (reader of `IFF_UP` and `IFF_RUNNING` via `getifaddrs` with keypath endpoint at `?`) | Closed on its two questions; keypath chain of the daemon, kernel readers of `state+113` and `dlil_ifclassq_setup` segment in `?` (§8) |
| [AWDL_STAGE1_IFM_CURRENT.md](AWDL_STAGE1_IFM_CURRENT.md) | `ifm_current` comes out of the media descriptor seeded by `registerNetworkInterface` from `RegInfo+0x28` (0x80) | Closed and verified; its open point is closed by [AWDL_STAGE1_SIOCSIFMEDIA.md](AWDL_STAGE1_SIOCSIFMEDIA.md) |
| [AWDL_STAGE1_SIOCSIFMEDIA.md](AWDL_STAGE1_SIOCSIFMEDIA.md) | Nothing can change `ifm_current` of `awdl0` before the daemon reads it | Closed and verified; T5.1 remains as a regression check |
| [AWDL_STAGE1_VIRTUAL_IF_PARENT.md](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) | The 97 is answered by the family with `getPrimarySkywalkInterface`; no own stub | Closed and verified |
| [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) | The ioctls 130/131 go over a BSD socket and fall out of range → 102; they are not fatal | Closed and verified |
| [AWDL_STAGE1_DISPATCH_PATH.md](AWDL_STAGE1_DISPATCH_PATH.md) | ioctl and user client routes up to the bridge of the 94; `createEventPipe` requires entitlement | Closed and verified; its §4 (the `this` from the `ifnet`) is closed by [AWDL_STAGE1_IFNET_TO_INTERFACE.md](AWDL_STAGE1_IFNET_TO_INTERFACE.md) |
| [AWDL_STAGE1_IFNET_TO_INTERFACE.md](AWDL_STAGE1_IFNET_TO_INTERFACE.md) | `ifnet_softc` delivers the BSD client; complete chains of `en2` and `awdl0` up to `RouteExternal` | Closed and verified |
| [AWDL_STAGE1_REGINFO_AND_GATES.md](AWDL_STAGE1_REGINFO_AND_GATES.md) | Flag of the 6 (bit `+0x52` of block `+0x128`), `RegInfo+0x10`/`+0x40` without readers and gate of the 94 | Closed and verified; the behavior of `registerService(2)` under the gate is decided by T2.2 |
| [AWDL_STAGE1_REGINFO_BROADCOM.md](AWDL_STAGE1_REGINFO_BROADCOM.md) | Broadcom side of `+0x10`/`+0x40` and correction of the container of `+0x118` | Closed and verified |

### 4.2 Stages 2 to 4: data, TX/RX and reviews

| Document | Content | Status |
|---|---|---|
| [AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md) | Plan of stages 2 to 4 after stage 1: real datapath, peer discovery and events, companion `llw0` and low latency; dependency graph and questions that block each stage | Plan derived only from already-verified documents; the step order is detailed by [AWDL_IMPLEMENTATION_PLAN.md](AWDL_IMPLEMENTATION_PLAN.md) and question E3 is closed by [AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md) |
| [AWDL_RTL8814_RAW_TX_PORT1.md](AWDL_RTL8814_RAW_TX_PORT1.md) | Transmitting a raw management frame (action, category 127) with the radio associated, direct programming of port 1 and RX hook in `validate_recv_frame` (`Source/rtl8814au` and `Source/Driver`, sources only) | Read from code, without disassembly; operational summary in §4; implemented afterwards (`AWDLTx.c`) |
| [AWDL_TX_DATOS_SPEC.md](AWDL_TX_DATOS_SPEC.md) | What replaces `RTLAwdInterface::txNoop`: contract of the `IOSkywalkTxSubmissionQueue` action, Ethernet→802.11 conversion with the AWDL encapsulation, new radio ABI function, change steps in the two kexts and tests without an Apple peer | Verified in IDA (25G241) with the addresses in §9; implemented (§10); open points in §8 `?` |
| [AWDL_TX_HOOKS_POR_PAQUETE.md](AWDL_TX_HOOKS_POR_PAQUETE.md) | What `logSkywalkTxReqPacket` updates per packet, what `preQueuePacketCheckQuietMode` does, whether the manager removes or marks inactive a peer with no TX toward it and minimum calls of a driver that transmits on its own | Verified in IDA (25G241); reader of `peer_ivars+184`, thresholds of `shouldRemovePeerFromNDP`, required gate and symbol export `?` |
| [AWDL_RX_DATA_TO_SKYWALK.md](AWDL_RX_DATA_TO_SKYWALK.md) | Delivery of a received AWDL data frame to the `RxCompletionQueue` of `awdl0`: on-the-air format, Broadcom 802.11→Ethernet conversion, Skywalk packet setters, enqueue routes, linkable symbols and recipe for the RTL8814AU | Verified in IDA (25G241); special DAs of `inputPacket`, entity that fills the RX tag and the whole RTL8814 hardware test `?` |
| [AWDL_RX_INPUTPACKET_HOOK.md](AWDL_RX_INPUTPACKET_HOOK.md) | The `inputPacket` pre-hook of `awdl0` (peer filter and PPP hold) before fitting into the `RxCompletionQueue`: role 6 peer filter, special destinations, symbol or vtable and code for `deliverData` | Verified in headless dumps of 25G241, 24H232 and 23J631; closes R4 of [AWDL_DATA_PATH_REVIEW.md](AWDL_DATA_PATH_REVIEW.md) and recipe §8.6 of [AWDL_RX_DATA_TO_SKYWALK.md](AWDL_RX_DATA_TO_SKYWALK.md); that the layer's `awdl0` carries role 6 on hardware `?` |
| [AWDL_TX_FRAME_REVIEW.md](AWDL_TX_FRAME_REVIEW.md) | Byte-by-byte review of the TX action frames (`buildAf`, `putChanSeq`, `txTick`, `alignClock`) against owly, the kernel parser notes and the event 41 payload (findings H1–H8) | Closed as review; H1, H2 and H7 fixed, H3–H6 unchanged, H8 closed by [AWDL_TLV18_OPCLASS_PARSER.md](AWDL_TLV18_OPCLASS_PARSER.md) |
| [AWDL_DATA_PATH_REVIEW.md](AWDL_DATA_PATH_REVIEW.md) | Review of the data path (`deliverData`, `txData`/`queueData`/`sendData`/`flushRadioTx`/`txTick`, `AWDLTx.c`): findings R1–R12 with scenario and fix | Closed as review; R4 deferred and closed afterwards by [AWDL_RX_INPUTPACKET_HOOK.md](AWDL_RX_INPUTPACKET_HOOK.md), R1–R3 fixed, R5 and R12 are not defects, the rest unchanged (§5) |
| [AWDL_PARK_CHANNEL_REVIEW.md](AWDL_PARK_CHANNEL_REVIEW.md) | Review of the channel park without association (`rtlRadioSetChannel`, commits `07a41e8` and `ab11c69`) | Closed as review; H-A applied in `8726bd4`, H-B in `9dc71a6`, H-C to H-E unchanged |

## 5. Tests, virtual peer and AirDrop

### 5.1 Stage 1: logs and script

| Document | Content | Status |
|---|---|---|
| [AWDL_STAGE1_TEST_LOG_PATTERNS.md](AWDL_STAGE1_TEST_LOG_PATTERNS.md) | Exact literals each test T1–T7 must look for in each log channel | Closed as document; the literals dependent on runtime values remain in its table §9 (`?`) |
| [AWDL_STAGE1_LOG_STRINGS_AUDIT.md](AWDL_STAGE1_LOG_STRINGS_AUDIT.md) | Contrast between the expected patterns and the strings the code really emits | Closed and verified |
| [AWDL_STAGE1_RUNBOOK_REVIEW.md](AWDL_STAGE1_RUNBOOK_REVIEW.md) | Review of the hardware test script T1–T7 (findings H1–H17) | Closed and verified; findings applied to the script, which lives outside the repo |
| [AWDL_STAGE1_DEPLOY_REVIEW.md](AWDL_STAGE1_DEPLOY_REVIEW.md) | Review of deployment and build (incidents 1–10) | Closed as review; incidents 1, 2, 3 and 6 applied, 4, 5, 7, 8, 9 and 10 not applied (low severity) |

### 5.2 Virtual peer, iPhone and AirDrop

| Document | Content | Status |
|---|---|---|
| [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) | Virtual AWDL peer on the same Mac (`rtlawdl_sim=1` port in the driver and `Tools/awdlsim` node): synchronization, election, channel sequence, TLVs and data without a second radio; BLE AirDrop trigger without an iPhone, layer synchronization with the master, end-to-end AirDrop with the `utun` bridge (S7) and results | Measured on hardware (2026-11-03 and 2026-11-05, §9); what the virtual peer does not prove, in §5; UMI and peer cache in §9.6–§9.7 |
| [AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md) | 15 to 20 minute script for the first test with a real peer (a visiting iPhone, always in its owner's hands): does the iPhone reach the Mac, TX so the iPhone sees the Mac, data over `awdl0` and how to read the result | Script rehearsed without a peer (`Tools/iphone_test.sh`); not run with an iPhone `?`; known limits at its end (cost of a peer that loses frames, no event 42, `awdl0_ip.pcap` depends on BPF seeing `awdl0`) |
| [AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md) | Result of the 2026-11-08 test with an iPhone: iPhone → Mac AirDrop works (photo and video at ~800 KB/s); length-wrapped frames and presence mode fixed; neighbour entry via `wifip2pd`; Mac → iPhone open | iPhone → Mac measured; Mac → iPhone unsolved, hypothesis `?` |
| [AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md) | Speed: the layer answers the iPhone's ADDBA and reorders per (peer, TID), dense presence during a transfer, and a TX rate ladder from 6 to 54 Mb/s (MCS 7 optional) driven by the 8814's per-frame CCX report | Built and tested outside the kernel (2026-11-09); MAC BlockAck, ladder and speed on air `?` |
| [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) | The A53 as a fixed-channel monitor and as a peer over the air: setup, the Mac's TX seen from outside (slots, AW grid, clock, cadence) and comparison with iOS 26 | Measured on 2026-11-06, with layer `EEEE65F6`'s TLVs 7 and 17 checked over the air and the capture rehearsal done; frames addressed to the Mac delivered and acknowledged; cadence on 149 with no time sharing: 8.80 PSF/s and 2.83 MIF/s; election over the air with metrics 525 and 60; the T9UH's ACK confirmed by the transmission status; the Mac's multicast data seen over the air; unicast ACK to port 1's MAC recognised by the chip, measured against a router; defect: the layer drops IP packets of 1429 to 1484 bytes, avoided with MTU 1428; risk: data goes out at the slot's edge; real iPhone frames accepted; the A53 does not send data, so data reception over the air remains untested |
| [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) | Why the Mac stayed in `Low Power` during a transfer: the packet counters `monitorAWDLDataMode` reads, and the change in the layer | Measured and read in IDA; loaded |
| [AWDL_ESTADO_Y_PLAN.md](AWDL_ESTADO_Y_PLAN.md) | Status at 2026-11-06 and plan: what is running, what was measured and with what, what only a real peer answers, and what follows for each result of the test | Current |
| [AWDL_IOS_27_HOST.md](AWDL_IOS_27_HOST.md) | AWDL on iOS 27.2 (iPhone 11) read from the IPSW: Wi-Fi is a dext, the `IO80211AWDLPeerManager` manager is the same as on macOS, same data mode thresholds, UMI conditions | Current |
| [AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md) | AirDrop with the Bluetooth controller absent or off: can the Mac receive from an iPhone and discover it; `sharingd`, `Sharing.framework` and `WirelessProximity.framework` of 25G241, log strings, answers and practical visit test | Static analysis (sharingd VAs and shared cache); whether the sender's BLE advertisement is needed to keep AWDL up and the payload layout of `startConnectionlessAWDLServiceAdvertisingWithData:` `?` (§9); its Bluetooth requirement is completed by [AWDL_AIRDROP_BLE_CONTROLADOR.md](AWDL_AIRDROP_BLE_CONTROLADOR.md) |
| [AWDL_AIRDROP_BLE_CONTROLADOR.md](AWDL_AIRDROP_BLE_CONTROLADOR.md) | AirDrop with third-party Bluetooth controllers (BCM20702, Intel AX200/AX210, MT7925, CSR, RTL8761B through BlueToolFixup): gates of `sharingd` and `bluetoothd`, full chain in order, `wirelessproxd` inside `bluetoothd` and measurement with a CSR8510 (TP-Link UB400) with its preference fix | Static analysis and measurement with the UB400 (2026-11-05, §8); pending items in its §6 `?` |

## 6. Cross-version compatibility and tools

The ports of the layer and of the offsets to 14.8.9 (23J631) and 15.8.2 (24H232) are
[AIRLAYER_PORT_14_15.md](AIRLAYER_PORT_14_15.md) and [AWDL_VERSION_SENSITIVE_OFFSETS_14_15.md](AWDL_VERSION_SENSITIVE_OFFSETS_14_15.md)
(table 3.1); those of specific areas and the comparison with 25G332 are in this table.

| Document | Content | Status |
|---|---|---|
| [AWDL_KEXT_LINKAGE.md](AWDL_KEXT_LINKAGE.md) | Which AWDL symbols IO80211Family exports, where a third-party kext links and which fallbacks remain | Closed and verified for 25.6.0; re-verify the table after each build; the no-factory fallback of §3 remains unexplored (stage 2) |
| [AWDL_COMPAT_DEPENDENCIES.md](AWDL_COMPAT_DEPENDENCIES.md) | Inventory of everything the AWDL code assumes from the OS, with the cheapest re-check recipe per version (14–26) | Closed as inventory; the per-version verifications were run afterwards with the 14/15 and 25G332 kernelcaches ([AWDL_CODIGO_VS_BUILDS.md](AWDL_CODIGO_VS_BUILDS.md) and the `*_14_15` documents) |
| [AWDL_KERNELCACHE_OBTENTION.md](AWDL_KERNELCACHE_OBTENTION.md) | How to get the x86_64 kernel collection of macOS 14/15/26 without installing the version and produce the dumps | Guide; the 14/15 installer paths are closed by its §2.4 (23J631, 24H232 and 25G332, same `__HIB` base; point 22 of §8.4) |
| [AWDL_BKE_ATTACH.md](AWDL_BKE_ATTACH.md) | Record of the adapter-attach session to the kernel collection and its operational limitations | Closed (historical session record) |
| [AWDL_CODIGO_VS_BUILDS.md](AWDL_CODIGO_VS_BUILDS.md) | Compatibility of `AWDLInterface.{h,cpp}` (validated on 25G241) with 25G332 (macOS 26.7.2), 24H232 and 23J631: symbols, vtable slots and offsets with the recheck tools, manual checks and list of changes per build | Verified with the dumps of each kernel collection: 25G332 works as is; 24H232 and 23J631 need their own binary and header; `25G332` still to be registered in `Tools/airlayer_builds.txt`, time sharing on 14/15 unmeasured and NAN peers on 14 `?` |
| [AWDL_CARD_CAPS_14_15.md](AWDL_CARD_CAPS_14_15.md) | Map of readers of the card capabilities block (GET 12) on 23J631 and 24H232, ported from [AWDL_CARD_CAPS_RESTANTES.md](AWDL_CARD_CAPS_RESTANTES.md): no centralized buffer on 14, readers without counterpart and gates whose mechanism changes | Verified in the 14 and 15 kernel collections; `InterfaceMonitor` channel feeding on 15 and internal pro-mode branches `?` |
| [AWDL_SECUENCIA_14_15.md](AWDL_SECUENCIA_14_15.md) | Port of the channel sequence to 23J631 and 24H232: slot type table (56 rows on 25G241, 54 on 14 and 15), state switch, dual-band gates and implications for the layer | Verified in the disassembly; open points in its §9 `?` (e.g. `dynSdbForceChanSeq` on cards without full SDB) |
| [AWDL_EVENTOS_Y_AF_14_15.md](AWDL_EVENTOS_Y_AF_14_15.md) | Portability of events 41/42/43, the `postMessage` route and the action frames (event 48), and PSF/MIF emission to 23J631 and 24H232 | Verified on the three builds; the `executeCommand` gate when hot, inlined TLV 17 on 14 and consumption of events 41/42/43 by other daemons `?` |
| [AWDL_DATA_PATH_PORT_14_15.md](AWDL_DATA_PATH_PORT_14_15.md) | Portability to 14.8.9 and 15.8.2 of the `awdl0` data code (RX, AW clock, TX, per-slot bounding, park rejection codes): accessors, packet life cycle, peer manager and template and channel sequence layouts | Verified with the symtabs and dumps of the three kernelcaches; the `interfaceStateChange` gate and the null action of `initWithPool` on 14/15 `?` |
| [AWDL_PRESENCE_OFFSETS_14_15.md](AWDL_PRESENCE_OFFSETS_14_15.md) | Presence and PPP offsets ported to 23J631 and 24H232 (ivars, vtable slots, handler table, threshold; `Tools/awdl_presence_offsets.tsv`), what did not move, shape changes and selection order for the gate | Each row resolved by symbol and read in the disassembly of the three builds; absent rows and open points in its §10 `?` |

## 7. Reading order for someone new

1. `README.md` (repo root): current state, build and install.
2. [AWDL_PROTOTYPE_DESIGN.md](AWDL_PROTOTYPE_DESIGN.md): the stage plan that orders everything else.
3. Daemon side: [AWDL_WIFIP2PD_REQUESTS.md](AWDL_WIFIP2PD_REQUESTS.md) →
   [AWDL_WIFIP2PD_DISCOVERY.md](AWDL_WIFIP2PD_DISCOVERY.md) →
   [AWDL_WIFIP2PD_POST_CREATE.md](AWDL_WIFIP2PD_POST_CREATE.md) →
   [AWDL_WIFIP2PD_IOCTLS.md](AWDL_WIFIP2PD_IOCTLS.md) with
   [AWDL_WIFIP2PD_CALLERS.md](AWDL_WIFIP2PD_CALLERS.md) and
   [AWDL_WIFIP2PD_STRUCTS_SEEN.md](AWDL_WIFIP2PD_STRUCTS_SEEN.md) →
   [AWDL_LLW_COMPANION.md](AWDL_LLW_COMPANION.md).
4. Kernel surface: [AWDL_KERNEL_ROLE_TABLE.md](AWDL_KERNEL_ROLE_TABLE.md) →
   [AWDL_PROXIMITY_VTABLE_HIERARCHY.md](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) and
   [AWDL_PURE_SLOTS.md](AWDL_PURE_SLOTS.md) →
   [AWDL_APPLE80211_DISPATCH.md](AWDL_APPLE80211_DISPATCH.md) →
   [AWDL_IOCTL_SLOT_MAP.md](AWDL_IOCTL_SLOT_MAP.md) →
   [AWDL_STRUCTS.md](AWDL_STRUCTS.md) →
   [AWDL_INTERFACE_BIRTH.md](AWDL_INTERFACE_BIRTH.md) →
   [AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) →
   [AWDL_EVENTS.md](AWDL_EVENTS.md) →
   [AWDL_REQUEST_ISSUERS.md](AWDL_REQUEST_ISSUERS.md).
5. Protocol and hardware: [AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) →
   [AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) →
   [AWDL_TLV18_OPCLASS_PARSER.md](AWDL_TLV18_OPCLASS_PARSER.md) →
   [AWDL_TLV5_ELECTION.md](AWDL_TLV5_ELECTION.md) →
   [AWDL_OPEN_QUESTIONS_1.md](AWDL_OPEN_QUESTIONS_1.md) →
   [AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md) →
   [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) →
   [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) →
   [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md).
6. Channel sequence, presence and capabilities:
   [AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) →
   [AWDL_CHANNEL_STRATEGY.md](AWDL_CHANNEL_STRATEGY.md) →
   [AWDL_SECUENCIA_129_EMISOR.md](AWDL_SECUENCIA_129_EMISOR.md) →
   [AWDL_SIN_ASOCIACION_INFRA.md](AWDL_SIN_ASOCIACION_INFRA.md) →
   [AWDL_SECUENCIA_CON_INFRA.md](AWDL_SECUENCIA_CON_INFRA.md) →
   [AWDL_SECUENCIAS_OBSERVADAS.md](AWDL_SECUENCIAS_OBSERVADAS.md) and
   [AWDL_SCAN_COEXISTENCIA.md](AWDL_SCAN_COEXISTENCIA.md); presence:
   [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) →
   [AWDL_WIFIP2PD_PRESENCE_TRIGGER.md](AWDL_WIFIP2PD_PRESENCE_TRIGGER.md) →
   [AWDL_WIFIP2PD_PRESENCE_ONCE.md](AWDL_WIFIP2PD_PRESENCE_ONCE.md) →
   [AWDL_PRESENCE_PREREQS.md](AWDL_PRESENCE_PREREQS.md) →
   [AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md); manager life cycle:
   [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) →
   [AWDL_PEERMANAGER_TEARDOWN.md](AWDL_PEERMANAGER_TEARDOWN.md); card capabilities:
   [AWDL_CARD_CAPS_RESTANTES.md](AWDL_CARD_CAPS_RESTANTES.md) →
   [AWDL_CAP29_DOBLE_BANDA_RIESGOS.md](AWDL_CAP29_DOBLE_BANDA_RIESGOS.md) and
   [AWDL_CAP78_CONCURRENCIA.md](AWDL_CAP78_CONCURRENCIA.md).
7. Stage 1: [AWDL_STAGE1_IMPL_SPEC.md](AWDL_STAGE1_IMPL_SPEC.md) first, and then the closures in
   this order: [AWDL_STAGE1_DISPATCH_PATH.md](AWDL_STAGE1_DISPATCH_PATH.md) and
   [AWDL_STAGE1_IFNET_TO_INTERFACE.md](AWDL_STAGE1_IFNET_TO_INTERFACE.md) (dispatch),
   [AWDL_STAGE1_QUEUE_SHAPE.md](AWDL_STAGE1_QUEUE_SHAPE.md) and
   [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md) (queues and BSD startup),
   [AWDL_STAGE1_IFM_CURRENT.md](AWDL_STAGE1_IFM_CURRENT.md) and
   [AWDL_STAGE1_SIOCSIFMEDIA.md](AWDL_STAGE1_SIOCSIFMEDIA.md) (media),
   [AWDL_STAGE1_VIRTUAL_IF_PARENT.md](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) and
   [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) (post-create sequence),
   [AWDL_STAGE1_STUB_VALUES.md](AWDL_STAGE1_STUB_VALUES.md),
   [AWDL_STAGE1_OPEN_POINTS.md](AWDL_STAGE1_OPEN_POINTS.md) and
   [AWDL_STAGE1_PENDING_CHECKS.md](AWDL_STAGE1_PENDING_CHECKS.md),
   [AWDL_STAGE1_REGINFO_AND_GATES.md](AWDL_STAGE1_REGINFO_AND_GATES.md) with
   [AWDL_STAGE1_REGINFO_BROADCOM.md](AWDL_STAGE1_REGINFO_BROADCOM.md),
   [AWDL_STAGE1_IFUP_PATH.md](AWDL_STAGE1_IFUP_PATH.md) and
   [AWDL_STAGE1_QSET_STATE.md](AWDL_STAGE1_QSET_STATE.md) (ifup), and finally the reviews:
   [AWDL_STAGE1_SPEC_DELTA.md](AWDL_STAGE1_SPEC_DELTA.md),
   [AWDL_STAGE1_CODE_FIXES.md](AWDL_STAGE1_CODE_FIXES.md),
   [AWDL_STAGE1_CODE_REVIEW_IFACE.md](AWDL_STAGE1_CODE_REVIEW_IFACE.md) and
   [AWDL_STAGE1_CODE_REVIEW_HOOK.md](AWDL_STAGE1_CODE_REVIEW_HOOK.md).
8. Stage 1 tests: [AWDL_STAGE1_TEST_LOG_PATTERNS.md](AWDL_STAGE1_TEST_LOG_PATTERNS.md) →
   [AWDL_STAGE1_LOG_STRINGS_AUDIT.md](AWDL_STAGE1_LOG_STRINGS_AUDIT.md) →
   [AWDL_STAGE1_RUNBOOK_REVIEW.md](AWDL_STAGE1_RUNBOOK_REVIEW.md) and
   [AWDL_STAGE1_DEPLOY_REVIEW.md](AWDL_STAGE1_DEPLOY_REVIEW.md).
9. Stages 2 to 4, data and TX/RX: [AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md) →
   [AWDL_IMPLEMENTATION_PLAN.md](AWDL_IMPLEMENTATION_PLAN.md) →
   [AWDL_RTL8814_RAW_TX_PORT1.md](AWDL_RTL8814_RAW_TX_PORT1.md) →
   [AWDL_TX_DATOS_SPEC.md](AWDL_TX_DATOS_SPEC.md) with
   [AWDL_TX_HOOKS_POR_PAQUETE.md](AWDL_TX_HOOKS_POR_PAQUETE.md) →
   [AWDL_RX_DATA_TO_SKYWALK.md](AWDL_RX_DATA_TO_SKYWALK.md) →
   [AWDL_RX_INPUTPACKET_HOOK.md](AWDL_RX_INPUTPACKET_HOOK.md), and the reviews
   [AWDL_TX_FRAME_REVIEW.md](AWDL_TX_FRAME_REVIEW.md),
   [AWDL_DATA_PATH_REVIEW.md](AWDL_DATA_PATH_REVIEW.md) and
   [AWDL_PARK_CHANNEL_REVIEW.md](AWDL_PARK_CHANNEL_REVIEW.md).
10. Time sharing and RTL8814A firmware: [AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) →
    [AWDL_FW_8814A_MCC.md](AWDL_FW_8814A_MCC.md) →
    [AWDL_FW_8814A_CHSW_OFFLOAD.md](AWDL_FW_8814A_CHSW_OFFLOAD.md).
11. Tests with a peer: [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) →
    [AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md), with
    [AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md) and
    [AWDL_AIRDROP_BLE_CONTROLADOR.md](AWDL_AIRDROP_BLE_CONTROLADOR.md) for the Bluetooth requirement.
12. Compatibility: [AWDL_KEXT_LINKAGE.md](AWDL_KEXT_LINKAGE.md) →
    [AWDL_COMPAT_DEPENDENCIES.md](AWDL_COMPAT_DEPENDENCIES.md) →
    [AWDL_KERNELCACHE_OBTENTION.md](AWDL_KERNELCACHE_OBTENTION.md) →
    [AWDL_CODIGO_VS_BUILDS.md](AWDL_CODIGO_VS_BUILDS.md) and the 14/15 ports
    ([AIRLAYER_PORT_14_15.md](AIRLAYER_PORT_14_15.md),
    [AWDL_VERSION_SENSITIVE_OFFSETS_14_15.md](AWDL_VERSION_SENSITIVE_OFFSETS_14_15.md),
    [AWDL_SECUENCIA_14_15.md](AWDL_SECUENCIA_14_15.md),
    [AWDL_EVENTOS_Y_AF_14_15.md](AWDL_EVENTOS_Y_AF_14_15.md),
    [AWDL_DATA_PATH_PORT_14_15.md](AWDL_DATA_PATH_PORT_14_15.md),
    [AWDL_PRESENCE_OFFSETS_14_15.md](AWDL_PRESENCE_OFFSETS_14_15.md),
    [AWDL_CARD_CAPS_14_15.md](AWDL_CARD_CAPS_14_15.md)). `AWDL_BKE_ATTACH.md` is
    a session record and is read only as historical context.

The intermediate inventories (`AWDL_APPLE80211_STRUCTS.md`, `AWDL_VTABLE_SLOTS.md`) and the
already-absorbed reviews can be skipped on the first read and consulted only as reference. The notes
on external projects and the comparison with iOS captures (`AWDL_REFERENCIAS_EXTERNAS.md`,
`AWDL_REF_SIMPLEBCM.md`, `AWDL_AF_VS_IPHONE.md`) are contrast material and need not be read
before implementing.

## 8. Open questions (`?`) still open across all documents

Collected from the "open points"/"pending" sections of each document, deduplicated; it replaces
section 4 of `HANDOFF_AWDL.md`, of which it corrects two already-closed points (the `this` of
`processBSDCommand`, closed by `AWDL_STAGE1_IFNET_TO_INTERFACE.md`, and the values of
`RegInfo+0x10`/`+0x40`, functionally closed by proving they have no reader in
`AWDL_STAGE1_REGINFO_AND_GATES.md` §3 and `AWDL_STAGE1_REGINFO_BROADCOM.md` §3).

### 8.1 The ones stage 1 hardware decides (tests T1–T7)

| # | Open point | Document | Closure |
|---:|---|---|---|
| 1 | Behavior of `registerService(kIOServiceSynchronous)` under the work loop gate of the 94: possible deadlock (finding A6) | `AWDL_STAGE1_IMPL_SPEC.md` §15.1; `AWDL_STAGE1_SPEC_DELTA.md` (#90); `AWDL_STAGE1_REGINFO_AND_GATES.md` §5; `AWDL_STAGE1_CODE_REVIEW_HOOK.md` A6 | Test T2.2 (measure the return of the 94). On hardware `awdl0` is created and published and `wifip2pd` uses it ([AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md) §1), so the success path does not hang; no document records the measurement of the return of the 94 |
| 2 | Rollback when the registration succeeded but the BSD name is missing; whether `terminate()` over a failed startup removes the queues (finding A7, #68) | `AWDL_STAGE1_CODE_REVIEW_HOOK.md` A7; `AWDL_STAGE1_SPEC_DELTA.md` §7 | Tests T3 and T7.3 |
| 3 | Real fatality of the event monitoring registration in wifip2pd (#62) | `AWDL_STAGE1_SPEC_DELTA.md` §6; `AWDL_STAGE1_IMPL_SPEC.md` T7.1 | Test T7.1 |
| 4 | Final properties in the IORegistry: `classNameOverride` of the subclass and `IOSkywalkNexusUUID` | `AWDL_STAGE1_SPEC_DELTA.md` §7 | Test T3.1 (`ioreg`) |

### 8.2 The ones a disassembly check closes (IDA)

| # | Open point | Document | Closure |
|---:|---|---|---|
| 5 | Class of the WCL object at `glue+0xD8` and the method of slot `+0x120` that decides whether the 94 is consumed; whether a WCL exists that consumes it in this stack (#95) | `AWDL_STAGE1_OPEN_POINTS.md` §4; `AWDL_STAGE1_SPEC_DELTA.md` §7 | Identify the class and the slot method in the KC |
| 6 | Caller of `requestEnqueue` on the queues of `awdl0` and enable point of the event source (`+0x28`) at datapath startup: with a null action it would go from latent to fatal | `AWDL_STAGE1_PENDING_CHECKS.md` §4 | Locate the caller in the family; T3.5 confirms it on hardware |
| 7 | ~~Real return type of `IO80211VirtualInterface::init` (#82); the code takes it as `bool`~~ | `AWDL_STAGE1_SPEC_DELTA.md` §7; `AWDL_STAGE1_CODE_FIXES.md` §3 | Read the return handling in the family `init`. Closed by [AWDL_STAGE1_SMALL_CHECKS.md](AWDL_STAGE1_SMALL_CHECKS.md) §1 |
| 8 | ~~Initial length of the stack `req` in the legacy path of the 94 when `len < 32` (finding A10)~~ | `AWDL_STAGE1_CODE_REVIEW_HOOK.md` A10 | Read the initialization of the `req` in `setVirtualInterfaceCreate`. Closed by [AWDL_STAGE1_SMALL_CHECKS.md](AWDL_STAGE1_SMALL_CHECKS.md) §2 |
| 9 | Fine points of the life cycle: zero-init of the typed allocator, cleanup of `registerInterestedDriver` if branch 5 of `init` fails, owner of the lock `ivars+0x30`, field of the `ifnet` that consumes `clienteBSD+0x13C`, linkage of `r15` in `AppleBCMWLANSkywalkInterface::start`, intent of the `uint` parameter of `deregisterEthernetInterface` | `AWDL_STAGE1_PENDING_CHECKS.md` §"Points left open" | Directed checks in the KC, one per point (partial: #4 `ifnet+0x58C`, #5 `r15 = rdx` and #7 dead parameter, closed by [AWDL_STAGE1_LIFECYCLE_CHECKS.md](AWDL_STAGE1_LIFECYCLE_CHECKS.md)) |
| 10 | TX path: errno the stack sees when the packet is dropped (mapping of `0xE00002D8` in `errnoFromReturn`), consumer of `state+113`/`if_link_state` in wifip2pd, call of the `ifclassq` to `tx_push`, registration path of the `netif_llink` in the kext path | `AWDL_STAGE1_IFUP_PATH.md` §8; `AWDL_STAGE1_QSET_STATE.md` §10; errno 16 (`EBUSY`) and searches in wifip2pd closed in `AWDL_STAGE1_ERRNO_TXPATH.md` §1-§6 | Walk the keypath chain of wifip2pd and the filling of the `ifclassq` table (`dlil_ifclassq_setup`); registration of the `netif_llink` |
| 11 | ~~Order between `setInterface` and the creation of the `ifnet` in `bsd_start`; who consumes the return 22 of the rejection by `num_txqs == 0`~~ | `AWDL_STAGE1_QUEUE_SHAPE.md` §11 | Closed by [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md): `setInterface` at `0xffffff8002b0a38d` (inside the registration, before the `registerService`); the `ifnet` at `0xffffff8002affbb5` (inside the `start` of the BSD client); the kernel 22 is consumed by the alloc of the provider and the one of the DriverKit path becomes `0xE00002BC` |
| 12 | Which getters of the 63 slots the daemon queries outside the post-create sequence (e.g. when opening AirDrop) | `AWDL_STAGE1_STUB_VALUES.md` §3/§5 | Read the master switch of events/requests of the daemon; confirmed with a real Apple peer on hardware (the virtual peer already exercises the AirDrop path of `wifip2pd`, [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §8, without listing the getters queried) |
| 13 | Unnamed TLV builders (datapath state, ranging, real-time group, version, NAN sync, pref chan) and exact content of the TX pull | `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11 and §4 | Identify and decompile the builders (stage 2). Partial: TLV 5 is closed by [AWDL_TLV5_ELECTION.md](AWDL_TLV5_ELECTION.md) and the per-packet data TX by [AWDL_TX_DATOS_SPEC.md](AWDL_TX_DATOS_SPEC.md) and [AWDL_TX_HOOKS_POR_PAQUETE.md](AWDL_TX_HOOKS_POR_PAQUETE.md); the other builders remain unnamed |
| 14 | ~~Link of the kernel `IO80211AWDLPeerManager` with the interface of an external driver (ivars `+0x120`/`+0x28`)~~ | `AWDL_APPLE80211_DISPATCH.md` §8.3; `AWDL_PROTOTYPE_DESIGN.md` §7 | Closed by [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md): the factory inherited from `0xB08` creates the manager in the `start` of the vif and publishes it at `ivars+0x28`; the controller must override `createVirtualInterface` for role 6 |
| 15 | ~~Which AWDL objects an external kext can instantiate and what happens if the instantiation is rejected; the vtable fallback does not cover creation without exported factories~~ | `AWDL_KEXT_LINKAGE.md` §3 | Closed by [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md): the factory inherited from `0xB08` creates the manager in the `start` of the vif and publishes it at `ivars+0x28`; the controller must override `createVirtualInterface` for role 6 |
| 16 | ~~TX path of action frames in stage 3: internal route (type 120 or others) or own mechanism of the kext~~ | `AWDL_PROTOTYPE_DESIGN.md` §5 and §7 | Follow the emitter of event 48 backwards to the driver (stage 3). Closed: the layer transmits with its own mechanism, the radio ABI `rtlRadioTx80211` (raw TX and port 1 in [AWDL_RTL8814_RAW_TX_PORT1.md](AWDL_RTL8814_RAW_TX_PORT1.md)), and event 48 comes out of the RX path, not the TX ([AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md)); PSF and MIF go out without TX failures on hardware ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9) |
| 17 | ~~Semantics of the u32 `@+8` of event 47 (`PEER_PRESENCE`)~~ | `AWDL_PROTOTYPE_DESIGN.md` §5 | Closed by [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §1: `1` = absence (log `postPeerAbsence`), the only value the kernel writes |
| 18 | Value of the `code` field (payload+0) of type 293 on the AWDL path and its semantics (the kernel does not read it) | `AWDL_LLW_COMPANION.md` §1.1 and §5; `AWDL_STRUCTS.md` §3.4 | Trace the fifth argument of the per-role creator in `sub_100047660` |

### 8.3 The ones a test on the RTL8814 hardware closes

| # | Open point | Document | Closure |
|---:|---|---|---|
| 19 | Real unit of the guard time (×10 factor observed unconfirmed), precision of the host software timers against the AWDL cadence and viability of `GTINT3`/`GTINT4` as hardware timer on USB | `AWDL_FIRMWARE_DUTIES.md` §7; `AWDL_RTL8814_HW_SUPPORT.md` §7 (rows b1/b3a/b3b) | Measurement on the 8814AU. Partial: the precision of the host timers was measured with the virtual peer (maximum offset 1 TU for minutes, [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9.2) and the driver's per-slot switch at 4357 and 4355 µs outbound ([AWDL_CAP29_DOBLE_BANDA_RIESGOS.md](AWDL_CAP29_DOBLE_BANDA_RIESGOS.md) §4); the unit of the guard time and `GTINT3`/`GTINT4` remain open |
| 20 | Live default value of `RXFLTMAP0`, TX confirmation (C2H/`CONFIG_XMIT_ACK`), operation of port 1 in the USB path and integration point of the delivery of action frames toward AirLayer/BPF | `AWDL_RTL8814_HW_SUPPORT.md` §6.2–§6.4 and §8 | Live registers and RX/TX test of action frames. Partial: action frame RX and TX work on hardware with the virtual peer (thousands of frames each way without TX failures, [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9) and the PSF RX of an A53 over the air reached the Mac; the live value of `RXFLTMAP0` and the TX confirmation by C2H remain open |
| 21 | The whole feasible/partial/missing classification of `AWDL_RTL8814_HW_SUPPORT.md` §7 | `AWDL_RTL8814_HW_SUPPORT.md` §7 | Stages 2–4 of the plan, on hardware. In progress: measured with the virtual peer (election, synchronization, IPv6 data and time sharing); a real peer is missing ([AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md)) |

### 8.4 The ones running the guide itself closes (compatibility)

| # | Open point | Document | Closure |
|---:|---|---|---|
| 22 | ~~Installer paths of Intel macOS 14/15 and derivation of the KC base on another version~~; what remains are the sealed volume on Apple silicon (out of scope) and the equality of the installer KC with that of an installed system | `AWDL_KERNELCACHE_OBTENTION.md` §1, §2 and §6 | Installers and base closed by `AWDL_KERNELCACHE_OBTENTION.md` §2.4 (23J631, 24H232, 25G332, same base `__HIB`); the equality is checked with the md5 of 25G332 after updating |

### 8.5 The ones that arose from the documents after 2026-10-29

| # | Open point | Document | Closure |
|---:|---|---|---|
| 23 | Behavior with a real peer (iPhone or Mac): acceptance of PSF and MIF, data over `awdl0`, TX visible to the iPhone; never tested | `AWDL_PRUEBA_CON_IPHONE.md` §5–§7; `AWDL_PAR_VIRTUAL.md` §5 | Run the visit script with an iPhone |
| 24 | Time sharing over the air: cost of a peer that loses frames, speeds above 12 Mb/s, which sequence AirDrop produces with a real peer | `AWDL_MCC_TIEMPO_COMPARTIDO.md` §8 | Test with a real peer |
| 25 | Real latency of the firmware channel switch offload (H2C 0x1C/0x87) for moving the primary 153↔149 | `AWDL_FW_8814A_CHSW_OFFLOAD.md` §11 | Measurement on the 8814AU |
| 26 | Runtime behavior on 14.8.9 and 15.8.2 (`postMessage` gate, time sharing, internal offsets); the layer needs its own binary and header per build and 25G332 is not registered in `Tools/airlayer_builds.txt` | `AWDL_CODIGO_VS_BUILDS.md` §5; `AWDL_EVENTOS_Y_AF_14_15.md` §7; `AWDL_DATA_PATH_PORT_14_15.md` §7; `AWDL_CARD_CAPS_14_15.md` §7 | Test on a machine with 14 or 15; register 25G332 in the build table |
| 27 | Whether iOS requires the sender's BLE advertisement to bring up and keep AWDL during a transfer, and the exact payload layout of `startConnectionlessAWDLServiceAdvertisingWithData:` | `AWDL_AIRDROP_SIN_BLUETOOTH.md` §9; `AWDL_AIRDROP_BLE_CONTROLADOR.md` §6 | Reference capture with an iPhone |
| 28 | Slot alignment with an iPhone, real `held`, ACK and Block Ack to the Mac's data | `AWDL_MODO_DATOS.md` §9; `AWDL_A53_SNIFFER.md` §13 | Test with an iPhone |
| 29 | Air budget without counting retries; stray frames outside the announced slots; how the 1.05 s cadence fits with `ivars+16064` | `AWDL_MODO_DATOS.md` §5 and §9 | Measurement with a real peer, and IDA |
