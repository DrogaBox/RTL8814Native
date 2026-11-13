# Open points of AWDL stage 1 resolved in the kernelcache

Resolves three of the points marked with `?` in `AWDL_STAGE1_IMPL_SPEC.md` §15 from the
disassembly database of `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin 25.6.0), opened over
a copy in a temporary directory. All addresses are kernelcache VAs. Nothing has been
run on hardware: what follows is code reading, and what the code does not pin down remains
marked with `?`.

## 1. Object the `safeMetaCast` of the 94 is done over and slot `+0x1160`

The table `gSetHandlerTable[93]` does not call `apple80211setVIRTUAL_IF_CREATE`: it calls handler
`setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`), which validates the request and decides whether to send it to the WCL or
to the bridge.

Handler (`0xffffff80022886a8`):

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff80022886c8` | `cmp dword ptr [rcx+18h], 20h` | requires `req_len == 32` (`+0x18`); else returns 22 |
| `0xffffff80022886d1`–`0xffffff80022886d8` | `mov rcx,[rcx+20h]` / `test rcx,rcx` / `jz` | requires a non-null `req_data` (`+0x20`); else returns 22 |
| `0xffffff80022886dd`–`0xffffff80022886e4` | `mov rax,[rsi+120h]` / `mov rdi,[rax+0D8h]` | `glue = *(iface+0x120)`; `wcl = *(glue+0xD8)` |
| `0xffffff80022886f8`–`0xffffff8002288708` | argument loading | `sendIOUCToWcl(glue, 0x802869C8, 94, data, 32, &consumed)` |
| `0xffffff800228870b` | `call IO80211Glue::sendIOUCToWcl` | WCL path |
| `0xffffff8002288710`–`0xffffff8002288714` | `test byte [r15],1` / `jz` | if the WCL does not consume it, to the bridge |
| `0xffffff8002288716`–`0xffffff800228871b` | `cmp eax, 0E082280Fh` / `jz` | if the WCL returns `0xE082280F`, to the bridge |
| `0xffffff800228874c`–`0xffffff800228875c` | `rdi = rsi` (the `IO80211SkywalkInterface*`), `rsi = [req+0x20]` | jump to the bridge with the **interface** and the **payload** |

Bridge `apple80211setVIRTUAL_IF_CREATE` (`0xffffff8002268494`):

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff80022684a1`–`0xffffff80022684a9` | `mov rax,[rdi]` / `mov esi,5Eh` / `call qword ptr [rax+0CC8h]` | `isCommandProhibited(94)` **over the same object** |
| `0xffffff80022684af`–`0xffffff80022684b7` | `test eax,eax` / `jz` / `retn` | if it returns non-zero, that value is the answer |
| `0xffffff80022684b8`–`0xffffff80022684c2` | `lea rsi, IO80211InfraProtocol::gMetaClass` / `call safeMetaCast` | the cast applies to the **same pointer**, with no intermediate lookup |
| `0xffffff80022684c7`–`0xffffff80022684ca` | `test rax,rax` / `jz` | if the cast fails, `0xE082280E` |
| `0xffffff80022684cc`–`0xffffff80022684e0` | `mov rax,[r14]` / `mov rax,[rax+1160h]` / `jmp rax` | slot **`+0x1160`** of the cast object, with `rdi = object`, `rsi = payload` |
| `0xffffff80022684e2` | `mov eax, 0E082280Eh` | return when the cast does not land |

Answered: **there is no lookup whatsoever**. The object of the `safeMetaCast` and of slot `+0x1160` is the
second argument of the handler, i.e. the `IO80211SkywalkInterface*` that the dispatch layer
delivers with the request; it is neither the controller nor the primary interface, and
`getAwdlInterface` is not queried. The first argument of the handler is the `IO80211Controller` (deduced from the
sibling handler, §3). Slot `+0x1160` figures as `___cxa_pure_virtual` in the vtable of
`IO80211InfraProtocol` (`__ZTV20IO80211InfraProtocol`, `0xffffff80024656a0`; the cell is at `+0x1170` from the
symbol, i.e. `vptr + 0x1160`), so the call always lands in the implementation of the concrete subclass
(the Broadcom controller one or `RTLAirInterface`).

`?` Which concrete object the dispatch delivers as second argument (the interface named in
`apple80211req.ifname` versus another glue reference) was not closed in this pass: the
handler does not check it and the bridge only requires passing the cast to `IO80211InfraProtocol`.

Consequence for stage 1: the gate `isCommandProhibited(94)` is evaluated at
`0xffffff80022684a9` over the **interface**. `RTLAirInterface` does not override that slot (`+0xCC8`),
so it answers `IO80211SkywalkInterface::isCommandProhibited` (`0xffffff80022f9bce`), which
decides nothing by itself: it calls `getController()` (slot `+0xBA8`, `0xffffff80022f957a`) and
returns the result of slot `+0xC50` of the controller. That slot is pure in
`__ZTV17IO80211Controller` and in the layer it is implemented by `RTLAirController::isCommandProhibited`
(`Source/AirLayer/RTLAirLayer.cpp:1726`, slot 394 of `Source/IO80211/IO80211_25_6_0.h:819`), which
returns `false`. The 94 is not blocked by that gate.

## 2. `sendIOUCToWcl` and the 94: when it falls to the bridge

`IO80211Glue::sendIOUCToWcl(uint, uint, void*, unsigned long, bool&)` is at
`0xffffff8002194278`.

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002194338`–`0xffffff8002194366` | block preparation | stores `msg = 0x802869C8`, `code = 94`, `data`, `len = 0x20` |
| `0xffffff8002194366` | `setz byte ptr [r13+2Dh]` | marks the case `msg == 0x802869C8` |
| `0xffffff800219449c`–`0xffffff80021944a8` | `mov al,[rax+50h]` / `and al,1` / `mov [rcx],al` | writes into `*consumed` the **bit 0** of the result of the block |
| `0xffffff80021945cf` | `____ZN11IO80211Glue13sendIOUCToWcl..._block_invoke` (`0xffffff80021945cf`) | runs the request on the glue thread |
| `0xffffff8002194619`+ | call to the WCL object via `vtable + 0x120` | `(*(...)(v2 + 288))(v2, ...)`, with `v2 = a1[7]` |

The block (`0xffffff80021945cf`) calls the WCL object via its slot `+0x120` and leaves the result in the
block cell that `0xffffff800219449c` reduces to a bit; that bit is what the handler reads at
`0xffffff8002288710`.

Answered: for a controller **without WCL** request 94 never reaches `sendIOUCToWcl`, because
`0xffffff80022886e4` loads `*(glue+0xD8)` and `0xffffff80022886eb`–`0xffffff80022886ee` jumps
directly to the bridge when it is null. With a WCL present but not taking it, the path also falls to the
bridge for two independent reasons: the consumption bit stays 0
(`0xffffff8002288710`–`0xffffff8002288714`) or the call returns `0xE082280F`
(`0xffffff8002288716`–`0xffffff800228871b`). Both paths converge at
`0xffffff8002288738`–`0xffffff800228875c`, which jumps to the bridge.

`?` The class of the object at `glue+0xD8` and the concrete method of slot
`+0x120` have not been identified; it is only known that its bit 0 decides the consumption.

## 3. `getVIRTUAL_IF_PARENT`: how the parent is found

`getVIRTUAL_IF_PARENT` is at `0xffffff800227b7ff`.

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff800227b80f`–`0xffffff800227b812` | `mov rax,[rdi]` / `call qword ptr [rax+0C80h]` | calls slot **`+0xC80`** of the **first argument** |
| `0xffffff800227b818` | `mov r14, rax` | the result is the candidate parent |
| `0xffffff800227b81b`–`0xffffff800227b825` | `safeMetaCast(a2, IO80211VirtualInterface::gMetaClass)` | the second argument must be a virtual interface |
| `0xffffff800227b82d`–`0xffffff800227b83a` | `eax = 6` / two `test`s / `jz` | if the parent or the cast are null, returns 6 |
| `0xffffff800227b83c`–`0xffffff800227b844` | `call IO80211SkywalkInterface::getBSDName` | name of the parent |
| `0xffffff800227b847`–`0xffffff800227b84a` | `strlen` | length of the name |
| `0xffffff800227b84f`–`0xffffff800227b852` | `cmp [rbx+18h], eax` / `jb` | if `req_len < strlen`, returns 22 |
| `0xffffff800227b854`–`0xffffff800227b85d` | `memmove([req+0x20], name, strlen)` | writes the name into the payload |
| `0xffffff800227b862` | `xor eax,eax` | success (0) |

Answered: the parent comes from a virtual call to slot `+0xC80` of the first argument of the
handler, which is the `IO80211Controller`; in the vtable of `IO80211Controller`
(`__ZTV17IO80211Controller`, cell `+0xC90`) that slot is
`IO80211Controller::getPrimarySkywalkInterface()` (`0xffffff8002295e44`). Neither
`getAwdlInterface()` nor the attached provider of the object is used. The name returned is the BSD one of the
primary interface (the STA one, `en2` on this system), not that of `awdl0`, and the operation is read-only over the
payload: it creates and publishes nothing.

Consequence for stage 1: the 97 is a family handler and needs no own code.
Over `awdl0` it answers with the name of the primary interface (the STA, `en2` on this system)
as long as `awdl0` passes the cast to `IO80211VirtualInterface` and the controller has a primary
interface. `setAwdlInterface()` takes no part in the 97 (point 2 of §15 of the spec): the
call remains useful for other consumers of `getAwdlInterface()`, but it is not a requirement of
this handler.

## 4. Points still open

- The exact object the dispatch delivers as second argument of the handler (§1), beyond
  the bridge only requiring the cast to `IO80211InfraProtocol`.
- The class of the WCL object at `glue+0xD8` and the method of slot `+0x120` deciding the consumption (§2).
- None of these readings has been contrasted on hardware; check T2 of
  `AWDL_STAGE1_IMPL_SPEC.md` §14 remains the one that decides.
