# RTL8814Native

A native macOS driver for Realtek RTL8814AU (4x4 802.11ac) USB Wi-Fi adapters. I wrote it because the only options on macOS were closed-source Realtek utilities, and I wanted the adapter to show up in the normal Wi-Fi menu like any other card.

It runs on macOS Sequoia and Tahoe (x86_64) through OpenCore. It is two kexts:

- `RTL8814Native.kext` is the radio driver: USB transport, the Realtek HAL/PHY code (from `morrownr/8814au`) behind a small Linux-to-XNU compatibility layer, firmware loading, and an in-kernel WPA2-PSK and WPA3-SAE supplicant (4-way handshake, 802.11w).
- `RTL8814AirLayer.kext` publishes the radio to macOS as a native Skywalk/IO80211 interface, so `airportd`, the menu bar and System Settings can scan, join and reconnect on their own.

Without the second kext you still get a working `enX` interface, but you have to join networks from the command line (`Tools/test_uc`).

## Read this first

I built and tested this on one setup only: a TP-Link Archer T9UH v1 (`2357:0106`) on an AMD Hackintosh with macOS 26.7.1, against one WPA2/WPA3-transition router on 5 GHz and an Android hotspot that allows WPA3-SAE only (2.4 and 5 GHz). I have not tried any other adapter, board, macOS version or access point. These are kernel extensions, so a bug can panic the machine. Keep a working copy of your EFI folder and use this at your own risk.

What works on that setup:

- Scan, join, leave and reconnect from the Wi-Fi menu, on 2.4 and 5 GHz. The first join after a disconnect used to drop; that is fixed (the access point ignores a client for about 20 s after a disassociation, so the driver keeps retrying authentication for about 10 s).
- Open, WPA2-PSK and WPA3-SAE networks, with 802.11w (protected management frames). SAE, the 4-way handshake and PMF are done in the driver (`Source/Driver/SAE.c`, `WPA2Supplicant.c`, `PMF.c`), and a WPA2/WPA3 mixed router joins as WPA3. Tested against an Android hotspot (SAE only, PMF required, 2.4 and 5 GHz) and one WPA2/WPA3-transition router. Known quirk: after a reboot without a clean leave, a PMF access point keeps the old association for 30-60 s and the first join attempt can fail; the next one works.
- Sleep/wake and unplug/replug.
- Location Services. macOS positions you from nearby Wi-Fi networks, and it works because the adapter shows up as a normal Wi-Fi interface.
- About 386 Mbit/s down and 291 Mbit/s up on a WPA2 5 GHz 80 MHz link (the adapter runs at USB 2.0 High-Speed on my port). Over WPA3-SAE a 100 MB download ran at about 3.2 MB/s and the ping to the router averaged 2 ms with no stalls.

What does not work, and why:

- **WPA2/WPA3 Enterprise (802.1X, certificates).** There is no EAP code in the driver, and I have no network to test it on.
- **WPA/TKIP-only and WEP.** The join code refuses anything but open or CCMP/WPA2-PSK.
- **AirDrop, wireless Sidecar, Universal Control.** They run over AWDL, which hops channels in sync with other Apple devices. The layer reports the AWDL capability so macOS tries to set it up, but the `awdl0` interface is not created, and I doubt a USB radio can do the channel timing anyway.
- **AirPlay and Handoff.** They should work over the normal network without AWDL, but I have not tested them.
- **Following an access point's channel switch (CSA).** The driver parses the announcement and moves to the new channel and width without leaving, but I could not make any of my access points announce one, so it is untested; if it fails the link drops and the layer rejoins. Roaming between bands is implemented but I have not seen it fire.

The `Info.plist` also lists USB IDs of other RTL8814AU adapters (ASUS USB-AC68, Alfa AWUS1900, Netgear A7000, Edimax EW-7833UAC, D-Link DWA-192, and others). Those come from the vendor driver and are untested.

## Installing (OpenCore)

1. Copy `RTL8814Native.kext` and `RTL8814AirLayer.kext` into `EFI/OC/Kexts/` on your internal EFI partition. Check which disk you are writing to: a USB stick can have the same volume name.
2. Strip resource forks and sync:
   ```bash
   dot_clean -m EFI/OC/Kexts/RTL8814Native.kext
   dot_clean -m EFI/OC/Kexts/RTL8814AirLayer.kext
   sync
   ```
3. In `config.plist`, under `Kernel -> Add`, add both kexts with `Enabled` set, and put `RTL8814AirLayer.kext` after `RTL8814Native.kext`.
4. Add `rtlair=2` to the boot-args. Without it only the driver loads and you get the plain interface.
5. Reboot and plug the adapter into a USB port on the motherboard (I would avoid hubs). Join your network from the Wi-Fi menu; macOS asks for the password the first time.

If something goes wrong, remove both kexts and `rtlair=2` from the EFI.

`Tools/phase5_deploy.sh` does these steps for me, including a backup of the EFI, but it is written for my setup.

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

## How it fits together

```
 airportd, Wi-Fi menu, System Settings
              |  IO80211 / Skywalk
 RTL8814AirLayer.kext   native interface, scan, join, link quality
              |  RTLRadioABI (internal)
 RTL8814Native.kext     compat layer, Realtek HAL/PHY, USB pipes,
                        firmware loader, WPA2/WPA3 supplicant
```

The adapter has one bulk IN, three bulk OUT and one interrupt endpoint. Receive runs several transfers in flight on its own worker thread; transmit is queued asynchronously with hardware aggregation.

## Diagnostics

`Tools/test_uc` talks to the driver through a user client:

```bash
clang -Wall -O2 Tools/test_uc.c -framework IOKit -framework CoreFoundation -o Tools/test_uc
./Tools/test_uc status      # link, RSSI, rates, counters
./Tools/test_uc joinlog     # timeline of the last joins and leaves
./Tools/test_uc peek 0x0290 # read a MAC register
```

## Docs

- [Docs/ROADMAP.md](Docs/ROADMAP.md): what is done, what is open, and why
- [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md): hardware notes, USB rules, and a dated log of bugs and fixes
- [Docs/PHASE5_IO80211.md](Docs/PHASE5_IO80211.md): notes on Apple's IO80211Family and Skywalk, including vtable layouts
- [Docs/SAE_HANDOFF.md](Docs/SAE_HANDOFF.md): WPA3-SAE and 802.11w: how it was built, the live results and what is still to verify

## License

GPL-2.0, see [LICENSE](LICENSE). The Realtek code comes from `morrownr/8814au`. The IOKit glue and the compatibility layer are Copyright (c) 2026 DrogaBox.
