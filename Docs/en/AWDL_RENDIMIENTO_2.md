# AWDL: throughput, part two (data pump and outbound A-MPDU)

Status: loaded and measured with the virtual peer on 2026-11-11 (§7): the software path goes from 1.5 MB/s
to about 7.5 MB/s at 20 MHz and about 19 MB/s at 80 MHz. On the air the MAC emits the
aggregates (§8); that an iPhone receives them is not tested. What is left is in §6. With the radio shared
with a BSS there is a new slot share and a driver defect fixed: 15.5 MB/s with `en2` associated (§9). The return to the
AWDL channel during a network scan without an association is loaded and measured with the virtual peer: the
4 s gap of each full scan becomes holes of 0.25 to 0.6 s (§10). The radio stopped receiving once after an
association, with the cause not proven and a guard loaded (§11).

Continues `AWDL_RENDIMIENTO_1.md`. Files: `Source/AirLayer/AWDLInterface.cpp`,
`Source/AirLayer/AWDLBaOrig.h`, `Source/Driver/AWDLTx.c`, `Source/Driver/RTLRadioABI.h`,
`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c`.

## 1. What limited sending

Measured on 2026-11-09 with the virtual peer: about 1135 frames/s from the Mac to the peer, 1.5 MB/s,
whatever load was offered. The causes, read in the code:

| Cause | Where | Effect |
|---|---|---|
| Data left once per AW | `txTick` called `sendData` at each AW boundary (16 TU) | A packet that arrived in the middle of an AW waited for the next; a full radio ended the batch until the next AW |
| One frame per medium access, at 54 Mb/s at most | `AWDLTx.c` sent each frame through the management queue at a fixed rate | At 54 Mb/s a 1400-byte frame costs about 370 µs with its ACK: an AW's air budget fits about 30 |
| 16 buffers for data | `NR_XMIT_EXTBUFF / 2` | Cap on frames in flight |
| Ring of 64 frames | `AWDL_TX_RING` | Larger bursts were lost before leaving |

The public reference ForbiddenItlwm (Intel AX211, 2026-11-10) gets about 20 MB/s from outbound Block Ack and
A-MPDU at 80 MHz. That is the piece missing here; the gate that holds data while the peer is on another
channel already existed (`peerOnChannel`).

## 2. The data pump

`txTick` still runs once per AW, but it no longer sends: it writes down what data may do in that AW and
calls `pumpData`.

| Field | Meaning |
|---|---|
| `_pumpOk` | There is a data window: served slot, window open, past the guard at the start, and not the last AW unless `rtlawdl_lastaw=1` |
| `_pumpSlot`, `_pumpCh` | Slot and channel of that AW |
| `_pumpEndUs` | End: the AW boundary, or 3 ms before the slot's end in the last AW |
| `_pumpAir` | Air left in the AW, in µs. Starts at `rtlawdl_air` with each AW |
| `_pumpStall` | The AW has no more to give (air spent, or the peer is on another channel) |

`pumpData` sends from the ring while `_pumpOk`, before `_pumpEndUs` and with air left. It is called from
three places:

- `txTick`, at the start of each AW.
- `queueData`, when a packet enters with the window open and nobody is sending.
- Itself, 1 ms later, when the radio answered busy.

There is one sender at a time (`_sendBusy`), so frames keep their order. The radio is called without the
lock held. `rtlawdl_pump=0` leaves only the call from `txTick`, which is the previous behaviour.

The ring grows from 64 to 256 frames. The driver grows from 32 to 128 management buffers and data may use
all but 16.

## 3. Outbound A-MPDU

### 3.1 Block Ack agreement (layer, `AWDLBaOrig.h`)

An A-MPDU needs a Block Ack agreement with the receiver. The layer asks for it: the family does not.

| Step | Rule |
|---|---|
| Request | 16 unicast frames to a peer within 1 s. ADDBA request: TID 0, window 64, immediate, no A-MSDU, no timeout. It goes to the head of the ring |
| No answer | Repeated after 500 ms, 3 times. Then single frames for 60 s |
| Answer | Status 0, immediate, TID 0 and a window above 0 open the session. Anything else: single frames for 60 s |
| DELBA from the peer | Closes the session; single frames for 10 s |
| Unused session | After 10 s it is asked for again, with the numbering going on |
| Watch | 64 frames or more sent in 2 s with no data frame arriving from the peer: the session is taken for broken, a DELBA goes out and frames are single for 60 s. `rtlawdl_aggwd=0` turns it off |

The watch exists because the RTL8814AU reports nothing about what it transmits (measured on 2026-11-09: no
CCX report). Real AirDrop traffic always brings frames back.

Up to 4 peers. A live session is not evicted while another entry exists.

### 3.2 Frames of a session

With the session open, unicast data to that peer leaves as QoS data (subtype 8, TID 0). The sequence number
is taken when the frame is handed to the radio and only advances if the radio accepted it. So a frame that
ages out in the ring or that the radio refuses leaves no hole, and the receiver does not wait for a number
that will never come. The number of the first QoS frame is the one the ADDBA announced.

Group frames, management frames and frames to peers without a session leave as before.

### 3.3 Driver

New radio ABI function, `rtlRadioAwdlAgg`, with `struct rtl_radio_awdl_agg`: rate (descriptor index, 0 turns
it off), widest bandwidth, short guard interval, MPDUs per A-MPDU, density; it returns the width the radio is
tuned to. `RTL_RADIO_ABI` does not change: a new layer on an old driver gets `kIOReturnUnsupported` and asks
for no sessions.

In `rtl8814native_radio_tx_80211`, a unicast AWDL data frame that is QoS data, with a rate set:

- keeps the sequence number of its header (`SET_TX_DESC_SEQ`, no `HWSEQ_EN`);
- goes to the queue of its TID (BE for TID 0) and not to the management queue;
- carries `AGG_ENABLE`, `MAX_AGG_NUM`, `AMPDU_DENSITY`, the rate, `DATA_BW` and `DATA_SC`.

It is the same set of fields the vendor tree gives the station's data with `ampdu_en`. The frame still
leaves through the management frame path (`dump_mgntframe`), with the management MAC ID.

### 3.4 Rate

VHT with two streams, chosen from the peers' signal as `rateByRssi` does. It goes down at once, up with 3 dB
to spare, and is given to the radio again every second. A wider channel asks for 3 dB more per doubling.

| MCS | Mb/s at 20 MHz | Mb/s at 80 MHz | Lowest RSSI at 20 MHz |
|---|---|---|---|
| 0 | 13 | 58 | none |
| 1 | 26 | 117 | -76 dBm |
| 2 | 39 | 175 | -73 dBm |
| 3 | 52 | 234 | -70 dBm |
| 4 | 78 | 351 | -66 dBm |
| 5 | 104 | 468 | -62 dBm |
| 7 | 130 | 585 | -58 dBm |
| 8 | 156 | 702 | -54 dBm |

The thresholds are estimates, like those of the single-frame ladder. Rate fallback on retries stays enabled
in the descriptor.

The width is decided by where the radio is tuned. Parked at 20 MHz, A-MPDUs go at 20 MHz. For 80 MHz:
`Tools/test_uc awdlbw 80`.

The air cost of a session frame is 40 µs plus its bits at the session rate, against 160 µs plus its bits at
the ladder's rate for a single frame.

## 4. Boot arguments

| Argument | Default | Effect |
|---|---|---|
| `rtlawdl_pump` | 1 | 0: data only leaves at the start of each AW |
| `rtlawdl_agg` | 1 | 0: no outbound Block Ack sessions |
| `rtlawdl_aggwd` | 1 | 0: never close a session for silence (one-way tests) |
| `rtlawdl_aggmax` | 31 | MPDUs per A-MPDU, 1 to 31 |
| `rtlawdl_aggden` | 6 | A-MPDU density, 0 to 7 |
| `rtlawdl_aggbw` | 80 | Widest bandwidth: 20, 40 or 80 |

`Tools/phase5_deploy.sh` accepts them in `RTL8814_AWDL_EXTRA` and removes them on each layer deployment.

## 5. Tests done (outside the kernel)

| Test | What it covers | Result |
|---|---|---|
| `Tools/baorig_test.cpp` | State machine of the agreement, ADDBA and DELBA frames, conversion to QoS, watch, eviction | Passes with ASan and UBSan |
| `pumptest.cpp` (function `pumpData` extracted verbatim) | Order, air budget, numbering with no hole after a refusal, end of window, balanced locks, radio never called with the lock held | Passes |
| `hbatest.cpp` (`handleBlockAck` extracted) | ADDBA response with the right and the wrong token, status 37, DELBA from the peer | Passes |
| `cargo test` of `Tools/awdlsim` | The virtual peer accepts the Mac's ADDBA and reads its QoS data in order | Passes |
| Build | Driver and layer | No errors |

The three tests with extracted functions are in `~/re_cache/work/awdl_tests` (`bash run.sh`).

## 6. Not checked

The first four rows were checked on 2026-11-11 (§7 and §8); of the third, the emission and the aggregation,
not the rate. The rest is still unchecked:

| What | How |
|---|---|
| The pump breaks nothing that worked | Send a file to the virtual peer, compare with 1.5 MB/s |
| The session request and the numbering | `awdlsim` prints `ADDBA request from awdl0` and `from awdl0: sessions N qos N gaps N`; `gaps` must stay at 0 |
| The radio emits A-MPDUs at a VHT rate | A53 capture in monitor mode: rate, width and aggregation bit of the Mac's frames |
| The RTL8814AU MAC aggregates with the management MAC ID and a fixed rate | The same capture. No chip documentation guarantees it |
| The MAC processes BlockAcks addressed to the port 1 address | Only with a real peer: iPhone |
| The iPhone accepts the ADDBA and QoS data on AWDL | iPhone. ForbiddenItlwm does it with an iPhone 11 |
| The signal thresholds | Capture with the iPhone |
| BE queue shared with the station's data during time sharing | Not measured. With the radio parked there is no station traffic |

The virtual peer has no radio: it sends no BlockAck. On the air, A-MPDUs to it are retried and dropped. What
the virtual peer measures is the software path, tapped before the antenna.

## 7. Measured in the kernel with the virtual peer (2026-11-11)

Driver `B74035FC`, layer `92DFA1D0`, macOS 26.7.3 (25G342), `rtlawdl_air=15000 rtlawdl_lastaw=1`. Virtual peer
`awdlsim --addba --tun`. Load: TCP from `awdl0` to a sink on the peer's `utun`
(`~/re_cache/work/awdl_tests/tcpflood.py`), which brings ACKs back as an AirDrop transfer does.

| Radio | Before (2026-11-09) | Now | Notes |
|---|---|---|---|
| Parked on 149, 20 MHz | 1.5 MB/s, about 1135 frames/s | 7.3 to 7.7 MB/s, about 5450 frames/s | Session at MCS 8, 156 Mb/s |
| Parked on 149, 80 MHz (`awdlbw 80`) | not measured | 18.5 to 19.4 MB/s | Session at MCS 8, 702 Mb/s; 191 MB in 16 s counting the stalls |
| Shared with a BSS on 153 | about 240 kB/s (AirDrop) | 785 kB/s | Only 2 of 16 slots carry data: dense presence does not turn on with a shared radio |

The values with the radio parked are those of the stretches with no stall. With `en2` disassociated the system
looks for networks every 13 s and the radio leaves the channel for about 4 s; nothing moves in those
stretches, neither data nor the virtual peer's status lines. The stalls match the `scan` bursts of `airportd`
to the second.

What was checked:

- The session request goes out and the peer accepts it: `ADDBA request from awdl0: token 2 ssn 4042 -> accepted`.
- The numbering: `from awdl0: sessions 3 qos 199723 gaps 1`. The only gap appeared during one of the network
  search stalls.
- The bytes the sink received are the bytes sent, in the three TCP runs.
- Layer counters in one run: `A-MPDU frames 55627 at 156 Mb/s, sessions asked 5 on 2 refused 0 unanswered 1`,
  `busy 11893`, `pump runs 12452`.
- The station link associated with the new kexts: ping to the router with no loss.

Defect found and fixed in the code (layer `5EC1F0A9`, built, not loaded): with the ring full, each new packet
evicted the head, and the ADDBA request was there. Under a UDP flood of 54000 packets/s the layer logged three
requests and none reached the peer. A management frame at the head now stays and the new packet is the one lost. With
TCP the request always went out.

The one-way UDP flood does not measure the session: with no frames back the watch closes it after 2 s (§3.1).
With single frames it gave about 890 frames/s with the stalls included, no clear change from the 1135 of
before: the radio rules there, answering busy.

These figures are of the software path, tapped before the antenna. The virtual peer sends no BlockAck. That
the chip really emits A-MPDUs at that rate and that an iPhone receives them stays in §6.

For the iPhone test: with `en2` associated on a non-social channel the radio is shared and the ceiling is the
third row's. The values of the first two need `en2` disassociated. That third row was measured under the
defect of §9.3.

## 8. On the air: the MAC emits the aggregates (2026-11-11)

Witness: the phone in monitor mode at 20 MHz on 149 (`~/Desktop/a53/telefono/vigia_mac.sh`).

### 8.1 What the phone does not hear

The phone decoded no QoS frame of the session, in three setups: at 80 MHz with MCS 8, at 80 MHz with a low
rate and with the radio parked at 20 MHz with MCS 0 (about 13000 frames sent). It did not decode the Mac's
station data to the router either: of 11201 packets it heard 9176 RTS from the Mac and 9479 BlockAcks from
the router, and no data frame. It does report aggregated frames of other stations on the channel. So its
captures do not tell whether the Mac emits the session's frames.

To lower the session's rate the virtual peer declares less signal (`awdlsim --rssi`). At -80 dBm the Mac does
not install the peer's neighbour entry and there is no route to it; at -72 dBm it accepts it.

### 8.2 The test: CTS-to-self

`RTL_OPT_AWDL_PROT` (`Tools/test_uc awdlprot 0|1|2`: none, RTS/CTS, CTS-to-self) makes the MAC protect the
session's frames. The protection goes at an OFDM rate and the phone hears it; its Duration field tells how
long the frame behind it is.

Driver `70AB93CF`, layer `5EC1F0A9`, `awdlprot 2`, `en2` associated on 153 (the radio keeps its 80 MHz on 149,
which is in the same block), peer at -72 dBm, 14 s of TCP:

| Item | Value |
|---|---|
| QoS frames handed to the radio | 6769 |
| CTS heard with RA equal to `awdl0`'s address | 944, at 6 Mb/s, all within the transfer |
| Duration of those CTS | 820 between 3000 and 3200 µs (3184 the repeated value); the rest spread from 200 to 3000 µs |
| Spacing between consecutive CTS | median 3.42 ms |

Reading:

- The MAC takes the session's frames through the data queue and puts them on the air: there is no CTS-to-self
  without a frame behind it.
- It aggregates them. At the 58.5 Mb/s of two-stream VHT MCS 0 at 80 MHz, 3184 µs are about 15 MPDUs of 1468
  bytes; a single MPDU lasts about 250 µs. The shorter durations are shorter aggregates.
- The CTS carries `awdl0`'s address, not the station's.
- 6769 frames in 944 CTS are 7.2 per CTS. That fits each aggregate going out twice, since the virtual peer
  sends no BlockAck; it was not measured.

Not checked: that a receiver decodes those VHT PPDUs, and the BlockAck coming back. That needs a real peer.

### 8.3 Ring fix, checked

With layer `5EC1F0A9`, under a UDP flood of 54000 packets/s the ADDBA request reached the peer
(`ADDBA request from awdl0: token 2 ssn 1068 -> accepted`). The session carried 986 frames and the watch
closed it: `Block Ack session ... carried data and nothing came back: closed, single frames for 60 s`.

## 9. Radio shared with a BSS (2026-11-11)

`en2` associated on 153 at 80 MHz, virtual peer at -40 dBm, `rtlawdl_mcc=1`. Channel 149 is in the BSS's
80 MHz block, so the radio keeps the width and only moves the primary. Load: the one of §7.

### 9.1 Slot share during a transfer

Outside a transfer the layer serves the first slot of each run of the social channel in the family's
sequence: 2 slots of 16. Dense presence did not turn on with the radio shared.

Dense presence now also turns on with time sharing. While it lasts, the layer serves the slots the sequence
gives the channel and the idle ones, in runs of at most `rtlawdl_mccrun` slots (3 by default, 0 keeps the old
behaviour), and leaves the slot after each run to the BSS. The slots the sequence gives another channel are
not taken (`Source/AirLayer/AWDLShare.h`, test in `Tools/share_test.cpp`).

| Family's sequence | Slots served |
|---|---|
| Idle: `153 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0` | 12 of 16 |
| With data: `149×6 153 153 6 149×5 153 153` | 9 of 16, in four visits |

### 9.2 Measured

Layer `2FF5140E`; driver `70AB93CF` in the first three rows and `CEBC43AE`, with the fix of §9.3, in the
fourth. Ping to the router through `en2` every 0.2 s during the transfer.

| Setup | TCP to the peer | Ping to the router |
|---|---|---|
| Previous layer `5EC1F0A9`, 2 slots | 785 kB/s | no loss, mean 28 ms, maximum 421 ms (with the peer at -72 dBm) |
| New share, 9 slots | 1499 kB/s | no loss, mean 81 ms, maximum 218 ms |
| New share and the bit of §9.3 cleared by hand | 15.3 MB/s over 14 s (15.7 to 16.5 MB/s after the first 2) | no loss, mean 77 ms, maximum 206 ms |
| New share and driver `CEBC43AE` | 15.5 MB/s over 14 s (15.8 to 16.6 MB/s after the first 2) | no loss, mean 68 ms, maximum 206 ms |

Without a transfer, with the new layer: ping without loss, mean 6 ms, maximum 53 ms.

With driver `CEBC43AE` the BSS was on 157, in the same 80 MHz block. The session's numbering had no gaps:
`from awdl0: sessions 1 qos 159758 gaps 0`.

The BSS with traffic of its own, driver `CEBC43AE`: an HTTPS download of 90 MB through `en2`.

| Setup | Download through `en2` | TCP to the peer |
|---|---|---|
| Download alone | 26.8 MB/s | |
| Download during the transfer | 9.1 MB/s | 15.1 MB/s over 16 s |

### 9.3 Defect: the management MAC ID asleep off the BSS's channel

The new share gave 4.5 times the slots and the rate only doubled. What was seen:

- With the session open, a UDP flood put out 1152 QoS frames per second, the same number second after
  second (and 864 in some): multiples of 288. The previous layer gave about 575 per second, derived from its
  785 kB/s. That is about 300 frames per visit to the social channel, whether the visit lasts one slot or
  three.
- `REG_MACID_SLEEP` (0x4D4) read 3 in 45 of 60 samples taken during the flood: MAC IDs 0 and 1 asleep. In 41
  of them the low-priority queue had no free pages and the public queue had 0x16 of 0x776 left. With the
  register at 0 the pages were free.

Cause: `rtl8814native_radio_awdl_offch` puts every MAC ID in use to sleep when it leaves the BSS's channel
(`rtw_hal_macid_sleep_all_used`). A session's frames carry the management MAC ID (1) in an AC queue
(§3.3). Reading of the figures: the chip held them until the return to the BSS's channel, and each visit
took in what fits in the transmit FIFO and the driver's buffers. Sync frames and single data frames go
through the management queue and went out in their slot with the same MAC ID asleep, which is why it did
not show before. With the radio parked no MAC ID sleeps.

Live test, without rebuilding: a user-space loop that reads 0x4D4 and, when it is 3, writes 1
(`Tools/test_uc peek` and `poke`), a few milliseconds after each departure. With the loop running, the same
transfer gave the third row of §9.2. In one run the loop ended before the transfer: the rate went from
15.8 MB/s to 1489 kB/s within the same connection.

Fix (`Source/Driver/AWDLTx.c`): after putting the MAC IDs in use to sleep, the management one is woken. The
station's data carries the AP's MAC ID and stays asleep. Driver `CEBC43AE`, loaded and measured (§9.2).

The 785 kB/s of §7 and the test of §8.2 were made with this defect. The CTS-to-self frames of §8.2 go out
duplicated on the four channels of the block, so that test does not tell when each aggregate went out.

### 9.4 Not checked

| What | How |
|---|---|
| A real peer with its own sequence | iPhone: its slots may not match the runs served |
| `rtlawdl_mccrun` other than 3 | Repeat §9.2 with 2 and with 5 |

## 10. Network scans without an association (2026-11-11)

Loaded: driver `02AC5E67`, layer `F877A0D8`. Measured with the virtual peer (§10.3). What is left is in §10.4.
Loaded afterwards: driver `06F390BF`, which returns to the AWDL channel only if there was data too (§10.5).

### 10.1 The gap

With `en2` not associated the system scans for networks every 13 s and AWDL goes silent for about 4 s each
time (§7). The driver's scan stays 100 ms on each channel (`SURVEY_TO`) and returns to its own channel
between groups only when there is an association: every 3 channels, 400 ms on the BSS's channel
(`RTW_SCAN_NUM_OF_CH`, `RTW_BACK_OP_CH_MS`). Without an association it goes through the whole list at once.
The layer stops serving its windows while the state is a scan.

### 10.2 The change

| Where | What |
|---|---|
| `rtw_scan_backop_decision` | Without an association the scan also returns between groups if the radio is parked on a social channel, the AWDL port is on and an AWDL frame came in during the last 10 s. Decided once per scan, when it is queued, and the scan's timeout counts the returns |
| `SCAN_BACKING_OP` | Without an association the return is to the parked channel, the one the end of the scan returns to, with the MAC IDs awake |
| `rtl8814native_radio_tx_80211` | During a scan without an association, outside those returns, AWDL frames are refused as busy: data waits in the layer's ring and does not go out on a foreign channel |
| `rtl_radio_status` | `scanning` is `RTL_RADIO_SCAN_BACKOP` in those returns too, with `idle_channel` set |
| `RTLAwdInterface::txTick` | In those returns the layer serves its windows as with the radio parked |

`Tools/test_uc awdlscan <seconds>` changes the 10 s; with 0 the scan runs as before.

The scan lasts longer while a peer is around: 400 ms more for every 3 channels. Without peers around nothing
changes.

### 10.3 Measurement (2026-11-11, virtual peer)

`en2` not associated, radio parked on 149 at 20 MHz, TCP load from `awdl0` to the virtual peer with the
reception sampled every 50 ms. Hole: continuous time with nothing received, 150 ms or more, after the first
2 seconds. The full scan is requested through CoreWLAN (`scanForNetworks`), as the Wi-Fi menu does; the
driver runs it as two scans in a row. `awdlscan` was changed at run time, without a reboot.

| | `awdlscan 0` (as before) | `awdlscan 10` |
|---|---|---|
| Length of the full scan | 3.5 to 3.8 s (1.2 s + 2.2 s in the driver) | 7.8 s (2.5 s + 4.8 s) |
| Hole in each scan | one of 3.9 to 4.0 s | several of 0.25 to 0.46 s (median 0.36 to 0.41 s); one of 0.6 s in 4 scans |
| Mean TCP over 60 s | 6.2 MB/s, a scan every 14 s | 6.0 MB/s, a scan every 18 s |
| Networks in the list, no load, 4 alternated scans | 10, 10, 10, 10 | 10, 10, 10, 9 |
| Networks in the list, under load, first round | 10, 10 | 8, 8, 8, 9 |
| Networks in the list, under load, 5 alternated scans | 9, 10, 9, 8, 8 | 10, 9, 9, 8, 8 |
| Directed scan of `networksetup -setairportnetwork` | finds the network in 4 s | finds the network in 9 s |

The mean throughput hardly changes: the radio spends the same time away from the AWDL channel, split into
stretches of 300 ms. What changes is that there are no longer 4 s of silence in a row.

The network list changes by itself over time, the same in both modes (from 10 to 8 networks in 2 minutes
in the alternated round): the change does not alter it. `networksetup` without a password finds the
network in both modes and fails the same way when joining (-3900); the association with the stored keys
only comes from the Wi-Fi menu.

Association from the Wi-Fi menu with the virtual peer running: the network is joined (WPA3 transition,
channel 157 at 80 MHz) in 1.6 s from the moment `airportd` gets the request; after that, normal ping
through `en2` and 15.7 MB/s of TCP over AWDL with the radio shared. The request goes out when the scan the
menu opened is over, which with the peer near lasts 7.8 s instead of 3.5 s: about 7 s went by from the
click to the start of the association. That is the visible cost of the change. The driver of §10.5 narrows it.

With nobody asking for a full scan, what the system did by itself with `en2` not associated was a scan from
`locationd` every 60 s on 3 channels: 0.35 s in the driver, one hole of 0.41 s, 7.2 MB/s over 180 s. That
scan is a single group and has no return in the middle. The 13 s cadence of §7 did not show up in this
session.

### 10.4 Not checked

| What | How |
|---|---|
| That without peers the scan lasts as before | Not measured. With the peer idle the return is not armed and it lasts 3.4 s (§10.5) |
| A real peer | With an iPhone it remains to see that its frames arrive in the returns and that the transfer goes on |

### 10.5 Only with data

Driver `06F390BF`, loaded. A peer that only keeps its sync frames going no longer lengthens the scan:
the return to the AWDL channel needs, besides an AWDL frame in the last 10 s, AWDL data within the same
time. Data is a data frame received from a peer or a unicast one sent, or waiting, towards a peer. The
node's own multicast data does not count. `awdlscan` sets both times at once.

Measured on 2026-11-11 with the virtual peer, `en2` disassociated and the radio parked on 149. The first
run was made with the radio not receiving (§11): its lengths hold, not the networks found. The second,
with driver `CFE7BB9D` and the radio healthy.

| Situation | Radio not receiving | Radio healthy |
|---|---|---|
| Idle peer | 3.5 to 3.8 s | 3.4 s in the driver, 10 scans, no return |
| Under TCP load | 7.8 s (6.1 s for the one that started with the load); holes of up to 0.51 s | 7.8 s, 2 scans; holes of 0.15 to 0.51 s, median 0.41 s; mean TCP 5.2 MB/s over 60 s |
| 13 to 14 s after the load | 3.5 s | 3.5 s, 2 scans, no return |
| Networks in the list | does not hold | 10 to 12 idle, 8 to 9 under load |

Under load the list brought fewer networks in this run: those on channels 3 and 11 were missing. In §10.3
that difference did not show. Two scans are not enough to settle it.

Right after the disassociation the system scanned by itself every 13 s (the cadence of §7) for about two
and a half minutes; each scan lasted 3.4 s in the driver.

Association from the Wi-Fi menu with the peer idle: the scan the menu opened lasted 0.28 s, the
association request went out with the click, 1.5 s after opening it, and the network was joined 1.5 s
later. The 7 s wait of §10.3 did not happen.

With no peer at all it was not measured. With the peer idle the driver takes the same path: the return
is not armed.

## 11. The radio stopped receiving after an association (2026-11-11)

Seen once, with driver `06F390BF` and layer `F877A0D8`. Cause not proven. A guard is loaded since the
16:26 boot of the same day: driver `CFE7BB9D`.

### 11.1 What was seen

| Time | Fact |
|---|---|
| 15:22:53 | Automatic association at boot: BSS on 157 at 80 MHz, no AWDL peers |
| after it | The layer takes the radio to the slots on 149 and back (`radio to channel 149`, `radio to channel 0`) |
| 15:22:57 | Last scan with results (5 networks) |
| 15:23:31 | The driver reports the link down (`link_off`), with no disconnect request |
| from 15:23:34 | Every scan returns 0 networks; the driver's `rx_frames` stays at 543 |

In that state:

| What | Value |
|---|---|
| The MAC's receive counters (`0x664`, by type) | Still: neither good frames nor frames with errors |
| The baseband's CCA counter (`0xF08`) | Keeps counting, about 230 a second; false alarms (`0xF48`) about 70 a second, against 1 with the radio healthy |
| Enables (`0x100`, RCR, `0x522`, RF `0x00`) | Normal: receive enabled, transmit not paused, RF in receive mode |
| Receive FIFO (`0x284`) | Empty and idle |
| Transmission | The USB writes keep going out; not checked on the air |
| Wi-Fi off and on, changing the parked width | Do not recover it |
| Reboot | Recovers; with the same driver the radio stayed well afterwards |

The virtual peer kept working because it does not use the air. Since 2026-11-10 `airportd`'s log holds no
other scan with 0 results before 15:23.

Of 218 MAC, baseband and RF registers read in the state without reception and with the radio healthy, the
38 that differ are the ones that change with the channel, the width and the association. The receive I/Q
compensation ones (`0xC10`, `0xE10`, `0x1810`, `0x1A10`) were not read in that state. With the radio healthy,
without an association and on 149 at 20 MHz, all four hold `0x00000100`.

### 11.2 What the code says

| Where | What |
|---|---|
| `odm_txpowertracking_callback_thermal_meter` | In the watchdog (command thread, every 2 s), if the chip's temperature moved 8 units or more, it recalibrates I/Q (`do_iqk_8814a`). On the 8814A it does no LCK |
| The same function | Waits until no scan is running (`is_scan_in_process`): the calibration does not live with channel changes |
| `rtl8814native_radio_awdl_offch` | The hop to an AWDL slot changes the channel from the layer's thread, outside the command thread, with `offch_mutex` and `setch_mutex`. The watchdog takes neither |
| `rtl8814native_radio_set_channel` | The parked channel without an association goes through the command queue: it does not cross the watchdog |

Nothing then stops a thermal calibration from running while the layer hops channels. That this is what
happened at 15:23 is not proven: the calibration left no record.

### 11.3 The change (loaded)

The thermal calibration holds `offch_mutex` while it lasts, and does not run while the radio is away on an
AWDL slot: the reference thermal value is not updated and the next watchdog tries again.
`Tools/test_uc awdlcal` shows how many calibrations ran and how many were put off.

With the driver loaded, 3 minutes after boot (network associated on 157, virtual peer just started) the
counter showed 1 calibration run and 0 put off. At 10 minutes, after a 60 s transfer without an
association, it was the same. The thermal calibration does run then in the first minutes after a
boot, with the network associated. The failure of §11.1 happened in the first minute after a boot: that fits
the suspicion and does not prove it.

Long run the same day: 62 minutes with the network associated, the first 16 under a TCP load over AWDL to
the virtual peer (three runs of 5 minutes at 15.4, 15.7 and 15.8 MB/s; holes of up to 1.0 s, 3.4 to 9.5 s
added up per run). The link held, reception went on and the counter stayed at 1 and 0. The thermal meter
(RF `0x42`, bits 15:10) went from 43 to 44 under the load, with one reading of 45: far from the 8 units
that trigger the calibration. With the chip already warm the load does not cause it; it stays confined to
the warm-up after a boot, and the guard can only be seen acting there.

### 11.4 Not checked

| What | How |
|---|---|
| A calibration put off by an AWDL slot | Not seen: 0 put off in 80 minutes. It can only happen in the first minutes after a boot |
| That the radio does not lose reception again | Normal use with the new driver; the failure was seen once |
| The I/Q registers if it happens again | `regdump.sh` extended with `0xC10`, `0xE10`, `0x1810`, `0x1A10` |
