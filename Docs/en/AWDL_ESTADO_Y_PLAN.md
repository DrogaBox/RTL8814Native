# AWDL: status and plan

Status at 2026-11-06, before the first test with an iPhone. As a reference it replaces
[AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md), from 2026-10-30, when there was no peer over the air. Anything not seen goes with `?`.

**Update 2026-11-14.** No Apple device has been available since 2026-11-08, so everything after that date is measured against the virtual peer. Done since then: receive and originator Block Ack with A-MPDU, the rate ladder, dense presence and the radio shared with an associated Wi-Fi link (TCP over `awdl0` at 14 to 18 MB/s with Wi-Fi associated, about 19 MB/s with the radio parked at 80 MHz); after a round of unanswered Block Ack requests the next request leaves 5.5 s later (60 s before), measured with `awdlsim --ba-silent 3`. The repository is public again since 2026-11-13, with one prebuilt preview and the report tool `Tools/awdl_report.sh`. Still untested against an Apple device: Mac to iPhone, Block Ack and A-MPDU, and the receive speed. The current list of what is done and what is open is at the end of [ROADMAP.md](../ROADMAP.md).

**Update 2026-11-09.** The first test with an iPhone (2026-11-08) is in
[AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md): iPhone → Mac AirDrop works end to end (a 64 MB video at about
800 KB/s) after fixing iOS 27's length-wrapped data frames (94e83e7) and the presence mode (7333d24). The Mac did not
discover the iPhone because the layer answered "unsupported" to GET 117 (`AWDL_SYNC_ENABLED`), which wifip2pd reads once
when it creates its interface and uses as the permission to publish peer presence; fixed in b492c65 and checked with the
virtual peer: the `awdl0` neighbour entry appears on its own, no `ndp -s`. Pending with the iPhone: that it shows in the
Mac's AirDrop sheet, and the Mac → iPhone speed. Loaded layer: UUID `5A7BC8B5`, md5 `233601f6`. New reference for speed:
`github.com/Dino-VN/MtkWireless` (software AWDL with AirDrop at about 4 MB/s on MT7921): rate ladder up to MCS 7, RX
Block Ack and dense presence during a transfer.

**Update 2026-11-09, evening.** Speed changes without an iPhone, in
[AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md): the layer answers the iPhone's ADDBA and reorders, announces every
free slot during a transfer, and raises the TX rate from the chip's per-frame CCX report (driver UUID `9B662F1B`,
md5 `d06f7b4a`; layer UUID `8FB89DDD`, md5 `cfd4f92e`). Not installed yet; tested first with `awdlsim --addba` and
then in a single iPhone visit, together with the Mac's AirDrop sheet.

## 1. What is running

macOS 26.7.1 (25G241), T9UH. The router moved on its own from 149 to 153 on 2026-11-06 at 17:58, so the test's case is decided that day.

| Piece | Build |
|---|---|
| `RTL8814Native.kext` | UUID `DB92193C`, md5 `9440a90b` |
| `RTL8814AirLayer.kext` | UUID `001EF268`, md5 `540b6837` (2026-11-06); `5A7BC8B5`, md5 `233601f6` since 2026-11-09 |
| Test kit | `Tools/iphone_test.sh`, `Tools/iphone_frames.py`, commit `253929e` |

`Tools/iphone_test.sh check` gives `READY`. The rehearsal without a peer the same day gave 2743 TX frames, 0 lost, a correct capture self-test and a complete summary.
Both builds are installed; `deploy` is only needed if one changes. The script is bash: under `zsh` it gives a false `NOT READY`.

## 2. What was measured

| Stretch | Result | With what | Where |
|---|---|---|---|
| `awdl0` creation, `AWDL ON`, channel sequence | Works | The Mac itself | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) |
| Synchronisation and election | Within 1 TU, in both roles | Virtual peer and A53 | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9, [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §10 |
| PSF and MIF on the air | 8.80 PSF/s and 2.83 MIF/s on 149, in the iPhone's range | A53 in monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §9 |
| TLV 7 and 17 in the Mac's frames | Sent since layer `EEEE65F6` | A53 in monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §6 |
| RX over the air | `HEARD` and `peers 1` with the A53's PSF and MIF | A53 as peer | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §7 |
| Hardware ACK to the `awdl0` MAC | 482 frames, 482 ACKs, no retries | Router, unicast | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §13 |
| Packet size | `awdl0` at 1428, what fits in the ring | A53 in monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §14 |
| Data position in the slot | 3 TU guard at the start of the slot; in `Data` the first group shows at about TU 2.5 | A53 in monitor | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §7 |
| The manager's `Data` mode | Goes to `Data` after 1.3 s of traffic. TCP upload from 113 to 507-542 KB/s | Virtual peer | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5 |
| Data held if the peer announces another channel | 113 KB/s only in slots 0, 2 and 10 with a peer at rest, and `en2`'s network without loss | Virtual peer with `--rest` | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9 |
| Cost on `en2`'s network | 430 frames/s to the router: 0 % loss, 3.7 ms on average, 16 ms at most | Router and A53 | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9 |
| AirDrop to the Mac | 4 MiB in about 2 s, identical file | Simulation port, not over the air | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5 |
| Scans | `system_profiler SPAirPortDataType` leaves AWDL silent for 9.3 s; the kit no longer uses it | System log | [AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md) §4 |
| `en2` not associated | 8.89 PSF/s and 1.90 MIF/s in slots 2 and 10, with 3.5 s of silence every 60 s from `locationd`'s scan on layer `2E3D2058`; 0.38 s on `001EF268` (2026-11-07) | A53 in monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17 |

## 3. What only a real peer answers

- ~~Whether the iPhone accepts the Mac's PSF and MIF and shows it in AirDrop.~~ Yes (2026-11-08): the Mac shows up and receives.
- Whether the iPhone ACKs the Mac's data, and whether it uses Block Ack. It requests ADDBA towards the Mac `?` (MtkWireless
  documents it on its hardware; not measured on the T9UH).
- Data over the air, in each direction. The A53 sends no data: its firmware refuses it or goes down
  ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §11).
- Whether the iPhone's slot number matches the Mac's. If it does not, the layer holds nothing and sends anyway
  ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9).
- The real speed and the value of `held`.

## 4. Plan

### 4.1 Until the visit

The code stays frozen. A change in the layer asks for a deploy, a reboot and another rehearsal, and the test yields more on a binary already known.
The possible exception is the 3 s of quiet after a scan with no association ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17). It does not block the test, but with the router on 153 it is the case that applies. Applied in layer `001EF268` (6741f35): the silence fell to 0.38 s and `en2` rejoins on the first try.

The same day, before the iPhone arrives:

1. `Tools/iphone_test.sh check`, which must give `READY`.
2. Look at `en2`'s channel, since the router moves on its own. `check` says which case applies.
3. `awdlsim` stopped and the A53's BLE advertisement off.

### 4.2 The visit

[AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md): 15 to 20 minutes, with `sudo Tools/iphone_test.sh run` going. If there is time, the A53 stays as a witness with
`telefono/vigia_mac.sh`, since it separates an iPhone that does not send from a frame that is sent and does not reach the layer.

### 4.3 Afterwards, depending on what happens

| Result | Next work |
|---|---|
| No `HEARD` | See with the witness whether the iPhone was sending and on which channel. Repeat not associated, or on 6. |
| `HEARD` without `peers 1` | Compare the saved frames with the manager's rejection (`BAD OUI`, `Invalid frame`) and with [AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md). |
| `peers 1`, and the Mac does not show on the iPhone | Compare the Mac's PSF and MIF with the iPhone's, TLV by TLV, in `frames.pcapng`. This can go on without the iPhone at hand and be repeated on another visit. |
| The Mac shows, no `TCP to 8770` | Resolution and unicast data: `ndp -an`, mDNS in `awdl0_ip.pcap`, and the `sent`, `dropped`, `fail` counters. |
| A file passes, in one direction or both | §4.4 follows, with the real figures. |
| **Real result (2026-11-08):** iPhone → Mac passes; the iPhone does not show on the Mac | Cause and fix in [AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md) §3. Next visit: the Mac's AirDrop sheet and the Mac → iPhone speed. |

Even if the test fails early, the run's files are enough to go on. The folder holds the iPhone's name and addresses and goes to no repository.

### 4.4 With data over the air

In this order, since each point depends on a figure from the one before:

1. Check slot alignment and `held` with the iPhone, and correct the hold if needed.
2. Make the air budget count retries. Today it does not, and a peer that loses frames can still fill the air (`?`, [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9).
3. Raise the data rate. At 12 Mb/s the ceiling is 568 KB/s ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §6), and going past it asks for another rate in the radio.
4. Time sharing over the air with the network off the social channels ([AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) §8), and the `Data` sequence in that case, unmeasured.

### 4.5 Other macOS releases

The final goal is macOS 14 onwards, and everything measured is 25G241. Each build needs its own vtable layout and offsets
([AWDL_CARD_CAPS_14_15.md](AWDL_CARD_CAPS_14_15.md), [AWDL_DATA_PATH_PORT_14_15.md](AWDL_DATA_PATH_PORT_14_15.md)). It comes once the path is proven with a real peer on 26.7.1, so as to port
something known to work.
