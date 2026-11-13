# Radio time sharing: infrastructure BSS and AWDL on the RTL8814AU

Can the vendor tree (`Source/rtl8814au`) share the radio between an infrastructure BSS on
a non-social channel (example: 5 GHz channel 153 at 80 MHz) and AWDL on channel 149 at 20 MHz, serving
the AWDL slots while the station stays associated, as a real Mac does? Analysis based
only on the vendor tree source; none of this has been measured on hardware. File paths are
relative to `Source/rtl8814au/`. Unverified items are marked `?`.

## 1. Verdict

Yes, via software. The tree already contains almost the whole necessary mechanism — it is the same as the
back-op of the linked scan (§5): null data with PM=1, channel and width switch, and null data with
PM=0 on return. The cost of a switch is estimated at 10–15 ms (§4), dominated by the reload
of the TX power table (~250 USB writes): a slot of 4 AWs (65.5 ms) endures the way out and
back with ~35–45 ms on the air; a single AW (16.4 ms) is not enough even for one switch.

The firmware MCC (`CONFIG_MCC_MODE`, `hal/hal_mcc.c`) exists in the tree and is portable with
moderate effort, but it depends on the 8814A firmware implementing the H2C MCCs, something unverified
(§2). The manual per-slot path does not depend on the firmware.

Useful note of the concrete case: primary 153 at 80 MHz has center channel 155, so the
80 MHz block of the AP covers 149, 153, 157 and 161. With AWDL on 149 at 80 MHz the center stays at 155: the
driver does not rewrite the RF channel and the switch reduces to the primary subchannel change
(§4.1), some 22 operations if the power reload is omitted, which with the same center is
redundant.

## 2. MCC: `CONFIG_MCC_MODE`, `hal_mcc.c` and the firmware H2Cs

**It exists, but it is not compiled.**

- The full code is in `hal/hal_mcc.c`, entirely under `#ifdef CONFIG_MCC_MODE`
  (`hal/hal_mcc.c:15`), with its header `include/rtw_mcc.h`. This version has no
  `core/rtw_mcc.c`: the 21 exported functions declared in `include/rtw_mcc.h:241-308`
  (`rtw_hal_mcc_*`, `rtw_mcc_cmd_hdl`, `rtw_set_mcc_duration_cmd`, …) are implemented in
  `hal/hal_mcc.c`.
- Hook points in the core, all conditioned: `core/rtw_p2p.c:3450,3615,4524`,
  `core/rtw_cmd.c:3299,5689`, `core/rtw_mlme_ext.c:7634,8071,12948,14942,15221,15523,16402`,
  `core/rtw_tdls.c:197`, besides `rtw_ap.c`/`rtw_xmit.c`. Module parameters already present:
  `rtw_en_mcc`, `rtw_mcc_duration` (`os_dep/linux/os_intfs.c:871,881,888,897`).
- `CONFIG_MCC_MODE` is not defined in `include/autoconf.h`. The restriction is in
  `include/drv_conf.h:41-57`: it requires `CONFIG_CONCURRENT_MODE` and fails the build if
  `CONFIG_BT_COEXIST` or `CONFIG_TDLS` is defined — neither is defined, so there is
  no conflict. `CONFIG_CONCURRENT_MODE` is not defined either (`autoconf.h:113` commented).
- `build.sh` does not compile `hal/hal_mcc.c` (the `VENDOR_SOURCES` list does not mention it), although it
  does compile all the core files that would call it (`core/rtw_mi.c`, `rtw_cmd.c`, `rtw_mlme_ext.c`,
  `rtw_p2p.c`, `rtw_xmit.c`).

**MCC is firmware-offload in this generation, and the generic path already reaches the 8814A:**

- H2Cs: `include/hal_com_h2c.h:37-45` — `H2C_MCC_LOCATION=0x10`, `H2C_RQT_TSF=0x15`,
  `H2C_MCC_MACID_BITMAP=0x16`, `H2C_MCC_CTRL_V2=0x17`, `H2C_MCC_CTRL=0x18`,
  `H2C_MCC_TIME_SETTING=0x19`, `H2C_MCC_IQK_PARAM=0x1A`. `CONFIG_MCC_MODE_V2` selects the
  new-ICs path (requires IQK in firmware: warning at `hal/hal_mcc.c:1277`); the V1 path uses
  `H2C_MCC_CTRL` (`hal/hal_mcc.c:2152`).
- `hal/rtl8814a/` does not have a single line of MCC (zero references in `rtl8814a_cmd.c` nor
  `rtl8814a_hal_init.c`; `HW_VAR_MCC` does not exist anywhere in the tree). But the transport is
  generic: `hal/hal_mcc.c` sends H2Cs via `rtw_hal_fill_h2c_cmd`
  (`hal_mcc.c:765,1703,1777,1855,1899,2044,2152,2953`), which on 8814A is `FillH2CCmd_8814`
  (`rtl8814a_hal_init.c:4501`); the response C2H falls from the 8814A's own handler to the generic
  one (`c2h_handler_8814a` returns `_FAIL` for unknown ids, `rtl8814a_cmd.c:725-750`, and
  `c2h_handler` dispatches `C2H_MCC=0x17` → `rtw_hal_mcc_c2h_handler`,
  `hal/hal_intf.c:1241-1244`); and the reserved page is downloaded via
  `_rtw_hal_set_fw_rsvd_page` (`hal/hal_com.c:10823`, MCC hook at `10992-11002`), which
  `rtl8814_download_rsvd_page` already invokes (`rtl8814a_cmd.c:410`).

**The real dependency is the firmware.** The header comment of the common H2Cs
(`include/hal_com_h2c.h:21`) lists "88e, 8723b, 8812, 8821, 92e use the same FW code base" — the
8814A is not on that list and carries its own images (`array_mp_8814a_fw_ap/nic`,
`hal/rtl8814a/hal8814a_fw.c:29,5319`). The inspection of the blob (`AWDL_FW_8814A_MCC.md`) closes the
question: the v33.6 firmware the driver downloads sends the H2Cs 0x10 and 0x15–0x1A to its error
path and emits no C2H 0x17, so the firmware MCC path does not exist with this image.

**Difference with concurrent mode.** `CONFIG_CONCURRENT_MODE` + `core/rtw_mi.c`
(`rtw_mi_status` at `rtw_mi.c:274`, channel unions at `include/rtw_mi.h:120-122`) is pure
channel hopping in driver software; MCC is firmware-scheduled alternation with TSF
synchrony, offloaded null data (`rtw_hal_mcc_issue_null_data`, `hal/hal_mcc.c:3568`), MACID
map and IQK in firmware. For the AWDL case, plan A is the manual path (§7); the MCC path remains
conditioned on verifying the firmware.

## 3. Software path: null data with PM, switch and return

### 3.1 The null data with PM

`_issue_nulldata` (`core/rtw_mlme_ext.c:9521`) builds a Null Data frame: subtype
`WIFI_DATA_NULL` (`set_frame_sub_type`, :9591), `SetToDs` for STA (:9566), the PM bit is bit
12 of the FC (`SetPwrMgt`, :9572-9573; `_PWRMGT_` = `BIT(12)`, `include/wifi.h:269`), addr3 = BSSID
of the AP (`get_my_bssid(&pmlmeinfo->network)`, :9583-9585), unencrypted, at 20 MHz, through the
management queue (`update_mgntframe_attrib`, :9552; `qsel = QSLT_MGNT`, :7632). With confirmation it uses
`dump_mgntframe_and_wait_ack` (:9598-9599), without it `dump_mgntframe` (:9601).

`issue_nulldata` (`core/rtw_mlme_ext.c:9616`) resolves `da == NULL` to the AP MAC from
`pmlmeinfo->network` (:9628-9629) and retries `try_cnt` times; `wait_ms == 0` is
fire-and-forget without waiting for confirmation.

The multi-interface wrapper is `rtw_ps_annc` (`core/rtw_mlme_ext.c:14955`): for each associated STA
it emits `issue_nulldata(iface, NULL, ps, 3, 500)` (:14980) — three attempts with up to
500 ms of wait, fit for scan, too much for a slot.

### 3.2 Firmware LPS versus software announcement

They are two different and complementary things. The firmware LPS (`rtl8814_set_FwPwrMode_cmd`,
`hal/rtl8814a/rtl8814a_cmd.c:261`, H2C `H2C_SET_PWR_MODE` at :332; mode 0 active, 1 LPS, 2 WMMPS;
`smart_ps` 0/1 = PS-Poll, 2 = NullData, :314-319) keeps the radio on the home channel: the firmware
generates its nulls/PS-Polls and wakes with the beacons/DTIM. The driver's PM=1 null is the point
announcement of "leaving the channel" that the AP must register before the radio disappears; the
PM=0 on return is the reverse notice. `LPS_Leave` is at `core/rtw_pwrctrl.c:1246` and by default
LPS is off (`CONFIG_POWER_SAVING` not defined; `os_dep/linux/os_intfs.c:91`).

### 3.3 Channel and width switch

- `set_channel_bwmode` (`core/rtw_wlan_util.c:596`): computes the center channel and
  `chnl_offset80` (:606-615), takes `dvobj->setch_mutex` (:616), pauses TX only on DFS CAC
  (compiled out: `CONFIG_DFS_MASTER` not defined), updates `dvobj->oper_channel/bw/offset`
  via `rtw_set_oper_ch/bw/choffset` (:641-643), contemplates IQK backup under MCC/TDLS (:645-655)
  and calls `rtw_hal_set_chnl_bw` (:657).
- `rtw_set_chbw_cmd` (`core/rtw_cmd.c:2153`) wraps the above as a command of the command thread; the
  local extension `RTW_CMDF_CHBW_IDLE_ONLY` (:2176, `include/rtw_cmd.h:49`) reaches
  `rtw_set_chbw_hdl` (`core/rtw_mlme_ext.c:16539`), which is a no-op if the fwstate has
  `_FW_LINKED|_FW_UNDER_LINKING|_FW_UNDER_SURVEY` (:16551-16553) and otherwise writes
  `pmlmeext->cur_channel/offset/bwmode` and calls `set_channel_bwmode` (:16558-16563). It is the
  path of the current parking when there is no association.
- `rtw_hal_set_chnl_bw` (`hal/hal_intf.c:1001`) computes `cch_20/40/80` (:1005-1029) and dispatches to the
  8814A handler `PHY_SetSwChnlBWMode8814` (`hal/rtl8814a/rtl8814a_phycfg.c:2340`) →
  `PHY_HandleSwChnlAndSetBW8814A` (:2222; exits early if channel, width, offset and center do not
  change, :2249-2269) → `phy_SwChnlAndSetBwMode8814A` (:2078) → `phy_SwChnl8814A` (:1883) and
  `phy_SetBwMode8814A` (:1660), followed by the TX power reload (:2108-2112) and IQK
  only if `pHalData->bNeedIQK` (:2113-2117). There is no `H2C_SET_CHANNEL` nor media-status-rpt in
  this chain (the offload `H2C_CHNL_SWITCH_OFFLOAD` requires `RegFWOffload == 2`, :1922-1923).

### 3.4 The return

`set_channel_bwmode` never sends a null; the PM=0 is emitted by the caller. Precedents:
`rtw_back_opch` (`core/rtw_mlme_ext.c:15021`: wakes MACID + `rtw_ps_annc(adapter, 0)`), the
scan back-op (:15387), the scan end (:15529) and the keep-alive (:13051, :13089). Among the
callers of `set_channel_bwmode` are the scan restoration (`survey_done_set_ch_bw`,
`core/rtw_mlme_ext.c:14885`), the back-op (:15384), the join (`rtw_join_done_chk_ch`, :16328), the
keep-alive (:13040, :13059), sreset (`core/rtw_sreset.c:172`), AP startup, DFS, P2P, TDLS and
CSA.

## 4. Cost of a switch on 8814A

**Blocking delays in the path: zero (verified).** There are no `mdelay`/`udelay`/`msleep` in
`hal/rtl8814a/rtl8814a_phycfg.c` (including `phy_SwChnl8814A`, `phy_SetBwMode8814A` and the
direct RF write :87-210), nor in `rtl8814a_rf6052.c`, `rtl8814a_dm.c` nor `hal_intf.c`. The
8814A RF write is a direct BB write to the `rf3wire` (:155), without wait loops.
The whole cost is, then, USB control traffic.

**Write count per switch 153/80 → 149/20 (4 RF paths, `NumTotalRFPath`, :1937):**

| Work | Writes | Where |
|---|---|---|
| Channel change | 1 BB (`rFc_area`) + 4×(1 read + 1 write `RF_CHNLBW`) + 1 BB (AGC 0x958) ≈ 10 ops | `phy_SwChnl8814A` :1922-1964 (RF :1953-1955, AGC :1959-1963) |
| Width change | TRXPTCL 0x8a0 (1R+1W), DATA_SC, ADC bw, AGC bw, 40/80 MHz cases, 4 RF paths (4R+4W), spur NBI ≈ 21-22 ops | `phy_SetBwMode8814A` :1660-1717; `PHY_RF6052SetBandwidth8814A` (`rtl8814a_rf6052.c:43-62`); `phy_SpurCalibration_8814A` :1464 |
| TX power reload | 62 rates × 4 paths ≈ 252 BB writes to 0x1998 (double for MGN_1M) | :2108-2112 → `PHY_SetTxPowerLevel8814` (:636) → `phy_set_tx_power_level_by_path` (`hal/hal_com_phycfg.c:3044-3081`), `PHY_SetTxPowerIndex_8814A` (`rtl8814a_phycfg.c:750-758`) |

Total ≈ 280-300 USB operations per switch; ~90 % is the power reload.

**USB transport.** `rtw_write32` → `usbctrl_vendorreq`
(`os_dep/linux/usb_ops_linux.c:29-215`): synchronous `usb_control_msg` over EP0, serialized by
`usb_vendor_req_mutex` (:70), limited retries and a 500 ms timeout only on failure. The real
per-operation latency is not in the code: 10-100 µs per vendor request is a common estimate.

**Total estimate per switch: ≈ 3-30 ms, midpoint 10-15 ms** (estimate; the proven part is the count of operations and the absence of delays). IQK does not run per
switch: `bNeedIQK` is only set by `HW_VAR_DO_IQK` (`hal/hal_com.c:12158-12162`) from TDLS,
connect/scan, CAC, AP startup or sreset; the periodic IQK retrigger of the watchdog is
disabled (`#if 0`, `hal/phydm/halrf/halphyrf_ce.c:1159`) and `halrf_watchdog` does no IQK nor
LCK (`hal/phydm/halrf/halrf.c:1330-1341`). If a full IQK were forced it would be ~100 ms typical and
~300 ms worst case (LOK delays 1 ms and WBIQK 10 ms per shot, `hal/phydm/halrf/rtl8814a/halrf_iqk_8814a.c:219-405`,
`hal/phydm/halrf/halrf_iqk.h:30-31`) — another reason not to trigger it per slot.

**Against the AWDL budget** (AW = 16 TU = 16.384 ms; slot of 4 AWs = 65.5 ms):

- One switch ≈ 2/3 of an AW. Out + back ≈ 20-30 ms of the 65.5 ms of the slot: it fits, with
  ~35-45 ms on the air. Switching more than once per slot overflows the budget.
- phydm learns the new channel via the `ODM_CMNINFO_CHNL/BW` pointers
  (`hal/hal_dm.c:443-444`) and acts on the next watchdog tick (DIG, CCA, CFO), without
  blocking the switch.
- Clear optimization lever: the power reload (≈ 252 writes) is 90 % of the cost;
  omitting it or shortening it when the destination channel is already loaded would reduce the switch to ~2-4 ms.

### 4.1 Variant within the 80 MHz block: 149/80 instead of 149/20

If AWDL uses 149 at 80 MHz instead of 20 MHz, the way out and back do not change the RF channel:

- `set_channel_bwmode` computes the center with `rtw_get_center_ch` (`core/rtw_wlan_util.c:549-581`):
  149, 153, 157 and 161 at 80 MHz always give 155 (:564-565). The `chnl_offset80` comes from comparing
  center and primary (:608-615): LOWER for both 149 and 153. Only the 40 MHz offset changes, which is
  LOWER for 149 and UPPER for 153.
- `rtw_hal_set_chnl_bw` receives the center (`core/rtw_wlan_util.c:657`, "set center channel"), and
  `PHY_HandleSwChnlAndSetBW8814A` only marks `bSwChnl` if `current_channel` changes
  (`hal/rtl8814a/rtl8814a_phycfg.c:2249-2255`). With the center at 155 there is no write of
  `RF_CHNLBW` nor per-channel AGC; `bSetChnlBW` remains for the change of `nCur40MhzPrimeSC`
  (:2258-2266), i.e. `phy_SetBwMode8814A` (≈ 22 operations).
- The power reload (:2110-2112) is called with `pHalData->current_channel`, which stays at 155:
  it writes the same values already there. Omitting it when `current_channel` did not change is
  equivalent, and leaves the switch at the ≈ 22 width operations (≈ 1-3 ms with the same
  per-operation estimate of §4).
- TX at 20 MHz within 80 MHz: the descriptor chooses the 20 MHz subchannel according to the primary and the
  offsets (`hal/rtl8814a/rtl8814a_xmit.c:473-489`), so frames go out on the primary of the moment. That the
  management path AWDL uses goes through that same assignment is `?`.
- The iPhone announces `{44,128}` in its 5 GHz slots (`AWDL_AF_VS_IPHONE.md` §3): class 128 is
  80 MHz, so 149 at 80 MHz matches what an Apple peer does on 149.

With the router on a channel of another block (for example 36 at 80 MHz), the switch becomes again the
full one of §4.

## 5. The closest existing mechanism: the back-op of the linked scan

It is, as-is, the cycle null → leave → return → null that AWDL needs, with the only difference of
the home dwell being measured by the scan and not by a slot schedule.

**Definitions.** `enum ss_backop_flag`: `SS_BACKOP_EN` (BIT0, "backop when linked"),
`SS_BACKOP_EN_NL` (BIT1), `SS_BACKOP_PS_ANNC` (BIT4), `SS_BACKOP_TX_RESUME` (BIT5) at
`include/rtw_mlme_ext.h:186-192`. State in `struct ss_res` (`pmlmeext->sitesurvey_res`,
`include/rtw_mlme_ext.h:194-239`): `backop_ms` (:221), `scan_cnt`, `scan_cnt_max`,
`backop_time`. State machine `enum SCAN_STATE` (`include/rtw_mlme_ext.h:160-182`).

**Flow** (`sitesurvey_cmd_hdl`, `core/rtw_mlme_ext.c:15156`; the `survey_timer_hdl` timer
:13160-13188 re-enqueues the command):

1. `SCAN_START` (:15223-15254): drains the hardware queues with `HW_VAR_CHECK_TXBUF`
   (`hal/rtl8814a/rtl8814a_hal_init.c:3739`: sets `REG_RETRY_LIMIT` SRL=LRL=1 and polls the
   FIFO pages every 10 ms **up to 2000 ms**), sleeps the used MACIDs
   (`rtw_hal_macid_sleep_all_used`, :15240) and announces PS with `rtw_ps_annc(padapter, 1)` (:15236),
   waiting 50 ms ("delay 50ms to protect nulldata(1)").
2. `SCAN_PROCESS`: `site_survey` (:14815) switches with `set_channel_bwmode` (:14822) to 20 MHz per
   channel, launches probereqs and sleeps `scan_ch_ms`; the back-op decision lives in
   `rtw_scan_backop_decision` (:14470): back-op only if there are linked interfaces and the policy
   carries `SS_BACKOP_EN`; after `scan_cnt >= scan_cnt_max` → `SCAN_BACKING_OP` (:14747-14757).
3. `SCAN_BACKING_OP` (:15355-15405): returns to the home channel with `set_channel_bwmode` (:15384),
   restores MSR, wakes MACID and, with `SS_BACKOP_PS_ANNC`, emits `rtw_ps_annc(padapter, 0)`
   (:15387-15389); with `SS_BACKOP_TX_RESUME` reschedules TX (`rtw_mi_os_xmit_schedule`, :15401).
   In `SCAN_BACK_OP` (:15400-15406) it waits `backop_ms` (or `scan_abort`) and returns to
   `SCAN_LEAVING_OP`.
4. `SCAN_COMPLETE` (:15489-15553): `survey_done_set_ch_bw` (:14885), phydm restoration,
   `rtw_ps_annc(padapter, 0)` (:15529), `rtw_rx_ampdu_apply`.

**Default values.** `backop_ms = RTW_BACK_OP_CH_MS` = 400 ms and
`scan_cnt_max = RTW_SCAN_NUM_OF_CH` = 3 (`include/rtw_mlme_ext.h:300-307`; initialization at
`core/rtw_mlme_ext.c:1081-1082`; editable via debugfs, `core/rtw_debug.c:1451-1489`). Dwell per
channel `SURVEY_TO` = 100 ms.

**Important nuance.** The policy of a pure linked STA is
`/*SS_BACKOP_EN|*/SS_BACKOP_PS_ANNC | SS_BACKOP_TX_RESUME` (`core/rtw_mlme_ext.c:1073`): the
`SS_BACKOP_EN` is commented out, so an associated STA does no periodic back-op — it does one
complete off-channel pass with a PM=1 null at the beginning and PM=0 at the end. The periodic
BACKING_OP↔BACK_OP cycle is only active when there is an AP/mesh up (policy with `SS_BACKOP_EN`,
:1075). Reusing the mechanism for AWDL means, in essence, reactivating that cycle with
`backop_ms` as the home dwell.

**TX/RX during the off-channel time.** TX is blocked in software:
`rtw_is_xmit_blocked`/`rtw_xmit_ac_blocked` (`core/rtw_xmit.c:5713-5764`) cut when
`rfctl->offch_state != OFFCHS_NONE` (:5740) or the scan state is not in
{`SCAN_DISABLE`, `SCAN_BACK_OP`} (:5747-5755); the xmit tasklet consults it in
`select_and_dequeue_pending_xmitbuf` (:5604). RX: `hw_var_set_mlme_sitesurvey`
(`hal/hal_com.c:11353`) closes `REG_RXFLTMAP2` (data frames are dropped), adjusts RCR
(`rtw_hal_rcr_set_chk_bssid`) and disables the TSF update, restoring at the end; the
IGI drops during scan (`sitesurvey_set_igi`, :15038) and AMPDUs are restricted (`rtw_rx_ampdu_apply`,
:15234).

**Entries and guards.** `rtw_sitesurvey_cmd` (`core/rtw_cmd.c:855-913`) requires
`pmlmepriv->lock` (:851-853), takes LPS down with `rtw_lps_ctrl_wk_cmd(LPS_CTRL_SCAN)` (:865) and arms the
watchdog `mlme_set_scan_to_timer` with `rtw_scan_timeout_decision` (:899-901). Off-operation
channel transitions are serialized by `rfctl->offch_mutex`
(`sitesurvey_set_offch_state`, :15145-15171).

## 6. Risks

1. **Low — the AP does not expel over a 65 ms absence.** `linked_status_chk`
   (`core/rtw_mlme_ext.c:12907`) runs every 2 s on the command worker
   (`core/rtw_cmd.c:3257`; `core/rtw_mlme.c:3302`). `chk_ap_is_alive` (:12728-12751) deems
   silence only if the deltas of data frames, beacons and proberesps addressed to the station
   are all zero in the 2 s window (beacons are counted in `core/rtw_recv.c:2020`); with
   `rx_chk_limit = 4` (`core/rtw_sta_mgt.c:315-319`) the `receive_disconnect`
   (:13093-13102, :11560-11582) arrives after ~5 consecutive silent checks (~10 s), and the
   null polling (`link_count_limit = 7`, ~16 s) even later. 65 ms is 0.6 % of a single
   window. `?`: how the firmware reacts to lost DTIMs if LPS were active.
2. **Medium — `rtw_leave_opch` can block seconds inside its mutex.**
   (`core/rtw_mlme_ext.c:14993-15018`, under `rfctl->offch_mutex`): `HW_VAR_CHECK_TXBUF`
   polls up to 2000 ms and the `rtw_ps_annc` null is 3 attempts × 500 ms → worst case ~3.5 s,
   incompatible with a 65.5 ms slot if run inline. The tick must use a lightweight variant
   (fire-and-forget null, no FIFO drain; see §7).
3. **High — a concurrent scan steals the channel and turns off data RX.**
   `hw_var_set_mlme_sitesurvey` (`hal/hal_com.c:11353`) sets `REG_RXFLTMAP2 = 0` (all data
   dropped) and sweeps channels with `set_channel_bwmode` (:14822); the vendor core protects
   nothing by itself. In the kext the existing gates mitigate it, but any new route
   (airportd scanning, wifip2pd) must respect `offch_mutex` and the slot state.
4. **Medium — the active keep-alive can bounce the radio mid-slot.** The
   :13020-13060 branch of `linked_status_chk` detects `oper_ch != cur_channel`, saves the backed-up channel,
   switches to the home channel, emits probereq + nulls (with waits) and restores the backed-up channel
   (:13036-13059). If a check falls inside an AWDL slot, interleaved with the tick,
   it breaks the timing. It must be kept out of the way while the slot is active.
5. **Low — LPS off by default; if enabled, `rtw_leave_opch` does not exit LPS.** With
   `CONFIG_POWER_SAVING` undefined, `power_mgnt = PS_MODE_ACTIVE` and
   `bLeisurePs = _FALSE` (`os_dep/linux/os_intfs.c:91`; `core/rtw_pwrctrl.c:2204-2205`). The scan
   path does exit LPS (`LPS_CTRL_SCAN`, `core/rtw_cmd.c:865` → `core/rtw_pwrctrl.c:3333`
   of the handler); `rtw_leave_opch` has no equivalent call. `?` whether the firmware in LPS
   would answer on the wrong channel during the switch.
6. **Low — sreset and the `cur_*`.** A TX hang with `REG_TXDMA_STATUS != 0` or not
   completing in >4 s triggers `sreset_reset` (`hal/rtl8814a/rtl8814a_sreset.c:30-67`;
   `rtl8814a_hal_init.c:4487`), and the restoration uses `pmlmeext->cur_*`
   (`core/rtw_sreset.c:172`). That is why the tick must not go through `rtw_set_chbw_hdl`
   (:16551 no-op when linked; :16558-16560 would overwrite `cur_*` with 149/20 and corrupt the
   restoration): switch with `set_channel_bwmode` directly.
7. **Medium — triple channel state that can diverge.** `pHalData->current_*`
   (`rtl8814a_phycfg.c:2275-2303`), `dvobj->oper_*` (`rtw_wlan_util.c:641-643`) and
   `mlmeextpriv.cur_*` are written by different paths. The tick must pass the exact triples —
   exit (149, DONT_CARE, 20 MHz), return (153, UPPER, 80 MHz) — and `set_channel_bwmode`
   keeps `oper_*` consistent by itself. The early-return of
   `PHY_HandleSwChnlAndSetBW8814A` (:2249-2269) makes the operation idempotent if nothing changes.
8. **Design note — the per-switch power reload.** ≈ 252 USB writes per switch
   (§4). Omitting it per slot is a bet on the precision of the per-channel regulatory limits (149 and
   153 share band but not table entries), not a structural necessity:
   it must be measured (§8).
9. **RX on 149: the hook is right; the risk is in the filters and in the scan.** The local
   `validate_recv_frame` hook (`core/rtw_recv.c:2310-2319`) consumes, before validation, every
   non-CTRL frame with A3 = AWDL BSSID and delivers it to the radio ABI; without the hook nothing
   changes. With port 1 active, `RCR_CBSSID_DATA` is kept at zero
   (`hal/hal_com.c:3829`) and `RCR_CBSSID_BCN` active while the STA is linked (:3844-3850):
   foreign beacons are filtered by the hardware and AWDL action frames pass. Infrastructure
   frames arriving during the slot are lost on the air — that is the expected cost of time
   sharing. The USB RX backlog is not affected: `usb_read_port`/`usb_recv_tasklet`
   (`os_dep/linux/usb_ops_linux.c:748-1060`) have no flush per channel change.

## 7. Proposal

### 7.1 What to reuse

A couple of new functions in the vendor tree replicating the back-op skeleton without going through the
scan command:

- **`awdl_slot_leave(adapter)`** (go to the slot):
  1. Mark off-channel: set `rfctl->offch_state` to a tick-specific value (or reuse
     `OFFCHS_BACKING_OP`) under `rfctl->offch_mutex` — `rtw_is_xmit_blocked`
     (`core/rtw_xmit.c:5740`) will block software TX alone.
  2. Announce PS with a lightweight null: `issue_nulldata(iface, NULL, 1, 1, 0)` — fire-and-forget,
     the pattern of :13087. No `rtw_ps_annc` (3 × 500 ms) nor `HW_VAR_CHECK_TXBUF`
     (up to 2000 ms): frames in flight toward the AP finish on their own during the switch.
  3. Switch: `set_channel_bwmode(adapter, 149, HAL_PRIME_CHNL_OFFSET_LOWER, CHANNEL_WIDTH_80)`
     when the home channel is in the 149-161 block at 80 MHz (§4.1); otherwise,
     `set_channel_bwmode(adapter, 149, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20)`. It takes
     `setch_mutex`, updates `oper_*`, does not touch `cur_*` nor `offch_mutex`.
  4. Serve the slot (TX mgmt/data and AWDL RX already implemented by the radio ABI).
- **`awdl_slot_return(adapter)`** (back home):
  1. `set_channel_bwmode(adapter, 153, HAL_PRIME_CHNL_OFFSET_UPPER, CHANNEL_WIDTH_80)`.
  2. `issue_nulldata(iface, NULL, 0, 1, 0)` (PM=0) — precedent :13089 and `rtw_back_opch`
     (:15021).
  3. Release `offch_state` to `OFFCHS_NONE` under `offch_mutex` and reschedule TX
     (`rtw_mi_os_xmit_schedule`, precedent :15401).

Both must run on the command thread or on its own workitem (not in the tick context if the tick must
keep measuring schedules), and respect `offch_mutex` against scan/ROC and
`setch_mutex` against any other switch (current only users: TDLS chsw,
compiled out).

### 7.2 Hook from the tick

New radio ABI call (`RTL_RADIO_FN_AWDL_*` family), e.g.
`RTL_RADIO_FN_AWDL_SLOT(ch, offset, bw, enter)` or the `LEAVE`/`RETURN` pair, invoked by the
existing AWDL tick at the edges of the served slots of the 129 when there is an infrastructure
association. The current parking (`RTW_CMDF_CHBW_IDLE_ONLY`) remains for the no-association case;
with association, the tick switches 153/80 ↔ 149/20 per slot instead of parking. A
boot-arg (e.g. `rtlawdl_mcc=0`) to disable time sharing and stay in single-channel mode. The
keep-alive of `linked_status_chk` (:13020-13060) must skip the restoration branch while the tick has the
channel out (guard on `offch_state`, which that branch could already consult).

### 7.3 Cost optimization

With the estimate of §4, the power reload is 90 % of the switch. Options, in order of
intrusion: (a) measure first without touching anything; (b) reload only the rates active in the slot (the
AWDL ones go to OFDM 6/12); (c) cache per channel: omit the reload if the destination channel was loaded
less than N slots ago. (b) is local to `PHY_SetTxPowerLevel8814`/`phy_set_tx_power_level_by_path`
(`hal/hal_com_phycfg.c:3044-3081`).

### 7.4 Firmware MCC path, conditional

Compiling `CONFIG_MCC_MODE + CONFIG_CONCURRENT_MODE` and adding `hal/hal_mcc.c` to `build.sh` is the
short path to TSF alternation with null data downloaded to the firmware — but only if the
8814A firmware answers the H2C MCCs. It does not answer (§2, `AWDL_FW_8814A_MCC.md` §7): the
hardware test is no longer needed and the manual path of §7.1-7.2 is the only one.

## 8. What to measure on hardware

1. **Real switch latency.** Timestamps around `set_channel_bwmode` on the way out and
   back (out 153/80→149/20 and back), with and without the power reload: close the
   10-15 ms estimate of §4 and the weight of the ~252 writes.
2. **Health of the infrastructure link during repeated slots.** Ping at 10 ms and traffic toward
   the AP for minutes with 4-AW slots served periodically: RTT, jitter, % loss,
   and stable association for hours (risk 1).
3. **Keep-alive counters.** Data/beacon/proberesp deltas that `chk_ap_is_alive` sees
   with and without AWDL active, to confirm no 2 s window is left totally silent.
4. **That IQK is not triggered** after repeated 153↔149 switches (`bNeedIQK` stays 0)
   and that the measured TX power on each channel does not degrade; if the omission of the reload is tested,
   compare power and EVM with and without it on 149 and 153.
5. **AWDL RX with the link active.** AF/PSF/MIF counters of the slot with a reference iPhone
   while `en2` is associated, compared with the unassociated session (af 0 with no peers in the
   test of 2026-10-30): confirm that time sharing does not cost reception within the
   slot.
6. **TX queues.** Frames held by the software blocking of `offch_state` and their total latency
   (backlog on return), and verify that `HW_VAR_CHECK_TXBUF` is not invoked per slot (risk 2).

Network on primary 157 at 80 MHz (block 149–161, same block as social 149, so only the primary moves and
no power reload), AirLayer `3699d28e` with capability 29, no peers.
| Driver | Out to 149 | Back to the BSS | Mean / maximum (1024 PSF) | Failures | Ping to gateway |
|---|---|---|---|---|---|
| `d5d828f7` (fixed 2 ms wait after the null) | 4357 and 4355 µs | 2389 and 2415 µs | 3381 / 5407 µs (1464 switches) | 0 | 20/20, 0.62 ms |
| `69dfea33` (`96e80cb`, wait bounded to URB and MGQ) | 2522 and 2646 µs | 2367 and 2361 µs | 2459 / 4668 µs (1445 switches) | 0 | 20/20, 0.63 ms |
With `d5d828f7`, after 2 h 14 min: 26,257 switches without failure, maximum 8928 µs, mean 3394 µs, 30/30
pings. The 10–15 ms estimate of §4 is far above what was measured in the same-block case;
the block-change case (power reload) remains unmeasured `?`.

Ping to the gateway every 100 ms and simultaneous `networkQuality`, with the network on 157/80:
| Test | AWDL state | Download / upload capacity | Ping | > 5 ms | Maximum |
|---|---|---|---|---|---|
| Load, AWDL at rest | 31 (149 in 2 of 16 slots) | 762 / 604 Mbps | 900/900 | 39 | 6.7 ms |
| Load, AirDrop window just opened | 45 → 3 → 31 | 772 / 605 Mbps | 600/600 | 39 | 7.1 ms |
| 10 min without load, AirDrop reopened every 2 min | 24/45/3/31 | — | 6000/6000 | 4 | 6.6 ms |
The 5–7 ms spikes concentrate in the first ~14 s after opening AirDrop (state 45, the 149 in 10
of 16 slots) and when they coincide with load. There were no losses or measurable capacity drop. In the
10 minutes: 2886 more switches without failure (mean 2462 µs, maximum 5594 µs).
Data over `awdl0`: the 6 frames dropped by age are the ones macOS sends via mDNS at
boot, while the sequence only has the 6 or is at rest. With the network on 5 GHz the layer only
serves the social slots of that band (149), not the ones of the 6, and a frame waiting more than 1.2 s
is dropped. In the 10 minutes with five AirDrop openings the counter did not rise (6 → 6).

Correction (2026-11-01): `networkQuality` ran without `-I` and goes out through the first default route,
which is `en1` (cable), not `en2`. The capacity column does not measure the Wi-Fi link: 762/604 Mbps
matches `networkQuality -I en1` of 01-11 (769/604 Mbps). For the ping the interface was not recorded;
the correlation of the spikes with state 45 points to `en2`, unconfirmed `?`.
Measurement forced through `en2` (`networkQuality -I en2`, `ping -b en2`), network on 161/80, −43/−79 dBm,
650 Mbps MCS 7, `awdl0` up and no AirDrop open:
| Test | Download / upload capacity | Latency at rest | Ping to gateway |
|---|---|---|---|
| `en2` | 310 / 145 Mbps | 90 ms | 0 % loss, base 1–2 ms, bursts of 12–70 ms every 5–6 s |
| `en2`, `awdl0` down (`ifconfig awdl0 down`) | 321 / 208 Mbps | 48 ms | base 1–3 ms, no bursts: 0 of 300 over 20 ms in 90 s |
| `en1` (reference) | 769 / 604 Mbps | 48 ms | — |
With `awdl0` down the periodic bursts disappear and upload goes from 145 to 208 Mbps; download
does not change (310 → 321 Mbps, with rate at 527–650 Mbps, MCS 6–7). The 12–70 ms bursts come
from time sharing with AWDL, even with no AirDrop open: with `rtlawdl_mcc=1`
`mccChannel` serves the first slot of each 149 run and `txTick` leaves the radio there the whole
slot (4 AWs × 16 TU = 64 TU, 65.5 ms); with the 149 in 2 of 16 slots that is about 131 ms out of the
BSS per period of 1.05 s (12.5 %), and 12 % of the pings every 250 ms went over 20 ms. The bursts
every 5–6 s are the beat between the ping interval and the 16-slot period.

With `0caa86e` (`rtlawdl_mccaw`, 1 AW by default: the radio returns to the BSS after the first AW of the
slot if no data, UMI nor MIF is pending), AirLayer md5 `c3489552`, same link (161/80,
−44/−79 dBm, 650 Mbps MCS 7), `awdl0` up and no AirDrop open:
| Test | Before (whole slot) | With 1 AW |
|---|---|---|
| Ping to gateway every 250 ms | 12 % over 20 ms, maximum 70 ms | 0 of 480 over 20 ms, 15 of 480 over 10 ms, maximum 15.7 ms |
| `networkQuality -I en2` download | 310 Mbps | 304–333 Mbps (4 runs) |
| `networkQuality -I en2` upload | 145 Mbps | 151–233 Mbps (4 runs) |
Upload varies between runs as much as the difference measured before with `awdl0` down (145 versus
208 Mbps), so that difference is not significant with a single run per case. In the
first ~10 s of measurement after boot there were two cuts of 2.7 and 3.5 s (queued pings going
out together), like the one seen when bringing `awdl0` down; none in the following 3 min.

Origin of the cuts: they are scans, not AWDL. A forced scan with CoreWLAN
(`scanForNetworks`, 7.5 s) produces two cuts of 3.4 s separated by a single normal ping. When associated,
the driver does not return to the BSS during the scan: `init_mlme_ext_priv` leaves `SS_BACKOP_EN`
commented out in the STA flags (`mlmeext_assign_scan_backop_flags_sta`, `rtw_mlme_ext.c`), so it walks the
31 channels back to back at `SURVEY_TO` (100 ms) each, about 3.1 s out plus the switches. Also
`setWCL_SCAN_REQ` (`RTLAirLayer.cpp`) ignores the channel list of the request and each request
scans everything; two consecutive requests give the two cuts. With `SS_BACKOP_EN` the vendor values
(`RTW_SCAN_NUM_OF_CH` 3, `RTW_BACK_OP_CH_MS` 400) leave at most ~300 ms out and
stretch each scan to about 7.5 s, below the 10 s cap of the linked scan of `RTLAirController::poll`.

With `dba50e6` (`SS_BACKOP_EN` in STA), driver md5 `5f5cf71b`, AirLayer `c3489552`, measured over
`en2` (161/80, −44/−76 dBm):
| Case | Before | With back-op |
|---|---|---|
| One scan (`scanForNetworks`) | 1 cut of 3.4 s (or 2 with two requests) | 9.5 s cycle: cuts of 250–494 ms every ~0.9 s, 1–2 ms between them |
| Startup, airportd requests scans of 2–3 channels in a row | cuts of 3.4 s | ~75 s cycle (8 scans of 9.5 s, 4 ms between one and the next) |
| No scan (90 s) | — | 0 of 360 pings over 20 ms, maximum 18 ms |
The maximum cut (~0.5 s) exceeds the ~300 ms of 3 × `SURVEY_TO`: the queue drain
(`HW_VAR_CHECK_TXBUF`), the 50 ms protection of the PM=1 null and the switches add up. No cut
goes over 0.5 s, but the total out time per scan rises from 3.4 to ~5 s. The requests of
airportd carry 2, 3 or 21 channels (ring `RTLAirLayerLog`, "WCL scan request, N channels") and each
one walks the 31; respecting that list would leave a 3-channel request at ~0.3 s out.
A second request right after the previous one (21 channels) ended by the 2.75 s cap of the linked
scan with no visible cut in the ping, which suggests the driver never got to scan it `?`.

With `ef52dda` (driver md5 `d91214e5`, AirLayer `5d175719`) each request scans only its own 2.4 and
5 GHz channels. The band marks of the entries (+8) are confirmed in the ring:
`0x8a` on 2.4 GHz (`APPLE80211_C_FLAG_2GHZ` 0x8), `0x92` on 5 GHz (0x10) and `0x112` on 5 GHz DFS
(0x100 additionally).
| Case | Requests of airportd | Ping over `en2` |
|---|---|---|
| Startup | 8 of 2–3 channels (9/10/36, 40/44/48, 149/153/157, 161/165/52, 56/60/64, 100/104/108, 112/116/132, 136/140) | one cut of 370–435 ms per request, every ~2.5 s (minimum of the linked scan of `poll`); before, ~75 s cycle |
| `scanForNetworks` | 11 channels of 2.4 GHz and 21 of 5 GHz: the whole plan | 9.3 s cycle with cuts of up to 475 ms, 0 % loss; same as before, because it asks for all the channels |
Each cut of ~0.4 s for 3 channels is the 3 × `SURVEY_TO` plus the queue drain, the 50 ms of the
PM=1 null and the switches. There was a single cut of 3.5 s on
`en2` shortly after bringing `awdl0` down (14 queued pings that went out together), which did not repeat in
the following 60 s; its cause was not isolated `?`. `ifconfig en2` and `ifconfig awdl0` return
`SIOCGIFXMEDIA: Input/output error`.

### 8.1 Adaptive dwell (2026-11-05)

With 1 AW per slot and a peer present, IO80211 removes the peer even when the channel switch arrives
on time. `IO80211AWDLPeer::monitorPeer` (`0xffffff80021a9294`) calls `shouldRemovePeerFromNDP`
(`0xffffff80021a9c50`). For an ordinary cached peer, removal happens when more than 6000 ms have
passed since its last action frame, or when there are no overlapping slots for more than 2000 ms.
Other peer types use other limits. `setPeerNotReachable` then leads to
`cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`), which posts `Peer Absence`.

Measured with the virtual peer and layer `1320BA53`: 60 of 67 absences come after a gap of 5 s or more
with no frames from the node, almost all of them 6-7 s, and gaps of 7-8 s recur every ~14.5 s, the same
as the absences. Which of the two conditions fires still has to be confirmed, since it was not read
directly; the correlation points to the 6 s one `?`.

Going back to the whole slot at all times would return the cost measured above to any machine with AWDL
active, even with no peer nearby. So the dwell now depends on peers: 1 AW with no peers, and the whole
slot for 30 s after the last action frame from another device. `rtlawdl_mccaw=1..4` still fixes the
value. The periodic `tx psf` line reports the value in use (`dwell`).

Layer md5 `7165bacc`, UUID `9CA670C1`, link on 161/80, `awdlsim` node with no over-the-air link, ping to
the gateway every 200 ms:
| Measure | No node (`dwell 1`) | With node (`dwell 4`) |
|---|---|---|
| Pings over 20 ms | 5 of 300 | 34 of 300 |
| Pings over 50 ms | 0 of 300 | 16 of 300 |
| Average and maximum latency | 2.8 and 26 ms | 7.6 and 75 ms |
| Loss | 0 of 300 | 0 of 300 |

With the node present, 12.9 % of its frames get in over 240 s at rest (`inj ok` +738, `offch` +5006),
with no second without frames. It was 1.6 % before. There was 1 `Peer Absence` in 8 min 43 s, against one
every ~14.5 s. It came at 13:23:27, as two consecutive scans of 3.25 and 3.5 s ended. A single scan of
2.75 s caused none. The link to the scans is inferred from the timing and the 6 s limit `?`. Another run
of 300 pings with the node, which included those scans, reached 47 of 300 over 20 ms and a maximum of
616 ms.

The 5 pings over 20 ms with no node contrast with the 0 of 480 of `c3489552` (ping every 250 ms, no early
switch). The cause of that difference has not been isolated `?`.

### 8.2 Slots during the scan back-op (2026-11-05)

The scan that caused the absence of §8.1, rebuilt from the log:
| Time | Event |
|---|---|
| 13:23:20.613 | `configd` publishes the IPv6 state of `en2` again |
| 13:23:20.618 | `airportd` asks for a full scan (`handleIPStatusNotification`, "Full band scan request on successful association and IP acq.") |
| 13:23:20.622 | The 11-channel request starts |
| 13:23:23.879 | The 21-channel request starts, 5 ms after the previous one ended |
| 13:23:26.999 | `Peer Absence`, 6.38 s after the start |
| 13:23:27.381 | The scan ends |

The node received no PSF and none of its frames got in between seconds 402 and 407 of its log. With the
radio associated, `txTick` served no slot for as long as a scan lasted. The full scan appeared at
13:14:38, 13:23:20 and 13:31:34; three samples do not establish a period. Why `configd` publishes again
was not investigated `?`.

An associated scan already returns to the BSS for 400 ms every 3 channels (`SCAN_BACK_OP`,
`RTW_BACK_OP_CH_MS`). The change uses those returns:
- `rtl_radio_status.scanning` is `RTL_RADIO_SCAN_BACKOP` (2) during the return; it is still non-zero, so
  readers that treat it as a boolean do not change.
- `rtlRadioAwdlOffch` accepts going to another channel in that state, and still returns `kIOReturnBusy`
  during the rest of the scan.
- `SCAN_BACK_OP` does not move to `SCAN_LEAVING_OP` while `offch_state` is `OFFCHS_AWDL`.
  `sitesurvey_close_awdl_backop` closes the window under `offch_mutex`, the same mutex the channel switch
  holds, so a slot cannot start between the check and the state change. If the radio is still away after
  another 500 ms, the scan itself brings it back.
- `txTick` serves slots in the return only if it heard a peer in the last 30 s. With no peers, scans do not
  change. Nothing changes either if the BSS is on a social channel.

Driver md5 `e8559aca`, UUID `A4BD0F2B`; layer md5 `ade1eb88`, UUID `C9F97715`; link on 161/80, `awdlsim`
node with no over-the-air link. Six scans forced with `scanForNetworks`, from 6.0 to 9.3 s, 30 s apart.
Five of them with 75 pings to the gateway every 200 ms:
| Scan | Duration | Node frames that get in | Consecutive seconds with no frames | Pings over 200 ms | Maximum | `Peer Absence` |
|---|---|---|---|---|---|---|
| 1 | 9.3 s | 9.3 % | 1 | 16 of 75 | 544 ms | 0 |
| 2 | 9.3 s | 7.8 % | 1 | 15 of 75 | 453 ms | 0 |
| 3 | 6.0 s | 14.2 % | 0 | 6 of 75 | 421 ms | 0 |
| 4 | 9.3 s | 9.2 % | 0 | 15 of 75 | 474 ms | 0 |
| 5 | 8.8 s | 8.8 % | 1 | 10 of 75 | 437 ms | 0 |

The sixth scan ran as an AirDrop send started: `/Discover` and `/Ask` worked during the scan, and the
1 MiB file arrived identical. The scan ended before `/Upload`, so the upload was not tested with a scan
in progress.

No absence in about 3 min with six scans, each as long as the 6 s limit or longer. Before this change only
the natural scan of §8.1 was measured, 6.76 s long, which did cause an absence; these forced scans were not
run on the previous code. The ping maximums are in the same range as the 475 ms measured in §8 with
`ef52dda` on a 9.3 s cycle, although that measurement had no peer.

The `tx psf` line 6 min after the node started gives `mcc 1432 fail 0`, maximum 4860 µs and average 2455 µs: no channel switch was refused, not even as a return closed. There was no `Peer Absence` in those 6 min.

Not tested: real over-the-air reception during the return. The node delivers its frames through the
simulation port, which only checks the channel; the real receiver is also affected by the settings of
`rtw_phydm_func_for_offchannel`, active for the whole scan `?`.

### 8.3 Data windows (2026-11-05)

AWDL data had a fixed cap of 4 frames per AW, whatever their size: a TCP ACK used as much as a full frame.
Each AW now has a budget of 12 ms of air time, of its 16.4 ms, at 12 Mb/s; a frame costs 160 µs plus its
length, and the queue goes from 32 to 64 frames. The driver's limit is still half the management pool,
which stops long runs of short frames.

The virtual node has no radio and never returns the 802.11 ACK, so every unicast frame used all its 6 tries.
That does not happen with a peer that responds, and it distorted the cost on the BSS. With the simulation
port active, data frames now go out with a single try (`data_tries`). Without the simulation they keep the 6.

Measurements with 1 MiB over TCP across the virtual link and 100 pings to the router every 200 ms in each run.
The budget rows use the layer md5 `2d7229d7`, UUID `8FF43136`; the 1-try row uses the driver md5 `f2750e21`,
UUID `2D84BB9B`. The provider's router moved from 161 to 149 by itself at 13:57:43, with the same BSSID, and the
cap row on 149 was measured at 13:59.

| Layer, BSS channel, tries, runs | Upload (Mac → node) | Download (node → Mac) | Ping during upload: average, maximum | Pings > 20 ms / > 200 ms during upload |
|---|---|---|---|---|
| Cap of 4 frames, 161/80, 6, 1 | 27 KB/s | 59 KB/s | not measured | not measured |
| Cap of 4 frames, 149/80, 6, 3 | 44-46 KB/s | 409-485 KB/s | 31-38 ms, 257 ms | 31-33 / 3-4 |
| Budget of 12 ms, 149/80, 6, 3 | 110-115 KB/s | 1523-2011 KB/s | 124-183 ms, 991 ms | 41-43 / 27-31 |
| Budget of 12 ms, 149/80, 1, 3 | 110-115 KB/s | 1526-1957 KB/s | 5-6 ms, 73 ms | 7-8 / 0 |

In the 149 rows, 0 `Peer Absence`; in both budget rows, a 1 MiB AirDrop file arrived identical.

**Speed.** The node's captures measure the effect directly: with the cap 4 frames go out per AW and 12 per
window; with the budget 10 per AW and 30 per window, with 1468-byte frames. On 149, with the same channel, the
upload grows 2.5 times, in the same proportion. Retries do not change the speed.

Short ACKs now reach about 50 per AW, and the download on 149 grows about 4 times. Both rows share a channel, so
the cap also limited the far end.

**Number of windows.** The sequence the Mac advertises, in its PSFs, depends on the BSS channel:

| BSS | Advertised sequence | Slots with data in the captures | Calculated frames/s with the cap |
|---|---|---|---|
| 161 | `0 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0` | 2 and 10 | 23 |
| 149 | `149 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0` | 0, 2 and 10 | 34 |

With the BSS on 149 the infrastructure slot is also 149 and works as a third window: 3 of every 16 slots, 2, 6
and 8 slots apart. The calculation gives 34 frames/s with the cap and 86 with the budget, which matches what was
measured on 149. With the BSS on 161 there are 2 windows and, in proportion, the upload with the budget should be
near 75 KB/s; that last figure was not measured.

The sequence also changes with IO80211's state. When the BLE beacon arrives, `setScheduleState` records
`BonjourServiceAdded`, and both budget runs show the same in the PSFs (BSS on 149):

| State | Duration | Slots with 149 |
|---|---|---|
| Discovery | 3 s | 0-3 and 9-11 |
| Idle | 6 s | 0-2 and 9-10 |
| Low Power | the rest, also during sends | 0, 2 and 10 |

**Repeated message, solved.** The log showed once per second `mismatch of channel sequence with Firmware, force
to program Low Power`. `sanityCheckAWStart` (`0xffffff80021e21de`) counts in `state+0xFB48` every AW start event
whose channel or flags do not match what is expected, and `monitorAWDLState` (`0xffffff80021ee79c`) prints the
message if that counter is not 0. The cause was in the layer: event 41 put the channel flags at `+0x18`, which is
the version of `apple80211_channel`, and left `+0x20`, the flags, at 0. So bits `0x2018` never matched. The event
now carries the version and flags of the sequence entry
([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2).

With the fix and the BSS on 149, the message goes from 60 per minute to none in 6 minutes with traffic.
`monitorAWDLState` stayed active, with 346 state lines in that time. Upload does not change (113-115 KB/s), neither
does the ping during upload (maximum 85-87 ms, 8-10 of 100 above 20 ms), there are no peer absences and the 1 MiB
AirDrop arrives identical. The state still reaches Low Power with the same sequence: the message was not what kept
it there, because `setScheduleState` runs the same with the counter at 0.

The effect of the error went beyond the message. `handleUmiTimer` (`0xffffff80021c3254`) compares the channel and
flags of the current window with those of each peer's sequence and, if they differ, skips that peer's UMI. With
the flags at 0, the code never gets to send a UMI in a window with a channel. The virtual node does not show the
difference: the capture has 0 unicast action frames from the Mac both before and after the fix, also during an
AirDrop. Those 0 frames had other causes; with a traffic registration and the node declaring 6E, the UMI goes out
(`AWDL_PAR_VIRTUAL.md` §9.6 and §9.7). With a real peer it is not tested.

**Cost on the BSS.** Between the two budget rows only the tries change: the maximum ping falls from 991 to 73 ms
and pings over 200 ms disappear. A frame with no ACK and 6 tries takes about 11 ms and one with an ACK about
1.2 ms (calculated, not measured). At 80 frames/s that is 88 % of the air against 10 %, and with the cap it was
35 %, close to the 31-33 % of pings over 20 ms in that row.

Not tested:

- The cost on the BSS with time sharing, and the budget with the BSS off the social channels. The router's channel
  cannot be set; on 149 there are no channel switches.
- A real peer that misses frames: it uses up to the 6 tries. The 6-try row is the worst case of a peer that never
  sends an ACK yet still answers over TCP, which a real peer would not do.
- Rates above 12 Mb/s: the virtual node cannot judge them.
- `mccChannel` in mode 1 serves only the first slot of each run of 149. If AirDrop with a real iPhone asks for
  long runs, data would be limited by that. It was not changed blindly: a sequence of only 149 served in full
  would lose the BSS.
- What sequence AirDrop produces with a real peer. `handleAirDropTrafficReg` sends `sendRealTimeUMI` and does not
  use High Thruput mode: `initiateHighThruputMode` is only called from `handleMigrationTrafficReg`.
