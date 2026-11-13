# `VIRTUAL_IF_PARENT` (97) over `awdl0`: where `en2` comes from and what must exist beforehand

It closes `AWDL_STAGE1_STUB_VALUES.md` §2, row 4's assumption: that the kernel answers the 97 with the
primary interface's name without own code. Base: kernelcache of macOS 26.7.1 (25G241, Darwin
25.6.0, x86_64), IDA headless (`idat -A -S`) over a copy of `boot_awdl.kc.i64`. All
addresses are VAs and every row was read in the disassembly; whatever was not fixed carries `?`.

## 1. The handler and its table entry

| VA | Fact |
|---|---|
| `0xffffff800245eb60` | `__ZL16gGetHandlerTable`, the modern get table (symbol present in the IDB). |
| `0xffffff800236cdab` | `lea rcx, __ZL16gGetHandlerTable` in `apple80211RouteExternal`. |
| `0xffffff800236ce2e` | `mov rax, ds:(...) [rcx+rax*8]`, indexed load of the get table. |
| `0xffffff800236ce43` | `call rax`: the handler receives `rdi` = controller, `rsi` = interface, `rcx` = `apple80211req`. |
| `0xffffff800245ee60` | cell `gGetHandlerTable+0x300` = index 96 = code 97; holds `0xffffff800227b7ff`. |
| `0xffffff800227b7ff` | head of `__ZL20getVIRTUAL_IF_PARENTP17IO80211ControllerP23IO80211SkywalkInterfaceP20IO80211APIUserClientP13apple80211req`. |

The handler is a single 0x76-byte block: it neither re-enters `apple80211RouteExternal` nor
queries `getAwdlInterface`.

## 2. What it reads to build the name

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff800227b809`–`0xffffff800227b80c` | `mov rbx, rcx` / `mov r15, rsi` | saves the `apple80211req` and the interface |
| `0xffffff800227b80f`–`0xffffff800227b812` | `mov rax,[rdi]` / `call qword ptr [rax+0C80h]` | slot **`+0xC80` of the controller** |
| `0xffffff800227b818` | `mov r14, rax` | candidate parent |
| `0xffffff800227b81b`–`0xffffff800227b825` | `safeMetaCast(rsi, IO80211VirtualInterface::gMetaClass)` | the requesting interface must be virtual |
| `0xffffff800227b82d` | `mov eax, 6` | failure value |
| `0xffffff800227b832`–`0xffffff800227b83a` | two `test`/`jz` | without parent or without cast → 6 |
| `0xffffff800227b83f` | `call IO80211SkywalkInterface::getBSDName` | parent object's name |
| `0xffffff800227b847`–`0xffffff800227b84a` | `strlen` | length |
| `0xffffff800227b84f`–`0xffffff800227b852` | `cmp [rbx+18h], eax` / `jb` | if `req_len < strlen` → 22 (EINVAL) |
| `0xffffff800227b856`–`0xffffff800227b85d` | `memmove(*(req+0x20), name, strlen)` | the destination is the pointer saved at `req+0x20`; copies `strlen` bytes without terminator |
| `0xffffff800227b862` | `xor eax, eax` | success |

It reads neither `+0x128`, nor a parent pointer on the interface, nor `req+0` (`ifname`). The only input is
the controller of the first argument, which the dispatch obtained with the interface's
`getController()` (slot `+0xBA8`, `0xffffff800236cd62`).

## 3. Slot `+0xC80` = `IO80211Controller::getPrimarySkywalkInterface`

| VA | Fact |
|---|---|
| `0xffffff8002461dd8` | cell `__ZTV17IO80211Controller` (`0xffffff8002461148`) `+0xC90`, i.e. vptr`+0xC80`; holds `0xffffff8002295e44`. |
| `0xffffff8002295e44` | head of `IO80211Controller::getPrimarySkywalkInterface` (header slot 400). |
| `0xffffff8002295e48` | `mov rax, [rdi+120h]`: controller's private block. |
| `0xffffff8002295e4f` | `mov rdi, [rax+188h]`: the parent saved in that block. |
| `0xffffff8002295e5e` | `safeMetaCast` to `IO80211SkywalkInterface::gMetaClass`; if the field is null, returns null. |

Seven-instruction function: no lock, no refcount, no lazy creation. The parent is
exactly the pointer saved at `*(controller+0x120)+0x188`.

## 4. Who writes `+0x188` of the controller's block

The block is reserved in `IO80211Controller::init` and saved at `[controller+0x120]`
(`0xffffff8002292926` / `0xffffff800229292b`); the field is zeroed at `0xffffff8002292984`.

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002295fce` | head of `__ZN17IO80211Controller15attachInterfaceEP8OSObjectP9IOService` | the only non-null write |
| `0xffffff8002295fe8` | `safeMetaCast(anObject, IO80211SkywalkInterface::gMetaClass)` | Skywalk interfaces only |
| `0xffffff8002295ffb` | `call qword ptr [rax+998h]` | `IONetworkController::prepare` (`0xffffff80026f3b88`) |
| `0xffffff8002296001` | `mov rax, [r15+120h]` | controller's block |
| `0xffffff8002296008` | `cmp qword ptr [rax+188h], 0` / `jnz` | **first gate**: it does not replace an existing primary |
| `0xffffff8002296012` | `mov rcx, [rbx+120h]` | interface's block |
| `0xffffff8002296019` | `cmp dword ptr [rcx+58h], 1` | **second gate**: role 1 |
| `0xffffff800229601f` | `mov [rax+188h], r14` | saves the interface object |

`+0x58` of the interface's block is the role: written by `IO80211SkywalkInterface::setInterfaceRole`
(`0xffffff80022f60f0`, store at `0xffffff80022f6101`) and read by `getInterfaceRole`
(`0xffffff80022f5ed8`, `[ivars+88]`).

`IO80211Controller::detachInterface` (`0xffffff800229604a`) is the only erasure: it compares the
object with `[rcx+188h]` at `0xffffff8002296071` and writes zero at `0xffffff8002296097`; if it does not match,
it only logs `"interface is not primary interface"`.

A sweep of all stores to `[reg+188h]` in `IO80211*`/`IOSkywalk*` functions of both ranges
gave five hits; the other three (`0xffffff800232b60f`, `0xffffff800235f43a`,
`0xffffff8002370284`) write to different objects (NAN, `IO80211InfraInterface`, monitor). For the
controller's block only the zero of `init` and the store of `attachInterface` exist.

## 5. Who calls `attachInterface`

References to `0xffffff8002295fce` are two and both data: vtable cell
`0xffffff8002461f20` (`__ZTV17IO80211Controller+0xDD8` = vptr`+0xDC8`) and `0xffffff80017ea5c0`.
There is no code reference.

| VA | Fact |
|---|---|
| `0xffffff80016aa874` | `AppleBCMWLANCore::createSkywalkInterface` |
| `0xffffff80016aaa38` | `AppleBCMWLANSkywalkInterface::withProvider(...)` returns the interface in `r13` |
| `0xffffff80016aaa84`–`0xffffff80016aaa8a` | `rsi = r13`, `rdx = rbx`, `call qword ptr [rax+0DC8h]`: the only caller of the slot |
| `0xffffff80016aaa90` | `test al, al` and aborts creation if the attach failed |

Inside the family nobody calls it: `IO80211Controller::createVirtualInterface`
(`0xffffff800229612e`) only serves role 7 (`"ap"`, slot `+0xE68`) and does not touch `+0x188`; the 94
(`setVIRTUAL_IF_CREATE` `0xffffff80022886a8`, bridge `0xffffff8002268494`) validates `req_len == 32` at
`0xffffff80022886c8`, talks to the WCL and calls slot 94 and that of `IO80211InfraProtocol`, without
any reference to the primary.

## 6. Our controller does call it

`RTLAirLayer.cpp:1487-1493` creates `RTLAirInterface` with `initWithController` (which sets
`rtl_sky_setRole(this, kRoleSta)` with `kRoleSta = 1`, lines 239 and 324) and **before booting it**
calls `attachInterface(_iface, this)`, resolving to
`__ZN17IO80211Controller15attachInterfaceEP8OSObjectP9IOService`; the built binary carries it
as an external reference (`nm -u build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer`). The
real boot (`startWithRegistration`) comes afterward, at line 1512.

Consequence: from that instant `*(controller+0x120)+0x188` is the `en2` object, and the name
returned by `getBSDName` (`0xffffff80022f5fca`) is `"en2"`: the function composes `"%s%u"` with
`ifnet_name`/`ifnet_unit` at `ivars+0x10` (`0xffffff80022f602b`) and caches it after flag
`[ivars+1]` (`0xffffff80022f5fdc`).

## 7. Answer

**Yes: a freshly created `awdl0` returns `en2` for the 97 without any action of `RTLAwdInterface`.**
The condition is not on the virtual interface but on the controller: that `+0x188` already holds the
primary object. In this driver it does because `RTLAirLayer.cpp:1493` calls `attachInterface` with the
STA interface. `RTLAwdInterface` must not and cannot fix it:

- `attachInterface` rejects a role other than 1 (`0xffffff8002296019`) and `awdl0` is role 6;
- the first gate (`0xffffff8002296008`) prevents replacing an already present primary;
- `awdl0` never calls `attachInterface` (only `IOService::attach`).

What `RTLAwdInterface` must meet is what `rtl_vif_init` already does: the interface's block pointing
to the controller at `+0x30` (`getController`, `0xffffff80022f957a`, reads
`[ivars+0x30]`) and the object passing the cast to `IO80211VirtualInterface`.

| Condition | Effect on the 97 |
|---|---|
| Normal boot path (step ≥ 4, `en2` attached) | 0 and `"en2"` |
| Controller without attached primary (step 3, or `awdl0` created by another route) | `+0x188` null → 6 (ENXIO) |
| `detachInterface` over `en2` (`0xffffff8002296097`) | back to 6 |
| `en2` without ifnet or without cached name | 0 with length 0; the name is composed on the first `getBSDName` (`?` the exact instant, not measured on hardware) |
| `awdl0` (role 6) trying to enter | rejected by `0xffffff8002296008` and `0xffffff8002296019` |

For stage 1 no stub of the 97 is needed. If the hardware check returned 6, the cause
would be the controller's attach (or a previous `detachInterface`), not
`awdl0`'s `RegistrationInfo`.

## 8. Searches executed and pending

Executed: `gGetHandlerTable` cell for code 97; full disassembly of
`getVIRTUAL_IF_PARENT`, `getPrimarySkywalkInterface`, `getBSDName`, `attachInterface`,
`detachInterface`, `setInterfaceRole`, `getInterfaceRole`, `createVirtualInterface`; sweep of
stores to `+188h` in `IO80211*`/`IOSkywalk*` functions; xrefs to `attachInterface`; sweep of
`[reg+0xDC8]` in `0xffffff8001500000`–`0xffffff8002400000`.

Remaining `?`: the effective value on hardware (decided by check T2), the instant the
name of `en2` gets cached relative to `awdl0`'s creation, and the behavior of
`AppleBCMWLANCore::createSkywalkInterface` outside this route, not reviewed because it does not run
with `RTLAirController`.
