# RTL8814AU hardware support for AWDL per the driver code

Inventory of what the RTL8814AU can do (per the registers and the code the kext compiles
today) for each obligation [`AWDL_FIRMWARE_DUTIES.md`](AWDL_FIRMWARE_DUTIES.md) attributes to the
Broadcom firmware and classifies in its §6 for a port without AWDL firmware. It answers six questions:
second concurrent MAC/port, TSF and timing, TX of action frames, speed of channel
change, RX of action frames from unknown peers, and a final table mapping each duty of §6 of
`AWDL_FIRMWARE_DUTIES.md` to what the RTL8814 route offers, with feasible / partial / missing and the
concrete gaps.

Code base: the vendored Realtek tree in `Source/rtl8814au/` (74 files compiled by
[`build.sh`](../../build.sh) `:37-56` and `:99-123`), the macOS glue in
`Source/Compat/VendorOsDep.c` and `Source/Driver/`. The `file:line` citations are relative to the repo root.
Anything unconfirmed in the code goes with `?`; no conclusion implies an RTL8814 hardware test of an
AWDL sequence. Complements [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
(§4 "Radio work") with the hardware side.

## 1. Short summary

1. **Second MAC: the hardware yes; this repo's driver, no.** The 8814A MAC has five pairs of
   MAC/BSSID registers (ports 0-4) and an 8-entry MBSSID CAM; but the current build is
   single-interface (`CONFIG_CONCURRENT_MODE` commented at `Source/rtl8814au/include/autoconf.h:113`,
   `CONFIG_IFACE_NUMBER = 1` at `Source/rtl8814au/include/drv_conf.h:361`) and the code of ports 1-4
   only compiles under `CONFIG_CONCURRENT_MODE` (`Source/rtl8814au/hal/hal_com.c:3373-3386`,
   `Source/rtl8814au/hal/hal_com.c:3445-3461`). For AWDL it suffices to treat it as management
   frames with arbitrary A1/A3 on the mgnt queue, without opening a second port.
2. **TSF: direct read and write per port** (`REG_TSFTR` 0x560 / `REG_TSFTR1` 0x568,
   `Source/rtl8814au/include/hal_com_reg.h:383-384`; write with a dedicated function at
   `Source/rtl8814au/hal/hal_com.c:11797-11834`). **There is no usable TBTT/early interrupt on USB**:
   the interrupt mask is programmed to 0 (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:1947-1984`)
   and the beacon/TBTT bits exist in the map but nobody enables them. Availability windows
   will be programmed with host software timers.
3. **TX of action frames: today it works as a standard mgnt frame** — queue `QSLT_MGNT` (0x12,
   `Source/rtl8814au/include/hal_com.h:262`), fixed rate in the descriptor
   (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:232-236`), retry 6/12
   (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:259-263`). **Scheduled TX does not exist** (neither
   timestamp nor per-frame channel): sending at a precise time requires a combination of
   `REG_TXPAUSE` (`Source/rtl8814au/include/hal_com_reg.h:350`) + software timer, and sending
   on another channel requires changing channel first (§5).
4. **Channel change: synchronous, on the order of tens of ms without IQK.** The chain
   `set_channel_bwmode` → `rtw_hal_set_chnl_bw` → `phy_SwChnlAndSetBwMode8814A` reprograms RF
   (4 paths), AGC table, CCK filters and power tables, and only runs IQK if asked
   (`HW_VAR_DO_IQK`): scanning skips it and advances at ~100 ms per channel on real hardware
   (`Docs/ARCHITECTURE.md:113`); the join asks for it (`Source/rtl8814au/core/rtw_mlme_ext.c:14250-14257`).
5. **RX of action frames from unknown peers: the hardware already accepts them** (RCR with AAP/AM/AMF and
   `RXFLTMAP0 = 0xFFFF` verified live, `Docs/regdumps/macos_c896a978_idle.txt:387`,
   `:393`); whoever discards them is the vendored core in software (`mgt_dispatcher` requires A1 = own
   MAC or broadcast, `Source/rtl8814au/core/rtw_mlme_ext.c:1317-1331`). The vendor core's
   monitor mode exists but the macOS port left it as a stub (`Source/Compat/VendorOsDep.c:1594-1598`).
6. The table of §7 classifies each duty: part (a) of §6.1 of `AWDL_FIRMWARE_DUTIES.md` is
   feasible; (b) periodic timing is partial (everything goes to software, no hardware timing help);
   (c) per-frame work is feasible with software retention, except the temporal precision of scheduled
   TX and the CCA statistics.

## 2. Second concurrent MAC/port

### 2.1 Port registers of the 8814A MAC

- The driver defines five hardware ports: `enum _hw_port { HW_PORT0..HW_PORT4 }`
  (`Source/rtl8814au/include/drv_types.h:1393-1399`).
- Each port has a MAC/BSSID pair: `REG_MACID` 0x0610 / `REG_BSSID` 0x0618 (port 0,
  `Source/rtl8814au/include/hal_com_reg.h:418-419`), `REG_MACID1` 0x0700 / `REG_BSSID1` 0x0708
  (`:506-507`), `REG_MACID2` 0x1620 / `REG_BSSID2` 0x1628 (`:518-519`), `REG_MACID3` 0x1630 /
  `REG_BSSID3` 0x1638 (`:521-522`), `REG_MACID4` 0x1640 / `REG_BSSID4` 0x1648 (`:524-525`).
- The per-port register selection is in `_get_macaddr_reg` / `_get_bssid_reg`
  (`Source/rtl8814au/hal/hal_com.c:3373-3386` and `:3445-3461`): ports 2-4 are only referenced
  under `CONFIG_RTL8814A` **and** `CONFIG_CONCURRENT_MODE`; the setter `rtw_hal_set_macaddr_port`
  (`:3394-3411`) and `rtw_hal_set_bssid` (`:3465-3480`) write the adapter port's pair.
- The second port has its own beacon and TSF infrastructure: `REG_BCN_CTRL_1` 0x0551
  (`Source/rtl8814au/include/rtl8814a_spec.h:359`), `REG_TXPKTBUF_BCNQ1_BDNY` programmed in the USB init
  (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:201-202`) and `REG_TSFTR1` 0x568
  (`Source/rtl8814au/include/hal_com_reg.h:384`).
- Switching of the per-port selectable shared register is done by
  `rtw_enter_protsel_port` → `REG_PORT_CTRL_SEL` (`Source/rtl8814au/hal/hal_com.c`), used e.g.
  by `rtw_hal_set_tsf_update` (`Source/rtl8814au/hal/hal_com.c:3482-3510`).

### 2.2 MBSSID CAM and BSSID filter

- The MBSSID CAM has 8 software-managed entries
  (`TOTAL_MBID_CAM_NUM = 8`, `Source/rtl8814au/include/drv_types.h:1004`), written through the
  registers `REG_MBIDCAMCFG_1/2` 0x0628/0x062C (`Source/rtl8814au/include/hal_com_reg.h:421-422`)
  with the case `HW_VAR_MBSSID_CAM_WRITE/CLEAR` of `SetHwReg`
  (`Source/rtl8814au/hal/hal_com.c:12187-12205`) and the number of APs in `REG_MBID_NUM` 0x0552
  (`Source/rtl8814au/include/hal_com_reg.h:373`, write at
  `Source/rtl8814au/hal/hal_com.c:3012-3016`).
- `RCR_ENMBID` (bit 24) means "answer ACK only to frames whose A1 matches the
  MBSSID CAM entries" (`Source/rtl8814au/include/hal_com_reg.h:1117`); `rtw_hal_set_macaddr_mbid`
  writes the adapter's MAC into an entry (`Source/rtl8814au/hal/hal_com.c:3298-3322`) and
  `rtw_mbid_cam_enable` adds the bit to the RCR (`:3018-3022`).
- Without MBSSID CAM, the BSSID filter is made of the bits `RCR_CBSSID_BCN` (bit 7) and `RCR_CBSSID_DATA`
  (bit 6) of the RCR (`Source/rtl8814au/include/hal_com_reg.h:1134-1135`), which compare against the
  port's `REG_BSSID`; `rtw_hal_rcr_set_chk_bssid` enables/disables them per MLME state
  (`Source/rtl8814au/hal/hal_com.c:3827-3857`) and the USB boot value is
  `RCR_APM|AM|AB|CBSSID_DATA|CBSSID_BCN|APP_ICV|AMF|HTC_LOC_CTRL|APP_MIC|APP_PHYST_RXFF`
  (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:450`).

### 2.3 Port state and gaps

- The current build is single-interface: `CONFIG_CONCURRENT_MODE` commented
  (`Source/rtl8814au/include/autoconf.h:113`), `CONFIG_IFACE_NUMBER = 1`
  (`Source/rtl8814au/include/drv_conf.h:357-363`), `CONFIG_MI_WITH_MBSSID_CAM` only with
  `CONFIG_IFACE_NUMBER > 2` (`:409-417`), and the 8814A is excluded by comment from
  `CONFIG_HWMPCAP_GEN2` (`:380`, the generation of 8822B/8821C/8822C; the 8814B uses an Address CAM of
  128 entries per `:382`). With one interface, `HW_VAR_MAC_ADDR` writes per port and not per CAM
  (`Source/rtl8814au/hal/hal_com.c:12160-12165`).
- The tree officially supports multi-interface on other chips (compilation errors for
  `> 3` GEN1 interfaces, `Source/rtl8814au/include/drv_conf.h:397-399`); for 8814A the driver's
  multi-port capability classification was left undefined. Whether port 1 really works with the
  USB path (beacon queue 1, `BCNDOK1` IRQ, etc.) is unproven in this repo and in hardware
  `?` — the per-port interrupt bits exist (`UHIMR_BCNDOK1`/`BCNDMAINT1`,
  `Source/rtl8814au/include/hal_com_reg.h:970-976`) but the whole mask is at zero (§3.3).
- **Verdict for AWDL:** a second MAC with its own BSSID is hardware-feasible, but it is not
  necessary: AWDL frames are management with multicast A1/non-BSS A3 that the mgnt queue emits with
  arbitrary A1/A3 without registering in any BSSID filter (§4). The point where a second
  port would matter is concurrency with the infra station (§7, duty "per-peer cache" and
  channel coexistence): the channel is a single chip resource (§5) and any simultaneity
  2.4/5 GHz is time-shared by software, as on Broadcom `?`.

## 3. TSF and timing

### 3.1 TSF read and write

- `REG_TSFTR` 0x0560 (port 0) and `REG_TSFTR1` 0x0568 (port 1),
  `Source/rtl8814au/include/hal_com_reg.h:383-384`. The read is a direct `rtw_read32`; the
  radio-measurement module uses it to generate random intervals
  (`Source/rtl8814au/core/rtw_rm_fsm.c:591`).
- The write is implemented in `rtw_hal_correct_tsf`
  (`Source/rtl8814au/hal/hal_com.c:11797-11834`): for port 0 it disables `EN_BCN_FUNCTION` in
  `REG_BCN_CTRL` 0x0550, writes the 8 bytes at `REG_TSFTR`/`REG_TSFTR+4` and re-enables it; for
  port 1 it uses `REG_BCN_CTRL_1` and `REG_TSFTR1`. It is reached via `HW_VAR_CORRECT_TSF`
  (`Source/rtl8814au/hal/hal_com.c:12351-12354`); the variant with hardware sync
  (`CONFIG_HAS_HW_VAR_CORRECT_TSF`) only exists for 8814B
  (`Source/rtl8814au/include/hal_ic_cfg.h:513`, inside the `CONFIG_RTL8814B` block ending at
  `:521`), and is not defined for 8814A.
- The value written comes from software TSF tracking: `update_TSF` copies the beacon's TSF
  field to `pmlmeext->TSFValue` (`Source/rtl8814au/core/rtw_wlan_util.c:3584-3597`) and
  `correct_TSF` pushes it down to the hardware aligned to the beacon interval
  (`Source/rtl8814au/hal/hal_com.c:11836-11863`: `tsf = TSFValue - mod64(TSFValue, bcn_interval*1024) - 1024`).
- The local TSF is also updated by hardware with each received beacon if `EN_BCN_FUNCTION`
  is active and `DIS_TSF_UDT` is clear (`REG_BCN_CTRL`, init at
  `Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:3056` and `:3090`); the command
  `EN_HW_UPDATE_TSF` re-enables it from the cmd thread
  (`Source/rtl8814au/core/rtw_cmd.c:2447` and `:5657-5658`).

### 3.2 Hardware timers

- The register map exposes five timers `REG_TC0_CTRL..REG_TC4_CTRL` 0x0150-0x0160 and
  `REG_TCUNIT_BASE` 0x0164 (`Source/rtl8814au/include/rtl8814a_spec.h:103-110`), and
  `REG_TIMER0/TIMER1` 0x0584/0x0588 (`Source/rtl8814au/include/hal_com_reg.h:388-389`). In the
  compiled tree there is no reference to the TCs (`?` firmware only); the only uses of
  `REG_TIMER0` are the clock source selection in `hw_port0_tsf_sync_sel`
  (`Source/rtl8814au/hal/hal_com.c:11593-11630`), which sits under `CONFIG_HW_P0_TSF_SYNC`
  (`Source/rtl8814au/include/hal_ic_cfg.h:499`, 8814B block) and does not compile for 8814A.
- All the driver's periodic timing is software: compat timers (e.g. the timer of
  `periodic_tsf_update_end`, `Source/rtl8814au/hal/hal_com.c:3670-3679`) and the cmd thread. For the
  AWDL availability windows the consequence is direct: aligning with Broadcom's firmware TSF
  is replaced by reading `REG_TSFTR` and firing a host timer; precision will depend on
  XNU's timer dispatcher and the TX queue `?` (no tolerance constant in the
  code, same as Broadcom — `AWDL_FIRMWARE_DUTIES.md` §6.2).

### 3.3 TBTT/early interrupts: they exist in the map, they are not used

- The 8814AU is USB and works by polling URBs: `rtl8814au_init_default_value` leaves
  `IntrMask[0] = 0` and `IntrMask[1] = 0` with **all** bits commented, including
  `IMR_BCNDOK0`, `IMR_BCNDMAINT0`, `IMR_TXBCN0OK`, `IMR_TXBCN0ERR`, `IMR_GTINT3`, `IMR_GTINT4`
  (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:1947-1984`); the mask is written as is into
  `REG_HIMR0/REG_HIMR1` (`:257-259`).
- The bits exist: `UHIMR_TXBCNOK` (bit 25), `UHIMR_TXBCNERR` (26), `UHIMR_BCNDOK0..3`,
  `UHIMR_BCNDMAINT0..3`, `UHIMR_TSF_BIT32_TOGGLE` (24) and the general timers
  `UHIMR_GTINT3` (27) / `UHIMR_GTINT4` (28)
  (`Source/rtl8814au/include/hal_com_reg.h:962-1006`). The early beacon registers
  `REG_DRVERLYINT` 0x0558 and `REG_BCNDMATIM` 0x0559 are defined
  (`Source/rtl8814au/include/rtl8814a_spec.h:363-364`) and without writers in the code `?`; the
  option `CONFIG_INTERRUPT_BASED_TXBCN` is commented
  (`Source/rtl8814au/include/autoconf.h:129-136`).
- TBTT proximity is only regulated by `REG_TBTT_PROHIBIT` 0x0540 (setup/hold in units of
  32 µs, `Source/rtl8814au/include/rtl8814a_spec.h:342-355`, init at
  `Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:3066-3073`), which forbids TX near the TBTT
  but does not announce it. `REG_INT_MIG` 0x0304 (interrupt migration,
  `Source/rtl8814au/include/rtl8814a_spec.h:171`) is only used to load CPUs, not for timing
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_dm.c:120-124`).
- **AWDL consequence:** there is no hardware interrupt for "the window starts": the plan of
  `AWDL_FIRMWARE_DUTIES.md` §6.2 is executed with a software timer anchored to the TSF read by
  vendor request. Enabling `UHIMR_GTINT3/GTINT4` + `REG_TIMER0/1` as a hardware
  timer would be a native route, but in this tree there is no USB usage precedent `?`.

## 4. TX of management/action frames

### 4.1 Current path of a management frame

1. The MLME builders (`issue_auth`, `issue_action`, ...) allocate an `xmit_frame` of
   `MGNT_FRAMETAG` with `alloc_mgtxmitframe`
   (`Source/rtl8814au/core/rtw_mlme_ext.c:7548-7581`).
2. `update_mgntframe_attrib` fixes `qsel = QSLT_MGNT`, `mac_id = RTW_DEFAULT_MGMT_MACID`,
   `rate = pmlmeext->tx_rate`, `retry_ctrl = TRUE` and no encryption
   (`Source/rtl8814au/core/rtw_mlme_ext.c:7616-7689`). The default rate is 6 Mbit/s OFDM on
   5 GHz and 1 Mbit/s CCK on 2.4 GHz (`Source/rtl8814au/core/rtw_mlme_ext.c:1062-1064`), changeable
   with `update_mgnt_tx_rate` (`:7546-7552`) — the driver-side equivalent of `awdl_min_rate`.
3. `dump_mgntframe` (`:7734-7743`) calls `rtw_hal_mgnt_xmit`
   (`Source/rtl8814au/hal/hal_intf.c:816-831`), which computes RA/TA addresses and enqueues
   `rtl8814au_mgnt_xmit` → `rtw_dump_xframe` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:897-899`).
4. `update_txdesc` fills the 32-byte descriptor and `rtw_dump_xframe` writes it to the FIFO through the
   USB pipe (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:403`).

### 4.2 TX descriptor fields relevant to AWDL

- Queue: `SET_TX_DESC_QUEUE_SEL` with `pattrib->qsel`
  (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:108`); MACID and rate ID at
  `:105-106`. Hardware sequence when there is no QoS (`:110-114`).
- Branch `MGNT_FRAMETAG` (`:229-272`): `USE_RATE = 1` and `TX_RATE = MRateToHwRate(pattrib->rate)`
  (`:232-236`) — fixed rate, no rate adaptation for mgnt; retry limit 6 if `retry_ctrl` or 12
  if not (`:259-263`); ACK report bit `SPE_RPT` under `CONFIG_XMIT_ACK` (`:266-272`), which in
  this build is **commented** (`Source/rtl8814au/include/autoconf.h:63`): with that
  `dump_mgntframe_and_wait_ack_timeout` degrades to send and sleep 50 ms
  (`Source/rtl8814au/core/rtw_mlme_ext.c:7795-7808`). For AWDL this means the TX
  confirmation of the `awdl_oob_af` style (Broadcom events 60/69) has no equivalent today
  `?` — it would require activating `CONFIG_XMIT_ACK` or using the C2H reports.
- Macro-fields defined in `Source/rtl8814au/include/rtl8814a_xmit.h`: `TX_RATE`
  (`:184`), `DATA_RETRY_LIMIT` (`:189`), `RTS_RATE` (`:190`), `DATA_BW` (`:198`), `NAV_USE_HDR`
  (`:177`), descriptor checksum (`rtl8814a_cal_txdesc_chksum`,
  `Source/rtl8814au/hal/rtl8814a/rtl8814a_xmit.c:238-253`).
- Whether the frame goes out encrypted is decided by `rtl8814a_fill_txdesc_sectype`, but that branch only applies to
  `DATA_FRAMETAG` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:124-129`); an mgnt is
  sent unencrypted by default (PMF is added by software in the port with
  `rtl_pmf_protect_mgnt`, `Source/rtl8814au/core/rtw_mlme_ext.c:7761`).

### 4.3 Queue and USB pipe

- MAC queues: `QSLT_BK` 0x2, `QSLT_BE` 0x0, `QSLT_VI` 0x5, `QSLT_VO` 0x7, `QSLT_BEACON` 0x10,
  `QSLT_HIGH` 0x11, `QSLT_MGNT` 0x12, `QSLT_CMD` 0x13
  (`Source/rtl8814au/include/hal_com.h:256-263`).
- The vendor's queue→USB pipe mapping: with 3 bulk OUT (those of the TP-Link Archer T9UH: 0x05/0x06/0x08,
  `Docs/ARCHITECTURE.md:20`), `_ThreeOutPipeMapping` sends MGT (index 5), BCN (4), HIGH (6) and
  TXCMD (7) to pipe 0 — the high bulk OUT — and the ACs to 1/2
  (`Source/rtl8814au/hal/hal_com.c:1136-1181`, MGT→RtOutPipe[0] at `:1166` and `:1179`).
- On the macOS port, `ffaddr2pipehdl` uses `Queue2Pipe[addr]` with fallback to `RtOutPipe[0]`
  (`Source/Compat/VendorOsDep.c:112-135`). The AWDL mgnt would share a pipe with the
  highest-priority ACs `?` (the real priority is set by the MAC EDCA, not the pipe).

### 4.4 What does not exist: scheduled TX and per-frame channel

- The descriptor has no channel field nor emission time: the band fields
  (`DATA_BW`, `RTS_SC`) are only written on the beamforming NDPA route
  (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:237-256`) and the frame goes out on the
  current RF channel (`pmlmeext->cur_channel` is what governs the setup, §5).
- The MAC's only temporal TX control is `REG_TXPAUSE` 0x0522
  (`Source/rtl8814au/include/hal_com_reg.h:350`) via `HW_VAR_TXPAUSE`
  (`Source/rtl8814au/core/rtw_wlan_util.c:644-647`, used by the DFS proxy): pause/resume whole
  queues, useful as the gate of an availability window but without per-frame granularity.
- Transmission at a precise instant is emulated in software: enqueue into the mgnt queue and open
  TXPAUSE just before; the real delay will be that of the host's dispatcher plus draining the queue
  `?`. There is no driver-side equivalent of `awdl_oob_af` (TX within the next AW) nor of
  `awdl_oob_af_auto` (repetition per AW): both would be own software queues consulting the
  current window before dequeuing, as `AWDL_FIRMWARE_DUTIES.md` §6.3 anticipates.

## 5. Channel change

### 5.1 Full chain

`set_channel_bwmode` (`Source/rtl8814au/core/rtw_wlan_util.c:596-665`, under mutex
`setch_mutex`) → `rtw_hal_set_chnl_bw` (`Source/rtl8814au/hal/hal_intf.c:1001-1037`) →
`PHY_SetSwChnlBWMode8814` registered as handler (`Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:4457`)
→ `phy_SwChnlAndSetBwMode8814A` (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:2078-2143`),
**synchronous** execution on the calling thread (on USB there is no work item; the timer
callbacks `PHY_SwChnlTimerCallback8814A` are under `#if 0`, `:1992-2019`).

### 5.2 What it touches (and what it does not)

- **Band change**: `phy_SwBand8814A` reads `REG_CCK_CHECK` 0x0800 bit 7 to learn the current
  band and calls `PHY_SwitchWirelessBand8814A` only if it changes
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:989-1013`). That function turns off the
  CCK/OFDM clock gate (`:1163-1167`), selects the AGC table 0x958[4:0], reprograms the RFE pinmux of the 4
  antennas (`:1017-1137`, `:1200+`), rTxPath and rCCK_RX, and switches `REG_CCK_CHECK`
  (`:1168-1197`).
- **Channel**: `phy_SwChnl8814A` (`:1883-1978`) writes `rFc_area_Jaguar` (BB), the RF register
  `RF_CHNLBW_Jaguar` on **all** RF paths (`:1946-1962`), the per-band AGC table (`0x958[4:0]`,
  `:1964-1971`) and the CCK TX DFIR filters if 2.4 GHz (`:1973-1977`). The per-channel BB gain
  kfree only updates state (`:1907-1922`, the real call to `phydm_ConfigKFree` is in
  `#if 0`).
- **Power tables**: after the switch, `odm_clear_txpowertracking_state` +
  `rtw_hal_set_tx_power_level` reprogram the power indices of the 4 paths for the new
  channel (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:2109-2111` → `PHY_SetTxPowerLevel8814`
  `:636-668` → `phy_set_tx_power_level_by_path`,
  `Source/rtl8814au/hal/hal_com_phycfg.c:3044`).
- **IQK**: only if `bNeedIQK` was marked with `HW_VAR_DO_IQK`
  (`Source/rtl8814au/hal/hal_com.c:12148-12156`) is `halrf_iqk_trigger` called
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:2112-2117` →
  `Source/rtl8814au/hal/phydm/halrf/halrf.c:1694`). The 8814A IQK backups MAC/BB/RF and does LOK +
  IQK TX/RX per path with busy waits: `ODM_delay_ms(1)` per LOK (plus polling up to 10 ms)
  and `WBIQK_delay = 10` ms (`Source/rtl8814au/hal/phydm/halrf/rtl8814a/halrf_iqk_8814a.c:217-260`,
  `:342`; constants at `Source/rtl8814au/hal/phydm/halrf/halrf_iqk.h:30-31`),
  `ODM_delay_ms` = `rtw_mdelay_os` (busy wait,
  `Source/rtl8814au/hal/phydm/phydm_interface.c:613-622`). The join asks for IQK around the change
  (`Source/rtl8814au/core/rtw_mlme_ext.c:14250-14257`), the IBSS start too
  (`:11313-11336`); **scanning does not ask for it** (direct `set_channel_bwmode` at
  `:14828` without `HW_VAR_DO_IQK`).

### 5.3 Cost observed on hardware

- Scanning completes channels at ~100 ms of total dwell per channel with the change included
  (`Docs/ARCHITECTURE.md:113`), and the channel walk was verified by direct read of
  `RF_CHNLBW` (`Docs/ARCHITECTURE.md:131`) — i.e. the switch without IQK is an order of
  magnitude smaller than the dwell `?` (no isolated measurement of the switch itself).
- The RCR drops during scanning to accept beacons from any BSSID and is restored afterward
  (`Docs/ARCHITECTURE.md:130`); that pattern (`rtw_hal_rcr_set_chk_bssid`, §2.2) is the same one
  the AWDL window would use to listen on social channels without losing the infra station `?`.
- **AWDL consequence:** hopping through the `awdl_chan_seq` sequence is viable with the current chain
  as long as IQK stays off the hot path (as in scanning); a 2.4 ↔ 5 GHz
  hop adds the full band switch. The AWDL cadence (64-128 TU) leaves reasonable margin `?`; the
  AWDL social channels are 6/44/149, all without DFS `?` (149 is U-NII-3), so the DFS proxy of
  Broadcom (`awdl_dfsp_cfg`) need not intervene.

## 6. RX of action frames from unknown peers

### 6.1 Hardware filter: it already accepts them

- Semantics of the RCR (`Source/rtl8814au/include/hal_com_reg.h:1110-1139`): `RCR_AMF` (bit 13)
  accepts management, `RCR_AM` (bit 2) multicast, `RCR_AB` (bit 3) broadcast, `RCR_AAP` (bit 1)
  accepts all unicast, `RCR_CBSSID_BCN/DATA` (bits 7/6) compare BSSID only for beacon/probe-rsp
  and data. Management subtypes pass through the mask `REG_RXFLTMAP0` 0x0620 (one bit per
  subtype; the action subtype = 13).
- Live state associated with an AP: `RCR = 0xF40060CE` (AAP/AM/AB/AMF/CBSSID_DATA/CBSSID_BCN
  active) and `RXFLTMAP0 = 0xFFFF` / `RXFLTMAP1 = 0xFFFFFFFF` — the hardware delivers the
  management multicast frames, including action frames from any sender
  (`Docs/regdumps/macos_c896a978_idle.txt:387`, `:393-394`; same RCR documented at
  `Docs/ARCHITECTURE.md:1078`). `RXFLTMAP0` is not written by the driver in normal mode (only
  monitor and MP modes, §6.3): the value 0xFFFF is the chip's reset default `?` (consistent: there is no
  writer).
- AWDL action frames go with A1 broadcast (4893 of 4893 in iOS 26 captures,
  [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §1), A2 = peer's MAC (no `sta_info`) and A3 =
  `00:25:00:FF:94:73`: the CBSSID bits do not touch them (they only apply to beacon/data) and `RCR_AB|AMF` lets
  them pass to the RX DMA. AWDL data frames do hit `RCR_CBSSID_DATA`
  ([AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §2.3).

### 6.2 Software filtering: this is where they get lost today

- The RX chain: `recv_func` → `recv_func_prehandle` → `validate_recv_frame` → for management
  `validate_recv_mgnt_frame` (`Source/rtl8814au/core/rtw_recv.c:2305-2307`, definition
  `:1969-2032`, which looks up `psta` by A2 without discarding if NULL) → `mgt_dispatcher`
  (`Source/rtl8814au/core/rtw_mlme_ext.c:1259`).
- `mgt_dispatcher` discards every frame whose A1 is not the local MAC or broadcast
  (`Source/rtl8814au/core/rtw_mlme_ext.c:1317-1331`). AWDL action frames, with A1 broadcast,
  pass; unicast management to the MAC of `awdl0` (e.g. ADDBA) dies here.
- `OnAction` dispatches by category against `OnAction_tbl` and ignores unknown categories
  (`Source/rtl8814au/core/rtw_mlme_ext.c:7463-7483`). Category 127 is there:
  `RTW_WLAN_CATEGORY_P2P = 0x7f` (`Source/rtl8814au/include/ieee80211.h:1525`) →
  `OnAction_p2p` (`Source/rtl8814au/core/rtw_mlme_ext.c:96`, `:7398`), which discards the AWDL frame
  because it requires own A1 and the Wi-Fi Direct OUI `0x506F9A09`. Full walk and diversion point in
  [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §3 and §4.
- For control TX/RX (RXFLTMAP1) the driver only touches the RX BAR bit
  (`Source/rtl8814au/hal/hal_com.c:12385-12398`).

### 6.3 Monitor mode: exists in the vendor core, stub in the port

- Monitor entry: `hw_var_set_opmode` with `_HW_STATE_MONITOR_` saves the RCR and the
  RXFLTMAPs, writes `RCR_AAP|APM|AM|AB|APWRMGT|ADF|ACF|AMF|APP_PHYST_RXFF|APPFCS|ACRC32|AICV` and
  sets `RXFLTMAP0/1/2 = 0xFFFF` (`Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:3160-3196`),
  with restoration on exit (`:3198-3212`).
- In that state `recv_func` sends **every** frame to `recv_frame_monitor`
  (`Source/rtl8814au/core/rtw_recv.c:4377-4385`), which prepends a radiotap header — with the
  TSFT field included — and calls `rtw_recv_monitor`
  (`Source/rtl8814au/core/rtw_recv.c:4176-4216`; radiotap at
  `Source/rtl8814au/core/rtw_recv.c:3854+`, bit `IEEE80211_RADIOTAP_TSFT` at `:3937`).
- On Linux `rtw_recv_monitor` delivers the raw skb with `ETH_P_80211_RAW`
  (`Source/rtl8814au/os_dep/linux/recv_linux.c:643-686`). **On the macOS port the port left the
  function as a stub returning `_FAIL`** (`Source/Compat/VendorOsDep.c:1594-1598`): in monitor
  frames are discarded after the radiotap.
- Alternative without monitor: intercept in `mgt_dispatcher`/`validate_recv_mgnt_frame` the action
  frames with the AWDL category/OUI and deliver them before the A1 check (the port itself already
  makes hooks of this style there: PMF/SAE at `Source/rtl8814au/core/rtw_recv.c:1972-1993` and
  auth logging at `Source/rtl8814au/core/rtw_mlme_ext.c:1277-1296`).

### 6.4 Delivery upward and per-frame RSSI

- Today the only thing leaving the core toward the stack is data traffic converted to Ethernet:
  `rtw_os_recv_indicate_pkt` intercepts EAPOL for the internal supplicant and the rest is delivered through
  `rtl_netif_rx` to `en2` (`Source/Compat/VendorOsDep.c:1506-1523`). Management frames never
  reach IOKit: the equivalent channel to Broadcom's event 75
  (`WLC_E_ACTION_FRAME_RX`, `AWDL_FIRMWARE_DUTIES.md` §4.2) and the design's BPF tap do not exist yet
  (`AWDL_PROTOTYPE_DESIGN.md` §5) `?` (AirLayer integration point to be decided).
- Per-frame RSSI is available for an `awdl_af_rssi`-style filter: the RX descriptor's PHY
  state is parsed by `odm_phy_status_query` (`Source/rtl8814au/core/rtw_recv.c:4785`) and
  fills `attrib->phy_info` (`Source/rtl8814au/include/rtw_recv.h:204`) with
  `rx_mimo_signal_strength[i]` per path (`Source/rtl8814au/hal/phydm/phydm_phystatus.c:973`).
- The descriptor itself exposes RX rate, seq, encrypt, `physt` and report type
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_rxdesc.c:20-62`; port call at
  `Source/Compat/VendorOsDep.c:498`).

## 7. Table: AWDL duties (`AWDL_FIRMWARE_DUTIES.md` §6) vs the RTL8814 route

| Duty (§ of `AWDL_FIRMWARE_DUTIES.md`) | What the RTL8814 route offers | State | Concrete gaps |
|---|---|---|---|
| **6.1 (a)** Pure configuration: store and return (SSID/STATE/BSSID/CHANNEL, capabilities, min rate, presence mode, election id/metric, …) | Requires no hardware: AirLayer ivars with the structs of `AWDL_STRUCTS.md`. `AWDL_MIN_RATE` has a real analog (`update_mgnt_tx_rate`, `rtw_mlme_ext.c:7546`); the rest is storage. | **feasible** | None for the storage part; each value only "acts" via (b)/(c). |
| **6.2 (b1)** Availability windows: `aw_period`/`aw_len`/`aw_ext_len`/`adv_period`, window start events (fw 96) | TSF readable/writable per port (`hal_com_reg.h:383-384`, `hal_com.c:11797`); `REG_TXPAUSE` as TX gate (`hal_com_reg.h:350`); host software timer. No usable TBTT/early interrupt on USB (`usb_halinit.c:1947-1984`) and no equivalent of event 96. | **partial** | Window machine 100 % software (timer + state); host scheduler jitter unmeasured `?`; GTINT3/GTINT4+`REG_TIMER0/1` as a hardware timer is an option without USB precedent in this tree `?`. |
| **6.2 (b2)** Master election (election id, metric, RSSI thresholds; resulting role, fw 97) | Per-frame RSSI available (`rtw_recv.c:4785`, `phydm_phystatus.c:973`); RX of sync frames feasible with the changes of §6; the machine is pure software (or `IO80211AWDLPeerManager`, as `AWDL_FIRMWARE_DUTIES.md` §1.5 anticipates). | **feasible** | Depends on opening the RX of action frames from unknown peers (§6.2/6.3); the role state is fed locally (without event 97). |
| **6.2 (b3a)** Channel hopping per `awdl_chan_seq` (≤ 160 B encoded) | Synchronous channel change chain proven at ~100 ms/channel in scanning (`ARCHITECTURE.md:113`), IQK skipped on the hot path (`rtw_mlme_ext.c:14828`), full band switch when 2.4↔5 GHz (`rtl8814a_phycfg.c:989-1197`). | **feasible** (with timing reserve) | Precision of the hop instant tied to the software timer `?`; without calibration on the hot path, each entry into 5 GHz reuses the band IQK `?`. |
| **6.2 (b3b)** Periodic TX of the sync frame (`awdl_payload` + `awdl_psf_dwell`) | Mgnt frame with fixed rate and low retry (§4.2), queue `QSLT_MGNT`, no offload; repetition by software. | **partial** | No scheduled TX nor hardware repetition: each PSF is a software enqueue anchored to the timer of (b1); precision ±jitter `?`. |
| **6.3 (c1)** AF TX header (`awdl_af_hdr`) and RX filter by RSSI (`awdl_af_rssi`) | SNAP/header construction local in software (already contemplated in `AWDL_PROTOTYPE_DESIGN.md` §6); per-frame RSSI in `attrib->phy_info`. | **feasible** | The RSSI filter and the header are new AirLayer code; RX must be opened first (§6.2). |
| **6.3 (c2)** Per-peer cache to decide transmitability (`awdl_peer_op`, counters `peeropdrop`/`noawchansw`) | All software: own table or `sta_info`; MACID/CAM limits very wide for 4 typical AWDL peers (`MACID_NUM_SW_LIMIT 32`, `SEC_CAM_ENT_NUM_SW_LIMIT 32`, `drv_conf.h:443-444`). | **feasible** | Retention/discard logic per window is new; there is no offload. |
| **6.3 (c3)** Point OOB TX (`awdl_oob_af`, confirmation via events 60/69), automatic repetition (`awdl_oob_af_auto`, `awdl_afs_pload`) | Immediate mgnt enqueue available (§4); TX confirmation without `CONFIG_XMIT_ACK` does not exist (`autoconf.h:63`; fallback to a 50 ms sleep, `rtw_mlme_ext.c:7795-7808`). | **partial** | No real TX/ACK confirmation until activating `CONFIG_XMIT_ACK` or reading the C2H reports `?`; per-AW repetition = software requeue. |
| **6.3 (c4)** Datapath encryption (`wsec_key`/`wsec`) | 32-entry security CAM (`TOTAL_CAM_ENTRY`, `hal_com_reg.h:836`) written by `rtw_sec_write_cam_ent` (`hal_com.c:2576-2628`); installation by cmd (`rtw_setstakey_cmd`, `rtw_cmd.c:1701`). | **feasible** | AWDL per-peer group keys and their rotation are new logic `?`; the core's SW encryption already exists as a fallback (`uc_swdec_pending_queue`, `rtw_recv.c:4400-4420`). |
| Firmware extras: DFS proxy (`awdl_dfsp_cfg/ucsa`, fw 80), CCA per channel (`cca_get_stats`), `awdl_stats` | `CONFIG_DFS` compiled (`autoconf.h:85`) and `HW_VAR_TXPAUSE` with radar detect in `set_channel_bwmode` (`rtw_wlan_util.c:626-647`), but it is own DFS master, not proxy; PhyDM CCX/NHM exists in the tree but is not in the build list (`build.sh:99-123` does not include `phydm_ccx.c`). | **missing** (DFS proxy) / **missing** (CCA/stats) | AWDL social channels do not require DFS `?`; the `awdl_stats`/`cca_get_stats` statistics would require enabling additional PhyDM modules or counting in software. |

## 8. Method and limits

- Directed search in the vendored tree (`Source/rtl8814au/`) by symbols: `MBID/MBSSID`, `TSF`,
  `TBTT`, `HIMR/IMR`, `RCR/RXFLTMAP`, `SwChnl/IQK`, `mgnt_xmit`, `QSLT`, `monitor/radiotap`,
  `correct_TSF`, `TXPAUSE`; reading of the cited functions in full and of the compilation
  conditionals (`autoconf.h`, `drv_conf.h`, `hal_ic_cfg.h`) to know which code is actually
  compiled in this kext.
- The live values of RCR/RXFLTMAP and the scanning timing come from already documented
  measurements of the project itself (`Docs/ARCHITECTURE.md`, `Docs/regdumps/`), not from new tests.
- Main `?`: default value of `RXFLTMAP0` (no writer in normal mode; the live value is
  0xFFFF), real functioning of port 1 on the USB path (BCNQ1, port IRQ), temporal
  precision of host timers against the AWDL cadence, exact category and OUI of the AWDL
  action frame vs `OnAction_tbl`, viability of `GTINT3/GTINT4` as a hardware timer on the 8814AU,
  and the integration point of the delivery of action frames toward AirLayer/BPF.
- No conclusion implies an RTL8814 hardware test of an AWDL sequence.
