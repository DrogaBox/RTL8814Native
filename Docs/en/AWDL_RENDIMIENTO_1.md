# AWDL: receive Block Ack, dense presence and TX rate ladder

Changes of 2026-11-09 for AirDrop speed in both directions, taken from the design of
`github.com/Dino-VN/MtkWireless` 1.0.3 (MT7921, AWDL in software, about 4 MB/s iPhone → Mac) and adapted to
the RTL8814AU. Tested in units outside the kernel and compiled; the air with an iPhone is still missing. What
has not been seen carries `?`.

| Piece | Build |
|---|---|
| `RTL8814Native.kext` | UUID `9B662F1B`, md5 `d06f7b4a` |
| `RTL8814AirLayer.kext` | UUID `8FB89DDD`, md5 `cfd4f92e` |

The two go together: the new layer with the previous driver still works, but without the rate ladder (fixed
12 Mb/s).

## 1. Starting point

- **RX.** The iPhone requests a Block Ack agreement (ADDBA, TID 0, window 64, A-MSDU allowed) about 4 times per
  second during a burst ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §15). The family never answers it
  ([AWDL_BLOCK_ACK.md](AWDL_BLOCK_ACK.md) §1), and the layer dropped it as `other`. Without an agreement the
  iPhone sends one MPDU per medium access: 64 MB at about 800 KB/s on 2026-11-08.
- **TX.** AWDL data went out at a fixed 12 Mb/s, through the management queue, with no feedback other than the
  end of the USB transfer. The theoretical ceiling is about 1.2 MB/s before headers and windows.
- **Windows.** At rest awdld leaves the data channel in 2 of 16 slots, and a peer only transmits in the slots the
  other announces. MtkWireless measured that awdld does not change that sequence during AirDrop.

## 2. Receive Block Ack (layer)

**The MAC sends the BlockAck in hardware `?`.** The vendor driver programs no receive session in registers,
not even for its own AP: `add_ba_rsp_hdl` only sends the ADDBA response as a management frame and enables its
software reordering (`core/rtw_mlme_ext.c`, `core/rtw_wlan_util.c:3548-3553`), and the only `HW_VAR_AMPDU_*`
writes are TX. Even so, in infrastructure it accepts the AP's ADDBA (`bAcceptAddbaReq`,
`Source/Driver/VendorProbe.c:328`) and receives A-MPDUs. A sender that gets no BlockAck after an A-MPDU retries
every subframe, so most probably the MAC generates it on its own. Not verified for the port 1 direction in ad hoc
mode, which is the `awdl0` one; the per-frame ACK in that direction is measured.

More evidence in the same direction. `REG_RXFLTMAP1` keeps only bit 10 (PS-Poll), so the MAC keeps BlockAck and BAR
frames and never passes them to the host (`hal/rtl8814a/usb/usb_halinit.c:545-552`). `RCR_VHT_DACK` picks ACK or BA
as the response per frame (`include/hal_com_reg.h:1115`). In Linux, rtw88 (which now includes the 8814A) and rtl8xxxu
do not touch the hardware on `IEEE80211_AMPDU_RX_START`: they only return 0. The Block Ack table registers
(`REG_BACAMCMD` 0x654, `REG_BACAMCONTENT` 0x658) are written by no driver and have no documented fields, so nothing is
written blind. What remains open is whether the MAC ties the session to a peer with no MACID entry, which is the AWDL
case. The test is an A53 capture on the channel: a BlockAck (subtype 9) from the Mac to the iPhone, one SIFS after
each A-MPDU burst.

What is left to the host (`Source/AirLayer/AWDLInterface.cpp`, `handleBlockAck`, `rxReorder`):

1. **ADDBA response.** Status 0, the same token, the A-MSDU, immediate policy and TID bits of the request, a
   window of 64 at most and the same timeout. It goes to the front of the TX queue for the peer's next window
   (`queueMgmt`): behind the data it could expire after 1.2 s. A delayed Block Ack, a TID above 7, a buffer size
   of 0 or a port 1 that is off are declined with 37.
2. **Repeated ADDBA.** If the session for the same peer and TID exists and the starting number falls inside the
   window, the session stays and only its window takes the size of the new request. Restarting it would raise
   what it holds ahead of the frame the peer is about to resend.
3. **DELBA.** The peer's, as initiator (bit 11), closes its session and raises what it held in order. Four DELBAs
   in 10 s turn the answer to 37 for 60 s: if the MAC did not send the BlockAck, the iPhone would tear the
   agreement down again and again, and that is how the single MPDU of before comes back.
4. **Reorder window.** Per (peer, TID), with a 12-bit sequence number and up to 64 frames, 8 sessions. A frame in
   order goes up at once together with the following ones that were waiting. An early one waits, one beyond the
   window slides it, and one behind is discarded. A gap older than 100 ms is skipped: the RX thread checks it with
   every action frame, or `txTick` wakes it if the peer goes quiet. Frames without QoS, group frames and frames of
   a session that does not exist go straight up, as before. A session is not closed for being idle: the peer keeps
   the agreement and would go on sending A-MPDUs, which would go up unordered. It ends with the peer's DELBA, with a
   new ADDBA or when the least recently used of the eight makes room.

The boot-arg `rtlawdl_ba=0` declines every request, as before. Counters in the layer's log (`RTLAwdlLog` in
ioreg): `ADDBA from … -> status`, `DELBA from …` and, every 256 action frames, `ba req … accept … decline …
delba …`.

## 3. Dense presence (layer)

A 250 ms sample with 10 or more data frames, received plus sent, counts as busy. With two busy samples among the
last four (1 s) the layer announces it in TLV 18 and also serves the slots that the family's sequence 129 leaves at 0;
once on, every busy sample keeps it, and it goes off 2 s after the last one (`updateDense`, `servedMask`). The
two-sample rule is from layer `AA2AA873`: with a single one, the bursts of about 16 frames of `rapportd`'s TLS retries
turned it on and off every few seconds (§5). In `AA2AA873` the two samples had to be in a row, which almost never
happens with dense presence off: data goes out only in slots 2 and 10, 524 ms apart, in short bursts at the start of
each slot, so the busy samples fall every other one (§5). The four-sample window is from layer `ED053DB1`. The slots the family puts on another channel are still not served. It does
not turn on while the radio shares time with a BSS on a non-social channel (`_mccActive`).

In those extra slots the window belongs to the layer: data and MIF are sent, but events 41 and 43 are not posted
(`_awPosted`), because the family's sequence says 0 there and event 41 carries flags the manager compares with
its own. The boot-arg `rtlawdl_dense=0` turns it off. The log marks `dense presence on/off`.

## 4. TX rate ladder (driver and layer)

**Per-frame report.** The 8814 returns a CCX report by C2H for every frame with `SPE_RPT`: end by retries or by
lifetime, number of retries and final rate (`include/rtl8814a_cmd.h:118-125`). The C2H dispatch is already built
(`Source/Compat/VendorOsDep.c:585-598`), but `C2HTxFeedbackHandler_8814` only did something with
`CONFIG_XMIT_ACK`, which is off. Now it counts every report (`rtl8814native_awdl_tx_report`,
`Source/Driver/AWDLTx.c`).

**ABI.** Two new functions in `Source/Driver/RTLRadioABI.h`, without changing `RTL_RADIO_ABI`: `rtlRadioAwdlRate`
sets the rate of unicast AWDL data as a descriptor index (0x04–0x0b OFDM 6–54 Mb/s, 0x0c–0x13 MCS 0–7; 0 returns
to the fixed 12 Mb/s), and `rtlRadioAwdlTxStat` returns the counters: clean, with one retry, with two or more,
lost, final rate of the last one and total. A previous driver answers `kIOReturnUnsupported` and the layer stays
at 12 Mb/s.

**Descriptor.** Frames with a set rate carry `SPE_RPT` and `DATA_RATE_FB_LIMIT` at OFDM 6 Mb/s. The hardware rate
drop on retries stays on (`DISABLE_FB` at 0), so a frame that fails at a high rung can go down to a lower rate on
its retries `?`: the exact shape of the drop depends on the MAC's `DARFRC` table (`0x430`–`0x437`) and shows in
the final rate of the reports. `update_mgntframe_attrib` now sets `data_tries` and the new field to 0, because
pool frames are reused without being cleared. Before, in sim mode, an infrastructure management frame could
inherit a retry's limit.

**Ladder** (`updateRate`, from `txTick`). OFDM rungs 6, 12, 24, 36, 48, 54 Mb/s and, with `rtlawdl_ratemax=6`,
MCS 7 (65 Mb/s); the default top is 54 Mb/s. It starts at 12 Mb/s, the rate of before. Rules:

- **Up.** 32 clean frames on the rung climb one; the new one stays on trial until another 32 gather, and once
  confirmed it needs another 32 before the next one is tried.
- **Down.** One failure during the trial goes down at once and waits 256 × 2^n clean frames before trying again
  (n grows up to 4 with each failure and goes down with each confirmed rung). On a confirmed rung, two failures
  without 128 clean frames between them go down one.
- **Failure.** A lost frame, one with two or more retries, or two or more with one retry in the same tick count
  as a failure. A single retry is usually the peer still on another channel.
- **Floor.** With no acknowledged frame yet (the virtual peer never sends an ACK) it does not go below 12 Mb/s.
- **Counters.** They are read while frames go out and for 8 ticks after, because the reports arrive late. If they
  go back to zero (an `RTL8814Native` reloaded under a live layer) they become the new base, with no decision.

**No reports: the rate from the signal.** Measured on 2026-11-09 with layer `1B702083` and driver `9B662F1B`: over
984 unicast data frames with `SPE_RPT` the report counter stayed at 0, and the driver received 32 C2H packets in all
since boot. `REG_TX_RPT_CTRL` (0x4EC) reads 0x3E3, with the enable bits set. The vendor tree ships `CONFIG_XMIT_ACK`
off for this chip. The RTL8814AU therefore sends no per-frame CCX report, and the ladder has nothing to feed on. After
64 frames sent with no report at all the layer switches to picking the rate from the peers' signal (`rateByRssi`): the
RSSI of their frames, smoothed over 8, with a floor per rung of −70 dBm for 24 Mb/s, −65 for 36, −60 for 48, −55 for
54 and −50 for MCS 7. Going up takes 3 dB more and at most one change per second; going down is immediate; with no
peer heard for 5 s it returns to 12 Mb/s. The hardware's rate fallback on retries covers a frame the rung was too fast
for. If reports show up, the ladder takes over again. The thresholds are an estimate (about 10 dB over the OFDM
sensitivity of each rate) `?` and get tuned with the final rate the capture shows.
`rtlawdl_rate=0` keeps the fixed 12 Mb/s. The air budget per window (`AWDL_TX_AIR_US`, 12 ms) now counts each
frame's cost at the current rate: before it always computed it at 12 Mb/s. The log shows `tx rate X -> Y Mb/s`
with the counters.

## 5. Tests performed

- **Outside the kernel**, with the functions copied as they are from the source and AddressSanitizer:
  - reordering, 13 cases: in order, gaps filled, duplicates, timeout, the 4095 to 0 jump, window slide, restart,
    frames without QoS, window full the other way;
  - `handleBlockAck`, 13 cases: response format, repeated ADDBA with held frames, far restart, DELBA, declines,
    DELBA block and its expiry;
  - ladder, 17 cases: climb to the top, failed probe with wait, tolerance of one retry, first failure on a
    confirmed rung as a strike and the second as a drop, drop to 6 Mb/s, floor without ACK, counters back to zero,
    late reports, driver without support, and the signal mode with its hysteresis.
- **Virtual peer.** `awdlsim --addba` requests the agreement like an iPhone, checks the answer and sends its data
  as QoS with one in eight frames swapped with the next (`Tools/awdlsim/src/ba.rs`, with its tests).
- **Virtual peer with the layer loaded (2026-11-09, layer `6C8A1475`, driver `9B662F1B`).** The radio was sharing
  time with a BSS on 153. The peer requested the agreement and got `status 0 window 64` in half a second; the layer
  log gave `ADDBA from … tid 0 win 64 ssn 256 amsdu 1 -> status 0`. The neighbour entry appeared by itself. Through
  the `utun` bridge 445 of 450 echoes of 200 bytes went through, sent as QoS with 49 swapped pairs. Reordering was not
  exercised: the data started more than 30 s after the agreement, and that day's layer closed an idle session after
  30 s, so the frames went straight up (`total held 0`). That is where the rule of 2.4 not to close on idle comes
  from. Dense presence turned on and off once during a scan of the BSS; it no longer turns on while the radio belongs
  to a BSS on another channel. The periodic TX log carries one more line with the rate, the chip's reports and the
  dense presence state.
- **Repeat with layer `1B702083` (2026-11-09, 20:45).** Same setup, with the echoes from the first second. The
  agreement was accepted as before. Of 600 echoes 592 came back, and the peer sent 67 swapped pairs. The layer passed
  up all 592 frames (`up 592 bad 0`), held 105 until the order was complete (`total held 105`), skipped 7 gaps older
  than 100 ms and dropped none as old or duplicate (`old 0 dup 0`). The 8 lost echoes and the 7 gaps would be frames the
  simulation port refused while the radio was off the channel (1662 refusals in the run) `?`. After a pause of more than 30 s, a second run of 400
  echoes (389 back) kept entering the same session: `total held` went from 105 to 174 with `old 0 dup 0`. Reordering
  and the persistent session are verified in the kernel. The chip sent no TX report at all (§4), so the rate stayed
  at 12 Mb/s in that run.
- **Rate from the signal with layer `8FB89DDD` (2026-11-09, 21:20).** Same setup. After 64 data frames with no
  reports the layer logged `no TX report from the radio for 64 data frames: rate by the peers' signal` and right after
  `tx rate 12 -> 54 Mb/s by signal (peers at -40 dBm)`, the RSSI the virtual peer declares. 490 of 500 echoes came
  back and reordering kept working (`up 490, total held 86, old 0 dup 0`). The virtual peer hears the copy of each
  frame before it goes on air, so this run proves the logic and that the driver takes the rate, not reception at
  54 Mb/s over the air.
- **AirDrop Mac → virtual peer with no BSS (2026-11-09, 22:25, layer `8FB89DDD`).** With `en2` disassociated
  (22:25:21) the radio stayed parked on 149 with no time sharing (`mcc` did not move), and dense presence came on for
  the first time on hardware (`dense presence on`). The Mac uploaded 64 251 343 bytes in 42.07 s, 1527 kB/s, at an
  even pace from start to end: 1 MB every 0.65 s. With the radio shared with a BSS on 157 the same setup had given
  220 kB/s ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10.4). The layer sent at 54 Mb/s from the signal (`tx rate
  54 Mb/s by signal, reports 0`), and during the upload dropped 77 frames on a full queue and 48 as old out of about
  48 000 sent (the `dropped full` and `old` counters before and after). How much comes from dense presence and how
  much from the unshared radio was not separated: that needs another run with `rtlawdl_dense=0`. The virtual peer has
  no radio, so this measures the Mac's TX scheduling, not reception over the air.
- **The same transfer seen over the air (2026-11-09, 22:30 to 22:34).** With the A53 in monitor mode on 149
  (`telefono/vigia_mac.sh 5745 240`) during two more uploads (a 28 MB APK and a 30 MB random file, 1537 and
  1545 kB/s): 91 209 data frames from the Mac to the peer, all at OFDM 54 Mb/s (`Tools/ba_capture_report.py`), with
  45 592 retries, one per frame. The retry comes from the setup: the virtual peer has no radio and sends no ACK, and
  with it the driver keeps a single retry (`data_tries = 1` in `Source/Driver/AWDLTx.c`); with two tries the hardware
  does not get to step the rate down. The rate the layer picks goes out on air as is. With a real peer that confirms
  the first try, the air time per frame would be half. 26 332 of the frames went out in their slot's first AW and the
  rest in the rest of the slot.
- **AirDrop virtual peer → Mac with Block Ack (2026-11-09, 22:39, layer `8FB89DDD`).** `airdrop_send.py` sent the
  same 31 457 280-byte random file from the peer (`awdlsim --addba`, session accepted, one frame in eight swapped with
  the next). `/Upload` ended with 200 2 s after accepting, and the file in `~/Downloads` has the same SHA-256 as the
  original. The layer passed up 23 911 data frames (`up` from 37 254 to 61 165, `bad 0`), held 3033 to reorder them
  (`total held` from 4964 to 7997) and skipped no gaps and dropped no old or duplicate frames (`gaps skipped 24`,
  `old 0 dup 0`, unchanged). The peer's frames come in through the simulation port, not over the air, so those 2 s
  measure the layer's and the stack's receive path, not the radio. For `sharingd` to open 8770 it took the A53's BLE
  advertisement (`Tools/airdrop_beacon`, §6 of [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md)) and stopping
  `airdrop_recv.py`, which holds the same port: with it running `sharingd` failed with `NWError` 48 (address in use).
- **Dense presence with short bursts.** After the upload, dense presence went on and off every few seconds (25
  changes in the run) on bursts of about 8 frames each way: `rapportd`'s TLS retries to the peer (§10.5 of
  [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md)). Each burst passes the threshold of 10 frames in 250 ms and leaves every
  free slot announced for 2 s. It does not affect a transfer; how often a real peer causes such bursts is not measured `?`.
  At 22:44, after some 20 more minutes with the peer rotating its `at=` every 30 s, the changes were at 595.
- **Dense presence with two samples in a row (2026-11-09, 23:00, layer `AA2AA873`).** With the rule of §3 loaded,
  `en2` off the network and the radio parked on 149, the Mac uploaded 31 459 142 bytes to the virtual peer in 20.58 s,
  1529 kB/s, the same as with the previous layer. Dense presence turned on once when the upload started and off once
  when it ended: 2 changes in all. In the next minute `rapportd` opened 7 TLS connections to the peer and there were
  no further changes. The TX queue dropped 28 frames as full during the upload (`dropped full 28`).
- **UDP without dense presence (2026-11-09, 23:04-23:07, layer `AA2AA873`).** 1300-byte UDP from the Mac to the
  virtual peer over `awdl0` (no TCP, no frames back), radio parked on 149: at 500, 1000, 1500 and 2500 packets/s and
  unlimited the peer got 157, 184, 190, 200 and 200 frames/s. Dense presence turned on in none of the five 8 s runs
  (`dense 0 (2 changes)`): with it off only slots 2 and 10 are served, each burst fits in one 250 ms sample and the next
  comes 524 ms later, never in the adjacent sample. About 100 frames per served slot are the ceiling of two slots per
  period. The rest was lost in the 64-frame queue (`dropped full` reached 1 654 417). Fix: four-sample window (§3),
  layer `ED053DB1`, measured in the next point. The AirDrop upload did turn it on; whether that came from the ACKs of
  the virtual peer, which does not keep to the slots, is not verified, nor whether an iPhone would have turned it on `?`.
- **UDP with the four-sample window (2026-11-09, 23:14-23:15, layer `ED053DB1`).** Same sweep, same setup: at 500,
  1000, 1500 and 2500 packets/s and unlimited the peer got 445, 827, 1123, 1125 and 1052 frames/s, against 157 to 200
  with `AA2AA873`. Dense presence turned on in each run with fewer than 200 frames sent and off when it ended: 10
  changes in 5 runs. The ceiling of about 1125 frames/s (1.46 MB/s of payload) shows from 1500 packets/s on; the cause is
  that seemed most likely was the air budget per AW; the next point rules it out.
- **Larger budget (2026-11-09, 23:31-23:32, layer `207B418D`, `rtlawdl_air=15000 rtlawdl_lastaw=1`).** Same sweep:
  450, 901, 1107, 1134 and 1139 frames/s. With 25 % more air per AW and data also in the slot's last AW the ceiling did
  not move, and the radio ended 729 batches with a transient refusal (`busy 729` in about 32 400 frames). With the
  virtual peer the limit is the radio's TX queue, not the layer's budget. The virtual peer sends no ACK, so every frame
  goes out twice over the air (the A53 capture of 22:30); with a peer that acknowledges, the same air would carry twice
  the frames and the budget would count again. How much it rises with an iPhone is not measured `?`.
- **TX queue full while receiving.** At some point between 22:30 and 22:41 the TX data queue dropped about 6900
  frames as full (`dropped full` from 228 to 7122). Most likely it was the 30 MB peer → Mac upload in 2 s, in which
  the Mac sends back thousands of TCP ACKs and the 64-frame queue only drains in the windows, but the log has no time
  and this is not verified `?`. The file arrived whole anyway.
- **Review.** A review of the code against 802.11-2020, the vendor's reordering (`check_indicate_seq`) and
  mac80211 produced the fixes of points 2.1, 2.2, 2.3, expiry without frames and the 8 sessions. It confirmed
  correct the field format, the `out[]` bounds, the 12-bit arithmetic, the ownership of each mbuf and the lock
  order. A second review, of the driver and the ladder, produced the fixes of the rung confirmation, the counters
  going back to zero, the late reports and the C2H report length. It confirmed the descriptor fields in the
  management branch the AWDL frames take, the `MRateToHwRate` mapping and that no other path sets `SPE_RPT`. Still
  open: an event 41 without its 43 when TX goes off with a window open, a defect older than these changes.

## 6. What the 2026-11-08 capture shows

`Tools/ba_capture_report.py` reads an A53 monitor capture (radiotap, pcap or pcapng) and summarises each direction:
ADDBA, DELBA, BAR, BlockAck with its distance to the previous data frame, A-MPDU bursts from the radiotap A-MPDU
status field, rates and ACKs. On the iPhone → Mac transfer of 2026-11-08 (`vigia_20261108_211409`):

| | iPhone → Mac | Mac → iPhone |
|---|---|---|
| Data frames | 1371, all QoS TID 0, none in a burst | 559, no QoS |
| Dominant rate | VHT MCS 1, 20 MHz (13 Mb/s), 100 % | OFDM 12 Mb/s, with 4 at 9 and 3 at 6 Mb/s |
| With the retry bit | 702 | 43 |
| ADDBA | 9 requests, no response | — |

- **Hardware rate fallback.** The Mac's frames at 9 and 6 Mb/s, with the rate fixed at 12, are the hardware's rate
  fallback on retries (`DISABLE_FB` at 0). The ladder of §4 relies on it.
- **The iPhone's first try is not seen.** Of the iPhone's 1337 distinct sequence numbers, 668 first appear with the
  retry bit already set: neither the A53 nor, apparently, the Mac saw the first try. When the A53 does see a
  transmission, the Mac nearly always acknowledges it (only 25 retries follow a transmission with no visible ACK). The
  iPhone never goes above MCS 1.
- **Hypothesis `?`.** The iPhone sends the first try at 40 or 80 MHz, which a 20 MHz receiver cannot decode. The layer
  advertises HT at 20 MHz only, but the VHT capability always implies 80 MHz, and awdl0 is at 20 MHz. If so, the iPhone
  loses one try per frame and its rate control stays at MCS 1, which limits iPhone → Mac as much as the lack of
  aggregation does. The A53 sees no frame wider than 20 MHz from anyone, so the capture neither confirms nor rules it
  out.
- **Two ways out.** Park the AWDL radio at 80 MHz around 149 while not associated, or stop advertising VHT and keep
  HT at 20 MHz (MCS 7, 65 Mb/s). The second changes `getVHT_CAPABILITY` in `RTLAirLayer.cpp`, which belongs to another
  session.
- **Test.** The driver has a live option for the width the idle radio parks at on 5 GHz, 20 MHz by default
  (`RTL_OPT_AWDL_BW`, `rtl8814native_awdl_set_park_bw` in `Source/Driver/AWDLTx.c`). With
  `sudo Tools/test_uc awdlbw 80` channel 149 becomes the primary of an 80 MHz block centred on 155, without a reboot.
  The Mac's frames still go out at 20 MHz on the primary, because the management descriptor never sets `DATA_BW`.
  At the visit the iPhone's rate and retry bit are compared at 20 and at 80 MHz, with the same script.
- **Checked on the Mac (2026-11-09, 22:41, driver `9B662F1B`, `en2` not associated).** `Tools/test_uc awdlbw 80` needs
  no root. The registers go from 20 to 80 MHz: baseband mode `0x8AC` bits 1:0 from 0 to 2 (sub-channel in bits 5:2),
  `0x483` to `0xA4`, and RF `0x18` from `0x53D95` (channel 149, bits 11:10 at 3, 20 MHz) to `0x5319B` (channel 155, the
  centre, bits 11:10 at 0, 80 MHz). AWDL went on the same with the virtual peer, and a 31 459 097-byte AirDrop Mac →
  peer gave 1539 kB/s, the same as at 20 MHz, with 28 frames dropped on a full queue. `awdlbw 20` puts RF `0x18` back
  to `0x53D95` and bits 1:0 of `0x8AC` back to 0. The only thing that matters about the change is still untested:
  whether an iPhone sends at 40 or 80 MHz and the Mac receives it `?`.

## 7. Pending on air

With the virtual peer, after installing and rebooting:

```bash
sudo Tools/awdlsim/target/release/awdlsim --mac 02:a5:3e:00:00:01 --metric 1 --seconds 120 --6e --addba --echo
```

It must print `ADDBA response from awdl0: status 0 window 64`, and the layer log `ADDBA from … -> status 0` with
`held` growing and `old`/`dup` at 0 while the echoes pass.

With the iPhone it remains to be seen:

1. **MAC BlockAck.** Whether the iPhone's A-MPDUs really get a BlockAck (A53 capture on the test channel) `?`. If
   not, DELBAs appear in series and the layer goes back to declining on its own.
2. **Answer without elements.** Whether the iPhone accepts the 33-byte answer without the HT Capabilities
   element; the vendor driver's does not carry it either `?`.
3. **Ladder on air.** How far it climbs and what final rate the reports show `?`.
4. **Speed** in both directions, against the 800 KB/s of 2026-11-08.
