# Data encapsulation in the `awdl0` datapath (TX, RX and multicast)

Exact format of the packet at both ends of the role-6 interface's data datapath: what the
Skywalk Tx submission queue action receives, who inserts the AWDL encapsulation on Broadcom, what the
reception queue expects, and what the RTL8814 driver must build and strip when crossing toward USB. Base:
kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin 25.6.0, md5
`f489ff8be80e6013a1bf47561dcaef45`), reading of the disassembly and vtable dumps over a copy
of the annotated kernelcache database in `Build/ida_awdl/task_dataencap/`. Addresses = VA of the
KC. Anything unobserved carries `?`. Complements `AWDL_PROXIMITY_DATAPATH.md` (§9–§10) and
`AWDL_STAGE2B_MBUF_TO_PACKET.md`; it corrects three details of that doc, pointed out in §5 and §8.

## 1. Summary

1. **TX**: the action of the `IOSkywalkTxSubmissionQueue` of `awdl0` receives a packet from the pool whose
   frame is **pure Ethernet II** (DA, SA, ethertype, payload) placed at `buffer + dataOffset`, with
   `dataOffset` = pool headroom (64). No piece of `IO80211Family`, `IOSkywalkFamily` or the
   `IO80211AWDLPeerManager` adds a header before the action: the action is the first family consumer
   of the packet.
2. **The AWDL encapsulation is inserted by the host**, in the role 6 path of
   `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets` — not the firmware. The frame that goes out
   through the flow ring toward the firmware already carries the SNAP; the firmware only adds afterward the
   802.11 MAC header (`?` firmware interior). The sequence counter is a u16 per flow queue (per
   destination and AC), on the host.
3. **RX**: the `RxCompletionQueue` of `awdl0` receives **Ethernet II after the disassembly**: the PCIe host
   converts the 802.11 frame to raw when the message brings it that way (flag 0x2: header trim
   24/26 + FCS), detects the Apple SNAP at +14, strips **16 bytes** (SNAP + `03 04` +
   sequence + `00 00` + ethertype) and recomposes the ethertype at +12. Every frame not carrying the Apple SNAP
   is diverted to the LLW interface (id 9): with OUI 0 it strips 8 bytes (RFC 1042) and with a direct
   ethertype it requires ≥ 0x600.
4. **Multicast**: on Broadcom a multicast of `awdl0` does not go straight to the radio. The queue duplicates it
   (`duplicatePacket`), marks the peer with flag 0x80 and saves it in the `IO80211AWDLPeerManager`
   (two arrays of 16: original and duplicate). The manager's multicast timer releases them in the
   opportune AW window by calling the interface's `p2pMulticastTx`, which feeds back into
   `dequeuePackets` (flag 0x80 turns them into "transmittable": the flag is cleared, the
   SNAP inserted and they enter the flow queue of the DA).
5. **The RTL8814 driver** must: on TX build `802.11 + AA AA 03 00 17 F2 08 00 | 03 04 | seq | 00
   00 | ethertype` from the packet's Ethernet II, with a counter per destination; on RX do the
   inverse conversion (802.11→Ethernet II, strip 16 bytes, recompose ethertype) and deliver
   Ethernet II into the reception queue. The 802.3 length field Broadcom uses internally does
   not exist on the air and must not be reproduced.

## 2. The frame on the air and the internal Broadcom format

On the air (owly AWDL-13 and the captures of `AWDL_REFERENCIAS_EXTERNAS.md` §5):

```
802.11 data (optional QoS) | AA AA 03 | 00 17 F2 | 08 00 | 03 04 | seq (u16) | 00 00 | ethertype (BE) | IPv6
```

The internal Broadcom host↔firmware format is the same body preceded by a 14-byte
pseudo-Ethernet header in which the type field is an **802.3 length**:

| offset | content |
|---:|---|
| +0..+6 | DA |
| +6..+12 | SA |
| +12..+14 | 802.3 length = `bswap16(total length − 14)` (only in the internal format; it does not exist on the air) |
| +14..+22 | SNAP `AA AA 03 00 17 F2 08 00` (LLC + Apple OUI 00:17:F2 + protocol 0x0800) |
| +22..+24 | `03 04` |
| +24..+26 | sequence: bits 0–11 counter per destination, bits 12–15 AC of the Tx submission queue |
| +26..+28 | `00 00` |
| +28..+30 | real ethertype (BE, e.g. 86 DD) |
| +30.. | payload |

The 802.11 header does not exist in this format: the firmware adds it when transmitting and it arrives in the
RX frame when the message indicates so (§5). On the host the `seq` field is written as u16 little-endian and the
firmware copies the body as is (`?` literal copy unverified); the external captures do
not spell out the byte order.

The two Broadcom constants:

- `0x0008F2170003AAAA` — the 8 bytes of the SNAP `AA AA 03 00 17 F2 08 00` packed as a
  64-bit little-endian immediate (Apple OUI `00:17:F2`, SNAP protocol 0x0800). Written on TX at
  `0xffffff80015432fd` (`mov rax, 8F2170003AAAAh`) → `0xffffff8001543307`. The pattern `… 17 8F 08`
  cited in `AWDL_PROXIMITY_DATAPATH.md` §9 was a typo: the byte is F2.
- `0x08F21700` — the 4 bytes compared on RX (`00 17 F2 08`: Apple OUI + first byte of protocol
  0x0800), at `0xffffff800154a238` (`cmp eax, 8F21700h`). The value `0x08F20800` cited in
  `AWDL_PROXIMITY_DATAPATH.md` §10 was a transcription typo of the same datum.

## 3. TX: the packet the action receives (question 1)

The action is `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(OSObject*, IO80211NetworkPacket**,
unsigned, void*)` (`0xffffff80015420f0`, slot +0x310 of the queue). The kernel feeds it through the normal
path (`packetSubmission` → `kern_netif_queue_tx_dequeue` → mbuf→packet conversion, traced in
`AWDL_STAGE2B_MBUF_TO_PACKET.md` §2) and Broadcom additionally calls it directly from `p2pMulticastTx`
(§6). Per packet:

1. `safeMetaCast` to `AppleBCMWLANPCIeSkywalkPacket` and check of interface enabled/chip ready
   (errors `0xE00002D8`/`0xE0000001`; role branch at `0xffffff80015423d3`: `bt eax(0x640), rol` for
   roles 6/7/9/10, `cmp r15d, 0Ah` at `0xffffff800154244e`, `cmp r15d, 9` at `0xffffff8001542458`,
   `cmp r15d, 6` at `0xffffff8001542468`).
2. Frame read: `getHeadroom` (`0xffffff8001542486`, direct call to
   `IOSkywalkNetworkPacket::getHeadroom`) and base address of the buffer through slot +0x1F0 of the packet
   (`0xffffff8001542495`; in the vtable of `AppleBCMWLANPCIeSkywalkPacket` that cell is
   `getVirtualAddress`, `0xffffff8001548498`). The frame ends up at `base + headroom`
   (`0xffffff800154249f`: `lea r12, [rax+rcx]`), and the code reads DA at +0 (`0xffffff80015424c7`) and
   ethertype at +12 (`0xffffff80015432a7`): **the frame the action receives is Ethernet II**.
3. The headroom is the pool's: 64, the constant value the interface put in the registration descriptor
   (+0x50) and that `registerSkywalkInterface` copied to the pool tracker
   (`AWDL_PROXIMITY_DATAPATH.md` §3–§4). The kernel publishes that same value as `txDataOffset`:
   `IOSkywalkNetworkInterface::getTxDataOffset` (`0xffffff8002b0baba`) returns the u16 at
   `RegistrationInfo+0x3C` (`movzx eax, word ptr [rax+3Ch]` at `0xffffff8002b0bad2`). The exact
   offset the kernel uses when copying the mbuf remains untraced to instruction (`?`,
   `AWDL_STAGE2B_MBUF_TO_PACKET.md` §9.3); the observable contract is "frame at `base+dataOffset`".
4. **No headers added**: no piece of the family touches the packet before the action (the
   action is the first family code consuming it; the stack's mbuf already carried the Ethernet
   header since dlil, `AWDL_STAGE2B_MBUF_TO_PACKET.md` §2). The `logSkywalkTxReqPacket` of slot
   +0x0B60 and the PeerManager act after the SNAP insertion.

Packet state at the action's entry: `dataOffset` = 64, `length` = length of the
Ethernet II frame, link header offset = whatever the kernel set (`?`; the Broadcom code overwrites it in
the insertion, §4). With the headroom of 64 there is room to prepend 16 bytes without a second copy: it is
exactly what the insertion does by lowering the headroom to 48.

## 4. TX: SNAP insertion and sequence counter (question 2)

**Who**: the host, in the role 6 path of `dequeuePackets`. The firmware receives the frame already
encapsulated (the `AppleBCMWLANPCIeFlowQueue::enqueuePacket` happens after the SNAP write,
`0xffffff80015433ca`) and only adds the 802.11 header (`?` firmware interior). There is no second
insertion anywhere else on the host path.

`IO80211Family` contains `IO80211AWDLPeerManager::addAwdlSnapHeader(mbuf*, u16)`
(`0xffffff8002216cd0`), which builds the same internal header (destination and source MAC, 802.3 length,
the constant `0x0008F2170003AAAA` and the rest from manager ivars). In 25G241 it has
no reference at all, neither code nor data: it is dead code. The family encapsulates no
packet of `awdl0`; whoever transmits does.

Role 6 path (after the validations of §3):

- **Unicast** (bit 0 of DA at 0, tested at `0xffffff80015424aa`): the flow
  queue key `{ifId, fAc, DA}` is built (`0xffffff80015424c7`–`0xffffff80015424e1`; `fAc` = u32 at ivars+0xB8 of
  the queue, the AC of this Tx submission queue) and it jumps to `0xffffff8001543228`.
- **Multicast** (bit 0 of DA at 1): not transmitted here; it is duplicated and saved in the
  PeerManager's stash (§6). Exception: if the packet already carries flag 0x80 (re-fed by
  `p2pMulticastTx`, §6) it is cleared (`and al, 7Fh` at `0xffffff800154321f`, write at
  `0xffffff8001543225`) and it proceeds as unicast.

Insertion sequence for unicast (all heads verified in the disassembly):

| VA | instruction | effect |
|---|---|---|
| `0xffffff8001543267` | `call [vtable+140h]` | `getDataLength` → L (length of the Ethernet II frame) |
| `0xffffff8001543271` | `add r15b, 0F0h` | new headroom = headroom − 16 |
| `0xffffff800154327c` | `call [vtable+1F0h]` | `getVirtualAddress` (buffer base) |
| `0xffffff8001543294`–`0xffffff80015432ad` | reads of the old frame | saves DA+SA[0:2] (+0), SA[2:6] (+8) and ethertype (+12) |
| `0xffffff80015432d0` | `call setHeadroom` | `IOSkywalkNetworkPacket::setHeadroom(headroom−16)`: the frame now starts 16 bytes earlier |
| `0xffffff80015432d5`–`0xffffff80015432e7` | writes | copies DA+SA (12 bytes) to the new start |
| `0xffffff80015432ec`–`0xffffff80015432f7` | `add eax,2; rol ax,8` | writes at +12 the 802.3 length `bswap16(L+2)` |
| `0xffffff80015432fd`/`0xffffff8001543307` | `mov rax, 8F2170003AAAAh` | 8-byte SNAP at +14 |
| `0xffffff800154330c` | `mov word ptr […+16h], 403h` | `03 04` at +22 |
| `0xffffff8001543314` | `mov […+1Ah], 0` | `00 00` at +26 |
| `0xffffff800154331a`–`0xffffff8001543321` | `mov […+1Ch], ax` | original ethertype at +28 |
| `0xffffff800154332f`–`0xffffff8001543339` | counter | reads and increments the u16 at +0x8A of the flow queue's internal struct |
| `0xffffff800154334d`–`0xffffff8001543358` | `and ecx,0FFFh; shl eax,0Ch; or` | sequence at +24 = `(counter & 0xFFF) \| (fAc << 12)` (uses the pre-increment value) |
| `0xffffff8001543368` | `call [vtable+148h]` | `setDataOffset(headroom−16)` |
| `0xffffff8001543378` | `call [vtable+138h]` | `setDataLength(L+16)` |
| `0xffffff8001543384` | `call setLinkHeaderOffset` | link header offset = headroom−16 |
| `0xffffff80015433b7` | `call [iface vtable+0B60h]` | `logSkywalkTxReqPacket(pkt, scratch, frame, fAc, isUnicast)` |
| `0xffffff80015433ca` | `call enqueuePacket` | `AppleBCMWLANPCIeFlowQueue::enqueuePacket(fq, pkt, true)` |

No flow queue available → `0xE00002C0` (`0xffffff80015433d4`). After the insertion: `dataOffset` =
48, `length` = L+16, link header offset = 48.

**Sequence counter**: it lives in the internal struct of each `AppleBCMWLANPCIeFlowQueue` (the 32-byte
object keeps in its field +0x18 a block created in the factory
`withProviderAndQueueInfo`, `0xffffff8001553f30`, which initializes the counter to 0 at +0x8A and the
flow ring id at +0x88 to 0xFFFF). Since the flow queues are indexed by `{interface, AC, DA}`
(`findOrCreateFlowQueue`, `0xffffff8001542006`; up to 16 per queue, occupancy counter at
ivars+0xA8), the counter is **per node and per AC**, matching the external observation of
per-node own sequences (`AWDL_REFERENCIAS_EXTERNAS.md` §5). The 4 high bits of the `seq` field
carry the AC (the id of the Tx submission queue); what an Apple receiver does with that nibble is `?`.

The notification of slot +0x0B60 is `IO80211VirtualInterface::logSkywalkTxReqPacket`
(`0xffffff800233218a`): it looks up the peer by DA, updates its timestamp, optionally taps to BPF
and, for role 6, calls `IO80211AWDLPeerManager::preQueuePacketCheckQuietMode`. It does not condition the
admission. (The doc `AWDL_PROXIMITY_DATAPATH.md` §9 cited this notice at slot +0x918; the disassembly
shows +0x0B60, and +0x918 is `processInterfaceCommand`.)

## 5. RX: the format the RxCompletionQueue expects (question 3)

`AppleBCMWLANPCIeSkywalk::rxCompRingDrain` (`0xffffff8001549dea`) processes each message of the RX
completion ring. Message fields: type 18 at +0, ifId at +1, u16 length at +0x0E, u16 data offset at
+0x10, u16 flags at +0x12 (`movzx r12d, word ptr [rax+12h]` at `0xffffff800154a1b6`). The frame
pointer is formed as `getVirtualAddress() + getHeadroom() + dataOffset` of the message
(`0xffffff800154a1a3`, `0xffffff800154a1bf`, `0xffffff800154a1fa`): the pool headroom (64) plus the
offset the firmware announces; what the firmware places in the intermediate bytes is `?`.

Role 6 path (`0xffffff800154a19c` onward):

1. **Minimum length**: `cmp word ptr [rbp+var_58], 1Ch` at `0xffffff800154a1a8`; < 28 → discard
   "Too small (len %d)" (`0xffffff800154a4c5`). 28 = 14 (pseudo-Ethernet) + 8 (SNAP) + 2 (`03 04`) +
   2 (seq) + 2 (`00 00`); the real ethertype stays at +28..+30.
2. **802.11→Ethernet conversion** if the message carries flag 0x2 (`test r12b, 2` at
   `0xffffff800154a1df` → block at `0xffffff800154a51d`): it requires 802.11 protocol version 0
   (`test byte ptr [rax+1], 3` at `0xffffff800154a520`), data frame (`and dl, 0Ch; cmp dl, 8` at
   `0xffffff800154a52e`–`0xffffff800154a534`), length ≥ 24+2·QoS+4 (`lea edx, ds:1Ch[rcx*2]` at
   `0xffffff800154a540`); with QoS = bit 7 of frame control (`0xffffff800154a53a`) the header measures
   24 or 26 (`lea ecx, ds:18h[rcx*2]` at `0xffffff800154a550`); it builds the Ethernet header inside
   the 802.11 header itself — DA = addr1 (`0xffffff800154a569`/`0xffffff800154a570`), SA = addr2
   (`0xffffff800154a559`/`0xffffff800154a562`) —, advances the offset `hdrLen−14`
   (`add ecx, 0FFFFFFF2h` at `0xffffff800154a579`) and trims `(hdrLen−14)+4` from the length
   (`add eax, 0FFFFFFFCh` at `0xffffff800154a58a`): the 4 bytes are the **FCS**, not a VLAN tag.
   (The doc `AWDL_PROXIMITY_DATAPATH.md` §10 described this branch as "optional VLAN (strip 4)"; the
   disassembly shows 802.11→Ethernet conversion with FCS trim.)
3. **SNAP**: with the frame already in Ethernet format, it checks `AA AA 03` at +14..+17
   (`0xffffff800154a20f`–`0xffffff800154a22d`) and reads the dword at +17 (`0xffffff800154a233`):
   - **`0x08F21700`** (Apple OUI + `08`): AWDL frame. It copies DA+SA[0:2] (+0..+8) to +16 and SA[2:6]
     (+8..+12) to +24 (`0xffffff800154a248`–`0xffffff800154a251`), advances the headroom +16
     (`0xffffff800154a25c`) and trims 16 from the length (`0xffffff800154a263`), `setHeadroom`
     (`0xffffff800154a271`). Result: Ethernet II frame `[DA][SA][ethertype][payload]`, with the
     ethertype of +28 of the pre-strip frame relocated at +12, length −16 and `dataOffset` +16.
   - **OUI other than 00**: discard "BAD OUI … Ethertype = 0x%04x"
     (`0xffffff800154a713`–`0xffffff800154a78f`).
   - **OUI 00** (RFC 1042): it locates the LLW interface with `getSkywalkInterface(core, 9)` — slot 632
     (+0x13C0) of `AppleBCMWLANCore`, `AppleBCMWLANCore::getSkywalkInterface(uint)`
     (`0xffffff80016b0c66`) — (`0xffffff800154aa4b`); if it does not exist, "LLW interface is null".
     It moves the header 8 bytes down (`0xffffff800154aa59`–`0xffffff800154aa67`: +0→+8, +4→+12),
     advances headroom +8 and trims 8 (`0xffffff800154aa72`–`0xffffff800154aa86`): the SNAP
     protocol (e.g. 0x0800) becomes the ethertype at +12. The packet is diverted to the LLW interface
     (r13 = interface 9, `0xffffff800154aa56`).
4. **No SNAP**: it requires ethertype ≥ 0x600 (`cmp word ptr [r12+0Ch], 600h` at `0xffffff800154a5b3`)
   and also diverts to the LLW interface (`0xffffff800154a5c1`–`0xffffff800154a5e1`; without LLW,
   "LLW interface does not exist", `0xffffff800154a680`).

That is: **only frames with the Apple SNAP reach the queue of `awdl0`**; everything else arriving
through the AWDL data path (OUI 0 or direct ethertype) goes to `llw0`.

Final queue (common, `0xffffff800154a5f1` onward; repeated at `0xffffff800154a3a8`–
`0xffffff800154a3d2` before staging): `setDataOffset(offset)` (slot +0x148 of the packet,
`0xffffff800154a603`), `setDataLength(length)` (slot +0x138, `0xffffff800154a613`),
`setLinkHeaderOffset(offset)` (direct call, `0xffffff800154a61f`); interface pre-hook
**`IO80211VirtualInterface::inputPacket`** (slot +0x0B50, called at `0xffffff800154a65c`) with
`(pkt, RX label, frame pointer, &flag, 0)`: for role 6 it looks up the peer by SA and discards frames
without a peer (counter `incrementNumInputPacketsDropped`), with special cases for
`33:33:00:00:00:FB` and `01:00:5E::/16`; return 0 → discard, flag bit 0 → consumed by the hook,
otherwise → admission. (The doc `AWDL_PROXIMITY_DATAPATH.md` §10 cited the hook at slot +0x8F8; the
disassembly shows +0x0B50, and +0x8F8 is `getBSDInterface`.) Afterward: `setRXTimestamp`
(`0xffffff800154a3fb`), and delivery with
**`AppleBCMWLANPCIeSkywalkRxCompletionQueue::stagePacket(pkt, false, false)`** (slot +0x340 of the
queue, called at `0xffffff800154a44c`; enabled per `IOEventSource::isEnabled`, slot +0x160,
`0xffffff800154a406`), not with `enqueuePackets`. When exhausting the batch: `requestEnqueue(0, 0)` (slot
+0x298) on the RX queue of each one of the 12 interfaces (`0xffffff800154abd1`, after
`getSkywalkInterface(core, i)` at `0xffffff800154aba5` and `getRxCompQueue` +0xD70 at
`0xffffff800154abb9`), `flushPackets` (+0x330) on failure (`0xffffff800154abef`) and
`scheduleRxCompleteLegacy` for refilling (`0xffffff800154acfc`).

Format at the queue's entry: **Ethernet II** at `buffer + dataOffset`, `linkHeaderOffset` =
`dataOffset`, length = full frame. The conversion to mbuf for the BSD ifnet is done by the family from
that point on. The RTL driver can use the same pair the STA layer already uses
(`enqueuePackets` +0x2A0/+0x2A8) or the Broadcom pattern (`stagePacket` + `requestEnqueue`); both are
entry points of the same queue (`?` internal equivalence not traced).

## 6. Multicast: duplication, stash and windows (question 4)

Multicast branch of role 6 in `dequeuePackets` (bit 0 of DA at 1, `0xffffff8001543020`):

1. If the packet already carries flag 0x80 at `pkt+0x78 → +0x28` (`mov al, [rdx+28h]` at
   `0xffffff8001543020`, `js` toward `0xffffff800154320b`): the flag is cleared and it is transmitted
   (SNAP insertion of §4 and flow queue of the DA). This is the re-feed path.
2. If not: `safeMetaCast` of the interface to `IO80211VirtualInterface` (`0xffffff8001543040`; without
   success → `0xE00002C0`, "Invalid Virtual Interface for AWDL !"), flag 0x80 is set on the original
   (`or byte ptr [r12+28h], 80h` at `0xffffff8001543066`), it is duplicated with
   `AppleBCMWLANPCIeSkywalkPacketPool::duplicatePacket` (`0xffffff800154307e`; pool at ivars+0x18 of
   the queue) and the duplicate is marked too (`0xffffff8001543090`–`0xffffff8001543094`); if there is no
   duplicate it is logged and it proceeds the same ("Could not dup multicast packet for 2nd soc channel").
   Then `IO80211VirtualInterface::stashMulticastPacketPair(if, original, dup)` (direct call
   at `0xffffff80015435be`) and the packet is taken as consumed (neither queue nor free).

**Where the stash lives**: `IO80211VirtualInterface::stashMulticastPacketPair` (`0xffffff8002334e20`)
only forwards to the peer manager of the interface (vtable +0x318) →
`IO80211AWDLPeerManager::stashMulticastPacketPair` (`0xffffff8002213b16`): two arrays of 16 entries
in the manager's ivars — originals at +65816 (u32 counter at +65944) and duplicates at +65952
(counter at +66080); with the arrays full the leftover packet is freed by the interface's
`freeMulticastPacket` (slot +0xF08). The base variant
`IO80211PeerManager::stashMulticastPacketPair` (`0xffffff800225f940`) is a "Not Implemented".

**Who transmits them**: `IO80211AWDLPeerManager::handleMulticastTimer` (`0xffffff80021c38c4`),
manager timer: if the remaining time of the current AW (`awRemainingTime`) reaches the threshold
(at ivars+748), it takes out an array — the duplicates when ivars+16128 equals 8, the originals otherwise — and calls
`p2pMulticastTx` of the **interface** (slot +0xF18 = `p2pMulticastTx(pkt**, uint)`;
only call sites found: `0xffffff80021c39a0` and `0xffffff80021c3a5c`), freeing the
leftovers with `freeMulticastPacket` (slot +0xF08). A variant with counter called by flag 0x40
of an object hanging from the manager (slot +0xF20, `0xffffff80021c3abc`/`0xffffff80021c3adc`) falls into the
base `IO80211VirtualInterface::p2pMulticastTx(uint, bool)` (`0xffffff8002334d98`), which logs
"Function not implemented": on Broadcom the operative path is the array one. The call peaks came out of a call sweep with offsets +0xF18/+0xF20/+0xF28 in the ranges
0xffffff8001500000–0xffffff8001800000 and 0xffffff8002100000–0xffffff8002400000.

**The return to the datapath**: `AppleBCMWLANProximityInterface::p2pMulticastTx` (`0xffffff800172a6d6`)
forwards to slot +0x368 of the first Tx submission queue →
`AppleBCMWLANPCIeSkywalkTxSubmissionQueue::p2pMulticastTx` (`0xffffff8001541d64`) →
`dequeuePackets(this, this, packets, n, 0)`: the stashed packets, with flag 0x80, are treated
as unicast (flag clearing, SNAP, flow queue per DA) and go out through the flow ring in the current window. The version of the Broadcom base class (`0xffffff80015d774a`) is "NOT IMPLEMENTED": only the
PCIe subclass implements it.

**Alternative path of the manager** (mbuf model): the same timer can instead mark
a TX request (`reqTx = 31` at ivars+2504 and `IO80211PeerManager::requestTxPacket`,
`0xffffff800224dc34`, which notifies the object at ivars+0x520 through its virtual slot 398 — name `?`, and the
drag to the driver has not been traced `?`); the driver then pulls with
`dequeueTxPacket(TxPacketRequest*)` (`0xffffff800221675e` on AWDL; validates state 5, data mode and
datapath lock, and fills the request: 8 mbuf chains at +24..+88 with counters at +8..+24)
→ `IO80211PeerManager::dequeueTxPacket` (`0xffffff8002250554`) → `getMulticastPackets` through the same
vtable (+0x168, `0xffffff800225076b`) and `getUnicastPackets` (+0x170, `0xffffff80022507bb`). The
multicasts come out of an mbuf queue of the manager (object at ivars+0x530, dequeue through slot +0x1C8 at
`0xffffff800224fe4e`), gated by bit 0x10 of ivars+2504. Who feeds that mbuf queue is `?`.

**What the driver family needs for multicast**: the stash and the timer belong to the
family and work without driver cooperation; what a third-party interface reusing the
`IO80211AWDLPeerManager` must provide are the interface overrides Broadcom implements and the
family leaves empty: `p2pMulticastTx(pkt**, uint)` (slot +0xF18; the base
`0xffffff8002334d6a` does nothing), `freeMulticastPacket` (slot +0xF08; it must return the packets to the
pool) and, for the manual trigger, `triggerAWDLMulticastTx` (slot +0xF10; on Broadcom it kicks the
multicast flow queue `findExistingFlowQueue(ifId | 0xFFFF0000)`, `0xffffff8001541cfe`). The
0x80 marks on `pkt+0x78 → +0x28` are internal convention of the PCIe packet; a custom driver can use
its own marking as long as its `p2pMulticastTx` distinguishes the re-fed packets.

## 7. What the RTL8814 driver must build and strip

**TX (action of the Tx submission queue of `awdl0`)**. Input: packet of its own pool with frame
**Ethernet II** at `base + dataOffset` (64) and `length` = frame. The driver must:

1. Read DA (+0), SA (+6), BE ethertype (+12) and payload (+14).
2. Build the USB frame: 802.11 data MAC header (addr1 = DA, addr2 = SA, addr3 = AWDL BSSID
   `00:25:00:FF:94:73` per the external captures; exact QoS/flags `?`) followed by the body
   `AA AA 03 00 17 F2 08 00 | 03 04 | seq (u16) | 00 00 | ethertype (BE) | payload`.
3. `seq = (counter & 0xFFF) | (AC << 12)`, with a u16 counter **per destination** (Broadcom: per flow
   queue `{interface, AC, DA}`, initialized to 0 and used before being incremented, so that the
   first frame of a destination carries 0). The high nibble with the AC is the Broadcom design; replicating it
   is the conservative option (`?` semantics on the Apple receiver).
4. Do not reproduce the 802.3 length field at +12: it is an artifact of the internal Broadcom
   host↔firmware format and disappears when the Ethernet header is replaced by the 802.11 one.
5. Multicast: either replicate the Broadcom stash model (duplicate, save per channel and transmit in
   the windows), or — without firmware — transmit the multicast in the guard strips the
   channel sequence indicates; while there are no peers, the stash can be deferred (stage 3 of
   `AWDL_ETAPAS_2_A_4.md`).

**RX (delivery to the RxCompletionQueue)**. Input: 802.11 frame with FCS from USB. The driver must
do in software what Broadcom splits between firmware and host:

1. Validate data frame, protocol 0; header length 24/26 per the QoS bit; DA = addr1,
   SA = addr2 (this is what the Broadcom host conversion does, §5.2).
2. If the body (after the 802.11 header) starts with `AA AA 03`: read 4 bytes at +17 of the body;
   if they equal `00 17 F2 08` (constant 0x08F21700) it is an AWDL frame → strip the 16 bytes of the body
   (SNAP + `03 04` + seq + `00 00` + ethertype), take the ethertype that was at +28 of the body and
   leave `[DA][SA][ethertype][payload]`. If the OUI is other than 00 → discard ("BAD OUI"). If the
   OUI is 00 → strip 8 bytes (RFC 1042) promoting the SNAP protocol to ethertype and direct the
   packet to `llw0` (id 9) when it exists.
3. No SNAP: only with ethertype ≥ 0x600, and also toward `llw0`. There is no VLAN handling on the
   Broadcom path; if ever needed, it would be a driver addition (`?`).
4. Deliver a packet of the pool with that Ethernet II at `base + dataOffset`,
   `setDataOffsetAndLength(offset, length)`, `setLinkHeaderOffset(offset)` and enqueue into the
   RxCompletionQueue (`enqueuePackets` in batches, as the STA layer already does, or `stagePacket` +
   `requestEnqueue(0,0)` per batch like Broadcom). Minimum length of a well-formed frame before the
   trim: 30 (14+16) plus payload; Broadcom only requires 28.
5. Before enqueueing, the driver calls `IO80211VirtualInterface::inputPacket` (slot +0x0B50), like
   Broadcom at `0xffffff800154a65c`. There the family applies the peer filter (unknown SA →
   discard) and retains the packets of a peer until `wifip2pd` sends ioctl 415. The
   retained ones come back through slots +0xF30/+0xF38 of the interface, which the
   driver has to implement ([AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §3 and §4). The family does not filter
   anything the driver enqueues without that call. The sequence counter is discarded on RX: it does
   not require state.

## 8. Address verification

Heads checked against the function table of the database (function start == cited
address): `0xffffff80015420f0` (`dequeuePackets`), `0xffffff8001542006` (`findOrCreateFlowQueue`),
`0xffffff8001541c14` (queue `freeMulticastPacket`), `0xffffff8001541cfe`
(queue `triggerAWDLMulticastTx`), `0xffffff8001541d64` (queue `p2pMulticastTx`),
`0xffffff80015479cc` (`duplicatePacket`), `0xffffff8001549dea` (`rxCompRingDrain`; range
0xffffff8001549dea–0xffffff800154ad67), `0xffffff800225f940` / `0xffffff8002213b16`
(`stashMulticastPacketPair` base/AWDL), `0xffffff8002216a2e` / `0xffffff8002216a4e`
(`getMulticastPackets`/`getUnicastPackets` AWDL), `0xffffff800221675e` (`dequeueTxPacket` AWDL),
`0xffffff8002250554` (`dequeueTxPacket` base), `0xffffff800224fe4e` (`getMulticastPackets` base),
`0xffffff800224dc34` (`requestTxPacket`), `0xffffff8002333cac` (`inputPacket`),
`0xffffff800233218a` (`logSkywalkTxReqPacket`), `0xffffff8002334d6a`/`0xffffff8002334d98`
(`p2pMulticastTx` base of `IO80211VirtualInterface`, two signatures), `0xffffff8002334e20`
(`stashMulticastPacketPair`), `0xffffff8002334e44` (`signalMulticastPackets`), `0xffffff8002334cfa`
(`handleMulticastTransmit`), `0xffffff8002334c8a` (`modifyUnicastPacket`), `0xffffff800172a6d6`
(`p2pMulticastTx` of Proxi), `0xffffff800172a3dc` (`triggerAWDLMulticastTx` of Proxi),
`0xffffff800172a6f6` (`freeMulticastPacket` of Proxi), `0xffffff8001553f30`
(`AppleBCMWLANPCIeFlowQueue::withProviderAndQueueInfo`), `0xffffff8002b0baba` (`getTxDataOffset`).
The full range of `dequeuePackets` (0xffffff80015420f0–0xffffff8001543b4d) and of `rxCompRingDrain`
is dumped in `Build/ida_awdl/task_dataencap/dis_txsub_dequeue.txt` and `dis_rxdrain.txt`.

Vtables read cell by cell: `__ZTV15IOSkywalkPacket` (0xffffff8002b3b510: +0x138
`setDataLength`, +0x140 `getDataLength`, +0x148 `setDataOffset`, +0x150 `getDataOffset`, +0x158
`setDataOffsetAndLength`, +0x178 `getDataVirtualAddress`), vtable of
`AppleBCMWLANPCIeSkywalkPacket` (0xffffff80015ae548: +0x1F0 `getVirtualAddress`
0xffffff8001548498, +0x1F8 `getBufferSize`, +0x200 `setPktEnqueueTime`), vtable of
`AppleBCMWLANCore` (0xffffff80017e97e8: slot 632, +0x13C0 `getSkywalkInterface(uint)`
0xffffff80016b0c66), vtable of `AppleBCMWLANPCIeSkywalkRxCompletionQueue` (slot 104, +0x340
`stagePacket(pkt,bool,bool)` 0xffffff8001546cc8; slot 44, +0x160 `IOEventSource::isEnabled`) and of
`AppleBCMWLANProximityInterface` (slots +0x0B50 `inputPacket`, +0x0B60 `logSkywalkTxReqPacket`,
+0x0D70 `getRxCompQueue`, +0x0F08 `freeMulticastPacket`, +0x0F10 `triggerAWDLMulticastTx`, +0x0F18
`p2pMulticastTx(pkt**,uint)`).

Derived data verified instruction by instruction (the ones supporting the tables of §4 and §5):
SNAP write `0xffffff80015432fd`/`0xffffff8001543307`, `03 04` `0xffffff800154330c`, zeros
`0xffffff8001543314`, ethertype `0xffffff8001543321`, 802.3 length `0xffffff80015432f7`,
counter `0xffffff800154332f`–`0xffffff8001543339`, sequence `0xffffff800154334d`–
`0xffffff8001543358`, stash `0xffffff8001543066`/`0xffffff800154307e`/`0xffffff8001543090`/
`0xffffff80015435be`, re-fed flag 0x80 `0xffffff8001543020`/`0xffffff800154321f`/
`0xffffff8001543225`, RX minimum length `0xffffff800154a1a8`, flag 0x2 `0xffffff800154a1df`,
802.11 conversion `0xffffff800154a51d`–`0xffffff800154a58a`, SNAP check `0xffffff800154a20f`–
`0xffffff800154a23d` (constant `0xffffff800154a238`), strip 16 `0xffffff800154a248`–
`0xffffff800154a271`, BAD OUI `0xffffff800154a713`–`0xffffff800154a78f`, OUI 0 `0xffffff800154aa40`–
`0xffffff800154aa86`, ethertype ≥ 0x600 `0xffffff800154a5b3`, LLW diversion `0xffffff800154a5c1`–
`0xffffff800154a5e1`, final queue `0xffffff800154a5f1`–`0xffffff800154a65c` and `0xffffff800154a3a8`–
`0xffffff800154a406`, `stagePacket` `0xffffff800154a44c`, batch close `0xffffff800154ab9c`–
`0xffffff800154abef` and `0xffffff800154acfc`. Calls with vtable offsets +0xF18/+0xF20
from `handleMulticastTimer`: `0xffffff80021c39a0`, `0xffffff80021c3a5c`, `0xffffff80021c3abc`,
`0xffffff80021c3adc`.

## 9. Open points (`?`)

1. Interior of the Broadcom firmware: construction of the 802.11 header on TX (QoS, rates) and what
   it places in the bytes between the pool headroom and the frame on RX (data offset field of the message).
2. Byte order of the `seq` field on the air and semantics of the high nibble (AC on Broadcom) for an
   Apple receiver; the external captures record an increasing u16 per node.
3. Object at ivars+0x520 of the manager receiving the notice of `requestTxPacket` (virtual slot 398) and
   who calls `dequeueTxPacket` on Broadcom; flag 0x40 (byte +2885 of that object) deciding
   between the counter variant and the array variant of `handleMulticastTimer`.
4. Who feeds the manager's multicast mbuf queue (ivars+0x530) that `getMulticastPackets` drains.
5. Full behavior of `inputPacket` for the special DAs `33:33:00:00:00:FB` and
   `01:00:5E::/16` (branch traced only at a coarse level).
6. Internal equivalence between delivering through `stagePacket`+`requestEnqueue` (Broadcom) and through
   direct `enqueuePackets` (RTL STA layer): both are entry points of the same queue, but the
   common machinery has not been dumped.
7. Exact flags of the 802.11 header an Apple peer requires on TX (QoS, A-MSDU) — refers to the
   captures of `AWDL_REFERENCIAS_EXTERNAS.md` §5.
