# AWDL: external projects and what they contribute

Review as of 2026-10-30 of the open AWDL implementations published outside Apple, to know what
code or information serves the work on `RTL8814AirLayer.kext`. The concrete question is whether
any of them lifts the lockup described in
[AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md) (lack of an AWDL peer) and whether their
data confirm or correct what was documented from the kernelcache. Anything unchecked goes
as `?`.

## 1. Reviewed sources

| Project | Revision | License | What it is |
|---|---|---|---|
| `t4t5/owly` | `9a676e1` (2026-10-26) | MIT | AWDL and AirDrop in software for Linux on non-Broadcom cards (Intel, MediaTek, Realtek). Specification in `docs/SPEC.md`, anonymized iOS 26 captures in `tests/captures/` |
| `s-iwaki-d/ESP32Drop` | `92bdc12` (2026-09-07) | ISC-like (permissive) | AWDL and AirDrop on an ESP32-S3, channel 6 only; tested against Mac and iPhone |
| `raph559/winawdl` | `2196fb4` (2026-10-13) | MIT; `radio` GPL-2.0, `awdl` GPL-3.0 | Native AWDL and AirDrop on Windows with the RTL8852CE in monitor mode; `DESIGN-NOTES.md` with measurements |
| `netmojo-systems/omdrop-awdl` | `46ed6e1` (2026-10-30) | GPL-2.0 | Sixteen patches to `brcmfmac` that expose the Broadcom AWDL firmware of Apple Silicon Macs under Linux |
| `seemoo-lab/owl` | `0ee3263` (2026-10-12) | GPL-3.0 | Reference AWDL implementation in C (OWL) |
| `seemoo-lab/opendrop` | `11fe7ba` (2026-08-17) | GPL-3.0 | AirDrop layer in Python over OWL |
| Project Zero, «Over The Air Vol. 2, Pt. 2» (2017) | — | — | Exploitation of the Broadcom firmware of an iPhone 7 with iOS 10.2 |

Local copies in `~/re_cache/work/awdl_refs/`.

## 2. Architecture difference

All the previous implementations run the full AWDL protocol (synchronization, election,
channel sequence, TLVs, data encapsulation) in their own code. On macOS that role belongs to
`IO80211AWDLPeerManager` inside `IO80211Family`
([AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md)); the layer only contributes radio,
window events and frame delivery
([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md)). That is why
no protocol block of these projects goes into the kext. What is reusable is:

- an AWDL peer that is not an Apple device (§3);
- real iOS 26 frames to contrast the documented format (§4);
- the radio solutions they had to find on non-Broadcom hardware, Realtek in particular
  (§5 to §7).

## 3. AWDL peer without an Apple device

| Option | Hardware needed | Band | Fit in this project |
|---|---|---|---|
| ESP32Drop | ESP32-S3 with PSRAM (tested on M5Stack StopWatch, core `m5stack:esp32` 3.3.8) | 2.4 GHz, fixed channel 6 | Direct: it needs no other machine nor other card. AirDrop tested against a Mac in both directions (sending to Mac PNG/JPEG, also over 128 KiB) |
| owly | A second Linux machine with a card from its list (e.g. MT7612U USB, RTL8852BU USB) | 2.4 and 5 GHz per card | Requires a second machine; the dev PC boots macOS or CachyOS, not both, and the T9UH is its only card |
| winawdl | PC with RTL8852CE and Windows 11 | 2.4 and 5 GHz | Not applicable: specific card the project does not have |

The ESP32-S3 stays on channel 6 and does not run a full election: it never elects itself,
advertises as a leaf with a low metric (advertising as a root made iOS treat it as outside the
mesh) and follows the master it hears. Without a master it does not transmit: `send_mif` refuses
while there is no election result (`ESP32Drop/src/awdl/core/awdl_tick.h:55`, `awdl_elect.h`,
`awdl_window.h:188`).
It also adjusts its clock from the master's PSFs (`awdl_gauge.h`, `awdl_port_esp32.cpp`).

Therefore, against a Mac with this driver the ESP32 does not serve for «RX first». The order
reverses:

1. The `IO80211AWDLPeerManager` of the Mac already elects itself master with `peers:0`
   ([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)). If the layer transmits
   its PSF/MIF on channel 6 with coherent times, the ESP32 hears them, elects the Mac as master
   and starts emitting MIF.
2. That reaction, visible on the ESP32 serial port (`awdl_status.h`, `awdl_diag.h`), validates the
   TX of the RTL8814AU without another receiver.
3. Only then are there ESP32 frames to receive and the RX test has content.

To have a frame source without own TX, the alternative is a modified build of the
ESP32 that advertises as master when it hears nobody. How the peer manager of macOS would react
to that master is `?`; iOS rejected it per the ESP32Drop comments.

Other limitations: channel 6 only, AirDrop send and receive in separate builds, and the
send-side discovery uses BLE, which the dev machine does not have working. macOS rotates its AWDL
MAC every 100 s approximately (`ESP32AWDL.h`). Whether the `IO80211AWDLPeerManager` accepts a peer
that only attends channel 6 is `?` until tested.

## 4. Contrast with the iOS 26 captures

`owly/tests/captures/` brings three sessions on channel 44 (discovery, receive and send between
an iPhone with iOS 26 and an owly node with `mt7925e`). The MAC addresses are replaced by
`02:00:00:00:00:0N`, names by `x` and unknown contents by zeros; the lengths and
times are preserved. A custom reader (radiotap header, 802.11 header, the 16 B fixed header and
TLV walk) gave, over the three captures:

| Measure | Value | Frames |
|---|---|---|
| AWDL action frames | 1968 PSF (subtype 0) and 2925 MIF (subtype 3) | 4893 |
| TLV 21 (version) of the iPhone | `0xA0`, device class 2 (iOS) | 1462 |
| TLV 21 of the owly node | `0x34`, class 1 | 3431 |
| TLV order, iPhone PSF | 4, 5, 18, 6, 24, 12, 7, 17, 21 | 705 |
| TLV order, iPhone MIF | 4, 5, 18, 24, 12, 7, 17, 16, 21, 6 and several type 2 | 757 |
| Leftover bytes after the last TLV | 0 in all | 4893 |
| Data header after LLC/SNAP `AA AA 03 00 17 F2 08 00` | `03 04`, sequence, `00 00`, ethertype `86 DD` | 4424 |

Type 17 appears in every iPhone frame and in none of owly's; in the kernel it is analyzed by
`parseAwdlIEEE80211CntnrTLV` ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md)), and per
`winawdl/DESIGN-NOTES.md` it is where the iPhone announces its VHT capabilities. The captures do
not contain MIFs with zero padding nor A-MSDU; winawdl did observe iPhone MIFs ending in 8 zero
bytes on channel 44. The kernel parser accepts it: the zeros pass as TLVs of type 0 and length 0
and go to the default case ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) §4).

### 4.1 Fields of TLV 4 (sync params)

The owly specification names all the fields of the body; the captures confirm their values. With
the convention of [AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) §1.2 (offset from the TLV start,
body at `+3`) the correspondence is:

| TLV offset | Name in `AWDL_TLV_BODIES.md` | Field per owly | Value in the 4893 frames |
|---:|---|---|---|
| `+3` | `tchan` | channel of the next AW | — |
| `+4` | `dc` (duty cycle / duration) | countdown in TU until the limit of the next slot | always ≤ 64 |
| `+6` | `mchan` | channel of the master | — |
| `+7` | (not listed) | guard time | 0 |
| `+8` | sync field 1 | AW period in TU | 16 |
| `+10` | sync field 2 | period between action frames in TU | 110 (iPhone also 440 in 133 MIFs) |
| `+12` | (not listed) | flags | `0x1800` |
| `+14` | `aw_ext_length` | extended AW length | 16 |
| `+16` | window field 1 | common AW length | 16 |
| `+18` | `rem` | TU remaining of the current AW | — |
| `+20..+23` | `ext byte 1..4` | minimum and maximum extension of multicast, unicast and action frames | `03 03 03 03` |
| `+24`, `+28` | «sequence» dword and word | MAC address of the master (6 B) | always one of the substituted MACs |
| `+30` | `pm` | presence mode (AW per slot) | 4 |
| `+32` | `sn` | sequence number of the next AW | — |

[AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) §1.2 already incorporates these names. The rows
`+24`/`+28` correct its earlier reading: on the air those
6 bytes are the master's address. That the kernel copies them to peer `+600`/`+604` as address and
not as sequence is consistent with that, but the later use of those ivars has not been reviewed
`?`. The rows `+4`, `+8`, `+10` and `+16` replace the generic names with the specification's; the
measured values (16, 110, 16) fit an AW period of 16 TU.

### 4.2 iPhone channel sequence (TLV 18)

In the 1462 iPhone frames (version `0xA0`, class 2), the TLV 18 header is always the same:
16 slots (`count-1 = 15`), encoding 3 (channel + operating class, 2 B per slot), `dup` 0,
`step` 3 and padding `0xFFFF`, with 41 B length. Observed sequences (channel per slot 0..15):

| Frames | Sequence |
|---:|---|
| 817 | `44 0 44 0 0 0 0 0 6 0 44 0 0 0 0 0` |
| 359 | `44 44 44 0 0 0 0 44 6 44 44 44 0 0 0 44` |
| 153 | `44 44 44 0 0 0 0 0 6 44 44 0 0 0 0 0` |
| 133 | `44 44 44 44 44 44 44 44 6 44 44 44 44 44 44 44` |

- The iPhone announces channel 0 in the slots it does not attend, which is the honest pattern
  [AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) §7 requires.
- Channel 6 only appears in slot 8, one out of 16.
- Channel 44, the channel of the captured session, occupies 3 to 15 slots depending on traffic.

For the RTL8814AU:
- with the radio fixed on 44 or 149, it matches the iPhone on several slots even at rest;
- with the radio on 6 (the ESP32-S3, or the T9UH on 2.4 GHz), it only matches slot 8.

The captures are from an owly node on 44; with another peer on another channel, the iPhone picks
another dominant channel `?`.

## 5. Data on the air

[AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) describes the header Broadcom exchanges
with its firmware (`AA AA 03 … 17 8F 08`, 12-bit counter with the ring id), and
[AWDL_PROTOTYPE_DESIGN.md](AWDL_PROTOTYPE_DESIGN.md) takes it for non-imitable. That header is
internal to the PCIe bus; on the air the AWDL data frame is (owly AWDL-13, patch 0006 of
omdrop-awdl, and the 4424
frames measured in §4):

```
802.11 data (QoS optional) | AA AA 03 | 00 17 F2 | 08 00 | 03 04 | seq (u16) | 00 00 | ethertype (BE) | IPv6
```

with BSSID `00:25:00:FF:94:73` on all AWDL frames (owly AWDL-1) and a per-node sequence,
incremented on each frame. Without AWDL firmware, the layer or the driver has to build this
encapsulation when going from the Skywalk TX queue to the USB and strip it on RX before
`enqueuePackets`. At which point of stage 2b
([AWDL_STAGE2B_MBUF_TO_PACKET.md](AWDL_STAGE2B_MBUF_TO_PACKET.md)) the packets arrive already
encapsulated or as Ethernet II is `?`. Receivers must accept A-MSDU (bit `0x0080`
of the QoS control, subframes aligned to 4 bytes, owly AWDL-14).

## 6. Fixed radio and timing

owly, winawdl and ESP32Drop do not hop channels: the radio stays on one and the iPhone visits it
in one or two slots of every 16. Published data:

- Slot = 4 AWs of 16 TU = 64 TU; sequence of 16 slots (owly `sync.rs`).
- Unicast only inside a shared AW, with 3 TU of guard at the edges; multicast with 16 TU of
  guard in slots 0 and 10, or in any served slot with 3 TU if the radio is fixed (owly
  AWDL-16).
- A node must announce in each slot only the channel it will really be on, and channel 0 in those
  it does not attend; a slot announced and not attended loses frames (owly AWDL-12).
- Reproducing the captures, owly keeps its schedule within 3 TU of the master with host timers
  and arrival timestamps, not chip ones (owly T-AWDL-3). winawdl does the same with a
  high-resolution Windows timer.
- iPhone at rest: one slot for its AP channel, one for 149, one for 6 and one for 44; when
  sending it adds slots on 44 (winawdl, measurement of 2026-10-11). On channel 6 the transfer is
  slow (188 kB/s on winawdl, about 35 kB/s on the RTL8188FTV of owly).

For the RTL8814AU this bounds the risk of [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) §7:
there is no need to reproduce the Broadcom channel hop to interoperate, as long as the channel
sequence the `IO80211AWDLPeerManager` announces matches the channel the radio is on. How to
influence that sequence from the layer (setter of type 18, advertised capabilities) is `?`.

## 7. Hardware lessons

**winawdl, RTL8852CE** (`DESIGN-NOTES.md`, «Acknowledging frames» and following):

- If the hardware does not ACK the AWDL node's MAC, the iPhone retries every frame up to its
  limit:
  85 % of frames with the retry bit and transfers that stall. With a MAC address filter of the
  configured MAC set to the local MAC, 0-4 %. On the RTL8814AU the equivalent is the second port
  (`REG_MACID1` 0x0700, `REG_BSSID1` 0x0708) with `RCR_ENMBID`
  ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md)); the T9UH has no Bluetooth, so the
  coexistence wait winawdl had to disable does not apply.
- The advertised HT capabilities must match the tuned width: with 40 MHz advertised and the
  radio at 20 MHz, 837 of 838 frames arrived only as retry at MCS 0.
- Block Ack: the iPhone requests an agreement on TID 0 with window 64 once a second while
  sending; accepting it went from 971 kB/s to 1.7 MB/s and the retries from 904 to 47 of some
  20 400 frames.
- TX retries: with a single attempt TCP answers were lost; with 7 (the usual in a station)
  not.

**owly, `drivers.toml`**: on fixed radios it is better to send unicast without waiting for the
hardware ACK (`tx_noack`), because a frame retried beyond the peer's visit blocks those behind
it. Realtek cards tested: RTL8822BE, RTL8852AE/BE/CE, RTL8852BU USB and RTL8188FTV USB work;
RTL8821CE does not. The RTL8814AU is not listed.

**omdrop-awdl, Broadcom**: the BCM4387 firmware creates the AWDL interface with `interface_create`
(flags
`0x1a`, explicit BSSID, index 2) and exposes the iovars `awdl_peer_op`, `awdl_af_hdr`,
`awdl_config` and
`awdl_af_rssi`. Unicast to a MAC without a peer entry in the firmware is dropped (`FW_TOSSED`); with
`awdl_peer_op ADD` it is confirmed. Cross-checkable against
[AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md): without
AWDL firmware, the driver has to keep its own peer table and decide which unicast goes out. Also,
the
first AWDL session after creating the interface does not transmit until AWDL is turned off and back
on (hence its `awdl-warmup` service); whether the `IO80211AWDLPeerManager` depends on something
similar is `?`.

## 8. What it does not contribute

- **OWL and opendrop**: owly and winawdl already collect what is useful from OWL (winawdl reuses
  it as-is) and
  opendrop is only the AirDrop layer. Both are GPL-3.0, incompatible with linking to the GPL-2.0
  code of
  Realtek.
- **Project Zero 2017**: it is about the BCM4355C0 firmware of iOS 10.2 and mentions AWDL only in
  passing, as
  a host-driven surface. For the `IO80211AWDLPeerManager` of the kernel, «An
  iOS zero-click radio proximity exploit odyssey» (Project Zero, 2020) is more relevant, not
  reviewed here `?`.

## 9. Reuse and licenses

No code fragment of these projects has been copied into the repository. If one were reused
for test tools (capture reader, frame replica), owly, winawdl (outside `radio` and
`awdl`) and ESP32Drop allow it with their license notice.

GPLs only impose conditions when distributing. A separate build for private use, not
published nor delivered to third parties, may mix GPL-3.0 code (OWL, opendrop) with the GPL-2.0
of
Realtek without violating them; what cannot be done is publishing that binary or the mixed tree.
The real
limit is technical, not legal: on macOS the protocol is run by `IO80211AWDLPeerManager`, so a
second AWDL engine inside the kext would compete with it for the same frames and the same
schedule, and
`omdrop-awdl` are patches to a Linux Broadcom driver with no equivalent on this hardware. Where a
private build does make sense is in tools outside the kext (for example, a capture reader or an
emitter of test frames).

## 10. Effect on the test order

| Step of [AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md) §4 | With an ESP32-S3 | With the owly captures |
|---|---|---|
| 1. RX of action frames on the social channel | Only after step 2: the ESP32 is silent while it has no master (or with a modified build that advertises as master) | Only synthetic injection at `actionFrameInput`, without real times |
| 2. TX of action frames and NDP | Yes, and it goes first: if the ESP32 elects the Mac as master, frames go out with a valid format and times | No |
| 3. Events 41/42/43 | Partial: the ESP32 only stays engaged if the Mac's 16 TU cadence is stable | Reference values from §4.1 |
| 4. Election and PSF/MIF | Yes: exit of `peers:0` when the ESP32 starts emitting MIF | No |
| 5. AirDrop | Reception on the Mac from the ESP32 `?` (its send needs BLE to wake the receiver) | No |

## 11. Review of 2026-11-03

| Project | Revision | License | What it is |
|---|---|---|---|
| `Mr-Hafid/Crosswind` | `406272f` (2026-11-02) | GPL-3.0; AWDL and AirDrop crates MIT | Renamed fork of owly with a menu bar app (Tauri) and a `crosswind-core` crate for platform detection |
| `kgp-macPro/AirportItlwm-Tahoe` | `9698dc4` (2026-10-31) | GPL-2.0 | AirportItlwm 1.0.0 for Tahoe on the AX210, with `awdl0` created |

**Crosswind.** `crates/crosswind-awdl/src` is the `owly-awdl` of `9a676e1` with the name changed:
after
normalizing the names, the only difference is the length of the ARPA TLV in a test (8 → 13, from
the
host name). The three captures of `tests/captures/` are identical byte for byte (same SHA-1) to
owly's, already contrasted in §4 and in
[AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md). There are no new
protocol data. The only new thing for this work is the report
`hardware/reports/crosswind-test-rtl8xxxu-1792818572.json`: a Realtek USB `0bda:f179` (RTL8188FU,
driver `rtl8xxxu`, channel 6, exclusive mode) passes «phone heard», «shown on the phone» and
«phone -> computer», and fails «computer -> phone». It is another Realtek USB radio that receives
from an iPhone with the AWDL in the host; the report gives no cause for the send failure.

**AirportItlwm-Tahoe.** It does not use the native `IO80211Family` of Tahoe: the Tahoe target
links
`IO80211FamilyLegacy` (`AirportItlwm-Monterey-Info.plist`) and needs the Ventura environment
restored
by the OCLP-CustoMac root patches (`INSTALL.md`). `awdl0` is born in `setVIRTUAL_IF_CREATE`
(`AirportSTAIOCTL.cpp`): for the role `APPLE80211_VIF_AWDL` it creates an `IO80211P2PInterface`,
starts it with prefix `awdl`, sets the infrastructure channel to 149/80 MHz, does `attach(this)`
and
`registerService()`. The `setAWDL_*`/`getAWDL_*` of `AirportVirtualIOCTL.cpp` only store or log
values, and `requestPacketTx` has the send commented out for the AWDL role: there is no
synchronization, election
or AWDL TX in the driver. The project itself does not claim AirDrop. What it documents as
«AWDL/P2P» is
`awdl0` active, `wifip2pd` attached and mDNS packets on `awdl0`. That path (legacy family,
`IO80211P2PInterface`) does not exist in the `IO80211Family` of 26.7.1 used by
`RTL8814AirLayer.kext`, where the
role 6 goes through the path of [AWDL_ROLE6_RE.md](AWDL_ROLE6_RE.md). It serves as reference for
an itlwm backend on the legacy family, not for this kext.

## 12. Articles on AWDL and Continuity over BLE (review of 2026-11-03)

| Work | What it contributes here |
|---|---|
| Stute, Kreitschmann, Hollick, «One Billion Apples' Secret Sauce», MobiCom 2018 (arXiv 1808.03156) | AWDL on macOS 10.12/10.13 with Broadcom: frame format, election, sync, channel sequence, measurements |
| Martin et al., «Handoff All Your Privacy», PoPETs 2019 (arXiv 1904.10600) | Continuity messages over BLE (Nearby, Handoff, Wi-Fi), flags distinguishing a Mac from an iPhone |
| Celosia, Cunche, «Discontinued Privacy», PoPETs 2020 (1) | Format of the AirDrop BLE message and its hashes |
| hexway, «Apple bleee», 2020 | Capture of a real AirDrop advertisement, consistent with the previous format |
| Shkurko, «Магия AirDrop», Habr 993750, 2026-02-09 | Outreach without driver data: BLE, UWB, `awdl0` on 149, and that Apple adopts Wi-Fi Aware profiles so AirDrop works with Android. Claims, without sources, a post-quantum key exchange in 2026 `?` |
| srpopty, «AWDL协议及漏洞分析», blog, 2020-08-11 | Chinese summary of MobiCom 2018 and USENIX 2019, no new data. Its «metric = random + counter» does not match `runParentElection` on 25G241 (§12.4) |

The four describe macOS 10.12-10.15 and iOS 10-13. What follows is marked as from the article
except
where the check on the 25G241 kernelcache is noted.

### 12.1 AWDL (MobiCom 2018)

- **Division of labor.** In those versions `IO80211Family` built and analyzed the frames and
  kept the state machine; the Broadcom driver did the time-critical part, sync included.
  Here that role belongs to the layer (`RTLAwdInterface::alignClock`, `txTick`).
- **Fixed header.** `TTx,PHY` (+8) and `TTx,Target` (+12), in µs. `TTx,Target` is set when
  creating the
  frame and `TTx,PHY` when enqueueing it; their difference approximates the TX delay of the
  sender. The slave estimates the start of the next AW as
  `T_AW = t_AW·1024 − (TTx,PHY − TTx,Target) + T_Rx`, where `t_AW`
  is the TU the TLV 4 gives until the next extended AW and `T_Rx` the arrival. Accepted error: 3
  ms
  (misalignment metric in `awdl_recv_action_frame` of Broadcom). On 25G241,
  `parseAwdlSyncParamsTlv` reads both fields and stores the difference as `sync_contention`
  ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) §4).
  - Comparison with the layer: `alignClock` takes `T_Rx` when draining the RX queue (the thread-call
    thread), not at arrival, and does not subtract `TTx,PHY − TTx,Target`. The layer puts the
    same value in both fields, so on TX the difference is 0. How much the draining delays is
    unmeasured `?`.
- **Election.** A node starts with metric 60, listens 2 s on the social channels and then elects
  a metric at random in a range depending on the AWDL version: 405-436 in v2.x and 505-536 in
  v3.x.
  The highest wins. Without a master frame for 96 AWs (≈1.5 s), another node takes its place.
  TLV 20
  (sync tree) lists the path to the top master to avoid loops.
  - The range of 26.7.1 is unchecked `?`. `awdlsim` prints the metric `awdl0` advertises
    ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §4).
- **RSSI.** Frames below the edge sync threshold, -65 dBm (or -78 with AirPlay), are
  dropped, and the master's frames get a 5 dB margin. It matches the default `rssi_threshold` of
  owly (-65).
- **Windows.** AW of 16 TU, presence mode 4, extension windows of 16 TU, slot of 64 TU,
  sequence of 16 entries with `step` 3 and padding `0xffff` (64 AWs ≈ 1 s). MIFs follow the
  announced sequence; PSFs go out at any time (period 110 or 440 TU).
- **TLVs.** 4 sync params, 5 and 24 election, 18 sequence, 20 sync tree, 6 service params,
  2 service response, 16 ARPA, 12 data path state, 7 HT, 17 VHT, 21 version.
  The 17, which iOS sends and owly does not (§4), is the VHT capabilities one. Version 3.x on
  macOS 10.13/iOS 11;
  device class 1 macOS, 2 iOS.
- **IPv6 addresses.** Link-local derived from the SA of the AFs, without NDP.
- **Logging.** The boot-args `awdl_log_flags`, `awdl_log_flags_verbose` and
  `awdl_log_flags_config`
  (all `0xffffffffffffffff`, the last `1`) plus `wlan.debug.enable=0xff` give the detailed
  logging of
  the manager. Checked on 25G241: `IO80211AWDLPeerManager::initWithInterface` reads them with
  `PE_parse_boot_argn` (`0xffffff80021c1a78`, `0xffffff80021c1acb`, `0xffffff80021c1d7b`), and
  `wlan.debug.enable` is read by `IO80211_io80211isDebuggable` (`0xffffff8002196b91`) and
  `IO80211Controller::start` (`0xffffff800229204c`). The manager is the generic one, so they also
  apply with the `awdl0` of the layer.

### 12.2 AirDrop BLE advertisement (PoPETs 2019 and 2020, hexway)

- **Container.** AD structure of manufacturer data (`0xFF`) with the Apple identifier
  `0x004C` and one or more Continuity messages in TLV: 1-byte type, 1-byte length, data.
- **AirDrop message, type `0x05`, length `0x12`.** 8 zero bytes, version (1 byte, `0x01`), four
  2-byte hashes and a final zero byte. Each hash is the top 16 bits of the unsalted SHA-256 of
  an
  email or phone of the iCloud account of the sender. hexway capture:
  `000000000000000001123412341234123400`.
  - This resolves the `?` of
    [AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md) §4: the 18
    typical bytes built by `-[WPAWDL startConnectionlessAWDLServiceAdvertisingWithData:]` are
    this body.
- **When and from where.** The sender sends it when opening the share sheet. It uses a
  non-resolvable random address that does not change until turning the Bluetooth off and on. A
  receiver on «Everyone» does
  not need the hashes to match
  ([AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md):
  «In Everyone mode, start server»).
- **Other messages.** Nearby Info (`0x10`) goes out whenever Bluetooth is on; its first data byte
  says whether Wi-Fi is on (`0x1C`) or off (`0x18`) on iOS 12. The BLE flags separate
  a Mac (LE only) from an iPhone (LE and BR/EDR).
- **Consequence for the tests.** Any BLE sender advertising `0x004C`/`0x05` with that body
  should trigger `handleFoundHashes:rssi:` in the `sharingd` of the Mac, with the receiver on
  «Everyone» and the
  UB400 dongle active: it can be the A53 with a BLE advertisement app, or an ESP32. Untested
  `?`. It is the
  BLE half of the test the virtual peer does not cover. The app is `Tools/airdrop_beacon`
  ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §6).

### 12.3 Apple developer forums (CoreWLAN and Networking)

- **Thread 847171** (September 2026), macOS 27.2 beta on an M4 Pro. With the AirDrop discovery
  active, Wi-Fi throughput drops and gateway latency spikes. `sudo ifconfig awdl0 down`
  removes them, and turning AirDrop off while leaving AWDL up also does. Feedback FB24842970; the
  only Apple reply points to that report.
  - The en2 spikes under time sharing (`27284f5`, `abd2dd7`) are therefore a behavior Apple
    hardware also has, not only this radio.
- **Thread 818603** (April 2026), M5 Pro with Apple's own Wi-Fi chip (N1). The driver identifies
  as `IO80211_driverkit-1540.16`: on that hardware IO80211 runs as DriverKit, not as the
  `IO80211Family` kext this layer uses.
  - If macOS brings that model to all hardware, the current path would stop working. On 26.7.1
    x86_64 the family is still a kext. Without more data `?`.
- **Thread 45283** («Wi-Fi Fundamentals», revised in 2021). It only confirms that AWDL is what
  `includePeerToPeer` of Network framework and Multipeer Connectivity use, and that a single radio
  changes channel to serve it.

### 12.4 Attacks on AWDL and AirDrop (USENIX Security 2019)

Stute, Narain, Mariotto, Heinrich, Kreitschmann, Noubir and Hollick, «A Billion Open Interfaces
for Eve
and Mallory: MitM, DoS, and Tracking Attacks on iOS and macOS Through Apple Wireless Direct Link»,
with their slides. It is the origin of OpenDrop.

- **AirDrop flow.** The sender advertises its hashes over BLE. A receiver on «Everyone», or with
  some hash in its agenda, activates AWDL. The sender searches for `_airdrop._tcp` by mDNS over
  AWDL and does
  `POST /Discover`, `/Ask` and `/Upload` over HTTPS with a client certificate. The receiver has to
  be unlocked to be discovered.
- **Full BLE advertisement (figure 4).** Flags (`02 01 1b`) and manufacturer data
  (`17 ff 4c 00 05 12`, 8 zeros, `01`, four hashes, `00`). Sent as `ADV_NONCONN_IND`. «Devices
  in everyone mode will enable AWDL immediately after receiving any AirDrop BLE advertisement».
  - `Tools/airdrop_beacon` emits that kind of advertisement (non-connectable and non-scannable,
    checked with
    `dumpsys bluetooth_manager` on the A53). It asks for the flags structure with
    `setDiscoverable(true)`
    (API 34+), but Android sets its own value, not `0x1b`. Whether it goes on the air and whether
    `sharingd` looks at the flags is unchecked `?`.
- **Unauthenticated connections.** AirDrop accepts a sender with a self-signed certificate and no
  Apple-signed registration if the receiver is on «Everyone»; that is how OpenDrop works.
- **Election.** The paper gives `cA > cB ∨ (cA = cB ∧ mA > mB)`: counter first, metric after. The
  counter starts at random and grows while the node is master. MobiCom 2018 speaks only of the
  metric,
  and owly (patch `cw-04`) observed in a recent iOS capture that the metric decides first.
  - **On 25G241 the metric decides**, checked in `IO80211AWDLPeerManager::runParentElection`
    (`0xffffff80021ec0e4`).
    - Among candidates with different roots the highest root metric wins (peer+579); on
      equality, the highest root MAC (peer+559).
    - With the same root, the root counter (peer+571, «RLFC») and then fewer hops (peer+575).
    - The local node is root if its metric exceeds the best candidate's, or is equal and its MAC is
      greater or equal.
  - `IO80211AWDLPeer::parseAwdlElectionParamsV2` (`0xffffff80021ba26c`) copies the whole TLV 24,
    43
    bytes, to peer+556, so those fields are those of the TLV value from +0: root
    MAC
    (+0), parent (+6), root counter (+12), hops (+16), root metric (+20) and own metric
    (+24). It discards a TLV of the same root with a counter lower than the stored one.
  - `IO80211AWDLPeerManager::isPeerSafeForElection` (`0xffffff80021eade6`) keeps out of the
    election, with «Discarding Legacy peer», a peer with AWDL version (peer+73) lower than `0x33`
    or with a root
    counter of 0.
    - The owly node advertises version `0x34`, but its counter is always 0. As master of
      itself, the Mac would discard it. `awdlsim` uses a random non-zero counter for that reason
      (`--counter`, `Config::counter`).
- **Apple fixes.**
  - The brute force of BLE hashes against «Contacts Only» was fixed in iOS 12.2.
  - CVE-2019-8612: desynchronization, with a fake master giving each victim different sync
    parameters.
  - CVE-2019-8567 and CVE-2019-8620: tracking by MAC and host name.
  - CVE-2018-4368: crash from corrupt frames.
  - The AirDrop MitM was fixed in iOS 13 beta.
  - The desynchronization exploits that AWDL does not authenticate frames. The current kernel
    could filter incoherent masters `?`.

### 12.5 Wi-Fi and AWDL on real Macs (hpese.club, 2026-03-25)

«不做背锅侠，Wi-Fi与AWDL的爱恨纠葛», by Lucas, on the HPE Aruba engineers blog. It measures the effect
of AWDL on macOS 26 with Apple Macs. Local copy saved by the user on the desktop.

- **AWDL state on Tahoe.** `sudo wdutil info` has an AWDL section: MAC, AirDrop mode,
  `AWDL Enabled`, `Interface Name: awdl0`, `Schedule State: Infra Priority` and the channel
  sequence.
  - Example with the network on 36++: `36++ 36++ 149++ 0 0 0 0 36++ 6 36++ 149++ 36++ 0 0 0`. The
    network channel occupies most slots, 149 at 80 MHz two and 6 one.
  - The strings `Schedule State` and `Infra Priority` are in the 25G241 kernelcache; what
    `wdutil` shows with the layer's `awdl0` is untested `?`.
- **Logging.** The article filters the Console by «AWDL is» to see `AWDL is on/off`. On 25G241 the
  kernelcache has `AWDL is %s`, `AWDL is OFF`, `AWDL is SUSPENDed` and a dozen variants.
- **BLE trigger.** With the Mac's Bluetooth on, opening AirDrop on a nearby iPhone activates
  AWDL on the Mac, even with its AirDrop off («AWDL is On» in the log). Turning off the
  Mac's Bluetooth makes AWDL go off in a few seconds. For the test of
  [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §6, this suggests watching «AWDL is» besides
  `sharingd`.
- **Cost on a real Mac.** UDP flow every 1 ms against a remote server:

  | AWDL | Background | Loss | Average latency | Packets > 15 ms | Maximum jitter |
  |---|---|---|---|---|---|
  | off | — | 0.05 % | 8.1 ms | 1.3 % | 20 ms |
  | on | — | 4.48 % | 38.2 ms | 40.2 % | 240 ms |
  | off | 40 Mb/s | 0.07 % | 9.0 ms | 3.1 % | 22 ms |
  | on | 40 Mb/s | 9.46 % | 69.9 ms | 59.6 % | 422 ms |

  In comparison, the layer's time sharing left ping spikes to the gateway of 15.7 ms
  (`abd2dd7`). The measurements are not directly comparable (local ping vs UDP over the
  internet).
- **Comments.** A reader sees `awdl0` active after every sleep on an M5 with 26.5.1. Another
  turns off channels 44 and 149 on their company's APs.

### 12.6 Automated protocol reverse engineering (Kleber, 2024)

Doctoral thesis of Stephan Kleber (Ulm), «Automation of the Reverse Engineering of Unknown Binary
Network Protocols»: message segmentation (NEMESYS, with PCA refinement) and message type
clustering. AWDL is one of the evaluation sets next to DHCP, DNS, NTP or SMB. It contributes no
AWDL data. It would be applicable to unnamed TLVs of own captures (for example, those of the
`--capture` of `awdlsim`), but it is not used here.

### 12.7 AirDrop on macOS 26.3 (CISPA, 2026)

Ale Ebrahim and Tippenhauer (CISPA), «Protocol Prying: Systematic Vulnerability Research in the
Apple
AirDrop and Android Quick Share Proximity Transfer Protocols». They analyze `sharingd` and
`Sharing.framework` on macOS 15.7.4 and 26.3 and on iOS 18.1 and 26.3. The artifacts, AIRFUZZ
included,
are on Zenodo (record 20442029). Local copy: `~/Downloads/airdrop_research.pdf`.

- **Stack.** BLE/AWDL; IPv6 link-local on `awdl0`; TLS 1.2/1.3 with self-signed certificates,
  without
  verifying the name nor requesting a client certificate; HTTP/1.1; binary plist; DVZip/gzip; CPIO
  `newc` extracted with BOM. On 25G241 the `/Upload` fails with `newc` («Decompression failed») and works
  with `odc` (`070707`); it also requires the HTTP header `TotalBytes`
  ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §8.1).
- **Service.** The receiver publishes `_airdrop._tcp.local.` by DNS-SD over AWDL, with some 25
  keys
  TXT. The `flags` key is a bitmask, e.g. `0x3FB`, whose bit `0xb` is `SupportsUPP`.
  `sharingd` listens on TCP **8770** over AWDL.
  - «Receiving off» does not open that port.
  - On «Everyone» the whole API is accessible unauthenticated.
  - On macOS the «Everyone» mode does not expire after 10 minutes.
- **API.** Seven POSTs: `/Discover`, `/Hello`, `/Ask`, `/Upload`, `/Exchange`, `/SharedIdentity`
  and
  `/Error`. `SDAirDropConnection` keeps one slot per connection for Discover, Ask and Upload.
  - `/Discover` answers `StatusCode` 100 (Everyone), 200 (contact) or 401 (Contacts Only
    without
    match).
  - `/Ask` shows the prompt to the user and waits up to 15 s.
  - `/Upload` goes on the same TLS connection as `/Ask`, unless `SupportsUPP` is announced.
  - An unknown route crashes `sharingd` (V1, `fatalError` in the Swift router).
- **Acceptance.** Every delivery needs the user to accept, or the same Apple ID verified with
  `SFAppleIDVerifyCertificateChain`. They tried ten ways to bypass the prompt and none worked.
- **Consequence for the tests.** A non-Apple sender can deliver a file to this Mac
  if it can speak TCP with the link-local of `awdl0` on port 8770: self-signed TLS, `/Ask` and
  `/Upload`, accepting the prompt by hand. With `awdlsim` an IP bridge is missing: a `utun` that
  carries the
  node's packets to a Mac TCP stack, and a client like OpenDrop. It is the end-to-end AirDrop test
  without an iPhone; it is not implemented `?`.

### 12.8 Continuity security guide (SEEMOO, reproduced on Zhihu)

Chinese translation of the work of Heinrich, Stute and others on Handoff, the universal clipboard
and
the Wi-Fi password sending. The tools are in `seemoo-lab/apple-continuity-tools`.

- **To debug with the UB400.** PacketLogger (Additional Tools of Xcode) logs the Bluetooth HCI of
  the Mac into `.pklg`, which Wireshark reads, and Bluetooth Explorer shows nearby BLE
  advertisements. They serve to check whether the advertisement of `Tools/airdrop_beacon` reaches
  the Mac.
- **Other services.** Handoff and the clipboard use `_companion-link._tcp.local` over AWDL, with
  TXT keys `rpBA`/`rpAD`, after a type `0x0c` BLE advertisement encrypted with AES-GCM. They need
  the same iCloud on both machines.
- **Another Zhihu article** («一晃13年过去了，苹果的Airdrop依然很坚挺», 2026-02-07). Outreach without
  new
  data: it says AWDL uses «usually 44».

## 13. Review of 2026-11-08

A search for news on AWDL and AirDrop outside Apple. What could be checked in the original material
carries no mark; what only a press article says names its source.

### 13.1 owly, new review

`t4t5/owly` (MIT, last push 2026-11-03) absorbed `omdrop-owl`, whose repository now redirects there.
Its `docs/SPEC.md`, `conformance.md`, `implementation.md` and `open-questions.md` are copied in
`~/re_cache/work/awdl_refs/owly_docs`.

- **Modes.** In shared mode Wi-Fi stays associated and the AP must be on 6, 44 or 149. Frames go out
  through a monitor interface carrying the station's MAC, which is what gives the ACKs. The node
  advertises only the slots the phone offers on that channel. In exclusive mode Wi-Fi goes down, and an
  ad hoc cell or a hidden AP on the AWDL BSSID gives the ACKs.
- **Hardware per its table.** Shared mode works on RTL8851BE, RTL8852AE, RTL8852BE, RTL8852CE, MT7925
  and AR9287. Exclusive works on Intel 9260 to BE200, on RTL8822BE, RTL8922AE and RTL8852BU (USB), and
  on MT7612U (USB). RTL8821CE, RTL8821AU, Qualcomm WCN6855/WCN7850 and Intel 7265/8260/8265 fail. The
  RTL8814AU is not listed.
- **`SPEC.md` rules that touch the layer:**
  - AWDL-12: each slot advertises only the channel the node will be on, and 0 in slots it does not
    serve, since a peer sends data into every slot it believes shared. The layer already puts 0 in slots
    0 and 8 with `en2` not associated ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17).
  - AWDL-16: unicast goes inside a shared AW with a 3 TU guard at the edges, as the layer does since
    `8E4C4AE5`. Multicast keeps a 16 TU guard and goes in slots 0 and 10, except on a node that stays on
    one channel, which sends it in any slot it serves with 3 TU.
  - AWDL-17: a peer is valid only after a MIF and a Version TLV with nonzero version and class. It is
    dropped after 5 s of silence, and data counts as hearing it. This is owly's rule, not an iOS
    measurement. The scan gap of layer `001EF268` is 0.38 s.
  - AWDL-18: a TCP SYN from a peer that is not valid yet is held for up to 2 s.
- **Published throughput.** On MT7925 with Wi-Fi associated: about 490 kB/s up and 1.3 MB/s down
  (BestHub summary, 2026-10-13; not in the repository). The layer gives 507 to 542 KB/s up in data mode
  ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md)).

### 13.2 AWDL in `brcmfmac` for T2 Macs (Linux)

Andre Eikmeyer posted a patch series to `brcmfmac` on 2026-11-05 (Phoronix, same day; the list thread
was not found). Per the cover letter, on those chips the firmware keeps the AWDL schedule, sends the
action frames the host hands it and carries the data. Election, discovery and AirDrop run in user
space, in `t2-drop` (Rust, kaiT2en project). It turns on with firmware that announces `awdl` and accepts
`interface_create` version 3. The BCM4350 in linux-firmware announces `awdl` but rejects that
`interface_create`.

It is the same split seen on the iPhone ([AWDL_IOS_27_HOST.md](AWDL_IOS_27_HOST.md) §1), and a reference
for a port on another driver with Broadcom firmware.

### 13.3 CISPA artifact on Zenodo (Airfuzz)

This is record 20442029 of the paper in §12.7. It is copied in
`~/re_cache/work/awdl_refs/airfuzz/artifact-evaluation`; nothing from it was run. A checked report,
whose 262 `file:line` citations resolve and whose 15 quotes match, is in
`~/re_cache/work/awdl_refs/airfuzz/REPORT.md`.

- **No AWDL on the air.** The tests run over the `awdl0` of two Apple devices. The AWDL frames in
  `airfuzz/targets/kernel.py` are synthetic: wrapped in Ethernet with ethertype `0x88B7`, without the
  AWDL fixed header, and with TLV numbers swapped (`AWDL_TLV_VERSION = 0x14`,
  `AWDL_TLV_SYNC_TREE = 0x11`, lines 149-150). Not a format reference.
- **No PDF.** The `paper/` directory its `README.md` names is not in the zip.
- **`/Ask` format on iOS 18** (`protocol-docs/RESEARCH_DOCUMENT.md:45-53`):
  - `TransferID` must be a dictionary `{"id": "<UUID>"}`;
  - `TransferType` must be `{"files": {}}`;
  - `Items` must be an empty list;
  - `FileBomPath` carries the `./` prefix;
  - `/Upload` must go over the same TLS connection.

  With `TransferID` as a string, `/Ask` returns 200 without showing the prompt, and the `/Upload` goes
  nowhere. With `/Upload` on another connection, the iPhone answers with an RST (lines 298-305).
- **DVZip.** `airfuzz/protocol/encoder.py` builds CPIO `odc` (`070707`) with the `.` entry, the file
  `./<name>` and `TRAILER!!!`. It splits that into `0x10000`-byte blocks, compresses each with zlib
  (level 6), and prefixes each with its 4-byte big-endian length (lines 75-80). This matches what was
  measured on 25G241, where `newc` fails and `odc` works ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md)
  §8.1).
- **`sharingd` routes on macOS 26.3**
  (`vulnerabilities/V1-airdrop-fatal-assertion/ROOT-CAUSE-ANALYSIS.md:178-186`): `/Discover`,
  `/Hello`, `/Ask`, `/Upload`, `/Exchange`, `/IdentityShare` and `/Error`. Any other path with a body
  ends in `fatalError`. §12.7 says `/SharedIdentity`, taken from the paper; in the 26.3 binary the route
  is named `/IdentityShare`.
- **`StatusCode`** appears in `airfuzz/targets/macos_local.py:591` next to
  `ReceiverMediaCapabilities`, which backs the `/Discover` answer in §12.7.

### 13.4 AirDrop on Tahoe with an OCLP fork (InsanelyMac)

kgp's thread (since 2025-12-04, with 26.7 notes) gets two-way AirDrop only with a Broadcom
BCM943602CDP. The patch restores Apple's wireless stack on the system volume and requires AMFIPass
1.4.1 with `-amfipassbeta`, `DisableIoMapper`, `SecureBootModel` disabled and a replacement
`IOSkywalkFamily`. It does not call Intel AX210 reliable. It is the same route as
[AWDL_REF_SIMPLEBCM.md](AWDL_REF_SIMPLEBCM.md): a Broadcom card, not another vendor's driver.

### 13.5 Wi-Fi Aware instead of AWDL

Under the EU interoperability order, Apple must implement Wi-Fi Aware and phase out AWDL (Ditto blog,
2025). There is no date for retiring AWDL, and nothing on what happens to AirDrop. iOS 27.2 still
carries `IO80211AWDLPeerManager` ([AWDL_IOS_27_HOST.md](AWDL_IOS_27_HOST.md)).

### 13.6 Projects being followed

- **AirPort_RTW89 (FranciumOS-org).** No new code since 2026-11-02. The last release is v0.5.0-BETA,
  of 2026-10-30. The AirDrop build from the 2026-11-04 video is not published.
- **AirportRTW (souissiak1-maker).** The latest is V2.3.1, of 2026-11-04, with AWDL off: no calls from
  production code and no capability advertised. On Discord it was said on 2026-11-06 to do AirDrop; no
  code backs it. It adapts this repository's public `AWDLInterface.h`, credited in the code.
- **Nothing useful new:** AMD_Vanilla for 26.7, and native Realtek USB drivers beyond
  TahoeRealtekWiFi.

### 13.7 AWDL in an itlwm fork (crazyhulk, AX210)

A pcbeta thread (2084025, 2026-11-08) says there is two-way AirDrop with an Intel AX210 on Tahoe. Only the
author makes the claim: in the thread, another AX210 user could not reproduce it, and AX200, AX201 and AX211
fail.

- **No code published.** The `awdl` tag of `crazyhulk/itlwm` points at `7204c69d`, the same commit as the
  `main` of `laobamac/itlwm`, with no AWDL. The `awdl-beta` release only carries an attachment,
  `AirportItlwm.kext.zip` (15,138,781 bytes, SHA-256 `2a027394…78756a40`, x86_64, version 2.4.0, binary of
  2026-11-08 06:52). `BlueToolFixup-FixA2DP` `ble-beta` is the same: a 12 KB binary.
- **Read in IDA** (copy and database outside the repository, in `~/re_cache/work/awdl_refs/crazyhulk_itlwm`).
  The C++ symbols are there, and nothing is taken from `AWDLInterface.h`.
  - **Control.** `AirportItlwmAWDLInterface` is an `IOSkywalkEthernetInterface` that the family creates with
    role 6 (`createAWDLInterface`, `0x6b80`). It serves the AWDL ioctls (`SYNC_PARAMS`, `SYNC_FRAME_TEMPLATE`,
    `SYNCHRONIZATION_CHANNEL_SEQUENCE`, `ELECTION_*`, `OOB_*`) and builds PSFs and MIFs itself
    (`buildAWDLFrame`). It links `com.apple.iokit.IO80211Family` 1.5, the older family that AirportItlwm
    injects on Tahoe, not the native one of 26.
  - **Data without Skywalk's P2P queue.** Its `awdl0` is not the family's Skywalk interface. It is a classic
    `IOEthernetInterface`, `AirportItlwmAWDLEthernetInterface` (prefix `awdl`), attached with
    `IONetworkController::attachInterface`. `outputAWDL` (`0x13b0`) puts each mbuf in a ring of 32
    (`enqueueData`, `0x6a70`). `flushData` (`0x5bc0`) builds the 802.11 data frame: AWDL BSSID, LLC
    `AA AA 03 00 17 F2 08 00`, header `03 04`, sequence and ethertype. It then injects it raw on the current
    channel; a return of 35 leaves it queued. This avoids the TX queue of the family's P2P interface, where
    `dequeueTxPackets` returns null in the AirportItlwm work for Tahoe (per an r/hackintosh comment
    about OpenIntelWireless/itlwm#1062; not checked here).
  - **RX.** `inputActionFrame` (`0x14c0`) hands AFs with OUI `00 17 F2` type 8 to the family's management
    frame pool (`getMgmtFramePool`, `AWDLBufferABI`), with channel, RSSI and host receive time.
    `syncFromFrame` follows the master. `isSyncMaster` is only true with no master or with its own
    (`0x2fe0`): there is no election of its own.
  - **Timing.** It keeps the AW timeline with timers (`scheduleNextAW`, `0x3240`) and sends 1 ms after the
    requested instant (`scheduleTx`, `0x57c0`). Associated on another channel, every N AWs it asks the
    firmware for a remain-on-channel to the AWDL channel (`maybeListen`, `0x4160`); on the same channel it
    does not hop.
  - **Boot-args.** `-itlawdl` (start), `-itlawdlaw` (AW timeline), `-itlawdllisten`, `-itlawdltx`,
    `-itlawdldata`, `-itlawdlp2p` and `-itlawdlpromisc`. Without them, none of the above runs.
- **For this repository.** The data format matches the one the layer already uses, and the native path of
  26 does not have the P2P queue problem: the architecture does not change. For a port on the older family,
  such as itlwm, the classic `awdl0` is the way around that queue. That AirDrop completes over it is not
  checked `?`.
- **Synchronisation, compared with the layer's `alignClock`.**
  - **The same.** Both follow only the node that announces itself as master in its TLV 4, and take from it
    the count of TUs to the next boundary and the AW number (`syncFromFrame`, `0x29e0`).
  - **The layer does more.** It subtracts the sender's delay (PHY TX minus target TX, fixed header +8 and
    +12). The fork only publishes it as `AWDLSyncTxDelayUs`. The layer also picks the master by the
    template's election v2 and holds it for 2 s.
  - **Jump rejection, which the layer lacks.** Once synchronised, if the AW number matches and the phase
    moves by more than 6000 µs, the fork drops the frame up to twice in a row and counts `AWDLSyncOutliers`.
    It accepts the third. The layer takes every frame from the master as it comes.
  - **The chip's receive time, which the layer does not use.** `hostRxUs` (`0x2530`) starts from the
    firmware's timestamp on each frame and tracks the offset between host and chip. It lets the offset
    drop freely, raises it by at most 20 µs per frame, and resets it if it jumps by more than 20 ms. From the
    host time it subtracts how much longer than that minimum the frame took, which removes the bus's
    variable delay. The layer stamps the arrival with `awdlNowUs()` on the RX thread (`AWDLInterface.cpp`,
    line 666), after USB. The RTL8814A RX descriptor carries `TSFL`, the low 32 bits of the chip's TSF, at
    byte 20 (`GET_RX_STATUS_DESC_TSFL_8814A`, `rtl8814a_recv.h:156`). The driver does not read it:
    `pattrib->tsfl` is never set for this chip. With `en2` associated, that TSF follows the AP's and jumps,
    hence the reset at 20 ms.
  - **Status.** How much the USB delay between the air and `awdlNowUs()` varies is not measured `?`. The
    `drain` the layer logs (32 to 69 µs in the runs of 11-05 and 11-06) only covers the RX thread to
    `alignClock`. Both improvements are candidates if the iPhone shows `Peer Absence` or misalignment. The
    spread of host minus `TSFL` would be measured first.
