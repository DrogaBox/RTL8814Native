# RegInfo +0x10/+0x40, `ifm_current`, el gate del 94 y el flag de `handleIoctlGated`

Cierre de los puntos 7, 8 y 9 de `AWDL_STAGE1_IMPL_SPEC.md` §15 y del gate del manejador del 94,
leído sobre el kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). Cada fila con VA es una instrucción
vista en el desensamblado de esta sesión. Los `?` indican la búsqueda concreta que se ejecutó y no
encontró nada.

## 1. El flag que devuelve 6: `*(iop+128h)+52h` bit 0

El byte es el indicador de terminación de un bloque de estado privado de la interfaz, guardado en
`+0x128`. No es un objeto de clase: `IO80211VirtualInterface::init` lo reserva con
`IOMallocTypeImpl(kalloc_type_view_341)` (`0xffffff8002330630`), una reserva tipada que vuelve a cero,
y guarda el puntero en `0xffffff8002330635`. Lo usan además las rutinas de bpf.

| VA | Función | Instrucción | Efecto |
|---|---|---|---|
| `0xffffff8002330635` | `IO80211VirtualInterface::init(IO80211Controller *, ether_addr *, unsigned, char *)` | `mov [rbx+128h], rax` | guarda el puntero del objeto |
| `0xffffff8002330c5f` → `0xffffff8002330c66` | `IO80211VirtualInterface::terminate(unsigned)` | `mov rax, [rdi+128h]` → `mov byte ptr [rax+52h], 1` | **pone** el bit |
| `0xffffff8002330d26` → `0xffffff8002330d2d` | `IO80211VirtualInterface::stop(IOService *)` | `mov rax, [r14+128h]` → `mov byte ptr [rax+52h], 1` | **pone** el bit |
| `0xffffff8002330e46` → `0xffffff8002330e4f` | `IO80211VirtualInterface::bpfTap(unsigned, unsigned)` | `mov rcx, [rdi+128h]` → `test byte ptr [rcx+52h], 1` | consulta |
| `0xffffff8002331d52` → `0xffffff8002331d5e` | `IO80211VirtualInterface::handleIoctlGated(...)` | `mov rcx, [r15+128h]` → `test byte ptr [rcx+52h], 1` | **el 6 de todo ioctl apple80211** |
| `0xffffff8002331ea2` → `0xffffff8002331ea9` | `IO80211VirtualInterface::terminating()` | `mov rax, [rdi+128h]` → `mov al, [rax+52h]` | lee el bit como valor de retorno |

Consecuencia: el 6 no es un rechazo de capacidad ni de rol; significa que la interfaz ya entró en
terminación. Se pone en `terminate` y en `stop`, y no hay ningún `mov byte ptr [..+52h], 0` en el
rango `0xffffff8002200000`-`0xffffff8002400000` que lo borre: el bit empieza a cero por la reserva
de `init` y sólo pasa a 1 en `terminate` o `stop`.

Para la etapa 1: no llamar `stop` ni `terminate` sobre `awdl0` antes de que wifip2pd termine su
secuencia; un 6 persistente en los ioctls apunta a ese bit, no al despacho.

## 2. `RegInfo+0x40`

`AWDL_INTERFACE_BIRTH.md` §2.2 documenta el escritor del camino de rol 6: `AppleBCMWLANProximityInterface`
escribe `RegInfo+0x40 = 4 o 6` según `checkPropertyTrue(core, "IOPMResetPowerStateOnWake")`; el camino
STA escribe 0. Ningún lector de `+0x40` aparece en los cuerpos decompilados de:

| VA | Función |
|---|---|
| `0xffffff8002b10a84` | `IOSkywalkEthernetInterface::registerEthernetInterface(RegistrationInfo const *, PacketQueue **, unsigned, Pool *, Pool *, unsigned)` |
| `0xffffff8002b10b46` | `IOSkywalkEthernetInterface::registerEthernetInterface(RegistrationInfo const *, LogicalLink *, Pool *, Pool *, unsigned)` |
| `0xffffff8002b0a0d6` | `IOSkywalkNetworkInterface::registerNetworkInterface(RegistrationInfo const *, PacketQueue **, unsigned, Pool *, Pool *, unsigned)` |
| `0xffffff8002b0a204` | `IOSkywalkNetworkInterface::registerNetworkInterface(RegistrationInfo const *, LogicalLink *, Pool *, Pool *, unsigned)` |

`initRegistrationInfo` (`0xffffff8002b108e2`) hace `bzero(a2+8, 0x128)` y escribe: qword en `+0`
con `0x00000001300000001`, `+0x8` con el resultado de un slot (2 por defecto), `+0xC` con otro
slot, `+0x14 = 256`, `+0x24 = 34`, `+0x28` con el resultado de un slot (32 por defecto), `+0x30`
con `"en"` por defecto, `+0x38` con la unit, `+0x44 = 46` y `+0x48`/`+0x4C = 1500`. En ese cuerpo
**no se escribe `+0x10` ni `+0x40`**: el `bzero` los deja a cero.

`copyRegistrationInfo` (`0xffffff8002b109f0`) aloca 0x130 bytes con `kalloc_type_view_142`
(`0xffffff8002b45fc0`), guarda el bloque en `*(this+0x118)`, copia los 0x130 con `memmove` y después
fija `IOMACAddress` desde `+0x108` por el índice 81 de la vtable. Los dos campos no se copian a
ivars propios: cualquier lector tendría que ir por `*(this+0x118)+off`.

Barrido de lectores: en todas las funciones `IOSkywalk*`/`IO80211*` de
`0xffffff8002200000`-`0xffffff8002400000` y `0xffffff8002a00000`-`0xffffff8002c00000`, una carga de
`[reg+118h]` seguida en 10 instrucciones de una lectura de `[reg+10h]` o `[reg+40h]` sólo aparece en
`IOSkywalkEthernetInterface::free` (`0xffffff8002b10896`) y en
`IOSkywalkEthernetInterface::ioctl_multicast` (`0xffffff8002b1127c`, `0xffffff8002b112a7`,
`0xffffff8002b112c6`, `0xffffff8002b1131a`, `0xffffff8002b1134b`). En esos dos métodos el `+0x10`
leído es el lock del objeto, no el campo del `RegistrationInfo`.

`?` Ningún lector de `RegInfo+0x10` ni de `RegInfo+0x40` ha aparecido; búsqueda ejecutada: el barrido
descrito, con ventana de 10 instrucciones y offsets `+8`, `+0xC`, `+0x10` y `+0x40`.

## 3. `RegInfo+0x10`

El valor de partida es 0: `initRegistrationInfo` lo deja así por el `bzero` y el camino de rol 6
documentado en `AWDL_INTERFACE_BIRTH.md` §2.2 no lo toca. El camino STA escribe 1. Tampoco aparece
lectura de `+0x10` en las cuatro funciones de §2 ni en el barrido de §2. `?` El significado del campo
sigue sin identificar; búsqueda ejecutada: `initRegistrationInfo`, `copyRegistrationInfo`, las cuatro
funciones de registro y el barrido de lectores de `*(this+0x118)`.

## 4. `ifm_current`

`IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`) no lee un campo `ifm_current`
directo: toma el objeto de medios en `this+0xC0` (`mov rax, [this+0xC0]` en el cuerpo), lee el
recuento en `+0x14`, aloca la tabla y deja que el slot `+0x948` la rellene
(`0xffffff8002b0ae78`, llamada al slot `+0x948`).

Del cuerpo decompilado se fijan además dos detalles: el descriptor de medios está en `*(this+0xC0)`
y su recuento en `+0x14`; la tabla se rellena por el slot `+0x948`; y la copia al `ifmediareq` del
usuario se hace bajo el lock de `*(this+0xC0)+0x10`.

El slot `+0x948` es `getSupportedMediaArray(unsigned *, unsigned *)`. En
`__ZTV23IO80211SkywalkInterface` (`0xffffff800246af00`) y `__ZTV23IO80211VirtualInterface`
(`0xffffff8002472b78`) apunta a `IO80211SkywalkInterface::getSupportedMediaArray` (`0xffffff80022f8e5e`);
en `__ZTV25IOSkywalkNetworkInterface` (`0xffffff8002b3d2b8`) a la versión base
`IOSkywalkNetworkInterface::getSupportedMediaArray` (`0xffffff8002b0b5f2`). El cuerpo de `0xffffff80022f8e5e` pide al core
(`*(this+0x120)+0x30`) la colección por su slot `+0x890`, la recorre con `OSCollectionIterator` y
escribe en la tabla el valor que devuelve el slot `+0x120` de cada objeto de la colección.

Los medios no salen del `RegistrationInfo` ni de un ivar por interfaz: salen de la colección de
medios del core. `?` Qué entrada de esa colección acaba en `ifm_current` (`ifmediareq+0x10`) y qué
escritores tiene la colección después del alta no se ha cerrado; búsqueda ejecutada: decompilación de
`ioctl_gifmedia` y de `getSupportedMediaArray` (`0xffffff80022f8e5e`).

## 5. Gate del 94

| VA | Hecho |
|---|---|
| `0xffffff8002331d20` | `handleIoctlGated` se invoca desde `handleIoctl` por el slot `+0x850` del controlador. |
| `0xffffff80024619a8` (celda `+0x850` de `__ZTV17IO80211Controller`, `0xffffff8002461148`) | el slot resuelve a `IONetworkController::executeCommand` (`0xffffff80026f51e0`): la acción se ejecuta en el work loop del controlador. |
| `0xffffff80022f78ad` | En la ruta `en2`, el despacho ocurre dentro de `performGatedCommandIOCTL`. |
| `0xffffff80022f6e26` | En la ruta del user client, `processBSDCommandGated` es el thunk de `performGatedCommandIOUC` (`0xffffff80022f6e30`), también gated. |

Consecuencia: en la ruta de `awdl0` el despacho corre dentro de `executeCommand`, con el work loop
del controlador retenido. En la ruta de `en2` (la que usa el 94) el despacho ocurre dentro de
`performGatedCommandIOCTL`, y no se ha leído qué gate tiene tomado en ese momento (`?`). Si es el
mismo work loop: Un `registerService(kIOServiceSynchronous)` dentro del manejador espera a que el
work loop procese el alta, y ese work loop es el que está ejecutando el manejador: el
interbloqueo es posible. `kIOServiceAsynchronous` evita la espera. `?` El punto exacto en el que
`executeCommand` toma y suelta el gate no se ha leído instrucción por instrucción, y el cuerpo de
`performGatedCommandIOCTL` (`0xffffff80022f779a`) no contiene ninguna llamada a `executeCommand` ni
referencia a un gate: en `en2` el gating viene del llamador, el `IOCommandGate` del cliente BSD de
IOSkywalkFamily (§6).

## 6. `if_ioctl` de IOSkywalkFamily

`IOSkywalkNetworkBSDClient::if_ioctl` (`0xffffff8002b002e6`) obtiene el cliente con `ifnet_softc`
(`0xffffff8002b002f9`) y salta al slot `+0x1C8` del objeto en `client+0x90`
(`0xffffff8002b0032c`). Ese campo se escribe en `IOSkywalkBSDClient::start`, `mov [rbx+90h], rax`
(`0xffffff8002afd448`), y lo consumen `ioctlHandler` (`0xffffff8002affd03`),
`runGatedAction` (`0xffffff8002b00658`) y `enableInterface` (`0xffffff8002b007a7`).

El objeto es un `IOCommandGate`: `IOSkywalkBSDClient::start` lo crea con
`IOCommandGate::commandGate(this, 0)` (`0xffffff8002afd443`) justo antes del almacén, sobre el work loop
que devuelve el slot `+0x688` del cliente (`0xffffff8002afd42d`). En `__ZTV13IOCommandGate` el slot
`+0x1C8` es `IOCommandGate::runAction`. Por tanto todo ioctl BSD, también el apple80211 sobre `en2`,
ejecuta `ioctlHandler` y `processBSDCommand` dentro de `runAction`, con ese gate tomado.

## 7. Búsquedas ejecutadas y pendientes

| Punto | Búsqueda | Resultado |
|---|---|---|
| flag `+0x52` | byte ops con carga previa de `[x+128h]` en funciones `IO80211*` | 5 sitios, tabla de §1 |
| lectores de `RegInfo` | pseudocódigo de las 4 funciones de registro | sin lecturas de `+0x10`/`+0x40` |
| medio actual | `getSupportedMediaArray` de `+0x948` | tabla desde la colección de medios del core; entrada actual sin fijar (`?`) |
| `RegInfo+0x10`/`+0x40` | barrido de lectores sobre `*(this+0x118)` | sólo `free` (`0xffffff8002b10896`) y `ioctl_multicast` (`0xffffff8002b1127c` …), y leen un lock; sin lectores (`?`) |
| slot `+0x948` | vtables de Skywalk/Virtual/IOSkywalkNetworkInterface | `getSupportedMediaArray` (`0xffffff80022f8e5e`, `0xffffff8002b0b5f2`) |
| bloque de `+0x128` | instrucciones previas a `0xffffff8002330635` | `IOMallocTypeImpl(kalloc_type_view_341)` en `0xffffff8002330630`: struct privado, no clase |
| objeto de `client+0x90` | almacén en `IOSkywalkBSDClient::start` + vtable `+0x1C8` | `IOCommandGate` (`0xffffff8002afd443`), slot `+0x1C8` = `runAction` |
