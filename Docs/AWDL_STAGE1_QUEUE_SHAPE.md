# Forma de colas de `awdl0` (rol 6): ¿basta 1 TX submission + 1 TX completion + 1 RX completion?

Cierra el `?` de `AWDL_STAGE1_IMPL_SPEC.md` §5.4: si la familia acepta para una interfaz virtual de
rol 6 la forma que ya funciona en el camino STA (1 TX submission, 1 TX completion, 1 RX completion,
un pool por dirección) o si hace falta el conjunto de Broadcom (4 TX submission, una por AC, más TX
completion, RX completion y la multicast opcional). Base: kernelcache de macOS 26.7.1 (25G241,
Darwin 25.6.0, x86_64), IDA headless (`idat -A -S`) sobre una copia de `boot_awdl.kc.i64`. Todas las
direcciones son VA. Cada fila marcada como instrucción se leyó en el desensamblado; las que sólo
citan cabeza de función se apoyan en el cuerpo decompilado. Lo que no quedó fijado lleva `?`.

## 1. Respuesta

**Sí: 1 TX submission + 1 TX completion + 1 RX completion se registra y arranca en una interfaz de
rol 6.** Ningún punto de la cadena cuenta colas por rol, por subfamilia ni por AC; las cuatro colas
TX de Broadcom son una elección suya para el modelo WMM y no un requisito.

La forma mínima que pasa el registro y la validación del enlace es **una sola cola TX submission**:

| Forma | Registro | Alta del `ifnet` | Colas que crea el netif |
|---|---|---|---|
| 0 colas | falla `0xE00002C2` | — | — |
| 1 TX submission | pasa | pasa | 1 TX, 0 RX (`?` alta funcional sin RX) |
| 1 TX submission + 1 RX completion | pasa | pasa | 1 TX, 1 RX |
| 1 TX submission + 1 RX completion + 1 TX completion | pasa | pasa | 1 TX, 1 RX |
| 4 TX submission (AC 0-3) + 1 TX completion + 1 RX completion | pasa | pasa, con WMM | 4 TX, 1 RX |

La única comprobación de recuento que rechaza es del netif, no de la familia: **si el qword de
información del conjunto de colas lleva el bit WMM (`0x8`), el número de colas TX submission debe ser
exactamente 4**; con 1 cola ese bit va a cero y no se exige nada. Un conjunto con 0 colas TX
submission se rechaza con 22 (`EINVAL`) y el mensaje `num_txqs == 0`. El número de colas RX no se
comprueba: cero es aceptado y simplemente no se crea ninguna cola RX.

## 2. Quién mira las colas, en orden

| VA | Función | Qué hace con las colas |
|---|---|---|
| `0xffffff80023308aa` | `IO80211VirtualInterface::start` | thunk a la base; no toca colas |
| `0xffffff80022f5792` | `IO80211SkywalkInterface::start` | crea pipes, peer manager, PM y la base de flow queues; ninguna cola de datapath, ningún recuento |
| `0xffffff8002b109f0` | `IOSkywalkEthernetInterface::copyRegistrationInfo` | valida el `RegistrationInfo` (versión 1, tamaño ≥ `0x130`), copia `0x130` bytes y publica `IOMACAddress`; no ve las colas |
| `0xffffff8002b10a84` | `IOSkywalkEthernetInterface::registerEthernetInterface` (array) | copia el `RegInfo` y delega el array de colas sin inspeccionarlo |
| `0xffffff8002b0a0d6` | `IOSkywalkNetworkInterface::registerNetworkInterface` (array) | decide el byte de información (`3` o `11`) con `isWMMSchedModelCapable`, crea el conjunto con `createWithQueues` y el enlace lógico con `createWithQueueSet` |
| `0xffffff8002b13384` | `IOSkywalkQueueSet::createWithQueues` | instancia el `IOSkywalkQueueSet` (0x80 bytes) y llama a `initWithQueues`; si falla, libera y devuelve nulo |
| `0xffffff8002b13406` | `IOSkywalkQueueSet::initWithQueues` | **única validación de recuento y tipo de la familia**; rellena los recuentos del qword de información |
| `0xffffff8002b0a204` | `IOSkywalkNetworkInterface::registerNetworkInterface` (enlace) | valida el `RegInfo` (versión 1, tamaño ≥ `0x108`, `+0x14` ≤ `0x400`), copia `0x108` bytes, exige un único conjunto con el bit 0 del qword |
| `0xffffff8002b0a7de` | `IOSkywalkNetworkInterface::addLogicalLink` | exige un único conjunto con el bit 0 y que la interfaz no tenga ya dirección; no mira recuentos |
| `0xffffff8002b0c406` | `IOSkywalkLogicalLink::initWithQueueSet` | acepta **1 a 16** conjuntos de colas; copia el qword de información de cada uno |
| `0xffffff8002b00ba6` | `IOSkywalkNetworkBSDClient::message` | en el mensaje `0xE0060105` añade el enlace al nexus (`kern_nexus_netif_llink_add`) |
| `0xffffff80009e6360` | `kern_nexus_netif_llink_add` | valida la configuración del enlace (`nx_netif_validate_llink_config`) |
| `0xffffff80009fad40` | `nx_netif_validate_llink_config` | **rechaza 0 colas TX submission** y exige 4 si el bit WMM está puesto |
| `0xffffff80009f9e00` | constructor del nodo de conjunto de colas del netif | copia los recuentos: nodo `+0x44` = byte 4 del qword, nodo `+0x45` = byte 5 |
| `0xffffff80009f9960` | `nx_netif_llink_ext_init_queues` (nombre en su cadena de log) | crea `[nodo+0x44]` colas RX y `[nodo+0x45]` colas TX, ambas con índices `0..n-1` |
| `0xffffff8002aff1e4` | `nxp_qset_init` | llama a `IOSkywalkQueueSet::initialize` |
| `0xffffff8002aff2c5` | `nxp_queue_init` | acota el índice contra el recuento TX o RX y añade la cola al proveedor |
| `0xffffff8002aff562` | `IOSkywalkNetworkBSDClient::start` | comprueba pools, asigna el proveedor del nexus y crea el `ifnet` |

No existe un símbolo `createDatapath` ni equivalente en ninguna clase de la familia: la creación de
colas es del driver y el alta es `registerEthernetInterface`/`registerNetworkInterface`. La búsqueda
de nombres con `Datapath` en `IO80211*` e `IOSkywalk*` sólo devuelve `printDataPath`,
`reportDatapathIssue` y los métodos `enableDatapath`/`disableDatapath` de las clases de Broadcom.

## 3. `initWithQueues`: 1 a 256 colas y cuatro tipos

La entrada se valida en tres instrucciones:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002b1341a`–`0xffffff8002b1342a` | `lea rdi,[rdx-101h]` / `cmp rdi,0FFFFFFFFFFFFFF00h` / `jb` | el recuento debe estar en **1..256**; fuera de rango devuelve 0 |
| `0xffffff8002b13430`–`0xffffff8002b1343c` | `test rsi,rsi` / `test rcx,rcx` | array de colas e info no nulos |
| `0xffffff8002b13471` | `jz` tras `mov rdi,[r12+r15*8]` | cualquier entrada nula del array aborta |

Cada cola se clasifica con `performCommand(0x10000)`, que devuelve un tipo constante por subclase:

| Tipo | Clase | Instrucción que escribe el tipo | Recuento | Array |
|---|---|---|---|---|
| 1 | `IOSkywalkTxSubmissionQueue` | `0xffffff8002b0d8f0` (`mov dword ptr [rdx],1`) | `+0x60` | `+0x20` |
| 2 | `IOSkywalkTxCompletionQueue` | `0xffffff8002b12e08` | `+0x68` | `+0x28` |
| 3 | `IOSkywalkRxSubmissionQueue` | `0xffffff8002b0e802` | `+0x70` | `+0x30` |
| 4 | `IOSkywalkRxCompletionQueue` | `0xffffff8002b0f578` | `+0x78` | `+0x38` |

Un tipo fuera de 1..4 se ignora en los recuentos y en los arrays, pero la cola sigue en la lista
plana (`+0x18`) y recibe `setQueueSet`.

Dos recuentos se fuerzan antes de reservar los arrays:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002b134d5`–`0xffffff8002b134e0` | `cmp rax,4` / `jz` / `mov qword ptr [rbx+60h],1` | TX submission: 4 se conserva; cualquier otro valor distinto de 0 pasa a 1 |
| `0xffffff8002b134ed`–`0xffffff8002b134f4` | `cmp qword ptr [rbx+68h],0` / `jz` / `mov qword ptr [rbx+68h],1` | TX completion: cualquier valor distinto de 0 pasa a 1 |

RX submission y RX completion no se fuerzan. Consecuencia: el netif nunca ve un recuento TX distinto
de 1 o 4. El exceso de colas TX sobre el recuento forzado se descarta del array por tipo (comprobación
`cmp rcx,[rbx+60h]` en `0xffffff8002b13685`) sin hacer fallar el alta.

Al final se reservan los arrays no vacíos y el bloque de información, y se escriben los dos recuentos
que viajan al netif:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002b13702`–`0xffffff8002b13717` | `test byte ptr [r14],8` / `call isWMMSchedModelCapable` / `test al,al` | si el llamador puso el bit WMM, se revalida la forma; falso aborta |
| `0xffffff8002b13733`–`0xffffff8002b13736` | `mov cl,[rbx+60h]` / `mov [rax+5],cl` | **info+5 = recuento TX submission** |
| `0xffffff8002b13739`–`0xffffff8002b1373c` | `mov cl,[rbx+78h]` / `mov [rax+4],cl` | **info+4 = recuento RX completion** |

Cualquier fallo de reserva devuelve 0; el llamador lo convierte en `0xE00002C2` (§9).

## 4. `isWMMSchedModelCapable`: cuatro colas TX, una por AC

`0xffffff8002b13774` recorre el array de colas TX submission y, sólo para las de tipo 1
(`0xffffff8002b137d8`), compara `getServiceClass()` con una tabla de cuatro dwords y cuenta una
aparición por AC (`0xffffff8002b137e9`, `0xffffff8002b137fb`). Al final exige que los cuatro
contadores valgan exactamente 1 (`0xffffff8002b13809`–`0xffffff8002b1381b`).

Con una sola cola el bucle encuentra un AC y deja los otros tres contadores a cero, así que devuelve
falso. Ese falso **no rechaza nada**: en
`registerNetworkInterface` (`0xffffff8002b0a0d6`) sólo decide el byte de información, `3` en vez de
`11`, y `initWithQueues` únicamente vuelve a llamar a `isWMM` si el byte trae el bit `0x8`, que con 3
no trae. El netif, en `nx_netif_validate_llink_config`, exige 4 colas TX sólo cuando ese bit está
puesto.

## 5. Lo que exige el netif

La validación del enlace (`0xffffff80009fad40`) lee el array de conjuntos del `llink_init` y, por cada
entrada de 8 bytes, interpreta el byte 5 como `num_txqs`:

| Comprobación | Instrucción / cadena | Resultado |
|---|---|---|
| número de conjuntos 0 | `*(u8*)(llink+4)==0` → `num qsets is zero` | 22 |
| array de conjuntos nulo | `*(u64*)(llink+0x18)==0` → `qsets is NULL` | 22 |
| más de un conjunto con el bit 0 | acumulación de `v9 & 1` → `has more than one default qset` | 22 |
| `num_txqs == 0` | byte 5 de la entrada → `num_txqs == 0` | 22 |
| bit WMM con `num_txqs != 4` | `(v9 & 8) != 0 && v10 != 4` → `invalid wmm mode` | 22 |

No hay ninguna comprobación del recuento RX. El constructor del nodo (`0xffffff80009f9e00`) copia
`entrada[4]` a `nodo+0x44` y `entrada[5]` a `nodo+0x45`, y `nx_netif_llink_ext_init_queues`
(`0xffffff80009f9960`) crea un netif queue por cada unidad:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff80009f99be` | `call qword ptr [r13+70h]` | `qset_init` con el índice `[nodo+0x28]` |
| `0xffffff80009f99d3` | `cmp byte ptr [r15+2Ch],0` | si el recuento RX (nodo `+0x44`) es 0, se salta el bucle RX |
| `0xffffff80009f9a0e` | `call qword ptr [r13+80h]` con `r8d=0` | `queue_init` RX para el índice `r14` (0..n-1) |
| `0xffffff80009f9a34` | `cmp r14,rax` / `jb` | tope del bucle RX: `[r15+2Ch]` |
| `0xffffff80009f9a39` | `cmp byte ptr [r15+2Dh],0` | si el recuento TX (nodo `+0x45`) es 0, se salta el bucle TX |
| `0xffffff80009f9a8d` | `call qword ptr [r13+80h]` con `r8d=1` | `queue_init` TX para el índice `rbx` (0..n-1) |
| `0xffffff80009f9ab0` | `cmp rbx,rax` / `jb` | tope del bucle TX: `[r15+2Dh]` |

El mensaje `0xE0060105` de `IOSkywalkNetworkBSDClient::message` (`0xffffff8002b00ba6`, llamada a
`kern_nexus_netif_llink_add` en `0xffffff8002b00d69`) transporta el `llink_init` que sale de
`IOSkywalkLogicalLink::getLogicalLinkNexusInfo`; el array de conjuntos es el que rellenó
`initWithQueueSet` con el qword de cada `getQueueSetNexusInfo` (`0xffffff8002b0c4a3`). Por eso el byte
5 que valida el netif es el `info+5` que escribió `initWithQueues`.

## 6. `nxp_qset_init` y `nxp_queue_init`

`nxp_qset_init` (`0xffffff8002aff1e4`) obtiene el conjunto por índice, copia el contexto del nexus y
llama a `IOSkywalkQueueSet::initialize` (`0xffffff8002b13a22`). Esa función exige un work loop no nulo
(`0x2b13a22`: si `a2` es nulo devuelve `0xE00002C2`), recorre las colas TX completion llamando a
`performCommand(0x50000)` y al hook `+0x1B8`, y después las RX submission con el mismo hook. **No
exige ningún recuento mínimo ni lee el puntero de acción.**

`nxp_queue_init` (`0xffffff8002aff2c5`) acota el índice contra el recuento de la dirección:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002aff309`–`0xffffff8002aff315` | `cmp [r12+60h],rax` / `ja` | TX: el índice debe ser menor que el recuento TX submission (`+0x60`); si no, 22 |
| `0xffffff8002aff31a`–`0xffffff8002aff324` | `cmp [r12+78h],rax` / `jbe` | RX: el índice debe ser menor que el recuento RX completion (`+0x78`); si no, 22 |
| `0xffffff8002aff32e` | `test r12,r12` | la cola del array debe existir |
| `0xffffff8002aff33d`–`0xffffff8002aff345` | llamada al slot `+0x888` del proveedor | añadir la cola al nexus; error se propaga |

Como el netif pide índices `0..n-1` y el primer índice es 0, un recuento 0 en TX submission deja al
netif sin ninguna cola TX que crear, y el rechazo llega antes por `num_txqs == 0`. Un recuento 0 en RX
completion no genera error en la validación: el bucle RX no se ejecuta y no se crea ninguna cola RX.
`?` Si el `ifnet` resultante queda utilizable en ese estado no se ha probado.

## 7. `IOSkywalkNetworkBSDClient::start` y los pools

`0xffffff8002aff562` es el último filtro antes del `ifnet`. Además del conjunto con el bit 0
(`0xffffff8002aff8c4`, `0xffffff8002aff8c6`) y del bit WMM para el valor de configuración
(`0xffffff8002aff92b`–`0xffffff8002aff92f`), comprueba los dos pools de la interfaz:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002aff940`–`0xffffff8002aff947` | `mov rcx,[rdi+98h]` / `mov rax,[rdi+0A0h]` | pools TX y RX de la interfaz, escritos en el alta desde los argumentos de `registerEthernetInterface` |
| `0xffffff8002aff94e`–`0xffffff8002aff970` | `test rcx,rcx` / `test rax,rax` / `xor dl,cl` / `test dl,1` | al menos uno no nulo; si los dos existen, deben coincidir en el bit 0 de `+0xB8` |
| `0xffffff8002aff98c`–`0xffffff8002aff996` | `test rax,rax` / `jz` | los dos nulos: salta al cierre |
| `0xffffff8002affb74` | `call IOSkywalkLogicalLink::getLogicalLinkNexusInfo` | el `llink_init` del proveedor |
| `0xffffff8002affbb5` | `call kern_nexus_controller_alloc_net_provider_instance` | reintenta 50 veces si devuelve 16 (`EBUSY`) |

Un fallo de pool o del proveedor no devuelve un código: cierra la interfaz y **devuelve 0**, es decir,
el `ifnet` no se crea y `awdl0` no aparece. Para T3 eso se manifiesta como ausencia de nombre BSD, no
como error de carga del kext.

## 8. `enable`/`disable` con colas sin acción

La familia no habilita las colas por su cuenta: `IOSkywalkQueueSet::setEnable` (`0xffffff8002b139ce`)
sólo tiene dos referencias, ambas de datos (celdas de vtable en `0xffffff8002b40c38` y
`0xffffff8002b43ed8`), ninguna de código. El camino de subida del `ifnet`
(`IOSkywalkNetworkBSDClient::enableInterface` → `IO80211SkywalkInterface::enable` →
`IOSkywalkEthernetInterface::enable`) no toca el conjunto ni las colas.

Las llamadas explícitas del driver tampoco leen la acción:

| VA | Función | Hecho |
|---|---|---|
| `0xffffff8002b0cc76` | `IOSkywalkTxSubmissionQueue::enable` | `IOEventSource::enable` (celda `+0x160` de `__ZTV20IOSkywalkPacketQueue`) y un hook; no lee `+0x18`/`+0x20` |
| `0xffffff8002b0efe0` | `IOSkywalkRxCompletionQueue::enable` | ídem |
| `0xffffff8002b12b2c` | `IOSkywalkTxCompletionQueue::enable` | ídem |
| `0xffffff8002b0ccd6` | `IOSkywalkTxSubmissionQueue::disable` | celda `+0x168`, borra el bit de `IOEventSource`; no lee la acción |
| `0xffffff8002b0f040` | `IOSkywalkRxCompletionQueue::disable` | ídem |
| `0xffffff8002b12b58` | `IOSkywalkTxCompletionQueue::disable` | ídem |

La acción (`+0x20`) y el target (`+0x18`) se leen sólo cuando hay trabajo pendiente y la cola está
habilitada: `IOSkywalkTxSubmissionQueue::legacyDequeue` (`0xffffff8002b2c860`) comprueba el bit 0 de
`+0x28` y llama a `r12 = [rbx+20h]` en `0xffffff8002b2c8de`; el camino RX equivalente lo hace en
`0xffffff8002b2d657` con `r12 = [rdi+20h]`. Con acción nula esa llamada salta a la dirección 0. La
etapa 1 no habilita las colas, así que no hay ninguna vía a ese salto.

## 9. Códigos que verá T3

| Valor | Nombre | Origen | Significado |
|---|---|---|---|
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b109f0` | `RegistrationInfo` con versión ≠ 1, tamaño < `0x130` o puntero nulo |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b0a204` | en el alta de red: tamaño < `0x108`, versión ≠ 1, enlace nulo o `+0x14` > `0x400` |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b1342a` | 0 colas, más de 256, array nulo o info nula |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b0a0d6` | `createWithQueues` o `LogicalLink::createWithQueueSet` devuelven nulo |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b13a22` | `IOSkywalkQueueSet::initialize` sin work loop |
| `0xE00002BD` | `kIOReturnNoMemory` | `0x2b109f0` | reserva de la copia del `RegistrationInfo` |
| `0xE00002BE` | `kIOReturnNoResources` | `0x2b109f0` | fallo al publicar `IOMACAddress` |
| `0xE00002D8` | `kIOReturnNotReady` | `0x2b2ca7c` | `requestDequeue` sin work loop o sin netif queue |
| `22` | `EINVAL` | `0x9fad40` | config del enlace: 0 conjuntos, array nulo, más de un conjunto por defecto, 0 colas TX, o bit WMM con recuento TX ≠ 4 |
| `22` | `EINVAL` | `0x2aff2c5` | índice fuera del recuento de su dirección, cola ausente o fallo al añadirla al proveedor |
| sin código | — | `0x2aff562` | pool ausente o descuadrado, o fallo del proveedor del nexus: cierra y devuelve 0; no hay `awdl0` |

El rechazo del netif por `num_txqs == 0` no cambia el retorno del registro, que ya había devuelto 0.
En el camino del registro en núcleo el 22 ni siquiera pasa por `kern_nexus_netif_llink_add`: lo
produce `nx_netif_default_llink_config` (`0xffffff80009faf35`, llamada desde
`kern_nexus_controller_alloc_net_provider_instance` en `0xffffff80009a4310`) y lo consume el propio
alloc, que lo devuelve tal cual; `IOSkywalkNetworkBSDClient::start` aborta con cualquier valor no
nulo y no hay `ifnet` ni nombre BSD. La vía que sí va por `kern_nexus_netif_llink_add` y por
`message` (que convierte el 22 en `0xE00002BC` en `0xffffff8002b00d6e`) es la del mensaje
`0xE0060105`, cuya única cadena es DriverKit (`addLogicalLink` ←
`IOUserNetworkWLAN::_AddLogicalLink_Impl`). Cerrado por `AWDL_STAGE1_BSDSTART_ORDER.md`.

## 10. Aplicación a la etapa 1

- **Condición previa:** la cola TX submission debe recibir una acción no nula. `IOSkywalkTxSubmissionQueue::initWithPool` (`0xffffff8002b0caac`) devuelve 0 cuando el argumento de acción (`a8`) es nulo, y `withPool` devuelve entonces nulo; el camino STA pasa `txAction` (`RTLAirLayer.cpp:365`). Pasar nulo hace fallar la creación de las colas antes de cualquier comprobación de §5 (`AWDL_STAGE1_SPEC_DELTA.md` §4).
- La forma de tres colas del plan (§5.4) pasa todas las comprobaciones, con esa condición: 1 TX submission deja
  `info+5 = 1` y el byte de información en `3`; el netif añade el enlace, crea una cola TX y una RX
  con índice 0, y el `ifnet` se crea.
- No hace falta añadir las 4 colas por AC. Si en una etapa posterior se quieren, deben ser exactamente
  4 con las clases de servicio de la tabla `wmmAC` para que `info` pase a `11`; con 4 colas y clases
  repetidas también se acepta (`info = 3`), pero el netif crea 4 colas TX igualmente.
- La TX completion no la usa el netif: ni su recuento viaja en el qword ni el netif le crea cola. Su
  presencia en la forma de tres colas es inocua; su ausencia también sería aceptada.
- Mantener la decisión de §5.4 de no habilitar las colas en la etapa 1: si se habilitara la TX
  submission sin acción y el stack emitiera un paquete por `awdl0`, `legacyDequeue` llamaría a la
  dirección 0.
- Si en T3 `awdl0` no aparece con la forma de tres colas, el problema no es el recuento: la forma no
  puede disparar ninguna de las comprobaciones de §5. Buscar en el retorno del registro, en los pools
  (§7) o en el alta del `ifnet`, no en el número de colas.

## 11. Búsquedas ejecutadas y pendientes

| Punto | Búsqueda | Resultado |
|---|---|---|
| límite de colas | guard de `initWithQueues` `0x2b1341a`–`0x2b1342a` | 1..256 |
| tipos | `performCommand(0x10000)` de las cuatro subclases | 1, 2, 3, 4 con sus instrucciones |
| recuentos | coercion `0x2b134d5`–`0x2b134f4` y escritura de `info+4`/`info+5` | TX: 4 o 1; TX comp: 1; RX sin tocar |
| WMM | `isWMMSchedModelCapable` y su llamada desde `initWithQueues` | sólo cambia `info`, no rechaza |
| netif | `nx_netif_validate_llink_config`, constructor del nodo y bucle de colas | exige ≥1 TX; 0 RX permitido; WMM pide 4 |
| rol | referencias a `IO80211SkywalkInterface::getInterfaceRole` (`0x22f5ed8`) | sólo código de `AppleBCMWLAN*`; ninguna en la cadena de registro ni en la familia |
| habilitación | referencias a `IOSkywalkQueueSet::setEnable` | dos de datos (vtable), ninguna de código |
| `createDatapath` | nombres con `Datapath` en `IO80211*` e `IOSkywalk*` | no existe; sólo `printDataPath`, `reportDatapathIssue` y los métodos de Broadcom |
| orden entre `setInterface` y la creación del `ifnet` en `bsd_start` | recorrido instrucción a instrucción | cerrado por `AWDL_STAGE1_BSDSTART_ORDER.md`: `setInterface` en `0xffffff8002b0a38d` (registro, antes del `registerService`); `ifnet` en `0xffffff8002affbb5` (`start` del cliente BSD) |
| `?` | alta con 0 colas RX (bucle omitido, sin comprobación) | no verificado en hardware |
| `?` | valores concretos de `service class` de la tabla `wmmAC` | la tabla se leyó en el desensamblado; su semántica (AC 0-3) no se ha mapeado a nombres |
