# Operating class conversion and the TLV 18 parser (closure of H8)

It closes H8 of `AWDL_TX_FRAME_REVIEW.md`: the exact behavior of the kernel's `convertOpClassToChannel`,
the call chain from the TLV 18 parser (channel sequence), the effect of
a conversion failure, encodings 0/1/3 and the acceptance of a peer advertising only
some served slots. Source: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, 25G241,
Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`), annotated database
`~/re_cache/ida/boot_awdl.kc.i64` (copy in `Build/ida_awdl/task_tlv18/kc.i64`). Anything unobserved
is marked `?`. Complements `AWDL_TLV_BODIES.md` (§2), which it corrects at two points (§4 and §3.2).

## 1. Short answers

| Question | Answer |
|---|---|
| Which operating classes are accepted? | Exactly the 24 of `operClassTable`: 81–84, 115–134 (`0xffffff80023ab450`). Any other class byte (including 0) ⇒ conversion fails ⇒ TLV 18 rejected. |
| 128 and `{0,0}`? | 128 yes (entry 17, 80 MHz, centers `{42,58,106,122,138,155}`). `{0,0}` yes: channel 0 never gets converted (`0xffffff80021b5ba1`). |
| Conversion failure? | The parser returns `-1` and logs `"fail convert op class, op class 0x%X"`. On the main pass (`actionFrameReport`) the peer's sequence is not applied and the frame is not discarded; on the minimal pass (`doMinimalAFProcessing`) the truncated sequence does get applied (§3.4, rarity). |
| Partial peer at 149 accepted? | Yes (§5): `{149,128}` slots valid, `{0,0}` without effect; overlap and common channel are computed by the manager per entry (band mask `0x2018`), and channel 0 never counts as overlap. |

## 2. `convertOpClassToChannel`

Symbol `_convertOpClassToChannel` (`0xffffff800223ca18`), classless (exported flat function).
Observed signature:

```c
int convertOpClassToChannel(u8 opClass, u64 chanBitmap, u32 centerBitmap,
                            char *outChan, u8 *outBW, u32 *outBand);
```

- Returns `0` if the class exists in the table and `0xE00002BC` (`kIOReturnError`) if not
  (`mov eax, 0E00002BCh` at `0xffffff800223ca5c`). There are no more error codes.
- It looks up the entry by walking `operClassTable` with step 70 and comparing byte 0
  (`cmp dil, [rbx+r11]` at `0xffffff800223ca34`; 24 entries, `cmp r10, 18h` at
  `0xffffff800223ca56`). With `chanBitmap = 0` (no bits) it continues to the next entry
  (`0xffffff800223ca40`–`0xffffff800223ca4d`).
- Outputs: `outChan = entry.channels[bit]` (`mov al, [rax+r14]; mov [rcx], al` at
  `0xffffff800223ca66`–`6a`, with `rax = 0xffffff80023ab456 = operClassTable+6`), `outBW =
  entry.bw` (`0xffffff800223ca6c`), `outBand = *(u16*)(entry+4)` (`0xffffff800223ca73`).
- With `centerBitmap` carrying bits (only relevant for 80/160 MHz) it replaces `outChan` with the corresponding
  center from the `bw80MHz` (`0xffffff80023abae0`), `bw160MHz5g`
  (`0xffffff80023abbf0`), `bw40MHz6g` (`0xffffff80023abb00`), `bw80MHz6g` (`0xffffff80023abb60`)
  and `bw160MHz6g` (`0xffffff80023abbb0`) tables. The AWDL parser always calls it with `centerBitmap = 0`,
  so the output channel stays untouched.

### 2.1 `operClassTable` (24 entries × 70 bytes, `0xffffff80023ab450`)

Layout per entry: `+0` class, `+1` width (0=20, 1=40, 2=80, 3=160), `+4..+5` u16 band
(`0x2000` = 2.4 GHz, `0x1000` = 5 GHz, `0x4000` = 6 GHz), `+6..+69` list of 64 channels.
Dump verified byte by byte:

| Entry @VA | Class | BW | Band | channels[0..] (non-null) |
|---|---:|---:|---:|---|
| `...3ab450` | 81 | 0 | 0x2000 | 1–13 |
| `...3ab496` | 82 | 0 | 0x2000 | 14 |
| `...3ab4dc` | 83 | 1 | 0x2000 | 1–9 |
| `...3ab522` | 84 | 1 | 0x2000 | 5–13 |
| `...3ab568` | 115 | 0 | 0x1000 | 36 40 44 48 |
| `...3ab5ae` | 116 | 1 | 0x1000 | 36 44 |
| `...3ab5f4` | 117 | 1 | 0x1000 | 40 48 |
| `...3ab63a` | 118 | 0 | 0x1000 | 52 56 60 64 |
| `...3ab680` | 119 | 1 | 0x1000 | 52 60 |
| `...3ab6c6` | 120 | 1 | 0x1000 | 56 64 |
| `...3ab70c` | 121 | 0 | 0x1000 | 100–144 |
| `...3ab752` | 122 | 1 | 0x1000 | 100–140 (centers) |
| `...3ab798` | 123 | 1 | 0x1000 | 104–144 (centers) |
| `...3ab7de` | 124 | 0 | 0x1000 | 149 153 157 161 |
| `...3ab824` | 125 | 0 | 0x1000 | 149 153 157 161 165 |
| `...3ab86a` | 126 | 1 | 0x1000 | 149 157 |
| `...3ab8b0` | 127 | 1 | 0x1000 | 153 161 |
| `...3ab8f6` | 128 | 2 | 0x1000 | 42 58 106 122 138 155 (80 centers) |
| `...3ab93c` | 129 | 3 | 0x1000 | 50 114 (160 centers) |
| `...3ab982` | 130 | 2 | 0x1000 | 42 58 106 122 138 155 |
| `...3ab9c8` | 131 | 0 | 0x4000 | 1, 5, … 233 (6 GHz, step 4) |
| `...3aba0e` | 132 | 1 | 0x4000 | 3, 11, … 227 (6 GHz 40 centers) |
| `...3aba54` | 133 | 2 | 0x4000 | 7, 23, … 215 (6 GHz 80 centers) |
| `...3aba9a` | 134 | 3 | 0x4000 | 15, 47, … 207 (6 GHz 160 centers) |

The width also comes out of `_convertOpClassToBandNBW` (`0xffffff800223cc49`, same table, only
`+1`/`+4`). The `bw*` width tables use the output channel as key and a 4- or 8-bit
bitmap (3rd argument) to pick the center; with the bitmap at 0 they modify nothing.

### 2.2 Siblings and callers

- `_convertChannelToOpClass` (`0xffffff800223d15d`): wrapper of
  `convertChannelToOpClassWithBand` that deduces the band from the channel (`(u8)(ch-1) < 14` ⇒ adds
  `0x1000` to base `0x1000`, i.e. 2.4 GHz). No xrefs in this kernelcache `?`.
- `_convertChannelToOpClassWithBand` (`0xffffff800223cc82`): inverse conversion (channel+width+band
  → class + bitmaps); used by `buildPrefChanTlv` (`0xffffff8002211516`, `0xffffff80022117b8`) to
  emit the manager's TLV 33, and by `isChannelOpClassAvailableWithBand` (`0xffffff800223d193`).
- `_convertOpClassToNanChannel` (`0xffffff800223d06c`): delegates to
  `convertOpClassToChannel` except for classes 131–134 (6 GHz NAN table
  `nan6GoperClassTable`, `0xffffff80023abc10`); no AWDL role.
- AWDL callers of `convertOpClassToChannel`: only `parseAwdlChanSeqFromChanSeqTLV`
  (`0xffffff80021b5bed`) and `parseAwdlPrefChanTLV` (`0xffffff80021b67cc`); the rest are NAN
  (`IO80211NANPeerManager::calculateNANChannelUtilization`, `0xffffff80022b3a24`) and
  `convertOpClassToNanChannel` itself (`0xffffff800223d0a3`).

## 3. Call chain of TLV 18 and the effect of a failure

### 3.1 Walk of a received AWDL frame

`IO80211AWDLPeerManager::actionFrameProcessInput` (`0xffffff800220ed31` takes the peer and retains it)
calls `IO80211AWDLPeer::actionFrameReport` (`0xffffff80021af6be`), which makes **two passes** over
the TLVs of the same frame:

1. `doMinimalAFProcessing` (`0xffffff80021b3ea6`, called at `0xffffff80021b013f`, before the
   main loop — the return to the loop is at `0xffffff80021b1387` toward `0xffffff80021b033f`).
   It walks the TLVs with the **raw types** (`switch (v18)` over byte 0 of the TLV) and handles
   4, 18, 32, 24 (election v2), HT caps…; each case is gated by bit 0 of byte
   `+0x57` of the context (e.g. `test byte ptr [r12+57h], 1` at `0xffffff80021b414b`). Exact
   semantics of that indicator `?` (it is set to 0 in `actionFrameReport` when there is no UMI,
   `0xffffff80021b02a0` region; the bit gates election v2 and TLV 12 too per
   `AWDL_OPEN_QUESTIONS_1.md`).
2. Main loop: `tlvCheckBounds` per TLV (`0xffffff80021b03ad`) and a `switch` with bias −2
   (`add r15d, 0FFFFFFFEh` at `0xffffff80021b03ce`). The jump descriptor in the database
   gives 34 cases, table at `0xffffff80021b2eb0` (4 B per entry, target = base + displacement)
   and `lowcase = 2`: the table index is type − 2 and the targets come from the
   `switch_info` dump. The disassembly's case labels cite the **real type** (index + 2):
   case 18 = TLV 18 (`0xffffff80021b0bf7`, table index 16), case 32 = TLV 32
   (`0xffffff80021b0b37`, index 30), case 33 = TLV 33 pref-chan (`0xffffff80021b0c77`, index
   31), case 4 = sync params (`0xffffff80021b0606`, index 2). This re-verifies the note of
   `AWDL_TLV_BODIES.md` about the −2 bias and resolves the case correspondence.

The TLV 18 case in the main loop (`0xffffff80021b0bf7`, index 16 of the table): if variable
`var_120` is already 1 (a TLV 32 appeared before) the 18 is ignored (`0xffffff80021b0bfa`
–`0xffffff80021b0c0f`); if not, it requires cursor ≥ 9 (`cmp r10, 9` at `0xffffff80021b0cc5`) and an
allocated buffer (`cmp [rbp+var_A0], 0` at `0xffffff80021b0ccf`) and calls
`parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b0cf2`). On success it sets `var_148 = 1`
(`0xffffff80021b0d01`) and `var_120 = 0` (`0xffffff80021b0d07`); the three indicators are initialized
to 0 before the loop (`0xffffff80021b02b5`/`02bf`/`02c9`).

At the end of the loop (`LABEL_291`, gate at `0xffffff80021b14ec`) **one** of the
parsed sequences is applied with `updateAwdlChanSeq` (`0xffffff80021b1520` for TLV 18/32,
`0xffffff80021b15ff` for the variant embedded in sync params, gate `var_144` at
`0xffffff80021b15de`):

- TLV 18 or TLV 32 with valid parsing → own 392 B buffer (`var_148 | var_120`).
- If not, and sync params brought a valid embedded sequence → its buffer.
- If neither, it is not called.

A failure of `updateAwdlChanSeq` (≠ 0) only logs `"Error: updating ch sequence from %d,
error: %d"` (line 3161, `0xffffff80021b1953`) and continues; the TLV's pass is annotated in the
context's field `+0x2C` (`0xffffff80021b19ca`) and the frame is not discarded.

### 3.2 Prior limits

- `tlvCheckBounds` (`0xffffff80021aefa0`), cases 18 and 32 (indices 18/32 of the unbiased table,
  `lowcase = 0`): minimum 6 and maximum 70 bytes of cursor (`mov edi, 6; mov esi, 46h` at
  `0xffffff80021aefdf`). With cursor = total TLV length (3 B header included), the maximum 70 leaves `9 + 2·n ≤ 70` ⇒ **n ≤ 30** in practice.
- The manager's `isValidChanSeqTlv` (`0xffffff8002214b76`): cursor ≥ 9, TLV length ≥ 6,
  `count−1 ≤ 0x1F` and `count−1 ≤ (payload−3−2)/2` (`0xffffff8002214bc9`–`...4bdc`); it is the
  coarse check on the manager side.
- Inside the parser: `count−1 < 0x20` (`cmp dil, 20h` at `0xffffff80021b5aac`), `2·n ≤ len−9`
  (`0xffffff80021b5a9a`) and `len ≠ 9` (`0xffffff80021b5aa5`). Violation ⇒
  `"Error: Assert failure"` (lines 6186/6206, `0xffffff80021b5d6c`/`0xffffff80021b5d8f`) and `-1`.

### 3.3 The parser, byte by byte

`IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b5a08`). TLV structure
(3 B header included): `+3` = n−1, `+4` = encoding, `+5` = dup, `+6` = step,
`+7..+8` = fill u16 LE, `+9` = n 2-byte entries. It copies the header into the 392 B
`awdl_chan_seq_info2` (`+0..+5`, `0xffffff80021b5b02`–`1e`) and per entry writes 12 B at `info2+8+12·i`:
`{u32 1, u32 channel, u32 flags}`.

- **Encoding 3** (`cmp byte ptr [r9+1], 3` at `0xffffff80021b5b61`): byte 0 = on-air
  channel, byte 1 = operating class. If the channel differs from 0 (`cmp byte ptr [r15], 0` at
  `0xffffff80021b5ba1`) it calls `convertOpClassToChannel(class, 1, 0, &chan_out, &bw_out,
  &band_out)` (`mov esi, 1; xor edx, edx` at `0xffffff80021b5bda`–`df`, `call` at
  `0xffffff80021b5bed`): channel bitmap `1` (first bit) and center bitmap `0`, i.e.
  **the entry's channel takes no part in the conversion**; the conversion only contributes width and
  band. Return ≠ 0 ⇒ `"fail convert op class, op class 0x%X\n"` (line 6268,
  `0xffffff80021b5ddf`–`07`) and `-1`.
- **Any other encoding** (`jnz 0xffffff80021b5c74`): only byte 0 (channel); byte 1
  is ignored except in the raw copy. No conversion call.
- Entry flags (branch 3, `0xffffff80021b5c1e`–`67`): `0x2000` if `band_out == 0x4000`;
  else `0x10` if `chan_out ∈ [36,165]` (`lea edx, [rcx-24h]; cmp dl, 82h` at
  `0xffffff80021b5c2f`–`3a`), `8` if `chan_out ∈ [1,14]` (`dec cl; cmp cl, 0Eh` at
  `0xffffff80021b5c3c`–`43`), and `0` otherwise; the width is added: `bw_out ∈ [1,3]` ⇒
  `dword_FFFFFF80023AA014[bw−1]` = {4, 1024, 2048} = {40 MHz, 80 MHz, 160 MHz}
  (`0xffffff80021b5c49`–`62`); `bw_out = 0` (20 MHz) ⇒ `2`. Flags of the non-encoding-3 branch
  (`0xffffff80021b5cb8`–`d8`), by on-air channel only: 0 → 0; [1,14] → 8; [15,35] → 0;
  [36,165] → 0x10; [166,255] → 0.
- The channel stored in the entry is **always the on-air byte**, never the converted one: the
  operating class only decides whether the entry is accepted and which flags it carries. There is no check
  that the channel belongs to the class: `{36,128}` or `{6,128}` are accepted with class 128's 5 GHz
  flags (`0xffffff80023ab8f6`, 80 centers).
- Raw copy of each entry's 2 B to the peer's private block (`[priv+0x558+2i]` and
  `+0x559+2i`, `0xffffff80021b5d09`–`48`; 0x558 = 1368, as `AWDL_TLV_BODIES.md` §2.1 already documented).
- Success: 0 after n entries (`xor eax, eax` at `0xffffff80021b5d68`).

### 3.4 What a conversion failure does to the TLV, the peer and the frame

- **TLV**: the whole parse returns `-1` at the failure point; the entries already written to the
  `awdl_chan_seq_info2` (and the header) remain written but the destination depends on the pass.
- **Main pass**: `var_148` stays 0 ⇒ `updateAwdlChanSeq` is not called for this TLV
  (§3.1). The peer's previous sequence stays intact. The frame is **not discarded**: the
  error is logged by the manager (`"Error: parsing ch sequence from 18, error: -1"`,
  `0xffffff80021b251a`–`5b`) and the rest of the TLVs are processed normally. If the same frame
  brings a valid TLV 32, the 32 wins (indicator `var_120`); if it brings sync params with a valid embedded
  sequence, the embedded one wins.
- **Minimal pass** (`doMinimalAFProcessing`): the "TLV 18 seen" indicator is set to 1
  **before** testing the result (`mov cl, 1; mov [rbp+var_A0], rcx` at
  `0xffffff80021b41c6`–`c8`, the `test eax, eax` is at `0xffffff80021b41cf`), and at the end of the
  walk `updateAwdlChanSeq` is called anyway (`0xffffff80021b45cb`→`0xffffff80021b45e8`),
  with a freshly allocated 392 B buffer via `IOMallocZeroData` (zero-filled). Therefore, with
  bit `ctx+0x57` active, a conversion failure **does apply** a truncated sequence: the
  entries before the failing one keep their channel and the rest stay at channel 0. An
  assert failure (short cursor) writes only the zeroed header (n = 0) and `updateAwdlChanSeq` exits with
  `-2` (`totalSlots = 0`, §3.4): the peer stops contributing overlap slots until the next valid
  AF. Observed rarity, not reproduced on hardware `?`; its real reach depends on the exact meaning of `ctx+0x57` (`?`).
- **Peer**: without failure, `updateAwdlChanSeq` (`0xffffff80021ab7e4`) copies header and entries to the
  peer's private block: `priv+0x38C` = encoding, `+0x38D` = min(n,16)
  (`cmovb` at `0xffffff80021ab8aa`–`b2`), `+0x38E` = step, `+0x38F` = dup, `+0x394` = fill u16,
  entries at `+0x398` (`{u32 1, u32 channel}`) and `+0x3A0` (flags) (`0xffffff80021ab96c`–`9a`).
  Returns: `-1` without buffer or n > 0x20 (`0xffffff80021ab89e`), `-2` if
  `totalSlots = (dup+1)·min(n,16)·(step+1)` falls in [513, 65023]
  (`imul`/`mov [priv+0x38A]` at `0xffffff80021ab93c`–`46`, `add esi, 0FFFFFDFFh; cmp edx, 0FE00h;
  jb -2` at `0xffffff80021ab94d`–`62`: valid if ≤ 512; the edge ≥ 65024 by modulo 2^16 is
  unreachable with dup/step ≤ 255 and n ≤ 32 except extreme combinations `?`), `-3` if the
  entry 0's channel is 0 (`cmp dword ptr [priv+39Ch], 0` at `0xffffff80021abbfe`, `mov r12d,
  0FFFFFFFDh` at `0xffffff80021abd5e`): with `-3` the entries were already copied and the later
  effects already ran, only the power-of-two computation
  (`priv+0x59C/0x5A0/0x5A4/0x5A8`) and the
  `cachedPeerHasChangedChannelSequence` notification (`0xffffff80021abd57`) are skipped.

## 4. Encodings 0, 1 and 3: bytes per slot and use of step/fill

In the **TLV 18** (and in TLV 32, which does not use this function) each slot occupies 2 bytes from `+9`,
whatever the encoding does; the index advances by 2 (`add rbx, 2` at
`0xffffff80021b5d57`):

| Encoding | byte 0 | byte 1 | Entry flags |
|---|---|---|---|
| 3 | on-air channel | operating class (must be in `operClassTable` if channel ≠ 0) | band+width of the **class** (§3.3) |
| 0, 1, 2, … | on-air channel | ignored | by channel: 0 → 0; 1–14 → 8; 15–35 → 0; 36–165 → 0x10; 166–255 → 0 |

The encoding is **not** validated (it is only compared with 3), and `dup` (`+5`), `step` (`+6`) and `fill`
(`+7..8`) are copied verbatim to `info2` and from there to the peer (`priv+0x38E/0x38F/0x394`). Their only observed
use: `totalSlots = (dup+1)·min(n,16)·(step+1)` at `priv+0x38A`, with the −2
sanity (§3.4), and the power-of-two computation of `priv+0x59C/0x5A0` for scheduling
(`0xffffff80021abc0b`–`c6e`). The parser was not seen expanding the sequence by `step+1`: the
logical expansion stays with the consumer, and per-slot overlap (§5) compares entries by
index 1:1, so both sequences (the peer's and the manager's own) must share the
same `step` convention to align. The reader of the peer's stored fill (`priv+0x394`) did not
appear in the walks `?`.

The **variant embedded in sync params** (`parseAwdlChanSeqFromSyncParamsTLV`,
`0xffffff80021b8196`; sub-TLV from `+36` of TLV 4) has different semantics and **does not call
`convertOpClassToChannel`**: encoding 0 = 1 byte per slot (channel, flags by channel);
encoding 1 = u16 LE with band mark (`& 0x300 == 0x100` ⇒ 6 GHz, channel = byte−2;
`== 0x200` ⇒ channel = byte+2 if `(byte & 0xFE) < 0xDE`; else low byte); encodings 2 and 3 =
{channel, ignored} pairs with flags by channel, no class conversion (`0xffffff80021b8470`
region of the `while (v15 != 1)` loop). This corrects the reading of `AWDL_TLV_BODIES.md` §2.1, which
attributed the "in width and band" conversion to the generic TLV 18 without distinguishing both variants and without
specifying the constant bitmap.

## 5. Peer advertising 149 (or 6) only on some slots and 0 on the rest

- **Acceptance**: yes. Entries `{149,128}` pass the conversion (class 128 in the table) and are
  stored as `{1, 149, 0x410}` (5 GHz + 80 MHz); with class 124 they would be `{1, 149, 0x12}`.
  The `{0,0}` ones are not converted and are stored as `{1, 0, flags}`. The 16-slot TLV measures
  41 B ≤ 70 and meets all limits; `totalSlots = 1·16·4 = 64 ≤ 512`.
- Rarity: a `{0,0}` slot's flags are **not zeroed**: they are computed with the leftovers of the
  last conversion of the same TLV (`var_31/32/38` on the stack, initialized to 0 at
  `0xffffff80021b5a2c`–`32` and rewritten only by `convertOpClassToChannel`): after a
  `{149,128}` they inherit `0x410`; if no slot converted before, they are `0x2` (0 of band | 2 of
  20 MHz). Since the channel is 0, it does not affect overlap or the channel vote (which require channel ≠ 0),
  but it does enter `updateAwdlChanSeq`'s change comparison `& 0x2018`
  (`0xffffff80021ab8ea`–`fe`).
- If the sequence's **first** entry is `{0,0}`, `updateAwdlChanSeq` returns `-3`
  (§3.4): the sequence is stored anyway but the peer does not notify the change to the manager
  (no `cachedPeerHasChangedChannelSequence`). Real sequences start with served slots, so in practice it does not happen.
- **Per-slot overlap**: `IO80211AWDLPeer::updateNumOfOverlapSlots` (`0xffffff80021abec2`,
  called from `updateAwdlChanSeq` at `0xffffff80021abaea` with `priv_manager+0xA774` as
  reference, `add rsi, 0A774h` at `0xffffff80021abae0` — the same address `setMyChanSeq` writes with
  ioctls 129/130, `0xffffff8002235302`) counts entries i where
  `peer_chan[i] == own_chan[i] ≠ 0` and `(peer_flags[i] ^ own_flags[i]) & 0x2018 == 0`
  (`mov edi, [priv+39Ch+12i]; cmp edi, [rsi+rdx+4]` at `0xffffff80021abeef`–`fa`; `xor edi,
  [priv+3A0h+12i]; test edi, 2018h` at `0xffffff80021abf04`–`0b`). Mask `0x2018` = band
  2.4/5/6 GHz (8/0x10/0x2000): width bits (2/4/0x400/0x800) do not take part. Direct consequence
  for the RTL layer: the project's own 129's entries must carry the right band bit
  (8 on 2.4 GHz, 0x10 on 5 GHz), else the overlap is 0 even when the channel matches.
- **Common channel**: `calculateHighestAllowedChannel` (`0xffffff80021ab1fa`) walks the peer's
  entries looking for 6 with `flags & 0x2018 == 8`, and 44 and 149 with `== 0x10`
  (`cmp r10d, 6/2Ch/95h` at `0xffffff80021ab278`–`84`; mask at `0xffffff80021ab250`) and saves
  the best at `priv+0x42` (`0xffffff80021ab5be`), with priority 149 > 44 > 6 and two
  "ultra low latency infra on 44" exceptions that lower it to 6 (`0xffffff80021ab2fb`–`338`). If
  no entry is 6/44/149 it logs `"Couldn't find Heighest Allowed Channel"` and returns 0:
  the peer does not push the common channel. With 149 present, `updateAWDLCommonChannel`
  (`0xffffff80022154de`, called at `0xffffff80021abad7` with a 1 s limit, §"Period 1000 ms")
  invokes `calculateAWDLCommonChannel` (`0xffffff800222b64e`), which votes with the `priv+0x42` of
  all peers: 149 by default (`mov al, 95h` at `0xffffff800222bad1`), 44 if some peer reports it and
  `isValidChannel(44, 0x10)` allows it (`0xffffff800222ba3f`–`a84`), 6 if some peer
  reports it (`0xffffff800222b908`), and at the end clips between floor 6 and ceiling
  `priv+0x42B0` (`0xffffff800222bb28`–`3e`). An iPhone serving 149 only on part of its slots
  leaves the common channel at 149 if there are no peers asking for 6 or 44.
- `followPeerChannelSequence` (`0xffffff80021f6e74`) copies the peer's sequence to the manager's
  scheduling only for AirPlay/ranging peers (`priv+1665` bit 0 and similar, called from
  `updateAwdlChanSeq` at `0xffffff80021abbf5`); it replaces disallowed 6 GHz channels with
  `_zeroChanInfo`/`_channel6Info`.

## 6. Consequences for the RTL layer (TX)

- The current shape of `putChanSeq` — `{15, 3, 0, 3, FF FF}` with pairs `{149,128}` and `{0,0}`
  (class 81 on 2.4 GHz) — is **accepted whole** by the kernel's parser: 128 and 81 are in the
  table, and the slots at 0 are not converted. H8 of `AWDL_TX_FRAME_REVIEW.md` closes both its
  doubts: 128 is accepted (80 MHz, band 0x1000) and `{0,0}` is (no conversion).
- The only thing that would reject the whole TLV on the main pass is a pair with channel ≠ 0 and a class
  outside the table (e.g. `{149, 0}`): the parser returns `-1` and the peer's sequence is not
  updated. Always emit a class of table §2.1 (81–84, 115–134) and `{0,0}` for the unserved slots.
- The on-air channel rules: it does not need to belong to the advertised class. Still,
  announce `{149,128}` (as owly and the iPhone do) so the receiver's flags (0x410) and the project's own 129's
  match in band.
- On reception, a real peer with `{0,0}` slots integrates without problem: overlap and common channel
  work per entry; the band bit of the project's own 129 is the critical part (§5).

## 7. Pending

- Exact meaning of bit 0 of `ctx+0x57` (the gate of `doMinimalAFProcessing`) and, with it, whether
  applying truncated sequences after a conversion failure (§3.4) is reachable with real frames.
- Reader of the peer's fill (`priv+0x394`) and writer of `priv+0x42B0` (ceiling of the common
  channel clip).
- Why `_convertChannelToOpClass` has no xrefs (only exported for client kexts?).
- Whether the `totalSlots ≥ 65024` edge of the −2 check is reachable (extreme dup/step).

## 8. References

- `AWDL_TLV_BODIES.md` (§2, TLV 18 and its correction in this document), `AWDL_TX_FRAME_REVIEW.md`
  (H8), `AWDL_CHANNEL_SEQUENCE_CONTROL.md` (129/130 and templates), `AWDL_PEERMANAGER_DRIVER_ABI.md`
  (12 B internal format and events 41/43), `AWDL_CHANNEL_STRATEGY.md` (common channel and
  airplaySink), `AWDL_OPEN_QUESTIONS_1.md` (`doMinimalAFProcessing`).
- Dumps and scripts of the session: `Build/ida_awdl/task_tlv18/` (`out1..out15.txt`, `d1..d6.py`).
