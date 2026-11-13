# AWDL on iOS 27.2: the host side on an iPhone 11

Static reading of the IPSW `iPhone12,1_27.2_24B119_Restore.ipsw` (iPhone 11, board `n104`), done on 2026-11-08 with IDA 9.4
without running anything. It tells which part of what was read on macOS 26.7.1 also holds for the test iPhone. Addresses
are those of the extracted copies described in §6.

## 1. Where Wi-Fi lives

- **The kernelcache does not carry it.** `kernelcache.release.iphone12b` has 243 kexts and none is `IO80211Family` or a
  Broadcom driver. Skywalk, PCIe and Bluetooth are there, and of AWDL only four networking strings (`awdl_rtobase`, among
  others).
- **The driver is a dext.** `/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext` (5.5 MB) talks to the
  chip, the BCM4378, and carries its firmware in `Firmware/C-4378__s-A0` to `C-4378__s-B1` (`moana.trx`, `.clmb`, `.txcb`).
  For AWDL it is mostly iovars to the firmware: `awdl_sync_params`, `awdl_peer_op`, `awdl_election_tree`, `awdl_aftxmode`,
  `awdl_min_rate`, `awdl_stats`, the last with counters such as `lostmaster` and `misalign`.
- **The peer manager is in `IO80211DriverKit`.** The dext links
  `/System/DriverKit/System/Library/PrivateFrameworks/IO80211DriverKit.framework`, which only exists inside the DriverKit
  dyld cache, in the SystemOS cryptex. That library carries `IO80211AWDLPeerManager` with C++ symbols and the source path
  `IO80211_driverkit/IO80211Family/AWDL/IO80211AWDLPeerManager.cpp`.
- **`wifip2pd`** is in `/usr/libexec`, on the system image, and depends on `WiFiPeerToPeer`, `CoreWiFi` and `IO80211`, which
  are in the main dyld cache.

## 2. It is the same manager as on macOS

`IO80211AWDLPeerManager::monitorAWDLDataMode` (`0x180b92734`) does the same as the 25G241 version described in
[AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md):

- It reads `getUnicastPacketsOutputCount` and `getUnicastPacketsInputCount` and subtracts the previous pass, kept in
  `ivars+16312` and `ivars+16308`. If a counter went down it logs `current Tx count (%u) is less than last count` and takes
  the new value as the difference.
- If there was TX or there are cached peers (`getAwdlCachedPeerCount`), and TX plus RX reaches `ivars+16020`, or the AW's
  stored sum reaches `ivars+16028`, it calls `setDataMode(6, 0)` and sets `ivars+16032` to 16.
- With no traffic for `16 × byte(ivars+16992) × (ivars+16032 + ivars+16024)` ms it goes to `setScheduleState(3, 6, 0)`.

`initParamsToDefaults` sets `ivars+16020` to 8, `ivars+16024` to 8, `ivars+16028` to 4 and `ivars+16032` to 0 (`0x180b79680`
to `0x180b7969c`). These are the same values as on 25G241, where the fields sit at `ivars+16060`, `16064`, `16068` and
`16072`, 40 bytes higher. The iPhone's data mode thresholds are the same ones the layer already feeds.

The iOS condition also accepts cached peers without own TX (`v12 != 0 || getAwdlCachedPeerCount() != 0`). Whether the macOS
version has the same alternative was not checked `?`.

## 3. When the iPhone sends a UMI

`IO80211AWDLPeerManager::handleUmiTimer` (`0x180b73bd4`) walks the peers waiting for a data path:

- It only acts inside an AW (`ivars+772` bit 0). Otherwise it logs `Not sending UMI as we are not in an AW`.
- It calls `setDataMode(16, 1)`.
- The peer must be on the current channel: it compares the peer's `seq2ChanInfo` with the channel in use (`isSameChannel`).
  The sequence the iPhone uses is the one the Mac announces in TLV 18.
- It does not repeat if the last UMI was 9 ms ago or less (`time since last UMI`).
- It does not send if the peer is not cached in the firmware (`Not sending UMI because peer ... is not cached`).
- It builds the options with `getOptionsForUMI` and `getExtOptionsForUMI` and calls `sendUnicastMI`.

For the test: with `en2` not associated, the Mac's TLV 18 puts 149 only in slots 2 and 10
([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17). The iPhone can only send its UMI to the Mac in the slots where both
sequences match.

## 4. What it does not answer

- **How much silence the iPhone tolerates.** Synchronisation and AW tracking are in the BCM4378 firmware, and the dext only
  passes iovars. The firmware was not analysed. At manager level the code is the same as on the Mac, where a peer that goes
  quiet stays in `peers` for 101 to 106 s ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §10). That does not tell when the
  iPhone's firmware loses sync `?`.
- **The value of `awdl_min_rate`.** `setMinRate` (`0x180bac7e4`) sends it to the firmware and stores it at `ivars+42385`.
  Who calls it and with what value was not checked `?`.
- **`wifip2pd`.** The text `Considering aging out peer ...` (`sub_1004D5390`) uses `NANPeerServiceIdentifier`: it is NAN,
  not AWDL.

## 5. What changes

- What was read of the macOS manager serves as a model of the iPhone: data mode, UMI, peer cache and states. The `ivars`
  offsets differ and must be taken from this copy.
- What is different is the firmware. A Broadcom Mac and an iPhone share the manager, but the T9UH has no AWDL firmware, and
  what the chip does on them the layer does here.

## 6. How it was obtained

Everything stays outside the repository, in `~/re_cache/ios/n104_24B119`.

1. The kernelcache is taken from the IPSW, its IM4P wrapper (DER, `bvx2` payload) removed, and decompressed with `lzfse`.
2. The system image (`141-08279-215.dmg.aea`) and the SystemOS cryptex (`141-10128-227.dmg.aea`) are AEA encrypted. The
   authentication data of each carries `com.apple.wkms.fcs-key-url` and `com.apple.wkms.fcs-response`. The URL, publicly
   reachable, returns a P-256 private key. With it, HPKE (P-256, HKDF-SHA256, AES-256-GCM, base mode) opens `wrapped-key`,
   and `aea decrypt -key-value base64:...` decrypts the image.
3. Both images are APFS and mount with `hdiutil attach -readonly -nobrowse`.
4. `IO80211DriverKit` is extracted from `System/DriverKit/System/Library/dyld/dyld_shared_cache_arm64e` with
   `dyld_shared_cache_extract_dylibs_progress` from `dsc_extractor.bundle` (Xcode, iPhoneOS platform).
5. The IDBs are `~/re_cache/ida/ios_io80211dk_24B119.i64` and `~/re_cache/ida/ios_wifip2pd_24B119.i64`.
