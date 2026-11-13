# Log patterns of the stage 1 tests T1–T7

Support doc of `AWDL_STAGE1_IMPL_SPEC.md` §14: it fixes the exact literals each hardware test must
search in the logs, so nobody has to guess strings at test time. The literals come exclusively
from these documents and code, cited by file:

- `Docs/AWDL_WIFIP2PD_LOGSTRINGS.md`
- `Docs/AWDL_WIFIP2PD_DISCOVERY.md`
- `Docs/AWDL_WIFIP2PD_POST_CREATE.md`
- `Docs/AWDL_LLW_COMPANION.md`
- `Docs/AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md`
- `Source/AirLayer/AWDLInterface.cpp` (`LOG` calls, cited by line)

Every literal none of those sources gives verbatim is marked `?` and collected in the final
table §9; no string is invented. The test ids (T1.1–T7.3) and the expectations are those of
`AWDL_STAGE1_IMPL_SPEC.md` §14; this document only adds the log patterns.

## 1. Log channels and command shape

Two channels depending on the emitter:

- **wifip2pd** (daemon, userspace): the system unified log.

  ```
  log show --last 3m --info --debug --predicate 'process == "wifip2pd"' | grep -E '<pattern>'
  ```

- **kernel** (kext `RTL8814AirLayer`): `IOLog` does not reach `log show` on 26.7.1 (comment of
  `Source/AirLayer/RTLAirLayer.cpp:20-24`); the `LOG` lines of `AWDLInterface.cpp` and the rest
  of the kext land in the `RTLAirLayerLog` ring of the IORegistry node `RTL8814Native` (200 lines at
  most, overwritten; read soon after the action):

  ```
  ioreg -r -c RTL8814Native -d 1 | grep -E '<pattern>'
  ```

Common notes:

- In the `ioreg` ring the lines go without a prefix (`awdl: born awdl0`); via `IOLog` they would
  carry the
  prefix `RTL8814AirLayer: `, a channel not observable with `log show` on this system.
- The wifip2pd literals are `os_log` format strings: `%s`/`%@` are substituted at runtime
  (e.g. `Found new interface %s with role %s` → `Found new interface awdl0 with role AirLink`;
  the string `AirLink` for role 6 is closed in `AWDL_KERNEL_ROLE_TABLE.md`, cited by
  `AWDL_WIFIP2PD_POST_CREATE.md` §1.2). The `grep`s use only the constant parts of the literal,
  to avoid depending on the interpolation.
- If the interpolated fields show as `<private>`, repeat with
  `sudo log show --private ...` (same predicate).
- **Absent** rows: same command with `grep -cE`; the expected output is `0`.
- The `--last 3m` window must cover the whole test action; widen it if the test lasts longer.
- The logger of the AirLink path uses the subsystem `com.apple.awdl`
  (`AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 7); alternative predicate:
  `--predicate 'subsystem == "com.apple.awdl"'`.
- The `log show` of spec §14 T4.1 uses `--last 2m` without `--info --debug`; here a 3 m window with
  `--info --debug` is used because the level of these messages is not documented.

## 2. T1 — Load and gate

| Test | Literal (verbatim) | Source | Process | Expected | Command |
|---|---|---|---|---|---|
| T1.1 | `?` — literal of the load gate; spec §14 T1.1 only gives fragments with ellipses (`RTLAirGate = "open-validated: ..."` and `gate open-validated`) | — (see §9, row 1) | kernel | present | `ioreg -r -c RTL8814Native -d 1 \| grep -iE 'gate'` (exploratory) |
| T1.2 | `?` — literal of the creation failure of the 94 in the daemon; spec §14 T1.2 leaves it `?`. Candidate: `Failed to create interface %s on %s with error %@`, which `AWDL_LLW_COMPANION.md` §2.1 gives verbatim but verified only for the epilogue of case 4 (companion) | `AWDL_LLW_COMPANION.md` §2.1 (site `0x100048fb5`) — applicability `?` (see §9, row 2) | wifip2pd | present (`?`) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create interface'` |
| T1.2 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | wifip2pd | **absent** — the create error branches signal the semaphore before exiting, with no 30 s wait (`AWDL_WIFIP2PD_POST_CREATE.md` §1.4, last point) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T1.2 | `?` — log of the 94 handler with `rtlawdl=0` (return 102) | — (see §9, row 3) | kernel | `?` | `ioreg -r -c RTL8814Native -d 1` (ring inspection) |
| T1.3 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | wifip2pd | **absent** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T1.3 | `awdl: born %s` → `awdl: born awdl0` | `Source/AirLayer/AWDLInterface.cpp:217` | kernel | present (same pattern as T3.4) | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: born'` |

## 3. T2 — Capability and gate

T2.1 has no log component: the check is the copyback of the `RawGet` 12 (byte `+0x08`
bit 0 at 1, byte `+0x0D` bit 0 at 0).

| Test | Literal (verbatim) | Source | Process | Expected | Command |
|---|---|---|---|---|---|
| T2.2 | `?` — `awdl: create role 6`, cited in spec §14 T2.2; no verbatim in the allowed sources (the 94 handler is outside `AWDLInterface.cpp`) | — (see §9, row 4) | kernel | present (`?`) | `ioreg -r -c RTL8814Native -d 1 \| grep -iE 'create'` (exploratory) |
| T2.2 | `awdl: registerEthernetInterface 0x%x` → `awdl: registerEthernetInterface 0x0` | `Source/AirLayer/AWDLInterface.cpp:199` | kernel | present; its appearance is the evidence of the return 0 in under 1 s | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: registerEthernetInterface'` |
| T2.3 | `?` — no documented literal for the duplicate case; the observation is the return 17 and the same name at `+0x10` | — (see §9, row 5) | kernel | `?` | — |
| T2.4 | `?` — no documented literal for the role 7 rejection (APSTA out of scope); the observation is the return 102 | — (see §9, row 3) | kernel | `?` | — |

## 4. T3 — Birth

T3.1–T3.3 have no log component (`ioreg -r -c RTLAwdInterface -l`, `ifconfig awdl0`,
`if_nametoindex("awdl0")`).

| Test | Literal (verbatim) | Source | Process | Expected | Command |
|---|---|---|---|---|---|
| T3.4 | `awdl: registerEthernetInterface 0x%x` → `awdl: registerEthernetInterface 0x0` | `Source/AirLayer/AWDLInterface.cpp:199` | kernel | present. Note: spec §14 T3.4 writes `awdl: registerEthernetInterface = 0x0` with an `=` the code does not produce; the real form has no `=` | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: registerEthernetInterface'` |
| T3.4 | `awdl: born %s` → `awdl: born awdl0` | `Source/AirLayer/AWDLInterface.cpp:217` | kernel | present | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: born'` |

Birth failure literals, all from `Source/AirLayer/AWDLInterface.cpp`, all from the kernel
process and all with **expected absence** on a correct birth (any one present indicates the failure
path that prevented it):

| Test | Literal (verbatim) | Line |
|---|---|---|
| T3.4 | `awdl: init() failed` | `AWDLInterface.cpp:105` |
| T3.4 | `awdl: base init failed` | `AWDLInterface.cpp:115` |
| T3.4 | `awdl: attach failed` | `AWDLInterface.cpp:120` |
| T3.4 | `awdl: packet pool alloc failed` | `AWDLInterface.cpp:144` |
| T3.4 | `awdl: packet queue alloc failed` | `AWDLInterface.cpp:152` |
| T3.4 | `awdl: no work queue` | `AWDLInterface.cpp:157` |
| T3.4 | `awdl: base start failed` | `AWDLInterface.cpp:169` |
| T3.4 | `awdl: reginfo alloc failed` | `AWDLInterface.cpp:174` |
| T3.4 | `awdl: datapath failed` | `AWDLInterface.cpp:194` |
| T3.4 | `awdl: no bsd name` | `AWDLInterface.cpp:212` |

Single command for the ten rows above (empty output = success):

```
ioreg -r -c RTL8814Native -d 1 | grep -E 'awdl: (init\(\) failed|base init failed|attach failed|packet pool alloc failed|packet queue alloc failed|no work queue|base start failed|reginfo alloc failed|datapath failed|no bsd name)'
```

## 5. T4 — Acceptance by the daemon

All rows are wifip2pd's (the process column is omitted for that reason).

| Test | Literal (verbatim) | Source | Expected | Command |
|---|---|---|---|---|
| T4.1 | `Found new interface %s with role %s` → `Found new interface awdl0 with role AirLink` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 8 (string `0x100494bb0`) | present | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Found new interface'` |
| T4.2 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | **absent**. Note: the `%s` interpolates the role name (`AWDL`), not `awdl0` (`AWDL_LLW_COMPANION.md` §2.2); the spec form `Timed out while waiting for interface awdl0` is not what the binary produces — hence the grep uses only the constant part | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T4.3 | `Ignoring %s because its not a WiFi interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 3 (string `0x100494b00`); also `AWDL_WIFIP2PD_DISCOVERY.md` | **absent** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'because its not a WiFi interface'` → `0` |
| T4.3 | `Ignoring %s because no interface index found` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 4 (string `0x100494b30`); also `AWDL_WIFIP2PD_DISCOVERY.md` | **absent** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'because no interface index found'` → `0` |
| T4.3 (compl.) | `Matched a service but unable to query its interface name` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 1 (string `0x100494ac0`) | absent (complement: `IOInterfaceName` missing in the registry) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'unable to query its interface name'` → `0` |
| T4.3 (compl.) | `Unable to find role for interface: %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 9 (string `0x100494b60`); also `AWDL_WIFIP2PD_DISCOVERY.md` | absent (complement: no role string matched) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Unable to find role for interface'` → `0` |
| T4.3 (compl.) | `Failed to bind to %s[%s]: %@` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 7 (string `0x100494b90`) | absent (complement: `Apple80211Open`/bind failure) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Failed to bind to'` → `0` |
| T4.4 | `No AWDL interface found. Creating AWDL interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 (string `0x1004a1350`); also `AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md` §1 | present exactly once per load cycle; more than one = re-creation loop | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found'` → `1` |

Combined sweep command for T4 (present and absents at once):

```
log show --last 3m --info --debug --predicate 'process == "wifip2pd"' | grep -E "Found new interface|Timed out while waiting|Ignoring |Unable to find role|Failed to bind|No AWDL interface found|unable to query"
```

## 6. T5 — Media and 97

T5.1 and T5.2 have no log on the documented path: T5.1 is the read of `ifm_current` via
`SIOCGIFMEDIA` and T5.2 the copyback of the `RawGet` 97 (parent name without NUL).

| Test | Literal (verbatim) | Source | Process | Expected | Command |
|---|---|---|---|---|---|
| T5.2 | `?` — log of the 97 handler in the kernel, if any | — (see §9, row 6) | kernel | `?` | — |
| T5.3 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | wifip2pd | **absent** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T5.3 | `?` — literal of the "parent error" in the daemon after the 97: no document of the list gives a string for the 97 failure | — (see §9, row 7) | wifip2pd | `?` | — |

## 7. T6 — Optional companion (decision D6: the kext does not implement the 293)

With D6 the modern dispatch returns 102 for the 293 and the failure is immediate, with no wait
(`AWDL_LLW_COMPANION.md` §2.1 and §4). T6.2 has no log (`ifconfig llw0`).

| Test | Literal (verbatim) | Source | Process | Expected | Command |
|---|---|---|---|---|---|
| T6.1 | `Creating companion low latency interface` | `AWDL_LLW_COMPANION.md` §1.3 step 4 (string `0x1004a36b0`, site `0x10041b48b`) | wifip2pd | present (precedes the creation attempt) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Creating companion low latency interface'` |
| T6.1 | `Failed to create low latency interface for %s because %@` → `Failed to create low latency interface for awdl0 because ...`; the `%@` is the code returned by the `RawSet` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 row 5 (string `0x100494cf0`); also `AWDL_LLW_COMPANION.md` §1.1 | wifip2pd | present (failure of the `RawSet` 293) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create low latency interface for'` |
| T6.1 | `Failed to create interface %s on %s with error %@`; the first `%s` carries the role name `Low Latency` | `AWDL_LLW_COMPANION.md` §2.1 (string `aFailedToCreate_1`, site `0x100048fb5`) | wifip2pd | present (`?` on the exact pair: spec §14 T6.1 counts "2 lines" and which two is not closed by the documents; see §9, row 8) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create interface'` |
| T6.1 | `Failed to create the companion low latency interface for AWDL: %@` | `AWDL_LLW_COMPANION.md` §2.3 (string `0x1004a75c0`, site `0x100417ead`) | wifip2pd | present (the `AWDLInterface` init absorbs the error and continues) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create the companion low latency interface for AWDL'` |
| T6.1 | `Registering for Power State changes from wifid` | `AWDL_LLW_COMPANION.md` §2.3 | wifip2pd | present — confirms the init continued after the failure, consistent with `awdl0` intact | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Registering for Power State changes from wifid'` |
| T6.1 | `Ignoring request to create AWDL companion low latency interface since it is already present: %s` | `AWDL_LLW_COMPANION.md` §1.3 step 4 (string `0x1004a7a00`) | wifip2pd | **absent** (no prior companion in stage 1) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Ignoring request to create AWDL companion'` → `0` |
| T6.1 | `Failed to create low latency interface because parent %s was not found` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 row 5 (string `0x100494ca0`); also `AWDL_LLW_COMPANION.md` §1.1 | wifip2pd | **absent** (the parent `awdl0` exists) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'because parent'` → `0` |
| T6.1/T6.3 | `Timed out while waiting for the low latency interface with parent %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 row 5 (string `0x1004939a0`); `AWDL_WIFIP2PD_LOGSTRINGS.md` §6; `AWDL_LLW_COMPANION.md` §2.2 (site `0x100048beb`) | wifip2pd | **absent** — the 30 s wait of the companion only runs if the 293 answers success without a verifiable name (`AWDL_LLW_COMPANION.md` §2.2); the D6 rejection is a fast path. The short form of spec T6.3 (`Timed out while waiting for the low latency interface`) is the constant prefix of this string | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for the low latency interface'` → `0` |
| T6.1 (compl.) | `Failed to find the interface name returned when creating the low latency interface for %s` | `AWDL_WIFIP2PD_LOGSTRINGS.md` §6 (string `0x100493940`) | wifip2pd | absent (complement: only the "success without verifiable name" path produces it, avoided with D6) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Failed to find the interface name returned'` → `0` |

## 8. T7 — Monitoring and unload

| Test | Literal (verbatim) | Source | Process | Expected | Command |
|---|---|---|---|---|---|
| T7.1 | `?` — literals of the monitoring registration (`Apple80211StartMonitoringEvent` / `Apple80211RawP2PEventMonitoringInit`): they are in `AWDL_WIFIP2PD_P2P_EVENTS.md`, which is not in the allowed source list of this document | — (see §9, row 9) | wifip2pd | `?` | — |
| T7.1 | `No AWDL interface found. Creating AWDL interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 (string `0x1004a1350`) | wifip2pd | **absent** after the first creation: once in total; reappearing = re-creation of `awdl0` | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found'` → `1` |
| T7.2 | `?` — literal of the handle cleanup on receiving `IOServiceTerminate`: no documented log (`AWDL_WIFIP2PD_POST_CREATE.md` §1.1 describes the cleanup without a known signal or string) | — (see §9, row 10) | wifip2pd | `?` | — |
| T7.2 | `awdl: born %s` | `Source/AirLayer/AWDLInterface.cpp:217` | kernel | **absent** after the unload (it must not reappear without a new load) | `ioreg -r -c RTL8814Native -d 1 \| grep -cE 'awdl: born'` |
| T7.2 | `Attempting recovery for %s[%s] with %s on %s` | `AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md` §4.4 (string `0x100047156`) | wifip2pd | `?` diagnostic: if it appears, the unload triggered daemon recovery instead of a clean cleanup | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Attempting recovery for'` |
| T7.2 | `awdl: enable 0x%x` / `awdl: disable 0x%x` | `Source/AirLayer/AWDLInterface.cpp:296` and `:302` | kernel | `?` diagnostic (when the family invokes them is not documented) | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: (enable\|disable)'` |
| T7.3 | `awdl: registerEthernetInterface 0x0` and `awdl: born awdl0` | `Source/AirLayer/AWDLInterface.cpp:199` and `:217` | kernel | present exactly once per load cycle (5 cycles → 5 and 5); a double registration in the same cycle is a defect | `ioreg -r -c RTL8814Native -d 1 \| grep -cE 'awdl: registerEthernetInterface'` → `5` |
| T7.3 | `No AWDL interface found. Creating AWDL interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 (string `0x1004a1350`) | wifip2pd | present once per load cycle | `log show --last 5m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found'` → `5` |
| T7.3 | `Already discovered %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 step 2 (string `0x100494be0`) | wifip2pd | `?` diagnostic: accumulation across cycles would indicate residual daemon state | `log show --last 5m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Already discovered'` |
| T7.3 | `Failed to recover the low latency interface %s: %@` | `AWDL_LLW_COMPANION.md` §2.3 (string `0x1004a7b70`, site `0x100419a2a`) | wifip2pd | `?` diagnostic (the companion recovery must not fire in the cycles) | `log show --last 5m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to recover the low latency interface'` |

## 9. Literals without a verbatim source (`?`)

No literal was invented; the `?`s of spec §14 are inherited and extended with what the allowed
sources of this document do not cover.

| # | Test | Searched literal | Why it is `?` | Where it would be resolved |
|---|---|---|---|---|
| 1 | T1.1 | Complete literal of the load gate (`RTLAirGate = "open-validated: ..."` and the line `gate open-validated`; spec §14 T1.1 only gives fragments with ellipses) | No allowed source gives it verbatim | Gate code in `Source/AirLayer/RTLAirLayer.cpp`, outside this document's source list |
| 2 | T1.2 | Literal of the creation failure of the 94 in wifip2pd (spec §14 T1.2 itself marks it `?`) | The candidate `Failed to create interface %s on %s with error %@` (`AWDL_LLW_COMPANION.md` §2.1) is verified only for the epilogue of case 4 (companion), not the AWDL path | Error epilogue of case 1 (AWDL) of `sub_100047660` in `wifip2pd` |
| 3 | T1.2 / T2.4 | Log of the 94 handler with `rtlawdl=0` or role 7 (returns 102) | No literal in the allowed sources | 94 handler in `Source/AirLayer/RTLAirLayer.cpp` |
| 4 | T2.2 | `awdl: create role 6` (spec §14 T2.2 cites it) | No verbatim in the allowed sources (the 94 handler is not in `AWDLInterface.cpp`) | 94 handler in `Source/AirLayer/RTLAirLayer.cpp` |
| 5 | T2.3 | Log of the duplicate case of the 94 (return 17) | No literal documented in any source of the list | 94 handler in `Source/AirLayer/RTLAirLayer.cpp` |
| 6 | T5.2 | Log of the 97 handler in the kernel | No literal documented | Kernel `getVIRTUAL_IF_PARENT` handler; or own probe |
| 7 | T5.3 | Literal of the "parent error" in the daemon after the 97 | No document of the list documents a log for the 97 failure | Closure of the `RawGet` 97 in the discoverer of `wifip2pd` (`sub_100046D30` / `sub_100043720`) |
| 8 | T6.1 | Which two exact lines are the "(2 lines)" of spec §14 T6.1 | The three candidates are cited verbatim in the §7 table; the spec count does not close unambiguously with them | Hardware test: count the real lines |
| 9 | T7.1 | Literals of the result of the event monitoring registration | They are in `AWDL_WIFIP2PD_P2P_EVENTS.md`, not included in this document's allowed source list | `AWDL_WIFIP2PD_P2P_EVENTS.md` |
| 10 | T7.2 | Literal of the handle cleanup on receiving `IOServiceTerminate` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.1 documents the cleanup without a known log | Consumer of `IOServiceTerminate` in `wifip2pd` (`sub_100045520`) |
