# Port of the AWDL channel sequence to macOS 14.8.9 (23J631) and macOS 15.8.2 (24H232)

Reference document for supporting the Apple Wireless Direct Link (AWDL) channel sequence on earlier macOS versions, taking as origin the implementation documented for macOS 26.7.1 (25G241, Darwin 25.6.0) in `Docs/AWDL_SECUENCIA_CON_INFRA.md`, `Docs/AWDL_SECUENCIA_129_EMISOR.md`, `Docs/AWDL_CHANNEL_STRATEGY.md`, `Docs/AWDL_CARD_CAPS_14_15.md`, `Docs/AWDL_DATA_PATH_PORT_14_15.md` and `Docs/AWDL_SECUENCIAS_OBSERVADAS.md`.

The cited code virtual addresses (VA) are instruction heads in the x86_64 kernel collections; the slot-type tables, the dual-band gates, the tokens and the cited functions were checked in the disassembly, and the §2 ivar displacements without their own VA are taken from the reads of those functions `?`. Collections (`boot_awdl.kc.i64`, `boot_24H232.kc.i64`, `boot_23J631.kc.i64`). The observed object bases are `pm = *(this + 0x18)` (base controller `IO80211Controller`/`IO80211PeerManager`) and `awdlIvars = *(this + 0x20)` (specific manager `IO80211AWDLPeerManager`).

---

## 1. Executive summary

The infrastructure query model and 16-slot channel sequence construction are architecturally identical in macOS 14.8.9 (23J631), macOS 15.8.2 (24H232) and macOS 26.7.1 (25G241):
1. **Invariant pull model**: The manager never receives the infrastructure channel via *push*; it actively pulls it through the WCL IOUC token `0xC02869C9` (type 4, 16 bytes) by calling `IO80211Controller::getInfraChannel` / `apple80211getCHANNEL`.
2. **Identical tokens and ioctls**: All magic numbers and 64-bit immediates are 100% identical across the three versions: WCL query token `0xC02869C9`, internal routing token `0x802869C8`, request 129 (`AWDL_SYNC_CHANNEL_SEQUENCE`, `0x81802869C8`), request 132 (`AWDL_MASTER_CHANNEL`, `0x84802869C8`), request 205 (`AWDL_SECONDARY_MASTER_CHANNEL`, `0xCD802869C8`), request 116 (`AWDL_SYNC_PARAMETERS`, `0x74802869C8`), request 117 (`AWDL_SYNC_ENABLED`, `0x75802869C8`) and request 341 (`GET awdl_cap`, `0x155C02869C9`).
3. **400-byte (`0x190`) payload 129**: Identical header structure and 16 12-byte slots, with change suppression via `memcmp(v6, prev_seq, 0x190)`.
4. **Internal layout variations**: The internal fields of `pm` and `awdlIvars` undergo systematic shifts between builds:
   - In `pm`: fields shift by `-0x10` to `-0x38` bytes in 15.8.2 and by `-0x12` to `-0x40` bytes in 14.8.9 relative to 26.7.1.
   - In `awdlIvars`: the master and social channel zone shifts `+0x08` bytes in 15.8.2 and `-0x38` bytes in 14.8.9. The 16-slot buffer resides at `+0xA774` (26.7.1), `+0x8054` (15.8.2) and `+0x8004` (14.8.9).
5. **Concrete functional differences**:
   - **Dual-band gate (`configureValidAWDLChannels`)**: In 14.8.9 the card capabilities block resides locally embedded at `pm + 0xB18` (evaluated as `[pm + 0xB1F] & 0x20`), with `IO80211Controller::isCardCapabilitiesSet` not existing. In 15.8.2 it is accessed via `*(ctrl + 0x120) + 0xB27 & 0x20`, and in 26.7.1 via `*(ctrl + 0x120) + 0xB37 & 0x20`.
   - **Static symbol `__ZL13_channel6Info`**: Present in 26.7.1 (`0xffffff80023aaff0`) and 15.8.2 (`0xffffff8002317fc0`) with `{1, 6, 0xA}`. In 14.8.9 this symbol is absent and was replaced directly by immediate constants in code (`mov rax, 600000001h` and `mov dword ptr [...], 0Ah`).
   - **Slot-type table `__ZL20_channelSeqSlotTypes`**: Has 54 rows (states 0..53) in 14.8.9 and 15.8.2, versus 56 rows in 26.7.1. State 38 (*Time Sync Retro*) was introduced in macOS 26, causing a +1 shift in state indices from 38 on (e.g. *High Thruput* is state 40 in 14/15 and 41 in 26; *AWDL Fast Discovery* is state 44 in 14/15 and 45 in 26). Also, state 55 (*RTG AirDrop*) was appended at the end in macOS 26.
   - **Row 4 (*Low Power*)**: It is the only row differing between 14.8.9 and 15.8.2. In 14.8.9 slots 0 and 9 use types 3 and 4 (`3 1 3 1 1 1 7 7 2 4 3 1 1 1 7 7`), whereas in 15.8.2 and 26.7.1 they evolved to types 9 and 7 (`9 1 3 1 1 1 7 7 2 7 3 1 1 1 7 7`), allowing dynamic infrastructure insertion and fallback to channel 6.

---

## 2. Comparative table of observed ivars

| Field / Meaning | Base | 25G241 (ref) | 23J631 (14.8.9) | 24H232 (15.8.2) | Delta 23 vs 25 | Delta 24 vs 25 |
|---|---|---|---|---|---|---|
| `pm` pointer (`ctrl`) | `this` | `+0x18` | `+0x18` | `+0x18` | 0 | 0 |
| `awdlIvars` pointer | `this` | `+0x20` | `+0x20` | `+0x20` | 0 | 0 |
| `channelConfigured` | `pm` | `+0x548` (1352) | `+0x530` (1328) | `+0x538` (1336) | -0x18 (-24 B) | -0x10 (-16 B) |
| `beacon_period` | `pm` | `+0x54C` (1356) | `+0x534` (1332) | `+0x53C` (1340) | -0x18 (-24 B) | -0x10 (-16 B) |
| `dtim_period` | `pm` | `+0x550` (1360) | `+0x538` (1336) | `+0x540` (1344) | -0x18 (-24 B) | -0x10 (-16 B) |
| `vifc` (virtual interface) | `pm` | `[pm+0x560]` | `[pm+0x548]` | `[pm+0x550]` | -0x18 (-24 B) | -0x10 (-16 B) |
| `countryCode` dirty flag | `pm` | `+0x6F8` (1784) | `+0x6D8` (1752) | `+0x6E8` (1768) | -0x20 (-32 B) | -0x10 (-16 B) |
| 6 GHz flag in `pm` | `pm` | `+0x72E` (1838) | `+0x6FE` (1790) | `+0x71E` (1822) | -0x30 (-48 B) | -0x10 (-16 B) |
| Card capabilities block | `pm`/`ctrl` | `*(ctrl+0x120)+0xB30` | `pm+0xB18` (embedded) | `*(ctrl+0x120)+0xB20` | n/a (local) | -0x10 (-16 B) |
| Dual band bit (0x20) | `pm`/`ctrl` | `*(ctrl+0x120)+0xB37` | `pm+0xB1F` | `*(ctrl+0x120)+0xB27` | n/a (local) | -0x10 (-16 B) |
| BSSID data | `pm` | `+0x930` (2352) | `+0x91E` (2334) | `+0x920` (2336) | -0x12 (-18 B) | -0x10 (-16 B) |
| SSID length | `pm` | `+0x938` (2360) | `+0x924` (2340) | `+0x928` (2344) | -0x14 (-20 B) | -0x10 (-16 B) |
| `countryCode` cache | `pm` | `+0x9A0` (2464) | `+0x968` (2408) | `+0x968` (2408) | -0x38 (-56 B) | -0x38 (-56 B) |
| Infra channel info `{v, ch, fl}` | `pm` | `+0x9BC/0x9C0/0x9C4` | `+0x97C/0x980/0x984` | `+0x984/0x988/0x98C` | -0x40 (-64 B) | -0x38 (-56 B) |
| Current AWDL channel `{v, ch, fl}` | `awdlIvars` | `+0x1A0/0x1A4/0x1A8` | `+0x1A0/0x1A4/0x1A8` | `+0x1A0/0x1A4/0x1A8` | 0 | 0 |
| Timer / re-arm store | `awdlIvars` | `+0x1D8` (472) | `+0x1D8` (472) | `+0x1D8` (472) | 0 | 0 |
| Gate ioctl `+0x300 == 1` | `awdlIvars` | `+0x300` (768) | `+0x300` (768) | `+0x300` (768) | 0 | 0 |
| `scheduleState` | `awdlIvars` | `+0x28E4` (10468) | `+0x28B4` (10420) | `+0x28CC` (10444) | -0x30 (-48 B) | -0x18 (-24 B) |
| `chanSwitchCapable` mirror | `awdlIvars` | `+0x3ECC` (16076) | `+0x3E9C` (16028) | `+0x3EB4` (16052) | -0x30 (-48 B) | -0x18 (-24 B) |
| Infra DFS / allowed bytes | `awdlIvars` | `+0x3ED8` / `+0x3ED9` | `+0x3EA4` / `+0x3EA5` | `+0x3EC0` / `+0x3EC1` | -0x34 (-52 B) | -0x18 (-24 B) |
| Sync off gate (bit 1) | `awdlIvars` | `+0x3EE0 bit 1` | `+0x3EC8 bit 1` | `+0x3EE8 bit 1` | -0x18 (-24 B) | +0x08 (+8 B) |
| Link-down gate | `awdlIvars` | `+0x3EE4` (16100) | `+0x3ECC` (16076) | `+0x3EEC` (16108) | -0x18 (-24 B) | +0x08 (+8 B) |
| CarPlay active flag | `awdlIvars` | `+0x3F04` (16132) | `+0x3EE8` (16104) | `+0x3F0C` (16140) | -0x1C (-28 B) | +0x08 (+8 B) |
| Peer derive-to-follow | `awdlIvars` | `+0x3F60` / `+0x3F78` | `+0x3F60` | `+0x3F68` / `+0x3F80` | variable | +0x08 (+8 B) |
| `chanSwitchCapable` | `awdlIvars` | `+0x3FA1` (16289) | `+0x3F89` (16265) | `+0x3FA9` (16297) | -0x18 (-24 B) | +0x08 (+8 B) |
| Mirror 2 `chanSwitchCapable` | `awdlIvars` | `+0x3FA8` (16296) | `+0x3F90` (16272) | `+0x3FB0` (16304) | -0x18 (-24 B) | +0x08 (+8 B) |
| Stored infra restore | `awdlIvars` | `+0x40F5` / `+0x40F8` | `+0x40C5` / `+0x40C8` | `+0x40FD` / `+0x4100` | -0x30 (-48 B) | +0x08 (+8 B) |
| Social channels `{6, 149, 44}` | `awdlIvars` | `+0x4290/94/98` | `+0x4258/5C/60` | `+0x4298/9C/A0` | -0x38 (-56 B) | +0x08 (+8 B) |
| Social mask (u16) | `awdlIvars` | `+0x429C` (17052) | `+0x4264` (17008) | `+0x42A4` (17060) | -0x38 (-56 B) | +0x08 (+8 B) |
| Primary master `{v, ch, fl}` | `awdlIvars` | `+0x42A0/A4/A8` | `+0x4268/6C/70` | `+0x42A8/AC/B0` | -0x38 (-56 B) | +0x08 (+8 B) |
| Secondary master `{v, ch, fl}` | `awdlIvars` | `+0x42AC/B0/B4` | `+0x4274/78/7C` | `+0x42B4/B8/BC` | -0x38 (-56 B) | +0x08 (+8 B) |
| Channel list destination | `awdlIvars` | `+0x42D4` (17108) | `+0x429C` (17052) | `+0x42DC` (17116) | -0x38 (-56 B) | +0x08 (+8 B) |
| `numChanSpecs` (u16) | `awdlIvars` | `+0x42E0` (17120) | `+0x42A8` (17064) | `+0x42E8` (17128) | -0x38 (-56 B) | +0x08 (+8 B) |
| Inactivity threshold (2000 ms) | `awdlIvars` | `+0x9144` (37188) | `+0x69E8` (27112) | `+0x6A2C` (27180) | -0x275C | -0x2718 |
| Command queue / `lockMap` | `awdlIvars` | `+0xA5C8` (42440) | `+0x7E60` (32352) | `+0x7EA8` (32424) | -0x2768 | -0x2720 |
| `airplaySink` | `awdlIvars` | `+0xA648` (42568) | `+0x7EE0` (32480) | `+0x7F28` (32552) | -0x2768 | -0x2720 |
| `airPlayServices` | `awdlIvars` | `+0xA650` (42576) | `+0x7EE8` (32488) | `+0x7F30` (32560) | -0x2768 | -0x2720 |
| `rdLink` / `companionLink` | `awdlIvars` | `+0xA654` / `+0xA658` | `+0x7EEC` / `+0x7EF0` | `+0x7F34` / `+0x7F38` | -0x2768 | -0x2720 |
| `nonAirPlayServices` | `awdlIvars` | `+0xA65C` (42588) | `+0x7EF4` (32496) | `+0x7F3C` (32572) | -0x2768 | -0x2720 |
| `airplaySoloModeEnabled` | `awdlIvars` | `+0xA6A4` (42660) | `+0x7F34` (32564) | `+0x7F84` (32644) | -0x2770 | -0x2720 |
| Slot encoding byte | `awdlIvars` | `+0xA772` (42866) | `+0x8002` (32770) | `+0x8052` (32850) | -0x2770 | -0x2720 |
| Sequence slots buffer (129) | `awdlIvars` | `+0xA774` (42868) | `+0x8004` (32772) | `+0x8054` (32852) | -0x2770 | -0x2720 |
| Previous sequence backup | `awdlIvars` | `+0xA8F4` (43252) | `+0x8184` (33156) | `+0x81D4` (33236) | -0x2770 | -0x2720 |
| `dynSdbForceChanSeq` | `awdlIvars` | `+0xAA74` (43636) | `+0x8304` (33540) | `+0x8354` (33620) | -0x2770 | -0x2720 |
| `wifidOnly` | `awdlIvars` | `+0xF638` (63032) | `+0xC9F8` (51704) | `+0xBEB8` (48824) | -0x2C40 | -0x3780 |
| Assisted discovery bit | `awdlIvars` | `+0xFA3A bit 0` | `+0x7FB8 bit 0` | `+0xC2BA bit 0` | variable | -0x3780 |

---

## 3. §3: GET of the infrastructure channel

| Item | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Note |
|---|---|---|---|---|---|
| A1 `getInfrastructureState` | VA `0xffffff80021da84a`<br>Callers: `0x21d9030` (`interfaceStateChange`), `0x21de938` (`reportDataPathEvents`) | VA `0xffffff80020ca152`<br>Callers: `0x20c890c` (`interfaceStateChange`), `0x20cd6e6` (`reportDataPathEvents`) | VA `0xffffff8002178616`<br>Callers: `0x2176e4d` (`interfaceStateChange`), `0x217bb69` (`reportDataPathEvents`) | no | Same callers and identical query flow; the destination offsets in `pm` and `awdlIvars` differ. |
| A1.1 WCL IOUC `getInfraChannel` | Token `0xC02869C9`<br>Type 4, 16 B<br>Call `0x21da898`<br>Bridge `0x229520e` | Token `0xC02869C9`<br>Type 4, 16 B<br>Call `0x20ca1bd`<br>Bridge `0x216ca92` | Token `0xC02869C9`<br>Type 4, 16 B<br>Call `0x2178664`<br>Bridge `0x222795c` | yes | Token `0xC02869C9`, type 4, 16 B buffer and `apple80211getCHANNEL` fallback method identical. |
| A1.2 `getBSSIDData` | Type 9, 12 B<br>Call `0x21da98f`<br>Destination `pm+0x930` | Type 9, 12 B<br>Call `0x20ca30d`<br>Destination `pm+0x91E` | Type 9, 12 B<br>Call `0x2178730`<br>Destination `pm+0x920` | no | Identical type 9 query; store shifted in `pm` (`+0x91E` on 14, `+0x920` on 15 vs `+0x930` on 26). |
| A1.3 `getSSIDData` | Type 1, 40 B (0x28)<br>Call `0x21da9e5`<br>Length `pm+0x938` | Type 1, 40 B<br>Call `0x20ca37f`<br>Length `pm+0x924` | Type 1, 40 B<br>Call `0x2178786`<br>Length `pm+0x928` | no | Identical type 1 query; length at `pm+0x924` (14) and `pm+0x928` (15) vs `pm+0x938` (26). |
| A1.4 `getBeaconPeriod` & `getDTIMInt` | Beacon type 0xA6 (8 B) at `pm+0x54C`<br>DTIM type 0x29 (8 B) at `pm+0x550` | Beacon type 0xA6 at `pm+0x534`<br>DTIM type 0x29 at `pm+0x538` | Beacon type 0xA6 at `pm+0x53C`<br>DTIM type 0x29 at `pm+0x540` | no | Identical WCL queries; `pm` fields shifted `-0x18` on 14 and `-0x10` on 15 vs 26. |
| A1.5 Infra compare/store block | Store `pm+0x9BC/0x9C0/0x9C4`<br>Band change mask: `0x2018`<br>Restore: `+0x40F5/+0x40F8` | Store `pm+0x97C/0x980/0x984`<br>Band change mask: `0x2018`<br>Restore: `+0x40C5/+0x40C8` | Store `pm+0x984/0x988/0x98C`<br>Band change mask: `0x2018`<br>Restore: `+0x40FD/+0x4100` | no | `0x2018` mask and logic identical; fields shifted `-0x40` on 14 and `-0x38` on 15. |
| A2 `interfaceStateChange` | VA `0xffffff80021d8cc8`<br>vtable `0x245a868` slot 55 (+0x1B8)<br>Cell `0x245aa20` | VA `0xffffff80020c85a2`<br>vtable `0x22ca618` slot 55 (+0x1B8)<br>Cell `0x22ca7d0` | VA `0xffffff8002176af0`<br>vtable `0x23ca1e8` slot 55 (+0x1B8)<br>Cell `0x23ca3a0` | no | Virtual slot 55 (+0x1B8) identical; function and cell VAs differ per build. |
| A2.1 Link-down gate | Gate `awdlIvars+0x3EE4 != 0`<br>vifc `[pm+0x560]`<br>vtable slot `+0xB88`, `cmp eax, 1`<br>Check at `0x21d8ea1` | Gate `awdlIvars+0x3ECC != 0`<br>vifc `[pm+0x548]`<br>vtable slot `+0xB88`, `cmp eax, 1`<br>Check at `0x20c8773` | Gate `awdlIvars+0x3EEC != 0`<br>vifc `[pm+0x550]`<br>vtable slot `+0xB88`, `cmp eax, 1`<br>Check at `0x2176cc1` | no | Same discard with log `"Interface link is down, nothing to do"`. Confirms `AWDL_DATA_PATH_PORT_14_15.md` §5.3. |
| A3 `reportDataPathEvents` | VA `0xffffff80021dd8f4`<br>Dispatches Sources: 9, 11, 5, 6<br>Calls `getInfrastructureState` | VA `0xffffff80020ccaaa`<br>Dispatches Sources: 9, 11, 5, 6<br>Calls `getInfrastructureState` | VA `0xffffff800217adb8`<br>Dispatches Sources: 9, 11, 5, 6<br>Calls `getInfrastructureState` | yes | Identical set of sources {9, 11, 5, 6} and infrastructure refresh prior to Source 5 at `0x20cd6e6` (14) and `0x217bb69` (15). |
| A4 Dead infra setters | `setInfraChannel` (`0x2254aee`)<br>`setInfraChannelInfo` (`0x225edec`)<br>`setInfraChannelSpec` (`0x21fc626`)<br>`vif::setInfraChannel` (`0x2330a0c`)<br>0 xrefs | `setInfraChannel` (`0x213abc2`)<br>`setInfraChannelInfo` (`0x2144daa`)<br>`setInfraChannelSpec` (`0x20e8a68`)<br>`vif::setInfraChannel` (`0x21d90e6`)<br>0 xrefs | `setInfraChannel` (`0x21ea73c`)<br>`setInfraChannelInfo` (`0x21f45fc`)<br>`setInfraChannelSpec` (`0x2198112`)<br>`vif::setInfraChannel` (`0x22a64c0`)<br>0 xrefs | yes | In all three versions the four symbols exist as dead stubs without active references nor presence in vtables. |
| A5 `StateChangedSource` table | VA `0xffffff800245d450`<br>15 strings: 0 Init, 1 BSSIDChanged, 2 Mdns, ..., 14 AWDL exiting | VA `0xffffff80022ccb10`<br>15 strings in identical order and semantics | VA `0xffffff80023ccc28`<br>15 strings in identical order and semantics | yes | Table of 15 diagnostic strings for transitions in `interfaceStateChange` identical. |

---

## 4. §4: Strategy and master buffers

| Item | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Note |
|---|---|---|---|---|---|
| B1 `configureValidAWDLChannels` | VA `0xffffff80021caa40`<br>Callers: `0x21c5b9b`, `0x21d9996`<br>Followed by update6G, find6G, selectChannels | VA `0xffffff80020bb342`<br>Callers: `0x20b5be8`, `0x20c923a`<br>Followed by update6G, find6G, selectChannels | VA `0xffffff8002168f52`<br>Callers: `0x2164300`, `0x21776cd`<br>Followed by update6G, find6G, selectChannels | yes | Same call topology from `configure` and `interfaceStateChange` and same subsequent sequence. |
| B2 Dual band gate | Check: `0x21cab1e`<br>`*(ctrl+0x120)+0xB37 & 0x20`<br>Zeroes qword `+0x4294` | Check: `0x20bb433`<br>`[pm+0xB1F] & 0x20`<br>Zeroes qword `+0x425C` | Check: `0x2169030`<br>`*(ctrl+0x120)+0xB27 & 0x20`<br>Zeroes qword `+0x429C` | no | In 14.8.9 the block resides at `pm+0xB18` (no indirection or `isCardCapabilitiesSet`). In 15.8.2 the offset is `0xB27`. Both zero 149 and 44. |
| B3 `getCountryCode` | VA `0xffffff8002295416`<br>Cache `pm+0x9A0/0x9A4`<br>Dirty flag `pm+0x6F8` | VA `0xffffff800216cc1e`<br>Cache `pm+0x968/0x96C`<br>Dirty flag `pm+0x6D8` | VA `0xffffff8002227b64`<br>Cache `pm+0x968/0x96C`<br>Dirty flag `pm+0x6E8` | no | Same cache logic in `pm`; fields shifted. |
| B4 Channel list ioctl (207/379) | Token `0xC02869C9`<br>Req 207 (379 if `pm+0x72E != 0`)<br>Destination `+0x42D4`, u16 `+0x42E0` | Token `0xC02869C9`<br>Req 207 (379 if `pm+0x6FE != 0`)<br>Destination `+0x429C`, u16 `+0x42A8` | Token `0xC02869C9`<br>Req 207 (379 if `pm+0x71E != 0`)<br>Destination `+0x42DC`, u16 `+0x42E8` | no | Destination buffer shifted; tokens 207/379, bzero size (`0x2720`) and defaults preservation (149/44) on failure identical. |
| B5 `isValidChannel` (5-arg) | VA `0xffffff80021d1d4c`<br>Bands 8 and 16 (36..183)<br>ETSI table `0x2462350` (39 countries)<br>"IL" rule, bits 0x1..0x100 | VA `0xffffff80020c2104`<br>Bands 8 and 16 (36..183)<br>ETSI table `0x22d0640` (39 countries)<br>"IL" rule, bits 0x1..0x100 | VA `0xffffff800216ffb4`<br>Bands 8 and 16 (36..183)<br>ETSI table `0x23d1900` (39 countries)<br>"IL" rule, bits 0x1..0x100 | yes | Regulatory filtering, 39 ETSI entries, Israel restriction rule and returned bits 100% identical. |
| B6 Saved social channels | 2G (6) at `+0x4290`<br>5G (149) at `+0x4294`<br>5G Alt (44) at `+0x4298` | 2G (6) at `+0x4258`<br>5G (149) at `+0x425C`<br>5G Alt (44) at `+0x4260` | 2G (6) at `+0x4298`<br>5G (149) at `+0x429C`<br>5G Alt (44) at `+0x42A0` | no | Shifts: `-0x38` on 14, `+0x08` on 15 vs 26. Same default values (6, 149, 44) and validation. |
| B7 Slot encoding byte | Condition `caps & 0x38` and `dynSdbForceChanSeq` (`+0xAA74 != 1`)<br>Writes 1 at `+0xA772` | Condition `caps & 0x38` and `dynSdbForceChanSeq` (`+0x8304 != 1`)<br>Writes 1 at `+0x8002` | Condition `caps & 0x38` and `dynSdbForceChanSeq` (`+0x8354 != 1`)<br>Writes 1 at `+0x8052` | no | Identical binary condition; fields shifted in `awdlIvars`. |
| B8 Infra DFS / allowed bytes | Word default `0x0100`<br>`+0x3ED8` (DFS), `+0x3ED9` (allowed)<br>DFS mask 1, flags `0x2018` | Word default `0x0100`<br>`+0x3EA4` (DFS), `+0x3EA5` (allowed)<br>DFS mask 1, flags `0x2018` | Word default `0x0100`<br>`+0x3EC0` (DFS), `+0x3EC1` (allowed)<br>DFS mask 1, flags `0x2018` | no | Identical two-step validation; default word `0x0100` emitted in all three cases. |
| B9 `initParamsToDefaults` | VA `0xffffff80021c7a40`<br>Qword `0x9500000006` at `+0x4290`<br>44 at `+0x4298`, flags 8 at `+0x42A8`<br>Idle 2000 at `+0x9144`<br>Mirror switch at `+0x3FA8` | VA `0xffffff80020b7950`<br>Qword `0x9500000006` at `+0x4258`<br>44 at `+0x4260`, flags 8 at `+0x4270`<br>Idle 2000 at `+0x69E8`<br>Mirror switch at `+0x3F90` | VA `0xffffff8002165fdc`<br>Qword `0x9500000006` at `+0x4298`<br>44 at `+0x42A0`, flags 8 at `+0x42B0`<br>Idle 2000 at `+0x6A2C`<br>Mirror switch at `+0x3FB0` | no | Identical seeding of master channels (6, 149, 44, flags 8) and 2000 ms inactivity threshold. |
| B10 `_zeroChanInfo` / `_channel6Info` | `_zeroChanInfo` `0x2488a88` `{1,0,0}`<br>`_channel6Info` `0x23aaff0` `{1,6,0xA}` | `_zeroChanInfo` `0x22ee6a8` `{1,0,0}`<br>`_channel6Info`: **ABSENT** | `_zeroChanInfo` `0x23f66f8` `{1,0,0}`<br>`_channel6Info` `0x2317fc0` `{1,6,0xA}` | no | In 14.8.9 there is no static symbol for channel 6; the compiler emitted immediates `0x600000001` and `0xA` in code. |
| C1 `selectChannelsToUseForAWDL` | VA `0xffffff80021dc2f2`<br>Primary `+0x42A0..+0x42A8`<br>Secondary `+0x42AC..+0x42B4`<br>u16 mask `+0x429C`<br>Current `+0x1A0`, re-arm `+0x1D8` | VA `0xffffff80020cb4c8`<br>Primary `+0x4268..+0x4270`<br>Secondary `+0x4274..+0x427C`<br>u16 mask `+0x4264`<br>Current `+0x1A0`, re-arm `+0x1D8` | VA `0xffffff800217984e`<br>Primary `+0x42A8..+0x42B0`<br>Secondary `+0x42B4..+0x42BC`<br>u16 mask `+0x42A4`<br>Current `+0x1A0`, re-arm `+0x1D8` | no | Bit rules in mask (bit0 master, bit1 44, bit2 149) and offsets `+0x1A0` / `+0x1D8` identical. |
| C2 `setAwdlMasterChannel` (132/205) | VAs `0x221d4de` / `0x221d314`<br>Gate `awdlIvars+0x300 == 1`<br>Token `0x802869C8` with 132 and 205 | VAs `0x2107326` / `0x2107178`<br>Gate `awdlIvars+0x300 == 1`<br>Token `0x802869C8` with 132 and 205 | VAs `0x21b7466` / `0x21b72b8`<br>Gate `awdlIvars+0x300 == 1`<br>Token `0x802869C8` with 132 and 205 | yes | Gate `+0x300 == 1`, RouteInternal token `0x802869C8` and internal ioctl numbers 132 and 205 identical. |
| C3 Mode 0 in `monitorAWDLState` | Calls `setStrategy(0, 5, false)`<br>Copies infra to primary and 0 to secondary<br>Calls `programMasterChannels` and `setScheduleState(state, 0x14, true)` | Calls `setStrategy(0, 5, false)`<br>Copies infra to primary and 0 to secondary<br>Calls `programMasterChannels` and `setScheduleState(state, 0x14, true)` | Calls `setStrategy(0, 5, false)`<br>Copies infra to primary and 0 to secondary<br>Calls `programMasterChannels` and `setScheduleState(state, 0x14, true)` | yes | Same conditional substitution behavior toward `FollowsInfra` under four business conditions. |
| C4 Mode 0 in `interfaceStateChange` | Block `0x21d9a44 - 0x21d9aa4`<br>Copies infra if `strategy == 0` | Block `0x20c9324`<br>Copies infra if `strategy == 0` | Block `0x2177785`<br>Copies infra if `strategy == 0` | yes | Same substitution block executed after `selectChannelsToUseForAWDL`. |
| C5 Five conditions of return to mode 0 | `airplaySink != 0` (`+0xA648`)<br>`soloMode == 0` (`+0xA6A4`)<br>`strategy == 1`<br>`nonAirPlay == 0 && airPlay != 0`<br>Inactivity vs 2000 (`+0x9144`) | `airplaySink != 0` (`+0x7EE0`)<br>`soloMode == 0` (`+0x7F34`)<br>`strategy == 1`<br>`nonAirPlay == 0 && airPlay != 0`<br>Inactivity vs 2000 (`+0x69E8`) | `airplaySink != 0` (`+0x7F28`)<br>`soloMode == 0` (`+0x7F84`)<br>`strategy == 1`<br>`nonAirPlay == 0 && airPlay != 0`<br>Inactivity vs 2000 (`+0x6A2C`) | no | The five logical conditions are identical; fields shifted. |
| C6 Strategy choice in `interfaceStateChange` | Evaluates `airplaySink` (`+0xA648`), `chanSwitchCapable` (`+0x3FA1`), `soloMode` (`+0xA6A4`), `nonAirPlay` (`+0xA65C`)<br>Calls `setStrategy` (reason 4)<br>Sole writer `+0x3FA1=1` in `configure` | Evaluates `airplaySink` (`+0x7EE0`), `chanSwitchCapable` (`+0x3F89`), `soloMode` (`+0x7F34`), `nonAirPlay` (`+0x7EF4`)<br>Calls `setStrategy` (reason 4)<br>Sole writer `+0x3F89=1` in `configure` | Evaluates `airplaySink` (`+0x7F28`), `chanSwitchCapable` (`+0x3FA9`), `soloMode` (`+0x7F84`), `nonAirPlay` (`+0x7F3C`)<br>Calls `setStrategy` (reason 4)<br>Sole writer `+0x3FA9=1` in `configure` | no | Same bifurcation toward `Solo mode` and single write point in `configure`. |

---

## 5. §5: Slot construction and emission 129/132/205

| Item | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Note |
|---|---|---|---|---|---|
| C7 Schedule state | Token 116 `0x74802869C8`<br>`channelConfigured` at `pm+0x548`<br>Sync off gate `+0x3EE0 bit 1`<br>State 0/3/5<br>`enableAWDL` does not touch strategy | Token 116 `0x74802869C8`<br>`channelConfigured` at `pm+0x530`<br>Sync off gate `+0x3EC8 bit 1`<br>State 0/3/5<br>`enableAWDL` does not touch strategy | Token 116 `0x74802869C8`<br>`channelConfigured` at `pm+0x538`<br>Sync off gate `+0x3EE8 bit 1`<br>State 0/3/5<br>`enableAWDL` does not touch strategy | no | Token 116, token 117 (`0x75802869C8`), GET 341 (`0x155C02869C9`) and state logic identical. |
| C8 Service counters | `getvalidAWDLServices` (`0x21da6de`)<br>Counters `+0xA650..+0xA65C`<br>Callback `_countAirPlayServices`<br>Strings "ip6","arpa","airplay","raop"<br>CarPlay flag `+0x3F04 bit 0` | `getvalidAWDLServices` (`0x20c9fe6`)<br>Counters `+0x7EE8..+0x7EF4`<br>Callback `_countAirPlayServices`<br>Strings "ip6","arpa","airplay","raop"<br>CarPlay flag `+0x3EE8 bit 0` | `getvalidAWDLServices` (`0x21784aa`)<br>Counters `+0x7F30..+0x7F3C`<br>Callback `_countAirPlayServices`<br>Strings "ip6","arpa","airplay","raop"<br>CarPlay flag `+0x3F0C bit 0` | no | Counting rule, filtering by type `0x21/0x0C` and CarPlay bypass identical. |
| D1 `setChannelSequenceByState` | VA `0xffffff80021f53aa`<br>Table `0x23aa1f0` (56 rows)<br>21-case switch without bias (`0x21f5498`) | VA `0xffffff80020e1648`<br>Table `0x223d1d0` (54 rows)<br>21-case switch without bias (`0x20e1730`) | VA `0xffffff8002190fca`<br>Table `0x2317240` (54 rows)<br>21-case switch without bias (`0x21910b2`) | no | Identical 21-case switch without bias. Table with 54 rows in 14/15 vs 56 in 26. |
| D2 Slot types 1, 2, 3, 4 | Type 1: `_zeroChanInfo`<br>Type 2: Primary `+0x42A0`<br>Type 3: Secondary `+0x42AC` (or prim)<br>Type 4: Current `+0x1A0`<br>6G guard at `+0xA77D bit 0x20` | Type 1: `_zeroChanInfo`<br>Type 2: Primary `+0x4268`<br>Type 3: Secondary `+0x4274` (or prim)<br>Type 4: Current `+0x1A0`<br>6G guard at `+0x800D bit 0x20` | Type 1: `_zeroChanInfo`<br>Type 2: Primary `+0x42A8`<br>Type 3: Secondary `+0x42B4` (or prim)<br>Type 4: Current `+0x1A0`<br>6G guard at `+0x805D bit 0x20` | no | Identical channel selection semantics; current channel preserved at `+0x1A0`. |
| D3 Slot type 7 | VA `0x21f5ef6`<br>3 conditions: `infraAllowed`, no flag `0x2000`, AirPlay count > 0 | VA `0x20e22ac`<br>3 conditions: `infraAllowed`, no flag `0x2000`, AirPlay count > 0 | VA `0x2191ca4`<br>3 conditions: `infraAllowed`, no flag `0x2000`, AirPlay count > 0 | yes | Same evaluation of the 3 conditions in `addInfraToMinimumChannelSequence`. If they fail, emits `_zeroChanInfo`. |
| D4 Slot type 8 | Gate `airplaySink != 0` at `+0xA648`<br>Loads infra or `_zeroChanInfo` | Gate `airplaySink != 0` at `+0x7EE0`<br>Loads infra or `_zeroChanInfo` | Gate `airplaySink != 0` at `+0x7F28`<br>Loads infra or `_zeroChanInfo` | no | Identical AirPlay Sink gate; ivar shifted. |
| D5 Slot type 9 | Infra if allowed; else slot 0 `{1,6,0xA}` (`_channel6Info`); rest channel 6 if SDB in `{45,47,48}` | Infra if allowed; else slot 0 `{1,6,0xA}` (immediates); rest channel 6 if SDB in `{44,46,47}` | Infra if allowed; else slot 0 `{1,6,0xA}` (`_channel6Info`); rest channel 6 if SDB in `{44,46,47}` | no | In 14.8.9 immediate constants in code. SDB mask `{44,46,47}` on 14/15 vs `{45,47,48}` on 26 due to the state shift. |
| D6 `followPeerChannelSequence` | VA `0x21f6e74`<br>Copies from `peer+920` to `+0xA774`<br>Saves previous at `+0xA8F4`<br>State 9, 129 forced<br>Gate `+0x3F78 bit 0` | VA `0x20e3094`<br>Copies from `peer+920` to `+0x8004`<br>Saves previous at `+0x8184`<br>State 9, 129 forced<br>Gate `+0x3F60` | VA `0x2192a32`<br>Copies from `peer+920` to `+0x8054`<br>Saves previous at `+0x81D4`<br>State 9, 129 forced<br>Gate `+0x3F80` | no | Identical 192 B peer sequence adoption; buffers shifted in `awdlIvars`. |
| E1 `setChannelSequenceUnconditional` (129) | VA `0xffffff80021cb69a`<br>Payload site `0x21cb710`<br>400 B buffer (`0x190`)<br>Memcmp vs previous `+0xA8F4`<br>Token 129 `0x81802869C8`<br>Message 195 | VA `0xffffff80020bbee6`<br>Payload site `0x20bbf46`<br>400 B buffer (`0x190`)<br>Memcmp vs previous `+0x8184`<br>Token 129 `0x81802869C8`<br>Message 195 | VA `0xffffff8002169b06`<br>Payload site `0x2169b74`<br>400 B buffer (`0x190`)<br>Memcmp vs previous `+0x81D4`<br>Token 129 `0x81802869C8`<br>Message 195 | no | 400 B payload structure, `memcmp` suppression, token `0x81802869C8` and message 195 identical. |
| E2 `programMasterChannels` (132/205) | VA `0xffffff80021d494a`<br>Token 132 `0x84802869C8` (`+0x42A4`)<br>Token 205 `0xCD802869C8` (`+0x42B0`) | VA `0xffffff80020c44a8`<br>Token 132 `0x84802869C8` (`+0x426C`)<br>Token 205 `0xCD802869C8` (`+0x4278`) | VA `0xffffff8002172800`<br>Token 132 `0x84802869C8` (`+0x42AC`)<br>Token 205 `0xCD802869C8` (`+0x42B8`) | no | 132 and 205 immediate tokens identical; read from the corresponding master ivars. |
| E3 `setAwdlChannelSequence` (userspace) | VA `0xffffff800221cedc`<br>Range n 1..32, DFS filter<br>RouteInternal token + 129<br>Strategy 3 (reason 3, force true) | VA `0xffffff8002106db4`<br>Range n 1..32, DFS filter<br>RouteInternal token + 129<br>Strategy 3 (reason 3, force true) | VA `0xffffff80021b6ece`<br>Range n 1..32, DFS filter<br>RouteInternal token + 129<br>Strategy 3 (reason 3, force true) | yes | Validation of range up to 32 channels, internal ioctl 129 and forced fixing of strategy 3 identical. |
| E4 Callers of the 129 emission | `configure`, `enableAWDL`, `setChannelSequence`, `followPeerChannelSequence`, `setRoamChannelSequence`, scan overlays | `configure`, `enableAWDL`, `setChannelSequence`, `followPeerChannelSequence`, `setRoamChannelSequence`, scan overlays | `configure`, `enableAWDL`, `setChannelSequence`, `followPeerChannelSequence`, `setRoamChannelSequence`, scan overlays | yes | Emission points centralized in `setChannelSequenceUnconditional` identical. |
| E5 Token inventory | Tokens: `0xC02869C9`, `0x802869C8`, `0x81802869C8`, `0x84802869C8`, `0xCD802869C8`, `0x74802869C8`, `0x75802869C8`, `0x155C02869C9`; reqs 207/379 | Same 64-bit immediate tokens and request numbers | Same 64-bit immediate tokens and request numbers | yes | 100% identical in value, representation and semantics across the three macOS versions. |

---

## 6. §6: Real scenario and variants

| Scenario / Behavior | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Technical detail |
|---|---|---|---|---|---|
| Infrastructure on non-DFS channel (e.g. 153/80 MHz) | Generates 7 slots on 149 (flags `0x10`), up to 4 on 153 (`0x410`), 1 on 6 (`0x8`) and rest on 0 | Same 16-slot pattern | Same 16-slot pattern | yes | With AirPlay services active, 20 MHz (social 149) and 80 MHz (infra 153) primaries coexist. |
| Infrastructure on DFS channel (e.g. 52..144) | `isValidChannel` marks bit 0x1 (radarDFS). Allowed byte set to 0. Slots 7 and 9 discard infra. Log `"infra is on DFS"`. | Identical infra discard | Identical infra discard | yes | The stack disables infrastructure injection in the 16 slots and forbids mode 0 (`FollowsInfra`). |
| Fallback to channel 6 in slot 0 (no infra) | Loads `{1, 6, 0xA}` from `__ZL13_channel6Info` | Loads `{1, 6, 0xA}` from immediates `0x600000001` and `0xA` | Loads `{1, 6, 0xA}` from `__ZL13_channel6Info` | yes | Identical semantics of guaranteeing the 2.4 GHz social channel in slot 0 when no infrastructure is available. |
| Slot 4 (*Low Power*) without services | Emits type 9 in slot 0 and type 7 in slot 9 | Emits type 3 in slot 0 and type 4 in slot 9 | Emits type 9 in slot 0 and type 7 in slot 9 | no | In 14.8.9 it uses fixed social channels; in 15.8.2 and 26.7.1 it switches dynamically per infra state. |
| `memcmp(0x190)` suppression of the 129 emission | Compares 400 bytes with the previous buffer. If no changes, skips the ioctl emission and log. | Compares 400 bytes with the previous buffer. | Compares 400 bytes with the previous buffer. | yes | Avoids overloading the USB/PCIe bus when the computed sequence has not changed. |
| Remote peer presence (`followPeer`) | Overwrites the buffer with the peer's sequence (192 B) and forces state 9 and emission 129. | Identical forced adoption | Identical forced adoption | yes | The local sequence becomes entirely subordinate to the direct-link master device's. |

---

## 7. Table of slot types `__ZL20_channelSeqSlotTypes` and state switch

### 7.1 Dimensions and symbols

- **macOS 26.7.1 (25G241)**:
  - Table VA: `0xffffff80023aa1f0`.
  - Size: 56 rows of 16 dwords (64 bytes per row = 3584 bytes = `0xE00`).
  - Upper delimiter: `__ZL13_channel6Info` at `0xffffff80023aaff0`.
  - State names (`__ZL18scheduleStateNames`): VA `0xffffff8002459c20`.
- **macOS 15.8.2 (24H232)**:
  - Table VA: `0xffffff8002317240`.
  - Size: 54 rows of 16 dwords (64 bytes per row = 3456 bytes = `0xD80`).
  - Upper delimiter: `__ZL13_channel6Info` at `0xffffff8002317fc0`.
  - State names (`__ZL18scheduleStateNames`): VA `0xffffff80023c95e0`.
- **macOS 14.8.9 (23J631)**:
  - Table VA: `0xffffff800223d1d0`.
  - Size: 54 rows of 16 dwords (64 bytes per row = 3456 bytes = `0xD80`).
  - Upper delimiter: `__ZL19_psfTansmissionMode` at `0xffffff800223df50` (`__ZL13_channel6Info` absent).
  - State names (`__ZL18scheduleStateNames`): VA `0xffffff80022c9a08`.

### 7.2 Jump table and absence of bias in `setChannelSequenceByState`

In the three analyzed versions, the selection of the action per slot is implemented through an indirect `jmp rax` instruction based on a *jump table* indexed directly by the slot type:
- **25G241**: Switch at `0xffffff80021f5498`, table `jpt_FFFFFF80021F5498` at `0xffffff80021f5e90`.
- **24H232**: Switch at `0xffffff80021910b2`, table `jpt_FFFFFF80021910B2` at `0xffffff8002191c40`.
- **23J631**: Switch at `0xffffff80020e1730`, table `jpt_FFFFFF80020E1730` at `0xffffff80020e2248`.

In all three cases:
- Number of cases: exactly 21 cases (range 0..20).
- Bias: 0 (`lowcase = 0`). The decompiler's labels and the jump table entries correspond directly and identically to the slot type numbers (slot types 0 to 20).

### 7.3 Comparative dump of key states

Below, the 16 slots of the representative states requested are compared:

| State | State name | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | Match? |
|---|---|---|---|---|---|
| **0** | Discovery | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` | **yes** (identical) |
| **1** | Discovery + Infra Stream | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | **yes** (identical) |
| **3** | Idle | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | **yes** (identical) |
| **4** | Low Power | `9 1 3 1 1 1 7 7 2 7 3 1 1 1 7 7` | `3 1 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `9 1 3 1 1 1 7 7 2 7 3 1 1 1 7 7` | **no** (slots 0 and 9) |
| **24** | Scan + Data | `3 3 3 3 3 1 1 1 2 4 3 4 4 1 1 1` | `3 3 3 3 3 1 1 1 2 4 3 4 4 1 1 1` | `3 3 3 3 3 1 1 1 2 4 3 4 4 1 1 1` | **yes** (identical) |
| **31** | Infra BE RT + No Cached Peers + LP | `9 1 3 1 1 1 1 1 2 1 3 1 1 1 1 1` | `9 1 3 1 1 1 1 1 2 1 3 1 1 1 1 1` | `9 1 3 1 1 1 1 1 2 1 3 1 1 1 1 1` | **yes** (identical) |
| **40** | High Thruput (14/15) / RtMode Start (26) | `0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0` | `4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4` | `4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4` | Shift (+1 in 26) |
| **41** | High Thruput (26) | `4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4` | `12 12 12 12 5 5 5 5 12 ... 12` | `12 12 12 12 5 5 5 5 12 ... 12` | Shift (+1 in 26) |
| **44 / 45** | AWDL Fast Discovery | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` (45) | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` (44) | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` (44) | **yes** (identical under mapping) |

### 7.4 Analysis of the structural shift by states 38 and 55

The automated byte-by-byte comparison of the 54-row tables (14.8.9 and 15.8.2) against the 56-row one (26.7.1) reveals:
1. **Rows 0 to 37**: They are 100% byte-identical between 15.8.2 and 26.7.1. Between 14.8.9 and 15.8.2 the only discrepancy in this block is row 4.
2. **Insertion of state 38 (*Time Sync Retro*) in macOS 26**:
   - In macOS 26.7.1 state 38 corresponds to *Time Sync Retro* with the sequence `3 1 1 1 3 1 1 1 3 1 1 1 3 1 1 1`.
   - This state does not exist in macOS 14.8.9 nor in macOS 15.8.2.
   - As a consequence, all subsequent states (38 to 53 in 14/15) are shifted exactly by +1 position in macOS 26 (they become states 39 to 54).
3. **Equivalence with shift**:
   - Rows 38..53 of 15.8.2 match 100% (0 bytes of discrepancy) rows 39..54 of 26.7.1.
   - Rows 38..53 of 14.8.9 match 100% rows 38..53 of 15.8.2.
4. **Insertion of state 55 (*RTG AirDrop*) in macOS 26**:
   - In macOS 26.7.1 state 55 (*RTG AirDrop*) was appended at the end, initialized with 16 zeros (`0 ×16`).

### 7.5 Discrepancy in row 4 (*Low Power*)

Row 4 is the only functionally distinct row between macOS 14.8.9 and the later versions:
- **macOS 14.8.9 (23J631)**:
  `State 4:  3  1  3  1  1  1  7  7  2  4  3  1  1  1  7  7`
  - Slot 0 is type 3 (secondary master channel 44/149 or primary).
  - Slot 9 is type 4 (current AWDL channel `+0x1A0`).
- **macOS 15.8.2 (24H232) and macOS 26.7.1 (25G241)**:
  `State 4:  9  1  3  1  1  1  7  7  2  7  3  1  1  1  7  7`
  - Slot 0 was updated to type 9 (infrastructure injection if allowed, with fallback to social channel 6).
  - Slot 9 was updated to type 7 (infrastructure injection conditioned to AirPlay services).

This change in macOS 15 gives the *Low Power* state the ability to hold a dynamic link with the associated infrastructure channel without forcing static permanence on the secondary or current social channel.

### 7.6 Slot-type resolution in `setChannelSequenceByState`

| Type | Resolution semantics | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | Match? |
|---|---|---|---|---|---|
| **0** | No-op / disabled slot | Does not write the slot | Does not write the slot | Does not write the slot | **yes** |
| **1** | Turned-off slot | Writes `_zeroChanInfo` `{1, 0, 0}` | Writes `_zeroChanInfo` `{1, 0, 0}` | Writes `_zeroChanInfo` `{1, 0, 0}` | **yes** |
| **2** | Primary master | Reads `+0x42A0` (channel) and `+0x42A8` (flags) | Reads `+0x4268` (channel) and `+0x4270` (flags) | Reads `+0x42A8` (channel) and `+0x42B0` (flags) | **yes** (shifted ivars) |
| **3** | Secondary master (fallback primary) | Reads secondary `+0x42AC`; if channel == 0, jumps to primary | Reads secondary `+0x4274`; if channel == 0, jumps to primary | Reads secondary `+0x42B4`; if channel == 0, jumps to primary | **yes** (shifted ivars) |
| **4** | Current AWDL channel | Reads current channel at `+0x1A0` and flags at `+0x1A8` | Reads current channel at `+0x1A0` and flags at `+0x1A8` | Reads current channel at `+0x1A0` and flags at `+0x1A8` | **yes** (same offset) |
| **7** | Infrastructure for AirPlay services | Calls `addInfraToMinimumChannelSequence()`. If true, copies infra `pm`; if false, writes `_zeroChanInfo`. | Same dispatch and evaluation of 3 conditions | Same dispatch and evaluation of 3 conditions | **yes** |
| **8** | AirPlay Sink | If `airplaySink != 0` (`+0xA648`), uses infra; else `_zeroChanInfo` | If `airplaySink != 0` (`+0x7EE0`), uses infra; else `_zeroChanInfo` | If `airplaySink != 0` (`+0x7F28`), uses infra; else `_zeroChanInfo` | **yes** (shifted ivars) |
| **9** | General infrastructure / fallback 6 | If `infraAllowed`, uses infra. If not and slot 0, `{1, 6, 0xA}`. If not and rest, channel 6 if SDB active in specific states. | Same rule; slot 0 uses immediates `0x600000001` and `0xA` instead of the static symbol. | Same rule; uses `__ZL13_channel6Info`. | **yes** (identical semantics) |

In all three versions, after programming each slot a 6 GHz guard is applied: if the card has no 6 GHz P2P capabilities (`test byte ptr [...], 20h`), the `0x2000` flag is removed from the slot.

---

## 8. Implications for the controller layer (`RTLAirLayer` / `RTL8814Native`)

For the same extension or AWDL emulation layer to operate without binary modifications on macOS 14.8.9, macOS 15.8.2 and macOS 26.7.1, the following adaptations must be implemented in the driver:

### 8.1 Publication of the card capabilities block (`getCARD_CAPABILITIES`)

Apple's stack decides whether to enable the 5 GHz social channels (149 and 44) by evaluating bit `0x20` of the card capabilities block inside `configureValidAWDLChannels`:
- **macOS 14 (Darwin 23)**: The block sits directly on the base controller object at `pm + 0xB18`. The check runs over `[pm + 0xB1F] & 0x20`. In this version the function `IO80211Controller::isCardCapabilitiesSet` does not exist.
- **macOS 15 (Darwin 24)**: The block is obtained by dereferencing the pointer at `ctrl + 0x120` and checking offset `0xB27`: `*(ctrl + 0x120) + 0xB27 & 0x20`.
- **macOS 26 (Darwin 25)**: The block sits at offset `0xB37`: `*(ctrl + 0x120) + 0xB37 & 0x20`.

**Rule for the driver**:
When answering the WCL capabilities request or injecting the card block into the controller, the compatibility layer must ensure bit `0x20` is active in the corresponding byte according to the major Darwin version running (`version_major == 23 ? 0xB1F : (version_major == 24 ? 0xB27 : 0xB37)`). If this bit is at 0, the stack prints `"Dual band not supported, so assume cannot use 5G"` and immediately nulls channels 149 and 44, confining AWDL exclusively to 2.4 GHz (channel 6).

### 8.2 Honesty in the GET of the infrastructure channel (`getInfraChannel`)

Since the AWDL manager has no active setter to receive the infrastructure channel from userspace or from link events (all the `setInfraChannel*` methods are proven dead code), the only channel the manager knows is the one the driver answers to the WCL request `0xC02869C9` (type 4, 16 bytes):
- If the driver reports channel 0 while `en2` is associated, the manager assumes infra link loss and leaves type 7 and type 9 slots without infrastructure `?`.
- If the driver reports a DFS channel (52..144), the manager activates the `radarDFS` flag and disables infrastructure injection (§6).
- To operate with infrastructure on 153/80 MHz, the driver must faithfully return `{version: 1, channel: 153, flags: 0x410}`.

### 8.3 Reception and dispatch of ioctl 129 (`AWDL_SYNC_CHANNEL_SEQUENCE`)

The format of the payload emitted by `setChannelSequenceUnconditional` is strictly identical in the three versions:
- Structure size: 400 bytes (`0x190`).
- Header:
  - `+0`: Version (u32 = 1).
  - `+4`: SDB flags (u8, `0x10` or `0x20`).
  - `+5`: Slot count (u8 = 16).
  - `+6`: Encoding (u8, 1 if dual band/SDB, 0 if single channel).
  - `+7`: Step (u8).
  - `+8`: Duplicate count (u8).
  - `+9`: Fill (u8).
- Slots (`+12..+203`): 16 consecutive 12-byte entries `{u32 version; u32 channel; u32 flags}`.

The driver can process ioctl 129 with the same routine on macOS 14, 15 and 26, without needing to distinguish different payload structures.

### 8.4 Primary and secondary master channels (ioctls 132 and 205)

The notification of the master channels toward the firmware (`programMasterChannels`) uses the same tokens in the three versions:
- Request 132 (`0x84802869C8`): Primary master channel (usually channel 6 or the infrastructure channel in mode 0).
- Request 205 (`0xCD802869C8`): Secondary master channel (usually channel 149 or 44).

The RTL layer receives them in slots 132 and 205 of the AWDL interface's vtable (shifted on 14 and 15, see `AWDL_CODIGO_VS_BUILDS.md` §2.2).

---

## 9. Open points

1. **Firmware impact of the row 4 (Low Power) difference**: In macOS 14.8.9 the *Low Power* state does not inject dynamic infrastructure in slots 0 and 9. Field tests must verify whether devices idling under macOS 14 experience higher AirDrop discovery latency versus macOS 15/26.
2. **Compatibility of mode 0 with bridged virtual interfaces**: If `en2` is part of a bridge (`bridge0`), the `getInfrastructureState` code contains a special fallback branch invoking `apple80211getCHANNEL` directly on the member device. It must be verified whether this branch affects frame filtering in the RTL driver.
3. **Persistence of the `dynSdbForceChanSeq` value**: On cards without full SDB capabilities, it must be determined whether forcing `dynSdbForceChanSeq = 0` stabilizes the alternation between channel 6 and channel 149 avoiding sync losses in the TSF timer.

---

## 10. Verification summary
- Tables `__ZL20_channelSeqSlotTypes`: 56 rows in 25G241 and 54 in 23J631 and 24H232; rows 0, 1, 3, 4, 24, 31,
  40/41 and 44/45 checked in the three collections, and the 21-case switch without bias in all three.
- Code VAs of the tables of §3 through §5: instruction heads in their collection.
