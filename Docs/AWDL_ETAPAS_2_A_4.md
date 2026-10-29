# Plan de las etapas 2 a 4 de AWDL en RTL8814AirLayer

Plan de trabajo para las etapas que siguen a la etapa 1 (`awdl0` creada y aceptada por `wifip2pd`,
sin pares ni datapath real). Todo el contenido se deriva exclusivamente de los documentos
`Docs/AWDL_*.md` ya verificados sobre el kernelcache 25G241 (md5 `f489ff8be80e6013a1bf47561dcaef45`)
y sobre `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`); nada de este documento proviene
de una verificación nueva. Todo lo que los documentos fuente no cierran va marcado `?`.

Numeración: este documento reordena las etapas de `AWDL_PROTOTYPE_DESIGN.md` §4–§6. Allí la etapa 2
era la sincronización/elección y la etapa 3 los action frames; aquí la **etapa 2** es el datapath
real de transmisión y recepción (§6 del prototipo), la **etapa 3** agrupa sincronía, action frames y
eventos hacia el daemon (§4 y §5 del prototipo: el descubrimiento de pares exige el motor de
sincronía como prerrequisito), y la **etapa 4** es el companion `llw0` y la baja latencia (§6 del
prototipo y §8 de la spec de etapa 1).

## 0. Punto de partida: lo que deja la etapa 1

- `awdl0` nace y el daemon la acepta: puerta `CARD_CAPABILITIES` (byte +0x08 bit 0), `RawSet` 94 con
  rol 6, `RawGet` 97 con el nombre del padre, `SIOCGIFMEDIA` con `(ifm_current & 0xE0) == 0x80`,
  ioctls ifnet estándar y registro del monitoreo de eventos devolviendo 0
  (`AWDL_WIFIP2PD_POST_CREATE.md` §2.6; `AWDL_PROTOTYPE_DESIGN.md` §3).
- Colas: 1 TX submission + 1 TX completion + 1 RX completion se registran y arrancan en una interfaz
  de rol 6; el netif sólo rechaza 0 colas TX y exige 4 si el qword de información lleva el bit WMM
  (`0x8`) (`AWDL_STAGE1_QUEUE_SHAPE.md` §1–§3). La acción de la TX submission es una no-op que
  devuelve el lote por la TX completion (`AWDL_STAGE1_CODE_FIXES.md` §1.2).
- Superficie AWDL: los slots puros (490 y 507–569) van como stubs; el tipo 293 no está implementado
  y el dispatch devuelve 102 sin tocar `awdl0` (`AWDL_LLW_COMPANION.md` §4).
- Radio: sin PSF, sin ventanas de disponibilidad, sin salto de canal, sin pares. Nada del protocolo
  AWDL está en el aire.

## 1. Etapa 2 — Datapath real de transmisión y recepción

### 1.1 Objetivo

Mover paquetes reales a través de `awdl0`: el stack encola en las TxSubmissionQueue, el kext
consumidor los lleva a la radio USB y los transmite; la radio entrega los frames recibidos y el kext
los publica en la RxCompletionQueue registrada. El MTU 0x594 (1428 útiles tras el headroom de 64)
y el headroom fijados en la etapa 1 se vuelven funcionales (`AWDL_PROTOTYPE_DESIGN.md` §6;
`AWDL_PROXIMITY_DATAPATH.md` §2). Sin pares todavía: la validez del datapath es local (TX sale por
la radio, RX entra al stack).

### 1.2 Qué exige el daemon para considerarla en marcha

El daemon no ejercita el datapath: en estado estacionario no emite ningún GET/SET AWDL — sus 4
call sites de `RawGet` (tipos 12 y 97) y 2 de `RawSet` (94 y 293) son todos
(`AWDL_PROTOTYPE_DESIGN.md` §1.5; `AWDL_STRUCTS.md` §2). Ningún evento es condición de salud y la
recuperación se dispara por fallos de `RawGet`/`RawSet`/ioctls, no por silencio de eventos
(`AWDL_EVENTS.md` §5). El juez de esta etapa es el stack, no el daemon:

1. `awdl0` sube con `ifconfig` y soporta el camino estándar de ioctls ifnet (`SIOCSIFFLAGS`,
   `SIOCSIFLLADDR`, `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI`)
   (`AWDL_WIFIP2PD_POST_CREATE.md` §2.6).
2. Tráfico real (p. ej. IPv6/ND) atraviesa `awdl0` sin pánico; en la etapa 1 la acción no-op ya
   consumía el lote por la TX completion (análogo de la prueba T3.5 de
   `AWDL_STAGE1_IMPL_SPEC.md` §14), pero sin radio.
3. El daemon mantiene su handle: ausencia del bucle de re-creación (`No AWDL interface found.
   Creating AWDL interface` repetido; `AWDL_LLW_COMPANION.md` §1.3) y de `Failed to recover event
   monitoring for %s` (`AWDL_EVENTS.md` §5).

### 1.3 Capacidades del RTL8814AU necesarias

| Capacidad | Estado en la ruta RTL8814 | Referencia |
|---|---|---|
| TX de datos por AC | La ruta de datos estándar existe y funciona para `en2`: colas MAC `QSLT_BK/BE/VI/VO`, tres bulk OUT, descriptor de 32 B (`Docs/ARCHITECTURE.md:20` citado allí) | `AWDL_RTL8814_HW_SUPPORT.md` §4.3 — **feasible** |
| RX de datos de unicast/multicast arbitrario | El RCR con `AAP/AM/AB/AMF` y `RXFLTMAP0 = 0xFFFF` vivos acepta frames de emisores desconocidos; el descarte software está en la ruta de management, no en la de datos | `AWDL_RTL8814_HW_SUPPORT.md` §6.1 — **feasible** |
| Entrega RX hacia AirLayer | Hoy lo único que sale del core hacia IOKit es tráfico de datos convertido a Ethernet hacia `en2` (`rtl_netif_rx`); no existe el canal equivalente hacia la capa AirLayer y el punto de integración está por decidir | `AWDL_RTL8814_HW_SUPPORT.md` §6.4, §8 — `?` |
| Frames dirigidos a la MAC AWDL | El hardware los pasa (`RCR_AAP`); qué filtro software aplica a un frame de datos cuyo A1 es la MAC AWDL no está descrito | `AWDL_RTL8814_HW_SUPPORT.md` §6.2 — `?` |
| Reserva del headroom de 64 B en la ruta RX del radio | El consumidor del `RegistrationInfo+0x3C` está cerrado (`IOSkywalkNetworkInterface::getTxDataOffset`, ranura 0x9C0), pero cómo reserva ese espacio el camino RX del vendor core no está descrito | `AWDL_STAGE1_PENDING_CHECKS.md` §1; `AWDL_RTL8814_HW_SUPPORT.md` §7 fila 6.1 — `?` |
| Canal y concurrencia con la estación infra | Un solo recurso de canal compartido por software; para la etapa 2 sin pares el canal es fijo y el reparto no interviene | `AWDL_RTL8814_HW_SUPPORT.md` §2.3, §5.3 — **suficiente para esta etapa** |
| TX programada / interrupciones de timing | No existen (ni TX timestamp ni TBTT/early IRQ utilizable en USB); no se necesitan hasta la etapa 3 | `AWDL_RTL8814_HW_SUPPORT.md` §3.3, §4.4 — **no requerido aquí** |

### 1.4 Slots y APIs de la familia implicados

- **Forma de colas.** Decidir entre mantener 1 TX submission (sin bit WMM) o pasar a 4 TX submission
  (una por WME AC, mapa {0,1,2,3}) con el bit WMM: ambos caminos se registran; el netif exige las 4
  sólo si el bit WMM está puesto (`AWDL_STAGE1_QUEUE_SHAPE.md` §2–§4). Las 4×AC son la elección
  Broadcom para su modelo WMM y el mapa AC→queue-id que usa `getTxSubQueue`
  (`AWDL_PROXIMITY_DATAPATH.md` §2, §7). Elección concreta para `awdl0`: `?`.
- **TX.** Acción de la TxSubmissionQueue no nula (requisito de `initWithQueues`/`initWithPool`,
  `0xffffff8002b0caac`; `AWDL_STAGE1_CODE_FIXES.md` §1) convertida en el consumidor real: habilitar
  (`IOEventSource` enable), `requestDequeue(void*, uint)`, procesar el lote y devolver los paquetes
  por la TX completion. Patrón Broadcom de referencia: `dequeuePackets` con chequeo de interfaz
  habilitada y chip listo (`AWDL_PROXIMITY_DATAPATH.md` §6, §9).
- **RX.** Entrega en la RxCompletionQueue registrada con `enqueuePackets` (lotes menores que la
  capacidad, paquetes del mismo pool que la cola) y arranque con `requestEnqueue(0, 0)`
  (`AWDL_PROXIMITY_DATAPATH.md` §10, §14).
- **Pool.** `IOSkywalkPacketBufferPool::initWithName` con las mismas `PoolOptions` que la capa ya
  usa (`maxBuffersPerPacket = 1`, flags = 1); tamaño propio elegible respetando los invariantes
  (`AWDL_PROXIMITY_DATAPATH.md` §5, §14).
- **Habilitación.** `enable(unsigned)`/`disable(unsigned)` → `enableDatapath`/`disableDatapath`:
  habilitar TX completion, RX completion y TX submissions, disparar `requestDequeue(0,0)` /
  `requestEnqueue(0,0)` (`AWDL_PROXIMITY_DATAPATH.md` §8).
- **Getters de datapath.** `getRxCompQueue` (+0xD70), `getTxCompQueue` (+0xD78), `getTxSubQueue`
  (+0xD80), `getTxPacketPool` (+0xD88); `getRxPacketPool` (+0xD90) queda heredado de la familia y su
  comportamiento para una interfaz no Broadcom no fue decompilado (`AWDL_PROXIMITY_DATAPATH.md` §7, §15).
- **`RegistrationInfo`.** MTU `+0x48/+0x4C = 0x594`; headroom `+0x3C = 64` leído por
  `getTxDataOffset` (ranura 0x9C0) (`AWDL_INTERFACE_BIRTH.md` §2.2;
  `AWDL_STAGE1_PENDING_CHECKS.md` §1).
- **Pendientes y flush.** `stagePacket`/`freePendingPackets`/`flushPackets` (+0x340/+0x348/+0x330) y
  `sendPendingPacketsToStack` (+0xF30) son slots añadidos por las subclases Broadcom sobre la
  vtable de la familia; el equivalente en las clases de familia no aparece en los documentos — `?`
  (`AWDL_PROXIMITY_DATAPATH.md` §6, §11).
- **Multicast AWDL.** Par original+duplicado (`stashMulticastPacketPair`, `duplicatePacket`) y cola
  multicast (`triggerAWDLMulticastTx`, `p2pMulticastTx`): parte específicamente AWDL del datapath;
  puede aplazarse a la etapa 3 junto con el segundo canal social — `?`
  (`AWDL_PROXIMITY_DATAPATH.md` §9, §11).

### 1.5 Riesgos

- Cambio de forma de colas (1 → 4) sin precedente en el árbol RTL; la revalidación del netif con el
  bit WMM y el mapa AC→queue-id son terreno nuevo (`AWDL_STAGE1_QUEUE_SHAPE.md` §2–§4).
- El punto de integración RX (vendor core → AirLayer) no está decidido; es el hueco mayor
  (`AWDL_RTL8814_HW_SUPPORT.md` §6.4, §8).
- Comportamiento del slot heredado `getRxPacketPool` para una interfaz propia
  (`AWDL_PROXIMITY_DATAPATH.md` §15).
- Los defectos de concurrencia detectados en la revisión de la etapa 1 (inversión
  `_awdlLock` ↔ work loop, carrera stop ↔ ranura 94) elevan su probabilidad con tráfico real
  (`AWDL_STAGE1_CODE_REVIEW_HOOK.md`).
- Coexistencia de los pipes USB entre el datapath de `en2` y el de `awdl0` (MGT comparte pipe con
  los AC altos; la prioridad real la fija el EDCA del MAC) — `?`
  (`AWDL_RTL8814_HW_SUPPORT.md` §4.3).

### 1.6 Prueba en hardware y patrón de log

Precondición: las pruebas T1–T7 de la etapa 1 pasan (`AWDL_STAGE1_IMPL_SPEC.md` §14).

| # | Acción | Esperado | Patrón de log |
|---:|---|---|---|
| E2.1 | Cargar el kext y crear `awdl0` (`rtlawdl=1`) | Igual que T2/T3: nacimiento limpio | Anillo del kext: `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: born'`; ausencia de los diez literales de fallo de nacimiento (`AWDL_STAGE1_TEST_LOG_PATTERNS.md` §4) |
| E2.2 | `ifconfig awdl0 up` y tráfico IPv6/ND | Sin pánico; la acción TX real consume el lote | `?` — ningún literal de log de esta etapa existe aún en las fuentes; añadir líneas `LOG` propias del kext (diseño `?`), legibles en el anillo de `RTLAirLayerLog` (`AWDL_STAGE1_TEST_LOG_PATTERNS.md` §1) |
| E2.3 | TX hacia la radio sin par (p. ej. NDP multicast) | El frame sale por el pipe USB (verificable con el contador de TX del descriptor) | `?` — contadores visibles: `netstat -in awdl0` (no documentado `?`); el frame no vuelve por RX |
| E2.4 | RX de un frame dirigido a la MAC AWDL (inyección desde otro adaptador en monitor, o eco del propio chip `?`) | El frame llega a la RxCompletionQueue y al stack | `tcpdump -i awdl0` (BPF estándar de ifnet, no documentado en los docs AWDL `?`); línea `LOG` del kext `?` |
| E2.5 | Con el daemon vivo durante todo lo anterior | Sin re-creación ni recuperación | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found|Failed to recover event monitoring'` → `0` (`AWDL_LLW_COMPANION.md` §1.3; `AWDL_EVENTS.md` §5) |

## 2. Etapa 3 — Eventos y estado de descubrimiento de pares expuesto al daemon

### 2.1 Objetivo

Que pares AWDL reales aparezcan ante el daemon: el motor de sincronía/elección funciona, los action
frames se intercambian en los canales sociales y el daemon recibe los eventos que alimentan su
estado. El plan de los documentos es **reutilizar el `IO80211AWDLPeerManager` del kernel** en lugar
de reimplementar el protocolo: la factoría `createPeerManager` es heredable y para rol 6 construye
`IO80211AWDLPeerManager::withInterface`, con toda la elección, plantillas MI/UMI, parseo de TLVs,
tabla de peers y 14 timers del lado del kernel (`AWDL_PEERMANAGER_SOFTWARE.md` §2.1, §10). Todos los
símbolos necesarios están exportados por el entry `com.apple.iokit.IO80211Family`
(`AWDL_KEXT_LINKAGE.md` §0, §1.2).

### 2.2 Qué exige el daemon para considerarla en marcha

- El registro del monitoreo debe seguir devolviendo 0; un error encadena la recuperación que
  re-crea la interfaz (`AWDL_EVENTS.md` §5).
- El daemon consume sobre `awdl0` los ids (switch `sub_1000388D0`; `AWDL_EVENTS.md` §4.2):

| id | Validación del daemon | Resultado |
|---:|---|---|
| 48 `AWDL_ACTION_FRAME_RX` | `len >= 0x48`; decodifica la cabecera `apple80211_act_frm_hdr` de 72 B y el resto con `BinaryDecoder` | `DriverEvent.AWDL` tag 1 — descubrimiento de peers |
| 47 `PEER_PRESENCE` | Payload 0x44 B; puerta: descarta si el byte @68 vale 1 o el u32 @8 ≠ 1; lee MAC @0 y u8 @4 | tag 8 — presencia/ausencia |
| 46 `AWDL_SYNC_STATE_CHANGED` | Sin lectura de offsets | tag 11 — estado de sync |
| 65 `AWDL_STATISTICS` | Sin lectura | tag 13 |
| 195 `AWDL_CHAN_SEQ_CHANGED` | Sin lectura | tag 12 |
| 149 `P2P_AIRPLAY_STATISTICS` | Sin lectura | tag 2 |
| 243 `AWDL_RTG_STATISTICS` | Lee u16 @62, u8 @63, u32 @68, u8 @70, u8 @71&1, u32 @69, u16 @73; copia 56 B | tag 3 |
| 68 `REALTIME_MODE_END`, 235 `IDLE_SESSION` | Sin lectura | tags 0 y 14 |

- Ningún evento es por sí condición de salud ni hay timeout por silencio (`AWDL_EVENTS.md` §5); lo
  que el daemon hace con los tags `DriverEvent.AWDL` más allá del switch no está en los documentos
  (`?`).
- Del lado kernel, `monitorTimeline` valida el payload del evento 41
  (`{u16 AwNumber +0, MAC maestro +8, MAC topMaster +14, u32 FwAwStartTime +20}`) contra el maestro
  programado; un emisor incoherente hace que el par quede «no sincronizado» (desviación ≥ 6554 µs
  con aritmética módulo 2^20 µs) (`AWDL_PEERMANAGER_SOFTWARE.md` §5;
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2–§2.3; `AWDL_OPEN_QUESTIONS_1.md` §2.2–§2.3).

### 2.3 Capacidades del RTL8814AU necesarias

| Capacidad | Estado en la ruta RTL8814 | Referencia |
|---|---|---|
| RX de action frames de peers desconocidos | El hardware ya los acepta (`RCR`/`RXFLTMAP0`); quien los descarta es el core vendido (`mgt_dispatcher` exige A1 propia o broadcast; `OnAction` ignora categorías desconocidas). Intervenir antes de la comprobación de A1 tiene precedente (ganchos PMF/SAE) | `AWDL_RTL8814_HW_SUPPORT.md` §6.1–§6.3 — **feasible**; el punto de entrega hacia AirLayer es `?` (§6.4, §8) |
| TX de sync frames y action frames | Frame mgnt estándar: cola `QSLT_MGNT`, tasa fija (análogo driver de `awdl_min_rate` con `update_mgnt_tx_rate`), retry 6/12; sin TX programada ni repetición por AW: cada envío es un encolado software anclado a un timer del host | `AWDL_RTL8814_HW_SUPPORT.md` §4.1–§4.4, §7 fila 6.2(b3b) — **parcial** |
| Ventanas de disponibilidad | TSF legible/escribible por puerto (`REG_TSFTR`/`REG_TSFTR1`); sin interrupción TBTT/early en USB → timers software del host; `REG_TXPAUSE` como compuerta de ventana | `AWDL_RTL8814_HW_SUPPORT.md` §3.1–§3.3, §7 fila 6.2(b1) — **parcial**; precisión del jitter `?` |
| Salto de canal por la secuencia | Cambio síncrono sin IQK en el camino caliente, ~100 ms/canal medidos en escaneo; conmutación de banda completa en 2.4↔5 GHz; canales sociales 6/44/149 sin DFS `?` | `AWDL_RTL8814_HW_SUPPORT.md` §5, §7 fila 6.2(b3a) — **feasible** con reserva de timing |
| Elección de maestro | 100 % del lado del kernel (PeerManager); el driver sólo refleja el rol (ioctl 134) en la radio. RSSI por frame disponible en `attrib->phy_info` si el manager lo consulta | `AWDL_PEERMANAGER_SOFTWARE.md` §3, §10; `AWDL_RTL8814_HW_SUPPORT.md` §7 fila 6.2(b2) — **feasible** |
| Confirmación de TX (`awdl_oob_af`) | `CONFIG_XMIT_ACK` está comentado; la espera degradada es sleep de 50 ms. Sin equivalente hoy | `AWDL_RTL8814_HW_SUPPORT.md` §4.2, §7 fila 6.3(c3) — `?` |
| Estadísticas CCA / `awdl_stats` | PhyDM CCX no está en la lista de compilación; contadores en software o módulos adicionales | `AWDL_RTL8814_HW_SUPPORT.md` §7 última fila — **missing**; declarar sin soporte vía `awdl_cap` (`AWDL_PEERMANAGER_SOFTWARE.md` §10 punto 8; `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.3) |

### 2.4 Slots y APIs de la familia implicados

**Creación del manager (todo exportado, `AWDL_KEXT_LINKAGE.md` §1.2):** `createPeerManager`
heredado (ranura 355, vptr+0xB08) → `IO80211AWDLPeerManager::withInterface`
(`0xffffff80021c14bc`); registro en el controlador en +400. El enlace usa
`OSBundleLibraries → com.apple.iokit.IO80211Family` como hoy (`AWDL_KEXT_LINKAGE.md` §1.1).

**Obligaciones del driver, en orden de criticidad** (`AWDL_PEERMANAGER_SOFTWARE.md` §10):

1. Repetir la plantilla del sync frame: recibir el ioctl interno 151 (`0x97802869C8`, plantilla de
   512 B) y transmitirla cada `syncFramePeriod` (ioctl 116, `0x74802869C8`) alineada con la línea
   temporal propia — el rol que en Broadcom hace el firmware (`AWDL_PEERMANAGER_SOFTWARE.md` §4, §9.2;
   `AWDL_FIRMWARE_DUTIES.md` §6.2).
2. Eventos de línea temporal: publicar 41/42/43 con el formato
   `{AwNumber, maestro, topMaster, FwAwStartTime}` por `reportDataPathEvents`
   (`0xffffff80021dd8f4`, celda 83, vptr+0x298) o por `postMessage` → `postMessageSync`
   (`0xffffff80022f8abc`) (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2; `AWDL_KEXT_LINKAGE.md` §2.2;
   `AWDL_EVENTS.md` §1.1).
3. Dwell de la secuencia de canales (ioctl 129, `0x81802869C8`) y canales sociales (132/205,
   `0x84802869C8`/`0xCD802869C8`) (`AWDL_PEERMANAGER_SOFTWARE.md` §6, §9.2).
4. RX de action frames: capturar en los canales sociales y llamar a `actionFrameInput` (celda 71,
   vptr+0x238, `0xffffff800220e1ae`) con un `IO80211Buffer` (`__ZTV13IO80211Buffer`
   `0xffffff8002473e80`); alternativa `bpfTapInput` (`0xffffff8002331070`). El buffer empieza en la
   categoría; cabecera fija de 16 B validada (OUI `00:17:F2`, tipo `0x08`, versión ≠ 0, subtipo 0/3
   en +6) y TLVs desde +16 con cabecera de 3 B (`AWDL_PEERMANAGER_DRIVER_ABI.md` §1;
   `AWDL_FRAME_WIRE_FORMAT.md` §2–§3).
5. Pull de TX de datos: servir `dequeueTxPacket` (celda 85, vptr+0x2A8),
   `getUnicastPackets`/`getMulticastPackets` (celdas 46/45) durante las ventanas
   (`AWDL_PEERMANAGER_SOFTWARE.md` §8.3, §9.1).
6. Estado de sync: aceptar el ioctl 134 (`0x86802869C8`) y reflejarlo en la radio; emitir el evento
   46 con `postSyncStateChanged` (`0xffffff8002332054`, id 46, len 0) o el payload de 16 B estilo
   Broadcom (`AWDL_EVENTS.md` §3.1, §3.3).
7. Slots puros: los tipos con slot GET/SET (generales 1/4/9/13 en +0xFC8..+0xFE0 y los específicos
   116–467 listados en `AWDL_PROTOTYPE_DESIGN.md` §4; contratos por tipo en
   `AWDL_APPLE80211_DISPATCH.md` §5 y `AWDL_STRUCTS.md` §4). Los 27 tipos sólo-PeerManager los
   resuelve la familia (`AWDL_PROTOTYPE_DESIGN.md` §4).

Reparto de emisión de eventos con el manager reutilizado: el **driver** emite 41/42/43 (su propio
reloj de ventanas) y el 46 (tras el 134/elección); el **kernel** emite 48 (`postAFToP2pd`
`0xffffff800220fc2c`, sólo UMIs), 47 (`removePeer`/`cleanUpForPeerRemovedFromNDP`), 195
(`setChannelSequenceUnconditional`), 65/142/149/243 (helpers `post*` con len fijo) — el driver no
emite esos ids (`AWDL_EVENTS.md` §3.2, §3.3). Emisor del 47 en el alta de un peer: no documentado `?`.

### 2.5 Riesgos

- Registro de una subclase de terceros en la jerarquía AWDL (metaclass) y uso del
  `createPeerManager` heredado: sin precedente verificado (`AWDL_STAGE1_IMPL_SPEC.md` §15.10).
- Ruta exacta de los ioctls internos del manager hacia los slots del kext: la codificación
  `0x802869C8` con el tipo en el dword alto es observada, no documentada por Apple
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.1; `AWDL_PEERMANAGER_SOFTWARE.md` §9.2).
- Constructores de TLV 6/8/10/11 de la plantilla y contenido exacto del pull de TX sin cerrar
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11).
- Condición UMI (subtipo + canal) que decide qué action frame llega al daemon como 48: abierta
  (`AWDL_FRAME_WIRE_FORMAT.md` §2; `AWDL_OPEN_QUESTIONS_1.md` §1.1).
- Precisión de los timers del host frente a la cadencia AWDL (64–128 TU) y unidad del guard time:
  `?` (`AWDL_RTL8814_HW_SUPPORT.md` §3.2; `AWDL_FIRMWARE_DUTIES.md` §6.2).
- Coherencia del reloj propio para `AwNumber`/`FwAwStartTime` (módulo 2^20 µs) que exige
  `monitorTimeline`: `?` (`AWDL_OPEN_QUESTIONS_1.md` §2).
- Ciclo de RX limitado por el reparto del canal con la estación infra (duty cycle de escucha en los
  canales sociales): `?` (`AWDL_RTL8814_HW_SUPPORT.md` §5.3).
- Entorno de pruebas sin peer Apple (sólo un Samsung A53 como peer de referencia): la verificación
  de descubrimiento real queda acotada (`HANDOFF_AWDL.md` §1).

### 2.6 Prueba en hardware y patrón de log

Precondición: etapa 2 pasada. Sin peer Apple el plan se divide en validación sintética (cadena
kernel → daemon) y validación real (`?` disponible).

| # | Acción | Esperado | Patrón de log |
|---:|---|---|---|
| E3.1 | Con `awdl0` creada, el manager del kernel queda instanciado para rol 6 | El manager existe y arma su plantilla | `?` — literales del manager (`Setting Election Params to Self`, `I am Root Node`; `AWDL_PEERMANAGER_SOFTWARE.md` §3.2) son `IOLog` del kext de familia; su visibilidad en `log show` de 26.7.1 no está verificada `?` |
| E3.2 | Inyección sintética: el driver inyecta por `actionFrameInput` un action frame construido localmente (cabecera de 16 B + TLVs válidos) | El manager lo parsea y lo clasifica; si es UMI, emite 48 hacia el daemon | wifip2pd: `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'nan_event:'` — el prefijo `nan_event: %s APPLE80211_M_…` del switch está documentado (`AWDL_EVENTS.md` §4.2); el literal por caso de 48 no se transcribió `?` |
| E3.3 | Cabecera deliberadamente incorrecta (OUI distinto) | Descarte con `BAD OUI in AF` y volcado | Literal kernel `BAD OUI in AF` (`AWDL_FRAME_WIRE_FORMAT.md` §2) — visibilidad `?` (E3.1) |
| E3.4 | Emitir 41/42/43 desde el reloj propio del driver | `monitorTimeline` los valida sin marcar desincronización | `?` — la validación es interna (flag `+10304`, `AWDL_PEERMANAGER_SOFTWARE.md` §5); observable por la ausencia de la traza de desviación `?` |
| E3.5 | Tráfico de datos durante la ventana propia | `dequeueTxPacket`/`getUnicastPackets` sirven y la radio transmite | Anillo del kext: líneas `LOG` propias (diseño `?`); `tcpdump -i awdl0` para el resultado en el aire `?` |
| E3.6 | Daemon vivo durante todo lo anterior | Sin recuperación, sin re-creación | `log show … \| grep -cE 'Failed to recover event monitoring|No AWDL interface found'` → `0` (`AWDL_EVENTS.md` §5; `AWDL_LLW_COMPANION.md` §1.3) |
| E3.7 | (Con peer Apple, si existe) acercar un Mac/iOS en modo AWDL | Pares en el daemon: eventos 48/47/46 reales | `?` — misma sonda `nan_event:` de E3.2; el anillo del event pipe no debe llenarse (`Eventpipe is full, drop message[%d][%s], dataLen %lu`; `AWDL_EVENTS.md` §1.1) |

## 3. Etapa 4 — Companion `llw0` y baja latencia

### 3.1 Objetivo

Implementar el tipo 293 (`COMPANION_SKYWALK_IF_CREATE`) para que el daemon obtenga su interfaz
companion de baja latencia `llw0`, y — sólo si el datapath de baja latencia se quiere real — el
movimiento de paquetes y las estadísticas asociadas. La creación de `llw0` es trabajo de pipeline:
una segunda interfaz Skywalk con prefijo `llw` y rol que publique
`IO80211InterfaceRole = "LowLatency"` (`AWDL_LLW_COMPANION.md` §3.2–§3.4, §4).

### 3.2 Qué exige el daemon para considerarla en marcha

Contrato cerrado (`AWDL_LLW_COMPANION.md` §1–§4):

- **Emisión única:** el `RawSet` 293 se envía contra el handle de `awdl0` (ya creada y aceptada) al
  construir el `AWDLInterface`, sin gate de features; payload de 24 B `{u32 code @0 (el kernel no
  lo lee, `?`), u32 op @4, nombre de salida @8}`; único caller `0x100048061`, emisor
  `sub_10004B300` (`RawSet` en `0x10004b3e6`).
- **Éxito:** escribir el nombre BSD en payload+8 (máx. 16 B); el daemon valida con
  `if_nametoindex("llw0")` (literal `0x30776C6C` en `0x10004b814`). Si la companion ya existe,
  devolver 0 con el nombre de la existente.
- **Destrucción:** `op = 0`; sin companion devuelve `0xE00002C0`. Ops distintos de 0/1: no-op que
  devuelve 0.
- **Fallo absorbido:** dos errores (`Failed to create low latency interface for %s because %@` en
  `0x10004b50f`; `Failed to create interface %s on %s with error %@` en `0x100048fb5`), el init lo
  absorbe con `Failed to create the companion low latency interface for AWDL: %@` (`0x100417ead`) y
  continúa: `awdl0` intacta, sin teardown ni reintento.
- **Trampa de los 30 s:** responder éxito sin crear nada cuesta una espera de 30 s por intento
  (`0x100048b13`) y el timeout `Timed out while waiting for the low latency interface with parent
  %s` (`0x100048beb`) — evitar.
- **Property de IORegistry:** `IO80211InterfaceRole = "LowLatency"` (rol 2, tabla
  `0xffffff800246bd88[1]`) es lo que el descubridor usa para señalar el semáforo del rol 4.
- El daemon **no abre canal Apple80211 hacia `llw0`**: el descubridor señala sin crear handle
  (`0x100045297`); usos vía witness del objeto `CoreP2P.WiFiInterface` companion (p. ej. ioctls
  `'i'` 130/131) sin resolver — `?` (`AWDL_LLW_COMPANION.md` §3.5).
- Estadísticas de baja latencia consumidas por el daemon sobre `awdl0`: 142 (`AWDL_HPP_STATISTICS`,
  0x658 B), 149 (0xD0 B), 243 (0x4C B) (`AWDL_EVENTS.md` §4.2); quién las dispara en un stack sin
  firmware — `?`.

### 3.3 Capacidades del RTL8814AU necesarias

| Capacidad | Estado en la ruta RTL8814 | Referencia |
|---|---|---|
| Crear `llw0` como segunda interfaz Skywalk | No requiere radio nueva: Broadcom la crea sobre el mismo chip como interfaz Skywalk de rol 2; el mecanismo de pool/colas/registro es el de la etapa 2 | `AWDL_LLW_COMPANION.md` §3.3–§3.4; `AWDL_PROXIMITY_DATAPATH.md` §3, §14 — **feasible** |
| Datapath de baja latencia real (tráfico concurrente) | Requiere segundo puerto/concurrencia: el hardware tiene 5 pares MAC/BSSID y MBSSID CAM de 8, pero la build es de interfaz única (`CONFIG_CONCURRENT_MODE` comentado) y el puerto 1 en el camino USB (BCNQ1, IRQ de puerto) no está probado | `AWDL_RTL8814_HW_SUPPORT.md` §1, §2.1–§2.3 — hardware **feasible**, driver `?` |
| Cola TX de baja latencia | Broadcom elige capacidad 1024 cuando `isLowLatencyEnabled`; la elección para la capa RTL es libre | `AWDL_PROXIMITY_DATAPATH.md` §4 — `?` |
| `hp2p` / `LLW_PARAMS` (tipo 344) | En Broadcom es un iovar de firmware; los slots +0x1098/+0x1190 de la vtable AWDL operan sobre el PeerManager (`llwTxDelayThr`/… vía `IO80211NANPeerManager`), no sobre `llw0` | `AWDL_LLW_COMPANION.md` §3.5; `AWDL_FIRMWARE_DUTIES.md` §3.4 — delegar en el manager `?` |
| Estadísticas 142/149/243 | Las emiten los helpers del manager (kernel); el driver no participa salvo que los contadores sean suyos | `AWDL_EVENTS.md` §3.2, §3.3 — `?` sobre el disparo sin firmware |

### 3.4 Slots y APIs de la familia implicados

- **Dispatch del 293:** `gSetHandlerTable[292]` (`0xffffff8002460810`) → handler
  `0xffffff800228b4b8` (exige `len == 24` y `data != NULL`, si no devuelve 22; intenta la ruta WCL
  y cae al glue) → `safeMetaCast` a `IO80211AWDLProtocol` → **slot +0x1180** de la vtable AWDL
  (`AWDL_LLW_COMPANION.md` §3.1).
- **Override en `RTLAwdInterface`:** `setCOMPANION_SKYWALK_IF_CREATE` en +0x1180: op en payload+4,
  nombre en payload+8 (`__strncpy_chk(..., 0x10)`), guardia de companion ya existente (ivar propio
  al estilo de `ivars+120` Broadcom), error sin escribir el nombre (`AWDL_LLW_COMPANION.md` §3.2).
- **Clase de la interfaz LL:** subclase Skywalk propia con rol 2 (`setInterfaceRole(2)` publica
  `IO80211InterfaceRole = "LowLatency"`), `getBSDNamePrefix` → `"llw"` (slot +0x9E8), `RegistrationInfo`
  con el prefijo en +0x30, flags `|= 4`, `setLowLatencyEnabled(true)`, `setInterfaceEnable` →
  `reportLinkStatus(3, 0x80)`, `processBSDCommand` rechazando las constantes apple80211-por-ioctl
  (`0xC02069C8`, `0xC02069C9`, `0xC02869C9`, `0x802069C8`, `0x802869C8`) — modelo
  `AppleBCMWLANLowLatencyInterface` (`AWDL_LLW_COMPANION.md` §3.3–§3.4). Que esa interfaz derive de
  la cadena infra (como Broadcom) o de otra base es una decisión `?`.
- **Enlaces de companion:** `IO80211SkywalkInterface::setCompanionInterfaceId`
  (`0xffffff80022f952a`) y `IO80211VirtualInterface::setCompanionLowLatencySkywalkInterface`
  (`0xffffff8002334c6e`, ivar +216). La exportación del segundo no aparece en la selección de
  `AWDL_KEXT_LINKAGE.md` §1.2 (el doc advierte que esa tabla es una selección) — `?`.
- **flushFlowQueues** delegando en la companion: null-safe (con companion nula llama sólo a la
  base), sin riesgo de pánico por ausencia (`AWDL_LLW_COMPANION.md` §3.5;
  `AWDL_PROXIMITY_DATAPATH.md` §11).
- **Destrucción:** por `op = 0` con el propio teardown del kext (Broadcom usa
  `destroySkywalkInterface` + slots 0x600/0x90) (`AWDL_LLW_COMPANION.md` §3.2).

### 3.5 Riesgos

- La trampa de los 30 s: un kext que responde éxito sin crear la interfaz paga 30 s por intento y
  acaba en el mismo fallo lógico; crear o devolver error, nunca éxito vacío
  (`AWDL_LLW_COMPANION.md` §2.2, §4).
- Semántica del `code` del payload (el kernel no lo lee; el valor en el camino AWDL no se determinó):
  `?` (`AWDL_LLW_COMPANION.md` §1.1, §5).
- Datapath de baja latencia real sobre el puerto 1 USB: sin precedente probado en este árbol — `?`
  (`AWDL_RTL8814_HW_SUPPORT.md` §2.3).
- Uso en runtime del objeto companion del daemon (ioctls `'i'` 130/131 sobre `llw0`): `?`
  (`AWDL_LLW_COMPANION.md` §3.5).
- Orden de creación: la petición llega sólo tras `awdl0` completa; una creación de `llw0` que
  falle no debe tocar el datapath ya registrado de `awdl0` (rollback propio, misma disciplina que
  §11 de `AWDL_STAGE1_IMPL_SPEC.md`).

### 3.6 Prueba en hardware y patrón de log

Precondición: etapa 1 pasada (no requiere etapas 2/3 para la parte de creación).

| # | Acción | Esperado | Patrón de log |
|---:|---|---|---|
| E4.1 | Con `awdl0` aceptada, esperar la secuencia post-create | El daemon envía el 293 y el kext crea `llw0` | wifip2pd: `grep -E 'Creating companion low latency interface'` presente (`0x10041b48b`; `AWDL_LLW_COMPANION.md` §1.3); `grep -cE 'Failed to create low latency interface|Timed out while waiting for the low latency interface'` → `0` (`AWDL_LLW_COMPANION.md` §1.1, §2.2) |
| E4.2 | `ifconfig llw0` | Existe, con índice BSD vivo | `if_nametoindex("llw0")` ≠ 0 — la validación exacta del daemon (`AWDL_LLW_COMPANION.md` §1.1) |
| E4.3 | IORegistry | Entrada de la interfaz LL con `IO80211InterfaceRole = "LowLatency"`, `IOInterfaceName = "llw0"`, prefijo `llw` | `ioreg -r -l \| grep -E 'LowLatency|llw0'` (`AWDL_LLW_COMPANION.md` §3.3; `AWDL_KERNEL_ROLE_TABLE.md` §1–§2) |
| E4.4 | Enlace con `awdl0` | `flushFlowQueues` de `awdl0` delega sin pánico; id de companion fijado | Sin pánico bajo tráfico (prueba manual); `?` sin literal |
| E4.5 | Destrucción (`op = 0`) | `llw0` desaparece sin pánico; `awdl0` intacta | `?` — sin literal documentado del daemon para la destrucción (`AWDL_LLW_COMPANION.md` §3.2) |
| E4.6 | Regresión: forzar el fallo de la creación (gate cerrado para el 293) | Dos errores y continuación con `awdl0` intacta | `grep -E 'Failed to create the companion low latency interface'` presente y `grep -cE 'Timed out while waiting for the low latency interface'` → `0` (`AWDL_LLW_COMPANION.md` §2.1–§2.3); inverso de T6 de `AWDL_STAGE1_IMPL_SPEC.md` §14 |

## 4. Grafo de dependencias

```
        E1: awdl0 nace y wifip2pd la acepta
            (hecha; AWDL_STAGE1_IMPL_SPEC)
              │
        ┌─────┴──────────────────────┐
        ▼                            ▼
  E2: datapath real TX/RX      E4a: 293 → llw0 creada
  (colas, pool, radio)         (sólo registro Skywalk,
        │                       sin radio nueva)
        ▼                            │
  E3: sincronía + action frames      │
      + eventos al daemon            │
   (PeerManager del kernel;          │
    RX/TX de AF usa E2)              │
        │                            │
        ▼                            ▼
  E4b: datapath de baja latencia real y sus
       estadísticas (142/149/243)  `?`
```

- **E4a depende sólo de E1:** la petición 293 llega siempre tras `awdl0` aceptada y su creación es
  pipeline, sin radio (`AWDL_LLW_COMPANION.md` §1.2–§1.3). Puede implementarse en cualquier punto
  posterior a la etapa 1; el prototipo la reservaba a la etapa 4 (`AWDL_PROTOTYPE_DESIGN.md` §3.6).
- **E3 depende de E2:** la repetición del sync frame, el pull de TX y la entrega RX de action
  frames reutilizan las colas, el pool y el punto de integración radio de la etapa 2.
- **E4b depende de E3** (ventanas/sincronía para el scheduling LL) **y de la concurrencia de
  segundo puerto** (`?`, `AWDL_RTL8814_HW_SUPPORT.md` §2.3).
- Transversal: todo lo verificado pertenece al build 25G241; cada salto de versión exige
  re-verificar tablas, slots y la tabla de símbolos del entry IO80211Family, con el gate por build
  existente como mitigación (`AWDL_PROTOTYPE_DESIGN.md` §7; `AWDL_KEXT_LINKAGE.md` §3).

## 5. Interrogantes (`?`) que bloquean cada etapa

| Etapa | Interrogante | Fuente |
|---|---|---|
| E2 | Punto de integración RX del core vendido hacia AirLayer (frames con A1 = MAC AWDL y datos) | `AWDL_RTL8814_HW_SUPPORT.md` §6.4, §8 |
| E2 | Forma de colas final (1 TX sin WMM frente a 4×AC con WMM) y su efecto en el tráfico del stack | `AWDL_STAGE1_QUEUE_SHAPE.md` §2–§4 |
| E2 | Comportamiento del slot heredado `getRxPacketPool` (+0xD90) para una interfaz propia | `AWDL_PROXIMITY_DATAPATH.md` §7, §15 |
| E2 | Equivalente en las clases de familia de `stagePacket`/`freePendingPackets`/`flushPackets` (slots sólo-Broadcom) | `AWDL_PROXIMITY_DATAPATH.md` §6, §11 |
| E2 | Reserva del headroom de 64 B en la ruta RX del vendor core | `AWDL_STAGE1_PENDING_CHECKS.md` §1; `AWDL_RTL8814_HW_SUPPORT.md` §7 |
| E3 | Registro de la subclase de terceros en la jerarquía AWDL (metaclass) y `createPeerManager` heredado en runtime | `AWDL_STAGE1_IMPL_SPEC.md` §15.10 |
| E3 | Ruta exacta de los ioctls internos del manager (`0x802869C8` + tipo en dword alto) hacia los slots del kext | `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.1 |
| E3 | Constructores de TLV 6/8/10/11 y contenido exacto del pull de TX | `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11 |
| E3 | Condición UMI (subtipo + canal) que decide qué AF llega al daemon como 48 | `AWDL_FRAME_WIRE_FORMAT.md` §2; `AWDL_OPEN_QUESTIONS_1.md` §1.1 |
| E3 | Precisión de los timers del host frente a la cadencia AWDL; unidad del guard time | `AWDL_RTL8814_HW_SUPPORT.md` §3.2; `AWDL_FIRMWARE_DUTIES.md` §6.2 |
| E3 | Relación exacta del reloj propio con `AwNumber`/`FwAwStartTime` (módulo 2^20 µs) que exige `monitorTimeline` | `AWDL_OPEN_QUESTIONS_1.md` §2 |
| E3 | Emisor del evento 47 en el alta de un peer (sólo la baja está documentada) y payload del 195 | `AWDL_EVENTS.md` §3.2 |
| E3 | Visibilidad de los `IOLog` de la familia en `log show` de 26.7.1 (afecta al patrón de log de las pruebas) | `AWDL_STAGE1_TEST_LOG_PATTERNS.md` §1 |
| E4 | Semántica del campo `code` del 293 | `AWDL_LLW_COMPANION.md` §1.1, §5 |
| E4 | Exportación de `IO80211VirtualInterface::setCompanionLowLatencySkywalkInterface` y uso del enlace de la familia | `AWDL_LLW_COMPANION.md` §3.5; `AWDL_KEXT_LINKAGE.md` §1.2 (selección) |
| E4 | Datapath de baja latencia real: puerto 1 en el camino USB del 8814AU | `AWDL_RTL8814_HW_SUPPORT.md` §2.3 |
| E4 | Forma de colas de `llw0` (capacidad LL, WMM, pool compartido con `awdl0`) | `AWDL_PROXIMITY_DATAPATH.md` §4 |
| E4 | Uso en runtime del objeto companion del daemon (ioctls `'i'` 130/131 sobre `llw0`) | `AWDL_LLW_COMPANION.md` §3.5 |
| E4 | Disparo de las estadísticas 142/149/243 en un stack sin firmware | `AWDL_EVENTS.md` §3.2 |

## 6. Correspondencia de fuentes

| Sección | Documentos de los que deriva |
|---|---|
| §0 | `AWDL_STAGE1_IMPL_SPEC.md`, `AWDL_STAGE1_QUEUE_SHAPE.md`, `AWDL_STAGE1_CODE_FIXES.md`, `AWDL_WIFIP2PD_POST_CREATE.md`, `AWDL_LLW_COMPANION.md` |
| §1 | `AWDL_PROTOTYPE_DESIGN.md` §6, `AWDL_PROXIMITY_DATAPATH.md`, `AWDL_STAGE1_QUEUE_SHAPE.md`, `AWDL_STAGE1_PENDING_CHECKS.md`, `AWDL_INTERFACE_BIRTH.md`, `AWDL_RTL8814_HW_SUPPORT.md`, `AWDL_STAGE1_CODE_REVIEW_HOOK.md` |
| §2 | `AWDL_PEERMANAGER_SOFTWARE.md`, `AWDL_PEERMANAGER_DRIVER_ABI.md`, `AWDL_KEXT_LINKAGE.md`, `AWDL_EVENTS.md`, `AWDL_FRAME_WIRE_FORMAT.md`, `AWDL_TLV_BODIES.md`, `AWDL_OPEN_QUESTIONS_1.md`, `AWDL_FIRMWARE_DUTIES.md`, `AWDL_RTL8814_HW_SUPPORT.md`, `AWDL_APPLE80211_DISPATCH.md`, `AWDL_STRUCTS.md`, `AWDL_PROTOTYPE_DESIGN.md` §4–§5 |
| §3 | `AWDL_LLW_COMPANION.md`, `AWDL_EVENTS.md`, `AWDL_PROXIMITY_DATAPATH.md`, `AWDL_KERNEL_ROLE_TABLE.md`, `AWDL_RTL8814_HW_SUPPORT.md`, `AWDL_FIRMWARE_DUTIES.md`, `AWDL_STAGE1_IMPL_SPEC.md` §8 |
| §4–§5 | Síntesis de todos los anteriores; estado global en `HANDOFF_AWDL.md` §4 |

Ninguna conclusión de este documento implica prueba en hardware RTL8814; las etapas se validan en
el orden indicado precisamente para acotar el radio de un fallo (`AWDL_PROTOTYPE_DESIGN.md` §7).
