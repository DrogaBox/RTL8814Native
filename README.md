# RTL8814Native

A native macOS driver for Realtek RTL8814AU (4x4 802.11ac) USB Wi-Fi adapters, with AWDL implemented in software so that AirDrop can run on a radio Apple never shipped.

I wrote it because the only options on macOS were closed-source Realtek utilities, and I wanted the adapter to show up in the normal Wi-Fi menu like any other card. Once that worked, I kept going into AWDL.

It is two kexts, loaded through OpenCore:

- `RTL8814Native.kext` is the radio driver: USB transport, the Realtek HAL/PHY code (from `morrownr/8814au`) behind a small Linux-to-XNU compatibility layer, firmware loading, and an in-kernel WPA2-PSK and WPA3-SAE supplicant (4-way handshake, 802.11w).
- `RTL8814AirLayer.kext` publishes the radio to macOS as a native Skywalk/IO80211 interface, so `airportd`, the menu bar and System Settings can scan, join and reconnect on their own. It also creates `awdl0` and runs AWDL on top of the same radio.

## Where this stands

Regular Wi-Fi works and I use it every day.

AirDrop works in the direction I was able to test: an iPhone sent a photo and a 64 MB video to this Mac. Sending from the Mac works against a software receiver I wrote for testing. Sending from the Mac to a real iPhone is implemented and has never been tested.

That last part is the reason this repository is public again. I do not own an iPhone, an iPad or a Mac with Apple Wi-Fi. I had one borrowed iPhone for one evening, on November 8, 2026. Everything written after that day was measured against a simulator. I can keep writing code, but I cannot keep testing it blind, and I am not going to claim something works when I have not seen it work.

If you have the adapter and any Apple device, twenty minutes of your time tells me more than another month of mine. The section [How you can help](#how-you-can-help) says exactly what I need.

If you have neither and still want to see this finished, you can [help me get an iPhone](#help-me-get-an-iphone).

A few things to know before you start:

- **One preview build.** The [Releases](https://github.com/DrogaBox/RTL8814Native/releases) page has a single prebuilt pair of kexts, so that people who want to test do not have to set up a build first. It is exactly what runs on my machine and it has been tested nowhere else. Building from source is still the reference.
- **No support.** I cannot debug installs on machines I cannot see. I read every report and I will answer when I can, but I make no promises about fixes or timing.
- **One setup.** All of this was built and tested on a single machine. Details below.

## What was tested, and how

My setup: a TP-Link Archer T9UH v1 (`2357:0106`) on an AMD Hackintosh, macOS 26.7.3 (25G342), x86_64, with a CSR-based USB Bluetooth dongle. One WPA2/WPA3-transition router on 5 GHz and an Android hotspot that allows WPA3-SAE only. I have not tried any other adapter, board or access point. These are kernel extensions, so a bug can panic the machine. Keep a working copy of your EFI folder and use this at your own risk.

### Wi-Fi

- Scan, join, leave and reconnect from the Wi-Fi menu, on 2.4 and 5 GHz.
- Open, WPA2-PSK and WPA3-SAE networks, with 802.11w (protected management frames). SAE, the 4-way handshake and PMF are done in the driver (`Source/Driver/SAE.c`, `WPA2Supplicant.c`, `PMF.c`), and a WPA2/WPA3 mixed router joins as WPA3. Known quirk: after a reboot without a clean leave, a PMF access point keeps the old association for 30 to 60 s and the first join attempt can fail; the next one works.
- Sleep/wake and unplug/replug.
- Location Services.
- USB 3.0. The adapter enumerates as USB 2.0 and the driver switches it to USB 3.0 when it loads; on a motherboard port it then runs at SuperSpeed (5 Gbps).
- Speed at SuperSpeed, on a 5 GHz 80 MHz link to my 2x2 router: 320 to 360 Mbit/s down and 130 to 330 Mbit/s up in October 2026, before AWDL existed. The Linux in-tree driver gives 288 to 366 down and 227 to 280 up with the same adapter, port and router, so the download side is on par. Measured again on November 13, 2026, with AWDL enabled and sharing the radio: 297 Mbit/s down and 266 Mbit/s up with macOS's own `networkQuality -I en2 -s`, and a 200 MB upload at 245 Mbit/s. At USB 2.0 High-Speed it was about 150 down and 83 up.

### AWDL and AirDrop

| What | State | How it was checked |
|---|---|---|
| `awdl0` exists and `wifip2pd` runs AWDL on it | Works | Daily use |
| The iPhone sees the Mac in its AirDrop sheet | Works | Real iPhone, iOS 27, November 8 |
| iPhone to Mac, a photo | Works | Real iPhone |
| iPhone to Mac, a 64.3 MB video | Works: 80 s, about 800 KB/s | Real iPhone |
| The Mac sees the iPhone in its AirDrop sheet | Failed that day. I found the cause and fixed it afterwards | Fix checked against the simulator only |
| Mac to iPhone, any file | **Never tested** | Nothing |
| Mac to the software receiver, a full file | Works | `Tools/awdlsim` |
| TCP from the Mac over `awdl0` | About 19 MB/s with the radio parked at 80 MHz, about 15 MB/s while Wi-Fi stays associated | `Tools/awdlsim` |
| Wi-Fi stays connected while AWDL runs | Works: one hour associated, link never dropped | `Tools/awdlsim` |
| Block Ack and A-MPDU toward an Apple device | **Never tested.** The Mac requests it and aggregates on the air; whether an iPhone accepts is unknown | Simulator only |
| Receive speed after the reordering work | **Never tested.** The 800 KB/s figure is from before it | Simulator only |
| Sidecar, Universal Control, AirPlay over AWDL | **Never tested** | Nothing |
| iPad, Apple Watch, Apple Silicon or Intel Macs as the other side | **Never tested** | Nothing |

The simulator is a virtual AWDL peer (`Tools/awdlsim`) that talks to the driver through a test port, without going over the air. It is good at finding my own mistakes. It cannot tell me how a real Apple device reacts, and that is everything in the table marked as never tested.

### What does not work

- **WPA2/WPA3 Enterprise (802.1X, certificates).** There is no EAP code in the driver, and I have no network to test it on.
- **WPA/TKIP-only and WEP.** The join code refuses anything but open or CCMP/WPA2-PSK.
- **Following an access point's channel switch (CSA).** The driver parses the announcement and moves without leaving, but I could not make any of my access points announce one, so it is untested. Roaming between bands is implemented but I have not seen it fire.

The `Info.plist` also lists USB IDs of other RTL8814AU adapters (ASUS USB-AC68, Alfa AWUS1900, Netgear A7000, Edimax EW-7833UAC, D-Link DWA-192, and others). Those come from the vendor driver and are untested.

## How you can help

There are three ways to test, depending on what you own, and one way to help if you own none of it. Reports go in a [new issue](https://github.com/DrogaBox/RTL8814Native/issues/new/choose).

### 1. You have an RTL8814AU adapter and an Apple device

This is the one I need most. An iPhone, an iPad or another Mac all count.

You need:

- An x86_64 Mac or Hackintosh on macOS 26.7.x. The layer checks the macOS build at boot; see [After a macOS update](#after-a-macos-update) if yours is not 25G342.
- Working Bluetooth with Low Energy. AirDrop finds devices over Bluetooth before AWDL carries anything. On my CSR dongle that took one preference change, described in [Docs/en/AWDL_AIRDROP_BLE_CONTROLADOR.md](Docs/en/AWDL_AIRDROP_BLE_CONTROLADOR.md).
- Both kexts built and installed with the AWDL boot-args from [Installing](#installing-opencore).
- AirDrop set to Everyone on both sides.
- `Tools/test_uc` built. The one-line command is under [Diagnostics](#diagnostics).

Then:

```bash
Tools/awdl_report.sh check
```

It changes nothing and needs no root. It tells you whether the machine is ready and what is missing. When it passes:

```bash
sudo Tools/awdl_report.sh run
```

Leave it running and do these, in this order, with a file of 50 MB or more:

1. Open the AirDrop sheet on the Apple device. Does this Mac appear?
2. Send the file from the Apple device to the Mac. Does it arrive? How long does it take?
3. Open the AirDrop sheet on the Mac (Finder, right-click a file, Share, AirDrop). Does the Apple device appear?
4. Send the file from the Mac to the Apple device. Does it arrive? How long does it take?
5. If you have time, repeat 2 and 4 with the Mac's Wi-Fi disconnected from any network.

Press Ctrl-C when you are done. The tool asks a few questions and writes `awdl_report.txt`. Attach that file to the issue.

About your privacy: the raw folder the tool collects holds device names and hardware addresses, and it stays on your machine. `awdl_report.txt` is built from it with addresses replaced by labels (`self-1`, `peer-1`) and names removed, and the tool refuses to write it if it still finds one. Read it before you post it anyway. It is plain text, and if you edit it by hand, `Tools/awdl_report.sh verify awdl_report.txt` repeats the check.

A failed test is as useful as a passed one. If step 3 fails, that is exactly the report I am missing.

### 2. You have Apple devices and no adapter

You can still help, with a capture of what Apple devices say to each other. I need to see three things that I can only guess at from the kernel:

- The Block Ack negotiation during an AirDrop: which side asks, with what parameters, and when relative to the first data frame.
- Whether data frames sent to an iPhone on current iOS carry the 8-byte length header that iOS 27 puts on the ones it sends.
- The rates and aggregate sizes two Apple devices settle on.

This takes three devices, because the Mac that captures cannot take part in the transfer:

1. On a Mac with Apple Wi-Fi, hold Option and click the Wi-Fi menu, choose Open Wireless Diagnostics, then Window, Sniffer. Pick channel 149 at 80 MHz. If nothing shows up, try 44, then 6.
2. Start the capture, then AirDrop a file of 50 MB or more between the other two devices, right next to the Mac. Send it once in each direction.
3. Stop the capture. macOS saves the file under `/var/tmp`.

Do not attach the capture to a public issue. It contains the names and addresses of your devices and of every other device nearby on that channel. Open an issue saying you have one, with the device models and OS versions, and I will arrange a private way to receive it.

### 3. You have a different adapter, board or macOS build

Reports about plain Wi-Fi are welcome too: another RTL8814AU model, an Intel Mac, a different macOS build. Say what you ran it on, what worked, and attach the output of `Tools/test_uc status` and `Tools/test_uc joinlog`.

### Help me get an iPhone

Everything above depends on other people's time. Someone runs a test, I change something, and then I wait until they can run it again. With a phone on my desk that same loop takes minutes.

I know what that is worth because I had it once. The evening with the borrowed iPhone showed me defects the simulator could never have shown, and they were fixed that same night, one build after another, with the phone still in the room. Receiving from an iPhone works today because of those few hours. Sending to one is still untested because the phone went home.

I live in Argentina and I do this in my spare time, unpaid. An iPhone is out of my reach right now. It does not have to be new or in good shape. A used one that still runs current iOS is enough, because all it has to do is AirDrop.

If this project is useful to you, or you just want AirDrop on hardware Apple does not sell, you can help pay for it:

From anywhere in the world:

- **GitHub Sponsors:** [github.com/sponsors/DrogaBox](https://github.com/sponsors/DrogaBox), or the Sponsor button at the top of this page
- **PayPal:** `mrleisures@gmail.com`

From a bank or wallet in Argentina (these two do not accept transfers from other countries):

- **US dollars:** alias `drogabox.developer`
- **Pesos:** alias `a.cristianabad`

Any amount helps. When the phone is bought I will say so here.

If you have an old iPhone in a drawer and would rather send that, open an issue and we will work it out.

### Code

Pull requests are welcome when they say how the change was tested. The reverse-engineering notes are in `Docs/`, in Spanish, with English copies under `Docs/en/`. [Docs/en/AWDL_INDICE.md](Docs/en/AWDL_INDICE.md) is the index and [Docs/en/GLOSSARY.md](Docs/en/GLOSSARY.md) explains the terms.

## Building

You need macOS with the Xcode Command Line Tools.

```bash
./build.sh            # Build/RTL8814Native.kext
./build_airlayer.sh   # Build/RTL8814AirLayer.kext
```

Do not compile anything under `Source/rtl8814au/os_dep/linux/`; the OS-specific parts live in `Source/Compat/` and `Source/Driver/`. After building the driver, check that no vendor symbols are left unresolved, because OpenCore silently drops a kext that has any:

```bash
nm -u Build/RTL8814Native.kext/Contents/MacOS/RTL8814Native | grep -E "rtw|rtl|hal|dm_|odm|beam"
```

That must print nothing.

## Installing (OpenCore)

The kexts come from your own build (`Build/`) or from the preview zip on the Releases page; the steps are the same. The preview was validated on build 25G241 and I run it on 25G342, so on any build other than 25G241 it needs `rtlairtrust=<your build>` in the boot-args (see [After a macOS update](#after-a-macos-update)).

1. Copy `Build/RTL8814Native.kext` and `Build/RTL8814AirLayer.kext` into `EFI/OC/Kexts/` on your internal EFI partition. Check which disk you are writing to: a USB stick can have the same volume name.
2. Strip resource forks and sync:
   ```bash
   dot_clean -m EFI/OC/Kexts/RTL8814Native.kext
   dot_clean -m EFI/OC/Kexts/RTL8814AirLayer.kext
   sync
   ```
3. In `config.plist`, under `Kernel -> Add`, add both kexts with `Enabled` set, and put `RTL8814AirLayer.kext` after `RTL8814Native.kext`.
4. Add the boot-args. For Wi-Fi only:
   ```
   rtlair=2
   ```
   For Wi-Fi and AWDL, the set I run with:
   ```
   rtlair=2 rtlawdl=1 rtlawdl_tx=1 rtlawdl_mcc=1 rtlawdl_sim=1 rtlawdl_air=15000 rtlawdl_lastaw=1
   ```
5. Reboot and plug the adapter into a USB 3 port on the motherboard. Avoid hubs: behind my monitor's USB 3 hub the switch to USB 3.0 fails, and the adapter drops off the bus until you replug it. Join your network from the Wi-Fi menu; macOS asks for the password the first time.

What the boot-args do:

| Boot-arg | Effect |
|---|---|
| `rtlair=2` | Loads the layer. Without it only the driver loads and you get a plain interface that you join from the command line (`Tools/test_uc`) |
| `rtlawdl=1` | The layer creates `awdl0` |
| `rtlawdl_tx=1` | AWDL is allowed to transmit. Without it the Mac only listens |
| `rtlawdl_mcc=1` | AWDL shares the radio with the network you are connected to |
| `rtlawdl_sim=1` | Opens the test port that `Tools/awdl_report.sh` and `Tools/awdlsim` use to read frames |
| `rtlawdl_air=15000`, `rtlawdl_lastaw=1` | Transmit time per AWDL window. These are the values the throughput figures above were measured with |

If something goes wrong, remove both kexts and the boot-args from the EFI.

`Tools/phase5_deploy.sh` and `Tools/iphone_test.sh deploy` do these steps for me, including a backup of the EFI, but they are written for my setup.

## After a macOS update

`RTL8814AirLayer.kext` uses private IO80211 / Skywalk layouts of one macOS build, so it checks the running system at boot and stays inert on a build it was not validated on (the plain `RTL8814Native` interface keeps working). Run `Tools/airlayer_gate.py status` to see why it started or not. If the build is new but the structure matches, boot once with `rtlairtrust=<build>` (`Tools/airlayer_gate.py trust-arg` prints it), test the Wi-Fi menu, then `Tools/airlayer_gate.py learn --yes` and rebuild to make it permanent. Details in [Docs/PHASE5_IO80211.md](Docs/PHASE5_IO80211.md#version-gate-032).

## How it fits together

```
 airportd, wifip2pd, sharingd, Wi-Fi menu, System Settings
              |  IO80211 / Skywalk
 RTL8814AirLayer.kext   native interface, scan, join, link quality,
                        awdl0: election, sync, windows, data path
              |  RTLRadioABI (internal)
 RTL8814Native.kext     compat layer, Realtek HAL/PHY, USB pipes,
                        firmware loader, WPA2/WPA3 supplicant,
                        AWDL frame transmit and channel hops
```

The adapter has one bulk IN, three bulk OUT and one interrupt endpoint. Receive runs several transfers in flight on its own worker thread; transmit is queued asynchronously with hardware aggregation. The chip has no AWDL support of its own: the election, the timing and the channel sequence all run in the layer, and the driver moves the radio between the network's channel and the AWDL channel.

## Diagnostics

`Tools/test_uc` talks to the driver through a user client:

```bash
clang -Wall -O2 Tools/test_uc.c -framework IOKit -framework CoreFoundation -o Tools/test_uc
./Tools/test_uc status      # link, RSSI, rates, counters
./Tools/test_uc joinlog     # timeline of the last joins and leaves
./Tools/test_uc peek 0x0290 # read a MAC register
```

`Tools/awdlsim` is the virtual AWDL peer (Rust 1.88 or later, `cd Tools/awdlsim && cargo build --release`). [Docs/en/AWDL_PAR_VIRTUAL.md](Docs/en/AWDL_PAR_VIRTUAL.md) explains what it covers and what it cannot.

## Docs

- [Docs/ROADMAP.md](Docs/ROADMAP.md): what is done, what is open, and why
- [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md): hardware notes, USB rules, and a dated log of bugs and fixes
- [Docs/PHASE5_IO80211.md](Docs/PHASE5_IO80211.md): notes on Apple's IO80211Family and Skywalk, including vtable layouts
- [Docs/SAE_HANDOFF.md](Docs/SAE_HANDOFF.md): WPA3-SAE and 802.11w
- [Docs/en/AWDL_INDICE.md](Docs/en/AWDL_INDICE.md): index of the AWDL notes
- [Docs/en/AWDL_RESULTADO_IPHONE_1.md](Docs/en/AWDL_RESULTADO_IPHONE_1.md): the one test with a real iPhone, and what it broke
- [Docs/en/AWDL_RENDIMIENTO_1.md](Docs/en/AWDL_RENDIMIENTO_1.md) and [AWDL_RENDIMIENTO_2.md](Docs/en/AWDL_RENDIMIENTO_2.md): throughput, Block Ack, sharing the radio with a connected network
- [Docs/en/AWDL_PAR_VIRTUAL.md](Docs/en/AWDL_PAR_VIRTUAL.md): the simulator

The notes separate what was read in Apple's binaries, what was measured on hardware and what is still a guess. Where something was not verified, they say so.

## License and credit

GPL-2.0, see [LICENSE](LICENSE). The Realtek code comes from `morrownr/8814au`. `Tools/awdlsim/vendor/crosswind-awdl` is MIT and keeps its own license file. The IOKit glue, the compatibility layer, the AirLayer and the AWDL implementation are Copyright (c) 2026 DrogaBox.

If you use any part of this, keep the copyright notices and credit the project. The license requires it, and it is the only thing I ask.
