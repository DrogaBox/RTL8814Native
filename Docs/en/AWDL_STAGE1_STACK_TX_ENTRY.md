# TX entry of the stack into `awdl0`: from `dlil_output` to the `ifclassq` and to the Skywalk queues

Closes the two `?`s of `Docs/AWDL_STAGE1_TXPUSH_LLINK.md` §10: which function receives the mbuf the BSD stack
sends over `awdl0`, whether the `ifclassq` is on that path and what reaches `IOSkywalkTxSubmissionQueue` and
`kern_netif_queue_tx_dequeue`. Everything was read in IDA headless over copies of `boot_awdl.kc.i64`; nothing
was linked, loaded nor run.

## 1. Answer

The stack does not call `ifnet_enqueue_pkt`. The mbuf enters via `ifp->if_output`, which
`ifnet_allocate_extended` fixes at `_dlil_output_handler` (`0xFFFFFF800063FC4F`–`0xFFFFFF800063FC56`), and
`_dlil_output_handler` (`0xFFFFFF80005EF500`) jumps to the pointer of `ifp+0x258` (`0x5EF504`, `jmp rax` at `0x5EF50C`).
That pointer is `_ifnet_enqueue` (`0xFFFFFF80005E5ED0`), installed by the same registration
(`0xFFFFFF800063FC22`–`0xFFFFFF800063FC30`).

`ifnet_enqueue` builds a list of packets and calls `sub_FFFFFF80005E5F40` (`0xFFFFFF80005E5F16`),
which in turn calls `sub_FFFFFF80005E6110` (`0xFFFFFF80005E6008`). It is the same `ifclassq` function
already documented: diversion by `[ifp+0x900]` (`0x5E6133`), enqueuing into `fq_if_enqueue`
(`0x5E6172`) and waking the startup thread (`0x5E61B2`–`0x5E6284`).

Therefore **yes**, the `ifclassq` is on the output path of the stack for this `ifnet`, and the diversion by
`[ifp+0x900]` and the purge of `_ifp_if_start` are relevant. `[ifp+0x900]` is the head of a `netem`
queue **embedded** in the `ifnet`: `sub_5E6110` passes `&ifp+0x900` to `_netem_enqueue` (`0xFFFFFF80005E62A3`,
`0xFFFFFF8000718340`), and with the queue empty the first packet takes the `ifclassq` path.

None of that path reaches the Skywalk queues. `IOSkywalkTxSubmissionQueue::packetSubmission`
(`0xFFFFFF8002B2CD68`) is only invoked from `enable` (`0xFFFFFF8002B0CCB2`), `checkForWork`
(`0xFFFFFF8002B2C82A`) and `nxp_tx_qset_notify` (`0xFFFFFF80009E24F8`). `nxp_tx_qset_notify` is called
from `nx_netif_na_txsync` (`0xFFFFFF80009E2440`), and that function only appears as **data** at
`0xFFFFFF80009E0E9F` (pointer installed into a callback table); it is the provider route, not the
stack one.

## 2. The TX entry of the stack

| Step | Instruction | Function | Done |
|---|---|---|---|
| 1 | `0xFFFFFF800063FC4F`–`0xFFFFFF800063FC56` | `ifnet_allocate_extended` `0xFFFFFF800063F3F0` | `[ifp+0x248] = _dlil_output_handler` (`if_output`). |
| 2 | `0xFFFFFF80005EF504`, `0xFFFFFF80005EF50C` | `_dlil_output_handler` `0xFFFFFF80005EF500` | `rax = [ifp+0x258]` and `jmp rax` with the mbuf. |
| 3 | `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30` | `ifnet_allocate_extended` | `[ifp+0x260] = [ifp+0x258] = _ifnet_enqueue` (if `+0x260` was zero). |
| 4 | `0xFFFFFF80005E5ED8`–`0xFFFFFF80005E5F16` | `_ifnet_enqueue` `0xFFFFFF80005E5ED0` | Walks the chain `[m+8]` and sums `[m+0x60]`; calls `sub_FFFFFF80005E5F40` with the list. |
| 5 | `0xFFFFFF80005E6008` | `sub_FFFFFF80005E5F40` `0xFFFFFF80005E5F40` | Calls `sub_FFFFFF80005E6110` per packet. |
| 6 | `0xFFFFFF80005E6133` | `sub_FFFFFF80005E6110` | `[ifp+0x900] != 0` → `_netem_enqueue` (`0x5E6343`); else continues to the `ifclassq`. |
| 7 | `0xFFFFFF80005E6172` | `sub_FFFFFF80005E6110` | `call [ifclassq+0x68]+0x18` = `fq_if_enqueue` (`0x713020`). |
| 8 | `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284` | `sub_FFFFFF80005E6110` | Wakes the startup thread (`&ifp+0x330`). |

`ifnet_enqueue_pkt` (`0xFFFFFF80005E65D0`) is **not** the entry of the stack: its callers are
`nx_netif_host_output` (`0xFFFFFF80009EABD5`, `0xFFFFFF80009EAC03`), `netif_transmit`
(`0xFFFFFF80009EB0B7`), `sub_FFFFFF80009F86C0` (`0xFFFFFF80009F8E8B`), `netif_qset_enqueue`
(`0xFFFFFF80009FA3D7`), `sub_FFFFFF80009BB780` (`0x9BB80D`), `sub_FFFFFF80009F4F60` (`0x9F506E`) and the
region `0x606849`/`0x606899`. It is the **provider injection** route (and that of `tx_push`), not the one of
`dlil_output`.

## 3. What reaches the Skywalk queues

| Entry into `packetSubmission` | Instruction | Origin |
|---|---|---|
| `enable` | `0xFFFFFF8002B0CCB2` | `IOSkywalkTxSubmissionQueue::enable`; in stage 1 it runs when enabling the queues, with the queue empty. |
| `checkForWork` | `0xFFFFFF8002B2C82A` | `IOEventSource`; only with the counter `+0x214` changed. |
| `notify` | `0xFFFFFF80009E24F8` | `nx_netif_na_txsync` → `[provider+0x90]` (`nxp_tx_qset_notify`); the provider invokes it. |

None of the three runs from `dlil_output`, `ifnet_enqueue`, `sub_5E5F40` nor `sub_5E6110`. The
sequence that does reach the action is the pull of §4 of `AWDL_STAGE1_TXPUSH_LLINK.md`:
`packetSubmission` (`0x2B2CD68`) → `refillTxRingForNetworking` (`0x2B0D3E0`) → `kern_netif_queue_tx_dequeue`
(`0x2B0D41F` → `0x9E6120`) → `legacyDequeue` (`0x2B2C860`).

## 4. The startup thread on this path

Step 8 wakes thread `sub_FFFFFF80005EA850` (created in `ifnet_attach`, `0x5E9693`), which calls
`ifp->if_start` (`0x5EB1F7`). For this interface `if_start` is `_ifp_if_start`
(`AWDL_STAGE1_TXPUSH_LLINK.md` §7): it checks `IFEF_TXSTART` (`0x5E3CE9`), takes `if_snd`
(`0x5E3CF4`) and jumps to `if_qflush` (`0x5E3CFC`).

`if_qflush` (`0x5D5730`) has an early exit: if `[ifp+0x78] & 7 == 0` it does not purge
(`0x5D5750`–`0x5D5755`); else it calls the purge operation of the `ifclassq` (`[ifq->ops+0x30]`,
`0x5D5796`). With `awdl0` enqueuing through `fq_codel`, the purge walks the flow queues and drops the ND.

## 5. Does the first ND reach `txNoop`?

| # | Step | Evidence |
|---|---|---|
| 1 | The stack delivers the mbuf to `_dlil_output_handler` | `ifp+0x248`, `0x63FC56`; `0x5EF504`. |
| 2 | `_ifnet_enqueue` assembles the list | `0x5E5ED0`, called at `0x5E5F16`. |
| 3 | `sub_5E5F40` → `sub_5E6110` | `0x5E6008`. |
| 4 | `[ifp+0x900] == 0` → `fq_if_enqueue` | `0x5E6133`, `0x5E6172`; `+0x900` is an embedded queue that starts empty. `?` The runtime value cannot be pinned without hardware. |
| 5 | The startup thread is ignited | `0x5E61B2`–`0x5E6284`; thread created at `0x5E9693`. |
| 6 | The thread calls `_ifp_if_start` | `0x5EB1F7`; `0x5E3CE0`. |
| 7 | `_ifp_if_start` purges | `0x5E3CFC`; `0x5D5796` (if `[ifp+0x78]&7 != 0`). |
| 8 | Nobody calls `packetSubmission` after the enqueuing | Only `enable`, `checkForWork` and `notify`, and none of them runs on this path. |

Answer: **no**. The ND enters the `ifclassq`, the enqueuing wakes the startup thread and the thread runs
`_ifp_if_start`, which purges the queue. The action of the queue (`txNoop`) does not run; the instruction that
could trigger it is `kern_netif_queue_tx_dequeue` in `refillTxRingForNetworking` (`0x2B0D41F`), and it is only
reached from `packetSubmission`, which stage 1 does not call again.

The condition deciding the observable detail: if `[ifp+0x78] & 7 == 0`, `if_qflush` does not purge and the packet
stays in the `ifclassq`; else it is dropped. In both cases the action does not run. `?` The value of
`ifp+0x78` on `awdl0` cannot be pinned without hardware.

## 6. What the driver has to do in stage 2

| Need | What to call | Evidence |
|---|---|---|
| Bring packets to the action | `IOSkywalkTxSubmissionQueue::packetSubmission(bool)` on each submission queue | `0x2B2CD68`; `legacyDequeue` at `0x2B2C860`; action with `call r12` at `0x2B2C8DE` |
| Provider alternative | `nxp_tx_qset_notify` (`[provider+0x90]`) via `nx_netif_na_txsync` | `0x9E2440`, `0x9E24F8` |
| Avoid the purge | drain before the startup thread runs `_ifp_if_start`, or do not enqueue into the `ifclassq` | `0x5E6284`, `0x5EB1F7`, `0x5E3CFC` |

Stage 2 cannot change `if_start`: field `ifp+0x250` is fixed by `ifnet_allocate_extended` and the nexus
requires `einit+0x48 == 0` (`0x9A41F4`), so it always ends up `_ifp_if_start`. The workable route is for
the driver, on its own workloop and after each enqueuing, to call `packetSubmission` before the
startup thread reaches `if_qflush`; `?` whether the family guarantees that order cannot be pinned from the
disassembly and is what the hardware test must measure.

## 7. Corrections to `AWDL_STAGE1_TXPUSH_LLINK.md`

| Point | Correction |
|---|---|
| §10, step 2 | `[ifp+0x900]` is not a pointer to an external object: it is a `netem` queue embedded in the `ifnet` (`&ifp+0x900` is the argument of `_netem_enqueue`, `0x5E62A3`). Empty at registration, the first packet goes to the `ifclassq`. |
| §4, "the pull" | The pull is not the only path to the action by being the only gate: additionally, the stack **does** go through the `ifclassq`; what it does not do is call `packetSubmission`. |
| §1 | Confirmed: `ifnet_enqueue_pkt` takes no part in the stack output; its role is provider injection (`nx_netif_host_output`, `netif_transmit`, `tx_push`). |

## 8. Address verification

Heads checked with `ida_funcs.get_func(ea).start_ea == ea`:
`0xFFFFFF80005E5ED0`, `0xFFFFFF80005E5F40`, `0xFFFFFF80005E6110`, `0xFFFFFF80005E65D0`,
`0xFFFFFF80005EF500`, `0xFFFFFF80005F32B0`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005D5730`,
`0xFFFFFF800063F3F0`, `0xFFFFFF8002B0CC76`, `0xFFFFFF8002B2CD68`, `0xFFFFFF8002B2C746`,
`0xFFFFFF8002B0D3E0`, `0xFFFFFF8002B2C860`, `0xFFFFFF80009E2440`, `0xFFFFFF80009E6120`,
`0xFFFFFF80009E5510`, `0xFFFFFF80009EA590`, `0xFFFFFF80009F86C0`, `0xFFFFFF80009FA2A0`,
`0xFFFFFF80009F4F60`, `0xFFFFFF80009BB780`, `0xFFFFFF8000713020`, `0xFFFFFF8000718340`,
`0xFFFFFF80005EA850`, `0xFFFFFF80005EB0F0`, `0xFFFFFF80009A4140`, `0xFFFFFF80005E8D20`.

Instruction citations within a verified head: `0xFFFFFF800063FC4F`–`0xFFFFFF800063FC56`,
`0xFFFFFF800063FC16`–`0xFFFFFF800063FC30`, `0xFFFFFF80005EF504`, `0xFFFFFF80005EF50C`, `0xFFFFFF80005E5ED8`–`0xFFFFFF80005E5F16`,
`0xFFFFFF80005E6008`, `0xFFFFFF80005E6133`, `0xFFFFFF80005E6172`, `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284`,
`0xFFFFFF80005E62A3`, `0xFFFFFF80005E6343`, `0xFFFFFF80005E3CE9`, `0xFFFFFF80005E3CF4`,
`0xFFFFFF80005E3CFC`, `0xFFFFFF80005D5750`–`0xFFFFFF80005D5755`, `0xFFFFFF80005D5796`,
`0xFFFFFF80005EB1F7`, `0xFFFFFF80005E9693`, `0xFFFFFF80009E24F8`, `0xFFFFFF8002B0CCB2`,
`0xFFFFFF8002B2C82A`, `0xFFFFFF8002B2C8DE`, `0xFFFFFF8002B0D41F`, `0xFFFFFF80009A41F4`,
`0xFFFFFF80009EABD5`, `0xFFFFFF80009EAC03`, `0xFFFFFF80009EB0B7`, `0xFFFFFF80009F8E8B`,
`0xFFFFFF80009FA3D7`, `0xFFFFFF80009F506E`.

Data read: pointer to `_ifnet_enqueue` at `ifp+0x258`/`ifp+0x260` (`0x63FC29`, `0x63FC30`); pointer to
`_dlil_output_handler` at `ifp+0x248` (`0x63FC56`); reference to `nx_netif_na_txsync` at
`0xFFFFFF80009E0E9F`.
