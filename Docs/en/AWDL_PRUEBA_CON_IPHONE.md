# AWDL: test with a visiting iPhone

Script to make the most of a visit from someone with an iPhone: 15 to 20 minutes, with the iPhone always in its
owner's hands. It is the first test with a real peer. What has been measured so far uses the virtual peer
([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md)) and, over the air, only PSFs from an A53 to the Mac.

The visit has three questions, in this order:

1. Does the iPhone reach the Mac? Its frames go through the chip and the driver, and
   `IO80211AWDLPeerManager` adds it (`peers:1`).
2. Does the Mac reach the iPhone? The iPhone accepts its PSFs and MIFs and shows it in AirDrop.
3. Does a file cross the air, in each direction?

Even if the first one fails, the capture of what the iPhone sends is useful after the visit.

What has not been seen on hardware goes with `?`.

## 1. What is in the code

| Piece | Where | Commit |
|---|---|---|
| Diversion in `validate_recv_frame`: every frame with A3 = `00:25:00:FF:94:73` goes to the radio ABI's AWDL hook, with RSSI and operating channel | `Source/rtl8814au/core/rtw_recv.c`, `Source/Driver/RTLEthernetAdapter.cpp`, `Source/Driver/VendorGlue.cpp` | `ccc4122` |
| `awdl0` hands each action frame to `IO80211VirtualInterface::bpfTapInput` on the work queue | `Source/AirLayer/AWDLInterface.{h,cpp}` | `3b53d2a` |
| PSF and MIF emitter, AW clock, windows, port 1 | `Source/AirLayer/AWDLInterface.cpp` | `cceb743`, `7e454be`, `7c95ead`, `62a2c49` |
| Data over `awdl0` in both directions (§7) | `Source/AirLayer/AWDLInterface.cpp`, `Source/Driver/AWDLTx.c` | `5b686a8`, `5630d49`, `847e46c` |
| Time sharing with the associated network | [AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) | `8bc4690`, `e166043` |
| Capture of both directions at the radio, without changing how it behaves (§2) | `Source/Driver/AWDLSim.cpp`, `Tools/test_uc.c` | `208e50a` |
| Check, collection and summary of the test | `Tools/iphone_test.sh`, `Tools/iphone_frames.py` | `208e50a`, `253929e` |
| `awdl0` MTU, 3 TU guard in the slot | `Source/AirLayer/AWDLInterface.cpp` | `e9a103e`, `e181cba` |
| The manager's counters and holding data if the peer announces another channel ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md)) | `Source/AirLayer/AWDLInterface.cpp` | `70ad06a`, `ca6be5c` |

## 2. Preparation, before the visit

Build of the first visit (the current one is in §8): `RTL8814Native` UUID `DB92193C`, md5 `9440a90b`, and
`RTL8814AirLayer` UUID `001EF268`, md5 `540b6837`. Both are installed together. The disk identifier changes
between boots and a USB stick also mounts as `NO NAME`, so `deploy` looks for the EFI partition of the internal
disk. On 2026-11-05 it was `disk2s1`, and `disk0s1` is a Linux partition.

```bash
sudo Tools/iphone_test.sh deploy
```

A reboot is needed after that step. The check needs no root and changes nothing:

```bash
Tools/iphone_test.sh check
```

It ends in `READY` or `NOT READY`, with each gap marked `MISSING`. It looks at the macOS version, the loaded
UUIDs against `Build/`, the boot-args, Bluetooth and the BLE preference, AirDrop on Everyone, `awdl0`, TX failures,
the channel of `en2` and the free space.

Rehearsal without an iPhone, once after the reboot:

```bash
sudo Tools/iphone_test.sh run
```

It passes if `[+] capture self-test: RX path ok` appears, `tx` rises in the periodic lines, `lost` stays at 0, the
final summary lists the frames and `en2` keeps working. The capture only needs `rtlawdl_sim=1`, already set.

The self-test runs at the start of each capture: it injects a frame through the RX entry and checks that it reaches
the ring. That frame does not go to the file; the layer counts it as `other` and logs
`awdl: rx other 02:00:00:aa:bb:fe`. The stretch from the chip to that entry was checked on 2026-11-05 with the A53's
PSFs (`rx af` +256 on 149). On 2026-11-06 the A53 got root back: it sends PSFs and MIFs, the manager reaches `peers:1`, and it also
works as a monitor to see what the Mac sends ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md)). The rehearsal with that peer and
the capture running was done that day: `HEARD`, `IO80211 peers 1` and the frames in `frames.pcapng` (§7 of that note).

For the visit:

- `awdlsim` stopped: `check` requires it, because it uses the same port and sends data with a single try.
- No BLE advertisement from the A53, so it does not wake `sharingd` by itself. The phone's update left the app
  uninstalled; it needs no root, and `adb install` of the APK from `Tools/airdrop_beacon` brings it back.
- The folder each run leaves carries the iPhone's name and addresses: it stays out of the repository.

## 3. Channel

AWDL uses the social channels 6 (2.4 GHz) and 44 or 149 (5 GHz)
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md)). The chip listens on one channel.

On 2026-11-05, with country `AR`, the Mac's manager uses `(6/149/149)` and at rest the sequence
`[149 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0]`. owly's iOS 26 captures are on 44. That an iPhone in Argentina picks 149
is what the same country suggests, but it is `?`.

`check` says which case `en2` is in. The router changes channel by itself, so the case is read on the day.

| `en2` | Radio | What to do |
|---|---|---|
| Associated with primary 149, 44 or 6 | Stays on that channel, no hops. It is the setup of the A53's reception (149, 2026-11-05). | Stay associated. |
| Associated on another channel | Time sharing: goes to the social channels in their slots ([AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md)). Untested with a real peer over the air. Measured with the A53 on 153: 1.96 PSF/s and 1.91 MIF/s on 149, against 8.9 and 2.9 with no time sharing ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §5). | Disconnect `en2` from the network in the Wi-Fi menu. `en1` gives internet if it has a link. |
| Not associated | The layer fixes the radio at 20 MHz on the social channel with the most slots in the manager's 129 sequence, or on 149 if there is none. Log: `awdl: parking the radio on channel C (was N)`. | The starting case if nothing is heard while associated. Measured with the A53: 8.89 PSF/s and 1.90 MIF/s in slots 2 and 10, and a 3.5 s silence per minute ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17). |

Without an association, the default sequence puts 6 in slots 0 and 8
([AWDL_SIN_ASOCIACION_INFRA.md](AWDL_SIN_ASOCIACION_INFRA.md) §3), so the radio may end up on 6. Forcing another
preferred channel takes a reboot and only acts without an association:

```bash
sudo Tools/iphone_test.sh channel 6
```

`channel 44` and `channel 149` do the same with those channels, and `channel auto` removes it. Each rewrites the
layer's boot-args with the current flags and keeps a copy in `Backups/`.

## 4. During the visit

Before arrival: `Tools/iphone_test.sh check` ends in `READY`. Then:

```bash
sudo Tools/iphone_test.sh run
```

During the run, `system_profiler SPAirPortDataType` is not used: it triggers two scans in a row, 3.25 s and 6 s, with AWDL silent all that time, more than the 6 s after which the manager stops counting on a peer
(measured on 2026-11-06, three times, with the same result). `check` and `run` no longer call it, and `SPBluetoothDataType` does not scan. The system's periodic scan, one of 2.75 s every 5 minutes with `en2` on 149, stays under those 6 s. With `en2` not associated, `locationd`'s leaves 3.5 s of silence every 60 s, also under them ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17).

The run creates `~/Desktop/iphone_<date>/`, keeps the Mac awake and uses no network. The screen shows the frames
heard (`HEARD <MAC> rssi ... ch ...`), changes in `IO80211 peers` and `sharingd`'s notices.

On the iPhone, in its owner's hands:

1. Wi-Fi and Bluetooth on, Personal Hotspot off.
2. Settings > General > AirDrop > "Everyone for 10 Minutes".
3. Unlocked, less than a metre from the dongle. A screenshot or an unimportant photo, Share, AirDrop, with the sheet
   open for the length of the test.

On the Mac: a Finder window on AirDrop.

| Step | What to look at |
|---|---|
| A. One minute with the sheet open | `sharingd: BLE device found`, `HEARD` and `IO80211 peers 1`, in that order `?` |
| B. iPhone to Mac | The Mac appears in the sheet. Tap it, accept on the Mac, and the file arrives in `~/Downloads`. |
| C. Mac to iPhone | The iPhone appears in the Finder window. Drag `envio_chico.txt` from the run's folder, accept on the iPhone, then `envio_1mb.txt`. |
| D. Ctrl-C | The summary appears on screen. `Tools/iphone_test.sh report <folder>` repeats it. |

If the test fails, let it run for five minutes with the sheet open before stopping: what the iPhone sends stays in
`frames.pcapng`.

With the A53 at hand, `telefono/vigia_mac.sh 5745 900` keeps it as a witness in monitor for 15 minutes, with no Wi-Fi meanwhile, and
lists the data frames at the end. It separates an iPhone that sends no data from a frame that is sent and does not reach the layer
([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §12).

With the owner's permission, the iPhone can also be on a USB cable to the Mac. The owner taps «Trust» once.
`check` then says `iPhone on USB and trusted`, and `run` takes the iPhone's side as well: its unified log at Ctrl-C
(`log collect --device`) and the IP on its interfaces (`rvictl`). Both are lockdown services, the same kind
Finder uses: they need neither Developer Mode nor a developer disk image. Trust can be revoked afterwards in
Settings > General > Transfer or Reset > Reset Location & Privacy. Collecting the log takes a minute or two, and the
iPhone stays on the cable until `run` prints the folder. That `rvictl` shows the iPhone's `awdl0` was not checked `?`.

| File | Contents |
|---|---|
| `frames.pcapng` | AWDL frames of both directions at the radio (802.11, `tcpdump -r` reads them); direction, channel and RSSI in each packet's comment, on the mach clock. `capture.txt` gives the equivalence with the time of day. |
| `awdl0_ip.pcap` | IP traffic on `awdl0`: mDNS and the TLS on port 8770. |
| `system.log` | AWDL lines of the kernel, `sharingd`, `wifip2pd` and `bluetoothd`. |
| `ring.txt`, `ring_final.txt` | The layer's own log (`RTLAwdlLog`). Its lines are not in the unified log. |
| `state_start.txt`, `state_end.txt` | Interfaces, neighbours and downloads at the start and at the end. |
| `iphone.logarchive`, `iphone_system.log` | Only with the iPhone on USB: its log since one minute before `run`, and the lines of `wifip2pd`, `sharingd`, `bluetoothd`, the Wi-Fi dext and anything saying AWDL. They carry the owner's data and stay off the repository. |
| `iphone_ip.pcap` | Only with the iPhone on USB: IP on its interfaces, PKTAP (`tcpdump -k I` shows the interface). |

## 5. How to read the result

| What is seen | Reading | Next step |
|---|---|---|
| No `BLE device found` | `bluetoothd` did not pass the iPhone's advertisement to `sharingd`, or the iPhone does not advertise | Close and reopen the sheet. Check Everyone on both sides. |
| BLE yes, no `HEARD`, `tx` rises | The chip does not hear AWDL on that channel, or the iPhone is on another | Disconnect `en2` (§3), no reboot. If nothing changes, `channel 6` and a reboot: the iPhone also goes through 6 `?`. |
| `HEARD`, `rssi` below −70 | The manager drops the AF at the edge threshold ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §1.3) | Bring the iPhone closer to the dongle. |
| `HEARD`, still `peers 0`, `rssi` fine | The manager rejects the AF | Look for `BAD OUI` and `Invalid frame` in `system.log`. The frames stay for comparison with [AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md). |
| `peers 1`, the Mac is not on the iPhone | The Mac hears but the iPhone does not accept it, or does not get it | The iPhone's `unicast` in the summary: at 0 it is not addressing the Mac. Compare own and iPhone TLVs in `frames.pcapng`. |
| `peers 1`, constant `Peer Absence` | The peer drops between windows | See the channel of `en2` (§3) and `dwell` in `ring.txt`. |
| The Mac is on the iPhone, no `TCP to 8770` | Resolution or unicast data fail | `state_end.txt` (`ndp -an`), mDNS in `awdl0_ip.pcap`, and `awdl: first data packet up` in `ring.txt`. |
| `TCP to 8770` without the transfer ending | Lossy data or TLS refused | `sharingd`'s log, and `sent`, `dropped` and `fail` of the `awdl: tx psf` line. |

The ring's `awdl: rx af <MAC> rssi ...` lines only appear for the first 16 frames, then one line per 256 action
frames. A short burst does not show there, but it does in `frames.pcapng`.

## 6. TX: the iPhone sees this Mac

With `rtlawdl_tx=1`, while `awdl0` is enabled and the radio is not scanning, `awdl0` broadcasts a PSF every 110 TU and
a MIF per slot. Each frame carries TLVs 4, 5 and 18 generated by the layer, plus the kernel's sync template (ioctl 151)
in PSFs and the MI template (requests 149 and 225) in MIFs. Without a template nothing goes out. The frames go at OFDM
6 Mb/s with category 127, which is not robust, so with PMF active no BIP is added (`CATEGORY_IS_NON_ROBUST`,
`Source/rtl8814au/include/ieee80211.h:1531`).

Without an iPhone, `check` shows the last `awdl: tx psf ... mif ... fail ...` line: a `fail` other than 0 appears as
`MISSING`.

| What is seen with the iPhone | Reading |
|---|---|
| `awdl: clock follows <MAC> aw ... next slot ... TU` in `ring.txt` | The local clock aligned with the iPhone as master |
| The Mac appears in the iPhone's sheet | TX reaches the air and the iPhone accepts it. There is no other proof of TX over the air: the Mac has no second radio to capture itself. |

With TX active, the windows also run (events 43 and 41 in each slot the 129 sequence puts on the radio's channel;
unserved slots are announced with channel 0), and port 1, which takes the MAC of `awdl0` and the AWDL BSSID in ad-hoc
mode so the hardware acknowledges unicasts (`awdl: port 1 refused` if refused).

## 7. Data over `awdl0`

- **RX** (`5b686a8`): each AWDL data frame received (also A-MSDU subframes) with Apple's SNAP goes up `awdl0` as
  Ethernet II if it is for the MAC of `awdl0` or a group address. In the log, once:
  `awdl: first data packet up from <MAC> ethertype 86dd`.
- **TX** (`5630d49`, `847e46c`): each packet the stack sends over `awdl0` becomes an AWDL data frame (no QoS, Apple's
  SNAP) and waits in a ring of 64 frames for the window of a served slot. Each AW has 12 ms of air at OFDM 12 Mb/s.
  Frames that wait over 1.2 s are dropped. In the log, once: `awdl: first data frame to <MAC> ethertype 86dd len N`,
  and in the periodic line `data queued Q sent S dropped full F old O bad B fail X`.

Known limits:

- the chip recognises ACKs to port 1's MAC, measured against a router ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §13). It remains to
  be seen with an iPhone, which changes channel and may use Block Ack. A frame with no ACK goes out as 7 copies in about 2 ms;
- the layer drops IP packets over 1428 bytes. Since the fix in §14 of [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) `awdl0` starts at 1428, and `check` and `run` warn otherwise. In
  `ring.txt`, the `bad` counter gives away a lost packet;
- the Mac goes to `Data` with traffic and sends in 15 slots of 16, with a ceiling of 568 KB/s at 12 Mb/s, and holds data in slots where the peer announced another channel
  ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5, §6 and §9). `report` counts the passes to `Data`, and the periodic log line carries `held N`;
- an iPhone drops data from a SA its manager does not have as a peer
  ([AWDL_RX_DATA_TO_SKYWALK.md](AWDL_RX_DATA_TO_SKYWALK.md) §6): data only passes once it accepts this Mac's PSFs and
  MIFs;
- an iPhone without 6E does not get the Mac's AirDrop UMI, by IO80211's design (`AWDL_PAR_VIRTUAL.md` §9.7);
- the cost of a peer that misses frames is not measured ([AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) §8.3);
- there is no event 42 (window extensions);
- `awdl0_ip.pcap` depends on BPF seeing `awdl0` `?`: if `tcpdump.txt` has an error, the rest of the run goes on without
  it.
The first visit (2026-11-08) left an AirDrop from the iPhone to the Mac of 64 MB at about 800 KB/s, and the iPhone did
not show in the Mac's sheet. What changed since then:
| Change | Where |
|---|---|
| The Mac publishes the peer's presence by itself: `ndp -s` is no longer needed | `b492c65` |
| Receive Block Ack, dense presence and TX rate by signal | [AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md) |
| 80 MHz width for AWDL, as a live option (`awdlbw`) | [AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md) §6 |
| AirDrop from the Mac to a virtual receiver, complete, 1527 kB/s with the radio unshared | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10 |
| Why `sharingd` drops an iPhone's classic announcement, and the setting that avoids it | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10.2 and §10.6 |
Loaded: driver `9B662F1B` (md5 `d06f7b4a`) and layer `207B418D` (md5 `ba4926eb`) with `rtlawdl_air=15000` and
`rtlawdl_lastaw=1`. A `sudo Tools/iphone_test.sh deploy` with no variables, plus a reboot, brings back the defaults.
- `Tools/iphone_test.sh check` ends in `READY`.
- `en2` disconnected from the network: the radio stays on 149 and dense presence only acts that way.
- `awdlsim` and `airdrop_recv.py` stopped. The receiver holds port 8770 and `sharingd` cannot listen there.
- No BLE beacon from the A53.
- `defaults read com.apple.Sharing PINPairingAirDrop` answers that it does not exist: the setting is at its factory
  value.
```bash
sudo Tools/iphone_test.sh run
```
In another terminal, the live log of `sharingd` and `rapportd`. Info-level messages, "Dropping legacy endpoint" among
them, are not persisted and can only be seen this way:
```bash
/usr/bin/log stream --info --debug --predicate 'process == "sharingd" OR process == "rapportd"' > ~/Desktop/sharing_vivo.log
```
With the A53 as a witness on 149 for 15 minutes: `telefono/vigia_mac.sh 5745 900`.
| Step | What to do | What to look at |
|---|---|---|
| 1. Mac to iPhone | iPhone unlocked with AirDrop on "Everyone for 10 Minutes". Finder's AirDrop window open for a minute. | The iPhone shows in the window. |
| 2. Transfer | `envio_chico.txt`, then `envio_1mb.txt` and a file of 30 to 60 MB. | Acceptance on the iPhone and the time of the large file. |
| 3. iPhone to Mac | A video of 50 MB or more. | Time, against the 800 KB/s of 2026-11-08. |
| 4. 80 MHz | `Tools/test_uc awdlbw 80`, repeat step 3, and `Tools/test_uc awdlbw 20` at the end. | Time with each width. |
| 5. Ctrl-C | In `run` and in the `log stream`. | Summary on screen. |
The Mac and the iPhone of 2026-11-08 were paired by code that night and the Mac still holds the identities
([AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md) §4); they last 30 days. With another iPhone, or once
they expire, the first transfer asks for a six-digit code: the receiver shows it and it is typed on the sender.
Expected path in step 1, in `rapportd`: `Starting PairVerify with:`, `Client PairVerify succeeded` and
`Updated browse client with 1 endpoints`. `Tools/iphone_test.sh run` announces them on screen.
If the iPhone does not show in step 1, `sharing_vivo.log` tells where it stopped:
| What it has | Reading | Next step |
|---|---|---|
| `Client PairVerify failed` or `PairVerify error` | The stored pairing did not work | Send a photo from the iPhone to the Mac: if the Mac shows a code, type it on the iPhone, and repeat step 1 `?`. |
| Neither "Dropping legacy endpoint" nor "Sending DISCOVER request" | The iPhone's AirDrop announcement did not reach `sharingd` | `wifip2pd`: `Received a new discovery result` in `system.log`; `peers 1` and `HEARD` in the run (§5). |
| "Dropping legacy endpoint" and, in `rapportd`, `Attempting discovery with` and `Starting PairVerify with` | The Mac saw the classic announcement, dropped it and looks for the iPhone through Rapport; PairVerify does not finish | Keep the log. Try the setting below. |
| "Dropping legacy endpoint" and nothing from `rapportd` | The iPhone's `_applicationservicepairing` and `_appsvcprepair` records did not arrive | `wifip2pd` in `system.log` and the iPhone's TLV 2s in `frames.pcapng`. Try the setting below. |
The setting makes the Mac treat the iPhone as a classic receiver (verified with the virtual peer; with an iPhone `?`):
```bash
defaults write com.apple.Sharing PINPairingAirDrop -bool NO
killall sharingd
```
Close and open the AirDrop window. The live log must show "Sending DISCOVER request" and "Got DISCOVER response". When
the visit ends, back to the factory value:
```bash
defaults delete com.apple.Sharing PINPairingAirDrop
killall sharingd
```
In `ring_final.txt`, from the layer:
| Line | What it says |
|---|---|
| `awdl: ADDBA from <MAC> … -> status 0` | The iPhone asked for Block Ack and the layer accepted. |
| `awdl: ba req … accept … decline … delba …, total held …` | Sessions and reordered frames. Several `delba` in a row: the iPhone does not get the chip's BlockAcks `?`. |
| `awdl: dense presence on` / `off` | Dense presence turns on when a transfer starts and off 2 s after it ends. |
| `awdl: tx rate … by signal (peers at … dBm)` | The rate chosen from the iPhone's signal. |
| `awdl: tx psf … data queued … dropped full … held … busy …` | `busy` counts transient refusals by the radio; `dropped full`, frames lost to a full ring. |
With the A53 capture:
```bash
python3 -I Tools/ba_capture_report.py <vigia.pcap>
```
It shows whether the Mac answers the iPhone's A-MPDUs with BlockAck, the rate at which the Mac's frames arrive
(54 Mb/s with the virtual peer, not verified on reception `?`), the retries and the width the iPhone uses with
`awdlbw` at 20 and at 80.
