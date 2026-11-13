# AirDrop without Bluetooth (macOS 26.7.1, 25G241)

Question: with a Bluetooth controller absent or off, can this Mac receive
files from an iPhone (receiver in "Everyone" 10 minutes) and can it discover the iPhone?
Static analysis of `/usr/libexec/sharingd`, `Sharing.framework` and
`WirelessProximity.framework` from the x86_64h dyld shared cache of 25G241. sharingd
VAs are absolute (base 0x100000000); those of Sharing and WirelessProximity are the
shared cache VAs. Unverified items are marked `?`.

## 1. Hardware state on this machine

`system_profiler SPBluetoothDataType`: `Address: NULL`, `State: Off`, `Chipset:
BCM_4350C2`, `Firmware Version: v0 c0`. There is no operative BLE radio.

## 2. Readiness check (receiver)

The arbiter is `-[SDStatusMonitor isAirDropReady]` (`0x10014BA09`). It returns the
conjunction:

```
enableAirDropReceiving && bluetoothEnabledIncludingRestricted && wirelessEnabled
&& !wirelessCarPlay && !isClarityBoardEnabled && !isMulticastAdvertisementsDisabled
```

If it fails, it logs "AirDrop not ready: wifi=%d, bluetooth=%d, carplay=%d,
receivingEnabled=%d, isVirtualMachine=%d isMulticastAdvertisementsDisabled=%d".

- `-[SDStatusMonitor enableAirDropReceiving]` (`0x100153AC4`): defaults
  `EnableAirDropReceiving` (true by default except with an audio accessory) and
  `isAirDropAvailable` (`SFDeviceSupportsAirDrop`).
- `-[SDStatusMonitor isMulticastAdvertisementsDisabled]` (`0x1001514E3`): defaults
  `NoMulticastAdvertisements`.
- `-[SDStatusMonitor bluetoothEnabledIncludingRestricted]` (`0x100151082`): if
  `SFDeviceIsVirtualMachine()` returns true, always accepts. If the
  `CBController` exists, requires `bluetoothState == 5` (poweredOn) or `== 10`
  (private state "restricted but allowed" `?`); any other value
  (unsupported, poweredOff, unauthorized) is rejected. Without `CBController`, it falls to
  `SFIsBluetoothEnabled` from Sharing (`0x7ff818EC06AF`), which requires the same state 5
  and returns false on error. With the phantom controller (Address NULL, firmware v0) the
  state cannot be poweredOn, so this term is false and
  `isAirDropReady` returns false.

Consequence in the UI: `-[SDStatusMonitor newDiscoverableMode]` (`0x10014BC3B`)
returns `Disabled` if AirDrop is not allowed and **`Off` if `isAirDropReady` is
false**; with Contacts Only it requires an Apple account (`appleIDAccountState == 1`) and
no education mode ("Discoverable mode off: appleIDAccountState=%@ eduModeEnabled=%d").
The available-modes selector (`sub_1002FC870`, Swift, available for
Control Panel/Settings) reduces the list to `[off]` when `isAirDropReady` is false:
**without Bluetooth the UI can only set "Nobody"**; it is not that Finder grays it out, it is
that the Contacts/Everyone options stop being available.

The saved mode is read in `-[SDStatusMonitor reconciledDiscoverableMode]`
(`0x10014BE74`): `NSUserDefaults objectForKey:` with key
`kSFOperationDiscoverableModeKey` (`"DiscoverableMode"`, Sharing constant at
`0x7ff842553018`); if there is no value, `-[SDStatusMonitor defaultDiscoverableMode]`
(`0x10014B93C`) returns Contacts Only. Logs "Reconciled discoverable mode:
%@ (%@)". The constant values are "Disabled", "Contacts Only", "Everyone"
(strings in Sharing; the exact value saved for off is "Disabled" `?`).

## 3. Reception server: when `_airdrop._tcp` is published

`-[SDAirDropServer updateServerState]` (`0x10018927C`) governs the server
(receiver). First `thisIsTheFinder` (in Finder the flow goes through
`SDAirDropInformation`). It requires: console user present
(`currentConsoleUser`), machine unlocked at least once (`deviceWasUnlockedOnce`),
screen on (`_screenOn`) and discoverable mode other than Off
(`discoverableByContactsOnly || discoverableByEveryone`). Without that, it logs
"Scanning mode off: console=%d, discoverable=%d, screenOn=%d, unlockedOnce=%d" and
calls `stop`.

With the gate met, two paths:

1. **Automatic trigger** if `-[SDStatusMonitor showMeInWormhole]`
   (`0x100153DF4`, defaults `ShowMeInWormhole`, false by default) and
   `-[SDServerBrowser isAirDropEnabled]` (`0x10013A7A3`, true if there is any
   active AirDrop browse session, e.g. the Finder AirDrop window) or if it is a
   virtual machine, or `enableDemoMode`. Logs "Auto-trigger server" and calls
   `startServer` **without waiting for Bluetooth**.
2. **Normal path**: `-[SDAirDropServer startProximityScanner]` (`0x100189F43`)
   and logs "Scanning mode Contacts Only/Everyone". The scanner:
   - `-[SDAirDropBLEController startScanningWithIdentifier:deviceUpdatesHandler:]`
     (`0x100381550`, Swift) — the daemon's own BLE scanning;
   - creates `WPAWDL` (`initWithDelegate:queue:`, WirelessProximity) and, when its
     `state == 3` (`-[SDAirDropServer awdlDidUpdateState:]` `0x100189127`, logs
     "BTLE scanner %@"), calls `startConnectionlessAWDLServiceScanning`.

Detection of a sender arrives via `-[SDAirDropServer awdl:foundDevice:rssi:]`
(`0x1001890A5`) → trims 8 bytes (4 short hashes of 2 bytes: `airdropHash1..4`,
`bleRSSI`) → `-[SDAirDropServer handleFoundHashes:rssi:]` (`0x100188DCA`):

- All: "In Everyone mode, start server";
- Contacts Only: `-[SDStatusMonitor contactsContainsShortHashes:]` compares against
  the address book; "Hashes %@ exist in contacts, start server" or "Hashes %@ not
  recognized".

Then `-[SDAirDropServer stopScanningAndStartServer]` (`0x100188D82`) →
`startTimers` → `-[SDAirDropServer startServer]` (`0x10018A4AC`) →
`-[SDAirDropServer start]` (`0x10018A5A6`, "AirDrop server started") →
`-[SDAirDropServer startHTTPServer:]` (`0x100189BC5`, `_CFHTTPServer` server with
SSL, "AirDrop server scheduled on port %@"; serves `/Discover`, `/Ask`, `/Upload`).

The Bonjour publication runs in the Swift state machine `sub_1003E0020`
("AirDrop Bonjour server setting up / ready / waiting with error %@ / stopped /
failed"), which launches the startup ("Starting AirDrop Bonjour Server %s - legacy
%{bool}d" in `sub_1003DE270`; restart for legacy peers "Restarting AirDrop
Bonjour Server for legacy peer %s" in `sub_1003E1660`) and waits until AWDL is
up ("AWDL not yet available for serviceName %s" in `sub_1003F1150` and
`sub_1003F1A40`). The actual publication is `-[SDAirDropPublisher publish]`
(`0x100073840`): `CFNetServiceCreate(kCFAllocatorDefault, "local", <type>, <name>,
<port>)` with type `_airdrop._tcp.` or `_airdrop-alt._tcp.` (string at
`0x10064B515`/`0x10064B528`, chosen by `sub_1001A7872`) and
`CFNetServiceRegisterWithOptions` with options `0x100801`
(kDNSServiceFlagsAutoTrigger | kDNSServiceFlagsForce | **kDNSServiceFlagsIncludeAWDL**
`0x100000`) plus `0x200000` when not registering on all interfaces
(`registerAllInterfaces`; exact semantics of bit 0x200000 in registration `?`).
`-[SDAirDropPublisher publishCallBack:]` (`0x1000731F4`) logs "Bonjour
advertise %@ started", retries ("Retrying Bonjour advertise in 3 seconds...") and
"Bonjour advertise failed (%ld|%d)". Two timers shut the server down:
`awdlTimeoutFired:` (`0x100188462`) and `bluetoothTimeoutFired:` (`0x1001884E9`),
both log "AirDrop server idle timeout" and call `stop` + `updateServerState`.

**Answer: the publication of `_airdrop._tcp` on awdl0 is indeed conditioned on the BLE
trigger** (the short hashes of the sender's advertisement) in the normal path.
Without Bluetooth the scanner cannot start (`awdl:failedToStartScanningWithError:`
`0x10018921A`) and the server does not start.

The "Auto-trigger server" (`ShowMeInWormhole` with an active browse, or
`EnableDemoMode`) **does not save it either**: it is inside the `updateServerState` gate,
which requires `discoverableByContactsOnly || discoverableByEveryone`. Those methods
(`0x100188D62`, `0x100188D42`) compare the server's `_discoverableMode`, which comes
from `-[SDStatusMonitor updateDiscoverableMode]` (`0x10014C2AF`) →
`newDiscoverableMode`, and the latter returns `Off` when `isAirDropReady` is false (§2).
Without Bluetooth the gate fails with "Scanning mode off: … discoverable=0 …" before
looking at `ShowMeInWormhole`. The only path in the code that skips the missing Bluetooth
is `SFDeviceIsVirtualMachine()` true: it accepts Bluetooth in
`bluetoothEnabledIncludingRestricted` and additionally triggers the auto-server. The
legacy `DisableAWDL` mode (§5) changes the interface, but not the Bluetooth gate.

## 4. Sender side: discovery of receivers

The AirDrop window or the share sheet opens a browse session:
`-[SDServerBrowser startAirDropBrowsing:bundleID:sessionID:helperConnection:delayBonjour:]`
(`0x100139E45`) → `-[SDServerBrowser startAirDropBrowsing:connection:sessionID:]`
(`0x10013A165`) → `-[SDBonjourBrowser start]` (`0x10004FB30`, "SDBonjourBrowser:
start requested") → `-[SDBonjourBrowser updateBrowserState]` (`0x10004F497`).

- Interface: `-[SDBonjourBrowser getInterface:]` (`0x10004EE3B`) returns the index
  of `awdl0` computed by `sub_100123A50` (`getifaddrs`, first IPv6 interface
  `sa_family == 30` whose name case-insensitively is `awdl0`, `if_nametoindex`),
  or `0` (all) if `browseAllInterfaces`. Without `awdl0` it logs "Bonjour browser
  failed to find awdl0 interface" (and "Bonjour resolver failed to find awdl0
  interface" in the resolver, `sub_100608341`).
- Scanning: `DNSServiceBrowse` over the shared connection
  (`DNSServiceCreateConnection`) with options `kDNSServiceFlagsShareConnection`
  (`0x4000`) | `kDNSServiceFlagsThresholdOne` (`0x2000000`, mode 1) or
  `ThresholdFinder` (`0x4000000`, mode 2) | **`kDNSServiceFlagsIncludeAWDL`**
  (`0x100000`, for AirDrop except `enableDemoMode`). Logs "AirDrop bonjour
  browser setting up / ready / waiting with error %@ / failed with error %@ /
  cancelled" (`sub_1003F7510`) and "Bonjour discovered %@ PTR/TXT record over %@ in
  %d ms".
- Parallel BLE advertising: `-[SDBonjourBrowser advertiseHashes]` (`0x10004F27A`)
  requires `WPAWDL.state == 3` and `-[SDStatusMonitor enableAirDropAdvertising]`
  (`0x100153A86`, which negates the `DisableAirDropAdvertising` default), takes
  `-[SDStatusMonitor myShortHashesForAirDrop]` (`0x100152B30`) and calls
  `startConnectionlessAdvertisingWithData:` (`0x10004F16D`); "BTLE advertising
  hashes %@" or "No email or phone number hashes to advertise". The advertiser state
  arrives via `awdlDidUpdateState:` (`0x10004F00D`, "BTLE advertiser %@").

**Answer: the browse does not wait for a BLE response**: `updateBrowserState` starts the
`DNSServiceBrowse` on awdl0 with no further conditions (`shouldStart` = `startCalled` and
console user). The BLE advertising failure (`awdl:failedToStartAdvertisingWithError:`
`0x10004F10B`) is only logged ("SDBonjourBrowser:
failedToStartAdvertisingWithError %@") and the browse continues. The BLE
advertisement of the sender exists to wake the iOS receiver: that dependency belongs to iOS and cannot
be verified here `?` — if the iPhone is already in its 10-minute "Everyone" window,
its receiver is awake beforehand, but if iOS requires a BLE advertisement from the sender
to bring AWDL up, a sending Mac without Bluetooth would not wake it `?`.

The real BLE layer is in WirelessProximity: `WPAWDL`
(`-[WPAWDL startConnectionlessAWDLServiceAdvertisingWithData:]` `0x7ff905E9DD69`
builds the advertisement body, length < 0x17 = 23 bytes, typically 18 bytes,
the Apple message body 0x004C type 0x05: 8 zero bytes, version, four 2-byte hashes and one
zero byte, per the articles of [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.2;
`+[WPAWDL hashEmail:]` `0x7ff905E9DA52` and `+[WPAWDL
generateDataFromEmails:]` `0x7ff905E9DB64` generate the email/phone hashes)
talks over XPC with `com.apple.wirelessproxd` (strings of the framework itself;
also `com.apple.wiproxd.btpairing.connect`). Without a radio, the requests fail and
`advertisingFailedToStart` (`0x7ff905E9E606`) / `scanningFailedToStart`
(`0x7ff905E9E7E7`) arrive instead of the `foundDevice` events.

## 5. Defaults keys and debug modes

Common reader in sharingd: `sub_1001A768E` (`0x1001A768E`) =
`CFPreferencesCopyAppValue` with domain **`com.apple.Sharing`** and, if missing,
**`com.apple.NetworkBrowser`**; the boolean reader is `sub_1001A7589`
(`0x1001A7589`). Observed keys:

| Key | Default | Effect | Function |
|---|---|---|---|
| `EnableAirDropReceiving` | true | receiving enabled | `0x100153AC4` |
| `NoMulticastAdvertisements` | false | disables `isAirDropReady` | `0x1001514E3` |
| `ShowMeInWormhole` | false | "Auto-trigger server" with active browse | `0x100153DF4` |
| `DisableAWDL` | false | legacy mode: uses `en0` instead of `awdl0` and drops IncludeAWDL | `sub_100123014` `0x100123014` |
| `DisableAirDropAdvertising` | false | sender BLE advertisement off | `0x100153A86` |
| `BrowseAllInterfaces` | `EnableDemoMode` or VM | browse/registration on all interfaces (index 0) | `sub_1001A7514` `0x1001A7514` |
| `EnableDemoMode` | false | demo: drops IncludeAWDL, wider browse/registration, auto-server | flags and `updateServerState` |
| `DiscoverableMode` | (absent → Contacts Only) | "Disabled"/"Contacts Only"/"Everyone" in the daemon domain `?` | `0x10014BE74` |

`DisableAWDL` is the switch of the "over the normal network" AirDrop: with it,
`-[SDBonjourBrowser getInterface:]` and `-[SDAirDropPublisher publish]` and
`-[SDAirDropClient createSession]` stop using `awdl0` (the index becomes `en0`,
`sub_100123A50`) and the registration loses `IncludeAWDL`; discovery becomes
Bonjour multicast over the infrastructure Wi-Fi.

Internal Sharing feature flags (table registered by `sub_7FF819102BB0` at
`0x7ff819102BB0`, queried via `+[SFFeatureFlagAdaptor
featureEnabledForDomain:feature:]` `0x7ff818F4CFC5`, which delegates to the
FeatureFlags framework `?`): `DisableAirDrop`, `DisableTLS`, `DisableAirDropAdvertising`,
`DisableAutoAccept`, `EnableDemoMode`, `BrowseAllInterfaces`,
`RegisterAllInterfaces`, `EveryoneModeBanner`, `AirDropPrivacyImprovements`,
`AirDropWalkAway`, `AirDropSystemLevelProgress`, `AirDropMoveToApp`,
`AskToAirDrop`, `AirDropContactShare`, `BoopToAction`, `BoopToAirDrop`,
`BoopToCollaborate`, `BoopShockwave`, `BoopShockwaveVariant`,
`BoopInteractionTypeOverride`, `ShockwaveHapticsPlayerStartsEarly`,
`NearFieldController`, `AirDropFinalALPN`, `PINPairingAirDrop`,
`ShareSheetSolarium`, `AirDropPrivateContactDiscovery`,
`A2ARangingAssumeClose`, `A2ARangingAssumeFar`, `QUICIdleTimeout`,
`VerboseWalkAway`, `NearFieldSharingEnabled`, `CellularUsageEnabled`,
`AirDropSensitivePreviewOverride`, `AirDropSensitiveContentOverride`,
`AirDropShowTransferIdentifierUI`, `AlwaysAutoAccept`,
`OverrideReceivedFilesPathiOS`. With `DisableAirDrop` active, AirDrop is
disabled entirely (state `Disabled` in `newDiscoverableMode` via
`isAirDropAllowed`).

## 6. Logging: strings for `log show`

Subsystem: `com.apple.airdrop` (`com.apple.airdrop_pro` also appears `?`).
String per decision:

| Decision | String | Function |
|---|---|---|
| Not ready (no BT) | "AirDrop not ready: wifi=%d, bluetooth=%d, carplay=%d, receivingEnabled=%d, isVirtualMachine=%d isMulticastAdvertisementsDisabled=%d" | `isAirDropReady` `0x10014BA09` |
| Mode collapsed to Off | "Discoverable mode off: appleIDAccountState=%@ eduModeEnabled=%d" | `newDiscoverableMode` `0x10014BC3B` |
| Mode read | "Reconciled discoverable mode: %@ (%@)" | `0x10014BE74` |
| Mode changed | "Discoverable mode changed to %@" | `0x100188AD9` |
| Server gate | "Scanning mode off: console=%d, discoverable=%d, screenOn=%d, unlockedOnce=%d" | `updateServerState` `0x10018927C` |
| Scanner path | "Scanning mode Contacts Only" / "Scanning mode Everyone" | `updateServerState` |
| Auto-server | "Auto-trigger server" | `updateServerState` |
| Finder | "Finder entered AirDrop" / "Finder exited AirDrop" | `finderAirDropEnabled:` `0x100188A00` |
| BLE scanner | "BTLE scanner %@" (states 0–3) | `0x100189127` |
| BLE advertiser | "BTLE advertiser %@" | `0x10004F00D` |
| Hashes received | "In Everyone mode, start server" / "Hashes %@ exist in contacts, start server" / "Hashes %@ not recognized" | `handleFoundHashes:rssi:` `0x100188DCA` |
| No own hashes | "No email or phone number hashes to advertise" | `advertiseHashes` `0x10004F27A` |
| Sender BLE failure | "SDBonjourBrowser: failedToStartAdvertisingWithError %@" | `0x10004F10B` |
| Server up | "AirDrop server started" / "AirDrop server scheduled on port %@" | `0x10018A5A6` / `0x100189BC5` |
| Publication | "Starting AirDrop Bonjour Server %s - legacy %{bool}d" / "Bonjour advertise %@" / "Bonjour advertise %@ started" / "Retrying Bonjour advertise in 3 seconds..." / "Bonjour advertise failed (%ld|%d)" | `sub_1003DE270`, `publish` `0x100073840`, `publishCallBack:` `0x1000731F4` |
| Publication state | "AirDrop Bonjour server setting up / ready / waiting with error %@ / stopped / failed with error %@" | `sub_1003E0020` |
| AWDL pending | "AWDL not yet available for serviceName %s" | `sub_1003F1150`, `sub_1003F1A40` |
| No awdl0 | "Bonjour browser failed to find awdl0 interface" | `getInterface:` `0x10004EE3B` |
| Browse | "SDBonjourBrowser: start requested" / "AirDrop bonjour browser ready" / "Bonjour discovered %@ PTR over %@ in %d ms" | `0x10004FB30`, `sub_1003F7510` |
| Shutdown | "AirDrop server idle timeout" / "AirDrop Bonjour server stopped" | `0x100188462`, `0x1001884E9`, `sub_1003E0020` |

Query suggestions:

```
log show --last 30m --predicate 'subsystem == "com.apple.airdrop"' --info
log show --last 30m --predicate 'process == "sharingd"' --info
```

## 7. Answers to the questions

1. **Does it refuse AirDrop without Bluetooth?** It does not disable it entirely: the
   scanning keeps existing and the browse (sender role) works. What collapses is the
   own discoverability: `isAirDropReady` false (term
   `bluetoothEnabledIncludingRestricted`) forces `newDiscoverableMode` to `Off`
   and the UI only offers "Nobody". Every attempt logs "AirDrop not ready: ...".
2. **Does it publish `_airdrop._tcp` on awdl0?** No. The publication requires
   `stopScanningAndStartServer`, which only arrives after a `handleFoundHashes:rssi:`
   (BLE), and the auto-server (`ShowMeInWormhole`, `EnableDemoMode`) sits behind
   the same discoverable-mode gate, which without Bluetooth is `Off` (§3). Only
   `SFDeviceIsVirtualMachine()` true opens both doors.
3. **Does the sender need a BLE response before browsing AWDL?** No: the browse
   (`DNSServiceBrowse` with `IncludeAWDL` over the `awdl0` index) starts without
   waiting for BLE; the failed BLE advertisement only leaves a log. The BLE
   restriction is on the receiver, not on the sender.
4. **Can the iPhone discover this Mac?** Not without Bluetooth: the reception
   server never gets published (§3), not even with `ShowMeInWormhole`.
5. **Can this Mac discover the iPhone?** The browse over awdl0 is viable if
   `awdl0` exists with IPv6; the iPhone in "Everyone" 10 min has its receiver
   awake. The doubt is whether iOS requires the sender's BLE advertisement to keep
   AWDL up during the transfer `?`.

## 8. Practical test with an iPhone visit

Requirements:

1. `awdl0` created and with an IPv6 link-local address (stage 3 of the project: already
   verified on hardware); `en2` associated and AWDL serving windows.
2. Receiving enabled: defaults `EnableAirDropReceiving` true (by
   default) and `NoMulticastAdvertisements` absent or false.
3. **Operational Bluetooth LE on the Mac.** Without it `isAirDropReady` is false, the
   discoverable mode stays `Off` even if "Everyone" is written into the defaults, and neither
   the BLE scanner nor the auto-server start (§3). It is a prerequisite of the
   test, at the same level as `awdl0`.
4. "Everyone" mode on the Mac (with Bluetooth the UI already offers it).
5. Screen on, session unlocked, console user present.
6. The iPhone sends from the share sheet: its BLE advertisement wakes the
   Mac's scanner ("In Everyone mode, start server") and the Mac publishes on awdl0.
   In the reverse direction, the Mac browses awdl0 without BLE, but the iOS receiver likely
   waits for the same sender BLE trigger `?`.

What to look for in the logs:

- "AirDrop not ready: ... bluetooth=0" — confirms the Bluetooth gate.
- "Scanning mode Everyone" or "Auto-trigger server" — which path
  `updateServerState` took.
- "AirDrop server started", "AirDrop server scheduled on port ..." — HTTP
  server up.
- "Starting AirDrop Bonjour Server ... - legacy 0" and "Bonjour advertise ..." —
  publication; "AirDrop Bonjour server waiting with error ..." would indicate AWDL
  down or missing interface.
- "Bonjour browser failed to find awdl0 interface" — if `awdl0` does not exist or has
  no IPv6 when opening the AirDrop window.
- On the sender side, "BTLE advertiser ..." and
  "SDBonjourBrowser: failedToStartAdvertisingWithError ..." — expected failure of the
  BLE advertisement; the browse must continue regardless.

## 9. Pending `?`

- Exact value saved for the Off mode in `DiscoverableMode` ("Disabled" vs
  "Off") and the daemon's exact domain (`com.apple.sharingd`): verify on a
  machine with Bluetooth or with `defaults read`.
- Meaning of `CBController` state 10 ("restricted") in
  `bluetoothEnabledIncludingRestricted`.
- Resolution mechanism of the feature flags (FeatureFlags framework; the
  `SFFeatureFlagAdaptor` thunk points to an import outside the carved
  sections).
- Whether iOS requires the sender's BLE advertisement to bring up/keep AWDL during a
  transfer (not verifiable without a reference iOS capture; the AWDLLab
  captures record the sender's Apple 0x004C type 0x05 advertisements, consistent with the
  wake-up role `?`).
- Exact length and layout of the payload of `WPAWDL
  startConnectionlessAWDLServiceAdvertisingWithData:` (18 bytes built at
  `0x7ff905E9DD69`; compatibility with the 0x05 format assumed from the length
  and the hashes, not compared byte by byte with the captures).
