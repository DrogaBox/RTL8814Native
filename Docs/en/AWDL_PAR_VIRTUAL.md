# AWDL: virtual peer on the same Mac

Test of the Mac side of AWDL against a peer running the full protocol (synchronization,
election, channel sequence, TLVs and data), without a second radio or an Apple device. It answers
one question: does the `IO80211AWDLPeerManager` accept a peer, synchronize with it in both roles
of the election and exchange data? Anything not tested on hardware carries `?`.

## 1. How it works

With the boot-arg `rtlawdl_sim=1`, the driver opens a port at the radio boundary:

- **Listen**: every AWDL frame the AirLayer delivers to `rtlRadioTx80211` is copied into a ring of 256
  records, with the timestamp (`mach_absolute_time` in µs, the same base as `awdlNowUs`) and the
  operating channel at the moment of the send. The frame still goes out to the air.
- **Injection**: a peer frame enters through `rtl8814native_awdl_rx`, the same path the
  radio uses when receiving. It only enters if the operating channel matches the peer's, because
  outside it the radio would not have heard it.

`Tools/awdlsim` runs a `crosswind-awdl` node (copy in `Tools/awdlsim/vendor`, MIT, with the change
of `PATCHES.md`) between the ring and the injection. The AirLayer and `IO80211Family` do not
change.

| Piece | Where |
|---|---|
| Ring, owner, wait, abort | `Source/Driver/AWDLSim.cpp`, `Source/Driver/RTLAwdlSim.h` |
| Copy on TX and injection through the RX | `Source/Driver/AWDLTx.c` (`rtl8814native_radio_tx_80211`, `rtl8814native_awdl_sim_rx`) |
| Injection under the lifecycle lock and only in the radio-owning instance | `RTL8814Native::awdlSimInject` |
| Start and abort in `stop`/USB re-plug | `Source/Driver/RTL8814Native.cpp` |
| User client selectors 12-14 | `Source/Driver/RTLUserClient.{hpp,cpp}` |
| Check without the tool | `Tools/test_uc.c`, `sim query/watch/inject` |
| Node, NS answer and echo, pcapng | `Tools/awdlsim` |

The port also captures for a test with a real peer (`sudo ./Tools/test_uc sim capture f.pcapng`,
[AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md)): the ring also takes what the radio hears, with the RSSI,
and the port's channel stays at 0, so data uses its normal retries. The only injection is its self-test's
frame.

Port rules:

- Without the boot-arg, the selectors answer `kIOReturnUnsupported` and the TX copy costs one read.
- The selectors require root (`clientHasPrivilege` over the task owning the connection).
- The client that enables the port is its owner. Only it reads, injects and turns it off, and its
  exit turns it off.
  Another client (`test_uc status`, the app) does not touch it.
- The ring and its lock are reserved once, when the driver boots with the boot-arg, and are never
  freed.
- A radio re-plug (sleep, USB reconnect) turns the port off and wakes the reader with
  `kIOReturnAborted`. The tool re-attaches up to five times.

## 2. Deployment

1. Build the driver (`./build.sh`) and the tool
   (`cd Tools/awdlsim && cargo build --release`, Rust ≥ 1.88). The AirLayer does not change.
2. Deploy the driver and rewrite the layer boot-args with the new variable:

   ```bash
   sudo ./Tools/phase5_deploy.sh "<EFI>" driver
   sudo RTL8814_AWDL=1 RTL8814_AWDL_TX=1 RTL8814_AWDL_SIM=1 ./Tools/phase5_deploy.sh "<EFI>" layer 2
   ```

   `RTL8814_LAYER_SRC`/`RTL8814_LAYER_MD5` point to the installed layer if it is not rebuilt.
3. Reboot. `log show --last boot --predicate 'eventMessage CONTAINS "awdl sim"'` must show
   `awdl sim port ready (rtlawdl_sim)`.

## 3. Pre-check with `test_uc`

With `awdl0` active (AirDrop window open, `ifconfig awdl0` showing `status: active`) and en2
unassociated, or associated on 6, 44 or 149:

```bash
sudo ./Tools/test_uc sim query
sudo ./Tools/test_uc sim watch 149 5
sudo ./Tools/test_uc sim inject 149 <owly capture discovery-ios26...pcap> 20
```

- `watch` lists the PSFs and MIFs of `awdl0`. The delay (`lag`) is the time between delivery to
  the radio and the read.
- `inject` sends iPhone PSFs from the capture with the source MAC changed (default
  `02:00:00:aa:bb:01`). The layer logs `awdl: rx af 02:00:00:aa:bb:01` on its first frames, and the
  `injected` counter rises.

## 4. Tests with `awdlsim`

For the detailed logging of the manager, add by hand to the boot-args in the `config.plist`:
`awdl_log_flags=0xffffffffffffffff awdl_log_flags_verbose=0xffffffffffffffff awdl_log_flags_config=1
wlan.debug.enable=0xff`. On 25G241 `IO80211AWDLPeerManager::initWithInterface` reads them
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.1). On macOS 10.13 the election
metric ended at 505-536 after the first 2 seconds; the value of 26.7.1 is the one the tool prints
(`awdl0 ... m<metric> c<counter>`).

On 25G241 the election compares the root metric first and then its MAC; the counter only counts
among peers following the same root ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md)
§12.4). For S3a it suffices for `--metric` to be above the metric of `M`. The Mac discards as a
«Legacy peer» a peer whose root advertises counter 0, so `awdlsim` uses a random non-zero
counter, and `--counter 0` is not accepted.

`log stream --predicate 'sender == "IO80211Family" OR sender == "RTL8814AirLayer"'` in parallel.
The exact name of the emitter of the election lines is `?` until the first run. `N` is the node's
MAC and `M` that of `awdl0`; both appear in the tool output.

| Test | Command | Passes when |
|---|---|---|
| S1, the node hears the Mac | `sudo Tools/awdlsim/target/release/awdlsim` | Before 5 s it prints `awdl0 is M` and `PeerUp M` |
| S2, the Mac hears the node | the same | Before 10 s the election of the manager stops showing `peers:0` and `N` appears in its logs. Event 47 (`PEER_PRESENCE`, from `removePeer`) during the test means the peer dropped: it fails |
| S3a, the node is master | `--metric` higher than the metric of `M` (`awdl0 ... m<metric>` in the output) | The tool shows `master node`, and the `aligned` counter of the layer's `awdl: tx off` line (when closing AirDrop) is above 0 |
| S3b, the Mac is master | `--metric 1` | The tool shows `master M h1` |
| S4, sync | S3a and S3b for 60 s | `offset max` below 3072 µs (3 TU) with at least 300 samples, without the `lag > 1 TU` warning |
| S5, data | `ping6 -c 20 -i 0.5 fe80::...%awdl0` (the address the tool prints) | At least 18 answers; `ns` and `echo` of the tool and `data queued/sent` of the layer rise |
| S6, time sharing (optional) | en2 associated outside the social channels, `rtlawdl_mcc=1` | The tool follows the channel of the PSFs of `awdl0`, no `PeerDown` in 120 s, `inj_offch` rises. The limit of S4 is not required |

Edge cases:

| Case | Expected |
|---|---|
| Without the boot-arg | `unsupported`; the real AWDL TX and RX unchanged |
| Without `sudo` | `not permitted` |
| Ctrl-C and `kill -9` with a read in progress | `sim query` shows `enabled 0`; no panic |
| `test_uc status` during a test | The port stays enabled |
| Second `awdlsim` | `another client owns the sim` |
| `kill -STOP` 1 s and `-CONT` | `ovr` rises, the node recovers or gives `PeerDown`/`PeerUp` |
| Unplug and plug, or sleep and wake | `aborted` on the read, re-attach or exit after 5 attempts; no panic |

## 5. What it does not test

- The air: RF, the hardware ACK of port 1, the real timing (the mark is the delivery to the
  radio, before the USB) and the chip filtering.
- The retries: the unicast frames from the Mac to the node get no ACK. Data frames go out with a
  single try while the simulation port is active, and management frames keep 6 (the MGNT descriptor
  does not accept a limit of 0). They occupy the air and the management pool during S5.
- Whether an Apple device accepts the Mac's frames: the node behaves like owly, whose frames the
  iPhone accepts.
- AirDrop with `sharingd` on both ends. §8 tests only the receiver, with a custom sender and without
  going over the air, and §10 the Mac's sender against a virtual receiver.

If the manager rejects the node because of a TLV that iOS sends and owly does not (17, VHT
capabilities, [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4), S2 fails with no
fault of the radio. `--capture f.pcapng` saves both directions to compare against
[AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md).

## 6. BLE trigger of AirDrop without an iPhone (with the Bluetooth dongle)

`Tools/airdrop_beacon` is a minimal Android app, in Java and with no dependencies, that emits the
advertisement an AirDrop sender sends with the share sheet open: manufacturer data `0x004C`,
message
`0x05`, length `0x12`, 8 zero bytes, version `0x01`, four 2-byte hashes (the high 16 bits
of the SHA-256 of each given identifier) and a zero byte
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.2). It covers the BLE half that
the virtual peer does not test: that the `sharingd` of the Mac sees a sender and raises its
AirDrop server.

Tested on the A53 (Android 16) on 2026-11-03:

- the log shows `start: 004c 0512000000000000000001973d00000000000000` and `advertising`, with
  `973d` equal to the high 16 bits of `SHA-256("test@example.com")`;
- `dumpsys bluetooth_manager` lists the app's set: legacy advertisement, non-connectable, interval
  of 160 × 0.625 ms (100 ms) and one manufacturer data entry.

Whether a Mac receives it is untested `?`.

```bash
cd Tools/airdrop_beacon
JAVA_HOME=~/toolchains/jdk-17.0.21+9/Contents/Home ~/.gradle/wrapper/dists/gradle-9.6.1-bin/*/gradle-9.6.1/bin/gradle --offline assembleDebug
export ADB_LIBUSB=1
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell pm grant com.drogabox.airdropbeacon android.permission.BLUETOOTH_ADVERTISE
adb shell am start -n com.drogabox.airdropbeacon/.MainActivity --ez start true
adb shell am start -n com.drogabox.airdropbeacon/.MainActivity --ez stop true
```

`--es ids correo@x,+5491100000000` adds up to four hashes. They are only needed for the «Contacts
Only» mode; how `sharingd` normalizes emails and phone numbers before hashing is unverified `?`.

Test with the UB400, with the Mac on AirDrop «Everyone» and Bluetooth on:

1. `log stream --predicate 'process == "sharingd"'` on the Mac.
2. Start the advertisement on the A53, within a meter.
3. Passes when `sharingd` logs the received hashes (`handleFoundHashes:rssi:`, «In Everyone mode,
   start server», [AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md)) and `awdl0` goes
   active without opening the AirDrop window.
4. With `awdlsim` running at the same time, the virtual node sees the frames `awdl0` starts
   emitting.

Also `log stream --predicate 'eventMessage CONTAINS "AWDL is"'` to see whether AWDL goes on, and
PacketLogger to check the advertisement reaches the Mac's Bluetooth
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.5 and §12.8). With AirDrop on
«Everyone», `sharingd` should open TCP 8770 on `awdl0` (`sudo lsof -nP -iTCP:8770`, §12.7).

Result (2026-11-05, 25G241, UB400, with the preference from
[AWDL_AIRDROP_BLE_CONTROLADOR.md](AWDL_AIRDROP_BLE_CONTROLADOR.md) §8): with the A53 advertisement as the
only stimulus, no Bonjour browse and no AirDrop window, the kernel log goes to `AWDL ON` in 1 s,
`wifip2pd` publishes `<id>._airdrop._tcp.local` and `sharingd` listens on `*:8770`. None of that happens
before the advertisement. Reception on the Mac was measured with `blescan` and the log, without
PacketLogger.

## 7. Layer sync with the master

Since this revision, `alignClock` computes the slot start with the arrival time of the frame
(taken on the RX thread of the radio and stored in the queue metadata) minus the TX delay the
master announces (`TTx,PHY − TTx,Target`, +8 and +12 of the fixed header). It is equation 1 of
Stute et al. 2018 ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.1). Before it
used the time at which the thread call drained the queue.

- The periodic line `awdl: rx af ...` reports `drain lag max`, the largest delay between arrival
  and draining of the last 256 AFs. The first `clock follows` lines report `drain` and
  `sender delay`.
- The measurements of S3a and S4 say whether the change matters: a `drain` in the order of 1 TU
  or more explains part of the error S4 measures.

## 8. End-to-end AirDrop with the `utun` bridge (S7)

`awdlsim --tun` creates a `utunN` interface (needs root) with the link-local address of the node
and MTU
1428, and connects it to the node:

- What the Mac's IP stack sends through `utunN` from the node's address enters the node as
  outbound IPv6 and travels to `awdl0` as AWDL data. Of ICMPv6 only echo and neighbor discovery
  pass, so the router solicitations and MLD reports of the stack do not go out.
- What the node delivers goes up through `utunN`. `utun` has no NDP, so the node keeps answering
  the neighbor solicitations of `awdl0` itself.

On seeing the first PSF, the tool prints both addresses and the sender command.

`awdl0` is `ND6ALT` (§9.3): the replies of `sharingd` to the node do not go out without a neighbor entry.
Before S7, `sudo ndp -s <node link-local>%awdl0 <node MAC>` and `awdlsim --mac <node MAC>
--tun`, with the same MAC. If `ndp -s` answers `No such process`, the `awdl0` route is missing (§9.3).

`Tools/awdlsim/airdrop_send.py` (Python, standard library only) talks to `sharingd` the way a
non-Apple sender would ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.7):

- it binds to the node's address on `utunN` and opens TLS without verifying the certificate
  against `[link-local of awdl0]:8770`;
- it sends `/Discover`, and on another connection `/Ask` (which waits for the user to accept on
  the Mac) and `/Upload`;
- the `/Upload` carries a `cpio odc` archive (magic `070707`) compressed with gzip, with the HTTP
  headers `Content-Type: application/x-cpio` and `TotalBytes`.

`--selftest` checks the cpio archive and the plist without the network. `ditto -x -z` extracts the
generated archive and serves to check the format without going through `sharingd`.

| Test | Command | Passes when |
|---|---|---|
| S7a, discovery | `airdrop_send.py ... --discover-only` with the Mac's AirDrop on «Everyone» | `/Discover -> 200` with `ReceiverComputerName` |
| S7b, send | `airdrop_send.py ... file.txt`, accepting on the Mac | `/Ask -> 200`, `/Upload -> 200` and the file in `~/Downloads` |

If it fails, the `--capture` capture and the `sharingd` log say at which layer.

### 8.1. Result (2026-11-05)

S7a and S7b pass on 25G241 with the UB400, `RTL8814Native` UUID `A40C375B`, `RTL8814AirLayer` 0.32.0
UUID `36B31970` and AirDrop on «Everyone». The A53 advertisement starts the server (§6).

| Step | Measured |
|---|---|
| TLS | TLS 1.3, no client certificate. The node does not announce AirDrop in its TLVs and the Mac accepts the connection anyway. |
| `/Discover` | 200 with `ReceiverComputerName`, `ReceiverMediaCapabilities` and `IsAirDropable: true`, no `StatusCode`. |
| `/Ask` | 200 about 3 s later, on accepting the notice, with `ReceiverComputerName`, `ReceiverModelName`, `SupportsContactExchange` and `IDSSessionID`. |
| `/Upload` | 200 in 1 s. |
| File | `~/Downloads/prueba_airdrop_s7.txt`, 68 bytes, identical to the one sent (`cmp`). |

The `/Upload` needed two corrections, each checked with a send:

| Send | `sharingd` |
|---|---|
| Without `TotalBytes` | «Missing totalBytes / contentType» and it closes the connection. The receiver (`sub_100514F90`) requires both headers and Apple's sender (`sub_100515680`) sends them. |
| `newc` with `TotalBytes` | «Decompression failed», POSIX 5. `ditto -x -z` rejects the same archive with «bad file format». |
| `odc` with `TotalBytes` | «Decompression succeeded» and the file imported. |

`TotalBytes` carried the length of the compressed archive. With a single small file, that does not
say whether `sharingd` uses it for more than progress `?`.

The log also says «Missing preview data» (the `/Ask` carries no thumbnail) and «UPLOAD connection is
not active» when going from `/Ask` to `/Upload`. Both errors appear in the send that works.

Scope:

- Packets between `utunN` and `awdl0` use the simulation port, with no air and no Apple device.
- en2 stayed associated on channel 161 (`AWDL ON:[infra(161) …]`). Most of the node's frames were
  lost for being off channel (`inj ok` 84 against `offch` 2108 in 89 s), and TCP still delivered
  everything. With a large file the timing would differ `?`.
- The BLE preference stays in effect after one reboot.

With layer `1320BA53`, also with en2 on 161, a 1 MiB file arrives identical: `/Upload -> 200` in 14 s,
about 75 KB/s. There is no `Peer Absence` during the transfer; they return at rest (§9.2).

With the adaptive dwell (layer `9CA670C1`), under the same conditions, the 1 MiB file arrives identical in
about 5 s, about 200 KB/s, and there is no `Peer Absence` at rest except after two consecutive scans (§9.2).
The send timings have a resolution of 1 s.

With the 3 TU guard and the MTU of 1428 in the layer (`8E4C4AE5`, [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §14 and §16), with en2 on 149:

| Direction | Measured | Earlier reference |
|---|---|---|
| Node to Mac, S7b with 1 MiB | Identical file, `/Upload -> 200` in 1 s | About 1.5 MB/s over TCP |
| Mac to node, TCP with 4 MiB | Identical data, MSS 1356, 113 KB/s | 110-115 KB/s |

The guard makes neither direction worse. In the second, the layer queued about 3150 frames, with `bad` at 0 and 21 dropped on a full ring,
which TCP sent again. A plain BSD socket gets a reset for what arrives on `awdl0`, so the Mac's side needs an `NWListener` with
`includePeerToPeer`, like `sharingd`'s.

With layer `43ACC5D0`, which feeds the manager's counters, the 4 MiB from the Mac to the node go to 507 KB/s and the 4 MiB by AirDrop to the Mac arrive
identical with `/Upload -> 200` in about 2 s ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5).

## 9. Hardware results (2026-11-03)

macOS 26.7.1 (25G241), T9UH, `RTL8814Native.kext` md5 `34128876`, `RTL8814AirLayer.kext` md5
`b2b805b4`, boot-args `rtlawdl=1 rtlawdl_tx=1 rtlawdl_mcc=1 rtlawdl_sim=1`, no Bluetooth.

### 9.1. How AWDL turns on without AirDrop

Without a Bluetooth controller the AirDrop setting stays at «Nobody» and cannot be changed. AWDL
still turns on with a peer-to-peer Bonjour browse (`Tools/awdlsim/p2p_browse.swift`): the kernel
log goes to `AWDL ON:[infra(0) …] (6/149/149) [6 0 149 0 0 0 0 0 6 0 149 0 …]` in about
4 s. Opening the AirDrop window of the Finder also turns it on
([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md) §6).

```bash
swift Tools/awdlsim/p2p_browse.swift 1800 &
```

In zsh, `log` is a shell builtin: the logs are read with `/usr/bin/log`.

### 9.2. Results

| Test | Result |
|---|---|
| S1 | Passes. `awdl0 is …` and `PeerUp` in under 0.5 s. |
| S2 | Passes with en2 unassociated. The AWDL state shows `peers:1` without absences for minutes. The layer received over 9000 PSFs and MIFs from the node, with a maximum drain delay of 312 µs; it sent over 5000 PSFs without failures. |
| S2 with en2 on channel 161 | Fails: the Mac only visits 149 in 2 of 16 slots and arrives late due to the channel change (2.5 ms average, 4.4 ms maximum). 99 % of the node's frames fall out of channel and the Mac publishes `Peer Absence` every ~15 s. This is the S6 case. |
| S2 with en2 on channel 161 and the early switch (`RTL8814AirLayer` UUID `1320BA53`, 2026-11-05) | Still fails. The change acts (`early 986` in 986 windows, `mcc 1969 fail 0`, 2.45 ms on average and 3.9 ms at most), but at rest 98.4 % of the node's frames fall off channel (`inj ok` +198 and `offch` +12233 in 515 s) and the Mac posts `Peer Absence` every ~14.5 s. Arriving late does not explain the whole problem: with 1 AW frames go missing for over 6 s (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.1). |
| S2 with en2 on channel 161 and the adaptive dwell (`RTL8814AirLayer` UUID `9CA670C1`, 2026-11-05) | Much better. With a peer heard in the last 30 s the radio stays the whole slot (`dwell 4`). Over 240 s at rest 12.9 % of the node's frames get in (`inj ok` +738, `offch` +5006), with no second without frames. There was 1 `Peer Absence` in 8 min 43 s, as two consecutive scans of 3.25 and 3.5 s ended. Without the node it returns to `dwell 1`. The cost on the BSS is in `AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.1. |
| S2 with en2 on channel 161 and slots in the scan back-op (driver UUID `A4BD0F2B`, layer `C9F97715`, 2026-11-05) | Passes with the virtual peer. Six forced scans of 6.0 to 9.3 s, with no `Peer Absence`. During them 7.8 % to 14.2 % of the node's frames get in, with at most 1 consecutive second without frames (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.2). |
| S3b, the Mac is master | Passes: `master <awdl0> h1`. The node follows the Mac with an offset of 0 to 1024 µs. |
| S4 with the Mac master | Passes: 237 s, 2397 valid samples, maximum offset 1024 µs (1 TU; 0 for 10 s and 1024 for 226 s). A single second with the `lag > 1 TU` warning, maximum delay 458 µs. |
| S3a, the node is master (`--metric 600`) | Passes in the first second: `master node h0 m600` and `awdl0 … follows node h1`. |
| S4 with the node master | Passes: 127 s, over 1300 samples, maximum offset 1024 µs, no `lag > 1 TU` warning. Twice, at ~37 s and ~98 s, awdl0 stops sending PSFs for about 3 s (`rx 0`, `offch` rises by 8); it looks like an infrastructure scan `?`. |
| S5, first time (Mac master) | 12 of 15 answers, RTT from 4 to 721 ms (average 311 ms). A static neighbor entry is required (§9.3). The layer brings up the node's 12 data packets (`first data packet up`). |
| S5 (node master) | Passes: 20 of 20 answers, RTT from 13 to 478 ms (average 221 ms), `ping6 -i 1` (0.5 s requires root). |
| 1 MiB over TCP with an air-time budget per AW (driver UUID `2D84BB9B`, layer `8FF43136`, en2 on 149/80, 2026-11-05) | Upload of 110-115 KB/s and download of 1526-1957 KB/s, with 30 frames per window instead of 12. No `Peer Absence`, and pings to the router peak at 73 ms during the upload (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.3). |

The high RTT is expected: data only goes out in the windows of channel 149, 2 of every 16 slots with
the BSS off that channel and 3 with the BSS on 149 (§8.3 of the document above).

### 9.3. Neighbor Discovery on awdl0

`ifconfig -v awdl0` shows `eflags=…ND6ALT,RESTRICTED_RECV,AWDL…`. With `ND6ALT` the kernel does
not send Neighbor Solicitations: the `neighbor solicitation` counter of `netstat -s -p icmp6` does
not change during a `ping6`, and no data frame shows in the capture. The neighbor entry is created
by the system when a service resolves the peer, not by `ping6`. For S5:

```bash
sudo ndp -s fe80::<node>%awdl0 <MAC of the node>
sudo Tools/awdlsim/target/release/awdlsim --mac <MAC of the node>
```

`--mac` keeps the MAC between runs; without it each `awdlsim` uses a new MAC and the static entry
goes stale.

In one boot awdl0 appeared without the route `fe80::%awdl0/64` (`ping6: UDP connect: No route to
host`). It is added with `sudo route -n add -inet6 -prefixlen 64 fe80::%awdl0 -interface awdl0`. On
the next boot it was there. It was missing again on 2026-11-05: `ndp -s`
gives `No such process`, the SYNs reach the Mac and its reply does not go out. With the route and the
neighbor entry the connection is established without restarting anything. The cause is `?`.

### 9.4. Changes made during the tests

- **Layer, `p2pMulticastTx` and `freeMulticastPacket`.** `IO80211AWDLPeerManager::handleMulticastTimer`
  delivers the stashed multicast with `p2pMulticastTx` and frees with `freeMulticastPacket` what
  is not taken. The `IO80211VirtualInterface` bodies do nothing («Function not implemented»).
  `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::p2pMulticastTx` passes them through its send action;
  the layer passes them through `txData`. In these tests it was not called (`mcast batches 0`).
- **Layer, peer cache.** `IO80211PeerManager::preQueuePacket` drops what goes to a peer that is
  not in the cache, and `IO80211P2PDataPathManager::addPeerToFWCache` only marks it if ioctl 143
  (`setPEER_CACHE_CONTROL`: operation at +4, 0 add and 2 delete; MAC at +8) succeeds. The layer
  answers success to 143 and to 164 (`setAWDL_PEER_TRAFFIC_REGISTRATION`), and 8 to 130
  (`getPEER_CACHE_MAXIMUM_SIZE`, the family default). In these tests 143 was not called. The layer
  did not confirm the operation with event 51 either (§9.6).
- **`awdlsim`, HT capabilities TLV.** `IO80211Peer::parseHTCapTLV` only accepts the full 802.11 HT
  element body (26 bytes) starting at byte 2 of TLV 7; Crosswind's had 8 bytes. Without HT or
  VHT, `IO80211AWDLPeer::hasHTorVHTCaps` is false and `addPeerToFWCache` does not cache the peer
  (`vendor/crosswind-awdl/PATCHES.md`).

None of the three changes is by itself the reason S5 passed; the blocker was §9.3.

### 9.5. Pending

- S5 every 0.5 s (with `sudo`) and the RTT: data only goes out in the windows of 149.
- The upload's cost on the BSS with time sharing: the router moved to 149 and its channel cannot be
  set (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.3).
- The 3 lost answers of the first S5 run are `?`.
- The ~3 s gaps without PSFs of awdl0 with the node as master.
- S6 (time sharing) with en2 outside the social channels: it still has to be tried with a real peer over
  the air, also during a scan (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.2).
- The Mac's UMI to a real peer, and what the peer does with it (§9.7).

### 9.6. UMIs and the peer cache

Captures of an AirDrop to the Mac hold 0 unicast action frames from the Mac. There are two separate causes:

- **Nothing asks for a UMI.** With the Mac as the receiver, `isInfraRealtimePacketThresholdAllowed` logs
  `noRegistrations:1` and `cachedPeerCount:0` for the whole run. The layer's data does not force the cache either:
  `txData` does not go through `IO80211PeerManager::preQueuePacket`.
- **The layer did not confirm the cache.** After ioctl 143, IO80211 waits for event 51
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.6). Without it, the peer never becomes cached and `handleUmiTimer` sends it
  no UMI; `handleAirDropTrafficReg` waits 3000 ms and `processUnicastAWSF` 200 ms. The cache counter (`CPC`) also
  rises with each add and only falls when a delete is confirmed, so the cache fills up after 8 adds.

`awdlsim --umi SECONDS` sends the Mac a unicast MIF with flag `0x1000` of TLV 12 and option 1.
`processUnicastAWSF` treats it as a received UMI and caches the peer, which measures this path without an iPhone.

One 60 s run on channel 149, with one UMI every 5 s and the Mac idle:

| IO80211 log | Without event 51 (`ded8cf01`) | With event 51 (`98eb3a8f`) |
|---|---|---|
| UMIs received | 12 | 11 |
| `Peer cache operation timeout` | 12 | 0 |
| Maximum `CPC` | 8, and it does not fall afterwards | 1 |
| `No space to cache peer` | 4 | 0 |
| Adds sent / deletes confirmed | 8 / 0 | 11 / 11 |
| From add to its confirmation | never | 1 to 17 ms |

IO80211 deletes the peer for inactivity 3.4 to 3.6 s after each add. Without the event, the delete does not lower `CPC`.

Regressions with `--tun --umi 5`, 6 runs of 1 MiB over TCP each way, on 149 and with a ping to the router:

| | Without event 51 or UMIs (3 runs) | With event 51 and UMIs (6 runs) |
|---|---|---|
| Upload | 113-115 KB/s | 109-115 KB/s |
| Download | 991-6941 KB/s (one 0.1 s run) | 1098-2004 KB/s |
| Ping during upload, average | 6.4-7 ms | 5.4-13.1 ms |
| Ping during upload, maximum | 85-87 ms | 71-77 ms in 5 runs, 446 ms in 1 |
| Peer absences | 0 | 0 |
| 1 MiB AirDrop | identical | identical |

Those runs had 79 adds, 79 confirmed deletes, 0 timeouts and a maximum `CPC` of 1. The 446 ms run had 2 pings
above 200 ms, did not repeat and its cause is `?`.

Option 1 asks for no reply: `processUnicastAWSF` only replies if the peer has `ivars+82`, and on receive only the
AirPlay options set it, which start other state machines. So that capture still has 0 unicast frames from the Mac.
The UMI the Mac sends is measured with a traffic registration (§9.7).

### 9.7. UMI sent by the Mac

`Tools/awdl_traffic_reg.c` makes with the peer the AirDrop traffic registration a real transfer makes: ioctl 164
(`APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION`, 184 bytes: version 1 at +0, MAC at +4, length and name of the
service `airdrop` at +12 and +16, active at +168), keeps it a few seconds and clears it. A socket `ioctl` on awdl0
answers `EOPNOTSUPP`; the IO80211 framework binds awdl0 in its "compatibility mode" and `Apple80211RawSet` goes over
XPC to `wifip2pd`, which wants the `com.apple.wifip2pd` entitlement. With `amfi_get_out_of_my_way=0x1` an ad-hoc
signature with that entitlement (`Tools/awdl_traffic_reg.plist`) is enough, without root. `com.apple.wlan.authentication`
is not needed: with and without it, the kernel answers "Peer not found" when the node is absent.

`handleAirDropTrafficReg` (`0xffffff80022087ac`) sends the UMI only to a peer with `IO80211Peer::is6ECapable`
(`ivars+1272`); for the others it logs "AirDrop registration ENABLE for legacy peer … skipped UMI".
`parseAwdlDataPathTLVAndTakeAction` (`0xffffff80021b92a0`) takes that bit from the le16 that closes TLV 12 when flag
`0x8000` is set: those are extended flags, and `0x800` is the 6E capability. The Mac announces `0x2d00`; the node
announced 0, and `awdlsim --6e` sets the `0x800`. By the code, with a peer that does not declare that bit AirDrop
sends no UMI at all, and that is not a layer fault; which models declare it is not measured.

With the peer cached, `sendRealTimeUMI` warns "Not safe to send UMI", queues the peer and `handleUmiTimer` sends the
unicast ioctl 149 in the next window of 149 (slot 10), 8 to 433 ms later. Both the enable and the disable of the
registration send one. The unicast 149 carries its own template, without the service TLVs 2, 6, 16 and 21 and with
option 1 in TLV 12. Layer `98eb3a8f` stored it over the template of the broadcast MIFs, which went out without
services until the next broadcast 149 or 225. Layer `240c4d75` stores each pending UMI's template with its
destination and leaves the broadcast one alone.

Two 60 s runs on 149 with `--6e` and a 15 s registration, 15 s after the node started:

| | Layer `98eb3a8f` | Layer `240c4d75` |
|---|---|---|
| "legacy peer" | no | no |
| From add to its confirmation (enable / disable of the registration) | 6 / 7 ms | 9 / 4 ms |
| "sending UMI#0" in the IO80211 log | 2 | 2 |
| Unicast MIFs from the Mac in the capture, 282 bytes, option 1 | 2 | 1 (the second UMI went out 6 s after the node ended) |
| 282-byte broadcast MIFs, without services | 14, for ~1 s after each UMI | 0 |
| 958-byte broadcast MIFs | 197 | 170 |

IO80211 deletes the peer for inactivity 3.8 to 3.9 s after each add, as with received UMIs.

## 10. The Mac as sender: a virtual AirDrop receiver (S8)

§8 tests the Mac's receiver with a sender of our own. This test is the reverse: the node announces itself as an
AirDrop receiver and the Mac's `sharingd` discovers it, resolves it and connects to it over `awdl0`.

### 10.1. How an iPhone announces its receiver

Measured in the 2026-11-08 capture (iOS 27). The iPhone does not use multicast mDNS for this: it carries the service
inside its MIFs, at the end of the frame, in Service Response TLVs (type 2), and marks the names in the bitmap of the
Service Parameters TLV (type 6), which goes in PSFs and MIFs.

Each TLV 2 is one DNS record: length of the name plus the type (u16), compressed name, type (u8), data length (u16),
two zero bytes and the data. Compression codes: `c0 07` is `_airdrop._tcp.local`, `c0 0a` is `_tcp.local`, `c0 0c` is
`local` and `c0 00` ends a name.

| Record | Name | Data |
|---|---|---|
| PTR (12) | `c0 07` | `<id>` + `c0 00` |
| SRV (33) | `<id>` + `c0 07` | priority 0, weight 0, port 8770, `<host>` + `c0 0c` |
| TXT (16) | `<id>` + `c0 07` | `flags=111611` |

`<id>` is 12 random hexadecimal digits and `<host>` is the same name as in the ARPA TLV (16). TLV 6 carries three zero
bytes, an update indicator (u16), a 32-bit mask of the non-zero bytes of a 256-bit filter, and those bytes. With the
AirDrop receiver on, two more bits appear: byte 19, bit 6, for every instance (`_airdrop._tcp.local`), and another one
that changes with the `<id>`. The hash function is not identified `?`, so the node uses an `<id>` the iPhone used,
whose bit (byte 25, bit 6) was read from the capture.

The iPhone and the Mac also announce, always, records of `_applicationservicepairing._tcp` and `_appsvcprepair._tcp`
with TXT `at=`, `sid=` and `sn=com.apple.sharingd.AirDrop`, and the Mac one of `_asquic._udp`: that is the new path,
through Rapport.

### 10.2. The `sharingd` filter

`sharingd` (25G241) opens two browsers when the AirDrop sheet shows: an application service one (Rapport) and a
Bonjour one for `_airdrop._tcp` with TXT. Each Bonjour result goes through `sub_1003F0B00` (`SDAirDropBonjourBrowser`),
which reads the `flags` of the TXT:

- with the "nearby sharing" device filter on, it requires bit `0x4000` ("Ignoring … since it is missing nearby
  sharing flag");
- otherwise, with `SFAirDropUserDefaults.pinPairingAirDrop` on and bit `0x10000` in the flags, it drops the result:
  "Dropping legacy endpoint … since the device supports PIN pair, so it should be discovered over Rapport".

The iPhone's flags, 111611 (`0x1B3FB`), carry bit `0x10000`. An iPhone on iOS 27 therefore does not enter the Mac's
sheet through `_airdrop._tcp`: the Mac expects it through Rapport. Only a result that passes the filter gets the
discovery connection (`SDAirDropBonjourDiscoverConnection`): TLS to the SRV port and `POST /Discover`.

### 10.3. Setup

```bash
sudo Tools/awdlsim/target/release/awdlsim --mac <node MAC> --metric 1 --seconds 600 --6e --addba --tun --airdrop
python3 -I Tools/awdlsim/airdrop_recv.py --iface <utun> --addr <node link-local>
```

`--airdrop` adds the three TLV 2s and the two TLV 6 bits to the node's MIFs, with `flags=46075`: the iPhone's without
bit `0x10000` (`--airdrop-flags` sets others). With the iPhone's flags the three records come out byte for byte equal
to the captured ones (test in `Tools/awdlsim/src/svc.rs`). `airdrop_recv.py` listens on the node's address on the
`utun`: TLS with a self-signed certificate, `/Discover`, `/Ask` (accepts) and `/Upload` (counts the bytes), and an mDNS
responder for PTR, SRV, TXT and AAAA. The responder's TXT carries `flags=46075` unless `--flags` is given: with another
value in the node's `--airdrop-flags` the receiver needs the same one in `--flags`, or the Mac sees two different TXTs
(§10.6).

### 10.4. Result (2026-11-09)

Layer `8FB89DDD`, driver `9B662F1B`, radio shared with a BSS on 153.

| Step | With `flags=111611` | With `flags=46075` |
|---|---|---|
| `wifip2pd`: `Received a new discovery result <MAC> [<id>._airdrop._tcp.local] at -40 dBm` | yes, when the sheet opens | yes |
| `mDNSResponder`: PTR and TXT result over D2D (`D2DServiceFound`) | yes, in 2 ms | yes |
| `sharingd`: connection to 8770 | no, in 2 minutes | yes |
| Receiver: `TLS TLSv1.3`, `POST /Discover` (3918 bytes, keys `DeviceSupportFlags`, `SenderRecordData`) | — | yes, `200` |
| `sharingd`: `Got DISCOVER response`, `Adding discovered endpoint` | — | yes |
| The Mac's AirDrop sheet | "No people found" | "awdlsim" under "Other devices" |

`wifip2pd` resolves the PTR and the TXT from the TLV 2s, with no traffic. The Mac also sends multicast mDNS questions
on `awdl0` (188 bytes, 3 questions), which reach the node; the responder's answer came back and `mDNSResponder` logged
it.

Verified on the Mac, with the layer: reading a peer's TLV 2s, publishing the result, resolving the name and the
address, the TCP and TLS connection over `awdl0` and the `/Discover` exchange. It does not test the Rapport path, the
one an iPhone on iOS 27 will use, nor anything over the air: the node has no radio.

**Sending a file.** Dropping a video on "awdlsim" made the Mac open another connection and send `POST /Hello` (141
bytes: `id`, `featureFlags` 3, `contextType`), `POST /Ask` (9383 bytes, with `SenderIdentityAuthTag` and `Files`) and
`POST /Upload` with a `chunked` body. A `/Hello` answered with 404 does not stop the sender ("Receiver does not support
HELLO, skipping"). The family went to `Data` with `handleAirDropTrafficReg: AirDrop registration ENABLE` and the layer
took the rate to 54 Mb/s from the signal. The data came in at a steady pace, 1 MB every 4.2 s (about 240 kB/s), with
the radio shared with the BSS: 10 MB in 42 s. After the tenth megabyte nothing more arrived in 50 s, although
`sharingd` logged `Compression finished` and `Finished sending UPLOAD` 11 and 17 s later; 2 s after seeing no traffic
the family went back to `Idle` (`monitorAWDLDataMode: Tx:0 Rx:0`). The node's run ended there at its 600 s limit, so
the upload did not complete and the reason for the stop is not identified `?`. A first attempt was lost because the
receiver was restarted during the upload. To repeat it: the node's output saved to a file, a long `--seconds`, a 1 or
2 MB file first and `en2` off the network on 153.

**Complete send (2026-11-09, 21:58).** In the next run, with the node's output saved and a 1800 s limit, the Mac
resumed sending the same video by itself as soon as the receiver listened again: `/Hello` (200), `/Ask` (200) and an
`/Upload` of 10 961 703 bytes in 49.7 s, 220 kB/s, with `200` at the end and `Received UPLOAD response` in `sharingd`.
The radio was still shared with the BSS, now on 157. The stop of the previous run, in the last megabyte of the same
file, did not repeat and its cause is still not identified `?`. AirDrop from the Mac to a receiver over the legacy
path is verified end to end: discovery, `/Discover`, `/Hello`, `/Ask` and `/Upload` over `awdl0`. The speed with the
radio not shared was measured later: 1527 kB/s with `en2` disassociated
([AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md) §5).

### 10.5. The Rapport path (2026-11-09, 22:14 to 22:22)

With the AirDrop sheet open, `rapportd` (25G241) starts two `RPPairing` Bonjour browsers:
`_applicationServicePairing._tcp` and `_appSvcPrePair._tcp`. Next to its AirDrop records, the iPhone in the
2026-11-08 capture sent in its MIFs, as TLV 2s, a PTR, an SRV and a TXT for each:

| Record | Name | Data |
|---|---|---|
| PTR | `_applicationservicepairing` + `c0 0a` | `iPhone` + `c0 00` |
| SRV | `iphone._applicationservicepairing` + `c0 0a` | port 5696x, `<host>` + `c0 0c` |
| TXT | same | `sn=com.apple.sharingd.AirDrop`, `at=<12 hex>`, `sid=<UUID>` |
| PTR, SRV, TXT | the same with `_appsvcprepair` | another port; the TXT adds `dnm=iPhone` |

`c0 0a` is `_tcp.local`. The instance name goes as is in the PTR and in lower case in the SRV and TXT. The iPhone
changed `at` and `sid` several times a minute. In the TLV 6 bitmap these records set byte 4 bit 0 and byte 5 bit 5,
which the Mac also sets for its own records (the two service types), and byte 2 bit 3 and byte 10 bit 5 (the instance
`iPhone`).

`awdlsim --pairing` adds the six records, with the instance `iPhone` so those bits apply, ports 56962 and 56963, and a
random `at` and `sid` that change every 30 s. With the iPhone's flags in the AirDrop TXT:

```bash
sudo Tools/awdlsim/target/release/awdlsim --mac <node MAC> --metric 1 --seconds 1800 --6e --addba --tun --airdrop-flags 111611 --pairing
python3 -I Tools/awdlsim/airdrop_recv.py --iface <utun> --addr <node link-local> --pairing
```

The receiver may start before the node: it waits for the `utun` and the address.

| Step | Result |
|---|---|
| `wifip2pd`: `Received a new discovery result … [iPhone._applicationservicepairing._tcp.local]` and `[iPhone._appsvcprepair._tcp.local]` | yes, next to the `_airdrop._tcp` one |
| `rapportd`: `browse_result_changed … @[awdl0]` in both browsers | yes |
| `rapportd`: `Attempting discovery with`, `Starting PairVerify with` | yes |
| `rapportd`: host resolution on `awdl0` and TCP to the port of the `_appsvcprepair` SRV | yes |
| `rapportd`: TLS `Client handshake started` (TLS 1.2 to 1.3) | yes: four 1496-byte ClientHellos at the node |
| PairVerify | no: the node does not answer the TLS |

`rapportd` checks each `at` against its own identity (`verifyAuthTag:data:type:` with type 6, `sub_1000DB590`) and
skips PairVerify on a match ("Skipping PairVerify for our own bonjour device"). A result that changes without a new
`at` is not tried again ("Changed endpoint ignored (same authTag)"), so after a `Connection refused` the next attempt
only came with the new `at`. Closing and opening the sheet did not restart the browse.

In the same run, with flags 111611, `sharingd` logged "Dropping legacy endpoint" and still, 5 s later, sent
`POST /Discover` to the node's port 8770 ("Creating service … Sending DISCOVER request"), and the node showed in the
sheet. The Rapport records are not the cause: the mDNS responder of `airdrop_recv.py` always answered with
`flags=46075` then, and with that TXT the Mac sends `/Discover` even though the node's TLV 2s carry 111611 (§10.6,
tests B and C).

Verified on the Mac, with the layer: the whole Rapport path that does not depend on Apple's identity, from publishing
the records through both `rapportd` browsers to resolution and TCP with TLS over `awdl0`. What is left (PairVerify, the
PIN and the encryption) is done by the Mac and the iPhone between themselves, and neither the driver nor the layer
takes part in it.
`SFAirDropUserDefaults.pinPairingAirDrop`, the condition of the filter in §10.2, is an `AirDropFeatureFlag` property of
`Sharing.framework` created with the key `PINPairingAirDrop` (read with `dyld_info -disassemble`: the library only
exists inside the dyld shared cache and there is no IDA database for it). `/System/Library/FeatureFlags/Domain/Sharing.plist`
ships that key as `FeatureComplete`. `sharingd` also looks the key up in the user's preferences while filtering the
Bonjour results; the CFPreferences debug log names the domain:
"found no value for key PINPairingAirDrop in CFPrefsSearchListSource (Domain: com.apple.Sharing …)".
Tests with layer `207B418D`, driver `9B662F1B`, `en2` disassociated and the node with `--airdrop-flags 111611` and no
`--pairing`. Each one restarted `sharingd`, opened Finder's AirDrop window and read 30 s of
`log stream --info --debug` of the process (the "Dropping legacy endpoint" message is info level and is not persisted):
| Test | Setting | Responder's TXT | "Dropping legacy endpoint" | "Sending DISCOVER request" | "completed discovery" |
|---|---|---|---|---|---|
| A | factory | no receiver | 1 | 0 | 0 |
| B | `com.apple.sharingd PINPairingAirDrop` = NO | `flags=46075` | 2 | 1 | 1 |
| C | factory | `flags=46075` | 2 | 1 | 1 |
| D | factory | `flags=111611` | 1 | 0 | 0 |
| E | `com.apple.sharingd PINPairingAirDrop` = NO | `flags=111611` | 1 | 0 | 0 |
| F | `com.apple.Sharing PINPairingAirDrop` = NO | `flags=111611` | 0 | 1 | 1 |
- B and C are equal: the `com.apple.sharingd` domain does not count, and the `/Discover` of those two tests comes from
  the responder's `flags=46075` TXT. It explains the `/Discover` of §10.5.
- D against F, everything else equal: with the setting off in `com.apple.Sharing`, `sharingd` stops dropping the result
  with bit `0x10000`, sends `POST /Discover` (the receiver answered `200`) and logs `Got DISCOVER response`,
  `Bonjour service … completed discovery` and `Endpoint discovered`.
```bash
defaults write com.apple.Sharing PINPairingAirDrop -bool NO
killall sharingd
```
To go back to the factory value: `defaults delete com.apple.Sharing PINPairingAirDrop` and `killall sharingd`. The Mac
was left at the factory value after the tests.
With the setting off the Mac treats a device that announces PIN pairing as a classic receiver: it finds it through
`_airdrop._tcp` and talks to the SRV port, with neither Rapport nor the PIN. It is an alternative if an iPhone with
iOS 27 does not show in the Mac's sheet through the Rapport path.
**Without a restart, and with a transfer (2026-11-10, 00:06).** With `sharingd` already running, the `defaults write`
alone was enough: opening the AirDrop window gave "Sending DISCOVER request" with no "Dropping legacy endpoint". A file
dropped on the node, still announcing `flags=111611`, went through complete: `/Hello`, `/Ask` and `/Upload` of
31 459 097 bytes in 20.15 s (1561 kB/s) with `200`, and `Received UPLOAD response` in `sharingd`. No PIN pairing
message in the log during the transfer.
Not verified `?`: that an iPhone answers that `/Discover` and accepts the transfer, and whether the setting changes
what the Mac announces or accepts as a receiver.
