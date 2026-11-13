# AWDL TLV bodies: sync params (type 4) and channel sequence (type 18)

This document pins the fields the kernel reads within two TLVs of the AWDL action frames and which
peer ivar each one goes to. Complements `AWDL_FRAME_WIRE_FORMAT.md` (16 B fixed header,
3 B TLV header and list of types) and `AWDL_PEERMANAGER_DRIVER_ABI.md` (driver↔manager contract).
Unconfirmed items are marked `?`.

Source: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
IDB `Build/ida_awdl/task_awdl_pm_abi/kc.i64`. Kernelcache VAs; instruction addresses are
cited as the reading point within the decompiled function.

**Type numbering**: the `switch` of `actionFrameReport` (`0xffffff80021af6be`) operates on
`type - 2` (subtraction at `0xffffff80021b03ce`). Therefore the real type on the air is the label + 2, and
the correspondence is: sync params = **4**, chan seq = **18** (`0x12`), sync tree = **20**
(`0x14`), election params v2 = **24** (`0x18`). This corrects the naive reading of the labels of
the decompiler.

## 1. TLV type 4 — sync params

Function: `IO80211AWDLPeer::parseAwdlSyncParamsTlv` (`0xffffff80021b4ba6`, 0x546 B).

### 1.1 Entry and checks

```c
int IO80211AWDLPeer::parseAwdlSyncParamsTlv(IO80211Peer *peer,
                                            IO80211BufferCursor *tlv,   // a2
                                            IO80211BufferCursor *hdr,   // a3 (apple80211_act_frm_hdr, 72 B)
                                            IO80211BufferCursor *buf);  // a4 (AF body)
```

- It requires `tlv->len >= 0x2A` (42 B counting from the start of the TLV); otherwise the pointers are nulled
  and the function exits through the assert of "Error: Assert failure" (source line 3399): return 0.
- It requires `hdr->len >= 0x48` (72 B) and `buf->len >= 0x10`; otherwise the trap of
  `bounded_ptr` fires ("The range of valid memory is too large..."). That is, an AF with an incomplete
  metadata header cannot be parsed.
- The presence byte (`+30`) must be non-zero; with zero it logs
  "Zero presence Mode. SyncParams Parse failure" and returns 0 without touching the peer.

### 1.2 Body fields

The displacements are relative to the TLV pointer the function receives, which includes the header:
the body starts at `+3`. `IO80211AWDLPeer::tlvCheckBounds` (`0xffffff80021aefa0`) confirms it: for
type 4 it requires `len >= 0x27` (39 B of body), and the parse function requires 42 B of cursor (39 + 3 of
header, `cmp qword ptr [rsi+8], 2Ah` at `0xffffff80021b4bbf`). `tlvCheckBounds` additionally checks
`+36 <= 0x1F` and `+37 <= 3`, which are the number of slots minus one and the encoding of the channel
sequence starting at `+36` (body `+33`).

The names of the third column are those of the kernel log; the fourth gives the on-air field per the
specification of owly (`docs/SPEC.md` §3.2), contrasted with 4893 real action frames of iOS 26
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4.1). Each field read is cited
by its load instruction.

| Offset | Type | Field (kernel log) | On-air field (measured value) | Destination / effect |
|---:|---|---|---|---|
| `+0` | 1 | TLV type (4) | | not checked here; the iterator uses it |
| `+1` | 2 | length (`len`) | | not checked here; `tlvCheckBounds` requires ≥ 39 |
| `+3` | 1 | `tchan` | channel of the next AW | peer `+756` and `+1432` (`0xffffff80021b4ea7`) |
| `+4` | 2 | `dc` | downcounter in TU until the boundary of the next slot (≤ 64) | peer `+758` (`0xffffff80021b4d2e`) |
| `+6` | 1 | `mchan` | master channel | trace only |
| `+7` | 1 | — | guard time (0) | not read |
| `+8` | 2 | — | AW period in TU (16) | peer `+762` (`0xffffff80021b4d3a`) |
| `+10` | 2 | — | action frame period in TU (110; the iPhone also 440) | peer `+764` (`0xffffff80021b4d46`) |
| `+12` | 2 | — | flags (`0x1800`) | not read |
| `+14` | 2 | `aw_ext_length` | extended AW length (16) | peer `+768` (`0xffffff80021b4d5e`); other than 16 ⇒ "Zero AWExt" and peer `+770 = 1` |
| `+16` | 2 | — | common AW length (16) | peer `+772` (`0xffffff80021b4d52`) |
| `+18` | 2 | `rem` | remaining TU of the current AW | peer `+904` (`0xffffff80021b4e9b`) |
| `+20` | 1 | `ext` 1st | minimum extension (3) | peer `+1452` (`0xffffff80021b4eb7`) |
| `+21` | 1 | `ext` 3rd | multicast extension maximum (3) | trace only |
| `+22` | 1 | `ext` 4th | unicast extension maximum (3) | trace only |
| `+23` | 1 | `ext` 2nd | action frame extension maximum (3) | trace only; the log prints `ext(+20/+23/+21/+22)` |
| `+24` | 6 | — | master MAC address (in the captures, always one of the session MACs) | peer `+600` (4 B) and `+604` (2 B) (`0xffffff80021b4f86`, `0xffffff80021b4f8a`); no reader of `+600` as an address found in the AWDL functions `?` |
| `+30` | 1 | `pm` | presence mode, AW per slot (4) | peer `+748`; zero ⇒ parse failure (`0xffffff80021b4cdc`) |
| `+31` | 1 | — | reserved | not read |
| `+32` | 2 | `sn` | sequence number of the next AW | peer `+106` (`0xffffff80021b4d25`); the previous value masked with `0xFFC0` goes to `+108`, and if it changes `mach_absolute_time()` is stored at `+112` |
| `+34` | 2 | — | AP alignment | not read |
| `+36` | var. | — | channel sequence, as in type 18 | `parseAwdlChanSeqFromSyncParamsTLV` |

The "peer `+N`" ivars of this table and of the rest of the document are in the private block of the peer
(`*(peer+0x20)`), not in the `IO80211AWDLPeer` object itself.

Additionally, after setting `peer+784 = mach_absolute_time()` (stamp of the last AF) and incrementing counter
`peer+732`, it calls `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`) to extract the
channel sequence traveling within this same TLV; a failure there logs
"Error: parsing ch sequence %d" and returns 0.

### 1.3 Use of the fixed header of the AF

Within this function two `uint32_t` of the fixed header of the frame are read (`buf+8` and `buf+12`, i.e.
`phy_tx_time` and `target_tx_time` in the terminology of `AWDL_FRAME_WIRE_FORMAT.md`):
`v18 = *(u32*)(buf+8)` and `v17 = *(u32*)(buf+12)`. If both are non-zero and
`v18 > v17`, the difference is stored at peer `+752` (which the log prints as `sync_contention`).
They are also passed to `monitorPeersTimeline(peer, tlv, v18, v17, *(u32*)(hdr+48))` together with the dword
`+48` of the metadata header. A driver must, therefore, fill `+8`/`+12` with coherent monotonic
values if it wants that contention check to make sense; with both at zero the
difference is not computed and no contention is recorded (`?` on the exact unit).

## 2. TLV type 18 — channel sequence

Function: `IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b5a08`). Type 18 is the
"classic" shape of the sequence; the extended shape arrives via type 32
(`parseAwdlChanSeqFromChanSeqExtTLV`, `0xffffff80021b5e84`) and the variant embedded in sync params
via `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`). The result is dumped into an
`awdl_chan_seq_info2` (`bounded_ptr`, 392 B at minimum) that `updateAwdlChanSeq` afterwards applies
(`0xffffff80021ab7e4`).

### 2.1 Body on the air

Offsets from the start of the TLV (3 B header included), per the decompilation of
`0xffffff80021b5a08`:

| Offset | Size | Field | Use |
|---:|---:|---|---|
| `+0` | 1 | type (18) | iterator |
| `+1` | 2 | length | the cursor must measure at least 9 B |
| `+3` | 1 | `count - 1` | `n = byte + 1`; it requires `byte < 0x20` (32 entries maximum) and `2·n <= len_cursor - 9` |
| `+4` | 1 | encoding | `3` = pairs {channel, operating class}; any other value = bare channel |
| `+5` | 1 | duplicate flags (`?` name) | copied to `info2` |
| `+6` | 1 | step count (`?` name) | copied to `info2` |
| `+7` | 2 | fill channel (`?` name) | copied as `u16` to `info2` |
| `+9` | `2·n` | entries of **2 B** | see below |

Each entry occupies **2 bytes**, whatever the encoding (the index advances `+= 2`):

- Encoding `3`: byte 0 = channel number; byte 1 = operating class, converted with
  `convertOpClassToChannel` into width and band. If the conversion fails it logs
  "fail convert op class, op class 0x%X" and the function returns `-1`.
- Other encoding: byte 0 = channel number; the band is deduced from the number (`< 15` ⇒ flag `8`,
  2.4 GHz; `36..165` ⇒ flag `16`, 5 GHz; `0` ⇒ empty slot with flags `0`).

Additionally, the raw 2 B of each entry are copied into the ivars block of the peer, at
`*(peer+32) + 1368 + 2·i`.

### 2.2 Conversion to the internal format

Into the output `awdl_chan_seq_info2` the header `{n, encoding, flags, step,
u16 fill}` is written at `+0..+5` and, from `+8`, one 12 B entry per channel with the same format as
ioctl 129: `{u32 1, u32 channel, u32 flags}`. The 12 B format is therefore **internal to the
kernel**: on the air each slot occupies 2 B.

Errors: a cursor of less than 9 B, a length not covering `2·n` B or `n > 32` log
"Error: Assert failure" and return `-1`.

## 3. Summary

- The real type is the one of the `switch` label plus 2: sync params 4, chan seq 18, sync tree 20,
  election v2 24.
- Sync params occupies 42 B counted from the start of the TLV and carries working channel, duty cycle,
  extension length (must be 16), presence, sequence number and two fields of the fixed header
  (`+8` and `+12`) used only to measure sync contention.
- The channel sequence travels as up to 32 entries of 2 B from `+9` (channel and, with
  encoding 3, operating class); the kernel expands it into internal entries of 12 B.
- Everything marked `?` (exact cursor displacement convention, unit of the times of the
  fixed header, semantics of the `+21..+23` bytes of sync params and names of `+5..+8` of the sequence) remains open.
