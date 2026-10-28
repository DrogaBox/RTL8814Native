# Datapath Skywalk de AppleBCMWLANProximityInterface (AWDL role 6)

Qué debe proporcionar una interfaz role 6 para mover paquetes: objetos Skywalk que crea (colas, pools, tamaños y opciones), cómo llega el TX al chip, cómo se entrega el RX al stack, y qué devuelve `getBSDNamePrefix`. Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), macOS 25.6.0, decompilación Hex-Rays de dos filesets del mismo KC: `com.apple.driver.AppleBCMWLANCoreMac` (interfaz y core) y `com.apple.driver.AppleBCMWLANBusInterfacePCIeMac` (bus y colas PCIe). Continúa `Docs/AWDL_ROLE6_RE.md` (ciclo de vida) y `Docs/AWDL_PROXIMITY_VTABLE_HIERARCHY.md` (herencia y slots); los offsets de vtable de la interfaz citados aquí coinciden con los de ese documento. Direcciones = VA del KC. Lo no observado va con `?`.

**Resumen corto:**

1. La interfaz no crea colas ni pools: los pide al bus (`AppleBCMWLANPCIeSkywalk::registerSkywalkInterface`, slot +0x118) con un descriptor de 12 qwords (`skywalkInterfaceRegisterParams_t`) que apunta a arrays de sus ivars, y el bus crea y devuelve **4 colas TX submission (una por WME AC), 1 TX completion, 1 RX completion y 1 multicast queue**, todas sobre **un único pool compartido** `AppleBCMWLANSkywalkPool` (por defecto 4864 paquetes × 2048 bytes, `maxBuffersPerPacket=1`, flags=1, sin DMA spec en PoolOptions).
2. Las colas son subclases Broadcom/PCIe de las colas de `IOSkywalkFamily` (`AppleBCMWLANPCIeSkywalkTxSubmissionQueue`, `…RxCompletionQueue`, `…MulticastQueue`), no las clases de la familia: añaden virtuales propios (+0x308–0x368) con los que la interfaz las gobierna (`triggerAWDLMulticastTx`, `p2pMulticastTx`, `freeMulticastPacket`, `stagePacket`, `freePendingPackets`, `collectQueueStats`, `flushPackets`).
3. TX: el stack encola en la TxSubmissionQueue de Skywalk; la acción `dequeuePackets` de la subclase PCIe clasifica por rol, y para role 6 inserta un header SNAP, duplica los multicast para el segundo canal (`stashMulticastPacketPair`) y encola en un `AppleBCMWLANPCIeFlowQueue` por destino (máx. 16 por cola) que patea su flow ring de firmware.
4. RX: `AppleBCMWLANPCIeSkywalk::rxCompRingDrain` drena los anillos de completado, para role 6 valida longitud ≥ 28, procesa VLAN/SNAP, llama al pre-hook de la interfaz (slot +0x8F8) y en cada paquete hace `enqueuePackets` en la **RxCompletionQueue de la interfaz destino** (obtenida por el slot +0xD70); al final del lote patea `requestEnqueue(0,0)`/`flushPackets` en las colas RX de las 12 interfaces.
5. `getBSDNamePrefix` devuelve un puntero al literal `"awdl"`; ese mismo literal es el nombre que `createProxInterface` pasa a `init`, va en `RegistrationInfo+0x30` y el BSD name final (`awdl0`) lo arma `IO80211SkywalkInterface::getBSDName` con `"%s%u"` desde el ifnet.

## 1. Método y fichas

Funciones decompiladas (todas OK, sin fallos): los 15 métodos pedidos de `AppleBCMWLANProximityInterface` más `withOptions`, `init×2`, `free`, `enable/disable(unsigned)`, `setInterfaceEnable`, `setPromiscuousModeEnable` (que es `validateDispatchQueue` + `return 0`), `isChipInterfaceReady`, `deleteChipInterface`, `setCOMPANION_SKYWALK_IF_CREATE`, `AppleBCMWLANCore::createProxInterface`/`createSkywalkInterface`, el bloque AppleBCMWLANSkywalkInterface (equivalente infra) completo, el bloque de familia `IOSkywalkPacketBufferPool/Queue/EthernetInterface`, las bases `IO80211VirtualInterface`/`IO80211SkywalkInterface`, y el bloque PCIe (`registerSkywalkInterface`, `allocSkywalkCommonResources`, pool, colas TX/RX, `rxCompRingDrain`, `dequeuePackets`, `triggerAWDLMulticastTx`, `p2pMulticastTx`). Vtables volcadas slot a slot con nombres: `AppleBCMWLANProximityInterface` (572 slots), `AppleBCMWLANBusSkywalk`/`AppleBCMWLANPCIeSkywalk`, las cuatro clases de cola PCIe y las de familia. Los slots citados abajo están todos resueltos por volcado numérico, no por muestreo. La lectura de la vtable del bus base en `AWDL_PROXIMITY_VTABLE_HIERARCHY.md` sigue válida; aquí el objeto concreto en runtime es `AppleBCMWLANPCIeSkywalk` (subclase que vive en el fileset PCIe), cuyo slot +0x118 apunta a la implementación real del registro.

## 2. Creación: `withOptions` e `init`

`AppleBCMWLANCore::createProxInterface` (`0xffffff800165dcbe`): si el slot de AWDL del core ya está ocupado, log `"already created"` y éxito; si no:

1. `AppleBCMWLANProximityInterface::withOptions(core, request+4, "awdl")` (`0xffffff80017159e6`): `OSObject_typed_operator_new` de 320 bytes, ctor de `IO80211AWDLProtocol` con la metaclase propia, vptr = `0xffffff80017f5b78`, `instanceConstructed`, y llamada por slot +0x11C8 a `init(core, mac, 6, "awdl")`. Si `init` falla → release y nulo (el caller devuelve `0xE00002BC`).
2. `init(core, mac, role, name)` (`0xffffff8001718db0`):
   - `init()` propia (slot +0x88): `IO80211SkywalkInterface::init()` (vía la vtable importada de la base) + `IOMallocType` de **0x460 bytes de ivars**.
   - `IO80211VirtualInterface::init(ctrl, mac, role, name)` (llamada calificada por la vtable importada de `IO80211AWDLProtocol`, slot +0xE78; `0xffffff80023305f8`): a su vez `IO80211SkywalkInterface::init(ctrl, mac)`, ivars propios de la base, `setInterfaceRole(6)`, `IOLockAlloc`, lista BPF, `registerInterestedDriver(ctrl)` + `ctrl->open(this)`, diccionario de procesos denegados (capacidad 5), y `IO80211FlowQueueDatabase::initialize()` → ivars base+512.
   - `setInterfaceId(2)` si la versión del core ≥ 5; `0xFFFFFFFF` en caso contrario.
   - `AppleBCMWLANTaskmaster::withWorkQueue(core→getWorkQueue, 0x10)` → ivars+56 (falla el init si no hay Taskmaster).
   - Ivars de estado: core→+72, role 6 →+688, `+932 = 0` (estado "chip interface ready", lo comprueba `isChipInterfaceReady` y `enableDatapath`), tiempos de la máquina de estados (lifetime 6, guard 20, maxNoMaster 4, etc. en +656…+800), boot-args `awdl_log_flags_config`, `awdl_max_peers` (+908, por defecto −1), `awdl_low_latency_default_mapping` (+912, 1), `awdl_low_latency_tx_requeue` (+916, 0).
   - Tx Power Manager del core →+1000 (falla el init si no está), `AppleBCMWLANCommander` del core →+968 (lo usa `setMacAddress`).
   - **Conteo y mapa de colas TX: `ivars+945 (byte) = 4`; mapa AC `ivars+948 = {0,1,2,3}`** (dos qwords `0x100000000`, `0x300000002`). Las colas aún no existen: las creará el bus en `start`.
3. `obj->attach(core)` por el slot virtual +0x670 (`IO80211VirtualInterface::attach`, `0xffffff800233023a`).
4. `IOSkywalkEthernetInterface::initRegistrationInfo(reg, 1, 0x130)` sobre un `RegistrationInfo` de 0x130 bytes en el stack del caller, y relleno Broadcom: `+0x0C = 3` (subfamilia Wi-Fi), `+0x14 = 2`, `+0x24 = 0x8000000080` (qword), `+0x30 = &"awdl"` (puntero al prefijo), `+0x38 = 0`, `+0x3A = 64` (u16), `+0x40 = 6` (u32; 4 si la propiedad `IOPMResetPowerStateOnWake` del bus no es true), `+0x108 = MAC` (6 bytes tomados de `getSelfMacAddr`, slot +0xCF8). `initRegistrationInfo` (`0xffffff8002b108e2`) valida exactamente versión 1 y tamaño 304 (0x130) y precarga defaults: subfamilia 2 en +0x08, familia en +0x0C, 256 en +0x14, 32 en +0x24, prefijo `"en"` en +0x30, unidad BSD en +0x38, 46 en +0x44 y MTU 1500 en +0x48/+0x4C.
5. `AppleBCMWLANProximityInterface::start(obj, core, reg)` (la especializada) y, si devuelve true, el nombre BSD se copia a la request.

## 3. `start(AppleBCMWLANCore*, RegistrationInfo*)` — registro contra el bus

`0xffffff8001729376`. El `start(IOService*)` (slot +0x5C0) solo hace `safeMetaCast(provider, AppleBCMWLANCore)` y delega aquí con `reg = 0`. Pasos:

1. **Base:** llamada calificada `IO80211VirtualInterface::start(provider)` por la vtable de `IO80211AWDLProtocol` (+0x5C0). Falla → log `"awdl skywalk start fail"` y false. La base (`IO80211SkywalkInterface::start`, `0xffffff80022f5792`) crea el CCPipe/CCStream de la interfaz (`"Interface_<clase>_<n>"` con `n` = contador de instancias por rol), PM (`PMinit`, `joinPMtree`, 2 estados), `IO80211FlowQueueDatabase`, array de pipes, y `IO80211MacAddressAgent`. El peer manager: para rol 2 se hereda del padre; para el resto (incluido 6) se crea uno propio por virtual. **El glue WCL (`IO80211Glue`) solo se crea para rol 1** — la interfaz AWDL no tiene ruta WCL.
2. Logger propio (slot +0xD08) → ivars base+64; `core` → ivars+72.
3. Bus: `core` (slot 397, `getWorkQueue`) → `IO80211WorkQueue` → ivars+1104; bus interface del core (estado +296 → +5424, virtual +0x1F8) y `safeMetaCast` a `AppleBCMWLANBusSkywalk` → ivars+984/+992. En runtime el objeto es `AppleBCMWLANPCIeSkywalk` (subclase del fileset PCIe).
4. **Descriptor** `skywalkInterfaceRegisterParams_t` (12 qwords, tabla en §4) rellenado con punteros a los ivars de la interfaz y headroom 64 (u16 constante) y logger; llamada `bus->registerSkywalkInterface(&descriptor)` (slot +0x118). Devuelve distinto de 0 → log `"Failed to register interface with bus interface"` y false.
5. El bus (§5) crea las colas y las deja **dentro de los arrays de los ivars** de la interfaz. De vuelta en `start`: por cada cola TX submission (conteo en `ivars+945`) y para TX completion e RX completion: `IO80211WorkQueue::addEventSource(queue)` (slot +0x140 del work queue) y `IOSkywalkPacketQueue::getQueueId` → `ivars+1088[map[i]]` (mapa en `ivars+948`); si `ivars+1064` (multicast queue) es no nulo se añade también. Para la ruta infra el orden es el mismo (pseudocódigo espejo en `AppleBCMWLANSkywalkInterface::start`, `0xffffff80015d909a`).
6. `RegistrationInfo+0x48 y +0x4C = 0x594` (dos u32, 1428): MTU post-headroom fijo de AWDL (la ruta infra en cambio resta el headroom del `RegistrationInfo` a los valores que dejó `initRegistrationInfo`).
7. `IOSkywalkEthernetInterface::registerEthernetInterface(reg, colas, n+2, txPool, rxPool)` — **el registro directo contra IOSkywalkFamily** (la ruta infra usa el wrapper `IO80211InfraInterface::registerInfraEthernetInterface`; decompilado: hace `copyRegistrationInfo` y llama `IOSkywalkNetworkInterface::registerNetworkInterface(this, logicalLink, queues, count, txPool, rxPool, 0)`). En el pseudocódigo de Proxi el decompilador no muestra los registros de los argumentos; la forma `reg + colas copiadas (txsub…, txcomp, rxcomp) + n+2 + pools de ivars +1072/+1080` se deduce del espejo infra y de la firma del callee (`?` en la reconstrucción fina). Error → log con el código y rollback (removeEventSource de todas las colas).
8. Éxito: `IOService::registerService(2)` (slot +0x5B0; valor de opciones observado 2, `?` su semántica exacta) e `IO80211SkywalkInterface::setRunningState(0)` (slot +0x938).

## 4. Descriptor `skywalkInterfaceRegisterParams_t`

Campos observados (offsets del descriptor / ivar Proxi / ivar infra; `in` = la interfaz lo rellena, `out` = lo escribe el bus):

| qword | contenido | Proxi ivar | infra ivar | dir |
|---|---|---|---|---|
| +0x00 | interfaz (`this`) | — | — | in |
| +0x08 | byte: nº de colas TX submission (máx. 7; `ud2` si ≥ 8) | +945 | +42 | in |
| +0x10 | u32[]: WME AC de cada cola TX | +948 | +44 | in |
| +0x18 | `IOSkywalkPacketQueue*[]`: colas TX submission | +1032 | +120 | out |
| +0x20 | puntero adicional (conjunto colas) | +1024 | +112 | out |
| +0x28 | cola TX completion | +1008 | +96 | out |
| +0x30 | cola RX completion | +1016 | +104 | out |
| +0x38 | multicast queue (opcional; 0 = no pedir) | +1064 | +152 | out |
| +0x40 | pool TX | +1072 | +80 | out |
| +0x48 | pool RX | +1080 | +88 | out |
| +0x50 | u16 headroom | 64 (constante) | `reg+0x3C` | in |
| +0x58 | logger (`CCLogStream*`) | getLogger() | getLogger() | in |

`AppleBCMWLANPCIeSkywalk::registerSkywalkInterface` (`0xffffff8001549528`) valida `this`, colas TX, TX comp y RX comp no nulos (error `0xE0000001`), lee `isLowLatencyEnabled` de la interfaz para elegir la capacidad TX (+24 normal / +28 low-latency del bus), y crea:

1. **Multicast queue** (solo si +0x38 ≠ 0): `AppleBCMWLANPCIeSkywalkMulticastQueue::withProviderAndQueueInfo(bus, interfaz, logger, fault)` (`0xffffff8001552e16`); objeto de 88 bytes que envuelve un `AppleBCMWLANPCIeSkywalkFlowQueue` con clave de flujo `0xFFFFFFFFFFFF` (multicast "todas").
2. **Una `AppleBCMWLANPCIeSkywalkTxSubmissionQueue` por entrada del byte de conteo** (`withProviderAndQueueInfoAndPool(bus, if, ifId, índice, AC del mapa, capacidad, workqueue, pool, logger, fault)`; `0xffffff8001541122`), con `setInterface(interfaz)`; objeto de 376 bytes.
3. **TX completion** (`AppleBCMWLANPCIeSkywalkTxCompletionQueue`, misma fábrica de familia) y **RX completion** (`AppleBCMWLANPCIeSkywalkRxCompletionQueue`, `0xffffff8001546478`; objeto de 328 bytes), ambas con `setInterface`.
4. El headroom del descriptor se copia dentro del pool (`*(u16*)(pool->tracker + 64) = headroom`).
5. **Pool TX y RX = el mismo puntero** (descriptor +0x40 y +0x48): el pool común del bus creado en `allocSkywalkCommonResources`.
6. Marca la interfaz en el bitmap del bus: `bus+44 |= 1 << ifId` (lo mismo que hace `completeRegistration`, `0xffffff80015494c6`). Fallo en cualquier alta → rollback con release de lo creado.

## 5. Pool y colas del bus (tamaños y opciones)

`AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources` (`0xffffff8001548d2a`), llamado por `initWithProviderAndCapacity`:

- `AppleBCMWLANPCIeSkywalkPacketPool::withName("AppleBCMWLANSkywalkPool", busif, logger, fault, &opts)` → bus+56. Opciones (`AppleBCMWLANSkywalkPoolOptions`, 24 bytes): `packetCount = bufferCount = bus+36`, `bufferSize = bus+40`, `maxBuffersPerPacket = 1`, `+0x10 = 0`, `flags (+0x14) = 1`, sin DMA spec en PoolOptions (el pool PCIe añade su `IOMapper` aparte, `setMapper`). La base Broadcom `AppleBCMWLANSkywalkPacketPool::initWithName` (`0xffffff800175c7ac`) copia esas 24 bytes y llama a la familia `IOSkywalkPacketBufferPool::initWithName(name, owner, 1, opts)` — **mismo tipo (1) y misma forma de PoolOptions que la capa RTL ya usa**; debajo, `kern_pbufpool_create` y segmentos (`segmentConstructor/Destructor`), con dos `AppleBCMWLANHashtable` (TX/RX trackers) dimensionados a la potencia de 2 de `packetCount`.
- Además crea la cola de refill del propio bus: `AppleBCMWLANPCIeSkywalkRxSubmissionQueue` con capacidad `bus+32` sobre el mismo pool, la añade al work loop del bus y la habilita; y un `IO80211TimerSource` para `lowLatencyRingCreateTimeout`.
- `rxCompRingDrain` rellena esos buffers de RX vía `refillRxPacket` (`0xffffff8001550c18`, `?` detalle interno no revisado en esta pasada).

Capacidades del bus: `initWithProviderAndCapacity` (`0xffffff8001548bd6`) guarda los argumentos en +24/+28/+32/+36/+40 y `AppleBCMWLANBusInterfacePCIe::allocateSkywalkBusResource` (`0xffffff800158b85a`) los calcula con defaults cuando la configuración de firmware (dwords del core en +4560/+4564/+4568/+4576) es cero. Defaults observados (el propio código los loguea como `txLlwQueueSize txQueueSize rxQueueSize skywalkPacketPoolSize`):

| uso | campo del bus | default |
|---|---|---|
| capacidad cola TX submission (y TX completion), AWDL | +24 | 512 (= `2 * txQueueSize` del log … `?` correspondencia exacta argumento a campo no re-verificada; el valor TX observado por defecto es 512) |
| capacidad cola TX low-latency | +28 | 1024 |
| capacidad RX completion (y cola RX submission del bus) | +32 | 1024 |
| pool `packetCount`/`bufferCount` | +36 | 4864 (= tx + 4·txLlw + 2·rx + 256 con los defaults) |
| pool `bufferSize` | +40 | 2048 (0x800) |

Consecuencia práctica: **todas las colas de todas las interfaces Broadcom comparten un pool de 4864 buffers de 2048 bytes**, y el MTU efectivo AWDL es el del `RegistrationInfo` (0x594 = 1428 útiles tras el headroom de 64).

## 6. Slots de las colas PCIe que usa la interfaz

Las subclases Broadcom extienden la vtable de la familia (la de familia termina en los `_RESERVEDIOSkywalk…`). Slots Broadcom (desde el vptr del objeto, resueltos en las vtables PCIe volcadas):

TX submission (`AppleBCMWLANPCIeSkywalkTxSubmissionQueue`, vptr `0xffffff80015acd00`):

| offset | método |
|---|---|
| +0x150 / +0x158 | familia: `IOEventSource` enable/disable (vía `IOSkywalkTxSubmissionQueue::enable/disable`) |
| +0x298 | familia: `IOSkywalkTxSubmissionQueue::requestDequeue(void*, uint)` |
| +0x308 | `AppleBCMWLANSkywalkTxSubmissionQueue::initWithPoolAndQueueInfo` |
| +0x310 | `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(OSObject*, IO80211NetworkPacket**, uint, void*)` |
| +0x318 | `…::forwardPacket(IO80211NetworkPacket*)` |
| +0x320 | `…::dupAndTransmitMcastPacket(pkt, uchar*, uchar*)` |
| +0x328 | `…::freeMulticastPacket(IO80211NetworkPacket*)` |
| +0x330 | `…::triggerAWDLMulticastTx(uint, uchar)` |
| +0x338 | `…::collectQueueStats(char*, ulong, ulong, ulong)` |
| +0x340/+0x348 | `…::getRingFreeSpace` / `getPendingPacketCount` |
| +0x350/+0x358 | `…::clearFWRings` / `clearFWRing` |
| +0x368 | `…::p2pMulticastTx(IO80211NetworkPacket**, uint)` |

RX completion (`AppleBCMWLANPCIeSkywalkRxCompletionQueue`, vptr `0xffffff80015ad9d8`):

| offset | método |
|---|---|
| +0x150 / +0x158 | familia: enable/disable |
| +0x298 | familia: `IOSkywalkRxCompletionQueue::requestEnqueue(void*, uint)` |
| +0x2A0 / +0x2A8 | familia: `enqueuePackets(IOSkywalkPacket* const*, uint, uint)` / variante de un paquete |
| +0x320 | `AppleBCMWLANSkywalkRxCompletionQueue::setInterface` |
| +0x328 | `AppleBCMWLANPCIeSkywalkRxCompletionQueue::collectQueueStats` |
| +0x330 | `…::flushPackets(void)` |
| +0x340 | `…::stagePacket(IO80211NetworkPacket*, bool, bool)` |
| +0x348 | `…::freePendingPackets(IO80211NetworkPacket**, uint)` |

TX completion (`AppleBCMWLANPCIeSkywalkTxCompletionQueue`): +0x320 `collectQueueStats`, +0x328 `getQueueId`, +0x330 `getLogger`.

## 7. Getters de datapath (overrides)

Todos leen ivars (bloque propio de 0x460 bytes en objeto+0x138):

- `getRxCompQueue` (`0xffffff800172a3c8`, slot +0xD70) → `ivars+1016`.
- `getTxCompQueue` (`0xffffff800172a3b4`, +0xD78) → `ivars+1008`.
- `getTxPacketPool` (`0xffffff800172a3a0`, +0xD88) → `ivars+1072` (el pool común del bus).
- `getTxSubQueue(wme_ac)` (`0xffffff800172a372`, +0xD80): `id = ivars+1088[4*ac]` (rellenado en start desde `getQueueId`); si `id >= ivars+945` devuelve nulo; si no, `ivars+1032[8*id]`. Es decir, tabla AC → queue-id → cola.
- `getRxPacketPool` **no está overrideado**: Proxi hereda `IO80211SkywalkInterface::getRxPacketPool` (slot +0xD90, `0xffffff80022f98ec`); su comportamiento para esta interfaz no fue decompilado (`?`).
- `getLogger` (+0xD08) → `ivars+64` (el CCStream que la base creó en start).
- `isChipInterfaceReady` (+0xB78) → `ivars+932 != 0`.

## 8. `enableDatapath` / `disableDatapath`

`enableDatapath` (`0xffffff8001729afc`, slot +0xD98):

1. Si `ivars+932 == 0` (chip no listo): log `"awdl interface is not ready, cannot enable data path yet"` y **devuelve 0** (no error).
2. Si `isInterfaceEnabled()` (slot +0xCF0) es falso: log `"%s enableDatapath failed because interface has not been enabled yet"` y devuelve `0xE00002BC`.
3. Habilita TX completion (`IOEventSource` enable, slot +0x150), habilita RX completion (+0x150), y por cada cola TX submission (mientras no haya un puntero nulo): enable (+0x150) y `requestDequeue(0, 0)` (familia, +0x298) — el "arranque" del servicio de dequeue. Puntero nulo a mitad → log `"tx submission queue ac %d not valid"` y `0xE00002BC`.
4. Si existe la multicast queue (`ivars+1064`): `IO80211WorkQueue::addWorkSource(wq, ws)` (obtiene el work queue por `getController` slot +0xBA8 → core slot 397), enable (+0x150) y llamada +0x1D0 sobre ella (la inicializa como work source; `?` nombre exacto).
5. `requestEnqueue(0, 0)` (familia, +0x298) sobre RX completion; si devuelve error, log info `"skywalk rx completion queue enqueue fail with return %d"` (no propaga el error).

`disableDatapath` (`0xffffff8001729ec6`, +0xDA0): deshabilita en orden inverso: colas TX submission (disable +0x158), multicast queue (disable + `IO80211WorkQueue::removeWorkSource`), RX completion, TX completion. Todo deshabilitado → 0; algo nulo → `0xE00002BC` con log `"%s: skywalk data path disable fail"`.

Contexto de ciclo de vida observado en Broadcom (y ya replicado en la capa RTL): la familia no habilita las colas del driver; es el propio driver el que llama a `enableDatapath` desde `enable(unsigned)` (slot +0x850: primero la base `IO80211VirtualInterface::enable` vía vtable importada, luego el slot +0xD98) y `disable(unsigned)` (+0x858 → base + slot +0xDA0). `setInterfaceEnable(bool)` (+0x930) llama a la base y, si la interfaz está subida (`ivars+932 != 0`), reporta link con `IOSkywalkNetworkInterface::reportLinkStatus(this, on ? 3 : 1, on ? 128 : 130)`; si no, `"AWDL interface not up yet, not enabling queues"`.

## 9. TX: del stack al chip

1. El stack encola en la TxSubmissionQueue registrada (la familia `IOSkywalkTxSubmissionQueue` gestiona el anillo de Skywalk y el crédito).
2. La acción de la cola es `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets` (slot +0x310; `0xffffff80015420f0`): por paquete hace `safeMetaCast` a `AppleBCMWLANPCIeSkywalkPacket` (los paquetes del pool PCIe) y comprueba `isInterfaceEnabled` (+0xCF0) e "chip ready" (slot +0xB78 de la interfaz); interfaz no habilitada → `0xE00002D8`, interfaz inválida o chip no listo → `0xE0000001`.
3. Reparto por rol de la interfaz: rol 1 → `dequeueInfraPacket` (flow queue del AP actual), roles 2/softAP/LLW → `dequeueSoftAPLLWPacket` (tabla de hasta 16 flow queues por MAC destino, creación bajo demanda con `AppleBCMWLANPCIeFlowQueue::withProviderAndQueueInfo`), y **roles 6 (AWDL) y 7–10 (NAN) → `dequeueAWDLNANPacket`**:
   - multicast del link: `AppleBCMWLANPCIeSkywalkPacketPool::duplicatePacket` y `IO80211VirtualInterface::stashMulticastPacketPair(if, pkt, dup)` — el par original+duplicado se guarda en la interfaz virtual para transmitir por el segundo canal social (flag 0x80 en el duplicado); sin interfaz virtual válida → error `0xE00002C0` (rama `"Invalid Virtual Interface for AWDL !"`).
   - unicast: inserción de un header SNAP propio de Broadcom (patrón observado en el paquete: `AA AA 03 … 17 8F 08`, constantes `0x0008F2170003AAAA`, contador de 12 bits en +24 combinado con el ring id `<< 12`), ajuste de headroom/`setLinkHeaderOffset`, notificación a la interfaz (slot +0x918) con el puntero del header, y `findOrCreateFlowQueue` (slot +0xC70 de la interfaz) → `AppleBCMWLANPCIeFlowQueue::enqueuePacket(fq, pkt, true)`.
   - Si no hay flow queue ni hueco (máx. 16 por cola, contador en ivars de la cola +168): error y descarte.
4. Las `AppleBCMWLANPCIeFlowQueue` están atadas a flow rings de firmware (`attachTxSubmRing` del bus, `AppleBCMWLANPCIeTransmitFlowRing`); al terminar el lote, si hay pendientes se señala `signalTxPendingFlowQueue`, y si no se "patean" los rings de los flujos con tráfico (llamada +0x230 del ring con `(id, 1)`).
5. Multicast/AWDL específicos, todos delegando en la **primera** cola TX submission (`ivars+1032[0]`):
   - `triggerAWDLMulticastTx(uint, uchar)` (+0xF10): buscan el flow queue de multicast con `IO80211VirtualInterface::findExistingFlowQueue(ifId | 0xFFFF0000)` (slot +0xC78) y patean su ring; no lo encuentra → `0xE00002C7`.
   - `p2pMulticastTx(IO80211NetworkPacket**, uint)` (+0xF18): llamada directa a `dequeuePackets(this, paquetes, n, 0)` — procesa la lista igual que el dequeue normal.
   - `freeMulticastPacket(IO80211NetworkPacket*)`: limpia el flag del paquete y `AppleBCMWLANPCIeSkywalkPacketPool::deallocatePacket`.
6. La cola TX completion (`ivars+1008`) es la que la familia usa para devolver los paquetes al pool; el drenaje firmware→comp queue es propio del bus (`txCompRingDrain`, no decompilado en esta pasada, `?`).

## 10. RX: del chip al stack

`AppleBCMWLANPCIeSkywalk::rxCompRingDrain(ring, ctx, msg, n)` (`0xffffff8001549dea`), por mensaje del anillo de completado RX:

1. Localiza la interfaz destino (por interface id del mensaje o `core→getInterfaceById`, slot 632; para role 6 usa `getProximityInterfaceId` del core).
2. Procesado para AWDL (role 6): descarta < 28 bytes (`"Too small (len %d)"`), procesa VLAN opcional (strip 4), y detecta SNAP en +14 (`AA AA 03`): si el OUI/ethertype coincide con el propio (comparación contra `0x08F20800`) quita 16 bytes; si OUI 0 quita 8 bytes y redirige a la interfaz LLW (`getInterfaceById(core, 9)`); sin SNAP exige ethertype ≥ 0x600. Ajustes de headroom/`setLinkHeaderOffset` y longitudes.
3. Pre-hook de la interfaz: llamada al slot +0x8F8 de la interfaz con `(paquete, datos, len, &flag, 0)`; retorno 0 → descarte (devuelve el paquete al pool).
4. Obtiene la cola destino con el **slot +0xD70 (`getRxCompQueue`) de la interfaz**, `safeMetaCast` a `AppleBCMWLANPCIeSkywalkRxCompletionQueue`; si no es de esa clase → `"Invalid getRxCompQueue(), go next"` y descarte. Fija timestamp RX (`setRXTimestamp`) y entrega el paquete a esa cola con una llamada de tres argumentos `(paquete, 0, 0)` a través de su vtable (`?` el decompilador no nombra el slot; la familia expone `enqueuePackets` de un paquete en +0x2A8 y es el único submit de la clase además de los Broadcom +0x320–0x348).
5. Descartes y errores devuelven el paquete al pool (`AppleBCMWLANSkywalkPacketPool::freePacket`, `?` nombre exacto del thunk `0xffffff80017fa328`).
6. Al agotar el lote: `AppleBCMWLANBusInterfacePCIe::scheduleRxCompleteLegacy` (refill), y para **las 12 posibles interfaces** (`getInterfaceById(core, 0..11)`): `requestEnqueue(0, 0)` (+0x298) sobre su `getRxCompQueue` y, si falla y el rol no es 1, `flushPackets` (+0x330). Después `updateDataPathActivityTimestamp`/`handleDataPathActivity` del core.

Es decir: el frame llega al stack porque el bus lo encola **en la misma RxCompletionQueue que la interfaz registró en Skywalk** — no hay segunda copia ni conversión a mbuf en todo el camino del kext Broadcom.

## 11. Pendientes, flush y diagnóstico

- `sendPendingPacketsToStack(IO80211NetworkPacket**, uint)` (+0xF30, `0xffffff800172a716`): con RX completion válida, por cada paquete `stagePacket(pkt, 0, 0)` (+0x340), después `requestEnqueue(0, 0)` (+0x298) y, si devuelve error, `flushPackets()` (+0x330). Sin cola → `"FAIL : Invalid Rx Completion Queue available"`. Es el mecanismo con el que la interfaz entrega al stack los paquetes retenidos (p. ej. los stashed multicast cuando el segundo canal se abre).
- `freePendingPackets(IO80211NetworkPacket**, uint)` (+0xF38): delega en la cola RX: `freePendingPackets(packets, n)` (+0x348), que devuelve los paquetes al pool.
- `flushFlowQueues(ether_addr*)` (+0xED8, `0xffffff800172ac9e`): llama `AppleBCMWLANSkywalkInterface::flushFlowQueues(compañera, mac)` sobre el puntero en `ivars+120` (la interfaz Skywalk compañera de low latency, un `AppleBCMWLANLowLatencyInterface` role 2 creado por `AppleBCMWLANCore::createSkywalkInterface` vía el ioctl `setCOMPANION_SKYWALK_IF_CREATE`; esa función por par MAC recorre las colas TX submission y llama al slot +0x358, "remove flow queue del peer"), y después la base `IO80211VirtualInterface::flushFlowQueues` (`0xffffff800233235e`), que en 25.6.0 es **vacía** (no-op).
- `printDataPath(userPrintCtx*)` (+0xC58): `collectQueueStats` (+0x338) de cada cola TX submission, de la TX completion (+0x320) y de la RX completion (+0x328), con cabeceras `"Dumping TxSubQueue:%u status"`, `"Logging TX Completion Queue Status"`, `"Logging RX Completion Queue Status"`.
- `setCOMPANION_SKYWALK_IF_CREATE` (`0xffffff8001722ef2`): request+4 = 1 crea la compañera si no existe (log `"already exists"` si ya hay; escribe su BSD name en request+8, máx. 16 bytes) y setCompanionInterfaceId; request+4 = 0 la destruye (`destroySkywalkInterface`) o devuelve `0xE00002C0` si no hay.

## 12. `setMacAddress`, `stop`, `free`

- `setMacAddress(ether_addr&)` (+0xF40, `0xffffff8001720354`): se niega (`0xE00002D5`) si la sincronía AWDL está activa (`ivars+672 != 0`) o hay creación de chip interface en curso (`ivars+1112 & 1`), con log `"Cannot update MAC address because sync state: %s CreatingChipIF %s"`. Si no: si el flag `ivars+128 & 1` está activo manda el iovar `cur_etheraddr` por el `AppleBCMWLANCommander` (`runVirtualIOVarSet`, error `"cur_etheraddr failed"`); después avisa al bus: bus interface del core → virtual +0x1F8 → `safeMetaCast AppleBCMWLANBusSkywalk` → llamada virtual con la interfaz como argumento (`?` el slot exacto dentro de la vtable del bus no quedó identificado por el tipado del decompilador; por símbolos existe `setMacAddress` en la familia de interfaces, no en el bus).
- `stop(IOService*)` (+0x5C8, `0xffffff80017195e6`): si hay work loop válida y no se está ya en ella, se re-entra vía `IOWorkLoop::runActionBlock` (el bloque llama al slot +0x5C8 propio). En el camino de teardown: por cada cola TX submission, TX completion, RX completion y la multicast/work source: `IOEventSource::disable` (+0x158), `IO80211WorkQueue::removeEventSource` (+0x148), `release` (+0x28) y slot a nulo; para el work source además `removeWorkSource`. Después, llamada calificada a la base `stop` (+0x5C8 de la vtable de `IO80211AWDLProtocol`). No toca los pools (los posee el bus).
- `free` (`0xffffff8001719582`): `freeResources` (slot propio +0x11D0), `bzero` de los 0x460 bytes de ivars + `IOFreeType`, y base `free` (+0x90).

## 13. `getBSDNamePrefix` y el nombre BSD

`getBSDNamePrefix` (`0xffffff8001715a7c`, slot +0x9E8) hace `IO80211SkywalkInterface::validateDispatchQueue(this)` y devuelve `&0xffffff8001797376`, cuyo contenido es el literal `"awdl\0"` (verificado leyendo los bytes en el KC). El mismo puntero es el que `createProxInterface` pasa como tercer argumento de `withOptions` (el nombre que llega a `init`) y el que se guarda en `RegistrationInfo+0x30` (campo cuyo default en `initRegistrationInfo` es `"en"`). El nombre final lo construye `IO80211SkywalkInterface::getBSDName` (`0xffffff80022f5fca`): cachea en ivars base+1 (16 bytes) `snprintf("%s%u", ifnet_name(ifnet), ifnet_unit(ifnet))`, con el ifnet obtenido por el slot +0x8F8 de la interfaz; de ahí `awdl0`. El prefijo, por tanto, entra al ifnet a través del `RegistrationInfo`, no por una llamada separada.

## 14. Comparación con RTL8814AirLayer (interfaz infra actual)

Lo que la capa ya hace (fuente: `Source/AirLayer/RTLAirLayer.{hpp,cpp}`, solo lectura) frente a lo que AWDL exige:

| pieza | AirLayer hoy | AWDL Broadcom | reutilizable / nuevo |
|---|---|---|---|
| Clase base de interfaz | `IO80211InfraProtocol` (vía `RTLAirInterfaceStubs`) | `IO80211AWDLProtocol` (subclase de `IO80211VirtualInterface`) | **Nuevo**: subclase AWDL con ~63 slots puros de la base por implementar (`Docs/AWDL_PROTOTYPE_DESIGN.md` §3) |
| `init` | `init()` + `IO80211SkywalkInterface::init` + `setInterfaceRole(1)`/`setInterfaceId(1)` + `attach` | idéntico en forma, con role 6, `setInterfaceId(2)`, Taskmaster y ~20 ivars de máquina de estados | Reutilizable el esqueleto; nuevo el Taskmaster y el estado AWDL |
| Pools | crea 2 pools propios (`withName("rtlair:tx"/"rtlair:rx", this, 1, opts)`; 512×2048, maxBuffersPerPacket 1, flags 1) | 1 pool compartido del bus ("AppleBCMWLANSkywalkPool", 4864×2048, mismas PoolOptions shapes) | Reutilizable el mecanismo (`withName` + kern_pbufpool); nuevo solo el tamaño/propiedad (un pool, compartido con otras interfaces si existiera bus propio) |
| Colas | 3: TxSubmission (cap 256), TxCompletion (256), RxCompletion (256), con acción propia en TX | 4 TX submission (una por AC, capacidad del bus 512), TX comp, RX comp (1024), multicast opcional; creadas por el bus | Parcialmente reutilizable: la creación por fábrica de familia es la misma; nuevo el modelo 4×AC + multicast y el hecho de que Broadcom las crea el bus, no la interfaz |
| Registro | `IO80211InfraInterface::registerInfraEthernetInterface(reg, {txsub,txcomp,rxcomp}, 3, txPool, rxPool)` | `IOSkywalkEthernetInterface::registerEthernetInterface` directo, tras `registerSkywalkInterface` del bus | Reutilizable el conocimiento del ABI; para una interfaz AWDL propia el registro directo contra la familia es el camino observado en el rol 6 |
| `RegistrationInfo` | versión 1/0x130, +0x0C=3, +0x10=1, +0x14=2, +0x24=0x8000000080, headroom 64 en +0x3C, flags 0 en +0x40, MAC en +0x108 | igual hasta +0x24; sin +0x10; +0x30="awdl", +0x3A=64, +0x40=4/6, +0x48/+0x4C=0x594 | Reutilizable casi todo; **nuevos** prefijo en +0x30 y MTU 0x594; discrepancia de offset del headroom (+0x3A vs +0x3C) a resolver antes de copiar |
| Habilitación de colas | `enable/disable` (slots 266/267) giran las 3 colas en el work queue (`IOEventSource`) | `enable/disable` (+0x850/858) → `enableDatapath/disableDatapath` (+0xD98/0xDA0): mismo patrón + `requestDequeue/requestEnqueue(0,0)` + work source | Reutilizable; nuevo el work source de multicast y las llamadas (0,0) |
| TX | acción propia: copia a mbuf → radio ABI → completado inmediato por chunks de 64 | acción de la subclase PCIe: SNAP + flow queue por destino + flow ring firmware; multicast duplicado con stash | **Nuevo** casi todo el modelo (flow queues/por-destination no existe en la capa); la parte "consumir paquete de la cola y devolverlo por la completion" es el mismo contrato |
| RX | hook de radio → staging mbuf (512) → thread call → `allocatePacket` + copia + `enqueuePackets` por 64 | drain de anillos → `enqueuePackets` directo en la RX comp registrada (cero copias) | Reutilizable el contrato `enqueuePackets` (incl. `prepare` sin source queue y lotes < capacidad); nuevo el resto (refill, anillos) |
| Pendientes/flush | no existen (completado TX síncrono) | `stagePacket`/`freePendingPackets`/`flushPackets` + `flushFlowQueues` (delegada en la compañera) | **Nuevo** |
| Multicast | nada (sin filtro ni cola) | multicast queue + `triggerAWDLMulticastTx` + `p2pMulticastTx` + `freeMulticastPacket` + duplicado por segundo canal | **Nuevo** (es la parte específicamente AWDL del datapath) |
| `getBSDNamePrefix` | no overrideado (naming por InterfaceNamer; MAC con bit local para forzar unidad propia) | devuelve `"awdl"` y va al `RegistrationInfo+0x30` | **Nuevo** y trivial: un literal + el campo del registro |
| `setMacAddress` | log y return 0 (el agente de la familia mueve la interfaz; sincroniza radio con `RTL_RADIO_FN_SET_MAC`) | gates de sync/chip-IF + iovar `cur_etheraddr` por Commander + aviso al bus | Reutilizable la parte del agente; nuevo el gate y el iovar (en RTL no aplica: la MAC la fija la radio) |
| `stop`/`free` | disable+removeEventSource+release por cola, luego base; `quiesce` en el controller | idéntico en orden (disable, removeEventSource, release, removeWorkSource, base) | Reutilizable tal cual |
| Peer manager / Glue WCL | WCL glue viaja con el rol 1 de la familia (ya activo en la capa STA) | sin glue; peer manager propio de la interfaz (creado por la base start) | Nuevo para AWDL (aceptar su peer manager y las requests AWDL de `wifip2pd`) |

En síntesis: del datapath AWDL se reutilizan directamente el ciclo `init/attach`, la construcción del `RegistrationInfo`, las fábricas de pool/cola de la familia, el patrón addEventSource/enable/disable sobre el `IO80211WorkQueue`, el registro contra Skywalk y el teardown; lo genuinamente nuevo para un role 6 propio es (1) la clase base `IO80211AWDLProtocol` con sus slots puros, (2) el modelo de 4 colas por WME AC con mapa AC→queue-id, (3) el par multicast (stash/duplicado, `triggerAWDLMulticastTx`, `p2pMulticastTx`), (4) los paquetes pendientes (`stagePacket`/`freePendingPackets`/`flushPackets`) y el flush de flow queues, (5) el prefijo `"awdl"` + MTU 0x594 en el `RegistrationInfo`, (6) Taskmaster/Commander/peer manager propios, y (7) la integración con `AppleBCMWLANCore::setAwdlInterface` y las requests AWDL posteriores. Las capacidades numéricas de colas (512/1024) y pool (4864×2048) son defaults Broadcom ligados a su firmware; un driver propio puede elegir otras mientras respete los invariantes observados (lote de `enqueuePackets` < capacidad de la cola; paquetes del mismo pool que la cola).

## 15. No verificado en esta pasada

- Comportamiento de `IO80211SkywalkInterface::getRxPacketPool` (slot +0xD90, heredado por Proxi) y del `IOMapper` del pool PCIe.
- Slot exacto del bus que recibe el aviso de `setMacAddress` y el nombre exacto del thunk de `freePacket` (`0xffffff80017fa328`).
- Correspondencia argumento-a-campo de `withProviderAndCapacity` (los defaults 512/1024/4864/2048 provienen del cálculo de `allocateSkywalkBusResource` y del log propio; la asignación fina +24/+28/+32/+36/+40 se deduce del uso en `registerSkywalkInterface`/`allocSkywalkCommonResources`).
- Semántica del argumento 2 de `registerService` en el éxito de `start` (infra usa 8).
- `txCompRingDrain`, `refillRxPacket`, `signalTxPendingFlowQueue` y el interior de `AppleBCMWLANPCIeFlowQueue` (anillos de firmware): se citan por símbolos y llamadas observadas, sin pseudocódigo completo.
- Todo lo marcado `?` arriba. Ninguna de las conclusiones implica prueba en hardware RTL8814.
