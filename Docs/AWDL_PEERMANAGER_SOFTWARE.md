# IO80211AWDLPeerManager: lo que implementa en software y lo que exige al driver

Complementa `AWDL_FIRMWARE_DUTIES.md` (lo que hace el firmware Broadcom en el rol 6) y
`AWDL_PURE_SLOTS.md` (los slots puros de `IO80211AWDLProtocol`). Aquí se documenta el gestor de
peers AWDL que vive dentro de IO80211Family: qué cálculos hace él solo, qué espera del driver que
tiene debajo y qué pasaría si un kext de terceros con una interfaz que subclasea las clases AWDL
de IO80211 intentara reutilizarlo en lugar de reimplementar elección y sincronía.

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
IDB copia en `Build/ida_awdl/task_awdl_peermgr/kc.i64`, descompilación con Hex-Rays de ~60 funciones
de `IO80211AWDLPeerManager`/`IO80211AWDLPeer`. Los nombres de función existen en el IDB (strtab
residual del kext); todos los offsets de ivars citados pertenecen a esa build y no están
congelados por ninguna ABI. Lo no verificado se marca `?`.

## 1. Resumen

`IO80211AWDLPeerManager` es una implementación de software completa de la parte "protocolo" de
AWDL: elección de maestro con métricas y árbol de sincronía, construcción de todas las plantillas
de master indication (TLVs), parseo de los TLVs de los pares, tabla de peers con su base de datos,
máquina de ventanas de disponibilidad y de secuencia de canales, y una docena de timers propios
sobre el workloop del controlador. Lo único que delega hacia abajo es la ejecución física: el TX de
los frames de sincronía en los instantes programados, el cambio de canal, la captura de los action
frames y el transporte de los paquetes de datos. La delegación se hace por dos caminos: ioctls
apple80211 internos contra la interfaz (los mismos tipos que hoy Broadcom reenvía al firmware) y
las celdas de su propia vtable que la pila de la interfaz invoca (RX de action frames, eventos,
pull de paquetes).

## 2. Instanciación y ciclo de vida

### 2.1 Quién lo crea

`IO80211VirtualInterface::createPeerManager` (`0xffffff80023308c2`, ranura vtable 355 contando desde el inicio de `__ZTV` (offset +0xB08 desde el vptr; las ranuras de este documento siguen esa convención), heredable: la vtable de `AppleBCMWLANProximityInterface` apunta a la misma función en +0xB08):

- Lee el rol de la interfaz (`*(interfaz_ivar+288)+88`; el mismo valor que `setInterfaceRole`
  publica como property, ver `AWDL_INTERFACE_BIRTH.md`) y el MAC propio con
  `IO80211SkywalkInterface::getSelfMacAddr` (ranura vtable 417).
- Rol 6 → `IO80211AWDLPeerManager::withInterface` (`0xffffff80021c14bc`). Rol 7 →
  `IO80211PeerManager::withInterface`, roles 8/9 → `IO80211NANPeerManager::withInterface`, rol 10
  → `IO80211NANDataInterfacePeerManager::withInterface` (misma función, rama por rama).
- El único llamador de `withInterface` es `createPeerManager` (xref `0xffffff8002330957`).
- Tras crear el manager lo registra en el controlador: `getController` (ranura 375) →
  `*(ctrl+288)+400 = interfaz` (400 para AWDL; 408 para el PeerManager genérico; 944/952 NAN).
  El `AWDL_APPLE80211_DISPATCH.md` ya describía el camino inverso (safeMetaCast desde ivars del
  controlador).

`IO80211AWDLPeerManager::withInterface`: `OSObject_typed_operator_new(&IO80211AWDLPeerManager_ktv, 40)`
(view de kalloc en `0xffffff8002493d80`), asigna la vtable `0xffffff800245a868`, registra en el
`gMetaClass` (`0xffffff800248d2e0`) y llama a `initWithInterface`. El objeto C++ en sí es pequeño
(40 B); el estado vive en dos bloques separados que monta `initWithInterface`.

### 2.2 `initWithInterface` (`0xffffff80021c1540`, 0x1789 B)

- Llama al base `IO80211PeerManager::initWithInterface` (que deja sus ivars en `this+0x18`) y
  aloja el bloque de ivars AWDL con `IOMallocTypeImpl` (view `kalloc_type_view_2098`) en
  `this+0x20`: más de 66 KB de estado. Campos con nombre propio verificables por los logs:
  self MAC `+37216`, bloque de elección `+37228..+37310` (copia previa en `+37271`), election id
  configurable `+63992`, bins de métrica `+43888..` (índices 5486–5511, doubles), periodos de
  ventana `+62940/+62944/+62948/+62952`, secuencia de canales `+42864` (longitud u16) y `+42868`
  (entradas de 12 B), anillo de action frames `+16848..+16872`, plantilla UMI `+9048` (1200 B),
  timers `+10312..+10408`.
- Boot-args leídos: `awdl_disable_dp_umi`, `awdl_enable`, `awdl_oper_mode`, `awdl_debug_tooLong`,
  `enable_forced_roam`, `_discard_airplay_records`, `awdl_mdns_mode`, `awdl_bonjour_offload`,
  `awdl_mdns_direct_path`, `awdl_block_mdns`, `awdl_disable_serviceDiscovery3`, `awdl_log_flags`,
  `awdl_bssid`, `awdl_election_id`, `awdl_fw_election` (→ `+10305`, fuerza elección en firmware),
  `sidecar_stats_lat_lim/time_lim`, `disable_p2pBssSteering`, `wlan.splitAWDLAFThread` (→ `+16880`),
  `awdl_allow_40`, `awdl_max_bw`, `forcecountry`, `dis_assist_disc_timer`, `unifiedcountry`,
  `awdl_sidecar`, `override_awdl_platform_type`.
- Crea 14 `IO80211TimerSource::allocWithParams` sobre el workloop del controlador
  (`getController` → vtable `+3176`): `handleInfraMonitorTimer` `+10384`,
  `handleAwdlScanTimer` `+10392`, `handleAwdlLLATimer` `+10400`,
  `handleMasterIndicationTimer` `+10312`, `handleAWDLRecoveryTimer` `+10352`,
  `handleUmiTimer` `+10320`, `handleMulticastTimer` `+10328`,
  `handleAssistedDiscoveryTimedOut` `+10336`, `handleSuspendTimer` `+10344`,
  `handleCcChangeTimer` (ivars base `+2480`), `jumpStartDFSProxy` `+10360` (tipo 0),
  `discoveryTimeout` `+10368`, `handleLinkChangeTimer` `+10376` y
  `actionFrameProcessThread` `+10408`. Más dos colas de comandos:
  `_serviceRequests` (`IO80211CommandQueue`, capacidad 0x100, ivars `+42440`) y
  `_serviceResponsesReceiving` (capacidad 1, `+42432`), y colas de paquetes
  `IOPacketQueue` (3 en `+10424`, 3 en `+10448`, capacidad 0x19).
- Crea los subordinados: `IO80211PeerBssSteeringManager::withManager(this, 6)` (`+16616`),
  `IO80211P2PSupervisor::withInterface` (`+480`, con backpointer al manager),
  `IO80211RealTimePeerManager::withInterface` (`+488`), un bloque de 6656 B (`+16840`), y
  `initAwdlPacketDescriptorPool(0x100)`.

### 2.3 Habilitar y deshabilitar

- `interfaceStateChange` (`0xffffff80021d8cc8`, celda 55 de la vtable, `+0x1B8`) →
  `enableAWDL` (`0xffffff80021dcf58`); `doDisable` (`0xffffff80021c69cc`) →
  `disableAWDL(int)` (`0xffffff80021c6ae8`); `monitorAWDLState` (`0xffffff80021ed724`, watchdog
  periódico) también puede deshabilitar.
- `setAwdlCtl` (`0xffffff80021cde94`, celda 56, `+0x1C0`) es la petición de enable/disable que
  llega del dispatch apple80211 (AWDL_CTL).
- `enableAWDL`: comprueba link (`vtable+2952` sobre la interfaz, "Interface LinkDown"), lee
  capacidades con el ioctl interno de codificación `0x155C02869C9` (payload `awdl_cap`; los bits
  que distingue: `SEC_PAYLOAD` `+16576`, `CCA_STATS` `+16577`, `PSF_DWELL_MI` `+16578`,
  `PSF_DWELL_UMI` `+16579`), `resetElectionState`, `updateBroadcastMI`,
  `updateSyncFrameTemplate(true)`, `validateForHostapAWDLConcurrency`, `initFWElectionParams`,
  `setChannelSequenceUnconditional(true)`, enable propiamente dicho con el ioctl interno
  `0x75802869C8` (tipo 117, `setAWDL_SYNC_ENABLED`), `ageoutRootNodeList`,
  `initializeAwdlMetricBins`, armado del watchdog con 5000 ms, `postSyncStateChanged`,
  `setPSFDwell`, y notificación a la interfaz de infraestructura
  (`IO80211InfraInterface::notifyAWDLStateChange`).

## 3. Elección de maestro en software

### 3.1 Estado

Bloque `+37228..+37310` (actual) y copia previa `+37271..+37298` (comparadas con `memcmp` de
0x2B bytes en `electionParamsChanged`, `0xffffff80021e9f6c`):

| Campo | Offset ivars | Nota |
|---|---:|---|
| nodo padre (parent) | `+37237` (6 B) | copia previa `+37280` |
| relay | `+37235` (6 B) | copia previa `+37279` |
| raíz (root) | `+37231` (6 B) | copia previa `+37274` |
| rootLFC | `+37243` u32 | frame count del root |
| hopCount | `+37247` u32 | |
| rootMetric | `+37251` u32 | |
| selfMetric | `+37255` u32 | |
| electionId | `+37259` u32 | semilla `+63992` (boot-arg `awdl_election_id`) |
| selfLFC | `+37560` u32 | contador propio |

Otros flags: `+10304` "cambio de maestro pendiente de aplicar en firmware" (debounce 1001 ms),
`+10306` "elección reprogramada" (lo levanta el parseo de Sync Params del par,
`parseAwdlSyncParamsTlv`, outF línea 808), `+10307` "elección fijada" (`setAwdlFixedElectionToSelf`,
`0xffffff80021ed37e`), `+37484` maestro forzado, `+37485` maestro forzado RTG. Lista de nodos raíz
en `+37576`: 100 entradas de 48 B `{valid, root MAC +1, parent MAC +7, RLFC u32 +16, lastSeen u64
+24, HC +32, RMet +36, AW u32 +40}` con caducidad de 1 h (`ageoutRootNodeList`,
`0xffffff80021ebd0e`). Camino al root para el TLV de árbol en `+37500`.

### 3.2 El algoritmo

`runElection` (`0xffffff80021e3e7c`), disparado tras procesar action frames (ver §8):

1. Bloqueo del datapath (objeto en ivars base `+120`, vtable `+280`/`+288`; los métodos
   `IO80211PeerManager::lockDataPath/unlockDataPath` están en `0xffffff800224db2a`/`0x224dc1e`).
2. Si hay AF reciente de un par (`isPeerQualifiedForElection` + `isPeerSafeForElection`) →
   `setElectionParamsToPeer` + `updateRootNodeList`. Si el AF es viejo o el par no existe →
   "Setting Election Params to Self": parent/relay/root = MAC propia, rootLFC = selfLFC, rootMetric
   = selfMetric.
3. `runParentElection` (`0xffffff80021ec0e4`) recorre la tabla de peers (hash en ivars base
   `+1344`, cabecera `+56`, contador `+36`) y elige padre comparando, en orden: mayor rootMetric
   (peer ivars `+579`); a igualdad, menor electionId (`+559`, memcmp de 6 B); a igualdad, mayor
   rootLFC (`+571`); a igualdad, menor hopCount (`+575`). Si el propio nodo gana ("I am Root
   Node"), se queda como maestro: hopCount 0 y selfMetric como rootMetric.
4. `isPeerQualifiedForElection` (`0xffffff80021eacc0`): AF del par hace menos de 3000 ms (peer
   ivars `+784`), el par anuncia el mismo electionId (`+587` == ivars `+37259`) y, si el par es
   maestro electoral (peer `+120 == 2`), su RSSI supera los umbrales de
   `awdl_election_rssi_thresholds` (ivars `+10272`/`+10276`).
5. Métrica propia: `monitorElectionMetric` (`0xffffff80021ed47e`) recalcula con
   `setElectionMetricForAirplaySink`/`setElectionMetricForNonAirplaySink` y los bins de
   `initializeAwdlMetricBins` (`0xffffff80021fbc50`: 1.0, 2.0, 3.0, 5.0, 10.0, 255.0, −55.0,
   −65.0, −75.0 como dobles); por defecto 65 (o 43 con el flag `+66224`), ver
   `setElectionMetricsToDefault` (`0xffffff80021cb2cc`). La métrica crece con el tiempo en rol de
   esclavo: selfLFC `+1` cada 2501 ms.
6. Si `electionParamsChanged` detectó diferencia: `updateBroadcastMI` +
   `updateSyncFrameTemplate(true)` + `programChipWithNewMaster` y guardado de la copia previa.
   Cambios de parent/root contados en `+47748`/`+47752`. Hop count ≥ 4 genera un informe de
   diagnóstico (`selfDiagnosticsReport`, "HighHC").

`programChipWithNewMaster` (`0xffffff80021ec4b6`) es el punto donde la decisión se baja al driver,
siempre por ioctls (§9): advertisers del maestro (tipo 229), `setSyncFramePeriod`
(`0xffffff80021ed024`, tipo 116), estado de sync con el maestro elegido (tipo 134, payload con
modo `+37263`, master MAC, hopCount, rootMetric, selfMetric) y parámetros de elección (tipo 155,
payload `{u8 modo +0, u16 electionId +2, u8 hopCount +4, root MAC +6, relay MAC +12, u32 rootMetric
+16, u32 selfMetric +20}`). `initFWElectionParams` (`0xffffff80021ed200`) envía el mismo 155 al
habilitar. Tras programar, `monitorTimeline` verifica contra los eventos del driver que el cambio
de maestro se aplicó (§5).

## 4. Árbol de sincronía y plantillas de master indication

- El propio nodo mantiene el camino al root (`+37500`) y lo anuncia en el TLV de árbol:
  `buildSyncTreeTlv` (`0xffffff80022112fe`) escribe TLV tipo 20 con
  `len = 3 + 6·min(hopCount+1, 10)`. El lado receptor lo parsea
  `IO80211AWDLPeer::parseAwdlSyncTreeTLV` (`0xffffff80021b56d0`).
- El par mantiene la sincronía temporal con un peer de referencia
  (`setTimeSyncState` `0xffffff8002207e06` → `timeSyncPeer` `+16136`, `setTimeSyncChannelAndSchedule`
  `0xffffff80021f6124`, registro de tráfico `handleTimeSyncTrafficReg` `0xffffff8002207cd4`).
- Las plantillas que el maestro emite se construyen íntegramente en software:
  `updateSyncFrameTemplate` (`0xffffff80021dc5b2`) compone 512 B con
  `buildServiceParamTlv` + `buildAWDLElectionParamsVer2` + `buildAwdlDataPathStateTlv` +
  `buildEnhDataRateTlv` (TLV 7) + `buildRangingTlv` + `buildRealTimeGroupTlv` +
  `buildIEEE80211CntnrTlv` (TLV 17) + `buildAWDLVersionTlv` + `buildSyncTreeTlv` +
  `buildNanSyncTlv` + `buildPrefChanTlv`, y las baja con el ioctl interno `0x97802869C8`
  (tipo 151 ?, plantilla de sync frame; Broadcom implementa
  `setAWDL_SYNC_FRAME_TEMPLATE` en `0xffffff800171da80`).
  `buildMasterIndicationTemplate` (`0xffffff8002211b42`) hace lo mismo para las UMIs (añade
  TLV 29 de BSS steering multi-peer, ProMode y AirDrop) sobre un buffer de 1200 B en `+9048`.
  `updateBroadcastMI` (`0xffffff80021dca18`) refresca el payload primario y, si la capacidad
  `SEC_PAYLOAD` está declarada, el secundario, con límite de pendientes (`+17004`/`+17008`).

## 5. Ventanas de disponibilidad y línea temporal

- `inAvailabilityWindow` (`0xffffff8002219af4`) devuelve el bit `+772`; lo limpia el ciclo de
  disable (outA línea 2076) y lo activa la ruta de eventos del driver
  (`reportDataPathEvents`, evento 41; verificado por xref de `monitorTimeline`, el escritor
  exacto dentro de esa función enorme queda sin verificar `?`).
- Periodo y longitud viven en `+62940`/`+62944` (`getAvailabilityWindowPeriod/Length`,
  `0xffffff80022355e0`/`0xffffff80022355f0`). El driver los recibe por el ioctl interno
  `0x74802869C8` (tipo 116, `setAWDL_SYNC_PARAMS`): payload `{u32 periodo +0, u32 longitud +4,
  u32 ? +8, u32 periodo-sync-frame +12}` — `setSyncFramePeriod` (`0xffffff80021ed024`) empuja
  ahí el periodo de master indication (`+62952`). Los valores por defecto no se han visto
  inicializados en el código analizado; el init escribe `0xFDE8` (65000) y `0x105B8` (67000) en
  `+66208`/`+66212` con unidad sin confirmar `?`.
- El driver reporta cada arranque de ventana: `reportDataPathEvents` (`0xffffff80021dd8f4`,
  celda 83, `+0x298`) consume los eventos 41/42/43 del dispatch y llama a
  `monitorTimeline` (`0xffffff80021e11ca`, xref `0x21dfb3d`) y `logAWStart`
  (`0xffffff80021e1b42`, xref `0x21e0286`). El payload que `monitorTimeline` espera
  (`apple80211_availability_window_start`): `{u16 AwNumber +0, MAC maestro +8, MAC topMaster +14,
  u32 FwAwStartTime +20}`; con ello comprueba que el maestro programado es el que el firmware
  está sirviendo (flag `+10304`) y detecta cambios de maestro ajenos.
- `postSyncStateChanged` (`0xffffff80021e58b4`) publica el estado hacia userspace vía
  `IO80211VirtualInterface::postSyncStateChanged` (evento 46 `AWDL_SYNC_STATE_CHANGED`); el
  estado numérico vive en `+17028` (`getSyncState`, `0xffffff800223548c`) y el log lo traduce con
  `awdlStringForState`. Los valores del estado se usan como constantes sueltas (0/1/5 en
  `shouldRunElection` y `dequeueTxPacket`); la enumeración completa queda sin verificar `?`.

## 6. Secuencia de canales

- La propia: `+42864` (u16 longitud) y `+42868` (entradas de 12 B, formato `apple80211_channel`).
  `setMyChanSeq` (`0xffffff8002235302`) valida canales 6 GHz (`is6GChannelALlowedForP2P`).
  `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`) y `recoverFromSetChanSeqFailure`
  (`0xffffff80021f3710`) completan la elección local; `processAWDLDFSChannelSequence`
  (`0xffffff800221a466`) gestiona la variante DFS.
- Bajada al driver: `setChannelSequenceUnconditional` (`0xffffff80021cb69a`) compone hasta 400 B
  y los envía con el ioctl interno `0x81802869C8` (tipo 129,
  `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`; Broadcom `0xffffff8001724436`), y luego
  `programMasterChannels` (`0xffffff80021d494a`) fija los canales sociales con
  `0x84802869C8` (tipo 132, `setAWDL_MASTER_CHANNEL`) y `0xCD802869C8` (tipo 205,
  `setAWDL_SECONDARY_MASTER_CHANNEL`), previa actualización de la lista con supresión PSF
  (`updateFWSocialChanListWithPSFSuppressionInfo`, `0xffffff800220db4e`).
- El lado receptor parsea la secuencia de cada par:
  `parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b5a08`),
  `parseAwdlChanSeqFromChanSeqExtTLV` (`0xffffff80021b5e84`),
  `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`) y
  `updateAwdlChanSeq` (`0xffffff80021ab7e4`, con `bounded_ptr<awdl_chan_seq_info2>` y trampa de
  pánico si se sale de rango), validación previa `isValidChanSeqTlv` (`0xffffff8002214b76`).

## 7. Tabla de peers

- Contenedor: tabla hash de `IO80211Peer` heredada de `IO80211PeerManager` (ivars base `+1344`,
  cabecera de lista `+56`, contador `+36`). Alta: `addAWDLPeer`
  (`0xffffff800220fe4a`, llamado por `actionFrameProcessInput` xref `0x220eceb`) →
  `IO80211AWDLPeer::withAddressAndManager` (`0xffffff80021a8dac`) →
  `initWithAddressAndManager` (`0xffffff80021a8e1e`) → `enqueuePeerInOrder`.
  Baja: `removePeer` (`0xffffff800221721c`, celda 79), `removeAllPeers` (`0xffffff800222be56`,
  celda 80), caché `addPeerToCache` (`0xffffff800222dbb2`).
- El ivar del par (QWORD `+0x20`) acumula lo recibido: electionId de 6 B `+559`, rootLFC `+571`,
  hopCount `+575`, rootMetric `+579`, selfMetric `+583`, electionId u32 `+587`, modo de maestro
  `+120`, hora del último AF `+784`, canal `+716`, presencia `+748`, campos de Sync Params
  `+756..+904`. Todo lo rellena `actionFrameReport`.
- Base de datos completa para userspace: `getAwdlPeerDatabase` (`0xffffff800221ff6c`, tipo 203,
  208480 B). Limpieza por ausencia: `cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`, evento 47).

## 8. Action frames

### 8.1 Recepción

`actionFrameInput` (`0xffffff800220e1ae`, celda 71, `+0x238`) — la interfaz es quien lo invoca con
cada action frame AWDL capturado:

- Contadores IOReport, `sendToBpfTap` de la interfaz (tap BPF de awdl0, ver `AWDL_EVENTS.md`).
- Si `wlan.splitAWDLAFThread` está activo (defecto 1), encola en el anillo propio (head `+16848`,
  tail `+16852`, talla `+16856`, flag de desbordamiento `+16868`; `growAFRing`
  `0xffffff800220dd0a` / `shrinkAFRing` `0xffffff800220dfca`) y despacha con el timer
  `+10408`: `actionFrameProcessThread` (`0xffffff80021c3ebc`). Si no, procesa en línea.

`actionFrameProcessInput` (`0xffffff800220e8b0`, 0x1174 B) valida (`isValidAF`
`0xffffff800220fa64`, OUI con `actionFrameHasCorrectOUI` `0xffffff8002210200`), crea el par si es
nuevo (`addAWDLPeer`) y delega el parseo del cuerpo en
`IO80211AWDLPeer::actionFrameReport` (`0xffffff80021af6be`, 0x37f0 B), que recorre todos los TLVs:
Sync Params (`parseAwdlSyncParamsTlv` `0xffffff80021b4ba6` — levanta el flag de reelección
`+10306` del manager), Election Params V1/V2, Chan Seq (+ext), Sync Tree, Data Path State, Pref
Chan, IEEE80211 container, versión, NAN sync, ranging, RTG, AirDrop, Pro Mode, BSS steering y
service response; `doMinimalAFProcessing` (`0xffffff80021b3ea6`) y `shouldStopAFProcessing`
(`0xffffff80021b48b8`) acotan el trabajo. Los UMIs van a `postAFToP2pd` (`0xffffff800220fc2c`)
hacia el daemon, y la gestión UMI encadena `isSafeToSendUmiNow` (`0xffffff8002210108`) y
`IO80211AWDLPeer::addPeerToUmiChain` (`0xffffff80021bcb44`).

### 8.2 Transmisión

El manager no programa TX de sincronía por su cuenta: construye plantillas (§4) y las baja por
ioctl (151 ?); el chip las repite en cada ventana según el periodo del ioctl 116. Para UMIs
puntuales, `sendUnicastMI` (`0xffffff8002213c0a`) exige estar dentro de la ventana (bit `+772`),
construye la plantilla del par y la envía con el ioctl interno `0x95802869C8` (tipo 149 ?, payload
con BSSID propio `+37204/+37208`, MAC destino, opción 6 o 14 "UMIOnDwell", plantilla y longitud en
`+10248`); `sendRealTimeUMI` (`0xffffff80022020e8`) y `setAFTXMode` (`0xffffff80021f7d06`, ioctl
tipo 208) completan la familia.

### 8.3 Datos

Modelo de tirón: el driver pide paquetes durante sus ventanas.

- `dequeueTxPacket(TxPacketRequest*)` (`0xffffff800221675e`, celda 85, `+0x2A8`): valida el
  estado, bloquea el datapath y rellena el array de `mbuf` de la petición; comprueba
  disponibilidad (`+408`), estado 5 y modo de datos (`+17032`).
- `getUnicastPackets` (`0xffffff8002216a4e`, celda 46) y `getMulticastPackets`
  (`0xffffff8002216a2e`, celda 45) por clase WME; colas en `+10424`/`+10448`, con
  `stashMulticastPacketPair` (`0xffffff8002213b16`) para duplicar multicast al segundo canal (el
  dequeue PCIe de Broadcom la usa, ver `AWDL_PROXIMITY_DATAPATH.md` §3).
- Entrada de datos: `inputPacket` (celda 70, base) y `formatInputPacket`/`formatOutputPacket`
  (celdas 41/40).

## 9. Contrato con el driver

### 9.1 Celdas de la vtable del PeerManager (`0xffffff800245a868`) que la pila de arriba usa

| Celda | Offset | Método | Quién lo dispara |
|---:|---:|---|---|
| 55 | `+0x1B8` | `interfaceStateChange` | ciclo de vida de la interfaz virtual |
| 56 | `+0x1C0` | `setAwdlCtl` (y 64 `getAwdlCtl`) | ioctl AWDL_CTL desde userspace |
| 70 | `+0x230` | `inputPacket` | ruta de datos RX |
| 71 | `+0x238` | `actionFrameInput` | cada action frame AWDL recibido |
| 45/46 | `+0x168`/`+0x170` | `getMulticastPackets`/`getUnicastPackets` | pull TX por clase WME |
| 85 | `+0x2A8` | `dequeueTxPacket` | pull TX del datapath |
| 83 | `+0x298` | `reportDataPathEvents` | eventos del driver (41/42/43/47/…, ver `AWDL_EVENTS.md` §3.2) |
| 72/73 | `+0x240`/`+0x248` | `doEnable`/`doDisable` | máquina de estados de la interfaz |
| 51/52 | `+0x198`/`+0x1A0` | `reset`/`awdlChipReset` | reset del controlador |
| 57/58 | `+0x1C8`/`+0x1D0` | `setScanningState`/`setJoiningState` | subsistema de scan |
| 61/62 | `+0x1E8`/`+0x1F0` | `setInfraTxState`/`notifyHostapState` | interfaz de infra |
| 42/43 | `+0x150`/`+0x158` | `countryCodeStateChanged`/`handleChannelSwitchAnnouncement` | país y CSA |

La ruta exacta de la pila Skywalk que invoca `dequeueTxPacket`/`getUnicastPackets` en el caso
genérico queda sin verificar `?`; en el caso Broadcom el dequeue de la cola PCIe interactúa con el
manager para los multicast (`stashMulticastPacketPair`), según `AWDL_PROXIMITY_DATAPATH.md`.

### 9.2 Ioctls apple80211 internos que el manager emite (`IO80211PeerManager::ioctl_internal`)

Codificación observada: qword de comando con token bajo `0x802869C8` (`_IOR`/`_IOWR` de grupo 'i',
número 200, longitud 40) y el tipo apple80211 real en el dword alto; una variante `0xC02869C9`
aparece en la lectura de capacidades. Tipos identificados:

| Codificación | Tipo | Comando | Payload observado |
|---|---:|---|---|
| `0x75802869C8` | 117 | `setAWDL_SYNC_ENABLED` | enable/disable |
| `0x155C02869C9` | ? | lectura `awdl_cap` (bit 0 `SEC_PAYLOAD`, 2 `CCA_STATS`, 8 `PSF_DWELL_MI`, 16 `PSF_DWELL_UMI`) | `?` |
| `0x74802869C8` | 116 | `setAWDL_SYNC_PARAMS` | `{periodo, longitud, ?, periodo-sync-frame}` |
| `0x97802869C8` | 151 ? | plantilla de sync frame | 512 B (§4) |
| `0x95802869C8` | 149 ? | envío de UMI | BSSID, destino, opción, plantilla |
| `0x81802869C8` | 129 | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 400 B |
| `0x84802869C8` | 132 | `setAWDL_MASTER_CHANNEL` | u32 canal social |
| `0xCD802869C8` | 205 | `setAWDL_SECONDARY_MASTER_CHANNEL` | u32 canal social |
| `0x86802869C8` | 134 | `setAWDL_SYNC_STATE` | 24 B: modo, master MAC, hopCount, métricas |
| `0x9B802869C8` | 155 | `setAWDL_ELECTION_PARAMS` | 24 B (§3.2) |
| `0xE5802869C8` | 229 | `setAWDL_AWDL_ADVERTISERS` | advertisers del maestro |
| `0xD0802869C8` | 208 | `setAWDL_AF_TX_MODE` | modo TX de action frames |

Los tipos 116/117/129/132/134/155/205/208/229 coinciden con la tabla de
`AWDL_APPLE80211_DISPATCH.md`; 149/151 no figuran ahí y se identifican por semántica (nombres de
log y del setter Broadcom) `?`.

### 9.3 Lo que Broadcom responde hoy (y que un kext sin firmware AWDL debe emular)

| Comando | Setter Broadcom | Qué hace el firmware detrás |
|---|---|---|
| `setAWDL_SYNC_PARAMS` 116 | `0xffffff800171ddee` | iovars de timing de AWDL (`AWDL_FIRMWARE_DUTIES.md` §awdl_sync_params) |
| `setAWDL_SYNC_ENABLED` 117 | `0xffffff800171e42a` | interruptor maestro `awdl` |
| `setAWDL_SYNC_STATE` 134 | `0xffffff800171e164` | rol master/non-electoral/slave (`WLC_E_AWDL_ROLE` 97) |
| `setAWDL_MASTER_CHANNEL` 132 | `0xffffff800171ea1a` | canal social primario |
| `setAWDL_SECONDARY_MASTER_CHANNEL` 205 | `0xffffff800171f01e` | canal social secundario |
| `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` 129 | `0xffffff8001724436` | dwell por la secuencia |
| `setAWDL_ELECTION_PARAMS` 155 | `0xffffff800171c4ca` | iovar `awdl_election_tree` |
| `setAWDL_AWDL_ADVERTISERS` 229 | `0xffffff800171c932` | advertisers en el MI |
| `setAWDL_SYNC_FRAME_TEMPLATE` ? | `0xffffff800171da80` | plantilla que el firmware repite |
| `setAWDL_AF_TX_MODE` 208 | `0xffffff80017199c4` | modo TX de action frames |

## 10. Veredicto de reutilización

Un kext de terceros cuya interfaz subclasee `IO80211VirtualInterface`/`IO80211AWDLProtocol` con rol
6 puede usar el PeerManager del kernel tal cual, sin reimplementar elección ni sincronía:

- La factoría es heredada: `createPeerManager` (ranura 355) comprueba el rol 6 y construye
  `IO80211AWDLPeerManager::withInterface`. No hace falta enlazar símbolos del PeerManager: la
  clase se registra vía su `gMetaClass`, pero `initWithInterface` no es virtual, así que la vía
  soportada es la herencia de `createPeerManager` (llamar `allocClassWithName` y montar el estado
  a mano no está soportado por ningún camino visible).
- Toda la máquina de elección, el árbol de sincronía, las plantillas MI/UMI, el parseo de TLVs, la
  tabla de peers y los 14 timers quedan del lado del kernel: el driver no calcula nada de protocolo.

Lo que el driver sí debe proveer, en orden de criticidad:

1. **Repetición de la plantilla de sync frame.** Recibir el ioctl 151 ? con la plantilla de 512 B
   y transmitirla cada `syncFramePeriod` (ioctl 116) alineada con la línea temporal compartida —
   en Broadcom lo hace el firmware; sin él, el driver necesita su propio reloj y scheduler de TX.
2. **Eventos de línea temporal.** Publicar por `reportDataPathEvents` los eventos 41/42/43
   (arranque de ventana, extensión, fin) con el formato de `apple80211_availability_window_start`
   (`{AwNumber, maestro, topMaster, FwAwStartTime}`) para que `monitorTimeline` valide el estado;
   y el 47 de presencia de peer. Sin firmware, el propio driver debe medir sus ventanas.
3. **Cambio de canal por secuencia.** Ejecutar el dwell del ioctl 129 (secuencia de canales) y
   fijar los canales sociales (132/205).
4. **RX de action frames.** Capturar los frames AWDL en los canales sociales y llamar a
   `actionFrameInput` (celda 71) con el buffer y la cabecera; sin esto no hay peers, ni elección,
   ni descubrimiento.
5. **Pull de TX de datos.** Durante las ventanas, servir `dequeueTxPacket`/`getUnicastPackets`/
   `getMulticastPackets` y transportar los mbufs (el driver decide el instante dentro de la
   ventana; el manager decide qué).
6. **Slots puros de `IO80211AWDLProtocol`.** Las 64 firmas de `AWDL_PURE_SLOTS.md` siguen siendo
   obligatorias para que la interfaz se construya; las que el PeerManager toca directamente son
   pocas (getWorkLoop del controlador, getSelfMacAddr 417, link state `+2952`, scan manager 374).
7. **Estado de sync.** Aceptar el ioctl 134 (modo master/slave y maestro elegido) y reflejarlo en
   el radio: es el equivalente del `WLC_E_AWDL_ROLE` de Broadcom.
8. **Capacidades honestas.** La lectura `awdl_cap` decide si el manager construye payload
   secundario y PSF dwell; declarar sin soporte reduce el trabajo del driver.

Riesgos: los offsets de ivars citados pertenecen a la build 25.6.0 y se mueven sin aviso entre
versiones de IO80211Family (el kext no puede depender de ellos salvo para depuración); la
codificación de los ioctls internos y su lista completa son observadas, no documentadas por Apple;
y el manager asume un controlador con workloop (`getController` → `getWorkLoop`) y el bloqueo de
datapath del base `IO80211PeerManager`, que la subclase hereda si el init base se ejecuta.
Además, subclasear las clases AWDL de IO80211Family exige enlazar contra sus símbolos (vtable y `gMetaClass`) sin KDK; AirLayer ya lo hace con `IO80211InfraProtocol`, pero el mismo enlace con `IO80211VirtualInterface`/`IO80211AWDLProtocol` no está comprobado `?`.
