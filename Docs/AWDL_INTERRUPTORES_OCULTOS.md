# Interruptores ocultos de AWDL en 26.7.3 y novedades de macOS 27

Lectura del 2026-11-23. Fuente: informe de trabajo `~/re_cache/work/awdl_refs/notes/wifi_discovery_27.md`
(diff de símbolos y cadenas 26.7.3 contra 27.2.1, más descompilación de los lectores). Los boot-args
marcados como verificados se comprobaron en `IO80211AWDLPeerManager::initWithInterface` (0x3E728) y
`IO80211P2PSupervisor::initWithInterface` (0x20DDFC) de la IDB con nombres del KDK 25G342. Nada se probó en
la máquina.

## 1. Boot-args de IO80211Family 26.7.3 (afectan a la capa que usa AirLayer)

Todos se leen con `PE_parse_boot_argn`: hacen falta en `boot-args` y un reinicio. Lista completa (64
funciones lectoras): `~/re_cache/ida/fb_awdl27/bootargs26.txt`.

| Boot-arg | Efecto | Estado |
|---|---|---|
| `awdl_enable` | Su sola presencia pone `_bonjourTriggersAWDL` = 0 (ivars+42776). No activa AWDL: evita que Bonjour lo levante. | verificado |
| `awdl_allow_40` | Escribe `valor == 0` en el campo de ancho máximo (ivars+43636): `=0` deja 1. Nombre engañoso. | verificado |
| `awdl_max_bw` | MHz → clase en ivars+43636: 20-39 → 0, 40-79 → 1, 80-159 → 2, 160 → 3; ≥161 → 0. | verificado el tope 0xA1; tramos según el informe |
| `peer_channel_steer_timeout` | Tiempo de cambio de canal hacia el par, ms (P2PSupervisor +696). | verificado |
| `peer_channel_steer_max_attempt` | Intentos máximos, por defecto 5 (+700). | verificado |
| `awdl_oper_mode=1` | Fuerza la estrategia de secuencia de canales 3. | informe |
| `enable_awdl_leaf=0` | Desactiva el modo solo-hoja; solo actúa si la capacidad 0x40 de +2884 está puesta. | informe |
| `enable_p2p_dfsp=0` | Apaga el proxy DFS de P2P. | informe |
| `enable_awdl_nan_concurrency=0` | Apaga la concurrencia AWDL/NAN (el log dice lo contrario). | informe |
| `enable_aggressive_edca` | Marca `_aggressiveEDCA` en el gestor NAN; en 26.7.3 no hay emisor ni ioctl 607. | informe |
| `dynamic_buffer` | ≠0 elige `IO80211DynamicBufferPool` en vez del estático. | verificado en la lectura de 27 contra 26 |
| `awdl_bssid`, `awdl_election_id`, `awdl_fw_election`, `override_awdl_platform_type` | BSSID, id de elección, elección en firmware y tipo de plataforma de AWDL. | informe |
| `awdl_mdns_mode`, `awdl_bonjour_offload`, `awdl_block_mdns`, `awdl_disable_serviceDiscovery3` | Control de mDNS y Bonjour sobre AWDL. | informe |
| `wlan.debug.enable`, `io80211.debug.enable` | Habilitan los comandos de depuración de IO80211. | informe |

`wlan.disableIOCTL`, `wlan.internal` y `wlan.factory` existen pero no son funciones de producto.

## 2. Preferencias en 26.7.3

- `com.apple.sharingd` (`defaults`): `DiscoverableMode` (aquí `Everyone`), estado de hashes de AirDrop.
- `/Library/Preferences/SystemConfiguration/com.apple.wifip2p.plist` (no existe en esta máquina): claves
  `debug`, `policyBased`, `awdl`, `awdlMACAddress`, `nanMACAddress`, `enableNANPairing*`,
  `enableUserspaceP2POptions`, `disableAuthentication`, `automaticallyUseEncryption`. Tipos y efectos
  deducidos de los nombres, sin descompilar; las dos últimas son de laboratorio.
- `com.apple.MobileBluetooth.debug`: `Logging`, `EnableVerboseL2CAPDump` (registro de Bluetooth).
- 26.7.3 ya tiene `supportsDisableAWDLOnInactivity` ("Disabling AWDL due to inactivity") en `wifip2pd`.

## 3. Solo en macOS 27

- `AWDLDiscoveryTimeout` en `wifip2pd`: plist por defecto
  `/System/Library/IdentityServices/ServiceDefinitions/com.apple.wifip2pd.InactivityConfig.plist` y
  plist local `/Library/Preferences/SystemConfiguration/com.apple.wifip2p.InactivityConfig.plist`, con
  `inactivityTimeoutSeconds`, `displayOnGracePeriodSeconds`, `systemWakeGracePeriodSeconds`,
  `allowedClientList`, `ExcludeServices`, `displayAgnostic`.
- EDCA agresivo durante AirDrop (ioctl 607, capacidad 179), ver `AWDL_SUNRISE_MEDIATEK.md` §7.
- `rapportd` `airDropBLEScanRate` (`Background` … `Aggressive`) y `AirDropAdvertiseRate` en Sharing.
- Claves de banco de pruebas NAN en `com.apple.wifip2p.plist`: `enabledTestbed*`, `tdsTestbedMode`,
  `enable6GHzDCEA`; desaparece `enableNANPairing`.

## 4. AirPlay: AWDL, NAN y reloj

- 26.7.3 `AirPlaySender` ya soporta NAN (`SupportsNAN`, `Open NAN full/partial`, log
  `preferAWDL flag %d requireNAN flag %d`): NAN en AirPlay no es nuevo de 27. 27 agrega
  `ForcedTransportType` (AWDL / Infra / NAN) y audio "responsive" por NAN.
- Elección (deducida de cadenas): por punto final, según `SupportsNAN`/`SupportsAWDL` y esos dos flags;
  AWDL preferido salvo que se exija NAN.
- `CoreMedia` 26.7.3 usa `TimeSyncClockAddAWDLPort`: el reloj gPTP del audio AirPlay va por el puerto
  AWDL. Del lado del driver pide el registro de tráfico del par de 184 bytes (habilitar en +168) y
  `APPLE80211_IOC_TIMESYNC_INFO` (406, según `AWDL_APPLE80211_DISPATCH.md`).

## 5. Ideas para RTL8814Native

- Sin código: `awdl_max_bw`, `peer_channel_steer_timeout` y `enable_p2p_dfsp=0` para pruebas
  controladas. Evitar `awdl_enable` y `awdl_allow_40` por sus nombres engañosos.
- Con código en driver o AirLayer: EDCA más agresivo en la cola AWDL durante una transferencia (idea de
  27); TimeSync sobre AWDL (406 y el registro de 184 bytes) si el audio de AirPlay se desfasa.
