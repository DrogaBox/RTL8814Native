# AWDL without infrastructure association

What `IO80211AWDLPeerManager` does when the primary interface is not associated: which paths turn
AWDL off, which channel sequence it builds without an infrastructure channel, which queries it launches on the
primary and what wifip2pd does in that state. Base: kernel collection 25G241 (macOS 26.7.1, x86_64)
over a copy of the annotated base; `wifip2pd` binary of 25G241. Each claim carries the VA of the
function or instruction; anything unobserved goes with `?`.

Conventions: `ivars` is `*(manager+0x20)` and `ctrl` is `*(manager+0x18)` (the `IO80211Controller`).
`ctrl+2496` (0x9C0) is the association indicator, `ctrl+2492` (0x9BC) the infrastructure's
`apple80211_channel_data` (channel at `+4`) and `ctrl+2500` (0x9C4) its flags (bit `0x2000` = 6 GHz).

## 1. Infrastructure state in the controller

- `IO80211AWDLPeerManager::associated()` (`0xffffff80021c6410`) returns
  `*(ctrl+0x9C0) != 0`; the comparison is `cmp dword ptr [rax+9C0h], 0` at `0xffffff80021c6418`.
  It has no direct xrefs in the binary; the reader is the very function `?`.
- `initParamsToDefaults` (`0xffffff80021c7a40`) leaves the channel and flags at zero:
  `mov [rax+9C4h], ecx` at `0xffffff80021c7b46` and `mov [rax+9BCh], rcx` at `0xffffff80021c7b53`.
- `getInfrastructureState` (`0xffffff80021da84a`) rewrites both fields from the controller read:
  `mov [rax+9C4h], ecx` at `0xffffff80021dad08` and `mov [rax+9BCh], rcx` at
  `0xffffff80021dad11`.

## 2. Does the lack of association turn anything of AWDL off?

### 2.1 Link-down gate on entry into `interfaceStateChange`

`IO80211AWDLPeerManager::interfaceStateChange` (`0xffffff80021d8cc8`), only with
`ivars+0x3EE4 != 0` (`cmp` at `0xffffff80021d8ea1`, else jumps to `0xffffff80021d8f0c`), checks the
interface object at `ctrl+0x560`: loads its vtable and calls slot `+0xB88`
(`0xffffff80021d8eaa`–`0xffffff80021d8eb8`). If the return **is** 1 (`cmp eax, 1` at
`0xffffff80021d8ebe`; the `jnz` of `0xffffff80021d8ec1` jumps to `0xffffff80021d8f51` with any other
value) it logs `"Interface link is down, nothing to do. Source = %s"` (`0xffffff80021d8ed8`), sets
`r12b = 1` and jumps to the end (`0xffffff80021d8f04`–`0xffffff80021d8f07`) without touching strategy,
services or AWDL. The method of slot `+0xB88` and whether that object is the primary or the
infrastructure interface stay `?`.

### 2.2 Strategy `AwdlFollowsInfra` (0)

The `ivars+4 == 0` branch of `interfaceStateChange` (`0xffffff80021d9c01` onward):

- If `*(ctrl+0x9C0) != 0` (`0xffffff80021d9c09`) and `infraAllowedForNonRealtimeCases()` is true
  (`0xffffff80021d9c19`), it returns to the activation decision (`0xffffff80021d9bc8`).
- If there is no association or the infrastructure is not usable, with AWDL already active
  (`test byte ptr [rcx+3EE0h], 2` at `0xffffff80021d9c26`) it logs
  `"In legacy infrastructure mode with no infra connection, waiting briefly before turning off
  AWDL"` (`0xffffff80021d9c5f`) and, if `infraAllowedForNonRealtimeCases()` still reads 0
  (`0xffffff80021d9c87`), `"… with unsupported infra connection …"` (`0xffffff80021d9c9e`) and jumps to
  `0xffffff80021da400` (`"can't enable awdl!"`). That path **does not call `disableAWDL`**: it stops
  activating and returns 0.
- `infraAllowedForNonRealtimeCases` (`0xffffff80021dc540`) requires `*(ctrl+0x9C0) != 0`
  (`0xffffff80021dc54b`); without association it returns 0.

### 2.3 `monitorAWDLState`: the only path that disables for lack of infrastructure

`IO80211AWDLPeerManager::monitorAWDLState` (`0xffffff80021ed724`) has a block that first
tries to return to `AwdlStateMachine` and, if it cannot use the infrastructure, disables AWDL:

- Transition: `setChannelSequenceStrategy(0, 5, false)` at `0xffffff80021ee3ae` (the entry
  conditions are in `AWDL_CHANNEL_STRATEGY.md` §6: `airplaySink != 0` at `0xffffff80021ee25e`,
  `airplaySoloModeEnabled == 0` at `0xffffff80021ee323`, strategy 1 at `0xffffff80021ee330`,
  no active sockets at `0xffffff80021ee352`; the exact composition of the auxiliary predicate stays
  `?`).
- With an infrastructure channel and `infraAllowedForNonRealtimeCases()` true (`0xffffff80021ee3c7`),
  no assisted discovery (`0xffffff80021ee3dc`) and no bit `0x2000` in `ctrl+0x9C4`
  (`0xffffff80021ee3e9`), it copies the infrastructure channel to the master, calls
  `programMasterChannels` (`0xffffff80021ee433`) and `setScheduleState(state, 0x14, true)`
  (`0xffffff80021ee44f`).
- In the remaining cases it builds the reason: `"infra is on DFS "` (bit 0 of `ivars+0x3ED8`,
  `0xffffff80021eecd0`), `"not associated "` (`*(ctrl+0x9C0) == 0`, `0xffffff80021eece2`),
  `"Infra Not allowed with AWDL "` (`0xffffff80021eecf9`), `"Asst Disc On "`
  (`ivars+0xFA3A`, `0xffffff80021eed16`) and `"on6G "` (bit 0x2000 of `ctrl+0x9C4`,
  `0xffffff80021eed21`), logs `"Disabling AWDL in FollowsInfra mode because  %s"`
  (`0xffffff80021eed35`) and calls `disableAWDL(this, 0)` at `0xffffff80021ee769`.

Consequence: without association, that path turns AWDL off with reason `"not associated "` only if
before it the transition conditions are met (AirPlay sink, AirPlay services, no sockets and idle
links). An `awdl0` without AirPlay services does not meet them; whether in practice it is not
reached stays `?` since it depends on the unnamed auxiliary predicate.

### 2.4 `setScheduleState`: warning and default sequence, without disabling

`IO80211AWDLPeerManager::setScheduleState` (`0xffffff80021d27a0`) checks
`*(ctrl+0x9C0) == 0` at `0xffffff80021d2ed3`; without association it logs
`"WARNING: In FollowsInfra mode without an infrastructure connection for %u ms"`
(`0xffffff80021d2f45`, with the timestamp `ivars+0xA5F8`) and calls
`initDefaultChannelSequence` (`0xffffff80021d2f65`). It does not disable.

### 2.5 `doEnable` and `enableAWDL`

`doEnable` (`0xffffff80021c5216`) queries `*(ctrl+0x9C0)` at `0xffffff80021c53a4` only to decide
whether to run `monitorInfraTraffic` (`0xffffff80021c53b0`); afterward it calls `enableInfraMonitoring`
(`0xffffff80021c53b8`) and enables the same. `enableAWDL` (`0xffffff80021dcf58`) does not read `ctrl+0x9C0`
at all.

### 2.6 Election, synchronization and windows do not depend on the association

Instruction sweep with displacement `+9C0h`, `+9BCh` and `+9C4h` in the range
`0xffffff80021c0000`–`0xffffff8002260000`: no reads appear in `enableAWDL`
(`0xffffff80021dcf58`), `setAwdlSyncEnabled` (`0xffffff800221b842`), `shouldRunElection`
(`0xffffff80021e3cca`), `runElection` (`0xffffff80021e3e7c`), `monitorTimeline`
(`0xffffff80021e11ca`) nor `sanityCheckAWStart` (`0xffffff80021e21de`). `shouldRunElection` only reads
`ivars+10307`, `ivars+16096`, `ivars+10468`, `ivars+10306`, `ivars+16020`, `ivars+16128` and the mark
`ivars+42472`; `runElection`, `monitorTimeline` and `sanityCheckAWStart` contain none of the
three references. Election and the window schedule therefore proceed without an
infrastructure channel; `sanityCheckAWStart` only compares the strategy with 1 (`?` the rest of its gate).
Readers through an alias other than the base register would not be covered by the sweep `?`.

## 3. Channel sequence without an infrastructure channel

### 3.1 Default channels: master 6, secondary master 149

`initParamsToDefaults` (`0xffffff80021c7a40`):

| Instruction | Field | Value |
|---|---|---|
| `0xffffff80021c7ada`/`0xffffff80021c7ae4` | `ivars+17040`/`+17044` | `0x00000006` / `0x95` (149) |
| `0xffffff80021c7aeb` | `ivars+17048` | `0x2C` (44) |
| `0xffffff80021c7af5`/`0xffffff80021c7aff` | `ivars+17056`/`+17060` | version 1, **channel 6** |
| `0xffffff80021c7b06` | `ivars+17064` | flags 8 |
| `0xffffff80021c7b14`–`0xffffff80021c7b27` | `ivars+17068`… | copy of the master to the secondary slot |

`configureValidAWDLChannels` (`0xffffff80021caa40`) validates 6 (`0xffffff80021cae41`), 149
(`0xffffff80021caf62`, store at `0xffffff80021caf85`) and 44 (`0xffffff80021cb074`, store at
`0xffffff80021cb097`), and fixes the mask of supported channels at `ivars+0x3ED8 = 0x100`
(`0xffffff80021cb0ad`). `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`) puts as master the
channel `ivars+17040` (6) and as secondary the 149, or the 44 if 149 is not valid, with flags 8.

Therefore **the default master channel is 6; 149 is the secondary master**. The choice of 149 or
44 depends on the country code; without a valid 5 GHz the secondary stays at 0 and the type 3 slots fall
to the primary (6).

### 3.2 Default sequence

`initDefaultChannelSequence` (`0xffffff80021c79c0`): fills the `n` slots with `_zeroChanInfo`,
writes slot 0 with `0x600000001` (`0xffffff80021c7a0e`) and flags 8
(`0xffffff80021c7a15`), and copies that entry to slot 8 (`0xffffff80021c7a29`,
`0xffffff80021c7a36`). Result: **slots 0 and 8 on channel 6, the other 14 on channel 0**.

### 3.3 Table per state

`setChannelSequenceByState` (`0xffffff80021f53aa`) indexes `__ZL20_channelSeqSlotTypes`
(`0xffffff80023aa1f0`) with `state << 6` (`0xffffff80021f5459`–`0xffffff80021f5460`), reads the slot
type (`0xffffff80021f547c`) and dispatches through a 21-case jump table based relative to its own
`jpt` (`movsxd` + `add` at `0xffffff80021f5491`–`0xffffff80021f5495`), i.e. **without bias**:
the case number is the slot type. Rows read from the table:

| State | Slots 0..15 |
|---|---|
| 0 Discovery | 3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7 |
| 3 Idle | 3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7 |
| 10 RealTimeData+InfraAssist | 9 ×16 |
| 11 …with Social | 9 9 3 9 9 9 9 9 9 9 4 9 9 9 9 9 |
| 29 Infra Priority | 9 9 3 1 1 1 1 9 2 9 3 9 1 1 1 9 |
| 41 High Thruput | 4 ×16 |

Sources per type in `setChannelSequenceByState` (`0xffffff80021f53aa`): 1 → `_zeroChanInfo`
(`0xffffff80021f549a`); 2 → primary `ivars+17056`/`+17064`; 3 → secondary `ivars+17068`/`+17072`
if the channel differs from 0, else primary; 4 → current AWDL channel `ivars+416`/`+424`; 7 → infrastructure channel
only if `addInfraToMinimumChannelSequence` (`0xffffff80021f5ef6`) allows it, and with
`ctrl+0x9BC == 0` the slot stays at zero; 9 → infrastructure if
`infraAllowedForNonRealtimeCases` (`0xffffff80021dc540`), and if not: slot 0 → channel 6 with flags
`0xA`, rest → channel 6 only with SDB active, state in the bit set and `ivars+65193` bit 0, in
otherwise zero.

Without an infrastructure channel and with 149 valid, the sequences become:

- State 0: `149 149 149 149 0 0 0 0 6 <current channel> 149 <current channel> 0 0 0 0`.
- State 3: `149 149 149 0 0 0 0 0 6 <current> 149 0 0 0 0 0`.
- State 10: `6 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0`.
- State 11: `6 0 149 0 0 0 0 0 0 0 <current> 0 0 0 0 0`.
- State 41: the current channel in the 16 slots.
- Without `configure`/strategy 1: the default sequence of §3.2 (6 at 0 and 8, zero on the rest).

### 3.4 Request 129

`setAwdlChannelSequence` (`0xffffff800221cedc`) validates the length (`a2[5]` between 1 and 32,
`0xffffff800221cef2`–`0xffffff800221cefc`), processes the DFS part (`0xffffff800221d0fc`), forwards the
payload to the driver with `apple80211RouteInternal(0x802869C8, 129, iface, a2)` (`0xffffff800221d12e`) and,
if the driver accepts, fixes the strategy with `setChannelSequenceStrategy(3, 3, true)`
(`esi=3` at `0xffffff800221d141`, `edx=3` at `0xffffff800221d146`, `ecx=1` at
`0xffffff800221d14b`), copies length, encoding, step and duplicate to `ivars+0xA744`, `+0xA772`,
`+0xA773` and `+0xA760` (`0xffffff800221d155`–`0xffffff800221d18f`), the entries to `ivars+0xA774`
(`0xffffff800221d1a3`–`0xffffff800221d1cc`) and the 400 B to `ivars+8`
(`0xffffff800221d1d1`–`0xffffff800221d1dd`). That is: **with request 129 the manager computes
no sequence; it adopts the userspace one, forwards it and freezes the strategy at
`AwdlUserConfigured`**. The concrete sequence with the infrastructure absent is decided by whoever emits the
129 `?`.

## 4. Queries on the primary interface and errors

### 4.1 Kernel side

`getInfrastructureState` (`0xffffff80021da84a`) does not emit apple80211 requests: it calls
`IO80211Controller::getInfraChannel` with an `apple80211_channel_data` on the stack, and if the channel is
not 0 it completes BSSID (`getBSSIDData`), SSID (`getSSIDData`), beacon period and DTIM
(`getBeaconPeriod`, `getDTIMInt`). With channel 0:

- if the previous cached channel was not 0, it logs `"lost infrastructure link (new channel
  %d)"` (`0xffffff80023d942b`, inside the same function), clears BSSID and periods and returns 1
  (state changed);
- if there never was an infrastructure channel, it returns 0.

Neither case propagates an error nor aborts AWDL. Callers:
`interfaceStateChange` (`0xffffff80021d9030`) and `reportDataPathEvents` (`0xffffff80021de938`); the
concrete event firing the second call stays `?`.

### 4.2 wifip2pd

The `wifip2pd` binary of 25G241 has its own database. Its known requests are
`RawGet` 12 (`CARD_CAPABILITIES`), `RawSet` 94 (`VIRTUAL_IF_CREATE`), `RawSet` 293 and `RawGet` 97; the
first three go on the AWDL interface handle that `sub_100047660` creates
(`0x100047660`), and the 16 infrastructure events are registered only for the role-0 handle in
`sub_100041B60` (`0x100041b60`). In this sweep no `CHANNEL`, `STATE`,
`BSSID` nor `CURRENT_NETWORK` request launched against the primary appears `?`; infrastructure information arrives
through events, not through polling. No gate aborting the creation of `awdl0`
when the primary is not associated has been found either `?`.

## 5. wifip2pd: infrastructure strings and handlers

| String | VA | Handler |
|---|---:|---|
| `Updated Infra Channel` | `0x100448c20` | `sub_100142BE0` (`0x100142be0`) |
| `Association Status` | `0x100448c60` | `sub_100142BE0` |
| `infraDisconnected` | `0x100449550` | `sub_10015CD80`, `sub_10015CE70`, `sub_10015CF30`, `sub_10015D020`, `sub_10015D0F0`, `sub_10015DB10` |
| `Infra association started on channel %s` | `0x1004a24e0` | `sub_1003925D0` |
| `Infra association failed on %s with status code %u (duration: %s)` | `0x1004a2440` | `sub_100392D90` |
| `Infra association complete` | `0x1004a2420` | `sub_1003932D0` |
| `Infra association ready` | `0x1004a2590` | `sub_1003973F0` |
| `Failed to start monitoring for events on infra interface: %@` | `0x1004a2850` | `sub_100395BC0` |
| `Failed to dispatch event %s because the InfrastructureWiFiInterface was deallocated…` | `0x1004a2890` | `sub_10038FF80` |

They are infrastructure event handlers and reporters; none of these strings appears in the
creation route of `awdl0` (`sub_100047660`, no references to infrastructure). Whether any of them
changes AWDL behavior stays `?`.

## 6. Consequences for the prototype

- Without association and with strategy 1, the manager does not turn AWDL off: election and the window
  schedule continue, and the sequence holds 149 in the type 3 slots, 6 in the type 2 ones and 0 in the
  type 7 ones (state 0: four 149 at the start, 6 at slot 8, rest 0 except the current channel).
- The default sequence (slots 0 and 8 on channel 6, rest 0) is the one appearing when there is no
  strategy 1 or when `setScheduleState` finds no infrastructure.
- The default master channel is 6; 149 is the secondary master. A radio fixed on 149 must
  advertise 149 on the slots it serves and 0 on the rest, not 6 at slot 0.
- The only shutdown for lack of infrastructure is in `monitorAWDLState` and requires the
  AirPlay scenario with services, no sockets and idle links; the warnings of `interfaceStateChange` and
  `setScheduleState` do not disable.
- The link-down gate of `interfaceStateChange` (`ctrl+0x560`, slot `+0xB88`, with
  `ivars+0x3EE4 != 0`) nullifies any state change if the consulted object returns 1;
  it is worth knowing what the project's own layer returns in that slot `?`.
- `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`) additionally copies the secondary master (149) to
  the current AWDL channel `ivars+416` when it is valid, so the type 4 slots also fall to 149.
  Without request 129, `RTLAwdInterface::servedMask` serves the 16 slots; with a 129 setting only
  the 6 (the default sequence of §3.2), a radio fixed on 149 serves none and does not transmit.

## 7. Pending

- Method of slot `+0xB88` of the object `ctrl+0x560` and whether it is the primary or the infrastructure.
- Auxiliary predicate of the `monitorAWDLState` block preceding the shutdown (exact composition).
- Whether the scenario without AirPlay services ever reaches that shutdown.
- wifip2pd requests on the primary other than the four verified ones, and whether any failure of
  the primary stops the creation of `awdl0`.
- Emitter of request 129 with the infrastructure absent and content of the sequence it sends.
