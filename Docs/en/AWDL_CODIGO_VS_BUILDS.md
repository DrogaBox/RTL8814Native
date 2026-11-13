# Compatibility of the RTL8814Native AWDL code against macOS builds

Evaluation of the AWDL layer code (`Source/AirLayer/AWDLInterface.cpp` and
`Source/AirLayer/AWDLInterface.h`), originally validated on macOS 26.7.1 (build 25G241), against
the macOS 26.7.2 (build 25G332) Kernel Collection and the distance to the previous versions
macOS 14.8.9 (build 23J631, Sonoma) and macOS 15.8.2 (build 24H232, Sequoia), all for the x86_64
architecture.

---

## 1. Overview and verdict per build

| Build | OS version | Verdict | Summary |
|---|---|---|---|
| **25G241** | macOS 26.7.1 | Baseline | Original validated code. 131/131 symbols OK, 157/157 vtable slots OK, 61/61 offsets OK. |
| **25G332** | macOS 26.7.2 | **Works as-is** | 131/131 symbols OK (0 missing). 157/157 vtable slots OK (0 mismatches). 48/61 offsets OK and 0 moved; the 13 rows of kernel-unsymbolized functions by searching the pattern across the whole kernel (12 with the same number of matches as in 25G241, 1 generic `?`). Tokens 129/132/205, event payloads 41/43, the dual-band gate and the presence/PPP offsets 100 % identical. |
| **25G342** | macOS 26.7.3 | **Works as-is** | Same kernel and same `IO80211Family` as 25G332 (equal UUIDs; 3 of the 204 Boot KC extensions change, all compression). Checked on 2026-11-10 with that day's code (§3.6). |
| **24H232** | macOS 15.8.2 | **Requires separate binary and header** | 129/131 symbols OK (`flushFlowQueues` and `signalMulticastPackets` missing). Cumulative shift of -0x20 in vtable (`sendPendingPacketsToStack` at `0xF10` vs `0xF30`; `inputPacket` at `0xB48` vs `0xB50`; pure `setMacAddress` at `0xCF8` vs `0xF40`; the AWDL block of the vtable -0x28, `getSSID` at `0xFA0` vs `0xFC8`). Capabilities block at `ctrl+0x120 + 0xB20` (delta -0x10). Presence offsets shifted. |
| **23J631** | macOS 14.8.9 | **Requires separate binary and header** | 121/131 symbols OK (10 absent). Massive vtable shift (-0x78 in `IO80211VirtualInterface`, -0xA0 in AWDL methods: `getSSID` at `0xF28` vs `0xFC8`). `inputPacket` signature with 4 arguments (without the final boolean). Interface object size `0x120` (instead of `0x130`) `?`. No centralized capabilities buffer in the controller (AWDL reads at `sub+0xB18`). Presence offsets shifted. |

---

## 2. Comparative tables per tool

### 2.1 Imported and required symbols (`Tools/awdl_recheck_symbols.py`)

Cross-check of the 131 requirements extracted from the sources (`AWDLInterface.cpp`,
`AWDLInterface.h`, `RTLAirLayer.cpp`): 37 direct assembler labels (`__asm`) and 94 virtual methods of
`IO80211VirtualInterface` resolved at load time (pure `setMacAddress` is omitted).

| Requirement / Symbol | 25G241 | 25G332 | 23J631 | 24H232 | Note |
|---|---|---|---|---|---|
| **Total requirements analyzed** | 131 | 131 | 131 | 131 | AirLayer layer requirements |
| **Defined symbols (OK)** | 131 | 131 | 121 | 129 | Link status |
| **Missing symbols (MISSING)** | 0 | 0 | 10 | 2 | Methods that do not exist in the version |
| `IO80211VirtualInterface::flushFlowQueues` | OK | OK | MISSING | MISSING | Slot `0xED8` on 26; purge of per-peer-MAC queues does not exist on 14/15 |
| `IO80211VirtualInterface::signalMulticastPackets` | OK | OK | MISSING | MISSING | Slot `0xF28` on 26; multicast signalling does not exist on 14/15 |
| `IO80211VirtualInterface::getPeerDataStats` | OK | OK | MISSING | OK | Slot `0xEA8` on 26 (`0xEA0` on 15); introduced in macOS 15 |
| `IO80211VirtualInterface::clearPeerDataStats` | OK | OK | MISSING | OK | Slot `0xEB0` on 26 (`0xEA8` on 15); introduced in macOS 15 |
| `IO80211VirtualInterface::getPeerSlotDataStats` | OK | OK | MISSING | OK | Slot `0xEA0` on 26 (`0xE98` on 15); introduced in macOS 15 |
| `IO80211VirtualInterface::setAwdlCurrentChannelSequenceIndex` | OK | OK | MISSING | OK | Slot `0xEB8` on 26 (`0xEB0` on 15); introduced in macOS 15 |
| `IO80211VirtualInterface::findPeer` | OK | OK | MISSING | OK | Slot `0xCD0` on 26 (`0xCC8` on 15); does not exist as virtual on 14 |
| `IO80211Controller::postMessage` (AirLayer signature) | OK | OK | MISSING | OK | Signature not exported on 14; in AWDL the vif one is used (`0xB18`) |
| `IO80211FaultReporter::allocWithParams` | OK | OK | MISSING | OK | Constructor absent on 14; on 14 there is a direct method at slot 436 |
| `IO80211InfraInterface::registerInfraEthernetInterface` | OK | OK | MISSING | OK | Ethernet interface registration on 14 uses `registerEthernetInterface` |
| Direct `extern "C"` symbols of `AWDLInterface.cpp` (27) | OK | OK | OK | OK | All direct links (Skywalk, queues, bpf) exist on the four builds |

---

### 2.2 Vtable slots (`Tools/awdl_recheck_vtable.py`)

Cross-check of the slots declared in `Source/AirLayer/AWDLInterface.h` against the vtable dumps
(`vt_AppleBCMWLANProximityInterface.txt` and `vt_IO80211VirtualInterface.txt`). In
`IO80211AWDLProtocol` the 63 AWDL cells are pure (`___cxa_pure_virtual`);
`AppleBCMWLANProximityInterface` implements all the concrete cells.

| Slot declared in header | Method | 25G241 | 25G332 | 23J631 | 24H232 | Note on the drift |
|---|---|---|---|---|---|---|
| `0x428` | `configureReport` | 0x428 OK | 0x428 OK | 0x428 OK | 0x428 OK | Identical on the 4 builds |
| `0x5C0` | `start` | 0x5C0 OK | 0x5C0 OK | 0x5C0 OK | 0x5C0 OK | Identical on the 4 builds |
| `0x600` | `terminate` | 0x600 OK | 0x600 OK | 0x600 OK | 0x600 OK | Identical on the 4 builds |
| `0x8E8` | `prepareBSDInterface` | 0x8E8 OK | 0x8E8 OK | 0x8D8 | 0x8E8 OK | On 14 shifted by a smaller number of previous methods |
| `0x910` | `processBSDCommand` | 0x910 OK | 0x910 OK | 0x900 | 0x910 OK | Same |
| `0x930` | `setInterfaceEnable` | 0x930 OK | 0x930 OK | 0x918 | 0x928 | On 25 `setRxFlowSteering` was inserted at `0x928` |
| `0x9B8` | `getFeatureFlags` | 0x9B8 OK | 0x9B8 OK | 0x9A0 | 0x9B0 | Delta -0x8 on 15 |
| `0xB08` | `createPeerManager` | 0xB08 OK | 0xB08 OK | 0xB08 OK | 0xB00 | Delta -0x8 on 15; on 14 same as on 26 |
| `0xB18` | `postMessage` | 0xB18 OK | 0xB18 OK | 0xB10 | 0xB10 | Delta -0x8 on 14 and 15 (on 26 `createPeer` was inserted at `0xB10`) |
| `0xB48` / `0xB50` | `inputPacket` | **0xB50** OK | **0xB50** OK | **0xB48** | **0xB48** | On 14 and 15 it is at `0xB48`. On 14 the signature has 4 args (no bool) |
| `0xB88` | `linkState` | 0xB88 OK | 0xB88 OK | 0xB88 OK | 0xB88 OK | Removal of `setLinkState` on 25 neutralized the delta |
| `0xE60` / `0xE68` | `init` | **0xE68** OK | **0xE68** OK | **0xDF0** | **0xE60** | Start of the virtual interface own methods |
| `0xCF8` / `0xF40` | `setMacAddress` | **0xF40** pure | **0xF40** pure | absent | **0xCF8** pure | On 15 it is a pure virtual at `0xCF8`; on 26 it moved to `0xF40` |
| `0xF10` / `0xF30` | `sendPendingPacketsToStack` | **0xF30** OK | **0xF30** OK | **0xE80** | **0xF10** | On 15 delta -0x20; on 14 delta -0xB0 |
| `0xF18` / `0xF38` | `freePendingPackets` | **0xF38** OK | **0xF38** OK | **0xE88** | **0xF18** | On 15 delta -0x20; on 14 delta -0xB0 |
| `0xF28` / `0xFC8` | `getSSID` (AWDL start) | **0xFC8** OK | **0xFC8** OK | **0xF28** | **0xFA0** | Start of the AWDL slots; on 14 -0xA0, on 15 -0x28 |
| `0x10B0` | `setAWDL_SYNC_PARAMS` | 0x10B0 OK | 0x10B0 OK | 0x1010 | 0x1088 | AWDL ioctl 116 slot; on 14 delta -0xA0, on 15 -0x28 |
| `0x10E0` | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 0x10E0 OK | 0x10E0 OK | 0x1040 | 0x10B8 | AWDL ioctl 129 slot; on 14 delta -0xA0, on 15 -0x28 |
| `0x10E8` | `setAWDL_MASTER_CHANNEL` | 0x10E8 OK | 0x10E8 OK | 0x1048 | 0x10C0 | AWDL ioctl 132 slot; on 14 delta -0xA0, on 15 -0x28 |
| `0x1130` | `setAWDL_SECONDARY_MASTER_CHANNEL` | 0x1130 OK | 0x1130 OK | 0x1090 | 0x1108 | AWDL ioctl 205 slot; on 14 delta -0xA0, on 15 -0x28 |
| `0x11B8` | `setAWDL_RTG_PEER_STATS_CONFIG` | 0x11B8 OK | 0x11B8 OK | 0x1110 | 0x1190 | Last AWDL cell; on 14 -0xA8, on 15 -0x28 |
| **ProxInterface summary** | 157 header slots | **157 OK** | **157 OK** | 22 OK, 117 shifted | 23 OK, 132 shifted | 25G332 matches 100 %; 14 and 15 require remapping |

---

### 2.3 Version-sensitive offsets (`Tools/awdl_recheck_offsets.py`)

Cross-check of the 61 pattern and instruction rows of `Tools/awdl_offsets_25G241.tsv` across the
four kernel collections. On 25G332 the tool reports 48 OK, 0 moved and 13 skipped: the 13 are
`com.apple.kernel` functions without symbol, which the tool only resolves with `--va-hint` on the
same build. Searching each of those patterns in the whole kernel code, 12 appear the same number
of times on 25G332 as on 25G241, only shifted (e.g. `IFEF_TXSTART` 5 and 5, `+0x330` 2 and 2,
`nll_state = 2` 7 and 7); the one of `ifclassq+0x68` is too generic to conclude (1262 versus
1255) `?`. The 14 and 15 columns come from `Tools/awdl_offsets_23J631.tsv` and
`awdl_offsets_24H232.tsv`.

| Group / Structure | Field | 25G241 | 25G332 | 23J631 | 24H232 | Note on the drift |
|---|---|---|---|---|---|---|
| `reginfo/RegistrationInfo` | `+0x04` size >= 0x108 | `0x4` | `0x4` | `0x4` | `0x4` | Identical |
| `reginfo/RegistrationInfo` | `+0x3C` headroom (`getTxDataOffset`) | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Identical |
| `reginfo/RegistrationInfo` | `+0x3E` start read | `0x3e` | `0x3e` | `0x3e` | `0x3e` | Identical |
| `reginfo/RegistrationInfo` | `+0x48/+0x4C` MTU pair | `0x48` | `0x48` | `0x48` | `0x48` | Identical |
| `reginfo/RegistrationInfo` | `+0x108` MAC | `0x108` | `0x108` | `0x108` | `0x108` | Identical |
| `eparams/ifnet_init_eparams` | `+0x00` version == 2 | `0x0` | `0x0` | `0x0` | `0x0` | Identical |
| `eparams/ifnet_init_eparams` | `+0x04` size == 0x158 | `0x158` | `0x158` | `0x158` | `0x158` | Identical |
| `eparams/ifnet_init_eparams` | `+0x144` tx data offset | `0x144` | `0x144` | `0x144` | `0x144` | Identical in structure |
| `eparams/ifnet_init_eparams` | `+0x146` alloc read | `0x146` | `0x146` | `0x146` | `0x146` | Identical |
| `ifnet/ifnet` | `+0x78` `if_refflags` (& 7 == 2) | `0x78` | `0x78` | `0x78` | `0x78` | Identical |
| `ifnet/ifnet` | `+0xBE` `if_flags` (`IFF_UP` gate) | `0xbe` | `0xbe` | `0xbe` | `0xbe` | Identical |
| `ifnet/ifnet` | `+0xC0` `IFEF_TXSTART` (enqueue gate) | `0xc0` | `0xc0` | `0xc0` | `0xc0` | Identical |
| `ifnet/ifnet` | `+0xC0` `IFEF_TXSTART` (thread gate) | `0xc0` | `0xc0` | `0xc0` | `0xc0` | Identical |
| `ifnet/ifnet` | `+0xC0` `IFEF_TXSTART` (write) | `0xc0` | `0xc0` | `0xc0` | `0xc0` | Identical |
| `ifnet/ifnet` | `+0xC3` `IFEF_SKYWALK_NATIVE` | `0xc3` | `0xc3` | `0xc3` | `0xc3` | Identical |
| `ifnet/ifnet` | `+0x248` `if_output` (`_dlil_output_handler`) | `0x248` | `0x248` | **`0x1c0`** | **`0x228`** | Delta -0x88 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x250` `if_start` (`_ifp_if_start`) | `0x250` | `0x250` | **`0x1c8`** | **`0x230`** | Delta -0x88 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x258` `output_handler` (stack jump) | `0x258` | `0x258` | **`0x1d0`** | **`0x238`** | Delta -0x88 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x258` `output_handler` (CAS) | `0x258` | `0x258` | **`0x1d0`** | **`0x238`** | Delta -0x88 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x260` output registration (write) | `0x260` | `0x260` | **`0x1d8`** | **`0x240`** | Delta -0x88 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x260` output registration (read) | `0x260` | `0x260` | **`0x1d8`** | **`0x240`** | Delta -0x88 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x598` default `if_start` | `0x598` | `0x598` | `0x598` | **`0x578`** | Delta -0x20 on 15 |
| `ifnet/ifnet` | `+0x5A0` default handler (write) | `0x5a0` | `0x5a0` | **`0x510`** | **`0x580`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x5A0` default handler (anchor) | `0x5a0` | `0x5a0` | **`0x510`** | **`0x580`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x300` bit 1 (thread running) | `0x300` | `0x300` | **`0x270`** | **`0x2e0`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x304` startup counter | `0x304` | `0x304` | **`0x274`** | **`0x2e4`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x309` gate byte | `0x309` | `0x309` | **`0x279`** | **`0x2e9`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x330` start thread (panic) | `0x330` | `0x330` | **`0x2a0`** | **`0x310`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x330` start thread (`assert_wait`) | `0x330` | `0x330` | **`0x2a0`** | **`0x310`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x338` `ifclassq` (start thread) | `0x338` | `0x338` | **`0x2a8`** | **`0x318`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x338` `ifclassq` (registration) | `0x338` | `0x338` | **`0x2a8`** | **`0x318`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x58C` tx data offset (write) | `0x58c` | `0x58c` | **`0x4fc`** | **`0x56c`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x58E` sibling (write) | `0x58e` | `0x58e` | **`0x4fe`** | **`0x56e`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x590` u32 (write) | `0x590` | `0x590` | **`0x500`** | **`0x570`** | Delta -0x90 on 14; -0x20 on 15 |
| `ifnet/ifnet` | `+0x900` embedded netem queue | `0x900` | `0x900` | `0x900` | `0x900` | Identical |
| `ifclassq/ifclassq` | `+0x4C` purge bit 1 | `0x4c` | `0x4c` | `0x4c` | `0x4c` | Identical |
| `ifclassq/ifclassq` | `+0x68` discipline table | `0x68` | `0x68` | `0x68` | `0x68` | Identical |
| `ifclassq/discipline` | table+`0x18` enqueue operation | `0x18` | `0x18` | `0x18` | `0x18` | Identical |
| `ifclassq/ifclassq` | `+0x5C` limit 0x800 | `0x5c` | `0x5c` | `0x5c` | `0x5c` | Identical |
| `llink/netif_llink` | `+0x3C` `nll_state = 1` (registration) | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Identical |
| `llink/netif_llink` | `+0x3C` `nll_state = 2` (destruction) | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Identical |
| `llink/netif_llink` | `+0x3C == 2` ENXIO gate | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Identical |
| `vtable/IOSkywalkNetworkInterface` | `+0x9C0` `getTxDataOffset` | `0x9c0` | `0x9c0` | **`0x9a8`** | **`0x9b8`** | Delta -0x18 on 14; -0x8 on 15 |
| `vtable/IOSkywalkEthernetInterface` | `+0x8E0` `initBSDInterfaceParameters` | `0x8e0` | `0x8e0` | **`0x8d0`** | `0x8e0` | Delta -0x10 on 14 |
| `vtable/IOSkywalkNetworkInterface` | `+0x8E8` `prepareBSDInterface` | `0x8e8` | `0x8e8` | **`0x8d8`** | `0x8e8` | Delta -0x10 on 14 |
| `vtable/IOService` | `+0x600` `terminate` | `0x600` | `0x600` | `0x600` | `0x600` | Identical |
| `vtable/IOService` | `+0x5B0` `registerService` | `0x5b0` | `0x5b0` | `0x5b0` | `0x5b0` | Identical |
| `vtable/IOSkywalkBSDClient` | `+0x5D0` `start` | `0x5d0` | `0x5d0` | `0x5d0` | `0x5d0` | Identical |
| `vtable/IOSkywalkInterface` | `+0x5D8` `stop` | `0x5d8` | `0x5d8` | `0x5d8` | `0x5d8` | Identical |
| `vtable/OSOrderedSet` | `+0x1D8` `lastObject` | `0x1d8` | `0x1d8` | **`0x1e0`** | `0x1d8` | `getLastObject` on 14 |
| `vtable/OSOrderedSet` | `+0x1F8` `getObject` | `0x1f8` | `0x1f8` | `0x1f8` | `0x1f8` | Identical |
| `vtable/IO80211VirtualInterface` | `+0xE68` `init` (super) | `0xe68` | `0xe68` | **`0xdf0`** | **`0xe60`** | Start of virtual interface methods |
| `vtable/AppleBCMWLANSkywalkInterface` | `+0x14E0` `start` | `0x14e0` | `0x14e0` | `0x14e0` | **`0x1478`** | BCM slot delta on 15 |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0xE8` interface | `0xe8` | `0xe8` | `0xe8` | `0xe8` | Identical |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0xF0` RegInfo copy | `0xf0` | `0xf0` | `0xf0` | `0xf0` | Identical |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0xF8` `ifnet` pointer | `0xf8` | `0xf8` | `0xf8` | `0xf8` | Identical |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0x13C` power/wake state | `0x13c` | `0x13c` | **`0x120`** | **`0x120`** | Delta -0x1C on 14 and 15 |
| **Summary of matches against 25G241** | 61 accesses | **61 OK** | **48 OK + 12 by pattern + 1 `?`** | 20 OK, 27 moved/nf | 28 OK, 20 moved/nf | 25G332 shows no drift at all |

---

## 3. Manual checks on 25G332 and cross-version cross-check

### 3.1 Base of the card capabilities block (`CARD_CAPABILITIES`)

On macOS 26 and 15 the controller stores the 28-byte response of ioctl 12 (`CARD_CAPABILITIES`)
in an indexed block; on macOS 14 the controller does not store this block and each client requests
or caches its own fields.

- **macOS 26.7.2 (25G332):**
  - Function: `IO80211Controller::loadCardCapabilities` (`0xffffff800229f4f6`).
  - Write: stores at `*(this+0x120) + 0xB30` (u32 version) and `+0xB34..+0xB4B` (24 bytes of
    capabilities `p[4..27]`).
  - Instruction heads:
    - `0xffffff800229f52a`: `mov ecx, 0B30h`
    - `0xffffff800229f52f`: `add rcx, [r14+120h]`
    - `0xffffff800229f53e`: `mov r8d, 1Ch`
    - `0xffffff800229f563`: `mov esi, 0B30h`
    - `0xffffff800229f568`: `add rsi, [r14+120h]`
  - Behavior identical to that of 25G241 (`0xffffff800229a202`).
- **macOS 15.8.2 (24H232):**
  - Function: `IO80211Controller::loadCardCapabilities` (`0xffffff800222cdf0`).
  - Write: stores at `*(this+0x120) + 0xB20` (constant delta of -0x10 relative to macOS 26).
- **macOS 14.8.9 (23J631):**
  - `loadCardCapabilities` does not exist in `IO80211Controller`.
  - Instead, `IO80211AWDLPeerManager::configure` (`0xffffff80020b55e6`) issues the ioctl 12
    (`0xC02869C9`) directly and stores the 28 bytes in its own internal substructure at
    `sub + 0xB18` (`sub = *(this+0x18)`), evaluating `sub+0xB1C` (`p[4]`).
    `IO80211InfraInterface::initCapabilities` (`0xffffff8002204bec`) stores 16 bytes in its local
    ivars `+28..43`.

---

### 3.2 Dual-band gate (`p[7]` bit `0x20` / capability 29)

The `0x20` bit of the corresponding capability byte (`p[7]` in the driver buffer; byte 3 after
stripping the 4-byte version header) is essential for the AWDL manager to accept social channels
in the 5 GHz band (149 and 44).

| Build | Reader function | VA instruction head | Verified instruction | Effect if the bit is 0 |
|---|---|---|---|---|
| **25G241** | `configureValidAWDLChannels` | `0xffffff80021cab1e` | `test byte ptr [rax+0B37h], 20h` | Nulls 5 GHz channels at `+0x4294`; log *"Dual band not supported..."* |
| **25G332** | `configureValidAWDLChannels` | `0xffffff80021cfd06` | `test byte ptr [rax+0B37h], 20h` | Identical to 25G241 |
| **25G332** | `handleAssistedDiscoveryTrafficReg` | `0xffffff800220b0f4` | `test byte ptr [rax+0B37h], 20h` | Denies 5 GHz social in assisted discovery |
| **25G332** | `fillLegacyDataPathPayload` | `0xffffff80022195e3` | `test byte ptr [rsi+0B37h], 20h` | TLV 12 flags: `0x8B03` instead of `0x8B23` |
| **24H232** | `configureValidAWDLChannels` | `0xffffff8002169030` | `test byte ptr [rax+0B27h], 20h` | Identical effect at `+0xB27` (delta -0x10) |
| **23J631** | `configureValidAWDLChannels` | `0xffffff80020bb433` | `test byte ptr [rax+0B1Fh], 20h` | Identical effect at `sub+0xB1F` (`sub = *(this+0x18)`) |

---

### 3.3 Tokens 129 / 132 / 205 and sequence layout

- **Token 129 (`APPLE80211_IOC_AWDL_SYNC_CHANNEL_SEQUENCE`):**
  - Value: `0x81802869C8`.
  - Built and issued in `IO80211AWDLPeerManager::setChannelSequenceUnconditional`:
    - On 25G332: `0xffffff80021d12a0` (`mov rax, 81802869C8h`).
    - On 25G241: `0xffffff80021cc0b8`.
    - On 24H232: `0xffffff800216a44a`.
    - On 23J631: `0xffffff80020bc6cc`.
  - Payload size: 400 bytes (`0x190` at `0xffffff80021d08d4` and `memcmp` check of `0x190` bytes
    at `0xffffff80021d0ae9`).
  - Header and slots layout (16 entries of 12 bytes):
    - `+0x00`: version (1)
    - `+0x04`: SDB flags (`0x10` ON, `0x20` OFF)
    - `+0x05`: number of slots (`n = 16`)
    - `+0x06`: encoding (`0` or `1`)
    - `+0x07`: step
    - `+0x08`: dup
    - `+0x09`: fill
    - `+0x0C + 12*i`: slot entry `i`: `+0` version (u32), `+4` channel (u32), `+8` flags (u32).
    - Loop copy verified on 25G332 at `0xffffff80021d096e` (`mov [r14+rcx+0Ch], rdi`) with an
      advance of 12 bytes (`add rcx, 0Ch` at `0xffffff80021d099b`).
  - The structure is 100 % coincident on the 4 versions.
- **Token 132 (`setAWDL_MASTER_CHANNEL`):**
  - Value: `0x84802869C8`. Issued in `programMasterChannels`:
    - On 25G332: `0xffffff80021d9bf9` (`mov rax, 84802869C8h`).
    - On 24H232: `0xffffff80021728c7`.
    - On 23J631: `0xffffff80020c456c`.
  - Payload: `apple80211_awdl_master_channel` (8 bytes).
- **Token 205 (`setAWDL_SECONDARY_MASTER_CHANNEL`):**
  - Value: `0xCD802869C8`. Issued in `programMasterChannels`:
    - On 25G332: `0xffffff80021d9c74` (`mov rax, 0CD802869C8h`).
    - On 24H232: `0xffffff8002172942`.
    - On 23J631: `0xffffff80020c4608`.
  - Payload: `apple80211_awdl_secondary_master_channel` (8 bytes).

---

### 3.4 Event payloads 41 and 43

Processed in `IO80211AWDLPeerManager::reportDataPathEvents`:
- On 25G332: entry point at `0xffffff80021e2adc`.
- On 25G241: entry point at `0xffffff80021dd8f4`.
- On 24H232: entry point at `0xffffff800217adb8`.
- On 23J631: entry point at `0xffffff80020ccaaa`.

#### Event 41 (`APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`)
- Payload: 36 bytes (`0x24`).
- Fields read in `reportDataPathEvents` and validated in `monitorTimeline`:
  - `+0x00` (`uint16_t`): number of AWs (`AwNumber`).
  - `+0x04` (`uint32_t`): sync state (`1` master, `2` slave).
  - `+0x08` (6 bytes): MAC address of the sync master.
  - `+0x0E` (6 bytes): MAC address of the top master.
  - `+0x14` (`uint32_t`): `FwAwStartTime` in microseconds, **big-endian** format.
  - `+0x18` (`uint32_t`): flags of the served channel slot.
  - `+0x1C` (`uint32_t`): served channel number.
  - `+0x20` (`uint32_t`): window flags (0).
- Dispatch to `monitorTimeline` on 25G332: instruction head at `0xffffff80021e4d25`.
- Match: 100 % on the 4 versions.

#### Event 43 (`APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_END`)
- Payload: 40 bytes (`0x28`).
- Actions in `reportDataPathEvents`:
  - On 25G332:
    - Evaluates election trigger: `0xffffff80021e40c8` (`call shouldRunElection`),
      `0xffffff80021e40d4` (`call runElection`).
    - Window availability close: `0xffffff80021e40f6` (`mov byte ptr [rdi+4280h], 0`).
    - Reading of CCA counters if the flag at `+0x10` is 1 (`0xffffff80021e411d: cmp word ptr [r14+10h], 1`).
    - Time base at `+0x18` converted to 0.1 µs scale (`0xffffff80021e412f: lea r9d, [rax+rax*4]`).
  - A payload full of zeros (as `AWDLInterface.cpp` sends it) closes the window and triggers
    `runElection` without processing CCA counters on the 4 versions.

---

### 3.5 Fifteen presence and PPP items

Exhaustive cross-check of the slots, ivars and thresholds defined in
`Tools/awdl_presence_offsets.tsv`. All addresses are instruction heads verified in disassembly.

| Item | Type | 25G241 (value / VA) | 25G332 (value / VA) | 24H232 (value / VA) | 23J631 (value / VA) | Note |
|---|---|---|---|---|---|---|
| `pm_llavalid` | ivar_offset | `0xFF1C` (`0x21d9b39`) | **`0xFF1C`** (`0xffffff80021ded21`) | `0xC70C` (`0xffffff8002177870`) | `0xC4A0` (`0xffffff80020c93db`) | AWDL ivars base = `this+0x20` |
| `pm_bonjourTriggersAWDL` | ivar_offset | `0xA718` (`0x21d9b0a`) | **`0xA718`** (`0xffffff80021decf2`) | `0x7FF8` (`0xffffff8002177841`) | `0x7FA8` (`0xffffff80020c93b1`) | Guard of the LLA block |
| `pm_stateflags` | ivar_offset | `0x3EE0` (`0x21a9339`) | **`0x3EE0`** (`0xffffff80021ae521`) | `0x3EE8` (`0xffffff800214928b`) | `0x3EC8` (`0xffffff800209b197`) | Quiet/association mode flags |
| `pm_llatimer` | ivar_offset | `0x28A0` (`0x21c209b`) | **`0x28A0`** (`0xffffff80021c7283`) | `0x3ED8` (`0xffffff8002160c24`) | `0x3EB8` (`0xffffff80020b2891`) | `IO80211TimerSource` for LLA |
| `pm_channelSequenceStrategy` | ivar_offset | `0x4` (`0x21d9bc2`) | **`0x4`** (`0xffffff80021dedaa`) | `0x4` (`0xffffff80021778f9`) | `0x4` (`0xffffff80020c9460`) | 1 `AwdlStateMachine`, 0 `FollowsInfra` |
| `pm_field_A66C` | ivar_offset | `0xA66C` (`0x21c16d7`) | **`0xA66C`** (`0xffffff80021c68bf`) | `0x7F4C` (`0xffffff8002160290`) | `0x7F04` (`0xffffff80020b1df7`) | Field in manual mode |
| `peer_pppdone_flag` | ivar_offset | `0x6DE` (`0x21a9705`) | **`0x6DE`** (`0xffffff80021ae8ed`) | `0x6AE` (`0xffffff8002149648`) | `0x6AE` (`0xffffff800209b538`) | Peer ivars base = `this+0x20` |
| `peer_earlyreturn_timestamp` | ivar_offset | `0x310` (`0x21a9342`) | **`0x310`** (`0xffffff80021ae52a`) | `0x2E8` (`0xffffff8002149294`) | `0x2E8` (`0xffffff800209b1a0`) | Early return mark |
| `vt_awdlpeer_monitorPeer` | vtable_slot | `0x180` (`0x245a110`) | **`0x180`** (`0xffffff8002460110`) | **`0x178`** (`0xffffff80023c9aa8`) | `0x180` (`0xffffff80022c9ed0`) | Cell `__ZTV15IO80211AWDLPeer` |
| `vt_vif_inputPacket` | vtable_slot | `0xB50` (`0x24736d8`) | **`0xB50`** (`0xffffff80024796d8`) | **`0xB48`** (`0xffffff80023dfea0`) | **`0xB48`** (`0xffffff80022dc6e0`) | Cell `__ZTV23IO80211VirtualInterface` |
| `vt_vif_sendPendingPacketsToStack` | vtable_slot | `0xF30` (`0x2473ab8`) | **`0xF30`** (`0xffffff8002479ab8`) | **`0xF10`** (`0xffffff80023e0268`) | **`0xE80`** (`0xffffff80022dca18`) | Dispatch of retained packets |
| `vt_vif_freePendingPackets` | vtable_slot | `0xF38` (`0x2473ac0`) | **`0xF38`** (`0xffffff8002479ac0`) | **`0xF18`** (`0xffffff80023e0270`) | **`0xE88`** (`0xffffff80022dca20`) | Discard on expiry |
| `call_pppDone_to_sendPendingPacketsToStack` | call_site | `0xF30` (`0x21bfc77`) | **`0xF30`** (`0xffffff80021c4e5f`) | `0xF10` (`0xffffff800215eb51`) | `0xE80` (`0xffffff80020b06eb`) | `call [rax+X]` in `pppDone` |
| `call_ageout_to_freePendingPackets` | call_site | `0xF38` (`0x21aa984`) | **`0xF38`** (`0xffffff80021afb6c`) | `0xF18` (`0xffffff800214a8e2`) | `0xE88` (`0xffffff800209c84e`) | `call [rax+X]` in `ageoutPacketsWaitingForPPP` |
| `ageout_threshold_ms` | immediate | `0x7D1` (`0x21aa8cf`) | **`0x7D1`** (`0xffffff80021afab7`) | `0x7D1` (`0xffffff800214a82d`) | `0x7D1` (`0xffffff800209c78b`) | Threshold of 2001 ms |

---

### 3.6 Check of 26.7.3 (25G342), 2026-11-10
The update the system offers on 2026-11-10 is 26.7.3 (25G342), not 26.7.2. Kernel Collections taken from the update
package, without installing it. Kernel `xnu-12377.161.15.701.48`, Darwin 25.6.0.
| Check | Result |
|---|---|
| UUID of each Boot KC extension, 25G332 against 25G342 | 201 of 204 equal, among them `com.apple.kernel`, `IO80211Family`, `IOSkywalkFamily`, `IONetworkingFamily`, `IOUSBHostFamily` and `IOBluetoothFamily`. `com.apple.kec.Compression` and two `AppleFSCompression` change. |
| UUID, 25G241 against 25G342 | 86 of 204 change, among them the kernel, `IO80211Family` and `IOUSBHostFamily`; `IOSkywalkFamily` and `IONetworkingFamily` do not. |
| `Tools/awdl_recheck_offsets.py` on the 25G342 Boot KC | 48 `OK`, 0 `MOVED`, 0 `NOT-FOUND`, 13 `SKIPPED` (functions with no symbol), same as on 25G332. |
| `Tools/awdl_recheck_symbols.py` and `Tools/awdl_recheck_vtable.py` with the sources of 2026-11-10 | 131 of 131 symbols and 157 of 157 slots (`AppleBCMWLANProximityInterface`), on the 25G332 dumps. |
| `IO80211PeerManager` counters the layer writes (`AWDL_PM_*`, added after the audit of 2026-10-31) | Read in IDA on the 25G332 database: ivars at `+0x18`, `+1968` and `+1988` in `skywalkLogTxPacket`, `+1948` and `+1992` in `skywalkInputPacket`, and the reads of `monitorAWDLDataMode`. Unchanged. |
| Size and superclass of the 7 classes of the version gate | Read in the metaclass constructors: `0xB0`, `0xE0`, `0x120`, `0x128`, `0x130`, `0x130`, `0x128`, those of `KnownBuilds.inc`. |
| Symbols imported by `RTL8814Native.kext` and `RTL8814AirLayer.kext` | The same ones resolve in the KCs of 25G241 and 25G342. |
The gate compares the build with a single string: with `rtlairtrust=25G332` the layer stays closed on 25G342. The boot
needs `rtlairtrust=25G342`. Not verified `?`: the daemons (`wifip2pd`, `sharingd`, `rapportd`, `bluetoothd`), which are
not in the Kernel Collections, and the layer running on 25G342.
---
## 4. List of exact changes each build would require

### 4.1 For macOS 26.7.2 (25G332)
**No code change.**
- `Source/AirLayer/AWDLInterface.cpp`: 0 changes needed.
- `Source/AirLayer/AWDLInterface.h`: 0 changes needed.
- The existing AirLayer binary build works directly on 25G332.
- At the level of the repository build authorization system (`Tools/airlayer_builds.txt`,
  `Source/AirLayer/KnownBuilds.inc`, `VersionGate.h`), the only thing that would correspond is to
  register the identifier `25G332` in the authorized kernels table if validation by whitelist is
  required.

---

### 4.2 For macOS 15.8.2 (24H232)
1. **Dedicated header (`AWDLInterface_15.h`)**:
   - Adjust the vtable slot offsets of `IO80211VirtualInterface`:
     - `createPeerManager`: `0xB00` (instead of `0xB08`).
     - `inputPacket`: `0xB48` (instead of `0xB50`).
     - `getP2PSkywalkPeerMgr`: `0xCB8` (instead of `0xCC0`).
     - `setMacAddress`: pure virtual method at slot `0xCF8` (instead of `0xF40`).
     - `init`: `0xE60` (instead of `0xE68`).
     - `handleIoctl`: `0xE78` (instead of `0xE80`).
     - `setAwdlCurrentChannelSequenceIndex`: `0xEB0` (instead of `0xEB8`).
     - `sendPendingPacketsToStack`: `0xF10` (instead of `0xF30`).
     - `freePendingPackets`: `0xF18` (instead of `0xF38`).
     - `IO80211AWDLProtocol` block shifted -0x28 (`getSSID` at `0xFA0`, 129 at `0x10B8`, 132 at
       `0x10C0`, 205 at `0x1108`).
   - Remove the declarations of virtual methods absent on 15:
     - `flushFlowQueues(ether_addr*)` (slot `0xED8` on 26).
     - `signalMulticastPackets(unsigned int, unsigned int)` (slot `0xF28` on 26).
     - `p2pMulticastTx(unsigned int, bool)` (slot `0xF20` on 26).
2. **Shift of the card capabilities block**:
   - Card block access in `IO80211Controller`: base at `*(controller + 0x120) + 0xB20` (instead of
     `+0xB30`).
   - Dual-band bit (`capability 29`) at `+0xB27` bit `0x20` (instead of `+0xB37`).
3. **Shift in vtable of `IO80211AWDLPeer`**:
   - `monitorPeer` slot at `0x178` (instead of `0x180`).
4. **Presence and ivars constants**:
   - Compile with the 24H232 ivars offsets (`LLAValid = 0xC70C`, `bonjourTriggersAWDL = 0x7FF8`,
     `stateflags = 0x3EE8`, `llatimer = 0x3ED8`, `field_A66C = 0x7F4C`,
     `peer_pppdone_flag = 0x6AE`, `peer_earlyreturn_timestamp = 0x2E8`).

---

### 4.3 For macOS 14.8.9 (23J631)
1. **Dedicated header (`AWDLInterface_14.h`) and standalone binary**:
   - Instance size of the derived classes: `0x120` (instead of `0x130` on macOS 26).
   - Massive relocation of vtable slots:
     - `IO80211VirtualInterface` block shifted -0x78 bytes (starts at `0xDF0` instead of `0xE68`).
     - `sendPendingPacketsToStack` at slot `0xE80` (instead of `0xF30`).
     - `freePendingPackets` at slot `0xE88` (instead of `0xF38`).
     - `IO80211AWDLProtocol` block shifted -0xA0 bytes (starts at slot `0xF28` with `getSSID`,
       instead of `0xFC8`).
   - Remove the absent vtable methods (`flushFlowQueues`, `getPeerDataStats`,
     `clearPeerDataStats`, `getPeerSlotDataStats`, `signalMulticastPackets`,
     `setAwdlCurrentChannelSequenceIndex`, `findPeer`, `setAWDL_CONFIG`).
   - Remove the pure declaration of `setMacAddress(ether_addr&)` from `IO80211AWDLProtocol` (it
     does not exist in the vtable of 14).
2. **Call signature adaptation**:
   - Method `inputPacket`: adapt the call to the 4-argument signature
     `(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*)`, dropping the fifth
     `bool` argument introduced in macOS 15/26.
   - Infrastructure link (`RTLAirLayer.cpp`): replace `registerInfraEthernetInterface` with
     `registerEthernetInterface`, do without `allocWithParams` of `IO80211FaultReporter` and
     adapt the control message dispatch to the controller.
3. **Card capabilities path**:
   - The controller does not keep the `+0xB30` block. The kext must answer the internal ioctl 12
     (`CARD_CAPABILITIES`) on the virtual interface / controller with the `0x20` bit of `p[7]`
     set so that `IO80211AWDLPeerManager::configure` copies it to `sub+0xB18`
     (`sub+0xB1F` bit `0x20`) and keeps the 5 GHz channels.
4. **Presence and ivars constants**:
   - Compile with the 23J631 offsets (`LLAValid = 0xC4A0`, `bonjourTriggersAWDL = 0x7FA8`,
     `stateflags = 0x3EC8`, `llatimer = 0x3EB8`, `field_A66C = 0x7F04`,
     `peer_pppdone_flag = 0x6AE`, `peer_earlyreturn_timestamp = 0x2E8`).

---

## 5. Open points

1. **Authorization in the version gate**: Although the code and ABI of 25G332 are 100 % identical
   to 25G241, the build control module (`VersionGate.h`) will check the exact kernel version `?`.
   `25G332` must be registered in `Tools/airlayer_builds.txt` to enable normal kext loading on
   that version.
2. **Dynamic time-sharing behavior (MCC) on 14/15**: The sequences 129 and the events 41/43 are
   identical at data and structure level, but the response times of the sync state machine over
   radios without native AWDL firmware have not been measured on hardware under the Sonoma and
   Sequoia thread schedulers `?`.
3. **Wi-Fi Aware / NAN peers on 14**: On 23J631 `wifip2pd` shows additional dependencies
   regarding legacy `mbuf` packet queue initialization `?`. It does not affect AirDrop nor the
   standard AWDL path of AirLayer.
