# Compatibility dependencies of the AWDL code

Inventory of everything the stage 1 AWDL code assumes from the operating system, with the
reference version (macOS 26.7.1, build 25G241, Darwin 25.6.0, `BootKernelExtensions.kc`
md5 `f489ff8be80e6013a1bf47561dcaef45`) and the cheapest recipe to re-check each assumption on a
new kernelcache (target: macOS 14–26).

Code scope: `Source/AirLayer/AWDLInterface.cpp`, `Source/AirLayer/AWDLInterface.h` and the
AWDL parts of `Source/AirLayer/RTLAirLayer.cpp`/`.hpp` (override of the 94, boot-arg `rtlawdl`,
creation/destruction of `awdl0`, capability bits). The justification of each row cites the
`Docs/AWDL_*` document that closes it and, when applicable, the section of
[`AWDL_STAGE1_IMPL_SPEC.md`](AWDL_STAGE1_IMPL_SPEC.md).

Conventions:

- `?` = not closed by the sources; the claim inherits the `?` from the source document.
- The cited VAs (e.g. `0xffffff8002b0caac`) are from 25.6.0 and rot on each version: on a
  new kernelcache every function is located by symbol, never by address.
- The fileset check tables use the symbol table of `LC_SYMTAB` per
  `LC_FILESET_ENTRY` (recipe `kcsym.py`, reference dumps in
  `Docs/re/io80211_25.6.0/syms.txt`).
- There are no ivar offsets hardcoded by hand in the project's own AWDL code: the ivars `_awdl`,
  `_awdlLock`, `_awdlArmed` (`RTLAirLayer.hpp:195-197`) are normal C++. The ivar offsets that
  appear in the documents (e.g. `*(ctrl+0x120)+0x188` for the 97) belong to the family,
  not to this code.

The last two columns of each table are: how the assumption can change between macOS versions, and
the cheapest verification over a kernelcache (or, when the constant lives in
userspace, over the corresponding binary).

## 1. Imported symbols

### 1.1 Explicit `__asm` symbols

The 14 symbols that `AWDLInterface.cpp` imports by mangled name. In parentheses, the twin of the
STA path in `RTLAirLayer.cpp`, which shares the same risk and the same check.

| Wrapper | Mangled symbol | Declaration | Use | How it can change | Cheapest verification |
|---|---|---|---|---|---|
| `rtl_sky_initRegInfo` | `__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm` | `AWDLInterface.cpp:29` (`RTLAirLayer.cpp:139`) | Validates version/size of the `RegistrationInfo` and leaves the defaults before the manual fill (`:188`) | Signature or validated size change; method removal | Symbol table of the IOSkywalkFamily fileset (defined external type); disassemble the function by symbol to read the version (1) and size (`0x130`) literals |
| `rtl_sky_setId` | `__ZN23IO80211SkywalkInterface14setInterfaceIdEj` | `:31` (`RTLAirLayer.cpp:146`) | Sets the BSD interface id 2 (`AWDL_IFACE_ID`) before `attach` (`:118`) | Signature change; removal | Symtab of the IO80211Family fileset |
| `rtl_sky_getWorkQueue` | `__ZN23IO80211SkywalkInterface12getWorkQueueEv` | `:33` (`RTLAirLayer.cpp:148`) | Work loop of the object to add/remove the three queues as event sources (`:163-170`, teardown `:234`, `:273`) | Change of the returned type or removal; if it stops being the same queue the family uses, the order with the gate breaks | Symtab; disassemble to confirm which queue it returns |
| `rtl_sky_getBSDName` | `__ZN23IO80211SkywalkInterface10getBSDNameEv` | `:35` | BSD name composed by the BSD client after the synchronous `registerService` (`:218`); empty ⇒ failure (`:219-223`) | Removal | Symtab of the IO80211Family fileset |
| `rtl_pool_withName` | `__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE` | `:37` (`RTLAirLayer.cpp:153`) | Creates the pools `rtlawdl:tx`/`rtlawdl:rx` (`:149-150`) | Signature change or change of the `PoolOptions` layout (§2.4) | Symtab of the IOSkywalkFamily fileset |
| `rtl_txsub_withPool` | `__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j` | `:40` (`RTLAirLayer.cpp:156`) | TX submission queue with non-null action `txNoop` (`:155`); `initWithPool` rejects a null action | The signature encodes the type of the action pointer: any signature change of the action breaks the mangling; new validations in `initWithPool` | Symtab; disassemble `IOSkywalkTxSubmissionQueue::initWithPool` |
| `rtl_txcomp_withPool` | `__ZN26IOSkywalkTxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j` | `:43` (`RTLAirLayer.cpp:159`) | TX completion queue with idle action `rtlAwdIdleAction` (`:157`) | Same | Symtab; same |
| `rtl_rxcomp_withPool` | `__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j` | `:46` (`RTLAirLayer.cpp:162`) | RX completion queue (`:158`) | Same | Symtab; same |
| `rtl_vif_init` | `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` | `:49` | Base init: publishes the role (6 → `AirLink`) and the prefix before `attach` (`:114`) | Signature change; renumbering of the role→name table (§3) | Symtab; disassemble `init` and the role name table |
| `rtl_txcomp_enqueue` | `__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` | `:51` | `txNoop` returns the packets through the TX completion (`:264`); requires `count < capacity` (`:261-262`) | New validations or different limits | Symtab; disassemble `enqueuePackets` |
| `rtl_pool_free` | `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `:53` (`RTLAirLayer.cpp:200`) | Return to the pool when `enqueuePackets` fails (`:266`) | Removal | Symtab |
| `rtl_vif_start` | `__ZN23IO80211VirtualInterface5startEP9IOService` | `:55` | Base start (PMinit/joinPMtree, peer manager, MAC agent) before registration (`:176`) | Signature change; new behavior that requires previous steps | Symtab; disassemble `start` |
| `rtl_ctrl_setAwdlInterface` | `__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface` | `:57` | Publishes/withdraws the object in the controller (`:337`; `RTLAirLayer.cpp:1806`, `:1823`) | No xrefs on 25.6.0 (`?`, spec §7.2): the kernel does not require it today and it can disappear in any version without anything in the system missing it; the only consumer would be `getAwdlInterface` | Symtab of the IO80211Family fileset; if absent, remove `rtlAwdlPublish` from the code |
| `rtl_eth_register` | `__ZN26IOSkywalkEthernetInterface25registerEthernetInterfaceEPKNS_16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_j` | `:60` | Datapath registration: publishes MAC, prefix, unit and media (`:206`). The STA path uses the different `registerInfraEthernetInterface` (`RTLAirLayer.cpp:142`) | Signature change (number of queues, flags) or validation changes (count 1..256, TX submission forced to 1 or 4) | Symtab; disassemble `registerEthernetInterface`/`initWithQueues` |

### 1.2 Symbols the linker resolves by inheritance

They do not go through `__asm`, but the loader demands each one at load time; the failure is a
link failure, not a runtime one.

| Group | Symbols | Reference | How it can change | Cheapest verification |
|---|---|---|---|---|
| Metaclass and structors of the base | `__ZN19IO80211AWDLProtocol11gMetaClassE`, `__ZN19IO80211AWDLProtocolC2EP11OSMetaClass`, `__ZN19IO80211AWDLProtocolD2Ev` (generated by `OSDefineMetaClassAndStructors`, `AWDLInterface.cpp:94`) | spec §5.1, [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §2.2 | Class rename or mangling change | Symtab of the IO80211Family fileset |
| Direct calls to the base | `__ZN23IO80211VirtualInterface4freeEv`, `__ZN23IO80211VirtualInterface5stopEP9IOService` (`AWDLInterface.cpp:283`, `:297`) | code | Same | Symtab |
| Inherited implementations that fill the cells 0x428–0xDC0 | ~48 symbols of `IO80211VirtualInterface` (`AWDLInterface.h:96-145`); on 25.6.0 all 93 symbols of the class are exported with type `0f` | [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §5; spec P0.5 | Any cell losing its implementation in the family leaves `__cxa_pure_virtual` or undefined in the object vtable | `nm -u` of the compiled object + symtab of the fileset (check P0.5) |
| IOKit/xnu | `IOWorkLoop`, `IOLock`, `thread_call`, `PE_parse_boot_argn`, `IOMalloc`/`IOFree` | code | Stable xnu APIs across 14–26 (low risk) | SDK headers of each version |

## 2. Offsets and sizes fixed

### 2.1 `RegistrationInfo` of `awdl0`

Struct of `IOSkywalkEthernetInterface` that the code fills byte by byte. The offsets are the
implicit contract with `initRegistrationInfo`, `registerEthernetInterface` and
`copyRegistrationInfo`.

| Field | Offset/value | Code | Justification | How it can change | Cheapest verification |
|---|---|---|---|---|---|
| Total size | `0x130` | `AWDLInterface.cpp:78` | spec §5.3; [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2, §3.4 | The struct can grow or shrink: the internal validation would stop passing | Disassemble `initRegistrationInfo`: version (1) and size literals |
| Version | `1` (3rd argument) | `:188` | spec §5.3 | The validator may require another version | Same |
| Subfamily | `+0x0C = 3` (`IFNET_SUBFAMILY_WIFI`) | `:79`, `:189` | spec §5.3 | Field shift or new semantics | Disassemble `initRegistrationInfo`/`copyRegistrationInfo` (reads) |
| Role | `+0x10 = 6` | `:80`, `:190` | spec §5.3; no reader on 25.6.0 (`?`, [`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §3) | Field without a reader today: harmless value, but a new version may start reading it with different semantics | Sweep of readers of `+0x10` in the new fileset |
| Number of media words | `+0x14 = 2` | `:81`, `:191` | spec §5.3 (STA path; `initRegistrationInfo` writes 256 on 25.6.0) | Shift | Disassemble `initRegistrationInfo` (the 256 write reveals the field) |
| `ifm_active` | `+0x24 = 0x80` (`IFM_IEEE80211`) | `:82`, `:192` | spec §5.3; birth §2.2 | Shift | Disassemble the media descriptor seeding (`0xffffff8002b0a32a` on 25.6.0, by symbol) |
| `ifm_current` | `+0x28 = 0x80` | `:83`, `:193` | spec §5.3 (D5; birth §6.1–§6.3); the daemon filter requires `(ifm_current & 0xE0) == 0x80`; the intermediate updater is not identified (`?`) | Field shift, or a new writer that replaces the seed before the daemon reads it | Disassemble the seeding and `ioctl_sifmedia`; check on the new system who rewrites `+0x28` between registration and read |
| BSD prefix | `+0x30 = &"awdl"` | `:84`, `:194` | spec §5.3; birth §3.1 | Shift; the literal must point to live memory while the ifnet exists | Disassemble `registerNetworkInterface` (publishes `IOInterfaceNamePrefix`) |
| Unit | `+0x38 = 0` | `:85`, `:195` | spec §5.3; birth §3.1 | Shift | Same (publishes `IOInterfaceUnit`) |
| Headroom | `+0x3C = 64` (u16) | `:86`, `:196` | spec §5.3; [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §6.1 (`createProxInterface` writes 64 at `+0x3C`; `+0x3A` is the high half of the unit). Reader `getTxDataOffset` (verified on 25.6.0) | Shift; the value 64 is a Broadcom precedent, not a kernel-read requirement | Reader identified on 25.6.0: `IOSkywalkNetworkInterface::getTxDataOffset` reads `u16 [copy+0x3C]` and the BSD client stores it at `+0x13C` (`AWDL_STAGE1_PENDING_CHECKS.md` §1); on a new kernelcache, disassemble `getTxDataOffset` by symbol |
| Flags | `+0x40 = 0` | `:87`, `:197` | spec §5.3; no reader on 25.6.0 (`?`, REGINFO_AND_GATES §2) | Same as `+0x10` | Reader sweep |
| MTU / max MTU | `+0x48`/`+0x4C` = `0x594` (1428) | `:88`, `:198` | spec §5.3; birth §2.4 step 4 (`0xffffff80017296ff`) | It is a driver-own value (Broadcom precedent); the kernel does not set it | Disassemble the Broadcom 94 path; `ifconfig awdl0` on the new system |
| MAC | `+0x108`, 6 B | `:89`, `:199` | spec §5.3; birth §2.2; [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2 | Shift | Disassemble `copyRegistrationInfo` (publishes `IOMACAddress`) |

### 2.2 `apple80211_virt_if_create_data` (payload of the 94)

Declared at `AWDLInterface.h:74-81`; the code consumes it at `RTLAirLayer.cpp:577-588`.

| Field | Offset | Code | Justification | How it can change | Cheapest verification |
|---|---|---|---|---|---|
| `f0` (u32, unread) | `+0x00` | `AWDLInterface.h:75` | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2; read `?` | wifip2pd may add fields in front and shift everything | Disassemble the 94 handler and the wifip2pd emitter (userspace, not kernelcache) |
| `mac[6]` | `+0x04` | `AWDLInterface.h:76`; `RTLAirLayer.cpp:588` | §3.2 | Same | Same |
| `pad0[2]` | `+0x0A` | `AWDLInterface.h:77` | §3.2 | Same | Same |
| `role` (u32) | `+0x0C` | `AWDLInterface.h:78`; `RTLAirLayer.cpp:582` | §3.2 | Same | Same |
| `ifname[16]` (output) | `+0x10` | `AWDLInterface.h:79`; `RTLAirLayer.cpp:588` | §3.2; the name is written by the handler with `strlcpy(payload+16, getBSDName(), 0x10)` | Same | Same |
| Size `32` B | `static_assert` | `AWDLInterface.h:81` | §3.2; the family handler requires `len == 32` and the legacy path copies `min(len, 32)` (spec §4.1) | The handler may require another length | Disassemble the 94 handler: length literals (32 on 25.6.0) in both dispatch tables |

### 2.3 Vtables

Convention: offset from the vptr (`vptr = ZTV + 0x10`). The full anchoring is at
`AWDLInterface.h:3-19` and spec §5.2; the `static_assert`s of `AWDLInterface.h:287-289` only protect
compilation against the 25.6.0 header, not against a new kernel.

| Block | Offsets | Code | Justification | How it can change | Cheapest verification |
|---|---|---|---|---|---|
| Declared base (`IO80211SkywalkInterface`) | up to `0xE60` | `Source/IO80211/IO80211_25_6_0.h` (inherited) | spec §5.2 | A new method in the family shifts everything that follows | Dump `__ZTV23IO80211SkywalkInterface` from the new fileset |
| 27 methods of `IO80211VirtualInterface` (`init` to `freePendingPackets`) | `0xE68`–`0xF38` | `AWDLInterface.h:149-180` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) rows 463–489; cross-checked cell by cell against `__ZTV23IO80211VirtualInterface` on 25.6.0 (0 discrepancies, `AWDL_STAGE1_CODE_REVIEW_IFACE.md`) | Insertion or removal of methods between `syncDPSStats` and `setMacAddress` shifts the 63 AWDL slots | Dump of `__ZTV23IO80211VirtualInterface`: count 27 cells between `syncDPSStats` and `setMacAddress` |
| `setMacAddress` (pure) | `0xF40` | `AWDLInterface.h:181` | [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) row 490 | May stop being pure or move | In the ZTV dump, the cell must point to `__cxa_pure_virtual` |
| 16 reserved | `0xF48`–`0xFC0` | `AWDLInterface.h:182-197` | VTABLE_SLOTS rows 491–506 | The family may consume reserved ones with real methods | ZTV dump |
| 63 pure AWDL slots | `0xFC8`–`0x11B8` | `AWDLInterface.h:206-284` | PURE_SLOTS; spec §16. Cross-checked by name and position against `__ZTV30AppleBCMWLANProximityInterface` on 25.6.0: 63 of 63 without discrepancies, including the last four (`0x11A0`–`0x11B8`) | Change of the pure set or of the order | Dump of the vtable of a concrete class implementing them (on 25.6.0 `AppleBCMWLANProximityInterface`) at `0xFC8`–`0x11B8`; the 63 must match by name against the header |
| 48 alignment overrides | `0x428`–`0xDC0` | `AWDLInterface.h:96-145` | [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §5 | Every upstream shift breaks all following cells: the object would be born with a vtable that is not the family's | `nm -u` of the object + ZTV dump (P0.4/P0.5); `createEventPipe` must remain at `0xDC0` (slot 440; `+0x1B8` is an internal pipe offset, not a slot — HANDOFF §3.3) |
| Class sizes | `0x128`/`0x130`/`0x130`, pad `0x8` | `AWDLInterface.h:199`, `:287-289` | Metaclass 0x130 verified against the kernel collection (header `:16`) | New ivar fields in the family enlarge the object: the pad and the asserts stop reflecting reality | The size literal passed to the `OSMetaClass` constructor of `IO80211VirtualInterface`/`IO80211AWDLProtocol` in the new fileset |
| Slot of the 94 in `IO80211InfraProtocol` | `+0x1160` (slot 556) | `RTLAirLayer.hpp:99-100`; `RTLAirLayer.cpp:577` | `IO80211_25_6_0.h:685`; the bridge casts to `IO80211InfraProtocol` and calls `+0x1160` (HANDOFF §3.2) | A new method in the infra hierarchy shifts the slot: the cast would land in the wrong method | ZTV dump of the infra hierarchy + disassemble the bridge `apple80211setVIRTUAL_IF_CREATE` (by symbol) |

### 2.4 Queues and pool

| Assumption | Value | Code | Justification | How it can change | Cheapest verification |
|---|---|---|---|---|---|
| Layout of `IOSkywalkPacketBufferPool::PoolOptions` | 8 fields, 40 B (`rtl_awdl_pool_options`) | `AWDLInterface.cpp:65-74` | Parity with the STA path (`RTLAirLayer.cpp:196-204`); no dedicated document (`?`) | Internal struct of the family: new fields enlarge it and `withName` may read others | Disassemble `withName`/`initWithPool`: which offsets of the options are read |
| Pool sizes | 256/256/2048/1, `poolFlags = 1` | `:142-148` | STA path + spec §5.4; `poolFlags = 1` = nosleep (`?`) | Memory budget; the flag may change meaning | Disassemble the pool client (flag→`kern_pbufpool` flags mapping) |
| Queue capacity and batch | 128; batches of 64 | `:155-158`, `:261-267` | [`AWDL_STAGE1_QUEUE_SHAPE.md`](AWDL_STAGE1_QUEUE_SHAPE.md) §1/§5/§10 (spec §5.4); `enqueuePackets` requires `count < capacity` | Different validated limits | Disassemble `enqueuePackets` and `initWithQueues` |
| Queue shape | 1 TX submission + 1 TX completion + 1 RX completion | `:205-206` | QUEUE_SHAPE: minimum the family accepts; the RX count is not validated and nothing looks at the role (`?` untested on hardware, spec §15.6) | The family may go back to requiring 4 TX (WMM) or validate by role | Disassemble `registerEthernetInterface`/`initWithQueues` |
| Non-null action on TX submission | `txNoop` mandatory | `:155` | [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §1 (`initWithPool` `0xffffff8002b0caac` rejects it) | The validation may disappear (harmless) or harden | Disassemble `initWithPool` by symbol |
| Queues are born disabled | `enable()`/`disable()` switch them | `:300-331` | `IOSkywalkPacketQueue::initWithPool` clears `enabled` and `packetSubmission` rejects a disabled queue (commits `4569206`, `e50d7cf`; spec §5.4) | The flag semantics may invert or the birth may change to enabled | Disassemble `IOSkywalkPacketQueue::initWithPool` |
| `registerService(kIOServiceSynchronous)` | synchronous inside the handler | `:215` | spec §5.5 (D4, conditional `?`: the gate of the 94 may be the same work loop and block) | The IOKit flag is stable; what can change is the gate under which the 94 runs | Identify the gate of the 94 in the new kc ([`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §5–§6); on hardware, T2.2 measures the return |

### 2.5 `apple80211_capability_data` (gate of type 12)

| Field | Offset/value | Code | Justification | How it can change | Cheapest verification |
|---|---|---|---|---|---|
| AWDL bit | byte `+0x08` bit 0 (`p[8]`), with the companion bit `+0x07` bit 0 | `RTLAirLayer.cpp:1883-1884` | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1/§7; spec §3; the copyback is `min(req_len, 21)` from `+0x04`, so `data[4]` = `+0x08` | The copyback, the size (21/28 B) or the bit tested by the daemon may move | Disassemble the handler of type 12 (copyback) in the kc; the tested bit lives in wifip2pd (binary of each version, not kernelcache) |
| NAN gate closed | byte `+0x0D` bit 0 at 0 | `RTLAirLayer.cpp:1885-1887` (no action over `p[13]` bit 0) | spec §3 | A NAN bit turned on changes the wifip2pd branch | Same |

## 3. Request codes, roles, ioctls and boot-args

| Value | Meaning | Code | Justification | How it can change | Cheapest verification |
|---|---|---|---|---|---|
| Request 94 `VIRTUAL_IF_CREATE` (RawSet, entry 93 of the set table) | Creation of the virtual interface | `RTLAirLayer.cpp:577` (override), `:1453` | spec §4.1; [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2; [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4 | Renumbering, removal or payload change | Locate `apple80211RouteExternal` (legacy and modern tables) and entry 93; symtab of `setVirtualInterfaceCreate` |
| Request 12 `CARD_CAPABILITIES` (RawGet) | Capability gate that triggers the 94 | `RTLAirLayer.cpp:1863-1892` | spec §3 | Same | Get table, entry 11 |
| Request 97 `VIRTUAL_IF_PARENT` (RawGet, entry 96) | The daemon asks for the parent after creating `awdl0`; the family serves it, no own code | no code (spec §7) | spec §7.1–§7.2 | Same; if it fails, the daemon semaphore signal stays at `?` (spec §7.3) | Get table, entry 96 |
| Request 293 companion `llw0` (RawSet) | Deliberately unanswered: the dispatch returns 102 and the daemon absorbs it | no code (D6) | spec §8; [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4 | If the daemon stops tolerating the 102, stage 4 has to create `llw0` | Set table, entry 292; daemon log on the new system |
| Role 6 = AWDL (`AirLink`) | Value sent to `rtl_vif_init` and to the `RegInfo`, and required by the payload | `AWDLInterface.cpp:92`, `:114`, `:190`; `RTLAirLayer.cpp:582` | [`AWDL_KERNEL_ROLE_TABLE.md`](AWDL_KERNEL_ROLE_TABLE.md); spec §4.1 | The kernel role→name table may renumber or rename | Disassemble `IO80211VirtualInterface::init` → `setInterfaceRole` and its table; `strings` of the fileset for `"AirLink"` |
| Role 7 → 102 (APSTA); roles 8–10 NAN (Broadcom); rest → `0xE0000001` | Ordered rejection of non-AWDL roles | `RTLAirLayer.cpp:584` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1; spec §4.3 | Same | Same |
| Interface id 2 | `AWDL_IFACE_ID`, via `rtl_sky_setId` | `AWDLInterface.cpp:91`, `:118` | birth §2.3 (Broadcom: 2 if the core version ≥ 5); the family does not validate it (`?`) | Broadcom convention, not a kernel requirement | Disassemble the `setInterfaceId` Broadcom does on the 94 path |
| Unit 0 + prefix `"awdl"` → `awdl0` | Final name of the ifnet | `AWDLInterface.cpp:194-195` | birth §3.1 | Name collision if another stack takes the prefix | `ifconfig`/`ioreg` on the new system |
| Media `0x80` (`IFM_IEEE80211`) and filter `(ifm_current & 0xE0) == 0x80` | What the media descriptor must report for the daemon to accept the interface | `AWDLInterface.cpp:192-193` | birth §6.1–§6.3; the filter belongs to wifip2pd | `IFM_IEEE80211` comes from `<net/if_media.h>` (stable); the filter lives in wifip2pd | `SIOCGIFMEDIA` on `awdl0`; disassemble the filter in the wifip2pd binary of each version |
| Ioctls `SIOCGIFMEDIA` `0xC02C6938`, `OSIOCGIFMEDIA` `0xC0286938`, `SIOCSIFMEDIA` `0xC0206937` | The code does not issue them; it depends on `ioctl_sifmedia` continuing to fail before the store on `awdl0`, so that `0x80` survives | no code | spec §6.3; [`AWDL_STAGE1_SIOCSIFMEDIA.md`](AWDL_STAGE1_SIOCSIFMEDIA.md) §5/§7 | The numbers are BSD-stable; the behavior of `ioctl_sifmedia` (failure without `IOMediumDictionary`) may change | Test `ioctl` on `awdl0`; disassemble `ioctl_sifmedia` |
| Entitlement `com.apple.wifi_awdl.event_monitor` | Family gate in `createEventPipe` (`0xE00002E2` without it); the daemon monitoring registration depends on the path returning 0 | no code | spec §9; [`AWDL_STAGE1_DISPATCH_PATH.md`](AWDL_STAGE1_DISPATCH_PATH.md) §2 | String or gate change | `strings` of the IO80211Family fileset; disassemble `createEventPipe` |
| `EEXIST` (17), `0xE0000001`, `0xE00002BC`, `kIOReturnUnsupported`, `kIOReturnBadArgument` | Handler return codes | `RTLAirLayer.cpp:584`, `:1790`, `:1803`; spec §4.3, §11 | The `kIOReturn*` are IOKit (stable); `EEXIST` is errno (stable); `0xE0000001` is a Broadcom convention, not a system value | Low; a change would only affect the daemon parsing | — |
| Boot-arg `rtlawdl` | Arms the AWDL path (inert without it) | `RTLAirLayer.cpp:1453-1457` | spec §2 D7 | Own name of the kext; does not depend on the OS. `PE_parse_boot_argn` is a stable KPI | Code |
| Boot-args `rtlair`/`rtlairstep`/`rtlairtrust`/`rtlairnogate` | Precondition: without `rtlair` the controller does not start and the 94 does not exist | `RTLAirLayer.cpp:1444-1452`; `VersionGate.h` | spec §14; previous layer practice | Same | Code |

## 4. Scope notes

- The `static_assert`s of `AWDLInterface.h` (`:81`, `:287-289`, `:405-406`) only cross-check
  against the 25.6.0 header; against a new kernel only the checks of the right-hand columns
  count.
- The constants that live in wifip2pd (tested capability bit, media filter, post-create
  sequence, tolerance to the 102 of the 293) are not checked on a kernelcache: they require the
  daemon binary of each version.
- Open items carried by the sources (`?`): read of `RegInfo+0x10` and `+0x40`, consumer of the
  `ifnet` field that receives the headroom from `bsdClient+0x13C`, registration of
  a third-party subclass under `IO80211AWDLProtocol` (spec §15.10), shape of 3 queues untested
  on hardware (spec §15.6), blocking of the synchronous `registerService` under the gate of the 94 (D4),
  field `f0` of the 94 payload, acceptance of the event pipe for an external driver interface
  (spec §9).
- The `?` of `IO80211VirtualInterface::init` in the symbol selection of
  [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2 is closed for 25.6.0 (type `0f`
  confirmed, spec §15.8); on a new kernelcache it becomes again the first check of the
  list: fileset symtab before compiling.
