# Emitters of the apple80211 requests over an AWDL interface (macOS 26, Darwin 25.6, x86_64)

Closes the question of [`AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md) and
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §1: wifip2pd only emits 6 requests
(types 12, 94, 97, 293), but the kernel exposes ~70 AWDL types. This document identifies who
emits the rest and by which path each emission goes down to the kernel. Base: byte sweep of the
complete dyld shared
cache `x86_64h` (7 files, 2.1 GB, `/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/`
with its `.map`), sweep of `BootKernelExtensions.kc` (md5
`f489ff8be80e6013a1bf47561dcaef45`, mapping
`VA = 0xffffff8000100000 + fileoff` from §2 of AWDL_APPLE80211_DISPATCH.md), symtab extracted
from the `__LINKEDIT` pool of the cache, IDA headless analysis over copies in
`Build/ida_awdl/task_awdl_issuers/` (airportd/sharingd/wifivelocityd `x86_64` from
`/usr/libexec/`, frameworks carved from the cache) and `otool`/`strings` over the binaries of
`/usr/libexec/`.
Daemon addresses are VAs of the x86_64 slice (implicit base `0x100000000` in the
`sub_1000xxxxx` subroutines); framework ones are cache VAs (`0x7ff8…`/`0x7ffa…`/`0x7ffc…`);
kernel ones are `.kc` VAs. Anything unverified goes as `?`.

## 1. Short answer

1. **The mass of the ~70 AWDL types is emitted by no userspace process: the kernel's own
   `IO80211AWDLPeerManager` emits it** over its interface, through the internal path (magic
   `0x802869C8` → `IO80211Glue::receiveMessageInternal` → `apple80211RouteInternal` →
   `gInternalSetHandlerTable`). The `.kc` contains 336 sites of the SET magic `0x802869C8` and
   299 of the GET magic `0xC02869C9`; dozens of the SETs fall inside PeerManager functions whose
   type matches the function itself (§6.2): every setter/getter of the manager self-calls through
   the same dispatch path userspace would use. The internal command encoding (token
   `0x802869C8` with the type in the high dword) is documented in
   [`AWDL_PEERMANAGER_SOFTWARE.md`](AWDL_PEERMANAGER_SOFTWARE.md) §9.2.
2. In userspace **a single system component emits apple80211 ioctls:
   `IO80211.framework`** (the userspace version, in the cache). The complete cache sweep
   produces exactly 5 GET sites (`0xC02869C9`) and 6 SET sites (`0x802869C8`), all inside
   that image (§2.2). The constant `0xC02869C8` («userspace SET» per the dispatch doc) **does not
   appear as an immediate in any system binary** (0 hits in the cache and in the 5
   daemons): userspace SETs all travel as `0x802869C8`.
3. `Apple80211.framework` no longer implements the API: its symtab only exports 13 external
   symbols (`AC*` of AppleConnect + `_Apple80211VersionString`/`_Apple80211VersionNumber`,
   symtab at `0x2dc07f00` of the pool, 83 entries). The `Apple80211*` API the daemons
   import is defined by `IO80211.framework` (§2.1).
4. Among the daemons, **the only explicit AWDL emitter is `airportd`**: types 381
   (`SET_RANGING_FOLLOW_MODE`) at AWDL start/stop, 164 (`AWDL_PEER_TRAFFIC_REGISTRATION`) via the
   typed layer of CoreWiFi, and the ranging 241/242/380 (§4). `sharingd`, `wifivelocityd`,
   `rapportd` and `bluetoothd` are ruled out with evidence (§7).
5. `CoreWiFi.framework` contains a band of per-type constructors with **all the AWDL types as
   immediates** (§5): it is the typed property layer (`setAWDLXxx:`) through which daemons emit
   without touching IOC numbers. `WiFiPeerToPeer.framework` contributes the configuration
   classes (`AWDLTrafficRegistrationConfiguration`, `AWDLServiceDiscoveryManager`) that airportd
   serializes inside those requests.
6. **wifip2pd uses neither `IOConnectCallMethod` nor the `IO80211APIUserClient`**: its import
   table contains no `IOServiceOpen`/`IOConnect*` (§3.2). It only touches the kernel via
   `Apple80211RawGet/RawSet` (which bind to `IO80211.framework`) and IOKit-registry for
   notifications.

## 2. The Apple80211 API on macOS 26: `IO80211.framework` (userspace)

### 2.1 Exported surface

The symtab of `IO80211.framework`
(`/System/Library/PrivateFrameworks/IO80211.framework/Versions/A/IO80211`,
symtab at `0x23ff49d0` of the `.04` file of the cache, 1517 entries, 1275 defined external
symbols) defines the full API the daemons import (extraction in
`Build/ida_awdl/task_awdl_issuers/io80211_exports.txt`). Relevant rows:

| Symbol | VA |
|---|---|
| `_Apple80211Open` | `0x7ffc1887c4a8` |
| `_Apple80211BindToInterface` | `0x7ffc1887dce5` |
| `_Apple80211BindToInterfaceWithService` | `0x7ffc1887e70e` |
| `_Apple80211Close` | `0x7ffc1887d947` |
| `_Apple80211Get` | `0x7ffc1886792d` |
| `_Apple80211GetWithIOCTL` | `0x7ffc18867943` |
| `_Apple80211Set` | `0x7ffc1885bc95` |
| `_Apple80211SetWithIOCTL` | `0x7ffc18873429` |
| `_Apple80211RawGet` | `0x7ffc1885e3ce` |
| `_Apple80211RawSet` | `0x7ffc1885daef` |
| `_Apple80211CopyValue` | `0x7ffc1885b2f8` |
| `_Apple80211SetProperty` | `0x7ffc1887ad50` |
| `_Apple80211IOCTLGetWrapper` | `0x7ffc18872ab0` |
| `_Apple80211IOCTLSetWrapper` | `0x7ffc18866e98` |
| `_Apple80211StartMonitoringEvent` | `0x7ffc1885da42` |
| `_Apple80211EventMonitoringInit` / `…Init2` / `…InitIOUC` | `0x7ffc1887b102` / `0x7ffc1885d77c` / `0x7ffc1887b45c` |
| `_Apple80211RawEventMonitoringInit` / `_Apple80211RawP2PEventMonitoringInit` | `0x7ffc1887c0a0` / `0x7ffc1887c1e6` |
| `_Apple80211EventMonitoringHelper` | `0x7ffc1888ab7a` |
| `___Apple80211AWDLCompatibilityCall` | `0x7ffc188a2398` |

`Apple80211.framework` is instead a transition library: its symtab only defines `AC*`
(`_ACInterfaceCreate` `0x7ff81f8ab8f8`, `_ACInterfaceDeviceNameCopy` `0x7ff81f8ab9ae`,
`_ACInterfaceSetPower` `0x7ff81f8aba53`, …) and contains no `Apple80211*`; it links
`IO80211.framework` as `LC_LOAD_DYLIB` (not a re-export). The `Apple80211*` imports of the
daemons resolve against `IO80211.framework`, which `airportd`, `wifip2pd` and `sharingd` carry in
their dylib list (`otool -L`).

### 2.2 The 11 apple80211 ioctl sites of the system (complete cache sweep)

Sweep of the bytes `C9 69 28 C0` (`0xC02869C9`) and `C8 69 28 80` (`0x802869C8`) over the 7
files of the cache (`Build/ida_awdl/task_awdl_issuers/sweep.py`, output `sweep_out.txt`):
all occurrences with a valid VA fall in `IO80211.framework` (`__TEXT` at
`0x7ffc18858000…0x7ffc188c09b0`). Each site inside the function the symtab attributes:

| Function (symtab) | Magic | Sites (VA) |
|---|---|---|
| `_Apple80211CopyValue` (`0x7ffc1885b2f8`) | GET `0xC02869C9` | `0x7ffc1885b3cb` |
| `_Apple80211RawSet` (`0x7ffc1885daef`) | SET `0x802869C8` | `0x7ffc1885dbbf`, `0x7ffc1885dc80`, `0x7ffc1885e1cf` |
| `_Apple80211RawGet` (`0x7ffc1885e3ce`) | GET `0xC02869C9` | `0x7ffc1885e4a6` |
| `_Apple80211IOCTLSetWrapper` (`0x7ffc18866e98`) | SET `0x802869C8` | `0x7ffc18866f4f`, `0x7ffc18867013`, `0x7ffc1886764b` |
| `_Apple80211IOCTLGetWrapper` (`0x7ffc18872ab0`) | GET `0xC02869C9` | `0x7ffc18872b52` |
| `_Apple80211BindToInterfaceWithService` (`0x7ffc1887e70e`) | GET `0xC02869C9` | `0x7ffc1887e89a` |
| `_Apple80211EventMonitoringHelper` (`0x7ffc1888ab7a`) | GET `0xC02869C9` | `0x7ffc1888c468` |

Reading of the decompilation (raw binary copy of the `__TEXT` loaded raw, functions
`sub_11A5D` = RawGet/RawSet region, `sub_12E90` = IOCTLSetWrapper, `sub_1EAA8` = IOCTLGetWrapper,
`sub_1F421` = SetWithIOCTL in `Build/ida_awdl/task_awdl_issuers/io80211_out/`): the wrappers
build the 40 B struct (`apple80211req`: `ifname[16]` @0, u32 type @+0x10, u32 val @+0x14,
u32 len @+0x18, pointer @+0x20 — layout already verified in `AWDL_WIFIP2PD_POST_CREATE.md` §2.0)
and call `ioctl` on a BSD socket. The userspace SET uses **always** `0x802869C8`
(`_IOW('i',200,40)`), not `0xC02869C8`; the kernel accepts it in the same branch
(`AWDL_APPLE80211_DISPATCH.md` §2). `Apple80211Get`/`Apple80211Set` (without `WithIOCTL`)
delegate to the `WithIOCTL`/wrapper variants (`?` the exact chaining: the raw decompilation of
`sub_D1AE` covering the `Set`/`Get` region reports «positive sp» and does not allow confirming
each jump).

## 3. wifip2pd: closing the user client question

### 3.1 Real chaining

The `Apple80211RawGet`/`Apple80211RawSet` of wifip2pd are the exports of `IO80211.framework`
(§2.1): the 6 known call sites (`0x100048d95` type 94, `0x10004b3e6` type 293,
`0x100046db9` type 97, three of type 12) go down through `_Apple80211RawSet`/`_Apple80211RawGet`
→ `ioctl(0x802869C8)`/`ioctl(0xC02869C9)` at `0x7ffc1885dbbf`/`0x7ffc1885dc80`/`0x7ffc1885e1cf`/
`0x7ffc1885e4a6` (§2.2) → `IO80211VirtualInterface::handleIoctlGated` /
`IO80211SkywalkInterface::performGatedCommandIOCTL` (`AWDL_APPLE80211_DISPATCH.md` §2).

### 3.2 No user client

`otool -arch x86_64 -Iv /usr/libexec/wifip2pd`: the only imported IOKit symbols are
`_IORegistryEntryCreateCFProperty` (+1); there is no `IOServiceOpen`, `IOServiceMatching`,
`IOConnectCallMethod`, `IOConnectCallStructMethod` nor `IOConnectCallScalarMethod`. The event
channel is the Apple80211 pipe (`Apple80211StartMonitoringEvent`,
`Apple80211RawEventMonitoringInit`,
`Apple80211RawP2PEventMonitoringInit`), already documented in `AWDL_EVENTS.md` §4. The user
client selectors (`sMethods`, table at `0xffffff8002478ed0` of the `.kc`; 24 B entries decoded
with the `.kc` fixup rule: `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)`) cover 0–8:
`0xffffff80023523f0` (0), `0xffffff800235240a` (1), `extSubscribeEvent` `0xffffff8002352486`
(2), `extEventMonitorInit` `0xffffff800235256e` (3), `extEventMonitorExit`
`0xffffff80023525aa`
(4), `0xffffff80023525e6` (5), `0xffffff8002352662` (6), `0xffffff800235267c` (7),
`0xffffff8002352696` (8, with 2 scalar inputs). None is the selector of the generic «command»
that reaches `performGatedCommandIOUC` (`0xffffff80022f6e30`,
`AWDL_APPLE80211_DISPATCH.md` §2): that specific selector stays `?` — irrelevant for wifip2pd,
which opens no user client.

## 4. airportd: the AWDL userspace emitter

Inventory of its Apple80211 surface (probe1 over copy `airportd_copy.i64`,
`Build/ida_awdl/task_awdl_issuers/airportd/summary.txt`): 45 call sites of `Apple80211Set`,
13 of `Apple80211Get`, 12 of `Apple80211CopyValue`, 2 of `Apple80211RawSet`
(`-[CWHostAPModeManager __startHostAPModeWithNetworkData:]` `0x1000ec1be` and
`__stopHostAPMode` — AP mode, not AWDL), 1 of `Apple80211RawGet`
(`-[CWHostAPModeManager __supportedChannel:]`), 5 of `ioctl` (none with apple80211
constants; the only identified is `0xC0206911` in `sub_100083C1C`), and 1 `IOServiceOpen` +
1 `IOConnectCallAsyncMethod` in `-[WiFiWatchdogMonitor initWithIOService:]`
(`0x1000fbc97`: `IOServiceOpen(..., type=1)` over the driver service and
`IOConnectCallAsyncMethod` at `0x1000fbebb`, both for the watchdog — no apple80211 requests).
The type extraction (`analyze_airportd.py`, output `airportd_calls.txt`) produces no
direct AWDL type except the ones below; the rest are general types (1, 2, 3, 4, 5, 9, 15, 16,
19, 24, 43, 48, 51, 52, 66, 69, 70, 81, 85, 87, 90, 96, 107, 191, 192, 193, 216, 223, 239, 245,
255, 256, 257, 260, 265, 358, 363, 380, 381, 382, 422, 448, 469, 483, 484, 487, 488, 490, 507,
512, 514, 516, 531, 551, plus two sites with `0xFFFFFFFF` as second argument and types 262/263
in `sub_100062404`/`sub_10006259C`, unidentified overload, `?`).

Sites with AWDL/ranging semantics (strings-xrefs of the IDB, `airportd/summary2.txt` and
`airportd/strings_pseudo/`):

| Request | Type / len | Function (airportd VA) | When |
|---|---|---|---|
| `SET_RANGING_FOLLOW_MODE` | 381 / 12 | `-[CWXPCInterfaceContext startAWDL:error:]` `0x1000a0aea` — `Apple80211Set(handle, 381, 0, dict, 8)` with the `APPLE80211KEY_RANGING_FOLLOW_MODE` dict (+`…_BAND`) | AWDL start in «assisted discovery follow» mode (AirDrop/AutoUnlock); log `"FAILED to set APPLE80211_IOC_SET_RANGING_FOLLOW_MODE"` at `0x10013d930` |
| `SET_RANGING_FOLLOW_MODE` | 381 / 12 | `-[CWXPCInterfaceContext stopAWDL:]` `0x1000a10ea` | AWDL stop; same error string |
| `AWDL_PEER_TRAFFIC_REGISTRATION` | 164 / 184 | `startAWDL:error:` `0x1000a0aea` and `stopAWDL:` `0x1000a10ea` via `-[CWXPCInterfaceContext __setPeerTrafficRegistrationWithConfiguration:error:]` — the request is not emitted with an immediate in the airportd text: it goes out through the typed CoreWiFi layer (§5); logs `"FAILED to set APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION…"` at `0x10013d8ca`/`0x10013da02` | On each AWDL start/stop with a traffic registration configuration (assisted discovery, CarPlay, ranging, migration) |
| `RANGING_ENABLE` | 241 | `-[CWXPCInterfaceContext setRangeable:peers:error:]` `0x10009efe8`; also `-[CWXPCInterfaceContext setNDD:]` `0x1000c4bac` (log `0x10013d268`) | Ranging sessions (AutoUnlock/AirDrop) |
| `RANGING_START` | 242 | function at `0x10009ed66` (log `"FAILED APPLE80211_IOC_RANGING_START"` at `0x10013d165`) | Ranging start |
| `SET_RANGING_IDENTIFIER` | 380 | `-[CWXPCInterfaceContext setRangingIdentifier:error:]` `0x10009f1e1` (`Apple80211Set(handle, 380, …)`) | Before each ranging session |
| `TRAP_INFO` (GET) | 257 | `-[CWXPCInterfaceContext __addExtendedWatchdogAnalyticsInfoToMetricIfAvailable:withType:]` `0x1000afe9e` | Analytics (not AWDL) |
| `LEAKY_AP_SSID_STATS` | 245 | `-[CWXPCInterfaceContext __handleLeakyApInfo]` `0x1000b1408` | Not AWDL |

The top-level trigger is the XPC of airportd:
`-[CWXPCSubsystem startAWDLWithInterfaceName:request:connection:error:]`
(`0x1000245a0`, selector string at `0x1001233c0`) and its counterpart
`stopAWDLWithInterfaceName:…`
— the clients (sharingd via `WiFiPeerToPeer.framework`, §5) request AWDL over XPC and airportd
emits these requests over the interface. `startAWDL:` itself validates the role of the interface
(`virtualInterfaceRole == 6`, call to `virtualInterfaceRole` in the pseudocode of
`0x1000a0aea`) before emitting anything.

## 5. CoreWiFi and WiFiPeerToPeer: the typed layer

### 5.1 CoreWiFi.framework

`/System/Library/PrivateFrameworks/CoreWiFi.framework/Versions/A/CoreWiFi` (`__TEXT`
`0x7ff81f8ac000`, 4,801,669 B). Two pieces of evidence:

1. **Typed AWDL ObjC selectors** in its `__TEXT` (offsets over the base `0x7ff81f8ac000`):
   `AWDLElectionID:` `+0x468704`, `AWDLAFTxMode:` `+0x468760`,
   `AWDLAvailabilityWindowAPAlignment:` `+0x46878e`, `AWDLElectionMetric:` `+0x46880e`,
   `AWDLElectionParameters:` `+0x468825`, `AWDLEncryptionType:` `+0x468887`,
   `AWDLPeerTrafficRegistration:` `+0x4689f8`, `AWDLPresenceMode` `+0x468a4c`,
   `AWDLBTLEStateParameters:` `+0x4687c1`, `AWDLContinuousElectionAlgorithmEnabled:`
   `+0x4687e2`, and the property names in the zone `+0x46ad00`–`+0x46b0ff` (individual offsets
   `?`).
2. **AWDL type immediates grouped** into two code bands (pattern scan of
   `mov r32, imm32`/`C7` with values of the AWDL set): band 1 at `+0xb2d11…+0xbf50b`
   — 116 `+0xb51e7`, 117 `+0xb5297`, 118 `+0xb5557`, 119 `+0xb5607`, 120 `+0xb53f7`,
   122 `+0xb54a7`, 125 `+0xb56b7`, 126 `+0xb58c7`, 127 `+0xb5977`, 129 `+0xb5ad7`,
   131 `+0xb5e47`, 132 `+0xb5ef7`, 133 `+0xb5d97`, 134 `+0xb5ce7`, 135 `+0xb5c37`,
   136 `+0xb5fa7`, 138 `+0xb6107`, 155 `+0xaa877`, 163 `+0xabb07`, 164 `+0xabbb7`,
   167 `+0xb6687`, 202 `+0xb9a8c`, 203 `+0xb82f7`, 204 `+0xb83a7`, 205 `+0xb8457`,
   208 `+0xb8667`, 213 `+0xb9ee7`, 217 `+0xbac27`, 219 `+0xb2d11`, 220 `+0xbb1ab`,
   229 `+0xbd101`, 233 `+0xbd417`, 253 `+0xbf50b`; band 2 at `+0x3493da…+0x35aff3`
   with the same set. Example VAs: type 164 at `0x7ff81f957bb7` (band 1) and
   `0x7ff81fbfdc73` (band 2); type 136 at `0x7ff81f961fa7`.

Conclusion: CoreWiFi implements the typed setters/getters (`setAWDLXxx:`) that translate
property → numeric IOC. airportd links CoreWiFi (`otool -L`) and its
`__setPeerTrafficRegistrationWithConfiguration:error:` contains no 164 immediate: the
emission of 164 (and of the rest of the typed types) happens inside CoreWiFi. Which daemon/process
invokes each specific setter stays as `?` (it requires following the ObjC call sites of
CoreWiFi; its classes also appear compiled inside the daemons: the `CWXPC*` of airportd and the
`W5*` of wifivelocityd).

### 5.2 WiFiPeerToPeer.framework

`__TEXT` `0x7ffa1cfce000`, 338,939 B. Classes and keys (strings):
`AWDLTrafficRegistrationConfiguration`
(`+0x4655e`), `AWDLServiceDiscoveryManager`/`AWDLServiceDiscoveryManagerXPC(+Delegate)`
(`+0x467b4`/`+0x46795`), `AWDLStateMonitor` (`+0x4303c`), `AWDLState.channelSequenceKey`
(`+0x43997`), `AWDLState.peerMasterChannelKey` (`+0x439f9`), `<AWDLTrafficRegistrationConfiguration %@ to %@>`
(`+0x41a8d`). sharingd links it (`otool -L /usr/libexec/sharingd`) and talks to airportd over
XPC:
it is the client part of the configuration airportd turns into request 164 (§4). No direct
emission is attributed to it.

## 6. Kernel: `IO80211AWDLPeerManager` as internal emitter

### 6.1 The path

The `.kc` contains 336 sites of the SET magic `0x802869C8` and 299 of the GET magic
`0xC02869C9` (`kc_sweep.py`). Sites of both magics fall in `IO80211Glue::receiveMessageInternal`
(`0xffffff800219426c` the SET) — the receiver of the internal path; the sites inside the static
bridges `__ZL*get/set*` (e.g. `__ZL7getSSID…`, `__ZL9setRSN_IE…`) correspond to the
magic validation at the receiver, not to emissions. The remaining relevant ones are emitters.

### 6.2 PeerManager emissions (type written next to the magic)

Method: for each site the type immediates were extracted (`mov r32, imm32` with value
1–600 and `C7` stores with value 1–600) in a ±0x60 B window
(`kc_types.py`, output `kc_types_out.txt`; aggregation `kc_issuers_full.txt`). The pattern is
systematic: every setter/getter of the manager builds the 40 B struct with **its own IOC
type** and re-injects it through the internal channel. The small types next to the main type
(1, 3, 8, 14, 32) may be constants of the neighboring code and not the `req_type` field; when
there are only small constants with no identifiable type the row says so. (The internal funnel is
`IO80211PeerManager::ioctl_internal`, with the type also packed in the high dword of the
command; see [`AWDL_PEERMANAGER_SOFTWARE.md`](AWDL_PEERMANAGER_SOFTWARE.md) §9.2.)

SET (the highlighted type is the one matching the function):

| Function (`IO80211AWDLPeerManager::`) | Site (VA) | Observed types |
|---|---|---|
| `setAwdlSyncParams(apple80211_awdl_sync_params*)` | `0xffffff800221b7e9+0xad` | **116**, 14 |
| `setAwdlSyncEnabled(…)` | `0xffffff800221baa4+0x262` | **117**, 1, 14 |
| `setAwdlAdvertisers(awdl_peer_advert_add*)` | `0xffffff800221c1c8+0x2a` | **229**, 14 |
| `setAwdlAfTxMode(apple80211_awdl_af_tx_mode*)` | `0xffffff800221c38d+0x5f` | **208**, 14, 32 |
| `setAwdlElectionId(apple80211_awdl_election_id*)` | `0xffffff800221c8c5+0x2d` | **122**, 32 |
| `setAwdlConfig(apple80211_awdl_config_request*)` | `0xffffff800221c9ea+0x2a` | **432**, 1, 14 |
| `setAwdlBssid(apple80211_awdl_bssid*)` | `0xffffff800221caf0+0xae` | **125**, 6, 14 |
| `setAwdlElectionParams(apple80211_awdl_election_params*)` | `0xffffff800221cba2+0x3c` | **155** |
| `setAwdlChannelSequence(apple80211_awdl_sync_channel_sequence*)` | `0xffffff800221d122+0x246` | **129**, 1, 3, 32 |
| `setAwdlOUI(apple80211_awdl_oui*)` | `0xffffff800221d289+0x2d` | **131**, 14 |
| `setAwdlSecondaryMasterChannel(apple80211_awdl_secondary_master_channel*)` | `0xffffff800221d3fe+0xea` | **205**, 1, 8 |
| `setAwdlMasterChannel(apple80211_awdl_master_channel*)` | `0xffffff800221d584+0xa6` | **132**, 1, 8 |
| `setAwdlKey(apple80211_key*)` | `0xffffff800221d66c+0x2e` | **219**, 14 |
| `setAwdlEncryptType(apple80211_encrypt*)` | `0xffffff800221d71c+0x2a` | **220**, 14 |
| `setAwdlSyncState(apple80211_awdl_sync_state*)` | `0xffffff800221e511+0x13d`, `0xffffff800221e556+0x182`, `0xffffff800221e6c3+0x2ef` | **134**, **117**, 1 |
| `setAwdlSyncFramePacketLifetime(apple80211_awdl_sync_frame_packet_lifetime*)` | `0xffffff800221e8e3+0x2d` | **138**, 14 |
| `setLLWParams(apple80211_llw_params*)` | `0xffffff800221e0b0+0x2a` | **344**, 14 |
| `setAwdlTxRate(apple80211_tx_rate*)` | `0xffffff800221e17f+0x6d` | **411** |
| `setAwdlBlockSetCommands(apple80211_awdl_block_set_commands*)` | `0xffffff800221e290+0x2a` | **233**, 14 |
| `setAwdlElectionRssiThresholds(apple80211_awdl_election_rssi_thresholds*)` | `0xffffff800221e355+0x61` | **135**, 14 |
| `setAwdlMinRate(apple80211_awdl_min_rate*)` | `0xffffff80022206a4+0x46` | **213**, 32 |
| `setAwdlElectionMetric(apple80211_awdl_election_metric*)` | `0xffffff80021cb371+0x2f` | 43, 65 — no 126 in the window (`?`) |
| `setAwdlExtensionParameters()` | `0xffffff80021cc6a6+0x36` | no immediate in the window (118 by another path, `?`) |
| `setPresenceMode(uint)` | `0xffffff80021cc5f6+0x50` | **136** |
| `setAwdlTxCalPeriod(apple80211_awdl_txcal_period*)` | `0xffffff800221c139+0x2d` | **226**, 14 (226 unnamed in the kernel table, `?`) |
| `setInfra6GMode(bool)` | `0xffffff8002238f51+0xa7` | 8, 216, **422** (`6G_MODE`) |
| `handleCarPlayTrafficReg(apple80211_awdl_peer_traffic_registration*)` | `0xffffff80022059f9+0x7b` | **164**, 14 |
| `handleAssistedDiscoveryTrafficReg(…,bool)` | `0xffffff8002205e16+0x282` | **164**, 1, 14, 43 |
| `handleRangingTrafficReg(…)` | `0xffffff800220608a+0x7c` | **164**, 14 |
| `handleMigrationTrafficReg(IO80211AWDLPeer*,…)` | `0xffffff80022067ae+0x1a6` | **164**, 1, 14 |
| `checkNSteerForConnectivityCheck(IO80211AWDLPeer*,…)` | `0xffffff80021f9c0d+0x13b`, `0xffffff80021f9cbe+0x1ec`, `0xffffff80021f9d9f+0x2cd` | **164**, 1, 4, 12 |
| `notifyRealTimeModeOnOff(…)` | `0xffffff80021f1fcc+0x274` | 164, 14, 128? |
| `configRTGPeerStats(IO80211AWDLPeer*,bool)` | `0xffffff800222f105+0xef` | 128, **467** (`?` name), 14 |
| `reportRTGPeerStats(IO80211AWDLPeer*)` | `0xffffff800222f3dc+0xf2` | 128, **467**, 14 |
| `programChipWithNewMaster()` | `0xffffff80021ec53d+0x87`, `0xffffff80021ec65d+0x1a7`, `0xffffff80021eca02+0x54c` | 1, 6 — no clear AWDL type (`?`) |
| `setChannelSequenceUnconditional(bool)` | `0xffffff80021cc0ba+0xa20` | 1, 400 (`?`) |
| `setAFTXMode(apple80211_awdl_af_tx_type_t)` | `0xffffff80021f7d5b+0x55`, `0xffffff80021f7db0+0xaa` | 3 (`?`) |
| `forceRoamToPeersAP(ether_addr*,uchar,uchar)` | `0xffffff80021f96e1+0x235` | 76, 140 — 167 expected, not observed (`?`) |
| `initFWElectionParams()` | `0xffffff80021ed247+0x47` | 1 (`?`) |
| `enableAWDL()` | `0xffffff80021dd45a+0x502` | 1 (`?`) |
| `disableAWDL(uint)` | `0xffffff80021c724e+0x766` | 1 (`?`) |
| `configure()` | `0xffffff80021c574b+0x261`, `0xffffff80021c5e7f+0x995`, `0xffffff80021c6012+0xb28`, `0xffffff80021c6096+0xbac`, `0xffffff80021c60ee+0xc04`, `0xffffff80021c61d0+0xce6` | only 1 in the last two (`?`) |
| `interfaceStateChange(StateChangedSource)` | `0xffffff80021da0e1+0x1419` | **13** (`STATE`) |
| `exitQuietMode()` | `0xffffff80021fae8c+0x170` | 1, 17 (`?`) |
| `reevaluateRTGConfiguration(bool)` | `0xffffff80021d4cb3+0x19d` | 3, 5 (`?`) |
| `runPiggybackScanStateMachine()` | `0xffffff80021e5766+0x6b8` | 3 (`?`) |
| `reportDataPathEvents(uint,void*,ulong)` | `0xffffff80021e08f6+0x3002` | 1 (`?`) |
| `setSyncFramePeriod(uint,bool)` | `0xffffff80021ed0e7+0xc3` | no immediate (`?`) |
| `setScheduleState(…)` | `0xffffff80021d4395+0x1bf5` | no immediate (`?`) |
| `setPSFDwell(psfDwell)` | `0xffffff80021f4f7b+0x551` | no immediate (`?`) |
| `programMasterChannels()` | `0xffffff80021d4a13+0xc9`, `0xffffff80021d4a8e+0x144` | no immediate (`?`) |
| `hostapForceChannelSwitch(uint,uint)` | `0xffffff80021d8a22+0x5c` | 2 (`?`) |
| `updatePrimaryPayloadMI(…)` / `updateSecondaryPayloadMI(…)` / `sendUnicastMI(…)` / `configureMITxOnDwellChannel(uint)` / `enterQuietMode()` / `enableRoaming()` / `configureDFSProxy(…)` / `configureDFSProxyAddRemoveUCSAPeer(…)` | `0xffffff80021fb610+0x2a6`, `0xffffff8002214306+0x158`, `0xffffff8002213f24+0x31a`, `0xffffff80022140f5+0x51`, `0xffffff80021f4256+0x1c8`, `0xffffff800220da3b+0x45`, `0xffffff80021f8590+0x38`, `0xffffff80021f8726+0x34` | no immediate in the window (`?`) |

GET (same pattern, magic `0xC02869C9`):

| Function | Site (VA) | Observed types |
|---|---|---|
| `getAwdlSyncParams(…)` | `0xffffff800221ef09+0x77` | **116**, 32 |
| `getAwdlSyncEnabled(…)` | `0xffffff800221f153+0x5f` | **117**, 32 |
| `getAwdlElectionId(…)` | `0xffffff800221f477+0x31` | **122** |
| `getAwdlElectionParams(…)` | `0xffffff800221f6b9+0x2d` | **155** |
| `getAwdlElectionMetric(…)` | `0xffffff800221f559+0x31` | **126**, 14, 32 |
| `getAwdlChannelSequence(…)` | `0xffffff800221f834+0x78` | **129**, 32 |
| `getAwdlMasterChannel(…)` | `0xffffff800221fb23+0x2d` | **132** |
| `getAwdlSecondaryMasterChannel(…)` | `0xffffff800221fc1f+0x4d` | **205** |
| `getAwdlSyncState(…)` | `0xffffff800221fd72+0x2a` | **134**, **135**, 14 |
| `getAwdlElectionRssiThresholds(…)` | `0xffffff800221fd32+0x24` | **134**, **135** |
| `getAwdlPresenceMode(…)` | `0xffffff800221fdea+0x2a` | **136**, 14 |
| `getAwdlEncryptType(…)` | `0xffffff800221d79e+0x2a` | **220**, 14 |
| `getAwdlBlockSetCommands(…)` | `0xffffff800221dd48+0x2a` | **233**, **338**, 14 |
| `getAwdlCCA(apple80211_awdl_cca*)` | `0xffffff800221dcf2+0xbc` | **338**, 14, 233 |
| `getLLWParams(apple80211_llw_params*)` | `0xffffff800221e02e+0x2a` | **344**, 14 |
| `getAwdlDevStats(apple80211_awdl_dev_stats*)` | `0xffffff800221dc20+0x24` | **247**, 14 |
| `getAwdlAfTxMode(…)` | `0xffffff800221c27b+0x5b` | **208** |
| `getAwdlTxCalPeriod(…)` | `0xffffff800221c0b4+0x2a` | **226**, 14 |
| `getP2PDriverMetrics(apple80211_p2p_steering_metrics*)` | `0xffffff800222a34a+0xe` | 1, **456** (`?`) |
| `configureValidAWDLChannels()` | `0xffffff80021cab7f+0x13f` | **207** (`CHANNELS_INFO`), 1, 379 (`?`) |
| `getSibCoexStatusFromDriver()` | `0xffffff80021cb52d+0x39` | no immediate (`?`) |
| `enableAWDL()` (GET) | `0xffffff80021dd180+0x228` | no immediate (`?`) |
| `handleDebugCmd_chanlist(…)` | `0xffffff8002220b0c+0x48` | no immediate (`?`) |

The generic funnel is `IO80211AWDLPeerManager::setIOCTLE(uint,uint,void*)`
(magic site at `0xffffff80021fb8ba+0xc8`): type/len/payload as arguments. The receiver
functions of the types match the table of `AWDL_APPLE80211_DISPATCH.md` §4
(`gSetHandlerTable[115] = setAWDL_SYNC_PARAMS` for 116, etc.): the self-emission walks the same
dispatch path as a userspace request and ends in the same setter, with the glue notification
(`CommonGlue::sendMessage`) included.

### 6.3 Other internal emitters

- `AppleBCMWLANCore::handleCardSpecific(IO80211SkywalkInterface*, uint, void*, bool)`:
  SET `0xffffff800165f7bd+0x147` (type 4), `0xffffff800165f863+0x1ed` (4),
  `0xffffff800165f9dc+0x366` (1, 16, 128), `0xffffff800165ffd7+0x961` (4), sites without immediates
  at `0xffffff800165ff10+0x89a` and `0xffffff8001660036+0x9c0`; GET `0xffffff800165f7d1+0x15b`
  and `0xffffff800165f871+0x1fb` (4). `?` the semantics of each site (CHANNEL 4 and keys 1/16/128).
- `AppleBCMWLANLowLatencyInterface::processBSDCommand(__ifnet*, uint, void*)`:
  SET `0xffffff80015e03f8+0x16` (1, 22, 182), GET `0xffffff80015e040a+0x28` (22, 182, 328) —
  the low-latency interface forwards BSD commands as apple80211 requests.
- `IO80211SkywalkInterface::processBSDCommand(__ifnet*, uint, void*)`:
  SET `0xffffff80022f6f19+0x81` (16, 128), `0xffffff80022f6fb5+0x11d` (1, 128),
  `0xffffff80022f6fd8+0x140` (1), `0xffffff80022f702d+0x195` (no immediate); GET
  `0xffffff80022f6f34+0x9c` (16, 128), `0xffffff80022f6fac+0x114` (1, 128),
  `0xffffff80022f6fe3+0x14b` (1), `0xffffff80022f703b+0x1a3`.
- `IO80211AWDLPeerManager::setInfra6GMode(bool)` SET `0xffffff8002238f51+0xa7` (8, 216, 422) and
  `IO80211NANInfraManager::setInfra6GMode(bool)` SET `0xffffff800232a4f1+0x89` (identical):
  both managers emit `6G_MODE` (422) toward the interface.
- `IO80211NANPeerManager::setIOCTLE` / `IO80211NANDataInterfacePeerManager::setIOCTLE`:
  `0xffffff80022acc9f+0x9d`, `0xffffff80022a977f+0x9d` — the same funnel for the NAN managers.

## 7. Discards

| Process | Discard evidence |
|---|---|
| `sharingd` | Imports `Apple80211Open/BindToInterface/GetIfListCopy/EventMonitoringInit/StartMonitoringEvent/Close` and `IOServiceOpen`+`IOConnectCallStructMethod/ScalarMethod`; the functions calling IOConnect belong to **ACMLib** (`AppleCredentialManager`): `sub_10060015D` (`0x10060015d`, log `"ACMLib" "performCommand"`, path `…/AppleCredentialManager_ClientLibs/ACMLib/ACMLib.c`) and `sub_1005FFB03` (`0x1005ffb03`, `updateLogLevelFromKext`). No `Apple80211Get/Set`: it emits no requests. Its AWDL role is XPC (start/stop to airportd) and event monitoring; it links `WiFiPeerToPeer.framework` for the configuration part (§5.2) |
| `wifivelocityd` | Its only `Apple80211RawSet` is `-[W5WiFiInterface setSTBCEnabled:]` (`0x1000630ef`, STBC type); the `IOServiceMatching` calls are `"IOPlatformExpertDevice"` (`0x10002f253`, `0x10007204e`) and `"apcie"` (`0x1000850c9+`). Its `AWDL*` strings (`AWDLElectionMetric`, `AWDLPresenceMode`, …) are IOReport/metric signal names, not requests |
| `rapportd` | No Apple80211/IOConnect symbol imported at all; only `IORegistryEntryGetRegistryEntryID/GetChildEntry` |
| `bluetoothd` | Binary not present in `/usr/libexec` (in the cache under another path `?`); no hit of the apple80211 magic outside `IO80211.framework` in the whole cache, so no cache component emits direct ioctls |
| Finder/AirDrop UI and other apps | Cannot emit: the sweep proves the only ioctl emitter is `IO80211.framework`; any process that does not go through it is excluded by construction (`?` for on-disk processes not analyzed outside `/usr/libexec`) |

## 8. Master table by type (AWDL)

For each AWDL type of the kernel (`AWDL_APPLE80211_DISPATCH.md` §4 gives name, handler and the
required `len`): who emits it. «PM» = internal `IO80211AWDLPeerManager` (§6.2, VA of the magic
site); «ad» = airportd; «CW» = typed band of CoreWiFi (§5.1, VA = base `0x7ff81f8ac000` +
offset). Types with no identified emitter stay `?`.

| Type | Name | Emitter | Evidence |
|---:|---|---|---|
| 116 | `AWDL_SYNC_PARAMS` | PM | `0xffffff800221b7e9+0xad` (SET), `0xffffff800221ef09+0x77` (GET); CW `+0xb51e7` |
| 117 | `AWDL_SYNC_ENABLED` | PM | `0xffffff800221baa4+0x262` (SET), `0xffffff800221f153+0x5f` (GET), re-emitted by `setAwdlSyncState` at `0xffffff800221e511+0x13d`; CW `+0xb5297` |
| 118 | `AWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `?` | CW `+0xb5557`; PM site without immediate (`0xffffff80021cc6a6+0x36`) |
| 119 | `AWDL_SERVICE_PARAMS` | `?` (CW only) | CW `+0xb5607` |
| 120 | `AWDL_PEER_SERVICE_REQUEST` | `?` (CW only) | CW `+0xb53f7` |
| 122 | `AWDL_ELECTION_ID` | PM | `0xffffff800221c8c5+0x2d` (SET), `0xffffff800221f477+0x31` (GET); CW `+0xb54a7` |
| 124 | `AWDL_GUARD_TIME` | `?` | not observed |
| 125 | `AWDL_BSSID` | PM | `0xffffff800221caf0+0xae` (SET); CW `+0xb56b7` |
| 126 | `AWDL_ELECTION_METRIC` | PM | `0xffffff800221f559+0x31` (GET); CW `+0xb58c7` (SET PM `0xffffff80021cb371+0x2f` without 126, `?`) |
| 127 | `AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` | `?` (CW only) | CW `+0xb5977` |
| 128 | `AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT` | PM | `0xffffff800222f105+0xef` and `0xffffff800222f3dc+0xf2` (RTG stats); CW `+0xb5ad7` |
| 129 | `AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | PM | `0xffffff800221d122+0x246` (SET), `0xffffff800221f834+0x78` (GET); CW `+0xb5e47` |
| 131 | `AWDL_OUI` | PM | `0xffffff800221d289+0x2d` (SET); CW `+0xb5ef7` |
| 132 | `AWDL_MASTER_CHANNEL` | PM | `0xffffff800221d584+0xa6` (SET), `0xffffff800221fb23+0x2d` (GET); CW `+0xb5d97` |
| 133 | `AWDL_TOP_MASTER` | `?` | not observed |
| 134 | `AWDL_SYNC_STATE` | PM | `0xffffff800221e511+0x13d` (SET), `0xffffff800221fd72+0x2a` (GET); CW `+0xb5ce7` |
| 135 | `AWDL_ELECTION_RSSI_THRESHOLDS` | PM | `0xffffff800221e355+0x61` (SET), `0xffffff800221fd32+0x24` (GET), `0xffffff80021e1027+0xc9` (`determineAndSetEdgeRssiThreshold`); CW `+0xb5c37` |
| 136 | `AWDL_PRESENCE_MODE` | PM | `0xffffff80021cc5f6+0x50` (SET), `0xffffff800221fdea+0x2a` (GET); CW `+0xb5fa7` |
| 138 | `AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME` | PM | `0xffffff800221e8e3+0x2d` (SET); CW `+0xb6107` |
| 139 | `AWDL_MASTER_MODE_SYNC_FRAME_PERIOD` | `?` | not observed (candidate `setSyncFramePeriod`, `0xffffff80021ed0e7+0xc3` without immediate) |
| 148 | `AWDL_STRATEGY` | `?` (CW only) | CW `+0xaa3a7`/`+0x338293` |
| 155 | `AWDL_ELECTION_PARAMS` | PM | `0xffffff800221cba2+0x3c` (SET), `0xffffff800221f6b9+0x2d` (GET); CW `+0xaa877` |
| 163 | `AWDL_SOCIAL_TIME_SLOTS` | `?` (CW only) | CW `+0xabb07` |
| 164 | `AWDL_PEER_TRAFFIC_REGISTRATION` | **airportd** + PM | ad `0x1000a0aea`/`0x1000a10ea` (logs `0x10013d8ca`/`0x10013da02`); CW `+0xabbb7`; PM `0xffffff80022059f9+0x7b` (CarPlay), `0xffffff8002205e16+0x282` (assisted discovery), `0xffffff800220608a+0x7c` (ranging), `0xffffff80022067ae+0x1a6` (migration), `0xffffff80021f9c0d+0x13b` (NSteer) |
| 167 | `AWDL_FORCED_ROAM_CONFIG` | PM `?` | CW `+0xb6687`; `forceRoamToPeersAP` `0xffffff80021f96e1+0x235` without 167 in the window |
| 201–205 | BTLE/secondary master | CW; PM 205 | CW `+0xb962b`/`+0xb9a8c`/`+0xb82f7`/`+0xb83a7`/`+0xb8457`; PM 205 `0xffffff800221d3fe+0xea` (SET), `0xffffff800221fc1f+0x4d` (GET) |
| 208 | `AWDL_AF_TX_MODE` | PM | `0xffffff800221c38d+0x5f` (SET), `0xffffff800221c27b+0x5b` (GET); CW `+0xb8667` |
| 213 | `AWDL_MIN_RATE` | PM | `0xffffff80022206a4+0x46` (SET); CW `+0xb9ee7` |
| 217 | `AWDL_OPER_MODE` | `?` (CW only) | CW `+0xbac27` |
| 219 | `AWDL_ENCRYPTION_KEYS` | PM | `0xffffff800221d66c+0x2e` (SET); CW `+0xb2d11` |
| 220 | `AWDL_ENCRYPTION_TYPE` | PM | `0xffffff800221d71c+0x2a` (SET), `0xffffff800221d79e+0x2a` (GET); CW `+0xbb1ab` |
| 226 | (unnamed in the kernel table; txcal) | PM | `0xffffff800221c139+0x2d` (SET), `0xffffff800221c0b4+0x2a` (GET) |
| 229 | `AWDL_AWDL_ADVERTISERS` | PM | `0xffffff800221c1c8+0x2a` (SET); CW `+0xbd101` |
| 233 | `AWDL_BLOCK_SET_COMMANDS` | PM | `0xffffff800221e290+0x2a` (SET), `0xffffff800221dd48+0x2a` (GET); CW `+0xbd417` |
| 247 | `AWDL_DEV_STATS` | PM | `0xffffff800221dc20+0x24` (GET) |
| 253 | `AWDL_NEARBY_LOG_TRIGGER` | `?` (CW only) | CW `+0xbf50b` |
| 338 | `AWDL_CCA` | PM | `0xffffff800221dcf2+0xbc` (GET) |
| 344 | `LLW_PARAMS` | PM | `0xffffff800221e0b0+0x2a` (SET), `0xffffff800221e02e+0x2a` (GET); CW `+0x57925` |
| 351 | `AWDL_SIDECAR_DIAGNOSTICS` | `?` (CW only) | CW `+0x57a56` |
| 411 | `TX_RATE` (re-emits to PeerManager) | PM | `0xffffff800221e17f+0x6d` (SET); CW `+0x58a9b` |
| 422 | `6G_MODE` (generic, from the AWDL manager) | PM `setInfra6GMode` | `0xffffff8002238f51+0xa7` |
| 432 | (unnamed in the kernel table) | PM `setAwdlConfig` | `0xffffff800221c9ea+0x2a` |
| 456/467 | (unnamed; P2P/RTG metrics) | PM | `0xffffff800222a34a+0xe`, `0xffffff800222f105+0xef` |

The large GET-only types (`AWDL_PEER_DATABASE` 203, `AWDL_STATISTICS` 223,
`AWDL_LOW_LATENCY_STATISTICS` 343, `AWDL_AIRPLAY_STATISTICS` 370, `AWDL_CTL` 162) show no
own emitters: they are read from userspace with generic `Apple80211Get`/`CopyValue` (airportd:
GET 223 at `sub_1000AE685` `0x1000ae685`) or they are internal manager consumption; `?` for the
rest.

## 9. Method and limits

- Byte sweeps (constants) over the whole cache and the `.kc`: `sweep.py`, `kc_sweep.py`,
  `kc_types.py` in `Build/ida_awdl/task_awdl_issuers/` (outputs `kc_types_out.txt`,
  `kc_issuers_full.txt`).
- Symtab of `IO80211.framework` extracted from the `__LINKEDIT` pool (`extract_syms.py` →
  `io80211_exports.txt`, 1275 symbols); `Apple80211.framework` (`13 AC* symbols`) and
  `CoreWiFi.framework` (969 symbols, no AWDL names because they are ObjC methods) with the same
  method. The extraction of `WiFiPeerToPeer.framework` failed (the pool of subcache `.02` could
  not be anchored by content); its strings are used as class evidence (§5.2).
- Daemons: `x86_64` copies (`bin/`) analyzed with IDA headless (`probe1.py`, `probe2.py` over
  `airportd_copy.i64`; `probe_all.py`/`probe3.py`/`probe4.py` over the raw `__TEXT` of
  `IO80211.framework` in `io80211fat.macho` — the synthetic Mach-O load with the cache vmaddrs
  was not accepted by the loader and a binary load with a `0x4000` fat header was used; the
  function bounds of the raw analysis do not match the symtab 1:1, which is why some sites cite
  the containing function of the analysis and not the symbol).
- Remain as `?`: the specific selector of the `IO80211APIUserClient` that reaches
  `performGatedCommandIOUC`; which process invokes each CoreWiFi typed setter; the PM
  sites where the immediate window did not isolate the type with confidence; the types
  124/133/139/162 (no emitter observed on either side); and the full semantics of 432.
- Hardware observation of when each request is emitted remains pending (the «when» of
  §4/§6.2 is inferred from the name and the log strings of the emitting functions).
