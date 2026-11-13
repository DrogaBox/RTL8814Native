# The AWDL channel sequence with associated infrastructure: the en2 case on 153 (80 MHz, center 155)

Which infrastructure channel `IO80211AWDLPeerManager` learns, from which driver request it obtains it,
how it mixes it with the social channels when building the 16 slots it brings down with ioctl 129
(`setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`) and what changes with a peer present. The reference
scenario is the test machine: `en2` associated to a 5 GHz BSS with primary 153, width 80 MHz,
center 155, so that the 80 MHz block 149–161 contains social channel 149
(`AWDL_WINDOW_TIMER.md` §8). Complements `AWDL_CHANNEL_SEQUENCE_CONTROL.md` (sequence machine)
and `AWDL_CHANNEL_STRATEGY.md` (strategy and fields); corrects two points of both (§11).

Source: kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0), copy of the annotated IDB
`~/re_cache/ida/boot_awdl.kc.i64` in `~/re_cache/ida/task_seq_infra/`, decompilation and disassembly of
the cited functions, plus a global sweep of `E8/E9` calls and of vtable cells for the
points declared dead. The VAs are instruction or data addresses of that IDB. The ivar offsets of the
AWDL manager refer to `*(this+0x20)` and those of the base object to `*(this+0x18)` (from now on `pm`).
Unverified items are marked `?`.

## 1. Summary

With infrastructure on 153/80 and no peers, the manager brings down with the 129 a 16-slot sequence
where **two different 20 MHz primaries of the same 80 MHz block coexist**: social 149 (with
flags `0x10`, 5 GHz at 20 MHz) in seven slots and infrastructure 153 (with flags `0x410`,
5 GHz at 80 MHz) in up to four, plus a slot of 6 (flags `0x8`) and the rest at channel 0. The 149 is not
derived from the 153: it is the default value of `initParamsToDefaults` validated against the list of
channels the driver answers to request 207; the 153 is, verbatim, what the driver answers to the
GET of channel (type 4, `getCHANNEL`). The infrastructure slots are only filled if additionally there is at
least one registered AirPlay service (`addInfraToMinimumChannelSequence`); without services, the infra
slots go to channel 0. With a peer present the project's own sequence is replaced by the peer's
(`followPeerChannelSequence`) and stops depending on the infrastructure.

## 2. Objects and structures

| Data | Value |
|---|---|
| `apple80211_channel` | 12 B `{u32 version; u32 channel; u32 flags}` |
| `apple80211_channel_data` | 16 B `{u32 version; apple80211_channel}` |
| Slot entry (129 and ivar) | 12 B `{u32 version; u32 channel; u32 flags}` at `awdlIvars+0xA774` (+42868), flags at `+0xA77C` |
| `_zeroChanInfo` | `0xffffff8002488a88` = `{1, 0, 0}` (unserved slot) |
| `_channel6Info` | `0xffffff80023aaff0` = `{1, 6, 0xA}` (0x2 = 20 MHz, 0x8 = 2.4 GHz) |
| Primary master info | `awdlIvars+0x42A0/0x42A4/0x42A8` = `{version, channel, flags}` |
| Secondary master info | `awdlIvars+0x42AC/0x42B0/0x42B4` |
| Infrastructure channel in `pm` | version `pm+0x9BC`, channel `pm+0x9C0` (+2496), flags `pm+0x9C4` (+2500) |
| Raw social channels | `+0x4290` (2G, +17040), `+0x4294` (5G, +17044), `+0x4298` (5G alt, +17048) |
| Social mask | `+0x429C` (u16): bit0 = 6, bit1 = 44, bit2 = 149 (`selectChannelsToUseForAWDL`) |
| Calendar state | `awdlIvars+0x28E4` (+10468); strategy at `awdlIvars+4` |

Channel flags observed on 25G241: **0x8 = 2.4 GHz band, 0x10 = 5 GHz band, 0x2000 =
6 GHz** (construction in `selectChannelsToUseForAWDL` `0xffffff80021dc303`: channel < 15 → 8; channel
36..183 → `16 * ((chan-36) < 0x82)`; and 6 GHz guard per slot in `setChannelSequenceByState`,
byte `+0xA77D` bit `0x20` = bit `0x2000` of the dword). The width is encoded by the bits the
log of `setInfraChannelSpec` decodes (`0xffffff80021fc626`): `0x800` = 160 MHz, `0x400` = 80 MHz, `0x4` =
40 MHz, rest 20 MHz (0x2). Both conventions match those the RTL layer uses:
`Source/AirLayer/RTLAirLayer.cpp:264` (`chanFlags`: 5G band → 0x10, 2.4G → 0x8; width 80 → 0x400,
40 → 0x4, rest 0x2). For en2 on 153/80 the channel is `{1, 153, 0x410}`.

## 3. Where the infrastructure channel comes from: pull model

There is no route pushing the channel to the manager: `getInfrastructureState` **queries** it from the
driver every time the manager learns of something.

### 3.1 `getInfrastructureState` (`0xffffff80021da84a`–`0x21dae6d`)

Verified callers: `interfaceStateChange` (`0xffffff80021d9030`) and `reportDataPathEvents`
(`0xffffff80021de938`). Before calling it, `interfaceStateChange` saves the previous pair
(pm+0x9BC/0x9C4) to compare (`0xffffff80021d9013`, `0x21d901a`).

Steps verified in the function:

1. `IO80211Controller::getInfraChannel(apple80211_channel_data *)` (`0xffffff800229520e`, called at
   `0x21da898`). The overload does `safeMetaCast(*(ctrl+0x120)+0x188, IO80211SkywalkInterface)` and:
   - `IO80211Glue::sendIOUCToWcl(glue, 0xC02869C9, 4, out, 0x10, …)` — **request 4 =
     `APPLE80211_IOC_CHANNEL`**, 16 B, over the IOUC to the WCL of the primary interface;
   - on failure (or `0xE00002C7`), direct fallback to the bridge `apple80211getCHANNEL(interface)`, which is
     the `getCHANNEL` slot of the driver. In the RTL stack `RTLAirInterface::getCHANNEL` answers
     (`Source/AirLayer/RTLAirLayer.cpp:928`).
   The log of the family calls it by name: `"Get of APPLE80211_IOC_CHANNEL failed"`.
2. If the channel ≠ 0: `getPrimarySkywalkInterface` (cell +0xC80 of the controller, call at
   `0x21da980`) and `IO80211Controller::getBSSIDData(ctrl, iface, out)` (`0xffffff8002294e5a`,
   call at `0x21da98f`): `sendIOUCToWcl(0xC02869C9, 9, out, 0xC, …)` — **request 9 =
   `APPLE80211_IOC_BSSID`** — with fallback `apple80211getBSSID`. BSSID to `pm+0x930`.
3. `IO80211Controller::getSSIDData` (`0xffffff8002294c0c`, call at `0x21da9e5`):
   `sendIOUCToWcl(0xC02869C9, 1, out, 0x28, …)` — **request 1 = `APPLE80211_IOC_SSID`** — with
   fallback `apple80211getSSID`. Length to `pm+0x938` and bytes via
   `IO80211PeerManager::setInfraSsidBytes` (`0xffffff800225ee3a`, call at `0x21daa78`).
4. `IO80211Controller::getBeaconPeriod(apple80211_beacon_period_data *)` (`0xffffff8002294f60`,
   call at `0x21dab95`; IOUC `0xC02869C9` type `0xA6` = 166, 8 B, fallback
   `apple80211getBEACON_PERIOD`) → `pm+0x54C`; `getDTIMInt` (`0xffffff800229501e`, call at
   `0x21dabb9`, fallback `apple80211getDTIM_INT`) → `pm+0x550` = dtim · beacon.
5. Comparison with the stored one: if the channel changes **or the band bits `0x2018` change** of
   the flags dword (2.4/5/6 GHz), it reprograms `pm+0x9BC` = new `{version, channel}` and `pm+0x9C4` =
   new flags, with log "infra channel %u(0x%X) -> %u(0x%X)"; it stores the channel for the
   restoration at `awdlIvars+0x40F5` (bit) and `+0x40F8`. Returns 1 if there is infrastructure, 0 if not
   ("lost infrastructure link").

In short: **the infrastructure channel of the manager is exactly what the driver answers to the GET
of channel of the primary interface**, with the flags unmodified. For the RTL8814 on 153/80:
`getCHANNEL` answers `{1, 1, 153, 0x410}` and the manager stores channel 153, flags `0x410`.

### 3.2 Who triggers the query (link events)

`interfaceStateChange(StateChangedSource)` (`0xffffff80021d8cc8`) is slot 55 (`+0x1B8`) of the
vtable of `IO80211AWDLPeerManager` (vptr `0xffffff800245a868`, cell `0xffffff800245aa20`). The
names of the `source`s are at `0xffffff800245d450`; those used in this chain: 1 `BSSIDChanged`,
5 `Interface`, 6 `Reset`, 8 `Channel Switch Complete`, **9 `SetInfraChannel`**, 10 `Master Channel
Changed`, 11 `Secondary Master Channel Changed`.

- `reportDataPathEvents` (`0xffffff80021dd8f4`, the consumer of the event pipe of the driver) calls
  `interfaceStateChange` with source **9** (`0x21de14c`, `0x21df49f`), **11** (`0x21de77c`), **5**
  (`0x21de7fb`) and **6** (`0x21df883`), and calls `getInfrastructureState` directly at
  `0x21de938`. The association/roam/BSS-change events of the driver arrive thus.
- The other verified callers of slot 55 are internal of the manager: `doEnable`
  (`0x21c53d6`), `disableAWDL` (`0x21c7696`), `setAwdlCtl` (`0x21d0124`),
  `exitAwdlSuspendedMode`, `addPeerToRealTimeGroup`, `updateLocalRequests`, `setAwdlSyncEnabled`,
  `setAwdlSocialTimeSlots`, `setAwdlMasterChannel` (source 10) and `setAwdlSecondaryMasterChannel`
  (source 11); `IO80211PeerManager::monitorCountryCode` also invokes it.

### 3.3 Dead entries on 25G241

The four functions that "push" the infrastructure channel exist with complete bodies but have
neither a single direct call (global sweep of `E8/E9`) nor a vtable cell pointing at them
(sweep of raw pointers over the whole kernelcache; the manager vtable, materialized between
`0xffffff800245a868` and slot 100, does not contain them):

| Function | VA | What it would do |
|---|---|---|
| `IO80211PeerManager::setInfraChannel(apple80211_channel *)` | `0xffffff8002254aee` | notifies source 9 (cell +440) and stores channel/flags at pm+0x9C0/0x9C4 |
| `IO80211PeerManager::setInfraChannelInfo(apple80211_channel *)` | `0xffffff800225edec` | stores pm+0x9BC (qword) and pm+0x9C4 |
| `IO80211AWDLPeerManager::setInfraChannelSpec(uint, apple80211_channel_data *)` | `0xffffff80021fc626` | if there was no channel, takes it from the argument or from the channel GET; decodes the width in the log |
| `IO80211VirtualInterface::setInfraChannel(apple80211_channel *)` | `0xffffff8002330a0c` | notifies source 9 to the manager of the virtual interface and stores |

Consequence: the live refresh of the infrastructure channel is exclusively the pull of §3.1. The
detail of `setInfraChannelSpec` keeps specification value (its log fixes the semantics of the
bits `0x800/0x400/0x4` of the width), but it is not part of the 25G241 path.

## 4. Strategy and master buffers (`+0x42A0..0x42B4`)

### 4.1 Strategy choice

- `configure` (`0xffffff80021c54ea`) sets, in this order: `chanSwitchCapable` (byte `+0x3FA1`) = 1
  unconditional (`0xffffff80021c5b5c`; the only writer in the whole kernelcache), strategy 1
  (`AwdlStateMachine`, reason 0, force) at `0x21c5b75` and `airplaySoloModeEnabled` (`+0xA6A4`) = 1 at
  `0x21c5b7e`.
- `interfaceStateChange` (branch `0x21d8f76`–`0x21d900a`): only with `airplaySink` (`+0xA648`) ≠ 0 does it ask for a
  strategy — 0 (`AwdlFollowsInfra`) if bit 0 of `+0x3FA1` is 0; 1 if it is 1 and additionally
  `airplaySoloModeEnabled ≠ 0` or `nonAirPlayServices` (`+0xA65C`) ≠ 0 (call at `0x21d900a`).
- `monitorAWDLState` (`0xffffff80021ed724`) returns to 0 (reason 5) at `0x21ee3ae` under the five
  conditions of `AWDL_CHANNEL_STRATEGY.md` §6.
- Correction with respect to `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.1: `enableAWDL`
  (`0xffffff80021dcf58`) **does not** call `setChannelSequenceStrategy` (0 occurrences in its body);
  the only 0/1 selection lives in the two previous functions.

On a machine without AirPlay sink (the RTL8814 case with wifip2pd `?`) the strategy stays at 1
permanently.

### 4.2 Validation of the social channels: `configureValidAWDLChannels` (`0xffffff80021caa40`)

Called from `configure` (`0xffffff80021c5b9b`) and from `interfaceStateChange`
(`0xffffff80021d9996`), right before `updatePref6GChannel` (`0x21d9a2f`),
`findCommon6GChannel` (`0x21d9a37`) and `selectChannelsToUseForAWDL` (`0x21d9a3f`). Steps:

1. Country: `IO80211Controller::getCountryCode` (`0xffffff8002295416`) if not cached at
   `pm+0x9A0/0x9A4`; cache flag at `pm+0x6F8`.
2. Dual-band gate: bit `0x20` of `*(ctrl+0x120)+0xB37` (`0xffffff80021cab1e`). It is the
   capability map of the card: `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800229a2ca`) reads
   bit `n` at `*(ctrl+0x120) + 0xB34 + n/8`, and `+0xB34` corresponds to `p[4]` of the
   `apple80211_capability_data` returned by `getCARD_CAPABILITIES`; `+0xB37` bit `0x20` is,
   then, `p[7]` bit `0x20` (capability 29). `AppleBCMWLANCore::getCARD_CAPABILITIES` sets it
   when its feature flag 28 is active. Without it, `configureValidAWDLChannels` writes 0 into the qword
   `+0x4294` (5 GHz socials and alternate to 0) with the log "Dual band not supported, so assuming
   2.4GHz is the basic and should be supported, initialize 5GHz channels to zero", and there is no
   5 GHz social channel: only 6 in the sequence.
3. Asks the driver for the **list of non-DFS channels of 40 MHz or more**: `ioctl_internal` with token
   `0xC02869C9` and request **207** (`0xCF`; **379** if byte `pm+0x72E` marks 6 GHz), response of
   10016 B at `awdlIvars+0x69F4`, copied to `+0x42D4`; `numChanSpecs` (u16) at `+0x42E0`. If the
   request fails, it logs "Unable to get non DFS 40MHz channels ret %d" and **keeps** the values
   of `initParamsToDefaults` (149 and 44); if the channel count is invalid, it sets both to 0
   ("cannot use 5G").
4. Validates with `isValidChannel(apple80211_channels_info *, chan, band, mask, uint *out)`
   (`0xffffff80021d1d4c`): requires band 8 for 2.4 GHz and 16 for 5 GHz (36..183); rejects 149 in
   ETSI countries (table `ETSICountryList`, 39 entries) and channels ≥ 65 in country `IL` (byte
   comparison 'I'/'L'); it walks the list of the driver and
   returns into `out` the capability bits of the entry: **`0x1` radarDFS, `0x2` passive,
   `0x4` indoorRestricted, `0x8` supports 40 MHz, `0x10` supports 80 MHz, `0x20` supports 160 MHz,
   `0x100` restricted CLM**; with mask ≠ 0 it requires all the marked bits to be present.
5. With the list: `+0x4290` is validated with band 8 (on failure, log "...not a valid channel defaulting
   to 6"; no store of the 6 has been seen on 25G241 `?` — the value is already 6 by default); `+0x4294` with
   band 16 and, on failure, it tries 149: valid → `+0x4294 = 149` (`0x21caf85`), invalid → 0
   (`0x21caf91`, "Disallowing Channel 149, Country Code"); `+0x4298` with band 16 and, on failure, it
   tries 44: valid → `+0x4298 = 44` (`0x21cb097`), invalid → 0 (`0x21cb0a3`).
6. If the valid 5 GHz social has capabilities `& 0x38` (40/80/160) and `+0xAA74 ≠ 1`
   (`dynSdbForceChanSeq`), it sets the encoding of the 129 to **1** at `+0xA772`.
7. Infrastructure channel: if `pm+0x9C0 ≠ 0` and it is not 6 GHz (`0x2000`), first
   `isValidChannel(list, channel, flags & 0x18, 1)` — the `1` requires radarDFS: if it passes,
   `+0x3ED8` = 1 (**DFS channel**, `0x21cb0fd`); if not, retries with `flags & 0x2018` and mask 0: if
   that also fails, **`+0x3ED9` = 0** (channel not allowed for AWDL, `0x21cb24e`). The default state is
   set by the word store `mov word [x+0x3ED8], 0x100` at `0x21cb0ad`: DFS bit at 0 and
   **"allowed" bit (`+0x3ED9`) at 1**.

The starting values are set by `initParamsToDefaults` (`0xffffff80021c7a40`): qword
`0x9500000006` at `0x21c7ae4` (i.e. `+0x4290 = 6`, `+0x4294 = 149`) and `+0x4298 = 44` at
`0x21c7aeb`. For a non-ETSI country with a dual-band driver, on 153/80 nothing changes: socials
**6 / 149 / 44**, and the 153 remains allowed and non-DFS.

### 4.3 `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`): the master buffers

Called from `interfaceStateChange` (`0x21d9a3f`) after `updatePref6GChannel` (`0x21d9a2f`) and
`findCommon6GChannel` (`0x21d9a37`):

- Primary master info `+0x42A0/0x42A4/0x42A8` = `{1, (u8)+0x4290, band}` — with the default
  values, `{1, 6, 0x8}`.
- Secondary master info `+0x42AC/0x42B0/0x42B4` = `{1, +0x4294 (or +0x4298 if that is 0), band}`,
  or `_zeroChanInfo` if both are 0 — in the scenario, `{1, 149, 0x10}`.
- Mask `+0x429C`: bit0 (channel 6) whenever there is a master; bit1 if the secondary is 44; bit2 if
  `+0x4294 == 149`. In the scenario: `0x5`.
- The master infos carry **only the band, without a width bit**: the social is announced at 20 MHz.
- It re-arms the current AWDL channel `+0x1A0/0x1A8` to the secondary if the current one no longer matches
  (`0x21dc4a0`–`0x21dc4f5`): seed `{1, 149, 0x10}`.

`setAwdlMasterChannel` (`0xffffff800221d4de`) and `setAwdlSecondaryMasterChannel`
(`0xffffff800221d314`) allow userspace to rewrite `+0x4290`/`+0x4294`; with `awdlIvars+0x300 == 1`
they additionally forward the 132/205 to the driver (`apple80211RouteInternal(0x802869C8, 132, …)`) and only adopt
the value if the driver accepts.

### 4.4 `AwdlFollowsInfra` mode (strategy 0)

Two sites copy the infrastructure channel **on top of** the primary master info:

- `monitorAWDLState`, after the transition to 0 (`0x21ee3b3`–`0x21ee454`): with channel ≠ 0,
  `infraAllowedForNonRealtimeCases()` true, no assisted discovery (`+0xFA3A` bit 0) and no
  6 GHz, it copies pm+0x9BC → `+0x42A0`, pm+0x9C4 → `+0x42A8`, `_zeroChanInfo` → `+0x42AC`, 0 →
  `+0x42B4`; calls `programMasterChannels` (`0x21ee433`) and `setScheduleState(current state,
  0x14, true)`. If anything fails: "Disabling AWDL in FollowsInfra mode because …" and `disableAWDL`.
- `interfaceStateChange`, after `selectChannelsToUseForAWDL`, if the strategy is 0 (block
  `0x21d9a44`–`0x21d9aa8`): the same copy.

Effect: in mode 0 the "social channel" becomes the infrastructure channel itself (153/80) and the
132 programs it so; types 2/3 of `setChannelSequenceByState` inherit that value. This mode requires
the AirPlay scenario of §4.1, so it is not the default path of the RTL8814.

## 5. Building the 16 slots

### 5.1 The calendar state that is applied

In `interfaceStateChange`, after the infrastructure query and `selectChannelsToUseForAWDL`:

1. Sends the sync params if missing (ioctl 116, token `0x74802869C8`, assembled at `0x21da0df`); its success
   activates the gate `+0xA640` that `setScheduleState`/`setChannelSequence` require.
2. Reprograms 132/205 if the channel changed or `channelConfigured` (byte `pm+0x548`) is 0:
   `programMasterChannels` at `0x21da04c`, and leaves `channelConfigured = (132 OK) && (template OK)`.
3. Chooses the state: with strategy 1 and sync inactive (`+0x3EE0` bit 1 at 0) the state is
   `5 * dataIntensiveServiceActive()` (call at `0x21da198`), and `dataIntensiveServiceActive`
   (`0xffffff80021dcb46`) returns **constant 0** on 25G241 → **state 0, Discovery**. With
   strategy 0: state 3 (Idle), or 5 if the source is 2 (Mdns). With sync active it keeps the current
   state `+0x28E4`.
4. With state 5 it calls `setDataMode(8, 1)` (`0x21da200`); in the other cases
   `setScheduleState(state, 8, true)` (`0x21da214`), which for strategies 1/0 rebuilds the
   sequence via `setChannelSequenceByState(state)` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.2; the
   inner call is in `setScheduleState`, line of the byState at 996 of the decompilation).
5. Enabling gate: with strategy 1 it requires an LLA configured or source 3
   (`isLLAConfigured` at `0x21d9b2c`) and absence of suspension; with strategy 0 it additionally requires an infra
   channel ≠ 0 (`0x21d9c09`) and `infraAllowedForNonRealtimeCases()` (`0x21d9c19`). Afterwards it calls
   `enableAWDL` (`0x21da336`), which reads `awdl_cap` (GET ioctl type 341, `0x155C02869C9`),
   emits the 129 for the first time (`setChannelSequenceUnconditional(1)`) and activates with the 117
   (`0x75802869C8`).

### 5.2 Sources per slot type (verified in the `switch` of `setChannelSequenceByState`)

`setChannelSequenceByState` (`0xffffff80021f53aa`) reads `__ZL20_channelSeqSlotTypes[state][slot]`
(`0xffffff80023aa1f0`, one dword per slot, rows of 64 B) and applies:

| Type | Source |
|---:|---|
| 0 | does not rewrite the slot |
| 1 | `_zeroChanInfo` — channel 0 |
| 2 | primary master info (`+0x42A0`, flags at `+0x42A8`) |
| 3 | secondary if `+0x42B0 ≠ 0`, else primary |
| 4 | current AWDL channel `+0x1A0/0x1A8` |
| 7 | infrastructure channel (pm+0x9BC/0x9C4) **verbatim** if `addInfraToMinimumChannelSequence()` allows it; else channel 0 |
| 8 | infrastructure channel if `airplaySink` (`+0xA648`) ≠ 0; else channel 0 |
| 9 | infrastructure channel if `infraAllowedForNonRealtimeCases()`; else: slot 0 → infra if `+0x3ED9` and there is a channel, else channel 6 (`0x600000001`, flags 10); rest → channel 6 only with SDB active and concrete states, else channel 0 |
| 5/6, 10–20 | auxiliary sources (presence peers, RT, DFS, mixes) — `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.4 |

After each slot runs the 6 GHz guard (bit `0x2000` of the flags of the entry and
`is6GChannelALlowedForP2P`): inoperative for 149 and 153.

**`addInfraToMinimumChannelSequence`** (`0xffffff80021f5ef6`–`0x21f5f8e`), the condition of type 7,
returns the conjunction of three things verified in the disassembly:

1. `infraAllowedForNonRealtimeCases()` (`0xffffff80021dc540`, call at `0x21f5f57`): infrastructure
   channel ≠ 0 **and** bit `+0x3ED9` at 1 (set by `configureValidAWDLChannels`, §4.2.7);
   if the channel is DFS (`+0x3ED8`) it additionally requires an active AirDrop/RT peer.
2. Infrastructure flags without `0x2000` (not 6 GHz) — `and [pm+0x9C4], 0x2000` at `0x21f5f4d`.
3. **AirPlay service count ≠ 0**: `IO80211CommandQueue::lockMap` with
   `_countAirPlayServices` (`0xffffff80021e0cee`) as callback and a local accumulator at zero
   (`0x21f5f1e`–`0x21f5f69`); the callback increments the accumulator per service request descriptor of
   type 0x21/0x0C, with string "ip6", without "arpa" and with "airplay" or "raop".

Without registered AirPlay services, type 7 stays at channel 0 even though the infrastructure is
associated and allowed. (The nonAirPlay services of wifip2pd do not count for this count.)

### 5.3 The 129

`setChannelSequenceUnconditional` (`0xffffff80021cb69a`) composes 400 B: `+0` version 1, `+4` flags
of `+0xAA74` with the SDB bits (`0x10` ON / `0x20` OFF depending on `dynSdbForceChanSeq` and the state),
`+5` n (16), `+6` encoding (`+0xA772`, 0 or 1 per §4.2.6), `+7` step, `+8` dup, `+9` fill, and
the 16 entries of 12 B copied from `+0xA774`. Suppression by `memcmp` of 0x190 against the copy at
`ivar+8`; `processAWDLDFSChannelSequence` before; emission with `ioctl_internal` and token
`0x81802869C8` (request 129). The driver is the one that executes the dwell and the one that renders TLVs 4
and 18 on the air (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.2). The 132 (`0x84802869C8`) carries the channel of
`+0x42A4` and the 205 (`0xCD802869C8`) that of `+0x42B0` (`programMasterChannels`,
`0xffffff80021d494a`).

## 6. Concrete answer: en2 on 153/80, no peers

Verified inputs for the scenario: infrastructure channel `{1, 153, 0x410}` (§3.1 and
`chanFlags` of the layer); socials 6/149/44 unchanged (§4.2); master infos `{1, 6, 0x8}` and
`{1, 149, 0x10}` (§4.3); strategy 1 (AwdlStateMachine, §4.1); state Discovery (0), §5.1; Discovery row
read today in the table: `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7`.

| Slot | Type | Channel | Flags | Note |
|---:|---:|---:|---:|---|
| 0–3 | 3 | **149** | `0x10` | secondary (`+0x42B0` = 149) |
| 4–5 | 1 | **0** | `0x0` | unserved |
| 6–7 | 7 | **153** `0x410` **or 0** | — | infra if there is ≥1 registered AirPlay service (§5.2); else channel 0 |
| 8 | 2 | **6** | `0x8` | primary master |
| 9 | 4 | **149** | `0x10` | current AWDL channel (seed; later evolution `?`) |
| 10 | 3 | **149** | `0x10` | secondary |
| 11 | 4 | **149** | `0x10` | current AWDL channel |
| 12–13 | 1 | **0** | `0x0` | unserved |
| 14–15 | 7 | **153** `0x410` **or 0** | — | same as 6–7 |

Totals: without AirPlay services → 149 in 7 slots (0–3, 9, 10, 11), 6 in 1 (slot 8), channel 0 in 8
(4, 5, 6, 7, 12, 13, 14, 15). With a registered AirPlay service → 149 in 7, **153 in 4** (6, 7, 14,
15), 6 in 1, channel 0 in 4 (4, 5, 12, 13). The global flags byte of the 129 (`+4`) depends on the SDB
state (0x10/0x20) and the encoding (`+6`) is 1 if the driver reported 149 with 40/80/160 MHz in the
207 list (0 otherwise).

Variants that change the row: ETSI country → `+0x4294 = 0`, the type-3 slots fall to the 6 and the
mask loses bit 2; driver without the dual-band bit (`+0xB37` bit `0x20`) → same. **This is the
current case of the RTL layer**: `RTLAirController::getCARD_CAPABILITIES`
(`Source/AirLayer/RTLAirLayer.cpp`) leaves `p[7] = 0x05`, without bit `0x20`, so today the manager has
no 5 GHz social and the type-3 slots resolve to the 6. It matches the parking on the 6
observed on hardware on 2026-10-30, although the first logged 129 (6 in slots 0 and 8, the rest
at 0) does not match any row of the table and seems to be the initial sequence, previous to the
per-state rebuild `?`. The log of each change of the 129 (`83977cf`) will clarify it; with
`airplaySink ≠ 0` and the conditions of `monitorAWDLState`, mode 0 replaces **also** the
social slots with 153/0x410 (§4.4) and the 132 programs 153.

## 7. 149 or 153 within the same 80 MHz block?

They do coexist: the manager does not choose one of the two, it uses them for different roles and never derives
one from the other. The 149 is a **social**: default value of `initParamsToDefaults`, validated against the
channel list of the driver and the country, and announced with band flags without width (`0x10`, 20 MHz).
The 153 is the **infrastructure**: literal copy of the channel GET of `en2` with its flags `0x410`
(80 MHz), valid only in the type-7 slots and only with AirPlay services. In the same
80 MHz block (center 155) both are different 20 MHz primaries: a radio tuned to primary 153
does not decode the 20 MHz AWDL frames with primary 149 `?` (standard 802.11 behavior, not
measured on the RTL8814AU; `AWDL_WINDOW_TIMER.md` §8). That is why a fixed radio on 153 only covers the
infrastructure slots (if any) and loses the seven ones of 149; with the router on primary 149 the
two populations coincide.

## 8. What changes with a peer present

- Any TLV 18/32/4 of a peer arrives via `IO80211AWDLPeer::updateAwdlChanSeq`
  (`0xffffff80021ab7e4`, called from `actionFrameReport` `0x21b1520` and
  `doMinimalAFProcessing` `0x21b45e8`) to `followPeerChannelSequence`
  (`0xffffff80021f6e74`): it copies the 16 12 B entries of the peer (block `peer+920`, channel at
  `+924`) to `+0xA774`, saves the previous one at `+0xA8F4`, sets state 9 (RealTimeData) and emits the
  129 forced. The project's own sequence stops depending on the infrastructure.
- `setScheduleState` diverts to follow mode when there is a marked peer (`+0x3F78` bit 0 or the peer of
  `+0x3F60` with the bit of `peer+0x681`): it calls `followPeerChannelSequence` and does not touch the table.
- The iPhone peer of the captures announces `{44,128}` in its 5 GHz slots and `{6,81}` in one of
  2.4 GHz (`AWDL_AF_VS_IPHONE.md` §3): operating classes, not flags — the render on the air belongs to
  the emitting driver, not to the manager.
- States 5/9 (Data/RealTimeData) do not rewrite the table (row at 0): the content remains what the peer
  tracking left (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.3).

## 9. Driver capability bits that alter the result

| Driver read | Effect on the sequence |
|---|---|
| `getCHANNEL` (request 4, §3.1) | channel and flags of the infrastructure slots and of the band compare `0x2018`; it is the only source of the 153 |
| Channel list (request 207/379, §4.2) | validates 149/44 by country; DFS of the 153 (`+0x3ED8`); AWDL permission (`+0x3ED9`); without a valid 149 it disappears from the socials; bits `0x38` → encoding 1 |
| `awdl_cap` (request 341, `enableAWDL`) | does not alter slots; activates SEC_PAYLOAD/CCA_STATS/PSF_DWELL_MI (`+0x40C0..`) |
| `getRsdbCaps` (in `configure`) | SDB state (`+0x4084/+0x4088/+0x4064`); does not change slots, but the byte `+4` of the 129 and states 9/10 |
| Dual band (`*(ctrl+0x120)+0xB37` bit `0x20` = `p[7]` bit `0x20` of `getCARD_CAPABILITIES`) | without it, `+0x4294 = 0`: sequence with only 6 and channel 0 |
| Capability bytes of the primary interface (`+0xB3F` bit 7 = 6E in `configure`, bit 6 in `enableAWDL`; `+0xB47` bit `0x20` Limited SDB) | do not touch slots; they adjust the 207/379 list and timers `?` |
| `chanSwitchCapable` (`+0x3FA1`) | does not come from the driver: `configure` sets it to 1; it conditions the strategy branch of `interfaceStateChange` and bit `0x400` of the capabilities TLV |
| `awdlIvars+0x300` = 1 | `setAwdlMasterChannel`/`setAwdlSecondaryMasterChannel` forward 132/205 to the driver and adopt its answer |

## 10. What a single-radio driver must do with that sequence

1. **Honest `getCHANNEL`** (request 4): real primary channel and correct flags
   (`0x10 | 0x400` for 153/80). It is the only thing the manager knows about the infrastructure; wrong
   flags break the band-change detection (`0x2018`) and the validation of §4.2.7.
2. **Dual-band capability**: `p[7]` bit `0x20` in `getCARD_CAPABILITIES` (§4.2.2). Without it there is
   no 5 GHz social even if the radio supports it; in the RTL layer it is missing today.
3. **Honest 207 list**: if the driver does not declare 149 valid, the manager withdraws the 5 GHz
   social (`+0x4294 = 0`) and the 5 GHz presence is reduced to the infrastructure slots. It must include the
   block of the infrastructure channel with its width support bits if the
   operating-class encoding is wanted.
4. **Accept 129/132/205 and render the TLV 18 per slot**: real channel of the radio in the served
   slots, channel 0 in the rest, operating class derived from the flags of each entry (the current render of
   the layer fixes `{ch,128}`/`{ch,81}` — `Source/AirLayer/AWDLInterface.cpp`, `awdlOpClass`/`putChanSeq`).
   The infrastructure entry (153, `0x410`) and the social one (149, `0x10`) are different primaries:
   if the radio does not hop channel, announce 0 in the slots it does not serve.
5. **Do not fake acceptance** of the 129 without applying it: the one that validates is the driver; the kernel only
   rebuilds and reprograms if the failure is declared (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §6–7).
6. **Events 41/43** only in the served slots, with the channel of the entry of the 129
   (`AWDL_WINDOW_TIMER.md` §5).
7. To coexist without hops with infrastructure on 153: convincing the manager that 149 is not
   servable does not suffice (it will keep announcing it); the clean way is the router with primary 149, or
   accepting the loss of the seven 5 GHz social slots.

## 11. Corrections to previous documents

- `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.1: `enableAWDL` does not choose strategy (§4.1); its assumed
  "chooses 0 or 1 according to `+16289` and the counters" corresponds to the branch of `interfaceStateChange`.
- Same §3.4, type 7: the complete condition is the triple conjunction of §5.2 (the AirPlay
  service count and the 6 GHz rejection were not documented), and type 8 depends on
  `airplaySink` (`+0xA648`), not on a "service counter".
- Same §4, row "Social channels": the raw fields are `+0x4290` (master), `+0x4294`
  (secondary) and `+0x4298` (alternate); the mask of `+0x429C` is not `bit0=6, bit1=44, bit2=149`
  as-is, but: bit1 only if the chosen secondary is 44 and bit2 only if `+0x4294 == 149`.
- `AWDL_CHANNEL_STRATEGY.md` §8.1: with strategy 0, `interfaceStateChange` does allow enabling
  AWDL when there is infrastructure and it is allowed (gate `0x21d9bc2` → `0x21d9c01` →
  `infraAllowedForNonRealtimeCases`); what aborts is the enable without infrastructure
  ("waiting briefly before turning off AWDL").
- The semantics of the flags: `0x8`/`0x10`/`0x2000` are the 2.4/5/6 GHz bands (not width bits); the
  width lives in `0x2/0x4/0x400/0x800` (20/40/80/160 MHz), per `setInfraChannelSpec`,
  `isValidChannel` and `initParamsToDefaults`.

## 12. Open

- Later evolution of the current AWDL channel `+0x1A0/0x1A8` (type 4) after events 41/43 `?`.
- Whether some writer restores `+0x4290 = 6` when the validation fails (the log promises "defaulting to 6"
  but the store has not been seen; the default value is already 6) `?`.
- Capability bytes `+0xB3F` (bit 6 and 6E) and `+0xB47` (bit 0x20, Limited SDB) read in `configure`:
  they are `p[15]` and `p[23]` of `getCARD_CAPABILITIES` by the same map of §4.2.2; their effect has not been
  followed `?`.
- Exact type of the IOUC of `getDTIMInt` (fallback `apple80211getDTIM_INT` verified; the expected
  type is 167 `?`).
- Real behavior with wifip2pd: which services the daemon registers in the request map
  (`_countAirPlayServices` only counts "airplay"/"raop") and whether some path activates
  `airplaySink` on this stack `?`.
