# RTL8814Native — Porting Roadmap

Master plan for porting `morrownr/8814au` (RTL8814AU, ~450k lines C) to
macOS as `RTL8814Native.kext`. Each phase has a hard acceptance test —
do not advance until it passes on real hardware.

Porting order principle: **bring the chip up before touching the data
path, and never compile `os_dep/linux/*.c`** (that directory is replaced
by this kext + the Compat layer).

---

## Phase 0 — Skeleton (DONE, this repo)

- [x] Kext structure, build script, USB personalities (full RTL8814AU table)
- [x] Compat layer core: locks, atomics, wait queues, timers, workqueue,
      kthreads, skb, net_device, URB, printk
- [x] IOKit side: device claim, endpoint discovery, ethernet interface,
      UserClient ABI

**Acceptance:** `./build.sh` produces a loadable kext; with the adapter
plugged in, `log stream` shows:

```
RTL8814Native: USB device 2357:0106 attached
RTL8814Native: endpoints OK (bulk in 0x81, bulk out 0x01, ...)
RTL8814Native: ethernet interface published (en8814)
```

and `ifconfig` shows a new interface with a placeholder MAC
(`02:11:22:33:44:55`).

---

## Phase 1 — Radio bring-up (the mountain) — DONE (Verified on Hardware)

> **Architecture Reference:** See [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md) for full technical architecture, root causes solved, and diagnostic commands (`./Tools/test_uc`).

Goal: firmware runs on the chip, registers respond, EFUSE factory MAC is published on `en2`, and background threads are alive.

- [x] **1. osdep variant & 0 undefined vendor symbols:** Implemented `Source/Compat/Compat.c` and `Source/Compat/VendorOsDep.c` bridging 74 Realtek HAL/Core/PhyDM C files to XNU.
- [x] **2. dvobj + adapter init + Multi-Pipe USB Discovery:** Implemented `rtl8814native_vendor_probe()` in [VendorProbe.c](../Source/Driver/VendorProbe.c) (`g_dvobj->chip_type = RTL8814A`, `g_dvobj->interface_type = RTW_USB`, `rtw_clr_drv_stopped`) and multi-Bulk-OUT pipe discovery (`0x05`, `0x06`, `0x08`) in [RTLUsbDevice.cpp](../Source/Driver/RTLUsbDevice.cpp).
- [x] **3. Register I/O + EFUSE MAC + 68KB Firmware Download:** Implemented synchronous Bulk OUT `usb_write_port` + `ffaddr2pipehdl` in [VendorOsDep.c](../Source/Compat/VendorOsDep.c) for 8814A Beacon RSVD page firmware download (`HalROMDownloadFWRSVDPage8814A` -> `IDDMADownLoadFW_3081`), plus `RTLUserClient` Selector 6 (`kRTLUC_GetRadioInfo`) and [Tools/test_uc.c](../Tools/test_uc.c).
- [x] **4. Live Hardware Verification:** Verified after reboot: `REG_SYS_CFG = 0x044411B5`, `CHIP_8814A`, permanent EFUSE MAC `7C:C2:C6:03:EA:84` on `en2`, `HAL Init = 1 (_SUCCESS)`, and `Firmware Ready = 1 (YES!)`.

---

## Phase 2 — Scan + join (control plane) — DONE (Verified on Hardware)

Goal: the chip scans 2.4/5 GHz channels and associates to Open or WPA2-PSK networks.

- [x] Start `RTW_CMD_THREAD` (`rtw_start_drv_threads`), `rtw_intf_start` (`rtw_hal_inirp_init`), and 2-second `dynamic_chk_timer`.
- [x] Wire `RTLUserClient` Selectors `1..5` (`kRTLUC_GetLinkStatus`, `kRTLUC_TriggerScan`, `kRTLUC_GetScanResult`, `kRTLUC_JoinNetwork`, `kRTLUC_Disconnect`).
- [x] Implement self-contained in-kernel **WPA2-PSK 4-Way EAPOL Supplicant** ([Source/Driver/WPA2Supplicant.c](../Source/Driver/WPA2Supplicant.c)) with PBKDF2-SHA1, PRF-384, HMAC-SHA1 MIC, RFC 3394 AES-128 Key Unwrap, and hardware CAM PTK/GTK installation.
- [x] **Milestone 2.1 (Verified on Hardware):** Fixed recursive `hwlock` spinlock panic in `rtw_set_802_11_bssid_list_scan` by switching `spinlock_t` to `IORecursiveLock *` and adding header-timestamp dependency checking to `build.sh`. Verified `./Tools/test_uc scan` runs with 0 kernel panics.
- [x] **Milestone 2.2 (Deployed to EFI — Ready for Reboot):** Fixed `loadparam()` RF Front-End (`RFE_Type = 64` -> `rfe_type = 1` for 8814AU USB instead of `0` for 8814AE PCIe), `rf_path = RF_TYPE_MAX`, `phydm_ability = 0xFFFFFFFF`, `halrf_ability = 0xFFFFFFFF`, `WIRELESS_11B` (1 Mbps CCK 2.4 GHz Beacons), dual-band `channel_plan = 0xFF`, `RCR_APPFCS` 4-byte FCS stripping in `recvbuf2recvframe`, thread-safe `sk_buff_head` locking, and live RF/RX/Scan telemetry counters in `./Tools/test_uc status`.
- [x] **Milestone 2.2 Live Check:** After reboot, `status` showed `rfe_type=1`, `rf_type=5`, channel plan `0x34` (32 channels), 8/8 RX URBs completing and `rx_frames=3` — then a page-fault panic in the RX path.
- [x] **Milestone 2.3 (Deployed to EFI — Ready for Reboot):** Fixed u16 `pkt_len` underflow (`pkt_len -= FCS_LEN` on runt frames -> 64 KB `memcpy` overrun in `rtw_os_alloc_recvframe`); added descriptor/bounds guards in `recvbuf2recvframe`.
- [x] **Milestone 2.3 Live Check:** No panics. Scan runs (state 4 -> 12 -> 0) but finds 0 networks; the bulk IN RX stalls during the scan and was sparse before that.
- [x] **Milestone 2.4 (Deployed to EFI — Ready for Reboot):** RX/TX drop-reason telemetry, raw RX descriptor capture, live RCR/RXFLTMAP readout, and Selector 7 live register peek/poke/RF read (`./Tools/test_uc peek|poke|rf`).
- [x] **Milestone 2.4 Live Check:** RX "stall" is just the post-scan `RCR` BSSID filter. Real bug: RX transfers split mid-frame. First network seen after a live `poke 0x0290 0x1E`.
- [x] **Milestone 2.5 (Deployed to EFI — Ready for Reboot):** Root cause: hardcoded `usb_speed = SuperSpeed` on a USB 2.0 High-Speed link -> 64-byte RX DMA burst, wrong TX ZLP padding, wrong RF path count. `usb_speed` now comes from the bulk IN `wMaxPacketSize`. Added opt-in RX reassembly and a raw transfer log (`rxlog`).
- [x] **Milestone 4.0 (Deployed to EFI — Ready for Reboot, `MD5 = ad8ae899...`):** USB2 -> USB3 mode switch (`HW_VAR_USB_MODE`, `switch_usb_mode = 1`) at probe, with partial-probe cleanup and a failed `start()` so a new instance picks up the SS device. Verify `usb_speed=3`, `bulk_in_mps=1024`, `ss_finalized=yes`, `peek 0x0290 = 0x0E`, then join + throughput.
- [x] **Milestone 4.0 Live Check 1 (monitor hub):** switch issued, no panic, but SS enumeration through the monitor hub failed persistently and the dongle dropped off the bus. Retest on a direct motherboard USB 3 port; if SS never enumerates, make the switch opt-in so boot doesn't lose Wi-Fi.
- [x] **Milestone 4.0 Live Check 2 (direct motherboard port): SuperSpeed VERIFIED** — 5 Gbit/s, `usb_speed=3`, `bulk_in_mps=1024`, `ss_finalized=yes`, firmware OK.
- [x] **Milestone 4.0 Live Check 3 (SS join + throughput):** `rf_type=RF_3T3R`, `0x0290 = 0x0E`, ~157 Mbit/s down / ~114 Mbit/s up (HS: ~150 / ~83), 0 drops. Upload gained, download did not: RX is capped elsewhere (Phase 4).
- [x] **Milestone 4.1 (Deployed to EFI):** hot-plug use-after-free panic (old instance's `rtl_workqueue_exit` vs new instance's `start`) fixed by serializing `start`/`stop` under a kext-lifetime recursive lock.
- [x] **Milestone 2.5 VERIFIED ON HARDWARE:** `usb_speed=2`, `0x0290 = 0x1E`, scan finds 20 networks on 2.4 + 5 GHz with 0 RX drops.
- [x] **Milestone 2.6 (Deployed to EFI — Ready for Reboot):** first `join` got through 802.11 auth, then panicked building the Association Request (`build_assoc_req_wfd_ie`, NULL `wdinfo.padapter`). `rtw_init_drv_sw()` now mirrors Linux including the P2P/WFD init.
- [x] **Milestone 2.6 VERIFIED ON HARDWARE:** WPA2-PSK join to my AP (5 GHz ch 157, RSSI -46 dBm), 4-way handshake completed and keys installed, no panic.

---

## Phase 3 — Data path (first packets) — DONE (Verified on Hardware)

Goal: DHCP lease and ping over the air on `en2`.

- [x] Port TX: `RTLEthernetAdapter::outputPacket` -> `skb` -> `rtl8814native_vendor_xmit` -> `_rtw_xmit_entry` -> `rtw_xmit` -> `rtl8814au_hal_xmit` -> `usb_write_port`.
- [x] Port RX: `usb_read_port` (8 Bulk IN URBs on `0x84`) -> `usb_read_port_complete` -> `usb_recv_tasklet` -> `recvbuf2recvframe` (`rtl8814_query_rx_desc_status`) -> `rtw_recv_entry` -> `rtw_os_recv_indicate_pkt` -> `rtl_netif_rx` -> `en2`.
- [x] **Milestone 3.0 VERIFIED ON HARDWARE:** automatic DHCP lease (`192.168.100.11`) + IPv6 SLAAC on `en2`; ping 0% loss to gateway, `8.8.8.8` and IPv6; ~150 Mbit/s down / ~83 Mbit/s up (`curl --interface en2`), 0 RX drops, 0 TX errors.
- [x] **Milestone 3.1 (Deployed to EFI — Ready for Reboot):** `netstat -I en2` packet counters were always 0; `RTLEthernetAdapter` now feeds `IONetworkStats` (`configureInterface`).
- [x] **Milestone 3.1 Live Check:** join + DHCP again with no panic; `Opkts` counts and driver `IONetworkStats` are correct, but ifnet `Ipkts` / `Ibytes` stay 0 (RX is attributed to `en2` in `netstat -s -I en2`; lost in the kernel's input-handler path, see ARCHITECTURE.md).
- [x] **Experiment 3.2 (negative):** `kInputOptionQueuePacket` + `flushInputQueue()` did not make ifnet `Ipkts` / `Ibytes` count; reverted to the direct path (`153485ea...`, pending redeploy). Cosmetic; RX polling mode is the last untested difference (Phase 4).

---

## Phase 4 — Stability & performance

- Sleep/wake, suspend/resume of the USB device, unplug/replug.
- Watchdog for stuck URBs; error recovery paths (`rtl8814a_sreset.c`).
- RX throughput cap (~155 Mbit/s even on SS while wired does ~650):
  - [x] Negotiated link: VHT 2SS MCS8/9, 80 MHz (router is 2x2), so it is not the air side.
  - [x] Aggregate cap (4 parallel streams = same total). No chip RX FIFO overflow. RX agg registers = Linux.
  - [x] Telemetry in `status` (rate/BW/NSS, aggregation, RX path cost, reorder counters). Build `7ca09811`, deployed.
  - [x] Skb recycling tested live: no change (host RX path ~40 µs per transfer, not the cap). Kept ON.
  - [x] Root cause found: `rtw_init_default_value()` lacked the Linux defaults block -> TX capped at 20 MHz (`ra_bw=20`), 8 KB HT A-MPDU advertised. Fixed in `0b9f9e93` (deployed).
  - [x] Verified: `ra_bw=80`, TX A-MPDU negotiated (`tx_ampdu_tids=0x01`). **Download ~215-266 Mbit/s** (was ~155).
  - [ ] **Upload regressed to ~55 Mbit/s** (was ~114): synchronous `usb_write_port()` kept one frame in flight. Async bulk OUT (`c896a978`) verified: upload ~68-77 Mbit/s, down ~225-245, USB err=0. Remaining cap is air-side.
  - [x] Linux reference captured (rtw88: down 288-366, up 227-280); register A/B (RRSR RSC, R2T/TX SIFS, pkt lifetime) has no effect. Boot after Linux gave down 334-364 / up 134-185 with the same build.
  - [x] Cold boot: Linux parity (up 202-297, down 320-368, RX dup ~3%). The slow case depends on warm-restart state (TXAGC/IQK differ).
  - [x] Reproduced on a macOS warm restart (up 43-59, dup 40%). Fix `35d6749b`: power the chip off at probe when it is already on (rtw88-style). **Verified:** restart -> up 183-262 / down 319-364, dup ~1% (Linux parity).
  - [x] `shutdownStall` reports investigated: pre-existing (also before the kext), launchd waiting on tuxera NTFS / user processes; none of our threads involved.
  - [x] Shutdown path: `systemWillShutdown` -> card disable **VERIFIED** (`cc2c41f3`): after a restart through the 4.5 build the probe sees the chip cold (`CR=0xEA`), restart does not hang (02:14 shutdownStall has zero frames in our kext), and throughput held at down 320-321 / up 200-214 Mbit/s, dup 1.06%. First 4.5 boot had been warm (previous shutdown ran 4.4).
  - [ ] Non-driver: `Non-monotonic time` panics on macOS 26.7.1 traced to an OpenCore kernel patch that no longer matches Tahoe (thread offset 0x490); config fixed with 3 exact 25.x patches (see ARCHITECTURE.md Milestone 0.1 update). Verify that no new reports appear. `Tools/check_kernel_patches.py` checks every patch against the installed kernel; run it after each macOS update.
  - [ ] AP changed channel 157->161 mid-session (same BSSID, no CSA handling in driver): spontaneous disconnect + rejoin works; consider CSA follow (Phase 5 / polish).
    - 2026-10-27 analysis: `process_csa_ie` (core/rtw_wlan_util.c) is compiled in (CONFIG_DFS) and `rtw_dfs_ch_switch_hdl` (core/rtw_cmd.c ~4022) handles a STA by sending deauth LEAVING and switching channel ("TODO: true op ch switching"), so the link drops and the layer re-joins. That deauth starts the AP's ~20 s auth-ignore window (see the 4.22 notes); 4.22's 9.6 s of auth retries may not cover it. A real follow needs a channel + bandwidth switch without deauth (VHT wide-bandwidth CSA IE) and an AP that announces CSA to test; not implemented because it cannot be verified on the current AP (ch 153, no DFS).
  - [ ] Real sleep/wake power management (`setPowerState`) — **implemented** (`4953df17`): sleep = card-disable keeping allocations, wake = HAL/firmware re-init + auto-rejoin last network. First boot panicked in `rtl_wq_thread` on freed work memory (kext-lifetime workqueue vs instance memory; async `usb_kill_urb`) — fixed in `37d04555` (synchronous killUrb, flush-before-free, queue drain on stop, stale-instance completion guards); verify with a sleep/wake cycle after reboot.
  - [ ] Sleep/wake on `37d04555` PANICKED on wake (03:34, `rtl_wq_thread` called the system queue head): a re-enumerated dongle starts a new instance before the old one stops, which re-inits live workqueues and global vendor state. Owner guard + no-restart-of-live-queue fix in `9af63e61` (4.6c) **VERIFIED**: wake re-enumerated the dongle, the new instance took over, no panic. The link stayed down (only the old wake path re-joined), so `ca1fe571` (4.6d) re-joins after a takeover; deployed, sleep test pending. The stale instance stayed `inactive, busy` because the USB interface was only closed in stop(); `f306b1b9` (4.6e) closes it in `didTerminate()`. Deployed; verify one instance after wake + unplug/replug. On the 4.6c takeover instance a manual join failed at auth/assoc (`RXFLTMAP2=0`); `g_suspended` reset fixed in `ab9bc7de` (4.6f). The 4.6f wake deadlocked the IOKit termination thread (old `stop()` waited for the lifecycle lock under the USB gate while the new `start()` held the lock and waited for the gate); no `en2`, all re-enumerated USB devices stuck. `4095c263` (4.6g) makes `stop()`/`didTerminate()` only try the lock and defer the release to a helper thread. **VERIFIED** on the 07:26 wake: no panic, one instance, no USB device stuck. The link stayed down (the old instance released first, so no takeover was detected and the 4.6d re-join did not fire); `b88cd137` (4.6h) re-joins after every successful probe. **VERIFIED** on the 07:32 wake: auto re-join, WPA2 keys installed. Two follow-ups in `f8b45cc7` (4.6i): `enable()` no longer clears a carrier reported before configd brings en2 up (en2 stayed inactive/no IP), and candidate selection prefers 5 GHz by 15 dB (it re-joined on 2.4 GHz ch 8). **VERIFIED** on the 07:39 wake: auto re-join on 5 GHz VHT80, en2 active with DHCP, down 273-328 / up 109-270 Mbit/s. Unplug/replug **VERIFIED** (07:41): no panic, one instance (new IOUSBHostDevice), 0 USB devices stuck, auto re-join on 5 GHz VHT80, en2 active with the same DHCP lease.
  - [x] RX tasklet is NOT on `rtl_rx_wq`: `usb_init_recv_priv()`'s `tasklet_init` undoes `rtl_work_bind_rx`. Fixed in `eb245605` (4.7): `hal_usb.c` re-binds after its `tasklet_init`. **VERIFIED**: no regression, no measurable gain (down average about 269, peak 317 Mbit/s; RX backlog_max=3, so RX parsing is not the bottleneck). Next ceiling suspects: rate stuck at VHT 2SS MCS7 (HAL says 3SS), RX reorder drops (1490), USB_AGG_EN=0.
  - [x] Rate ceiling VHT 2SS MCS7: `c719470a` (4.8) adds AP VHT/HT MCS maps (from beacon IEs), negotiated map and FW `ra_mask` to `test_uc status`. **Answered:** the AP is 2x2 (VHT map `FA FF`), RA mask has 2SS MCS0-9, downlink already at MCS8/9, uplink RA picks MCS7 SGI. No driver cap. `networkQuality`: en2 349/305 vs wired 810/630; Linux rtw88 on the same setup 288-366/227-280, so we are at Linux parity.
  - [x] RX aggregation tuning: live A/B of `0x280` (0x0A03/05/07, 0x0505, 0x1405) and aggregation off all give 321-360 Mbit/s down; even 1 frame per transfer (443k transfers) does 334. USB/host RX is not the limit, so the vendor default stays.
  - [x] The uplink 'MCS7' was not stale, just stable: the 8814A FW sends a C2H RA report (id 0x0C) only when the rate changes. Measured live on 4.8 (2026-10-27): during an upload at 332 Mbit/s, one C2H arrived (230 -> 231) and the rate moved from VHT 2SS MCS8 LGI to MCS8 SGI. TX is at the AP's 2SS ceiling. `tx_bw` is 0xFF because the 8814A report is shorter than 7 bytes (no BW byte); test_uc now prints `n/a`. The HW_DEF_RA_INFO_DUMP mailbox query (0x8C = 0x40000100|macid, read 0x2F0) returns 0 on the 8814A FW, so it is not an alternative. 4.9's per-id C2H counters still confirm which ids arrive.
  - [x] About 2% A-MPDU reorder drops on download: old/duplicate subframes after lost BAs (`check_indicate_seq`), same as earlier good boots; not a bug.
  - [x] BSD `Ipkts/Ibytes` = 0 on en2: 4.9 added batched `ifnet_stat_increment_in`; **VERIFIED LIVE** on hardware: `netstat -b -I en2` confirms `Ipkts: 502,533` / `Ibytes: 672 MB` actively counting!
  - [ ] **ACK/BlockAck exchange:** AP retransmits ~36% of frames during upload (~6% during download); throughput scales with MAC A-MPDU size (~1.8 ms fixed per PPDU). Instrument TX reports/retries, compare BA/response settings with Linux, get a Windows reference.
  - [x] RX tasklet on its own `rtl_rx_wq` thread: **VERIFIED** in 4.7 (`eb245605`), backlog_max ≤ 3.
  - [ ] ~7% A-MPDU reorder duplicates on download (AP retransmits): check BlockAck response.
- Optional RX polling mode (`IONetworkPacketPollingParameters`, LucyRTL8125-style); may also fix the ifnet `Ipkts` / `Ibytes` = 0 cosmetic issue.
- Swap PRNG shim for a real entropy source; review skb copy path
  (mbuf fast path if CPU-bound).
- Channel/regulatory: static world-safe channels first (like YYavic's
  default table), `country` boot-arg later.

**Acceptance:** 24h soak with mixed traffic; survive sleep/wake x20.

---

## Phase 5 — Native IO80211 layer (optional, separate repo)

RE findings, radio ABI (4.10/4.11, staged) and the verified 25.6.0 header generator: [PHASE5_IO80211.md](PHASE5_IO80211.md) (class model, the 12 IO80211Controller + ~200 IO80211InfraProtocol pure virtuals, tools in `Tools/re/`).
- [x] RE of the WCL contract (2026-10-27): scan/associate/key struct offsets, the driver→WCL event IDs (201 beacon, 237 scan done, 211/212/213 join, 216 link), interface bring-up via Skywalk packet queues; no class-name gate on third-party controllers.
- [x] Layer kext skeleton RTL8814AirLayer 0.1 (2026-10-27): builds, 0 vtable mismatches, imports resolve; control plane wired to the radio ABI; gated by boot-arg `rtlair=1`. Staged in Backups/airlayer_0.1, not deployed.
- [ ] First boot with the layer (`rtlair=1`): interface appears, scan reaches the Wi-Fi menu.
- [x] Step 3b written: Skywalk data path (TX submission -> radio TX -> TX completion; radio RX hook -> pool packet -> RX completion), active only with `rtlair=2` while linked. Untested on hardware.
- [x] CARD_CAPABILITIES (Apple's unconditional baseline) and SUPPORTED_CHANNELS layouts.
- [x] Review pass, RTL8814AirLayer 0.2 + RTL8814Native 4.11 (2026-10-27): RX deadlock, sleeping RX allocation, scan-while-linked, link-loss reporting and lifecycle fixed. Staged in Backups/airlayer_0.2 and Backups/build_4.11, not deployed.
- [x] Second review, RTL8814AirLayer 0.3 (2026-10-27): `rtlair=1` scan-only, en2's link never dropped by WCL leave/power-off, interface stop as Apple's. Staged in Backups/airlayer_0.3, not deployed.
- [x] Third review, RTL8814AirLayer 0.4 (2026-10-27): interface enable/disable switch the packet queues (as AppleBCMWLANSkywalkInterface does); EFI check and diagnostics scripts for the test boot. Staged in Backups/airlayer_0.4, not deployed.
- [x] RTL8814AirLayer 0.4 deployed with `rtlair=1` (2026-10-27): loads, start() fails, no panic. 0.5 (CoreCapture logger + fault reporter, step log in ioreg `RTLAirLayerLog`) deployed 12:06: panicked in CCLogPipe::start (no IO80211WorkQueue). 0.6 adds createWorkQueue/getWorkQueue (root cause of 0.4 too); deployed 12:17: panicked in IONetworkController::start (IO80211WorkQueue::getThread is NULL). 0.7 adds RTLAirNub (as Broadcom bus nub); deployed 12:26: IO80211Controller::start completes, panic in IO80211SkywalkInterface::start (header class sizes wrong). 0.8 pads the classes to the KC sizes; deployed 12:35: no panic, layer + interface start fully; radio RX/TX stall whenever the layer is started (A/B with inert confirmed). 0.9 adds the rtlairstep bisection boot-arg; staged.
- [x] Layer RX stall root-caused and fixed (2026-10-27): 0.9 `rtlairstep=3` worked; 0.10 call trace showed no radio calls from the layer; RTL8814Native 4.12 `RTLPowerTrace` showed setPowerState(0) -> vendor_suspend 7 ms after RTLAirInterface joined our power plane at state 0 (no own power desire), and the resume left bulk IN dead. RTL8814Native 4.13 calls changePowerStateTo(1): verified 13:20 with the layer fully started (linked ch 153, WPA2, IP).
- [x] airportd drives the interface (0.11, 2026-10-27): InterfaceNamer had given it en2's unit (DB keyed by MAC); a locally administered MAC with rtlair=1 got en3, and the native Wi-Fi menu / Settings pane appear.
- [x] Scan results in the Wi-Fi menu: 0.12 posts APPLE80211_M_SCAN_DONE (10); 0.13 posts inside the IO80211WorkQueue gate (IO80211PostOffice::sendMail drops messages posted outside it). Verified 2026-10-27: airportd gets 10 results on en3.
- [x] Join from the Wi-Fi menu (rtlair=2): driver 4.14 drops en2 when rtlair>=2 so the Skywalk interface keeps the hardware MAC; layer 0.13 joins with the PMK from airportd and hooks the data path. First try: WCL aborted the join (WCL_LOW_LATENCY_INFO unsupported; interface on a private MAC the radio did not use). 4.15 (rtlRadioSetMac) + 0.14 (low latency info, radio MAC follows the interface): first association from the menu, then WCL left (EXTENDED_BSS_INFO). 0.15 answered it and the link stayed up, but the first RX frame panicked (unprepared Skywalk RX packet). 0.16 prepared them with the RX completion queue and panicked in its default packetCompletion; 0.17 prepares them with no source queue: first working join from the menu (DHCP, ping, 20 MB download on en2), but WCL left every 60 s ("missed beacons timeout": no LQM events). 0.18 posts LQM every 5 s: VERIFIED stable (no drops, 0% ping loss, ~300 Mbit/s down, ~116 Mbit/s up on en2). Boot auto-join works; reconnect after a menu disconnect needed a re-issued join (0.19 verified, 0.20 retries sooner). Next: why the first join after a disconnect fails in the radio, remaining Unsupported WCL IOCs (WCL_REASSOC: 0.21 refused the roam during the scan WCL aborts first; 0.22 fixes that; "nothing better" path verified on 5 GHz, the roam itself still untested). 4.16 + 0.23: menu details (MCS/NSS/country/noise VERIFIED with 4.17; noise is SNR-derived, reads high on strong links), beacon info, traffic counters, roam profile; WCL settings accepted. 0.24: CHANNELS_INFO (VERIFIED). 0.25: FW_HOT_CHANNELS, PLATFORM_CONFIG, SOI/real-time/BT coex settings (VERIFIED: no layer IOC used by WCL/airportd still unsupported). First-join drop after a disconnect: driver 4.18/4.19 join log shows the AP never answers the first auth (no mgmt frame back, beacons fine, TX/RX regs fine); the retry after a directed SSID survey works. 4.20's directed one-channel probe survey did NOT help (tested: auth still ignored) — the constant is time since our last frame to the AP. 4.21 (VERIFIED at 21 s: flush scanq → full SSID survey → first-try join; boot cache join untouched) flushes the scan queue on joins within 25 s of a leave. **Window measured on 4.21's aggressive test: the AP ignores auth ~19-20 s after a disassociation and each ignored auth refreshes it** (auth at 17.2 s failed even after the survey; auth at 22.5 s answered). **Driver 4.22 (VERIFIED ON HARDWARE):** `REAUTH_LIMIT` 4→32 (`rtw_mlme_ext.h:33`) + `MAX_JOIN_TIMEOUT` 6500→15000 (`rtw_mlme.h:22`). Tested live on hardware with an aggressive 5.2s reconnect: retries 0..16 spanned the silence window, AP answered at retry 16, resulting in **100% first-try association** with zero `join res -1` and zero layer rescues! **Layer 0.26 (DEPLOYED to EFI):** added `setWCL_SET_ROAM_LOCK` returning `kIOReturnSuccess`, silencing the IO80211Family roam lock queue error.
- [x] rtlair=2: do not publish en2 while the layer owns the radio (same MAC conflict). Done in driver 4.14: `RTLEthernetAdapter::start` skips the legacy Ethernet interface when `rtlair>=2`, so the Skywalk interface keeps the hardware MAC as en2.
- [ ] In-place vendor_resume leaves bulk IN dead (exposed by the PM bug in 4.12; affects sleep/wake without re-enumeration). On this machine real sleep re-enumerates the dongle, so this is hardening for other boards.
- [ ] Live roam test (layer 0.22 leave+join): the "nothing better" path is verified on 5 GHz (ch 153); still needs a boot that lands on 2.4 GHz to watch airportd's BEST CONNECTED ROAM fire the leave+join to 5 GHz.

airportd-driven Wi-Fi (System Settings, Keychain, auto-join). Requires
RE of the IO80211/Skywalk class layouts for the exact Tahoe build —
see how YYavicWiFi pins builds. License note: this layer must be
written fresh (GPL-2.0-compatible) — do not copy itlwm/YYavic code
here without resolving the GPL-2/GPL-3 combination.

---

## Debugging playbook

- `log stream --predicate 'eventMessage CONTAINS "RTL8814Native"'`
- Panic files: `/Library/Logs/DiagnosticReports/Kernel-*.panic`
- USB view: `ioreg -p IOUSB -w0 -l` (look for the claimed interface)
- Kext load failures: `sudo kmutil load -p ... -v` and
  `kmutil showloaded | grep RTL`
- Common phase-1 pitfalls: spinlock section too long (XNU panics
  "spinlock held too long" — convert to mutex), sleeping in timer
  callbacks, mbuf allocation in workloop context (use MBUF_DONTWAIT).

## Open: security modes beyond WPA2-PSK (documented 2026-10-27, NOT implemented)

The layer (`setWCL_ASSOCIATE`, `Source/AirLayer/RTLAirLayer.cpp`) accepts open networks and a 32-byte PMK (WPA2-PSK, derived by airportd); any other key length is refused with `unsupported`. The vendor Windows/Linux driver supports more, so these are real gaps:

- [ ] **WPA3-SAE.** Needs the SAE commit/confirm exchange inside authentication frames (P-256 elliptic-curve arithmetic written in-kernel; no convenient public KPI), the SAE AKM in the RSN IE, PMF (802.11w, MFP required by WPA3) and PMKID handling; then the existing in-radio 4-way handshake runs from the derived PMK. Step 0 before writing code: log what airportd hands the layer for a SAE join (auth type at `c+16/c+20`, key type/length, RSN IE) against a WPA3-only SSID.
- [ ] **WPA2/WPA3-Enterprise (802.1X, certificates).** macOS runs EAP in userspace (`eapolclient`); the driver must associate with the 802.1X AKM, pass EAPOL frames both ways through the interface, and accept the PMK at the end of EAP to run the 4-way handshake. Needs a network with a RADIUS server to test; `set4wayHsTimeout`/`isEAPJoin` hints already exist in the layer.
- [ ] WPA/TKIP-only and WEP: not planned.
- [ ] Channel-switch follow without deauth: see the CSA note above (cannot be verified on the current AP).
- [ ] Auth retry budget: 4.22 retries auth for ~9.6 s; the AP ignores auth for ~20 s after any disassociation we cause (including a CSA deauth). Raising `REAUTH_LIMIT` to ~70 would cover the whole window.

Nothing in this list was testable with the available hardware (one WPA2/WPA3-transition router, no WPA3-only SSID, no RADIUS server).

## Open: Apple continuity features (AirDrop, AirPlay, Handoff, Sidecar, Universal Control) — started 2026-10-27

- [x] Stage 1a (kernel side): `VIRTUAL_IF_ROLE/PARENT` failures are normal for a primary interface (IO80211Family answers 6); not a bug. awdl0 is created by `setVIRTUAL_IF_CREATE`; our layer never receives it. See `Docs/ARCHITECTURE.md` (2026-10-27, stage 1).
- [x] Stage 1b: the `sharingd` "WiFiP2P bit is not set" line was a startup transient. The real gate is `wifip2pd`: `CARD_CAPABILITIES` kernel byte `+8` bit 0 (AWDL) and `+13` bit 0 (NAN); IO80211Family strips the 4-byte version before the userspace copy. Found by disassembling `/usr/libexec/wifip2pd` with `dyld_info` and confirmed with `Tools/re/cardcap.c`. See `Docs/ARCHITECTURE.md` ("AWDL gate FOUND").
- [x] Stage 1c: layer 0.27 sets `p[7]|=1, p[8]|=1`; VERIFIED on hardware: `wifip2pd` now sends `VIRTUAL_IF_CREATE` (role 6 = AWDL) and our stub answers unsupported. No panic, link unchanged.
- [ ] Stage 2 (analysed, NOT started, low value alone): `setVIRTUAL_IF_CREATE` role 6 = a second Skywalk interface (`createProxInterface`) that only yields an empty `awdl0`; test AirPlay/Handoff/Universal Control with a second Apple device on the same Apple ID.
- [ ] Stage 3: real AWDL (channel time-slicing between the AP channel and 6/149, AWDL frames and sync) for AirDrop and wireless Sidecar; may not be feasible on a USB radio.

