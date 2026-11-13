# AWDL implementation plan for the RTL8814 layer

Numbered steps in dependency order to take `RTL8814AirLayer.kext` + `RTL8814Native.kext`
from the stage-1 state (`awdl0` created and accepted by `wifip2pd`, no AWDL radio) to a
full AWDL datapath: port 1, reception and diversion, action frame injection, templates, slot
clock, PSF/MIF emission, data, multicast and Block Ack.

Document base: `AWDL_RTL8814_RX_ACK.md`, `AWDL_RTL8814_TX_RECIPE.md`, `AWDL_WINDOW_TIMER.md`,
`AWDL_PSF_MIF_EMISSION.md`, `AWDL_DATA_ENCAP.md`, `AWDL_BLOCK_ACK.md`,
`AWDL_CHANNEL_SEQUENCE_CONTROL.md`, `AWDL_CAPS_ADVERTISED.md`, `AWDL_PEERMANAGER_DRIVER_ABI.md`
(§0–§3), `AWDL_ETAPAS_2_A_4.md`, `AWDL_STAGE2B_MBUF_TO_PACKET.md`, `AWDL_ESTADO_Y_BLOQUEO.md` and
`AWDL_INDICE.md` §8, plus `AWDL_PEER_PRESENCE_PPP.md` for RX delivery. Code lines
verified against the working tree of 2026-10-30 (`8ca31c5`). `Source/AirLayer/RTLAirLayer.cpp`
has uncommitted changes from another session, so its line numbers may shift when
integrating them. Anything not backed by a document or cited line carries `?`.

Plan conventions:

- Each step has its **own boot-arg keeping it disabled by default**, checked with
  `PE_parse_boot_argn` in the corresponding boot path (pattern of `rtlawdl` in
  `Source/AirLayer/RTLAirLayer.cpp:1453-1457`). The activation order is cumulative: no step
  requires the next step's boot-arg.
- "The layer" = `RTL8814AirLayer.kext` (`Source/AirLayer/`); "the radio" = `RTL8814Native.kext`
  (`Source/rtl8814au/`, `Source/Driver/`). The link between both are the platform functions of
  `Source/Driver/RTLRadioABI.h` (`RTL_RADIO_FN_ABI` :24, `RTL_RADIO_FN_MAC` :31,
  `RTL_RADIO_FN_DATA_HOOK` :41, `RTL_RADIO_FN_TX` :42) and `Source/Driver/RTL8814Native.cpp`
  (:443, :486, :500, :504).
- The layer's log literals go out through `rtlAirLog` (`Source/AirLayer/RTLAirLayer.cpp:61-62`,
  visible in the `RTLAirLayerLog` ring of node `RTL8814Native`, `ioreg`); the radio's, through the
  `JL_*` channels of `Source/Driver/RTLJoinLog.h` (:32, :37, :42) via `rtl8814native_jlog`
  (`Source/rtl8814au/include/rtw_mlme_ext.h:1322`).
- Capture replay tool: `~/Desktop/AWDLLab/tools/awdl_af_extract.py` (`AWAF` records
  with source, channel, RSSI, subtype and body from the category byte, ready for
  `bpfTapInput`) and `awdl_pcap.py` (PSF/MIF summary, TLV 4 and TLV order); source captures
  `refs/owly/tests/captures/`, revision `9a676e1` (`AWDL_REFERENCIAS_EXTERNAS.md` §4). Both live
  in the private lab repository (`HANDOFF_AWDL.md`, lab section).

## 0. Starting point

- `awdl0` is born through the handler of the 94 (`Source/AirLayer/RTLAirLayer.cpp:577-589`) and registers with
  three queues without datapath (`Source/AirLayer/AWDLInterface.cpp:142-174, 176-229`); the TX action is
  `txNoop` (`AWDLInterface.cpp:257-271`), which consumes the batch through the completion queue.
- The 18 AWDL setters answer success without effect (macro `AWDL_ACK`,
  `Source/AirLayer/AWDLInterface.h:314-338`, bodies in `AWDLInterface.cpp:341-365`); the GETs and the
  rest answer `kIOReturnUnsupported` (`AWDL_STUB`, `AWDLInterface.h:340-394`).
- The family kernel already instantiates the `IO80211AWDLPeerManager` for role 6 and has completed
  `configure`, `interfaceStateChange` (116) and `enableAWDL` (155/129/117) on hardware, with the
  lone election every second (`AWDL_ESTADO_Y_BLOQUEO.md` §1); nothing reaches the radio because
  the setters do not touch it.
- The radio delivers the data traffic of `en2` to the layer through the hook `RTL_RADIO_FN_DATA_HOOK`
  (`RTLAirLayer.cpp:1308-1315`, `Source/Driver/RTL8814Native.cpp:500`) with Ethernet frames; no
  channel exists for raw 802.11 frames or for management TX.

## 1. Port 1: MAC of `awdl0`, AWDL BSSID and data filter

**What.** Program MAC port 1 in ad-hoc mode with the MAC of `awdl0` and the AWDL BSSID
`00:25:00:FF:94:73`, and withdraw `RCR_CBSSID_DATA` while `awdl0` is active, so that the
hardware accepts and acknowledges (ACK) AWDL frames.

**Where.**

- Radio, new port-1 programming function (model `rtw_hal_set_msr`, case `HW_PORT1`,
  `Source/rtl8814au/hal/hal_com.c:2382-2396`: bits 18-19 of `REG_CR` = 1) and write of
  `REG_MACID1` `0x0700` / `REG_BSSID1` `0x0708` (live dump with both at `0x87654321`:
  `AWDL_RTL8814_RX_ACK.md` §2.1). With `CONFIG_CONCURRENT_MODE` disabled
  (`Source/rtl8814au/include/autoconf.h:113`) no existing call writes port 1: the
  new function writes the registers directly `?` (or calls `rtw_hal_set_macaddr_port` /
  `rtw_hal_set_bssid` / `rtw_hal_set_msr` with an adapter whose `hw_port` is `HW_PORT1`,
  `AWDL_RTL8814_RX_ACK.md` §4).
- Radio, withdrawal of `RCR_CBSSID_DATA`: preceded by `rtw_hal_rcr_set_chk_bssid`
  (`Source/rtl8814au/hal/hal_com.c:3820-3828`), which already removes it with AP, mesh, TDLS and scanning. Add
  the "`awdl0` active" condition in the same place or in the call the step activates.
- Link: new entry in `Source/Driver/RTLRadioABI.h` (proposal: `RTL_RADIO_FN_AWDL_PORT`,
  parameters MAC of `awdl0` + BSSID + enable/disable) `?` (name and signature to be decided), and its
  dispatch in `Source/Driver/RTL8814Native.cpp` (pattern of :500).
- Layer: store the BSSID of the 125 and the creation MAC. `setAWDL_BSSID` today is pure `AWDL_ACK`
  (`Source/AirLayer/AWDLInterface.h:315`, body `AWDLInterface.cpp:347`); add a field `_bssid[6]`
  in `RTLAwdInterface` (`AWDLInterface.h:397-402`) and the MAC is already in `_mac` (filled by
  `initWithController`, `AWDLInterface.cpp:98-126`). After storing both, call the new platform
  function.

**What it serves.** The 125 (`setAWDL_BSSID`) that `wifip2pd`/`airportd` push down when creating `awdl0`, and the MAC
of the 94. It is a prerequisite of the hardware ACK of all unicast to `awdl0` (data and ADDBA) and of
receiving data with a foreign A3 (`AWDL_RTL8814_RX_ACK.md` §2.3-§2.4).

**Test without an Apple peer.** Live register dump (pattern `Docs/regdumps/`): `REG_CR` bits
18-19 = 1, `REG_MACID1`/`REG_BSSID1` with the programmed values, `REG_RCR` without `CBSSID_DATA`;
log `LOG("awdl: port1 ...")` in the layer. Real RX: with a second adapter in monitor mode
injecting a data frame from the owly captures (A3 `00:25:00:FF:94:73`), check on a new
`JL_*` channel that the frame reaches the software instead of being discarded by BSSID.

**Risk.** The USB path of port 1 (outgoing ACK, `REG_BCN_CTRL_1` off to avoid emitting
IBSS beacons) is untested `?` (`AWDL_RTL8814_RX_ACK.md` §4-§5); if the ad-hoc type of port 1
activates beacons or TSF update, it would have to be masked. Cost of removing `CBSSID_DATA`: data from other BSSs of the channel arrive, discarded later by A3 (step 2).

**Boot-arg.** `rtlawdlport=1`.

## 2. RX diversion in `validate_recv_frame`

**What.** Before classification by type, take out of the radio's normal path every frame whose
A3 is the AWDL BSSID and deliver it to the layer as a raw 802.11 frame with metadata (RSSI, rate,
mactime, channel). It is the only point common to action frames, data and unicast management to the MAC of
`awdl0`, before the three known discards (`AWDL_RTL8814_RX_ACK.md` §3-§4: `mgt_dispatcher`
requires own A1 or broadcast at `Source/rtl8814au/core/rtw_mlme_ext.c:1317-1319`;
`OnAction_p2p` at `Source/rtl8814au/core/rtw_mlme_ext.c:7398`; the station branch of
`sta2sta_data_frame` requires "sa and bssid should always be BSSID" at
`Source/rtl8814au/core/rtw_recv.c:1299-1303`).

**Where.**

- Radio, `Source/rtl8814au/core/rtw_recv.c` in `validate_recv_frame` (:2208): short-circuit
  immediately before the `switch` by type (:2304). Compare `GetAddr3Ptr(ptr)` with the BSSID
  programmed in step 1 and, if it matches, consume the frame through the AWDL path (return `_FAIL`
  toward the caller of :4248 without touching `validate_recv_data_frame` :2354 nor
  `validate_recv_mgnt_frame`).
- Radio, delivery channel: the frame arrives as `union recv_frame` inside the RX tasklet
  (`usb_recv_tasklet`, `Source/Compat/VendorOsDep.c:677`; `recvbuf2recvframe` :433). Convert it to
  mbuf and deliver it through a new platform function (proposal: `RTL_RADIO_FN_AWDL_HOOK`,
  symmetric to `RTL_RADIO_FN_DATA_HOOK` of :41, with a metadata struct {rssi, rate, mactime,
  channel}) `?` (name and signature to be decided). Source of each field at the diversion point:
  - **RSSI in dBm**: `pattrib->phy_info.recv_signal_power` (`s8`). `pre_recv_entry`
    (`core/rtw_recv.c:4870`) calls `rx_query_phy_status` (:4911) before `rtw_recv_entry`, so it
    is already filled when reaching `validate_recv_frame`. The 8814A goes through
    `phydm_rx_physts_1st_type` (`hal/phydm/phydm_phystatus.c:1145`, branch `ODM_IC_11AC_SERIES` of
    :3278): on OFDM it is `(pwdb_all & 0x7f) − 110` and on CCK `phydm_get_cck_rssi(lna, vga)`. The
    PHY report goes on all frames because the boot RCR carries `RCR_APP_PHYST_RXFF`
    (`hal/rtl8814a/usb/usb_halinit.c:450`). If `pattrib->physt` equals 0, there is no measurement, and a 0 in
    `+0x2c` would disable the edge filter: better to discard the frame or put −100 `?`.
  - **Rate**: `pattrib->data_rate`, 7-bit `DESC_RATE*` index (`rtl8814a_rxdesc.c:55`).
  - **`mactime`**: the 8814A descriptor parser does not read the TSF. The macro
    `GET_RX_STATUS_DESC_TSFL_8814A` (dword 5, `include/rtl8814a_recv.h:156`) exists and the read would have to
    be added. Which port that TSF belongs to is not checked `?`.
  - **Channel**: `pattrib->ch` is not filled on the 8814A; the current channel of the radio must be taken.

  The manager only uses `+0x2c` (RSSI): `+0x0c` channel, `+0x28` rate and `+0x30` `mactime` are not read
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §1.3). They travel inside the 72 B header of event 48, and
  whether the daemon uses them is not traced `?`.
- Layer, reception: own queue under `_awdlLock`/`_rxLock` (`RTLAirLayer.cpp:1485`, pattern of
  `rxHook`/`rxDrainEntry` :1255-1292), with its own drain thread or the same `_rxDrain` extended.
  Classification by type in the layer: category 127 → step 3; data frame with Apple SNAP → step
  7; unicast management (ADDBA) → step 9.

**What it serves.** It is the entry gate of all AWDL RX: action frames the manager converts
into events 48 toward `wifip2pd`, TLVs that feed synchronization, data of `awdl0` and ADDBA/DELBA/BAR.

**Test without an Apple peer.** Replay of captures: extract the action frames of the iOS 26 captures with
`awdl_af_extract.py` and inject them through the new hook from a monitor adapter (or a real
ESP32-S3 peer, `AWDL_ESTADO_Y_BLOQUEO.md` §3); counter of diverted frames on a new `JL_*` channel and
`LOG("awdl: rx ...")` in the layer. Negative: data frames of the infrastructure's BSSID (A3
different) must not be diverted.

**Risk.** Without `RCR_CBSSID_DATA` (step 1) the diversion by A3 is the only data filter; the
per-frame comparison in the tasklet adds minimal cost but sits on the RX hot path `?`.
Lock discipline: reuse the `rxHook` pattern (never touch Skywalk under the radio's
lock, comment of `RTLAirLayer.cpp:1249-1254`).

**Boot-arg.** `rtlawdlrx=1` (requires `rtlawdlport`).

## 3. Action frame injection into the PeerManager

**What.** Deliver each received AWDL action frame to the `IO80211AWDLPeerManager` through the
supported door `IO80211VirtualInterface::bpfTapInput(mbuf*, 12, &hdr, 72)`
(`AWDL_PEERMANAGER_DRIVER_ABI.md` §1.5: overload `0xffffff8002330e7a`, draws from the
controller's `mngPool`, 256 × 2048 B, which exists because the layer does not override slot 438).

**Where.**

- Layer, new method of `RTLAwdInterface` (e.g. `injectActionFrame(const uint8_t *body, size_t
  len, const rtl_awdl_rx_meta *)` in `Source/AirLayer/AWDLInterface.h`, next to the private
  methods :396-409). It builds:
  - `struct apple80211_act_frm_hdr` of exactly 72 B (more than 72 triggers the deliberate `ud1` of the
    manager, `AWDL_PEERMANAGER_DRIVER_ABI.md` §1.1): source MAC at `+0x00` (A2 of the frame),
    channel at `+0x0c` (`apple80211_channel`, 12 B), rate at `+0x28`, **signed RSSI dBm at
    `+0x2c`** (compared with the edge threshold of `ivars+10268`, −70 or −78 depending on the set
    `determineAndSetEdgeRssiThreshold` picks, `AWDL_PEER_PRESENCE_PPP.md` §5.1; at 0 the
    filter always passes, `AWDL_PEERMANAGER_DRIVER_ABI.md` §1.3), `mactime` at `+0x30`; rest to zero.
  - An mbuf with the body from the category byte (`7F 00 17 F2 08 …`).
  - Call: `awdl0->bpfTapInput(m, 12, &hdr, 72)` and `mbuf_freem(m)` afterward (the mbuf stays
    owned by the caller, §1.5).
- The reference to the manager is not needed: `bpfTapInput` resolves it internally.

**What it serves.** Manager feeding: peer additions, TLV-by-TLV parsing
(`AWDL_PEERMANAGER_DRIVER_ABI.md` §1.6) and, for UMIs, event 48 consumed by `wifip2pd`
(`AWDL_ETAPAS_2_A_4.md` §2.4).

**Test without an Apple peer.** Two levels, already defined as E3.2/E3.3 (`AWDL_ETAPAS_2_A_4.md` §2.6):
synthetic injection of a valid frame from the captures (the manager parses it; if it is a UMI it emits the
48, observable with `log show --predicate 'process == "wifip2pd"' | grep -E 'nan_event:'`) and a frame
with a different OUI (discard with the kernel literal `BAD OUI in AF`). With the ESP32-S3: the PSF/MIF
it emits upon being elected master must produce peers at the daemon.

**Risk.** `hdrLen` and RSSI are the two silent failures: a 73 B header traps the kernel and an
RSSI faked at 0 disables the edge filter. Both stay covered by own asserts before
the call `?` (design).

**Boot-arg.** `rtlawdlaf=1` (requires `rtlawdlrx`).

## 4. Template and parameter storage in the layer

**What.** Replace the `AWDL_ACK`s with real storage: each setter saves its payload in the layer so
steps 5 and 6 can build TLVs and the schedule. The affected setters, with their internal type
(`AWDL_PEERMANAGER_DRIVER_ABI.md` §3) and current state:

| Type | Slot (`AWDLInterface.h`) | Internal payload | State today |
|---:|---|---|---|
| 116 | `setAWDL_SYNC_PARAMS` :322 | 20 B `{0, aw_length, aw_period, ext_length, sync_frame_period}` (§3.4) | ACK |
| 117 | `setAWDL_SYNC_ENABLED` :323 | 12 B, `enabled` at `+8` (§3.2); the only one whose failure reverts the manager | ACK |
| 118 | `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` :335 | 4 extension bytes (§4, "Extension parameters" entry of `AWDL_CHANNEL_SEQUENCE_CONTROL.md`) | ACK |
| 129 | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` :325 | 400 B: `+5` n, `+6` encoding, `+7` step, 12 B entries from `+12` (§3.7) | ACK |
| 132/205 | `setAWDL_MASTER_CHANNEL` :317 / `setAWDL_SECONDARY_MASTER_CHANNEL` :330 | 8 B, channel at `+4` (§3.8) | ACK |
| 134 | `setAWDL_SYNC_STATE` :321 | 24 B: master at `+8`, op_mode at `+0x10` (§3.5) | ACK |
| 135 | `setAWDL_ELECTION_RSSI_THRESHOLDS` :318 | 20 B, firmware edge threshold at `+16` (−70 or −78; `AWDL_PEER_PRESENCE_PPP.md` §5.1) | ACK |
| 136 | `setAWDL_PRESENCE_MODE` :375 (stub) | `u32 ≤ 0x3F` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §4) | **stub** → move to ACK+store |
| 151 | `setAWDL_SYNC_FRAME_TEMPLATE` :328 | 16 B: `+4` length, `+8` pointer to a 512 B template (§3.11, `AWDL_PSF_MIF_EMISSION.md` §2.1) | ACK |
| 149/225 | `setAWDL_OOB_REQUEST` :377 (stub) | 56 B: dst `+4`, option `+0x14`, self_bssid `+0x16`, `offset_us` `+0x0c` and `mi_bitmap` `+0x10` (225 only), `template_len` `+0x28`, `template` `+0x30` (`AWDL_PSF_MIF_EMISSION.md` §2.2-§2.3) | **stub** → move to ACK+store |
| 155 | `setAWDL_ELECTION_PARAMS` :327 | 36 B: op_mode `+0x10`, election_id `+0x12`, hop_count `+0x14`, root_mac `+0x16`, metrics `+0x1c/+0x20` (§3.6) | ACK |
| 229 | `setAWDL_AWDL_ADVERTISERS` :333 | 44 B of the master's schedule: aw, dc, tx_delay, period, presence_mode, mactime, root/master MAC (§3.12) | ACK |

**Where.** Bodies in `Source/AirLayer/AWDLInterface.cpp` (block `AWDL_ACK` :341-365 and stubs
:340-394). New state in `RTLAwdInterface` (`AWDLInterface.h:396-409`): copies of the PSF
template (512 B) and MI (1200 B, **copied at request time**: the kernel buffer `ivars+9048`
is reused for the secondary MI of the 342, `AWDL_PSF_MIF_EMISSION.md` §2.4), sync params, chan seq
(400 B), social channels, presence, extension, election, sync state and advertisers. The read of
the 129 (`0xC02869C9`) must answer what is stored or `0xE00002C7` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md`
§7 point 2) `?` — the GET slot is `getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` :352.

**What it serves.** All internal ioctls of the table of §3.1 of the ABI; it is the base of the render of TLVs
4/5/18 (step 6), of the schedule (step 5) and of the TX pull.

**Test without an Apple peer.** Without a peer: repeat the sequence observed on hardware in
`AWDL_STAGE3_CONFIGURE_RESULTS.md` (116 → 117 → 155/129/117 of `enableAWDL`), with `LOG` of
lengths and key values on each store, and a counter accessible via `ioreg` `?` (design of the
dump). Coherence: re-issue the 151 one second later (the manager re-emits it on each state change,
27 sites, `AWDL_PSF_MIF_EMISSION.md` §3.1) and verify the copy is replaced.

**Risk.** Low: the setters are already invoked and accepted; only the body changes. The 149/225 is the
only one with a new layout for the layer (56 B, corrected in `AWDL_PSF_MIF_EMISSION.md` §9).

**Boot-arg.** `rtlawdltpl=1`.

## 5. Slot clock on the host and events 41/43

**What.** A slot schedule in the layer (full spec in `AWDL_WINDOW_TIMER.md`): `base_us` +
`base_aw`, free-running clock as master (§3.1), alignment to the elected master with the stored 229
(§3.2) and with the received TLV 4s, and emission of events 41 (opening, only on served slots)
and 43 (closing, triggers election) through `reportDataPathEvents` (cell `+0x298`).

**Where.**

- Layer, new class (e.g. `Source/AirLayer/AWDLSlotClock.{h,cpp}`) with the state of §2 of the
  windows doc and the sequence table of the stored 129 (step 4): channel per slot from
  `Source/rtl8814au` — no; from the entry `+12·i+4` of the 129 `?` (index formula of the manager:
  `min(15, (AwNumber % ((step+1)·n)) / (step+1))`, `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2).
- Emission: the manager is created by the inherited factory `createPeerManager` (slot `0xB08`) from
  `IO80211SkywalkInterface::start`, which publishes it at `*(ivars+0x120)+0x28` of the very `awdl0`.
  The layer reads it after the `start` with `IO80211SkywalkInterface::getPeerManager` (exported in
  14, 15 and 26) without overwriting anything; it can also override `0xB08`, call the base and save
  the return. `getP2PSkywalkPeerMgr` (`0xCC0`) always returns 0 and is useless
  (`AWDL_PEERMANAGER_CREATION.md` §5 and §7). The events go out through `(*(pm->vptr + 0x298))(pm, 41, data, 36)` or through
  `postMessage` (slot `0xB18`, :111) — both paths listed in `AWDL_ETAPAS_2_A_4.md` §2.4.
- Timing: `thread_call` with deadline (pattern `_poll`/`_rxDrain`,
  `RTLAirLayer.cpp:1486-1487`). Served slots = entries of the 129 whose channel matches the
  radio's channel; the rest are treated as unserved (channel 0) `?` (fixed radio without hopping,
  `AWDL_WINDOW_TIMER.md` §4; the 8814AU channel hop stays out of this plan).
- Payload of the 41 (36 B, `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2): `AwNumber` u16 `+0`, sync state
  u32 `+4` (1 master / 2 slave, values of `printState`), master MAC `+8` (that of the stored 134),
  root MAC `+0x0e` (that of the 155 `+0x16` or its own), `FwAwStartTime` u32 **big-endian**
  `+0x14`, flags `+0x18`, channel `+0x1c`, window flags `+0x20` = 0. Payload of the 43 (40 B) with
  `+0x10 = 0` to skip the CCA branch (§2.5). The 42 is optional and is not emitted (§5.2 of the windows doc).
- The `sn` of TLV 4 and the `AwNumber` of the 41 must be coherent (`monitorTimeline` compares and only
  logs, `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.3; tolerance 5120 µs).

**What it serves.** Events 41/43 (and the state of the 46 after the 134, via `postSyncStateChanged` `?` —
deferred; the manager publishes the 46 itself with the 134 accepted `?`). Without the 41 the
manager never enters the window (`ivars+772`) or pulls TX (`AWDL_PEERMANAGER_DRIVER_ABI.md` §5.2).

**Test without an Apple peer.** E3.4 (`AWDL_ETAPAS_2_A_4.md` §2.6): emit the events and verify the
absence of the desynchronization traces (`AW Number discontinuity`, `AW start time inaccuracy`,
`slot 0 start too soon` — literals of §2.3 of the ABI) in the kernel's `log show`; cadence measured with
layer marks (one opening per served slot, 65,536 µs between slots). With the ESP32-S3: if
it follows the Mac as master, the free clock and the TX are coherent
(`AWDL_WINDOW_TIMER.md` §9; `AWDL_REFERENCIAS_EXTERNAS.md` §3).

**Risk.** Precision of the host timers without measurement on this machine (`?`,
`AWDL_WINDOW_TIMER.md` §7); the "slot 0 start too soon" guard (64 ms) punishes early timers;
the concurrency defects A1/A7 of the stage-1 review (`AWDL_STAGE1_CODE_REVIEW_HOOK.md`)
raise their probability with new timers `?`.

**Boot-arg.** `rtlawdlev=1` (requires `rtlawdltpl`).

## 6. PSF/MIF emission with the driver's TLVs 4, 5 and 18

**What.** Transmit the sync frame (PSF, subtype 0) every `sync_frame_period` of the 116 and the master
indication (MIF, subtype 3) once per availability window (and the UMI of the 149 unicast), with the
802.11 header, the fixed 16 B header and the TLVs 4, 5 and 18 rendered by the layer in front of the
kernel template (full contract in `AWDL_PSF_MIF_EMISSION.md` §4, §8).

**Where.**

- Layer, frame builder (new, e.g. in the same file as the clock or in `AWDLInterface.cpp`):
  - 802.11 header: FC `0xD0 0x00`, dur 0, A1 `FF:FF:FF:FF:FF:FF` (peer MAC for the UMI), A2 =
    MAC of `awdl0` (`_mac`), A3 = `00:25:00:FF:94:73` (`AWDL_RTL8814_TX_RECIPE.md` §4).
  - Fixed header: `7F | 00 17 F2 | 08 | ver ≠ 0 | subtype | 00 | phy_tx (u32) | target_tx (u32)`,
    both from the layer's µs clock (§6.5 of the emission doc).
  - **TLV 4** (sync params, §5.1): periods of the stored 116, `pm` of the 136, ext bytes of the 118,
    `mchan` of the 132, master of the 134/155, `tchan`/`dc`/`rem`/`sn` of the step-5 clock, embedded
    sequence = replica of TLV 18 (73 B measured in the captures, §10 of the emission doc).
  - **TLV 5** (election v1, §5.2): rendered from the stored 155; the exact on-air body stays
    open `?`.
  - **TLV 18** (sequence, §5.3): rendered copy of the stored 129 — real channel on the served
    slots, 0 on the rest, encoding/step/fill preserved
    (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7 point 3).
  - Verbatim copy of the stored template of the 151 (PSF) or of the 149/225 (MI; TLV 16 ARPA already
    comes inside, do not add a custom one).
- Layer → radio: new platform function (proposal: `RTL_RADIO_FN_AWDL_TX`, mbuf with the full
  802.11 frame) `?` (signature to be decided).
- Radio, transmission: replica of the vendor injection pattern (`rtw_monitor_xmit_entry`,
  `Source/rtl8814au/core/rtw_xmit.c:4445`, adapted from sk_buff to mbuf `?`): `alloc_mgtxmitframe`
  (`Source/rtl8814au/core/rtw_mlme_ext.c:7496`), copy to `buf_addr + TXDESC_OFFSET`,
  `update_mgntframe_attrib` with `update_mgntframe_attrib_addr` intact (A2/A3 are not rewritten,
  :7691), rate fixed with `update_mgnt_tx_rate` (:7546) — 6 Mbit/s OFDM on channel 6 and Broadcom's reference 12 Mbps on
  5 GHz `?` (`AWDL_PSF_MIF_EMISSION.md` §6.4) —, `retry_ctrl = FALSE` (12,
  `Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:259-263`; irrelevant on broadcast `?`),
  `dump_mgntframe` (`Source/rtl8814au/core/rtw_mlme_ext.c:7734`). Do not use
  `update_monitor_frame_attrib` uncorrected: it fixes `MGN_VHT1SS_MCS9` at 20 MHz, an invalid rate
  (`:7592`; `AWDL_RTL8814_TX_RECIPE.md` §3.1).

**What it serves.** Types 151 (repetition contracted by the 116), 149/225 (MIF per AW, UMI) and the
TX half of the manager's contract (`AWDL_PSF_MIF_EMISSION.md` §6). No TX event per frame: the
kernel does not expect one (§7 of the emission doc).

**Test without an Apple peer.** Second adapter in monitor mode capturing the social channel: verify
the on-air order against the captures (PSF `4,5,18,6,24,12,…`, MIF `4,5,18,24,12,…,16,21,…`,
`AWDL_PSF_MIF_EMISSION.md` §4) with `awdl_pcap.py`. With the ESP32-S3: the ESP32 only emits after electing
a master; if it synchronizes with the PSFs received from the layer, the TX is valid
(`AWDL_ESTADO_Y_BLOQUEO.md` §3). The advertised `AwNumber`/`sn` must match the step-5 events 41.

**Risk.** Latency between enqueue and USB output unmeasured `?` (`AWDL_RTL8814_TX_RECIPE.md` §5);
the window is marked by the step-5 timer and the enqueue must respect the TX guards
(unicast > 3 TU from the edges, `AWDL_WINDOW_TIMER.md` §6). The PMF of the port does not touch these frames
(A1 broadcast and category 127, `AWDL_RTL8814_TX_RECIPE.md` §2).

**Boot-arg.** `rtlawdlpsf=1` (requires `rtlawdlev`).

## 7. Data of `awdl0`: TX (Ethernet II → 802.11 + SNAP) and RX (strip of 16 bytes)

**What.** Move the real traffic of `awdl0`: on TX, convert the Ethernet II frame the stack delivers
into an 802.11 data frame with the AWDL SNAP and per-destination sequence; on RX, do the inverse
conversion and deliver Ethernet II into the `RxCompletionQueue` of `awdl0` (`AWDL_DATA_ENCAP.md` §1 and §7).

**Where, TX.**

- The Tx submission queue does not accept driver injection: the supported pattern is the direct call to the
  action with packets from the pool (`AWDL_STAGE2B_MBUF_TO_PACKET.md` §1 and §4). The frame of the stack arrives
  at the ifnet's `output` handler, which the layer installs:
  - Override of `prepareBSDInterface` (slot `0x8E8`, declared in `AWDLInterface.h:104`, today
    inherited) and write of the handler at `params+0x40`, or the slot `0x8E0`
    (`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`; choice between both `?`).
  - New `rtlAwdOutput(__ifnet *, __mbuf *)` with the sequence of §5 of doc 2b:
    `IOSkywalkPacketBufferPool::allocatePacket` (symbol `0xFFFFFF8002B058EE`) → `prepare` → copy
    of the mbuf to `base + 64` → `setDataOffsetAndLength(64, len)` → `mbuf_freem` → direct call to
    the action (the signature of `txNoop`, `AWDLInterface.cpp:257`). errno return: 0 = accepted.
- The real action (replacing `txNoop`): per packet, read DA/SA/ethertype of the Ethernet II frame,
  build `802.11 (A1=DA, A2=SA, A3=AWDL BSSID) + AA AA 03 00 17 F2 08 00 | 03 04 | seq | 00 00 |
  ethertype | payload` (`AWDL_DATA_ENCAP.md` §7), with `seq = (counter & 0xFFF) | (AC << 12)` and a u16 counter **per
  destination** (the high nibble is the Broadcom design; replicating it is the conservative option
  `?`), without the 802.3 length field of the internal format. Complete through the TX completion queue
  (mechanics of `txNoop`, batches of 64).
- Radio: same path as step 6 but with data attributes: legacy rate (24 Mbit/s or HT MCS7 at
  20 MHz `?`; never VHT MCS9 at 20 MHz), `retry_ctrl = TRUE` (6 retries) for unicast
  (`AWDL_RTL8814_TX_RECIPE.md` §3.1-§3.2), `BMC` bit for multicast (`rtl8814au_xmit.c:88-89`).

**Where, RX.**

- The layer receives from step 2 the raw data frames; validate data frame, protocol 0,
  header 24/26 per the QoS bit, DA = addr1, SA = addr2 (`AWDL_DATA_ENCAP.md` §5.2); check
  `AA AA 03` at `+14` and the dword `+17` against `00 17 F2 08`; if AWDL, strip 16 bytes and recompose
  the ethertype at `+12` (`AWDL_DATA_ENCAP.md` §5.3). OUI other than 00 → discard; OUI 00 or no
  SNAP → path of `llw0`, out of this plan (stage 4, `AWDL_ETAPAS_2_A_4.md` §3).
- Delivery: packet of the RX pool with `rtl_pkt_prepare`/copy/`setDataOffsetAndLength` and
  `rtl_rxcomp_enqueue` in batches — the pattern already in production in `rxFrames`
  (`Source/AirLayer/RTLAirLayer.cpp:445-482`), with `setLinkHeaderLength(14)`.
- Before enqueueing each packet, call `inputPacket` (slot `+0x0B50`, declared in
  `AWDLInterface.h:114`, family body). Return 0: discard. `consumed = 1`: the packet
  stays in the peer's PPP wait queue and is not touched. Other value: enqueue. The family does not filter
  anything enqueued without this call (`AWDL_PEER_PRESENCE_PPP.md` §3-§4).
- New overrides of `sendPendingPacketsToStack` (`+0xF30`) and `freePendingPackets` (`+0xF38`),
  declared in `AWDLInterface.h:178-180`. Those of the family only log "Not Implemented". The
  first receives the retained packets when `wifip2pd` sends ioctl 415 and enqueues them into the
  `RxCompletionQueue`. The second receives those exceeding 2 s waiting or those of a removed peer and
  returns them to the pool. Without them no packet of a new peer reaches the stack.

**What it serves.** The full data datapath of `awdl0` (stage 2 of `AWDL_ETAPAS_2_A_4.md` §1); on
RX it feeds the registered `RxCompletionQueue` and on TX it serves both the direct call of the `output`
and the manager's pull if it ever gets used.

**Test without an Apple peer.** E2.1-E2.5 (`AWDL_ETAPAS_2_A_4.md` §1.6): `ifconfig awdl0 up` + IPv6/ND
traffic without panic; `tcpdump -i awdl0` for RX; TX verifiable in monitor mode (the frame goes out
with SNAP and per-destination increasing sequence, contrast with `awdl_pcap.py`); real transfer with
the ESP32-S3 (ESP32Drop validates TX first, `AWDL_ESTADO_Y_BLOQUEO.md` §3).

**Risk.** The handler runs on the stack's thread: if the radio requires the work loop, jump with
`IOWorkLoop::runAction` retaining the packet (`?` decision, `AWDL_STAGE2B_MBUF_TO_PACKET.md` §5
step 7); kernel conversion offset untraced (64 chosen by the layer, §9.3); the headroom of
64 of the `RegistrationInfo` (`AWDLInterface.cpp:198`) is what leaves room to prepend 16 bytes.
Reserving the headroom in the radio's RX path without precedent `?`
(`AWDL_ETAPAS_2_A_4.md` §1.5).

**Boot-arg.** `rtlawdldata=1` (requires `rtlawdlrx` and `rtlawdlport`).

## 8. Multicast of `awdl0`

**What.** That IP multicasts (mDNS, `33:33:…`, `01:00:5E:…`) go out in the windows and enter through
RX. On Broadcom the stash and the timer belong to the family and require three interface
overrides that today stay at the base (`AWDL_DATA_ENCAP.md` §6): `p2pMulticastTx(IO80211NetworkPacket**, uint)`
(slot `+0xF18`, base `0xffffff8002334d6a` does nothing), `freeMulticastPacket` (`+0xF08`, must
return to the pool) and `triggerAWDLMulticastTx` (`+0xF10`, optional).

**Where.**

- Layer: overrides of `p2pMulticastTx`/`freeMulticastPacket` (declared in
  `AWDLInterface.h:173-179`) that feed the TX action directly (Broadcom `p2pMulticastTx` pattern,
  `0xffffff8001541d64` → `dequeuePackets`, `AWDL_STAGE2B_MBUF_TO_PACKET.md` §4) and
  return the packets to the pool. The stash (`stashMulticastPacketPair`, `handleMulticastTimer`) and
  the 0x80 marks work without driver cooperation as long as the overrides distinguish the
  re-fed packets `?` (own marking convention, §6 of the encapsulation doc).
- Alternative without stash (simpler, no firmware extending windows): transmit the multicast
  directly on the served slots with the multicast guards (16 TU on slots 0 and 10, or
  3 TU on any served slot without channel hopping, `AWDL_WINDOW_TIMER.md` §6) `?` (design
  choice; the doc admits it, `AWDL_DATA_ENCAP.md` §7 point 5).
- RX: A1 multicasts arrive through the step-2 diversion and are delivered as data of step 7; the
  special DAs `33:33:00:00:…`, `33:33:80:00:00:FB` and `01:00:5E:00:…` are filtered by `inputPacket`, which
  the layer calls in step 7 (`AWDL_PEER_PRESENCE_PPP.md` §3).

**What it serves.** TX multicast of the Tx submission queue (branch of bit 0 of DA) and RX multicast; neither
conditions a specific ioctl — it is part of the datapath.

**Test without an Apple peer.** `ping6 -m`/mDNS between `awdl0` and the ESP32-S3 `?` (multicast support on the
ESP32 peer unverified); counter of stashed/delivered packets in `LOG`; in monitor, the multicast
frame goes out inside the served slot and with the `BMC` bit of the descriptor `?` (verifying the
bit requires descriptor instrumentation).

**Risk.** Stash arrays limited to 16 entries per side (the leftovers are freed with
`freeMulticastPacket`): an override that does not free exhausts the pool. The two base overrides are
"Function not implemented" — without them the multicast of `awdl0` never goes out.

**Boot-arg.** `rtlawdlmc=1` (requires `rtlawdldata`).

## 9. Block Ack: ADDBA, DELBA/BAR and reordering

**What.** The family does no Block Ack (dead `BLOCK_ACK` handlers, `AWDL_BLOCK_ACK.md` §2) nor
reordering (§5): the layer answers the peer's ADDBA by itself, processes DELBA and BAR, and delivers in
order to the `RxCompletionQueue` with a reorder buffer per (peer, TID). winawdl measured
971 kB/s → 1,7 MB/s when accepting the ADDBA (TID 0, window 64).

**Where.**

- Interception: frames of category 3 (`RTW_WLAN_CATEGORY_BACK`,
  `Source/rtl8814au/include/ieee80211.h:1511`) arrive through the step-2 diversion; the layer
  classifies them before the data path. Parsing precedent in the radio: `OnAction_back`
  (`Source/rtl8814au/core/rtw_mlme_ext.c:3677`) with `process_addba_req` (:3747) and the three
  `JL_BA` logs already present (:3734-3740).
- Layer, ADDBA answer: build the response frame (category 3, action 1) accepting TID 0 and
  window 64, and transmit it through the path of step 6 (unicast to the peer, mgnt) `?` (body of the
  response to be composed from the peer's request and `AWDL_REFERENCIAS_EXTERNAS.md` §7).
- Layer, DELBA (action 2) and BAR (control 0x94): close/advance the session per (peer, TID); emit the
  response Block Ack `?`. If the 8814AU MAC can hold the session in hardware, leave it to the
  hardware `?` (`AWDL_RTL8814_RX_ACK.md` §5, peer CAM/macid unverified).
- Reordering: buffer per (peer, TID), window 64, expiration, in-order delivery; model of the
  driver tree: `struct recv_reorder_ctrl` (`Source/rtl8814au/include/rtw_recv.h:83`) and
  `recv_indicatepkts_in_order` (`Source/rtl8814au/core/rtw_recv.c:3266`, with
  `check_indicate_seq` :3146 and `enqueue_reorder_recvframe` :3201) — Linux code to port `?`.
  Reordering is interleaved before the delivery of step 7.
- `setPEER_CACHE_CONTROL` (slot `0x1108`, stub `AWDLInterface.h:376`): keep returning 0; optional
  use of the opcode (`+4`) and of the ampdu flag (`+44`, always 1 in the family, `AWDL_BLOCK_ACK.md`
  §4) as per-peer switch of the BA session `?`.

**What it serves.** Incoming ADDBA/DELBA/BAR from the peer (no ioctl or event carries them: they travel in
action frames); no A-MPDU on TX until an agreement exists (`AWDL_BLOCK_ACK.md` §6-§7).

**Test without an Apple peer.** Replay: the captures contain 56 ADDBA; extract them with the extractor and
inject them (step 3) to see the response in monitor mode. With the ESP32-S3: if the peer emits ADDBA
`?` (support unverified) and the throughput with/without BA is measured with iperf over `awdl0` `?`.
Without BA, the data path of step 7 must keep working (non-aggregated frames arrive all the same).

**Risk.** It is the step with the most open pieces: 8814AU hardware BA `?`, exact body of the
response `?`, port of the reordering. Last in the order precisely because everything before is
verifiable without it; without BA the peer retransmits up to its limit (rate cost, not correctness).

**Boot-arg.** `rtlawdlba=1` (requires `rtlawdldata`).

## 10. Dependency graph

```
P1 port 1 + RCR ──► P2 RX diversion ──► P3 AF injection ──┐
        ▲                        │                        │
        │                        │                        ▼
P4 template storage ─────────────┼──► P5 clock + events 41/43 ──► P6 PSF/MIF TX
                                 │                                     │
                                 │                                     ▼
                                 └──────────────► P7 data TX/RX ◄────┘ (via radio)
                                                           │
                                                           ▼
                                                     P8 multicast
                                                           │
                                                           ▼
                                                     P9 Block Ack
```

- P1 depends on a fraction of P4 (store the 125 and the MAC); its core belongs to the radio and can be
  built first with the MAC/BSSID fixed by the boot-arg `?`.
- P3 and P7 consume the diversion of P2; P6 and P7 consume the storage of P4 and the clock of P5; P9 consumes
  P2 (interception), P6 (mgnt TX path) and P7 (in-order delivery).
- `AWDL_ETAPAS_2_A_4.md` §4 places the same thing in stages: P1-P2 = radio prerequisites of stage 2;
  P4-P6 = stage 3 (synchronization and action frames); P7-P8 = stage 2 (datapath); P9 = closing of the
  Block Ack note. This plan's order prioritizes unlocking RX over TX because RX is the lowest-risk
  test with a peer available (`AWDL_ESTADO_Y_BLOQUEO.md` §4).

## 11. Order of hardware tests (without an Apple peer)

1. **Registers** (P1): live dump of `REG_CR`/`REG_MACID1`/`REG_BSSID1`/`REG_RCR` (pattern
   `Docs/regdumps/`).
2. **Synthetic RX** (P2-P3): replay of the 4893 action frames of the captures with the AWDLLab
   extractor; peers visible at `wifip2pd` (`nan_event:`) and `BAD OUI in AF` discard on the negative.
3. **Templates** (P4): log of stores in the already observed enablement sequence
   (`AWDL_STAGE3_CONFIGURE_RESULTS.md`).
4. **Events** (P5): cadence of the 41/43 without `monitorTimeline` traces; with the ESP32-S3 nearby,
   master coherence.
5. **PSF/MIF TX** (P6): monitor on the social channel + TLV order contrast with
   `awdl_pcap.py`; the ESP32-S3 synchronizes with the layer's PSFs.
6. **Data** (P7): local ND/IPv6 without panic, `tcpdump -i awdl0`, transfer with the ESP32-S3.
7. **Multicast** (P8): mDNS/ping6 multicast on `awdl0`.
8. **BA** (P9): replay of the 56 ADDBA of the captures and throughput measurement with the ESP32-S3 `?`.

Starting state of these tests: none executed; with an Apple peer the ones of
`AWDL_ESTADO_Y_BLOQUEO.md` §4 also stay pending, which this plan does not replace.

## 12. Open questions of the plan itself (`?`)

| # | Point | Closure |
|---:|---|---|
| 1 | Signature and name of the new platform functions (`AWDL_PORT`, `AWDL_HOOK`, `AWDL_TX`) | Design of `RTLRadioABI.h` before P1/P2/P6 |
| 2 | Programming path of port 1 and effect of the ad-hoc type on beacons/TSF | Bounded in `AWDL_RTL8814_RX_ACK.md` §5: write `MSR`/`REG_MACID1`/`REG_BSSID1` without `hw_var_set_opmode` and leave `REG_BCN_CTRL_1` at the measured `0x10` (beacon off, TSF not updated); missing the monitor capture of P1 |
| 3 | ~~Exact RSSI/rate/mactime field the radio delivers with the raw frame~~ | Closed in step 2: `recv_signal_power`, `data_rate`, TSFL of the descriptor not read yet; the value for frames without a PHY report stays open |
| 4 | ~~Capture of the manager pointer~~ | Closed by `AWDL_PEERMANAGER_CREATION.md`: `getPeerManager` after the `start`, or an override of `0xB08` that calls the base |
| 5 | Exact body of TLV 5 and its mapping with the 155 | P6; `AWDL_PSF_MIF_EMISSION.md` §10 |
| 6 | Rate of PSF/MIF (12 Mbps reference vs 6 Mbit/s OFDM on 2.4 GHz) and of data (legacy 24 vs HT MCS7) | Measurement in P6/P7 |
| 7 | Installation of the project's own `output` (slot `0x8E0` vs `0x8E8`) | P7; `AWDL_STAGE1_OUTPUT_HANDLER_LINK.md` |
| 8 | Thread context of the TX action (stack vs work loop) | P7; `AWDL_STAGE2B_MBUF_TO_PACKET.md` §9.5 |
| 9 | BA session in 8814AU hardware and body of the ADDBA response | P9; `AWDL_BLOCK_ACK.md` §8 |
| 10 | Multicast support of the ESP32-S3 peer | Test of P8 |
