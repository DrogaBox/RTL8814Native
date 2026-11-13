# Portability of the `awdl0` data path to macOS 14.8.9 and 15.8.2 (23J631 / 24H232)

Portability check of the data code added to `Source/AirLayer/AWDLInterface.cpp`
by commits `5b686a8` (data RX to `awdl0`), `28b58e8` (AW clock following a master),
`5630d49` (data TX), `f2969a4` (window bracketing and `preQueuePacketCheckQuietMode`) and
`9dc71a6` (rejection codes of the parking). Reference: 26.7.1 (25G241, Darwin 25.6.0).
Targets: 23J631 (14.8.9, Darwin 23.6.0) and 24H232 (15.8.2, Darwin 24.6.0), both x86_64.

Method: symtabs of the three kernelcaches (dumps `Docs/re/io80211_*/syms*.txt`) for
symbol existence, and reading in IDA headless (`idat -A -S`, copies of
`boot_23J631.kc.i64`, `boot_24H232.kc.i64` and `boot_awdl.kc.i64` in `Build/ida_port_data/`,
dumps `out23/`, `out24/`, `out26/`) for the behavior contracts. The cited VAs are
function or instruction heads of the corresponding build; they change per version and only
accredit the finding. Unobserved items go with `?`.

Verdict: **the data path symbols exist with the same decorated name in the
three versions and the five behavior contracts of the scope replicate unchanged**.
The per-version dependencies remain those already documented in
[`AIRLAYER_PORT_14_15.md`](AIRLAYER_PORT_14_15.md) (class vtables and headers per build);
the data commits add none.

## 1. Link symbols

Definitions read from the symtabs (address and type `0f` = defined). The `__ZNK…`
names of the const accessors also appear as undefined reference (`0000000000000000 01`) in
`syms.txt` of IO80211Family: it is the family's use of them, not their definition, which
lives in `syms_com.apple.iokit.IOSkywalkFamily.txt`.

| Symbol | 23J631 | 24H232 | 25G241 |
|---|---|---|---|
| `__ZN23IO80211SkywalkInterface14getPeerManagerEv` | `0xffffff80021a0a3c` | `0xffffff8002263490` | `0xffffff80022f5fa8` |
| `__ZN22IO80211AWDLPeerManager28preQueuePacketCheckQuietModeEP12ether_header` | `0xffffff8002100c70` | `0xffffff80021b0b80` | `0xffffff800221649e` |
| `__ZNK15IOSkywalkPacket13getDataOffsetEv` | `0xffffff8002861dac` | `0xffffff80029a457a` | `0xffffff8002b2c5e2` |
| `__ZNK15IOSkywalkPacket13getDataLengthEv` | `0xffffff8002861bf2` | `0xffffff80029803e8` | `0xffffff8002b064d0` |
| `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv` | `0xffffff8002861dcc` | `0xffffff8002980430` | `0xffffff8002b06518` |
| `__ZNK15IOSkywalkPacket13getBufferSizeEv` | `0xffffff8002861b68` | `0xffffff80029803d0` | `0xffffff8002b064b8` |
| `__ZNK15IOSkywalkPacket20getPacketBufferCountEv` | `0xffffff8002861bae` | `0xffffff80029803de` | `0xffffff8002b064c6` |
| `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj` | `0xffffff8002861e3e` | `0xffffff80029804a2` | `0xffffff8002b0658a` |
| `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj` | `0xffffff8002861d00` | `0xffffff80029a44d0` | `0xffffff8002b2c538` |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh` | `0xffffff8002862b22` | `0xffffff8002981098` | `0xffffff8002b07180` |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj` | `0xffffff8002860b0a` | `0xffffff800297f82e` | `0xffffff8002b058ee` |
| `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `0xffffff8002860d8c` | `0xffffff80029a4072` | `0xffffff8002b2c0de` |
| `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` (const) | `0xffffff800286b1aa` | `0xffffff80029a56b6` | `0xffffff8002b2d766` |
| `__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` (const) | `0xffffff800286ebe6` | `0xffffff80029a5afc` | `0xffffff8002b2dbb6` |
| `__ZN26IOSkywalkTxSubmissionQueue13legacyDequeueEv` | `0xffffff80028681ee` | `0xffffff80029a47a4` | `0xffffff8002b2c860` |
| `__ZN26IOSkywalkTxSubmissionQueue12checkForWorkEv` | `0xffffff8002867c56` | `0xffffff80029a468a` | `0xffffff8002b2c746` |
| `__ZN26IOSkywalkTxSubmissionQueue8withPoolE…` (7 arguments) | `0xffffff80028679ec` | `0xffffff80029860c2` | `0xffffff8002b0ca86` |
| `__ZN26IOSkywalkRxCompletionQueue8withPoolE…` (7 arguments) | `0xffffff800286a828` | `0xffffff80029883e0` | `0xffffff8002b0ee04` |
| `__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE` | `0xffffff800285faa0` | `0xffffff800297ebb8` | `0xffffff8002b04be8` |
| `__ZN23IO80211VirtualInterface11bpfTapInputEP6__mbufjPvm` | `0xffffff80021d95c6` | `0xffffff80022a6926` | `0xffffff8002330e7a` |
| `__ZN23IO80211VirtualInterface12setLinkStateE16IO80211LinkStatej` | `0xffffff80021dbba0` | `0xffffff80022a8e9e` | `0xffffff8002333408` |
| `__ZN22IO80211AWDLPeerManager20interfaceStateChangeE18StateChangedSource` | `0xffffff80020c85a2` | `0xffffff8002176af0` | `0xffffff80021d8cc8` |
| `__ZN22IO80211AWDLPeerManager31setChannelSequenceUnconditionalEb` | `0xffffff80020bbee6` | `0xffffff8002169b06` | `0xffffff80021cb69a` |
| `__ZN22IO80211AWDLPeerManager23updateSyncFrameTemplateEb` | `0xffffff80020cb764` | `0xffffff8002179b0e` | `0xffffff80021dc5b2` |
| `__ZN22IO80211AWDLPeerManager27buildAWDLElectionParamsVer2EP23awdlElectionParamsTlvV2j` | `0xffffff80020fb5d8` | `0xffffff80021ab474` | `0xffffff8002210a3c` |

The 27 symbols that `AWDLInterface.cpp` links by name (`__asm`) are defined in the symtabs of 23J631 and 24H232. The other IO80211/Skywalk symbols that the binary imports and are missing on 14 or 15 are vtable cells inherited by compiling with the headers of 26 (`AIRLAYER_PORT_14_15.md` §4), none of the data path.

Notes:

- The two variants of `enqueuePackets` (const array `EPKP…` and mutable array `EP…`) exist
  in the three versions; the kext uses the const one (`AWDLInterface.cpp`, `rtl_rxcomp_enqueue` /
  `rtl_txcomp_enqueue`). The mutable variant is at `0xffffff800286b250` / `0xffffff800286ec7a`
  (23J631), `0xffffff80029a5760` / `0xffffff80029a5bb4` (24H232), `0xffffff8002b2d810` /
  `0xffffff8002b2dc6e` (25G241).
- `bpfTapInput` over `IO80211Buffer` (`EP13IO80211BufferjPvm`) also exists in all three; the
  kext uses the `__mbuf` one.
- `IOSkywalkTxSubmissionQueue::initWithPool` (which on 25G241 rejects a null action,
  `AWDL_TX_DATOS_SPEC.md` §2.1) was not re-read on 14/15 (`?`); it was verified that the
  `withPool` thunk of 7 arguments forwards to the 9-argument one with the main action in the same position
  (§2.2).

## 2. Contract of the submission queue action (`legacyDequeue`)

`IOSkywalkTxSubmissionQueue::legacyDequeue` is **identical in the three versions**: same
size (`0x124`), same instruction sequence and same fields.

- Entry gates: byte `+0x28` (source enabled) and byte `+0xCC`; on 23J631 they are coded as
  `cmp byte,0/jz` and on 24H232/25G241 as `cmp byte,1/jnz` (or `test`): same predicate.
- Call to the action: action at `[this+0x20]`, `owner` at `[this+0x18]`, `refCon` at
  `[this+0x58]`, packet array = base of the table `[this+0xE8]+8` shifted by the
  index `[[this+0xE8]+0x80]`, count in `ecx`. Sites: `0xffffff800286826c` (23J631),
  `0xffffff80029a4822` (24H232), `0xffffff8002b2c8de` (25G241).
- **The action return is the number of packets consumed**: `test eax,eax` (0 ⇒ marks
  "no progress" at `+0x13C` and cuts), and otherwise the table index
  `[[this+0xE8]+0x80]` advances exactly by `eax` with wraparound over the capacity at `+4`
  (`add edx, eax`, `cmp edx, esi`, `cmovb`): `0xffffff8002868295`–`8b` (23J631),
  `0xffffff80029a4845`–`8b` (24H232), `0xffffff8002b2c941`–`97` (25G241). Returning less than
  `count` keeps consuming the batch partially; returning `count` (what `txData` does)
  consumes it all.
- `checkForWork` keeps the same chain in the three versions
  (`packetSubmissionForKPipe` → `legacyDequeue`, gates `+0x28`/`+0xCC`, `requestEnqueue`
  via a vtable cell); on 23J631 it measures `0xCC` versus `0x117` bytes, due to reordering of the
  prologue.

The 7-argument `IOSkywalkTxSubmissionQueue::withPool` thunk forwards to the 9-argument one with the same
stack shape in the three versions (`call` to the 9-argument overload at `0xffffff8002867a06`
(23J631), `0xffffff80029860dc` (24H232), `0xffffff8002b0caa0` (25G241)); the action of the fifth
parameter arrives as the main action. Consequence: `createDatapath` is valid as-is on
14/15, and the stage 1 requirement "non-null action" has not been relaxed (`?` on the exact
gate of `initWithPool` on 14/15, see §1).

## 3. Completion queues (`enqueuePackets`)

### 3.1 RX, const variant (`IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket const**, uint, uint)`)

Same checks and constants in the three versions (23J631 `0xffffff800286b1aa`,
size `0xA5`; 24H232 `0xffffff80029a56b6` and 25G241 `0xffffff8002b2d766`, size `0xAA`):

- `pkts == 0` or `n == 0` ⇒ `0xE00002C2`.
- **`n < capacity` strict** against field `+0xAC` (`cmp`/`jbe` to error).
- No work loop (`[this+0x30] == 0`) ⇒ `0xE00002D8`; inside the lock: source disabled
  (byte `+0x28`) or not initialized (byte `+0xCC`) ⇒ `0xE00002D7`; byte `+0x118` with bit 0
  set ⇒ `0xE00002D5`.
- The fourth argument (`opts`) is **not read** in any version.
- Identical routing: `[this+0x78] != 0` ⇒ `enqueuePacketsForNetworking`, else
  `enqueuePacketsForKPipe`.

The batching of `deliverData` (`n ≤ 16 < 128`) and of `txData` (`64 < 128`) is still accepted.

### 3.2 TX, const variant (`IOSkywalkTxCompletionQueue::enqueuePackets(IOSkywalkPacket const**, uint, uint)`)

Identical in the three versions (23J631 `0xffffff800286ebe6`, size `0x94`; 24H232
`0xffffff80029a5afc` and 25G241 `0xffffff8002b2dbb6`, size `0xB7`): same constants
(`0xE00002C2/D8/D7/D5`), strict capacity `+0xAC`, `opts` unread, `memmove` of the batch to the
table and `completePackets(IOSkywalkPacket**, uint)`. Only divergence, internal to the family:
the "packet prepared" byte lives at `+0x108` on 23J631 and 24H232 and at `+0x110` on 25G241
(RX has it at `+0x118` in all three). Does not affect the kext.

## 4. Accessors and packet life cycle

Instruction-by-instruction comparison; in the three versions the fields of the
`IOSkywalkPacket` do not move:

| Function | Verification |
|---|---|
| `getDataOffset` | `[[this+0x30]]` with guard `+0x64` (buffer count) ⇒ `IOSkywalkPacketBuffer::getDataOff`. Identical. |
| `getDataLength` | Sum of `IOSkywalkPacketBuffer::getDataLength` over table `+0x30` with guard `+0x64`. Identical. |
| `getDataVirtualAddress` | `getMemorySegment().getVirtualAddress() + getMemorySegmentOffset()` of the first buffer. Identical. |
| `getBufferSize` | `[[this+0x40]+0x30]`. Identical. |
| `getPacketBufferCount` | `[this+0x64]`. Identical. |
| `prepare` | Byte `+0x58` bit 1 already prepared ⇒ 0; null queue ⇒ address 0; else virtual `prepareWithQueue`. Identical: the kext call `prepare(pkt, nullptr, 0, 0)` is valid in all three. |
| `setDataOffsetAndLength` | Same shape and guard `+0x64`; 23J631 differs only in the prologue order. |
| `setLinkHeaderLength` | Byte-for-byte identical: `_kern_packet_set_link_header_length([this+0x28], …)` with error coalescing to `0xE00002C2`. |

Pool: `allocatePacket` is byte-for-byte identical (queue via vtable slot `+0x130` with the
forced-wait mark at 1, `esi=1`); `deallocatePacket` has the same semantics (null
pointer guard, `completePreparedPacket` if byte `+0x58` has bit 1, `release` via cell
`+0x1E0` and `_kern_pbufpool_free`). The pattern of `deliverData` (allocate with `opts=1`,
`prepare` without queue, write the buffer, `setDataOffsetAndLength`, `setLinkHeaderLength`,
`enqueuePackets`, and on error `deallocatePacket`) is portable unchanged.

## 5. Peer manager

### 5.1 `getPeerManager`

Same semantics, different containers: 23J631 `[[this+0x110]+0x30]`
(`0xffffff80021a0a3c`), 24H232 `[[this+0x110]+0x28]` (`0xffffff8002263490`), 25G241
`[[this+0x120]+0x28]` (`0xffffff80022f5fa8`). Matches the offsets of
[`AWDL_PRESENCE_OFFSETS_14_15.md`](AWDL_PRESENCE_OFFSETS_14_15.md) (vif ivars `0x110`
on 23/24, `0x120` on 25; manager seat `+0x30` on 23, `+0x28` on 24/25). Called by
symbol: portable.

### 5.2 `preQueuePacketCheckQuietMode`

Same logic in the three versions (23J631 `0xffffff8002100c70`, 24H232
`0xffffff80021b0b80`, 25G241 `0xffffff800221649e`): AirPlay service counter at zero as
precondition, mask of the AWDLOn dword (`(v & 0xC) == 0` ⇒ out; `v & 8` ⇒ out;
otherwise ⇒ proceed), unicast of the Ethernet header ⇒ `setDataMode(this, 3, 0)`, log
"Exiting quiet mode due to queued packet from network stack" and queue to `exitQuietMode`.
Only the manager's internal ivars change: service counter `+0x7EE0` (23J631) /
`+0x7F28` (24H232) / `+0xA648` (25G241); AWDLOn `+0x3EC8` / `+0x3EE8` / `+0x3EE0`. The kext call
(`rtl_pm_preQueueQuietMode` with the raw Ethernet header of the packet) is valid
as-is.

### 5.3 Link-down gate of `interfaceStateChange`

`IO80211AWDLPeerManager::interfaceStateChange` keeps the entry gate in the three
versions, with the same shape: after `ivars+0x3EE4/0x3ECC/0x3EEC != 0` (25G241/23J631/24H232,
the fourth byte after AWDLOn), it reads an object from the controller, calls its slot `+0xB88` and
compares with 1:

| | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| object read | `[ctrl+0x560]`, `0xffffff80021d8eae` | `[ctrl+0x548]`, `0xffffff80020c8780` | `[ctrl+0x550]`, `0xffffff8002176cce` |
| slot called | `+0xB88` | `+0xB88` | `+0xB88` |
| comparison | `cmp eax, 1` | `cmp eax, 1` | `cmp eax, 1` |
| in case 1 | log "Interface link is down, nothing to do. Source = %s" and exit without touching strategy | same | same |

Slot `+0xB88` is stable and in the three versions its entry in the vtable of
`IO80211VirtualInterface` is `linkState()` (dumps `Docs/re/io80211_*/vt_IO80211VirtualInterface.txt`,
slot 369 in all three; the neighboring `setLinkState` of 4 arguments is at `+0xB80` in the
three). The read object is therefore **a virtual interface, not the peer manager** (the
manager has no `linkState`); whether it is the vif of `awdl0`, the primary one or the infrastructure one
remains unresolved (`?`, already open in `AWDL_SIN_ASOCIACION_INFRA.md` §2.1). The controller ivar
holding the object moves (`0x560` → `0x548` → `0x550`), but the kext does not
reference it: the gate is internal to the family and `enable()`/`disable()` only call their own
`setLinkState`.

Vtable warning: the virtual cell of `IO80211VirtualInterface::setLinkState` (2
arguments), which `enable()`/`disable()` dispatch through the kext vtable, shifts
(slot 462 on 25G241, 461 on 24H232, **447 on 23J631**): it is the general drift already documented
in `AIRLAYER_PORT_14_15.md` §5, not a new effect of the data commits.

## 6. Template and channel sequence layouts

### 6.1 TLV 24 (election v2) read by `alignClock`

`buildAWDLElectionParamsVer2` is instruction-by-instruction identical in the three versions
(capability gate `cap > 0x2A`, else log "skipping buildAWDLElectionParamsV…" and return 0).
It writes the same layout:

| Field | Position | Value |
|---|---|---|
| type | `+0` | `0x18` (24) |
| length | `+1..2` | `0x28` (40) |
| root master | value `+0..5` | 6 bytes |
| **sync master** | value `+6..11` | 6 bytes |
| rootLFC | value `+12..15` | u32 |
| hop count ("distance") | value `+16..19` | u32 |
| root metric | value `+20..23` | u32 |
| self metric | value `+24..27` | u32 |
| election id | value `+28..31` | u32 |
| zero | value `+32..35` | u32 |
| selfLFC | value `+36..39` | u32 |

Total returned length: `0x2B` (43). The reads of `alignClock` (`e2+3` top master,
`e2+3+6` sync master, `*(u32*)(e2+3+16)` distance saturated at 255, `e2+3+20` root
metric, `e2+3+24` self metric) and of `buildAf` hold unchanged on 14/15. The template of the
151 keeps being composed with the same builders in the same order
(`updateSyncFrameTemplate` calls `buildServiceParamTlv` → `buildAWDLElectionParamsVer2` →
`buildAwdlDataPathStateTlv` … `buildSyncTreeTlv` in the three versions), so TLV 24
reaches the kext inside the template as `findTlv(…, 24, 28)` expects it.

### 6.2 Request 129 (synchronization channel sequence)

`setChannelSequenceUnconditional` assembles the same 400 B payload in the three versions
(build sites: `0xffffff80020bbf46…` (23J631), `0xffffff8002169b74…` (24H232),
`0xffffff80021cb710…` (25G241)):

- `+0x00` u32 version = 1.
- `+0x04` u8 flags (SDB) — written after the entries loop.
- `+0x05` u8 `num_channels`: byte read from the manager; it is also the bound of the copy loop.
- `+0x06..+0x09`: four bytes of ivars (`unknown6`, sub-slots, …).
- Entries from `+0x0C`, 12 B each, loop step `0xC`: qword copied from the manager's store
  (u32 version/flags at `entry+0`, **u32 channel at `entry+4`**) and **u32 flags at
  `entry+8`** (`mov [payload+rcx+14h], …` with `rcx` the entry displacement).

The kext parsing in `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` (`n = p[5]`, channel
`*(u32*)(e+4)`, flags `*(u32*)(e+8)`, own cap of 16 entries) is correct in the three
versions. The dedup by `memcmp` of `0x190` bytes and the emission via `ioctl_internal`
are also preserved.

## 7. Conclusions

- **Linking**: today's data path symbols exist with identical decorated names on 23J631 and 24H232 (§1). No signature changed: the const overload of
  `enqueuePackets` and `prepare(IP20IOSkywalkPacketQueueyj)` are in both old versions.
- **Behavior**: `legacyDequeue` (return = consumed, §2), both `enqueuePackets`
  (strict capacity `n < +0xAC`, `opts` without effect, §3), the packet accessors and the
  pool (§4), `getPeerManager` and `preQueuePacketCheckQuietMode` (§5), the gate of
  `interfaceStateChange` (slot `+0xB88` stable, §5.3), TLV 24 (§6.1) and the payload of the
  129 (§6.2) replicate the 25G241 contracts documented in `AWDL_TX_DATOS_SPEC.md`,
  `AWDL_RX_DATA_TO_SKYWALK.md`, `AWDL_TX_HOOKS_POR_PAQUETE.md`,
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7/§3.11 and `AWDL_SIN_ASOCIACION_INFRA.md` §2.1.
- **No new per-version dependencies**: everything the data path added today uses is
  a non-virtual C symbol (`__asm`) or an internal family contract. The only
  version-dependent pieces it touches remain those already covered by `AIRLAYER_PORT_14_15.md`
  (headers and vtables per build: `postMessage`, `setLinkState`, the `init` super-call).
- The controller ivar that `interfaceStateChange` queries moves per version
  (`0x560/0x548/0x550`); irrelevant for the kext as long as it is not referenced directly.

### Open

| Point | Status |
|---|---|
| Identity of the object of the `interfaceStateChange` gate (vif of `awdl0`, primary or infra) | `?` in the three versions |
| Null-action gate of `IOSkywalkTxSubmissionQueue::initWithPool` on 14/15 | `?` (no dump; the `withPool` thunk of 7 args was checked) |
| Readers of the internal ivars that change (`+0x7EE0/+0x7F28/+0xA648`, AWDLOn) | Covered by `AWDL_PRESENCE_OFFSETS_14_15.md`; out of scope here |
