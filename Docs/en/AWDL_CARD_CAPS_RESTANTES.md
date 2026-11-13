# The card capabilities block (`*(ctrl+0x120)+0xB34..+0xB4B`) on 25G241: readers and comparison with the RTL layer

## 1. Summary

The GET 12 (`CARD_CAPABILITIES`) of the driver is copied whole into the card object of the
controller and all kernel readers access that copy by direct offset.
This document lists every byte and bit of the block with a reader in the kernel collection of
macOS 26.7.1 (build 25G241, `BootKernelExtensions.kc` md5 `f489ff8be80e6013a1bf47561dcaef45`),
which gate each one, and compares it with what `RTLAirController::getCARD_CAPABILITIES`
(`Source/AirLayer/RTLAirLayer.cpp`, read only) sets today.

Conclusion: of all the bits the kernel reads, the only absent one with functional effect
on AWDL with an infrastructure association is **bit `0x20` of `p[7]` (`+0xB37`,
capability 29, dual band)**, already documented in `AWDL_SECUENCIA_CON_INFRA.md` §4.2/§9.
Bytes `+0xB3F` (p[15], 6E) and `+0xB47` (p[23], Limited SDB/ranging) are closed here:
with the current layer at 0, all 6 GHz, pro-mode, DPS and ranging paths stay
disabled, which is the correct behavior for a single-band radio without 6E; the
only consequence of `+0xB47` is cosmetic (Broadcom sets its bit 4 unconditionally and
the ranging TLV 30 bit comes out at 0).

## 2. The object and how it is filled

- `IO80211Controller::loadCardCapabilities` (`0xffffff800229a202`): writes the response
  to `*(*(this+0x120)) + 0xB30` — in the decompilation, `*(this+288) + 2864`. First
  it tries the WCL bridge: `IO80211Glue::sendIOUCToWcl(0xC02869C9, 12, dst, 0x1C)`;
  if it fails or returns `0xE082280F` (`-528340977`), it calls
  `apple80211getCARD_CAPABILITIES(iface, dst)` and logs
  `"loadCardCapabilities status=<0x%x> "`. The copy is 28 bytes:
  u32 version at `+0xB30..+0xB33` and the 24 capability bytes at `+0xB34..+0xB4B`.
- `apple80211getCARD_CAPABILITIES` (`0xffffff80022629e6`): calls cell 24 of the
  interface vtable (with type 12; continues only if it returns 0), gets the
  controller via cell 21 and calls its cell `+0xCD0` (slot 410) with `(iface, buf)` —
  that is, the `getCARD_CAPABILITIES` of the concrete driver. The driver output stays
  as-is at `+0xB30..+0xB4B`.
- `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800229a2b2`): the accessor
  `byte *(card + n/8 + 2868)` with bit `n & 7` (`2868 = 0xB34`). It has no direct callers
  in the kernel collection: readers access the block directly.
- Userspace path: `getCARD_CAPABILITIES` (`0xffffff8002279e89`, GET table) fills a
  local 28-byte buffer through the same double path (WCL or driver) and does
  `memmove(req, buf+4, min(req_len, 21))`: the strip of the 4-byte version that
  shifts the map in userspace (`Docs/ARCHITECTURE.md` "AWDL gate FOUND").

Byte map: `+0xB34+k` is `p[4+k]` of the driver's `apple80211_capability_data`, and
it contains capabilities `8k..8k+7` (bit n = capability n). `p[0..3]` is the u32 version
at `+0xB30`.

The RTL layer must keep filling the version at `p[0] = 1` and the 24 bytes
`p[4..27]`; any byte left at 0 is a bit the kernel sees clear.

## 3. Method

Sweep of the 4-byte displacements `0xB30..0xB50` over the complete kernel collection
(826 segments, 1465 candidates), filtered by WiFi filesets
(IO80211Family/AppleBCMWLAN*/IOSkywalkFamily), verification of the base chain of each
read (`pm+0x18 → +0x520 → +0x120` in the AWDL manager, `iface+0x128 → +0x168 → +0x120`
in `IO80211InfraInterface`, `peer → manager → +0x120` in `IO80211Peer`) to exclude the
same-named ivars of `IO80211Peer`/`IO80211PeerManager`/`state`, and decompilation of the
reader functions. The cited VAs are instruction heads verified against the
base analysis. Sites with a `strncmp`/`jmp`/log-constant pattern were
discarded as false positives.

## 4. Byte-by-byte readers

"cap n" = capability n per the accessor map. "RTL" = current value of the byte in
`RTLAirController::getCARD_CAPABILITIES`. Function addresses are the starts.

### `+0xB34` (p[4], capabilities 0–7) — RTL `0x6F`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` (`0xffffff80021c54ea`) | `0xffffff80021c56d8` | `& 0x04` (cap 2) | If clear: log `"ERROR: invalid card capabilities "` and continues (does not abort). The layer has it set (0x6F ∧ 0x04). |

Rest of the byte without readers. Broadcom sets the base `0x6F` the same as the layer (plus
`| 0x80` with `shouldSupportTethering`).

### `+0xB35` (p[5], capabilities 8–15) — RTL `0xE6`

No block readers in the kernel. Broadcom adds `| 0x08` with feature flag 118.

### `+0xB36` (p[6], capabilities 16–23) — RTL `0x61`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211InfraInterface::updateLinkSpeed` (`0xffffff8002360f36`) | `0xffffff80023610cb`, `0xffffff8002361211`, `0xffffff8002361327` | full byte; bits 2 (cap 18) and 3 (cap 19) | They choose the 40 MHz (`VHT_MCS_RATE_TABLE_40MHZ`) and 20 MHz VHT MCS tables for the speed the interface reports. With the current layer (bits 2/3 clear) they are not chosen; it only affects the reported speed, not the air. |

Broadcom sets bit 3 (0x08) if word `ivars+1038` has `0x20` and bit 2 (0x04)
if it has `0x40` (firmware report), on the same base `0x61`.

### `+0xB37` (p[7], capabilities 24–31) — RTL `0x05`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211VirtualInterface::attachToBpf` (`0xffffff8002330386`) | `0xffffff80023303c6` | `& 0x01` (cap 24) | If set, `IOSkywalkNetworkInterface::bpfAttach(if, 12, 0x48)` (DLT 12 tap) on the virtual interface (awdl0); the P2P branches (`peerToPeerAttachToBpf`, roles 6/8/9/10) run earlier without condition. The layer has it set. |
| `IO80211InfraInterface::prepareBSDInterface` (`0xffffff800235ff64`) | `0xffffff800236003a` | `& 0x01` (cap 24) | Same on the infrastructure interface (role 1), after creating the `IO80211AssociationJoinSnapshot`. The layer has it set. |
| `IO80211AWDLPeerManager::configureValidAWDLChannels` (`0xffffff80021caa40`) | `0xffffff80021cab1e` | `& 0x20` (cap 29, dual band) | Without it: qword `+0x4294` (5 GHz social and alternate) to 0 with the log `"Dual band not supported, so assuming 2.4GHz is the basic and should be supported, initialize 5GHz channels to zero"`; the sequence is left with only channel 6. Documented and observed on hardware (`AWDL_SECUENCIA_CON_INFRA.md` §4.2). The layer does NOT have it: the only critical absence. |
| `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` (`0xffffff8002205b94`) | `0xffffff8002205f0c` | `& 0x20` (cap 29) | Without it no 5 GHz social channel is chosen (149 → 44 → master channel) for the assisted discovery traffic registration. |
| `IO80211AWDLPeerManager::fillLegacyDataPathPayload` (`0xffffff80022143ca`) | `0xffffff80022143fb` | `& 0x20` (cap 29) | Base of the u16 flags of TLV 12: `0x8B23` with the bit, `0x8B03` without it. |
| `IO80211AWDLPeerManager::printDataPath` (`0xffffff80022239d4`) | `0xffffff8002223aa2` | full byte | Debug dump. |

Broadcom: `| 0x20` with feature flag 28 (dual band); `| 0x01` in the AWDL branch
(flag 2 == 0, the same branch as `p[8]` bit 0, see `Docs/ARCHITECTURE.md`
"AWDL gate FOUND"); `| 0x80` with flag 27; `| 0x04` as base. The layer sets `0x04 | 0x01`.

### `+0xB38` (p[8], capabilities 32–39) — RTL `0x01`

| Reader | VA | Test | Effect |
|---|---|---|---|
| wifip2pd (userspace) | `sub_100047660` (`AWDL_STRUCTS.md` §1: `0x1000483b5`) | bit 0 (cap 32) | Creation gate of `awdl0` (`VIRTUAL_IF_CREATE`). The layer has it set. |

No reader of this byte in the kernel: bits 1/2 (caps 33/34) that Broadcom sets
with flags 21+fw and 27 have no consumer found in the kernel collection. The
`[obj + i*4 + 0xB38]` accesses of `IO80211PeerManager` (`resetTxPathHealthCheck`,
`interfaceMonitor`, `preQueuePacket`, …) are the peer manager's own counters, not
the card block: without the `+0x120` chain, discarded.

### `+0xB39` (p[9], capabilities 40–47) — RTL `0x40`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5804` | bit 7 (cap 47) with `+0xB3C` bit 7 | If BOTH are clear: log `"Dynamic country code not supported on this device"` and `sub+0x6FC = 0` (`sub` = `*(this+0x18)`; `initParamsToDefaults` sets it to 1 at `0xffffff80021c7a95` and `NANLocaleManager::configure` also to 1 at `0xffffff800229f4ea`, and both `configure` lower it to 0 with the same condition). It is the flag of the country code learned from a peer: its only reader, `IO80211PeerManager::setCountryCodeState` (`0xffffff800225b5e6`, called from `configureValidAWDLChannels` and `NANLocaleManager::configure`), only with the cell ≠ 0 and the host country empty or `X…` moves to state 2, "ask a P2P peer for the country code". The layer has them clear: with a valid country via GET 8 the branch is not taken; without a country, AWDL does not learn it from an iPhone. |
| `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` (`0xffffff800225b736`) | `0xffffff800225b74c`, `0xffffff800225b755` | bit 7 (cap 47) or `+0xB3C` bit 7 (cap 71) | Returns 1 if either is set. |
| `IO80211NANLocaleManager::configure` (`0xffffff800229f4d2`) | `0xffffff800229f502`, `0xffffff800229f50b` | bits 7 (caps 47/71) | The same dynamic country code condition, NAN path. |
| `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80023612f3` | bit 3 (cap 43) | Chooses `VHT_MCS_RATE_TABLE_80MHZ`. Broadcom sets it with a firmware report (`ivars+1024 >= 128` and `ivars+1066 & 0x20`). Clear in the layer: cosmetic. |

Broadcom adds bits to this byte with flags 1 (`| 0x01`) and 29 (`| 0x80`), and `| 0x20`
under another condition not detailed, on the base `0x40` the layer copies.

### `+0xB3A` (p[10], capabilities 48–55) — RTL `0x14`

No block readers in the kernel. The gates of this byte are in userspace:
Apple80211IsAuthSelAllowed/_getMFPCaps read bits 0/4 (`Docs/PHASE5_IO80211.md` §432,
cap 52 = PMF; the layer added `0x10` for that) and the `0x04` base of Broadcom is
unconditional. Broadcom adds `| 0x08` (cap 50) in the AWDL branch and `| 0x20`/`| 0x40` with
flags 39/46.

### `+0xB3B` (p[11], capabilities 56–63) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c585e` | `& 0x02` (cap 57, RSDB) | If set: log `"RSDB is supported on this device"` and `state+0xA728 = 1`. That state feeds `buildRangingTlv` (bit `0x08` of TLV 30, `0xffffff8002212790`) and the accounting of `updateChanOutageTime`. Clear in the layer: correct for a single radio. |
| `IO80211NANPeerManager::initDefaultNANParams` (`0xffffff80022ad378`) | `0xffffff80022ad3cd` | `& 0x02` (cap 57) | Without it: `"SDB not supported, switching to single map"` in NAN. |
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5905` | bit 7 (cap 63, CLM) | If set: log `"CHANNEL CLM RESTRICTED CAPABILITY is supported on this device"` and `state+0xA72A = 1`. |
| `IO80211NANLocaleManager::isCardChannelCLMRestrictedCapable` (`0xffffff80022a009c`) | `0xffffff80022a00b9` | `>> 7` (cap 63) | Getter. |
| `IO80211NANLocaleManager::isChannelValid` (`0xffffff800229fb72`) | `0xffffff800229fbcf` | bit 7 (cap 63) | With the bit clear the channel is deemed valid for this filter; with the bit set it is required that the properties bring neither radarDFS (`0x1`) nor restricted CLM (`0x100`), and if they do: `"Channel %d is invalid. CLM restricted is %s and Radar DFS is %s"`. |
| `IO80211InfraInterface::updateChanOutageTime` (`0xffffff8002362cf4`) | `0xffffff8002362d21` | `& 0x02` (cap 57) | Reads the RSDB bit for the per-channel outage count; the exact later use was not followed `?`. |

### `+0xB3C` (p[12], capabilities 64–71) — RTL `0x01`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5957` | `& 0x08` (cap 67, DFS proxy) | If set: log `"DFS Proxy (AWDL on DFS) is supported on this device"`, `state+0x4019 = 1`, `state+0x401A = 1` and a call via `state+0x12C0`. The layer has it clear: without a firmware proxy, enabling it would fake a capability that cannot be fulfilled. |
| `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80022ad468` | `& 0x08` (cap 67) | Same in NAN (`nan+26 = 1`). |
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c580d` | bit 7 (cap 71) | With `+0xB39` bit 7: dynamic country code not supported (see `+0xB39`). |
| `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff800225b755` | `>> 7` (cap 71) | Second half of the getter. |

### `+0xB3D` (p[13], capabilities 72–79) — RTL `0x0A`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c58b3` | `& 0x40` (cap 78, concurrency) | If set: log `"Concurrency supported"` and `state+0xD066 = 1`. The readers of `state+0xD066` were not followed `?`; with the layer clear it is not activated. |
| wifip2pd (userspace) | `0x10004812a`, `0x10004826d` | bit 0 (cap 72, NAN) | NAN/NAN Data gate. The layer deliberately leaves it clear. |
| airportd (userspace) | — | bit 3 (cap 75, SAE) | `"WPA3-SAE security not supported for this platform"` if missing (`Docs/PHASE5_IO80211.md` §432). The layer has it set (`0x08`). |

### `+0xB3E` (p[14], capabilities 80–87) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211InfraInterface::createLinkQualityMonitor` (`0xffffff8002366426`) | `0xffffff8002366555` | `& 0x08` (cap 83) | Without it: `"LinkQualityMonitor not supported"` and `0xE00002C7` — the link quality monitor of the infra association is not created (slow-wifi symptoms). Diagnosis only; harmless for AWDL. |

Broadcom sets several bits of this byte with flags 88/25/etc. (`| 0x80`, `| 0x40`,
`| 0x10`, `| 0x08`, `| 0x04`, `| 0x02`, `| 0x01`).

### `+0xB3F` (p[15], capabilities 88–95) — RTL `0x00` — the "6E" byte

Bit 7 (cap 95, AWDL 6E):

| Reader | VA | Effect |
|---|---|---|
| `IO80211PeerManager::is6ECapable` (`0xffffff800225f8fa`) | `0xffffff800225f910` | Returns bit 7 (`>> 7`). |
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5a39` | Byte ≠ 0: log `"supported AWDL 6E"`. |
| `IO80211AWDLPeerManager::buildAwdlDataPathStateTlv` (`0xffffff8002210e5a`) | `0xffffff8002210f14` | `>> 7` → bit 11 of the extended flags of TLV 12. |
| `IO80211AWDLPeerManager::buildPrefChanTlv` (`0xffffff8002211416`) | `0xffffff800221144a`, `0xffffff80022115f5` | TLV 33 (6 GHz preferred channel) is only built if the byte is negative (bit 7 set). |

Bit 6 (cap 94, 6E infra):

| Reader | VA | Effect |
|---|---|---|
| `IO80211PeerManager::isInfra6ECapable` (`0xffffff800225f91c`) | `0xffffff800225f932` | `& 0x40`. |
| `IO80211AWDLPeerManager::enableAWDL` (`0xffffff80021dcf58`) | `0xffffff80021dcfce` | Sets the wait timeout to 35000 ms with the bit, 25000 ms without it. |
| `IO80211AWDLPeerManager::monitorAWDLState` (`0xffffff80021ed724`) | `0xffffff80021ed766` | Same (35000/25000 ms). |
| `IO80211AWDLPeerManager::getScanTimeout` (`0xffffff80021f3524`) | `0xffffff80021f353a` | Same. |
| `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80022ad456` | Byte of the NAN state. |

Bits 4 and 3 (caps 92/91, DPS):

| Reader | VA | Effect |
|---|---|---|
| `IO80211PeerMonitor::checkForDPS` (`0xffffff800237e7f4`) | `0xffffff800237fe49` | `& 0x10`: without it the DPS recovery is skipped (`"DPS recovery disabled"`). |
| `IO80211PeerMonitor::recordDPSSymptomsInput` (`0xffffff800237ff60`) | `0xffffff800238016f` | `& 0x08`: without it the message `"DPS Symptoms trigger posted message to userspace"` is not published. |

Full byte ≠ 0 (any bit of the byte activates these paths): the 6G/pro-mode group and the
traffic registrations of the AWDL manager — `updatePref6GChannel` (`0xffffff80021c8882`,
`0xffffff80021c88b3`), `findCommon6GChannel` (`0xffffff80021fa150`, `0xffffff80021fa183`),
`is6GChannelALlowedForP2P` (`0xffffff80021f5fc0`, `0xffffff80021f5fe8`),
`is6GChanSeqAllowed` (`0xffffff80022379dc`, `0xffffff80022379f2`),
`isProModeAllowed` (`0xffffff8002238248`, `0xffffff800223827d`),
`shouldEnterProMode` (`0xffffff800223840a`, `0xffffff8002238435`),
`enteringProMode` (`0xffffff800223878c`, `0xffffff80022387c5`),
`monitorProModeState` (`0xffffff80021f1804`, `0xffffff80021f185b`),
`processPeerProModeTlv` (`0xffffff8002236dc8`, `0xffffff8002236dfa`),
`processPeerProModeUMIStatus` (`0xffffff80021e9760`, `0xffffff80021e9790`),
`notifyPeerRemovedFromCache` (`0xffffff8002238b34`, `0xffffff8002238b6b`),
`setHighThruputState` (`0xffffff80021faa08`, `0xffffff80021faa7a`),
`setAWDLMaxBandwidth` (`0xffffff80021cde02`, `0xffffff80021cde35`),
`removePeerFromRealTimeGroup` (`0xffffff80021e8186`, `0xffffff80021e8778`),
`processAirDropEnd` (`0xffffff80021bd44e`, `0xffffff80021bd48e`),
`doMinimalAFProcessing` (`0xffffff80021b3ea6`, `0xffffff80021b4117`),
`actionFrameReport` (`0xffffff80021af6be`, `0xffffff80021af973`),
`handleAirDropTrafficReg` (`0xffffff80022087ac`, `0xffffff8002208e27`),
`setAwdlPeerTrafficRegistration` (`0xffffff800220946a`, `0xffffff800220bdb8`),
`handleMigrationTrafficReg` (`0xffffff8002206608`, `0xffffff8002206709`),
`handleAirPlaySinkTrafficReg` (`0xffffff8002207ee0`, `0xffffff8002207f4b`),
`isAirPlaySinkEnabled` (`0xffffff8002207fb4`, `0xffffff8002207fca`),
`airplayStatsPostMetrics` (`0xffffff80022044d2`, `0xffffff800220456e`),
`awdlStatsCollectCachedPeersStats` (`0xffffff8002224a1a`, `0xffffff8002224acc`),
`rtgStatsPostMetrics` (`0xffffff800222a438`, `0xffffff800222a4eb`),
`submitRTGMetricstoCoreAnalytics` (`0xffffff800222f444`, `0xffffff800222f9bc`) — all
with the chain `pm+0x18 → +0x520 → +0x120` verified. The exact polarity of each branch
was not followed one by one `?`; the common pattern is to treat byte ≠ 0 as
"6E capability present".

With the layer at 0 the whole group stays off, which is correct: the 8814AU has no
6 GHz and faking 6E would activate 6G sequence and pro-mode paths the radio cannot
serve. Even bits 3/4 (DPS) should not be set: any bit of this byte
turns on the byte ≠ 0 group. Broadcom sets bits of this byte with feature flags 76/93 and
firmware fields; the exact conditions are in the decompilation of
`AppleBCMWLANCore::getCARD_CAPABILITIES` (`0xffffff8001661076`) and are not copied here
because no kernel gate distinguishes among them (all test byte ≠ 0 or
bits 6/7).

### `+0xB40` (p[16], capabilities 96–103) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211InfraInterface::createLinkQualityMonitor` | `0xffffff8002366a4e` | `& 0x20` (cap 101) | `"LQM-WiFi: slow-wifi Extended CCA capabilities supported : [%hhu]"`: activates the monitor's extended CCA measurement. Diagnostic; harmless. |

### `+0xB41` (p[17], capabilities 104–111) — RTL `0x00`

No block readers in the kernel. Broadcom fills it with flags 105/106/107/108/109/35
and the firmware build report (`v5 <= 0x10`).

### `+0xB42` (p[18], capabilities 112–119) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c59e4` | `& 0x01` (cap 112) | `"Per Peer Stats are supported on this device"` and `state+0xA729 = 1`. Per-peer statistics; harmless clear. |

### `+0xB43` (p[19], capabilities 120–127) — RTL `0x00`

No block readers in the kernel (the candidates were jump tables and
log constants). Broadcom fills it with flags 50/32 and the firmware report
(`fw+240` bits 0/1/2 → `0x40/0x10/0x20`).

### `+0xB44` (p[20], capabilities 128–135) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c6373` | `& 0x40` (cap 134) and the variable of `enable_awdl_leaf` | With both: `"Device prefers leaf-only operation"` and `state+0x102B0 = 1` (Leaf mode; without them 0 is written, `0xffffff80021c63be`). The variable is 1 by default (`0xffffff80021c631c`) and the boot-arg can only turn it off, so the bit alone suffices. With the layer clear: `+0x102B0 = 0`. |

### `+0xB45` (p[21], capabilities 136–143) — RTL `0x00` — the "HE" byte

Bit 7 (cap 143, HE/Wi-Fi 6):

| Reader | VA | Effect |
|---|---|---|
| `IO80211PeerManager::getHeCapable` (`0xffffff800225f3f0`) | `0xffffff800225f406` | Returns `>> 7`. |
| `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff800221470a` | Byte ≠ 0 → bit `0x100` of the extended flags of TLV 12 (correction: the reader tests the full byte, not only bit 7; with the byte at 0 it makes no difference). |

Bit 6 (cap 142, data statistics): `getLastUnicastRxLinkActivity`
(`0xffffff800225ef14`, `0xffffff800225ef2f`), `getLastUnicastTxLinkActivity`
(`0xffffff800225f4e6`, `0xffffff800225f501`), `getDataPathPeerStats`
(`0xffffff800225fb7c`, `0xffffff800225fb9f`), `updateCumDataStats`
(`0xffffff8002243178`, `0xffffff8002243196`), `updateIntervalDataStats`
(`0xffffff80022431e6`, `0xffffff8002243204`), `clearIntervalDataStats`
(`0xffffff800224323a`, `0xffffff800224325f`), `logDataStats`
(`0xffffff8002244dbe`, `0xffffff8002244ec4`), `getLastQueuePacket`
(`0xffffff8002245b90`, `0xffffff8002245bb3`), `getCumSlotDataStats`
(`0xffffff80021aead4`, `0xffffff80021aeaf7`), `getIntervalSlotDataStats`
(`0xffffff80021aeb3a`, `0xffffff80021aeb5d`), `handleMulticastTimer`
(`0xffffff80021c38c4`, `0xffffff80021c3989` and `0xffffff80021c3a45`),
`handleEnsembleTrafficReg` (`0xffffff8002202960`, `0xffffff8002203a10` and
`0xffffff8002203f52`), `logRealTimePrintStats` (`0xffffff8002238cd4`,
`0xffffff8002238d54`), `updateTxRxLatency` of `IO80211InfraInterface`
(`0xffffff8002367aa8`, `0xffffff8002367b9e`): all `& 0x40`, gates of counting and
metrics. Harmless clear.

Broadcom fills the byte with flags 114/115/116/117 (bits 1–5) and 67 (bit 7, HE). The layer
leaves it at 0 and that is the honest choice: the 8814AU is not 802.11ax.

### `+0xB46` (p[22], capabilities 144–151) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211RangingManagerExt::getRangingCaps` (`0xffffff8002348958`) | `0xffffff80023489df` | `& 0x10` (cap 148) | After the internal ioctl 266, sets box 146 of `apple80211_ranging_capabilities_t` (fine timing) only with the bit. Harmless clear. |
| `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80022ad443` | `>> 7` (cap 151) | Byte of the NAN state. |

### `+0xB47` (p[23], capabilities 152–159) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5a83` | `& 0x20` (cap 157, Limited SDB) | If set: `"Limited SDB is supported on this device"` and `state+0x103FC = 1`; if not, `state+0xFFC4 = 0`. Closes the `?` of `AWDL_SECUENCIA_CON_INFRA.md` §12: the byte adjusts the limited SDB state, it does not touch slots of the sequence. |
| `IO80211AWDLPeerManager::buildRangingTlv` (`0xffffff80022126f8`) | `0xffffff8002212790` | `& 0x10` (cap 156) | Bit `0x10` of TLV 30 (ranging). Broadcom sets this bit ALWAYS (`p[23] |= 0x10` unconditional, and `| 0x40` if `ivars+32189 & 1`); bit 5 (`0x20`, Limited SDB) its GET never sets. With the layer at 0, TLV 30 would come out without the bit `?` (the builder today does not emit TLV 30 with the current templates). |

### `+0xB48` (p[24], capabilities 160–167) — RTL `0x00`

| Reader | VA | Test | Effect |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c63c4`, `0xffffff80021c63d6`, `0xffffff80021c63e8` | bits 1/2/3 (caps 161/162/163) | Bit 1 clears `state+0x401A` (the DFS proxy flag of cap 67); bit 2 sets `state+0x103A0 = 1` (`0xffffff80021c63e1`; without the bit it is not written and the 0 of `initParamsToDefaults` remains, `0xffffff80021c83ac`); bit 3 writes `state+0x102B1` = 1 or 0 (`0xffffff80021c63fc` / `0xffffff80021c63f3`). Before the three tests, `configure` writes the Leaf flag into `state+0x102B0` (§`+0xB44`, `0xffffff80021c63be`). Readers in the next table. |

Readers of the cells `p[24]` sets (sweep of operands `[reg+0x103A0]` and
`[reg+0x102B1]` over the whole kernel collection; there are no accesses to `+0x102F1`, which was an
erroneous read of `+0x102B1`):

| Cell | Reader | VA | Use | With the layer (bit clear) |
|---|---|---|---|---|
| `state+0x103A0` (cap 162) | `reportDataPathEvents`, case 127 | `0xffffff80021dded7` | With the cell at 1, an infra association started by driver/firmware suspends AWDL (`"Infra assoc started by driver/fw, suspend AWDL"`, `setAwdlSuspendedMode`) or moves to `setRoamChannelSequence`. | At 0: event 127 does nothing. |
| `state+0x103A0` | `reportDataPathEvents` | `0xffffff80021df09b`, `0xffffff80021df610`, `0xffffff80021df6f8` | Only an argument of the log `"DeviceCannotRoamDuringAWDL %d RoamChanSeqInEffect %d …"`, which names the cell. | Log with 0. |
| `state+0x103A0` | `addPeerToRealTimeGroup` | `0xffffff80021e9346`, `0xffffff80021e938f` | At 1 disables P2P steering and defers it (`"Delaying the steering !"`). | At 0: does not force the deferral (the other conditions decide). |
| `state+0x103A0` | `monitorAWDLState` | `0xffffff80021eda2e` | At 1, with `+0x103A1` set, measures the time since the association for roaming (`"Time since infra assoc for roam"`). | At 0: branch skipped. |
| `state+0x102B1` (cap 163) | `gatherInfraStats` (called from `monitorInfraTraffic`) | `0xffffff80021efdfc`, `0xffffff80021effc7`, `0xffffff80021f0121` | Source of the infra traffic counters: at 1, an object the controller returns via its vtable slot `+3200` (Broadcom); at 0, the IOReporter channels of the controller's `IO80211InterfaceMonitor` (`IfTxPkBe`, `IfTxPkBk`, `IfTxPkVo`, `IfTxPkVi`, …). | At 0: reads the `InterfaceMonitor`. If the layer does not feed those channels, AWDL sees infra traffic at 0 `?`. |

`state+0x102B0` (Leaf mode, §`+0xB44`) is read by `setElectionMetricsToDefault`,
`setAwdlElectionMetric`, `setElectionMetricForAirplaySink` and
`setElectionMetricForNonAirplaySink`; it does not depend on `p[24]`.
| `IO80211AWDLPeerManager::isProMode5Enabled` (`0xffffff8002236d76`) | `0xffffff8002236d9e` | `& 0x10` (cap 164) | Enables pro-mode 5. Broadcom sets it next to `p[15]` bit 7 (flag 93). Harmless clear. |

### `+0xB49`, `+0xB4A`, `+0xB4B` (p[25..27], capabilities 168–191) — RTL `0x00`

No readers in the kernel. Broadcom never sets anything either: its zeroing
qwords cover `p[20..27]` and there are no later stores. Equal in both layers.

## 5. Userspace gates (reference)

The ones that do not go through the `+0xB34` block but consume the same GET (via
`getCARD_CAPABILITIES` `0xffffff8002279e89` with the 4-byte strip): wifip2pd AWDL =
userspace byte 4 bit 0 = `p[8]` bit 0; wifip2pd NAN = byte 9 bit 0 = `p[13]` bit 0
(`AWDL_STRUCTS.md` §1, `Docs/ARCHITECTURE.md` "AWDL gate FOUND"); airportd SAE =
capability 75 = `p[13]` bit 3 (`Docs/PHASE5_IO80211.md` §432); PMF = userspace byte 6
bits 0/4 = `p[10]` (`Docs/PHASE5_IO80211.md` §432-433). The layer covers all four as
appropriate (AWDL yes, NAN no, SAE yes, PMF yes).

## 6. What is missing in `RTLAirController::getCARD_CAPABILITIES` and what matters

Current state (read from the working tree of the other session): `p[4..5] = 0xE66F`,
`p[6] = 0x61`, `p[7] = 0x05`, `p[8] = 0x01`, `p[9] = 0x40`, `p[10] = 0x14`,
`p[12] = 0x01`, `p[13] = 0x0A`, rest 0.

| Bit | Readers that ask for it | Verdict for AWDL with infra |
|---|---|---|
| `p[7]` bit `0x20` (cap 29, dual band) | `configureValidAWDLChannels` `0xffffff80021cab1e`, `handleAssistedDiscoveryTrafficReg` `0xffffff8002205f0c`, `fillLegacyDataPathPayload` `0xffffff80022143fb` | **CRITICAL and unique.** Without it there is no 5 GHz social channel: sequence with only 6, discovery assistance without 5 GHz, TLV 12 with base `0x8B03`. Already observed on hardware; the patch `~/re_cache/work/dualband_cap.patch` (`p[7] |= 0x20`) covers it. |
| `p[6]` bits `0x04/0x08` (caps 18/19) and `p[9]` bit `0x08` (cap 43) | `updateLinkSpeed` | Optional. They only choose the VHT MCS 40/20/80 tables for the reported speed of the association. Adding them would make the speed more faithful at 40/80 MHz; without them the reported speed falls to the base table. Does not touch the air nor AWDL. |
| `p[14]` bit `0x08` (cap 83) and `p[16]` bit `0x20` (cap 101) | `createLinkQualityMonitor` | Optional/diagnostic. Without cap 83 the infra does not create the link quality monitor (slow-wifi symptoms); with the current layer there is simply no LQM. |
| `p[12]` bit `0x08` (cap 67, DFS proxy) | `configure` `0xffffff80021c5957` | Leave it clear. Faking the DFS proxy activates `state+0x4019/0x401A` and a path that expects the firmware to serve AWDL over radar channels; the 8814AU does not do that. With non-DFS infra (153) it is not missed. |
| `p[11]` bits `0x02` (cap 57, RSDB) and `0x80` (cap 63, CLM) | `configure`, `initDefaultNANParams`, `isChannelValid`, `updateChanOutageTime`, `buildRangingTlv` | Leave them clear. A single radio is not RSDB; CLM is an additional regional restriction. |
| `p[15]` (whole byte, caps 88–95) | 6E/pro-mode/DPS group (§`+0xB3F`) | Leave it at 0. The 8814AU has no 6 GHz; any bit of the byte additionally turns on the byte ≠ 0 group (6G/pro-mode). The cost of leaving it clear is only the pair of timeouts 25000/35000 ms and the DPS symptoms. |
| `p[21]` (caps 136–143, HE and stats) | `getHeCapable`, `fillLegacyDataPathPayload`, stats gates `& 0x40` | Leave it at 0. HE would be false; bit 142 only trims metrics. |
| `p[22]` bit `0x10` (cap 148) and `p[23]` bit `0x10` (cap 156) | `getRangingCaps`, `buildRangingTlv` | Optional. Broadcom always announces ranging (cap 156 unconditional); without it TLV 30 comes out without the bit and userspace does not see fine timing. Harmless for AirDrop/AirPlay. |
| `p[23]` bit `0x20` (cap 157, Limited SDB) | `configure` `0xffffff80021c5a83` | Leave it clear: not even Broadcom's GET sets it. |
| `p[24]` (caps 160–167) | `configure`, `isProMode5Enabled` | Leave it clear. Bit 2 (cap 162) is "the device cannot roam during AWDL": set, an association started by the firmware suspends AWDL and P2P steering is deferred; the 8814AU does not have that limitation. Bit 3 (cap 163) only chooses where AWDL reads the infra traffic counters from (§`+0xB48`). Bit 1 is the DFS proxy, bit 4 is pro-mode 5. |
| `p[13]` bit `0x40` (cap 78, concurrency) | `configure` `0xffffff80021c58b3` | Leave it clear: it only affects SoftAP+AWDL concurrency (`AWDL_CAP78_CONCURRENCIA.md`). |

In short: with the dual-band patch (`p[7] |= 0x20`) the block is complete for
AWDL with an infrastructure association. Everything else missing is cosmetic
(reported speed, ranging, metrics), diagnostic (LQM, DPS) or deliberately
clear (6E, pro-mode, DFS proxy, RSDB, HE).

## 7. Corrections and verifications of previous documents

- `AWDL_CAPS_ADVERTISED.md` §8 (TLV 12, extended flags): "`0x100` = bit 7 of
  `ctrl_data+0xB45`" — the real reader (`fillLegacyDataPathPayload`,
  `0xffffff800221470a`) tests the full byte ≠ 0; only `getHeCapable`
  (`0xffffff800225f406`) uses bit 7 alone. With the byte at 0 the practical outcome does not
  change.
- `AWDL_SECUENCIA_CON_INFRA.md` §12, open items: the capability bytes `+0xB3F` and
  `+0xB47` read in `configure` are closed (§`+0xB3F` and §`+0xB47` of this
  document): bit 7 of `p[15]` = AWDL 6E (only a log in `configure`, plus the 6G group),
  bit 6 = 6E infra (timeouts 35000/25000 ms), bit 5 of `p[23]` = Limited SDB
  (`state+0x103FC`).
- `AWDL_SECUENCIA_CON_INFRA.md` §9, row "Dual band": the VA of the gate
  `0xffffff80021cab1e` is re-verified as an instruction head.

## 8. Open items

- Closed: `state+0xD066` (cap 78, in `AWDL_CAP78_CONCURRENCIA.md`), `state+0x103A0` and
  `state+0x102B1` (bits 2/3 of `p[24]`, §`+0xB48`) and `sub+0x6FC` (§`+0xB39`).
- Whether the layer feeds the IOReporter channels `IfTxPk*`, `IfTxBy*`, `IfRxPkts` and `IfRxByts` of the `InterfaceMonitor`
  that `gatherInfraStats` reads with cap 163 clear. The only writers of the monitor are
  `IO80211InterfaceMonitor::recordOutputPacket(__mbuf *)` (from
  `IO80211VirtualInterface::dequeueTxPackets`), `recordOutputPacket(apple80211_wme_ac,int,int)`
  and `recordInputPacket(int,int)` (from the same-named ones of `IO80211SkywalkInterface`; the output one is slot
  358) and `recordInterfaceTxRxCounts` (from
  `IO80211SkywalkInterface::updateInterfaceDataStats`, slot 453). The layer code does
  not call any; whether the family does it on the RTL infra path was not followed (calls
  via vtable) `?`. If nobody feeds them, AWDL sees zero infra traffic in
  `monitorInfraTraffic` `?`.
- Branch-by-branch polarity of the byte ≠ 0 group of `+0xB3F` (30+ sites listed in §4):
  common pattern verified, individual branches not followed `?`.
- Exact semantics of `isAirPlaySinkEnabled` (`0xffffff8002207fb4`) reading `+0xB3F`:
  the symbol suggests the sink gate; the content read is the 6E byte `?`.
- Bits of `p[6]`/`p[8]`/`p[9]` that `updateLinkSpeed` does not use (0,1,4–7 of p[6]) and the rest
  of `p[17]`–`p[19]`: no reader in the kernel; only Broadcom writes them according to feature
  flags `?`.
- The field `+0xB50` of the card object (dword, read by `enableAWDL` at
  `0xffffff80021dd07e` with `& 0x4000000` → `state+0x422E`) is outside the block: the
  GET copy is 28 bytes (`+0xB30..+0xB4B`) and that field is written by another path `?`.
