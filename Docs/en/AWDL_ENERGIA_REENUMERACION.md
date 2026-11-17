# Power, dongle re-enumeration and restart

Three defects of the layer (`RTL8814AirLayer.kext`) that show up when the T9UH disappears and comes
back (every sleep on this board, or an unplug) or when it appears late at boot, plus the kernel's
shutdown order that explains them. Measured on 2026-11-16 on macOS 26.7.3 (25G342), driver
`CFE7BB9D`; the `airportd` disassembly is from 25G342 and the kernelcache one from 25G332 (same
xnu; the IOSkywalkFamily offsets match the 25G342 traces). The Spanish original is
[../AWDL_ENERGIA_REENUMERACION.md](../AWDL_ENERGIA_REENUMERACION.md).

## 1. Context

- In S3 the board cuts power to the USB port: on wake the T9UH re-enumerates and a new
  `RTL8814Native` is born. The persistent `RTLAirNub` (also anchored to `IOResources`) keeps the
  controller, `en2`, `awdl0` and `llw0`; `RTLAirController::radioGone` drops the old radio and
  `radioBack` adopts the new one (`HANDOFF_AWDL.md`, "Sleep/wake with the AirLayer").
- `en2`, `awdl0` and `llw0` are power children of the radio (`RTL8814Native`) in the IOPower
  plane. When the old radio terminates, its `PMstop` drops them.
- `IOSkywalkNetworkBSDClient` (power child of each interface) serves the BSD ioctls.

## 2. Defect A: auto-join fails with "driver not available"

**Symptom.** On a cold boot `airportd`'s auto-join aborts with
`error=(37 'driver not available')` for every trigger (`iff_up`, `dark_wake`, `user_wake`,
`screen_off`) for the whole session; joining by hand from the menu (`WiFiAgent`, `ASSOC`) works.
Code 37 is generic in CoreWiFi (also "Already associated…", "AJ cancelled…"); only the text
"driver not available" is the defect, and that text lives in `/usr/libexec/airportd`.

**Mechanism (IDA, `airportd` x86_64 from 25G342).**

| Function | Address | What it does |
|---|---|---|
| `-[CWXPCInterfaceContext __allowAutoJoinWithTrigger:reply:]` | 0x1000b6df8 | Returns `NSPOSIXErrorDomain` 37 "driver not available" when the interface node has byte +196 at 0 |
| `deviceNodeForName` | 0x10007aadb | Walks the node list (`qword_100176190`) by name |
| `_initInterface` | 0x10007b3ef | Creates the node and sets +196 = (fourth argument != 0) |
| `_initAttachedAirPortInterfaces` | — | Calls `_initInterface` with 1: interfaces present when `airportd` starts |
| `_initInterface_block_invoke_3` | — | Late `KEV_DL_IF_ATTACHED` path: passes a captured byte (0 in practice) |
| `_bsd_80211_event_callback` | 0x1000852e0 | The only other writer of +196: event 55 (`APPLE80211_M_DRIVER_AVAILABLE`), `available` = u32 at +8; on 0→1 it logs "Driver Availability : available. Begin autojoin" and calls `processDriverAvailabilityForAutojoin` |

**Measurement.** Five boots that day: in the only bad one (cold power-on, 16:08) `en2` attached
0.95 s after `airportd` started; in the four good ones `airportd` found `en2` already present. The
layer never posted event 55.

**Broadcom's layout (kernelcache 25G332).** `AppleBCMWLANCore::powerOn` (call at
0xffffff8001680ad5) zeroes 0xF8 bytes, u32 3 at +0, `available` = 1 at +8, `reason` 0xE0821803 at
+16, and calls `IO80211Controller::postMessage(this, staIface, 0x37, buf, 0xF8, true)`; also from
`powerOff`, `bootChipImage` and `watchdog`. It matches `struct apple80211_driver_available_data` in
the public headers (184 B; Broadcom sends 248).

**Fix** (`1066272`, layer `7C2E196C`). `RTLAirController::setPOWER` posts event 55 in that layout
on power on. If the node was already marked, `airportd` ignores it (it acts on a change only).
**Verified** on a cold boot with the same bad order (`en2` 0.8 s late): "Begin autojoin", auto-join
`STARTED`, `en2` with an IP and no "driver not available".

## 3. Defect B: with no radio, every ioctl on `en2` sleeps forever

**Symptom.** Hot-unplugging the T9UH left `configd` hung: the userspace watchdog flagged it twice
(`IPConfigurationAgentQueue`, 60 s unresponsive) and the system was unusable until a reboot.

**Trace** (report `configd_*.userspace_watchdog_timeout.spin`, kernel frames symbolicated with
`atos` on `/System/Library/Kernels/kernel`, UUID 28267FD5, base 0xffffff8000200000 + offset):
`ioctl` → `ifioctl` → `ifnet_ioctl` → `IOSkywalkNetworkBSDClient::ioctlHandler` (IOSkywalkFamily
+0x4e0e) → `IOCommandGate::runAction` → `IOWorkLoop::sleepGate`, never returning. Two other
processes were stopped at the same point.

**Mechanism (IDA).** `ioctlHandler` sleeps on the gate while the BSD client is powered off (dword at
+316 == 0) and the command needs power, or while another command holds the busy flag (+320);
`setPowerStateHandler` wakes it. `CommandPowerTable` marks `SIOCSIFFLAGS`, `SIOCADDMULTI` and
`SIOCDELMULTI` as needing power; the rest need none when bit 2 of the configuration is set, and
need power otherwise. With the radio gone, `en2` and `awdl0` have no power parent and are off.

**Fix** (`a39e329`, layer `947F9977`). `radioGone` arms a 5 s `thread_call`; if there is no radio by
then (a sleep re-enumeration takes about 1 s), the root domain becomes a second power parent of
`en2`, `awdl0` and `llw0` (`anchorPower`), which stay powered while the system is awake, and the
controller answers without a radio. `radioBack` cancels a pending timer; the anchor stays once
made. **Not yet tested** with a real unplug.

## 4. Defect C: restart hangs after a re-enumeration

**Symptom.** Restarting after a sleep, or after the dongle reconnected on its own (19:07:51,
"hardware connection lost"), left the machine hung in the last part of shutdown, which is not in the
unified log (`logd` has exited). `shutdown_stall` reports exist for every shutdown since 11-10 and
show none of this: they cover the userspace part.

**Tests and what each ruled out.**

| Test | Result | Conclusion |
|---|---|---|
| Cold boot without the T9UH, restart | Fine | The hang is in the driver or the layer |
| `rtlawdl_llw=0` (no `llw0`), restart | Fine | `llw0` is involved |
| `llw0` present; take `llw0` down, remove its IPv6, `SIOCPROTODETACH`/`SIOCPROTODETACH_IN6` by hand; restart | Fine | Not discriminating: no re-enumeration in that session |
| `llw0` present, nothing by hand, no re-enumeration; restart | Fine | `llw0` alone is not enough |
| Session that hung at 19:17 | Re-enumeration at 19:07:51 | The trigger is the re-enumeration |
| Sleep and wake with the old layer; `ioreg -p IOPower` | `en2` and `awdl0` under the new `RTL8814Native`; `RTLLlwInterface` outside it, `CurrentPowerState` 0 (desired 1), its BSD client also at 0 | Cause confirmed |

**Mechanism.** `radioBack` and `anchorPower` re-joined only `en2` and `awdl0` to the tree. After a
re-enumeration `llw0` had no parent and stayed off. On restart, `reboot_kernel` (25G332,
0xffffff80008c0ea0) runs, in order: `IOSystemShutdownNotification(0)` (which delivers
`systemWillShutdown(kIOMessageSystemPagingOff)` to the power plane through
`IOPMrootDomain::handlePlatformHaltRestart(6)`), `proc_shutdown`, unmount, `if_down_all`
(0xffffff80005def10: `if_down` and IPv4 and IPv6 unplumb of every `ifnet`, then `dlil_if_free`) and
`PEHaltRestart`. Unplumbing IPv6 from `llw0` reaches `ioctlHandler`, which waits for power that
never comes: the same wait as defect B, now on the restart thread.

**Fix** (`f5deb52`, layer `4997D361`). `RTLAwdInterface::companion()` exposes `llw0`, and
`radioBack` and `anchorPower` join it to the new parent too. **Verified**: after a sleep
(re-enumeration at 20:21:44) `llw0` and its BSD client are under the new `RTL8814Native` with
`CurrentPowerState` 1, Wi-Fi comes back on its own and the following restart completed.

**Discarded attempt.** `4185ed6` detached `llw0`'s protocols from
`RTLLlwInterface::systemWillShutdown` on `kIOMessageSystemPagingOff`. It rested on the manual tests,
which were not discriminating, and with `llw0` powered off the IPv6 detach would wait the same way.
Reverted in `3a3bb88`.

## 5. Other findings of the same day (this machine)

- **OpenCore `PowerTimeoutKernelPanic` and `pf`.** On 25G332/25G342 the inline patch for
  `IOService::ackTimerTick` does not match (0xffffff8000ab0250, size 0x6b4) and the fallback
  replaces the kernel's first `"com.apple\0"` with `"not.apple"`. That string (0xffffff8000cd8f00) is
  also used by `pf`'s anchor check (`sub_FFFFFF80006CC910`, `pf_anchors`/`pf_is_enabled`): with the
  quirk on, `net.filter_state` = `pf` and `net.link.generic.system.enable_netagent` = 0, and
  Network.framework leaves the Skywalk path. With the quirk off, `enable_netagent` = 1 and
  `filter_state` empty (measured). Effect on AirDrop speed not measured (needs an iPhone).
- **`now >= *pkt_enq_time` (fq_codel) panic on the wired NIC.** It matches cross-core TSC skew:
  with ForgedInvariant active, 120–350 ns were measured and a different core ahead on each boot
  (also with `-FIPeriodic`); with `-FIOff`, 0 backwards reads, also after a sleep. `-FIOff` had been
  removed by mistake; put back, 0 ns skew measured. The RTL8814 kexts are not involved.
- **Immediate wake from S3.** Caused by a USB RGB controller on the chipset XHCI controller; with it
  unplugged the machine stays asleep.

## 6. Builds and commits

| Layer | Commit | Content | State |
|---|---|---|---|
| `7C2E196C` | `1066272` | Event 55 in `setPOWER` | Verified |
| `947F9977` | `a39e329` | Root-domain anchor if the radio is not back in 5 s | Installed; unplug not tested |
| `2D9A9F4E` | `4185ed6` | Detach `llw0`'s protocols at shutdown | Reverted (`3a3bb88`) |
| `4997D361` | `f5deb52` | `llw0` back in the power tree on `radioBack` and in the anchor | Verified (sleep + restart) |

## 7. Open

- Hot unplug with `4997D361`: `configd` should not hang and, after 5 s, the layer should log
  "interfaces kept powered by the root domain".
- An `llw0` created after the anchor was set goes under whatever radio exists then: not checked
  without a radio.
- The addresses in this note are from 25G342 (`airportd`, kernel) and 25G332 (kernelcache); 14.x
  and 15.x were not looked at.
