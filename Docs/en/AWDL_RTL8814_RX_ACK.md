# AWDL on the RTL8814AU: reception, filters and ACK for `awdl0`

What the driver does today with the AWDL frames arriving from the air, where they get lost, and
what has to be programmed so the hardware accepts them and sends the ACK of the unicasts
addressed to `awdl0`. Complements
[AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §2 and §6 and corrects two assumptions of
that document.

Sources:
- the driver tree (`Source/rtl8814au/`);
- the register dump with the driver associated to an AP (`Docs/regdumps/macos_c896a978_idle.txt`);
- the iOS 26 captures of owly (`tests/captures/`, revision `9a676e1`, 4893 action frames and 4424
  AWDL data frames; see [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4);
- the winawdl notes on the RTL8852CE.

MAC behavior the code does not show carries `?`; none of this has been tested on hardware.

## 1. Addresses of the AWDL frames on the air

Measured over the captures:

| Frames | A1 | A3 | ToDS/FromDS | Count |
|---|---|---|---|---|
| AWDL action frames (category 127, OUI `00:17:F2`, type 8) | broadcast `FF:FF:FF:FF:FF:FF` | `00:25:00:FF:94:73` | 0/0 | 4893 of 4893 |
| Data, subtype 0 | unicast (AWDL MAC of the destination) | `00:25:00:FF:94:73` | 0/0 | 2256 |
| QoS data, subtype 8, no A-MSDU | unicast | `00:25:00:FF:94:73` | 0/0 | 2123 |
| Data, subtype 0, multicast | multicast | `00:25:00:FF:94:73` | 0/0 | 45 |
| ADDBA (category 3, action 0) | unicast | — | — | 56 |

All AWDL frames have the shape of ad-hoc network frames: A1 destination, A2 source and A3 the fixed
AWDL BSSID. The sync action frames do not go to a multicast address but to broadcast.

## 2. Hardware filter

### 2.1 Live state

With the driver associated to an AP:

| Register | Value | Reading |
|---|---|---|
| `REG_CR` `0x0100` | `0x000206FF` | `MSR` (`REG_CR+2`, `include/hal_com_reg.h:584`) = `0x02`: port 0 in station mode (bits 16-17 = 2), port 1 without link (bits 18-19 = 0). Dump, line 65 |
| `REG_RCR` `0x0608` | `0xF40060CE` | `AAP`, `AM`, `AB`, `AMF`, `CBSSID_DATA`, `CBSSID_BCN`. Line 387 |
| `REG_MACID` `0x0610` | MAC of `en2` | Line 389 |
| `REG_MACID1` `0x0700` / `REG_BSSID1` `0x0708` | `0x87654321` / `0x87654321` | Port 1 unprogrammed. Lines 449 and 451 |

The network type values are those of `include/rtw_mlme_ext.h:52-56`: 0 no link, 1 ad-hoc, 2
station, 3 AP.

### 2.2 Action frames

Action frames pass the filter: `RCR_AMF` accepts management, `RCR_AB` broadcast and
`REG_RXFLTMAP0 = 0xFFFF` all subtypes ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md)
§6.1). `RCR_CBSSID_BCN` only affects beacons and probe responses.

### 2.3 Data with a foreign BSSID

`RCR_CBSSID_DATA` is active in station mode and compares the BSSID of data frames. With port
1 unprogrammed, the AWDL data frames (A3 = `00:25:00:FF:94:73`, different from the BSSID of the
AP) do not match. Whether the hardware compares against the BSSID of each active port or only
against that of port 0 is
`?`.

The driver itself does not trust a per-port filter: when there is an AP, mesh or TDLS, or during
the best-channel scan, it removes `RCR_CBSSID_DATA` instead of programming another BSSID
(`rtw_hal_rcr_set_chk_bssid`, `hal/hal_com.c:3820-3828`). It is the direct precedent for AWDL.

### 2.4 ACK of the unicasts to `awdl0`

`awdl0` has its own MAC, different from that of `en2`. `RCR_AAP` lets all unicast through to DMA,
but
the hardware ACK only goes out for frames whose A1 matches the MAC of a port (or the MBSSID CAM
if `RCR_ENMBID` is active, [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §2.2). Without
an ACK, the sender repeats every frame up to its retry limit. winawdl measured it on an RTL8852CE:
85 %
of frames with the retry bit and stalled transfers; with the local address in the CAM of the
chip, 0 to 4 % (`refs/winawdl/DESIGN-NOTES.md`, «Acknowledging frames»).

owly achieves the same effect on Linux by joining an ad-hoc cell with the AWDL BSSID
(`join_ibss` at `crates/owly-radio/src/injection/setup.rs:369`; BSSID at
`crates/owly-awdl/src/lib.rs:169`).

## 3. Software path and where each frame gets lost

Classification by type is in `validate_recv_frame`: management at `core/rtw_recv.c:2305`, data
at `:2321` and `validate_recv_data_frame` at `:2354`.

**AWDL action frames:**
1. They pass `mgt_dispatcher`. The filter of `core/rtw_mlme_ext.c:1317` only accepts own A1 or
   broadcast, and the A1 of AWDL action frames is broadcast (§1). This corrects
   [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §6.2, which had them lost here by
   assuming them multicast.
2. `OnAction` fans them out by category. 127 is in the table: `RTW_WLAN_CATEGORY_P2P = 0x7f`
   (`include/ieee80211.h:1525`), entry `OnAction_p2p` at `core/rtw_mlme_ext.c:96`.
3. `OnAction_p2p` (`core/rtw_mlme_ext.c:7398`) drops them: it requires A1 equal to the local MAC and
   the Wi-Fi Direct OUI `0x506F9A09` (`include/wifi.h:1101`). The broadcast with OUI `00:17:F2`
   goes out with no effect.

**AWDL data** (ToDS/FromDS 0/0):
1. They enter through case 0 of `validate_recv_data_frame` (`core/rtw_recv.c:2064`) into
   `sta2sta_data_frame` (`:1174`).
2. In station mode, the branch of `:1299` requires the source to be the BSSID («sa and bssid should
   always be BSSID»): the frames of an AWDL peer are dropped.
3. The ad-hoc branch (`:1204-1224`) would drop them too: it compares A1 against the MAC of the
   adapter (`en2`, not `awdl0`) and A3 against the BSSID of the adapter.

**Management unicast to the MAC of `awdl0`** (e.g. the ADDBAs): they die in the A1 filter of
`mgt_dispatcher` (`core/rtw_mlme_ext.c:1317`), because that MAC is not the adapter's.

## 4. What has to be programmed

| Piece | What | Why |
|---|---|---|
| Port 1 | `REG_MACID1` = MAC of `awdl0`, `REG_BSSID1` = `00:25:00:FF:94:73`, ad-hoc network type (`MSR` bits 18-19 = 1) with `rtw_hal_set_msr` (`hal/hal_com.c:2382`, case `HW_PORT1`) | ACK response for the unicasts to `awdl0`, like the owly ad-hoc cell. The beacon function of port 1 (`REG_BCN_CTRL_1` `0x0551`) must stay off so as not to emit IBSS beacons `?` |
| RCR | Remove `RCR_CBSSID_DATA` while `awdl0` is active | Precedent of the driver itself with AP, mesh and TDLS (§2.3). Cost: data of other BSSs on the same channel reach the software, which have to be dropped by A3 |
| Software diversion | In `validate_recv_frame`, before the `switch` by type (`core/rtw_recv.c:2304`), divert to the AWDL path every frame with A3 = `00:25:00:FF:94:73` | It is the only point common to action frames, data and management unicast, before the three drops of §3. Action frames go to `actionFrameInput`; data, after removing the AWDL header, to the `RxCompletionQueue` of `awdl0` (encapsulation under analysis) |
| Block Ack | Decide whether to accept the peer's ADDBA (`refs/owly/docs/SPEC.md` AWDL-15) | winawdl went from 971 kB/s to 1.7 MB/s by accepting it; it requires the hardware to know the peer as an A-MPDU sender `?` |

With `CONFIG_CONCURRENT_MODE` disabled (`include/autoconf.h:113`), the port 1 write has no
adapter to originate it: `rtw_hal_set_macaddr_port`, `rtw_hal_set_bssid` and `rtw_hal_set_msr`
choose the port by `adapter->hw_port`. The layer will have to write the port 1 registers
directly, or call those functions with an adapter whose `hw_port` is `HW_PORT1`.

## 5. Open

- Whether `RCR_CBSSID_DATA` compares against the BSSID of each active port or only against that of
  port 0 `?`. Resolved on hardware: with port 1 programmed and `CBSSID_DATA` active, do the
  AWDL data frames arrive?
- Whether the ad-hoc type of port 1 by itself activates beacon emission or TSF
  update with beacons of the same BSSID `?`. What is measured and read:
  - In the live dumps of the driver on macOS, the dword `0x0550` is `0x00001009`
    (`Docs/regdumps/macos_c896a978_idle.txt:341`, same cold and warm boot). Thus,
    `REG_BCN_CTRL_1` (`0x0551`) = `0x10`: only `DIS_TSF_UDT` (`include/hal_com_reg.h:1540`), with
    `EN_BCN_FUNCTION` (bit 3) off. Port 1 today has the beacon function off and the TSF
    without update.
  - What turns on the beacon of port 1 in the tree is the ad-hoc branch of `hw_var_set_opmode`
    (`hal/rtl8814a/rtl8814a_hal_init.c:3266-3274`): `ResumeTxBeacon` and
    `REG_BCN_CTRL_1 = DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB`. That branch only compiles
    with `CONFIG_CONCURRENT_MODE`, which is disabled.
  - Therefore, port 1 programming must write `MSR`, `REG_MACID1` and `REG_BSSID1` without
    passing through `hw_var_set_opmode`, and leave `REG_BCN_CTRL_1` at `0x10`. That the ad-hoc
    type in `MSR` with `EN_BCN_FUNCTION` off emits nothing is checked with a monitor-mode capture
    `?`.
- Whether the MAC needs a `macid` entry or a security CAM entry for the peer before sending the
  ACK of its A-MPDUs `?`.
