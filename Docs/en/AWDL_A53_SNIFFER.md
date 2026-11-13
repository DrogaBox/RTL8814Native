# AWDL: the A53 as a monitor and as a peer over the air

Measurements from 2026-11-06 with a Samsung A53 (SM-A536E, kernel 5.10.237, S620 firmware, root in memory) and the
T9UH with driver `DB92193C`. Sections 2 to 5 use layer `0BBDEE68`; sections 6 to 8 use `EEEE65F6`. This is the first check of the Mac's TX by a
receiver other than the Mac itself. What has not been measured carries a `?`.

The result in one sentence: the Mac's frames go on air on the channels and in the slots it advertises, and its clock
is coherent; with time sharing few go out, with layer `EEEE65F6` they now carry TLVs 7 and 17, and the Mac follows a
peer's clock and keeps sending MIFs.

## 1. Setup

The phone's `scsc_wlan` driver accepts `NL80211_IFTYPE_MONITOR` and delivers radiotap. This corrects the earlier idea
that the A53 could not capture. The module sources are not in the repository.

| Step | Command | Note |
|---|---|---|
| 1 | `svc wifi disable`, `ip link set wlan0 down` | `wlan0` stays `UP` after `svc wifi disable` because of Android's always-on scanning |
| 2 | `nlmon newmon 0 mon0`, `ip link set mon0 up` | In-house nl80211 tool, no libnl. The firmware accepts the vif (ifnum 3) even with the STA associated |
| 3 | `insmod awdl6.ko ifname=mon0 freq=5745` | For another channel: `rmmod` and `insmod` |
| 4 | `tcpdump -i mon0 -y IEEE802_11_RADIO -w x.pcap -U` | `tcpdump` ships in `/system/bin` |
| 5 | `ip link set mon0 down`, `nlmon delif mon0`, `rmmod awdl6`, `ip link set wlan0 up`, `svc wifi enable` | Associates again in about 10 s, with no reboot |

`NL80211_CMD_SET_CHANNEL` does not work. With another interface running, cfg80211 answers `EBUSY`. With `wlan0` down,
`slsi_set_monitor_channel` sends the request on `wlan0`'s vif, which is closed, and gives `EINVAL` after
`No cfm(0x2147)`. The `awdl6` module calls `slsi_mlme_configure_monitor_mode` on `mon0`'s vif, and the firmware answers
success.

With no channel set, `mon0` hears only what the STA or a scan tunes to: 53 frames in 15 s between 2412 and 5785 MHz.
With the channel set it heard 1370 frames in 19.4 s.

**Monitor loss.** The two nearby access points with a 100 TU interval give 190 of 190 and 187 of 190 expected
beacons. A Mac frame missing from the capture very likely did not go out on that channel.

**Limits.**

- The capture has no FCS, so damaged frames cannot be told apart.
- The monitor and TX as a peer do not coexist: TX uses the associated STA's vif.
- Root is lost on a full reboot or a panic. Any new module carries that cost.
- A TX thread must stop at the first error and not send data frames down the management path. The kernel tree does not
  reproduce the `slsi_dev` layout of the loaded driver (§8).

## 2. The A53 as a peer: the Mac listening

Module `awdl5`: 293-byte PSFs and 307-byte MIFs with a 28-byte TLV 7, metric 300 and address `02:a5:3e:00:00:01`,
through `slsi_mlme_send_frame_mgmt` on the STA's channel (5765 MHz, channel 153). The time fields are fixed, so it is
not a peer coherent in time.

| Measure | Value |
|---|---|
| Rounds in 60 s | 432 PSFs and 432 MIFs, no firmware failures |
| Layer | `rx af` reaches 512, RSSI -31 and -32, `clock follows 02:a5:3e:00:00:01` |
| `AWDLStateDump` | `peers:1` from 09:10:46 to 09:12:22, `peers:0` before and after |
| `Posted Peer Absence` | 1, at 09:11:20, about 7 s after the last frame |
| `cached` | 0 |

This is the first time a peer over the air reaches `peers:1`. Before there was only `Peer Absence`, with bare PSFs and
no MIFs. `cached:0` does not indicate a failure: that counter rises on a cache request from the manager, which did not
happen here `?`.

The layer's log shows the PSFs with `ch 153` and the MIFs with `ch 149`, although the phone only sent on 153. It is the
channel the radio was on, and the fixed time fields cause the split by frame kind (§7).

**Earlier `awdl5` panic.** `/proc/reset_summary` keeps the trace: `__cfi_check_fail [scsc_wlan]` from
`peer_thread [awdl5]`. `__init` is `__nocfi` and a kthread body is not, so the indirect call between modules trips
CFI. `noinline __nocfi` on the sending function resolves it. `pstore` is empty on this phone.

## 3. The Mac as seen by the A53

The Mac's state: `en2` associated on channel 153 at 80 MHz, time sharing, `Low Power`, no peer.

| Channel | Duration | `awdl0` frames | `en2` frames |
|---|---|---|---|
| 149 (5745 MHz) | 19.4 s | 73 (37 PSF, 36 MIF) | 12 |
| 153 (5765 MHz) | 14.3 s | 0 | 94 |
| 6 (2437 MHz) | 14.2 s | 0 | 0 |

The `en2` frames on 153 show that capture could hear the Mac. Those from `en2` on 149 are 12 retries of one Null with
PM=1, once in 19 s. Whether it went out on 149 or is signal from adjacent 153 is not known `?`.

- **Rate and power.** All at 6 Mb/s, RSSI between -41 and -37 with the two devices close together.
- **Advertised sequence.** `[0 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0]`.
- **Slots.** The 36 MIFs fall in the first AW of slots 2 and 10, those of 149. Of the 37 PSFs, 36 go out in the last
  AW of slots 1 and 9, about 5.4 TU before the MIF, because of the early channel switch. The other falls in slot 10.
- **AW grid.** `aw_seq` advances 32 per 512 TU, that is, 16 TU per AW.
- **`tx_counter`.** It is 64 in the MIFs and 5 or 6 in almost all PSFs. It matches the real time to the next slot
  boundary, with an error under 1 TU.
- **Time stamp against the phone's clock.** 35 ppm drift, residual with a 119 µs deviation and a 788 µs maximum over
  73 frames.

**Sequence number.** It advances 4 per PSF and MIF pair heard (2970, 2971, 2974, 2975...). The counter is the hardware's
and `en2` and `awdl0` share it. The capture on 153 gives the rest: a Null with PM=1 (3690) and, 26 ms later, a Null with
PM=0 (3693), the leave and return notices of time sharing around the PSF and MIF (3691 and 3692). No frame is lost.

## 4. Against iOS 26

Reference: owly's captures on channel 44, with no time sharing. The iPhone figures come from two separate captures,
split by a slash.

| Field | iPhone | Mac over the air | Reading |
|---|---|---|---|
| TLV 4 head: AW, AF, flags, extension | `1000 6e00 0018 1000 1000` | Same | Matches |
| Min and max extension, presence | `03030303`, `0400` | Same | Matches |
| `tx_counter` | Time to the next slot | Same | Matches |
| AW the MIF goes out in | Second or third of the slot | First; third with a peer heard (§7) and on 149 with no peer (§10) | Valid by its stamp; untested with an iPhone `?` |
| AW remaining in the MIF | 0 | 16 | Coherent with going out at the start of the AW |
| `target_tx` against `phy_tx` | `phy_tx` about 82 µs later | Equal | owly also makes them equal and works with an iPhone |
| TLVs 7 and 17 | Present | **Absent** | Fixed in layer `EEEE65F6` (§6) |
| TLV 21 | `a002` | `a001` | Version with a different device class `?` |
| PSFs per second | 8.7 / 7.6 | **1.96** | Result of time sharing |
| MIFs per second | 3.0 / 4.6 | **1.91** | Result of time sharing |

## 5. What it means for the test with an iPhone

**Associating on a non-social channel cuts what goes out by a lot.** The radio tap of the 2026-11-05 rehearsal, on 149
with no time sharing (`mcc 0`), gave 8.9 PSF/s and 2.9 MIF/s over 44.9 s, the iPhone's cadence, confirmed over the air in §9. On 153 the air gives
1.96 and 1.91: a single 149 window every 512 TU. The advice to disconnect `en2` in that case
([AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md) §3) now has a figure behind it. The cadence with no association
was measured afterwards (§17).

**The A53 can check the deployment with nobody else around.**

1. Check TLVs 7 and 17 after loading layer `EEEE65F6`: done (§6).
2. `sudo Tools/iphone_test.sh run` with the peer active: a real frame enters the capture: done (§7).
3. `en2` disconnected: measure the cadence with no association: done (§17).

A capture of this kind carries the Mac's name in TLVs 2 and 16: it stays out of the repository.

## 6. With layer `EEEE65F6`

Same `en2` state (153 at 80 MHz, time sharing), a 30 s capture on 5745 MHz after rebooting.

| TLV | On the air | Against what was expected |
|---|---|---|
| 7 | `00 00 2c 00 1f ff 00 00` | Identical to [AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) §10 |
| 17 | `bf 0c 00 00 80 01 fd ff 00 00 fd ff 00 00` | Identical |

- **Frames.** 56 PSFs of 326 bytes and 56 MIFs of 986, both TLVs in all of them: 1.94 per second of each, unchanged.
- **TLV order.** PSF `4 5 18 6 24 12 7 17 21` and MIF `4 5 18 24 12 7 17 16 21 6 2...`, the same as owly's iPhone.
- **Log.** `check` says READY. `Invalid _htCapabilityLength` appears only until 09:30:06, during startup, and not
  after. `getVHtCapabilityIE Unable` does not appear in the last 8 minutes.
- **`en2`.** Ping to the router with no loss, 1.8 ms on average.
- **A53 peer, 40 s.** 313 rounds with no failures, `rx af` reaches 512, 7 dumps with `peers:1`, `cached:0` and one
  `Peer Absence`, as with the previous layer. Ping over `en2` after the run: 2.0 ms.

Whether an iPhone requires these TLVs is still untested, and AirDrop delivery through the virtual peer has not been
repeated on this layer `?`.

## 7. Capture rehearsal with the A53 as a peer

A run of `sudo Tools/iphone_test.sh run` with `en2` on 153 (time sharing) and two 40 s peers in a row: `awdl5` with the
fixed time fields of §2, and `awdl7`, the same send path with TLV 4 and the two stamps computed for each frame from the
phone's clock.

**The kit collects a real frame.** The console showed `HEARD 02:a5:3e:00:00:01 rssi -32 ch 153 action len 293` and
`IO80211 peers 1`, with `lost 0`. `frames.pcapng` holds the frames both ways, and `report` gives the TLVs of the peer and
the Mac, the split by channel and the IO80211 lines. It was the only part of the kit not yet seen.

| Measure | `awdl5`, fixed clock | `awdl7`, running clock |
|---|---|---|
| Sent by the phone | 312 PSFs and 312 MIFs | 313 PSFs and 313 MIFs |
| Received on the Mac | 307 PSFs and 223 MIFs (85 %) | 300 PSFs and 307 MIFs (97 %) |
| `ch` of those received | PSFs: 153. MIFs: 149, except 1 | 541 on 153 and 66 on 149, the same in PSFs and MIFs |
| Mac TX during the peer | 8.83 PSF/s and **0.03 MIF/s** | 2.82 PSF/s and **1.90 MIF/s** |
| Slots of the Mac's MIFs | A single MIF | 2 and 10, third AW in 75 of 76 |
| Mac's position minus the phone's | Not applicable | Median -0.5 TU, between -0.9 and -0.1 (p10 and p90) |
| `AWDLStateDump` | `peers:1`, `cached:0` | `peers:1`, `cached:0` |

With no peer, in the same run, the Mac sends about 2 PSF/s and 2 MIF/s.

**Why the MIFs stop with `awdl5`.** The fixed templates say `aw 6`, next slot in 18 TU, in the PSF and `aw 0`, in 64 TU,
in the MIF. `alignClock` takes both, because the peer announces itself as master, so each frame received sets the clock
back and the Mac loops between slots 0 and 2. `txTick` sends a MIF only if `aw / AWDL_PM != _lastMifSlot`, and slot 2 never
changes its number: one MIF goes out and no more. With `awdl7` the count advances and the MIFs go on at the same cadence as
with no peer. A master with a coherent clock does not cause this stop.

**Reading `ch`.** It is the channel the radio is on, not the one the frame was sent on. With `awdl5`, each MIF arrives with
the Mac in slot 2 (149) and each PSF with the Mac still on 153. With `awdl7` the split no longer depends on the frame kind:
11 % of what is received arrives on 149, which takes 2 of 16 slots. The Mac hears on 149 frames sent on adjacent 153, with
more loss: 71 % of `awdl5`'s MIFs against 98 % of its PSFs.

**What not to read into this.**

- `awdl5`'s 8.83 PSF/s is not a cadence gain: the Mac stays stuck in the slots that serve 149.
- With a peer heard, the MIF goes out in the third AW (`dwell 4` in the log), as the iPhone's does, not in the first as in §3.
  On 149 it goes out in the third with no peer as well (§10), so the peer alone does not explain the change.
- The election does not reflect who is followed. The Mac announces metric 514 and declares itself master, the A53 announces
  300, and the layer still follows the A53 because it is the first to present itself as master. In owly's captures the iPhone announces 525, which
  wins, and what follows it is owly's Linux stack, with metric 60 (§10). Both cases were measured over the air with A53 frames
  (§10). An iPhone following has not been seen, since no capture shows one `?`.
- The A53 sends only on 153, does not follow the Mac's sequence and sends no data. `cached` is still 0.

## 8. Frames addressed to the Mac

Module `awdl8`, with `awdl7`'s clock. At 09:57:31 the access point moved from 153 to 149 with the same BSSID: `en2`
associated again in 9 s, and from then on there is no time sharing (`mcc` does not advance). The phone's channel during
these rounds was not checked `?`. The capture had already ended, so there are only counters: `rx af` is logged every 256
frames and gives a range.

| Mode | Destination | Source | Sent | `rx af` increase |
|---|---|---|---|---|
| 1 | `awdl0` | `02:a5:3e:00:00:01`, not the phone's | 1092 | 34 048, about 31 times per frame |
| 3 | `awdl0` | The phone's | 470 | Between 257 and 767 |
| 4 | An address nobody has | The phone's | 470 | Under 256 |

- **The chip delivers what is addressed to `awdl0`** and filters what is addressed elsewhere.
- **The chip acknowledges that address.** An ACK goes to the source address, and the phone's chip takes it only for its own.
  With another source the firmware repeats each frame up to the limit; with its own, the repeats go away. The firmware's
  transmission status confirms it frame by frame (§11).

**Data: a failed test, repeated under control in §11.** Mode 2 sent an IPv6 Neighbor Solicitation and Echo as AWDL data
(frame control `0x08`) through `slsi_mlme_send_frame_mgmt`, one pair every 8 rounds of 128 ms, the first in round 4. The
phone kept that boot's kmsg in `/data/log/dumpstate_lastkmsg_*_KP.log.gz`, a zip despite the extension, and
`/proc/reset_summary` gives the panic's stack:

| Since the start | Event |
|---|---|
| Up to 0.58 s | Five PSFs and five MIFs, with no error in the log |
| About 0.64 s | By the module's schedule, the first data frames are due |
| 0.649 s | `WLBT FW failure`: the firmware goes down with a level 8 error and code `0x4` |
| 0.661 s | `No cfm(0x2110) for req(0x2010)`: the module was waiting for the confirm of an `MLME_SEND_FRAME_REQ` |
| 0.827 s | `wifi@1.0-service` closes `wlan0` and recovery begins |
| 9.55 s | First of 10 `0x800a` (`INVALID_VIRTUAL_INTERFACE_INDEX`): the reloaded firmware does not have that vif |
| 12.39 s | `wlan0` opens again |
| 12.69 s | `wifi@1.0-service` closes `wlan0` once more, and `hip4_deinit` destroys the wakelocks |
| 12.70 s | `BUG_ON(!timer->function)` at `timer.c:973`, inside the `awdl8` thread: kernel panic and root lost |

These are two separate failures.

- **The firmware went down as the first data frames came due.** That is a match in timing: no log names the frame in
  flight. §11 reproduces it with a single data frame. Where in the firmware remains unread `?`.
- **The module caused the kernel panic.** Its thread went on sending after the first error. `wakeup_source_remove` leaves `timer.function = NULL`, and
  `scsc_wifi_transmit_frame` used that destroyed TX wakelock through `pm_wakeup_ws_event`. At that moment `wifi@1.0-service`
  was still inside `hip4_deinit`. The `0x800a` was not the cause, only the module sending to a vif that no longer existed.

**Second panic: the first attempt at a fix.** To close that race, `awdl7` was changed to take `start_stop_mutex`. On loading
it, the kernel panicked 54 µs after the thread started, in `send_one`'s first `mutex_lock` (`osq_lock`,
`UBSAN Trap: Out-of-bounds`), and root was lost again. The module took the mutex at `sdev+0x398`, and the real
`scsc_wlan.ko`, extracted from `vendor_boot`, has it at `sdev+0x3C8`. The 0x30-byte difference is one `work_struct`: `dev.h`
adds `chipset_logging_work` only if `SCSC_SEP_VERSION >= 12`, and the modules' Makefile does not define that macro. That is why
`device_state` (`+0x284`), `sdev` and `vif_mutex` match and the earlier modules worked.

Current state of `awdl7` and `awdl8`: `send_one` is back to the earlier version, with no `start_stop_mutex`, and the thread
stops at the first error, which would have avoided the first panic. Mode 2 is gone. If Android closes `wlan0` during a run
with no earlier error, the race is still open, in `awdl5` too. Any `slsi_dev` offset a module uses is checked first against
the real `scsc_wlan.ko`.

`awdl7` in this form was loaded on 5745 MHz with `en2` on 149: 312 PSFs and 312 MIFs in 40 s, 0 failures, a clean unload and
root intact. The layer's log reached `rx af` 36608 from 36352, the only visible step given the granularity of 256, and
`rx data` is still 0. With no `run` going, `peers` and the clock were not read in this run `?`. `awdl8` was loaded again
for §11. The phone can return to 2.4 GHz on boot, where the Mac does not hear it: check the BSSID before loading.

Nothing reached the Mac: `rx data 0`, 0 input packets on `awdl0` and no new `ndp` entry. `check` still says READY and `en2`
answers. Reception of AWDL data over the air and the Mac's answer to a peer remain untested `?`. The first real data frame
will be the iPhone's.

## 9. Cadence on 149, with no time sharing

Monitor on 5745 MHz for 29.65 s, no peer, with `en2` on 149 at 80 MHz and `mcc` not advancing in the layer's log.

| Measure | With time sharing (§4) | On 149 | iOS 26 (§4) |
|---|---|---|---|
| PSFs per second | 1.96 | **8.80** | 8.7 / 7.6 |
| MIFs per second | 1.91 | **2.83** | 3.0 / 4.6 |

- The air confirms the log's cadence. The MIF/PSF ratio is 84/261 = 0.32 in the capture and 329/1024 = 0.32 in the layer.
- PSFs go out every 112 TU at the median (p10 96, p90 112). The maximum of 224 TU is a single PSF not heard.
- The sequence advances +1 in 341 of 344 steps and +2 in the other 3. The +4 and the Nulls of §3 are gone, because there are
  no windows to open.
- TLVs 7 and 17 are still in PSFs and MIFs, in the same order.

This cadence depends on the channel the access point chooses, which has changed once already (§8). If it returns to 153,
time sharing returns. `Tools/iphone_test.sh check` shows the channel.

## 10. Election over the air on 149

Four runs of the A53 as a peer, with `Tools/iphone_test.sh run` going, `en2` on 149 and no time sharing. The peer sends a PSF
and a MIF every 110 ms. What changes is the metric it announces, the master it declares and the count in TLV 24. The Mac
announces metric 512 in all of them. In owly's captures, 525 is the metric of the iPhone, which is always master, and 60 is `METRIC_INIT` of owly's Linux stack,
which follows it. So the Mac should be expected to follow an iPhone, since 512 loses to 525. The metric 60 case does not imitate
an iPhone.

| Measure | Metric 300, declares itself master | 525, fixed count | 525, running count | 60, declares the Mac master |
|---|---|---|---|---|
| Length and peer frames | 40 s, 313 + 313 | 40 s, 313 + 313 | 60 s, 469 + 469 | 40 s, 313 + 313 |
| Frames received per `rx af` | 626 of 626 | 626 of 626 | 938 of 938 | 626 of 626 |
| Master the Mac announces | Itself | The peer for 11 s, then itself | The peer for the whole run | Itself |
| Master change after the first frame | None | 0.196 s | 0.399 s | None |
| Mac PSFs per second during the run | 8.89 | 8.87 | 8.88 | 8.87 |
| Mac MIFs per second during the run | 2.85 | 2.87 | 2.85 | 2.85 |
| Mac minus peer, median in TU | -0.3 | -0.4 | -0.4 | -6.7 |

**Reading.**

- A peer with metric 525 wins the election. The Mac's log shows `programChipWithNewMaster` with `HC 1 met 525`, and the Mac
  starts announcing the peer as master in TLV 4 and TLV 24. The cadence does not change.
- The count has to advance. TLV 24 carries the top master's count at +12 and the sender's at +36. The kernel calls the first
  one RLFC. With a fixed value, the Mac drops the master after 11 s, with the peer still sending, and logs `STALE RLFC`. With a
  count that advances every 2.097 s, it follows the peer for the 60 s, with no warning.
- While following the peer, the Mac copies the master's count into +12, holds its own at +36 and reprograms the chip on each
  new value. As master, its count advances every 3.146 s, about 3072 TU, and starts advancing again when it takes that role back.
- When the peer goes quiet, the Mac is master again after 3.7 s. `peers` stays at 1 until between 101 and 106 s after the last
  frame in the three runs measured, then goes to 0. `cached` does not leave 0.
- A follower peer does not disturb the Mac: it remains master, and the difference between clocks stays at -6.7 TU (p10 -7.5, p90
  -5.9), with no alignment. That value belongs to this run, since the two clocks are independent.
- The kernel's election and the layer's clock decide separately. With metric 300, the kernel keeps the Mac as master and the clock
  follows the peer to within 1 TU, because `alignClock` accepts a peer that declares itself master (§7).
- On 149 the MIF goes out in the third AW with `peers` at 0 as well: `aw % 4 == 2` in 44 of 44 and 45 of 45 MIFs ahead of two
  runs. With the metric 525 peer it is 165 of 171, and the other 6 go out in the second AW.

The peer did not stop or report errors in any run, and the phone kept root.

**What this does not prove.** The frames take only the metric values from iOS, and they do not come from an AWDL stack. An iPhone may
require something more. `rx data` is still 0, so data reception over the air remains untested (§11).

## 11. Data from the A53: the firmware does not send it

No path tried gets an AWDL data frame out of the phone, so the A53 cannot test data reception on the Mac.

**Method.** Module `awdl10` sends from `__init`, with no thread, so a firmware failure leaves nothing sending. Each frame's status
is read from `MLME_FRAME_TRANSMISSION_IND` with a tracefs kprobe on `slsi_rx_frame_transmission_ind`. The offsets come from the
real `scsc_wlan.ko`: `skb->data` at +0xD8, `host_tag` at +0xC and `transmission_status` at +0xE. On the Mac, the readings are
`awdl0`'s `Ipkts` and the layer's `data` counter.

**Control with `awdl8`, 24 PSFs and 24 MIFs per mode.**

| Mode | Destination | Source | Status of the 48 frames |
|---|---|---|---|
| 3 | `awdl0` | The phone's | `SUCCESSFUL` |
| 4 | An address nobody has | The phone's | `RETRY_LIMIT` |
| 1 | `awdl0` | `02:a5:3e:00:00:01`, not the phone's | `RETRY_LIMIT` |

The T9UH acknowledges what is addressed to `awdl0`. This replaces the deduction in §8.

**Data frame.** Frame control `0x08` (`0x88` in one row), the AWDL BSSID and an IPv6 Echo to `awdl0`'s link-local address, in the
format the layer expects.

| Path | Message type | Result |
|---|---|---|
| `wlan0`, `MLME_SEND_FRAME` as 802.11 | 0 | Refused, `-22` |
| Same | 1 EAP, 4 ARP, 5 DHCP | Accepted, 3 of each type, no transmission notice |
| Same | 8 `ANY_OTHER` | Accepted, no transmission notice on the 5 frames read out of 46 |
| Same, `0x88` | 8 `ANY_OTHER` | Accepted, 3 frames, no transmission notice |
| Same | `0x10` `ACTION` | The firmware goes down on the first: `WLBT FW PANIC 0x04`, subcode `0x189c` |
| `mon0`, on 5745 MHz | 8 and `0x10` | Refused, `-22`, as is a control PSF |

`MA_UNITDATA` was not tried, since in `tx.c` it carries only 802.3 and the firmware builds the header from the vif, with no room
for the AWDL BSSID. On the Mac, `Ipkts` stayed at 0 through the whole series and the layer counted `data 0` after the first 41
frames with `ANY_OTHER`.

- The failure in §8 is reproducible: one data frame with `ACTION` is enough. Sent from `__init` there is no kernel panic, the phone
  does not reboot, it keeps root and was associated again 38 s later.
- The missing notice alone does not prove the frame stayed in, though it matches nothing reaching the Mac.

**The Mac's side, untested over the air.** Nothing read so far blocks these frames. Live, `RCR` is `0xF400608E`, with `CBSSID_DATA`
off, `RXFLTMAP2` is `0xFFFF`, and port 1 is ad-hoc with `awdl0`'s MAC and the AWDL BSSID. In `validate_recv_frame`, no check drops
the frame before the AWDL hook. The first real data frame will be the iPhone's, and the layer logs it as
`awdl: first data packet up`.

## 12. The A53 as a witness: the Mac's data and an iPhone's frames

**The Mac's data over the air.** With the phone in monitor on 5745 MHz, `ping6 -c 16 -i 1 ff02::1%awdl0`. This is the first time
this stack's data frames have been seen over the air. It is multicast, so it does not measure the ACK, which §13 measures.

| Measure | Result |
|---|---|
| Frames heard / sent per the layer | 16 / 16 (`sent` goes from 1 to 17) |
| Frame control, retries | `0x0800`, retry bit at 0 |
| Addresses | A1 `33:33:00:00:00:01`, A2 `awdl0`, A3 the AWDL BSSID |
| Header | SNAP `aa aa 03 00 17 f2 08 00`, `03 04`, a sequence advancing by 1, `86dd` |
| Rate | 12 Mb/s |
| Position | 15 within 1 TU of the slot's start, and 1 at 16 TU, the start of the second AW |

The counter's 13 `old` frames predate this test and did not grow. The slot clock is the Mac's own: a frame at the very start of the
slot may arrive before a peer changing channel is listening. That doubt is unmeasured `?`, and §16 develops it.

**An iPhone's frames.** Module `awdl11` sends the 327-byte PSF and the MIF of an iPhone from `receive-ios26.x`, with the original
TLVs 4, 5, 6, 7, 12, 17, 18, 21 and 24. Only the address, channel 44 for 149, the clock and the TLV 24 count change. TLV 2 is removed,
since the capture holds it anonymized as zeros, so the services part cannot be tested. In 40 s, 313 + 313 go out with no failure, and
the Mac follows that master from the first frame until 3.7 s after the last.

An equal run with `awdl9`'s frames, right after, gives the same messages from the kernel, `wifip2pd`, `sharingd`, `rapportd` and
`mDNSResponder`, once numbers and addresses are normalized: 495 and 509 lines, with no message of its own in either, other than the
single `topMasterChanged`, which differs in its tail. IO80211 takes these TLVs without complaint, but it does not react differently
either: with no TLV 2 there is nothing to connect to.

**For a real test.** `telefono/vigia_mac.sh` keeps the phone as a witness on a fixed channel while the test runs. It separates two
cases the Mac cannot tell apart on its own: an iPhone that sends no data and a data frame that is sent and does not reach the layer.
The phone has no Wi-Fi meanwhile.

## 13. ACK of unicasts sent with port 1's MAC

The layer took for granted that the chip recognises ACKs addressed to the MAC of `awdl0`, which is port 1's. To measure it, the Mac
sends UDP every 20 ms to a link-local address with a static `ndp` entry on `awdl0`, and the phone listens on 5745 MHz. Each run
changes the destination.

| Destination | Distinct frames | Copies per frame | With the retry bit | ACKs heard to `awdl0` |
|---|---|---|---|---|
| Phone in monitor, which does not answer | 475 | 7 in 469 of them | 2841 of 3315 | 0 |
| Phone associated, with a monitor on top | 95 | 7 in 72 of them | 514 of 602 | 0 |
| The network's router | 482 | 1 in all | 0 of 482 | 482 |

**The chip recognises those ACKs.** The router answers its address in hardware even with a foreign BSSID. In all 482 frames, the
next thing heard is an ACK to `awdl0`, and the T9UH repeats none of them. The TX descriptor does not set the port
(`SET_TX_DESC_PORT_ID_8814A` is unused in the tree), and the match still works with port 1's MAC.

**With no ACK, each frame uses all its tries.** 7 copies go out, the limit of 6 retries plus the first, and first to last takes
1.8 ms at the median and 4.8 ms at most. The few frames with fewer copies prove nothing: the monitor may have missed some, or the
capture may have cut them off.

**The associated phone does not work as a peer for this.** There are still 7 copies in a window of about 2 s with the monitor active,
so its chip does not acknowledge these frames or the ACK does not arrive. The cause is unknown `?`: it may be the foreign BSSID or
power saving. It would not have heard itself either. That is why the router is the destination that decides the measurement.

What this does not cover: a peer that changes channel, A-MPDU aggregation with Block Ack and an iPhone's ACK, all still untested `?`.

## 14. Defect: the layer drops IP packets of 1429 to 1484 bytes

`ifconfig awdl0` gives MTU 1484, and the TX ring holds 1428 (`AwdlTxFrame.frame`, `AWDLInterface.h:517`). `queueData` counts what does
not fit as `bad` and returns no error to the stack, so the sender does not find out.

Measured with unicast UDP, 5 sends per size and the phone in monitor. Each packet that goes out is heard 7 times, since nobody answers.

| IP size | Frames heard |
|---|---|
| 1348 | 35 |
| 1428 | 35 |
| 1429 | 0 |
| 1448 | 0 |
| 1484 | 0 |
| 1485, which the stack fragments | Only the last fragment, of 61 bytes: 35 |

`bad` goes from 0 to 20 in that run, the 4 sizes by 5 sends.

**Effect on TCP.** The Mac's SYN announces MSS 1424 and, in owly's captures, the iPhone's announces 1440. A full segment with
timestamps then reaches 1484 IP bytes, and every full segment from the Mac to the iPhone would be lost. Short segments pass, so a
connection can open and then stall at its first large send. PMTU black hole detection is on
(`net.inet.tcp.pmtud_blackhole_detection`, MSS 1200) and might rescue it after several retransmissions, at a time cost that is
unmeasured `?`.

**Why it did not show earlier.** The virtual peer's bridge uses a `utun` interface with MTU 1428 (`AWDL_PAR_VIRTUAL.md` §8): the Mac
sees an MSS of 1368 and never goes over the limit. Multicast pings do not show it either, since the stack fragments them at 1280.

**With MTU 1428 the defect goes away.** `sudo ifconfig awdl0 mtu 1428` takes effect at once, with no rebuild or reboot. The same
measurement gives MSS 1368 in the SYN and loses nothing: sizes 1348 and 1428 go out whole, and the larger ones go out fragmented, at
1424 bytes plus the rest. The layer queues and sends 59 packets, the 50 of UDP and 9 SYNs, and `bad` stays at 20. owly's MSS, 1390,
transfers with an iPhone, and 1368 is close to that value.

The same command does not undo the change: `ifconfig awdl0 mtu 1484` answers `Invalid argument`, so the system allows at most 1428, the
value registered in `RegInfo`, even though the interface starts at 1484. Without the fix below, a reboot should be expected to bring back
1484 `?`.

**Where the 1484 comes from.** `IO80211VirtualInterface::peerToPeerConfigureIfnet` (IDA, 25G241) runs inside `prepareBSDInterface` and calls
`ifnet_set_mtu` with 1500 if the controller is `AppleBCMWLANCore` and with 1484 otherwise, over the value in `RegInfo`. It is the only place
in the family that gives an AWDL interface that value.

**Fix in the layer, checked at boot.** `RTLAwdInterface::prepareBSDInterface` calls the family's function and leaves the MTU at what fits in
the ring, 1428. After a boot with that layer, `ifconfig awdl0` reads 1428 unasked and the log says `prepareBSDInterface 0x0, MTU 1428 -> 0`.
`Tools/iphone_test.sh run` still sets the value and warns if it cannot.

**Scope of a fix.** Enlarging only the ring is not enough: `rtl8814native_radio_tx_80211` limits the frame to `MAX_XMIT_EXTBUF_SZ` (1536)
minus the descriptor, and a frame with 1484 IP bytes takes 1524. Reception has no such cap: `AWDL_RX_FRAME_MAX` is 2048, and an
iPhone's 1526-byte size remains unmeasured `?`.

## 15. Block Ack and channel sequence in owly's captures

**Block Ack is not required to transfer.** The iPhone asks for ADDBA on TID 0, with window 64 and A-MSDU allowed, about 4 times per
second in each burst. None of the three captures holds a response, and data passes anyway.

| Capture | Direction | Frames | Type | Retries |
|---|---|---|---|---|
| `receive` | iPhone to owly | 1986 | QoS, TID 0, no A-MSDU | 154 |
| `receive` | owly to iPhone | 1868 | No QoS, short | 0 |
| `send` | owly to iPhone | 346 | No QoS, 1450 IP bytes | 0 |

The layer does the same as owly: a category 3 frame is counted as `other` and freed. `deliverData` takes QoS with and without A-MSDU.
What accepting the ADDBA would add with this chip remains unmeasured `?`.

**The Mac's sequence fits inside the iPhone's.** They match at rest, with each capture's channel:

| Device | Common channel in slots | Other channel |
|---|---|---|
| iPhone at rest | 0, 2 and 10 | 6 in slot 8 |
| owly at rest | 0, 2 and 10 | None |
| Mac as measured, on 149 | 0, 2 and 10 | None |

So the Mac sends no data while the iPhone is on 6. With traffic, the iPhone widens its sequence, up to every slot but 8, and owly
copies it in under 1 s. In `receive`, 1979 of the 1986 frames fall in a slot where both are on the same channel. Whether the Mac's
manager widens its own too remains to be seen `?`. If it does not, data passes in only 3 of 16 slots: it works, but more slowly.

## 16. Risk: the Mac's data goes out at the slot's edge

The layer sends what is queued at each AW tick, and only in served slots: 3 of 16 at rest. Nearly everything generated between slots
then goes out at the first tick of the next one. The position is estimated with the clock in the sender's TLV 4, with an error of a
few TU.

| Sender | First copies | In the first TU |
|---|---|---|
| Mac, unicast to the router | 482 | 391 in the first 4 TU |
| Mac, multicast (§12) | 16 | 15 in the first TU |
| iPhone to owly, in slots where owly's sequence has just changed value | 355 | 6 in the first 3 TU |
| iPhone to owly, without that change | 1477 | Even spread: about 25 per TU |

The three slots the Mac serves at rest, 0, 2 and 10, come after one in which the iPhone announces 0, so this is the case where it is
most likely to be arriving from another channel. owly's rule AWDL-16 asks for a 3 TU guard at the edges for unicast and, for a
fixed-channel node, for multicast too. The Mac does not meet it at the starting edge. With no ACK, a frame uses its 7 copies in about
2 ms (§13), and a multicast goes out only once.

None of this proves those frames are lost: how long the iPhone takes to change channel is unmeasured `?`. If they are lost, the symptoms
would be many unicast retries and a Mac mDNS the iPhone does not see.

**Change applied.** At a slot's start, `txTick` sends no data if more than 500 µs remain to the 3 TU (`AWDL_DATA_GUARD_TU`), and arms itself
again for that moment. Management frames do not change. The 12 ms budget starting at 3 TU still ends inside the 16.4 ms AW.

Measured with multicast and `telefono/mcast.sh`: 390 frames, previously 15 of 16 in the first TU.

| TU of the slot | 0 | 1 | 2 | 3 | 4 | 5 to 14 | 15 | 16 or more |
|---|---|---|---|---|---|---|---|---|
| Frames | 1 | 11 | 208 | 135 | 5 | 0 | 6 | 24 |

The group moved to TU 2 and 3. The clock error cannot separate those two values, so the measurement fits the 3 TU asked for without showing
that no frame goes out earlier. The 12 in TU 0 and 1 remain unexplained `?`. Those from TU 15 on are the ones queued during the guard, which
go out at the next AW's tick.

Measured with unicast to the router and `telefono/ack_ap.sh`: 494 frames, one copy of each, no retry bit and an ACK right after all 494.

| TU of the slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 to 14 | 15 | 16 or more |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Frames | 0 | 6 | 89 | 113 | 104 | 85 | 51 | 3 | 0 | 17 | 26 |

Unicast spreads more than multicast because each frame waits for the previous one's ACK. The guard does not affect ACK matching. Whether this
helps with a peer that changes channel is still unmeasured `?`.

## 17. Cadence with `en2` not associated

This is point 3 of §5. Monitor on 5745 MHz, no peer, 2026-11-06, with `en2` just disconnected from 153 and `mcc` not advancing. The manager announces
`[6 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0]` in `Low Power`, and the layer leaves the radio on 149 (log `parking the radio on channel 149`).

| Measure | Not associated, 119.8 s | Not associated, 29.6 s | Associated on 149 (§9) |
|---|---|---|---|
| PSF per second, outside the silences | **8.89** | 8.81 | 8.80 |
| MIF per second, outside the silences | **1.90** | 1.88 | 2.83 |
| PSF and MIF per second, everything | 8.36 and 1.79 | 7.76 and 1.65 | 8.80 and 2.83 |
| Silences over 0.5 s | 2, of 3.56 and 3.59 s | 1, of 3.52 s | none in that capture |

- **Fewer MIF, from fewer slots.** TLV 18 on the air puts 149 only in slots 2 and 10. Slots 0 and 8, on 6 in the manager's sequence, are announced with channel 0 because the radio does not go to 6. An iPhone
  at rest then shares 2 of its 3 slots (§15), against the 3 of the case associated on 149.
- **The silences have a cause.** With no association, `locationd` asks every 60 s for a scan of channels 1, 6 and 11. `airportd` reports it finished in 0.5 s and `rtlRadioScanCopy` comes 500 ms after `rtlRadioScanStart`, while what shows on
  the air lasts 3.5 s. The layer adds the other 3 s: `infraTouch` marks 3000 ms of infrastructure quiet (`kInfraQuietMs`, `RTLAirLayer.cpp`), and `txTick` only serves a radio not associated with `!infraBusy()`. That is 0.5 s plus 3 s, as
  measured. It is the likely explanation of the gaps of about 3 s per minute already seen with the virtual peer not associated.
- **What it implies.** The 3.5 s stay below the 6 s after which the Mac's manager stops counting on a peer. How much an iPhone tolerates is unmeasured `?`. During those 3 s the radio listens but sends no PSF, MIF or data, and data
  waiting over 1.2 s is discarded, so a transfer would slow down once a minute `?`.
- **Change applied (layer `001EF268`, commit 6741f35).** The quiet protects against asking for a channel change just before an association. If the radio already sits on the parking channel nothing needs asking: `txTick` keeps
  serving and `parkRadio` only holds back the request while `infraBusy()`. Measured 2026-11-07, 119.8 s on 5745 MHz with `en2` not associated: 8.81 PSF/s and 1.89 MIF/s, no silence over 0.5 s and two of 0.38 s at
  18:19:22 and 18:20:23, 60 s apart, which are the `locationd` scans. `en2` rejoined 153 on the first try (18:23:05) and the ping to the router gave 0 % loss.

The router moved on its own from 149 to 153 that same day at 17:58, with nobody touching it. With the router on 153, this is the case that applies to the iPhone test ([AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md) §3).
