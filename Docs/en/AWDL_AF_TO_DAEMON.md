# Which AWDL action frames reach the daemon as event 48 and which are consumed silently

Closes the open question E3 of `AWDL_ETAPAS_2_A_4.md` §5 ("UMI condition of subtype + channel that
decides which AF reaches the daemon as 48") and the last pending item of
`AWDL_WIFIP2PD_PRESENCE_TRIGGER.md` §7. Short answer: **the gate is neither the subtype nor the
channel**. An AF is posted as event 48 (`postAFToP2pd`) if and only if, at the end of
`actionFrameProcessInput`, the context carries no error code (`ctx+0x2C == 0`) and the peer has been
left with `peerReachability == 1` (`priv+0x78 == 1`). Everything else (header, OUI, RSSI, ring, rate
limit, TLV parsing) only decides whether the frame reaches that gate or is consumed silently (with a
dump to the BPF tap in some cases).

Source: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, build 25G241, md5
`f489ff8be80e6013a1bf47561dcaef45`), copy of the annotated IDB in `/tmp` during the session. All
addresses are kernelcache VAs, read from the disassembly or from Hex-Rays pseudocode. The
conventions (vtable, `priv = *(peer+0x20)`, 72 B header) are those of
`AWDL_PEERMANAGER_DRIVER_ABI.md`. Unconfirmed items carry `?`.

## 1. The full path

```
bpfTapInput (dlt 12)                                   0xffffff8002331070  (ABI §1.5)
  └─ IO80211AWDLPeerManager::actionFrameInput          0xffffff800220e1ae  (slot 71, +0x238)
       └─ [ring 104 B] actionFrameProcessThread        0xffffff80021c3ebc  (default, bit 0 of +16880)
            └─ actionFrameProcessInput                 0xffffff800220e8b0
                 ├─ isValidAF / actionFrameHasCorrectOUI   0xffffff800220fa64 / 0xffffff8002210200
                 ├─ findPeer / addAWDLPeer             0xffffff800220ec86 / 0xffffff800220eceb
                 ├─ IO80211AWDLPeer::actionFrameReport 0xffffff80021af6be
                 │    ├─ extractVersionTlvForLegacy…   0xffffff80021b3114 (call 0xffffff80021afc2d)
                 │    ├─ doMinimalAFProcessing         0xffffff80021b3ea6 (call 0xffffff80021b013f)
                 │    └─ shouldStopAFProcessing        0xffffff80021b48b8 (call 0xffffff80021b0160)
                 └─ postAFToP2pd                       0xffffff800220fc2c (only xref: 0xffffff800220f94c)
```

`postAFToP2pd` has a single caller in the whole binary: `actionFrameProcessInput` at
`0xffffff800220f94c` (verified xref). There is no alternative path to event 48.

## 2. The event 48 gate

In `actionFrameProcessInput`, after the call to `actionFrameReport` (`0xffffff800220ed31`):

| Step | Address | Effect |
|---|---|---|
| `cmp dword [ctx+0x2C], 0` | `0xffffff800220ed36`, jump `0xffffff800220ed42` | ≠ 0 ⇒ `xor r15d,r15d` and return 0 (`0xffffff800220ed44`-`0xffffff800220ed47`): AF consumed silently, no `SYNC_INFO` and no event. The return value in `eax` of `actionFrameReport` is not consulted anywhere. |
| peer check | `0xffffff800220f927` | no peer (`v86 == 0`, e.g. `addAWDLPeer` failed) ⇒ return without event. |
| `cmp dword [priv+0x78], 1` | `0xffffff800220f930`-`0xffffff800220f939` | `peerReachability != 1` ⇒ jumps to peer release (`0xffffff800220f761`) without event. |
| call `postAFToP2pd(this, buf, hdr)` | `0xffffff800220f94c` | only posting. Afterwards it releases the peer (`0xffffff800220f768`, vtable+0x28) and returns 0. |

`ctx` is the 0x98 B `actionFrameReportContext` that `actionFrameProcessInput` builds on the stack
(zeroed at `0xffffff800220e9b4`; fields at `0xffffff800220ec40`-`0xffffff800220ec79`: buffer +0x00,
header +0x08, hdrLen +0x10). `actionFrameReport` writes `ctx+0x2C = 1` (or codes 0x12–0x21) on its
error paths; success leaves it at 0. The snapshot of `priv+120` into `ctx+0x58`
(`0xffffff80021b01d8`/`0xffffff80021b2d80`) is informational and does not take part in the gate.

`peerReachability` (`priv+0x78`, dword) — doc `AWDL_PEERMANAGER_SOFTWARE.md` §3 calls it "master mode
+120"; the real field name is the enum `peerReachability`:

| Value | Writer | Address of the write |
|---:|---|---|
| 1 (routable) | `setPeerRoutable` | `0xffffff80021aaf2d` (`mov qword [rax+78h], 1`) |
| 1 | `extractVersionTlvForLegacyPeerProcessing` (OS 8/4 or flag `priv+0x31` bit 0) | `0xffffff80021b3243` |
| 2 (observed/legacy) | `extractVersionTlvForLegacyPeerProcessing` (`isLegacyPeerNotRoutable`) | `0xffffff80021b334d` |
| 3 (not reachable) | `setPeerNotReachable` | `0xffffff80021aa702` |
| generic | `setPeerReachability(v)` | `0xffffff80021ab180` (no direct xrefs: virtual call `?`) |
| 0 | initial state (peer created with `IOMallocZeroData` via `addAWDLPeer` `0xffffff800220fe4a` → `withAddressAndManager` `0xffffff80021a8dac`) | — |

## 3. What `postAFToP2pd` does (`0xffffff800220fc2c..0xffffff800220fdcb`)

1. `len = getLength(buf)` (vtable+304) and `body = getBytesNoCopy(buf)` (vtable+344);
   `IOMallocZeroData(len + 72)`; failure ⇒ log `Could not allocate AR BUFFER for AF message` (lines
   19654/19655) and **nothing is posted**.
2. Copies the 72 B header (`apple80211_act_frm_hdr`) and behind it the full buffer body:
   the event payload is `{hdr 72 B ‖ AF body}`.
3. Posts with slot +0xB18 (slot 355) of the AWDL interface vtable
   (`*(*(manager+24)+1376)`), which in `__ZTV23IO80211VirtualInterface` is
   `IO80211VirtualInterface::postMessage(uint, void*, ulong, bool)` (`0xffffff8002333192`),
   arguments `(48, buffer, len+72, 0)` — the channel that `wifip2pd` consumes as `DriverEvent.AWDL`
   tag 1 (`AWDL_WIFIP2PD_PRESENCE_TRIGGER.md` §3.1; `AWDL_EVENTS.md` §4.2).
4. Increments the IOReport counter `fAWDLActionFrames` (chID `0x4966416650325064` "IfAfP2Pd");
   failure ⇒ log `%s::%s(%d) Failed to inc %s`.

## 4. Condition table

### 4.1 Entry discards (`actionFrameInput` and header checks)

| Condition | Address | Effect |
|---|---|---|
| AWDL off (`+16096` bit 1 = 0) or disable in progress (`+414` bit 0 = 1), path with thread | `0xffffff800220e37f`, `0xffffff800220e388` | counters "IfAfIOFF", `sendToBpfTap` (`0xffffff800220e493`), buffer return (`0xffffff800220e49e`); no event |
| same, inside `actionFrameProcessInput` (path without thread) | `0xffffff800220e9bd`, `0xffffff800220e9c6` | immediate return 0 (`0xffffff800220e9d3`); no event |
| ring full (`(head+1) & mask == tail`) | `0xffffff800220e4b5`-`0xffffff800220e4c9` | overflow flag `+16868 = 1` (`0xffffff800220e4cf`), log `Out of Space in AF Queue` (19292, `0xffffff800220e50b`), arms the thread timer (`0xffffff800220e541`-`0xffffff800220e55d`), BPF tap and buffer returned; no event |
| `isValidAF` fails: `hdrLen <= 0x47`, null cursor or wrong OUI | `0xffffff800220fa64` (call of the path with thread after `0xffffff800220e4c9`; inline call of the path without thread at `0xffffff800220eb4e`) | logs `Invalid frame/header, headerLen %lu` / `frame len %lu … sizeof 72` / `BAD OUI in AF`; in `actionFrameInput`: log `Invalid AF` (19320, `0xffffff800220e87d`) + BPF tap; return 1 of `actionFrameProcessInput` (`0xffffff800220eb53`-`0xffffff800220eb58`) = "tap only". No event |
| OUI/type/version: `body[1..3] == 00:17:F2`, `body[4] == 0x08`, `body[5] != 0` | `0xffffff8002210200` (comparisons within `0xffffff8002210200..0xffffff8002210435`, exact instruction `?`) | the only content filter of the frame; `body[0]` (category) and `body[6]` (subtype) are **not checked** |
| `hdrLen >= 0x49` when enqueuing into the ring | `0xffffff800220e8a5` (`ud1`), same in the drain (`0xffffff80021c46ed`, end of function `?`) | deliberate kernel trap: a driver passing `hdrLen > 72` crashes the system |
| entry asserts of `actionFrameProcessInput` (null buf 19686, `getLength()==0` 19690, null hdr 19694, no workspace 19699) | `0xffffff800220ea00`, `0xffffff800220ea30`, `0xffffff800220ea5d`, `0xffffff800220ea8a` | return 0; no event |

### 4.2 RSSI discards in `actionFrameProcessInput`

| Condition | Address | Effect |
|---|---|---|
| known peer, `hdr+0x2C` (RSSI, signed) < `ivars+10268` (edge threshold, −70/−78) | comparison `0xffffff800220ecb4` (`jge` passes) | releases the peer (`0xffffff800220ecc3`) and returns 0; no event |
| known peer with bit 0 of `priv+0x0C` set to 1 | `0xffffff800220eca6` | skips the RSSI filter |
| unknown sender with RSSI < `ivars+10268` | `0xffffff800220ecd6` (`jl`) | return 0 without calling `addAWDLPeer` (`0xffffff800220eceb`); no event |
| `addAWDLPeer` returns 0 | `0xffffff800220ecf7` | log `AF NA [%3d]` (`0xffffff800220ee70`, snprintf at `0xffffff800220ee84`), notification to the RangingManager (`0xffffff800220eebc`); no event |

The values of `+10268` (+10272/+10276) are set by `determineAndSetEdgeRssiThreshold`
(`0xffffff80021e0f5e`; −70/−78, −65/−78, −45/−58; `AWDL_PEER_PRESENCE_PPP.md` §5.1).

### 4.3 Discards inside `actionFrameReport` (`ctx+0x2C ≠ 0`)

All of them end in the return without event; the caller only distinguishes zero from non-zero
(`0xffffff800220ed36`), although the written code varies:

| Code | Address of the write | Cause (log/line) |
|---|---|---|
| 1 | `0xffffff80021af9f4` | **rate limit**: ≥ 33 AFs (`0x21`, checked at `0xffffff80021af9e3`/`0xffffff80021afa00`) from the same peer within a 1024 ms window (`0x400`, `0xffffff80021af9db`; counter `priv+296`, stamp `priv+288`); when the window expires, log `Received %d Action Frames from peer … in 1 second. Bad Peer` (2226) |
| 1 | `0xffffff80021afde9` | "bad chan seq tlv, getCursorSize()" (2299): TLV 18 present but `isValidChanSeqTlv` (`0xffffff8002214b76`) fails |
| 1 | `0xffffff80021afe9a` | "bad chan seq ext tlv" (2316): TLV 32 with SDB active (signed byte of the controller +2879) invalid |
| 1 | `0xffffff80021b016b` | parse error flagged by `didAFPayloadChange` (`out-bool` at `0xffffff80021b0165`), `doMinimalAFProcessing == 2` (`0xffffff80021b0147`), or TLV 12 longer than the frame (`0xffffff80021affa3`) |
| 1 | `0xffffff80021b1913` | "serviceParam Bad ssth length" (malformed services TLV 6) |
| 0x12 | `0xffffff80021b19ca` | "Error: updating sync param…" |
| 1 (×5) | `0xffffff80021b1ace`, `0xffffff80021b1ae8`, `0xffffff80021b1b02`, `0xffffff80021b1b46`, `0xffffff80021b1b60` | `[BssSteering]` errors ("multiple") |
| 0x17 | `0xffffff80021b1f92` | "Error: parsing the NSync TLV" |
| 0x1D | `0xffffff80021b1ff7` | "[BssSteering] [ERROR] tlvParsing" |
| 0x1F / 0x1E / 0x15 | `0xffffff80021b217a`, `0xffffff80021b21d1`, `0xffffff80021b22e9` | "Error: parsing the version TLV" (TLV 21) |
| 0x21 | `0xffffff80021b2292` | "Error: parsing pref channel fr…" |
| 0x12 | `0xffffff80021b2575` | "Error: parsing ch sequence fro…" (TLV 4 of sync params) |

Additionally, the initial asserts of `actionFrameReport` (null ctx 2132, null buffer 2140, invalid
cursor 2168, `hdrLen < 0x48` 2188, 16 B body without TLVs 2203) return 0 **without** writing
`ctx+0x2C`: the frame continues toward the event 48 gate. That is, a valid AF with no TLV at all
from a routable peer is posted (empty body behind the header). `?` on whether any real producer
generates frames without TLVs.

### 4.4 Loss of eligibility (`peerReachability` stops being 1)

| Condition | Address | Effect |
|---|---|---|
| active RTG peer (`priv+1689` bit 0, state `priv+1698` = 1/2) whose AFs stop carrying the RTG TLV for ≥ 5001 ms (`0x1389`) since `priv+1744` | logs 3192/3198 and calls in the block `0xffffff80021b1d40`-`0xffffff80021b1db3` | `removePeerFromRealTimeGroup` (`0xffffff80021b1da6`) + `setPeerNotReachable` (`0xffffff80021b1dae` ⇒ `priv+120 = 3`, `cleanUpForPeerRemovedFromNDP`, which feeds event 47) + `removeAllIfOnlyPassivePeers`; the peer's next AFs are not posted until routable again |
| same with 2001–5000 ms | log "…no RTG TLV for 2 Seconds, trying to recover" (3210) | recovery attempt with `sendRealTimeUMI` (`0xffffff80022020e8`); no reachability change |
| passive RTG (state 3) without RTG TLV | log 3188 | only `removePeerFromRealTimeGroup`; reachability does not change here |

### 4.5 Conditions that decide `peerReachability == 1` (via `setPeerRoutable`)

`setPeerRoutable` (`0xffffff80021aad6c`) writes 1 at `0xffffff80021aaf2d` **before** its gates;
the following two conditions only decide whether that write is reached, and the third only whether
the "postPeerPresence" log is printed:

| Condition | Address | Effect |
|---|---|---|
| RSSI bubble: peer not seen on dual-band 5 GHz (`isDualBandPeerDiscoveredOn5GHz` `0xffffff80021b8cd4`) with RSSI < `ivars+10276` (−45/−58) | `0xffffff80021aad6c` (threshold read in pseudocode, instruction `?`) | log "Not Routable … outside bubble" and the 1 is **not** written (the peer stays at 0/2/3) |
| posting guard (`waitForPostingPeer` `0xffffff80021b801c`): with fastDiscovery (`PM+65193` bit 0 or `priv+1756` bit 0), less than `ndpPostingGuardTimemSec` (`PM+65216`; 128 ms by default) since the first AF (`priv+736`), unless `priv+1757` bit 0 | write `0xffffff80021aaf2d` only if the guard passes | the first AF of a new peer normally does NOT post; the guard must be waited out |
| `LLAValid` (`PM+65308` bit 0) and `AWDLOn` (`PM+16096` bit 1) | gate after the write (read of `PM+0xFF1C` at `0xffffff80021aaf47`, log 4050 "Cannot post peer presence … (LLAValid %d AWDLOn %d)") | if missing, only the log changes: 4050 appears instead of "postPeerPresence for…" (4057 or the `msecSinceAwdlOn` variant). `setPeerRoutable` does nothing else in either branch: `priv+120` is already 1 and the 48 is emitted all the same |

Callers of `setPeerRoutable`: `shouldStopAFProcessing` (`0xffffff80021b4958` tvOS/watchOS path/
flag and `0xffffff80021b4a11` normal path), `monitorPeerRssi` (`0xffffff80021aa432`) and
`peerDiscoveredOnBothSide` (`0xffffff80021bcda3`).

### 4.6 `shouldStopAFProcessing` (`0xffffff80021b48b8`): refines but does not decide the 48

| Condition | Address | Effect |
|---|---|---|
| parse error (`didAFPayloadChange` `0xffffff80021b8ee2` writes the `out-bool`) | `0xffffff80021b0165` → `0xffffff80021b016b` | **does discard**: `ctx+0x2C = 1` (malformed TLV 12 or TLV 33, log 3640/3652) |
| non-zero return ("stop") | return at `0xffffff80021b017a`-`0xffffff80021b017c` | **does not discard**: `actionFrameReport` returns 1 filling `ctx+0x3C..0x58` (`0xffffff80021b0188`-`0xffffff80021b01e1`) but `ctx+0x2C` stays at 0, so `actionFrameProcessInput` continues and the 48 is evaluated all the same; only the final block of the report is skipped (FW cache, peer cache update, `X+0xA8`) |
| condition of the "stop": peer already posted to the NDP (`X+0xA8 != 0`, `X = *(peer+0x18)`) and (final state ≠ 1 or payload unchanged) | flag read `0xffffff80021b4b4a`; computation `0xffffff80021b4b54`-`0xffffff80021b4b5e`; without posting ⇒ return 0 with log "Not Posted Peer to NDP so do not stop processing action frames." (3620) | the "payload unchanged" comes from `didAFPayloadChange`, which returns bit 0 of `priv+88` (`?` exact instruction; marked by `parseAwdlDataPathTLVAndTakeAction`) |
| counter of AFs without state 1 (`priv+124`) and reachability counters of the manager (`ivars+42800 + 4*state`) | `0xffffff80021b49fb`, `0xffffff80021b4b37`/`0xffffff80021b4b3f` | statistics only |
| tvOS peer (OS 8) or watchOS peer (OS 4) or flag `priv+0x31` bit 0 | `0xffffff80021b4943`-`0xffffff80021b4949` | skips the RSSI logic and goes straight to `setPeerRoutable` (if state ≠ 1) and return 0 |
| state 1: reliable RSSI tracking (threshold −60/−55/`ivars+10272` depending on the peer version) | `0xffffff80021b4981`-`0xffffff80021b49f6` | marks `priv+128`/`priv+136` for later expiry; no effect on the 48 |
| state 3 (recovery): header RSSI (`hdr+0x2C`) against `ivars+10276` if channel ≤ 14 or `ivars+10284` if > 14 (`getGoodRssiThreshold5G`, −55 by default: `initParamsToDefaults` writes it with the qword `0xFFFFFFC9FFFFFFD3` into `ivars+0x2828` at `0xffffff80021c7cf3`, and no other access with offset `0x2828` or `0x282C` in the IO80211Family AWDL functions writes it; `isPeerRoutable`, `shouldStopAFProcessing` and `determinePeerReachability` read it); "NO ROUTE -> ROUTABLE" | `0xffffff80021b4a1f`-`0xffffff80021b4a54` | the only channel condition of the whole path: it chooses the threshold, it does not forbid channels |

### 4.7 First AF of a new peer (`ctx+0x3A`)

`actionFrameProcessInput` zeroes the whole context with `bzero(ctx, 0x98)`
(`0xffffff800220e9b4`; the context is `rbp-0xC8`) and writes `ctx+0x3A = 1` in only one place:
`mov [rbp+var_8E], 1` at `0xffffff800220ecf0`, right after `addAWDLPeer`
(`0xffffff800220eceb`). The bit therefore marks that the peer was created by this very AF. In
`actionFrameReport` it controls two blocks:
| Test | Jump if 0 | Block that only runs in the AF that creates the peer |
|---|---|---|
| `test byte ptr [rbx+3Ah], 1` at `0xffffff80021afc1d` | `jz 0xffffff80021afc3a` | `extractVersionTlvForLegacyPeerProcessing` (`0xffffff80021afc2d`); if it returns 0 it jumps to `0xffffff80021b016b`, which sets `ctx+0x2C = 1` and the AF is discarded |
| `test byte ptr [rbx+3Ah], 1` at `0xffffff80021afe3b` | `jz 0xffffff80021aff14` | processing of TLV 32 (writes at `0xffffff80021afe50` and `0xffffff80021afe5b`) |
Consequence: the `peerReachability` 1 and 2 writes of `extractVersionTlvForLegacyPeerProcessing`
(§2) only happen with the first AF of each peer, and if that first AF does not pass the version TLV
extraction the peer is left created but that AF never reaches the event 48 gate.

## 5. Subtype, channel and sync state: what role they really play

- **Subtype (`body[6]`)**: the only check in the whole path is `body[6] == 3` in
  `actionFrameReport` (after the rate limit; block known only from pseudocode, exact instructions
  `?`) and it only feeds per-band counters: MIF → `priv+156`
  (2.4 GHz) / `priv+160` (5 GHz); PSF (0) → `priv+164` / `priv+168`; other subtypes are not counted.
  It also labels the `SYNC_INFO` log ("MI " if `body[6] != 0`, "PSF " if 0,
  comparison `0xffffff800220f215`) and feeds the MI template regeneration block
  (condition `MIF | other flag` in the LABEL_420 of `actionFrameReport`, address `?`). Neither
  `isValidAF` nor the event 48 gate look at the subtype: a PSF from a routable peer is posted just
  like a MIF. The "UMIs only" hypothesis of `AWDL_ETAPAS_2_A_4.md` §2.4 is corrected by this table.
- **Channel**: travels in `hdr+0x10` (filled in by the driver in the 72 B header). It is stored in
  `priv+1432` next to the byteswapped mactime in `priv+912` (consistent with
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.12), separates the per-band counters (§5) and chooses the RSSI
  threshold in the 3→1 recovery (`0xffffff80021b4a3d`). No channel is forbidden or required.
- **Sync state**: the only condition is AWDL enabled (`+16096` bit 1). Being inside the window
  (`ivars+772` bit 0) only opens the `SYNC_INFO` accounting block (sequence errors in
  `priv+280`, CoreCapture from 16 errors and 50001 ms, `selfDiagnosticsReport` at
  `0xffffff800220f9f8`); it does not condition the posting. Event 48 also goes out outside the window.

## 6. Dedup and rate, kernel/daemon split

- **Kernel**: there is no dedup of event 48. Every AF from a peer with `peerReachability == 1` and
  no error in `ctx+0x2C` is posted again, including repeats with identical payload (the "stop" of
  `shouldStopAFProcessing` only trims the internal report). The only rate control is the 33
  AFs / 1024 ms per peer one (`0xffffff80021af9f4`) and the input ring (full ⇒ tap without
  processing, `0xffffff800220e4b5`).
- **Daemon**: the real dedup is its own — it posts presence once per peer (own flag of the
  `AWDLPeer` struct, `AWDL_WIFIP2PD_PRESENCE_TRIGGER.md` §3.1) and the repeated 415s deliver nothing
  new (§6.2 of the same doc). That is why the kernel can afford to post every AF.

## 7. Implications for the RTL8814 driver

1. For `wifip2pd` to see peers it is enough to inject via `bpfTapInput(m, 12, &hdr, 72)` (`ABI §1.5`)
   with a real RSSI in `hdr+0x2C`: above `+10268` the AF creates/updates the peer; the
   `setPeerRoutable` chain (with its 128 ms guard and the −45/−58 bubble on 2.4 GHz) does the rest
   and from then on every AF generates its 48.
2. A driver that sets `hdr+0x2C = 0` always passes the edge filter (`0xffffff800220ecb4`, signed
   comparison) and the bubble of `setPeerRoutable`; the thresholds stop filtering anything. To mimic
   Broadcom behavior a real RSSI must be lowered.
3. The channel in `hdr+0x10` must be consistent with the real capture channel: it is only used for
   counters and thresholds, but a fake value degrades the 3→1 recovery (`0xffffff80021b4a3d`) and
   the statistics.
4. The natural PSF/MIF rate (one per 16 TU window ≈ every 15–30 ms per peer) is far below the 33
   AFs/1024 ms limit: the limit would only catch a driver that re-injects in a loop.
5. Frames discarded by OUI/length come out through the BPF tap of `awdl0` instead of event 48:
   `tcpdump -i awdl0` shows them even when `wifip2pd` receives nothing — useful for separating
   injection failures from reachability failures.

## 8. Pending (`?`)

- Exact instructions of: the per-subtype/band counter block of `actionFrameReport`
  (pseudocode after `0xffffff80021af9f4`), bit 0 of `priv+88` as the return of
  `didAFPayloadChange`, and the second `ud1`
  of the thread (`0xffffff80021c46ed` is the function limit, not the instruction).
