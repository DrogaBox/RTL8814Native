# Qué delega AppleBCMWLANProximityInterface (y AppleBCMWLANCore) en el firmware Broadcom para AWDL

Inventario de todo lo que el kext Broadcom manda al firmware (iovars, comandos y suscripción de
eventos) cuando una interfaz AWDL (role 6) está activa, con el payload exacto de cada iovar, la
request apple80211 o el evento que lo dispara, y la función que se espera del firmware. Cierra con
la clasificación de obligaciones en (a) configuración pura, (b) timing periódico y (c) trabajo por
frame, como base para decidir qué debe reimplementar un kext propio sin firmware AWDL.

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), macOS 26 / kernel
25.6.0, filesets `com.apple.driver.AppleBCMWLANCoreMac` (core, interfaces y Commander) y
`com.apple.driver.AppleBCMWLANBusInterfacePCIeMac` (bus). Copia IDB headless del kernelcache en
`Build/ida_awdl/task_awdl_fw_duties/` (deriva de `Build/ida_awdl/BootKernelExtensions.i64`, la
misma base que las tareas `task_dispatch`/`task_datapath`/`task_awdl_structs`/`task_awdl_events`).
Direcciones = VA del KC. Los payloads se extrajeron del desensamblado de los sitios de
`sendVirtualIOVarSet`/`runVirtualIOVarSet`/`runVirtualIOVarGet`/`sendIOVarSetCommand` (el
pseudocódigo de Hex-Rays oculta los argumentos de nombre y payload). Lo no verificado va con `?`.
Complementos: [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) (datapath Skywalk),
[`AWDL_EVENTS.md`](AWDL_EVENTS.md) (eventos hacia userspace y payloads),
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) (tipo de request → slot).

## 1. Resumen corto

1. El transporte único hacia el firmware es `AppleBCMWLANCommander` (instancia del core en
   `core+0x1540` = +5440, referenciada por la interfaz en `ivars+968`): tres entradas —
   `runVirtualIOVarSet`/`runVirtualIOVarGet` síncronas (`0xffffff8001760c58` /
   `0xffffff8001760d6c`), `sendVirtualIOVarSet` asíncrona con completion
   (`0xffffff800176103a`) y `runIOVarSet`/`runIOVarGet`/`sendIOVarSet` sobre la interfaz
   primaria; todas convergen en `issueCommand` (`0xffffff800175ecf2`), que mantiene una cola de
   comandos con temporizador propio y un tamaño máximo (`getMaxCmdTxPayload`).
2. En el rol 6 el kext delega en el firmware: la creación de la instancia AWDL en el chip
   (`interface_create`/`awdl_if`), el motor de sincronía y sus parámetros (`awdl`,
   `awdl_sync_params`, `awdl_opmode`), la elección de maestro (`awdl_election_tree`),
   el salto de canal (`awdl_chan_seq`), la ventana de disponibilidad y su extensión
   (`awdl_extcounts`), la transmisión de sync frames (`awdl_payload`, `awdl_psf_dwell`,
   `awdl_af_hdr`), la planificación del TX de action frames (`awdl_peer_op`,
   `awdl_advertisers`, `awdl_aftxmode`, `awdl_oob_af`, `awdl_oob_af_auto`, `awdl_afs_pload`),
   el filtrado RX (`awdl_af_rssi`), el proxy DFS (`awdl_dfsp_cfg`, `awdl_dfsp_ucsa`) y las
   estadísticas/estado (`awdl_stats`, `cca_get_stats`, `awdl_maxpeers`, `awdl_presencemode`,
   `awdl_osoc_chan`, `awdl_min_rate`, `awdl_phycal_period`, `awdl_config`).
3. Los eventos del firmware llegan como `wl_event_msg_t` de 48 bytes (framing BDC + bcmeth);
   `AppleBCMWLANCore::handleEventPacket` (`0xffffff800160cda6`) los enruta a
   `AppleBCMWLANProximityInterface::handleEvent` (`0xffffff8001715ece`), que consume los ids
   0, 16, 26, 54, 59, 60, 69, 75, 80, 96 (`WLC_E_AWDL_AW`), 97 (`WLC_E_AWDL_ROLE`),
   98 (`WLC_E_AWDL_EVENT` y su sub-despacho), 109, 150 y 152. Los nombres canónicos están en
   `kEventBitMapping` (`0xffffff80017ed070`, 208 entradas {u32 id, char* nombre}).
4. No existen en este binario los strings `awdl_aws`, `awdl_af_tx`, `awdl_mon` ni
   `awdl_election` a secas: sus funciones están cubiertas por `awdl_sync_params` (el programa
   de availability windows viaja dentro de los sync params), `awdl_oob_af`/`awdl_oob_af_auto`
   (TX de action frames), el valor de `awdl_opmode` y `awdl_election_tree` (elección).
5. Para un port RTL8814 sin firmware AWDL: la parte (a) es almacenable tal cual, la parte (b)
   exige un temporizador software con los periodos del §6.2 (los valores reales los programa
   userspace vía `AWDL_SYNC_PARAMS`; los defaults del kext están en `init`, `0xffffff8001718db0`)
   y una máquina de elección (o reutilizar `IO80211AWDLPeerManager` de IO80211Family, que es
   donde viven election id/métrica en la ruta moderna), y la parte (c) ya está descrita en
   `AWDL_PROXIMITY_DATAPATH.md` §9/§10 salvo las piezas que aquí se atribuyen al firmware.

## 2. Transporte: AppleBCMWLANCommander

- El formato iovar es el clásico Broadcom: nombre + NUL + datos. `sendIOVarSetCommand`
  (`AppleBCMWLANProximityInterface::sendIOVarSetCommand`, `0xffffff800171d61c`) concatena dos
  `iovec` en un buffer (`IOMallocZeroData_external`), comprueba el total contra
  `AppleBCMWLANCommander::getMaxCmdTxPayload` y encola con `sendVirtualIOVarSet`; el contador de
  comandos en vuelo vive en `ivars+48` con máximo 0x10 (16) — "Too many async iovars in flight
  (%d), rejecting awdl_oob_af" (`0xffffff8001726818`).
- `runVirtualIOVarSet/Get(commander, ifid, nombre, tx, rx, pref)` (`0xffffff8001760c58` /
  `0xffffff8001760d6c`): validan `ifid < 12` y nombre no nulo y llaman
  `AppleBCMWLANCommander::issueCommand(this, pref)`; la validación superior loguea
  `ERROR: Virtual IOVar "%s" on invalid interface %d` (líneas 1168/1197 de
  `AppleBCMWLANCommander.cpp`). "Virtual" = el comando lleva el id de interfaz virtual y el
  firmware lo dirige a la bsscfg AWDL.
- `sendVirtualIOVarSet(commander, ifid, nombre, tx, rxExpected, completion, pref)`
  (`0xffffff800176103a`): variante asíncrona; la completion es un callback de la interfaz que
  llega por el workqueue del `AppleBCMWLANTaskmaster`. En el rol 6 las completions usadas son
  `handleProxIovarAsyncCallBack` (`0xffffff8001718cc6`, genérica),
  `handleFlowControlledProxIovarAsyncCallBack` (`0xffffff800171d9ba`),
  `handleElectionTreeIovarAsyncCallBack` (`0xffffff800171c6e0`),
  `handleOobAfIovarAsyncCallBack` (`0xffffff800171d4da`),
  `handleSlotBssIovarAsyncCallBack` (`0xffffff800171d578`) y
  `handleCacheControlAsyncCallBack` (`0xffffff800172056c`).
- `issueCommand` (`0xffffff800175ecf2`, 0x1AFE bytes) implementa la cola: "no free commands
  left!", "Cannot send a command async/synch", "Commander Check Queue Timer",
  `enqueueCommand`/`serviceCommand`/`dequeueCommand`, volcado "CommandID info: interface[...]".
  El envío final al hardware lo hace el bus PCIe (`AppleBCMWLANBusInterfacePCIeMac`) mediante
  sus anillos de host→firmware (`?` detalle interno del bus no decompilado en esta pasada).
- Suscripción de eventos: `AppleBCMWLANCore::writeEventBitField` (`0xffffff800167e054`) envía el
  iovar **`event_msgs`** (16 bytes de máscara desde `core+1096`, con cabecera de valor
  `00 AA AA AA 07 00 00 00`) o **`event_msgs_ext`** (~67 bytes si el feature flag 34 del core
  está activo; máscara desde `core+1096..1124`) con `issueCommand`. Los bits los marca
  `AppleBCMWLANCore::addEventBit`: `doSetSyncState(true)` añade 0x60/0x61/0x62 (96/97/98) y
  `doSetSyncState(false)` añade 0x4B (75) y reescribe la máscara (`0xffffff8001719e38`,
  sitios `0xffffff800171a051` (addEventBit) y `0xffffff800171a167` (writeEventBitField)).

## 3. Inventario de iovars del rol 6

Convención: "SET/GET" = iovar enviado por la función citada (dirección de la función y del sitio
de llamada); "disparador" = request apple80211 (número de tipo según
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §5) o evento/interno que la causa;
"payload" = bytes que recibe el firmware (los offsets se leyeron del desensamblado del sitio de
`send*`); "función esperada del firmware" = interpretación del nombre y de los campos, marcada
con `?` donde es inferencia.

### 3.1 Ciclo de vida de la instancia AWDL en el chip

- **`awdl_if`** (string `0xffffff80017cce93`): la implementa `setAWDL_BSSID`
  (`0xffffff800172181a`), que es también el "createChipInterface" (así se llama en sus logs,
  referencias al string `createChipInterface` entre `0xffffff8001721a96` y `0xffffff8001722a8a`). Firmware < 5 (comprobación
  `core→+780 < 5`): `AppleBCMWLANCommander::runIOVarSet` (sitio `0xffffff8001721bda`) con
  **20 bytes** `{u32 rol/ver = ivars+928 (init 3), u32 op (1 = crear), MAC BSSID 6 B
  (ivars+608/612), MAC propia 6 B (getSelfMacAddr, slot +0xCF8)}` (hexdump
  `AppleBCMWLAN: sending awdl_if =`); tras el envío se espera el evento interno
  "Interface Created" con timeout 3000 ms (`waitForDriverEvent(0, 3000)`,
  `0xffffff8001715c62`) y se verifica la MAC con un GET de `cur_etheraddr`
  (`0xffffff8001722303`). Borrado (`deleteChipInterface`, `0xffffff8001722ae2`): mismo iovar
  con op = 0 y espera de 1000 ms (sitio `0xffffff8001722d45`).
- **`interface_create`** (fw ≥ 5): `runIOVarGet` sobre la interfaz primaria con **request de
  28 bytes** `{u32 0x1BF083, u32 0x0020001C, u32 26 (0x1A), MAC propia 6 B, MAC BSSID 6 B}` y
  **respuesta** `{bssCfgIdx, ifIdx, ifName}` ("Return bssCfgIdx %d, ifIdx %d,
  ifName %s"; sitio exacto de la llamada `?`, dentro de `setAWDL_BSSID`); el índice devuelto
  es la bsscfg que el firmware asigna a AWDL y la que después direccionan los iovars virtuales.
  Disparador: `APPLE80211_IOC_AWDL_BSSID` (tipo 125) — `setAWDL_BSSID` es el slot +0x10D0.
- **`interface_remove`** (fw ≥ 5): `runVirtualIOVarSet` en `deleteChipInterface`
  (`0xffffff8001722ae2`); payload `?` (esperable el bssCfgIdx devuelto por
  `interface_create`).
- **`cur_etheraddr`** (string `0xffffff8001796f83`): SET de 6 bytes desde `setMacAddress`
  (`0xffffff8001720354`, sitio `0xffffff8001720471`) cuando el chip interface ya existe
  (flag `ivars+128 & 1`); GET de verificación en la creación. La MAC del rol 6 se cambia
  **antes** de abrir la sincronía: `setMacAddress` lo rechaza con `0xE00002D5` si
  `ivars+672 != 0` (sync activa) o hay creación en curso (`ivars+1112 & 1`).
- **`2g_mrate`/`bg_mrate` y `5g_mrate`/`a_mrate`** (`setBssRates` inline en `setAWDL_BSSID`,
  `0xffffff8001721cdc`–`0xffffff8001721cf5`): u32 en unidades de 0.5 Mbps = 2 × `ivars+704` (init 12 → 12 Mbps);
  el par elegido depende de la versión de firmware del chip (`core+1020 < 2` → `bg_mrate`/
  `a_mrate`). El segundo envío (`5g_mrate`) exige el feature flag 28 del core. Función
  esperada: tasa base de los frames de gestión AWDL (sync frames) `?`.

### 3.2 Motor de sincronía

- **`awdl`** (string `0xffffff8001797376`, el literal "awdl"): el interruptor maestro.
  SET u32 (4 bytes, 0/1) desde `doSetSyncState(bool)` (`0xffffff8001719e38`, sitio
  `0xffffff800171a24c`, async con completion genérica); GET u32 desde
  `getAWDL_SYNC_ENABLED` (`0xffffff800171b092`, sitio `0xffffff800171b118`, rx 4 B).
  Disparador: `APPLE80211_IOC_AWDL_SYNC_ENABLED` (tipo 117) → `setAWDL_SYNC_ENABLED`
  (`0xffffff800171e42a`) → `setSYNC_ENABLED_internal` (`0xffffff800171e4c2`) →
  `doSetSyncState`. Al habilitar, además de arrancar el motor AWDL del firmware, el kext
  suscribe los eventos 96/97/98, marca concurrencia (`setConcurrencyState(2, on)`), fuerza
  `setAfTxMode` y ajusta ahorro de energía (`AppleBCMWLANPowerManager::configurePowerSaveForAWDL`,
  `0xffffff80015eec56`). Al deshabilitar re-escribe la máscara de eventos y limpia estado.
- **`awdl_sync_params`** (string `0xffffff80017cadfe`): el programa temporal de la sincronía.
  SET de **36 bytes** desde `setSyncParams` (`0xffffff800171dbb2`, sitio `0xffffff800171dd26`,
  async, completion `handleProxIovarAsyncCallBack`): `+1..+2` u16 = `0x0024` (36; `?` semántica:
  longitud lógica del registro), `+6` u8 = `fLifeTime` (`ivars+688`, init 6), `+8` u16 =
  `aw_period` (`ivars+728`), `+0xA` u16 = `adv_period`/periodo de action frames (`ivars+740`,
  init 100), `+0xE` u16 = `aw_ext_len` (`ivars+732`), `+0x10` u16 = `aw_len` (`ivars+724`);
  resto a cero. Los ivars los carga `setAWDL_SYNC_PARAMS` (`0xffffff800171ddee`: request+4..20 →
  ivars+724/728/732/740/668; log `aw_period %lu aw_len %lu aw_ext_len %lu adv_period %lu as non
  election master %lu`). Disparadores: tipo 116 (`AWDL_SYNC_PARAMS`, len 24) y tipo 132
  (`setAWDL_MASTER_CHANNEL`, `0xffffff800171ea1a`, que reenvía los params tras cambiar de
  canal maestro vía `updateSyncParams`). GET de 36 bytes (tx de petición a cero + rx): en
  `getAWDL_SYNC_PARAMS` (`0xffffff800171b1b8`, sitio `0xffffff800171b2f5`),
  `getAWDL_MASTER_CHANNEL` (`0xffffff800171f1e6`, reinterpreta los mismos campos) y
  `dumpSyncParams` (`0xffffff800171ac60`, para el dump de debug). El rx se vuelca a los mismos
  ivars y al struct `apple80211_awdl_sync_params`. Función esperada: el firmware programa el
  periodo de availability windows, su longitud, la longitud de la porción extendida y la
  cadencia de los action frames de sincronía `?`.
- **`awdl_opmode`** (string `0xffffff80017caec7`): el rol de sincronía local. SET de **28
  bytes** desde `setAWDL_SYNC_STATE` (`0xffffff800171e164`, sitio `0xffffff800171e2ce`, async):
  `+0` u8 = modo (0 = slave, 1 = non-electoral master, 2 = master; mapeo desde
  `request+4` ∈ {1→2, 3→1, 2→0} y log `set awdl opmode %u %s ne_bcast…` con los literales
  MASTER/NE_MASTER/SLAVE de `0xffffff8001770a80..`), `+1..+2` u16 = `fMasterMode`
  (`ivars+668`; el flag "non election master" del tipo 116), `+3` u8 = `ne_bcast`
  (`request+0x10`), `+4` u32 y `+8` u16 desde `request+8`/`+0xC` (timestamps de la
  request `?`), `+0xA` u8 adicional de `request+0x14` `?`. GET (rx 28 B) en
  `getAWDL_SYNC_STATE` (`0xffffff800171b528`). Disparador: tipo 134
  (`AWDL_SYNC_STATE`, len 24). Función esperada: fija el papel del chip en la sincronía; la
  confirmación vuelve por `WLC_E_AWDL_ROLE` (97).
- **`awdl_election_tree`** (string `0xffffff80017cb196`): los parámetros de elección. Cuatro
  setters escriben ivars (`+560..+585`) y lanzan el iovar por `sendVirtualIOVarSet` async:
  `setAWDL_ELECTION_ID` (`0xffffff800171cace`, sitio `0xffffff800171cbef`: id u16 de
  `request+4` → `ivars+561`), `setAWDL_ELECTION_PARAMS` (`0xffffff800171c4ca`, sitio
  `0xffffff800171c596`: `request+16`→`+560` u8, `+18`→`+561` u16, `+20`→`+585` u8,
  `+22`→`+575` u32, `+26`→`+579` u16, `+28`→`+581` u32, `+32`→`+563` u32 métrica),
  `setAWDL_ELECTION_METRIC` (`0xffffff800171cf76`, sitio `0xffffff800171d097`: u32 →
  `ivars+563`), `setAWDL_ELECTION_RSSI_THRESHOLDS` (`0xffffff800171d16a`, sitio
  `0xffffff800171d210`: bytes de `request+4/8/0xC/0x10` → `ivars+571/570/567/569`; init de
  esos umbrales en `0xffffff8001718db0`: `ivars+567..571` = `0xC3 E9 C3 CE BA`,
  es decir −61/−23/−61/−50/−70 dBm `?`). GETs: `getElectionParams` (`0xffffff800171c2fa`,
  struct `awdl_election_tree_info`), `getAWDL_ELECTION_RSSI_THRESHOLDS` (`0xffffff800171d2e4`),
  `getAWDL_ELECTION_METRIC` (`0xffffff800171cd70`), `getAWDL_ELECTION_ID` (`0xffffff800171c456`,
  que relee los ivars). La completion `handleElectionTreeIovarAsyncCallBack`
  (`0xffffff800171c6e0`) reenvía el iovar si el firmware devolvió error. Disparadores: tipos
  122/155/126/135. Función esperada: el firmware ejecuta la elección de maestro AWDL
  (preferencia, métrica, umbrales RSSI) y notifica el resultado por `WLC_E_AWDL_ROLE`.
  En la ruta moderna estos mismos valores los gestiona `IO80211AWDLPeerManager` del kernel
  (`setAwdlElectionId/Metric/Params`, ver
  [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §5).
- **`awdl_extcounts`** (string `0xffffff80017cb000`): SET de **4 bytes** desde
  `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` (`0xffffff800171bfdc`, sitio
  `0xffffff800171c078`): `+0` = `request+0x10`, `+1` = `request+4`, `+2` = `request+8`,
  `+3` = `request+0xC` (u8 cada uno; log `min %u maxuni %u maxmu…` = mínimo, máximos de
  extensión unicast/multicast de la AW `?`). GET en `0xffffff800171be78`. Disparador: tipo 118
  (`AWDL_EXTENSION_STATE_MACHINE_PARAMETERS`, len 20). Función esperada: cuántas extensiones de
  availability window admite el firmware por AW.
- **`awdl_chan_seq`** (string `0xffffff80017cd36f`): la secuencia de salto de canal. SET desde
  `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` (`0xffffff8001724436`): valida
  `request[5]` (longitud) y `request[6] < 2` (encoding); si `request[4] & 0x30` usa
  `buildChanSequenceCommand` (`0xffffff8001724840`, petición SDB) y si no
  `buildAWDLChanSequence` (`0xffffff80017258bc`) que produce ≤ 160 bytes en un buffer temporal,
  hexdump `awdl_chan_seq:` y `sendVirtualIOVarSet` async. La request completa (400 bytes,
  `apple80211_awdl_sync_channel_sequence`) se cachea en `ivars+132` (memcpy de 0x190). GET en
  `0xffffff800172421a` (rx). Disparador: tipo 129 (len 400). Función esperada: el firmware salta
  de canal por slot según la secuencia (channel sequence del protocolo AWDL).
- **`awdl_osoc_chan`** (string `0xffffff80017cbbbe`): SET u32 desde
  `setAWDL_SECONDARY_MASTER_CHANNEL` (`0xffffff800171f01e`, sitio `0xffffff800171f0a1`; valor
  `request+4` → `ivars+692`; log `channel %lu secondary %lu` con `ivars+688/+692`); GET en
  `0xffffff800171ec10`. Disparador: tipo 205. Función esperada: canal del maestro secundario
  (fallback de sincronía) `?`.
- **`awdl_phycal_period`** (string `0xffffff80017cdb96`): SET de 4 bytes (sitio
  `0xffffff8001728c0f`) / GET u32 en `setAWDL_TXCAL_PERIOD` (`0xffffff8001728b8c`) /
  `getAWDL_TXCAL_PERIOD` (`0xffffff8001728cae`). Disparador: tipo 226. Función esperada:
  periodo de calibración PHY fuera de la AW `?`.
- **`awdl_presencemode`** (string `0xffffff80017ca096`): SET u32 desde
  `setAWDL_PRESENCE_MODE` (`0xffffff80017231d8`, sitio `0xffffff80017232b3`; valor `request+4`,
  también a `ivars+664`; puertas `ivars+840/+844`); GET en `0xffffff800171bd12`. Además lo
  reenvía `handleEvent` al recibir el evento de firmware id 0 (sitio `0xffffff8001716815`).
  Disparador: tipo 136 y evento fw 0. Función esperada: presence mode del propio dispositivo
  en los sync frames `?`.

### 3.3 TX de management/action frames AWDL

- **`awdl_af_hdr`** (string `0xffffff80017ccb7c`): SET de **10 bytes** en la creación
  (`setAWDL_BSSID`, sitio `0xffffff8001721e68`): `FF FF FF FF FF FF 7F 00 17 F2` — destino
  broadcast + byte `0x7F` `?` + OUI Apple `00:17:F2`. Función esperada: cabecera (SNAP/da) que
  el firmware antepone a todo action frame AWDL que transmite.
- **`awdl_af_rssi`** (string `0xffffff80017ccc2e`): SET s32 = −60 en la creación (sitio
  `0xffffff8001721f82`). Función esperada: umbral RSSI para aceptar action frames RX `?`.
- **`awdl_peer_op`** (string `0xffffff80017cd2dd`): la caché de peers del firmware. SET desde
  `setPEER_CACHE_CONTROL` (`0xffffff80017235ce`, sitio `0xffffff8001723d6e`, async, completion
  `handleCacheControlAsyncCallBack`): descriptor de longitud variable — `+1` u8 opcode
  (0 = add/update, 1 = delete; el opcode de la request en `+4`: 0/6 → 0, 2 → 1),
  `+2` u32 índice/parámetros (`request+8`), `+6` u16 (`request+12`), `+8` u8 flags
  (bit0 HT `?`, bit2 VHT `?`; calculados de `request+44/+60` y del estado del core en
  `core+14372`), bloque HT de 32 bytes en `+9` si `request[16..17] == {0x2D, 0x1A}`,
  VHT IE de 16 bytes en `+37` si `core+1024 ≥ 128 && request[45..47] == {≠0, 0xBF, 0x0C}`,
  HE IE fija de 24 bytes en `+51` si HE y NSS = 1 (constantes
  `0x012316FF`/`0x1F000D000E200400`/`0xFFFEFFFE000C000C` escritas en +0x33/+0x3B/+0x43, `setPEER_CACHE_CONTROL`, `0xffffff8001723887`–`0xffffff80017238a9`),
  y para add con `request[70] == 3` un bloque final asociado a la secuencia de canal
  (`request[69]`, constantes `0x08F20800`…, sitio `?`) `?` mapa fino.
  GET (volcado) en `dumpPeerCache` (`0xffffff8001721038`, sitio `0xffffff80017210d0`).
  Disparador: tipo 143 (`PEER_CACHE_CONTROL`) y la gestión interna de peers. Función esperada:
  el firmware mantiene por peer la disponibilidad (chan seq, capacidades HT/VHT/HE) necesaria
  para planificar el TX de action frames y datos dentro de la AW común.
- **`awdl_advertisers`** (string `0xffffff80017cb256`): SET desde `setAWDL_AWDL_ADVERTISERS`
  (`0xffffff800171c932`, `awdl_peer_advert_add` de 44 bytes, tipo 229); GET en
  `dumpAwdlPeers` (`0xffffff80017280c4`). Función esperada: tabla de advertisers que el
  firmware usa para la identidad en los AF `?`.
- **`awdl_aftxmode`** (string `0xffffff80017ca3e6`): SET u8 desde `setAWDL_AF_TX_MODE`
  (`0xffffff80017199c4`, sitio `0xffffff8001719a38`) y desde `setAfTxMode` (`0xffffff800171a75a`,
  sitio `0xffffff800171a822`, llamada por `doSetSyncState` tras cada enable); GET en
  `0xffffff8001719b62`. Disparador: tipo 208. Función esperada: modo de offload del TX de AF
  (normal/limitado) `?`.
- **`awdl_oob_af`** (string `0xffffff80017cb6a9`): TX programado de un action frame unicast.
  SET desde `setAWDL_OOB_REQUEST` (`0xffffff8001726474`; el helper `sendUnicastAFIOVar` está
  inline): exige `fSyncEnabled != 0` (`ivars+672`), comprueba `getMaxCmdTxPayload`, construye
  **cabecera de 38 bytes** `{u32 request+22, u16 request+26, u32 request+4, u16 request+8,
  16 bytes a cero, u16 request+20, u16 len(request+40) en +36}` + el frame del usuario
  (`request+48`, `request+40` bytes), lo concatena en un buffer y encola (máx. 16 en vuelo,
  contador `ivars+48`); completion `handleOobAfIovarAsyncCallBack` → publica el evento
  userspace 69 (`AWDL_UNICAST_AF_STATUS`). Disparador: tipo 149 (`AWDL_OOB_REQUEST`).
  Función esperada: el firmware transmite el frame dentro de la próxima AW dirigida al peer `?`.
- **`awdl_oob_af_auto`** (string `0xffffff80017cd6cd`): SET vía `sendIOVarSetCommand` desde
  `setAWDL_OOB_AUTO_REQUEST` (`0xffffff8001727c0e`, sitio `0xffffff8001727cf2`); payload =
  cabecera similar + frame (`?` mapa fino). Disparador: tipo 225. Función esperada: repetición
  automática del frame por el firmware en cada AW (petición permanente en vez de una
  transmisión) `?`.
- **`awdl_afs_pload`** (string `0xffffff80017cd747`): SET vía `sendIOVarSetCommand` desde
  `setAWDL_OOB_SEC_PAYLOAD_REQUEST` (`0xffffff8001727d56`, sitio `0xffffff8001727e16`).
  Disparador: tipo 342. Función esperada: payload secundario adjunto a los AF (capabilities
  secundarias) `?`.
- **`awdl_payload`** (string `0xffffff80017cb7c3`): la plantilla del sync frame. SET vía
  `sendIOVarSetCommand` desde `setAWDL_SYNC_FRAME_TEMPLATE` (`0xffffff800171da80`, sitio
  `0xffffff800171db89`): iovecs `{u32 len (request+4), u16 0xAAAA}` + plantilla de
  `request+8` (`?` la marca 0xAAAA). Disparador: tipo 151. Función esperada: el firmware
  transmite ese sync frame (PSF) periódicamente dentro del programa de `awdl_sync_params`.
- **`awdl_psf_dwell`** (string `0xffffff80017cb96f`): SET de **8 bytes** desde `setPSF_DWELL`
  (`0xffffff800171dfc0`, sitio `0xffffff800171e05d`): `{u32 0, u16 request+4, u16 request+8}`.
  Disparador: tipo 418. Función esperada: parámetros de permanencia/dwell del PSF `?`.
- **`awdl_min_rate`** (string `0xffffff80017cdb44`): SET u8 desde `setAWDL_MIN_RATE`
  (`0xffffff8001728a48`). Disparador: tipo 213. Función esperada: tasa mínima de transmisión
  de los frames AWDL `?`.
- **`slot_bss`**: SET desde `setP2P_COEX_PROTECTION` (`0xffffff800172a968`): buffer de 256
  bytes con cabecera `{u32 0x01018000 `?`, u16 3, u32 1, u8 2}` y dos bloques TLV
  (longitud `request+4` + blob `request+5`, longitud `request+74` + blob `request+75`,
  longitudes totales u16 al final). Función esperada: protección/coexistencia BT durante los
  slots AWDL `?`.
- **`wsec_key`** y el ioctl **`WLC_SET_KEY`**: `setAWDL_ENCRYPTION_KEYS` (`0xffffff800171f480`)
  convierte la clave con `mapAppleKeyToBcomKey` y la instala con `programKeySync`/
  `programKeyAsync` (iovar `wsec_key`; el async usa `AppleBCMWLANJoinAdapter::programKeyAsync`
  `0xffffff80015f850c` / sync `0xffffff80015f8640`; fallo = `WLC_SET_KEY() failed`,
  sitio `0xffffff800171f717`). Disparador: tipo 219 (`AWDL_ENCRYPTION_KEYS`, len 148).
  Función esperada: cifrado del datapath AWDL (WPA2 con clave de grupo/grupal por peer) `?`.
- **`wsec`**: SET/GET u32 en `setAWDL_ENCRYPTION_TYPE` (`0xffffff800171fa46`) /
  `getAWDL_ENCRYPTION_TYPE` (`0xffffff800171f900`). Disparador: tipo 220. Función esperada:
  familia de cifrado activa `?`.

### 3.4 Configuración miscelánea y estado

- **`awdl_config`** (string `0xffffff80017cca86`): SET u32 desde `setAWDL_CONFIG`
  (`0xffffff80017215e2`, dos sitios; log `send awdl_config (0x%x)`); el valor por defecto en la
  creación es 0x73 (sitio `0xffffff8001721ef5`). Disparador: tipo 432. Función esperada:
  bitmask de opciones del motor AWDL (el bit 0x73 habilita las funciones básicas) `?`.
- **`awdl_stats`** (string `0xffffff80017caf10`): GET en `getAWDL_DEV_STATS`
  (`0xffffff800171b698`, buffer según versión de fw `?`); los campos se publican como
  `apple80211_awdl_dev_stats` (tipo 247, len 124). El volcado de debug
  `dumpAwdlStats` (`0xffffff8001720de8`) nombra los contadores del firmware: afrx, aftx,
  datatx, datarx, txdrop, rxdrop, monrx, lostmaster, misalign, aws, aw_dur, debug, txsupr,
  afrxdrop, awdrop, noawchansw, rx80211, peeropdrop, psfchanswtchskip, psfstateupdskip —
  i.e., el firmware contabiliza sus aciertos y fallos de scheduling `?`.
- **`awdl_maxpeers`** (string `0xffffff80017cd0bd`): GET en `getPEER_CACHE_MAXIMUM_SIZE`
  (`0xffffff80017233b6`). Disparador: tipo 130. Función esperada: tamaño máximo de la caché
  de peers del firmware `?`.
- **`cca_get_stats`**: GET en `getAWDL_CCA` (`0xffffff800171ba8e`); devuelve las estadísticas
  de ocupación del canal por canal AWDL (struct `apple80211_awdl_cca`, tipo 338, len 392) `?`.
- **`awdl_dfsp_cfg`** (string `0xffffff80017cdc35`): SET desde `setAWDL_DFSP_CONFIG`
  (`0xffffff8001728dd0`, sitio `0xffffff8001728eb5`; aplica `request+12 |= 0xD000` cuando la
  sync está activa); GET en `0xffffff8001728fba`. Disparador: tipo 262. Función esperada:
  proxy DFS (BCSA/PCSA): el firmware gestiona los canales radar de la secuencia `?`.
- **`awdl_dfsp_ucsa`** (string `0xffffff80017cdccd`): SET desde `setAWDL_DFSP_UCSA_CONFIG`
  (`0xffffff800172919c`, sitio `0xffffff800172924b`). Disparador: tipo 263. Variante UCSA del
  proxy DFS `?`.
- **`hp2p`** (string `0xffffff80017a0903`; LLW): GET en `getLLW_PARAMS` (`0xffffff800172753e`,
  sitio `0xffffff8001727712`) y SET con subcomandos en `setLLW_PARAMS` (`0xffffff8001726ac8`,
  completion `setLLW_PARAMSCallback` `0xffffff80017269d4`); ambos rechazan si el firmware no
  soporta LLW (`FW does not support llw`). Disparador: tipo 344. Función esperada: link
  low-latency acompañante de AWDL `?`.
- **`txminrate`**: GET en `getTX_RATE` (`0xffffff8001720ade`); SET de 12 bytes
  `{u32 0x80000, u32 request+4, u16 request+8, u16 request+12}` en `setTX_RATE`
  (`0xffffff8001720c20`). Disparador: tipos `getTX_RATE` (sin handler moderno `?`) y 411.
- **`btc_flags`** y **`wlan.llw.awdlbssid`**: en `setAWDL_PEER_TRAFFIC_REGISTRATION`
  (`0xffffff800171fcaa`): según el servicio registrado (`request+16`, longitud `request+12`:
  `CARPLAY_UUID`, `airplay`, `sidecar`, `ensemble`, `rtmode`, `wifid-assisted-discovery`,
  `airplay-connectivity-check`, `MPRemoteCamera`, `RemoteCamera`, `RemoteScreen`,
  `TVRemoteCamera`) conmuta `btc_flags` (async) y, para fw ≤ 4, un comando con
  `{9, activo}` por `issueCommand` `?`; refleja el estado en `ivars+100/102/103` y repite la
  petición a la interfaz compañera low-latency (slot +0xE78 del companion, `ivars+120`).
  Disparador: tipo 164 (`AWDL_PEER_TRAFFIC_REGISTRATION`, len 184). Función esperada: el
  firmware prioriza/suspende tráfico según el tipo de servicio AWDL activo `?`.
- **Lado core**: `AppleBCMWLANCore` envía por su cuenta `awdl_ranging_config`
  (string `0xffffff80017a881b`; `resetRangingState`, `0xffffff800168b854`, `runIOVarSet`
  primaria) y `awdl_ftm_ranging_config` (string `0xffffff800179f0da`;
  `setRANGING_START_V3`, `0xffffff80016866e8`, `sendVirtualIOVarSet` async con completion).
  Disparadores: requests de ranging (tipos 241/242) y reinicio de estado `?`.
- **Boot-args leídos por la interfaz** (`init`, `0xffffff8001718db0`): `awdl_log_flags_config`
  (sitio `0xffffff80017190d6`; si ≥ 2, `ivars+720 = valor−1`), `awdl_max_peers`
  (`ivars+908`, default −1), `awdl_low_latency_default_mapping` (`ivars+912`, default 1),
  `awdl_low_latency_tx_requeue` (`ivars+916`, default 0). Strings en
  `0xffffff80017ca2bd`..`0xffffff80017ca303`. Son configuración local del kext, no iovars.

## 4. Eventos de firmware consumidos por el rol 6

### 4.1 Framing y enrutado

- Formato (parseo en `AppleBCMWLANCore::handleEventPacket`, `0xffffff800160cda6`): cabecera BDC
  (su tamaño es 4 × byte@+3), cabecera bcmeth (OUI 3 bytes comparados en `+23`
  contra `0xffffff800176ecbc` y subtype u16 en `+26` con `(v & 0xFBFF) == 0x100`; `0x500`
  (1280) deriva a `handleDongleEventPacket`), y a continuación un **`wl_event_msg_t` de 48
  bytes** (variante Apple): `+0` u16 len, `+2` u16 bsscfgidx, `+4` u32 `event_type` (LE),
  `+8` status, `+12` reason, `+16` auth_type, `+20` u32 datalen (BE→LE), `+24` MAC origen,
  `+30` ifname[16], `+46` u8 **ifidx**, `+47` u8 bssidx; los datos del evento en `+48`.
  Byte-swap de los campos multi-byte en `0xffffff800160d055`–`0xffffff800160d165`. `event_type ≥ 0xDD` →
  "Received garbage event, ignore" (comparación en `0xffffff800160d172`, log en `0xffffff800160d1bc`, línea 15344 del log).
- Enrutado: si el id de la interfaz Proxi (`core+11336`) coincide, todo evento va a
  `handleEvent` (vía `AppleBCMWLANCore::isEventForInterface`, `0xffffff80016367be`: compara el
  byte ifidx `+46` con `IO80211SkywalkInterface::getInterfaceId`; caso especial ifidx == 1 y
  `event_type == 54`). Además se entregan a Proxi **independientemente del ifidx**: id 26
  (sitio `0xffffff800160de32`, junto a `ScanAdapter::scanComplete` e `AppleBCMWLANNANInterface::
  handleEvent`), id 98 (rama `case 0x62` → `LABEL_92`), id 80 (`case 0x50` → junto a
  `AppleBCMWLANCore::csaReceived`) e id 75 (`case 0x4B` → `handleActionFrame_rx` directamente),
  sitios `0xffffff800160d2c5`/`0xffffff800160de32`/`0xffffff800160df19`.
- Nombres canónicos: `kEventBitMapping` (`0xffffff80017ed070`, 208 entradas de 16 bytes
  {u32 id, char* nombre}) consumida por `convertEventToString` (`0xffffff80016bb3d3`).
  Ids relevantes: 26 `WLC_E_SCAN_COMPLETE`, 54 `WLC_E_P2P_DISC_LISTEN_COMPLETE`,
  59 `WLC_E_ACTION_FRAME`, 60 `WLC_E_ACTION_FRAME_COMPLETE`, 69 `WLC_E_ESCAN_RESULT`,
  75 `WLC_E_ACTION_FRAME_RX`, 80 `WLC_E_CSA_COMPLETE_IND`, 96 `WLC_E_AWDL_AW`,
  97 `WLC_E_AWDL_ROLE`, 98 `WLC_E_AWDL_EVENT`, 109 `WLC_E_PROXD`, 150 `WLC_E_TWT_SETUP` (así
  llamada en la tabla; la usada como "ranging auth" en `handleEvent` `?` divergencia del enum
  público), 152 `WLC_E_NAN_NON_CRITICAL` (ídem `?`), 169 `WLC_E_SLOTTED_BSS_PEER_OP`.

### 4.2 Lo que hace `handleEvent` (`0xffffff8001715ece`) con cada id

| id | nombre (`kEventBitMapping`) | manejo en el kext |
|---:|---|---|
| 0 | `WLC_E_SET_SSID` (enum público: `WLC_E_NONE` `?`) | reenvía el iovar `awdl_presencemode` (sitio `0xffffff8001716815`) y limpia `ivars+844` |
| 16 | `WLC_E_LINK` | contadores `ivars+900/+904` y estado link en `ivars+179` según bit 0 de `cursor+2` |
| 26 | `WLC_E_SCAN_COMPLETE` | ignorado (LABEL_146); el core ya lo consumió |
| 54 | `WLC_E_P2P_DISC_LISTEN_COMPLETE` | status ≤ 4 → log "OCL_COREDOWN"; si hay payload ≥ 5 B lo procesa `?` |
| 59 | `WLC_E_ACTION_FRAME` | solo log |
| 60 | `WLC_E_ACTION_FRAME_COMPLETE` | timestamp de TX de OOB AF (`mach_continuous_time`) y config low-latency `?` |
| 69 | `WLC_E_ESCAN_RESULT` | contador `ivars+84`; si `ivars+88 & 1`, `ScanAdapter::eventScanComplete` |
| 75 | `WLC_E_ACTION_FRAME_RX` | `handleActionFrame_rx` (`0xffffff800171795c`): reconstruye el frame + cabecera de 72 B y lo entrega por BPF (DLT 0xC) — ver [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §1.3 |
| 80 | `WLC_E_CSA_COMPLETE_IND` | publica evento userspace 94 (`AWDL_DFS_CSA_COMPLETE`, sitio `0xffffff8001715fd4`) |
| 96 | `WLC_E_AWDL_AW` | con payload ≥ 28 B y sync activa: `handleAvailabilityWindowStart/Extension/End` (`0xffffff8001718756`/`0xffffff8001718942`/`0xffffff80017184b6`) → eventos userspace 41/42/43; el payload fw lleva u32@+0, u16@+8/+10, u8@+12 (canal), u32@+22 `?` |
| 97 | `WLC_E_AWDL_ROLE` | status u32 del evento (`cursor+8`) → `ivars+676`; 0 → `handleSlaveEvent` (`0xffffff8001718ad6`), 1 → `handleNonElectionMasterEvent` (`0xffffff8001718bd4`), 2 → `handleMasterEvent` (`0xffffff80017189e4`); los tres publican el evento userspace 46 (`AWDL_SYNC_STATE_CHANGED`) |
| 98 | `WLC_E_AWDL_EVENT` | sub-despacho por el id del payload (TABLA de [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §3.1): 98.0 → eventos 125/126 (scan), 98.2/3 → 64 (TXCALIBRATION_REQUEST), 98.9 → 69 (UNICAST_AF_STATUS), 98.0x12 → 88 (DFS_CSA), 98.0x13/0x14 → 183 (DFSP_SUSPEND_RESUME), 98.0x15 → 204 (DWELL_START) |
| 109 | `WLC_E_PROXD` | manejo de proximidad/ranging `?` |
| 150 | (tabla: `WLC_E_TWT_SETUP`; uso real: ranging `?`) | publica evento userspace 84 (`RANGING_AUTHENTICATION_DONE`, u32 del fw) |
| 152 | (tabla: `WLC_E_NAN_NON_CRITICAL`; uso real: SDB `?`) | publica evento userspace 135 (`AWDL_SDB_TRANSITION`, mapa u32 de `fw+8`/`fw+12`) |

El firmware, por tanto, es quien detecta el ARRANQUE de cada availability window (96), quien
decide el rol de elección (97) y quien multiplexa los eventos internos AWDL (98); el kext solo
los traduce a eventos `apple80211` para wifip2pd/WCL.

## 5. Reparto de obligaciones observado

**Lo que el firmware Broadcom hace por sí solo** (y por lo que el kext solo ve eventos/config):

1. Mantener la instancia AWDL en el chip (`interface_create`/`awdl_if`) con su bsscfg propia.
2. Ejecutar la **elección de maestro** con los umbrales de `awdl_election_tree` y publicar el
   rol por `WLC_E_AWDL_ROLE` (97).
3. Sostener la **sincronía temporal**: programa de availability windows de `awdl_sync_params`
   (periodo, longitud, extensión, cadencia de AF), extensiones con `awdl_extcounts`, y emisión
   de `WLC_E_AWDL_AW` (96) al abrirse cada ventana.
4. **Transmitir los sync frames** con la plantilla `awdl_payload`, dwell `awdl_psf_dwell`,
   cabecera `awdl_af_hdr` y presencia `awdl_presencemode`.
5. **Saltar de canal** según `awdl_chan_seq` (y su variante SDB), con proxy DFS
   `awdl_dfsp_cfg`/`awdl_dfsp_ucsa`.
6. **Planificar el TX de action frames y datos por peer** con la caché `awdl_peer_op`
   (disponibilidad + capacidades por peer), `awdl_advertisers` y `awdl_aftxmode`, más los
   envíos OOB puntuales (`awdl_oob_af`, `awdl_oob_af_auto`, `awdl_afs_pload`).
7. **Filtrar RX** por RSSI (`awdl_af_rssi`) y entregar los AF recibidos por
   `WLC_E_ACTION_FRAME_RX` (75).
8. Medir y contabilizar: CCA (`cca_get_stats`), estadísticas internas (`awdl_stats`), límite de
   peers (`awdl_maxpeers`), ranging (`awdl_ranging_config`, `awdl_ftm_ranging_config`,
   `WLC_E_PROXD`).

## 6. Clasificación para un kext sin firmware AWDL (RTL8814)

### 6.1 (a) Configuración pura: almacenar y devolver

Requests cuya implementación Broadcom es esencialmente un almacén de ivars + (opcionalmente) un
iovar de eco, sin lógica temporal: SSID/STATE/BSSID/CHANNEL (getters triviales,
`0xffffff800171a702`–`0xffffff800171a752`), `AWDL_DEVICE_CAPABILITIES`
(`0xffffff800171be38`, constantes), `AWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES`
(`0xffffff800171ed38`), `AWDL_MIN_RATE` (`0xffffff8001728a3c`/`0xffffff8001728a48`),
`AWDL_TXCAL_PERIOD` (`0xffffff8001728cae`/`0xffffff8001728b8c`), `AWDL_DFSP_CONFIG/UCSA`
(`0xffffff8001728fba`/`0xffffff8001728dd0`/`0xffffff800172919c`),
`AWDL_PRESENCE_MODE` (`0xffffff800171bd12`/`0xffffff80017231d8`),
`AWDL_AF_TX_MODE` (`0xffffff8001719b62`/`0xffffff80017199c4`),
`AWDL_ENCRYPTION_TYPE/KEYS` (`0xffffff800171f900`/`0xffffff800171fa46`/`0xffffff800171f480`),
`AWDL_CONFIG` (`0xffffff80017215e2`), `PEER_CACHE_MAXIMUM_SIZE` (`0xffffff80017233b6`),
`AWDL_SECONDARY_MASTER_CHANNEL` (`0xffffff800171ec10`/`0xffffff800171f01e`),
`LLW_PARAMS` (`0xffffff800172753e`/`0xffffff8001726ac8`) y los debug dumps (`dumpSyncParams`,
`dumpPeerCache`, `dumpAwdlPeers`, `dumpAwdlStats`, `dump`, `logElectionTree`). En el port, estos
slots pueden limitarse a guardar el valor y contestar; el efecto real de cada uno se aplica en
(b)/(c).

### 6.2 (b) Timing periódico que el kext debe ejecutar

El firmware hace tres máquinas periódicas que un port debe reimplementar en software:

1. **Availability windows**: periodo `aw_period` (u16, `awdl_sync_params+8`), longitud
   `aw_len` (+0x10), extensión `aw_ext_len` (+0xE), periodo de action frames `adv_period`
   (+0xA), lifetime (+6, init 6). Valores de arranque del kext (`init`,
   `0xffffff8001718db0`): `ivars+688 = 6` (lifetime), `ivars+728 = 200` (aw_period; el log de
   init anuncia "guard time 20" para este ivar — factor ×10, unidades `?`),
   `ivars+740 = 100` (adv_period), `ivars+696 = 4` (maxNoMasterPeriod; log "4"),
   `ivars+700 = 20`, `ivars+676 = 4`, `ivars+664 = 1`, `ivars+668 = 90`; los valores reales
   los programa userspace con `AWDL_SYNC_PARAMS` (tipo 116, 24 bytes: aw_period, aw_len,
   aw_ext_len, adv_period, flag non-election-master). El guard time como request aparte es el
   tipo 124 (`AWDL_GUARD_TIME`, 8 bytes) — sin slot Broadcom `?`.
2. **Elección de maestro**: election id (u16), métrica (u32) y umbrales RSSI (init
   −61/−23/−61/−50/−70 dBm `?`) — en un port sin firmware, o lo ejecuta software (máquina de
   `IO80211AWDLPeerManager` en la ruta moderna) o se fija manualmente; el rol resultante debe
   alimentar los mismos estados que `handleEvent` observa por el evento 97.
3. **Salto de canal y PSF**: la secuencia (`awdl_chan_seq`, hasta 400 B de request, ≤ 160 B
   codificados al firmware, cacheada en `ivars+132`) debe ejecutarse con temporizador propio,
   igual que la transmisión periódica del sync frame (`awdl_payload` + `awdl_psf_dwell`).
   Tolerancias explícitas en el binario: no hay constantes de tolerancia de timing además de
   los timeouts de espera de la creación/borrado del chip interface (3000 ms / 1000 ms,
   `waitForDriverEvent`, `0xffffff8001715c62`) y la cola de 16 iovars asíncronos en vuelo.

### 6.3 (c) Trabajo por frame TX/RX

Lo no cubierto por [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) §9/§10 (flow
queues, SNAP, multicast duplicado, drain RX) y que aquí es responsabilidad del firmware:

- Anteponer la cabecera de AF (`awdl_af_hdr`, `FF×6 7F 00 17 F2`) a cada action frame TX y
  filtrar el RX por RSSI (`awdl_af_rssi`).
- Mantener la caché por peer (`awdl_peer_op`: opcode add/update/delete, capacidades
  HT/VHT/HE, chan seq por peer) para decidir cuándo un frame dirigido a un peer es
  transmisible dentro de la AW compartida; los contadores del firmware (`peeropdrop`,
  `noawchansw`, `psfchanswtchskip`) muestran que ese filtrado por-ventana es suyo.
- Programar los envíos OOB puntuales (`awdl_oob_af`: cabecera de 38 B + frame, con
  confirmación por el evento 60/69) y las transmisiones automáticas (`awdl_oob_af_auto`,
  `awdl_afs_pload`).
- Cifrar el datapath con la clave de grupo (`wsec_key`/`wsec`, tipo 219/220).

En el RTL8814AU estas cuatro funciones son lógica de la cola TX del kext (consultar la ventana
actual antes de dequear un frame, descartar/retener según el peer, construir la cabecera SNAP
local y cifrar en software o en el chip según capacidad).

## 7. Método y límites

- Escaneo de strings `awdl*` + xrefs, llamadores de `runVirtualIOVarSet/Get`, decompilación de
  ~90 funciones y desensamblado de los sitios de envío (probes `a_scan.py`, `b_decomp.py`,
  `c_probe.py`, `d_probe.py`, `e_tbl.py`, `f_probe.py` en
  `Build/ida_awdl/task_awdl_fw_duties/`; salidas `a_out.txt`, `b_out1/2.txt`, `c_out.txt`,
  `c_windows_digest.txt`, `d_out.txt`, `e_out.txt`, `f_out.txt`).
- Tabla de eventos leída de `kEventBitMapping` (208 entradas verificadas una a una, id por id).
- Payloads: offsets tomados del desensamblado de los sitios de `send*`/`run*` (las llamadas de
  Hex-Rays ocultan los argumentos de nombre/payload); los marcados `?` son campos cuyo mapa
  fino byte a byte no se reconstruyó por completo.
- `?` principales: unidad del guard time (factor ×10 observado, sin confirmación), mapa fino de
  `awdl_peer_op` (bloques HE/chan-seq finales), payload de `interface_remove` (fw ≥ 5), rx de
  `awdl_stats`/`cca_get_stats` según versión de fw, semántica de los ids 150/152 frente al
  enum público (`kEventBitMapping` dice `WLC_E_TWT_SETUP`/`WLC_E_ESCAN_RESULT`-adyacentes pero
  `handleEvent` los usa para ranging/SDB), el interior de `issueCommand` hasta los anillos del
  bus PCIe, y el sitio exacto de `interface_create` dentro de `setAWDL_BSSID`.
- Ninguna de las conclusiones implica prueba en hardware RTL8814.
