# Order of `setInterface` and of the `ifnet` birth in the `bsd_start` of `awdl0`

It closes the `?` of `AWDL_STAGE1_QUEUE_SHAPE.md` §9/§11 and point 11 of §8.2 of
`AWDL_INDICE.md`: the order between `setInterface` (the BSD client link) and the
`ifnet` creation in the BSD client's boot, and the consumer of the 22 (`EINVAL`) return of the
rejection by `num_txqs == 0`. Base: kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), IDA headless
(`idat -A -S`) over a copy of `boot_awdl.kc.i64` in `Build/ida_awdl/task_bsdstart/`. All
addresses are VAs. Each claim was read in the disassembly; anything unseen carries `?`.

## 1. Answer

**(a) `setInterface` happens inside `registerEthernetInterface`, before the `registerService` and
therefore before the `ifnet`.** `IOSkywalkLogicalLink::setInterface(link, iface)` executes at
`0xffffff8002b0a38d`, inside `IOSkywalkNetworkInterface::registerNetworkInterface` (variant with
logical link). The `ifnet` is born afterward, inside `IOSkywalkNetworkBSDClient::start`
(`kern_nexus_controller_alloc_net_provider_instance` at `0xffffff8002affbb5`), reached only
via the IOKit matching triggered by the driver's `registerService(2)`
(`0xffffff800172982a` on the Broadcom path; `kIOServiceSynchronous = 2` per
`Kernel.framework/Headers/IOKit/IOService.h:100` of the SDK).

**(b) The 22 of the rejection by `num_txqs == 0` never reaches whoever calls
`registerEthernetInterface`.** There are two paths and in neither does it arrive as a registration code:

| Path | Where it is validated | Consumer of the 22 | What the driver sees |
|---|---|---|---|
| In-kernel registration (Broadcom and stage 1) | `nx_netif_default_llink_config` (`0xffffff80009faf35`) called from `kern_nexus_controller_alloc_net_provider_instance` (`0xffffff80009a4310`), inside the BSD client's `start` | `kern_nexus_controller_alloc_net_provider_instance` itself returns it (`0xffffff80009a436a`) and `IOSkywalkNetworkBSDClient::start` aborts with any non-null value (`0xffffff8002affbd3`) | `registerEthernetInterface` already returned 0; the symptom is that there is no `ifnet` nor BSD name |
| Message `0xE0060105` (DriverKit only) | `kern_nexus_netif_llink_add` (`0xffffff80009e6397`) from `IOSkywalkNetworkBSDClient::message` (`0xffffff8002b00d69`) | `message` replaces it with `0xE00002BC` (`0xffffff8002b00d6e`); `addLogicalLink` does not even read that return: it fails on `containsObject` false and returns `0xE00002BC` (`0xffffff8002b0a89a`) | `IOUserNetworkWLAN::_AddLogicalLink_Impl` (`0xffffff8002b07ce8`) hands it to the Dext |

The message path takes no part in in-kernel registration: the only emitter of `0xE0060105` is
`IOSkywalkNetworkInterface::addLogicalLink` and its only caller is the DriverKit thunk. In
role 6 registration, the link information reaches the netif inside the provider's own alloc,
not through a message.

## 2. Full sequence, step by step

### 2.1 Driver side (Broadcom reference; stage 1 replicates this order)

| VA | Instruction / call | Effect |
|---|---|---|
| `0xffffff800172973d` | `call IOSkywalkEthernetInterface::registerEthernetInterface` | site in `AppleBCMWLANProximityInterface::start` (`0xffffff8001729376`); the queue array comes from `[rbp+__dst]` |
| `0xffffff8001729742`–`0xffffff800172974b` | `mov r12d,eax` / `test r12d,r12d` / `setz r15b` | Broadcom's `start` return boolean is "the registration returned 0" |
| `0xffffff8001729752` | `jz loc_1729825` | only with successful registration does it continue; on failure the queues are freed through slot `+0x148` (`0xffffff8001729784`–`0xffffff8001729820`) and "registerEthernetInterface re…" is logged with the error (`0xffffff8001729874`–`0xffffff800172989b`) |
| `0xffffff800172982a` | `mov esi,2; call [rax+5B0h]` | `IOService::registerService(kIOServiceSynchronous)`; cell `+0x5B0` is `IOService::registerService(uint)` (`0xffffff8000a93250`) in the three involved vtables (`__ZTV30AppleBCMWLANProximityInterface`, `__ZTV25IOSkywalkNetworkInterface`, `__ZTV25IOSkywalkNetworkBSDClient`); its return is not checked |

The registration result (0 or error) is the only thing deciding that boolean: a later
BSD client failure does not touch it.

### 2.2 Registration: `registerEthernetInterface` → `registerNetworkInterface`

| VA | Instruction / call | Effect |
|---|---|---|
| `0xffffff8002b10aa5` | `call copyRegistrationInfo` | validates version 1 / size `0x130` and publishes `IOMACAddress`; failure ⇒ `deregisterEthernetInterface(0)` and direct return (`0xffffff8002b10ab3`–`0xffffff8002b10ab5`) |
| `0xffffff8002b10adf` | `call registerNetworkInterface` (variant with queues) | the `RegistrationInfo` pushed down is `*(*(iface+0x118))` (`0xffffff8002b10abe`–`0xffffff8002b10ac5`), the copy `copyRegistrationInfo` left in container `+0x118` (`AWDL_STAGE1_REGINFO_BROADCOM.md`) |
| `0xffffff8002b0a129`–`0xffffff8002b0a12f` | `isWMMSchedModelCapable` | information byte `3` (`0xffffff8002b0a11d`) or `11` (`0xffffff8002b0a138`) |
| `0xffffff8002b0a14f` | `IOSkywalkQueueSet::createWithQueues` | null ⇒ `0xE00002C2` (`0xffffff8002b0a1bc`) |
| `0xffffff8002b0a176` | `IOSkywalkLogicalLink::createWithQueueSet` | null ⇒ `0xE00002C2` (`0xffffff8002b0a1c3`) |
| `0xffffff8002b0a196` | `call registerNetworkInterface` (variant with link) | the set and the link lose their creator reference on return (`0xffffff8002b0a1a9`, `0xffffff8002b0a1d4`) |

Variant with link (`0xffffff8002b0a204`–`0xffffff8002b0a430`); `getLogicalLinkNexusInfo` at
`0xffffff8002b0a229`:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002b0a23c`–`0xffffff8002b0a279` | `cmp [r12+4],108h` / `cmp [r12],1` / `test r13,r13` / `cmp [r12+14h],400h` / `or rax,rdx` | validation of the `RegInfo` and the pools; failure ⇒ `0xE00002C2` |
| `0xffffff8002b0a286`–`0xffffff8002b0a2a9` | `IOMallocTypeImpl` / `memmove 108h` | new block of `0x108` bytes with the `RegInfo` copy; the pointer is saved in the cell referred by `[iface+0xC0]` (`0xffffff8002b0a292`: `mov rcx,[r14+0C0h]; mov [rcx],rax`); without block ⇒ `0xE00002BD` (`0xffffff8002b0a419`) |
| `0xffffff8002b0a2b4` and `0xffffff8002b0a2e7` | cell `+0x9E8` → property `IOInterfaceNamePrefix` | BSD prefix ("awdl" for role 6) |
| `0xffffff8002b0a2cd` and `0xffffff8002b0a301` | cell `+0x9F0` → property `IOInterfaceUnit` | unit of `RegInfo+0x38`, only if ≥ 0 |
| `0xffffff8002b0a34a`–`0xffffff8002b0a375` | loop over the link's qsets (`getQueueSet`/`getQueueSetNexusInfo`), `cmp r14b,1` | requires exactly one set with bit 0 of the information byte |
| `0xffffff8002b0a37e` | `IOSkywalkLogicalLink::setDefaultLogicalLink` | marks the link as default |
| `0xffffff8002b0a38a`–`0xffffff8002b0a38d` | `IOSkywalkLogicalLink::setInterface(link, iface)` | **the BSD client's link is bound to the interface here** |
| `0xffffff8002b0a39d`–`0xffffff8002b0a3b2` | `OSOrderedSet::withCapacity(1)` at `[iface+0xC8]` and `setFirstObject(link)` (cell `+0x1B0` of `__ZTV12OSOrderedSet`, `0xffffff800025b750`) | the interface saves the link |
| `0xffffff8002b0a3d6`–`0xffffff8002b0a3f5` | `[iface+0x98]` / `[iface+0xA0]` with retain (cell `+0x20`) | the registration pools stay in the interface |
| `0xffffff8002b0a392` / `0xffffff8002b0a41e` | `xor ebx,ebx` / `mov eax,ebx` | **return 0; the function sends no message or touches the nexus** |

`IOSkywalkLogicalLink::setInterface` (`0xffffff8002b0c6d6`) saves the pointer at `[link+0x18]`
(`0xffffff8002b0c6e4`) and transfers the ownership of each queue of each qset to the pool: with a non-null
interface `checkInPacketQueue` (`0xffffff8002b0c742`) and `setOwnerId(getRegistryEntryID())`
(`0xffffff8002b0c74a`–`0xffffff8002b0c75e`); with null, `checkOutPacketQueue` and `setOwnerId(0)`
(`0xffffff8002b0c754`–`0xffffff8002b0c759`).

### 2.3 Emitters of the message `0xE00601xx` (byte sweep over all segments)

| Value | Emitter | VA of the site |
|---|---|---|
| `0xE0060105` (add link) | `IOSkywalkNetworkInterface::addLogicalLink` | `0xffffff8002b0a879` (`mov esi, 0E0060105h`) |
| `0xE0060106` (remove link) | `IOSkywalkNetworkInterface::removeLogicalLink` | `0xffffff8002b0a7ad` |
| `0xE0060100` | `IOSkywalkNetworkInterface::reportLinkStatus` | `0xffffff8002b0b521` (`lea r14d, ds:0FFFFFFFFE0060100h[rdx*2]`) |
| `0xE0060102` | `IOSkywalkNetworkInterface::reportEventType` | `0xffffff8002b0a8d7` (`cmp eax, 0E0060102h`) |
| `0xE0060104` | `IOSkywalkNetworkInterface::setAggressiveness` | `0xffffff8002b0b941` |
| `0xE0060107` / `0xE0060108` | `addSteeringRule` / `removeSteeringRule` | `0xffffff8002b0aa12` / `0xffffff8002b0a66e` |

No function of the registration (`registerEthernetInterface`, the two variants of
`registerNetworkInterface`) appears in the list: in-kernel registration does not send `0xE0060105`.

### 2.4 `bsd_start`: `IOSkywalkNetworkBSDClient::start` (`0xffffff8002aff562`)

IOKit matching invokes it with the interface as provider; the first relevant instruction
chains the base and the rest prepares the ground for the `ifnet`:

| VA | Instruction / call | Effect |
|---|---|---|
| `0xffffff8002aff5c1`–`0xffffff8002aff5ce` | `lea rax, __ZTV18IOSkywalkBSDClient; call [rax+5D0h]; test al,al` | call to the `start` cell of the base vtable (`ZTV+0x5D0` = vptr `+0x5C0`, the same position `IOSkywalkNetworkBSDClient::start` occupies in its own vtable); the base's implementation is `IOSkywalkBSDClient::start` (`0xffffff8002afd3ce`, symtab symbol); false ⇒ failure |
| `0xffffff8002aff689`–`0xffffff8002aff6a2` | `safeMetaCast(provider, IOSkywalkNetworkInterface::metaClass)` → `[client+0xE8]` | the nub's provider is the registered interface |
| `0xffffff8002aff6ae`–`0xffffff8002aff6df` | property `IODeferBSDAttach` and `cmp r15,r12` with the true boolean | if it equals true, the client refuses to boot (jumps to the close `0xffffff8002aff8dc`) |
| `0xffffff8002aff6ef`–`0xffffff8002aff739` | property `IOInterfaceUnit` → `[rcx+148h]` | numeric unit |
| `0xffffff8002aff7c7` | `openInterface(iface)` (`0xffffff8002b00252`) | `iface->open(client, 0, 0)` through cell `+0x5D0` (`0xffffff8002b0026f`) |
| `0xffffff8002aff7d4`–`0xffffff8002aff809` | cell `+0x9E8` (prefix) + `snprintf("%s%u", prefix, unit)` | **the BSD name ("awdl0") is composed before the `ifnet` exists** |
| `0xffffff8002aff83f` / `0xffffff8002aff86b` | `setProperty` "BSD Name" / "IOInterfaceName" | the name is published on the interface |
| `0xffffff8002aff884`–`0xffffff8002aff8ce` | `[iface+0xC8]` → `OSOrderedSet::getObject(0)` (cell `+0x1F8`) → cast to `IOSkywalkLogicalLink` → qset loop | requires bit 0 of the default set (`0xffffff8002aff8c4`) and reads the WMM bit (`0xffffff8002aff92b`) |
| `0xffffff8002aff939`–`0xffffff8002aff9b9` | `[iface+0x98]` / `[iface+0xA0]` (`+0xB8`) | coherence of the pools; two nulls or contradictory bits ⇒ failure |
| `0xffffff8002aff9f9` | `registerNexusControllerProvider` | false ⇒ failure |
| `0xffffff8002affa0c`–`0xffffff8002affab7` | `ifnet` parameters on the stack (`var_128`) | `+0x18` = `prepareNexusCallback` (`0xffffff8002affb47`); `+0x20`/`+0x28` = `getTxPoolHandle`/`getRxPoolHandle` (`0xffffff8002affb55`, `0xffffff8002affb68`); `+0x30` = `getLogicalLinkNexusInfo(link)` (`0xffffff8002affb71`–`0xffffff8002affb79`); `if_output`/`if_ioctl`/`if_detach` (`0xffffff8002affa4d`–`0xffffff8002affa6d`) |
| `0xffffff8002affbb5` | `call kern_nexus_controller_alloc_net_provider_instance` | **birth of the `ifnet`**; retry ×50 with `IOSleep(100)` if it returns 16 (`0xffffff8002affbba`–`0xffffff8002affbcc`) |
| `0xffffff8002affbd3`–`0xffffff8002affbd5` | `test eax,eax; jnz loc_2affc82` | any error of the alloc (including the 22) ⇒ `0xffffff8002affc82`: `[client+0xF8] = 0`, `closeInterface` (`0xffffff8002aff8eb`), `stop` (cell `+0x5C8`, `0xffffff8002aff8f9`) and false return (`0xffffff8002aff8ff`) |
| `0xffffff8002affbdf`–`0xffffff8002affbfa` | `uuid_unparse_upper` → property `IOSkywalkNexusUUID` | only on success |
| `0xffffff8002affcac` / `0xffffff8002affcb9` | `startIOReporting` / `call [rax+5B0h]` with `esi=0` | reporting and `registerService(kIOServiceAsynchronous)` of the client itself; true return (`0xffffff8002affcbf`) |

### 2.5 Inside the alloc: link validation and netif creation

`kern_nexus_controller_alloc_net_provider_instance` (`0xffffff80009a4140`) consumes field `+0x30`
as `llink_init` (`mov rsi,[r15+30h]`), the same offset where `start` wrote the result of
`getLogicalLinkNexusInfo` at `RingVars+0x30`:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff80009a4300`–`0xffffff80009a4307` | `mov rsi,[r15+30h]` / `test rsi,rsi` | the link's `llink_init`; null ⇒ log "…logical link configuratio…" and `0x16` (22) (`0xffffff80009a4321`–`0xffffff80009a4352`) |
| `0xffffff80009a4310` | `call nx_netif_default_llink_config(netif, llink_init)` | default link configuration |
| `0xffffff80009a4315`–`0xffffff80009a431a` | `mov var_2C,eax` / `test eax,eax; jnz` | the error is kept |
| `0xffffff80009a436a` / `0xffffff80009a4388`–`0xffffff80009a4390` | `mov r12d,[var_2C]` / `nx_close(nx,1)` | **the function returns exactly the validator's errno (22)** and closes the nx |

`nx_netif_default_llink_config` (`0xffffff80009faf20`) validates before copying:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff80009faf2d`–`0xffffff80009faf3c` | `mov esi,1` / `call nx_netif_validate_llink_config` / `jnz` | **here the check table of `AWDL_STAGE1_QUEUE_SHAPE.md` §5 applies**; the 22 exits untouched (`0xffffff80009fb08f`) |
| `0xffffff80009faf74` | `[netif+6F8h] = kalloc(kern_nexus_netif_llink_init)` | copy of the `llink_init` |
| `0xffffff80009faf9d`–`0xffffff80009fb007` | `kalloc_ext` + loop | copy of the qset array to `[rcx+18h]`, "not added" mark at `[rcx+4]` |
| `0xffffff80009fb00b`–`0xffffff80009fb07b` | log "failed to alloc kern_nexu…" | out of memory ⇒ `0xC` |

The netif is created by the nexus provider's machine: the only caller of `nx_netif_alloc`
(`0xffffff80009dff20`) is the provider constructor `nx_netif_prov_nx_ctor`
(`0xffffff80009defd6`; single xref checked). `?` the intermediate segment between the provider's
alloc and that constructor has not been followed instruction by instruction. The already created
`ifnet` returns to the client through the callback `start` registered at `RingVars+0x18`:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002b00350`–`0xffffff8002b00358` | `kern_nexus_get_context()` / `[ctx+100h] = nexus` | the context is the client (third argument of the alloc, `0xffffff8002affba6`); the handler ends up where `message` reads it (`[rbx+100h]` at `0xffffff8002b00d54`) |
| `0xffffff8002b0035f`–`0xffffff8002b0037b` | `runAction(gatedPrepareNexus, ifnet)` on the work loop | delivery under lock |
| `0xffffff8002b0067c`–`0xffffff8002b00688` | `iface->prepareBSDInterface(ifnet, 0)` (cell `+0x8E8`) | non-null ⇒ abort |
| `0xffffff8002b006a6` | `mov [rbx+0F8h], r15` | **the `__ifnet*` lands in the BSD client** |
| `0xffffff8002b006b7` / `0xffffff8002b006ca` | `getPacketTapInfo` (cell `+0x950`) / `bpfattach` | the `ifnet` enters BPF; `[client+146h] = 1` (`0xffffff8002b006cf`) |

## 3. The return 22: full chain on each path

### 3.1 In-kernel registration path (that of `awdl0` and `en2`)

```
nx_netif_validate_llink_config (0xffffff80009fad40)  → 22
nx_netif_default_llink_config      (0xffffff80009faf35)  → 22 untouched
kern_nexus_controller_alloc_net_provider_instance (0xffffff80009a4310→0x9a436a) → 22
IOSkywalkNetworkBSDClient::start   (0xffffff8002affbd3)  → failure: [client+0xF8]=0,
                                     closeInterface + stop, false return
```

Final consumer: the BSD client's own `start`, which distinguishes only "zero / non-zero". The 22 is
neither transformed nor propagated as `IOReturn`. At this point the driver already received 0 from
`registerEthernetInterface` and its `registerService(kIOServiceSynchronous)` already returned; the only
thing revealing the rejection is the absence of a BSD name (`getBSDName` empty), which stage 1 turns into
failure of `startWithRegistration` (`Source/AirLayer/AWDLInterface.cpp:218`–`:223`). Broadcom
never gets to check it: its `start` returns the registration result (`setz` at
`0xffffff800172974b`).

### 3.2 Message path `0xE0060105` (DriverKit only)

```
addLogicalLink                (0xffffff8002b0a879)  messageClients(0xE0060105, link, 8)
  └ IOService::messageClients (0xffffff8000a95630)  applyToInterested(gIOGeneralInterest, …)
      └ applier               (0xffffff8000a963c0)  messageClient(type, destination, link, 8);
                                                    saves the last non-null return (0xa963e7)
          └ IOService::messageClient (0xffffff8000a953d3–0xa953f7)
                destination->message(type, sender, link)  [the link travels in rcx]
                └ IOSkywalkNetworkBSDClient::message case 0xE0060105 (0xffffff8002b00d4b)
                    kern_nexus_netif_llink_add (0xffffff8002b00d69) → 22
                    ⇒ 0xffffff8002b00d6e: the 22 is replaced by 0xE00002BC
addLogicalLink ignores the message's return (0xffffff8002b0a881 without test) and decides by
containsObject (0xffffff8002b0a894–0xffffff8002b0a8a2): the handler failed before
setLastObject (0xffffff8002b00d90), so it returns 0xE00002BC (0xffffff8002b0a89a)
  └ IOUserNetworkWLAN::_AddLogicalLink_Impl (0xffffff8002b07ce8, jmp) → to the Dext
```

Citable details of the mechanism: `messageClients` calls through cell `+0x760`
(`IOService::applyToInterested`), saves the type/link/options in the context
(`0xffffff8000a95648`–`0xffffff8000a95653`) and returns the last non-null
(`0xffffff8000a95672`); `messageClient` checks the destination is an `IOService`
(`0xffffff8000a95396`–`0xffffff8000a953ac`) and chains `message` with the link as third
argument (`0xffffff8000a953e6`: `mov rcx, r15`). In the handler the link arrives in `rcx`
(`mov r15, rcx` at `0xffffff8002b00bb3`); with rcx null it answers `0xE00002C2`
(`0xffffff8002b00dee`). On success the handler adds the link to the interface's set with
`setLastObject` (cell `+0x1B8` of `OSOrderedSet`, `0xffffff8002b00d90`).

The second caller of `kern_nexus_netif_llink_add` is at `0xffffff80005fe2d9`, BSD region of
the kernel between `_loopattach` (`0xffffff80005f82e0`) and `_rn_refines` (`0xffffff8000612860`), without
a defined function in the IDB, with the request taken from the stack and in a loop
(`0xffffff80005fe2b0`–`0xffffff80005fe2ec`): the native/flowswitch path, unrelated to the
IOSkywalk family. `?` what exact function it is.

## 4. Corrections this walk introduces

- The `?` of `AWDL_STAGE1_QUEUE_SHAPE.md` §9 ("it travels as 22 to `kern_nexus_netif_llink_add` and from there
  to `IOSkywalkNetworkBSDClient::message`") only describes the DriverKit path. In in-kernel
  registration the 22 passes neither through `kern_nexus_netif_llink_add` nor through `message`: the provider's
  alloc produces and consumes it inside the BSD client's `start`.
- The open point's `setInterface` is not the message `0xE0060105`: it is the direct call of
  `0xffffff8002b0a38d` (registration) or of `0xffffff8002b0a8aa` (`addLogicalLink` after a successful
  nexus registration). The message's handler only invokes it with null, on removal
  (`0xffffff8002b00dcd`).
- The counting conclusion of `AWDL_STAGE1_QUEUE_SHAPE.md` §1 and §5 still holds: the validator is
  the same (`nx_netif_validate_llink_config`); what changes is who invokes it (the provider's
  alloc on the kernel path) and what it returns in each case.
- The BSD name is composed and published before the `ifnet` (`0xffffff8002aff7d4`–`0xffffff8002aff86b`
  versus `0xffffff8002affbb5`); the property's existence does not imply a live `ifnet`: the later
  alloc failure leaves everything without `ifnet` (`[client+0xF8] = 0`).

## 5. Application to stage 1

- The code order (`AWDLInterface.cpp:206` registration → `:215`
  `registerService(kIOServiceSynchronous)` → `:218` name read) matches instruction by instruction the
  Broadcom path: `setInterface` already happened inside the registration; the
  `ifnet` is born during the synchronous `registerService`.
- If T3 shows registration with return 0 and no `awdl0`, the validator's rejection in the alloc is a
  possible result and it is distinguished in the kernel log: the alloc logs
  "…logical link configuratio…" (`0xffffff80009a4338`) with function name
  `kern_nexus_controller_alloc_net_provider_instance`, and `kern_nexus_netif_llink_add` logs
  "Invalid llink init params" (`0xffffff80009e63b7`) — the latter only on the DriverKit path. The
  validator's strings ("num_txqs == 0" and friends) are those of `AWDL_STAGE1_QUEUE_SHAPE.md` §5.
- The rejection does not corrupt state: the alloc closes the nx (`nx_close`, `0xffffff80009a4390`) and the
  client's `start` calls its `stop`; a later registration retry goes through the
  same full path again.
- No new ordering risks: the BSD part runs on the synchronous `registerService`'s thread
  (SDK semantics, `IOService.h:595`), the same as `AWDL_INTERFACE_BIRTH.md` §2.3–§2.4 assumed.

## 6. Open points

| Point | State |
|---|---|
| Identity of the function covering `0xffffff80005fe2d9` (second caller of `kern_nexus_netif_llink_add`, native/flowswitch region) | `?` unnamed in the IDB and without a symbol in the dump; unrelated to the IOSkywalk family |
| Whether the strings of `nx_netif_validate_llink_config` reach a log observable by `log show` on the alloc path (the alloc and `llink_add` do have their own os_log) | `?` confirmed in T3 with the kernel ring |
| Exact IOKit mechanism pairing the `IOSkywalkNetworkBSDClient` nub with the registered interface (the family's personalities) | covered by `AWDL_INTERFACE_BIRTH.md` §2.3–§2.4; here only the `start` provider being the interface is verified (`0xffffff8002aff689`) |
