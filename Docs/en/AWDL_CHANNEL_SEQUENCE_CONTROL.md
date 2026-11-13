# Control of the AWDL channel sequence: who decides it, what feeds it, and what a radio that does not hop can do

How `IO80211AWDLPeerManager` decides the channel sequence advertised on the air (TLV 18 and the
sequence embedded in TLV 4), which inputs feed it, how it is pushed down to the driver, and what happens if the
driver rejects or ignores the order. It closes with the minimum the RTL8814AU layer must implement so
that the advertised sequence is honest. Complements `AWDL_PEERMANAGER_SOFTWARE.md` §6,
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7 and `AWDL_TLV_BODIES.md`.

Source: kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0), copy of the annotated IDB
`~/re_cache/ida/boot_awdl.kc.i64`, decompilation of the cited functions. All VAs
are instruction or data addresses read in that IDB. The ivar offsets belong to this build. Anything
unverified is marked `?`.

## 1. Summary

The 16-slot sequence advertised on the air **is not built by IO80211Family**: the kernel's
PSF and UMI templates (`updateSyncFrameTemplate`, `buildMasterIndicationTemplate`) do not contain
TLV 4 (sync params) or TLV 18 (chan seq); on Broadcom those two TLVs are composed by the firmware from
the programmed state. The kernel decides the **content** of the sequence and pushes it down to the driver
with internal ioctl 129 (`setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`, 400 B), plus the social
channels with the 132/205; the driver is the one that executes it (dwell per slot) and the one that renders the TLVs.

The kernel's decision is a three-level chain: a **strategy** (ivar `+4`: `AwdlFollowsInfra`,
`AwdlStateMachine`, `AwdlFixed`, `AwdlUserConfigured`), a **schedule state** (`scheduleState`,
chosen by `setScheduleState`) and a **static table** `__ZL20_channelSeqSlotTypes[state][slot]`
that assigns to each slot one of 21 types, each type with its channel source (zero = not served, social
channel, infrastructure channel, current channel, RT channel, …). The resulting sequence lives in
`ivars+42868` (16 entries of 12 B) and goes out through the 129.

A radio that does not hop channels can operate with an honest sequence without touching IO80211Family: the driver
receives the 129 and it is the one that renders TLV 18; it suffices that it advertises its fixed channel on the slots it
serves and channel 0 on the rest, with the TLV 4 presence mode consistent. The kernel already has
precedents of single-channel sequences: the default sequence puts channel 6 on slots 0 and 8
and zero on the rest, and during a roam all slots are filled with the same channel.

## 2. Where the project's own sequence lives

AWDL ivar block (`*(manager+0x20)`):

| Ivar | Field |
|---:|---|
| `+42864` | `u16` encoded length = `n · (step+1) · (dup+1)` (written by `setChannelSequenceUnconditional`) |
| `+42866` | encoding (0 = bare channel, 3 = channel/operating class pairs) |
| `+42867` | step count (sub-windows per slot) |
| `+42848` | duplicate `?` name |
| `+42856` | fill channel (`getChanSeqFillChan`, `0xffffff8002234ff8`) |
| `+42820` | `n`, number of declared slots (0..32) |
| `+42868` | entries of 12 B: `{u32 version/flags, u32 channel, u32 flags}` — the channel is the dword at `+42872`. The state machine fills the first 16; the copy loop of the 129 iterates up to `+42820`, which can reach 32 with a userspace sequence |
| `+43252` | copy of the previous sequence (384 B; saved by `setScheduleState` and `followPeerChannelSequence` with `real_mode_bootstrap_end`/memmove of 0x180) |
| `+8` | copy of the last 400 B sent to the driver (memcmp of 0x190 to suppress re-sends) |

`setMyChanSeq` (`0xffffff8002235302`) writes one entry: copies 12 B to `+42868+12·i` and, if the entry
marks 6 GHz (bit `0x20` of `+42877`) and `is6GChannelALlowedForP2P` rejects it, replaces it with
`_channel6Info` (with SDB active) or `_zeroChanInfo`. Only caller:
`IO80211RealTimePeerManager::updateAWDLChannelSequenceWithRtChannelSequence` (`0xffffff800234c05e`).
The same 6 GHz guard runs at the end of each slot in `setChannelSequenceByState`.

Fill globals (data):

| Address | Symbol | Content |
|---|---|---|
| `0xffffff8002488a88` | `__ZL13_zeroChanInfo` | `{1, 0, 0}` — **channel 0 = unserved slot** |
| `0xffffff8002488a90` | `dword_FFFFFF8002488A90` | the band dword of `_zeroChanInfo` (0) |
| `0xffffff80023aaff0` | `__ZL13_channel6Info` | `{1, 6, 10}` — channel 6 with flags `0xA` `?` semantics of bit 1 |

References read at `0xffffff80021f54ab`, `0xffffff80021f549e`, `0xffffff80021f5bb0` (inside
`setChannelSequenceByState`) and at `0xffffff80021c79d8`/`0xffffff80021c79e2` (inside
`initDefaultChannelSequence`).

## 3. Who decides the content: strategy → state → table

### 3.1 Strategy

`setChannelSequenceStrategy(new, reason, force)` (`0xffffff80021ca958`) saves the strategy in the
ivar `+4` of the AWDL block. Names of the table `0xffffff800245B6D0`:

| Value | Name |
|---:|---|
| 0 | `AwdlFollowsInfra` |
| 1 | `AwdlStateMachine` |
| 2 | `AwdlFixed` |
| 3 | `AwdlUserConfigured` |

Callers (verified xrefs): `configure` (`0xffffff80021c54ea`, sets 1 with reason 0),
`setAwdlCtl` (`0xffffff80021cde94`, sets it from the userspace request with reason 3 and relaunches
`setScheduleState` with reason `0x15` and `programMasterChannels`), `interfaceStateChange`
(`0xffffff80021d8cc8`, at `0xffffff80021d900a`), `monitorAWDLState` (`0xffffff80021ed724`, at
`0xffffff80021ee3ae`), `setAwdlChannelSequence` (sets 3 when accepting a userspace sequence),
`setAwdlAutoMode` and `setAwdlOperatingMode` (set 1, reason 6), and `enableAWDL`
(`0xffffff80021dcf58`): if there are valid AWDL services (`+42568`), it chooses 0 or 1 (reason 4) based on
`+16289` and the counters `+42660`/`+42588` `?` exact condition of the value 0/1.

### 3.2 Schedule state

`setScheduleState(scheduleState, reasons, force)` (`0xffffff80021d27a0`) is the orchestrator. Verifiable
early exits: the gate `+42560` (activated when `setAwdlSyncParams` successfully completes an
ioctl 116, see §4), AWDL enabled and connected (`+16096` bits 0 and 1), no disable in progress (`+414`),
and the SDB exclusion modes in progress (`+16505`, `+66560`). If there is a peer whose sequence to follow
(`+16248` or the peer of `+16224` with the bit of `+1665`), it calls `followPeerChannelSequence` and does not touch
the table. Otherwise it applies one of three paths (lines of the decompilation):

1. `setChannelSequenceByState(state)` (`0xffffff80021f53aa`, internal call) — strategies 1/0 with
   a per-state table;
2. `initDefaultChannelSequence()` (`0xffffff80021c79c0`) + `setChannelSequence(force)`
   (`0xffffff80021f7f78`) — default path;
3. `followPeerChannelSequence(peer)` — follow mode.

The names of the 56 states are in the table `0xffffff8002459C20` (`StringForAwdlState`,
`0xffffff80021a8b9e`). The ones appearing in the decompilation of `setScheduleState` and in the slot
table: 0 Discovery, 1 Discovery+InfraStream, 3 Idle, 5 Data, 9 RealTimeData, 10/11
RealTimeData+InfraAssist, 12–21 Split Infra variants, 23 IdleAndRealTimeData, 24/25/26/27/28 Scan,
29 Infra Priority, 30/31 Infra BE RealTime, 43 Ranging, 45/47/48 AWDL Fast Discovery, 49–52 ProMode,
53 Long Discovery, 54 Ultra Low Latency Infra, 55 RTG AirDrop. The current state lives at `+10468`.

### 3.3 The slot type table

`__ZL20_channelSeqSlotTypes` at `0xffffff80023aa1f0` (reference read at instruction
`0xffffff80021f5475` of `setChannelSequenceByState`): one dword per slot, 16 slots per state.
The index is direct: `state << 6` (`shl rax, 6` at `0xffffff80021f545c`) plus `slot·4`, and the
`switch` at `0xffffff80021f5480` has 21 cases (types 0..20). There is one row for each of the 56
states. Relevant rows:

| State | Slots 0..15 (type) |
|---|---|
| 0 Discovery | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` |
| 3 Idle | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` |
| 5 Data | `0 ×16` |
| 9 RealTimeData | `0 ×16` |
| 10 RealTimeData + InfraAssist | `9 ×16` |
| 11 RealTimeData + InfraAssist with Social | `9 9 3 9 9 9 9 9 9 9 4 9 9 9 9 9` |
| 12 RealTimeData + Split Infra Legacy | `12 12 12 12 5 5 5 5 12 12 12 12 6 6 6 6` |
| 14 RealTimeData + Split Infra + Same DFS | `12 5 5 5 5 5 5 5 5 12 5 5 5 5 5 5` |
| 29 Infra Priority | `9 9 3 1 1 1 1 9 2 9 3 9 1 1 1 9` |
| 41 High Thruput | `4 ×16` (current AWDL channel on all slots) |
| 45 AWDL Fast Discovery | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` |

Type 0 means "do not rewrite this slot". The fully-zero rows are those of states 5 Data,
9 RealTimeData, 28 Piggyback Scan + Data, 32 Forced, 39 Sidecar Start, 40 RtMode Start, 44
RealTimeGroup and 55 RTG AirDrop. In them the kernel does not recompute the sequence and advertises whatever is already in
`+42868`, which is exactly where `followPeerChannelSequence`, `setMyChanSeq` (RTG) and the
userspace sequence leave their result.

### 3.4 Sources per slot type

`setChannelSequenceByState` (`0xffffff80021f53aa`) walks 16 slots (`+42868` in steps of 12) and applies the
type read from the table. Types verified in the `switch`:

| Type | Source |
|---:|---|
| 0 | not written (slot intact) |
| 1 | `_zeroChanInfo` — **channel 0** |
| 2 | primary social channel (`+17056`) |
| 3 | secondary if `+17072 ≠ 0`, else primary |
| 4 | current AWDL channel (`+416`/`+424`) |
| 5/6 | pairs `+17080/+17088` and `+17092/+17100` `?` semantics (presence/assist channels) |
| 7 | infrastructure channel (controller `+2492`/`+2500`) if `addInfraToMinimumChannelSequence` (`0xffffff80021f5ef6`) allows it; else zero |
| 8 | infra if `+42568 ≠ 0` (AirPlay services counter `?`), else zero |
| 9 | infra if `infraAllowedForNonRealtimeCases`; else: slot 0 → infra if `+16089` and there is an infra channel, else channel 6; slots > 0 → channel 6 only with SDB active, bit `+65193`, state ≤ 0x30 and the bitset `0x1A00000000000`; otherwise zero |
| 10 | channel saved at `+64008` if `+64012 ≠ 0`; else social peer `+17056`/`+17068` |
| 11/14 | like 10 but with a 1 s limit (marks `+64040`/`+64048` measured with `absolutetime_to_nanoseconds`) and fall back to the infra channel before the social |
| 12 | peer `+440/+448` `?` semantics |
| 13 | mix of the discovery sequence (`+64008..+64028`) with the social peers; marks match bitmaps at `+64032`/`+64036` |
| 15 | peer `+452/+460` `?` semantics |
| 16 | RT channel `+65404` if `+65408 ≠ 0`, else current channel |
| 17/18/19/20 | mixes of RT channel / infra / social / channel 6 depending on SDB, DFS and `infraAllowedForNonRealtimeCases` |

After each slot: if the entry marks 6 GHz and it is not allowed, it is replaced with `_channel6Info` (slot
0 or SDB active) or `_zeroChanInfo`.

### 3.5 Default sequence

`initDefaultChannelSequence` (`0xffffff80021c79c0`): puts `_zeroChanInfo` in the `n` slots of
`+42820` and then hand-writes slot 0 = channel 6 (`0x600000001`, band `8`) and copies that entry
to slot 8 (`+42964`). It is the kernel equivalent of the on-air pattern: social channel on 1–2 slots
of 16 and channel 0 on the rest (owly AWDL-12/16, `AWDL_REFERENCIAS_EXTERNAS.md` §6).

### 3.6 Following a peer's sequence

`followPeerChannelSequence` (`0xffffff80021f6e74`): called from
`IO80211AWDLPeer::updateAwdlChanSeq` (`0xffffff80021ab7e4`, two xrefs), `parseAWDLRangingTLV`,
`parseAwdlDataPathTLVAndTakeAction`, `setScheduleState`, `setAirPlayState` and
`enterAssistedDiscoveryFollowMode`. Copies the `n` 12 B entries of the peer — read from the peer block
starting at `peer+920` (channel at `+924`), in the same three-dword format — to `+42868`, saves the
previous one at `+43252`, applies the 6 GHz guard, sets `+10468 = 9` (RealTimeData) or overlaps a pending scan,
and calls `setChannelSequenceUnconditional(true)`. With RTG active
(`+64418`) instead of following it reprograms the RTG configuration (`reevaluateRTGConfiguration`).

### 3.7 Roam: the "all slots equal" precedent

`setRoamChannelSequence(channel)` (`0xffffff80021e5b74`): raises `+66465` (roam in progress) and fills
**all** slots of `+42868` with the same channel (with the 6 GHz guard), then
`setChannelSequenceUnconditional(true)`. Proof that the contract admits a constant
single-channel sequence with no change whatsoever in the kernel. State 41 High Thruput does the same
through the table: type 4 (current AWDL channel) on all 16 slots (§3.3).

## 4. Inputs that feed the sequence

| Input | Verified path |
|---|---|
| Strategy (ivar `+4`) | `configure`, `setAwdlCtl`, `interfaceStateChange`, `monitorAWDLState`, `setAwdlChannelSequence` (userspace ⇒ 3), `setAwdlAutoMode`/`setAwdlOperatingMode` (⇒ 1), `enableAWDL` (§3.1) |
| Schedule state (`scheduleState`) | `setScheduleState` `0xffffff80021d27a0`; the table `__ZL20_channelSeqSlotTypes` chooses sources per slot |
| Static table | `0xffffff80023aa1f0` (§3.3) |
| Social channels | `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`, called from `interfaceStateChange`): chooses among 6/44/149 based on the master channel `+17044` and the secondary `+17048`; mask at `+17052` (bit0=6, bit1=44, bit2=149); infos at `+17056`/`+17068` |
| Infrastructure channel | controller `+2492` (channel) / `+2500` (flags; `0x2000` blocks the choice of channel 6/RT `?` name, `0x800/0x400/4` = width class, `0x10` = `?`), used by types 7/8/9/10/11/14/17/19 |
| Userspace sequence | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` (type 129) from `setAWDL_...` handler `0xffffff8002288cfe` → `setAwdlChannelSequence` (`0xffffff800221cedc`): validates `n ≤ 32`, processes DFS, forwards the 129 to the driver, saves the sequence as its own and sets strategy 3 |
| A peer's sequence | TLV 18/32/4 parsed → `updateAwdlChanSeq` → `followPeerChannelSequence` (§3.6) |
| RTG / AirPlay | `IO80211RealTimePeerManager::updateAWDLChannelSequenceWithRtChannelSequence` → `setMyChanSeq`; `setRealTimePeerSession` and `resolveAWDLChannelConflict` (verified xrefs to the emitter of the 129) |
| Roam | `setRoamChannelSequence` (§3.7) |
| Presence mode | `setAwdlPresenceMode` (`0xffffff800221e806`, type 136 from userspace, requires value ≤ 0x3F) and `setPresenceMode` (`0xffffff80021cc5a6`, internal ioctl `0x88802869C8` with type 136): both leave the mode at `+17032` and push it down to the driver with the 136. It writes no slot of `+42868`; its effect on the sequence is indirect (sources 5/6 and the render the firmware makes of TLV 4) `?` |
| Extension parameters | `setAwdlExtensionParams` (`0xffffff800221bc44`, type 118 from userspace; −1 = "use the current one") and `setAwdlExtensionParameters` (`0xffffff80021cc670`, internal ioctl `0x76802869C8` type 118): 4 bytes at `+17035..+17038` (the ext bytes 1–4 of TLV 4, `03 03 03 03` on the air) |
| SDB | flags `+43636`: `setChannelSequenceUnconditional` adds `0x10` (SDB ON) or `0x20` (OFF) to them depending on `dynSdbForceChanSeq` (`0xffffff80021f8128`), the state `+16512`, and the bits `+16501/+16503/+16504`; events 64/160 (`SDB_MODE_CHANGED`) rebuild the template and the MI (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.6) |
| Sync params (gate) | `setAwdlSyncParams` (`0xffffff800221b73c`, type 116 from userspace): saves period/length/ext (`+62940/+62944/+62948/+62952`) and activates the gate `+42560` without which `setScheduleState` and `setChannelSequence` do nothing |
| Country / DFS | `countryCodeStateChanged` (cell 42, `0xffffff800220dca4`); `processAWDLDFSChannelSequence` (`0xffffff800221a466`) runs before each emission of the 129 and in `setAwdlChannelSequence`; `APPLE80211_M_AWDL_DFS_CSA` handled in `reportDataPathEvents` |
| `awdl_cap` | read `0x155C02869C9` in `enableAWDL`: decides `SEC_PAYLOAD`/`CCA_STATS`/`PSF_DWELL_*`, does not alter the slots |
| Boot-args | `initWithInterface` reads `awdl_allow_40`, `awdl_max_bw`, `awdl_election_id`, … (`AWDL_PEERMANAGER_SOFTWARE.md` §2.2); which of them conditions the channel sources of the table has not been traced slot by slot `?` |
| 6 GHz | global guard `is6GChannelALlowedForP2P` in `setMyChanSeq`, `setRoamChannelSequence`, `followPeerChannelSequence` and at the end of each slot of `setChannelSequenceByState` |

## 5. Push-down to the driver and return from the air

### 5.1 The ioctl 129 and the emission

`setChannelSequenceUnconditional(force)` (`0xffffff80021cb69a`):

1. Composes the 400 B payload: `+0 = 1` (version), `+4` = flags of `+43636` with the SDB bits of §4,
   `+5 = n`, `+6` encoding, `+7` step, `+8` dup, `+9` fill, 12 B entries from `+12`
   copied from `+42868` (same layout as `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7). Detects whether there are
   2.4 GHz channels (channel 1–14 with bit 13 of flags at 0) and marks `+63023`.
2. Sets the encoded length `+42864 = n·(step+1)·(dup+1)`; if the `u16` result reaches 513
   (`(len-1) >= 0x200`), it logs the assert and does not emit.
3. Suppression: `memcmp` of 0x190 against `+8`; if nothing changed and not forced, it does not emit.
4. `processAWDLDFSChannelSequence` and emission with `ioctl_internal` and command `0x81802869C8`
   (type 129) — the decompilation shows `v95 = 0x81802869C8` before `ioctl_internal`.
5. Success: saves the copy at `+8`, `updateBroadcastMI`, and if the sequence changed:
   `updateMulticastQueues`, refresh of cached peers (`_updateCachedPeers`), publication of
   message 195 to the interface with the 400 B (via the vtable `+2840` of the interface
   `*(manager+24)+1376` `?` symbolic name of the message) and rearm of timer `+10376`. Failure: see §6.

The emission is triggered by (xrefs of §3): `configure`, `enableAWDL`, `setChannelSequence` (gate
`+42560` and AWDL up), `followPeerChannelSequence`, `setRoamChannelSequence`, events 198
(6 GHz scan overlay, in `reportDataPathEvents`) and RTG reevaluation, among others.

The social channels are pushed down separately in `programMasterChannels` (`0xffffff80021d494a`): 132
(`0x84802869C8`, channel `+17060`) and 205 (`0xCD802869C8`, channel `+17072`), after
`updateFWSocialChanListWithPSFSuppressionInfo` (`0xffffff800220db4e`) if applicable.

### 5.2 Who builds the on-air TLVs

- The kernel templates carry neither TLV 4 nor TLV 18: `updateSyncFrameTemplate`
  (`0xffffff80021dc5b2`) composes TLVs 6, 24, 12 (data path state, `buildAwdlDataPathStateTlv`
  `0xffffff8002210e5a`), 7, ranging, RTG, 17, 21, 20, NAN sync and 33 (pref chan,
  `buildPrefChanTlv` `0xffffff8002211416`); `buildMasterIndicationTemplate`
  (`0xffffff8002211b42`) adds 29 (BSS steering), ProMode and AirDrop. In neither one is there a
  constructor of type 4 or 18.
- The Broadcom side confirms the split: `AppleBCMWLANProximityInterface::buildAWDLChanSequence`
  (`0xffffff80017258bc`) converts the `apple80211_awdl_sync_channel_sequence` of the 129 to the firmware
  format (`len-1`, encoding, dup, step, fill as `u16` `0xFFFF` if it equals 255, and entries of
  **2 B**; with encoding 3 it computes the ChanSpec with `getChanSpec` and writes it big-endian with
  `ROL2(x,8)`; slots with channel 0 stay at zero). Broadcom-specific detail: if slot 0
  arrives at 0, it forces it to channel 6 ("chan_sequence[0]==0, forcing to 6"). TLV 4 is composed by the
  firmware with the period of the 116, the presence of the 136 and the ext bytes of the 118 — the template of
  the 151 carries neither timing nor presence (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11), so that is
  the only place it can be born `?`.
- The kernel only **reads** those two forms on reception: `parseAwdlChanSeqFromChanSeqTLV`
  (`0xffffff80021b5a08`), `parseAwdlChanSeqFromChanSeqExtTLV` (`0xffffff80021b5e84`) and
  `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`) per `AWDL_TLV_BODIES.md`.

### 5.3 Per-slot execution

The kernel does not order channel hops per slot: it delivers the full sequence (129) and the
driver/firmware does the dwell; events 41/42/43 of `reportDataPathEvents`
(`0xffffff80021dd8f4`) return the timeline. In the branch of event 43, after closing the window,
if `+17032 ≠ 0` the manager calls `IO80211Controller::setCurrentChannel`. The decompilation only
shows the controller as an argument (`?` if the convention hides another one), and there is no per-slot
channel computed before the call: it reasserts the channel the controller already considers current, it does not
program a hop. The slot
index the manager uses from event 41 is `min(15, (AwNumber % ((+42867+1)·+42820))/(+42867+1))`
(formula of `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2, written at `+16128`; `getCurrentAWSlot`
`0xffffff800223508a` returns it).

### 5.4 Read-back

`getAwdlChannelSequence` (`0xffffff800221f7bc`) asks the driver first (read ioctl
`0xC02869C9`, type 129); if the driver answers `0xE00002C7` (unsupported), it fills the response from
its ivars; if it answers fine, it compares encoding (`+42866`), length (`+42820`) and step (`+42867`)
between "FW" and "Family" and logs `"Parameter mismatch ..."` if they differ. It is the only coherence
mechanism between what was programmed and what the driver claims to have.

## 6. What happens if the driver rejects or ignores

- **Rejection of the 129**: `setChannelSequenceUnconditional` logs
  `"Couldn't set channel sequence! ..."`, zeroes the copy `+8` (the next attempt re-emits) and
  calls `updateBroadcastMI`. Also, after any attempted emission — success or failure, it is the
  same epilogue block — the schedule is reprogrammed with `setAWDLStateStop` +
  `setAWDLStateStart(state, awdlDutyCycle)`. It does not retry inline: the watchdog
  `monitorAWDLState` (`0xffffff80021ed724`, xref at `0xffffff80021edfcc`) calls
  `recoverFromSetChanSeqFailure` (`0xffffff80021f3710`), which clears `+16912` and relaunches
  `setScheduleState` (rebuilds the sequence and emits the 129 again) if there is AirPlay/RTG active.
- **Rejection of the 132**: `programMasterChannels` logs `"Unable to set master channel ..."` and aborts
  without emitting the 205. Failure of the 205: logging only.
- **Silent ignoring** (answering 0 without applying): the manager takes the sequence as programmed (copy
  `+8`, event 195). The discrepancy only manifests as traces: `monitorTimeline` compares the
  channel and the `AwNumber` of each event 41 against the sequence and logs discontinuities without
  resynchronizing (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.3), and the FW/Family comparison of §5.4 only runs
  if someone does a `get`. Neither one corrects the advertisement: **the one rendering TLV 18 is
  the driver**, so an ignored sequence becomes a lying advertisement without any kernel log
  betraying it.

## 7. Minimum for the RTL8814 to advertise an honest sequence

The premise of the scenario is a radio that cannot keep hopping channels on its own (whether the T9UH
under this stack can or wants to is an open question `?`); the contract allows it because the render of
the TLVs is the driver's job. Minimum verified against the code:

1. **Accept and register the ioctls**: 129 (sequence, 400 B per §5.1/`AWDL_PEERMANAGER_DRIVER_ABI.md`
   §3.7), 132/205 (social channels), 136 (presence, `u32 ≤ 0x3F`), 118 (ext bytes, 20 B), 116
   (periods; its success activates the gate `+42560` that unlocks the machine) and 117 (enable, the only one with
   revert). Answer 0 to all of them unless there is an explicit contrary policy.
2. **Answer the read of the 129** (`0xC02869C9`) with the same thing accepted by write; if not implementing it is
   preferred, return `0xE00002C7` and the manager will use its copy — coherence stops being
   verifiable from outside.
3. **Honest render of TLV 18** from the received 129: real channel of the radio on each slot the
   driver is going to serve, **channel 0 on those it is not**, preserving encoding (if it equals 3, the pair
   {channel, operating class} must correspond to the width actually tuned; winawdl measured the cost
   of advertising 40 MHz and being on 20 MHz), step and fill. Refer to the precedents: the Broadcom
   firmware forces slot 0 to 6 if it arrives at zero; the kernel admits constant (roam) and
   two-slot (default) sequences.
4. **Presence mode of TLV 4 consistent with TLV 18**: the slots with AW in the `pm` byte
   (`+30` of TLV 4) must be exactly those TLV 18 advertises with a channel; the driver builds
   both TLVs, and the ext bytes come from the 118. A channel advertised on unserved slots loses the
   peer's frames (owly AWDL-12).
5. **Events 41/42/43** aligned with the project's own schedule (§5.3 and `AWDL_PEERMANAGER_DRIVER_ABI.md`
   §2): without the 41 the manager does not enter the window; discrepancies of `AwNumber`/channel only produce
   logs, so the honesty of the render cannot be delegated to kernel validation.
6. **Do not suppress the 129 by answering 0 without applying it**: if the programmed sequence cannot
   be executed, rewrite it in the render (point 3) and do not fake acceptance; the only reaction of the
   kernel to a declared failure is to rebuild and reprogram (`recoverFromSetChanSeqFailure`), which is
   the desirable path.

With 1–6, the sequence the `IO80211AWDLPeerManager` "advertises" (its copy `+42868`, event 195 and
the real on-air TLVs) differs only on the slots the driver declares unserved with channel 0 —
exactly the pattern fixed-radio implementations (owly, winawdl, ESP32Drop) use to
interoperate without hopping channels.

## 8. Open items

- Semantics of the channel sources `+17080/+17088`, `+17092/+17100`, `+440/+448`, `+452/+460`
  (types 5/6/12/15) and of the bits of the controller's `+2500` `?`.
- Which boot-args of `initWithInterface` (`awdl_allow_40`, `awdl_max_bw`, …) in practice condition
  the sources of the slot table `?`.
- Exact conditions under which `enableAWDL` chooses between `AwdlFollowsInfra` (0) and `AwdlStateMachine`
  (1) at boot `?`.
- Whether any kernel path ever builds TLV 18 outside the driver (no constructor found; type 33 pref chan and 12 data path state are the only channel TLVs built
  in software) `?`.
- Unit and semantics of the flags byte `0xA` of `_channel6Info` (bit 1) `?`.
