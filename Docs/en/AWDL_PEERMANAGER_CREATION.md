# Creation and publication of the `IO80211AWDLPeerManager` for an `awdl0` of an external driver (role 6)

It closes open points 14 and 15 of the index (§8.2): how the kernel creates and publishes the
`IO80211AWDLPeerManager` for a role-6 virtual interface fabricated by a third-party kext, where
the pointer ends up and how that kext retrieves it. Addresses of KC 25.6.0 (25G241) unless noted;
those of 14.8.9/15.8.2 are in §9.

Summary of the chain (addresses verified against the IDB, disassembly or pseudocode; §10):

1. The 94 (`VIRTUAL_IF_CREATE`) ends in the driver, which fabricates the vif and registers it
   (§6). The manager's creation **does not** happen in the 94.
2. After registration, the vif's `start` executes `IO80211SkywalkInterface::start`
   (`0xffffff80022f5792`), which calls the **virtual slot `+0xB08`** (`createPeerManager`) at
   `0xffffff80022f5bca` and publishes the result at **`ivars+0x28`** of the vif
   (`0xffffff80022f5bd0`–`0xffffff80022f5bd8`).
3. The inherited factory `IO80211VirtualInterface::createPeerManager` (`0xffffff80023308c2`) chooses
   by role: 6 → `IO80211AWDLPeerManager::withInterface` (`0xffffff80021c14bc`), and registers the vif
   on the controller at `*(ctrl+0x120)+0x190`.
4. The apple80211 dispatch bridges retrieve the manager with
   `safeMetaCast(*(*(if+0x120)+0x28), &IO80211AWDLPeerManager::gMetaClass)` (§5).
5. The external kext obtains the pointer through the paths of §7; the one the plan proposes (step 5) via
   `getP2PSkywalkPeerMgr` (`+0xCC0`) **does not work** (§5.3).

## 1. Objects and ivars involved

Three state blocks; the first two are reachable from the `+0x120` ivar of their owners:

- **Ivars of `IO80211SkywalkInterface`** (0xF0 B, `initIvars` `0xffffff80022f520c` allocates them with
  `IOMallocTypeImpl` and leaves them at `this+0x120`; `initIvars` clears them with `memset_s` if they already
  existed). Named fields:
  | Offset | Content | Evidence |
  |---|---|---|
  | `+0x00` | entitlement byte (`wlan.entitlement`) | `PE_parse_boot_argn` of the `start` writes `*(ivars)` (pseudocode; exact address not dumped, `?`) |
  | `+0x18` | `OSDictionary` (capacity 5) | `start`, final branch |
  | `+0x28` | **pointer to the PeerManager** | store `0xffffff80022f5bd8`; read by the bridges (§5) |
  | `+0x30` | `IO80211Controller*` | `start`: `safeMetaCast(provider, IO80211Controller::gMetaClass)`; getter `getController` `0xffffff80022f957a` |
  | `+0x58` | role (u32) | `setInterfaceRole` `0xffffff80022f60f0`: `*(ivars+88) = a2` + properties |
  | `+0x5C` | interfaceId (u32) | argument of the `start` logs |
  | `+0x68` | parent interface (role 2 inherits its manager) | `start`, branch `role == 2` |
  | `+0x80` | `CCPipe` of the interface log | `start` |
  | `+0xB8` | `OSString` of the role | `setInterfaceRole` |
  | `+0xC8` | `IO80211FlowQueueDatabase` | `start`, after creating the manager |
  - The `+0x00` byte was not followed instruction by instruction: offset taken from the pseudocode
    (`?` on its fine-grained disassembly).

- **Ivars of `IO80211Controller`** at `ctrl+0x120` (same pattern; the alloc was not followed, `?`).
  Virtual interface table per role, written by `createPeerManager`:
  | Offset | Content |
  |---|---|
  | `+0x190` (400) | the role-6 vif (AWDL) |
  | `+0x198` (408) | the role-7 vif (generic PeerManager) |
  | `+0x3B0` (944) / `+0x3B8` (952) | the NAN vifs (roles 8/9 and 10) |

- **Manager ivars**: the C++ object is 40 B (`OSObject_typed_operator_new(&…_ktv, 40)` in
  `withInterface`); the state lives at `this+0x18` (base, from `IO80211PeerManager::initWithInterface`
  `0xffffff8002246c88`) and `this+0x20` (AWDL, >66 KB, from `IO80211AWDLPeerManager::initWithInterface`
  `0xffffff80021c1540`; detail in `AWDL_PEERMANAGER_SOFTWARE.md` §2.2). Backpointers written by
  the base: `+0x558` (1368) = `IO80211SkywalkInterface` — **nullptr** on the AWDL path, the base is
  called with `a3 = 0` from `IO80211AWDLPeerManager::initWithInterface` (call not dumped, `?`) — and
  `+0x560` (1376) = the `IO80211VirtualInterface`.

## 2. The inherited factory: `IO80211VirtualInterface::createPeerManager` (slot `+0xB08`)

`0xffffff80023308c2` (cell `__ZTV23IO80211VirtualInterface+0xB08` = `0xffffff8002473690`).
Disassembly verified:

1. Reads the role: `r14d = *(*(this+0x120)+0x58)` at `0xffffff80023308e0`.
2. Takes the local MAC with slot `+0xCF8` (`getSelfMacAddr`, `0xffffff80022f841c`) at
   `0xffffff80023308ee` and leaves it on the stack (6 B) to pass it to the factory.
3. Split by role (`0xffffff80023308ff`–`0xffffff80023309d8`):
   - 6 → `IO80211AWDLPeerManager::withInterface(this, &mac)` (`0xffffff80021c14bc`), called at
     `0xffffff8002330957`. **Only code xref of the factory in the whole KC** (the only way
     the kernel instantiates the AWDL manager).
   - 7 → `IO80211PeerManager::withInterface(this, 0, …)` (called at `0xffffff8002330926`) with the
     log `"IO80211VirtualInterface::createPeerManager for role %d\n"` at `0xffffff800233091c`.
   - 8/9 → `IO80211NANPeerManager::withInterface`; 10 → `IO80211NANDataInterfacePeerManager::
     withInterface`.
   - Any other role → returns 0.
4. If the factory returned a manager, it calls `getController` (slot `+0xBA8`,
   `0xffffff80022f957a`) and executes `*(*(ctrl+0x120) + {0x190|0x198|0x3B0|0x3B8}) = this` — it saves
   **the interface** (not the manager) in the controller's table; for role 6 the store is
   `mov ecx, 190h` at `0xffffff8002330974` and `mov [rax+rcx], rbx` at `0xffffff80023309df`.
5. Returns the manager (or 0). **It does not write `ivars+0x28`**: that publication is done by the `start` (§3).

`IO80211AWDLPeerManager::withInterface` (`0xffffff80021c14bc`): typed alloc of 40 B, vtable
`0xffffff800245a868`, registration in `gMetaClass` (`0xffffff800248d2e0`) and `initWithInterface`
(`0xffffff80021c1540`); if the init returns false it does `release` and returns nullptr (the vif's
`start` will fail, §4).

## 3. Publication: `IO80211SkywalkInterface::start` writes `ivars+0x28`

`0xffffff80022f5792`. Verified order:

1. Double-boot guard on entry (first statement of the `start`; exact address not dumped,
   `?`): if `*(ivars+0x28) != 0` →
   `panic("\"Calling start twice in a row is not allowed\\n\" @%s:%d", "IO80211SkywalkInterface.cpp", 387)`.
   The manager is created exactly once, here.
2. `ivars+0x30 = safeMetaCast(provider, IO80211Controller::gMetaClass)` — the `start`'s provider
   of the vif is the controller that registered it.
3. Branch by role:
   - `role == 2` (P2P): it inherits the manager from the parent interface —
     `*(ivars+0x28) = *(*( *(ivars+0x68) + 0x120 ) + 0x28)`, with log
     `"Assigning peerManager %p for interfaceid %d, from ivars->parentInterface (%p) role %d"`.
   - the rest of the roles (including 6): log `"Creating peer manager for %s, ifId %d, role %d"` at
     `0xffffff80022f5bbe` and **`call qword ptr [rax+0xB08h]` at `0xffffff80022f5bca`**; the result
     is published with `mov rcx,[r12+120h]; mov [rcx+28h],rax` (`0xffffff80022f5bd0`–`0xffffff80022f5bd8`).
4. If the manager is 0 → `jz` to `0xffffff80022f5de5` with log
   `"ERROR: Family Skywalk Interface start peer manager init fail"`: the vif's `start` fails and
   the interface never comes up.
5. Only afterward it creates the `IO80211FlowQueueDatabase` (`ivars+0xC8`).

Only real caller of slot `+0xB08` among the filesets with possible dependency on
IO80211Family (§10): the other four sites with the same offset hit the `0xB08` slot of
**other classes** — `AppleBCMWLANCore::watchdog` `0xffffff80016053b8`/`0xffffff8001605ae0` and
`AppleBCMWLANCore::getTRAP_INFO` `0xffffff8001674607` (`getTrapInfo` of its provider, per the
log literal "ivars->fProvider->getTrapInfo") and `AppleSkywalkAVBLegacyEthernet::setupAVB`
`0xffffff80029cea3e` (on `AppleSkywalkAVBEthernetInterface`, AVB getters of realtime queues) —,
not the interface. Consequence: on the Broadcom path, the manager's creation is also born from
the vif's `start` with the inherited factory — the vtable of `AppleBCMWLANProximityInterface` points
to the base at `+0xB08` (cell `0xffffff80017f5080` → `0xffffff80023308c2`).

## 4. When it happens relative to the `VIRTUAL_IF_CREATE`

1. The 94 arrives at the dispatch bridge (modern `setVIRTUAL_IF_CREATE` `0xffffff80022886a8`; BSD
   ioctl via `setVirtualInterfaceCreate` `0xffffff8002357f33`); both first try the WCL route
   (`IO80211Glue::sendIOUCToWcl(0x802869C8, 0x5E, data, 32)`) and if they do not consume it they call
   `apple80211setVIRTUAL_IF_CREATE` (`0xffffff8002268494`), which does `safeMetaCast` to
   `IO80211InfraProtocol::gMetaClass` and calls the virtual slot `+0x1160` of the main object.
2. The driver answers in that slot: it fabricates its role-6 vif and takes it up with
   `IO80211Controller::attachVirtualInterface(&vif, mac, rol, register=true)`
   (`0xffffff80022960aa`): slot `+0xDD8` `createVirtualInterface` (the base `0xffffff800229612e`
   **only knows how to create role 7 "ap"** via `IO80211VirtualInterface::gMetaClass.alloc`; for 6 the
   driver must overwrite it and return its own class), slot `+0x670` `attach`
   (`0xffffff800233023a`, plain `kIO80211PlaneName`) and, if the bool asks for it, `registerService`
   (slot `+0x5B0`).
3. The `registerService` fires the IOKit matching and the vif's `start` with the controller
   as provider: §3 runs there. The order of this segment matches `AWDL_INTERFACE_BIRTH.md` §6.
4. The manager exists and is published at `ivars+0x28` before the daemon talks to `awdl0`
   (`AWDL_WIFIP2PD_POST_CREATE.md`); the daemon's AWDL ioctls find the manager through the path
   of §5.

## 5. How the kernel (and the driver) retrieves the pointer

### 5.1 The apple80211 bridges (path of the 27+43 AWDL types)

Identical pattern in the get/set bridges with cast to `IO80211AWDLPeerManager` (118 xrefs to the
`gMetaClass` `0xffffff800248d2e0` in the KC: some 95 get/set bridges and the rest own consumers
of the family — `inputPacket`, `ScanManager`, `InfraInterface`… —; §10). Verified sample:

- `getAWDL_SYNC_PARAMS` `0xffffff800227bbcd` (ioctl 116) and `setAWDL_SYNC_ENABLED`
  `0xffffff800228889d` (ioctl 117):
  1. `a2` = the interface the command came in on (the vif `awdl0` on the paths described by
     `AWDL_APPLE80211_DISPATCH.md` §2);
  2. `v6 = *(*(a2+0x120)+0x28)`;
  3. if nullptr → returns **45**; if `a2` is nullptr → 45; if the payload's `len` does not match → 22;
  4. `safeMetaCast(v6, &IO80211AWDLPeerManager::gMetaClass)`: if it fails → **19**;
  5. if it casts, it calls the manager's method (`getAwdlSyncParams`/`setAwdlSyncEnabled`, kernel
     state, without going through the interface's vtable).
- `IO80211Controller::getInterfaceActiveBitmap` (`0xffffff800229a8c6`) walks the other direction:
  vif from `*(ctrl+0x120)+0x190` (and `+0x3B0` for NAN), manager from `*(vif+0x120)+0x28`.
- `IO80211VirtualInterface::inputPacket` (`0xffffff8002333cac`) also casts to
  `IO80211AWDLPeerManager` (RX path; `AWDL_PEER_PRESENCE_PPP.md` §3).

### 5.2 Direct getter of the family

`IO80211SkywalkInterface::getPeerManager` (`0xffffff80022f5fa8`): `return *(ivars+0x28)`. **Without
any xref** in the KC (it is in no vtable of the three classes consulted and no code calls it: helper without consumers, probably inlined in the live branches). It is exported
in the three builds (§9), so a kext can link it and call it on its vif.

### 5.3 `getP2PSkywalkPeerMgr` (slot `+0xCC0`): useless for obtaining the manager

- Bases: `IO80211VirtualInterface::getP2PSkywalkPeerMgr` `0xffffff8002334c1a` and
  `IO80211SkywalkInterface::getP2PSkywalkPeerMgr` `0xffffff80022f8ab4` — **both `return 0`**.
- Only caller of the slot in the KC: `AppleBCMWLANBusInterfacePCIe::start` at
  `0xffffff800155a68c` (`call qword ptr [rax+0xCC0h]`); with result ≠ 0 it reports init failure
  (`reportInitFailure(..., 0x83A)`). With the family's bases the value is always 0 and the
  check passes; the exact class of the receiving object (`rbx`) was not determined (`?`).
- It corrects the plan (step 5): the "alternative without override: `getP2PSkywalkPeerMgr`, slot `0xCC0`"
  does not give the manager on 25.6.0. The real read without override is the one of §7.2.

## 6. What fails if the subclass overwrites nothing

| Piece | If the base is inherited | Evidence |
|---|---|---|
| `createPeerManager` (`+0xB08`) | **Works**: for role 6 the base builds the real `IO80211AWDLPeerManager`, registers the vif at `*(ctrl+0x120)+0x190` and returns the pointer. It is the Broadcom path (§3). No manager symbol needs linking. | `0xffffff80023308c2`; Broadcom cell `0xffffff80017f5080` |
| `getP2PSkywalkPeerMgr` (`+0xCC0`) | Works (returns 0 and nobody in the family uses it to hand out the manager; the only consumer in the tree expects 0). Overwriting it contributes nothing. | §5.3 |
| Override of `+0xB08` returning 0 | The vif's `start` fails ("peer manager init fail"); `awdl0` never comes up. | `0xffffff80022f5bdc`–`0xffffff80022f5de5` |
| Override of `+0xB08` returning an object that does not cast to `IO80211AWDLPeerManager` | Boot continues, but the bridges return 19 for all AWDL types; the daemon configures nothing. | §5.1 |
| Override of `+0xB08` returning a manager other than the published one | There is no second publication: if the override is not what the `start` saves, the bridges do not see it (they read `ivars+0x28`, not the return). | §3, §5.1 |
| `createVirtualInterface` (`+0xDD8`) inherited with role 6 | Returns 0 (`if (a3 != 7) return 0`); the `attachVirtualInterface` fails and there is no vif. For role 6 the controller's subclass must overwrite it. | `0xffffff800229612e` |

## 7. Pointer capture paths for the external kext

1. **Override of slot `+0xB08`** (`createPeerManager`, declared in `AWDLInterface.h:110`):
   the override calls the base's implementation (or directly
   `IO80211AWDLPeerManager::withInterface(this, &mac)`, exported in the three builds) and saves the
   return before returning it. This is what step 5 of the plan proposes; the kernel's
   publication (`ivars+0x28`) stays intact because the `start` saves what the override returns.
2. **Direct read without override**: after the `start`, `manager = *(*(vif+0x120)+0x28)`. The ivars
   belong to the family; the kext's vif only needs to read its own ivar `+0x120`. It is equivalent
   to calling the exported symbol `IO80211SkywalkInterface::getPeerManager` (`0xffffff80022f5fa8`).
   It removes the need to overwrite the slot and avoids the order race
   `start`↔capture: the value does not exist until the `start` finishes, so any reader must
   wait for that point (same order warning step 5 already notes).
3. There is no path through `getP2PSkywalkPeerMgr` (§5.3) nor through the isolated factory: `withInterface`
   publishes nothing by itself, and its only caller in the kernel is the base of `+0xB08`.

The `IVarsLock`/concurrency of that read is not in the KC (field without its own lock in the
branches seen) `?`.

## 8. Release

- `IO80211SkywalkInterface::free` (`0xffffff80022f6184`) does `release` of the ivars `+0x18`,
  `+0x30`, `+0x38`, `+0x40`, `+0x48`, `+0xA8`, `+0xB8`, `+0xC8`, `+0xD8` and deletes the block
  (0xF0 B), but **the release of `ivars+0x28` does not appear** (index `v4[5]` missing from the list) `?`:
  either the manager is freed on another leg (e.g. the manager itself when the vif dies, or the
  controller's destruction route), or the family leaves it to the kext's unmount. Not followed (`?`).
- `IO80211VirtualInterface::terminate` (`0xffffff8002330c50`) and `stop` (`0xffffff8002330cee`)
  free its own ivars `this+0x128` (`_bpfListHead` at `+0x28` of that block, not the manager).

## 9. Compatibility across builds

Exported addresses (dumps `Docs/re/io80211_{25.6.0,23J631,24H232}/syms.txt`, type `0f` =
IO80211Family entry):

| Symbol | 25.6.0 (25G241) | 14.8.9 (23J631) | 15.8.2 (24H232) |
|---|---|---|---|
| `IO80211VirtualInterface::createPeerManager` | `0xffffff80023308c2` | `0xffffff80021d8fbe` | `0xffffff80022a6376` |
| `IO80211VirtualInterface::getP2PSkywalkPeerMgr` | `0xffffff8002334c1a` | `0xffffff80021dd3f2` | `0xffffff80022aa68a` |
| `IO80211SkywalkInterface::createPeerManager` | `0xffffff80022f7be6` | `0xffffff80021a2072` | `0xffffff8002265092` |
| `IO80211SkywalkInterface::getP2PSkywalkPeerMgr` | `0xffffff80022f8ab4` | `0xffffff80021a2cda` | `0xffffff8002265f66` |
| `IO80211SkywalkInterface::getPeerManager` | `0xffffff80022f5fa8` | `0xffffff80021a0a3c` | `0xffffff8002263490` |
| `IO80211AWDLPeerManager::withInterface` | `0xffffff80021c14bc` | `0xffffff80020b1bd8` | `0xffffff8002160078` |

- Slots (dumps `vt_IO80211VirtualInterface.txt` and `vt_IO80211SkywalkInterface.txt`): **25.6.0 and
  23J631 = `+0xB08`/`+0xCC0`; 24H232 = `+0xB00`/`+0xCB8`** in both classes (one slot fewer
  ahead). The plan's `+0xB08` override is `+0xB00` on 15.8.2 — same adjustment already covered by
  `AIRLAYER_PORT_14_15.md` for the other classes.
- The vtable of `AppleBCMWLANProximityInterface` of 23J631 inherits the base at `+0xB08`/`+0xCC0`
  (`vt_AppleBCMWLANProximityInterface.txt`); that of 24H232 unverified here `?`.
- The ivar offsets of this note (`+0x120`, `+0x28`, `+0x58`, `+0x190`…) belong to the 25.6.0. The
  instances of 14/15 have sizes equal to each other but different from Tahoe
  (`AIRLAYER_PORT_14_15.md`), and the known ivar shift on 14/15 does not cover these
  fields: re-verify the cited stores/loads (§2, §3, §5.1) against the kernelcaches
  23J631/24H232 before fixing per-build constants `?`.

## 10. Method and limits

- IDA 9.4 headless (`idat -A -S`) over copies of `~/re_cache/ida/boot_awdl.kc.i64` in `/tmp`
  (`boot_awdl_copy.i64`, `boot_awdl_copy2.i64`); decompilation of 31 functions and disassembly of
  the key sites; byte scan of the call patterns `call [reg+0xB08/0xCC0/0xBA8/0xDD8]`
  over the `__text` of `IO80211Family`, `AppleBCMWLANCoreMac`, `AppleBCMWLANBusInterfacePCIeMac`,
  `IOSkywalkFamily`, `corecapture` and `AppleSkywalkAVB` (4/1/84/2 sites for the first four
  slots; the 0xB08 hits outside `IO80211Family` were classified one by one as slots of other
  classes). The base kernel does not link against IO80211Family, so there are no callers outside these
  filesets `?` (dependency direction assumed, not verified dump by dump). Xrefs of
  symbols and of `gMetaClass` from the IDB's xref base (complete).
- Addresses cited first-hand in this note: all of §1–§8 and §5.3. Taken from
  prior documents without re-verification here: none (those of `withInterface`/`initWithInterface`
  and the content of `initWithInterface` extend `AWDL_PEERMANAGER_SOFTWARE.md` §2, whose factory and
  registration match what was observed).
- Explicit `?`: fine disassembly of the `+0x00` byte of the ivars and of the `start` guard;
  alloc of the controller's ivars; class of the receiving object of the `call [rax+0xCC0]` on the PCIe
  bus; release of the manager on destruction; lock of the read `ivars+0x28`; ivar
  offsets on 23J631/24H232.
