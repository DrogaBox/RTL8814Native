# Portabilidad del camino de datos de `awdl0` a macOS 14.8.9 y 15.8.2 (23J631 / 24H232)

Comprobación de portabilidad del código de datos añadido a `Source/AirLayer/AWDLInterface.cpp`
por los commits `5b686a8` (RX de datos a `awdl0`), `28b58e8` (reloj AW siguiendo un maestro),
`5630d49` (TX de datos), `f2969a4` (acotación por franja y `preQueuePacketCheckQuietMode`) y
`9dc71a6` (códigos de rechazo del aparcamiento). Referencia: 26.7.1 (25G241, Darwin 25.6.0).
Destino: 23J631 (14.8.9, Darwin 23.6.0) y 24H232 (15.8.2, Darwin 24.6.0), ambos x86_64.

Método: symtabs de los tres kernelcaches (volcados `Docs/re/io80211_*/syms*.txt`) para la
existencia de símbolos, y lectura en IDA headless (`idat -A -S`, copias de
`boot_23J631.kc.i64`, `boot_24H232.kc.i64` y `boot_awdl.kc.i64` en `Build/ida_port_data/`,
volcados `out23/`, `out24/`, `out26/`) para los contratos de comportamiento. Los VA citados son
cabezas de función o de instrucción del build correspondiente; cambian por versión y sólo
acreditan el hallazgo. Lo no observado va con `?`.

Veredicto: **los símbolos del camino de datos existen con el mismo nombre decorado en las
tres versiones y los cinco contratos de comportamiento del alcance se replican sin cambios**.
Las dependencias por versión siguen siendo las ya documentadas en
[`AIRLAYER_PORT_14_15.md`](AIRLAYER_PORT_14_15.md) (vtables de la clase y headers por build);
los commits de datos no añaden ninguna nueva.

## 1. Símbolos de enlace

Definiciones leídas en los symtabs (dirección y tipo `0f` = definida). Los nombres `__ZNK…`
de los accesores const aparecen además como referencia indefinida (`0000000000000000 01`) en
`syms.txt` de IO80211Family: es el uso que la familia hace de ellos, no su definición, que
vive en `syms_com.apple.iokit.IOSkywalkFamily.txt`.

| Símbolo | 23J631 | 24H232 | 25G241 |
|---|---|---|---|
| `__ZN23IO80211SkywalkInterface14getPeerManagerEv` | `0xffffff80021a0a3c` | `0xffffff8002263490` | `0xffffff80022f5fa8` |
| `__ZN22IO80211AWDLPeerManager28preQueuePacketCheckQuietModeEP12ether_header` | `0xffffff8002100c70` | `0xffffff80021b0b80` | `0xffffff800221649e` |
| `__ZNK15IOSkywalkPacket13getDataOffsetEv` | `0xffffff8002861dac` | `0xffffff80029a457a` | `0xffffff8002b2c5e2` |
| `__ZNK15IOSkywalkPacket13getDataLengthEv` | `0xffffff8002861bf2` | `0xffffff80029803e8` | `0xffffff8002b064d0` |
| `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv` | `0xffffff8002861dcc` | `0xffffff8002980430` | `0xffffff8002b06518` |
| `__ZNK15IOSkywalkPacket13getBufferSizeEv` | `0xffffff8002861b68` | `0xffffff80029803d0` | `0xffffff8002b064b8` |
| `__ZNK15IOSkywalkPacket20getPacketBufferCountEv` | `0xffffff8002861bae` | `0xffffff80029803de` | `0xffffff8002b064c6` |
| `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj` | `0xffffff8002861e3e` | `0xffffff80029804a2` | `0xffffff8002b0658a` |
| `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj` | `0xffffff8002861d00` | `0xffffff80029a44d0` | `0xffffff8002b2c538` |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh` | `0xffffff8002862b22` | `0xffffff8002981098` | `0xffffff8002b07180` |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj` | `0xffffff8002860b0a` | `0xffffff800297f82e` | `0xffffff8002b058ee` |
| `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `0xffffff8002860d8c` | `0xffffff80029a4072` | `0xffffff8002b2c0de` |
| `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` (const) | `0xffffff800286b1aa` | `0xffffff80029a56b6` | `0xffffff8002b2d766` |
| `__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` (const) | `0xffffff800286ebe6` | `0xffffff80029a5afc` | `0xffffff8002b2dbb6` |
| `__ZN26IOSkywalkTxSubmissionQueue13legacyDequeueEv` | `0xffffff80028681ee` | `0xffffff80029a47a4` | `0xffffff8002b2c860` |
| `__ZN26IOSkywalkTxSubmissionQueue12checkForWorkEv` | `0xffffff8002867c56` | `0xffffff80029a468a` | `0xffffff8002b2c746` |
| `__ZN26IOSkywalkTxSubmissionQueue8withPoolE…` (7 argumentos) | `0xffffff80028679ec` | `0xffffff80029860c2` | `0xffffff8002b0ca86` |
| `__ZN26IOSkywalkRxCompletionQueue8withPoolE…` (7 argumentos) | `0xffffff800286a828` | `0xffffff80029883e0` | `0xffffff8002b0ee04` |
| `__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE` | `0xffffff800285faa0` | `0xffffff800297ebb8` | `0xffffff8002b04be8` |
| `__ZN23IO80211VirtualInterface11bpfTapInputEP6__mbufjPvm` | `0xffffff80021d95c6` | `0xffffff80022a6926` | `0xffffff8002330e7a` |
| `__ZN23IO80211VirtualInterface12setLinkStateE16IO80211LinkStatej` | `0xffffff80021dbba0` | `0xffffff80022a8e9e` | `0xffffff8002333408` |
| `__ZN22IO80211AWDLPeerManager20interfaceStateChangeE18StateChangedSource` | `0xffffff80020c85a2` | `0xffffff8002176af0` | `0xffffff80021d8cc8` |
| `__ZN22IO80211AWDLPeerManager31setChannelSequenceUnconditionalEb` | `0xffffff80020bbee6` | `0xffffff8002169b06` | `0xffffff80021cb69a` |
| `__ZN22IO80211AWDLPeerManager23updateSyncFrameTemplateEb` | `0xffffff80020cb764` | `0xffffff8002179b0e` | `0xffffff80021dc5b2` |
| `__ZN22IO80211AWDLPeerManager27buildAWDLElectionParamsVer2EP23awdlElectionParamsTlvV2j` | `0xffffff80020fb5d8` | `0xffffff80021ab474` | `0xffffff8002210a3c` |

Los 27 símbolos que `AWDLInterface.cpp` enlaza por nombre (`__asm`) están definidos en los symtabs de 23J631 y 24H232. Los demás símbolos IO80211/Skywalk que el binario importa y faltan en 14 o 15 son celdas de vtable heredadas por compilar con los headers de 26 (`AIRLAYER_PORT_14_15.md` §4), ninguno del camino de datos.

Notas:

- Las dos variantes de `enqueuePackets` (array const `EPKP…` y array mutable `EP…`) existen
  en las tres versiones; el kext usa la const (`AWDLInterface.cpp`, `rtl_rxcomp_enqueue` /
  `rtl_txcomp_enqueue`). La variante mutable está en `0xffffff800286b250` / `0xffffff800286ec7a`
  (23J631), `0xffffff80029a5760` / `0xffffff80029a5bb4` (24H232), `0xffffff8002b2d810` /
  `0xffffff8002b2dc6e` (25G241).
- Existe además `bpfTapInput` sobre `IO80211Buffer` (`EP13IO80211BufferjPvm`) en las tres; el
  kext usa la de `__mbuf`.
- `IOSkywalkTxSubmissionQueue::initWithPool` (que en 25G241 rechaza acción nula,
  `AWDL_TX_DATOS_SPEC.md` §2.1) no se releyó en 14/15 (`?`); sí se comprobó que el thunk
  `withPool` de 7 argumentos reenvía al de 9 con la acción principal en la misma posición
  (§2.2).

## 2. Contrato de la acción de la cola de envío (`legacyDequeue`)

`IOSkywalkTxSubmissionQueue::legacyDequeue` es **idéntico en las tres versiones**: mismo
tamaño (`0x124`), misma secuencia de instrucciones y mismos campos.

- Puertas de entrada: byte `+0x28` (fuente habilitada) y byte `+0xCC`; en 23J631 se codifican
  `cmp byte,0/jz` y en 24H232/25G241 `cmp byte,1/jnz` (o `test`): mismo predicado.
- Llamada a la acción: acción en `[this+0x20]`, `owner` en `[this+0x18]`, `refCon` en
  `[this+0x58]`, array de paquetes = base de la tabla `[this+0xE8]+8` desplazada por el
  índice `[[this+0xE8]+0x80]`, contador en `ecx`. Sitios: `0xffffff800286826c` (23J631),
  `0xffffff80029a4822` (24H232), `0xffffff8002b2c8de` (25G241).
- **El retorno de la acción es el número de paquetes consumidos**: `test eax,eax` (0 ⇒ marca
  «sin progreso» en `+0x13C` y corta), y en caso contrario el índice de la tabla
  `[[this+0xE8]+0x80]` avanza exactamente `eax` con envolvente por la capacidad en `+4`
  (`add edx, eax`, `cmp edx, esi`, `cmovb`): `0xffffff8002868295`–`8b` (23J631),
  `0xffffff80029a4845`–`8b` (24H232), `0xffffff8002b2c941`–`97` (25G241). Devolver menos que
  `count` sigue consumiendo parcialmente el lote; devolver `count` (lo que hace `txData`)
  consume todo.
- `checkForWork` conserva la misma cadena en las tres versiones
  (`packetSubmissionForKPipe` → `legacyDequeue`, puertas `+0x28`/`+0xCC`, `requestEnqueue`
  por celda de vtable); en 23J631 mide `0xCC` frente a `0x117` bytes, por reordenación del
  prólogo.

El thunk `IOSkywalkTxSubmissionQueue::withPool` de 7 argumentos reenvía al de 9 con la misma
forma de pila en las tres versiones (`call` a la sobrecarga de 9 en `0xffffff8002867a06`
(23J631), `0xffffff80029860dc` (24H232), `0xffffff8002b0caa0` (25G241)); la acción del quinto
parámetro llega como acción principal. Consecuencia: `createDatapath` es válido tal cual en
14/15, y el requisito «acción no nula» de la etapa 1 no se ha relajado (`?` sobre la puerta
exacta de `initWithPool` en 14/15, ver §1).

## 3. Colas de completado (`enqueuePackets`)

### 3.1 RX, variante const (`IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket const**, uint, uint)`)

Mismas comprobaciones y constantes en las tres versiones (23J631 `0xffffff800286b1aa`,
tamaño `0xA5`; 24H232 `0xffffff80029a56b6` y 25G241 `0xffffff8002b2d766`, tamaño `0xAA`):

- `pkts == 0` o `n == 0` ⇒ `0xE00002C2`.
- **`n < capacidad` estricto** contra el campo `+0xAC` (`cmp`/`jbe` a error).
- Sin work loop (`[this+0x30] == 0`) ⇒ `0xE00002D8`; dentro del cerrojo: fuente deshabilitada
  (byte `+0x28`) o no inicializada (byte `+0xCC`) ⇒ `0xE00002D7`; byte `+0x118` con bit 0
  puesto ⇒ `0xE00002D5`.
- El cuarto argumento (`opts`) **no se lee** en ninguna versión.
- Enrutado idéntico: `[this+0x78] != 0` ⇒ `enqueuePacketsForNetworking`, si no
  `enqueuePacketsForKPipe`.

El lotaje de `deliverData` (`n ≤ 16 < 128`) y de `txData` (`64 < 128`) sigue siendo aceptado.

### 3.2 TX, variante const (`IOSkywalkTxCompletionQueue::enqueuePackets(IOSkywalkPacket const**, uint, uint)`)

Idéntico en las tres versiones (23J631 `0xffffff800286ebe6`, tamaño `0x94`; 24H232
`0xffffff80029a5afc` y 25G241 `0xffffff8002b2dbb6`, tamaño `0xB7`): mismas constantes
(`0xE00002C2/D8/D7/D5`), capacidad `+0xAC` estricta, `opts` sin leer, `memmove` del lote a la
tabla y `completePackets(IOSkywalkPacket**, uint)`. Única divergencia, interna de la familia:
el byte de «paquete preparado» vive en `+0x108` en 23J631 y 24H232 y en `+0x110` en 25G241
(el RX lo tiene en `+0x118` en las tres). No afecta al kext.

## 4. Accesores y ciclo de vida del paquete

Comparación instrucción a instrucción; en las tres versiones los campos del
`IOSkywalkPacket` no se mueven:

| Función | Verificación |
|---|---|
| `getDataOffset` | `[[this+0x30]]` con guarda `+0x64` (recuento de buffers) ⇒ `IOSkywalkPacketBuffer::getDataOff`. Idéntico. |
| `getDataLength` | Suma `IOSkywalkPacketBuffer::getDataLength` sobre la tabla `+0x30` con guarda `+0x64`. Idéntico. |
| `getDataVirtualAddress` | `getMemorySegment().getVirtualAddress() + getMemorySegmentOffset()` del primer buffer. Idéntico. |
| `getBufferSize` | `[[this+0x40]+0x30]`. Idéntico. |
| `getPacketBufferCount` | `[this+0x64]`. Idéntico. |
| `prepare` | Byte `+0x58` bit 1 ya preparado ⇒ 0; cola nula ⇒ dirección 0; si no, virtual `prepareWithQueue`. Idéntico: la llamada del kext `prepare(pkt, nullptr, 0, 0)` es válida en las tres. |
| `setDataOffsetAndLength` | Misma forma y guarda `+0x64`; 23J631 difiere sólo en el orden del prólogo. |
| `setLinkHeaderLength` | Byte a byte idéntico: `_kern_packet_set_link_header_length([this+0x28], …)` con coalescing del error a `0xE00002C2`. |

Pool: `allocatePacket` es byte a byte idéntico (cola por la ranura de vtable `+0x130` con la
marca de espera forzada a 1, `esi=1`); `deallocatePacket` tiene la misma semántica (guarda de
puntero nulo, `completePreparedPacket` si el byte `+0x58` tiene bit 1, `release` por la celda
`+0x1E0` y `_kern_pbufpool_free`). El patrón de `deliverData` (allocar con `opts=1`,
`prepare` sin cola, escribir el buffer, `setDataOffsetAndLength`, `setLinkHeaderLength`,
`enqueuePackets`, y en error `deallocatePacket`) es portable sin cambios.

## 5. Gestor de pares

### 5.1 `getPeerManager`

Misma semántica, contenedores distintos: 23J631 `[[this+0x110]+0x30]`
(`0xffffff80021a0a3c`), 24H232 `[[this+0x110]+0x28]` (`0xffffff8002263490`), 25G241
`[[this+0x120]+0x28]` (`0xffffff80022f5fa8`). Coincide con los desplazamientos de
[`AWDL_PRESENCE_OFFSETS_14_15.md`](AWDL_PRESENCE_OFFSETS_14_15.md) (ivars del vif `0x110`
en 23/24, `0x120` en 25; asiento del gestor `+0x30` en 23, `+0x28` en 24/25). Llamada por
símbolo: portable.

### 5.2 `preQueuePacketCheckQuietMode`

Misma lógica en las tres versiones (23J631 `0xffffff8002100c70`, 24H232
`0xffffff80021b0b80`, 25G241 `0xffffff800221649e`): contador de servicios AirPlay a cero como
precondición, máscara del dword de AWDLOn (`(v & 0xC) == 0` ⇒ fuera; `v & 8` ⇒ fuera;
resto ⇒ procede), unicast de la cabecera Ethernet ⇒ `setDataMode(this, 3, 0)`, log
`"Exiting quiet mode due to queued packet from network stack"` y cola a `exitQuietMode`.
Sólo cambian los ivars internos del gestor: contador de servicios `+0x7EE0` (23J631) /
`+0x7F28` (24H232) / `+0xA648` (25G241); AWDLOn `+0x3EC8` / `+0x3EE8` / `+0x3EE0`. La llamada
del kext (`rtl_pm_preQueueQuietMode` con la cabecera Ethernet cruda del paquete) es válida
tal cual.

### 5.3 Puerta de link-down de `interfaceStateChange`

`IO80211AWDLPeerManager::interfaceStateChange` conserva la puerta de entrada en las tres
versiones, con la misma forma: tras `ivars+0x3EE4/0x3ECC/0x3EEC != 0` (25G241/23J631/24H232,
el cuarto byte tras AWDLOn), lee un objeto del controlador, llama su ranura `+0xB88` y
compara con 1:

| | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| lectura del objeto | `[ctrl+0x560]`, `0xffffff80021d8eae` | `[ctrl+0x548]`, `0xffffff80020c8780` | `[ctrl+0x550]`, `0xffffff8002176cce` |
| ranura llamada | `+0xB88` | `+0xB88` | `+0xB88` |
| comparación | `cmp eax, 1` | `cmp eax, 1` | `cmp eax, 1` |
| en caso 1 | log `"Interface link is down, nothing to do. Source = %s"` y salida sin tocar estrategia | ídem | ídem |

La ranura `+0xB88` es estable y en las tres versiones su entrada en la vtable de
`IO80211VirtualInterface` es `linkState()` (volcados `Docs/re/io80211_*/vt_IO80211VirtualInterface.txt`,
ranura 369 en las tres; el vecino `setLinkState` de 4 argumentos está en `+0xB80` en las
tres). El objeto leído es por tanto **una interfaz virtual, no el gestor de pares** (el
gestor no tiene `linkState`); si es la vif de `awdl0`, la primaria o la de infraestructura
sigue sin resolverse (`?`, ya abierto en `AWDL_SIN_ASOCIACION_INFRA.md` §2.1). El ivar del
controlador que guarda el objeto se mueve (`0x560` → `0x548` → `0x550`), pero el kext no lo
referencia: la puerta es interna de la familia y `enable()`/`disable()` sólo llaman al
`setLinkState` propio.

Advertencia de vtable: la celda virtual de `IO80211VirtualInterface::setLinkState` (2
argumentos), que `enable()`/`disable()` despachan por la vtable del kext, se desplaza
(ranura 462 en 25G241, 461 en 24H232, **447 en 23J631**): es la deriva general ya documentada
en `AIRLAYER_PORT_14_15.md` §5, no un efecto nuevo de los commits de datos.

## 6. Layouts de plantilla y de secuencia de canales

### 6.1 TLV 24 (elección v2) que lee `alignClock`

`buildAWDLElectionParamsVer2` es instrucción a instrucción idéntico en las tres versiones
(gate de capacidad `cap > 0x2A`, si no log `"skipping buildAWDLElectionParamsV…"` y retorno 0).
Escribe el mismo layout:

| Campo | Posición | Valor |
|---|---|---|
| tipo | `+0` | `0x18` (24) |
| longitud | `+1..2` | `0x28` (40) |
| root master | value `+0..5` | 6 bytes |
| **sync master** | value `+6..11` | 6 bytes |
| rootLFC | value `+12..15` | u32 |
| hop count («distancia») | value `+16..19` | u32 |
| root metric | value `+20..23` | u32 |
| self metric | value `+24..27` | u32 |
| election id | value `+28..31` | u32 |
| cero | value `+32..35` | u32 |
| selfLFC | value `+36..39` | u32 |

Longitud total devuelta: `0x2B` (43). Las lecturas de `alignClock` (`e2+3` top master,
`e2+3+6` sync master, `*(u32*)(e2+3+16)` distancia saturada a 255, `e2+3+20` métrica del
root, `e2+3+24` métrica propia) y de `buildAf` valen sin cambios en 14/15. La plantilla del
151 sigue componiéndose con los mismos constructores en el mismo orden
(`updateSyncFrameTemplate` llama `buildServiceParamTlv` → `buildAWDLElectionParamsVer2` →
`buildAwdlDataPathStateTlv` … `buildSyncTreeTlv` en las tres versiones), así que el TLV 24
llega al kext dentro de la plantilla tal como lo espera `findTlv(…, 24, 28)`.

### 6.2 Petición 129 (secuencia de canales de sincronización)

`setChannelSequenceUnconditional` monta el mismo payload de 400 B en las tres versiones
(sitios del build: `0xffffff80020bbf46…` (23J631), `0xffffff8002169b74…` (24H232),
`0xffffff80021cb710…` (25G241)):

- `+0x00` u32 versión = 1.
- `+0x04` u8 flags (SDB) — escrito tras el bucle de entradas.
- `+0x05` u8 `num_channels`: byte leído del gestor; es además el límite del bucle de copia.
- `+0x06..+0x09`: cuatro bytes de ivars (`unknown6`, sub-slots, …).
- Entradas desde `+0x0C`, 12 B cada una, paso de bucle `0xC`: qword copiado del almacén del
  gestor (u32 versión/flags en `entry+0`, **u32 canal en `entry+4`**) y **u32 flags en
  `entry+8`** (`mov [payload+rcx+14h], …` con `rcx` el desplazamiento de la entrada).

El parseo del kext en `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` (`n = p[5]`, canal
`*(u32*)(e+4)`, flags `*(u32*)(e+8)`, tope propio de 16 entradas) es correcto en las tres
versiones. La deduplicación por `memcmp` de `0x190` bytes y la emisión por `ioctl_internal`
también se conservan.

## 7. Conclusiones

- **Enlace**: los símbolos del camino de datos de hoy existen con nombres decorados
  idénticos en 23J631 y 24H232 (§1). Ninguna firma cambió: la sobrecarga const de
  `enqueuePackets` y el `prepare(IP20IOSkywalkPacketQueueyj)` están en las dos versiones
  viejas.
- **Comportamiento**: `legacyDequeue` (retorno = consumidos, §2), las dos `enqueuePackets`
  (capacidad estricta `n < +0xAC`, `opts` sin efecto, §3), los accesores de paquete y el
  pool (§4), `getPeerManager` y `preQueuePacketCheckQuietMode` (§5), la puerta de
  `interfaceStateChange` (ranura `+0xB88` estable, §5.3), el TLV 24 (§6.1) y el payload del
  129 (§6.2) replican los contratos de 25G241 documentados en `AWDL_TX_DATOS_SPEC.md`,
  `AWDL_RX_DATA_TO_SKYWALK.md`, `AWDL_TX_HOOKS_POR_PAQUETE.md`,
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7/§3.11 y `AWDL_SIN_ASOCIACION_INFRA.md` §2.1.
- **Sin dependencias nuevas por versión**: todo lo que usa el camino de datos añadido hoy es
  símbolo C no virtual (`__asm`) o contrato interno de la familia. Las únicas piezas
  versión-dependientes que toca siguen siendo las ya cubiertas por `AIRLAYER_PORT_14_15.md`
  (headers y vtables por build: `postMessage`, `setLinkState`, la super-llamada de `init`).
- El ivar del controlador que `interfaceStateChange` consulta se mueve por versión
  (`0x560/0x548/0x550`); irrelevante para el kext mientras no se referencie directamente.

### Abiertos

| Punto | Estado |
|---|---|
| Identidad del objeto de la puerta de `interfaceStateChange` (vif de `awdl0`, primaria o infra) | `?` en las tres versiones |
| Puerta de acción nula de `IOSkywalkTxSubmissionQueue::initWithPool` en 14/15 | `?` (no volcado; el thunk `withPool` de 7 args sí se comprobó) |
| Lectores de los ivars internos que cambian (`+0x7EE0/+0x7F28/+0xA648`, AWDLOn) | Cubiertos por `AWDL_PRESENCE_OFFSETS_14_15.md`; fuera del alcance aquí |
