# Phase 5 — Native IO80211 layer: RE findings (macOS 26.7.1 / Darwin 25.6.0)

Goal: present the RTL8814AU to macOS as a real Wi-Fi interface (airportd: Wi-Fi menu,
System Settings, Keychain, auto-join) instead of the current `IOEthernetController` (`en2`)
plus `test_uc join`.

## How the data was obtained (no reboot needed)
- `IO80211Family`, `IOSkywalkFamily`, `IONetworkingFamily` and Apple's Broadcom driver
  (`AppleBCMWLANCoreMac`) live in `/System/Library/KernelCollections/BootKernelExtensions.kc`
  (the `.kext` folders on disk only hold `Info.plist`). `WLANDriver` is in `SystemKernelExtensions.kc`.
- `Tools/re/kcsyms.py <kc> <bundle-id>` dumps one fileset entry's segments and symbols.
- `Tools/re/vt.py <Class>` decodes a vtable: KC pointers use chained fixups; the low 30 bits are
  the target's offset from the **lowest KC vmaddr, `0xffffff8000100000` (`__HIB`)**, not from
  `__TEXT` (`0x...200000`). Run it from a directory holding the `syms_*.txt` dumps.
- Dumps for this build: `Docs/re/io80211_25.6.0/` (`classes.txt`, `vt_*.txt`). IO80211Family is
  `1200.13.1`, SDK `25G221`. Re-dump after any macOS update: vtable slots move between builds.

## Class model (Tahoe)
- There is **no legacy `IO80211Interface`** any more: the data path is Skywalk only
  (`IO80211SkywalkInterface` -> `IO80211InfraInterface`, `IO80211VirtualInterface`).
- `IO80211Controller : IOEthernetController` (464 vtable slots). Our `RTLEthernetAdapter`
  already derives from `IOEthernetController`, so the controller side is a re-parent, not a rewrite.
- Apple's driver splits into `AppleBCMWLANCore : IO80211Controller` and
  `AppleBCMWLANInfraProtocol : IO80211InfraProtocol` (plus `AppleBCMWLANBusSkywalk`,
  `AppleBCMWLANJoinAdapter`, `AppleBCMWLANScanAdapter`, ...). We need the same two subclasses.

## What a driver must implement
`IO80211Controller` has 12 pure virtuals (names resolved from `AppleBCMWLANCore`'s vtable):

| slot | method |
|---|---|
| 394 | `isCommandProhibited(int)` |
| 403 | `handleCardSpecific(IO80211SkywalkInterface*, unsigned long, void*, bool)` (apple80211 ioctl dispatcher) |
| 408-409 | `getDRIVER_VERSION`, `getHARDWARE_VERSION` |
| 410 | `getCARD_CAPABILITIES` |
| 411-412 | `getPOWER`, `setPOWER` |
| 413-414 | `getCOUNTRY_CODE`, `setCOUNTRY_CODE` |
| 415 | `setGET_DEBUG_INFO` |
| 424 | `getLogger() const` |
| 432 | `getFaultReporterFromDriver()` |

`IO80211InfraProtocol` has **~200 pure virtuals**, one `get*/set*` per apple80211 request
(full list: `vt_IO80211InfraProtocol.txt` vs `vt_AppleBCMWLANInfraProtocol.txt`). Most can be
stubs returning `kIOReturnUnsupported`. The ones that matter for a working Wi-Fi menu:
- Scan / join / leave: `setWCL_SCAN_REQ`, `setWCL_SCAN_ABORT`, `setWCL_ASSOCIATE`,
  `setWCL_JOIN_ABORT`, `setWCL_LEAVE_NETWORK`, `setWCL_LINK_UP_DONE`.
- Keys: `setCIPHER_KEY` — airportd/eapolclient runs the WPA handshake in userspace and hands
  the keys to the driver, so our in-kext supplicant becomes a fallback only.
- State: `getCHANNEL`, `getRSSI`, `getRATE`, `getOP_MODE`, `getSUPPORTED_CHANNELS`,
  `getHW_SUPPORTED_CHANNELS`, `getCOUNTRY_CHANNELS`, `getHT_CAPABILITY`, `getVHT_CAPABILITY`,
  `getMCS`, `getMCS_VHT`, `getNSS`, `getHW_ADDR`, `getTXPOWER`, `getPOWERSAVE`,
  `getWCL_BSS_INFO`, `getWCL_EXTENDED_BSS_INFO`, `getWCL_TRAFFIC_COUNTERS`.
- "WCL" (Wi-Fi Control Layer) is the Tahoe-era path; the request/response structs
  (`apple80211_*`) are private and must be recovered from the callers in IO80211Family
  and from `AppleBCMWLANJoinAdapter` / `AppleBCMWLANScanAdapter`.

## Existing approaches (for context, not copied)
- itlwm: `IOEthernetController` + HeliPort app (what we have now). AirportItlwm used IO80211.
- Sonoma-era workaround: inject older `IO80211FamilyLegacy` + replacement `IOSkywalkFamily`
  (block the vanilla Skywalk). See khronokernel/IO80211-Patches.
- Tahoe Broadcom: AppleBCMWLANCompanion (needs VT-d; Dortania notes it is not usable on AMD).
- License: this layer must be written fresh (GPL-2.0-compatible).

## RE results: WCL request / event model (headless IDA 9.4, 2026-10-27)
Source: Hex-Rays output of `AppleBCMWLANCore`, its adapters and `WCLJoinRequest` /
`IO80211*` in BootKernelExtensions.kc (25.6.0). The IDB is saved, so later passes take seconds:
`Tools/re/ida/decomp2.py <boot.kc.i64> <outdir> <names.txt>` (a `XREF:` prefix lists callers),
`vcall.py` (finds virtual calls by vtable displacement), `msgids.py` (lists `postMessage` event IDs).
Offsets are for 25.6.0 only. They are measured, not taken from any header.

**No class-name gate.** The only `"AppleBCMWLANCore"` literal in IO80211 is a `metaCast` in
`IO80211VirtualInterface::peerToPeerConfigureIfnet`: it picks MTU 1500 vs 1484 for P2P
interfaces. Nothing rejects a third-party `IO80211Controller`.

### Requests (airportd → WCL → driver)
- **`setWCL_SCAN_REQ(apple80211ScanRequest*)`**:
  - Fields: +4 u8 randomise-MAC, +8 u32 home/away dwell, +84 u32 channel count, +88 channel
    array (12-byte entries; +4 inside each one is the channel number).
  - Return immediately and deliver results asynchronously (see the events table).
- **`setWCL_ASSOCIATE(apple80211AssocCandidates*)`**, filled by `WCLJoinRequest::fillAssocCandidatesList`:

  | Offset | Content |
  |---|---|
  | +4 | u32 version = 120 |
  | +12 | u16 mode (1 = IBSS) |
  | +16 / +20 / +24 | u32 lower auth / upper auth / upper auth ext |
  | +28 | u32 ssid_len |
  | +32 | ssid[32] |
  | +64 | embedded **`apple80211_key` (148 B)**; its cipher is at +72. For PSK this is where the PMK travels. |
  | +212 | u16 RSN IE len |
  | +214 | RSN IE[257] |
  | +471 | MLD addr |
  | +480 / +484 / +488 | flag words |
  | +532 | u32 candidate count |
  | +536 | candidates, 18 B each, max 10: +4 bssid[6], +16 u16 chanspec |
  | +716 | u16 extra-IE len |
  | +718 | IEs[257] |
  | +976 | band bitmap |

- **`setCIPHER_KEY(apple80211_key*)`**: +4 key_len, +8 cipher type, +12 u16 flags, +14 u16 index,
  +16 key[64], +80 rsc_len. It goes to `JoinAdapter::setKey`.
- **`setWCL_LEAVE_NETWORK`** → `leaveNetworkSync`.
- **`setWCL_JOIN_ABORT(u32*)`** → abort the firmware join.
- **Getters:**
  - `getRATE`: Mbps at +8, only when associated; otherwise 0xE0821803.
  - `getPOWER`: +4 = 1, +8 = power state.
  - `getDRIVER_VERSION`: +4 u16 len, +6 string, max 383.
  - `getCHANNEL`: `apple80211_channel` at +4.

### Events (driver → IO80211)
All events go through
`IO80211Controller::postMessage(ctrl, primaryIface, msg, data, len, true)`.
That call forwards to `IO80211SkywalkInterface::postMessageInternal`, which hands the event to
`IO80211Glue::routeEventToWcl` (the in-kernel WCL state machine).

| msg | len | when | payload |
|---|---|---|---|
| 125 / 126 | small | scan started / finished | 126 = u16 0x0100 |
| 201 | 68 + ie_len | one per BSS found | `apple80211_beacon_msg` (below) |
| 237 | 4 | scan done (WCL) | u32 status: 0, or 0xE0820400\|fw_status |
| 211 | 28 | auth / assoc result | u16 IEEE status, u16 Apple reason (1001/1003/1004/1017…) |
| 212 | 20 | join (SET_SSID) result | u16 status, u16 reason, bssid[6], pad, u32 0xE0820400\|status, u32 0xE0821000\|reason |
| 213 | 164 | connect complete | u16 status, u16 reason, 10×16 B per-candidate records |
| 216 | 16 | link up / down | bssid[6], u8 up, u8 iface id, u32 reason (down: 9 = internal) |
| 203 | ? | deauth / disassoc received | not decoded yet |
| 214 | ? | firmware join aborted | |

**`apple80211_beacon_msg`** (68-byte header, then the IEs from the SSID element on, max 2048):

| Offset | Field |
|---|---|
| +0 | u32 ie_len |
| +4 | u16 chanspec (chan \| sb<<8 \| bw<<11 \| band<<14) |
| +6 | ssid[32] |
| +38 | ssid_len |
| +39 | primary channel |
| +41 | bssid[6] |
| +48 | s32 rssi |
| +52 | s16 noise |
| +56 | u16 beacon interval |
| +58 | u16 capinfo |
| +64 | u32 flags (bit1 always set) |

**Link state is not set by the driver.** Broadcom never calls
`IO80211InfraInterface::setLinkState` for the STA interface. WCL calls it from
`IO80211InfraInterface::setWCL_LINK_STATE_UPDATE` (implemented in IO80211 itself) after the
driver posts 212/211/213/216. So our layer posts events and lets WCL drive en0's link state.

### Interface bring-up (`AppleBCMWLANSkywalkInterface::start(core, RegistrationInfo*)`)
1. `AppleBCMWLANCore::createSkywalkInterface` → `withProvider` +
   `IOSkywalkEthernetInterface::initRegistrationInfo`.
2. `InfraProtocol::initWithProvider(core)` sets up the IOUserClient routing.
3. `IO80211InfraInterface::start(core)`.
4. Create the `IOSkywalkPacketQueue`s (TX/RX) and an `IOSkywalkPacketBufferPool`, and add
   each one to the `IO80211WorkQueue` (vtable +320).
5. `IO80211InfraInterface::registerInfraEthernetInterface(regInfo, queues, nqueues+2, pool, pool)`.

Data therefore flows through Skywalk packet queues, not mbufs. The layer has to convert
between Skywalk packets and the radio ABI's mbuf hook. That conversion is the main new code
in step 3.

## Reference: YYavicWiFi (github.com/yusufcanyavic/YYavicWiFi, read 2026-10-27, commit 4b106f3)
MediaTek MT7922 driver whose native layer is an `IO80211Controller` + `IO80211InfraInterface`
subclass with a Skywalk data path. Facts that shape our plan (ideas only; its layer is GPL-3.0,
so nothing is copied):
- **It targets macOS 15.8.1 (24H32) only**, not Tahoe: at boot it compares the "OS Build Version"
  and disables the native layer on any other build (`-yyavicnoverify` overrides). So no public
  project has this working on 26.x yet; our 25.6.0 vtable dumps are new ground.
- Same method as ours: its `IO80211_15_8_1.h` is auto-generated from the KC vtables and checked
  at the mangled-name level, and the ~200 pure-virtual stubs are generated too.
- Split design: the native layer is a **separate kext** that drives the radio through a small
  ABI over `callPlatformFunction` (scan start/copy, channels, join, status, leave, MAC, power,
  an RX data hook, TX of Ethernet frames). The radio's own Ethernet interface reports link-down
  while the hook is attached.
- **airportd hands the PMK to the driver** inside the `setWCL_ASSOCIATE` candidate, and the
  radio still runs the 4-way handshake in the kernel. That matches our `WPA2Supplicant.c` (we
  already derive and use a PMK), so our supplicant stays; only the PMK source changes.
- Only ~22 InfraInterface methods carry real logic (`setWCL_SCAN_REQ`, `setWCL_ASSOCIATE`,
  `setWCL_JOIN_ABORT`, `setWCL_LEAVE_NETWORK`, `getWCL_BSS_INFO`, `getWCL_EXTENDED_BSS_INFO`,
  `getWCL_TRAFFIC_COUNTERS`, `getCHANNEL`, `getRSSI`, `getRATE`, `getMCS`, `getMCS_VHT`, `getNSS`,
  `getOP_MODE`, `getSUPPORTED_CHANNELS`, `getHW_SUPPORTED_CHANNELS`, `getCHANNELS_INFO`,
  `getCOUNTRY_CHANNELS_INFO`, ...) plus the 12 controller pure virtuals. Events posted back:
  driver-available, scan done, link up, supplicant keyed, join failed.
- Known gaps there: no sleep/wake, no WPA3/Enterprise, partial roaming. We already have sleep/wake.

## Proposed plan for RTL8814Native
1. **Radio ABI in RTL8814Native** (no IO80211 dependency): export scan/join(PMK)/status/leave/
   MAC/power/RX hook/TX through `callPlatformFunction`. Everything it needs already exists
   (`test_uc` scan/join paths, `RTLEthernetAdapter` RX/TX). Testable with `test_uc` alone.
2. **Generator for 25.6.0**: extend `Tools/re/vt.py` to emit a C++ header (class layouts by slot)
   and the stub `.inc` files, plus a slot-by-slot check against the KC.
3. **New layer kext `RTL8814AirLayer`** (GPL-2.0, written fresh): Controller + InfraInterface
   subclasses, build gate on 25G221 or the exact IO80211 UUID, the ~22 real methods, the rest stubs.
4. Recover the `apple80211_*` struct layouts we need (WCL scan request, associate candidate,
   BSS info) from IO80211Family / AppleBCMWLAN* disassembly.

## Progress (2026-10-27, no reboot)
### Step 1 — radio ABI in RTL8814Native (build 4.10, staged, NOT deployed)
- `Source/Driver/RTLRadioABI.h` (ABI 1): `rtlRadioAbi`, `rtlRadioScanStart`, `rtlRadioScanCopy`
  (BSS with full IEs, capinfo, beacon interval), `rtlRadioChannels`, `rtlRadioJoin` (SSID + PMK +
  optional BSSID), `rtlRadioStatus`, `rtlRadioLeave`, `rtlRadioMac`, `rtlRadioDataHook`, `rtlRadioTx`.
- Entry: `RTL8814Native::callPlatformFunction` (only the instance that owns the radio answers;
  others return `kIOReturnNotReady`). Backends in `VendorProbe.c` (`rtl8814native_radio_*`).
- Join with a PMK reuses the supplicant: the PMK is passed as 64 hex digits (the supplicant
  already treats a 64-char PSK as the PMK); with a BSSID it uses `rtw_set_802_11_connect`.
- Data hook: `RTLEthernetAdapter::inputFromDriver` hands frames to the hook (under `_stateLock`)
  and en2 reports link inactive while a hook is attached; `transmitFrame()` is shared by en2's
  `outputPacket` and `rtlRadioTx`.
- `kRTLUC_RadioCall` (selector 9) lets `test_uc radio [abi|status|mac|channels|scan|leave|join]`
  drive the same `callPlatformFunction` path. `radio join <SSID> <PSK> [BSSID]` derives the PMK in
  user space (CommonCrypto PBKDF2), so the passphrase never reaches the kext on this path.
- Build: MD5 `9aed0533c96d7925d920095dff42b2a8`, UUID `2AF2FAB8`, 0 undefined vendor symbols,
  **imports identical to 4.9** (an `IOMallocZero` import was replaced by IOMalloc+bzero).
  Staged at `Backups/build_4.10/`; the EFI keeps 4.9 until 4.9 is verified.

### Step 2 — slot-exact header for 25.6.0
- `Tools/re/gen_io80211.py` -> `Source/IO80211/IO80211_25_6_0.h` plus `_ctrl_decl/_stubs.inc`
  (12 pure virtuals) and `_infra_decl/_stubs.inc` (203 pure virtuals). Chain:
  `IOService -> IOSkywalkInterface -> IOSkywalkNetworkInterface -> IOSkywalkEthernetInterface ->
  IO80211SkywalkInterface -> IO80211InfraInterface -> IO80211InfraProtocol` (the class a driver
  subclasses in 26.x; Apple's concrete one is `AppleBCMWLANSkywalkInterface`, which also names
  the remaining pure slots), and `IOEthernetController -> IO80211Controller`.
- **Fixed 2026-10-27:** the stub list used to include `enable`, `disable` and
  `initBSDInterfaceParameters`. They are pure in `IOSkywalkNetworkInterface` but
  `IO80211SkywalkInterface` / `IO80211InfraInterface` implement them. A stub there would have
  replaced Apple's code with `return 0`. The generator now stubs only the slots still pure in
  `IO80211InfraProtocol` / `IO80211Controller`: **200 infra + 12 ctrl**. The header itself is
  unchanged.
- Every override (including overrides of IOService / IOEthernetController virtuals) is redeclared
  in its owning class with the SDK's return type, so a subclass binds each slot to Apple's
  function, not the SDK parent's.
- `Tools/re/check_io80211.sh` compiles `Tools/re/check/layout_test.cpp` (stub subclasses) and
  checks: **vtable mismatches 0** (668 InfraProtocol slots, 465 Controller slots), **298
  IO80211/Skywalk imports, 0 missing from the KC**. Known difference: the public SDK declares
  `IONetworkController` slots 325-327 as `_RESERVED2..4`; the KC has `outputStart`,
  `setInputPacketPollingEnable`, `pollInputPackets`. RTLEthernetAdapter has the same today; we
  rely on the kext linker's pad-slot patching. This is confirmed indirectly: build 4.9 imports the
  same `_RESERVEDIONetworkController2..5` symbols, which the KC does not export, and it loads
  and works.
- Return types for non-SDK, non-get/set methods default to `uintptr_t`; fix the ones we call or
  implement in `RET_OVERRIDES`.

### Step 3 — layer kext (RTL8814AirLayer 0.5 staged; 0.4 deployed, start() fails)
- **Code:** `Source/AirLayer/RTLAirLayer.{hpp,cpp}`, `Source/AirLayer/Info.plist`, `./build_airlayer.sh`.
  It is a separate kext; `./build.sh` and RTL8814Native are untouched.
- **Staged build:** `Backups/airlayer_0.4/`, MD5 `bc6dfde7eef40abe535862bd897c5001`,
  UUID `3E0D36DC-DC73-3502-8FE2-15DBD13695FE`. 0.1-0.3 (in `Backups/airlayer_0.1` … `_0.3`) are
  superseded, see the reviews below.
- **Matching and safety:**
  - Matches `IOProviderClass = RTL8814Native` and needs radio ABI 1 (build 4.10+; 4.11 adds the
    `scanning` status flag the layer uses for scans while associated).
  - **`start()` refuses unless the boot-arg `rtlair` is set.** In the EFI without the boot-arg it
    does nothing.
  - `rtlair=1`: scan and getters only. `setWCL_ASSOCIATE` is refused, so neither the menu nor an
    auto-join at boot can move the radio off en2's link.
  - `rtlair=2`: joins, and while linked the data goes through the native interface, not en2.
  - Leave / abort / power-off only touch the radio when this layer made the link (joining or
    linked). Getters answer "not associated" unless the layer owns the link, even if the radio is
    linked for en2.
- **Classes:** `RTLAirControllerStubs` / `RTLAirInterfaceStubs` carry the generated stubs, and
  `RTLAirController : IO80211Controller` / `RTLAirInterface : IO80211InfraProtocol` override what
  is implemented. Bring-up follows the 25.6.0 Apple order recovered above:
  1. `IO80211Controller::start`.
  2. Interface: `init()`, then `init(ctrl, mac)`, role 1 / id 1, `attach`.
  3. `attachInterface`.
  4. `initRegistrationInfo(reg, 1, 0x130)`, then fill it as `createSkywalkInterface` does
     (subfamily 3 = Wi-Fi, +0x14 = 2, +0x24, headroom 64, MAC at +0x108).
  5. `IO80211InfraInterface::start`.
  6. TX and RX pools (`PoolOptions` as in `AppleEthernetRL::startInterface`, no DMA spec);
     TxSubmission, TxCompletion and RxCompletion queues, added to the IO80211WorkQueue.
  7. `registerInfraEthernetInterface(reg, {txsub, txcomp, rxcomp}, 3, txPool, rxPool)`.
  8. `registerService(async)`, `setRunningState(false)`.
- **Implemented:**
  - Scan: `setWCL_SCAN_REQ` calls radio `SCAN_START`. A thread call polls status, then
    `SCAN_COPY` posts one msg 201 per BSS and msg 237.
  - Join: `setWCL_ASSOCIATE` takes SSID, PMK (embedded key, len 32) and the first candidate
    BSSID, and calls radio `JOIN`. The poll reports 211 → 216 → 212 → 213 on success, or
    211/212/213 with status 1 after 20 s.
  - Leave / abort.
  - `setCIPHER_KEY` is accepted (the radio runs the 4-way handshake itself).
  - Getters: CHANNEL, RSSI, RATE, OP_MODE, NSS, WCL_BSS_INFO, DRIVER/HARDWARE_VERSION,
    POWER (get/set), isCommandProhibited = false.
  - CARD_CAPABILITIES: version 1, plus the bits AppleBCMWLANCore sets unconditionally
    (+4 u16 0xE66F, +6 0x61, +7 0x04, +9 0x40, +10 0x04, +12 u16 0x0201; struct 28 bytes).
  - SUPPORTED_CHANNELS / HW_SUPPORTED_CHANNELS, from the radio's channel plan:
    - Layout: +0 version, +4 count, +8 `apple80211_channel[400]` (12 B each),
      +4808 include mask, +4812 exclude mask.
    - Entry flags: 0x80 ACTIVE, 0x100 DFS.
- **Data path (step 3b), only with `rtlair=2` and only while linked.** The radio's data hook is
  attached on join and detached on leave or stop. While attached, en2 reports its link inactive.
  - TX: the TxSubmission action copies `getDataVirtualAddress() + getDataOffset()` (as
    `AppleEthernetRL::dequeueTx4DWLegacyPackets` does) into an mbuf, calls radio `TX`, and hands
    the packets back with `TxCompletionQueue::enqueuePackets` in chunks of 64 (the count must
    be below the capacity).
  - RX: the hook runs on the radio's RX thread under the radio's hook lock, so it only queues
    the mbuf (max 512) and kicks a drain thread call. The drain takes `allocatePacket(…, 1)`
    (bit 0 = `kern_pbufpool_alloc_nosleep`, read from the 25.6.0 implementation), copies the
    frame, calls `setDataOffsetAndLength(0, len)` and `setLinkHeaderLength(14)`, then
    `RxCompletionQueue::enqueuePackets` in batches of 64, which takes the queue gate itself.
  - With `rtlair=1`, TX packets are completed unsent and en2 keeps carrying traffic.
- **Checks** (`Tools/re/check_airlayer.sh`):
  - vtable mismatches 0, for every override against the Apple class implementing the same method.
  - Imports: 753, all in the KC except the 4 IONetworkController pad slots (same as 4.9).
  - `IOWorkLoop::addEventSource` = vtable +0x140, the slot Apple calls on IO80211WorkQueue.
  - Vendor-pattern undefined symbols: 0.
- **Unverified, to watch on the first boot with `rtlair=1`:**
  - Calling `init(ctrl, mac)` after `init()` (it re-runs the ivar setup).
  - The pool with a NULL DMA spec.
  - Whether BSD attach needs `deferBSDAttach(false)`.
  - Interface id 1 vs 0.
  - The d11ac-style chanspec in msg 201.
  - Posting events from a thread call instead of the work queue.

### First boot with 0.4 + `rtlair=1` (2026-10-27 11:49, hardware)
- The kext loaded (`kmutil showloaded`: RTL8814AirLayer 0.4.0) but `RTLAirController` /
  `RTLAirInterface` instance counts are 0: `start()` returned false and the object was freed. No
  panic; en2 kept working (4.11, manual join).
- No reason visible: `IOLog` never reaches `log show` here, and the dmesg ring is flooded by
  sandbox "System Policy" lines within minutes (even `sudo dmesg` had no boot lines).

### 0.5 (2026-10-27, built, NOT deployed)
- **Log to the registry:** every log line is also appended to the RTL8814Native node as the
  `RTLAirLayerLog` array (max 200). Read without sudo:
  `ioreg -r -c RTL8814Native -d 1 -w0 | grep -A3 RTLAirLayerLog`. The strings survive the layer
  failing and unloading. `start()` logs each step.
- **CoreCapture (would have panicked next):** `IO80211Controller::start` (25.6.0) calls
  `getLogger()` (slot 424) and stores it as the family's global `CCLogStream`, then
  `findAndAttachToFaultReporter` calls `getFaultReporterFromDriver()` (slot 432) and **panics**
  ("No ivars->_faultReporter", IO80211Controller.cpp:2877) on NULL. Our stubs returned NULL. 0.5
  builds both before `IO80211Controller::start`, as `AppleMultiFunctionManagerLogger::
  initWithProviderAndOptions` does on 25.6.0 (YYavicWiFi hit the same panic on 15.8.1):
  - Log: `CCPipe::withOwnerNameCapacity` with CCPipeOptions (0x350 B: +0 type 0, +8 = 1,
    +0x10 0x1000, +0x18 0x666, +0x20 1000, +0x24 name, +0x124 prefix, +0x224 0x200080000,
    +0x244 component), then `CCStream::withPipeAndName` (CCStreamOptions 0x358 B: +8 level 5,
    +0xc 1, +0x58 prefix) -> CCLogStream, `CCPipe::startPipe`.
  - Fault: data pipe (+0 type 1, +4 = 2, +8 = 2, +0x10 64), data stream (+0 type 1, +8 = -1)
    -> CCDataStream, `CCFaultReporter::withStreamWorkloop` (own IOWorkLoop), `addOwner` x2,
    `startPipe`, wrapped in `IO80211FaultReporter::allocWithParams` (as
    `AppleBCMWLANBusInterfacePCIe::deferredStart`).
  - New library: `com.apple.driver.corecapture` 1.0.0. Its symbols are now dumped in
    `Docs/re/io80211_25.6.0/syms_com.apple.driver.corecapture.txt`, so the import check covers them.
- Rest of `IO80211Controller::start` on 25.6.0, for the next failure: `createIOReporters`,
  `setupControlPathLogging`, `IO80211RangingManager`, `IO80211CommandGate` on `getWorkQueue`
  (slot 397, Apple's), `CreatePostOffice` (slot 440, Apple's), timer source / factory, RNG agent,
  frame pool. None of those slots are ours.
- Staged: `Backups/airlayer_0.5/`, MD5 `f1ccd8a5d41d6d216ffcbb9334f30c24`, UUID `30F70EA6`.
  Checks: 0 vtable mismatches, 769 imports (only the 4 pad slots missing), analyzer clean.
  `Tools/phase5_deploy.sh` now installs 0.5.
- The IDA database now lives in `~/re_cache/ida/boot.kc.i64` (the scratchpad is wiped on reboot).

### 0.4 review (2026-10-27, no hardware)
- **Packet queues were never enabled.** The family does not call `enableDatapath`; it is the
  driver's own `AppleBCMWLANSkywalkInterface::enable(unsigned)` (and `AppleBCMWLANCore::powerOn`)
  that calls it after `IO80211SkywalkInterface::enable`, on the work queue, and
  `enableDatapath` enables the queues (`IOEventSource::enable`, vtable +336). `disable` mirrors it.
  0.3 inherited the empty base, so with `rtlair=2` `RxCompletionQueue::enqueuePackets` would have
  refused every packet (it checks `enabled`). 0.4 overrides `enable` / `disable` (slots 266 / 267,
  vtcompare 0 mismatches) and switches the three queues on the work queue.
- Tools for the test boot: `Tools/phase5_efi_check.py "<EFI mount>"` (read-only: which builds are
  in the EFI by MD5, `._` files, Kernel -> Add order, rtlair, NVRAM Delete of boot-args) and
  `Tools/phase5_collect.sh` (no sudo: log since boot, registry, interfaces, radio status into
  `Logs/phase5_<date>.txt`).

### 0.3 review (2026-10-27, no hardware)
- **en2 safety:** in 0.2 a WCL leave or power-off (airportd sends both at boot and on menu
  toggles) called radio `LEAVE` even when the link was en2's, and `rtlair=1` still accepted joins
  without a data path. Either could cut the machine off. Fixed as listed under "Matching and
  safety".
- **Interface stop:** added `RTLAirInterface::stop`, as `AppleBCMWLANSkywalkInterface::stop`
  does it: on the work queue, for each packet queue `disable()`, `removeEventSource`, release;
  then `IO80211InfraInterface::stop`. 0.2 freed the queues while still on the work queue.
- A join while linked detaches the data path first (en2 carries data until the new link is up).
- RATE / NSS / POWER getters now fill the version (and RATE the radio count) fields.
- `start()` refuses a zero MAC (radio not up yet).
- `clang --analyze` and `-Wall -Wextra`: no findings.

### 0.2 review (2026-10-27, no hardware)
Bugs found re-reading 0.1, all fixed in 0.2:
- **Deadlock (data path):** 0.1 handed RX frames to Skywalk from inside the hook. The radio
  calls the hook with its `_stateLock` held, and `RxCompletionQueue::enqueuePackets` takes the
  IO80211 gate. A WCL leave/abort/power-off runs with the gate held and detaches the hook, which
  takes `_stateLock`: lock-order inversion. 0.2 queues in the hook and drains on a thread call.
- **RX allocation could sleep:** `allocatePacket(…, 0)` is `kern_pbufpool_alloc` (may sleep);
  bit 0 selects the no-sleep variant. 0.2 passes 1.
- **Scan while associated finished at once:** the radio reports `LINKED`, not `SCAN`, while an
  associated radio scans, so 0.1 posted the stale list after 500 ms. RTL8814Native 4.11 adds
  `rtl_radio_status.scanning` (in a reserved byte, ABI stays 1); 0.2 also waits at least 2.75 s
  for such a scan, for builds without the flag.
- **Join lost if a scan was pending:** the poll finished the scan and returned without
  re-arming for the join. 0.2 runs scan, join and link monitor from one poll.
- **No link-loss report:** after a join nothing watched the link. 0.2 polls every second while
  linked; on loss it detaches the data path, stops the vendor reconnect (radio `LEAVE`) and posts
  216 down (reason 9).
- **Leave racing the join report:** `reportJoin` now claims the joining phase under the lock, so
  a leave/abort in between wins.
- **WCL_BSS_INFO** used the link width in a chanspec built from the primary channel (wrong for
  40/80 MHz); it now uses 20 MHz like the scan, and reads the scan cache under the lock.
- **Lifecycle:** a failed `start()` leaked locks / thread calls and left the controller half
  started; resources now go in `free()`, and a failed start undoes the interface and calls
  `IO80211Controller::stop`. `stop()` quiesces hook, poll and drain before terminating.

### First test plan (needs someone at the machine: EFI mount, sudo, reboots)
1. **Verify 4.9 (already in the EFI):**
   - `kextstat | grep RTL8814`: UUID 49EC946A.
   - Wi-Fi on en2.
   - If it is broken, roll back to 4.8 from `Backups/efi_backup_m48`.
2. **Deploy 4.11** from `Backups/build_4.11` (dot_clean, sync, MD5 db46a680; 4.11 = 4.10 + the
   `scanning` status flag, same imports as 4.10):
   - `./Tools/test_uc radio abi`: should return 1.
   - `radio status` (now prints `scanning=`), `radio mac`, `radio channels`, `radio scan`.
   - You can also run `radio join "MyNetwork" "TuPassword"` yourself.
3. **Deploy the layer, control plane only.**
   - Copy `Backups/airlayer_0.4/RTL8814AirLayer.kext` to `EFI/OC/Kexts`.
   - Add it to config.plist → Kernel → Add **after** RTL8814Native.
   - Add `rtlair=1` to boot-args.
   - Before rebooting: `python3 Tools/phase5_efi_check.py "<EFI mount>"` must say OK.
   - Reboot, then run `./Tools/phase5_collect.sh` and check:
     - `log show --last boot --predicate 'eventMessage CONTAINS "RTL8814AirLayer" OR eventMessage CONTAINS "IO80211"'`
     - `ioreg -l -w0 | grep -E "RTLAir|IO80211"`
     - `ifconfig -a` (a new enX)
     - The Wi-Fi menu: networks should show up after a scan. Joining from the menu is refused
       with `rtlair=1` (by design); en2 must keep working the whole time.
   - **Rollback:** remove `rtlair=1` from boot-args. The layer stays inert, so nothing else
     needs to change.
4. **Data path:** with `rtlair=2`, join from the Wi-Fi menu; DHCP should come up on the new
   interface instead of en2.

## WPA3-SAE gates (layer 0.29 / 0.30)

macOS refuses a WPA3-only network before the layer ever sees it, unless the card says it can do SAE. I found three gates by tracing `airportd` and `Apple80211Associate2` (IO80211.framework) with lldb breakpoints and `dyld_info -disassemble`:

1. `airportd` checks that `capabilities` contains 75, otherwise it fails with "WPA3-SAE security not supported for this platform". That is userspace byte 9 bit 3 of CARD_CAPABILITIES, so `p[13] |= 0x08` in the kernel struct (the family strips a 4-byte version, same shift as the AWDL bit).
2. `Apple80211IsAuthSelAllowed(SAE)` needs that same bit, `wifi.forcewpa2` off, and a PMF flag.
3. The PMF flag comes from `_getMFPCaps`, which reports nothing unless userspace byte 6 has bit 0 or bit 4 set (`p[10] |= 0x10`; Broadcom sets it from a firmware feature). Without it `Apple80211Associate2` returns -3900 after 4 ms and no ioctl is sent.

With all three set (layer 0.30), `setWCL_ASSOCIATE` finally runs for a WPA3 hotspot. What macOS hands over for SAE:

- the key length is the passphrase length (8 for my test hotspot), not 32, and the passphrase itself is at `key + 16`;
- request header dwords: `+16` is 1 and `+20` is 0x1000, which I read as open-system lower auth plus the SAE upper auth.

Side effect: macOS now also tries SAE on my WPA2/WPA3 mixed router, and the layer still refuses anything that is not a 32-byte PMK, so that router stops joining. Layer 0.28 does not have the bits and keeps working. What is missing for real SAE is the Dragonfly exchange on P-256 (commit and confirm in auth frames), PMF with BIP, and the key schedule into the existing 4-way handshake.
