# Etapa 3: emisores de eventos, ruta desde el kext y estado que consume `wifip2pd`

Cierra los `?` de la etapa 3 de `AWDL_ETAPAS_2_A_4.md` §2: quién emite cada evento AWDL, por qué API
llega al daemon, qué método tiene que llamar `RTLAwdInterface` y qué estado lee `wifip2pd` para
considerarse sincronizado y anunciar un servicio. Base: `BootKernelExtensions.kc` (md5
`f489ff8be80e6013a1bf47561dcaef45`) y `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`),
IDA 9.4 headless sobre copias de las IDB. Direcciones del kernel en VA (`0xffffff800…`), del daemon en
VA del binario (`0x100…`). Las funciones se citan por cabeza comprobada (`ida_funcs.get_func`); los
sitios de llamada se citan aparte. Lo que no fija el desensamblado va como `?`.

## 1. Resultado

1. **El evento 47 no tiene emisor de alta.** Los dos únicos sitios del KC que envían el id 47 son
   `IO80211PeerManager::removePeer` (`0xffffff8002254096`, sitio `0xffffff8002254248`) y
   `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`, sitio
   `0xffffff8002214d6d`), ambos por `postMessageSync` directa, ambos con el log `postPeerAbsence` y el
   mismo payload de 68 B con `u32@8 = 1`. No hay emisor en el alta: la presencia de un peer llega al
   daemon por el evento 48 (frames AWDL); la tabla de peers es estado interno del kernel.
2. **La ruta desde el kext es una sola llamada virtual**, `postMessage(id, dato, longitud, false)` en el
   slot `0xB18` (`IO80211VirtualInterface::postMessage`, `0xffffff8002333192`). Esa llamada entra en
   `reportDataPathEvents` (slot `0xB20`), pasa por la puerta del controlador
   (`IONetworkController::executeCommand`, `0xffffff80026f51e0`) y termina en
   `postMessageSync` → `postMessageIOUC` → `IO80211IOUCEventPipe::writePipe`, que es donde se aplica la
   suscripción del cliente. No hay comprobación de interfaz arriba/abajo en ese camino.
3. **`wifip2pd` no lee el estado AWDL por `apple80211` GET.** Solo hace `RawGet` 12 y 97 y `RawSet` 94 y
   293; el estado (sync, maestro, secuencia de canales, peers) lo construye con los eventos 46/195/47/48
   y lo publica por XPC como `WiFiP2PAWDLState`. El anuncio de un servicio (AirDrop) lo dispara el
   registro Bonjour, no una comprobación de sincronización. Para la etapa 3 el mínimo es: registro de
   monitoreo correcto (pipe + máscara), 46 y 195 —que con el manager reutilizado salen solos— y aceptar
   los ioctls 134/129; 41/42/43 no los consume ningún cliente del KC ni `wifip2pd`.

## 2. Punto 1 — emisores por id y API

### 2.1 Matriz de emisores

| id | emisor (cabeza) | sitio | API | payload |
|---:|---|---|---|---|
| 41 `AWDL_AVAILABILITY_WINDOW_START` | `AppleBCMWLANProximityInterface::handleAvailabilityWindowStart` `0xffffff8001718756` | `0xffffff80017188d1` | `IO80211Controller::postMessage` `0xffffff8002299fae` | ~24 B derivados del evento de firmware |
| 42 `…_EXTENSION_START` | `…::handleAvailabilityWindowExtension` `0xffffff8001718942` | `0xffffff80017189ac` | idem | 8 B |
| 43 `…_EXTENSIONS_END` | `…::handleAvailabilityWindowEnd` `0xffffff80017184b6` | `0xffffff8001718649` | idem | ≥16 B |
| 46 (firmware) | `…::handleMasterEvent` `0xffffff80017189e4`, `handleSlaveEvent` `0xffffff8001718ad6`, `handleNonElectionMasterEvent` `0xffffff8001718bd4`, `bringdownLink` `0xffffff8001719cec` | `0xffffff8001718a7e`, `0xffffff8001718b6c`, `0xffffff8001718c6e`, `0xffffff8001719d66` | idem | 16 B: `u32@0` = 1 maestro / 2 esclavo / 3 maestro sin elección / 0 caído |
| 46 (familia) | `IO80211VirtualInterface::postSyncStateChanged` `0xffffff8002332054` | `0xffffff8002332073` (`mov esi, 2Eh`) | `postMessageSync` directa | 0 B |
| 46 (manager) | `IO80211AWDLPeerManager::postSyncStateChanged` `0xffffff80021e58b4` | `0xffffff80021e594d` (llama al helper anterior) | llamada directa | 0 B |
| 47 `PEER_PRESENCE` | `IO80211PeerManager::removePeer` `0xffffff8002254096` | `0xffffff8002254248` | `postMessageSync` directa | 68 B: MAC `@0..5`, `u32@8 = 1`, resto 0 |
| 47 | `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` `0xffffff8002214cf2` | `0xffffff8002214d6d` | idem | idéntico |
| 48 `AWDL_ACTION_FRAME_RX` | `IO80211AWDLPeerManager::postAFToP2pd` `0xffffff800220fc2c` | `0xffffff800220fce2` | virtual `+0xB18` | cabecera `apple80211_act_frm_hdr` de 72 B + frame |
| 195 `AWDL_CHAN_SEQ_CHANGED` | `IO80211AWDLPeerManager::setChannelSequenceUnconditional` `0xffffff80021cb69a` | `0xffffff80021cc29b` | virtual `+0xB18` | **400 B** (buffer `IOMallocZeroData(400)`, `u32@0 = 1`, lista de secuencia en `+12/+20`) |
| 65, 68, 142, 149, 235, 243 | helpers `post*` de `IO80211VirtualInterface` y `IO80211AWDLPeerManager` | ver `AWDL_EVENTS.md` §3.2–§3.3 | `postMessageSync` o virtual `+0xB18` | longitudes fijas ya documentadas |

Los ids 41, 42 y 43 no tienen otro emisor: el kext Broadcom los construye en su dispatcher de eventos
de firmware y los entrega por `IO80211Controller::postMessage`, que es una ruta de controlador (glue),
no la ruta de interfaz. En un Mac sin ese kext no existen.

### 2.2 Barrido del 47

Método: (a) todos los `call` directos a `IO80211SkywalkInterface::postMessageSync`
(`0xffffff80022f8abc`) del KC, con lectura de los 32 B previos para el inmediato del registro de
argumento — 28 sitios, exactamente dos con `esi = 0x2F`; (b) todos los `call` virtuales a `+0B18h`,
`+0B20h` y `+298h` con el id en los 40 B previos — dos con un id de la lista AWDL (48 y 195) y ninguno
con 47; (c) funciones que contienen a la vez un `mov esi/edx/r8d, 2Fh` y una llamada virtual de esas —
ninguna del lado AWDL (`IO80211AWDLPeerManager::monitorAWDLState` `0xffffff80021ed724`, 0x1945 B, entra en la
lista por un inmediato 0x2F que no es un id de evento y no llama a `postMessage` en todo su cuerpo).

Conclusión: el 47 es **ausencia**. El `u32@8 = 1` es una constante en los dos emisores y no distingue
presencia de ausencia; el nombre y el log de la función que lo emite (`postPeerAbsence`) sí. La
presencia en el alta viaja como 48 (`postAFToP2pd`, que emite cada action frame que el manager acepta y
entrega al daemon); la tabla de peers del manager es estado interno del kernel y no se publica tal
cual. `wifip2pd` mantiene su tabla con los tags 1 y 8 del switch (`AWDL_EVENTS.md` §4.2). El byte `@68` que lee la puerta del daemon queda fuera
del payload de 68 B; el buffer de eventos del daemon es de 0x100 B y se reutiliza
(`sub_100041FE0`), así que ese byte no lo escribe el kernel — semántica `?`.

### 2.3 Qué API usa cada emisor

| API | Quién | Efecto |
|---|---|---|
| `IO80211VirtualInterface::postMessage` (slot `0xB18`) | manager (`postAFToP2pd`, `setChannelSequenceUnconditional`) | ruta con puerta de controlador, flag 0 → `postMessageSync` |
| `IO80211SkywalkInterface::postMessageSync` (directa) | 46 helper, 47 (dos sitios), 65/142/149/243 | ruta sin puerta; el llamador ya está serializado por su propio gate |
| `IO80211Controller::postMessage` | kext Broadcom (41/42/43 y 46 de firmware) | glue del controlador → misma `postMessageSync` sobre la interfaz del manager |
| `Apple80211StartMonitoringEvent` / `RawP2PEventMonitoringInit` | `wifip2pd` | crea el pipe (`extEventMonitorInit` `0xffffff800235256e`) y fija la suscripción (`extSubscribeEvent` `0xffffff8002352486`); sin esto `writePipe` descarta |

## 3. Punto 2 — ruta desde `RTLAwdInterface` hasta el daemon

### 3.1 Cadena, con instrucciones

| # | Paso | Cabeza | Instrucción que enlaza |
|---:|---|---|---|
| 1 | `postMessage(id, dato, longitud, false)` | `IO80211VirtualInterface::postMessage` `0xffffff8002333192` (slot `0xB18`; `__ZTV30AppleBCMWLANProximityInterface` `0xffffff80017f5b68` + `0x10` + `0xB18`) | `0xffffff800233334b` `call [rax+0B20h]`, con `r8 = 0` |
| 2 | `reportDataPathEvents(id, dato, longitud, 0)` | `IO80211SkywalkInterface::reportDataPathEvents` `0xffffff80022f8dc8` (slot `0xB20`) | `0xffffff80022f8e07` `call [rax+0BA8h]` (getController); `0xffffff80022f8e2a` `call [r10+850h]` |
| 3 | puerta del controlador | `IONetworkController::executeCommand` `0xffffff80026f51e0` (vtable del controlador `0xffffff8002461148` + `0x10` + `0x850`) | recibe el callback `reportDataPathEventsGated` y cinco punteros |
| 4 | callback con la puerta tomada | `IO80211SkywalkInterface::reportDataPathEventsGated` `0xffffff80022f8d1a` | `0xffffff80022f8d98` `call [rax+298h]` (filtro en `ivars+0x28`); `0xffffff80022f8db2` `call postMessageInternal` |
| 5 | reparto WCL/síncrono | `IO80211SkywalkInterface::postMessageInternal` `0xffffff80022f8600` | `0xffffff80022f861e` `jmp routeEventToWcl` si flag 1 y glue; `0xffffff80022f862a` `call postMessageSync` si no |
| 6 | entrega al pipe | `IO80211SkywalkInterface::postMessageSync` `0xffffff80022f8abc` | `0xffffff80022f8aed` `call [rax+0DE0h]` (`isIOUCPipeOpened` `0xffffff80022f6678`); `0xffffff80022f8b15` `call [r9+0DD8h]` (`postMessageIOUCE` `0xffffff80022f66a2`) |
| 7 | reparto por cliente | `IO80211SkywalkInterface::postMessageIOUCE` `0xffffff80022f66a2` | recorre los pipes del BSDName y llama `writePipe` |
| 8 | filtro y copia | `IO80211IOUCEventPipe::writePipe` `0xffffff80022a7e7c` | `+368` bit 0, `+416` bypass, bitmap `+417`, longitud ≤ 0x1000, `reserveRingEntry` y `notifyUser` |

Con el flag 0 (el que pasa siempre el slot `0xB18`) el paso 5 va directo a `postMessageSync`: la ruta
WCL solo se toma si el llamador invoca `IO80211SkywalkInterface::postMessage` (`0xffffff80022f85f6`)
con `true`, algo que ningún emisor AWDL hace.

No confundir los dos métodos homónimos: `IO80211AWDLPeerManager::reportDataPathEvents(int, void*,
ulong)` (`0xffffff80021dd8f4`) es el consumidor **del kernel** (recibe los ids reenviados por
`routeToP2PInterface` y alimenta `monitorTimeline` `0xffffff80021e11ca`), mientras que
`IO80211SkywalkInterface::reportDataPathEvents(int, void*, ulong, bool)` (`0xffffff80022f8dc8`, slot
`0xB20`) es el emisor hacia **userspace** que usa la cadena de §3.1.

### 3.2 Condiciones que se prueban (y las que no)

| Condición | Dónde | Efecto si falla |
|---|---|---|
| rol 6 (`*(ivars+88) == 6`) | solo en `postSyncStateChanged` `0xffffff8002332054` | no emite 46 (no-op) |
| filtro en `ivars+0x28` (método `+0x298`, clase `?`) | `0xffffff80022f8d98` | si existe y devuelve 0, no entrega |
| pipe abierto (`isIOUCPipeOpened`) | `0xffffff80022f8aed` | `postMessageSync` retorna 0 sin entregar |
| al menos un cliente de eventos para la interfaz | `postMessageIOUCE` | retorna `0xE00002C7` |
| cliente suscrito (`+368` bit 0) | `writePipe` | `0xE00002D8`, log `Client not subscribe for receiving event yet` |
| bit del id en el bitmap (`+417`, bit `(id-1)&7`) | `writePipe` | `0xE00002E2` si el bit está a 0 y no hay bypass `+416` |
| longitud ≤ 0x1000 | `writePipe` | `0xE00002E1` |
| **no** se comprueba | — | estado administrativo de la interfaz, rango del id, contenido del payload |

Los dos puntos que pueden descartar sin error visible hacia el llamador son el filtro `ivars+0x28` y la
ausencia de pipe: si el daemon no completó el registro de monitoreo (o el user client no creó el pipe),
`postMessage` devuelve éxito y no llega nada.

### 3.3 Qué tiene que hacer el kext

- Llamar `postMessage(id, dato, longitud, false)` por el slot `0xB18` de la interfaz. Es una llamada
  virtual: no necesita símbolo exportado, solo la definición de la clase.
- Hacerlo desde contexto de workloop o desde una acción ya encolada en el gate del controlador: el paso
  3 toma `executeCommand`, que espera la puerta del controlador. Llamarlo desde el handler de RX/USB en
  contexto de interrupción puede bloquear; `?` el comportamiento exacto de `executeCommand` en ese
  contexto (no se probó en hardware).
- Para el 46, la vía corta es `postSyncStateChanged()` (`0xffffff8002332054`): comprueba rol 6, emite
  `0x2E` con longitud 0 y avisa al `IO80211RangingManager`. Si se prefiere no depender del helper,
  `postMessage(46, NULL, 0, false)` produce el mismo evento en el pipe, sin el aviso al ranging.
- Para el 195 y el 48 no hace falta código propio si se reutiliza el manager: `setChannelSequenceUnconditional`
  y `postAFToP2pd` ya emiten por el slot `0xB18`. El 47 también es del manager (ausencia).
- Para el 41/42/43 no hay consumidor en el KC ni en `wifip2pd`; solo una llamada directa del driver al
  `reportDataPathEvents` del manager alimentaría `monitorTimeline` (§5).

## 4. Punto 3 — qué estado lee `wifip2pd` y qué dispara el anuncio

### 4.1 Superficie apple80211 del daemon

| Petición | Tipo / longitud | Uso |
|---|---|---|
| `RawGet` 12 `CARD_CAPABILITIES` | 12 / 21 | puerta previa a crear la interfaz |
| `RawSet` 94 `VIRTUAL_IF_CREATE` | 94 / 32 | crea `awdl0` (rol 6 en `+0x0C`) |
| `RawGet` 97 `VIRTUAL_IF_PARENT` | 97 / longitud del nombre | nombre del padre |
| `RawSet` 293 `COMPANION_SKYWALK_IF_CREATE` | 293 / 24 | crea `llw0` |
| ioctl `'i'` 130/131 | `0xC0806982` / `0xC1206983` | secuencia de canales; el kernel de 26.7.1 los rechaza (`AWDL_WIFIP2PD_POST_CREATE.md` §2.5) |
| monitoreo | `RawP2PEventMonitoringInit` (rol 1) | crea el pipe y la suscripción |

No hay `RawGet` de `AWDL_SYNC_STATE`, `AWDL_MASTER_CHANNEL`, `AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`
ni de contador de peers. Esos valores no se piden al kext: llegan por eventos. Si otro cliente los pide
por los puentes `apple80211get*` (slots `0x1028`, `0x1020`, `0x1010`, `0xFF8`), el valor coherente es el
que el manager tenga programado en ese momento; quién los pide además de `wifip2pd` no se verificó (`?`).

### 4.2 El estado que publica el daemon

| Pieza | Dirección | Qué hace |
|---|---|---|
| registro de cliente de estado | `sub_10040F3A0` | guarda el cliente XPC y, si lo pide, envía el estado inicial y la secuencia |
| constructor del estado | `sub_10040FAC0` | `WiFiP2PAWDLState initWithInterfaceName:supportsSoloMode:supportsDataTransfer:channelSequence:isEnabled:substate:macAddress:peerMasterChannel:peerPrimaryPreferredChannel:peerSecondaryPreferredChannel:` |
| reenvío XPC | `sub_100443FB0` | `updatedAWDLState:` con el objeto del constructor |
| estado de sync | evento 46 (tag 11 del switch `sub_1000388D0`) | fuente del `substate` que consume el constructor (enlace campo a campo `?`) |
| secuencia de canales | evento 195 (tag 12) y `channelSequenceChangedEvent:` | array `WiFiChannel` del objeto |

Los campos del objeto salen de estado interno del daemon (`substate` por getter de testigo, `isEnabled`
de `self+760`, la secuencia de su store), no de una lectura al kernel. Es un espejo: la elección y la
secuencia reales las lleva el `IO80211AWDLPeerManager` del kernel.

### 4.3 El anuncio del servicio

`sub_10001D2F0` (arranque del motor Bonjour) registra `Starting advertise for %s (browses: %ld,
advertises: %ld)`. En el cuerpo descompilado, antes de ese log la única salida temprana identificada es
el conjunto de servicios vacío; no aparece comprobación de sync, maestro ni secuencia (el resto de
ramas del cuerpo no se desglosó, `?`). El anuncio de AirDrop lo dispara el registro del servicio
(Bonjour/XPC), no un evento de sincronización. Lo que sí depende del kernel es que el manager esté
elegido y transmitiendo MIs para que el servicio sea visible en el aire.

Con el manager reutilizado, la secuencia es: el driver acepta el ioctl 134 y refleja el rol en la radio;
el manager elige, programa la secuencia (emite 195) y cambia de estado (emite 46); `wifip2pd` actualiza
su espejo y sigue anunciando. Sin elección no hay MI, aunque el daemon ya esté anunciando en su lado.

### 4.4 Mínimos de la etapa 3

| Requisito | ¿Lo aporta el manager reutilizado? | Nota |
|---|---|---|
| registro de monitoreo con pipe y suscripción | no | user client / `extEventMonitorInit` y `extSubscribeEvent` |
| evento 46 tras el 134/elección | sí (`postSyncStateChanged`) | el driver solo refleja el rol en la radio |
| evento 195 | sí (`setChannelSequenceUnconditional`) | payload de 400 B |
| eventos 48 y 47 | sí (`postAFToP2pd`, `removePeer`) | el 47 es ausencia |
| eventos 41/42/43 | no aplica | sin consumidor en 26.7.1 |
| GET `AWDL_*` | no aplica | `wifip2pd` no los pide |
| ioctl 129/134 y `actionFrameInput` | no | obligación del driver |

## 5. Correcciones a documentos previos

| Documento | Afirmación previa | Verificado |
|---|---|---|
| `AWDL_EVENTS.md` §1.1 y §3 | el slot `+0xB18` es `IO80211SkywalkInterface::postMessage` | es `IO80211VirtualInterface::postMessage` `0xffffff8002333192`; el `+0xB20` es `reportDataPathEvents`, no otro `postMessage` |
| `AWDL_EVENTS.md` §3.2 | payload del 195 `?` | 400 B, `u32@0 = 1`, lista en `+12/+20` |
| `AWDL_EVENTS.md` §3.2 | emisor del 47 en el alta `?` | no existe; los dos emisores son de baja |
| `AWDL_ETAPAS_2_A_4.md` §2.4 punto 2 | el driver debe publicar 41/42/43 y `monitorTimeline` los valida | `monitorTimeline` solo se alcanza desde la rama del 41 de `IO80211AWDLPeerManager::reportDataPathEvents`; su único llamador dentro del KC es `IO80211InfraInterface::routeToP2PInterface` `0xffffff800236364e`, cuya lista blanca no incluye 41–43, y `wifip2pd` descarta el 41. Solo una llamada directa del driver a ese método del manager lo alimentaría; ningún cliente del KC lo hace |
| `AWDL_ETAPAS_2_A_4.md` §2.4 punto 6 | el 46 se emite «tras el 134/elección» | correcto, y el camino corto es `postSyncStateChanged` `0xffffff8002332054` (rol 6, longitud 0) |

## 6. Método y límites

- IDA 9.4 headless sobre copias en `/tmp/ida_awdl` de la IDB del KC y de
  `Build/ida_awdl/wifip2pd.i64`; las IDB de origen no se abrieron en escritura.
- Cabezas comprobadas con `ida_funcs.get_func` (cabeza = dirección citada) para todas las funciones de
  las tablas §2.1 y §3.1; los sitios de llamada se leyeron del desensamblado y del pseudocódigo.
- El barrido del 47 cubre llamadas directas a `postMessageSync`, llamadas virtuales a `+0B18/+0B20/+298`
  y búsqueda del inmediato `0x2F` en las funciones candidatas. No cubre un emisor que cargue el id 47
  desde una variable fuera de esos patrones; no se encontró ninguno en el rango AWDL.
- `?` que quedan: semántica exacta del `u32@8` del payload del 47 y del byte `@68` que lee el daemon;
  comportamiento de `executeCommand` si se le llama en contexto de interrupción; consumo de los GET
  `AWDL_*` por clientes distintos de `wifip2pd` (airportd, wifivelocityd) — no verificados aquí; y todo
  lo que depende de una elección real con un peer Apple (frames, secuencia, MIs).
