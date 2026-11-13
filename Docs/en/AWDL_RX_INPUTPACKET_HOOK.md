# The `inputPacket` pre-hook of `awdl0`: peer filter and PPP retention before admission

It closes R4 of `AWDL_DATA_PATH_REVIEW.md` and the §8.6 recipe of `AWDL_RX_DATA_TO_SKYWALK.md`: how
`RTLAwdInterface::deliverData` (`Source/AirLayer/AWDLInterface.cpp`) must call the project's own interface's pre-hook
`IO80211VirtualInterface::inputPacket` before admitting each Ethernet packet into
the `RxCompletionQueue`, just as Broadcom does in `AppleBCMWLANPCIeSkywalk::rxCompRingDrain`. Everything
cited is verified in headless dumps of the IDBs of the three builds (25G241, 24H232, 23J631);
anything unverified carries `?`. Dumps: `Build/ida_awdl/task_rx_inputpacket/out_{25G241,24H232,23J631}.txt`.

## 1. Summary

The pre-hook slot is `+0xB50` on 26 (25G241) and `+0xB48` on 15 (24H232) and 14 (23J631). The
family's base implementation (`IO80211VirtualInterface::inputPacket`) applies for role 6 the AWDL peer
filter over the SA of the Ethernet header (third argument, not the tag), discards without a peer,
lets the group destinations pass with special cases, and delegates admission to
`IO80211PeerManager::skywalkInputPacket`, which is who can hold the packet (bit of the `bool*`) waiting
for ioctl 415. `inputPacket` never frees the packet: the one returning `false` is freed by the
caller, the held one becomes property of the PPP queue, and the accepted one is admitted by the caller. The
symbol is exported on 15 and 26 but **not on 14**, so the only portable call is the virtual
one through the interface's vtable, which is also the one Broadcom uses.

## 2. Signature and slot per build

| build | ZTV `__ZTV23IO80211VirtualInterface` | slot | function | mangled |
|---|---|---|---|---|
| 26 (25G241) | `0xffffff8002472b78` | `+0xB50` (cell `0xffffff80024736d8`) | `0xffffff8002333cac` | `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPbb` |
| 15 (24H232) | `0xffffff80023df348` | `+0xB48` | `0xffffff80022a9742` | identical to 26's |
| 14 (23J631) | `0xffffff80022dbb88` | `+0xB48` | `0xffffff80021dc34e` | `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPb` |

C++ (26 and 15): `uintptr_t inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool)`;
C++ (14): the same **without the trailing `bool`** — 4 parameters. Register by register (verified in the three
disassemblies): `rsi` = packet, `rdx` = tag (`packet_info_tag*`), `rcx` = Ethernet header
(`ether_header*`), `r8` = holding `bool*` (on 14 it is the last argument), `r9` = `bool` (only 15/26).
The return value is tested on `AL` in the caller (`test al,al`); `true` = continue, `false` = the
caller frees. The 9 data references to the function on each build are cells of vtables of the
family and of the Broadcom subclasses; there are no direct calls.

The layer's header (`Source/AirLayer/AWDLInterface.h:114`, with the 5-argument signature and the
comment `0xB50`) follows 26's layout; porting the layout to 14/15 (slot `+0xB48`, one
fewer slot) is `AIRLAYER_PORT_14_15.md`'s work.

## 3. The `packet_info_tag` tag: what Broadcom fills before the call

The tag Broadcom passes **is not a caller buffer**: it is the per-packet *scratch*,
`PacketSkywalkScratch` at `[packet+0x78]` (26: `0xffffff800154a04b`; 24: `0xffffff80014da6f3`;
23: `0xffffff80014849cf`). There is no `memset`: concrete fields are filled from the completion message
(`BCOMIPCRxCompleteMessageV7*`), with the same offsets in the three builds:

| field | write | 26 | 24 | 23 |
|---|---|---|---|---|
| timestamps (`getRxTimeStamp(tag, msg, getItemSize()==0x28)`) | call | `0xffffff800154a06d` | `0xffffff80014da715` | `0xffffff80014849f4` |
| `+0x00` u16 ← `(int8)msg[0x1C]` (**channel**) | store | `0xffffff800154a095` | `0xffffff80014da73d` | `0xffffff8001484a1d` |
| `+0x0C` u32 ← `ratespec2rate(msg u32 at +0x14)` (**rate**) | store | `0xffffff800154a08d` | `0xffffff80014da735` | `0xffffff8001484a14` |
| `+0x18` u8 ← `msg[0x12] >> 5` (**RSSI**) | store | `0xffffff800154a079` | `0xffffff80014da721` | `0xffffff8001484a00` |
| `+0x28` u8 ← bit 24 of `msg[0x1C]` (dword) | store | `0xffffff800154a0a8` | `0xffffff80014da750` | `0xffffff8001484a22` |

`ratespec2rate` (26 `0xffffff800165D3A2`, 24 `0xffffff80015ED6C2`, 23 `0xffffff80015AEB47`) and
`getRxTimeStamp` (26 `0xffffff800154DAF4`, 24 `0xffffff80014DE148`, 23 `0xffffff8001488234`) write
the timestamps into tag fields not determined here (`?`). The total size of
`packet_info_tag`/`PacketSkywalkScratch` is also not established; the flag byte the caller clears when
freeing is at `+0x90` (26/24) and `+0xB8` (23), so the structure exceeds by a wide margin the
`+0x28` written here (`?`).

**Correction to `AWDL_RX_DATA_TO_SKYWALK.md` §6** (and to the R4-proposed fix): the description "byte 0
marks multicast (bit 0) and `+6..+11` carries the SA" corresponds to the **Ethernet header of the
third argument** (`ether_header*`), not the tag. In the three builds the family reads the multicast byte and
the SA from the `ether_header*` (`dhost[0]`, `ether+6`), and the tag only carries radio metadata (channel,
rate, RSSI, timestamps). `packet_info_tag` does not appear in the local types of any of the three IDBs
(4959 ordinals reviewed in 25G241); the layout above is the one deduced from the caller's writes.

The two call sites of `rxCompRingDrain`:

1. **NaN data interface (role 10)**: interface = `getSkywalkInterface(core, 4)` (26:
   `0xffffff800154a2bf`), `bool* = NULL` (26/24: `xor r8d,r8d` at `0xffffff800154a31f` /
   `0xffffff80014da9c1`; 23: same at `0xffffff8001484dc5`), call at `0xffffff800154a31f` /
   `0xffffff80014da9c7` / `0xffffff8001484dc8`.
2. **AWDL (role 6)**: interface = `getSkywalkInterface(core, 9)` — Broadcom's LLW interface; if it is
   null, `IOLog("%s[%d] ERROR : LLW interface does not e...")` (26: `0xffffff800154a5cc` and string at
   `0xffffff80015995E2`; 24: `0xffffff80014dad28`; 23: `0xffffff8001484fea`, line 5F8). The Ethernet
   header is formed as `getVirtualAddress() + getDataOffset()` of the packet (26:
   `0xffffff800154a624`–`0xffffff800154a642`; 23, identical pattern at `0xffffff8001484db0`–`b3`).
   `bool*` = `&var_35`, initialized to **`0xAA`** in the LLC/SNAP branch (26 `0xffffff800154a19c`, 24
   `0xffffff80014da844`, 23 `0xffffff8001484b35`) or to **`0x00`** in the raw Ethernet branch (26
   `0xffffff800154a5f1`, 24 `0xffffff80014dac99`, 23 `0xffffff8001484f5e`); the fifth `bool` goes at 0 in
   both sites (`xor r9d,r9d`). Call: **`0xffffff800154a65c`** (26), `0xffffff80014dad04` (24),
   `0xffffff8001484fc6` (23).

## 4. The holding `bool*` and the trailing `bool`

- **`bool*` (4th argument)**: `inputPacket` never writes it; it forwards it untouched as the 7th
  parameter of `skywalkInputPacket` (26: `push [rbp+var_58]` at `0xffffff8002333de2`). The role 7
  branch passes `NULL` (26: `xor r8d,r8d` at `0xffffff8002333d19`). The caller initializes it before and tests
  bit 0 after the call (26: `test [rbp+var_35],1` at `0xffffff800154a66a`; 24: `cmp …,0` at
  `0xffffff80014dad0e`; 23: `cmp …,0` at `0xffffff8001484fd4`): at 1 ⇒ the packet was **held** by the
  called stack and the caller neither frees nor admits it. The real write happens inside
  `skywalkInputPacket` (body not analyzed in any build, `?`), which is where the retention lives for
  the traffic register released by ioctl 415 (`AWDL_PEER_PRESENCE_PPP.md` §2–§4: PPP queue,
  `+0xF30`/`+0xF38`, 2000 ms ageout).
- **Trailing `bool` (5th argument, only 15/26)**: it conditions nothing inside `inputPacket`; it is
  forwarded as the 8th parameter of `skywalkInputPacket` (26: read at `0xffffff8002333dce`). Both
  Broadcom RX callers pass 0. Its downstream effect is `?`.

## 5. Return paths and packet ownership

`inputPacket` never frees the packet (its only calls: the monitor's `logRxCompletionPacket`
`[ctrl+0x40]`, peer lookup, `safeMetaCast`, `logDebug`, `incrementNumInputPacketsDropped`,
`skywalkInputPacket`). Three exits:

1. **No `IO80211PeerManager`** (`[container+0x28]` == 0; 14: `+0x30`) → returns **`true`** (26:
   `0xffffff8002333d2c`–`d2f`; 23: epilogue at `0xffffff80021dc3ca`).
2. **`false` (discard)**: no peer (§6) or silent drop of the special destinations (§6). In all
   builds the epilogue sets `eax=0` (26: `xor r12d,r12d` at `0xffffff8002333f11`/`f5d`/`e54`). The
   **caller** frees: discard counter `++` (26: `[ [this+0x10]+0x150 ]++`; 24: same at
   `0xffffff80014dad4b`; 23: `[ [var_50]+0x158 ]++` at `0xffffff800148500c`, shifted cell, `?`),
   clears bit 0x80 of the scratch flag (26/24: `[tag+0x90]`; 23: `[tag+0xB8]`,
   `0xffffff800148501b`) and calls its free callback with `(pool, packet)` — pool =
   `[ [container]+0x38 ]`, callback `var_98` = `[[g_0xffffff80015AC170]+0x150]` (26:
   `0xffffff800154a437`/`a4a6`; 24: `0xffffff80014daadf` via `0xffffff80014dad65`; 23:
   `0xffffff8001485025`).
3. **`true` with bit 0 of the `bool*` at 1 (retention)**: the caller only increments its held
   counter (26: `[…+0x148 ]++` at `0xffffff800154a47b`) and moves to the next ring message: it neither
   frees nor admits. Ownership went to the PPP wait queue of the 415 (§4); its later release
   is that of `AWDL_PEER_PRESENCE_PPP.md` §3–§4.
4. **`true` with bit 0 at 0 (pass)**: the caller obtains the queue (`getRxCompQueue`, slot `+0xD70` of the
   interface; 23: `+0xD78`, `0xffffff8001484c21`) and admits with `stagePacket(pkt, false, false)`
   (`+0x340`, 26: `0xffffff800154a44c`; 23: `0xffffff8001484d1e`) + `requestEnqueue` at the end of the batch;
   if the queue is not of the PCIe class, it frees with the same callback.

Dispatch by role inside `inputPacket` (role read from `[ [this+container]+0x58 ]` dword; 14:
`+0x50`; container = `this+0x120` on 26, `this+0x110` on 15/23): role 7 → queue into slot `+0x230` of
the PeerManager's vtable (26: `jmp rax` at `0xffffff8002333d2a`; 23: `+0x230` at `0xffffff80021dc3d4`;
15: **`+0x228`**, `0xffffff80022a97c0`) — method unnamed in the dumps (`?`). Role ≠ 6 → direct pass
to `skywalkInputPacket`, with the 6th argument = `rol == 2` (26: `setz` at `0xffffff8002333dc6`;
23: `0xffffff80021dc46a`) and the peer from the virtual lookup. Role 6 → full filter (§6).

The final delegation in all builds is
`IO80211PeerManager::skywalkInputPacket(IO80211NetworkPacket*, IO80211Peer*, packet_info_tag*, ether_header*, bool, bool, bool*, bool)`
(14: without the trailing `bool`; 7 parameters): 26 `0xffffff800225D554` (call `0xffffff8002333de7`, with
`rdx` = the peer from the lookup or `[[peerMgr+0x18]+0x530]` if multicast, `0xffffff8002333db1`–`b5`), 24
`0xffffff80021F2FEA` (call `0xffffff80022a987d`), 23 `__ZN18IO80211PeerManager18skywalkInputPacketEP20IO80211NetworkPacketP11IO80211PeerP15packet_info_tagP12ether_headerbbPb`
`0xffffff8002143C1E` (call `0xffffff80021dc480`).

## 6. The peer filter (role 6) and the special destinations

Peer lookup by the **SA of the `ether_header*`** (`ether+6`): first virtual slot `+0x120` of the
PeerManager (26: `0xffffff8002333d4e`; 24: `0xffffff80022a97e4`; 23: `0xffffff80021dc3ec`) and, if role 6
and still null, `IO80211PeerManager::findPeer(uchar*)` (26 `0xffffff8002250FE0`, call
`0xffffff8002333d6d`; 24 `0xffffff80021E6E50`, call `0xffffff80022a9803`; 23 `0xffffff80021355F2`,
call `0xffffff80021dc40b`). No peer → discard with log
`"%s: Dropping %s input packet because no peer was found for %02X:%02X:%02X:%02X:%02X:%02X\n"`
("Unicast"/"Multicast" per `dhost[0]&1`; `logDebug` with mask `0x800000000000000`, 26:
`0xffffff8002333f2c`, string `0xffffff800242EC62`; 24: `0xffffff80022a99c2`; 23:
`0xffffff80021dc5c4`), `incrementNumInputPacketsDropped` on the `IO80211AWDLPeerManager`
(26 `0xffffff800221EA84`, call `0xffffff8002333f58`; 24 `0xffffff80021B87A0`, call
`0xffffff80022a99ee`; 23 `0xffffff8002108762`, call `0xffffff80021dc5f0`) and `return false`.

With a peer, group destinations undergo extra AWDL checks before the pass (comparisons
over `dhost`, identical in the three builds; failures go to the pass, hits to the special chain):

- **`33:33:00:00:xx:xx`**: `dhost[0]==0x33` (26 `0xffffff8002333d7e`, 24 `0xffffff80022a9814`, 23
  `0xffffff80021dc41c`), `dhost[1]==0x33` (26 `0xffffff8002333dfb`, 24 `0xffffff80022a9891`, 23
  `0xffffff80021dc494`), `dhost[2]==0` (26 `0xffffff8002333e15`, 24 `0xffffff80022a98ab` via
  `test edx,edx`, 23 `0xffffff80021dc4ae`), `dhost[3]==0` (26 `0xffffff8002333e19`, 24
  `0xffffff80022a98af`, 23 `0xffffff80021dc4b2`).
- **`33:33:80:00:00:FB` exact**: `dhost[2]==0x80` (26 `0xffffff8002333e09`, 24
  `0xffffff80022a989f`, 23 `0xffffff80021dc4a2`) + `dhost[3]==0 ∧ dhost[4]==0 ∧ dhost[5]==0xFB` (26
  `0xffffff8002333f65`–`f79`, 24 `0xffffff80022a99fb`–`a13`, 23 `0xffffff80021dc5fd`–`60b`).
- **`01:00:5E:00:xx:xx`**: `dhost[0]==1` (26 `0xffffff8002333d83`, 24 `0xffffff80022a9819`, 23
  `0xffffff80021dc421`), `dhost[1]==0` (26 `0xffffff8002333d8c`, 24 `0xffffff80022a9822`, 23
  `0xffffff80021dc42a`), `dhost[2]==0x5E` (26 `0xffffff8002333d92`, 24 `0xffffff80022a9828`, 23
  `0xffffff80021dc430`), `dhost[3]==0` (same point as above).

Special chain (after the `safeMetaCast`s to `IO80211AWDLPeerManager` and `IO80211AWDLPeer`; both
null ⇒ `return false`): any failed condition ⇒ `return false` **without log nor counter**; all
met ⇒ pass to `skywalkInputPacket`.

| check | 26 | 24 | 23 |
|---|---|---|---|
| manager cast (gMetaClass `0xffffff800248D2E0`/`0xffffff80023F8FE0`/`0xffffff80022EFA10`) | `0xffffff8002333e38` | `0xffffff80022a98c4` | `0xffffff80021dc4c3` |
| peer cast (gMetaClass `0xffffff800248D060`/`0xffffff80023F8D60`/`0xffffff80022EF790`) | `0xffffff8002333e4f` | `0xffffff80022a98d7` | `0xffffff80021dc4d6` |
| manager's map object: null ⇒ discards | `[[awdlMgr+0x20]+0xA5C8]` null, `0xffffff8002333e7b` | `[[awdlMgr+0x20]+0x7EA8]` null, `0xffffff80022a9907`–`e` | `[[awdlMgr+0x20]+0x7E60]` null, `0xffffff80021dc506`–`10` |
| object's dword == 0 ⇒ discards | `[[obj]+0x10]+0x24`, `0xffffff8002333e89` | `[obj+0x24]`, `0xffffff80022a991b` | `[[obj]+0x10]+0x24`, `0xffffff80021dc51a` |
| peer's subobject byte (`[[awdlPeer+0x20]+0x…`), 0 ⇒ discards | `+0x6DE` bit 0, `0xffffff8002333e93` | `+0x6AE == 1`, `0xffffff80022a9929` | `+0x6AE == 0`, `0xffffff80021dc528` |
| manager state: bit 3 of `[X+0x168]` set and `[X+0x3A8]` ≠ 0 (26: `+0x3B0`), with `X = [[awdlMgr+0x18]+0x518/0x520]+0x120` | `0xffffff8002333eb9`–`f98` | `0xffffff80022a9948`–`a2e` | no equivalent |
| bit 6 of `[[awdlPeer+0x20]+0x5B0]` (26) / `+0x588` (24/23) ⇒ discards | `0xffffff8002333fa5` | `0xffffff80022a9a34` | `0xffffff80021dc54a`, only if `supportAWDLToNANMigration()` ≠ 0 (`0xffffff80020FC522`, call `0xffffff80021dc538`) |

On 14 the third condition is replaced by
`IO80211AWDLPeerManager::supportAWDLToNANMigration() ∧ bit 6 of [[awdlPeer+0x20]+0x588]`
(`0xffffff80021dc538`–`55f`); there is no chain `[awdlMgr+0x18]→+0x518→+0x120/+0x168/+0x3A8`. The byte
`+0x6DE/0x6AE` is the already known presence marker (`AWDL_WIFIP2PD_PRESENCE_ONCE.md`: write to 1
when the peer posts presence).

On 26, the object at `ivars+0xA5C8` is the queue of locally registered AWDL services: an
`IO80211CommandQueue::withGateAndCapacity(gate, 0x80, 0x100)` created in
`IO80211AWDLPeerManager::initWithInterface` (`0xffffff80021c2398`, saved at `0xffffff80021c23a1`), and the
dword `[[obj]+0x10]+0x24` is what `getRegisteredServicesCount` (`0xffffff80021dc598`) returns. Its
other users are `updateLocalRequests`, `buildServiceResponseTlvs` and
`buildMasterIndicationTemplate`. The identity of `+0x7EA8`/`+0x7E60` on 15/14 is presumed the same by
position `?`.

**Consequence for Bonjour.** IPv6 mDNS (`33:33:00:00:00:FB`) falls in the first case and IPv4
(`01:00:5E:00:00:FB`) in the third. With the pre-hook active, a known peer's mDNS passes only if
this Mac has at least one AWDL service registered (which happens when sharingd publishes
`_airdrop._tcp` with `IncludeAWDL`, `AWDL_AIRDROP_SIN_BLUETOOTH.md` §3) and the peer has bit 0 of
`+0x6DE` set; if not, it is discarded without log nor counter. Without the pre-hook, as today, that traffic always passes.
Enabling the pre-hook before verifying on hardware that the peer's presence bit gets set can
cut AirDrop discovery.

## 7. Symbol or vtable?

`kcsym.py` over the symtabs of the three KCs, with positive controls on 23 (`findPeer`, `logDebug`,
`setLinkHeaderOffset` = `0xf`):

| mangled | 26 (25G241) | 15 (24H232) | 14 (23J631) |
|---|---|---|---|
| `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPbb` | `0xf` (exported) | `0xf` (exported) | — |
| `…ether_headerPb` (4 arguments) | — | — | **MISSING** |

On 26 and 15 a kext can link the symbol by name; on 14 the name does not exist in the symtab (with
exported controls in the same KC) and **it can only be called through the vtable**. Also, the
qualified-superclass call (`IO80211VirtualInterface::inputPacket(...)`) would emit an undefined
reference that does not resolve on 14. The only portable way to call the pre-hook is the virtual
one on the project's own interface, which is exactly what Broadcom does (`call [rax+0B50h]`/`[rax+0B48h]`).

In the layer's object (`Build/obj_airlayer/AWDLInterface.o`) cell `+0xB50` of
`__ZTV15RTLAwdInterface` already references the kernel's base implementation: the reloc
`__ZN23IO80211VirtualInterface11inputPacket…` lands at `__DATA,__const+0xB70` = address of the object
`0x8228` = `ZTV+0x10+0xB50` (the neighboring relocs of the section, `+0xB78` and `+0xB80`, are
`forwardInfraRelayPackets` and `logSkywalkTxReqPacket`, slots 365/366 of the family). The override
declared in
`AWDLInterface.h:114` without a body makes the cell inherit the base; the symbol of
`RTLAwdInterface::inputPacket` does not exist in the object. Today, then, a virtual `inputPacket(…)`
call on `this` executes the family's filter — the same behavior Broadcom would have — and if the
class ever defines the override, the call would follow the vtable dispatch.

## 8. Code for `RTLAwdInterface::deliverData`

Replace the final stretch of the subframe loop (`Source/AirLayer/AWDLInterface.cpp`, after
`rtl_netpkt_setLinkHdrLen(pkt, 14)` and before `pkts[n++] = pkt;`) with:

```c
		rtl_netpkt_setLinkHdrLen(pkt, 14);
		/* Pre-hook +0xB50/+0xB48 (IO80211VirtualInterface::inputPacket): the
		 * family applies the AWDL peer filter and may hold the packet for the
		 * presence gate (ioctl 415), exactly as Broadcom at
		 * 0xffffff800154a65c (Docs/AWDL_RX_INPUTPACKET_HOOK.md). The family
		 * reads the addresses from the Ethernet header; the tag only carries
		 * radio metadata (channel +0x00, rate +0x0C, RSSI +0x18 in the
		 * Broadcom scratch), which this path no longer has once the RX meta
		 * is stripped, so it stays zeroed. Called through the vtable: the
		 * symbol is not exported on macOS 14 and the qualified base call
		 * would not link there. */
		uint8_t tag[0x100];
		bzero(tag, sizeof(tag));
		bool held = false;
		uintptr_t ok = inputPacket((IO80211NetworkPacket *)pkt, (packet_info_tag *)tag,
		                           (ether_header *)e, &held, false);
		if (!ok) {
			rtl_pool_free(_rxPool, pkt);
			_rxDataNotUs++;
			continue;
		}
		if (held)
			continue; /* owned now by the PPP wait queue (ioctl 415) */
		pkts[n++] = pkt;
```

Code notes:

- `inputPacket(...)` unqualified is the virtual call on `this` (dispatch through the
  vtable cell, today the family's base); it matches the Broadcom pattern and avoids the symbol
  reference that does not link on 14.
- The fifth argument `false` replicates what Broadcom passes in its two RX sites (§4).
- Retention (`held`) can only come from `skywalkInputPacket` (§4); until the class
  implements `+0xF30`/`+0xF38` (`AWDL_PEER_PRESENCE_PPP.md` §3: the base answers "Not Implemented"), a
  held packet would have no later release route — a leak risk closed by those two
  overrides (presence stage).
- Tag of 0x100 bytes at zero. It covers the fields Broadcom writes (`+0x00`, `+0x0C`, `+0x18`,
  `+0x28`) and leaves margin. `skywalkInputPacket` reads the tag up to `+0x2A` (among them the u16 of
  `+0x22`) and passes it to `IO80211PeerMonitor::incrementRxExtStats`, whose reads are not bounded.
  The real size of `packet_info_tag` is `?` (§3). Reads of zeroed fields by
  `logRxCompletionPacket`/`skywalkInputPacket` are presumed harmless (`?`).
- Do not enable the pre-hook before having the presence chain: the group filter of §6 can
  discard the peer's mDNS. If it is enabled earlier for testing, do it behind a boot-arg. Propagating the
  RSSI and channel of `injectFrame` (today lost when entering `deliverData`) would leave the tag
  identical to Broadcom's; it requires passing both values as parameters.

## 9. Unverified (`?`)

1. Body of `IO80211PeerManager::skywalkInputPacket` in the three builds: real writer of the `bool*`,
   enqueue into the PPP queue, meaning of the trailing `bool` and reads it makes of the tag. VAs: §5.
2. Total size of `packet_info_tag`/`PacketSkywalkScratch` and the offsets of the timestamps written by
   `getRxTimeStamp`.
3. Fields read by `IO80211InterfaceMonitor::logRxCompletionPacket` (only runs if `[ctrl+0x40]` ≠ 0)
   from a zeroed tag.
4. Method of slot `+0x230` (26/23) / `+0x228` (15) of the PeerManager's vtable that role 7
   transfers the call to.
5. Identity of `+0x7EA8`/`+0x7E60` on 15/14 (on 26, `+0xA5C8` is the registered services queue, §6).
6. Discard counter of the caller on 23: cell `+0x158` over `[var_50]` vs `+0x150` over
   `[var_50]+0x10` on 24/26 — layout shift not interpreted.
7. That on hardware the layer's `awdl0` has role 6 at `[ [this+0x120]+0x58 ]` (26) — without it the
   filter does not apply (pending the T tests).
