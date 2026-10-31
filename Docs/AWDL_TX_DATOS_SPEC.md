# Especificación de implementación: TX de datos desde `awdl0`

Qué debe sustituir a `RTLAwdInterface::txNoop` (`Source/AirLayer/AWDLInterface.cpp:289`) para que
`awdl0` transmita los datos que la pila le entrega: el contrato exacto de la acción de la
`IOSkywalkTxSubmissionQueue` creada con `withPool`, la conversión Ethernet→802.11 que inserta la
encapsulación AWDL, la función nueva del ABI de radio, y los pasos de cambio en los dos kexts con su
plan de pruebas sin par Apple. Base: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build
25G241, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`) verificado sobre una copia de la base
de datos anotada en `Build/ida_awdl/task_tx_datos/` (`verify.txt`, `verify2.txt`, `verify3.txt`: 44
cabeceras de función con nombre comprobadas, volcados completos de `legacyDequeue`, `enqueuePackets`,
`completePackets`, `withPool`/`initWithPool`, vtables celda a celda y 34 puntos de instrucción), los
docs `AWDL_DATA_ENCAP.md` (§3, §4, §7), `AWDL_STAGE2B_MBUF_TO_PACKET.md`, `AWDL_RX_DATA_TO_SKYWALK.md`
y `AWDL_RTL8814_RAW_TX_PORT1.md`, y las fuentes de `Source/rtl8814au` y `Source/Driver`.
Direcciones = VA del KC; convención de vtables: una llamada `call [vptr+X]` lee la celda `ZTV+X+0x10`.
Lo no observado va con `?`. No incluye la RX de datos (`AWDL_RX_DATA_TO_SKYWALK.md`).

## 1. Resumen

1. La acción TX de `awdl0` recibe del kernel paquetes del pool propio cuyo frame es **Ethernet II**
   (`DA, SA, ethertype, payload`) colocado en `getDataVirtualAddress() + getDataOffset()`, con
   `dataOffset` = headroom del `RegistrationInfo` (`+0x3C` = 64, `Source/AirLayer/AWDLInterface.cpp:230`),
   que el kernel lee vía `IOSkywalkNetworkInterface::getTxDataOffset` (`0xffffff8002b0baba`,
   `movzx eax, word ptr [rax+3Ch]` en `0xffffff8002b0bad2`). Ninguna pieza de la familia añade
   cabecera: la acción es el primer consumidor (`AWDL_DATA_ENCAP.md` §3).
2. La acción debe: leer el frame, construir la trama 802.11 de datos con la encapsulación Apple
   (SNAP + cabecera AWDL de datos con su contador de secuencia por destino) y entregarla a la radio
   por una función nueva del ABI (`rtlRadioTxData`), después devolver **todos** los paquetes por la
   `IOSkywalkTxCompletionQueue` en tandas con `n < capacidad` y retornar el recuento consumido.
   Devolver 0 estanca la cola (`+0x13C` bit 2); devolver un código de error corrompe el anillo de
   lectura (§2.3): la acción nunca retorna errores.
3. La conversión que en Broadcom reparten host y firmware (host: SNAP + cabecera AWDL de datos +
   contador; firmware: cabecera MAC 802.11, `?`) la hace aquí entera la acción: la radio envía los
   bytes tal cual por la cola de gestión, con tasa fija OFDM 6 Mb/s, sin retocar nada del frame.
4. El multicast sale también desde la acción en la primera etapa (sin stash); la segunda etapa
   opcional replica el modelo Broadcom (duplicado + `stashMulticastPacketPair` + temporizador del
   gestor + overrides `p2pMulticastTx`/`freeMulticastPacket`/`triggerAWDLMulticastTx`, §3.4).
5. La radio necesita una única incorporación al ABI, `RTL_RADIO_FN_TX_DATA`, clon de
   `rtlRadioTx80211` que acepte tramas de datos (`Source/Driver/AWDLTx.c:32` hoy las rechaza); el
   puerto 1 no interviene en el descriptor TX (§4.4).

## 2. Contrato de la acción de la cola de envío

### 2.1 Creación con `withPool`

Factoría usada (7 parámetros), ya enlazada por la etapa 1 (`Source/AirLayer/AWDLInterface.cpp:40-42`):

| | |
|---|---|
| Símbolo | `__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j` |
| VA | `0xffffff8002b0ca86` (cabecera verificada) |
| Firma | `IOSkywalkTxSubmissionQueue *withPool(IOSkywalkPacketBufferPool *pool, uint32_t capacity, uint32_t flags, OSObject *owner, uint32_t (*action)(OSObject *, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket * const *, uint32_t, void *), void *refCon, uint32_t opts)` |

El desensamblado de la factoría (`0xffffff8002b0ca8a`–`0xffffff8002b0caa0`) la muestra reenviando a
la variante de 9 parámetros (`0xffffff8002b0c9b4`) con una acción de notificación
`uint32_t (*)(OSObject *, IOSkywalkTxSubmissionQueue *, uint32_t *)` nula y `-1` en el último
parámetro; la acción principal que la etapa 1 registra es la del quinto parámetro. La variante de 9
(`0xffffff8002b0c9b4`, volcada en `verify3.txt`) llama al virtual `initWithPool` (celda vptr
`+0x2A8`, `0xffffff8002b0ca37`) con el `-1` de la fábrica en el tercer entero (acaba en `+0xB8`,
`0xffffff8002b0cb24` vía `edx`), la capacidad en el cuarto (`ecx`, el que exige ≥ 8) y los flags en
el quinto (`r8d`); fuerza además dirección 1 en la init base (`mov ecx, 1` en
`0xffffff8002b0cb06`). Existe también una variante de 8 parámetros con acción `IOSkywalkPacket **`
no constante que fuerza el bit 3 de `opts` (modo lista, `or r10d, 8` en `0xffffff8002b0ca6d`); no
se usa.

Requisitos que comprueba `IOSkywalkTxSubmissionQueue::initWithPool` (`0xffffff8002b0caac`,
`__ZN26IOSkywalkTxSubmissionQueue12initWithPoolE…`, volcado completo en `verify.txt`):

| Requisito | VA | Efecto si falla |
|---|---|---|
| `pool != 0` | `0xffffff8002b0cac3`–`0xffffff8002b0cac6` | init falso |
| **`action != 0`** | `0xffffff8002b0cacc`–`0xffffff8002b0cad3` | init falso (el defecto bloqueante de la etapa 1, `f9abb43`, cerrado por la acción no-op) |
| `capacity >= 8` | `0xffffff8002b0caba` (`cmp ecx, 8`) | init falso |
| `opts <= 0x1F` | `0xffffff8002b0cadd`–`0xffffff8002b0cae1` | init falso |

`opts` consume los bits: `0x8` → modo lista (`or byte [cola+0x142], 2` en `0xffffff8002b0cb31`),
`0x2` → `or byte [cola+0x142], 4` (`0xffffff8002b0cb3e`), `0x4` → ABFS con `_gIOSkywalkABFSEnable`
(`0xffffff8002b0cb45`–`0xffffff8002b0cb52`). La etapa pasa 0. La capacidad (128 en `awdl0`) entra en
la tabla de paquetes: `createPacketTable([cola+0xAC])` (`0xffffff8002b0cb81`–`0xffffff8002b0cb87`).
La init base (`IOSkywalkPacketQueue::initWithPool`, `0xffffff8002b08bc8`) guarda el pool retenido en
`+0x50` (`0xffffff8002b08c53`), el `refCon` en `+0x58` (`0xffffff8002b08c40`), la dirección en `+0xB4`
(`0xffffff8002b08c44`), la capacidad en `+0xAC` (j3, desde el `ecx` de la subclase) y el tercer
entero (flags, `< 0x10000`, `0xffffff8002b08c46`) en `+0xA8`; deja `+0xB8` a `0xFFFFFFFF`
(`0xffffff8002b08c50`), que la subclase reescribe con el `-1` de la fábrica.
`owner` queda en `+0x18` y la acción en `+0x20` (campos de `IOEventSource` que la init base llena con
`IOEventSource::init(owner, acción)`; las lee `legacyDequeue`, §2.2).

### 2.2 Llamada de la acción (`legacyDequeue`)

`IOSkywalkTxSubmissionQueue::legacyDequeue` (`__ZN26IOSkywalkTxSubmissionQueue13legacyDequeueEv`,
`0xffffff8002b2c860`; volcado completo en `verify.txt`):

1. Puertas: `byte +0x28 bit 0` (habilitada, `0xffffff8002b2c86e`) y `byte +0xCC bit 0` (arrancada,
   `0xffffff8002b2c87b`); si falta la segunda, cola a la celda vptr `+0x2A0` = `purgePackets`
   (`0xffffff8002b2c97e`).
2. Bucle: pendientes = escritura − lectura (`[tabla+0x00] − [tabla+0x80]`), o capacidad − lectura
   (`[tabla+4] − [tabla+0x80]`) si la escritura envolvió (`0xffffff8002b2c899`–`0xffffff8002b2c8b2`);
   marca el bit 1 de `+0x13C` en vuelo (`0xffffff8002b2c8c9`) y llama

   `action(owner = [cola+0x18], cola, paquetes = tabla + 8·índiceLectura, count = pendientes, refCon = [cola+0x58])`
   (`0xffffff8002b2c8d0`–`0xffffff8002b2c8de`).

   El array es una porción contigua de la tabla de paquetes de la cola (`AWDL_STAGE2B_MBUF_TO_PACKET.md`
   §2); cada elemento es un `IOSkywalkPacket *` (constante para la acción).
3. **Retorno**: se usa como recuento consumido, sin signo y sin camino de error:
   - `0` → bit 2 de `+0x13C` (estancamiento, `0xffffff8002b2c937`–`0xffffff8002b2c93a`), marca de
     tiempo en el bloque de estadísticas `[cola+0x100]+0x78` y fin.
   - `ret > 0` → índice de lectura `[tabla+0x80] += ret`, con una única resta de la capacidad
     (`[tabla+4]`) si el resultado la cruza (`0xffffff8002b2c90a`–`0xffffff8002b2c92c`).
   - No hay comparación de signo: un `IOReturn` negativo (p. ej. `0xE00002D8`) entra como entero sin
     signo enorme y el índice de lectura salta con envoltura parcial de una sola resta
     (`0xffffff8002b2c91c`–`0xffffff8002b2c92a`), dejando el anillo incoherente. **La acción devuelve
     siempre el número de paquetes consumidos (0 sólo si no hay pendientes); nunca un código de
     error.** Los errores propios del trabajo (radio ocupada, sin recursos) se registran y no alteran
     el retorno: los paquetes se devuelven igualmente por la cola de completado.
4. Contadores del bloque `[cola+0x100]`: `+0x60` llamadas, `+0x00` consumos parciales
   (`ret < pendientes`, `0xffffff8002b2c8ff`–`0xffffff8002b2c904`), `+0x08` estancamientos;
   `[cola+0x130] += ret` (`0xffffff8002b2c954`).
5. El estancamiento no es permanente: `checkForWork`
   (`__ZN26IOSkywalkTxSubmissionQueue12checkForWorkEv`, `0xffffff8002b2c746`, volcado en
   `verify3.txt`) limpia el bit 2 de `+0x13C` (`0xffffff8002b2c7df`–`0xffffff8002b2c7e2`) y vuelve a
   llamar a `packetSubmission` (`0xffffff8002b2c848`), con estadística del tiempo parado en
   `[cola+0x100]+0x70`.

### 2.3 Qué contiene el array y quién es dueño

Los paquetes los produce el kernel, no la acción:
`packetSubmission` → `refillTxRingForNetworking` (`0xffffff8002b0d3e0`) →
`kern_netif_queue_tx_dequeue` (`0xffffff8002b0d41f`) y, por cada handle kern,
`getPacketWithHandle(pool, handle)` (`0xffffff8002b0d473`, pool en `[cola+0x50]`),
`acquireWithPacketHandle(handle, 0)` (celda vptr `+0x1D8`, `0xffffff8002b0d486`),
`prepareWithQueue(cola, 1, 0)` (celda vptr `+0x188`, `0xffffff8002b0d49c`), BPF de salida
(`0xffffff8002b0d4dc`) y `producePacket` en la tabla (`0xffffff8002b0d4fa`). El frame lo copió el
kernel al buffer del paquete con `dataOffset` = headroom (§1.1); el paquete llega ya `prepare`-ado
(byte `+0x58` bit 2) y con su handle kern en `+0x28`.

Propiedad: mientras la acción corre, los paquetes del array están fuera del anillo del kernel y
dentro de la tabla de la cola de envío. La acción no debe llamar
`IOSkywalkPacketBufferPool::deallocatePacket` sobre ellos: el cierre del handle kern lo hace la cola
de completado dentro de `completePackets` (§2.5). Sólo los paquetes que el driver haya creado por su
cuenta (el duplicado multicast de la fase 2, §3.4) admiten `deallocatePacket` directo, porque no
pasaron por la tabla. La vía de retorno es única para los dos orígenes: `enqueuePackets` de la cola
de completado, que llama a `deallocatePackets` del pool al final (mismo pool: el de la cola,
`[cola+0x50]`).

### 2.4 Devolución por la cola de completado

`IOSkywalkTxCompletionQueue::enqueuePackets(IOSkywalkPacket const*, uint, uint)`
(`__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj`, `0xffffff8002b2dbb6`;
volcado completo en `verify.txt`). Errores, en orden:

| Comprobación | VA | Código |
|---|---|---|
| `pkts != 0` y `count != 0` | `0xffffff8002b2dbcb`–`0xffffff8002b2dbde` | `0xE00002C2` |
| `count < [cola+0xAC]` (capacidad, **estricto**) | `0xffffff8002b2dbe6`–`0xffffff8002b2dbec` | `0xE00002C2` |
| `[cola+0x30] != 0` (work loop) | `0xffffff8002b2dbee`–`0xffffff8002b2dbf3` | `0xE00002D8` |
| `byte +0x28 bit 0` y `byte +0xCC bit 0` | `0xffffff8002b2dc06`–`0xffffff8002b2dc13` | `0xE00002D7` |
| `byte +0x110 bit 0` puesto | `0xffffff8002b2dc15`–`0xffffff8002b2dc1c` | `0xE00002D5` |

Con éxito: `memmove` de los punteros al array propio `[cola+0xE0]` (reservado por
`initWithPool` con `capacidad × 8` bytes, `0xffffff8002b12a2d`–`0xffffff8002b12a43`) y llamada a
`completePackets(pkt**, n)` (`0xffffff8002b2dc41`). Devuelve `kIOReturnSuccess`. **Si falla, los
paquetes siguen siendo responsabilidad del llamador**: liberarlos con
`deallocatePacket` (así lo hace `txNoop`, `Source/AirLayer/AWDLInterface.cpp:296-301`; las tandas de
la acción usan `n = 64` con capacidad 128). La variante encadenada
(`__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEP15IOSkywalkPacketjj`, `0xffffff8002b2dc6e`)
recorre `getNextPacket`/`setNextPacket` y hace el mismo cierre paquete a paquete.

### 2.5 `completePackets`: qué toca la familia a cada paquete

`IOSkywalkTxCompletionQueue::completePackets(IOSkywalkPacket**, uint)`
(`__ZN26IOSkywalkTxCompletionQueue15completePacketsEPP15IOSkywalkPacketj`, `0xffffff8002b12b9e`;
volcado en `verify.txt`), por paquete:

1. `getDataLength` (celda vptr `+0x140`, llamada en `0xffffff8002b12bdd`).
2. `getSourceQueue` (`0xffffff8002b12be9`); si hay cola origen, `notifySrcQueue(false, cola, len)`
   (`0xffffff8002b12bfe`) — aterriza en el `packetCompletion` de la cola de envío
   (`0xffffff8002b0d758`, vptr `+0x230`), que sólo lleva contadores (`AWDL_STAGE2B_MBUF_TO_PACKET.md` §5).
3. `completeWithQueue(colaCompletado, 1, 0)` (celda vptr `+0x198`, `0xffffff8002b12c13`).
4. `kern_packet_tx_completion(pkt+0x28)` **sólo si `[cola+0xE8] != 0`** (`0xffffff8002b12c19`–
   `0xffffff8002b12c29`).

Al terminar el lote: `deallocatePackets(pool = [cola+0x50], pkts, n)` por la celda `+0x148` de la
vtable del pool (`0xffffff8002b12c42`–`0xffffff8002b12c58`), `notifySrcQueue(true, 0, 0)`
(`0xffffff8002b12c6a`) y, si `[cola+0xF0] != 0`, llamada `[vptr+0x268](n, bytes)`
(`0xffffff8002b12c6f`–`0xffffff8002b12c84`). `[cola+0xF0]` guarda el séptimo parámetro de
`initWithPool` (`0xffffff8002b0cb98`–`0xffffff8002b0cb9c`; `completePackets` lo desreferencia como
objeto con vtable, aunque el nombre mangled lo tipa como función `?`); la fábrica de 7 parámetros
pasa NULL en ese hueco, así que la llamada no ocurre con las colas de la etapa 1. **No se lee ni se exige ningún
campo de estado o flags del paquete en 25G241**: la acción no tiene que marcar nada antes de
devolver los paquetes; el único estado que importa es el bit de preparado (`+0x58` bit 2, ya puesto
por `prepareWithQueue` en los paquetes del kernel, o por `prepare` en los propios) y las marcas
internas de la subclase PCIe (`pkt+0x78 → +0x28`, flag 0x80 de Broadcom, §3.4), que no aplican a un
paquete de familia. La evidencia operativa de que el ciclo completo funciona con colas creadas
exactamente así es la capa STA (`en2` transmite con el mismo patrón,
`Source/AirLayer/RTLAirLayer.cpp:408-437`); el interior de `kern_packet_tx_completion` cuando
`[cola+0xE8] = 0` no se ha volcado (`?`, irrelevante si la vía en2 lo demuestra en hardware).

### 2.6 Accessors de `IOSkywalkPacket` / `IOSkywalkPacketBuffer` para leer el frame

Todos exportados (tipo `0x0f` en `Docs/re/io80211_25.6.0/syms_com.apple.iokit.IOSkywalkFamily.txt`),
resueltos contra `__ZTV15IOSkywalkPacket` `0xffffff8002b3b510` (volcado completo en `verify.txt`;
la celda X de la vtable = `vptr+X−0x10`). Con `maxBuffersPerPacket = 1` (pool de la etapa 1,
`Source/AirLayer/AWDLInterface.cpp:181`) todo paquete tiene un único segmento y el frame se lee con
los cuatro primeros.

| Método (mangled) | VA | Celda vptr | Uso |
|---|---|---|---|
| `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv` | `0xffffff8002b06518` | `+0x188` | dirección del segmento **sin** `dataOffset` |
| `__ZNK15IOSkywalkPacket13getDataOffsetEv` | `0xffffff8002b2c5e2` | `+0x160` | frame = base + offset |
| `__ZNK15IOSkywalkPacket13getDataLengthEv` | `0xffffff8002b064d0` | `+0x150` | longitud del frame Ethernet II |
| `__ZNK15IOSkywalkPacket20getPacketBufferCountEv` | `0xffffff8002b064c6` | `+0x138` | nº de segmentos (1 en este pool) |
| `__ZNK15IOSkywalkPacket16getPacketBuffersEPP21IOSkywalkPacketBufferj` | `0xffffff8002b2c400` | `+0x130` | array de segmentos (encadenado general) |
| `__ZNK15IOSkywalkPacket19getMemoryDescriptorEv` | `0xffffff8002b2c43a` | `+0x140` | descriptor de memoria del primer segmento |
| `__ZNK15IOSkywalkPacket13getBufferSizeEv` | `0xffffff8002b064b8` | no virtual | tamaño del buffer (2048) |
| `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj` | `0xffffff8002b2c538` | `+0x168` | sólo para paquetes propios |
| `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj` | `0xffffff8002b0658a` | `+0x1A0` | obligatorio antes de tocar el buffer (paquete propio) |
| `__ZN15IOSkywalkPacket13setNextPacketEPS_` | `0xffffff8002b068e2` | — | encadenar (variante encadenada) |
| `__ZNK15IOSkywalkPacket13getNextPacketEv` | `0xffffff8002b068ec` | — | recorrer la cadena (exportada `0x0f`; corrige el MISSING de `AWDL_RX_DATA_TO_SKYWALK.md` §7) |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh` | `0xffffff8002b07180` | — | 14, en paquetes propios |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderOffsetEj` | `0xffffff8002b071a8` | — | Broadcom lo usa en TX (`0xffffff8001543384`) |
| `__ZN22IOSkywalkNetworkPacket11setHeadroomEh` | `0xffffff8002b07158` | — | el truco −16 de Broadcom (`0xffffff80015432d0`) |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj` | `0xffffff8002b058ee` | — | paquetes propios (fase 2) |
| `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `0xffffff8002b2c0de` | — | errores previos a la acción; fase 2 |
| `__ZN25IOSkywalkPacketBufferPool17deallocatePacketsEPP15IOSkywalkPacketj` | `0xffffff8002b2c128` | — | lote (lo llama `completePackets`) |

Accessors de `IOSkywalkPacketBuffer` por si algún día el pool crece
(`__ZNK21IOSkywalkPacketBuffer13getBufferSizeEv` `0xffffff8002b06ca8`,
`…13getDataLengthEv` `0xffffff8002b06cb6`, `…13getDataOffsetEv` `0xffffff8002b06cc0`,
`…22getMemorySegmentOffsetEv` `0xffffff8002b06ce4`, `…16getMemorySegmentEv` `0xffffff8002b06cda`,
`__ZN21IOSkywalkPacketBuffer13setDataLengthEj` `0xffffff8002b2c602`,
`__ZN21IOSkywalkPacketBuffer22setDataOffsetAndLengthEtj` `0xffffff8002b2c67e`).
`getSourceQueue` (`0xffffff8002b12be9` es la llamada interna; símbolo `0x0f`) lo consulta la familia
en el cierre, la acción no lo necesita.

### 2.7 Sin tablas de salto

El barrido de las funciones del contrato (`legacyDequeue`, las dos `enqueuePackets`,
`completePackets`, `dequeuePackets` PCIe) no halló tablas de salto: la bifurcación de roles de
Broadcom es por pruebas de bits (`bt`/`cmp`), no un `switch` (no hay números de caso que sesgar).

## 3. Conversión Ethernet → AWDL 802.11

### 3.1 Lo que hace Broadcom (verificado)

En `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets` (`0xffffff80015420f0`, acción de su
cola TX), ruta role 6 (`AWDL_DATA_ENCAP.md` §3-§4; todos los puntos re-verificados como `SPOT` en
`verify.txt`):

- El frame de entrada es **Ethernet II** (DA en +0, `0xffffff80015424c7`; ethertype en +12).
- Unicast (bit 0 de DA a 0, `test cl, 1` en `0xffffff80015424aa`): construye la clave de flow queue
  `{ifId, fAc, DA}` (`findOrCreateFlowQueue`, `0xffffff8001542006`) e inserta 16 bytes anteponiendo
  el frame: guarda el frame viejo, baja el headroom 16 (`setHeadroom` en `0xffffff80015432d0`,
  `add r15b, 0F0h` en `0xffffff8001543271`), copia DA+SA, escribe la longitud 802.3 en +12
  (`0xffffff80015432f7`, artefacto del formato interno host↔firmware, **no existe en el aire**), el
  SNAP `AA AA 03 00 17 F2 08 00` en +14 (`mov rax, 8F2170003AAAAh` en `0xffffff80015432fd`, almacén
  en `0xffffff8001543307`), `03 04` en +22 (`0xffffff800154330c`), `00 00` en +26
  (`0xffffff8001543314`), el ethertype original en +28 (`0xffffff8001543321`) y la secuencia en +24:
  lee el contador u16 en `+0x8A` del struct interno de la flow queue **antes** de incrementarlo
  (`0xffffff800154332f`/`0xffffff8001543339`) y escribe
  `seq = (contador & 0xFFF) | (fAc << 12)` (`0xffffff800154334d`–`0xffffff8001543358`); ajusta
  `setDataOffset` (vptr+0x148, `0xffffff8001543368`), `setDataLength` (vptr+0x138,
  `0xffffff8001543378`), `setLinkHeaderOffset` (`0xffffff8001543384`) y encola en la flow queue
  (`0xffffff80015433ca`). El firmware recibe la trama ya encapsulada y añade después la cabecera
  MAC 802.11 (`?` interior del firmware: subtipo QoS, tasas, duración).
- **QoS**: el host no decide nada de QoS (el formato interno no lo lleva). En el aire las tramas de
  datos AWDL se ven sin QoS para MSDU único (`fc 0x0008`) y con QoS (`0x0088`) sólo en A-MSDU
  (owly `docs/SPEC.md` AWDL-13/14; `crates/owly-awdl/src/frame.rs:313` transmite sin QoS,
  `frame.rs:334-335` A-MSDU con QoS y TID 0). Qué subtipo elige el firmware Broadcom para un MSDU
  único es `?`; el equivalente owly y las capturas de `AWDL_REFERENCIAS_EXTERNAS.md` §5 marcan el
  camino sin QoS.
- Multicast (bit 0 de DA a 1, `0xffffff8001543020`): si el paquete ya lleva el flag 0x80
  (`mov al, [rdx+28h]`, `and al, 7Fh` en `0xffffff800154321f`, `mov [rdx+28h], al` en
  `0xffffff8001543225`) se transmite como un unicast; si no, se marca (`or byte ptr [r12+28h], 80h`
  en `0xffffff8001543066`), se duplica (`duplicatePacket`, `0xffffff800154307e`), se marcan ambos y
  `stashMulticastPacketPair` (`0xffffff80015435be` → `0xffffff8002334e20` →
  `IO80211AWDLPeerManager::stashMulticastPacketPair`, `0xffffff8002213b16`, dos arrays de 16) deja
  la pareja en el gestor. El temporizador del gestor
  (`IO80211AWDLPeerManager::handleMulticastTimer`, `0xffffff80021c38c4`) los libera en la ventana
  AW oportuna llamando al slot `+0xF18` de la interfaz (`p2pMulticastTx(IO80211NetworkPacket**, uint)`;
  la base `0xffffff8002334d6a` no hace nada; la PCIe `0xffffff8001541d64` reentra en su acción
  `dequeuePackets` vía la celda `+0x310`). Los sobrantes se liberan con `freeMulticastPacket`
  (slot `+0xF08`; base `0xffffff8002334d5a` vacía). `IO80211Family` también contiene
  `IO80211AWDLPeerManager::addAwdlSnapHeader(mbuf*, u16)` (`0xffffff8002216cd0`), código muerto sin
  referencias: la familia no encapsula nada.
- En RX el simétrico exige el SNAP de Apple (`cmp eax, 8F21700h` en `0xffffff800154a238`) y sólo las
  tramas con ese SNAP alcanzan la cola de `awdl0` (`AWDL_RX_DATA_TO_SKYWALK.md` §3).

### 3.2 Lo que debe construir la acción RTL

Trama completa (sin FCS), por paquete, sobre el Ethernet II recibido:

```
802.11 (24 B) | AA AA 03 00 17 F2 08 00 | 03 04 | seq (u16 LE) | 00 00 | ethertype (BE) | payload
```

| Campo | Valor | Justificación |
|---|---|---|
| Frame control | `08 00` (datos, sin QoS, To DS/From DS 0) | owly AWDL-13, `frame.rs:313`; el receptor debe aceptar también QoS (`frame.rs:157`) pero el emisor owly no lo usa |
| Duration | `00 00` | sin firmware que la rellene `?`; el MAC puede imponer la suya `?` |
| addr1 | DA del frame Ethernet | conversión de Broadcom en RX usa addr1 como DA (`0xffffff800154a569`) |
| addr2 | MAC de `awdl0` | SA en RX = addr2 (`0xffffff800154a559`) |
| addr3 | `00:25:00:FF:94:73` | BSSID AWDL (`frame.rs:160`; `Source/rtl8814au/core/rtw_recv.c:27`) |
| Sequence control | el que ponga el hardware | la cola TX del vendor activa secuencia de hardware cuando `qos_en = 0` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:112-117`); el `SetSeqNum` de la receta de gestión sobra para datos |
| SNAP + `03 04` + `00 00` + ethertype | igual que Broadcom, sin la longitud 802.3 | §3.1; el campo de longitud desaparece al sustituir la cabecera Ethernet por la 802.11 (`AWDL_DATA_ENCAP.md` §7.4) |
| seq AWDL | `(contador & 0xFFF) \| (AC << 12)`, u16 LE, contador por DA, primera trama 0 | Broadcom lo toma de la flow queue por `{if, AC, DA}` antes de incrementar (§3.1); AC = 0 con una sola cola de envío; la semántica del nibble alto en un receptor Apple es `?` (owly no lo menciona) |

Tamaño máximo: 24 + 16 + MTU (0x594 = 1428) = 1468 B ≤ límite del buffer de gestión
`MAX_XMIT_EXTBUF_SZ − TXDESC_OFFSET` = 1536 − 48 (`Source/rtl8814au/include/rtw_xmit.h:89`,
`TXDESC_SIZE 40` en `rtw_xmit.h:199-201`, `PACKET_OFFSET_SZ 8` en
`Source/rtl8814au/include/rtl8814bu_hal.h:25`; el check de `AWDLTx.c:29` ya lo acota). A-MSDU no se
emite (MSDU único, como owly); si un día se emitiera, la cabecera sería 26 B con QoS control
TID 0 + bit 0x0080 y subtramas con longitud BE16 = 16 + payload (`frame.rs:326-348`).

### 3.3 Reparto Broadcom ↔ RTL

Broadcom reparte: host = SNAP + cabecera AWDL de datos + contador; firmware = cabecera MAC 802.11 +
QoS + tasas (`?`). En RTL no hay firmware AWDL, así que la acción construye la trama entera (§3.2) y
la radio la envía como bytes crudos — la misma división de responsabilidades que ya funciona para los
action frames (`RTL_RADIO_FN_TX_80211`, `Source/Driver/RTLRadioABI.h:66`, «The bytes are copied
before the call returns»). La alternativa (la acción inserta sólo los 16 bytes con el truco de
headroom −16 y el driver construye la cabecera 802.11) añadiría conocimiento AWDL al vendor tree sin
ventaja: se descarta.

### 3.4 Multicast

- **Fase 1** (con esta spec): la acción transmite el multicast inmediatamente por la radio con su
  propio contador de secuencia para ese DA, sin stash. Los pares sólo escuchan en sus ventanas de
  disponibilidad, así que la entrega no está garantizada; se acepta y se cuenta (§6). El trío
  familia (stash + temporizador) funciona sin cooperación del driver, pero exige los overrides de
  interfaz para ser útil.
- **Fase 2** (opcional, posterior): overrides en `RTLAwdInterface` de los slots ya declarados en el
  shim (`Source/AirLayer/AWDLInterface.h:173-176`, cuerpos hoy en la familia):
  `freeMulticastPacket` (slot `+0xF08`: `deallocatePacket` al pool TX),
  `triggerAWDLMulticastTx` (slot `+0xF10`), `p2pMulticastTx(IO80211NetworkPacket**, uint)`
  (slot `+0xF18`: transmitir cada paquete por la radio y devolverlo con
  `deallocatePacket`). La acción, ante DA multicast, duplica el paquete
  (`allocatePacket` + `prepare(pkt, nullptr, 0, 0)` + copia — `duplicatePacket`
  (`0xffffff80015479cc`) es de la subclase PCIe y no enlaza), entrega
  original y duplicado a `IO80211VirtualInterface::stashMulticastPacketPair`
  (`__ZN23IO80211VirtualInterface24stashMulticastPacketPairEP20IO80211NetworkPacketS1_`,
  `0xffffff8002334e20`, exportada en `syms.txt:15742`; llega al almacén del gestor
  `0xffffff8002213b16`) y da el original por consumido (sin completar ni liberar: el stash se queda
  la propiedad). `handleMulticastTimer` (`0xffffff80021c38c4`) llamará entonces al `p2pMulticastTx`
  propio dentro de la ventana AW con los arrays del gestor. Los paquetes stashados NO se devuelven
  por la cola de completado: nunca entraron en ella y su cierre es `deallocatePacket`.
  Distinguir re-alimentados (el flag 0x80 de Broadcom es convención interna del paquete PCIe) no
  hace falta: el `p2pMulticastTx` propio transmite directamente y no reentra en la acción.

## 4. Incorporación al ABI de radio

### 4.1 Prototipo

En `Source/Driver/RTLRadioABI.h`, junto a `RTL_RADIO_FN_TX_80211` (línea 66):

```c
/*
 * One data frame, complete 802.11 (header + body, no FCS): the AWDL data
 * encapsulation is already in place. Sent on the operating channel at OFDM
 * 6 Mb/s with the hardware sequence number; unicast frames expect an ACK,
 * group-addressed frames go without retries. kIOReturnNotReady while the
 * radio is down, kIOReturnBusy while TX waits for a channel,
 * kIOReturnNoResources with no free management buffer. Must not be called
 * from inside a radio hook.
 */
#define RTL_RADIO_FN_TX_DATA     "rtlRadioTxData"   /* p1: const uint8_t *frame, p2: uint32_t *len */
```

`RTL_RADIO_ABI` se mantiene en 2: sólo se añade un nombre, igual que `rtlRadioSetChannel` (las
builds anteriores responden `kIOReturnUnsupported`). El dispatch va en el `callPlatformFunction`
de `Source/Driver/RTL8814Native.cpp` (patrón de los casos 432-512, caso del hook AWDL en 504-506) y
delega en la función nueva del vendor tree.

### 4.2 Implementación en el vendor tree

`int rtl8814native_radio_tx_data(const uint8_t *frame, uint32_t len)` en `Source/Driver/AWDLTx.c`,
clon de `rtl8814native_radio_tx_80211` (`AWDLTx.c:21-61`) con cuatro cambios:

1. Aceptar tramas de datos: fuera el filtro `GetFrameType(frame) != WIFI_MGT_TYPE`
   (`AWDLTx.c:32-33`); validar `GetFrameType(frame) == WIFI_DATA_TYPE` (`wifi.h:75`) y To DS/From DS
   a 0 opcionalmente.
2. Tasa fija explícita: `pattrib->rate = IEEE80211_OFDM_RATE_6MB` (`ieee80211.h:772`) en lugar del
   `pmlmeext->tx_rate` de `update_mgntframe_attrib` (`Source/rtl8814au/core/rtw_mlme_ext.c:7672`).
3. Sin `SetSeqNum` (`AWDLTx.c:50-51`): el descriptor activa la secuencia de hardware
   (`SET_TX_DESC_HWSEQ_EN` con `qos_en = 0`, `rtl8814au_xmit.c:112-117`;
   `update_mgntframe_attrib` pone `qos_en = _FALSE` en `rtw_mlme_ext.c:7677`).
4. Reintentos por destino: `pattrib->retry_ctrl = _FALSE` si `IS_MCAST(GetAddr1Ptr(pframe))`
   (patrón ya en `AWDLTx.c:54-55`).

El resto de la receta se conserva: `alloc_mgtxmitframe` (`rtw_mlme_ext.c:7496`,
`frame_tag = MGNT_FRAMETAG` en 7519), copia de los bytes a `buf_addr + TXDESC_OFFSET`,
`update_mgntframe_attrib` (`rtw_mlme_ext.c:7616`), `pattrib->last_txcmdsz = len`,
`dump_mgntframe` (`rtw_mlme_ext.c:7734`) → `rtw_hal_mgnt_xmit` (`hal_intf.c:816`) →
`rtl8814au_mgnt_xmit` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:896-899`) →
`rtw_dump_xframe` (`rtl8814au_xmit.c:369`), que llama `update_txdesc` por fragmento (`:406`),
elige la cola USB con `rtw_get_ff_hwaddr` (`:415`) y escribe el puerto USB con `rtw_write_port`
(`:427`). El PMF hook de `dump_mgntframe` (`rtl_pmf_protect_mgnt`,
`Source/rtl8814au/core/rtw_mlme_ext.c:7723-7732`) no altera tramas sin clave PMF aplicable `?`.

### 4.3 Cola (QSLT), tasa, reintentos y macid

| Elección | Valor | Citas |
|---|---|---|
| QSLT (fase 1) | `QSLT_MGNT` 0x12 | `update_mgntframe_attrib`, `rtw_mlme_ext.c:7632`; constante en `Source/rtl8814au/include/hal_com.h:262`; `rtw_get_ff_hwaddr` → `MGT_QUEUE_INX` (`Source/rtl8814au/core/rtw_xmit.c:4390-4392`) |
| Tasa | OFDM 6 Mb/s fija: `USE_RATE = 1` + `TX_RATE = MRateToHwRate(IEEE80211_OFDM_RATE_6MB)` (`DESC_RATE6M` = 0x04) | rama `MGNT_FRAMETAG`: `SET_TX_DESC_USE_RATE` (`rtl8814au_xmit.c:229`) y `SET_TX_DESC_TX_RATE` (`:232`); `MRateToHwRate` en `Source/rtl8814au/hal/hal_com.c:489`; `DESC_RATE6M` en `hal_com.h:39` |
| Reintento unicast | límite 6 (`retry_ctrl = _TRUE`) | `rtl8814au_xmit.c:259-261` (`RETRY_LIMIT_ENABLE` + `DATA_RETRY_LIMIT 6`) |
| Reintento multicast | límite 12 (`retry_ctrl = _FALSE`); A1 de grupo no recibe ACK | `rtl8814au_xmit.c:262-263`; sin ACK por A1 grupo `?` (decisión del MAC, no hay cita en el árbol) |
| macid | `RTW_DEFAULT_MGMT_MACID` = 1 | `Source/rtl8814au/include/drv_types.h:806`, fijado en `rtw_mlme_ext.c:7631`; con `USE_RATE = 1` no se consulta la tabla de adaptación de tasa, y `EN_DESC_ID`/CAM no se toca en esta rama (sólo `fill_txdesc_force_bmc_camid` bajo `CONFIG_CONCURRENT_MODE`, `rtl8814au_xmit.c:125-128`, desactivado en `Source/rtl8814au/include/autoconf.h:113`) → macid inerte salvo el `raid` de PHYDM (`rtw_get_mgntframe_raid`, `rtw_mlme_ext.c:7667-7671`) |

Justificación de la cola: la vía completa de gestión ya está probada en hardware con los action
frames (commits `4ac3340`, `cceb743`), el EP de gestión no compite con los datos de `en2`, y la rama
`MGNT_FRAMETAG` del descriptor da tasa fija y límite de reintentos sin tocar nada más. El camino de
datos puro (`DATA_FRAMETAG`, `rtl8814au_xmit.c:121-224`, `QSLT_BE` 0x0 → `BE_QUEUE_INX`,
`rtw_xmit.c:4364-4368`, `do_queue_select` en `rtw_xmit.c:4401`) es la mejora de rendimiento
posterior (agregación, tasas por RA) y exige construir el `pkt_attrib` de datos completo
(`psta`, `ether_type`, ramas EAP/ARP de `:140-224`); queda fuera de esta spec.

### 4.4 El puerto 1 y el descriptor TX

El puerto 1 (`rtlRadioAwdlPort`, `AWDLTx.c:77-102`) no afecta al descriptor TX: `update_txdesc`
(`rtl8814au_xmit.c:60-305`) no lee `REG_MACID1`/`REG_BSSID1` ni los bits de tipo de red del puerto 1;
los únicos campos dependientes de puertos del árbol (`fill_txdesc_force_bmc_camid`) están bajo
`CONFIG_CONCURRENT_MODE`, desactivado. La trama sale por el canal operativo del radio (aparcado en
el canal social por `rtlRadioSetChannel`, `AWDLTx.c:111-127`, o en el canal del BSS). El puerto 1
sólo aporta RX: ACK de hardware para las tramas unicast que los pares envían a la MAC de `awdl0`
(`AWDL_RTL8814_RX_ACK.md` §2.4; `RCR_CBSSID_DATA` se mantiene limpio mientras el puerto esté
activo, `AWDLTx.c:100`).

## 5. Cambios por kext

### 5.1 `RTL8814AirLayer` (`Source/AirLayer`)

1. **Sustituir la acción** (`AWDLInterface.cpp:289-303`): nueva `RTLAwdInterface::txDataAction` con
   la misma firma que `txNoop` (§2.1). Por paquete: `getDataVirtualAddress() + getDataOffset()` y
   `getDataLength()` (patrón de lectura de la capa STA, `RTLAirLayer.cpp:418-421`), DA en +0;
   construir la trama de §3.2 en un buffer lineal propio (≤ 1468 B); llamar a la radio
   (`callPlatformFunction(RTL_RADIO_FN_TX_DATA, …)` con `_radioSvc`, patrón de `sendAf`,
   `AWDLInterface.cpp:924-938`); contar resultado. Después devolver **todos** los paquetes por
   `_txCompQ` en tandas de 64 (`n < 128`, §2.4) y `return (IOReturn)count`. Fallo de
   `enqueuePackets` → `deallocatePacket` de esa tanda (como `txNoop`, `AWDLInterface.cpp:300-301`).
   Guardas: sin `_txAllowed` (boot-arg `rtlawdl_tx`) o radio ausente → devolver los paquetes sin
   transmitir y contar (nunca retornar 0 con pendientes ni un error, §2.2).
2. **Contador de secuencia por DA**: tabla pequeña en la interfaz (p. ej. 16 entradas
   `{mac[6], u16 seq}` con búsqueda lineal y sustitución LRU), inicializada a 0, valor usado antes
   de incrementar, envoltura a 0x0FFF, nibble AC a 0 (§3.2). Bajo `_txLock`.
3. **Multicast fase 1**: DA multicast → transmitir inmediato con contador propio; contador
   `mcast` aparte en las estadísticas (§6).
4. **Multicast fase 2 (opcional, después de ver la fase 1 con par real)**: overrides de
   `+0xF08`/`+0xF10`/`+0xF18` en `RTLAwdInterface` (las declaraciones del shim ya fijan los
   offsets, `AWDLInterface.h:173-176`) + duplicado propio + `stashMulticastPacketPair`, según §3.4.
5. **No tocar** los paquetes de entrada: la familia no exige status ni flags (§2.5); tampoco hace
   falta `setLinkHeaderLength` en los paquetes del kernel (sólo en paquetes propios, §2.6).
6. Boot-args: reutilizar `rtlawdl_tx` como interruptor general de TX (hoy gobierna los action
   frames, `AWDLInterface.cpp:126-128`); los datos salen con él. No añadir interruptores nuevos.

### 5.2 `RTL8814Native` (`Source/Driver`, `Source/rtl8814au`)

1. `RTLRadioABI.h`: `RTL_RADIO_FN_TX_DATA` + comentario (§4.1).
2. `AWDLTx.c`: `rtl8814native_radio_tx_data` según §4.2 (declaración junto a
   `rtl8814native_radio_tx_80211`, línea 16).
3. `RTL8814Native.cpp`: caso `RTL_RADIO_FN_TX_DATA` en el dispatch (patrón 432-512).
4. Nada en `Source/rtl8814au` fuera de lo anterior: la cola de gestión, el descriptor
   `MGNT_FRAMETAG` y el reparto USB ya existen.

Orden de dependencia: 5.2 antes que 5.1 (la acción llama a la radio; sin la función nueva responde
`kIOReturnUnsupported` y la acción sólo cuenta). El paso 5.1.4 es independiente y posterior.

## 6. Logs

Con `airLog` (como el resto de la interfaz), el único canal visible en el nodo RTL8814Native:

| Evento | Línea | Frecuencia |
|---|---|---|
| Lote TX | `awdl: tx data n=%u u=%llu m=%llu fail=%llu drop=%llu` | primera y luego cada 256 lotes |
| Primera trama de cada tipo | hex de los primeros 64 B + `da=%02x:…:%02x len=%u seq=%u ch=%u` | una vez unicast, una vez multicast |
| Fallo de radio | `awdl: radio tx data -> 0x%x` | primeras 16 y luego cada 256 |
| Cola | `awdl: txq stalled` si la acción consume 0 con pendientes | debería no ocurrir; diagnóstico |

El estado del canal/aparcamiento ya lo imprime el tick (`AWDLInterface.cpp:756-757`).

## 7. Pruebas sin par Apple

1. **Estática**: objeto compilado con `nm -u` (los símbolos nuevos del §2.6 deben aparecer como
   indefinidos y resolverse contra el KC; patrón de la auditoría ABI previa); kext construido sin
   cargar.
2. **Ejercicio del stack sin RF válido**: con `awdl0` arriba (`rtlawdl=1 rtlawdl_rx=1 rtlawdl_tx=1`),
   `ping6 -c 3 ff02::1%awdl0` (multicast enlace-local, NDP responde) y `ping6 fe80::1%awdl0`
   (unicast a un inexistente: solicitudes de anuncio). La acción se dispara desde el work loop con
   los paquetes del kernel: verificar en el log el contador de lotes, el hex de la primera trama y
   que el radio responda (fallo esperable si el canal no es social: el tick avisa,
   `AWDLInterface.cpp:787-793`). Esto valida el contrato §2 entero sin depender del éter.
3. **Ciclo de devolución**: comprobar que el pool no se agota (contadores de `txNoop` hoy, de
   `txDataAction` después; sin mensajes de pool lleno) y que `en2` sigue transmitiendo (la cola de
   completado es la misma mecánica).
4. **RF con observador externo**: owlsim/ESP32 en monitor sobre el canal social (guía del par en
   `~/Desktop/AWDLLab`, `AWDL_REFERENCIAS_EXTERNAS.md` §6): la trama debe decodificarse como AWDL-13
   y el campo seq crecer de 1 en 1 por DA; comparar los primeros bytes con el byte de oro de
   `frame.rs:391-400` (`… 30 12 | aa aa 03 00 17 f2 08 00 | 03 04 07 00 00 00 86 dd | …`).
5. **RX propio `?`**: el hook de radio entrega toda trama con A3 = BSSID AWDL; una trama de datos
   propia no suele volver por la radio (el MAC no se recibe a sí mismo sin promiscuo), así que el
   autobucle no está garantizado; si el hardware lo permite, serviría para ver la propia trama en el
   log de RX (`AWDLInterface.cpp:549-552` ya cuenta data).
6. **ACK y reintentos**: con un vecino que ackée (un AP en el canal social no ackée AWDL; sin par
   Apple no hay ACK real) sólo se puede verificar el contador de reintentos por los contadores del
   vendor `?` — queda para la etapa con par.

## 8. Puntos abiertos (`?`)

1. Interior del firmware Broadcom: subtipo QoS elegido para un MSDU único, tasas y duración que
   añade a la cabecera 802.11 (§3.1); las capturas de `AWDL_REFERENCIAS_EXTERNAS.md` §5 son la
   referencia de partida.
2. Semántica del nibble alto del campo seq (AC en Broadcom) para un receptor Apple; con una sola
   cola de envío se emite 0 (§3.2).
3. Duration en el aire: si el MAC RTL rellena el campo o la trama sale con 0 (§3.2).
4. Decisión del MAC sobre ACK para A1 de grupo en la cola de gestión (§4.3) — observable en pruebas
   con par.
5. `kern_packet_tx_completion` con `[colaTxComp+0xE8] = 0` (§2.5): el interior no se ha volcado; la
   evidencia operativa es `en2`.
6. Interior de `rtl_pmf_protect_mgnt` sobre tramas de datos sin claves PMF aplicables (§4.2).
7. Re-alimentación multicast: la fase 2 completa (stash + temporizador + overrides) sin par que
   escuche ventanas no es observable; sólo su mecánica de llamadas.

## 9. Verificación de direcciones

Pasada: copia de la IDB anotada en `Build/ida_awdl/task_tx_datos/boot_awdl_txdatos.i64`, scripts
`verify_tx_datos.py` / `verify2.py` / `verify3.py`, salidas `verify.txt` / `verify2.txt` /
`verify3.txt`. Comprobado:

- **Cabeceras de función** (inicio de función == dirección citada, nombre del símbolo verificado):
  44/44, incluidas todas las de §2.1, §2.4, §2.5, §2.6, §3.1 y §3.4 (`verify.txt`, bloques
  `NAMECHECK`).
- **Volcados completos**: `legacyDequeue` (`0xffffff8002b2c860`–`0xffffff8002b2c986`),
  `enqueuePackets` TXComp (`0xffffff8002b2dbb6`–`0xffffff8002b2dc6d`) y su variante encadenada
  (`0xffffff8002b2dc6e`–`0xffffff8002b2de00`), `completePackets`
  (`0xffffff8002b12b9e`–`0xffffff8002b12cb0`), `withPool` TX (7 parámetros, `0xffffff8002b0ca86`–
  `0xffffff8002b0caab`), `initWithPool` TX (`0xffffff8002b0caac`–`0xffffff8002b0cbce`),
  `getTxDataOffset` (`0xffffff8002b0baba`–`0xffffff8002b0bae3`),
  `refillTxRingForNetworking` (`0xffffff8002b0d3e0`–`0xffffff8002b0d57c`), y en la segunda pasada
  `withPool` de 8 parámetros (`0xffffff8002b0ca62`), `IOSkywalkPacketQueue::initWithPool`
  (`0xffffff8002b08bc8`–`0xffffff8002b08c8d`), `withPool`/`initWithPool` de la cola de completado
  (`0xffffff8002b12860`, `0xffffff8002b129cc`); tercera pasada (`verify3.txt`): la fábrica de 9
  parámetros (`0xffffff8002b0c9b4`–`0xffffff8002b0ca62`, mapeo de parámetros a `initWithPool` vía la
  celda vptr `+0x2A8` en `0xffffff8002b0ca37`) y `checkForWork`
  (`0xffffff8002b2c746`–`0xffffff8002b2c85d`, limpia el bit de estancamiento y llama a
  `packetSubmission`), de la que sale la aclaración del campo `+0xF0` y del `-1` en `+0xB8`.
- **Vtables celda a celda**: `__ZTV15IOSkywalkPacket` (`0xffffff8002b3b510`, celdas `+0x10`–`+0x288`),
  `__ZTV26IOSkywalkTxCompletionQueue` (`0xffffff8002b406f0`, `+0x10`–`+0x2F8`),
  `__ZTV26IOSkywalkTxSubmissionQueue` (`0xffffff8002b3e068`, `+0x10`–`+0x2B8`); slots citados:
  paquete `+0x130`…`+0x1F0` (§2.6), cola de envío `+0x1E8` `packetSubmission(bool)`, `+0x2A8`
  `requestDequeue`, `+0x2B0` `purgePackets` (celda `purgePackets` = `0xffffff8002b0d870`), `+0x2B8`
  `initWithPool`, cola de completado `+0x2A8` `requestEnqueue`, `+0x2B0`/`+0x2B8`
  `enqueuePackets`, `+0x2C0` `initWithPool`.
- **Puntos de instrucción** (`SPOT` en `verify.txt`): los 34 de §3.1 y §2 (inserción del SNAP,
  contador `+0x8A`, secuencia, multicast, constantes `8F2170003AAAAh` y `8F21700h`), todos con el
  texto esperado.
- **Tablas de salto**: ninguna en las funciones volcadas (§2.7).
- **Exportaciones**: `syms_com.apple.iokit.IOSkywalkFamily.txt` (§2.6; `getNextPacket` exportada
  `0x0f`, corrige `AWDL_RX_DATA_TO_SKYWALK.md` §7) y `syms.txt` de IO80211Family
  (`stashMulticastPacketPair` línea 15742, `p2pMulticastTx` 15661, `freeMulticastPacket` 15701,
  `triggerAWDLMulticastTx` 15729).

## 10. Implementación respecto a esta especificación

La TX de datos se implementó en paralelo a esta especificación (`42c04d3`, `5630d49`, `0eb77c2`,
`f2969a4`). El contrato de la acción (§2) se cumple: `RTLAwdInterface::txData` devuelve siempre el
número de paquetes recibidos, nunca un código de error, y los devuelve todos por la cola de
completado en tandas de 64, liberándolos con `deallocatePacket` sólo si esa cola los rechaza.
Diferencias con lo propuesto aquí:

- **ABI**: en lugar de una función `RTL_RADIO_FN_TX_DATA` nueva, `rtlRadioTx80211` acepta también
  tramas de datos con A3 = BSSID AWDL (sin `To/From DS`, sin bit de protección). Un driver anterior
  responde `kIOReturnBadArgument` y la capa apaga la TX de datos una vez.
- **Momento del envío**: la acción no transmite. Copia cada trama a un anillo de 32 y el tick de AW
  las envía mientras está abierta la ventana de una franja servida, 4 por AW como máximo y ninguna en
  el último AW de la franja; caducan a los 1,2 s.
- **Tasa**: OFDM 12 Mb/s para datos y 6 Mb/s para las action frames al BSSID AWDL, nunca CCK; los
  datos se rechazan con `kIOReturnNoResources` cuando quedan menos de 16 de los 32 marcos de gestión
  libres.
- **Secuencia 802.11**: la pone el hardware (`HWSEQ_EN` para tramas sin QoS); el contador por destino
  sólo va en la cabecera AWDL, con 12 bits y sin el nibble de AC.
- **Gestor**: cada paquete pasa por `IO80211AWDLPeerManager::preQueuePacketCheckQuietMode`
  ([AWDL_TX_HOOKS_POR_PAQUETE.md](AWDL_TX_HOOKS_POR_PAQUETE.md) §2).
- **Multicast**: sin el modelo de guardado de §3.4; sale por el mismo anillo.
