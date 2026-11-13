# Sequence 129: who emits it, how the 16 slots are built and what is at `+0xB88`

Answers to three questions about macOS 26.7.1 (25G241, x86_64): issuer of request 129 on `awdl0`,
construction of the 16 slots and the method of slot `+0xB88` that `interfaceStateChange` consults.
Base: copy of the kernel collection 25G241 and copy of the `wifip2pd` database of 25G241. Each
claim carries the VA of the function or instruction; unobserved items go with `?`.

Conventions: `A = *(manager+0x18)` is the block that `IO80211PeerManager::initWithInterface`
(`0xffffff8002246c88`) allocates at `0xffffff8002246cd7` and where the object of `+0x560` lives;
`B = *(manager+0x20)` is the block the gate queries at `+0x3EE4`. The previous documentation calls
`ctrl` the `A` and `ivars` the `B`; field `+0x560` of `A` is an `IO80211VirtualInterface *`, not the
controller. Sequence entry: 12 B `{u32 version/flags, u32 channel, u32 flags}`.

## 1. Who emits the 129

### 1.1 The kernel, via the internal path

`IO80211AWDLPeerManager::setChannelSequenceUnconditional` (`0xffffff80021cb69a`) composes the
400 B payload and brings it down to the driver with `IO80211PeerManager::ioctl_internal`
(`0xffffff80022524e8`):

- command `0x81802869C8` at `0xffffff80021cc0b8`, copied into the `ioctl_internal_req` at
  `0xffffff80021cc0c6` and sent at `0xffffff80021cc0d0`;
- payload: `+0 = 1`, `+4` flags, `+5 = n`, `+6` encoding, `+7` step, `+8` duplicate, `+9` filler,
  12 B entries from `+12`, copied from `ivars+42868` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1);
- suppression by `memcmp` of 0x190 against `ivars+8` and, if it changed or is forced, emission; afterwards it stores
  the copy at `+8` and posts message 195.

That internal command is the 129 the driver sees (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1: "internal ioctl
129, `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`, 400 B"). Its triggers are `configure`, `enableAWDL`,
`setChannelSequence`, `followPeerChannelSequence` (`0xffffff80021f6e74`), `setRoamChannelSequence`
(`0xffffff80021e5b74`), the overlay events of scan and the RTG re-evaluation.

The userspace entry is another: `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`
(`0xffffff8002288caa`) calls `IO80211AWDLPeerManager::setAwdlChannelSequence`
(`0xffffff800221cedc`) at `0xffffff8002288cfe`. That function validates `n` between 1 and 32
(`0xffffff800221cef2`–`0xffffff800221cefc`), processes DFS (`0xffffff800221d0fc`), re-sends the payload
with `apple80211RouteInternal(0x802869C8, 129, …)` (`0xffffff800221d12e`) and sets strategy 3
(`0xffffff800221d141`–`0xffffff800221d150`). It has no callers in the kernel: only a userspace
`RawSet` 129 reaches it.

### 1.2 wifip2pd only reads it

In `wifip2pd` 25G241 the 129 appears twice, and both are reads:

- `sub_1000A97D0` (`0x1000a97d0`) reserves 400 B, zeroes them (`0x1000a97e4`–`0x1000a9800`), loads
  `edi = 0x81` (`0x1000a9828`) and calls `sub_10004A440` (`0x1000a983e`). That wrapper loads
  `_Apple80211RawGet_ptr` (`0x10004a456`) and passes selector 262 to `sub_10004A4E0`; it is `RawGet`, not
  `RawSet`.
- `sub_1000A9900` (`0x1000a9900`) is a generic getter: `edi = 0x81` at `0x1000a997a`, call to
  `sub_1000A65F0`, which reserves and zeroes the buffer, runs the read and loads the result
  (`0x1000a6698`–`0x1000a66ac`).

The result of the read is parsed in `sub_1000D5390` (`0x1000d5390`): copies 0x180 B from `+12`,
reads the number of slots at `+5` and walks 12 B entries; `sub_1000D3040` converts each channel. It is
consumption for the internal state and statistics of the daemon (`channelSequenceMismatchOn2GCount`,
`channelSequenceMismatchOn5GCount`, `channelSequenceChangedEvent:`), not a write.

The write wrapper exists (`sub_10004A490`, `0x10004a490`, loads `_Apple80211RawSet_ptr` at
`0x10004a4a6`), but there is no call with 129: the only two appearances of immediate `0x81`
in the binary are the previous reads. A `RawSet` 129 with the number computed in a register is not
covered by this sweep `?`; no other daemon has been analyzed `?`.

Conclusion: the 129 that reaches `awdl0` is emitted by the `IO80211AWDLPeerManager` itself through the
internal path; `wifip2pd` reads it for reporting. No alternate issuer has been observed.

## 2. How the 16 slots are built

### 2.1 Strategy, state and table

The decision is the documented chain: strategy at `ivars+4` (`setChannelSequenceStrategy`,
`0xffffff80021ca958`), calendar state at `ivars+10468` (`setScheduleState`,
`0xffffff80021d27a0`) and table `__ZL20_channelSeqSlotTypes` (`0xffffff80023aa1f0`), indexed with
`state << 6` (`0xffffff80021f5459`–`0xffffff80021f5460`) and dispatched by the 21-case `switch` of
`0xffffff80021f5480`. The jump table uses a base relative to the `jpt` itself (`movsxd` + `add` at
`0xffffff80021f5491`–`0xffffff80021f5495`), without bias: the case number is the slot type.

Relevant rows:

| State | Slots 0..15 |
|---|---|
| 0 Discovery | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` |
| 3 Idle | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` |
| 10 RealTimeData + InfraAssist | `9 ×16` |
| 11 RealTimeData + InfraAssist with Social | `9 9 3 9 9 9 9 9 9 9 4 9 9 9 9 9` |
| 29 Infra Priority | `9 9 3 1 1 1 1 9 2 9 3 9 1 1 1 9` |
| 41 High Thruput | `4 ×16` |

### 2.2 Sources and flags

`setChannelSequenceByState` (`0xffffff80021f53aa`) writes 16 entries of 12 B into `ivars+42868`:

| Type | Source | Channel / flags |
|---:|---|---|
| 1 | `_zeroChanInfo` (`0xffffff8002488a88`) | channel 0, flags 0 |
| 2 | primary `ivars+17056`/`+17064` | by default 6, flags 8 |
| 3 | secondary `ivars+17068`/`+17072` if ≠ 0, else primary | 149 (or 44), flags 8 |
| 4 | current AWDL channel `ivars+416`/`+424` | the current one, with its flags |
| 7 | infrastructure if `addInfraToMinimumChannelSequence` (`0xffffff80021f5ef6`) allows it | without association, 0 |
| 9 | infrastructure if `infraAllowedForNonRealtimeCases` (`0xffffff80021dc540`); if not, slot 0 → 6 with flags `0xA`, rest 0 except SDB | 6 / `0xA` |

`initParamsToDefaults` (`0xffffff80021c7a40`) leaves the master at 6 (`0xffffff80021c7ada`,
`0xffffff80021c7aff`), the secondary at 149 (`0xffffff80021c7ae4`) and the alternate at 44
(`0xffffff80021c7aeb`), with flags 8 (`0xffffff80021c7b06`). `initDefaultChannelSequence`
(`0xffffff80021c79c0`) puts zero in all slots and then channel 6 with flags 8 in slot 0
(`0xffffff80021c7a0e`, `0xffffff80021c7a15`) and copies that entry to slot 8 (`0xffffff80021c7a29`,
`0xffffff80021c7a36`). The meaning of flags 8 and `0xA` remains `?`.

### 2.3 Infrastructure unassociated or link-down

`getInfrastructureState` (`0xffffff80021da84a`) reads `getInfraChannel` and, with channel 0, logs
"lost infrastructure link (new channel %d)" (`0xffffff80023d942b`) if there was a previous channel, or returns
0 if there never was; it does not propagate an error. In the sequence:

- type 7 stays at zero because the cached channel is 0;
- type 9 falls to the 6 of slot 0;
- the default sequence is already 6 in slots 0 and 8;
- type 3 uses secondary 149, which does not depend on the infrastructure.

Additionally, if the `linkState` of the vifc is 1, `interfaceStateChange` exits before touching state or
sequence (§3), so the missing association may never get to re-evaluate anything.

### 2.4 Resulting sequences

Without association, strategy 1 (`AwdlStateMachine`) and a valid 149:

- State 0 Discovery, no peers:
  `149 149 149 149 0 0 0 0 6 <current> 149 <current> 0 0 0 0`.
- State 3 Idle, no peers:
  `149 149 149 0 0 0 0 0 6 <current> 149 0 0 0 0 0`.
- State 10: `6 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0`.
- State 11: `6 0 149 0 0 0 0 0 0 0 <current> 0 0 0 0 0`.
- State 41: the current channel in the 16 slots.

With a peer, `setScheduleState` can take the path of following its sequence: if there is a peer whose
sequence to follow (`ivars+16248`, or the peer of `+16224` with the bit of `+1665`), it calls
`followPeerChannelSequence` (`0xffffff80021f6e74`), which copies the `n` 12 B entries of the peer
(peer block, channel at `+924`; detail in `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.6) to
`ivars+42868`, sets state 9 (`0xffffff80021f7088`) and emits with
`setChannelSequenceUnconditional(true)`. The resulting sequence is then the peer's, not one
built by the table; for a peer parked on 149 it will be 149 in the slots it serves and 0 in the
rest, with the exact composition depending on its TLV 18 `?`. If the peer is not followed, the
row of state 3 is applied.

The emitter reads no userspace strategy: it uses its own ivar `+4` and state `+10468`. The
reads of `wifip2pd` (§1.2) serve the internal state and statistics of the daemon, not the decision of the
sequence.

## 3. The object of `+0x560` and slot `+0xB88`

### 3.1 The object is the vifc, not the controller

The gate of `interfaceStateChange` (`0xffffff80021d8cc8`), only with `B+0x3EE4 != 0`
(`0xffffff80021d8e9d`–`0xffffff80021d8ea8`), does:

- `mov rax, [rbx+18h]` at `0xffffff80021d8eaa`;
- `mov rdi, [rax+560h]` at `0xffffff80021d8eae`;
- `mov rax, [rdi]` and `call qword ptr [rax+0B88h]` at `0xffffff80021d8eb5`/`0xffffff80021d8eb8`;
- `cmp eax, 1` at `0xffffff80021d8ebe`: if it is 1, it logs "Interface link is down, nothing to do.
  Source = %s" (`0xffffff80021d8ed8`) and exits; any other value continues.

`IO80211PeerManager::initWithInterface` stores the first argument (`IO80211VirtualInterface *`,
`r12` at `0xffffff8002246c9f`) into `[A+0x560]` (`0xffffff8002246e08`), after allocating `A` at
`0xffffff8002246cd7`. The object is, therefore, the `IO80211VirtualInterface` of the peer manager; for
`awdl0`, its subclass `IO80211AWDLProtocol`, and in this layer `RTLAwdInterface`. It is not the controller:
the vtable of the controller at `+0xB88` is another one (§3.3).

### 3.2 The vtable and the real target

The vtable symbols of Mach-O carry the two header words in front of the address point: in
`__ZTV` the first qword is 0, the second 0 and the destructor appears at `+0x10` (`0xffffff8002472b78`,
`0xffffff800246af00`, `0xffffff800247b4f8`). That is why slot `+0xB88` of the object is the qword
`+0xB98` of the symbol.

| Class | `__ZTV` symbol | Target at `+0xB88` | Return |
|---|---:|---:|---|
| `IO80211VirtualInterface` | `0xffffff8002472b78` | `0xffffff80023333f8` `linkState` | `*(u32 *)(*(this+0x128)+8)` |
| `IO80211AWDLProtocol` | `0xffffff8002456440` | `0xffffff80023333f8` (inherited) | same |
| `IO80211SkywalkInterface` | `0xffffff800246af00` | `0xffffff80022f982a` `linkState` | 0 |
| `IO80211InfraInterface` | `0xffffff800247b4f8` | `0xffffff80023614a8` `linkState` | `*(u32 *)(*(this+0x128)+0x18)` |
| `IO80211Controller` | `0xffffff8002461148` | `0xffffff80026eccc8` `IOEthernetController::setRealtimeReceiveQueuePacketHandler` | `0xE00002C7` |

`IO80211VirtualInterface::linkState` (`0xffffff80023333f8`) is
`mov rax, [rdi+128h]; mov eax, [rax+8]; ret`; the field is written by `setLinkState`
(`0xffffff8002333427`–`0xffffff8002333471`). The values used by the layer are 1 = down and 2 = up
(`AWDLInterface.cpp`, `AWDL_LINK_DOWN`/`AWDL_LINK_UP`).

### 3.3 Subclasses that do not overwrite it

`IO80211AWDLProtocol` does not define `linkState`: its vtable points to the same `0xffffff80023333f8`, so
it returns the field at `+8`, initially 0 and with whatever value `setLinkState` leaves. `RTLAirController`
derives from `IO80211Controller` and does not overwrite the slot: if the gate were applied to the controller,
it would call `IOEthernetController::setRealtimeReceiveQueuePacketHandler` (`0xffffff80026eccc8`), which
returns `0xE00002C7` and never 1; but the object of `+0x560` is the vifc, not the controller.
`RTLAwdInterface` does overwrite `linkState` (`AWDLInterface.h`, slot `0xB88`) and returns 1 or 2; without
that override it would inherit the kernel body and return the field at `+8`.

Practical consequence: while the vifc returns 1, `interfaceStateChange` does not re-evaluate strategy or
sequence; with 2 (or any other value) it continues. That is why the layer puts the link at 2 when enabling.

## 4. Pending

- Exact correspondence between the internal command `0x81802869C8` and the request number 129: the field
  carrying it has not been isolated `?`; the previous documentation takes it as 129.
- Userspace client that, if it exists, does a `RawSet` 129 (airportd or another): not observed `?`.
- Exact sequence with a peer: depends on its TLV 18 and has not been captured `?`.
- Semantics of the flags 8 and `0xA` of the entries `?`.
- State 0 or 3 on an idle Mac without peers depending on the timers of `setScheduleState` `?`.
