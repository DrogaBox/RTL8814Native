# Open questions of AWDL parsing (I): election v2, timeline and unnamed fields

This document answers three questions left open in `AWDL_FRAME_WIRE_FORMAT.md` and
`AWDL_TLV_BODIES.md`: where the election params v2 TLV enters (type 24 on the air, which
falls into the default case of the `switch` of `actionFrameReport`), what
`IO80211AWDLPeer::monitorPeersTimeline` does with the two times of the fixed header of the action frame and
with the dword `+48` of the metadata header, and what the still-unnamed bytes of the sync params TLV
(`+21..+23`) and of the channel sequence (`+5..+8`) are called. Unconfirmed items are marked `?`.

Source: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
IDB `Build/ida_awdl/task_awdl_open1/kc.i64` (copy of the one of `task_awdl_pm_abi`). VAs of the
kernelcache. Reminder: the `switch` of `actionFrameReport` (`0xffffff80021af6be`) operates on
`type - 2` (subtraction at `0xffffff80021b03ce`), so switch label 22 is type 24 on the
air; the TLV displacements cited here count from the start of the 3 B TLV header,
as in `AWDL_TLV_BODIES.md`.

New reference functions:

| Function | Address | Role |
|---|---|---|
| `IO80211AWDLPeer::doMinimalAFProcessing` | `0xffffff80021b3ea6` | second TLV pass: sync params, election v2, HT cap, chan seq |
| `IO80211AWDLPeer::parseAwdlElectionParamsV2` | `0xffffff80021ba26c` | election TLV v2 (type 24) |
| `IO80211AWDLPeer::parseAwdlElectionParamsV1` | `0xffffff80021b50ec` | election TLV v1 (type 5, via `switch`) |
| `IO80211AWDLPeer::monitorPeersTimeline` | `0xffffff80021b86ec` | timeline deviation of the peer |
| `IO80211AWDLPeer::calculatePeerTimeDiff` | `0xffffff80021ba5a6` | formula of the deviation |
| `IO80211AWDLPeerManager::getCurrentAWSeqNum` | `0xffffff800223507a` | current AW (ivars +16104) |
| `IO80211AWDLPeerManager::getCurrentAWSlot` | `0xffffff800223508a` | current slot (ivars +16128) |
| `IO80211AWDLPeerManager::buildAWDLElectionParamsVer2` | `0xffffff8002210a3c` | construction of TLV 24 in the templates |
| `IO80211AWDLPeer::updateAwdlChanSeq` | `0xffffff80021ab7e4` | applies the channel sequence of the peer |
| `IO80211AWDLPeerManager::reportDataPathEvents` | `0xffffff80021dd8f4` | events 41/42/43: AW number and slot |
| `IO80211AWDLPeerManager::monitorTimeline` | `0xffffff80021e11ca` | contrast of the `FwAwStartTime` of event 41 |
| `IO80211AWDLPeerManager::logAWStart` | `0xffffff80021e1b42` | window start log and copy of the time |
| `IO80211AWDLPeerManager::getAwStartFwTimeUsec` | `0xffffff8002234f5e` | reader of `ivars+796` (with byteswap) |

## 1. Election params TLV v2 (type 24): parse path and body

### 1.1 The path: `doMinimalAFProcessing`, not the `switch`

Type 24 falls into the default case of the `switch` of `actionFrameReport`
(`0xffffff80021b0cb2`, which only retrieves a record and jumps to `0xffffff80021b133b` to continue
the walk). Its parsing is in the second pass that `actionFrameReport` delegates:
`IO80211AWDLPeer::doMinimalAFProcessing(const actionFrameReportContext*)`, called at
`0xffffff80021b013f` (only call in the binary), right after the block of the real-time group TLV.
`actionFrameReport` checks the return at `0xffffff80021b0144`: a value 2 jumps to
`0xffffff80021b016b` and discards the AF; other values continue toward `shouldStopAFProcessing`.

`doMinimalAFProcessing` (`0xffffff80021b3ea6`, 0xa11 B) rebuilds two cursors from the
context: the body one (pointer from cell `+0x168` of the vtable of the `IO80211Buffer` stored
in the context — a cell not listed in the table of `AWDL_PEERMANAGER_DRIVER_ABI.md` — and length
from `+0x130`, `getLength`) and the metadata header one (context `a2[3]`/`a2[4]`, which
`parseAwdlSyncParamsTlv` receives as third argument). It walks the TLVs from `buf+16` with the same
`IO80211TLVIterator<IO80211TLVHeader<unsigned char, unsigned short, 0, 1, 0>>` and dispatches by the
**raw type** (without the subtraction of 2 of the first `switch`): 4 (sync params), 7 (HT cap), 17
(IEEE 802.11 container), 18 (channel sequence), 24 (election v2) and 32 (extended sequence);
other types only advance the iterator.

Condition and validations of the call to `parseAwdlElectionParamsV2` (disassembly
`0xffffff80021b4264` .. `0xffffff80021b42ae`):

1. bit 0 of byte `+0x57` of the `actionFrameReportContext` set to 1 (UMI flag; `actionFrameReport`
   sets and clears it around the processing of the datapath state TLV, consistent with the trace
   "parsing the  RTG TLV in UMI" of the same zone) `?` exact condition of setting;
2. TLV length (`*(u16*)(tlv+1)`) `>= 0x28` (`cmp ax, 0x28` at `0xffffff80021b426c`);
3. 43 B (`0x2B`) fit from the cursor (`cmp rax, 0x2A` at `0xffffff80021b4294`);
4. length **exactly 40** (`cmp r13d, 0x28` at `0xffffff80021b429a`).

Failure of 2 or 4 goes to `0xffffff80021b4742`: trace
`"%s:%u Error Invalid length : %lu\n"` (source line 3918) and return 2, which `actionFrameReport`
turns into discarding the whole AF. That is: in a UMI, a malformed election v2 TLV is not
silently ignored like the unknown types, but aborts the frame.

The same pass parses the other TLVs with these validations (all in `doMinimalAFProcessing`):

- type 4: cursor ≥ `0x2A` (42 B), `len >= 0x27` (39) and byte `tlv+36` (header of the embedded
  channel sequence) `<= 0x1F`; if not, `"%s:%u Error Invalid length : %u\n"` (3903); if
  `parseAwdlSyncParamsTlv` returns 0, `"Unable to parse Sync Parameters from ..."`, line 3898.
- type 7: `len >= 29`, sub-cursor `{tlv+5, len-2}`; call to `IO80211Peer::parseHTCapTLV`
  (`0xffffff80021b4257`).
- type 18: `len >= 6` (failure 3941) and call to `parseAwdlChanSeqFromChanSeqTLV`.
- type 32: call to `parseAwdlChanSeqFromChanSeqExtTLV` (only with the SDB flag of the manager active).
- type 17: call to `parseAwdlIEEE80211CntnrTLV`; negative return ⇒ `"Error Parameter : %lu"`.
- It also records the peer discovery ("New Peer ... discovered and msecSinceAwdlOn", line
  3881; "discovered and (numSyncActionFrameRx: %u, postedtoNDP:%s...", 3885) and stamps
  `mach_absolute_time()` into peer ivar `+736`.

`parseAwdlElectionParamsV1` (`0xffffff80021b50ec`), in contrast, is indeed invoked from the first
`switch` (case of type 5, call at `0xffffff80021b0a77`) and works with a 24 B body
(`cursor >= 0x18`), destined to peer ivars `+606..+632` (root MAC `+606`, `+610`,
`+612`, `+624`, `+628`, `+632`) — old form, unrelated to the `+556` block of the v2.

### 1.2 Validations inside `parseAwdlElectionParamsV2`

`0xffffff80021ba26c`:

1. The cursor (which includes the 3 B TLV header) must measure at least `0x2B` (43 B). If not, trace
   `"%s::%s Invalid AwdlElectionParamsV2\n"` and return.
2. The length field must be **exactly 40** (`*(u16*)(tlv+1) != 40` ⇒ trace
   `"%s::%s Invalid SyncTreeTLV Length\n"` — text inherited from the v1 — and return). That is: TLV
   total 43 B, 40 B body. The limit matches the one the caller imposes (§1.1).

### 1.3 TLV body (40 B of body, 43 B total)

The manager builder `buildAWDLElectionParamsVer2` (`0xffffff8002210a3c`) writes the same
layout the parser reads; the offsets of the table are from the start of the TLV (the body starts at
`+3`), all multi-byte fields in **little-endian order** (they are read and written with native
accesses, no byteswap):

| TLV+ | Body+ | Size | Field | Destination in the peer (ivars) |
|---:|---:|---:|---|---|
| `+0` | — | 1 | type (24) | — |
| `+1` | — | 2 | length (40) | validated, not stored |
| `+3` | `+0` | 6 | root MAC (root) | `+559` (compared with the manager root `+37231`) |
| `+9` | `+6` | 6 | parent MAC (parent) | `+565` (within the copied block) |
| `+15` | `+12` | 4 | `rootLFC` (frame count of the root) | `+571` |
| `+19` | `+16` | 4 | `hopCount` | `+575` |
| `+23` | `+20` | 4 | `rootMetric` | `+579` |
| `+27` | `+24` | 4 | `selfMetric` | `+583` |
| `+31` | `+28` | 4 | `electionId` | `+587` |
| `+35` | `+32` | 4 | zero (the builder writes the `electionId` with an 8 B zero-extended store) `?` | — |
| `+39` | `+36` | 4 | `selfLFC` | `+591` (the builder takes it from ivars `+37560`) |

The parser copies the whole TLV (43 B, header included) into peer ivar `+556` with
`IO80211BufferCursor::copyFrom`; that is why the body fields land at `+559`, `+571`, `+575`,
`+579`, `+583` and `+587`. **Correction** to `AWDL_PEERMANAGER_SOFTWARE.md` §8: the 6 B block at
`+559` is not a "6 B electionId" but the **root MAC** — the evidence is the 6 B `memcmp` against
the root MAC of the manager (`+37231`) both in `parseAwdlElectionParamsV2` and in the comparison of
`runParentElection`; the 32-bit `electionId` is at `+587` (same offset the doc already cited).

Comparisons and consequences in `parseAwdlElectionParamsV2`:

- Prior `memcmp` of the parent MAC of the manager (`+37237`) against the source MAC of the AF (context
  `+88`, 6 B): marks whether the sender is the current electoral parent.
- Same root as stored (`tlv+3` == peer `+559`) and new `rootLFC` (`tlv+15`) **lower** than
  the stored one (`+571`): return without touching anything (old frame of the same root).
- If the 43 B are identical to the previous copy (`+556`, `memcmp` of `0x2B`): nothing to force; only
  `+105` is refreshed.
- If they differ: they are copied to `+556` and, if the sender is the current parent, trace
  `"%s:%u forceElection bec Parent changed election params\n"` (source line 5903) and election is
  forced; if the sender is not the parent, election is forced only if the announced root "wins" over
  the manager's (peer `rootMetric` `+579` > manager `rootMetric` `+37251`, or equality with a greater
  root MAC by `memcmp`), with the trace `"%s:%u forceElection bec of better Root\n"` (5923); if the peer already
  points at the same root as the manager but the manager hop count exceeds the announced one by more than 1
  (`+37247 > peer+575 + 1`), it renounces with `"%s:%u NO forceElection bec of HC change  \n"` (5915).
- When forcing: manager flag `+10306` (rescheduled election, the one `runElection` serves) to 1 and
  peer `+105` = (peer root MAC == manager root MAC).

For a driver emitting election params v2: body of 40 B, little-endian, with the eight fields of
the table; the length must be exactly 40 and the TLV is processed in UMI-type AFs.

## 2. `IO80211AWDLPeer::monitorPeersTimeline`: phy_tx_time, target_tx_time and the +48 dword

### 2.1 Entry

Only call: `parseAwdlSyncParamsTlv`, at `0xffffff80021b508a`:

```c
IO80211AWDLPeer::monitorPeersTimeline(peer, tlv, v18, v17, *(uint32 *)(hdr + 48));
```

with `v18 = *(u32*)(buf+8)` (fixed header `+8`, `phy_tx_time`), `v17 = *(u32*)(buf+12)`
(`target_tx_time`) and the `+48` (`+0x30`) dword of the 72 B metadata header. Mangled signature:
`(const IO80211BufferCursor&, uint, uint, uint)`. The call is at the end of
`parseAwdlSyncParamsTlv` and is only reached when the parsing has passed its validations (non-null
cursors, non-null presence); the early exits (assert of line 3399 and
"Zero presence Mode") never reach it. It runs both through the `switch` path and through the
`doMinimalAFProcessing` one, regardless of `+8`/`+12` being zero.

### 2.2 The computation (`calculatePeerTimeDiff`, `0xffffff80021ba5a6`)

Units: **microseconds** in the domain of the firmware clock of the receiver (the same one of the
`FwAwStartTime` of event 41); 1 TU = 1024 µs (`<< 10`); the full period of the timeline
is 2^20 µs = 1024 TU (16 windows of 64 TU), and the intermediate arithmetic is 32-bit.

Manager state it uses:

- `currAwNum` = return of cell 95 of the manager vtable (offset `+0x2F8`, call at
  `0xffffff80021b8769`), `getCurrentAWSeqNum` (`0xffffff800223507a`, read of `ivars+16104` at
  `0xffffff8002235082`);
- `currAwStartFwTime` = `byteswap(*(u32*)(ivars+796))` (load at `0xffffff80021b8771`, `bswap` at
  `0xffffff80021b877c`) minus **3500 µs** of guard (`add r13d, 0FFFFF254h` at
  `0xffffff80021b877f`).

Formula verified in the decompilation (`tlv` = pointer to the sync params TLV with header):

```c
peerNextAwNum = pm + sn - sn % pm;             // pm = tlv[30] (u8), sn = *(u16*)(tlv+32), all u16
v8 = (currAwStartFwTime - 3500)
     + *(u16*)(tlv+8) * (peerNextAwNum - currAwNum) << 10;   // *(u16*)(tlv+8) = AW in TU
v9 = (*(u16*)(tlv+4) << 10)                     // dc (tlv+4) in TU → µs
     + target_tx_time (buf+12)
     + afReceiveFwTime (hdr+48)
     - phy_tx_time (buf+8);
d  = |v8 - v9|;  r = d & 0xFFFFF;               // modulo 2^20 µs
return (r < 0x80001) ? r : r - 0x100000;        // centered: [−0x7FFFF, +0x80000] µs
```

Reads: `pm` (TLV+30, presence), `sn` (TLV+32, peer sequence number — the log calls it
`afAwNum`), `*(u16*)(tlv+8)` = duration of the availability window in TU (field `+8`, peer ivar
`+762`), `*(u16*)(tlv+4)` = `dc` (TLV+4, ivar `+758`; the log prints it as `downCounter`
multiplied by 1024). The term `afReceiveFwTime − phy_tx_time` is the flight time of the frame
(reception minus physical emission); `target_tx_time` is the sender's stamp. The comparison pits
the predicted start of the peer window (v8, on its own clock) against the instant of AF emission
corrected with flight and `dc` (v9); the sign is lost (`d` is an absolute value), so the
deviation measures distance, not advance nor delay `?` (so it reads from the disassembly; the design
intent is not verifiable). Since the final reduction is modulo 2^20 µs and the product is multiplied
by 1024, the deviation only depends on the position of the frame within the 16-window period.

A cursor with less than 42 B or null produces `"%s::%s[%u] Error: Invalid sync params\n"` (source
line 4185) and a difference of 0.

### 2.3 Use of the difference in `monitorPeersTimeline` (`0xffffff80021b86ec`)

- Entry guard: cursor of the TLV ≥ `0x2A` (42 B) and non-null (comparison at
  `0xffffff80021b8728`); otherwise trace
  `"%s::%s() Peer %02X:...: Invalid sync params\n"` and the peer is not touched.
- `peer+192` (dword) = returned difference (µs, with the modular centering described; store at
  `0xffffff80021b87b3`).
- Threshold: `0x199A` = **6554 µs** (≈ 6.55 ms ≈ 1/10 of the 65.536 ms window); the absolute value and
  the comparison are at `0xffffff80021b87bc` (`neg`/`cmovs`) and `0xffffff80021b87c3`
  (`cmp r15d, 199Ah`):
  - With |shift| ≥ 6554 and bit 0 of byte `peer+1667` at 1 (was synchronized): trace
    `"%s::%s() Peer ...: currAwNum = %d, currAwStartFwTime = %u, afReceiveFwTime = %u, afAwNum = %d, downCounter = %d"`
    (with `afAwNum = sn` and `downCounter = dc << 10`), trace
    `"%s::%s() Peer ...: timeline is shifted by %8d[usec] (%3d[AWs] & %3d[msec]) relative to current timeline"`
    (AWs = shift/65536; ms = remainder/1000), stamps `mach_absolute_time()` at `peer+176` (instant when
    it stopped being synchronized), samples the elapsed time (`now − peer+184`, that is the
    duration of the ending "synchronized" state, converted to seconds) into the histogram
    `_syncStats` (10 bins) and traces `"%s::%s() Peer ...: Became UnSynced\n"`.
  - With |shift| < 6554 and the bit at 0 (was not synchronized): stamps `mach_absolute_time()` at
    `peer+184`, samples `now − peer+176` (duration of the "not synchronized" state) into the histogram
    `unk_FFFFFF8002488750` (10 bins) and traces `"%s::%s() Peer ...: became synced\n"`.
- Always: boolean sampling into the histogram `unk_FFFFFF80024889A8` (2 bins) with 1.0 if
  |shift| < 6554 and 0.0 otherwise, and `bit 0 of peer+1667 = (|shift| < 6554)`.

Summarized consequences: bit `peer+1667.0` is the state "synchronized with this peer" (the
peer constructor initializes it to 0); `peer+192` keeps the last deviation in µs; `peer+176` and
`peer+184` are the marks of when it became unsynchronized and when synchronized. `?` consumers
of bit `+1667` outside this function and the peer init (none located in the analyzed
functions).

### 2.4 Where `currAwNum` and `currAwStartFwTime` come from

- `ivars+16104` (`currAwNum`): the handler of event 41
  (`APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`) inside `reportDataPathEvents` does
  `ivars+16104 = *(u32*)(payload+0)` (store at `0xffffff80021dfbb5`); the `AwNumber` must go in the
  low 16 bits `?` on the high 16 bits. From the same number the slot derives: with
  `n = ivars+42820` (sequence channels) and `s = ivars+42867`,
  `slot = (awNum % ((s+1)·n)) / (s+1)` capped at 15, written into `ivars+16128` (`0xffffff80021e0226`).
- `currAwStartFwTime` (`ivars+796`): **`monitorTimeline` writes it**. At the end (common
  end label, stores at `0xffffff80021e18ee` .. `0xffffff80021e191f`) it copies the payload of event 41
  **raw** to `ivars+776..811` (`payload+0` → `+776`, `payload+8` → `+784`, `payload+16` →
  `+792`, `payload+24` → `+800`, `payload+32` → `+808`); so `FwAwStartTime` (`payload+20`,
  **big-endian**) lands at `+796` exactly as the driver delivered it. Readers apply
  `_byteswap_ulong` to it: `monitorPeersTimeline` (`0xffffff80021b8771`: load `[ivars+31Ch]` and `bswap`
  at `0xffffff80021b877c`), `getAwStartFwTimeUsec` (`0xffffff8002234f5e`, read at
  `0xffffff8002234f66`) and `monitorTimeline` itself (`0xffffff80021e126a`).
- `monitorTimeline` additionally validates the report: with the received AW number and the computed slot
  it deduces an expected start (`stored + slots·16384 µs`, each slot = 16 TU = 16384 µs, product
  `<< 14`) and if the deviation against the received `FwAwStartTime` reaches `0x1401` (5121 µs) it logs
  `"%s::%s() Detected AW start time inaccuracy: expetedAwStartFwTime = %u, currAwStartFwTime = %u,
  delta = %d [usec]\n"` (source line 32402); it also detects AW number jumps without a master
  change ("Detected AW Number discontinuity..."). That is, the window clock the driver
  reports must advance continuously and coherently with the scheduled channel sequence.
- `logAWStart` (`0xffffff80021e1b42`) re-stores the already-converted value (`byteswap(payload+20)`) at
  `ivars+42668` (`0xffffff80021e1b8f`) and emits the canonical log
  `"start:/Start:%u.%03u[fw:%u.%03u.%03u] sn=%03u/%02u ch=%3u ... [RLFC:%u HC:%u Rmet:%u SMet:%u
  SLFC:%u] nC:%u R:%u O:%u NR:%u pkt %u"` (RLFC = `+37243`, HC = `+37290`, Rmet = `+37294`,
  SMet = `+37298`, SLFC = `+37560`).

### 2.5 What a driver without AWDL firmware must fill

- `hdr+48` (`afReceiveFwTime`): the kernel reads it **without order conversion** (direct access
  `*(u32*)(hdr+48)`); the Broadcom producer writes there the already-converted value of the RX event
  (`_byteswap_ulong`), i.e. in native order. A custom driver must write at `+48` the reception stamp of the
  frame in **µs and native order (little-endian)**, in the same clock domain as the
  `FwAwStartTime` it reports via event 41 (which is stored big-endian and read with byteswap).
- `buf+8` (`phy_tx_time`) and `buf+12` (`target_tx_time`): they travel on the air; the sender writes them.
  For a peer to appear synchronized it must satisfy
  `target_tx_time + (afReceiveFwTime − phy_tx_time) + dc·1024 ≡ (currAwStartFwTime − 3500) +
  (peerNextAwNum − currAwNum)·AW_TU·1024` (modulo 2^20 µs) with error < 6554 µs. A coherent sender
  stamps `target_tx_time` with the start of its availability window and `phy_tx_time` with the
  actual instant of TX, both on its fw clock `?` (deduction from the formula; no constant of the
  binary confirms it).
- With the three fields at zero the TLV parsing is not affected (what is said in
  `AWDL_FRAME_WIRE_FORMAT.md` §5.4 still holds), but `monitorPeersTimeline` runs all the same and the resulting
  deviation will mark the peer as **not synchronized** (`peer+1667.0 = 0`,
  trace `Became UnSynced` at the first AF with a sync params TLV): **correction** to the previous doc,
  which suggested there was no consequence at all. A driver wanting "synchronized" peers must
  fill the three times with values coherent among themselves and with the `FwAwStartTime` of event 41.
- `sync_contention` (`peer+752`): only computed if `phy_tx_time` and `target_tx_time` are different
  from zero and `phy_tx_time > target_tx_time` (`parseAwdlSyncParamsTlv`); with zeros no contention
  is recorded. It is the only effect of `+8`/`+12` outside `monitorPeersTimeline`.

## 3. Unnamed bytes: sync params `+21..+23` and channel sequence `+5..+8`

### 3.1 Sync params `+20..+23` ("ext")

The only name evidence in the binary is the trace of `parseAwdlSyncParamsTlv` (conditioned on
`shouldLog(0x10000)`):

```c
"   sn=%u ext(%u/%u/%u/%u) dc=%u rem=%u tchan=%u mchan=%u pm=%u sync_contention:%u\n"
```

which prints the four "ext" bytes in the order **TLV+20, TLV+23, TLV+21, TLV+22** (and confirms the
names of the rest: `sn`=+32, `dc`=+4, `rem`=+18, `tchan`=+3, `mchan`=+6, `pm`=+30). Verified
behavior:

- `TLV+20` is copied to peer ivar `+1452`; `TLV+21`, `TLV+22` and `TLV+23` are not stored in
  any ivar.
- No consumer of `peer+1452` has appeared in the analyzed functions (nor in the dumps of
  the previous PeerManager tasks) `?`.
- The names of the four bytes remain `?`: the manager never builds a sync params TLV
  (the master indication template of `updateSyncFrameTemplate` `0xffffff80021dc5b2` only carries
  TLVs 6, 24, 12, 7±, 30±, 31±, 17±, 21, 20±, 23 and 33), so on the Broadcom path these
  bytes are generated by the firmware and the kernel only displays them.

### 3.2 Channel sequence `+5..+8`

The four bytes stay alive after parsing. In `parseAwdlChanSeqFromChanSeqTLV`
(`0xffffff80021b5a08`) they are copied into the output `awdl_chan_seq_info2`: `TLV+5` → `info2+2`,
`TLV+6` → `info2+3` and `*(u16*)(TLV+7)` → `info2+4` (next to `count` at `info2+0` and `encoding`
`TLV+4` at `info2+1`).

`updateAwdlChanSeq` (`0xffffff80021ab7e4`) applies them to the peer state with explicit arithmetic:

- `peer+908` = encoding (`info2+1`); `peer+909` = `min(count, 16)` (clip to 16 entries);
- `peer+911` = `TLV+5`; `peer+910` = `TLV+6`; `peer+916` (u16) = `TLV+7..8` without transformation;
- **period in slots** = `(TLV+5 + 1) · min(count,16) · (TLV+6 + 1)`, written into `peer+906` (u16),
  and required in the range `[1, 512]`: outside that range the function returns −2 without applying anything;
- the entries (12 B: `{u32 1, u32 channel, u32 flags}` from `info2+8`) are compared with the previous copy
  (`peer+924`/`peer+928`, flag mask `0x2018`) and re-copied to `peer+920..`.

The role of `TLV+6` is additionally confirmed in the manager itself: its copy (`ivars+42867`, field
`+0x07` of the struct of ioctl 129) determines how many windows each channel lasts in the slot split
of event 41 (`slot = (awNum % ((s+1)·n)) / (s+1)`, with `n = ivars+42820`): that is,
**`TLV+6` = number of windows per channel minus 1** (the step of the sequence), and **`TLV+5` multiplies
the whole period** (`TLV+5 + 1` repetitions). `TLV+7..8` takes part in no verified arithmetic `?`.

The canonical protocol names (`duplicate count` for `+5`, `step count` for `+6`, `fill
channel` for `+7..8`) come from the public dissectors of AWDL and are not verifiable from this
binary `?`; what is verified is the arithmetic above. The variant embedded in sync params repeats the
same structure four positions later: in `parseAwdlChanSeqFromSyncParamsTLV`
(`0xffffff80021b8196`) the header {`count−1`, `encoding`, `+38`, `+39`, u16 `+40`} is at
`TLV+36..41`, and byte `TLV+36` is the one `doMinimalAFProcessing` requires `<= 0x1F`.

## 4. Operational summary

- TLV 24 (election v2) is parsed in `doMinimalAFProcessing` (`0xffffff80021b3ea6`), the second
  pass that `actionFrameReport` triggers at `0xffffff80021b013f`, and only if the AF is UMI
  (context `+0x57` bit 0) and the length is exactly 40; a malformed TLV discards the whole frame
  ("Error Invalid length", return 2).
- The v2 body is 40 B little-endian: root MAC (+0), parent MAC (+6), rootLFC (+12), hopCount (+16),
  rootMetric (+20), selfMetric (+24), electionId (+28), selfLFC (+36); the parser copies it whole into
  peer `+556` and forces re-election when what the parent announces changes or a better root appears.
- `monitorPeersTimeline` turns `phy_tx_time` (buf+8), `target_tx_time` (buf+12) and `hdr+48`
  (reception stamp in native µs) into a timeline deviation modulo 2^20 µs, stored at
  `peer+192`; with deviation ≥ 6554 µs the peer becomes "not synchronized" (bit `peer+1667.0` at 0, with
  traces and histograms). A driver without firmware must stamp the three times coherently
  with its `FwAwStartTime` of event 41 if it wants synchronized peers; at zero, the peer stays
  permanently not synchronized.
- The state feeding the timeline travels via event 41: `reportDataPathEvents` writes
  `ivars+16104` (AW number, payload+0) and `ivars+16128` (slot), and `monitorTimeline` copies the
  payload raw to `ivars+776..811`, so the big-endian `FwAwStartTime` of `payload+20`
  lands at `ivars+796`, which is the value `monitorPeersTimeline` uses (with byteswap) as the start of the
  current window; the report must be continuous and precise (deviations ≥ 5121 µs are logged
  as "AW start time inaccuracy").
- Of the unnamed bytes: `+20..+23` of sync params are only printed (order `+20, +23, +21, +22`)
  and `+20` goes to `peer+1452` with no known consumer; `+5..+8` of the channel sequence are the
  factors of the period (`+5+1` repetitions, `+6+1` windows per channel, product ≤ 512 slots) and a
  u16 copied as-is (`+7..8`).
