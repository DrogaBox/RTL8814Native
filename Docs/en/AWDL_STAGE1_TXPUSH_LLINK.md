# From the `ifclassq` to the startup thread, the `if_start` of the netif and the `+0x3C` sweep

Closes the three open points of `Docs/AWDL_STAGE1_QSET_STATE.md` §10 and corrects the conclusion of the
first version of this document: the enqueuing of the `ifclassq` **does** wake the startup thread of the
`ifnet`, but that thread does not notify the provider: it runs `if_start`, which for this interface is
`_ifp_if_start`, and `_ifp_if_start` purges the submission queue. Everything was read in IDA headless over copies of
`boot_awdl.kc.i64`; nothing was linked, loaded nor run.

## 1. Answer

There is no jump from the `ifclassq` to `nxp_queue_tx_push`. The enqueuing operation of the `ifclassq`
(`+0x18` of the `pktsched` table) is `fq_if_enqueue`, which only enqueues into a flow queue of the
`fq_codel` scheduler and does not cross into the provider table. `nxp_queue_tx_push` is called only
from the **virtual port** path (`netif_vp_na_txsync`, `0xFFFFFF80009F4D7F`), not from the submission of the
BSD stack.

The enqueuing **does** have a trigger: `sub_FFFFFF80005E6110`, after enqueuing, checks
`if_eflags & IFEF_TXSTART` (`0xFFFFFF80005E61B2`) and, if the startup thread exists
(`[ifp+0x330] != 0`) and is not running, increments `[ifp+0x304]` and wakes channel `&ifp+0x330`
(`0xFFFFFF80005E6284`). That thread is `sub_FFFFFF80005EA850`, created in `ifnet_attach`
(`kernel_thread_start_priority(sub_5EA850, ifp, -1, &ifp+0x330)` at `0xFFFFFF80005E9693`); its loop
`sub_FFFFFF80005EB0F0` calls `ifp->if_start` at `0xFFFFFF80005EB1F7`.

The `if_start` of this interface is `_ifp_if_start` (`0xFFFFFF80005E3CE0`), not a routine of the
Skywalk family: field `ifp+0x250` is loaded in `ifnet_allocate_extended` from `ifnet_init_eparams+0x48`
(`0xFFFFFF800063F4B5`) and, if that field is zero, `_ifp_if_start` is put there (`0xFFFFFF800063F752`,
stored at `0xFFFFFF800063F89D`). `IOSkywalkNetworkBSDClient::start` leaves that field at zero:
`bzero(params, 0x150)` at `0xFFFFFF8002AFFA1B`, header version 2/len `0x158` at `0xFFFFFF8002AFFA20`,
and no write to `params+0x48`; the constructor of `IOSkywalkEthernetInterface`
(`0xFFFFFF8002B10BCA`) does not write it either, and the nexus requires it to be zero (`0xFFFFFF80009A41F4`).

`_ifp_if_start` checks `IFEF_TXSTART` and does `if_qflush(ifp, ifp->if_snd)`
(`0xFFFFFF80005E3CF4`–`0xFFFFFF80005E3CFC`); `if_qflush` (`0xFFFFFF80005D5730`) calls the purge operation of
the `ifclassq` (`[ifq->ops+0x30]`, `0xFFFFFF80005D5796`). That is: the trigger of the enqueuing
**purges** the packet, it does not deliver it to the action of the queue.

The only path reaching `legacyDequeue` and the action is the explicit pull: `enable`
(`0xFFFFFF80002B0CCB2`), `checkForWork` (`0xFFFFFF80002B2C82A`) and `nxp_tx_qset_notify`
(`[provider+0x90]`, called at `0xFFFFFF80009E24F8`). None of the three fires after the
enqueuing with the stage 1 code.

The other two points are closed as follows. The `netif_llink` is not created with message
`0xE0060105`: only `IOSkywalkNetworkInterface::addLogicalLink` (`0xFFFFFF8002B0A7DE`) sends it,
whose only call is the DriverKit route `IOUserNetworkWLAN::_AddLogicalLink_Impl` (`0xFFFFFF8002B07CE8`).
The only call to `kern_nexus_netif_llink_add` (`0xFFFFFF80009E6360`) is at `0xFFFFFF80005FE2D9`,
within the prologue region `0xFFFFFF80005FCD20` (`create_netif_provider_and_instance`), and it runs
after `kern_nexus_controller_alloc_net_provider_instance` (`0xFFFFFF80009A4140`, called at
`0xFFFFFF80005FDF42`), i.e. after `ifnet_allocate_extended` (`0x9A420D`) and `ifnet_attach`
(`0x9A42DF`). The full sweep of `+0x3C` over the kernel text finds 1494 accesses; only two
write the byte of the `netif_llink`: `0xFFFFFF80009F98CE` (value 1, in `sub_FFFFFF80009F96E0`) and
`0xFFFFFF80009F9C63` (value 2, in `sub_FFFFFF80009F9C60`).

## 2. The enqueuing into the `ifclassq`

| Step | Instruction | Function | What it checks or does |
|---|---|---|---|
| Entry | `0xFFFFFF80005E65EC`–`0xFFFFFF80005E65FE` | `ifnet_enqueue_pkt` `0xFFFFFF80005E65D0` | `if_refflags & 7 == 2` (`0x05E65EC`) and `if_eflags` (`0x05E65F5`); if it passes, it calls `sub_FFFFFF80005E6110`. |
| Diversion | `0xFFFFFF80005E6133` | `sub_FFFFFF80005E6110` | If `[ifp+0x900] != 0`, jumps to `0xFFFFFF80005E62A3` (`_netem_enqueue`, `0xFFFFFF80005E6343`) and does not touch the `ifclassq`. |
| Enqueuing | `0xFFFFFF80005E6172` | `sub_FFFFFF80005E6110` | `call qword ptr [r10+18h]`, with `r10 = [ifclassq+0x68]`; it is the `enqueue` operation of the discipline. |
| Discipline | `0xFFFFFF800070E9C0` | `ifclassq_enqueue` | Wrapper of the same call (`+0x18` of `[ifclassq+0x68]`). |
| Selection | `0xFFFFFF800070DC80` | `ifclassq_setup` | Chooses the type by `if_output_sched_model` (`ifp+0x340`); with model 0 it installs type 7 or 8, both `fq_codel`. |
| Tables | `0xFFFFFF80011D6CF8` (type 7) and `0xFFFFFF80011D6D50` (type 8) | data | `+0x18` = `fq_if_enqueue` `0xFFFFFF8000713020` in both. |
| Real enqueuing | `0xFFFFFF8000713020` | `fq_if_enqueue` | Enqueues into a flow queue; its only indirect call is `[rdi+0xF8]` (`0x71321A`), internal of the scheduler. It does not touch the provider table. |
| Trigger | `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284` | `sub_FFFFFF80005E6110` | If `if_eflags & 0x80` (`IFEF_TXSTART`), `[ifp+0x300]&1 == 0`, `[ifp+0x309] == 0` and `[ifp+0x330] != 0`: `inc [ifp+0x304]` and wakes `&ifp+0x330`. |

`if_output_sched_model` is not set by stage 1: the `RegistrationInfo` of `Source/AirLayer/AWDLInterface.cpp`
leaves `AWDL_REGINFO_FLAGS` (`+0x40`) at zero, and with model 0 the type is 7 or 8; in both cases the operation is
the same.

The diversion by `+0x900` was not seen written for the `ifnet` at registration: the writes of `+0x900`
located in the nexus region (`0xFFFFFF80009FE66F`, `0xFFFFFF80009FE70F`) are of a matrix of
entries of 0x1200 bytes (`add rdi, 1200h`), not of the `ifnet` block. For `awdl0` the
`ifclassq` path is taken. `?` The runtime value of `ifp+0x900` cannot be pinned without hardware.

## 3. Why there is no jump to `tx_push`

The initialization table of the provider is 0x60 bytes and is built at `0xFFFFFF8002AFF15E`–
`0xFFFFFF8002AFF1D9`:

| Displacement | Content |
|---|---|
| `+0x00` | 2 (version) |
| `+0x28` | `nxp_qset_init` `0xFFFFFF8002AFF1E4` |
| `+0x30` | `nxp_qset_fini` `0xFFFFFF8002AFF299` |
| `+0x38` | `nxp_queue_init` |
| `+0x40` | `nxp_queue_fini` |
| `+0x48` | `nxp_tx_qset_notify` `0xFFFFFF8002AFF3E2` |
| `+0x58` | `nxp_queue_tx_push` `0xFFFFFF8002AFF4D1` |

The kernel copies that table into its own `kern_nexus_provider` with base `+0x48`: `nxp_qset_init` (`+0x28`)
appears in the provider at `+0x70` (`call [r13+70h]` at `0xFFFFFF80009F99BE`) and `nxp_queue_init` (`+0x38`)
at `+0x80` (`0xFFFFFF80009F9A0E` and `0xFFFFFF80009F9A8D`). Therefore `notify` lands at `+0x90` and `tx_push`
at `+0xA0`, and the two calls appear together in `nx_netif_na_txsync` (`0xFFFFFF80009E2440`):

| Instruction | Call | What it is |
|---|---|---|
| `0xFFFFFF80009E24E9`–`0xFFFFFF80009E24F8` | `rax = [rdi+0x90]; call rax` | `nxp_tx_qset_notify` over the provider (`rdi = [nexus+0x18]`). |
| `0xFFFFFF80009E2545` | `call qword ptr [rdi+0xA0h]` | `nxp_queue_tx_push`, only when the flags ask for it. |

In the whole text there are only two `call qword ptr [reg+0A0h]` in network code: `0xFFFFFF80009E2545` (here) and
`0xFFFFFF80009F4D7F` (`netif_vp_na_txsync`, virtual port path). The submission of the BSD stack does not go through
`netif_transmit` (`0xFFFFFF80009E5510`): its callers are `nx_netif_host_output` (`0xFFFFFF80009EB0B7`),
`fsw_ring_flush` (`0xFFFFFF80009BDA76`) and `sub_FFFFFF80009F86C0` (`0xFFFFFF80009F8F27`).

## 4. The pull from the `ifclassq`

| Step | Instruction | Function | Condition or effect |
|---|---|---|---|
| Trigger 1 | `0xFFFFFF8002B0CCB2` | `IOSkywalkTxSubmissionQueue::enable` `0xFFFFFF8002B0CC76` | After `IOEventSource::enable`, if `netif_queue` (`+0x78`) is not null and `(+0x142)&4 == 0`, it calls `packetSubmission(false)`. |
| Trigger 2 | `0xFFFFFF8002B2C82A` | `checkForWork` `0xFFFFFF8002B2C746` | Only if counter `+0x214` changed. |
| Trigger 3 | `0xFFFFFF80009E24F8` | `nx_netif_na_txsync` → `nxp_tx_qset_notify` | Per queue it calls `[vtable+0x1D8](queue, 1)`: the same `packetSubmission(bool)`. |
| Fill | `0xFFFFFF8002B2CED4` | `packetSubmission(bool)` `0xFFFFFF8002B2CD68` | With `(+0x142)&2` it uses `getPacketListForNetworking`; else `refillTxRingForNetworking`. Afterwards, `legacyDequeue`. |
| Pull | `0xFFFFFF8002B0D41F` | `refillTxRingForNetworking` `0xFFFFFF8002B0D3E0` | `kern_netif_queue_tx_dequeue`; only 0 and 35 are success; any other code returns `0xE00002B4`. |
| Gate | `0xFFFFFF80009E614E` | `kern_netif_queue_tx_dequeue` `0xFFFFFF80009E6120` | `byte [llink+0x3C] == 2` returns 6 (`ENXIO`); additionally doorbell cap (`nx_netif_doorbell_max_dequeue`), flow control and `ifclassq_dequeue`. |
| Output | `0xFFFFFF8002B2C860` | `legacyDequeue` | Calls the action of the queue; it is the point where `txNoop` runs in stage 1. |

`refillTxRingForNetworking` does not call `tx_push`: it converts each `kern_packet` into an `IOSkywalkPacket`, it
produces it into the `IOSkywalkPacketTable` and, at the end, calls `IOSkywalkPacketQueue::packetSubmission(uint,uint)`
(`0xFFFFFF8002B0D53E`), which only updates counters (`0xFFFFFF8002B09280`). The action runs via the
subsequent `legacyDequeue`.

## 5. The registration of the `netif_llink`

| Route | Evidence | Verdict |
|---|---|---|
| Message `0xE0060105` | `IOSkywalkNetworkInterface::addLogicalLink` `0xFFFFFF8002B0A7DE` sends it with value 3758489861 (`0xE0060105`); its only caller is `IOUserNetworkWLAN::_AddLogicalLink_Impl` `0xFFFFFF8002B07CE8` (`jmp` at `0xFFFFFF8002B07CED`). | DriverKit only. Not the route of a normal kext. |
| Direct call | `kern_nexus_netif_llink_add` `0xFFFFFF80009E6360` has a single code reference: `0xFFFFFF80005FE2D9`, in prologue region `0xFFFFFF80005FCD20` (not recognized as a function by the IDB; prologue `push rbp`/`mov rbp,rsp`/`sub rsp,3D8h` at `0x5FCD20`–`0x5FCD2D`). | It is the only possible creation. |

Order within region `0x5FCD20`:

| Instruction | Done |
|---|---|
| `0xFFFFFF80005FDF42` | `kern_nexus_controller_alloc_net_provider_instance` `0xFFFFFF80009A4140`. |
| `0xFFFFFF80009A420D` | Within the previous one: `ifnet_allocate_extended` `0xFFFFFF800063F3F0`. |
| `0xFFFFFF80009A42DF` | Within the previous one: `ifnet_attach` `0xFFFFFF80005E8D20`. |
| `0xFFFFFF80005FDFCE` | `nx_find` of the freshly created nexus. |
| `0xFFFFFF80005FE2D9` | `kern_nexus_netif_llink_add`, after building the link configuration (loop with step `0x510`). |

With respect to `start`: `IOSkywalkNetworkBSDClient::start` `0xFFFFFF8002AFF562` calls
`kern_nexus_controller_alloc_net_provider_instance` at `0xFFFFFF8002AFFBB5`; the registration of the link happens
after that call, already with the `ifnet` created and attached. The exact instruction entering region
`0x5FCD20` is not pinned by the disassembly: the region has no direct references and the pair
`{0xFFFFFF80005FCD20, 0xFFFFFF80005FE5B0}` of `0xFFFFFF80011D5F70` has no xrefs or pointers in data.
`nx_netif_compat_init` (`0xFFFFFF80009E64B0`) only registers `nx_netif_compat_prov_s`
(`0xFFFFFF80011DF7E0`). The caller of `0x5FCD20` remains `?`; what does remain pinned is that, when it
runs, the link is created after `ifnet_allocate_extended` and after `ifnet_attach`.

## 6. The startup thread of the `ifnet`

| Instruction | Done |
|---|---|
| `0xFFFFFF80005E9632` | `ifnet_attach`: `cmp qword [ifp+0x330], 0`; if non-zero, panic (`ifp->if_start_thread == THREAD_NULL`). |
| `0xFFFFFF80005E9668` | `cmp qword [ifp+0x250], 0`; if zero, panic (`ifp->if_start != NULL`). |
| `0xFFFFFF80005E9693`–`0xFFFFFF80005E96A5` | `kernel_thread_start_priority(sub_FFFFFF80005EA850, ifp, -1, &ifp+0x330)`. |
| `0xFFFFFF80005EA8A6` | The thread is named `ifnet_start_%s`; if `[ifp+0xC3] & 4` (`IFEF_SKYWALK_NATIVE`), `skywalk_doorbell_%s_tx` (`0xFFFFFF80005EA8CA`). |
| `0xFFFFFF80005EAA0C`–`0xFFFFFF80005EAA18` | `assert_wait(&ifp+0x330, 0)`. |
| `0xFFFFFF80005EAA3A`–`0xFFFFFF80005EAA44` | `thread_block_parameter(sub_FFFFFF80005EB0F0, ifp)`. |
| `0xFFFFFF80005EB1F7`–`0xFFFFFF80005EB1FE` | Thread loop: `rax = [ifp+0x250]; call rax`. |

Who wakes it:

| Instruction | Done |
|---|---|
| `0xFFFFFF80005E61B2` | `test byte [ifp+0xC0], 80h` (`IFEF_TXSTART`). |
| `0xFFFFFF80005E61CE` | `test byte [ifp+0x300], 1`; if set, it does not wake. |
| `0xFFFFFF80005E61DB` | `inc dword [ifp+0x304]` (counter of startup requests). |
| `0xFFFFFF80005E61E1` | `cmp byte [ifp+0x309], 0`; if the thread is already in the loop, it does not wake. |
| `0xFFFFFF80005E61EE` | `cmp qword [ifp+0x330], 0`; without a thread, there is nothing to wake. |
| `0xFFFFFF80005E6222`–`0xFFFFFF80005E6284` | Computes the index of the `waitq` from `&ifp+0x330` and calls `sub_FFFFFF80003CD190` over that channel. |

`ifnet_start` (`0xFFFFFF80005E5060`) repeats the same sequence (`0xFFFFFF80005E5083`–`0xFFFFFF80005E513F`),
so it also wakes the thread when `IFF_UP` changes.

The channel is the same one where the thread does `assert_wait`: `&ifp+0x330`. Routine `sub_FFFFFF80003CD190`
resolves the channel by hash in the table of `0xFFFFFF800021B400`/`0xFFFFFF800021B3F8` and enters
`sub_FFFFFF80003CBC90`; it is not a `thread_call`.

## 7. The `if_start` of this interface is `_ifp_if_start`

| Instruction | Done |
|---|---|
| `0xFFFFFF800063F4B5` | `ifnet_allocate_extended`: `var_60 = [params+0x48]`. |
| `0xFFFFFF800063F740`–`0xFFFFFF800063F752` | If `params+0x48 == 0`, `r15 = _ifp_if_start`; else the value of the parameter is used. |
| `0xFFFFFF800063F89D` | `[ifp+0x250] = r15` (`if_start`). |
| `0xFFFFFF8002AFFA1B` | `IOSkywalkNetworkBSDClient::start`: `bzero(params, 0x150)`. |
| `0xFFFFFF8002AFFA20`–`0xFFFFFF8002AFFA2A` | Header of the `ifnet_init_eparams`: version 2, length `0x158`. |
| `0xFFFFFF8002B10BCA` | `IOSkywalkEthernetInterface::initBSDInterfaceParameters`: writes `+0xD8`–`+0xF8`, `+0x128`, `+0x130`, `+0x2C`, `+0x30` and `+0x140`; does not write `+0x48`. |
| `0xFFFFFF80022F635C` | `IO80211SkywalkInterface::initBSDInterfaceParameters`: delegates into the previous one and only sets a flag at `params+0xB`. |
| `0xFFFFFF80009A41F4` | `kern_nexus_controller_alloc_net_provider_instance` requires `params+0x48 == 0`; the nexus does not admit a custom `if_start`. |
| `0xFFFFFF80005E3CE9` | `_ifp_if_start`: `test byte [ifp+0xC0], 80h`; without `IFEF_TXSTART` it does nothing. |
| `0xFFFFFF80005E3CF4`–`0xFFFFFF80005E3CFC` | `rsi = [ifp+0x338]` (`if_snd`) and `jmp _if_qflush`. |
| `0xFFFFFF80005D5796` | `if_qflush`: `call qword ptr [rax+30h]` with `rax = [ifq+0x68]`; it is the **purge** operation of the `ifclassq`. |

Consequence: the startup thread neither notifies the provider nor calls `packetSubmission` nor
`kern_netif_queue_tx_dequeue`. If it runs with the packet already enqueued, it drops it.

## 8. Sweep of writes of `+0x3C`

All the instructions of text `0xFFFFFF8000400000`–`0xFFFFFF8002600000` with
displacement `+3Ch` were walked:

| Metric | Value |
|---|---|
| Total accesses to `+3Ch` | 1494 |
| Writes into some structure | 563 |
| Writes in functions of the `netif_llink` | 2 |
| Writes to the byte of the `netif_llink` | 2 |

The two writes of the link:

| Instruction | Function | Value |
|---|---|---|
| `0xFFFFFF80009F98CE` | `sub_FFFFFF80009F96E0` (initialization) | 1 |
| `0xFFFFFF80009F9C63` | `sub_FFFFFF80009F9C60` (destruction) | 2 |

The rest writes the `+0x3C` of other structures; for example `0xFFFFFF800064035B` and `0xFFFFFF8000640368`
(`ifnet_set_flags`, own byte of the `ifnet`), `0xFFFFFF8000739DC9` (`inp_get_soprocinfo`),
`0xFFFFFF800076355B`/`0xFFFFFF80007636A7` (`sub_FFFFFF8000763430`). None of the functions of the
`netif_llink` writes another value. Any block copy (`memcpy`/`bzero`) that could
reach the byte without a literal displacement remains `?`; none was found in the life cycle of the link.

## 9. Corrections to the previous documents

| Document | Previous claim | Correction |
|---|---|---|
| `AWDL_STAGE1_QSET_STATE.md` §10 | "The instruction was not located" of the jump to `tx_push`. | There is no such jump: the `ifclassq` enqueues into `fq_if_enqueue`; `tx_push` is only used in `netif_vp_na_txsync`. |
| `AWDL_STAGE1_QSET_STATE.md` §9 | "`nxp_qset_init`/`nxp_queue_init` … when registering the provider". | They run when creating the `netif_llink` (`nx_netif_llink_ext_init_queues`, `0x9F9960`, calls `+0x70` and `+0x80`), not when registering the table of the provider. |
| `AWDL_STAGE1_IFUP_PATH.md` §4 and risk 4 | The first ND/RS reaches `txNoop` with the queues enabled. | Enabling does not suffice: a pull (`packetSubmission`/`checkForWork`/`notify`) is needed **after** the enqueuing. `enable()` does it earlier, with the queue empty. |
| First version of this document, §1/§8 | "The enqueuing wakes nobody; the packet stays in the `ifclassq`". | The enqueuing **does** wake the startup thread (`0x5E6284`), but that thread runs `_ifp_if_start`, which purges the queue: the packet is dropped, it does not stay enqueued, and it never reaches `txNoop` either. |
| `AWDL_STAGE1_QSET_STATE.md` §10 | "Registration route of the `netif_llink` … both exist". | Message `0xE0060105` is DriverKit only; the only creation is the direct call of `0x5FE2D9`. |

## 10. Final table: does the first ND of `awdl0` reach `txNoop`?

| # | Step | Condition | Pinned by the disassembly? |
|---|---|---|---|
| 1 | `ifnet_enqueue_pkt` accepts the packet | `if_refflags & 7 == 2` and correct `if_eflags` | Yes; `ifnet_attach` and `ifnet_allocate_extended` set them. |
| 2 | The `ifclassq` path is taken | `[ifp+0x900] == 0` | `?` The block of the `ifnet` was not seen written at registration; the runtime value cannot be pinned without hardware. |
| 3 | `fq_if_enqueue` enqueues it | Discipline 7/8 and flow queue with room | Yes. |
| 4 | The startup thread is woken | `IFEF_TXSTART`, `[ifp+0x300]&1 == 0`, `[ifp+0x309] == 0`, `[ifp+0x330] != 0` | Yes; the thread is created by `ifnet_attach` at `0x5E9693`. |
| 5 | The thread runs `if_start` | `ifp+0x250` = `_ifp_if_start` | Yes: `params+0x48 == 0` and the default value applies. |
| 6 | `if_start` purges | `IFEF_TXSTART` set → `if_qflush(ifp, if_snd)` | Yes. |
| 7 | Someone pulls the packet to the action | `packetSubmission`/`checkForWork`/`notify` after the enqueuing | **No**: with stage 1 none of the three is invoked afterwards. |

Answer: **no, the first ND does not reach `txNoop`**. The enqueuing wakes the startup thread, but that
thread runs `_ifp_if_start`, which purges the `ifclassq`; there is no notification to the provider nor a pull. The
action would only run if something called `packetSubmission`/`checkForWork`/`notify` after the
enqueuing, and the stage 1 code does not do it.

The only condition deciding the observable detail in T3.5 is step 7: if the workloop of the family
issues a `checkForWork` (or the provider a `notify`) before the startup thread purges, the packet
reaches `txNoop` and the counter rises; if not, it is purged without going through the action. That is the point the
hardware test has to distinguish; the rest of the chain is pinned by the disassembly.

## 11. Address verification

Heads checked with `ida_funcs.get_func(ea).start_ea == ea`:
`0xFFFFFF80005E6110`, `0xFFFFFF80005E65D0`, `0xFFFFFF800070E9C0`, `0xFFFFFF800070E9D0`,
`0xFFFFFF800070DC80`, `0xFFFFFF8000713020`, `0xFFFFFF8002B0CC76`, `0xFFFFFF8002B0CCD6`,
`0xFFFFFF8002B2CD68`, `0xFFFFFF8002B0D3E0`, `0xFFFFFF8002B0D57C`, `0xFFFFFF8002B2CB2A`,
`0xFFFFFF80009E6120`, `0xFFFFFF8002B2C860`, `0xFFFFFF8002B2C746`, `0xFFFFFF80009F49C0`,
`0xFFFFFF8002AFF4D1`, `0xFFFFFF8002AFF1E4`, `0xFFFFFF80009E6360`, `0xFFFFFF80009A4140`,
`0xFFFFFF8002AFF562`, `0xFFFFFF8002B0A7DE`, `0xFFFFFF8002B07CE8`, `0xFFFFFF80009F9590`,
`0xFFFFFF80009F96E0`, `0xFFFFFF80009F9C60`, `0xFFFFFF80009FA2A0`, `0xFFFFFF80005E8D20`,
`0xFFFFFF800063F3F0`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005D5730`, `0xFFFFFF80009E64B0`,
`0xFFFFFF80009E64F0`, `0xFFFFFF80009FAD40`, `0xFFFFFF80009FAF20`, `0xFFFFFF80009E5510`,
`0xFFFFFF80009E2440`, `0xFFFFFF80009E3800`, `0xFFFFFF8002AFF3E2`, `0xFFFFFF8002B09280`,
`0xFFFFFF80005EA850`, `0xFFFFFF80005EB0F0`, `0xFFFFFF80005E5060`, `0xFFFFFF80003CD190`,
`0xFFFFFF8002B10BCA`, `0xFFFFFF80022F635C`, `0xFFFFFF80009FE5B0`, `0xFFFFFF8000718340`.

Region without a recognized head: `0xFFFFFF80005FCD20` (prologue `push rbp`/`mov rbp,rsp`/`sub rsp,3D8h`).

Instruction citations within a verified head or region: `0xFFFFFF80005E6133`,
`0xFFFFFF80005E6172`, `0xFFFFFF80005E61B2`, `0xFFFFFF80005E61CE`, `0xFFFFFF80005E61DB`,
`0xFFFFFF80005E61E1`, `0xFFFFFF80005E61EE`, `0xFFFFFF80005E6222`–`0xFFFFFF80005E6284`,
`0xFFFFFF80005E62A3`, `0xFFFFFF80005E6343`, `0xFFFFFF80005E65EC`–`0xFFFFFF80005E65FE`,
`0xFFFFFF80005E9632`, `0xFFFFFF80005E9668`, `0xFFFFFF80005E9693`–`0xFFFFFF80005E96A5`,
`0xFFFFFF80005E3CE9`, `0xFFFFFF80005E3CF4`–`0xFFFFFF80005E3CFC`, `0xFFFFFF80005EA8A6`,
`0xFFFFFF80005EA8CA`, `0xFFFFFF80005EAA0C`–`0xFFFFFF80005EAA18`, `0xFFFFFF80005EAA3A`,
`0xFFFFFF80005EB1F7`–`0xFFFFFF80005EB1FE`, `0xFFFFFF80005E5083`–`0xFFFFFF80005E513F`,
`0xFFFFFF800063F4B5`, `0xFFFFFF800063F740`–`0xFFFFFF800063F752`, `0xFFFFFF800063F89D`,
`0xFFFFFF8002AFFA1B`, `0xFFFFFF8002AFFA20`, `0xFFFFFF8002AFFACB`, `0xFFFFFF8002AFFADC`,
`0xFFFFFF80009A41F4`, `0xFFFFFF80009E24E9`–`0xFFFFFF80009E24F8`, `0xFFFFFF80009E2545`,
`0xFFFFFF8002B0CCB2`, `0xFFFFFF8002B2C82A`, `0xFFFFFF8002B2CED4`, `0xFFFFFF8002B0D41F`,
`0xFFFFFF8002B0D526`, `0xFFFFFF8002B0D53E`, `0xFFFFFF80009E614E`, `0xFFFFFF80009F99BE`,
`0xFFFFFF80009F9A0E`, `0xFFFFFF80009F9A8D`, `0xFFFFFF80009F4D7F`, `0xFFFFFF80005FDF42`,
`0xFFFFFF80005FDFCE`, `0xFFFFFF80005FE2D9`, `0xFFFFFF80009A420D`, `0xFFFFFF80009A42DF`,
`0xFFFFFF8002AFFBB5`, `0xFFFFFF80009F98CE`, `0xFFFFFF80009F9C63`, `0xFFFFFF800064035B`,
`0xFFFFFF8000640368`, `0xFFFFFF8000739DC9`, `0xFFFFFF800076355B`, `0xFFFFFF80007636A7`,
`0xFFFFFF80005D5796`, `0xFFFFFF80009FE66F`, `0xFFFFFF80009FE70F`.

Data read: discipline tables `0xFFFFFF80011D6CF8` and `0xFFFFFF80011D6D50` (`+0x18` = `0x713020`);
pair `{0xFFFFFF80005FCD20, 0xFFFFFF80005FE5B0}` at `0xFFFFFF80011D5F70`; descriptor
`nx_netif_compat_prov_s` at `0xFFFFFF80011DF7E0`; pointer to `_ifp_if_start` at `0xFFFFFF800063F752`.
