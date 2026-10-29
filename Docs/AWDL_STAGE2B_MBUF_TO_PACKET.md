# Etapa 2b: del mbuf del stack al `IOSkywalkPacket` de `awdl0`

Cierra el punto abierto de los pasos 3–4 de la §6 de `Docs/AWDL_STAGE1_STAGE2_PUSH_DESIGN.md`: qué debe
hacer exactamente el `if_output` propio (`rtlAwdOutput(__ifnet *, __mbuf *)`, instalado por el campo
`+0x40` de `ifnet_init_eparams` desde la celda `0x8E0`) para que el frame que la pila le entrega llegue a
la acción de la `IOSkywalkTxSubmissionQueue` de `awdl0`. La respuesta corrige el diseño: el kick
`packetSubmission(false)` no sirve y la cola no acepta paquetes del driver; el patrón con soporte en el
desensamblado es la llamada directa a la acción con paquetes del pool propio, que ya usa Broadcom para el
multicast AWDL. Base: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin 25.6.0,
md5 `f489ff8be80e6013a1bf47561dcaef45`), lectura del desensamblado, del pseudocódigo y de volcados de
vtable sobre una copia de la base de datos anotada del kernelcache, y volcados de símbolos de `Docs/re/io80211_25.6.0/`.
Direcciones = VA del KC. Nada se ha enlazado, cargado ni ejecutado; lo no visto va con `?`.

## 1. Respuesta

**No existe ninguna vía para «encolar» un paquete del driver en la `IOSkywalkTxSubmissionQueue`.** La cola
de envío sólo se llena desde el kernel:

1. `enqueuePackets` es un virtual **puro** de `IOSkywalkPacketQueue` (celdas `+0x2A0`/`+0x2A8` de su
   vtable a cero) y la subclase de envío **no lo implementa**: en `__ZTV26IOSkywalkTxSubmissionQueue`
   esas celdas contienen `purgePackets` (`0xFFFFFF8002B0D870`) y `initWithPool` (`0xFFFFFF8002B0CAAC`).
   Las implementaciones de `enqueuePackets` que existen (RX/TX completion, RX submission) son de colas de
   dirección opuesta.
2. `IOSkywalkTxSubmissionQueue::packetSubmission(bool)` (`0xFFFFFF8002B2CD68`) saca los paquetes en sus
   dos modos «networking» con la **misma** llamada al kernel,
   `kern_netif_queue_tx_dequeue` (`0xFFFFFF80009E6120`): `refillTxRingForNetworking`
   (`0xFFFFFF8002B0D3E0`, llamada en `0xFFFFFF8002B0D41F`) para el modo anillo y
   `getPacketListForNetworking` (`0xFFFFFF8002B0D57C`, llamada en `0xFFFFFF8002B0D5BC`) para el modo
   lista. La «lista» (cabeza/cola en los qwords `+0x108`/`+0x110`, recuento en el dword `+0x150`) sólo se
   llena desde esa misma dequeue del kernel, nunca desde el driver.
3. `kern_netif_queue_tx_dequeue` drena el `ifclassq` del `ifnet` (`ifclassq_dequeue`,
   `0xFFFFFF800070E9D0`): los mbufs que el driver pudiera poner ahí se convierten a `kern_packet` dentro
   del kernel. No hay `kern_netif_queue_tx_enqueue`: en la tabla del kernel sólo existen
   `kern_netif_queue_rx_enqueue` (`0xFFFFFF80009E5FD0`), `kern_netif_queue_tx_dequeue`
   (`0xFFFFFF80009E6120`) y `kern_netif_queue_get_service_class` (`0xFFFFFF80009E6490`), los tres en
   `com.apple.kpi.private`, como `ifnet_enqueue` (`0xFFFFFF80005E5ED0`) — no enlazan desde un kext de
   terceros (§7), de modo que ni siquiera la vía «re-encolar en el `ifclassq` y patear» es enlazable.
4. Además, `packetSubmission` sólo tira cuando hay notificaciones del kernel: el volumen se calcula como
   `dword[q+0x130] − xchg dword[q+0x134]` (`0xFFFFFF8002B2CE1D`–`0xFFFFFF8002B2CE23`); con ambos a cero
   (cola de driver sin tráfico del kernel) ni siquiera llega a llamar a la dequeue y devuelve
   `0xE00002DB` (`0xFFFFFF8002B2CF11`).

**El patrón con soporte en el desensamblado es la llamada directa a la acción.** Broadcom lo usa para el
multicast AWDL: `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::p2pMulticastTx(IO80211NetworkPacket**,
unsigned int)` (`0xFFFFFF8001541D64`) carga `vtable+0x310` (su `dequeuePackets`, la acción registrada en
la fábrica) y la llama con `(this, this, paquetes, n, 0)` — paquetes que ella misma sacó del pool. El
camino RX del código propio de la capa (`RTLAirLayer.cpp::rxFrames`) ya aplica la mitad inversa del mismo
contrato: `allocatePacket` + `prepare` + copia + `setDataOffsetAndLength` + cola de completado.

**Secuencia mínima de `rtlAwdOutput`** (detalle en §5): convertir el mbuf en un paquete del pool TX
(`allocatePacket` + `prepare` + copia + `setDataOffsetAndLength`), liberar el mbuf con `mbuf_freem` y
llamar a la acción de la cola directamente con el array de paquetes; la acción transmite y devuelve los
paquetes por la cola de completado TX (mecánica ya escrita en `txNoop`, `Source/AirLayer/AWDLInterface.cpp:255`).
El valor de retorno hacia dlil es un errno: 0 = aceptado. El `packetSubmission` del diseño anterior sobra.

## 2. El camino normal: del mbuf del stack a la acción

Tabla del pipeline que la familia monta para un `ifnet` de proveedor (todas las cabezas verificadas,
§10):

| Paso | Evidencia | Hecho |
|---|---|---|
| Entrada | `_dlil_output_handler` `0xFFFFFF80005EF500` (14 bytes) | Llama a `[ifp+0x258]` y devuelve su resultado tal cual. |
| Encolado | `ifnet_enqueue` → `ifclassq` | Los mbufs entran en el `ifclassq` como ítems de tipo 1 (cadena de mbufs, enlace en `+8`); el tipo 2 (cadena de paquetes, enlace en `+168`) convive en el mismo código (`sub_FFFFFF800070EA80`, con las aserciones de `xnu/bsd/net/classq/classq_subr.c`). |
| Kick | `nx_netif_na_txsync` → `nxp_tx_qset_notify` → `[vtable+0x1D8](cola, 1)` | `packetSubmission(true)`, la variante que usa el kernel (ya en `AWDL_STAGE1_TXPUSH_LLINK.md` §4). |
| Dequeue del kernel | `kern_netif_queue_tx_dequeue` `0xFFFFFF80009E6120` | Puerta de estado del enlace: `cmp byte [r9+3Ch], 2` → retorno 6 (`ENXIO`) en `0xFFFFFF80009E614E`; segunda prueba `cmp byte [rsi+3Ch], 1` en `0xFFFFFF80009E6202`. Llama a `ifclassq_dequeue` y devuelve el primer handle en el qword de salida. |
| Conversión a paquete de la familia | `refillTxRingForNetworking` | Por handle: `IOSkywalkPacketBufferPool::getPacketWithHandle(pool, handle)` (`0xFFFFFF8002B0589C`, indexa el tracker con `kern_packet_get_object_index`), slot `+0x1D8` del paquete (`acquireWithPacketHandle(handle, 0)`, llamada en `0xFFFFFF8002B0D486`), slot `+0x188` (`prepareWithQueue(cola, 1, 0)`, llamada en `0xFFFFFF8002B0D49C`), opcionalmente trace/BPF, y `IOSkywalkPacketTable::producePacket` (`0xFFFFFF8002B09756`, llamada en `0xFFFFFF8002B0D4FA`). |
| Entrega | `legacyDequeue` `0xFFFFFF8002B2C860` | Exige `byte+0x28 & 1` (habilitada) y `byte+0xCC & 1` (`0xFFFFFF8002B2C87B`); carga la acción de `[this+0x20]` (`0xFFFFFF8002B2C888`) y la llama con `(owner, this, tabla+8*índice, pendientes, refCon)` — una porción contigua del array de la tabla de paquetes (`+0xE8`, `shl rdx,3; add rdx,[rcx+8]` en `0xFFFFFF8002B2C8C1`–`0xFFFFFF8002B2C8C5`). El recuento devuelto avanza el índice de lectura; 0 deja el bit de estancamiento en `+0x13C`. |

La copia mbuf→`kern_packet` y la liberación del mbuf ocurren dentro del subárbol del kernel
(`ifclassq_dequeue` → `sub_FFFFFF800070EA80` → `sub_FFFFFF800070EE10` y la operación de la disciplina);
la instrucción concreta de cada una no se ha localizado (`?`). Para el diseño de la etapa 2b no afecta:
ese camino queda fuera cuando el `output` propio sustituye a `_ifnet_enqueue`.

## 3. Vías cerradas, con evidencia

| Vía | Por qué no |
|---|---|
| `enqueuePackets` sobre la cola de envío | Virtual puro de la base (`__ZTV20IOSkywalkPacketQueue` celdas `+0x2A0`/`+0x2A8` = 0); la subclase TX pone ahí `purgePackets`/`initWithPool`. No existe símbolo `IOSkywalkTxSubmissionQueue::enqueuePackets` en el volcado del entry (§7). |
| `packetSubmission(false)` como «notificar la cola» | Requiere EventSource habilitada (`byte+0x28 & 1`, `0xFFFFFF8002B2CDC6`) y el bit de arranque `byte+0xCC & 1` (`0xFFFFFF8002B2CDCE`); estado `+0x13C & 5` → `0xE00002D5`. Con ambos bits correctos, el volumen vale `dword+0x130 − xchg dword+0x134`; sin notificaciones del kernel devuelve `0xE00002DB` sin tirar (`0xFFFFFF8002B2CF11`). Y aunque tirara, sacaría del `ifclassq` del kernel, no de memoria del driver. |
| Modo lista (opción `0x8` de la fábrica) | Sólo cambia el consumidor: `byte+0x142 & 2` (`0xFFFFFF8002B2CEBE`) desvía a `getPacketListForNetworking`, que llama al mismo `kern_netif_queue_tx_dequeue` (`0xFFFFFF8002B0D5BC`) y encadena lo que el kernel devuelve. |
| `kern_netif_queue_tx_enqueue` | No existe en el KC (sólo `rx_enqueue`/`tx_dequeue`/`get_service_class`, todos `com.apple.kpi.private`). |
| Re-encolar en el `ifclassq` (`ifnet_enqueue`) y patear | `ifnet_enqueue` está en `com.apple.kpi.private` (`0xFFFFFF80005E5ED0`): no enlaza (§7). |
| Conversor mbuf→paquete de la familia | No existe: el tirón de mbufs del PeerManager, `IO80211VirtualInterface::dequeueTxPackets` (`0xFFFFFF80023313B2`), no tiene una sola referencia de código en el KC (barrido de xrefs vacío) y sus mbufs están pensados para ir directos a la radio. |

Quién escribe el bit de arranque (`byte+0xCC`) queda sin cerrar: un barrido de bytes sobre todo el texto
del entry `IOSkywalkFamily` (`0xFFFFFF8002AFE000`–`0xFFFFFF8002B46000`) no halló ninguna escritura a ese
desplazamiento; el escritor presumiblemente está en el kernel al adjuntar la cola al `netif_queue` (`?`).
No afecta a la vía directa (§5), que no pasa por `legacyDequeue`; sí explica que en la etapa 1 ningún kick
de la familia pudiera mover una cola de driver.

## 4. El patrón con soporte: acción directa

`p2pMulticastTx` (`0xFFFFFF8001541D64`, cabeza verificada; bytes en §10):

```
0xffffff8001541d64  push    rbp
0xffffff8001541d65  mov     rbp, rsp
0xffffff8001541d68  mov     ecx, edx              ; count
0xffffff8001541d6a  mov     rdx, rsi              ; paquetes
0xffffff8001541d6d  mov     rax, [rdi]            ; vptr
0xffffff8001541d70  mov     rax, [rax+310h]       ; dequeuePackets (la acción)
                    mov rsi, rdi ; xor r8d, r8d ; pop rbp ; jmp rax   ; llamada de cola con (this, this, paquetes, count, 0)
```

Es decir: la familia no ofrece una puerta para meter paquetes en la cola de envío, pero **la acción es del
driver** y llamarla directamente con paquetes del pool propio es exactamente lo que hace la subclase de
Broadcom. El paquete directo nunca pasa por la tabla de paquetes ni por `acquireWithPacketHandle` (no hay
handle externo que atar): es un paquete normal del pool, con su ciclo de vida completo gestionado por las
API que la capa ya enlaza (§7). La mitad RX del mismo contrato ya corre en producción en la interfaz STA
(`RTLAirLayer.cpp::rxFrames`: `allocatePacket` → `prepare` → copia → `setDataOffsetAndLength` →
`enqueuePackets` por tandas).

## 5. Secuencia mínima de `rtlAwdOutput(__ifnet *, __mbuf *)`

| Paso | Llamada (símbolo enlazable, §7) | Evidencia y notas |
|---|---|---|
| 0 | Guardas: datapath habilitado, `_txPool`/`_txSubQ` no nulos | El handler corre en el hilo de la pila (dlil), no en el work loop. |
| 1 | `IOSkywalkPacketBufferPool::allocatePacket(&pkt, 1)` | `0xFFFFFF8002B058EE`: delega en el slot `+0x130` del pool (el alloc concreto). La opción 1 es el `alloc` sin dormir del `kern_pbufpool`; con el pool lleno devuelve error → liberar el mbuf y devolver errno. |
| 2 | `IOSkywalkPacket::prepare(pkt, _txSubQ, 1, 0)` | `0xFFFFFF8002B0658A`: si `byte+0x58 & 2` ya está preparado devuelve 0; si no llama a `prepareWithQueue` (slot `+0x188`) con la dirección que `getDirection` lee de la cola (`[cola+0xB4]`, `0xFFFFFF8002B08EEC`) — 1 para una cola de envío. Sin `prepare` el paquete no tiene tabla de buffers (`getDataVirtualAddress` falla; probado en la capa STA) y tampoco podría completarse: `complete` exige el mismo bit (`byte+0x58 & 2`, `0xFFFFFF8002B066AE`). |
| 3 | Copia del frame al buffer del paquete | Destino: `IOSkywalkPacket::getDataVirtualAddress()` + offset elegido; origen: el mbuf con `mbuf_copydata` (recorre la cadena). Offset recomendado: 64, el mismo headroom que el `RegistrationInfo` lleva en `+0x3C` (`Docs/AWDL_STAGE1_LIFECYCLE_CHECKS.md`) y que el descriptor del bus de Broadcom copia al tracker del pool; deja sitio para anteponer cabecera de enlace sin segunda copia. `?` el offset exacto que usa el kernel en su propia conversión (§2) no se ha trazado a instrucción. |
| 4 | `IOSkywalkPacket::setDataOffsetAndLength(64, len)` | `0xFFFFFF8002B2C538`: valida `off ≤ tamaño de buffer` y `off+len ≤ buffers×tamaño` (el tamaño está en el descriptor del pool, campo `+48`); sin buffers devuelve `0xE00002F0`. Con `off = 64` la acción lee el frame en `base + 64` (`getDataVirtualAddress + getDataOffset`). |
| 5 | (opcional) `IOSkywalkNetworkPacket::setLinkHeaderLength(pkt, 14)` | `0xFFFFFF8002B07180`: delega en `kern_packet_set_link_header_length`; el kernel lo usa para el offload de cabecera. |
| 6 | `mbuf_freem(m)` | En todos los caminos, éxito y fallo (§6). |
| 7 | Llamada directa a la acción: `action(owner, _txSubQ, &pkt, 1, refCon)` | Misma firma que usa `legacyDequeue` (`0xFFFFFF8002B2C888`): `(owner=[cola+0x18], cola, IOSkywalkPacket* const*, recuento, refCon=[cola+0x58])`. En la etapa 1 la acción es `txNoop` (`AWDLInterface.cpp:255`); la acción real de la etapa 2b la sustituye. La llamada es síncrona en el hilo de la pila; si la radio exige el contexto del work loop, el handler debe saltar con `IOWorkLoop::runAction` y retener el paquete hasta el salto (`?` decisión de diseño de la radio, no fijada por el KC). |
| 8 | La acción transmite y devuelve los paquetes | Por `IOSkywalkTxCompletionQueue::enqueuePackets(const IOSkywalkPacket*, uint, uint)` (`0xFFFFFF8002B2DBB6`, tandas con recuento < capacidad del dword `+0xAC`) y devuelve el recuento consumido. La completión (`completePackets`, `0xFFFFFF8002B12B9E`): por paquete `getDataLength` (slot `+0x140`, `0xFFFFFF8002B12BDD`), `getSourceQueue()` (`0xFFFFFF8002B12BE9`) y, si hay cola origen, `notifySrcQueue(false, origen, longitud)` — que aterriza en el `packetCompletion` de la cola de envío (`0xFFFFFF8002B0D758`: sólo contadores y `signalWorkAvailable` periódico); después el slot `+0x198` del paquete (`completeWithQueue`) y, si la cola tiene tabla (`+0xE8`), `kern_packet_tx_completion(handle)` con el handle `kern` que el paquete lleva en `+0x28` desde el `allocatePacket`. Al acabar el lote devuelve los paquetes al pool por el slot `+0x148` de la vtable del pool (la firma de `deallocatePackets`, `0xFFFFFF8002B2C128`) y llama a `notifySrcQueue(true, nullptr, 0)`. El ciclo completo funciona en la interfaz STA. |
| 9 | `return 0` | Errno de aceptación (§6). |

Códigos que la cola devuelve por si el driver consulta estado o usa `requestDequeue(void*, unsigned int)`
(`0xFFFFFF8002B2CA7C`; Broadcom lo llama desde `enableDatapath`): sin `netif_queue` (`+0x78` nulo,
`0xFFFFFF8002B2CA9A`) devuelve `0xE00002D8`; con ella llama a `packetSubmission(false)` por el slot
`+0x1D8` (`0xFFFFFF8002B2CAB4`). `packetSubmission(bool)` devuelve: `0xE00002D8` (no habilitada, camino
`true`), `0xE00002D7` (camino `false`: no habilitada `0xFFFFFF8002B2CDC0` o sin bit de arranque
`0xFFFFFF8002B2CDCE`), `0xE00002D5` (estado `+0x13C & 5`), `0xE00002DB` (sin notificaciones del kernel,
`0xFFFFFF8002B2CF11`). `enable` (`0xFFFFFF8002B0CC76`) sólo llama al slot `+0x1D8` si `+0x78` no es nulo y
`+0x142 & 4 == 0` (`0xFFFFFF8002B0CC9A`–`0xFFFFFF8002B0CCB2`); `checkForWork` (`0xFFFFFF8002B2C746`)
llama al mismo slot con 0.

## 6. Vida del mbuf y valor de retorno

- **El mbuf es propiedad del `if_output` desde la llamada.** Evidencia binaria: el handler propio de la
  familia lo libera él mismo — `IOSkywalkNetworkBSDClient::if_output` (`0xFFFFFF8002B002CE`) hace
  `mbuf_freem` (`0xFFFFFF8002B002DA`) y devuelve 19 (bytes `b8 13 00 00 00` en `0xFFFFFF8002B002E0`) — y
  el camino normal entrega el mbuf al `ifclassq`, desde donde lo libera el kernel tras convertirlo
  (`?` instrucción no localizada, §2). Ningún tramo posterior vuelve a mirar el mbuf.
- **El handler propio debe liberar siempre** (`mbuf_freem` libera la cadena completa): tras la copia al
  paquete (éxito), y en cada fallo (pool lleno, `prepare` fallido, paquete devuelto). Los KPI necesarios
  (`mbuf_freem`, `mbuf_copydata`, `mbuf_pkthdr_len`, `mbuf_nextpkt`, `mbuf_setnextpkt`) están en
  `com.apple.kpi.bsd` (§7).
- **El retorno es un errno**: el thunk `_dlil_output_handler` (`0xFFFFFF80005EF500`) devuelve el valor del
  handler tal cual y el handler de la familia devuelve 19 (`ENODEV`) al descartar. 0 = frame aceptado;
  distinto de 0 = errno del fracaso (con el mbuf ya liberado). `?` el tratamiento fino del errno en
  `dlil_output` (quién lo consume) no se ha perseguido; para el diseño basta el contrato del handler de
  la familia.

## 7. Símbolos enlazables

`Tools/awdl_recheck_symbols.py` con los cinco volcados de `Docs/re/io80211_25.6.0/` y la lista de
candidatos de la etapa 2b: **142 de 143 OK**, todos definidos con tipo `0x0f` en el entry
`com.apple.iokit.IOSkywalkFamily` (o ya importados por el binario construido). El único MISSING fue un
error de la lista de prueba (`8setNextPacket` en vez de `13setNextPacket`); el símbolo real
`__ZN15IOSkywalkPacket13setNextPacketEPS_` está en `0xFFFFFF8002B068E2`, `0x0f`.

| Papel en el handler | Símbolo | Dirección |
|---|---|---|
| Crear cola de envío con acción (ya enlazado) | `IOSkywalkTxSubmissionQueue::withPool(pool, cap, flags, owner, acción, refCon, opciones)` | `0xFFFFFF8002B0CA86` |
| Sacar paquete del pool | `IOSkywalkPacketBufferPool::allocatePacket(IOSkywalkPacket**, uint)` | `0xFFFFFF8002B058EE` |
| Preparar el paquete | `IOSkywalkPacket::prepare(IOSkywalkPacketQueue*, u64, uint)` | `0xFFFFFF8002B0658A` |
| Datos y longitudes | `getDataVirtualAddress` / `getDataOffset` / `getDataLength` / `getBufferSize` / `getPacketBufferCount` / `setDataOffsetAndLength` | `0xFFFFFF8002B06518` / `0xFFFFFF8002B2C5E2` / `0xFFFFFF8002B064D0` / `0xFFFFFF8002B064B8` / `0xFFFFFF8002B064C6` / `0xFFFFFF8002B2C538` |
| Cabecera de enlace (opcional) | `IOSkywalkNetworkPacket::setLinkHeaderLength(uint8)` | `0xFFFFFF8002B07180` |
| Devolver paquete al pool en los fallos previos a la acción | `IOSkywalkPacketBufferPool::deallocatePacket(IOSkywalkPacket*)` | `0xFFFFFF8002B2C0DE` |
| Devolución tras transmitir | `IOSkywalkTxCompletionQueue::enqueuePackets(const IOSkywalkPacket*, uint, uint)` | `0xFFFFFF8002B2DBB6` |
| Liberar el mbuf | `mbuf_freem` (`com.apple.kpi.bsd`) | `0xFFFFFF80008DF8E0` |
| Copiar del mbuf | `mbuf_copydata`, `mbuf_pkthdr_len`, `mbuf_nextpkt`, `mbuf_setnextpkt` (KPI BSD) | — |

Nota sobre `withPool`: el séptimo parámetro es la palabra de opciones que `initWithPool` consume
(`test r12b, 8` → `or byte [cola+0x142], 2` en `0xFFFFFF8002B0CB2B`–`0xFFFFFF8002B0CB31`; `test r12b, 2`
→ `or byte [cola+0x142], 4` en `0xFFFFFF8002B0CB38`–`0xFFFFFF8002B0CB3E`); el desensamblado de la
fábrica de 7 parámetros (`0xFFFFFF8002B0CA86`, `mov r10d, [rbp+arg_0]` en `0xFFFFFF8002B0CA90`) lo
reenvía tal cual. La capa pasa 0: modo anillo, sin ABFS propio. Ninguna opción crea una puerta de
inyección (§3).

KPI del kernel, del plist `SymbolsSets` de `__LINKINFO,__symbolsets` del propio KC (binario en el offset
de fichero `0x3300000`, 9 listas): `mbuf_*` (freem, copydata, copyback, allocpacket, pkthdr_setlen,
pkthdr_len, nextpkt, setnextpkt, data, len) = `com.apple.kpi.bsd`, enlazables. En cambio
`ifnet_enqueue`, `ifnet_dequeue`, `if_qflush`, `dlil_output`, `dlil_output_handler`,
`kern_netif_queue_tx_dequeue`, `kern_netif_queue_rx_enqueue`, `kern_pbufpool_*`, `kern_packet_*` y
`kern_buflet_*` están en `com.apple.kpi.private`: no enlazan desde un kext de terceros (el mismo criterio
de `Docs/AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`).

## 8. Correcciones a `AWDL_STAGE1_STAGE2_PUSH_DESIGN.md` §6

| Paso del diseño | Corrección |
|---|---|
| Paso 3: «encolar el mbuf en el anillo propio del proveedor» | No hay anillo encolable por el driver: la cola de envío se llena sólo desde `kern_netif_queue_tx_dequeue` (§3). Sustituir por: paquete del pool TX (pasos 1–4 de §5) y llamada directa a la acción (paso 7), el patrón `p2pMulticastTx` de Broadcom (§4). |
| Paso 3: «notificar la cola de envío con `packetSubmission(false)`» | Sobra y es inefectiva: sin notificaciones del kernel devuelve `0xE00002DB` sin tirar (`0xFFFFFF8002B2CF11`), y el camino `false` exige el bit de arranque `+0xCC` sin escritor conocido en la familia (§3). El kick de `AWDL_STAGE1_TXPUSH_LLINK.md` §4 vale para el camino normal (kernel → cola), no para colas servidas por el driver. |
| Paso 4: «poner la acción real en lugar del no-op» | Se mantiene; la firma y el consumo por la completion queue de `txNoop` ya son correctos (`AWDLInterface.cpp:255`). Lo nuevo: la acción se ejecutará en el hilo de la pila y no en el work loop, y debe devolver el recuento consumido (0 estanca la maquinaria de la familia si algún día la cola recibe tráfico del kernel). |
| Pasos 1–2 (instalación del `output` por `params+0x40`) y paso 5–6 (alternativa `0x8E8`/rollback) | Sin cambios. |

## 9. Puntos abiertos (`?`)

1. **Escritor del bit de arranque `byte+0xCC`** de la cola: ninguna función del texto del entry
   `IOSkywalkFamily` lo escribe (barrido de bytes §3); presumiblemente lo escribe el kernel al adjuntar
   la cola al `netif_queue`. Irrelevante para la vía directa, decisivo si algún día se quiere el kick de
   la familia.
2. **Copia mbuf→`kern_packet` y liberación del mbuf en el camino normal**: dentro del subárbol de
   `ifclassq_dequeue` (`0xFFFFFF800070E9D0`) → `sub_FFFFFF800070EA80` → `sub_FFFFFF800070EE10`
   (`classq_subr.c`; ítems tipo 1 mbuf / tipo 2 packet). Sólo afecta al contraste de §2.
3. **Offset de datos que usa el kernel** en los paquetes que produce (el headroom del pool, en el tracker
   `+64` en el bus de Broadcom): el handler propio elige 64; el valor exacto del kernel no se ha trazado.
4. **Devolución al pool en `completePackets`**: el tramo final pasa por el slot `+0x148` de la vtable del
   pool con `(pool, paquetes, recuento)` — la firma de `deallocatePackets` — y por
   `kern_packet_tx_completion`; el interior del slot no se ha decompilado (la pertenencia al pool del
   paquete directo es la misma que la de los paquetes del camino normal, creados igual por
   `allocatePacket`).
5. **Contexto de hilo de la acción real**: la radio RTL decide si transmite directo desde el hilo de la
   pila o salta al work loop; el KC no fija nada (es código propio).
6. **Tratamiento fino del errno de `if_output` en `dlil_output`**: se sabe que es un errno (la familia
   devuelve 19); el consumidor no se ha perseguido.
7. Ya anotado en el diseño previo y vigente: que la maquinaria Skywalk tolere un `ifp+0x258` propio en un
   `ifnet` de proveedor se comprueba en ejecución (hardware).

## 10. Verificación de direcciones

Cabezas comprobadas contra la tabla de funciones de la base de datos (inicio de función == dirección citada):
`0xFFFFFF8002B2CD68` (`packetSubmission(bool)`), `0xFFFFFF8002B0D57C` (`getPacketListForNetworking`),
`0xFFFFFF8002B0D3E0` (`refillTxRingForNetworking`), `0xFFFFFF8002B2C860` (`legacyDequeue`),
`0xFFFFFF8002B2C986` (`listDequeue`), `0xFFFFFF8002B2CA7C` (`requestDequeue`),
`0xFFFFFF8002B0CAAC` (`initWithPool` TX), `0xFFFFFF8002B0CA86` (`withPool` 7), `0xFFFFFF8002B0C9B4`
(`withPool` 10), `0xFFFFFF8002B2DBB6` (`TxCompletionQueue::enqueuePackets`), `0xFFFFFF8002B0D758`
(`packetCompletion`), `0xFFFFFF8002B0658A` (`prepare`), `0xFFFFFF8002B058EE` (`allocatePacket`),
`0xFFFFFF8002B2C538` (`setDataOffsetAndLength`), `0xFFFFFF8002B002CE` (`if_output`),
`0xFFFFFF8001541D64` (`p2pMulticastTx`), `0xFFFFFF8002B08BC8` (`initWithPool` base),
`0xFFFFFF8002B2CB2A` (`packetSubmission(u64*,u32*,u32*)`), `0xFFFFFF8002B0CF84` (`synchronize`),
`0xFFFFFF8002B0CC76` (`enable`), `0xFFFFFF8002B2C746` (`checkForWork`), `0xFFFFFF8002B09016`
(`setKernNetifQueue`), `0xFFFFFF8002B0589C` (`getPacketWithHandle`), `0xFFFFFF8002B12B9E`
(`completePackets`), `0xFFFFFF8002B08EEC` (`getDirection`), `0xFFFFFF80009E6120`
(`kern_netif_queue_tx_dequeue`), `0xFFFFFF800070E9D0` (`ifclassq_dequeue`),
`0xFFFFFF800070EA80` (`sub_70EA80`), `0xFFFFFF80005EF500` (`_dlil_output_handler`),
`0xFFFFFF8002B07180` (`setLinkHeaderLength`), `0xFFFFFF8002B12B9E` (`completePackets`, cabeza
reverificada por `get_func`), `0xFFFFFF8002B066AE` (`complete`), `0xFFFFFF8002B066FA`
(`completeWithQueue`), `0xFFFFFF8002B08EC8`/`0xFFFFFF8002B08EE0` (base
`packetSubmission(u64…)`/`sendNotification`, ambas `return 0xE00002E7`), `0xFFFFFF8002B2CAFE`
(`sendNotification` TX → `kern_channel_notify`).

Citas de instrucción (bytes leídos de la base de datos) dentro de las cabezas anteriores:
`0xFFFFFF8002B2CD88` (`test al,1`), `0xFFFFFF8002B2CDC0` (`mov r15d, 0E00002D7h`),
`0xFFFFFF8002B2CDCE` (`test byte [rbx+0CCh], 1`), `0xFFFFFF8002B2CDE6`–`0xFFFFFF8002B2CDEC`
(`mov eax,[rbx+13Ch]`; `test al,5`), `0xFFFFFF8002B2CE1D`–`0xFFFFFF8002B2CE23`
(`xchg eax,[rbx+134h]`; `mov r15d,[rbx+130h]`), `0xFFFFFF8002B2CEBE` (`test byte [rbx+142h], 2`),
`0xFFFFFF8002B2CED4` (`call refillTxRingForNetworking`), `0xFFFFFF8002B2CEE0` (`call legacyDequeue`),
`0xFFFFFF8002B2CF11` (`mov r15d, 0E00002DBh`), `0xFFFFFF8002B2CF19` (`call getPacketListForNetworking`),
`0xFFFFFF8002B0D41F` (`call kern_netif_queue_tx_dequeue`), `0xFFFFFF8002B0D473`
(`call getPacketWithHandle`), `0xFFFFFF8002B0D486` (`call [rax+1D8h]` = `acquireWithPacketHandle`),
`0xFFFFFF8002B0D49C` (`call [rax+188h]` = `prepareWithQueue`), `0xFFFFFF8002B0D4FA`
(`call producePacket`), `0xFFFFFF8002B0D53E` (`call packetSubmission(uint,uint)`), `0xFFFFFF8002B0D5BC`
(`call kern_netif_queue_tx_dequeue` en `getPacketListForNetworking`), `0xFFFFFF8002B2C86E`
(`test byte [rdi+28h], 1`), `0xFFFFFF8002B2C87B` (`test byte [rdi+0CCh], 1`), `0xFFFFFF8002B2C888`
(`mov r12, [rbx+20h]`, la acción), `0xFFFFFF8002B2C88C` (`mov rcx, [rbx+0E8h]`, la tabla),
`0xFFFFFF8002B2C8C1`–`0xFFFFFF8002B2C8C5` (`shl rdx,3`; `add rdx,[rcx+8]`), `0xFFFFFF8002B2C8C9`
(`or byte [rbx+13Ch], 1`), `0xFFFFFF8002B0CA90` (`mov r10d, [rbp+arg_0]`, opciones de la fábrica),
`0xFFFFFF8002B0CB2B`–`0xFFFFFF8002B0CB31` (`test r12b,8`; `or byte [rbx+142h], 2`),
`0xFFFFFF8002B0CB38`–`0xFFFFFF8002B0CB3E` (`test r12b,2`; `or byte [rbx+142h], 4`),
`0xFFFFFF8002B0CB87`–`0xFFFFFF8002B0CB8C` (`call createPacketTable`; `mov [rbx+0E8h], rax`),
`0xFFFFFF8002B0CC9A`–`0xFFFFFF8002B0CCB2` (`cmp qword [rbx+78h], 0`; `test byte [rbx+142h], 4`;
`call [rax+1D8h]`), `0xFFFFFF8002B2CA9A`–`0xFFFFFF8002B2CAB4` (`cmp qword [rbx+78h], 0`;
`call [rax+1D8h]`), `0xFFFFFF8002B2CADD` (`mov r14d, 0E00002D8h`), `0xFFFFFF8001541D68`–
`0xFFFFFF8001541D70` (`mov ecx,edx`; `mov rdx,rsi`; `mov rax,[rdi]`; `mov rax,[rax+310h]`),
`0xFFFFFF8002B002CE`–`0xFFFFFF8002B002E0` (`test rsi,rsi`; `call mbuf_freem`; `mov eax, 13h`),
`0xFFFFFF8002B0658A` (`test byte [rdi+58h], 2`), `0xFFFFFF8002B058EE`–`0xFFFFFF8002B058FA`
(`mov rax,[rdi]`; `mov rax,[rax+130h]`), `0xFFFFFF8002B058A8` (`call kern_packet_get_object_index`),
`0xFFFFFF8002B07184`–`0xFFFFFF8002B0718F` (`mov rdi,[rdi+28h]`;
`call kern_packet_set_link_header_length`; `mov ecx, 0E00002C2h`), `0xFFFFFF8002B12BDD`
(`call [rax+140h]` = `getDataLength`), `0xFFFFFF8002B12BE9` (`call getSourceQueue`),
`0xFFFFFF8002B08EEC`–`0xFFFFFF8002B08EF0` (`mov eax, [rdi+0B4h]`), `0xFFFFFF80009E614E`
(`cmp byte [r9+3Ch], 2`), `0xFFFFFF80009E6202` (`cmp byte [rsi+3Ch], 1`).

Datos leídos: `__ZTV26IOSkywalkTxSubmissionQueue` (`0xFFFFFF8002B3E068`, 96 celdas desde el vptr:
sin `enqueuePackets`; `+0x1D8` = `packetSubmission(bool)`, `+0x298` = `requestDequeue`, `+0x2A0` =
`purgePackets`, `+0x2A8` = `initWithPool`), `__ZTV15IOSkywalkPacket` (`0xFFFFFF8002B3B510`, 80 celdas:
`+0x140` = `getDataLength`, `+0x158` = `setDataOffsetAndLength`, `+0x188` = `prepareWithQueue`,
`+0x1D8` = `acquireWithPacketHandle`), `__ZTV20IOSkywalkPacketQueue` (`0xFFFFFF8002B3CC70`, 90 celdas:
`+0x2A0`/`+0x2A8` = 0), vptr de `AppleBCMWLANPCIeSkywalkTxSubmissionQueue` (`0xFFFFFF80015ACD00`,
celda `+0x368` = `0xFFFFFF8001541D64`), y el plist `SymbolsSets` del KC (offset de fichero `0x3300000`,
`0x4D6D4` bytes).
