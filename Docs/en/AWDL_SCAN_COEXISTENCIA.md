# AWDL: coexistence of infrastructure scanning with awdl0

Question: with `awdl0` up and AWDL active, what limits the infrastructure scans that
airportd/WCL request from the driver? Source: kernelcache 25G241 (md5
`f489ff8be80e6013a1bf47561dcaef45`),
x86_64, disassembled over a copy of the annotated base; every cited address was verified as a
function start in that copy. Anything unverified is marked `?`. Complements
`AWDL_CHANNEL_SEQUENCE_CONTROL.md`
(who renders the TLVs 4/18) and `AWDL_PEERMANAGER_SOFTWARE.md` (election and timers).

## 1. Overview

Infrastructure scanning is not rejected by the driver: the family filters it before it arrives and,
once running, it is the `IO80211AWDLPeerManager` who steps aside. The layers involved, in order:

1. `IO80211ScanManager` validates, deduplicates and consults the AWDL/NAN policy; it may return
   `EBUSY`.
2. WCL (the kernel common layer, classes `WCLScanManager*`) enqueues, rejects if another scan is in
   progress or there is incompatible activity (join, IP, reassoc, BT, low signal) and converts the
   request to the driver format.
3. The driver (Broadcom or another) receives the scan and runs it at its own pace; the family does
   not measure it per channel.
4. The peer manager learns of it via `scanStarted`/`scanDone` (and via firmware events on
   Broadcom),
   overlays the scan on its channel sequence (zeroed slots) or pushes it up as a *piggyback scan*
   to
   the firmware, and restores data mode at the end.

For a third-party driver the practical consequence: the scan arrives the same with AWDL active
(already filtered by the previous gates), and the real limit of the mono-channel hardware is set by
the driver itself, which must interleave the return to the AWDL channel between scan channels and
communicate the gaps in the TLVs 4/18.

## 2. Path of a scan request

airportd issues the apple80211 scan command (type 10) over the infrastructure interface; the kernel
bridge is `setSCAN_REQ` (`0xffffff80022874aa`), with the alternative user client path
`setScanRequest` (`0xffffff800235650c`), which copies `min(len, 5528)` bytes to the kernel.

`setSCAN_REQ` (`0xffffff80022874aa`):

1. `IO80211Controller::getPrimaryInterfaceScanManager` → without a manager returns 6.
2. `IO80211ScanManager::isScanAllowedByP2P` (`0xffffff80022e1402`); something other than 0 cuts
   here.
3. Forwarding first to WCL: `IO80211Glue::sendIOUCToWcl(glue, 0x802869C8, 10, scan_data, 5528)`.
   `IO80211SkywalkInterface::routeIoctlToWcl` (`0xffffff80022f9bf6`) returns `0xE082280F` if there
   is no
   glue (`iface+0x120 → +0xD8`) or if WCL does not claim the command.
4. If WCL did not claim it (`0xE082280F`), it falls to the direct bridge `apple80211setSCAN_REQ`
   (`0xffffff8002267ac8`): checks slot +0xCC8 of the interface with type 10
   (`IO80211SkywalkInterface::isCommandProhibited`, `0xffffff80022f9bce`, which delegates to slot
   +0xC50 of the controller), requires `safeMetaCast` to `IO80211NoneProtocol` and jumps to its
   slot +0x1B0.
   Without an `IO80211NoneProtocol` ancestor it returns `0xE082280E`. The hierarchy is closed by
   the metaclass constructors: `IO80211NoneProtocol::MetaClass` (`0xffffff8002186332`) descends
   from
   `OSObject` (size 0x10) and `IO80211InfraProtocol::MetaClass` (`0xffffff80022ca72c`) descends
   from
   `IO80211InfraInterface` (size 0x130): an `IO80211InfraProtocol` driver does not pass the cast to
   `NoneProtocol`, so its scan enters via the WCL path (§7).
5. Send errors: `0xE0820447` → `scanDone(9, ...)`, `0xE0820448` → `scanDone(7, ...)`,
   others → `scanDone(8, code)`.

`IO80211ScanManager::startScan` (`0xffffff80022e16a8`) adds, before the send:

- `validateScanData` (`0xffffff80022e107a`): `ssid_len` (+0x10) < 33, `num_channels` (+0x44) < 401
  and
  `num_networks` (+0x1314) < 11; if not, 22 (BADARG).
- Deduplication: `searchForPendingAndClearPid` (`0xffffff80022e0be2`) looks for a pending scan from
  the same
  pid; if it is <21 s old and the 5528 bytes are identical, the new one hangs as a waiter on the
  previous one (coalescing); if it exceeds 21 s, the expired node is deleted.
- Nodes: `_allocScanReqNode` (`0xffffff80022e0a8e`) copies the `apple80211_scan_data` (5528 B) or
  the
  `apple80211_scan_multiple_data` (5352 B) into a 10936 B node; maximum 400 channels per request.
- `IO80211Controller::scanStarted` (`0xffffff8002294794`) warns the peer managers.
- It generates a UUID, writes it as a string at `scan_data+0x154C` and stores it in the controller.

The multi-type scan (`fastScanMultiple`, `0xffffff80022e0df4`) first consults the result cache
(`getCachedScanResults`) and only enqueues a node if the cache does not answer; `fastScan`
(`0xffffff80022e09f2`) is pure cache.

## 3. The AWDL gates of the family

### 3.1 isScanAllowedByP2P (`0xffffff80022e1402`)

- NAN: if `IO80211NANPeerManager` exists, it consults `isInfraScanAllowed(flags bit 0x20)` and
  three state bits; failing them returns 16 (the log says "returning EBUSY").
- AWDL: calls `isScanDisallowedByAwdl` (§3.2). If AWDL or NAN block and `scanType != 3`, log
  `"IO80211ScanManager::startScan AWDL %d, NAN %d returning EBUSY"` and returns 16.
- If `scanType` is 5 or 6, it rewrites the type at `scan_data+0x34`: it becomes 2, and 1 when the
  requested channel (`scan_data+0x48..`, 12 B entries) turns out DFS per
  `IO80211AWDLPeerManager::isDFSChannelInfo`.

### 3.2 isScanDisallowedByAwdl (`0xffffff80022e111e`)

It obtains the `IO80211AWDLPeerManager` by `safeMetaCast` from
`controller→+0x190→+0x120→+0x28`. It accepts `apple80211_scan_data` or
`apple80211_scan_multiple_data` (+512/+528/+5336). It reads `scanType`
(+0x34 / +0x200), the number of channels (+0x44 / +0x210) and flags (+0x1308 / +0x14D8). With the
peer manager present it computes, over the manager ivars (+0x20):

- `RTModeScan` = bits +0x3F1C.0 / +0x3F32.0 / +0xFBA2.0 / +0x40D8.0 and `scanType != 5` `?` (ivars
  without symbolic names; per the log below, real-time mode policy).
- `NonSteering` = `bssSteeringInProgress() != 0` or (+0x3EE0 bit1 and +0x4200.0), with `scanType !=
  6`.
- `assistDisc` = +0xFA3A.0; `HTMode` = +0x3F30.0.
- `AWDL-restore` state (+0xFF20) other than 0 cuts with "allowed" (log "AWDL-restore state:%d").
- Only if (`RTModeScan` ∨ `NonSteering` ∨ `assistDisc` ∨ `HTMode`) does it apply the restriction:
  forbidden
  unless `flags & 0x20` is set, or `scanType == 6`, or (`deviceAllows2GOnlyScan()` and all the
  channels of the list are ≤ 14 (2.4 GHz; it walks the 12 B entries comparing the channel dword)).
  Outside those modes the scan simply continues.
- The decision log (`0xffffff80022e13a8`, line 1148) prints
  `"InfraScanAllowed %d (RTModeScan %d NonSteering %d assistDisc %d HTMode %d RTModeNeeded %d Immin %d
  ScanType %d Flags %llu ScanOn2GOnly %d DevAllows2G %d)"`.

The 0x20 bit of the flags is thus the marker "scan compatible with AWDL in real-time mode": scans
the peer manager itself launches carry it (`scanForPeersBSSIDOnDFSChannel` (`0xffffff80021f043a`)
builds an `apple80211_scan_data` with `flags |= 0x28`, `scanType` 5, one channel, and feeds it
through
`startScan`; it retries after 200 ms if it fails).

### 3.3 Others

- `isOpModeScanFriendly` (`0xffffff80022e1dac`): reads `OP_MODE` via WCL (`0xC02869C9` sub 0xF) or,
  if
  WCL does not respond, with `apple80211getOP_MODE`, and returns false if the mode has bits
  0x1800000000 set. `startScan` then rejects with "Rejecting scan request because of interface's
  operational mode", and `WCLScanManager::sendRequest` has its own variant of the same message.
  A driver advertising those bits in `OP_MODE` ends up without scans.
- WCL also keeps a per-scan-in-progress rejection: `WCLScanManager::handleScanRequest`
  (`0xffffff800219c882`) returns 16 with "scan in progress rejecting" when a request is already in
  progress, and `scanRequestHandler` (`0xffffff800219a4a6`) validates length 5528 and sanitizes with
  `IO80211ScanSanitizer::validateAndSanitizeScanData` (`0xffffff80022d976a`).

## 4. WCL: incompatible activity and pace

`WCLScanManager::isScanAllowedByOtherActivity` (`0xffffff800219b9c2`), called from
`sendRequest` (`0xffffff800219b672`), rejects the scan (with its code and a reason in the caller's
mask) if:

- GAS manager busy (16), NDD busy (`0xE0820442`), join manager busy (`0xE0822401`).
- IP resolution in progress (`0xE0820442`), unless it has been in that state for >10 s.
- High-priority reassociation with less than 3 s (0xBB7 ms).
- Roaming by low RSSI in progress, except a `LOW_LATENCY` scan (flag 0x20 at `+0x1310` of the WCL
  request).
- Suppression by link loss or deauthentication (`0xE0820442`).
- Associated waiting for IP (`0xE0820447`); BT busy with a call (`0xE0820447`); low-signal zone
  (RSSI ≥ −90 and below the roam threshold, `0xE0820447`).

`sendRequest` converts the request with `WCLScanRequest::getScanRequestForDriver`
(`0xffffff80022fe1de`) → `IO80211ScanRequest::getScanRequestForDriver` (`0xffffff80022eb05e`) and
sends it to the controller as user client command 441 with 5456 bytes.

`updateScanTimeout` (`0xffffff800219c1b4`): the scan timeout is 20000 ms; with AWDL active, ≥100
channels (u16 at +0x44) and the channel of the first entry (+0x4C) outside 1..20 it goes to 60000 ms
with the log `"Long scan while AWDL is enabled, giving more time (60000)"`; a
device flag (+0x40) adds 10000 ms. `timeoutScan` (`0xffffff800219a15c`) fires FSM event 6
on expiry. That is the only host-side time limit; nothing in the family measures
channel by channel.

## 5. AppleBCMWLAN with AWDL on

- `AppleBCMWLANCore::setWCL_SCAN_REQ` (`0xffffff80016758ae`): receives the
  `apple80211ScanRequest`
  (5456 B; number of channels at +0x54, 12 B entries with the channel at +0x5C). If any entry is
  2.4 GHz (channel ≤ 13), it calls `checkBackToBackHostScans` (`0xffffff8001675906`), which only
  accumulates telemetry counters of consecutive scans (<3 s window): it rejects nothing.
- `AppleBCMWLANScanAdapter::startScan` (`0xffffff800174929e`): rejects the scan with `0xE00002D5`
  ("Action frame in progress. Rejecting escan request!") if an action frame is in flight; applies
  the *home/away time* of the request (+0x8) with `setScanHomeAwayTime` (`0xffffff80017490ea`, log
  "Setting Scan Home away time to %u", before each scan); random scan MAC if bit +0x4 asks for
  it; and launches `startEventScan` if the mode is 2.
- `startEventScan` (`0xffffff80017493dc`, builder `fillScanParams2`): builds the firmware scan
  with active dwell = `req+0x48` (40 ms by default, 20 ms when associated), passive dwell =
  `req+0x4C` (110 ms by default), converts each channel to a 2-byte chanspec with `getChanSpec` and,
  if
  the request brings no channels, uses a default list of 13 chanspecs (`kDefaultScanList`). There is
  no host-side batching: the whole scan goes in one command and the firmware reports by events
  (`handleEventScanStart` (`0xffffff800174fc30`) validates two channel "slices" of the event;
  `handleEventScanAdd`; `eventScanComplete` (`0xffffff800174cb06`)).
- The firmware is who knows AWDL (see `AWDL_FIRMWARE_DUTIES.md`): the driver receives
  `WLC_E_AWDL_SCAN_START`/`WLC_E_AWDL_SCAN_DONE` in `AppleBCMWLANProximityInterface::handleEvent`
  (`0xffffff8001715ece`) and logs "AWDL turned off while in a roam scan"/"OOB scan" in
  `doSetSyncState` (`0xffffff8001719e38`). The return to the project's own channel between scan channels is
  done by the firmware `?` (no interleaving logic appears on the host beyond the home/away time).
- `AppleBCMWLANCore::isScanAllowed` (`0xffffff8001675ada`): suppresses scans with bit 0x20 of
  `+0x10419` ("Scans suppressed"), with BT in a call and in the low-signal zone; called from
  `isScanResumeAllowed`.

## 6. The peer manager: notice, reaction and effect on peers

Generic notice (driver-independent): `IO80211Controller::scanStarted` (`0xffffff8002294794`) and
`scanDone` (`0xffffff8002294848`) invoke slot +0xB90 of up to three peer manager objects
(AWDL at `+0x190`, NAN at `+0x3B0`, and the one at `+0xC80`) with `(scanSource, started/ended,
scan_data, status)`. In AWDL it lands in:

`IO80211AWDLPeerManager::setScanningState` (`0xffffff800221924e`), which with infrastructure
connected and
AWDL on (bit 1 of +0x16096) and without `refuse_scans` (ivar +0x37192, settable via
`setAwdlCtl` (`0xffffff80021cde94`), which also exposes `awdlRtScanAllowed`,
`sibCoexAWDLScanChanSeqActive` and low-level dwells like `lvl1_scans_max_dwell_time`):

- On start: if the scan is pure 2.4 GHz (all channels ≤ 11, `usePiggybackScan`
  (`0xffffff8002219902`)) and the driver supports it (ivar +0x4 == 1 `?`), it sets flag 0x4 in the
  scan flags, copies the 5528 B to `+0x10488` and arms the *piggyback scan* (the firmware runs it
  inside its own windows; `reportDataPathEvents` handles its state machine and on completion
  launches the real scan via `ioctl_internal` and restores state). If not, it enters sequence state
  24 (or 49 with flag +0x65432): `overlayScanRequestOnRealTimeChannelSequence`
  (`0xffffff80021e71c2`) zeroes (`zeroChanInfo`) up to six pairs of slots of the real-time
  sequence to make room for the scan; in SDB mode with a 2.4 GHz scan and three consecutive
  non-2G slots it touches nothing.
- On completion: "Done with Scan, going back to data mode" → `setScheduleState(5, 1, true)`, it
  restores the saved presence mode (+0x17033/+0x17034) and drains the DFS proxy handlers
  (`dfspScanDoneHandler`, `0xffffff80021fd838`) and BSS steering.
- It also notifies through the driver event path `reportDataPathEvents` (`0xffffff80021dd8f4`):
  code 125 "Wifi Infra scan request. 2.4G: %d 5G: %d", 126 "WiFi infra scan complete notification
  from FW", 137 "Roam Scan Started by FW", 243 "Wifi Infra scan add request. 6G channels added %d"
  and
  '>' "Received pscan begin from driver" (state 28). These are the events the Broadcom firmware
  emits;
  a third-party driver `?` has no equivalent channel today except `scanStarted`/`scanDone`.

Other pieces: `scanDisabledInRTMode` (`0xffffff80021f81c4`) summarizes when real-time mode forbids
scanning; `handleAwdlScanTimer` (`0xffffff80021c2d68`) launches the manager's own scans (DFS proxy,
`scanType` 5; BSS steering, `scanType` 6); `updateSibCoexAWDLScanChanSeqActiveState`
(`0xffffff80021cb59c`) activates the overlay for sibling P2P coexistence when scanning 5 GHz;
`followPeerChannelSequence` (`0xffffff80021f6e74`) applies the overlay also when following the
sequence of
a peer if there is a pending scan (container counter `+0x2472`), and otherwise sets state 9.

Effect on peers: the effective sequence (with the overlay's zeroed slots) is the one rendered in
the TLVs 4 and 18, which the driver emits (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §"failures and
templates"); peers thus see reduced availability during the scan and no explicit
"absence" announcement `?` (no unavailability TLV or message associated with scan was found).

## 7. What a third-party controller receives

Two formats depending on the path:

1. **WCL path (that of an `IO80211InfraProtocol` driver, the AirLayer kext case).** The bridge
   `apple80211setWCL_SCAN_REQ` (`0xffffff800226bee2`, command descriptor table at
   `0xffffff800247eeb0`, next to `setWCL_ASSOCIATE` and the other `WCL_*` bridges) checks
   `isCommandProhibited(441)` (slot +0xCC8), requires the cast to `IO80211InfraProtocol` and jumps
   to its slot
   **+0x118** with a 5456-byte `apple80211ScanRequest`: home/away time at +0x8, scan type for
   the firmware at +0x40 (1 active, 2 passive, 3 high precision, 4 low power; 5+ invalid), byte
   flags at +0x44 (0x1 low-power scan core, 0x2 low priority, 0x4 offchannel results, 0x20
   6 GHz probe, 0x40 FILS, ...), active dwell +0x48 and passive +0x4C, number of channels +0x54 and
   12 B entries
   from +0x58 (band, channel, flags: 0x8 2G, 0x10 5G, 0x20 6G) up to 400. The full conversion from
   the original scan is in `IO80211ScanRequest::getScanRequestForDriver` (`0xffffff80022eb05e`),
   with
   WCL policy bits (`WCLScanRequest::getScanRequestForDriver`, `0xffffff80022fe1de`): automatic low
   priority if it can abort a roam, step to low-power scan core if IP is expected, etc.
2. **Direct path (only `IO80211NoneProtocol` ancestors).** `apple80211setSCAN_REQ`
   (`0xffffff8002267ac8`), slot +0x1B0 with the 5528-byte `apple80211_scan_data` intact.

The `apple80211_scan_data` (5528 B) the family validates and shares with the peer manager:

| Offset | Content |
|---|---|
| +0x00..+0x0F | BSSID filter of directed scan (the peer manager writes peer BSSID and channel there) `?` |
| +0x10 / +0x14 | SSID length (<33) and SSID |
| +0x34 | `scanType`: 1 active, 2 passive, 3 exempt from the P2P gate, 5 and 6 reserved to the AWDL's own scans |
| +0x44 | number of channels (<401) |
| +0x48 | 12 B entries: band (dword), channel (dword), flags (0x8 2G / 0x10 5G / 0x20 6G) |
| +0x1308 | u64 flags: 0x2 low priority, 0x8 offchannel results, 0x20 "allowed with AWDL"/LOW_LATENCY, 0x100 6 GHz probe, 0x200 FILS, 0x800 RNR, 0x1000 low-power scan core only |
| +0x1314 | number of networks (<11) |
| +0x154C | scan UUID, written by `startScan` as a string |

There is no per-channel dwell field in this format: the dwell is chosen by the driver (Broadcom
receives 40/110 ms by default in its own format). Results: the driver finishes and the family
(`IO80211ScanManager::scanDone`, `0xffffff80022e2fa2`, fired by `postMessage` from
`IO80211InfraInterface::postMessage` (`0xffffff80023614b8`)) dumps the results with
`queryScanResult` (`0xffffff80022e2e90`; with the scan still in progress it returns `0xE0820445`
"Scan is still in progress, query on the next scan done") to the cache, and airportd reads them
afterward with
`getSCAN_RESULT` (`0xffffff8002279dbc`). Nobody times individual channels: the only time
pressure is the WCL timeout (§4), the `ABORT_SCAN` of airportd (bridge of selector 381) and the
21 s dedup for new requests from the same pid.

## 8. Fields that allow returning to the AWDL channel between channels

- The scan is asynchronous by contract: `setSCAN_REQ`/441 returns immediately and the results
  arrive at the end. Between the start and `scanDone` the driver does whatever it wants with the
  channel; the family only imposes the §4 timeouts (tens of seconds) and the WCL "scan in
  progress" bit.
- Bit 0x20 of the scan flags (and the 2.4 GHz restriction that accompanies it) marks the scans the
  AWDL policy already declared compatible: for those, the family does not even demand extra speed
  (`isScanDisallowedByAwdl` always lets them through).
- The channel entries carry band and flags per channel, so the driver can order the list and
  group the channels sharing a displacement relative to its AWDL channel.
- The home/away time (type 446, bridge `apple80211setWCL_SET_SCAN_HOME_AWAY_TIME`
  (`0xffffff800226c041`), slot +0x12E8 of `IO80211InfraProtocol`) is the mechanism intended to tell
  the driver how much time to return to the project's own channel during the scan; Broadcom applies it per
  scan
  (`setScanHomeAwayTime`). If airportd also emits it against a third-party driver, the kext can
  use it as the seed of its own interleaving `?` (the kext stub today ignores it).
- The `scanType` and the UUID (+0x154C) allow distinguishing internal AWDL scans (5/6) from
  airportd's and correlating start/end.

## 9. Recommendation for a single-channel USB driver

1. Keep the scan on the WCL path: answer 0 to `isCommandProhibited` (slot +0xCC8) for
   types 10, 441 and 446 and receive the `apple80211ScanRequest` in slot +0x118, as
   `RTLAirInterface::setWCL_SCAN_REQ` does today. The `NoneProtocol` bridge (+0x1B0) does not need
   to be implemented.
2. Never reject a scan for AWDL being active: that is what `isScanDisallowedByAwdl` and WCL are
   for, which already filter (or force 2.4 GHz, or require flag 0x20). A local rejection would
   duplicate the policy and break scans the family already deemed compatible.
3. Autopacing: split the channel list into short batches (e.g. 20-40 ms dwell per channel) and
   return to the AWDL channel (the parking one or that of the next served window) between batches
   and during every open AWDL window. The margin is given by the 20 s (60 s with AWDL and ≥100
   channels) of the WCL timeout and the `ABORT_SCAN` as a hard limit.
4. Coherence with peers: while the radio is off the AWDL channel, render those slots as
   channel 0 (unserved) in the TLVs 4/18, exactly what the peer manager overlay does with
   `zeroChanInfo`. That way the on-air announcement matches the radio's reality.
5. Notify the peer manager of the scan cycle: `scanStarted`/`scanDone` already notify through slot
   +0xB90 if the driver goes through the standard family path (and the `M_SCAN_DONE` the kext
   emits today reaches `IO80211ScanManager::scanDone`); it is worth keeping so the manager enters
   and leaves the states 24/49/5 even though the firmware does not generate events 125/126.
6. For pure 2.4 GHz scans, imitate the piggyback: they are the case the family marks with flag
   0x4 and treats as harmless for AWDL; they can run completely without protecting windows.

## 10. Verification

- All cited addresses were checked as function starts against the IDB copy of the
  kernelcache 25G241 (`md5` of the KC `f489ff8be80e6013a1bf47561dcaef45`); the supporting
  disassemblies and decompilations remain in `Build/ida_awdl/task_scan_coexist/`
  (`p1_family_gate/out1..out9`).
- Marked with `?`: the exact semantics of the real-time mode ivars (+0x3F1C, +0x3F32, +0xFBA2,
  +0x40D8, +0xFA3A, +0x3F30, +0xFF20), the BSSID filter of the first 16 bytes, the
  *piggyback scan* support per driver (ivar +0x4), the interleaving of the home channel inside
  the Broadcom firmware, the existence of an event channel 125/126 for third-party drivers and
  the use airportd would make of type 446 against a non-Broadcom driver.
