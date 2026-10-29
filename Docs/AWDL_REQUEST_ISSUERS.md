# Emisores de los requests apple80211 sobre una interfaz AWDL (macOS 26, Darwin 25.6, x86_64)

Cierra la pregunta de [`AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md) y de
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §1: wifip2pd solo emite 6 requests
(tipos 12, 94, 97, 293), pero el kernel expone ~70 tipos AWDL. Este documento identifica quién
emite el resto y por dónde baja cada emisión al kernel. Base: barrido de bytes del dyld shared
cache `x86_64h` completo (7 ficheros, 2,1 GB, `/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/`
con su `.map`), barrido del `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`, mapeo
`VA = 0xffffff8000100000 + fileoff` del §2 de AWDL_APPLE80211_DISPATCH.md), symtab extraído del
pool `__LINKEDIT` del caché, análisis IDA headless sobre copias en
`Build/ida_awdl/task_awdl_issuers/` (airportd/sharingd/wifivelocityd `x86_64` de `/usr/libexec/`,
frameworks tallados del caché) y `otool`/`strings` sobre los binarios de `/usr/libexec/`.
Las direcciones de daemons son VA del slice x86_64 (base `0x100000000` implícita en las
subrutinas `sub_1000xxxxx`); las de frameworks son VA del caché (`0x7ff8…`/`0x7ffa…`/`0x7ffc…`);
las del kernel son VA del `.kc`. Lo no verificado va como `?`.

## 1. Respuesta corta

1. **La masa de los ~70 tipos AWDL no la emite ningún proceso de userspace: la emite el propio
   `IO80211AWDLPeerManager` del kernel** sobre su interfaz, por la ruta interna (magic
   `0x802869C8` → `IO80211Glue::receiveMessageInternal` → `apple80211RouteInternal` →
   `gInternalSetHandlerTable`). El `.kc` contiene 336 sites del magic SET `0x802869C8` y 299 del
   magic GET `0xC02869C9`; decenas de los SET caen dentro de funciones del PeerManager cuyo tipo
   coincide con la propia función (§6.2): cada setter/getter del manager se auto-llama por el
   mismo camino de despacho que usaría userspace. La codificación del comando interno (token
   `0x802869C8` con el tipo en el dword alto) está documentada en
   [`AWDL_PEERMANAGER_SOFTWARE.md`](AWDL_PEERMANAGER_SOFTWARE.md) §9.2.
2. En userspace **un único componente del sistema emite ioctls apple80211:
   `IO80211.framework`** (la versión userspace, en el caché). El barrido completo del caché
   produce exactamente 5 sites de GET (`0xC02869C9`) y 6 de SET (`0x802869C8`), todos dentro de
   esa imagen (§2.2). La constante `0xC02869C8` («SET usuario» según el doc de despacho) **no
   aparece como inmediato en ningún binario del sistema** (0 hits en el caché y en los 5
   daemons): los SET de userspace viajan todos como `0x802869C8`.
3. `Apple80211.framework` ya no implementa el API: su symtab solo exporta 13 símbolos externos
   (`AC*` de AppleConnect + `_Apple80211VersionString`/`_Apple80211VersionNumber`,
   symtab en `0x2dc07f00` del pool, 83 entradas). El API `Apple80211*` que importan los daemons
   lo define `IO80211.framework` (§2.1).
4. Entre los daemons, **el único emisor AWDL explícito es `airportd`**: tipos 381
   (`SET_RANGING_FOLLOW_MODE`) en start/stop AWDL, 164 (`AWDL_PEER_TRAFFIC_REGISTRATION`) vía la
   capa tipada de CoreWiFi, y los ranging 241/242/380 (§4). `sharingd`, `wifivelocityd`,
   `rapportd` y `bluetoothd` quedan descartados con evidencia (§7).
5. `CoreWiFi.framework` contiene una banda de constructores por tipo con **todos los tipos AWDL
   como inmediatos** (§5): es la capa de propiedades tipadas (`setAWDLXxx:`) por la que los
   daemons emiten sin tocar números de IOC. `WiFiPeerToPeer.framework` aporta las clases de
   configuración (`AWDLTrafficRegistrationConfiguration`, `AWDLServiceDiscoveryManager`) que
   airportd serializa dentro de esas requests.
6. **wifip2pd no usa `IOConnectCallMethod` ni el `IO80211APIUserClient`**: su tabla de imports no
   contiene `IOServiceOpen`/`IOConnect*` (§3.2). Solo toca el kernel vía
   `Apple80211RawGet/RawSet` (que bindean a `IO80211.framework`) e IOKit-registry para
   notificaciones.

## 2. El API Apple80211 en macOS 26: `IO80211.framework` (userspace)

### 2.1 Superficie exportada

El symtab de `IO80211.framework` (`/System/Library/PrivateFrameworks/IO80211.framework/Versions/A/IO80211`,
symtab en `0x23ff49d0` del fichero `.04` del caché, 1517 entradas, 1275 símbolos externos
definidos) define el API completo que los daemons importan (extracción en
`Build/ida_awdl/task_awdl_issuers/io80211_exports.txt`). Filas relevantes:

| Símbolo | VA |
|---|---|
| `_Apple80211Open` | `0x7ffc1887c4a8` |
| `_Apple80211BindToInterface` | `0x7ffc1887dce5` |
| `_Apple80211BindToInterfaceWithService` | `0x7ffc1887e70e` |
| `_Apple80211Close` | `0x7ffc1887d947` |
| `_Apple80211Get` | `0x7ffc1886792d` |
| `_Apple80211GetWithIOCTL` | `0x7ffc18867943` |
| `_Apple80211Set` | `0x7ffc1885bc95` |
| `_Apple80211SetWithIOCTL` | `0x7ffc18873429` |
| `_Apple80211RawGet` | `0x7ffc1885e3ce` |
| `_Apple80211RawSet` | `0x7ffc1885daef` |
| `_Apple80211CopyValue` | `0x7ffc1885b2f8` |
| `_Apple80211SetProperty` | `0x7ffc1887ad50` |
| `_Apple80211IOCTLGetWrapper` | `0x7ffc18872ab0` |
| `_Apple80211IOCTLSetWrapper` | `0x7ffc18866e98` |
| `_Apple80211StartMonitoringEvent` | `0x7ffc1885da42` |
| `_Apple80211EventMonitoringInit` / `…Init2` / `…InitIOUC` | `0x7ffc1887b102` / `0x7ffc1885d77c` / `0x7ffc1887b45c` |
| `_Apple80211RawEventMonitoringInit` / `_Apple80211RawP2PEventMonitoringInit` | `0x7ffc1887c0a0` / `0x7ffc1887c1e6` |
| `_Apple80211EventMonitoringHelper` | `0x7ffc1888ab7a` |
| `___Apple80211AWDLCompatibilityCall` | `0x7ffc188a2398` |

`Apple80211.framework` es en cambio una librería de transición: su symtab solo define `AC*`
(`_ACInterfaceCreate` `0x7ff81f8ab8f8`, `_ACInterfaceDeviceNameCopy` `0x7ff81f8ab9ae`,
`_ACInterfaceSetPower` `0x7ff81f8aba53`, …) y no contiene ningún `Apple80211*`; enlaza
`IO80211.framework` como `LC_LOAD_DYLIB` (no re-export). Los imports `Apple80211*` de los
daemons resuelven contra `IO80211.framework`, que `airportd`, `wifip2pd` y `sharingd` llevan en
su lista de dylibs (`otool -L`).

### 2.2 Los 11 sites ioctl apple80211 del sistema (barrido completo del caché)

Barrido de los bytes `C9 69 28 C0` (`0xC02869C9`) y `C8 69 28 80` (`0x802869C8`) sobre los 7
ficheros del caché (`Build/ida_awdl/task_awdl_issuers/sweep.py`, salida `sweep_out.txt`):
todas las ocurrencias con VA válido caen en `IO80211.framework` (`__TEXT` en
`0x7ffc18858000…0x7ffc188c09b0`). Cada site dentro de la función que el symtab atribuye:

| Función (symtab) | Magic | Sites (VA) |
|---|---|---|
| `_Apple80211CopyValue` (`0x7ffc1885b2f8`) | GET `0xC02869C9` | `0x7ffc1885b3cb` |
| `_Apple80211RawSet` (`0x7ffc1885daef`) | SET `0x802869C8` | `0x7ffc1885dbbf`, `0x7ffc1885dc80`, `0x7ffc1885e1cf` |
| `_Apple80211RawGet` (`0x7ffc1885e3ce`) | GET `0xC02869C9` | `0x7ffc1885e4a6` |
| `_Apple80211IOCTLSetWrapper` (`0x7ffc18866e98`) | SET `0x802869C8` | `0x7ffc18866f4f`, `0x7ffc18867013`, `0x7ffc1886764b` |
| `_Apple80211IOCTLGetWrapper` (`0x7ffc18872ab0`) | GET `0xC02869C9` | `0x7ffc18872b52` |
| `_Apple80211BindToInterfaceWithService` (`0x7ffc1887e70e`) | GET `0xC02869C9` | `0x7ffc1887e89a` |
| `_Apple80211EventMonitoringHelper` (`0x7ffc1888ab7a`) | GET `0xC02869C9` | `0x7ffc1888c468` |

Lectura de la decompilación (copia binaria del `__TEXT` cargada cruda, funciones
`sub_11A5D` = región RawGet/RawSet, `sub_12E90` = IOCTLSetWrapper, `sub_1EAA8` = IOCTLGetWrapper,
`sub_1F421` = SetWithIOCTL en `Build/ida_awdl/task_awdl_issuers/io80211_out/`): los wrappers
construyen el struct de 40 B (`apple80211req`: `ifname[16]` @0, tipo u32 @+0x10, val u32 @+0x14,
len u32 @+0x18, puntero @+0x20 — layout ya verificado en `AWDL_WIFIP2PD_POST_CREATE.md` §2.0) y
llaman `ioctl` sobre un socket BSD. El SET de userspace usa **siempre** `0x802869C8`
(`_IOW('i',200,40)`), no `0xC02869C8`; el kernel lo acepta en la misma rama
(`AWDL_APPLE80211_DISPATCH.md` §2). `Apple80211Get`/`Apple80211Set` (sin `WithIOCTL`) delegan en
las variantes `WithIOCTL`/wrappers (`?` el encadenado exacto: la decompilación cruda de
`sub_D1AE` que cubre la región de `Set`/`Get` marca «positive sp» y no permite confirmar cada
salto).

## 3. wifip2pd: cierre de la pregunta del user client

### 3.1 Encadenado real

`Apple80211RawGet`/`Apple80211RawSet` de wifip2pd son los exports de `IO80211.framework`
(§2.1): los 6 call sites conocidos (`0x100048d95` tipo 94, `0x10004b3e6` tipo 293,
`0x100046db9` tipo 97, tres de tipo 12) bajan por `_Apple80211RawSet`/`_Apple80211RawGet`
→ `ioctl(0x802869C8)`/`ioctl(0xC02869C9)` en `0x7ffc1885dbbf`/`0x7ffc1885dc80`/`0x7ffc1885e1cf`/
`0x7ffc1885e4a6` (§2.2) → `IO80211VirtualInterface::handleIoctlGated` /
`IO80211SkywalkInterface::performGatedCommandIOCTL` (`AWDL_APPLE80211_DISPATCH.md` §2).

### 3.2 Sin user client

`otool -arch x86_64 -Iv /usr/libexec/wifip2pd`: los únicos símbolos IOKit importados son
`_IORegistryEntryCreateCFProperty` (+1); no hay `IOServiceOpen`, `IOServiceMatching`,
`IOConnectCallMethod`, `IOConnectCallStructMethod` ni `IOConnectCallScalarMethod`. El canal de
eventos es el pipe Apple80211 (`Apple80211StartMonitoringEvent`, `Apple80211RawEventMonitoringInit`,
`Apple80211RawP2PEventMonitoringInit`), ya documentado en `AWDL_EVENTS.md` §4. Los selectores del
user client (`sMethods`, tabla en `0xffffff8002478ed0` del `.kc`; entradas de 24 B decodificadas
con la regla de fixups del `.kc`: `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)`) cubren 0–8:
`0xffffff80023523f0` (0), `0xffffff800235240a` (1), `extSubscribeEvent` `0xffffff8002352486`
(2), `extEventMonitorInit` `0xffffff800235256e` (3), `extEventMonitorExit` `0xffffff80023525aa`
(4), `0xffffff80023525e6` (5), `0xffffff8002352662` (6), `0xffffff800235267c` (7),
`0xffffff8002352696` (8, con 2 escalares de entrada). Ninguno es el selector del «comando»
genérico que llega a `performGatedCommandIOUC` (`0xffffff80022f6e30`,
`AWDL_APPLE80211_DISPATCH.md` §2): ese selector concreto queda `?` — irrelevante para wifip2pd,
que no abre ningún user client.

## 4. airportd: el emisor userspace AWDL

Inventario de su superficie Apple80211 (probe1 sobre copia `airportd_copy.i64`,
`Build/ida_awdl/task_awdl_issuers/airportd/summary.txt`): 45 call sites de `Apple80211Set`,
13 de `Apple80211Get`, 12 de `Apple80211CopyValue`, 2 de `Apple80211RawSet`
(`-[CWHostAPModeManager __startHostAPModeWithNetworkData:]` `0x1000ec1be` y
`__stopHostAPMode` — modo AP, no AWDL), 1 de `Apple80211RawGet`
(`-[CWHostAPModeManager __supportedChannel:]`), 5 de `ioctl` (ninguno con constantes
apple80211; el único identificado es `0xC0206911` en `sub_100083C1C`), y 1 `IOServiceOpen` +
1 `IOConnectCallAsyncMethod` en `-[WiFiWatchdogMonitor initWithIOService:]`
(`0x1000fbc97`: `IOServiceOpen(..., type=1)` sobre el servicio del driver y
`IOConnectCallAsyncMethod` en `0x1000fbebb`, ambos para el watchdog — sin requests apple80211).
La extracción de tipos (`analyze_airportd.py`, salida `airportd_calls.txt`) no produce ningún
tipo AWDL directo salvo los que siguen; el resto son tipos generales (1, 2, 3, 4, 5, 9, 15, 16,
19, 24, 43, 48, 51, 52, 66, 69, 70, 81, 85, 87, 90, 96, 107, 191, 192, 193, 216, 223, 239, 245,
255, 256, 257, 260, 265, 358, 363, 380, 381, 382, 422, 448, 469, 483, 484, 487, 488, 490, 507,
512, 514, 516, 531, 551, más dos sites con `0xFFFFFFFF` como segundo argumento y tipos 262/263
en `sub_100062404`/`sub_10006259C`, sobrecarga no identificada, `?`).

Sites con semántica AWDL/ranging (strings-xrefs del IDB, `airportd/summary2.txt` y
`airportd/strings_pseudo/`):

| Request | Tipo / len | Función (VA airportd) | Cuándo |
|---|---|---|---|
| `SET_RANGING_FOLLOW_MODE` | 381 / 12 | `-[CWXPCInterfaceContext startAWDL:error:]` `0x1000a0aea` — `Apple80211Set(handle, 381, 0, dict, 8)` con el dict `APPLE80211KEY_RANGING_FOLLOW_MODE` (+`…_BAND`) | Arranque de AWDL en modo «assisted discovery follow» (AirDrop/AutoUnlock); log `"FAILED to set APPLE80211_IOC_SET_RANGING_FOLLOW_MODE"` en `0x10013d930` |
| `SET_RANGING_FOLLOW_MODE` | 381 / 12 | `-[CWXPCInterfaceContext stopAWDL:]` `0x1000a10ea` | Parada de AWDL; mismo string de error |
| `AWDL_PEER_TRAFFIC_REGISTRATION` | 164 / 184 | `startAWDL:error:` `0x1000a0aea` y `stopAWDL:` `0x1000a10ea` vía `-[CWXPCInterfaceContext __setPeerTrafficRegistrationWithConfiguration:error:]` — la request no se emite con un inmediato en el texto de airportd: sale por la capa tipada de CoreWiFi (§5); logs `"FAILED to set APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION…"` en `0x10013d8ca`/`0x10013da02` | En cada start/stop AWDL con configuración de registro de tráfico (assisted discovery, CarPlay, ranging, migración) |
| `RANGING_ENABLE` | 241 | `-[CWXPCInterfaceContext setRangeable:peers:error:]` `0x10009efe8`; también `-[CWXPCInterfaceContext setNDD:]` `0x1000c4bac` (log `0x10013d268`) | Sesiones de ranging (AutoUnlock/AirDrop) |
| `RANGING_START` | 242 | función en `0x10009ed66` (log `"FAILED APPLE80211_IOC_RANGING_START"` en `0x10013d165`) | Inicio de ranging |
| `SET_RANGING_IDENTIFIER` | 380 | `-[CWXPCInterfaceContext setRangingIdentifier:error:]` `0x10009f1e1` (`Apple80211Set(handle, 380, …)`) | Antes de cada sesión de ranging |
| `TRAP_INFO` (GET) | 257 | `-[CWXPCInterfaceContext __addExtendedWatchdogAnalyticsInfoToMetricIfAvailable:withType:]` `0x1000afe9e` | Analítica (no AWDL) |
| `LEAKY_AP_SSID_STATS` | 245 | `-[CWXPCInterfaceContext __handleLeakyApInfo]` `0x1000b1408` | No AWDL |

El disparador de nivel superior es el XPC de airportd:
`-[CWXPCSubsystem startAWDLWithInterfaceName:request:connection:error:]`
(`0x1000245a0`, string del selector en `0x1001233c0`) y su pareja `stopAWDLWithInterfaceName:…`
— los clientes (sharingd vía `WiFiPeerToPeer.framework`, §5) piden AWDL por XPC y airportd
emite estas requests sobre la interfaz. El propio `startAWDL:` valida el rol de la interfaz
(`virtualInterfaceRole == 6`, llamada a `virtualInterfaceRole` en el pseudocódigo de
`0x1000a0aea`) antes de emitir nada.

## 5. CoreWiFi y WiFiPeerToPeer: la capa tipada

### 5.1 CoreWiFi.framework

`/System/Library/PrivateFrameworks/CoreWiFi.framework/Versions/A/CoreWiFi` (`__TEXT`
`0x7ff81f8ac000`, 4.801.669 B). Dos evidencias:

1. **Selectores ObjC tipados AWDL** en su `__TEXT` (offsets sobre la base `0x7ff81f8ac000`):
   `AWDLElectionID:` `+0x468704`, `AWDLAFTxMode:` `+0x468760`,
   `AWDLAvailabilityWindowAPAlignment:` `+0x46878e`, `AWDLElectionMetric:` `+0x46880e`,
   `AWDLElectionParameters:` `+0x468825`, `AWDLEncryptionType:` `+0x468887`,
   `AWDLPeerTrafficRegistration:` `+0x4689f8`, `AWDLPresenceMode` `+0x468a4c`,
   `AWDLBTLEStateParameters:` `+0x4687c1`, `AWDLContinuousElectionAlgorithmEnabled:`
   `+0x4687e2`, y los nombres de propiedad en la zona `+0x46ad00`–`+0x46b0ff` (offsets individuales `?`).
2. **Inmediatos de tipo AWDL agrupados** en dos bandas de código (escaneo de patrones
   `mov r32, imm32`/`C7` con valores del conjunto AWDL): banda 1 en `+0xb2d11…+0xbf50b`
   — 116 `+0xb51e7`, 117 `+0xb5297`, 118 `+0xb5557`, 119 `+0xb5607`, 120 `+0xb53f7`,
   122 `+0xb54a7`, 125 `+0xb56b7`, 126 `+0xb58c7`, 127 `+0xb5977`, 129 `+0xb5ad7`,
   131 `+0xb5e47`, 132 `+0xb5ef7`, 133 `+0xb5d97`, 134 `+0xb5ce7`, 135 `+0xb5c37`,
   136 `+0xb5fa7`, 138 `+0xb6107`, 155 `+0xaa877`, 163 `+0xabb07`, 164 `+0xabbb7`,
   167 `+0xb6687`, 202 `+0xb9a8c`, 203 `+0xb82f7`, 204 `+0xb83a7`, 205 `+0xb8457`,
   208 `+0xb8667`, 213 `+0xb9ee7`, 217 `+0xbac27`, 219 `+0xb2d11`, 220 `+0xbb1ab`,
   229 `+0xbd101`, 233 `+0xbd417`, 253 `+0xbf50b`; banda 2 en `+0x3493da…+0x35aff3`
   con el mismo conjunto. VAs de ejemplo: tipo 164 en `0x7ff81f957bb7` (banda 1) y
   `0x7ff81fbfdc73` (banda 2); tipo 136 en `0x7ff81f961fa7`.

Conclusión: CoreWiFi implementa los setters/getters tipados (`setAWDLXxx:`) que traducen
propiedad → IOC numérico. airportd enlaza CoreWiFi (`otool -L`) y su
`__setPeerTrafficRegistrationWithConfiguration:error:` no contiene el inmediato 164: la
emisión de 164 (y del resto de tipos tipados) ocurre dentro de CoreWiFi. Qué daemon/proceso
invoca cada setter concreto queda como `?` (requiere seguir los call sites ObjC de CoreWiFi;
sus clases aparecen también compiladas dentro de los daemons: los `CWXPC*` de airportd y los
`W5*` de wifivelocityd).

### 5.2 WiFiPeerToPeer.framework

`__TEXT` `0x7ffa1cfce000`, 338.939 B. Clases y claves (strings): `AWDLTrafficRegistrationConfiguration`
(`+0x4655e`), `AWDLServiceDiscoveryManager`/`AWDLServiceDiscoveryManagerXPC(+Delegate)`
(`+0x467b4`/`+0x46795`), `AWDLStateMonitor` (`+0x4303c`), `AWDLState.channelSequenceKey`
(`+0x43997`), `AWDLState.peerMasterChannelKey` (`+0x439f9`), `<AWDLTrafficRegistrationConfiguration %@ to %@>`
(`+0x41a8d`). sharingd la enlaza (`otool -L /usr/libexec/sharingd`) y habla con airportd por XPC:
es la parte cliente de la configuración que airportd convierte en la request 164 (§4). No se
le atribuye emisión directa.

## 6. Kernel: `IO80211AWDLPeerManager` como emisor interno

### 6.1 La ruta

El `.kc` contiene 336 sites del magic SET `0x802869C8` y 299 del magic GET `0xC02869C9`
(`kc_sweep.py`). Sites de ambos magics caen en `IO80211Glue::receiveMessageInternal`
(`0xffffff800219426c` el SET) — el receptor de la ruta interna; los sites dentro de los puentes
estáticos `__ZL*get/set*` (p. ej. `__ZL7getSSID…`, `__ZL9setRSN_IE…`) corresponden a la
validación del magic en el receptor, no a emisiones. El resto relevante son emisores.

### 6.2 Emisiones del PeerManager (tipo escrito junto al magic)

Método: para cada site se extrajeron los inmediatos de tipo (`mov r32, imm32` con valor
1–600 y stores `C7` con valor 1–600) en una ventana de ±0x60 B
(`kc_types.py`, salida `kc_types_out.txt`; agregado `kc_issuers_full.txt`). El patrón es
sistemático: cada setter/getter del manager construye el struct de 40 B con **su propio tipo
de IOC** y lo reinyecta por el canal interno. Los tipos pequeños junto al tipo principal
(1, 3, 8, 14, 32) pueden ser constantes del código vecino y no el campo `req_type`; cuando
solo hay constantes pequeñas sin tipo identificable la fila lo indica. (El funnel interno es
`IO80211PeerManager::ioctl_internal`, con el tipo también empaquetado en el dword alto del
comando; ver [`AWDL_PEERMANAGER_SOFTWARE.md`](AWDL_PEERMANAGER_SOFTWARE.md) §9.2.)

SET (el tipo destacado es el que coincide con la función):

| Función (`IO80211AWDLPeerManager::`) | Site (VA) | Tipos observados |
|---|---|---|
| `setAwdlSyncParams(apple80211_awdl_sync_params*)` | `0xffffff800221b7e9+0xad` | **116**, 14 |
| `setAwdlSyncEnabled(…)` | `0xffffff800221baa4+0x262` | **117**, 1, 14 |
| `setAwdlAdvertisers(awdl_peer_advert_add*)` | `0xffffff800221c1c8+0x2a` | **229**, 14 |
| `setAwdlAfTxMode(apple80211_awdl_af_tx_mode*)` | `0xffffff800221c38d+0x5f` | **208**, 14, 32 |
| `setAwdlElectionId(apple80211_awdl_election_id*)` | `0xffffff800221c8c5+0x2d` | **122**, 32 |
| `setAwdlConfig(apple80211_awdl_config_request*)` | `0xffffff800221c9ea+0x2a` | **432**, 1, 14 |
| `setAwdlBssid(apple80211_awdl_bssid*)` | `0xffffff800221caf0+0xae` | **125**, 6, 14 |
| `setAwdlElectionParams(apple80211_awdl_election_params*)` | `0xffffff800221cba2+0x3c` | **155** |
| `setAwdlChannelSequence(apple80211_awdl_sync_channel_sequence*)` | `0xffffff800221d122+0x246` | **129**, 1, 3, 32 |
| `setAwdlOUI(apple80211_awdl_oui*)` | `0xffffff800221d289+0x2d` | **131**, 14 |
| `setAwdlSecondaryMasterChannel(apple80211_awdl_secondary_master_channel*)` | `0xffffff800221d3fe+0xea` | **205**, 1, 8 |
| `setAwdlMasterChannel(apple80211_awdl_master_channel*)` | `0xffffff800221d584+0xa6` | **132**, 1, 8 |
| `setAwdlKey(apple80211_key*)` | `0xffffff800221d66c+0x2e` | **219**, 14 |
| `setAwdlEncryptType(apple80211_encrypt*)` | `0xffffff800221d71c+0x2a` | **220**, 14 |
| `setAwdlSyncState(apple80211_awdl_sync_state*)` | `0xffffff800221e511+0x13d`, `0xffffff800221e556+0x182`, `0xffffff800221e6c3+0x2ef` | **134**, **117**, 1 |
| `setAwdlSyncFramePacketLifetime(apple80211_awdl_sync_frame_packet_lifetime*)` | `0xffffff800221e8e3+0x2d` | **138**, 14 |
| `setLLWParams(apple80211_llw_params*)` | `0xffffff800221e0b0+0x2a` | **344**, 14 |
| `setAwdlTxRate(apple80211_tx_rate*)` | `0xffffff800221e17f+0x6d` | **411** |
| `setAwdlBlockSetCommands(apple80211_awdl_block_set_commands*)` | `0xffffff800221e290+0x2a` | **233**, 14 |
| `setAwdlElectionRssiThresholds(apple80211_awdl_election_rssi_thresholds*)` | `0xffffff800221e355+0x61` | **135**, 14 |
| `setAwdlMinRate(apple80211_awdl_min_rate*)` | `0xffffff80022206a4+0x46` | **213**, 32 |
| `setAwdlElectionMetric(apple80211_awdl_election_metric*)` | `0xffffff80021cb371+0x2f` | 43, 65 — sin 126 en la ventana (`?`) |
| `setAwdlExtensionParameters()` | `0xffffff80021cc6a6+0x36` | sin inmediato en la ventana (118 por otra vía, `?`) |
| `setPresenceMode(uint)` | `0xffffff80021cc5f6+0x50` | **136** |
| `setAwdlTxCalPeriod(apple80211_awdl_txcal_period*)` | `0xffffff800221c139+0x2d` | **226**, 14 (226 sin nombre en la tabla del kernel, `?`) |
| `setInfra6GMode(bool)` | `0xffffff8002238f51+0xa7` | 8, 216, **422** (`6G_MODE`) |
| `handleCarPlayTrafficReg(apple80211_awdl_peer_traffic_registration*)` | `0xffffff80022059f9+0x7b` | **164**, 14 |
| `handleAssistedDiscoveryTrafficReg(…,bool)` | `0xffffff8002205e16+0x282` | **164**, 1, 14, 43 |
| `handleRangingTrafficReg(…)` | `0xffffff800220608a+0x7c` | **164**, 14 |
| `handleMigrationTrafficReg(IO80211AWDLPeer*,…)` | `0xffffff80022067ae+0x1a6` | **164**, 1, 14 |
| `checkNSteerForConnectivityCheck(IO80211AWDLPeer*,…)` | `0xffffff80021f9c0d+0x13b`, `0xffffff80021f9cbe+0x1ec`, `0xffffff80021f9d9f+0x2cd` | **164**, 1, 4, 12 |
| `notifyRealTimeModeOnOff(…)` | `0xffffff80021f1fcc+0x274` | 164, 14, 128? |
| `configRTGPeerStats(IO80211AWDLPeer*,bool)` | `0xffffff800222f105+0xef` | 128, **467** (`?` nombre), 14 |
| `reportRTGPeerStats(IO80211AWDLPeer*)` | `0xffffff800222f3dc+0xf2` | 128, **467**, 14 |
| `programChipWithNewMaster()` | `0xffffff80021ec53d+0x87`, `0xffffff80021ec65d+0x1a7`, `0xffffff80021eca02+0x54c` | 1, 6 — sin tipo AWDL claro (`?`) |
| `setChannelSequenceUnconditional(bool)` | `0xffffff80021cc0ba+0xa20` | 1, 400 (`?`) |
| `setAFTXMode(apple80211_awdl_af_tx_type_t)` | `0xffffff80021f7d5b+0x55`, `0xffffff80021f7db0+0xaa` | 3 (`?`) |
| `forceRoamToPeersAP(ether_addr*,uchar,uchar)` | `0xffffff80021f96e1+0x235` | 76, 140 — 167 esperado, no observado (`?`) |
| `initFWElectionParams()` | `0xffffff80021ed247+0x47` | 1 (`?`) |
| `enableAWDL()` | `0xffffff80021dd45a+0x502` | 1 (`?`) |
| `disableAWDL(uint)` | `0xffffff80021c724e+0x766` | 1 (`?`) |
| `configure()` | `0xffffff80021c574b+0x261`, `0xffffff80021c5e7f+0x995`, `0xffffff80021c6012+0xb28`, `0xffffff80021c6096+0xbac`, `0xffffff80021c60ee+0xc04`, `0xffffff80021c61d0+0xce6` | solo 1 en los dos últimos (`?`) |
| `interfaceStateChange(StateChangedSource)` | `0xffffff80021da0e1+0x1419` | **13** (`STATE`) |
| `exitQuietMode()` | `0xffffff80021fae8c+0x170` | 1, 17 (`?`) |
| `reevaluateRTGConfiguration(bool)` | `0xffffff80021d4cb3+0x19d` | 3, 5 (`?`) |
| `runPiggybackScanStateMachine()` | `0xffffff80021e5766+0x6b8` | 3 (`?`) |
| `reportDataPathEvents(uint,void*,ulong)` | `0xffffff80021e08f6+0x3002` | 1 (`?`) |
| `setSyncFramePeriod(uint,bool)` | `0xffffff80021ed0e7+0xc3` | sin inmediato (`?`) |
| `setScheduleState(…)` | `0xffffff80021d4395+0x1bf5` | sin inmediato (`?`) |
| `setPSFDwell(psfDwell)` | `0xffffff80021f4f7b+0x551` | sin inmediato (`?`) |
| `programMasterChannels()` | `0xffffff80021d4a13+0xc9`, `0xffffff80021d4a8e+0x144` | sin inmediato (`?`) |
| `hostapForceChannelSwitch(uint,uint)` | `0xffffff80021d8a22+0x5c` | 2 (`?`) |
| `updatePrimaryPayloadMI(…)` / `updateSecondaryPayloadMI(…)` / `sendUnicastMI(…)` / `configureMITxOnDwellChannel(uint)` / `enterQuietMode()` / `enableRoaming()` / `configureDFSProxy(…)` / `configureDFSProxyAddRemoveUCSAPeer(…)` | `0xffffff80021fb610+0x2a6`, `0xffffff8002214306+0x158`, `0xffffff8002213f24+0x31a`, `0xffffff80022140f5+0x51`, `0xffffff80021f4256+0x1c8`, `0xffffff800220da3b+0x45`, `0xffffff80021f8590+0x38`, `0xffffff80021f8726+0x34` | sin inmediato en la ventana (`?`) |

GET (mismo patrón, magic `0xC02869C9`):

| Función | Site (VA) | Tipos observados |
|---|---|---|
| `getAwdlSyncParams(…)` | `0xffffff800221ef09+0x77` | **116**, 32 |
| `getAwdlSyncEnabled(…)` | `0xffffff800221f153+0x5f` | **117**, 32 |
| `getAwdlElectionId(…)` | `0xffffff800221f477+0x31` | **122** |
| `getAwdlElectionParams(…)` | `0xffffff800221f6b9+0x2d` | **155** |
| `getAwdlElectionMetric(…)` | `0xffffff800221f559+0x31` | **126**, 14, 32 |
| `getAwdlChannelSequence(…)` | `0xffffff800221f834+0x78` | **129**, 32 |
| `getAwdlMasterChannel(…)` | `0xffffff800221fb23+0x2d` | **132** |
| `getAwdlSecondaryMasterChannel(…)` | `0xffffff800221fc1f+0x4d` | **205** |
| `getAwdlSyncState(…)` | `0xffffff800221fd72+0x2a` | **134**, **135**, 14 |
| `getAwdlElectionRssiThresholds(…)` | `0xffffff800221fd32+0x24` | **134**, **135** |
| `getAwdlPresenceMode(…)` | `0xffffff800221fdea+0x2a` | **136**, 14 |
| `getAwdlEncryptType(…)` | `0xffffff800221d79e+0x2a` | **220**, 14 |
| `getAwdlBlockSetCommands(…)` | `0xffffff800221dd48+0x2a` | **233**, **338**, 14 |
| `getAwdlCCA(apple80211_awdl_cca*)` | `0xffffff800221dcf2+0xbc` | **338**, 14, 233 |
| `getLLWParams(apple80211_llw_params*)` | `0xffffff800221e02e+0x2a` | **344**, 14 |
| `getAwdlDevStats(apple80211_awdl_dev_stats*)` | `0xffffff800221dc20+0x24` | **247**, 14 |
| `getAwdlAfTxMode(…)` | `0xffffff800221c27b+0x5b` | **208** |
| `getAwdlTxCalPeriod(…)` | `0xffffff800221c0b4+0x2a` | **226**, 14 |
| `getP2PDriverMetrics(apple80211_p2p_steering_metrics*)` | `0xffffff800222a34a+0xe` | 1, **456** (`?`) |
| `configureValidAWDLChannels()` | `0xffffff80021cab7f+0x13f` | **207** (`CHANNELS_INFO`), 1, 379 (`?`) |
| `getSibCoexStatusFromDriver()` | `0xffffff80021cb52d+0x39` | sin inmediato (`?`) |
| `enableAWDL()` (GET) | `0xffffff80021dd180+0x228` | sin inmediato (`?`) |
| `handleDebugCmd_chanlist(…)` | `0xffffff8002220b0c+0x48` | sin inmediato (`?`) |

El funnel genérico es `IO80211AWDLPeerManager::setIOCTLE(uint,uint,void*)`
(site del magic en `0xffffff80021fb8ba+0xc8`): tipo/len/payload como argumentos. Las funciones
receptoras de los tipos coinciden con la tabla de `AWDL_APPLE80211_DISPATCH.md` §4
(`gSetHandlerTable[115] = setAWDL_SYNC_PARAMS` para 116, etc.): la auto-emisión recorre el mismo
camino de despacho que una request de userspace y termina en el mismo setter, con la
notificación al glue (`CommonGlue::sendMessage`) incluida.

### 6.3 Otros emisores internos

- `AppleBCMWLANCore::handleCardSpecific(IO80211SkywalkInterface*, uint, void*, bool)`:
  SET `0xffffff800165f7bd+0x147` (tipo 4), `0xffffff800165f863+0x1ed` (4),
  `0xffffff800165f9dc+0x366` (1, 16, 128), `0xffffff800165ffd7+0x961` (4), sites sin inmediato
  en `0xffffff800165ff10+0x89a` y `0xffffff8001660036+0x9c0`; GET `0xffffff800165f7d1+0x15b`
  y `0xffffff800165f871+0x1fb` (4). `?` la semántica de cada site (CHANNEL 4 y claves 1/16/128).
- `AppleBCMWLANLowLatencyInterface::processBSDCommand(__ifnet*, uint, void*)`:
  SET `0xffffff80015e03f8+0x16` (1, 22, 182), GET `0xffffff80015e040a+0x28` (22, 182, 328) —
  la interfaz de baja latencia reenvía comandos BSD como requests apple80211.
- `IO80211SkywalkInterface::processBSDCommand(__ifnet*, uint, void*)`:
  SET `0xffffff80022f6f19+0x81` (16, 128), `0xffffff80022f6fb5+0x11d` (1, 128),
  `0xffffff80022f6fd8+0x140` (1), `0xffffff80022f702d+0x195` (sin inmediato); GET
  `0xffffff80022f6f34+0x9c` (16, 128), `0xffffff80022f6fac+0x114` (1, 128),
  `0xffffff80022f6fe3+0x14b` (1), `0xffffff80022f703b+0x1a3`.
- `IO80211AWDLPeerManager::setInfra6GMode(bool)` SET `0xffffff8002238f51+0xa7` (8, 216, 422) y
  `IO80211NANInfraManager::setInfra6GMode(bool)` SET `0xffffff800232a4f1+0x89` (idéntico):
  ambos managers emiten `6G_MODE` (422) hacia la interfaz.
- `IO80211NANPeerManager::setIOCTLE` / `IO80211NANDataInterfacePeerManager::setIOCTLE`:
  `0xffffff80022acc9f+0x9d`, `0xffffff80022a977f+0x9d` — el mismo funnel para los managers NAN.

## 7. Descartes

| Proceso | Evidencia de descarte |
|---|---|
| `sharingd` | Importa `Apple80211Open/BindToInterface/GetIfListCopy/EventMonitoringInit/StartMonitoringEvent/Close` y `IOServiceOpen`+`IOConnectCallStructMethod/ScalarMethod`; las funciones que llaman a IOConnect son de **ACMLib** (`AppleCredentialManager`): `sub_10060015D` (`0x10060015d`, log `"ACMLib" "performCommand"`, path `…/AppleCredentialManager_ClientLibs/ACMLib/ACMLib.c`) y `sub_1005FFB03` (`0x1005ffb03`, `updateLogLevelFromKext`). Sin `Apple80211Get/Set`: no emite requests. Su rol AWDL es XPC (start/stop a airportd) y monitoreo de eventos; enlaza `WiFiPeerToPeer.framework` para la parte de configuración (§5.2) |
| `wifivelocityd` | Su único `Apple80211RawSet` es `-[W5WiFiInterface setSTBCEnabled:]` (`0x1000630ef`, tipo STBC); los `IOServiceMatching` son `"IOPlatformExpertDevice"` (`0x10002f253`, `0x10007204e`) y `"apcie"` (`0x1000850c9+`). Sus strings `AWDL*` (`AWDLElectionMetric`, `AWDLPresenceMode`, …) son nombres de señales IOReport/métricas, no requests |
| `rapportd` | Sin ningún símbolo Apple80211/IOConnect importado; solo `IORegistryEntryGetRegistryEntryID/GetChildEntry` |
| `bluetoothd` | Binario no presente en `/usr/libexec` (en caché bajo otra ruta `?`); ningún hit del magic apple80211 fuera de `IO80211.framework` en todo el caché, por lo que ningún componente del caché emite ioctls directos |
| Finder/AirDrop UI y demás apps | No pueden emitir: el barrido demuestra que el único emisor de ioctl es `IO80211.framework`; cualquier proceso que no pase por él queda fuera por construcción (`?` para procesos on-disk no analizados fuera de `/usr/libexec`) |

## 8. Tabla maestra por tipo (AWDL)

Para cada tipo AWDL del kernel (`AWDL_APPLE80211_DISPATCH.md` §4 da nombre, handler y `len`
exigido): quién lo emite. «PM» = `IO80211AWDLPeerManager` interno (§6.2, VA del site del
magic); «ad» = airportd; «CW» = banda tipada de CoreWiFi (§5.1, VA = base `0x7ff81f8ac000` +
offset). Tipos sin emisor identificado quedan `?`.

| Tipo | Nombre | Emisor | Evidencia |
|---:|---|---|---|
| 116 | `AWDL_SYNC_PARAMS` | PM | `0xffffff800221b7e9+0xad` (SET), `0xffffff800221ef09+0x77` (GET); CW `+0xb51e7` |
| 117 | `AWDL_SYNC_ENABLED` | PM | `0xffffff800221baa4+0x262` (SET), `0xffffff800221f153+0x5f` (GET), reemitido por `setAwdlSyncState` en `0xffffff800221e511+0x13d`; CW `+0xb5297` |
| 118 | `AWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `?` | CW `+0xb5557`; site PM sin inmediato (`0xffffff80021cc6a6+0x36`) |
| 119 | `AWDL_SERVICE_PARAMS` | `?` (solo CW) | CW `+0xb5607` |
| 120 | `AWDL_PEER_SERVICE_REQUEST` | `?` (solo CW) | CW `+0xb53f7` |
| 122 | `AWDL_ELECTION_ID` | PM | `0xffffff800221c8c5+0x2d` (SET), `0xffffff800221f477+0x31` (GET); CW `+0xb54a7` |
| 124 | `AWDL_GUARD_TIME` | `?` | no observado |
| 125 | `AWDL_BSSID` | PM | `0xffffff800221caf0+0xae` (SET); CW `+0xb56b7` |
| 126 | `AWDL_ELECTION_METRIC` | PM | `0xffffff800221f559+0x31` (GET); CW `+0xb58c7` (SET PM `0xffffff80021cb371+0x2f` sin 126, `?`) |
| 127 | `AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` | `?` (solo CW) | CW `+0xb5977` |
| 128 | `AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT` | PM | `0xffffff800222f105+0xef` y `0xffffff800222f3dc+0xf2` (RTG stats); CW `+0xb5ad7` |
| 129 | `AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | PM | `0xffffff800221d122+0x246` (SET), `0xffffff800221f834+0x78` (GET); CW `+0xb5e47` |
| 131 | `AWDL_OUI` | PM | `0xffffff800221d289+0x2d` (SET); CW `+0xb5ef7` |
| 132 | `AWDL_MASTER_CHANNEL` | PM | `0xffffff800221d584+0xa6` (SET), `0xffffff800221fb23+0x2d` (GET); CW `+0xb5d97` |
| 133 | `AWDL_TOP_MASTER` | `?` | no observado |
| 134 | `AWDL_SYNC_STATE` | PM | `0xffffff800221e511+0x13d` (SET), `0xffffff800221fd72+0x2a` (GET); CW `+0xb5ce7` |
| 135 | `AWDL_ELECTION_RSSI_THRESHOLDS` | PM | `0xffffff800221e355+0x61` (SET), `0xffffff800221fd32+0x24` (GET), `0xffffff80021e1027+0xc9` (`determineAndSetEdgeRssiThreshold`); CW `+0xb5c37` |
| 136 | `AWDL_PRESENCE_MODE` | PM | `0xffffff80021cc5f6+0x50` (SET), `0xffffff800221fdea+0x2a` (GET); CW `+0xb5fa7` |
| 138 | `AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME` | PM | `0xffffff800221e8e3+0x2d` (SET); CW `+0xb6107` |
| 139 | `AWDL_MASTER_MODE_SYNC_FRAME_PERIOD` | `?` | no observado (candidato `setSyncFramePeriod`, `0xffffff80021ed0e7+0xc3` sin inmediato) |
| 148 | `AWDL_STRATEGY` | `?` (solo CW) | CW `+0xaa3a7`/`+0x338293` |
| 155 | `AWDL_ELECTION_PARAMS` | PM | `0xffffff800221cba2+0x3c` (SET), `0xffffff800221f6b9+0x2d` (GET); CW `+0xaa877` |
| 163 | `AWDL_SOCIAL_TIME_SLOTS` | `?` (solo CW) | CW `+0xabb07` |
| 164 | `AWDL_PEER_TRAFFIC_REGISTRATION` | **airportd** + PM | ad `0x1000a0aea`/`0x1000a10ea` (logs `0x10013d8ca`/`0x10013da02`); CW `+0xabbb7`; PM `0xffffff80022059f9+0x7b` (CarPlay), `0xffffff8002205e16+0x282` (assisted discovery), `0xffffff800220608a+0x7c` (ranging), `0xffffff80022067ae+0x1a6` (migración), `0xffffff80021f9c0d+0x13b` (NSteer) |
| 167 | `AWDL_FORCED_ROAM_CONFIG` | PM `?` | CW `+0xb6687`; `forceRoamToPeersAP` `0xffffff80021f96e1+0x235` sin 167 en la ventana |
| 201–205 | BTLE/secondary master | CW; PM 205 | CW `+0xb962b`/`+0xb9a8c`/`+0xb82f7`/`+0xb83a7`/`+0xb8457`; PM 205 `0xffffff800221d3fe+0xea` (SET), `0xffffff800221fc1f+0x4d` (GET) |
| 208 | `AWDL_AF_TX_MODE` | PM | `0xffffff800221c38d+0x5f` (SET), `0xffffff800221c27b+0x5b` (GET); CW `+0xb8667` |
| 213 | `AWDL_MIN_RATE` | PM | `0xffffff80022206a4+0x46` (SET); CW `+0xb9ee7` |
| 217 | `AWDL_OPER_MODE` | `?` (solo CW) | CW `+0xbac27` |
| 219 | `AWDL_ENCRYPTION_KEYS` | PM | `0xffffff800221d66c+0x2e` (SET); CW `+0xb2d11` |
| 220 | `AWDL_ENCRYPTION_TYPE` | PM | `0xffffff800221d71c+0x2a` (SET), `0xffffff800221d79e+0x2a` (GET); CW `+0xbb1ab` |
| 226 | (sin nombre en la tabla del kernel; txcal) | PM | `0xffffff800221c139+0x2d` (SET), `0xffffff800221c0b4+0x2a` (GET) |
| 229 | `AWDL_AWDL_ADVERTISERS` | PM | `0xffffff800221c1c8+0x2a` (SET); CW `+0xbd101` |
| 233 | `AWDL_BLOCK_SET_COMMANDS` | PM | `0xffffff800221e290+0x2a` (SET), `0xffffff800221dd48+0x2a` (GET); CW `+0xbd417` |
| 247 | `AWDL_DEV_STATS` | PM | `0xffffff800221dc20+0x24` (GET) |
| 253 | `AWDL_NEARBY_LOG_TRIGGER` | `?` (solo CW) | CW `+0xbf50b` |
| 338 | `AWDL_CCA` | PM | `0xffffff800221dcf2+0xbc` (GET) |
| 344 | `LLW_PARAMS` | PM | `0xffffff800221e0b0+0x2a` (SET), `0xffffff800221e02e+0x2a` (GET); CW `+0x57925` |
| 351 | `AWDL_SIDECAR_DIAGNOSTICS` | `?` (solo CW) | CW `+0x57a56` |
| 411 | `TX_RATE` (reemite a PeerManager) | PM | `0xffffff800221e17f+0x6d` (SET); CW `+0x58a9b` |
| 422 | `6G_MODE` (genérico, desde el manager AWDL) | PM `setInfra6GMode` | `0xffffff8002238f51+0xa7` |
| 432 | (sin nombre en la tabla del kernel) | PM `setAwdlConfig` | `0xffffff800221c9ea+0x2a` |
| 456/467 | (sin nombre; métricas P2P/RTG) | PM | `0xffffff800222a34a+0xe`, `0xffffff800222f105+0xef` |

Los tipos solo-GET de gran tamaño (`AWDL_PEER_DATABASE` 203, `AWDL_STATISTICS` 223,
`AWDL_LOW_LATENCY_STATISTICS` 343, `AWDL_AIRPLAY_STATISTICS` 370, `AWDL_CTL` 162) no muestran
emisores propios: se leen desde userspace con `Apple80211Get`/`CopyValue` genéricos (airportd:
GET 223 en `sub_1000AE685` `0x1000ae685`) o son consumo interno del manager; `?` para el resto.

## 9. Método y límites

- Barridos de bytes (constants) sobre el caché completo y el `.kc`: `sweep.py`, `kc_sweep.py`,
  `kc_types.py` en `Build/ida_awdl/task_awdl_issuers/` (salidas `kc_types_out.txt`,
  `kc_issuers_full.txt`).
- Symtab de `IO80211.framework` extraída del pool `__LINKEDIT` (`extract_syms.py` →
  `io80211_exports.txt`, 1275 símbolos); `Apple80211.framework` (`13 símbolos AC*`) y
  `CoreWiFi.framework` (969 símbolos, sin nombres AWDL porque son métodos ObjC) con el mismo
  método. La extracción de `WiFiPeerToPeer.framework` falló (el pool del subcache `.02` no se
  pudo anclar por contenido); sus strings se usan como evidencia de clases (§5.2).
- Daemons: copias `x86_64` (`bin/`) analizadas con IDA headless (`probe1.py`, `probe2.py` sobre
  `airportd_copy.i64`; `probe_all.py`/`probe3.py`/`probe4.py` sobre el `__TEXT` crudo de
  `IO80211.framework` en `io80211fat.macho` — la carga Mach-O sintética con vmaddrs del caché no
  fue aceptada por el loader y se recurrió a carga binaria con `0x4000` de cabecera fat; los
  límites de función del análisis crudo no coinciden 1:1 con el symtab, por eso algunos sites
  citan la función contenedora del análisis y no el símbolo).
- Quedan como `?`: el selector concreto del `IO80211APIUserClient` que llega a
  `performGatedCommandIOUC`; qué proceso invoca cada setter tipado de CoreWiFi; los sites PM
  donde la ventana de inmediatos no aisló el tipo con confianza; los tipos 124/133/139/162
  (sin emisor observado en ninguno de los dos lados); y la semántica completa de 432.
- La observación en hardware de cuándo se emite cada request sigue pendiente (los «cuándo» de
  §4/§6.2 se infieren del nombre y de los strings de log de las funciones emisoras).
