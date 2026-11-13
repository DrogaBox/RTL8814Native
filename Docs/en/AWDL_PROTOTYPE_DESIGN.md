# Staged design of the AWDL prototype (role 6) for RTL8814AirLayer

Replaces the previous version of this document. That one posed the prototype as a
linkage hypothesis conditioned on unclosed pending items (what released the 30 s wait after
`VIRTUAL_IF_CREATE`, which requests `wifip2pd` made afterward, what the AWDL gate was, what types
97 and 293 were). All those points were closed in the series of analysis documents of
`Docs/`, and this document turns those results into a staged implementation plan: for
each stage it lists the apple80211 requests, the IORegistry properties and the events the
kext must provide, with the kernel and daemon addresses that fix each requirement. Anything
unverified goes as `?`.

## 0. Base, conventions and method

Source documents (verified, kernel md5 `f489ff8be80e6013a1bf47561dcaef45`, daemon
`a120cfccfa2783303604ce5be9e1763a`):

| Document | What it contributes to the design |
|---|---|
| [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) | Release of the 30 s semaphore, full request sequence of the daemon, types 97 and 293 |
| [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) | Dispatch routes, modern/legacy/internal tables, AWDL surface (+0xFC8..+0x11C0) and slots |
| [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) | C layouts of the payloads (gate, create, companion and per-type structs) |
| [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) | Birth of `awdl0` in the kernel: properties, unit, ifnet, `registerService` order |
| [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) | Skywalk datapath of role 6: queues, pools, MTU, TX/RX |
| [`AWDL_EVENTS.md`](AWDL_EVENTS.md) | Event channels, emitters and what `wifip2pd` consumes |
| [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) | Companion `llw0`: emission of the 293, consequences of its failure, verdict for stage 1 |

Conventions: addresses are VAs of the indicated binary (`w2p` = `wifip2pd.x86_64`); `?` = not
verified. The addresses cited in this document were re-checked with IDA 9.4 headless over
copies of the IDBs in `Build/ida_awdl/task_awdl_design/kc/` and `/p2p/` (scripts `verify_kc.py` /
`verify_p2p.py`, outputs `verify_out.txt`; detail in §8).

## 1. Verified facts that fix the design

### 1.1 The AWDL gate is bit 0 of byte +0x08 of `CARD_CAPABILITIES`

`wifip2pd` opens each role branch with `RawGet` type 12 (`CARD_CAPABILITIES`, 21 B buffer;
sites `w2p 0x10004811d`, `0x100048260`, `0x1000483a8`). The AWDL branch (case 1 of switch
`0x100047E85`) tests bit 0 of `data[4]` (`test byte [rbp+var_70+8],1` at `w2p 0x1000483b5`);
since the copyback of the modern handler (`gGetHandlerTable[11]` = `0xffffff800245ebb8` → handler
`getCARD_CAPABILITIES` `0xffffff8002279e89`) delivers `min(req_len, 21)` bytes from byte 4 of the
struct `apple80211_capability_data` (28 B: `version` u32 @+0x00, `flags[24]` @+0x04), `data[4]` is
the **byte +0x08 of the struct**. The NAN branches (cases 2/3) test `data[9]` (`w2p 0x10004812a`,
`0x10004826d`) = **byte +0x0D**, which is the Wi-Fi Aware gate, not AWDL's. On Broadcom
byte +0x0D bit 0 is set by `AppleBCMWLANCore::getCARD_CAPABILITIES` (`0xffffff8001661076`) under
`featureFlagIsBitSet(6)`; the condition setting +0x08 bit 0 on Broadcom was not identified
(`?`) — irrelevant for the design: the kext must set that bit deliberately.
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1.)

### 1.2 The 30 s wait is released by IORegistry, not by any Apple80211 event

After a successful `RawSet` 94, the per-role creator (`sub_100047660`, `w2p 0x100047660`) waits in
`OS_dispatch_semaphore.wait(timeout:)` (`w2p 0x1000491a8`) with `DispatchTime.now() + 30 s`
(`mov qword [r12], 0x1E` at `w2p 0x10004913b`). The wait is released by the
**`IOServiceMatched`** notification: the daemon registers `IOServiceAddMatchingNotification` with the dictionary
`{"IOPropertyExistsMatch": "IOInterfaceName"}` (`w2p 0x100043314`), and the discoverer
(`sub_100043720`) filters each new service by: (a) property `IOInterfaceName` present;
(b) `SIOCGIFMEDIA` with `(ifm_current & 0xE0) == 0x80` (`IFM_IEEE80211`; ioctl at `w2p 0x100043cbf`,
filter at `w2p 0x100043ccc`); (c) live BSD index via `getifaddrs`/`AF_LINK`; (d) role read from
`IO80211VirtualInterfaceRole` (or `IO80211InterfaceRole`): the string `AirLink` matches internal code
1 and signals the role's semaphore (`call sub_100046BD0` at `w2p 0x1000451f2`; signal
`w2p 0x100046d03`). The Apple80211 event route (`sub_1000685A0` → `sub_100041FE0`) never touches the
semaphore. If handle creation fails, it signals anyway (`w2p 0x1000445a3`).
([`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.)

### 1.3 `VIRTUAL_IF_CREATE` (type 94): role 6, 32 B payload, name at payload+16

`gSetHandlerTable[93]` (`0xffffff80024601d8`) points to handler `setVIRTUAL_IF_CREATE`
(`0xffffff80022886a8`), which requires `len == 32` and `data != NULL`, tries the WCL route and falls to the bridge
`apple80211setVIRTUAL_IF_CREATE`, which does `safeMetaCast` to `IO80211InfraProtocol` and calls slot
**+0x1160 (slot 556)** of the object — on Broadcom it is implemented by
`AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`): reads the role at `payload+0x0C`
(`0xffffff800167891c`), dispatches 6 → `createProxInterface` (`0xffffff800165dcbe`, call at
`0xffffff8001678bdd`), 7 → APSTA, 8..10 → NAN; if the interface already exists it returns 17 and still jumps
to the common block; and writes the BSD name with `strlcpy(payload+16, getBSDName(), 0x10)`
(`call _strlcpy` at `0xffffff8001678a2b`). The payload the daemon arms: u32 @+0 (sent, read
`?`), MAC 6 B @+0x04, role u32 @+0x0C (6 = AWDL; the daemon also uses 9 and 0xA `?`), output
name @+0x10. ([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2,
[`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1.)

### 1.4 Birth of `awdl0`: the name comes from the `RegistrationInfo`, not from a role counter

Broadcom chain (`createProxInterface` → `withOptions` → `init` → `attach` → `start`):
`AppleBCMWLANProximityInterface::withOptions(core, payload+4, "awdl")`
(`0xffffff80017159e6`), `init(core, mac, 6, "awdl")` (`0xffffff8001718db0`) which calls
`IO80211VirtualInterface::init` (`0xffffff80023305f8`) and publishes `IO80211VirtualInterfaceRole =
"AirLink"` via `setInterfaceRole(6)` (`0xffffff80022f60f0`; key table `0xffffff800246bdd8[5]`,
values `0xffffff800246bd88[5]`), `attach` through slot +0x670, and `start(core, RegInfo)`
(`0xffffff8001729376`): queue registration with the bus, MTU 0x594 at `RegInfo+0x48/+0x4C`
(`mov rax, 0x59400000594` at `0xffffff80017296ff`, `mov [r15+0x48], rax` at `0xffffff8001729709`) and
`IOSkywalkEthernetInterface::registerEthernetInterface` (`0xffffff8002b10a84`; `initRegistrationInfo`
`0xffffff8002b108e2` validates version 1 / size 0x130), which publishes `IOMACAddress`,
`IOInterfaceNamePrefix` and `IOInterfaceUnit` (`registerNetworkInterface` `0xffffff8002b0a204`).
On success, synchronous **`registerService(2)`** (`mov esi,2` at `0xffffff8001729825`,
`call [rax+0x5B0]` at `0xffffff800172982a`): inside runs `IOSkywalkNetworkBSDClient::start`
(`0xffffff8002aff562`), which composes `"awdl0"` with `snprintf("%s%u", prefix, unit)`, publishes
`"BSD Name"` and **`IOInterfaceName`** (first publication: that is the moment it fires the matching
of `wifip2pd`), creates the ifnet (`kern_nexus_controller_alloc_net_provider_instance` →
`ifnet_allocate_extended` → `prepareBSDInterface`, which republishes `IOInterfaceName` at
`0xffffff80022f63eb` → `bpfattach` → `ifnet_attach`) and finishes with its own `registerService(0)`.
Back out, `setVIRTUAL_IF_CREATE` copies the name to payload+16 and returns 0; when the daemon
receives the 0, the ifnet already exists and the notification is on its way or delivered.

The unit is **the value of `RegistrationInfo+0x38` the driver writes** (0 for `awdl`, 1 for the
AP), not the global per-role counter `intfRoleToNumberMap` (which only feeds the pipe name).
Prefix at `RegInfo+0x30` (pointer to the literal `"awdl"` at `0xffffff8001797376`).
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2–§5.)

### 1.5 AWDL surface and dispatch

Four routes reach the tables of `IO80211Family`, indexed by `type − 1` with range `[1, 586]`
(`apple80211RouteExternal`, `0xffffff800236cd2e`): modern user client (`gGetHandlerTable`
`0xffffff800245eb60` / `gSetHandlerTable` `0xffffff800245fef0`), classic BSD ioctl
(`gGetHandlerTableLegacy` `0xffffff8002470d50` / `gSetHandlerTableLegacy` `0xffffff8002479060`,
no AWDL-specific types), internal glue route (`gInternalGet/SetHandlerTable`
`0xffffff800247cea0` / `0xffffff800247e0f0`) and the modern AWDL types that do not touch the vtable but
`IO80211AWDLPeerManager`. The bridges serving the AWDL surface first call
`isCommandProhibited` (slot +0xCC8), cascade `safeMetaCast` by protocol class and
dispatch the corresponding slot; without a successful cast they return `0xE082280E`; a request with no row in the
active table returns **102** without reaching the driver.

The AWDL surface is the slots **vptr +0xFC8..+0x11C0** (64; `__ZTV19IO80211AWDLProtocol`;
Broadcom vptr `0xffffff80017f5b78`). In the base they all carry the same stub; Broadcom
overwrites them all (verified in this pass: +0xFC8 → `getSSID` `0xffffff800171a70e`, +0x1180 →
`setCOMPANION_SKYWALK_IF_CREATE` `0xffffff8001722ef2`, +0x11C0 → `getTX_RATE`,
+0x11C8 → the specialized `init` of the subclass). The pure slots an instantiable
subclass must cover: **490 (`setMacAddress(ether_addr&)`, native to `IO80211VirtualInterface`) and 507–569**
([`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md)); the dispatch adds that +0x11C0 (`getTX_RATE`)
also carries a stub in the base ([`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §7) —
minor count discrepancy between both documents (`?`), with no effect on the conclusion: the
subclass must implement the whole surface, not only the interesting types.

The 43 types with slot, struct and length are closed in
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §5 and
[`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §4; another 27 AWDL types only reach the `PeerManager`
(§5 of dispatch). Important consequence: **`wifip2pd` emits no AWDL GET/SET in steady
state** — its 4 `RawGet` call sites (three of type 12 and one of 97) and 2 of `RawSet` (94 and
293) are all of them (`AWDL_STRUCTS.md` §2) — so the surface is exercised by the internal glue
route and other clients, not the daemon.
([`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §2–§7.)

### 1.6 Skywalk datapath of role 6

The interface does not create queues: it requests them from the bus with the descriptor `skywalkInterfaceRegisterParams_t`
(12 qwords; TX count in one byte, AC map u32[], output arrays of queues and pools, headroom u16,
logger) and the bus (`AppleBCMWLANPCIeSkywalk::registerSkywalkInterface`, `0xffffff8001549528`) creates and
returns **4 TX submission queues (one per WME AC, map {0,1,2,3}), 1 TX completion, 1 RX completion
and 1 optional multicast**, all over **a single shared pool** (Broadcom: 4864 packets × 2048
B, `maxBuffersPerPacket = 1`, flags = 1; `allocSkywalkCommonResources`, `0xffffff8001548d2a`). TX:
`dequeuePackets` (`0xffffff80015420f0`) splits by role; RX: `rxCompRingDrain`
(`0xffffff8001549dea`) delivers into the registered RxCompletionQueue, no copies. Effective MTU:
**0x594 (1428) usable after headroom 64** (`RegInfo+0x48/+0x4C`). The family does not enable the queues:
the driver calls `enableDatapath`/`disableDatapath` (`0xffffff8001729afc` / `0xffffff8001729ec6`)
from `enable(unsigned)`/`disable(unsigned)`.
([`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) §2–§10, §14.)

### 1.7 Events: channel, consumption and what is blocking

Main channel: event pipe of `IO80211Family` — `postMessageSync`
(`0xffffff80022f8abc`) writes (id apple80211, payload ≤ 0x1000 B) pairs with a 48 B header into the
ring (`IO80211IOUCEventPipe::writePipe`, `0xffffff80022a7e7c`) and notifies with
`sendAsyncResult64`; canonical names are in `gAppleMIndexToString`
(`0xffffff8002483e80`; 46/47/48/65/195/243 verified in this pass). AWDL frames also travel
as BPF taps (DLT 0xC). `wifip2pd` registers monitoring with
`Apple80211RawP2PEventMonitoringInit` (role 1) or `Apple80211RawEventMonitoringInit` + 16×
`Apple80211StartMonitoringEvent` (role 0, ids 1, 2, 3, 9, 11, 55, 59, 66, 74, 78, 82, 125, 126, 127,
160, 215) and consumes in the master switch `sub_1000388D0` (`w2p 0x1000388d0`). **No event is
a condition of acceptance of `awdl0`** (the semaphore is released by IORegistry, §1.2); what is blocking is that
the **monitoring registration returns 0** — an error chains recovery (`sub_100046E30`,
`w2p 0x100046e30`) which re-creates the interface. In operation the daemon consumes over `awdl0` the ids
**46** (sync state), **47** (peer presence), **48** (RX action frames), **65/142/149/243**
(statistics), **195** (chan seq), 68/172/182/200/234/235; ids 41/42/43, 64, 69, 77, 86, 88,
94, 135, 170, 183, 204 emitted by Broadcom are discarded.
([`AWDL_EVENTS.md`](AWDL_EVENTS.md) §1–§5.)

### 1.8 Companion `llw0` (type 293): optional in stage 1

The `RawSet` 293 (`COMPANION_SKYWALK_IF_CREATE`; `gSetHandlerTable[292]` `0xffffff8002460810` →
handler `0xffffff800228b4b8`; 24 B payload: `code` u32 @+0, `op` u32 @+4 (1 = create), output
name @+8) is only emitted when creating the internal-role-4 interface (`LowLatency`, prefix `llw`)
against an already created and accepted parent: only caller `w2p 0x100048061` (case 4 of the per-role creator),
emitter `sub_10004B300` (`w2p 0x10004b300`, `RawSet` at `w2p 0x10004b3e6`). If it fails, `wifip2pd`
logs two errors and **continues**: the init of `AWDLInterface` absorbs the failure, there is no teardown
of `awdl0`, no `RawSet` 95 nor retry; the only associated wait (30 s, `w2p 0x100048b13`) happens
only if the kext answers success without creating anything. The daemon validates the result with
`if_nametoindex("llw0")` (immediate `0x30776C6C` at `w2p 0x10004b814`). On Broadcom the
implementation (`0xffffff8001722ef2`) creates a custom Skywalk interface with prefix `"llw"`, role 2
(which publishes `IO80211InterfaceRole = "LowLatency"`, table `0xffffff800246bd88[1]` verified) and id 9.
([`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §1–§4.)

## 2. Starting point in the repo

`RTL8814AirLayer` is already a separate kext with declared dependencies on
`com.apple.iokit.IO80211Family` and `IOSkywalkFamily` (`Source/AirLayer/Info.plist:54-72`), compiled
`-fapple-kext` against the local headers (`build_airlayer.sh:20-29`), with version gating
(`Source/AirLayer/VersionGate.h`, `KnownBuilds.inc`). The existing STA path demonstrates the
reusable skeleton: `RTLAirInterface : RTLAirInterfaceStubs : IO80211InfraProtocol`
(`Source/AirLayer/RTLAirLayer.hpp:44-57`), init/attach/start/pools/queues and registration via
`IO80211InfraInterface::registerInfraEthernetInterface` with `initRegistrationInfo` linked by asm
(`Source/AirLayer/RTLAirLayer.cpp:124-130`, `:313-385`, `:1489-1514`); the controller already declares the
virtual `setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data*)` in the local header
(`Source/IO80211/IO80211_25_6_0.h:685`, slot 556 = +0x1160). The layer additionally already answers
`RawGet` 12 on hardware ([`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §2.1, with
reference to [`ARCHITECTURE.md`](ARCHITECTURE.md)); stage 1's new requirement is the value
of byte +0x08 of that response.

## 3. Stage 1 — `awdl0` created and accepted by `wifip2pd`, without peers

Objective: load the kext with the infrastructure support intact and have `wifip2pd` create its AWDL
handle without timeout, without peers and without real datapath. Acceptance criterion: daemon log
`"Found new interface awdl0 with role AirLink"` and absence of
`"Timed out while waiting for interface awdl0"` (`w2p 0x10004974b`); `awdl0` visible with BSD index
and full IORegistry entry.

### 3.1 Requests the kext must answer

| # | Request | Exact contract | Reference |
|---:|---|---|---|
| 1 | `RawGet` **12** `CARD_CAPABILITIES` | `len != 0`; response with `flags[4] bit0 = 1` (byte **+0x08** of the 28 B struct) and bit0 of `flags[9]` (byte +0x0D, NAN gate) at 0; copyback `min(len, 21)` from struct+4 | §1.1; dispatch §4 (GET 12); structs §3.1 |
| 2 | `RawSet` **94** `VIRTUAL_IF_CREATE` | `len == 32`; role u32 @+0x0C == 6; MAC @+0x04..+0x09; create the interface; write BSD name at **payload+0x10** (`strlcpy`, 16 B); return 0; if it already existed, return 17 while writing the name anyway | §1.3; structs §3.2; birth §2.1 |
| 3 | `RawGet` **97** `VIRTUAL_IF_PARENT` | `req_len >= strlen(parent)`; copy `strlen` bytes **without NUL** of the parent's name (`en0`); the daemon sends `len = strlen(awdl0)` and looks for `'\0'` with that cap | POST_CREATE §2.3; structs §3.3 (handler `0xffffff800227b7ff`) |
| 4 | Event monitoring | `Apple80211RawP2PEventMonitoringInit` (awdl0) and, if applicable, `Apple80211RawEventMonitoringInit` + 16× `Apple80211StartMonitoringEvent` must **return 0** | EVENTS §4.1, §5 |
| 5 | ifnet ioctls | `SIOCSIFFLAGS` (up/down), `SIOCSIFLLADDR` (MAC), `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI` — the standard ifnet path | POST_CREATE §2.6; structs §2 |

Any other request may stay without row/handler: the dispatch returns 102 without reaching the driver
(§1.5), and in steady state the daemon emits nothing more (structs §2).

### 3.2 IORegistry properties and registration order (what fires `IOServiceMatched`)

On the entry of the interface object, before matching can fire:

1. `IO80211VirtualInterfaceRole = "AirLink"` — via `setInterfaceRole(6)`
   (`0xffffff80022f60f0`; key `0xffffff800246bdd8[5]`, value `0xffffff800241dbc0`), or published
   directly if the family's init is not used. Birth §4.
2. `IOMACAddress` — 6 B, the MAC of payload+4 (on Broadcom `copyRegistrationInfo` publishes it
   inside `registerEthernetInterface`). Birth §4.
3. `IOInterfaceNamePrefix = "awdl"` (RegInfo+0x30) and `IOInterfaceUnit = 0` (RegInfo+0x38,
   `OSNumber` 32 b). Birth §3.2, §4.
4. Ifnet with prefix `awdl`, unit 0, `IFT_ETHER`, MAC of the payload, `ifnet_attach` done and live
   BSD index (the discoverer checks it with `getifaddrs`/`AF_LINK`, §1.2).
5. **`IOInterfaceName = "awdl0"`** — the matching property. On Broadcom the BSD
   client publishes it inside the synchronous `registerService(2)` (`0xffffff800172982a`) and the republish
   `prepareBSDInterface` (`0xffffff80022f63eb`); in a custom kext without that driver the correct minimum
   order is: properties + ifnet attach + `IOInterfaceName` + `IOInterfaceUnit` + role **before**
   `registerService` (birth §7.3). Synchronous `registerService` (option 2, like Broadcom) leaves
   the full attach before returning control to the handler.
6. `IOInterfaceType = 6` (`IFT_ETHER`), `"BSD Name" = "awdl0"`, `IOSkywalkNexusUUID` —
   the BSD client publishes them on Broadcom; a custom kext publishes the equivalents that apply. Birth §4.

### 3.3 `SIOCGIFMEDIA`: the media word that passes the filter

The discoverer requires `(ifm_current & 0xE0) == 0x80` (`IFM_IEEE80211`; ioctl at `w2p 0x100043cbf`,
filter at `w2p 0x100043ccc`). `SIOCGIFMEDIA` = `0xC02C6938` (legacy variant `0xC0286938`); on
Broadcom `IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`) answers reading the
seeds `ifm_active = 0x80` / `ifm_current = 8` / `ifm_status = 0` from `RegistrationInfo+0x24`,
seeds with which **the filter would not pass** — on a real system `ifm_current` is updated
before the daemon's read, updater not identified (`?`,
[`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §6.3). For the kext the consequence does not
depend on that gap: the project's own `if_ioctl` of `awdl0` must answer `SIOCGIFMEDIA`/
`OSIOCGIFMEDIA` with `ifm_current` and `ifm_active` of type `IFM_IEEE80211` (e.g. `0x80`),
`ifm_status` with `IFM_AVALID` when applicable and a list of 802.11 words.

### 3.4 `RegistrationInfo` and minimum registration datapath

`IOSkywalkEthernetInterface::RegistrationInfo` of **0x130 B**, version 1
(`initRegistrationInfo`, `0xffffff8002b108e2`, validates exactly 1/0x130), with the Broadcom
birth values as ABI reference:
`+0x0C = 3` (subfamily), `+0x14 = 2` (number of media words), `+0x24 = 0x80`
(`IFM_IEEE80211`) and `+0x28 = 8`, `+0x30 = "awdl"`, `+0x38 = 0` (unit), `+0x3A = 64` (u16),
`+0x40 = 4` or `6`, `+0x48/+0x4C = 0x594` (MTU), `+0x108 = MAC` (6 B).
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2, §3; datapath §2, §3.6.)

Registering against Skywalk requires the queues of §1.6: **4 TX submissions (one per AC,
map {0,1,2,3}), 1 TX completion, 1 RX completion**, optional multicast, and **a shared pool**
(`maxBuffersPerPacket = 1`, flags = 1; the Broadcom sizes — 4864×2048, capacities 512/1024 —
are defaults tied to their firmware; a custom driver may choose others respecting the invariants:
packets from the same pool as the queue, `enqueuePackets` batches smaller than the capacity). The registration
is `IOSkywalkEthernetInterface::registerEthernetInterface` (`0xffffff8002b10a84`); the RTL layer uses
today the Infra wrapper with 3 queues, and for role 6 the observed path is direct registration with the
full set. The queues must be enableable by `enable`/`disable` of the interface
(pattern `IO80211WorkQueue::addEventSource` + `IOEventSource` enable/disable +
`requestDequeue/requestEnqueue(0,0)`, datapath §8).

### 3.5 Interface class and creation door

- Door: the override `setVIRTUAL_IF_CREATE` (slot 556 / +0x1160) on the object reachable by the
  `safeMetaCast` to `IO80211InfraProtocol` of the bridge (on Broadcom it lands on
  `AppleBCMWLANCore::setVIRTUAL_IF_CREATE`, `0xffffff80016788f4`). In the layer, `RTLAirController`
  already declares the virtual in the local header; verify that its stub hierarchy makes it castable to
  `IO80211InfraProtocol` (`?` — the exact cast of the receiving object was not re-verified here).
- Class of `awdl0`: own subclass of `IO80211AWDLProtocol` (Broadcom hierarchy:
  `AppleBCMWLANProximityInterface : IO80211AWDLProtocol : IO80211VirtualInterface :
  IO80211SkywalkInterface : IOSkywalkEthernetInterface : IOSkywalkNetworkInterface`). It is
  essential to inherit from `IO80211AWDLProtocol` (not from `IO80211VirtualInterface`) so the
  `safeMetaCast`s of the AWDL bridges reach it; the cost is implementing the pure slots of §1.5
  (490 and 507–569) even if with stubs returning 102/`0xE00002C7` on the not-yet-interesting types.
  Deriving from `IO80211VirtualInterface` alone would **not** receive the AWDL requests and it
  is not verified the family accepts it (`?`).
- Validation of the 94 in the override: `len == 32`, role 6, MAC present; a single AWDL
  interface (Broadcom returns 17 if one already exists, with the name written anyway); full rollback
  (deregister → stop → detach → release) on any failure before writing the name; write
  the name and return 0 only when the ifnet and the properties are published.

### 3.6 What stays out of stage 1

- **Type 293 / `llw0`: not implemented.** The dispatch returns 102 and `wifip2pd` absorbs it without
  touching `awdl0` (§1.8). Worse alternatives: answering success without creating anything costs 30 s per attempt;
  implementing it is stage 4 work.
- **Ioctls `'i'` 130/131 with type 7708:** the dispatch of `IO80211Family` cannot serve them
  (`handleIoctlGated` `0xffffff8002331d20` / `performGatedCommandIOCTL` `0xffffff80022f779a` only
  accept the 200/201 constants and return `0xE00002C7`); nothing to implement
  (POST_CREATE §2.5).
- **Emission of operation events** (46/47/48/195/…): they do not condition acceptance. Only the
  monitoring registration must return 0 (§1.7).

## 4. Stage 2 — synchronization and election (detectable peers, no datapath)

Objective: that the interface takes part in the AWDL synchronization (sync/election state machine) and
exposes coherent state, a prerequisite for peers to appear.

- **AWDL surface by slots.** Contract closed per type in dispatch §5 and structs §4: the
  general ones 1 (SSID), 4 (CHANNEL), 9 (BSSID), 13 (STATE) arrive by cascade at +0xFC8..+0xFE0, and
  the specific ones 116, 117, 118, 122, 125, 126, 129 (400 B), 130, 132, 134, 135, 136, 143, 149,
  151, 155, 158, 164 (184 B), 205, 208, 213, 219, 220, 225, 226, 229 (44 B), 247, 262, 263, 293,
  338, 341, 342, 344, 411, 414, 418, 432, 467 have a GET/SET slot with defined struct and length.
  The PeerManager-only ones (120, 127, 131, 133, 138, 139, 148, 163, 167, 201, 202, 203, 204, 217, 218,
  223, 233, 244, 249, 253, 340, 343, 351, 370, 415, 416, 417, 419, 420) are resolved by the family
  against its own state. Who triggers them: not `wifip2pd` (structs §2) but the internal glue
  route and other clients; the concrete schedule of that stage is `?`.
- **Peer manager.** `IO80211SkywalkInterface::start` (`0xffffff80022f5792`) creates a custom one for
  role ≠ 2; how that family object binds to an external driver's interface
  (ivars +0x120/+0x28) is not verified (`?`, dispatch §8.3) — it is the main open point of the
  stage.
- **Events to emit** (the ones feeding state at the daemon): **46** `AWDL_SYNC_STATE_CHANGED`
  (16 B: u32 state @0 — 1 master, 2 slave, 3 non-election master, 0 down), **47**
  `PEER_PRESENCE` (0x44 B: MAC @0, u32 @8), **195** `AWDL_CHAN_SEQ_CHANGED`, and statistics
  65/142/149/243. The family's helper is `postSyncStateChanged`
  (`0xffffff8002332054`, id 46, len 0); own emission goes through `postMessage` → `postMessageSync`.
- **Radio work** (not covered by the dispatch docs): social channels, availability
  window and election with sync TLVs — the genuinely new part on the RTL8814; the equivalent Broadcom
  pieces (Taskmaster, Commander, Tx Power Manager, defaults of the init at
  `0xffffff8001718db0`) mark the shape. Complementary reference:
  [`AWDL_FIRMWARE_DUTIES.md`](AWDL_FIRMWARE_DUTIES.md).

## 5. Stage 3 — action frames (peer discovery)

- **RX through event 48** `APPLE80211_M_AWDL_ACTION_FRAME_RX`: payload `frameLen + 72` with the
  `apple80211_act_frm_hdr` header (72 B) @0 and the raw frame @72; reference emitter
  `IO80211AWDLPeerManager::postAFToP2pd` (`0xffffff800220fc2c`, id 48 via the virtual +0xB18). The
  daemon's case 48 requires `len >= 0x48` and decodes the 72 B header before the frame
  (EVENTS §4.2). Parallel channel: BPF tap with DLT 0xC (`bpfTapInput` `0xffffff8002331070`; on
  Broadcom `handleActionFrame_rx` `0xffffff800171795c` exercises it).
- **Peer presence:** event **47** with the 0x44 B payload (§4.2 of the daemon: validates MAC @0 and
  u32 @8 == 1; exact semantics of the u32 `?`).
- **TX of action frames:** the Broadcom path is firmware-internal and does not go through an identifiable apple80211
  request in the source documents (`?`) — open design decision: either the
  family asks for it through the internal route (type 120 `AWDL_PEER_SERVICE_REQUEST` or others), or the kext
  needs its own mechanism.
- **Associated peer requests:** 229 (`awdl_peer_advert_add`, 44 B), 164 (traffic registration,
  184 B), 122 (election id), 126 (election metric), 129 (chan sequence, 400 B) — same tables of
  dispatch §5/structs §4.

## 6. Stage 4 — datapath and low latency

- **Real TX through the 4 ACs:** consume from the TxSubmissionQueues, classify by destination (Broadcom:
  flow queues per MAC, max 16, tied to firmware flow rings — `dequeuePackets`
  `0xffffff80015420f0`; the Broadcom SNAP header is specific to its firmware, not imitable) and
  return packets through the TX completion. AWDL multicast: original+duplicate pair for the second
  social channel (`stashMulticastPacketPair`), multicast queue and `triggerAWDLMulticastTx`/`p2pMulticastTx`
  (datapath §9).
- **Real RX:** delivery into the registered RxCompletionQueue (`enqueuePackets`), VLAN/SNAP filters and
  headroom 64; pending and flush (`stagePacket`/`freePendingPackets`/`flushPackets`,
  `sendPendingPacketsToStack` `0xffffff800172a716`) (datapath §10–§11).
- **`llw0` (type 293):** only if the low latency datapath is wanted. Closed contract: 24 B
  payload with `op` @+4 (1 = create), output name @+8, prefix `llw`, role publishing
  `IO80211InterfaceRole = "LowLatency"`, and guarantee that `if_nametoindex("llw0")` resolves
  (LLW §3.2–§4). Destruction through `op = 0` (values other than 0/1 are no-ops returning 0).
- **MTU 0x594 + headroom 64** already fixed in stage 1 become functional here.

## 7. Risks and open questions

- **Pure slots / vtable:** an instantiable subclass of `IO80211AWDLProtocol` must implement slots
  490 and 507–569 (and, per dispatch §7, also +0x11C0) — layouts and signatures are in
  [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) and [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md); calling an
  unimplemented slot aborts (`__cxa_pure_virtual`). The fine count 570 (+0x11C0) differs between
  both documents (`?`).
- **Cast of the controller:** the 94 arrives through `safeMetaCast` to `IO80211InfraProtocol` with slot
  +0x1160; the exact receiving object (controller vs main interface) and the castability of
  `RTLAirController` remain to be verified (`?`).
- **`ifm_current` of Broadcom:** the updater between birth and the daemon's read is not
  identified (`?`, birth §6.3); the design sidesteps it by requiring the correct answer of
  `SIOCGIFMEDIA` in the project's own `if_ioctl` (§3.3).
- **PeerManager ↔ external interface link** (stage 2) and **TX path of action frames** (stage 3):
  open (`?`).
- **Compatibility across macOS builds:** the addresses and offsets of this document belong to KC
  25.6.0; the layer already gates builds, and every version jump requires re-verifying tables and slots.
- **Hardware:** no conclusion implies a test on RTL8814; the stages are validated in that order
  precisely to bound the blast radius of a failure.

## 8. Re-verification of the addresses of this document

Confirmation pass with IDA 9.4 headless (`idat -A -S`) over fresh copies of the IDBs in
`Build/ida_awdl/task_awdl_design/kc/` (`BootKernelExtensions.i64`) and `/p2p/` (`wifip2pd.i64`);
scripts `verify_kc.py`/`verify_p2p.py`, outputs `verify_out.txt` of each directory, logs
`run.log`. Confirmed in this pass: the four table slots (12/97/94/293 with their handlers
`0xffffff8002279e89`, `0xffffff800227b7ff`, `0xffffff80022886a8`, `0xffffff800228b4b8`), the
names of all the cited kernel functions, the Broadcom vtable slots +0xFC8, +0xFD0,
+0xFD8, +0xFE0, +0xFE8, +0x10A0, +0x1180, +0x11B8, +0x11C0, +0x11C8, the literals `"awdl"`
(`0xffffff8001797376`) and the role pairs (`AirLink`/`IO80211VirtualInterfaceRole` index 5,
`LowLatency`/`IO80211InterfaceRole` index 1), the entries 46/47/48/65/195/243 of
`gAppleMIndexToString`, the role read (`0xffffff800167891c`), the `strlcpy` to payload+16
(`0xffffff8001678a2b`), the `registerService(2)` (`0xffffff8001729825`+`0xffffff800172982a`), the
MTU 0x594 write (`0xffffff80017296ff`–`0xffffff8001729709`), and on the daemon the call sites
of `Apple80211RawGet/RawSet` (`0x10004811d`, `0x100048260`, `0x1000483a8`, `0x100046db9`,
`0x100048d95`, `0x10004b3e6`), the `signal` (`0x100046d03`), the 30 s waits (`0x1000491a8`,
`0x100048b13`), the media filter (`0x100043ccc`, mask `0xE0` at `0x100043cd6`), the gate
tests (`0x1000483b5` byte +8; `0x10004812a`/`0x10004826d` byte +0x0D), the roles 6/9/0xA
(`0x100048cc8`/`0x100048c66`/`0x100048c97`), the type 94/len 32 (`0x100048d6d`/`0x100048d77`), the
timeouts 0x1E (`0x10004913b`, `0x100048aa6`), the signals (`0x1000451f2`, `0x100045281`) and the
literal `"llw0"` (`0x10004b814`). Not verified here and cited only through its source document:
everything marked `?` in the text.
