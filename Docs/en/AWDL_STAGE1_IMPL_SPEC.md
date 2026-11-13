# Implementation specification — AWDL stage 1 in RTL8814AirLayer

Implementation document of **stage 1** of the plan of [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
§3: `wifip2pd` sees the AWDL capability, requests `VIRTUAL_IF_CREATE` with role 6, the virtual
interface `awdl0` is born and gets published in the IORegistry with role `AirLink`, and the daemon stops waiting its
30 s. The `llw0` companion (type 293) is out of the mandatory scope and is specified as an
option.

For each piece it fixes: the AirLayer method or handler added or overwritten, the point of the
current code where it hooks (read-only references `Source/AirLayer/<file>:<line>`), the
exact payload layout and return codes, the IO80211Family symbols called
(all exported, [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1–§2), the order and locking, the
failure handling and the tests. No code, build or
tools file was modified to write this document.

Conventions:

- Addresses without prefix = VA of the kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build
  25G241, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`); `w2p` = VA of
  `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`).
- `?` = not verified in `Docs/` nor in the code. Every claim without a section citation or a
  `file:line` carries `?`.
- Slot numbering: [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) counts from the first virtual cell
  with `slot 2 = vptr+0` (offset = `8 × (slot − 2)`); the comments of
  `Source/IO80211/IO80211_25_6_0.h` use `offset = 8 × slot`. The two conventions differ by 2
  positions; both describe the same vtable (the "slot 460" of
  [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) is at offset `0xE50`, which is header label 458).
  In this document **the offset is authoritative**; slot numbers are cited with the source document's convention and the
  equivalence is clarified in §5.2.

## 0. Summary, flow and acceptance criterion

### 0.1 Stage 1 flow

| Step | What happens | Where it is implemented |
|---:|---|---|
| 1 | `wifip2pd` reads `CARD_CAPABILITIES` (type 12) over the infrastructure interface and finds the AWDL bit. | No change: already in `RTLAirController::getCARD_CAPABILITIES` (`Source/AirLayer/RTLAirLayer.cpp:1755`), byte `+0x08` bit 0 at `:1776`. |
| 2 | The daemon creates the per-role handle, emits `RawSet` 94 `VIRTUAL_IF_CREATE` (32 B, role 6, MAC at `+0x04`) and waits 30 s. | New override `RTLAirInterface::setVIRTUAL_IF_CREATE` (§4), over the current stub of `RTLAirInterfaceStubs`. |
| 3 | The kext creates the `awdl0` object, attaches it to the controller and boots it with its `RegistrationInfo`. | New class `RTLAwdInterface : IO80211AWDLProtocol` (§5). |
| 4 | The boot registers the interface on Skywalk, the family's BSD client creates the ifnet and publishes `IOInterfaceName`; `registerService(2)` leaves everything done before returning to the handler. | `RTLAwdInterface::startWithRegistration` + `IOSkywalkEthernetInterface::registerEthernetInterface` + `registerService(kIOServiceSynchronous)` (§5.5, §6). |
| 5 | The handler writes the BSD name at `payload+0x10` and returns 0. | `RTLAirInterface::setVIRTUAL_IF_CREATE` (§4.4). |
| 6 | The `IOServiceMatched` notification of `wifip2pd` finds `IOInterfaceName`, `IO80211VirtualInterfaceRole = "AirLink"` and a live BSD index; releases the semaphore. | Consequence of steps 3–5 (§6). |

### 0.2 Acceptance criterion

1. The daemon's log shows `Found new interface awdl0 with role AirLink` (literal
   `0x100494bb0`, [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.2 step 8) and
   **does not** show `Timed out while waiting for interface awdl0` (`0x100493910`, written at
   `w2p 0x10004974b`, §1.4).
2. `ioreg -r -c RTLAwdInterface -l` shows, on the interface object's entry, the properties of
   §6.2 with the indicated values.
3. An ifnet `awdl0` exists with non-zero BSD index (`if_nametoindex("awdl0") > 0`), MAC
   equal to the 94's payload, MTU 1428 (`0x594`) and 802.11 media.
4. `RawGet` 97 `VIRTUAL_IF_PARENT` returns the parent interface's name without error (§7).
5. No Apple80211 event is an acceptance condition: what is blocking is that the
   monitoring registration returns 0 ([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1.7,
   [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §4.1).
6. The stage emits no traffic or holds peers: the datapath of `awdl0` stays unused (§5.6).

## 1. Starting point in the repository

| Existing piece | State | Reference |
|---|---|---|
| Version gate | Opens only if the structure and build match; `rtlairtrust`/`rtlairnogate` as escapes. `airGateOpen()` is evaluated at controller boot. | `Source/AirLayer/VersionGate.h:91-113`, `Source/AirLayer/RTLAirLayer.cpp:1402` |
| Nub and controller | `RTLAirNub : IOService` publishes the `IO80211WorkQueue`; `RTLAirController : IO80211Controller` (not `IO80211InfraProtocol`). | `Source/AirLayer/RTLAirLayer.hpp:34-42`, `:127-131`, `Source/AirLayer/RTLAirLayer.cpp:1402-1530` |
| Infrastructure interface | `RTLAirInterface : RTLAirInterfaceStubs : IO80211InfraProtocol`, with `IO80211SkywalkInterface::init(ctrl, mac)`, role 1, id 1, `attach(ctrl)`. | `Source/AirLayer/RTLAirLayer.hpp:50-120`, `Source/AirLayer/RTLAirLayer.cpp:311-331` |
| Infra surface stubs | `setVIRTUAL_IF_CREATE` already exists as a stub returning `kIOReturnUnsupported`. | `Source/IO80211/IO80211_25_6_0_infra_stubs.inc:90`, declaration `Source/IO80211/IO80211_25_6_0.h:685` (slot 556, offset `0x1160`) |
| STA interface registration | `RegInfo` of `0x130` B, `IO80211InfraInterface::start`, `createDatapath` with pool and 3 queues, `registerInfraEthernetInterface`, `registerService(kIOServiceAsynchronous)`. | `Source/AirLayer/RTLAirLayer.cpp:334-348`, `:350-388`, `:1502-1513` |
| AWDL capability | Byte `+0x08` bit 0 is already published (and companion bit `+0x07` bit 0) and the NAN gate (`+0x0D` bit 0) is left closed. | `Source/AirLayer/RTLAirLayer.cpp:1749-1794` |
| AWDL skeleton | A signature catalog exists, **not** an ABI declaration: it omits slots 2–489 and 491–506; its own header says so. Not included in the current build. | `Source/IO80211/IO80211AWDL_25_6_0.h:1-6`, `:60-73`, `:75-141` |
| Build | Compiles `RTLAirLayer.cpp` and links `info.o` + `RTLAirLayer.o`. | `build_airlayer.sh:39`, `:63-67` |

## 2. Design decisions

| # | Decision | Reason | State |
|---:|---|---|---|
| D1 | The 94's gate is implemented as an **override in `RTLAirInterface`** (the `IO80211InfraProtocol` object), not on the controller. | The bridge casts to `IO80211InfraProtocol` and calls slot `+0x1160`; the controller does not derive from that protocol. The exact receiving object of the cast is not verified (`?`). | [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1.3, §3.5; `Source/AirLayer/RTLAirLayer.hpp:50`, `Source/IO80211/IO80211_25_6_0.h:595` |
| D2 | `awdl0` is implemented as a subclass of **`IO80211AWDLProtocol`** and not of `IO80211VirtualInterface` alone. | Stage 2 needs the `safeMetaCast`s to `IO80211AWDLProtocol` to reach the object; deriving from the base would not receive the AWDL requests. | §3.5 of the design; [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) |
| D3 | The class declaration goes in a **new file** with the full chain by offsets; the scattered catalog is not used. | With the current catalog the C++ indices do not match the real vtable (the 27 methods `0xE68`–`0xF38` are missing before slot 490 and the 16 reserved after), and the object would be born with the vtable shifted 27 positions. | `Source/IO80211/IO80211AWDL_25_6_0.h:1-6`; [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) rows 463–506 |
| D4 | The 94 is served **inline** in the handler, without thread call nor wait, and `registerService(2)` is synchronous. | It is what Broadcom does (`registerService(2)` at `0xffffff800172982a`) and leaves the ifnet created before returning the name; the daemon needs no further request. Conditional (`?`): the 94 runs under a gate —identified as the BSD client's only on the ioctl route ([`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §5–§6)— and the synchronous `registerService` may wait on that same work loop; T2.2 measures the return and, if it blocks, the asynchronous alternative forces revisiting the name criterion at `payload+0x10`. | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 step 6, §5 |
| D5 | `awdl0`'s `RegistrationInfo` is filled with prefix `awdl`, unit 0, MTU `0x594` and media `0x80`/`0x80`. | The daemon's filter requires `(ifm_current & 0xE0) == 0x80`; the Broadcom seed `8` would not pass and the intermediate updater is not identified (`?`). | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §6.1–§6.3; §3.3 of the design |
| D6 | Type 293 is not implemented in stage 1: the dispatch returns 102 and the daemon absorbs it. | The failure does not contaminate `awdl0`: no teardown or retry, and the 30 s wait only appears if the kext answers success without creating anything. | [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4 |
| D7 | The whole path is enabled with a new boot-arg (`rtlawdl=1`), off by default. | It allows A/B comparison against the current behavior (102) without reloading the kext and bounds the blast radius of a boot failure. | Layer's own practice: `rtlair` at `Source/AirLayer/RTLAirLayer.cpp:1402-1445` |

## 3. Piece A — Capability gate (`RawGet` 12, `CARD_CAPABILITIES`)

### 3.1 Contract

`wifip2pd` opens the AWDL branch with `RawGet` type 12, 21 B buffer (`w2p 0x1000483a8`), and tests
bit 0 of `data[4]` (`w2p 0x1000483b5`). The modern handler copybacks `min(req_len, 21)` bytes
from byte 4 of the 28 B struct, so `data[4]` is **byte `+0x08`**
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1; [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
§1.1). Byte `+0x0D` bit 0 is the NAN gate and must stay at 0.

Layout ([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §7, C definition):

```c
struct apple80211_capability_data {
    uint32_t version;        /* +0x00: 1 */
    uint8_t  flags[24];      /* +0x04: flags[4] bit0 = AWDL, flags[9] bit0 = NAN */
};
```

### 3.2 Current state and needed change

`RTLAirController::getCARD_CAPABILITIES` (`Source/AirLayer/RTLAirLayer.cpp:1755-1794`) already does what
stage 1 needs:

- `p[0..3] = 1`, `p[4..5] = 0xE66F`, `p[6] = 0x61`, `p[7] = 0x04`, `p[9] = 0x40`, `p[10] = 0x04`,
  `p[12..13] = 0x0201` (`:1761-1769`). In `flags` numbering, `p[4]` is `flags[0]` and `p[8]`
  is `flags[4]`.
- `p[7] |= 0x01` and **`p[8] |= 0x01`** (`:1775-1776`): the AWDL gate (byte `+0x08` of the struct) and the
  companion bit Broadcom lights next to it ([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1, note of
  `AppleBCMWLANCore::getCARD_CAPABILITIES`).
- `p[13]` (byte `+0x0D`) does not get bit 0: `p[13] |= 0x08` at `:1781-1783` lights bit 3
  (user capability 75, WPA3/SAE), not the NAN gate. The NAN gate stays closed, as §3.1 of the design requires.

**Code change: none.** The piece only checks that a future modification does not erase
the bit when reordering the block. A one-line comment over `:1776` naming type 12 and the section is proposed, without touching the value.

### 3.3 Test

| Check | Expected |
|---|---|
| Kext load with the gate open | `RTLAirGate = "open-validated: ..."` on node `RTL8814Native` and line `gate open-validated` in the log |
| Request 12 over `en2` before creating anything | Returns 0 and byte 4 of the copyback (byte `+0x08`) equals `0x01` |
| Byte `+0x0D` | Bit 0 at 0 (NAN closed) |
| Daemon log before the 94 | No `Unable to find role for interface: ...` (`0x100494b60`) attributable to missing capability |

`?` No daemon literal explicitly logging "AWDL capability present" is known; the
positive test is that it emits the 94 (next step).

## 4. Piece B — Creation gate (`RawSet` 94, `VIRTUAL_IF_CREATE`)

### 4.1 Handler contract

| Step | Contract | Reference |
|---|---|---|
| Entry | `gSetHandlerTable[93]` → handler `setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`); requires `len == 32` and `data != NULL`. | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2; [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4 |
| WCL route | Tries `sendIOUCToWcl(glue, 0x802869C8, 94, data, 32)`; if WCL does not consume it, falls to the bridge. | same |
| Bridge | `safeMetaCast` to `IO80211InfraProtocol` and call to slot **`+0x1160`** (slot 556). Without cast → `0xE082280E`. | §1.3 of the design |
| Payload | 32 B: `u32 f0 @+0x00` (read `?`), `mac[6] @+0x04`, `pad[2] @+0x0A`, `role u32 @+0x0C`, `ifname[16] @+0x10` (output). | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2, §7 |
| Role | 6 = AWDL. Broadcom: 7 → APSTA, 8..10 → NAN, other → `0xE0000001` + log `Invalid virtual interface role 0x%X name %s`. | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1 |
| Already exists | Returns 17 and **writes anyway** the name at `+0x10` (shared block). | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1, §5 |
| Success | Zero at `+0x10..+0x1F`, `strlcpy(payload+16, getBSDName(), 0x10)` and return 0. | §1.3 of the design |

### 4.2 Hook point

`RTLAirInterface` already inherits slot 556's declaration and its stub:

- Declaration: `Source/IO80211/IO80211_25_6_0.h:685`, inside
  `class IO80211InfraProtocol : public IO80211InfraInterface` (`:595`).
- Current stub: `Source/IO80211/IO80211_25_6_0_infra_stubs.inc:90` (via
  `RTLAirInterfaceStubs`, `Source/AirLayer/RTLAirLayer.hpp:45-52`).
- The object is attached to the controller (`attachInterface(_iface, this)`,
  `Source/AirLayer/RTLAirLayer.cpp:1493`) and registered (`registerService()` at `:1513`).

The override is added to `RTLAirInterface` (new declaration in `RTLAirLayer.hpp`, next to the other
WCL ones at `:56-99`) and implemented in `RTLAirLayer.cpp`. The minimal patch is in §12.3.

`?` The `isCommandProhibited` query (slot `+0xCC8`) is documented for the 63 bridges of the
AWDL surface, not for the 94's bridge, and the slot belongs to the interface, not the controller
([`AWDL_IOCTL_SLOT_MAP.md`](AWDL_IOCTL_SLOT_MAP.md) §1); the current code returns
`false` (`Source/AirLayer/RTLAirLayer.cpp:1726`). That the 94 is exempt from that gate is not
proven: T2.2 decides.

`?` The bridge might cast the object the controller returns by another route (main
interface, `getAwdlInterface()`); which one is not verified. If the cast does not land on
`RTLAirInterface`, the handler returns `0xE082280E` and the daemon creates nothing: test T2 of §14 detects it
immediately. It is also unverified that the WCL route (`sendIOUCToWcl(…, 94, …)`) does not consume
the request before reaching the bridge on this controller (`?`); if it did, the override would not
run and the daemon would stay without interface.

### 4.3 Validation and return codes of the override

| Condition | Return | Side effect |
|---|---|---|
| `req == nullptr` | `kIOReturnBadArgument` (`0xE00002C2`) | Log `awdl: create bad len`; nothing else. The family's handler validates the length (`0xffffff80022886c8`) before the bridge; the override only receives the pointer ([`AWDL_STAGE1_VIRTUAL_IF_PARENT.md`](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) §5) |
| `role == 6` and `rtlawdl` off or gate closed | `kIOReturnUnsupported` (`0xE00002C7`, 102) | Current system behavior; the daemon logs the failure and continues |
| `role` other than 6 and other than 7 | `0xE0000001` | Log `awdl: invalid role %u` (Broadcom uses the same value, §2.1) |
| `role == 7` | `kIOReturnUnsupported` | APSTA is out of stage 1 |
| An AWDL interface already exists | `17` (EEXIST) | `strlcpy(req->ifname, bsdName, 16)`; no other is created |
| Creation or boot fails | The cause's `IOReturn` (`0xE00002BC` if the object's creation fails, `?` others) | Full rollback (§11); the name is **not** written |
| Success | `0` | Name at `+0x10` and `setAwdlInterface()` already done |

### 4.4 Override sequence (implementation)

```text
RTLAirController::createAwdlInterface(mac, out):
  IOLockLock(_awdlLock)
  if _awdl != NULL:
      strlcpy(out, _awdl->bsdName(), 16)
      IOLockUnlock; return 17
  awdl = OSTypeAlloc(RTLAwdInterface)
  if !awdl: unlock; return kIOReturnNoMemory
  if !awdl->initWithController(this, mac, 6, "awdl"): free; unlock; return 0xE00002BC
  if !awdl->startWithRegistration(this): rollback; unlock; return 0xE00002BC
  _awdl = awdl
  rtl_ctrl_setAwdlInterface(this, awdl)           /* IO80211Controller::setAwdlInterface */
  strlcpy(out, awdl->bsdName(), 16)               /* "awdl0" */
  IOLockUnlock
  return 0
```

The name is obtained from the ifnet already created (the family's BSD client composes it inside
`registerService(2)`), not from its own counter: the unit comes from `RegInfo+0x38` and the prefix from
`RegInfo+0x30` ([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §3.1).

## 5. Piece C — Birth and class of `awdl0`

### 5.1 Hierarchy and symbols

Real chain of the object (verified in [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 and
[`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2):

```text
RTLAwdInterface : IO80211AWDLProtocol : IO80211VirtualInterface : IO80211SkywalkInterface
                : IOSkywalkEthernetInterface : IOSkywalkNetworkInterface : IOSkywalkInterface
```

Symbols stage 1 needs (exported with type `0f`, except the caveat at the foot):

| Use | Symbol (mangled) | Address | Source |
|---|---|---|---|
| Build the object | `IO80211AWDLProtocol::gMetaClass`, `C2(OSMetaClass*)`, `D2` | `0xffffff800248cde8`, `0xffffff8002198c04`, `0xffffff8002198c24` | [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §2.2; `Tools/re/check_awdl_symbols.sh:18-20` |
| Init with role and name | `IO80211VirtualInterface::init(IO80211Controller*, ether_addr*, unsigned, char const*)` | `0xffffff80023305f8` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) row 463; §2.3 of the birth |
| Publish the role | `IO80211SkywalkInterface::setInterfaceRole(unsigned)` | `0xffffff80022f60f0` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §4 |
| Interface id | `IO80211SkywalkInterface::setInterfaceId(unsigned)` | `0xffffff80022f950a` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.3 (`setInterfaceId(2)` on Broadcom) |
| Base boot | `IO80211VirtualInterface::start(IOService*)` | `0xffffff80023308aa` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) row 186 |
| `RegInfo` | `IOSkywalkEthernetInterface::initRegistrationInfo(RegistrationInfo*, unsigned, unsigned long)` | `0xffffff8002b108e2` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2 |
| Skywalk registration | `IOSkywalkEthernetInterface::registerEthernetInterface(RegistrationInfo const*, IOSkywalkPacketQueue**, unsigned, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*, unsigned)` | `0xffffff8002b10a84` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 step 4; `Tools/re/check_awdl_symbols.sh:29` |
| Queues and pool | `IOSkywalkPacketBufferPool::withName`, `IOSkywalkTxSubmissionQueue::withPool`, `IOSkywalkTxCompletionQueue::withPool`, `IOSkywalkRxCompletionQueue::withPool` | see `Source/AirLayer/RTLAirLayer.cpp:140-150` | already linked by the STA path |
| Publish the object on the controller | `IO80211Controller::setAwdlInterface(IO80211VirtualInterface*)` / `getAwdlInterface()` | `0xffffff8002299c24` / `0xffffff8002293256` | `Tools/re/check_awdl_symbols.sh:24-25`; [`ARCHITECTURE.md`](ARCHITECTURE.md):1275; [`AWDL_ROLE6_RE.md`](AWDL_ROLE6_RE.md):54 |

`?` `IO80211VirtualInterface::init` does not appear in the selection of
[`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2 (that list is a selection, not the full
inventory: 179 symbols of the class). Its address and type `0f` do appear in
[`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) row 463; prior check P0 of §14 must
confirm the type before compiling.

### 5.2 Vtable declaration: why a new file is needed

`Source/IO80211/IO80211AWDL_25_6_0.h` is a signature catalog, not an ABI declaration: its header
says so (`:1-6`) and it omits slots 2–489 and 491–506. With that declaration, each new `virtual` gets
the next available C++ index, so slot 490 (`setMacAddress`) would land at offset
`0xE68` instead of `0xF40` and the 63 AWDL slots would fall 27 positions lower: the object would be born
with a vtable that is not the family's and any kernel call would land on the wrong
method.

The new declaration is written by **offsets** (`offset = 8 × slot` with the convention of
`IO80211_25_6_0.h`), chaining:

| Block | Offsets | Content | Source |
|---|---|---|---|
| `IO80211VirtualInterface` (overrides) | `0x428`–`0xDC0` (50 slots) | overrides of already existing slots: they do not shift the vtable; each one's only effect is that the cell points to the family's implementation instead of `IO80211SkywalkInterface`'s or `IOService`'s. Full list with each one's exported symbol in [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §5 | real header, `Source/AirLayer/AWDLInterface.h:91-145` |
| `IO80211SkywalkInterface` | up to `0xE60` | already declared in `Source/IO80211/IO80211_25_6_0.h:386-471`; the last declared method is `syncDPSStats` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) row 462 |
| `IO80211VirtualInterface` | `0xE68`–`0xF38` (27 methods) | `init(ctrl, mac, u32, name)`, `setLinkState`, `forwardPacket`, `handleIoctl`, `addPeerToCache`, `deletePeerFromCache`, `handleDebugCmd`, `getPeerSlotDataStats`, `getPeerDataStats`, `clearPeerDataStats`, `setAwdlCurrentChannelSequenceIndex`, `isOutputFlowControlled`, `setOutputFlowControlled`, `clearOutputFlowControlled`, `flushFlowQueues`, `configureAQMOutput`, `attachToBpf`, `configureIfnet`, `reset`, `dupAndTransmitMcastPacket`, `freeMulticastPacket`, `triggerAWDLMulticastTx`, `p2pMulticastTx` (×2), `signalMulticastPackets`, `sendPendingPacketsToStack`, `freePendingPackets` | rows 463–489 |
| `IO80211VirtualInterface` | `0xF40` | `setMacAddress(ether_addr&)`, **pure** | row 490; [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) |
| `IO80211VirtualInterface` | `0xF48`–`0xFC0` (16) | `_RESERVEDIO80211VirtualInterface0..15` | rows 491–506 |
| `IO80211AWDLProtocol` | `0xFC8`–`0x11B8` (63) | slots 507–569, all pure; includes `setCOMPANION_SKYWALK_IF_CREATE` (562) | [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md); rows 507–569 |

**Numbering equivalence**: the labels of `Source/IO80211/IO80211_25_6_0.h:458-460`
(`getLastTxTimeStamp` = 458, `getLastRxTimeStamp` = 459, `syncDPSStats` = 460) and slots 460–462 of
[`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) describe the same three methods at offsets
`0xE50`, `0xE58` and `0xE60`; the difference is only the convention. The last method declared by the
header lands at `0xE60`, so the first new method (`init`) goes at `0xE68` and the 27
methods of `IO80211VirtualInterface` occupy `0xE68`–`0xF38`. `?` The full chain has not been
contrasted method by method against the vtable dump: check P0.3 of §14 verifies the
count and the limit before compiling.

**Compilation rule**: the new file declares `IO80211VirtualInterface` and `IO80211AWDLProtocol`
and **must not be included together with `IO80211AWDL_25_6_0.h`** (duplicate definitions of the same class).
The existing catalog is not modified nor included.

### 5.3 `RegistrationInfo` of `awdl0`

Struct of `0x130` B, version 1, validated by `initRegistrationInfo`
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2, §3.4). Values for stage 1:

| Offset | Width | Value | Origin |
|---:|---:|---|---|
| `+0x00` | 4 | `version = 1` | `initRegistrationInfo` |
| `+0x0C` | 4 | `3` (`IFNET_SUBFAMILY_WIFI`) | birth §2.2 (`0xffffff800165de3c`) |
| `+0x10` | 4 | `6` (field without reader: `initRegistrationInfo` leaves it 0 and nothing reads it in the kernelcache, [`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §3; the value 6 is harmless) | `Source/AirLayer/RTLAirLayer.cpp:1506` |
| `+0x14` | 4 | `2` (number of media words) | STA path (`Source/AirLayer/RTLAirLayer.cpp:210`, `:1507`); `initRegistrationInfo` writes 256 ([`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §2) |
| `+0x24` | 4 | `0x80` (`IFM_IEEE80211`) | birth §2.2; design §3.4 |
| `+0x28` | 4 | **`0x80`** (`ifm_current`; Broadcom puts `8`) | deliberate deviation D5; birth §6.3 |
| `+0x30` | 8 | pointer to the literal `"awdl"` | birth §3.1 |
| `+0x38` | 4 | `0` (unit) | birth §3.1 |
| `+0x3C` | 2 | `64` (u16, headroom) | STA path (`Source/AirLayer/RTLAirLayer.cpp:212`, `:1509`) and `createProxInterface` (`0xffffff800165de8c`, `word [r12+3Ch] = 40h`, [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §6.1); `?` reader not identified |
| `+0x40` | 4 | `0` (field without reader: nothing reads it, [`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §2; the STA uses 0, Broadcom 4 or 6 depending on a property) | `Source/AirLayer/RTLAirLayer.cpp:1510` |
| `+0x48` / `+0x4C` | 4 / 4 | `0x594` (MTU 1428) | birth §2.4 step 4 (`0xffffff80017296ff`) |
| `+0x108` | 6 | MAC of the 94's `payload+0x04` | birth §2.2; [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2 |

`?` Headroom: [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2 places it at `+0x3A`, but that
offset is the high half of the unit at `+0x38` —writing 64 there leaves the unit at `0x00400000` and the
real headroom at 0 ([`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §6.1)— and the reference
write (`createProxInterface`, `0xffffff800165de8c`) goes at `+0x3C`. No reader of `+0x3C`
appears in the published sweeps.

Filling precedent in the code: `Source/AirLayer/RTLAirLayer.cpp:1502-1511` (same struct for
role 1). `awdl0`'s `RegInfo` is the object's own and survives until its `free()`
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 note on the pointer's retention).

### 5.4 Queues and pool

Broadcom registers 4 TX submission queues (one per AC, map `{0,1,2,3}`), 1 TX completion, 1 RX
completion and an optional multicast, with a shared pool ([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
§3.4; [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) §2–§10). Stage 1 **does
not transmit nor receive**: the datapath is unused. To minimize surface, the same shape
already working on the STA path is registered —1 TX submission, 1 TX completion, 1 RX completion, one pool per
direction— with the documented invariants (an `enqueuePackets` batch smaller than the capacity; the
queue's packets come from its pool). [`AWDL_STAGE1_QUEUE_SHAPE.md`](AWDL_STAGE1_QUEUE_SHAPE.md)
§1, §5 and §10 close the shape: one TX submission queue is the minimum the family accepts, the TX
completion is not consumed by the BSD client, the RX queue count is not validated and no check
looks at the role; the plan B of the full set of §3.4 of the design is not needed.

The TX submission requires a non-null action: `IOSkywalkTxSubmissionQueue::initWithPool`
(`0xffffff8002b0caac`) rejects it and `withPool` returns null, leaving the boot without
`awdl0` ([`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §1). The queues are born enabled
(`IOEventSource::init`, `0xffffff8000aca900`, leaves `enabled = true`, and the code does not
disable them): the plan either passes the no-op action of CODE_FIXES §1.2 to the TX submission —it consumes the batch and
returns it through the TX completion queue; returning 0 would stall the queue, because `legacyDequeue`
(`0xffffff8002b2c860`) advances its ring by the return— or disables them, and while there
is no draining it does not add them as event sources. The stage 1 code (CODE_FIXES §1.2, §2) passes the
no-op action and keeps the three queues in the work loop until `teardownDatapath`; T3.5 exercises
that set. The registration pattern is at
`Source/AirLayer/RTLAirLayer.cpp:374-388` and `:515-535`.

### 5.5 Boot sequence (exact order)

```text
initWithController(ctrl, mac, role=6, name="awdl"):
  1. init()                                   /* IOService */
  2. IO80211VirtualInterface::init(ctrl, &ea, 6, "awdl")
        -> publishes IO80211VirtualInterfaceRole = "AirLink"
  3. setInterfaceId(2)                        /* Broadcom: 2 if the core version >= 5 */
  4. attach(ctrl)                             /* IOService::attach; entry hanging, unregistered */

startWithRegistration(ctrl):
  5. IO80211VirtualInterface::start(this)     /* base: PMinit/joinPMtree, peer manager, MAC agent */
  6. initRegistrationInfo(reg, 1, 0x130) + filling of §5.3
  7. create pool + 3 queues, addEventSource on the work queue
  8. registerEthernetInterface(reg, queues, 3, txPool, rxPool, 0)
        -> publishes IOMACAddress, IOInterfaceNamePrefix, IOInterfaceUnit and the media state
  9. registerService(kIOServiceSynchronous)
        -> inside: the family's BSD client composes "awdl0", publishes "BSD Name" and
           IOInterfaceName, creates the ifnet and does ifnet_attach
 10. setRunningState(false)
```

Step 11 (`setAwdlInterface(awdl)` on the controller) is done by the handler invoking the boot,
not by the boot itself (§4.4): that way the pointer is only published when the object is already booted.

Steps 2–4 follow [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.3; steps 5–10,
§2.4 and §5. `registerService(2)` leaves the ifnet created before returning control to the handler
(§5 of the birth): when `setVIRTUAL_IF_CREATE` writes the name and returns 0, `awdl0` already exists
and the notification is on its way or delivered.

`?` D4 stays conditional: the 94 runs under a gate —the BSD client's on the ioctl route
([`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §5–§6); on the user
client route the gate is not identified— and the synchronous `registerService(2)` may wait on that same
work loop. T2.2 measures the return; if it blocks, the asynchronous alternative forces revisiting the
name criterion at `payload+0x10` (§0.1 step 5).

### 5.6 What stage 1 does **not** do

- No TX nor RX: the queues stay without a useful action and `enable`/`disable` only log the state.
- No own peer manager is created nor action frames processed ([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
  §4–§5).
- Events 46/47/48/195 are not emitted ([`AWDL_EVENTS.md`](AWDL_EVENTS.md) §2–§5).
- `setMacAddress(ether_addr&)` (slot 490) accepts and does not change the MAC: the MAC is already fixed in the
  `RegInfo` and in the ifnet.

## 6. Piece D — IORegistry publication, order and `SIOCGIFMEDIA`

### 6.1 Who publishes what

All properties go on the entry of the **interface object**, not the controller
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §4):

| Property | Expected value | Written by | Moment |
|---|---|---|---|
| `IO80211VirtualInterfaceRole` | `"AirLink"` | `setInterfaceRole(6)` (role table, index 5) | step 2, before registering |
| `IOMACAddress` | 6 B (MAC of the 94) | `registerEthernetInterface` → `copyRegistrationInfo` | step 8 |
| `IOInterfaceNamePrefix` | `"awdl"` | `registerNetworkInterface` from `RegInfo+0x30` | step 8 |
| `IOInterfaceUnit` | `0` (OSNumber 32 b) | same, from `RegInfo+0x38` | step 8 |
| `"BSD Name"` | `"awdl0"` | family's BSD client | inside step 9 |
| `IOInterfaceName` | `"awdl0"` | BSD client; `prepareBSDInterface` republishes it | inside step 9, before `ifnet_attach` |
| `IOInterfaceType` | `6` (`IFT_ETHER`) | BSD client | end of step 9 |
| `IOSkywalkNexusUUID` | UUID of the nexus | BSD client | end of step 9 |

**Order rule**: role, MAC, prefix, unit, attached ifnet and `IOInterfaceName` must exist **before**
the notification can fire; in this implementation that holds because everything happens
inside the synchronous `registerService(2)` of step 9, except the role (step 2). An asynchronous
`registerService` would leave the name unwritten when the handler returns 0 and the daemon could read an
empty `payload+0x10` (`?` how long matching takes in that case).

### 6.2 Verification in the IORegistry

```text
ioreg -r -c RTLAwdInterface -l
  +-o RTLAwdInterface  <class RTLAwdInterface, ...>
      "IO80211VirtualInterfaceRole" = "AirLink"
      "IOMACAddress" = <...>
      "IOInterfaceNamePrefix" = "awdl"
      "IOInterfaceUnit" = 0
      "IOInterfaceName" = "awdl0"
      "BSD Name" = "awdl0"
      "IOInterfaceType" = 6
```

`?` The exact class name in the IORegistry is that of the object's own metaclass
(`RTLAwdInterface`); it is not verified that the family does not impose a `classNameOverride`
([`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) row 319: `classNameOverride`, slot 319).

### 6.3 `SIOCGIFMEDIA`

The discoverer requires `(ifm_current & 0xE0) == 0x80` (`SIOCGIFMEDIA = 0xC02C6938`; ioctl at
`w2p 0x100043cbf`, filter at `w2p 0x100043ccc`). The family's `ioctl_gifmedia` copies
`ifm_active ← state+48`, `ifm_current ← state+52`, `ifm_status ← state+56`, and those fields are
seeded from `RegInfo+0x24` and `RegInfo+0x28`
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §3.2 step 5, §6.2). With `+0x24 = +0x28 = 0x80`
the answer passes the filter without writing any own `if_ioctl`. The legacy variant
`OSIOCGIFMEDIA = 0xC0286938` follows the same path.

The closure is in [`AWDL_STAGE1_SIOCSIFMEDIA.md`](AWDL_STAGE1_SIOCSIFMEDIA.md) §5 and §7: the media
descriptor is seeded at registration (`+0x30` and `+0x34` from `RegInfo+0x24`/`+0x28`,
`0xffffff8002b0a32a`/`0x2b0a332`) and the only later writer, `ioctl_sifmedia` (head
`0xffffff8002b0affa`), fails before the store on `awdl0` (no `IOMediumDictionary`): `0x80` is
preserved.

## 7. Piece E — `VIRTUAL_IF_PARENT` (type 97)

### 7.1 Contract

`RawGet` 97 over `awdl0`: handler `getVIRTUAL_IF_PARENT` (`0xffffff800227b7ff`,
`gGetHandlerTable[96]`) obtains the virtual interface's parent (`safeMetaCast` to
`IO80211VirtualInterface`), takes its BSD name (`IO80211SkywalkInterface::getBSDName`), requires
`req_len >= strlen(name)` and copies `strlen` bytes **without NUL** into `req_data`
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.3). The daemon sends `len = strlen("awdl0")` and parses
looking for `'\0'` with that cap (`sub_100046D30`, `w2p 0x100046db9`).

### 7.2 Implementation

**No code is added**: the handler belongs to the family and works with any attached
`IO80211VirtualInterface`. The requirements fall on piece C:

1. The object must really be an `IO80211VirtualInterface` (that is why D2 and D3); a bare
   `IO80211InfraProtocol` object does not pass the `safeMetaCast` and the handler returns `0xE082280E`.
2. The parent's name is that of the controller / infrastructure interface (`en2` on this system,
   `en0` in the document's reference).

The old requirement of publishing the object with `setAwdlInterface()` is withdrawn: the 97 reads
`getPrimarySkywalkInterface` (controller block `+0x188`, written only by `attachInterface`
for role 1) and does not consult `getAwdlInterface`; the primary is already attached
(`Source/AirLayer/RTLAirLayer.cpp:1493`) and `setAwdlInterface` has no reference in the
kernelcache ([`AWDL_STAGE1_VIRTUAL_IF_PARENT.md`](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) §3–§7).

Expected answer in the 16 B buffer: `en2` (3 bytes) or whatever the infrastructure interface's name is, without
NUL and with the rest intact.

### 7.3 Risk and test

`?` It is not verified that the daemon signals the semaphore if `RawGet` 97 fails; the static
sequence places it before the signal (`0x100044728` → `0x1000451f2`). That is why stage 1 treats it
as a requirement. Test T5 verifies it indirectly: absence of timeout and presence of
`Found new interface awdl0 with role AirLink`.

## 8. Piece F — Companion `llw0` (type 293), optional

### 8.1 Contract, if implemented

`RawSet` 293 (`gSetHandlerTable[292]` → `0xffffff800228b4b8`), 24 B, emitted once by
`sub_10004B300` (`w2p 0x10004b3e6`) against the already accepted parent's handle
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.4; [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4):

| Offset | Field | Semantics |
|---:|---|---|
| `+0x00` | `code` u32 | propagated from the caller; the kernel does not read it; value `?` |
| `+0x04` | `op` u32 | `1` = create; `0` = destroy; other = no-op returning 0 |
| `+0x08` | `name[16]` | output: BSD name of the companion (`llw0`), written only on success |

Kernel precedent: create a Skywalk interface with prefix `llw`, role 2 (which publishes
`IO80211InterfaceRole = "LowLatency"`), id 9, and write the name at `payload+8`
([`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §3.2–§3.3). The daemon validates with
`if_nametoindex("llw0")` (`w2p 0x10004b814`).

### 8.2 Decision for stage 1 (D6)

Not implemented: the dispatch returns **102** for lack of a useful row for the interface and the daemon
absorbs it. Consequences verified in [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4:

- two error lines in the daemon's log (`Failed to create low latency interface for %s ...`,
  `0x100494cf0`, and the epilogue `0x100048fb5`) and the init of `AWDLInterface` continues;
- **no** teardown of `awdl0`, nor `RawSet` 95, nor retry;
- the llw-specific 30 s wait does not appear; it only runs if the kext answers success without
  a verifiable name (`w2p 0x100048b13`, timeout `0x100048beb`);
- answering success without creating anything is worse: it costs 30 s per attempt.

The alternative (implementing `llw0`) belongs to stage 4 of the design (§6).

## 9. Piece G — Event monitoring registration

`wifip2pd` registers monitoring through two paths and **aborts on the first error**
([`AWDL_EVENTS.md`](AWDL_EVENTS.md) §4.1):

- role 1 (`awdl0`): `Apple80211RawP2PEventMonitoringInit` (`sub_100041F20`);
- role 0: `Apple80211RawEventMonitoringInit` + 16 × `Apple80211StartMonitoringEvent` (ids 1, 2, 3,
  9, 11, 55, 59, 66, 74, 78, 82, 125, 126, 127, 160, 215).

No event is an acceptance condition; what is blocking is that the registration returns 0. The route is the
user client / event pipe of the family (`createEventPipe`, slot 440 = `0xDC0`; `postMessageSync` at
`0xffffff80022f8abc`), and the gate is already in the family:
`IO80211VirtualInterface::createEventPipe` (`0xffffff80023347fc`) covers role 6 and requires the
entitlement `com.apple.wifi_awdl.event_monitor`, with `0xE00002E2` as the answer without it
([`AWDL_STAGE1_DISPATCH_PATH.md`](AWDL_STAGE1_DISPATCH_PATH.md) §2).

**Code change: none planned.** `?` It is not verified that the family's event pipe
accepts an external driver's interface just like that. `?` The recovery the design cites —an error
chains `sub_100046E30`, which re-creates the interface
([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1.7)— does not appear as an abort branch in
[`AWDL_STAGE1_STUB_VALUES.md`](AWDL_STAGE1_STUB_VALUES.md) §4; T7.1 decides. If the registration fails,
the review touches the user client, not the 94 handler.

## 10. Order, locking and lifecycle

### 10.1 Execution context

| Path | Context | Rule |
|---|---|---|
| `setVIRTUAL_IF_CREATE` | caller's thread, under a gate: the BSD client's on the ioctl route ([`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §5–§6); on the user client route there is also a gate, unidentified (`?`) | create and boot inline; no thread calls nor waits; D4 conditional (§5.5) |
| `registerService(2)` | inside the handler; runs the BSD client synchronously | must not be called with the work queue's gate closed by the same thread (`?` the user client route's gate; T2.2 measures the return) |
| `enable`/`disable` | work queue (`runAction`) | they touch no AWDL state |
| `stop`/`free` of the controller | IOKit termination thread | they tear down `awdl0` before freeing the controller |

The code already documents the inverse risk: `postMessage` must run inside the gate
(`Source/AirLayer/RTLAirLayer.cpp:1288-1305`) and the RX hook cannot take it under the radio's
lock (`:1213-1222`). The creation of `awdl0` takes part in neither: it emits no events and
touches no radio.

### 10.2 Locks

- `_awdlLock` (new `IOLock`, allocated in `RTLAirController::start` next to `_lock`/`_rxLock`,
  `Source/AirLayer/RTLAirLayer.cpp:1443-1449`): serializes creation, the "already exists" query and
  destruction. Taken **before** looking at `_awdl` and released after writing the name and
  `setAwdlInterface()`.
- Existing `_lock`: not used for the AWDL path; it already protects the scan/join phase.
- `_rxLock`: not touched.

### 10.3 Lifecycle

| Event | Action |
|---|---|
| `rtlawdl=0` or gate closed | the handler returns 102; nothing is created |
| First 94 with role 6 | create, boot, publish, save pointer |
| Second 94 with role 6 | return 17 with the name |
| `stop(IOService*)` of the controller | `setAwdlInterface(NULL)`; `_awdl->terminate()`; `OSSafeReleaseNULL(_awdl)`; free `_awdlLock` in `free()` |
| Failure mid-creation | rollback of §11 before returning the error |
| Kext unload | the ifnet disappears with the service; the daemon receives `IOServiceTerminate` and cleans the handle without signaling (`sub_100045520`, [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.1) |

## 11. Failure handling and rollback

| Failure point | Action | Return to the daemon | Expected log |
|---|---|---|---|
| Null pointer (the length is validated by the family's handler, §4.3) | nothing | `0xE00002C2` | `awdl: create bad len` |
| Unsupported role | nothing | `0xE0000001` (roles ≠ 6,7) or `0xE00002C7` (role 7) | `awdl: invalid role %u` |
| `OSTypeAlloc` fails | nothing | `kIOReturnNoMemory` | `awdl: alloc failed` |
| `initWithController` fails (init, role, attach) | `terminate()` + release | `0xE00002BC` | `awdl: init failed` |
| `initRegistrationInfo` fails | stop queues, detach, release | `0xE00002BC` | `awdl: reginfo failed` |
| Pool or queues fail | same | `kIOReturnNoResources` | `awdl: queues failed` |
| `registerEthernetInterface` fails | remove queues from the work queue, `deregisterEthernetInterface` (if it got registered), detach, release | the received `IOReturn` | `awdl: register 0x%x` |
| `registerService(2)` does not create the ifnet | full rollback; the name is **not** written | `0xE00002BC` | `awdl: no bsd name` |
| Empty name after boot | full rollback | `0xE00002BC` | `awdl: empty name` |
| Failure in controller `stop` | continue the teardown even if the step fails; never leave `_awdl` pointing to freed memory | — | `awdl: teardown` |

Common rule: the name is written at `payload+0x10` **only** when the ifnet and the properties are
published; any earlier path leaves `ifname` at zero (the buffer arrives zeroed from the daemon,
[`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2).

## 12. C++ skeleton

Proposed new files: `Source/AirLayer/AWDLInterface.h` and `Source/AirLayer/AWDLInterface.cpp`.
The hooking patch on existing files is in §12.3 and **is not applied** in this stage.

### 12.1 `Source/AirLayer/AWDLInterface.h`

```cpp
/*
 * RTL8814AirLayer — stage 1 of AWDL: declaration of the vtable chain of
 * IO80211VirtualInterface / IO80211AWDLProtocol and of class RTLAwdInterface.
 *
 * The chain is declared complete and anchored to offsets (offset = 8 * slot, the
 * convention of Source/IO80211/IO80211_25_6_0.h). It must not be included along
 * Source/IO80211/IO80211AWDL_25_6_0.h: that catalog omits slots and its C++
 * indices do not match the real vtable.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL_AWDL_INTERFACE_H
#define RTL_AWDL_INTERFACE_H

#include "RTLAirLayer.hpp"

/* ---- AWDL structures used only as pointer in the vtable ---- */
struct apple80211_ssid_data;
struct apple80211_channel_data;
struct apple80211_bssid_data;
struct apple80211_state_data;
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
struct apple80211_p2p_coex_protection;
struct apple80211_awdl_psf_dwell_params;
struct apple80211_awdl_config_request;
struct apple80211_rtg_peer_stats_cfg;

/* ---- Lifecycle payloads (Docs/AWDL_STRUCTS.md §7) ---- */
struct apple80211_virt_if_create_data {
    uint32_t f0;             /* +0x00: sent by wifip2pd, read ?        */
    uint8_t  mac[6];         /* +0x04: requested MAC                         */
    uint8_t  pad0[2];        /* +0x0A                                         */
    uint32_t role;           /* +0x0C: 6 = AWDL                                */
    char     ifname[16];     /* +0x10: output, created BSD name              */
};
static_assert(sizeof(apple80211_virt_if_create_data) == 32, "virt_if_create_data");

/* Pool options: same shape as Source/AirLayer/RTLAirLayer.cpp:196-204. */
struct rtl_pool_options {
    uint32_t packetCount;
    uint32_t bufferCount;
    uint32_t bufferSize;
    uint32_t maxBuffersPerPacket;
    uint32_t reserved0;
    uint32_t poolFlags;
    void    *dmaSpec;
    uint64_t reserved1;
};

/* ---- Symbols of IO80211Family / IOSkywalkFamily used by stage 1 ---- */
extern "C" {
bool rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
    __asm("__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm");
void rtl_sky_setId(void *iface, uint32_t id)
    __asm("__ZN23IO80211SkywalkInterface14setInterfaceIdEj");
void *rtl_sky_getWorkQueue(void *iface)
    __asm("__ZN23IO80211SkywalkInterface12getWorkQueueEv");
void *rtl_pool_withName(const char *name, OSObject *owner, uint32_t type, const void *opts)
    __asm("__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE");
void *rtl_txsub_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                         void *action, void *refCon, uint32_t opts)
    __asm("__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j");
void *rtl_txcomp_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                          void *action, void *refCon, uint32_t opts)
    __asm("__ZN26IOSkywalkTxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
void *rtl_rxcomp_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                          void *action, void *refCon, uint32_t opts)
    __asm("__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
bool rtl_vif_init(void *self, IO80211Controller *ctrl, ether_addr *mac, uint32_t role,
                  const char *name)
    __asm("__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc");
uintptr_t rtl_vif_start(void *self, IOService *provider)
    __asm("__ZN23IO80211VirtualInterface5startEP9IOService");
IOReturn rtl_ctrl_setAwdlInterface(IO80211Controller *ctrl, void *awdl)
    __asm("__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface");
void *rtl_ctrl_getAwdlInterface(IO80211Controller *ctrl)
    __asm("__ZN17IO80211Controller16getAwdlInterfaceEv");
IOReturn rtl_eth_register(void *self, void *regInfo, void **queues, uint32_t n,
                          void *txPool, void *rxPool, uint32_t flags)
    __asm("__ZN26IOSkywalkEthernetInterface25registerEthernetInterfaceEPKNS_16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_j");
}

/* ---- Declaration of the surface missing in IO80211_25_6_0.h ---- */
/* The last method declared by IO80211SkywalkInterface is syncDPSStats
 * (offset 0xE60). From there, this chain reproduces the real vtable. */
class IO80211VirtualInterface : public IO80211SkywalkInterface {
    OSDeclareAbstractStructors(IO80211VirtualInterface)
public:
    /* Overrides of already existing slots: they do not shift the vtable. */
    virtual void free() override;
    virtual void stop(IOService *) override;
    /* 50 more overrides of already existing slots (`0x428`–`0xDC0`), declared without
     * local body: each cell resolves to the exported symbol of
     * IO80211VirtualInterface and does not shift the vtable. The full list, with
     * each one's exported symbol, is in AWDL_STAGE1_CODE_FIXES.md §5. */
    using IO80211SkywalkInterface::init;   /* avoids hiding IOService's init() */
    virtual bool init(IO80211Controller *, ether_addr *, uint32_t, const char *);   /* 0xE68 */
    virtual uintptr_t setLinkState(uint32_t, uint32_t);                            /* 0xE70 */
    virtual uintptr_t forwardPacket(void *);                                       /* 0xE78 */
    virtual uintptr_t handleIoctl(void *, unsigned long, void *);                   /* 0xE80 */
    virtual uintptr_t addPeerToCache(unsigned char *);                             /* 0xE88 */
    virtual uintptr_t deletePeerFromCache(unsigned char *);                        /* 0xE90 */
    virtual uintptr_t handleDebugCmd(void *);                                      /* 0xE98 */
    virtual uintptr_t getPeerSlotDataStats(ether_addr *, bool, void *);            /* 0xEA0 */
    virtual uintptr_t getPeerDataStats(ether_addr *, bool, void *);                /* 0xEA8 */
    virtual uintptr_t clearPeerDataStats(ether_addr *, bool);                      /* 0xEB0 */
    virtual uintptr_t setAwdlCurrentChannelSequenceIndex(uint32_t);                /* 0xEB8 */
    virtual uintptr_t isOutputFlowControlled();                                    /* 0xEC0 */
    virtual uintptr_t setOutputFlowControlled();                                   /* 0xEC8 */
    virtual uintptr_t clearOutputFlowControlled();                                 /* 0xED0 */
    virtual uintptr_t flushFlowQueues(ether_addr *);                               /* 0xED8 */
    virtual uintptr_t configureAQMOutput();                                        /* 0xEE0 */
    virtual uintptr_t attachToBpf();                                               /* 0xEE8 */
    virtual uintptr_t configureIfnet();                                            /* 0xEF0 */
    virtual uintptr_t reset();                                                     /* 0xEF8 */
    virtual uintptr_t dupAndTransmitMcastPacket(void *, unsigned char *,
                                                unsigned char *);                  /* 0xF00 */
    virtual uintptr_t freeMulticastPacket(void *);                                 /* 0xF08 */
    virtual uintptr_t triggerAWDLMulticastTx(uint32_t, unsigned char);             /* 0xF10 */
    virtual uintptr_t p2pMulticastTx(void **, uint32_t);                           /* 0xF18 */
    virtual uintptr_t p2pMulticastTx(uint32_t, bool);                              /* 0xF20 */
    virtual uintptr_t signalMulticastPackets(uint32_t, uint32_t);                  /* 0xF28 */
    virtual uintptr_t sendPendingPacketsToStack(void **, uint32_t);                /* 0xF30 */
    virtual uintptr_t freePendingPackets(void **, uint32_t);                       /* 0xF38 */
    virtual uintptr_t setMacAddress(ether_addr &) = 0;                             /* 0xF40: PURE */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface0();                         /* 0xF48 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface1();                         /* 0xF50 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface2();                         /* 0xF58 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface3();                         /* 0xF60 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface4();                         /* 0xF68 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface5();                         /* 0xF70 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface6();                         /* 0xF78 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface7();                         /* 0xF80 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface8();                         /* 0xF88 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface9();                         /* 0xF90 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface10();                        /* 0xF98 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface11();                        /* 0xFA0 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface12();                        /* 0xFA8 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface13();                        /* 0xFB0 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface14();                        /* 0xFB8 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface15();                        /* 0xFC0 */
private:
    uint8_t _rtlAwdPad[0x8];   /* metaclass 0x130: 0x128 of Skywalk + 0x8 */
};

class IO80211AWDLProtocol : public IO80211VirtualInterface {
    OSDeclareAbstractStructors(IO80211AWDLProtocol)
public:
    virtual IOReturn getSSID(apple80211_ssid_data *) = 0;                          /* 0xFC8 */
    virtual IOReturn getCHANNEL(apple80211_channel_data *) = 0;
    virtual IOReturn getBSSID(apple80211_bssid_data *) = 0;
    virtual IOReturn getSTATE(apple80211_state_data *) = 0;
    virtual IOReturn getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) = 0;
    virtual IOReturn getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) = 0;
    virtual IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        apple80211_awdl_extension_state_machine_parameter *) = 0;
    virtual IOReturn getAWDL_ELECTION_ID(apple80211_awdl_election_id *) = 0;
    virtual IOReturn getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) = 0;
    virtual IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        apple80211_awdl_sync_channel_sequence *) = 0;
    virtual IOReturn getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size *) = 0;
    virtual IOReturn getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) = 0;
    virtual IOReturn getAWDL_SYNC_STATE(apple80211_awdl_sync_state *) = 0;
    virtual IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(
        apple80211_awdl_election_rssi_thresholds *) = 0;
    virtual IOReturn getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) = 0;
    virtual IOReturn getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) = 0;
    virtual IOReturn getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities *) = 0;
    virtual IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(
        apple80211_awdl_secondary_master_channel *) = 0;
    virtual IOReturn getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) = 0;
    virtual IOReturn getAWDL_MIN_RATE(apple80211_awdl_min_rate *) = 0;
    virtual IOReturn getAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) = 0;
    virtual IOReturn getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) = 0;
    virtual IOReturn getAWDL_DEV_STATS(apple80211_awdl_dev_stats *) = 0;
    virtual IOReturn getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) = 0;
    virtual IOReturn getAWDL_CCA(apple80211_awdl_cca *) = 0;
    virtual IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap *) = 0;
    virtual IOReturn getLLW_PARAMS(apple80211_llw_params *) = 0;
    virtual IOReturn setSSID(apple80211_ssid_data *) = 0;
    virtual IOReturn setCHANNEL(apple80211_channel_data *) = 0;
    virtual IOReturn setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) = 0;
    virtual IOReturn setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) = 0;
    virtual IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        apple80211_awdl_extension_state_machine_parameter *) = 0;
    virtual IOReturn setAWDL_ELECTION_ID(apple80211_awdl_election_id *) = 0;
    virtual IOReturn setAWDL_BSSID(apple80211_awdl_bssid *) = 0;
    virtual IOReturn setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) = 0;
    virtual IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        apple80211_awdl_sync_channel_sequence *) = 0;
    virtual IOReturn setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) = 0;
    virtual IOReturn setAWDL_SYNC_STATE(apple80211_awdl_sync_state *) = 0;
    virtual IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(
        apple80211_awdl_election_rssi_thresholds *) = 0;
    virtual IOReturn setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) = 0;
    virtual IOReturn setPEER_CACHE_CONTROL(apple80211_peer_cache_control *) = 0;
    virtual IOReturn setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *) = 0;
    virtual IOReturn setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *) = 0;
    virtual IOReturn setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) = 0;
    virtual IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(
        apple80211_awdl_peer_traffic_registration *) = 0;
    virtual IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(
        apple80211_awdl_secondary_master_channel *) = 0;
    virtual IOReturn setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) = 0;
    virtual IOReturn setAWDL_MIN_RATE(apple80211_awdl_min_rate *) = 0;
    virtual IOReturn setAWDL_ENCRYPTION_KEYS(apple80211_key *) = 0;
    virtual IOReturn setAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) = 0;
    virtual IOReturn setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *) = 0;
    virtual IOReturn setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) = 0;
    virtual IOReturn setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add *) = 0;
    virtual IOReturn setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) = 0;
    virtual IOReturn setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config *) = 0;
    virtual IOReturn setCOMPANION_SKYWALK_IF_CREATE(
        apple80211_companion_skywalk_if_create *) = 0;                             /* 0x1180 */
    virtual IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(
        apple80211_awdl_af_sec_payload_request *) = 0;
    virtual IOReturn setLLW_PARAMS(apple80211_llw_params *) = 0;
    virtual IOReturn setTX_RATE(apple80211_tx_rate *) = 0;
    virtual IOReturn setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection *) = 0;
    virtual IOReturn setPSF_DWELL(apple80211_awdl_psf_dwell_params *) = 0;
    virtual IOReturn setAWDL_CONFIG(apple80211_awdl_config_request *) = 0;
    virtual IOReturn setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg *) = 0;
};
static_assert(sizeof(IO80211AWDLProtocol) == 0x130, "AWDLProtocol KC metaclass size");

/* ---- Concrete class of stage 1 ---- */
class RTLAwdInterface : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(RTLAwdInterface)
public:
    bool initWithController(RTLAirController *ctrl, const uint8_t mac[6]);
    bool startWithRegistration(RTLAirController *ctrl);
    void stop(IOService *provider) override;
    void free() override;
    const char *bsdName() const { return _bsdName; }

    uintptr_t setMacAddress(ether_addr &) override { return 0; }
    uintptr_t enable(unsigned int opts) override;
    uintptr_t disable(unsigned int opts) override;

    /* AWDL surface: stage 1 stubs (102 / kIOReturnUnsupported). */
#define AWDL_STUB(name, type) IOReturn name(type *) override { return kIOReturnUnsupported; }
    AWDL_STUB(getSSID, apple80211_ssid_data)
    AWDL_STUB(getCHANNEL, apple80211_channel_data)
    /* ... one line per each of the 63 slots 507-569; names and
     * offsets are in annex §16 and in AWDL_PURE_SLOTS.md ... */
#undef AWDL_STUB
    IOReturn setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create *) override
    { return kIOReturnUnsupported; }   /* D6: the companion is stage 4 */

private:
    RTLAirController *_ctrl;
    void *_txPool, *_rxPool;
    void *_txSubQ, *_txCompQ, *_rxCompQ;
    uint8_t *_regInfo;
    uint8_t _mac[6];
    char _bsdName[16];

    bool createDatapath();
    void teardownDatapath();
    static IOReturn txNoop(OSObject *owner, void *queue, void *const *pkts,
                           uint32_t count, void *refCon);
    static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);
};
#endif /* RTL_AWDL_INTERFACE_H */
```

> `?` The signatures of the 27 methods of `0xE68`–`0xF38` are declared with the types documented in
> [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md); the return type is not encoded in the
> mangled name and `uintptr_t` is used except on `init` (boolean). They only matter for
> vtable alignment: they are not called from the kext in stage 1.

### 12.2 `Source/AirLayer/AWDLInterface.cpp`

```cpp
/*
 * RTL8814AirLayer — stage 1 of AWDL: creation of awdl0 (role 6).
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "AWDLInterface.h"
#include <IOKit/IOLib.h>

/* The patch (§12.3) exposes rtlAirLog() from RTLAirLayer.cpp; traces go to the
 * RTLAirLayerLog ring of node RTL8814Native. */
#define LOG(fmt, ...) rtlAirLog("RTL8814AirLayer: " fmt "\n", ##__VA_ARGS__)

/* TX completion queue and pool, for the TX submission's no-op action
 * (AWDL_STAGE1_CODE_FIXES.md §1.2). */
IOReturn rtl_txcomp_enqueue(void *q, void *const *pkts, uint32_t n, uint32_t opts)
	__asm("__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
void rtl_pool_free(void *pool, void *pkt)
	__asm("__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket");

OSDefineMetaClassAndStructors(RTLAwdInterface, IO80211AWDLProtocol)

/* Same offsets as the STA path (Source/AirLayer/RTLAirLayer.cpp:207-214). */
#define AWDL_REGINFO_SIZE       0x130
#define AWDL_REGINFO_SUBFAMILY  0x0c
#define AWDL_REGINFO_ROLE       0x10
#define AWDL_REGINFO_NMEDIA     0x14
#define AWDL_REGINFO_MEDIA      0x24    /* u32 ifm_active  */
#define AWDL_REGINFO_MEDIA_CUR  0x28    /* u32 ifm_current */
#define AWDL_REGINFO_PREFIX     0x30
#define AWDL_REGINFO_UNIT       0x38
#define AWDL_REGINFO_HEADROOM   0x3c
#define AWDL_REGINFO_FLAGS      0x40
#define AWDL_REGINFO_MTU        0x48
#define AWDL_REGINFO_MAC        0x108

bool RTLAwdInterface::initWithController(RTLAirController *ctrl, const uint8_t mac[6])
{
    _ctrl = ctrl;
    memcpy(_mac, mac, 6);
    if (!init()) {
        LOG("awdl: init() failed");
        return false;
    }
    ether_addr ea;
    memcpy(ea.octet, mac, 6);
    if (!rtl_vif_init(this, ctrl, &ea, 6, "awdl")) {   /* publishes the AirLink role */
        LOG("awdl: IO80211VirtualInterface::init failed");
        return false;
    }
    rtl_sky_setId(this, 2);                            /* Broadcom: id 2 for AWDL */
    if (!attach(ctrl)) {
        LOG("awdl: attach failed");
        return false;
    }
    return true;
}

bool RTLAwdInterface::createDatapath()
{
    rtl_pool_options o;
    bzero(&o, sizeof(o));
    o.packetCount = 256;
    o.bufferCount = 256;
    o.bufferSize = 2048;
    o.maxBuffersPerPacket = 1;
    o.poolFlags = 1;
    _txPool = rtl_pool_withName("rtlawdl:tx", this, 1, &o);
    _rxPool = rtl_pool_withName("rtlawdl:rx", this, 1, &o);
    if (!_txPool || !_rxPool)
        return false;
    _txSubQ = rtl_txsub_withPool(_txPool, 128, 0, this,
                                 (void *)&RTLAwdInterface::txNoop, nullptr, 0);
    _txCompQ = rtl_txcomp_withPool(_txPool, 128, 0, this, nullptr, nullptr, 0);
    _rxCompQ = rtl_rxcomp_withPool(_rxPool, 128, 0, this, nullptr, nullptr, 0);
    if (!_txSubQ || !_txCompQ || !_rxCompQ)
        return false;
    IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
    if (!wq)
        return false;
    wq->addEventSource((IOEventSource *)_txSubQ);
    wq->addEventSource((IOEventSource *)_txCompQ);
    wq->addEventSource((IOEventSource *)_rxCompQ);
    return true;
}

bool RTLAwdInterface::startWithRegistration(RTLAirController *ctrl)
{
    if (!rtl_vif_start(this, ctrl)) {
        LOG("awdl: base start failed");
        return false;
    }
    _regInfo = (uint8_t *)IOMalloc(AWDL_REGINFO_SIZE);
    if (!_regInfo)
        return false;
    bzero(_regInfo, AWDL_REGINFO_SIZE);
    rtl_sky_initRegInfo(this, _regInfo, 1, AWDL_REGINFO_SIZE);
    *(uint32_t *)(_regInfo + AWDL_REGINFO_SUBFAMILY) = 3;
    *(uint32_t *)(_regInfo + AWDL_REGINFO_ROLE) = 6;          /* ? exact value */
    *(uint32_t *)(_regInfo + AWDL_REGINFO_NMEDIA) = 2;
    *(uint32_t *)(_regInfo + AWDL_REGINFO_MEDIA) = 0x80;      /* IFM_IEEE80211 */
    *(uint32_t *)(_regInfo + AWDL_REGINFO_MEDIA_CUR) = 0x80;  /* D5: passes the filter */
    *(const char **)(_regInfo + AWDL_REGINFO_PREFIX) = "awdl";
    *(uint32_t *)(_regInfo + AWDL_REGINFO_UNIT) = 0;
    *(uint16_t *)(_regInfo + AWDL_REGINFO_HEADROOM) = 64;
    *(uint32_t *)(_regInfo + AWDL_REGINFO_FLAGS) = 0;
    *(uint64_t *)(_regInfo + AWDL_REGINFO_MTU) = 0x59400000594ULL;  /* +0x48 and +0x4C */
    memcpy(_regInfo + AWDL_REGINFO_MAC, _mac, 6);

    if (!createDatapath())
        return false;
    void *queues[3] = { _txSubQ, _txCompQ, _rxCompQ };
    IOReturn r = rtl_eth_register(this, _regInfo, queues, 3, _txPool, _rxPool, 0);
    LOG("awdl: registerEthernetInterface = 0x%x", r);
    if (r != kIOReturnSuccess) {
        teardownDatapath();                        /* no dangling events (CODE_FIXES §2) */
        return false;
    }

    /* Synchronous: the BSD client composes "awdl0", publishes IOInterfaceName and
     * creates the ifnet before returning (Docs/AWDL_INTERFACE_BIRTH.md §5). */
    registerService(kIOServiceSynchronous);
    setRunningState(false);
    /* Name of the already created ifnet (§4.4), not of its own counter: read with
     * getBSDName and fail if empty (AWDL_STAGE1_CODE_FIXES.md §2). */
    strlcpy(_bsdName, getBSDName(), sizeof(_bsdName));
    if (!_bsdName[0]) {
        LOG("awdl: no bsd name");
        teardownDatapath();
        return false;
    }
    LOG("awdl: born %s", _bsdName);
    return true;
}

/* Disables, removes from the work loop, frees and nulls each queue, skipping
 * the already null ones; idempotent (AWDL_STAGE1_CODE_FIXES.md §2.1). */
IOReturn RTLAwdInterface::removeQueues(OSObject *owner, void *, void *, void *, void *)
{
    RTLAwdInterface *me = (RTLAwdInterface *)owner;
    IOEventSource *qs[3] = { (IOEventSource *)me->_txSubQ, (IOEventSource *)me->_txCompQ,
                             (IOEventSource *)me->_rxCompQ };
    for (int i = 0; i < 3; i++)
        if (qs[i])
            qs[i]->disable();
    return kIOReturnSuccess;
}

void RTLAwdInterface::teardownDatapath()
{
    IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
    if (wq)
        wq->runAction(&RTLAwdInterface::removeQueues, this);
    else
        removeQueues(this, nullptr, nullptr, nullptr, nullptr);
}

/* TX submission action, stage 1: nothing is transmitted. The packets go back
 * through the TX completion queue, as RTLAirInterface::txAction does once the
 * radio has consumed them; the completion queue completes them under its gate
 * and returns them to the pool. legacyDequeue advances its ring by the returned
 * count, so returning 0 would stall the queue: consume the whole batch.
 */
IOReturn RTLAwdInterface::txNoop(OSObject *owner, void *, void *const *pkts,
                                 uint32_t count, void *)
{
    RTLAwdInterface *me = OSDynamicCast(RTLAwdInterface, owner);
    if (!me || !count || !me->_txCompQ)
        return 0;
    /* enqueuePackets requires count < capacity; 64 < 128 always. */
    for (uint32_t done = 0; done < count; done += 64) {
        uint32_t n = count - done > 64 ? 64 : count - done;
        if (rtl_txcomp_enqueue(me->_txCompQ, pkts + done, n, 0) != kIOReturnSuccess)
            for (uint32_t i = 0; i < n; i++)
                rtl_pool_free(me->_txPool, pkts[done + i]);
    }
    return (IOReturn)count;
}

void RTLAwdInterface::stop(IOService *provider)
{
    teardownDatapath();
    IO80211VirtualInterface::stop(provider);
}

void RTLAwdInterface::free()
{
    OSSafeReleaseNULL(*(OSObject **)&_txSubQ);
    OSSafeReleaseNULL(*(OSObject **)&_txCompQ);
    OSSafeReleaseNULL(*(OSObject **)&_rxCompQ);
    OSSafeReleaseNULL(*(OSObject **)&_txPool);
    OSSafeReleaseNULL(*(OSObject **)&_rxPool);
    if (_regInfo) {
        IOFree(_regInfo, AWDL_REGINFO_SIZE);
        _regInfo = nullptr;
    }
    IO80211VirtualInterface::free();
}

uintptr_t RTLAwdInterface::enable(unsigned int)
{
    LOG("awdl: enable");
    return kIOReturnSuccess;
}

uintptr_t RTLAwdInterface::disable(unsigned int)
{
    LOG("awdl: disable");
    return kIOReturnSuccess;
}
```

Implementation notes:

- The MAC is saved in the object's `_mac` ivar when constructed (`memcpy(_mac, mac, 6)` in
  `initWithController`) and copied to the `RegInfo` in step 6. `?` No public getter of the MAC in the
  base has been verified; the STA path keeps it in `RTLAirController::_mac`
  (`Source/AirLayer/RTLAirLayer.hpp:187`).
- The name `awdl0` is read from the ifnet with `getBSDName()` once `registerService(2)` finished and
  it fails if empty (§4.4; [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §2).
- `registerService(kIOServiceSynchronous)` replaces the STA path's `kIOServiceAsynchronous`
  (`Source/AirLayer/RTLAirLayer.cpp:345`): it is the difference guaranteeing the ifnet exists before
  returning the name (D4).
- `LOG` uses the `rtlAirLog()` the patch exposes from `RTLAirLayer.cpp`
  (`Tools/awdl_stage1_hook.patch:60-72`; `AWDLInterface.cpp:23-25`), so the `awdl:` lines go to the
  `RTLAirLayerLog` ring of node `RTL8814Native`. The patch additionally carries the
  controller functions: the 94's override, `createAwdlInterface`, `destroyAwdlInterface` and the
  boot-arg arming (§12.3).
- The ivar `_mac[6]` is added to the class (`uint8_t _mac[6];`), next to `_bsdName`.

### 12.3 Hooking patch (described, not applied)

`Source/AirLayer/RTLAirLayer.hpp`, inside `RTLAirInterface` (next to `:98`):

```cpp
    /* Gate of VIRTUAL_IF_CREATE (type 94, slot 556). */
    IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req) override;
```

`Source/AirLayer/RTLAirLayer.hpp`, inside `RTLAirController` (next to `:141`), with
`class RTLAwdInterface;` declared before the controller's class:

```cpp
    bool awdlAllowed() const { return _awdlArmed; }
    RTLAwdInterface *awdlInterface() const { return _awdl; }
    IOReturn createAwdlInterface(const uint8_t mac[6], char out[16]);
    void destroyAwdlInterface();
```

New controller members (private, next to `:151-186`): `RTLAwdInterface *_awdl;
IOLock *_awdlLock; bool _awdlArmed;`.

`Source/AirLayer/RTLAirLayer.cpp`, override implementation:

```cpp
IOReturn RTLAirInterface::setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req)
{
    if (!req)
        return kIOReturnBadArgument;
    if (req->role != 6) {
        LOG("awdl: invalid role %u", req->role);
        return req->role == 7 ? kIOReturnUnsupported : (IOReturn)0xE0000001;
    }
    if (!_ctrl->awdlAllowed())
        return kIOReturnUnsupported;          /* current behavior (102) */
    return _ctrl->createAwdlInterface(req->mac, req->ifname);
}
```

So the `awdl:` lines reach the `RTLAirLayerLog` ring, `Source/AirLayer/RTLAirLayer.cpp`
exposes its unit's logger (next to `:58`): `void rtlAirLog(const char *fmt, ...) { airLog(fmt, ...); }`
and the new file uses that function instead of `IOLog`. Without this change, the skeleton compiles and
works, but its traces only go to the kernel log.

And in `RTLAirController::createAwdlInterface()`, the sequence of §4.4 with `_awdlLock`; in
`RTLAirController::stop()` (`Source/AirLayer/RTLAirLayer.cpp:1547`), `destroyAwdlInterface()` before
`_iface->terminate()`; in `free()` (`:1558`), `IOLockFree(_awdlLock)`; in `start()` (`:1402`),
`PE_parse_boot_argn("rtlawdl", &v, sizeof(v)); _awdlArmed = v != 0;` and `_awdlLock = IOLockAlloc();`.

## 13. Change in `build_airlayer.sh` (described, not applied)

A single change, on two lines of the same block:

1. **Line 40** (`build_airlayer.sh:40`), the compilation invocation of `RTLAirLayer.cpp`: one
   more line, identical except the input file (`Source/AirLayer/AWDLInterface.cpp`) and the output
   object (`"$OBJ/AWDLInterface.o"`). Flags do not change. The version gate generator has shifted the block: the compile step is now at `:40` and the link list at `:66`.

2. **Line 66** (`build_airlayer.sh:66`), the link's object list: add
   `"$OBJ/AWDLInterface.o"` next to `"$OBJ/RTLAirLayer.o"`.

   ```diff
        -o "$KEXT/Contents/MacOS/RTL8814AirLayer" \
   -    "$OBJ/info.o" "$OBJ/RTLAirLayer.o" \
   +    "$OBJ/info.o" "$OBJ/RTLAirLayer.o" "$OBJ/AWDLInterface.o" \
   ```

`CFLAGS`/`CXXFLAGS` need no touching (`build_airlayer.sh:20-29`): `-ISource/IO80211` and
`-ISource/AirLayer` already cover the includes, and the new file needs no own flags.
`?` The patch applies cleanly over the current tree (checked with `git apply --check`,
[`AWDL_STAGE1_SPEC_DELTA.md`](AWDL_STAGE1_SPEC_DELTA.md) §1) and `AWDLInterface.cpp` compiles in isolation
with these flags ([`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §0), but the kext has not been
linked: the first real compilation must confirm the offset-chained declaration does not collide with
`IO80211_25_6_0.h` (it does not include the AWDL catalog, §5.2).

## 14. Test checklist

All tests run with the gate open and `rtlair` at the level the system already uses; the
AWDL path is controlled with `rtlawdl`.

### P0 — Prior checks (without loading the kext)

| # | Check | Expected |
|---:|---|---|
| P0.1 | `Tools/re/check_awdl_symbols.sh` | 11/11 symbols with type `0f` |
| P0.2 | `Tools/re/check_awdl_header.py` | No discrepancies in pure slots 490 and 507–569 |
| P0.3 | Limit `0xE60 → 0xE68` of §5.2 against the dump of `__ZTV23IO80211VirtualInterface` | 27 methods between `syncDPSStats` and `setMacAddress`; `setMacAddress` at `0xF40` |
| P0.4 | `nm`/table of the built binary | `IO80211VirtualInterface::init`, `setAwdlInterface` and `registerEthernetInterface` resolved, no `__cxa_pure_virtual` in the object's vtable; the 50 overrides of §5.2 each resolve to the family's exported symbol (dedicated check in P0.5) |
| P0.5 | `nm -u` of the compiled object and contrast with `Docs/re/io80211_25.6.0/syms.txt` (recipe in [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §5) | The 93 symbols of `IO80211VirtualInterface` with type `0f`; 48 of the 50 overrides reach the object (`free` and `stop` have local bodies) |

### T1 — Load and gate

| # | Action | Expected |
|---:|---|---|
| T1.1 | Load `RTL8814AirLayer.kext` | `RTLAirGate = "open-validated: ..."`; log `gate open-validated` |
| T1.2 | With `rtlawdl=0`, force request 94 | The handler returns 102; the daemon logs the creation failure and **does not** wait 30 s ([`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4; `?` the concrete literal) |
| T1.3 | With `rtlawdl=1`, repeat | No timeout; `awdl0` present |

### T2 — Capability and gate

| # | Action | Expected |
|---:|---|---|
| T2.1 | Request 12 over `en2` | Byte 4 of the copyback (`+0x08`) with bit 0 at 1; byte 9 (`+0x0D`) bit 0 at 0 |
| T2.2 | Request 94 with role 6 | Log `awdl: create role 6`; return 0 in less than 1 s |
| T2.3 | Repeat the 94 | Return 17 and the same name at `+0x10` |
| T2.4 | Request 94 with role 7 | Return 102 (APSTA out of scope) |

### T3 — Birth

| # | Action | Expected |
|---:|---|---|
| T3.1 | After T2.2, `ioreg -r -c RTLAwdInterface -l` | Properties of §6.2 complete |
| T3.2 | `ifconfig awdl0` | Payload's MAC, MTU 1428, 802.11 media |
| T3.3 | `if_nametoindex("awdl0")` | Different from 0 |
| T3.4 | Kernel log | `awdl: registerEthernetInterface = 0x0`, `awdl: born awdl0` |
| T3.5 | Bring `awdl0` up and provoke IPv6/ND traffic | No panic: the no-op action consumes the batch through the TX completion queue and the enabled queues leave no dangling events ([`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §1.2, §7) |

### T4 — Acceptance by the daemon

| # | Action | Expected |
|---:|---|---|
| T4.1 | `log show --last 2m --predicate 'process == "wifip2pd"'` | `Found new interface awdl0 with role AirLink` |
| T4.2 | Same | No `Timed out while waiting for interface awdl0` |
| T4.3 | Same | No `Ignoring awdl0 because its not a WiFi interface` nor `... no interface index found` |
| T4.4 | Same | No re-creation loop (`No AWDL interface found. Creating AWDL interface` only once) |

### T5 — Media and 97

| # | Action | Expected |
|---:|---|---|
| T5.1 | `ioctl(SIOCGIFMEDIA)` over `awdl0` | `ifm_current & 0xE0 == 0x80` |
| T5.2 | `RawGet` 97 over `awdl0` | Parent's name (`en2`) without NUL |
| T5.3 | Daemon log after T5.2 | No parent error; no timeout |

T5 stays as a regression check and no longer decides anything: `ifm_current = 0x80` and parent `en2`
are fixed by the documents ([`AWDL_STAGE1_IFM_CURRENT.md`](AWDL_STAGE1_IFM_CURRENT.md) §3;
[`AWDL_STAGE1_SIOCSIFMEDIA.md`](AWDL_STAGE1_SIOCSIFMEDIA.md) §7;
[`AWDL_STAGE1_VIRTUAL_IF_PARENT.md`](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) §6); the three rows serve
to detect a `RegInfo` or `attachInterface` regression.

`?` No standard user command to emit the 97 isolated has been identified; the test
relies on the effect (T4) and on a custom probe if one is added.

### T6 — Optional companion

| # | Action | Expected with D6 |
|---:|---|---|
| T6.1 | After T4, wait 60 s | `Failed to create low latency interface ...` (2 lines) and `awdl0` intact |
| T6.2 | `ifconfig llw0` | Does not exist |
| T6.3 | Daemon log | No `Timed out while waiting for the low latency interface` |

### T7 — Monitoring and unload

| # | Action | Expected |
|---:|---|---|
| T7.1 | Monitoring registration | Returns 0; no re-creation of `awdl0` |
| T7.2 | Unload the kext | `awdl0` disappears; no panic; the daemon cleans the handle |
| T7.3 | Repeat load/unload 5 times | No visible leaks, no double `registerService`, no panic |

## 15. Risks and open points

1. **`registerService(2)` from the handler (promoted, `?`)**: the 94 runs under a gate —the
   BSD client's on the ioctl route ([`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md)
   §5–§6); on the user client route the gate is not identified— and, if the registration needs that same
   work loop, the synchronous `registerService` blocks. Broadcom uses the same pattern and it works.
   Mitigation: T2.2 measures the return time and T3.4 confirms the name; if it blocks, the asynchronous
   alternative forces revisiting the name criterion at `payload+0x10` (D4).
2. **Null action of the TX submission (new)**: `IOSkywalkTxSubmissionQueue::initWithPool`
   (`0xffffff8002b0caac`) requires a non-null action; with it null `withPool` returns null and the boot
   ends without `awdl0` ([`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §1). The code
   carries the no-op action of CODE_FIXES §1.2 and T3.5 exercises it.
3. **Incomplete datapath rollback (new)**: if the registration or the name fails, the three queues
   must leave the work loop before freeing the object (`teardownDatapath` → `removeQueues`,
   [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §2). `?` Whether `terminate()` on a
   failed boot invokes `stop()` and removes the queues.
4. **Headroom of the `RegistrationInfo` (new)**: the write goes at `+0x3C` (§5.3); `+0x3A` is the
   high half of the unit and would corrupt it ([`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md)
   §6.1). `?` No reader of `+0x3C` identified.
5. **Linking the 50 overrides (new)**: each override of `Source/AirLayer/AWDLInterface.h:91-145`
   must resolve to `IO80211VirtualInterface`'s exported symbol; the isolated compilation of
   CODE_FIXES §0 shows 93/93 symbols `0f` and P0.5 repeats the check on the build's object.
6. **Return type of `IO80211VirtualInterface::init`**: resolved by disassembly as `bool`
   (0 on each failure branch, 1 on success,
   [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §3.1); the skeleton declares it `bool` and
   checks the result.
7. **Numbering convention**: closed with the offset anchoring (`IO80211_25_6_0.h:458-460` and
   [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) rows 460–462 differ only in convention); check
   P0.3 verifies the count (27 methods, `0xE60 → 0xE68`).
8. **Export of `IO80211VirtualInterface::init`**: type `0f` confirmed in the dump and in
   `Docs/re/io80211_25.6.0/syms.txt` (93/93, [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md)
   §0); [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2 is a selection, not the inventory.
9. **Semaphore signal if the 97 fails (`?`)**: the static sequence places it before the signal and the
   error path is not documented ([`AWDL_STAGE1_STUB_VALUES.md`](AWDL_STAGE1_STUB_VALUES.md)
   §4). It stops being blocking: the primary is already attached
   (`Source/AirLayer/RTLAirLayer.cpp:1493`) and the 97 cannot fail in this flow
   ([`AWDL_STAGE1_VIRTUAL_IF_PARENT.md`](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) §6).
10. **Subclass metaclass (`?`)**: `OSDefineMetaClassAndStructors` needs `gMetaClass` and
    `C2` of `IO80211AWDLProtocol` (exported, [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §2.2);
    the registration of a third-party subclass in that hierarchy is not verified.
11. **`classNameOverride`** (slot 319, [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md)): could
    change the visible name in the IORegistry; unverified.
12. **Semantics of the 293's `code` (`?`)**: the kernel does not read it; documented for stage 4.
13. **WCL route of the 94 (`?`)**: partially closed — the fall to the bridge exists in both
    dispatch tables ([`AWDL_STAGE1_DISPATCH_PATH.md`](AWDL_STAGE1_DISPATCH_PATH.md) §1.3); `?` whether a
    WCL exists in this stack that could consume the request before the bridge.
14. **Build**: the skeleton compiles in isolation with the flags of `build_airlayer.sh` and the patch applies
    cleanly ([`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §0;
    [`AWDL_STAGE1_SPEC_DELTA.md`](AWDL_STAGE1_SPEC_DELTA.md) §1); the kext has not been linked. `?` The
    first linked compilation must confirm the offset-chained declaration does not collide with
    `IO80211_25_6_0.h` and that the signatures of the 27 methods of `0xE68`–`0xF38` match.

## 16. Annex — Slot table of stage 1

`off` = offset from the vptr, not from the `ZTV` header of the vtable (`vptr = ZTV + 0x10`);
`slot` = numbering of [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) (`slot = off/8 + 2`). The
methods of `0xFC8`–`0x11B8` are pure and in stage 1 return `kIOReturnUnsupported`;
`setMacAddress` (`0xF40`) is also pure and returns 0.

| off | slot | Method | Stage 1 treatment |
|---:|---:|---|---|
| `0xE68` | 463 | `init(ctrl, mac, role, name)` | called in `initWithController` |
| `0xE70`–`0xF38` | 464–489 | 26 methods of `IO80211VirtualInterface` | inherited from the family, no override |
| `0xF40` | 490 | `setMacAddress(ether_addr&)` | override: accepts, does not change |
| `0xF48`–`0xFC0` | 491–506 | `_RESERVEDIO80211VirtualInterface0..15` | inherited, no override |
| `0xFC8` | 507 | `getSSID` | stub 102 |
| `0xFD0` | 508 | `getCHANNEL` | stub 102 |
| `0xFD8` | 509 | `getBSSID` | stub 102 |
| `0xFE0` | 510 | `getSTATE` | stub 102 |
| `0xFE8`–`0x1010` | 511–516 | sync params/enabled/extension/election id/metric/chan seq | stubs 102 |
| `0x1018`–`0x1040` | 517–522 | peer cache max, master channel, sync state, rssi thresholds, presence, election params | stubs 102 |
| `0x1048`–`0x1070` | 523–528 | device caps, secondary master, af tx mode, min rate, encryption type, txcal | stubs 102 |
| `0x1078`–`0x1098` | 529–533 | dev stats, dfsp, cca, af sec payload caps, llw params | stubs 102 |
| `0x10A0`–`0x10C8` | 534–540 | setSSID…setAWDL_BSSID | stubs 102 |
| `0x10D0`–`0x1108` | 541–547 | election metric…peer cache control | stubs 102 |
| `0x1110`–`0x1138` | 548–553 | oob request, sync template, election params, traffic reg, secondary master, af tx mode | stubs 102 |
| `0x1140`–`0x1168` | 554–559 | min rate, encryption keys, encryption type, oob auto, txcal, advertisers | stubs 102 |
| `0x1170`–`0x1198` | 560–565 | dfsp, dfsp ucsa, **companion (562, `0x1180`)**, oob sec, llw params, tx rate | stubs 102 / D6 |
| `0x11A0`–`0x11B8` | 566–569 | p2p coex, psf dwell, awdl config, rtg peer stats | stubs 102 |

`?` The names of the final stretch are not fixed: [`AWDL_STAGE1_STUB_VALUES.md`](AWDL_STAGE1_STUB_VALUES.md)
§3 reads at `+0x11A0`..`+0x11B8` `setLLW_PARAMS`, `setTX_RATE`, `setP2P_COEX_PROTECTION` and
`setPSF_DWELL`, exactly `0x10` (2 slots) above the previous table; it is the signature of a
read from the ZTV instead of the vptr. Under that hypothesis the two tables match, but until
P0.3 reads the vtable from the vptr, the names of the last four rows and the order of the last
four stubs stay `?`. The other 59 names are unaffected.

Slot 570 (`getTX_RATE`, `0x11C0`) stays out of the declaration: its pure nature differs between
[`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) and [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md)
§7 (`?`), and stage 1 does not need it. `?` The table presupposes check P0.3.

## 17. References

- [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1, §3, §7 — facts and stage 1.
- [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2–§7 — birth, properties, order, media.
- [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §1, §3, §7 — payloads and C definitions.
- [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1–§2 — semaphore and sequence.
- [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §4–§5 — monitoring.
- [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §3–§4 — companion and verdict.
- [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1–§2 — exported symbols.
- [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) — full slot table.
- [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) — pure slots and signatures.
- [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4 — dispatch tables.
- `Source/AirLayer/RTLAirLayer.cpp`, `.hpp`, `Source/IO80211/*`, `build_airlayer.sh` — current code.

## 18. Application of the consistency delta's edits

The 14 ordered edits of [`AWDL_STAGE1_SPEC_DELTA.md`](AWDL_STAGE1_SPEC_DELTA.md) §5, applied
over this specification:

| # | Sections | Edit | State |
|---:|---|---|---|
| 1 | §4.3, §11 | `len != 32` out of the override's contract; the length is validated by the family's handler | Applied |
| 2 | §4.2 | `isCommandProhibited` withdrawn as a check of the 94; conclusion in `?` until T2.2 | Applied |
| 3 | §5.2, §12.1, P0.4 | 50 overrides from `0x428` to `0xDC0`; they do not shift the vtable; linking of their symbols in P0.4 | Applied |
| 4 | §5.3, §12.2 | Source of `+0x14 = 2` corrected to the STA path; `+0x10` and `+0x40` marked without reader; headroom resolved to `+0x3C` | Applied |
| 5 | §5.4 | Queue shape closed by QUEUE_SHAPE; non-null action mandatory; queues born enabled; no-op action in the plan | Applied |
| 6 | §2, §5.5, §10.1 | D4 conditional: gate of the 94 and blocking risk of the synchronous `registerService`; T2.2 measures | Applied |
| 7 | §7.2 | Requirement 2 (`setAwdlInterface`) deleted, with the 97's justification | Applied |
| 8 | §6.3 | "Not verified" replaced by the SIOCSIFMEDIA closure: seeding at registration and `ioctl_sifmedia` fails before the store | Applied |
| 9 | §9 | Real gate of the event (`com.apple.wifi_awdl.event_monitor`, `0xffffff80023347fc`); `sub_100046E30` stops being cited as fact and moves to T7.1 | Applied |
| 10 | §12.1, §12.2 | `snprintf` by `getBSDName`; `rtlAirLog`; `removeQueues` full with `teardownDatapath`; no-op action; controller functions of the patch | Applied |
| 11 | §13 | Lines of `build_airlayer.sh` updated (`:40`, `:66`); patch applies cleanly, kext unlinked | Applied |
| 12 | §14 | P0.5 and T3.5 added; T5 downgraded to regression check | Applied |
| 13 | §15 | List rewritten: closed items removed (1, 2, 6, 7, 8, 9, 15), 11 promoted, new risks (null action, rollback, headroom, overrides) | Applied |
| 14 | §16 | Offsets declared from the vptr; `0x10` discrepancy with STUB_VALUES §3; last four names in `?` | Applied |

No edit is omitted. Application clarifications:

- Edit 3 asks to add the linking check of the 50 overrides to P0.4 and edit 12 adds
  P0.5 for those same symbols: P0.4 references P0.5 and the check is not duplicated.
- Edit 4 offers "resolve `+0x3A` vs `+0x3C` or mark both as `?`": resolved to
  `+0x3C` with the evidence of [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §6.1
  (`createProxInterface` writes 64 at `+0x3C`; `+0x3A` is the unit's high half), and the `?` is
  kept for the field's reader.
- Edit 5 prescribes not adding the queues as event sources while there is no draining; the code
  of [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §1.2 and §2 keeps them in the work loop
  with the no-op action until `teardownDatapath`, and T3.5 exercises that set; §5.4 covers both
  things.
- The edits did not touch the old risks 3, 4 and 5 of §15; they remain in the rewritten list with the
  closure [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) brings (§3.1 type `bool`, §0
  symbols `0f`).
