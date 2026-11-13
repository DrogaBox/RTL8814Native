# What AppleBCMWLANProximityInterface (and AppleBCMWLANCore) delegate to the Broadcom firmware for AWDL

Inventory of everything the Broadcom kext sends to the firmware (iovars, commands and subscription of
events) while an AWDL interface (role 6) is active, with the exact payload of each iovar, the
apple80211 request or the event that triggers it, and the function expected from the firmware. It closes with
the classification of obligations into (a) pure configuration, (b) periodic timing and (c) per-frame
work, as a basis for deciding what a kext of its own without AWDL firmware must reimplement.

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), macOS 26 / kernel
25.6.0, filesets `com.apple.driver.AppleBCMWLANCoreMac` (core, interfaces and Commander) and
`com.apple.driver.AppleBCMWLANBusInterfacePCIeMac` (bus). Headless IDB copy of the kernelcache in
`Build/ida_awdl/task_awdl_fw_duties/` (derived from `Build/ida_awdl/BootKernelExtensions.i64`, the
same base as the `task_dispatch`/`task_datapath`/`task_awdl_structs`/`task_awdl_events` tasks).
Addresses = KC VAs. The payloads were extracted from the disassembly of the sites of
`sendVirtualIOVarSet`/`runVirtualIOVarSet`/`runVirtualIOVarGet`/`sendIOVarSetCommand` (the
Hex-Rays pseudocode hides the name and payload arguments). Unverified items go with `?`.
Complements: [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) (Skywalk datapath),
[`AWDL_EVENTS.md`](AWDL_EVENTS.md) (events toward userspace and payloads),
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) (request type → slot).

## 1. Short summary

1. The single transport toward the firmware is `AppleBCMWLANCommander` (instance of the core at
   `core+0x1540` = +5440, referenced by the interface at `ivars+968`): three entries —
   synchronous `runVirtualIOVarSet`/`runVirtualIOVarGet` (`0xffffff8001760c58` /
   `0xffffff8001760d6c`), asynchronous `sendVirtualIOVarSet` with completion
   (`0xffffff800176103a`) and `runIOVarSet`/`runIOVarGet`/`sendIOVarSet` on the primary
   interface; all converge in `issueCommand` (`0xffffff800175ecf2`), which maintains a
   command queue with its own timer and a maximum size (`getMaxCmdTxPayload`).
2. In role 6 the kext delegates to the firmware: the creation of the AWDL instance on the chip
   (`interface_create`/`awdl_if`), the synchrony engine and its parameters (`awdl`,
   `awdl_sync_params`, `awdl_opmode`), master election (`awdl_election_tree`),
   channel hopping (`awdl_chan_seq`), the availability window and its extension
   (`awdl_extcounts`), sync frame transmission (`awdl_payload`, `awdl_psf_dwell`,
   `awdl_af_hdr`), the scheduling of action frame TX (`awdl_peer_op`,
   `awdl_advertisers`, `awdl_aftxmode`, `awdl_oob_af`, `awdl_oob_af_auto`, `awdl_afs_pload`),
   RX filtering (`awdl_af_rssi`), the DFS proxy (`awdl_dfsp_cfg`, `awdl_dfsp_ucsa`) and
   statistics/state (`awdl_stats`, `cca_get_stats`, `awdl_maxpeers`, `awdl_presencemode`,
   `awdl_osoc_chan`, `awdl_min_rate`, `awdl_phycal_period`, `awdl_config`).
3. Firmware events arrive as 48-byte `wl_event_msg_t` (BDC + bcmeth framing);
   `AppleBCMWLANCore::handleEventPacket` (`0xffffff800160cda6`) routes them to
   `AppleBCMWLANProximityInterface::handleEvent` (`0xffffff8001715ece`), which consumes ids
   0, 16, 26, 54, 59, 60, 69, 75, 80, 96 (`WLC_E_AWDL_AW`), 97 (`WLC_E_AWDL_ROLE`),
   98 (`WLC_E_AWDL_EVENT` and its sub-dispatch), 109, 150 and 152. The canonical names are in
   `kEventBitMapping` (`0xffffff80017ed070`, 208 entries {u32 id, char* name}).
4. The strings `awdl_aws`, `awdl_af_tx`, `awdl_mon` and bare `awdl_election` do not exist in
   this binary: their functions are covered by `awdl_sync_params` (the
   availability window program travels inside the sync params), `awdl_oob_af`/`awdl_oob_af_auto`
   (action frame TX), the value of `awdl_opmode` and `awdl_election_tree` (election).
5. For an RTL8814 port without AWDL firmware: part (a) is storable as-is, part (b)
   requires a software timer with the periods of §6.2 (the real values are programmed by
   userspace via `AWDL_SYNC_PARAMS`; the kext defaults are in `init`, `0xffffff8001718db0`)
   and an election machine (or reuse `IO80211AWDLPeerManager` of IO80211Family, which is
   where election id/metric live in the modern path), and part (c) is already described in
   `AWDL_PROXIMITY_DATAPATH.md` §9/§10 except for the pieces attributed here to the firmware.

## 2. Transport: AppleBCMWLANCommander

- The iovar format is the classic Broadcom one: name + NUL + data. `sendIOVarSetCommand`
  (`AppleBCMWLANProximityInterface::sendIOVarSetCommand`, `0xffffff800171d61c`) concatenates two
  `iovec` into a buffer (`IOMallocZeroData_external`), checks the total against
  `AppleBCMWLANCommander::getMaxCmdTxPayload` and enqueues with `sendVirtualIOVarSet`; the counter of
  in-flight commands lives at `ivars+48` with a maximum of 0x10 (16) — "Too many async iovars in flight
  (%d), rejecting awdl_oob_af" (`0xffffff8001726818`).
- `runVirtualIOVarSet/Get(commander, ifid, name, tx, rx, pref)` (`0xffffff8001760c58` /
  `0xffffff8001760d6c`): validate `ifid < 12` and a non-null name and call
  `AppleBCMWLANCommander::issueCommand(this, pref)`; the upper validation logs
  `ERROR: Virtual IOVar "%s" on invalid interface %d` (lines 1168/1197 of
  `AppleBCMWLANCommander.cpp`). "Virtual" = the command carries the virtual interface id and the
  firmware directs it to the AWDL bsscfg.
- `sendVirtualIOVarSet(commander, ifid, name, tx, rxExpected, completion, pref)`
  (`0xffffff800176103a`): asynchronous variant; the completion is an interface callback that
  arrives via the workqueue of `AppleBCMWLANTaskmaster`. In role 6 the completions used are
  `handleProxIovarAsyncCallBack` (`0xffffff8001718cc6`, generic),
  `handleFlowControlledProxIovarAsyncCallBack` (`0xffffff800171d9ba`),
  `handleElectionTreeIovarAsyncCallBack` (`0xffffff800171c6e0`),
  `handleOobAfIovarAsyncCallBack` (`0xffffff800171d4da`),
  `handleSlotBssIovarAsyncCallBack` (`0xffffff800171d578`) and
  `handleCacheControlAsyncCallBack` (`0xffffff800172056c`).
- `issueCommand` (`0xffffff800175ecf2`, 0x1AFE bytes) implements the queue: "no free commands
  left!", "Cannot send a command async/synch", "Commander Check Queue Timer",
  `enqueueCommand`/`serviceCommand`/`dequeueCommand`, dump "CommandID info: interface[...]".
  The final send to the hardware is done by the PCIe bus (`AppleBCMWLANBusInterfacePCIeMac`) through
  its host→firmware rings (`?` internal bus detail not decompiled in this pass).
- Event subscription: `AppleBCMWLANCore::writeEventBitField` (`0xffffff800167e054`) sends the
  iovar **`event_msgs`** (16 mask bytes from `core+1096`, with value header
  `00 AA AA AA 07 00 00 00`) or **`event_msgs_ext`** (~67 bytes if core feature flag 34
  is active; mask from `core+1096..1124`) with `issueCommand`. The bits are marked by
  `AppleBCMWLANCore::addEventBit`: `doSetSyncState(true)` adds 0x60/0x61/0x62 (96/97/98) and
  `doSetSyncState(false)` adds 0x4B (75) and rewrites the mask (`0xffffff8001719e38`,
  sites `0xffffff800171a051` (addEventBit) and `0xffffff800171a167` (writeEventBitField)).

## 3. Inventory of role 6 iovars

Convention: "SET/GET" = iovar sent by the cited function (address of the function and of the call
site); "trigger" = apple80211 request (type number per
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §5) or the event/internal that causes it;
"payload" = bytes the firmware receives (offsets were read from the disassembly of the site of
`send*`); "expected firmware function" = interpretation of the name and of the fields, marked
with `?` where it is inference.

### 3.1 Life cycle of the AWDL instance on the chip

- **`awdl_if`** (string `0xffffff80017cce93`): implemented by `setAWDL_BSSID`
  (`0xffffff800172181a`), which is also the "createChipInterface" (that is its name in the logs,
  references to the `createChipInterface` string between `0xffffff8001721a96` and `0xffffff8001722a8a`). Firmware < 5 (check
  `core→+780 < 5`): `AppleBCMWLANCommander::runIOVarSet` (site `0xffffff8001721bda`) with
  **20 bytes** `{u32 role/ver = ivars+928 (init 3), u32 op (1 = create), 6 B BSSID MAC
  (ivars+608/612), 6 B own MAC (getSelfMacAddr, slot +0xCF8)}` (hexdump
  `AppleBCMWLAN: sending awdl_if =`); after the send it waits for the internal event
  "Interface Created" with a 3000 ms timeout (`waitForDriverEvent(0, 3000)`,
  `0xffffff8001715c62`) and verifies the MAC with a GET of `cur_etheraddr`
  (`0xffffff8001722303`). Deletion (`deleteChipInterface`, `0xffffff8001722ae2`): same iovar
  with op = 0 and a 1000 ms wait (site `0xffffff8001722d45`).
- **`interface_create`** (fw ≥ 5): `runIOVarGet` on the primary interface with **28-byte
  request** `{u32 0x1BF083, u32 0x0020001C, u32 26 (0x1A), 6 B own MAC, 6 B BSSID MAC}` and
  **response** `{bssCfgIdx, ifIdx, ifName}` ("Return bssCfgIdx %d, ifIdx %d,
  ifName %s"; exact call site `?`, inside `setAWDL_BSSID`); the returned
  index is the bsscfg the firmware assigns to AWDL and the one the virtual iovars address afterwards.
  Trigger: `APPLE80211_IOC_AWDL_BSSID` (type 125) — `setAWDL_BSSID` is slot +0x10D0.
- **`interface_remove`** (fw ≥ 5): `runVirtualIOVarSet` in `deleteChipInterface`
  (`0xffffff8001722ae2`); payload `?` (expected to be the bssCfgIdx returned by
  `interface_create`).
- **`cur_etheraddr`** (string `0xffffff8001796f83`): 6-byte SET from `setMacAddress`
  (`0xffffff8001720354`, site `0xffffff8001720471`) when the chip interface already exists
  (flag `ivars+128 & 1`); verification GET at creation. The role 6 MAC is changed
  **before** opening the synchrony: `setMacAddress` rejects it with `0xE00002D5` if
  `ivars+672 != 0` (sync active) or there is a creation in progress (`ivars+1112 & 1`).
- **`2g_mrate`/`bg_mrate` and `5g_mrate`/`a_mrate`** (`setBssRates` inline in `setAWDL_BSSID`,
  `0xffffff8001721cdc`–`0xffffff8001721cf5`): u32 in units of 0.5 Mbps = 2 × `ivars+704` (init 12 → 12 Mbps);
  the chosen pair depends on the chip's firmware version (`core+1020 < 2` → `bg_mrate`/
  `a_mrate`). The second send (`5g_mrate`) requires core feature flag 28. Expected
  function: base rate of the AWDL management frames (sync frames) `?`.

### 3.2 Synchrony engine

- **`awdl`** (string `0xffffff8001797376`, the literal "awdl"): the master switch.
  SET u32 (4 bytes, 0/1) from `doSetSyncState(bool)` (`0xffffff8001719e38`, site
  `0xffffff800171a24c`, async with generic completion); GET u32 from
  `getAWDL_SYNC_ENABLED` (`0xffffff800171b092`, site `0xffffff800171b118`, rx 4 B).
  Trigger: `APPLE80211_IOC_AWDL_SYNC_ENABLED` (type 117) → `setAWDL_SYNC_ENABLED`
  (`0xffffff800171e42a`) → `setSYNC_ENABLED_internal` (`0xffffff800171e4c2`) →
  `doSetSyncState`. When enabling, in addition to starting the firmware AWDL engine, the kext
  subscribes events 96/97/98, marks concurrency (`setConcurrencyState(2, on)`), forces
  `setAfTxMode` and adjusts power saving (`AppleBCMWLANPowerManager::configurePowerSaveForAWDL`,
  `0xffffff80015eec56`). When disabling it rewrites the event mask and clears state.
- **`awdl_sync_params`** (string `0xffffff80017cadfe`): the temporal program of the synchrony.
  SET of **36 bytes** from `setSyncParams` (`0xffffff800171dbb2`, site `0xffffff800171dd26`,
  async, completion `handleProxIovarAsyncCallBack`): `+1..+2` u16 = `0x0024` (36; `?` semantics:
  logical length of the record), `+6` u8 = `fLifeTime` (`ivars+688`, init 6), `+8` u16 =
  `aw_period` (`ivars+728`), `+0xA` u16 = `adv_period`/action frame period (`ivars+740`,
  init 100), `+0xE` u16 = `aw_ext_len` (`ivars+732`), `+0x10` u16 = `aw_len` (`ivars+724`);
  rest zeroed. The ivars are loaded by `setAWDL_SYNC_PARAMS` (`0xffffff800171ddee`: request+4..20 →
  ivars+724/728/732/740/668; log `aw_period %lu aw_len %lu aw_ext_len %lu adv_period %lu as non
  election master %lu`). Triggers: type 116 (`AWDL_SYNC_PARAMS`, len 24) and type 132
  (`setAWDL_MASTER_CHANNEL`, `0xffffff800171ea1a`, which re-sends the params after changing the
  master channel via `updateSyncParams`). GET of 36 bytes (tx of zeroed request + rx): in
  `getAWDL_SYNC_PARAMS` (`0xffffff800171b1b8`, site `0xffffff800171b2f5`),
  `getAWDL_MASTER_CHANNEL` (`0xffffff800171f1e6`, reinterprets the same fields) and
  `dumpSyncParams` (`0xffffff800171ac60`, for the debug dump). The rx is dumped into the same
  ivars and into struct `apple80211_awdl_sync_params`. Expected function: the firmware programs the
  availability window period, its length, the length of the extended portion and the
  cadence of the synchrony action frames `?`.
- **`awdl_opmode`** (string `0xffffff80017caec7`): the local synchrony role. SET of **28
  bytes** from `setAWDL_SYNC_STATE` (`0xffffff800171e164`, site `0xffffff800171e2ce`, async):
  `+0` u8 = mode (0 = slave, 1 = non-electoral master, 2 = master; mapping from
  `request+4` ∈ {1→2, 3→1, 2→0} and log `set awdl opmode %u %s ne_bcast…` with the literals
  MASTER/NE_MASTER/SLAVE of `0xffffff8001770a80..`), `+1..+2` u16 = `fMasterMode`
  (`ivars+668`; the "non election master" flag of type 116), `+3` u8 = `ne_bcast`
  (`request+0x10`), `+4` u32 and `+8` u16 from `request+8`/`+0xC` (request timestamps
  `?`), `+0xA` u8 additional from `request+0x14` `?`. GET (rx 28 B) at
  `getAWDL_SYNC_STATE` (`0xffffff800171b528`). Trigger: type 134
  (`AWDL_SYNC_STATE`, len 24). Expected function: fixes the chip's role in the synchrony; the
  confirmation comes back via `WLC_E_AWDL_ROLE` (97).
- **`awdl_election_tree`** (string `0xffffff80017cb196`): the election parameters. Four
  setters write ivars (`+560..+585`) and launch the iovar via async `sendVirtualIOVarSet`:
  `setAWDL_ELECTION_ID` (`0xffffff800171cace`, site `0xffffff800171cbef`: u16 id from
  `request+4` → `ivars+561`), `setAWDL_ELECTION_PARAMS` (`0xffffff800171c4ca`, site
  `0xffffff800171c596`: `request+16`→`+560` u8, `+18`→`+561` u16, `+20`→`+585` u8,
  `+22`→`+575` u32, `+26`→`+579` u16, `+28`→`+581` u32, `+32`→`+563` u32 metric),
  `setAWDL_ELECTION_METRIC` (`0xffffff800171cf76`, site `0xffffff800171d097`: u32 →
  `ivars+563`), `setAWDL_ELECTION_RSSI_THRESHOLDS` (`0xffffff800171d16a`, site
  `0xffffff800171d210`: bytes of `request+4/8/0xC/0x10` → `ivars+571/570/567/569`; init of
  those thresholds at `0xffffff8001718db0`: `ivars+567..571` = `0xC3 E9 C3 CE BA`,
  i.e. −61/−23/−61/−50/−70 dBm `?`). GETs: `getElectionParams` (`0xffffff800171c2fa`,
  struct `awdl_election_tree_info`), `getAWDL_ELECTION_RSSI_THRESHOLDS` (`0xffffff800171d2e4`),
  `getAWDL_ELECTION_METRIC` (`0xffffff800171cd70`), `getAWDL_ELECTION_ID` (`0xffffff800171c456`,
  which re-reads the ivars). The completion `handleElectionTreeIovarAsyncCallBack`
  (`0xffffff800171c6e0`) re-sends the iovar if the firmware returned an error. Triggers: types
  122/155/126/135. Expected function: the firmware runs the AWDL master election
  (preference, metric, RSSI thresholds) and notifies the result via `WLC_E_AWDL_ROLE`.
  In the modern path these same values are managed by the kernel `IO80211AWDLPeerManager`
  (`setAwdlElectionId/Metric/Params`, see
  [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §5).
- **`awdl_extcounts`** (string `0xffffff80017cb000`): SET of **4 bytes** from
  `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` (`0xffffff800171bfdc`, site
  `0xffffff800171c078`): `+0` = `request+0x10`, `+1` = `request+4`, `+2` = `request+8`,
  `+3` = `request+0xC` (u8 each; log `min %u maxuni %u maxmu…` = minimum, unicast/multicast
  AW extension maxima `?`). GET at `0xffffff800171be78`. Trigger: type 118
  (`AWDL_EXTENSION_STATE_MACHINE_PARAMETERS`, len 20). Expected function: how many availability
  window extensions the firmware admits per AW.
- **`awdl_chan_seq`** (string `0xffffff80017cd36f`): the channel hopping sequence. SET from
  `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` (`0xffffff8001724436`): validates
  `request[5]` (length) and `request[6] < 2` (encoding); if `request[4] & 0x30` it uses
  `buildChanSequenceCommand` (`0xffffff8001724840`, SDB request) and otherwise
  `buildAWDLChanSequence` (`0xffffff80017258bc`) which produces ≤ 160 bytes in a temporary buffer,
  hexdump `awdl_chan_seq:` and async `sendVirtualIOVarSet`. The full request (400 bytes,
  `apple80211_awdl_sync_channel_sequence`) is cached at `ivars+132` (memcpy of 0x190). GET at
  `0xffffff800172421a` (rx). Trigger: type 129 (len 400). Expected function: the firmware hops
  channel per slot according to the sequence (AWDL protocol channel sequence).
- **`awdl_osoc_chan`** (string `0xffffff80017cbbbe`): SET u32 from
  `setAWDL_SECONDARY_MASTER_CHANNEL` (`0xffffff800171f01e`, site `0xffffff800171f0a1`; value
  `request+4` → `ivars+692`; log `channel %lu secondary %lu` with `ivars+688/+692`); GET at
  `0xffffff800171ec10`. Trigger: type 205. Expected function: secondary master channel
  (synchrony fallback) `?`.
- **`awdl_phycal_period`** (string `0xffffff80017cdb96`): SET of 4 bytes (site
  `0xffffff8001728c0f`) / GET u32 in `setAWDL_TXCAL_PERIOD` (`0xffffff8001728b8c`) /
  `getAWDL_TXCAL_PERIOD` (`0xffffff8001728cae`). Trigger: type 226. Expected function:
  PHY calibration period outside the AW `?`.
- **`awdl_presencemode`** (string `0xffffff80017ca096`): SET u32 from
  `setAWDL_PRESENCE_MODE` (`0xffffff80017231d8`, site `0xffffff80017232b3`; value `request+4`,
  also to `ivars+664`; gates `ivars+840/+844`); GET at `0xffffff800171bd12`. It is also
  re-sent by `handleEvent` when receiving firmware event id 0 (site `0xffffff8001716815`).
  Triggers: type 136 and fw event 0. Expected function: presence mode of the local device
  in the sync frames `?`.

### 3.3 AWDL management/action frame TX

- **`awdl_af_hdr`** (string `0xffffff80017ccb7c`): SET of **10 bytes** at creation
  (`setAWDL_BSSID`, site `0xffffff8001721e68`): `FF FF FF FF FF FF 7F 00 17 F2` — broadcast
  destination + byte `0x7F` `?` + Apple OUI `00:17:F2`. Expected function: header (SNAP/da) that
  the firmware prepends to every AWDL action frame it transmits.
- **`awdl_af_rssi`** (string `0xffffff80017ccc2e`): SET s32 = −60 at creation (site
  `0xffffff8001721f82`). Expected function: RSSI threshold to accept RX action frames `?`.
- **`awdl_peer_op`** (string `0xffffff80017cd2dd`): the firmware peer cache. SET from
  `setPEER_CACHE_CONTROL` (`0xffffff80017235ce`, site `0xffffff8001723d6e`, async, completion
  `handleCacheControlAsyncCallBack`): variable-length descriptor — `+1` u8 opcode
  (0 = add/update, 1 = delete; request opcode at `+4`: 0/6 → 0, 2 → 1),
  `+2` u32 index/parameters (`request+8`), `+6` u16 (`request+12`), `+8` u8 flags
  (bit0 HT `?`, bit2 VHT `?`; computed from `request+44/+60` and the core state at
  `core+14372`), 32-byte HT block at `+9` if `request[16..17] == {0x2D, 0x1A}`,
  16-byte VHT IE at `+37` if `core+1024 ≥ 128 && request[45..47] == {≠0, 0xBF, 0x0C}`,
  fixed 24-byte HE IE at `+51` if HE and NSS = 1 (constants
  `0x012316FF`/`0x1F000D000E200400`/`0xFFFEFFFE000C000C` written at +0x33/+0x3B/+0x43, `setPEER_CACHE_CONTROL`, `0xffffff8001723887`–`0xffffff80017238a9`),
  and for add with `request[70] == 3` a final block associated with the channel sequence
  (`request[69]`, constants `0x08F20800`…, site `?`) `?` fine map.
  GET (dump) at `dumpPeerCache` (`0xffffff8001721038`, site `0xffffff80017210d0`).
  Trigger: type 143 (`PEER_CACHE_CONTROL`) and internal peer management. Expected function:
  the firmware keeps per peer the availability (chan seq, HT/VHT/HE capabilities) needed
  to schedule the TX of action frames and data within the common AW.
- **`awdl_advertisers`** (string `0xffffff80017cb256`): SET from `setAWDL_AWDL_ADVERTISERS`
  (`0xffffff800171c932`, 44-byte `awdl_peer_advert_add`, type 229); GET at
  `dumpAwdlPeers` (`0xffffff80017280c4`). Expected function: advertiser table the
  firmware uses for identity in the AFs `?`.
- **`awdl_aftxmode`** (string `0xffffff80017ca3e6`): SET u8 from `setAWDL_AF_TX_MODE`
  (`0xffffff80017199c4`, site `0xffffff8001719a38`) and from `setAfTxMode` (`0xffffff800171a75a`,
  site `0xffffff800171a822`, called by `doSetSyncState` after each enable); GET at
  `0xffffff8001719b62`. Trigger: type 208. Expected function: offload mode of the AF TX
  (normal/limited) `?`.
- **`awdl_oob_af`** (string `0xffffff80017cb6a9`): scheduled TX of a unicast action frame.
  SET from `setAWDL_OOB_REQUEST` (`0xffffff8001726474`; the helper `sendUnicastAFIOVar` is
  inline): requires `fSyncEnabled != 0` (`ivars+672`), checks `getMaxCmdTxPayload`, builds the
  **38-byte header** `{u32 request+22, u16 request+26, u32 request+4, u16 request+8,
  16 zero bytes, u16 request+20, u16 len(request+40) at +36}` + the user frame
  (`request+48`, `request+40` bytes), concatenates them into a buffer and enqueues (max. 16 in flight,
  counter `ivars+48`); completion `handleOobAfIovarAsyncCallBack` → posts the
  userspace event 69 (`AWDL_UNICAST_AF_STATUS`). Trigger: type 149 (`AWDL_OOB_REQUEST`).
  Expected function: the firmware transmits the frame within the next AW addressed to the peer `?`.
- **`awdl_oob_af_auto`** (string `0xffffff80017cd6cd`): SET via `sendIOVarSetCommand` from
  `setAWDL_OOB_AUTO_REQUEST` (`0xffffff8001727c0e`, site `0xffffff8001727cf2`); payload =
  similar header + frame (`?` fine map). Trigger: type 225. Expected function: automatic
  repetition of the frame by the firmware in every AW (permanent request instead of a single
  transmission) `?`.
- **`awdl_afs_pload`** (string `0xffffff80017cd747`): SET via `sendIOVarSetCommand` from
  `setAWDL_OOB_SEC_PAYLOAD_REQUEST` (`0xffffff8001727d56`, site `0xffffff8001727e16`).
  Trigger: type 342. Expected function: secondary payload attached to the AFs (secondary
  capabilities) `?`.
- **`awdl_payload`** (string `0xffffff80017cb7c3`): the sync frame template. SET via
  `sendIOVarSetCommand` from `setAWDL_SYNC_FRAME_TEMPLATE` (`0xffffff800171da80`, site
  `0xffffff800171db89`): iovecs `{u32 len (request+4), u16 0xAAAA}` + template from
  `request+8` (`?` the 0xAAAA mark). Trigger: type 151. Expected function: the firmware
  transmits that sync frame (PSF) periodically within the `awdl_sync_params` program.
- **`awdl_psf_dwell`** (string `0xffffff80017cb96f`): SET of **8 bytes** from `setPSF_DWELL`
  (`0xffffff800171dfc0`, site `0xffffff800171e05d`): `{u32 0, u16 request+4, u16 request+8}`.
  Trigger: type 418. Expected function: PSF dwell/persistence parameters `?`.
- **`awdl_min_rate`** (string `0xffffff80017cdb44`): SET u8 from `setAWDL_MIN_RATE`
  (`0xffffff8001728a48`). Trigger: type 213. Expected function: minimum transmission rate
  of the AWDL frames `?`.
- **`slot_bss`**: SET from `setP2P_COEX_PROTECTION` (`0xffffff800172a968`): 256-byte
  buffer with header `{u32 0x01018000 `?`, u16 3, u32 1, u8 2}` and two TLV blocks
  (length `request+4` + blob `request+5`, length `request+74` + blob `request+75`,
  u16 total lengths at the end). Expected function: BT protection/coexistence during the
  AWDL slots `?`.
- **`wsec_key`** and the **`WLC_SET_KEY`** ioctl: `setAWDL_ENCRYPTION_KEYS` (`0xffffff800171f480`)
  converts the key with `mapAppleKeyToBcomKey` and installs it with `programKeySync`/
  `programKeyAsync` (iovar `wsec_key`; the async uses `AppleBCMWLANJoinAdapter::programKeyAsync`
  `0xffffff80015f850c` / sync `0xffffff80015f8640`; failure = `WLC_SET_KEY() failed`,
  site `0xffffff800171f717`). Trigger: type 219 (`AWDL_ENCRYPTION_KEYS`, len 148).
  Expected function: encryption of the AWDL datapath (WPA2 with per-peer group key) `?`.
- **`wsec`**: SET/GET u32 in `setAWDL_ENCRYPTION_TYPE` (`0xffffff800171fa46`) /
  `getAWDL_ENCRYPTION_TYPE` (`0xffffff800171f900`). Trigger: type 220. Expected function:
  active cipher family `?`.

### 3.4 Miscellaneous configuration and state

- **`awdl_config`** (string `0xffffff80017cca86`): SET u32 from `setAWDL_CONFIG`
  (`0xffffff80017215e2`, two sites; log `send awdl_config (0x%x)`); the default value at
  creation is 0x73 (site `0xffffff8001721ef5`). Trigger: type 432. Expected function:
  bitmask of options of the AWDL engine (bit 0x73 enables the basic functions) `?`.
- **`awdl_stats`** (string `0xffffff80017caf10`): GET at `getAWDL_DEV_STATS`
  (`0xffffff800171b698`, buffer depending on fw version `?`); the fields are published as
  `apple80211_awdl_dev_stats` (type 247, len 124). The debug dump
  `dumpAwdlStats` (`0xffffff8001720de8`) names the firmware counters: afrx, aftx,
  datatx, datarx, txdrop, rxdrop, monrx, lostmaster, misalign, aws, aw_dur, debug, txsupr,
  afrxdrop, awdrop, noawchansw, rx80211, peeropdrop, psfchanswtchskip, psfstateupdskip —
  i.e., the firmware accounts for its scheduling hits and misses `?`.
- **`awdl_maxpeers`** (string `0xffffff80017cd0bd`): GET at `getPEER_CACHE_MAXIMUM_SIZE`
  (`0xffffff80017233b6`). Trigger: type 130. Expected function: maximum size of the
  firmware peer cache `?`.
- **`cca_get_stats`**: GET at `getAWDL_CCA` (`0xffffff800171ba8e`); returns the
  per-AWDL-channel channel occupancy statistics (struct `apple80211_awdl_cca`, type 338, len 392) `?`.
- **`awdl_dfsp_cfg`** (string `0xffffff80017cdc35`): SET from `setAWDL_DFSP_CONFIG`
  (`0xffffff8001728dd0`, site `0xffffff8001728eb5`; applies `request+12 |= 0xD000` when the
  sync is active); GET at `0xffffff8001728fba`. Trigger: type 262. Expected function:
  DFS proxy (BCSA/PCSA): the firmware manages the radar channels of the sequence `?`.
- **`awdl_dfsp_ucsa`** (string `0xffffff80017cdccd`): SET from `setAWDL_DFSP_UCSA_CONFIG`
  (`0xffffff800172919c`, site `0xffffff800172924b`). Trigger: type 263. UCSA variant of the
  DFS proxy `?`.
- **`hp2p`** (string `0xffffff80017a0903`; LLW): GET at `getLLW_PARAMS` (`0xffffff800172753e`,
  site `0xffffff8001727712`) and SET with subcommands in `setLLW_PARAMS` (`0xffffff8001726ac8`,
  completion `setLLW_PARAMSCallback` `0xffffff80017269d4`); both reject if the firmware does not
  support LLW (`FW does not support llw`). Trigger: type 344. Expected function: low-latency link
  accompanying AWDL `?`.
- **`txminrate`**: GET at `getTX_RATE` (`0xffffff8001720ade`); 12-byte SET
  `{u32 0x80000, u32 request+4, u16 request+8, u16 request+12}` in `setTX_RATE`
  (`0xffffff8001720c20`). Trigger: types `getTX_RATE` (no modern handler `?`) and 411.
- **`btc_flags`** and **`wlan.llw.awdlbssid`**: in `setAWDL_PEER_TRAFFIC_REGISTRATION`
  (`0xffffff800171fcaa`): depending on the registered service (`request+16`, length `request+12`:
  `CARPLAY_UUID`, `airplay`, `sidecar`, `ensemble`, `rtmode`, `wifid-assisted-discovery`,
  `airplay-connectivity-check`, `MPRemoteCamera`, `RemoteCamera`, `RemoteScreen`,
  `TVRemoteCamera`) it switches `btc_flags` (async) and, for fw ≤ 4, a command with
  `{9, active}` via `issueCommand` `?`; it mirrors the state into `ivars+100/102/103` and repeats the
  request to the low-latency companion interface (slot +0xE78 of the companion, `ivars+120`).
  Trigger: type 164 (`AWDL_PEER_TRAFFIC_REGISTRATION`, len 184). Expected function: the
  firmware prioritizes/suspends traffic according to the active AWDL service type `?`.
- **Core side**: `AppleBCMWLANCore` sends on its own `awdl_ranging_config`
  (string `0xffffff80017a881b`; `resetRangingState`, `0xffffff800168b854`, `runIOVarSet`
  primary) and `awdl_ftm_ranging_config` (string `0xffffff800179f0da`;
  `setRANGING_START_V3`, `0xffffff80016866e8`, async `sendVirtualIOVarSet` with completion).
  Triggers: ranging requests (types 241/242) and state reset `?`.
- **Boot-args read by the interface** (`init`, `0xffffff8001718db0`): `awdl_log_flags_config`
  (site `0xffffff80017190d6`; if ≥ 2, `ivars+720 = value−1`), `awdl_max_peers`
  (`ivars+908`, default −1), `awdl_low_latency_default_mapping` (`ivars+912`, default 1),
  `awdl_low_latency_tx_requeue` (`ivars+916`, default 0). Strings at
  `0xffffff80017ca2bd`..`0xffffff80017ca303`. These are kext-local configuration, not iovars.

## 4. Firmware events consumed by role 6

### 4.1 Framing and routing

- Format (parsing in `AppleBCMWLANCore::handleEventPacket`, `0xffffff800160cda6`): BDC header
  (its size is 4 × byte@+3), bcmeth header (3-byte OUI compared at `+23`
  against `0xffffff800176ecbc` and u16 subtype at `+26` with `(v & 0xFBFF) == 0x100`; `0x500`
  (1280) diverts to `handleDongleEventPacket`), and next a **48-byte
  `wl_event_msg_t`** (Apple variant): `+0` u16 len, `+2` u16 bsscfgidx, `+4` u32 `event_type` (LE),
  `+8` status, `+12` reason, `+16` auth_type, `+20` u32 datalen (BE→LE), `+24` source MAC,
  `+30` ifname[16], `+46` u8 **ifidx**, `+47` u8 bssidx; the event data at `+48`.
  Byte-swap of the multi-byte fields at `0xffffff800160d055`–`0xffffff800160d165`. `event_type ≥ 0xDD` →
  "Received garbage event, ignore" (comparison at `0xffffff800160d172`, log at `0xffffff800160d1bc`, log line 15344).
- Routing: if the Proxi interface id (`core+11336`) matches, every event goes to
  `handleEvent` (via `AppleBCMWLANCore::isEventForInterface`, `0xffffff80016367be`: compares the
  ifidx byte `+46` with `IO80211SkywalkInterface::getInterfaceId`; special case ifidx == 1 and
  `event_type == 54`). Additionally the following are delivered to Proxi **independently of the ifidx**: id 26
  (site `0xffffff800160de32`, next to `ScanAdapter::scanComplete` and `AppleBCMWLANNANInterface::
  handleEvent`), id 98 (`case 0x62` branch → `LABEL_92`), id 80 (`case 0x50` → next to
  `AppleBCMWLANCore::csaReceived`) and id 75 (`case 0x4B` → `handleActionFrame_rx` directly),
  sites `0xffffff800160d2c5`/`0xffffff800160de32`/`0xffffff800160df19`.
- Canonical names: `kEventBitMapping` (`0xffffff80017ed070`, 208 entries of 16 bytes
  {u32 id, char* name}) consumed by `convertEventToString` (`0xffffff80016bb3d3`).
  Relevant ids: 26 `WLC_E_SCAN_COMPLETE`, 54 `WLC_E_P2P_DISC_LISTEN_COMPLETE`,
  59 `WLC_E_ACTION_FRAME`, 60 `WLC_E_ACTION_FRAME_COMPLETE`, 69 `WLC_E_ESCAN_RESULT`,
  75 `WLC_E_ACTION_FRAME_RX`, 80 `WLC_E_CSA_COMPLETE_IND`, 96 `WLC_E_AWDL_AW`,
  97 `WLC_E_AWDL_ROLE`, 98 `WLC_E_AWDL_EVENT`, 109 `WLC_E_PROXD`, 150 `WLC_E_TWT_SETUP` (so
  called in the table; the one used as "ranging auth" in `handleEvent` `?` divergence from the public
  enum), 152 `WLC_E_NAN_NON_CRITICAL` (same `?`), 169 `WLC_E_SLOTTED_BSS_PEER_OP`.

### 4.2 What `handleEvent` (`0xffffff8001715ece`) does with each id

| id | name (`kEventBitMapping`) | handling in the kext |
|---:|---|---|
| 0 | `WLC_E_SET_SSID` (public enum: `WLC_E_NONE` `?`) | re-sends the `awdl_presencemode` iovar (site `0xffffff8001716815`) and clears `ivars+844` |
| 16 | `WLC_E_LINK` | counters `ivars+900/+904` and link state at `ivars+179` according to bit 0 of `cursor+2` |
| 26 | `WLC_E_SCAN_COMPLETE` | ignored (LABEL_146); the core already consumed it |
| 54 | `WLC_E_P2P_DISC_LISTEN_COMPLETE` | status ≤ 4 → log "OCL_COREDOWN"; if the payload is ≥ 5 B it processes it `?` |
| 59 | `WLC_E_ACTION_FRAME` | log only |
| 60 | `WLC_E_ACTION_FRAME_COMPLETE` | OOB AF TX timestamp (`mach_continuous_time`) and low-latency config `?` |
| 69 | `WLC_E_ESCAN_RESULT` | counter `ivars+84`; if `ivars+88 & 1`, `ScanAdapter::eventScanComplete` |
| 75 | `WLC_E_ACTION_FRAME_RX` | `handleActionFrame_rx` (`0xffffff800171795c`): rebuilds the frame + 72 B header and delivers it via BPF (DLT 0xC) — see [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §1.3 |
| 80 | `WLC_E_CSA_COMPLETE_IND` | posts userspace event 94 (`AWDL_DFS_CSA_COMPLETE`, site `0xffffff8001715fd4`) |
| 96 | `WLC_E_AWDL_AW` | with payload ≥ 28 B and sync active: `handleAvailabilityWindowStart/Extension/End` (`0xffffff8001718756`/`0xffffff8001718942`/`0xffffff80017184b6`) → userspace events 41/42/43; the fw payload carries u32@+0, u16@+8/+10, u8@+12 (channel), u32@+22 `?` |
| 97 | `WLC_E_AWDL_ROLE` | u32 status of the event (`cursor+8`) → `ivars+676`; 0 → `handleSlaveEvent` (`0xffffff8001718ad6`), 1 → `handleNonElectionMasterEvent` (`0xffffff8001718bd4`), 2 → `handleMasterEvent` (`0xffffff80017189e4`); all three post the userspace event 46 (`AWDL_SYNC_STATE_CHANGED`) |
| 98 | `WLC_E_AWDL_EVENT` | sub-dispatch by the payload id (TABLE of [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §3.1): 98.0 → events 125/126 (scan), 98.2/3 → 64 (TXCALIBRATION_REQUEST), 98.9 → 69 (UNICAST_AF_STATUS), 98.0x12 → 88 (DFS_CSA), 98.0x13/0x14 → 183 (DFSP_SUSPEND_RESUME), 98.0x15 → 204 (DWELL_START) |
| 109 | `WLC_E_PROXD` | proximity/ranging handling `?` |
| 150 | (table: `WLC_E_TWT_SETUP`; real use: ranging `?`) | posts userspace event 84 (`RANGING_AUTHENTICATION_DONE`, u32 from the fw) |
| 152 | (table: `WLC_E_NAN_NON_CRITICAL`; real use: SDB `?`) | posts userspace event 135 (`AWDL_SDB_TRANSITION`, u32 map of `fw+8`/`fw+12`) |

The firmware, therefore, is the one that detects the START of each availability window (96), the one that
decides the election role (97) and the one that multiplexes the internal AWDL events (98); the kext only
translates them into `apple80211` events for wifip2pd/WCL.

## 5. Observed split of obligations

**What the Broadcom firmware does by itself** (and for which the kext only sees events/config):

1. Keeping the AWDL instance on the chip (`interface_create`/`awdl_if`) with its own bsscfg.
2. Running the **master election** with the thresholds of `awdl_election_tree` and publishing the
   role via `WLC_E_AWDL_ROLE` (97).
3. Sustaining the **temporal synchrony**: availability window program of `awdl_sync_params`
   (period, length, extension, AF cadence), extensions with `awdl_extcounts`, and emission
   of `WLC_E_AWDL_AW` (96) when each window opens.
4. **Transmitting the sync frames** with the `awdl_payload` template, `awdl_psf_dwell` dwell,
   `awdl_af_hdr` header and `awdl_presencemode` presence.
5. **Hopping channel** according to `awdl_chan_seq` (and its SDB variant), with DFS proxy
   `awdl_dfsp_cfg`/`awdl_dfsp_ucsa`.
6. **Scheduling the per-peer TX of action frames and data** with the `awdl_peer_op` cache
   (availability + capabilities per peer), `awdl_advertisers` and `awdl_aftxmode`, plus the
   one-off OOB sends (`awdl_oob_af`, `awdl_oob_af_auto`, `awdl_afs_pload`).
7. **Filtering RX** by RSSI (`awdl_af_rssi`) and delivering the received AFs via
   `WLC_E_ACTION_FRAME_RX` (75).
8. Measuring and accounting: CCA (`cca_get_stats`), internal statistics (`awdl_stats`), peer
   limit (`awdl_maxpeers`), ranging (`awdl_ranging_config`, `awdl_ftm_ranging_config`,
   `WLC_E_PROXD`).

## 6. Classification for a kext without AWDL firmware (RTL8814)

### 6.1 (a) Pure configuration: store and return

Requests whose Broadcom implementation is essentially an ivar store + (optionally) an
echo iovar, with no temporal logic: SSID/STATE/BSSID/CHANNEL (trivial getters,
`0xffffff800171a702`–`0xffffff800171a752`), `AWDL_DEVICE_CAPABILITIES`
(`0xffffff800171be38`, constants), `AWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES`
(`0xffffff800171ed38`), `AWDL_MIN_RATE` (`0xffffff8001728a3c`/`0xffffff8001728a48`),
`AWDL_TXCAL_PERIOD` (`0xffffff8001728cae`/`0xffffff8001728b8c`), `AWDL_DFSP_CONFIG/UCSA`
(`0xffffff8001728fba`/`0xffffff8001728dd0`/`0xffffff800172919c`),
`AWDL_PRESENCE_MODE` (`0xffffff800171bd12`/`0xffffff80017231d8`),
`AWDL_AF_TX_MODE` (`0xffffff8001719b62`/`0xffffff80017199c4`),
`AWDL_ENCRYPTION_TYPE/KEYS` (`0xffffff800171f900`/`0xffffff800171fa46`/`0xffffff800171f480`),
`AWDL_CONFIG` (`0xffffff80017215e2`), `PEER_CACHE_MAXIMUM_SIZE` (`0xffffff80017233b6`),
`AWDL_SECONDARY_MASTER_CHANNEL` (`0xffffff800171ec10`/`0xffffff800171f01e`),
`LLW_PARAMS` (`0xffffff800172753e`/`0xffffff8001726ac8`) and the debug dumps (`dumpSyncParams`,
`dumpPeerCache`, `dumpAwdlPeers`, `dumpAwdlStats`, `dump`, `logElectionTree`). In the port, these
slots can be limited to storing the value and answering; the real effect of each is applied in
(b)/(c).

### 6.2 (b) Periodic timing the kext must run

The firmware runs three periodic machines a port must reimplement in software:

1. **Availability windows**: period `aw_period` (u16, `awdl_sync_params+8`), length
   `aw_len` (+0x10), extension `aw_ext_len` (+0xE), action frame period `adv_period`
   (+0xA), lifetime (+6, init 6). Boot values of the kext (`init`,
   `0xffffff8001718db0`): `ivars+688 = 6` (lifetime), `ivars+728 = 200` (aw_period; the init log
   announces "guard time 20" for this ivar — ×10 factor, units `?`),
   `ivars+740 = 100` (adv_period), `ivars+696 = 4` (maxNoMasterPeriod; log "4"),
   `ivars+700 = 20`, `ivars+676 = 4`, `ivars+664 = 1`, `ivars+668 = 90`; the real values
   are programmed by userspace with `AWDL_SYNC_PARAMS` (type 116, 24 bytes: aw_period, aw_len,
   aw_ext_len, adv_period, non-election-master flag). The guard time as a separate request is the
   type 124 (`AWDL_GUARD_TIME`, 8 bytes) — no Broadcom slot `?`.
2. **Master election**: election id (u16), metric (u32) and RSSI thresholds (init
   −61/−23/−61/−50/−70 dBm `?`) — in a port without firmware, either software runs it (the
   machine of `IO80211AWDLPeerManager` in the modern path) or it is set manually; the resulting role must
   feed the same states that `handleEvent` observes via event 97.
3. **Channel hopping and PSF**: the sequence (`awdl_chan_seq`, up to 400 B of request, ≤ 160 B
   encoded to the firmware, cached at `ivars+132`) must run with its own timer,
   the same as the periodic transmission of the sync frame (`awdl_payload` + `awdl_psf_dwell`).
   Explicit tolerances in the binary: there are no timing tolerance constants besides
   the wait timeouts of the chip interface creation/deletion (3000 ms / 1000 ms,
   `waitForDriverEvent`, `0xffffff8001715c62`) and the queue of 16 asynchronous iovars in flight.

### 6.3 (c) Per-frame TX/RX work

What is not covered by [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) §9/§10 (flow
queues, SNAP, duplicated multicast, RX drain) and here is the firmware's responsibility:

- Prepending the AF header (`awdl_af_hdr`, `FF×6 7F 00 17 F2`) to every action frame TX and
  filtering the RX by RSSI (`awdl_af_rssi`).
- Keeping the per-peer cache (`awdl_peer_op`: add/update/delete opcode, HT/VHT/HE
  capabilities, per-peer chan seq) to decide when a frame addressed to a peer is
  transmittable within the shared AW; the firmware counters (`peeropdrop`,
  `noawchansw`, `psfchanswtchskip`) show that per-window filtering is its own.
- Scheduling the one-off OOB sends (`awdl_oob_af`: 38 B header + frame, with
  confirmation via event 60/69) and the automatic transmissions (`awdl_oob_af_auto`,
  `awdl_afs_pload`).
- Encrypting the datapath with the group key (`wsec_key`/`wsec`, types 219/220).

On the RTL8814AU these four functions are logic of the kext TX queue (consult the current
window before dequeuing a frame, drop/hold according to the peer, build the SNAP header
locally and encrypt in software or on the chip according to capability).

## 7. Method and limits

- String scan `awdl*` + xrefs, callers of `runVirtualIOVarSet/Get`, decompilation of
  ~90 functions and disassembly of the send sites (probes `a_scan.py`, `b_decomp.py`,
  `c_probe.py`, `d_probe.py`, `e_tbl.py`, `f_probe.py` in
  `Build/ida_awdl/task_awdl_fw_duties/`; outputs `a_out.txt`, `b_out1/2.txt`, `c_out.txt`,
  `c_windows_digest.txt`, `d_out.txt`, `e_out.txt`, `f_out.txt`).
- Event table read from `kEventBitMapping` (208 entries verified one by one, id by id).
- Payloads: offsets taken from the disassembly of the `send*`/`run*` sites (Hex-Rays
  calls hide the name/payload arguments); those marked `?` are fields whose fine
  byte-by-byte map was not fully reconstructed.
- Main `?`: the unit of the guard time (×10 factor observed, unconfirmed), the fine map of
  `awdl_peer_op` (final HE/chan-seq blocks), the payload of `interface_remove` (fw ≥ 5), the rx of
  `awdl_stats`/`cca_get_stats` depending on the fw version, the semantics of ids 150/152 versus the
  public enum (`kEventBitMapping` says `WLC_E_TWT_SETUP`/`WLC_E_ESCAN_RESULT`-adjacent but
  `handleEvent` uses them for ranging/SDB), the inside of `issueCommand` down to the PCIe bus
  rings, and the exact site of `interface_create` within `setAWDL_BSSID`.
- None of the conclusions implies a test on RTL8814 hardware.
