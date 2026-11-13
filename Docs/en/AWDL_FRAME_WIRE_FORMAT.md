# On-air format of the AWDL action frames as the kernel parses them

This document describes the AWDL frame that `IO80211AWDLPeerManager` accepts and parses: which bytes are
in front of the TLVs, what the kernel checks and which fields the driver must fill on each
transmission. Complements `AWDL_PEERMANAGER_DRIVER_ABI.md` (§1 covers the injection through
`actionFrameInput` and the `IO80211Buffer`) and `AWDL_FIRMWARE_DUTIES.md`. Only what
can be read in the code is documented; anything unconfirmed is marked `?`.

Source: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
IDB `Build/ida_awdl/task_awdl_pm_abi/kc.i64`. VA addresses of the kernelcache.

Reference functions:

| Function | Address | Role |
|---|---|---|
| `IO80211AWDLPeerManager::actionFrameInput` | `0xffffff800220e1ae` | buffer entry |
| `IO80211AWDLPeerManager::isValidAF` | `0xffffff800220fa64` | minimum lengths |
| `IO80211AWDLPeerManager::actionFrameHasCorrectOUI` | `0xffffff8002210200` | fixed header |
| `IO80211AWDLPeerManager::actionFrameProcessInput` | `0xffffff800220e8b0` | peer addition and split |
| `IO80211AWDLPeer::actionFrameReport` | `0xffffff80021af6be` | TLV parsing |
| `IO80211AWDLPeer::parseAwdlSyncParamsTlv` | `0xffffff80021b4ba6` | sync params TLV |
| `IO80211AWDLPeer::parseAwdlSyncTreeTLV` | `0xffffff80021b56d0` | tree TLV |
| `IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqTLV` | `0xffffff80021b5a08` | sequence TLV |
| `IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqExtTLV` | `0xffffff80021b5e84` | extended sequence TLV |
| `IO80211AWDLPeer::parseAwdlChanSeqFromSyncParamsTLV` | `0xffffff80021b8196` | sequence inside sync params |

## 1. What the `IO80211Buffer` contains

The buffer passed to `actionFrameInput` **does not** start at the 802.11 `frame control`: it starts at the
**category** octet of the action. The proof is in `actionFrameHasCorrectOUI`
(`0xffffff8002210200`), which reads the buffer cursor and compares:

```c
v5[1] == 0x00 && v5[2] == 0x17 && v5[3] == 0xF2 && v5[4] == 0x08 && v5[5] != 0
```

that is, OUI at `+1..+3`, type at `+4` and version at `+5`; `v5[0]` (the category) is not checked.
The 802.11 header and the FCS stay outside the buffer: the radio consumes them before delivering the
frame to the stack (`?` on whether the hardware removes them or the driver must trim them; on the
Broadcom path the body copied to the buffer already comes without them).

Lengths required before looking at anything else:

- `isValidAF` (`0xffffff800220fa64`) requires `hdrLen > 0x47` (72 B of `apple80211_act_frm_hdr`, the
  metadata, not the frame) and a cursor with non-null `ptr` and `len`.
- `actionFrameHasCorrectOUI` requires `len >= 0x10`.
- `actionFrameReport` (`0xffffff80021af6be`) requires `hdrLen >= 0x48` and `len >= 0x10`, and from
  there **fixes the start of the TLVs at `buf + 16`** (`v323[0] = v7 + 16`, `v292 = v8 - 16`).

## 2. Fixed 16 B header

The first 16 bytes of the buffer are the fixed header of the AWDL action; the TLVs start
exactly at `+16`.

| Offset | Size | Field | Checked in the kernel |
|---:|---:|---|---|
| `+0` | 1 | category (`0x7F`, *vendor specific* action) | not checked (`?` the required value; 0x7F is the usual one) |
| `+1` | 3 | OUI `00:17:F2` | yes: `actionFrameHasCorrectOUI` compares the three octets |
| `+4` | 1 | type `0x08` (AWDL) | yes: comparison with `0x08` |
| `+5` | 1 | version | yes: must be different from 0 |
| `+6` | 1 | AF subtype: `0` or `3` | yes: `actionFrameReport` branches `== 3` and `== 0`; other values do not increment the counter and are treated as "not UMI" (`v300 = 0`) |
| `+7` | 1 | reserved (`?`) | not read |
| `+8` | 4 | `phy_tx_time` (`?` name and units) | not read in the analyzed functions |
| `+12` | 4 | `target_tx_time` (`?` name and units) | not read in the analyzed functions |

Verified notes:

- The only byte of the fixed header the parser consults is `+6`. Everything else between `+7` and `+15`
  is opaque to the kernel: it is neither validated nor used for anything (`?` on whether Broadcom's
  firmware fills it; on the Broadcom path the content comes from the received frame itself).
- Subtype `0` and `3` are counted separately in the peer's ivars (`+160`, `+164`, `+168`), and
  combined with the channel (metadata header `+16`, i.e. the flags byte of the
  `apple80211_channel` of the `apple80211_act_frm_hdr`) they decide whether the AF is considered a UMI; the detail of
  that combination stays `?` because it depends on the interpretation of the channel in the metadata header.
- `+5` (version) is only required to be non-zero. A driver can always emit the same value; the
  kernel does not compare against a constant.

## 3. TLV format

The TLVs are walked with `IO80211TLVIterator<IO80211TLVHeader<unsigned char, unsigned short, 0, 1, 0>>`
(template visible in `actionFrameReport`, `findTLVCursorForType<...>`). The header is therefore:

```c
struct awdl_tlv_header {     // 3 B
    uint8_t  type;           // +0
    uint16_t len;            // +1  machine order (little-endian on x86_64)
};                           // the body starts at +3
```

- `type` is 8-bit and `len` 16-bit; the iterator advances `3 + len` and `tlvCheckBounds`
  (`IO80211AWDLPeer::tlvCheckBounds`) validates that `3 + len` does not leave the cursor before dispatching.
- A TLV whose `3 + len` exceeds the rest of the buffer triggers the log
  `"tlv parse error for Peer ... tlvType[%4u] tlvSize[%4lu] tlvlen[%4lu]"` and the abandonment of the parsing
  (`?` on whether it also discards the whole AF).
- The lengths the manager writes in its own template (ioctl 151) use the same format, so the
  kernel template and the on-air TLVs are interchangeable byte by byte.

## 4. TLV types the parser recognizes

`actionFrameReport` walks the TLVs and dispatches with a 34-case `switch` at
`0xffffff80021b03ed`. Just before, at `0xffffff80021b03ce`, `add r15d, -2` runs, so
**the `switch` label is the type minus 2**. The cases with their real on-air type (decimal) and
the destination of each:

| Type | Case destination | Effect |
|---:|---|---|
| 2 | `processServiceResponseTLV` | service response |
| 4 | `logDebug` trace and jump to `0xffffff80021b0f02` | sync params (the real parsing is `parseAwdlSyncParamsTlv` `0xffffff80021b4ba6`, `?` on the exact condition) |
| 5 | `parseAwdlElectionParamsV1` | election params v1 |
| 6 | virtual call `+0x38` and `OSMetaClass::getClassName` | service params (`?` on the consumer) |
| 7 | `IO80211Peer::parseHTCapTLV` | HT capabilities |
| 12 | skipped (already read before by `findTLVCursorForType(12)`) | datapath state |
| 16 | jump to `0xffffff80021b0e3d` (`shouldLog` and trace) | ARPA (`?` name) |
| 17 | `parseAwdlIEEE80211CntnrTLV` | IEEE 802.11 container |
| 18 | skipped (already read before by `findTLVCursorForType(18)`) | channel sequence (`parseAwdlChanSeqFromChanSeqTLV` `0xffffff80021b5a08`) |
| 20 | `parseAwdlSyncTreeTLV` `0xffffff80021b56d0` | sync tree |
| 21 | `parseAwdlVersionTlv` | AWDL version |
| 23 | `parseNanSyncTlv` | NAN sync |
| 29 | `logDebug` trace only | `?` |
| 30 | `parseAWDLRangingTLV` | ranging |
| 31 | `parseAWDLRealTimeGroupTLV` (also searched before with `findTLVCursorForType(31)`) | real time group |
| 32 | `parseAwdlChanSeqFromChanSeqExtTLV` `0xffffff80021b5e84` (also searched before with `findTLVCursorForType(32)`) | extended channel sequence |
| 33 | `parseAwdlPrefChanTLV` | preferred channel |
| 34 | `parseAwdlProModeTLV` | Pro Mode |
| 35 | `parseAwdlAirDropTLV` | AirDrop |

Types 3, 8–11, 13–15, 19, 22 and 24–28 fall into the default case (`0xffffff80021b0cb2`): the
iterator advances to the next TLV without error. Type 24 (election v2, the one the manager builds in
its template) appears there, so its parsing happens through another path (`?`).

Pre-scans before the walk with `findTLVCursorForType<...>`, with the real type in `r8d`:
`18` at `0xffffff80021afc5b`, `32` at `0xffffff80021afcdf`, `12` at `0xffffff80021affca` and `31` at
`0xffffff80021b006d`.

With this correction the numbering matches that of the manager's constructors (ioctl 151):
6 service params, 7 enhanced data rate, 17 IEEE 802.11 container, 20 sync tree and 24 election v2.
The type of the TLV on the air is exactly the `type` byte of the header. The parser tolerates unknown
types: it ignores them and moves on to the next.

**End of the walk and padding.** The loop does not reject leftover bytes after the last TLV. It stops without
error when fewer than 3 bytes remain (`cmp rsi, 3` at `0xffffff80021b02ab` before entering and at
`0xffffff80021b1376` after each advance) or when the declared length of the next TLV plus 3 exceeds what
remains. `IO80211TLVIterator<...>::operator++` (`0xffffff80021b7d70`, called at
`0xffffff80021b1342`) applies the same two conditions. A zero padding of any length passes
as TLVs of type 0 and length 0: `tlvCheckBounds` (`0xffffff80021aefa0`, called at
`0xffffff80021b03ad`) accepts type 0 with length between 0 and 1024, and the `switch` sends it to the default
case. Thus the iPhone MIFs ending in 8 zero bytes
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4) are processed whole, and a final
truncated TLV is silently discarded instead of invalidating the frame. A custom emitter needs no padding.

## 5. Fields the driver must fill per transmission

For each action frame emitted:

1. **Fixed 16 B header**: category at `+0`, OUI `00:17:F2` at `+1..+3`, `0x08` at `+4` and non-zero version
at `+5`. Those are the four fields the kernel validates; a wrong value discards
the AF with `"BAD OUI in AF"` and a dump of the full frame. Subtype `+6` must be `0` or `3` for the
AF to be counted as a normal AF or as a UMI.
2. **TLVs from `+16`**, each with `{u8 type, u16 len}` and body of `len` bytes. The buffer must
not contain the `frame control` nor the FCS.
3. **Times relative to the availability window**: the fixed header carries none of the
time markers the manager uses to validate the timeline. Those values travel outside
the frame: the window start is published through event 41 (`FwAwStartTime` big-endian in the
payload) and the period through ioctl 116. A driver without firmware must, therefore, keep its own
window clock and derive from it (a) the instant of repetition of the template of the 151 and (b) the
`AwNumber`/`FwAwStartTime` it reports in event 41.
4. **Fields `phy_tx_time`/`target_tx_time` (`+8`/`+12`)**: times in µs of the actual and the
planned transmission (owly AWDL-3). `parseAwdlSyncParamsTlv` reads them (`mov r12d, [r14+8]` at
`0xffffff80021b4ce7`): if both are non-zero and `+8 > +12`, it saves the difference as
`sync_contention` and passes it to `monitorPeersTimeline`
([AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) §1.3). At zero they do not break parsing, but they disable that
measure; a custom emitter must fill them with its microsecond clock.

## 6. Operational summary

- The injected buffer starts at the category of the action; the next 16 B are fixed header and TLV
  number one starts at `+16`.
- The kernel only validates category/OUI/type/version (`+1..+5`) and the subtype (`+6`); the rest of the
  fixed header is opaque.
- The TLV header is 3 B and the length is 16-bit in machine order.
- No time field of the frame is validated; the timeline is contracted through events and ioctls.
- The on-air TLV types match those of the manager's constructor; the parser's `switch`
  works on `type - 2`.
- Everything marked `?` (semantics of `phy_tx_time`/`target_tx_time`, path of type 24 and the use of the
  subtype combined with the channel) stays open.
