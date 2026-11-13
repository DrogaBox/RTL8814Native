# Byte-by-byte review of the TX action frames of `RTLAwdInterface`

Independent review of `buildAf`, `putChanSeq`, `txTick` and `alignClock`
(`Source/AirLayer/AWDLInterface.cpp`, commits `cceb743` and `7e454be`): every offset, length,
endianness and value is contrasted against three references: owly (`~/Desktop/AWDLLab/refs/owly`,
`docs/SPEC.md` §3 and `crates/owly-awdl/src/{tlv,channel,sync,frame}.rs`, which interoperate with
iPhones), the notes of the kernel parser (`AWDL_TLV_BODIES.md`, `AWDL_FRAME_WIRE_FORMAT.md`,
`AWDL_TLV5_ELECTION.md`) and the payload of event 41 (`AWDL_WINDOW_TIMER.md` §5.1,
`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2). What is not verifiable is marked `?`.

No code was touched, no kext loaded, no `sudo` run. The fixes are proposed, not
applied. IDA was not opened: everything contrastable with the listed documents is closed; the
points that would require disassembly remain as `?` in §6.

The line numbers `:NNN` refer to the working tree at the moment of the review, intermediate
between `ce0481b` and `ab11c69`, and match no commit: they are located by the name of the cited
function. The status of each finding after the review is in §11.

## 1. Verdict

The three rendered TLVs (4, 5, 18), the 802.11 header, the fixed header, the
template→TLV 5 mapping, the clock arithmetic and event 41 are byte by byte correct against the
three references. The hand-built PSF of §5 reproduces exactly what `buildAf` emits and
the parse rules of owly accept it without errors. No on-air format defect was
found. There is one race defect that can corrupt the phase bytes of a
frame (H1), two minor differences with owly in unnormalized values (H2, H3) and five
interop/robustness observations (H4–H8).

## 2. 802.11 header and AWDL fixed header

`buildAf` `AWDLInterface.cpp:921-938`; constants `:601-618`, `:620`.

| Field | Code | Reference | Status |
|---|---|---|---|
| FC | `0xD0` at `+0`, `+1..+3` zeroed (duration included) | `frame.rs:37` `ACTION = 0x00d0` LE; `put_header` writes duration 0 (`frame.rs:271`) | OK |
| A1 | `ff×6` (`:925`) | broadcast, `action_frame` uses `MacAddr::BROADCAST` (`frame.rs:288`) | OK |
| A2 | `_mac` (`:926`) | AWDL-2: the MAC of `awdl0` | OK |
| A3 | `00:25:00:FF:94:73` (`:927`, `:620`) | AWDL-1 and `AWDL_RTL8814_RX_ACK.md` | OK |
| Seq | zeroed (`:922`) | the radio numbers it (`rtlRadioTx80211`, commit `4ac3340`); owly sets it at the sender, here the driver adds it | OK |
| Category/OUI/type | `7F`, `00 17 F2`, `08` (`:931-933`) | `actionFrameHasCorrectOUI` compares `+1..+4` (`AWDL_FRAME_WIRE_FORMAT.md` §2); owly `frame.rs:195-199` requires the same | OK |
| Version | `0x10` (`:609`, `:934`) | the kernel only requires ≠ 0; owly requires exactly `0x10` (`AWDL_VERSION_COMPAT`, `frame.rs:66`) | OK |
| Subtype | `0` PSF / `3` MIF (`:607-608`) | kernel: branches `==0`/`==3` (§2); owly `frame.rs:80-82` | OK |
| Reserved `+7` | 0 (`:922`) | SPEC AWDL-3 | OK |
| `phy_tx`/`target_tx` | `nowUs` LE32 at `+8`/`+12` (`:936-937`) | owly writes the same value in both (`frame.rs:295-296`); the kernel reads them in `parseAwdlSyncParamsTlv` and, being equal, `sync_contention` stays 0 (`AWDL_TLV_BODIES.md` §1.3); unit µs per `AWDL_FRAME_WIRE_FORMAT.md` §5.4 | OK |

The TLVs start at `+16` of the fixed header (`AWDL_FRAME_WIRE_FORMAT.md` §2), which is where
`findTlv` (`:653-665`) and `alignClock` (`:1004`) look for them. TLV header `{u8 type, u16 len LE}`
(`:941-942`, `putLe16`) = `AWDL-4` and §3 of the format doc.

## 3. TLV 4 (sync params) and channel sequence

`buildAf` `AWDLInterface.cpp:940-961`; `putChanSeq` `:636-650`; `AWDL_SYNC_LEN = 33`, `AWDL_CHANSEQ_LEN = 38` (`:614-615`).

| Value offset | Field | Code | Kernel (`AWDL_TLV_BODIES.md` §1.2, TLV offsets = value+3) | owly (`tlv.rs:368-392`) | Status |
|---:|---|---|---|---|---|
| len | 73 | `:942` | ≥ 39 required; `0x2A` of cursor (76) ≥ 42 | test `owls_tlv_lengths` expects 73 | OK |
| 0 | `tchan` | `channel` (`:944`) | `+3`, to `peer+756/+1432` | `own.channel` | OK |
| 1–2 | `dc` | `toSlot` LE (`:945`) | `+4`, ≤ 64 | `next_slot_tu` ∈ [1,64] (`sync.rs:56-58`) | OK; formula identical to `64 − (tu−base)/1024 mod 64` (`AWDL_WINDOW_TIMER.md` §2) |
| 3 | `mchan` | `channel` (`:946`) | `+6`, trace only | `own.master_channel` | OK¹ |
| 4 | guard | 0 (`:947`) | not read | 0 | OK |
| 5–6 | AW period | 16 (`:948`) | `+8` → `peer+762`; the air measures 16 | `AW_PERIOD_TU` = 16 | OK |
| 7–8 | AF period | 110 (`:949`) | `+10` → `peer+764`; air 110 (iPhone 440 in MIF) | `psf_interval_tu` | OK |
| 9–10 | flags | `0x1800` (`:950`) | not read; air `0x1800` | `0x1800` | OK |
| 11–12 | AW ext | 16 (`:951`) | `+14`; ≠ 16 ⇒ "Zero AWExt" | `AW_PERIOD_TU` | OK |
| 13–14 | AW common | 16 (`:952`) | `+16` | `AW_PERIOD_TU` | OK |
| 15–16 | rem | `awLeft` LE (`:953`) | `+18` → `peer+904` | remaining rem | OK; `16 − tu mod 16` ∈ [1,16] |
| 17–20 | ext ×4 | 3 (`:954`) | `+20..+23`; `+20` → `peer+1452`; air `03 03 03 03` | `PRESENCE_MODE−1` = 3 | OK |
| 21–26 | master | `e2+3` (TLV 24 value 0) (`:919,:955`) | `+24..+29` → `peer+600/+604` | `own.election.master` = root address | OK² |
| 27 | `pm` | 4 (`:956`) | `+30`; 0 ⇒ parse failure | `PRESENCE_MODE` = 4 | OK |
| 28 | reserved | 0 (`:957`) | `+31` not read | 0 | OK |
| 29–30 | `sn` | `aw` LE (`:958`) | `+32` → `peer+106`; the kernel masks with `0xFFC0` | `own.aw_counter` = current AW | OK³ |
| 31–32 | AP alignment | 0 (`:959`) | `+34` not read | `own.aw_counter` | H3 |
| 33–70 | chanseq | `putChanSeq` (`:960`) | `tlvCheckBounds`: value+33 ≤ 0x1F and value+34 ≤ 3 | `own.sequence.put` | OK |
| 71–72 | padding | 0 (`:922`) | — | 2 zeros | OK |

¹ `mchan` should be the master channel (ioctl 132), not that of the radio; they coincide while the
layer follows its own schedule or a master on its same channel (fixed radio). If `alignClock`
followed a master on another channel, the announcement would be short (H5).
² owly puts its `Election.master` into `+21..+26`, which `parse_v2` takes from offset 0 of TLV 24
(`tlv.rs:160`); the code uses the same offset (`e2+3`), and in the kernel template that offset
is `root_mac` (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11). Consistent in the three sources.
³ `sn` and the `AwNumber` of event 41 come out of the same `aw` (`:913/:958` versus `:825`), as
`AWDL_PSF_MIF_EMISSION.md` §5.1 requires.

### 3.1 Embedded sequence and TLV 18

`putChanSeq` writes `{15, 3, 0, 3, FF FF}` and 16 entries of 2 B (`:640-649`). Contrast:

- Kernel (`AWDL_TLV_BODIES.md` §2.1, TLV offsets): `+3` count−1 (< 0x20), `+4` encoding (3 =
  pairs {channel, operating class}), `+5` duplicate, `+6` step, `+7..+9` fill, `+9` 2 B entries;
  `2·n ≤ len_cursor − 9`. With len 41: 32 ≤ 44−9 = 35. OK.
- owly (`channel.rs:188-211`): only accepts the strict shape 15/enc/dup 0/step 3/fill `0xffff`;
  opclass entries `[channel, opclass]` (`channel.rs:52-55`). The code matches byte by byte and the
  3 B padding inside the value (len 41) is what owly emits (`tlv.rs:410-415`, test expects
  41) and what the iPhone captures measure (41 B, `AWDL_REFERENCIAS_EXTERNAS.md` §4.2). OK.
- `awdlOpClass` (`:634`): 81 on 2.4 GHz, 128 on 5 GHz = `Channel::opclass` (`channel.rs:47-49`,
  0x51/0x80). OK. Unserved slots `{0, 0}`: same pattern as owly (`Sequence::empty`) and as the
  iPhone (channel 0, §4.2). OK (see H8 on the kernel conversion of 128 and of 0).
- Fixed encoding/step/fill instead of copied from the 129 (`p[6]`/`p[7]` are ignored at
  `:699-709`): owly rejects any other shape, so fixing them is the interoperable choice; it differs
  from the keep-them advice of `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7.3. H6.

## 4. TLV 5 (election v1) from the TLV 24 of the template

`buildAf` `AWDLInterface.cpp:963-975`; `findTlv(_tpl, _tplLen, 24, 28)` at `:918`.

The TLV 24 of the template of the 151 measures 40 B of value (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11:
root_mac `+3`, master_mac `+9`, rootLFC `+15`, hopCount `+19`, rootMetric `+23`, selfMetric
`+27`, electionId `+31`, selfLFC `+39`, all TLV-relative) and equals the SPEC layout
(value offsets 0/6/12/16/20/24/28/36).

| TLV 5 value | Code | TLV 24 source | owly v1 (`tlv.rs:395-407`) | Captures (`AWDL_TLV5_ELECTION.md` §3.2/§4) | Status |
|---:|---|---|---|---|---|
| 0 flags | 0 (`:922`) | — | 0 | always 0 | OK |
| 1–2 ID | 0 | — | 0 | always 0 | OK |
| 3 distance | `(u32)(e2+3+16)` (`:968`) | hopCount (value 16) | `e.height.min(255)` | dist 0 master / 1 follower | H2 |
| 4 reserved | 0 | — | 0 | 0 | OK |
| 5–10 root | `memcpy(e2+3, 6)` (`:969`) | root_mac (value 0) | `e.master` | root MAC | OK |
| 11–14 root metric | `memcpy(e2+3+20, 4)` (`:970`) | rootMetric (value 20) | `e.master_metric` LE | 525/65/60 | OK |
| 15–18 own metric | `memcpy(e2+3+24, 4)` (`:971`) | selfMetric (value 24) | `e.self_metric` LE | 525/65/60 | OK |
| 19–20 padding | 0 | — | 2 zeros | `00 00` | OK |

Fixed len 21 (`:965`): the air only uses 21 and owly emits it so. OK. Emitting the 5 next to the 24 is what
owly (`put_all`) and the Apple firmware do (the captures carry it in PSF and MIF); the
receiver seeing the 24 ignores the 5 (SPEC, §3.2). The branch without TLV 24 (`:972-974`) leaves metrics at
0; it is only reachable with a template without TLV 24, which the manager does not produce per §3.11 `?`.

## 5. Template of the 151, order and sizes

- Read of the 151: `len` at `+4` and pointer at `+8` (`:671-672`) = 16 B struct of
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11. Own buffer of 512 B (`AWDLInterface.h:440`), like the
  kernel one. Copy under `_txLock` (`:678-684`). OK.
- The template goes whole at the end of the frame (`:983`): the resulting order is
  `4, 5, 18, 6, 24, 12, 7, 17, 21, …` — exactly the order measured in the iOS 26 PSFs and the one
  `AWDL_PSF_MIF_EMISSION.md` §4 prescribes (insert 4/5/18 in front of the first TLV of the
  template, without final padding). OK.
- `need` (`:906-907`) = 24+16+76+24+44+tplLen = 184+tplLen ≤ 184+512 = 696 < `AWDL_AF_MAX` 1024;
  `bzero(out, need)` covers exactly the frame, so the internal paddings (2 B of TLV 4,
  3 B of TLV 18) stay zero without an extra write. OK.
- MIF = template of the 151 with subtype 3: it lacks TLV 16 (ARPA, which the kernel only composes in
  the primary MI, `AWDL_PSF_MIF_EMISSION.md` §3.2) and TLV 2, and the 6 travels in PSF position.
  Neither the kernel nor owly requires it to validate the sender (AWDL-17 asks MIF + TLV 21 nonzero, and the
  21 comes in the template); the host name does not travel in AF. Already marked `?` in the source
  (`:599`). H4.

## 6. Clock arithmetic and `alignClock`

Constants (`:601-606`): TU 1024 µs, AW 16 TU, PM 4, slot 64 TU, 16 slots, PSF 110 TU — equal to
`sync.rs:12-20` and to the measured air.

`txTick` (`:767-868`):

- `awIdx = (now − _clkBaseUs + 500) / 16384` (`:791`): the 500 µs margin only rounds up a tick falling
  right before the boundary; `thread_call_enter_delayed` does not fire early, so it is harmless. `aw = (u16)(_clkBaseAw + awIdx)` (`:792`): u16 with overflow,
  like the `AwNumber` of event 41. OK.
- `slot = (aw / 4) % 16` (`:794`) = `(aw >> 2) & 0xF` of `AWDL_WINDOW_TIMER.md` §2 and = `min(15,
  (awNumber % 64)/4)` of the manager (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2). OK.
- PSF every 110 TU with lateness recovery (`:812-816`), same as `node.rs:566-572`; MIF in the
  third AW of each served slot (`aw % 4 == 2`, `:807`) = half of the owly slot
  (`node.rs:574-578`, `SLOT_US/2`). OK.
- Window: opens with 41 only in the first AW of a served slot (`:803-806`), closes with 43 at the
  boundary of the next (`:799-801`) — the window covers the whole served slot, as `AWDL_WINDOW_TIMER.md` §5.3
  asks, and the 43 precedes the 41. Without a 129 (`_seqValid` false) all slots are
  served (`:876-877`), preceded by the kernel (roam sequence with all slots on the same channel). OK.

`alignClock` (`:994-1023`):

- Reads `dc` at value+1 and `sn` at value+29 of TLV 4 (`:1007-1008`), with `t+3+21 == A2` as the
  condition of "it is its own master" (`:1005`): same offsets as the parsing of §3. The clamp
  `toSlot ∈ [1, 64]` (`:1009`) matches `next_slot_tu ∈ [1..64]` of owly.
- Realign: `into = 64 − toSlot`; `_clkBaseUs = now − into·1024`; `_clkBaseAw = aw − into/16`
  (`:1015-1017`). It is equivalent to `Clock::update` (`sync.rs:93-97`): owly rounds
  `aw_counter & !3` downward and the code subtracts the whole elapsed AWs; for a
  master whose slots start at aw ≡ 0 (mod 4) both produce the same pair (base, slot-start AW). OK.

## 7. Event 41 (and 43)

`txTick` `AWDLInterface.cpp:817-833` and `:839-848`; 36 B payload (`ev41[36]`, `:782`).

| Offset | Code | `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2 / `AWDL_WINDOW_TIMER.md` §5.1 | Status |
|---:|---|---|---|
| `+0x00` | `(u16)aw` native (`:825`) | `AwNumber` u16; when opening only at `aw % 4 == 0`, it is the first AW of the slot (multiple of 4), as §4 of the windows doc requires | OK |
| `+0x04` | `memcmp(sync,_mac) ? 2 : 1` u32 (`:826`) | state 1 Master / 2 Slave; `sync` is the second MAC of TLV 24 (value+6), the `is_master` criterion of owly (`election.rs:62-64`) | OK |
| `+0x08` | `sync` = TLV 24 value+6 (`:823,:827`) | MAC of the followed master (that of the 134); in the kernel template that field is `master_mac` (§3.11) | OK |
| `+0x0e` | `top` = TLV 24 value+0 (`:822,:828`) | MAC of the root master; in the template it is `root_mac` | OK |
| `+0x14` | `OSSwapHostToBigInt32((u32)awStart)` (`:829`) | `FwAwStartTime` big-endian; when opening at `aw%4==0`, `awStart` is the start of the first AW = start of the slot | OK |
| `+0x18` | `_seqFlags[slot]` of the 129, else `0x8`/`0x10` (`:830-831`) | flags of the entry of the 129 (`+0x08` of the 12 B entry, §3.7); the default values 8 (2.4 GHz) / 16 (5 GHz) are the kernel convention (`AWDL_TLV_BODIES.md` §2.1) | OK |
| `+0x1c` | `ch` u32 native (`:832`) | channel number | OK |
| `+0x20` | 0 (`bzero`) | window flags (0) | OK |

Event 43: 40 B zeroed (`:840-841`), the shape omitting the CCA branch (`AWDL_WINDOW_TIMER.md`
§5.3). The slot index the manager deduces from the 41 matches `slot` (§6). The read of the 129
(`:699-717`: channel at `+4` and flags at `+8` of each 12 B entry from `+0x0c`, `n` at `+5`)
matches `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7. OK.

## 8. Example PSF and acceptance by the owly rules

Parameters: A2 `56:0d:25:xx:xx:xx`, channel 149, invented 60 B template (TLV 24 of 43 B with
root = own MAC, metrics 60; TLV 6 of 12 B; TLV 21 of 5 B), mask 0xFFFF (all slots served, state without a
129), clock with `_clkBaseUs = 0`, `_clkBaseAw = 0` and `nowUs = 6 574 080`
(`0x00645000`). With that: `tu = 6420`, `aw = 401`, `toSlot = 44`, `awLeft = 12`, `slot = 4`.

```
000  d0 00 00 00 ff ff ff ff  ff ff 56 0d 25 xx xx xx   FC+dur; A1 broadcast; A2
010  00 25 00 ff 94 73 00 00  7f 00 17 f2 08 10 00 00   A3 BSSID; seq=0; 7F,OUI,08,ver 1.0,sub 0,rsvd
020  00 50 64 00 00 50 64 00  04 49 00 95 2c 00 95 00   phy=target=6574080 µs; TLV4 len 73; tchan 149; dc 44; mchan 149; guard 0
030  10 00 6e 00 00 18 10 00  10 00 0c 00 03 03 03 03   AW 16; AF 110; 1800; ext 16; common 16; rem 12; ext 03×4
040  56 0d 25 xx xx xx 04 00  91 01 00 00 0f 03 00 03   master; pm 4; rsvd; sn 401; AP align 0; seq: 15,enc 3,dup 0,step 3
050  ff ff 95 80 95 80 95 80  95 80 95 80 95 80 95 80   fill FFFF; entries {149,128}
060  95 80 95 80 95 80 95 80  95 80 95 80 95 80 95 80
070  95 80 00 00 05 15 00 00  00 00 00 00 56 0d 25 xx   seq end; padding 2; TLV5 len 21; flags/ID/dist/rsvd 0; root
080  xx xx 3c 00 00 00 3c 00  00 00 00 00 12 29 00 0f   metrics 60/60; padding; TLV18 len 41; 15,enc 3,dup 0,step 3
090  03 00 03 ff ff 95 80 95  80 95 80 95 80 95 80 95   fill FFFF; entries {149,128}
0a0  80 95 80 95 80 95 80 95  80 95 80 95 80 95 80 95
0b0  80 95 80 95 80 00 00 00  18 28 00 56 0d 25 xx xx   seq end; padding 3; TLV24 len 40; root
0c0  xx 56 0d 25 xx xx xx 00  00 00 00 00 00 00 00 3c   master; counter 0; distance 0
0d0  00 00 00 3c 00 00 00 00  00 00 00 00 00 00 00 00   root metric 60; own metric 60; electionId 0
0e0  00 00 00 06 09 00 00 00  00 00 00 00 00 00 00 15   selfLFC 0; TLV6 len 9 (zeros)
0f0  02 00 34 01                                       TLV21 len 2: version 3.4, class 1
```

244 B in total. Validation against the owly rules (exact replica of `frame.rs:139-214`,
`tlv.rs:56-88` and `:308-337`, `channel.rs:188-211` run at `/tmp/awdl_psf_check/psf_check.py`;
zero errors):

- `frame.rs:parse`: FC management (`0x00D0 & 0x000C == 0`), no protected/fragments, `fc & 0x00FC
  == 0x00D0` (action), body at `+24`; `action()`: category 127, OUI `00:17:F2`, type 8, version
  exactly `0x10` (`AWDL_VERSION_COMPAT`), subtype 0 = PSF; `phy_tx`/`target_tx` = `0x645000`.
- `tlvs()`: exact walk, no leftover tail — TLVs `4(73), 5(21), 18(41), 24(40), 6(9), 21(2)`
  sum 204 = 244 − 24 − 16.
- `SyncParams::parse` (tlv.rs:109-121): tchan 149, next_tu 44, aw_period 16, af_period 110,
  master `56:0d:25:xx:xx:xx`, presence 4, aw_counter 401.
- `Sequence::parse` (channel.rs:188-211): strict shape 15/3/0/3/0xFFFF accepted; 16 slots of
  channel 149; the 3 B padding is ignored.
- `ElectionParams::parse_v1` (dist 0, own root, metrics 60/60) and `parse_v2` (own master/sync
  metrics 60/60): the v2, arriving after the v1, wins in `Advert::parse` the same as in the code.
- `parse_version`: (0x34, 1), version and class non-null → with a custom MIF it would satisfy AWDL-17.

The same checks of the kernel side (`AWDL_FRAME_WIRE_FORMAT.md` §1-4,
`AWDL_TLV_BODIES.md` §1-2): valid OUI/type/version/subtype, TLV 4 with 76 B cursor ≥ 42 and
presence ≠ 0, `dc` 44 ≤ 64, `aw_ext` = 16, embedded sequence with count−1 = 15 ≤ 0x1F and
encoding 3 ≤ 3, TLV 18 with cursor 44 ≥ 9 and 2·16 ≤ 44 − 9, TLV 5 with len 21 within the
21..37 that `parseAwdlElectionParamsV1` accepts.

## 9. Findings

| # | Severity | Where | What | Proposed fix |
|---|---|---|---|---|
| H1 | minor (corrupts one frame) | `AWDLInterface.cpp:912` | `buildAf` runs outside the first locked segment of `txTick` (`:835` releases, `:850-853` calls): if `alignClock` realigns the clock between the sampling of `nowUs` (`:784`) and the construction, `nowUs − _clkBaseUs` can go negative and, being `u64`, overflow: `tu`, `aw`, `toSlot` and `awLeft` come out as garbage and the frame carries a false phase. It happens when the realignment lands with `into·1024 µs` smaller than the RX latency (`into = 0`, `toSlot = 64`, the case of a PSF of the master on its own slot boundary). Not measured on hardware `?` | Saturate in `buildAf`: `uint64_t delta = nowUs > _clkBaseUs ? nowUs - _clkBaseUs : 0;` and derive `tu` from `delta`. Requires user OK |
| H2 | minor (robustness) | `AWDLInterface.cpp:968` | The distance is truncated to `u8` (`(uint8_t)u32`): a hop ≥ 256 would be announced as 0 (= master). owly saturates (`height.min(255)`, `tlv.rs:400`). The manager does not produce such hops `?` | Saturate at 255 instead of truncating. Requires OK |
| H3 | observation | `AWDLInterface.cpp:959` | AP alignment (`+31..+32`) at 0; owly repeats `aw_counter` there (`tlv.rs:388`). No reader in the kernel nor in owly; no known interop impact `?` | None needed |
| H4 | observation | `AWDLInterface.cpp:850-853`, `:599` | The MIF is built over the template of the 151: no TLV 16 (ARPA) nor TLV 2, and with the 6 in PSF position, versus the Apple MIF (`…,17,16,21,6,2…`). The kernel and owly accept it; AWDL-17 is still satisfied. Already marked `?` in the source | Add ARPA when a host name is available |
| H5 | observation | `AWDLInterface.cpp:946` | `mchan` copies the channel of the radio; the canonical source is ioctl 132 (channel of the master). Equivalent while the master is on the radio channel; if a master on another channel were followed, it would be wrong | Copy the `mchan` of TLV 4 of the master in `alignClock` |
| H6 | observation | `AWDLInterface.cpp:640-649`, `:699-709` | Encoding 3, step 3 and fill `0xFFFF` fixed; the 129 carries those values (`p[6]`/`p[7]`) and `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7.3 asks to keep them. owly only accepts the fixed shape and the captures always use it, so fixing it is the interoperable choice; the other fields of the 129 are indeed copied | None needed; documents the deviation from §7.3 |
| H7 | observation | `AWDLInterface.cpp:994-1023` | `alignClock` follows any node announcing itself master of itself (TLV 4 `+21 == A2`); owly only aligns with its `election.sync` (`node.rs:433-443`) and `AWDL_WINDOW_TIMER.md` §3.2 speaks of the elected master (229/134). With two autonomous masters in range, the clock would jump between schedules | Limit the realignment to an elected master (e.g. the first stable one or that of the last 229). Requires OK |
| H8 | `?` out of TX scope | `AWDL_TLV_BODIES.md` §2.1 | `convertOpClassToChannel` of the kernel: it is not known to accept 128 (40 MHz) nor the pair `{0,0}` with encoding 3; a conversion failure invalidates the whole TLV (`-1`). For TX only the parsing of the pair matters (iPhones: accepted by owly and by its own captures with encoding 3 and slots at 0); for the project's own RX (parsing of the peer frames by the manager) it is unverified | Verify in IDA both conversions before trusting the RX to real peer TLVs |

## 10. Not verified

- The behavior of `convertOpClassToChannel` with 128 and with `{0,0}` (H8).
- The real frequency of H1 on hardware (USB RX latency versus `into`).
- That the manager does not produce election hops ≥ 256 (H2) nor templates without TLV 24 (§4).
- Exact unit of `phy_tx`/`target_tx` beyond the host µs `monitorTimeline` expects
  (`AWDL_FRAME_WIRE_FORMAT.md` §5.4 takes it as good; the docs mark it `?`).

## 11. Status after the review

| # | Status |
|---|---|
| H1 | Confirmed in the code and fixed in `2bb1c45`: `buildAf` takes the time under `_txLock` and `txTick` takes it after the lock. The same overflow existed in `txTick` (`now` sampled before `IOLockLock`) and is fixed in the same commit |
| H2 | Fixed in `28b58e8`: the distance of election v1 saturates at 255 |
| H3, H4, H5, H6 | No change |
| H7 | Fixed in `28b58e8`: `alignClock` only follows the sync master of the election v2 of the template (value+6 of TLV 24) when it is another node; otherwise the first master is taken while it is heard for 2 s |
| H8 | Pending IDA |
