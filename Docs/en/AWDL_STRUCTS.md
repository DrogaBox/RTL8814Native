# C layouts of the apple80211 payloads an AWDL interface must answer (macOS 26, kernel 25.6.0)

Recovery of the exact layouts of the data structs of the apple80211 requests that reach
the AWDL surface: the struct name (from the mangled signature of the Broadcom method of the slot), the
total size (from the `len == N` check of the modern kernel handler and from the buffer size the
bridge passes to WCL), and each field with offset, width, type and meaning. It closes the pending item of
[`AWDL_APPLE80211_STRUCTS.md`](AWDL_APPLE80211_STRUCTS.md), which documented 42 type names without
layouts.

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`) and `wifip2pd.x86_64`
(md5 `a120cfccfa2783303604ce5be9e1763a`), decompiled with IDA 9.4 headless (`idat -A -S`) over
copies of the IDBs in `Build/ida_awdl/task_awdl_structs/kc/` and `/p2p/` (dumps: `kc_proxi.txt`,
`kc_pm_get.txt`, `kc_pm_set.txt`, `kc_handlers.txt`, `kc_impl.txt`, `kc_impl2.txt`, `kc_tables.txt`,
`p2p_decomp.txt`, `p2p_disasm_key.txt`). Addresses = VA of the indicated binary (kernel unless
marked `w2p`). Unverified items go as `?`.

Three sources per struct when they exist: (a) the modern handler of `IO80211Family` that validates the
length and does the copyback, (b) the `AppleBCMWLANProximityInterface` override that fills or consumes the
struct (vtable slot, vtable `0xffffff80017f5b78`), (c) the equivalent getter/setter of
`IO80211AWDLPeerManager` (modern path), and (d) when applicable, the code of `wifip2pd` assembling or
parsing the buffer and the Swift field names of the type dump of the daemon
(`Build/ida_awdl/awdl_callers_probe.log`). The field offsets of sections 4–6 come from the
writes/reads observed in the decompilations; a field without observed write goes as `?`.

## 1. The request: `apple80211req` (40 B)

| Offset | Width | Field | Meaning |
|---:|---:|---|---|
| +0x00 | 16 | `ifname[16]` | BSD name of the destination interface (awdl0 for the AWDL path) |
| +0x10 | 4 | `req_type` u32 | request type (1..586) |
| +0x14 | 4 | `val` u32 | auxiliary field (0 in the observed requests of wifip2pd) |
| +0x18 | 4 | `req_len` u32 | length of the data buffer |
| +0x20 | 8 | `req_data` ptr | pointer to the data buffer |

Evidence: the dispatcher reads the type at `+0x10` and validates `[1, 586]`
(`apple80211RouteExternal`, `0xffffff800236cd2e`: `v13 = *(_DWORD *)(a4 + 16)`, branches
`gGetHandlerTable[type-1]` / `gSetHandlerTable[type-1]`, bases `0xffffff800245eb60` /
`0xffffff800245fef0`); wifip2pd assembles the request with type at `+16`, length at `+24` and pointer at
`+32` (`sub_100046D30`, `w2p 0x100046d5e`: `*(_DWORD *)(a4 + 16) = 97`,
`*(_DWORD *)(a4 + 24) = len`, `*(_QWORD *)(a4 + 32) = ptr`; same in `sub_10004B300`:
`LODWORD(v66) = 293`, `DWORD2(v66) = 24`, `v67 = v61`, `w2p 0x10004b3d2`); the handlers read
`req_len` at `+0x18` and `req_data` at `+0x20` (all of the decompilations in
`Build/ida_awdl/task_awdl_structs/kc_handlers.txt`). It matches the raw ioctl buffer of
[`AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md).

## 2. Order of the requests of wifip2pd and steady state

The inventory of call sites is closed (`AWDL_WIFIP2PD_POST_CREATE.md` §2.0-2.1): `Apple80211RawGet`
has exactly 4 call sites (three of type 12 and one of type 97) and `Apple80211RawSet` 2 (94 and
293); `Apple80211Get`/`Apple80211Set` do not exist in the daemon. The static order of the requests
over the AWDL interface:

| # | Request | len | Emitter (w2p VA) | Note |
|---:|---|---:|---|---|
| 1 | `RawGet` 12 `CARD_CAPABILITIES` | 21 | `0x10004811d`, `0x100048260`, `0x1000483a8` (in `sub_100047660`) | 21 B buffer at `var_70+4`; AWDL branch (case 1 of switch `0x100047e85` → `0x100048302`): bit 0 of `data[4]` (`0x1000483b5`: `test byte [rbp+var_70+8], 1`); NAN branches (cases 2/3): bit 0 of `data[9]` (`0x10004812a`, `0x10004826d`: `test byte [rbp+var_70+0Dh], 1`) |
| 2 | `RawSet` 94 `VIRTUAL_IF_CREATE` | 32 | `0x100048d95` (payload built at `0x100048c41`–`0x100048d17`) | role 6 (AWDL) at payload+0x0C; response name at payload+16 |
| 3 | 30 s wait | — | wait `0x1000491a8` | not a request; the IORegistry notification releases it (`AWDL_WIFIP2PD_POST_CREATE.md` §1) |
| 4 | `RawGet` 97 `VIRTUAL_IF_PARENT` | strlen(awdl0) | `sub_100046D30` at `0x100046db9` | answer = parent name copied without NUL |
| 5 | `RawSet` 293 `COMPANION_SKYWALK_IF_CREATE` | 24 | `sub_10004B300` at `0x10004b3e6` | creates `llw0`; response name at payload+8 (see §3.4) |
| 6 | ifnet ioctls | — | `sub_100046A30` + wrappers | `SIOCGIFMEDIA` (802.11 gate), `SIOCSIFFLAGS`, `SIOCSIFLLADDR`, `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI` |
| 7 | ioctls `'i'` 130/131 with type 7708 | — | `sub_100058420`/`sub_1000586D0` | not reachable through the apple80211 dispatch (`AWDL_WIFIP2PD_POST_CREATE.md` §2.5) |

In steady state the daemon emits no additional `RawGet`/`RawSet` (the 6 listed call sites
are all of them): it only monitors events, via `Apple80211StartMonitoringEvent` ×16 (branch
role 0 + NSObject queue, `sub_100041EB0`, ids 1, 11, 59, 2, 3, 125, 126, 127, 9, 66, 74, 78, 82, 215,
55, 160; `AWDL_WIFIP2PD_P2P_EVENTS.md` §1) or via `Apple80211RawP2PEventMonitoringInit`
(`sub_100041F20` → `0x100041fc2`) for the P2P/AWDL route. Events are not requests and require no
response structs.

## 3. Structs of the life cycle (the ones wifip2pd sends)

### 3.1 `apple80211_capability_data` — type 12 GET, 28 B, len `!= 0`

| Offset | Width | Field | Meaning |
|---:|---:|---|---|
| +0x00 | 4 | `version` u32 | 1 in the Broadcom implementation |
| +0x04 | 24 | `flags[24]` u8×24 | capability bit map (193 bits used in Broadcom) |

GET handler `0xffffff8002279e89`: requires `len != 0` and `data != NULL`, calls WCL with an internal
28 B buffer (`sendIOUCToWcl(glue, 0xC02869C9, 12, buf, 0x1C)`) and does the copyback of
`min(req_len, 21)` bytes **from `buf+4`** (the flags without the version):
`memmove(req_data, &v13[1], min(req_len, 21))`. Broadcom implementation
(`AppleBCMWLANCore::getCARD_CAPABILITIES`, `0xffffff8001661076`): writes `version = 1` at +0
(`*(_DWORD *)a3 = 1`), constant value 0xBF at +4 (`*(_WORD *)(a3 + 4) = -6545`), 0x40 at +9, 0x41
at +12 (`*(_WORD *)(a3 + 12) = 513`), 0x61 at +6 and dozens of bits conditioned on
`featureFlagIsBitSet(N)` in bytes +6..+24.

**The bit wifip2pd requires for AWDL is bit 0 of byte +0x08 of the struct** (flags[4]). The
buffer of the request is `var_70+4` (`lea r12, [rbp+var_70+4]` at `w2p 0x100048090`, data
pointer in `var_80`), so `data[k]` is `var_70+4+k` and, since the 21 B copyback starts at
`buf+4` of the struct, `data[k]` is byte `4+k` of the struct. The AWDL branch of the switch by role code
(`0x100047e85`, case 1 → `0x100048302`) does its `RawGet` at `0x1000483a8` and tests
`test byte [rbp+var_70+8], 1` (`w2p 0x1000483b5`) = `data[4]` = **byte +0x08**. The branches of
codes 2 and 3 (Wi-Fi Aware) do the `RawGet` at `0x10004811d`/`0x100048260` and test
`test byte [rbp+var_70+0Dh], 1` (`w2p 0x10004812a`/`0x10004826d`) = `data[9]` = **byte +0x0D**,
which is the NAN gate. In Broadcom byte +0x0D bit 0 is set when `featureFlagIsBitSet(this, 6)
!= 0` (`0xffffff8001661076`: `if (featureFlagIsBitSet(this, 6) != 0) *(_BYTE *)(a3 + 13) |= 1u;`);
the Broadcom condition setting byte +0x08 bit 0 remains `?`.
For a custom kext: answering GET 12 with `flags[4] |= 0x01` (byte +0x08) is the minimum gate
for AWDL; `flags[9] |= 0x01` (byte +0x0D) would also open the Wi-Fi Aware one.

### 3.2 `apple80211_virt_if_create_data` — type 94 SET, 32 B, len `== 32`

| Offset | Width | Field | Meaning |
|---:|---:|---|---|
| +0x00 | 4 | u32 | sent by wifip2pd (`mov [rbp+var_C0], r14d`, `w2p 0x100048c5f`); no observed read in Broadcom `?` |
| +0x04 | 6 | `mac[6]` | requested MAC for the interface (wifip2pd writes it byte by byte at `w2p 0x100048cd9`–`0x100048d17`) |
| +0x0A | 2 | pad | 0 (the buffer is zeroed with `movaps` at `w2p 0x100048c44`) |
| +0x0C | 4 | `role` u32 | virtual interface role: 6 = AWDL (`w2p 0x100048cc8`), 9 = `?` (`0x100048c66`), 0xA = `?` (`0x100048c97`) |
| +0x10 | 16 | `ifname[16]` | **output**: BSD name created, written by the driver |

SET handler `0xffffff80022886a8`: `len == 32`, `data != NULL`, WCL route
(`sendIOUCToWcl(glue, 0x802869C8, 94, data, 32)`) and, if it is not consumed, bridge
`apple80211setVIRTUAL_IF_CREATE` (cast to `IO80211InfraProtocol`, slot +0x1160;
`AWDL_APPLE80211_DISPATCH.md` §4). Broadcom implementation
(`AppleBCMWLANCore::setVIRTUAL_IF_CREATE`, `0xffffff80016788f4`): reads `role` at `+0x0C`
(`v4 = *(_DWORD *)(a2 + 12)`; 6 → Proximity/AWDL interface, 7 → APSTA, 8..10 →
`createNANInterface`), passes `a2+4` as `ether_addr*` of the MAC to
`AppleBCMWLANIO80211APSTAInterface::withOptions` (and the equivalent in branch 6 via
`createProxInterface`), and on success it clears and writes the output name:
`*(_QWORD *)(a2 + 24) = 0; *(_QWORD *)(a2 + 16) = 0; strlcpy((char *)(a2 + 16), bsdname, 0x10)`.
wifip2pd reads the name from payload+16 and continues (`AWDL_WIFIP2PD_POST_CREATE.md` §1.4).

### 3.3 `VIRTUAL_IF_PARENT` — type 97 GET (no struct: answer = C-string)

Handler `getVIRTUAL_IF_PARENT` (`0xffffff800227b7ff`) uses no struct: it gets the parent of the
virtual interface (`safeMetaCast` to `IO80211VirtualInterface`), takes the BSD name of the parent
(`IO80211SkywalkInterface::getBSDName`) and requires `req_len >= strlen(name)`; it copies `strlen(name)`
bytes **without NUL** into `req_data` (`memmove(*(void **)(a4 + 32), BSDName, v10)`). wifip2pd sends
`len = strlen(awdl0)` and parses looking for `'\0'` capped at the offered length
(`sub_100046D30`, `w2p 0x100046dd8`). Expected answer over awdl0: `en0` (the parent name).

### 3.4 `apple80211_companion_skywalk_if_create` — type 293 SET, 24 B, len `== 24`

| Offset | Width | Field | Meaning |
|---:|---:|---|---|
| +0x00 | 4 | `code` u32 | companion code that wifip2pd propagates from its caller (`v61[0] = a5`, `w2p 0x10004b34d`); the kernel never reads it and its value on the AWDL path remains `?`; Swift auxiliary type `apple80211_companion_skywalk_if_create_code` = `UInt32` rawValue (`awdl_callers_probe.log:1666`) |
| +0x04 | 4 | `op` u32 | 1 = create (`v61[1] = 1`, `w2p 0x10004b352`); 0 = destroy; any other value = no-op returning 0 |
| +0x08 | 16 | `name[16]` | **output**: BSD name of the companion interface; written only on creation success, also when the interface already existed |

SET handler `0xffffff800228b4b8`: `len == 24`, `data != NULL`, WCL route with 0x18 and fallback to the
bridge. Broadcom implementation (`AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE`,
`0xffffff8001722ef2`; semantics verified in
[`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §3.2): reads `op` at `+0x04`
(`v3 = *(_DWORD *)(a2 + 4)`) and never reads `code` at `+0x00`. `op == 1` creates the low-latency Skywalk
interface via `createSkywalkInterface` (site `0xffffff8001723068`) and on success writes
the name at payload+8; if the companion already exists (pointer at ivars+120), it likewise writes the
existing BSD name at payload+8 and returns 0 with the log
"AWDL companion skywalk interface %s already exists for %s"; if the creation fails, it returns the
error without writing payload+8. `op == 0` destroys via `destroySkywalkInterface` (site
`0xffffff8001722fe9`); without a companion it returns `0xE00002C0` with the log
"Deletion failed, NAN companion skywalk interface does not exists for %s".
**Any other value of `op` is a no-op returning 0.** The name is written only on
creation success: `__strncpy_chk((void *)(a2 + 8), bsdname, 0x10)`. **The response name
is returned at payload+8, not in `req.ifname`**: wifip2pd reads it with `String.init(cString:)` over
the same offset (`v62` at `rbp-0x70` = `v61+8` after `v61` at `rbp-0x78`, `w2p 0x10004b40a`) and
validates with `if_nametoindex("llw0")` (immediate `813132908` = `0x30776C6C`, `w2p 0x10004b814`) if
the role of the handle is 1, or `if_nametoindex("llw1")` (`829910124` = `0x31776C6C`,
`w2p 0x10004b862`) if it is 2. This corrects the detail of `AWDL_WIFIP2PD_POST_CREATE.md` §2.4 that
attributed the answer to the `ifname` field of the request (over awdl0 goes the parent name as the
address of the request; the created name comes back at payload+8). The `ifname` field of the request
(req+0) carries `awdl0`.

## 4. AWDL surface with a vtable slot: structs per type

Summary table. "len" = equality required by the modern handler (read from
`Build/ida_awdl/task_awdl_structs/kc_handlers.txt`; matches `AWDL_APPLE80211_DISPATCH.md` §4);
"—" = no row in `gGet/gSetHandlerTable` (entry 0 verified in `kc_tables.txt` for types
143, 149, 151, 158, 225, 262, 263, 341, 342, 414, 418, 432, 467): those requests return 102 through the
modern path and are only reachable via the internal glue route
(`AWDL_APPLE80211_DISPATCH.md` §6).

| GET/SET type | IOC | struct | len GET/SET | GET/SET slot (vptr) | GET method Broadcom | PM GET/SET |
|---|---|---|---|---|---|---|
| 1/1 | SSID | `apple80211_ssid_data` | special copyback / copyback | +0xFC8 / +0x10A0 | `getSSID` `0xffffff800171a70e` | — |
| 4/4 | CHANNEL | `apple80211_channel_data` | ==16 / !=0 | +0xFD0 / +0x10A8 | stub `0xffffff800171a752` | — |
| 9/— | BSSID | `apple80211_bssid_data` | >=6 / — | +0xFD8 / — | error stub `0xffffff800171a702` | — |
| 13/— | STATE | `apple80211_state_data` | ==8 / — | +0xFE0 / — | `getSTATE` `0xffffff8001719944` | — |
| 116/116 | AWDL_SYNC_PARAMS | `apple80211_awdl_sync_params` | ==24 / ==24 | +0xFE8 / +0x10B0 | `0xffffff800171b1b8` | `0xffffff800221ee92` / `0xffffff800221b73c` |
| 117/117 | AWDL_SYNC_ENABLED | `apple80211_awdl_sync_enabled` | ==12 / ==12 | +0xFF0 / +0x10B8 | `0xffffff800171b092` | `0xffffff800221f0f4` / `0xffffff800221b842` |
| 118/118 | AWDL_EXTENSION_STATE_MACHINE_PARAMETERS | `apple80211_awdl_extension_state_machine_parameter` | ==20 / ==20 | +0xFF8 / +0x10C0 | `0xffffff800171be78` | `0xffffff800221ed76` / `0xffffff800221bc44` |
| 122/122 | AWDL_ELECTION_ID | `apple80211_awdl_election_id` | ==8 / ==8 | +0x1000 / +0x10C8 | `0xffffff800171c456` | `0xffffff800221f446` / `0xffffff800221c898` |
| —/125 | AWDL_BSSID | `apple80211_awdl_bssid` | — / ==16 | — / +0x10D0 | — | `0xffffff800221f654` / `0xffffff800221ca42` |
| 126/126 | AWDL_ELECTION_METRIC | `apple80211_awdl_election_metric` | ==12 / ==12 | +0x1008 / +0x10D8 | `0xffffff800171cd70` | `0xffffff800221f528` / `0xffffff80021cb342` |
| 129/129 | AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE | `apple80211_awdl_sync_channel_sequence` | ==400 / ==400 | +0x1010 / +0x10E0 | `0xffffff800172421a` | `0xffffff800221f7bc` / `0xffffff800221cedc` |
| 130/— | PEER_CACHE_MAXIMUM_SIZE | `apple80211_peer_cache_maximum_size` | ==8 / — | +0x1018 / — | `0xffffff80017233b6` | `0xffffff800221f62c` (getAwdlMaxPeers) |
| 132/132 | AWDL_MASTER_CHANNEL | `apple80211_awdl_master_channel` | ==8 / ==8 | +0x1020 / +0x10E8 | `0xffffff800171f1e6` | `0xffffff800221faf6` / `0xffffff800221d4de` |
| 134/134 | AWDL_SYNC_STATE | `apple80211_awdl_sync_state` | ==24 / ==24 | +0x1028 / +0x10F0 | `0xffffff800171b528` | `0xffffff800221fd48` / `0xffffff800221e3d4` |
| 135/135 | AWDL_ELECTION_RSSI_THRESHOLDS | `apple80211_awdl_election_rssi_thresholds` | ==20 / ==20 | +0x1030 / +0x10F8 | `0xffffff800171d2e4` | — / `0xffffff800221e2f4` |
| 136/136 | AWDL_PRESENCE_MODE | `apple80211_awdl_presence_mode` | ==8 / ==8 | +0x1038 / +0x1100 | `0xffffff800171bd12` | `0xffffff800221fdc0` / `0xffffff800221e806` |
| —/143 | PEER_CACHE_CONTROL | `apple80211_peer_cache_control` | — / — | — / +0x1108 | — | — |
| —/149 | AWDL_OOB_REQUEST | `apple80211_awdl_oob_request` | — / — | — / +0x1110 | — | — |
| —/151 | AWDL_SYNC_FRAME_TEMPLATE | `apple80211_awdl_sync_frame_template` | — / — | — / +0x1118 | — | — |
| 155/155 | AWDL_ELECTION_PARAMS | `apple80211_awdl_election_params` | ==56 / ==56 | +0x1040 / +0x1120 | `0xffffff800171ce5a` | `0xffffff800221f68c` / `0xffffff800221cb66` |
| 158/— | AWDL_DEVICE_CAPABILITIES | `apple80211_awdl_device_capabilities` | — / — | +0x1048 / — | `0xffffff800171be38` | — |
| —/164 | AWDL_PEER_TRAFFIC_REGISTRATION | `apple80211_awdl_peer_traffic_registration` | — / ==184 | — / +0x1128 | — | `0xffffff800220d6ae` / `0xffffff800220946a` |
| 205/205 | AWDL_SECONDARY_MASTER_CHANNEL | `apple80211_awdl_secondary_master_channel` | ==8 / ==8 | +0x1050 / +0x1130 | `0xffffff800171ec10` | `0xffffff800221fbd2` / `0xffffff800221d314` |
| 208/208 | AWDL_AF_TX_MODE | `apple80211_awdl_af_tx_mode` | ==8 / ==8 | +0x1058 / +0x1138 | `0xffffff8001719b62` | `0xffffff800221c220` / `0xffffff800221c32e` |
| 213/213 | AWDL_MIN_RATE | `apple80211_awdl_min_rate` | ==8 / ==8 | +0x1060 / +0x1140 | `0xffffff8001728a3c` | `0xffffff8002220748` / `0xffffff800222065e` |
| —/219 | AWDL_ENCRYPTION_KEYS | `apple80211_key` | — / ==148 | — / +0x1148 | — | — / `0xffffff800221d63e` |
| 220/220 | AWDL_ENCRYPTION_TYPE | `apple80211_encrypt` | ==8 / ==8 | +0x1068 / +0x1150 | `0xffffff800171f900` | `0xffffff800221d774` / `0xffffff800221d6f2` |
| —/225 | AWDL_OOB_AUTO_REQUEST | `apple80211_awdl_oob_request` | — / — | — / +0x1158 | — | — |
| 226/226 | AWDL_TXCAL_PERIOD | `apple80211_awdl_txcal_period` | — / — | +0x1070 / +0x1160 | `0xffffff8001728cae` | `0xffffff800221c08a` / `0xffffff800221c10c` (internal route type 226) |
| —/229 | AWDL_AWDL_ADVERTISERS | `awdl_peer_advert_add` | — / ==44 | — / +0x1168 | — | — / `0xffffff800221c19e` |
| 247/— | AWDL_DEV_STATS | `apple80211_awdl_dev_stats` | ==124 / — | +0x1078 / — | `0xffffff800171b698` | `0xffffff800221dbfc` (internal route 247) |
| 262/262 | AWDL_DFSP_CONFIG | `apple80211_awdl_dfsp_config` | — / — | +0x1080 / +0x1170 | `0xffffff8001728fba` | — |
| —/263 | AWDL_DFSP_UCSA_CONFIG | `apple80211_awdl_dfsp_ucsa_config` | — / — | — / +0x1178 | — | — |
| 293/293 | COMPANION_SKYWALK_IF_CREATE | `apple80211_companion_skywalk_if_create` | — / ==24 | — / +0x1180 | — | — |
| 338/— | AWDL_CCA | `apple80211_awdl_cca` | ==392 / — | +0x1088 / — | `0xffffff800171ba8e` | `0xffffff800221dc36` |
| 341/— | AWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES | `apple80211_awdl_cap` | — / — | +0x1090 / — | `0xffffff800171ed38` | — |
| —/342 | AWDL_OOB_SEC_PAYLOAD_REQUEST | `apple80211_awdl_af_sec_payload_request` | — / — | — / +0x1188 | — | — |
| 344/344 | LLW_PARAMS | `apple80211_llw_params` | ==44 / ==44 | +0x1098 / +0x1190 | `0xffffff800172753e` | `0xffffff800221e004` / `0xffffff800221e086` |
| —/411 | TX_RATE | `apple80211_tx_rate` | — / ==16 | — / +0x1198 | — | — / `0xffffff800221e112` |
| —/414 | P2P_COEX_PROTECTION | `apple80211_p2p_coex_protection` | — / — | — / +0x11A0 | — | — |
| —/418 | PSF_DWELL | `apple80211_awdl_psf_dwell_params` | — / — | — / +0x11A8 | — | — |
| —/432 | AWDL_CONFIG | `apple80211_awdl_config_request` | — / — | — / +0x11B0 | — | — / `0xffffff800221c9c0` (internal route 432) |
| —/467 | AWDL_RTG_PEER_STATS_CONFIG | `apple80211_rtg_peer_stats_cfg` | — / — | — / +0x11B8 | — | — |

The general types 1/4/9/13 reach the slots via the `safeMetaCast` cascade of the bridges
(`AWDL_APPLE80211_DISPATCH.md` §3); the rest, via the specific bridge with cast to
`IO80211AWDLProtocol`.

### 4.1 Observed fields per struct

The following offsets come from writes/reads over the struct parameter in the
cited decompilations. "—" in meaning = not determined.

**`apple80211_ssid_data`** (40 B; GET handler 1 `0xffffff8002279821`, WCL buffer 0x28, copyback
`min(req_len, ssid_len)`)
- `+0x00` u32 `version` — not written by the Broadcom method `?` (the handler copies it as-is from the internal buffer)
- `+0x04` u32 `ssid_len` — length of the SSID (`*(_DWORD *)(a2 + 4) = strlen(...)`, `0xffffff800171a70e`)
- `+0x08` 32 `ssid[32]` — SSID + NUL; the Broadcom AWDL method returns the fixed string "awdl"
  (bytes read at `0xffffff8001797376`: `awdl\0`), `getSSID` `0xffffff800171a70e`
- `setSSID` Broadcom = stub returning 102 (`0xffffff8001715a70`); SET handler 1
  (`0xffffff8002286f15`) copies `req_len` bytes of the request into the bridge buffer.

**`apple80211_channel_data`** (16 B; GET handler 4 `0xffffff80022799c1` len==16; SET 4
`0xffffff80022871b1` len!=0)
- `+0x00` u32 `version` — not filled by Broadcom AWDL `?`
- `+0x04` u32 `channel.version` — `apple80211_channel` (Swift: `{version, channel, flags}`,
  metadata `w2p $s3__C18apple80211_channelVMa 0x10000f5f0`)
- `+0x08` u32 `channel.channel` — Proxi `setCHANNEL` reads it from there (`*(_DWORD *)(a2 + 8)`,
  `0xffffff800171e8d8`); the GET of the main driver fills it (`ChanSpecConvToApple80211Channel(..., a2 + 4)`,
  `AppleBCMWLANCore::getCHANNEL 0xffffff8001660eb2`)
- `+0x0C` u32 `channel.flags`
- `getCHANNEL` of the ProximityInterface is a stub returning 0 without touching the struct
  (`0xffffff800171a752`).

**`apple80211_bssid_data`** (12 B; GET handler 9 `0xffffff8002279ce7`, len >= 6, copyback of
`min(req_len, 6)` bytes from struct+4 — only the MAC)
- `+0x00` u32 `version`
- `+0x04` 6 `bssid` ether_addr — `AppleBCMWLANCore::getBSSID 0xffffff800166105a` writes there
  (`IO80211BssManager::getCurrentBSSID(..., a2 + 4)`)
- `+0x0A` 2 pad
- `getBSSID` of the ProximityInterface = stub returning `0xE00002C1` (`0xffffff800171a702`):
  GET 9 over awdl0 fails with the Broadcom implementation.

**`apple80211_state_data`** (8 B; GET handler 13 `0xffffff8002279fb1` len==8)
- `+0x00` u32 `version`
- `+0x04` u32 `state` — the Broadcom method writes 4 (`*(_DWORD *)(a2 + 4) = 4`,
  `getSTATE 0xffffff8001719944`, log `"getSTATE %u"`); value 4 `?` (probable operating state)

**`apple80211_awdl_sync_params`** (24 B; len ==24 in both directions, handlers
`0xffffff800227bbcd`/`0xffffff800228883b`)
- `+0x00` u32 `version` — the PM getter sets it to 0 (`*a2 = 0`, `0xffffff800221ee92`)
- `+0x04` u32 `aw_cmn_length` — check `== 0` → error "Invalid aw_cmn_length"
  (`getAWDL_SYNC_PARAMS 0xffffff800171b1b8`, log line 1802)
- `+0x08` u32 — unnamed; u16 value of the chip iovar extended to u32 (disassembly
  `0xffffff800171b395`–`0xffffff800171b3ee`: `movzx word [rbp+var_50]` → store into a2+8)
- `+0x0C` u32 `aw_ext_length` — check `== 0` → "Invalid aw_ext_length" (line 1811)
- `+0x10` u32 `action_frame_period` — check `== 0` → "Invalid action_frame_period" (line 1806)
- `+0x14` u32 — cache of Proxi ivar +0x29C (`a2[5] = v16[167]`; `setAWDL_SYNC_PARAMS
  0xffffff800171ddee` persists it from the request)
- The PM setter applies the convention "-1 = keep the current value" over +4..+16 and stores into
  its ivars +62940/+62944/+62948/+62952 (`setAwdlSyncParams 0xffffff800221b73c`); the PM getter
  dumps those same ivars and additionally re-routes the request to the driver
  (`apple80211RouteInternal(0xC02869C9, 116, ...)`, `getAwdlSyncParams 0xffffff800221ee92`).

**`apple80211_awdl_sync_enabled`** (12 B; len ==12, handlers `0xffffff800227bc2f`/`0xffffff800228889d`)
- `+0x00` u32 `version`
- `+0x04` u32 `flags`
- `+0x08` u32 `enabled` — the PM compares with bit 1 of ivar +16096
  (`getAwdlSyncEnabled 0xffffff800221f0f4`); Proxi writes `+8`
  (`getAWDL_SYNC_ENABLED 0xffffff800171b092`); names of the three fields from the Swift type dump
  (`awdl_callers_probe.log:1345-1348`)

**`apple80211_awdl_extension_state_machine_parameter`** (20 B; len ==20, handlers
`0xffffff800227bc91`/`0xffffff80022888ff`)
- `+0x00` u32 `version`
- `+0x04` u32 (effective u8 value) — PM ivar +17036 (`getAwdlExtensionParams 0xffffff800221ed76`:
  `v12[17036] = *((_BYTE *)a2 + 4)`)
- `+0x08` u32 (u8) — PM ivar +17035
- `+0x0C` u32 (u8) — PM ivar +17037
- `+0x10` u32 (u8) — PM ivar +17038
- The PM setter treats -1 as "keep" over the four (`setAwdlExtensionParams
  0xffffff800221bc44`).

**`apple80211_awdl_election_id`** (8 B; len ==8, handlers `0xffffff800227bd8a`/`0xffffff8002288a72`)
- `+0x00` u32 `version`
- `+0x04` u32 `election_id` — PM ivar +37259 (`getAwdlElectionId 0xffffff800221f446`:
  `*((_DWORD *)a2 + 1) = *(_DWORD *)(... + 37259)`); the effective value fits in u16 (the Proxi
  method copies only 16 bits: `*(_DWORD *)(a2 + 4) = *(unsigned __int16 *)&v4[1]`,
  `0xffffff800171c456`)

**`apple80211_awdl_bssid`** (16 B; len ==16 SET only, handler `0xffffff8002288b36`)
- `+0x00` u32 `version`
- `+0x04` 6 `bssid` ether_addr — compared with the current address (`memcmp(a2+4, ...)`,
  `setAwdlBssid 0xffffff800221ca42`) and logged byte by byte by Proxi
  (`setAWDL_BSSID 0xffffff800172181a`, bytes +4..+9)
- `+0x0A` 6 — second 6 B MAC compared/logged by the PM (`memcmp(__s1, a2 + 10, 6)`,
  `0xffffff800221ca42`); meaning `?` (unnamed in logs)
- `+0x10` pad

**`apple80211_awdl_election_metric`** (12 B; len ==12, handlers `0xffffff800227be4e`/`0xffffff8002288b98`)
- `+0x00` u32 `version`
- `+0x04` u32 `metric` — PM ivar +37255 (`getAwdlElectionMetric 0xffffff800221f528`);
  Proxi persists it in its ivar +563 (`setAWDL_ELECTION_METRIC 0xffffff800171cf76`)
- `+0x08` u8 `flag` — if != 0 the PM keeps its current metric instead of the request one
  (`setAwdlElectionMetric 0xffffff80021cb342`: `if (*(_BYTE *)(a2 + 8) != 0) a2[1] = ivar+37255`);
  name `?`
- `+0x09` 3 pad

**`apple80211_awdl_sync_channel_sequence`** (400 B; len ==400, handlers
`0xffffff800227bf70`/`0xffffff8002288caa`)
- `+0x00` u32 `version`
- `+0x04` u8 — `?`
- `+0x05` u8 `count` — number of channel entries; the PM validates the range
  (`setAwdlChannelSequence 0xffffff800221cedc`: `(unsigned __int8)(a2[5] - 33) > 0xDF` → error
  "AWDL channel sequence length invalid %u (max = 32)", log lines with `a2[5]`)
- `+0x06` u8 `dfs` — stored into PM ivar +42866 (`a2[6]`)
- `+0x07` u8 — PM ivar +42867 (`?`)
- `+0x08` u8 — PM ivar +42848 (`?`)
- `+0x09` u8 — PM ivar +42856 (`?`); when != 0 the PM additionally copies a u32 of the struct itself
  (`*((_DWORD *)a2 + 99) = *(_DWORD *)(v10 + 4)`, `getAwdlChannelSequence 0xffffff800221f7bc`)
- `+0x0A` 6 pad
- `+0x10` u16 `entries[]` — channel sequence; the PM prints them as `0x%04x` and does
  `memmove(pm + 8, a2, 0x190)` of the whole block (`setAwdlChannelSequence`); Proxi fills from
  `a2+16`/`a2+12` (`getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE 0xffffff800172421a`)
- `+0x18C` u32 — `?` (field at +396 touched by the PM getter)
- (400 = 0x190, the full size the PM persists)

**`apple80211_peer_cache_maximum_size`** (8 B; len ==8 GET, handler `0xffffff800227bfd5`)
- `+0x00` u32 `version`
- `+0x04` u32 `size` — `_maxPeerCacheSize` in the PM (`getAwdlMaxPeers 0xffffff800221f62c`);
  Proxi writes 0 (`getPEER_CACHE_MAXIMUM_SIZE 0xffffff80017233b6`)

**`apple80211_awdl_master_channel`** (8 B; len ==8, handlers `0xffffff800227c033`/`0xffffff8002288d0f`)
- `+0x00` u32 `version`
- `+0x04` u32 `channel` — PM ivars +17060 (get, `0xffffff800221faf6`) and +17040 (set,
  `0xffffff800221d4de`)

**`apple80211_awdl_sync_state`** (24 B; len ==24, handlers `0xffffff800227c111`/`0xffffff8002288e21`)
- `+0x00` u32 `version` — the PM setter forces it to 1 (`setAwdlSyncState 0xffffff800221e3d4`)
- `+0x04` u32 `state` — PM ivar +17028 (`getAwdlSyncState 0xffffff800221fd48`); the Proxi setter
  rejects unsupported modes (`setAWDL_SYNC_STATE 0xffffff800171e164`, "unsupported mode %u")
- `+0x08` u32 — written to 0 by Proxi get (`0xffffff800171b528`); `?`
- `+0x0C` u16 — PM ivar +37241 (`?`)
- `+0x10` u32 — read by Proxi set (`v8 = *(_DWORD *)(a2 + 16)`); `?`
- `+0x14` u8 — read by Proxi set (`v7 = *(_BYTE *)(a2 + 20)`); `?`
- `+0x15` 3 pad

**`apple80211_awdl_election_rssi_thresholds`** (20 B; len ==20, handlers
`0xffffff800227c1d5`/`0xffffff8002288e83`)
- `+0x00` u32 `version`
- `+0x04` s8 — threshold 1 (Proxi set copies it to ivar +567: `a2[4]` over `_BYTE*`, `0xffffff800171d16a`;
  Proxi get: sign-extend of a chip dword, `0xffffff800171d2e4`)
- `+0x08` s8 — threshold 2 (ivar +570)
- `+0x0C` s8 — threshold 3 (ivar +571)
- `+0x10` s8 — threshold 4 (ivar +569)
- `+0x11` 3 pad
- Per-threshold semantics `?` (four sbytes unnamed in logs).

**`apple80211_awdl_presence_mode`** (8 B; len ==8, handlers `0xffffff800227c261`/`0xffffff8002288ee5`)
- `+0x00` u32 `version`
- `+0x04` u32 `mode` — limited to <= 0x3F (`setAwdlPresenceMode 0xffffff800221e806`:
  `if (*((_DWORD *)a2 + 1) <= 0x3Fu)`); PM ivar +17032 (low u8)

**`apple80211_awdl_election_params`** (56 B; len ==56, handlers `0xffffff800227c387`/`0xffffff8002289140`)
- `+0x00` u32 `version` — the PM setter forces it to 1 (`setAwdlElectionParams 0xffffff800221cb66`)
- `+0x04` 12 — no observed write `?`
- `+0x10` u8 — from TLV `awdlElectionParamsTlv` byte 0 (Proxi get `0xffffff800171ce5a`)
- `+0x12` u16 — TLV bytes 1-2
- `+0x14` u8 — TLV byte 25
- `+0x16` u32 — TLV bytes 15-18
- `+0x1A` u16 — TLV bytes 19-20
- `+0x1C` u32 — TLV bytes 21-24
- `+0x20` u32 — TLV bytes 3-6
- `+0x24` u32 `election_id` — the PM compares it with its ivar +37259 (`getAwdlElectionParams`:
  `a2[9]` over `unsigned __int16*` = dword @36)
- `+0x28` 16 rest `?` (56 − 40)
- Field names `?` (TLV `awdlElectionParamsTlvV2` appears as a type in
  `___FUNCTION__._ZN22IO80211AWDLPeerManager26setElectionParamsToNANPeerEP23awdlElectionParamsTlvV2`,
  `syms.txt:2779`, without layouts).

**`apple80211_awdl_device_capabilities`** (size `?` >= 20; no len — type 158 without a table row)
- `+0x00` u32 `version` `?`
- `+0x08` u32 — constant 10 (`a2[2] = 10`, `getAWDL_DEVICE_CAPABILITIES 0xffffff800171be38`)
- `+0x0C` u32 (u16 value) — from chip state +142
- `+0x10` u32 (u16 value) — from chip state +140

**`apple80211_awdl_secondary_master_channel`** (8 B; len ==8, handlers
`0xffffff800227cc7b`/`0xffffff80022899d4`)
- `+0x00` u32 `version`
- `+0x04` u32 `channel` — PM ivar +17072 (get, `0xffffff800221fbd2`) / +17044 (set,
  `0xffffff800221d314`); effective value u8

**`apple80211_awdl_af_tx_mode`** (8 B; len ==8, handlers `0xffffff800227cdaa`/`0xffffff8002289a36`)
- `+0x00` u32 `version`
- `+0x04` u32 `mode` — PM ivar +37144 (`getAwdlAfTxMode 0xffffff800221c220`); Proxi writes 0 by
  default (`getAWDL_AF_TX_MODE 0xffffff8001719b62`)

**`apple80211_awdl_min_rate`** (8 B; len ==8, handlers `0xffffff800227ce86`/`0xffffff8002289a98`)
- `+0x00` u32 `version`
- `+0x04` u8 `rate` — PM ivar +42417 (`setAwdlMinRate 0xffffff800222065e`); Proxi reads it as u8
  (`setAWDL_MIN_RATE 0xffffff8001728a48`)
- `+0x05` 3 pad

**`apple80211_key`** (148 B; len ==148 SET only, handler `0xffffff8002289c8b`)
- Classic Apple80211 key layout; observed: the PM logs bytes +92..+97
  (`setAwdlKey 0xffffff800221d63e`) and Proxi converts it with `mapAppleKeyToBcomKey(dst, a2)` into a
  0xA4 buffer (`setAWDL_ENCRYPTION_KEYS 0xffffff800171f480`). Field breakdown `?` (without individual
  writes in the decompilations; the standard struct has version/length/index/data/RSC, but it was not verified here).

**`apple80211_encrypt`** (8 B; len ==8, handlers `0xffffff800227d210`/`0xffffff8002289cf0`)
- `+0x00` u32 `version`
- `+0x04` u32 `encrypt_type` — Proxi writes 0 by default (`getAWDL_ENCRYPTION_TYPE
  0xffffff800171f900`); `?` value catalog

**`apple80211_awdl_txcal_period`** (8 B; no len in modern tables — the route is the internal one of type
226, `setAwdlTxCalPeriod 0xffffff800221c10c` / `getAwdlTxCalPeriod 0xffffff800221c08a`)
- `+0x00` u32 `version`
- `+0x04` u32 `period` — `*((_DWORD *)a2 + 1)` logged by the PM setter

**`awdl_peer_advert_add`** (44 B; len ==44 SET only, handler `0xffffff8002289eb1`)
- `+0x00` u16 `aw_counter`
- `+0x02` u16 `tx_counter`
- `+0x04` u16 `tx_delay`
- `+0x06` u16 `period_tu`
- `+0x08` u16 `aw_cmn_length`
- `+0x0A` u16 `aw_ext_length`
- `+0x0C` u16 `aw_period`
- `+0x0E` i8 `rssi`
- `+0x0F` u8 `guard_time`
- `+0x10` u8 `presence_mode`
- `+0x11` u8 `age`
- `+0x12` u8 `dist_top`
- `+0x13` 1 pad
- `+0x14` u32 `ms`
- `+0x18` u32 `self_metrics`
- `+0x1C` u32 `top_master_metrics`
- `+0x20` 6 `top_master` ether_addr
- `+0x26` 6 `addr` ether_addr
- `+0x2C` 2 pad
- Fields from the Swift type dump (`awdl_callers_probe.log:1298-1317`); natural alignment (u32 @20)
  closes exactly at 44. Proxi copies the first 40 bytes as qwords + the dword @40
  (`setAWDL_AWDL_ADVERTISERS 0xffffff800171c932`: `v6..v10 = a2[0..4]`,
  `*((_DWORD *)v5 + 11) = *((_DWORD *)a2 + 10)`).

**`apple80211_awdl_dev_stats`** (124 B; len ==124 GET, handler `0xffffff800227d848`)
- `+0x00` u32 `version` `?` (the PM getter only re-routes the request: `apple80211RouteInternal(..., 247, ...)`,
  `getAwdlDevStats 0xffffff800221dbfc`)
- `+0x04` u32×29 — counters/statistics copied from the chip state (Proxi get
  `0xffffff800171b698`: writes `*(v18 + 4..72)`, and sums of two chip sources at
  +12/+16/+20/+24/+28/+52/+68)
- `+0x4C` u16 — `?` (`+76`)
- `+0x4E` u16 — `?` (`+78`)
- `+0x54`..`+0x73` — `?` (no observed writes)
- `+0x74` u32 — `?` (`+116`)

**`apple80211_awdl_dfsp_config`** (size `?` >= 22; type 262 without a table row)
- `+0x00` u16 `version` `?`
- `+0x02` u16, `+0x04` u16, `+0x06` u16, `+0x08` u8, `+0x09` u8, `+0x0C` u16, `+0x0E` u32,
  `+0x12` u16, `+0x14` u16 — initialized to 0 and filled by Proxi
  (`getAWDL_DFSP_CONFIG 0xffffff8001728fba`); `setAWDL_DFSP_CONFIG 0xffffff8001728dd0` forces
  `+0x0C |= 0xD000` when activating the mode ("Enabled DFS Proxy Mode."); names `?`

**`apple80211_awdl_cca`** (392 B; len ==392 GET, handler `0xffffff800227f76c`)
- `+0x00` u32 `version`
- `+0x04` u16 `channel` — validated <= 0x10 and != 0 by Proxi (`getAWDL_CCA 0xffffff800171ba8e`)
- `+0x1E` u8.. — per-channel CCA result table (the PM walks `LOBYTE(a2[v11/2 + 15])` with
  channel index, `getAwdlCCA 0xffffff800221dc36`)
- rest of offsets `?`

**`apple80211_awdl_cap`** (size `?`; type 341 without a table row)
- `+0x00` u16 `version` `?`
- `+0x02` u16 — copied from the chip state (`getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES
  0xffffff800171ed38`)
- `+0x04` u8 — `?`

**`apple80211_llw_params`** (44 B; len ==44, handlers `0xffffff800227f903`/`0xffffff800228c58b`)
- `+0x00` u32 `version`
- `+0x04` u32 — base of the conversion to iovar (`v9 = (_DWORD *)(a2 + 4)`,
  `setLLW_PARAMS 0xffffff8001726ac8`); `?`
- `+0x08` u16 — `?` (copied to iovar+4)
- `+0x0A` u8 — `?` (iovar+8/+15)
- `+0x0B` u8 `enable` — `*(_BYTE *)(a2 + 11)` decides the set branch (`0xffffff8001726ac8`) and the get
  writes it as the bool of the subcmd (`getLLW_PARAMS 0xffffff800172753e`: `*(_BYTE *)(a2 + 11) =
  (*(DWORD *)v28 + 2) != 0`)
- `+0x0E` u16 — `?` (iovar+6)
- `+0x10` u16 — `?` (iovar+8)
- `+0x12` u16 — bit field (set splits bits 1/2/4 and >>3 toward iovar; get recombines them)
- `+0x14` u8 — low nibble toward iovar+13 (`*(_BYTE *)(a2 + 20) & 0xF`)
- `+0x15` u8, `+0x16` u8 — toward iovar+16/+17
- `+0x18` u8 — bit field (get: `v36 = v28[7] & 1 | *(_BYTE *)(a2 + 24) & 0xFE`)
- `+0x19`..`+0x2B` — `?`
- The underlying route is the hp2p subset (`runHP2PCommand`, logs "hp2p get/set iovar failed",
  "Unexpected hp2p subcmd resp id").

**`apple80211_tx_rate`** (16 B; len ==16 SET only, handler `0xffffff800228d772`)
- `+0x00` u32 `version`
- `+0x04` u32 — `?` (Proxi `setTX_RATE 0xffffff8001720c20`: `v13[1] = *(_DWORD *)(a2 + 4)`)
- `+0x08` u16 — `?`
- `+0x0C` u16 — `?` (also written by `getTX_RATE 0xffffff8001720ade`)
- `+0x0E` 2 pad
- The PM only re-routes (`setAwdlTxRate 0xffffff800221e112`, internal type 411).

**`apple80211_awdl_peer_traffic_registration`** (184 B = 0xB8; len ==184 SET only, handler
`0xffffff8002289465`)
- `+0x00` u32 `version` — validated (`setAwdlPeerTrafficRegistration 0xffffff800220946a`:
  "Rejecting Traffic Reg with invalid version: %d")
- `+0x04` 6 `mac` ether_addr — affected peer; if it is the null address it resolves by another way
  (`memcmp(a2 + 4, &null_address, 6)`, `IO80211PeerManager::findPeer(a2 + 4)`)
- `+0x0A` u16 — `?` (copied as `LOWORD(v166)` in the airplay branch)
- `+0x0C` u32 `uuid_len` — mandatory != 0 ("UUID Has length == 0, incorrect, failing traffic reg")
- `+0x10` char `uuid[]` — service string; the PM does `strncmp` against `rtmode`, `catspolicy`,
  `CARPLAY_UUID`, `FACETIME_UUID`, `wifid-assisted-discovery`, `data-assisted-discovery`,
  `ranging-responder`, `ranging-Intiator`, `airplay-sink`, `avconference`, `WiFiCallingUUID`,
  `A2CB4344-1CC7-47EF-8D25-FB68B047620F`, `D2DMigration`, `timeSync`, `airplay-connectivity-check`,
  `copresence`, `airdrop`, `ensemble`, `MPRemoteCamera`, `EdgeKit`, `spatialStreaming`,
  `airplay`, `sidecar`, `MacVirtualDisplay` (all in `setAwdlPeerTrafficRegistration`)
- `+0x8F` u8 — set to 0 by the PM (`a2[143] = 0`)
- `+0x90` u32, `+0x94` u32 — `?` (`*((_DWORD *)a2 + 36)` / `+37`)
- `+0xA8` u32 `enable` — `*((_DWORD *)a2 + 42)`; decides register/deregister of the RTG (`!= 0` → register)
- `+0xAC` u32 — `*((_DWORD *)a2 + 43)`; `?` (logged next to the uuid)
- `+0xB0` u16 — `*((_WORD *)a2 + 88)`; `?`
- `+0xB2` u16 — `*((_WORD *)a2 + 89)`; `?`
- `+0xB4` 4 rest `?`
- The PM persists the 184 B in its ivar +16644 (`memmove((char *)pm + 16644, a2, 0xB8)`); Proxi
  recognizes `CARPLAY_UUID`/`airplay`/`sidecar`/`airplay-connectivity-check` and processes `enable`
  (`setAWDL_PEER_TRAFFIC_REGISTRATION 0xffffff800171fcaa`).

**`apple80211_companion_skywalk_if_create`** — see §3.4.

## 5. PeerManager-only structs (modern path, no surface slot)

| GET/SET type | IOC | struct | len GET/SET | PM GET / PM SET |
|---|---|---|---|---|
| —/120 | AWDL_PEER_SERVICE_REQUEST | `apple80211_peer_service_request` | ==20(&&data) / 20? | `0xffffff800222be4a` / `0xffffff8002218d98` |
| 127/127 | AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT | `apple80211_awdl_availability_window_ap_alignment` | ==16 / ==16 | `0xffffff800221f78a` / `0xffffff800221cc4c` |
| 128/128 | AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT | `apple80211_awdl_sync_frame_ap_alignment` | ==12 / ==12 | `0xffffff800221f7b0` / `0xffffff800221cd78` |
| 119/119 | AWDL_SERVICE_PARAMS | `apple80211_service_parameters` | ==44 / ==44 | `0xffffff800221f43e` / `0xffffff800221c7f4` |
| 131/131 | AWDL_OUI | `apple80211_awdl_oui` | ==8 / ==8 | `0xffffff800221fac6` / `0xffffff800221d25c` |
| 133/133 | AWDL_TOP_MASTER | `apple80211_awdl_top_master` | ==16 / ==16 | `0xffffff800221fce4` / `0xffffff800221e2e8` |
| 138/138 | AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME | `apple80211_awdl_sync_frame_packet_lifetime` | ==8 / ==8 | `0xffffff800221fe1e` / `0xffffff800221e8b6` |
| 139/139 | AWDL_MASTER_MODE_SYNC_FRAME_PERIOD | `apple80211_awdl_master_sync_frame_period` | ==8 / ==8 | `0xffffff800221fe34` / `0xffffff800221e956` |
| 148/148 | AWDL_STRATEGY | `apple80211_awdl_strategy` | ==24 / ==24 | `0xffffff800221feee` / `0xffffff800221c840` |
| 163/163 | AWDL_SOCIAL_TIME_SLOTS | `apple80211_awdl_social_time_slots` | ==8 / ==8 | `0xffffff800221bfb0` / `0xffffff800221bf26` |
| —/167 | AWDL_FORCED_ROAM_CONFIG | `apple80211_awdl_forced_roam_config` | — / ==16 | — / `0xffffff800221c968` |
| 201/— | AWDL_BTLE_PEER_INDICATION | `apple80211_awdl_btle_peer_indication_data` | ==32 / — | — / `0xffffff800221c400` |
| 202/— | AWDL_BTLE_STATE_PARAMS | `apple80211_awdl_btle_state_params` | ==28 / — | `0xffffff800221c004` |
| 203/— | AWDL_PEER_DATABASE | `apple80211_awdl_peer_database` | ==208480 / — | `0xffffff800221ff6c` |
| 204/— | AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS | `apple80211_awdl_btle_enable_synch_params` | — / ==20 | — / `0xffffff800221c656` |
| 217/217 | AWDL_OPER_MODE | `apple80211_awdl_oper_mode` | ==8 / ==8 | `0xffffff800221eb7a` / `0xffffff800221eb90` |
| 218/— | RESTORE_DEFAULTS | `apple80211_awdl_restore_defaults` | ==4 / — | `0xffffff800221ecf0` |
| 223/— | AWDL_STATISTICS | `apple80211_awdl_statistics` | ==3752 / — | `0xffffff800221d7f6` |
| 233/233 | AWDL_BLOCK_SET_COMMANDS | `apple80211_awdl_block_set_commands` | ==8 / ==8 | `0xffffff800221dd1e` / `0xffffff800221e266` |
| 244/— | AWDL_PREFERRED_CHANNELS | `apple80211_awdl_preferred_channels` | ==12 / — | `0xffffff800221f1fa` |
| —/253 | AWDL_NEARBY_LOG_TRIGGER | `apple80211_awdl_nearby_log_trigger_data` | — / ==8 | — / `0xffffff800221a10c` |
| —/340 | TRAFFIC_REGISTRATION | `apple80211_traffic_registration` | — / ==512 | — / `0xffffff800220c40a` |
| 249/— | AWDL_COMMON_CHANNEL | `apple80211_awdl_common_channel` | ==8 / — | `0xffffff800221fcce` |
| 343/— | AWDL_LOW_LATENCY_STATISTICS | `apple80211_awdl_low_latency_statistics` | ==4024 / — | `0xffffff800221dda0` |
| 351/— | AWDL_SIDECAR_DIAGNOSTICS | `apple80211_awdl_sidecar_diagnostics` | ==80 / — | `0xffffff800221df50` |
| 370/— | AWDL_AIRPLAY_STATISTICS | `apple80211_p2p_airplay_ext_stats` | ==4032 / — | `0xffffff800221de3a` |
| 415/— | POST_PRESENCE_DONE | `apple80211_awdl_ppp_done` | — / ==12 | — / `0xffffff800221c978` |
| 416/416 | AWDL_MI_BITMAP | `apple80211_awdl_mi_bitmap` | ==8 / ==8 | `0xffffff80022207be` / `0xffffff800222075e` |
| 417/— | AWDL_UMI_TX_PEER | `apple80211_awdl_UMI_data` | — / ==16 | — / `0xffffff8002219fca` |
| 419/— | LTE_RESTRICTED_CHANNELS | `apple80211_lte_restricted_channels` | — / ==136 | — / `0xffffff800221e9ea` |
| 420/— | NANPH_SOFTAP_CSA | `apple80211_nanph_softap_csa` | — / ==1 | — / `0xffffff8002219f8c` |
| 162/162 | AWDL_CTL | `apple80211_awdl_ctl` | ==243872 / ==243872 | `0xffffff80021d4e32` / `0xffffff80021cde94` |

Layout notes for the ones contributing observed fields:

**`apple80211_peer_service_request`** (20 B of fixed part + payload; Swift type dump
`awdl_callers_probe.log`)
- `+0x00` u32 `version`
- `+0x04` 6 `peer` ether_addr
- `+0x0A` u8 `delete_request`
- `+0x0B` u8 `aw_offset`
- `+0x0C` u32 `token`
- `+0x10` u16 `data_len`
- `+0x12` u16 `expect_fragmented_response`
- (Swift adds the flexible `data` field behind; the modern handler requires `len == 20` with
  `data != NULL`, `0xffffff800227bd3e`)

**`apple80211_awdl_availability_window_ap_alignment`** (16 B)
- `+0x00` u32 `version`
- `+0x04` u32 — PM ivar +37200 (`getAwdlAvailabilityWindowApAlignment 0xffffff800221f78a`;
  the setter treats -1 as "keep", `0xffffff800221cc4c`); `?` name
- `+0x08` u32 — PM ivar +37196; `?` name
- `+0x0C` u32 — 0 in the getter

**`apple80211_awdl_sync_frame_ap_alignment`** (12 B)
- `+0x00` u32 `version`; `+0x04` u32 `?`; `+0x08` u32 `?` (PM methods without fields observed in the
  digest; `getAwdlSyncFrameApAlignment 0xffffff800221f7b0`, `setAwdlSyncFrameApAlignment
  0xffffff800221cd78`)

**`apple80211_service_parameters`** (44 B; AWDL_SERVICE_PARAMS, 119)
- `+0x00` u32 `version` `?`
- `+0x04` u16 — PM ivar +43656 (`setAwdlPeerServiceParams 0xffffff800221c7f4`)
- `+0x08` u32 — PM ivar +43660
- `+0x0C` u64 — `?`
- `+0x14` u64 — `?`
- `+0x1C` u64 — `?`
- `+0x24` u64 — PM ivar +43688
- (44 = 0x2C)

**`apple80211_awdl_oui`** (8 B)
- `+0x00` u32 `version`
- `+0x04` 3 `oui[3]` — PM ivars +42424..+42426 (`getAwdlOUI 0xffffff800221fac6` /
  `setAwdlOUI 0xffffff800221d25c`)
- `+0x07` 1 pad

**`apple80211_awdl_top_master`** (16 B)
- `+0x00` u32 `version`
- `+0x04` u32 — `?` (written by `getAwdlTopMaster 0xffffff800221fce4`)
- `+0x08` u16 `election_id` — PM ivar +37235 (`*(_WORD *)(a2 + 8) = *(_WORD *)(v2 + 37235)`)
- `+0x0A` 6 pad

**`apple80211_awdl_sync_frame_packet_lifetime`** (8 B)
- `+0x00` u32 `version`; `+0x04` u32 `lifetime` — PM ivar +42420
  (`getAwdlSyncFramePacketLifetime 0xffffff800221fe1e` / `set... 0xffffff800221e8b6`)

**`apple80211_awdl_master_sync_frame_period`** (8 B)
- `+0x00` u32 `version`; `+0x04` u32 `period` — PM ivar +62952
  (`getAwdlMasterSyncFramePeriod 0xffffff800221fe34`)

**`apple80211_awdl_strategy`** (24 B; Swift `{version, strategy: awdlStrategy(UInt32), param, lastStateChange: UInt64}`, metadata `w2p $s3__C24apple80211_awdl_strategyVMa 0x10000d160`)
- `+0x00` u32 `version`
- `+0x04` u32 `strategy` — PM ivar +10464 (`getAwdlStrategy 0xffffff800221feee`)
- `+0x08` u32 `param` — PM ivar +10468
- `+0x10` u64 `lastStateChange` — `*(_QWORD *)(a2 + 16) = v7`
- `+0x18` 4 pad

**`apple80211_awdl_social_time_slots`** (8 B; Swift `{version, enabled: UInt8}`)
- `+0x00` u32 `version`; `+0x04` u8 `enabled` — PM ivar +42660
  (`getAwdlSocialTimeSlots 0xffffff800221bfb0`); `+0x05` 3 pad

**`apple80211_awdl_forced_roam_config`** (16 B; SET 167 `0xffffff80022894ca`, len==16 &&data)
- `+0x00` u32 `version` `?`
- `+0x04` 6 `peer` ether_addr — consumed by `forceRoamToPeersAP(addr, u8, u8)`
  (`0xffffff80021f94ac`, via `setAwdlForcedRoamConfig 0xffffff800221c968`)
- `+0x0A` 6 `?`

**`apple80211_awdl_btle_peer_indication_data`** (32 B; SET 201 `0xffffff8002289910`, len==32)
- `+0x00` u32 `version` `?`
- `+0x04` u32 `state` — `v6 = *(_DWORD *)(a2 + 4)` (`setAwdlBtlePeerIndication 0xffffff800221c400`)
- `+0x08` 6 `peer` ether_addr — `findPeer(a2 + 8)`
- `+0x0E` 18 rest `?`

**`apple80211_awdl_btle_state_params`** (28 B; GET 202 `0xffffff800227cbb4`, len==28)
- `+0x00` u32 `version` `?`
- `+0x04` u32 — `?` (`getAwdlBtleStateParams 0xffffff800221c004`)
- `+0x08` u16 — PM ivar +37220
- `+0x0A` u16 — PM ivar +43656
- `+0x0C` u32 — PM ivar +16272
- `+0x10` u64 — `?`
- `+0x18` u32 — PM ivar +16284
- `+0x1C` 4 pad

**`apple80211_awdl_peer_database`** (208480 B; GET 203 `0xffffff800227cc16`)
- `+0x00` u32 `version` `?`
- `+0x04` u32 — state value (100 when there is a limit, `getAwdlPeerDatabase 0xffffff800221ff6c`)
- `+0x327C8` (206408) u32 / `+0x327CC` u16 — header of the first peer block (PM ivars +37216/+37220)
- `+0x32810` (206416) — area of 0x810 B per block (`bzero(a2 + 206416, 0x810)`); peer stride
  `?` (2064 B)
- `+0x329D0` (206913) u8 / `+0x33000` (208461)..`+0x33018` (208472) — tail fields of the dump
  (PM ivars +16392, +16156, +42568, +16178, +64373, +64396)
- Structure of an individual peer `?` (the method copies blocks and loose fields; no names).

**`apple80211_awdl_btle_enable_synch_params`** (20 B; SET 204 `0xffffff8002289972`, len==20)
- `+0x00` u32 `version`; `+0x04` u32 `enable` — PM ivar +16288 as bool
  (`setAwdlBtleEnableSynchWithParams 0xffffff800221c656`); `+0x08` 12 `?`

**`apple80211_awdl_oper_mode`** (8 B; Swift `{version, mode}`, metadata `w2p $s3__C25apple80211_awdl_oper_modeVMa 0x10000cee0`)
- `+0x00` u32 `version`; `+0x04` u32 `mode` — PM ivar +768 (`getAwdlOperatingMode
  0xffffff800221eb7a`); the setter validates the catalog ("Unrecognized AWDL operation mode %i",
  `0xffffff800221eb90`; name table `awdlOperModeDesc` at `syms.txt:6114`)

**`apple80211_awdl_statistics`** (3752 B; GET 223 `0xffffff800227d272`) — integral dump of the
statistics block of the PM: `memmove(a2, pm + 43792, 0xEA8)` (`getAwdlStatistics 0xffffff800221d7f6`);
content `?` (blob of ivars).

**`apple80211_awdl_block_set_commands`** (8 B; 233)
- `+0x00` u32 `version`; `+0x04` u32 `mask` `?` (PM re-routes internally, `0xffffff800221dd1e`/`e266`)

**`apple80211_awdl_preferred_channels`** (12 B; Swift `{version, preferred: UInt16, master: UInt16, awdl_info_ext: {secondary_preferred: UInt16}}`, metadata `w2p $s3__C34apple80211_awdl_preferred_channelsVMa 0x10000d140`)
- `+0x00` u32 `version`
- `+0x04` u16 `preferred` — `?` (the PM getter writes the social channel: 6/11/1/0 into `a2[4]` over `_WORD*`)
- `+0x06` u16 `master` — `?`
- `+0x08` u16 `secondary_preferred` — `?`
- `+0x0A` 2 pad
- (`getAwdlPreferredChannels 0xffffff800221f1fa`)

**`apple80211_awdl_nearby_log_trigger_data`** (8 B; SET 253 `0xffffff800228a630`, len==8)
- `+0x00` u32 `version`; `+0x04` u16 `trigger` — PM ivar +64088
  (`setAwdlNearbyLogTrigger 0xffffff800221a10c`); `+0x06` 2 pad

**`apple80211_traffic_registration`** (512 B; SET 340 `0xffffff800228c4a7`) — version validated by
`setTrafficRegistration 0xffffff800220c40a` ("Rejecting Traffic Reg with invalid version");
fields `?`.

**`apple80211_awdl_common_channel`** (8 B; GET 249 `0xffffff800227d8d4`)
- `+0x00` u32 `version`; `+0x04` u32 `channel` — PM ivar +420 (`getAwdlCommonChannel 0xffffff800221fcce`)

**`apple80211_awdl_low_latency_statistics`** (4024 B; GET 343 `0xffffff800227f89e`) — `memmove(a2,
pm + 47608, 0xFB8)` (`getAwdlLowLatencyStatistics 0xffffff800221dda0`); content `?` (the Swift type dump
splits it into `configs` + `traffic` with `counts` of 14 u64 and histograms of `Double` tuples:
`awdl_callers_probe.log:1339-1341`, `1699-1712`).

**`apple80211_awdl_sidecar_diagnostics`** (80 B; GET 351 `0xffffff800227fca6`) — `memmove(a2,
pm + 6658, 0x50)` + dynamic fields from the vtable of the sidecar (`a2[5]`, `a2[6]`, `a2 + 7`,
`getAwdlSidecarDiagnostics 0xffffff800221df50`); breakdown `?`.

**`apple80211_p2p_airplay_ext_stats`** (4032 B; GET 370 `0xffffff8002280217`) — `memmove(a2,
pm + 53368, 0xFC0)` and reset of the origin (`getAwdlAirplayStatistics 0xffffff800221de3a`); content `?`.

**`apple80211_awdl_ppp_done`** (12 B; Swift `{version: UInt32, peerMac: ether_addr}`, metadata
`w2p $s3__C24apple80211_awdl_ppp_doneVMa 0x100007700`)
- `+0x00` u32 `version`; `+0x04` 6 `peerMac` — `findPeer(a2 + 4)`
  (`setAwdlPostPeerPresenceDone 0xffffff800221c978`); `+0x0A` 2 pad

**`apple80211_awdl_mi_bitmap`** (8 B; 416)
- `+0x00` u32 `version`; `+0x04` u32 `bitmap` — `adjustMiBitmap(bitmap, false, 1)`
  (`setAwdlMiBitmap 0xffffff800222075e`; getter `0xffffff80022207be`, PM ivar +42760)

**`apple80211_awdl_UMI_data`** (16 B; Swift `{version, umiOption: UInt32, peer: ether_addr}`, metadata `w2p $s3__C24apple80211_awdl_UMI_dataVMa 0x10000cf50`)
- `+0x00` u32 `version`; `+0x04` u32 `umiOption` — `sendRealTimeUMI(peer, umiOption, 0)`
  (`setAWDLUMIToPeer 0xffffff8002219fca`); `+0x08` 6 `peer` — `findPeer(a2 + 8)`; `+0x0E` 2 pad

**`apple80211_lte_restricted_channels`** (136 B; SET 419 `0xffffff800228da8e`)
- `+0x00` u32 `version` `?`
- `+0x04` u16 `channels[64]` — `memmove(pm + 62620, a2 + 4, 2 * count)`
- `+0x84` u32 `count` — PM ivar +62748 (`setLteRestrictedChannels 0xffffff800221e9ea`)

**`apple80211_nanph_softap_csa`** (1 B; SET 420 `0xffffff800228daf3`, len==1 &&data) — u8 at +0;
`setSoftApCSANanPh 0xffffff8002219f8c` rejects without HostAP/AWDL concurrency
("HostAP/AWDLConcurrency invalid.").

**`apple80211_awdl_restore_defaults`** (4 B; GET 218 `0xffffff800227d1ae`) — u32 at +0 without
observed write (`getAwdlRestoreDefaults 0xffffff800221ecf0`); `?`.

**`apple80211_awdl_ctl`** (required len 243872; internal buffer of route 0x3B8A0 = 244128 B, GET
`0xffffff800227c6f9`: `memmove(v6, v9, 0x3B8A0)`)
- `+0x04` char `cmd[128]` — parameter name (`strnlen(a2+4, 0x80)`); catalog of names at
  `setAwdlCtl 0xffffff80021cde94`: `awdl_bonjour_offload`, `awdlMaxBW`, `multicastTxOffset`,
  `UMITxOffset`, `guardTime`, `awdl_min_rate`, `channelSequenceStrategy`, `scheduleState`,
  `awdlCCSanityFailureEnabled`, `customRTGInfraChannel`, `force6GChannel`, … (more than 60)
- `+0x88` u64 `value` — `*(_QWORD *)(a2 + 136)`
- `+0x3B7D4` (243860) u32 `out` — output accumulator (`+= v39` in the get)
- `+0x3B7E0` (243872) u64 `callback` — must be a kernel pointer (`>= 0xFFFFFFFFFFFFF000` and
  `>> 39 == 0x1FFFFFF`)
- `+0x3B7E8` (243880) u32 `tag` — `== 1` required by both methods
- `+0x3B7EC` (243884) u32 — `?`
- The fields from +243872 on fall outside the `len == 243872` required from the client: the internal
  route fills them with the 0x3B8A0 buffer (`?` exact mechanics).

## 6. AWDL surface slots with no reachable request

Types 143 (`PEER_CACHE_CONTROL`), 149 (`AWDL_OOB_REQUEST`), 151
(`AWDL_SYNC_FRAME_TEMPLATE`), 158 (`AWDL_DEVICE_CAPABILITIES`), 225
(`AWDL_OOB_AUTO_REQUEST`), 262/263 (`AWDL_DFSP_*`), 341 (`AWDL_CAP`), 342
(`AWDL_OOB_SEC_PAYLOAD_REQUEST`), 414 (`P2P_COEX_PROTECTION`), 418 (`PSF_DWELL`), 432
(`AWDL_CONFIG`) and 467 (`AWDL_RTG_PEER_STATS_CONFIG`) have entry 0 in `gGetHandlerTable` and
`gSetHandlerTable` (read in `kc_tables.txt`: `type 143 slot ffffff800245efd0 -> 0`, etc.). A
user client sending them receives 102 without reaching the driver. The corresponding Broadcom
slots are exercised via the internal route (WCL glue → bridges → slots; the bridges have
a row in `gInternalGet/SetHandlerTable`, `AWDL_APPLE80211_DISPATCH.md` §6) or internally by the
driver itself. Observed fields in Proxi for those contributing something:

- `apple80211_awdl_oob_request` (`setAWDL_OOB_REQUEST 0xffffff8001726474` and
  `setAWDL_OOB_AUTO_REQUEST 0xffffff8001727c0e`): `+0x04` u32, `+0x08` u16, `+0x14` u16, `+0x16`
  u32, `+0x1A` u16, `+0x28` u16 `len`, `+0x30` u64 `ptr` to data (Proxi does
  `memmove(dst, *v11, v11[1])` with pointer+length taken from the struct); size `?`.
- `apple80211_awdl_sync_frame_template` (`setAWDL_SYNC_FRAME_TEMPLATE 0xffffff800171da80`):
  `+0x04` u32 (0 = `?`), `+0x08` u64 `ptr`; size `?`.
- `apple80211_p2p_coex_protection` (`setP2P_COEX_PROTECTION 0xffffff800172a968`): `+0x04` u8
  `len1` + data at `+0x05`; `+0x4A` (74) u8 `len2` + data at `+0x4B`; size `?`.
- `apple80211_awdl_af_sec_payload_request` (`setAWDL_OOB_SEC_PAYLOAD_REQUEST 0xffffff8001727d56`):
  `+0x02` u16, `+0x04` u32, `+0x08` u64 `ptr`; size `?`.
- `apple80211_rtg_peer_stats_cfg` (`setAWDL_RTG_PEER_STATS_CONFIG 0xffffff800171fb6c`): `+0x01`
  u8, `+0x02` u16, `+0x04` 6 `peer` ether_addr, `+0x0A` u8, `+0x0C` u32; size `?`.
- `apple80211_peer_cache_control` (`setPEER_CACHE_CONTROL 0xffffff80017235ce`): opcode `?` with
  validation "invalid opcode %u" and a `memmove` of 0x1D0 B; size `?`.
- `apple80211_awdl_config_request` (`setAWDL_CONFIG 0xffffff80017215e2`): `+0x04` u32 (gets
  `| 3` applied); size `?`.
- `apple80211_awdl_psf_dwell_params` (`setPSF_DWELL 0xffffff800171dfc0`): no fields observed
  (route to commander); the PM equivalent receives `psfDwell` by value
  (`setPSFDwellE8psfDwell`, `syms.txt:13780`); size `?`.
- `apple80211_awdl_dfsp_ucsa_config` (`setAWDL_DFSP_UCSA_CONFIG 0xffffff800172919c`): no fields
  observed; size `?`.

## 7. C definitions

The sizes with `static_assert` have the `len == N` verified in the modern handler or an exact
equivalence with the buffer of the bridge. Fields `fN`/`pad` with `?` = meaning not determined;
the offsets are verified unless indicated.

```c
#include <stdint.h>
#include <stddef.h>
#include <sys/_types/_u_int16_t.h>

struct ether_addr { uint8_t octet[6]; };

/* 1. Request header (ioctl SIOCGA80211/SIOCSA80211, _IOWR('i',200/201,40)) */
struct apple80211req {
    char     ifname[16];     /* +0x00: destination interface               */
    uint32_t req_type;       /* +0x10: type 1..586                         */
    uint32_t val;            /* +0x14: auxiliary (0 in wifip2pd)           */
    uint32_t req_len;        /* +0x18: length of the data buffer           */
    void    *req_data;       /* +0x20: buffer                              */
};
_Static_assert(sizeof(struct apple80211req) == 40, "apple80211req");

/* 3.1 CARD_CAPABILITIES (GET 12) */
struct apple80211_capability_data {
    uint32_t version;        /* +0x00: 1 in Broadcom                       */
    uint8_t  flags[24];      /* +0x04: flags[4] bit0 = AWDL gate, flags[9] bit0 = NAN gate */
};
_Static_assert(sizeof(struct apple80211_capability_data) == 28, "capability_data");
/* copyback of the handler: min(req_len, 21) bytes from &flags[0]          */

/* 3.2 VIRTUAL_IF_CREATE (SET 94) */
struct apple80211_virt_if_create_data {
    uint32_t f0;             /* +0x00: sent by wifip2pd, read ?            */
    uint8_t  mac[6];         /* +0x04: requested MAC                       */
    uint8_t  pad0[2];        /* +0x0A                                      */
    uint32_t role;           /* +0x0C: 6=AWDL, 9/0xA=?, 7=APSTA, 8..10=NAN */
    char     ifname[16];     /* +0x10: output, created BSD name            */
};
_Static_assert(sizeof(struct apple80211_virt_if_create_data) == 32, "virt_if_create_data");

/* 3.4 COMPANION_SKYWALK_IF_CREATE (SET 293) */
struct apple80211_companion_skywalk_if_create {
    uint32_t code;           /* +0x00: companion code                      */
    uint32_t op;             /* +0x04: 1=create, rest=delete (?)           */
    char     name[16];       /* +0x08: output, BSD name (llw0/llw1)        */
};
_Static_assert(sizeof(struct apple80211_companion_skywalk_if_create) == 24, "companion");

/* 4. SSID (GET/SET 1) */
struct apple80211_ssid_data {
    uint32_t version;        /* +0x00                                      */
    uint32_t ssid_len;       /* +0x04                                      */
    uint8_t  ssid[32];       /* +0x08: "awdl\0" in Broadcom AWDL           */
};
_Static_assert(sizeof(struct apple80211_ssid_data) == 40, "ssid_data");

/* 4. CHANNEL (GET/SET 4) */
struct apple80211_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04                                      */
    uint32_t flags;          /* +0x08                                      */
};
struct apple80211_channel_data {
    uint32_t version;        /* +0x00: ? in Broadcom AWDL (stub)           */
    struct apple80211_channel channel; /* +0x04                            */
};
_Static_assert(sizeof(struct apple80211_channel_data) == 16, "channel_data");

/* 4. BSSID (GET 9) */
struct apple80211_bssid_data {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr bssid; /* +0x04                                      */
    uint8_t  pad0[2];        /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_bssid_data) == 12, "bssid_data");

/* 4. STATE (GET 13) */
struct apple80211_state_data {
    uint32_t version;        /* +0x00                                      */
    uint32_t state;          /* +0x04: 4 in Broadcom AWDL                  */
};
_Static_assert(sizeof(struct apple80211_state_data) == 8, "state_data");

/* 4. AWDL_SYNC_PARAMS (GET/SET 116) */
struct apple80211_awdl_sync_params {
    uint32_t version;        /* +0x00                                      */
    uint32_t aw_cmn_length;  /* +0x04                                      */
    uint32_t f8;             /* +0x08: ?                                   */
    uint32_t aw_ext_length;  /* +0x0C                                      */
    uint32_t action_frame_period; /* +0x10                                  */
    uint32_t f20;            /* +0x14: cache Proxi ivar +0x29C             */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_params) == 24, "awdl_sync_params");

/* 4. AWDL_SYNC_ENABLED (GET/SET 117) */
struct apple80211_awdl_sync_enabled {
    uint32_t version;        /* +0x00                                      */
    uint32_t flags;          /* +0x04                                      */
    uint32_t enabled;        /* +0x08                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_enabled) == 12, "awdl_sync_enabled");

/* 4. AWDL_EXTENSION_STATE_MACHINE_PARAMETERS (GET/SET 118) */
struct apple80211_awdl_extension_state_machine_parameter {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: effective u8 value                  */
    uint32_t f8;             /* +0x08: effective u8 value                  */
    uint32_t f12;            /* +0x0C: effective u8 value                  */
    uint32_t f16;            /* +0x10: effective u8 value                  */
};
_Static_assert(sizeof(struct apple80211_awdl_extension_state_machine_parameter) == 20, "ext_params");

/* 4. AWDL_ELECTION_ID (GET/SET 122) */
struct apple80211_awdl_election_id {
    uint32_t version;        /* +0x00                                      */
    uint32_t election_id;    /* +0x04: effective u16 value                 */
};
_Static_assert(sizeof(struct apple80211_awdl_election_id) == 8, "election_id");

/* 4. AWDL_BSSID (SET 125) */
struct apple80211_awdl_bssid {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr bssid; /* +0x04                                      */
    struct ether_addr f10;   /* +0x0A: second MAC, ?                       */
};
_Static_assert(sizeof(struct apple80211_awdl_bssid) == 16, "awdl_bssid");

/* 4. AWDL_ELECTION_METRIC (GET/SET 126) */
struct apple80211_awdl_election_metric {
    uint32_t version;        /* +0x00                                      */
    uint32_t metric;         /* +0x04                                      */
    uint8_t  flag;           /* +0x08: !=0 => PM keeps its metric          */
    uint8_t  pad0[3];        /* +0x09                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_election_metric) == 12, "election_metric");

/* 4. AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE (GET/SET 129) */
struct apple80211_awdl_sync_channel_sequence {
    uint32_t version;        /* +0x00                                      */
    uint8_t  f4;             /* +0x04: ?                                   */
    uint8_t  count;          /* +0x05                                      */
    uint8_t  dfs;            /* +0x06                                      */
    uint8_t  f7;             /* +0x07: ?                                   */
    uint8_t  f8;             /* +0x08: ?                                   */
    uint8_t  f9;             /* +0x09: ?                                   */
    uint8_t  pad0[6];        /* +0x0A                                      */
    uint16_t entries[];      /* +0x10: channels (flexible; block = 0x190)  */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_channel_sequence) == 16, "chanseq hdr");

/* 4. PEER_CACHE_MAXIMUM_SIZE (GET 130) */
struct apple80211_peer_cache_maximum_size {
    uint32_t version;        /* +0x00                                      */
    uint32_t size;           /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_peer_cache_maximum_size) == 8, "pcms");

/* 4. AWDL_MASTER_CHANNEL (GET/SET 132) */
struct apple80211_awdl_master_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_master_channel) == 8, "master_channel");

/* 4. AWDL_SYNC_STATE (GET/SET 134) */
struct apple80211_awdl_sync_state {
    uint32_t version;        /* +0x00: 1 when writing                      */
    uint32_t state;          /* +0x04                                      */
    uint32_t f8;             /* +0x08: ?                                   */
    uint16_t f12;            /* +0x0C: ?                                   */
    uint16_t pad0;           /* +0x0E                                      */
    uint32_t f16;            /* +0x10: ?                                   */
    uint8_t  f20;            /* +0x14: ?                                   */
    uint8_t  pad1[3];        /* +0x15                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_state) == 24, "sync_state");

/* 4. AWDL_ELECTION_RSSI_THRESHOLDS (GET/SET 135) */
struct apple80211_awdl_election_rssi_thresholds {
    uint32_t version;        /* +0x00                                      */
    int8_t   t1;             /* +0x04: sbyte in a 4-byte hole (PM signext) */
    uint8_t  pad0[3];        /* +0x05                                      */
    int8_t   t2;             /* +0x08                                      */
    uint8_t  pad1[3];        /* +0x09                                      */
    int8_t   t3;             /* +0x0C                                      */
    uint8_t  pad2[3];        /* +0x0D                                      */
    int8_t   t4;             /* +0x10                                      */
    uint8_t  pad3[3];        /* +0x11                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_election_rssi_thresholds) == 20, "rssi_thr");

/* 4. AWDL_PRESENCE_MODE (GET/SET 136) */
struct apple80211_awdl_presence_mode {
    uint32_t version;        /* +0x00                                      */
    uint32_t mode;           /* +0x04: <= 0x3F                             */
};
_Static_assert(sizeof(struct apple80211_awdl_presence_mode) == 8, "presence_mode");

/* 4. AWDL_ELECTION_PARAMS (GET/SET 155) */
struct apple80211_awdl_election_params {
    uint32_t version;        /* +0x00: 1 when writing                      */
    uint8_t  f4[12];         /* +0x04: ?                                   */
    uint8_t  f16;            /* +0x10                                      */
    uint8_t  pad17;          /* +0x11                                      */
    uint16_t f18;            /* +0x12                                      */
    uint8_t  f20;            /* +0x14                                      */
    uint8_t  pad21;          /* +0x15                                      */
    uint32_t f22;            /* +0x16 (unaligned)                          */
    uint16_t f26;            /* +0x1A                                      */
    uint32_t f28;            /* +0x1C                                      */
    uint32_t f32;            /* +0x20                                      */
    uint32_t election_id;    /* +0x24                                      */
    uint8_t  f40[16];        /* +0x28: ?                                   */
} __attribute__((packed));   /* u32 at +0x16 unaligned in the kernel       */
_Static_assert(sizeof(struct apple80211_awdl_election_params) == 56, "election_params");

/* 4. AWDL_SECONDARY_MASTER_CHANNEL (GET/SET 205) */
struct apple80211_awdl_secondary_master_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04: effective u8 value                  */
};
_Static_assert(sizeof(struct apple80211_awdl_secondary_master_channel) == 8, "sec_master");

/* 4. AWDL_AF_TX_MODE (GET/SET 208) */
struct apple80211_awdl_af_tx_mode {
    uint32_t version;        /* +0x00                                      */
    uint32_t mode;           /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_af_tx_mode) == 8, "af_tx_mode");

/* 4. AWDL_MIN_RATE (GET/SET 213) */
struct apple80211_awdl_min_rate {
    uint32_t version;        /* +0x00                                      */
    uint8_t  rate;           /* +0x04                                      */
    uint8_t  pad0[3];        /* +0x05                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_min_rate) == 8, "min_rate");

/* 4. AWDL_ENCRYPTION_TYPE (GET/SET 220) */
struct apple80211_encrypt {
    uint32_t version;        /* +0x00                                      */
    uint32_t encrypt_type;   /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_encrypt) == 8, "encrypt");

/* 4. AWDL_TXCAL_PERIOD (GET/SET 226, internal route) */
struct apple80211_awdl_txcal_period {
    uint32_t version;        /* +0x00                                      */
    uint32_t period;         /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_txcal_period) == 8, "txcal_period");

/* 4. AWDL_AWDL_ADVERTISERS (SET 229) */
struct awdl_peer_advert_add {
    uint16_t aw_counter;         /* +0x00                                  */
    uint16_t tx_counter;         /* +0x02                                  */
    uint16_t tx_delay;           /* +0x04                                  */
    uint16_t period_tu;          /* +0x06                                  */
    uint16_t aw_cmn_length;      /* +0x08                                  */
    uint16_t aw_ext_length;      /* +0x0A                                  */
    uint16_t aw_period;          /* +0x0C                                  */
    int8_t   rssi;               /* +0x0E                                  */
    uint8_t  guard_time;         /* +0x0F                                  */
    uint8_t  presence_mode;      /* +0x10                                  */
    uint8_t  age;                /* +0x11                                  */
    uint8_t  dist_top;           /* +0x12                                  */
    uint8_t  pad0;               /* +0x13                                  */
    uint32_t ms;                 /* +0x14                                  */
    uint32_t self_metrics;       /* +0x18                                  */
    uint32_t top_master_metrics; /* +0x1C                                  */
    struct ether_addr top_master;/* +0x20                                  */
    struct ether_addr addr;      /* +0x26                                  */
};
_Static_assert(sizeof(struct awdl_peer_advert_add) == 44, "peer_advert_add");

/* 4. AWDL_CCA (GET 338) */
struct apple80211_awdl_cca {
    uint32_t version;        /* +0x00                                      */
    uint16_t channel;        /* +0x04: <= 0x10                             */
    uint8_t  rest[386];      /* +0x06: CCA table from +0x1E; breakdown ?   */
};
_Static_assert(sizeof(struct apple80211_awdl_cca) == 392, "awdl_cca");

/* 4. LLW_PARAMS (GET/SET 344) */
struct apple80211_llw_params {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t f8;             /* +0x08: ?                                   */
    uint8_t  f10;            /* +0x0A: ?                                   */
    uint8_t  enable;         /* +0x0B                                      */
    uint8_t  pad0[2];        /* +0x0C                                      */
    uint16_t f14;            /* +0x0E: ?                                   */
    uint16_t f16;            /* +0x10: ?                                   */
    uint16_t f18;            /* +0x12: bit field                           */
    uint8_t  f20;            /* +0x14: low nibble                          */
    uint8_t  f21;            /* +0x15: ?                                   */
    uint8_t  f22;            /* +0x16: ?                                   */
    uint8_t  pad1;           /* +0x17                                      */
    uint8_t  f24;            /* +0x18: bit field                           */
    uint8_t  rest[19];       /* +0x19: ?                                   */
};
_Static_assert(sizeof(struct apple80211_llw_params) == 44, "llw_params");

/* 4. TX_RATE (SET 411) */
struct apple80211_tx_rate {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t f8;             /* +0x08: ?                                   */
    uint16_t f12;            /* +0x0C: ?                                   */
    uint16_t pad0;           /* +0x0E                                      */
};
_Static_assert(sizeof(struct apple80211_tx_rate) == 16, "tx_rate");

/* 4. AWDL_PEER_TRAFFIC_REGISTRATION (SET 164) */
struct apple80211_awdl_peer_traffic_registration {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr mac;   /* +0x04: peer (null = another criterion)     */
    uint16_t f10;            /* +0x0A: ?                                   */
    uint32_t uuid_len;       /* +0x0C: != 0 mandatory                      */
    char     uuid[127];      /* +0x10: service name (strncmp)              */
    uint8_t  f143;           /* +0x8F: the PM sets it to 0                 */
    uint32_t f144;           /* +0x90: ?                                   */
    uint32_t f148;           /* +0x94: ?                                   */
    uint8_t  rest[16];       /* +0x98..+0xA7: no observed writes ?         */
    uint32_t enable;         /* +0xA8                                      */
    uint32_t f172;           /* +0xAC: ?                                   */
    uint16_t f176;           /* +0xB0: ?                                   */
    uint16_t f178;           /* +0xB2: ?                                   */
    uint8_t  rest2[4];       /* +0xB4                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_peer_traffic_registration) == 184, "traffic_reg");

/* 5. AWDL_PEER_SERVICE_REQUEST (GET/SET 120) */
struct apple80211_peer_service_request {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr peer;  /* +0x04                                      */
    uint8_t  delete_request; /* +0x0A                                      */
    uint8_t  aw_offset;      /* +0x0B                                      */
    uint32_t token;          /* +0x0C                                      */
    uint16_t data_len;       /* +0x10                                      */
    uint16_t expect_fragmented_response; /* +0x12                          */
    /* flexible payload follows (Swift: `data` field)                       */
};
_Static_assert(sizeof(struct apple80211_peer_service_request) == 20, "peer_service_request");

/* 5. AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT (GET/SET 127) */
struct apple80211_awdl_availability_window_ap_alignment {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: PM ivar +37200; -1 = keep           */
    uint32_t f8;             /* +0x08: PM ivar +37196                      */
    uint32_t f12;            /* +0x0C: 0 in the getter                     */
};
_Static_assert(sizeof(struct apple80211_awdl_availability_window_ap_alignment) == 16, "aw_ap_align");

/* 5. AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT (GET/SET 128) */
struct apple80211_awdl_sync_frame_ap_alignment {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint32_t f8;             /* +0x08: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_frame_ap_alignment) == 12, "sf_ap_align");

/* 5. AWDL_SERVICE_PARAMS (GET/SET 119) */
struct apple80211_service_parameters {
    uint32_t version;        /* +0x00: ?                                   */
    uint16_t f4;             /* +0x04                                      */
    uint16_t pad0;           /* +0x06                                      */
    uint32_t f8;             /* +0x08                                      */
    uint64_t f12;            /* +0x0C                                      */
    uint64_t f20;            /* +0x14                                      */
    uint64_t f28;            /* +0x1C                                      */
    uint64_t f36;            /* +0x24                                      */
} __attribute__((packed));   /* u64 at +0x0C unaligned in the kernel       */
_Static_assert(sizeof(struct apple80211_service_parameters) == 44, "service_parameters");

/* 5. AWDL_OUI (GET/SET 131) */
struct apple80211_awdl_oui {
    uint32_t version;        /* +0x00                                      */
    uint8_t  oui[3];         /* +0x04                                      */
    uint8_t  pad0;           /* +0x07                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_oui) == 8, "awdl_oui");

/* 5. AWDL_TOP_MASTER (GET/SET 133) */
struct apple80211_awdl_top_master {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t election_id;    /* +0x08                                      */
    uint8_t  pad0[6];        /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_top_master) == 16, "top_master");

/* 5. AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME (GET/SET 138) */
struct apple80211_awdl_sync_frame_packet_lifetime {
    uint32_t version;        /* +0x00                                      */
    uint32_t lifetime;       /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_frame_packet_lifetime) == 8, "psf_lifetime");

/* 5. AWDL_MASTER_MODE_SYNC_FRAME_PERIOD (GET/SET 139) */
struct apple80211_awdl_master_sync_frame_period {
    uint32_t version;        /* +0x00                                      */
    uint32_t period;         /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_master_sync_frame_period) == 8, "msfp");

/* 5. AWDL_STRATEGY (GET/SET 148) */
struct apple80211_awdl_strategy {
    uint32_t version;        /* +0x00                                      */
    uint32_t strategy;       /* +0x04                                      */
    uint32_t param;          /* +0x08                                      */
    uint32_t pad0;           /* +0x0C                                      */
    uint64_t lastStateChange;/* +0x10                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_strategy) == 24, "awdl_strategy");

/* 5. AWDL_SOCIAL_TIME_SLOTS (GET/SET 163) */
struct apple80211_awdl_social_time_slots {
    uint32_t version;        /* +0x00                                      */
    uint8_t  enabled;        /* +0x04                                      */
    uint8_t  pad0[3];        /* +0x05                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_social_time_slots) == 8, "social_time_slots");

/* 5. AWDL_FORCED_ROAM_CONFIG (SET 167) */
struct apple80211_awdl_forced_roam_config {
    uint32_t version;        /* +0x00: ?                                   */
    struct ether_addr peer;  /* +0x04                                      */
    uint8_t  f10[6];         /* +0x0A: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_forced_roam_config) == 16, "forced_roam");

/* 5. AWDL_BTLE_STATE_PARAMS (GET 202) */
struct apple80211_awdl_btle_state_params {
    uint32_t version;        /* +0x00: ?                                   */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t f8;             /* +0x08                                      */
    uint16_t f10;            /* +0x0A                                      */
    uint32_t f12;            /* +0x0C                                      */
    uint64_t f16;            /* +0x10                                      */
    uint32_t f24;            /* +0x18                                      */
} __attribute__((packed));   /* no rounding to 8 (sizeof = 28)             */
_Static_assert(sizeof(struct apple80211_awdl_btle_state_params) == 28, "btle_state");

/* 5. AWDL_BTLE_PEER_INDICATION (SET 201) */
struct apple80211_awdl_btle_peer_indication_data {
    uint32_t version;        /* +0x00: ?                                   */
    uint32_t state;          /* +0x04                                      */
    struct ether_addr peer;  /* +0x08                                      */
    uint8_t  rest[18];       /* +0x0E: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_btle_peer_indication_data) == 32, "btle_peer_ind");

/* 5. AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS (SET 204) */
struct apple80211_awdl_btle_enable_synch_params {
    uint32_t version;        /* +0x00                                      */
    uint32_t enable;         /* +0x04                                      */
    uint8_t  rest[12];       /* +0x08: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_btle_enable_synch_params) == 20, "btle_sync");

/* 5. AWDL_OPER_MODE (GET/SET 217) */
struct apple80211_awdl_oper_mode {
    uint32_t version;        /* +0x00                                      */
    uint32_t mode;           /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_oper_mode) == 8, "oper_mode");

/* 5. AWDL_BLOCK_SET_COMMANDS (GET/SET 233) */
struct apple80211_awdl_block_set_commands {
    uint32_t version;        /* +0x00                                      */
    uint32_t mask;           /* +0x04: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_block_set_commands) == 8, "block_set");

/* 5. AWDL_PREFERRED_CHANNELS (GET 244) */
struct apple80211_awdl_info_ext {
    uint16_t secondary_preferred;    /* Swift type dump                    */
};
struct apple80211_awdl_preferred_channels {
    uint32_t version;        /* +0x00                                      */
    uint16_t preferred;      /* +0x04                                      */
    uint16_t master;         /* +0x06                                      */
    struct apple80211_awdl_info_ext awdl_info_ext; /* +0x08                */
    uint16_t pad0;           /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_preferred_channels) == 12, "pref_chan");

/* 5. AWDL_NEARBY_LOG_TRIGGER (SET 253) */
struct apple80211_awdl_nearby_log_trigger_data {
    uint32_t version;        /* +0x00                                      */
    uint16_t trigger;        /* +0x04                                      */
    uint8_t  pad0[2];        /* +0x06                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_nearby_log_trigger_data) == 8, "nearby_log");

/* 5. AWDL_COMMON_CHANNEL (GET 249) */
struct apple80211_awdl_common_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_common_channel) == 8, "common_channel");

/* 5. POST_PRESENCE_DONE (SET 415) */
struct apple80211_awdl_ppp_done {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr peerMac; /* +0x04                                    */
    uint8_t  pad0[2];        /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_ppp_done) == 12, "ppp_done");

/* 5. AWDL_MI_BITMAP (GET/SET 416) */
struct apple80211_awdl_mi_bitmap {
    uint32_t version;        /* +0x00                                      */
    uint32_t bitmap;         /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_mi_bitmap) == 8, "mi_bitmap");

/* 5. AWDL_UMI_TX_PEER (SET 417) */
struct apple80211_awdl_UMI_data {
    uint32_t version;        /* +0x00                                      */
    uint32_t umiOption;      /* +0x04                                      */
    struct ether_addr peer;  /* +0x08                                      */
    uint8_t  pad0[2];        /* +0x0E                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_UMI_data) == 16, "umi_data");

/* 5. LTE_RESTRICTED_CHANNELS (SET 419) */
struct apple80211_lte_restricted_channels {
    uint32_t version;        /* +0x00: ?                                   */
    uint16_t channels[64];   /* +0x04                                      */
    uint32_t count;          /* +0x84                                      */
};
_Static_assert(sizeof(struct apple80211_lte_restricted_channels) == 136, "lte_restricted");

/* 5. NANPH_SOFTAP_CSA (SET 420) */
typedef uint8_t apple80211_nanph_softap_csa;    /* len == 1               */

/* 5. AWDL_CTL (GET/SET 162; internal buffer 0x3B8A0) */
struct apple80211_awdl_ctl {
    uint32_t version;        /* +0x00: ?                                   */
    char     cmd[128];       /* +0x04: parameter name                      */
    uint8_t  pad0[4];        /* +0x84                                      */
    uint64_t value;          /* +0x88 (136)                                */
    uint8_t  mid[243716];    /* +0x90..+0x3B7D3: work area ?               */
    uint32_t out;            /* +0x3B7D4 (243860): output accumulator      */
    uint8_t  pad1[8];        /* +0x3B7D8                                   */
    void    *callback;       /* +0x3B7E0 (243872): required kernel ptr     */
    uint32_t tag;            /* +0x3B7E8 (243880): == 1                    */
    uint32_t f243884;        /* +0x3B7EC: ?                                */
};
_Static_assert(sizeof(struct apple80211_awdl_ctl) == 243888, "awdl_ctl");
/* NOTE: the len required from the client is 243872; the callback/tag/f    */
/* fields sit behind and the internal route fills them (buffer 0x3B8A0).   */
```

## 8. Method and limits

- Headless decompilation (Hex-Rays, `idat -A -S`) over copies of the IDBs in
  `Build/ida_awdl/task_awdl_structs/kc/` (kernel) and `/p2p/` (wifip2pd); dumps:
  `kc_proxi.txt` (64 ProximityInterface methods), `kc_pm_get.txt`/`kc_pm_set.txt` (101 methods of
  `IO80211AWDLPeerManager`), `kc_handlers.txt` (95 modern GET/SET handlers resolved from the
  tables), `kc_impl.txt`/`kc_impl2.txt` (Broadcom implementations and dispatcher), `kc_tables.txt`
  (2×586 entries with slot VA), `p2p_decomp.txt` (7 functions of the daemon), `p2p_disasm_key.txt`
  (flow of gate 12 and of payload 94), `p2p_jumptable.txt` (role table of `sub_100047660`).
- The field offsets come from writes/reads of the struct parameter in the decompilations; the
  names, from the error strings of Broadcom, from the Swift type dump of wifip2pd
  (`Build/ida_awdl/awdl_callers_probe.log`) and from the mangled names. The u16→u32 extraction of
  the block of `getAWDL_SYNC_PARAMS` is verified in disassembly (`0xffffff800171b395`–
  `0xffffff800171b3ee`).
- Explicit `?`: meaning of the marked fields; field +0 of `apple80211_virt_if_create_data`
  (Broadcom does not read it); the internal breakdown of `apple80211_key`, `apple80211_awdl_dev_stats`,
  `apple80211_awdl_statistics`, `apple80211_awdl_low_latency_statistics`,
  `apple80211_p2p_airplay_ext_stats`, `apple80211_awdl_peer_database`,
  `apple80211_traffic_registration` and the structs of section 6; the exact mechanics of the
  0x3B8A0 buffer of `AWDL_CTL`; the sizes of the structs without table rows (section 6); the catalog of
  values of `apple80211_encrypt` and `awdl_oper_mode` (the latter with a name table at
  `syms.txt:6114` not decompiled); the link of the `IO80211AWDLPeerManager` with an interface of
  an external driver (`AWDL_APPLE80211_DISPATCH.md` §8.3).
- The sizes marked with `_Static_assert` are backed by the `len == N` of the modern handler.
  Three kernel structs carry fields without natural alignment (u32 at +0x16 in
  `election_params`, u64 at +0x0C in `service_parameters`, u64 at +0x10 without rounding to 8 in
  `btle_state_params`): they are declared `packed`; the rest uses natural alignment (`awdl_peer_advert_add`
  places `ms` at +0x14 aligned). In `apple80211_awdl_bssid` the handler requires 16 and the struct is 16;
  for the kext it suffices to respect `len == 16` and the reads of the PM.
- No file of the repository was modified outside `Docs/AWDL_STRUCTS.md` and the work files of
  `Build/ida_awdl/task_awdl_structs/`.
