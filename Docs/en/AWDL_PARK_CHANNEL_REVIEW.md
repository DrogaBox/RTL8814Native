# Review of the AWDL channel parking (rtlRadioSetChannel)

Independent review of the source code of parking the radio on a social channel without
association: commits `07a41e8` (driver side) and `ab11c69` (AirLayer layer side). All claims were
verified against the current tree (`42c04d3`) and against the Base code in
`Source/rtl8814au`. Gaps that cannot be verified are marked `?`.

Scope note: `ab11c69` was already amended within the same branch by `2bb1c45` (clock read under
`_txLock`) and `404af9b` (the parking target becomes the social channel that the 129
of the manager repeats in the most slots, with the preference of `rtlawdl_ch` before a 129, on
ties and when the 129 names none). The driver side of `07a41e8` did not change (only `42c04d3`
added AWDL data to `rtlRadioTx80211`). The review covers both commits as base and the current
state of `parkRadio` (section 2).

## 1. Code walkthrough

Driver (`07a41e8`):

- `rtl8814native_radio_set_channel` (`Source/Driver/AWDLTx.c:111-127`): rejects channels outside
  the plan (`rtw_chset_search_ch`, `AWDLTx.c:117`), rejects linked/joining/under_survey
  (`AWDLTx.c:119`), sets `pmlmeext->tx_rate` to OFDM 6 Mb/s (`update_mgnt_tx_rate`,
  `AWDLTx.c:122`) and enqueues `rtw_set_chbw_cmd(ch, CHANNEL_WIDTH_20, DONT_CARE, flags 0)`
  (`AWDLTx.c:123-124`). Without RTW_CMDF_DIRECTLY: it only enqueues; the heavy work happens on
  the command thread.
- `callPlatformFunction` (`Source/Driver/RTL8814Native.cpp:511-519`): `p1` is `const uint32_t *`
  with the channel; maps `-EINVAL/EBUSY/ENOMEM` to `kIOReturnBadArgument/Busy/NoResources`.
- `rtl_radio_status.idle_channel` = `pmlmeext->cur_channel` only in the IDLE state
  (`Source/Driver/VendorProbe.c:1202-1203`; the initial `memset` leaves it at 0 in the other
  states).
- ABI: `RTL_RADIO_FN_SET_CHANNEL` (`Source/Driver/RTLRadioABI.h:86`), `idle_channel`
  (`RTLRadioABI.h:154-159`). The header is the same for both kexts
  (`build_airlayer.sh:26` includes `Source/Driver`): no size change (`reserved[1]` →
  `idle_channel`), compatible in both directions.

Layer (`ab11c69` + amendments, current tree):

- `initWithController` reads `rtlawdl_ch` (144-147 of `Source/AirLayer/AWDLInterface.cpp`); only
  6/44/149 are valid, anything else `_parkCh = 0` (no parking).
- `txTick` (`AWDLInterface.cpp:910-924`): if `!st.scanning`, it serves `st.channel` when LINKED or
  calls `parkRadio(st.idle_channel, ...)` when IDLE; in JOINING/SCAN it does nothing.
- `parkRadio` (`AWDLInterface.cpp:1026-1062`, version of `404af9b`): chooses the target — the
  social channel that the 129 repeated in the most slots, tie broken in favor of `_parkCh` —
  under `_txLock`, and requests the change at 1 Hz at most (`_parkReqUs`, 1048-1050);
  `kIOReturnUnsupported` disables the parking (1054-1056); it only serves when `idle_channel` is
  already the target (1046-1047).
- `awdlSocial` (`AWDLInterface.cpp:915-918`) filters 6/44/149; `setRadioTx` restarts `_parkReqUs`
  on the on transition (`AWDLInterface.cpp:870`).

## 2. Races between `_SetChannel` and scan/join/disconnect/PS (question 1)

**The state check lives only in the enqueuer.** `check_fwstate(_FW_LINKED |
_FW_UNDER_LINKING | _FW_UNDER_SURVEY)` (`AWDLTx.c:119`) is evaluated at enqueue time;
`rtw_set_chbw_hdl` (`Source/rtl8814au/core/rtw_mlme_ext.c:16539-16556`) writes
`cur_channel/cur_bwmode/cur_ch_offset` (`:16553-16555`) and calls `set_channel_bwmode`
(`:16556`) **without re-examining the MLME state**. Between the check and the execution on the
command thread a join or a scan can start. Consequences per case:

- **Scan started after the check** (`_FW_UNDER_SURVEY` is set when enqueueing the
  survey, `Source/rtl8814au/core/rtw_cmd.c:892`): if the survey order was enqueued earlier, the
  parking hdl executes in the middle of the sweep (the command thread is free between channel
  hops, which triggers the `survey_timer`). `site_survey` only changes the operating channel
  (`Source/rtl8814au/core/rtw_mlme_ext.c:14827-14828`), not `cur_channel`, and at the end
  `survey_done_set_ch_bw` returns to the value the parking left (149): harmless, one lost dwell
  at most.
- **Join started after the check** (`_FW_UNDER_LINKING` is set when enqueueing,
  `Source/rtl8814au/core/rtw_ioctl_set.c:96`): the join order rewrites `cur_channel` with the
  DSConfig of the BSS (`rtw_bss_get_chbw`, `rtw_mlme_ext.c:14194`) and switches the radio
  (`:14253`); if the parking hdl runs afterward, in the middle of the auth/assoc exchange, it
  moves the radio to 149 while the join waits for answers on the BSS channel: the join fails and
  retries (and the parking returns to 1 Hz). Narrow window (IDLE read of `txTick` against a
  concurrent connect plus command thread delay) but real. Finding H-A (§6) with the fix.
- **Disconnection**: does not touch the channel. `rtw_mlmeext_disconnect` (`rtw_mlme_ext.c:12372`)
  only drops BSSID/macid, re-evaluates RCR (`:12406`) and sets MSR of port 0 to STATION
  (`:12409`);
  `disconnect_hdl` (`:14267`) does not change the channel. `cur_channel` stays on the BSS channel
  — exactly the case the 1 Hz re-park of `parkRadio` covers.
- **`survey_done_set_ch_bw` returns to the parked channel**: verified. Without a
  linked/linking union (`rtw_mi_get_ch_setting_union` ignores interfaces without
  `_FW_LINKED|_FW_UNDER_LINKING`, `Source/rtl8814au/core/rtw_mi.c:133-135`) and without P2P in
  LISTEN (the `CONFIG_P2P` branch of `survey_done_set_ch_bw`, `rtw_mlme_ext.c:14907-14930`;
  `wdinfo` is in `P2P_STATE_NONE` on this system, nobody uses the vendor P2P), it falls back to
  `pmlmeext->cur_channel` (`rtw_mlme_ext.c:14934-14938`) and restores with `set_channel_bwmode`
  (`:14945`).

**Other writers of `cur_channel`** (complete inventory of assignments):

| Site | Reached during AWDL rest? |
|---|---|
| `rtw_mlme_ext.c:1052` (init, `registrypriv.channel` = 1, `VendorProbe.c:275`) | only at boot |
| `rtw_mlme_ext.c:11314` (`start_create_ibss`) | only when linking |
| `rtw_mlme_ext.c:14194` (join) | only when linking |
| `rtw_mlme_ext.c:16259` (`rtw_join_done_chk_ch`) | only foreign AP/mesh interfaces; `CONFIG_CONCURRENT_MODE` undefined (`include/autoconf.h:113`), a single iface ⇒ the loop skips |
| `rtw_ap.c:4452/4700/4737` | AP not started in this deployment (`CONFIG_AP_MODE` is set, `autoconf.h:127`, but nobody calls start_ap) |
| `rtw_mlme_ext.c:16553` (`rtw_set_chbw_hdl`) | it is the parking itself |
| `rtw_p2p.c` (direct set_channel_bwmode) | vendor P2P ioctls with no users here |

**Writers of `tx_rate` for management**: only `update_tx_basic_rate` (`rtw_wlan_util.c:3091/3093`)
and `update_wireless_mode` (`:3397/3399`), both with callers on link paths only
(`start_create_ibss`/`start_clnt_join` at `rtw_mlme_ext.c:11318/11381`, P2P at
`rtw_p2p.c:5311/5370`). At rest nobody touches it: the OFDM 6 Mb/s of the parking persists until
the next join (even if the BSS is 11B and leaves it at CCK 1M, the later re-park restores it).

**PS/IPS/LPS**: `CONFIG_POWER_SAVING` is not defined, so `CONFIG_IPS` and `CONFIG_LPS` are not
compiled (`include/autoconf.h:92-108`); additionally the driver sets `power_mgnt = PS_MODE_ACTIVE`
and `ips_mode = IPS_NONE` (`Source/Driver/VendorProbe.c:288-289`). There is no internal suspend
path that would dismiss the radio or rewrite the channel. `CONFIG_AUTOSUSPEND`/BT coexistence
neither (`autoconf.h:293` under `#ifdef`).

**dynamic_chk** (`rtw_dynamic_check_timer_handlder`, `Source/rtl8814au/core/rtw_mlme.c:3427` →
`rtw_dynamic_chk_wk_cmd` → `linked_status_chk`, `rtw_mlme_ext.c:12907`): beacon watching
and roaming only act with a link; the PHYDM watchdog (DIG/CCA) does not change channel.
`rtw_sreset.c:172` restores `cur_channel` as-is (only after an sreset; harmless). No
`rtw_hal_set_hwreg` writes the channel: the only RF path is `set_channel_bwmode`.

**Serialization**: `set_channel_bwmode` takes `setch_mutex` (`rtw_wlan_util.c:626`) around
`rtw_set_oper_ch` + `rtw_hal_set_chnl_bw` (`:644-658`), so two concurrent changes (survey
timer vs command thread) never leave the RF half-done; the `cur_channel` write of the hdl
(`rtw_mlme_ext.c:16553`) is outside the lock, but as it runs on the command thread and only
affects later readers, the risk stays within H-A.

## 3. Sufficiency of `set_channel_bwmode` on 149/44/6 from rest (question 2)

Full chain verified: `rtw_set_chbw_hdl` → `set_channel_bwmode` (`rtw_wlan_util.c:596`) →
`rtw_hal_set_chnl_bw` (`hal/hal_intf.c:1001`) → `PHY_SetSwChnlBWMode8814`
(`hal/rtl8814a/rtl8814a_phycfg.c:2340`) → `PHY_HandleSwChnlAndSetBW8814A` (`:2222`; rejection of
an illegal channel at `:2251`) → `phy_SwChnlAndSetBwMode8814A` (`:2078`). That body executes:

- `phy_SwChnl8814A` (`:2102`, body at `:1883`): band change via `phy_SwBand8814A`
  (`:1897`, 2.4↔5 GHz), table `rFc_area_Jaguar` and RF synthesis on all paths
  (`:1934-1950`), AGC table selection per band (`:1952` and following).
- `phy_SetBwMode8814A` (`:2106`): BB to 20 MHz.
- **TX power per channel**: `rtw_hal_set_tx_power_level` (`:2112`) →
  `PHY_SetTxPowerLevel8814` (`:636`) → `phy_set_tx_power_level_by_path` per path (`:668`), plus
  `odm_clear_txpowertracking_state` (`:2111`).
- IQK: only if `bNeedIQK` (`:2113-2117`), same as in any scan/join channel hop; nothing is missing
  relative to the normal driver behavior.

**Channel plan**: the registry requests `CONFIG_RTW_CHPLAN` = 0xFF
(`include/drv_conf.h:247`; `VendorProbe.c:326`), which is invalid per
`rtw_is_channel_plan_valid` (`core/rtw_chplan.h:134`) and ignored in
`hal_com_config_channel_plan` (`hal/hal_com.c:439-445`); it falls to the default plan
`RTW_CHPLAN_REALTEK_DEFINE` (`hal/rtl8814a/rtl8814a_hal_init.c:1241`,
`hal/hal_com.c:450-451`) = 2G world + 5G FCC1 (`core/rtw_chplan.c:385-386`): 1-13,
36-64, 100-140 (DFS), **149-165 active** (`rtw_chplan.c:189`). 6, 44 and 149 are present and none
is DFS or passive; `rtw_chset_search_ch` (`rtw_mlme_ext.c:828-842`) accepts them.

**DFS/CSA**: `CONFIG_DFS_MASTER` is not defined, so there is no CAC or radar-detect as master;
`rtw_rfctl_is_tx_blocked_by_ch_waiting` (`rtw_mlme_ext.c:414-417`) is only true with
detected radar or a pending `csa_ch`, states not reachable at rest (the CSA of the AP
needs a link, `rtw_mlme_ext.c:3190-3227`). The raw emitter already consults that function
(`AWDLTx.c:36-37`); the parking does not need it.

**RCR/MSR at rest**: with port 1 on, `rtl8814native_awdl_port1` keeps
`RCR_CBSSID_DATA` out on **every** re-evaluation — scan enter/done, connect/disconnect
(`hal/hal_com.c:3829` and calls at `:11378/:11408`, `core/rtw_mlme_ext.c:12398`,
`core/rtw_sreset.c:184`)— so AWDL data frames with a foreign BSSID keep arriving;
`RCR_CBSSID_BCN` stays set at rest
(`hal_com.c:3844-3850`) and only filters beacons, not action frames. MSR: port 0 in STATION after
disconnect (`rtw_mlme_ext.c:12409`), port 1 in ADHOC by `rtlRadioAwdlPort`
(`Source/Driver/AWDLTx.c:92-94`); independent bits.

Conclusion: `rtw_set_chbw_cmd(ch, 20 MHz, 0)` suffices; no RF, BB, per-channel power, or
additional check is missing for RX/TX on 149/44/6.

## 4. Management TX without association (question 3)

It works; it is exactly the path of the probe requests of an unassociated scan:

- `alloc_mgtxmitframe` takes from the management queue, with no link dependency;
  `update_mgntframe_attrib` (`core/rtw_mlme_ext.c:7616`) sets
  `mac_id = RTW_DEFAULT_MGMT_MACID` (= 1, `include/drv_types.h:806`),
  `qsel = QSLT_MGNT`, raid of mode G and **`rate = pmlmeext->tx_rate`** (`:7663-7665`) — the OFDM
  6 Mb/s the parking set. The descriptor dumps it directly:
  `SET_TX_DESC_TX_RATE_8814A` (`hal/rtl8814a/usb/rtl8814au_xmit.c:232`).
- Macid 1 is the default management seat, configured at init; it does not depend on
  stainfo. `update_mgntframe_attrib_addr` (invoked by `rtw_hal_mgnt_xmit`,
  `hal/hal_intf.c:816-820`) searches for the stainfo of the RA and leaves it NULL without problem
  (the precedent: `_issue_probereq` uses the same `alloc_mgtxmitframe`/`dump_mgntframe` during
  unassociated scans, `core/rtw_mlme_ext.c:8453`).
- Shut-off gates: `RTW_CANNOT_RUN` in `dump_mgntframe` (`core/rtw_mlme_ext.c:7737`) and the
  emitter itself (`AWDLTx.c:34`); `RTW_CANNOT_TX` (`drv_types.h:1800`) is checked on the USB
  write (`hal/rtl8814a/usb/usb_ops_linux.c:212`). Definitions at `drv_types.h:1786/1800`.
- RA: the AWDL action frames carry a broadcast A1; the emitter disables retries
  (`AWDLTx.c:54-55`). The data extension of `42c04d3` shares the attributes and the same
  conclusions.

## 5. airportd sweeps without association and the re-parking cadence (question 4)

- **During a sweep** there is no AWDL TX nor parking request: `st.scanning` = 1 makes
  `txTick` not serve the channel and `parkRadio` not be called (`AWDLInterface.cpp:912-919`).
- **At the end**, `survey_done_set_ch_bw` restores `cur_channel` (§2): if it was already parked
  on 149, resuming needs no new request and happens on the next tick, ≤ 1 AW =
  16.384 ms (`AWDLInterface.cpp:737-742`). The clock and the windows close/reopen around
  the sweep (events 43/41), without desynchronization (the clock is own, `2bb1c45`).
- **Re-parking**: only needed when something rewrites `cur_channel` — a join (rewrite at
  `rtw_mlme_ext.c:14194`) or the boot. In those cases, one request per disconnect plus
  retries at 1 Hz (`parkRadio` 1048-1050) until the hdl applies it; never more than 1 p/s.
- **Gap duration**: the sweep uses `SURVEY_TO` = 100 ms per channel
  (`include/rtw_mlme_ext.h:24`); the full plan is 37 channels (§3), ~3.7 s per sweep plus
  hops. The interval at which airportd sweeps without associating is not in this repository: `?`.
  The effect is arithmetic: each sweep silences AWDL for its full duration; between sweeps TX
  works with no extra requests.

## 6. Findings and proposed fixes (not applied)

**H-A (medium-high) — the hdl does not re-examine the MLME state.** `rtw_set_chbw_hdl`
(`rtw_mlme_ext.c:16539`) applies the channel even if between the check of `AWDLTx.c:119` and
execution a join started (the worst case, §2) or a scan (harmless). Exact fix, optional for the
other callers (CSA at `rtw_mlme_ext.c:3227`, `rtw_wlan_util.c:2757`):

1. `include/rtw_cmd.h:898` — extend the parameter with semantics:
   ```c
   struct set_ch_parm {
       u8 ch;
       u8 bw;
       u8 ch_offset;
       u8 idle_only; /* RT8814Native: skip if LINKED/UNDER_LINKING/UNDER_SURVEY */
   };
   ```
2. `core/rtw_cmd.c` (`rtw_set_chbw_cmd`, `:2153`): reuse the `flags` parameter with a
   new constant (e.g. `#define RTW_CHBW_IDLE_ONLY 0x80`, distinct from `RTW_CMDF_DIRECTLY/
   WAIT_ACK`) and do `set_ch_parm->idle_only = !!(flags & RTW_CHBW_IDLE_ONLY);`
3. `core/rtw_mlme_ext.c:16539` (`rtw_set_chbw_hdl`): before writing:
   ```c
   if (set_ch_parm->idle_only &&
       check_fwstate(&padapter->mlmepriv, _FW_LINKED | _FW_UNDER_LINKING | _FW_UNDER_SURVEY))
       return H2C_SUCCESS; /* state changed in flight: do not touch */
   ```
4. `Source/Driver/AWDLTx.c:123`: pass `RTW_CHBW_IDLE_ONLY`.

The layer already re-syncs by itself (`radioStatus` + retry at 1 Hz), so the skip needs no
notification to the caller.

**H-B (medium) — `kIOReturnBadArgument` is retried at 1 Hz forever and silently.** If the
channel is not in the chset (`AWDLTx.c:117`), `parkRadio` only treats
`kIOReturnUnsupported` as terminal (`AWDLInterface.cpp:1052-1056`); a `BadArgument` (e.g. an
empty chset due to an invalid plan in future variants) would repeat every second without a log.
Exact fix in `parkRadio`: treat `kIOReturnBadArgument` as terminal, same as Unsupported:

```c
} else if (r == kIOReturnUnsupported || r == kIOReturnBadArgument) {
    LOG("awdl: radio cannot park on a channel (0x%x), AWDL only on an associated social channel", r);
    _parkCh = 0;
}
```

(With the current REALTEK_DEFINE plan, 6/44/149 exist and the case does not occur today; the fix
makes it immune to future configurations.)

**H-C (low, observation without fix).** `_parkReqUs` is written in `parkRadio` outside `_txLock`
(`AWDLInterface.cpp:1050`) and `setRadioTx` restarts it inside (`:870`). Today there is no
reachable race: `setRadioTx(false)` does `thread_call_cancel_wait` (`:884`) and the restart only
happens on `on && !was`, when no tick is in flight. If more threads calling
`setRadioTx`/`parkRadio` are added, move the restart outside the lock or the access of `parkRadio`
inside.

**H-D (low, accepted behavior).** With `404af9b`, if the 129 changes between ticks, the target
can alternate between two social channels (e.g. 44↔149); the 1 s throttle limits the cost to one
channel change per second and `servedMask` is read from the same `_seq` under the same lock, so
there is no internal inconsistency. The peers of the abandoned channel stop hearing AWDL until
returning: consistent with the fixed radio design.

**H-E (information).** The first parking since boot starts from `registrypriv.channel` = 1
(`VendorProbe.c:275`): it implies a 2.4→5 GHz band change resolved inside
`phy_SwChnl8814A` (§3); nothing else to do.

## 7. Verification of the commit texts

- `07a41e8` — «while not associated the radio parks … through the command thread» (enqueues, the
  hdl runs on the command thread: true); «with management frames at OFDM 6 Mb/s» (`AWDLTx.c:122`,
  `rtw_mlme_ext.c:7546`); «later scans return to it» (`survey_done_set_ch_bw`, the P2P
  LISTEN exception is inoperative here, §2); «idle_channel reports where an idle radio sits»
  (`VendorProbe.c:1202-1203`). «kIOReturnBusy while linked, joining or scanning» (ABI, `:119` and
  the mapping in `RTL8814Native.cpp:511-519`): true. Correct.
- `ab11c69` — «parks it on a social channel (149, or 6/44 with rtlawdl_ch)»: true in its
  version; `404af9b` extended the target to the manager 129 (§1). «serves that channel once the
  radio sits there» (`idleCh == target`, `:1046-1047`): true. «repeating the request at most
  once a second after a disconnect»: true (§2, §5). Correct with the later amendment.

## 8. Summary

- The parking is sufficient for RX and TX AWDL on 6/44/149 from rest: RF, BB, band, per-channel
  power and RCR/MSR are covered by the standard vendor path (§3).
- Management TX (and the data one of `42c04d3`) works without association through the same path
  as the probe requests (§4).
- Sweeps pause AWDL TX for their full duration and require no re-parking; re-parking only
  appears after a join/disconnect, at 1 Hz at most (§5).
- Two actionable findings: H-A (state guard in the hdl, §6) and H-B (terminal BadArgument,
  §6). Both with a proposed fix and not applied. IPS/LPS do not exist in this build and the
  dynamic check does not touch the channel (§2).

## 9. Status after the review

The line numbers of this document refer to the tree at the moment of the review
(`404af9b` plus the changes of `42c04d3`); later commits shift them.

| # | Status |
|---|---|
| H-A | Applied in `8726bd4`: `rtw_set_chbw_cmd` accepts `RTW_CMDF_CHBW_IDLE_ONLY`, which stores `idle_only` in `set_ch_parm`, and `rtw_set_chbw_hdl` does not touch the channel if the radio is already associated, joining or scanning |
| H-B | Applied in `9dc71a6`: `parkRadio` stops requesting the parking also on `kIOReturnBadArgument` and logs the channel and the code |
| H-C, H-D, H-E | No change |
