# Stage 2: how the driver pushes the send queue of `awdl0`

Static design of the send queue push for stage 2. Closes the `?`s of
`Docs/AWDL_STAGE1_STACK_TX_ENTRY.md`: the value of `ifp+0x78` and its effect on `if_qflush`, the
order between the
enqueue, the start thread and a pull from the driver, and what the IO80211 family does with its
`if_output`. It also corrects step 1 of the design, which used symbols not linkable from a kext, and
replaces it with the two virtual methods the family already invokes. Everything was read in IDA
headless over
copies of `boot_awdl.kc.i64`; nothing has been linked, loaded or run.

## 1. Answer

`ifp+0x78` holds **1** on a freshly created `ifnet`: `ifnet_allocate_extended` writes it at
`0xFFFFFF800063FE18` (`mov dword ptr [r13+78h], 1`), right after initializing the `ifclassq`
(`0xFFFFFF800063FE11`, `[ifp+0x338]`). No other writer was found in the sweep of the functions of
the
`ifnet` in `0x5E8000`–`0x5F0000` and `0x63F000`–`0x641000` (includes `ifnet_attach`, `ifnet_set_flags`
and
`ifnet_allocate_extended`). `if_qflush` tests
`byte [ifp+0x78], 7` (`0xFFFFFF80005D5750`–`0xFFFFFF80005D5755`): with 1, the test is non-zero and it
**purges**
(`0xFFFFFF80005D5796`). The purge of §7 of `AWDL_STAGE1_TXPUSH_LLINK.md` happens, with an added
condition: `if_qflush` only calls the
purge operation if `byte [ifclassq+0x4C] & 2` is active (`0xFFFFFF80005D5786`–`0xFFFFFF80005D5796`);
with the
`ifclassq` enabled (`IFEF_TXSTART`) it holds. `?` The runtime value of that bit is confirmed on
hardware.

The order does not allow a safe pull from the driver. `ifnet_enqueue` enqueues and wakes the start
thread (`0xFFFFFF80005E6284`) in the same call; the thread runs on another kernel thread
(`sub_FFFFFF80005EA850`, created at `0x5E9693`) and executes `_ifp_if_start` → `if_qflush`. There is
no barrier between the return of `ifnet_enqueue` and the purge: whoever wins is the scheduler's
decision. `?` The
disassembly does not fix the order; only the hardware decides it.

The path that avoids the `ifclassq` is not the `output` symbols: `ifnet_set_output_handler`
(`0xFFFFFF80005EF540`), `ifnet_reset_output_handler` (`0xFFFFFF80005EF560`),
`dlil_set_output_handler` (`0xFFFFFF80005EF4E0`) and `dlil_reset_output_handler`
(`0xFFFFFF80005EF510`)
exist in the kernel table, but do not appear in any of the nine KPI lists of
`__LINKINFO,__symbolsets` and no kext of the collection imports them, so they do not link from a
kext (`Docs/AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`). The usable path is two virtual methods that
`IOSkywalkNetworkBSDClient::start` already invokes on the interface object:

| Method | Cell | Call | Effect |
|---|---|---|---|
| `initBSDInterfaceParameters` | `0x8E0` | `0xFFFFFF8002AFFADC` | Writes `params+0x40`; the registration copies it to `ifp+0x260` (`0x63F890`) and then to `ifp+0x258` (`0x63FC30`) when setting `IFEF_TXSTART` (`0x63FC0A`). |
| `prepareBSDInterface` | `0x8E8` | `0xFFFFFF8002B00688` | Receives the already created and attached `ifnet`; allows `cmpxchg` at `ifp+0x258` against `[ifp+0x5A0]`, where the registration stores the installed value (`0x63FDA5`). |

The first is the main path: it leaves the `output` installed from the registration and the `ifnet`
complete when the BSD client sees it. The second is the alternative: it arrives after `_ifnet_attach`
(`0x9A42DF`) and before
`bpfattach` (`0x2B006CA`), and does not depend on the layout of `ifnet_init_eparams`. Neither needs a
kernel symbol.

The IO80211 family does not call `packetSubmission` on its own: there is no direct call to
`IOSkywalkPacketQueue::packetSubmission(bool)` (`0xFFFFFF8002B2CD68`) in the whole kernel; the
function only
appears as data in three virtual tables (`0x15ACED8` of
`AppleBCMWLANPCIeSkywalkTxSubmissionQueue`, `0x17E13B8` of `AppleBCMWLANSkywalkTxSubmissionQueue`
and
`0x2B3E250` of `IOSkywalkTxSubmissionQueue`). The real push is the generic machinery's: `enable`,
`checkForWork` and `nxp_tx_qset_notify`. The `output` the BSD client registers in
`ifnet_init_eparams` (`IOSkywalkNetworkBSDClient::if_output`, `0xFFFFFF8002B002CE`) drops the mbuf and
returns `0x13`; it ends at `ifp+0x258` (`0x63F882`) and `ifnet_allocate_extended` replaces it with
`_ifnet_enqueue` when setting `IFEF_TXSTART` (`0x63FC16`–`0x63FC30`), so for `awdl0` nothing stays
installed.
With field `+0x40` written from cell `0x8E0`, that replacement takes its own value instead of
`_ifnet_enqueue` (`0x63FC16`–`0x63FC29`).

## 2. `ifp+0x78`: value and effect

| Instruction | Fact |
|---|---|
| `0xFFFFFF800063FE0A`–`0xFFFFFF800063FE11` | `ifnet_allocate_extended`: `rax = [ifp+0x338]` (the `ifclassq`); `[rax+0x5C] = 0x800`. |
| `0xFFFFFF800063FE18` | `[ifp+0x78] = 1`. |
| `0xFFFFFF80005D5750`–`0xFFFFFF80005D5755` | `if_qflush`: `test byte [ifp+0x78], 7`; if zero, returns without purging. |
| `0xFFFFFF80005D5796` | `if_qflush`: `call qword ptr [rax+30h]` with `rax = [ifq+0x68]`; purge. |
| `0xFFFFFF80005F3352` | `dlil_output`: compares `dword [ifp+0x78]` with `0x0A`; with 1 it does not take that path. |

The field holds 1 and its three low bits are `001`; the check of `if_qflush` uses only those three
bits, so the purge executes. `ifp+0x78` corresponds to `if_refflags` (value 1 = embryonic at
registration; the registration at `ifnet_attach` moves it to
«attached»): in both states the three low bits are non-zero, so the test result does not change. `?`
The concrete writer
at `ifnet_attach` was not located. Also `?` the field name is not fixed by the disassembly: it is
written once
at registration and compared with `0x0A` in `dlil_output`; what matters for this decision is the
value 1 and the
mask `& 7`.

## 3. The order between enqueueing, the start thread and the pull

| Step | Instruction | Fact |
|---|---|---|
| 1 | `0xFFFFFF80005E6008` | `sub_5E5F40` calls `sub_5E6110` (enqueue). |
| 2 | `0xFFFFFF80005E6172` | `fq_if_enqueue` leaves the packet in the `ifclassq`. |
| 3 | `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284` | Same thread: `inc [ifp+0x304]` and wakes `&ifp+0x330`. |
| 4 | `0xFFFFFF80005EAA18`, `0x5EAA3A` | Another thread: `assert_wait(&ifp+0x330, 0)` and `thread_block_parameter`. |
| 5 | `0xFFFFFF80005EB1F7` | Thread loop: `call [ifp+0x250]`. |
| 6 | `0xFFFFFF80005E3CFC` | `_ifp_if_start` → `if_qflush` → purge. |

There is no synchronization guaranteeing a `packetSubmission` call from the driver happens
before step 6: step 3 only wakes, and step 4 runs when the scheduler gives it CPU. `?`
Therefore the order is indeterminate and does not serve as a design. `packetSubmission` neither
takes the packet out of the
`ifclassq`: what it does is fill the Skywalk queue ring with `kern_netif_queue_tx_dequeue`
(`0x2B0D41F`), and `if_qflush` purges the `ifclassq` on its own.

## 4. Paths to avoid the `ifclassq`

| Path | Entry point and instructions | Effect |
|---|---|---|
| A. Field `+0x40` of `ifnet_init_eparams` (main) | `initBSDInterfaceParameters`, cell `0x8E0`, called at `0xFFFFFF8002AFFADC`; the registration reads the field at `0x63F4A6`, copies it to `ifp+0x260` at `0x63F890` and to `ifp+0x258` at `0x63FC30` | `_dlil_output_handler` jumps to its own function (`0x5EF50C`); the packet never enters the `ifclassq`. |
| B. Write to `ifp+0x258` (alternative) | `prepareBSDInterface`, cell `0x8E8`, called at `0xFFFFFF8002B00688`, after `_ifnet_attach` (`0x9A42DF`) and before `bpfattach` (`0x2B006CA`) | The same jump, with a `cmpxchg` against `[ifp+0x5A0]` (`0x63FDA5`). |
| C. `ifnet_set_output_handler` / `dlil_set_output_handler` | `0xFFFFFF80005EF540` / `0xFFFFFF80005EF4E0` | Discarded: not in the KPI lists and no kext imports them; they do not link (`Docs/AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`). |
| D. `IFEF` flags | `0xFFFFFF800063FC0A` sets `0x80` (`IFEF_TXSTART`) | Discarded: there is no `if_eflags` setter for the driver and the registration already sets it upon seeing `if_start` non-null. |

Field `+0x38` of `ifnet_init_eparams` does not work: `IOSkywalkNetworkBSDClient::start` fills it
with
`IOSkywalkNetworkBSDClient::if_output` (`0x2AFFA4D`–`0x2AFFA54`) and the registration replaces it with
`ifp+0x260` at
`0x63FC30` because `IFEF_TXSTART` is active. The surviving field is `+0x40`. `?` The disassembly
fixes the `+0x40` displacement, not the field name in `ifnet_init_eparams`.

The block `0x63FC00`–`0x63FC30` only runs with `ifp+0x250` non-null. The nexus requires
`params+0x48 == 0`
(`0x9A41F4`), so the registration installs `_ifp_if_start` at `ifp+0x250` (`0x63F89D`) and the block
always runs;
it does not depend on any field the driver could leave at zero.

In path A, the value left at `ifp+0x5A0` is its own function, not `_ifnet_enqueue`: `0x63FDA5`
copies
`[ifp+0x258]` after the replacement. That is why path B must not compare against `_ifnet_enqueue`
but
against `[ifp+0x5A0]`, which is what `ifnet_set_output_handler` does at
`0x5EF544`–`0x5EF54D`. The two paths
are alternatives; applied together, B rewrites the same pointer and changes nothing.

## 5. What the IO80211 family does

| Fact | Evidence |
|---|---|
| `packetSubmission(bool)` has no direct calls: it only appears as data in three virtual tables | `0x2B2CD68` without code xrefs; `0x15ACED8`, `0x17E13B8`, `0x2B3E250` |
| The calls are virtual, through `vtable+0x1D8` | `enable` `0x2B0CCB2`; `checkForWork` `0x2B2C82A`; `notify` `0x9E24F8` |
| `IOSkywalkNetworkBSDClient::if_output` drops the mbuf and returns `0x13` | `0x2B002CE`: `mbuf_freem` (`0x2B002DA`), `eax = 0x13` (`0x2B002E0`) |
| That `if_output` is registered at `ifnet_init_eparams+0x38` | `0x2AFFA4D`–`0x2AFFA54` (`[r13+0x30]`, with `r13 = params+8`) |
| `ifnet_allocate_extended` replaces that `ifp+0x258` with `_ifnet_enqueue` when `IFEF_TXSTART` is present | `0xFFFFFF800063F882` (`[ifp+0x258] = params+0x38`), `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30` |
| The provider installs `nx_netif_na_txsync` as callback | data reference at `0xFFFFFF80009E0E9F`; notifies at `0xFFFFFF80009E24F8` |

Cells `0x8E0`/`0x8E8` of the involved virtual tables (read at `__ZTV` + `0x8F0` / `+0x8F8`):

| Class | Cell `0x8E0` | Cell `0x8E8` |
|---|---|---|
| `IO80211AWDLProtocol` (`0xFFFFFF8002456440`) | `IO80211SkywalkInterface::initBSDInterfaceParameters` (`0x22F635C`) | `IO80211VirtualInterface::prepareBSDInterface` (`0x2331ADA`) |
| `AppleBCMWLANProximityInterface` (`0xFFFFFF80017F5B68`) | `0x22F635C` | `0x2331ADA` |
| `AppleBCMWLANSkywalkInterface` (`0xFFFFFF80017E2628`) and `AppleBCMWLANInfraProtocol` (`0xFFFFFF80017DECA8`) | `IO80211InfraInterface::initBSDInterfaceParameters` (`0x235FF5A`) | `IO80211InfraInterface::prepareBSDInterface` (`0x235FF64`) |
| `AppleBCMWLANLowLatencyInterface` (`0xFFFFFF80017E4490`) | `AppleBCMWLANLowLatencyInterface::initBSDInterfaceParameters` (`0x15E034A`) | `0x235FF64` |
| `IOSkywalkEthernetInterface` (`0xFFFFFF8002B3F178`) | `IOSkywalkEthernetInterface::initBSDInterfaceParameters` (`0x2B10BCA`) | `IOSkywalkEthernetInterface::prepareBSDInterface` (`0x2B10E58`) |

None of them writes `params+0x38` nor `params+0x40`, nor `ifp+0x258`. `IO80211SkywalkInterface` only
adds `[params+0xB] |= 0x40` (`0x22F6390`) over the base body; `IO80211InfraInterface` delegates to
that
same body and returns its result (`0x235FF5A`), and its `prepareBSDInterface` (`0x235FF64`) adds
`bpfAttach`, static properties and MTU, without touching the `output`;
`AppleBCMWLANLowLatencyInterface` writes `params+0x10` and
`params+0x18` (`0x15E034A`) besides calling the Infra one. Overriding cell `0x8E0` is thus
a pattern the family itself uses (Infra and LowLatency), but installing a custom `output` via
field `+0x40` is done by no Apple class. `?` Whether the Skywalk machinery tolerates an `ifp+0x258`
different from `_ifnet_enqueue` on a provider `ifnet` cannot be fixed from the kernelcache; it is
checked at runtime.

For a real `awdl0` of an Apple Wi-Fi, the push does not come from IO80211 nor the BSD layer: it
comes from the
Skywalk queue, served when the provider calls `nx_netif_na_txsync`, and from
`enable`/`checkForWork` in the
machinery of `IOEventSource`. The BCM kext only contributes the queue subclass and its
`packetSubmission`; it
does not call the base on its own. `?` The concrete kext thread that fires the `txsync` cannot be
fixed from the kernelcache.

## 6. Proposed design for stage 2

| Step | Action | Why |
|---|---|---|
| 1 | Declare in `RTLAwdInterface` the override `uintptr_t initBSDInterfaceParameters(ifnet_init_eparams *, sockaddr_dl **) override` (cell `0x8E0`) | The base already declares the method (`Source/IO80211/IO80211_25_6_0.h:403`); an override adds no cell or shifts the following ones. |
| 2 | In the body, first call `IO80211SkywalkInterface::initBSDInterfaceParameters(params, lladdr)` and, only if it returns 0, write `*(void **)((char *)params + 0x40) = (void *)&rtlAwdOutput` | The registration copies `+0x40` to `ifp+0x260` (`0x63F890`) and to `ifp+0x258` (`0x63FC30`); if the base fails, there is no `ifnet` to prepare. |
| 3 | In `rtlAwdOutput(__ifnet *, __mbuf *)`, enqueue the mbuf into the provider's own ring and notify the send queue with `packetSubmission(false)` | `packetSubmission` does not take the mbuf: it makes `legacyDequeue` pull what is in the ring and deliver it to the action. The signature is that of `if_output` (`0x2B002CE`, which returns `0x13`). |
| 4 | Put the real action of the queue in place of the stage 1 no-op | Without an action, `legacyDequeue` delivers nothing to the radio. |
| 5 | Alternative to 1–2: override `prepareBSDInterface(__ifnet *, unsigned int)` (cell `0x8E8`) and do `cmpxchg` at `ifp+0x258` against `[ifp+0x5A0]`, saving the previous value | Does not depend on `ifnet_init_eparams`; the `ifnet` is already attached (`0x9A42DF`) and not yet published. |
| 6 | In `stop`/`terminate`, restore the saved value with a `cmpxchg` if path B was used | There is no reset symbol; `ifnet_detach_final` resets `ifp+0x248`, `+0x258` and `+0x260` to `_ifp_if_output` (`0x5E2C2F`–`0x5E2C44`) when the `ifnet` is recycled. `?` The `ifnet` lifetime relative to `stop` cannot be fixed from the kernelcache. |

`RTLAwdInterface` derives from `IO80211AWDLProtocol` (`Source/AirLayer/AWDLInterface.h:297`), which
derives from
`IO80211VirtualInterface` and the latter from `IO80211SkywalkInterface`
(`Source/IO80211/IO80211_25_6_0.h:386`). The
local header already redeclares `prepareBSDInterface` in `IO80211VirtualInterface`
(`Source/AirLayer/AWDLInterface.h:104`); `initBSDInterfaceParameters` is only declared in the base
(`Source/IO80211/IO80211_25_6_0.h:403`). Declaring the second in `RTLAwdInterface` adds no cell:
it replaces the body of cell `0x8E0` and leaves the others in place.

Nothing is installed in `initWithController`: at that point only the interface registration exists
(`registerEthernetInterface`), and the `ifnet` is born later, inside the BSD client `start` that
fires
`registerService`. The first point where it can be written is `initBSDInterfaceParameters`; the
last,
`prepareBSDInterface`.

With this design the first ND of `awdl0` does reach the action: the stack enters through
`rtlAwdOutput`, there is no
`ifclassq` nor purge, and `packetSubmission` brings the packet up to `legacyDequeue`. Without step
1, the same
ND is purged in `if_qflush` before the action sees it (`0x5D5796`). `?` The real capacity of the
ring and
the number of packets the driver must request per round depend on the radio and are tuned on
hardware.

## 7. Corrections to `AWDL_STAGE1_STACK_TX_ENTRY.md`

| Point | Correction |
|---|---|
| §2, step 3 | `ifp+0x258` does not keep the client's `output` (`IOSkywalkNetworkBSDClient::if_output`): `ifnet_allocate_extended` replaces it with `_ifnet_enqueue` because `params+0x40` is zero and there is `IFEF_TXSTART` (`0x63FC16`–`0x63FC30`). |
| §1 | It is confirmed the stack goes through `_ifnet_enqueue` and the `ifclassq`; what it does not do is call `packetSubmission`. |
| §5 | `ifnet_set_output_handler` / `dlil_set_output_handler` are not a path: they do not link. The path is field `+0x40` of `ifnet_init_eparams` from cell `0x8E0`, with the write at `ifp+0x258` from cell `0x8E8` as alternative. |
| Previous design (§6, step 1) | It installed the `output` in `initWithController` with a nonexistent symbol and before the `ifnet` existed; replaced by the override of `initBSDInterfaceParameters` and field `+0x40`. |

## 8. Address verification

Heads checked with `ida_funcs.get_func(ea).start_ea == ea`:
`0xFFFFFF800063F3F0`, `0xFFFFFF80005D5730`, `0xFFFFFF80005E5F40`, `0xFFFFFF80005E6110`,
`0xFFFFFF80005EA850`, `0xFFFFFF80005EB0F0`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005EF4E0`,
`0xFFFFFF80005EF500`, `0xFFFFFF80005EF510`, `0xFFFFFF80005EF540`, `0xFFFFFF80005EF560`,
`0xFFFFFF80005E5330`, `0xFFFFFF8002B002CE`, `0xFFFFFF8002B2CD68`, `0xFFFFFF8002B0CC76`,
`0xFFFFFF8002B2C746`, `0xFFFFFF80009E2440`, `0xFFFFFF8002AFF562`, `0xFFFFFF80022F635C`,
`0xFFFFFF8002331ADA`, `0xFFFFFF80022F63B6`, `0xFFFFFF800235FF5A`, `0xFFFFFF800235FF64`,
`0xFFFFFF80015E034A`, `0xFFFFFF8002B10BCA`, `0xFFFFFF8002B10E58`,
`0xFFFFFF80009A4140`, `0xFFFFFF80005E8D20`, `0xFFFFFF8002B00654`.

Instruction citations inside a verified head: `0xFFFFFF800063FE0A`–`0xFFFFFF800063FE18`,
`0xFFFFFF800063F882`, `0xFFFFFF800063FC0A`, `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30`,
`0xFFFFFF80005D5750`–`0xFFFFFF80005D5755`, `0xFFFFFF80005D5796`, `0xFFFFFF80005F3352`,
`0xFFFFFF80005E6008`, `0xFFFFFF80005E6172`, `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284`,
`0xFFFFFF80005EAA18`, `0xFFFFFF80005EAA3A`, `0xFFFFFF80005EB1F7`, `0xFFFFFF80005E3CFC`,
`0xFFFFFF80005EF50C`, `0xFFFFFF80005EF544`, `0xFFFFFF80005EF577`, `0xFFFFFF80005EF54D`,
`0xFFFFFF80005EF4ED`, `0xFFFFFF8002AFFA4D`–`0xFFFFFF8002AFFA54`, `0xFFFFFF8002B002DA`,
`0xFFFFFF8002B002E0`, `0xFFFFFF8002B0CCB2`, `0xFFFFFF8002B2C82A`, `0xFFFFFF80009E24F8`,
`0xFFFFFF8002AFFADC`, `0xFFFFFF8002AFFB31`, `0xFFFFFF8002AFFBB5`, `0xFFFFFF80009A41DE`,
`0xFFFFFF80009A41F4`, `0xFFFFFF80009A420D`, `0xFFFFFF80009A42DF`, `0xFFFFFF800063F4A6`,
`0xFFFFFF800063F89D`, `0xFFFFFF800063F890`, `0xFFFFFF800063FDA5`, `0xFFFFFF80005E9668`,
`0xFFFFFF80005E9693`, `0xFFFFFF8002B00688`, `0xFFFFFF8002B006CA`,
`0xFFFFFF80005E2C2F`–`0xFFFFFF80005E2C44`,
`0xFFFFFF80022F6390`, `0xFFFFFF80015E034A`.

Data read: virtual tables with `packetSubmission` at `0x15ACED8`, `0x17E13B8` and `0x2B3E250`;
pointer
to `nx_netif_na_txsync` at `0xFFFFFF80009E0E9F`; cells `0x8F0`/`0x8F8` of
`__ZTV19IO80211AWDLProtocol`
(`0xFFFFFF8002456440`), `__ZTV30AppleBCMWLANProximityInterface` (`0xFFFFFF80017F5B68`),
`__ZTV28AppleBCMWLANSkywalkInterface` (`0xFFFFFF80017E2628`), `__ZTV25AppleBCMWLANInfraProtocol`
(`0xFFFFFF80017DECA8`), `__ZTV31AppleBCMWLANLowLatencyInterface` (`0xFFFFFF80017E4490`) and
`__ZTV26IOSkywalkEthernetInterface` (`0xFFFFFF8002B3F178`).
