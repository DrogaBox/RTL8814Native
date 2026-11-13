# Lifecycle of AWDLInterface in wifip2pd

Sequence of discovery → creation → configuration → (absence of) destruction of the `wifip2pdCore.AWDLInterface` object, with the Apple80211 calls and ioctls in order. Base: `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), attached rea/IDA 2026-10-28 + prior project docs cited by path. Anything unseen goes as `?`. Names only when the binary carries them (Swift metadata accessors); the rest `sub_XXXX`.

## Swift types identified (metadata accessors, observed as callees)

| Symbol | Address | Meaning |
|---|---|---|
| `$s12wifip2pdCore13AWDLInterfaceCMa` | `0x10041c460` | class `wifip2pdCore.AWDLInterface` |
| `$s12wifip2pdCore16AWDLNetworkAgentCMa` | `0x10039cac0` | class `wifip2pdCore.AWDLNetworkAgent` |
| `$s12wifip2pdCore13P2PControllerC10ComponentsVMa` | `0x100367340` | struct `wifip2pdCore.P2PController.Components` |
| `$s7CoreP2P13WiFiInterfaceCMa` | `0x100287ad0` | class `CoreP2P.WiFiInterface` |
| `$s7CoreP2P8P2PTimerCMa` | `0x100179390` | class `CoreP2P.P2PTimer` |
| `$s3__C30AWDLTrafficRegistrationServiceVMa` | `0x100006d90` | struct `AWDLTrafficRegistrationService` (C) |
| `$s12wifip2pdCore13AWDLInterfaceC40AdditionalTrafficRegistrationInformationVMa` | `0x100436490` | nested struct of `AWDLInterface` |
| `$s12wifip2pdCore13AWDLInterfaceC45TrafficRegistrationServiceAvailabilityMonitorVMa` | `0x100436280` | nested struct of `AWDLInterface` |
| `$s12wifip2pdCore13AWDLInterfaceC21StateMonitoringClientVMa` | `0x1004364d0` | nested struct of `AWDLInterface` |
| `$s7CoreP2P13WiFiInterfaceC22NeighborDiscoveryTableVMa` | `0x100287ac0` | struct `CoreP2P.WiFiInterface.NeighborDiscoveryTable` |

## 1) Discovery — `sub_10036C2B0` (0x4d5 B, `0x10036c2b0`)

- Documented behavior (`AWDL_ROLE6_RE.md` §sub_10036C2B0): looks up/queries its AWDL interface; if not found, it emits "No AWDL interface found. Creating AWDL interface" and triggers the creation task.
- Callees observed now: `sub_1003654A0` (the creation task), `$s7CoreP2P11PreferencesVMa` (`0x1000df000`), `Logger.logObject`, `__os_log_impl`, `Dispatch._dispatchPreconditionTest`, `NSObject.==`, `swift_getAssociatedTypeWitness/ConformanceWitness` — consistent with preferences query + log + object comparison.
- The exact Swift → creation ioctl connection stayed **unverified** in the prior sample (`AWDL_ROLE6_RE.md`); it remains so (`?`).

## 2) Creation task — `sub_1003654A0` (0x529 B, `0x1003654a0`)

- Callees: metadata accessors of **`wifip2pdCore.AWDLNetworkAgent`** (`0x10039cac0`) and **`wifip2pdCore.AWDLInterface`** (`0x10041c460`), `P2PController.Components` (`0x100367340`), `Preferences` (`0x1000df000`), `sub_10040F300`, `sub_10001CB80`, `sub_1000DA780/900`. It builds/associates the `AWDLInterface` object (and the `AWDLNetworkAgent`) inside `P2PController.Components`.
- How it connects to the driver's `VIRTUAL_IF_CREATE`: the `RawSet` type 94 with role 6 is done by `sub_100047660` (`0x100048d95`, see section 4); the exact chaining `sub_1003654A0` → handle → `RawSet` was not traced in this pass (`?`).

## 3) Object initialization — `sub_100416D00` (0x15ab B, `0x100416d00`) and companion — `sub_10041B1F0` (0x598 B, `0x10041b1f0`)

- `sub_100416D00` (Swift initializer of `AWDLInterface`; `AWDL_ROLE6_RE.md`: it creates the `AWDLInterface` state and calls `sub_10041B1F0`; if that call fails it writes "Failed to create the companion low latency interface for AWDL" and continues). Callees observed now confirm the state construction: `Logger(subsystem:category:)` (`0x100444832`), `AWDLInterface.AdditionalTrafficRegistrationInformation` (`0x100436490`), `AWDLInterface.TrafficRegistrationServiceAvailabilityMonitor` (`0x100436280`), `AWDLInterface.StateMonitoringClient` (`0x1004364d0`), `AWDLTrafficRegistrationService` (`0x100006d90`), `WiFiInterface.NeighborDiscoveryTable` (`0x100287ac0`), `P2PTimer` (`0x100179390`), `DispatchTime.uptimeNanoseconds`, `objc_allocWithZone`, `bridgeErrorToNSError`. It also calls `sub_10041B1F0` (companion) and `sub_10041B7F0`, `sub_1004292F0`, `sub_10040EB90`, `sub_100435C80`.
- `sub_10041B1F0` (creation of the low latency companion interface): callees include the metadata of **`CoreP2P.WiFiInterface`** (`0x100287ad0`) and `WiFiInterface.NeighborDiscoveryTable` (`0x100287ac0`), plus `sub_1004155C0` (0x225), `sub_100286E10`, `sub_10027DE30`, `sub_100282E30`, `Logger` info/error — it builds the companion interface over a `WiFiInterface`.

## 4) Configuration: Apple80211 calls and ioctls in observable order

The following order is what can be sustained with the caller chains; it does not imply it is the only sequence at runtime.

1. **Handle per role** (`sub_100047660`, `0x100047660`): `RawGet` type **12** len 21 (`CARD_CAPABILITIES`, gate) ×3 branches; `RawSet` type **94** len 32 (`VIRTUAL_IF_CREATE`) with buffer: u32 @+0, MAC @+4..+9, **role @+0x0C = 6 (AWDL)** or 0x0A; semaphore wait 30 s (doc: `AWDL_WIFIP2PD_REQUESTS.md`). The event releasing it stays pending (`?`; see `AWDL_WIFIP2PD_P2P_EVENTS.md` §3).
2. **Discovery/validation** (`sub_100043720` ← `sub_100047660` at `0x100047f09`/`0x100047f2c`): `SIOCGIFMEDIA` (`0xC02C6938`) and check `IFM_TYPE==IFM_IEEE80211` (`0x100043ccc-0x100043cde`).
3. **Event registration** (`sub_100041B60` ← `sub_1000421E0`/`sub_100049F20`/`sub_10004A0C0`): via `sub_100041EB0`+16 ids of `Apple80211StartMonitoringEvent`, or via `Apple80211RawP2PEventMonitoringInit` (`sub_100041F20`). See `AWDL_WIFIP2PD_P2P_EVENTS.md`.
4. **Recovery** (`sub_100046E30`, `0x100046e30`): log `"Attempting recovery for %s[%s] with %s on %s"` (`0x100047156`) with roles `"Infrastructure"/"NME"/"NAN Data"/"Low Latency"`, re-creates the handle (`sub_100047660` at `0x1000471be`) and re-arms the monitoring (`sub_1000421E0` at `0x10004724e`); on failure log `"creation failed"`/`"WiFi-Aware Discovery"` (`0x10004728c-0x1000472b0`).
5. **Post-create requests by RawGet/RawSet**: `RawGet` type **97** (wrapper `sub_100046D30`, called by `sub_100068C20`) and `RawSet` type **293** len 24 (`sub_10004B300`, `0x10004b3e6`, followed by `if_nametoindex`) — not attributed to AWDL (`?`).
6. **Configuration by direct ioctl over an AF_INET6 socket**: pair 131 (GET 288 B) → 130 (SET 128 B with `apple80211req` type **7708**), in `sub_100058420`/`sub_1000586D0` (see `AWDL_WIFIP2PD_IOCTLS.md` §3-6). Exact AWDL operation: `?` (id 7708 unnamed).
7. **Interface management via the dispatcher** (`sub_100046A30`): `SIOCSIFFLAGS` up/down (`sub_100056990`), `SIOCSIFLLADDR` (MAC, `sub_100057120`), `SIOCSIFADDR`/`SIOCDIFADDR` (`sub_1000578F0`), `SIOCADDMULTI`/`SIOCDELMULTI` (`sub_100057DE0` + thunks `sub_10005F7D0`/`sub_10005F800`).

## 5) Destruction: VIRTUAL_IF_DELETE **does not appear**

- The binary has exactly **two** `Apple80211RawSet` sites (documented in `AWDL_WIFIP2PD_REQUESTS.md` and not refuted here): `0x100048d95` (in `sub_100047660`, type 94 = `VIRTUAL_IF_CREATE`) and `0x10004b3e6` (in `sub_10004B300`, type 293). **There is no `RawSet` type 95 (`VIRTUAL_IF_DELETE`)**.
- None of the 9 ioctls of the table (`0x100493008`) is a virtual interface delete; the closest observed are `SIOCSIFFLAGS` with flags without `IFF_UP` (`sub_100056990` with bool=0 → flags `& 0xFFBE` without `|0x41`) and `SIOCDIFADDR` (`sub_1000578F0` with an optional nil).
- `Apple80211EventMonitoringHalt` is used (`sub_100042120`, doc `AWDL_WIFIP2PD_REQUESTS.md`), and there is a partial teardown log during recovery: `"Failed to clear all previous peer presence after interface recovery"` (`0x1004a7ae0`). A route of complete deletion of the `AWDLInterface` (Swift deinit/destructor) was not located in this pass (`?`).
