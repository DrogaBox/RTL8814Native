# Capabilities advertised in the output TLVs of the `IO80211AWDLPeerManager` (7, 17, 21 and 12)

Where the `IO80211AWDLPeerManager` takes each value it emits in TLVs 7 (HT capabilities),
17 (IEEE 802.11 container, VHT), 21 (AWDL version and device class) and 12 (data path state), what
`RTL8814AirLayer` answers today to each source, and which fields could advertise 40/80 MHz,
short guard interval, LDPC or more spatial chains than the radio actually receives on `awdl0`.
Base: macOS 26.7.1 kernelcache (25G241, Darwin 25.6.0, x86_64). Anything unverified carries `?`.

## 1. The three templates and the four builders

The TLVs come out of three functions that compose full templates over buffers of the Peer
Manager (PM) itself:

| Template | VA | Use |
|---|---|---|
| `updateSyncFrameTemplate` | `0xffffff80021dc5b2` | 512 B template of the sync frame (PSF); it pushes it down with the internal ioctl `0x97802869C8` (type 151, `AWDL_SYNC_FRAME_TEMPLATE`) |
| `buildMasterIndicationTemplate` | `0xffffff8002211b42` | template of the master indication (UMI) |
| `buildSecondaryMasterIndicationTemplate` | `0xffffff8002210436` | template of the secondary master indication |

All three invoke the same builders with the same caches: TLV 12 with
`buildAwdlDataPathStateTlv` (`0xffffff8002210e5a`), TLV 7 with `buildEnhDataRateTlv`
(`0xffffff800221126c`), TLV 17 with `buildIEEE80211CntnrTlv` (`0xffffff80022113c4`) and TLV 21 with
`buildAWDLVersionTlv` (`0xffffff8002211846`). Call sites of each builder:

| Builder | `updateSyncFrameTemplate` | secondary | MI |
|---|---|---|---|
| TLV 12 | `0xffffff80021dc6a1` | `0xffffff8002210518`, `0xffffff8002210674` | `0xffffff8002211c77`, `0xffffff8002211d61` |
| TLV 7 | `0xffffff80021dc6e5` | `0xffffff8002210577` | `0xffffff8002211e1c` |
| TLV 17 | `0xffffff80021dc863` | `0xffffff8002210789` | `0xffffff8002211f8b` |
| TLV 21 | `0xffffff80021dc89d` | `0xffffff80022108f6` | `0xffffff80022120fb` |

The hardware confirms the cadence: after `enableAWDL` the PM rebuilds the template and pushes down
ioctl 151 once per second ([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)
§table; `enableAWDL` calls `updateSyncFrameTemplate` at `0xffffff80021dd375`).

### PM data objects

| Symbol | Object | Relevant content |
|---|---|---|
| `sub` = `*(pm+0x18)` | caps/data of the PM surface | HT cache `+0x945` (28 B), HT length `+0x61C`; VHT flag `+0x620` and VHT cache `+0x961` (14 B); HE flag `+0x621` and HE cache `+0x970` (40 B); country cache `+0x9A4` (3 B); infra BSSID cache `+0x930` (6 B); infra channel `+0x9C0` and flags `+0x9C4` |
| `state` = `*(pm+0x20)` | PM state | device class `+0x4008`; TLV 12 counters and marks (`+0xA608`, `+0x3EF8`, `+0x92C0`, `+0x3F04`…) |
| `ctrl_data` = `*(*(sub+0x520)+0x120)` | controller object | capability bytes `+0xB37`, `+0xB3F`, `+0xB45` (readers verified; writers not identified `?`) |

## 2. TLV 7 — HT capabilities: `buildEnhDataRateTlv`

The builder copies a cache; it does not query the driver:

- `0xffffff800221127e`: reads the length `_htCapabilityLength` at `sub+0x61C`; if `len+2` exceeds the
  gap, it skips the TLV (log `54BBh`, `0xffffff80022112af`).
- `0xffffff80022112be`: writes the header word to 0; `0xffffff80022112d1`: source
  `sub+0x947`; `0xffffff80022112dd`: `memmove` of `len` bytes; returns `len+2`.

Consequence of the length computation (see §2.2): the TLV body is the bytes
`sub+0x947..sub+0x946+len`, i.e. the HT cache without its first two bytes.

### 2.1 Who fills the HT cache

| Writer | VA | Request |
|---|---|---|
| `IO80211PeerManager::getHtCapabilityIE` | `0xffffff8002252a56` | internal GET `0xC02869C9` type 154 (`HT_CAPABILITY`), 40 B response |
| `IO80211PeerManager::setHtCapabilityIE` | `0xffffff800225283a` | internal SET `0x802869C8` type 154; called only by the family handler `setHT_CAPABILITY` (`0xffffff800228912f` from `0xffffff80022890de`) |

Both save the 28 payload bytes of the response (struct `apple80211_ht_capability`: 4 B of
version + 28 B) at `sub+0x945` (stores at `0xffffff8002252b46` and `0xffffff800225291c`) and compute
the length at `sub+0x61C` as the index of the last non-null byte +1 within the 28. Only
`getHtCapabilityIE` also clears bit 0x10 (HT-Greenfield) of the word at `sub+0x947`; the
SET path does not.

### 2.2 Emission gates

The three callers require `1 <= _htCapabilityLength <= 26`: with a length out of range
`updateSyncFrameTemplate` logs `"Invalid _htCapabilityLength %u, skipping buildEnhDataRateTlv"`
and skips the TLV (builder call at `0xffffff80021dc6e5`; same check in the other two
templates). With an empty cache (length 0) TLV 7 is not emitted.

### 2.3 Where the type 154 response comes from

`IO80211PeerManager::do_ioctl` (`0xffffff800225254a`) routes through `apple80211RouteInternal` toward
**the primary interface of the controller** (slot `0xC80` = `getPrimarySkywalkInterface`), with
alternatives `sub+1376` and `sub+1368`. For the PM of `awdl0` the primary is `en2` (verified in
[AWDL_STAGE1_OPEN_POINTS.md](AWDL_STAGE1_OPEN_POINTS.md) §3): **the PM's GET/SET 154 lands on
`en2`, not on `awdl0`**.

On the user route the handler `getHT_CAPABILITY` (`0xffffff800227c503`, type 154 table at
`0xffffff800245f028`) requires `len == 32`, first tries the WCL glue
(`IO80211Glue::sendIOUCToWcl` with `0xC02869C9`/154) and without glue falls to the bridge
`apple80211getHT_CAPABILITY` (`0xffffff8002263d2f`): cast to `IO80211InfraProtocol` and call of the
same-named slot; without cast it answers `0xE082280E`. The VHT and HE bridges have the same shape
(`getVHT_CAPABILITY` `0xffffff800227cee8` with `len == 20`, `getHE_CAPABILITY`
`0xffffff80022818e9` with `len == 40`; set VHT `0xffffff8002289afa`, set HT `0xffffff80022890de`).

## 3. TLV 17 — IEEE 802.11 container (VHT): `buildIEEE80211CntnrTlv`

The builder is a pure `memmove` of the buffer the caller passes (`0xffffff80022113c4`; header
written by the caller, body at `+0x961` below). The three templates pass it the same thing:
**the 14 bytes of the VHT cache `sub+0x961`**, and only if flag `sub+0x620` bit 0 is set
(`updateSyncFrameTemplate`, check in the decompiled code; call sites in §1). With the flag at 0,
TLV 17 is not emitted.

### 3.1 Who fills the VHT cache

| Writer | VA | Request |
|---|---|---|
| `IO80211PeerManager::getVHtCapabilityIE` | `0xffffff8002252c88` | internal GET `0xC02869C9` type 214 (`VHT_CAPABILITY`), 20 B response; saves `a2+4..+17` (qword→`+0x961`, dword→`+0x969`, word→`+0x96D`) and sets `sub+0x620 = 1` |
| `IO80211PeerManager::setVHtCapabilityIE` | `0xffffff8002252e66` | internal SET `0x802869C8` type 214 (only reached from the handler `setVHT_CAPABILITY` `0xffffff8002289b4b`); same store and same flag |

The type 214 response that gets cached is 14 bytes and is the whole element: id and length (`bf 0c`),
VHT Capability Info (4 B), MCS RX map (2 B),
RX Highest (2 B), MCS TX map (2 B), TX Highest (2 B) and 2 trailing bytes of the struct `?` (the
exact partition of the last 2 bytes was not verified; the order of the stores was).

### 3.2 HE: cache present, no output

`getHeCapabilityIE` (`0xffffff8002253062`, internal GET type 570, 40 B response without a version
prefix) saves the block at `sub+0x970` and marks `sub+0x621`, but **no TLV builder uses the
HE cache in 25.6.0**: `getMyHeCap` (`0xffffff800225f63c`) has no callers, and the wrappers
`IO80211AWDLPeerManager::get{Ht,VHt,He}CapabilityIE` (`0xffffff80021cdcd8`, `0xffffff80021cddd6`,
`0xffffff80021cddf8`, virtual slots `+0x210/+0x218/+0x220` of the PM vtable) and the helpers
`getAwdl*CapabilityIE` (`0xffffff800221fe4a`, `0xffffff800221fe80`, `0xffffff800221feac`) have
no direct or indirect callers found in the `IO80211*` functions. SET 570 has no handler
(`type 570` in the set table = 0). The TLV 17 of this build is VHT only.

## 4. TLV 21 — AWDL version: `buildAWDLVersionTlv`

It writes 5 bytes: type 21 (`0xffffff800221187e`), length 2 (`0xffffff8002211881`), **constant
version `0xA0`** (`0xffffff8002211887`) and device class byte read from
`state+0x4008` (`0xffffff800221188f`, store at `0xffffff8002211895`).

The device class does not come from the driver:

- `IO80211AWDLPeerManager::initWithInterface` (`0xffffff80021c1540`) writes `dword 1` at
  `state+0x4008` (`0xffffff80021c292b`): class 1 = macOS.
- The boot-arg `override_awdl_platform_type` (`PE_parse_boot_argn` at `0xffffff80021c294c`)
  replaces that value if present and non-null (`0xffffff80021c296e`); a name table at
  `0xffffff800245B500` (first entry `"MacOS"`) validates classes 1..9.

Nothing from the layer takes part in TLV 21.

## 5. TLV 12 — data path state: `buildAwdlDataPathStateTlv` + `fillLegacyDataPathPayload`

`buildAwdlDataPathStateTlv` (`0xffffff8002210e5a`; requirement `gap > 0x31`, returned 50) builds a
50-byte TLV (type 12 at `+0`, length 47 at `+1..+2`) and delegates the body to
`fillLegacyDataPathPayload` (`0xffffff80022143ca`, signature `(tlv&, bool legacy, u32 channel, bool,
bool)`). Breakdown of the body (offsets from byte 3 of the TLV):

| Body | Content | Source | Cache writer |
|---|---|---|---|
| +0/+1 | u16 flags, base `0x8B03` (`0xffffff80022143de`) or `0x8B23` if bit 5 of `ctrl_data+0xB37` (`0xffffff80022143fb`, `0xffffff8002214404`); `\|0x40` if `state+0xA648 != 0`; `\|0xC0` if bit 0 of `state+0xF624`; `\|0x400` if bit 0 of `state+0x3FA1` and `state+0xA6A4 != 0`; `\|0x2000` if the device class is 2 and bytes `state+0xFB85..0xFB87` allow it; `\|0x4000` depending on bit 1 of a byte after `ctrl_data+880` `?` | PM ivars | — |
| +2..+4 | country code (3 B) | `sub+0x9A4` | `interfaceStateChange` (`0xffffff80021da2bd`, `0xffffff80021da2c3`) copies the 3 B from `state+0x3E9C`; seed: boot-arg `forcecountry` in `initWithInterface` (writes at `state+0x3E9C` verified in the decompiled code) |
| +5/+6 | u16 | `state+0x429C` | `?` |
| +7..+12 | infrastructure BSSID (6 B); only if the caller passes the legacy flag or bit 0 of `state+0x4050` | `sub+0x930` | `getInfrastructureState` (`0xffffff80021da9b1`) from `IO80211Controller::getBSSIDData` (`0xffffff8002294e5a`; WCL glue or `getBSSID` bridge) |
| +13/+14 | infrastructure channel (u16; 0 if bit 13 of `sub+0x9C4`) | `sub+0x9C0` | `reportDataPathEvents`, case 82 of the jump table (`0xffffff80021de15a` and `0xffffff80021de164`, from the event payload +4/+8; driver events 41/42) |
| +15..+20 | infrastructure address (6 B) | `state+0x9160`; if `state+0x9166` is not the null address, `state+0x9166..+0x916B` | `?` |
| +21/+22 | constant 4 | builder | — |
| +23..+26 | u32 channel of the caller | argument; 0 in the sync frame template, channel of the UMI in the MI | — |
| +27/+28 | u16 extended flags: `0x20` fixed; bit 11 = bit 7 of `ctrl_data+0xB3F`; bit 13 = bit 0 of `state+0xFF98`; from `fillLegacy`: bits 0/1 (counter `state+0xFA3A`), `0x04/0x10/0x40` depending on internal state, `0x80` (reporters), **`0x100` = bit 7 of `ctrl_data+0xB45`** (the same byte that `getHeCapable` `0xffffff800225f3f0` returns as "HE capable"), `0x200` (`state+0xF628`), `0x400` (bit 0 of `state+0xFE99`), `0x1000` | PM ivars | — |
| +29/+30 | u16 | `state+0xD064` or `state+0xFA58` | `?` |
| +31..+34 | u32 | `state+0x917B` | `?` |
| +35..+38 | milliseconds since the mark `state+0xA608` | `mach_absolute_time` in the builder | — |
| +39..+42 | u32 | `state+0x3EF8` | `?` |
| +43..+46 | u32 counter | `state+0x92C0`, incremented on each build when the caller asks for it (MI asks, the sync template does not) | — |

TLV 12 carries no PHY capabilities: no channel width, GI, LDPC or chains.

## 6. What `RTL8814AirLayer` answers today to the sources above

| Source | Path toward the layer | Current answer |
|---|---|---|
| GET 154 (`HT_CAPABILITY`) | PM `do_ioctl` → primary interface (`en2`) → slot 479 of `RTLAirInterface` | inherited stub `kIOReturnUnsupported` (`Source/IO80211/IO80211_25_6_0_infra_stubs.inc:13`; no override in `Source/AirLayer/RTLAirLayer.cpp`) |
| SET 154 | same, slot 557 | stub `kIOReturnUnsupported` (`infra_stubs.inc:91`) |
| GET/SET 214 (`VHT_CAPABILITY`) | same, slots 482/561 | stubs `kIOReturnUnsupported` (`infra_stubs.inc:16`, `:95`) |
| GET 570 (`HE_CAPABILITY`) | same, slot 541 | stub `kIOReturnUnsupported` (`infra_stubs.inc:75`); SET 570 does not exist in the table |
| GET/SET 154/214/570 aimed at `awdl0` | bridge with cast to `IO80211InfraProtocol` | `RTLAwdInterface` is not `IO80211InfraProtocol` (`Source/AirLayer/AWDLInterface.h:297`): the bridge answers `0xE082280E` without reaching the layer |
| internal ioctl 151 (sync frame template) | bridge → slot `+0x1118` of `awdl0` | `RTLAwdInterface::setAWDL_SYNC_FRAME_TEMPLATE` answers `kIOReturnSuccess` without saving the template (`Source/AirLayer/AWDLInterface.cpp:341-358`, macro `AWDL_ACK`) |
| events 41/42 (infra channel/flags of TLV 12) | `reportDataPathEvents` | the layer does not emit them (deferred windows, [AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)) |
| `getBSSID` (infra BSSID of TLV 12) | `getBSSIDData` → glue/bridge | no override in the layer; inherited behavior unverified at runtime `?` |

Result up to 2026-11-05: `_htCapabilityLength` stayed at 0 and `sub+0x620` at 0, so **the PM emitted neither
TLV 7 nor TLV 17**. Measured that day on 25G241: in one boot, 879 lines `Invalid _htCapabilityLength 0, skipping
buildEnhDataRateTlv` and 3 `getVHtCapabilityIE Unable to get vht capabilities ret e00002c7`, and none of the 529
frames captured at the radio carried those TLVs. Peers saw a node advertising neither HT nor VHT. TLV 21 always
goes out (`0xA0`, class 1). Since that day the layer answers GET 154 and 214 with the values of §10.

## 7. Fields through which a driver could advertise more than the radio receives

With the layer answering GET 154/214, the response content goes on the air almost
verbatim — the kernel only clears the HT-Greenfield bit on the GET path. The lying fields would
be:

| Field | TLV | Bits at stake |
|---|---|---|
| HT Capability Info (2 B, body of TLV 7) | 7 | bit 0 LDPC; bits 1-2 width (40 MHz); bit 5 SGI 20; bit 6 SGI 40; bits 7-8 STBC |
| A-MPDU Parameters (1 B) | 7 | agreed length/exp/interval |
| Supported MCS Set (16 B) | 7 | RX MCS mask: defines how many chains and rates the peer can use against this node |
| VHT Capability Info (4 B, TLV 17) | 17 | bits 2-3 width (80/160/80+80); bit 5 SGI 80 |
| MCS RX/TX maps (8 B) | 17 | 2 bits per chain (3 = not supported) |

The failure documented in [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §7 applies
directly: with 40 MHz advertised and the radio tuned to 20 MHz, 837 of 838 frames arrived
only as a retry at MCS 0. On `awdl0` the RTL8814AU operates at 20 MHz on the social channel, so
any 40/80 width bit, any 40/80 SGI and any chain above what
the radio actually receives at that width causes peer frames the radio cannot decode. The
LDPC receive capability of the RTL8814AU is not verified in this repository `?`: while it
stays unverified, bit 0 of HT Capability Info must stay at 0.

## 8. How to keep the values honest

1. **Omission is not a valid state.** An earlier version of this note called it interoperable. The virtual
   peer tests later showed the opposite on macOS: without HT or VHT, `IO80211AWDLPeer::hasHTorVHTCaps` is false,
   `addPeerToFWCache` does not cache the peer and `preQueuePacket` drops everything sent to it
   ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9.4). Whether iOS does the same is `?`: `parseHTCapTLV` copies
   26 B and does not mark HT with a shorter TLV, and owly, which sends an 8 B TLV 7 and no TLV 17, works with an
   iPhone.
2. **If GET 154 is implemented** (40 B response, 28 B of payload): width at 20 MHz (bits 1-2 =
   0), SGI only if the driver really applies it on TX/RX at 20 MHz, LDPC at 0, MCS mask limited
   to the chains the radio receives on `awdl0` (the same figure `getNSS` already answers from the
   radio state, `Source/AirLayer/RTLAirLayer.cpp:988`), A-MPDU consistent with what the driver
   negotiates. The width bits must match `getSUPPORTED_CHANNELS`, which today
   publishes only 20 MHz entries (`Source/AirLayer/RTLAirLayer.cpp:1011-1044`).
3. **If GET 214 is implemented** (20 B response, 14 B cached): width bits at 20 MHz (bits
   2-3 = 0), SGI 80 at 0, MCS maps with 3 (not supported) on the chains above the real one. Note:
   even with VHT advertised at 20 MHz, a peer that decides to send VHT uses MCS of one chain at most;
   the honest mapping keeps it from trying more.
4. **Care with the SET**: `setHtCapabilityIE`/`setVHtCapabilityIE` save the payload without
   validating width or clearing greenfield. If SET 154/214 are ever accepted from the daemon, the
   payload must be validated against the real width before accepting it, or they must be rejected (today they already fail
   with the stub `kIOReturnUnsupported`, which is the safe answer).
5. **TLV 21 and TLV 12** need no protection: the 21 reads nothing from the driver (§4) and the 12
   carries no PHY caps (§5). The only hybrid flag is bit `0x100` of the extended flags of
   TLV 12, mirror of the "HE capable" byte `ctrl_data+0xB45`: since its writers are not
   identified `?`, it is worth checking on the air that it goes out at 0 before relying on it.
6. **Coherence with template delivery**: the rebuilt template is pushed down through ioctl 151
   and the layer accepts it without saving it, so none of this reaches the radio yet
   ([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)); the TLV values only
   describe what the PM *would advertise* if the PSF/MIF frames went out.

## 9. Open items

- Writers of `ctrl_data+0xB37/0xB3F/0xB45` not found by offset (possible bulk write by
  `memcpy` in the init of the object `*(ctrl+0x120)`): identity of those three bytes `?`.
- Writers of `state+0x429C`, `state+0x9160/0x9166`, `state+0xD064`, `state+0x917B`,
  `state+0x3EF8` (fields of TLV 12) `?`.
- Real behavior of `getBSSIDData` against the layer (no override of `getBSSID`) `?`.
- `IO80211AWDLPeerManager::doEnable`, `configure` and `reportDataPathEvents` call slot `+0x210` (HT getter). The
  3 `getVHtCapabilityIE` lines of §6 prove the VHT getter also runs, but its call site was not located `?`.
- Whether an iPhone sends at HT or VHT rates this radio receives well at 20 MHz, and whether it asks for an ADDBA
  the layer does not answer ([AWDL_BLOCK_ACK.md](AWDL_BLOCK_ACK.md)), is `?` until tested with one.

## 10. What the layer answers

They describe `awdl0`, not the link to the network: 20 MHz, one stream, no LDPC or beamforming, A-MPDU up to 64 KB.
`getHtCapabilityIE` clears the greenfield bit and works out the length of TLV 7 (§2.2).

| | Response bytes after the version | On the air |
|---|---|---|
| HT | `2d 1a 2c 00 1f ff`, then zeros up to 28 B | TLV 7: `00 00 2c 00 1f ff 00 00` |
| VHT | `bf 0c 00 00 80 01 fd ff 00 00 fd ff 00 00` | TLV 17: the same 14 B |
| iPhone, for comparison | | TLV 7: `00 00 6f 00 1f ff ff 00 00`; TLV 17: `bf 0c 32 00 80 03 fa ff 00 00 fa ff 00 00` |
| owly, for comparison | | TLV 7: `00 00 ce 11 1b ff 00 00`; no TLV 17 |

`updateHTandVHTCapsForRSDB` rewrites both VHT maps only if the PM's RSDB byte is set (`0xffffff80021cdcfa`). The "On
the air" column was checked on 2026-11-06 with a capture from another device ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §6). Whoever asks through `en2` gets the same
answers; whether anything on the infrastructure side uses them is `?`.
