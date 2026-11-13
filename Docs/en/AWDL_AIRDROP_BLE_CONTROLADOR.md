# AirDrop with third-party Bluetooth controllers (macOS 26.7.1, 25G241)

Question: with a third-party USB Bluetooth controller —Broadcom BCM20702 with
BrcmPatchRAM, Intel AX200/AX210, MediaTek MT7925, CSR, Realtek RTL8761B, all of
them exposed to bluetoothd through BlueToolFixup—, is that enough for AirDrop
discovery, beyond the "Bluetooth on" requirement already
documented in `AWDL_AIRDROP_SIN_BLUETOOTH.md`?

Static analysis of `/usr/libexec/sharingd` and of the x86_64 slice of
`/usr/sbin/bluetoothd` (universal binary) from 25G241. The VAs are absolute,
base `0x100000000` in both binaries. Unverified items are marked `?`.

## 1. Short answer

Neither sharingd nor bluetoothd contains a manufacturer, HCI version or
"Continuity capability" check that denies LE scanning or advertising to a client
such as wirelessproxd. The verified gates are:

1. In sharingd, the controller state (`bluetoothState == 5` or `== 10`) as part
   of `isAirDropReady`; without it the discoverable mode collapses to `Off`
   (already documented). Nothing else: the scanner and the advertiser do not
   query the controller again.
2. In bluetoothd, standard LE capabilities: LE supported, LE states mask
   (active scan, advertising types) and the number of advertising instances
   available. Every BLE 4.0 controller passes the first two; the third one
   conditions multi-instance advertising, not scanning or single-instance
   advertising.

The Apple-specific mechanisms (vendor list for offload filtering, match
tables, AOP buffers, VSC per chipset family) degrade to "no offload" when the
controller does not answer them; they do not reject the service.
The practical consequence: any functional BLE 4.0+ controller should sustain
AirDrop discovery; the 5.x ones (Intel AX200/AX210, MT7925,
RTL8761B if its firmware implements extended advertising) additionally keep
multi-instance. For the BCM20702 the advantage is not guaranteed: the
board-id policy of Intel Macs is Broadcom, but a third-party dongle enters via
the 4000 transport, which frames VSCs in the SRS family (§3.3); which of the
two prevails was not traced `?`.

Measured correction (§8): a CSR8510 does not sustain LE scanning or advertising
with the stock configuration; it needs the `SupportsMultipleAdvertising`
preference set to false. The text above describes the static analysis, which
did not see that failure.

## 2. sharingd: from "Bluetooth on" to the scanner and the advertiser

Chain verified function by function (sharingd addresses):

1. `-[SDStatusMonitor isAirDropReady]` (`0x10014BA09`): already-documented
   conjunction; the Bluetooth term is
   `-[SDStatusMonitor bluetoothEnabledIncludingRestricted]` (`0x100151082`):
   `SFDeviceIsVirtualMachine()` true always accepts; with a controller it
   requires `bluetoothState == 5` (poweredOn) or `== 10` (restricted state `?`);
   without a controller it falls back to `SFIsBluetoothEnabled` (same state 5).
   `-[SDStatusMonitor bluetoothEnabled]` (`0x1001510F5`) requires 5 outright.
   The monitor is an instance of `CBAdvertiser` (private
   CoreBluetooth class, allocated in `-[SDStatusMonitor installBluetoothMonitor]`
   `0x100155B29`) observed by `-[SDStatusMonitor bluetoothStateChanged]`
   (`0x100155E5A`, table of state names 0–10). In sharingd there is no
   `CBCentralManager` nor `CBPeripheralManager` (only CoreBluetooth reference:
   `CBController`/`CBAdvertiser`); the AirDrop BLE scanning does not touch
   CoreBluetooth directly.
2. `-[SDStatusMonitor newDiscoverableMode]` (`0x10014BC3B`): `Off` if
   `isAirDropReady` is false (without Bluetooth the UI only offers "Nobody").
3. `-[SDAirDropServer updateServerState]` (`0x10018927C`) →
   `-[SDAirDropServer startProximityScanner]` (`0x100189F43`): the gates are
   console user, prior unlock, screen and discoverable mode; there is
   no additional Bluetooth query.
4. Scanner: `-[SDAirDropBLEController
   startScanningWithIdentifier:deviceUpdatesHandler:]` (`0x100381550`; init
   `0x10037F820` → `sub_10037F590`; startup `sub_100380CC0`): at startup
   there are no LE support checks, controller features,
   Continuity nor manufacturer checks `?` (Swift concurrency machine, reviewed
   without finding gates). The actual scan is ordered over `WPAWDL`
   (WirelessProximity):
   `-[SDAirDropServer awdlDidUpdateState:]` (`0x100189127`) only with
   `state == 3` calls `startConnectionlessAWDLServiceScanning`; failure arrives
   via `awdl:failedToStartScanningWithError:` (`0x10018921A`).
5. Advertiser (sender): `-[SDBonjourBrowser advertiseHashes]` (`0x10004F27A`)
   requires `WPAWDL.state == 3` and `enableAirDropAdvertising` (`0x100153A86`);
   advertising failure only leaves a log (`0x10004F10B`) and the browse goes on.
6. `SFBLEScanner`/`SFBLEAdvertiser`/`SFBLEClient`/`SFBLEConnection` (class
   references at `0x10076D258`–`0x10076D270`): their users in sharingd are
   `SDAutoUnlockSessionManager`, `SDNearbyAgent`, `SDSetupAgent` and
   `SDRemoteInteractionAgent`; no AirDrop path uses them.
7. `WPAWDL` talks over XPC with `com.apple.wirelessproxd`; state 3 is produced
   by that daemon, which is bluetoothd's Continuity client. Its internal gates
   are outside this analysis (binary in the shared cache, not carved here)
   `?`.

sharingd conclusion: between "Bluetooth poweredOn" and the scanner/advertiser
running only mediate the `CBAdvertiser` state (5/10) and the state 3 of
`WPAWDL`. There are no LE, vendor or Continuity checks.

## 3. bluetoothd: what carries LE scanning and advertising

### 3.1 Standard LE capabilities (the only denial gates)

- LE supported: if the controller does not declare LE, SMP registration fails
  (`sub_1002261C9`, `0x1002261C9`; "LE not supported, can't register SMP.",
  string `0x10094499C`, error 147). Without LE there is no BLE at all.
- LE states mask: the standard HCI command `LE_Read_Supported_States` is
  processed in `sub_1001728A1` (`0x1001728A1`,
  "initCmdReadLeSupportedStatesCB") which stores the mask in
  `qword_100B30EC8` via `sub_100216E28` (`0x100216E28`).
- Active scanning: `sub_1002182A0` (`0x1002182A0`) requires bit `0x20` of the
  mask; without it returns error 1330 ("Local device does not support active
  scanning.", string `0x100941077`, xref `0x1002182E6`). Passive scan has
  no gate of its own.
- Advertising types: `sub_100218870` (`0x100218870`, entry of
  `LE_StartAdvertising`) requires, according to the requested type, bits `0x1`
  (non-connectable), `0x2` (discoverable), `0x4` (connectable) and `0x8`
  (direct) of the same mask; without the bit it returns 1330 with "Device does
  not support … advertising." (strings `0x10094119B`, `0x1009411C7`,
  `0x1009411F8`, `0x10094122A`). All four states are mandatory in every
  BLE 4.0 controller, so these gates do not discriminate manufacturers.

### 3.2 Advertising instances (multi-adv)

- In `sub_100218870`: requesting `numOfInstances > 1` requires
  `sub_1001F7321()` (`0x1001F7321`, returns `byte_100B2FE20`) greater than 0 and
  a `numOfInstances` less than or equal; with counter 0 and `n != 1` it returns error
  101 with the log "numOfInstances can not be greater than 1, using regular
  advertising" (string `0x100941157`, xref `0x1002189FA`): despite the text, the
  code rejects the request, it does not fall back to a single instance `?` (no
  retry path with `n = 1` was found).
- `byte_100B2FE20` counts the instances registered in `byte_100B2FE22[]` by
  `sub_1001F7675` (`0x1001F7675`), with cap `byte_100B294D0`, set by
  `sub_1001F79D0` (`0x1001F79D0`) with the value from the chip policy object.
- The cap is computed by slot `+3976` of the policy object's vtable
  (`sub_100357A9C`, `0x100357A9C`) according to the family field `+820`: families
  < 2000 (Broadcom from Macs) 4–16 depending on family; families 5000–5999 and 6000: 8;
  family 2000–3999 (ACI): `4 * slot2328 + 4` or `slot2328 + 5`. With family 0
  (object absent) it returns 0 and there is no multi-instance.
- How the counter gets filled with a non-Broadcom controller was not traced
  end to end `?`: the Broadcom path uses the `_BCM_*MultipleAdvInstance` VSCs
  (implementations `sub_10019D64A` and onwards,
  table `0x100AADF50`); the standard "BT Spec advertising sets" path (extended
  advertising from LE 5.0) exists — `initCoreStack` (`sub_1005100C6`,
  `0x1005100C6`) logs "enableBTSpecAdvertisingSets=%d … maxAdvHandles=%d"
  and the extended startup logs "Stack supports %d advertising instances"
  (string `0x100A31460`, xref `0x1006A779A`, with substitute 1 if the counter is
  0) `?`.
- The advertising that serves wirelessproxd arrives via
  `-[CBStackBLEAdvertiserBTStack shouldAdvertiseForWPDaemon:advData:advInstanceType:]`
  (`0x1000359E1`, manufacturer data of up to 28 bytes) and uses separate
  single-instance sessions (connectable, non-connectable, contact, object
  discovery; ivars at `0x100B16718`–`0x100B16788`); the fifth
  multi-instance session belongs only to the SWUP/Master path
  (`-[CBStackBLEAdvertiserBTStack _updateMultiInstancesAdvertising]`
  `0x100037985`). What wirelessproxd exactly asks was not verified `?`.

### 3.3 Chipset family, VSCs and offloads

- bluetoothd picks the chip family by the Mac model, not by the HCI
  manufacturer id of the controller: `sub_100393F90` (`0x100393F90`) constructs the
  policy object (`qword_100B3ABB0`, constructor `sub_100339650`,
  `0x100339650`) with families 21/22/23/24 (board-id tables passed to
  `MGIsDeviceOneOfType`, with encrypted names; the models could not be listed
  `?`), 2000 (environment variable `BT_CHIP=ACI BT`), 5000/5001
  (boot-arg `bt_pcie_srs`), 3000 and 6000; the fallback is family 1. On this
  machine (board-id `Mac-27AD2F918AE68F61`, SMBIOS MacPro7,1) the family
  corresponds to one in the Mac range, that is 1–1999 `?`.
- The VSC dispatcher `sub_1001E348C` (`0x1001E348C`) splits by the chipset value
  set in `dword_100B1CCFC` (writer `sub_1001E3726`,
  `0x1001E3726`, admits 1–6000): 1–1999 → BCM family, 2000–3999 → ACI,
  4000–5999 → SRS, 6000 → ACI; any other value logs "Invalid chipset
  %d" (string `0x100937168`) and leaves the daemon without VSCs.
- The chipset arrives from two places: the policy object (`policy+820`, via
  `sub_100378C0A` from `initCoreStack`) or the transport: stack
  initialization (`sub_100158C5F`, `0x100158C5F`) explicitly handles the "Third
  Party USB Bluetooth Dongle" (transport 4000; getter `sub_10016057B`,
  selector `sub_1001605B8`) and if its transport fails it logs "Transport layer
  initialization failed for Third Party USB Bluetooth Dongle. Switch to
  internal Bluetooth module" (string `0x10091EA63`) and retries with the internal
  module. With a working third-party dongle, chipset 4000 frames it
  in the SRS VSC family.
- Consequence with a non-Broadcom controller under Broadcom family policy
  (Intel Macs): the Broadcom VSCs that bluetoothd sends for offloads get
  errors from the controller and each feature ends up disabled (the log block
  "BT chip support for … : %s / hw …",
  region `0x100410D65`–`0x1004121B2`); standard scanning and advertising go on.
  No path was found that denies the service because of failed VSCs.
- "LE Feature Vendor List" (`sub_100411E38`, `0x100411E38`; flag
  `byte_100B3B9A8`): hardware bit of slot `+3112` (`sub_10033C0C6`,
  false by default for families 22–24), defaults override for group `LE`
  key `FeatureVendorList` and log "LE Feature Vendor List is %s (hw %s)"
  (`0x1009CA370`). The override can only disable (with hardware 0 the flag
  stays 0). The flag and the `IncludeHiddenLEFeatureVendors` one
  (`sub_100412069`, `0x100412069`) have no consumers in code — only the
  reference from the capability-getter vtable (`0x100ABBEE0`) — that is,
  informational, with no effect on scan/adv.
- Scan filter by manufacturer (`LEManufacturerScanFilter`,
  `sub_100410D65`, `0x100410D65`): hardware support (slot `+2840`) AND feature
  flag `BluetoothFeatures/LEManufacturerScanFilter`; with both, the offload
  uses VSCs (`_BCM_LEMetaAddManufacturerScanFilter`, impl `sub_1001AE1C4`).
  Without hardware, filtering runs on the host; there is no denial.
- Filtering of non-Apple advertisements: `sub_10075F2DA` (`0x10075F2DA`) applies the
  "enhanced rules" (OUI/rules database) only when the payload's
  manufacturer differs from 76 = `0x4C` (Apple); Apple advertisements pass
  without rules.
- The proximity scanner (`-[CBStackBLEScannerBTStack _scanRequestCreate]`,
  `0x10005FB68`) registers the discovery types (2, 3, 5, 6, 7, 8, 9,
  10, 15, 16, 17, 18, 19, 21, 22, 24, 26, 27, among them the Apple
  Continuity and proximity ones) and adds offloads (AOP buffers from slot
  `+2744`, MiLo `+2760`, interval threshold `+2736`, targetCore of slot
  `+672`) only if the policy object declares support; without them the request
  is created all the same and the scan works without offload.

### 3.4 What is not there

- No comparison against `0x000F` (Broadcom) nor `0x004C` (Apple) that
  allows or denies scan/adv; the only verified comparisons with `0x4C`
  are the rules filter for non-Apple manufacturer data (§3.3). `?`
- No HCI version check that denies scan or adv `?` (no strings or paths
  with that purpose were found; the LE gate exists as a features bit,
  §3.1).

## 4. Evaluation per controller family

The data of each controller (HCI manufacturer id, Bluetooth version,
extended advertising support) come from public specification, not verified
against the binary nor against hardware `?`; the gates cited are those of §3.

| Gate (bluetoothd) | BCM20702 + BrcmPatchRAM | Intel AX200/AX210 | MT7925 | CSR8510 | RTL8761B |
|---|---|---|---|---|---|
| LE supported (SMP, §3.1) | yes (BT 4.0) `?` | yes (5.2/5.3) `?` | yes (5.4) `?` | yes (4.0) `?` | yes (5.0) `?` |
| Active scan, bit 0x20 (§3.1) | passes | passes | passes | passes | passes |
| Advertising types, bits 0x1–0x8 (§3.1) | pass | pass | pass | pass | pass |
| Single-instance advertising (Continuity sessions, §3.2) | yes | yes | yes | yes | yes |
| Multi-instance (§3.2) | probable: Broadcom VSCs present in the BrcmPatchRAM firmware, fit the BCM family of the policy `?` | probable: native extended advertising fills the counter `?` | probable: native extended advertising `?` | no: without extended adv or VSCs → counter 0 → error 101 if any client asks `n > 1` | firmware-dependent: extended adv `?` |
| Offload VSCs (§3.3) | BCM family = its native family `?` | fail (no functional effect) | fail (no functional effect) | fail (no functional effect) | fail (no functional effect) |
| "LE Feature Vendor List" / vendor filters (§3.3) | no effect (flag without consumers) | same | same | same | same |

Reading per family:

- **BCM20702 with BrcmPatchRAM** — it is the only family whose manufacturer
  matches the Broadcom policy that bluetoothd applies on Intel Macs; if that
  policy prevails over the 4000 transport of the dongle (§3.3, `?`) and the
  firmware loaded by BrcmPatchRAM implements the Broadcom multi-instance
  VSCs, it additionally keeps the offloads and multi-instance. Even without
  that it passes all the gates of §3.1 and the single-instance ones of §3.2,
  like the rest.
- **Intel AX200/AX210 and MT7925** — different manufacturer (`0x0002` Intel,
  MediaTek `?`); the Broadcom VSCs fail and the offloads are disabled, but
  scanning, single-instance advertising and —via the standard extended
  advertising of LE 5.x— presumably multi-instance remain operational `?`.
- **CSR8510** — BT 4.0 without extended advertising nor VSCs: active scan and
  single-instance advertising work; any multi-instance request would get the
  error 101 of §3.2. Measured: without the preference of §8 it neither scans
  nor advertises.
- **RTL8761B** — same as the 5.x ones if its firmware implements extended
  advertising; without it, falls to the CSR case `?`.

In all cases, the sharingd requirements (§2) are met the same: the
"poweredOn" state that BlueToolFixup exposes through bluetoothd is the only
thing the sharing daemon queries.

## 5. Full gate chain, in order

1. sharingd `isAirDropReady` (`0x10014BA09`) ← `bluetoothState` 5/10 of the
   `CBAdvertiser` (`0x100151082`).
2. sharingd `newDiscoverableMode` (`0x10014BC3B`) → `Off` if 1 fails.
3. sharingd `updateServerState` (`0x10018927C`) → `startProximityScanner`
   (`0x100189F43`), no additional Bluetooth gates.
4. sharingd/WirelessProximity: `WPAWDL.state == 3`
   (`awdlDidUpdateState:` `0x100189127` scanner; `advertiseHashes`
   `0x10004F27A` advertiser); the state is produced by wirelessproxd `?`.
5. bluetoothd: LE supported (`sub_1002261C9`), LE states (`sub_1001728A1` →
   `qword_100B30EC8`), active scan bit `0x20` (`sub_1002182A0`), advertising
   types bits `0x1`–`0x8` (`sub_100218870`).
6. bluetoothd: multi-instance (`sub_1001F7321` > 0 and `n` ≤ counter;
   cap `byte_100B294D0` = `sub_100357A9C` according to the board-id family).
7. bluetoothd: VSC family by chipset (`sub_1001E348C`), policy by
   board-id (`sub_100393F90`) or 4000 dongle transport
   (`sub_100158C5F`); failed VSCs degrade offloads, they do not deny
   the service.

## 6. Pending `?`

- What wirelessproxd exactly asks the advertiser (separate 1-instance
  sessions or multi-instance): requires carving wirelessproxd out of the shared
  cache; decides whether the multi-instance error 101 affects AirDrop/Continuity.
- Whether the "BT Spec advertising sets" path fills `byte_100B2FE20` on
  non-Broadcom 5.x controllers (incomplete trace between the extended startup
  and the handle registration).
- Families 21–24 ↔ concrete Mac models: the board-id tables are
  encrypted (16-byte hashes fed to `MGIsDeviceOneOfType`).
- Exact meaning of `CBAdvertiser` state 10 ("restricted").
- HCI manufacturer ids and extended advertising support of MT7925 and RTL8761B, and
  the inventory of Broadcom VSCs loaded by BrcmPatchRAM: data from
  public specification and community tools, not verified here.
- wirelessproxd internals: its own Continuity gates (if any) outside of
  bluetoothd. Closed for reception in §7.

## 7. wirelessproxd lives inside bluetoothd

In 25G241 there is no `wirelessproxd` binary: the `com.apple.wirelessproxd` Mach service is
published by `bluetoothd` (`/System/Library/LaunchDaemons/com.apple.bluetoothd.plist`,
`MachServices`). The startup of `bluetoothd` (`sub_100032940`) creates `CBDaemonServer` and, on top of
the same `CBStackAdaptorBTStack`, a `WPDaemonServer` (class imported from the private
WirelessProximity framework of the shared cache) which it calls `startListening` on.

The state that `sharingd` requires from `WPAWDL` (3, §2.4-5) comes out of
`+[WPDState getWPStateFromCBManagerState:]` (`0x7ff919fa2db4` in the image of the framework
extracted from the shared cache): an 11-entry table indexed by the CoreBluetooth state,
`{2, 1, 2, 2, 2, 3, 0, 0, 0, 0, 3}`. The 3 corresponds only to states 5 (poweredOn) and 10, the
same ones that `bluetoothEnabledIncludingRestricted` accepts in `sharingd`; out-of-range states give 0.
`-[WPDState coalesceState:Restricted:UpdateCache:]` (`0x7ff919fa2963`) takes the minimum among the
registered managers. No manufacturer or capability condition appears: state 3
is "Bluetooth on" as seen from WirelessProximity.

For the reception test (the iPhone sends, the Mac receives) the Mac only scans: the AirDrop
sender is the one who advertises over BLE and the receivers listen to it to bring AWDL up. The only
scan gate in `bluetoothd` is the active scan of §3.1, which every BLE 4.0 controller meets. The
multiple instances question (§3.2, error 101) only affects the Mac as sender; the
advertising entry of WPDaemon,
`-[CBStackBLEAdvertiserBTStack shouldAdvertiseForWPDaemon:advData:advInstanceType:]`
(`0x1000359E1`), filters by `advInstanceType` and manufacturer data of up to 28 bytes, but the
type AirDrop asks for was not traced `?`.

The measurement in §8 corrects this paragraph: the multi-instance startup also blocks scanning on
a controller with no advertising instances.

## 8. Measurement with a CSR8510 (TP-Link UB400)

Measured on 2026-11-05 on 25G241 with a TP-Link UB400 (CSR8510, USB `0a12:0001`, BT 4.0, HCI version
6) on a direct board port, BlueToolFixup 2.7.2 and chipset `THIRD_PARTY_DONGLE`. VAs are from the
x86_64 slice of `bluetoothd`.

### 8.1 Symptom

CoreBluetooth reports poweredOn and `sharingd` sees Bluetooth on, but a 25 s scan with a
`004C 05 12…` advertisement sent next to it receives 0 packets. The log shows
`stackDidStart -- status != OI_OK -- status = 0x0098`, `Reset scan in LE_Scan: STATUS 712 for location 3`
and `Stack is not started`.

The HCI capture shows `LE_SetScanParams` (`00 e001 3000 01 00`: passive, random own address) with
status 0 and `LE_SetScanEnable` (`01 01`) with status `0x0C`, Command Disallowed. `LE_SetRandomAddr`
never appears. The controller declares LE states `ffffff1f`, so this is not a combination it rejects
for lack of capability.

### 8.2 Chain

1. `sub_10069838E` (LeBroadcaster stackDidStart) calls
   `sub_10021990E(3 * (v7 ^ 1), 1, (*(a1 + 144) & ~2) == 0, sub_100698818)`. The third argument
   measured on this machine is 1.
2. With that argument, `sub_10021990E` calls `sub_1001F7352` (`LE_InitMultipleAdvertising`), which
   returns 152 at once: the controller has neither BT spec advertising sets nor a VSC table.
3. Because of the error, callback `sub_1002197D9` never runs. That callback calls `sub_1002198B9`,
   whose routine `sub_100219FB0` sets the address, and then reports that the stack started. So both
   `LE_SetRandomAddr` and the report are missing.
4. The scanner asks for a random own address and `LE_SetScanEnable` fails. Callback `sub_100224D98`
   passes the error to `sub_100224DE4` ("location 3").
5. `sub_100739FCE` only logs the error and leaves the state at Starting(2). `sub_1007266E0` then
   blocks every request ("Waiting for scan state to settle") until `bluetoothd` restarts.

With the third argument at 0, `sub_10021990E` goes straight to `sub_1002198B9` and schedules the
startup report with a timer (`sub_100219B96`).

### 8.3 Preference

`sub_100339650` (constructor of the policy object) sets byte `+160` to 1 for families 1–1999 except
9, 2000–3999 and 5000–6000, then overrides it if boolean key `SupportsMultipleAdvertising` exists in
section `LE`, logging `Warning: Overriding Multiple Advertising support - %s`. If the byte is 0,
`sub_100696E92` logs `Using a single advertising instance` and sets `+144` to 1, which gives 0 in the
third argument.

```bash
sudo defaults write com.apple.MobileBluetooth.debug LE -dict SupportsMultipleAdvertising -bool false; sudo pkill -x bluetoothd
```

To revert it:

```bash
sudo defaults delete com.apple.MobileBluetooth.debug LE; sudo pkill -x bluetoothd
```

There is no need to patch `bluetoothd` or change BlueToolFixup.

### 8.4 Result

| | Stock | With the preference |
|---|---|---|
| `stackDidStart` | `0x0098` | `OI_OK` |
| 712 errors | yes, on every start | 0 |
| Scanning | stays at Starting(2) | `Scanning started successfully` |
| Advertising | `Stack is not started` | `Started advertising successfully` |
| Packets in 25 s, with the advertisement next to it | 0 | 20, including the advertisement |

With the `004C 05 12…` advertisement and Everyone mode, `sharingd` logs `Found BLE device`,
`Device in everyone mode` and `AirDrop Bonjour server ready`.

The preference stays in effect after a reboot (one reboot, 2026-11-05): on that boot `bluetoothd` logs
`Overriding Multiple Advertising support` and `Using a single advertising instance` once,
`stackDidStart` gives `OI_OK` and there are no 712 errors. Before the advertisement nothing listens on TCP
8770, and with it the three `sharingd` lines, `AWDLAdvertise <id>._airdrop._tcp.local` and the port appear.

### 8.5 Not verified

- Whether `LE_SetRandomAddr` is what unblocks `LE_SetScanEnable` or the startup report plays a part:
  the preference changes both at once and the HCI capture was not repeated with it `?`.
- That the preference stays in effect after updating macOS.
- An AirDrop transfer with an iPhone: the one in [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §8.1 uses a
  custom sender and does not go over the air.
- The effect on other readers of byte `+160`: `sub_1005CE364` (XPC reply) and `sub_1005EF727` `?`.
- The behaviour with other controllers without multi-instance.

### 8.6 HCI capture without PacketLogger

At start `bluetoothd` reads dictionary `HCITraces` from the same domain and writes
`/var/root/Library/Logs/Bluetooth/bluetoothd-hci-latest.pklg`, buffered: the end of the file may only
appear when the process exits.

```bash
sudo defaults write com.apple.MobileBluetooth.debug HCITraces -dict HCIFileTraces -bool true; sudo pkill -x bluetoothd
```

To turn it off without losing the preference of §8.3:

```bash
sudo defaults delete com.apple.MobileBluetooth.debug HCITraces; sudo pkill -x bluetoothd
```
