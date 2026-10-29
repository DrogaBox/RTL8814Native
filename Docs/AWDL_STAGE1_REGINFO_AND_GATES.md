# RegInfo +0x10/+0x40, `ifm_current`, el gate del 94 y el flag de `handleIoctlGated`

Cierre de los puntos 7, 8 y 9 de `AWDL_STAGE1_IMPL_SPEC.md` §15 y del gate del manejador del 94,
leído sobre el kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). Cada fila con VA es una instrucción
vista en el desensamblado de esta sesión. Los `?` indican la búsqueda concreta que se ejecutó y no
encontró nada.

## 1. El flag que devuelve 6: `*(iop+128h)+52h` bit 0

El byte es el indicador de terminación del objeto que la interfaz guarda en `+0x128`. Se escribe en
`IO80211VirtualInterface::init` y lo usan las rutinas de bpf, lo que identifica el objeto como el
conjunto de recursos bpf de la interfaz. No se ha localizado el nombre de su clase (`?`; búsqueda:
referencias al almacén `[x+128h]` en el rango `0xffffff800232f000`-`0xffffff8002334000`).

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
rango `0xffffff8002200000`-`0xffffff8002400000` que lo borre; el borrado debe ocurrir al crear el
objeto, antes de `init` (`?`; búsqueda: instrucciones que escriben un byte en `[reg+52h]` en todas
las funciones con `IO80211` en el nombre).

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

`?` Los cuerpos son envoltorios: reenvían el puntero a `initRegistrationInfo` y a
`copyRegistrationInfo`, y esas dos funciones no se han recorrido campo a campo. Búsqueda ejecutada:
grep de `+ 16` / `+ 64` sobre el pseudocódigo de las cuatro.

## 3. `RegInfo+0x10`

No hay escritura de `+0x10` en el camino de rol 6 documentado en `AWDL_INTERFACE_BIRTH.md` §2.2, y el
camino STA escribe 1. Tampoco aparece lectura de `+0x10` en las cuatro funciones de §2. `?` El
significado del campo sigue sin identificar; búsqueda ejecutada: la misma de §2.

## 4. `ifm_current`

`IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`) no lee un campo `ifm_current`
directo: toma el objeto de medios en `this+0xC0` (`mov rax, [this+0xC0]` en el cuerpo), lee el
recuento en `+0x14`, aloca la tabla y deja que el slot `+0x948` la rellene
(`0xffffff8002b0ae78`, llamada al slot `+0x948`).

`?` Qué ivar concreto devuelve como medio actual y qué escritores tiene después del alta no se ha
cerrado. Búsqueda ejecutada: decompilación completa de `ioctl_gifmedia`; no se han listado los
escritores del array de medios.

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
`executeCommand` toma y suelta el gate no se ha leído instrucción por instrucción.

## 6. `if_ioctl` de IOSkywalkFamily

`IOSkywalkNetworkBSDClient::if_ioctl` (`0xffffff8002b002e6`) obtiene el cliente con `ifnet_softc`
(`0xffffff8002b002f9`) y salta al slot `+0x1C8` del objeto en `client+0x90`
(`0xffffff8002b0032c`). `?` La clase de ese objeto no se ha resuelto: la búsqueda sobre todas las
`__ZTV*BSDClient` da `_RESERVEDIORegistryEntry16` en `+0x1C8` para
`__ZTV25IOSkywalkNetworkBSDClient` (`0xffffff8002b393e8`), así que el objeto no es el cliente BSD.

## 7. Búsquedas ejecutadas y pendientes

| Punto | Búsqueda | Resultado |
|---|---|---|
| flag `+0x52` | byte ops con carga previa de `[x+128h]` en funciones `IO80211*` | 5 sitios, tabla de §1 |
| clase de `+0x128` | referencias a `[x+128h]` en `0xffffff800232f000`-`0xffffff8002334000` | sólo bpf; clase sin nombre (`?`) |
| lectores de `RegInfo` | pseudocódigo de las 4 funciones de registro | sin lecturas de `+0x10`/`+0x40` |
| medio actual | `ioctl_gifmedia` completo | tabla de medios vía slot `+0x948`; ivar sin fijar (`?`) |
| gate | vtable del controlador `+0x850` | `IONetworkController::executeCommand` |
| clase de `client+0x90` | todas las `__ZTV*BSDClient`, slot `+0x1C8` | `_RESERVED`, clase no resuelta (`?`) |
