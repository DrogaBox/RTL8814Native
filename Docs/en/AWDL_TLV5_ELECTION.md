# TLV 5 — election parameters v1: body on the air, kernel parser and render from ioctl 155

This document pins the on-air body of the election TLV v1 (type 5), the parser the kernel applies to it
on reception and the field-by-field correspondence with the payload of ioctl 155
(`setAWDL_ELECTION_PARAMS`, 36 B) that the RTL8814 driver must convert into bytes. It closes the `?` of
`AWDL_PSF_MIF_EMISSION.md` §5.2 and complements `AWDL_TLV_BODIES.md` (types 4 and 18),
`AWDL_FRAME_WIRE_FORMAT.md` §4 (route of type 5 in the `switch`) and `AWDL_OPEN_QUESTIONS_1.md` §1
(TLV v2, type 24). Unconfirmed items are marked `?`.

Sources: captures `~/Desktop/AWDLLab/refs/owly/tests/captures/{discovery,receive,send}-ios26.x-mt7925e-ch44-1.pcap`
(4,893 TLVs 5 decoded, read-only) and kernelcache `BootKernelExtensions.kc` (macOS 26,
Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`) over a copy of the annotated IDB. Frame
numbers are per capture, counting all the records of the pcap. The external owly spec
(`refs/owly/docs/SPEC.md` §3.2, "Election Parameters (5)") matches byte for byte what was measured.

**Type numbering**: the label of the `switch` of `actionFrameReport` is the type minus 2
(`AWDL_TLV_BODIES.md`); TLV 5 corresponds to case 3 of the pseudocode, which IDA labels
`jumptable FFFFFF80021B03ED case 5` at `0xffffff80021b0a63` (the disassembly label already
includes the bias), with the parser call at `0xffffff80021b0a77`
(`AWDL_FRAME_WIRE_FORMAT.md` §4).

## 1. Body on the air

Complete TLV of **24 B**: 3 B header (`05` type, u16 LE length = 21 = `15 00`) and 21 B body. The offsets of the table count from the start of the TLV (header included); the body
starts at `+3`. Multi-byte integers are little-endian.

| TLV offset | Body offset | Size | Field (owly SPEC §3.2) | Measured values (4,893 TLVs) |
|---:|---:|---:|---|---|
| `+0` | `+0` | 1 | type | `0x05` |
| `+1` | `+1` | 2 | length | `21` (0x15) in all 4,893 |
| `+3` | `+0` | 1 | flags | `0` in all 4,893 |
| `+4` | `+1` | 2 | election ID | `0` in all 4,893 |
| `+6` | `+3` | 1 | distance to the root master | `0` in 1,462 (masters), `1` in 3,431 (followers) |
| `+7` | `+4` | 1 | reserved | `0` in all 4,893 |
| `+8` | `+5` | 6 | MAC of the root master | the root MAC of the session (§4) |
| `+14` | `+11` | 4 | root master metric | `525` (0x20D) in 4,868, `65` (0x41) in 25 |
| `+18` | `+15` | 4 | own metric | `525` in 1,462 (masters), `60` (0x3C) in 3,431 (followers) |
| `+22` | `+19` | 2 | padding | `00 00` in all 4,893 |

Patterns of the captures (one session per pcap, channel 44, mt7925e):

- **Master** (`dist = 0`): the root MAC is its own and `root_metric == self_metric`
  (e.g. `discovery` frame 3: sender `02:00:00:00:00:02`, dist 0, root
  `02:00:00:00:00:02`, 525/525).
- **Follower** (`dist = 1`): the root MAC is that of the master, `root_metric` is the metric the
  master announces and `self_metric` is its own (e.g. `discovery` frame 1: sender
  `02:00:00:00:00:03`, dist 1, root `02:00:00:00:00:02`, 525/60).
- In `send` there are 25 frames of `02:00:00:00:00:04` with root `02:00:00:00:00:01` and `root_metric = 65`
  (frames 1866–1898, 1946–1947, 2025, 2078, 2096, 2134): the follower follows a master with metric
  65, higher than its own 60. In the kernel an announced root "wins" with a greater metric
  (`AWDL_OPEN_QUESTIONS_1.md` §1, `forceElection bec of better Root`), consistent with
  `owly SPEC` AWDL-9 (raise the metric to win).
- TLV 5 travels in **second position** (after TLV 4) both in PSF (subtype 0) and in MIF
  (subtype 3): counts per capture in §4.

## 2. Kernel parser: `parseAwdlElectionParamsV1`

Function: `IO80211AWDLPeer::parseAwdlElectionParamsV1` (`0xffffff80021b50ec..0xffffff80021b5215`,
0x129 B), single call from case 3 of the `switch` (`0xffffff80021b0a77`).

```c
int parseAwdlElectionParamsV1(IO80211AWDLPeer *this, const IO80211BufferCursor *tlv);
```

### 2.1 Input checks

- It requires `tlv->getCursorSize() >= 0x18` (24 B = 3 B header + 21 B body;
  `cmp qword ptr [rsi+8], 18h` at `0xffffff80021b50f9`); with a smaller cursor or a null data
  pointer (jump at `0xffffff80021b5120`) it goes to `0xffffff80021b515b`:
  `logDebug("... Error: Assert failure", "parseAwdlElectionParamsV1", source line 5844)` and
  returns without touching the peer.
- `tlvCheckBounds` (`0xffffff80021aefa0`, called from `0xffffff80021b03ad` before the `switch`)
  applies to type 5 a minimum of **21** and a maximum of **37** over the TLV length (body,
  without header): `case 5u: v9 = 21; LODWORD(v10) = 37;`. A 21 B body (the on-air one) passes;
  bodies of 22..37 B are accepted and the surplus bytes are not read.

### 2.2 Gate of case 5 in `actionFrameReport`

The `switch` case only calls the parser if bit 0 of byte `+87` (`+0x57`) of the context of
`actionFrameReport` is 1 (`test byte ptr [rbx+57h], 1` at `0xffffff80021b0a66`). The same
test protects other cases of the `switch` (`0xffffff80021b0450`, `0xffffff80021b05b0`,
`0xffffff80021b0621`, `0xffffff80021b070b`, `0xffffff80021b0c24`), so it is not exclusive to
type 5. Writers of the byte in the same function:

- `0xffffff80021aff76`: set to 1 after the parsing of the header.
- `0xffffff80021afff4`: set to 0 if `getUnicastOptionsFromDataPathTLV` returns non-zero
  over the cursor that `findTLVCursorForType(..., 0x0C)` looks up at `0xffffff80021affca` (TLV 12,
  datapath state).
- `0xffffff80021b0097`: set to 0 in a branch following the lookup of TLV 31
  (`findTLVCursorForType(..., 0x1F)` at `0xffffff80021b006d`); the exact jump condition has not been
  traced `?`.

In the captures PSF and MIF carry TLV 12, so the condition is met. Whether an AF without TLV 12
making `getUnicastOptionsFromDataPathTLV` fail leaves it unset is `?`. The owly rule ("a node ignores
type 5 if the peer also sends type 24") does not appear on this parse route `?`; the kernel parses
v1 and v2 through independent routes (v2 in `doMinimalAFProcessing`, `AWDL_OPEN_QUESTIONS_1.md` §1).

### 2.3 Fields read and destination

Cursor offsets relative to the start of the TLV (the cursor includes the header; the body starts at
`+3`). The destination block is the private ivars of the peer (`*(peer+0x20)`), with the exact store
addresses of `0xffffff80021b51b8..0xffffff80021b51ec`:

| Read | Destination ivar | Store | Field |
|---|---|---|---|
| `tlv+4` u16 | `+612` (4 B store with zero extension) | `0xffffff80021b51cd` | election ID (body `+1`) |
| `tlv+6` u8 | `+628` | `0xffffff80021b51c2` | distance (body `+3`) |
| `tlv+8` u32 and `tlv+12` u16 | `+606` and `+610` | `0xffffff80021b51ec` / `0xffffff80021b51e5` | root MAC (body `+5..+10`) |
| `tlv+14` u32 | `+632` | `0xffffff80021b51b8` | root metric (body `+11`) |
| `tlv+18` u32 | `+624` | `0xffffff80021b51d7` | own metric (body `+15`) |

The flags byte (body `+0`), the reserved one (`+4`) and the padding (`+19..+20`) are not read.

### 2.4 Update semantics

The parser first compares the stored root MAC (`+606`, 6 B) with that of the TLV
(`memcmp` at `0xffffff80021b5141`, 6 B):

- **Different MAC**: `+700 = 1` (`0xffffff80021b514e`) and complete rewrite of
  `+606/+610/+612/+624/+628/+632`.
- **Same MAC**: `+700 = 0` (`0xffffff80021b5194`); it rewrites only if the root metric changed
  (`cmp` at `0xffffff80021b51a5`) or the distance (`0xffffff80021b51b2`). A change only in the
  election ID does not trigger a rewrite.
- The return is the root metric (the one read from the TLV or the stored one if nothing changed); case 5
  of the `switch` does not consume it.

`IO80211AWDLPeer::getElectionParams` (`0xffffff80021bf124`) returns `*(peer+0x20)+556`, the block
of TLV v2, not this v1 block. Readers of `+606..+632` and of the mark `+700` outside the parser itself:
not located in this pass `?` ("old shape" block per `AWDL_OPEN_QUESTIONS_1.md` §1).

## 3. Render from ioctl 155

### 3.1 Payload of the 155 (36 B, re-verified by raw stores)

`programChipWithNewMaster` (`0xffffff80021ec4b6`) assembles the payload of the 155 (cmd
`0x9B802869C8`) with these stores (`0xffffff80021eca24..0xffffff80021eca6a`; same in
`initFWElectionParams` `0xffffff80021ed200`, decompiled in the pass):

| Payload | Manager ivar | Field |
|---|---|---|
| `+0x00..+0x07` | — | `0`; `dword +0x08 = 1` ("valid") |
| `+0x10` u8 | `+37263` | op_mode; a sweep of displacement `0x918F` in IO80211Family yields only readers (`electionParamsChanged`, `programChipWithNewMaster`, `initFWElectionParams`), no direct writer `?` |
| `+0x12` u16 | `+37259` | election ID |
| `+0x14` u8 | `+37247` | hop count: peer distance + 1 (`setElectionParamsToPeer` `0xffffff80021ea50e` reads the v2 TLV of the peer at `+575` and adds 1); 1 if the forced master `+37472` differs from the local MAC and 0 if it matches (`setElectionParamsToForcedPeer` `0xffffff80021ea6f8`); `setElectionParamsToSelf` `0xffffff80021ea494` sets it to 0 (`mov dword ptr [rax+917Fh], 0` at `0xffffff80021ea4ea`), so a node becoming master again announces distance 0 |
| `+0x16` 6 B | `+37231..+37236` | root MAC (own if acting as master — `setElectionParamsToSelf` `0xffffff80021ea494` copies `+37216` — or that of the master peer) |
| `+0x1c` u32 | `+37251` | root metric (the peer's, `+579`, or the project's own `+37255`) |
| `+0x20` u32 | `+37255` | own metric |

This corroborates the struct of `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.6 and corrects the compact 24 B
layout that `AWDL_PEERMANAGER_SOFTWARE.md` §3 records for the same ioctl. Note: helper
`setAwdlElectionParams` (`0xffffff800221cb66`, called from `0xffffff8002289191`) writes
`dword +0x00 = 1` (version?, `?`) before routing the 155, while the two internal routes
leave `+0x00..+0x07` at 0; the fields of the render (`+0x10..+0x23`) are identical in both.

### 3.2 Air ↔ 155 correspondence

Each field of the on-air body has a direct counterpart in the payload of the 155, in the same
relative order; the difference is that the payload adds the `op_mode` byte in front and the air adds the
reserved byte `+4` and 2 B of padding at the end:

| Air (TLV offset) | Size | 155 source |
|---:|---:|---|
| `+3` flags | 1 | no clear counterpart: on the air `0`; candidate byte `+0x10` (op_mode) `?` |
| `+4` election ID | 2 | `+0x12` (u16 LE) |
| `+6` distance | 1 | `+0x14` (hop count; air: 0 master, 1 follower) |
| `+7` reserved | 1 | `0` (pad of `+0x15` of the payload) |
| `+8` root MAC | 6 | `+0x16..+0x1B` |
| `+14` root metric | 4 | `+0x1c` (u32 LE) |
| `+18` own metric | 4 | `+0x20` (u32 LE) |
| `+22` padding | 2 | `00 00` |

### 3.3 Render recipe for the RTL8814 layer

For each PSF/MIF (and UMI), insert after TLV 4 a 24 B TLV:

```
05 15 00                      ; type 5, len 21 (u16 LE)
<flags>                       ; 0 (byte +0x10 of the 155 as alternative, ?)
<election_id:2 LE>            ; 155 +0x12
<hop>                         ; 155 +0x14
00                            ; reserved
<root_mac:6>                  ; 155 +0x16
<root_metric:4 LE>            ; 155 +0x1c
<self_metric:4 LE>            ; 155 +0x20
00 00                         ; padding
```

- Fixed length 21 in the len; the parser accepts 21..37 but the air only uses 21.
- The three middle fields already travel coherent in the 155: the hop arrives with the +1 of the hop
  relative to the peer, and the root metric is the one the master announced (via the peer v2 in
  `setElectionParamsToPeer`), so the render need not recompute anything.
- `self_metric` and `root_metric` observed: 60 by default in followers (owly AWDL-8) and 525/65 in
  masters; the kernel inverts the usual comparison (greater metric wins,
  `AWDL_OPEN_QUESTIONS_1.md` §1).
- If the current master is the node itself, the 155 arrives with root = own MAC, hop = 0 and
  `root_metric == self_metric` (pattern of the frames with `dist = 0` of §4).

## 4. Matrix of the captures

Counts of TLVs 5 per capture, sender, role and subtype (0 = PSF, 3 = MIF/UMI; master and follower
emit both):

| Capture | Sender | dist | root | root/self | PSF | MIF | Total |
|---|---|---:|---|---|---:|---:|---:|
| `discovery` | `02:00:00:00:00:02` (master) | 0 | itself | 525/525 | 296 | 101 | 397 |
| `discovery` | `02:00:00:00:00:03` (local, follower) | 1 | `:02` | 525/60 | 303 | 519 | 822 |
| `receive` | `02:00:00:00:00:01` (master) | 0 | itself | 525/525 | 312 | 190 | 502 |
| `receive` | `02:00:00:00:00:02` (local, follower) | 1 | `:01` | 525/60 | 367 | 630 | 997 |
| `send` | `02:00:00:00:00:03` (master) | 0 | itself | 525/525 | 97 | 466 | 563 |
| `send` | `02:00:00:00:00:04` (local, follower) | 1 | `:03` | 525/60 | 584 | 1003 | 1587 |
| `send` | `02:00:00:00:00:04` (local, follower) | 1 | `:01` | **65**/60 | 9 | 16 | 25 |

Total 4,893 (1,968 in PSF, 2,925 in MIF/UMI). The three masters split the 1,462 TLVs with
`dist = 0`; the 3,431 with `dist = 1` are the followers. Constant values in the three sessions:
len 21, flags 0, election ID 0, reserved 0, padding `00 00`. The `local` label of each session is
the one of the toml replay file of the owly project itself (`crates/owly-radio/tests/replay.rs`).

## 5. Open

- On-air flags byte (body `+0`): whether it is a copy of the `op_mode` of the 155 (`+0x10`, ivar
  `+37263`) or a firmware-fixed 0. The writer of `ivars+37263` did not appear in the reviewed election
  functions (`programChipWithNewMaster` only reads it) `?`.
- Consumers of the v1 block of the peer (`+606..+632`) and of the mark `+700`: only the
  parser itself has been seen; the peer exposes the v2 block (`+556`) outward `?`. The other accesses with
  displacement `0x2BC` in IO80211Family belong to other classes (`IO80211P2PSteeringManager`,
  `IO80211LQMData`, `IO80211P2PSupervisor`, `IO80211Peer::print`), not to the private block of the peer.
- The owly rule "ignore type 5 if the peer sends 24": no equivalent on the parse route;
  if it exists, it would be applied at the election decision level, not at parse level `?`.
- Why the masters of the captures announce own metric 525 (and 65 in the `:01` case):
  election dynamics not covered by this document; the kernel only compares greater metrics as
  "better root" (`?` on the origin of those values).
