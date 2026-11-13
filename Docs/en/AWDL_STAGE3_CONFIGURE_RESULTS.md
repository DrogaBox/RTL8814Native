# Stage 3: hardware result of `configure()`, `interfaceStateChange` and `enableAWDL`

Result of the tests on the dev machine with the stage 3 ioctl recognition layer
(patches `Tools/awdl_stage3_configure.patch`, `Tools/awdl_stage3b_sync.patch` and
`Tools/awdl_stage3c_acks.patch`): which chain of internal ioctls
`IO80211AWDLPeerManager` walks from `awdl0` coming up until AWDL is enabled, which of them are
fatal and what remains unimplemented. Machine: macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64),
`RTL8814Native.kext` 4.34 and `RTL8814AirLayer.kext`
0.32.0, `rtlair=2 rtlawdl=1`. Addresses are VAs of the kernelcache of that build; anything unseen
carries `?`.

## 1. Result

1. **With the six setters of stage 3 (125, 122, 132, 135, 213, 134) and `setLinkState(2)` in
   `enable()`, `configure()` finishes** (`IO80211AWDLPeerManager::configure configure done!`,
   `doEnable ... Setting
   _enabled to TRUE`). The state stays at `0x21 CONFIGURED OFF`.
2. **The first service request enters `interfaceStateChange` and aborts on ioctl 116**
   (`AWDL_SYNC_PARAMS`, line 6005 of the function): the return `0xE00002C7` (unsupported) leads to
   `Unable to set SYNC params ret -536870201`, link down, `disableAWDL` and `_stateFlags = 0x20`. The
   following 120 requests returned 6 with `Received service request but not configured
   (_stateFlags:0x20)`.
3. **With the recognition of 116, 117, 226, 129, 155, 151, 205, 208, 229, 126, 118 and 418 the full
   sequence passes**: `Initialize FW Election Params` → 129 → 117 →
   `AWDL ON:[infra(149) ...] (6/0/6) [6 6 6 6 6 0 0 0 ...]`. Ioctl 120 (`PEER_SERVICE_REQUEST`)
   returns **0**, and `wifip2pd` registers the searches `_airplay-p2p._tcp`, `_raop._tcp` and
   `_airplay._tcp`. No panics or hangs during boot and the test. The tests of 3b and 3c were done
   together; 3b alone was not tested.
4. **The manager runs the solo election**: `runElection ... New P:<own MAC> R:<own MAC>`,
   `peers:0`, and calls 116/151/134/155 every second.

## 2. Observed chain and fatality

Approximate order of the internal ioctls at AWDL start, with the consequence of a failure:

| Order | Type | Setter | Source | If it fails |
|---:|---:|---|---|---|
| 1 | 125, 122, 132, 135, 213, 134 | BSSID, ELECTION_ID, MASTER_CHANNEL, RSSI_THRESHOLDS, MIN_RATE, SYNC_STATE | `configure` | aborts `configure` (213 is not fatal) |
| 2 | 132, 205 | MASTER_CHANNEL, SECONDARY_MASTER_CHANNEL | `programMasterChannels` | 132 fatal; 205 only logged |
| 3 | 151 | SYNC_FRAME_TEMPLATE | `updateSyncFrameTemplate` | only logged |
| 4 | **116** | SYNC_PARAMS | `interfaceStateChange` (6005) | **fatal at this caller**: link down and `_stateFlags = 0x20`. Any return other than 0 and 82 |
| 5 | 226 | TXCAL_PERIOD | `setScheduleState` | only `Unable to set txcal period` |
| 6 | 341 (read) | `AF_SECONDARY_PAYLOAD_CAPABILITIES` | `enableAWDL` | `ioctl awdl_cap failed`; declares 0 capabilities and continues |
| 7 | 155 | ELECTION_PARAMS | `initFWElectionParams` | only logged |
| 8 | 129 | SYNCHRONIZATION_CHANNEL_SEQUENCE | `setChannelSequenceUnconditional` | `Couldn't set channel sequence`; sequence recovery |
| 9 | **117** | SYNC_ENABLED | `enableAWDL` | fatal: `Failure to enable AWDL`, `+414 = 0` |
| 10 | 208 | AF_TX_MODE | `setAFTXMode` | only logged |

Correction to `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.13: the 116 is listed there as non-fatal. It is
non-fatal in `setSyncFramePeriod`, but in `interfaceStateChange` the failure of the 116 disables AWDL. The rest
of the
table matches what was observed.

## 3. What remains unimplemented (does not slow the boot)

- `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` (341), `getAWDL_SYNC_ENABLED`,
  `getVHtCapabilityIE`/HT capabilities:
  they return `0xE00002C7`; the manager logs it and continues (`awdl_cap 0x0`,
  `Invalid _htCapabilityLength 0`).
- `setAWDL_OOB_AUTO_REQUEST` fails on every sync template (`0xE00002C7`).
- `APPLE80211_IOC_P2P_COEX_PROTECTION` (414): `Unable to set Coex Protection ... unexpected call`;
  `setAwdlElectionMetric (65)` and `notifyRealTimeModeOnOff` fail the same way.
- Requests of `wifip2pd` when creating `awdl0` outside the AWDL surface: 293
  (`COMPANION_SKYWALK_IF_CREATE`)
  and 575 (`OS_ELIGIBILITY`, `0xE082280E`) keep returning errors; the daemon does not treat them as
  fatal.

## 4. What is missing for windows and peers

- **The driver does not publish event 41** (availability window start), nor 42/43. The manager logs
  it every second: `monitorAWEvents WARNING: Did not get AW start event ...` and, at 4 s,
  `ERROR: aw start event missing for too long`. It is a log, no action is taken. The delivery
  path is `postMessage(41, payload, 36, false)` through slot `0xB18` of the interface
  (`AWDL_STAGE3_EVENT_EMITTERS.md` §3) and the layout is at `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2.
- Emitting it without a radio backing the windows would make `inAvailabilityWindow()` return true
  and the manager start pulling TX (`dequeueTxPacket`), whose action today is a no-op. That is why
  it stays deferred until there is real TX and RX over USB.
- Pendings depending on the hardware: window timing with the RTL8814AU, the real cadence of
  `AwNumber` and `FwAwStartTime` (`?`), and an AWDL peer to validate the election, the PSFs and the
  MIFs.
  See `AWDL_ETAPAS_2_A_4.md` §1 and `AWDL_RTL8814_HW_SUPPORT.md`.

## 5. Patches and deployment

| Patch | Content |
|---|---|
| `Tools/awdl_stage3_configure.patch` | `setLinkState(2)` in `enable()` and `setLinkState(1)` in `disable()`; recognition of the six setters of `configure` |
| `Tools/awdl_stage3b_sync.patch` | recognition of 116, 117 and 226 |
| `Tools/awdl_stage3c_acks.patch` | recognition of 129, 155, 151, 205, 208, 229, 126, 118 and 418 |

The recognitions do not touch the radio; they only keep the manager from aborting.
`Build/RTL8814AirLayer.kext` with
the three patches, md5 `647f1ab25c3aa2c32a252b2e60371d8b`, deployed to the internal EFI with
`RTL8814_AWDL=1 Tools/phase5_deploy.sh <EFI> layer 2`.

## 6. Method

`log show` records of the kernel (`com.apple.p2p.awdl0`, `IOC DEBUG`) and of `wifip2pd` after boot
and
after opening AirDrop in the Finder (without Bluetooth, the window shows no peers; request 120 goes
out the same).
Code of `interfaceStateChange` (`0xffffff80021d8cc8`), `enableAWDL` (`0xffffff80021dcf58`),
`setScheduleState` (`0xffffff80021d27a0`) and `programMasterChannels` (`0xffffff80021d494a`) read in
IDA
over a copy of the IDB. The circular log buffer of the kext (`RTLAirLayerLog`) keeps only 200
lines and fills with the WiFi scans; for AWDL the system log is more reliable.
