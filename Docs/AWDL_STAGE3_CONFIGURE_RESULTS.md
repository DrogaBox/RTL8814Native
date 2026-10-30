# Etapa 3: resultado en hardware de `configure()`, `interfaceStateChange` y `enableAWDL`

Resultado de las pruebas en el equipo de desarrollo con la capa de reconocimiento de ioctls de la etapa 3
(parches `Tools/awdl_stage3_configure.patch`, `Tools/awdl_stage3b_sync.patch` y
`Tools/awdl_stage3c_acks.patch`): qué cadena de ioctls internos recorre `IO80211AWDLPeerManager` desde que
`awdl0` sube hasta que AWDL queda habilitado, cuáles de ellos son fatales y qué queda sin implementar.
Equipo: macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), `RTL8814Native.kext` 4.34 y `RTL8814AirLayer.kext`
0.32.0, `rtlair=2 rtlawdl=1`. Las direcciones son VA del kernelcache de esa build; lo no visto va con `?`.

## 1. Resultado

1. **Con los seis setters de la etapa 3 (125, 122, 132, 135, 213, 134) y `setLinkState(2)` en `enable()`,
   `configure()` termina** (`IO80211AWDLPeerManager::configure configure done!`, `doEnable ... Setting
   _enabled to TRUE`). El estado queda en `0x21 CONFIGURED OFF`.
2. **La primera petición de servicio entra en `interfaceStateChange` y aborta en el ioctl 116**
   (`AWDL_SYNC_PARAMS`, línea 6005 de la función): el retorno `0xE00002C7` (no soportado) lleva a
   `Unable to set SYNC params ret -536870201`, enlace abajo, `disableAWDL` y `_stateFlags = 0x20`. Las
   peticiones 120 siguientes devolvían 6 con `Received service request but not configured
   (_stateFlags:0x20)`.
3. **Con el reconocimiento de 116, 117, 226, 129, 155, 151, 205, 208, 229, 126, 118 y 418 la secuencia
   completa pasa**: `Initialize FW Election Params` → 129 → 117 → `AWDL ON:[infra(149) ...] (6/0/6) [6 6 6 6
   6 0 0 0 ...]`. El ioctl 120 (`PEER_SERVICE_REQUEST`) devuelve **0**, y `wifip2pd` registra las
   búsquedas `_airplay-p2p._tcp`, `_raop._tcp` y `_airplay._tcp`. Sin pánicos ni cuelgues durante el arranque
   y la prueba. Las pruebas de 3b y 3c se hicieron juntas; 3b por sí sola no se probó.
4. **El manager corre la elección en solitario**: `runElection ... New P:<MAC propia> R:<MAC propia>`,
   `peers:0`, y llama a 116/151/134/155 cada segundo.

## 2. Cadena observada y fatalidad

Orden aproximado de los ioctls internos en el arranque de AWDL, con la consecuencia de un fallo:

| Orden | Tipo | Setter | Origen | Si falla |
|---:|---:|---|---|---|
| 1 | 125, 122, 132, 135, 213, 134 | BSSID, ELECTION_ID, MASTER_CHANNEL, RSSI_THRESHOLDS, MIN_RATE, SYNC_STATE | `configure` | aborta `configure` (213 no es fatal) |
| 2 | 132, 205 | MASTER_CHANNEL, SECONDARY_MASTER_CHANNEL | `programMasterChannels` | 132 fatal; 205 solo se registra |
| 3 | 151 | SYNC_FRAME_TEMPLATE | `updateSyncFrameTemplate` | solo se registra |
| 4 | **116** | SYNC_PARAMS | `interfaceStateChange` (6005) | **fatal en este llamante**: enlace abajo y `_stateFlags = 0x20`. Cualquier retorno distinto de 0 y de 82 |
| 5 | 226 | TXCAL_PERIOD | `setScheduleState` | solo `Unable to set txcal period` |
| 6 | 341 (lectura) | `AF_SECONDARY_PAYLOAD_CAPABILITIES` | `enableAWDL` | `ioctl awdl_cap failed`; declara 0 capacidades y sigue |
| 7 | 155 | ELECTION_PARAMS | `initFWElectionParams` | solo se registra |
| 8 | 129 | SYNCHRONIZATION_CHANNEL_SEQUENCE | `setChannelSequenceUnconditional` | `Couldn't set channel sequence`; recuperación de secuencia |
| 9 | **117** | SYNC_ENABLED | `enableAWDL` | fatal: `Failure to enable AWDL`, `+414 = 0` |
| 10 | 208 | AF_TX_MODE | `setAFTXMode` | solo se registra |

Corrección a `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.13: el 116 figura allí como no fatal. Lo es en
`setSyncFramePeriod`, pero en `interfaceStateChange` el fallo del 116 deshabilita AWDL. El resto de la
tabla coincide con lo observado.

## 3. Lo que sigue sin implementar (no frena el arranque)

- `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` (341), `getAWDL_SYNC_ENABLED`, `getVHtCapabilityIE`/capacidades HT:
  devuelven `0xE00002C7`; el manager lo registra y continúa (`awdl_cap 0x0`, `Invalid _htCapabilityLength 0`).
- `setAWDL_OOB_AUTO_REQUEST` falla en cada plantilla de sync (`0xE00002C7`).
- `APPLE80211_IOC_P2P_COEX_PROTECTION` (414): `Unable to set Coex Protection ... unexpected call`;
  `setAwdlElectionMetric (65)` y `notifyRealTimeModeOnOff` fallan igual.
- Peticiones de `wifip2pd` al crear `awdl0` fuera de la superficie AWDL: 293 (`COMPANION_SKYWALK_IF_CREATE`)
  y 575 (`OS_ELIGIBILITY`, `0xE082280E`) siguen devolviendo error; el daemon no las trata como fatales.

## 4. Lo que falta para que haya ventanas y pares

- **El driver no publica el evento 41** (inicio de ventana de disponibilidad), ni el 42/43. El manager lo
  registra cada segundo: `monitorAWEvents WARNING: Did not get AW start event ...` y, a los 4 s,
  `ERROR: aw start event missing for too long`. Es un registro, no se toma ninguna acción. La ruta de
  entrega es `postMessage(41, payload, 36, false)` por el slot `0xB18` de la interfaz
  (`AWDL_STAGE3_EVENT_EMITTERS.md` §3) y el layout está en `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2.
- Emitirlo sin radio que respalde las ventanas haría que `inAvailabilityWindow()` devolviera cierto y el
  manager empezara a tirar de TX (`dequeueTxPacket`), cuya acción hoy es una no-op. Por eso queda diferido
  hasta tener TX y RX reales por el USB.
- Pendientes que dependen del hardware: temporización de las ventanas con el RTL8814AU, la cadencia real de
  `AwNumber` y `FwAwStartTime` (`?`), y un par AWDL con el que validar la elección, los PSF y los MIF.
  Ver `AWDL_ETAPAS_2_A_4.md` §1 y `AWDL_RTL8814_HW_SUPPORT.md`.

## 5. Parches y despliegue

| Parche | Contenido |
|---|---|
| `Tools/awdl_stage3_configure.patch` | `setLinkState(2)` en `enable()` y `setLinkState(1)` en `disable()`; reconocimiento de los seis setters de `configure` |
| `Tools/awdl_stage3b_sync.patch` | reconocimiento de 116, 117 y 226 |
| `Tools/awdl_stage3c_acks.patch` | reconocimiento de 129, 155, 151, 205, 208, 229, 126, 118 y 418 |

Los reconocimientos no tocan la radio; solo evitan que el manager aborte. `Build/RTL8814AirLayer.kext` con
los tres parches, md5 `647f1ab25c3aa2c32a252b2e60371d8b`, desplegado en el EFI interno con
`RTL8814_AWDL=1 Tools/phase5_deploy.sh <EFI> layer 2`.

## 6. Método

Registros de `log show` del kernel (`com.apple.p2p.awdl0`, `IOC DEBUG`) y de `wifip2pd` tras el arranque y
tras abrir AirDrop en el Finder (sin Bluetooth, la ventana no muestra pares; la petición 120 sale igual).
Código de `interfaceStateChange` (`0xffffff80021d8cc8`), `enableAWDL` (`0xffffff80021dcf58`),
`setScheduleState` (`0xffffff80021d27a0`) y `programMasterChannels` (`0xffffff80021d494a`) leído en IDA
sobre una copia de la IDB. El buffer circular del registro del kext (`RTLAirLayerLog`) guarda solo 200
líneas y se llena con los escaneos de WiFi; para AWDL es más fiable el registro del sistema.
