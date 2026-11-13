# Implementation specification: TX of data from `awdl0`

What must replace `RTLAwdInterface::txNoop` (`Source/AirLayer/AWDLInterface.cpp:289`) so that
`awdl0` transmits the data the stack hands it: the exact contract of the action of the
`IOSkywalkTxSubmissionQueue` created with `withPool`, the Ethernet→802.11 conversion inserting the
AWDL encapsulation, the new radio ABI function, and the change steps in the two kexts with their
test plan without an Apple peer. Base: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build
25G241, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`) verified over a copy of the
annotated database in `Build/ida_awdl/task_tx_datos/` (`verify.txt`, `verify2.txt`, `verify3.txt`:
44
named function heads checked, complete dumps of `legacyDequeue`, `enqueuePackets`,
`completePackets`, `withPool`/`initWithPool`, vtables cell by cell and 34 instruction points), the
docs `AWDL_DATA_ENCAP.md` (§3, §4, §7), `AWDL_STAGE2B_MBUF_TO_PACKET.md`, `AWDL_RX_DATA_TO_SKYWALK.md`
and `AWDL_RTL8814_RAW_TX_PORT1.md`, and the sources of `Source/rtl8814au` and `Source/Driver`.
Addresses = KC VA; vtable convention: a `call [vptr+X]` reads cell `ZTV+X+0x10`.
Anything not observed carries `?`. It excludes data RX (`AWDL_RX_DATA_TO_SKYWALK.md`).

## 1. Overview

1. The TX action of `awdl0` receives from the kernel packets of its own pool whose frame is
   **Ethernet II**
   (`DA, SA, ethertype, payload`) placed at `getDataVirtualAddress() + getDataOffset()`, with
   `dataOffset` = headroom of the `RegistrationInfo` (`+0x3C` = 64,
   `Source/AirLayer/AWDLInterface.cpp:230`),
   which the kernel reads via `IOSkywalkNetworkInterface::getTxDataOffset`
   (`0xffffff8002b0baba`,
   `movzx eax, word ptr [rax+3Ch]` at `0xffffff8002b0bad2`). No piece of the family adds a
   header: the action is the first consumer (`AWDL_DATA_ENCAP.md` §3).
2. The action must: read the frame, build the 802.11 data frame with the Apple encapsulation
   (SNAP + AWDL data header with its per-destination sequence counter) and deliver it to the radio
   through a new ABI function (`rtlRadioTxData`), then return **all** the packets via the
   `IOSkywalkTxCompletionQueue` in batches with `n < capacity` and return the consumed count.
   Returning 0 stalls the queue (`+0x13C` bit 2); returning an error code corrupts the read ring
   (§2.3): the action never returns errors.
3. The conversion Broadcom splits between host and firmware (host: SNAP + AWDL data header +
   counter; firmware: 802.11 MAC header, `?`) is done here entirely by the action: the radio sends
   the bytes as-is over the management queue, at a fixed OFDM 6 Mb/s rate, touching nothing of the
   frame.
4. Multicast also goes out from the action in the first stage (no stash); the optional second
   stage replicates the Broadcom model (duplicate + `stashMulticastPacketPair` + manager timer +
   `p2pMulticastTx`/`freeMulticastPacket`/`triggerAWDLMulticastTx` overrides, §3.4).
5. The radio needs a single addition to the ABI, `RTL_RADIO_FN_TX_DATA`, a clone of
   `rtlRadioTx80211` accepting data frames (`Source/Driver/AWDLTx.c:32` rejects them today); port 1
   takes no part in the TX descriptor (§4.4).

## 2. Contract of the send queue action

### 2.1 Creation with `withPool`

Factory used (7 parameters), already linked by stage 1 (`Source/AirLayer/AWDLInterface.cpp:40-42`):

| | |
|---|---|
| Symbol | `__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j` |
| VA | `0xffffff8002b0ca86` (verified head) |
| Signature | `IOSkywalkTxSubmissionQueue *withPool(IOSkywalkPacketBufferPool *pool, uint32_t capacity, uint32_t flags, OSObject *owner, uint32_t (*action)(OSObject *, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket * const *, uint32_t, void *), void *refCon, uint32_t opts)` |

The factory disassembly (`0xffffff8002b0ca8a`–`0xffffff8002b0caa0`) shows it forwarding to the
9-parameter variant (`0xffffff8002b0c9b4`) with a null notification action
`uint32_t (*)(OSObject *, IOSkywalkTxSubmissionQueue *, uint32_t *)` and `-1` in the last
parameter; the main action stage 1 registers is that of the fifth parameter. The 9-parameter
variant (`0xffffff8002b0c9b4`, dumped in `verify3.txt`) calls the virtual `initWithPool` (vptr cell
`+0x2A8`, `0xffffff8002b0ca37`) with the `-1` of the factory in the third integer (ends at `+0xB8`,
`0xffffff8002b0cb24` via `edx`), the capacity in the fourth (`ecx`, the one requiring ≥ 8) and the
flags in the fifth (`r8d`); it also forces direction 1 in the base init (`mov ecx, 1` at
`0xffffff8002b0cb06`). There is also an 8-parameter variant with a non-const `IOSkywalkPacket **`
action forcing bit 3 of `opts` (list mode, `or r10d, 8` at `0xffffff8002b0ca6d`); not
used.

Requirements `IOSkywalkTxSubmissionQueue::initWithPool` checks (`0xffffff8002b0caac`,
`__ZN26IOSkywalkTxSubmissionQueue12initWithPoolE…`, full dump in `verify.txt`):

| Requirement | VA | Effect if it fails |
|---|---|---|
| `pool != 0` | `0xffffff8002b0cac3`–`0xffffff8002b0cac6` | init false |
| **`action != 0`** | `0xffffff8002b0cacc`–`0xffffff8002b0cad3` | init false (the blocking defect of stage 1, `f9abb43`, closed by the no-op action) |
| `capacity >= 8` | `0xffffff8002b0caba` (`cmp ecx, 8`) | init false |
| `opts <= 0x1F` | `0xffffff8002b0cadd`–`0xffffff8002b0cae1` | init false |

`opts` consumes the bits: `0x8` → list mode (`or byte [queue+0x142], 2` at
`0xffffff8002b0cb31`),
`0x2` → `or byte [queue+0x142], 4` (`0xffffff8002b0cb3e`), `0x4` → ABFS with `_gIOSkywalkABFSEnable`
(`0xffffff8002b0cb45`–`0xffffff8002b0cb52`). The stage passes 0. The capacity (128 on `awdl0`) goes
into
the packet table: `createPacketTable([queue+0xAC])` (`0xffffff8002b0cb81`–`0xffffff8002b0cb87`).
The base init (`IOSkywalkPacketQueue::initWithPool`, `0xffffff8002b08bc8`) stores the retained pool
at
`+0x50` (`0xffffff8002b08c53`), the `refCon` at `+0x58` (`0xffffff8002b08c40`), the direction at
`+0xB4`
(`0xffffff8002b08c44`), the capacity at `+0xAC` (j3, from the `ecx` of the subclass) and the third
integer (flags, `< 0x10000`, `0xffffff8002b08c46`) at `+0xA8`; leaves `+0xB8` at `0xFFFFFFFF`
(`0xffffff8002b08c50`), which the subclass rewrites with the `-1` of the factory.
`owner` goes to `+0x18` and the action to `+0x20` (`IOEventSource` fields the base init fills with
`IOEventSource::init(owner, action)`; `legacyDequeue` reads them, §2.2).

### 2.2 Action invocation (`legacyDequeue`)

`IOSkywalkTxSubmissionQueue::legacyDequeue`
(`__ZN26IOSkywalkTxSubmissionQueue13legacyDequeueEv`,
`0xffffff8002b2c860`; full dump in `verify.txt`):

1. Gates: `byte +0x28 bit 0` (enabled, `0xffffff8002b2c86e`) and `byte +0xCC bit 0` (started,
   `0xffffff8002b2c87b`); if the second is missing, tail to the vptr cell `+0x2A0` =
   `purgePackets`
   (`0xffffff8002b2c97e`).
2. Loop: pending = write − read (`[table+0x00] − [table+0x80]`), or capacity − read
   (`[table+4] − [table+0x80]`) if the write wrapped (`0xffffff8002b2c899`–`0xffffff8002b2c8b2`);
   sets bit 1 of `+0x13C` in flight (`0xffffff8002b2c8c9`) and calls

   `action(owner = [queue+0x18], queue, packets = table + 8·readIndex, count = pending, refCon = [queue+0x58])`
   (`0xffffff8002b2c8d0`–`0xffffff8002b2c8de`).

   The array is a contiguous portion of the send queue's packet table
   (`AWDL_STAGE2B_MBUF_TO_PACKET.md`
   §2); each element is an `IOSkywalkPacket *` (const for the action).
3. **Return**: used as the consumed count, unsigned, no error path:
   - `0` → bit 2 of `+0x13C` (stall, `0xffffff8002b2c937`–`0xffffff8002b2c93a`), timestamp in the
     statistics block `[queue+0x100]+0x78` and end.
   - `ret > 0` → read index `[table+0x80] += ret`, with a single subtraction of the capacity
     (`[table+4]`) if the result crosses it (`0xffffff8002b2c90a`–`0xffffff8002b2c92c`).
   - There is no signed comparison: a negative `IOReturn` (e.g. `0xE00002D8`) enters as a huge
     unsigned integer and the read index jumps with a partial single-subtraction wrap
     (`0xffffff8002b2c91c`–`0xffffff8002b2c92a`), leaving the ring incoherent. **The action always
     returns the number of consumed packets (0 only if there are no pending ones); never an error
     code.** Own work errors (busy radio, no resources) are logged and do not alter the return: the
     packets are returned anyway via the completion queue.
4. Counters of block `[queue+0x100]`: `+0x60` calls, `+0x00` partial drains
   (`ret < pending`, `0xffffff8002b2c8ff`–`0xffffff8002b2c904`), `+0x08` stalls;
   `[queue+0x130] += ret` (`0xffffff8002b2c954`).
5. The stall is not permanent: `checkForWork`
   (`__ZN26IOSkywalkTxSubmissionQueue12checkForWorkEv`, `0xffffff8002b2c746`, dump in
   `verify3.txt`) clears bit 2 of `+0x13C` (`0xffffff8002b2c7df`–`0xffffff8002b2c7e2`) and calls
   `packetSubmission` again (`0xffffff8002b2c848`), with stopped-time statistics at
   `[queue+0x100]+0x70`.

### 2.3 What the array contains and who owns it

The packets are produced by the kernel, not the action:
`packetSubmission` → `refillTxRingForNetworking` (`0xffffff8002b0d3e0`) →
`kern_netif_queue_tx_dequeue` (`0xffffff8002b0d41f`) and, per kern handle,
`getPacketWithHandle(pool, handle)` (`0xffffff8002b0d473`, pool at `[queue+0x50]`),
`acquireWithPacketHandle(handle, 0)` (vptr cell `+0x1D8`, `0xffffff8002b0d486`),
`prepareWithQueue(queue, 1, 0)` (vptr cell `+0x188`, `0xffffff8002b0d49c`), outbound BPF
(`0xffffff8002b0d4dc`) and `producePacket` into the table (`0xffffff8002b0d4fa`). The kernel
copied the frame into the packet buffer with `dataOffset` = headroom (§1.1); the packet arrives
already `prepare`d
(byte `+0x58` bit 2) and with its kern handle at `+0x28`.

Ownership: while the action runs, the array packets are outside the kernel ring and
inside the send queue table. The action must not call
`IOSkywalkPacketBufferPool::deallocatePacket` on them: the kern handle close is done by the
completion queue inside `completePackets` (§2.5). Only packets the driver created by
itself (the multicast duplicate of phase 2, §3.4) admit a direct `deallocatePacket`, because they
never went through the table. The return path is unique for both origins: `enqueuePackets` of the
completion queue, which calls the pool's `deallocatePackets` at the end (same pool: the queue's,
`[queue+0x50]`).

### 2.4 Return via the completion queue

`IOSkywalkTxCompletionQueue::enqueuePackets(IOSkywalkPacket const*, uint, uint)`
(`__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj`,
`0xffffff8002b2dbb6`;
full dump in `verify.txt`). Errors, in order:

| Check | VA | Code |
|---|---|---|
| `pkts != 0` and `count != 0` | `0xffffff8002b2dbcb`–`0xffffff8002b2dbde` | `0xE00002C2` |
| `count < [queue+0xAC]` (capacity, **strict**) | `0xffffff8002b2dbe6`–`0xffffff8002b2dbec` | `0xE00002C2` |
| `[queue+0x30] != 0` (work loop) | `0xffffff8002b2dbee`–`0xffffff8002b2dbf3` | `0xE00002D8` |
| `byte +0x28 bit 0` and `byte +0xCC bit 0` | `0xffffff8002b2dc06`–`0xffffff8002b2dc13` | `0xE00002D7` |
| `byte +0x110 bit 0` set | `0xffffff8002b2dc15`–`0xffffff8002b2dc1c` | `0xE00002D5` |

On success: `memmove` of the pointers into its own array `[queue+0xE0]` (reserved by
`initWithPool` with `capacity × 8` bytes, `0xffffff8002b12a2d`–`0xffffff8002b12a43`) and a call to
`completePackets(pkt**, n)` (`0xffffff8002b2dc41`). Returns `kIOReturnSuccess`. **If it fails, the
packets remain the caller's responsibility**: free them with
`deallocatePacket` (that is what `txNoop` does, `Source/AirLayer/AWDLInterface.cpp:296-301`; the
batches of the action use `n = 64` with capacity 128). The chained variant
(`__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEP15IOSkywalkPacketjj`, `0xffffff8002b2dc6e`)
walks `getNextPacket`/`setNextPacket` and does the same close packet by packet.

### 2.5 `completePackets`: what the family touches per packet

`IOSkywalkTxCompletionQueue::completePackets(IOSkywalkPacket**, uint)`
(`__ZN26IOSkywalkTxCompletionQueue15completePacketsEPP15IOSkywalkPacketj`,
`0xffffff8002b12b9e`;
dump in `verify.txt`), per packet:

1. `getDataLength` (vptr cell `+0x140`, called at `0xffffff8002b12bdd`).
2. `getSourceQueue` (`0xffffff8002b12be9`); if there is a source queue, `notifySrcQueue(false,
   queue, len)`
   (`0xffffff8002b12bfe`) — it lands in the send queue's `packetCompletion`
   (`0xffffff8002b0d758`, vptr `+0x230`), which only carries counters
   (`AWDL_STAGE2B_MBUF_TO_PACKET.md` §5).
3. `completeWithQueue(completionQueue, 1, 0)` (vptr cell `+0x198`, `0xffffff8002b12c13`).
4. `kern_packet_tx_completion(pkt+0x28)` **only if `[queue+0xE8] != 0`** (`0xffffff8002b12c19`–
   `0xffffff8002b12c29`).

At the end of the batch: `deallocatePackets(pool = [queue+0x50], pkts, n)` through cell `+0x148` of
the pool vtable (`0xffffff8002b12c42`–`0xffffff8002b12c58`), `notifySrcQueue(true, 0, 0)`
(`0xffffff8002b12c6a`) and, if `[queue+0xF0] != 0`, call `[vptr+0x268](n, bytes)`
(`0xffffff8002b12c6f`–`0xffffff8002b12c84`). `[queue+0xF0]` holds the seventh parameter of
`initWithPool` (`0xffffff8002b0cb98`–`0xffffff8002b0cb9c`; `completePackets` dereferences it as
an object with a vtable, although the mangled name types it as a function `?`); the 7-parameter
factory passes NULL in that slot, so the call does not happen with the stage 1 queues. **No state or
flags field of the packet is read or required on 25G241**: the action has nothing to mark before
returning the packets; the only state that matters is the prepared bit (`+0x58` bit 2, already set
by `prepareWithQueue` on the kernel packets, or by `prepare` on own ones) and the internal marks
of the PCIe subclass (`pkt+0x78 → +0x28`, Broadcom flag 0x80, §3.4), which do not apply to a
family packet. The operational evidence that the full cycle works with queues created exactly
this way is the STA layer (`en2` transmits with the same pattern,
`Source/AirLayer/RTLAirLayer.cpp:408-437`); the interior of `kern_packet_tx_completion` when
`[queue+0xE8] = 0` has not been dumped (`?`, irrelevant if the en2 path proves it on hardware).

### 2.6 `IOSkywalkPacket` / `IOSkywalkPacketBuffer` accessors to read the frame

All exported (type `0x0f` in
`Docs/re/io80211_25.6.0/syms_com.apple.iokit.IOSkywalkFamily.txt`),
resolved against `__ZTV15IOSkywalkPacket` `0xffffff8002b3b510` (full dump in `verify.txt`;
vtable cell X = `vptr+X−0x10`). With `maxBuffersPerPacket = 1` (stage 1 pool,
`Source/AirLayer/AWDLInterface.cpp:181`) every packet has a single segment and the frame is read
with the first four.

| Method (mangled) | VA | vptr cell | Use |
|---|---|---|---|
| `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv` | `0xffffff8002b06518` | `+0x188` | address of the segment **without** `dataOffset` |
| `__ZNK15IOSkywalkPacket13getDataOffsetEv` | `0xffffff8002b2c5e2` | `+0x160` | frame = base + offset |
| `__ZNK15IOSkywalkPacket13getDataLengthEv` | `0xffffff8002b064d0` | `+0x150` | length of the Ethernet II frame |
| `__ZNK15IOSkywalkPacket20getPacketBufferCountEv` | `0xffffff8002b064c6` | `+0x138` | number of segments (1 in this pool) |
| `__ZNK15IOSkywalkPacket16getPacketBuffersEPP21IOSkywalkPacketBufferj` | `0xffffff8002b2c400` | `+0x130` | segment array (general chain) |
| `__ZNK15IOSkywalkPacket19getMemoryDescriptorEv` | `0xffffff8002b2c43a` | `+0x140` | memory descriptor of the first segment |
| `__ZNK15IOSkywalkPacket13getBufferSizeEv` | `0xffffff8002b064b8` | non-virtual | buffer size (2048) |
| `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj` | `0xffffff8002b2c538` | `+0x168` | own packets only |
| `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj` | `0xffffff8002b0658a` | `+0x1A0` | mandatory before touching the buffer (own packet) |
| `__ZN15IOSkywalkPacket13setNextPacketEPS_` | `0xffffff8002b068e2` | — | chain (chained variant) |
| `__ZNK15IOSkywalkPacket13getNextPacketEv` | `0xffffff8002b068ec` | — | walk the chain (exported `0x0f`; corrects the MISSING of `AWDL_RX_DATA_TO_SKYWALK.md` §7) |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh` | `0xffffff8002b07180` | — | 14, on own packets |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderOffsetEj` | `0xffffff8002b071a8` | — | Broadcom uses it in TX (`0xffffff8001543384`) |
| `__ZN22IOSkywalkNetworkPacket11setHeadroomEh` | `0xffffff8002b07158` | — | the −16 trick of Broadcom (`0xffffff80015432d0`) |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj` | `0xffffff8002b058ee` | — | own packets (phase 2) |
| `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `0xffffff8002b2c0de` | — | pre-action errors; phase 2 |
| `__ZN25IOSkywalkPacketBufferPool17deallocatePacketsEPP15IOSkywalkPacketj` | `0xffffff8002b2c128` | — | batch (`completePackets` calls it) |

`IOSkywalkPacketBuffer` accessors in case the pool ever grows
(`__ZNK21IOSkywalkPacketBuffer13getBufferSizeEv` `0xffffff8002b06ca8`,
`…13getDataLengthEv` `0xffffff8002b06cb6`, `…13getDataOffsetEv` `0xffffff8002b06cc0`,
`…22getMemorySegmentOffsetEv` `0xffffff8002b06ce4`, `…16getMemorySegmentEv`
`0xffffff8002b06cda`,
`__ZN21IOSkywalkPacketBuffer13setDataLengthEj` `0xffffff8002b2c602`,
`__ZN21IOSkywalkPacketBuffer22setDataOffsetAndLengthEtj` `0xffffff8002b2c67e`).
`getSourceQueue` (`0xffffff8002b12be9` is the internal call; symbol `0x0f`) the family consults at
close; the action does not need it.

### 2.7 No jump tables

The sweep of the contract functions (`legacyDequeue`, the two `enqueuePackets`,
`completePackets`, the PCIe `dequeuePackets`) found no jump tables: the Broadcom role
branching is by bit tests (`bt`/`cmp`), not a `switch` (no case numbers to bias).

## 3. Ethernet → AWDL 802.11 conversion

### 3.1 What Broadcom does (verified)

In `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets` (`0xffffff80015420f0`, the action of
its TX queue), role 6 path (`AWDL_DATA_ENCAP.md` §3-§4; every point re-verified as `SPOT` in
`verify.txt`):

- The input frame is **Ethernet II** (DA at +0, `0xffffff80015424c7`; ethertype at +12).
- Unicast (bit 0 of DA at 0, `test cl, 1` at `0xffffff80015424aa`): builds the flow queue key
  `{ifId, fAc, DA}` (`findOrCreateFlowQueue`, `0xffffff8001542006`) and inserts 16 bytes in front
  of the frame: saves the old frame, lowers the headroom 16 (`setHeadroom` at
  `0xffffff80015432d0`,
  `add r15b, 0F0h` at `0xffffff8001543271`), copies DA+SA, writes the 802.3 length at +12
  (`0xffffff80015432f7`, artifact of the host↔firmware internal format, **it does not exist on the
  air**), the
  SNAP `AA AA 03 00 17 F2 08 00` at +14 (`mov rax, 8F2170003AAAAh` at `0xffffff80015432fd`, store
  at `0xffffff8001543307`), `03 04` at +22 (`0xffffff800154330c`), `00 00` at +26
  (`0xffffff8001543314`), the original ethertype at +28 (`0xffffff8001543321`) and the sequence at
  +24:
  reads the u16 counter at `+0x8A` of the flow queue internal struct **before** incrementing it
  (`0xffffff800154332f`/`0xffffff8001543339`) and writes
  `seq = (counter & 0xFFF) | (fAc << 12)` (`0xffffff800154334d`–`0xffffff8001543358`); adjusts
  `setDataOffset` (vptr+0x148, `0xffffff8001543368`), `setDataLength` (vptr+0x138,
  `0xffffff8001543378`), `setLinkHeaderOffset` (`0xffffff8001543384`) and enqueues into the flow
  queue
  (`0xffffff80015433ca`). The firmware receives the already-encapsulated frame and adds the 802.11
  MAC header afterward (`?` firmware interior: QoS subtype, rates, duration).
- **QoS**: the host decides nothing about QoS (the internal format does not carry it). On the air
  AWDL data frames are seen without QoS for single MSDU (`fc 0x0008`) and with QoS (`0x0088`) only
  in A-MSDU (owly `docs/SPEC.md` AWDL-13/14; `crates/owly-awdl/src/frame.rs:313` transmits without
  QoS,
  `frame.rs:334-335` A-MSDU with QoS and TID 0). Which subtype the Broadcom firmware picks for a
  single MSDU is `?`; the owly equivalent and the captures of `AWDL_REFERENCIAS_EXTERNAS.md` §5
  mark the no-QoS path.
- Multicast (bit 0 of DA at 1, `0xffffff8001543020`): if the packet already carries flag 0x80
  (`mov al, [rdx+28h]`, `and al, 7Fh` at `0xffffff800154321f`, `mov [rdx+28h], al` at
  `0xffffff8001543225`) it transmits like a unicast; if not, it marks it
  (`or byte ptr [r12+28h], 80h` at
  `0xffffff8001543066`), duplicates it (`duplicatePacket`, `0xffffff800154307e`), marks both and
  `stashMulticastPacketPair` (`0xffffff80015435be` → `0xffffff8002334e20` →
  `IO80211AWDLPeerManager::stashMulticastPacketPair`, `0xffffff8002213b16`, two arrays of 16)
  leaves the pair with the manager. The manager timer
  (`IO80211AWDLPeerManager::handleMulticastTimer`, `0xffffff80021c38c4`) frees them in the right AW
  window by calling slot `+0xF18` of the interface (`p2pMulticastTx(IO80211NetworkPacket**, uint)`;
  the base `0xffffff8002334d6a` does nothing; the PCIe one `0xffffff8001541d64` re-enters its
  `dequeuePackets` action via cell `+0x310`). The leftovers are freed with `freeMulticastPacket`
  (slot `+0xF08`; base `0xffffff8002334d5a` empty). `IO80211Family` also contains
  `IO80211AWDLPeerManager::addAwdlSnapHeader(mbuf*, u16)` (`0xffffff8002216cd0`), dead code
  without references: the family encapsulates nothing.
- On RX the symmetric requires the Apple SNAP (`cmp eax, 8F21700h` at `0xffffff800154a238`) and only
  frames with that SNAP reach the queue of `awdl0` (`AWDL_RX_DATA_TO_SKYWALK.md` §3).

### 3.2 What the RTL action must build

Full frame (without FCS), per packet, over the received Ethernet II:

```
802.11 (24 B) | AA AA 03 00 17 F2 08 00 | 03 04 | seq (u16 LE) | 00 00 | ethertype (BE) | payload
```

| Field | Value | Justification |
|---|---|---|
| Frame control | `08 00` (data, no QoS, To DS/From DS 0) | owly AWDL-13, `frame.rs:313`; the receiver must also accept QoS (`frame.rs:157`) but the owly sender does not use it |
| Duration | `00 00` | no firmware to fill it `?`; the MAC may impose its own `?` |
| addr1 | DA of the Ethernet frame | Broadcom RX conversion uses addr1 as DA (`0xffffff800154a569`) |
| addr2 | MAC of `awdl0` | SA on RX = addr2 (`0xffffff800154a559`) |
| addr3 | `00:25:00:FF:94:73` | AWDL BSSID (`frame.rs:160`; `Source/rtl8814au/core/rtw_recv.c:27`) |
| Sequence control | whatever the hardware puts | the vendor TX queue enables hardware sequence when `qos_en = 0` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:112-117`); the `SetSeqNum` of the management recipe is superfluous for data |
| SNAP + `03 04` + `00 00` + ethertype | same as Broadcom, without the 802.3 length | §3.1; the length field disappears when replacing the Ethernet header with the 802.11 one (`AWDL_DATA_ENCAP.md` §7.4) |
| AWDL seq | `(counter & 0xFFF) \| (AC << 12)`, u16 LE, counter per DA, first frame 0 | Broadcom takes it from the flow queue by `{if, AC, DA}` before incrementing (§3.1); AC = 0 with a single send queue; the semantics of the high nibble at an Apple receiver is `?` (owly does not mention it) |

Maximum size: 24 + 16 + MTU (0x594 = 1428) = 1468 B ≤ management buffer limit
`MAX_XMIT_EXTBUF_SZ − TXDESC_OFFSET` = 1536 − 48 (`Source/rtl8814au/include/rtw_xmit.h:89`,
`TXDESC_SIZE 40` at `rtw_xmit.h:199-201`, `PACKET_OFFSET_SZ 8` at
`Source/rtl8814au/include/rtl8814bu_hal.h:25`; the check of `AWDLTx.c:29` already bounds it). A-MSDU
is not emitted (single MSDU, like owly); if ever emitted, the header would be 26 B with QoS control
TID 0 + bit 0x0080 and subframes with BE16 length = 16 + payload (`frame.rs:326-348`).

### 3.3 Broadcom ↔ RTL split

Broadcom splits: host = SNAP + AWDL data header + counter; firmware = 802.11 MAC header +
QoS + rates (`?`). In RTL there is no AWDL firmware, so the action builds the whole frame (§3.2)
and the radio sends it as raw bytes — the same division of responsibilities that already works for
action frames (`RTL_RADIO_FN_TX_80211`, `Source/Driver/RTLRadioABI.h:66`, «The bytes are copied
before the call returns»). The alternative (the action inserts only the 16 bytes with the headroom
−16 trick and the driver builds the 802.11 header) would add AWDL knowledge to the vendor tree
without advantage: discarded.

### 3.4 Multicast

- **Phase 1** (with this spec): the action transmits the multicast immediately via the radio with
  its own sequence counter for that DA, without stash. Peers only listen in their availability
  windows, so delivery is not guaranteed; it is accepted and counted (§6). The family trio
  (stash + timer) works without driver cooperation, but requires the interface overrides to be
  useful.
- **Phase 2** (optional, later): overrides in `RTLAwdInterface` of the slots already declared in
  the shim (`Source/AirLayer/AWDLInterface.h:173-176`, bodies today in the family):
  `freeMulticastPacket` (slot `+0xF08`: `deallocatePacket` to the TX pool),
  `triggerAWDLMulticastTx` (slot `+0xF10`), `p2pMulticastTx(IO80211NetworkPacket**, uint)`
  (slot `+0xF18`: transmit each packet via the radio and return it with
  `deallocatePacket`). The action, on a multicast DA, duplicates the packet
  (`allocatePacket` + `prepare(pkt, nullptr, 0, 0)` + copy — `duplicatePacket`
  (`0xffffff80015479cc`) belongs to the PCIe subclass and does not link), delivers
  original and duplicate to `IO80211VirtualInterface::stashMulticastPacketPair`
  (`__ZN23IO80211VirtualInterface24stashMulticastPacketPairEP20IO80211NetworkPacketS1_`,
  `0xffffff8002334e20`, exported at `syms.txt:15742`; it reaches the manager stash
  `0xffffff8002213b16`) and counts the original as consumed (without completing or freeing: the
  stash keeps the ownership). `handleMulticastTimer` (`0xffffff80021c38c4`) will then call the project's own
  `p2pMulticastTx` inside the AW window with the manager arrays. The stashed packets are NOT
  returned via the completion queue: they never entered it and their close is `deallocatePacket`.
  Distinguishing re-fed ones (the Broadcom flag 0x80 is an internal PCIe packet convention) is
  unnecessary: the project's own `p2pMulticastTx` transmits directly and does not re-enter the action.

## 4. Addition to the radio ABI

### 4.1 Prototype

In `Source/Driver/RTLRadioABI.h`, next to `RTL_RADIO_FN_TX_80211` (line 66):

```c
/*
 * One data frame, complete 802.11 (header + body, no FCS): the AWDL data
 * encapsulation is already in place. Sent on the operating channel at OFDM
 * 6 Mb/s with the hardware sequence number; unicast frames expect an ACK,
 * group-addressed frames go without retries. kIOReturnNotReady while the
 * radio is down, kIOReturnBusy while TX waits for a channel,
 * kIOReturnNoResources with no free management buffer. Must not be called
 * from inside a radio hook.
 */
#define RTL_RADIO_FN_TX_DATA     "rtlRadioTxData"   /* p1: const uint8_t *frame, p2: uint32_t *len */
```

`RTL_RADIO_ABI` stays at 2: only a name is added, like `rtlRadioSetChannel` (previous
builds answer `kIOReturnUnsupported`). The dispatch goes in the `callPlatformFunction`
of `Source/Driver/RTL8814Native.cpp` (pattern of cases 432-512, AWDL hook case at 504-506) and
delegates to the new function of the vendor tree.

### 4.2 Implementation in the vendor tree

`int rtl8814native_radio_tx_data(const uint8_t *frame, uint32_t len)` in `Source/Driver/AWDLTx.c`,
clone of `rtl8814native_radio_tx_80211` (`AWDLTx.c:21-61`) with four changes:

1. Accept data frames: out goes the `GetFrameType(frame) != WIFI_MGT_TYPE` filter
   (`AWDLTx.c:32-33`); validate `GetFrameType(frame) == WIFI_DATA_TYPE` (`wifi.h:75`) and To DS/From
   DS at 0 optionally.
2. Explicit fixed rate: `pattrib->rate = IEEE80211_OFDM_RATE_6MB` (`ieee80211.h:772`) instead of
   the `pmlmeext->tx_rate` of `update_mgntframe_attrib` (`Source/rtl8814au/core/rtw_mlme_ext.c:7672`).
3. No `SetSeqNum` (`AWDLTx.c:50-51`): the descriptor enables hardware sequence
   (`SET_TX_DESC_HWSEQ_EN` with `qos_en = 0`, `rtl8814au_xmit.c:112-117`;
   `update_mgntframe_attrib` sets `qos_en = _FALSE` at `rtw_mlme_ext.c:7677`).
4. Retries per destination: `pattrib->retry_ctrl = _FALSE` if `IS_MCAST(GetAddr1Ptr(pframe))`
   (pattern already at `AWDLTx.c:54-55`).

The rest of the recipe is kept: `alloc_mgtxmitframe` (`rtw_mlme_ext.c:7496`,
`frame_tag = MGNT_FRAMETAG` at 7519), copy of the bytes to `buf_addr + TXDESC_OFFSET`,
`update_mgntframe_attrib` (`rtw_mlme_ext.c:7616`), `pattrib->last_txcmdsz = len`,
`dump_mgntframe` (`rtw_mlme_ext.c:7734`) → `rtw_hal_mgnt_xmit` (`hal_intf.c:816`) →
`rtl8814au_mgnt_xmit` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:896-899`) →
`rtw_dump_xframe` (`rtl8814au_xmit.c:369`), which calls `update_txdesc` per fragment (`:406`),
chooses the USB queue with `rtw_get_ff_hwaddr` (`:415`) and writes the USB port with `rtw_write_port`
(`:427`). The PMF hook of `dump_mgntframe` (`rtl_pmf_protect_mgnt`,
`Source/rtl8814au/core/rtw_mlme_ext.c:7723-7732`) does not alter frames without an applicable PMF
key `?`.

### 4.3 Queue (QSLT), rate, retries and macid

| Choice | Value | Citations |
|---|---|---|
| QSLT (phase 1) | `QSLT_MGNT` 0x12 | `update_mgntframe_attrib`, `rtw_mlme_ext.c:7632`; constant at `Source/rtl8814au/include/hal_com.h:262`; `rtw_get_ff_hwaddr` → `MGT_QUEUE_INX` (`Source/rtl8814au/core/rtw_xmit.c:4390-4392`) |
| Rate | OFDM 6 Mb/s fixed: `USE_RATE = 1` + `TX_RATE = MRateToHwRate(IEEE80211_OFDM_RATE_6MB)` (`DESC_RATE6M` = 0x04) | `MGNT_FRAMETAG` branch: `SET_TX_DESC_USE_RATE` (`rtl8814au_xmit.c:229`) and `SET_TX_DESC_TX_RATE` (`:232`); `MRateToHwRate` at `Source/rtl8814au/hal/hal_com.c:489`; `DESC_RATE6M` at `hal_com.h:39` |
| Unicast retry | limit 6 (`retry_ctrl = _TRUE`) | `rtl8814au_xmit.c:259-261` (`RETRY_LIMIT_ENABLE` + `DATA_RETRY_LIMIT 6`) |
| Multicast retry | limit 12 (`retry_ctrl = _FALSE`); group A1 gets no ACK | `rtl8814au_xmit.c:262-263`; no ACK for group A1 `?` (MAC decision, no citation in the tree) |
| macid | `RTW_DEFAULT_MGMT_MACID` = 1 | `Source/rtl8814au/include/drv_types.h:806`, set at `rtw_mlme_ext.c:7631`; with `USE_RATE = 1` the rate adaptation table is not consulted, and `EN_DESC_ID`/CAM are not touched in this branch (only `fill_txdesc_force_bmc_camid` under `CONFIG_CONCURRENT_MODE`, `rtl8814au_xmit.c:125-128`, disabled in `Source/rtl8814au/include/autoconf.h:113`) → macid inert except the PHYDM `raid` (`rtw_get_mgntframe_raid`, `rtw_mlme_ext.c:7667-7671`) |

Queue justification: the full management path is already proven on hardware with the action
frames (commits `4ac3340`, `cceb743`), the management EP does not compete with the data of `en2`,
and the
`MGNT_FRAMETAG` branch of the descriptor gives a fixed rate and a retry limit without touching
anything else. The pure data path (`DATA_FRAMETAG`, `rtl8814au_xmit.c:121-224`, `QSLT_BE` 0x0 →
`BE_QUEUE_INX`,
`rtw_xmit.c:4364-4368`, `do_queue_select` at `rtw_xmit.c:4401`) is the later performance
improvement (aggregation, RA rates) and requires building the full data `pkt_attrib`
(`psta`, `ether_type`, EAP/ARP branches of `:140-224`); it stays out of this spec.

### 4.4 Port 1 and the TX descriptor

Port 1 (`rtlRadioAwdlPort`, `AWDLTx.c:77-102`) does not affect the TX descriptor: `update_txdesc`
(`rtl8814au_xmit.c:60-305`) does not read `REG_MACID1`/`REG_BSSID1` nor the network type bits of
port 1;
the only port-dependent fields of the tree (`fill_txdesc_force_bmc_camid`) are under
`CONFIG_CONCURRENT_MODE`, disabled. The frame goes out on the radio operating channel (parked on
the social channel by `rtlRadioSetChannel`, `AWDLTx.c:111-127`, or on the BSS channel). Port 1
only contributes RX: hardware ACK for the unicast frames peers send to the MAC of `awdl0`
(`AWDL_RTL8814_RX_ACK.md` §2.4; `RCR_CBSSID_DATA` stays clear while the port is
active, `AWDLTx.c:100`).

## 5. Changes per kext

### 5.1 `RTL8814AirLayer` (`Source/AirLayer`)

1. **Replace the action** (`AWDLInterface.cpp:289-303`): new `RTLAwdInterface::txDataAction` with
   the same signature as `txNoop` (§2.1). Per packet: `getDataVirtualAddress() + getDataOffset()`
   and
   `getDataLength()` (read pattern of the STA layer, `RTLAirLayer.cpp:418-421`), DA at +0;
   build the frame of §3.2 into its own linear buffer (≤ 1468 B); call the radio
   (`callPlatformFunction(RTL_RADIO_FN_TX_DATA, …)` with `_radioSvc`, pattern of `sendAf`,
   `AWDLInterface.cpp:924-938`); count the result. Then return **all** the packets via
   `_txCompQ` in batches of 64 (`n < 128`, §2.4) and `return (IOReturn)count`. Failure of
   `enqueuePackets` → `deallocatePacket` of that batch (like `txNoop`,
   `AWDLInterface.cpp:300-301`).
   Guards: without `_txAllowed` (boot-arg `rtlawdl_tx`) or missing radio → return the packets
   without transmitting and count (never return 0 with pendings nor an error, §2.2).
2. **Per-DA sequence counter**: small table in the interface (e.g. 16 entries
   `{mac[6], u16 seq}` with linear search and LRU replacement), initialized to 0, value used before
   incrementing, wrap at 0x0FFF, AC nibble at 0 (§3.2). Under `_txLock`.
3. **Multicast phase 1**: multicast DA → immediate transmit with its own counter; separate
   `mcast` counter in the statistics (§6).
4. **Multicast phase 2 (optional, after seeing phase 1 with a real peer)**: overrides of
   `+0xF08`/`+0xF10`/`+0xF18` in `RTLAwdInterface` (the shim declarations already fix the
   offsets, `AWDLInterface.h:173-176`) + own duplicate + `stashMulticastPacketPair`, per §3.4.
5. **Do not touch** the input packets: the family requires no status or flags (§2.5); nor is
   `setLinkHeaderLength` needed on the kernel packets (only on own ones, §2.6).
6. Boot-args: reuse `rtlawdl_tx` as the general TX switch (today it governs the action
   frames, `AWDLInterface.cpp:126-128`); data go out with it. Add no new switches.

### 5.2 `RTL8814Native` (`Source/Driver`, `Source/rtl8814au`)

1. `RTLRadioABI.h`: `RTL_RADIO_FN_TX_DATA` + comment (§4.1).
2. `AWDLTx.c`: `rtl8814native_radio_tx_data` per §4.2 (declaration next to
   `rtl8814native_radio_tx_80211`, line 16).
3. `RTL8814Native.cpp`: case `RTL_RADIO_FN_TX_DATA` in the dispatch (pattern 432-512).
4. Nothing in `Source/rtl8814au` beyond the above: the management queue, the
   `MGNT_FRAMETAG` descriptor and the USB split already exist.

Dependency order: 5.2 before 5.1 (the action calls the radio; without the new function it answers
`kIOReturnUnsupported` and the action only counts). Step 5.1.4 is independent and later.

## 6. Logs

With `airLog` (like the rest of the interface), the only visible channel on the RTL8814Native
node:

| Event | Line | Frequency |
|---|---|---|
| TX batch | `awdl: tx data n=%u u=%llu m=%llu fail=%llu drop=%llu` | first and then every 256 batches |
| First frame of each type | hex of the first 64 B + `da=%02x:…:%02x len=%u seq=%u ch=%u` | once unicast, once multicast |
| Radio failure | `awdl: radio tx data -> 0x%x` | first 16 and then every 256 |
| Queue | `awdl: txq stalled` if the action consumes 0 with pendings | should not happen; diagnostic |

The channel/parking state is already printed by the tick (`AWDLInterface.cpp:756-757`).

## 7. Tests without an Apple peer

1. **Static**: compiled object with `nm -u` (the new symbols of §2.6 must appear as
   undefined and resolve against the KC; pattern of the previous ABI audit); kext built without
   loading.
2. **Stack exercise without valid RF**: with `awdl0` up (`rtlawdl=1 rtlawdl_rx=1 rtlawdl_tx=1`),
   `ping6 -c 3 ff02::1%awdl0` (link-local multicast, NDP answers) and `ping6 fe80::1%awdl0`
   (unicast to a nonexistent one: advertisement solicitations). The action fires from the work loop
   with the kernel packets: verify in the log the batch counter, the hex of the first frame and
   that the radio answers (expected failure if the channel is not social: the tick warns,
   `AWDLInterface.cpp:787-793`). This validates the whole §2 contract without depending on the
   ether.
3. **Return cycle**: check the pool does not run out (`txNoop` counters today, `txDataAction`
   later; no pool-full messages) and that `en2` keeps transmitting (the completion queue is the
   same mechanics).
4. **RF with an external observer**: owlsim/ESP32 in monitor on the social channel (peer guide in
   `~/Desktop/AWDLLab`, `AWDL_REFERENCIAS_EXTERNAS.md` §6): the frame must decode as AWDL-13
   and the seq field must grow by 1 per DA; compare the first bytes with the golden bytes of
   `frame.rs:391-400` (`… 30 12 | aa aa 03 00 17 f2 08 00 | 03 04 07 00 00 00 86 dd | …`).
5. **Own RX `?`**: the radio hook delivers every frame with A3 = AWDL BSSID; a custom data frame
   usually does not come back through the radio (the MAC does not receive itself without promiscuous),
   so the self-loop is not guaranteed; if the hardware allows it, it would serve to see the project's own
   frame in the RX log (`AWDLInterface.cpp:549-552` already counts data).
6. **ACK and retries**: with a neighbor that ACKs (an AP on the social channel does not ACK AWDL;
   without an Apple peer there is no real ACK) the retry counter can only be verified via the
   vendor counters `?` — left for the stage with a peer.

## 8. Open points (`?`)

1. Broadcom firmware interior: QoS subtype chosen for a single MSDU, rates and duration it
   adds to the 802.11 header (§3.1); the captures of `AWDL_REFERENCIAS_EXTERNAS.md` §5 are the
   starting reference.
2. Semantics of the high nibble of the seq field (AC on Broadcom) for an Apple receiver; with a
   single send queue it is emitted as 0 (§3.2).
3. Duration on the air: whether the RTL MAC fills the field or the frame goes out with 0 (§3.2).
4. MAC decision on ACK for group A1 on the management queue (§4.3) — observable in tests
   with a peer.
5. `kern_packet_tx_completion` with `[txCompQueue+0xE8] = 0` (§2.5): the interior has not been
   dumped; the operational evidence is `en2`.
6. Interior of `rtl_pmf_protect_mgnt` on data frames without applicable PMF keys (§4.2).
7. Multicast re-feed: full phase 2 (stash + timer + overrides) without a peer listening in windows
   is not observable; only its call mechanics.

## 9. Address verification

Pass: copy of the annotated IDB at `Build/ida_awdl/task_tx_datos/boot_awdl_txdatos.i64`, scripts
`verify_tx_datos.py` / `verify2.py` / `verify3.py`, outputs `verify.txt` / `verify2.txt` /
`verify3.txt`. Checked:

- **Function heads** (function start == cited address, symbol name verified):
  44/44, including all of §2.1, §2.4, §2.5, §2.6, §3.1 and §3.4 (`verify.txt`, `NAMECHECK`
  blocks).
- **Full dumps**: `legacyDequeue` (`0xffffff8002b2c860`–`0xffffff8002b2c986`),
  `enqueuePackets` TXComp (`0xffffff8002b2dbb6`–`0xffffff8002b2dc6d`) and its chained variant
  (`0xffffff8002b2dc6e`–`0xffffff8002b2de00`), `completePackets`
  (`0xffffff8002b12b9e`–`0xffffff8002b12cb0`), `withPool` TX (7 parameters, `0xffffff8002b0ca86`–
  `0xffffff8002b0caab`), `initWithPool` TX (`0xffffff8002b0caac`–`0xffffff8002b0cbce`),
  `getTxDataOffset` (`0xffffff8002b0baba`–`0xffffff8002b0bae3`),
  `refillTxRingForNetworking` (`0xffffff8002b0d3e0`–`0xffffff8002b0d57c`), and in the second pass
  `withPool` of 8 parameters (`0xffffff8002b0ca62`), `IOSkywalkPacketQueue::initWithPool`
  (`0xffffff8002b08bc8`–`0xffffff8002b08c8d`), `withPool`/`initWithPool` of the completion queue
  (`0xffffff8002b12860`, `0xffffff8002b129cc`); third pass (`verify3.txt`): the 9-parameter
  factory (`0xffffff8002b0c9b4`–`0xffffff8002b0ca62`, parameter mapping to `initWithPool` via the
  vptr cell `+0x2A8` at `0xffffff8002b0ca37`) and `checkForWork`
  (`0xffffff8002b2c746`–`0xffffff8002b2c85d`, clears the stall bit and calls
  `packetSubmission`), from which the clarification of field `+0xF0` and of the `-1` at `+0xB8`
  comes.
- **Vtables cell by cell**: `__ZTV15IOSkywalkPacket` (`0xffffff8002b3b510`, cells
  `+0x10`–`+0x288`),
  `__ZTV26IOSkywalkTxCompletionQueue` (`0xffffff8002b406f0`, `+0x10`–`+0x2F8`),
  `__ZTV26IOSkywalkTxSubmissionQueue` (`0xffffff8002b3e068`, `+0x10`–`+0x2B8`); cited slots:
  packet `+0x130`…`+0x1F0` (§2.6), send queue `+0x1E8` `packetSubmission(bool)`, `+0x2A8`
  `requestDequeue`, `+0x2B0` `purgePackets` (cell `purgePackets` = `0xffffff8002b0d870`), `+0x2B8`
  `initWithPool`, completion queue `+0x2A8` `requestEnqueue`, `+0x2B0`/`+0x2B8`
  `enqueuePackets`, `+0x2C0` `initWithPool`.
- **Instruction points** (`SPOT` in `verify.txt`): the 34 of §3.1 and §2 (SNAP insertion,
  `+0x8A` counter, sequence, multicast, constants `8F2170003AAAAh` and `8F21700h`), all with the
  expected text.
- **Jump tables**: none in the dumped functions (§2.7).
- **Exports**: `syms_com.apple.iokit.IOSkywalkFamily.txt` (§2.6; `getNextPacket` exported
  `0x0f`, corrects `AWDL_RX_DATA_TO_SKYWALK.md` §7) and `syms.txt` of IO80211Family
  (`stashMulticastPacketPair` line 15742, `p2pMulticastTx` 15661, `freeMulticastPacket` 15701,
  `triggerAWDLMulticastTx` 15729).

## 10. Implementation relative to this specification

The data TX was implemented in parallel to this specification (`42c04d3`, `5630d49`, `0eb77c2`,
`f2969a4`). The action contract (§2) is met: `RTLAwdInterface::txData` always returns the
number of received packets, never an error code, and returns them all via the completion
queue in batches of 64, freeing them with `deallocatePacket` only if that queue rejects them.
Differences from what is proposed here:

- **ABI**: instead of a new `RTL_RADIO_FN_TX_DATA` function, `rtlRadioTx80211` also accepts
  data frames with A3 = AWDL BSSID (no `To/From DS`, no protection bit). An older driver
  answers `kIOReturnBadArgument` and the layer turns the data TX off once.
- **Send moment**: the action does not transmit. It copies each frame into a ring of 32 and the AW
  tick sends them while the window of a served slot is open, 4 per AW at most and none in
  the last AW of the slot; they expire after 1.2 s.
- **Rate**: OFDM 12 Mb/s for data and 6 Mb/s for action frames to the AWDL BSSID, never CCK;
  data are rejected with `kIOReturnNoResources` when fewer than 16 of the 32 management frames
  remain free.
- **802.11 sequence**: the hardware puts it (`HWSEQ_EN` for frames without QoS); the per-destination
  counter only goes in the AWDL header, with 12 bits and without the AC nibble.
- **Manager**: every packet goes through
  `IO80211AWDLPeerManager::preQueuePacketCheckQuietMode`
  ([AWDL_TX_HOOKS_POR_PAQUETE.md](AWDL_TX_HOOKS_POR_PAQUETE.md) §2).
- **Multicast**: without the stash model of §3.4; it goes out over the same ring.
