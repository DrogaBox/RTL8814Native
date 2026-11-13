# Linkage of the AirLayer against the AWDL classes of IO80211Family

This document fixes which symbols of `IO80211VirtualInterface`, `IO80211AWDLPeerManager`,
`IO80211AWDLProtocol` and adjacent classes IO80211Family exports in the boot kernelcollection,
through which a third-party kext can link them, what `RTL8814AirLayer.kext` imports today, what
stage 2 would add (reusing the kernel's `IO80211AWDLPeerManager`) and which fallbacks remain if
a future build stops exporting them. Complements `AWDL_PEERMANAGER_DRIVER_ABI.md` (the byte
contract of the manager) and `AWDL_PEERMANAGER_SOFTWARE.md` (what it computes).

Source: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin 25.6.0, md5
`f489ff8be80e6013a1bf47561dcaef45`): dump of the symbol table of the fileset entry
`com.apple.iokit.IO80211Family` and of the `com.apple.kernel` entry, plus cross-check of each cited address
against the IDB in `Build/ida_awdl/task_awdl_linkage/kc.i64` (dumps and verification script in
`Build/ida_awdl/task_awdl_linkage/`). All addresses are unslid VAs of the kernelcache.
The offsets and the composition of the table belong to this build. Anything unconfirmed is marked `?`.

---

## 0. Summary

- **Everything stage 2 needs is exported by name** in the table of the entry
  `com.apple.iokit.IO80211Family`: vtables `__ZTV`, `gMetaClass`, `C1`/`C2` constructors,
  factories (`withInterface`, `createPeerManager`) and the contract methods
  (`actionFrameInput`, `bpfTapInput`, `reportDataPathEvents`, `dequeueTxPacket`,
  `dequeueTxPackets`, `sendToBpfTap`). Nothing has to be resolved by address for the direct
  path.
- The linkage path is the one the AirLayer already uses: `OSBundleLibraries` →
  `com.apple.iokit.IO80211Family` (`1.0`), already declared in `Source/AirLayer/Info.plist`. The
  kext loader resolves the undefineds against the entry's table at load time; no address
  of IO80211Family is baked into the binary.
- Empirical verification: the 230 IO80211/CC symbols the built binary imports today
  (`Build/RTL8814AirLayer.kext`) appear in those tables; none AWDL yet.
- Real risk: IO80211Family is not a KPI and the table composition changes between builds (today
  4,433 entries of the entry are already private). The fallbacks of §3 only apply if a future build
  privatizes or removes a symbol; a resolution failure prevents loading the kext, it does not cause a
  runtime failure.

---

## 1. What the entry exports and how it links

### 1.1 Mechanism

The boot KC is an `MH_FILESET` Mach-O with one entry per kext. Each entry carries its own
symbol table in `__LINKEDIT`; entry `com.apple.iokit.IO80211Family` occupies
`__TEXT` `0xffffff8002183000`–`0xffffff8002450000` and `__DATA` from `0xffffff8002450000`, with
20,001 entries distributed by type byte as follows:

| Type | Meaning | Entries |
|---|---|---:|
| `0x0f` | `N_SECT\|N_EXT`: **exported**, linkable by kexts | 11,951 |
| `0x1e` | `N_PEXT\|N_SECT`: private (only internal localization) | 4,433 |
| `0x0e` | `N_SECT` locals | 2,614 |
| `0x01` | stubs toward other kexts (value 0; e.g. `CCPipe::withOwnerNameCapacity` and `CCStream::withPipeAndName`, defined by entry `com.apple.driver.corecapture`) | 1,003 |

A kext declares the dependency with the key `OSBundleLibraries` → `com.apple.iokit.IO80211Family`
(version `1.0`) and the loader resolves each undefined against the `0x0f` entries of that table. The
list `BootKernelExtensions.kc.elides` does not include IO80211Family: nothing of the entry is elided.

That `0x0f` = linkable is not an assumption: the 230 IO80211/CC undefineds of the built AirLayer
binary appear in the tables (225 in the IO80211Family entry, all `0x0f`; the remaining 5 in
`com.apple.driver.corecapture`), and that binary loads on 25G241 (build validated in
`KnownBuilds.inc`).

The Apple model classes (`AppleBCMWLANProximityInterface`, `AppleBCMWLANCore`,
`AppleBCMWLANInfraProtocol`) live in another entry (`com.apple.driver.AppleBCMWLANCoreMac`) and are not
needed for linkage; they are only the behavioral reference.

### 1.2 Symbols per class (verified addresses)

Exported symbols with proper name of each class (includes `const` methods):
`IO80211AWDLPeerManager` 995, `IO80211PeerManager` (base) 413, `IO80211AWDLPeer` 307,
`IO80211VirtualInterface` 179, `IO80211Buffer` 42, `IO80211AWDLProtocol` 15. Selection of the
relevant ones for stage 2:

`IO80211AWDLPeerManager` (vtable at `0xffffff800245a858`; the `vptr` of a live object points to
`__ZTV+0x10`, i.e. `0xffffff800245a868`):

| Symbol | Address |
|---|---|
| `__ZTV22IO80211AWDLPeerManager` | `0xffffff800245a858` |
| `IO80211AWDLPeerManager::gMetaClass` | `0xffffff800248d2e0` |
| `IO80211AWDLPeerManager::withInterface(IO80211VirtualInterface*, ether_addr*)` | `0xffffff80021c14bc` |
| `IO80211AWDLPeerManager::actionFrameInput(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff800220e1ae` |
| `IO80211AWDLPeerManager::reportDataPathEvents(unsigned, void*, size_t)` | `0xffffff80021dd8f4` |
| `IO80211AWDLPeerManager::dequeueTxPacket(TxPacketRequest*)` | `0xffffff800221675e` |
| `IO80211AWDLPeerManager::enableAWDL()` | `0xffffff80021dcf58` |
| `IO80211AWDLPeerManager::IO80211AWDLPeerManager()` (C1/C2) | `0xffffff80021c145c` / `0xffffff80021c148c` |
| `IO80211AWDLPeerManager::MetaClass::MetaClass()` (C1/C2) | `0xffffff80021c12f0` / `0xffffff80021c13ce` |
| `__ZTVN22IO80211AWDLPeerManager9MetaClassE` | `0xffffff800245ab98` |

`IO80211VirtualInterface`:

| Symbol | Address |
|---|---|
| `__ZTV23IO80211VirtualInterface` | `0xffffff8002472b78` |
| `IO80211VirtualInterface::gMetaClass` | `0xffffff800248e478` |
| `IO80211VirtualInterface::IO80211VirtualInterface(OSMetaClass*)` (C2) | `0xffffff800233003e` |
| `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff8002331070` |
| `IO80211VirtualInterface::bpfTapInput(mbuf*, unsigned, void*, size_t)` | `0xffffff8002330e7a` |
| `IO80211VirtualInterface::dequeueTxPackets(TxPacketRequest*)` | `0xffffff80023313b2` |
| `IO80211VirtualInterface::sendToBpfTap(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff8002331100` |
| `IO80211VirtualInterface::peerToPeerConfigureIfnet()` | `0xffffff8002331218` |
| `IO80211VirtualInterface::createPeerManager()` | `0xffffff80023308c2` |

`IO80211AWDLProtocol` and adjacent classes:

| Symbol | Address |
|---|---|
| `__ZTV19IO80211AWDLProtocol` | `0xffffff8002456440` |
| `IO80211AWDLProtocol::gMetaClass` | `0xffffff800248cde8` |
| `IO80211AWDLProtocol::IO80211AWDLProtocol(OSMetaClass*)` (C2) | `0xffffff8002198c04` |
| `IO80211AWDLProtocol::~IO80211AWDLProtocol()` (D0/D1/D2) | `0xffffff800239ef2a` / `0xffffff800239ef24` / `0xffffff8002198c24` |
| `IO80211AWDLProtocol::MetaClass::alloc()` | `0xffffff8002198c6e` |
| `__ZTV18IO80211PeerManager` (base) | `0xffffff800245d070` |
| `IO80211PeerManager::gMetaClass` (base) | `0xffffff800248d508` |
| `IO80211PeerManager::withInterface(IO80211VirtualInterface*, IO80211SkywalkInterface*)` | `0xffffff8002246c16` |
| `__ZTV15IO80211AWDLPeer` | `0xffffff8002459f80` |
| `IO80211AWDLPeer::gMetaClass` | `0xffffff800248d060` |
| `IO80211AWDLPeer::withAddressAndManager(unsigned char*, IO80211AWDLPeerManager*)` | `0xffffff80021a8dac` |
| `__ZTV13IO80211Buffer` | `0xffffff8002473e80` |
| `IO80211Buffer::gMetaClass` | `0xffffff800248e4d0` |

Creation symbols the AirLayer **already imports today** (also `0x0f`):

| Symbol | Address |
|---|---|
| `IO80211SkywalkInterface::createPeerManager()` | `0xffffff80022f7be6` |
| `IO80211Controller::createVirtualInterface(ether_addr, unsigned)` | `0xffffff800229612e` |
| `IO80211Controller::attachVirtualInterface(IO80211VirtualInterface**, ether_addr, unsigned char, bool)` | `0xffffff80022960aa` |

### 1.3 Vtables and fixups

The slot convention is the one of `AWDL_PEERMANAGER_DRIVER_ABI.md`: `vptr = __ZTV + 0x10` and cell
*n* lives at `vptr + 8n`. Verified on the manager's vtable by reading the IDB (the chained
fixups of the KC already applied): `ZTV+0x10+0x238` contains `0xffffff800220e1ae`
(`actionFrameInput`), `+0x298` contains `0xffffff80021dd8f4` (`reportDataPathEvents`) and `+0x2A8`
contains `0xffffff800221675e` (`dequeueTxPacket`). In the on-disk KC those pointers are encoded
as chained fixups (base `__HIB` `0xffffff8000100000`); in live kernel memory they are
direct VAs, so the read `*(void**)object → +0x10 + 8n` works without decoding.

Examples of symbols **not** exported (locals `0x0e`, no linking possible): the
metaclass destructors (`IO80211AWDLProtocol::MetaClass::~` at `0xffffff8002198bfa`/`0xffffff8002198ca2`), the
static `__ZL23IO80211AWDLProtocol_ktv` (`0xffffff8002492ec0`) and the initializers
`__GLOBAL__sub_I_*`. None is needed for the stage 2 paths.

---

## 2. Current imports of the AirLayer and stage 2 needs

### 2.1 Today (binary 0.32, build 25G241)

The undefined table of the built binary has 800 entries; 230 are from IO80211Family and
corecapture, resolved as follows:

- 225 against the entry `com.apple.iokit.IO80211Family`, all `0x0f`: practically all of
  `IO80211Controller` (~130 methods, `gMetaClass`, `C2`/`D2`, `_RESERVED*`), ~100 of
  `IO80211SkywalkInterface` (including `createPeerManager`, `createPeer`, `getP2PSkywalkPeerMgr`,
  `reportDataPathEvents`), the trio of subclassing of `IO80211InfraProtocol` (`gMetaClass`,
  `C2(OSMetaClass*)`, `D2`), ~90 of `IO80211InfraInterface`, plus `__ZTV21IO80211InfraInterface` and
  `__ZTV23IO80211SkywalkInterface`.
- 5 against `com.apple.driver.corecapture` (`CCPipe`, `CCStream`, `CCFaultReporter`; two of them
  also arrive as `0x01` stubs in the IO80211Family table).
- The rest: IOSkywalkFamily (85), kernel KPI (`OSMetaClass`, `IORegistryEntry`, `IOWorkLoop`,
  `getMetaClassWithName` included, used by the version gate) and bsd KPI (mbuf, etc.).

No undefined is today from an AWDL class (`IO80211AWDL*`, `IO80211VirtualInterface`): 0.

### 2.2 Stage 2 extras

All verified `0x0f`; those marked "already" are already linked:

| Role | Symbol | Address |
|---|---|---|
| Create the AWDL virtual interface | `IO80211Controller::createVirtualInterface` | already (`0xffffff800229612e`) |
| Attach it | `IO80211Controller::attachVirtualInterface` | already (`0xffffff80022960aa`) |
| Create the kernel manager on that interface | `IO80211AWDLPeerManager::withInterface(IO80211VirtualInterface*, ether_addr*)` | `0xffffff80021c14bc` |
| Factory the AWDL interfaces inherit (calls `withInterface`) | `IO80211VirtualInterface::createPeerManager` | `0xffffff80023308c2` |
| Variant of the Skywalk class (another slot, does not create the AWDL manager) | `IO80211SkywalkInterface::createPeerManager` | already (`0xffffff80022f7be6`) |
| Safe cast of the manager read from the interface | `IO80211AWDLPeerManager::gMetaClass` (for `safeMetaCast`/`OSDynamicCast`) | `0xffffff800248d2e0` |
| Cast of interfaces | `IO80211VirtualInterface::gMetaClass` | `0xffffff800248e478` |
| Action frame injection | `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff8002331070` |
| Buffer pool for that injection | `__ZTV13IO80211Buffer` + `IO80211Buffer::gMetaClass` | `0xffffff8002473e80` / `0xffffff800248e4d0` |
| Subclass the AWDL protocol (current `IO80211InfraProtocol` pattern) | `IO80211AWDLProtocol::gMetaClass` + `C2(OSMetaClass*)` + `D2` | `0xffffff800248cde8` / `0xffffff8002198c04` / `0xffffff8002198c24` |
| Direct call to the AF entry (alternative to `bpfTapInput`) | `IO80211AWDLPeerManager::actionFrameInput` | `0xffffff800220e1ae` |
| Datapath events 41/42/43 (if not going through `postMessage`) | `IO80211AWDLPeerManager::reportDataPathEvents` | `0xffffff80021dd8f4` |

Not needed: `C1`/`C2` of the manager (`withInterface` creates it), `IO80211AWDLPeer::withAddressAndManager`
(the manager registers the peers by itself, §1.6 of the ABI doc) nor `dequeueTxPacket` (the pull is
initiated by Skywalk on the interface).

---

## 3. Runtime fallbacks

With the current table they are not needed; they remain here for future builds where a symbol appears as
`0x1e`/`0x0e` or disappears. All of this section are from the `com.apple.kernel` entry (libkern
KPI, `0x0f`) and therefore stable in a way IO80211Family is not:

| Primitive | Symbol | Address |
|---|---|---|
| Metaclass by name | `OSMetaClass::getMetaClassWithName(const OSSymbol*)` | `0xffffff8000a1bef0` (already imported by the gate) |
| Instantiation by name | `OSMetaClass::allocClassWithName(const char*)` | `0xffffff8000a1c070` |
| Instantiation by name | `OSMetaClass::allocClassWithName(const OSSymbol*)` | `0xffffff8000a1bfc0` |
| Cast by name (runtime equivalent of `OSDynamicCast` without `gMetaClass` linked) | `OSMetaClassBase::metaCast(const char*)` | `0xffffff8000a1aec0` |
| Cast between metaclasses | `OSMetaClassBase::safeMetaCast` | `0xffffff8000a1ab30` |
| Cast check by name | `OSMetaClass::checkMetaCastWithName(const char*, const OSMetaClassBase*)` | `0xffffff8000a1c140` |
| Gate fingerprint | `OSMetaClass::getClassSize()` / `getSuperClass()` / `getClassName()` | `0xffffff8000a1b200` / `0xffffff8000a1c1e0` / `0xffffff8000a1b1d0` |

Possible combination without a single AWDL class symbol: `getMetaClassWithName("IO80211AWDLPeerManager")`
→ `allocClassWithName` or the `alloc` slot of its metaclass → call of methods by vtable slot
read from the live object (`vptr+0x10+8n`, pointers already resolved in memory), with `metaCast` to
verify the type before each use.

Risks:

- **Slot indices change between builds** (the offsets +0x238/+0x298/+0x2A8 belong to 25G241); the
  existing build gate (`VersionGate.h` + `KnownBuilds.inc`, only 25G241 validated) is the
  mitigation, extendable with the size fingerprint of the AWDL classes.
- **No type or signature checking**: a wrong signature corrupts or traps (reminder:
  `actionFrameInput` executes a deliberate `ud1` if `hdrLen > 0x48`).
- **Silent privatization**: a symbol moved to `0x1e` gives no runtime symptom; it breaks the
  kext's resolution and the kext does not load. The table (entry dump) should be re-verified after
  each update, the same way the vtables are re-verified.
- **The vtable fallback does not cover creation**: without `withInterface`/`createVirtualInterface`
  exported there is no clean way to build the manager nor the interface; the only option left would be reusing
  the objects the system itself creates (`?` not explored).

---

## 4. Summary table

"ST" = symbol table of the indicated fileset entry (type `0x0f` unless noted); "IDB" = address
also cross-checked against the IDB of the KC; "bin" = imported today by `Build/RTL8814AirLayer.kext`.

| Symbol | Exported | Address | Evidence |
|---|---|---|---|
| `__ZTV22IO80211AWDLPeerManager` | yes | `0xffffff800245a858` | ST-IO80211Family + IDB |
| `__ZN22IO80211AWDLPeerManager10gMetaClassE` | yes | `0xffffff800248d2e0` | ST-IO80211Family + IDB |
| `IO80211AWDLPeerManager::withInterface` | yes | `0xffffff80021c14bc` | ST + IDB |
| `IO80211AWDLPeerManager::actionFrameInput` | yes | `0xffffff800220e1ae` | ST + IDB + vtable cell 71 |
| `IO80211AWDLPeerManager::reportDataPathEvents` | yes | `0xffffff80021dd8f4` | ST + vtable cell 83 |
| `IO80211AWDLPeerManager::dequeueTxPacket` | yes | `0xffffff800221675e` | ST + vtable cell 85 |
| `IO80211AWDLPeerManager::enableAWDL` | yes | `0xffffff80021dcf58` | ST |
| `IO80211AWDLPeerManager` C1/C2 | yes | `0xffffff80021c145c`/`0xffffff80021c148c` | ST |
| `__ZTV23IO80211VirtualInterface` | yes | `0xffffff8002472b78` | ST + IDB |
| `IO80211VirtualInterface::gMetaClass` | yes | `0xffffff800248e478` | ST + IDB |
| `IO80211VirtualInterface::C2(OSMetaClass*)` | yes | `0xffffff800233003e` | ST |
| `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*,…)` | yes | `0xffffff8002331070` | ST + IDB |
| `IO80211VirtualInterface::bpfTapInput(mbuf*,…)` | yes | `0xffffff8002330e7a` | ST |
| `IO80211VirtualInterface::dequeueTxPackets` | yes | `0xffffff80023313b2` | ST + IDB |
| `IO80211VirtualInterface::sendToBpfTap` | yes | `0xffffff8002331100` | ST |
| `IO80211VirtualInterface::createPeerManager` | yes | `0xffffff80023308c2` | ST + IDB |
| `__ZTV19IO80211AWDLProtocol` | yes | `0xffffff8002456440` | ST + IDB |
| `IO80211AWDLProtocol::gMetaClass` | yes | `0xffffff800248cde8` | ST + IDB |
| `IO80211AWDLProtocol::C2(OSMetaClass*)` / `D2` | yes | `0xffffff8002198c04` / `0xffffff8002198c24` | ST |
| `__ZTV18IO80211PeerManager` (base) | yes | `0xffffff800245d070` | ST + IDB |
| `__ZTV15IO80211AWDLPeer` | yes | `0xffffff8002459f80` | ST + IDB |
| `IO80211AWDLPeer::withAddressAndManager` | yes | `0xffffff80021a8dac` | ST |
| `__ZTV13IO80211Buffer` | yes | `0xffffff8002473e80` | ST |
| `IO80211SkywalkInterface::createPeerManager` | yes (already) | `0xffffff80022f7be6` | ST + IDB + bin |
| `IO80211Controller::createVirtualInterface` | yes (already) | `0xffffff800229612e` | ST + bin |
| `IO80211Controller::attachVirtualInterface` | yes (already) | `0xffffff80022960aa` | ST + bin |
| `IO80211AWDLProtocol::MetaClass::~` (D1/D0) | no (local `0x0e`) | `0xffffff8002198bfa`/`0xffffff8002198ca2` | ST |
| `__ZL23IO80211AWDLProtocol_ktv` | no (local `0x0e`) | `0xffffff8002492ec0` | ST |
| `OSMetaClass::getMetaClassWithName` | yes (already, kernel) | `0xffffff8000a1bef0` | ST-kernel + IDB + bin |
| `OSMetaClass::allocClassWithName(const char*)` | yes (kernel) | `0xffffff8000a1c070` | ST-kernel |
| `OSMetaClassBase::metaCast(const char*)` | yes (kernel) | `0xffffff8000a1aec0` | ST-kernel |
| `OSMetaClassBase::safeMetaCast` | yes (kernel) | `0xffffff8000a1ab30` | ST-kernel |
| `OSMetaClass::checkMetaCastWithName(const char*,…)` | yes (kernel) | `0xffffff8000a1c140` | ST-kernel |
| `OSMetaClass::getClassSize/getSuperClass/getClassName` | yes (kernel) | `0xffffff8000a1b200`/`0xffffff8000a1c1e0`/`0xffffff8000a1b1d0` | ST-kernel |
