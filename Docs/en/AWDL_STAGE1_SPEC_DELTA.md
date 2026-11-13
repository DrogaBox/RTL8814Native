# Consistency delta — `AWDL_STAGE1_IMPL_SPEC.md` against the ten later documents and the code

Review of `AWDL_STAGE1_IMPL_SPEC.md` in full against `AWDL_STAGE1_DISPATCH_PATH.md`,
`AWDL_STAGE1_IFNET_TO_INTERFACE.md`, `AWDL_STAGE1_REGINFO_AND_GATES.md`, `AWDL_IOCTL_SLOT_MAP.md`,
`AWDL_STAGE1_IFM_CURRENT.md`, `AWDL_STAGE1_STUB_VALUES.md`, `AWDL_STAGE1_IOCTL_130_131.md`,
`AWDL_STAGE1_VIRTUAL_IF_PARENT.md`, `AWDL_STAGE1_SIOCSIFMEDIA.md` and `AWDL_STAGE1_QUEUE_SHAPE.md`,
and
against the stage 1 code already present in `Source/AirLayer/AWDLInterface.{h,cpp}` and
`Tools/awdl_stage1_hook.patch`. Base: kernelcache of macOS 26.7.1 (25G241, x86_64), IDA headless
(`idat -A -S`) over a copy of `boot_awdl.kc.i64`. The specification and no
code file have been modified.

Point checks executed in this review, besides the reading:

| Check | Result |
|---|---|
| Xrefs to `IO80211Controller::getAwdlInterface` (`0xffffff8002293256`) | **0**, neither code nor data |
| Xrefs to `IO80211Controller::setAwdlInterface` (`0xffffff8002299c24`) | **0**, neither code nor data |
| `IOEventSource::init` (`0xffffff8000aca900`) | `this->enabled = true` in the body |
| `IOSkywalkPacketQueue::initWithPool` (`0xffffff8002b08bc8`) | calls the init of `IOEventSource`; does not call `disable` |
| `IOSkywalkTxSubmissionQueue::initWithPool` (`0xffffff8002b0caac`) | guard `a8 != nullptr` before accepting; `a8` is the action (the STA path passes a null refCon and it works) |
| `git apply --check Tools/awdl_stage1_hook.patch` | applies cleanly over the current tree (`0`) |

## 1. State of the stage 1 code

- `Source/AirLayer/AWDLInterface.h` and `.cpp` are in the tree and **are not compiled**:
  `build_airlayer.sh`
  only compiles `RTLAirLayer.cpp` (`build_airlayer.sh:40`) and links `info.o` + `RTLAirLayer.o`
  (`build_airlayer.sh:66`). The patch adding their compilation and linking
  (`Tools/awdl_stage1_hook.patch:216`, `:225`) is not
  applied, and `RTLAirLayer.cpp` contains neither the override of the 94 nor the logger `rtlAirLog`.
- The patch applies cleanly over the current working tree, including the changes of other sessions
  in
  `RTLAirLayer.cpp`, `build_airlayer.sh` and `Info.plist` (checked with `git apply --check`).
- With the current code, `createDatapath` cannot build the TX submission queue: a null
  action is passed (`AWDLInterface.cpp:140`) and `IOSkywalkTxSubmissionQueue::initWithPool` requires a
  non-null one
  (`0xffffff8002b0caac`); `withPool` returns null and the boot fails. This is the blocking defect of
  §4.
- The references `Source/AirLayer/RTLAirLayer.cpp:<line>` of the specification are those of the
  tree
  **without** the patch. When applying it, the lines shift (1 line at `:17`, 12 at `:57`, 20 at
  `:567`, 4 at `:1449`, 1 at `:1585`, 4 at `:1620` and the block of `:1764`); for example
  `attachInterface` goes from `:1493` to `:1530` (1 + 12 + 20 + 4). `?` A sweep of all the
  references has not been done.

## 2. Delta table

Convention: «later document» cites the document and its section that confirms, corrects or closes
the claim. `?` = unresolved by the documents nor by the checks of this review.

### 2.1 Overview, flow and criteria (§0)

| # | § | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 1 | 0.1 step 1 | The AWDL capability is already published in `getCARD_CAPABILITIES` (byte `+0x08` bit 0). | §3.2 of the specification itself; code at `RTLAirLayer.cpp:1776`. | Confirmed |
| 2 | 0.1 step 2 | The 94 reaches the override of `RTLAirInterface`. | `DISPATCH_PATH.md` §1.4 and §3; `IFNET_TO_INTERFACE.md` §5. | Confirmed (see #26) |
| 3 | 0.1 steps 3-4 | The object is created, started and `registerService(2)` leaves the ifnet done. | `VIRTUAL_IF_PARENT.md` §6 (the ifnet and the name are composed in `getBSDName`); `QUEUE_SHAPE.md` §7 (the BSD client creates the ifnet). | Confirmed in mechanism; the exact order remains `?` (#90) |
| 4 | 0.1 step 5 | The handler writes the name at `payload+0x10`; the name comes from the ifnet. | Code: `AWDLInterface.cpp:200-205` reads `getBSDName` and fails if empty. | Confirmed; §12.2 contradicts it (#30) |
| 5 | 0.2 criterion 4 | The 97 returns the parent name. | `VIRTUAL_IF_PARENT.md` §7. | Closed (needs no driver action) |
| 6 | 0.2 criterion 5 | No event is an acceptance condition; the blocking thing is that the registration returns 0. | `DISPATCH_PATH.md` §2 (role 6 and authorization); `STUB_VALUES.md` §4 leaves it as `?`. | Contradicted in fatality (#62) |
| 7 | 0.2 criterion 6 | The stage emits no traffic: the datapath goes unused. | `QUEUE_SHAPE.md` §8 + `IOEventSource::init` (this review). | **Corrected**: «unused» is not «harmless»; see #44 and #68 |

### 2.2 Starting point and decisions (§1–§2)

| # | § | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 8 | 1 (table) | Gate, nub/controller, STA interface, stubs and capability: current state. | No changes in the ten documents; the code matches. | Confirmed |
| 9 | 1 (table) | The `RegInfo` STA uses a pool and 3 queues. | `QUEUE_SHAPE.md` §1: the 3-queue shape passes all validations. | Confirmed |
| 10 | 1 (table) | The `IO80211AWDL_25_6_0.h` catalog is not included in the build. | It stands; `AWDLInterface.h:20-22` repeats the prohibition of including it next to the catalog. | Confirmed |
| 11 | 1 (table) | The build compiles `RTLAirLayer.cpp` and links two objects. | The patch adds `AWDLInterface.o`; `git apply --check` = 0. | Confirmed, not compiled (#96) |
| 12 | D1 | The 94 is served as an override in `RTLAirInterface`; the cast receiver was unverified. | `DISPATCH_PATH.md` §1.4; `IFNET_TO_INTERFACE.md` §5. | Closed |
| 13 | D2 | `awdl0` derives from `IO80211AWDLProtocol`. | `IOCTL_SLOT_MAP.md` §1: the 63 bridges cast to that metaclass. | Confirmed |
| 14 | D3 | New declaration by offsets; do not use the catalog. | `AWDLInterface.h:1-22` implements it; `P0.3` still pending. | Confirmed |
| 15 | D4 | 94 inline and synchronous `registerService(2)`. | `REGINFO_AND_GATES.md` §5: if the handler runs on the same work loop, the synchronous one may block; `?`. | Still open (#90) |
| 16 | D5 | Seed `0x80`/`0x80` as a deliberate deviation from Broadcom's `8`. | `IFM_CURRENT.md` §3; `SIOCSIFMEDIA.md` §7. Also the role 1 path already writes `0x8000000080` (`RTLAirLayer.cpp:210`, `:1508`). | Closed; the deviation is not new (#17) |
| 17 | D5 (nuance) | «The Broadcom seed `8` would not pass and the intermediate updater is not identified». | `IFM_CURRENT.md` §3 closes the value; `SIOCSIFMEDIA.md` §5 closes the writers. | Closed |
| 18 | D6 | The 293 returns 102 and the daemon absorbs it. | `STUB_VALUES.md` §4 (not fatal); `IOCTL_130_131.md` §3 (the daemon swallows errors via `throws`). | Confirmed |
| 19 | D7 | Boot-arg `rtlawdl`, off by default. | Implemented in the patch (`Tools/awdl_stage1_hook.patch:105-109`). | Confirmed |

### 2.3 Capability and gate of the 94 (§3–§4)

| # | § | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 20 | 3.1–3.2 | Byte `+0x08` bit 0 and companion bit `+0x07`; NAN closed. | None of the ten documents reviews it; it stands. | Confirmed |
| 21 | 4.1 entry | `gSetHandlerTable[93]`, `len == 32`, `data != NULL`. | `VIRTUAL_IF_PARENT.md` §5 (`cmp req_len, 0x20` at `0xffffff80022886c8`); `DISPATCH_PATH.md` §1.3. | Confirmed |
| 22 | 4.1 WCL path | `sendIOUCToWcl`; if WCL does not consume it, falls to the bridge. | `DISPATCH_PATH.md` §1.3: the WCL rule applies in both tables; the fall to the bridge exists. | Partially closed; `?` whether there is WCL in this stack |
| 23 | 4.1 bridge | `safeMetaCast` to `IO80211InfraProtocol` and slot `+0x1160`. | `DISPATCH_PATH.md` §1.3-§1.4; `IFNET_TO_INTERFACE.md` §5. | Confirmed |
| 24 | 4.1 (nuance) | The offset `+0x1160` is that of slot 556 of `IO80211InfraProtocol`. | `IOCTL_SLOT_MAP.md` §2 assigns the same `+0x1160` to `setAWDL_TXCAL_PERIOD` in `IO80211AWDLProtocol`. | No contradiction: two distinct hierarchies; worth saying in §16 |
| 25 | 4.2 | `isCommandProhibited` is consulted before the cast and the code returns `false` (`RTLAirLayer.cpp:1726`). | `IOCTL_SLOT_MAP.md` §1 attributes that consult to the 63 AWDL bridges, not the one of the 94; slot `+0xCC8` belongs to the interface, not the controller. | **Corrected**: the premise is unproven for the 94; `?` (T2.2 decides) |
| 26 | 4.2 | The cast might not land on `RTLAirInterface`. | `DISPATCH_PATH.md` §1.4: the interface is who issued the request; `IFNET_TO_INTERFACE.md` §5. | Closed |
| 27 | 4.3 | `req == nullptr` **or `len != 32`** returns `kIOReturnBadArgument`. | `VIRTUAL_IF_PARENT.md` §5: the length is validated in the family handler, not in the method; the override signature receives no `len`. | **Corrected**: the `len` row is not implementable in the override |
| 28 | 4.3 | `role == 7` → 102; other roles → `0xE0000001`. | Patch code (`Tools/awdl_stage1_hook.patch:88-93`). | Confirmed |
| 29 | 4.3 | «Already exists» → 17 with the name. | Code (`Tools/awdl_stage1_hook.patch:163-167`). | Confirmed |
| 30 | 4.4 | The sequence takes the name from the ifnet, «not from its own counter». | Code: `AWDLInterface.cpp:200-205` uses `getBSDName`; §12.2 composes `"awdl%d", 0`. | **Internal contradiction of the specification**; the code follows §4.4 |
| 31 | 4.4 | `_awdlLock` serializes and the name is written after publishing. | Code (`Tools/awdl_stage1_hook.patch:159-206`). | Confirmed |

### 2.4 Birth, `RegInfo`, queues and boot (§5)

| # | § | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 32 | 5.1 | Exported symbols and `init` with type `0f` unconfirmed in the selection. | `REGINFO_AND_GATES.md` does not touch it; P0.1/P0.4 still pending. | Still open |
| 33 | 5.1 | `setAwdlInterface` / `getAwdlInterface` are needed to publish the object. | `VIRTUAL_IF_PARENT.md` §1 and §7: the 97 does not consult them; this review: 0 xrefs to both in the whole kernelcache. | **Corrected**: not needed (#57) |
| 34 | 5.2 | The new declaration chains only `0xE68`–`0x11B8` over the catalog. | `AWDLInterface.h:96-145` adds 50 overrides of already existing slots (`0x428`–`0xDC0`) that the specification does not enumerate. | **Corrected**: the code declares them; P0.4 does not check them (#78) |
| 35 | 5.2 | The chain has not been cross-checked method by method; P0.3 verifies it. | Still the same. | Still open |
| 36 | 5.3 `+0x10` | Role of the `RegInfo`, value 6, `?`. | `REGINFO_AND_GATES.md` §3: `initRegistrationInfo` leaves it at 0 and there is no reader. | Closed: inert field; 6 is harmless |
| 37 | 5.3 `+0x14` | `2` media words, cited as a value of `initRegistrationInfo` (birth §2.2). | `REGINFO_AND_GATES.md` §2: `initRegistrationInfo` writes **256** at `+0x14`; the 2 is the write of the role 1 path (`RTLAirLayer.cpp:210`, `:1507`). | **Corrected**: the source is the STA path, not the default |
| 38 | 5.3 `+0x24`/`+0x28` | `0x80`/`0x80`, deviation D5. | `IFM_CURRENT.md` §1-§3; `RTLAirLayer.cpp:210`, `:1508` (`0x8000000080`). | Closed; the value is already validated by the STA path |
| 39 | 5.3 `+0x30`/`+0x38` | Prefix `"awdl"`, unit 0. | `VIRTUAL_IF_PARENT.md` §6: `getBSDName` composes `"%s%u"` with `ifnet_name`/`ifnet_unit`. | Confirmed |
| 40 | 5.3 `+0x3A` | Headroom u16 = 64 (`0xffffff800165de8c`). | `AWDL_INTERFACE_BIRTH.md` §2.2 says `0x3A`; the role 1 path writes 64 at `0x3C` (`RTLAirLayer.cpp:212`, `:1509`). | `?` **Code-document discrepancy**: two offsets for the same value |
| 41 | 5.3 `+0x40` | `0`; Broadcom 4 or 6. | `REGINFO_AND_GATES.md` §2: no reader. | Closed; keep 0 |
| 42 | 5.3 `+0x48`/`+0x4C` | MTU 1428 (`0x594`) in both. | Code (`AWDLInterface.cpp:180`). | Confirmed |
| 43 | 5.4 | 3 queues; unverified that the family accepts them; plan B = Broadcom's set. | `QUEUE_SHAPE.md` §1, §5, §10: 1 TX submission is the minimum; the TX completion is unused by the netif; the RX count is not validated; no check looks at the role. | Closed; plan B unnecessary, but the TX submission requires a non-null action (§4) |
| 44 | 5.4 | `enable`/`disable` only log the state and do not enable the queues. | `QUEUE_SHAPE.md` §8; this review: `IOEventSource::init` leaves `enabled = true` and the code never disables them. | **Contradicted** |
| 45 | 5.5 steps 7-9 | `addEventSource` + `registerEthernetInterface` + `registerService(2)`. | Code (`AWDLInterface.cpp:152-154`, `:190`, `:197`); `REGINFO_AND_GATES.md` §5 (`?` work loop). | Partially closed; risk of #90 remains |
| 46 | 5.5 step 10 | `setRunningState(false)`. | Code (`AWDLInterface.cpp:198`). | Confirmed |
| 47 | 5.5 step 11 | `setAwdlInterface(awdl)` from the handler. | `VIRTUAL_IF_PARENT.md` §1/§7 and 0 xrefs. | Unnecessary; harmless (#57) |
| 48 | 5.6 | No TX/RX; the queues go unused. | `QUEUE_SHAPE.md` §8: an enabled queue with a null action jumps to address 0 at the first work. | **Corrected**: the TX submission is not even built with a null action; the rest is born enabled (§4) |

### 2.5 Publication, media and 97 (§6–§7)

| # | § | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 49 | 6.1 | Role, MAC, prefix, unit, `BSD Name`, `IOInterfaceName`, type and UUID. | `VIRTUAL_IF_PARENT.md` §6 (name from the ifnet); `IFNET_TO_INTERFACE.md` §1 (BSD client). | Confirmed except the UUID (`?`) |
| 50 | 6.1 | The order requires everything published before the notification. | `VIRTUAL_IF_PARENT.md` §6; the synchronous `registerService` of the code keeps it if it does not block (#90). | Still open because of the blocking |
| 51 | 6.2 | `ioreg -r -c RTLAwdInterface -l` with the properties. | No changes; `classNameOverride` still `?`. | Still open |
| 52 | 6.3 | `ioctl_gifmedia` copies «state+48/52/56» from `RegInfo+0x24`/`+0x28`. | `IFM_CURRENT.md` §1: reads `*(this+0xC0)+0x30/+0x34/+0x38`, seeded at registration at `0xffffff8002b0a32a`/`0x2b0a332`. | **Corrected** in provenance and names |
| 53 | 6.3 | Unverified that anything rewrites `+0x34` before the daemon read. | `SIOCSIFMEDIA.md` §5 and §7: the only later writer is `ioctl_sifmedia` and on `awdl0` it fails before the store; no binary of the list issues `SIOCSIFMEDIA` on `awdl0`. | Closed; `0x80` stands |
| 54 | 6.3 | `SIOCGIFMEDIA = 0xC02C6938`. | `SIOCSIFMEDIA.md` §1 and §6. | Confirmed |
| 55 | 7.1 | The 97 does `safeMetaCast` to `IO80211VirtualInterface`, takes `getBSDName` of the parent and copies without NUL. | `VIRTUAL_IF_PARENT.md` §1-§2. | Confirmed |
| 56 | 7.2 req. 1 | The object must be `IO80211VirtualInterface`. | `VIRTUAL_IF_PARENT.md` §2. | Confirmed |
| 57 | 7.2 req. 2 | `setAwdlInterface()` is mandatory. | `VIRTUAL_IF_PARENT.md` §3-§7: the parent is `getPrimarySkywalkInterface` (controller block `+0x188`), written only by `attachInterface` for role 1; the 97 never consults `getAwdlInterface`; 0 xrefs. | **Corrected**: unnecessary requirement |
| 58 | 7.2 req. 3 | The parent name is that of the infrastructure interface. | `VIRTUAL_IF_PARENT.md` §6: `en2`, already attached at `RTLAirLayer.cpp:1493`. | Confirmed |
| 59 | 7.3 | A failure of the 97 would prevent the semaphore signal. | `STUB_VALUES.md` §4: no abort branch documented (`?`). | Degree discrepancy; `?` (T4/T5 decide) |

### 2.6 Companion, events, cycle and failures (§8–§11)

| # | § | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 60 | 8.1-8.2 | 293 optional; 102 and the daemon continues; no teardown. | `STUB_VALUES.md` §4; `IOCTL_130_131.md` §3. | Confirmed |
| 61 | 9 | The path is the user client / `createEventPipe` (slot 440); no planned code. | `DISPATCH_PATH.md` §2: `+0xDC0` is slot 440, `IO80211VirtualInterface::createEventPipe` already covers role 6 and requires `com.apple.wifi_awdl.event_monitor`. | Closed on the kernel side |
| 62 | 9 | A registration error chains `sub_100046E30`, which re-creates the interface. | `STUB_VALUES.md` §4: no abort branch recorded; `DISPATCH_PATH.md` §2 does not address it. | Contradicted by two documents |
| 63 | 10.1 | The 94 runs on the caller thread, outside own gates. | `REGINFO_AND_GATES.md` §5-§6: the ioctl path runs inside the `IOCommandGate::runAction` of the BSD client; the user client one (RawSet, the 94 if it goes that way) is also gated, but the gate was not identified (`?`). | **Corrected**: there is a taken gate |
| 64 | 10.2 | `_awdlLock` covers creation, consult and destruction. | Patch code (`:159-206`), which additionally holds it during `registerService`. | Confirmed; the long hold aggravates #90 |
| 65 | 10.3 | Lifecycle and teardown in the controller `stop`. | Patch code (`:196-206`). | Confirmed |
| 66 | 11 | `len != 32` → `0xE00002C2` in the override. | Same as #27. | **Corrected** |
| 67 | 11 | `initWithController` failure → `terminate()` + release. | The code declares `rtl_vif_init` as `void` (`AWDLInterface.cpp:48`) and ignores its result (`:110`). | **Corrected by the code**: the row is not implemented |
| 68 | 11 | Pool/queue or registration failure: remove the queues from the work queue and `deregisterEthernetInterface`. | The code returns `false` without removing the queues (`AWDLInterface.cpp:190-194`); the patch does `terminate()` + release (`:175-179`). | **Corrected by the code**: incomplete rollback |
| 69 | 11 | No ifnet or empty name: complete rollback, do not write the name. | Code: checks the name (`AWDLInterface.cpp:200-205`), but does not deregister. | Partial; the ifnet does not appear as a detectable case |

### 2.7 Skeleton, build and tests (§12–§14)

| # | § | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 70 | 12.1 | Header with 27 + 1 + 16 + 63 slots and `_rtlAwdPad[8]`. | `AWDLInterface.h` reproduces it and adds 50 overrides of existing slots, the size `static_assert`s (`:287-289`) and `rtlAwdlPublish` (`:407`). | **Corrected** (extension) |
| 71 | 12.2 | `snprintf(_bsdName, "awdl%d", 0)`; `rtl_txsub_withPool(..., nullptr, nullptr, 0)`. | `AWDLInterface.cpp:200-205` uses `getBSDName` and fails if empty; the null action of the skeleton is rejected by `initWithPool` (§4). | **Corrected by the code**; §4.4 already said the right thing (#30) and the skeleton does not boot |
| 72 | 12.2 | `LOG` uses `IOLog` until the logger is exposed. | The patch defines `rtlAirLog` (`Tools/awdl_stage1_hook.patch:60-72`) and `AWDLInterface.cpp:23-25` uses it. | **Corrected** |
| 73 | 12.3 | Patch described, not applied. | `Tools/awdl_stage1_hook.patch` exists, applies cleanly and also adds the controller functions and the logger. | Confirmed; not applied |
| 74 | 13 | Line 39 and lines 63-67 of `build_airlayer.sh`. | The current tree has the gate generator before the `clang++`; the cited lines no longer match (`:40`, `:63-67`). | **Corrected**: shifted references; the hunk still applies |
| 75 | 14 P0.1 | `check_awdl_symbols.sh` 11/11. | The script exists (`Tools/re/check_awdl_symbols.sh`). | Still open (not run here) |
| 76 | 14 P0.2 | `check_awdl_header.py` without discrepancies. | The script exists (`Tools/re/check_awdl_header.py`). | Still open |
| 77 | 14 P0.3 | 27 methods between `0xE60` and `0xE68`; `setMacAddress` at `0xF40`. | Not checked in this review. | Still open |
| 78 | 14 P0.4 | No `__cxa_pure_virtual`; symbols resolved. | It does not cover the 50 overrides of the real header (#34). | **Corrected**: add the check of those symbols |
| 79 | 14 T1-T7 | List of tests. | No content change; the probative value changes (§6). | See §6 |

### 2.8 Risks and annex (§15–§16)

| # | §15/§16 | What the specification says | Later document | Status |
|---:|---|---|---|---|
| 80 | 1 | Receiver of the cast of the 94 unverified. | `DISPATCH_PATH.md` §1.4; `IFNET_TO_INTERFACE.md` §5. | Closed |
| 81 | 2 | Interface search for the 97 unverified; `setAwdlInterface` might be mandatory. | `VIRTUAL_IF_PARENT.md` §3, §7; 0 xrefs. | Closed; the call is unnecessary |
| 82 | 3 | Return type of `IO80211VirtualInterface::init`. | The code declares it `void` (`AWDLInterface.cpp:47-48`) and the specification §12.1 assumes `bool`. | Still open; the code loses the failure (#67) |
| 83 | 4 | Per-slot numbering convention. | `IFM_CURRENT.md` §1 and `IOCTL_SLOT_MAP.md` use offsets from the vptr; the equivalence `0xE50` = label 458 stands. | Closed with offset anchoring; P0.3 verifies it |
| 84 | 5 | Export of `init` type `0f` unconfirmed. | No changes. | Still open |
| 85 | 6 | Shape of 3 queues vs Broadcom's 4+2. | `QUEUE_SHAPE.md` §1, §10. | Closed |
| 86 | 7 | `RegInfo+0x10`. | `REGINFO_AND_GATES.md` §3. | Closed |
| 87 | 8 | `RegInfo+0x40`. | `REGINFO_AND_GATES.md` §2. | Closed |
| 88 | 9 | `ifm_current`. | `IFM_CURRENT.md` §3; `SIOCSIFMEDIA.md` §7. | Closed |
| 89 | 10 | Semaphore signal if the 97 fails. | `STUB_VALUES.md` §4 (`?`); the 97 cannot fail in this flow because the primary is attached (`VIRTUAL_IF_PARENT.md` §6). | Still open but stops being blocking |
| 90 | 11 | `registerService(2)` from the handler. | `REGINFO_AND_GATES.md` §5: same work loop possible; Broadcom does it and it works. | Still open; promoted |
| 91 | 12 | Metaclass of the subclass. | No changes. | Still open |
| 92 | 13 | `classNameOverride`. | No changes. | Still open |
| 93 | 14 | `code` of the 293. | No changes (stage 4). | Still open |
| 94 | 15 | Coexistence with the AWDL catalog. | `AWDLInterface.h:20-22`. | Confirmed |
| 95 | 16 | WCL path of the 94. | `DISPATCH_PATH.md` §1.3: there is a fall to the bridge; `?` whether there is WCL in this stack. | Partial |
| 96 | 17 | The skeleton has not been compiled. | `git apply --check` = 0; not compiled. | Still open |
| 97 | 16 (annex) | `0x11A0` = `setP2P_COEX_PROTECTION`, `0x11A8` = `setPSF_DWELL`, etc. | `STUB_VALUES.md` §3 reads at `+0x11A0`..`+0x11B8` `setLLW_PARAMS`, `setTX_RATE`, `setP2P_COEX_PROTECTION`, `setPSF_DWELL`. | **Contradicted**: exact difference of `0x10` (2 slots); `?` |
| 98 | 16 (annex) | Slot 570 (`getTX_RATE`, `0x11C0`) stays out. | `IOCTL_SLOT_MAP.md` lists 63 rows ending at `0x11B8`; 570 is not among them. | Confirmed in the exclusion; the pure nature remains `?` |

## 3. Discrepancies between later documents

1. **Fatality of the event registration.** The specification (§9) claims an error chains
   `sub_100046E30`, which re-creates the interface; `STUB_VALUES.md` §4 says no abort branch is
   recorded and leaves it
   `?`. `DISPATCH_PATH.md` §2 does not resolve the user side. T7.1 decides. **Contradicted.**
2. **Fatality of the 97.** The specification (§7.3) treats it as a requirement because the static
   sequence places it before the signal; `STUB_VALUES.md` §4 does not document an abort. It is not a
   contradiction of fact but of degree: the 97 cannot fail with the primary attached
   (`VIRTUAL_IF_PARENT.md` §6).
3. **Names of the final stretch of the 63 slots.** `IOCTL_SLOT_MAP.md` §2 and `STUB_VALUES.md` §3
   assign
   different names to `+0x11A0`..`+0x11B8`. The difference is exactly `0x10` (2 slots) in the
   four rows, and `STUB_VALUES.md` §3 itself warns that in its vtable `+0xFC8`/`+0xFD0` are
   still `_RESERVEDIO80211VirtualInterface14/15`: it is the signature of a read from the ZTV
   instead of the
   vptr. Under that hypothesis the two tables match, but neither declares it. **`?`** Until P0.3
   reads the vtable from the vptr, the names of the final stretch (and the order of the last 4
   stubs) are not fixed. The rest of the 59 names is not affected by this difference.
4. **Queue shape and null-action safety.** `QUEUE_SHAPE.md` §8 concludes there is no path to the
   jump to 0 because «stage 1 does not enable the queues»; `IOEventSource::init`
   (`0xffffff8000aca900`) sets `enabled = true` at construction, and the code does not disable
   them.
   Moreover, `IOSkywalkTxSubmissionQueue::initWithPool` rejects building without an action, so the
   scenario of §8 cannot happen with the TX submission: either there is no queue, or there is an
   action. **Contradicted** by the kernel on both halves.
5. **`setAwdlInterface`.** The specification §7.2 declares it mandatory; `VIRTUAL_IF_PARENT.md` §7
   proves the 97 does not use it. **Contradicted**; the code call is harmless but unnecessary.
6. **Name of `awdl0`.** §4.4 requires taking it from the ifnet; §12.2 composes it with `snprintf`.
   It is an internal contradiction of the specification; the code resolves in favor of §4.4.
7. **Headroom.** `AWDL_INTERFACE_BIRTH.md` §2.2 and the specification §5.3 use `+0x3A`; the role 1
   path, already in production, writes 64 at `+0x3C` (`RTLAirLayer.cpp:212`, `:1509`). None of the
   ten
   documents reconciles it. **`?`** No reader of either appears in the published
   sweeps (which only cover `+0x08`, `+0x0C`, `+0x10` and `+0x40`).

## 4. Stage 1 code against the documents

| File:line | What it does | What the documents show | Status |
|---|---|---|---|
| `Source/AirLayer/AWDLInterface.cpp:140` | Creates the TX submission with null action and refCon. | `IOSkywalkTxSubmissionQueue::initWithPool` (`0xffffff8002b0caac`) requires a non-null action; `withPool` (`0xffffff8002b0c9b4`) returns null. The STA path always passes `txAction` (`RTLAirLayer.cpp:365`). | **Blocking defect**: null `_txSubQ` → `createDatapath` false (`:143-146`) → no `awdl0` and return `0xE00002BC` |
| `Source/AirLayer/AWDLInterface.cpp:152-154` | Adds the three queues to the work queue. | `QUEUE_SHAPE.md` §8 assumes a queue with a null action can exist and that «they are not enabled»; `IOEventSource::init` leaves `enabled = true` and `IOSkywalkPacketQueue::initWithPool` (`0xffffff8002b08bc8`) does not clear it. With the null action fixed, the queues stay enabled and the work loop serves them. | **Secondary defect**: with a valid action there is no jump to 0, but the §8 premise is false |
| `Source/AirLayer/AWDLInterface.cpp:190-194` | If registration fails, returns `false`. | Specification §11: remove the queues from the work queue and deregister. | **Defect**: the rollback does not remove the queues |
| `Tools/awdl_stage1_hook.patch:175-179` | On init/start failure, `terminate()` + release; does not call `stop()` explicitly. | Specification §11/§10.3. `?` `IOService::terminate` may invoke `stop` and reach `removeQueues`; unverified. | `?` |
| `Source/AirLayer/AWDLInterface.cpp:197` | `registerService(kIOServiceSynchronous)` inside the 94 handler. | `REGINFO_AND_GATES.md` §5: the dispatch runs under a gate; if the registration needs that same work loop, it blocks. Broadcom uses the same pattern. | **Open risk**; T2.2 measures it |
| `Source/AirLayer/AWDLInterface.cpp:48`, `:110` | `rtl_vif_init` is declared `void` and its result is ignored. | Specification §11 requires rollback if the init fails. | **Defect**: no failure signal |
| `Source/AirLayer/AWDLInterface.cpp:270-273` and `AWDLInterface.h:407` | Publishes the object with `setAwdlInterface`. | 0 xrefs to `set/getAwdlInterface` in the kernelcache; `VIRTUAL_IF_PARENT.md` §7. | **Unnecessary**; harmless |
| `AWDLInterface.h:96-145` | 50 overrides of existing slots, declared without local body. | They are not in specification §12.1 nor in P0.4; each must resolve to the exported symbol of `IO80211VirtualInterface`. | `?` **Link risk**; P0.4 must enumerate them |
| `AWDLInterface.h:287-289` | `static_assert` of sizes `0x128`/`0x130`. | The specification only fixes the `0x130` of the metaclass. | Confirmed; the build decides |
| `AWDLInterface.cpp:200-205` | Gets the name with `getBSDName` and fails if empty. | §4.4 and `VIRTUAL_IF_PARENT.md` §6. | Correct; §12.2 is obsolete |
| `AWDLInterface.cpp:212-228` | `removeQueues` disables, removes from the work loop and frees. | Specification §11; `QUEUE_SHAPE.md` §8. | Correct if it gets to run |

## 5. Edits of the specification, in order

1. **§4.3 and §11**: remove `len != 32` from the override contract. The family handler validates
   the length (`0xffffff80022886c8`) before the bridge; the method only receives the pointer.
2. **§4.2**: withdraw the sentence about `isCommandProhibited` as proof for the 94. The consult is
   documented for the 63 AWDL bridges, not the one of the 94, and slot `+0xCC8` belongs to the
   interface, not the controller. Leave the conclusion as `?` until T2.2.
3. **§5.2 and §12.1**: add the 50 overrides of already existing slots the real header declares
   (`0x428`–`0xDC0`) and state they do not shift the vtable being overrides. Add the link check of
   their symbols to P0.4.
4. **§5.3**: correct the source of `+0x14 = 2` (STA path, `RTLAirLayer.cpp:210`/`:1507`, not the
   default 256 of `initRegistrationInfo`); mark `+0x10` and `+0x40` as fields without a reader;
   resolve
   `+0x3A` versus `+0x3C` or mark both as `?`.
5. **§5.4**: replace the `?` and plan B with the result of `QUEUE_SHAPE.md`: 1 TX submission is the
   minimum, the TX completion is unused by the netif, the RX count is not validated and no check
   looks at the role. Add that the TX submission requires a non-null action in `initWithPool` and
   that the queues are born enabled: the plan must pass a no-op action (or disable them) and not
   add them as event sources until there is draining.
6. **§5.5 and §10.1**: leave D4 as conditional. The 94 runs under a gate, identified as the BSD
   client's one only on the ioctl path (`REGINFO_AND_GATES.md` §5-§6); the synchronous
   `registerService`
   may wait on that same work loop.
   T2.2 measures the return; if it blocks, the asynchronous alternative forces revisiting the name
   criterion at
   `payload+0x10`.
7. **§7.2**: delete requirement 2. The 97 reads `getPrimarySkywalkInterface` (controller block
   `+0x188`, written only by `attachInterface` for role 1) and does not consult
   `getAwdlInterface`; the
   primary is already attached at `RTLAirLayer.cpp:1493`. `setAwdlInterface` has no reference
   in the kernelcache.
8. **§6.3**: replace «unverified» with the close of `SIOCSIFMEDIA.md`: the descriptor is seeded at
   registration (`+0x30` and `+0x34` from `RegInfo+0x24`/`+0x28`, `0xffffff8002b0a32a`/`0x2b0a332`)
   and the
   only later writer, `ioctl_sifmedia` (head `0xffffff8002b0affa`), fails before the store on
   `awdl0`; `0x80` stands.
9. **§9**: add the real gate of the event (role 6 and `com.apple.wifi_awdl.event_monitor`,
   `0xffffff80023347fc`) and resolve the fatality with T7.1 instead of citing `sub_100046E30` as
   fact.
10. **§12.2**: replace the `snprintf` with `getBSDName` (the code already does it) and reflect
    `rtlAirLog`,
    `removeQueues` and the controller functions of the patch.
11. **§13**: update the lines of `build_airlayer.sh` (the gate generator shifts the block) and
    note the patch applies cleanly but has not been compiled.
12. **§14**: add P0.5 (symbols of the 50 overrides) and T3.5 (bring up `awdl0`, provoke IPv6 traffic and
    check there is no panic from the null action). Demote T5.1 to a regression check: it no
    longer decides anything.
13. **§15**: rewrite the list with the states of §2.8; remove 1, 2, 6, 7, 8, 9 and 15 as closed;
    promote 11; add as new risks the null action (§4), the incomplete rollback (#68), the
    headroom (#40) and the unchecked overrides (#34).
14. **§16**: state the offsets are from the vptr and not the ZTV; add the `0x10` discrepancy with
    `STUB_VALUES.md` §3 and leave the last four names as `?` until P0.3.

## 6. Hardware tests T1–T7: what each one decides now

| Test | What it decides today |
|---|---|
| T1 (load and gate) | That the version gate and `rtlawdl` work; T1.2 that the 94 returns 102 without the boot-arg. |
| T2 (capability and gate) | T2.1 the capability; **T2.2** the cast receiver, the return in under 1 s and, with it, whether the synchronous `registerService` blocks (#90); T2.3 the EEXIST path; T2.4 role 7. |
| T3 (birth) | T3.1 the properties and role; T3.2 MAC, MTU and media; T3.3 the BSD index; T3.4 the logs. If `awdl0` does not appear, T3.4 must show `awdl: packet queue alloc failed`: it is the signature of the TX submission rejection with a null action (§4), prior to any datapath test. **T3.5 new**: bring up `awdl0` and provoke IPv6/ND traffic to check the no-op action and the enabled queues cause no panic. |
| T4 (acceptance) | The media filter, the 97 and the name: T4.1–T4.3 confirm the daemon accepts the interface; T4.4 rules out the re-creation loop. |
| T5 (media and 97) | Regression only: `ifm_current = 0x80` and `en2` are already fixed by the documents; they serve to detect a `RegInfo` or `attachInterface` regression. |
| T6 (companion) | That the 102 of the 293 is absorbed without timeout, without teardown and without `llw0`. |
| T7 (monitoring and unload) | **T7.1** decides the only remaining factual discrepancy between documents (fatality of the event registration, #62) and the process authorization; T7.2 the teardown; T7.3 the load/unload cycle and the queue rollback (#68). |

All remain necessary. T5 drops from decisive to regression; T2.2, T3.5 and T7.1 rise in value.

## 7. Pendings the documents do not close

- `?` Names and order of the last four pure slots (#97) until reading the vtable from the vptr.
- `?` Headroom at `+0x3A` or `+0x3C` (#40).
- `?` Return type of `IO80211VirtualInterface::init` (#82); the code takes it as `void`.
- `?` Symbols of the 50 overrides of `AWDLInterface.h` (#34).
- `?` Behavior of `registerService(2)` under the BSD client gate (#90).
- `?` Whether `terminate()` over a failed boot invokes `stop()` and removes the queues (#68).
- `?` Real fatality of the event registration in wifip2pd (#62).
- `?` Whether a WCL exists in this stack that could consume the 94 (#95).
- `?` `classNameOverride`, metaclass of the subclass and `IOSkywalkNexusUUID` in the IORegistry.
