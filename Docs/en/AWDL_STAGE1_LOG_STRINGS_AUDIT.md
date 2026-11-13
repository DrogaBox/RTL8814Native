# Audit of the log strings of stage 1

Contrast between the patterns `AWDL_STAGE1_TEST_LOG_PATTERNS.md` expects and the strings the
code really emits. Audited sources: `Source/AirLayer/AWDLInterface.cpp` (complete, its 14
`LOG` calls), the AWDL parts of `Source/AirLayer/RTLAirLayer.cpp` (handler of the 94, creation and
teardown of `awdl0`, startup with `rtlawdl`), the load gate (`Source/AirLayer/VersionGate.h`
with `GateLogic.h` and `KnownBuilds.inc`) and the log path (`airLog`/`rtlAirLog`). No audited file was modified.

## 1. Verdicts and conventions

The literals were checked against the current tree (commit `439c713`). The line numbers of the tables correspond to a tree previous to the inert action of the completion queues and have shifted a few lines in `AWDLInterface.cpp` (e.g. `born` at `:225`, `enable`/`disable` at `:319`/`:327`); the texts did not change.

| Verdict | Meaning |
|---|---|
| VERBATIM | the code emits the exact literal the doc looks for |
| OTHER TEXT | the path emits a log, with text different from the searched one |
| EMITS NOTHING | no log call on that path |
| wifip2pd | the emitter of the literal is the daemon binary, not the kext; outside the audited sources |

- `?` marks what these sources do not close: everything about the `wifip2pd` binary and the
  values interpolated at runtime (BSD names, error codes).
- "Line" cites the current tree (`file:line`); the format string of the call is audited, and the interpolated
  value is noted separately when it matters.
- The §1 note of the patterns doc about the prefix is also verified, and the currency of its
  line citations.

## 2. Prefix of the macro (note §1 of the patterns doc: confirmed)

- `LOG` at `AWDLInterface.cpp:24` points to `rtlAirLog` (`RTLAirLayer.cpp:62-70`), which formats the
  buffer and calls `airLog("%s", buf)` without adding anything.
- `airLog` (`RTLAirLayer.cpp:31-56`) stores the buffer as-is into the ring `RTLAirLayerLog`
  (200 lines, `RTLAirLayer.cpp:25-26`) of node `RTL8814Native`; the prefix `RTL8814AirLayer: `
  is only added in the `IOLog` branch (`RTLAirLayer.cpp:38`), a channel not observable with `log show` on
  this system.
- Conclusion: the `grep`s of the patterns doc without prefix are correct for the ring.

## 3. Kernel patterns

| Test | Doc pattern | Real string of the code | Line | Verdict |
|---|---|---|---|---|
| T1.1 (frag. 1) | `RTLAirGate = "open-validated: ..."` | property `RTLAirGate` = `snprintf("%s: %s", gateVerdictName(v), why)` | `VersionGate.h:57` and `:81` | VERBATIM (conditional, see below) |
| T1.1 (frag. 2) | `gate open-validated` | `LOG("gate %s: %s", gateVerdictName(v), why)` | `VersionGate.h:108` | VERBATIM (conditional, see below) |
| T1.2 | `?` log of the handler of the 94 with `rtlawdl=0` (return 102) | — | `RTLAirLayer.cpp:586-587` | EMITS NOTHING: with `rtlawdl=0` (`awdlAllowed()`, `RTLAirLayer.hpp:182`) the handler returns `kIOReturnUnsupported` silently |
| T1.2 (candidate) | `Failed to create interface %s on %s with error %@` | — | — | wifip2pd; the kext does not emit it; applicability to the AWDL path of the daemon remains `?` |
| T1.3 | `awdl: born %s` → `awdl: born awdl0` | `LOG("awdl: born %s", _bsdName)` | `AWDLInterface.cpp:217` | VERBATIM the format; the interpolated value is composed by the family (`?` until hardware, expected `awdl0` by prefix+unit 0 of the RegInfo) |
| T2.2 | `awdl: create role 6` | — | — | EMITS NOTHING: that string does not exist; the success of the 94 logs `awdl: created %s` (`RTLAirLayer.cpp:1809`) |
| T2.2 | `awdl: registerEthernetInterface 0x%x` → `0x0` | `LOG("awdl: registerEthernetInterface 0x%x", r)` | `AWDLInterface.cpp:199` | VERBATIM the format; the `0x0` comes from `kIOReturnSuccess` on the correct path |
| T2.3 | `?` duplicated case (return 17) | — | `RTLAirLayer.cpp:1787-1791` | EMITS NOTHING: the duplicated path copies the name into `out` and returns `EEXIST` without a log |
| T2.4 | `?` rejection of role 7 (return 102) | `LOG("awdl: invalid role %u", req->role)` → `awdl: invalid role 7` | `RTLAirLayer.cpp:583` | OTHER TEXT (resolves the `?`: there is a log, with this text) |
| T3.4 | `awdl: registerEthernetInterface 0x%x` | same as T2.2 | `AWDLInterface.cpp:199` | VERBATIM; confirmed it does not produce the `=` spec §14 T3.4 wrote |
| T3.4 | `awdl: born %s` | same as T1.3 | `AWDLInterface.cpp:217` | VERBATIM |
| T3.4 | `awdl: init() failed` | same | `AWDLInterface.cpp:105` | VERBATIM |
| T3.4 | `awdl: base init failed` | same | `AWDLInterface.cpp:115` | VERBATIM |
| T3.4 | `awdl: attach failed` | same | `AWDLInterface.cpp:120` | VERBATIM |
| T3.4 | `awdl: packet pool alloc failed` | same | `AWDLInterface.cpp:144` | VERBATIM |
| T3.4 | `awdl: packet queue alloc failed` | same | `AWDLInterface.cpp:152` | VERBATIM |
| T3.4 | `awdl: no work queue` | same | `AWDLInterface.cpp:157` | VERBATIM |
| T3.4 | `awdl: base start failed` | same | `AWDLInterface.cpp:169` | VERBATIM |
| T3.4 | `awdl: reginfo alloc failed` | same | `AWDLInterface.cpp:174` | VERBATIM |
| T3.4 | `awdl: datapath failed` | same | `AWDLInterface.cpp:194` | VERBATIM |
| T3.4 | `awdl: no bsd name` | same | `AWDLInterface.cpp:212` | VERBATIM |
| T5.2 | `?` log of the handler of the 97 in the kernel | — | `RTLAirLayer.hpp:99-100` | EMITS NOTHING: the kext declares no handler of the 97 (only override of the VIRTUAL_IF family: `setVIRTUAL_IF_CREATE`); the request is resolved by the family without a kext log |
| T7.2 | `awdl: born %s` (expected absence) | same as T1.3 | `AWDLInterface.cpp:217` | VERBATIM |
| T7.2 | `awdl: enable 0x%x` / `awdl: disable 0x%x` | `LOG("awdl: enable 0x%x", opts)` / `LOG("awdl: disable 0x%x", opts)` | `AWDLInterface.cpp:311` and `:319` | VERBATIM; the doc citations (`:296` and `:302`) were left stale by the queues commit (`e50d7cf`) |
| T7.3 | `awdl: registerEthernetInterface 0x0` and `awdl: born awdl0` | same as T2.2 / T1.3 | `AWDLInterface.cpp:199` and `:217` | VERBATIM the format; the 5+5 count subject to the interpolated values (`?` until hardware) |

Conditions of T1.1: the gate only decides once, at the first `airGateOpen()` (`VersionGate.h:91-111`),
which is reached with the boot-arg `rtlair` armed (`RTLAirLayer.cpp:1392-1396`). With a matching
structure and a validated build the verdict is `open-validated` (`GateLogic.h:69,120`); the running
build (25G241) is the only entry of the validated list (`KnownBuilds.inc:16-17`). With a different
structure the texts would be `closed-structure` / `closed-unknown-build` (`GateLogic.h:72-73`). The
property `RTLAirGate` is a pair of node `RTL8814Native` (not a line of the ring), but the
exploratory `ioreg` of the doc shows it anyway next to `RTLAirGateBuild` (`VersionGate.h:82`).

## 4. wifip2pd patterns (emitter: the daemon, not the kext)

None of these literals appears in the kext code; all of them come from the `wifip2pd` binary
cited in the RE documents. The kext can neither emit them nor prevent them; the presence/absence expectations of the doc depend on the daemon and on the hardware.

| Test | Literal | Verdict |
|---|---|---|
| T1.2 | `Failed to create interface %s on %s with error %@` | wifip2pd; applicability to the AWDL path `?` |
| T1.2/T1.3/T4.2/T5.3 | `Timed out while waiting for interface %s` | wifip2pd |
| T4.1 | `Found new interface %s with role %s` | wifip2pd |
| T4.3 | `Ignoring %s because its not a WiFi interface` | wifip2pd |
| T4.3 | `Ignoring %s because no interface index found` | wifip2pd |
| T4.3 | `Matched a service but unable to query its interface name` | wifip2pd |
| T4.3 | `Unable to find role for interface: %s` | wifip2pd |
| T4.3 | `Failed to bind to %s[%s]: %@` | wifip2pd |
| T4.4/T7.1/T7.3 | `No AWDL interface found. Creating AWDL interface` | wifip2pd |
| T5.3 | `?` parent error after the 97 | still `?`: no source gives it |
| T6.1 | `Creating companion low latency interface` | wifip2pd |
| T6.1 | `Failed to create low latency interface for %s because %@` | wifip2pd |
| T6.1 | `Failed to create interface %s on %s with error %@` (peer `Low Latency`) | wifip2pd |
| T6.1 | `Failed to create the companion low latency interface for AWDL: %@` | wifip2pd |
| T6.1 | `Registering for Power State changes from wifid` | wifip2pd |
| T6.1 | `Ignoring request to create AWDL companion low latency interface since it is already present: %s` | wifip2pd |
| T6.1 | `Failed to create low latency interface because parent %s was not found` | wifip2pd |
| T6.1/T6.3 | `Timed out while waiting for the low latency interface with parent %s` | wifip2pd |
| T6.1 | `Failed to find the interface name returned when creating the low latency interface for %s` | wifip2pd |
| T7.1 | `?` literals of the monitoring registration | still `?` (`AWDL_WIFIP2PD_P2P_EVENTS.md`) |
| T7.2 | `?` handle cleanup in `IOServiceTerminate` | still `?` |
| T7.2 | `Attempting recovery for %s[%s] with %s on %s` | wifip2pd |
| T7.3 | `Already discovered %s` | wifip2pd |
| T7.3 | `Failed to recover the low latency interface %s: %@` | wifip2pd |

## 5. LOG the code emits and the patterns doc does not mention

`AWDLInterface.cpp` contributes none: its 14 calls are all covered. The following are
from the kext, on the AWDL path and the gate:

| String (format) | Line | When it comes out | Note |
|---|---|---|---|
| `awdl: invalid role %u` | `RTLAirLayer.cpp:583` | 94 with a role other than 6 | The doc looked for it as a `?` without a literal (§9 row 3); the real text is this |
| `awdl: alloc failed` | `RTLAirLayer.cpp:1795` | `OSTypeAlloc` without an object in `createAwdlInterface` | Failure of the 94 not covered by the T3.4 rows |
| `awdl: init failed` | `RTLAirLayer.cpp:1799` | `initWithController` or `startWithRegistration` fail | Different from `awdl: base init failed` of `AWDLInterface.cpp:115` |
| `awdl: created %s` | `RTLAirLayer.cpp:1809` | 94 with role 6 successful | Only success log of the 94; the doc does not list it (T1.2/T2.2/T4.1 could use it) |
| `awdl: teardown` | `RTLAirLayer.cpp:1830` | `destroyAwdlInterface` (stop or end) | Relevant for T7.2: footprint of the unload in the ring |
| `gate %s: %s` | `VersionGate.h:108` | first `airGateOpen()` | It was listed as a fragment `?` (§9 row 1); real text `gate <verdict>: <reason>` |
| `version gate closed, staying out (en2 is unaffected)` | `RTLAirLayer.cpp:1397` | gate closed | Changes the T1.1 expectation if the system does not validate |
| `start 0.32, rtlair=%u rtlairstep=%u rtlawdl=%u` | `RTLAirLayer.cpp:1457` | startup of the controller | Echo of the `rtlawdl`; useful triage for T1.2/T1.3 |
| `@%llu stub %s` | `RTLAirLayer.cpp:131` | first call of each stub, dumped by the flush thread | On the first 94 there appears `@<ms> stub setVIRTUAL_IF_CREATE`; generic tracing, not AWDL |
| `not armed (boot-arg rtlair=1, or 2 with data path), staying out` | `RTLAirLayer.cpp:1393` | boot without `rtlair` | Adjacent to T1: without `rtlair` there is neither gate nor `awdl0` |

The rest of the `LOG` calls of `RTLAirLayer.cpp` (STA/WCL path: scan, association, join,
roam, CoreCapture) are outside the AWDL scope of this audit.

## 6. Currency of the line citations of the patterns doc

All the `AWDLInterface.cpp` citations of the doc (105, 115, 120, 144, 152, 157, 169, 174, 194, 199,
212, 217) remain exact. The only stale ones are those of `enable`/`disable` in §8 T7.2
(`:296` and `:302` versus the real `:311` and `:319`, due to the queues commit `e50d7cf`). The
literals themselves are correct in both cases.

## 7. Resolution of the `?` rows of the patterns doc (§9)

| # | Test | Status after the audit |
|---|---|---|
| 1 | T1.1 | Resolved: property `RTLAirGate` (`VersionGate.h:81`, text `"<verdict>: <why>"` from `:57`) and line `gate <verdict>: <why>` (`:108`). With a matching structure and build 25G241 the verdict is `open-validated` (`GateLogic.h:69`; `KnownBuilds.inc:16-17`) |
| 2 | T1.2 | Still `?`: error epilogue of the AWDL case in the `wifip2pd` binary |
| 3 | T1.2 / T2.4 | Half resolved: role 7 does log (`awdl: invalid role 7`, `RTLAirLayer.cpp:583`); `rtlawdl=0` logs nothing (`RTLAirLayer.cpp:586-587`) |
| 4 | T2.2 | Resolved: `awdl: create role 6` does not exist; the success logs `awdl: created %s` (`RTLAirLayer.cpp:1809`) |
| 5 | T2.3 | Resolved: the duplicated case returns `EEXIST` without a log (`RTLAirLayer.cpp:1787-1791`) |
| 6 | T5.2 | Resolved: there is no handler of the 97 in the kext (`RTLAirLayer.hpp:99-100`), so no kext log on that path |
| 7 | T5.3 | Still `?`: closure of the `RawGet` 97 in the `wifip2pd` binary |
| 8 | T6.1 | Still `?`: count of the "2 lines", only closes on hardware |
| 9 | T7.1 | Still `?`: literals in `AWDL_WIFIP2PD_P2P_EVENTS.md` |
| 10 | T7.2 | Still `?`: handle cleanup in the `wifip2pd` binary |
