# The «presence posted» flag of `AWDLPeer` and the `DataPathState+64` byte in `wifip2pd`

Closes the two points of
[AWDL_WIFIP2PD_PRESENCE_TRIGGER.md](AWDL_WIFIP2PD_PRESENCE_TRIGGER.md) §7
about the Bool that limits presence publication to once per peer and the `+64` byte of the
`AWDLActionFrame.DataPathState`. Base: `wifip2pd` of 25G241, IDB copied to `/tmp/awdl_once`
(source `~/re_cache/ida/wifip2pd_25G241.i64`). Binary addresses unless noted. Field
and case names come from the Swift reflection field descriptors
(`__swift5_fieldmd`), not assumptions; anything unchecked carries `?`.

## 1. Short answer

- The Bool is the field **`peerPresencePosted`** (index 23, type `Bool`) of the
  `CoreP2P.AWDLPeer` struct (field descriptor at `0x1004c7898`). Every access goes through the
  field offset vector of the metadata: `movsxd r, dword ptr [metadata+6Ch]` (the slot of field 23).
  The byte offset inside the struct is not statically visible: the type is instantiated at runtime
  (resilient struct) and the module code never uses it as a constant.
- It is born **0** in the struct init (`0x10016f71f`) and becomes **1** on the event 48 path
  after publishing presence (`0x10041ce49`). The modified peer is persisted into the interface
  peer store (a `Dictionary`) in `sub_10041FD50` (`0x100420123`), the only caller of the store
  setter in the whole binary.
- It returns to **0** through three paths: the init of any new `AWDLPeer`, the aged-out (the peer
  is removed from the store by key and rebuilt), and the interface recovery (massive reset of the
  flag in all store peers after the full NDP wipe). The «Driver interface was removed» does not
  touch the peer flag.
- The comparison of `0x100305aa7` (initiator DPResp path) **is not this flag**: it is another Bool
  inside an ivar of the initiator session object, state separate from the peer store
  (correction of §3.3 of the trigger doc).
- The byte `DataPathState+64` is the **inline tag of `unicastOptions: UMIOptions?`** (field 8 of
  the `DataPathState` field descriptor): `0x31` (49) is `nil` and values `0x00`–`0x30` are the
  cases of the enum `UMIOptions`. It governs neither presence nor absence: it decides whether the
  event 48 activates a traffic registration.

## 2. The `AWDLPeer` struct and its store

Field descriptor of `CoreP2P.AWDLPeer` at `0x1004c7898` (26 fields, 12 B records with flags 2,
names verified): `logger`, `macAddress`, `lastUpdated`, `numberOfActionFrames`,
`deprecatedElectionParameter`, `electionParameter`, `serviceParameter`,
`lastProcessedServiceUpdateIndicator`, `synchronizationParameter`, `arpa`,
`dataPathStateParameter`, `nanSynchronization`, `version`, `realtimeGroup`, `preferredChannels`,
`advertisedServiceIDPool`, `advertisedServices`, `syncContention`, `mactime`, `average2_4RSSI`,
`average5RSSI`, `average6RSSI`, `reachability`, **`peerPresencePosted`** (`Sb`),
`needsToRecoverTrafficRegistrations` (`Sb`), `needsToRecoverResponderTrafficRegistrations`
(`Sb`). The enum `AWDLPeer.Reachability` (descriptor `0x1004c79e0`) has the cases `routable`
(tag 0), `observed` and `notReachable`.

The three final Bools are read from the metadata as `movsxd rax, dword ptr [metadata+slot]` with
slots `+6Ch` (field 23), `+70h` (24) and `+74h` (25).

The peer store is a `Dictionary` ivar of the self object of the presence functions
(standard access `[class+1C0h]` at `0x10040f183`, `0x1004200f1`, `0x100428fb0`, with
`swift_beginAccess` READ at `0x10040f199`, MODIFY at `0x100420110` and `0x10041a6d9`). The lookup
is done in `sub_10040F120` (`0x10040f1cc` with the comparators `sub_10013BF40`/`sub_1000270F0`,
per-stride element with `imul rbx, [metadata-8+48h]` at `0x10040f1f6`); if it does not exist, it
builds a peer with the init `sub_10016F4C0` (`0x10040f2b0`). The exact correspondence between slot
`+1C0h` and a field of the field descriptor of `wifip2pdCore.AWDLInterface` (FD `0x1004d1ad0`,
field 0 `peers`, a `Dictionary`) does not close by the `FieldOffsetVectorOffset` mapping `?`; the
use as store is accredited by the four cited accesses and by the remove-by-key of §4.2.

## 3. Writers and readers of `peerPresencePosted`

Complete sweep: the 91 functions referencing the metadata accessor of `AWDLPeer`
(`0x10016e690`) plus the direct callers of the property accessors (none: the use is
always inline). Accesses to slot `+6Ch`:

| Function | VA | Operation |
|---|---|---|
| `sub_10016F4C0` (init of `AWDLPeer`) | `0x10016f71b`–`0x10016f71f` | `mov byte [peer+FOV], 0`: to zero |
| `sub_10016F350/370/390` (getter/setter/`_modify` of the property) | `0x10016f37b`, `0x10016f39f`, `0x10016f3cb` | standard accessor; no direct callers |
| `sub_10041C470` (event 48) | `0x10041cbc2`–`0x10041cbc6` | read: `cmp byte [box+FOV], 0` |
| `sub_10041C470` (event 48) | `0x10041ce49` | write: `mov byte [box+FOV], 1` |
| `sub_100427D70` (aged-out) | `0x100428105`–`0x10042810e` | read: `cmp byte [peer+FOV], 1` |
| `sub_100428F70` | `0x100428f80`–`0x100428f84` | write: `mov byte [peer+FOV], 0` |
| `sub_10016F7C0` | — | references the accessor; does not touch slot `+6Ch` |

### 3.1 The event 48 path (single publication)

In `sub_10041C470` (single call from the dispatcher at `0x10041a216`):

1. Two guards of the interface on the self: `[class+2F8h]` must be non-zero
   (`0x10041caf0`–`0x10041caff`; if 0 jumps to `0x10041fd35` without processing) and
   `[class+2C8h]` must be 0 (`0x10041cb08`–`0x10041cb13`). These are accesses through the field
   offset vector of the self class; their names do not appear in any consulted field descriptor `?`
   (the values 760 and 712 of the trigger doc are the same offsets in decimal).
2. The peer box is obtained from `sub_10040F120` (`0x10041cb64`): it copies the peer from the
   store or creates a new one (with `peerPresencePosted = 0`).
3. If the Bool is 0 (`0x10041cbc6`), it calls `sub_1002823D0` (`0x10041cc69`, publication into
   the NDP and send of the 415) and writes 1 (`0x10041ce49`). If it is 1, it jumps straight to
   `0x10041ce87` without publishing.
4. `sub_10041FD50` (`0x10041ced8`) persists the peer: `swift_beginAccess` MODIFY on the store
   (`0x100420106`–`0x100420110`) and write with `sub_1003A7E90` (`0x100420123`; only caller
   of the setter in the binary, verified by sweeping `E8`). The same function handles the
   other two Bools of the peer (`needsToRecoverTrafficRegistrations`, slot `+70h`, read at
   `0x100420149`) and their recovery (`sub_100421410`).

### 3.2 Aged-out (`sub_100427D70`)

The aged-out is invoked from the dispatcher with a peer decoded from the event (`0x10041ab67`)
and from `sub_1004291D0` (`0x1004292b1`, the cleanup after interface recovery). In
the dispatcher, the jumptable case that leads to it (case 8 at `0x10041a6a5`) does:

- `swift_beginAccess` MODIFY on the store (`0x10041a6cc`–`0x10041a6d9`) and remove by the MAC
  of the event with `sub_1003A82F0` (`0x10041a6eb`): the peer leaves the store.
- If the peer existed, it is decoded (`sub_100031330`) and passed to the aged-out (`0x10041ab67`).

Inside the aged-out: it rebuilds an `AWDLPeer` with the init (`0x1004280d1`) and copies the
`advertisedServices` of the old one (`sub_10035B720`, `0x1004280e6`); then it reads the Bool of the
old peer (`0x100428105`): if it was 1, it publishes the absence with `sub_100282980`
(`0x10042817e`, the method of `"Posted peer absence for %s on %s"`); if it was 0, it jumps to
`0x100428343` without absence. Its own log is `"Aged out %s with total traffic registrations:
%ld, total responder traffic registrations: %ld"` (format cited at `0x100428026`) and the failure
one is `"Failed to post peer absence for %s: %@"` (at `0x100428294`).

### 3.3 Massive reset on interface recovery

In the dispatcher `sub_1004184D0`, the interface state event (case 4 of the `DriverEvent` switch,
with a substate in the payload) has the interface-ready branch:

- `sub_100282E30` (`0x10041b079`): `Dictionary.removeAll` of the neighbor table (the full wipe of
  `sub_1002823D0`/`sub_100282980` of the trigger doc §2).
- Error log `"Failed to clear all previous peer presence after interface recovery"` (format
  loaded at `0x10041b0d3`, `_os_log_impl` at `0x10041b0e9`).
- If there was no error: `sub_100428F90` (`0x10041b127`), which iterates the whole store
  (`swift_beginAccess` at `0x100428fc6`, `_HashTable.startBucket.getter` at `0x100429001`) and
  applies the callback `sub_100428F70` to each peer: `mov byte [peer+FOV], 0` at `0x100428f84`. All
  `peerPresencePosted` return to 0.
- Then: `sub_1004291D0` (`0x10041b12c`), the Bool of the interface to 0
  (`mov byte [self+FOV], 0` at `0x10041b13c` with slot `[class+2C8h]`) and `sub_1004292F0`
  (`0x10041b142`). The logs of the same dispatcher branch: `"Failed to recover the low latency
  interface %s: %@"` (format loaded at `0x100419a2a`) and `"Failed to suspend AWDL after
  interface recovery: %@"` (format loaded at `0x10041aeb7`).

### 3.4 Interface down does not touch the flag

The branch `"Driver interface was removed"` (jumptable case 6, writes at `0x100419371`–
`0x100419398`) only touches the three Bools of the interface: the one at slot `2C8h` to 1, the
one at `2D8h` to 0 and the one at `2D0h` to 0. The `peerPresencePosted` of the peers does not
change. The effect is indirect: with the Bool of slot `2C8h` at 1, the guards of `sub_10041C470`
(`0x10041caff`/`0x10041cb13`) abort all processing of event 48 until recovery.

### 3.5 When can the 415 repeat for the same peer?

| Event | Effect on `peerPresencePosted` | New 415 possible |
|---|---|---|
| First AF of the peer (new or rebuilt peer) | 0 → publishes → 1 | the first one |
| Following AFs while the peer persists | stays 1 | no |
| Aged-out of the peer (dispatcher case 8, registration inactivity) | the peer is removed from the store; rebuilt with 0 and absence published if it had 1 | yes, with the next AF |
| Interface recovery («ready» substate) | massive reset to 0 + `removeAll` of the NDP | yes, for all peers |
| «Driver interface was removed» | no change (only the interface Bools) | no (moreover the guards block the 48) |
| Box destruction without remove from the store | the value persists in the store | no |

## 4. The comparison of `0x100305aa7` is another Bool

`sub_100305990` (single call from `sub_1002FF710` at `0x100302802`, manager of the initiator's
datapath response) operates on the object `r13` the caller passes it:

- `0x100305a38`–`0x100305a3f`: store over the self ivar whose offset comes from
  `[class+0D8h]` (`cmp byte [r13+r12], 0`, set to 1 at `0x100305adc` after publishing).
- `0x100305a4b`–`0x100305a58`: `cmp byte [r13+rax+10h], 3` over the self ivar whose offset comes
  from `[class+0D0h]`.
- `0x100305a92`–`0x100305aa7`: `and rax, isaMask` over `[r13+0]`, then
  `mov rax, [class+0A0h]` and `cmp byte [r13+rax+6], 1`: byte +6 of the ivar (6 B MAC at
  `+0..+5`, extracted at `0x100305aca`–`0x100305ad9`). If it is 1, the function returns without
  publishing (`0x100305aaf`); if not, it publishes (`sub_1002823D0` at `0x100305bc9` and
  `0x1003063c4`) and sets the other ivar's flag to 1 (`0x100305adc`).

That Bool lives in the ivar of a class object (offsets of the field offset vector of its class),
not in the box of `AWDLPeer`, and therefore is not `peerPresencePosted`. The metadata
`$s17InitiatorInstanceC12PeerIdentityVMa` appearing at `0x1003059c3` is a two-case enum
(`pairingKeystoreID`, `pairedDeviceID`, field descriptor `0x1004cfda8`) used as a value, not the
type of the tested ivar: `InitiatorInstance` has 30 fields (FD `0x1004cfc30`) and the slots
`0A0h`/`0D8h` (indices 36 and 50 over a vector at `+10h`) are out of range. The exact class
of object `r13` was not identified by name `?`; the caller additionally instantiates
`wifip2pdCore.NANPeer` (accessor called at `0x1002ff77f`).

Practical consequence: the event 48 path and the DPResp path each carry their own «presence
already published» flag; the event 48 one governs the main 415.

## 5. The byte `DataPathState+64`: `unicastOptions: UMIOptions?`

Field descriptor of `CoreP2P.AWDLActionFrame.DataPathState` at `0x1004c4bcc` (33 fields):
`countryCode`, `socialChannelMap`, `infraBSSID`, `infraChannel`, `infraAddress`, `infraSSID`,
`awdlAddress`, `isUnicast`, **`unicastOptions`** (`Optional` of the enum `UMIOptions`),
`unicastPeerCachingIndication`, `portUniqueLogID`, `rootLoopFreeCounter`, `supportsDualBand`,
`isAirPlaySink`, `followChannelSequence`, `supportsAirPlaySolo`,
`supportsUnicastMasterIndication`, `infraRealTimeActive`, `isRangeable`,
`supportsSimultaneousDualBand`, `supportsDFSProxy`, `assistedDiscoveryActive`,
`supportsSocialChannelMap`, `nanConcurrencyEnabled`, `supportsHighEfficiencyRate`,
`isSidekickHub`, `supports6GHz`, `supportsUltraLowLatencyInfra`, `proModeActive`,
`hasChannelSequence`, `awdlOnSince`, `availabilityWindowSequenceNumber`,
`actionFrameUpdateCounter`. The enum `UMIOptions` (FD `0x1004c4d68`, 49 cases):
`legacyAirPlayStart`, `legacyAirPlayStop`, `realtimeAirPlayStart`, `realtimeAirPlayStop`,
`realtimeAirplayStartBackwardsCompatibility`, `recoverAirPlayStart`,
`channelSequenceUpdate`, `channelSequenceChangeRequested`,
`channelSequenceChangeAcknowledged`, `retryDFSAirPlay`, `dfsAirPlayAllowed`,
`airPlayOnDFSDenied`, `chainSwitch`, `multiPeerSteeringCommand`, `multiPeerSteeringFailure`,
`realtimeStartRequest`, `realtimeStopRequest`, `highThroughputStart`, `highThroughputStop`,
`rangingStartRequest`, `rangingStart`, `rangingStop`, `universalControlStart`,
`universalControlStop`, `legacySidecarStart`, `legacySidecarStop`, `sidecarStart`,
`sidecarStop`, `unusedSidecarRequestBackground`, `unusedSidecarRequestForeground`,
`fastDiscovery`, `recovery`, `disconnectedInfrastructureForRetroMode`, `mpRemoteCameraStart`,
`mpRemoteCameraStop`, `edgeKitStart`, `edgeKitStop`, `remoteCameraStart`, `remoteCameraStop`,
`tvRemoteCameraStart`, `tvRemoteCameraStop`, `remoteScreenStart`, `remoteScreenStop`,
`spatialStreamingStart`, `spatialStreamingStop`, `macVirtualDisplayStart`,
`macVirtualDisplayStop`, `continuityRealTimeGroupStart`, `continuityRealTimeGroupStop`.

The enum exhausts the tag byte (49 cases, tags 0x00–0x30), so the `Optional` stores its
`nil` case with tag 49 = **0x31**. Evidence:

| Function | VA | Test |
|---|---|---|
| default init `sub_10010BD60` | `0x10010be20` | `mov byte [rbx+40h], 31h`: the field is born `nil` (the rest of the init layout: `+00` word 0, `+02` byte 1, `+04` dword 0, `+08` word 3, `+0A` dword 0, `+0E` word 0, `+10` byte 1, `+14` dword 0, `+18` word 3, `+1A` dword 0, `+1E` word 0, `+20` byte 1, `+28` 16 B zeroed, `+36` qword 0 (an `Address`), `+3E` byte 1, `+42` word 0, `+44` byte 1, `+48` dword 0, `+4C` byte 1; the last three fields go through FOV `[metadata+88h/8Ch/90h]` with `storeEnumTagSinglePayload(1,1)` for `awdlOnSince = nil`) |
| Decodable `sub_10010C780` | `0x10010c44f`–`0x10010c4aa` | `decode(UInt16)`, `decode(UInt32)`; bit 0 of the u32 → byte `+41h` (`unicastPeerCachingIndication`); `(rawValue & ~1)` → `sub_10011C2B0` (`init(rawValue:)` of the enum) → tag at `+40h` |
| `==` (`sub_10011A5B0`) | `0x10011a9bc`–`0x10011a9d2` | both `0x31` are equal to each other (both `nil`); the rest compare by value |
| `hash(into:)` (`sub_10010B670`) | `0x10010ba85`–`0x10010bab4` | `0x31` is hashed as `nil` (tag 0, no payload); any other value combines the `u32` rawValue from the table `unk_10046BE74` |

### 5.1 What the byte does on the event 48 path

In `sub_10041C470`, after the publication:

1. The last received `DataPathState` is stored in the peer's `dataPathStateParameter` field
   (FOV slot `+38h` of the metadata, field 10): `0x10041cee1`. The optional extraction is done
   with `sub_100031390` (`0x10041cefd`) and the `nil` test with
   `getEnumTagSinglePayload` of the value witness of `Optional<DataPathState>` (`0x10041cf04`–
   `0x10041cf24`).
2. If the `DataPathState` is not `nil`, the byte is read: `movzx ebx, byte [r15+40h]`
   (`0x10041cf46`), the value is decoded (`sub_1000312D0`, `0x10041cf55`) and compared with
   `0x31` (`cmp bl, 31h` at `0x10041cf5a`).
3. If the byte **is** `0x31` (`unicastOptions == nil`): nothing more happens (`jz 0x10041d140`).
4. If it **is not**: it looks up the traffic registration configuration with the map of
   `AWDLTrafficRegistrationConfiguration` (`sub_100068EA0`, `0x10041cf73`) and the tag as key
   (`movzx esi, bl` at `0x10041cf78`; `sub_10014B180` at `0x10041cf89`). If it exists, it activates
   it with the interface witness and the arguments `(1, 1)` (indirect call at `0x10041cfc8`).

The same byte is consulted by the traffic registration recovery `sub_100421410` (read at
`0x10042179b`, path invoked after every event 48 by `sub_10041FD50`).

Semantics: a non-null `UMIOptions` in the peer's `DataPathState` declares the unicast option
(AirPlay, sidecar, ranging, …) the peer keeps active, and the daemon activates the corresponding
traffic registration (which in turn publishes presence via the `sub_10040B650` path of the
trigger doc §3.4). A null `unicastOptions` activates no registration. **The byte takes no part in
publishing presence or absence**: neither `sub_1002823D0` nor `sub_100282980` looks at it; its
only gate is the traffic registration.

## 6. Implications for the RTL8814 driver

1. With the event 48 flow right, `wifip2pd` emits the 415 once per peer while the peer persists
   in its store. Do not expect a 415 per action frame nor treat its absence as an injection
   failure: the second publication of a known peer is cut at `0x10041cbc6`.
2. After an interface recovery (e.g. a driver watchdog or a reset the daemon sees as «ready»),
   the daemon wipes the NDP and resets all flags: the next AF of any peer produces 415 again. A
   driver restart re-populates presence with no residual state.
3. The aged-out removes the peer for inactivity of its traffic registrations: a peer that stops
   sending AFs with `UMIOptions` or services loses its flag and repeats the presence sequence
   when reappearing.
4. For the daemon to activate traffic registrations (and with them the presence path of §3.4 of
   the trigger doc), the `DataPathState` of the AF must arrive with a real `UMIOptions`: the
   `u32` rawValue of the TLV from which `sub_10010C780` decodes (bit 0 =
   `unicastPeerCachingIndication`, bits 1+ = the `init(rawValue:)` of `UMIOptions` at
   `sub_10011C2B0`). A `DataPathState` without
   that part leaves the byte at `0x31` and the registration is not activated.
5. The TLVs must arrive with the kernel parsing intact: if `actionFrameReport` marks an error at
   `ctx+0x2C`, the event 48 does not come out and no daemon flag moves
   ([AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md) §2).

## 7. Pendings (`?`)

- Exact name of the class of the self of `sub_10041C470`/`sub_1004184D0` and of the ivar of the
  peer store (slot `[class+1C0h]`). The FD of `wifip2pdCore.AWDLInterface` (FD `0x1004d1ad0`,
  field 0 `peers`, field 44 `allServicesRegisteredDuringCurrentSession` — the latter aligns
  exactly with the aged-out slot `268h`) suggests the class, but the mapping of the
  `FieldOffsetVectorOffset` of the nominal descriptor (`0x1004bddf8`, value 110) does not close
  with slots `1C0h`/`2C8h`/`2F8h`.
- ~~Names of the two interface Bools (slots `2C8h` and `2F8h`)~~: closed in
  [AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md) §3.1. The `2F8h` one is written once in the
  interface init (`0x100417647`) with witness 4 of the `AWDLWiFiDriver` conformance of the driver type:
  for `AppleIO80211Driver` it is `RawGet(117)` (`AWDL_SYNC_ENABLED`) without error and `enabled`
  (`u32 @+8`) not 0 (`sub_1000A93F0`); for `AppleBroadcomDriver` it is a constant 0 (`sub_100003B30`).
  The `2C8h` one is set to 1 by "Driver interface was removed" (§3.4) and to 0 by the init and the
  recovery (§3.3).
- `sub_10014B180` (search of the `AWDLTrafficRegistrationConfiguration` by `UMIOptions` tag)
  and the rawValues table `unk_10046BE74`: which `u32` rawValue corresponds to each case.
- `sub_1004291D0`/`sub_1004292F0` (cleanups called inside the interface recovery) and the name of
  the `DriverEvent` case of the jumptable leading to the aged-out (case 8 of `0x100418a43`).
- The dispatcher jumptable case with the MAC feeding the aged-out: whether it is a
  «traffic registration aged out» event from the driver or an internal daemon expiry.
