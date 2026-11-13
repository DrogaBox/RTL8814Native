# The channel sequence strategy of the `IO80211AWDLPeerManager`

Everything cited is verified in the 26.7.1 kernelcache (25G241, x86_64); each VA is the exact
instruction of the annotated base. This document names the four fields that govern
`ivars+4` (the open question of `AWDL_PRESENCE_PREREQS.md` §1.5) and closes the two local
conditions that remained as `?`: those of `monitorAWDLState` before the return to
`AwdlFollowsInfra`, and the `"Transitioning to Solo mode"` branch of `interfaceStateChange`.

The offsets `ivars+…` refer to the ivars block of the peer manager, `*(this+0x20)`.

## 1. `ivars+4` = `channelSequenceStrategy`

Recap of `AWDL_PRESENCE_PREREQS.md` §1.5 with the complete mechanism.

`IO80211AWDLPeerManager::setChannelSequenceStrategy(channelSequenceStrategy,
channelSequenceStrategyReasons, bool)` (`0xffffff80021ca958`–`0x21caa40`):

- Reads the current value (`0xffffff80021ca96e`) and always prints
  `"setChannelSequenceStrategy"` line `0x28e1` with
  `"set channelSequenceStrategy %s => %s (reason %d) force=%d"` (`0xffffff80023d6d32`,
  called at `0xffffff80021ca99f`); the names come from the table `0xffffff800245b6d0`
  (`AwdlFollowsInfra` 0, `AwdlStateMachine` 1, `AwdlFixed` 2, `AwdlUserConfigured` 3,
  verified in `0xffffff800245b6d0`..`0x6e8`), with `"unknown"` if the index exceeds 3
  (`0xffffff80021ca971`). The same table is used by the entry log of `interfaceStateChange`
  (`0xffffff80021d8dca`), a later log of the same function (`0xffffff80021d9ed4`),
  `setChannelSequenceUnconditional` (`0xffffff80021cbb37`), `setAwdlCtl` (`0xffffff80021cffe2`),
  `setScheduleState` (`0xffffff80021d2d06`) and `printState` (`0xffffff80022226fc`); the helper
  `StringForAwdlStrategy(channelSequenceStrategy)` (`0xffffff80021a8b88`) uses a copy at
  `0xffffff8002459c00`. `printDataPath` prints the numeric value:
  `"... %u _channelSequenceStrategy %u"` (`0xffffff80023e794a`, called at `0xffffff8002223b80`).
- Writes only if `force` or if the current value is neither already the requested one nor 3
  (`0xffffff80021ca9d5`–`0x21ca9ec`): `AwdlUserConfigured` is sticky without `force`.
- If `dword ivars+0x4008 == 8` (`0xffffff80021ca9ee`; `0x4008` is the device class per
  `AWDL_CAPS_ADVERTISED.md`, which compares it against 2 at `0xffffff800221445a` — the meaning of 8 `?`),
  the transit to or from `AwdlFollowsInfra` adjusts the MI map with `adjustMiBitmap(uint, bool, uint)`
  (`0xffffff80021caa1f`–`0x21caa2d`): entering 0 → mask `0x05050505` (`esi` at `0xffffff80021caa02`),
  leaving 0 → mask `0xffffffff` (`esi` at `0xffffff80021caa16`).
- Stores at `0xffffff80021caa32`.

Complete writers (the sweep of `[reg+4]` with base in the ivars block only adds readers;
the write goes through `setChannelSequenceStrategy` or is direct):

| Site | VA | Value (reason, force) |
|---|---|---|
| `initWithInterface` (default) | `0xffffff80021c15d3` (via §1.5) | 1 |
| `initWithInterface` with `awdl_oper_mode=1` | `0xffffff80021c16c8` | 3, direct |
| `initChannelSequenceStrategy(strategy)` | `0xffffff80021c2d04` | argument, direct |
| `setAwdlManualMode` | `0xffffff80021c2d12` | 3, direct |
| `configure` | `0xffffff80021c5b75` | 1 (0, yes) |
| `setAwdlCtl`, key `"channelSequenceStrategy"` | `0xffffff80021cffba` | ctl value (3, yes) |
| `setAwdlChannelSequence` | `0xffffff800221d150` | 3 (3, yes) |
| `setAwdlAutoMode` | `0xffffff800221e976` | 1 (6, yes) |
| `setAwdlOperatingMode`, mode 1 | `0xffffff800221ec89` | 3, direct |
| `setAwdlOperatingMode`, other mode | `0xffffff800221ecbc` | 1 (6, yes) |
| `interfaceStateChange` | `0xffffff80021d900a` | 0 or 1 (4, no) — §7 |
| `monitorAWDLState` | `0xffffff80021ee3ae` | 0 (5, no) — §6 |

Readers of the value (exact `[reg+4]` with ivars base): `setChannelSequenceUnconditional`
(`0xffffff80021cbb28`), `setAwdlCtl` for the log (`0xffffff80021cffd4`), `setScheduleState`
(`0xffffff80021d28a2`, `0x21d2cf5`, `0x21d2e34`, `0x21d2f8d`, `0x21d30e7`), `getAwdlCtl`
(`0xffffff80021d5491`, `0x21d6317`, `0x21d633d`), the entry of `interfaceStateChange`
(`0xffffff80021d8dc0`, name in the log), the strategy branch (`0xffffff80021d8fa7`),
the gate of `enableAWDL` (`0xffffff80021d9bc2`: with strategy 0 it does not activate) and the
channel recalibration in `AwdlFollowsInfra` mode (`0xffffff80021d9a48`, `jnz` with the value),
two later logs (`0xffffff80021d9ec0`, `0x21da179`), `sanityCheckAWStart`
(`0xffffff80021e2a6e`, compares against 1), `monitorAWDLState` (`0xffffff80021ee330` and, manually
reviewed, `0x21ee4aa`: with 0 it does not take the activation branch), `exitQuietMode`
(`0xffffff80021fae4e`, compares against 0),
`updateLocalRequests` (`0xffffff8002212c9d`), `setScanningState` (`0xffffff8002219615`),
`usePiggybackScan` (`0xffffff8002219914`), `getAwdlChannelSequence` (`0xffffff800221faaa`),
`printState` (`0xffffff80022226f2`) and `printDataPath` (`0xffffff8002223b78`).
The linear sweep uses per-function register tracking and misses uses whose path goes
through an interleaved call (e.g. `0x21ee4aa` was recovered by hand from the dump); the set of
writers does not depend on that limitation because the sweep of the four large offsets is
global and complete.

## 2. `ivars+0xA648` = `airplaySink` (dword; capability copy at `+0xA64C`)

IO80211Family names the field: `setAwdlCtl` prints `"airplaySink : %u -> %u"`
(`0xffffff80023d0f35`, called at `0xffffff80021d06cd`) and
`"User configured airplaySink %u -> %u"` (`0xffffff80023d1605`, called at `0xffffff80021d06f4`);
`getAwdlCtl` reports it as `"airplaySink = %u"` (`0xffffff80021d69e4`). Other logs cite it:
`"Restored, airplaySink:%d airplaySinkCap:%d"` (`0xffffff80023ed11b`, in `restoreAirPlaySink`
`0xffffff8002208008`), `"AirPlay/Sidecar sender, disbale AirPlay sink. airplaySink:%d
airplaySinkCap:%d"` (`0xffffff80023ed162`, in `disableAirPlaySink` `0xffffff8002201e2b`),
`"sidecar starts. Turn off AirPlay sink support (airplaySink:%d airplaySinkCap:%d)"`
(`0xffffff80023dbaed`, in `setSidecarChanseq` `0xffffff800222d30d`), `"%s _airplaySink:%u"`
(`0xffffff80023db952`), `"ivars->_airplaying:%u _airplaySink:%u, is24GhzChanSeq:%u
coexPolicyEnable:%u"` (`0xffffff80023dafe3`), `"%s UltraLowLatencyRealTime -
airdropServiceCount=%d numAirPlayServices=%d airplaySink=%d"` (`0xffffff80023d76db`, called at
`0xffffff80021d332a` of `setScheduleState`) and `"dualband %u airplaySink %u ..."`
(`0xffffff80023c8b1e`).

Observed semantics: the Mac acts as an AirPlay receiver (sink) over the AWDL interface; the getter
`isAirPlaySink()` (`0xffffff8002234f04`) returns `dword != 0` (`cmp` at `0xffffff8002234f0c`,
`setnz al` at `0xffffff8002234f13`). `+0xA64C` is `airplaySinkCap`, the value
configured by the user that survives the temporary shutdowns.

Writers:

| Site | VA | Operation |
|---|---|---|
| `setAwdlCtl`, key `"airplaySink"` (request 162) | `0xffffff80021d06bf` | `[0xA648] = value`; then `[0xA64C] = value` (`0x21d071e`) and `[0xA648] = value` again (`0x21d0724`) |
| `disableAirPlaySink` | `0xffffff8002201e18` | `[0xA648] = 0` if it was not 0; `updateBroadcastMI(0xF, false, true)` (`0x2201e68`) |
| `restoreAirPlaySink` | `0xffffff8002207ffd` | `[0xA648] = [0xA64C]`; `updateBroadcastMI(0xF, false, changed)` (`0x2208042`) |
| `setSidecarChanseq` (sidecar start with bit 0 of `+0x3F33` at 0) | `0xffffff800222d490` | `[0xA648] = 1` |

The disable/restore pair is used by `setSidecarChanseq` (`0xffffff800222d33e` calls
`disableAirPlaySink` when bit 0 of `+0x3F33` is already 1, with the log
`"sidecar starts. Turn off AirPlay sink support"`; when stopping the sidecar it calls
`restoreAirPlaySink` twice, `0xffffff800222d436` and `0x222d43e`).

Readers (global sweep; function — VA — role):

- `interfaceStateChange` `0xffffff80021d8f76` — gate of the whole strategy decision (§7).
- `monitorAWDLState` `0xffffff80021ee25e` — first condition of the return to `AwdlFollowsInfra` (§6).
- `evaluateQuietMode` `0xffffff80021da69a` — if it is ≠ 0 sets `ivars+0xA66C = 0` (no quiet mode).
- `configure` `0xffffff80021c62d7` — together with `_maxPeerCacheSize == 4` forces solo (§4).
- `enableAWDL` `0xffffff80021dd52b` — if it is 0 calls `setLogPolicy(1)` (`0x21dd537`).
- `setScheduleState` `0xffffff80021d3318` (log), `0x21d3645`, `0x21d36d4`, `0x21d3719`,
  `0x21d37c6` — with 0 they substitute the fixed budget `0x6E` (110) by `byte ivars+0x4288 * 110`.
- `reevaluateRTGConfiguration` `0xffffff80021d4d19` — same pattern of the `0x6E` budget.
- `monitorElectionMetric` `0xffffff80021ed552` — chooses `setElectionMetricForAirplaySink` if ≠ 0.
- `processUnicastAWSF` `0xffffff80021baa59`, `0x21bafb6`, `0x21bc62b` — gates of
  `realTimeCannotUseInfra` and of the peer traffic registration.
- `handleUmiTimer` `0xffffff80021c3526`, `jumpStartDFSProxy` `0xffffff80021c3d0a` — gates of UMI
  and of the DFS proxy jumpstart (with airplaySink ≠ 0 the jumpstart continues; with 0 it prints the error
  `"ERROR: DFSProxy Jumpstart reques…"`, call at `0xffffff80021c3d13`).
- `determineAirPlaySoloMode` `0xffffff80021d8041`, `setAirPlayPeer` `0xffffff80021fc79c`,
  `calculateDataChannelsForAirplaySolo` `0xffffff80021f9e75`,
  `calculateDataChannelsForAirplaySoloDFS` `0xffffff80021f9062` and `0x21f90a6` —
  AirPlay data channel computation.
- `setChannelSequenceByState` `0xffffff80021f5930` — in case 8 it chooses the channel of the
  infrastructure (`[this+0x18]+0x9BC/0x9C4`) if ≠ 0, or channel zero if it is 0.
- `recoverAWDLPeerTrafficRegistrationSession` `0xffffff80021d864d` — gate of UMI recovery.
- `handleAirPlayEnsembleTrafficReg` `0xffffff80021ffe81`, `0x21fff3f`, `0x22000a2`;
  `handleAirPlayToOdeonTrafficReg` `0xffffff80022019c9`;
  `handleSideCarEnsembleTrafficReg` `0xffffff8002204a53`;
  `setAwdlPeerTrafficRegistration` `0xffffff800220a9f2`, `0x220b9ce`, `0x220bddc`, `0x220bebf`;
  `airplayStatsPostMetrics` `0xffffff800220459f`; `notifyThreadCoexEvent` `0xffffff8002201372`;
  `buildRealTimeGroupTlv` `0xffffff8002210d99`; `updateLocalRequests` `0xffffff8002212c94`,
  `0x221370f`; `cachedPeerHasChangedInfra` `0xffffff8002215bec`;
  `cachedPeerChangedRealTimeState` `0xffffff8002215dca`;
  `preQueuePacketCheckQuietMode` `0xffffff80022164a2`; `clearAwdlStatistics`
  `0xffffff800221e24d`; `updateRTGPeersRoleMetricsInfo` `0xffffff80022320d3`;
  `getAwdlPeerDatabase` (byte) `0xffffff80022200f6`;
  bridge `setAWDL_STATISTICS` `0xffffff8002289dc9`; `setSidecarChanseq` (log)
  `0xffffff800222d2fb`; `fillLegacyDataPathPayload` `0xffffff8002214413` (bit `0x40` of the
  capabilities TLV, see `AWDL_CAPS_ADVERTISED.md`); `restoreAirPlaySink` (comparison against the shadow)
  `0xffffff8002207ff2`; `setAwdlCtl` (old value for the log) `0xffffff80021d06b5`; `getAwdlCtl`
  `0xffffff80021d61cb`, `0x21d69d7`, `0x21d6a00`.

## 3. `ivars+0x3FA1`, bit 0 — `chanSwitchCapable` (the getter is named `supportsTrackingTopMaster`)

Bit 0 of byte `0x3FA1` has two names from the binary itself:

- The entry log of `interfaceStateChange`
  `"%s::%s Enter [reason %s] state 0x%x%s%s%s%s%s %s %s %s soloEnabled %u chanSwitchCapable %u
  RoamInProgress %d"` (`0xffffff80023d2260`, called at `0xffffff80021d8e7d`) prints it as
  `chanSwitchCapable`: the value stacked at `0xffffff80021d8e51` is `byte [ivars+0x3FA1] & 1`
  (`0xffffff80021d8e14`). In the same log, `soloEnabled` is `dword [ivars+0xA6A4]` (stacked at
  `0x21d8e53`, read at `0x21d8e06`) and `RoamInProgress` is bit 0 of `ivars+0x4200`
  (`0x21d8e20`).
- The method `IO80211AWDLPeerManager::supportsTrackingTopMaster()` (real symbol of the binary,
  `0xffffff80021e3bf6`) returns the same bit (`0xffffff80021e3bfe`, `and al, 1`).

Only writer in the whole family: `configure` sets the byte to 1
(`0xffffff80021c5b5c`). No other instruction in the kernelcache writes it; in particular neither
`reset` nor `awdlChipReset` nor `initParamsToDefaults` clears it (the sweep of `+3FA1` only
finds the store of `configure`). After the first `configure` the bit stays at 1 permanently.

Mirror writes: `configure` copies the bit to `byte ivars+0x3ECC` (`0xffffff80021c5b92`) and
`initParamsToDefaults` copies it to `dword ivars+0x3FA8` (`0xffffff80021c7f54`).

Readers: `interfaceStateChange` (entry log and strategy branch, §7), `setScheduleState`
`0xffffff80021d3a22` (for state 0xA it counts `nonAirPlayServices` only with the bit at 1, after
checking `ivars+0x3FA8 == 0` at `0x21d3a05`), `reportDataPathEvents` `0xffffff80021e058a`
(with the bit at 1 it compares 6 bytes of `ivars+0x916F` before copying the channel configuration,
`0x21e05ab`), `setElectionMetricForNonAirplaySink` `0xffffff80022163a2`, `configure`
(mirror `0x3ECC`), `initParamsToDefaults` (mirror `0x3FA8`) and `fillLegacyDataPathPayload`
`0xffffff800221443e` — together with `airplaySoloModeEnabled != 0` it adds the bit `0x400` to the
capabilities TLV (`0x2214450`, see `AWDL_CAPS_ADVERTISED.md`).

## 4. `ivars+0xA6A4` = `airplaySoloModeEnabled` (dword)

Named by IO80211Family: `setAwdlCtl` prints `"airplaySoloModeEnabled : %u -> %u"`
(`0xffffff80023d0ea1`, called at `0xffffff80021d05b1`); `getAwdlCtl` reports it as
`"airplaySoloModeEnabled = %u"` (`0xffffff80021d6815`); `setAwdlSocialTimeSlots` prints
`"%s::%s new mode %u old mode %u"` (`0xffffff800221bf57`) and `setElectionMetricForAirplaySink`
`"%s:%u: ATV Solo:%u Airplaying:%u met:%u…"` (`0xffffff800221609f`, the field as `ATV Solo`).

Writers:

| Site | VA | Operation |
|---|---|---|
| `configure` | `0xffffff80021c5b7e` | `= 1` unconditional, after `setChannelSequenceStrategy(1, 0, true)` |
| `configure`, end | `0xffffff80021c630b` | `= 1` if the global variable `_maxPeerCacheSize` (`__ZL17_maxPeerCacheSize`, read at `0xffffff80021c62ab`) equals 4 and `airplaySink != 0`, with log `"Forcing Solo Mode (PC Size %d)"` (`0xffffff80023d0744`, call `0x21c6302`) |
| `setAwdlCtl`, key `"airplaySoloModeEnabled"` (request 162) | `0xffffff80021d05a3` | `= low dword of the value` |
| `setAwdlSocialTimeSlots` (request 163) | `0xffffff800221bf8d` | `= byte +4` of the payload (`apple80211_awdl_social_time_slots::enabled`, `AWDL_STRUCTS.md` §5) if it differs; on change it notifies through the vtable cell `+0x1B8` with argument 13 (`0xffffff800221bf99`) |

Readers: `interfaceStateChange` (entry log `0xffffff80021d8e06`, strategy branch
`0xffffff80021d8f8f`, argument of the Solo log `0xffffff80021d8fc9`), `monitorAWDLState`
`0xffffff80021ee323` (must be 0 to return to `AwdlFollowsInfra`, §6), `monitorAWDLStats`
`0xffffff80021cda93` (sets bit 3 of the statistics dword `ivars+0xAB14`:
`[0xAB14] = ([0xAB14] & ~8) + solo*8`, `0x21cda9d`–`0x21cdaa9`), `calculateChannelsForDataMode`
`0xffffff80021f740d` (with the peer in AirPlay mode `peer+0x99` and solo ≠ 0 it diverts to
`calculateDataChannelsForAirplaySolo`, `0x21f743f`), `setElectionMetricForAirplaySink`
`0xffffff8002216085` (log) and `0x2216106` (election metric of the sink node), `fillLegacyDataPathPayload`
`0xffffff8002214447` (bit `0x400` next to bit 0 of `0x3FA1`), `getAwdlSocialTimeSlots`
`0xffffff800221bff4` (returns the low byte), and the old values/renders of `setAwdlCtl`
(`0xffffff80021d0599`) and `getAwdlCtl` (`0xffffff80021d618f`, `0x21d6808`, `0x21d6831`).

## 5. `ivars+0xA65C` = `nonAirPlayServices` and `ivars+0xA650` = `airPlayServices` (counters)

`getvalidAWDLServices()` (`0xffffff80021da6de`–`0x21da849`) recomputes them on every call from
the service request descriptor map `ivars+0xA5C8` using
`IO80211CommandQueue::lockMap`, passing the address of the field itself as accumulator:
`&ivars[0xA65C]` with `_countNonAirPlayServices` (`0xffffff80021da715`),
`&ivars[0xA650]` with `_countAirPlayServices` (`0xffffff80021da73f`), `&ivars[0xA654]` with
`_countRDLinkServices` (`0xffffff80021da769`), `&ivars[0xA658]` with
`_countCompanionLinkServices` (`0xffffff80021da793`) and `&ivars[0xF638]` with
`_countWifidOnlyServices` (`0xffffff80021da7bd`). Before that it zeroes `0xF638`
(`0x21da6eb`), `0xA650` and `0xA658` (`0x21da6f7`, `0x21da6fe`); `0xA65C` and `0xA654` are not
zeroed, so the callbacks must assign (exact accumulator mechanics `?`). The callbacks filter
descriptors by type (byte `+0x38` ∈ {0x21, 0x0C}), presence of `"ip6"`, absence of `"arpa"`
and the strings `"airplay"`/`"raop"` (visible in `_countAirPlayServices`, `0xffffff80021e0cee`;
the nonAirPlay variant at `0xffffff80021f7b71` is the complement).

Returned value (`0xffffff80021da836`): if bit 0 of `ivars+0x3F04` (CarPlay active) is 1,
`wifidOnlyServices` (`0xF638`, with the log `"CarPlay is active so allow wifid…"`, called at
`0xffffff80021da825`); otherwise `nonAirPlayServices + airPlayServices`.

Writer: only `getvalidAWDLServices`. The adds and removes that move the count arrive through the
service requests that fill the map `0xA5C8` (`setAwdlPeerServiceRequest`, vtable cell
`0xffffff800245aa30`; `deleteWifidServiceRequest`; `updateLocalRequests`), issued by
wifip2pd/wifid. Callers of `getvalidAWDLServices`: `disableAWDL` (`0x21c764a`),
`interfaceStateChange` (`0x21d8f66`, right before the strategy branch), `evaluateQuietMode`
(`0x21da691`), `isRemoteScreenRTGSyncElectionEnabled` (`0x21eab2b`),
`isContinuityRealTimeGroupRTGSyncElectionEnabled` (`0x21eac0e`), `monitorAWDLState`
(`0x21ee071`), `buildRealTimeGroupTlv` (`0x2210d75`), `updateLocalRequests` (`0x2212ae6`),
`validServiceExceptRDLink` (`0x2214863`) and `AWDLStateDump` (`0x222afdb`).

Readers of `0xA65C`: the strategy branch of `interfaceStateChange` (`0xffffff80021d8f98`,
`0x21d8fc2` and `0x21d9da9`), `evaluateQuietMode` (`0xffffff80021da6bd`), `monitorAWDLState`
(`0xffffff80021ee07a` and `0x21eefda`), `AWDLStateDump` (`0xffffff800222affc`), `updateLocalRequests`
(`0xffffff8002212ca3`). Logs that print them: `"…nonAirPlayServices(%u)"` of the Solo log
(`0xffffff80023d23b3`), `"%s::%s(): [reason: %s] %s (state 0x%x… numservices:%u[AirPlay:%u,
NonAirplay:%u](valid:%u)…"` (`0xffffff80023d2889`, called at `0x21d9db0`, which loads
`0xA650` at `0x21d9da2` and `0xA65C` at `0x21d9da9`), `"%s::%s AWDL is still enabled due to active
sockets, but no services[nonAirplay:%u Airplay %u]"` (`0xffffff80023d6b0d`, called at
`0x21eefe5`), `"%s[%d] : Active Sockets %s ValidSvc %d NumAirplay %d AFHandlePending %d
TimeOfLastAFHandleRequest %d ms"` (`0xffffff80023d6740`, called at `0x21ee128`; `ValidSvc` is
the return of `getvalidAWDLServices` and `NumAirplay` the field `0xA650`, loaded at
`0xffffff80021ee085`) and `"%s[%d]: BonJourTrig %d ValidSvc %d RTApp %d TSReq %d HasActAirDrop %d
SocketsActive %d"` (`0xffffff80023d69be`, called at `0x21ee67e`).

`evaluateQuietMode` (`0xffffff80021da688`) defines with these fields the variable
`ivars+0xA66C`: it is 1 only if `airplaySink == 0` and `airPlayServices != 0` and
`nonAirPlayServices == 0` (`0x21da6a3`–`0x21da6c6`), and in that case it calls
`freeInfraMulticastPackets`.

## 6. `monitorAWDLState`: the conditions of the return to `AwdlFollowsInfra`

`IO80211AWDLPeerManager::monitorAWDLState()` (`0xffffff80021ed724`–`0x21ef069`) runs from
`interfaceMonitor` (`0xffffff80021ed628`, vtable cell `0xffffff800245a9c8`), the periodic
watchdog. The block that decides the transition:

1. `airplaySink != 0`: `0xffffff80021ee25e`, if it is 0 jumps to `0x21ee459` (the rest of the
   monitor continues without transition). Between both addresses, and only with bits `0xF62D` bit 0 at 0 and
   `0xF62E` bit 0 at 1 (`0x21ee26b`–`0x21ee27f`), it withdraws the NAN layer of AirDrop if there are no active
   sockets and the links have been idle for more than the threshold (log `"AirDrop not active, removing NAN
   ov…"`, `0xffffff80021ee2b2`).
2. `airplaySoloModeEnabled == 0`: `0xffffff80021ee323`, if it is ≠ 0 jumps to `0x21ee459`.
3. Current strategy `AwdlStateMachine` (1) and `r14b`: `0xffffff80021ee330` (`setz al`),
   `and al, r14b` at `0xffffff80021ee337`, jump to `0x21ee459` if the conjunction is not 1.
4. `!activeSockets(false)` and `r15b`: call at `0xffffff80021ee352`, `xor al, 1` at
   `0xffffff80021ee357`, `and al, r15b` at `0xffffff80021ee359`; if it is not 1 the function ends
   (epilogue at `0xffffff80021eeca2`, after `clearAirPlayingState` and `monitorLimitedSdb`).
5. With all true: log `"%s::%s(): Transitioning back to InfraAssist mode"`
   (`0xffffff80023d6854`, called at `0xffffff80021ee387`) and
   `setChannelSequenceStrategy(0, 5, false)` at `0xffffff80021ee3ae` (arguments `esi=0`,
   `edx=5`, `ecx=0` at `0x21ee3a5`–`0x21ee3ac`).

The two local conditions that `AWDL_PRESENCE_PREREQS.md` §1.5 left as `?`:

- **`r14b`** = (`dword ivars+0xA65C == 0`) AND (`dword ivars+0xA650 != 0`):
  `nonAirPlayServices == 0` (`0xffffff80021ee07a`, `setz [rbp+var_39]` at `0x21ee081`) and
  `airPlayServices != 0` (`0xffffff80021ee085`, `setnz [rbp+var_3B]` at `0x21ee08f`); the
  conjunction forms at `0xffffff80021ee25a` (`and r14b, [rbp+var_3B]`) and is loaded at
  `0x21ee1ff`/`0x21ee213`. That is: only AirPlay services remain registered.
- **`r15b`** = (µs since the last unicast TX > threshold) AND (µs since the last unicast RX >
  threshold): `getLastUnicastTxLinkActivity` at `0xffffff80021edfe6` and
  `getLastUnicastRxLinkActivity` at `0xffffff80021ee01f`, ns→µs conversion (multiplier
  `0x431BDE82D7B634DB`, total shift 82) at `0x21ee012`–`0x21ee018` and
  `0x21ee049`–`0x21ee04c`, comparisons against `dword ivars+0x9144`
  (`0xffffff80021ee054`, `setb [rbp+var_48]` at `0x21ee060` for TX and `setb [rbp+var_3C]` at
  `0x21ee06a` for RX), conjunction at `0xffffff80021ee256`. The threshold `ivars+0x9144` is only
  written by `initParamsToDefaults` with `2000` (0x7D0) at `0xffffff80021c7e1d`: 2 ms without
  unicast activity in both directions.

Immediate collateral effect of the transition (`0xffffff80021ee3b3`–`0x21ee454`): with
`[this+0x18]+0x9C0 != 0` and `infraAllowedForNonRealtimeCases()` true (`0x21ee3c7`), without
assisted discovery (`byte 0xFA3A` bit 0 at 0, `0x21ee3dc`) and without the bit 0x2000 of
`[this+0x18]+0x9C4` (`0x21ee3e9`; `r12d` was loaded at `0x21ee346`), it copies the infrastructure
channel into the master sequence buffers (`[ctrl+0x9BC]` → `ivars+0x42A0`,
`[ctrl+0x9C4]` → `ivars+0x42A8`, `_zeroChanInfo` → `ivars+0x42AC`, dword
`0xffffff8002488a90` → `ivars+0x42B4`; the same buffers that `setChannelSequenceByState`
consumes at `0xffffff80021f5900`), calls `programMasterChannels()` (`0x21ee433`) and
`setScheduleState(ivars[0x28E4], 0x14, true)` (`0x21ee44f`). The failed branches end in the
diagnostic log `"infra is on DFS "`/`"not associated "`/`"Infra Not allowed with AWDL "`
(`0xffffff80021eecc5`–`0x21eed2d`).

Consequence of staying at 0: `monitorAWDLState` no longer takes its activation branch
(`cmp dword [ivars+4], 0; jz` at `0xffffff80021ee4aa` → counting path
`_countNonATVCachedPeers` at `0x21ee4e2`) and `interfaceStateChange` never reaches `enableAWDL`
(§7): the interface does not reactivate by itself.

## 7. `interfaceStateChange`: the branch of `"Transitioning to Solo mode"`

`IO80211AWDLPeerManager::interfaceStateChange(StateChangedSource)`
(`0xffffff80021d8cc8`–`0x21da65d`). Before deciding it calls `evaluateQuietMode()`
(`0xffffff80021d8f5e`) and `getvalidAWDLServices()` (`0xffffff80021d8f66`), so the
counters of §5 are freshly recomputed. The full branch:

```
0xffffff80021d8f76  cmp dword [ivars+0xA648], 0     ; airplaySink
0xffffff80021d8f7d  jz  0xffffff80021d900f          ; == 0: no call, strategy untouched
0xffffff80021d8f83  xor r15d, r15d                  ; 0 = AwdlFollowsInfra
0xffffff80021d8f86  test byte [ivars+0x3FA1], 1     ; chanSwitchCapable
0xffffff80021d8f8d  jz  0xffffff80021d8ffd          ; == 0: call with 0
0xffffff80021d8f8f  cmp dword [ivars+0xA6A4], 0     ; airplaySoloModeEnabled
0xffffff80021d8f96  jnz 0xffffff80021d8fa1          ; != 0: Solo
0xffffff80021d8f98  cmp dword [ivars+0xA65C], 0     ; nonAirPlayServices
0xffffff80021d8f9f  jz  0xffffff80021d900f          ; both at 0: no call
0xffffff80021d8fa1  mov r15d, 1                     ; 1 = AwdlStateMachine
0xffffff80021d8fa7  cmp dword [ivars+4], 1          ; already in AwdlStateMachine?
0xffffff80021d8fab  jz  0xffffff80021d8ffd          ; already: call without log
0xffffff80021d8fdc  log "Transitioning to Solo mode.  SoloModeEnabled(%u),
                         nonAirPlayServices(%u)"
                    ; r9d = dword 0xA6A4 (load at 0x21d8fc9)
                    ; stack = dword 0xA65C (load at 0x21d8fc2)
0xffffff80021d900a  setChannelSequenceStrategy(r15d, 4, false)   ; reason 4
```

Summary: the call happens only with `airplaySink != 0`; the requested value is 0 if
`chanSwitchCapable` is 0, and 1 if it is set and additionally `airplaySoloModeEnabled != 0` or
`nonAirPlayServices != 0`; with `chanSwitchCapable` at 1 and both counters at 0 the strategy is not
touched. With the strategy already at 1 the Solo log is suppressed (`0x21d8fab`) but the call
still happens (harmless: `setChannelSequenceStrategy` does not rewrite the same value).

Reason 4 corresponds to the interface state change; the emitter of the event is the
`StateChangedSource` of `AWDL_PRESENCE_PREREQS.md` §1.4 (`doEnable`, `disableAWDL`,
`reportDataPathEvents`, `exitAwdlSuspendedMode`, `handleAwdlLLATimer`, …).

The same function is who activates AWDL: the gate of `enableAWDL` requires a strategy other than 0
(`cmp dword [ivars+4], 0; jz` at `0xffffff80021d9bc2`) and re-reads the
counters for its log with `numservices` (`0x21d9da2`–`0x21d9da9`).

## 8. Final conditions: dropping to `AwdlFollowsInfra` and returning to `AwdlStateMachine`

### 8.1 How an external `awdl0` ends in `AwdlFollowsInfra` (0)

Two paths, both with `force=false` (strategy 3 is not overwritten, but 1→0 is not protected):

1. **Via `interfaceStateChange` (reason 4), only before the first `configure`.**
   Requires `airplaySink != 0` and bit 0 of `0x3FA1` at 0. After `configure` the bit is 1 and no
   writer that clears it exists (§3), so this path closes as soon as the interface is configured.
2. **Via `monitorAWDLState` (reason 5), the real post-configure path.** All five
   simultaneous conditions of §6 are needed:
   - `airplaySink != 0` (someone sent request 162 with the key `airplaySink`, or started a
     sidecar);
   - `airplaySoloModeEnabled == 0` (nobody activated it via 162/163/`configure` — or it was turned
     off via 163);
   - current strategy 1;
   - `nonAirPlayServices == 0` with `airPlayServices != 0` (only AirPlay services remain
     registered in the map `0xA5C8`);
   - no active sockets and more than `ivars[0x9144]` µs (2000 by default) without unicast TX or RX.

   The condition `airPlayServices != 0` is the protective key: an `awdl0` with no registered
   service at all (`0xA650 == 0` and `0xA65C == 0`) never drops to 0 through this path, and the
   `interfaceStateChange` branch does not touch it either (with `chanSwitchCapable==1` and both
   counters at 0 there is no call). The drop requires an AirPlay scenario: sink activated, solo
   deactivated, AirPlay services present, no other services, no sockets and idle links.

With the strategy at 0, neither `interfaceStateChange` (gate at `0x21d9bc2`) nor `monitorAWDLState`
(gate at `0x21ee4aa`) reactivate AWDL: this is the lockup described in
`AWDL_PRESENCE_PREREQS.md` §1.5.

### 8.2 What returns the interface to `AwdlStateMachine` (1)

1. **`configure`** — `setChannelSequenceStrategy(1, 0, true)` at `0xffffff80021c5b75`, with
   `force=true`: it wins even over `AwdlUserConfigured`. `configure` runs from `doEnable`
   (table `0xffffff800245aaa8`; request 117 `SET_AWDL_SYNC_ENABLED` and 134
   `SET_AWDL_SYNC_STATE` via `setAwdlSyncEnabled`/`setAwdlSyncState`), from certain sources of
   `interfaceStateChange`, from `setAwdlBtleEnableSynchWithParams`, `getAwdlRestoreDefaults`
   (`GET RESTORE_DEFAULTS`), `reportDataPathEvents` and `exitAwdlSuspendedMode` (reached from
   `setAwdlOperatingMode` (217), `handleSuspendTimer`, `setAwdlAutoMode` and
   `reportDataPathEvents`).
2. **`interfaceStateChange` (reason 4)** — with `airplaySink != 0`, `chanSwitchCapable == 1`
   (always true after `configure`) and `airplaySoloModeEnabled != 0` or `nonAirPlayServices != 0`
   (`0xffffff80021d900a`). In practice: activate solo (162/163) or register any non-AirPlay
   service and wait for an interface state change.
3. **Request 162 (`AWDL_CTL`), key `"channelSequenceStrategy"`** — `setChannelSequenceStrategy(value, 3, true)`
   at `0xffffff80021cffba`: it sends whatever value is requested, including 1.
4. **Request 217 (`SET_AWDL_OPER_MODE`) with mode ≠ 1** — `setChannelSequenceStrategy(1, 6, true)`
   at `0xffffff800221ecbc` (mode 1 writes 3 directly at `0x221ec89`). `setAwdlAutoMode`
   (`0xffffff800221e976`) would do the same but has no xrefs on 25G241 `?`.
5. **Request 129 (`AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`)** — sets 3 (`AwdlUserConfigured`),
   not 1 (`0xffffff800221d150`); it serves to take the interface out of 0 at the cost of freezing the
   sequence.

## 9. Userland requests that touch the fields

Index into the modern table `0xffffff800245fef0` = request − 1 (verified against the known
pairs 94/93, 117/116, 129/128, 134/133):

| Request | Bridge (index) | Effect |
|---|---|---|
| 162 `AWDL_CTL` | `setAWDL_CTL` `0xffffff80022892d1` (161) | `airplaySink` (+ shadow `airplaySinkCap`), `airplaySoloModeEnabled`, `channelSequenceStrategy` (reason 3, force), and the rest of the key catalog of `setAwdlCtl` |
| 163 `AWDL_SOCIAL_TIME_SLOTS` | `setAWDL_SOCIAL_TIME_SLOTS` `0xffffff8002289403` (162) | `airplaySoloModeEnabled = payload+4` |
| 148 `AWDL_STRATEGY` | `setAWDL_STRATEGY` `0xffffff8002288ff4` (147) | Does NOT touch `ivars+4`: `setAwdlStrategy` writes `apple80211_awdl_strategy+4` into `ivars+0x28E0` (`0xffffff800221c850`) |
| 217 `AWDL_OPER_MODE` | `setAWDL_OPER_MODE` `0xffffff8002289c29` (216) | mode 1 → strategy 3 direct; other mode → 1 (reason 6) |
| 129 `AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `0xffffff8002288caa` (128) | strategy 3 (reason 3) |
| 117 `AWDL_SYNC_ENABLED` / 134 `AWDL_SYNC_STATE` | `0xffffff800228889d` (116) / `0xffffff8002288e21` (133) | reach `configure` → strategy 1 (reason 0, force) and `airplaySoloModeEnabled = 1` |

It matches `AWDL_APPLE80211_DISPATCH.md` (162/163) and `AWDL_REQUEST_ISSUERS.md` (163 only CW =
CoreWiFi `?`). Whether wifip2pd sends `AWDL_CTL` with the key `airplaySink` or
`AWDL_SOCIAL_TIME_SLOTS` on an Apple machine is not recorded in the wifip2pd docs of this
repository `?`.

## 10. Auxiliary tables

- Strategy names: `0xffffff800245b6d0` (copy at `0xffffff8002459c00` for
  `StringForAwdlStrategy`, `0xffffff80021a8b88`).
- The contiguous table `0xffffff800245b6f0` (`Infra Assist (Legacy)`, `Split Mode`, `Server Mode`,
  `Client Mode`) is NOT of reasons: it is used by `airplayModeToString(AirPlayMode_t)`
  (`0xffffff80021f1615`) and `printState`. The observed `reason` values of
  `channelSequenceStrategyReasons` are only numbers: 0 `configure`, 3 `setAwdlCtl`/
  `setAwdlChannelSequence`, 4 `interfaceStateChange`, 5 `monitorAWDLState`,
  6 `setAwdlOperatingMode`/`setAwdlAutoMode`.
- Contiguous service fields: `+0xA650` `airPlayServices`, `+0xA654` rdLink,
  `+0xA658` companionLink, `+0xA65C` `nonAirPlayServices`, `+0xF638` wifidOnly.
- `ivars+0x9144`: unicast idleness threshold in µs, 2000 by default
  (`initParamsToDefaults` `0xffffff80021c7e1d`); no other writers.
- `ivars+0x28B0` bit 0: `AFHandlePending` (log `0xffffff80023d6740`); it is written by
  `actionFrameInput` (`0xffffff800220e54a`, `0x220e751`), `actionFrameProcessThread`
  (`0xffffff80021c4628`) and cleared by `enableAWDL` (`0xffffff80021dd065`),
  `actionFrameProcessThread` (`0x21c4039`) and `actionFrameQueueCleanup` (`0x21c86cf`).

## 11. Pending

- Exact accumulator mechanics of `_countNonAirPlayServices`/`_countRDLinkServices`
  (assignment versus addition) `?`.
- Meaning of the gate `dword ivars+0x4008 == 8` in `setChannelSequenceStrategy`
  (`0xffffff80021ca9ee`) and of the MI masks `0x05050505`/`0xffffffff` `?`.
- Whether wifip2pd/airportd emit `AWDL_CTL` (162) with `airplaySink` or `AWDL_SOCIAL_TIME_SLOTS`
  (163) on Apple hardware `?`.
- `setAwdlAutoMode` (`0xffffff800221e976`) without code xrefs on 25G241 `?`.
- The offset sweep is linear with per-function register tracking; uses whose only path goes
  through an interleaved call may be missing (case `0x21ee4aa`, recovered by
  hand). The writers, in contrast, come from a complete global sweep.
