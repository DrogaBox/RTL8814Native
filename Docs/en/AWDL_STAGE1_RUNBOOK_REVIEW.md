# Review of the hardware test runbook T1–T7

Object: `~/Desktop/GUION_PRUEBAS_AWDL.md` (local file, outside the repo, not modified).
Contrast against `Docs/AWDL_STAGE1_IMPL_SPEC.md` §14, `Docs/AWDL_STAGE1_TEST_LOG_PATTERNS.md`,
`Docs/AWDL_STAGE1_LOG_STRINGS_AUDIT.md`, `Tools/phase5_deploy.sh`, `build_airlayer.sh` and the
current code of
`Source/AirLayer`. Static review: nothing of the runbook was executed nor anything loaded.
The runbook lines are cited as `GUION:<n>`; the measurements taken on this machine (25G241,
2026-10-29) are marked «measured». Global verdict: the runbook is consistent with spec §14 in
structure, rollback and daemon expectations, but it contains three errors that would block the
tests (kernel channel, the nonexistent literal of the 94 and the zsh `log` builtin), a `rtlawdl=1`
mechanism that already exists and is not used, and it omits the rows T2.1–T2.4, T3.3 and T7.1 of
the spec.

## Status after the verification

Verified and applied to the runbook: H1 (`log` is a zsh builtin, measured with
`zsh -fc 'type log'`), H2 (ring via `ioreg -l`), H3, H5–H17. H6 confirmed: the plist of `wifip2pd`
does not declare `KeepAlive`. Precision on H4: the original boot-args filter did not match
`rtlawdl=` (pattern `^rtlair(step|awdl)?=`), so it did not remove it; fixed in
`Tools/phase5_deploy.sh` (commit `4021fb9`), and the documented form is
`sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh ...`. The revised runbook stays outside the repo.

## 1. Findings

| # | Step | Severity | Problem | Evidence | Proposed correction |
|---|---|---|---|---|---|
| H1 | All the `log show` calls (GUION:46, 63, 83, 107) | Blocking | The runbook says «each `bash` block is pasted as-is» (GUION:3), but the default shell of macOS 26 is zsh and `log` resolves to the builtin of the zsh/log module: `log show ...` fails with `zsh:log: N: too many arguments` before reaching `/usr/bin/log` | Measured with `/bin/zsh -f`: `type -a log` → «log is a shell builtin» + «log is /usr/bin/log»; `log show --last 1m --predicate ...` → builtin error | Prefix all the calls: `/usr/bin/log show ...` (or `command log show ...`). Alternative: paste the blocks in `bash` |
| H2 | T2/T3 (GUION:63-65) | Blocking | `log show --last 2m --predicate 'process == "kernel"' \| grep -i "awdl:"` will return nothing: on 26.7.1 the `IOLog` of the kext does not reach `log show`; the lines go to the `RTLAirLayerLog` ring of the `RTL8814Native` node | `Source/AirLayer/RTLAirLayer.cpp:20-24` (comment and mechanism), `:38` (`airLog` writes IOLog and the ring); `Source/AirLayer/AWDLInterface.cpp:24` (`LOG` → `rtlAirLog`). Measured: the ring content is only greppable with `ioreg -l`; without `-l` ioreg truncates the value (0 matches of `gate open-validated` without `-l`) | `ioreg -r -c RTL8814Native -d 1 -l \| grep -E 'awdl: (registerEthernetInterface\|created\|born\|enable\|disable\|teardown)'` — read early (200 lines, overwritten) |
| H3 | T2/T3 expected (GUION:67) | Blocking | `awdl: create role 6` does not exist: the success of the 94 emits `awdl: created %s` | `Docs/AWDL_STAGE1_LOG_STRINGS_AUDIT.md` §3 row T2.2 and §7 row 4 (DOES NOT EMIT); `Source/AirLayer/RTLAirLayer.cpp:1809` | Expected of the 94: `awdl: registerEthernetInterface 0x0` (`AWDLInterface.cpp:207`), `awdl: created awdl0` (`RTLAirLayer.cpp:1809`) and `awdl: born awdl0` (`AWDLInterface.cpp:225`) |
| H4 | T1.2/T1.3 (GUION:49-51) | Major | «To activate `rtlawdl=1` it must be added to the boot-args … extend `phase5_deploy.sh` or edit by hand»: the script already supports it with the variable `RTL8814_AWDL=1`; moreover, every deployment WITHOUT that variable erases `rtlawdl=` from the boot-args | `Tools/phase5_deploy.sh:8` (usage), `:70` (the cleanup regex includes `rtlair(step\|awdl)?=`), `:73` (adds `rtlawdl=1`) | `cd ~/Desktop/RTL8814Native && sudo RTL8814_AWDL=1 RTL8814_LAYER_SRC=Build/RTL8814AirLayer.kext RTL8814_LAYER_MD5=$(md5 -q Build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer) ./Tools/phase5_deploy.sh "<internal EFI>" layer 2` and reboot. Note for the runbook: if it is redeployed without `RTL8814_AWDL=1`, T2+ go back to `rtlawdl=0` |
| H5 | Before starting, point 1 (GUION:8-11) | Major | Obsolete: the patch is already integrated into the tree and `build_airlayer.sh` already compiles and links `AWDLInterface.cpp`; the kext with AWDL exists built | `build_airlayer.sh:40` (compiles `AWDLInterface.cpp`) and `:67` (links it); `Source/AirLayer/RTLAirLayer.cpp:577-589` (the 94 handler is already in the tree); `Build/obj_airlayer/AWDLInterface.o` and `Build/RTL8814AirLayer.kext` present (measured, md5 `535c1767…`) | Reduce the point to: the tree already integrates the patch and the compilation; it suffices to run P0 and deploy. The caution remains that P0 must recompile before deploying |
| H6 | T1.2 and T2/T3, trigger (GUION:43-46, 56) | Major | «Provoke the daemon to emit it by restarting it» assumes automatic respawn: the launchd plist of `wifip2pd` has no KeepAlive; it is on-demand via `MachServices`, so after `killall` it may stay stopped and the 94 is not emitted | Measured: `/System/Library/LaunchDaemons/com.apple.wifip2pd.plist` only declares `MachServices`, `ProcessType`, `EnableTransactions/PressuredExit` — without KeepAlive | After the `killall`, check `ps aux \| grep '[w]ifip2pd'`; if it is not there, wake the service (toggle Wi-Fi or close/open AirDrop; `?` which exact client re-launches it) or reboot. The create runs at daemon startup («No AWDL interface found. Creating AWDL interface», patterns §5 T4.4) |
| H7 | All the `log show` of the daemon (GUION:46, 83) | Major | Without `--info --debug` the wifip2pd messages may not appear (the level is not documented); `tail -40` does not filter by the literals | `Docs/AWDL_STAGE1_TEST_LOG_PATTERNS.md` §1 (commands with `--info --debug`, 3 m window) and its note on spec T4.1 | `/usr/bin/log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E '<constant patterns>'`; if the interpolated fields come out `<private>`, repeat with `sudo /usr/bin/log show --private …` (patterns §1) |
| H8 | Global order | Major | There is no Wi-Fi verification step after each reboot: the deployment changes the kext (new md5) and the whole runbook assumes `rtlair=2` alive before touching AWDL | `Docs/AWDL_STAGE1_IMPL_SPEC.md:1200-1201` («with the gate open and `rtlair` at the level the system already uses»); rollback available at `Tools/phase5_deploy.sh:106-108` | New step after T1.1 (and after the H4 redeploy): check association and traffic over `en2` (e.g. `ifconfig en2 \| head -5` + ping to the gateway); if it fails, `sudo ./Tools/phase5_deploy.sh "<internal EFI>" inert` before continuing |
| H9 | T3.5 (GUION:71-78) | Major | Unnoted boot-loop risk: with `rtlawdl=1` persistent in the `config.plist`, after a panic the next boot creates `awdl0` again (the daemon creates it per cycle, patterns §8); and `ping6 -I awdl0` needs an `inet6 fe80` on the interface, not guaranteed | Patterns §8 T7.3 (one create per load cycle); `ping6` requires a source address on the interface (`?` whether awdl0 receives it at birth) | Add: before the ping, `ifconfig awdl0 inet6 \| head -3`; if there is a panic and it repeats at boot, remove `rtlawdl` from Recovery/OC with the `config.plist` or redeploy without `RTL8814_AWDL=1` before leaving the system at rest |
| H10 | T6 (GUION:83-87) | Major | «Two lines of `Failed to create low latency interface`» accepts a count the documents do not close (there are three candidate strings); the present/complement literals and the 60 s wait of the spec are missing | Patterns §7 (three T6.1 literals) and §9 row 8 («the count of the spec does not close»); `Docs/AWDL_STAGE1_IMPL_SPEC.md:1270` («wait 60 s») | Mark the count as `?`; grep the three: `Failed to create low latency interface for`, `Failed to create interface` (peer `Low Latency`), `Failed to create the companion low latency interface for AWDL`; present `Creating companion low latency interface` and `Registering for Power State changes from wifid`; absent `because parent`, `Ignoring request to create AWDL companion`, `Timed out while waiting for the low latency interface`; wait 60 s after T4 |
| H11 | T7 (GUION:95-103) | Major | T7.1 (monitoring registration) is missing: it is the test deciding the last factual discrepancy between documents and the runbook omits it | `Docs/AWDL_STAGE1_SPEC_DELTA.md:286` (row T7.1); `Docs/AWDL_STAGE1_IMPL_SPEC.md:1278` | After T4: `/usr/bin/log show … \| grep -cE 'No AWDL interface found'` → 1 (no re-creation) and search the event registration literals per `AWDL_WIFIP2PD_P2P_EVENTS.md` (exact literal `?`) |
| H12 | T1.1 (GUION:36-41) | Minor | It only checks the property; not the ring line nor the closed branch that halts the boot | `VersionGate.h:81` (property), `:108` (`gate <verdict>: <reason>`); `GateLogic.h:69,72-73` (`open-validated` / `closed-structure` / `closed-unknown-build`); `RTLAirLayer.cpp:1397` (`version gate closed, staying out (en2 is unaffected)`); `KnownBuilds.inc:16` (25G241 validated). Measured on the live system: `RTLAirGate = "open-validated: build 25G241 validated, structure matches"` | Add `ioreg -r -c RTL8814Native -d 1 -l \| grep -E 'RTLAirGate\|gate '` and the rule: if the verdict is not `open-validated`, stop and roll back `inert` |
| H13 | P0 (GUION:15-25) | Minor | P0.1, P0.2 and the part of P0.4 without `__cxa_pure_virtual` are missing; the nm count of 93 is correct (measured: 93) and the citation to CODE_FIXES §5 is correct | `Docs/AWDL_STAGE1_IMPL_SPEC.md:1203-1211`; `Tools/re/check_awdl_symbols.sh` and `Tools/re/check_awdl_header.py` exist (measured); `Docs/AWDL_STAGE1_CODE_FIXES.md` §5 (the 93 recipe) | Add: `Tools/re/check_awdl_symbols.sh` → 11/11 type `0f`; `Tools/re/check_awdl_header.py` → no discrepancies; `nm Build/obj_airlayer/AWDLInterface.o \| grep -c __cxa_pure_virtual` → 0. P0.3 (vtable order) was closed statically (commit `dd81ec8`); cite it instead of repeating it |
| H14 | T2/T3 failure triage (GUION:68-69) | Minor | It only lists 2 of the birth failure strings and omits the 2 of the 94 handler | Patterns §4 (ten literals + combined grep, `Docs/AWDL_STAGE1_TEST_LOG_PATTERNS.md:106-110`); audit §5 (`awdl: alloc failed` at `RTLAirLayer.cpp:1795`, `awdl: init failed` at `:1799`, different from `awdl: base init failed`) | Replace with the combined grep of patterns doc §4 plus `awdl: (alloc failed\|init failed\|invalid role\|created)` |
| H15 | T7 (GUION:97-103) | Minor | `kextunload`/`kextload` on a boot KC kext (OpenCore) can be rejected; unload footprint and state check missing; the «no panic» expectation coexists with reviewed hook defects | Correct identifier: `Source/AirLayer/Info.plist:10`; no non-Apple libraries (`Info.plist:54-72`). `?` behavior of kextunload with boot KC kexts. Known defects: `Docs/AWDL_STAGE1_CODE_REVIEW_HOOK.md`. Footprints: `RTLAirLayer.cpp:1830` (`awdl: teardown`), `AWDLInterface.cpp:319`/`:327` (`enable`/`disable`) | Before: `kextstat \| grep -i rtl` (kextstat exists on 26.7.1 and delegates to `kmutil showloaded`, measured). After the unload: look for `awdl: teardown` in the ring. If `kextunload` rejects, use a reboot as the cycle; for loading, if `sudo kextload <bundle>` answers «already loaded», try `sudo kmutil load -p <bundle>` (kmutil present, measured) |
| H16 | If something fails (GUION:107-108) | Minor | `log show --last 5m > fallo.log` does not contain the kext lines (kernel channel does not exist, H2) and does not collect the load state | Derives from H2 | Add: `ioreg -r -c RTL8814Native -d 1 -l > ~/Desktop/fallo_ioreg.log`; `ioreg -r -c RTLAwdInterface -l > ~/Desktop/fallo_awdl.log` if it was born; `kextstat \| grep -i rtl`. The rest (panic report, `inert` rollback, `Backups/efi_phase5_*`) is correct: `Tools/phase5_deploy.sh:9,38-44,106-108` |
| H17 | T4 (GUION:83-87) | Minor | «Without `Timed out`» and «a single creation» without a counting command; the `%s` of `Timed out while waiting for interface` interpolates the role (`AWDL`), not `awdl0`, so grep by the constant part | Patterns §5 T4.2 and T4.4 | `/usr/bin/log show … \| grep -cE 'Timed out while waiting for interface'` → 0 and `grep -cE 'No AWDL interface found'` → 1 |

## 2. Correct steps

- **P0, build** (GUION:18): `./build_airlayer.sh` produces the object and the kext; the `nm -u … | grep -c`
  with 93 is correct (measured) and the citation to `AWDL_STAGE1_CODE_FIXES.md` §5 is verbatim.
- **Deployment** (GUION:30): env `RTL8814_LAYER_SRC`/`RTL8814_LAYER_MD5` and the pair `layer 2` match the
  script (`Tools/phase5_deploy.sh:28-29`, `:83`: `rtlair=2` = join + data); the md5 is computed on
  the fly, so no hash is pasted. The internal-mount caution is superfluous but harmless: the script
  requires it (`:33-36`), and the backup is automatic (`:38-44`).
- **T1.1** (GUION:39-41): `ioreg -l | grep -i RTLAirGate` is valid and the expected
  `open-validated` is the right one for 25G241 (measured on the live system; KnownBuilds.inc:16).
- **T1.2, background** (GUION:49): «creation failure without 30 s wait» is consistent with the
  patterns
  §2 (T1.2) and with the silent 102 of the handler with `rtlawdl=0` (`RTLAirLayer.cpp:586-587`); no
  kernel literal is expected on that path.
- **T2/T3, ioreg and ifconfig** (GUION:56-60): `ioreg -r -c RTLAwdInterface -l` uses the right
  class name (`AWDLInterface.cpp:94`) and with `-l`. MTU 1428 and 802.11 media are the code's
  (`AWDLInterface.cpp:198` writes `0x594` twice; `:192-193` media/`ifm_current` 0x80).
- **T3.5, syntax** (GUION:74): `sudo ifconfig awdl0 up; ping6 -c 3 -I awdl0 ff02::1` is right
  (ping6 accepts `-I interface`, measured) and the no-op rationale is in the code
  (`AWDLInterface.cpp:255-269`; queues enabled at `:317-331`).
- **T4, literals** (GUION:83-87): `Found new interface awdl0 with role AirLink`, no `Ignoring
  awdl0` and no `llw0` are the correct expectations (patterns §5; spec T6.2).
- **T5.1** (GUION:90-93): correct as a regression — the delta §6 lowers T5 to regression —; only
  note that the exact rendering of `ifconfig -v` for media 0x80 is unverified (`?`).
- **T7, identifier and rollback** (GUION:100, 108): `com.DrogaBox.driver.RTL8814AirLayer` is the
  real bundle id (`Info.plist:10`); the `inert` rollback and the `Backups/efi_phase5_*` exist in
  the script; saving the report from `/Library/Logs/DiagnosticReports/` is right given the stated
  risk of T3.5.

## 3. Coverage against spec §14 (what the runbook does not cover)

- **T2.1** (capability, byte `+0x08` bit 0): without a probe there is no user command for the
  `RawGet` 12;
  `Tools/test_uc.c` only talks to the native driver user client (telemetry/scan/join). Cover it
  «by effect»: the daemon only emits the 94 if the bit is published.
- **T2.2, return < 1 s**: it is the test of the synchronous `registerService` (delta §6, risk #90
  of spec §15); the ring does not mark times. Approximate with the timestamps of the daemon
  `log show` and note it as `?`.
- **T2.3** (duplicate → 17) and **T2.4** (role 7 → 102 + `awdl: invalid role 7`,
  `RTLAirLayer.cpp:583`): they require emitting the 94 manually; without a probe in the repo they
  are not executable. Note them as not covered or leave them for a custom probe.
- **T3.3** (`if_nametoindex` > 0): implicit in `ifconfig awdl0`; optional `netstat -I awdl0 -n`.
- **T5.2/T5.3**: no user command for the 97 (the spec marks it `?`); the kext does not log on that
  path either (audit §3, DOES NOT EMIT).
- **T6.2** (`ifconfig llw0` does not exist): covered sideways by «no `llw0`» in T4; an explicit
  step after the 60 s of H10 is worth adding.
- **T7.1**: see H11.

## 4. Final note

This document is the only modified file; the revised runbook and the rest of the tree stay intact.
