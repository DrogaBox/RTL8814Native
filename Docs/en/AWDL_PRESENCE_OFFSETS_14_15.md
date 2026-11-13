# Presence and PPP offsets of the AWDL stack: macOS 14.8.9 (23J631) and 15.8.2 (24H232)

Port of every ivar offset, vtable slot, handler table entry and threshold cited by
[`AWDL_PRESENCE_PREREQS.md`](AWDL_PRESENCE_PREREQS.md), [`AWDL_PEER_PRESENCE_PPP.md`](AWDL_PEER_PRESENCE_PPP.md)
and [`AWDL_PEERMANAGER_CREATION.md`](AWDL_PEERMANAGER_CREATION.md) for 25G241 (macOS 26.7.1) to the
kernelcaches of 23J631 and 24H232, both x86_64. Each row was first resolved by symbol in all three
builds and then the equivalent instruction or cell was read in the disassembly of the same function.
The exchange data stay in [`Tools/awdl_presence_offsets.tsv`](../../Tools/awdl_presence_offsets.tsv)
(columns `name, kind, va_25G241, value_25G241, va_23J631, value_23J631, va_24H232, value_24H232, note`).
Anything unchecked goes with `?`. The 101 rows with build in the TSV (34 in 25G241, 33 in 23J631 plus the
absence of `getController`, 34 in 24H232) were re-verified one by one against the three IDBs: instruction head
with the cited immediate or displacement, cell equal to `ZTV + 0x10 + slot` with the
expected method, and `gSetHandlerTable` entry equal to the handler.

## 1. Method

- Per-build symbol dumps (`Docs/re/io80211_{25.6.0,23J631,24H232}/syms.txt`): the 29 functions
  of the chain exist with the same mangled name in the three versions; no row was resolved
  by byte similarity.
- Instruction-by-instruction disassembly of those functions and cell reads (`__ZTV15IO80211AWDLPeer`,
  `__ZTV11IO80211Peer`, `__ZTV17IO80211Controller`, `__ZTV23IO80211VirtualInterface`, `gSetHandlerTable`,
  `gSetHandlerTableLegacy`) with IDA 9.4 headless (`idat -A -S`) over copies of the three IDBs in `/tmp`.
- No row depends on jump tables: the cited accesses are loads/stores with direct
  displacement, calls by cell or immediate comparisons. The only nearby indexed table (mode
  names in the log of `interfaceStateChange`, `cmp rax, 3; jmp [table+rax*8]`) was reviewed and does not affect
  any row. Each vtable slot was read from the cell (`cell = ZTV + 0x10 + slot`), never by
  counting.
- The cited VAs are instruction heads or cells of each build; they change between versions and only
  accredit the finding.

## 2. Summary of deltas

| Block | Field | 25G241 | 23J631 | 24H232 |
|---|---|---|---|---|
| Manager's AWDL ivars (`this+0x20`) | `LLAValid` | `+0xFF1C` | `+0xC4A0` | `+0xC70C` |
|  | `AWDLOn`/stateFlags | `+0x3EE0` | `+0x3EC8` | `+0x3EE8` |
|  | `_bonjourTriggersAWDL` | `+0xA718` | `+0x7FA8` | `+0x7FF8` |
|  | LLA timer | `+0x28A0` | `+0x3EB8` | `+0x3ED8` |
|  | `channelSequenceStrategy` (dword) | `+0x4` | `+0x4` | `+0x4` |
|  | companion field of the manual mode | `+0xA66C` | `+0x7F04` | `+0x7F4C` |
| AWDL ivars of the peer (`this+0x20`) | PPP done flag | `+0x6DE` | `+0x6AE` | `+0x6AE` |
|  | early-return mark | `+0x310` | `+0x2E8` | `+0x2E8` |
| Vtable of `IO80211AWDLPeer` | `monitorPeer` | `+0x180` | `+0x180` | `+0x178` |
| Vtable of the vif | `inputPacket` | `+0xB50` | `+0xB48` | `+0xB48` |
|  | `sendPendingPacketsToStack` | `+0xF30` | `+0xE80` | `+0xF10` |
|  | `freePendingPackets` | `+0xF38` | `+0xE88` | `+0xF18` |
|  | `createPeerManager` | `+0xB08` | `+0xB08` | `+0xB00` |
|  | `getP2PSkywalkPeerMgr` | `+0xCC0` | `+0xCC0` | `+0xCB8` |
|  | `handleIoctl` | `+0xE80` | `+0xE08` | `+0xE78` |
|  | `getController` | `+0xBA8` | **absent** | `+0xBA8` |
| Vtable of `IO80211Controller` | `executeCommand` | `+0x850` | `+0x850` | `+0x850` |
| Ivars of the vif (`IO80211SkywalkInterface`) | ivars pointer (`this`) | `+0x120` | `+0x110` | `+0x110` |
|  | peer manager | `+0x28` | `+0x30` | `+0x28` |
|  | controller | `+0x30` | `+0x38` | `+0x30` |

The drift is again non-monotonic: `sendPendingPacketsToStack`/`freePendingPackets` drop 0xB0 in
23J631 and recover 0x70 in 24H232 without returning to the 25G241 value; `monitorPeer`, `createPeerManager`,
`getP2PSkywalkPeerMgr` and `inputPacket` only move in 24H232. No slot can be deduced from the
adjacent version.

## 3. Ivars of the `IO80211AWDLPeerManager`

In the three builds the AWDL block is reachable from `this+0x20` (`mov r13, [rbx+20h]` in 25G241,
`lea rax, [r15+20h]`/`mov rcx, [r15+20h]` in 23J631 and 24H232): the base did not move; the
fields inside the block moved.

### 3.1 `LLAValid` (byte; `PM+65308` in 25G241)

| Writer | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `interfaceStateChange`, store 1 | `0xffffff80021d9b39` | `0xffffff80020c93db` | `0xffffff8002177870` |
| `interfaceStateChange`, store 0 | `0xffffff80021d9b44` | `0xffffff80020c93e7` | `0xffffff800217787b` |
| `monitorAWDLState`, copy of `isLLAConfigured` | `0xffffff80021ed932` | `0xffffff80020dad48` | `0xffffff8002189d67` |
| `monitorAWDLState`, re-test of the bit | `0xffffff80021ee228` | `0xffffff80020db269` | `0xffffff800218a6a3` |
| `initParamsToDefaults`, store 0 | `0xffffff80021c818f` | `0xffffff80020b8095` | `0xffffff8002166728` |

The LLA block of `interfaceStateChange` stays intact in 14/15: same strings
(`"ERROR: enableLLAMonitoring  as n..."`, `"NOT-ENABLING-AWDL"`, `"%s: Enabling AWDL due to %s\n"`),
same call to `isLLAConfigured` (23J631 `0xffffff80020c93ce`, 24H232 `0xffffff8002177863`) and same
immediates.

### 3.2 `_bonjourTriggersAWDL` (dword)

The LLA block's guard is tested by `cmp dword ptr [ivars+X], 0` in `interfaceStateChange`:
`0xffffff80021d9b0a` (`+0xA718`), `0xffffff80020c93b1` (`+0x7FA8`), `0xffffff8002177841` (`+0x7FF8`).
Writers, with the same per-build split as in 25G241:

| Writer | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `initWithInterface` default 1 | `0xffffff80021c15d6` | `0xffffff80020b1cf9` | `0xffffff8002160192` |
| `initWithInterface` boot-arg `awdl_enable` → 0 | `0xffffff80021c1668` | `0xffffff80020b1d88` | `0xffffff8002160221` |
| `initWithInterface` `awdl_oper_mode=1` → 0 | `0xffffff80021c16d1` | `0xffffff80020b1df1` | `0xffffff800216028a` |
| `setAwdlCtl` (payload, log `"bonjourTriggersAWDL"`) | `0xffffff80021d032a` | `0xffffff80020c0aa7` | `0xffffff800216e5e4` |
| `setAwdlOperatingMode` (two sites) | `0xffffff800221ec92` / `0xffffff800221ecc5` | `0xffffff8002108a7c` / `0xffffff8002108a55` | `0xffffff80021b8998` / `0xffffff80021b89cb` |

The boot-args `awdl_enable` and `awdl_oper_mode` are still present with the same name in all three
(`lea rdi, "awdl_enable"` / `"awdl_oper_mode"` in the slice of `initWithInterface`).

### 3.3 `AWDLOn`/stateFlags (dword)

| Site | 25G241 (`+0x3EE0`) | 23J631 (`+0x3EC8`) | 24H232 (`+0x3EE8`) |
|---|---|---|---|
| `monitorPeer`, `test byte, 0Ch` (QUIET/FOR ASSOCIATION) | `0xffffff80021a9339` | `0xffffff800209b197` | `0xffffff800214928b` |
| `enableAWDL`, `(x & ~6) \| 2` | `0xffffff80021dd5a9` | `0xffffff80020cc6e6` | `0xffffff800217aa65` |
| `setAwdlSyncState`, `x & ~2` | `0xffffff800221e49d` | `0xffffff800210803b` | `0xffffff80021b8255` |
| `setAwdlSyncState`, `x & ~4` | `0xffffff800221e52e` | `0xffffff8002108101` | `0xffffff80021b831b` |
| `setAwdlSyncState`, final write `(x & ~6) \| 2` | `0xffffff800221e6ec` | `0xffffff8002108254` | `0xffffff80021b84a2` |
| `setAwdlSyncState`, test bit 0 (CONFIGURED) | `0xffffff800221e41b` | `0xffffff8002107fc7` | `0xffffff80021b81df` |
| `enterQuietMode`, `(x & ~6) \| 4` | `0xffffff80021f42b5` | `0xffffff80020e0e5e` | `0xffffff8002190127` |
| `exitQuietMode`, `x & ~6` | `0xffffff80021faf7e` | `0xffffff80020e7432` | `0xffffff80021969f2` |
| `exitQuietMode`, `(x & ~6) \| 2` | `0xffffff80021fb085` | `0xffffff80020e7511` | `0xffffff8002196b00` |
| `setAwdlSuspendedMode`, `x \| 8` | `0xffffff80021e5cc9` | `0xffffff800210848f` | `0xffffff80021827a1` |

The activation decision of `interfaceStateChange` keeps the shape: `cmp dword ptr [rcx+4], 0`
(25G241 `0xffffff80021d9bc2`, 23J631 `0xffffff80020c9460`, 24H232 `0xffffff80021778f9`), test of
source == 3 (`cmp r12d, 3` on 23J631/24H232; the register of 25G241 was `r15d`) and suspension with
`test al, 8` over the low byte of the field (`0xffffff80021d9bd7` / `0xffffff80020c9476` /
`0xffffff800217790e`).

### 3.4 LLA timer, `channelSequenceStrategy` and companion field

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| Store of the `IO80211TimerSource` in `initWithInterface` | `+0x28A0`, `0xffffff80021c209b` | `+0x3EB8`, `0xffffff80020b2891` | `+0x3ED8`, `0xffffff8002160c24` |
| Reload in `interfaceStateChange` (enable +0x150) | `0xffffff80021d9b8a` / `0xffffff80021d9b99` | `0xffffff80020c942b` / `0xffffff80020c943a` | `0xffffff80021778c5` / `0xffffff80021778d4` |
| Rearm `setTimeoutMS(0x1F4)` (cell +0x1D0) | `0xffffff80021d9bb6` | `0xffffff80020c9453` | `0xffffff80021778ed` |
| `channelSequenceStrategy`: `cmp dword [ivars+4], 0` (decision) | `0xffffff80021d9bc2` | `0xffffff80020c9460` | `0xffffff80021778f9` |
| `channelSequenceStrategy`: default 1 (`AwdlStateMachine`) / manual 3 (`AwdlUserConfigured`) | `0xffffff80021c15d3` / `0xffffff80021c16c8` | `0xffffff80020b1cf6` / `0xffffff80020b1de8` | `0xffffff800216018f` / `0xffffff8002160281` |
| Companion field (to 0 in manual mode) | `+0xA66C`, `0xffffff80021c16d7` | `+0x7F04`, `0xffffff80020b1df7` | `+0x7F4C`, `0xffffff8002160290` |

In `handleAwdlLLATimer` the timer's cells did not move: `+0x218` (cancelTimeout),
`+0x158` (disable) and `+0x1D0` (setTimeoutMS) with the three identical log strings in the three builds
(25G241 `0xffffff80021c2f4b`/`c2f5f`/`c2fb0`/`c2fc8`/`c2ffd`; 23J631 `0xffffff80020b3801`/`b3815`/
`b386d`/`b3887`/`b38c3`; 24H232 `0xffffff8002161a9b`/`61aaf`/`61b00`/`61b18`/`61b4d`).

## 4. Ivars of the `IO80211AWDLPeer`

The peer's AWDL block is also reachable from `this+0x20` (`mov rax, [rbx+20h]` before each test) and the
two cited fields go down:

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| PPP done flag: guard of the ageout in `monitorPeer` | `+0x6DE`, `test byte, 1`, `0xffffff80021a9705` | `+0x6AE`, `cmp byte, 0`, `0xffffff800209b538` | `+0x6AE`, `cmp byte, 0`, `0xffffff8002149648` |
| PPP done flag: store of the bool in `pppDone` | `0xffffff80021bfb86` | `0xffffff80020b05fa` | `0xffffff800215ea60` |
| PPP done flag: test in `skywalkInputPacket` | `0xffffff80021bf9c5` | `0xffffff80020b0445` | `0xffffff800215e8a7` |
| Early-return mark in `monitorPeer` (after `test 0Ch`) | `+0x310`, `0xffffff80021a9342` | `+0x2E8`, `0xffffff800209b1a0` | `+0x2E8`, `0xffffff8002149294` |

`pppDone` keeps the `IOMallocZeroData(0x200)` and the log `"Posted Peer Presence for ..."` in all three
(25G241 `0xffffff80021bfb3b`/`bfd43`; 23J631 `0xffffff80020b05af`/`b07b7`; 24H232
`0xffffff800215ea15`/`ec1d`). `setAwdlPostPeerPresenceDone` still does `findPeer(data+4)` +
`safeMetaCast` + `pppDone(true)` (25G241 `0xffffff800221c980`/`c98f`/`c9a1`; 23J631
`0xffffff800210692a`/`6939`/`694b`; 24H232 `0xffffff80021b69fe`/`6a0d`/`6a1f`).

## 5. Vtable slots and called cells

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| Cell `monitorPeer` in `__ZTV15IO80211AWDLPeer` | `0xffffff800245a110`, slot `+0x180` | `0xffffff80022c9ed0`, `+0x180` | `0xffffff80023c9aa8`, `+0x178` |
| Call in `_doMonitoring` | `0xffffff800224f720` `[rax+180h]` | `0xffffff8002134ce0` `[rax+180h]` | `0xffffff80021e556e` `[rax+178h]` |
| Cell `inputPacket` (packet) in `__ZTV23IO80211VirtualInterface` | `0xffffff80024736d8`, `+0xB50` | `0xffffff80022dc6e0`, `+0xB48` | `0xffffff80023dfea0`, `+0xB48` |
| Cell `sendPendingPacketsToStack` | `0xffffff8002473ab8`, `+0xF30` | `0xffffff80022dca18`, `+0xE80` | `0xffffff80023e0268`, `+0xF10` |
| Cell `freePendingPackets` | `0xffffff8002473ac0`, `+0xF38` | `0xffffff80022dca20`, `+0xE88` | `0xffffff80023e0270`, `+0xF18` |
| `pppDone` calls the delivery cell | `0xffffff80021bfc77` `[rax+0F30h]` | `0xffffff80020b06eb` `[rax+0E80h]` | `0xffffff800215eb51` `[rax+0F10h]` |
| `ageoutPacketsWaitingForPPP` calls the release cell | `0xffffff80021aa984` `[rax+0F38h]` | `0xffffff800209c84e` `[rax+0E88h]` | `0xffffff800214a8e2` `[rax+0F18h]` |
| `flushPacketsWaitingForPPP` calls the release cell | `0xffffff80021bff87` | `0xffffff80020b09bd` | `0xffffff800215ee47` |
| Ageout threshold (`cmp edx, 7D1h`) | `0xffffff80021aa8cf` | `0xffffff800209c78b` | `0xffffff800214a82d` |
| Cell `createPeerManager` | `0xffffff8002473690`, `+0xB08` | `0xffffff80022dc6a0`, `+0xB08` | `0xffffff80023dfe58`, `+0xB00` |
| Call from the vif's `start` | `0xffffff80022f5bca` | `0xffffff80021a06eb` | `0xffffff80022630b2` |
| Cell `getP2PSkywalkPeerMgr` | `0xffffff8002473848`, `+0xCC0` | `0xffffff80022dc858`, `+0xCC0` | `0xffffff80023e0010`, `+0xCB8` |
| Cell `handleIoctl` | `0xffffff8002473a08`, `+0xE80` | `0xffffff80022dc9a0`, `+0xE08` | `0xffffff80023e01d0`, `+0xE78` |

The base implementations of `sendPendingPacketsToStack`/`freePendingPackets` remain the
`IOLog("... Not Implemented")`-only stubs on 14/15: 25G241 `0xffffff8002334e68`/`0xffffff8002334e88`,
23J631 `0xffffff80021dd636`/`0xffffff80021dd656`, 24H232 `0xffffff80022aa87e`/`0xffffff80022aa89e`.
Without override, retained packets are lost the same as in 25G241.

## 6. The ioctl 415: handler table and length check

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `gSetHandlerTable[414]` (cmd 415, cell) | `0xffffff8002460be0` | `0xffffff80022cf1e0` | `0xffffff80023d0250` |
| Handler (`setPOST_PRESENCE_DONE`) | `0xffffff800228d968` | `0xffffff800216790b` | `0xffffff800222038e` |
| Length check `cmp dword [req+18h], 0Ch` | `0xffffff800228d973` | `0xffffff8002167916` | `0xffffff8002220399` |
| Tail-call to `setAwdlPostPeerPresenceDone` | `0xffffff800228d9b9` | `0xffffff800216795c` | `0xffffff80022203df` |
| `gSetHandlerTableLegacy[414]` | 0 | 0 | 0 |

The legacy entry for the 415 is empty in all three builds: the 415 only arrives through the modern table.
The handler's signature changes per build (25G241 `(Controller, SkywalkInterface, APIUserClient, req)`,
23J631 `(Controller, VirtualInterface, InfraInterface, req)`, 24H232 `(Controller, SkywalkInterface,
req)`), but the body is identical: length 12, non-null data, manager from the interface's
ivars, `safeMetaCast` to `IO80211AWDLPeerManager` (45 / 13h / 2Dh as equal error codes).

## 7. Peer manager and controller pointer in the vif

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| Ivars pointer of the vif (`getPeerManager` first load) | `+0x120`, `0xffffff80022f5fac` | `+0x110`, `0xffffff80021a0a40` | `+0x110`, `0xffffff8002263494` |
| Peer manager inside the ivars (`getPeerManager` second load) | `+0x28`, `0xffffff80022f5fb3` | `+0x30`, `0xffffff80021a0a47` | `+0x28`, `0xffffff800226349b` |
| Store of the manager in `start` | `0xffffff80022f5bd8` | `0xffffff80021a06f9` | `0xffffff80022630c0` |
| Store of the controller in `start` (`safeMetaCast(provider, IO80211Controller)`) | `+0x30`, `0xffffff80022f583f` | `+0x38`, `0xffffff80021a0393` | `+0x30`, `0xffffff8002262d27` |
| Cell `getController` in the vif's vtable | `0xffffff8002473730`, `+0xBA8` | **absent** | `0xffffff80023dff00`, `+0xBA8` |
| Cell `executeCommand` in `__ZTV17IO80211Controller` | `0xffffff80024619a8`, `+0x850` → `0xffffff80026f51e0` | `0xffffff80022cfc58`, `+0x850` → `0xffffff80025156ee` | `0xffffff80023d0f50`, `+0x850` → `0xffffff8002630c98` |
| `handleIoctl` calls `executeCommand` | `0xffffff8002331cf6` `[r10+850h]` | `0xffffff80021da579` `[rax+850h]` | `0xffffff80022a77d6` `[r10+850h]` |

In 23J631 `getController` stopped being in the vif's vtable (slot `+0xBA8` is occupied by
`updateLinkParameters`) and `handleIoctl` reads the controller directly from the ivars:
`mov rax, [rcx+110h]; mov rdi, [rax+38h]` at `0xffffff80021da55a`–`0xffffff80021da561`. The four
ioctl codes that `handleIoctl` filters (`0x802069C8`, `0x802869C8`, `0xC02869C9`, `0xC02069C9`,
with `0xC02069C8` as boundary) are the same in the three builds; the other ioctls keep falling into
`processBSDCommand` (tail `jmp` at `0xffffff8002331d1b` / `0xffffff80021da58e` / `0xffffff80022a77fb`).

## 8. What did not move

- AWDL ivar base of manager and peer: `this+0x20` in all three.
- `ivars+4` (`channelSequenceStrategy`) of the manager.
- Timer cells (`+0x150`, `+0x158`, `+0x1D0`, `+0x218`) and the period `0x1F4` ms.
- Ageout threshold `0x7D1` (2001 ms).
- Length of the 415 (12) and the batch size `0x200` of the packet array in `pppDone`.
- Slot `+0x850` of `IO80211Controller` (`executeCommand`).
- The four ioctl codes of `handleIoctl` and the gate/`processBSDCommand` split.
- The log strings of the LLA block and of the PPP cycle (same literals in the three versions).

## 9. Shape changes affecting the port

- **`setPOST_PRESENCE_DONE` changes signature in the three versions** (§6): the per-build gate cannot
  fix the index of the interface argument; the reliable route remains the entry of
  `gSetHandlerTable` and the payload length 12.
- **`inputPacket` in 23J631 loses the trailing `bool`** and the vtable adds a new overload
  `inputPacket(__mbuf*, packet_info_tag*)` at slot `+0xE98` (cell `0xffffff80022dca30`,
  implementation `0xffffff80021dd5b0`). In 24H232 the packet signature carries the trailing `bool` again
  and the mbuf overload does not exist. The layer's RX diversion must call slot `+0xB48` with the
  build's signature on 14/15.
- **`IO80211PeerManager::skywalkInputPacket` changes signature in 23J631** (one fewer `bool` at the end);
  the rest of the RX chain through the peer's `skywalkInputPacket` did not move.
- **`getController` falls out of the vif's vtable in 23J631** (§7): the controller is read from
  `ivars+0x38` in that build and from slot `+0xBA8` in 25G241/24H232.
- The workqueue ivar that `start` takes from the controller (slot `+0xC68` in 25G241) resettles at
  `ivars+0x38` of the vif in 25G241/24H232 and outside the verified range in 23J631 `?`.

## 10. Absent rows and open points

| Row | Build | State |
|---|---|---|
| `getController` slot in the vif's vtable | 23J631 | Absent; the access is direct through the ivar (§7) |
| Entry 415 of `gSetHandlerTableLegacy` | all three | 0; the 415 does not circulate through the legacy table |
| Emitters of sources 0/1/3/8/10/12/13 of `StateChangedSource` | — | Pending of the source docs, unchanged in this pass |
| Lock of the path `removePeer` → `flushPacketsWaitingForPPP` | 25G241 | Closed in `AWDL_PRESENCE_PREREQS.md` §3.2 (no data-path lock, after `unlockDataPath`); not an offsets row |

## 11. Consumption by the gate

The TSV feeds the per-build gate the same way as `Tools/awdl_offsets_23J631.tsv` and
`Tools/awdl_offsets_24H232.tsv`: the VA column of each row points at a site of the build itself, so a
mechanical re-verification can locate the instruction and compare the value. The gate's selection
order for the presence layer should be: ivar base of the vif (`+0x120`/`+0x110`),
manager offset (`+0x28`/`+0x30`), `inputPacket` slot (`+0xB50`/`+0xB48`), PPP
slots (`+0xF30`/`+0xE80`/`+0xF10` and `+0xF38`/`+0xE88`/`+0xF18`), `monitorPeer` slot
(`+0x180`/`+0x178`) and then the manager's fields (`LLAValid`, `AWDLOn`, `_bonjourTriggersAWDL`,
timer, companion field). The unchanged rows (`ivars+4`, `+0x850`, threshold, length) do not need
selection.
