# Presencia de pares AWDL: evento 47, ioctl 415 y la cola de espera PPP

Cómo el kernel avisa de la presencia y de la ausencia de un par AWDL, qué hace con los paquetes
recibidos de un par antes de que `wifip2pd` lo publique, y qué tiene que implementar el driver
RTL8814 para que esos paquetes lleguen a la pila. Cierra el punto abierto #17 de
[AWDL_INDICE.md](AWDL_INDICE.md) §8.2. Corrige además el paso 5 de la receta RX de
[AWDL_DATA_ENCAP.md](AWDL_DATA_ENCAP.md) §7. Base: KC 25G241 (macOS 26.7.1) en IDA y `wifip2pd` de
la misma build. Lo no comprobado va con `?`.

## 1. Evento 47 (`APPLE80211_M_PEER_PRESENCE`): solo ausencia

Lo emiten dos sitios, los dos con `postMessageSync(0x2F, buf, 0x44)` sobre la interfaz del manager
(`awdl0`). La búsqueda de `mov esi, 2Fh` antes de cada llamada a `postMessageSync` no encuentra
más:

| Emisor | Llamada | Contexto |
|---|---|---|
| `IO80211PeerManager::removePeer` `0xffffff8002254096` | `0xffffff8002254248` | justo antes, registra `"%s postPeerAbsence for %02X:…"` |
| `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` `0xffffff8002214cf2` | `0xffffff8002214d6d` | seguido de `pppDone(false)` en `0xffffff8002214d77` |

Payload de 68 B, puesto a cero salvo:

| Offset | Tamaño | Valor |
|---|---|---|
| `+0x00` | 6 | MAC del par (`peer+88..93` de las ivars comunes) |
| `+0x08` | u32 | `1` |

El `u32@8 = 1` significa **ausencia**: es el único valor que escribe el kernel y el log que lo
acompaña es `postPeerAbsence`. El daemon descarta el evento si ese campo no vale 1
([AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md), tabla de eventos). En el daemon el struct se llama
`apple80211_peer_presence_indication` (cadena `0x10045cfe0`).

La **presencia** no llega por el evento 47. El kernel no tiene ningún emisor de 47 con otro valor:
la llegada de un par se ve en los action frames (evento 48), y el daemon la gestiona en su tabla de
vecinos (`WiFiInterface.NeighborDiscoveryTable`, log `"Posted peer presence for %s on %s"` en
`sub_1002823D0`).

## 2. Ioctl 415 (`APPLE80211_IOC_POST_PRESENCE_DONE`): el daemon libera los paquetes

`setPOST_PRESENCE_DONE` (`0xffffff800228d968`) llama en `0xffffff800228d9b9` a
`IO80211AWDLPeerManager::setAwdlPostPeerPresenceDone` (`0xffffff800221c978`):

```
peer = findPeer(req + 4)                       // apple80211_awdl_ppp_done: u32 version, MAC @4
if (safeMetaCast(peer, IO80211AWDLPeer)) IO80211AWDLPeer::pppDone(peer, true)
return 0
```

`IO80211AWDLPeer::pppDone(bool)` (`0xffffff80021bfb08`) guarda el bool en el flag `+1758` de las
ivars AWDL del par (`this+0x20`):

- **`true`** (desde el 415): saca la cola PPP en lotes de 64 con `dequeuePPPWaitPacketFromHead`
  y entrega cada lote con el slot **+0xF30** de la interfaz `awdl0` (`sendPendingPacketsToStack`).
  Registra `"Posted Peer Presence for …, uploading %d packets to stack"`.
- **`false`**: no entrega nada. Registra `"Posted Peer Absence for …"`. Lo llaman
  `IO80211AWDLPeerManager::removePeer` (`0xffffff800221721c`, tras `flushPacketsWaitingForPPP` en
  `0xffffff80022177e4`, llamada en `0xffffff80022177ee`) y `cleanUpForPeerRemovedFromNDP`.

El 415 lo genera `wifip2pd`. En el binario hay exactamente dos inmediatos `0x19F`, y los dos están
en funciones que registran `"Post peer presence for %s"`:

| Función | Envío | Payload |
|---|---|---|
| `sub_1000CA0F0` (sin llamadas directas: método de protocolo) | `0x1000ca412` → `sub_1000CABE0(415, …)` | `apple80211_awdl_ppp_done` con la MAC del par |
| `sub_1000D6000` (vía el thunk `sub_1000CA0C0`, desde la tabla de testigos `0x100517bf8`) | `0x1000d6229` → `sub_10004A490(415, …)`, solo si el byte `+32` del objeto vale 1 | `{u32 version = 1, MAC del par}` |

El struct `apple80211_awdl_ppp_done` es de 12 B ([AWDL_STRUCTS.md](AWDL_STRUCTS.md)). Ese método de
publicación lo llama la tabla de vecinos (`sub_1002823D0`, log `"Posted peer presence for %s on
%s"`). A esta, a su vez, llegan varias rutas Swift (`sub_10028F280`, `sub_100305990`,
`sub_10040B650`, `sub_10041C470`). Qué evento del par dispara cada ruta no está trazado `?`.

## 3. La cola de espera PPP (*post peer presence*)

Ruta RX de un paquete de datos de `awdl0`:

1. **`IO80211VirtualInterface::inputPacket`** (`0xffffff8002333cac`, slot **+0xB50**). Para el rol
   6 busca el par por la SA (`ether_header+6`). Si no hay par, descarta y suma
   `incrementNumInputPacketsDropped`. Los multicast `33:33:00:00:…`, `33:33:80:00:00:FB` y
   `01:00:5E:00:…` tienen comprobaciones propias del par y del manager, y se descartan si no las
   pasan. El resto pasa a
2. `IO80211PeerManager::skywalkInputPacket` (`0xffffff800225d554`), que llama al slot +0x168 del
   par:
3. **`IO80211AWDLPeer::skywalkInputPacket`** (`0xffffff80021bf9a4`). Si el flag `+1758` está a 0
   (aún no hay 415 para ese par), llama a `peerDiscoveredOnBothSide` y encola el paquete con
   `enqueuePPPWaitPacketAtTail` (`0xffffff80021bf9fa`). Después pone `*consumed = 1` y devuelve 1.

La cola es una lista en `+1760` (cabeza) y `+1768` (cola) de las ivars AWDL del par. Cada entrada es
un descriptor de `allocZeroAwdlPacketDescriptor` con el paquete en `+8` y el `mach_absolute_time`
en `+16`. Hay tres salidas:

| Salida | Función | Slot de la interfaz |
|---|---|---|
| 415 recibido | `pppDone(true)` | +0xF30 `sendPendingPacketsToStack(pkts, n)` |
| Paquete con más de 2000 ms en cola | `ageoutPacketsWaitingForPPP` (`0xffffff80021aa7ca`), desde `monitorPeer` (`0xffffff80021a9294`, llamada en `0xffffff80021a9711`) | +0xF38 `freePendingPackets(pkts, n)` |
| Par eliminado | `flushPacketsWaitingForPPP` (`0xffffff80021bfe44`) | +0xF38 |

## 4. Lo que la familia deja al driver

En la clase base los dos slots solo registran un error:

| Slot | `IO80211VirtualInterface` | `AppleBCMWLANProximityInterface` |
|---|---|---|
| +0xB50 `inputPacket` | implementado (§3) | hereda el de la familia |
| +0xF30 `sendPendingPacketsToStack` | `0xffffff8002334e68`: solo `IOLog("… Not Implemented")` | `0xffffff800172a716` |
| +0xF38 `freePendingPackets` | `0xffffff8002334e88`: solo `IOLog("… Not Implemented")` | `0xffffff800172a812` |

Broadcom usa la `RxCompletionQueue` de la interfaz (`ivars+1016`):

- `sendPendingPacketsToStack`: `stagePacket(pkt, false, false)` (slot +0x340) para cada paquete,
  luego `requestEnqueue(0, 0)` (slot +0x298) y, si devuelve distinto de 0, `flushPackets` (slot
  +0x330).
- `freePendingPackets`: `freePendingPackets(pkts, n)` de la cola (slot +0x348).
- Sin cola, registra `"FAIL : Invalid Rx Completion Queue available"`.

Implementación de la cola en cada clase (celdas leídas de cada vtable):

| Celda | `AppleBCMWLANPCIeSkywalkRxCompletionQueue` | `AppleBCMWLANSkywalkRxCompletionQueue` |
|---|---|---|
| +0x298 | `requestEnqueue` (`0xffffff8001546d18`) | `IOSkywalkRxCompletionQueue::requestEnqueue` (`0xffffff8002b2d6d8`), heredado |
| +0x330 | `flushPackets` (`0xffffff80015466fe`) | `flushPackets` (`0xffffff80015d83a2`) |
| +0x340 | `stagePacket` (`0xffffff8001546cc8`) | `stagePacket` (`0xffffff80015d805e`) |
| +0x348 | `freePendingPackets` (`0xffffff8001546852`) | `freePendingPackets` (`0xffffff80015d8328`): solo log `"Not Implemented"` |

El `freePendingPackets` de la cola PCIe recorre el array, salta las entradas nulas, limpia el bit
`0x80` de un byte privado de Broadcom (`*(pkt+0x78)+0x90`) y devuelve cada paquete al pool con
`IOSkywalkPacketBufferPool::deallocatePacket` (puntero `0xffffff80017fa328` →
`0xffffff8002b2c0de`; pool en `*(*(cola+0x140)+0x18)`, llamada en `0xffffff800154689d`). Para
`awdl0` del RTL8814 el equivalente es devolver cada paquete del array a su pool RX con
`deallocatePacket`, símbolo que `Source/AirLayer/AWDLInterface.cpp` ya importa.

`IO80211AWDLPeerManager` tiene sus propios `sendPendingPacketsToStack` (`0xffffff80022389e6`) y
`freePendingPackets` (`0xffffff8002238c40`), que solo reenvían a las celdas +0xF30/+0xF38 de la
interfaz (`*(ivars+1376)`); no tienen xrefs de código.

Consecuencias para la interfaz `awdl0` del RTL8814:

1. **Llamar a `inputPacket`** (+0xB50) con cada paquete RX antes de entregarlo, como Broadcom en
   `0xffffff800154a65c` ([AWDL_DATA_ENCAP.md](AWDL_DATA_ENCAP.md) §5). Retorno 0: descartar.
   `consumed = 1`: el paquete es de la familia y no se toca. Otro valor: entregarlo a la cola.
   Encolar sin pasar por `inputPacket` salta el filtro de pares, la retención PPP y la
   contabilidad del par.
2. **Sobrescribir +0xF30 y +0xF38.** Si la interfaz hereda los de `IO80211VirtualInterface`, los
   paquetes retenidos no llegan nunca a la pila. Además no se liberan, porque las versiones base
   no hacen nada con el array `?` (pérdida del pool). Con la cola RX propia:
   `stagePacket` + `requestEnqueue`, o `enqueuePackets` por lote como hace la capa STA, en +0xF30;
   devolver al pool en +0xF38.
3. Los dos slots los llama el `PeerManager` desde su contexto (`monitorPeer`, el ioctl 415),
   no desde el hilo RX del driver. El cerrojo con el que se entra no está comprobado `?`.

## 5. Cuándo el kernel da al par por enrutable

`IO80211AWDLPeer::setPeerRoutable` (`0xffffff80021aad6c`; lo llaman `monitorPeerRssi`,
`shouldStopAFProcessing` y `peerDiscoveredOnBothSide`) es la que registra `postPeerPresence`. Pone
`+120 = 1` en las ivars AWDL y guarda la marca de tiempo en `+200` de las comunes. Antes exige:

- **Par visto en 5 GHz o con RSSI suficiente.** Si `isDualBandPeerDiscoveredOn5GHz` es falso, el
  RSSI del par (`+1188`, o `+1180 − 10`; −100 si los dos valen 0) tiene que ser mayor o igual que el
  umbral en `PM+10276`. Si no, registra `"Not Routable … outside bubble"`. Los valores los pone
  `determineAndSetEdgeRssiThreshold` (§5.1).
- **Fin de la guarda de publicación** (`waitForPostingPeer` `0xffffff80021b801c`). Si
  `PM+65193` bit 0 o `+1756` del par están activos, espera `ndpPostingGuardTimemSec` (`PM+65216`;
  128 ms si vale 0) desde el primer AF (`+736`), salvo que `+1757` esté activo.
- **`LLAValid` y `AWDLOn`.** Si falta alguno, registra
  `"Cannot post peer presence … (LLAValid %d AWDLOn %d)"`. Por el orden de los argumentos del log:
  - `LLAValid` es `PM+65308` bit 0. Lo escriben `initParamsToDefaults` (`0xffffff80021c818f`),
    `interfaceStateChange` (1 en `0xffffff80021d9b39`, 0 en `0xffffff80021d9b44`) y
    `monitorAWDLState` (`0xffffff80021ed932`). Qué estado de la interfaz lo activa no está trazado
    `?`.
  - `AWDLOn` es `PM+16096` bit 1. `reportDataPathEvents` registra ese mismo bit como `isAwdlOn`.

### 5.1 Umbrales de RSSI

`IO80211AWDLPeerManager::determineAndSetEdgeRssiThreshold(uint)` (`0xffffff80021e0f5e`, escrituras
en `0xffffff80021e0fc1`–`0xffffff80021e1107`) elige entre dos juegos:

| Campo | Normal | Ampliado |
|---|---:|---:|
| `PM+10268` (borde del firmware; descarte de AF en `actionFrameProcessInput`) | −70 | −78 |
| `PM+10272` (borde del host) | −65 | −78 |
| `PM+10276` (enrutable solo en 2,4 GHz) | −45 | −58 |

El juego ampliado se usa si el argumento (número de servicios AirPlay) no es 0, si `PM+16156` bit 0
está activo o si `increaseRssiThresholdForSingleStreamDevice` lo pide. Cuando cambia el borde, el
kernel manda el valor del firmware al driver con el **ioctl 135**
(`APPLE80211_IOC_AWDL_ELECTION_RSSI_THRESHOLDS`, `apple80211RouteInternal(0x802869C8, 135, …)`).
Es un buffer de 20 B con ceros en los primeros 16 bytes y el umbral en `+16`.

Para el RTL8814 la primera condición importa: con la radio fija en un canal de 5 GHz el par se ve
en 5 GHz y el umbral de 2,4 GHz no se aplica. Si la radio se queda en el canal 6, el par tiene que
llegar con −45 dBm o más (−58 en el juego ampliado) para pasar a enrutable.

## 6. Pendiente

- ~~Qué evento del par dispara el ioctl 415 en `wifip2pd`~~: cerrado por [AWDL_WIFIP2PD_PRESENCE_TRIGGER.md](AWDL_WIFIP2PD_PRESENCE_TRIGGER.md) (primer action frame de cada par, más datapath y registro de tráfico).
- Qué cambio de estado de la interfaz activa `LLAValid` (`PM+65308`) y quién activa el bit 1 de
  `PM+16096` (hay escrituras completas del campo en `enableAWDL`, `setAwdlSyncState`, `reset` y
  otras).
- Cerrojo con el que se llaman +0xF30/+0xF38 y si el array que reciben se libera después
  (`IOFreeData` lo libera `pppDone`; los paquetes no).
