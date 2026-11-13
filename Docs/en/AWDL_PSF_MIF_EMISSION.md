# PSF and MIF emission: contract for a driver layer without AWDL firmware

This document fixes, function by function and byte by byte, which requests `IO80211AWDLPeerManager`
hands to a driver so it can transmit AWDL's periodic action frames (the sync frame, **PSF**, and the
master indication frame, **MIF**), where the driver must insert the TLVs 4 (sync params), 5 (election
v1) and 18 (channel sequence) the kernel does not build, where each field of those TLVs comes from,
with what cadence and 802.11 header they are transmitted, and which events the driver must return after
transmitting. It closes with the ordered list of what the RTL8814AU layer must build per frame.
Complements `AWDL_PEERMANAGER_DRIVER_ABI.md` (§3.11 already described the template of the 151),
`AWDL_FIRMWARE_DUTIES.md` (Broadcom side: iovars `awdl_payload`, `awdl_oob_af`, `awdl_oob_af_auto`),
`AWDL_FRAME_WIRE_FORMAT.md`, `AWDL_TLV_BODIES.md` and `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.2.

Source: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, 25G241, Darwin 25.6.0, md5
`f489ff8be80e6013a1bf47561dcaef45`), copy of the annotated IDB `~/re_cache/ida/boot_awdl.kc.i64` in
`Build/ida_awdl/task_psf_mif/`, decompilation and disassembly of the cited functions (outputs
`s1_out.txt`–`s5_out.txt` of that folder). Addresses = VA of the kernelcache, read in that IDB. Anything
unverified is marked `?`.

## 1. Summary

The kernel holds **two templates** and delivers them to the driver through different paths:

| Template | Builder | Delivery requests | Subtype on the air |
|---|---|---|---|
| Sync frame (PSF) | `updateSyncFrameTemplate` `0xffffff80021dc5b2` | internal ioctl **151** | `0` |
| Master indication (MI) | `buildMasterIndicationTemplate` `0xffffff8002211b42` | internal ioctl **149** (unicast = UMI; broadcast = immediate MIF) and internal ioctl **225** (permanent MIF per AW); **342** for the secondary MI (SDB) | `3` |

Neither template contains TLV 4 (sync params), TLV 5 (election v1) nor TLV 18
(channel sequence): there is no call to any builder of those types in either
function (§3), and no builder symbol exists for them in the fileset. The kernel also does not build
the 802.11 header nor the fixed 16 B header: the template traveling in the requests starts
directly at the first TLV. All that is driver/firmware work, which on the air produces the
order measured in the iOS 26 captures (`AWDL_REFERENCIAS_EXTERNAS.md` §4): PSF `4,5,18,6,24,12,7,17,21`
and MIF `4,5,18,24,12,7,17,16,21,6,2,…` — exactly the driver's TLVs in front of the kernel's TLVs, in the order each
builder stacks them.

TLV 16 (ARPA) **is built by the kernel** inside `buildMasterIndicationTemplate` (§3.2): the
driver must not add it on its own.

## 2. Requests delivering templates to the driver

### 2.1 Type 151 — template of the sync frame (PSF)

Only emitter in the whole fileset: `updateSyncFrameTemplate` (the sweep of the qword `0x97802869C8` over
the IO80211Family fileset, segment `0xffffff8002183600`–`0xffffff800239f200`, gives a single hit,
`0xffffff80021dc97b`; the request is formed with `mov rax, 97802869C8h` at `0xffffff80021dc97b` and
`call IO80211PeerManager::ioctl_internal` `0xffffff80022524e8` at `0xffffff80021dc996`).

```c
struct awdl_sync_frame_template_req {  // 16 B
    uint32_t reserved;                 // +0x00 = 0
    uint32_t length;                   // +0x04  valid bytes of the template
    uint64_t template;                 // +0x08  pointer to the 512 B buffer
};
```

The buffer is composed on the stack (512 B, `secure_memset(..., 0xAA, 0x200)`, `mov esi, 0AAh` at
`0xffffff80021dc5f1` and the call at `0xffffff80021dc5f6`), it is emitted only if `buildServiceParamTlv`
returned `< 0x200` and `length` is the sum of the stacked TLVs. The `bool` parameter of the symbol
(`updateSyncFrameTemplate(bool)`) takes no part in the body `?`. The userspace setter for type
151, `setAwdlSyncFrameTemplate` (`0xffffff800221d254`), is a stub returning 0: type 151 only
makes sense manager→driver.

The driver must copy exactly `length` bytes: what follows in the kernel's buffer is
`0xAA` padding. The template is re-issued on every state change affecting its TLVs (27 call sites of
`updateSyncFrameTemplate` listed in §3.1) and the driver must replace its copy on each 151.

### 2.2 Type 149 — unicast MI (UMI) and immediate MIF

Only emitter of the qword `0x95802869C8` in the fileset (hit `0xffffff8002213f22`):
`IO80211AWDLPeerManager::sendUnicastMI(peer, options, options2, u8 *bssSteeringData)`
(`0xffffff8002213c0a`). Assembled call at `0xffffff8002213f22`/`0xffffff8002213f32`. Its only
caller is `handleUmiTimer` (`0xffffff80021c3254`, xref at `0xffffff80021c3562`).

56 B payload (reconstructed from the decompilation of `0xffffff8002213c0a`; it corrects the layout of
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.10, see §9):

```c
struct awdl_mi_request {          // 56 B, compatible with apple80211_awdl_oob_request
    uint32_t reserved0;           // +0x00 = 0
    uint8_t  dst[6];              // +0x04  MAC of the peer (peer+88..93)
    uint16_t pad0;                // +0x0a
    uint32_t offset_us;           // +0x0c  = 0 (only the 225 uses it)
    uint32_t mi_bitmap;           // +0x10  = 0 (only the 225 uses it)
    uint16_t option;              // +0x14  6 = normal, 14 = UMIOnDwell
    uint8_t  self_bssid[6];       // +0x16  ivars+37204..37209
    uint32_t reserved1;           // +0x1c = 0
    uint32_t reserved2;           // +0x20 = 0
    uint16_t template_len;        // +0x28  ivars+10248
    uint8_t  pad1[6];             // +0x2a
    uint64_t template;            // +0x30  ivars+9048 (1200 B)
};
```

Before emitting, `sendUnicastMI` rebuilds the MI template with
`buildMasterIndicationTemplate(this, ivars+9048, 1200, 1, options, options2, …, bssSteeringData, peer)`
(call at `0xffffff8002213cec`) and saves the length at `ivars+10248`. Preconditions: AWDL
connected (`+16096` bit 1), inside window (`+772` bit 0) and ≥ 10 ms since the last UMI to the same
peer (`peer_ivars+1632`; `getUMIInterval` `0xffffff8002213bfe` returns the literal 10). Option 14
(UMIOnDwell) only if the PSF dwell is active (`+16943`), the `PSF_DWELL_UMI` capability (`+16579`) is
declared, the peer was not discovered on 5 GHz and the last dwell was ≤ 109 ms ago.

**The same request with `dst = FF:FF:FF:FF:FF:FF` is the immediate MIF**: `updatePrimaryPayloadMI`
(§2.3) reuses this type for the "send now" trigger of the kernel's MI timer.

### 2.3 Type 225 — permanent MI template (MIF per AW) and 149 "send now"

`IO80211AWDLPeerManager::updatePrimaryPayloadMI(reason, sendNow, …)` (`0xffffff80021fb36a`) is the
second consumer of `buildMasterIndicationTemplate` (xref `0xffffff80021fb3cf`) and the only emitter
of type 225. It rebuilds the MI template at `ivars+9048` (1200 B) unless `reason == 17` and
`ivars+10248 != 0`, and emits an ioctl whose low half is `0x802869C8` (`mov dword ptr [rsi],
802869C8h` at `0xffffff80021fb60e`, call to `ioctl_internal` at `0xffffff80021fb617`) and whose type
goes in the high half written separately:

- **149** ("send now"): `mov dword ptr [rbp+var_80+4], 95h` at `0xffffff80021fb533` — taken when
  `reason == 17` or `sendNow != 0`.
- **225** ("template to fw"): `mov dword ptr [rbp+var_80+4], 0E1h` at `0xffffff80021fb546` — taken when
  `reason != 17` and `sendNow == 0`.

Payload: the same 56 B structure of §2.2 with `dst = FF:FF:FF:FF:FF:FF` (`v38 =
0xFFFFFFFF00000000`, `v39 = 0xFFFF` in the decompilation) and, only on the 225 path, two additional
fields:

- `+0x0c uint32_t offset_us` — transmission offset within the window, in µs, computed with
  `read_random`: if bit `+64058` is active, `100·(rnd & 0xF)`; if bit `+65193` is active,
  `100·(rnd & 0x3F) + 5000`; if not, `100·(rnd8 + 10·(+744)) + 5000`. Trace
  `"Sending MI template size %u; miBitmap 0x%X at offset %uus"`.
- `+0x10 uint32_t mi_bitmap` — `ivars+42760` (primary MI bitmap; `setMiBitmap`
  `0xffffff80021fb2f8` rewrites it and forces an immediate send by calling `updatePrimaryPayloadMI`
  with `reason = 17`, i.e. through the 149).

The error is logged as `"ERROR: could not send Master Indication frame %s: %s"` with `%s` =
`"template to fw"` (225) or `"now"` (149). On the Broadcom side type 225 is the iovar
`awdl_oob_af_auto` — "permanent request instead of a transmission", repeated by the firmware on
each AW (`AWDL_FIRMWARE_DUTIES.md` §3.3) — and type 149 is `awdl_oob_af`.

Resulting semantics for the driver: **149 = transmit now** (unicast → UMI to the peer; broadcast →
MIF in the current window); **225 = store template + bitmap + offset and repeat the MIF in each
availability window**, with the indicated offset, until a new request arrives.

### 2.4 Type 342 — secondary MI (SDB)

`updateSecondaryPayloadMI` (`0xffffff80022141ae`) rebuilds the MI with
`buildSecondaryMasterIndicationTemplate` **over the same buffer `ivars+9048`** (1200 B) and emits the
qword `0x156802869C8` (unique hit `0xffffff8002214304`) with a 16 B payload:

```c
struct awdl_sec_mi_request {      // 16 B
    uint16_t pad;                 // +0x00
    uint16_t template_len;        // +0x02  ivars+10248
    uint32_t primary_bitmap;      // +0x04  ivars+42768
    uint64_t template;            // +0x08  ivars+9048
};
```

Called by `updateBroadcastMI` when the `SEC_PAYLOAD` capability (`+16576`) is declared, byte
`ctrl+1674` bit 0 is at 0 and `sendNow == 0`. Since the buffer is shared, a secondary MI destroys
the primary's bytes in the kernel: the driver must copy the template at request time, not
retain the pointer.

### 2.5 Emitter table (exhaustive sweep)

Sweep of the `ioctl_internal` qwords over the whole text segment of the fileset
(`0xffffff8002183600`–`0xffffff800239f200`, 2,210,816 B):

| Type | Qword | Hits | Emitter |
|---:|---|---|---|
| 149 | `0x95802869C8` | 1 | `sendUnicastMI` (`0xffffff8002213f22`); the 149 of `updatePrimaryPayloadMI` is formed with two 32-bit stores (`0xffffff80021fb533` + `0xffffff80021fb60e`) and does not appear as a qword |
| 151 | `0x97802869C8` | 1 | `updateSyncFrameTemplate` (`0xffffff80021dc97b`) |
| 225 | `0xE1802869C8` | — | `updatePrimaryPayloadMI` (`0xffffff80021fb546`), same as above |
| 342 | `0x156802869C8` | 1 | `updateSecondaryPayloadMI` (`0xffffff8002214304`) |
| 418 | `0x1A2802869C8` | 1 | `setPSFDwell` (`0xffffff80021f4f79`) |
| 116 | `0x74802869C8` | 2 | `setSyncFramePeriod` (`0xffffff80021ed0e5`) and `interfaceStateChange` (`0xffffff80021da0df`) |
| 132 | `0x84802869C8` | 2 | `configure` (`0xffffff80021c6010`) and `programMasterChannels` (`0xffffff80021d4a11`) |
| 129 | `0x81802869C8` | 1 | `setChannelSequenceUnconditional` (`0xffffff80021cc0b8`) |

Besides the 116 of `setSyncFramePeriod`, `interfaceStateChange` re-emits the sync params on its own
(`mov rax, 74802869C8h` at `0xffffff80021da0df`, with the trace `"Configuring the Sync Params"`).

## 3. Template composition in the kernel

### 3.1 PSF template — `updateSyncFrameTemplate` (`0xffffff80021dc5b2`)

Verified stacking order (each TLV `{u8 type, u16 len, body}`; VAs of the calls read in the
disassembly):

| # | TLV | Builder | Call | Condition |
|---:|---:|---|---|---|
| 1 | 6 service params | `buildServiceParamTlv` | `0xffffff80021dc613` | always (emission gate) |
| 2 | 24 election v2 | `buildAWDLElectionParamsVer2` | `0xffffff80021dc675` | always |
| 3 | 12 datapath state | `buildAwdlDataPathStateTlv` | `0xffffff80021dc6a1` | always (non-SDB variant, `false, 0, v8, false`) |
| 4 | 7 enhanced data rate | `buildEnhDataRateTlv` | `0xffffff80021dc6e5` | `1 ≤ ctrl+1564 ≤ 0x19` (HT length) |
| 5 | 30 ranging | `buildRangingTlv` | `0xffffff80021dc7e7` | `ivars+42545` bit 0 |
| 6 | — RTG | `buildRealTimeGroupTlv` | `0xffffff80021dc821` | `ivars+64418` or `ivars+65477` bit 0 |
| 7 | 17 802.11 container | `buildIEEE80211CntnrTlv` | `0xffffff80021dc863` | `ctrl+1568` bit 0; 14 B from `ctrl+2401` |
| 8 | 21 version | `buildAWDLVersionTlv` | `0xffffff80021dc89d` | always |
| 9 | 20 sync tree | `buildSyncTreeTlv` | `0xffffff80021dc8ce` | `ivars+10308` bit 0 |
| 10 | 23 NAN sync | `buildNanSyncTlv` | `0xffffff80021dc8f2` | always called; returns 0 without NAN (§3.4) |
| 11 | 33 pref chan | `buildPrefChanTlv` | `0xffffff80021dc911` | always called; may return 0 (§3.4) |

**There is no builder of TLV 4, TLV 5 or TLV 18**, and the fixed 16 B
header is not written: the first byte of the 512 B buffer is the `type` of TLV 6.

The 27 call sites of `updateSyncFrameTemplate` (verified xrefs): `disableAWDL`,
`interfaceStateChange` (`0xffffff80021da05e`), `dfsProxyStateMachine` ×3, `forceSdbModeONForRealTimeGroup`,
`enableAWDL` (`0xffffff80021dd375`), `reportDataPathEvents` (`0xffffff80021e0a60`), `dynSdbStateMachine`,
`runElection` (`0xffffff80021e44b1`), `setElectionParamsToNANPeer`, `monitorNearbyLogTrigger`,
`enterQuietMode`, `exitQuietMode`, `updateActionFrameReasonRTGState`, `enterAssistedDiscoveryFollowMode`,
`dynSdbForceSDBModeOn`, `updateLocalRequests` ×2, `updateRangingState`, `setAwdlNearbyLogTrigger`,
`setAwdlSyncEnabled` (`0xffffff800221b933`), `setAwdlSyncState` (`0xffffff800221e4d8`), `disableSdbMode`,
`limitedSdbForceSDBModeOn` and the ranging managers (`IO80211RangingManager`/`IO80211RangingManagerExt`,
9 sites). That is: each master change, strategy change, SDB change, ranging change or services change
re-generates the template and pushes down a new 151.

### 3.2 MI template — `buildMasterIndicationTemplate` (`0xffffff8002211b42`)

Signature (from the symbol): `(u8 *buf, u32 len, bool ensemble, u32 options, bool addProMode, bool
addAirDrop, IO80211AWDLPeerManager *?, IO80211AWDLPeer *peer)`; requires `buf != null` and `len ≥ 0x101`.
The primary path (MI of MIF/UMI) enters with `ensemble = 0` from `updatePrimaryPayloadMI`
(`ivars+9048`, 1200 B, rest of arguments at 0) and from `sendUnicastMI` (with `ensemble = 1`,
`options`, `options2`, bss steering data and the peer).

Verified order on the `ensemble = 0` path:

| # | TLV | Builder | Call | Condition |
|---:|---:|---|---|---|
| 1 | 24 election v2 | `buildAWDLElectionParamsVer2` | `0xffffff8002211b81` | always |
| 2 | 12 datapath state | `buildAwdlDataPathStateTlv` | `0xffffff8002211d61` | always (non-SDB variant) |
| 3 | 30 ranging | `buildRangingTlv` | `0xffffff8002211d89` | `ivars+42545` |
| 4 | — RTG | `buildRealTimeGroupTlv` | `0xffffff8002211dc0` | `ivars+64418` or `ivars+65477` |
| 5 | 23 NAN sync | `buildNanSyncTlv` | `0xffffff8002211de0` | always called; 0 without NAN |
| 6 | 7 enhanced data rate | `buildEnhDataRateTlv` | `0xffffff8002211e1c` | `1 ≤ ctrl+1564 ≤ 0x19` |
| 7 | 20 sync tree | `buildSyncTreeTlv` | `0xffffff8002211f46` | `ivars+10308` |
| 8 | 17 802.11 container | `buildIEEE80211CntnrTlv` | `0xffffff8002211f8b` | `ctrl+1568` bit 0 |
| 9 | 33 pref chan | `buildPrefChanTlv` | `0xffffff8002211faf` | always called; may return 0 |
| 10 | **16 ARPA (IPv6)** | inline + `IO80211ServiceRequestDescriptor::copyDataOnly` | `0xffffff800221202d` / `0xffffff8002212053` | descriptor `_rarpv6Service` (`ivars+42448`) non-null and with data; trace `"    -- added arpa ip6 tlv (dlen %u) -> %u"` (string `0xffffff80023dfbe3`) |
| 11 | 21 version | `buildAWDLVersionTlv` | `0xffffff80022120fb` | always |
| 12 | 6 service params | `buildServiceParamTlv` | `0xffffff8002212136` | `ctrl+1674` bit 0 = 0 or `SEC_PAYLOAD` (`+16576`) = 0 |
| 13 | 2 service responses | `buildServiceResponseTlvs` | `0xffffff80022121c5` | `ivars+16389` bit 0 |

TLV 16 is written with `type = 16` (`mov byte ptr [rsi-4], 10h` at `0xffffff800221202d`), length
`dlen+1`, first body byte `3`, and then the service descriptor's data
(`copyDataOnly`). If the size exceeds the threshold it logs
`"ERROR: %s:%d skipping ivars->_rarpv6Service TLV, tlv %u exceeds threshold %u"`. If the MI payload
overflows, bit `0x20` of the TLV 6 flags is forced (`mov byte ptr [rax+10h], 1` at
`0xffffff8002212191` on the flag, trace `"MI payload is overflowed"`, string
`0xffffff80023dfc8e`).

On the `ensemble = 1` path (only used by `sendUnicastMI` with ensemble options) the order
changes: 24, RTG (`0xffffff8002211c22`, trace `"Ensemble UMI TLV"`, string `0xffffff80023dfafb`), 12
(`0xffffff8002211c77`, SDB variant with `options`), TLV 29 bss steering (`buildMultiPeerBssSteeringTlv`,
`0xffffff8002211cb7`, only with data), ProMode (`0xffffff8002211ce7`, bit `addProMode`), AirDrop
(`0xffffff8002211d0d`, bit `addAirDrop`) and exit after `buildPrefChanTlv` (`0xffffff8002211d34`)
if `options & 0x100 == 0`.

**Conclusion about TLV 16 (ARPA): the kernel composes it in the primary MI, between the 33 and the 21.
A driver layer without firmware must not add its own TLV 16** — on the air it appears in that position
(`AWDL_REFERENCIAS_EXTERNAS.md` §4: MIF `…,17,16,21,6,2…`) and not in the PSF, consistent with the
builder existing only in the MI template.

### 3.3 TLVs only the driver can render

- **TLV 4 (sync params)** and **TLV 18 (channel sequence)**: neither builder
  calls anything that generates them; the fileset symbol sweep contains no
  `build*` for types 4, 5 or 18 (the only election/tree/channel builders are
  `buildAWDLElectionParamsVer2`, `buildSyncTreeTlv` and `buildPrefChanTlv`). The kernel only
  **parses** them on reception (`parseAwdlChanSeqFromChanSeqTLV` `0xffffff80021b5a08`,
  `parseAwdlChanSeqFromSyncParamsTLV` `0xffffff80021b8196`, per `AWDL_TLV_BODIES.md`).
- **TLV 5 (election v1)**: same; the receive parser exists (`parseAwdlElectionParamsV1`, cited
  in `AWDL_FRAME_WIRE_FORMAT.md` §4) but there is no builder. On the air the iPhone sends it in PSF and
  MIF in second position, so the driver/firmware renders it with the data of ioctl 155 (§5.2).

### 3.4 Builders that may render nothing

- `buildNanSyncTlv` (`0xffffff8002211146`): returns 0 unless the current master has a NAN sync TLV
  cached (it copies it from the master peer and increments its length; trace
  `"HC %d from master's Nsync tlv mismatch, should be %d"`). It explains why the iPhone captures do not
  show TLV 23.
- `buildPrefChanTlv` (`0xffffff8002211416`): returns 0 if byte `netif+2879` is not negative
  (6 GHz indicator), if there is no infrastructure channel convertible to operating class, or if the
  remaining space does not suffice; it writes `{type 33, len 7+…, flags, channel+class}`. It explains the absence
  of TLV 33 in the captures.

## 4. On-air order and insertion point

With the above, the frame the driver assembles is:

```
802.11 (FC, dur, A1, A2, A3) | 7F | 00 17 F2 | 08 | ver | subtype | rsvd | phy_tx_time(4) | target_tx_time(4) |
[TLV 4 sync params] [TLV 5 election v1] [TLV 18 chan seq] | <kernel template, TLVs in its order> | (no padding)
```

Contrast with the iOS 26 captures (`AWDL_REFERENCIAS_EXTERNAS.md` §4, 4893 frames):

| Frame | Measured order | Driver | Kernel |
|---|---|---|---|
| PSF | `4, 5, 18, 6, 24, 12, 7, 17, 21` | 4, 5, 18 | 6, 24, 12, 7, 17, 21 (§3.1; ranging/RTG/20/23/33 inactive in that session) |
| MIF | `4, 5, 18, 24, 12, 7, 17, 16, 21, 6, 2, …` | 4, 5, 18 | 24, 12, 7, 17, 16, 21, 6, 2 (§3.2; 16 between 33 and 21, 33 and 23 at zero) |

The correspondence is exact if the driver inserts **TLV 4, TLV 5 and TLV 18 — in that order — in front
of the first TLV of the received template** (type 6 in the PSF, type 24 in the MI) and does not touch the rest.
The subtype of the fixed header (`+6`) is `0` for the template of the 151 and `3` for the MI one; the
kernel does not indicate it in any request: the driver deduces it from which template it is transmitting.

## 5. Source of each field of the driver's TLVs

### 5.1 TLV 4 — sync params

Body layout in `AWDL_TLV_BODIES.md` §1.2 (42 B minimum counting the TLV header, with the
channel sequence embedded from `+36`). Kernel source of each field:

| Offset | Field | Source for the driver |
|---:|---|---|
| `+3` | `tchan` (channel of the next AW) | own execution of the 129 (next slot) |
| `+4` | `dc` (TU until the slot limit) | driver's own schedule |
| `+6` | `mchan` | ioctl **132** (`setAWDL_MASTER_CHANNEL`, 8 B, channel at `+4`; ioctl **205** for the secondary) |
| `+7` | guard | 0 (on-air value) |
| `+8` | AW period (TU) | ioctl **116** `+0x08` (`aw_period`, ivar `+62940`) |
| `+10` | period between action frames | ioctl **116** `+0x10` (`sync_frame_period`, ivar `+62952`); the kernel fixes 110 (§6.1) |
| `+12` | flags (`0x1800` on the air) | no reader in the kernel; origin `?` |
| `+14` | `aw_ext_length` | ioctl **116** `+0x0c` (`ext_length`, ivar `+62948`) |
| `+16` | common AW length | ioctl **116** `+0x04` (`aw_length`, ivar `+62944`) |
| `+18` | `rem` (remaining TU) | driver's own schedule |
| `+20..+23` | ext bytes 1–4 | ioctl **118** (`setAWDL_EXTENSION_PARAMETERS`, 20 B → ivars `+17035..+17038`; the exact order of the four bytes vs `+20/+23/+21/+22` `?`) |
| `+24..+29` | MAC of the master | ioctl **134** `+0x08` (master) / ioctl **155** `+0x16` (root MAC) |
| `+30` | `pm` (presence mode) | ioctl **136** (`setAWDL_PRESENCE_MODE`, `u32 ≤ 0x3F`) |
| `+31` | reserved | 0 |
| `+32` | `sn` (number of the next AW) | driver's own counter; it must be coherent with the `AwNumber` of event 41 (`monitorTimeline` compares) |
| `+34` | alignment with the AP | no reader `?` |
| `+36..` | embedded sequence | same encoding as TLV 18 (§5.3), parsed by `parseAwdlChanSeqFromSyncParamsTLV` `0xffffff80021b8196`; a failure there invalidates the whole TLV 4. The on-air captures do not allow fixing whether it travels with full entries or in minimal form `?`; replica of TLV 18 is the conservative option |

The 116/136/118 values are programmed by userspace (wifip2pd/airportd) through the manager's
slots (`setAwdlSyncParams` `0xffffff800221b73c` saves `+62940/+62944/+62948/+62952`; the
userspace request of type 116 is 24 B and the internal payload reaching the driver is the 20 B one of
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.4, verified here at `0xffffff80021ed024`: `{0, aw_length,
aw_period, ext_length, sync_frame_period}` with `LODWORD(v12) = +62940`, `HIDWORD(v11) = +62944`,
`HIDWORD(v12) = +62948`, `LODWORD(v13) = a2`).

### 5.2 TLV 5 — election v1

The parameters are delivered by ioctl **155** (`setAWDL_ELECTION_PARAMS`, 36 B:
`op_mode +0x10`, `election_id u16 +0x12`, `hop_count +0x14`, `root_mac[6] +0x16`, `root_metric +0x1c`,
`self_metric +0x20`; emitted by `programChipWithNewMaster` `0xffffff80021ec4b6` and
`initFWElectionParams` `0xffffff80021ed200` on each master change and when enabling). The body of
TLV 5 on the air is not fixed in this document (the kernel parses it with
`parseAwdlElectionParamsV1`, no builder of its own) `?`; the field-by-field correspondence with the
payload of the 155 is the render route, and the kernel itself emits TLV 24 (v2) already assembled with those
same data.

### 5.3 TLV 18 — channel sequence

Direct copy of ioctl **129** (`setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`, 400 B; layout in
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7 and `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1): `+3 = n−1`
(`num_channels−1`, byte), `+4 = encoding` (`unknown6`/`+42866`), `+5 = duplicate flags`,
`+6 = step`, `+7..8 = fill u16`, 2 B entries (`+9` onward): channel and, with encoding 3,
operating class. Entries with channel 0 = unserved slot (fixed radio pattern,
`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7). The 12 B format of the 129 is internal; on the air each
slot occupies 2 B (`AWDL_TLV_BODIES.md` §2).

## 6. When and how they are transmitted

### 6.1 PSF: period 110 contracted by 116/418

- The kernel delivers the template through the 151 and contracts the cadence through the **116**:
  `setSyncFramePeriod(period, force)` (`0xffffff80021ed024`) emits the 116 with
  `sync_frame_period = period` at `+0x10`; the driver must repeat the template of the 151 every that
  period, aligned with the shared schedule (same conditioning as
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §5.4).
- The value 110 is fixed by the kernel itself: `setPSFDwell` (`0xffffff80021f4a2a`) computes the dwell
  pairs `{110, 110}` (`mov rax, 6E0000006Eh` at `0xffffff80021f4c14` and `0xffffff80021f4c89`) or
  `{440, 440}` (`0x1B8000001B8`) depending on state, pushes them down through the **418** (`mov rax, 1A2802869C8h` at
  `0xffffff80021f4f79`, call at `0xffffff80021f4f89`, 16 B payload with the two 4 B dwells —
  traces `"Setting PSFDwell to {%ums %ums}"`) and, if the dwell ended up active, calls
  `setSyncFramePeriod(this, 110, 0)` (`mov esi, 6Eh` at `0xffffff80021f501e`, call at
  `0xffffff80021f5025`). The kernel's traces print the unit as `ms`; owly's spec measures 110 TU in TLV 4
  (`+10`) — the same number in both scales, unit unconfirmed `?`.
- Failure of the 418 disables the dwell (`ivars+16943 = 0`); success enables it and refreshes `+16956..71`.
  The dwell channel for the MI is contracted by `configureMITxOnDwellChannel` (`0xffffff80022140a4`) with
  the **432** (`awdl_config`, qword `0x1B0802869C8`) and a `u32` = `(channel != 0) << 7`: bit `0x80`
  of `awdl_config` is "TX of MI on the dwell channel" (traces `"awdl_config %d, …"`).
- The re-send of the 116 also happens in `interfaceStateChange` (`0xffffff80021da0df`) and on each
  `programChipWithNewMaster` (xrefs of `0xffffff80021ed024`: two sites at `0xffffff80021ec51e` /
  `0xffffff80021ecbd1`, `setScheduleState` ×2, `reevaluateRTGConfiguration`, `runPiggybackScanStateMachine`
  and `setPSFDwell`).

### 6.2 MIF: inside each availability window

- The kernel keeps its own MI timer (`handleMasterIndicationTimer` `0xffffff80021c3014`,
  registered in `initWithInterface` `0xffffff80021c1540` with `lea rcx, handleMasterIndicationTimer`
  at `0xffffff80021c20bf`; likewise the UMI timer at `0xffffff80021c213d`). Each firing executes
  `updateBroadcastMI(this, 2, 0, 1)` — reason 2, `sendNow = 1` — which goes through
  `updatePrimaryPayloadMI` (called at `0xffffff80021dcab3`) and exits through the **149 with broadcast dst**:
  an "now" MIF. The timer's rearm interval has not been traced `?`; the effective
  cadence on the air is one MIF per AW (the captures measure 2925 MIF vs 1968 PSF in the same
  session, and Broadcom repeats `awdl_oob_af_auto` "on each AW").
- The **225** path ("template to fw") is the permanent form: the driver stores template, bitmap and
  offset and repeats the MIF in each window with the indicated offset (§2.3). A layer that only
  implements the broadcast 149 works if it serves it on each kernel firing; the 225 is the offload mode.
- `updateBroadcastMI` (`0xffffff80021dca18`) only emits with AWDL connected (`+16096` bit 1) and
  respecting the in-flight request counter (`+17004` ≤ `+17008`); with reason 2 and bitmap
  `0x05050505` it additionally calls `adjustMiBitmap(0xFFFFFFFF, true, 2)`. Its ~35 xrefs (the same
  state changes that regenerate the PSF, plus `setChannelSequenceUnconditional` `0xffffff80021cc1da`
  after each 129) confirm that **each relevant change pushes down a new MI template**.

### 6.3 UMI

`sendUnicastMI` (§2.2) with dst = MAC of the peer, at most once per window and peer (≥ 10 ms of
separation, `getUMIInterval` = 10), inside the window (`+772`). `handleUmiTimer`
(`0xffffff80021c3254`) walks the queue of peers with pending UMI and skips a peer if its channel
(`peer+924`) or its flags (`peer+928 & 0x2018`) do not match the current window (`ivars+42828` /
`ivars+42832`), or if the peer is not in the cache (`peer+600/+601`): the decision of which peer gets a
UMI in each window is the kernel's, not the driver's.

### 6.4 Channel, rate and 802.11 header

- **Channel**: no template request (151/149/225) carries a channel. The frame goes out on the channel
  the radio occupies when transmitting, i.e. the current slot of the 129's sequence the driver itself
  executes, or the dwell channel if bit `0x80` of `awdl_config` is active (§6.1).
  For the advertisement to be honest, the transmission channel must match what TLV 18
  declares for that slot (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7).
- **802.11 header** (built by the driver; the kernel does not touch it):
  - A1: broadcast `FF:FF:FF:FF:FF:FF` for PSF and MIF (on the 149/225 the kernel passes exactly that
    dst for the broadcast path, and Broadcom fixes `awdl_af_hdr = FF×6 7F 00 17 F2` —
    `AWDL_FIRMWARE_DUTIES.md` §3.3 — for "every AWDL action frame it transmits"); the peer's MAC
    for the UMI (dst of the 149).
  - A2: own MAC of `awdl0` (the one fixed by type 125/`cur_etheraddr`; in the 149/225 payload
    `self_bssid` travels too).
  - A3: AWDL BSSID `00:25:00:FF:94:73` — value measured on all AWDL frames
    (`AWDL_REFERENCIAS_EXTERNAS.md` §5) and programmed on the interface by userspace with type 125
    (`setAWDL_BSSID`); the kernel forwards it in `self_bssid` (`ivars+37204..37209`) of each 149/225.
  - Body: category `0x7F`, OUI `00:17:F2`, type `0x08`, version ≠ 0, subtype (0 PSF / 3 MI),
    reserved, `phy_tx_time` and `target_tx_time` (§6.5), and then the TLVs.
- **Rate**: the kernel contracts no rate in the template requests. Broadcom initializes
  `2g_mrate`/`5g_mrate` to 12 Mbps at chip interface creation (`AWDL_FIRMWARE_DUTIES.md` §3.1)
  and exposes `awdl_min_rate` (type 213). The PSF/MIF rate is driver policy `?` (12 Mbps is the
  only value with support in the analyzed binaries).

### 6.5 `phy_tx_time` and `target_tx_time`

Fields `+8`/`+12` of the fixed header (`AWDL_FRAME_WIRE_FORMAT.md` §2). `parseAwdlSyncParamsTlv`
(`0xffffff80021b4ba6`) reads them: if both are non-zero and `+8 > +12`, the difference is saved as
`sync_contention` (peer `+752`) and both are passed to `monitorPeersTimeline`
(`AWDL_TLV_BODIES.md` §1.3). At zero they do not break parsing, but they disable that measure. A driver must
fill them with its own microsecond clock (actual and planned TX time) so the contention
measurement makes sense; the exact unit the air expects is owly's (µs) `?`.

## 7. Feedback the kernel expects after transmitting

- **Events 41/42/43** (mandatory, via `reportDataPathEvents`, cell `+0x298`): 36/4/40 B
  payloads documented in `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2/§2.4/§2.5. The 41 (`AwNumber`, master, top
  master, `FwAwStartTime` big-endian, channel, flags) is the one opening the window (`ivars+772`) and enabling
  the TX pull and the UMIs; the 42 zeroes/updates the counter `ivars+408`; the 43 closes it and
  triggers the election. The `AwNumber` and channel of the 41 must be coherent with the `sn`/`tchan` the
  driver renders in TLV 4: `monitorTimeline` compares both against the sequence (it only logs
  discontinuities, does not correct — `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §6).
- **Per-frame TX status: does not exist.** `reportDataPathEvents` has no branch for any
  "TX status"; the only confirmation of a template transmission is the synchronous return of the
  ioctl (0 = delivered). On Broadcom the completion of an OOB AF arrives as `WLC_E_ACTION_FRAME_COMPLETE`
  (60) and the kext publishes it as userspace event 69 (`AWDL_FIRMWARE_DUTIES.md` §4.2), a chain the
  manager does not consume. A custom driver must not invent a TX event: the kernel does not expect one.
- **Event 195 (`AWDL_CHAN_SEQ_CHANGED`)**: **published by the manager, not the driver**. After a successful
  129, `setChannelSequenceUnconditional` emits message 195 to the interface with the 400 B of the
  sequence (via cell `+2840` of the interface; `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1 and
  `AWDL_EVENTS.md` §3). It is transparent to the driver.

## 8. What the RTL8814 layer must build per frame (ordered list)

**On receiving the 151 (PSF template):** copy `length` bytes of the template (TLVs `6, 24, 12, 7, …,
17, 21, …` in the order of §3.1) and register the period of the last 116 (`+0x10`).

**On receiving 149/225 (MI template):** copy `template_len` bytes of `template` immediately (the
buffer `ivars+9048` is reused for the secondary MI, §2.4); with 225, additionally store
`offset_us` and `mi_bitmap`. With 342, same for the secondary MI (SDB).

**Per PSF (repetition of the sync frame, every `sync_frame_period` of the 116, aligned to the shared
schedule):**

1. 802.11 header: A1 `FF:FF:FF:FF:FF:FF`, A2 MAC of `awdl0`, A3 `00:25:00:FF:94:73`; rate per
   policy (Broadcom reference 12 Mbps `?`).
2. Fixed 16 B header: `7F | 00 17 F2 | 08 | ver≠0 | subtype 0 | rsvd | phy_tx_time | target_tx_time`
   (own µs clock).
3. **TLV 4** (sync params, §5.1): period/lengths/ext of the 116, `pm` of the 136, ext bytes of the 118,
   `mchan` of the 132, master of the 134/155, `tchan`/`dc`/`rem`/`sn` of the project's own schedule, embedded
   sequence = TLV 18.
4. **TLV 5** (election v1, §5.2) with the data of the 155.
5. **TLV 18** (sequence, §5.3) with the data of the 129: real channel on the served slots, 0 on the
   rest.
6. Verbatim copy of the received template of the 151 (without inserting anything between its TLVs).
7. No trailing padding (the parser tolerates it, does not need it).

**Per MIF (once per availability window; at the 225's offset if auto mode was accepted, or
on receiving the 149 broadcast of the MI timer):** same as 1–7 with subtype `3` and the MI template of
the 149/225 (TLVs `24, 12, …, 17, 16, 21, 6, 2…`; the 16 ARPA already comes inside). Do not add a custom TLV 16.

**Per UMI (149 unicast, inside the current window):** same with A1 = dst of the 149 and subtype `3`,
as soon as possible inside the window (the kernel already limited the pace to 10 ms per peer).

**Transmit on the current slot's channel** (or the dwell one if `awdl_config` bit `0x80`), so that
TX channel and TLV 18 match.

**After each window: events 41/42/43** with the payloads of `AWDL_PEERMANAGER_DRIVER_ABI.md` §2,
coherent with the advertised `sn`/`tchan` and with the channel actually used. Nothing else: no
per-frame TX event; the 195 is generated by the manager.

## 9. Corrections to prior notes

- `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.10 places `template_len` at `+0x20` and `template` at `+0x28` of
  the 149's payload; the decompilation of `sendUnicastMI` (`0xffffff8002213c0a`: `LOWORD(v40)` = len,
  `v41` = ptr, with `v40`/`v41` at the payload offsets +0x28/+0x30) and the symmetric read of the Broadcom
  handler (`request+0x28` = len, `request+0x30` = pointer, `AWDL_STRUCTS.md` §
  `apple80211_awdl_oob_request`) fix the correct layout: `template_len` at `+0x28` and `template` at
  `+0x30`. The total length of 56 B does not change.
- `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11 describes the template of the 151 with builders 8
  (`buildAWDLVersionTlv`), 10 (`buildNanSyncTlv`) and 11 (`buildPrefChanTlv`) as "always"; they are
  unconditional calls but the last two may render 0 bytes (§3.4), which explains the on-air captures
  without TLV 23 nor 33.
- `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.2 attributes TLVs 4 and 18 to Broadcom firmware and leaves
  implicit that TLV 5 and 16 are also born outside the kernel: the 5 yes (no builder in the
  kernel), but the 16 is composed by `buildMasterIndicationTemplate` in the primary MI (§3.2).
- New datum: `interfaceStateChange` also emits the 116 on its own (`0xffffff80021da0df`), and
  bit `0x80` of `awdl_config` (type 432) is the flag "TX of MI on the dwell channel"
  (`configureMITxOnDwellChannel` `0xffffff80022140a4`).

## 10. Open items

- Unit of the 116's period/`sync_frame_period` (110): the kernel's traces say `ms`, owly measures
  110 TU in TLV 4 `+10` `?`.
- Exact content of the body of TLV 5 on the air and its byte-by-byte mapping with the payload of the 155 `?`.
- Closed: in the iOS 26 captures TLV 4 occupies 73 B in the 4893 frames (iPhone and owly node),
  i.e. 33 B of fields + the full embedded sequence (6 B of header + 16 slots × 2 B in
  encoding 3) + 2 B of padding. The driver must render the full sequence inside TLV 4.
- Internal order of the four ext bytes of the 118 vs the positions `+20/+23/+21/+22` of
  TLV 4 `?`.
- Origin of the `0x1800` flags of `+12` of TLV 4 and of field `+34` (AP alignment) `?`.
- Transmission rate of PSF/MIF: the kernel does not contract it; 12 Mbps is only Broadcom's init `?`.
- Rearm interval of the kernel's MI/UMI timers (registered in `initWithInterface`, rearm
  not traced) `?`.
- Fine semantics of `mi_bitmap` (`ivars+42760`, bits per slot per `setMiBitmap`/`adjustMiBitmap`)
  in the payload of the 225 `?`.
