# Block Ack en AWDL (role 6): quién lo maneja y qué debe implementar la capa RTL8814

Quién recibe, interpreta y responde las tramas ADDBA/DELBA/BAR y los A-MPDU para `awdl0` en macOS
26.7.1 (25G241, Darwin 25.6.0), y qué queda para un driver sin firmware Broadcom. Cierra la pregunta
abierta de [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §4 (fila Block Ack) con el dato de
[AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §7 (winawdl: aceptar la ADDBA del iPhone
en TID 0 con ventana 64 subió el rendimiento de 971 kB/s a 1,7 MB/s).

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), macOS 25.6.0,
decompilación Hex-Rays de los filesets `com.apple.driver.AppleBCMWLANCoreMac`,
`com.apple.driver.AppleBCMWLANBusInterfacePCIeMac` e `IO80211Family`. Direcciones = VA del KC,
todas vistas en el desensamblado/decompilado. Lo no observado va con `?`.

**Resumen corto:**

1. **La familia no hace Block Ack.** Los dos handlers del request `APPLE80211_IOC_BLOCK_ACK`
   (`apple80211getBLOCK_ACK` `0xffffff80022632f4`, `apple80211setBLOCK_ACK` `0xffffff8002268050`)
   son stubs que devuelven siempre el error `0xE082280E`, y su dirección no aparece en ninguna tabla
   de handlers del rango `0xffffff8002440000`–`0xffffff8002500000`: selector y handlers están
   muertos en 25.6.0. Ni `IO80211AWDLPeerManager` ni `IO80211AWDLPeer` parsean ni responden
   ADDBA/DELBA/BAR (verificado sobre los cuerpos decompilados de `actionFrameProcessInput` y
   `doMinimalAFProcessing`: cero referencias a Block Ack/AMPDU).
2. **En Broadcom todo el Block Ack lo hace el firmware.** El host solo registra cada par como
   capaz de A-MPDU mediante la petición `APPLE80211_IOC_PEER_CACHE_CONTROL` (struct de 143 B,
   flag `ampdu` en +44), que la interfaz proximity convierte en el iovar `awdl_peer_op` del
   firmware. Las sesiones (ADDBA req/resp, DELBA, BAR, ventana, reordenado) son internas de la
   firmware; los iovars `ampdu_tid`, `ampdu_send_addba`, `ampdu_send_delba`,
   `ampdu_tx_ba_window_cfg`, `ampdu_rx_ba_window_cfg`, `ampdu_rx_density`, `ampdu_rx_factor` son su
   única palanca.
3. **El evento por par que abre/cierra el A-MPDU es la traffic registration**: wifip2pd emite
   `APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION` (164 B) → `setAwdlPeerTrafficRegistration` →
   para el tipo «High Thruput» `initiateHighThruputMode` → `addPeerToFWCache` → la misma
   `PEER_CACHE_CONTROL`. El modo High Thruput/RTG es una migración D2D, no una sesión BA.
4. **El reordenado RX no existe en la familia ni en el datapath Skywalk**: la `RxCompletionQueue`
   entrega tal cual lo que el driver le mete. Con la firmware Broadcom, la ventana BA (contadores
   out-of-window/out-of-sequence/duplicate/stuck) es firmware. La capa RTL debe entregar en orden.
5. **El tirón de TX no limita la agregación**: `TxPacketRequest` (96 B) no tiene campos de
   A-MPDU; la decisión es del driver/firmware.
6. **Para la capa RTL8814**: responder la ADDBA por sí misma (la familia no lo hará nunca),
   procesar DELBA y BAR, llevar un buffer de reordenado por (par, TID) y entregar en orden a la
   `RxCompletionQueue`. La petición `setPEER_CACHE_CONTROL` (hoy stub en la capa,
   `AWDLInterface.h`) es el interruptor por par exactamente equivalente al que Broadcom le da al
   firmware.

## 1. Método

Pasadas sobre una copia del IDB en `Build/ida_awdl/task_blockack/`: (a) búsqueda global de
strings y nombres con patrones `addba`, `delba`, `blockack`, `block_ack`, `ampdu`, `amsdu`,
`reorder`, `ba_window`, `ba_session`, `thruput`, `rtgtx`, `traffic_reg`, `aggregat`;
(b) decompilación de los 30+ funciones citadas;
(c) rutas de llamadas vía xrefs; (d) lectura de la tabla de handlers del dispatcher de la familia.
Los VAs citados son cabeceras de instrucción o inicios de función vistos en la salida.

## 2. Superficie Block Ack de IO80211Family: muerta

`apple80211RouteExternal` (`0xffffff800236cd2e`, dispatcher de la familia) despacha por
`req->req_type` (dword en `apple80211req+16`) con dos pares de tablas: las legacy
(`_gGetHandlerTableLegacy` `0xffffff8002470d50`, `_gSetHandlerTableLegacy` `0xffffff8002479060`)
para selectores 1–581 y las modernas para 1–586, en ambos casos indexando `[selector-1]`; para el
log de errores resuelve el nombre con una tabla de strings `APPLE80211_IOC_*` (el literal
`APPLE80211_IOC_BLOCK_ACK` está en `0xffffff800244301d`).

- `apple80211getBLOCK_ACK` (`0xffffff80022632f4`) y `apple80211setBLOCK_ACK`
  (`0xffffff8002268050`): ambos `return 0xE082280E` incondicional. Ningún código los llama
  directamente y su puntero no está en ninguna de las tablas de handlers del rango escaneado
  (`0xffffff8002440000`–`0xffffff8002500000`, que incluye las dos legacy y la tabla adicional que
  empieza en `0xffffff800247ce90`): ni siquiera el selector puede llegar a ellos. `?` si existiera
  una tabla fuera de ese rango que los enrute; da igual: devuelven error siempre.
- El resto de surface AMPDU de la familia es **telemetría**, no control:
  `IO80211Controller::setAMPDUstat` (`0xffffff80022954d4`),
  `IO80211InterfaceMonitor::setAMPDUstat` (`0xffffff80023758a4`),
  `IO80211InterfaceMonitor::getAMPDUChannelID` (`0xffffff8002375652`),
  `IO80211LQMCrashTracer::recordRxAmpduUpdate` (`0xffffff80022a3dd4`) y el mensaje de evento
  `APPLE80211_M_RX_AMPDU_UPDATE` (string `0xffffff80024488d7`): estadísticas hacia userspace.
- El único control de agregación TX de la familia es infra: `IO80211ApFeatureConfig::
  shouldDisableTxAMPDU` (`0xffffff80022ea884`) y el request `APPLE80211_IOC_WCL_LIMITED_AGGREGATION`
  (string `0xffffff800244675c`). No hay equivalente AWDL.

**Conclusión (1):** ni la familia ni `IO80211AWDLPeerManager` ni `IO80211AWDLPeer` reciben,
parsean o responden ADDBA/DELBA/BAR para `awdl0`. `actionFrameProcessInput`
(`0xffffff800220e8b0`) y `IO80211AWDLPeer::doMinimalAFProcessing` (`0xffffff80021b3ea6`) —la ruta
completa de un action frame llegado a `actionFrameInput`— no contienen ninguna rama de categoría
Block Ack (categoría 3) ni referencia a AMPDU: una ADDBA que llegue al manager se descarta sin
efecto. En el diseño Broadcom el Block Ack es asunto de la firmware; el host solo declara qué
pares pueden agregarlo (§4).

## 3. Traffic registration y High Thruput: el disparador, no la sesión BA

Cadena de userspace a driver para marcar tráfico en tiempo real con un par AWDL:

```
wifip2pd → APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION (set)
  → glue setAWDL_PEER_TRAFFIC_REGISTRATION(IO80211Controller*, IO80211SkywalkInterface*, …)  0xffffff8002289465
      exige req+24 == 184 y payload no nulo; metaCast a IO80211AWDLPeerManager
      → IO80211AWDLPeerManager::setAwdlPeerTrafficRegistration(reg)                          0xffffff800220946a
          → handleAirDropTrafficReg / handleEnsembleTrafficReg / handleMigrationTrafficReg … (0xffffff80022087ac, 0xffffff8002202960, 0xffffff8002206608)
  → variante por interfaz: apple80211setAWDL_PEER_TRAFFIC_REGISTRATION                       0xffffff8002268d88
      valida tamaño 164 y llama a un método virtual del interfaz con el payload
      (el slot concreto no quedó identificado en esta pasada `?`)
      → AppleBCMWLANProximityInterface::setAWDL_PEER_TRAFFIC_REGISTRATION                    0xffffff800171fcaa
```

- La discrepancia 184 (glue, longitud del request) frente a 164 (handler de familia, tamaño del
  struct `apple80211_awdl_peer_traffic_registration`) y el slot virtual de la segunda ruta no
  quedan explicados por el decompilado `?`.
- Campos que las implementaciones leen: nombre de servicio en +16 (longitud en +12;
  `"CARPLAY_UUID"`, `"wifid-assisted-discovery"`, `"sidecar"`, `"rtmode"`, `"airplay"`,
  `"ensemble"`, `…Camera`, `"RemoteScreen"`, `"airplay-connectivity-check"` en la capa Broadcom),
  MAC del par en +4 (gestionada por el manager) y flag activo en +168 (u16 extra en +176).
- La variante Broadcom no toca AMPDU: enciende/baja el estado de tráfico de baja latencia
  (`setLowLatencyTrafficState`), engancha la interfaz compañera LLW y limpia con
  `clearTrafficRegistrationState` (`0xffffff800172a8a4`, que solo borra los bytes de estado
  CarPlay/AirPlay/SideCar en +100/+102/+103 y +552).
- Para el tipo de registro «High Thruput» (`handleMigrationTrafficReg` `0xffffff8002206608`,
  llamada en `0xffffff80022066de`), el manager entra en modo D2D:
  `initiateHighThruputMode(peer)` (`0xffffff80021fabcc`) → `setHighThruputState`
  (`0xffffff80021faa08`, marca `ivars+16176` y notifica `notifyRealTimeModeOnOff("D2DMigration")`)
  → `addPeerToFWCache(peer, true)` → `disassocInfra` + `setDataMode(34, 1)`. El par sale por
  `stopHighThruputMode` (`0xffffff80021fa86a`) al recibir el UMI con la opción de parada.
- Los UMI con estas opciones viajan en los action frames unicast AWDL:
  `IO80211AWDLPeer::processUnicastAWSF` (`0xffffff80021ba828`, llamado desde
  `parseAwdlDataPathTLVAndTakeAction` `0xffffff80021b92a0`) interpreta el bit `0x20000` como
  `kUnicastMIOptionsHighThruputStart` (string `0xffffff80023cbe65`, xref `0xffffff80021bb71b`,
  rama que llama a `startHighThruputMode`) y `0x40000` como `…HighThruputStop`
  (string `0xffffff80023cc6a6`, xref `0xffffff80021bc52d`); `initiateHighThruputMode` escribe
  `0x20000` en los ivars del par (+212) para responder. En la misma función, con opciones de
  datos, el manager también llama a `addPeerToFWCache` de forma rutinaria (log «Waiting for
  caching of Peer…»).
- `setAwdlRTGTxMinRate(bool)` (`0xffffff80021d22de`) solo fija la tasa mínima de TX del grupo RTG;
  nada de BA.

**Conclusión (2):** el único mecanismo «por par» que abre o cierra el A-MPDU es la pareja
traffic registration → peer cache (§4). No existe petición ni evento por TID en la familia; la
política BA por TID (`changeAggregationPerEDCATID`, §4) es interna del core Broadcom sin ruta
ioctl. El payload relevante es el de `PEER_CACHE_CONTROL` (§4).

## 4. La ruta real: `PEER_CACHE_CONTROL` → iovar `awdl_peer_op` → firmware

```
IO80211P2PDataPathManager::addPeerToFWCache(peer, hiPrio)          0xffffff800229db9c
  límites LRU (_currentPeerCacheSize/_maxPeerCacheSize), cooldown de ~1 s
  (≤999 ms), bloqueos por hot-spot concurrency y delete en curso
  → buffer de 464 B (IOMallocZeroData) → peer vtable+352 = fillInCacheParams(buf)
      IO80211AWDLPeer::fillInCacheParams                            0xffffff80021ac04c
        └ IO80211Peer::fillInCacheParams                            0xffffff8002240ade
  → sendIoctlToCachePeer(buf, peer)                                 0xffffff800229e458
      ioctl_internal con token 0x8F802869C8 (dword bajo = el ioctl SET 0x802869C8;
      dword alto 0x8F = 143, el mismo tamaño que valida la familia `?` si es tamaño o tipo)
      → apple80211setPEER_CACHE_CONTROL                             0xffffff8002268abb
          valida tamaño 143; metaCast a IO80211AWDLProtocol → slot vtable +0x1108
          (o IO80211SapProtocol → +0x1068; si no, 0xE082280E)
          → AppleBCMWLANProximityInterface::setPEER_CACHE_CONTROL   0xffffff80017235ce
              construye el buffer del iovar "awdl_peer_op" y lo envía por
              AppleBCMWLANCommander::sendVirtualIOVarSet (callback asíncrono
              handleCacheControlAsyncCallBack); error → "Unable to send awdl_peer_op"
```

Estructura `apple80211_peer_cache_control` (tal y como la rellena la familia y la lee la capa
Broadcom; la petición de usuariospace se valida en 143 B, el buffer interno del kernel es de
464 B — `bzero(a2+4, 0x1CC)` en `IO80211Peer::fillInCacheParams`):

| offset | campo | observación |
|---|---|---|
| +0 | u32 versión = 1 | |
| +4 | u32 opcode | 0 = ADD, 2 = DELETE, 6 = UPDATE (el manager usa UPDATE si el par ya está cacheado: flags en `peer+600/+601`) |
| +8..+13 | MAC del par | de `peer+88..93` |
| +16..+43 | IE HT (ID 45, len 26) | copiada de `peer+444..471`; presencia = byte +16 == 45 (y +17 == 26) |
| +44 | u8 **ampdu** | la familia lo escribe **a 1 sin condición** en `fillInCacheParams` |
| +45 | u8 VHT presente | si `peer+487 & 1`; IE VHT en +46..+59 (ID 191, len 12, cuerpo 12 B) |
| +60 | u8 HE | `IO80211AWDLPeer::fillInCacheParams` lo pone si el bit 0 del byte +1248 de los ivars del par está a 1 |
| +68..+72 | cabecera de secuencia de canales | count en +69 (de `peer+909`); el paquete exacto de las entradas difiere entre la vista de la familia (12 B por entrada desde +76, de `peer+920`) y la lectura Broadcom (3 B por entrada desde +83) `?` |

La capa Broadcom (`setPEER_CACHE_CONTROL` `0xffffff80017235ce`) compone el iovar `awdl_peer_op`
(122 B + secuencias de canal) a partir del struct: opcode (ADD/DELETE/UPDATE; el propio método
registra «Opcode %u %s […] ampdu %u ht %u vht %u he %u», string `0xffffff80017cd12a`, ins
`0xffffff8001723fca`, y «adding HT IE, ampdu %u», string `0xffffff80017cd196`, ins
`0xffffff8001724134`; el callback asíncrono `handleCacheControlAsyncCallBack` repite el «Opcode
%u …» sin el campo HE, string `0xffffff80017cc27d`, ins `0xffffff8001720870`), MAC, flag
**ampdu** en el byte +8
del buffer — `ampdu = (src[44] != 0) && (core ivar+14372 == 0)` —, flag HE en el bit 2, las IEs
HT/VHT/HE copiadas y la secuencia de canales del par como IE de vendor (tag `0xDD`, constante
`0x08F20800`, la misma del SNAP AWDL del datapath). Nombres e iovar: strings `awdl_peer_op`
(`0xffffff80017cd203`, `0xffffff80017cd2dd`), «Unable to send awdl_peer_op»
(`0xffffff80017cd2ea`), «WLC_SET_VAR: awdl_peer_op» (`0xffffff80017da673`). El core confirma la
operación con `AppleBCMWLANCore::completePeerCacheControl` (`0xffffff800165b8b6`), que republica
un mensaje 0x33 de 464 B con el opcode mapeado (ADD/UPDATE→1, DELETE→3).

El flag de la familia siempre sale a 1 (+44), así que **todo par AWDL cacheado se registra ante
la firmware como capaz de A-MPDU**. Los únicos interruptores de AMPDU AWDL son de depuración:
`AppleBCMWLANCore::forceAwdlAmpdu`/`setForceAwdlAmpdu` (`0xffffff80016b09fc`/`0xffffff80016b0a10`,
ivar +14376) y `forceDisableAwdlAmpdu`/`setForceDisableAwdlAmpdu`
(`0xffffff80016b0a24`/`0xffffff80016b0a38`, ivar +14372, el que el byte +8 del peer op consulta),
alimentados por los comandos de debug `a-force-ampdu`/`a-disable-ampdu`
(`0xffffff80017c43df`/`0xffffff80017c43cf`; `AppleBCMWLANCoreDbg::cmdAWDLForceAMPDU`
`0xffffff80016f46a4`, `cmdAWDLDisableAMPDU` `0xffffff80016f45fa`).

El resto de knobs AMPDU del core son configuración de la firmware, no sesiones del host:

| función | iovar | payload |
|---|---|---|
| `AppleBCMWLANCore::changeAggregationPerEDCATID(tid, on)` `0xffffff80016b049a` | `ampdu_tid` (2 B `{tid, on}`; también en `setupFirmware`, xrefs `0xffffff8001614e03`/`0xffffff8001614e6e`) y `ampdu_send_addba`/`ampdu_send_delba` (8 B con el BSSID de `IO80211BssManager::getCurrentBSSID`) | «Block ACK enabled/disabled for TID %d»; sin callers directos encontrados `?` (probablemente vía vtable, ruta infra) |
| `AppleBCMWLANCore::cfgAmpduWindowSize()` `0xffffff8001627628` (caller `linkChangeEvent` `0xffffff8001631884`) | `ampdu_tx_ba_window_cfg` / `ampdu_rx_ba_window_cfg` (strings `0xffffff8001789f17`/`0xffffff8001789fb4`), 40 B | «Setting AMPDU BA window size» |
| `getAMPDURxDensityFromHW` / `getAMPDURxFactorFromHW` `0xffffff8001627b38`/`0xffffff8001627b50`, `updateHTAndVHTCapBasedOnHWSupport` `0xffffff800161f348` | `ampdu_rx_density` / `ampdu_rx_factor` (xrefs `0xffffff800161f488`/`0xffffff800161f4a1`) | capacidades que van a las IEs HT propias |
| `disableRxAMSDUInAMPDU` `0xffffff80016a9c0e` | `rx_amsdu_in_ampdu` | |
| propiedad de plataforma | `wlan.vo.blockack` (string `0xffffff80017ae72b`) | config |

Que la firmware sostiene las sesiones lo muestran sus propios contadores que el host solo
publica: «RX AMPDU … rxaddbareq=%u txaddbaresp=%u rxbar=%u txdelba=%u rxdelba=%u» (string
`0xffffff8001777529`, `AppleBCMWLANLQM::updateLinkQualityMetrics` `0xffffff80015ce95d`) y
`printPeerRxAmpduStats`/`printPeerTxAmpduStats` (`0xffffff80015d6680`/`0xffffff80015d674e`):
la firmware recibe las ADDBA req, emite las respuestas, procesa BAR y hace DELBA.

## 5. Reordenado RX para `awdl0`

- En la familia no hay reordenador: la búsqueda global de `reorder` solo da TCP/`netem`, listas
  de candidatos WCL y telemetría LQM; el datapath Skywalk entrega a la `RxCompletionQueue` tal
  cual (`rxCompRingDrain` → `enqueuePackets`, [AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) §10).
- En Broadcom la ventana BA vive en la firmware: los contadores que la firmware reporta incluyen
  out-of-window/out-of-sequence/duplicate/stuck («AMPDU Rx Out-of-Window errors»
  `0xffffff800244e8c9`, «AMPDU Rx Out-of-Sequence errors» `0xffffff800244e8e7`,
  «AMPDU Rx Duplicate errors» `0xffffff800244e907`, «AMPDU Rx Stuck errors» `0xffffff800244e921`,
  y los `rxoow/rxoos/rxdup/rxstuck` de `0xffffff8001777529`); el host recibe ya en orden.
- Por tanto, para `awdl0` el orden lo garantiza quien recibe del aire. La capa RTL debe entregar
  los paquetes a la `RxCompletionQueue` en orden de secuencia: buffer de reordenado propio por
  (par, TID) del tamaño de la ventana negociada (64 en las capturas), con expiración, o entrega
  inmediata en orden si el hardware reordena. El árbol del driver trae un reordenador reutilizable:
  `struct recv_reorder_ctrl` (`Source/rtl8814au/include/rtw_recv.h:83`) y
  `recv_indicatepkts_in_order` (`Source/rtl8814au/core/rtw_recv.c:3266`).

**Conclusión (3):** la familia no reordena; quien entregue a la `RxCompletionQueue` debe hacerlo
en orden. Sin firmware, eso es la capa RTL.

## 6. Agregación TX

- El tirón de TX del manager (`IO80211VirtualInterface::dequeueTxPackets` →
  `IO80211AWDLPeerManager::dequeueTxPacket` `0xffffff800221675e`,
  [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §4) rellena un `TxPacketRequest`
  de 96 B (flags, contadores, 8 listas de mbuf): **no hay campos de A-MPDU, densidad ni ventana**;
  el manager no agrega ni limita la agregación.
- El máximo A-MPDU por par que existe en el sistema Broadcom viaja dentro de la IE HT del peer
  cache (byte de parámetros AMPDU en el cuerpo, log «HT: Info(%02x) AMPDU (%02x)…» en
  `fillInCacheParams` `0xffffff8002240ade`) y lo consume la firmware; el host no aplica límites.
- En AWDL no hay `WCL_LIMITED_AGGREGATION` ni `shouldDisableTxAMPDU` (§2: solo infra).

**Conclusión (4):** ninguna limitación del manager; la decisión de agregar en TX es del driver.
Mientras no haya acuerdo BA con el par, la capa RTL no debe enviar A-MPDU.

## 7. Qué debe implementar la capa RTL8814

| pieza | decisión | por qué |
|---|---|---|
| Responder ADDBA req (categoría 3, acción 0) | **Sí, la capa misma**. Interceptar la trama en el desvío de RX ([AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §4, antes de que ninguna otra parte la consuma) y emitir ADDBA response aceptando los parámetros del request (TID 0, ventana 64) | La familia no responde nunca (§2); en Broadcom lo hace la firmware (§4); winawdl midió 971 kB/s → 1,7 MB/s al aceptarla. El par reenvía cada trama hasta su límite de reintentos sin acuerdo |
| ADDBA response entrante y DELBA (acciones 1 y 2) | Procesar en la capa: confirmar/cerrar la sesión por (par, TID) | Mismo contrato de sesión que la firmware sostiene hoy (§4) |
| BAR | Procesar: avance de ventana y emisión del Block Ack (control 0x94); si el MAC del 8814AU puede hacer el BA por hardware, dejarlo al hardware `?` (RX_ACK §5 ya apunta la `?` equivalente del CAM) | Receptor del acuerdo: la firmware Broadcom es quien procesa `rxbar`/`txback` (§4) |
| Reordenado | Buffer por (par, TID), ventana 64, expiración, entrega en orden a `enqueuePackets` de la `RxCompletionQueue` | La familia no reordena (§5); sin orden, TCP sufre y el par rebasa la ventana |
| `setPEER_CACHE_CONTROL` | Hoy stub (`Source/AirLayer/AWDLInterface.h:376`, slot +0x1108): seguir respondiendo 0 y usar opcionalmente opcode ADD/DELETE/UPDATE + flag ampdu (+44) como interruptor de sesión BA por par, emulado desde la señal de userspace | Es exactamente el semántico Broadcom: registrar el par como A-MPDU-capable ante «la firmware» (aquí, la propia capa) |
| `AWDL_PEER_TRAFFIC_REGISTRATION` | Sin cambios funcionales: no toca BA (§3) | Su efecto High Thruput converge en el peer cache, no en sesiones BA |
| TX | Sin A-MPDU hasta tener acuerdo; si se agrega, respetar las capacidades HT que la capa anuncia (20 MHz: los límites de [AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) siguen aplicando) | Ningún límite viene del manager (§6); anunciar A-MPDU sin soportarlo degrada igual que en winawdl con 40 MHz |

Referencias del árbol del driver para las piezas anteriores: `RTW_WLAN_CATEGORY_BACK = 3`
(`Source/rtl8814au/include/ieee80211.h:1511`), tabla de categorías con `OnAction_back`
(`Source/rtl8814au/core/rtw_mlme_ext.c:76`), `OnAction_back` con
`RTW_WLAN_ACTION_ADDBA_REQ`/`_ADDBA_RESP`/`_DELBA` y `process_addba_req`
(`Source/rtl8814au/core/rtw_mlme_ext.c:3677` y siguientes), y el reordenador
`recv_reorder_ctrl`/`recv_indicatepkts_in_order` (§5).

## 8. No verificado en esta pasada

- Números de selector de `BLOCK_ACK`/`PEER_CACHE_CONTROL`/`AWDL_PEER_TRAFFIC_REGISTRATION`/
  `TRAFFIC_REGISTRATION`: los handlers están en la tabla que empieza en `0xffffff800247ce90`
  (get `PEER_CACHE_MAXIMUM_SIZE` en +131; sets de `PEER_CACHE_CONTROL`/`AWDL_PEER_TRAFFIC_REG`/
  `TRAFFIC_REG`/`WCL_LIMITED_AGG` en +730/+751/+927/+1080). Si las tablas modernas get/set del
  dispatcher fueran contiguas de 586 entradas cada una, los sets caerían en 145/166/342/495:
  hipótesis aritmética sin confirmar, ninguna base cierra simultáneamente con el rango del
  dispatcher (1–581/586) ni con la tabla de nombres `?`.
- Correspondencia exacta entre el paquete de entradas de la secuencia de canales del peer cache
  (12 B/entrada en la vista familia) y la lectura de 3 B/entrada de la capa Broadcom (§4) `?`.
- Si el MAC del RTL8814AU puede responder ADDBA y BAR sin intervención del host (§7) `?`.
- Si la firmware Broadcom contesta ADDBA para pares AWDL sin registro de traffic registration,
  o solo tras el peer op ADD `?` (el flag ampdu del cache sale siempre a 1, lo que sugiere lo
  primero tras el cacheo rutinario de `processUnicastAWSF`).
- La discrepancia 184/164 del request `AWDL_PEER_TRAFFIC_REGISTRATION` (§3) `?`.
