# Hooks por paquete en la TX de awdl0

Qué actualiza `IO80211VirtualInterface::logSkywalkTxReqPacket` por paquete, qué hace
`IO80211AWDLPeerManager::preQueuePacketCheckQuietMode`, si el gestor elimina o marca inactivo un par
sin TX hacia él y qué llamadas mínimas necesita un driver que transmite datos por su cuenta. Base:
copia de la colección de kernel 25G241. Lo no observado va con `?`.

## 1. `logSkywalkTxReqPacket` (`0xffffff800233218a`, ranura +0xB60)

La función toma el paquete, el `PacketSkywalkScratch`, la cabecera Ethernet, la clase WME y un booleano
(`a6`). Orden observado en la descompilación:

1. `result = *(this+0x120)` y `peerManager = *(result+0x28)`.
2. Búsqueda del par por la cabecera: `peer = (*(vtable+0x120))(peerManager, ether_header)` en la
   llamada indirecta de `0xffffff800233218a` (la ranura `+0x120` del peer manager recibe la cabecera,
   no sólo la DA). Si hay par, escribe `mach_absolute_time()` en `*(peer_ivars+184)`
   (`*(*(peer+0x18)+184)`), marca de último TX hacia ese par.
3. BPF: si `(*(*(this+0x128)+60) & 2) != 0`, llama a `bpfTapOutputPacket(this, 0x93, packet, 0, 0)`.
4. Estadísticas: `IO80211PeerManager::skywalkLogTxPacket(peerManager, packet, scratch, wme, peer, a6)`.
5. Si `a6 != 0`, escribe `mach_absolute_time()` en `peerManager_ivars+2280` y `+2288` (dos marcas
   consecutivas).
6. Si el rol de la interfaz (`*(*(this+0x120)+88)`) es 6, llama a
   `preQueuePacketCheckQuietMode(peerManager, ether_header)`.

Lectores de los campos: `+184` no se ha rastreado hasta un lector concreto `?`; las marcas
`+2280/+2288` no aparecen en el aging de pares `?`; las estadísticas de `skywalkLogTxPacket` alimentan
los reportes del gestor. El aging de pares que sí se ha verificado usa `peer_ivars+784` (último AF),
no `+184` (§3). El `0x93` de BPF es el tipo de tap de salida `?` nombre.

## 2. `preQueuePacketCheckQuietMode` (`0xffffff800221649e`)

Con `ivars = *(manager+0x20)`:

- Si `*(ivars+42568) != 0` (contador de servicios AirPlay) no hace nada.
- Si `*(ivars+42568) == 0`, lee `*(ivars+16096)`. Sale sin cambios cuando
  `(v & 0xC) == 0` o `(v & 8) != 0`; en los demás casos (`(v & 0xC) != 0` y `(v & 8) == 0`)
  considera que hay que salir del modo silencioso.
- Si la MAC destino es unicast (`(ether_dhost[0] & 1) == 0`), llama a `setDataMode(this, 3, 0)`.
- Registra `"Exiting quiet mode due to queued packet from network stack"` y llama a
  `exitQuietMode` (`0xffffff80021fad1c`).

Saltárselo deja el modo silencioso activo aunque la pila de red tenga un paquete encolado; el paquete
espera a que otra vía lo saque (timer, evento o el siguiente paquete si el driver sí llama). No se ha
medido un bloqueo permanente `?`; el riesgo es un retardo o una cola parada hasta la salida por otra
vía.

## 3. ¿Se elimina o marca inactivo un par sin TX?

No se ha observado un camino que elimine o marque inactivo un par **sólo** por falta de TX hacia él:

- `IO80211AWDLPeer::shouldRemovePeerFromSystem` (`0xffffff80021aa5e2`) mide el tiempo desde
  `peer_ivars+784` (último AF) y devuelve verdadero a partir de `0x186A1` ms = 100 001 ms (~100 s);
  no lee `+184`.
- `IO80211AWDLPeer::shouldRemovePeerFromNDP` (`0xffffff80021a9c50`) usa `+784` y `+56`; los umbrales
  concretos no se han extraído `?`.
- `ageoutRootNodeList` (`0xffffff80021ebd0e`) caduca entradas de la lista raíz a los `0x36EE81` ms =
  3 600 001 ms (~1 h).
- `ageoutPacketsWaitingForPPP` (`0xffffff80021aa7ca`) descarta paquetes en espera a los `0x7D1` ms =
  2 001 ms.
- `shouldRemovePeerFromSystem` usa `reachability` (`peer_ivars+120`) sólo en el log; la decisión es la
  marca de último AF.

Por tanto, la falta de TX no provoca por sí sola la retirada del par con los caminos verificados; el
par se retira por antigüedad de AF. Si `logSkywalkTxReqPacket` no se llama, `+184` queda obsoleto y
cualquier lector de esa marca (no identificado `?`) vería actividad vieja, pero el aging por AF sigue
funcionando.

## 4. Llamadas mínimas por paquete

La llamada que concentra el trabajo es la propia `logSkywalkTxReqPacket` de la interfaz virtual, ranura
`+0xB60` (VA `0xffffff800233218a`), con:

| Argumento | Valor |
|---|---|
| `this` | la `IO80211VirtualInterface` de `awdl0` (en esta capa, `RTLAwdInterface`) |
| paquete | el `IOSkywalkPacket`/`IO80211NetworkPacket` que se transmite |
| scratch | el `PacketSkywalkScratch` del paquete |
| cabecera | `ether_header *` con la DA real del frame |
| WME | la clase `apple80211_wme_ac` usada |
| booleano | `a6`: activa las marcas `+2280/+2288` y el tratamiento especial |

Esa llamada hace la búsqueda del par, la marca `+184`, el tap BPF, las estadísticas y, en rol 6, la
comprobación de modo silencioso. Si el driver no la llama, el tráfico puede seguir saliendo, pero el
gestor no ve la actividad TX de ese paquete: no actualiza `+184`, no cuenta las estadísticas y no sale
del modo silencioso por esa vía. La alternativa mínima es llamar a `skywalkLogTxPacket` y, en rol 6, a
`preQueuePacketCheckQuietMode`, pero se pierden la marca `+184` y el tap BPF.

No se ha observado una toma de lock dentro de `logSkywalkTxReqPacket`; `skywalkLogTxPacket` y
`preQueuePacketCheckQuietMode` se ejecutan en la ruta de TX y el gestor usa sus propias puertas `?`
(no se ha aislado la puerta exacta ni si el llamador debe mantenerla). La función es un método de
vtable, no una KPI: se alcanza por la ranura `+0xB60`; si el símbolo está exportado para enlazarlo
directamente desde un kext no se ha comprobado `?`.

## 5. Pendientes

- Lector de `peer_ivars+184` y de las marcas `+2280/+2288` `?`.
- Umbrales de `shouldRemovePeerFromNDP` `?`.
- Puerta/lock exigidos por `skywalkLogTxPacket` y `preQueuePacketCheckQuietMode` `?`.
- Exportación del símbolo para enlace directo `?`.
