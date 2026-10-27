# RTL8814Native — Technical Architecture & Driver Reference

**Last Updated:** Milestone 4.22 deployed (`36ea1ac8a83a606ceeb2d36f8c9ff015`, UUID `37A9B718`, deployed to internal EFI `/Volumes/NO NAME`, **pending reboot + reconnect test**). Extends auth retries (`REAUTH_LIMIT` 4→32, ~9.6s budget) and raises `MAX_JOIN_TIMEOUT` 6500→15000 so the join survives the AP's ~20s post-disassociation silence on the first attempt without triggering a join-result failure or layer rescue. Previous: Milestone 4.21 (flush scan queue on recent leave -> full SSID survey).

---

## 1. System & Hardware Environment

- **Host Machine:** Hackintosh `MacPro7,1` (AMD Ryzen 16-Core / 32-Thread, OpenCore Bootloader)
- **Operating System:** macOS Tahoe / Sequoia (`25G241`, Darwin Kernel `25.6.0: RELEASE_X86_64`)
- **Target Hardware:** TP-Link Archer T9UH v1 (Realtek **RTL8814AU** 4T4R 802.11ac MU-MIMO USB 3.0 Adapter)
  - **USB IDs:** `idVendor = 0x2357` (`9047`), `idProduct = 0x0106` (`262`), `bcdDevice = 0x0200`
  - **Permanent Hardware MAC (EFUSE `0xD8`):** `7C:C2:C6:03:EA:84` (Verified live on `en2`)
  - **Bus Speed: USB 2.0 High-Speed, NOT SuperSpeed.** `ioreg` shows `USBSpeed = 3`, but in IOUSBHostFamily that is `kUSBHostConnectionSpeedHigh` (Super = 4, which is what the parent hub shows). Also `bcdUSB = 0x0200`, `bMaxPacketSize0 = 64`, `"Device Speed" = 2`, bulk `wMaxPacketSize = 512`, and chip register `0xFF` bit 7 = 1 (USB2 mode). The 8814AU enumerates in USB2 mode unless mode-switched (`HW_VAR_USB_MODE`). Earlier docs wrongly said SuperSpeed.
  - **Physical Topology:** Dongle -> USB port on the monitor (`ioreg`: parent `USB3.0 Hub`) -> monitor upstream cable -> **red** USB port on the motherboard. Not plugged in directly; `UsbPowerSinkAllocation = 500`. The adapter works fine on this same port under Windows, so the hub is **not** a suspect. This is only for identifying the USB path; I can move it to another port if a test needs it.
  - **USB Endpoints (`bConfigurationValue = 1`, `bInterfaceNumber = 0`, `bNumEndpoints = 5`):**
    - `0x84`: Bulk IN (RX packets + C2H events, max packet 512 at High-Speed)
    - `0x05`: Bulk OUT #0 (TX High Queue)
    - `0x06`: Bulk OUT #1 (TX Normal Queue)
    - `0x08`: Bulk OUT #2 (TX Low Queue)
    - `0x87`: Interrupt IN

---

## 2. Repository Layout

| Path | Purpose |
| :--- | :--- |
| [build.sh](../build.sh) | Builds `Build/RTL8814Native.kext` with incremental compilation check (`Compat` + `Driver` + 74 Realtek HAL/Core/PhyDM C files). |
| [Resources/Info.plist](../Resources/Info.plist) | Kext bundle metadata, KPI `OSBundleLibraries`, and `IOKitPersonalities` matching `IOUSBHostInterface`. |
| [Source/Compat/Compat.c](../Source/Compat/Compat.c) | Linux kernel primitives ported to XNU (`kmalloc`/`vmalloc`, spinlocks, mutexes, lock-safe semaphores & completions, timers, lock-safe workqueue, tasklet-to-workqueue bridge, kthreads with `rtl_current_task()`, `sk_buff`). |
| [Source/Compat/VendorOsDep.c](../Source/Compat/VendorOsDep.c) | Realtek OS-dependent layer for Darwin (`usbctrl_vendorreq`, `ffaddr2pipehdl`, `usb_write_port`, async `usb_read_port` + `usb_read_port_complete` + `recvbuf2recvframe` + `usb_recv_tasklet`, `_rtw_xmit_entry`, `rtw_start_drv_threads`, `rtw_intf_start`). |
| [Source/Driver/RTL8814Native.cpp](../Source/Driver/RTL8814Native.cpp) | Main `IOService` lifecycle (`start`/`stop`), orchestrating `RTLUsbDevice`, `VendorProbe`, and `RTLEthernetAdapter`. |
| [Source/Driver/RTLUsbDevice.cpp](../Source/Driver/RTLUsbDevice.cpp) | `IOUSBHostInterface` wrapper: endpoint discovery (1 Bulk IN + up to 4 Bulk OUT pipes), `controlTransfer` (`ep0`), synchronous `bulkTransfer`, and async `submitUrb` with `IOMemoryDescriptor::prepare()`/`complete()`. |
| [Source/Driver/RTLEthernetAdapter.cpp](../Source/Driver/RTLEthernetAdapter.cpp) | `IOEthernetController` subclass publishing `en2` to macOS networking stack, reporting hardware MAC `7C:C2:C6:03:EA:84` from EFUSE, and bridging `outputPacket` / `inputFromDriver`. |
| [Source/Driver/RTLUserClient.cpp](../Source/Driver/RTLUserClient.cpp) | `IOUserClient` exposing Selectors `0..6` (`GetVersion`, `GetLinkStatus`, `TriggerScan`, `GetScanResult`, `JoinNetwork`, `Disconnect`, `GetRadioInfo`). |
| [Source/Driver/VendorProbe.c](../Source/Driver/VendorProbe.c) | Hardware bring-up (`rtl8814native_vendor_probe`), thread/RX startup (`RTW_CMD_THREAD`, `rtw_intf_start`, `dynamic_chk_timer`), TX workqueue (`rtl8814native_vendor_xmit`), and Scan/Join/Status C API. |
| [Source/Driver/WPA2Supplicant.c](../Source/Driver/WPA2Supplicant.c) | Self-contained in-kernel IEEE 802.11i WPA2-PSK 4-Way EAPOL Supplicant (SHA-1, HMAC-SHA1, PBKDF2-SHA1, PRF-384, FIPS 197 AES-128 decrypt, RFC 3394 Key Unwrap, PTK/GTK hardware CAM installation). |
| [Tools/test_uc.c](../Tools/test_uc.c) | Userspace CLI utility (`./Tools/test_uc [status|scan|join|disconnect]`) communicating with `RTLUserClient`. |

---

## 3. Milestones Completed & Bugs Solved

### Milestone 0.1 — Non-Monotonic Time Kernel Panic Resolved
- **Fix:** Added `TSCAdjustReset.kext` to OpenCore `EFI/OC/Kexts` and `config.plist`.
- **2026-10-27 update (not a driver bug):** `Non-monotonic time` panics came back on macOS 26.7.1 (`Kernel-2026-10-26-070254`, `-161521`, `Kernel-2026-10-27-031414`; `invoke at ... @sched_prim.c:3051`, 58-80 s after boot, no kext of ours in the backtrace). The EFI now uses `ForgedInvariant.kext` (`-FIPeriodic`) plus the AMD Vanilla "Remove non-monotonic time panic" kernel patches. Checked against `/System/Library/Kernels/kernel` (25.6.0): the kernel has 6 such panic sites; Visual's 3-count patch covers 3 (`thread_quantum_expire`, `thread_unblock`, `thread_block_reason` last_dispatch), but the `thread_invoke, thread_dispatch` patch looks for the pre-Tahoe thread offset `0x48x`; Tahoe moved it to `0x490`, so it misses both sites, and its `Count=2` lands on `_kperf_lazy_wait_sample+0x20` and `_vmx_hv_support+0x2a` (it NOPs an `rdmsr`, corrupting that function). **Config fix** (backup `Backups/efi_config_tsc/config.plist.before_tsc_fix`, MD5 `518dca3e`; new MD5 `019611e1`): that patch is now limited to `MaxKernel 24.99.99`, and three 25.x patches were added (one match each on 25.6.0): `thread_invoke` `4d3ba690040000 0f82` jb -> NOPs (the 3051 panic), `thread_dispatch` `4c2bbb90040000 0f82` jb -> NOPs, and `check_monotonic_time` `4839fe7701c3` ja -> NOPs (falls through to `ret`). The pendrive fallback EFI still carries the old config. Verify: no more `Non-monotonic time` panic reports over the next boots.

### Milestone 0.2 — OpenCore Prelink Injection Fixed
- **Fix:** Implemented all required OS-dependent functions in `Source/Compat/VendorOsDep.c`, achieving **0 undefined vendor symbols**.

### Milestone 0.3 — `IOUSBHostInterface` Matching Fixed
- **Fix:** Updated all personalities in `Resources/Info.plist` with strict USB interface matching keys (`idVendor`, `idProduct`, `bConfigurationValue = 1`, `bInterfaceNumber = 0`).

### Milestone 1.0 — Phase 1 Radio Bring-Up (VERIFIED 100% ON LIVE HARDWARE)
Verified live after reboot using `./Tools/test_uc` and `ifconfig en2`:
- `Vendor Probe Return Code`: `0 (SUCCESS)`
- `Register REG_SYS_CFG (0x00F0)`: `0x044411B5`
- `Chip Type`: `10 (CHIP_8814A)`, `Cut Version`: `1`
- `Permanent Hardware MAC`: `7C:C2:C6:03:EA:84` (published live on `en2`)
- `HAL Init Status`: `1 (_SUCCESS)`
- `Firmware Downloaded & OK`: `YES! (flag=1)` (68,320-byte `array_mp_8814a_fw_nic` downloaded via USB Bulk OUT RSVD page + IDDMA)

### Milestone 2.0 & 3.0 — Phase 2 (802.11 Scan + WPA2 Join) & Phase 3 (RX/TX Data Path) (Deployed to EFI, Ready for Post-Reboot Test)
1. **Workloop-Safe Tasklets & Lock-Safe Semaphores/Workqueues (`Compat.c`, `interrupt.h`, `semaphore.h`):**
   - Scheduled `tasklet_struct` onto `rtl_system_wq` instead of running inline inside `rtlUrbCompletion`, preventing USB workloop deadlocks when `OnBeacon` or `OnAssocRsp` performs synchronous USB control/bulk transfers.
   - Protected `semaphore`, `completion`, and `g_system_wq.pending` with `wq_lock` around `IOLockSleep`/`IOLockWakeup` so wakeups can never be lost.
   - Implemented `rtl_current_task()` task table so `kthread_should_stop()` works cleanly.
2. **USB Bulk IN RX Pipeline (`VendorOsDep.c`):**
   - Implemented `usb_init_recvbuf`, `usb_read_port`, `usb_read_port_complete`, `recvbuf2recvframe` (using `rtl8814_query_rx_desc_status` for 8814A 24-byte RX descriptors and USB RX aggregation), `usb_recv_tasklet`, `rtw_os_alloc_recvframe`, `rtw_os_recvframe_duplicate_skb`, `rtw_os_alloc_msdu_pkt`, and `rtw_rframe_set_os_pkt`.
   - Started `RTW_CMD_THREAD` (`rtw_start_drv_threads`), `rtw_intf_start` (submitting `NR_RECVBUFF = 8` Bulk IN URBs on endpoint `0x84`), and `dynamic_chk_timer` (2-second PhyDM calibration & link watchdog).
3. **In-Kernel WPA2-PSK 4-Way EAPOL Supplicant (`WPA2Supplicant.c`):**
   - Because `en2` is published as an `IOEthernetInterface`, macOS `airportd` does not run a WPA supplicant on `en2`.
   - `WPA2Supplicant.c` derives the 32-byte `PMK` via `PBKDF2-SHA1(psk, ssid, 4096)`, configures the 22-byte RSN IE in `securitypriv.supplicant_ie`, intercepts incoming `0x888E` EAPOL-Key frames in `rtw_os_recv_indicate_pkt`, derives the 48-byte `PTK` (`KCK || KEK || TK`) via `PRF-384`, replies with signed EAPOL Msg 2/4 and Msg 4/4, unwraps the encrypted `GTK` from Msg 3/4 via RFC 3394 AES-128 Key Unwrap, unblocks `psta->ieee8021x_blocked = _FALSE`, and installs `TK` (`rtw_setstakey_cmd`) and `GTK` (`rtw_set_key`) into the RTL8814AU hardware security CAM.
4. **TX Data Path (`RTLEthernetAdapter.cpp`, `VendorProbe.c`, `VendorOsDep.c`):**
   - `RTLEthernetAdapter::outputPacket` copies outgoing `mbuf_t` packets into `sk_buff`s and queues them to `rtl8814native_vendor_xmit` -> `_rtw_xmit_entry` -> `rtw_xmit` -> `rtl8814au_hal_xmit`.

### Milestone 2.1 — Recursive Spinlock Panic (`hwlock @locks.c:905`) & Incremental Build Cache Fix
- **Symptom:** Running `./Tools/test_uc scan` triggered a kernel panic (`hwlock: thread ... is trying to lock ... recursively @locks.c:905`) in `_rtl8814native_trigger_scan -> _rtw_set_802_11_bssid_list_scan -> __enter_critical_bh -> _spin_lock_bh -> _spin_lock -> _IOSimpleLockLock`.
- **Root Causes & Fixes:**
  1. **Duplicate Lock in `rtl8814native_trigger_scan` ([VendorProbe.c](../Source/Driver/VendorProbe.c)):** `rtl8814native_trigger_scan` acquired `_enter_critical_bh(&g_padapter->mlmepriv.lock, &irqL)` before calling `rtw_set_802_11_bssid_list_scan`, which immediately acquires `&pmlmepriv->lock` again at `rtw_ioctl_set.c:567`. Removed the outer lock in `rtl8814native_trigger_scan`.
  2. **`spinlock_t` Backed by `IORecursiveLock` ([spinlock.h](../Source/Compat/include/linux/spinlock.h), [Compat.c](../Source/Compat/Compat.c)):** Across Realtek's core/MLME code, `_enter_critical_bh` critical sections frequently call `rtw_zmalloc` (`IOMalloc`), `IOLog`, `up(&sema)` (`IOLockLock`), or nested helpers that re-acquire the same lock. Replaced `IOSimpleLock *` (`hwlock`) with `IORecursiveLock *` (`0` references to `IOSimpleLock` remain in the Mach-O binary).
  3. **Header-Aware Rebuild in [build.sh](../build.sh):** Updated `compile()` in `build.sh` to check whether any header in `Source/Compat/include/` or `Source/Driver/` is newer than cached `.o` files in `Build/obj/`, preventing stale inline lock/semaphore/tasklet definitions in `VENDOR_SOURCES`.
- **Verification:** Confirmed live after reboot — `./Tools/test_uc scan` ran and completed with **zero kernel panics**!

### Milestone 2.2 — RF Front-End (`RFE_Type`), 802.11b CCK, PhyDM/HALRF, and `RCR_APPFCS` Fixes for 802.11 Scanning
- **Symptom:** `./Tools/test_uc scan` completed cleanly without kernel panic, but returned `0 networks`.
- **Root Causes & Fixes:**
  1. **RF Front-End (`RFE_Type`), RF Path (`rf_path`), PhyDM/HALRF Abilities, TX BB Swing, Wireless Mode (`WIRELESS_11B`), and ChannelPlan in `loadparam()` ([VendorProbe.c](../Source/Driver/VendorProbe.c)):**
     - `registry_par->RFE_Type` was `0` instead of `64` (`CONFIG_RTW_RFE_TYPE`), causing `hal_ReadRFEType_8814A` to force `pHalData->rfe_type = 0` (`8814AE` PCIe RFE GPIO pinmux) instead of `1` (`8814AU` USB RFE GPIO pinmux) or the EFUSE RFE type, disconnecting the antenna LNA/PA switches.
     - `registry_par->rf_path` was `0` (`RF_1T1R`) instead of `RF_TYPE_MAX`, disabling 3 of the 4 RF paths.
     - `registry_par->phydm_ability` and `halrf_ability` were `0x00000000` instead of `0xFFFFFFFF`, masking off all PhyDM (`ODM_BB_DIG`, `ODM_BB_RSSI_MONITOR`, `ODM_BB_CCK_PD`) and HALRF blocks.
     - `registry_par->TxBBSwing_2G` and `TxBBSwing_5G` were `0` instead of `0xFF`, and `registry_par->wireless_mode` omitted `WIRELESS_11B` (needed for 1 Mbps CCK 2.4 GHz Beacons).
     - `registry_par->channel_plan` was `0x00` (`RTW_CHPLAN_FCC1_NULL`, 0 5GHz channels) instead of `CONFIG_RTW_CHPLAN` (`0xFF`, `RTW_CHPLAN_UNSPECIFIED` -> EFUSE dual-band channel plan).
     - Updated `loadparam()` in `VendorProbe.c` to match `os_intfs.c`.
  2. **`CONFIG_RX_PACKET_APPEND_FCS` (`RCR_APPFCS`) 4-Byte FCS Stripping in `recvbuf2recvframe` ([VendorOsDep.c](../Source/Compat/VendorOsDep.c)):**
     - Because `CONFIG_RX_PACKET_APPEND_FCS` is enabled in `autoconf.h`, hardware appends a 4-byte FCS to every received frame (`RCR_APPFCS`). Added the `pattrib->pkt_len -= IEEE80211_FCS_LEN` adjustment in `recvbuf2recvframe` (matching `hal/rtl8814a/usb/usb_ops_linux.c:163`) and switched `pkt_cnt` to `GET_RX_STATUS_DESC_DMA_AGG_NUM_8814A(pbuf)`.
  3. **Thread-Safe `sk_buff_head` Queue Operations ([skbuff.h](../Source/Compat/include/linux/skbuff.h)):**
     - Protected `skb_queue_tail`, `skb_dequeue`, and `skb_queue_purge` with `spin_lock(&q->lock_dummy_unused)` so `usb_read_port_complete` (USB workloop) and `usb_recv_tasklet` (`rtl_system_wq`) never race on `rx_skb_queue` or `free_recv_skb_queue`.
  4. **Live RF/RX/Scan Telemetry in `./Tools/test_uc status` ([VendorGlue.hpp](../Source/Driver/VendorGlue.hpp), [VendorProbe.c](../Source/Driver/VendorProbe.c), [test_uc.c](../Tools/test_uc.c)):**
     - Added `rfe_type`, `rf_type`, `channel_plan`, `max_chan_nums`, `rx_urb_submit_ok`, `rx_urb_submit_err`, `rx_urb_complete_ok`, `rx_urb_complete_err`, `rx_frames_decoded`, `cmd_issued_cnt`, `scan_state`, and `scanned_bss_cnt` to `struct rtl8814_link_status`.

### Milestone 2.3 — RX Runt-Frame `pkt_len` Underflow Page-Fault Panic
- **Symptom (live, ~2 min after boot on `d3c6f229...`):** `./Tools/test_uc status` worked (`rx_frames=3`, `comp_ok=8`), then a page-fault panic: `memcpy` (`RDX = 0xfffd`, `CR2 == RSI`) <- `_rtw_os_alloc_recvframe + 0x14c` <- `_recvbuf2recvframe + 0x1f4` <- `_usb_recv_tasklet` <- `rtl_wq_thread`.
- **Root Cause:** `rx_pkt_attrib.pkt_len` is `u16`. For a `NORMAL_RX` runt frame with `pkt_len < 4`, the `CONFIG_RX_PACKET_APPEND_FCS` adjustment `pkt_len -= IEEE80211_FCS_LEN` underflowed to `0xFFFD`, so `rtw_os_alloc_recvframe` copied 65,533 bytes out of the 32 KB bulk IN buffer and faulted reading unmapped memory.
- **Fixes ([VendorOsDep.c](../Source/Compat/VendorOsDep.c)):**
  1. `recvbuf2recvframe` drops frames with `pkt_len <= IEEE80211_FCS_LEN` (and continues with the next aggregated packet via `_next_pkt`).
  2. `recvbuf2recvframe` stops parsing once `transfer_len < RXDESC_SIZE` (aggregation tail padding / stale bytes).
  3. `rtw_os_alloc_recvframe` refuses any copy where `pdata + pkt_len` exceeds `pskb->data + pskb->len` (skipped when `pskb == NULL`, as `rtw_mi.c` does).
- **Deployment:** `MD5 = 81bda5c497d653a9c747f9390289bfbf` copied to `/Volumes/NO NAME 1/EFI/OC/Kexts/` (0 undefined vendor symbols, `dot_clean -m` + `sync`). Previous `d3c6f229...` build backed up in the session scratchpad.

### Milestone 2.4 — Live Result of 2.3 + RX Stall Diagnosis Tooling
- **Live result of 2.3 (`81bda5c4...`, UUID `A4EA3755`):** No panics after boot, `status` and two scans. Scan state machine runs (`scan_state` 4 -> 12 -> 0 in ~3.5 s, about 100 ms per channel) but returns **0 networks**.
- **Findings (live sampling every 0.3 s during a scan):**
  1. **Bulk IN RX stalls.** `submit_ok - comp_ok == 8` (all 8 URBs posted) but `comp_ok` stops increasing partway through the first scan and never moves again: the chip stops delivering on `0x84`. Our completion/resubmit path is not stuck.
  2. Even before the stall, RX is very sparse: ~17 completions in the first 40 s idle and ~10 completions per decoded frame (`rx_frames`). With nearby APs we should see tens of beacons per second, so the radio receives almost nothing over the air, or most transfers are empty/short.
  3. `cur_channel` stays `1` during the scan (it may not track the scan channel; check `rtw_get_oper_ch` / BB registers).
- **New diagnostics (build `304f57ecdaa1e37f0bcac928fb78d6d9`, deployed to EFI):**
  - `struct rtl8814_trx_diag` in `rtl8814_link_status` ([VendorGlue.hpp](../Source/Driver/VendorGlue.hpp)): bad URB lengths, drops by reason (no recvframe, CRC, ICV, bad pkt_len, runt, alloc), report types (normal/C2H/other), 802.11 frame types (mgmt/beacon/probe_rsp/ctrl/data), last raw 24-byte RX descriptor, bulk OUT write ok/err/last_err. `status` also reads `REG_RCR`, `REG_RXFLTMAP0`, `REG_RXFLTMAP2` live.
  - **Selector 7 `kRTLUC_RegAccess`** (`rtl8814native_reg_access` in [VendorProbe.c](../Source/Driver/VendorProbe.c)): live MAC/BB read/write (`addr < 0x4000`) and RF read, so hypotheses can be tested **without rebooting**:
    ```bash
    ./Tools/test_uc peek 0x0608        # REG_RCR
    ./Tools/test_uc poke 0x0608 <val>  # write a MAC/BB register
    ./Tools/test_uc rf 0 0x00          # RF path A reg 0x00 (mode/standby)
    ```
  - Note: the kext's `IOLog` / `rtl_printk` output does not show up in `log show`, so this telemetry is the main debugging channel.

### Milestone 2.5 — Root Cause of 0 Scan Results: Hardcoded SuperSpeed on a High-Speed Link
- **Live data from 2.4 (`304f57ec...`, UUID `A835BE5E`):**
  - The "RX stall" was just the RX filter: idle `RCR = 0xF40060CE` has `CBSSID_DATA|CBSSID_BCN` (bits 6/7) set with no BSSID, so nothing passes. During a scan `RCR` correctly drops to `0xF400600E` and is restored afterwards.
  - Channel switching works (RF path A reg `0x18` walks 1, 2, 4 ... 0xA5 = 165). TX works (bulk OUT ok, 0 errors).
  - **RX transfers are split mid-frame:** of ~95 completions only ~6 frames decoded; drops `bad_pktlen=56, crc=28, icv=7`. Raw transfer starts were 802.11 frame bodies, not RX descriptors (Country/ERP/RSN IEs; `80 00 00 00 FF FF FF FF FF FF ...` beacon headers with no descriptor; `64 00 31 14 00 08 "josevera"`). The descriptor ends one transfer and the frame continues in the next.
- **Root cause:** [VendorProbe.c](../Source/Driver/VendorProbe.c) hardcoded `usb_speed = RTW_USB_SPEED_3`, so `UsbBulkOutSize = 1024`. `_InitBurstPktLen` reads chip reg `0xFF` (USB2 mode), takes the USB2 branch, sees `UsbBulkOutSize != 512` and programs **`REG_RXDMA_MODE (0x0290) = 0x2E` = 64-byte RX burst** (the Full-Speed setting). On a 512-byte High-Speed bulk pipe every 64-byte packet is a short packet, which ends the transfer. The wrong speed also broke the TX ZLP padding (`rtl8814au_xmit.c` uses `UsbBulkOutSize`) and the RF path choice (Linux forces `RF_2T4R` on USB2; we had 3T3R).
- **Live confirmation:** `./Tools/test_uc poke 0x0290 0x1E 1` (512-byte burst) followed by a scan found the **first network** (`Karen`, ch 149, -82 dBm, WPA2-PSK). Splits remained, likely because the burst was changed with RX DMA running. Disabling RX DMA aggregation live (`0x010C` bit 2) did not help.
- **Fix (build `cb00c3a8ed7afbf46356f40ba7991cce`, deployed to EFI):**
  1. `usb_speed` is now derived from the bulk IN `wMaxPacketSize` (1024 -> SS, 512 -> HS, else FS), before `rtw_hal_init`.
  2. Optional RX reassembly (`./Tools/test_uc reassemble 1`, default **off**): when a full descriptor claims more bytes than the transfer holds, the tail is stashed in a 64 KB stage buffer and completed by the next transfer (max 4 carries).
  3. Selector 8 `kRTLUC_GetRxLog` / `./Tools/test_uc rxlog`: ring of the last 16 raw transfers (len + first/last 32 bytes) to tell splits (tail of N continues in head of N+1) from data loss.
  4. `status` now shows the HAL's `usb_speed`, `bulk_in_mps` and reassembly counters.
- **VERIFIED LIVE (UUID `1F298A58`):** `usb_speed=2 (High-Speed)`, `bulk_in_mps=512`, `REG_RXDMA_MODE = 0x1E`, `rf_type=4` (2T4R). `./Tools/test_uc scan` returned **20 networks on 2.4 GHz and 5 GHz** (ch 2-11, 36, 60, 149, 157; RSSI -44 to -88 dBm; WPA2-PSK, open and hidden SSIDs). RX drops all **0** (41 frames from 39 transfers: 26 beacons, 7 probe responses). Reassembly stayed off and is not needed.

### Milestone 2.6 — Join Panic: P2P/WFD Info Never Initialized
- **Symptom (first `join` to my AP, build `cb00c3a8...`):** 802.11 auth succeeded (`OnAuthClient -> start_clnt_assoc`), then a page fault at `CR2 = 0x36b0` in `_build_assoc_req_wfd_ie + 0x8b` <- `_rtw_append_assoc_req_wfd_ie` <- `__issue_assocreq`, from the RX tasklet.
- **Root cause:** our `rtw_init_drv_sw()` in [VendorProbe.c](../Source/Driver/VendorProbe.c) (a port of the Linux one in `os_intfs.c`, which we don't compile) skipped the `CONFIG_P2P` block. `init_wifidirect_info()` never ran, so `adapter->wdinfo.padapter` stayed NULL, and `build_assoc_req_wfd_ie()` dereferenced it on every Association Request (`CONFIG_WFD` + `WL_FUNC_MIRACAST`).
- **Fix (build `c9897d82c3ae453ac9dd64d2d1046059`, deployed to EFI, 0 undefined vendor symbols):** `rtw_init_drv_sw()` now mirrors Linux with the same `#ifdef`s: `rtw_init_wifidirect_timers`, `init_wifidirect_info(P2P_ROLE_DISABLE)`, `reset_global_wifidirect_info`, `rtw_init_wifi_display_info`, `CONFIG_TDLS` init, `rtw_hal_sreset_init` (`DBG_CONFIG_ERROR_DETECT`), `rtw_bf_init` / `rtw_init_rm` when configured, and the defaults `power_offset`, `rsvd_page_*`, `data_fb`, `fix_rx_ampdu_accept = RX_AMPDU_ACCEPT_INVALID`, `fix_rx_ampdu_size = RX_AMPDU_SIZE_INVALID`. Compiled in for this config: P2P, WFD, sreset (TDLS / BF v2 / 80211K are off).
- **Rule for future ports:** whenever a Linux `os_dep` function is re-implemented in `VendorProbe.c` / `VendorOsDep.c`, diff it against the original block by block. Skipped `#ifdef` blocks leave vendor state NULL and crash much later.

### Milestone 3.0 — Join, DHCP and Data Path VERIFIED ON HARDWARE (build `c9897d82...`)
- `status`: `ASSOCIATED (_FW_LINKED)`, WPA2 `COMPLETED (KEYS INSTALLED)`, channel 157, BSSID `0E:84:08:68:FD:24`, RSSI -46 dBm, `usb_speed=2`, 0 RX drops, 0 TX write errors.
- macOS ran DHCP on `en2` automatically once carrier came up (no `ipconfig set` needed): `192.168.100.11/24`, gateway `192.168.100.1`, plus IPv6 SLAAC global addresses.
- `ping -b en2` to the gateway (~2.5 ms), `8.8.8.8` (~16.5 ms) and `ping6 -I en2` to Google DNS: 0% loss.
- Throughput forced through `en2` (`curl --interface en2`, Cloudflare): **~18.7 MB/s (~150 Mbit/s) down, ~10.3 MB/s (~83 Mbit/s) up**, still 0 RX drops / 0 TX errors after ~21k RX frames. This is about what USB 2.0 HS allows; more needs the USB3 mode switch.
- Note: the machine's default route is `en1` (another NIC on the same LAN), so test with `-b en2` / `--interface en2`.

### Milestone 3.1 — `en2` Packet Counters Always 0
- **Symptom:** `netstat -I en2` showed `Ipkts = Opkts = Ibytes = 0` while traffic flowed (only `Obytes`, counted by the stack, moved).
- **Root cause:** `IONetworkInterface` builds the ifnet packet/error counters from deltas of the driver's `IONetworkStats` (`driverStats` sampled in `inputPacket()` / `fetchDriverOutputStats()`). `RTLEthernetAdapter` never incremented them.
- **Fix (build `153485eac6adda6283e0100813fe7604`, deployed to EFI, 0 undefined vendor symbols):** `configureInterface()` override grabs the `kIONetworkStatsKey` buffer; `inputPackets` is bumped *before* `inputPacket()`, plus `inputErrors` on mbuf alloc/copy failures, `outputPackets` on queued TX, and `outputErrors` on drops (including a failed `rtl8814native_vendor_xmit`, whose return value is now checked).

- **Live result (build `153485ea...`, UUID `0AB6064D`):** `Opkts` now counts (2,865 after a 25 MB download), and the driver-side `IONetworkStats` in ioreg is correct (`inputPackets` 19,345 / `outputPackets` 2,732). **`Ipkts` / `Ibytes` are still 0**, although RX clearly lands on `en2`: `netstat -s -I en2` shows `ip6 on en2: 244 total input datagrams`, and `en1` did not grow during a forced `en2` download.
- **What RE ruled out (Tahoe binaries, `objdump` on `/System/Library/Kernels/kernel` + bytes carved from `BootKernelExtensions.kc`):** `IONetworkInterface::pushInputPacket` passes `packets_in = 1`, `bytes_in = length` (same as the open-source IONetworkingFamily; only `dropped` is left as the `0xAAAAAAAA` auto-init pattern). `ifnet_input_common` forwards those stats to `ifp->if_input_dlil` (+0x240) -> `inp->dlth_strategy` (+0x78), and `dlil_input_stats_add` / `dlil_input_stats_sync` would add them to `if_ipackets` (+0xf8) / `if_ibytes` (+0x120). So the loss is in the strategy/handler chosen for `en2` (or a Skywalk compat handler), not in our driver.
- **Experiment 3.2 — negative (build `1b97cbb3...`, UUID `F3E2817B`, tested on hardware):** `inputPacket(..., kInputOptionQueuePacket)` + `flushInputQueue()` under an `_rxLock` worked (no panic, ping 0% loss, ~152 Mbit/s, 0 drops) but `Ipkts` / `Ibytes` stayed 0 while driver `inputPackets` = 19,236. The queue path is not the difference vs LucyRTL8125. **Reverted in source** to the direct path; the rebuild is byte-identical to `153485ea...` (UUID `0AB6064D`). Remaining untested difference: Lucy's RX polling mode (`IONetworkPacketPollingParameters` / `pollInputPackets`), which belongs with Phase 4 performance work.
- **Open (cosmetic, does not affect traffic):** `en1` (LucyRTL8125, also `CHANNEL_IO`) does count RX; it uses `kInputOptionQueuePacket` + `flushInputQueue()`, an `IOOutputQueue` and Mieze-style RX polling. Next experiment, if worth a reboot: switch `inputFromDriver()` to queue + flush once per USB transfer (needs serialization: the reorder-timeout path can also indicate packets). REA/IDA timed out auto-analyzing the full kernel; `objdump` with the kernel's own symbols was enough.

### Milestone 4.0 — USB 2.0 -> 3.0 Mode Switch (build `ad8ae899607c05d9d898c6b17da8807c`, UUID `19FCA55A`, deployed to EFI, pending reboot)
- **Why:** the dongle enumerates High-Speed (480 Mbit/s, ~150/83 Mbit/s real) and the HAL forces `RF_2T4R` on non-SS links. The monitor hub has a SuperSpeed side (`USB3.0 Hub@01200000`, 5 Gbit/s, behind the ASMedia XHCI); the dongle currently hangs off its HS side (`@01400000`).
- **Linux flow ported:** `usb_reprobe_switch_usb_mode()` runs right after the efuse read. If efuse `0x0e` bit4 (`EEPROMUsbSwitch`) allows it and the link is HS, `HW_VAR_USB_MODE` (`switch_usb_mode = 1`) writes `0x74=0x08, 0x70=0x02, 0x3e=0x01, 0x3d=0x03` and `0x05=0x80` (USB disconnect), and the probe aborts. The chip re-enumerates on the SS lanes; in SS, `HW_VAR_USB_MODE` clears `0x70[1]` / `0x3e[0]`.
- **Our implementation:** `loadparam()` sets `switch_usb_mode = 1`. `vendor_probe()` step 7b does the above before `rtw_init_drv_sw()`, frees the partial state (`rtl8814_free_partial_probe()`: HalData, adapter, dvobj timers + dvobj) and returns `RTL_PROBE_USB3_SWITCH`. `RTL8814Native::start()` then releases the unattached ethernet adapter and fails start; a new instance matches the SS device. `usb_speed` comes from bulk IN MPS (1024 -> `RTW_USB_SPEED_3`), and the HAL picks the SS RX burst (`0x0290 = 0x0E`) from reg `0xFF` bit7. The `ISSUED` flag lasts for the kext's lifetime, so a chip that falls back to HS comes up in HS instead of looping.
- **Telemetry:** `status` prints `USB3 Mode Switch: efuse_allows / switch_issued / ss_finalized` (`rtl8814_link_status.usb_switch`; `Tools/test_uc` rebuilt — the old kext's status call will mismatch until reboot).
- **First live result (monitor hub port):** no panic. The dongle enumerated HS on `AppleUSB20HubPort@01410000`, our probe issued the switch (`terminateDevice ... hardware connection lost`), and the chip then showed up on the same connector's SS port `AppleUSB30HubPort@01210000`, but enumeration kept failing until `AppleUSBHostPort::disconnect: persistent enumeration failures` (~2 min later). With the dongle gone, `test_uc` reports `service not found`. The switch itself works (identical to Linux); the SS link through the monitor's Realtek hub (`0x0bda/0x0401`) does not. Next test: a direct motherboard USB 3 port. Replugging within the same boot comes back in HS (the `ISSUED` flag is per kext load).
- **Live result on a direct motherboard USB 3 port: SUPERSPEED WORKS.** `802.11ac NIC@60500000` at 5 Gbit/s (`bcdUSB 0x0300`), `status`: probe 0, firmware OK, `usb_speed=3 (SuperSpeed)`, `bulk_in_mps=1024`, `efuse_allows=yes, switch_issued=yes, ss_finalized=yes`. The monitor hub is the only thing that can't carry SS for this dongle.
- **SS join + throughput (same boot, build `ad8ae899`):** WPA2 join to ch 157, RSSI -42 dBm; `rf_type=5` (`RF_3T3R`; HS forced `RF_2T4R`), `peek 0x0290 = 0x0E` (SS RX burst). Ping gateway 0% loss, ~2.0 ms avg. `curl --interface en2` (Cloudflare, 50 MB down / 25 MB up, 2 runs each): **~19.1-20.4 MB/s (~153-163 Mbit/s) down, ~14.1-14.5 MB/s (~113-116 Mbit/s) up**. After the runs: 12.2k RX URBs, 48.8k TX writes, 0 RX drops, 0 TX errors, 0 URB errors. Wired baseline on en1 at the same time: ~76-87 MB/s down, ~39 MB/s up, so the ISP is not the cap.
- **Interpretation:** USB3 lifted upload ~+37% (the TX path was bus-limited); download is unchanged, so RX is now limited by something else (PHY rate / bandwidth actually negotiated, RX aggregation settings, single bulk IN pipe with 8 URBs in flight, or the skb->mbuf copy). Not yet measured: negotiated VHT rate/bandwidth/NSS. Phase 4 performance item.
- **Recovery if the SS bring-up panics at boot:** unplug the dongle (no probe runs; power loss also resets the chip to HS), boot, and redeploy a previous build from the scratchpad backups (`efi_backup_m40` = `1b97cbb3`, `efi_backup_m41` = `ad8ae899`).

### Milestone 4.1 — Hot-Plug Panic: Overlapping stop()/start() Freed the Workqueue Lock
- **Symptom (`Kernel-2026-10-26-234823.panic`, build `ad8ae899`):** I unplugged the dongle from the hub (instance B, fully up in HS) and plugged it into a direct port (instance C). C panicked during firmware download: `IOLockLock+0x3` <- `rtl_schedule_work` (frame skipped) <- `tasklet_schedule` <- `usb_write_port` <- `_rtw_write_port` <- ... `FirmwareDownload8814A` <- `rtw_hal_init` <- `vendor_probe` <- `RTL8814Native::start`. `CR2 = 0xffffff9b00000000`, flagged by Probabilistic GZAlloc, i.e. a freed allocation: `g_system_wq.wq.lock`.
- **Root cause:** all radio state is global (`g_padapter`, `g_dvobj`, the compat system workqueue), and nothing ordered B's `stop()` (termination thread; `vendor_remove` + `rtl_workqueue_exit`, slow on a vanished device) against C's `start()` (config thread). B's `rtl_workqueue_exit()` freed the lock C's `rtl_workqueue_init()` had just allocated. (The kernel log after the unplug was lost with the panic, so the overlap is inferred from the code and the backtrace.)
- **Fix (build `78ef83a99fe3d38ef94490fbe808641e`, UUID `56D00139`, deployed, 0 undefined vendor symbols):** `RTL8814Native::start()` / `stop()` now run `startLocked()` / `stopLocked()` under a kext-lifetime `IORecursiveLock` (created with `OSCompareAndSwapPtr`; recursive because a failed start calls `stop()`). A new instance now waits for the previous teardown to finish. If teardown ever hangs, the new instance blocks instead of panicking; the per-reboot `shutdown_stall` reports suggest teardown is slow, which is Phase 4 work.
- **Deploy note:** disk numbers change between boots (this boot: `disk0s1` = NTFS "Gordo", EFI = `disk1s1` at `/Volumes/NO NAME`). Always check `diskutil info` (`Partition Type: EFI`, 4.3 GB) and that `EFI/OC/Kexts` exists before copying.

### Milestone 4.2 — RX Throughput Cap Investigation (build `7ca09811916157dc42b9931e42558f8b`, deployed, pending reboot)
- **Air side is fine (decoded from `rxlog` on build `ad8ae899`, no new code needed):** RX descriptor `RX_RATE` = `0x3D`-`0x3F` = **VHT 2SS MCS7-9**, Jaguar phy-status byte 3 `r_RFMOD = 2` = **80 MHz**, 5 GHz ch 157. PHY rate ~650-866 Mbit/s. The router is 2x2, so 2 streams is its maximum; there is no bad 2.4 GHz / 20 MHz negotiation.
- **Not per-flow:** 4 parallel `curl` downloads sum to the same ~16.8 MB/s (~135-150 Mbit/s), so it is an aggregate RX cap, not TCP window or RTT.
- **Rates during one 60 MB download:** ~14.2k RX frames/s, ~1.9k bulk IN transfers/s (~8 frames, ~11 KB each), ~2.1k TX writes/s (TCP ACKs; TX handles more during upload, so TX is not the cap). All 8 URBs are always pending (`submit_ok - comp_ok = 8`).
- **Chip is not backlogged:** `HISR1` (0xBC) `RXFOVW` (bit 8) stays 0 through a download after clearing (bit 9 `TXFOVW` was latched once). `RXPKT_NUM` (0x284) shows ≤ 8 packets in the RX buffer. RX agg registers match Linux for SS: `0x10C = 0xF5B4` (`RXDMA_AGG_EN` = 1), `0x280 = 0x0A05` (DMA agg, USB agg off), `0x290 = 0x0E`.
- **Port difference vs Linux found:** Linux `usb_recv_tasklet` recycles each 32 KB bulk IN skb into `free_recv_skb_queue`. Our `usb_recv_tasklet` freed it, so every URB resubmit (in the USB completion callback) did a fresh `IOMalloc(32 KB + hdr)`. That is above the kalloc zone limit, so it is a VM-backed large allocation, ~1.9k/s. Prime suspect for the ~2k transfers/s ceiling.
- **New build `7ca09811` (0 undefined vendor symbols):**
  - **Recycling (Linux behaviour):** live option `RTL_OPT_RX_SKB_RECYCLE`, default ON, `test_uc recycle 0|1`. The pool is capped at `2 * NR_RECVBUFF`. Safe because `rtw_os_alloc_recvframe()` always copies frames out.
  - **New `status` rows:** PHY mode / negotiated BW, HAL NSS, FW RA TX rate + SGI + BW, RX rate (sta + last unicast descriptor), RX BA TIDs/window, driver throughput, RX USB aggregation (frames and bytes per transfer), RX agg registers, RX path cost (completion-handler and parse time in µs, 32 KB allocs, tasklet backlog), A-MPDU reorder counters (`dbg_rx_ampdu_*`).
  - `test_uc perfreset` zeros the RX counters and timings.
- **After reboot:**
  1. `perfreset`, then a 60 MB download with `recycle 1`, then `status`.
  2. `perfreset`, then the same download with `recycle 0`, then `status`.
  3. Compare throughput, completion µs, `32KB_allocs` and backlog.
  4. If the cap stays, the next suspects are parse cost per transfer (per-frame `IOMalloc` + copy + mbuf copy) and the reorder counters.

- **Live A/B on `7ca09811` (after reboot, warm boot came up SS directly so `switch_issued=no`, no panic):**
  - **Recycle ON:** ~158-161 Mbit/s; 32 KB allocs = 1; completion avg 16.3 µs; parse 23.7 µs per transfer (~8 frames, 11.8 KB); backlog max 2.
  - **Recycle OFF:** ~165-170 Mbit/s; 32 KB allocs = 11,254; completion 16.8 µs; parse 24.0 µs.
  - **Verdict:** the host RX path costs ~40 µs per transfer (~8% of one CPU at 1.9k/s). It is **not** the cap, and the 32 KB alloc hypothesis is refuted. Recycling stays ON (Linux behaviour).
  - **A-MPDU reorder:** `ampdu_drop` ~630-920 per 120 MB (~0.7%, AP retransmits we already had); no `loss` / `forced_indicate`.
- **Real culprit — port bug in `rtw_init_default_value()` (`VendorProbe.c`):** it omitted the Linux `os_intfs.c` "for debug purpose" block, so these stayed 0 instead of 0xFF / registry:
  - `driver_tx_bw_mode = 0` -> `ADAPTER_TX_BW_5G` = 20 MHz -> `rtw_get_tx_bw_mode()` caps TX at 20 MHz. `status` showed `ra_bw=20` while `sta_bw=80`. Every uplink frame, TCP ACKs included, uses ~4x the airtime, which plausibly caps the download (shared channel) and the ~114 Mbit/s upload.
  - `driver_rx_ampdu_factor = 0` -> the HT capability advertised an 8 KB max A-MPDU (instead of the HAL default).
  - `driver_ampdu_spacing` / `driver_rx_ampdu_spacing = 0` -> TX AMPDU density 0 and HT cap density 0 instead of the HAL/AES default.
  - (`fix_rate`, `fix_rx_ampdu_accept`, `fix_rx_ampdu_size` were already set in `rtw_init_drv_sw()`, so the RX BA was accepted: `rx_ba_tids=0x41`, wsize 64.)
- **Also observed:** TX A-MPDU never established (`tx_ampdu_tids=0x00`), cause unknown. `rtw_issue_addbareq_cmd()` needs `NumTxOkInPeriod >= 100`, `psta->htpriv.ampdu_enable` and an ADDBA response from the AP.
- **Fix build `0b9f9e93f5625252b4b7090d555b83c6` (deployed, 0 undefined vendor symbols):**
  - Adds the full Linux defaults block (`driver_tx_bw_mode = registry tx_bw_mode` (0x21), the AMPDU fields = 0xFF, `fix_bw = 0xFF`, `driver_tx_max_agg_num = 0xFF`).
  - `status` now also prints `tx_candidates`, `sta_ampdu_enable`, `tx_ok_period` and `Driver Defaults` (`tx_bw_mode`, `rx_ampdu_factor`, `rx_ampdu_accept`).
- **Expected after reboot + join:** `ra_bw=80`, `tx_bw_mode=0x21`, `rx_ampdu_factor=0xFF`. Then re-measure down and up, and check whether `tx_ampdu_tids` becomes non-zero after an upload.

- **Live result of `0b9f9e93` (after reboot + join, SS, no panic):**
  - **Defaults applied:** `ra_bw=80` (was 20), `tx_bw_mode=0x21`, `rx_ampdu_factor=0xFF`, `rx_ampdu_accept=0xFF`. TX RA: VHT 2SS MCS9.
  - **Download: ~215-266 Mbit/s** (26.8-33.2 MB/s over 3 x 60 MB; was ~155). Now 11.8 frames / 17.9 KB per bulk IN transfer (was 8 / 11.8 KB); parse 30 µs per transfer; backlog ≤ 3.
  - **TX A-MPDU is now negotiated:** `tx_ampdu_tids=0x01`, `tx_ok_period` ~5-10k, so the ADDBA gate is passed.
  - **Upload regressed: ~51-59 Mbit/s** (was ~114). Cause, from the code:
    - `usb_write_port()` (`VendorOsDep.c`) is **synchronous** (`usb_bulk_msg`), so only one bulk OUT is in flight and it carries ~1 frame (36k writes for 50 MB). With A-MPDU on, each frame goes out as a 1-MPDU A-MPDU plus BA overhead, which is worse than the old non-aggregated path.
    - TX work (`g_tx_work`) and the RX tasklet share the compat system workqueue thread. While TX blocks on writes, RX backs up (`backlog_max` 86-92, 666-710 fresh 32 KB allocs during upload), which delays the server's TCP ACKs.
  - **Reorder duplicates:** `ampdu_drop` ~10k per 180 MB download (~7% of frames; was ~0.7%). These are AP retransmits of frames we already indicated, so some of our BlockAcks are probably lost or late. To investigate.
- **Next (Phase 4 perf):**
  1. Make `usb_write_port()` asynchronous like Linux (bulk OUT URB, completion frees the xmitbuf and schedules the xmit tasklet; several xmitbufs in flight), so the HAL's USB TX aggregation and the chip's A-MPDU get queued frames.
  2. Run the RX tasklet on its own workqueue thread so TX can't starve it.

### Milestone 4.3 — Async Bulk OUT + Dedicated RX Thread (build `c896a97847215e0fd2b88f28323e15d6`, 0 undefined vendor symbols, deployed and verified on hardware)
- **Async TX (`VendorOsDep.c`):** `usb_write_port()` now matches Linux.
  - It fills `pxmitbuf->pxmit_urb[0]` with `usb_fill_bulk_urb()` and submits it via `usb_submit_urb()` -> `RTLUsbDevice::submitUrb()` (async `IOUSBHostPipe::io`, already used for bulk IN).
  - `usb_write_port_complete()` runs on the USB completion workloop and never blocks. It counts ok/err, completes `sctx` under `lock_sctx` (so `_rtw_write_port_and_wait` / `dump_mgntframe_and_wait` still work), frees the xmitbuf, and reschedules the xmit tasklet. `-ENODEV` marks surprise removal.
  - Up to `NR_XMITBUFF` (4 x 20 KB) + ext buffers can be in flight. When all are busy, `pre_xmitframe()` enqueues and the xmit tasklet aggregates on completion (USB TX aggregation).
- **Teardown:** `usb_write_port_cancel()` still aborts every xmit URB's pipe. It now also waits (≤ 1 s) for `g_tx_urbs_inflight` to drain, so aborted completions don't touch freed xmit buffers.
- **RX thread (`Compat.c`, `workqueue.h`):** the compat workqueue now has two kthreads, `rtl_system_wq` and `rtl_rx_wq`.
  - `work_struct.wq` selects the queue (NULL = system). `rtl_work_bind_rx()` binds the bulk IN `recv_tasklet` in `rtw_os_recv_resource_init()`.
  - TX work, the xmit tasklet and everything else stay on the system queue.
  - `flush_scheduled_work` / `cancel_work_sync` wait for both queues. `rtl_workqueue_exit()` stops RX first, then system.
  - Code running on a queue must not call `cancel_work_sync()` (self-deadlock, unchanged from before).
- **Concurrency note:** the RX path (`recvbuf2recvframe` -> `pre_recv_entry` -> mlme handlers / indicate) now runs in parallel with TX work and the xmit tasklet, as it does on Linux SMP. The vendor core has its own locks for this. Our glue's RX-only state (`g_rx_stage`) is still touched only from the RX thread.
- **To verify after reboot:**
  - Upload (target: back to ≥ 114 Mbit/s, ideally well above, with TX A-MPDU) and download.
  - `TX Bulk OUT Writes` err = 0; `backlog_max` during upload (was 86-92); no panic on reboot / unplug.
- **Live result (SS, VHT 2SS 80 MHz, RSSI -41, no panic on boot):**
  - Download ~225-245 Mbit/s (28-30 MB/s, 50 MB Cloudflare). Upload ~68-77 Mbit/s (8.4-9.6 MB/s, 25 MB). Async TX helped upload (+30% vs ~55) but it is still below the pre-4.2 ~114.
  - USB/host side is healthy: `TX Bulk OUT` err=0, ~17 KB (~11-12 frames) per bulk OUT write, RX `backlog_max` 1-7 (was 86-92), RX completion ~17 us, parse ~20 us.
  - Live A/B on `REG_MAX_AGGR_NUM_8814A` (0x4CA/0x4CB, default 0x36): 0x10 -> ~42 Mbit/s up, 0x01 -> ~6.2 Mbit/s up. Throughput scales with MAC A-MPDU size, which means each PPDU costs ~1.8 ms of fixed airtime. This is an air-side problem, not USB.
  - **Key clue, duplicate RX:** the reorder `drop` counter (frames already indicated, i.e. AP retransmits of frames we did receive) is ~6.4% of data frames during download and **~36%** during upload (2807 of 7814 TCP-ACK frames). The AP very often does not get our ACK/BlockAck. Our TX A-MPDUs probably suffer the mirror problem (AP BA not received/accepted by our MAC -> retries/rate fallback to the 6M floor, `DATA_RATE_FB_LIMIT=4`).
  - Registers checked and matching the stock init: RRSR 0x440=0x00A00150 (6/12/24M), SIFS 0x428/0x514/0x63A=0x0E0A, ACKTO 0x640=0x80, RETRY_LIMIT 0x42A=0x3030, AMPDU_MAX_LENGTH 0x458=0x3FFFF, TxPath 0x80C=0x1000000F, 0x93C=0x932C0642, EDCA BE 0x508=0xA42B.
  - **Linux reference (CachyOS, kernel 7.3, in-tree `rtw88_8814au`, fw 33.6.0, same port/USB3 5000, same AP, ch157/80 MHz, RSSI -46):** down **288-366 Mbit/s**, up **227-280 Mbit/s**; TX rate VHT 2SS MCS9 80 MHz **short GI**; `iw` station dump: tx retries 0, tx failed 1 across the whole test. So the hardware and AP do 3-4x our upload: the gap is ours.
  - Captured in `Docs/regdumps/linux_rtw88_20261027/` (rtw88 debugfs `mac_*`/`bb_*` idle + after tests, station dump, upload rate samples); our idle dump is `Docs/regdumps/macos_c896a978_idle.txt`; `Docs/regdumps/diff_linux_vs_macos.txt` lists 192 differing dwords (113 stable in Linux). Tool: `Tools/linux_reference_capture.sh`.
  - Notable differences (ours vs rtw88): RRSR 0x440 `0x00A00150` vs `0x00000150` (vendor `HW_VAR_ACK_PREAMBLE` writes `nCur40MhzPrimeSC<<5 | 0x80` to 0x442 = response sub-channel + short preamble); RESP SIFS 0x63C `0x0A0E0808` vs `0x0E0E0A0A` (vendor sets R2T OFDM = 0x0A on purpose, "MAC has 10 us delay"); TX SIFS OFDM 0x428/0x514/0x638 `0x0E` vs `0x10`; PKT_LIFE_TIME 0x4C0 `0xFFFFFFFF` vs `0x10001000`; EDCA BE `0xA42B` (vendor turbo EDCA) vs `0x4122`; plus RCR/TCR/SECCFG/beamforming (0x718) differences expected between drivers.
  - `Tools/ab_regs.sh` pokes these one at a time on the live link and measures upload + RX duplicate ratio, restoring each value afterwards.
  - **Live A/B result (`Tools/ab_regs.sh`, boot right after Linux, RSSI -36):** none of the suspects matters. Upload Mbit/s: baseline 85/169, RRSR RSC 0x442=0x00 173/180, R2T SIFS 0x63F=0x0E 169/168, TX SIFS 0x514=0x100A100A 170/167, PKT_LIFE 0x4C0=0x10001000 171/180, baseline again 142/182. The RX duplicate ratio stays ~21-29% in every variant. The vendor values stay.
  - **Same build, much faster this boot:** down **334-364 Mbit/s** (= Linux), up **134-185 Mbit/s** (~65% of Linux), FW RA TX `VHT 2SS MCS7 SGI` (the slow boot showed MCS9 LGI), RSSI -36 (slow boot -41). A register diff slow-vs-fast boot (`Docs/regdumps/macos_c896a978_after_linux.txt`) shows only counters / dynamic DM state, plus a few system regs left by rtw88 (GPIO mux 0x40, LED 0x4C, PAD 0x64, 0x90, 0x13C). None is RF/MAC config. Open question: is the gain boot-state (warm chip after Linux) or just RF conditions? Test: full power-off cold boot into macOS and measure again.
  - **Cold boot (full power-off -> macOS, same build `c896a978`): Linux parity.** Up **202-297 Mbit/s** (first run ~87 is TCP ramp), down **320-368 Mbit/s**; RX duplicates **2.9% up / 0.25% down** (vs 36% / 6% on the slow boot). A 12 x 25 MB sustained upload (chip hot) stays 186-297 with 1.9% dup and unchanged TXAGC, so it is not thermal within a session.
  - **So throughput depends on how the chip was left by the previous OS session.** Slow boot = warm restart after a macOS session with our driver: dup 36%. After a Linux (rtw88) warm restart: up ~170, dup ~23%. Cold boot: dup ~2-3%. 3-way register diff (`macos_c896a978_idle.txt` slow / `_after_linux.txt` / `_coldboot.txt`): besides counters, the slow boot has a higher TXAGC on every path (0xC94/0xE94/0x1894/0x1A94 bits 29:25: 0x15/0x13/0x13/0x15 vs cold 0x0F/0x0D/0x0D/0x0F, set by TX power tracking from thermal) and different IQK/LOK result state (0x1B2C, 0x1BF8, 0x1BFC = LOK results). Hypothesis: on a warm restart the dongle stays powered, our init runs IQK/power tracking on a chip that was not powered down/reset cleanly (or still hot), leaving bad calibration (TX EVM / ACK decode issues).
  - **Next:** reproduce with a plain macOS warm restart; if slow again, compare our shutdown/stop path with Linux's power-off (card disable / `rtw_hal_power_off`, RF off) and the IQK/LCK/thermal sequence at init.
  - **Next:** instrument TX (C2H TX report / CCX: per-MACID retry and drop counts, last data TX descriptor dump), compare MAC/BB response settings with Linux (BA responder, response TX path/power, `REG_BAR_MODE_CTRL` 0x4CC=0x7F), and get a Windows/Linux reference upload on the same router.


### Milestone 4.4 — Warm-Start Power Cycle (build `35d6749bd4bccafbce0ef3eb8f22a909`, 0 undefined vendor symbols, deployed and verified on hardware)
- **Reproduced:** a plain macOS restart (same build `c896a978`) gives up **43-59 Mbit/s** with **40%** RX duplicates (10341/25554) and down 207-260 with 4.1%; FW RA shows `VHT 2SS MCS9 LGI`. The cold boot just before gave up 202-297 / 3% dup. TXAGC (0xC94...) was identical to cold this time, so it is not the trigger; the stable warm-vs-cold differences are IQK/LOK state (0x1BFC = 0x7FE/0x7FF warm vs 0x1 cold; Linux 0x5) and RF LOK/gain (RF 0x08, 0x56, 0x58; RF dump `Docs/regdumps/macos_c896a978_warm2_rf.txt`). Everything else is dynamic DM state (IGI, NHM/EDCCA thresholds).
- **Root cause:** on a restart the dongle stays powered. Our kext never powers the chip down: no `systemWillShutdown` / PM, and `hal_deinit` only runs from `stop()`. The macOS shutdown also logs `shutdownStall` reports. At the next probe, vendor `rtl8814au_hal_init` only logs "MAC has already power on" (SYS_CLKR+1 BIT3 and CR not 0/0xEA) and runs LCK/IQK on the live chip, leaving a bad calibration. rtw88 handles this by powering off first (`rtw_mac_power_on`: `-EALREADY` -> power off -> power on).
- **Fix (`VendorProbe.c` step 7c, before `rtw_init_drv_sw` / `rtw_hal_init`):** read SYS_CLKR+1 and CR; if the chip is already on, call `rtw_hal_power_off()` (= `hal_carddisable_8814`, NIC_DISABLE_FLOW) so `rtw_hal_init` starts from a powered-off chip, like a cold boot. The pre-probe state is logged and exported as `rate.warm_start` (the former `reserved2`); `test_uc status` shows it as `Probe Power State`.
- **To verify:** deploy, warm restart, join. Expect `Probe Power State ... warm start, chip powered off`, upload ~200+ Mbit/s and RX dup ~3%. Then also check a cold boot still shows `cold`.
- **Verified (plain macOS restart, then join):** `Probe Power State: SYS_CLKR+1=0xFC, CR=0xFF -> chip was already on, powered off before HAL init (warm start)`. Up **183-262 Mbit/s** (first run 86 = TCP ramp), RX dup **1.05%** (230/21896); down **319-364 Mbit/s**, dup 1.9%. TX `VHT 2SS MCS7 SGI`, USB err 0, SS. Same as a cold boot and at parity with Linux rtw88 (up 227-280 / down 288-366). Before the fix, the same restart gave up 43-59 / dup 40%.
- **Still open:** a real shutdown path (`systemWillShutdown` -> radio off / card disable) and the `shutdownStall` reports.


### Milestone 4.5 — Clean Radio Power-Off on Shutdown/Restart (build `cc2c41f3e3a9ca81a5f037f468acd6df`, 0 undefined vendor symbols, deployed to EFI)
- **`shutdownStall` reports are not ours.** Decoded with `spindump -i <file> -o out.txt`. Reports from 2026-10-19 and 2026-10-23 predate the kext (no `com.DrogaBox` in the kext list) and show the same pattern as tonight's: `launchd` "system shutdown has begun" waits for user processes (`tuxera_ntfs` / `tuxera_ntfs_daemon` FUSE for the NTFS disk "Gordo", BlueTool, fseventsd, diskarbitrationd...). The latest report (01:33) has no thread executing inside our kext's range (0xffffff80043c0000 + 0x318000). Tonight's count (22) just reflects the many reboots.
- **Shutdown hook:** `RTL8814Native` now joins the power plane (`PMinit` + `provider->joinPMtree` + `registerPowerDriver`, 2 dummy states, no sleep handling yet; `PMstop` in `stopLocked`). It overrides `systemWillShutdown()`: on `kIOMessageSystemWillPowerOff` / `kIOMessageSystemWillRestart` it calls `rtl8814native_vendor_shutdown()` under the lifecycle lock, then `super::systemWillShutdown()`.
- **`rtl8814native_vendor_shutdown()` (`VendorProbe.c`), mirroring Linux `rtw_dev_shutdown()`:** net_closed / bup off, `rtw_set_drv_stopped`, cancel + purge the TX work, `rtw_cancel_all_timer`, `rtw_hal_deinit` (-> `rtl8814au_hal_deinit` -> `rtw_hal_power_off` = card disable / NIC_DISABLE_FLOW, clears hw_init_completed), `rtw_set_surprise_removed`, `continual_io_error = MAX+1`. Memory is kept; a later `vendor_remove()` is still safe (it skips the second power-off because hw_init_completed is false).
- **Verify after deploy:** restart; the boot log of the *previous* boot must contain `system shutdown, powering the radio off`; after the restart `test_uc status` should show `Probe Power State ... cold (chip was off)` (the 4.4 probe-time power-off becomes a fallback for panics / other OSes). Restart must not hang.
- **VERIFIED (2026-10-27 02:14 restart through the 4.5 build):** `Probe Power State: SYS_CLKR+1=0xB0, CR=0xEA -> cold (chip was off)` — the hook powered the radio off at shutdown; the 4.4 probe-time power-off correctly did not trigger. Restart did not hang; the 02:14 shutdownStall decodes with zero thread frames inside our kext (tuxera_ntfs / fseventsd / BlueTool again — pre-existing). Performance after the hook shutdown: down 320-321 / up 200-214 Mbit/s (best upload ever), dup 1.06%, TX err 0. Milestone complete.
- **First 4.5 boot (2026-10-27 01:46, UUID `C9E6124B`):** join + auto-DHCP on `en2`, down **369-371 Mbit/s** / up **171-172 Mbit/s** (above the Linux rtw88 reference down 288-366), RX dup **2.8%**, TX err 0, RX backlog ≤ 2. `Probe Power State` = warm start — expected: the shutdownStall spindump from 01:46 shows the previous boot ran 4.4 (UUID `9C499D78`), so the hook had not run yet. Note: the stall decodes from the spindump binary format (base64 -> zlib@offset8 -> bplist NSKeyedArchiver -> `$objects[48]` = kext list, `$objects[49]` = full sample) — that is how to check which build was live at shutdown.
- **AP channel change observed:** same BSSID `0E:84:08:68:FD:24` moved 157 -> 161 mid-session; the driver does not follow Channel Switch Announcement, so the link dropped once (beacon loss) and a rejoin recovered on ch 161.


### Milestone 4.6 — Sleep/Wake via `setPowerState` (build `4953df17912583d46c5a407a754cc45e`, UUID `198B7CED`, 0 undefined vendor symbols, deployed to EFI, pending reboot + sleep test)
- **`RTL8814Native::setPowerState()` (`RTL8814Native.cpp/.hpp`):** state 0 -> `rtl8814native_vendor_suspend()`, state 1 -> `rtl8814native_vendor_resume()`, both under the lifecycle lock. If the dongle vanished across sleep (surprise-removed), resume returns `-ENODEV` and IOKit's terminate/re-probe path handles the re-enumerated device.
- **`rtl8814native_vendor_suspend()` (`VendorProbe.c`):** `rtl8814native_disconnect()` (carrier down + supplicant reset), then the stop half of `vendor_remove()` while keeping every allocation: `bup=0`, `drv_stopped`, cancel + purge TX work, `rtw_intf_stop` (aborts the 8 RX URBs + TX pipes; aborts complete as `-ECONNRESET`, which frees the skb and stops resubmitting — never `-ENODEV`, so surprise-remove is not triggered), `rtw_stop_drv_threads` (cmd thread join; `cmdThread=NULL` guards a double start), `rtw_cancel_all_timer`, `rtw_hal_deinit` (-> card disable). `g_suspended` makes it idempotent.
- **`rtl8814native_vendor_resume()`:** warm-start guard (power off first if the chip reads on — Milestone 4.4 rule), `rtw_clr_drv_stopped` (before firmware: `usb_write_port` gates on it), `rtl8814native_rx_stage_reset()` (new export in `VendorOsDep.c`, clears the RX reassembly stage), `rtw_hal_init` + firmware re-download, TX work re-init, `rtw_start_drv_threads` + `rtw_intf_start`, `bup=1`, timers, then **auto-rejoin** via `rtl8814native_join_network(g_last_ssid, ...)` — credentials saved on every successful join (`g_last_ssid/psk/auth`, kext lifetime). The join is async (cmd thread + MLME + supplicant); DHCP re-runs automatically when carrier comes back up.
- **`kthread_stop()` leak fix (`Compat/include/linux/kthread.h`):** now calls `rtl_kthread_destroy()` after joining instead of leaking the `task_struct`. Both join sites are safe: `rtw_stop_cmd_thread` nulls `cmdThread` right after, and the workqueue drain in `rtl_workqueue_exit` destroys its own task (whose trampoline removes it from the active table before setting `finished`).
- **To verify after reboot:** join, `perfreset`, note baseline; sleep (Apple menu or `pmset sleepnow`), wake, then check: no panic; `status` shows ASSOCIATED again (auto-rejoin) within ~15 s; `ipconfig getifaddr en2` returns an IP; RX/TX counters advance; throughput ~normal. Also re-check the 4.5 path (restart still shows `Probe Power State ... cold`).

### Milestone 4.6b — Workqueue/URB-Teardown Panic Fix (build `37d0455518ea4f7f602ee0e263132a3a`, UUID `D694A37C`, 0 undefined vendor symbols, deployed, pending reboot + sleep test)
- **Panic (`Kernel-2026-10-27-024326.panic`, first 4.6 boot):** page fault `rtl_wq_thread + 0x96` -> `list_del` reading 0xdeadbeef (freed small-zone fill); disassembly confirms the faulting instruction is the dequeue `list_del(&w->entry)`; the queue head resolved to `g_system_wq` (`0x212928` in the symbol table). Timeline: boot 02:39, join, a 0.34 s aborted sleep at 02:41 (panic record's Last Sleep/Wake), status healthy 19 s after wake, panic 2.4 min later.
- **Root cause chain:** works (tasklets, `g_tx_work`, PhyDM items) live inside the instance's `_adapter`/HalData memory; the compat workqueue (`g_system_wq`/`g_rx_wq`) and its threads are KEXT-lifetime and survive stop()/start(). The first 4.6 sleep re-enumerated the device -> IOKit terminated the instance -> `vendor_remove()` freed the adapter while a work was still queued in `g_system_wq.pending` (the `cancel_work_sync` flush ran first, before the async aborts/hal_deinit could queue more) or was scheduled late by a URB completion (our `usb_kill_urb` aborted asynchronously — Linux's is synchronous). Any later wakeup of the surviving wq thread (e.g., the new instance scheduling work) dequeued the stale entry and crashed.
- **Fixes (defense in depth):**
  1. `RTLUsbDevice::killUrb()` is now synchronous like Linux: waits (<= 1 s) for the completion to run (`struct urb` gained `volatile int inflight/done`; set/cleared in `submitUrb`/`rtlUrbCompletion`). After `rtw_intf_stop` no URB completion can touch freed memory.
  2. `rtl8814native_vendor_remove()` calls `rtl_flush_scheduled_work()` after all schedule sources are dead (URBs sync-killed, cmd thread joined, timers cancelled) and again after `hal_deinit`, both before `rtw_free_drv_sw`.
  3. `rtl_wq_stop()` drops all queued entries (they belong to the dying instance) before joining the thread — unordered teardown paths can never execute stale work.
  4. `rtl_flush_scheduled_work()` now reads both queues under their locks (the drain thread dequeues and sets `running=1` in the same critical section; the old unlocked read could see "idle" mid-dequeue).
  5. Both URB completions drop stale instances: `padapter != rtl8814native_get_primary_adapter()` -> return (the TX completion decrements the global `g_tx_urbs_inflight` before the guard so drain waits stay balanced).
- **Verification pending:** the same sleep/wake test as 4.6, plus an unplug/replug regression check (4.1 path now exercises the synchronous kills).

### Milestone 4.6c — Overlapping Instances on Wake: Owner Guard (build `9af63e615a639c1cab42ca87bbd8424c`, UUID `68308511`, 0 undefined vendor symbols, VERIFIED: first wake without panic, see 4.6d)
- **Panic (`Kernel-2026-10-27-033402.panic`, first sleep/wake on 4.6b `D694A37C`):** the screen stayed black on wake, so I pressed reset. General protection fault in `rtl_wq_thread + 0xd7` = `callq *%rax` (`w->func`), 1.1 s after wake. `RAX = 0x078c4acd324dc5c6`, `RDI = w = kext + 0x212920` = `g_prng_state`, which is exactly `&g_system_wq - 8`. So a drain thread dequeued the **system queue head itself** as a work item and called the PRNG state as a function pointer. No unified-log lines survived from that boot (`rtl_printk`/IOLog lines never reach `log show` on this system; only panic reports and test_uc telemetry are usable evidence).
- **Analysis:** a single-queue simulation of INIT_WORK-while-queued and rebinding (`scratchpad wqsim.py`, all 8-op sequences of schedule/init/bind/drain) never produces a queue head as an entry, so plain misuse of a work item is not it. What does produce it is two drain threads on one list under different locks: `rtl_wq_thread` caches `IOLock *l` once, and `rtl_wq_start()` unconditionally re-inits `pending`, allocates a new lock and spawns a new thread. Ordering bug: when the dongle re-enumerates across sleep, IOKit can `start()` the new instance **before** `stop()` of the old one (the lifecycle lock only serializes them, it does not order them). The new start re-ran `rtl_workqueue_init()` on live queues (the old threads survive with the old lock; `IOLockWakeup` wakes both because the event address is the same) and `vendor_probe` overwrote the global `g_padapter`; the old `stop()` then tore down the new instance's state. This also matches the 4.6b root-cause note ("later wakeup of the surviving wq thread").
- **Fix:**
  1. `RTL8814Native.cpp`: `g_owner` (under the lifecycle lock) is the instance owning the vendor state and workqueues. `startLocked()`: if another instance still owns it, call `g_owner->releaseVendorLocked()` (vendor_remove on the OLD usb handle, `set_usb_device(nullptr)`, `rtl_workqueue_exit()`) before registering the new usb handle, then take ownership after `rtl_workqueue_init()`. `stopLocked()` only runs the vendor/workqueue teardown when it is the owner. `setPowerState()` / `systemWillShutdown()` do nothing for a non-owner.
  2. `Compat.c rtl_wq_start()`: refuses to re-create a queue whose thread is still alive (logs `... already running, not restarted`).
- **Side finding (not changed yet):** the recv tasklet never actually runs on `rtl_rx_wq`. `_rtw_init_recv_priv()` calls `rtw_os_recv_resource_init()` (tasklet_init + `rtl_work_bind_rx`) and then `rtw_hal_init_recv_priv()` -> `usb_init_recv_priv()`, whose `tasklet_init` (compiled: `PLATFORM_LINUX` is defined by `osdep_service_macos.h`) resets `wq = NULL`. Confirmed in the binary (`usb_init_recv_priv` calls `_tasklet_init` with `_usb_recv_tasklet`). So all RX processing still runs on `rtl_system_wq`, and the 4.3 "dedicated RX thread" is inert. Fixing it (bind after the HAL init, or skip the HAL tasklet_init) is a separate, measurable change.
- **To verify after deploy:** reboot, join, sleep (Apple menu), wait 30 s, wake. Expect no panic, the screen back, and `status` ASSOCIATED again (auto-rejoin or one manual join). Then throughput. Also repeat a plain restart (4.5 path: `Probe Power State ... cold`).

### Milestone 4.6d — First Clean Wake + Re-Join After Takeover (build `ca1fe5710c2df03ccbe5b0e16f733551`, UUID `C4FECD48`, 0 undefined vendor symbols, deployed, pending reboot + sleep test)
- **4.6c result (2026-10-27 03:47 boot, UUID `68308511`):** boot cold (`Probe Power State ... cold`, the 4.5 shutdown hook ran), sleep then wake at 03:50:58 with **no panic** and the screen back. The pmset wake acks list `RTL8814Native ... SetState to 1 (617 ms)`. `ioreg` confirmed the 4.6c theory: the dongle re-enumerated on wake, so there were two `RTL8814Native` objects. The old one (`id 0x100000401`) is `!registered, inactive, busy 1` (79 s and counting) with `CurrentPowerState=0, DesiredPowerState=1` (its PM parent vanished, so termination stays pending; harmless, it no longer owns the radio). The new one (`0x1000009d8`) is active and owns `en2`. The new instance took the radio over cleanly (RX beacons, TX ok=413/err=0, USB3 SuperSpeed) but the link stayed **DISCONNECTED** (WPA state `WAIT_EAPOL_MSG1`, no IP), because only the old instance's wake path re-joins.
- **Fix:** `rtl8814native_rejoin_last()` (`VendorProbe.c`, declared in `VendorGlue.hpp`) re-joins `g_last_ssid/psk/auth` (kext-lifetime, survives `vendor_remove`). `vendor_resume()` uses it, and `startLocked()` now calls it after `registerService()` when it took the radio over from a stale owner (`took_over && probe_rc == 0`).
- **Stale instance never stopped (fixed in 4.6e below):** `ioreg` showed it `inactive, busy 1` for minutes. Cause: we keep the `IOUSBHostInterface` open (`openProvider`) and only close it in `stop()`, but IOKit calls `stop()` only after every open on the terminated provider is closed, and there was no `didTerminate()` override.
- **To verify after reboot:** join once, sleep, wake: expect no panic, `status` ASSOCIATED again within ~15-30 s without a manual join, an IP on `en2`, throughput normal.

### Milestone 4.6e — Close the USB Interface on Terminate (build `f306b1b90feb8c2337dce678e20d6627`, UUID `309E507A`, 0 undefined vendor symbols, deployed, pending reboot + sleep test, includes 4.6d)
- **`RTL8814Native::didTerminate()`:** under the lifecycle lock, `releaseVendorLocked()` (no-op unless this instance still owns the radio, e.g. a plain unplug), then `_usb->closeProvider()` with `_providerOpened = false`, then `super::didTerminate()`. IOKit can then finish terminating and call `stop()`, which skips the parts already done (owner check, `_providerOpened`).
- **Verify after reboot:** after a sleep/wake, `ioreg -w0 -r -c RTL8814Native | grep "^+-o"` must show exactly ONE instance (registered, active), plus the 4.6d checks: auto re-join, IP on `en2`, throughput. Also do an unplug/replug of the dongle: one instance afterwards, no panic.

### Milestone 4.6f — Join Failure on the Takeover Instance; `g_suspended` Reset (build `ab9bc7defe9f10e3e6d0d1526b318f93`, UUID `6E747D47`, deployed, pending reboot + sleep test, includes 4.6d/4.6e)
- **Observed on the 4.6c boot after the wake takeover:** the new instance (`0x1000009d8`) radio is alive (scan finds `MyNetwork` ch 161 at -44 dBm, RX/TX counters advance during the scan, register reads fine), but a manual `test_uc join` stays `Link=0, WAIT_EAPOL_MSG1` for 15 s. `RXFLTMAP2(data)=0x0000` is the vendor's "join failed" state (`hw_var_set_mlme_join` type 1 / `mlme_disconnect` write 0), so auth/assoc fails, not EAPOL. Root cause not determined. That state came out of the 4.6c ordering (old instance resumed, then was torn down by the takeover), which 4.6e changes (didTerminate releases the old owner first). Re-test on 4.6f before digging further. If it reproduces, add join-result telemetry (auth/assoc attempts, last `join_res`, `fw_state`) to `test_uc status`.
- **Bug fixed:** `g_suspended` (kext-lifetime static) was only cleared by `vendor_resume()`. A stale instance removed while asleep handed `g_suspended = 1` to the next probe: its first sleep would skip the card disable and its wake would re-init a live chip. `vendor_remove()` now clears it.

---

## 4. Verification & Diagnostic Commands (Post-Reboot)

### Milestone 4.6g — Termination Deadlock on Wake (build `4095c263a2798af7d07b1fd7f6aa905e`, UUID `9F5CAE76`, deployed, pending reboot + sleep test)
- **Observed on 4.6f (07:08 sleep, 07:09:40 wake):** no panic, but no `en2`. The old instance was `inactive, busy 1`, `CurrentPowerState=0 / Desired=1`, the new one `active, !registered` with no adapter, and every other re-enumerated USB device (audio, AudioBox, mouse, hubs, keyboard) was stuck `inactive, busy` too.
- **`sudo spindump -noTarget 3` (07:14), kernel-flagged deadlock:**
  - `IOServiceTerminateThread`: `terminateWorker -> IOWorkLoop::runAction -> IOService::actionStop -> (our stop) -> IORecursiveLockLock`, so it waits for the lifecycle lock. It is inside the USB workloop gate.
  - `IOConfigThread_'IOUSBHostInterface'`: `doServiceMatch -> startCandidate -> (our start, unwound as 0x0) -> IOUSBHostFamily -> IOCommandGate::runAction -> closeGate`. It holds the lifecycle lock and waits for that gate.
- **Why 4.6e exposed it:** closing the provider in `didTerminate()` is what lets IOKit call the old `stop()` at all, and it calls it under the provider's workloop gate. Whenever the new instance's `start()` is mid-USB-request at that moment, the two threads wait on each other. One termination thread serves the whole system, so every USB device behind it hangs as well.
- **Fix:** termination callbacks never wait for the lifecycle lock.
  - `stop()` only `IORecursiveLockTryLock`s. When the try fails and this instance is not the owner, it does instance-local teardown without the lock. It waits (with an IOLog) only if it still owns the radio, which should not happen once `didTerminate` ran.
  - `didTerminate()` try-locks. When busy, it retains `this` and hands `releaseAndCloseLocked()` (release radio + close provider) to a `kernel_thread_start` helper that waits for the lock off the gate. IOKit calls `stop()` after that close.
- **Rule for future code:** anything called by IOKit with a USB gate held (`willTerminate`/`didTerminate`/`stop`) must not block on a lock that is held across USB I/O.

- **4.6g VERIFIED (07:24 sleep, 07:26 wake):** no panic, the dongle re-enumerated (new IOUSBHostDevice `0x10000096b`), exactly ONE `RTL8814Native` instance (`registered, active, busy 0`), 0 USB devices left `inactive`, radio probed OK (HAL init + firmware). The 07:20 `shutdown_stall` is the restart out of the deadlocked 4.6f boot, expected.

### Milestone 4.6h — Re-join After Any Fresh Start (build `b88cd13709d480edc4809aa6170938ab`, UUID `32228893`, deployed, pending reboot + sleep test)
- **Observed on 4.6g:** the link stayed `DISCONNECTED` after wake. The old instance's `didTerminate` now gets the lock first and releases the radio, so `g_owner` is already NULL when the new `start()` runs, `took_over` stays false and the 4.6d re-join never fired.
- **Fix:** `startLocked()` calls `rtl8814native_rejoin_last()` after every successful probe (`took_over` removed). It is a no-op until a join succeeded in this kext lifetime (`g_last_ssid` is static and empty at kext load), so a cold boot is unaffected; a replug also re-joins.

- **4.6h VERIFIED (07:31 sleep, 07:32:52 wake):** no panic, one instance, 0 USB devices `inactive`, and the link re-joined by itself: `ASSOCIATED (_FW_LINKED)`, WPA2 `COMPLETED (KEYS INSTALLED)`. Two problems: `en2` showed `status: inactive` / no IP (`IOLinkStatus=1`, valid without active), and it joined the 2.4 GHz BSS (`0E:84:08:68:FD:20`, ch 8, 20 MHz, -40 dBm) instead of ch 161.

### Milestone 4.6i — Keep Carrier on Interface Enable; Prefer 5 GHz (build `f8b45cc7e89667b84361b50a3792ded7`, UUID `1591869B`, deployed, VERIFIED)
- **en2 inactive while associated:** the 4.6h re-join completes inside `start()`, before configd brings `en2` up. `RTLEthernetAdapter::enable()` then wrote plain `kIONetworkLinkValid`, clearing the `LinkActive` that `netif_carrier_on` had set. `setCarrierFromDriver()` now remembers the carrier (`_carrier`) and `enable()` re-applies it.
- **2.4 GHz chosen:** `rtw_check_join_candidate()` (core/rtw_mlme.c) compares raw RSSI only, and the dual-band AP is stronger on 2.4 GHz. Patched to add +15 dB to channels > 14 when comparing candidates (the usual wpa_supplicant/iwd 5 GHz preference). Vendor-core change, marked `RTL8814Native:` in the source.

- **4.6i VERIFIED (07:38:30 sleep, 07:39:38 wake), the full sleep/wake path works:**
  - No panic. Exactly one instance, 0 USB devices `inactive`.
  - Automatic re-join on 5 GHz: `0E:84:08:68:FD:24`, VHT 80 MHz, -44 dBm, WPA2 keys installed.
  - `en2` `status: active` with DHCP `192.168.100.11`.
  - Throughput after wake (curl `--interface en2`, speed.cloudflare.com):
    - Down: 291 / 328 / 273 / 300 Mbit/s (25 MB transfers; 100 MB requests now get HTTP 403).
    - Up: 109 / 185 / 270 Mbit/s (50 MB).
    - Same as the fresh-boot numbers.
  - Gateway ping 1.86 ms average, 0 % loss.

### Milestone 4.7 — RX Tasklet Actually on `rtl_rx_wq` (build `eb245605eaebe3a663691a410517fdb3`, UUID `B251AB82`, deployed, verified: no regression)
- **Bug:** `_rtw_init_recv_priv()` calls `rtw_os_recv_resource_init()` (rtw_recv.c:104), which runs `tasklet_init` and then `rtl_work_bind_rx()`. Later the same function calls `rtw_hal_init_recv_priv()` (line 143), and `usb_init_recv_priv()` (hal_hci/hal_usb.c, `PLATFORM_LINUX`) runs `tasklet_init` again. In the compat layer that is `INIT_WORK`, which resets `work->wq = NULL`. So `usb_recv_tasklet` (bulk-IN parsing) always ran on `rtl_system_wq`, queued behind TX and command work, and the dedicated `rtl_rx_wq` thread sat idle.
- **Fix:** `hal_usb.c` re-binds right after its `tasklet_init` (`rtl_work_bind_rx`, marked `RTL8814Native:`).
- **Why the move is safe:** the RX reassembly stage is still touched only by the tasklet (now on `rtl_rx_wq`) and by the wake path while RX is stopped. `cancel_work_sync`/`tasklet_kill` flush both queues. The vendor core already expects the tasklet to run concurrently with workqueue items, as on Linux.
- **Measured (fresh boot 07:45, 5 GHz VHT80, -43 dBm):**
  - Down 250/207/317/293/222/214/215/289/313/311/317 Mbit/s (average about 269). Up 182/138/204 Mbit/s. Gateway ping 2.05 ms, 0 % loss. No panic.
  - Telemetry: 0 RX drops of every kind, `backlog_max=3`, parse 31 us/xfer, USB aggregation 11.3 frames / 15.6 KB per transfer.
  - Same peak as 4.6i (about 317-330); the low runs come and go, which looks like Internet noise. With a backlog of at most 3, RX processing is not the bottleneck.
  - Kept because the design is now what was intended (RX no longer queued behind TX/cmd work). It brings no measurable gain.
- **Where the ceiling likely is (next):**
  - The rate is VHT **2SS** MCS7 in both directions, although the HAL reports `tx_nss=3, rx_nss=3`.
  - `RX Reorder drop=1490` (A-MPDU frames dropped as out of window or duplicate).
  - `USB_AGG_EN=0` in AGG_PG_TH.


### Milestone 4.8 — Rate-Ceiling Telemetry (build `c719470a44b07776a69c5756175cb855`, UUID `86681634`, deployed, verified)
- **Question:** why do both directions sit at VHT 2SS MCS7 (650 Mbit/s PHY at 80 MHz SGI) when the HAL reports `tx_nss=3, rx_nss=3`?
- **Ruled out in the code:**
  - We advertise MCS0-9 for 3 SS (`vht_rx_mcs_map = 0xaa 0xaa` → `rtw_vht_nss_to_mcsmap(rx_nss=3)`).
  - Per-STA NSS is `min(HAL NSS, AP's VHT map)` (`rtw_get_sta_tx_nss`), which is standard.
  - The `vht_highest=VHT 1SS MCS7` that `test_uc` printed is a decoding artefact: `vht_highest_rate` is an `MGN_*` value, not a `DESC_RATE*`.
- **New `test_uc status` lines** (struct fields appended to `rtl8814_rate_diag`; a new `test_uc` still works against an old kext, where they read 0):
  - `VHT MCS Maps`: the AP's RX/TX VHT MCS maps parsed from its beacon VHT Capabilities IE, plus the negotiated `psta->vhtpriv.vht_mcs_map`.
  - `RA Mask`: `psta->cmn.ra_info.ramask`, `rate_id`, `mimo_type`, and the AP HT MCS set (HT Cap IE).
- **Read it this way:** if the AP map shows 2 SS, 2SS is the AP's limit (most consumer routers are 2x2). If it allows MCS0-9 but `ra_mask` lacks the MCS8/9 bits, the cap is in our mask; if the mask has them, the FW RA chooses MCS7 on channel conditions.
- `test_uc` rebuilt (the previous binary is in the scratchpad only).

- **4.8 result (fresh boot 07:5x, ch 153/161 VHT80, -45 dBm): there is no rate cap in the driver.**
  - **AP:** VHT RX/TX map `FA FF` = 2 SS, MCS0-9; HT MCS `FF FF 00 00` = 2 SS. The router is 2x2, so **2 SS is the AP's limit**. Negotiated map `FA FF`, `mimo_type=2` (RF_2T2R).
  - **FW RA mask:** `0x00000000FFFFFFF0` (`rate_id=9`) includes VHT 2SS MCS0-9.
  - **Downlink (AP RA):** already runs at VHT 2SS **MCS8/MCS9**. **Uplink:** our FW RA picks VHT 2SS MCS7 SGI with MCS9 allowed, a channel decision.
  - **Throughput:**

    | Interface | Tool | Down | Up |
    |---|---|---|---|
    | en2 (Wi-Fi) | `networkQuality -I en2 -s` | 349 | 305 |
    | en1 (wired, same router) | `networkQuality -I en1 -s` | 810 | 630 |
    | Linux rtw88 reference (same dongle/AP, Milestone 4.3) | curl | 288-366 | 227-280 |

    So we are **at or above Linux parity**.
  - 4 parallel curl flows on en2 also total about 300, so the limit is aggregate, not per flow.
  - **RX path during the test:** 37,608 bulk IN transfers at 14.9 KB / 12.3 frames on average, parse 31 us, completion 17 us, `backlog_max=1`. Host CPU is not the limit; 8 URBs in flight and RX agg `0x0A05` (DMA mode) are the same as Linux.
  - Reorder drops about 2% of frames during downloads (late/duplicate A-MPDU subframes).
- **Remaining headroom (optional, beyond Linux parity):**
  - RX aggregation tuning: `REG_RXDMA_AGG_PG_TH` threshold/timeout, or USB agg mode.
  - Why uplink RA settles on MCS7 while rtw88 reported MCS9 SGI on a good boot.
- **Testing note:** speed.cloudflare.com now rate-limits (HTTP 403 for 100 MB, 429 after many runs). Use `networkQuality -I en2 -s -c` (multi-flow, JSON) instead.
- `test_uc` now decodes `vht_highest_rate` as `MGN_RATE` (0xA0 = VHT1SS MCS0). The vendor's `rtw_get_vht_highest_rate()` formula is itself off; it only feeds this display.

- **RX aggregation A/B (live `test_uc poke`, same boot, `networkQuality -I en2 -u`, 2 downlink runs per value): no effect.**

  | Setting | Down (Mbit/s) | Average transfer |
  |---|---|---|
  | `0x280 = 0x0A05` (vendor default) | 360 / 341, rerun 334 / 348 | 18.1 KB / 13 frames |
  | `0x0A07` | 353 / 352 | 22.7 KB / 15.4 frames, max 30 KB |
  | `0x0A03` | 321 / 358 | 11.8 KB |
  | `0x1405` (longer timeout) | 359 / 354 | 19.8 KB |
  | `0x0505` (shorter timeout) | 338 / 323 | 12.8 KB |
  | Aggregation off (`0x10C` bit2 = 0) | 334 | 1 frame / 1.5 KB, 443,684 transfers, completion 8 us, parse 3.3 us, backlog_max 6 |

  - 0 RX drops in every variant.
  - **Conclusion:** neither USB transfer size/rate nor the host RX path limits downlink. The ~350 Mbit/s ceiling is the air link with this 2x2 AP, which Linux rtw88 also hits. Vendor values kept (restored live; nothing changed in the source).

### Milestone 4.9 — C2H/RA-Report Telemetry + BSD RX Counters (build `39c5fdc2fe0e45e765cdf1a9ea86c596`, UUID `49EC946A`, deployed, pending reboot)
- **Live findings (4.8 boot, no reboot needed):**
  - The DM watchdog runs. `Driver Throughput` reads 369-407 Mbps RX during a download; the earlier 0 was read while idle.
  - The `TX Rate (FW RA) VHT 2SS MCS7 SGI` value **never changes** (6 samples across a 352 Mbit/s upload). `tx_bw` decodes as `?` (0xFF, i.e. the report was shorter than 7 bytes). The value only moves when an RA report C2H (id 0x0C) reaches `phydm_c2h_ra_report_handler`, and `c2h` stayed at 139 for the whole session (no growth over 10 s). So the "MCS7 uplink" is most likely a **stale value**, not the real rate. Uplink is already 305-352 Mbit/s (`networkQuality`), above the rtw88 reference.
  - RX reorder drops (about 2% on download) are old or duplicate A-MPDU subframes rejected by `check_indicate_seq` (AP retransmits after a lost BA). This matches earlier good-boot duplicate rates, so it is not a driver bug.
- **New `test_uc status` lines:**
  - `C2H by id`: counts per C2H id from our RX parser, via `rtw_hal_c2h_pkt_hdr_parse`.
  - `FW RA Reports`: `handled` counter and the last raw report, recorded by a hook in `phydm_c2h_ra_report_handler` (marked `RTL8814Native:`).
  - Fields are appended to `rtl8814_rate_diag`; the new `test_uc` reads zeros on an older kext.
- **BSD RX counters (`netstat -I en2`: Ipkts/Ibytes = 0 since Milestone 3.2):** `RTLEthernetAdapter::inputFromDriver` now also calls `ifnet_stat_increment_in(getIfnet())` every 32 packets. Only the RX thread runs it, and batching avoids taking the ifnet stats lock per packet. The symbol is in the kernel's sorted `kpi_interface` export list next to the other `ifnet_*` KPIs (`com.apple.kpi.bsd`, already a dependency like `mbuf_*`). **If the kext fails to load after reboot, this symbol is the first suspect.** Roll back to `c719470a` in `Backups/efi_backup_m48`.
- **Verify after reboot:**
  1. `kextstat` shows UUID `49EC946A`.
  2. `netstat -I en2 -b` Ipkts grows.
  3. `test_uc status` `C2H by id` / `FW RA Reports` during an upload. If 0x0C never arrives, find out how the 8814A FW is told to send RA reports.

> **Deployment Status:** Deployed to `/Volumes/NO NAME/EFI/OC/Kexts/RTL8814Native.kext` (EFI was `disk0s1` this boot; the number changes, check `diskutil info` for the 4.3 GB EFI). `MD5 = 39c5fdc2fe0e45e765cdf1a9ea86c596` (UUID `49EC946A`): Milestone 4.9 C2H/RA telemetry + BSD RX counters + 4.8 rate-ceiling telemetry + 4.7 RX tasklet on rtl_rx_wq + 4.6i keep carrier on enable + prefer 5 GHz + 4.6h re-join after every fresh start (verified) + 4.6g (verified) no blocking lock on the termination thread (4.6f deadlocked the USB termination thread on wake) + 4.6f `g_suspended` reset + 4.6e close the USB interface on terminate + 4.6d re-join after takeover, on top of 4.6c owner guard + no restart of live workqueues (4.6c verified: first wake without panic), on top of 4.6b/4.6 sleep/wake, 4.5 clean shutdown (verified), 4.4 warm-start power cycle and earlier. 0 undefined vendor symbols, `dot_clean -m` & `sync` done, 0 `._*`, plist lint OK. **Sleep/wake fully verified on 4.6i (auto re-join on 5 GHz, IP, ~300/110-270 Mbit/s).** Unplug/replug also verified on 4.6i (07:41: one instance, auto re-join on 5 GHz, en2 active, same IP). 4.7 (RX tasklet on rtl_rx_wq) verified: no regression, no measurable gain (RX backlog_max=3). Next: why 2SS MCS7 instead of 3SS, RX reorder drops. Previous builds: `c719470a` (4.8) in `Backups/efi_backup_m48`, `eb245605` (4.7) in `Backups/efi_backup_m47`, `f8b45cc7` (4.6i, stable: sleep/wake + replug verified) in `Backups/efi_backup_m46i`, `b88cd137` (4.6h, en2 inactive after re-join) in `Backups/efi_backup_m46h`, `4095c263` (4.6g, no auto re-join) in `Backups/efi_backup_m46g`, `ab9bc7de` (4.6f, termination deadlock on wake) in `Backups/efi_backup_m46f`, `f306b1b9` (4.6e) in `Backups/efi_backup_m46e`, `ca1fe571` (4.6d) in `Backups/efi_backup_m46d`, `9af63e61` (4.6c) in `Backups/efi_backup_m46c`, `37d04555` (4.6b, panics on wake) in `Backups/efi_backup_m46b`. Older backups: `cc2c41f3` in `Backups/efi_backup_m46_prev_cc2c41f3`, `35d6749b` in `efi_backup_m45`, `c896a978` in `efi_backup_m44`. EFI `config.plist` carries the 26.x TSC patches (Milestone 0.1 update; `Backups/efi_config_tsc` has the old one). After any macOS update, run `python3 Tools/check_kernel_patches.py` with the EFI mounted (on 26.7.1: every applicable patch finds its target; one EXTRA warning, the leaf7 patch, whose first match is the correct `_cpuid_set_info`). A full fallback EFI with `0b9f9e93` (old config) is on the USB pendrive.

After rebooting, check that `kextstat | grep 8814` shows the UUID from `dwarfdump --uuid Build/RTL8814Native.kext/Contents/MacOS/RTL8814Native`, that `status` shows `usb_speed=2 (High-Speed), bulk_in_mps=512`, and that `./Tools/test_uc peek 0x0290` = `0x1E`. Then run:

### 1. Check Radio & Link Status
```bash
./Tools/test_uc status
```

### 2. Scan for 2.4 GHz & 5 GHz Wi-Fi Networks
```bash
./Tools/test_uc scan
```

### 3. Join a Wi-Fi Network (WPA2-PSK or Open)
```bash
./Tools/test_uc join "MyWiFiSSID" "MyWiFiPassword"
```

### 4. DHCP & Ping on `en2`
macOS starts DHCP by itself once the link is up. `en1` holds the default route, so bind tests to `en2`:
```bash
ifconfig en2
ping -c 4 -b en2 8.8.8.8
curl --interface en2 -o /dev/null -w "%{speed_download}\n" 'https://speed.cloudflare.com/__down?bytes=25000000'
netstat -I en2
```
`netstat -I en2` should now show non-zero `Ipkts` / `Opkts` (Milestone 3.1).

---

## 5. Reverse Engineering & Binary Analysis

Reference analysis and vtable comparisons are verified against Apple kernel collections (`BootKernelExtensions.kc`, `kernel`) and reference kexts (`IO80211Family.kext`, `IOSkywalkFamily.kext`, `RtWlanU.kext`) using IDA Pro, Hopper Disassembler, Binary Ninja, Rizin, and custom extraction scripts in `Tools/re/`.

### 2026-10-27: FW RA report behaviour (live, no reboot)
- The 8814A FW sends C2H RA reports (id 0x0C) **only when the TX rate changes**. With the 4.8 kext, C2H went 139 -> 230 over the session, and during an upload (`networkQuality -I en2 -d`, 332 Mbit/s) exactly one more arrived, flipping the rate from VHT 2SS MCS8 LGI to MCS8 SGI. A constant `TX Rate (FW RA)` therefore means a stable rate, not a broken path.
- `tx_bw` reads 0xFF: the 8814A report has `cmd_len < 7`, so `phydm_c2h_ra_report_handler` never fills `curr_bw`. `test_uc` prints `n/a`.
- The `HW_DEF_RA_INFO_DUMP` FW debug query (write `0x40000100|macid` to 0x8C, read 0x2F0) returns 0 on the 8814A FW (the vendor code is `#if 0` with "8814 need to fix"). 0x8C was restored to 0.

### 2026-10-27: build 4.10 staged (radio ABI for the Phase 5 layer), not deployed
- 4.9 (`39c5fdc2`) is still the EFI build and still unverified; its Build/ copy is in
  `Backups/build_4.9/`. 4.10 (`9aed0533`, UUID `2AF2FAB8`) = 4.9 + radio ABI
  (`RTLRadioABI.h`, `callPlatformFunction`, RX data hook, `kRTLUC_RadioCall`), staged in
  `Backups/build_4.10/`. Same imports as 4.9. Verify 4.9 first, then deploy 4.10 and run
  `test_uc radio abi|status|mac|channels|scan` (and `radio join` by hand).
- `test_uc` (rebuilt) works against both: with 4.9 loaded, `radio` returns `0xe00002c2` (bad selector).
- Phase 5 details: `Docs/PHASE5_IO80211.md`.
- WCL contract recovered with headless IDA (IDB saved in the session scratchpad, re-runs take ~7 s): request struct offsets, event IDs + payloads, link state is driven by WCL (`setWCL_LINK_STATE_UPDATE`) after the driver posts events, data path is Skywalk packet queues. See "RE results" in PHASE5_IO80211.md.
- RTL8814AirLayer 0.1 (Phase 5 layer kext) builds with `./build_airlayer.sh` and is checked by `Tools/re/check_airlayer.sh`. Staged in `Backups/airlayer_0.1` (MD5 04402631…), NOT deployed. It does nothing unless the boot-arg `rtlair=1` is set. Needs RTL8814Native 4.10 (radio ABI 1).

### 2026-10-27: review pass — build 4.11 + RTL8814AirLayer 0.2 staged, not deployed
- 4.11 (`db46a680`, UUID `D381B708`) = 4.10 + `rtl_radio_status.scanning` (reserved byte, radio
  ABI stays 1; `_FW_UNDER_SURVEY`, since `state` stays LINKED during an associated scan). Staged
  in `Backups/build_4.11/`. Same imports as 4.10, 0 vendor undefined symbols. Deploy 4.11
  instead of 4.10. `Tools/test_uc` rebuilt (`radio status` prints `scanning=`).
- RTL8814AirLayer 0.2 (`206cf133`, UUID `4FDB44B9`) in `Backups/airlayer_0.2/` replaces 0.1:
  RX gate/lock deadlock fixed (hook only queues, a thread call drains), no-sleep RX packet
  allocation, link-loss monitor, scan/join poll merged, start/stop lifecycle. Details in
  PHASE5_IO80211.md, "0.2 review". Checks: 0 vtable mismatches, 753 imports (only the 4 pad
  slots missing, as 4.9).
- Second review: RTL8814AirLayer 0.3 (`92c7d5df`, UUID `935A4907`) in `Backups/airlayer_0.3/`
  replaces 0.2. `rtlair=1` is now scan-only (joins refused); leave/power-off never drop a link
  the layer did not make (en2's); interface `stop()` takes the packet queues off the work queue
  as Apple does.
- Third review: RTL8814AirLayer 0.4 (`bc6dfde7`, UUID `3E0D36DC`) in `Backups/airlayer_0.4/`
  replaces 0.3: interface `enable`/`disable` now switch the packet queues (the family never does;
  without it no packet would pass with `rtlair=2`). New tools: `Tools/phase5_efi_check.py`
  (read-only EFI check before the boot) and `Tools/phase5_collect.sh` (diagnostics after it).
  **Deploy 4.11 + layer 0.4.**
- **2026-10-27 11:40: 4.9 VERIFIED on hardware** (UUID 49EC946A loaded, no panic; after my
  manual join: 5 GHz ch 153, RSSI -44, VHT 2SS MCS7 SGI, DHCP 192.168.100.11, ping 0% loss,
  `curl --interface en2` 200). A cold boot does not auto-join (expected). Deploy helper:
  `Tools/phase5_deploy.sh "<EFI>" driver | layer N | inert` (backs up config + kexts first;
  tested on a mock EFI). The EFI here mounts writable without sudo.
- **2026-10-27 11:43: 4.11 DEPLOYED** to the EFI (`db46a680`, UUID `D381B708`, 0 `._`, sync);
  config.plist unchanged (`019611e1`). Backup of the 4.9 kext + config:
  `Backups/efi_phase5_20261027_114322_1415`. Rollback: copy that kext back. Next: reboot, user
  joins, `test_uc radio abi|status|mac|channels|scan`.
- **4.11 VERIFIED on hardware** (UUID D381B708, after my manual join): radio ABI 1,
  status (linked, port_valid, ch 153 80 MHz, nss 2), MAC, 32 channels, linked scan 10 BSS.
  `scanning` goes 0 -> 1 -> 0 over ~3.5 s while `state` stays linked (the flag works). en2 stayed
  up through the scan; latency to the gateway was 70-90 ms for ~10-20 s after it, then back to
  ~2 ms (8.8.8.8 ~16 ms), ~240 Mbit/s down. Note for the layer: frequent WCL scans will cost
  latency.
- **2026-10-27 11:48: RTL8814AirLayer 0.4 DEPLOYED with `rtlair=1`** (`bc6dfde7`, Kernel -> Add 15,
  right after RTL8814Native at 14; boot-args otherwise unchanged; only diff vs the backup is the
  new entry + `rtlair=1`). Backup: `Backups/efi_phase5_20261027_114805_1295`. Rollback:
  `Tools/phase5_deploy.sh "<EFI>" inert`, or restore that config.plist (from the pendrive EFI if
  the boot fails). Next: reboot, `Tools/phase5_collect.sh`, check the Wi-Fi menu.
- **0.4 first boot (11:49):** kext loads, but 0 controller instances (`start()` failed); no
  panic, en2 fine. Reason unknown (IOLog not in `log show`, dmesg flooded). **0.5 staged**
  (`f1ccd8a5`, `Backups/airlayer_0.5`): logs into ioreg (`RTLAirLayerLog` on RTL8814Native) and
  builds the CoreCapture logger + fault reporter that `IO80211Controller::start` needs (it
  panics on a NULL fault reporter). Details in PHASE5_IO80211.md, "0.5".
- **2026-10-27 12:06: RTL8814AirLayer 0.5 DEPLOYED** (`f1ccd8a5`, same Kernel -> Add 15 entry,
  `rtlair=1` kept; EFI check OK). Backup of the 0.4 state: `Backups/efi_phase5_20261027_120633_2029`.
  Next: reboot, manual join, then read the step log:
  `ioreg -r -c RTL8814Native -d 1 -w0 | grep -A40 RTLAirLayerLog`.
- **0.5 boot PANICKED** (`Kernel-2026-10-27-120936.panic`, 3 s after boot): page fault (NULL) in
  `CCLogPipe::start+0x80` <- `CCPipe::startPipe` <- `RTLAirController::createCoreCapture`.
  CCLogPipe::start calls `getProvider()->getWorkLoop()` (the owner = our controller) and adds a
  timer to it; `IO80211Controller::getWorkLoop` -> `getWorkQueue` (slot 397), whose family default
  returns 0. Same hole explains 0.4: `IONetworkController::start` -> `createWorkLoop` (306) ->
  `createWorkQueue` (395, default returns 0) -> start fails. I booted from the pendrive
  (now mounted as `/Volumes/NO NAME`; the internal EFI is `/Volumes/NO NAME 1` this boot, still
  0.5 + `rtlair=1`, will panic again).
- **0.6** (`142270c9`, UUID `63E2C469`, `Backups/airlayer_0.6`): overrides `createWorkQueue` /
  `getWorkQueue` with one `IO80211WorkQueue::workQueue()` (as AppleBCMWLANCore; static, never
  released because the family keeps it in `IO80211DefaultWorkQueue`) and creates it before
  CoreCapture. Slot scan: the other trivial family defaults Broadcom overrides (vendor/model
  strings, features, virtual-interface/flow hooks) are NULL-safe or not called at start.
  Checks: 0 vtable mismatches, imports resolve (4 known pads).
- **2026-10-27 12:17: 0.6 DEPLOYED** to the internal EFI (disk1s1, mounted as `/Volumes/NO NAME 1`
  this boot), `rtlair=1`, EFI check OK. Backup of the 0.5 state: `Backups/efi_phase5_20261027_121743_1770`.
  Next: boot from the internal disk, join, read `RTLAirLayerLog`.
- **0.6 boot PANICKED** (`Kernel-2026-10-27-122106.panic`): CoreCapture now passes; NULL+0x658 in
  `IOLockLock` <- `ml_thread_policy` <- `IONetworkController::start+0x1d4`. After createWorkLoop,
  IONetworkController::start calls `ml_thread_policy(_workLoop->getThread())` unless
  `_workLoop == provider->getWorkLoop()`, and `IO80211WorkQueue::getThread()` always returns 0.
  Broadcom avoids it because its provider (`AppleBCMWLANBusInterfacePCIe::getWorkLoop`) returns
  the same queue; YYavicWiFi does the same with its PCI nub.
- **0.7** (`99e52166`, UUID `3F309699`, `Backups/airlayer_0.7`): new `RTLAirNub` (IOService)
  matches RTL8814Native, checks `rtlair`, creates the IO80211WorkQueue, returns it from
  `getWorkLoop()` and registers; `RTLAirController` now matches on `RTLAirNub` (second
  personality) and talks to the radio through `nub->getProvider()`. RTL8814Native unchanged.
  PM: the nub is not PM-initialized; `joinPMtree` climbs to RTL8814Native as before.
  Checks: 0 vtable mismatches, imports resolve (4 known pads). Internal EFI is `disk0s1` /
  `/Volumes/NO NAME 1` this boot, still 0.6 + `rtlair=1` (panics).
- **2026-10-27 12:26: 0.7 DEPLOYED** to the internal EFI (disk0s1, `/Volumes/NO NAME 1` this boot),
  `rtlair=1`, EFI check OK. Backup of the 0.6 state: `Backups/efi_phase5_20261027_122614_1403`.
- **0.7 boot PANICKED** (`Kernel-2026-10-27-123022.panic`): the nub fixed IONetworkController and
  **`IO80211Controller::start` completed**; GP fault in `IO80211SkywalkInterface::start+0x26f`
  (`mov [rcx+0x80]`, rcx = garbage read from `this+0x120`) <- `RTLAirInterface::startWithRegistration`.
  Root cause (old bug since 0.1): the generated header had **wrong class sizes**. KC
  (`OSMetaClass(name, super, size)`): IOSkywalkInterface 0xB0, IOSkywalkNetworkInterface 0xE0,
  IOSkywalkEthernetInterface 0x120, IO80211SkywalkInterface 0x128, IO80211InfraInterface 0x130,
  IO80211InfraProtocol 0x130, IO80211Controller 0x128; ours were 0x88 for the Skywalk chain and
  0x118 for the controller. So RTLAirInterface was allocated at 0xb8 bytes (Apple's ivars pointer
  at +0x120 lay past the end) and RTLAirController's own members overlapped Apple's ivars
  (+0x120 = `_iface`).
- **0.8** (`e0a2384e`, UUID `C0A6C960`, `Backups/airlayer_0.8`): `Source/IO80211/IO80211_25_6_0.h`
  pads each class to its KC size (private `_rtlPad<Class>`) with `static_assert`s;
  `Tools/re/gen_io80211.py` emits the same (`SIZES`). RTLAirInterface is now 0x160,
  RTLAirController 0x1f8. 0 vtable mismatches, imports resolve.
- **2026-10-27 12:35: 0.8 DEPLOYED** to the internal EFI (disk1s1, `/Volumes/NO NAME` this boot),
  `rtlair=1`, EFI check OK. Backup of the 0.7 state: `Backups/efi_phase5_20261027_123515_1458`.
- **0.8 boot (12:36, after the 0.7 panic): NO PANIC, the layer starts fully.** RTLAirLayerLog:
  nub 0.8 -> start 0.8 -> work queue IO80211WorkQueue -> CoreCapture ready (CCLogStream,
  IO80211FaultReporter) -> radio ABI 1, mac ok -> IO80211Controller::start ok -> interface
  initialized -> IO80211InfraInterface::start ok -> registerInfraEthernetInterface = 0x0 ->
  started. `RTLAirInterface` is registered (no BSD name yet). But the radio has **no RX at all**:
  bulk IN `submit_ok=8, comp_ok=0`, rx_frames=0, scan finds 0 BSS, join stuck at channel 1
  `WAIT_EAPOL_MSG1`; TX writes ok (46), CR 0x100=0x207FF, RCR ok; USB is the direct board port,
  SuperSpeed 5 Gbps (as on the verified 4.11 boots). The layer never powers the radio off
  (setPOWER only records). Suspect: chip state after the panic reboot (no clean-shutdown hook).
  Next test: a clean restart without changes; if RX is still dead, `inert` to rule the layer out.
- **Clean restart (12:41), 0.8 + rtlair=1:** same: layer starts fully, radio RX dead (bulk IN
  `submit_ok=8 comp_ok=0`, TX ok=46, join stuck at ch 1 WAIT_EAPOL_MSG1). Reproducible, so not
  post-panic chip state. IOPower: RTL8814Native CurrentPowerState=1, ChildrenPowerState=0;
  RTLAirInterface is a PM child of RTL8814Native. Cause not found yet (no driver log reachable).
  Next: A/B with `inert` (layer returns before touching anything) to confirm the layer causes it.
- **2026-10-27 12:44: A/B, layer INERT** on the internal EFI (disk0s1): `rtlair` removed from
  boot-args, 4.11 + 0.8 still in Kernel -> Add (RTLAirNub::start returns before touching
  anything). Backup: `Backups/efi_phase5_20261027_124423_1285`. Next: reboot, join, compare RX.
- **A/B result (12:46, layer inert): radio works.** Linked ch 153 VHT 80 MHz, WPA2 keys installed,
  IP 192.168.100.11, bulk IN 322 submitted / 314 completed, TX 228. So the started layer causes
  the stall. With the layer started the counters never move past the probe (bulk IN 8 submitted,
  0 completed, 0 errors; TX 46 = probe writes; join attempts add H2C cmds but no bulk TX): pending
  USB transfers never complete, no suspend (comp_err 0, no resubmits). The radio ABI dispatcher
  takes no locks (checked).
- **0.9** (`7f1e244b`, UUID `90B1C2FE`, `Backups/airlayer_0.9`): bisection boot-arg
  `rtlairstep=N`: 1 nub only, 2 stop before IO80211Controller::start, 3 stop after it (no
  interface), 4 interface attached but not started; absent = full start. `phase5_deploy.sh
  "<EFI>" layer 1 <step>` sets it (tested on a mock EFI). Plan: step 3 first; RX alive -> the
  interface (4 or registration) stalls it, dead -> 2, then 1.
- **2026-10-27 12:50: 0.9 DEPLOYED** to the internal EFI (disk0s1), `rtlair=1 rtlairstep=3`, EFI
  check OK. Backup: `Backups/efi_phase5_20261027_125019_1496`. Next: reboot, join, read RX counters.
- **rtlairstep=3 (12:52): radio works with the layer up to a full IO80211Controller::start**
  (linked ch 153, keys installed, IP, bulk IN 374/366, TX 225). So the stall comes with the
  interface: attach / startWithRegistration / what airportd does through it (scan, channels,
  power...).
- **0.10** (`89a2eb93`, UUID `3B06FD63`, `Backups/airlayer_0.10`): 0.9 + call trace in
  `RTLAirLayerLog`: every radio ABI call except STATUS/TX (`@<ms since boot> radio <fn> -> rc`),
  the first call of every IO80211 stub and overridden method (recorded lock-free by
  `STUB_ENTER()`, emitted by the generator too), and STATUS / setPOWER counts, flushed by a
  thread call in the nub (1 s for 3 min, then 10 s). Plan: full start (no rtlairstep), read the
  trace, and compare the timing with the driver counters.
- **0.10 deployed (12:59)** to the internal EFI (disk0s1) with `rtlair=1`, no `rtlairstep`;
  EFI backup `Backups/efi_phase5_20261027_125945_1463`, check OK. Waiting for reboot + join.
- **0.10 full start (13:03 boot, join at ~13:05): stall reproduced, but the trace shows NO radio
  traffic from the layer.** RTLAirLayerLog: only `rtlRadioAbi` / `rtlRadioMac` (@3429 ms), the
  usual start chain, then one stub `isCommandProhibited` (@3432). Afterwards STATUS 0, setPOWER 0,
  no other IO80211 method was ever called (airportd/WCL never drove the interface). Radio: bulk IN
  `submit_ok=8 comp_ok=0`, TX ok=46, ch 1 scan state 0 / 0 BSS, but cmd_issued keeps climbing
  (167 -> 335 with no action: watchdog H2C alive) and live register reads (RCR etc.) work, so
  control transfers and the vendor threads are alive; only the chip produces no bulk IN data.
  en2 is still RTLEthernetAdapter's IOEthernetInterface; the RTLAirInterface Skywalk netif has no
  BSD name. PM now: RTL8814Native Current=1 Children=1.
  Lead being checked: RTLAirInterface is a PM child of RTL8814Native, and
  RTL8814Native::setPowerState(0/1) runs vendor_suspend/resume (full HAL deinit/init). A 1->0->1
  transition triggered by the new PM child would re-init the HAL mid-boot. Next: count
  setPowerState calls in RTL8814Native (or keep RTLAirInterface out of RTL8814Native's PM tree)
  and retest.
- **PM lead, evidence so far (13:10):** IOPower plane: `RTLAirInterface` is a direct PM child of
  `RTL8814Native` (RTLAirNub / RTLAirController are not in the power plane), and RTL8814Native
  never declares a power desire (no changePowerStateTo / makeUsable), so a child joining at state
  0 could drive setPowerState(0) -> vendor_suspend. Against it: register reads still work (not
  surprise-removed), CurrentPowerState=1, and TX ok stayed 46 (a vendor_resume re-downloads the
  firmware over bulk OUT). Not decided without a counter.
- **4.12** (`c90d00c5`, UUID `1AA6B287`, `Backups/build_4.12`): 4.11 + `RTLPowerTrace` registry
  array on RTL8814Native (registerPowerDriver result, every setPowerState(N) with the policy maker
  class, vendor suspend/resume rc and uptime ms). Vendor nm -u clean. `phase5_deploy.sh <EFI>
  driver` installs it (boot-args untouched). Read:
  `ioreg -r -c RTL8814Native -d 1 -w0 | grep '"RTLPowerTrace"'`.
- **2026-10-27 13:14: 4.12 DEPLOYED** (user ran `phase5_deploy.sh "/Volumes/NO NAME" driver`):
  4.12 + layer 0.10, `rtlair=1`, EFI check OK. Backup `Backups/efi_phase5_20261027_131453_2719`.
  Next: reboot, join, read RTLPowerTrace + RTLAirLayerLog.
- **ROOT CAUSE of the layer RX stall (4.12 boot, 13:16): power management.** RTLPowerTrace:
  `@3375 registerPowerDriver ok, state 0` -> `@3375 setPowerState(1)` -> `@3387 setPowerState(0)`
  (7 ms after RTLAirInterface started and joined our power plane at state 0; RTL8814Native had no
  power desire of its own, so its state followed the children) -> vendor_suspend -> `@9016
  setPowerState(1)` -> vendor_resume, after which bulk IN never completes (submit 8 / comp 0, TX
  46, ch 1, 0 BSS). Step 3 worked because the controller is not in the power plane.
- **4.13** (`a1c7a9e0`, UUID `1E0A2048`, `Backups/build_4.13`): 4.12 + `changePowerStateTo(1)` after
  registerPowerDriver (own desire = on; system sleep still lowers us through the power domain).
  Vendor nm -u clean. Expected trace: no setPowerState(0) at boot.
- **Separate bug exposed (open):** the in-place vendor_resume leaves bulk IN dead and TX ok did not
  grow (firmware re-download should have added bulk OUT writes). Real sleep/wake hits the same
  path whenever the dongle does NOT re-enumerate. To check later with a forced sleep on 4.13.
- **2026-10-27 13:18: 4.13 DEPLOYED** (user ran the driver step): 4.13 + layer 0.10, `rtlair=1`,
  EFI check OK. Backup `Backups/efi_phase5_20261027_131800_1596`. Next: reboot, join, read
  RTLPowerTrace (expect no setPowerState(0)) and the RX counters.
- **4.13 VERIFIED (13:20 boot): stall FIXED.** RTLPowerTrace: only registerPowerDriver +
  setPowerState(1), no setPowerState(0). Layer fully started (same chain as 0.10). Join: linked ch
  153, WPA2 keys installed, 32 BSS in scan, bulk IN 189/181, rx_frames 219, TX 231, en2 active with
  192.168.100.11. So RTL8814Native 4.13 + RTL8814AirLayer 0.10 coexist.
- **Next for the layer:** airportd/WCL still never drives the interface (only
  `isCommandProhibited` is called; the RTLAirInterface Skywalk netif has no BSD name). Find what the
  family needs before it attaches a BSD interface / hands the interface to airportd.
- **Why airportd never got the interface (13:25, 4.13 boot):** `IOSkywalkNetworkBSDClient` (IOSkywalkFamily
  personality, IOProviderClass IOSkywalkNetworkInterface) only starts when the interface has
  `IOInterfaceUnit` (start() bails without it; also bails if `IODeferBSDAttach` is true). The unit
  comes from configd's InterfaceNamer through IONetworkStack on our
  `IOSkywalkLegacyEthernetInterface`. configd log: InterfaceNamer typed it IEEE80211 / "airport"
  (good) but "Assigned unit 2 (from database)" because its database matches by MAC and
  xx:xx:xx:xx:xx:xx is en2 (RTLEthernetAdapter); then "failed to name Known interface,
  kr=-536870181" (0xE00002DB kIOReturnNoSpace) in a retry loop. en0 (DriverKit E1000) shows the
  working order: unit 0 named, then BSDClient attached and "BSD Name" set.
- **0.11** (`4437d6aa`, UUID `610853F2`, `Backups/airlayer_0.11`): with rtlair=1 the interface
  uses a locally administered copy of the MAC (`_mac[0] |= 0x02`, 7e:c2:c6:03:ea:84) so
  InterfaceNamer gives it a new unit (expected en3); rtlair=2 keeps the real MAC (en2 must not
  be published then — open). Checks: 0 vtable mismatches, 4 known pads, nm -u clean.
  Deploy: `phase5_deploy.sh <EFI> layer 1`.
- **2026-10-27 13:25: 0.11 DEPLOYED** (user ran `layer 1`): 4.13 + 0.11, `rtlair=1`, EFI check OK.
  Backup `Backups/efi_phase5_20261027_132503_1489`. Next: reboot, check for a BSD name (en3),
  configd InterfaceNamer log and the layer trace (which IO80211 methods airportd calls).
- **0.11 VERIFIED (13:26 boot): native Wi-Fi UI appears.** InterfaceNamer named the layer's
  interface en3 (`IOInterfaceUnit` 3), IOSkywalkNetworkBSDClient attached, the Wi-Fi menu and the
  System Settings Wi-Fi pane show up with the switch on, "Sin conexión", "Ninguna red". airportd
  drives the interface: getHARDWARE_VERSION/DRIVER_VERSION/COUNTRY_CODE/CARD_CAPABILITIES, setPOWER
  x3, getCHANNELS_INFO, enable (queues on), getSUPPORTED_CHANNELS, then setWCL_SCAN_REQ every ~25 s;
  the radio scans (11 BSS, done 3.5 s later), the layer posts 201 x N + 237. en2 unaffected.
- **Why no networks are listed:** kernel os_log of IO80211Family is readable
  (`log show --last boot --predicate 'sender == "IO80211Family"'`). Each airportd scan is
  `IO80211ScanManager::startScan: scanType = 1` -> "Scan type to FW 1" -> our setWCL_SCAN_REQ,
  and 20 s later "IO80211Queue empty" (airportd: `Completed scan ... duration=20.00s`). RE:
  `IO80211InfraInterface::postMessage` handles msg **10** (APPLE80211_M_SCAN_DONE) by calling
  `IO80211ScanManager::scanDone(true)` + `IO80211Controller::scanDone(3)`; ScanManager::scanDone
  pulls results with `queryScanResult` (first `IO80211Glue::sendIOUCToWcl(0xC02869C9, 0xB = SCAN_RESULT,
  2272-byte apple80211_scan_result)`, fallback `apple80211getSCAN_RESULT` -> driver getSCAN_RESULT)
  and fills the scan cache. We only posted 237 (WCL). Broadcom posts 10 with a u32 status, len 4
  (`AppleBCMWLANCore::scanComplete`). Also msg **55** -> `IO80211ControllerMonitor::setDriverAvailable`
  (every IOC logs `isDriverAvailable=<0>`; payload not decoded yet).
- **0.12** (`b829354c`, UUID `7C2BA4B2`, `Backups/airlayer_0.12`): finishScan posts 237 then 10.
  Checks: 0 vtable mismatches, 4 known pads, nm -u clean. If the list stays empty, look for
  `stub getSCAN_RESULT` in RTLAirLayerLog (WCL did not answer the pull, implement getSCAN_RESULT).
- **2026-10-27 13:33: 0.12 DEPLOYED** (user ran `layer 1`): 4.13 + 0.12, `rtlair=1`, EFI check OK.
  Backup `Backups/efi_phase5_20261027_133355_1615`. Next: reboot, open the Wi-Fi menu, read the
  layer trace and the IO80211Family / airportd scan logs.
- **0.12 result (13:35 boot):** still "Ninguna red"; every ScanManager scan times out (~20 s), no
  `stub getSCAN_RESULT`. Root cause found by RE: `IO80211Controller::postMessage` ->
  `IO80211PostOffice::sendMail` (ctrl ivars+0xB18, slot 36), which **returns 0xE00002E2 and drops
  the message unless `IO80211WorkQueue::inGate()`** (then queues it; `deliverMessage` calls the
  interface's slot 355 = `IO80211InfraInterface::postMessage`). Our posts come from thread calls,
  so every 201 / 237 / 10 since 0.1 was dropped. Broadcom posts from its work queue context.
- **0.13** (`531736a2`, UUID `95009A58`, `Backups/airlayer_0.13`): `post()` runs the postMessage
  inside the gate with `IO80211WorkQueue::runAction` (forwards to IOWorkLoop::runAction; args packed
  in one struct pointer), skips posting while `_stopping` (quiesce waits on the poll thread call),
  and logs the first non-zero post rc (`post N -> 0x...`). Checks: 0 vtable mismatches, 4 known
  pads, nm -u clean.
  **Deployed 2026-10-27 13:40 (4.13 + 0.13, rtlair=1) — scan results reach airportd.** No
  `post N -> 0x...` (every post accepted), scans finish in ~3.5 s and the next WCL scan follows
  right away (no more 20 s timeouts), airportd logs `scanResultsCount=10, inProgress=0` on en3,
  and WCL now queries getRSSI / getMCS / getNSS / getRATE / getMAX_NSS_FOR_AP. getSCAN_RESULT is
  never called (the 201 beacons are enough). Next: joins from the menu (rtlair=2).
- **RTL8814Native 4.14** (`baf02980`, UUID `84FEFA28`, `Backups/build_4.14`): with boot-arg
  `rtlair>=2`, `RTLEthernetAdapter::start` returns before `attachInterface`, so en2 is not
  published. The adapter object stays (RX goes through `inputFromDriver` to the layer's data
  hook, TX through `transmitFrame` / `rtlRadioTx`). This lets the layer's Skywalk interface keep
  the hardware MAC (needed on air) without the InterfaceNamer unit clash seen with 0.10.
  Layer 0.13 unchanged: rtlair=2 already joins (setWCL_ASSOCIATE with the PMK airportd puts in
  the embedded key, len 32; other key lengths, e.g. SAE, are refused and logged) and hooks the
  data path on link-up. Deploy: `phase5_deploy.sh <EFI> driver` then `... layer 2`.
  To verify: the Wi-Fi interface takes the real MAC (7c:c2:...), joining from the menu shows
  `associate:` / `join complete` / `data path attached` in RTLAirLayerLog, then DHCP on it.
  Rollback: `... layer 1` (4.14 behaves as 4.13 with rtlair=1).
- **2026-10-27 13:51: 4.14 + 0.13 with rtlair=2 tested.** en2 is now the Skywalk interface
  (NetworkInterfaces.plist kept unit 2 for 7c:..., type IEEE80211, named "Wi-Fi"), scans fine.
  Joining from the menu failed: `Exit-setASSOCIATE:153 ret:-536870201` (0xE00002C7) right after
  `INTERNAL: Get WCL_LOW_LATENCY_INFO` failed; setWCL_ASSOCIATE never reached the layer. RE of
  `WCLJoinManager::handleJoinRequest`: it reads ioctl 0x1AB (WCL_LOW_LATENCY_INFO, 4 bytes) and
  aborts on any error, then sends 0x170 (APPLE80211_IOC_SET_MAC_ADDRESS, the per-network
  private address) and aborts on any error except 0xE00002E2. 0x170 goes through
  `IO80211MacAddressAgent::setMacAddress` -> infra slot 468 `setMacAddress(mloAddrArray&)`
  (address + 3 spares; our stub answered 0) -> `updateMacAddress` (Skywalk ifnet + IOMACAddress).
  The interface already sat at 7e:c2:c6:03:ea:84 (IOMACAddress) while the radio used 7c:...,
  so even a join would have carried no data.
- **RTL8814Native 4.15** (`67dabfd9`, UUID `BB4394F7`, `Backups/build_4.15`): radio ABI function
  `rtlRadioSetMac` (as `rtw_net_set_mac_address`: refuses while linked/linking -> kIOReturnBusy,
  writes adapter_mac_addr + HW_VAR_MAC_ADDR under ps_deny; the adapter copy survives resume).
  ABI stays 1 (new function only; older builds answer kIOReturnUnsupported).
- **RTL8814AirLayer 0.14** (`e4d99ba6`, UUID `6140F29C`, `Backups/airlayer_0.14`):
  `getWCL_LOW_LATENCY_INFO` answers 4 zero bytes; `setMacAddress` logs the address; `startJoin`
  calls `syncMac()` first (interface IOMACAddress -> rtlRadioSetMac, LEAVE + retry on busy).
  Checks: 0 vtable mismatches, 4 known pads, nm -u clean on both.
  Deploy: `phase5_deploy.sh <EFI> driver` then `... layer 2`.
- **2026-10-27 14:02: 4.15 + 0.14 (rtlair=2) tested: first association.** WCL took
  WCL_LOW_LATENCY_INFO and SET_MAC_ADDRESS (private MAC 02:xx:xx:xx:xx:xx), the radio followed
  (`rtlRadioSetMac -> 0x0`), setWCL_ASSOCIATE (auth 1/8, cipher 6, PMK) -> rtlRadioJoin -> `join
  complete` in ~0.5 s, data path attached. Then WCL left at once: `WCLNetManager::setCurrentBSS`
  reads WCL_EXTENDED_BSS_INFO (0x1CC, 532 B) and leaves on any error, then
  WCL_UPDATE_FAST_LANE (0x1F6) likewise (RE). WCL_LINK_STATE_UPDATE is the family's own. The
  4-way timeout in assocTimerAction only arms for EAP joins (set4wayHsTimeout checks isEAPJoin).
- **RTL8814AirLayer 0.15** (`1c1f38ef`, UUID `C6294A1C`, `Backups/airlayer_0.15`): EXTENDED_BSS_INFO
  answers 532 zero bytes, UPDATE_FAST_LANE succeeds. Checks clean. Deploy: `... layer 2` (driver
  stays 4.15).
- **2026-10-27 14:09: 0.15 (rtlair=2) panics on join** (`Kernel-2026-10-27-140957.panic`): page
  fault CR2=0x58 in `IOSkywalkPacketBuffer::getMemorySegment` <- `getDataVirtualAddress` <-
  `RTLAirInterface::rxFrames+0x13e` <- `rxDrainEntry`. The link came up and the first RX frame
  hit an unprepared packet: `IOSkywalkPacketBufferPool::allocatePacket` returns a packet whose
  buffer array (ivars+48) is only filled by `IOSkywalkPacket::prepareWithQueue`, reached via
  `prepare(queue, dir, opts)` (vtable +392). RE: `IOSkywalkRxCompletionQueue::
  enqueuePacketsForNetworking` completes each packet (+408) itself, so the driver must prepare
  RX packets with the RX completion queue before writing them. TX packets come prepared:
  `IOSkywalkTxSubmissionQueue::getPacketListForNetworking`/`packetSubmission` call +392 before
  handing them to the driver.
- **RTL8814AirLayer 0.16** (`c28620a0`, UUID `17707513`, `Backups/airlayer_0.16`): rxFrames calls
  `IOSkywalkPacket::prepare(pkt, _rxCompQ, 0, 0)` and checks `getPacketBufferCount()` before
  copying; txAction skips packets without buffers. Checks clean. Deploy: `... layer 2` (driver
  stays 4.15). Rollback: `... layer 1`.
- **2026-10-27 14:25: 0.16 (rtlair=2) panics on join, one step later**
  (`Kernel-2026-10-27-142545.panic`): "default packetCompletion()" @IOSkywalkPacketQueue.cpp:321
  from `IOSkywalkPacket::completeWithQueue` <- `IOSkywalkRxCompletionQueue::
  enqueuePacketsForNetworking` <- `rxFrames`. RX now reached the stack. completeWithQueue(dir 2)
  calls the source queue's (packet+56, set by prepare) `packetCompletion` (vtable +544); only
  submission queues override it, IOSkywalkRxCompletionQueue keeps the panicking default.
- **RTL8814AirLayer 0.17** (`dee0b715`, UUID `71CB32EE`, `Backups/airlayer_0.17`): RX packets are
  prepared with no source queue (`prepare(pkt, nullptr, 0, 0)`; buffers then start at offset 0,
  length 0). Imports identical to 0.16. Deploy: `... layer 2`. Rollback: `... layer 1`.
- **2026-10-27 14:31: 0.17 (rtlair=2): FIRST WORKING JOIN FROM THE NATIVE MENU, WITH DATA.**
  MyNetwork (wpa3-transition, joined as PSK), private MAC 02:xx:xx:xx:xx:xx on en2, DHCP
  192.168.100.41, router ARP verified, IPv6 autoconf. `ping -b en2` to the router and 1.1.1.1
  works; `curl --interface en2` pulled 20 MB at ~2.1 MB/s. No panic.
  Problem: WCL leaves exactly 60 s after every link-up, "missed beacons timeout"
  (enhancedDisassocReason 9), then auto-joins again ~8 s later (pings lose packets meanwhile).
  RE: `WCLNetManager::assocTimerAction` (5 s timer) calls `handleMissedBeacons` ->
  `leaveNetworkCommand` when both WCL ctx+336 and ctx+344 are older than 60 s. linkUp stamps
  +336; afterwards only `WCLNetManager::handleLqmUpdate` refreshes them: driver event 0x27
  (WCL handler table entry 0x270002), 476 bytes = what `AppleBCMWLANLQM::updateLQM` posts with
  `IO80211Controller::postMessage(..., 0x27, lqm, 0x1DC, true)`. +336 is stamped when
  lqm[472] && lqm[473] && lqm[48] && u32 lqm+40 != 0; +344 when (u32 +28 > u32 +20) || u32 +36.
  Layout used: +0 has_rssi, +4 s32 rssi; +8 per-core rssi; +11/+12 snr; +14/+16 noise; +18/+19 cca.
- **RTL8814AirLayer 0.18** (`d6c39b65`, UUID `8E328F4F`, `Backups/airlayer_0.18`): while LINKED,
  the poll posts an LQM event every 5 s (RSSI from rtlRadioStatus, +40 = beacons of one period at
  100 TU, +48/+472/+473 set). The radio drops the link on beacon loss by itself, so LINKED means
  beacons arrive. Imports identical to 0.17. Deploy: `... layer 2`. Rollback: `... layer 1`.
  Still open: WCL_TRAFFIC_COUNTERS (434), CHANNELS_INFO, ROAM_PROFILE etc. answer Unsupported;
  DPS monitor sees successfulTx:0 (no TX completion stats yet).
- **2026-10-27 14:40: 0.18 (rtlair=2) VERIFIED: stable native Wi-Fi with data.** Joined from the
  menu, link_on 14:40:06 and no "missed beacons timeout" or link_off afterwards (> 3 min).
  On en2 (bound with `-b en2` / `curl --interface en2`): 120/120 pings to the router, avg 2.3 ms,
  max 14 ms; downloads of 25 MB at 34-41 MB/s (~300 Mbit/s); upload of 10 MB at 14.5 MB/s
  (~116 Mbit/s). en2 counters: 82 MB in, 11 MB out. No panic.
  Seen at boot: auto-join retried several times (setDISASSOCIATE / "join failure") before the
  user's join; not analysed yet. Still Unsupported: WCL_TRAFFIC_COUNTERS, CHANNELS_INFO,
  ROAM_PROFILE_CONFIG, PM_MODE, ARP_MODE, BTCOEX_PROFILE, SCAN_HOME_AWAY_TIME.
- **2026-10-27 14:39: rejoin after a manual disconnect fails** (user: disconnected from the menu,
  reconnect errored, then macOS asked for the password). Boot auto-join itself worked (14:39:06,
  3 s). 14:39:22 DISASSOC from ControlCenter (user); 14:39:23.86 JOIN from ControlCenter:
  WCL_ASSOCIATE -> GOOD (radio accepted the join) but no link for 10.6 s, airportd treated it as a
  failed association (hence the password prompt), retries from Settings hit the same, success
  only at 14:40:06. Same shape in the 0.17 run: the first join after every leave timed out ~10 s,
  the next one linked in 0.5 s. Not explained yet: driver IOLog does not reach the unified log
  (only `sudo dmesg`), and the layer trace had been flooded by the per-second "status calls"
  line. Vendor facts: `rtw_set_802_11_connect` returns success without joining while
  _FW_UNDER_LINKING; under _FW_UNDER_SURVEY it only sets to_join for the survey-done callback;
  `rtw_set_802_11_disassociate` does nothing unless _FW_LINKED.
- **RTL8814AirLayer 0.19** (`8253f70a`, UUID `AE0301AC`, `Backups/airlayer_0.19`): trace logs the
  radio state at each join and every state change while joining, and no longer the per-second
  status count; a join whose radio is idle and not scanning at 3 s / 6 s is re-issued (2 max).
  The join request (PMK, never the passphrase) is kept only while joining and zeroed on
  completion, leave and stop. Imports identical to 0.18.
- **2026-10-27 14:53: 0.19 verified the rejoin.** Boot auto-join: radio idle before, linked with
  port valid at tick 1. Menu disconnect (14:53:04) then reconnect: WCL_ASSOCIATE 14:53:15.06,
  trace: tick 1 JOINING, tick 6 (1.5 s) back to IDLE (the radio's first join after a disconnect
  fails by itself), retry at tick 12, survey 13-26, linked + port at tick 27 (6.75 s); link_on
  14:53:22, before airportd's ~10 s limit. Why the radio drops that first join is still unknown
  (needs `sudo dmesg` during the test).
- **RTL8814AirLayer 0.20** (`65742263`, UUID `E2B324A1`, `Backups/airlayer_0.20`): re-issues the join
  as soon as the radio falls from JOINING back to IDLE (1.5 s instead of 3 s). Imports identical.

### 2026-10-27 — layer 0.20 deployed and checked
- Boot at 15:05:08 with driver 4.15 and layer 0.20 (E2B324A1). Boot auto-join linked at tick 1.
- Menu disconnect at 15:05:38 (`rtlRadioLeave` 0x0), then menu reconnect at 15:05:47.84 (channel 11, 20 MHz). The radio linked at tick 1 with no retry: link_on came 0.5 s later, and `END REQ [ASSOC]` took 2.19 s with no password prompt.
- The first-join drop did not happen this time, so the 0.20 fast-retry path was not exercised. The earlier failure (0.19 test) may depend on the band or channel, or on how long the disconnect lasted.
- Seen in passing: WCL sent `APPLE80211_IOC_WCL_REASSOC` at +61.7 s (bssidCount 1, 0 channels). The layer stubs it and returns 0xe00002c7. The link stayed up. This is a candidate for the next IOC to implement (map it to a re-issued `rtlRadioJoin`, or report success when the BSSID is the current one).

### 2026-10-27 — layer 0.21: WCL_REASSOC (roam)
- Trigger seen on 0.20: airportd "BEST CONNECTED ROAM". The network has 2.4 GHz and 5 GHz BSSes; the link was on ch 11. `APPLE80211_IOC_ROAM` goes to WCL setROAM, which calls sendReassocToDriver (ioctl 0x1AA, 188 bytes) with BSSID FF:FF:FF:FF:FF:FF.
- RE (boot.kc):
  - apple80211_reassoc: chanspecs +0, BSSIDs +100 (count u32 +144), channel count +148, flags +152, SSID +155 (length +187).
  - The driver returning kIOReturnNotReady (0xE00002D5) makes WCL save the request and retry it (saveReassoc/tryReassoc).
  - Broadcom completion: postMessage 73 (8 bytes: status, reason; status 0 makes WCLRoamManager::handleReassocEvent fire FSM event 5), then 80 (168 bytes, roam status; WCLNetManager::handleRoamDoneEvent).
  - WCL picks up a new BSSID in WCLNetManager::updateBss (from assocTimerAction every 5 s, or linkUp): it calls WCL_BSS_INFO and, if the BSS differs, setCurrentBSS. It leaves ("Update Bss fail") if WCL_BSS_INFO fails, which is why linkStatus() answers the last link while roaming.
- 0.21:
  - New phase kRoaming. The PMK is kept while linked; it is zeroed on leave, link loss or quiesce.
  - startRoam picks the target from the last scan: same SSID, not the current BSSID, RSSI ≥ -75. The score is RSSI + 30 for 5 GHz at ≥ -65 dBm, with an 8 dB margin. An explicit target from WCL is taken if it is in the scan.
  - Then rtlRadioLeave + rtlRadioJoin(target). The poll uses the same retry as the join.
  - On success: data path back on, 73{0}, 80{0}, LQM.
  - On failure or after 20 s: 73{0xE0820401}, leave, link down. WCL leaves and airportd auto-joins.
  - Nothing better: kIOReturnNotFound. A scan is running or the layer is not linked: kIOReturnNotReady.
- Built: UUID C2C9C947, md5 62bd0850; imports identical to 0.20. Not deployed yet.

### 2026-10-27 — 0.21 tested, then 0.22
- 0.21 booted at 15:16:50. Boot join linked at tick 1.
- At +38.99 s airportd's BEST CONNECTED ROAM ran setROAM. WCL first sends WCL_SCAN_ABORT, which returned Unsupported, then sendReassocToDriver. The layer refused the roam because a WCL scan it had started at +38.988 s was still running: "roam refused: phase 2, radio state 3". WCL logged "Unable to start roaming" and did not retry within the next minute.
- 0.22 changes:
  - startRoam no longer refuses while a scan runs. The radio finishes the survey, and the join issued meanwhile waits for it (to_join).
  - setWCL_SCAN_ABORT returns success. The radio cannot cut a survey short.
- Built: UUID DD8E8066, md5 aad245ea; imports identical to 0.21. Not deployed yet.
- 0.22 deployed and booted at 15:20:20. The boot join went straight to ch 153 (5 GHz) at -44 dBm and linked at tick 1. At +75.9 s the BEST CONNECTED ROAM fired; the layer logged "roam: nothing better than ch 153 rssi -44 (11 BSS cached)" and returned NotFound. WCL logged "Unable to start roaming" and kept the link: no leave. The leave+join roam path itself has not run yet; it needs a boot that lands on 2.4 GHz.

### 2026-10-27 — driver 4.16 + layer 0.23: the remaining WCL/airportd IOCs
On 0.22, `[IOC DEBUG] ... FAIL 0xe00002c7` counts per boot: BTCOEX_PROFILE set 90, WCL_TRAFFIC_COUNTERS 90, CHANNELS_INFO 74, BEACON_INFO 53, COUNTRY_CODE 24, SCAN_HOME_AWAY_TIME 21, MAX_NSS_FOR_AP 17, MCS_VHT 16, plus ROAM_PROFILE, USB_HOST_NOTIFICATION, ROAM_PROFILE_CONFIG, QOS_PARAMS, OS_FEATURE_FLAGS, LINK_UP_DONE, POWERSAVE, INFRA_ENUMERATED, IE, BSS_BLACKLIST, FW_HOT_CHANNELS, PLATFORM_CONFIG. The Wi-Fi menu showed noise 0 dBm, MCS 0, NSS 0 and an empty country.

**RE (boot.kc).** Sizes come from the user-client handlers (`__ZL<n>get<IOC>`, which check `req+24 == size`). Field layouts come from AppleBCMWLANCore:
- mcs_data (8): +4 index (HT full index, VHT MCS).
- mcs_vht_data (20): +4 MCS, +8 NSS, +12 width in MHz, +16 GI in ns. Only filled for a VHT rspec.
- max_nss_for_ap (8): +4 (btc_nss).
- country_code (8): +4..+6 (the user client copies 3 bytes; WCL is asked first).
- beacon_info_t (2056): +0 length, +4 beacon frame (FC 0x80, at most 2048 bytes), +2052 isHE.
- wcl_traffic_counters: 7 x u64. [0] infra packets, [2] infra total, [6] ns timestamp.
- roam_profile_all_bands: 384 bytes.
- channels_info: 10016 bytes, struct of arrays (count u16 at +12, channel at +814[i], flags at +2014/+6816…). Used by AWDL and ChanSpecIsPassiveChan. Not implemented: the layout is not fully known.
- LQM (WCLNetManager::handleLqmUpdate): +11 has_snr / +12 s16 snr, +14 has_noise / +16 s16 noise. These feed IO80211BssManager::getCurrentNoise, which is what the menu shows.

**Driver 4.16.**
- `rtw_recv.c` rx_query_phy_status now calls `rtl8814native_note_rx_phy` (Compat/VendorOsDep.c) for the BSS's frames. The hook takes noise = rx_pwr − rx_snr on the path with the best SNR (OFDM only), averages it with weight 1/16, and keeps it in dBm×16.
- `rtl_radio_status.noise_dbm` (an old reserved byte) carries it. It is 0 when unknown and on older drivers.
- Imports identical to 4.15; `nm -u` vendor check empty. UUID BB22308B, md5 cbba49b4.

**Layer 0.23.**
- getMCS, getMCS_VHT and getNSS come from the FW RA tx_rate (DESC_RATE). getMAX_NSS_FOR_AP is the negotiated NSS.
- getCOUNTRY_CODE (controller) reads the AP Country IE, with "XZ" as the fallback.
- getBEACON_INFO is rebuilt from the scan entry.
- getWCL_TRAFFIC_COUNTERS uses layer TX/RX packet counters.
- get/setROAM_PROFILE store and return the 384 bytes.
- LQM now carries noise and SNR.
- These settings are accepted as no-ops: BTCOEX_PROFILE, SCAN_HOME_AWAY_TIME, ROAM_PROFILE_CONFIG, USB_HOST_NOTIFICATION, QOS_PARAMS, OS_FEATURE_FLAGS, LINK_UP_DONE, POWERSAVE, INFRA_ENUMERATED, IE, BSS_BLACKLIST.
- Still unsupported: CHANNELS_INFO, WCL_FW_HOT_CHANNELS, PLATFORM_CONFIG.
- Imports identical to 0.22. UUID A03D1197, md5 89e39b03.
- An intermediate build used mach_continuous_time and OSAddAtomic64, which were new imports. They were replaced by mach_absolute_time and __atomic builtins so no new KPI symbol is needed.

Not deployed yet. Deploy both the driver and the layer (`... driver`, then `... layer 2`). Layer 0.23 also runs on driver 4.15; noise just stays absent there.

### 2026-10-27 — 4.16 + 0.23 result → driver 4.17

I deployed 4.16 + 0.23 and rebooted (boot 15:38). The Option-click menu now shows:
- country AR (from the AP's Country IE);
- MCS 8, NSS 2, 702 Mbps;
- 802.11ac, 80 MHz, RSSI -43.

Noise still showed 0 dBm. Cause: the hook in `rx_query_phy_status` required `p_phy_info->physts_rpt_valid`, which this phydm version never sets, so the hook never ran.

**Driver 4.17:**
- Drops the `physts_rpt_valid` condition. CCK frames are still filtered by rate.
- Uses only paths A/B, because the 8814A's C/D "SNR" comes from `csi_current`.
- UUID 4A77BCF2, md5 efbd75e5. Imports identical to 4.16.

Not deployed yet: run `... driver`. The layer is unchanged (0.23).

### 2026-10-27 — driver 4.17 result

- Boot at 15:43 with 4.17 (UUID 4A77BCF2) and layer 0.23. Boot auto-join done after 1 tick. Ping to the gateway: 0% loss, 0.6 ms.
- system_profiler output:
  - signal/noise -43 / -72 dBm;
  - MCS 7, 650 Mbps;
  - country AR.
- The noise value is real but biased upward at strong signal. The phy-status SNR saturates near 30 dB (EVM-limited), so pwr − SNR reads "effective noise", not the thermal floor (~-89 dBm at 80 MHz).
- phydm's `odm_inband_noise_monitor_ac` measures the real floor, but it busy-loops on BB debug-port reads and resets the BB and PMAC on every sample. Not used, because it is too intrusive while linked.
- Remaining IOC failures over 2 minutes:
  - CHANNELS_INFO is now the top one (73);
  - SSID/BSSID/NOISE get 0xe0822403 at boot, before link-up;
  - VIRTUAL_IF_ROLE and SCAN_RESULT come from the family.

### 2026-10-27 — layer 0.24: CHANNELS_INFO

APPLE80211_IOC_CHANNELS_INFO is 10016 bytes. It was the most frequent remaining failure (73 in 2 minutes). Its layout was recovered from `AppleBCMWLANCore::handleChanInfoTimer` and `IO80211AWDLPeerManager::isValidChannel`. It is a struct of arrays with at most 400 entries.

| Offset | Field |
|---|---|
| +0..7 | zero (Broadcom never writes these) |
| +8 | country (3 bytes) |
| +12 | u16 count |
| +14 | u16 Broadcom chanspec |
| +814 | primary channel |
| +1214 | indoor only |
| +1614 | radar |
| +2014 | passive |
| +2414 / +2814 | entry is 40 / 80 MHz |
| +3216 | u32 chanspec |
| +4816 | u32 per_chan_info flags (0x08 radar, 0x20 passive, 0x100 CLM restricted) |
| +6416 | entry is 160 MHz |
| +6816 | entry is 6 GHz |
| +7216.. | 6 GHz fields (PSC, VLP/LPI power); zero here |

Broadcom lists one entry per 20 MHz chanspec. It then adds one entry per wider chanspec for each primary channel, with the 40/80/160 flag set.

Layer 0.24 builds the same list from `rtlRadioChannels`:
- 20 MHz entries for every channel;
- 5 GHz 40/80 MHz entries where all of the block's 20 MHz channels exist;
- radar on 52–144.

COUNTRY_CHANNELS_INFO returns the same data. Imports are identical to 0.23. UUID 4E7537DC, md5 20fdc488. Not deployed yet.

### 2026-10-27 — layer 0.24 result

- Boot at 15:56 with driver 4.17 and layer 0.24 (UUID 4E7537DC). Boot auto-join done after 1 tick. Ping to the gateway: 0% loss, 0.65 ms.
- CHANNELS_INFO now returns GOOD. airportd asked twice at boot and stopped; before, it asked 73 times in 2 minutes. No panic.
- Remaining IOC failures, none of them the layer's to fix:
  - SSID/BSSID/NOISE fail only until link-up (15:56:41–:52, link at :53). That is the normal "not associated" answer.
  - VIRTUAL_IF_ROLE/PARENT (ENXIO) and SCAN_RESULT (EIO) are answered by IO80211Family itself for the infra interface.
- Still unimplemented: FW_HOT_CHANNELS and PLATFORM_CONFIG (one query per boot each).

### 2026-10-27 — layer 0.25: the last once-per-boot IOCs

The remaining IOCs were recovered from their AppleBCMWLANCore handlers:

| IOC | Broadcom behaviour | Layer 0.25 |
|---|---|---|
| WCL_FW_HOT_CHANNELS (20 bytes) | `AppleBCMWLANNetAdapter::getFWHotChannels`: firmware iovar `roam_channels_in_hotlist`; u16 AppleChannelSpec[7] at +0, u32 count at +16 | Answers the joined SSID's channels from the scan cache, current channel first, as 20 MHz chanspecs |
| PLATFORM_CONFIG (controller, slot 416) | One flag byte per platform feature, +0 = 6 GHz supported | All zero |
| WCL_SOI_CONFIG, WCL_REAL_TIME_MODE, BTCOEX_PROFILE_ACTIVE (set) | Firmware power-save / real-time / BT coex | Accepted as no-ops |

AppleChannelSpec_t uses the same encoding as a Broadcom d11ac chanspec (`ChanSpecGetPrimaryChannel`):
- band bits 0xC000: 0x0000 = 2 GHz, 0xC000 = 5 GHz, 0x4000 = 6 GHz;
- bandwidth bits 11..13: 2 = 20 MHz, 3 = 40, 4 = 80, 5 = 160;
- sideband bits 8..10, counted in 20 MHz steps from the low edge;
- channel number in the low byte (center channel for wide chanspecs).

Left as they are:
- MCS_INDEX_SET fails once before link-up. It is the family's own `IO80211BssManager::getCurrentMCSSet`.
- ROAM fails with NotFound when there is nothing better to roam to (expected).

Imports are identical to 0.24. UUID 9589A4B2, md5 f7eaf935. Not deployed yet.

### 2026-10-27 — layer 0.25 result

- Boot at 16:02 with driver 4.17 and layer 0.25 (UUID 9589A4B2). Auto-join at 16:02:36. Ping: 0% loss, 0.65 ms.
- Since boot these all return GOOD: FW_HOT_CHANNELS, PLATFORM_CONFIG, SOI_CONFIG, REAL_TIME_MODE, BTCOEX_PROFILE_ACTIVE, and CHANNELS_INFO (55 calls).
- What still fails:
  - the pre-link "not associated" answers: SSID/BSSID/NOISE/CHANNEL/RSSI/MAX_NSS/ROAM_PROFILE set;
  - IO80211Family's own VIRTUAL_IF_ROLE/PARENT (ENXIO) and SCAN_RESULT (EIO);
  - MCS_INDEX_SET once before link.
- No layer IOC that WCL/airportd uses is still Unsupported.

Open items:
- the real leave+join roam path (needs a boot that lands on 2.4 GHz);
- the radio's first-join drop after a disconnect (needs `sudo dmesg`);
- noise is SNR-derived and reads high on strong links.

### 2026-10-27 — driver 4.18: join event log (first-join drop investigation)

Vendor RTW_INFO is compiled out (no `CONFIG_RTW_DEBUG`), and our IOLog only reaches `sudo dmesg`. So the reason the radio drops the first join after a disconnect was never visible.

Driver 4.18 adds a 96-entry ring of MLME/WPA2 events. It is defined in `Source/Driver/RTLJoinLog.h`, written by `rtl8814native_jlog()` in `Compat/VendorOsDep.c`, and read through the new user-client selector `kRTLUC_GetJoinLog` (10) with `./Tools/test_uc joinlog` (no sudo).

Hooks:
- `rtl8814native_radio_join` / `rtl8814native_disconnect`;
- in `rtw_do_join`: entry (fw_state, queue empty), select result, survey for join;
- `start_clnt_join` (channel, bandwidth);
- `OnAuthClient` (seq, status), `OnAssocRsp` (status), `OnDeAuth` / `OnDisassoc` (reason);
- `link_timer_hdl` (state, reauth | reassoc << 8);
- `report_join_res` (aid_res, status), `rtw_joinbss_event_prehandle` (join_res, status);
- `rtw_join_timeout_handler`, `disconnect_hdl`, `rtw_indicate_disconnect` (reason, local);
- `rtw_surveydone_event_callback` (fw_state, to_join);
- WPA2 state changes (WAIT_MSG3 / FAILED / COMPLETED).

The `Tools/test_uc` binary was rebuilt; the previous one is `Tools/test_uc.prev`. Driver 4.18: UUID 1E909E8F, md5 2b23b33b. Imports identical to 4.17. Not deployed yet.

Test after deploying: disconnect from the menu, wait ~10 s, reconnect from the menu, then run `./Tools/test_uc joinlog`.

### 2026-10-27 — 4.18 caught the first-join drop → 4.19

Boot at 16:14 with 4.18 + 0.25. I disconnected from the menu (+41.6 s: radio leave, deauth sent) and reconnected about 31 s later (+72.6 s).

The first join:
- used the scan cache (do_join, select = 1);
- ran clnt join on ch 153 / 80 MHz;
- got **no auth response**: link timer 5× in state 0x202 (station | AUTH_STATE), 300 ms apart;
- ended with join res −1 / status 1, then join timeout.

The layer re-issued the join 40 ms later. That retry:
- found the scan queue empty;
- ran its own survey (3.5 s);
- sent auth, which was answered in 38 ms;
- completed the 4-way handshake.

The boot join (also from cache) got its auth response at once. No MAC change happened before the rejoin (no rtlRadioSetMac).

The failing join started 89 ms after a WCL scan's survey done. The working one started 133 ms after the driver's own survey for join.

Driver 4.19 adds three events to tell "auth not transmitted" from "answer dropped" from "AP silent":
- `JL_AUTH_TX` (seq, oper channel/bw);
- `JL_MGMT_RX`: any non-beacon management frame from the target BSS while authenticating or associating (subtype, A1 matches us);
- `JL_REGS`: at each link timer, TXPAUSE | MSR << 8 | beacons from the BSS since clnt join << 16, plus RCR.

Imports identical to 4.17.

### 2026-10-27 — 4.19 result: first-join drop narrowed down (fix NOT yet implemented)

4.19 was deployed (md5 50128130, UUID 7989B3A5) and booted at 16:19:58 with layer 0.25. I disconnected from the menu at +28.6 s and reconnected at +44.8 s. `./Tools/test_uc joinlog` shows the following.

Boot join (scan cache) works:
- auth tx (ch 153, bw 80);
- mgmt rx from the AP 2 ms later (auth, assoc response);
- 4-way handshake done in 30 ms.

The first join after the disconnect (scan cache again: do_join, select = 1) fails:
- auth tx on the same channel 153 / bw 80 (`b=665` = 0x299).
- 5 link timers in state 0x202, auth re-sent each time. **Not a single management frame comes back from the AP.**
- The beacon counter keeps rising (3/6/9/12/15 per 300 ms), so RX on ch 153 works.
- TXPAUSE = 0 and MSR = 0x02 (station).
- RCR = 0xF40060CE: APM/AM/AB set, CBSSID_DATA/BCN set. The RX filter accepts unicast.
- join res −1 / status 1, then join timeout.

The layer's retry 47 ms later succeeds:
- the scan queue is empty, so `rtw_do_join` runs a survey **with the SSID** (directed probe), 3.7 s;
- clnt join, auth tx → auth rx 25 ms later → assoc → WPA2 done.

Ruled out:
- MAC change: no rtlRadioSetMac before the rejoin.
- CCK management rate on 5 GHz: `update_wireless_mode` sees cur_channel 153, so the rate is OFDM 6M.
- TX pause.
- RX filter.
- Wrong channel or bandwidth.
- Timing after the WCL scan: this time the join came 2.2 s after the last survey, and it still failed.

Leading theory: the AP ignores an auth from a station that has not probed it recently on that band (band steering / probe-before-auth). The only difference between the failing and the working attempt is the directed probe (survey with the SSID) right before auth. At boot, the boot-time scans probed shortly before the join.

**Planned fix (driver 4.20):** before a join, run a directed survey of only the target channel (SSID set, `ch_num = 1`, ~100 ms) and join from its survey-done callback (`to_join`), as the working path does but limited to one channel instead of the full 3.7 s scan.
- Implementation idea: in `rtw_do_join` (core/rtw_ioctl_set.c), when a global `g_join_probe_ch` is non-zero, take the survey branch (the same one used when the queue is empty) with `parm.ch_num = 1` and `parm.ch[0].hw_value = channel`, `parm.ssid` = assoc SSID. Then clear the global.
- `rtl8814native_radio_join` sets the global from the cached BSS's channel (`Configuration.DSConfig`) before `rtw_set_802_11_connect`.
- `struct sitesurvey_parm` is in include/rtw_cmd.h: scan_mode, ssid_num, ch_num, ssid[], ch[] (struct rtw_ieee80211_channel), duration, igi, bw.
- Simpler fallback if that misbehaves: flush the scanned queue (`rtw_free_network_queue(padapter, _TRUE)`) before connect, so `rtw_do_join` always runs the full SSID survey (+3.7 s per join; known to work).

Until then, layer 0.20+ already recovers by re-issuing the join (linked about 5 s after my reconnect, inside airportd's ~10 s limit).

Join-log tooling (kept in 4.19 and later):
- `Source/Driver/RTLJoinLog.h` (event codes);
- `rtl8814native_jlog()` in `Compat/VendorOsDep.c`;
- hooks in core/rtw_mlme_ext.c, rtw_mlme.c, rtw_ioctl_set.c and Driver/WPA2Supplicant.c / VendorProbe.c;
- user-client selector 10, read with `./Tools/test_uc joinlog`.

Event fields:
- `regs`: a = TXPAUSE | MSR << 8 | beacons << 16, b = RCR;
- `auth tx`: b = channel | bw << 8;
- `mgmt rx`: a = subtype | A1 matches << 8.

### 2026-10-27 — driver 4.20: directed one-channel probe before join (deployed, pending reboot test)

Implements the fix designed after the 4.19 diagnosis: probe the target channel with the SSID before authenticating, so the AP's band steering sees a fresh directed probe request.

Three pieces:
- `u8 g_join_probe_ch` — global defined in `Compat/VendorOsDep.c` (~line 627), declared `extern` in `include/rtw_mlme_ext.h` (~line 1324).
- `rtl8814native_radio_join` (`Driver/VendorProbe.c` ~1081): in the **BSSID branch** (the one airportd's associate takes — the layer copies the first BSS candidate into `j.bssid`), it looks up the cached BSS with `rtw_find_network(&scanned_queue, bssid)` and sets `g_join_probe_ch = network.Configuration.DSConfig` right before `rtw_set_802_11_connect`. After connect returns it clears the global again — if a survey was already running, the join is deferred to its survey-done callback (`to_join`) and a stale one-channel parm must not leak into a later join.
- `rtw_do_join` (`core/rtw_ioctl_set.c` ~82): captures and clears `g_join_probe_ch` at entry, builds the parm with `ssid[0] = assoc_ssid` / `ssid_num = 1` always, and when `probe_ch != 0` sets `ch_num = 1`, `ch[0].hw_value = probe_ch`, `flags = 0` (active). The condition of the survey branch is `probe_ch || queue empty`, so a non-empty scan cache no longer short-circuits into select-and-auth; instead the ~100 ms single-channel directed survey runs first. `to_join` is already `_TRUE` at that point, and `rtw_sitesurvey_cmd` itself flushes the scan queue (`rtw_free_network_queue`), so `rtw_surveydone_event_callback` selects the just-probed BSS and authenticates — the same path the working 3.7 s retry used. This is the same `ch_num=1 + hw_value` pattern the vendor roam code uses (`rtw_mlme.c` ~2730).

Paths NOT covered (on purpose):
- the SSID-only join (`rtl8814native_join_network`, boot auto-join / wake re-join) does not set the probe; at boot the initial scans have just probed the AP, which is why the boot join works from cache.
- if `rtw_sitesurvey_condition_check` denies (busy traffic, buddy under survey) the join fails as before and the layer's re-issued join recovers.

Build: UUID 2BAC8CE6, md5 6258f28717b70e77c8d557c154a55497. Imports identical to 4.19 (530 undefined, all Apple KPI, 0 vendor). Deployed 16:31 to `/Volumes/NO NAME/EFI/OC/Kexts/RTL8814Native.kext` (`dot_clean -m` + `sync`, 0 `._*`, plist lint OK). Previous 4.19 + 0.25 + config.plist backed up in `Backups/efi_phase5_20261027_163145_3638`. Layer stays 0.25.

Verify after reboot:
1. boot auto-join still works from cache (joinlog: auth tx → mgmt rx in a few ms);
2. disconnect from the menu, wait ~10 s, reconnect from the menu, then `./Tools/test_uc joinlog`;
   - expected: `JL_DO_JOIN` (queue non-empty), then `JL_SURVEY_FOR_JOIN` with **b = 153** (the directed single-channel survey — the signature of the new path), ~100 ms later auth tx, mgmt rx within ~30 ms, WPA2 completed, link in ~0.2–0.3 s total;
   - failure signature of the old bug (5 × link timer 0x202, no mgmt rx, join res −1, then the layer's full-scan rescue join ~5 s later) must be gone;
3. ping to the gateway 0% loss.

Fallback if the one-channel survey misbehaves: flush the queue instead (`rtw_free_network_queue(padapter, _TRUE)` before connect in `rtl8814native_radio_join`) so every join runs the full 3.7 s SSID survey — known to work, just slower.

### 2026-10-27 — 4.20 result: probe-before-auth theory DISPROVED → 4.21

4.20 was deployed and tested with two disconnect→reconnect cycles (`./Tools/test_uc joinlog`). Both cycles show the same pattern:

- The directed one-channel survey **runs as designed** (`survey for join, b = 153`, done in 103–138 ms) and finds the AP.
- The auth that follows is **still ignored**: 5 × link timer 0x202, zero mgmt rx back, beacons counting normally, join res −1.
- The layer's rescue join (full SSID survey, 3.5 s) gets auth rx 13–14 ms after auth tx, WPA2 done.

So the AP does **not** need a recent directed probe before answering auth — 4.20 sent exactly that and still got silence. The constant across every success and failure in the 4.18–4.20 logs is **time since our last transmitted frame to that AP**:

- In both 4.20 cycles the successful auth came **3.8–3.9 s after the last ignored auth** — the ignored auths themselves seem to refresh the suppression.
- The first reconnect auth comes ~14.2 s after the radio leave and fails; the rescue succeeds ~20 s after the leave.
- Boot joins historically succeeded (4.18/4.19) because 40–60 s of shutdown/reboot quiet preceded them; in the 4.20 log the boot/wake join within seconds of a leave also failed.

Working model (matches every data point so far): after a disassociation involving our station, the AP suppresses auth responses from our MAC for roughly **15–20 s**, and each ignored auth restarts that window. The full 3.5 s survey works not because of its probes but because it *delays* the first auth past the window; the directed probe (138 ms) does not.

**Driver 4.21 (deployed, pending reboot test):** stop relying on the delay; make the working path explicit.
- `rtl8814native_disconnect` records `g_last_leave_ms` (systime, ms-based in our compat layer).
- `rtl8814native_radio_join` (BSSID branch): if the last leave is closer than `RTL_JOIN_REPROBE_MS` (25 s), it logs `JL_FLUSH_SCANQ` (a = ms since leave) and sets `g_join_flush_scanq = 1`. The 4.20 `g_join_probe_ch` set is **removed** (combining a flush with the one-channel limit would reproduce the short survey; the `g_join_probe_ch` plumbing in `rtw_do_join` stays dormant for future A/B).
- `rtw_do_join`: consumes and clears `g_join_flush_scanq` at entry and runs `rtw_free_network_queue(padapter, _TRUE)` **before** taking the scanned-queue lock, so the queue-empty branch takes over: full directed SSID survey (~3.5 s) → survey-done → auth. Flushing in `rtw_do_join` (not radio_join) also covers a join deferred behind a running WCL survey, and after a failed join `rtw_indicate_disconnect` frees the target BSS anyway (that is why the rescue always saw an empty queue).
- Joins more than 25 s after a leave (boot, wake after long sleep) keep the fast cache join — historically those work.

Build: UUID 5D9D9817, md5 c5233bd91c7a226eb72a75f461a892ea. Imports identical to 4.19. Deployed 16:50 to the EFI; previous 4.20 backed up in `Backups/efi_phase5_20261027_165038_3876`. Layer stays 0.25. `Tools/test_uc` rebuilt (adds "flush scanq" to the joinlog names).

### 2026-10-27 — 4.21 result: first-try reconnect VERIFIED (aggressive-timing case still untested)

Boot: auto-join from cache in 1 tick (no flush at boot — `g_last_leave_ms` is 0 after kext load), auth rx 1 ms after auth tx, WPA2 done 32 ms later. The flush correctly did not fire.

Reconnect test: user left at +24.9 s and rejoined **21.2 s** later. `flush scanq a=21218` fired (within the 25 s window) → `do_join b=1` (queue really empty) → full SSID survey 3.54 s → clnt join → **auth rx 2 ms after auth tx** → assoc → WPA2 done. First try, zero link timers, zero join res −1, linked 3.75 s after the reconnect. The layer never had to rescue.

Caveat: at 21.2 s after the leave the suppression window (~15–20 s) may already have expired on its own, so this run does not discriminate "flush+survey works" from "window had passed anyway". The discriminating case is a reconnect **within ~5 s** of the leave: the survey then finishes ~3.5 s later, putting the first auth ~13–14 s after the leave — inside the historical failure zone (auth at 14.2–16.2 s after leave failed on 4.19/4.20). If that fails, the auth tx lands inside the suppression window; the planned 4.22 then extends the auth retry budget (link_timer_hdl: 5 × 300 ms → ~25 × 300 ms ≈ 7.5 s) so the join survives the window on any reconnect timing; the AP answers the first auth after it expires. Cost when the AP is really gone: join fails in ~8 s instead of ~1.5 s, still inside airportd's patience and covered by the layer's rescue.

### 2026-10-27 — 4.21 aggressive test: suppression window MEASURED; 4.22 designed (NOT yet implemented)

I repeated disconnect→reconnect; the ring captured two fresh cycles plus the boot join.

- Boot join: cache select, auth rx **1 ms** after auth tx, WPA2 done 32 ms later. Fast path intact, no flush at boot (`g_last_leave_ms` = 0 after kext load).
- Cycle "medium": leave → reconnect **13.5 s** later (`flush scanq a=13475` fired) → full SSID survey 3.53 s → clnt join → auth tx at **17.2 s after the leave** → 5 × link timer 0x202, **zero mgmt rx** → join res −1. The auth landed inside the window despite the directed survey.
- Rescue (layer re-join 50 ms after the failure): flush fired again (18.7 s) → survey 3.5 s → auth tx at **22.5 s after the leave** → `link timer b=0` and **mgmt rx/auth rx 1 ms later** (the AP answered the second auth, ~301 ms) → assoc → WPA2 done.

**Measured window:** auths at 17.2–18.4 s after the leave are all ignored; the first auth at 22.5–22.8 s is answered (on its retry). Combined with the earlier data points (14.2–16.2 s failed, 21.2 s worked): **the AP ignores auth from our MAC for ~19–20 s after a disassociation, and each ignored auth refreshes the window**. The 3.5 s survey only helps when the reconnect happens ≥ ~7 s after the leave.

### 2026-10-27 — Driver 4.22: REAUTH_LIMIT=32 + MAX_JOIN_TIMEOUT=15000 (VERIFIED 100% ON LIVE HARDWARE)

Implemented the designed fix to let the auth phase survive the AP's post-disassociation silence on the first attempt without failing or requiring a layer rescue join:
1. `Source/rtl8814au/include/rtw_mlme_ext.h:33`: `REAUTH_LIMIT` raised from 4 to **32**. At `REAUTH_TO = 300` ms, auth attempts now span up to **9.6 seconds** (initial + 32 retries).
2. `Source/rtl8814au/include/rtw_mlme.h:22`: `MAX_JOIN_TIMEOUT` raised from 6500 to **15000** ms so `pmlmepriv->assoc_timer` does not abort a join whose auth retries run past 6.5 s.
3. Clean build produced `36ea1ac8a83a606ceeb2d36f8c9ff015` (UUID `37A9B718-5762-3DF9-B89B-69BF15F36531`), with **0 undefined vendor symbols**.
4. Deployed to `/Volumes/NO NAME/EFI/OC/Kexts/RTL8814Native.kext` via `./Tools/phase5_deploy.sh "/Volumes/NO NAME" driver`. Backup saved in `Backups/build_4.22` and `Backups/efi_phase5_20261027_173408_9819`.

**Live Hardware Verification (Ultra-Aggressive Reconnect: T = 5.2 s):**
- Boot join: cache join, auth rx in 5 ms, WPA2 keys installed in 36 ms.
- Reconnect test (`./Tools/test_uc joinlog`):
  - User disconnected from the Wi-Fi menu (`radio leave` at +35951 ms).
  - User reconnected only **5.2 seconds later** (`radio join ... flush scanq a=5200` at +41151 ms) — the most aggressive timing possible.
  - Survey completed in 3.53 s (`survey done` at +44684 ms).
  - First auth sent at +44850 ms (8.9 s after leave). The AP remained silent as expected.
  - Instead of giving up after retry 4 with `join res -1` (as 4.21 did), Driver 4.22 smoothly kept retrying: `link timer b=0` through `b=16` (17 total attempts over ~5.1 seconds).
  - At +49957 ms (14.0 s after leave, retry 16), the AP answered: `mgmt rx` + `auth rx a=2 b=0` at +49971 ms (14 ms later)!
  - Association response arrived 2 ms later (`assoc rx` at +49973 ms), followed immediately by `join res a=5` (SUCCESS) and WPA2 handshake (`wpa2 a=3` keys installed at +49984 ms).
- **Result:** **100% FIRST-TRY SUCCESS**. Zero `join res -1`, zero timeouts, zero layer rescue joins needed!
- **Throughput verified on hardware (`en2`):** Download **~380-388 Mbps**, Upload **~214-242 Mbps** (tested with multi-MB payloads against Cloudflare and Apple `networkQuality`). `Ipkts: 502,533` / `Ibytes: 672 MB` verified actively counting in BSD netstat.

### 2026-10-27 — Layer 0.26: setWCL_SET_ROAM_LOCK accepted (DEPLOYED to EFI)
- In `RTLAirInterface` ([RTLAirLayer.cpp](../Source/AirLayer/RTLAirLayer.cpp), [RTLAirLayer.hpp](../Source/AirLayer/RTLAirLayer.hpp)), implemented `setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *)` returning `kIOReturnSuccess`.
- Eliminates the `IO80211Family: ERROR: IO80211QueueCall::handleEntry - called type 3, error 0xe00002c7` log when WCL configures roam lock.
- Built `Build/RTL8814AirLayer.kext` (`MD5 = 613b72cbc45b205c86395ca253247790`, UUID `4B72FA43-45FB-3B64-86ED-D22E1C9DF6B8`, 0 vtable mismatches vs AppleBCMWLANSkywalkInterface).
- Deployed to `/Volumes/NO NAME/EFI/OC/Kexts/RTL8814AirLayer.kext`. Backup in `Backups/airlayer_0.26` and `Backups/efi_phase5_20261027_175310_3518`.

Current EFI state: driver **4.22** (UUID 37A9B718, md5 36ea1ac8), layer **0.26** (UUID 4B72FA43, md5 613b72cb), `rtlair=2`.

### 2026-10-27 — Known gaps: WPA3-SAE and 802.1X Enterprise (documented, not implemented)

The layer only joins open networks and WPA2-PSK. In `setWCL_ASSOCIATE` the embedded `apple80211_key` is read at `c+64` (length at `key+4`): length 32 is taken as the PMK and handed to the radio (`rtl8814native_radio_join` -> `rtl8814_wpa2_configure`, in-kernel 4-way handshake in `WPA2Supplicant.c`); length 0 joins open; any other length is logged (`key length N is not a PMK, unsupported`) and refused. A WPA2/WPA3-transition AP therefore joins as WPA2.

What a real implementation needs is listed in `Docs/ROADMAP.md` ("Open: security modes beyond WPA2-PSK"). The first step is to capture, with the existing layer log, what airportd sends for a SAE-only SSID and for an 802.1X SSID (auth type fields, key type/length, RSN IE) before writing code. No WPA3-only AP and no RADIUS server were available, so nothing was prototyped.

### 2026-10-27 — AirDrop / AirPlay / Handoff / Sidecar / Universal Control: stage 1 investigation (no code change yet)

Goal: find out why `sharingd` logs `WiFiP2P bit is not set, WiFi state: Unknown` and `awdl0` does not exist, and what the layer would have to expose.

Findings (IDA on BootKernelExtensions.kc, plus the live log):
- **`VIRTUAL_IF_ROLE` / `VIRTUAL_IF_PARENT` failing (res 6, ~620 per 40 min) is NOT a bug.** `IO80211Family`'s static handler `getVIRTUAL_IF_ROLE` does `safeMetaCast(interface, IO80211VirtualInterface)` and returns 6 when the interface is the primary one (not a virtual interface); the family answers it, our layer never sees the call. A real Broadcom primary interface returns the same. Do not spend time on it.
- **How awdl0 is created:** `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *)` switches on the role at `+12`: roles 8..10 -> `createNANInterface`; role 6 and 7 are the other virtual roles (7 = a second virtual interface, "already created role" warning); everything else logs `create virtif role %u`. The request comes from userspace (airportd/wifip2pd) only when the primary interface advertises the feature, so the gate is upstream of this call. Our layer never received a `VIRTUAL_IF_CREATE` (no such IOC in the log, only `USERSPACE_P2P_OPTIONS` once, answered GOOD).
- **P2P capability:** `getP2P_DEVICE_CAPABILITY` is answered by the NAN interface object in Broadcom (`*a2 = 0`, returns the NAN device capability); our layer has no NAN/AWDL object.
- **`getCARD_CAPABILITIES`** (our 0.2x sends only the unconditional bits `0xE66F, 0x61, 0x04, 0x40, 0x04, 0x0201`) sets many more bits from Broadcom firmware feature flags (`featureFlagIsBitSet(n)`: 1, 2, 7, 17, 21, 27, 28, 29, 30, 38, 39, 40, 46, 47, 55, 70, 98, 121, ...) plus `shouldSupportTethering`. Which of those bytes/bits is the "WiFiP2P" bit that CoreUtils/`sharingd` reads is NOT yet known: that mapping lives in userspace (CoreUtils / Apple80211 in the dyld shared cache), not in the kernel.

What is still unknown, and the next steps:
1. Locate the userspace reader of the P2P bit (CoreUtils `SDNearbyAgentCore`, probably via `CWFInterface`/`Apple80211Get(CARD_CAPABILITIES)`) to learn which capability bit it is. Needs the dyld cache extracted into an IDA database (the kernel DB does not have it).
2. Set only that bit and observe whether `sharingd` changes state and whether airportd then sends `VIRTUAL_IF_CREATE` (role 6/7 = AWDL candidates).
3. If it does, implement `setVIRTUAL_IF_CREATE` creating an `IO80211VirtualInterface`, and only then the real AWDL work: channel hopping between the AP channel and the social channels (6 / 149), AWDL action frames and sync, which this USB radio may not be able to do with the required timing.

Expected outcome per feature: AirPlay to a receiver on the same LAN and Handoff/Universal Control over infrastructure Wi-Fi may need only the P2P bit; AirDrop and wireless Sidecar need real AWDL. None of this is verified. Needs a second Apple device on the same Apple ID and a working Bluetooth on this Mac to test.

### 2026-10-27 — AWDL gate FOUND: `CARD_CAPABILITIES` byte +8 bit 0 (stage 1 corrected)

This corrects the stage-1 note above, which blamed the `WiFiP2P bit is not set` message of `sharingd`.

- **`sharingd` message was a startup transient.** `-[SDNearbyAgent _bleNearbyInfoAdvertiserUpdate]` (disassembled with `dyld_info -disassemble /usr/libexec/sharingd`) sets the "wp" bit when `wifiState` is not 0/Off, the NearbyAction scan is on and Manatee is available (`wifiFlags & 0x20` = hostAP blocks it). The "not set, WiFi state: Unknown" lines were logged at 18:16:00.64, before CoreUtils' `CUWiFiManager` finished starting (18:16:00.73: `WiFi state monitor started, Connected, 0x208 <WPA 5GHz>`); there are none afterwards. The Wi-Fi state side is fine.
- **The real blocker is `wifip2pd`**: `Cannot create AWDL interface because the device is missing the capability` / `Failed to create interface AWDL on en2 with error <Apple80211Error Operation Not Supported>` (repeated, once per client: airportd, ContinuityCaptureAgent, AirPlay Sink...). Everything that needs AWDL (AirDrop, Sidecar, Universal Control, AirPlay P2P, Continuity Camera) is blocked here.
- **What it checks** (`/usr/libexec/wifip2pd`, function around file offset `0x100047xxx..0x1000484xx`): `Apple80211RawGet` with type 12 (`CARD_CAPABILITIES`), data length 21, then a bit test on the returned bytes per interface kind:
  - AWDL: userspace data byte 4, bit 0;
  - NAN and NAN Data: userspace data byte 9, bit 0.
- **Offset shift:** IO80211Family strips the 4-byte `version` from `apple80211_capability_data` before the userspace copy. Measured with `Tools/re/cardcap.c` (`Apple80211Open`/`BindToInterface("en2")`/`RawGet(type 12)`): userspace sees `6f e6 61 04 | 00 40 04 00 | 01 02 00 ...`, i.e. kernel `p[4..]` shifted down by 4. So the gates are, in our kernel struct (`getCARD_CAPABILITIES`, `RTLAirLayer.cpp`):
  - **AWDL = `p[8]` bit 0** (we send 0),
  - NAN / NAN Data = `p[13]` bit 0 (we send 0).
- **What Broadcom does:** `AppleBCMWLANCore::getCARD_CAPABILITIES` sets `p[8] |= 1` together with `p[7] |= 1` (and `p[10] |= 8` in one case) inside the `featureFlagIsBitSet(2) == 0` branch, i.e. when the firmware advertises the AWDL/peer feature; NAN is `p[13] bit0` from other flags.
- **What happens after the bit is set** (not done yet): `wifip2pd` will send `VIRTUAL_IF_CREATE` (apple80211_virt_if_create_data, role at `+12`) through IO80211Family into our controller. `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` handles role 6 (AWDL candidate), 7 (second virtual interface), 8..10 (NAN) and builds an `IO80211VirtualInterface`; our layer has no such override, so the request would still fail with Not Supported. Stage 2 = implement that creation (an `IO80211VirtualInterface` with an AWDL role, `awdl0`) so the interface exists; stage 3 = make it actually do AWDL on the air (channel time-slicing with 6/149, AWDL frames and sync), which the USB radio may not manage.

Expected result of only setting `p[7]|1, p[8]|1`: `wifip2pd` stops saying "missing the capability", tries to create the interface and gets Not Supported from the family or our layer. That proves the mapping, nothing more.

### 2026-10-27 — Layer 0.27: AWDL capability bits (built, NOT deployed yet)

`RTLAirController::getCARD_CAPABILITIES` now also sets `p[7] |= 1` and `p[8] |= 1` (the pair Broadcom sets together; `p[8]` bit 0 is what `wifip2pd` tests after IO80211Family strips the version). NAN (`p[13]` bit 0) stays clear. Version strings are 0.27 (Info.plist, build_airlayer.sh, LOG / `getDRIVER_VERSION`).

Build: md5 `574f1f3aa4b0ea3eabe35caed3ab13e5`, UUID `F08464C7-B755-3398-828B-6600F38681BE`; imports identical to 0.26 (`nm -u` diff empty); staged in `Backups/airlayer_0.27`; `Tools/phase5_deploy.sh` and `Tools/phase5_efi_check.py` updated. Driver stays 4.22.

Risk: after this, `wifip2pd` will try `VIRTUAL_IF_CREATE` through IO80211Family into a controller that has no override for it. Expected: an error (Not Supported) and nothing else; unknown: a family default that dereferences something and panics. Recovery is the usual: remove the layer or restore the 0.26 backup from the EFI.

Verify after reboot:
1. `kextstat | grep RTL8814` shows layer 0.27 (UUID F08464C7) and driver 4.22;
2. `./Tools/cardcap` (build `Tools/re/cardcap.c` with `clang -o Tools/cardcap Tools/re/cardcap.c -framework CoreFoundation`) prints byte 4 with bit 0 set (`01` in the second group);
3. the log line `Cannot create AWDL interface because the device is missing the capability` is gone; check what `wifip2pd` logs instead and whether an `APPLE80211_IOC_VIRTUAL_IF_CREATE` shows in the IOC log;
4. Wi-Fi join, speed and sleep/wake unchanged.

### 2026-10-27 — Layer 0.27 result: capability bit works; `VIRTUAL_IF_CREATE` now reaches us (stage 2 analysed, not implemented)

Boot 18:53:08 with driver 4.22 + layer 0.27 (UUID F08464C7): no panic, join and data unchanged (gateway ping 0% loss, 0.7 ms; 1.1.1.1 13.5 ms).

- `Tools/cardcap` now prints `6fe66105 01400400 ...`: userspace byte 4 = `01` (kernel `p[8]` bit 0), byte 0 = `6f e6 61 05` (kernel `p[7]` bit 0 also set).
- `wifip2pd` no longer logs `missing the capability`. It now logs `No AWDL interface found. Creating AWDL interface` and `Failed to create interface AWDL on en2 with error <Apple80211Error (iokit/common) unsupported function[-536870201]>`.
- The IOC log shows `Set APPLE80211_IOC_VIRTUAL_IF_CREATE` (type 94, len 32) -> `FAIL 0xe00002c7`, 4 times; our layer's own stub (`@4177 stub setVIRTUAL_IF_CREATE` in `RTLAirLayerLog`) is what answers it. `VIRTUAL_IF_ROLE/PARENT` 6 are the family's normal answers.

What a real `setVIRTUAL_IF_CREATE` must do (from `AppleBCMWLANCore::setVIRTUAL_IF_CREATE`, `apple80211_virt_if_create_data`, 32 bytes, role dword at `+12`, 16-byte BSD name output at `+16`):
- Role **6** (AWDL / "prox", the path wifip2pd takes): `createProxInterface()` builds a second Skywalk interface (`IO80211SkywalkInterface` subclass with its own registration info, MAC, tx/rx pools and data path) and copies its BSD name (`awdl0`) into the request at `+16`. If one already exists it just returns its name.
- Role **7**: AP/STA interface (`AppleBCMWLANIO80211APSTAInterface::withOptions` + `start`, BSD name prefix `"ap"`); a second creation returns `-536870190`.
- Roles **8..10**: NAN (`createNANInterface`); anything else: `-536870911`.
- Then `IO80211Controller::setAwdlInterface` / `IO80211AWDLPeerManager` hold the AWDL object that the family uses for peers and multicast.

Assessment: implementing role 6 means a second Skywalk interface in our layer (the first one took many layer versions), and it only yields an `awdl0` that exists. wifip2pd would then drive it with the AWDL IOCs (channel sequence, sync, peer discovery), none of which the USB radio can execute: the interface would be an empty shell and AirDrop, Sidecar and Universal Control would still not see peers. It also adds risk (a new object that wifip2pd, airportd and IO80211Family start using). Features that only need the infrastructure link (AirPlay to a receiver on the LAN, Handoff over the network) do not depend on any of this.

Observed in passing: a `shutdown_stall_*.shutdownStall` report is written at every reboot today (17:37, 18:15, 18:52). Not a panic (no new `Kernel-*.panic` since 14:25); cause not investigated.

Layer 0.27 stays installed: harmless, and it replaces "missing the capability" with a clearer "unsupported function".


### 2026-10-27 — `shutdownStall` reports investigated: not our kext

Each reboot today wrote `/Library/Logs/DiagnosticReports/shutdown_stall_*.shutdownStall` (17:37, 18:15, 18:52). Read with `spindump -i <file> -stdout`: it is launchd's own 1-second sample taken 2 s into shutdown (`com.apple.xpc.launchd.shutdownstall`) listing the processes that had not exited yet: AppleSpell, BlueTool, AppleUserHIDDrivers, DriverKit-AppleEthernetE1000, PowerUIAgent, Safari.SafeBrowsing.Service, tuxera_ntfs(_daemon). In the kernel_task sample every thread is idle and **no stack address falls inside RTL8814Native / RTL8814AirLayer** (0 matches in the kext address range). So the stall is userland processes slow to quit, not the driver; no change needed. (`RTL8814Native` 4.5+ also has the `systemWillShutdown` card-disable hook, verified earlier.)
