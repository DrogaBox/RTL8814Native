# errno of the TX drop and the link state seen by `wifip2pd`

Closes the first two sub-questions of point 10 of `AWDL_INDICE.md` §8.2: the errno the stack sees when
`packetSubmission` returns `0xE00002D8` (queue disabled) and the consumer of `state+113`/`if_link_state`
in `wifip2pd`. The other two sub-questions of point 10 (the call of the `ifclassq` to `tx_push` and the registration path of the
`netif_llink`) are re-evaluated in §7. Base: kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64) and the
binary `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`, the same of the documents
`AWDL_WIFIP2PD_*`), both analyzed headless (`idat -A -S`) over copies of the annotated databases
(`boot_awdl.kc.i64` and `wifip2pd.i64`). Kernel addresses are VAs; those of the daemon are its
image addresses. Every address cited as a head was checked with
`ida_funcs.get_func(ea).start_ea == ea`; instruction citations are lines of the disassembly or of the
decompilation. IOReturn/errno/flag constant names are taken from the SDK headers
(`IOReturn.h`, `sys/errno.h`, `net/if.h`). What the analysis does not pin down goes with `?`.

## 1. Answer

1. **`errnoFromReturn` gives 16 (`EBUSY`) for that code.** `IOService::errnoFromReturn` (`0xffffff8000a95ab0`) maps
   `0xE00002D8` (`kIOReturnNotReady`) to errno 16 (`EBUSY`): the case is in the group whose handler
   executes `mov eax, 10h` (`0xffffff8000a95ae9`). The conversion is applied by `nxp_queue_tx_push`, which
   converts the return of slot `+0x1D0` with cell `+0x788` of the vtable (`jmp rcx` at
   `0xffffff8002aff52a`). `?` Which caller sees that errno: `nxp_queue_tx_push` is only reached by the
   virtual port path (`netif_vp_na_txsync`), not by the `ifclassq` of the BSD submission; the segment of §3 from
   `sub_FFFFFF80005E6110` upward is not demonstrated (§3).
2. **wifip2pd consumes neither the notification `if_link_state` nor `state+113`.** The interface
   discoverer (`sub_100043720`) only reads `IOInterfaceName`, the role property and the BSD index; there is
   no consumer of event `APPLE80211_M_LINK_CHANGED` (id 4, absent from the registered table) nor of
   route messages carrying link state. The only thing close to it is a reader of the `IFF_UP|IFF_RUNNING`
   state of the BSD entry by interface name via `getifaddrs` (`sub_100056790`,
   predicate `(~ifa_flags & 0x41) == 0` over the `AF_LINK` entry), exposed as a Swift property;
   `?` the endpoint of that chain (the thunks are referenced from keypath data, not from code) has not been
   walked up to the decision point (§6).
3. **All the "link state" wifip2pd produces is its own model.** The link-change literals
   ("WiFi powered on, setting link up on %s", "WiFiAwareStateHandler link state change to %s") are emitted
   from the WiFi power state and the `nw_path` of Network.framework, not from the `if_link_state` of the
   system (§6.3).

## 2. `errnoFromReturn`

The function is `__ZN9IOService15errnoFromReturnEi`, head `0xffffff8000a95ab0`, end
`0xffffff8000a95b66`. It is a virtual method of `IOService`: cell `+0x788` of the vtable holds
`0xffffff8000a95ab0` both in `__ZTV9IOService` (`0xffffff80002ad4e0`; qword at `0xffffff80002adc78`,
i.e. ZTV+0x798 = vptr+0x788) and in `__ZTV25IOSkywalkNetworkInterface`
(`0xffffff8002b3d2b8`, same cell). Verified structure:

1. Direct pass-through of the errno band: `mov eax, esi` (`0xffffff8000a95ab4`), `and eax, 3FFFh`
   (`0xffffff8000a95ab6`), `lea ecx, [rax+0C000h]` (`0xffffff8000a95abb`), `cmp ecx, esi`
   (`0xffffff8000a95ac1`); if equal it returns `rtn & 0x3FFF`. That is, values
   `0xC000..0xFFFF` return `rtn - 0xC000` without a table.
2. Rest: 49-case biased switch: `lea eax, [rsi+1FFFFD43h]` (`0xffffff8000a95ac7`),
   `cmp eax, 30h` / `ja default` (`0xffffff8000a95acd`/`0xffffff8000a95ad0`), jump table
   `jpt_FFFFFF8000A95AE0` at `0xffffff8000a95b68`, `jmp rax` (`0xffffff8000a95ae0`). The valid
   indices cover the common IOReturns `0xE00002BD`-`0xE00002ED`.
3. **Case `0xE00002D8`** (signed decimal `-536870184`): handler `0xffffff8000a95ae9`,
   `mov eax, 10h; pop rbp; retn` → **16 = `EBUSY`**. The same group covers `-536870187`
   (`0xE00002D5`, `kIOReturnBusy`), `-536870185` (`0xE00002D7`), `-536870165` and `-536870163`.
4. Default (`0xffffff8000a95b0c`): `mov eax, esi; test esi, esi; jz …; mov eax, 5` — 0 if the argument
   is 0, else **5 = `EIO`**.

Contrasts that confirm the table reading (same switch, neighboring cases):

| IOReturn | Constant (`IOReturn.h`) | Case (decimal) | Return | Handler |
|---|---|---:|---|---|
| `0xE00002D8` | `kIOReturnNotReady` | `-536870184` | 16 `EBUSY` | `0xffffff8000a95ae9` (`mov eax, 10h`) |
| `0xE00002D5` | `kIOReturnBusy` | `-536870187` | 16 `EBUSY` | same group |
| `0xE00002DB` | `kIOReturnNoSpace` | `-536870181` | 28 `ENOSPC` | `0xffffff8000a95b3c` (`mov eax, 1Ch`) |
| `0xE00002C2` | `kIOReturnBadArgument` | `-536870206` | 22 `EINVAL` | `0xffffff8000a95af7` (`mov eax, 16h`) |
| `0xE00002E2` | `kIOReturnNotPermitted` | `-536870174` | 1 `EPERM` | `0xffffff8000a95b20` (`mov eax, 1`) |
| `0xE00002D6` | `kIOReturnTimeout` | `-536870186` | 60 `ETIMEDOUT` | `0xffffff8000a95b5f` (`mov eax, 3Ch`) |

The first three are the failures `AWDL_STAGE1_IFUP_PATH.md` §5 documents for the queues; the
`kIOReturnNotPermitted` one is what `createEventPipe` returns without the entitlement (`AWDL_STAGE1_DISPATCH_PATH.md`).

## 3. The chain that converts the error

| Step | VA | Instruction or fact |
|---|---|---|
| `packetSubmission` rejects the queue | `0xffffff8002b2cb3f` | `test byte ptr [rdi+28h], 1`; `jz 0xffffff8002b2cd5c` (`0xffffff8002b2cb43`) |
| error code load | `0xffffff8002b2cd5c` | `mov r14d, 0E00002D8h`; `jmp 0xffffff8002b2cc3b` (epilogue, `0xffffff8002b2cd62`) |
| `nxp_queue_tx_push` calls slot 58 | `0xffffff8002aff506` | `call qword ptr [rax+1D0h]` with `rax = [r12]` (vptr of the queue); head of function `__ZL17nxp_queue_tx_pushP19kern_nexus_providerP10kern_nexusPvPyPjS5_` at `0xffffff8002aff4d1` |
| conversion with cell `+0x788` | `0xffffff8002aff50c`-`0xffffff8002aff52a` | `mov rcx, [r13+0]`; `mov rcx, [rcx+788h]`; `mov esi, eax`; `jmp rcx` — tail call to `errnoFromReturn` with `(context, IOReturn)` |
| registration of `tx_push` in the provider | `0xffffff8002aff1cc` | `mov [rax+58h], r8` after `lea r8, nxp_queue_tx_push` (`0xffffff8002aff1c5`), inside the table passed to `IOSkywalkBSDClient::registerNexusControllerProvider` (`0xffffff8002aff1d9`) |

The rows above are verified up to the conversion in `nxp_queue_tx_push`. What links that conversion
to `sub_FFFFFF80005E6110` (`ifnet_enqueue`) is **not** demonstrated: the enqueuing operation of the
`ifclassq` (`call qword ptr [r10+18h]`, `0xffffff80005e6172`) points at `fq_if_enqueue`, not at the
provider table, so errno 16 does not climb through that route. Verified result: `errnoFromReturn` converts
`0xE00002D8` into **16 (`EBUSY`)**; `?` the site that receives it when the queue is disabled.

## 4. `state+113` in the kernel

The writer is `IO80211SkywalkInterface::setInterfaceEnable`
(`__ZN23IO80211SkywalkInterface18setInterfaceEnableEb`, head `0xffffff80022f99ce`):

- `mov rax, [rdi+120h]` (`0xffffff80022f99e9`) and `mov [rax+71h], r12b` (`0xffffff80022f99f0`):
  writes byte **113 (0x71)** of the state object in ivar `+0x120` with the boolean argument.
- `IO80211VirtualInterface::setInterfaceEnable` (`0xffffff8002331ba6`) invokes it at
  `0xffffff8002331bf0`, and after the BPFs it ends with the call via slot `+0xE70`
  (`call qword ptr [rax+0E70h]`, `0xffffff8002331c5e`) that `AWDL_STAGE1_IFUP_PATH.md` §2.1 attributes to
  `setLinkState` (slot name `?` in this analysis).
- The same function calls `IO80211MacAddressAgent::setInterfaceEnable(bool)`
  (`0xffffff80022f9a33`) and creates the IOReporters after `checkPropertyExists("IOInterfaceName")`
  (`0xffffff80022f9a5c`, `0xffffff80022f9a68`).

`state+113` is, therefore, a kernel ivar with no direct route to user space; the visible
BSD-side equivalent is the interface state (`IFF_RUNNING`, `if_link_state`). `?` The readers of
`state+113` inside the kernel remain unsearched (keeps the `?` of `AWDL_STAGE1_IFUP_PATH.md` §2.1).

## 5. wifip2pd: searches run

Over `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), copy of database `wifip2pd.i64`:

| Channel | Search | Result |
|---|---|---|
| Apple80211 events | dwords of the registration table of `sub_100041B60` (`0x100515ca0`) | `1, 0xb, 0x3b, 2, 3, 0x7d, 0x7e, 0x7f, …` (the 16 ids of `AWDL_WIFIP2PD_DISCOVERY.md` §Pending 3). **It does not contain id 4**; id 1 is `APPLE80211_M_POWER_CHANGED` per the table of `AWDL_EVENTS.md` §3.1 |
| strings | 128 matches of `link/iff_/io80211/iointerface/bsdname/ifindex/operational/sioc/route` | none corresponds to reading the system link state; the link ones belong to the project's own model (§6.3) |
| names | 324 matches of `link/monitor` | Swift types `WiFiInterfaceLinkState`, `LinkStatsUpdate`, network/gateway monitors; no symbol of `if_link_state` |
| imports | `getifaddrs` (8 calls in 7 functions), `sysctl` (`sub_1001CEB40`, `sub_1001D1930`), `socket` (only wrapper `sub_1001CFA90`), `IORegistryEntryCreateCFProperty` (only discoverer `sub_100043720` and `sub_100045520`), `IOServiceAddMatchingNotification` (`sub_100042BB0`) | detailed in §6 |
| notifications | `IOServiceAddInterestNotification`, `CFNotificationCenterAddObserver`, `IORegistryEntrySearchCFProperty` | **not imported** |
| `notify_register_dispatch` | 6 calls (`sub_10018AFD0` ×2, `sub_10018B4E0`, `sub_1002D5F80`, `sub_100309050`, `sub_100435C80`) | Darwin notifications of purpose `?`; none associated with an interface in the surrounding strings |
| discoverer | string references of `sub_100043720` (`0x100043720`-`0x1000454c8`) | 11 in total: `IOInterfaceName`, `IO80211VirtualInterfaceRole`, `IO80211InterfaceRole`, the MAC format and log literals; **none of link state** — consistent with `AWDL_WIFIP2PD_DISCOVERY.md` |
| sysctl | MIBs built on the stack | `sub_1001CEB40`: `{4,17,0,0,2,4}`; `sub_1001D1930`: `{4,17,0,0,1,0}` — family `CTL_NET`/`PF_ROUTE` (sysctl of the route table, commented as such in `sys/socket.h`); exact names `NET_RT_*` `?`. They are route dumps (the `GatewayMonitor` carries ivar `routingSocket`, `0x100548d88`), not link state |

## 6. wifip2pd: findings

### 6.1 The `IFF_UP|IFF_RUNNING` reader by name

`sub_100056790`: walks `getifaddrs`, compares each `ifa_name` with a Swift `String` captured in the
context (`+16`/`+24`) and, over the entry with `ifa_addr->sa_family == 18` (`AF_LINK`), returns
`(~ifa_flags & 0x41) == 0` — i.e. it requires `IFF_UP (0x1)` and `IFF_RUNNING (0x40)` simultaneously
(values of `net/if.h`). It returns 2 if it does not find the entry or `getifaddrs` fails. The same predicate
appears in the generic closure `sub_100056970` (`(~*(u8 *)(a1+16) & 0x41) == 0`), which the generic runner
`sub_100046B00` executes with family `18` and metadata of enum **`WiFiInterfaceLinkState`**
(`sub_1000568D0`): that enum is computed from the flag state of the BSD entry.

The generic runner `sub_100046B00` is also used by the discoverer (`0x100043d44`) and the sites of
`if_nametoindex` (`0x10004b722`, `0x10004b9fd`), all with a captured interface name.

### 6.2 The keypath chain, without a closed endpoint

The concrete thunks `sub_10005F690` (state), `sub_10005F6B0` (flags writer), `sub_10005F6D0`
(MAC), `sub_10005F730` (IPv4) and `sub_10005F790` (IPv6) are only referenced from Swift **keypath data**:
`0x100517ba0`/`0x10052cb58` for the first and `0x100517ba8`/`0x10052cb60` for the second. The
generic writer (`sub_100056B90`, equivalent `sub_100056990`) rebuilds a flags value
`flags & 0xFFBE` and adds `0x41` or `0` to it depending on a boolean, and publishes it via keypath `0x10045FE30`:
synthesized flags, not read from the system at that point. `?` The concrete type evaluating those keypaths and
the decision place consuming the predicate of §6.1 have not been walked: the referencers are
keypath structures, not direct calls, and the Swift chain gets lost in generic dispatch.

### 6.3 The "link state" it produces is its own model

| Literal | VA | Function | Origin of the state |
|---|---|---|---|
| `WiFi powered on, setting link up on %s` / `WiFi powered off, setting link down on %s` | `0x1004a7990` / `0x1004a7900` | `sub_10041B7F0` (`0x10041b8c9` / `0x10041b9ce`) | WiFi power event; calls `sub_10027D480(1/0)` and the forced state (`sub_10040EB90`), not any read of the `ifnet` |
| `WiFiAwareStateHandler link state change to %s for interface %s with MAC %s` (`Link Up`/`Link Dow[n]`) | `0x1004a3620` | `sub_1003BB3E0` (`0x1003bb53e`) | the state and MAC arrive as parameters of the caller (`sub_1003C2770`, `sub_1003BB2F0`); it is a notification of the internal model |
| `Path became %s with ipv4: %s, ipv6: %s` + `setLinkEventWithIntegerReason:…forInterface:` | `selRef 0x100544060` | `sub_1000506A0` (`0x1000512a0`, `0x100051356`) | `nw_path_get_status` of Network.framework over the session interface (`NetworkConnectivityMonitor` of `InternetSharingRequesterSession`); it uses `getifaddrs` only to resolve IPv4/IPv6 addresses of the name and posts the event with `objc_msgSend(qword_100548580, …)` |
| `Forcing new link state because of mismatch` | `0x1004a6a90` | `sub_10040EB90` (`0x10040eda9`) | reconciliation of the internal model, not a read of the `ifnet` |

None of these routes consumes the notification `if_link_state` of the kernel nor `state+113`.

## 7. Consequences for stage 1

- For T3.5: the mapping `0xE00002D8` → 16 (`EBUSY`) is pinned in `errnoFromReturn`; the site receiving
  that errno when the queue is disabled is `?` (the `ifclassq` path does not go through
  `nxp_queue_tx_push`, §3). The expected symptom (zero TX without panic) does not change.
- Risk 5 of `AWDL_STAGE1_IFUP_PATH.md` §7 (wifip2pd requiring an active link) remains without an identified
  consumer: the discoverer does not look at the link (§5), event `LINK_CHANGED` is not registered (id 4), and
  the only reader of the BSD flags state is the generic one of §6.1 with its endpoint at `?`. If on hardware the
  daemon treated `awdl0` as degraded by the link, the route to record is the keypath chain of §6.2.
- Of point 10 of the index what remains open: the registration path of the `netif_llink` and the segment of the
  `ifclassq` up to the driver action (`AWDL_STAGE1_QSET_STATE.md` §10), which are re-evaluated separately.

## 8. Open points

| Point | Status |
|---|---|
| Final consumer of the `IFF_UP\|IFF_RUNNING` predicate of `sub_100056790` (type and decision after keypaths `0x100517ba0`/`0x10052cb58`) | `?` The referencers are Swift keypath data; chain not walked to the end |
| Purpose of the 6 calls to `notify_register_dispatch` | `?` No visible association to an interface |
| Destination of `setLinkEventWithIntegerReason:…` (object `qword_100548580`, initialized by `sub_100051680`) | `?` Class of the receiver not identified |
| Kernel readers of `state+113` (byte `+0x71` of the object at ivar `+0x120`) | `?` Not searched; inherits the `?` of `AWDL_STAGE1_IFUP_PATH.md` §2.1 |
| Segment of the `ifclassq` up to the driver action | `?` No jump to `tx_push` on that route; re-evaluated separately |
| Registration path of the `netif_llink` on the kext path | `?` Unchanged (`AWDL_STAGE1_QSET_STATE.md` §10) |

## 9. Address verification

Heads checked with `ida_funcs.get_func(ea).start_ea == ea` (kernelcache):
`0xffffff8000a95ab0` (`__ZN9IOService15errnoFromReturnEi`), `0xffffff8002aff4d1`
(`__ZL17nxp_queue_tx_pushP19kern_nexus_providerP10kern_nexusPvPyPjS5_`), `0xffffff80005e6110`,
`0xffffff80005e5f40`,
`0xffffff8002b2cb2a` (`packetSubmission`), `0xffffff80022f99ce`
(`__ZN23IO80211SkywalkInterface18setInterfaceEnableEb`), `0xffffff8002331ba6`
(`__ZN23IO80211VirtualInterface18setInterfaceEnableEb`).

Instruction citations within a verified head (kernelcache): `0xffffff8000a95ab4`-`0xffffff8000a95ae0`,
`0xffffff8000a95ae9`, `0xffffff8000a95af7`, `0xffffff8000a95b0c`, `0xffffff8000a95b20`,
`0xffffff8000a95b3c`, `0xffffff8000a95b5f`, `0xffffff8000a95b68` (jump table, read as data),
`0xffffff8002b2cb3f`, `0xffffff8002b2cb43`, `0xffffff8002b2cd5c`, `0xffffff8002b2cd62`,
`0xffffff8002aff506`, `0xffffff8002aff50c`-`0xffffff8002aff52a`, `0xffffff8002aff1c5`,
`0xffffff8002aff1cc`, `0xffffff8002aff1d9`, `0xffffff80005e614d`, `0xffffff80005e6158`,
`0xffffff80005e6172`, `0xffffff80005e6176`, `0xffffff80005e61a2`, `0xffffff80005e61a8`,
`0xffffff80005e6291`, `0xffffff80005e6008`, `0xffffff80022f99e9`, `0xffffff80022f99f0`,
`0xffffff80022f9a33`, `0xffffff80022f9a5c`, `0xffffff80022f9a68`, `0xffffff8002331bf0`,
`0xffffff8002331c5e`.

Vtable cells read as data: `0xffffff80002adc78` (`__ZTV9IOService`+0x798) and
`0xffffff8002b3da50` (`__ZTV25IOSkywalkNetworkInterface`+0x798), both `0xffffff8000a95ab0`.

Heads verified in wifip2pd: `sub_100043720`, `sub_10004D460`, `sub_1000506A0`, `sub_100056790`,
`sub_1000568D0`, `sub_100056970`, `sub_100056990`, `sub_100056B90`, `sub_100056D80`, `sub_100056FE0`,
`sub_1000576F0`, `sub_100057830`, `sub_100057B60`, `sub_10005F690`, `sub_10005F6B0`, `sub_10005F6D0`,
`sub_10005F730`, `sub_10005F790`, `sub_100068ED0`, `sub_100069090`, `sub_1001CEB40`, `sub_1001CFA90`,
`sub_1001D1930`, `sub_100286E10`, `sub_1003BB3E0`, `sub_10040EB90`, `sub_1004151A0`, `sub_10041B7F0`.

Data references cited: event table `0x100515ca0`; ivar `routingSocket` `0x100548d88`;
keypath `0x10045FE30`; thunk referencers `0x100517ba0`, `0x100517ba8`, `0x10052cb58`,
`0x10052cb60`; literals `0x1004a3620`, `0x1004a6a00`, `0x1004a6a90`, `0x1004a7900`, `0x1004a7930`,
`0x1004a7990`; `selRef` `0x100544060` (used at `0x1000512a0`, `0x100051356`).
