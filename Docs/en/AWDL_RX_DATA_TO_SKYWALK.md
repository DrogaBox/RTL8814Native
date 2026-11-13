# Delivery of received AWDL data frames to the `RxCompletionQueue` of `awdl0`

How a driver must deliver an AWDL data frame received on the air so that the stack sees it as an
Ethernet packet: the exact format on the air (owly spec, AWDL-13/14), the 802.11→Ethernet
conversion the Broadcom host does as a reference, the setter contract of the Skywalk packet with its mangled
names, the three enqueuing paths into the reception queue with signatures and flags, and which symbols an
external kext can link. Base: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241,
Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`), dumps and Hex-Rays decompilations over a
copy of the annotated database in `Build/ida_awdl/task_rx_data/` (`probe*.txt`, `probe*.py`), plus the
previous disassembly of `Build/ida_awdl/task_dataencap/dis_rxdrain.txt`. On-the-air protocol
reference: `~/Desktop/AWDLLab/refs/owly` (SPEC §3.5 and `crates/owly-awdl/src/frame.rs`). Addresses = KC
VAs. Unobserved items go with `?`. Complements `AWDL_DATA_ENCAP.md` (TX, RX and multicast) and
`AWDL_PROXIMITY_DATAPATH.md` §10; corrects two vtable details of that doc pointed out in §4 and §5.

## 1. Summary

1. On the air an AWDL data frame is a **three-address** 802.11 data frame (24 bytes, or 26 with
   QoS) with `To DS`/`From DS` at 0 and BSSID `00:25:00:ff:94:73`; the body is Apple SNAP
   `AA AA 03 00 17 F2 08 00` + an 8-byte AWDL data header (`03 04`, u16 LE sequence, `00 00`,
   BE ethertype `86 DD`) + IPv6. The QoS variant with the A-MSDU bit (`0x0080` of QoS control) carries
   subframes with their own addresses and lengths (owly AWDL-14).
2. The stack expects **Ethernet II at `buffer + dataOffset`**: DA, SA, ethertype, payload. Broadcom receives
   from the firmware the raw frame with flag 0x2 of the completion message and the host
   (`AppleBCMWLANPCIeSkywalk::rxCompRingDrain`) converts it to Ethernet reusing the 14 bytes of the
   802.11 header, detects the Apple SNAP, removes 16 bytes and re-seats the ethertype at +12. A driver without
   firmware does that conversion in software.
3. The packet is prepared with `IOSkywalkPacketBufferPool::allocatePacket` (from the pool of the
   interface itself), `IOSkywalkPacket::prepare` (null queue), copy of the frame, `setDataOffsetAndLength` and
   `setLinkHeaderLength(14)`; the final fitting is
   `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket const*, uint, uint)` in batches **with
   `n < capacity` (strict)** and flags at 0. Broadcom instead uses `stagePacket` + `requestEnqueue(0,0)`
   at the end of the batch: two entries of the same queue.
4. The family chains the kern packets, completes each packet (`completeWithQueue(queue, 2, 0)`), taps the
   input BPF if it is active, notifies the action of the queue and delivers with
   `kern_netif_queue_rx_enqueue(netif, chain, n, 1)`: from there the BSD ifnet receives an mbuf with
   the Ethernet header.
5. The symbols an external kext needs for this path (packet setters, pool
   allocation, enqueuing of the two completion queues, queue factory) are **exported** in the symtabs
   of the KC: 31 of the 35 checked with `kcsym.py` (type `0xf`); the remaining 4 are internal
   helpers (`gatedEnqueue`, `getNextPacket`, `getSourceQueue`, `getFreeSpace`) and do not export.

## 2. The AWDL data frame on the air (owly, AWDL-13/14)

Spec: `docs/SPEC.md` §3.5 (`AWDL-13` at line 299, `AWDL-14` at 310). Implementation:
`crates/owly-awdl/src/frame.rs`. Constants: `fc::DATA = 0x0008`, `fc::QOS_DATA = 0x0088` (lines
38-39), `HEADER_LEN = 24` (52), `DATA_HEADER_LEN = 16` (56), `MAX_AMSDU_SUBFRAMES = 16` (59),
`LLC_SNAP = AA AA 03 00 17 F2 08 00` (73), `DATA_HEAD = 0x0403` (74), `ETHERTYPE_IPV6 = 0x86dd` (75);
`BSSID = 00:25:00:ff:94:73` (`lib.rs:169`), `OUI = 00:17:f2` (`lib.rs:172`).

MAC header (three addresses; `parse_header`, `frame.rs:127-136`; golden test, `frame.rs:387-410`):

| offset | size | content |
|---:|---:|---|
| 0 | 2 | frame control: `0x0008` data or `0x0088` QoS data; `To DS`/`From DS` (bits 8-9) **at 0**; discard with `PROTECTED` (`0x4000`), `MORE_FRAGMENTS` (`0x0400`) or fragment ≠ 0 (`frame.rs:151`) |
| 2 | 2 | duration |
| 4 | 6 | addr1 = **destination** (receiver) |
| 10 | 6 | addr2 = **source** (transmitter) |
| 16 | 6 | addr3 = AWDL BSSID `00:25:00:ff:94:73` (`frame.rs:160`) |
| 22 | 2 | sequence control (12 sequence bits in the high ones) |
| 24 | 2 | **QoS only**: QoS control; A-MSDU bit = `0x0080` (`frame.rs:164-167`) |

Body of an MSDU (not A-MSDU: without the 2 bytes of QoS control; with A-MSDU: body of each subframe;
`msdu()`, `frame.rs:253-266`; `put_msdu`, `frame.rs:301-308`):

| MSDU offset | size | content |
|---:|---:|---|
| 0 | 8 | LLC/SNAP `AA AA 03` + OUI `00 17 F2` + protocol `08 00`. owly only validates `AA AA 03` and `08 00` (`frame.rs:257`, "The OUI isn't [checked]"); Broadcom additionally requires the full OUI (§3) |
| 8 | 2 | head `03 04` (u16 LE `0x0403`) |
| 10 | 2 | AWDL sequence u16 **LE**, per node, incremented per frame; the subframes of an A-MSDU carry consecutive numbers (`frame.rs:304`, `345`) |
| 12 | 2 | padding `00 00` |
| 14 | 2 | **real BE ethertype** (`0x86DD`) |
| 16 | n | IPv6 payload |

Golden bytes (`frame.rs:391-400`): `… 30 12 | aa aa 03 00 17 f2 08 00 | 03 04 07 00 00 00 86 dd | IPv6`.

A-MSDU variant (`msdus()`, `frame.rs:228-251`; `amsdu_frame()`, `frame.rs:326-348`): after the QoS
control, each subframe is destination (6) + source (6) + **BE16 length** (value = `16 + length of the
payload`, `frame.rs:344`) + the body above; between subframes (not after the last) padding to a multiple
of 4 (`pad = (4 − (14+len) % 4) % 4`, `frame.rs:246`); maximum 16 subframes per frame (AWDL-14, limit
of `owl-08`). owly transmits single-MSDU frames **without QoS** (`fc::DATA`,
`frame.rs:313`) and the A-MSDUs with QoS and TID 0 (`frame.rs:334-335`); a receiver must accept both
subtypes (`frame.rs:157`).

**Ethernet reconstruction** (`msdus()`+`msdu()`, `frame.rs:228-266`; `node.rs:483`): for each MSDU,
`DA` = addr1 (not A-MSDU) or the subframe destination; `SA` = addr2 (not A-MSDU) or the subframe source;
`ethertype` = the BE16 at +14 of the MSDU; payload = +16. The frame is `[DA][SA][ethertype][payload]`.
owly additionally filters `ethertype == 0x86dd` and `DA == own || multicast` (`node.rs:483`).

## 3. The format the RX queue expects (Broadcom reference, 25G241)

`AppleBCMWLANPCIeSkywalk::rxCompRingDrain(AppleBCMWLANByteRing*, void*, uint)` (`0xffffff8001549dea`;
function start verified in the IDB). Per message of the completion ring: u16 length at +0x0E,
u16 data offset at +0x10, u16 flags at +0x12 (`movzx r12d, word ptr [rax+12h]` at
`0xffffff800154a1ba`). The frame pointer is formed by adding to the packet the current headroom
(`getHeadroom()`, `0xffffff800154a1a3`) plus the data offset of the message (`add r15b, r13b` at
`0xffffff800154a1bf`), setting it with `setHeadroom(headroom + msgOff)` (`0xffffff800154a1fa`) and reading
`getVirtualAddress() + that value` (`0xffffff800154a205`–`0xffffff800154a20b`); later it leaves it
consistent with the normal contract: `setDataOffset(headroom + msgOff)` (`0xffffff800154a603`) and frame
= `getVirtualAddress() + getDataOffset()` (`0xffffff800154a62a`–`0xffffff800154a645`). What the
firmware places between the pool headroom and the frame is `?`.

Role 6 path (the VAs are inside `rxCompRingDrain`; full dump in
`Build/ida_awdl/task_dataencap/dis_rxdrain.txt`):

1. Minimum length: `< 28` → discard "Too small (len %d)" (`cmp word ptr [rbp+var_58], 1Ch` at
   `0xffffff800154a1a8`). 28 = 14 (pseudo-Ethernet) + 8 (SNAP) + `03 04` + seq + `00 00`; the real ethertype
   goes at +28..+30 of the internal format.
2. **802.11→Ethernet conversion** if flag `0x2` is in the message (`test r12b, 2` at
   `0xffffff800154a1df`, block at `0xffffff800154a51d`–`0xffffff800154a58a`): it requires 802.11 version 0
   (`test byte ptr [rax+1], 3`, `0xffffff800154a520`), a data frame (`and dl, 0Ch; cmp dl, 8`,
   `0xffffff800154a52e`), length ≥ 24+2·QoS+4 (`0xffffff800154a540`); with the QoS bit of the frame
   control (`shr cl, 7`, `0xffffff800154a53a`) the header measures 24 or 26 (`0xffffff800154a550`).
   It builds the Ethernet inside its own header: **DA = addr1** (`0xffffff800154a569`/
   `0xffffff800154a570`), **SA = addr2** (`0xffffff800154a559`/`0xffffff800154a562`), advances the offset
   `hdrLen−14` (`0xffffff800154a579`) and trims `(hdrLen−14)+4` from the length (`0xffffff800154a58a`),
   where the 4 are the FCS. The frame becomes `[DA][SA][802.3 length][SNAP 8][03 04][seq][00 00][ethertype][payload]`
   (internal host↔firmware Broadcom format; the length field at +12 does not exist on the air).
3. **Apple SNAP**: checks `AA AA 03` at +14 (`0xffffff800154a20f`–`0xffffff800154a22d`) and the dword
   at +17 against `0x08F21700` = bytes `00 17 F2 08` (`cmp eax, 8F21700h` at `0xffffff800154a238`):
   - Match (AWDL): copies DA+SA to +16/+24, advances headroom +16 (`add cl, 10h` at
     `0xffffff800154a25c`), trims 16 from the length (`0xffffff800154a263`) and `setHeadroom`
     (`0xffffff800154a271`). Result: **Ethernet II `[DA][SA][ethertype][payload]`**, with the
     ethertype of +28 re-seated at +12, `dataOffset` +16 and length −16.
   - OUI ≠ 0: discard "BAD OUI … Ethertype = 0x%04x" (`0xffffff800154a713`).
   - OUI 0 (RFC 1042): removes 8 bytes promoting the SNAP protocol to ethertype and diverts to the
     LLW interface (id 9, `getSkywalkInterface(core, 9)`, `0xffffff800154aa4b`). Without SNAP: requires
     ethertype ≥ 0x600 (`cmp word ptr [r12+0Ch], 600h` at `0xffffff800154a5b3`) and also LLW. **Only
     the frames with the Apple SNAP reach the queue of `awdl0`.**
4. Final setters (§4), pre-hook `inputPacket` (§6), `setRXTimestamp`
   (`__ZN23AppleBCMWLANPCIeSkywalk14setRXTimestampEPvP29AppleBCMWLANPCIeSkywalkPacketP23IO80211SkywalkInterfaceb`
   at `0xffffff800154a3fb`) and fitting with `stagePacket(pkt, false, false)` (`0xffffff800154a44c`,
   `call [rax+340h]`).
5. At the end of the batch, for each interface 0..11 (`getSkywalkInterface(core, i)` via slot 632
   (`+0x13C0`) of `AppleBCMWLANCore`, `0xffffff800154aba5`): `getRxCompQueue` (slot `+0xD70`,
   `0xffffff800154abb9`) and `requestEnqueue(0, 0)` (`call [rax+298h]` at `0xffffff800154abd1`);
   `flushPackets` (`call [rax+330h]` at `0xffffff800154abef`) if it fails; `scheduleRxCompleteLegacy`
   (`0xffffff800154acfc`) refills the ring.

There is no A-MSDU handling on this host path: the subframe is not split and the result would be an
Ethernet frame whose payload starts with the destination/source/length of the subframe. Whether the
Broadcom firmware disaggregates the A-MSDUs before the completion message is `?`. owly disaggregates them in software
(`frame.rs:228-251`) and AWDL-14 declares it mandatory for the receiver.

## 4. The Skywalk packet contract

Convention: the `call [vptr+X]` calls of the code read cell `ZTV+X+0x10`. Vtables verified
cell by cell (`Build/ida_awdl/task_rx_data/probe.txt` and `probe2.txt`): `__ZTV15IOSkywalkPacket`
`0xffffff8002b3b510`, `__ZTV22IOSkywalkNetworkPacket` `0xffffff8002b3ba68`,
`__ZTV29AppleBCMWLANPCIeSkywalkPacket` `0xffffff80015ae548`,
`__ZTV26IOSkywalkRxCompletionQueue` `0xffffff8002b3e890`,
`__ZTV40AppleBCMWLANPCIeSkywalkRxCompletionQueue` `0xffffff80015ad9c8`,
`__ZTV25IOSkywalkPacketBufferPool` `0xffffff8002b3b1c8`. Correction to `AWDL_DATA_ENCAP.md` §8: in the
vtable of the family, `setDataLength` is at cell `+0x148` (not `+0x138`) and `getDataVirtualAddress`
at `+0x188` (not `+0x178`); the subclass offsets cited there (`+0x1F0`/`+0x1F8`/`+0x200` of the PCIe
packet) correspond to cells `+0x200`/`+0x208`/`+0x210`.

Slots used by `rxCompRingDrain` on the packet (all calls verified in the disassembly
and resolved against `__ZTV29AppleBCMWLANPCIeSkywalkPacket`):

| call | ZTV cell | target |
|---|---|---|
| `[vptr+0x138]` at `0xffffff800154a613` | `+0x148` | `__ZN15IOSkywalkPacket13setDataLengthEj` — `setDataLength(uint)`, `0xffffff8002b2c454` |
| `[vptr+0x148]` at `0xffffff800154a603` | `+0x158` | `__ZN15IOSkywalkPacket13setDataOffsetEt` — `setDataOffset(ushort)`, `0xffffff8002b2c5c0` |
| `[vptr+0x150]` at `0xffffff800154a639` | `+0x160` | `__ZNK15IOSkywalkPacket13getDataOffsetEv` — `0xffffff8002b2c5e2` |
| `[vptr+0x1F0]` at `0xffffff800154a62a` | `+0x200` | `__ZN29AppleBCMWLANPCIeSkywalkPacket17getVirtualAddressEv` — `0xffffff8001548498` |

Decompiled semantics (`probe2.txt`, `probe3.txt`):

- `IOSkywalkPacket::setDataLength(uint)` (`0xffffff8002b2c454`): only with **one buffer**
  (`getPacketBufferCount() == 1`); delegates to `IOSkywalkPacketBuffer::setDataLength(buffer, uint)`
  (`0xffffff8002b2c602`), which rejects `len + offset_of_buffer > size of segment` with `0xE00002C2`.
  With more than one buffer it returns `0xE00002C7`.
- `setDataOffset(ushort)` (`0xffffff8002b2c5c0`): requires ≥ 1 buffer (else `0xE00002F0`); delegates to
  `IOSkywalkPacketBuffer::setDataOff`.
- `setDataOffsetAndLength(ushort, uint)` (`0xffffff8002b2c538`,
  `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj`): the complete variant — validates
  `bufferSize >= offset` and `len + offset ≤ bufferSize × number of buffers` (else `0xE00002C2`) and splits
  over buflets with `IOSkywalkPacketBuffer::setDataOffAndLen`. It is what the RTL layer uses
  (`Source/AirLayer/RTLAirLayer.cpp:194`). It is the safe path with `maxBuffersPerPacket = 1` and also
  with buflets.
- `getDataVirtualAddress()` (`0xffffff8002b06518`, `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv`):
  returns the **virtual address of the segment + `memorySegmentOffset` of the first buffer**, without adding the
  `dataOffset`: the frame is at `getDataVirtualAddress() + getDataOffset()`.
- `prepare(IOSkywalkPacketQueue*, ulonglong, uint)` (`0xffffff8002b0658a`,
  `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj`): if byte +88 already has bit 2 it returns
  0 (already prepared); with a non-null queue it reads its address; it calls the virtual `prepareWithQueue` (vptr+392 →
  cell `ZTV+0x198`: for a family packet, `IOSkywalkPacket::prepareWithQueue(IOSkywalkPacketQueue*, uint, uint)`,
  `0xffffff8002b065d6`; for the PCIe packet, `IO80211NetworkPacket::prepareWithQueue`,
  `0xffffff8002350f9a`). A freshly allocated packet **needs `prepare` before touching the
  buffer**: without it `getDataVirtualAddress` fails (panic 0.15 documented at
  `Source/AirLayer/RTLAirLayer.cpp:456-460`).
- Direct (non-virtual) methods of `IOSkywalkNetworkPacket`: `setLinkHeaderLength(uchar)`
  `0xffffff8002b07180` (`__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh`, used by the RTL layer
  with 14), `setLinkHeaderOffset(uint)` `0xffffff8002b071a8` (`__ZN22IOSkywalkNetworkPacket19setLinkHeaderOffsetEj`,
  used by Broadcom at `0xffffff800154a61f` with the same value as the `dataOffset`) and
  `setHeadroom(uchar)` `0xffffff8002b07158`.

Allocation (`__ZTV25IOSkywalkPacketBufferPool`; the family vtable does not expose allocation as a
top-level virtual, they are direct/virtual methods of their own):

- `allocatePacket(IOSkywalkPacket**, uint)` (`0xffffff8002b058ee`,
  `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj`): wrapper calling the
  virtual `allocatePacket(uint, IOSkywalkPacket**, uint)` (cell `ZTV+0x140`,
  `0xffffff8002b2bed2`) with `count = 1`. The second uint is the options of the `kern_pbufpool_alloc`
  (the RTL layer passes `1` = nosleep, `Source/AirLayer/RTLAirLayer.cpp:198` and `:250`).
- Batch variants: `allocatePackets(uint, uint*, IOSkywalkPacket**, uint)` `0xffffff8002b2bf7c`;
  `deallocatePacket(IOSkywalkPacket*)` `0xffffff8002b2c0de`;
  `deallocatePackets(IOSkywalkPacket**, uint)` `0xffffff8002b2c128`.

## 5. Enqueuing into the `IOSkywalkRxCompletionQueue`

Cells of the PCIe subclass (`__ZTV40AppleBCMWLANPCIeSkywalkRxCompletionQueue`, ZTV offsets; cell
X equals `vptr+X-0x10`):

| ZTV cell | VA | method |
|---|---|---|
| `+0x2A8` | `0xffffff8001546d18` | `__ZN40AppleBCMWLANPCIeSkywalkRxCompletionQueue14requestEnqueueEPvj` (PCIe override of `requestEnqueue(void*, uint)`) — reachable as `call [vptr+0x298]` (`0xffffff800154abd1`) |
| `+0x2B0` | `0xffffff8002b2d766` | family: `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` — `enqueuePackets(IOSkywalkPacket const*, uint, uint)` |
| `+0x2B8` | `0xffffff8002b2d810` | family: `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEP15IOSkywalkPacketjj` — variant that walks a linked **chain** with `setNextPacket` |
| `+0x328` | `0xffffff80015468b8` | Broadcom: `enqueuePackets(OSObject*, IO80211NetworkPacket**, uint, void*)` |
| `+0x340` | `0xffffff80015466fe` | Broadcom: `flushPackets(void)` |
| `+0x350` | `0xffffff8001546cc8` | Broadcom: `stagePacket(IO80211NetworkPacket*, bool, bool)` |
| `+0x358` | `0xffffff8001546852` | Broadcom: `freePendingPackets(IO80211NetworkPacket**, uint)` |

The three fitting paths (`probe2.txt`, `probe4.txt`, `probe6.txt`):

1. **`enqueuePackets(IOSkywalkPacket const**, uint n, uint opts)`** (`0xffffff8002b2d766`): the path of
   the RTL layer (`Source/AirLayer/RTLAirLayer.cpp:202` and `:477`, batches of 64 with `opts = 0`).
   Requirements: `pkts != 0`, `n != 0` and **`n < capacity` (strict comparison** against the field
   `+172` of the queue; 256 in the STA layer, `Source/AirLayer/RTLAirLayer.cpp:380`; 128 in the queue of
   `awdl0` of stage 1, `Source/AirLayer/AWDLInterface.cpp:189`). Errors:
   `0xE00002C2` (null/zero/capacity), `0xE00002D8` (no work loop), `0xE00002D7` (disabled or not
   initialized), `0xE00002D5` (byte `+280` bit 0 set). On success returns 0. The `opts` **does not reach**
   the networking helper on 25G241 (it is only forwarded on the KPipe route): no observable effect
   (`?` intended semantics).
2. **Chained variant** `enqueuePackets(IOSkywalkPacket*, uint, uint)` (`0xffffff8002b2d810`):
   walks the `getNextPacket` chain up to `n` packets, builds the kern chain, taps BPF if applicable,
   calls `kern_netif_queue_rx_enqueue(netif, chain, n, 1)` and the action of the queue. Linking the
   packets requires `setNextPacket` (`__ZN15IOSkywalkPacket13setNextPacketEPS_`, exported).
3. **Broadcom model** (what `rxCompRingDrain` does): per packet
   `stagePacket(pkt, false, false)` (`0xffffff8001546cc8` — only hooks the packet into the pending
   list of the queue: anchor at ctx+72, head at ctx+80, no delivery) and, at the end of the batch,
   `requestEnqueue(0, 0)`: the family (`0xffffff8002b2d6d8`,
   `__ZN26IOSkywalkRxCompletionQueue14requestEnqueueEPvj`) with bit 0 of the second argument at 0 does
   `gatedEnqueue(this, 0)` under lock if the queue is enabled (it drains the pending ones); with bit 0 at 1
   it only increments counters and `signalWorkAvailable`. `flushPackets` releases the pending ones if
   `requestEnqueue` fails.

What the family does on fitting (`enqueuePacketsForNetworking`,
`__ZN26IOSkywalkRxCompletionQueue27enqueuePacketsForNetworkingEPKP15IOSkywalkPacketjj`,
`0xffffff8002b0f33c` — internal but exported, §7): per packet, it reads the kern chain
(`kern_packet_get_next` implicit via `getSourceQueue`/qword +40 of the packet), calls
`completeWithQueue(queue, 2, 0)` (vptr+408 → cell `ZTV+0x1A8` of `IOSkywalkPacket`,
`__ZN15IOSkywalkPacket17completeWithQueueEP20IOSkywalkPacketQueuejj`), `disposePacket` (vptr+480),
taps `bpf_tap_packet_in` if the queue has it active (byte `+308` bit 0), sums bytes and at the end notifies the
action of the queue (vtable +616) and delivers everything with
**`kern_netif_queue_rx_enqueue(netif, chain, n, 1)`**: there is born the mbuf the BSD ifnet sees. There is
no examination of the frame content anywhere in the family: what enters this queue must already carry the
Ethernet header at `buffer + dataOffset`.

## 6. `inputPacket`: the pre-hook of the interface (peer filter and PPP retention)

Before fitting, Broadcom calls slot `+0xB50` of the interface (`call [rax+0B50h]` at
`0xffffff800154a65c`) = `IO80211VirtualInterface::inputPacket` (`0xffffff8002333cac`), with
`(packet, origin, RX tag, &flag, 0)`. The tag is a buffer whose byte 0 marks multicast (bit
0) and whose offset +6..+11 carries the **SA**; for role 6 the family looks up the peer by that SA
(`findPeer(manager, etq+6)`) and, if it does not exist, discards ("Dropping … because no peer was found for
%02X:…", `incrementNumInputPacketsDropped`), with special cases for DAs `33:33:00:00:xx:xx`,
`33:33:80:00:00:FB` `?` and `01:00:5E:00:xx:xx` that consult AWDL peer state before fitting;
for role 2 the filter does not apply. A packet held awaiting ioctl 415 returns to the datapath via
slots `+0xF30`/`+0xF38` of the interface (`AWDL_PEER_PRESENCE_PPP.md` §3-§4;
`AWDL_PROXIMITY_DATAPATH.md` §11). The call is part of the Broadcom design; the family does not filter anything
the driver enqueues without it — omitting it lets frames of unknown peers through.

## 7. Symbols linkable by an external kext

Result of `kcsym.py` (symtabs of `LC_SYMTAB`/`LC_FILESET_ENTRY` of KC 25G241; `0xf` = external
defined in its section). The VAs of the first column are verified in the IDB; an undefined symbol of a kext
resolves by name.

| mangled symbol | VA 25G241 | use |
|---|---|---|
| `__ZN15IOSkywalkPacket13setDataLengthEj` | `0xffffff8002b2c454` | length (1 buffer) |
| `__ZN15IOSkywalkPacket13setDataOffsetEt` | `0xffffff8002b2c5c0` | data offset (u16) |
| `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj` | `0xffffff8002b2c538` | offset+length over buflets |
| `__ZN15IOSkywalkPacket10setDataOffEx` | — (`0xf`) | internal data off (i64) |
| `__ZN15IOSkywalkPacket16setDataOffAndLenExm` | `0xffffff8002b2c4ac` | i64/u64 variant |
| `__ZNK15IOSkywalkPacket13getDataLengthEv` | — (`0xf`) | length |
| `__ZNK15IOSkywalkPacket13getDataOffsetEv` | — (`0xf`) | data offset |
| `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv` | `0xffffff8002b06518` | buffer base (without dataOffset) |
| `__ZNK15IOSkywalkPacket20getPacketBufferCountEv` | — (`0xf`) | number of buflets |
| `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj` | `0xffffff8002b0658a` | prepare new packet |
| `__ZN15IOSkywalkPacket17completeWithQueueEP20IOSkywalkPacketQueuejj` | — (`0xf`) | called by the family |
| `__ZN15IOSkywalkPacket13setNextPacketEPS_` | — (`0xf`) | chain variant 2 of §5 |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh` | `0xffffff8002b07180` | link header length (14) |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderOffsetEj` | `0xffffff8002b071a8` | link header offset |
| `__ZN22IOSkywalkNetworkPacket11setHeadroomEh` | `0xffffff8002b07158` | headroom (Broadcom RX) |
| `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` | `0xffffff8002b2d766` | **RX fitting in batches** |
| `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEP15IOSkywalkPacketjj` | `0xffffff8002b2d810` | chained variant |
| `__ZN26IOSkywalkRxCompletionQueue14requestEnqueueEPvj` | `0xffffff8002b2d6d8` | kick (stage model) |
| `__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j` | `0xffffff8002b0ee04` | create the queue (used by stage 1, `Source/AirLayer/AWDLInterface.cpp:48`) |
| `__ZN26IOSkywalkRxCompletionQueue12initWithPoolE…` (same type list) | `0xffffff8002b0eeae` | equivalent init (cell `ZTV+0x2C0`) |
| `__ZN26IOSkywalkRxCompletionQueue27enqueuePacketsForNetworkingEPKP15IOSkywalkPacketjj` | `0xffffff8002b0f33c` | internal helper, exported |
| `__ZN26IOSkywalkRxCompletionQueue22enqueuePacketsForKPipeEPKP15IOSkywalkPacketjj` | — (`0xf`) | internal helper, exported |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj` | `0xffffff8002b058ee` | allocate 1 packet |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEjPP15IOSkywalkPacketj` | `0xffffff8002b2bed2` | allocate n (virtual, cell `ZTV+0x140`) |
| `__ZN25IOSkywalkPacketBufferPool15allocatePacketsEjPjPP15IOSkywalkPacketj` | `0xffffff8002b2bf7c` | batch with real count |
| `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `0xffffff8002b2c0de` | return packet (on errors) |
| `__ZN25IOSkywalkPacketBufferPool17deallocatePacketsEPP15IOSkywalkPacketj` | `0xffffff8002b2c128` | batch |
| `__ZN25IOSkywalkPacketBufferPool12initWithNameEPKcP8OSObjectjPKNS_11PoolOptionsE` | — (`0xf`) | create the pool |
| `__ZN21IOSkywalkPacketBuffer13setDataLengthEj` | `0xffffff8002b2c602` | buffer length |
| `__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` | — (`0xf`) | TX return (already used) |
| `__ZN26IOSkywalkTxSubmissionQueue25refillTxRingForNetworkingEjjb` | `0xffffff8002b0d3e0` | TX: ring refill (reference; `kern_netif_queue_tx_dequeue`) |

Not exported (checked MISSING with `kcsym.py`): `__ZN26IOSkywalkRxCompletionQueue13gatedEnqueueEPv`,
`__ZNK15IOSkywalkPacket13getNextPacketEv`, `__ZN15IOSkywalkPacket13getSourceQueueEv`,
`__ZNK20IOSkywalkPacketQueue9getFreeSpaceEv`. The family vtable of the pool has no
allocation cell: `allocatePacket` is linked by direct name (as the RTL layer already does,
`Source/AirLayer/RTLAirLayer.cpp:198`).

## 8. Recipe for the RTL8814AU driver (RX of AWDL data → `awdl0`)

The radio hook already delivers the raw frame with A3 = AWDL BSSID to the kext (`ccc4122`); the **data**
frames arriving through it are processed thus (all in software, no firmware):

1. Validate: version 0, data subtype (QoS or not), `To DS`/`From DS` = 0, no `PROTECTED` nor
   fragmentation; BSSID at addr3 = `00:25:00:ff:94:73` (owly AWDL-1, `lib.rs:169`).
2. Header: 24 bytes, +2 if the QoS bit (`0x0080` of the frame control). If the QoS control carries the
   A-MSDU bit (`0x0080` of QoS control), split the subframes (destination 6, source 6, BE16 length = 16+n,
   body, padding to 4 between subframes, maximum 16 — AWDL-14); the Broadcom host does not do it (§3), it is
   the driver's responsibility.
3. Per MSDU: require `AA AA 03` at +0 and, conservatively, the dword `00 17 F2 08` at +3 (Broadcom,
   `0xffffff800154a238`; owly does not check the OUI, `frame.rs:257`); ethertype = BE16 at +14; payload =
   +16; discard the SNAP frames with OUI ≠ `00:17:F2` (Broadcom sends those to `llw0`; without SNAP only with
   ethertype ≥ 0x600).
4. Build Ethernet II: DA = addr1 or subframe destination; SA = addr2 or subframe source;
   `[DA][SA][ethertype][payload]`. The AWDL sequence of +10 of the MSDU is dropped (no state).
5. Packet: `allocatePacket(pool_rx, &pkt, 1)` (nosleep) → `prepare(pkt, nullptr, 0, 0)` → copy of the
   frame → `setDataOffsetAndLength(pkt, 0, len)` (offset 0 works; the frame is
   `getDataVirtualAddress() + getDataOffset()`) → `setLinkHeaderLength(pkt, 14)`. Errors →
   `deallocatePacket`. (Exact sequence already operative in the STA layer:
   `Source/AirLayer/RTLAirLayer.cpp:441-478`.)
6. Recommended, like Broadcom: `inputPacket(pkt, origin, tag, &flag, 0)` (§6) for the peer
   filter and PPP retention; if there is no peer, return the packet to the pool.
7. Fitting: batches via `enqueuePackets(rxCompQ, pkts, n, 0)` with `n < 128` (capacity of the queue of
   `awdl0` of stage 1, `Source/AirLayer/AWDLInterface.cpp:189`; chunks of 64 like the STA layer), or the
   Broadcom model `stagePacket` + `requestEnqueue(0,0)` + `flushPackets`. Fitting failure →
   `deallocatePacket` of the whole batch. The queue must be enabled and with an event source added to the work
   queue (already done in stage 1, `Source/AirLayer/AWDLInterface.cpp:201`).
8. On TX the symmetric one is already documented (`AWDL_DATA_ENCAP.md` §7): from the Ethernet II of the packet to
   `802.11 + SNAP + 03 04 + seq + 00 00 + ethertype`, counter per destination.

## 9. Not verified (`?`)

1. Whether the Broadcom firmware disaggregates A-MSDUs before the completion message (the host does not, §3).
2. Intended semantics of the `opts` of `enqueuePackets` (accepted without observable effect on the
   networking route of 25G241) and of byte `+280` bit 0 that produces `0xE00002D5`.
3. The special DAs of `inputPacket`: the exact `33:33:80:00:00:FB` branch versus the wildcard
   `33:33:00:00:xx:xx` was traced at the level of byte checks (`probe3.txt`), without protocol name; and which
   entity fills the RX tag of Broadcom (in the RTL driver the kext itself fills it).
4. What the Broadcom firmware places between the pool headroom and the frame on RX (data offset field of the
   message) — irrelevant for a driver without firmware.
5. The whole test on RTL8814 hardware (pending stage; the STA side of this recipe already runs on `en2`).
