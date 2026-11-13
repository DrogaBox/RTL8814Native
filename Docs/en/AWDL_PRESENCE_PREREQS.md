# Prerequisites of AWDL presence: LLAValid, AWDLOn and the context of the PPP slots

It closes the three pending items of [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §6: what activates
`LLAValid` (`PM+65308`), who writes `AWDLOn` (`PM+16096`) and on which thread/lock
`monitorPeer` and ioctl 415 run when they call slots +0xF30/+0xF38. Base: KC 25G241
(macOS 26.7.1) in IDA. Anything unchecked goes with `?`.

## 1. LLAValid (`PM+65308`, byte at `ivars+0xFF1C`)

### 1.1 What it means

It is the result of `IO80211AWDLPeerManager::isLLAConfigured` (`0xffffff80021dc508`): if the virtual
interface (`ivars base+1376`) is not null, it returns
`IOSkywalkNetworkInterface::isLinkLocalAddressConfigured(ifnet)` (`0xffffff8002b0bebe`); if it is null,
it logs `"isLLAConfigured virtual interface is NULL"` and returns 0. The checker walks the
address list of the `ifnet` with `ifnet_get_address_list_family(..., 0x1E)` (AF_INET6) and accepts the
first with `out_addr[8] == 0xFE && out_addr[9] < 0xC0` (prefix fe80::/10); with a null
`ifnet` it returns 1. The `ifnet` is taken from `ivars+8` of the Skywalk interface (`0xffffff8002b0bebe`).

There is no IPv6 event hooked: the value is polled. The three writers:

| Writer | Address | Value | Guard |
|---|---|---|---|
| `initParamsToDefaults` (`0xffffff80021c7a40`) | `0xffffff80021c818f` | 0 | none (start) |
| `interfaceStateChange` (`0xffffff80021d8cc8`) | `0xffffff80021d9b39` / `0xffffff80021d9b44` | 1 / 0 | `_bonjourTriggersAWDL != 0` (`ivars+0xA718`, test at `0xffffff80021d9b0a`) **and** `getvalidAWDLServices() != 0` |
| `monitorAWDLState` (`0xffffff80021ed724`) | `0xffffff80021ed932` | byte = `isLLAConfigured()` | none (direct copy on each tick) |

All three write the whole byte with 0/1, so "bit 0 of `PM+65308`" is the whole byte.
A sweep of all accesses with displacement `0xFF1C` in IO80211Family gives no more writers;
the readers are `setPeerRoutable` (`0xffffff80021aaf47`), `isLLAValid` (`0xffffff8002236d6c`) and
`monitorAWDLState` (`0xffffff80021ee228`).

### 1.1.1 `ivars+0xA718` = `_bonjourTriggersAWDL`

The name comes from the logs `"bootarg override ivars->_bonjourTriggersAWDL -> %u"`
(`initWithInterface`, `0xffffff80021c168d` and `0xffffff80021c16f8`) and `"bonjourTriggersAWDL"`
(`setAwdlCtl`, `0xffffff80021d0338`). Writers (full sweep of displacement `0xA718`):

| Function | Address | Value |
|---|---|---|
| `initWithInterface` | `0xffffff80021c15d6` | 1 (default, together with `ivars+4 = 1`) |
| `initWithInterface` | `0xffffff80021c1668` | 0 if boot-arg `awdl_enable` exists |
| `initWithInterface` | `0xffffff80021c16d1` | 0 if `awdl_oper_mode=1` (also `ivars+4 = 3`, `ivars+0xA66C = 0`) |
| `setAwdlManualMode` (`0xffffff80021c2d0a`) | `0xffffff80021c2d1b` | 0 (`ivars+4 = 3`); no code xrefs |
| `setAwdlAutoMode` (`0xffffff800221e95e`) | `0xffffff800221e97f` | 1; no code xrefs |
| `setAwdlOperatingMode` (`0xffffff800221eb90`, from `setAWDL_OPER_MODE`) | `0xffffff800221ec92` / `0xffffff800221ecc5` | mode 1 → 0 (manual); mode 2 → `setAwdlSuspendedMode` without touching it; other → 1 and `exitAwdlSuspendedMode` |
| `setAwdlCtl` | `0xffffff80021d032a` | dword of the payload (log `"bonjourTriggersAWDL %u -> %u"`) |

Readers: `interfaceStateChange` (`0xffffff80021d9b0a`), `monitorAWDLState` (`0xffffff80021ee4c0`,
`0xffffff80021ee691`), `updateLocalRequests` (`0xffffff8002212acb`), `getAwdlCtl` and
`setAwdlCtl` itself (`0xffffff80021d0320`). Without boot-args and without a manual `AWDL_OPER_MODE` the field equals 1, so
the LLA block of `interfaceStateChange` runs in the default automatic mode.

### 1.2 `handleAwdlLLATimer`: the 500 ms poll

`IO80211AWDLPeerManager::handleAwdlLLATimer` (`0xffffff80021c2ee6`) is the callback of the timer
saved at `AWDL ivars+10400` (`0x28A0`), created in `initWithInterface`
(`0xffffff80021c2080`: `IO80211TimerSource::allocWithParams(2, this, workqueue, handleAwdlLLATimer)`,
stored at `0xffffff80021c209b`). The workqueue is the controller's: it is obtained through cell
+0xC68 of the object at `ivars base+1312` (`0xffffff80021c203b`). Three branches:

- If `PM+16096 & 2` (AWDLOn already active): logs `"AWDL is enable, cancel monitor timer"` and calls
  cell +0x218 (`IOTimerEventSource::cancelTimeout`) and +0x158
  (`IOTimerEventSource::disable`) of `__ZTV18IO80211TimerSource`. The timer is not freed.
- If `isLLAConfigured()`: logs `"LLA is configured so go ahead and enable AWDL"`, does the same
  `cancelTimeout` + `disable` and calls cell +440 of its own vtable (`interfaceStateChange`)
  with source **7** (`Enable`).
- If not: logs `"WARNING: LLA still not configured so continue monitoring"` and rearms the timer with
  `setTimeoutMS(500)` (cell +0x1D0).

The timer is only armed while there is no LLA: `interfaceStateChange` enables and arms it in the error
branch (§1.3) and `handleAwdlLLATimer` disarms it upon getting the LLA or upon seeing AWDL already active.

### 1.3 The LLA block of `interfaceStateChange`

In the common body (`0xffffff80021d8cc8`), after the entry log:

1. If `_bonjourTriggersAWDL == 0` or `getvalidAWDLServices() == 0`, the block is skipped.
2. It calls `isLLAConfigured()`. With 0: writes `PM+65308 = 0` (`0xffffff80021d9b44`), logs
   `"ERROR: enableLLAMonitoring as no IPv6 LLA, Source = %s"` (`0xffffff80021d9b5c`), enables the
   LLA timer (cell +0x150 = `IOTimerEventSource::enable`, called at `0xffffff80021d9b99`) and arms it
   with 500 ms (cell +0x1D0 = `setTimeoutMS` with `esi=0x1F4`,
   `0xffffff80021d9bb1`–`0xffffff80021d9bb6`).
3. With 1: writes `PM+65308 = 1` (`0xffffff80021d9b39`) and continues.
4. Activation decision (`0xffffff80021d9bc2`–`0xffffff80021d9be6`): exits immediately if
   `channelSequenceStrategy == AwdlFollowsInfra` (`ivars+4 == 0`, `cmp dword ptr [rcx+4], 0` at
   `0xffffff80021d9bc2`; §1.5); if not,
   `enable = (LLAValidJustSet | source == 3) & !(AWDLOn & 8)`; the source test is
   `cmp r15d, 3` at `0xffffff80021d9bc8` and the suspension test `test al, 8` at
   `0xffffff80021d9bd7`. Without activation and without services/AirPlay/sidecar, it exits
   (`"NOT-ENABLING-AWDL"`); with activation it ends up calling `enableAWDL`
   (xref at `0xffffff80021da336`, after the log `"Enabling AWDL due to %s"`).

`monitorAWDLState`, besides the copy at `0xffffff80021ed932`, re-checks the bit later
(`test byte ptr [rcx+0FF1Ch], 1` at `0xffffff80021ee228`): with LLAValid at 0 and services present
it logs `"No IPv6 Address on AWDL interface ! Request IPv6"` (branch following `0xffffff80021ee228`).

### 1.4 `StateChangedSource`: complete enum and verified emitters

`IO80211PeerManager::stringForInterfaceStateChangedSource` (`0xffffff8002254562`) indexes the string
table at `0xffffff800245d450` (0..14, >14 = `"unknown"`):

| Value | Name | Verified emitter (immediate in the emitter) |
|---|---|---|
| 0 | `Init` | none with an immediate `?` |
| 1 | `BSSIDChanged` | none with an immediate `?` |
| 2 | `Mdns` | `updateLocalRequests` `0xffffff8002212951` and `0xffffff8002212f97` |
| 3 | `setSyncEnabled` | `setAwdlSyncEnabled` `0xffffff800221b9e6` (called at `0xffffff800221b9eb`, branch with `payload+8 != 0`); special case in the decision of `interfaceStateChange` (`0xffffff80021d9bc8`) |
| 4 | `Debug` | `setAwdlCtl` `0xffffff80021d011f` |
| 5 | `Interface` | `reportDataPathEvents` `0xffffff80021de7fb` |
| 6 | `Reset` | `reportDataPathEvents` `0xffffff80021df883`; `exitAwdlSuspendedMode` `0xffffff80021e5f76` |
| 7 | `Enable` | `doEnable` `0xffffff80021c53d1`; `disableAWDL` `0xffffff80021c7691`; `handleAwdlLLATimer` (final call to cell +440 with 7) |
| 8 | `Channel Switch Complete` | none with an immediate `?` |
| 9 | `SetInfraChannel` | `IO80211PeerManager::setInfraChannel` `0xffffff8002254b03`; `IO80211VirtualInterface::setInfraChannel` `0xffffff8002330a2c`; `reportDataPathEvents` `0xffffff80021de147` and `0xffffff80021df49a` |
| 10 | `Master Channel Changed` | `setAwdlMasterChannel` `0xffffff800221d5eb` (called at `0xffffff800221d5f0`) |
| 11 | `Secondary Master Channel Changed` | `monitorCountryCode` `0xffffff800225b3b6`; `reportDataPathEvents` `0xffffff80021de77c`; `setAwdlSecondaryMasterChannel` `0xffffff800221d489` |
| 12 | `Grace Period expired` | none with an immediate `?` |
| 13 | `AirPlay Solo Request` | `setAwdlSocialTimeSlots` `0xffffff800221bf99` (called at `0xffffff800221bf9e`), only if the byte `payload+4` changes `ivars+0xA6A4` (store at `0xffffff800221bf8d`) |
| 14 | `AWDL exiting suspension` | `exitAwdlSuspendedMode` `0xffffff80021e5ff2` |

The `0x4A20` mask of `updateHTandVHTCapsForRSDB` (`bt` at `0xffffff80021d9ac3`) selects the
sources {5, 9, 11, 13, 14}; source 6 produces the log `"Recovering Reset due to watchdog"`
(`0xffffff80021d9b01`); 11 and 14 reset the channel configuration.

For the RTL8814: `reportDataPathEvents` is the driver's path (events 41/42/43) and the sources
it emits (5, 6, 9, 11) all enter through the LLA block when there are valid services.

### 1.5 `ivars+4` = `channelSequenceStrategy`

The dword `ivars+4` is the channel sequence strategy. `setChannelSequenceStrategy`
(`0xffffff80021ca958`) prints it with the table `0xffffff800245b6d0` (log
`"set channelSequenceStrategy %s => %s (reason %d) force=%d"`) and `initChannelSequenceStrategy`
(`0xffffff80021c2cfc`) writes it directly:

| Value | Name |
|---|---|
| 0 | `AwdlFollowsInfra` |
| 1 | `AwdlStateMachine` |
| 2 | `AwdlFixed` |
| 3 | `AwdlUserConfigured` |

`setChannelSequenceStrategy(strategy, reason, force)` only changes the value if `force` or if the current
value is neither the requested one nor 3 (`AwdlUserConfigured` is sticky without `force`). Writers:

| Site | Address | Value (reason, force) |
|---|---|---|
| `initWithInterface` | `0xffffff80021c15d3` | 1 by default |
| `initWithInterface` (`awdl_oper_mode=1`), `setAwdlManualMode`, `setAwdlOperatingMode` mode 1 | `0xffffff80021c16c8`, `0xffffff80021c2d12`, `0xffffff800221ec89` | 3, direct write |
| `configure` | `0xffffff80021c5b75` | 1 (0, yes) |
| `setAwdlCtl` | `0xffffff80021cffba` | dword of the payload (3, yes) |
| `setAwdlChannelSequence` | `0xffffff800221d150` | 3 (3, yes) |
| `setAwdlAutoMode`, `setAwdlOperatingMode` other mode | `0xffffff800221e976`, `0xffffff800221ecbc` | 1 (6, yes) |
| `interfaceStateChange` | `0xffffff80021d900a` | (4, no): with `ivars+0xA648 != 0`, 0 if bit 0 of `ivars+0x3FA1` is at 0; 1 (log `"Transitioning to Solo mode"`) if it is at 1 and `ivars+0xA6A4` or `ivars+0xA65C` are not 0; no call if both are 0 |
| `monitorAWDLState` | `0xffffff80021ee3ae` | 0 (5, no), log `"Transitioning back to InfraAs…"`: with `ivars+0xA6A4 == 0`, strategy 1, no active sockets (`activeSockets(false)`) and two local conditions on `r14b`/`r15b` `?` |

Consequence: the activation block of §1.3 only calls `enableAWDL` with a strategy other than
`AwdlFollowsInfra`. `configure` leaves 1, so after configuration activation is
possible; if `monitorAWDLState` returns the interface to `AwdlFollowsInfra` (return to infrastructure
without sockets), the next activation needs something to raise the strategy again. The fields
`ivars+0xA648`, `+0x3FA1`, `+0xA6A4` and `+0xA65C` that govern it have no name yet `?`.

## 2. AWDLOn (`PM+16096`, dword at `ivars+0x3EE0`)

### 2.1 Bit map

From the entry log of `interfaceStateChange` (`0xffffff80021d8d1c`–`0xffffff80021d8da9`) and from
`configure` (`0xffffff80021c54ea`):

| Bit | Mask | Name (log string) |
|---|---|---|
| 0 | 0x1 | `CONFIGURED` (`test r14b, 1`) |
| 1 | 0x2 | `SYNC-ACTIVE` (`test r14b, 2`; the bit `reportDataPathEvents` calls `isAwdlOn`) |
| 2 | 0x4 | `QUIET` (set by `enterQuietMode`); the log prints `QUIET` with `test r14b, 0Ch`, i.e. also with bit 3 |
| 3 | 0x8 | `FOR ASSOCIATION` (`test r14b, 8`): suspension for infra association; set by `setAwdlSuspendedMode` |
| 4 | 0x10 | configure in progress (`"Configure is already in progress"`, branch of `interfaceStateChange`; `configure` sets it at `0xffffff80021c5652` and clears it at `0xffffff80021c5ede`) |
| 5 | 0x20 | second flag of `configure` (`0xffffff80021c57eb`); it is the only one `failToEnable` preserves `?` |

Bits 0xE off = `OFF` in the same log (`test r14b, 0Eh`).

### 2.2 Writers

| Function | Address(es) | Operation |
|---|---|---|
| `enableAWDL` (`0xffffff80021dcf58`) | `0xffffff80021dd5a3`–`0xffffff80021dd5a9` | `(x & ~6) \| 2`: clears QUIET, sets SYNC-ACTIVE |
| `setAwdlSyncState` (`0xffffff800221e3d4`) | `0xffffff800221e49d` | `x & ~2` (turns SYNC-ACTIVE off before reconfiguring) |
| | `0xffffff800221e52e` | `x & ~4` (clears QUIET after the first successful ioctl 117) |
| | `0xffffff800221e6e6`–`0xffffff800221e6ec` | `(x & ~6) \| 2` (reactivates SYNC-ACTIVE after the second ioctl 117) |
| `reset` (`0xffffff80021f2a20`) | `0xffffff80021f2f86` | `x = 0` (zeroes the whole dword); the immediate `"state %x"` log prints 0 |
| `awdlChipReset` (`0xffffff80021f24d0`) | `0xffffff80021f266f` | `x = 0` |
| `disableAWDL` (`0xffffff80021c6ae8`) | `0xffffff80021c721e`–`0xffffff80021c7221` | `x & ~2` (if SYNC-ACTIVE was at 0, exits with `"AWDL already disabled"`, `0xffffff80021c71f6`) |
| | `0xffffff80021c72a9`–`0xffffff80021c72bd` | `x = (x & ~arg) & ~4`: clears QUIET and the bits the argument asks for (mask; bit 3 is consulted at `0xffffff80021c723a`), then `setEnableState(bit1)` (`0xffffff80021c72c3`–`0xffffff80021c72cd`) |
| `configure` (`0xffffff80021c54ea`) | `0xffffff80021c5652`, `0xffffff80021c57eb`, `0xffffff80021c5ede`, `0xffffff80021c625d`–`0xffffff80021c6268` | `x \| 0x10`, `x \| 0x20`, `x & ~0x10`; final `(x & ~0x11) + 1`: sets CONFIGURED and clears bit 4 |
| `failToEnable` (`0xffffff80021c50de`) | `0xffffff80021c50eb` | `x & 0x20` |
| `setAWDLMaxBandwidth` (`0xffffff80021cde02`) | `0xffffff80021cde76` | `x & ~1` (removes CONFIGURED) |
| `setAwdlSyncEnabled` (`0xffffff800221b842`) | `0xffffff800221ba80` | `x & ~6` |
| `reportDataPathEvents` (`0xffffff80021dd8f4`) | `0xffffff80021df3bb`–`0xffffff80021df3be` | `x & ~2` |
| `setAwdlSuspendedMode` (`0xffffff80021e5c7a`) | `0xffffff80021e5cc6`–`0xffffff80021e5cc9` | `x \| 8` (if not already) |
| `clearAWDLSuspendState` (`0xffffff80021c777a`) | `0xffffff80021c7793`–`0xffffff80021c7796` | `x & ~8` |
| `exitAwdlSuspendedMode` (`0xffffff80021e5e76`) | `0xffffff80021e5e8e` | `x & ~8` |
| `enterQuietMode` (`0xffffff80021f408e`) | `0xffffff80021f42af`–`0xffffff80021f42b5` | `(x & ~6) \| 4`, then `setEnableState(false)` |
| `exitQuietMode` (`0xffffff80021fad1c`) | `0xffffff80021faf7e`; `0xffffff80021fb07f`–`0xffffff80021fb085` | `x & ~6`; `(x & ~6) \| 2` |
| `initParamsToDefaults` (`0xffffff80021c7a40`) | — | does not touch 16096 (only `PM+65308 = 0` at `0xffffff80021c818f`) |

The list comes from a sweep of all accesses with displacement `0x3EE0` in IO80211Family.
`enableAWDL` has a single code xref: `interfaceStateChange` at `0xffffff80021da336`. AWDLOn
bit 1 is set at three sites: `enableAWDL`, `setAwdlSyncState` after the second successful ioctl 117 and
`exitQuietMode` (called from `exitAwdlSuspendedMode`, `initiateHighThruputMode` and others). The
first activation after `configure` goes through `interfaceStateChange → enableAWDL`, with the
sources of the table of §1.4 (7 direct, or any source with `LLAValid` freshly set and no suspension).
`reset` and `awdlChipReset` turn everything off; `disableAWDL` preserves CONFIGURED and QUIET except for the
argument's mask (the value `doDisable` (`0xffffff80021c69cc`) and the rest of callers pass is not
traced `?`).

### 2.3 `setAwdlSyncState` step by step

`IO80211AWDLPeerManager::setAwdlSyncState` (`0xffffff800221e3d4`), when the requested state is 0
(Stopped) or bit CONFIGURED is active (`0xffffff800221e41b` test of bit 0; condition on
branch `a2[1] == 0 || (x & 1)`):

1. `x & ~2` (`0xffffff800221e49d`), timestamp at `ivars+42472`, `resetElectionState`,
   `updateBroadcastMI`, `updateSyncFrameTemplate(true)`.
2. `apple80211RouteInternal(0x802869C8, 117, ...)` (`0xffffff800221e510`–`0xffffff800221e51a`);
   on success `x & ~4` (`0xffffff800221e52e`).
3. ioctl **134** (sync state) with the payload (`0xffffff800221e556`); on success
   `ivars+17028 = a2[1]`.
4. `initFWElectionParams` and second ioctl **117** (`0xffffff800221e6c0`–`0xffffff800221e6cc`); on
   success `(x & ~6) | 2` (`0xffffff800221e6e6`–`0xffffff800221e6ec`) and `setEnableState(bit1)`
   (`0xffffff800221e794`).

That is: the field goes down and up around ioctls 117/134; the final state of bit 1 depends
on the success of the firmware rearm, not on the argument.

## 3. Thread, workloop and locks of `monitorPeer` and ioctl 415

### 3.1 The chain of `monitorPeer`

`IO80211AWDLPeer::monitorPeer` (`0xffffff80021a9294`, cell +0x180 of vtable
`__ZTV15IO80211AWDLPeer @ 0xffffff8002459f80`; the same cell exists in the base vtable
`__ZTV11IO80211Peer @ 0xffffff800245c738`, value at `0xffffff800245c8c8`) has a single caller
in the whole kernelcache: `_doMonitoring` (`0xffffff800224f70b`), which executes
`*(vtable+0x180)(peer, ctx)` at `0xffffff800224f720`, writes `*ctx = 0` before and returns 12 if the
byte stays at 1 (it is the result of `shouldRemovePeerFromSystem`, written by `monitorPeer`).

`_doMonitoring` is invoked by `IO80211PeerManager::interfaceMonitor` (`0xffffff800224e658`) through
`IO80211CommandQueue::map(commandQueue, _doMonitoring, this, &ctx, {0, 0, _addToFlush}, 0xFFFFFFFF)`
(call at `0xffffff800224e943`; `commandQueue` = `ivars base+1344`). The `ctx` is a local
112-byte buffer zeroed; only its byte 0 is used. After the `map`, it unlocks the data path and calls
`removeFlushedPeers`. The two prior accesses to the +0x180 cells of the objects at
`ivars base+0x530`/`+0x538` (`0xffffff800224e836`, `0xffffff800224e895`) pass the same ctx; in the
vtables of `IO80211TimerSource`/`WCLTimerSource` that cell is `IOEventSource::_RESERVEDIOEventSource1`
(`0xffffff8000bb7540`), so filling the ctx stays unverified `?` (irrelevant for the PPP queue: the byte is written by
`monitorPeer`).

The full temporal chain:

1. `_interfaceMonitorTimer` at `ivars base+0x718` (1816), created in
   `IO80211PeerManager::initWithInterface` (`0xffffff800224789b`–`0xffffff80022478b3`:
   `allocWithParams(2, this, workqueue, handleMonitorTimer)`) on the controller's workqueue
   (`ivars base+0x528`).
2. `handleMonitorTimer` (`0xffffff80022482d4`) calls cell +352 (+0x160) = `interfaceMonitor`
   and, if it returns non-zero, rearms the timer of `ivars base+1816` with a period with jitter
   derived from `ivars+1840` (minimum ~101 ms).
3. `interfaceMonitor` (base) checks its guards, calls `lockDataPath`
   (`0xffffff800224db2a`: cell +0x118 of the object at `ivars base+120`; the unlock is cell
   +0x120), does the `map` of §3.1 and unlocks.
4. `IO80211AWDLPeerManager::interfaceMonitor` (`0xffffff80021ed628`, cell +0x160) runs when the
   base returns 1 and `ivars+16100 == 0` (chip reset not in progress) and executes the
   `monitor*` suite, among them `monitorAWDLState`.

Therefore, `monitorPeer`, `ageoutPacketsWaitingForPPP` and the call to slot +0xF38 run on the
**controller's workqueue** (workloop context, serialized with the rest of the driver's work) **with the data path lock taken**
(`lockDataPath`, same object/cell `reset` uses, whose failure branch logs `"lockDataPath failed"`).

### 3.2 The path of ioctl 415

`setPOST_PRESENCE_DONE` (`0xffffff800228d968`) requires request length 12 and non-null data, takes
the manager from the interface (`safeMetaCast` to `IO80211AWDLPeerManager`) and calls
`setAwdlPostPeerPresenceDone` (`0xffffff800221c978`), which does `findPeer(data+4)` +
`safeMetaCast` to `IO80211AWDLPeer` + `pppDone(peer, true)`. Neither function takes
its own lock.

The context is set by the request router:

- `apple80211RouteExternal` (`0xffffff800236cd2e`) indexes `gSetHandlerTable` with `cmd-1` and calls the
  handler directly (the 415's entry is `0xffffff8002460be0`), without gate or workloop in the
  function itself.
- Its three callers are gated: `IO80211SkywalkInterface::performGatedCommandIOUC`
  (`0xffffff80022f6e30`), `IO80211SkywalkInterface::performGatedCommandIOCTL`
  (`0xffffff80022f779a`, xref `0xffffff80022f78ad`) and `IO80211VirtualInterface::handleIoctlGated`
  (`0xffffff8002331d20`).
- The BSD path of the 415: `IO80211VirtualInterface::handleIoctl` (`0xffffff8002331c88`) compares the
  code with the four apple80211 ioctls (`0x802069C8`, `0x802869C8`, `0xC02069C9`,
  `0xC02869C9`). With one of them it obtains the controller (cell +0xBA8 =
  `IO80211SkywalkInterface::getController`) and calls its cell +0x850, which in
  `__ZTV17IO80211Controller` is `IONetworkController::executeCommand` (`0xffffff80026f51e0`), with
  `handleIoctlGated` as action (reference at `0xffffff8002331cdc`). `handleIoctlGated` rebuilds the
  `apple80211req` and calls `apple80211RouteExternal` directly (`0xffffff8002331e56`). The other
  ioctls jump to `processBSDCommand` (`0xffffff8002331d1b`), which goes through
  `performGatedCommandIOCTL` (`0xffffff80022f70e8`). In both cases the handler runs under the
  **command gate of the controller's workloop**, on the thread that made the ioctl (wifip2pd).

Context comparison for the RTL8814 driver: +0xF30 arrives through the controller's gate from an
ioctl; +0xF38 arrives from `ageoutPacketsWaitingForPPP` (`0xffffff80021aa984`) on the workqueue with the
data-path lock, and from `flushPacketsWaitingForPPP` (`0xffffff80021bff87`) on peer removal.

The removal route is traced like this: `flushPacketsWaitingForPPP` has a single caller,
`IO80211AWDLPeerManager::removePeer(IO80211Peer*)` (`0xffffff800221721c`, called at
`0xffffff80022177e4`), which has no direct xrefs and occupies cell +0x278 (632) of
`__ZTV22IO80211AWDLPeerManager`. The only call to that cell in the managers' code is in
`IO80211PeerManager::removeFlushedPeers` (`0xffffff800224f73a`, `call [rcx+278h]` at
`0xffffff800224f768`), which drains with `IO80211CommandQueue::lockDequeue` the to-remove peer queue
of `ivars base+1880` (`0x758`). `removeFlushedPeers` has two callers:

- `IO80211PeerManager::interfaceMonitor`, in this order: `lockDataPath` (cell +0x118 of the object at
  `ivars base+0x78`, `0xffffff800224e8f3`), `IO80211CommandQueue::map(_doMonitoring)`
  (`0xffffff800224e95a`), `unlockDataPath` (cell +0x120, `0xffffff800224e96a`) and
  `removeFlushedPeers` (`0xffffff800224e973`). The flush runs on the controller's workqueue but
  **without** the data-path lock: the `ageout` does it with the lock and the flush right after
  releasing it.
- `IO80211PeerManager::removeAllPeers` (`0xffffff80022542e4`, called at `0xffffff800225441e`),
  reached from `IO80211AWDLPeerManager::removeAllPeers` (`0xffffff800222be5f`) and from three
  vtables. `IO80211AWDLPeerManager::removeAllPeers` occupies cell +0x280 of
  `__ZTV22IO80211AWDLPeerManager` and is called through that cell from `failToEnable`
  (`0xffffff80021c5174`), `disableAWDL` (`0xffffff80021c7359`), `interfaceStateChange`
  (`0xffffff80021da645`), `setAwdlSyncEnabled` (`0xffffff800221ba76`), `IO80211PeerManager::stop`
  (`0xffffff80022492de`), `IO80211PeerManager::doDisable` (`0xffffff800224c1cb`) and
  `IO80211PeerManager::reset` (`0xffffff800224cc08`): these are shutdown and reset routes, many
  reachable from an ioctl under the command gate. Whether any of them has the data-path lock taken at that
  moment is not checked `?`.

`cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`) does not call `flushPacketsWaitingForPPP`
directly. The implementation of +0xF38 must be safe on the workloop, with or without the data-path
lock taken, and must not sleep.

### 3.3 What gets freed after +0xF30/+0xF38

All three callers build the array the same way: `IOMallocZeroData(512)` = 64 pointers (in `pppDone`,
`0xffffff80021bfb3b`), fill it in batches of 64 with `dequeuePPPWaitPacketFromHead` and call the slot
(+0xF30 at `0xffffff80021bfc77` for `pppDone`; +0xF38 at `0xffffff80021aa984` for `ageout` and
`0xffffff80021bff87` for `flush`). Afterward:

- `pppDone` does `IOFreeData` of the array on all paths (`jmp _IOFreeData` at
  `0xffffff80021bfd93`).
- `ageout` (`IOFreeData` at the close of its loop) and `flush` (final `IOFreeData`, decompiled from
  `0xffffff80021bfe44`) likewise free only the array.

The packets of the queue are not touched in any of the three functions: their ownership passes to the slot.
With the inherited slots of `IO80211VirtualInterface` (`0xffffff8002334e68`/`0xffffff8002334e88`,
only `IOLog("… Not Implemented")`) the array is returned to the allocator but the packets are lost
(no delivery and no return to the pool). This closes the doubt of §6 of
[AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md): the caller always frees the array; what
leaks is the packet queue if the slots are not overwritten.

Allocation failure: if `IOMallocZeroData` fails, all three use `globalPacketArray` (static) as the
array and `pppDone`/`flush` end up passing it to `IOFreeData` `?` (freeing a global object on the error path; not verified at runtime).

### 3.4 `monitorPeer` guards relevant to the PPP queue

- Early return if `PM+16096 & 0xC` (QUIET or FOR ASSOCIATION): it only updates the mark
  `peer ivars+0x310` and exits without touching the queue (`test byte ptr [rcx+3EE0h], 0Ch` at
  `0xffffff80021a9339`, return at `0xffffff80021a9349`).
- `ageoutPacketsWaitingForPPP` is only called if the peer's PPP flag is at 0
  (`test byte ptr [rax+6DEh], 1` at `0xffffff80021a9705`; call at `0xffffff80021a9711`).
  Once the 415 is received (`pppDone(true)` set `peer ivars+1758 = 1` in `pppDone`), the ageout stops
  running for that peer. This corrects table §3 of
  [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md): the ageout is not unconditional.
- The ageout threshold is `>= 2001 ms` (`cmp edx, 0x7D1` equivalent: `if (ms < 0x7D1) break`, in
  the decompiled code of `0xffffff80021aa7ca`), not exactly 2000 ms.

## 4. Summary for the AWDL interface of the RTL8814

1. `LLAValid` requires an fe80::/10 address on the `ifnet` of `awdl0` and active polling: without IPv6 on
   the interface, `handleAwdlLLATimer` retries every 500 ms and `interfaceStateChange` rearms the timer
   on each invocation with valid services. In the kernel the lack of LLA does not block the peer:
   `setPeerRoutable` writes `priv+120 = 1` before looking at `LLAValid`/`AWDLOn`
   (`0xffffff80021aaf2d`) and those two bits only choose between the 4050 log "Cannot post peer presence"
   and the "postPeerPresence" log; the real block is that AWDL does not get activated (point 2).
2. The first activation of `AWDLOn` bit 1 goes through `interfaceStateChange → enableAWDL`
   (afterward `setAwdlSyncState` and `exitQuietMode` set it back); the driver's
   sources (`reportDataPathEvents`, 5/6/9/11) are valid paths if there are valid AWDL services and
   `_bonjourTriggersAWDL != 0`, which is the default value without the boot-args `awdl_enable` or
   `awdl_oper_mode=1`.
3. +0xF30 runs under the controller's command gate (`executeCommand`, thread of ioctl 415); +0xF38 runs on the
   controller's workqueue with the data-path lock (ageout) or on peer removal (flush). Both
   receive an array of 64 pointers that the caller always frees afterward.

## 5. Pending

- Emitters of sources 0, 1, 8 and 12 of `StateChangedSource` (§1.4): no call to cell +0x1B8 of a peer manager passes them as immediate; if they exist, the value arrives in a register.
- Names of `ivars+0xA648`, `+0x3FA1`, `+0xA6A4` and `+0xA65C`, which decide the strategy in
  `interfaceStateChange` and `monitorAWDLState` (§1.5).
- Lock of each caller of `removeAllPeers` (§3.2).
- The calls to the +0x180 cells of the objects at `ivars base+0x530/0x538` inside
  `interfaceMonitor` (§3.1) and the meaning of bit 5 of `PM+16096` that `configure` sets (§2.1).
