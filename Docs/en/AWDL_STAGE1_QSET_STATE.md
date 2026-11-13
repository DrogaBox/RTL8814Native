# State of the queue set and of `if_refflags`/`if_eflags` before the first submission of `awdl0`

Trace, over the kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), of the two states the
stack consults before delivering a packet to the submission queue: byte `+0x3C` compared by
`netif_qset_enqueue` and fields `+120` and `+192` checked by `ifnet_enqueue_pkt`. It determines who writes them
during the registration of the interface and whether the first ND/RS of `awdl0` reaches `nxp_queue_tx_push` with the
queues that `RTLAwdInterface::enable` enables (commit `e50d7cf`). No code, patch or
specification was modified. Base: IDA headless (`idat -A -S`) over copies of `boot_awdl.kc.i64`. All
addresses are VAs; each one cited as a head was checked with
`ida_funcs.get_func(ea).start_ea == ea` and instruction citations are marked as such. What the
disassembly does not pin down goes with `?`.

## 1. Answer

1. **`qset+0x3C` is not a field of the queue set, but `netif_llink::nll_state`.** The object
   `netif_qset_enqueue` looks at is the `netif_llink` (`qset->nqs_llink`), not the `netif_qset`. It is written **1**
   when creating the link (`0xffffff80009F98CE`) and **2** when destroying it (`0xffffff80009F9C63`). Only value 2
   makes `netif_qset_enqueue` free the chain and return 6 (`0xffffff80009FA2D3`). A live `awdl0`
   has 1: the gate never closes.
2. **`netif_qset_enqueue` is not on the ND/RS path.** Its only caller is `dp_flow_tx_process`
   (`0xffffff80009BA430`, called at `0xffffff80009BAF05`), the TX processing of a flow entry
   (`_flow_entry_alloc`, `0xffffff80009CAEAE`). It corrects what `AWDL_STAGE1_IFUP_PATH.md` §4 said, which
   placed it between `ifnet_enqueue_pkt` and `nxp_queue_tx_push`.
3. **`ifnet+0x78` (120) is `if_refflags`.** It is **1** (`IFRF_EMBRYONIC`) when allocating the interface
   (`0xffffff800063FE18`), **0x0A** when attaching it (`0xffffff80005E9F3D`) and **4** when detaching it
   (`0xffffff80005EB58D`). The check `(& 7) == 2` of `ifnet_enqueue_pkt` is, therefore,
   "interface attached and neither embryonic nor detached" (`IFRF_ATTACHED`).
4. **`ifnet+0xC0` (192) is `if_eflags`.** `ifnet_eflags` (`0xffffff8000640630`) returns that dword. The bit
   `ifnet_enqueue_pkt` requires is **`IFEF_TXSTART` (0x80)**, set by `ifnet_allocate_extended` at
   `0xffffff800063FC0A` when the registration descriptor leaves a non-null TX startup routine.
5. **Both fields are set by IOSkywalkFamily, not by stage 1.** The chain is
   `IOSkywalkNetworkBSDClient::start` (`0xffffff8002AFF562`) →
   `kern_nexus_controller_alloc_net_provider_instance` (`0xffffff80009A4140`), which calls
   `ifnet_allocate_extended` (`0xffffff80009A420D`) and `ifnet_attach` (`0xffffff80009A42DF`). The
   descriptor `start` assembles leaves `pre_enqueue` at zero and writes `0x80000000` into the dword of flags
   (`0xffffff8002AFFA7C`), which makes `ifnet_allocate_extended` install `ifp_if_start`
   (`0xffffff80005E3CE0`) and activate `IFEF_TXSTART`.
6. **With `e50d7cf`, the first ND/RS does reach `nxp_queue_tx_push` and the no-op action.** `IFEF_TXSTART`,
   `if_refflags == 2`, `IFF_UP` and `IOEventSource::enabled` of the queue all pass. The
   `nll_state` check takes no part in this route.
7. **Stage 1 leaves none of those states unset:** it writes none of the three fields and does not
   need to; the only thing it contributes is the enabling of the queues, which it does do.

## 2. The `+0x3C` byte of the set: `nll_state` of the `netif_llink`

The asserts whose string appears in the binary identify the object and the field:

| Literal | VA | Content |
|---|---|---|
| `sub_FFFFFF80009F9310` | `0xffffff80009F9321`, `0xffffff80009F94E4`, `0xffffff80009F9514` | `qset->nqs_llink->nll_state == NETIF_LLINK_STATE_DESTROYED` and `drvq->nq_qset->nqs_llink->nll_state == ...` |

That is, `+0x3C` is `nll_state` of the link, which the set points at in `nqs_llink`. Writers and
readers found:

| Function | VA | Done |
|---|---|---|
| `sub_FFFFFF80009F96E0` | `0xffffff80009F96E0` | Allocates the `netif_llink`; at the end writes **1** into `+0x3C` (`0xffffff80009F98CE`). `nx_netif_llink_add` (`0xffffff80009F9590`) calls it. |
| `sub_FFFFFF80009F9C60` | `0xffffff80009F9C60` | Destroys the link; at the beginning writes **2** into `+0x3C` (`0xffffff80009F9C63`). `nx_netif_llink_remove` (`0xffffff80009FA1B0`) and the undo of `nx_netif_llink_add` on failure call it. |
| `netif_qset_enqueue` | `0xffffff80009FA2A0` | `cmp byte ptr [rdx+3Ch], 2` (`0xffffff80009FA2D3`); if equal, `pp_free_packet_chain` and return 6 (`ENXIO`). |
| `kern_netif_queue_tx_dequeue` | `0xffffff80009E6120` | `if (*((_BYTE *)v8 + 60) == 2) return 6;` with the same object; it is the gate on the driver drain side. |

Values: **1** while the link is published and **2** when it is destroyed. The 0 only exists between
the allocation (zeroed memory) and the write of the 1, before publishing the link. `?` No exhaustive sweep
of writes to `+0x3C` was made over the whole binary; in the functions of the `netif_llink` only the
previous two appear.

Consequence: on a registered `awdl0`, the link is at 1 and `netif_qset_enqueue` returns no `ENXIO`
for this reason. The 2 only appears after `nx_netif_llink_remove`, which corresponds to the
teardown of the interface, not to the first packet.

## 3. `netif_qset_enqueue` is not on the ND/RS path

The function has **a single reference**, a direct call from `dp_flow_tx_process`
(`0xffffff80009BAF05`). `dp_flow_tx_process` registers itself as TX processor of a flow entry in
`_flow_entry_alloc` (`0xffffff80009CAEAE`).

| Step | VA | Done |
|---|---|---|
| `dp_flow_tx_process` | `0xffffff80009BA430` | Processes the chain of a flow entry and calls `netif_qset_enqueue` with the pair `{llink, ifnet}`. |
| `netif_qset_enqueue` | `0xffffff80009FA2A0` | Checks `nll_state`, accounts and **calls `ifnet_enqueue_pkt`** (`0xffffff80009FA3D7`), i.e. injects into the output queue of the `ifnet`. |

The normal submission of the stack follows another path:

| Step | VA | Done |
|---|---|---|
| `ifnet_enqueue` | `0xffffff80005E5ED0` | KPI entry point; it ends in `sub_FFFFFF80005E5F40`. |
| `sub_FFFFFF80005E5F40` | `0xffffff80005E5F40` | Repeats the two state checks (`if_refflags & 7 == 2`, `IFF_UP`) and moves to `sub_FFFFFF80005E6110`. |
| `sub_FFFFFF80005E6110` | `0xffffff80005E6110` | Calls operation `+0x18` of the table pointed at by `ifclassq+0x68` (the `ifclassq` is at `ifp+0x338`, i.e. `+824`), with the `ifclassq` as first argument. |
| `dlil_ifclassq_setup` | `0xffffff80005E8C80` | Installs the `ifclassq` in the `ifnet`; the link stores that same object in its `+0x20` (`0xffffff80009F96E0`). |

Therefore, the `+0x3C` test T3.5 could encounter in front **does not belong to this route**. The
correction affects `AWDL_STAGE1_IFUP_PATH.md` §4 and its risk 4: the only state of this kind the
first ND/RS crosses is the `nll_state` of the link, and it is 1.

## 4. `ifnet+0x78` (120): `if_refflags`

The identification is not nominal: `ifnet_detach` uses the literal `!(ifp->if_refflags & IFRF_EMBRYONIC)` when
checking bit 0 of that dword (`0xffffff80005EB581`), and `ifnet_set_interface_state`
(`0xffffff8000640770`) and `ifnet_get_interface_state` (`0xffffff80006407B0`) return 6 (`ENXIO`) if
`(*(u32 *)(ifp+120) & 7) != 2`.

| Moment | VA | Value written | Meaning |
|---|---|---|---|
| `ifnet_allocate_extended` | `0xffffff800063FE18` | `1` | `IFRF_EMBRYONIC`: it exists but is not attached. |
| `ifnet_attach` | `0xffffff80005E9F33` / `0xffffff80005E9F3D` | `0x10000000A` (qword) | `+120 = 0x0A` (`ATTACHED` + bit 3) and `+124 = 1`. The later check `and eax, 7` (`0xffffff80005E9F53`) gives 2. |
| `ifnet_detach` | `0xffffff80005EB58D` | `(previous & ~7) | 4` | `IFRF_DETACHED`: it stops being attached. |

Independent confirmation: `dlil_attach_flowswitch_nexus` (`0xffffff80005E0060`) requires
`*(u32 *)(ifp+120) == 0x0A` to attach the flow nexus; any other value it treats as "no
attached" and exits without doing anything.

Readers on the path of interest:

| Function | VA | Check |
|---|---|---|
| `ifnet_enqueue_pkt` | `0xffffff80005E65F5`–`0xffffff80005E65FE` | `mov eax, [rdi+78h]; and eax, 7; cmp eax, 2`; if not, frees and returns 6. |
| `sub_FFFFFF80005E5F40` | `0xffffff80005E5F40` | The same condition at the beginning, before looking at `IFF_UP`. |
| `ifnet_set_interface_state` / `get` | `0xffffff8000640770` / `0xffffff80006407B0` | Return 6 if it is not attached. |
| `ifnet_detach` | `0xffffff80005EB430` | Requires bit 1 and rejects 4. |

## 5. `ifnet+0xC0` (192): `if_eflags`

`ifnet_eflags` (`0xffffff8000640630`) returns `*(u32 *)(ifp+192)`. The bit
`ifnet_enqueue_pkt` requires is `IFEF_TXSTART`:

| Writer / reader | VA | Done |
|---|---|---|
| `ifnet_allocate_extended` | `0xffffff800063FC0A` | `lock or dword ptr [r13+0C0h], 80h` when the TX startup routine was left non-null. The same value is checked right after at `0xffffff800063FD1F` with the assert `ifp->if_eflags & IFEF_TXSTART`. |
| `ifnet_attach` | `0xffffff80005E9284` | `test byte ptr [rbx+0C0h], 80h; jnz`: the registration branch with the output (the one installing the TX startup) is only taken if the bit is set. |
| `ifnet_enqueue_pkt` | `0xffffff80005E65EC` | `test byte ptr [rdi+0C0h], 80h`; if missing, frees and returns 6. |
| `nd6_na_output` / `nd6_ns_output` | `0xffffff8000815984` / `0xffffff80008164BE` | They check the same bit before delivering the ND/NA. |
| `ifnet_set_eflags` | `0xffffff80006403B0` | Can set or clear the bit; it takes no part in the registration of `awdl0`. |

## 6. Who sets the states during the registration

The sequence, with the verified calls:

| Step | VA | Done |
|---|---|---|
| `RTLAwdInterface::start` | `AWDLInterface.cpp` | Calls `rtl_vif_start` and then `rtl_eth_register` with the three queues. |
| `IO80211VirtualInterface::start` | `0xffffff80023308AA` | Only calls `IO80211SkywalkInterface::start`; it neither registers the interface nor writes these fields. |
| `IOSkywalkEthernetInterface::registerEthernetInterface` | `0xffffff8002B10A84` | Copies the `RegistrationInfo` and registers the interface with the queue array; the variant with logical link is `0xffffff8002B10B46`. `?` The exact split between the two variants of `registerNetworkInterface` (`0xffffff8002B0A0D6` with queues, `0xffffff8002B0A204` with link) was not fully decompiled. |
| `IOSkywalkNetworkBSDClient::start` | `0xffffff8002AFF562` | Assembles the `ifnet` descriptor, registers the provider of the nexus and calls `kern_nexus_controller_alloc_net_provider_instance` (`0xffffff8002AFFBB5`). |
| descriptor of `ifnet` | `0xffffff8002AFFA7C` | Writes `0x80000000` into the flags dword of the descriptor (`einit+8`); the prior `bzero` of 0x150 bytes leaves `pre_enqueue` (`einit+0x48`) and the rest at zero. |
| `kern_nexus_controller_alloc_net_provider_instance` | `0xffffff80009A4140` | Requires `*(qword *)(einit+0x48) == 0` (`0xffffff80009A41F4`) and `(*(u8 *)(einit+8) & 3) == 0` (`0xffffff80009A41FF`). |
| `ifnet_allocate_extended` | called at `0xffffff80009A420D`; function `0xffffff800063F3F0` | With flags = `0x80000000` and a null `pre_enqueue` it takes the `v5 < 0` branch, installs `ifp_if_start` and activates `IFEF_TXSTART` (`0xffffff800063FC0A`). If that were not the case it would return 22 and `awdl0` would not exist. |
| `nx_create` | called at `0xffffff80009A4251` | Creates the nexus of the netif. |
| `nx_netif_default_llink_config` | called at `0xffffff80009A4310`; function `0xffffff80009FAF20` | Stores the default link configuration at `netif+0x6F8`; it does **not** create the link yet. |
| `ifnet_attach` | called at `0xffffff80009A42DF`; function `0xffffff80005E8D20` | Writes `if_refflags = 0x0A` (`0xffffff80005E9F3D`). |
| `kern_nexus_netif_llink_add` | `0xffffff80009E6360` | Registers the `netif_llink`; the creation (`0xffffff80009F96E0`) leaves `nll_state = 1` (`0xffffff80009F98CE`). |

On the last step: `kern_nexus_netif_llink_add` has two entry routes. The documented one is message
`0xE0060105` of `IOSkywalkNetworkBSDClient::message` (`0xffffff8002B00BA6`, called at
`0xffffff8002B00D69`); the `addLogicalLink` sending that message (`0xffffff8002B0A7DE`, send at
`0xffffff8002B0A879`) is only used by the DriverKit route `IOUserNetworkWLAN::_AddLogicalLink_Impl`
(`0xffffff8002B07CED`), not the kext one. The other is a direct call from the nexus code at
`0xffffff80005FE2D9`, within a region without a function symbol. `?` Which of the two runs the
registration on the `awdl0` path was not pinned; in any case it happens during the registration of the provider or shortly
after, before the interface is published, and the first ND/RS is much later.

## 7. Enabling of the queues by stage 1

`RTLAwdInterface::enable` (`AWDLInterface.cpp:317`) calls `q->enable()` over an `IOEventSource *`, i.e.
via vtable. Cell `+0x160` of `__ZTV26IOSkywalkTxSubmissionQueue` (`0xffffff8002B3E068`)
is not `IOEventSource::enable` (`0xffffff8000ACAA40`), but
`IOSkywalkTxSubmissionQueue::enable` (`0xffffff8002B0CC76`); `+0x168` is
`IOSkywalkTxSubmissionQueue::disable` (`0xffffff8002B0CCD6`). The same happens in the other two:

| Queue | Vtable | `enable` at `+0x160` | `disable` at `+0x168` |
|---|---|---|---|
| TX submission | `0xffffff8002B3E068` | `0xffffff8002B0CC76` | `0xffffff8002B0CCD6` |
| TX completion | `0xffffff8002B406F0` | `0xffffff8002B12B2C` | `0xffffff8002B12B58` |
| RX completion | `0xffffff8002B3E890` | `0xffffff8002B0EFE0` | `0xffffff8002B0F040` |

`IOSkywalkTxSubmissionQueue::enable` takes the gate, and only if the queue is not already enabled calls
`IOEventSource::enable` (via `off_FFFFFF8002B3CDD0`, which holds `0xffffff8000ACAA40`) and afterwards runs an
action of the queue: if there is a `netif_queue` at `+0x78` and bit 2 of `+0x322` is zero, one of the
high cells of the vtable; if there is no `netif_queue`, another one; if there is a `netif_queue` and the bit is set, none. `?`
The exact name of those two cells was not pinned; what matters for this trace is that `+0x28` ends up at
1. The operation is idempotent. The enabled-queue check on submission is the first useful instruction of
`IOSkywalkTxSubmissionQueue::packetSubmission` (`0xffffff8002B2CB2A`):
`test byte ptr [rdi+28h], 1` (`0xffffff8002B2CB3F`), with a jump to the `0xE00002D8` return if it is zero.

## 8. Gate by gate for the first ND/RS

| # | Gate | VA | Check | Value on `awdl0` | Result |
|---|---|---|---|---|---|
| 1 | ND6 output | `0xffffff80008164BE`, `0xffffff8000815984` | `if_eflags & IFEF_TXSTART` | `0x80` set at registration | Passes |
| 2 | `ifnet_enqueue` / `sub_5E5F40` | `0xffffff80005E5ED0` / `0xffffff80005E5F40` | `if_refflags & 7 == 2` | `0x0A` | Passes |
| 3 | `sub_5E5F40` / `ifnet_enqueue_pkt` | `0xffffff80005E6600` | `IFF_UP` (`+0xBE` bit 0) | Set by `ifconfig awdl0 up` | Passes |
| 4 | `ifclassq` | `0xffffff80005E6110` | Linked to the `netif_llink` | Configured at registration | Passes; `?` the exact jump of the `ifclassq` to the `tx_push` of the provider was not walked |
| 5 | `nxp_queue_tx_push` | `0xffffff8002AFF4D1` | `tx_push` operation of the provider, registered at `+0x58` of its table (`0xffffff8002AFF1C5`) | Registered | It runs |
| 6 | `packetSubmission` | `0xffffff8002B2CB3F` | `IOEventSource::enabled` (`+0x28`) | Enabled by `enable()` of stage 1 | Passes |
| 7 | `legacyDequeue` → action | `0xffffff8002B2C860` → `+0x20` of the queue | Non-null action and `+0xCC` | `txNoop` (`AWDLInterface.cpp:255`) | It runs |
| — | `netif_qset_enqueue` | `0xffffff80009FA2A0` | `nll_state == 2` | Not on this route; the link is at 1 | Does not apply |

Original conclusion (corrected): it was taken for granted that the first ND/RS reached the no-op action with the
queues enabled. That jump of the `ifclassq` to `tx_push` does not exist: the enqueuing wakes the
startup thread of the `ifnet`, whose `if_start` is `_ifp_if_start` and purges the `ifclassq`; the action only runs if
the family hits the queue after the enqueuing. The full analysis, with rows 4, 5 and 7 of the previous
table invalidated, is in `AWDL_STAGE1_TXPUSH_LLINK.md` §1 and §10. What was pinned here about `if_refflags`,
`if_eflags` and `nll_state` remains valid.

## 9. What stage 1 leaves unset

| State | Does stage 1 set it? | Is it needed? |
|---|---|---|
| `if_refflags` (`+120`) | No | No: `ifnet_attach` sets it at registration. |
| `if_eflags` / `IFEF_TXSTART` (`+192`) | No | No: `ifnet_allocate_extended` sets it at registration. |
| `nll_state` (`+0x3C` of the link) | No | No: the link creation sets it. |
| `netif_queue` of each queue (`+0x78`) and `+0xCC` | No | No: the set initialization sets them (`nxp_qset_init`, `nxp_queue_init`, `IOSkywalkPacketQueue::initialize`) when registering the provider. |
| `IOEventSource::enabled` (`+0x28`) of the three queues | **Yes** | **Yes**: without it `packetSubmission` returns `0xE00002D8`. |

The override of `enable` still leaves out `IO80211VirtualInterface::setInterfaceEnable` and, with it,
`state+113` and the `IO80211MacAddressAgent`. That affects none of the gates of this route; `?` whether
some consumer of `wifip2pd` requires it remains as stated in `AWDL_STAGE1_IFUP_PATH.md` §2.1.

## 10. Open points

| Point | Status |
|---|---|
| Call of the `ifclassq` to the `tx_push` operation of the provider | Closed by `AWDL_STAGE1_TXPUSH_LLINK.md` §1-§4: that jump does not exist; `tx_push` is only used on the virtual port path. |
| Registration route of the `netif_llink` on the kext path (message `0xE0060105` or nexus call at `0xffffff80005FE2D9`) | `?` Both exist; the message one has only been seen leaving the DriverKit route. |
| Other writes of `+0x3C` outside the functions of the `netif_llink` | `?` No exhaustive sweep was made; in the life cycle of the link only 1 and 2 appear. |
| Meaning of bit 8 of `if_refflags` (0x0A at registration) | `?` The name was not identified; the check of `ifnet_enqueue_pkt` only looks at the three low bits. |
| Exact moment of the link registration relative to `start` | `?` It is known to be within the registration of the provider; the instruction triggering it was not pinned. |

## 11. Address verification

Heads checked with `ida_funcs.get_func(ea).start_ea == ea`:
`0xffffff80009FA2A0`, `0xffffff80009F9590`, `0xffffff80009F96E0`, `0xffffff80009F9C60`,
`0xffffff80009FA1B0`, `0xffffff80009E6360`, `0xffffff80009E6120`, `0xffffff80009BA430`,
`0xffffff80009A4140`, `0xffffff80009FAF20`, `0xffffff80009CA840`, `0xffffff80005E65D0`, `0xffffff80005E5ED0`,
`0xffffff80005E5F40`, `0xffffff80005E6110`, `0xffffff80005E8D20`, `0xffffff80005EB430`,
`0xffffff8000640630`, `0xffffff8000640770`, `0xffffff80006407B0`, `0xffffff800063F3F0`,
`0xffffff80006403B0`, `0xffffff80005E3CE0`, `0xffffff80005E0060`, `0xffffff8002AFF562`,
`0xffffff8002B00BA6`, `0xffffff8002B10A84`, `0xffffff8002B0A204`, `0xffffff8002B0A7DE`,
`0xffffff8002B0CC76`, `0xffffff8002B12B2C`, `0xffffff8002B0EFE0`, `0xffffff8002B2CB2A`,
`0xffffff8002B2C860`, `0xffffff80023308AA`, `0xffffff8000815DD0`, `0xffffff8000815380`.

Instruction citations within a verified head: `0xffffff80009FA2D3` (in
`0xffffff80009FA2A0`), `0xffffff80009F98CE` (in `0xffffff80009F96E0`), `0xffffff80009F9C63` (in
`0xffffff80009F9C60`), `0xffffff80009BAF05` (in `0xffffff80009BA430`), `0xffffff80005E65EC`,
`0xffffff80005E65F5`–`0xffffff80005E65FE`, `0xffffff80005E6600` (in `0xffffff80005E65D0`),
`0xffffff800063FE18`, `0xffffff800063FC0A`, `0xffffff800063FD1F` (in `0xffffff800063F3F0`),
`0xffffff80005E9F33`–`0xffffff80005E9F53`, `0xffffff80005E9284` (in `0xffffff80005E8D20`),
`0xffffff80005EB581`, `0xffffff80005EB58D` (in `0xffffff80005EB430`), `0xffffff80009A41F4`,
`0xffffff80009A41FF`, `0xffffff80009A420D`, `0xffffff80009A4251`, `0xffffff80009A42DF`,
`0xffffff80009A4310` (in `0xffffff80009A4140`), `0xffffff8002AFFA7C`, `0xffffff8002AFFAAF`,
`0xffffff8002AFFBB5` (in `0xffffff8002AFF562`), `0xffffff8002B00D69` (in `0xffffff8002B00BA6`),
`0xffffff8002B0A879` (in `0xffffff8002B0A7DE`), `0xffffff8002B2CB3F` (in `0xffffff8002B2CB2A`),
`0xffffff80009CAEAE` (in `0xffffff80009CA840`), `0xffffff80008164BE` (in `0xffffff8000815DD0`),
`0xffffff8000815984` (in `0xffffff8000815380`).

Vtables read as data: `0xffffff8002B3E068`, `0xffffff8002B406F0`, `0xffffff8002B3E890`; the global pointer
`0xffffff8002B3CDD0` holds `0xffffff8000ACAA40` (`IOEventSource::enable`).
