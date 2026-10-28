# Ciclo de vida de AWDLInterface en wifip2pd

Secuencia de descubrimiento → creación → configuración → (ausencia de) destrucción del objeto `wifip2pdCore.AWDLInterface`, con las llamadas Apple80211 e ioctls en orden. Base: `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), rea/IDA adjunto 2026-10-28 + docs previas del proyecto citadas por ruta. Lo no visto va como `?`. Nombres sólo cuando el binario los trae (metadata accessors Swift); el resto `sub_XXXX`.

## Tipos Swift identificados (metadata accessors, observados como callees)

| Símbolo | Dirección | Significado |
|---|---|---|
| `$s12wifip2pdCore13AWDLInterfaceCMa` | `0x10041c460` | clase `wifip2pdCore.AWDLInterface` |
| `$s12wifip2pdCore16AWDLNetworkAgentCMa` | `0x10039cac0` | clase `wifip2pdCore.AWDLNetworkAgent` |
| `$s12wifip2pdCore13P2PControllerC10ComponentsVMa` | `0x100367340` | struct `wifip2pdCore.P2PController.Components` |
| `$s7CoreP2P13WiFiInterfaceCMa` | `0x100287ad0` | clase `CoreP2P.WiFiInterface` |
| `$s7CoreP2P8P2PTimerCMa` | `0x100179390` | clase `CoreP2P.P2PTimer` |
| `$s3__C30AWDLTrafficRegistrationServiceVMa` | `0x100006d90` | struct `AWDLTrafficRegistrationService` (C) |
| `$s12wifip2pdCore13AWDLInterfaceC40AdditionalTrafficRegistrationInformationVMa` | `0x100436490` | struct anidado de `AWDLInterface` |
| `$s12wifip2pdCore13AWDLInterfaceC45TrafficRegistrationServiceAvailabilityMonitorVMa` | `0x100436280` | struct anidado de `AWDLInterface` |
| `$s12wifip2pdCore13AWDLInterfaceC21StateMonitoringClientVMa` | `0x1004364d0` | struct anidado de `AWDLInterface` |
| `$s7CoreP2P13WiFiInterfaceC22NeighborDiscoveryTableVMa` | `0x100287ac0` | struct `CoreP2P.WiFiInterface.NeighborDiscoveryTable` |

## 1) Descubrimiento — `sub_10036C2B0` (0x4d5 B, `0x10036c2b0`)

- Comportamiento documentado (`AWDL_ROLE6_RE.md` §sub_10036C2B0): busca/consulta su interfaz AWDL; si no la encuentra emite `"No AWDL interface found. Creating AWDL interface"` y dispara la tarea de creación.
- Callees observados ahora: `sub_1003654A0` (la tarea de creación), `$s7CoreP2P11PreferencesVMa` (`0x1000df000`), `Logger.logObject`, `__os_log_impl`, `Dispatch._dispatchPreconditionTest`, `NSObject.==`, `swift_getAssociatedTypeWitness/ConformanceWitness` — coherente con consulta de preferencias + log + comparación de objeto.
- La conexión exacta Swift → ioctl de creación quedó **no verificada** en la muestra previa (`AWDL_ROLE6_RE.md`); sigue igual (`?`).

## 2) Tarea de creación — `sub_1003654A0` (0x529 B, `0x1003654a0`)

- Callees: metadata accessors de **`wifip2pdCore.AWDLNetworkAgent`** (`0x10039cac0`) y **`wifip2pdCore.AWDLInterface`** (`0x10041c460`), `P2PController.Components` (`0x100367340`), `Preferences` (`0x1000df000`), `sub_10040F300`, `sub_10001CB80`, `sub_1000DA780/900`. Construye/asocia el objeto `AWDLInterface` (y el `AWDLNetworkAgent`) dentro del `P2PController.Components`.
- Cómo se conecta con el `VIRTUAL_IF_CREATE` del driver: el `RawSet` tipo 94 con rol 6 lo hace `sub_100047660` (`0x100048d95`, ver sección 4); el encadenado exacto `sub_1003654A0` → handle → `RawSet` no se trazó en esta pasada (`?`).

## 3) Inicialización del objeto — `sub_100416D00` (0x15ab B, `0x100416d00`) y companion — `sub_10041B1F0` (0x598 B, `0x10041b1f0`)

- `sub_100416D00` (inicializador Swift de `AWDLInterface`; `AWDL_ROLE6_RE.md`: crea el estado de `AWDLInterface` y llama a `sub_10041B1F0`; si esa llamada falla escribe `"Failed to create the companion low latency interface for AWDL"` y continúa). Callees observados ahora confirman la construcción del estado: `Logger(subsystem:category:)` (`0x100444832`), `AWDLInterface.AdditionalTrafficRegistrationInformation` (`0x100436490`), `AWDLInterface.TrafficRegistrationServiceAvailabilityMonitor` (`0x100436280`), `AWDLInterface.StateMonitoringClient` (`0x1004364d0`), `AWDLTrafficRegistrationService` (`0x100006d90`), `WiFiInterface.NeighborDiscoveryTable` (`0x100287ac0`), `P2PTimer` (`0x100179390`), `DispatchTime.uptimeNanoseconds`, `objc_allocWithZone`, `bridgeErrorToNSError`. También llama `sub_10041B1F0` (companion) y `sub_10041B7F0`, `sub_1004292F0`, `sub_10040EB90`, `sub_100435C80`.
- `sub_10041B1F0` (creación de la interfaz companion de low latency): callees incluyen la metadata de **`CoreP2P.WiFiInterface`** (`0x100287ad0`) y `WiFiInterface.NeighborDiscoveryTable` (`0x100287ac0`), más `sub_1004155C0` (0x225), `sub_100286E10`, `sub_10027DE30`, `sub_100282E30`, `Logger` info/error — construye la interfaz companion sobre un `WiFiInterface`.

## 4) Configuración: llamadas Apple80211 e ioctls en orden observable

El orden siguiente es el que se puede sostener con las cadenas de callers; no implica que sea la única secuencia en runtime.

1. **Handle por rol** (`sub_100047660`, `0x100047660`): `RawGet` tipo **12** len 21 (`CARD_CAPABILITIES`, puerta) ×3 ramas; `RawSet` tipo **94** len 32 (`VIRTUAL_IF_CREATE`) con buffer: u32 @+0, MAC @+4..+9, **rol @+0x0C = 6 (AWDL)** o 0x0A; espera semáforo 30 s (doc: `AWDL_WIFIP2PD_REQUESTS.md`). El evento que lo libera queda pendiente (`?`; ver `AWDL_WIFIP2PD_P2P_EVENTS.md` §3).
2. **Descubrimiento/validación** (`sub_100043720` ← `sub_100047660` en `0x100047f09`/`0x100047f2c`): `SIOCGIFMEDIA` (`0xC02C6938`) y check `IFM_TYPE==IFM_IEEE80211` (`0x100043ccc-0x100043cde`).
3. **Registro de eventos** (`sub_100041B60` ← `sub_1000421E0`/`sub_100049F20`/`sub_10004A0C0`): vía `sub_100041EB0`+16 ids de `Apple80211StartMonitoringEvent`, o vía `Apple80211RawP2PEventMonitoringInit` (`sub_100041F20`). Ver `AWDL_WIFIP2PD_P2P_EVENTS.md`.
4. **Recuperación** (`sub_100046E30`, `0x100046e30`): log `"Attempting recovery for %s[%s] with %s on %s"` (`0x100047156`) con roles `"Infrastructure"/"NME"/"NAN Data"/"Low Latency"`, re-crea el handle (`sub_100047660` en `0x1000471be`) y re-arranca el monitoreo (`sub_1000421E0` en `0x10004724e`); en fallo log `"creation failed"`/`"WiFi-Aware Discovery"` (`0x10004728c-0x1000472b0`).
5. **Peticiones post-create por RawGet/RawSet**: `RawGet` tipo **97** (wrapper `sub_100046D30`, llamado por `sub_100068C20`) y `RawSet` tipo **293** len 24 (`sub_10004B300`, `0x10004b3e6`, seguido de `if_nametoindex`) — no atribuidos a AWDL (`?`).
6. **Configuración por ioctl directo sobre socket AF_INET6**: par 131 (GET 288 B) → 130 (SET 128 B con `apple80211req` tipo **7708**), en `sub_100058420`/`sub_1000586D0` (ver `AWDL_WIFIP2PD_IOCTLS.md` §3-6). Operación AWDL exacta: `?` (id 7708 sin nombre).
7. **Gestión de interfaz vía despachador** (`sub_100046A30`): `SIOCSIFFLAGS` up/down (`sub_100056990`), `SIOCSIFLLADDR` (MAC, `sub_100057120`), `SIOCSIFADDR`/`SIOCDIFADDR` (`sub_1000578F0`), `SIOCADDMULTI`/`SIOCDELMULTI` (`sub_100057DE0` + thunks `sub_10005F7D0`/`sub_10005F800`).

## 5) Destrucción: VIRTUAL_IF_DELETE **no aparece**

- El binario tiene exactamente **dos** sitios de `Apple80211RawSet` (documentados en `AWDL_WIFIP2PD_REQUESTS.md` y no refutados aquí): `0x100048d95` (en `sub_100047660`, tipo 94 = `VIRTUAL_IF_CREATE`) y `0x10004b3e6` (en `sub_10004B300`, tipo 293). **No hay `RawSet` tipo 95 (`VIRTUAL_IF_DELETE`)**.
- Ninguno de los 9 ioctls de la tabla (`0x100493008`) es un delete de interfaz virtual; lo más cercano observado es `SIOCSIFFLAGS` con flags sin `IFF_UP` (`sub_100056990` con bool=0 → flags `& 0xFFBE` sin `|0x41`) y `SIOCDIFADDR` (`sub_1000578F0` con opcional nil).
- `Apple80211EventMonitoringHalt` sí se usa (`sub_100042120`, doc `AWDL_WIFIP2PD_REQUESTS.md`), y hay log de teardown parcial durante recovery: `"Failed to clear all previous peer presence after interface recovery"` (`0x1004a7ae0`). Una ruta de borrado completo del `AWDLInterface` (deinit/destructor Swift) no fue localizada en esta pasada (`?`).
