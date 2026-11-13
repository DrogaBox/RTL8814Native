# Stage 2b: from the stack mbuf to the `IOSkywalkPacket` of `awdl0`

Closes the open point of steps 3–4 of §6 of `Docs/AWDL_STAGE1_STAGE2_PUSH_DESIGN.md`: what exactly
the project's own `if_output` (`rtlAwdOutput(__ifnet *, __mbuf *)`, installed by field
`+0x40` of `ifnet_init_eparams` from cell `0x8E0`) must do so the frame the stack hands it reaches
the action of the `IOSkywalkTxSubmissionQueue` of `awdl0`. The answer corrects the design: the
`packetSubmission(false)` kick does not work and the queue does not accept packets from the driver;
the pattern with support in the
disassembly is the direct call to the action with packets of its own pool, which Broadcom already
uses for
AWDL multicast. Base: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin
25.6.0,
md5 `f489ff8be80e6013a1bf47561dcaef45`), reading of the disassembly, the pseudocode and vtable
dumps over a copy of the annotated database of the kernelcache, and symbol dumps of
`Docs/re/io80211_25.6.0/`.
Addresses = KC VA. Nothing has been linked, loaded or run; anything unseen carries `?`.

## 1. Answer

**There is no path at all to «enqueue» a driver packet into the `IOSkywalkTxSubmissionQueue`.** The
send queue is only filled from the kernel:

1. `enqueuePackets` is a **pure** virtual of `IOSkywalkPacketQueue` (cells `+0x2A0`/`+0x2A8` of its
   vtable at zero) and the send subclass **does not implement it**: in
   `__ZTV26IOSkywalkTxSubmissionQueue`
   those cells contain `purgePackets` (`0xFFFFFF8002B0D870`) and `initWithPool`
   (`0xFFFFFF8002B0CAAC`).
   The existing implementations of `enqueuePackets` (RX/TX completion, RX submission) belong to
   opposite-direction queues.
2. `IOSkywalkTxSubmissionQueue::packetSubmission(bool)` (`0xFFFFFF8002B2CD68`) pulls the packets in
   its two «networking» modes with the **same** kernel call,
   `kern_netif_queue_tx_dequeue` (`0xFFFFFF80009E6120`): `refillTxRingForNetworking`
   (`0xFFFFFF8002B0D3E0`, called at `0xFFFFFF8002B0D41F`) for ring mode and
   `getPacketListForNetworking` (`0xFFFFFF8002B0D57C`, called at `0xFFFFFF8002B0D5BC`) for list
   mode. The «list» (head/tail in the qwords `+0x108`/`+0x110`, count in dword `+0x150`) is only
   filled from that same kernel dequeue, never from the driver.
3. `kern_netif_queue_tx_dequeue` drains the `ifclassq` of the `ifnet` (`ifclassq_dequeue`,
   `0xFFFFFF800070E9D0`): the mbufs the driver could put there get converted to `kern_packet`
   inside the kernel. There is no `kern_netif_queue_tx_enqueue`: in the kernel table only
   `kern_netif_queue_rx_enqueue` (`0xFFFFFF80009E5FD0`), `kern_netif_queue_tx_dequeue`
   (`0xFFFFFF80009E6120`) and `kern_netif_queue_get_service_class` (`0xFFFFFF80009E6490`) exist, all
   in
   `com.apple.kpi.private`, like `ifnet_enqueue` (`0xFFFFFF80005E5ED0`) — they do not link from a
   third-party kext (§7), so not even the «re-enqueue into the `ifclassq` and kick» path is
   linkable.
4. Additionally, `packetSubmission` only pulls when there are kernel notifications: the volume is
   computed as `dword[q+0x130] − xchg dword[q+0x134]` (`0xFFFFFF8002B2CE1D`–`0xFFFFFF8002B2CE23`);
   with both at zero (driver queue with no kernel traffic) it does not even reach the dequeue call
   and returns
   `0xE00002DB` (`0xFFFFFF8002B2CF11`).

**The pattern with support in the disassembly is the direct call to the action.** Broadcom uses it
for the
AWDL multicast: `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::p2pMulticastTx(IO80211NetworkPacket**,
unsigned int)` (`0xFFFFFF8001541D64`) loads `vtable+0x310` (its `dequeuePackets`, the action
registered at
the factory) and calls it with `(this, this, packets, n, 0)` — packets it itself took from the
pool. The RX path of the layer's own code (`RTLAirLayer.cpp::rxFrames`) already applies the inverse
half of the same
contract: `allocatePacket` + `prepare` + copy + `setDataOffsetAndLength` + completion queue.

**Minimal sequence of `rtlAwdOutput`** (detail in §5): convert the mbuf into a packet of the TX pool
(`allocatePacket` + `prepare` + copy + `setDataOffsetAndLength`), free the mbuf with `mbuf_freem` and
call the queue action directly with the packet array; the action transmits and returns the
packets via the TX completion queue (mechanics already written in `txNoop`,
`Source/AirLayer/AWDLInterface.cpp:255`).
The return value toward dlil is an errno: 0 = accepted. The `packetSubmission` of the previous
design is superfluous.

## 2. The normal path: from the stack mbuf to the action

Table of the pipeline the family mounts for a provider `ifnet` (all heads verified,
§10):

| Step | Evidence | Fact |
|---|---|---|
| Entry | `_dlil_output_handler` `0xFFFFFF80005EF500` (14 bytes) | Calls `[ifp+0x258]` and returns its result as-is. |
| Enqueue | `ifnet_enqueue` → `ifclassq` | The mbufs enter the `ifclassq` as type 1 items (mbuf chain, link at `+8`); type 2 (packet chain, link at `+168`) coexists in the same code (`sub_FFFFFF800070EA80`, with the assertions of `xnu/bsd/net/classq/classq_subr.c`). |
| Kick | `nx_netif_na_txsync` → `nxp_tx_qset_notify` → `[vtable+0x1D8](queue, 1)` | `packetSubmission(true)`, the variant the kernel uses (already in `AWDL_STAGE1_TXPUSH_LLINK.md` §4). |
| Kernel dequeue | `kern_netif_queue_tx_dequeue` `0xFFFFFF80009E6120` | Link state gate: `cmp byte [r9+3Ch], 2` → return 6 (`ENXIO`) at `0xFFFFFF80009E614E`; second test `cmp byte [rsi+3Ch], 1` at `0xFFFFFF80009E6202`. Calls `ifclassq_dequeue` and returns the first handle in the output qword. |
| Conversion to a family packet | `refillTxRingForNetworking` | Per handle: `IOSkywalkPacketBufferPool::getPacketWithHandle(pool, handle)` (`0xFFFFFF8002B0589C`, indexes the tracker with `kern_packet_get_object_index`), packet slot `+0x1D8` (`acquireWithPacketHandle(handle, 0)`, call at `0xFFFFFF8002B0D486`), slot `+0x188` (`prepareWithQueue(queue, 1, 0)`, call at `0xFFFFFF8002B0D49C`), optionally trace/BPF, and `IOSkywalkPacketTable::producePacket` (`0xFFFFFF8002B09756`, call at `0xFFFFFF8002B0D4FA`). |
| Delivery | `legacyDequeue` `0xFFFFFF8002B2C860` | Requires `byte+0x28 & 1` (enabled) and `byte+0xCC & 1` (`0xFFFFFF8002B2C87B`); loads the action from `[this+0x20]` (`0xFFFFFF8002B2C888`) and calls it with `(owner, this, table+8*index, pending, refCon)` — a contiguous portion of the packet table array (`+0xE8`, `shl rdx,3; add rdx,[rcx+8]` at `0xFFFFFF8002B2C8C1`–`0xFFFFFF8002B2C8C5`). The returned count advances the read index; 0 leaves the stall bit at `+0x13C`. |

The mbuf→`kern_packet` copy and the mbuf free happen inside the kernel subtree
(`ifclassq_dequeue` → `sub_FFFFFF800070EA80` → `sub_FFFFFF800070EE10` and the discipline operation);
the concrete instruction of each was not located (`?`). For the stage 2b design it does not matter:
that path stays out when the project's own `output` replaces `_ifnet_enqueue`.

## 3. Closed paths, with evidence

| Path | Why not |
|---|---|
| `enqueuePackets` on the send queue | Pure virtual of the base (`__ZTV20IOSkywalkPacketQueue` cells `+0x2A0`/`+0x2A8` = 0); the TX subclass puts `purgePackets`/`initWithPool` there. No symbol `IOSkywalkTxSubmissionQueue::enqueuePackets` exists in the entry dump (§7). |
| `packetSubmission(false)` as «notify the queue» | Requires an enabled EventSource (`byte+0x28 & 1`, `0xFFFFFF8002B2CDC6`) and the start bit `byte+0xCC & 1` (`0xFFFFFF8002B2CDCE`); state `+0x13C & 5` → `0xE00002D5`. With both bits right, the volume is `dword+0x130 − xchg dword+0x134`; without kernel notifications it returns `0xE00002DB` without pulling (`0xFFFFFF8002B2CF11`). And even if it pulled, it would take from the kernel `ifclassq`, not driver memory. |
| List mode (factory option `0x8`) | Only changes the consumer: `byte+0x142 & 2` (`0xFFFFFF8002B2CEBE`) diverts to `getPacketListForNetworking`, which calls the same `kern_netif_queue_tx_dequeue` (`0xFFFFFF8002B0D5BC`) and chains what the kernel returns. |
| `kern_netif_queue_tx_enqueue` | Does not exist in the KC (only `rx_enqueue`/`tx_dequeue`/`get_service_class`, all `com.apple.kpi.private`). |
| Re-enqueue into the `ifclassq` (`ifnet_enqueue`) and kick | `ifnet_enqueue` is in `com.apple.kpi.private` (`0xFFFFFF80005E5ED0`): does not link (§7). |
| Family mbuf→packet converter | Does not exist: the mbuf pull of the PeerManager, `IO80211VirtualInterface::dequeueTxPackets` (`0xFFFFFF80023313B2`), has not a single code reference in the KC (xref sweep empty) and its mbufs are meant to go straight to the radio. |

Who writes the start bit (`byte+0xCC`) remains unclosed: a byte sweep over the whole text
of the `IOSkywalkFamily` entry (`0xFFFFFF8002AFE000`–`0xFFFFFF8002B46000`) found no write to
that
displacement; the writer is presumably in the kernel when attaching the queue to the `netif_queue`
(`?`).
It does not affect the direct path (§5), which does not go through `legacyDequeue`; it does explain
that in stage 1 no kick of the family could move a driver queue.

## 4. The pattern with support: direct action

`p2pMulticastTx` (`0xFFFFFF8001541D64`, verified head; bytes in §10):

```
0xffffff8001541d64  push    rbp
0xffffff8001541d65  mov     rbp, rsp
0xffffff8001541d68  mov     ecx, edx              ; count
0xffffff8001541d6a  mov     rdx, rsi              ; packets
0xffffff8001541d6d  mov     rax, [rdi]            ; vptr
0xffffff8001541d70  mov     rax, [rax+310h]       ; dequeuePackets (the action)
                    mov rsi, rdi ; xor r8d, r8d ; pop rbp ; jmp rax   ; tail call with (this, this, packets, count, 0)
```

That is: the family offers no gate to put packets into the send queue, but **the action belongs to the
driver** and calling it directly with packets of its own pool is exactly what the Broadcom subclass
does. The direct packet never goes through the packet table nor `acquireWithPacketHandle` (there is
no external handle to bind): it is a normal pool packet, with its full lifecycle managed by the
APIs the layer already links (§7). The RX half of the same contract already runs in production on
the STA interface
(`RTLAirLayer.cpp::rxFrames`: `allocatePacket` → `prepare` → copy → `setDataOffsetAndLength` →
`enqueuePackets` in batches).

## 5. Minimal sequence of `rtlAwdOutput(__ifnet *, __mbuf *)`

| Step | Call (linkable symbol, §7) | Evidence and notes |
|---|---|---|
| 0 | Guards: datapath enabled, `_txPool`/`_txSubQ` non-null | The handler runs on the stack thread (dlil), not the work loop. |
| 1 | `IOSkywalkPacketBufferPool::allocatePacket(&pkt, 1)` | `0xFFFFFF8002B058EE`: delegates to slot `+0x130` of the pool (the concrete alloc). Option 1 is the non-sleeping `alloc` of `kern_pbufpool`; with the pool full it returns an error → free the mbuf and return errno. |
| 2 | `IOSkywalkPacket::prepare(pkt, _txSubQ, 1, 0)` | `0xFFFFFF8002B0658A`: if `byte+0x58 & 2` is already prepared it returns 0; otherwise it calls `prepareWithQueue` (slot `+0x188`) with the direction `getDirection` reads from the queue (`[queue+0xB4]`, `0xFFFFFF8002B08EEC`) — 1 for a send queue. Without `prepare` the packet has no buffer table (`getDataVirtualAddress` fails; proven on the STA layer) and could not be completed either: `complete` requires the same bit (`byte+0x58 & 2`, `0xFFFFFF8002B066AE`). |
| 3 | Copy of the frame into the packet buffer | Destination: `IOSkywalkPacket::getDataVirtualAddress()` + chosen offset; source: the mbuf with `mbuf_copydata` (walks the chain). Recommended offset: 64, the same headroom the `RegistrationInfo` carries at `+0x3C` (`Docs/AWDL_STAGE1_LIFECYCLE_CHECKS.md`) and that the Broadcom bus descriptor copies into the pool tracker; it leaves room to prepend a link header without a second copy. `?` the exact offset the kernel uses in its own conversion (§2) has not been traced to an instruction. |
| 4 | `IOSkywalkPacket::setDataOffsetAndLength(64, len)` | `0xFFFFFF8002B2C538`: validates `off ≤ buffer size` and `off+len ≤ buffers×size` (the size is in the pool descriptor, field `+48`); without buffers returns `0xE00002F0`. With `off = 64` the action reads the frame at `base + 64` (`getDataVirtualAddress + getDataOffset`). |
| 5 | (optional) `IOSkywalkNetworkPacket::setLinkHeaderLength(pkt, 14)` | `0xFFFFFF8002B07180`: delegates to `kern_packet_set_link_header_length`; the kernel uses it for header offload. |
| 6 | `mbuf_freem(m)` | On every path, success and failure (§6). |
| 7 | Direct call to the action: `action(owner, _txSubQ, &pkt, 1, refCon)` | Same signature `legacyDequeue` uses (`0xFFFFFF8002B2C888`): `(owner=[queue+0x18], queue, IOSkywalkPacket* const*, count, refCon=[queue+0x58])`. In stage 1 the action is `txNoop` (`AWDLInterface.cpp:255`); the real action of stage 2b replaces it. The call is synchronous on the stack thread; if the radio requires the work loop context, the handler must jump with `IOWorkLoop::runAction` and retain the packet until the jump (`?` radio design decision, not fixed by the KC). |
| 8 | The action transmits and returns the packets | Via `IOSkywalkTxCompletionQueue::enqueuePackets(const IOSkywalkPacket*, uint, uint)` (`0xFFFFFF8002B2DBB6`, batches with count < capacity of dword `+0xAC`) and returns the consumed count. The completion (`completePackets`, `0xFFFFFF8002B12B9E`): per packet `getDataLength` (slot `+0x140`, `0xFFFFFF8002B12BDD`), `getSourceQueue()` (`0xFFFFFF8002B12BE9`) and, if there is a source queue, `notifySrcQueue(false, source, length)` — which lands in the send queue's `packetCompletion` (`0xFFFFFF8002B0D758`: only counters and a periodic `signalWorkAvailable`); then packet slot `+0x198` (`completeWithQueue`) and, if the queue has a table (`+0xE8`), `kern_packet_tx_completion(handle)` with the `kern` handle the packet carries at `+0x28` since `allocatePacket`. At the end of the batch it returns the packets to the pool via slot `+0x148` of the pool vtable (the signature of `deallocatePackets`, `0xFFFFFF8002B2C128`) and calls `notifySrcQueue(true, nullptr, 0)`. The full cycle works on the STA interface. |
| 9 | `return 0` | Acceptance errno (§6). |

Codes the queue returns in case the driver consults state or uses `requestDequeue(void*, unsigned int)`
(`0xFFFFFF8002B2CA7C`; Broadcom calls it from `enableDatapath`): without a `netif_queue` (`+0x78`
null,
`0xFFFFFF8002B2CA9A`) it returns `0xE00002D8`; with one it calls `packetSubmission(false)` through
slot
`+0x1D8` (`0xFFFFFF8002B2CAB4`). `packetSubmission(bool)` returns: `0xE00002D8` (not enabled, path
`true`), `0xE00002D7` (path `false`: not enabled `0xFFFFFF8002B2CDC0` or without the start bit
`0xFFFFFF8002B2CDCE`), `0xE00002D5` (state `+0x13C & 5`), `0xE00002DB` (no kernel notifications,
`0xFFFFFF8002B2CF11`). `enable` (`0xFFFFFF8002B0CC76`) only calls slot `+0x1D8` if `+0x78` is non-null
and
`+0x142 & 4 == 0` (`0xFFFFFF8002B0CC9A`–`0xFFFFFF8002B0CCB2`); `checkForWork` (`0xFFFFFF8002B2C746`)
calls the same slot with 0.

## 6. mbuf lifetime and return value

- **The mbuf is the property of the `if_output` from the call onward.** Binary evidence: the
  family's own handler frees it itself — `IOSkywalkNetworkBSDClient::if_output`
  (`0xFFFFFF8002B002CE`) does
  `mbuf_freem` (`0xFFFFFF8002B002DA`) and returns 19 (bytes `b8 13 00 00 00` at
  `0xFFFFFF8002B002E0`) — and
  the normal path delivers the mbuf to the `ifclassq`, from where the kernel frees it after
  converting it (`?` instruction not located, §2). No later segment looks at the mbuf again.
- **Its own handler must always free** (`mbuf_freem` frees the whole chain): after the copy to
  the packet (success), and on every failure (pool full, failed `prepare`, returned packet). The
  needed KPIs
  (`mbuf_freem`, `mbuf_copydata`, `mbuf_pkthdr_len`, `mbuf_nextpkt`, `mbuf_setnextpkt`) are in
  `com.apple.kpi.bsd` (§7).
- **The return is an errno**: the thunk `_dlil_output_handler` (`0xFFFFFF80005EF500`) returns the
  handler value as-is and the family handler returns 19 (`ENODEV`) when dropping. 0 = frame
  accepted;
  non-zero = errno of the failure (with the mbuf already freed). `?` the fine handling of the errno
  in
  `dlil_output` (who consumes it) was not pursued; for the design the family handler's contract
  suffices.

## 7. Linkable symbols

`Tools/awdl_recheck_symbols.py` with the five dumps of `Docs/re/io80211_25.6.0/` and the stage 2b
candidate list: **142 of 143 OK**, all defined with type `0x0f` in the entry
`com.apple.iokit.IOSkywalkFamily` (or already imported by the built binary). The only MISSING was an
error of the test list (`8setNextPacket` instead of `13setNextPacket`); the real symbol
`__ZN15IOSkywalkPacket13setNextPacketEPS_` is at `0xFFFFFF8002B068E2`, `0x0f`.

| Role in the handler | Symbol | Address |
|---|---|---|
| Create send queue with action (already linked) | `IOSkywalkTxSubmissionQueue::withPool(pool, cap, flags, owner, action, refCon, options)` | `0xFFFFFF8002B0CA86` |
| Take a packet from the pool | `IOSkywalkPacketBufferPool::allocatePacket(IOSkywalkPacket**, uint)` | `0xFFFFFF8002B058EE` |
| Prepare the packet | `IOSkywalkPacket::prepare(IOSkywalkPacketQueue*, u64, uint)` | `0xFFFFFF8002B0658A` |
| Data and lengths | `getDataVirtualAddress` / `getDataOffset` / `getDataLength` / `getBufferSize` / `getPacketBufferCount` / `setDataOffsetAndLength` | `0xFFFFFF8002B06518` / `0xFFFFFF8002B2C5E2` / `0xFFFFFF8002B064D0` / `0xFFFFFF8002B064B8` / `0xFFFFFF8002B064C6` / `0xFFFFFF8002B2C538` |
| Link header (optional) | `IOSkywalkNetworkPacket::setLinkHeaderLength(uint8)` | `0xFFFFFF8002B07180` |
| Return the packet to the pool on pre-action failures | `IOSkywalkPacketBufferPool::deallocatePacket(IOSkywalkPacket*)` | `0xFFFFFF8002B2C0DE` |
| Return after transmitting | `IOSkywalkTxCompletionQueue::enqueuePackets(const IOSkywalkPacket*, uint, uint)` | `0xFFFFFF8002B2DBB6` |
| Free the mbuf | `mbuf_freem` (`com.apple.kpi.bsd`) | `0xFFFFFF80008DF8E0` |
| Copy from the mbuf | `mbuf_copydata`, `mbuf_pkthdr_len`, `mbuf_nextpkt`, `mbuf_setnextpkt` (BSD KPI) | — |

Note on `withPool`: the seventh parameter is the options word `initWithPool` consumes
(`test r12b, 8` → `or byte [queue+0x142], 2` at `0xFFFFFF8002B0CB2B`–`0xFFFFFF8002B0CB31`;
`test r12b, 2`
→ `or byte [queue+0x142], 4` at `0xFFFFFF8002B0CB38`–`0xFFFFFF8002B0CB3E`); the disassembly of the
7-parameter factory (`0xFFFFFF8002B0CA86`, `mov r10d, [rbp+arg_0]` at `0xFFFFFF8002B0CA90`)
forwards it as-is. The layer passes 0: ring mode, no own ABFS. No option creates an injection
gate (§3).

Kernel KPIs, from the `SymbolsSets` plist of `__LINKINFO,__symbolsets` of the KC itself (binary at
file offset `0x3300000`, 9 lists): `mbuf_*` (freem, copydata, copyback, allocpacket, pkthdr_setlen,
pkthdr_len, nextpkt, setnextpkt, data, len) = `com.apple.kpi.bsd`, linkable. In contrast
`ifnet_enqueue`, `ifnet_dequeue`, `if_qflush`, `dlil_output`, `dlil_output_handler`,
`kern_netif_queue_tx_dequeue`, `kern_netif_queue_rx_enqueue`, `kern_pbufpool_*`, `kern_packet_*`
and
`kern_buflet_*` are in `com.apple.kpi.private`: they do not link from a third-party kext (the same
criterion of `Docs/AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`).

## 8. Corrections to `AWDL_STAGE1_STAGE2_PUSH_DESIGN.md` §6

| Design step | Correction |
|---|---|
| Step 3: «enqueue the mbuf into the provider's own ring» | There is no ring the driver can enqueue into: the send queue is filled only from `kern_netif_queue_tx_dequeue` (§3). Replace with: TX pool packet (steps 1–4 of §5) and direct call to the action (step 7), the `p2pMulticastTx` pattern of Broadcom (§4). |
| Step 3: «notify the send queue with `packetSubmission(false)`» | Superfluous and ineffective: without kernel notifications it returns `0xE00002DB` without pulling (`0xFFFFFF8002B2CF11`), and the `false` path requires the `+0xCC` start bit with no known writer in the family (§3). The kick of `AWDL_STAGE1_TXPUSH_LLINK.md` §4 holds for the normal path (kernel → queue), not for driver-served queues. |
| Step 4: «put the real action in place of the no-op» | Kept; the signature and the completion-queue consumption of `txNoop` are already right (`AWDLInterface.cpp:255`). New: the action will run on the stack thread and not the work loop, and it must return the consumed count (0 stalls the family machinery if the queue ever receives kernel traffic). |
| Steps 1–2 (installing the `output` via `params+0x40`) and steps 5–6 (alternative `0x8E8`/rollback) | No change. |

## 9. Open points (`?`)

1. **Writer of the queue start bit `byte+0xCC`**: no function of the `IOSkywalkFamily` entry
   text writes it (byte sweep §3); presumably the kernel writes it when attaching the queue to the
   `netif_queue`. Irrelevant for the direct path, decisive if the family kick is ever wanted.
2. **mbuf→`kern_packet` copy and mbuf free on the normal path**: inside the subtree of
   `ifclassq_dequeue` (`0xFFFFFF800070E9D0`) → `sub_FFFFFF800070EA80` → `sub_FFFFFF800070EE10`
   (`classq_subr.c`; type 1 mbuf / type 2 packet items). Only affects the contrast of §2.
3. **Data offset the kernel uses** in the packets it produces (the pool headroom, `+64` in the
   tracker on the Broadcom bus): its own handler picks 64; the exact kernel value has not been
   traced.
4. **Return to the pool in `completePackets`**: the final segment goes through slot `+0x148` of the
   pool vtable with `(pool, packets, count)` — the `deallocatePackets` signature — and through
   `kern_packet_tx_completion`; the slot interior has not been decompiled (the direct packet's pool
   membership is the same as that of the normal-path packets, created the same by
   `allocatePacket`).
5. **Thread context of the real action**: the RTL radio decides whether to transmit directly from
   the stack thread or jump to the work loop; the KC fixes nothing (it is own code).
6. **Fine handling of the `if_output` errno in `dlil_output`**: it is known to be an errno (the
   family returns 19); the consumer was not pursued.
7. Already noted in the previous design and standing: whether the Skywalk machinery tolerates an
   own `ifp+0x258` on a provider `ifnet` is checked at runtime (hardware).

## 10. Address verification

Heads checked against the function table of the database (function start == cited address):
`0xFFFFFF8002B2CD68` (`packetSubmission(bool)`), `0xFFFFFF8002B0D57C` (`getPacketListForNetworking`),
`0xFFFFFF8002B0D3E0` (`refillTxRingForNetworking`), `0xFFFFFF8002B2C860` (`legacyDequeue`),
`0xFFFFFF8002B2C986` (`listDequeue`), `0xFFFFFF8002B2CA7C` (`requestDequeue`),
`0xFFFFFF8002B0CAAC` (TX `initWithPool`), `0xFFFFFF8002B0CA86` (`withPool` 7), `0xFFFFFF8002B0C9B4`
(`withPool` 10), `0xFFFFFF8002B2DBB6` (`TxCompletionQueue::enqueuePackets`), `0xFFFFFF8002B0D758`
(`packetCompletion`), `0xFFFFFF8002B0658A` (`prepare`), `0xFFFFFF8002B058EE` (`allocatePacket`),
`0xFFFFFF8002B2C538` (`setDataOffsetAndLength`), `0xFFFFFF8002B002CE` (`if_output`),
`0xFFFFFF8001541D64` (`p2pMulticastTx`), `0xFFFFFF8002B08BC8` (base `initWithPool`),
`0xFFFFFF8002B2CB2A` (`packetSubmission(u64*,u32*,u32*)`), `0xFFFFFF8002B0CF84` (`synchronize`),
`0xFFFFFF8002B0CC76` (`enable`), `0xFFFFFF8002B2C746` (`checkForWork`), `0xFFFFFF8002B09016`
(`setKernNetifQueue`), `0xFFFFFF8002B0589C` (`getPacketWithHandle`), `0xFFFFFF8002B12B9E`
(`completePackets`), `0xFFFFFF8002B08EEC` (`getDirection`), `0xFFFFFF80009E6120`
(`kern_netif_queue_tx_dequeue`), `0xFFFFFF800070E9D0` (`ifclassq_dequeue`),
`0xFFFFFF800070EA80` (`sub_70EA80`), `0xFFFFFF80005EF500` (`_dlil_output_handler`),
`0xFFFFFF8002B07180` (`setLinkHeaderLength`), `0xFFFFFF8002B12B9E` (`completePackets`, head
re-verified by `get_func`), `0xFFFFFF8002B066AE` (`complete`), `0xFFFFFF8002B066FA`
(`completeWithQueue`), `0xFFFFFF8002B08EC8`/`0xFFFFFF8002B08EE0` (base
`packetSubmission(u64…)`/`sendNotification`, both `return 0xE00002E7`), `0xFFFFFF8002B2CAFE`
(`sendNotification` TX → `kern_channel_notify`).

Instruction citations (bytes read from the database) inside the previous heads:
`0xFFFFFF8002B2CD88` (`test al,1`), `0xFFFFFF8002B2CDC0` (`mov r15d, 0E00002D7h`),
`0xFFFFFF8002B2CDCE` (`test byte [rbx+0CCh], 1`), `0xFFFFFF8002B2CDE6`–`0xFFFFFF8002B2CDEC`
(`mov eax,[rbx+13Ch]`; `test al,5`), `0xFFFFFF8002B2CE1D`–`0xFFFFFF8002B2CE23`
(`xchg eax,[rbx+134h]`; `mov r15d,[rbx+130h]`), `0xFFFFFF8002B2CEBE`
(`test byte [rbx+142h], 2`),
`0xFFFFFF8002B2CED4` (`call refillTxRingForNetworking`), `0xFFFFFF8002B2CEE0`
(`call legacyDequeue`),
`0xFFFFFF8002B2CF11` (`mov r15d, 0E00002DBh`), `0xFFFFFF8002B2CF19`
(`call getPacketListForNetworking`),
`0xFFFFFF8002B0D41F` (`call kern_netif_queue_tx_dequeue`), `0xFFFFFF8002B0D473`
(`call getPacketWithHandle`), `0xFFFFFF8002B0D486` (`call [rax+1D8h]` =
`acquireWithPacketHandle`),
`0xFFFFFF8002B0D49C` (`call [rax+188h]` = `prepareWithQueue`), `0xFFFFFF8002B0D4FA`
(`call producePacket`), `0xFFFFFF8002B0D53E` (`call packetSubmission(uint,uint)`),
`0xFFFFFF8002B0D5BC`
(`call kern_netif_queue_tx_dequeue` in `getPacketListForNetworking`), `0xFFFFFF8002B2C86E`
(`test byte [rdi+28h], 1`), `0xFFFFFF8002B2C87B` (`test byte [rdi+0CCh], 1`), `0xFFFFFF8002B2C888`
(`mov r12, [rbx+20h]`, the action), `0xFFFFFF8002B2C88C` (`mov rcx, [rbx+0E8h]`, the table),
`0xFFFFFF8002B2C8C1`–`0xFFFFFF8002B2C8C5` (`shl rdx,3`; `add rdx,[rcx+8]`), `0xFFFFFF8002B2C8C9`
(`or byte [rbx+13Ch], 1`), `0xFFFFFF8002B0CA90` (`mov r10d, [rbp+arg_0]`, factory options),
`0xFFFFFF8002B0CB2B`–`0xFFFFFF8002B0CB31` (`test r12b,8`; `or byte [rbx+142h], 2`),
`0xFFFFFF8002B0CB38`–`0xFFFFFF8002B0CB3E` (`test r12b,2`; `or byte [rbx+142h], 4`),
`0xFFFFFF8002B0CB87`–`0xFFFFFF8002B0CB8C` (`call createPacketTable`; `mov [rbx+0E8h], rax`),
`0xFFFFFF8002B0CC9A`–`0xFFFFFF8002B0CCB2` (`cmp qword [rbx+78h], 0`; `test byte [rbx+142h], 4`;
`call [rax+1D8h]`), `0xFFFFFF8002B2CA9A`–`0xFFFFFF8002B2CAB4` (`cmp qword [rbx+78h], 0`;
`call [rax+1D8h]`), `0xFFFFFF8002B2CADD` (`mov r14d, 0E00002D8h`), `0xFFFFFF8001541D68`–
`0xFFFFFF8001541D70` (`mov ecx,edx`; `mov rdx,rsi`; `mov rax,[rdi]`; `mov rax,[rax+310h]`),
`0xFFFFFF8002B002CE`–`0xFFFFFF8002B002E0` (`test rsi,rsi`; `call mbuf_freem`; `mov eax, 13h`),
`0xFFFFFF8002B0658A` (`test byte [rdi+58h], 2`), `0xFFFFFF8002B058EE`–`0xFFFFFF8002B058FA`
(`mov rax,[rdi]`; `mov rax,[rax+130h]`), `0xFFFFFF8002B058A8`
(`call kern_packet_get_object_index`),
`0xFFFFFF8002B07184`–`0xFFFFFF8002B0718F` (`mov rdi,[rdi+28h]`;
`call kern_packet_set_link_header_length`; `mov ecx, 0E00002C2h`), `0xFFFFFF8002B12BDD`
(`call [rax+140h]` = `getDataLength`), `0xFFFFFF8002B12BE9` (`call getSourceQueue`),
`0xFFFFFF8002B08EEC`–`0xFFFFFF8002B08EF0` (`mov eax, [rdi+0B4h]`), `0xFFFFFF80009E614E`
(`cmp byte [r9+3Ch], 2`), `0xFFFFFF80009E6202` (`cmp byte [rsi+3Ch], 1`).

Data read: `__ZTV26IOSkywalkTxSubmissionQueue` (`0xFFFFFF8002B3E068`, 96 cells from the vptr:
no `enqueuePackets`; `+0x1D8` = `packetSubmission(bool)`, `+0x298` = `requestDequeue`, `+0x2A0` =
`purgePackets`, `+0x2A8` = `initWithPool`), `__ZTV15IOSkywalkPacket` (`0xFFFFFF8002B3B510`, 80
cells:
`+0x140` = `getDataLength`, `+0x158` = `setDataOffsetAndLength`, `+0x188` = `prepareWithQueue`,
`+0x1D8` = `acquireWithPacketHandle`), `__ZTV20IOSkywalkPacketQueue` (`0xFFFFFF8002B3CC70`, 90
cells:
`+0x2A0`/`+0x2A8` = 0), vptr of `AppleBCMWLANPCIeSkywalkTxSubmissionQueue`
(`0xFFFFFF80015ACD00`,
cell `+0x368` = `0xFFFFFF8001541D64`), and the `SymbolsSets` plist of the KC (file offset
`0x3300000`,
`0x4D6D4` bytes).
