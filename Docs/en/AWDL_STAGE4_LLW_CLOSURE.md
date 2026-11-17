# Stage 4 (llw0): closure of the three open points

Closes the `?`s of `AWDL_ETAPAS_2_A_4.md` §3 and of `AWDL_LLW_COMPANION.md`: who uses the
ioctls 130/131, what the kernel does upon receiving them, who triggers the destruction of the
companion and which vtable cells and family calls a minimal `RTLLlwInterface` class
requires. Base: `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`) and the kernelcache
26.7.1 (md5 `f489ff8be80e6013a1bf47561dcaef45`), headless disassembly over copies of the
databases; the originals were not opened. The addresses are VAs of the indicated binary and
the head of each cited function was checked against the database; what cannot be
decided without execution goes as `?`.

## 1. Result

| Point | Result |
|---|---|
| **1. Ioctls 130/131** | They are not apple80211 requests. `wifip2pd` issues them over an `AF_INET6`/`SOCK_DGRAM` socket with `0xC0806982` (`_IOWR('i',130,128)`) and `0xC1206983` (`_IOWR('i',131,288)`). The 130 is consumed by the IPv6 stack in `in6_control` (0xffffff80007dd4b0), without a direct call to `processBSDCommand` (the indirect calls of the attachment were not resolved, `?`). The 131 does not fit the switch of `in6_control`: it falls to `ifnet_ioctl` (0xffffff80007de187) and, if the bridge does not intercept it (`?`), it reaches the `processBSDCommand` of the interface, where base `IO80211SkywalkInterface::processBSDCommand` (0xffffff80022f6e98) rejects it with **45 (ENOTSUP)** and the log `Rejected w/ENOTSUP`; the LL class does not reject it itself. The daemon sees `errno = 45` on the first ioctl (131) and no longer sends the 130. It does not break AWDL: the 293 does not depend on this pair and there is no teardown of `awdl0`. |
| **2. Destruction (op = 0)** | `wifip2pd` never issues `op = 0`: the only `Apple80211RawSet` of type 293 (0x10004b3e6, in `sub_10004B300`) builds the payload with `op = 1`; there is no `RawSet` 95 nor another construction of the 293. The destruction branch of the kernel exists and was walked: `AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE` (0xffffff8001722ef2) → `AppleBCMWLANCore::destroySkywalkInterface` (0xffffff80016aae4e) → `IOService::terminate(2)` (slot +0x600, 0xffffff8000a937b0) → `AppleBCMWLANLowLatencyInterface::free` (slot +0x90, 0xffffff80015e0046) → core table slot to zero. It is only reachable from an external client, not from this daemon. |
| **3. Minimal class** | It is a second object of the `IO80211VirtualInterface` chain, created with the same sequence as `awdl0` (init with role 2 and name "llw", `setInterfaceId`, `attach`, `start`, `initRegistrationInfo`, `registerEthernetInterface`, `registerService`), with `getBSDNamePrefix` → "llw" and the BSD name copied to payload+8. `awdl0` does **not** need a reference to the companion for the 293 to return 0; the two links of the family are null-safe or write-only. **Corrected in §6:** the sequence lacks `setParentInterface(awdl0)` before `start`; without it, `start` fails for role 2. |

## 2. Point 1 — the ioctls 130/131

### 2.1 Daemon side: who emits them

| Element | Address | Evidence |
|---|---|---|
| `sub_100058420` | 0x100058420 | Creates `socket(30, 2, 0)` (`sub_1001CFA90`, site 0x10005846e): family 30 = `AF_INET6`, type 2 = `SOCK_DGRAM`. Fills two requests shaped like `in6_aliasreq`: the first (288 B) only with the name at +0 (copied with `strlcpy(..., 0x10)` from the `String` of `self+0x10`); the second (128 B) adds `sin6_len = 28`/`sin6_family = 30` at +0x10 and the 16 B address at +0x18. |
| `sub_1000586D0` | 0x1000586d0 | Twin of the previous one: same two ioctls and same assembly. No absolute references in the binary; it may be in a relative method list (`?`). |
| `sub_10005F7B0` | 0x10005f7b0 | Thunk: `return sub_100058420(a1)`. |
| `sub_100068EE0` | 0x100068ee0 | Thunk of `sub_10005F7B0`; its only references are data at 0x100517bd8 and 0x10052cb90 (tables of function pointers). The exact Swift name of the protocol and of the method was not recovered (`?`). |
| Request table | 0x100493008 | Index 5 → `0xC0806982`; index 6 → `0xC1206983`. `sub_1004443B4` (0x1004443b4) only indexes the table. |
| Ioctl 131 | 0x100058509 | `sub_100058420` calls `ioctl(fd, 0xC1206983, &req)` with the 288 B `in6_aliasreq`. |
| Ioctl 130 | 0x1000585f8 | Second call of `sub_100058420`, with the 128 B buffer. In `sub_1000586D0` the pair is at 0x1000587e6 (131) and 0x1000588ee (130). |

The sequence is **131 and then 130**, not the other way around. The destination is the name carried by the
`Socket` object at `self+0x10`; the helper serves any interface and contains no
`llw0` literal, so the claim "over `llw0`" depends on who builds that `Socket`:
the initializer passing it the companion name was not located (`?`). The
`llw0` literals of the binary are in the creation validation (`if_nametoindex`, 0x10004b814), not
in this helper. What does remain closed is that it does not use the apple80211 channel: there is no
`Apple80211RawSet`/`RawGet` in the function, only BSD `ioctl` over an IPv6 socket.

### 2.2 Kernel side: what it does upon receiving them

| Step | Address | Evidence |
|---|---|---|
| `in6_control` | 0xffffff80007dd4b0 | Only 130: `mov ecx, 0C0806982h` at 0xffffff80007ddcee, `cmp r15, rcx` and call to the helper at 0xffffff80007ddd19. The helper compares 0xC0786982 and 0xC0806982 (two sizes of the same number 130), not the 131. |
| Case helper | 0xffffff80007dec30 | Compares `a2` against 3229116802 (`0xC0786982`, size 120) and 3229641090 (`0xC0806982`, size 128); any other value triggers the assert of `in6.c:745`. Copies 128 B in both cases, checks `sin6_family` (byte +0x11) and calls `in6_ifattach_aliasreq` at 0xffffff80007ded41. |
| `in6_ifattach_aliasreq` | 0xffffff80007e7c00 | `in6_ifattach_prelim` (0xffffff80007e77b0), which requires `if_type == 57` or bit 15 of `if_flags` (`*(int16*)(ifp+190) < 0`; the name of that bit was not determined, `?`) and assembles `ND_IFINFO`/`nd6_ifattach`/`scope6_ifattach`; afterwards it generates the `fe80::` link-local and adds it with `sub_FFFFFF80007E7FD0` (0xffffff80007e7fd0), which obtains the IID from `in6_iid_from_hw`. |
| 131 | 0xffffff80007de172 | `in6_control` does not recognize it and runs the default case: `ifnet_ioctl(ifp, 30, cmd, arg)` at 0xffffff80007de187. For a Skywalk interface, the `if_ioctl` ends in `IO80211SkywalkInterface::processBSDCommand` (0xffffff80022f6e98; the exact `IOSkywalkNetworkBSDClient` bridge was not followed, `?`), which only serves four apple80211 constants and returns `v38 = 45` (`ENOTSUP`) for the rest, with `IOLog("Rejected w/ENOTSUP: ...")` at line 1072. `AppleBCMWLANLowLatencyInterface::processBSDCommand` does not reject 131 (its four constants are others), but it delegates into `IO80211InfraInterface::processBSDCommand` (0xffffff8002360290), which in turn calls this base, and the result is 45. |
| Return | — | 130: `0` if the interface exists and the attachment finishes; `ENXIO` if the name does not resolve. 131: `45` before leaving the `ifnet_ioctl`, so the first ioctl of the pair fails and the second is not sent. The daemon converts the `errno` into a Swift error (checks `v14 != -1`/`v14 != 0` in `sub_100058420`, sites 0x10005850e and 0x1000585fd). |

The pair is not symmetric: the 130 resolves in the IPv6 stack (except for the
indirect calls of `sub_FFFFFF80007E7FD0`, which were not resolved, `?`) and the 131 goes down through
`ifnet_ioctl` up to the `processBSDCommand` of the interface, where the base rejects it with 45.
The LL class is not the one rejecting: its override returns 22 only for the four apple80211
constants (§5) and delegates the 131 into the base, which is the one answering `ENOTSUP`.

### 2.3 Effect on AWDL

- These ioctls are not on the path of the 293 (`sub_10004B300` does not call them) and no observed
  path destroys `awdl0`; the failure of the 131 (45) only leaves without the IPv6 address the
  helper intended to configure.
- The order 131 → 130 makes the error of the 131 prevent the attachment of the 130: the pair does not get
  to complete on an interface whose `processBSDCommand` does not implement the 131. For
  `awdl0`/`llw0` to complete the IPv6 configuration, that command would need to be served in the corresponding
  class; for the 293 it is not necessary.
- The creation of the companion remains optional: if the 293 fails or does not exist, `awdl0`
  stays accepted (`AWDL_LLW_COMPANION.md` §2.3).
- It remains `?` whether the caller of the helper treats the error as fatal; the function throws, but the
  catch was not located.

## 3. Point 2 — destruction of the companion (op = 0)

### 3.1 Who triggers it

`wifip2pd` has **exactly two** calls to `Apple80211RawSet` (0x1004457e0):

| Site | Type | Function | Payload |
|---|---:|---|---|
| 0x100048d95 | 94 (`VIRTUAL_IF_CREATE`) | `sub_100047660` | creation of `awdl0` |
| 0x10004b3e6 | 293 (`COMPANION_SKYWALK_IF_CREATE`) | `sub_10004B300` | `op = 1` (decompiled: `v61[0] = a5`, `v61[1] = 1`); type at 0x10004b3be |

There is no other 293, there is no `RawSet` of type 95 (`VIRTUAL_IF_DELETE`) and no construction of the
payload with `op = 0` was found. Therefore **this build of `wifip2pd` does not destroy the
companion**: the destruction branch of the kernel remains for an external client (diagnostic
tool or a manual `RawSet`), not for the daemon. The fact that `sub_1000586D0` has no
absolute references does not change the conclusion: that function is IPv6 (§2.1), not the 293.

### 3.2 What the kernel does

| Step | Address | Evidence |
|---|---|---|
| Handler 293 | 0xffffff8001722ef2 | `op` at payload+4. `op = 0`: if the companion pointer (`ivars+120`, base `this+0x138`) is not null, `destroySkywalkInterface(core, getInterfaceId(companion))` at 0xffffff8001722fe9; if null, returns `0xE00002C0` with the log of line 3853. `op` other than 0/1: no-op returning 0. |
| `destroySkywalkInterface` | 0xffffff80016aae4e | Resolves the `[ifid]` entry of the table of the core (`core+11232`), calls `[obj+0x600](2)` (0xffffff80016aaebc) and `[obj+0x90]()` (0xffffff80016aaed7), and writes 0 into `core->ifTable[ifid]` (0xffffff80016aaeec). |
| Slot +0x600 | 0xffffff8000a937b0 | `IOService::terminate(unsigned int)` with `2`; the exact meaning of that option was not followed (`?`), but it is the withdrawal of the service that precedes the `free`. |
| Slot +0x90 | 0xffffff80015e0046 | `AppleBCMWLANLowLatencyInterface::free`: frees the blob of `ivars+0x140` with `IOFreeTypeImpl` and chains into `AppleBCMWLANSkywalkInterface::free` (0xffffff80015d8ce6). |

What is released is, therefore, the LL object (with its 0x140 blob) and its entry in the table
of the core; the withdrawal of the `ifnet`/IORegistry entry happens within
`IOService::terminate`/`free` (the detail instruction by instruction was not followed, `?`). An
own class must reproduce the two slots if it wants `op = 0` not to leave the object hanging:
`terminate(2)` and `free` (or the equivalent `stop` + `release`), and clear its own pointer.

## 4. Point 3 — minimal `RTLLlwInterface` class

### 4.1 Success condition of the 293

Slot **+0x1180** of the AWDL class (declared at `AWDLInterface.h:275`, today with
`AWDL_STUB(...)` → `kIOReturnUnsupported`) must:

1. Read `op` at payload+4 and the name at payload+8.
2. `op = 1`: if there is no companion, create the LL object and its `ifnet`; copy the BSD name
   (max. 16 B) into payload+8 and return 0. `if_nametoindex("llw0")` in the daemon
   (0x10004b814) is the final validation.
3. `op = 0`: destroy and return 0 (or `0xE00002C0` if there is no companion, like Broadcom).
4. Any other `op`: return 0 without touching anything.

### 4.2 Calls to the family

The valid sequence is the one `awdl0` already uses in `Source/AirLayer/AWDLInterface.cpp`
(`initWithController` + `startWithRegistration`), with role 2 and prefix "llw":

| Call | Symbol / address | For `llw0` |
|---|---|---|
| `IO80211VirtualInterface::init(ctrl, &mac, role, name)` | 0xffffff80023305f8 | `role = 2`, `name = "llw"`; sets `IO80211InterfaceRole = "LowLatency"` (role table, entry 2) |
| `IO80211SkywalkInterface::setInterfaceId(id)` | 0xffffff80022f950a | id 9, the one Broadcom uses for the LL (`createSkywalkInterface`, 0xffffff80016aa874, params+0x10 = 9) |
| `attach(ctrl)` | slot 0x670 of the base | same as `awdl0` |
| `IO80211VirtualInterface::start(ctrl)` | 0xffffff80023308aa | startup of the base; the LL of Broadcom adds `setInitMacAddress` (0xffffff80015e00bc) before the base |
| `IOSkywalkEthernetInterface::initRegistrationInfo(reg, 1, 0x130)` | 0xffffff8002b108e2 | version 1, size 0x130 |
| `registerEthernetInterface(reg, queues, n, txPool, rxPool, flags)` | 0xffffff8002b10a84 | the same three queues as `awdl0` (TX submission, TX completion, RX completion) |
| `registerService(kIOServiceSynchronous)` | `IOService` | publishes and creates the `ifnet`; the name is read afterwards with `getBSDName` |
| `IO80211SkywalkInterface::getBSDName()` | 0xffffff80022f5fca | source of the name for payload+8 |
| `setCompanionInterfaceId(awdlId)` | 0xffffff80022f952a | id link; write-only (§4.4) |

`RegistrationInfo` (0x130 B): +0x0C = 3 (subfamily), +0x10 = 2 (role, set by `init`),
+0x14 = 2 (nmedia), +0x24 = 0x80, +0x28 = 0x80 (media and current media), +0x30 = pointer to
"llw", +0x38 = 0 (unit), +0x3C = 64 (headroom), +0x40 = 4 (flags, the `|= 4` Broadcom puts
in `createSkywalkInterface`), +0x48 = 0x594/0x594 (MTU/max.), +0x108 = MAC. They are the
same offsets `AWDLInterface.cpp` uses for `awdl0` (`AWDL_REGINFO_*`).

The minimal base is `IO80211VirtualInterface` (it does not require the 63 pure slots of
`IO80211AWDLProtocol`); if the compiler deems it abstract due to `OSDeclareAbstractStructors`,
the alternative is deriving from `IO80211AWDLProtocol` and inheriting the stubs of `RTLAwdInterface`.
That choice does not change the cells of §4.3; the instantiability of the base remains `?` until
compiling.

`IO80211Controller::setAwdlInterface` (0xffffff8002299c24) is **not** needed for `llw0`: it is
the AWDL pointer of the controller, not the mechanism creating the `ifnet`; on `awdl0` the BSD
name already exists after `registerService` and before publishing the pointer. It remains `?` whether the BSD
client of the family accepts a second virtual interface of the same controller without a specific
role 2 registration; the only static evidence is the sequence of `awdl0`.

### 4.3 Vtable cells

| Cell | Reference LL implementation | Required for the 293? | What it does |
|---|---|---|---|
| +0x9E8 `getBSDNamePrefix` | 0xffffff80015e00fc | **Yes** | Returns "llw"; without it the name would be that of the base. It calls `validateDispatchQueue` first. |
| +0x14E0 `start` | 0xffffff80015e00bc | **Yes** | `setInitMacAddress` from the `RegistrationInfo` and `AppleBCMWLANSkywalkInterface::start`. |
| +0x8E0 `initBSDInterfaceParameters` | 0xffffff80015e034a | `?` | The base creates the parameters; the LL adds the pointer of `ivars+0x140` and `params+0x18 = 8`. For a normal Ethernet `ifnet` the base may suffice. |
| +0x930 `setInterfaceEnable` | 0xffffff80015e010e | Recommended | Base + `reportLinkStatus(this, 3, 0x80)` when enabling; it is the visible event of `llw0`. |
| +0x910 `processBSDCommand` | 0xffffff80015e03e2 | Optional | Rejects 4 apple80211 constants with 22 and delegates the rest into `IO80211InfraInterface::processBSDCommand` (0xffffff8002360290). |
| +0xB38 `logTxPacket` | 0xffffff80015e0268 | Datapath | Logging; a no-op is admitted. |
| +0xB50 `inputPacket` | 0xffffff80015e02e2 | Datapath | Real RX; no-op if there is no LL traffic. |
| +0xA80 `configureMulticastFilter` | 0xffffff80015e041e | No | No-op (returns 0). |
| +0x90 `free` / +0x600 `terminate` | 0xffffff80015e0046 / 0xffffff8000a937b0 | For `op = 0` | Frees its own blob and withdraws the service; without them the destruction leaves the object alive. |
| +0xED8 `flushFlowQueues` | base (`IO80211SkywalkInterface`), null-safe | No | Only delegates if there is a companion (§4.4). |

Correction note: the list of constants rejected by `processBSDCommand` is of **four**
(0xC02069C9, 0xC02869C9, 0x802069C8, 0x802869C8), not five. `0xC02069C8` only appears
as the boundary of the `cmp edx, 0C02069C8h` / `jg` of 0xffffff80015e03e6, not as an equality
rejected; the equalities returning 22 are at 0xffffff80015e0400 (0xC02069C9) and
0xffffff80015e0408 (0xC02869C9), plus 0xffffff80015e03ee and 0xffffff80015e03f6 for the two
shapes `0x8020/0x8028`. That is why `0xC02069C8` falls into the base. The correction affects
`AWDL_LLW_COMPANION.md` §3.4.

### 4.4 Does `awdl0` need a reference to the companion?

Not for the 293 nor for the daemon:

- `AppleBCMWLANProximityInterface::flushFlowQueues` (0xffffff800172ac9e) reads `ivars+120`,
  checks nullity and only then delegates into `AppleBCMWLANSkywalkInterface::flushFlowQueues`;
  afterwards it calls the base. It is null-safe.
- `IO80211SkywalkInterface::setCompanionInterfaceId` (0xffffff80022f952a) has only two
  callers (the 293 handlers of Proximity and NAN); `getCompanionInterfaceId`
  (0xffffff80022f951a) **has no xrefs**: the id is written and nobody reads it in this KC.
- `IO80211VirtualInterface::setCompanionLowLatencySkywalkInterface` (0xffffff8002334c6e)
  is only called from `AppleBCMWLANLowLatencyInterface::withProvider`
  (0xffffff80015dfee4, site 0xffffff80015dffbf); its getter (0xffffff80023344cc) has no
  xrefs. It is an internal link of the Broadcom factory, not a requirement of the 293. Whether the
  symbol is linkable from a kext remains `?` (the export table was not checked in this pass).

Therefore, the AWDL class may store the pointer only if it implements `flushFlowQueues` for
real, and it must do it null-safe; the 293 returns 0 and the daemon validates `llw0` without consulting
that link.

## 5. Corrections to the previous documents

| Document | Correction |
|---|---|
| `AWDL_LLW_COMPANION.md` §3.4 | `processBSDCommand` rejects 4 constants, not 5: `0xC02069C8` is only the boundary of the `cmp edx, 0C02069C8h` / `jg` of 0xffffff80015e03e6; the equalities returning 22 are 0xC02069C9 (0xffffff80015e0400) and 0xC02869C9 (0xffffff80015e0408), plus the two shapes 0x8020/0x8028, so `0xC02069C8` is delegated into the base. |
| `AWDL_LLW_COMPANION.md` §3.5 / `AWDL_ETAPAS_2_A_4.md` §3.4 | The ioctls 130/131 are not apple80211. The 130 resolves in `in6_control`; the 131, if the bridge does not intercept it (`?`), goes through `processBSDCommand` and the base rejects it with 45 (ENOTSUP). |
| `AWDL_ETAPAS_2_A_4.md` §3.2 | The destruction by `op = 0` is not issued by `wifip2pd` in this build (there is only one 293, with `op = 1`); the kernel branch remains for an external client. |

## 6. Correction (2026-11-15): `llw0` needs `awdl0` as its parent before `start`

The sequence in §4.2 leaves out a mandatory step. Basis: the same copy of the 26.7.1
kernelcache (md5 `f489ff8be80e6013a1bf47561dcaef45`), headless Hex-Rays on a fresh copy of the
database.

### 6.1 What `start` requires for role 2

`IO80211SkywalkInterface::start` (0xffffff80022f5792) reads the role at ivars+88. For **role 2**:

- it takes the parent from ivars+104;
- if it is null, it logs `"Error: Assert failure"` (source `IO80211SkywalkInterface.cpp`, line
  516) and returns failure: the interface does not start;
- if it exists, it copies the parent's peer manager (`*(*(parent+288)+40)`) into the LL's
  ivars+40 and logs `"Assigning peerManager %p for interfaceid %d, from ivars->parentInterface (%p) role %d"`.

For the other roles, `start` calls the peer manager creation slot and fails if it returns null
(`"ERROR: Family Skywalk Interface start peer manager init fail"`). For role 2 that slot is not
called. `IO80211VirtualInterface::createPeerManager` (0xffffff80023308c2) creates a manager only
for roles 6, 7, 8, 9 and 10; for any other role it returns 0.

### 6.2 Who writes the parent and in what order

ivars+104 is written by `IO80211SkywalkInterface::setParentInterface` (0xffffff80022f97a2), which
does only that write. `AppleBCMWLANLowLatencyInterface::withProvider` (0xffffff80015dfee4),
before the `start` done by `createSkywalkInterface`, calls in this order:

1. `AppleBCMWLANSkywalkInterface::initWithProviderAndParameters` (role from the params, 2);
2. `IO80211SkywalkInterface::setLowLatencyEnabled(true)` (0xffffff80022f97c6, byte ivars+112);
3. `IO80211SkywalkInterface::setParentInterface(LL, parent)`;
4. `IO80211VirtualInterface::setCompanionLowLatencySkywalkInterface(parent, LL)`.

Correction to §4.2: `setLowLatencyEnabled(true)` and `setParentInterface(awdl0)` go between
`setInterfaceId` and `start`. §4.4 still holds for `awdl0`: the LL needs the link to its
parent, not the other way round.

### 6.3 Base of the class

Broadcom's LL belongs to the infra chain (`IO80211InfraProtocol`). In that chain:

- `IO80211InfraInterface::start` (0xffffff800235f2e6) does work of its own only when the role
  (ivars+88) is 1;
- `IO80211InfraInterface::registerInfraEthernetInterface` (0xffffff80023683e0) also limits its
  own part to role 1 and calls `IOSkywalkEthernetInterface::registerEthernetInterface`.

An LL on the `IO80211VirtualInterface` chain, as §4.2 proposed, is not ruled out if the parent
is set before `start`, but this was not checked (`?`). The implementation follows Broadcom's
chain.

### 6.4 State of the implementation

`RTLLlwInterface` in `Source/AirLayer/AWDLInterface.cpp`:

- infra chain, role 2, id 9, low latency enabled and `awdl0` as its parent before `start`;
- prefix `llw`, registration flags 4, `awdl0`'s MAC and MTU;
- what it sends goes into `awdl0`'s TX ring; reception is not implemented.

It answers the 293 only with `rtlawdl_llw=1`. The symbols resolve against the 25G342
kernelcache. The layer (UUID `CB0F467C`) was installed on the EFI on 2026-11-15 and was loaded; what was measured is in §6.5.

Three boots with `rtlawdl_llw=1`, 25G342 kernelcache:
1. **Layer `CB0F467C`.** The 293 arrives and `llw0` registers, but the BSD name is still empty
   after `registerService` and the layer tears it down. The kernel logs
   `PM waiting on pmDriverCallout(0x2) to RTLLlwInterface (2000 ms)` and the ioctl takes about
   10 s. `IO80211InfraInterface::setPowerState` (0xffffff8002367f58, 25G241 addresses) calls
   `IO80211SkywalkInterface::setPowerState` (0xffffff80022f8782) and then, when not on the
   thread of the work loop at ivars+56, passes the WCL system state through `runActionBlock` on
   that work loop, which the 293 handler holds. Fix: `llw0` answers `setPowerState` with the
   Skywalk part only.
2. **Layer `F1B9F4DB`.** The power wait is gone, but no `ifnet` appears and `wifip2pd` runs out
   its 30 s (`Timed out while waiting for the low latency interface`). The kernel creates and
   closes the `llw0` nexus provider twice, 5 s apart. `IOSkywalkNetworkBSDClient::start`
   (0xffffff8002aff562) retries `kern_nexus_controller_alloc_net_provider_instance` 50 times
   every 100 ms while it returns 16 (EBUSY).
   `IOSkywalkEthernetInterface::initBSDInterfaceParameters` (0xffffff8002b10bca) uses the 6-byte
   MAC as the `ifnet` unique id (eparams +16 pointer, +24 length), and `llw0` carries `awdl0`'s
   MAC. `AppleBCMWLANLowLatencyInterface::initBSDInterfaceParameters` (0xffffff80015e034a)
   replaces it with an 8-byte id of its own. Fix: the same, with the MAC followed by `"ll"`.
3. **Layer `CF88F890`.** `llw0` appears with `awdl0`'s MAC, MTU 1428, `media: autoselect` and
   `IO80211InterfaceRole = "LowLatency"`. `wifip2pd` accepts it (`Created WiFiInterface ...[llw0]`,
   link up, link-local IPv6 address) and no longer logs the companion error. `awdl0` stays up.
Pending: reception on `llw0`, what `rapportd` does with a real low latency session and which
`fd39::` address `llw0` gets. That needs an iPhone.

## 7. Method and limits

- Headless disassembly over copies of the databases of `wifip2pd` and of the kernelcache,
  with probing scripts `e1`–`e10` (daemon) and `f1`–`f14` (kernel); the originals were not
  opened for writing.
- Every cited address was checked against the corresponding function head; the call
  sites were read in the disassembly and the cited `cmp`/`mov` are real instructions.
- There was no execution on hardware nor with the kext loaded: the runtime behavior of
  `registerService` for a second virtual interface and the `catch` of the IPv6 error remain
  as `?`.
- No code nor other documents were touched; this file is the only result of the task.
