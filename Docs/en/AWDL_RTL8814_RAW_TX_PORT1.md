# AWDL on RTL8814: raw TX of action frames and port 1 (sources only)

Scope: `Source/rtl8814au` and `Source/Driver` read directly, without disassembly. Goal:
how to transmit a raw 802.11 management frame (action, category 127, A1 broadcast, A3
`00:25:00:FF:94:73`) on the current channel with the radio associated to an AP, how to program the
hardware port 1 directly and where to hook the RX of those frames. Current configuration:
`CONFIG_CONCURRENT_MODE` off (`Source/rtl8814au/include/autoconf.h:113`), a single
adapter with `hw_port = HW_PORT0` (`Source/rtl8814au/os_dep/linux/usb_intf.c:1320`).

## 1. Transmission of a raw management frame

### 1.1 Exact functions

The full recipe already exists in the tree: `rtl_sae_issue_auth` (`Source/Driver/SAEAuth.c:17-75`)
builds and emits a raw authentication frame. The chain for an AWDL action frame is the
same with a different body:

1. `alloc_mgtxmitframe(&padapter->xmitpriv)` (7529) — wrapper of `_alloc_mgtxmitframe`
   (`Source/rtl8814au/core/rtw_mlme_ext.c:7496`): `rtw_alloc_xmitframe_ext` + `rtw_alloc_xmitbuf_ext`
   (7511), sets `frame_tag = MGNT_FRAMETAG`, `pxmitbuf` and `buf_addr = pxmitbuf->pbuf`
   (7519-7521). Declaration at `Source/rtl8814au/include/rtw_mlme_ext.h:675`.
2. `update_mgntframe_attrib(padapter, pattrib)` (`Source/rtl8814au/core/rtw_mlme_ext.c:7616`,
   declaration `rtw_mlme_ext.h:884`).
3. Header write into `pmgntframe->buf_addr + TXDESC_OFFSET` (pattern
   `Source/Driver/SAEAuth.c:40-54`; the buffer is zeroed at
   `WLANHDR_OFFSET + TXDESC_OFFSET` before writing).
4. `pattrib->last_txcmdsz = pattrib->pktlen` (`Source/Driver/SAEAuth.c:70`).
5. `dump_mgntframe(padapter, pmgntframe)` (`Source/rtl8814au/core/rtw_mlme_ext.c:7734`).

`dump_mgntframe` first applies the project's own PMF hook of the tree (`rtl_pmf_protect_mgnt`,
`Source/rtl8814au/core/rtw_mlme_ext.c:7723-7732`; on unencrypted AWDL frames it alters nothing ?)
and calls `rtw_hal_mgnt_xmit` (`Source/rtl8814au/hal/hal_intf.c:816`), which copies `ra`/`ta` from
the header into the attrib (`update_mgntframe_attrib_addr`, `rtw_mlme_ext.c:7691-7715`) and calls
`hal_func.mgnt_xmit`, assigned to `rtl8814au_mgnt_xmit` for USB
(`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:2043`). That function delegates to
`rtw_dump_xframe` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:369,897-899`): generates the
TX descriptor (`update_txdesc`, 403), computes the USB queue with `rtw_get_ff_hwaddr` (415) and delivers
the buffer with `rtw_write_port` (427). With `qsel = QSLT_MGNT (0x12)`, `rtw_get_ff_hwaddr` returns
`MGT_QUEUE_INX` (`Source/rtl8814au/core/rtw_xmit.c:4359,4390-4392`).

`rtw_hal_mgnt_xmit` additionally inserts BIP if there is a BIP key installed
(`Source/rtl8814au/hal/hal_intf.c:822-829`); on a normal STA link without PMF active it does not enter.

### 1.2 Rate and retry fields that `update_mgntframe_attrib` sets

`Source/rtl8814au/core/rtw_mlme_ext.c:7616-7689`:

| Field | Value | Line |
|---|---|---|
| `hdrlen` | 24 | 7628 |
| `priority` | 7 | 7630 |
| `mac_id` | `RTW_DEFAULT_MGMT_MACID` = 1 (`Source/rtl8814au/include/drv_types.h:806`) | 7631 |
| `qsel` | `QSLT_MGNT` = 0x12 (`Source/rtl8814au/include/hal_com.h:262`) | 7632 |
| `rate` | `pmlmeext->tx_rate` | 7672 |
| `raid` | `rtw_get_mgntframe_raid` according to CCK/OFDM | 7667-7671 |
| `encrypt` / `bswenc` | `_NO_PRIVACY_` / `_FALSE` | 7674-7675 |
| `qos_en` / `ht_en` / `sgi` | `_FALSE` | 7677-7681 |
| `bwmode` | `CHANNEL_WIDTH_20` | 7679 |
| `seqnum` | `pmlmeext->mgnt_seq` | 7683 |
| `retry_ctrl` | `_TRUE` | 7685 |
| `mbssid` | 0 | 7687 |

The base rate is 6 Mbps OFDM when the current channel is > 14 (or the mode does not include 11B); 1 Mbps CCK
otherwise (`Source/rtl8814au/core/rtw_mlme_ext.c:1062-1064`). `update_mgnt_tx_rate`
(7546-7552) changes it globally. The TX descriptor of the `MGNT_FRAMETAG` branch
(`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:226-274`) sets `USE_RATE=1`,
`TX_RATE = MRateToHwRate(pattrib->rate)` (232), activates the retry limit and puts
`DATA_RETRY_LIMIT` to 6 if `retry_ctrl == _TRUE`, 12 otherwise (259-263). With `CONFIG_XMIT_ACK`
off (`Source/rtl8814au/include/autoconf.h:63`) no TX report is requested (the
`CONFIG_XMIT_ACK` block of 266-274 is dead).

The sender must increment the sequence: SAE uses `SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
pmlmeext->mgnt_seq++` (`Source/Driver/SAEAuth.c:45-46`). For AWDL its own counter is advisable
to avoid interfering with the numbering of infrastructure management.

Variant with ACK wait: `dump_mgntframe_and_wait_ack` (`rtw_mlme_ext.c:7809`, timeout 500 ms
at 7812) requires `CONFIG_XMIT_ACK`; without it it falls to `dump_mgntframe` + `rtw_msleep_os(50)`
(7803-7804). A broadcast frame (A1 `ff:ff:ff:ff:ff:ff`) receives no 802.11 ACK, so the
correct variant for AWDL is `dump_mgntframe`. `dump_mgntframe_and_wait` (7746-7774) waits
for the USB completion via `submit_ctx`, not for a radio ACK.

### 1.3 Frame supplied by another kext

The radio ABI (`Source/Driver/RTLRadioABI.h`, `RTL_RADIO_ABI = 2`, line 20) has the management TX
function: `RTL_RADIO_FN_TX_80211` (`rtlRadioTx80211`, 66) receives the whole frame (header and
body, without FCS) in p1 and the length in p2. It is implemented by `rtl8814native_radio_tx_80211`
(`Source/Driver/AWDLTx.c:21-61`): validates management type and radio state, `alloc_mgtxmitframe`,
copies the bytes as-is to `buf_addr + TXDESC_OFFSET`, sets the driver sequence (50-51),
disables retries if A1 is group-addressed (53-55) and calls `dump_mgntframe` (59). The vendor
kernel does not analyze the frame on the TX path, it only trims it to `last_txcmdsz`; category 127 never
goes through any action frame table on TX. The data TX (`RTL_RADIO_FN_TX`, 42) remains
Ethernet-only. The AWDL RX is `RTL_RADIO_FN_AWDL_HOOK` (53), which registers an `rtl_radio_awdl_rx_fn`
(55-56) that receives the raw bytes (header included, FCS removed, 47-51).

The `callPlatformFunction` dispatch is at `Source/Driver/RTL8814Native.cpp:431-536`: the AWDL
hook case at 504-507, `RTL_RADIO_FN_AWDL_PORT` (port 1) at 508-510, `RTL_RADIO_FN_SET_CHANNEL` at
511-519 and `RTL_RADIO_FN_TX_80211` at 520-529. The AirLayer layer uses it from `RTLAwdInterface`
(`Source/AirLayer/AWDLInterface.cpp`).

### 1.4 Thread and locks

- `dump_mgntframe` does not sleep: the allocation goes with the spinlocks of `xmit_priv` and
  `rtw_write_port` sends the URB. `rtw_hal_mgnt_xmit` itself warns
  "[IMPORTANT] This function would be run in interrupt context"
  (`Source/rtl8814au/hal/hal_intf.c:814`).
- The live SAE sender runs in `sae_work`, a `work_struct` enqueued with `rtl_schedule_work`
  (`Source/Driver/SAEJoin.c:591,615,632,651`): process context (workqueue), without
  holding any vendor-tree locks while calling `dump_mgntframe`. That is the pattern to
  replicate: copy the bytes of the calling kext into the mgntframe and dispatch the send in an
  own workitem; the `callPlatformFunction` call of the AWDL kext should not enqueue the frame to
  the USB directly ?
- `dump_mgntframe_and_wait_ack` does sleep (mutex `ack_tx_mutex` + `rtw_sctx_wait`,
  `rtw_mlme_ext.c:7789-7796`): forbidden from timers or atomic context.
- There is no mutex on the mgnt TX path: the mutual exclusion is the frame/buffer queue. If the kext
  can call concurrently, serialization is a task of the glue ?

## 2. Direct programming of port 1

### 2.1 Register map

| Register | Address | Definition |
|---|---|---|
| `MSR` | 0x0102 (`REG_CR + 2`) | `Source/rtl8814au/include/hal_com_reg.h:116,584` |
| `REG_BCN_CTRL` / `REG_BCN_CTRL_1` | 0x0550 / 0x0551 | 371-372 |
| `REG_TSFTR` / `REG_TSFTR1` | 0x0560 / 0x0568 | comment in `hal_com.c:4132-4133` |
| `REG_RCR` | 0x0608 | `hal_com_reg.h:413` |
| `REG_MACID` / `REG_BSSID` | 0x0610 / 0x0618 | 418-419 |
| `REG_MACID1` / `REG_BSSID1` | 0x0700 / 0x0708 | 506-507 (8814A mirror: `Source/rtl8814au/include/rtl8814a_spec.h:433-434`) |

Bits: network type of port 1 = `REG_CR` BIT[19:18] = bits 3:2 of byte `MSR`
(comment at `Source/rtl8814au/hal/hal_com.c:2407`); values `_HW_STATE_ADHOC_` = 0x01
(`Source/rtl8814au/include/rtw_mlme_ext.h:53`, equal to `MSR_ADHOC`,
`hal_com_reg.h:667`). `RCR_CBSSID_DATA` = BIT(6) and `RCR_CBSSID_BCN` = BIT(7)
(`hal_com_reg.h:1134-1135`). In `REG_BCN_CTRL_1`, `EN_TXBCN_RPT` = BIT(2),
`EN_BCN_FUNCTION` = BIT(3), `DIS_TSF_UDT` = BIT(4) (`hal_com_reg.h:1533-1534,1540`): the
0x10 value of the live dumps equals DIS_TSF_UDT only, without beacon function nor TX report.

### 2.2 Why the port 1 registers stay free

Every writer of the vendor tree goes through `adapter->hw_port`, which with concurrent off is
`HW_PORT0` always (`usb_intf.c:1320`; the `HW_PORT1` of
`Source/rtl8814au/os_dep/linux/os_intfs.c:3142` is only touched by `rtw_drv_add_vir_if`, 3111,
nonexistent without concurrent mode). The exception is the AWDL glue: `rtl8814native_radio_awdl_port`
writes the port 1 registers directly (section 2.5). Specifically:

- `REG_MACID1`/`REG_BSSID1`: only `_get_macaddr_reg`/`_get_bssid_reg` reach them under
  `CONFIG_CONCURRENT_MODE` (`Source/rtl8814au/hal/hal_com.c:3376-3391` and 3448-3463), and
  `rtw_hal_set_macaddr_port`/`rtw_hal_set_bssid` (3397-3419, 3468-3482) use the port of the
  adapter.
- `REG_BCN_CTRL_1`: `hw_var_set_bcn_func` only picks it with concurrent + `HW_PORT1`
  (`hal_com.c:11254-11256`); `rtw_hal_correct_tsf` touches `REG_BCN_CTRL_1`/`REG_TSFTR1` only
  when the parameter is `HW_PORT1` (11801-11832), and with concurrent off nobody passes it.
- Bits 3:2 of `MSR`: `rtw_hal_set_msr` case `HW_PORT1` (2406-2410), not reachable with
  `hw_port = HW_PORT0`.
- `hw_var_port_switch` (full port swap) is under
  `CONFIG_RUNTIME_PORT_SWITCH`, which is only defined with concurrent mode
  (`Source/rtl8814au/include/autoconf.h:115`).

That is why the dumps show `REG_BCN_CTRL_1 = 0x10` in normal operation (HANDOFF_AWDL.md,
state of 2026-10-30; commit `ab5e120`).

### 2.3 Write helpers from the C glue

- Direct: `rtw_write8`/`rtw_write16`/`rtw_write32` (macros over `_rtw_write8/16/32`,
  declared at `Source/rtl8814au/include/rtw_io.h:328-330`, macros 391/425; read analogs
  `rtw_read8/16/32`). MAC write pattern: loop of 6 bytes over the base register
  (`hal_com.c:3415-3416` for MACID, 3478-3479 for BSSID).
- High level: `rtw_hal_set_hwreg` (`Source/rtl8814au/hal/hal_intf.c:622`) with
  `HW_VAR_MEDIA_STATUS` → `rtw_hal_set_msr` (`hal_com.c:12152-12155`, 2385),
  `HW_VAR_BSSID` → `rtw_hal_set_bssid` (`hal_com.c:12171-12172`, 3468),
  `HW_VAR_RCR` → `hw_var_rcr_config` (`hal_com.c:12174-12175`, 3680: writes `REG_RCR` at 3687 and
  updates the `hal_data->ReceiveConfig` cache at 3689).

**Caution**: the high-level helpers operate on `adapter->hw_port` (port 0 here).
To touch port 1 there are two ways: direct writes with `rtw_write8/16/32` to 0x700,
0x708 and 0x102, or temporarily changing `adapter->hw_port` to `HW_PORT1` around the call
to `rtw_hal_set_hwreg`. The second touches shared adapter state and competes with the MLME; the
first is the one the tree itself uses for port 1 (6-byte loops). The bits of `MSR` are
maintained with read-modify-write: `val8 = rtw_read8(adapter, MSR) & 0x03; val8 |= net_type << 2;`
(replicating `hal_com.c:2408-2410`). To put ad-hoc on port 1: `_HW_STATE_ADHOC_ << 2` =
0x04 into the high bits, preserving bits 1:0 of port 0.

For `RCR_CBSSID_DATA` use `rtw_hal_rcr_clear(adapter, RCR_CBSSID_DATA)`
(`hal_com.c:3735-3749`): it reads `HW_VAR_RCR`, clears the bit and writes only if it changes, so the
`ReceiveConfig` cache stays coherent; a raw write of `REG_RCR` would desynchronize that
cache.

### 2.4 What restores the registers on disconnect and scan

- **Disconnect**: `rtw_mlmeext_disconnect` (`Source/rtl8814au/core/rtw_mlme_ext.c:12372`)
  calls `HW_VAR_MLME_DISCONNECT` (12403), `HW_VAR_BSSID` with a null address (12404),
  `rtw_hal_rcr_set_chk_bssid(MLME_STA_DISCONNECTED)` (12406) and `Set_MSR(_HW_STATE_STATION_)`
  (12409). Everything goes through the port of the adapter (`HW_PORT0`): port 1 is not touched. `MACID1`,
  `BSSID1`, bits 3:2 of `MSR` and `BCN_CTRL_1` survive the disconnect.
- **Scan**: `hw_var_set_mlme_sitesurvey` (`Source/rtl8814au/hal/hal_com.c:11353`, reached
  by `HW_VAR_MLME_SITESURVEY`, 12323-12324) rewrites `REG_RXFLTMAP2` (11376/11405) and the RCR
  with `rtw_hal_rcr_set_chk_bssid` for `MLME_SCAN_ENTER`/`MLME_SCAN_DONE` (11378/11408). Port
  1 is not touched.
- **The exception is the RCR**, a single global register shared by both ports:
  `rtw_hal_rcr_set_chk_bssid` (`hal_com.c:3751`) recomputes `RCR_CBSSID_DATA`/`RCR_CBSSID_BCN`
  from the MI state on every MLME transition: connecting (14243 in
  `Source/rtl8814au/core/rtw_mlme_ext.c`), connected (12589), disconnected (12516, 14115),
  scan start/end (`hal_com.c:11378`, 11408). While `rtl8814native_awdl_port1` is 1,
  the same function keeps `RCR_CBSSID_DATA` clear on its own (`hal_com.c:3829-3830`) and
  `rtlRadioAwdlPort` invokes it when activating and deactivating the port (`Source/Driver/AWDLTx.c:100`):
  with the port running there is no need to re-apply the clear after every event. With the port
  off, a manual clear (`rtw_hal_rcr_clear`, 3735-3749) is restored on the next
  transition (the `|=` of `RCR_CBSSID_DATA` at 3833 and the final write at 3861).
- `MACID1`/`BSSID1`/`BCN_CTRL_1`/bits of `MSR` of port 1: no restoring writer exists
  in this configuration (section 2.2); their value persists until the glue changes it.

### 2.5 Scope of port 1 in this design

AWDL TX no longer requires association: without a network, the AirLayer layer parks the radio on the
idle channel with `RTL_RADIO_FN_SET_CHANNEL` (`rtlRadioSetChannel`, `RTLRadioABI.h:84`;
`rtl8814native_radio_set_channel`, `Source/Driver/AWDLTx.c:111-127`, channel at 20 MHz via
`rtw_set_chbw_cmd` and management rate at 6 Mb/s OFDM). The channel is visible in
`rtl_radio_status.idle_channel` (`RTLRadioABI.h:157-159`) and `txTick` only emits when the radio
is on a social channel (`Source/AirLayer/AWDLInterface.cpp:899-902` and 913-926). With the radio
associated (port 0, infra channel), AWDL TX goes through the MGNT queue of port 0 on the current
channel; port 1 takes no part in TX.

The role of port 1 is that the unicasts the peers send back to `awdl0` receive a hardware
ACK: `RTL_RADIO_FN_AWDL_PORT` (`rtlRadioAwdlPort`, `RTLRadioABI.h:74`) programs
`REG_MACID1` with the MAC of `awdl0`, `REG_BSSID1` with `00:25:00:FF:94:73` and the ad-hoc type into the
bits 3:2 of `MSR` (`Source/Driver/AWDLTx.c:87-93`); `REG_BCN_CTRL_1` stays intact at 0x10. The
layer activates it when starting to transmit and deactivates it when stopping
(`RTLAwdInterface::setRadioTx`, `Source/AirLayer/AWDLInterface.cpp:855-891`: call with the MAC
at 746, with NULL at 754). While it is active, `rtw_hal_rcr_set_chk_bssid` keeps
`RCR_CBSSID_DATA` clear so the AWDL frames are not dropped for not belonging to the infra
BSSID (section 2.4). The filtering and hardware ACK mechanics are developed in
`AWDL_RTL8814_RX_ACK.md`; only the write and restoration mechanisms are recorded here.

## 3. RX hook in `validate_recv_frame`

### 3.1 Exact point (already implemented)

`validate_recv_frame` is at `Source/rtl8814au/core/rtw_recv.c:2213`. The hook exists and occupies
lines 2309-2318, immediately before the `switch (type)` of line 2319:

- Condition: `u.hdr.len >= WLAN_HDR_A3_LEN` (24 bytes, `Source/rtl8814au/include/wifi.h:29`),
  `type != WIFI_CTRL_TYPE`, and `_rtw_memcmp(GetAddr3Ptr(ptr), rtl_awdl_bssid, ETH_ALEN)` —
  `GetAddr3Ptr` = `pbuf + 16` (`wifi.h:437`), `rtl_awdl_bssid = {00,25,00,FF,94,73}`
  (`Source/rtl8814au/core/rtw_recv.c:27`).
- Call: `rtl8814native_awdl_rx(adapter->pnetdev, ptr, precv_frame->u.hdr.len,
  pattrib->phy_info.recv_signal_power, rtw_get_oper_ch(adapter))` (2313-2315). If it returns
  non-zero, `retval = _FAIL` with `goto exit` (2316-2317) and the frame is freed in
  `recv_func_prehandle` after the return (`rtw_recv.c:4263-4266`); if it returns zero, it continues to the
  normal switch (mgnt → `validate_recv_mgnt_frame`, 1974).
- The copy is done by the recipient: `rtl8814native_awdl_rx` is at
  `Source/Driver/VendorGlue.cpp:235-241` and dispatches to `RTLEthernetAdapter::awdlInput`
  (`Source/Driver/RTLEthernetAdapter.cpp:242`), which delivers the bytes to the AWDL kext hook
  registered by `RTL_RADIO_FN_AWDL_HOOK`; "The bytes are only valid during the call: the hook
  copies what it keeps" (`RTLRadioABI.h:49-50`).

To copy "every frame whose A3 is the AWDL BSSID", this is the point: A3 is available without
parsing anything (offset 16), the type does not matter yet and the hook runs before any software
filtering.

### 3.2 Is the 802.11 header intact?

Yes. The USB chain is `recvbuf2recvframe`
(`Source/rtl8814au/hal/rtl8814a/usb/usb_ops_linux.c:105`): `rtl8814_query_rx_desc_status`
(144) fills the attrib from the descriptor, and `rtw_os_alloc_recvframe` (168) makes `rx_data`
point at `pbuf + shift_sz + drvinfo_sz + RXDESC_SIZE`, i.e. the first byte of the
802.11 header. `validate_recv_frame` runs inside `recv_func_prehandle`
(`Source/rtl8814au/core/rtw_recv.c:4237`, called at 4263), **before** `decryptor` (first
call of `recv_func_posthandle`, 4274 and 4287): the header is the raw one from the air. The AWDL
action frames are not encrypted, and the FCS is already removed: with `CONFIG_RX_PACKET_APPEND_FCS`
defined (`autoconf.h:252`) the initial RCR includes `RCR_APPFCS`
(`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:449-458`) and the driver subtracts
`IEEE80211_FCS_LEN` from `pkt_len` at `usb_ops_linux.c:163-166`. The bytes the hook receives are
those of the complete frame without FCS.

During a scan `hw_var_set_mlme_sitesurvey` writes `REG_RXFLTMAP2` with
`value_rxfltmap2` (`hal_com.c:11376`). In this configuration `CONFIG_FIND_BEST_CHANNEL` is
active (`autoconf.h:142`, under `CONFIG_AP_MODE` of line 127), so the value is 0xFFFF
(11364) and the management frames, including action, keep arriving during the scan; the
branch setting it to 0 (11367) is not compiled. Outside scan, AP-mode startups write it
to 0xFFFF (`hal_com.c:3137,3257`) and `hw_var_set_mlme_disconnect` to 0x0000 (11322): with
0xFFFF the action subtype (0xD) is accepted.

### 3.3 Fields of `rx_pkt_attrib` available at that point

`struct rx_pkt_attrib` at `Source/rtl8814au/include/rtw_recv.h:151-211`.

- **RSSI**: `pattrib->phy_info.recv_signal_power` (`Source/rtl8814au/include/cmn_info/rtw_sta_info.h:235`,
  field `recv_signal_power` of `phydm_phyinfo_struct` (225), embedded as `phy_info` at
  `rtw_recv.h:204`). It is filled by `rx_query_phy_status`
  (`Source/rtl8814au/core/rtw_recv.c:4718`) through PHYDM, which assigns
  `recv_signal_power = rx_power` with the comment "RSSI in dB"
  (`Source/rtl8814au/hal/phydm/phydm_phystatus.c:1985,2802`). It is processed before
  `validate_recv_frame`: `pre_recv_entry` calls `rx_query_phy_status` (4926) and then
  `rtw_recv_entry` (4927), which goes through `recv_func` → `recv_func_prehandle` (4263). It is the only
  piece of data the peer manager needs (AWDL_PEERMANAGER_DRIVER_ABI §1: threshold at +0x2c).
- **Channel**: there is no channel in the attrib for 8814A USB — `rtl8814_query_rx_desc_status`
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_rxdesc.c:20-60`) does not fill `pattrib->ch`
  (`rtw_recv.h:194`, "RX channel"), which stays at 0. The hook uses
  `rtw_get_oper_ch` (operating channel of the dvobj, `Source/rtl8814au/core/rtw_wlan_util.c:415-417`),
  correct while the radio does not hop channel, and therefore the AWDL queue cannot know which
  channel a frame came from if the channel changed in the interval ?
- **TSF**: `pattrib->tsfl` (`rtw_recv.h:200`) exists but is not filled on 8814A (the descriptor
  parser does not touch it); only the monitor radiotap builder consumes it
  (`rtw_recv.c:3949-3956`). In `validate_recv_frame` there is no per-packet mactime. For a
  hardware mactime the TSF counter must be read: `rtw_hal_get_tsftr_by_port(adapter, port)`
  (`Source/rtl8814au/hal/hal_com.c:11891`), which for 8814A selects the port at
  `REG_MBSSID_BCN_SPACE + 3` (0x554) bits [30:28] (11928-11932) and reads `REG_TSFTR` (0x0560,
  11934-11936).
  It is a read of the current counter, not of the reception instant ?
- Extra: `pattrib->data_rate` does come from the descriptor (`rtl8814a_rxdesc.c:55`).

## 4. Operational summary

| Action | Mechanism | Citations |
|---|---|---|
| Raw TX of AWDL action | `rtlRadioTx80211`: `alloc_mgtxmitframe` → copy bytes to `buf_addr+TXDESC_OFFSET` → driver sequence → `dump_mgntframe` | AWDLTx.c:21-61; SAEAuth.c:33-73; rtw_mlme_ext.c:7496,7616,7734 |
| Rate/retries | `attrib.rate = pmlmeext->tx_rate` (6M OFDM at 5 GHz), `retry_ctrl=TRUE` → RETRY_LIMIT 6 | rtw_mlme_ext.c:1062,7672,7685; rtl8814au_xmit.c:232,259-263 |
| Send context | workitem (`sae_work` pattern); no sleeps in `dump_mgntframe` | SAEJoin.c:591,615,632,651; hal_intf.c:814 |
| Port 1 ad-hoc | `rtlRadioAwdlPort`: `REG_MACID1` 0x700, `REG_BSSID1` 0x708 and `MSR` (0x102) bits 3:2 = 01b via `rtw_write8`; running only while awdl0 transmits (`setRadioTx`) | AWDLTx.c:77-102; AWDLInterface.cpp:855-891; hal_com.c:2406-2410; hal_com_reg.h:506-507,584 |
| Idle channel | `rtlRadioSetChannel`: radio parked at 20 MHz unassociated, visible in `idle_channel` | RTLRadioABI.h:84,157-159; AWDLTx.c:111-127 |
| `REG_BCN_CTRL_1` | do not touch (0x10 = DIS_TSF_UDT) | hal_com_reg.h:1540 |
| `RCR_CBSSID_DATA` | clear while `rtl8814native_awdl_port1` = 1 (`rtw_hal_rcr_set_chk_bssid` recomputes it); with the port off, manual clear that is restored on each MLME transition | hal_com.c:3735-3749,3751,3829; AWDLTx.c:100; rtw_mlme_ext.c:12516,12589 |
| AWDL RX | hook at `validate_recv_frame` 2309-2318 (A3 = 00:25:00:FF:94:73), copy in the hook | rtw_recv.c:27,2309-2318 |
| RX data | RSSI `phy_info.recv_signal_power`; channel `rtw_get_oper_ch`; no TSF per packet | rtw_recv.c:2314,4718; hal_com.c:11891 |
