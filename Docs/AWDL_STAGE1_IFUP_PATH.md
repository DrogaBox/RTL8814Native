# Subida de `awdl0` y primer paquete transmitido: qué ejecuta el kernelcache

Traza de lo que ocurre en el kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64) cuando
`ifconfig awdl0 up` pone `IFF_UP` y cuando la pila envía un paquete IPv6 (ND/RS) por la interfaz, con
el código de la etapa 1 ya aplicado (`Source/AirLayer/AWDLInterface.cpp`, commit `fe29719`). Sirve para
predecir el resultado de la prueba T3.5 antes de cargar el kext. Base: kernelcache completo (IO80211Family
e IOSkywalkFamily van en el mismo binario), IDA headless (`idat -A -S`) sobre una copia de
`boot_awdl.kc.i64`. Todas las direcciones son VA; cada una citada como cabeza se comprobó con
`ida_funcs.get_func(ea).start_ea == ea`. Lo que el desensamblado no fija va con `?`.

Nota posterior: `AWDL_STAGE1_QSET_STATE.md` corrige §4 y el riesgo 4 de §7: `netif_qset_enqueue` no está en el camino del primer ND/RS (su único llamador es el procesado de flujos) y el byte `+0x3C` que compara es `nll_state` del `netif_llink`, que vale 1 en una interfaz viva. Con las colas habilitadas, el primer paquete sí llega a la acción no-op.

## 1. Respuesta

1. **La subida termina bien.** `ifconfig awdl0 up` recorre `if_ioctl` → `ioctlHandler` →
   `IOSkywalkNetworkBSDClient::enableInterface` → la sobreescritura `RTLAwdInterface::enable`
   (`AWDLInterface.cpp:294`), que devuelve `kIOReturnSuccess`, y el `SIOCSIFFLAGS` devuelve 0 con
   `IFF_UP|IFF_RUNNING` puestos.
2. **Ninguna cola queda habilitada.** `IOSkywalkPacketQueue::initWithPool` escribe **0** en
   `+0x28` (`0xffffff8002b08c6d`) después de que `IOEventSource::init` lo hubiera puesto a 1. Ese byte
   es `IOEventSource::enabled` (`[rdi+28h]`, `0xffffff8000acaa44`). La etapa 1 no llama a `enable()` en
   ninguna de las tres colas, así que las tres quedan deshabilitadas.
3. **El primer paquete ND/RS no llega al no-op.** `IOSkywalkTxSubmissionQueue::packetSubmission`
   (`0xffffff8002b2cb2a`) comprueba `+0x28` antes de cualquier otra cosa y, con la cola deshabilitada,
   devuelve `0xE00002D8` (`kIOReturnNotReady`) antes de tocar el anillo; el error vuelve al netif y
   `legacyDequeue` (`0xffffff8002b2c860`) no se ejecuta.
4. **Corrección a la documentación anterior.** `AWDL_STAGE1_SPEC_DELTA.md` §4.1 y
   `AWDL_STAGE1_CODE_FIXES.md` §1 afirman que las colas nacen habilitadas porque `IOEventSource::init`
   deja `enabled = true`. La primera mitad es cierta y la conclusión no: `IOSkywalkPacketQueue::initWithPool`
   borra el byte al final de su inicialización. La acción no nula sigue siendo necesaria, pero no es
   suficiente para que T3.5 ejerza el camino de TX.
5. **Arreglo mínimo para T3.5:** que `RTLAwdInterface::enable` habilite las colas y `disable` las
   deshabilite, como hace Broadcom en `enableDatapath`
   (`0xffffff8001729afc`, descrito en `AWDL_PROXIMITY_DATAPATH.md` §8). La base de la familia
   `IO80211SkywalkInterface::enableDatapath` (`0xffffff80022f98f4`) sólo devuelve 0.

## 2. La cadena de subida

`ifconfig awdl0 up` emite `SIOCSIFFLAGS` = `0x80206910` (`-2145359600` con signo).

| Paso | VA | Hecho |
|---|---|---|
| `if_ioctl` | `0xffffff8002b002e6` | `ifnet_softc(ifp)` da el cliente; toma `client+0x90` (objeto glue) y `client+0x108` y salta al slot `+0x1C8` del glue con `(glue, client+0x108, ifp, cmd, data, 0)`. En `client+0x108` está el puntero a `ioctlHandler`, que `start` escribió en `0xffffff8002aff7a1` (`lea rax, ioctlHandler` / `mov [rbx+108h], rax`). `?` El cuerpo del glue que invoca ese puntero no se ha recorrido. |
| `ioctlHandler` | `0xffffff8002affce6` | Para `cmd == 0x80206910` lee `ifnet_flags(ifp)`. Si `IFF_UP` (bit 0) está puesto y `IFF_RUNNING` (bit 6) no, ejecuta la subida. También se invoca directamente con `0x80206910` desde `preConnectAction` (`0xffffff8002b008e7`) y `disconnectedAction` (`0xffffff8002b00a70`), es decir, al conectar y desconectar el nexus. |
| `ifnet_flags` | `0xffffff8000640390` | Devuelve `*(u16 *)(ifp + 190)`. Los bits 0 y 6 son `IFF_UP` e `IFF_RUNNING`. |
| `setRunningState(true)` | `0xffffff80022f85d2` | Escribe el byte `state+136` y devuelve 0. El `ioctlHandler` exige 0 para continuar. |
| `adjustInterfaceEnable` | `0xffffff8002b006ee` | Con `+324` y `+322` puestos, compara `+323` (habilitado) con `+325` (deseado) y llama a `enableInterface` o `disableInterface` según `+325`. |
| `enableInterface` | `0xffffff8002b0079a` | Llama al slot `+0x850` de la interfaz con `(iface, 0)`. Si devuelve 0, pone `+323 = 1`, avisa al poller KDP y devuelve 0. |
| slot `+0x850` | celda de `__ZTV23IO80211VirtualInterface` | En la familia es `IO80211VirtualInterface::enable` (`0xffffff80023308b4`); en `RTLAwdInterface` es la sobreescritura de `AWDLInterface.cpp:294`. |
| `RTLAwdInterface::enable` | `AWDLInterface.cpp:294` | Registra `"awdl: enable 0x%x"` y devuelve `kIOReturnSuccess` (0). |
| vuelta a `ioctlHandler` | `0xffffff8002affce6` | Con 0, deja `+324 = 1` y ejecuta `ifnet_set_flags(ifp, flags \| 0x40)`, que pone `IFF_RUNNING`. |
| `processBSDCommand` | `0xffffff8002331b36` | El `ioctlHandler` llama después al slot `+0x910` con el mismo comando. Salta al slot `+0xE80` (`handleIoctl`). |
| `handleIoctl` | `0xffffff8002331c88` | Para un comando que no es apple80211, cae a `IO80211SkywalkInterface::processBSDCommand` (`0xffffff80022f6e98`). |
| `processBSDCommand` (familia) | `0xffffff80022f6e98` | Para un comando que no es apple80211 llama al puntero de datos `0xffffff8002b3fa98`, que vale `IOSkywalkEthernetInterface::processBSDCommand` (`0xffffff8002b1102c`). |
| `ioctl_sifflags` | `0xffffff8002b11090` | Sólo gestiona `IFF_PROMISC` (`0x100`) e `IFF_ALLMULTI` (`0x200`); sin cambios en esos bits devuelve 0. |

Resultado: el `ioctl` devuelve 0 y la interfaz queda `UP,RUNNING`. La subida **no falla ni se cuelga**
con los stubs: ninguno de los dos toma locks ni puede bloquear; `enable` sólo escribe un log y devuelve 0.

### 2.1 Lo que la subida **no** ejecuta

La sobreescritura de `enable` reemplaza la celda `+0x850`, así que no se ejecuta ninguna de estas capas:

| Función | VA | Qué habría hecho |
|---|---|---|
| `IO80211VirtualInterface::enable` | `0xffffff80023308b4` | Llamar a `IO80211SkywalkInterface::enable`. |
| `IO80211SkywalkInterface::enable` | `0xffffff80022f990c` | Llamar a `IOSkywalkEthernetInterface::enable` (`0xffffff8002b114f8`). |
| `IOSkywalkEthernetInterface::enable` | `0xffffff8002b114f8` | Llamar a `IOSkywalkNetworkInterface::enable` (`0xffffff8002b0b41a`). |
| `IOSkywalkNetworkInterface::enable` | `0xffffff8002b0b41a` | Llamar al slot `+0x930` (`setInterfaceEnable(true)`) y escribir `+216 = (resultado == 0)`. |
| `IO80211VirtualInterface::setInterfaceEnable` | `0xffffff8002331ba6` | Poner `state+113`, crear los IOReporters, actualizar el `IO80211MacAddressAgent` y llamar a `setLinkState`. |
| `AppleBCMWLANSkywalkInterface::enableDatapath` | `0xffffff80015d9d7e` | Modelo Broadcom: su propia `enable` la llama después de la base para habilitar las colas de datos. |

Consecuencias medidas:

- `+216` (lo que devuelve `IOSkywalkNetworkInterface::isEnabled`, `0xffffff8002b0b54a`) queda a 0. Es
  inocuo: esa función no tiene ninguna referencia de código ni de datos.
- `state+113` y el `IO80211MacAddressAgent` no se actualizan. `?` Si algún consumidor de la familia
  lee `state+113` para filtrar RX no se ha comprobado.
- No se llama a `reportLinkStatus` (`0xffffff8002b0b49e`), que es lo que Broadcom hace desde su
  `setInterfaceEnable` cuando la interfaz está subida. `if_link_state` no cambia. `?` Si `wifip2pd`
  exige estado de enlace activo para considerar `awdl0` operativa no se ha verificado.

## 3. Quién habilita las colas (y quién no)

El byte `+0x28` es `IOEventSource::enabled`. `IOEventSource::enable` (`0xffffff8000acaa40`) lo pone a 1
con `mov byte ptr [rdi+28h], 1` (`0xffffff8000acaa44`) y `disable` (`0xffffff8000acaa60`) a 0. La
secuencia de creación de cualquiera de las tres colas es:

| Paso | VA | Hecho |
|---|---|---|
| `IOSkywalkTxSubmissionQueue::withPool` | `0xffffff8002b0ca86` | Envuelve a `initWithPool`. |
| `IOSkywalkTxSubmissionQueue::initWithPool` | `0xffffff8002b0caac` | Exige acción no nula (`a8`), reenvía a `IOSkywalkPacketQueue::initWithPool`. |
| `IOSkywalkPacketQueue::initWithPool` | `0xffffff8002b08bc8` | Llama a `IOEventSource::init` (deja `enabled = 1`) y al final escribe **0** en `+0x28` (`0xffffff8002b08c6d`) y 1 en `+0xCD` (`0xffffff8002b08c71`). |

Es decir: **la cola nace deshabilitada**. Nada en la familia lo revierte:

| Candidato | VA | Hecho |
|---|---|---|
| `IOSkywalkQueueSet::setEnable` | `0xffffff8002b139ce` | Sólo dos referencias, ambas de datos (celdas de vtable `0xffffff8002b40c38` y `0xffffff8002b43ed8`); ninguna de código. |
| `IOSkywalkQueueSet::initialize` | `0xffffff8002b13a22` | Recorre sólo los arrays `+0x28` (TX completion) y `+0x30` (RX submission); llama al hook `+0x1B8` y a `performCommand(0x50000)`. No toca las colas TX submission. |
| `nxp_queue_init` | `0xffffff8002aff2c5` | Llama al hook `+0x1B8` de cada cola del conjunto con el `netif_queue` y le asigna el puntero con `IOSkywalkPacketQueue::setKernNetifQueue` (`0xffffff8002b09016`). |
| `IOSkywalkPacketQueue::initialize` | `0xffffff8002b08cce` | Escribe 1 en `+0xCC` (`0xffffff8002b08d85`), el byte que después exigen `legacyDequeue` y la cola de completion. **No** toca `+0x28`. |
| `IO80211SkywalkInterface::enableDatapath` | `0xffffff80022f98f4` | Devuelve 0. Es un stub base. |

La habilitación es responsabilidad del driver. Broadcom la hace en `enableDatapath`
(`0xffffff8001729afc`, `AWDL_PROXIMITY_DATAPATH.md` §8): habilita la TX completion, la RX completion y
cada TX submission (slot `+0x150` = `IOSkywalkTxSubmissionQueue::enable`, `0xffffff8002b0cc76`) y pide
`requestDequeue(0, 0)`. `IOSkywalkTxSubmissionQueue::enable` hace exactamente lo necesario aquí:
comprueba `[+28h] & 1` (`0xffffff8002b0cc84`), llama a `IOEventSource::enable` (slot `+0x160`) y, si ya
hay `netif_queue` en `+0x78` y `+0x322` bit 2 está a cero, llama a `packetSubmission(false)` y
`sendNotification`.

La etapa 1 no hace nada de eso: `AWDLInterface.cpp:294-303` sólo registra y devuelve 0.

## 4. La cadena del primer paquete

Un ND/RS sale por `awdl0` cuando la interfaz está `UP`. El recorrido es:

| Paso | VA | Hecho / comprobación |
|---|---|---|
| `ifnet_enqueue_pkt` | `0xffffff80005e65d0` | Pasa sólo si `*(s8 *)(ifp+192) < 0` **y** `(*(u32 *)(ifp+120) & 7) == 2`; si no, libera y devuelve 6 (`ENXIO`). Después exige `*(u8 *)(ifp+190) & 1` (`IFF_UP`), si no libera y devuelve 50 (`ENETDOWN`). `?` El significado de `+192`/`+120` no se ha identificado; `+190` es `if_flags`, confirmado por `ifnet_flags` (`0xffffff8000640390`). |
| `netif_qset_enqueue` | `0xffffff80009fa2a0` | `cmp byte ptr [qset+3Ch], 2` (`0xffffff80009fa2d3`): si el estado del conjunto es 2, libera la cadena y devuelve 6 (`ENXIO`). |
| `nxp_queue_tx_push` | `0xffffff8002aff4d1` | Es el `tx_push` del proveedor. Llama al slot `+0x1D0` de la cola (58) y convierte el error con `IOService::errnoFromReturn` (celda `+0x788` de la interfaz, `0xffffff8000a95ab0`). |
| `IOSkywalkTxSubmissionQueue::packetSubmission` | `0xffffff8002b2cb2a` | **Primera comprobación: `+0x28 & 1`.** Con la cola deshabilitada devuelve `0xE00002D8` sin consumir ni un paquete. |
| `IOSkywalkTxSubmissionQueue::legacyDequeue` | `0xffffff8002b2c860` | Sólo se alcanza con `packetSubmission` ya dentro. Exige `+0x28 & 1` y `+0xCC & 1`; con `+0xCC` a 0 llama a `purgePackets` (`0xffffff8002b0d870`, slot `+0x2A0`). |
| acción de la cola | `+0x20` de la cola | `RTLAwdInterface::txNoop` (`AWDLInterface.cpp:247`), con `(owner +0x18, cola, array, count, refCon +0x58)`. |
| `IOSkywalkTxCompletionQueue::enqueuePackets` | `0xffffff8002b2dbb6` | Exige `+0x28 & 1`, `+0xCC & 1`, `count < capacidad (+0xAC)`, `+0x30 != 0` y `+0x110` bit 0 a cero. |
| `IOSkywalkTxCompletionQueue::completePackets` | `0xffffff8002b12b9e` | Por paquete, `getSourceQueue` y `notifySrcQueue`; el `IOSkywalkTxSubmissionQueue::packetCompletion` (`0xffffff8002b0d758`) incrementa el contador de completados `+0x134` y señala trabajo. |

### 4.1 Qué **no** está en este camino

- `IO80211VirtualInterface::findOrCreateFlowQueue` (`0xffffff800233270a`) y sus variantes sólo tienen
  referencias desde manejadores de IO80211/Broadcom (`0xffffff80017f51e0`, `0xffffff80024570b8`,
  `0xffffff800246efe8`, `0xffffff80024737f0`, …); no desde el envío del netif.
- `IO80211VirtualInterface::dequeueTxPackets` (`0xffffff80023313b2`) no tiene ninguna referencia: es la
  API que usaría un driver que saca paquetes por su cuenta, no la etapa 1.
- `IO80211SkywalkInterface::isCommandProhibited` (`0xffffff80022f9bce`) se llama desde los manejadores
  de comandos apple80211, no desde la ruta de datos.
- No hay ninguna comprobación de `linkState` en esta ruta: el único estado de enlace que se mira es
  `IFF_UP` en `ifnet_enqueue_pkt`.

Por tanto, si la cola estuviera habilitada, el paquete **sí** llegaría a `txNoop`; con el código actual
se queda en `packetSubmission` con `0xE00002D8`.

## 5. Valores de retorno esperados

| Llamada | Éxito | Fallo | Efecto del fallo |
|---|---|---|---|
| `enable(unsigned)` (slot `+0x850`) | 0 | cualquier otro | `adjustInterfaceEnable` lo devuelve, `ioctlHandler` deshace `IFF_RUNNING` (`ifnet_set_flags(flags & ~0x40)`), pone `+324 = 0`, llama a `setRunningState(false)` y el `SIOCSIFFLAGS` devuelve el error. |
| `disable(unsigned)` (slot `+0x858`) | cualquiera | — | El retorno se ignora; `+323` se pone a 0 igualmente. |
| acción TX | número de paquetes consumidos, `0 ≤ n ≤ count` | — | `0`: marca de atasco en `+0x13C` bit 1 (`+79` dword), no avanza el anillo, se reintenta después. `0 < n < count`: avance parcial y contador de parciales. `n > count`: el índice del anillo avanza de más (no hay recorte) y se desincroniza. |
| `packetSubmission` | 0 | `0xE00002D8` `kIOReturnNotReady` | cola deshabilitada. |
| | | `0xE00002D5` `kIOReturnBusy` | `(+0x13C & 5) != 0` (`?` significado de los dos bits). |
| | | `0xE00002DB` `kIOReturnNoSpace` | sin espacio libre en el anillo; se marca `+0x13C` bit 1. |
| `TxCompletionQueue::enqueuePackets` | 0 | `0xE00002C2` `kIOReturnBadArgument` | puntero nulo, `count == 0` o `count ≥` capacidad. |
| | | `0xE00002D8` `kIOReturnNotReady` | cola deshabilitada o sin `+0x30`. |
| | | `0xE00002D5` `kIOReturnBusy` | `+0x110` bit 0 puesto. |

## 6. Predicción de T3.5

Con el código actual (colas creadas y nunca habilitadas):

1. `ifconfig awdl0 up` devuelve 0; `ifconfig` muestra `UP,RUNNING`; el log del kext registra
   `awdl: enable 0x0`.
2. No hay panic ni cuelgue: el camino no toma gates propios ni espera a nadie.
3. `netstat -I awdl0` no incrementa los paquetes de salida. Un `ping6` a un vecino falla tras los
   reintentos de ND; `?` el errno exacto que ve la pila (el mapeo de `0xE00002D8` en `errnoFromReturn`
   no se ha decompilado).
4. Si se instrumenta `txNoop` con un log, **no aparece**: el paquete se queda en
   `packetSubmission` (0xE00002D8) y el netif lo descarta.
5. Si se añade la habilitación de colas, el mismo `ping6` debe producir la llamada a `txNoop` con
   `count` entre 1 y 64, y el contador de completados `+0x134` debe crecer.

Falsación: si con las colas sin habilitar apareciera una llamada a `txNoop`, habría otro camino que
habilita la cola (por ejemplo, una llamada de `wifip2pd` a `setInterfaceEnable` o a `enableDatapath`);
habría que registrar su pila.

## 7. Riesgos para T3.5, por probabilidad

| # | Riesgo | Probabilidad | Evidencia | Síntoma |
|---|---|---|---|---|
| 1 | La cola TX submission está deshabilitada y el paquete no llega al no-op | **Cierta con el código actual** | `0xffffff8002b08c6d` escribe 0 en `+0x28`; `0xffffff8002b2cb2a` lo comprueba antes de todo | `ifconfig` OK, cero TX, sin panic |
| 2 | La documentación previa da las colas por habilitadas y T3.5 se interpreta como fallo del no-op | Alta | `AWDL_STAGE1_SPEC_DELTA.md` §4.1, `AWDL_STAGE1_CODE_FIXES.md` §1 | diagnóstico equivocado; se busca el fallo en la acción en vez de en el enable |
| 3 | Se habilita sólo la TX submission y no la TX completion: el no-op libera los paquetes en el pool y el contador de completados no crece | Media | `0xffffff8002b2dbb6` exige `+0x28`; `packetCompletion` (`0xffffff8002b0d758`) es quien incrementa `+0x134` | T3.5 pasa con pocos paquetes; atasco al agotar el espacio libre (`?` umbral exacto) |
| 4 | `netif_qset_enqueue` descarta con `ENXIO` porque el estado del conjunto es 2 | Media | `0xffffff80009fa2d3`; el escritor del byte `+0x3C` no se ha identificado (`?`) | ni siquiera se llega a `packetSubmission`; cero TX |
| 5 | `setInterfaceEnable` no se ejecuta: no se reporta enlace ni se actualiza el `MacAddressAgent` | Media-baja | `0xffffff8002331ba6` queda fuera por la sobreescritura de `enable` | `if_link_state` inactivo; `?` si `wifip2pd` lo exige |
| 6 | `enable` devuelve algo distinto de 0 y el `ioctl` deshace `IFF_RUNNING` | Baja | `0xffffff8002affce6` revierte si `adjustInterfaceEnable != 0` | `ifconfig up` falla; la interfaz queda DOWN |
| 7 | El no-op devuelve `count` pero la completion falla: el anillo avanza y `+0x134` no | Baja (latente) | `0xffffff8002b2c860` avanza por el valor devuelto; `0xffffff8002b0d758` es el único que incrementa `+0x134` | atasco tras agotar el espacio libre |

## 8. Búsquedas ejecutadas y puntos abiertos

| Punto | Búsqueda | Resultado |
|---|---|---|
| subida | `SIOCSIFFLAGS` (`0x80206910`) en `ioctlHandler` | `0xffffff8002affce6`; `IFF_UP`/`IFF_RUNNING` con `ifnet_flags` (`0xffffff8000640390`, u16 en `+190`) |
| enable del driver | celdas `+0x850`/`+0x858` de `__ZTV23IO80211VirtualInterface` | `0xffffff80023308b4` / `0xffffff80022f9922`; la sobreescritura de la etapa 1 sustituye la primera |
| estado de las colas | escrituras a `+0x28` | `IOEventSource::init` escribe 1 (`0xffffff8000aca922`); `IOSkywalkPacketQueue::initWithPool` escribe 0 (`0xffffff8002b08c6d`) |
| habilitación en la familia | referencias a `IOSkywalkQueueSet::setEnable` | dos de datos, ninguna de código |
| inicialización en el nexus | `nxp_queue_init` | hook `+0x1B8` (pone `+0xCC = 1`) y `setKernNetifQueue`; no habilita |
| ruta de TX | `packetSubmission` → `legacyDequeue` → acción | `0xffffff8002b2cb2a` → `0xffffff8002b2c860` → `+0x20` |
| descarte antes del proveedor | `netif_qset_enqueue` | `cmp [qset+3Ch], 2` → `ENXIO` (`0xffffff80009fa2d3`) |
| `?` | escritor del byte `+0x3C` del conjunto de colas del netif | no identificado |
| `?` | mapeo de `0xE00002D8` a errno en `errnoFromReturn` | no decompilado |
| `?` | consumidor de `state+113` y de `if_link_state` en `wifip2pd` | no verificado |
| `?` | significado del qword `+0x30` de la TX completion queue (`0xffffff8002b2dbb6` lo exige no nulo) | no identificado |
