# Glossary for the AWDL project

Terms a developer new to wireless and to this project meets in the `Docs/AWDL_*.md` notes. Meanings are one or two plain sentences taken from the cited documents; where the
documents do not explain a term, the Meaning says "not covered". Sections follow the reading order
suggested in `AWDL_INDICE.md` §7: the on-air protocol, the macOS objects, the two kexts and their
contract, and the test tools.

## 1. AWDL protocol

| Term | Meaning | Where |
|---|---|---|
| AWDL | Apple's peer-to-peer Wi-Fi protocol implemented here: peers elect a master, agree on a repeating channel sequence of availability windows, exchange periodic action frames that carry TLVs, and send IPv6 data over `awdl0`. | `AWDL_PEERMANAGER_SOFTWARE.md` §1; `AWDL_FRAME_WIRE_FORMAT.md` §2 |
| AW (availability window) | Availability window, the basic slice of the AWDL schedule: 16 TU = 16384 µs, the value TLV 4 carries at `+8`. The driver reports each window start with event 41. | `AWDL_WINDOW_TIMER.md` §1; `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2 |
| slot | A group of 4 AWs: 64 TU = 65536 µs. The channel sequence has 16 slots (2^20 µs ≈ 1.05 s); a slot whose channel is 0 is not served. | `AWDL_WINDOW_TIMER.md` §1, §4 |
| channel sequence | The per-slot channel plan advertised on the air (TLV 18, also embedded in TLV 4): 16 entries with the channel of each slot. The kernel decides the content and pushes it with ioctl 129; the driver executes the dwell and renders the TLVs, announcing channel 0 on slots it does not serve. | `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §1, §7 |
| social channels | The three discovery channels AWDL uses: 6, 44 and 149. The manager programs the primary and secondary ones with ioctls 132 and 205. | `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §4; `AWDL_PEERMANAGER_SOFTWARE.md` §6 |
| PSF | The sync frame, one of the two periodic AWDL action frames: carries the sync params, election and channel-sequence TLVs and is repeated every sync frame period (110, contracted by ioctls 116/418) from the template delivered with ioctl 151. | `AWDL_PSF_MIF_EMISSION.md` §1, §2.1, §6.1 |
| MIF | Master indication frame, the second periodic action frame: carries the master's TLVs (MI template). Sent unicast as a UMI (ioctl 149) or repeated every AW from the permanent template (ioctl 225). | `AWDL_PSF_MIF_EMISSION.md` §1, §2.2–§2.3 |
| UMI | Unicast master indication: a master indication frame addressed to one peer, sent through ioctl 149 (option 6 normal, 14 "UMIOnDwell"); the manager only sends it inside the availability window. | `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.10; `AWDL_PSF_MIF_EMISSION.md` §2.2 |
| TLV | Type-Length-Value element of the action frame body: a 3-byte header `{u8 type, u16 len}` in machine (little-endian) order plus a body of `len` bytes. TLVs start at `+16` of the frame; unknown types are skipped. | `AWDL_FRAME_WIRE_FORMAT.md` §3, §4 |
| election | The protocol that picks the master: `runElection`/`runParentElection` sweep the peer table and elect the parent comparing root metric, then root MAC, then root frame count, then hop count; triggered by event 43 and by parsing a peer's sync params. | `AWDL_PEERMANAGER_SOFTWARE.md` §3 |
| master | The elected node the others follow: its channel sequence and window timing define the schedule. Sync state 1 = Master, 2 = Slave (field `+0x04` of event 41); the master emits the PSFs and MIFs. | `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2; `AWDL_PEERMANAGER_SOFTWARE.md` §3 |
| metric | Election metric: the root metric is compared first, then the root MAC. The default self metric is 65 (43 with a flag) and grows while in the slave role; `awdlsim --metric` raises it to become master. | `AWDL_PEERMANAGER_SOFTWARE.md` §3.2; `AWDL_PAR_VIRTUAL.md` §4 |
| counter | Election counter carried in the election TLVs; it only counts among peers following the same root. A peer whose root advertises counter 0 is discarded as a "Legacy peer", so `awdlsim` uses a random non-zero counter and rejects `--counter 0`. | `AWDL_PAR_VIRTUAL.md` §4 |
| sync tree | The path of nodes from the root master down to the sender, announced in TLV 20: `len = 3 + 6·min(hopCount+1, 10)` bytes, built by `buildSyncTreeTlv` and parsed by `parseAwdlSyncTreeTLV`. | `AWDL_PEERMANAGER_SOFTWARE.md` §4; `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11 |
| TU | 802.11 time unit: 1024 µs. An AW is 16 TU, a slot 64 TU; the S4 sync test requires the clock offset to stay below 3 TU. | `AWDL_WINDOW_TIMER.md` §1; `AWDL_PAR_VIRTUAL.md` §4 |
| role 6 | The virtual interface role IO80211Family uses for AWDL (published as `IO80211VirtualInterfaceRole = AirLink`). `wifip2pd` requests it in VIRTUAL_IF_CREATE and the controller's `setVIRTUAL_IF_CREATE` override creates the interface. | `AWDL_ROLE6_RE.md` §1; `AWDL_KERNEL_ROLE_TABLE.md` (per `AWDL_INDICE.md` §2) |

## 2. macOS IO80211 and Skywalk objects

| Term | Meaning | Where |
|---|---|---|
| awdl0 | BSD name of the AWDL virtual interface: prefix `"awdl"` plus unit 0, both taken from the `RegistrationInfo` and composed as `snprintf("%s%u", ...)` during the BSD registration inside `registerService`. | `AWDL_INTERFACE_BIRTH.md` §3.1, §3.3 |
| wifip2pd | The Apple userspace daemon that drives AWDL: it queries CARD_CAPABILITIES (RawGet 12), creates `awdl0` (RawSet 94, waiting up to 30 s on a semaphore released by `IOServiceMatched`), configures it and consumes its events. | `AWDL_ROLE6_RE.md` §4, §6; `AWDL_INDICE.md` §1 |
| sharingd | The macOS userspace daemon behind AirDrop: it needs the BLE advertisement hashes to start the AirDrop server, and with AirDrop on "Everyone" it opens TCP 8770 on `awdl0`. | `AWDL_PAR_VIRTUAL.md` §6 |
| IO80211Family | Apple's kernel extension with the `IO80211*` class hierarchy (controller, virtual interfaces, peer managers). A third-party kext builds its AWDL interface on its exported vtables and meta classes. | `AWDL_PEERMANAGER_SOFTWARE.md` §1, §10; `AWDL_KEXT_LINKAGE.md` (per `AWDL_INDICE.md` §6) |
| Skywalk | The network interface stack below IO80211Family (kext `com.apple.iokit.IOSkywalkFamily`): packet queues and pools registered with the bus, and the BSD client that allocates and attaches the `ifnet`. The `awdl0` class chain ends in `IOSkywalkEthernetInterface` : `IOSkywalkNetworkInterface`. | `AWDL_INTERFACE_BIRTH.md` §2.4, §3.2–§3.3 |
| IO80211VirtualInterface | Base class of the virtual interfaces (AP, AWDL, NAN...). Its vtable layout differs between releases (Tahoe has four more methods than 15.8.2), so a header with the wrong layout fails to link. | `AWDL_INTERFACE_BIRTH.md` §2.3 |
| IO80211AWDLProtocol | Kernel class between `IO80211VirtualInterface` and the concrete interface: the Broadcom AWDL object (`AppleBCMWLANProximityInterface`) inherits from it, and the project's `RTLAwdInterface` subclasses it with the matching vtable layout. | `AWDL_INTERFACE_BIRTH.md` §1, §2.4 |
| IO80211AWDLPeerManager | Kernel class that implements the AWDL protocol in software: election with metrics, sync tree, MI/UMI templates, TLV parsing, peer table, availability-window state machine and 14 timers. It delegates only the physical execution (frame TX, channel change, frame capture, data transport) to the driver below. | `AWDL_PEERMANAGER_SOFTWARE.md` §1, §10; `AWDL_PEERMANAGER_DRIVER_ABI.md` (whole) |
| RegistrationInfo | The 0x130-byte structure the driver fills for `registerEthernetInterface`: BSD prefix `"awdl"` at `+0x30`, unit 0 at `+0x38`, MAC at `+0x108`, media words at `+0x24`/`+0x28`, and MTU and maximum MTU at `+0x48`/`+0x4C`: `initRegistrationInfo` fills 1500, the AWDL birth path writes `0x594` (1428). | `AWDL_INTERFACE_BIRTH.md` §2.2, §3.2 |
| CARD_CAPABILITIES | apple80211 GET type 12: a 28-byte capability block (4-byte version + 24 bytes of bits) copied into the controller card object at `+0xB30`. `wifip2pd` gates AWDL creation on `p[8]` bit 0 (Broadcom also sets `p[7]` bit 0). | `AWDL_CARD_CAPS_RESTANTES.md` §1, §2 |
| VIRTUAL_IF_CREATE | apple80211 RawSet type 94 with a 32-byte payload: MAC at `+4`, role at `+0x0C`, and the driver returns the BSD name at `+16`. On role 6 it creates or reuses the AWDL interface. | `AWDL_INTERFACE_BIRTH.md` §2.1; `AWDL_ROLE6_RE.md` §1, §4 |

## 3. The two kexts and their ABI

| Term | Meaning | Where |
|---|---|---|
| RTL8814Native | The radio driver kext for the Realtek RTL8814AU (TP-Link T9UH): USB transport, the Realtek HAL from `morrownr/8814au` behind a Linux-to-XNU layer, firmware loading, WPA2/WPA3 supplicant and the AWDL radio hooks (`Source/Driver/AWDLTx.c`, `AWDLSim.cpp`). | `README.md` |
| RTL8814AirLayer | The kext that publishes the radio to IO80211Family as a native Skywalk interface (infrastructure Wi-Fi) and creates `awdl0`: `Source/AirLayer/AWDLInterface.{h,cpp}` (vtables, AWDL setters, TX ring, clock alignment, data path) and `RTLAirLayer.cpp` (controller). | `README.md` |
| rtlRadio ABI | The set of `callPlatformFunction` names in `Source/Driver/RTLRadioABI.h` (`rtlRadioTx80211`, `rtlRadioAwdlHook`, `rtlRadioAwdlPort`, `rtlRadioSetChannel`, `rtlRadioAwdlOffch`, ...): the only channel between the two kexts, so another chip can drive the same layer by implementing it. | `AWDL_RTL8814_RAW_TX_PORT1.md` §1.3 |
| port 1 | The second hardware MAC port of the RTL8814AU, programmed directly (`REG_MACID1` 0x700, `REG_BSSID1` 0x708 = `00:25:00:FF:94:73`, ad-hoc in `MSR` bits 3:2) via `rtlRadioAwdlPort` so unicasts sent to `awdl0` receive a hardware ACK; free because concurrent mode is off. | `AWDL_RTL8814_RAW_TX_PORT1.md` §2.1–§2.5 |
| ioctl 143 | `PEER_CACHE_CONTROL`: adds or deletes a peer in the firmware peer cache (operation at `+4`: 0 add, 2 delete; MAC at `+8`). The layer answers success. | `AWDL_PAR_VIRTUAL.md` §9.4 |
| ioctl 130 | `getPEER_CACHE_MAXIMUM_SIZE`: the layer answers the family default (8). In `wifip2pd`'s post-create sequence the ioctls 130/131 travel a BSD socket, fall out of range (102) and are not fatal. | `AWDL_PAR_VIRTUAL.md` §9.4; `AWDL_STAGE1_IOCTL_130_131.md` (per `AWDL_INDICE.md` §4) |
| ioctl 164 | `setAWDL_PEER_TRAFFIC_REGISTRATION`: a peer's traffic registration, which the layer answers with success; an AirDrop one makes IO80211 send that peer a UMI. | `AWDL_PAR_VIRTUAL.md` §9.4, §9.7 |
| peer cache | The family drops packets to peers not in the cache (`preQueuePacket`), and `addPeerToFWCache` only caches a peer if ioctl 143 succeeds and the peer advertises HT or VHT capabilities (HT in TLV 7; where VHT comes from is not covered). | `AWDL_PAR_VIRTUAL.md` §9.4–§6.3 |
| ND6ALT | The `ifconfig` eflag of `awdl0` that stops the kernel from sending Neighbor Solicitations: neighbor entries only appear when a service resolves a peer, so a bare `ping6` never leaves the host until `ndp -s` adds a static entry. | `AWDL_PAR_VIRTUAL.md` §9.3 |
| time sharing (MCC) | Sharing the radio between the associated infrastructure BSS and the AWDL slots (`rtlawdl_mcc=1`): on the RTL8814AU it is a software path (null data frame with PM=1, channel switch, return). Measured on hardware a switch takes 2.5 ms on average and 4.4 ms at most, and started at the slot boundary it reaches the 149 slot late, so most peer frames are missed (open work). | `AWDL_MCC_TIEMPO_COMPARTIDO.md` §1, §3; `AWDL_PAR_VIRTUAL.md` §9.2 |

## 4. Test tools and reverse-engineering environment

| Term | Meaning | Where |
|---|---|---|
| awdlsim | The virtual AWDL peer (`Tools/awdlsim`, Rust, vendored `crosswind-awdl`): it reads the frames `awdl0` transmits from a driver ring, injects its own frames into the RX path as if received, and runs the S1–S7 tests (election, sync, ping6, AirDrop with `--tun`). Options include `--mac`, `--metric`, `--counter`, `--capture`, `--tun`. | `AWDL_PAR_VIRTUAL.md` §1, §4 |
| test_uc | The user-client test tool (`Tools/test_uc.c`): status, joins and the `sim query/watch/inject` selectors 12–14 of the driver, used to check the sim port without running the node. | `AWDL_PAR_VIRTUAL.md` §3 |
| kernel collection | The boot kernel collection (`BootKernelExtensions.kc`) of each macOS release: the binary every reverse-engineering note cites (all addresses are kernel-collection VAs of the stated build), obtained and dumped per the kernelcache guide. | `AWDL_KERNELCACHE_OBTENTION.md` |
| vtable slot | The position of a method in a C++ vtable, cited either as byte offset from the `vptr` (`vptr` = `__ZTV` symbol + 0x10) or as cell = offset/8. Slots move between releases, so each version needs its own layout check against the kernel collection. | `AWDL_PEERMANAGER_DRIVER_ABI.md` "Slot convention" |
