# Desmontaje del `IO80211AWDLPeerManager` de una `awdl0` de driver externo (rol 6)

Cierra el `?` de [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) §8 (quién libera
`ivars+0x28`) y detalla el ciclo completo de destrucción: qué libera o cancela el manager, en qué
orden respecto al `stop` de la interfaz y al desregistro BSD, qué caminos llaman al driver y con
qué cerrojos. Base: KC 25G241 (macOS 26.7.1) en IDA. Lo no comprobado va con `?`.

Resumen de la cadena (todas las direcciones verificadas en desensamblado o pseudocódigo; §12):

1. El driver llama el slot del controlador **+0xDE8** `detachVirtualInterface(vif, sync)`
   (`0xffffff8002296116`), que invoca la ranura **+0x600** (`terminate`) de la vif con
   `options = 2*sync+1` ∈ {1,3} (§2).
2. `IO80211VirtualInterface::terminate` cierra la vif sobre el controlador
   (`IOService::close`), retira los interesados, libera la lista BPF y encadena
   `IOService::terminate` → `willTerminate` → `didTerminate` → `stop` (§3).
3. `IO80211VirtualInterface::stop` libera los recursos BPF, pide
   `disableVirtualInterface` al controlador (base: devuelve 0 sin tocar al driver) y llama
   `IO80211SkywalkInterface::stop` (§4).
4. **`IO80211SkywalkInterface::stop` (`0xffffff80022f885c`) es quien libera el manager** para
   rol ≠ 2: llama `IO80211PeerManager::stop` (ranura +0x290) y después `release()` sobre
   `ivars+0x28`, y pone el puntero a 0 para todos los roles (§5). Con los retains equilibrados
   (§7), ese `release` lleva el contador a 0 y `IO80211AWDLPeerManager::free` corre de forma
   síncrona en el mismo hilo.
5. `IO80211PeerManager::stop` = `removeAllPeers` (ranura +0x280) + `freeResources`
   (ranura +0x288, tail-jmp): borrado de pares —con callback +0xF38 al driver— y desactivación
   de timers y colas (§6).
6. El desregistro BSD (`deregisterNetworkInterface`) ocurre **después** del release del manager,
   dentro del mismo `swi_stop` (§5.3). Nada de esto corre bajo el workloop/gate del controlador:
   es el hilo de terminación de IOKit; el manager toma su propio cerrojo data-path dentro de
   `removeAllPeers` (§8).

## 1. Vtables y ranuras implicadas

| Clase | vptr | Ranura | Destino |
|---|---|---|---|
| `IO80211SkywalkInterface` | `0xffffff800246af10` | +0x090 `free` | `0xffffff80022f6184` |
| | | +0x400 `willTerminate` | `IOSkywalkNetworkInterface::willTerminate` `0xffffff8002b0bfea` |
| | | +0x408 `didTerminate` | `IOSkywalkInterface::didTerminate` `0xffffff8002afbe12` |
| | | +0x5C8 `stop` | `0xffffff80022f885c` |
| | | +0x600 `terminate` | `IOService::terminate` `0xffffff8000a937b0` |
| | | +0x678 `detach` | `IOService::detach` `0xffffff8000a94670` |
| `IO80211VirtualInterface` | `0xffffff8002472b88` | +0x090 `free` | `0xffffff8002330b8c` |
| | | +0x5C8 `stop` | `0xffffff8002330cee` |
| | | +0x600 `terminate` | `0xffffff8002330c50` |
| | | +0x678 `detach` | `0xffffff800233027c` |
| | | +0xF38 `freePendingPackets` | base `0xffffff8002334e88` (solo log) |
| `IO80211PeerManager` | `0xffffff800245d080` | +0x090 `free` | `0xffffff80022492f6` (celda `0xffffff800245d110`) |
| | | +0x190 `destroyReporters` | `0xffffff80022496ae` |
| | | +0x258 `disableWorkQueueSources` | `0xffffff80022497e6` |
| | | +0x278 `removePeer(Peer*)` | `0xffffff8002254096` (celda `0xffffff800245d2f8`) |
| | | +0x280 `removeAllPeers` | `0xffffff80022542e4` (celda `0xffffff800245d300`) |
| | | +0x288 `freeResources` | `0xffffff8002249000` (celda `0xffffff800245d308`) |
| | | +0x290 `stop` | `0xffffff80022492d2` (celda `0xffffff800245d310`) |
| `IO80211AWDLPeerManager` | `0xffffff800245a868` | +0x090 `free` | `0xffffff80021c4aae` (celda `0xffffff800245a8f8`) |
| | | +0x160 `interfaceMonitor` | `0xffffff80021ed628` |
| | | +0x190 `destroyReporters` | `0xffffff80021cc87e` |
| | | +0x278 `removePeer` | `0xffffff800221721c` (celda `0xffffff800245aae0`) |
| | | +0x280 `removeAllPeers` | `0xffffff800222be56` (celda `0xffffff800245aae8`) |
| | | +0x288 `freeResources` | `0xffffff80021c48b0` |
| | | +0x290 `stop` | `0xffffff80021c487a` (celda `0xffffff800245aaf8`) |
| `IO80211Controller` | `0xffffff8002461158` | +0x5D8 `close` | `IOService::close` `0xffffff8000a935c0` |
| | | +0x850 `executeCommand` | `IONetworkController::executeCommand` `0xffffff80026f51e0` |
| | | +0xC68 `getWorkQueue` | `0xffffff8002295e28` |
| | | +0xD10 `disableVirtualInterface` | `0xffffff80022965bc` |
| | | +0xD88 `allocIO80211RecursiveLock` | `0xffffff800229a99a` |
| | | +0xDE8 `detachVirtualInterface` | `0xffffff8002296116` (celda `0xffffff8002461f40`) |
| `IO80211Peer` | `0xffffff800245c748` | +0x158 `stop` | `0xffffff800223f858` |
| | | +0x200 `flushQueues(uint)` | `0xffffff8002240ea0` |

Los destructores de ambos managers son triviales (solo `OSObject::~OSObject` + `operator delete`
tipado: `pm_D1` `0xffffff8002246342`, `pm_D0` `0xffffff800224634c` con `delete` de 32 B,
`awdl_D1` `0xffffff80021c1376`, `awdl_D0` `0xffffff80021c1380` con `delete` de 40 B). Todo el
desmontaje real vive en `free` (+0x090), `freeResources` (+0x288) y `disableWorkQueueSources`
(+0x258).

## 2. Punto de entrada del driver

`IO80211Controller::detachVirtualInterface(IO80211VirtualInterface*, bool)`
(`0xffffff8002296116`, ranura **+0xDE8**): carga `esi = 2*bool+1` en `0xffffff800229611d`, lee la
ranura +0x600 de la vif en `0xffffff8002296124` y salta (`0xffffff800229612c`). Es decir, una
llamada a `vif->terminate(1)` (síncrona) o `terminate(3)`. No hay ningún llamador de esta función
en el KC (solo las celdas de vtable del controlador: la familia `0xffffff8002461f40` y la de
Broadcom `0xffffff80017ea5e0`): es la puerta de entrada del kext.

`IO80211VirtualInterface::terminate` (`0xffffff8002330c50`):

1. Marca el byte `ivars_vif+0x52` = 1 (bandera de terminación; ivars de la vif en `this+0x128`).
2. Toma el provider con la ranura +0xBA8 (`getController`) y llama su ranura **+0x5D8**
   (`call qword ptr [rcx+5D8h]` en `0xffffff8002330c7e`) con `(controller, vif, options)` =
   `IOService::close`. En la vtable base del `IO80211Controller` esa celda es `IOService::close`
   (`0xffffff8000a935c0`): un override de `close` del controlador RTL recibiría la llamada aquí.
3. `IOService::deRegisterInterestedDriver(provider, vif)` (`call` en `0xffffff8002330c96`).
4. Libera la lista BPF (`IO80211BPFListFree`, `call` en `0xffffff8002330cab`; el bloque de
   `ivars_vif+0x28` con `IOFreeTypeImpl` en `0xffffff8002330cc9`).
5. Tail-jmp a `IOService::terminate` (`0xffffff8002330ce8` → `0xffffff8000a937b0`), que lanza
   `terminatePhase1(options|4)`. La maquinaria IOKit llama después `willTerminate` (ranura
   +0x400: `IOSkywalkNetworkInterface::willTerminate` `0xffffff8002b0bfea`, que retira los
   logical links no por defecto y las reglas de steering, y encadena
   `IOSkywalkInterface::willTerminate` `0xffffff8002afbdfe`, que fija el byte de terminación de
   su bloque de ivars), `didTerminate` (+0x408, `0xffffff8002afbe12`, heredada sin override en
   ninguna de las dos interfaces) y finalmente `stop` (+0x5C8).

El detach del plano (`IO80211VirtualInterface::detach`, `0xffffff800233027c`) solo hace
`detachFromParent` en el plano `kIO80211PlaneName` + `IOService::detach`: corre en la fase final
de IOKit, no es camino del driver.

## 3. `IO80211VirtualInterface::stop` (`0xffffff8002330cee`)

1. Log `"IO80211VirtualInterface::willTerminate: called, vif %s\n"` (enabled según el byte
   `ivars+0x50`, test en `0xffffff8002330d10`).
2. `ivars+0x52 = 1` (`0xffffff8002330d2d`).
3. `freeBpfResources` (`call` en `0xffffff8002330d34`).
4. Si la vif estaba habilitada (bit 0 de `ivars+0x50`): `getController` (ranura +0xBA8,
   `0xffffff8002330d4c`) y **`disableVirtualInterface`** (ranura +0xD10 del controlador,
   `call` en `0xffffff8002330d5b`). La base `IO80211Controller::disableVirtualInterface`
   (`0xffffff80022965bc`) es un `return 0`: no llega al driver. Después `ivars+0x50 = 0`
   (`0xffffff8002330d78`).
5. Tail-jmp a `IO80211SkywalkInterface::stop` (`0xffffff8002330d86`).

## 4. `IO80211SkywalkInterface::free` no libera el manager

`IO80211SkywalkInterface::free` (`0xffffff80022f6184`) hace `release` de los ivars `+0xD8`,
`+0xB8`, `+0xA8`, `+0x40`, `+0x48`, `+0x18`, `+0xC8`, `+0x30`, `+0x38`, `memset_s` del bloque de
0xF0 B (`call` en `0xffffff80022f631b`) y `IOFreeTypeImpl` (`0xffffff80022f6335`). **No toca
`+0x28`**: cuando `free` corre, el puntero ya es 0 y el objeto ya no existe (lo garantiza el
`stop`, §5).

## 5. `IO80211SkywalkInterface::stop` (`0xffffff80022f885c`): el punto que libera el manager

Ivars base = `*(this+0x120)`. Orden verificado:

1. `IOService::PMstop` sobre la vif (ranura +0x798, `call` en `0xffffff80022f889e`).
2. Objeto de `ivars+0x80` (pipe de log): ranura +0x870 (`call` en `0xffffff80022f88ba`) y
   `release` (`0xffffff80022f88d6`); `release` de `ivars+0x78` (`0xffffff80022f88f7`) y de
   `ivars+0x50` (`0xffffff80022f8915`), con `ivars+0x50 = 0` en `0xffffff80022f891f`.
3. Test de rol: `cmp dword ptr [rax+58h], 2` en `0xffffff80022f8927`; si el rol es 2 (P2P, que
   hereda el manager del padre) salta directo a `0xffffff80022f89a5`. Para el resto de roles
   (incluido el 6):
   1. `IO80211Peer::stop` (ranura +0x158) sobre el par broadcast de `ivars base+0x530`
      (`call` en `0xffffff80022f8947`) y sobre el par unicast de `ivars base+0x538`
      (`call` en `0xffffff80022f896e`). La base `ivars+0x530` sale de `mov rax, [rcx+530h]` en
      `0xffffff80022f8935` con `rcx = manager+0x18`.
   2. **`manager->stop()`** (ranura +0x290, `call qword ptr [rax+290h]` en `0xffffff80022f8982`).
   3. **`manager->release()`** (ranura +0x28, `call qword ptr [rax+28h]` en `0xffffff80022f899b`).
4. **`ivars+0x28 = 0`** para todos los roles (`mov qword ptr [rax+28h], 0` en
   `0xffffff80022f89a5`).
5. `IO80211Glue::stop` directo (`call` en `0xffffff80022f89b9` → `0xffffff80021939ac`) sobre el
   objeto de `ivars+0xD8`, si existe.
6. Llamada desvirtualizada a la ranura +0x5D8 de la vtable de `IOSkywalkNetworkInterface`
   (`mov rax, cs:off_FFFFFF8002450128` en `0xffffff80022f89be` — el vptr guardado es
   `__ZTV26IOSkywalkEthernetInterface`, `call qword ptr [rax+5D8h]` en `0xffffff80022f89cb`):
   cae en `IOSkywalkNetworkInterface::stop` (`0xffffff8002b09e66`) (§5.3).
7. Log final de contadores de referencias (`"IO80211SkywalkInterface::stop[%s] RC=<%d> core
   RC=<%d>\n"`; c-string con la ranura +0x148 en `0xffffff80022f89e7`, `getRetainCount` en
   `0xffffff80022f89fb` y `0xffffff80022f8a0f`).

Este `stop` corre en el hilo de terminación de IOKit, sin gate del controlador y sin el cerrojo
data-path del manager.

### 5.1 Cierre del `?` de AWDL_PEERMANAGER_CREATION §8

El release de `ivars+0x28` no estaba en `free` porque está en `stop`, con guard de rol: el
manager se detiene y libera en `IO80211SkywalkInterface::stop` (`0xffffff80022f8982` y
`0xffffff80022f899b`) y el puntero se anula en `0xffffff80022f89a5`.

### 5.2 Ranura +0x158 sobre los pares persistentes

Los dos seudopares de la base (`+0x530` broadcast, creado con `IO80211Peer::withAddressAndManager`
sobre la dirección broadcast en `initWithInterface`; `+0x538` unicast) se detienen con
`IO80211Peer::stop` (`0xffffff800223f858`: libera el almacen de latencia TX de `ivars+0x4F0`,
hace `release` del objeto de `ivars+0x4B8`, llama su ranura +0x200 con 0 y limpia su bit del
mapa de interfaces activas del controlador). El desmontaje definitivo de ambos es en
`freeResources` (§6.5).

### 5.3 Desregistro BSD, después del manager

`IOSkywalkNetworkInterface::stop` (`0xffffff8002b09e66`): si el ivar de registro no es nulo
(lectura en el primer enunciado del pseudocódigo), llama
`IOSkywalkNetworkInterface::deregisterNetworkInterface` (`0xffffff8002b09e9c`) —retira logical
links y reglas de steering, hace `IOSkywalkLogicalLink::setInterface(nullptr)`, libera el enlace
y sus colas y libera el bloque de parámetros—; después encadena `IOService::stop`
(`0xffffff8000a93470`, sin efecto sin IOUserServer). El orden queda: **manager stop + release →
glue stop → desregistro BSD**, todo dentro de `swi_stop`.

## 6. `IO80211PeerManager::stop` y el desmontaje del manager

### 6.1 `IO80211AWDLPeerManager::stop` (`0xffffff80021c487a`)

Si `this+0x20` (ivars AWDL) existe, llama la ranura **+0x118** del objeto en `ivars AWDL+0x1E0`
(`call qword ptr [rax+118h]` en `0xffffff80021c489b`): es el `IO80211P2PSupervisor` creado en
`initWithInterface`, y su ranura +0x118 es `IO80211P2PSupervisor::stop` (`0xffffff8002390a46`),
que hace `release` del manager almacenado en `supervisor+0x10` y anula el puntero — este release
equilibra el retain que `initWithInterface` hizo al registrar el puntero. Después tail-jmp a
`IO80211PeerManager::stop` (`0xffffff80021c48aa`).

`IO80211PeerManager::stop` (`0xffffff80022492d2`): `call qword ptr [rax+280h]`
(`removeAllPeers`) en `0xffffff80022492de` y tail-jmp a la ranura +0x288 (`freeResources`) en
`0xffffff80022492f0`.

### 6.2 `removeAllPeers` (`0xffffff80022542e4`)

1. `lockDataPath`: ranura **+0x118** del cerrojo en `ivars base+0x78` (`call` en
   `0xffffff8002254309`; el objeto lo crea `IO80211Controller::allocIO80211RecursiveLock`,
   ranura +0xD88). Con fallo registra `"lockDataPath failed"` y sale sin borrar nada.
2. Recorre la cola de pares (`ivars base+0x540`) con `IO80211CommandQueue::map` (lea de
   `_removePeers` en `0xffffff8002254358`, `call` en `0xffffff800225436e`). `_removePeers`
   (`0xffffff800225443f`) solo devuelve 12 (marca la entrada para flush) y el mecanismo del map
   entrega cada par a `_addToFlush` (`0xffffff800224f6b0`), que lo encola en la cola de flush
   `ivars base+0x758`.
3. `IO80211Peer::flushQueues(0)` (ranura +0x200) sobre `+0x538` (`call` en `0xffffff8002254388`)
   y `+0x530` (`call` en `0xffffff80022543a3`), con el cerrojo tomado.
4. `unlockDataPath` (ranura +0x120, `call` en `0xffffff80022543b4`).
5. `setPeersCount` (`call` en `0xffffff8002254416`) y **`removeFlushedPeers`** (`call` en
   `0xffffff800225441e`), ya **sin** el cerrojo data-path.

`removeFlushedPeers` (`0xffffff800224f73a`) desencola de `+0x758` con
`IO80211CommandQueue::lockDequeue` (`call` en `0xffffff800224f755`) y llama la ranura **+0x278**
del manager (`call qword ptr [rcx+278h]` en `0xffffff800224f768`) por cada par: el borrado real
—y los callbacks al driver— corren fuera del cerrojo data-path.

### 6.3 `IO80211AWDLPeerManager::removePeer` (`0xffffff800221721c`) por par

1. Limpia los estados que referencian al par (sidecar, AirPlay, timesync, high-thruput, RTG,
   ranging, canal común: `updateAWDLCommonChannel(false)` en `0xffffff80022177dc`).
2. **`IO80211AWDLPeer::flushPacketsWaitingForPPP`** (`call` en `0xffffff80022177e4`):
   vacía la cola PPP del par en lotes de 64 (`dequeuePPPWaitPacketFromHead`, `call` en
   `0xffffff80021bff50`) y entrega cada lote a la **ranura +0xF38 de la vif**
   (`call qword ptr [rax+0F38h]` en `0xffffff80021bff87`). Array de 512 B (`IOMallocZeroData`),
   liberado con `IOFreeData` por la propia función; los paquetes pasan a ser propiedad del slot.
3. `pppDone(false)` (`call` en `0xffffff80022177ee`, con `esi = 0` en `0xffffff80022177ec`).
4. **`IO80211SkywalkInterface::detachPeer`** (ranura **+0xDF8** de la vif): carga la vif de
   `ivars base+0x560` (`0xffffff80022177f7`) y la MAC del par (`ivars comunes+0x58`) y llama
   `call qword ptr [rax+0DF8h]` en `0xffffff8002217809`.
5. Tail-jmp a `IO80211PeerManager::removePeer` (`0xffffff8002217823` → `0xffffff8002254096`),
   que publica el evento 47 (`postMessageSync(0x2F, buf, 0x44)` en `0xffffff8002254248`, solo si
   `ivars base+0x560` no es nulo), llama `IO80211Peer::stop` (ranura +0x158 del par) y, si
   procede, `deletePeerFromFWCache`, y termina con `release()` del par.

`IO80211Peer::free` (`0xffffff800223f96a`) libera los tres bloques de ivars del par, llama
`IO80211PeerManager::removePeerOperation` (contador) y hace **`release` del manager** que el par
retenía en sus ivars comunes `+0x60`, antes de liberar el bloque de 0x508 B.

### 6.4 `IO80211AWDLPeerManager::freeResources` (`0xffffff80021c48b0`)

1. Libera el array NDP de `ivars AWDL+0x41C8` (`IOFreeTypeVarImpl`, 104 B × contador de
   `+0x41D8`).
2. **`release` de los 13 timers AWDL** (`ivars AWDL+0x2848`…`+0x28A8`), sin `cancelTimeout` ni
   `disable` previos. Handlers (de `initWithInterface`): `handleMasterIndicationTimer` (+0x2848),
   `handleUmiTimer` (+0x2850), `handleMulticastTimer` (+0x2858), `handleAssistedDiscoveryTimedOut`
   (+0x2860 `?`, por orden de creación), `handleSuspendTimer` (+0x2868),
   `handleAWDLRecoveryTimer` (+0x2870), `jumpStartDFSProxy` (+0x2878), `discoveryTimeout`
   (+0x2880), `handleLinkChangeTimer` (+0x2888), `handleInfraMonitorTimer` (+0x2890),
   `handleAwdlScanTimer` (+0x2898), **`handleAwdlLLATimer` (+0x28A0)**, y `actionFrameProcessThread`
   en +0x28A8 `?` (por descarte).
3. Tail a `IO80211PeerManager::freeResources` (`0xffffff8002249000`).

### 6.5 `IO80211PeerManager::freeResources` (`0xffffff8002249000`)

1. **`disableWorkQueueSources`** (ranura +0x258, `call` en `0xffffff800224900c`), que sobre los
   tres timers de la base hace `cancelTimeout` (ranura +0x218) e `disable` (ranura +0x158):
   `+0x720` data-path (`0xffffff800224983d`/`0xffffff8002249851`), `+0x708` unflow-control
   (`0xffffff800224986a`/`0xffffff800224987e`), `+0x718` monitor de interfaz
   (`0xffffff8002249897`/`0xffffff80022498ab`); después `IO80211CommandQueue::flush` de la cola
   `+0x768` (`call` en `0xffffff80022498cb`), con `dword ivars+0x6B0 = 0` y byte
   `ivars+0x714 = 0` (pseudocódigo).
2. `IO80211Peer::stop` (ranura +0x158, `call` en `0xffffff8002249025` y `0xffffff800224905f`) +
   `release` (`0xffffff800224903e`, `0xffffff8002249078`) de los seudopares `+0x530`/`+0x538`.
3. `destroyReporters` (ranura +0x190, `call` en `0xffffff8002249090`).
4. Fault reporter (`ivars base+0x8C8`): `IO80211FaultReporter::unregisterCallbacks(manager)`
   (ranura +0x128, `call` en `0xffffff80022490ac`; `0xffffff80022c0ba0`) + `release`
   (`0xffffff80022490c5`); y `release` del objeto de `+0x8B0` (`0xffffff80022490e6`).
5. `destroyPeerManagerDatapathPcapLogger` (`call` en `0xffffff80022490fb`).
6. `release` de los loggers CC (`ivars+0x9D8`, `+0x9E8`: `0xffffff8002249113`,
   `0xffffff8002249134`) y cierre+release de los de `+0x9D0` y `+0x9E0` (ranura +0x870 en
   `0xffffff8002249155` y `0xffffff800224918f`, luego `release`).

### 6.6 `IO80211AWDLPeerManager::free` (`0xffffff80021c4aae`) — cadena del release final

1. `destroyReporters` (ranura +0x190) y byte `ivars base+0x8D8 = 0`.
2. `release` de: 3 `IOPacketQueue` de `ivars AWDL+0x28B8`–`+0x28C8`, 2 de `+0x28D0`/`+0x28D8`,
   el `IO80211P2PSupervisor` de `+0x1E0` (el singleton global `_supervisor` sobrevive; solo cae
   esta referencia), 2 colas de servicio `+0xA5C0`/`+0xA5C8`, el
   `IO80211PeerBssSteeringManager` de `+0x40E8` y el `IO80211RealTimePeerManager` de `+0x1E8`.
3. `IOFreeTypeImpl` del bloque de `+0xFE90` (`call` en `0xffffff80021c4c08`).
4. `resetServiceRegStats` (`call` en `0xffffff80021c4c1b`) y
   `freeAwdlPacketDescriptorPool` (`call` en `0xffffff80021c4c23`).
5. `memset_s` de los 0x10440 B de ivars AWDL (`call` en `0xffffff80021c4c38`) y
   `IOFreeTypeImpl` (`0xffffff80021c4c50`).
6. Tail a `IO80211PeerManager::free` (`0xffffff80022492f6`).

### 6.7 `IO80211PeerManager::free` (`0xffffff80022492f6`)

Sobre el bloque de ivars base (0xC48 B): `IOLockFree` de `+0x740` (`call` en
`0xffffff800224931c`) y `+0x748` (`0xffffff800224933c`); `release` del cerrojo data-path `+0x78`
(`0xffffff800224935c`); `release` del command gate `+0x518` (`0xffffff800224937a`); las 4 colas
de pools de descriptores `+0x4F8`–`+0x510` con `IO80211CommandQueue::flush` (`call` en
`0xffffff800224939c`) + `release`; los buffers globales de log (`IOFreeData` en
`0xffffff80022493e9` y `0xffffff8002249418`); la cola de pares `+0x540` (flush
`0xffffff8002249442` + release `0xffffff800224945d`) y las colas `+0x760`, `+0x758`, `+0x768`
(flush en `0xffffff800224947e`, `0xffffff80022494ba`, `0xffffff80022494f6` + releases); el
`IO80211P2PDataPathManager` de `+0x00` (`0xffffff8002249531`); el cronómetro `+0x5C0`
(`0xffffff8002249605`); los tres timers `+0x718`, `+0x708`, `+0x720` (`release` en
`0xffffff8002249599`, `0xffffff80022495bd`, `0xffffff80022495e1`); `+0x9B0` (`0xffffff8002249629`);
`IOLockFree` de `+0x628` (`0xffffff800224964a`); el workqueue del controlador `+0x528`
(`release` en `0xffffff8002249665`); `memset_s` (`0xffffff8002249682`), `IOFreeTypeImpl`
(`0xffffff8002249696`) y `IO80211QueueDescriptor::free`.

## 7. Contador de referencias del manager

Retenedores localizados:

| Retenedor | Adquisición | Liberación |
|---|---|---|
| `ivars+0x28` de la vif | publicación en `IO80211SkywalkInterface::start` | `release` en `swi_stop` (`0xffffff80022f899b`) |
| `IO80211P2PSupervisor` (singleton `_supervisor`, campo `+0x10`) | retain en `AWDL::initWithInterface` | `IO80211P2PSupervisor::stop` (`0xffffff8002390a46`), invocado desde `awdl_stop` |
| cada par (ivars comunes `+0x60`) | retain al crear el par | `IO80211Peer::free` (`0xffffff800223f96a`) |

Con los pares ya borrados (§6.2) y el supervisor detenido (§6.1), el `release` de `swi_stop`
llega a 0 y la cadena `D0 → free` corre de forma síncrona en el hilo que ejecuta el `stop`.
No se ha localizado ningún otro retenedor en el KC (los puentes apple80211 usan el puntero sin
retener), pero el barrido de retenedores no fue exhaustivo `?`.

## 8. Cerrojos y contexto de hilo

- Todo el desmontaje (terminate→willTerminate→stop→free, incluida la destrucción del manager)
  corre en el **hilo de terminación de IOKit** que llamó `detachVirtualInterface`; no pasa por el
  command gate del controlador ni por su workqueue.
- El único cerrojo que el manager toma es el **data-path lock** (`IO80211RecursiveLock` en
  `ivars base+0x78`), dentro de `removeAllPeers` (`lock` en `0xffffff8002254309`, `unlock` en
  `0xffffff80022543b4`), y se suelta antes de borrar los pares (`removeFlushedPeers` en
  `0xffffff800225441e`). El `disableWorkQueueSources` y toda la cadena `free` corren sin él.
- Los callbacks de timers corren en el **workqueue del controlador** (obtenido con la ranura
  +0xC68). El desmontaje cancela (`+0x218`) y desactiva (`+0x158`) los tres timers de la base
  antes de liberarlos; los 13 timers AWDL se liberan sin cancelación previa y confían en
  `IO80211TimerSource::free` (`0xffffff80021a71e0`), que llama la ranura **+0x148** del workqueue
  (`call` en `0xffffff80021a71f8`) — celda que en `IO80211WorkQueue` es
  `IOWorkLoop::removeEventSource` (`0xffffff8000ac9660`) — y encadena
  `IOTimerEventSource::free` (`tail` en `0xffffff80021a720e`). Que ese desenganche sea una
  barrera suficiente frente a un callback en vuelo no está trazado `?`.
- `IOWorkLoop::removeEventSource` no tiene ninguna llamada directa desde IO80211Family: la
  desconexión de fuentes pasa siempre por los `free` de los propios objetos (el manager nunca
  llama `removeEventSource` explícitamente).

### 8.1 Callbacks al driver durante el desmontaje

| Callback | Camino | Cerrojo | Hilo |
|---|---|---|---|
| Ranura +0xF38 `freePendingPackets` de la vif | `removeFlushedPeers` → `removePeer` → `flushPacketsWaitingForPPP` (`0xffffff80021bff87`) | ninguno propio del manager (data-path ya soltado) | hilo de stop |
| Ranura +0xF38 | `monitorPeer` → `ageoutPacketsWaitingForPPP` (`0xffffff80021aa984`), si un par expira durante el desmontaje | data-path lock | workqueue del controlador |
| Ranura +0xF30 `sendPendingPacketsToStack` | solo por ioctl 415 (`pppDone(true)`); no forma parte del desmontaje | gate del controlador | hilo del ioctl |
| Ranura +0xDF8 `detachPeer` de la vif | `removePeer` (`0xffffff8002217809`); la base lo implementa en la familia, no llega al driver sin override | ninguno propio | hilo de stop |
| Evento 47 (`postMessageSync`) | `IO80211PeerManager::removePeer` (`0xffffff8002254248`) | ninguno propio | hilo de stop |
| `IOService::close` del controlador (ranura +0x5D8 del provider) | `VI::terminate` (`0xffffff8002330c7e`) | ninguno | hilo de stop; solo si el controlador RTL sobreescribe `close` |
| `disableVirtualInterface` (ranura +0xD10) | `VI::stop` (`0xffffff8002330d5b`), solo si la vif estaba habilitada | ninguno | base: `return 0`, no llega al driver |

## 9. Huecos que quedan en la familia

1. **El asiento `*(ctrl ivars)+0x190` (la vif rol 6) no se limpia.** El único escritor
   localizado es el store indexado de `createPeerManager` (`mov ecx, 190h` en
   `0xffffff8002330974`, `mov [rax+rcx], rbx` en `0xffffff80023309df`); un barrido de stores
   directos con desplazamiento 0x190 sobre el `__text` de IO80211Family no da ningún resultado,
   y ni `IO80211Controller::stop` (`0xffffff8002292d08`) ni `disableVirtualInterface` lo tocan.
   Un store indexado a cero en otro sitio no se ha descartado `?`. Mientras tanto,
   `getInterfaceActiveBitmap` (`0xffffff800229a8c6`) seguiría leyendo el puntero tras el
   desmontaje `?`.
2. La anulación del registro del `IO80211P2PSupervisor` singleton global entre interfaces no está
   trazada (su `initWithInterface` reconfigura el manager en la reutilización) `?`.
3. La serialización exacta entre un `ageoutPacketsWaitingForPPP` en vuelo (workqueue, con cerrojo
   data-path) y el `removeFlushedPeers` del hilo de stop (sin cerrojo) sobre el mismo par no está
   trazada: la familia se apoya en el flag `+1758` del par y en la cola `+0x758` `?`.
4. `IO80211Peer::flushQueues(0)` sobre los seudopares (§6.2 paso 3) puede soltar paquetes de sus
   colas; su destino (pool o slot de la vif) no está trazado `?`.

## 10. Receta para el teardown de `awdl0` en el RTL8814

1. **Entrada única**: `controller->detachVirtualInterface(vif, true)` (ranura **+0xDE8** del
   controlador; equivale a `vif->terminate(1)`). Con el retorno síncrono, al volver la llamada la
   vif ya pasó `terminate → willTerminate → stop → free` y el manager está destruido. No hay que
   llamar a nada más de la familia para el desmontaje.
2. **No liberar el manager**: el release lo hace `swi_stop` (`0xffffff80022f899b`). Si el kext
   capturó el puntero (paso 5 del plan), debe soltar su propia referencia (`release`) o el
   contador nunca llegará a 0: fuga del bloque de 66 KiB de ivars AWDL, de los 13 timers AWDL y
   los 3 de la base, y de todas las colas.
3. **No leer `ivars+0x28` después de iniciar el teardown**: queda a 0 en `0xffffff80022f89a5`;
   un puntero capturado antes y usado después del `stop` es use-after-free (la destrucción es
   síncrona con el `stop`).
4. **Los overrides +0xF30/+0xF38 de la vif deben seguir válidos hasta que
   `detachVirtualInterface` retorne**: durante el `stop` la familia llama `+0xF38` por cada par
   con paquetes retenidos (§8.1). Con la cola RX propia, devolver los paquetes al pool en `+0xF38`
   y no tocar el pool después. Los paquetes entregados por `+0xF30` solo llegan por el ioctl 415,
   que ya no ocurrirá una vez el daemon deje de ver la interfaz.
5. **El radio puede apagarse antes, pero no el código**: los caminos `monitorPeer` (workqueue)
   y `removePeer` (hilo de stop) pueden aún invocar slots de la vif mientras corre la
   terminación; la vtable de la vif debe permanecer intacta hasta el `free`.
6. **No reutilizar el asiento `*(ctrl ivars)+0x190`** para una segunda vif sin comprobar que la
   familia lo dejó vacío (§9.1): la evidencia actual dice que queda sin limpiar `?`.
7. Un override de `close` en el controlador RTL (ranura +0x5D8 del provider) recibiría
   `(this, vif, options)` desde `VI::terminate`; en la base no se llama a ningún método del
   driver en ningún punto del desmontaje.
8. El último evento hacia el daemon es el 47 (`postPeerAbsence`) de `removePeer`, publicado
   mientras la vif todavía existe; después el pipe de eventos se pierde con la vif.

## 11. Compatibilidad entre builds

Todos los offsets de ivars y ranuras citados son del 25.6.0 (25G241). Los símbolos clave están
exportados en 23J631 y 24H232 (volcados `Docs/re/io80211_{23J631,24H232}/syms.txt`:
`IO80211PeerManager::stop`, `free`, `removeAllPeers`, `freeResources`,
`IO80211AWDLPeerManager::{stop,free,...}`), pero las ranuras de vtable y los offsets de ivars
(+0x28, +0x58, +0x120, +0x190, +0x530/0x538, +0x540, +0x718…) hay que re-verificarlos por build
antes de fijar constantes, igual que en [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) §9 `?`.

## 12. Método y límites

- IDA 9.4 headless (`idat -A -S`) sobre copia de `~/re_cache/ida/boot_awdl.kc.i64` en
  `/tmp/awdl_teardown`; descompilación de ~40 funciones (dec/…dec5/) y volcados de desensamblado
  con direcciones de instrucción para los sitios citados (s1–s11).
- Vtables resueltas celda a celda: `IO80211PeerManager` vptr `0xffffff800245d080` y
  `IO80211AWDLPeerManager` vptr `0xffffff800245a868` (deducidas y verificadas por los xrefs de
  datos de `stop`/`removePeer`/`removeAllPeers`/`freeResources`/`destroyReporters`, coincidentes
  en los cuatro desplazamientos); `IO80211SkywalkInterface` vptr `0xffffff800246af10` (por la
  celda de `free` `0xffffff800246afa0`).
- Las direcciones de la factoría y de la publicación (`0xffffff80022f5bd8`, `0xffffff80023308c2`)
  se toman de [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) sin re-verificar aquí
  (el store del asiento del controlador sí: `0xffffff8002330974`, `0xffffff80023309df`; y
  `IO80211AWDLPeerManager::withInterface` `0xffffff80021c14bc` se resolvió de nuevo por nombre);
  las de `flushPacketsWaitingForPPP`, `pppDone`, el 415 y el ageout, de
  [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) (la llamada de flush y la de `pppDone`
  en `removePeer` sí se re-verificaron: `0xffffff80022177e4`, `0xffffff80022177ee`).
- `?` explícitos: barrido exhaustivo de retenedores del manager (§7), barrera del
  `removeEventSource` frente a callbacks en vuelo (§8), limpieza del asiento `+0x190` (§9.1),
  ciclo de vida del singleton `P2PSupervisor` (§9.2), serialización ageout↔removeFlushedPeers
  (§9.3), destino de los paquetes de `flushQueues` (§9.4).
