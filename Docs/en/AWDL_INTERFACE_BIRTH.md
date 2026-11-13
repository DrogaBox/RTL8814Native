# Birth of the AWDL virtual interface in the kernel (macOS 26 / BootKernelExtensions)

How `awdl0` is born inside the kernel from the moment the modern handler of `VIRTUAL_IF_CREATE`
invokes the +0x1160 slot of `IO80211InfraProtocol` with role 6 until the `IOInterfaceName` property
appears in the IORegistry and fires the `IOServiceMatched` notification that releases `wifip2pd`
(see [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1). It covers: the Broadcom
path of role 6, the choice of BSD name and unit, ifnet registration, published properties, the
`registerService` ↔ BSD attach order and the response of `SIOCGIFMEDIA`.

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), IDA 9.4 headless
(`idat -A -S`) over copies of `~/re_cache/ida/boot.kc.i64` in
`Build/ida_awdl/task_awdl_birth/bcm` and `.../fam` (scripts `bcm_birth.py`, `fam_birth*.py`, outputs
`out*.txt`), symbols from `Docs/re/io80211_25.6.0/` and the binary `wifip2pd.x86_64`
(md5 `a120cfccfa2783303604ce5be9e1763a`) to verify the user filter. Addresses are
kernel VAs unless noted. Anything unverified goes as `?`. The entry point is already closed in
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4: the modern handler validates
`len == 32` and calls the bridge that casts to `IO80211InfraProtocol` and executes slot +0x1160,
which in the Broadcom kext is `AppleBCMWLANCore::setVIRTUAL_IF_CREATE`.

## 1. Short answer

1. Role 6 reaches `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`), which reads the
   role from the payload at `+0x0C` (`0xffffff800167891c`) and calls
   `AppleBCMWLANCore::createProxInterface` (`0xffffff800165dcbe`, call at `0xffffff8001678bdd`).
   It creates `AppleBCMWLANProximityInterface` via `withOptions(core, mac=payload+4, "awdl")`
   (`0xffffff800165dd6d`), attaches it to the controller (`IOService::attach` through slot +0x670
   at `0xffffff800165dd92`), fills a 0x130-byte `IOSkywalkEthernetInterface::RegistrationInfo`
   (prefix `"awdl"` at +0x30, unit 0 at +0x38, MAC at +0x108, media at +0x24/+0x28) and calls
   `AppleBCMWLANProximityInterface::start(core, RegInfo)` (`0xffffff800165dead`).
2. `Proxi::start` (`0xffffff8001729376`) calls the base `start` through the vtable imported from
   `IO80211AWDLProtocol` (`[r14+0x5D0]` at `0xffffff80017293a5`, which resolves to
   `IO80211VirtualInterface::start` → `IO80211SkywalkInterface::start`), registers the queues with
   the Skywalk bus and calls `IOSkywalkEthernetInterface::registerEthernetInterface`
   (`0xffffff800172973d`), which copies the `RegistrationInfo` (and publishes `IOMACAddress`) and
   registers the interface in the Skywalk family publishing `IOInterfaceNamePrefix`/`IOInterfaceUnit`.
3. On success, `Proxi::start` calls **`IOService::registerService(2)`** (synchronous) through slot
   +0x5B0 at `0xffffff800172982a`. Inside that `registerService` the whole BSD part runs: the
   driver `IOSkywalkNetworkBSDClient` starts, composes `"awdl0"` with `snprintf("%s%u",
   getBSDNamePrefix(), unit)`, publishes `"BSD Name"` and `IOInterfaceName`, and creates the ifnet
   with `kern_nexus_controller_alloc_net_provider_instance` → `ifnet_allocate_extended` →
   `prepareBSDInterface` (publishes `IOInterfaceName` again) → `bpfattach` → `ifnet_attach`.
4. On return, `setVIRTUAL_IF_CREATE` writes the name into the payload with
   `strlcpy(payload+16, getBSDName(), 0x10)` (`0xffffff8001678a2b`) and returns 0. The
   `IOServiceMatched` notification of `wifip2pd` fires when `IOInterfaceName` appears for the first
   time over the already-registered entry of the interface (inside
   `IOSkywalkNetworkBSDClient::start`).
5. `SIOCGIFMEDIA` on `awdl0` ends up being answered by
   `IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`), which copies the media state
   of the interface: `ifm_active` ← state+48 (seed `0x80` = `IFM_IEEE80211`), `ifm_current` ←
   state+52 (seed `8`), `ifm_status` ← state+56 (seed `0`). The filter of `wifip2pd`
   (`(ifm_current & 0xE0) == 0x80`, verified in its binary at `0x100043ccc`) requires
   `ifm_current` to carry the 802.11 type; how `ifm_current` gets updated between the birth and
   the daemon read was not closed in this pass (`?`), but for the kext the practical
   consequence is unique: answer `SIOCGIFMEDIA` with media words of type `IFM_IEEE80211`.

As a side effect, the `?` of the chain above `IO80211SkywalkInterface` left by
[`AWDL_PROXIMITY_VTABLE_HIERARCHY.md`](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) §1 gets closed: the real
hierarchy is
`AppleBCMWLANProximityInterface` : `IO80211AWDLProtocol` : `IO80211VirtualInterface` :
`IO80211SkywalkInterface` : **`IOSkywalkEthernetInterface`** : **`IOSkywalkNetworkInterface`**
(kext `com.apple.iokit.IOSkywalkFamily`) — evidence in §2.4.

## 2. The role 6 path in the Broadcom controller

### 2.1 The gate: `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`)

| Step | Address | Evidence |
|---|---|---|
| Role read: `mov eax, [payload+0x0C]` | `0xffffff800167891c` | disasm |
| Roles 8–10 → `createNANInterface` (tail-jmp `0xffffff8001678952` to `0xffffff8001678490`) | `0xffffff800167891f`–`0xffffff8001678952` | `lea ecx,[rax-8]; cmp ecx,3; jnb` |
| Role 6 → Proximity path (`cmp eax,6; jnz` to the role 7 path at `0xffffff8001678a94`) | `0xffffff8001678957` | disasm |
| Role 7 → `AppleBCMWLANIO80211APSTAInterface::withOptions(this, payload+4, 7, payload+16)` | call at `0xffffff8001678c26` (to `0xffffff8001701c3a`) | disasm |
| Invalid role → `0xE0000001` + log `"Invalid virtual interface role 0x%X name %s"` | `0xffffff8001678b79` | disasm |
| Prox interface already exists (slot `core->ivars+0x2C48`, i.e. +11336) → result `0x11` (17) and jumps anyway to the name-writing block | `0xffffff80016789e4`–`0xffffff80016789f9` (`r15d=0x11` at `0xffffff80016789eb`, `cmp qword [rax+0x2C48],0; jz create`) | disasm |
| `createProxInterface(this, payload)` | call at `0xffffff8001678bdd` | disasm |
| Success (0) → `jmp 0xffffff8001678e61` → `xor r15d,r15d; jmp 0xffffff80016789ff` (shared name-writing block) | `0xffffff8001678be7`, `0xffffff8001678e61`–`0xffffff8001678e64` | disasm |
| Name write (common to both paths): zero at payload+0x18 and +0x10, `getBSDName()`, `strlcpy(payload+16, name, 0x10)` | zeros `0xffffff8001678a03`/`0xffffff8001678a09`; call `getBSDName` `0xffffff8001678a1b`; **call `_strlcpy` `0xffffff8001678a2b`** (`_strlcpy` = `0xffffff800048a270`) | disasm |
| Log `"create virtif <%s> role %u"` (line 34181/0x8585) | `0xffffff8001678a76` | disasm |

Structure of the 32 B payload consumed here: role u32 @+0x0C, MAC 6 B @+0x04, returned BSD name
@+0x10..+0x1F (matches what `wifip2pd` builds in
[`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.4).

### 2.2 `AppleBCMWLANCore::createProxInterface` (`0xffffff800165dcbe`)

1. If `core->ivars+0x2C48` already has an interface → log `"already created"` (`0xffffff800165dd37`)
   and returns 0 without re-creating (`0xffffff800165dce8`).
2. `AppleBCMWLANProximityInterface::withOptions(this, payload+4, "awdl")` — call at
   `0xffffff800165dd6d`; argument preparation: `add rsi,4` (rsi = payload+4, the **MAC the
   daemon put in the payload**) at `0xffffff800165dd5c` and `lea r15,[0xffffff8001797376]` (the
   string **`"awdl"`**) at `0xffffff800165dd60`, passed as the 3rd argument (`rdx`, the `char*`
   name parameter) at `0xffffff800165dd6a`.
3. Stores the result in `core->ivars+0x2C48` (`0xffffff800165dd79`) and calls **`attach`** through
   slot +0x670 of the Proxi own vtable with the core as provider
   (`call [rcx+0x670]` at `0xffffff800165dd92`; slot +0x670 is `IOService::attach`).
4. Builds the `RegistrationInfo` on the stack (0x130 bytes): `bzero` (`0xffffff800165dda7`) +
   `IOSkywalkEthernetInterface::initRegistrationInfo(proxi, RegInfo, 1, 0x130)`
   (call at `0xffffff800165ddc7` to `0xffffff8002b108e2`; validates magic `version=1, size=0x130`
   and fills the defaults: +0x14 = 256, +0x24 = 34, +0x28 = 32, +0x30 = prefix from
   `getBSDNamePrefix`, +0x38 = `getBSDUnitNumber`, +0x44 = 46, +0x48/+0x4C = MTU 1500) and then
   overwrites its own fields:
   - `RegInfo+0x14 = 2` (`0xffffff800165ddcc`) — media word count = 2.
   - 6 B MAC into `RegInfo+0x108..0x10D` from slot +0xCF8 of the interface (`call [rax+0xCF8]`
     at `0xffffff800165dde6`, getter of the MAC the init set from the payload MAC).
   - qword `0x0000000800000080` at `RegInfo+0x24` (`0xffffff800165de2d`–`0xffffff800165de37`):
     **u32 @+0x24 = `0x80` (`IFM_IEEE80211`), u32 @+0x28 = `8`** — the media pair.
   - `RegInfo+0x0C = 3` (`0xffffff800165de3c`).
   - `RegInfo+0x30 = &"awdl"` (`0xffffff800165de82`) — BSD prefix.
   - `RegInfo+0x38 = 0` (`0xffffff800165de87`) — **unit 0**.
   - `RegInfo+0x3A = 64` (u16, `0xffffff800165de8c`).
   - `RegInfo+0x40 = 4 or 6` depending on `checkPropertyTrue(core, "IOPMResetPowerStateOnWake")`
     (`0xffffff800165de6b`–`0xffffff800165de94`).
5. `AppleBCMWLANProximityInterface::start(proxi, core, RegInfo)` — call at `0xffffff800165dead`.
   `withOptions` failure → `0xE00002BC` (`0xffffff800165dec0`).

### 2.3 `AppleBCMWLANProximityInterface::withOptions` and `init` — the init chain

`withOptions` (`0xffffff80017159e6`, already described in
[`AWDL_KERNEL_ROLE_TABLE.md`](AWDL_KERNEL_ROLE_TABLE.md)
§3.2): allocates 320 B with `OSObject_typed_operator_new`, sets the vptr `0xffffff80017f5b78` and
calls slot +0x11C8 with `(self, core, mac, 6, "awdl")` — the literal 6 at `0xffffff8001715a51`.

`AppleBCMWLANProximityInterface::init(Core*, ether_addr*, unsigned, char*)` (`0xffffff8001718db0`,
fully decompiled in this pass):

| Step | Address | Note |
|---|---|---|
| Own 0-arg `init()` (slot +0x88) | `0xffffff8001718dde`–`0xffffff8001718de1` | `Proxi::init()` (`0xffffff8001718d72`) allocates its own ivars (0x288 B, `this+312`) and calls the base `init()` through the imported slot `off_FFFFFF800246AF98` |
| `IO80211VirtualInterface::init(core, mac, 6, "awdl")` through slot `off_FFFFFF80024572B8` | call at `0xffffff8001718e09` | = `0xffffff80023305f8` (closed in [`AWDL_KERNEL_ROLE_TABLE.md`](AWDL_KERNEL_ROLE_TABLE.md) §6) |
| `setInterfaceId(2)` if core version ≥ 5, else `setInterfaceId(0xFFFFFFFF)` | `0xffffff8001718e27` (decompiled: branches `< 5` / `else`) | |
| `ivars+0x2B0 = 6` (role in Broadcom ivars) | `0xffffff8001718ec5` | see also [`AWDL_PROXIMITY_VTABLE_HIERARCHY.md`](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) §3 |
| `AppleBCMWLANTaskmaster::withWorkQueue(core->workQueue, 0x10)` | `0xffffff8001718ea4` | stored in ivars+56 |
| AWDL defaults: lifetime 6 (`ivars+688`), guard time 20 (+700), maxNoMasterPeriod 4 (+696), election OUI at +567..+574, etc. | `0xffffff8001718ec5`–`0xffffff8001718f60` approx. (store block of the pseudocode) | literal values in the pseudocode |
| Boot-args `awdl_log_flags_config`, `awdl_max_peers` (+908), `awdl_low_latency_default_mapping` (+912), `awdl_low_latency_tx_requeue` (+916) | `PE_parse_boot_argn` in the final block | |
| Tx Power Manager: `core->...->+5560` → ivars+1000; if missing, error and `freeResources` | end of the function | |

`IO80211VirtualInterface::init` (`0xffffff80023305f8`) does, in order: `IO80211SkywalkInterface::init(this, controller, mac)`
(`0xffffff80022f56e2`: calls the `init(OSDictionary*)` of `IOSkywalkEthernetInterface` through
the imported slot `off_FFFFFF8002B3F3D0` = `__ZTV26IOSkywalkEthernetInterface+0x258` (vptr+0x248),
allocates the 0xF0 B ivars with `initIvars` (`0xffffff80022f520c`) and copies the 6 B MAC from the
argument to `ivars+228`); then `setInterfaceRole(this, 6)` (`0xffffff800233064b`), `IOLockAlloc`,
BPF list, `registerInterestedDriver(controller, this)` + `controller->open(this)`, boot-arg
`wlan.entitlement`, denied-process dictionary (`OSDictionary::withCapacity(5)`) and
`IO80211FlowQueueDatabase::initialize`. The call with the name hint (`"awdl"`) produces no visible
write in this init besides the ivars (`?` internal use of the hint).

### 2.4 `AppleBCMWLANProximityInterface::start(core, RegInfo)` (`0xffffff8001729376`) and the chain up to registration

1. **Base start through a qualified vtable**: `r14 = off_FFFFFF80017DE1C0` (the imported pointer to
   `__ZTV19IO80211AWDLProtocol`) and `call [r14+0x5D0]` at `0xffffff80017293a5` — the
   vptr+0x5C0 slot (start) of the base vtable, which contains
   `IO80211VirtualInterface::start` (`0xffffff80023308aa`, a simple forward to
   `IO80211SkywalkInterface::start` `0xffffff80022f5792`). Failure → log
   `"ERROR: awdl skywalk start fail"` (`0xffffff800172941b`).
2. `IO80211SkywalkInterface::start` (`0xffffff80022f5792`, fully decompiled): panic if called
   twice; meta-cast of the provider to `IO80211Controller`; **per-role counter**
   `intfRoleToNumberMap[6]++` (`0xffffff80022f58d4`; map at `0xffffff800248df70`) for the pipe name
   `snprintf("Interface_%s_%u", "AirLink", counter)`; `CCPipe` `"com.apple.iokit.IO80211Family"`;
   `PMinit` + `joinPMtree` + `registerPowerDriver`; **peer manager** (for role ≠ 2 it creates its
   own); `IO80211Glue::withOptions` **only for role 1** (Infrastructure) — the AWDL (role 6) gets
   no glue; `IO80211MacAddressAgent::withOptions` with the MAC from ivars+228; conditional
   property `IOPMResetPowerStateOnWake` if the controller has it.
3. Back in `Proxi::start`: resolves the `AppleBCMWLANBusSkywalk` (`safeMetaCast` at
   `0xffffff8001729499`, stored at ivars+0x3E0) and registers it with the parameter array that
   includes the pointers to the `IOSkywalkPacketQueue` via their slot +0x118
   (`call [rax+0x118]` at `0xffffff800172958b`; failure → log
   `"Failed to register interface with bus interface"`); it also obtains the shared
   `IO80211WorkQueue` of the core (slot +0xC68, call at `0xffffff8001729459`, stored at
   ivars+0x450 — the same queue the base `start` caches at ivars+56) and registers in it every
   queue through slot +0x140 of the work queue (`0xffffff8001729672`–`0xffffff80017296f4`),
   storing the returned queue-ids in ivars+0x440... (indexed by ivars+0x3B4).
4. If the `RegInfo` came in (the birth path), writes `RegInfo+0x48 = 0x59400000594`
   (`0xffffff80017296ff`–`0xffffff8001729709`) and calls
   **`IOSkywalkEthernetInterface::registerEthernetInterface(this, RegInfo, queues, nqueues+2, txPool, rxPool, 0)`**
   — call at `0xffffff800172973d` to `0xffffff8002b10a84`.
5. `registerEthernetInterface` (`0xffffff8002b10a84`): `copyRegistrationInfo(this, RegInfo)`
   (`0xffffff8002b109f0`: validates magic, allocates 0x130 B, copies the RegInfo into the interface
   ivars and publishes **`IOMACAddress`** — see §4) and then
   `IOSkywalkNetworkInterface::registerNetworkInterface(RegInfo, logicalLink, pools, 0)`
   (`0xffffff8002b0a204`) — see §3. Error → `deregisterEthernetInterface` and queue cleanup
   (slots +0x148 of `r14` at `0xffffff8001729797`–`0xffffff800172981a`); log
   `"registerEthernetInterface returned 0x%x"` (`0xffffff8001729880`).
6. With result 0: **`registerService(2)`** — `mov esi,2; call [rax+0x5B0]` at
   `0xffffff8001729825`–`0xffffff800172982a` (slot +0x5B0 = `IOService::registerService`, whose
   implementation is `0xffffff8000a93250`; the value 2 = `kIOServiceSynchronous`) — and right after
   `setRunningState(false)` through slot +0x938 (`call [rax+0x938]` with esi=0 at
   `0xffffff8001729838`; = `IO80211SkywalkInterface::setRunningState`). Returns 0.
7. There is also the IOKit entry `AppleBCMWLANProximityInterface::start(IOService*)`
   (`0xffffff8001729348`, slot +0x5C0 of its own vtable) that delegates to `start(core, RegInfo)`
   (xref from `0xffffff8001729371`); in the role 6 birth the core calls the two-argument one
   directly and never goes through the IOKit one (`?` if someday it starts via IOKit).

The real base class: the qualified slots used in the chain point to the vtable of
`IOSkywalkEthernetInterface` (`__ZTV26IOSkywalkEthernetInterface` = `0xffffff8002b3f178`): the base
init via `ZTV+0x258` (`0xffffff8002B3F3D0` → vptr+0x248 =
`IOSkywalkEthernetInterface::init(OSDictionary*)`, `0xffffff8002b10820`), and the base
`prepareBSDInterface` via `ZTV+0x8F8` (`0xffffff8002b3fa70` →
vptr+0x8E8 = `IOSkywalkEthernetInterface::prepareBSDInterface`, `0xffffff8002b10e58`; data xrefs
from `0xffffff8002b3fa70` and `0xffffff8002b3c658`). Together with the signatures with
`IOSkywalkEthernetInterface::RegistrationInfo` in the Broadcom `start`s (e.g.
`0xffffff8001729376`), the chain `... : IO80211SkywalkInterface` :
**`IOSkywalkEthernetInterface` : `IOSkywalkNetworkInterface`** of the IOSkywalkFamily kext is
established.

## 3. BSD name `awdl0`, unit and ifnet registration

### 3.1 Where prefix and unit come from

| Piece | Value | Producing function | Address |
|---|---|---|---|
| Prefix | `"awdl"` (string at `0xffffff8001797376`) | `AppleBCMWLANProximityInterface::getBSDNamePrefix` | `0xffffff8001715a7c` (validates it with `validateDispatchQueue` and returns the literal) |
| Alternative prefix of the RegInfo | the same pointer `0xffffff8001797376` written to `RegInfo+0x30` | `AppleBCMWLANCore::createProxInterface` | `0xffffff800165de82` |
| Unit | `0` (u32 at `RegInfo+0x38`) | `AppleBCMWLANCore::createProxInterface` | `0xffffff800165de87` |
| Unit read | `getBSDUnitNumber()` = `*(copied RegInfo + 0x38)` | `IOSkywalkNetworkInterface::getBSDUnitNumber` | `0xffffff8002b0ba70` (dereferences `ivars+0xC0 → RegInfo+56`; `-1` if no RegInfo) |
| Composed name 1 | `snprintf(buf16, "%s%u", getBSDNamePrefix(), unit)` → `"awdl0"` | `IOSkywalkNetworkBSDClient::start` | `0xffffff8002aff562` (the call to `getBSDNamePrefix` via slot +0x9E8 at `0xffffff8002aff6xx` and the `snprintf` `_snprintf` = `0xffffff80008eaa10`) |
| Composed name 2 | `snprintf(ivars+1, 0x10, "%s%u", ifnet_name(ifp), ifnet_unit(ifp))` | `IO80211SkywalkInterface::getBSDName` | `0xffffff80022f5fca` (ifnet via slot +0x8F8 `getBSDInterface`; `_ifnet_name` `0xffffff80006401d0`, `_ifnet_unit` `0xffffff8000640230`) |

The unit of `awdl0` is **a driver value** (0 written by `createProxInterface`), not the global
per-role counter: `intfRoleToNumberMap` (`0xffffff800248df70`) has only two readers
(`IO80211SkywalkInterface::start` at `0xffffff80022f58d4` for the pipe name
`Interface_AirLink_%u`, and `getInterfaceRoleCounter` `0xffffff80022f5eb6`) and does not take part
in the BSD name. For role 7 (AP) the core writes unit 1 in the same field (`buf[56] = 1` on the
path of `setVIRTUAL_IF_CREATE`), so the prefix+unit scheme is common to all Broadcom virtual
interfaces.

### 3.2 Registration of the interface in the Skywalk family (`registerNetworkInterface`)

`IOSkywalkNetworkInterface::registerNetworkInterface(RegInfo, logicalLink, pools, flags)`
(`0xffffff8002b0a204`, called from `0xffffff8002b10a84` via the variant with
`IOSkywalkPacketQueue**` that creates the `IOSkywalkLogicalLink` with an `IOSkywalkQueueSet`):

1. Validates the RegInfo (`version==1`, `size ≥ 0x108`, `RegInfo+0x14 ≤ 0x400`).
2. Copies 0x108 B of the RegInfo into its own object and attaches it to `ivars+0xC0`.
3. Calls **`getBSDNamePrefix()`** (slot +0x9E8, `0xffffff8002b0a2b4`) and
   **`getBSDUnitNumber()`** (slot +0x9F0, `0xffffff8002b0a2cd`) on the interface itself.
4. Publishes **`IOInterfaceNamePrefix`** (`setProperty` slot +0x270, call at
   `0xffffff8002b0a2e7`) and, if the unit ≥ 0, **`IOInterfaceUnit`** as a 32-bit
   `OSNumber` (slot +0x280, call at `0xffffff8002b0a310`).
5. Copies the media pair into the interface state: `state+0x30 (48) ← RegInfo+0x24` (`0x80`) at
   `0xffffff8002b0a32a` and `state+0x34 (52) ← RegInfo+0x28` (`8`) at `0xffffff8002b0a332`
   (the state is the struct attached to `ivars+0xC0`; its fields +48/+52/+56 are the ones
   `ioctl_gifmedia` later reads, see §6).
6. Marks the default logical link (`setDefaultLogicalLink` `0xffffff8002b0a37e`,
   `setInterface` `0xffffff8002b0a38d`) and orders it in an `OSOrderedSet`.

### 3.3 BSD registration: `IOSkywalkNetworkBSDClient::start` and the ifnet

`IOSkywalkNetworkBSDClient::start(IOService*)` (`0xffffff8002aff562`) is an IOKit driver whose
provider is the freshly registered interface (the matching triggered by the previous
`registerService(2)` starts it):

1. Requires the **`IOInterfaceUnit`** property of the provider (getProperty via slot +744 and cast
   to `OSNumber`); without it aborts. From there it takes the unit (`v12`).
2. Installs its handlers (`ioctlHandler` `0xffffff8002affce6`, `setPowerStateHandler`,
   `reportInterfaceWakeFlags` at `this+33..35`), does `openInterface`.
3. Composes `"awdl0"` with `snprintf("%s%u", getBSDNamePrefix(), unit)`; calls
   `setBSDName("awdl0")` (slot +0x900, stores at `state+65`);
   publishes **`"BSD Name"`** (slot +0x270, call at `0xffffff8002aff84d`, only if
   `getBSDUnitNumber() ≥ 0`) and **`IOInterfaceName`** (slot +0x270, call at
   `0xffffff8002aff86b`) on the interface. **The first publication of `IOInterfaceName` happens
   here.**
4. Calls `interface->initBSDInterfaceParameters(eparams, &sdl)` (slot +0x8E0 at
   `0xffffff8002aff7xx`): the override of `IO80211SkywalkInterface`
   (`0xffffff80022f635c`) and the base of `IOSkywalkEthernetInterface`
   (`0xffffff8002b10bca`) fill the 0x158 B `ifnet_init_eparams` (version 2):
   `+16` = pointer to the MAC (RegInfo+0x108), `+24` = 6, `+296/+304` = broadcast, `+216..+248` =
   `ether_demux/ether_add_proto/ether_del_proto/ether_check_multi/ether_frameout`, `+44` and `+320`
   from vtable slots 308/309, `+48` = 6 (`IFT_ETHER`). The BSDClient additionally writes
   eparams+32 = prefix and eparams+40 = unit (in its frame `v39[3]`/`v39[4]`); the exact internal
   mapping of those fields inside `ifnet_allocate_extended` was not followed in xnu (`?`), but the
   observable result is the ifnet `awdl0` with unit 0.
5. `registerNexusControllerProvider` and then
   **`kern_nexus_controller_alloc_net_provider_instance`** (call with retries ×50 every 100 ms;
   `0xffffff80009a4140`):
   - `ifnet_allocate_extended(eparams, &ifp)` — call at `0xffffff80009a420d` (kernel
     `0xffffff800063f3f0`).
   - `nx_create` of the netif provider.
   - Preparation callback `IOSkywalkNetworkBSDClient::prepareNexusCallback`
     (`0xffffff8002b00340`, passed in the params) → gated →
     **`IOSkywalkNetworkBSDClient::gatedPrepareNexus`** (`0xffffff8002b0066c`): calls
     `interface->prepareBSDInterface(ifp)` (slot +0x8E8) and **`bpfattach(ifp, ...)`**
     (`_bpfattach` = `0xffffff80005be690`).
     - `IO80211SkywalkInterface::prepareBSDInterface` (`0xffffff80022f63b6`): calls the Ethernet
       base through the imported vtable (`off_FFFFFF8002450128` =
       `__ZTV26IOSkywalkEthernetInterface`, `call [rax+0x8F8]` at `0xffffff80022f63cd`), calls
       `getBSDName()` (`0xffffff80022f63d9`) and
       **publishes `IOInterfaceName` again** with `setPropertyHelper(this, "IOInterfaceName", name)`
       (helper `__Z17setPropertyHelperP9IOServicePKcS2_` = `0xffffff8002311f57`; **call at
       `0xffffff80022f63eb`**); for role 2 it sets MTU 1500 (`ifnet_set_mtu` at
       `0xffffff80022f640a`) and for all `ifnet_set_eflags(0x200000, 0x200000)`
       (`0xffffff80022f6432`).
       - `IOSkywalkNetworkInterface::prepareBSDInterface` (`0xffffff8002b0aae2`, base): stores the
         ifp at `state+8`, sets the MTU from `RegInfo+0x4C` (1500) and marks `state+64 = 1`.
   - **`ifnet_attach(ifp, sdl)`** — call at `0xffffff80009a42df` (kernel `0xffffff80005e8d20`); the
     `sockaddr_dl` comes out of `initBSDInterfaceParameters` (embedded in the copy of the RegInfo
     at +0x1E, with the MAC at +0x26..+0x2B).
6. Publishes **`IOInterfaceType`** = 6 with the value of `eparams+48` (setProperty slot +0x280 in
   the final stretch of `BSDClient::start`), `IOSkywalkNexusUUID`, PM management (or
   `makeUsable`), `startIOReporting` and ends with **`registerService(0)` of the BSD client
   itself** (slot +0x5B0 with esi=0 at `0xffffff8002aff8xx`, final stretch of the function).

In `IO80211VirtualInterface::prepareBSDInterface` (`0xffffff8002331ada`, override of slot +0x8E8
for virtuals) the BPF client is also checked and it logs
`"ERROR: cannot attach to bpf"` if there is no consumer. The generic path
`IO80211Controller::attachVirtualInterface` (`0xffffff80022960aa`: create via slot +0xDD8 →
attach via +0x670 → optional `registerService(0)` via +0x5B0) **does not take part in role 6** —
the Broadcom core does create/attach/start by hand; it serves as contrast (for role 7 the factory
`IO80211Controller::createVirtualInterface` `0xffffff800229612e` only accepts role 7 and calls
`IO80211VirtualInterface::init` with `7, "ap"`).

## 4. IORegistry properties published at birth

All on the registry entry of the **interface object** (`AppleBCMWLANProximityInterface`), except
where noted. Common write helper: `setPropertyHelper(IOService*, char const*, char const*)` =
`0xffffff8002311f57`; the `setProperty` calls with non-string values go through slots +0x270
(object) and +0x280 (integer) of the vtable.

| Property | Value | Written by | Call site | Moment |
|---|---|---|---|---|
| `IO80211VirtualInterfaceRole` | `"AirLink"` (table of roles 6–10, key at `0xffffff800246bdd8[5]` → `0xffffff800241dbc8`, value `0xffffff800246bd88[5]` → `0xffffff800241dbc0`) | `IO80211SkywalkInterface::setInterfaceRole` (`0xffffff80022f60f0`), called from `IO80211VirtualInterface::init` at `0xffffff800233064b` | `setPropertyHelper` at `0xffffff80022f6139` | During the `init`, **before** the `registerService` |
| `IOMACAddress` | 6 B with the MAC of payload+4 (copied to RegInfo+0x108 via slot +0xCF8 at `0xffffff800165dde6`) | `IOSkywalkEthernetInterface::copyRegistrationInfo` (`0xffffff8002b109f0`) | `setProperty` via slot +0x288 at `0xffffff8002b10a66` | Inside `registerEthernetInterface`, before the `registerService` |
| `IOInterfaceNamePrefix` | `"awdl"` | `IOSkywalkNetworkInterface::registerNetworkInterface` (`0xffffff8002b0a204`) | slot +0x270 at `0xffffff8002b0a2e7` | Same |
| `IOInterfaceUnit` | `0` (OSNumber 32 b, the unit from RegInfo+0x38) | same | slot +0x280 at `0xffffff8002b0a310` (only if unit ≥ 0) | Same |
| `"BSD Name"` | `"awdl0"` | `IOSkywalkNetworkBSDClient::start` (`0xffffff8002aff562`) | slot +0x270 at `0xffffff8002aff84d` | Inside the `registerService(2)` of the interface, **before** creating the ifnet |
| `IOInterfaceName` | `"awdl0"` (1st: composed by `snprintf("%s%u")`; 2nd: read from the live ifnet) | 1st same (`0xffffff8002aff86b`); 2nd `IO80211SkywalkInterface::prepareBSDInterface` (`0xffffff80022f63b6`) | 2nd at `0xffffff80022f63eb` | Inside the `registerService(2)`; the 2nd during nexus preparation, before `ifnet_attach` |
| `IOInterfaceType` | `6` (`IFT_ETHER`, taken from `eparams+48` filled by `initBSDInterfaceParameters`) | `IOSkywalkNetworkBSDClient::start` | setProperty slot +0x280 in the final stretch (after `kern_nexus_controller_alloc_net_provider_instance`) | At the end of the BSD client start |
| `IOPMResetPowerStateOnWake` | present only if the controller has it (`checkPropertyTrue`) | `IO80211SkywalkInterface::start` (`0xffffff80022f5792`) | `setPropertyHelper` in the PM block | During the base `start` |
| `IOSkywalkNexusUUID` | UUID of the nexus (parsed with `uuid_unparse_upper`) | `IOSkywalkNetworkBSDClient::start` | setProperty via own slot +0x270 | End of the BSD client start |

On the **parent** (the entry of `AppleBCMWLANCore`): no function of the chain
setVIRTUAL_IF_CREATE → createProxInterface → init/attach/start → BSD client writes properties on
it; the interface hangs as a child with `IOService::attach` (slot +0x670 at
`0xffffff800165dd92`) and the parent keeps the properties it already published in its own start
(`?` exhaustive discard not done, but in the ~30 decompiled functions of the chain there is no
`setProperty` with a destination other than the interface itself). There is also no write of
**`IOProviderClass`** in the chain: in the kernel that literal only appears in generic paths
(`OSKext::uniquePersonalityProperties` `0xffffff8000a34cc0`, `IOService::initialize`
`0xffffff8000aa45d0`, IONetworkingFamily, etc.), so the matching of the
`IOSkywalkNetworkBSDClient` with its provider comes from the kext personality, not from a property
set at birth time (`?`). The dictionary of `wifip2pd` (`IOPropertyExistsMatch` over
`IOInterfaceName`, see [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.1) needs
nothing more.

## 5. Exact order `registerService` ↔ BSD attach

Full sequence of role 6, with the point of the notification:

1. `setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`) → `createProxInterface` (`0xffffff8001678bdd`).
2. `withOptions` → alloc + `init` (slots +0x11C8 → +0x88 → `IO80211VirtualInterface::init` →
   `IO80211SkywalkInterface::init` → `IOSkywalkEthernetInterface::init`) — here it already
   publishes `IO80211VirtualInterfaceRole = AirLink` (the entry **is not registered yet**, so no
   matching fires).
3. `attach` (slot +0x670, `0xffffff800165dd92`) — the entry is attached to the core, still unregistered.
4. `Proxi::start` (`0xffffff800165dead`) → base start (peer manager, PM, MAC agent) → queue
   registration → `registerEthernetInterface` (`0xffffff800172973d`): publishes `IOMACAddress`,
   `IOInterfaceNamePrefix`, `IOInterfaceUnit` and prepares the media state (§6) — still no
   registration, no notifications.
5. **`registerService(2)`** (synchronous) at `0xffffff800172982a`. Inside it, synchronously:
   1. IOKit matches the driver `IOSkywalkNetworkBSDClient` against the new entry → its `start`
      (`0xffffff8002aff562`).
   2. Publishes `"BSD Name"` (`0xffffff8002aff84d`) and **`IOInterfaceName`**
      (`0xffffff8002aff86b`) —
      **this is the moment the registered entry gains `IOInterfaceName` and the
      `IOPropertyExistsMatch` matching of `wifip2pd` fires `IOServiceMatched`** (delivery to the
      notification port of the daemon is asynchronous).
   3. `initBSDInterfaceParameters` → `kern_nexus_controller_alloc_net_provider_instance`
      (`0xffffff80009a4140`): `ifnet_allocate_extended` (`0xffffff80009a420d`) →
      `prepareNexusCallback` → `gatedPrepareNexus` → `prepareBSDInterface` (2nd write of
      `IOInterfaceName` at `0xffffff80022f63eb`, now with the real ifnet) → `bpfattach` →
      **`ifnet_attach`** (`0xffffff80009a42df`).
   4. `IOInterfaceType`, `IOSkywalkNexusUUID`, PM, `registerService(0)` of the BSD client.
6. Back in `Proxi::start`: `setRunningState(false)` (`0xffffff8001729838`) and return 0.
7. `setVIRTUAL_IF_CREATE`: `getBSDName` (`0xffffff8001678a1b` — already returned from the cache
   `ivars+1` filled by `getBSDName` or composed from the ifnet) → `strlcpy(payload+16, ..., 0x10)`
   (`0xffffff8001678a2b`) → return 0.

Consequence: when `RawSet` type 94 returns 0 to `wifip2pd`, the ifnet `awdl0` already exists, has
a BSD index and the `IOServiceMatched` notification is on its way (or delivered); the 30 s wait of
the daemon releases without any additional apple80211 request (consistent with
[`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1). If the interface already
existed, the result is `0x11` (17) but the name is written into payload+16 anyway (same shared
block, `0xffffff8001678e61 → 0xffffff80016789ff`).

## 6. `SIOCGIFMEDIA` on `awdl0`

### 6.1 What `wifip2pd` requires

`SIOCGIFMEDIA` = `_IOWR('i', 56, struct ifmediareq)` with `sizeof(struct ifmediareq)` = 44
(`#pragma pack(4)`: 16 of name + 5 ints + pointer) = **`0xC02C6938`**; the 40 B legacy variant
(OSIOCGIFMEDIA) is **`0xC0286938`**. The daemon filter (discoverer
`sub_100043720` of `wifip2pd.x86_64`): after the ioctl it checks the dword of the buffer at offset
+0x10 —
`movl -0x140(%rbp),%eax; andl $0xE0,%eax; cmpl $0x80,%eax` at **`0x100043ccc`** with the buffer of
the `ifmediareq` at `-0x150(%rbp)` — that is **`ifm_current`** (offset 16 of the struct):
`(ifm_current & 0xE0) == 0x80` = type `IFM_IEEE80211` (`IFM_NMASK 0xE0`, `IFM_IEEE80211 0x80`).

### 6.2 Full kernel path

```
ioctl(SIOCGIFMEDIA) on the BSD socket
  └─ xnu _ifioctl                                  0xffffff80005d62d0
       (cmd comparisons at 0xffffff80005d67be / 0xffffff80005d6bde)
       └─ sub_FFFFFF80005FA970  (SIOCGIFMEDIA/OSIOCGIFMEDIA: cases 0x5fab00-0x5fab08)
            builds a kernel ifmediareq and calls
            _ifnet_ioctl(ifp, 0, 0xC02C6938, &ifmr)   call at 0xffffff80005faf4e → 0xffffff80005e0c30
                 └─ if_ioctl of the ifnet = IOSkywalkNetworkBSDClient::if_ioctl   0xffffff8002b002e6
                      (_softc[18] = gate, __softc[33] = action; call [vtable+456])
                      └─ IOSkywalkNetworkBSDClient::ioctlHandler (gated)        0xffffff8002affce6
                           └─ interface->processBSDCommand(ifp, cmd, data)  (slot +0x910, call at 0xffffff8002aff9xx)
                                └─ IOSkywalkNetworkInterface::processBSDCommand  0xffffff8002b0ad4a
                                     (compares 0xC02C6938 at 0xffffff8002b0adba and 0xC0286938 at 0xffffff8002b0adca)
                                     └─ IOSkywalkNetworkInterface::ioctl_gifmedia    0xffffff8002b0ae78
```

`ioctl_gifmedia` (`0xffffff8002b0ae78`, fully decompiled):

- Media list: count = `copied RegInfo+0x14` (2 for AWDL; the field was set by
  `createProxInterface` at `0xffffff800165ddcc`); the words are produced by
  `getSupportedMediaArray` (slot +0x948; override
  `IO80211SkywalkInterface::getSupportedMediaArray` `0xffffff80022f8e5e`, which iterates the
  `IONetworkMedium` media table of the controller) and copied to user with `copyout`.
- Copies the state under the IOLock of `state+16` (the fields written in §3.2 step 5):
  **`ifm_active` (offset 28) ← `state+48`**, **`ifm_current` (offset 16) ← `state+52`**,
  **`ifm_status` (offset 24) ← `state+56`**.
- The user write of `ifm_current` (`SIOCSIFMEDIA` = `0xC0206937`, dispatched at
  `0xffffff8002b0ad8c` to `ioctl_sifmedia` `0xffffff8002b0affa`) writes `state+52`.
- Slot +0x940 `handleChosenMedia` (`IO80211SkywalkInterface::handleChosenMedia`
  `0xffffff80022f8f5c`, **not overridden by Broadcom** — vptr+0x940 of Proxi =
  `0xffffff80022f8f5c`) translates a media word to `selectMedium` of the controller (finds the
  `IONetworkMedium` in the controller table and calls its slot +0x8F0); its only references are
  vtable slots, so the caller is a vcall not identified in this pass (`?`).

### 6.3 The open gap and the consequence for the kext

With the static seeds, `awdl0` is born with `ifm_active = 0x80` (type 802.11 ✓) but
`ifm_current = 8` and `ifm_status = 0`; `8 & 0xE0 = 0`, so **the filter of `wifip2pd` would not
pass with the seeds as-is**. On a real system the check passes, so the `ifm_current` of `awdl0`
must have been updated to a word with type `IFM_IEEE80211` (e.g. `0x80` =
`IFM_IEEE80211|IFM_AUTO`, since `IFM_AUTO = 0` per `net/if_media.h`) before the daemon read; the
natural candidate is the path
`handleChosenMedia` → `selectMedium` of the Broadcom controller during link bringup
(`bringupLink`), or an `SIOCSIFMEDIA` from the stack itself, but the concrete instruction that
rewrites `state+52` during the birth was not identified (`?`). For `RTL8814AirLayer` the
consequence does not change: the project's own if_ioctl of `awdl0` must answer `SIOCGIFMEDIA`
(`0xC02C6938`, and the variant `0xC0286938`) with `ifm_current` and `ifm_active` of type
`IFM_IEEE80211` (`& 0xE0 == 0x80`; e.g. `0x80`), `ifm_status` with at least `IFM_AVALID` when the
link is up, and a list of 802.11 words (the daemon only looks at `ifm_current` for the gate; the
remaining fields are used by the rest of the tools).

## 7. Operational summary for `RTL8814AirLayer`

1. `setVIRTUAL_IF_CREATE` (slot 556/+0x1160) with role 6: create the interface object
   (Proxi equivalent), `attach` to the controller, and in the project's own `start`:
   `IO80211VirtualInterfaceRole = "AirLink"` (or publish directly if the family init is not used),
   `IOMACAddress` with the MAC of payload+4, `IOInterfaceNamePrefix = "awdl"`,
   `IOInterfaceUnit = 0`.
2. Create the ifnet with prefix `awdl`, unit 0, `IFT_ETHER`, MAC from the payload, and do
   `ifnet_attach`; compose and cache the name `awdl0`.
3. Publish `IOInterfaceName = "awdl0"` and **then** call `registerService` (synchronous if the
   BSD attach must be complete before returning control to the handler, as Broadcom does with
   `registerService(2)` at `0xffffff800172982a`; the Broadcom order is: properties →
   registerService → [matching → BSD name + ifnet attach inside] — in a custom kext without the BSD
   client driver, the minimal correct order is: ifnet attach + `IOInterfaceName` +
   `IOInterfaceUnit` + role **before** `registerService`, which is what fires the notification).
4. Write the name at payload+16 with `strlcpy(..., 16)` and return 0 (or 17 if it already
   existed, with the name written anyway).
5. Answer `SIOCGIFMEDIA`/`OSIOCGIFMEDIA` with type `IFM_IEEE80211` in `ifm_current` (§6.3).

## 8. Method and limits

- Headless decompilation (Hex-Rays) of ~50 functions and disassembly of the stretches with the
  exact call sites (scripts and outputs in `Build/ida_awdl/task_awdl_birth/{bcm,fam}/`:
  `bcm_birth.py` → `out.txt`; `fam_birth.py` → `out.txt`; `fam_birth2.py` → `out2.txt`;
  `fam_birth3.py` → `out3.txt`; `fam_birth4.py` → `out4.txt`). Function addresses cross-checked
  with `Docs/re/io80211_25.6.0/syms.txt`, `syms_bcm.txt` and
  `syms_com.apple.iokit.IOSkywalkFamily.txt`.
- Ioctl constants verified by immediate search in the kernel (`SIOCGIFMEDIA`
  `0xC02C6938`: 22 hits, including `_ifioctl`, `IOSkywalkNetworkInterface::processBSDCommand` and
  `ioctl_gifmedia`; `SIOCSIFMEDIA` `0xC0206937`: hit at `0xffffff8002b0ad8c`) and by the real
  filter of `wifip2pd` disassembled with `objdump` (`0x100043ccc`).
- Explicit `?`: the updater of `ifm_current` (`state+52`) between the birth and the read of
  `wifip2pd` (§6.3); the xnu internal mapping of `ifnet_init_eparams` (prefix/unit fields observed
  at `eparams+32/+40` written by `IOSkywalkNetworkBSDClient::start`, not followed inside
  `ifnet_allocate_extended`); the caller of the vcall to `handleChosenMedia` (+0x940); the use of
  the name parameter (`"awdl"`) in the init chain; the exhaustive discard of property writes on
  the parent; the `IOProviderClass` (not written at runtime in the chain; kext personality `?`);
  the exact address of the `setProperty("IOInterfaceType")` and of the final `registerService(0)`
  of the BSD client (function decompiled, call sites not individually disassembled); the
  `slot +0x910` call inside `ioctlHandler` cited from the decompiled output (exact call site not
  disassembled).
- The addresses of the vtable slots (e.g. +0x5B0 `registerService`, +0x670 `attach`,
  +0x8E8 `prepareBSDInterface`, +0x8F8 `getBSDInterface`, +0x938 `setRunningState`, +0x940
  `handleChosenMedia`, +0x948 `getSupportedMediaArray`, +0xCF8 getMACAddress) come from the
  disassemblies (`call qword ptr [rax+off]`) and from the dump `vt_IO80211SkywalkInterface.txt` of
  the project; slot +0x5B0 contains the address of `IOService::registerService`
  (`0xffffff8000a93250`) in all the vtables of the family (data xrefs in `fam/out.txt`
  §registerService).
