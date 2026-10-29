# Puentes apple80211 que llegan a los slots puros de `IO80211AWDLProtocol`

Mapa de los 63 métodos puros de `IO80211AWDLProtocol` (offsets `0xFC8`-`0x11B8` desde el vptr,
vptr = ZTV + 0x10) a los puentes `apple80211get*`/`apple80211set*` de IO80211Family que los invocan.
Base: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). Nombres de método tomados de
`__ZTV30AppleBCMWLANProximityInterface` (`0xffffff80017f5b68`), que implementa los 63.

## 1. Resultado

Cada slot puro tiene un puente propio con el mismo nombre (`apple80211` + método) que hace
`safeMetaCast(interfaz, IO80211AWDLProtocol::gMetaClass)` y salta al slot. No hay slots puros sin
puente: los 63 son alcanzables desde apple80211. Como el cast exige que el objeto sea un
`IO80211AWDLProtocol`, sólo se alcanzan cuando la petición se emite sobre `awdl0`; sobre `en2` el
cast falla y el puente prueba otras clases o devuelve error.

Antes del cast, todos los puentes llaman a `isCommandProhibited(tipo)` por el slot `+0xCC8`.
Qué código de petición y qué ruta (tabla moderna, legacy o interna, o el atajo al
`IO80211AWDLPeerManager`) llevan a cada puente está en `AWDL_APPLE80211_DISPATCH.md`.

Método de obtención: barrido de todas las funciones `apple80211get*`/`apple80211set*` buscando el
`lea rsi, IO80211AWDLProtocol::gMetaClass` y la primera carga `mov reg, [reg+disp]` con `disp` en
`0xFC8`-`0x11B8` posterior al cast. En 51 puentes la carga aparece explícita y su offset coincide
con el método del mismo nombre en los 51 casos. En 12 puentes el cast forma parte de una cascada de
varias clases (`IO80211NoneProtocol`, `IO80211SapProtocol`, `IO80211NANProtocol`, …) con un único
`jmp rax` final. Ahí el offset no aparece como desplazamiento inmediato después del cast, y la
correspondencia se da por el nombre. Esas filas llevan `cascada` en la columna de carga.

## 2. Tabla

| Offset | Método | Puente | Cast a `IO80211AWDLProtocol` | Carga del slot |
|---:|---|---|---|---|
| `0xFC8` | `getSSID` | `apple80211getSSID` | `0xffffff80022626e3` | cascada |
| `0xFD0` | `getCHANNEL` | `apple80211getCHANNEL` | `0xffffff80022627be` | cascada |
| `0xFD8` | `getBSSID` | `apple80211getBSSID` | `0xffffff8002262953` | cascada |
| `0xFE0` | `getSTATE` | `apple80211getSTATE` | `0xffffff8002262a87` | cascada |
| `0xFE8` | `getAWDL_SYNC_PARAMS` | `apple80211getAWDL_SYNC_PARAMS` | `0xffffff8002263868` | `0xffffff800226387f` |
| `0xFF0` | `getAWDL_SYNC_ENABLED` | `apple80211getAWDL_SYNC_ENABLED` | `0xffffff80022638bd` | `0xffffff80022638d4` |
| `0xFF8` | `getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `apple80211getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `0xffffff8002263912` | `0xffffff8002263929` |
| `0x1000` | `getAWDL_ELECTION_ID` | `apple80211getAWDL_ELECTION_ID` | `0xffffff8002263988` | `0xffffff800226399f` |
| `0x1008` | `getAWDL_ELECTION_METRIC` | `apple80211getAWDL_ELECTION_METRIC` | `0xffffff80022639fe` | `0xffffff8002263a15` |
| `0x1010` | `getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `apple80211getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `0xffffff8002263a69` | `0xffffff8002263a80` |
| `0x1018` | `getPEER_CACHE_MAXIMUM_SIZE` | `apple80211getPEER_CACHE_MAXIMUM_SIZE` | `0xffffff8002263abe` | cascada |
| `0x1020` | `getAWDL_MASTER_CHANNEL` | `apple80211getAWDL_MASTER_CHANNEL` | `0xffffff8002263b39` | `0xffffff8002263b50` |
| `0x1028` | `getAWDL_SYNC_STATE` | `apple80211getAWDL_SYNC_STATE` | `0xffffff8002263b99` | `0xffffff8002263bb0` |
| `0x1030` | `getAWDL_ELECTION_RSSI_THRESHOLDS` | `apple80211getAWDL_ELECTION_RSSI_THRESHOLDS` | `0xffffff8002263bee` | `0xffffff8002263c05` |
| `0x1038` | `getAWDL_PRESENCE_MODE` | `apple80211getAWDL_PRESENCE_MODE` | `0xffffff8002263c43` | `0xffffff8002263c5a` |
| `0x1040` | `getAWDL_ELECTION_PARAMS` | `apple80211getAWDL_ELECTION_PARAMS` | `0xffffff8002263da8` | `0xffffff8002263dbf` |
| `0x1048` | `getAWDL_DEVICE_CAPABILITIES` | `apple80211getAWDL_DEVICE_CAPABILITIES` | `0xffffff8002263e5d` | `0xffffff8002263e74` |
| `0x1050` | `getAWDL_SECONDARY_MASTER_CHANNEL` | `apple80211getAWDL_SECONDARY_MASTER_CHANNEL` | `0xffffff800226421e` | `0xffffff8002264235` |
| `0x1058` | `getAWDL_AF_TX_MODE` | `apple80211getAWDL_AF_TX_MODE` | `0xffffff80022642d3` | `0xffffff80022642ea` |
| `0x1060` | `getAWDL_MIN_RATE` | `apple80211getAWDL_MIN_RATE` | `0xffffff8002264354` | `0xffffff800226436b` |
| `0x1068` | `getAWDL_ENCRYPTION_TYPE` | `apple80211getAWDL_ENCRYPTION_TYPE` | `0xffffff80022644c9` | `0xffffff80022644e0` |
| `0x1070` | `getAWDL_TXCAL_PERIOD` | `apple80211getAWDL_TXCAL_PERIOD` | `0xffffff8002264555` | `0xffffff800226456c` |
| `0x1078` | `getAWDL_DEV_STATS` | `apple80211getAWDL_DEV_STATS` | `0xffffff80022648d6` | `0xffffff80022648ed` |
| `0x1080` | `getAWDL_DFSP_CONFIG` | `apple80211getAWDL_DFSP_CONFIG` | `0xffffff8002264ca9` | cascada |
| `0x1088` | `getAWDL_CCA` | `apple80211getAWDL_CCA` | `0xffffff8002265aba` | `0xffffff8002265ad1` |
| `0x1090` | `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` | `apple80211getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` | `0xffffff8002265b6f` | `0xffffff8002265b86` |
| `0x1098` | `getLLW_PARAMS` | `apple80211getLLW_PARAMS` | `0xffffff8002265bda` | `0xffffff8002265bf1` |
| `0x10A0` | `setSSID` | `apple80211setSSID` | `0xffffff800226787c` | cascada |
| `0x10A8` | `setCHANNEL` | `apple80211setCHANNEL` | `0xffffff8002267967` | cascada |
| `0x10B0` | `setAWDL_SYNC_PARAMS` | `apple80211setAWDL_SYNC_PARAMS` | `0xffffff8002268688` | `0xffffff800226869f` |
| `0x10B8` | `setAWDL_SYNC_ENABLED` | `apple80211setAWDL_SYNC_ENABLED` | `0xffffff80022686dd` | `0xffffff80022686f4` |
| `0x10C0` | `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `apple80211setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `0xffffff8002268732` | `0xffffff8002268749` |
| `0x10C8` | `setAWDL_ELECTION_ID` | `apple80211setAWDL_ELECTION_ID` | `0xffffff80022687a8` | `0xffffff80022687bf` |
| `0x10D0` | `setAWDL_BSSID` | `apple80211setAWDL_BSSID` | `0xffffff8002268813` | `0xffffff800226882a` |
| `0x10D8` | `setAWDL_ELECTION_METRIC` | `apple80211setAWDL_ELECTION_METRIC` | `0xffffff8002268868` | `0xffffff800226887f` |
| `0x10E0` | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `apple80211setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `0xffffff80022688d3` | `0xffffff80022688ea` |
| `0x10E8` | `setAWDL_MASTER_CHANNEL` | `apple80211setAWDL_MASTER_CHANNEL` | `0xffffff800226893e` | `0xffffff8002268955` |
| `0x10F0` | `setAWDL_SYNC_STATE` | `apple80211setAWDL_SYNC_STATE` | `0xffffff800226899e` | `0xffffff80022689b5` |
| `0x10F8` | `setAWDL_ELECTION_RSSI_THRESHOLDS` | `apple80211setAWDL_ELECTION_RSSI_THRESHOLDS` | `0xffffff80022689f3` | `0xffffff8002268a0a` |
| `0x1100` | `setAWDL_PRESENCE_MODE` | `apple80211setAWDL_PRESENCE_MODE` | `0xffffff8002268a48` | `0xffffff8002268a5f` |
| `0x1108` | `setPEER_CACHE_CONTROL` | `apple80211setPEER_CACHE_CONTROL` | `0xffffff8002268adf` | cascada |
| `0x1110` | `setAWDL_OOB_REQUEST` | `apple80211setAWDL_OOB_REQUEST` | `0xffffff8002268b86` | `0xffffff8002268b9d` |
| `0x1118` | `setAWDL_SYNC_FRAME_TEMPLATE` | `apple80211setAWDL_SYNC_FRAME_TEMPLATE` | `0xffffff8002268be6` | `0xffffff8002268bfd` |
| `0x1120` | `setAWDL_ELECTION_PARAMS` | `apple80211setAWDL_ELECTION_PARAMS` | `0xffffff8002268ca6` | `0xffffff8002268cbd` |
| `0x1128` | `setAWDL_PEER_TRAFFIC_REGISTRATION` | `apple80211setAWDL_PEER_TRAFFIC_REGISTRATION` | `0xffffff8002268dac` | `0xffffff8002268dc3` |
| `0x1130` | `setAWDL_SECONDARY_MASTER_CHANNEL` | `apple80211setAWDL_SECONDARY_MASTER_CHANNEL` | `0xffffff800226912b` | `0xffffff8002269142` |
| `0x1138` | `setAWDL_AF_TX_MODE` | `apple80211setAWDL_AF_TX_MODE` | `0xffffff8002269196` | `0xffffff80022691ad` |
| `0x1140` | `setAWDL_MIN_RATE` | `apple80211setAWDL_MIN_RATE` | `0xffffff8002269217` | `0xffffff800226922e` |
| `0x1148` | `setAWDL_ENCRYPTION_KEYS` | `apple80211setAWDL_ENCRYPTION_KEYS` | `0xffffff8002269337` | `0xffffff800226934e` |
| `0x1150` | `setAWDL_ENCRYPTION_TYPE` | `apple80211setAWDL_ENCRYPTION_TYPE` | `0xffffff800226938c` | `0xffffff80022693a3` |
| `0x1158` | `setAWDL_OOB_AUTO_REQUEST` | `apple80211setAWDL_OOB_AUTO_REQUEST` | `0xffffff8002269457` | `0xffffff800226946e` |
| `0x1160` | `setAWDL_TXCAL_PERIOD` | `apple80211setAWDL_TXCAL_PERIOD` | `0xffffff80022694ac` | `0xffffff80022694c3` |
| `0x1168` | `setAWDL_AWDL_ADVERTISERS` | `apple80211setAWDL_AWDL_ADVERTISERS` | `0xffffff8002269561` | `0xffffff8002269578` |
| `0x1170` | `setAWDL_DFSP_CONFIG` | `apple80211setAWDL_DFSP_CONFIG` | `0xffffff8002269ad8` | cascada |
| `0x1178` | `setAWDL_DFSP_UCSA_CONFIG` | `apple80211setAWDL_DFSP_UCSA_CONFIG` | `0xffffff8002269b48` | `0xffffff8002269b5f` |
| `0x1180` | `setCOMPANION_SKYWALK_IF_CREATE` | `apple80211setCOMPANION_SKYWALK_IF_CREATE` | `0xffffff800226a353` | cascada |
| `0x1188` | `setAWDL_OOB_SEC_PAYLOAD_REQUEST` | `apple80211setAWDL_OOB_SEC_PAYLOAD_REQUEST` | `0xffffff800226ae4e` | `0xffffff800226ae65` |
| `0x1190` | `setLLW_PARAMS` | `apple80211setLLW_PARAMS` | `0xffffff800226aeae` | `0xffffff800226aec5` |
| `0x1198` | `setTX_RATE` | `apple80211setTX_RATE` | `0xffffff800226b86d` | `0xffffff800226b884` |
| `0x11A0` | `setP2P_COEX_PROTECTION` | `apple80211setP2P_COEX_PROTECTION` | `0xffffff800226b985` | cascada |
| `0x11A8` | `setPSF_DWELL` | `apple80211setPSF_DWELL` | `0xffffff800226b9fd` | `0xffffff800226ba14` |
| `0x11B0` | `setAWDL_CONFIG` | `apple80211setAWDL_CONFIG` | `0xffffff800226bce7` | `0xffffff800226bcfe` |
| `0x11B8` | `setAWDL_RTG_PEER_STATS_CONFIG` | `apple80211setAWDL_RTG_PEER_STATS_CONFIG` | `0xffffff800226c39c` | `0xffffff800226c3b3` |

## 3. Consecuencia para la etapa 1

Los 63 stubs de `RTLAwdInterface` son la respuesta que reciben las peticiones apple80211 sobre
`awdl0`. Devolver `kIOReturnUnsupported` en un `get*` que wifip2pd consulta durante el arranque
corta su secuencia. Qué peticiones emite el daemon tras crear la interfaz, y en qué orden, está en
`AWDL_WIFIP2PD_POST_CREATE.md` y `AWDL_WIFIP2PD_REQUESTS.md`. Los stubs de esos métodos son los
primeros que necesitan un valor válido.
