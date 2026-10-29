# Eventos del kernel hacia userspace en el lado AWDL (awdl0) y su consumo en wifip2pd

Inventario completo de lo que `AppleBCMWLANProximityInterface`, `IO80211AWDLProtocol`,
`IO80211VirtualInterface`, `IO80211SkywalkInterface` y el resto de `IO80211Family` envían
hacia userspace por la familia de canales Apple80211 (`postMessage`, ids de evento
apple80211, canal P2P, frames AWDL y BPF) una vez que existe `awdl0`, y qué consume
`wifip2pd` de todo ello. Cierra la pregunta abierta en `AWDL_WIFIP2PD_P2P_EVENTS.md` §3
(dónde está el switch de ids de evento). Base: `BootKernelExtensions.kc` (md5
`f489ff8be80e6013a1bf47561dcaef45`) y `wifip2pd.x86_64` (md5
`a120cfccfa2783303604ce5be9e1763a`), análisis con IDA 9.4 headless (idat -A -S) sobre
copias de ambos IDB en `Build/ida_awdl/task_awdl_events/kernel/` y `/p2pd/` (scripts
`k_probe*.py`/`w_probe*.py`, salidas `k_out*.txt`/`w_out*.txt`). Lo no verificado va
como `?`. Las direcciones son VA del binario indicado (kernel salvo indicación).

**Resumen en cuatro líneas:**

1. El canal principal es el *event pipe* de IO80211Family: cada evento es un par
   (id apple80211, payload ≤ 0x1000 B) que `IO80211SkywalkInterface::postMessageSync`
   escribe en un anillo compartido con el cliente (`IO80211IOUCEventPipe::writePipe`)
   con una cabecera fija de 48 B y notifica con `sendAsyncResult64`. Los nombres
   canónicos de los ids están en la tabla kernel `gAppleMIndexToString`.
2. Los frames AWDL (action frames) viajan por DOS vías: como evento
   `APPLE80211_M_AWDL_ACTION_FRAME_RX` (id 48) con payload
   `{apple80211_act_frm_hdr[72], frame[]}` sobre la interfaz awdl0
   (`IO80211AWDLPeerManager::postAFToP2pd`), y como taps BPF
   (`AppleBCMWLANProximityInterface::handleActionFrame_rx` → `bpfTapInput`, DLT 0xC).
3. `wifip2pd` registra el monitoreo con `Apple80211RawP2PEventMonitoringInit` (awdl0,
   rol 1) o `Apple80211RawEventMonitoringInit` + 16× `Apple80211StartMonitoringEvent`
   (rol 0), y consume los eventos en un único switch maestro (`sub_1000388D0`,
   0x8e9e B) con 58 casos sobre ids crudos; el switch construye un enum `DriverEvent`
   y entrega el evento al `eventHandler` de la `InterfaceHandle`.
4. Para considerar sana la interfaz AWDL, wifip2pd no necesita ningún evento en
   tiempo de creación (el semáforo de 30 s lo libera IORegistry, cerrado en
   `AWDL_WIFIP2PD_POST_CREATE.md`); lo que sí debe tener es el registro del monitoreo
   sin error, y en operación consume los ids 46/47/48/65/142/195/234/235/243 entre
   otros. Los ids que el kext Broadcom emite sobre la interfaz AWDL y que wifip2pd
   ignora (41/42/43, 64, 69, 88, 94, 135, 170, 183, 204, …) son opcionales.

## 1) Canales de entrega del kernel

### 1.1 Canal Apple80211 general (event pipe por interfaz)

Cadena de entrega, verificada descompilando cada eslabón:

| Paso | Función | Dirección |
|---|---|---|
| Emisión Broadcom (controlador) | `IO80211Controller::postMessage(iface, id, data, len, bool)` | `0xffffff8002299fae` |
| Delegación del controlador | `CommonGlue::sendMessage(CommonGlueMessage&)` (slot 288 de la vtable del objeto en ivar del controlador) | `0xffffff8002186c3c` |
| Recepción CommonGlue | `IO80211Glue::receiveMessageInternal` | `0xffffff8002194204` |
| Emisión IO80211Family (interfaz) | `IO80211SkywalkInterface::postMessage` (10 B) → `postMessageInternal` | `0xffffff80022f85f6` → `0xffffff80022f8600` |
| Ruta asíncrona WCL | `IO80211Glue::routeEventToWcl` → `addEventToPendingQueue` | `0xffffff80021941c0` → `0xffffff8002194094` |
| Drenaje de la cola | `IO80211Glue::processPendingEventQueue` → `sendEventAndFilter` | `0xffffff8002193ff4` → `0xffffff8002193d5c` |
| Entrega síncrona | `IO80211SkywalkInterface::postMessageSync` | `0xffffff80022f8abc` |
| Reparto a los clientes | `IO80211SkywalkInterface::postMessageIOUCE` (por BSDName, itera los pipes) | `0xffffff80022f66a2` |
| Escritura en el anillo | `IO80211IOUCEventPipe::writePipe` | `0xffffff80022a7e7c` |
| Reserva de entrada | `IO80211IOUCEventPipe::reserveRingEntry` | `0xffffff80022a776e` |
| Notificación al cliente | `IO80211IOUCEventPipe::notifyUser` → `IOUserClient::sendAsyncResult64` | `0xffffff80022a76ee` |

- `postMessageInternal` (`0xffffff80022f8600`): si el flag `sendToWcl` está activo y hay
  Glue, entrega por `routeEventToWcl` (cola); si no, directamente por
  `postMessageSync`. `IO80211Controller::postMessage` delega en el objeto del ivar del
  controlador y aterriza en el mismo `postMessageSync` vía `sendEventAndFilter`
  (`0xffffff8002193e59`), que entrega sobre la interfaz primaria.
- **Entrada de la cola** (`postMessageQueueEntry`, construida en `addEventToPendingQueue`
  `0xffffff8002194094`, `IOMallocZeroData(len+32)`, panic `"Failed to alloc
  postMessageQueueEntry\n" @IO80211Glue.cpp:336`): enlaces de lista `+0`/`+8`, **u32 id
  `@+16`**, **u32 len `@+20`**, u64 timestamp µs `@+24`, **payload copiado `@+32`**
  (`memmove(entry+32, data, len)`). `sendEventAndFilter` lee exactamente esos campos
  (`+16`, `+20`, `+24`, `+32` en `0xffffff8002193d5c`), llama al filtro (slot 288 de la
  vtable de CommonGlue) y si no filtra, a `postMessageSync`.
- **Payload en el anillo** (`reserveRingEntry` `0xffffff80022a776e`): cabecera de 48 B
  `{u32 marcador 82 ('R') `@+0` (escrito tras `_mm_mfence`), u16 `@+4` (=0), u16
  número de serie `@+6`, u64 epoch secs `@+8`, u64 epoch µsecs `@+16`, u16 longitud del
  payload `@+24`, u16 padding `@+26`, u32 id de evento `@+28`, char ifname[16] `@+32`
  (`strlcpy(..., 0x10)`)}; el payload se copia detrás (`@+48` + padding alineado). En
  el relleno por wrap se escribe el marcador 80 ('P'). `writePipe` rechaza payloads
  `> 0x1000` y entradas sin espacio (devuelve -536870210, "Eventpipe is full, drop
  message[%d][%s], dataLen %lu" en `postMessageSync` `0xffffff80022f8abc`, con el
  nombre del id tomado de `gAppleMIndexToString`).
- **Filtro de suscripción**: `IO80211APIUserClient::extSubscribeEvent` (selector 2 de
  `sMethods`, tabla en `0xffffff8002478ed0`, función `0xffffff8002352486`) copia un
  filtro de 64 B del argumento a cliente`+304..+367` y pone el flag "suscrito"
  `@cliente+368`. `writePipe` comprueba el flag `@+368`, luego el bit por id en el
  bitmap `@cliente+417+((id-1)>>3)` bit `(id-1)&7`, salvo que esté activo el flag de
  bypass `@cliente+416` (en cuyo caso pasa todo). El setter exacto del bit por id (lo
  que termina llamando `Apple80211StartMonitoringEvent`) y del flag de bypass no se
  localizó (`?`); el accessor de lectura es `IO80211APIUserClient::isSubscribedToMsg`
  (`0xffffff80023556ba`). `extEventMonitorInit` (selector 3, `0xffffff800235256e`) crea
  el pipe (`IO80211SkywalkInterface::createEventPipe` `0xffffff80022f6bf2` →
  `IO80211IOUCEventPipe::withClient` `0xffffff80022a71d8`); `extEventMonitorExit`
  (selector 4, `0xffffff80023525aa`) lo destruye.
- Canal paralelo kev (notificaciones de interfaz a agentes): función global
  `IO80211InterfacePostMessage(ifname, id, data, len, kev_msg*)` (`0xffffff800229b746`)
  arma `{0x600000001, 1, id, 16, ifname, len|ptr, ctrl}` y llama
  `IO80211QueueCall::kev_post_msg` (`0xffffff800236a11a`); solo notifica si
  `len != 0` y (`len < 0xFF1` o `data == NULL`) — `"IO80211InterfacePostMessage: bad
  message data or message too long\n"` — y nunca los ids 12 y 58.

### 1.2 Canal P2P: reenvío de eventos de infra a los managers P2P

`IO80211InfraInterface::postMessage` (`0xffffff80023614b8`, 0x1375 B) maneja
internamente cada evento (switch gigante sobre el id: SSID 2, BSSID 3, link 4, RSSI 39,
CCA 60, IP 66, canal 82 con len 12, chan outage 125 con len 803, LQM 145–194, SIB 192,
LQMData 224–233, …) y al final llama siempre a
`IO80211InfraInterface::routeToP2PInterface(id, data, len)` (`0xffffff800236364e`).
Ese router **filtra por whitelist** y reenvía a dos consumidores:

- El manager NAN (meta-cast a `IO80211AWDLPeerManager`/NAN y llamada por vtable,
  slot `?` en `0xffffff800236364e`); el patrón coincide con
  `IO80211NANPeerManager::postMessage` (`0xffffff80022adfce`), que hace
  `postMessageSync` sobre la interfaz NAN (ivar `+1376` del manager).
- `IO80211AWDLPeerManager::reportDataPathEvents(id, data, len)`
  (`0xffffff80021dd8f4`, 0x322a B, resuelto por la vtable del manager `__ZTV22…` en
  `0xffffff800245a858`, vptr+664) — consumidor AWDL de los eventos reenviados.

**Whitelist del router P2P** (comparaciones en `0xffffff800236364e`): ids **2, 3, 4,
9, 66, 80, 125, 126, 127, 139, 160, 198, 242**. Nada más llega a los managers P2P por
esta vía.

### 1.3 Frames AWDL hacia wifip2pd

- **Canal de evento**: `IO80211AWDLPeerManager::postAFToP2pd(IO80211Buffer*,
  apple80211_act_frm_hdr*)` (`0xffffff800220fc2c`): asigna `frameLen+72` B, copia la
  cabecera `apple80211_act_frm_hdr` (72 B) `@+0` y el frame crudo `@+72`
  (`buffer->getLength()` vtable+304, `buffer->getBytes()` vtable+344), y lo emite con
  la llamada virtual `postMessage` (slot vptr+0xB18) con **id 48** y flag WCL=0 sobre
  la interfaz del manager (ivar `+1376` = awdl0). Incrementa el contador
  `ivars->fAWDLActionFrames` (reporter key `0x4966416650325064` = "IfAfP2Pd"). El
  fallo de asignación se loguea con `"-------- %s[%d] ------- Could not allocate AR
  BUFFER for AF message"` (IOLog en el camino de error de `0xffffff800220fc2c`,
  sitio exacto `?`).
- **Canal BPF**: `AppleBCMWLANProximityInterface::handleEvent` (fw id 75/98.1) →
  `handleActionFrame_rx` (`0xffffff800171795c`): reconstruye el frame en un buffer del
  pool de management (`getMgmtFramePool`), le antepone una cabecera de 72 B (0x48)
  tipo `wl_event_rx_frame_data` — int `@+0` desde cursor+24, u16 `@+4` desde cursor+28,
  campos MAC/chan con byteswap desde cursor+52/56/60 (variante v1, cuando el u16 del
  header != 512) o cursor+56/64/60 + 4 B de cursor+68 (variante v2, u16 == 512), y
  canal convertido con `AppleBCMWLANChanSpec::getAppleChannelSpec` +
  `ChanSpecConvToApple80211Channel` — y llama
  `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*, uint, void*, ulong)`
  (`0xffffff8002331070`) con DLT 0xC, sitio `0xffffff8001717f9d`;
  `handleProbeResponse_rx` también tapea (sitio `0xffffff80017183d6`). La ruta
  genérica es `IO80211InfraInterface::bpfTapInternal` (`0xffffff8002367126`).

## 2) Nombres canónicos de los ids: `gAppleMIndexToString`

La tabla `__ZL20gAppleMIndexToString` (`0xffffff8002483e80`) del kext IO80211Family
mappea id → string; las entradas 1–246 son los nombres `APPLE80211_M_*` (la función
`postMessageSync` la usa para loguear, `0xffffff80022f8abc`); las entradas 247–259 ya
no son eventos sino strings de rol de interfaz NAN/AWDL ("Discovery", "Data + InfraData",
…). Nombres de los ids relevantes para AWDL, leídos de la tabla:

| id | nombre | id | nombre |
|---:|---|---:|---|
| 1 | `APPLE80211_M_POWER_CHANGED` | 48 | `APPLE80211_M_AWDL_ACTION_FRAME_RX` |
| 2 | `APPLE80211_M_SSID_CHANGED` | 49 | `APPLE80211_M_RESET_INTERFACE` |
| 3 | `APPLE80211_M_BSSID_CHANGED` | 55 | `APPLE80211_M_DRIVER_AVAILABLE` |
| 4 | `APPLE80211_M_LINK_CHANGED` | 59 | `APPLE80211_M_MAC_ADDRESS_CHANGED` |
| 9 | `APPLE80211_M_ASSOC_DONE` | 65 | `APPLE80211_M_AWDL_STATISTICS` |
| 10 | `APPLE80211_M_SCAN_DONE` | 66 | `APPLE80211_M_ASSOC_READY` |
| 11 | `APPLE80211_M_COUNTRY_CODE_CHANGED` | 67/68 | `…_AWDL_REALTIME_MODE_START/END` |
| 22 | `APPLE80211_M_P2P_PEER_DETECTED` | 74 | `APPLE80211_M_AUTH` |
| 41 | `…_AWDL_AVAILABILITY_WINDOW_START` | 78 | `APPLE80211_M_ASSOC` |
| 42 | `…_AWDL_AVAILABILITY_WINDOW_EXTENSION_START` | 82 | `APPLE80211_M_SET_INFRA_CHANNEL` |
| 43 | `…_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_END` | 95–129 | `APPLE80211_M_NAN_*` |
| 46 | `…_AWDL_SYNC_STATE_CHANGED` | 125/126 | `…_INFRA_SCAN_START/COMPLETE` |
| 47 | `APPLE80211_M_PEER_PRESENCE` | 127 | `APPLE80211_M_INFRA_ASSOC_START` |
| 64 | `…_AWDL_TXCALIBRATION_REQUEST` | 160 | `APPLE80211_M_SDB_MODE_CHANGED` |
| 69 | `…_AWDL_UNICAST_AF_STATUS` | 215 | `…_INFRA_PEER_REQUIRES_IP_RES` |
| 77 | `…_AWDL_ADVERTISERS_LIST` | 234/235 | `…_AWDL_IDLE_DETECT/SESSION` |
| 88/94 | `…_AWDL_DFS_CSA/_COMPLETE` | 195 | `…_AWDL_CHAN_SEQ_CHANGED` |
| 135 | `…_AWDL_SDB_TRANSITION` | 200 | `…_AWDL_STATS_P2P_FAILURE` |
| 142 | `…_AWDL_HPP_STATISTICS` | 243 | `…_AWDL_RTG_STATISTICS` |
| 149 | `…_P2P_AIRPLAY_STATISTICS` | 246 | `…_NAN_RADIO_SCHED_CHANGED` |

La lista completa 1–246 está volcada en `Build/ida_awdl/task_awdl_events/kernel/k_out3.txt`.

## 3) Emisores del kernel (matriz id → emisor → payload)

Los sitios de llamada se extrajeron por xrefs (probe 1) y por barrido de llamadas
virtuales `call [vptr+0xB18]` con el inmediato del id en el registro de argumento
(probe 5); los ids están todos confirmados por el pseudocódigo del emisor salvo
indicación. Payload = lo que el emisor construye; "sobre" indica la interfaz cuyo
event pipe lo recibe.

### 3.1 `AppleBCMWLANProximityInterface` (kext Broadcom; fw events → `IO80211Controller::postMessage`)

`handleEvent(IO80211BufferCursor const&)` (`0xffffff8001715ece`, 0x1a09 B) es el
dispatcher de eventos de firmware del rol 6: lee el id de fw en cursor`+4` y el payload
en cursor`+48` (len cursor`+20`). Emisiones hacia userspace (todas via
`IO80211Controller::postMessage`, sitio de llamada citado):

| id | emisor (función `@` sitio) | payload |
|---:|---|---|
| 41 `AWDL_AVAILABILITY_WINDOW_START` | `handleEvent` fw96 → `handleAvailabilityWindowStart` `0xffffff80017188d1` | ~24 B en pila desde el evento fw de 28 B (u8 fw+12, u32 fw+0, u16 fw+8/10, u32 fw+22, canal convertido); mapeo exacto campo a campo `?` |
| 42 `…_EXTENSION_START` | `handleAvailabilityWindowExtension` `0xffffff80017189ac` | 8 B: u8 fw+12 `@0`, u32 fw+0 `@4` |
| 43 `…_EXTENSIONS_END` | `handleAvailabilityWindowEnd` `0xffffff8001718649` | ≥16 B: u8 `@0`, u32 `@4`, u8 `@8`, u32 `@12`; con el flag de keep-alive del core añade u16 `@14`, u16 `@4`, qwords `@16/24` (keep-alive) |
| 46 `AWDL_SYNC_STATE_CHANGED` | `handleMasterEvent` `0xffffff8001718a7e` / `handleSlaveEvent` `0xffffff8001718b6c` / `handleNonElectionMasterEvent` `0xffffff8001718c6e` / `bringdownLink` `0xffffff8001719d66` | 16 B: u32 estado `@0` (1=master, 2=slave, 3=non-election master, 0=down), resto a 0/0xAA |
| 64 `AWDL_TXCALIBRATION_REQUEST` | `handleEvent` fw98.2/3, sitio compartido `0xffffff8001715fd4` (id en registro; pseudocódigo `v11 = 64`; el inmediato visible en ese sitio corresponde a la ruta fw80/94) | 2 B: u16 leído del fw |
| 69 `AWDL_UNICAST_AF_STATUS` | `handleOobAfIovarAsyncCallBack` `0xffffff800171d4da` (directo); `handleEvent` fw98.9 vía el tail compartido de `LABEL_145` (id en registro, `?` sitio individual) | 8 B: u32 `@0`, u16 `@4`, u8 `@6` (0xDA) |
| 88 `AWDL_DFS_CSA` | `handleEvent` fw98.0x12 (id en registro por pseudocódigo `v11 = 88`, `?` sitio individual) | passthrough del payload fw (≥4 B) |
| 94 `AWDL_DFS_CSA_COMPLETE` | `handleEvent` fw80, sitio `0xffffff8001715fd4` (compartido con la ruta de 64) | NULL |
| 125 `INFRA_SCAN_START` | `handleEvent` fw98.0, sitio `0xffffff80017173e9` | buffer de **0x322 B (802)** a cero con el último byte a 1 (`bzero(&v95, 0x322)` + `v98 = 1`) |
| 126 `INFRA_SCAN_COMPLETE` | `handleEvent` fw98.0, sitio `0xffffff80017172d5` | 2 B: u16 `0x0100` |
| 135 `AWDL_SDB_TRANSITION` | `handleEvent` fw152, sitio `0xffffff8001716c23` | 8 B: u32 mapa de fw+8 `@0`, u32 mapa de fw+12 `@4` |
| 170 `RETRY_CHAN_SEQ` | `handleProxSlotBSSCompletion` `0xffffff8001720a14` | NULL (con estatus -16/-25 del offload) |
| 183 `DFSP_SUSPEND_RESUME` | `handleEvent` fw98.0x13/0x14 vía `LABEL_145` (id en registro `v36 = 183`, `?` sitio individual) | 4 B: u32 `0x10000` (STA desaparece) / `0x1000000` |
| 204 `DWELL_START` | `handleEvent` fw98.0x15 vía `LABEL_145` (id en registro `v36 = 204`, `?` sitio individual) | 4 B: u32 `0x10000` |
| 208 `WCL_REASSOC_LOW_LATENCY_TRAFFIC_CHANGED` | `setLowLatencyTrafficState` `0xffffff800172031d` | 2 B: u8, u8 — sobre la **interfaz de infra** (`getInterface` ivar 11328), no sobre awdl0 |
| 84 `RANGING_AUTHENTICATION_DONE` | `handleEvent` fw150 (id en registro, `?` sitio individual) | 4 B: u32 fw+8 — sobre la interfaz de infra |

En `handleEvent` los únicos sitios de llamada/jump a
`IO80211Controller::postMessage` con inmediato verificable son
`0xffffff8001715fd4` (94), `0xffffff8001716c23` (135), `0xffffff80017172d5` (126) y
`0xffffff80017173e9` (125) (probe 8, `k_out8b.txt`); los demás casos convergen en
esos mismos sitios con el id cargado en registro (pseudocódigo de `k_out2b.txt`).

### 3.2 `IO80211AWDLPeerManager` / `IO80211PeerManager` (kext IO80211Family)

| id | emisor (función `@` sitio) | payload | sobre |
|---:|---|---|---|
| 47 `PEER_PRESENCE` | `IO80211PeerManager::removePeer` `0xffffff8002254248`; `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` `0xffffff8002214d6d` | **0x44 B (68)**: MAC 6 B `@0` (peer+88/92), u32 `@8` = 1 (marca de ausencia/presencia, semántica exacta `?`), resto 0 | la interfaz del manager (awdl0) |
| 48 `AWDL_ACTION_FRAME_RX` | `postAFToP2pd` `0xffffff800220fce2` | `frameLen+72`: `apple80211_act_frm_hdr` 72 B `@0`, frame `@72` | awdl0 |
| 65 `AWDL_STATISTICS` | `awdlStatsPostMetrics` `0xffffff80021fbe63`; helper `postAwdlStatistics` `0xffffff8002332024` | NULL, len 0 (solo si rol == 6) | awdl0 |
| 86 `AWDL_NEARBY_LOG_TRIGGER` | `nearbyLogTriggerReceived` `0xffffff8002220a52` | `?` | awdl0 |
| 142 `AWDL_HPP_STATISTICS` | `sidecarStatsPostMetrics` `0xffffff8002225306`; helper `postAwdlHppStatsEvent` `0xffffff80023345ba` | `apple80211_awdl_low_latency_statistics_reduced`, **0x658 B (1624)** | awdl0 |
| 149 `P2P_AIRPLAY_STATISTICS` | `airplayStatsPostMetrics` `0xffffff80022045d1`; helper `0xffffff800233460c` | `apple80211_p2p_airplay_statistics`, **0xD0 B (208)** | awdl0 |
| 157 `LOWLATENCY_STATISTICS` | `IO80211NANPeerManager::lowlatencyStatsPostMetrics` `0xffffff80022acf9f`; helper `postLowlatencyStatistics` `0xffffff80023345e0` | `apple80211_nan_low_latency_peer_statistics`, **0xF20 B (3872)** (solo roles 8/9) | interfaz NAN |
| 172 `SOFTAP_CH_CHANGED_EVENT` | `validateForHostapAWDLConcurrency` `0xffffff80021d8b40`; helper `postHostapChannelChanged` `0xffffff8002334658` | `apple80211_hostap_state`, 0x10 B | awdl0 |
| 182 `P2P_THREAD_COEX_EVENT` | `notifyThreadCoexEvent` `0xffffff80022013eb`; `P2PSupervisor::setAWDLToAdoptNANChannelSequence` `0xffffff8002391044`; helper `postP2pThreadCoexEvent` `0xffffff800233467e` | `apple80211_p2p_thread_coex_event`, 2 B | awdl0 |
| 195 `AWDL_CHAN_SEQ_CHANGED` | `setChannelSequenceUnconditional` `0xffffff80021cc29b` (vía virtual +0xB18) | `?` (el len no es inmediato visible) | awdl0 |
| 200 `AWDL_STATS_P2P_FAILURE` | `postP2pFailureStatistics` `0xffffff80021f3700` | `?` | awdl0 |
| 234 `AWDL_IDLE_DETECT` | `AWDLStateDump` `0xffffff800222b4b5` | `?` | awdl0 |
| 235 `AWDL_IDLE_SESSION` | `disableAWDL` `0xffffff80021c6c9c` | `?` | awdl0 |
| 243 `AWDL_RTG_STATISTICS` | `rtgStatsPostMetrics` `0xffffff800222a550`; helper `postAWDLRtgStatistics` `0xffffff8002334632` | `apple80211_awdl_rtg_statistics`, **0x4C B (76)** | awdl0 |
| 210 `P2P_PEER_COUNTRY_CODE` | `disableAWDL` `0xffffff80021c6f32`; `PeerManager::monitorCountryCode` `0xffffff800225b1f9` | `?` | infra |
| 140 `BSS_STEERING_REQUEST_EVENT` | `forceRoamToPeersAP` `0xffffff80021f96c6`; `disassocInfra` `0xffffff80021fab51`; steering managers `0xffffff80022c57da`/`0xffffff80022f1bf1`/`0xffffff80022f2342` | `?` | infra |
| 215 `INFRA_PEER_REQUIRES_IP_RES` | `IO80211InfraPeer::printStats` `0xffffff800223e6f7` | `?` | infra |
| 68 `AWDL_REALTIME_MODE_END` | `clearAirPlayingState` `0xffffff80021f31e9` | `?` | awdl0 |
| 67 `AWDL_REALTIME_MODE_START` | `notifyRealTimeModeOnOff` `0xffffff80021f1d58` (vía virtual, inmediato no visible) `?` | `?` | awdl0 |
| 246 `NAN_RADIO_SCHED_CHANGED` | `IO80211NANRadioResourceManager::setDeviceChannelAvailability` `0xffffff80022fedfe` | `?` | NAN |
| 197 `NAN_PREFERRED_CHANNELS_CHANGED` | `IO80211NANDataPathManager` `0xffffff800230779f`/`0xffffff800230e67a`; `P2PSupervisor::notifyPreferredChannelChange` `0xffffff800239210a` | `?` | NAN |
| 159 `NAN_OOB_AF_RX` | `IO80211NANPeer::handleOobActionFrameRx` `0xffffff80022b8dd2` / `handlePasnActionFrameRx` `0xffffff80022b8f9f` | `?` | NAN |

### 3.3 Helpers genéricos de `IO80211VirtualInterface` (emiten solo en rol 6/9, verificando `*(ivar36+88)`)

Los helpers `post*` (probe 5) fijan id y longitud; el llamador aporta el buffer:

| helper | dirección | id | len |
|---|---|---:|---:|
| `postSyncStateChanged` | `0xffffff8002332054` | 46 | 0 |
| `postAwdlStatistics` | `0xffffff8002332024` | 65 | 0 |
| `postAwdlHppStatsEvent` | `0xffffff80023345ba` | 142 | 0x658 |
| `postLowlatencyStatistics` | `0xffffff80023345e0` | 157 | 0xF20 |
| `postP2PAirplayStatistics` | `0xffffff800233460c` | 149 | 0xD0 |
| `postAWDLRtgStatistics` | `0xffffff8002334632` | 243 | 0x4C |
| `postHostapChannelChanged` | `0xffffff8002334658` | 172 | 0x10 |
| `postP2pThreadCoexEvent` | `0xffffff800233467e` | 182 | 2 |
| `postHostAPBandUpdate` | `0xffffff80023346a4` | 225 | 4 |

`IO80211VirtualInterface::postMessage` (`0xffffff8002333192`, virtual +0xB18) además
intercepta en kernel los ids 4 (hostap state), 84 (ranging auth, len 20) y 85 (ranging
result, len 448) antes de reenviar por el slot vtable+2848.

## 4) Consumo en wifip2pd

### 4.1 Registro del monitoreo (cierra `AWDL_WIFIP2PD_P2P_EVENTS.md` §3)

`sub_100041B60` (`0x100041b60`) — refinado respecto del doc previo: **ambas vías son
Raw**; la distinción es la API y el flag:

- `role == 0 && cola NSObject` → `sub_100041EB0` (`0x100041eb0`) →
  `sub_100041F40(…, &unk_100515D20, Apple80211RawEventMonitoringInit)`, y **además**
  (solo flag 1) bucle de 16 ids desde `0x100515CA0` con
  `Apple80211StartMonitoringEvent` (ids: 1, 2, 3, 9, 11, 55, 59, 66, 74, 78, 82, 125,
  126, 127, 160, 215; bucle `0x100041e42–0x100041e76`; aborta al primer error).
- Cualquier otro caso (rol 1 = awdl0) → `sub_100041F20` (`0x100041f20`) →
  `sub_100041F40(…, &unk_100515D48, Apple80211RawP2PEventMonitoringInit)` — sin bucle
  de ids.
- `sub_100041F40` (`0x100041f40`) construye el bloque ObjC
  `{isa=_NSConcreteStackBlock, flags=0x42000000, invoke=sub_100041ED0, desc, ctx}`
  con la closure Swift en bloque`+32` y su contexto en bloque`+40`, y llama
  `Raw*EventMonitoringInit(reference, queue, _Block_copy(bloque))`.
- El callback C registrado es `sub_1000685A0` (`0x1000685a0`, con `self` = ctx Swift de
  0x30 B metadata `stru_100515C68`, campos `[2]`=closure fn, `[3]`=closure ctx,
  `[4]`=eventBuffer, `[5]`=eventBuffer+0x100): reenvía a `sub_100041FE0`.
- `sub_100041FE0` (`0x100041fe0`, dispatcher): si id == 0 o data == NULL entrega
  `Data` vacío; si `eventBuffer != NULL` y el payload cabe en 0x100 B, hace
  `memcpy(eventBuffer, data, len)` y construye el `Data` **sobre el buffer
  preasignado** con deallocator `.none` (`sub_10026BFE0`), sin copia; si no, copia a
  un `Data` nuevo (`sub_10027C790`). Invoca a la closure capturada
  `(id_evento, Data)`.
- Invoke del bloque: `sub_100041ED0` (`0x100041ed0`): `fn = *(bloque+32)`,
  `ctx = *(bloque+40)`, `fn(id, data, len)` — idéntico al descrito en
  `AWDL_WIFIP2PD_P2P_EVENTS.md` §2.

Cadena de closures hasta el switch: `sub_1000697B0` (`0x1000697b0`, thunk de 10 B) →
`sub_100063E80` (`0x100063e80`) → `sub_1000387F0(a2, a3, a4, ctx[2], ctx[3])`
(`0x1000387f0`) → decodifica y reparte. Los instaladores del handler:
`sub_1000421E0` (`0x1000421e0`, recovery, pasa `sub_1000697B0` y guarda el handler en
el ivar `eventHandler` de `InterfaceHandle`), `sub_100049F20` (`0x100049f20`, pasa
`sub_100063E80`; callers: thunk `sub_10004D140` con data-xref de witness en
`0x100514e48`, y `sub_100126180` ← `sub_1001263F0`), `sub_10004A0C0` (`0x10004a0c0`,
sin callers de código). La resolución por witness tables del camino AWDL sigue sin
nombrarse (`?`, igual que en `AWDL_WIFIP2PD_POST_CREATE.md` §2.1).

### 4.2 El switch maestro `sub_1000388D0` (0x1000388d0, 0x8e9e B)

Función que recibe `(id, Data.w0, Data.w1)`, valida el payload con helpers
(`sub_100062B50`, `sub_1000631F0`, `sub_1000632E0`, `sub_100063370`, `sub_1000629F0`,
`sub_1000626D0`, `sub_100068600`, `sub_100068700`, `BinaryDecoder` `sub_1001C3F40`, …),
loguea con el prefijo `"nan_event: %s APPLE80211_M_…"` y construye un valor del enum
`DriverEvent` (casos `DriverEvent.AWDL` con tag numérico; nombres de los casos del enum
no recuperados de la metadata Swift, `?`), que entrega al handler (tail común
`LABEL_149`/`LABEL_204`; fallos de validación → `LABEL_235` = descarte). El switch
sobre el id crudo tiene **58 casos**; los ids y lo que cada case lee del payload
(offsets sobre `__dst` = inicio del payload):

| id | offsets leídos | log / comportamiento visible |
|---:|---|---|
| 1, 2, 3, 9, 11 | — | cases presentes; manejo no desglosado en detalle (`?`) |
| 46 | — | `DriverEvent.AWDL` tag 11 con payload `(1, 2)` |
| 47 | u8 `@68`, u32 `@8` (==1), u32 `@0`, u8 `@4` | `DriverEvent.AWDL` tag 8; puerta: si el byte `@68` vale 1 u `@8` != 1 descarta; coincide con el payload kernel de 0x44 B (MAC `@0`, u32 `@8`) |
| 48 | rep `Data` (0/1/2), len ≥ 0x48 | copia 64+ B, valida el header de 72 B (`AWDLActionFrame.Header`), decodifica el resto con `BinaryDecoder` (`sub_1001C3F40`) → `AWDLActionFrame`; `DriverEvent.AWDL` tag 1 |
| 55 | u8 `@8`, q `@16` | log `"nan_event: %s APPLE80211_M_DRIVER_AVAILABLE with powerOn false %d"` |
| 65 | — | `DriverEvent.AWDL` tag 13 |
| 66 | `@8`, `@24`, `@40` | — |
| 67/68 | — | 68 → `DriverEvent.AWDL` tag 0 |
| 74 | `@4`, `@104` | — |
| 78, 82 | — | — |
| 95 | `@0`, `@8` | log `APPLE80211_M_NAN_DEVICE_ROLE_CHANGED role=%u sync=%u hopCount=%hhu` |
| 96 | `@19`, `@35`, `@40`, `@48`, `@52` | `APPLE80211_M_NAN_MASTER_CHANGED immediateMaster=%@ rank=%hhu:%hhu anchorMaster=%@ … flags=%u` |
| 97 | `@2`–`@5` | `APPLE80211_M_NAN_CLUSTER_CHANGED` |
| 98 | `@8`, `@16`–`@32`, `@41`, `@44`, `@60`, `@64`, `@72`, `@73` | — |
| 99 | `@8` | `APPLE80211_M_NAN_FOLLOW_UP_TX_COMPLETE` |
| 100 | MAC `@0`–`@8`, `@16` | `APPLE80211_M_NAN_DISCOVERY_RESULT_RECEIVED from %s` |
| 102 | `@0`–`@8`, `@16` | `APPLE80211_M_NAN_PUBLISH_REPLIED_PRIVATE` |
| 104/105 | `@0`, `@8`, `@16` | `…_PUBLISH/SUBSCRIBE_TERMINATED` |
| 106 | `@2`–`@8`, `@16` | `APPLE80211_M_NAN_FOLLOW_UP_RECEIVED` |
| 112 | `@2`, `@6`, `@7`, `@11`–`@16` | `APPLE80211_M_NAN_DP_REQ_RECVD` |
| 113 | `@8`, `@12`–`@18`, `@24` | `APPLE80211_M_NAN_DP_RSP_RECVD` |
| 114 | `@8`, `@16` | `APPLE80211_M_NAN_DP_CONFIRM_RECVD` |
| 117 | — | `APPLE80211_M_NAN_DP_ESTABLISHED` |
| 118 | `@0`–`@13` | `APPLE80211_M_NAN_DP_END` |
| 122 | `@8`, `@16`, `@24`, `@28`–`@29`, `@48`–`@72` | — |
| 124 | `@8`–`@32`, `@48`, `@64`, `@80`, `@96`, `@112` | — |
| 125 | copia 0x323 B; gate `sub_100068600` | construye el evento con marcador u16 `0x100` `@+40` del struct |
| 126 | u16 via `sub_1000632E0`; gate `(v & 0xFF0000) == 0x10000` | marcador u16 `0x200` `@+40` |
| 127 | via `sub_100063370` + `sub_1000D3040` | marcador u16 `0x300` `@+40` |
| 129 | — | `APPLE80211_M_NAN_DP_HOST_ASSIST_REQUEST` |
| 142 | — | — |
| 149 | — | `DriverEvent.AWDL` tag 2 |
| 157 | `@4`–`@8`, `@16`–`@20`, `@28`, `@32`, `@36`–`@37` | — |
| 158 | u8 `@2` | `APPLE80211_M_NAN_OOB_AF_TX_STATUS` |
| 159 | — | `APPLE80211_M_NAN_OOB_AF_RX` |
| 160 | — | `APPLE80211_M_SDB_MODE_CHANGED received mode:%hhu` |
| 172 | — | — |
| 179 | `@4`, `@8`, `@16`–`@48`, `@256` | log `"Failed to parse event[%ld]: %s"`, contexto "AirPlay over Infrastructure" / "Service Registration" |
| 182 | — | — |
| 195 | — | `DriverEvent.AWDL` tag 12 |
| 197 | — | `APPLE80211_M_NAN_PREFERRED_CHANNELS_CHANGED count=%ld channels=%s`; `"Invalid channel_band in preferred channel list"` |
| 200 | `@16`, `@32`, `@40` | — |
| 215 | — | — |
| 224 | — | — |
| 225 | u8 `@0` | — |
| 234 | — | — |
| 235 | — | `DriverEvent.AWDL` tag 14 |
| 243 | u16 `@62`, u8 `@63`, u32 `@68`, u8 `@70`, u8 `@71&1`, u32 `@69`, u16 `@73`; copia 56 B `@0` | repack + `DriverEvent.AWDL` tag 3 (storeEnumTagMultiPayload) |
| 246 | copia 0xF3/0xF2 B | `APPLE80211_M_NAN_RADIO_SCHED_CHANGED band24G=%s band5G=%s` |
| default | — | descartado (array vacío) |

Los cases sin desglose campo a campo en la tabla existen y compilan el mismo patrón
(validar → log → `DriverEvent`); su detalle fino no se transcribió (`?` sobre el
detalle individual, no sobre el consumo).

### 4.3 Correlación kernel ↔ wifip2pd

- **Lo que wifip2pd consume y el kernel emite sobre awdl0**: 46, 47, 48, 65, 68, 142,
  149, 172, 182, 195, 200, 234, 235, 243 (emisores en §3.2/§3.3) — todos con case en
  `sub_1000388D0`.
- **Lo que el kernel emite sobre awdl0 y wifip2pd ignora** (sin case → `default`):
  41, 42, 43 (ventanas de disponibilidad), 64, 69, 77, 86, 88, 94, 135, 170, 183, 204.
- **Canal P2P del router de infra** (§1.2): wifip2pd consume {2, 3, 9, 66, 125, 126,
  127, 160} de la whitelist (los registra además con StartMonitoringEvent en rol 0);
  los ids 4, 80, 139, 198, 242 se reenvían a los managers P2P pero wifip2pd no los
  maneja en el switch (4 sin case `?` — no aparece en la lista de cases).
- **Los 16 ids de `Apple80211StartMonitoringEvent`** (rol 0): todos menos 4 tienen
  case; 1, 2, 3, 9, 11, 55, 59, 66, 74, 78, 82, 125, 126, 127, 160, 215 sí.

## 5) Clasificación: necesario para la salud de awdl0 vs opcional

- **Registro (bloqueante si falla)**: la llamada a `Apple80211RawP2PEventMonitoringInit`
  (rol 1) o `Apple80211RawEventMonitoringInit` + las 16 `Apple80211StartMonitoringEvent`
  (rol 0) debe devolver 0; un error encadena `sub_1000421E0` (`0x1000421e0`) → log
  `"Failed to recover event monitoring for %s"` (sitio exacto `?`, dentro de
  `sub_1000421E0`) → error Swift → recuperación `sub_100046E30` (`0x100046e30`,
  "Attempting recovery for %s[%s] with %s on %s", sitio exacto `?`) que re-crea la
  interfaz (`sub_100047660`). Para el kext esto significa: los overrides de monitoreo
  (lo que sirva el ioctl/user client de registro) deben responder éxito.
- **Operación**: ningún evento es condición de salud por sí mismo — no hay temporizador
  que declare la interfaz muerta por silencio de eventos (`?`; el consumidor no
  contiene lógica de timeout, y la recuperación se dispara por fallos de
  RawGet/RawSet/ioctls, no por ausencia de eventos). Dicho esto, los eventos que
  alimentan el estado que wifip2pd expone son, por el contenido del switch:
  **48** (frames AWDL = descubrimiento de peers), **47** (presencia/ausencia de peer),
  **46** (estado de sincronización), **195** (secuencia de canales), **65/142/149/243**
  (estadísticas), **125/126** (scan de infra), y son los que un kext propio debería
  emitir para un comportamiento equivalente; los demás cases son contabilidad y logs.
- **Opcionales (emite el kext Broadcom, wifip2pd los descarta)**: 41, 42, 43, 64, 69,
  77, 86, 88, 94, 135, 170, 183, 204, 208, 210, 140, 215 (`?` para 215: tiene case,
  pero su manejo no se desglosó).
- El semáforo de 30 s tras `VIRTUAL_IF_CREATE` **no** depende de ningún evento
  (IORegistry `IOServiceMatched`; cerrado en `AWDL_WIFIP2PD_POST_CREATE.md` §Parte 1).

## 6) Método y límites

- IDA 9.4 headless (`idat -A -S`) sobre copias en
  `Build/ida_awdl/task_awdl_events/kernel/` (BootKernelExtensions.i64) y `/p2pd/`
  (wifip2pd.i64). El IDB antiguo `~/re_cache/ida/boot.kc.i64` **crashea Hex-Rays**
  (internal error 1228, reproducible en `k_run2*.log`); todo el lado kernel se hizo
  sobre una copia del IDB más reciente `Build/ida_awdl/BootKernelExtensions.i64`
  (mismo KC, md5 del contenedor `f489ff8b…`), también como copia local. Probes:
  `k_probe1` (tablas+xrefs), `k_probe2a/2b` (plomería y emisores Broadcom),
  `k_probe3` (matriz por registros + `gAppleMIndexToString` + selectores del user
  client), `k_probe4` (routeToP2PInterface, extSubscribeEvent, action frames, removePeer),
  `k_probe5` (helpers post*, barrido de llamadas virtuales +0xB18), `k_probe6`
  (postAFToP2pd, reportDataPathEvents), `k_probe7` (bitmap de suscripción);
  `w_probe1–5` (cadena de closures y switch maestro).
- La matriz de ids por sitio de llamada se extrajo del inmediato cargado en el
  registro de argumento (edx para `IO80211Controller::postMessage`, esi para el resto)
  caminando hacia atrás desde el `call`; los sitios marcados STALE (una llamada
  intermedia entre la carga y el `call`) son de confianza alta solo cuando el id
  cuadra con la función (p. ej. GAS → 219/220); los que no cuadran no se listaron.
- `?` explícitos: setter kernel del bit por id del bitmap de suscripción y del flag de
  bypass `@+416`; nombres de los casos del enum Swift `DriverEvent`/`DriverEvent.AWDL`
  (solo tags numéricos); payloads de 67, 86, 140, 195, 200, 210, 215, 234, 235, 246 y
  de todos los NAN; mapeo campo a campo del payload de 41; semántica del u32 `@8` del
  payload de 47 (ausencia vs presencia); el instalador del `eventHandler` del rol 1
  (dispatch por witness `0x100514e48` sin resolver); ausencia de timeouts por silencio
  de eventos (inferencia de ausencia, no búsqueda exhaustiva).
- No se modificó ningún archivo del repo fuera de `Docs/AWDL_EVENTS.md` y
  `Build/ida_awdl/task_awdl_events/`.
