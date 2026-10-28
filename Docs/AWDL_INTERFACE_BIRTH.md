# Nacimiento de la interfaz virtual AWDL en el kernel (macOS 26 / BootKernelExtensions)

Cómo nace `awdl0` dentro del kernel desde que el handler moderno de `VIRTUAL_IF_CREATE` invoca el
slot +0x1160 de `IO80211InfraProtocol` con rol 6 hasta que la property `IOInterfaceName` aparece en
el IORegistry y dispara la notificación `IOServiceMatched` que libera a `wifip2pd` (ver
[`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1). Cubre: la ruta Broadcom del rol
6, la elección del nombre BSD y del unit, el registro ifnet, las properties publicadas, el orden
`registerService` ↔ attach BSD y la respuesta de `SIOCGIFMEDIA`.

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), IDA 9.4 headless
(`idat -A -S`) sobre copias de `~/re_cache/ida/boot.kc.i64` en
`Build/ida_awdl/task_awdl_birth/bcm` y `.../fam` (scripts `bcm_birth.py`, `fam_birth*.py`, salidas
`out*.txt`), símbolos de `Docs/re/io80211_25.6.0/` y el binario `wifip2pd.x86_64`
(md5 `a120cfccfa2783303604ce5be9e1763a`) para verificar el filtro de usuarios. Las direcciones son
VA del kernel salvo indicación. Lo no verificado va como `?`. El punto de entrada ya está cerrado en
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4: el handler moderno valida
`len == 32` y llama el puente que castea a `IO80211InfraProtocol` y ejecuta el slot +0x1160, que en
el kext Broadcom es `AppleBCMWLANCore::setVIRTUAL_IF_CREATE`.

## 1. Respuesta corta

1. El rol 6 llega a `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`), que lee el rol
   del payload en `+0x0C` (`0xffffff800167891c`) y llama `AppleBCMWLANCore::createProxInterface`
   (`0xffffff800165dcbe`, call en `0xffffff8001678bdd`). Esta crea
   `AppleBCMWLANProximityInterface` vía `withOptions(core, mac=payload+4, "awdl")`
   (`0xffffff800165dd6d`), la adjunta al controlador (`IOService::attach` por el slot +0x670 en
   `0xffffff800165dd92`), rellena un `IOSkywalkEthernetInterface::RegistrationInfo` de 0x130 bytes
   (prefijo `"awdl"` en +0x30, unit 0 en +0x38, MAC en +0x108, medios en +0x24/+0x28) y llama
   `AppleBCMWLANProximityInterface::start(core, RegInfo)` (`0xffffff800165dead`).
2. `Proxi::start` (`0xffffff8001729376`) llama la `start` de la base por la vtable importada de
   `IO80211AWDLProtocol` (`[r14+0x5D0]` en `0xffffff80017293a5`, que resuelve a
   `IO80211VirtualInterface::start` → `IO80211SkywalkInterface::start`), registra las colas con el
   bus Skywalk y llama `IOSkywalkEthernetInterface::registerEthernetInterface`
   (`0xffffff800172973d`), que copia el `RegistrationInfo` (y publica `IOMACAddress`) y registra la
   interfaz en la familia Skywalk publicando `IOInterfaceNamePrefix`/`IOInterfaceUnit`.
3. Sobre el éxito, `Proxi::start` llama **`IOService::registerService(2)`** (síncrono) por el slot
   +0x5B0 en `0xffffff800172982a`. Dentro de ese `registerService` corre toda la parte BSD: el
   driver `IOSkywalkNetworkBSDClient` arranca, compone `"awdl0"` con `snprintf("%s%u",
   getBSDNamePrefix(), unit)`, publica `"BSD Name"` e `IOInterfaceName`, y crea el ifnet con
   `kern_nexus_controller_alloc_net_provider_instance` → `ifnet_allocate_extended` →
   `prepareBSDInterface` (vuelve a publicar `IOInterfaceName`) → `bpfattach` → `ifnet_attach`.
4. Al volver, `setVIRTUAL_IF_CREATE` escribe el nombre en el payload con
   `strlcpy(payload+16, getBSDName(), 0x10)` (`0xffffff8001678a2b`) y devuelve 0. La notificación
   `IOServiceMatched` de `wifip2pd` se dispara cuando `IOInterfaceName` aparece por primera vez
   sobre la entrada ya registrada de la interfaz (dentro de `IOSkywalkNetworkBSDClient::start`).
5. `SIOCGIFMEDIA` sobre `awdl0` lo termina respondiendo
   `IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`), que copia el estado de medios
   de la interfaz: `ifm_active` ← estado+48 (semilla `0x80` = `IFM_IEEE80211`), `ifm_current` ←
   estado+52 (semilla `8`), `ifm_status` ← estado+56 (semilla `0`). El filtro de `wifip2pd`
   (`(ifm_current & 0xE0) == 0x80`, verificado en su binario en `0x100043ccc`) exige que
   `ifm_current` lleve el tipo 802.11; cómo queda actualizado `ifm_current` entre el nacimiento y la
   lectura del daemon no quedó cerrado en esta pasada (`?`), pero para el kext la consecuencia
   práctica es única: contestar `SIOCGIFMEDIA` con palabras de medios de tipo `IFM_IEEE80211`.

De paso queda cerrado el `?` de la cadena por encima de `IO80211SkywalkInterface` que dejaba
[`AWDL_PROXIMITY_VTABLE_HIERARCHY.md`](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) §1: la jerarquía real es
`AppleBCMWLANProximityInterface` : `IO80211AWDLProtocol` : `IO80211VirtualInterface` :
`IO80211SkywalkInterface` : **`IOSkywalkEthernetInterface`** : **`IOSkywalkNetworkInterface`** (kext
`com.apple.iokit.IOSkywalkFamily`) — evidencia en §2.4.

## 2. La ruta del rol 6 en el controlador Broadcom

### 2.1 La puerta: `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`)

| Paso | Dirección | Evidencia |
|---|---|---|
| Lectura del rol: `mov eax, [payload+0x0C]` | `0xffffff800167891c` | disasm |
| Roles 8–10 → `createNANInterface` (tail-jmp `0xffffff8001678952` a `0xffffff8001678490`) | `0xffffff800167891f`–`0xffffff8001678952` | `lea ecx,[rax-8]; cmp ecx,3; jnb` |
| Rol 6 → camino Proximity (`cmp eax,6; jnz` al camino del rol 7 en `0xffffff8001678a94`) | `0xffffff8001678957` | disasm |
| Rol 7 → `AppleBCMWLANIO80211APSTAInterface::withOptions(this, payload+4, 7, payload+16)` | call en `0xffffff8001678c26` (a `0xffffff8001701c3a`) | disasm |
| Rol inválido → `0xE0000001` + log `"Invalid virtual interface role 0x%X name %s"` | `0xffffff8001678b79` | disasm |
| Ya existe interfaz Prox (slot `core->ivars+0x2C48`, es decir +11336) → resultado `0x11` (17) y salta igualmente al bloque de escritura del nombre | `0xffffff80016789e4`–`0xffffff80016789f9` (`r15d=0x11` en `0xffffff80016789eb`, `cmp qword [rax+0x2C48],0; jz create`) | disasm |
| `createProxInterface(this, payload)` | call en `0xffffff8001678bdd` | disasm |
| Éxito (0) → `jmp 0xffffff8001678e61` → `xor r15d,r15d; jmp 0xffffff80016789ff` (bloque compartido de escritura del nombre) | `0xffffff8001678be7`, `0xffffff8001678e61`–`0xffffff8001678e64` | disasm |
| Escritura del nombre (común a ambos caminos): cero en payload+0x18 y +0x10, `getBSDName()`, `strlcpy(payload+16, nombre, 0x10)` | ceros `0xffffff8001678a03`/`0xffffff8001678a09`; call `getBSDName` `0xffffff8001678a1b`; **call `_strlcpy` `0xffffff8001678a2b`** (`_strlcpy` = `0xffffff800048a270`) | disasm |
| Log `"create virtif <%s> role %u"` (línea 34181/0x8585) | `0xffffff8001678a76` | disasm |

Estructura del payload de 32 B consumida aquí: rol u32 @+0x0C, MAC 6 B @+0x04, nombre BSD
devuelto @+0x10..+0x1F (coincide con lo que construye `wifip2pd` en
[`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.4).

### 2.2 `AppleBCMWLANCore::createProxInterface` (`0xffffff800165dcbe`)

1. Si `core->ivars+0x2C48` ya tiene interfaz → log `"already created"` (`0xffffff800165dd37`) y
   devuelve 0 sin re-crear (`0xffffff800165dce8`).
2. `AppleBCMWLANProximityInterface::withOptions(this, payload+4, "awdl")` — call en
   `0xffffff800165dd6d`; preparación de argumentos: `add rsi,4` (rsi = payload+4, la **MAC que el
   daemon puso en el payload**) en `0xffffff800165dd5c` y `lea r15,[0xffffff8001797376]` (el string
   **`"awdl"`**) en `0xffffff800165dd60`, pasado como 3.er argumento (`rdx`, el parámetro `char*`
   nombre) en `0xffffff800165dd6a`.
3. Guarda el resultado en `core->ivars+0x2C48` (`0xffffff800165dd79`) y llama **`attach`** por el
   slot +0x670 de la vtable propia de Proxi con el core como proveedor
   (`call [rcx+0x670]` en `0xffffff800165dd92`; el slot +0x670 es `IOService::attach`).
4. Construye el `RegistrationInfo` en pila (0x130 bytes): `bzero` (`0xffffff800165dda7`) +
   `IOSkywalkEthernetInterface::initRegistrationInfo(proxi, RegInfo, 1, 0x130)`
   (call en `0xffffff800165ddc7` a `0xffffff8002b108e2`; valida magic `version=1, size=0x130` y
   rellena los defaults: +0x14 = 256, +0x24 = 34, +0x28 = 32, +0x30 = prefijo por
   `getBSDNamePrefix`, +0x38 = `getBSDUnitNumber`, +0x44 = 46, +0x48/+0x4C = MTU 1500) y después
   sobrescribe los campos propios:
   - `RegInfo+0x14 = 2` (`0xffffff800165ddcc`) — recuento de palabras de medios = 2.
   - MAC de 6 B en `RegInfo+0x108..0x10D` desde el slot +0xCF8 del interfaz (`call [rax+0xCF8]` en
     `0xffffff800165dde6`, getter de la MAC que fijó la init con el MAC del payload).
   - qword `0x0000000800000080` en `RegInfo+0x24` (`0xffffff800165de2d`–`0xffffff800165de37`):
     **u32 @+0x24 = `0x80` (`IFM_IEEE80211`), u32 @+0x28 = `8`** — la pareja de medios.
   - `RegInfo+0x0C = 3` (`0xffffff800165de3c`).
   - `RegInfo+0x30 = &"awdl"` (`0xffffff800165de82`) — prefijo BSD.
   - `RegInfo+0x38 = 0` (`0xffffff800165de87`) — **unit 0**.
   - `RegInfo+0x3A = 64` (u16, `0xffffff800165de8c`).
   - `RegInfo+0x40 = 4 o 6` según `checkPropertyTrue(core, "IOPMResetPowerStateOnWake")`
     (`0xffffff800165de6b`–`0xffffff800165de94`).
5. `AppleBCMWLANProximityInterface::start(proxi, core, RegInfo)` — call en `0xffffff800165dead`.
   Fallo de `withOptions` → `0xE00002BC` (`0xffffff800165dec0`).

### 2.3 `AppleBCMWLANProximityInterface::withOptions` e `init` — la cadena de inits

`withOptions` (`0xffffff80017159e6`, ya descrita en [`AWDL_KERNEL_ROLE_TABLE.md`](AWDL_KERNEL_ROLE_TABLE.md)
§3.2): aloca 320 B con `OSObject_typed_operator_new`, fija el vptr `0xffffff80017f5b78` y llama el
slot +0x11C8 con `(self, core, mac, 6, "awdl")` — el literal 6 en `0xffffff8001715a51`.

`AppleBCMWLANProximityInterface::init(Core*, ether_addr*, unsigned, char*)` (`0xffffff8001718db0`,
decompilado completo en esta pasada):

| Paso | Dirección | Nota |
|---|---|---|
| `init()` 0-args propia (slot +0x88) | `0xffffff8001718dde`–`0xffffff8001718de1` | `Proxi::init()` (`0xffffff8001718d72`) aloca los ivars propios (0x288 B, `this+312`) y llama la `init()` de la base por el slot importado `off_FFFFFF800246AF98` |
| `IO80211VirtualInterface::init(core, mac, 6, "awdl")` por el slot `off_FFFFFF80024572B8` | call en `0xffffff8001718e09` | = `0xffffff80023305f8` (cerrado en [`AWDL_KERNEL_ROLE_TABLE.md`](AWDL_KERNEL_ROLE_TABLE.md) §6) |
| `setInterfaceId(2)` si versión del core ≥ 5, si no `setInterfaceId(0xFFFFFFFF)` | `0xffffff8001718e27` (decompilado: ramas `< 5` / `else`) | |
| `ivars+0x2B0 = 6` (rol en ivars Broadcom) | `0xffffff8001718ec5` | ver también [`AWDL_PROXIMITY_VTABLE_HIERARCHY.md`](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) §3 |
| `AppleBCMWLANTaskmaster::withWorkQueue(core->workQueue, 0x10)` | `0xffffff8001718ea4` | guardado en ivars+56 |
| Defaults AWDL: lifetime 6 (`ivars+688`), guard time 20 (+700), maxNoMasterPeriod 4 (+696), election OUI en +567..+574, etc. | `0xffffff8001718ec5`–`0xffffff8001718f60` aprox. (bloque de stores del pseudocódigo) | valores literales en el pseudocódigo |
| Boot-args `awdl_log_flags_config`, `awdl_max_peers` (+908), `awdl_low_latency_default_mapping` (+912), `awdl_low_latency_tx_requeue` (+916) | `PE_parse_boot_argn` en el bloque final | |
| Tx Power Manager: `core->...->+5560` → ivars+1000; si falta, error y `freeResources` | final de la función | |

`IO80211VirtualInterface::init` (`0xffffff80023305f8`) hace, en orden: `IO80211SkywalkInterface::init(this, controller, mac)`
(`0xffffff80022f56e2`: llama la `init(OSDictionary*)` de `IOSkywalkEthernetInterface` por el slot
importado `off_FFFFFF8002B3F3D0` = `__ZTV26IOSkywalkEthernetInterface+0x258` (vptr+0x248), aloca los
ivars de 0xF0 B con `initIvars` (`0xffffff80022f520c`) y copia la MAC de 6 B del argumento a
`ivars+228`); después `setInterfaceRole(this, 6)` (`0xffffff800233064b`), `IOLockAlloc`,
lista BPF, `registerInterestedDriver(controller, this)` + `controller->open(this)`, boot-arg
`wlan.entitlement`, diccionario de procesos denegados (`OSDictionary::withCapacity(5)`) y
`IO80211FlowQueueDatabase::initialize`. La llamada con nombre-hint (`"awdl"`) no produce ninguna
escritura visible en esta init además de los ivars (`?` uso interno del hint).

### 2.4 `AppleBCMWLANProximityInterface::start(core, RegInfo)` (`0xffffff8001729376`) y la cadena hasta el registro

1. **Start de la base por vtable calificada**: `r14 = off_FFFFFF80017DE1C0` (el puntero importado a
   `__ZTV19IO80211AWDLProtocol`) y `call [r14+0x5D0]` en `0xffffff80017293a5` — el slot
   vptr+0x5C0 (start) de la vtable de la base, que contiene
   `IO80211VirtualInterface::start` (`0xffffff80023308aa`, simple forward a
   `IO80211SkywalkInterface::start` `0xffffff80022f5792`). Fracaso → log
   `"ERROR: awdl skywalk start fail"` (`0xffffff800172941b`).
2. `IO80211SkywalkInterface::start` (`0xffffff80022f5792`, decompilado completo): pánico si se
   llama dos veces; meta-cast del proveedor a `IO80211Controller`; **contador por rol**
   `intfRoleToNumberMap[6]++` (`0xffffff80022f58d4`; mapa en `0xffffff800248df70`) para el nombre de
   pipe `snprintf("Interface_%s_%u", "AirLink", contador)`; `CCPipe` `"com.apple.iokit.IO80211Family"`;
   `PMinit` + `joinPMtree` + `registerPowerDriver`; **peer manager** (para rol ≠ 2 se crea propio);
   `IO80211Glue::withOptions` **solo para rol 1** (Infraestructura) — la AWDL (rol 6) no recibe glue;
   `IO80211MacAddressAgent::withOptions` con la MAC de ivars+228; property condicional
   `IOPMResetPowerStateOnWake` si el controlador la tiene.
3. De vuelta en `Proxi::start`: resuelve el `AppleBCMWLANBusSkywalk` (`safeMetaCast` en
   `0xffffff8001729499`), construye el array de `IOSkywalkPacketQueue` (cola base +32, queues a
   ivars+0x3F0/+0x3F8/+0x400/+0x428/+0x430/+0x438, contados por `ivars+0x3B1` = 4 desde la init) y
   los registra con el bus (slot +0x140 de `r14`), guarda los queue-id en ivars+0x440..
4. Si el `RegInfo` vino (el camino de nacimiento), escribe `RegInfo+0x48 = 0x59400000594`
   (`0xffffff80017296ff`–`0xffffff8001729709`) y llama
   **`IOSkywalkEthernetInterface::registerEthernetInterface(this, RegInfo, queues, nqueues+2, txPool, rxPool, 0)`**
   — call en `0xffffff800172973d` a `0xffffff8002b10a84`.
5. `registerEthernetInterface` (`0xffffff8002b10a84`): `copyRegistrationInfo(this, RegInfo)`
   (`0xffffff8002b109f0`: valida magic, aloca 0x130 B, copia el RegInfo a los ivars del interfaz y
   publica **`IOMACAddress`** — ver §4) y luego
   `IOSkywalkNetworkInterface::registerNetworkInterface(RegInfo, logicalLink, pools, 0)`
   (`0xffffff8002b0a204`) — ver §3. Error → `deregisterEthernetInterface` y cleanup de colas
   (slots +0x148 de `r14` en `0xffffff8001729797`–`0xffffff800172981a`); log
   `"registerEthernetInterface returned 0x%x"` (`0xffffff8001729880`).
6. Con resultado 0: **`registerService(2)`** — `mov esi,2; call [rax+0x5B0]` en
   `0xffffff8001729825`–`0xffffff800172982a` (slot +0x5B0 = `IOService::registerService`, cuya
   implementación es `0xffffff8000a93250`; el valor 2 = `kIOServiceSynchronous`) — y a continuación
   `setRunningState(false)` por el slot +0x938 (`call [rax+0x938]` con esi=0 en
   `0xffffff8001729838`; = `IO80211SkywalkInterface::setRunningState`). Devuelve 0.
7. Existe además la entrada IOKit `AppleBCMWLANProximityInterface::start(IOService*)`
   (`0xffffff8001729348`, slot +0x5C0 de la vtable propia) que delega en la `start(core, RegInfo)`
   (`xref desde `0xffffff8001729371`); en el nacimiento por rol 6 el core llama la de dos argumentos
   directamente y nunca pasa por la IOKit (`?` si algún día se arranca por IOKit).

La clase base real: los slots calificados usados en la cadena apuntan a la vtable de
`IOSkywalkEthernetInterface` (`__ZTV26IOSkywalkEthernetInterface` = `0xffffff8002b3f178`): la init
de la base por `ZTV+0x258` (`0xffffff8002B3F3D0` → vptr+0x248 = `IOSkywalkEthernetInterface::init(OSDictionary*)`,
`0xffffff8002b10820`), y la `prepareBSDInterface` de la base por `ZTV+0x8F8` (`0xffffff8002b3fa70` →
vptr+0x8E8 = `IOSkywalkEthernetInterface::prepareBSDInterface`, `0xffffff8002b10e58`; xrefs de datos
desde `0xffffff8002b3fa70` y `0xffffff8002b3c658`). Junto con las firmas con
`IOSkywalkEthernetInterface::RegistrationInfo` en los `start` Broadcom (p. ej.
`0xffffff8001729376`), queda la cadena `... : IO80211SkywalkInterface` :
**`IOSkywalkEthernetInterface` : `IOSkywalkNetworkInterface`** del kext IOSkywalkFamily.

## 3. Nombre BSD `awdl0`, unit y registro del ifnet

### 3.1 De dónde salen prefijo y unit

| Pieza | Valor | Función que lo produce | Dirección |
|---|---|---|---|
| Prefijo | `"awdl"` (string en `0xffffff8001797376`) | `AppleBCMWLANProximityInterface::getBSDNamePrefix` | `0xffffff8001715a7c` (lo valida con `validateDispatchQueue` y devuelve el literal) |
| Prefijo alternativo del RegInfo | el mismo puntero `0xffffff8001797376` escrito en `RegInfo+0x30` | `AppleBCMWLANCore::createProxInterface` | `0xffffff800165de82` |
| Unit | `0` (u32 en `RegInfo+0x38`) | `AppleBCMWLANCore::createProxInterface` | `0xffffff800165de87` |
| Lectura del unit | `getBSDUnitNumber()` = `*(RegInfo copiado + 0x38)` | `IOSkywalkNetworkInterface::getBSDUnitNumber` | `0xffffff8002b0ba70` (dereferencia `ivars+0xC0 → RegInfo+56`; `-1` si no hay RegInfo) |
| Nombre compuesto 1 | `snprintf(buf16, "%s%u", getBSDNamePrefix(), unit)` → `"awdl0"` | `IOSkywalkNetworkBSDClient::start` | `0xffffff8002aff562` (la llamada a `getBSDNamePrefix` por el slot +0x9E8 en `0xffffff8002aff6xx` y el `snprintf` `_snprintf` = `0xffffff80008eaa10`) |
| Nombre compuesto 2 | `snprintf(ivars+1, 0x10, "%s%u", ifnet_name(ifp), ifnet_unit(ifp))` | `IO80211SkywalkInterface::getBSDName` | `0xffffff80022f5fca` (ifnet por el slot +0x8F8 `getBSDInterface`; `_ifnet_name` `0xffffff80006401d0`, `_ifnet_unit` `0xffffff8000640230`) |

El unit de `awdl0` es **un valor del driver** (0 escrito por `createProxInterface`), no el contador
global por rol: `intfRoleToNumberMap` (`0xffffff800248df70`) solo tiene dos lectores
(`IO80211SkywalkInterface::start` en `0xffffff80022f58d4` para el nombre de pipe
`Interface_AirLink_%u`, y `getInterfaceRoleCounter` `0xffffff80022f5eb6`) y no interviene en el
nombre BSD. Para el rol 7 (AP) el core escribe unit 1 en el mismo campo (`buf[56] = 1` en el camino
de `setVIRTUAL_IF_CREATE`), de modo que el esquema prefijo+unit es común a todas las interfaces
virtuales Broadcom.

### 3.2 Registro de la interfaz en la familia Skywalk (`registerNetworkInterface`)

`IOSkywalkNetworkInterface::registerNetworkInterface(RegInfo, logicalLink, pools, flags)`
(`0xffffff8002b0a204`, llamado desde `0xffffff8002b10a84` vía la variante con
`IOSkywalkPacketQueue**` que crea el `IOSkywalkLogicalLink` con un `IOSkywalkQueueSet`):

1. Valida el RegInfo (`version==1`, `size ≥ 0x108`, `RegInfo+0x14 ≤ 0x400`).
2. Copia 0x108 B del RegInfo a un objeto propio y lo colgó de `ivars+0xC0`.
3. Llama **`getBSDNamePrefix()`** (slot +0x9E8, `0xffffff8002b0a2b4`) y **`getBSDUnitNumber()`**
   (slot +0x9F0, `0xffffff8002b0a2cd`) sobre la propia interfaz.
4. Publica **`IOInterfaceNamePrefix`** (`setProperty` slot +0x270, call en `0xffffff8002b0a2e7`)
   y, si el unit ≥ 0, **`IOInterfaceUnit`** como `OSNumber` de 32 bits (slot +0x280, call en
   `0xffffff8002b0a310`).
5. Copia la pareja de medios al estado del interfaz: `estado+0x30 (48) ← RegInfo+0x24` (`0x80`) en
   `0xffffff8002b0a32a` y `estado+0x34 (52) ← RegInfo+0x28` (`8`) en `0xffffff8002b0a332`
   (el estado es el struct colgado de `ivars+0xC0`; sus campos +48/+52/+56 son los que luego lee
   `ioctl_gifmedia`, ver §6).
6. Marca el logical link por defecto (`setDefaultLogicalLink` `0xffffff8002b0a37e`,
   `setInterface` `0xffffff8002b0a38d`) y lo ordena en un `OSOrderedSet`.

### 3.3 Registro BSD: `IOSkywalkNetworkBSDClient::start` y el ifnet

`IOSkywalkNetworkBSDClient::start(IOService*)` (`0xffffff8002aff562`) es un driver IOKit cuyo
proveedor es la interfaz recién registrada (la arranca el matching que dispara el
`registerService(2)` del paso anterior):

1. Exige la property **`IOInterfaceUnit`** del proveedor (getProperty por el slot +744 y cast a
   `OSNumber`); sin ella aborta. De ahí saca el unit (`v12`).
2. Instala sus handlers (`ioctlHandler` `0xffffff8002affce6`, `setPowerStateHandler`,
   `reportInterfaceWakeFlags` en `this+33..35`), hace `openInterface`.
3. Compone `"awdl0"` con `snprintf("%s%u", getBSDNamePrefix(), unit)`; llama
   `setBSDName("awdl0")` (slot +0x900, guarda en `estado+65`);
   publica **`"BSD Name"`** (slot +0x270, call en `0xffffff8002aff84d`, solo si
   `getBSDUnitNumber() ≥ 0`) y **`IOInterfaceName`** (slot +0x270, call en `0xffffff8002aff86b`)
   sobre la interfaz. **La primera publicación de `IOInterfaceName` ocurre aquí.**
4. Llama `interface->initBSDInterfaceParameters(eparams, &sdl)` (slot +0x8E0 en
   `0xffffff8002aff7xx`): la override de `IO80211SkywalkInterface`
   (`0xffffff80022f635c`) y la base de `IOSkywalkEthernetInterface`
   (`0xffffff8002b10bca`) rellenan el `ifnet_init_eparams` de 0x158 B (version 2):
   `+16` = puntero a la MAC (RegInfo+0x108), `+24` = 6, `+296/+304` = broadcast, `+216..+248` =
   `ether_demux/ether_add_proto/ether_del_proto/ether_check_multi/ether_frameout`, `+44` y `+320`
   desde slots 308/309 de la vtable, `+48` = 6 (`IFT_ETHER`). El BSDClient además escribe
   eparams+32 = prefijo y eparams+40 = unit (en su frame `v39[3]`/`v39[4]`); el mapeo interno exacto
   de esos campos dentro de `ifnet_allocate_extended` no se siguió en xnu (`?`), pero el resultado
   observable es el ifnet `awdl0` con unit 0.
5. `registerNexusControllerProvider` y luego
   **`kern_nexus_controller_alloc_net_provider_instance`** (call con reintentos ×50 cada 100 ms;
   `0xffffff80009a4140`):
   - `ifnet_allocate_extended(eparams, &ifp)` — call en `0xffffff80009a420d` (kernel
     `0xffffff800063f3f0`).
   - `nx_create` del netif provider.
   - Callback de preparación `IOSkywalkNetworkBSDClient::prepareNexusCallback`
     (`0xffffff8002b00340`, pasado en los params) → gated →
     **`IOSkywalkNetworkBSDClient::gatedPrepareNexus`** (`0xffffff8002b0066c`): llama
     `interface->prepareBSDInterface(ifp)` (slot +0x8E8) y **`bpfattach(ifp, ...)`**
     (`_bpfattach` = `0xffffff80005be690`).
     - `IO80211SkywalkInterface::prepareBSDInterface` (`0xffffff80022f63b6`): llama la base
       Ethernet por la vtable importada (`off_FFFFFF8002450128` = `__ZTV26IOSkywalkEthernetInterface`,
       `call [rax+0x8F8]` en `0xffffff80022f63cd`), llama `getBSDName()` (`0xffffff80022f63d9`) y
       **vuelve a publicar `IOInterfaceName`** con `setPropertyHelper(this, "IOInterfaceName", nombre)`
       (helper `__Z17setPropertyHelperP9IOServicePKcS2_` = `0xffffff8002311f57`; **call en
       `0xffffff80022f63eb`**); para rol 2 fija MTU 1500 (`ifnet_set_mtu` en `0xffffff80022f640a`) y
       para todos `ifnet_set_eflags(0x200000, 0x200000)` (`0xffffff80022f6432`).
       - `IOSkywalkNetworkInterface::prepareBSDInterface` (`0xffffff8002b0aae2`, base): guarda el
         ifp en `estado+8`, fija MTU desde `RegInfo+0x4C` (1500) y marca `estado+64 = 1`.
   - **`ifnet_attach(ifp, sdl)`** — call en `0xffffff80009a42df` (kernel `0xffffff80005e8d20`); la
     `sockaddr_dl` sale de `initBSDInterfaceParameters` (embebida en la copia del RegInfo en +0x1E,
     con la MAC en +0x26..+0x2B).
6. Publica **`IOInterfaceType`** = 6 con valor de `eparams+48` (setProperty slot +0x280 en el tramo
   final de `BSDClient::start`), `IOSkywalkNexusUUID`, gestión PM (o `makeUsable`), `startIOReporting`
   y termina con **`registerService(0)` del propio BSD client** (slot +0x5B0 con esi=0 en
   `0xffffff8002aff8xx`, tramo final de la función).

En `IO80211VirtualInterface::prepareBSDInterface` (`0xffffff8002331ada`, override del slot +0x8E8
para las virtuales) además se comprueba el cliente BPF y se loguea
`"ERROR: cannot attach to bpf"` si no hay consumidor. La ruta genérica
`IO80211Controller::attachVirtualInterface` (`0xffffff80022960aa`: create por slot +0xDD8 → attach
por +0x670 → `registerService(0)` opcional por +0x5B0) **no interviene en el rol 6** — el core
Broadcom hace create/attach/start a mano; sirve de contraste (para el rol 7 la factoría
`IO80211Controller::createVirtualInterface` `0xffffff800229612e` solo acepta rol 7 y llama la
`IO80211VirtualInterface::init` con `7, "ap"`).

## 4. Properties del IORegistry publicadas en el nacimiento

Todas sobre la registry entry del **objeto interfaz** (`AppleBCMWLANProximityInterface`), salvo
indicación. Helper común de escritura: `setPropertyHelper(IOService*, char const*, char const*)` =
`0xffffff8002311f57`; los `setProperty` con valor no-string van por los slots +0x270 (objeto) y
+0x280 (entero) de la vtable.

| Property | Valor | Escrita por | Call site | Momento |
|---|---|---|---|---|
| `IO80211VirtualInterfaceRole` | `"AirLink"` (tabla de roles 6–10, clave en `0xffffff800246bdd8[5]` → `0xffffff800241dbc8`, valor `0xffffff800246bd88[5]` → `0xffffff800241dbc0`) | `IO80211SkywalkInterface::setInterfaceRole` (`0xffffff80022f60f0`), llamada desde `IO80211VirtualInterface::init` en `0xffffff800233064b` | `setPropertyHelper` en `0xffffff80022f6139` | Durante la `init`, **antes** del `registerService` |
| `IOMACAddress` | 6 B con la MAC del payload+4 (copiada al RegInfo+0x108 por el slot +0xCF8 en `0xffffff800165dde6`) | `IOSkywalkEthernetInterface::copyRegistrationInfo` (`0xffffff8002b109f0`) | `setProperty` por slot +0x288 en `0xffffff8002b10a66` | Dentro de `registerEthernetInterface`, antes del `registerService` |
| `IOInterfaceNamePrefix` | `"awdl"` | `IOSkywalkNetworkInterface::registerNetworkInterface` (`0xffffff8002b0a204`) | slot +0x270 en `0xffffff8002b0a2e7` | Ídem |
| `IOInterfaceUnit` | `0` (OSNumber 32 b, el unit del RegInfo+0x38) | ídem | slot +0x280 en `0xffffff8002b0a310` (solo si unit ≥ 0) | Ídem |
| `"BSD Name"` | `"awdl0"` | `IOSkywalkNetworkBSDClient::start` (`0xffffff8002aff562`) | slot +0x270 en `0xffffff8002aff84d` | Dentro del `registerService(2)` de la interfaz, **antes** de crear el ifnet |
| `IOInterfaceName` | `"awdl0"` (1.ª: compuesto por `snprintf("%s%u")`; 2.ª: leído del ifnet vivo) | 1.ª ídem (`0xffffff8002aff86b`); 2.ª `IO80211SkywalkInterface::prepareBSDInterface` (`0xffffff80022f63b6`) | 2.ª en `0xffffff80022f63eb` | Dentro del `registerService(2)`; la 2.ª durante la preparación del nexus, antes del `ifnet_attach` |
| `IOInterfaceType` | `6` (`IFT_ETHER`, tomado de `eparams+48` que rellena `initBSDInterfaceParameters`) | `IOSkywalkNetworkBSDClient::start` | setProperty slot +0x280 en el tramo final (tras el `kern_nexus_controller_alloc_net_provider_instance`) | Al final del arranque del BSD client |
| `IOPMResetPowerStateOnWake` | presente solo si el controlador la tiene (`checkPropertyTrue`) | `IO80211SkywalkInterface::start` (`0xffffff80022f5792`) | `setPropertyHelper` en el bloque de PM | Durante la `start` de la base |
| `IOSkywalkNexusUUID` | UUID del nexus (parseado con `uuid_unparse_upper`) | `IOSkywalkNetworkBSDClient::start` | setProperty por slot +0x270 propio | Final del arranque del BSD client |

Sobre el **padre** (la entry de `AppleBCMWLANCore`): ninguna función de la cadena
setVIRTUAL_IF_CREATE → createProxInterface → init/attach/start → BSD client escribe properties en
él; la interfaz se cuelga como hija con `IOService::attach` (slot +0x670 en `0xffffff800165dd92`) y
el padre conserva las properties que ya tenía publicadas en su propio arranque (`?` descarte
exhaustivo no hecho, pero en los ~30 decompilados de la cadena no hay ningún `setProperty` con
destino distinto del propio interfaz). Tampoco existe ninguna escritura de **`IOProviderClass`** en
la cadena: en el kernel ese literal solo aparece en caminos genéricos (`OSKext::uniquePersonalityProperties`
`0xffffff8000a34cc0`, `IOService::initialize` `0xffffff8000aa45d0`, IONetworkingFamily, etc.), de
modo que el matching del `IOSkywalkNetworkBSDClient` con su proveedor viene de la personality del
kext, no de una property fijada en tiempo de nacimiento (`?`). El diccionario de `wifip2pd`
(`IOPropertyExistsMatch` sobre `IOInterfaceName`, ver
[`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.1) no necesita nada más.

## 5. Orden exacto `registerService` ↔ attach BSD

Secuencia completa del rol 6, con el punto de la notificación:

1. `setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`) → `createProxInterface` (`0xffffff8001678bdd`).
2. `withOptions` → alloc + `init` (slots +0x11C8 → +0x88 → `IO80211VirtualInterface::init` →
   `IO80211SkywalkInterface::init` → `IOSkywalkEthernetInterface::init`) — aquí ya se publica
   `IO80211VirtualInterfaceRole = AirLink` (la entry **todavía no está registrada**, así que no
   dispara matching).
3. `attach` (slot +0x670, `0xffffff800165dd92`) — la entry se cuelga del core, sigue sin estar
   registrada.
4. `Proxi::start` (`0xffffff800165dead`) → base start (peer manager, PM, MAC agent) → registro de
   colas → `registerEthernetInterface` (`0xffffff800172973d`): publica `IOMACAddress`,
   `IOInterfaceNamePrefix`, `IOInterfaceUnit` y prepara el estado de medios (§6) — todavía sin
   registro, sin notificaciones.
5. **`registerService(2)`** (síncrono) en `0xffffff800172982a`. Dentro, sincrónicamente:
   1. IOKit casa el driver `IOSkywalkNetworkBSDClient` contra la nueva entry → su `start`
      (`0xffffff8002aff562`).
   2. Publica `"BSD Name"` (`0xffffff8002aff84d`) y **`IOInterfaceName`** (`0xffffff8002aff86b`) —
      **este es el momento en que la entry registrada gana `IOInterfaceName` y el matching
      `IOPropertyExistsMatch` de `wifip2pd` dispara `IOServiceMatched`** (la entrega al puerto de
      notificación del daemon es asíncrona).
   3. `initBSDInterfaceParameters` → `kern_nexus_controller_alloc_net_provider_instance`
      (`0xffffff80009a4140`): `ifnet_allocate_extended` (`0xffffff80009a420d`) →
      `prepareNexusCallback` → `gatedPrepareNexus` → `prepareBSDInterface` (2.ª escritura de
      `IOInterfaceName` en `0xffffff80022f63eb`, ya con el ifnet real) → `bpfattach` →
      **`ifnet_attach`** (`0xffffff80009a42df`).
   4. `IOInterfaceType`, `IOSkywalkNexusUUID`, PM, `registerService(0)` del BSD client.
6. De vuelta en `Proxi::start`: `setRunningState(false)` (`0xffffff8001729838`) y return 0.
7. `setVIRTUAL_IF_CREATE`: `getBSDName` (`0xffffff8001678a1b` — ya devuelto de la caché
   `ivars+1` rellenada por `getBSDName` o compuesto del ifnet) → `strlcpy(payload+16, ..., 0x10)`
   (`0xffffff8001678a2b`) → return 0.

Consecuencia: cuando `RawSet` tipo 94 devuelve 0 a `wifip2pd`, el ifnet `awdl0` ya existe, tiene
índice BSD y la notificación `IOServiceMatched` está en camino (o entregada); la espera de 30 s del
daemon se libera sin ninguna petición apple80211 adicional (coherente con
[`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1). Si la interfaz ya existía, el
resultado es `0x11` (17) pero el nombre se escribe igualmente en payload+16 (mismo bloque
compartido, `0xffffff8001678e61 → 0xffffff80016789ff`).

## 6. `SIOCGIFMEDIA` sobre `awdl0`

### 6.1 Lo que exige `wifip2pd`

`SIOCGIFMEDIA` = `_IOWR('i', 56, struct ifmediareq)` con `sizeof(struct ifmediareq)` = 44
(`#pragma pack(4)`: 16 de nombre + 5 ints + puntero) = **`0xC02C6938`**; la variante legacy de 40 B
(OSIOCGIFMEDIA) es **`0xC0286938`**. El filtro del daemon (descubridor
`sub_100043720` de `wifip2pd.x86_64`): tras el ioctl comprueba el dword del buffer a offset +0x10 —
`movl -0x140(%rbp),%eax; andl $0xE0,%eax; cmpl $0x80,%eax` en **`0x100043ccc`** con el buffer del
`ifmediareq` en `-0x150(%rbp)` — es decir **`ifm_current`** (offset 16 del struct):
`(ifm_current & 0xE0) == 0x80` = tipo `IFM_IEEE80211` (`IFM_NMASK 0xE0`, `IFM_IEEE80211 0x80`).

### 6.2 Ruta kernel completa

```
ioctl(SIOCGIFMEDIA) en el socket BSD
  └─ xnu _ifioctl                                  0xffffff80005d62d0
       (comparaciones del cmd en 0xffffff80005d67be / 0xffffff80005d6bde)
       └─ sub_FFFFFF80005FA970  (SIOCGIFMEDIA/OSIOCGIFMEDIA: cases 0x5fab00-0x5fab08)
            arma un ifmediareq kernel y llama
            _ifnet_ioctl(ifp, 0, 0xC02C6938, &ifmr)   call en 0xffffff80005faf4e → 0xffffff80005e0c30
                 └─ if_ioctl del ifnet = IOSkywalkNetworkBSDClient::if_ioctl   0xffffff8002b002e6
                      (_softc[18] = gate, __softc[33] = acción; call [vtable+456])
                      └─ IOSkywalkNetworkBSDClient::ioctlHandler (gated)        0xffffff8002affce6
                           └─ interface->processBSDCommand(ifp, cmd, data)  (slot +0x910, call en 0xffffff8002aff9xx)
                                └─ IOSkywalkNetworkInterface::processBSDCommand  0xffffff8002b0ad4a
                                     (compara 0xC02C6938 en 0xffffff8002b0adba y 0xC0286938 en 0xffffff8002b0adca)
                                     └─ IOSkywalkNetworkInterface::ioctl_gifmedia    0xffffff8002b0ae78
```

`ioctl_gifmedia` (`0xffffff8002b0ae78`, decompilado completo):

- Lista de medios: cuenta = `RegInfo copiado+0x14` (2 para AWDL; el campo lo puso
  `createProxInterface` en `0xffffff800165ddcc`); las palabras las produce
  `getSupportedMediaArray` (slot +0x948; override `IO80211SkywalkInterface::getSupportedMediaArray`
  `0xffffff80022f8e5e`, que itera la tabla de medios `IONetworkMedium` del controlador) y se copian
  al usuario con `copyout`.
- Copia el estado bajo el IOLock de `estado+16` (los campos escritos en §3.2 paso 5):
  **`ifm_active` (offset 28) ← `estado+48`**, **`ifm_current` (offset 16) ← `estado+52`**,
  **`ifm_status` (offset 24) ← `estado+56`**.
- La escritura de `ifm_current` por usuarios (`SIOCSIFMEDIA` = `0xC0206937`, despachada en
  `0xffffff8002b0ad8c` a `ioctl_sifmedia` `0xffffff8002b0affa`) escribe `estado+52`.
- El slot +0x940 `handleChosenMedia` (`IO80211SkywalkInterface::handleChosenMedia`
  `0xffffff80022f8f5c`, **no overrideado por Broadcom** — vptr+0x940 de Proxi = `0xffffff80022f8f5c`)
  traduce una palabra de medios a `selectMedium` del controlador (busca el `IONetworkMedium` en la
  tabla del controlador y llama el slot +0x8F0 de este); sus únicas referencias son slots de
  vtable, de modo que el llamador es un vcall no identificado en esta pasada (`?`).

### 6.3 El hueco abierto y la consecuencia para el kext

Con las semillas estáticas, `awdl0` nace con `ifm_active = 0x80` (tipo 802.11 ✓) pero
`ifm_current = 8` y `ifm_status = 0`; `8 & 0xE0 = 0`, así que **el filtro de `wifip2pd` no pasaría
con las semillas tal cual**. En un sistema real el check pasa, de donde el `ifm_current` de `awdl0`
debe haber sido actualizado a una palabra con tipo `IFM_IEEE80211` (p. ej. `0x80` =
`IFM_IEEE80211|IFM_AUTO`, ya que `IFM_AUTO = 0` según `net/if_media.h`) antes de la lectura del
daemon; el candidato natural es la vía
`handleChosenMedia` → `selectMedium` del controlador Broadcom durante el arranque del enlace
(`bringupLink`), o un `SIOCSIFMEDIA` del propio stack, pero la instrucción concreta que reescribe
`estado+52` durante el nacimiento no quedó identificada (`?`). Para `RTL8814AirLayer` la
consecuencia no cambia: la if_ioctl propia de `awdl0` debe responder `SIOCGIFMEDIA`
(`0xC02C6938`, y la variante `0xC0286938`) con `ifm_current` e `ifm_active` de tipo
`IFM_IEEE80211` (`& 0xE0 == 0x80`; p. ej. `0x80`), `ifm_status` con al menos `IFM_AVALID` cuando el
enlace esté arriba y una lista de palabras 802.11 (el daemon solo mira `ifm_current` para el gate; los
campos restantes los usa el resto de herramientas).

## 7. Resumen operativo para `RTL8814AirLayer`

1. `setVIRTUAL_IF_CREATE` (slot 556/+0x1160) con rol 6: crear el objeto interfaz (equivalente
   Proxi), hacer `attach` al controlador, y en el `start` propio: `IO80211VirtualInterfaceRole =
   "AirLink"` (o directamente publicarla si no se usa la init de la familia), `IOMACAddress` con la
   MAC del payload+4, `IOInterfaceNamePrefix = "awdl"`, `IOInterfaceUnit = 0`.
2. Crear el ifnet con prefijo `awdl`, unit 0, `IFT_ETHER`, MAC del payload, y hacer
   `ifnet_attach`; componer y cachear el nombre `awdl0`.
3. Publicar `IOInterfaceName = "awdl0"` y **después** llamar `registerService` (síncrono si se
   quiere que el attach BSD quede completo antes de devolver el control al handler, como hace
   Broadcom con `registerService(2)` en `0xffffff800172982a`; el orden Broadcom es: properties →
   registerService → [matching → BSD name + ifnet attach dentro] — en un kext propio sin el driver
   BSD client, el orden mínimo correcto es: ifnet attach + `IOInterfaceName` + `IOInterfaceUnit` +
   rol **antes** de `registerService`, que es lo que dispara la notificación).
4. Escribir el nombre en payload+16 con `strlcpy(..., 16)` y devolver 0 (o 17 si ya existía, con el
   nombre escrito igualmente).
5. Contestar `SIOCGIFMEDIA`/`OSIOCGIFMEDIA` con tipo `IFM_IEEE80211` en `ifm_current` (§6.3).

## 8. Método y límites

- Descompilación headless (Hex-Rays) de ~50 funciones y desensamblado de los tramos con los call
  sites exactos (scripts y salidas en `Build/ida_awdl/task_awdl_birth/{bcm,fam}/`: `bcm_birth.py` →
  `out.txt`; `fam_birth.py` → `out.txt`; `fam_birth2.py` → `out2.txt`; `fam_birth3.py` → `out3.txt`;
  `fam_birth4.py` → `out4.txt`). Direcciones de funciones cruzadas con
  `Docs/re/io80211_25.6.0/syms.txt`, `syms_bcm.txt` y `syms_com.apple.iokit.IOSkywalkFamily.txt`.
- Constantes de ioctl verificadas por búsqueda de inmediatos en el kernel (`SIOCGIFMEDIA`
  `0xC02C6938`: 22 hits, incluidos `_ifioctl`, `IOSkywalkNetworkInterface::processBSDCommand` e
  `ioctl_gifmedia`; `SIOCSIFMEDIA` `0xC0206937`: hit en `0xffffff8002b0ad8c`) y por el filtro real
  de `wifip2pd` desensamblado con `objdump` (`0x100043ccc`).
- `?` explícitos: el actualizador de `ifm_current` (`estado+52`) entre el nacimiento y la lectura de
  `wifip2pd` (§6.3); el mapeo interno xnu de `ifnet_init_eparams` (campos prefijo/unit observados en
  `eparams+32/+40` escritos por `IOSkywalkNetworkBSDClient::start`, no seguidos dentro de
  `ifnet_allocate_extended`); el llamador del vcall a `handleChosenMedia` (+0x940); el uso del
  parámetro nombre (`"awdl"`) en la cadena de inits; el descarte exhaustivo de escrituras de
  properties sobre el padre; el `IOProviderClass` (no escrito en runtime en la cadena; personality
  del kext `?`); la dirección exacta del `setProperty("IOInterfaceType")` y del `registerService(0)`
  final del BSD client (función decompilada, call sites no disassembledos individualmente); la
  llamada `slot +0x910` dentro de `ioctlHandler` citada del decompilado (call site exacto no
  disassembledo).
- Las direcciones de los slots de vtable (p. ej. +0x5B0 `registerService`, +0x670 `attach`,
  +0x8E8 `prepareBSDInterface`, +0x8F8 `getBSDInterface`, +0x938 `setRunningState`, +0x940
  `handleChosenMedia`, +0x948 `getSupportedMediaArray`, +0xCF8 getMACAddress) salen de los
  desensamblados (`call qword ptr [rax+off]`) y del dump `vt_IO80211SkywalkInterface.txt` del
  proyecto; el slot +0x5B0 contiene la dirección de `IOService::registerService` (`0xffffff8000a93250`)
  en todas las vtables de la familia (xrefs de datos en `fam/out.txt` §registerService).
