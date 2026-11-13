# RegInfo +0x10/+0x40 on the Broadcom side

Closure of the Broadcom side of the `+0x10` and `+0x40` fields of `IOSkywalkEthernetInterface::RegistrationInfo`
(0x130 bytes) that the family copies to `*(this+0x118)` via `copyRegistrationInfo`
(`0xffffff8002b109f0`). Complements `AWDL_STAGE1_REGINFO_AND_GATES.md` (family side) and
`AWDL_INTERFACE_BIRTH.md` §2.2 (role 6 path). Read against the macOS 26.7.1
(25G241, Darwin 25.6.0, x86_64) kernelcache. Every cited VA is a function head or an instruction seen in the
disassembly of this session; the `?`s mark what the disassembly does not fix.

## 1. Correction: `*(this+0x118)` is a container, not the RegInfo copy

`AWDL_STAGE1_REGINFO_AND_GATES.md` §2 describes `*(this+0x118)` as the RegInfo copy. The
full disassembly of `copyRegistrationInfo` corrects the detail: ivar `+0x118` points to a
**container** allocated separately (freed with `kalloc_type_view_90` in `free`), whose first qword is
the pointer to the 0x130-byte copy (allocated with `kalloc_type_view_142`):

| VA | Function | Instruction | Fact |
|---|---|---|---|
| `0xffffff8002b10a1a` | `copyRegistrationInfo` (`0xffffff8002b109f0`) | `lea rdi, ...kalloc_type_view_142` → `call _IOMallocTypeImpl` | allocates the copy (0x130) |
| `0xffffff8002b10a26`–`0xffffff8002b10a2d` | same | `mov rcx, [rbx+118h]` → `mov [rcx], rax` | saves the copy at `*(holder)` |
| `0xffffff8002b10a40` | same | `call _memmove` | copies 0x130 bytes |
| `0xffffff8002b10868` | `IOSkywalkEthernetInterface::free` (`0xffffff8002b1085e`) | `mov r14, [rdi+118h]` | takes the holder |
| `0xffffff8002b10896` | same | `mov rdi, [r14+10h]` → `call [rax+28h]` | frees the object at `holder+0x10` (not the RegInfo) |
| `0xffffff8002b111f6`, `0xffffff8002b112c1` | `ioctl_multicast` (`0xffffff8002b1116a`) | `mov [rax+18h], 0` / `mov [r12+10h], r15` | `holder+0x18` = multicast counter, `holder+0x10` = `OSData*` with the list |

Consequence: the accesses to `holder+0x10` and `holder+0x18` of `free` and `ioctl_multicast` that the previous
doc left as doubtful RegInfo readers are the container's own fields (a multicast `OSData*` and
its count). Neither touches the `+0x10`/`+0x40` fields of the RegInfo copy.

## 2. What the family fixes

`initRegistrationInfo` (`0xffffff8002b108e2`) does `bzero(RegInfo+8, 0x128)`
(`0xffffff8002b1091a`–`0xffffff8002b10923`) and writes: qword `0x13000000001` at `+0`
(`0xffffff8002b10928`–`0xffffff8002b10932`), `+0x8` through slot `+0x9B0` of `IOSkywalkNetworkInterface`'s
vtable with default 2 (`0xffffff8002b10958`), `+0xC` through slot `+0x9B8`
(`0xffffff8002b10966`), `+0x14 = 0x100` (`0xffffff8002b10935`), `+0x24 = 0x22`
(`0xffffff8002b10969`), `+0x28` through slot `+0x9C0` with default 0x20 (`0xffffff8002b10985`),
`+0x30` through slot `+0x9F8` with default `"en"` (`0xffffff8002b109a1`), `+0x38` from
`getBSDUnitNumber` (`0xffffff8002b109ad`), `+0x44` through slot `+0x998` with default `0x2E`
(`0xffffff8002b109c5`) and `+0x48`/`+0x4C` through slot `+0x988` with default `0x5DC`
(`0xffffff8002b109dd`–`0xffffff8002b109e0`). In that body neither `+0x10` nor `+0x40` is written: the
`bzero` leaves them at zero. `copyRegistrationInfo` copies the 0x130 unaltered and only derives
`IOMACAddress` from `copy+0x108` (slot `+0x288`, `0xffffff8002b10a66`).

## 3. Broadcom writers of the RegInfo: `+0x10`

All xrefs to `initRegistrationInfo` whose caller is Broadcom build the RegInfo on the stack with
the same pattern (`bzero` of 0x130 + `initRegistrationInfo`). Full inventory of the
Broadcom constructors (xrefs verified this session):

| Constructor | Head | `initRegistrationInfo` site | Writes `+0x10` |
|---|---|---|---|
| `AppleBCMWLANCore::createNANMgmtDataInterface` | `0xffffff800165d4dc` | `0xffffff800165d675` | no |
| `AppleBCMWLANCore::createNANDataInterface` | `0xffffff800165d878` | `0xffffff800165da11` | no |
| `AppleBCMWLANCore::createProxInterface` (role 6) | `0xffffff800165dcbe` | `0xffffff800165ddc7` | no |
| `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (role 1, `"ap"`) | `0xffffff80016788f4` | `0xffffff8001678cc0` | no |
| `AppleBCMWLANCore::createSkywalkInterface` | `0xffffff80016aa874` | `0xffffff80016aaabc` | **role 1 only** |
| `AppleEthernetRL::startInterface` | `0xffffff800293fe60` | `0xffffff800293feb3` | `?` (outside the AWDL target, not inspected) |
| `IOUserNetworkEthernet::updateRegistrationInfo` | `0xffffff8002b15b40` | `0xffffff8002b15b5a` | `+0x10 = 1` (`0xffffff8002b15c15`) |

Only AWDL-relevant writer: in `createSkywalkInterface`, role 1 writes `RegInfo+0x10 = 1`
(`mov [rbp+var_150], 1` at `0xffffff80016aab8d`; the RegInfo base is `var_160`, `lea r15,
[rbp+var_160]` at `0xffffff80016aaa98`, so `var_150` = `RegInfo+0x10`). Role 2
(`AppleBCMWLANLowLatencyInterface`) does not write it. The role 6 path (`createProxInterface`) and the
94 path for the primary (`setVIRTUAL_IF_CREATE`) leave it at zero.

The four Broadcom `start`s receiving the RegInfo as third argument and passing it to
`registerEthernetInterface` do not modify it:

| `start` | Head | RegInfo received at | Writes over the RegInfo |
|---|---|---|---|
| `AppleBCMWLANIO80211APSTAInterface::start` | `0xffffff8001702b8e` | `rdx` → `r15` → `var_D8`, passed intact (`mov rsi, [rbp+var_D8]` at `0xffffff8001702fbf`) | none |
| `AppleBCMWLANProximityInterface::start` | `0xffffff8001729376` | `rdx` → `r15` (`0xffffff800172938a`) | only `+0x48 = 0x59400000594` (`0xffffff80017296ff`–`0xffffff8001729709`, if RegInfo non-null) |
| `AppleBCMWLANNANInterface::start` | `0xffffff800172b120` | `rdx` → `r15` (`0xffffff800172b134`) | none |
| `AppleBCMWLANNANDataInterface::start` | `0xffffff8001745480` | `rdx` → `r15` (`0xffffff8001745494`) | none |

Reading warning: in those four `start`s, `[r12+10h]`/`[r12+40h]` stores abound, but the
base `r12` are stack descriptors initialized with `0xAAAA...` (`lea r12, [rbp+var_C0]` at
`0xffffff8001702c83`, `0xffffff800172b26d`, `0xffffff80017455d1`, `0xffffff80017294b6`): the
bus's registration descriptor and the queue container, not the RegInfo.

## 4. Broadcom writers of the RegInfo: `+0x40`

The six inspected Broadcom constructors write a power bitmap. Seed value,
composition and result:

| Constructor | Seed | Composition | Result |
|---|---|---|---|
| `createProxInterface` (role 6) | 0 (`mov [r12+40h], r14d`, `0xffffff800165de48`) | `checkPropertyTrue(core, "IOPMResetPowerStateOnWake")` (`0xffffff800165de6b`): false → `|4`, true → 6 (`mov eax,6` / `cmovz eax,ecx`, `0xffffff800165de7a`–`0xffffff800165de7f`; store `0xffffff800165de94`) | **6 or 4** |
| `setVIRTUAL_IF_CREATE` (role 1, 94) | 0 (`0xffffff8001678d45`) | same property (`0xffffff8001678d6e`), same pattern (`0xffffff8001678d7c`–`0xffffff8001678d81`); store `0xffffff8001678d97` | **6 or 4** |
| `createNANMgmtDataInterface` | 4 (`0xffffff800165d701`) | property true → `or var_190, 2` (`0xffffff800165d733`) | 4 or 6 |
| `createNANDataInterface` | 4 (`0xffffff800165da9d`) | same (`0xffffff800165dacc`) | 4 or 6 |
| `createSkywalkInterface` | 0 (`0xffffff80016aaaf7`) | property → `+2` (`0xffffff80016aab26`); boot-arg `wlan.skywalk.legacy-power-off-mode` → `+4` (`shl ecx,2` at `0xffffff80016aab56`); role 2 → `+4` more (`0xffffff80016aac1f`–`0xffffff80016aac22`) | 0–10 |
| `IOUserNetworkEthernet::updateRegistrationInfo` | — | fixed value 8 at `+0x40` and 1 at `+0x10` (`0xffffff8002b15bb2`, `0xffffff8002b15c15`) | 8 |

Role 6 vs primary difference: **none at `+0x40`**. Both the role 6
path (`createProxInterface`) and the primary's via the 94 (`setVIRTUAL_IF_CREATE`) write 6 if
`IOPMResetPowerStateOnWake` is true on the provider and 4 if not. `createSkywalkInterface` (roles 1
and 2, role 6 does not use it) adds the boot-arg's and role's bits. `?` The semantic meaning of bits
1 and 2 (power, per the property's and boot-arg's names) is inferred from those names; the
real effect does not appear in the swept range (§5).

## 5. Sweep of readers of `RegInfo+0x10` and `+0x40`

Pattern: load `mov reg, [x+118h]` followed within 12 instructions by an access `[reg+10h]` or
`[reg+40h]`, over 26,207 functions in three ranges: Broadcom
(`0xffffff800153f600`–`0xffffff8001593e90` and `0xffffff80015b9600`–`0xffffff8001766fb0`),
`IO80211*` (`0xffffff8002200000`–`0xffffff8002480000`) and `IOSkywalk*`
(`0xffffff8002a00000`–`0xffffff8002c00000`). Result: 36 matches, all false positives:

- `AppleBCMWLANConfigManager::copyWlanPwrDutyCycleTable` (`0xffffff80016f060e`) and six functions of
  `AppleBCMWLANBonjourOffload` (`0xffffff80017115a6`, `0xffffff8001711a6a`, `0xffffff80017130ba`,
  `0xffffff8001713744`, `0xffffff80017145e6`): they have their own ivar at `+0x118` (other objects);
  the `+0x10` accesses are own fields or locks.
- `IO80211LQMData::createDeltaLQMData` (`0xffffff800233cb4e`),
  `IO80211PeerMonitor::incrementTxExtStats` (`0xffffff800237d798`): own ivars.
- `IOSCSITargetDevice` (`0xffffff8002a54c92`, `0xffffff8002a55610`, `0xffffff8002a557b2`): locks.
- `IOSkywalkEthernetInterface::free` (`0xffffff8002b1085e`) and `ioctl_multicast`
  (`0xffffff8002b1116a`): container fields of §1, not the RegInfo.
- `IOSkywalkEthernetInterface::initBSDInterfaceParameters` (`0xffffff8002b10bca`) and
  `prepareBSDInterface` (`0xffffff8002b10e58`): the pattern's `+0x10` store goes on another register
  base, not on the holder; `?` exact base not traced instruction by instruction.
- `IOUserNetworkRxSubmissionQueue::replenishQueues` (`0xffffff8002b1a30c`) and four functions of
  `IOSurfaceRootUserClient` (`0xffffff8002b9b934`, `0xffffff8002b9c01a`, `0xffffff8002b9c10c`,
  `0xffffff8002b9c2f8`, `0xffffff8002b9c5fa`): unrelated classes with its own ivar at `+0x118`.

No access of the sweep reaches `copy+0x10` nor `copy+0x40`. The copy's consumer is
`IOSkywalkNetworkInterface::registerNetworkInterface` (`0xffffff8002b0a0d6`), which receives it from the
holder (`mov rsi, [rax]` at `0xffffff8002b10ac5`, via `registerEthernetInterface`
`0xffffff8002b10a84`) and only reads the documented offsets (`+0x24`, `+0x28`, `+0x30`, `+0x38`,
`+0x4C`, `+0x108`; `AWDL_INTERFACE_BIRTH.md` §2.5). `?` This session's sweep covers
simple dereference (`holder+0x10/+0x40`); the double dereference (`*(holder)+0x10/+0x40`) was not
repeated here and is covered by the decompilation of the four registration functions in
`AWDL_STAGE1_REGINFO_AND_GATES.md` §2, which found no reads.

## 6. Conclusion

**A driver leaving `RegInfo+0x10` and `RegInfo+0x40` at zero changes nothing observable: no.**

- `+0x10` has no writer on the role 6 path nor on the primary's 94 path
  (`setVIRTUAL_IF_CREATE`); the only AWDL-relevant writer is `createSkywalkInterface` for role
  1, and no reader appears in `IO80211Family`, `IOSkywalkFamily` nor `AppleBCMWLAN*` (§5). The copy
  travels intact to both storage locations (holder of `IOSkywalkEthernetInterface` and `ivars+0xC0` of
  `IOSkywalkNetworkInterface`) and dies there.
- `+0x40` is written (6/4 on role 6 and primary; power bitmap on the rest), but no
  reader appears in the sweep of §5 nor in the copy's consumers. Broadcom's values 4/6
  do not alter any verified route of the birth, the BSD registration, the IORegistry registration nor the
  apple80211 dispatch of `awdl0`.
- For stage 1: the `?` values of `AWDL_STAGE1_IMPL_SPEC.md` §15.7–8 are resolved as
  **0 in both fields**, so the driver's RegInfo replicates the observable behavior of the Broadcom
  path (which also boots with both at zero due to `initRegistrationInfo`'s `bzero`).

## 7. Searches executed

| Point | Search | Result |
|---|---|---|
| constructors | xrefs to `initRegistrationInfo` and to both `registerEthernetInterface`s | 7 Broadcom callers + 2 family ones (§3) |
| writers `+0x10` | full disassembly of the 6 constructors and 4 `start`s | only `createSkywalkInterface` role 1 and `IOUserNetworkEthernet` (§3) |
| writers `+0x40` | same | bitmap in the 6 constructors; no `start` (§4) |
| readers | sweep window 12 after `mov reg,[x+118h]`, 26,207 functions, 3 ranges | 36 hits, all from other objects (§5) |
| container `+0x118` | disassembly of `copyRegistrationInfo`, `free`, `ioctl_multicast`, `deregisterEthernetInterface` | holder with copy at `+0x00`, `OSData*` at `+0x10`, counter at `+0x18` (§1) |
