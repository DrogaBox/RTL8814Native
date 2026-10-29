# Comprobaciones pendientes de la etapa 1

Cierre de cuatro puntos abiertos de `AWDL_STAGE1_CODE_FIXES.md` y `AWDL_STAGE1_SPEC_DELTA.md`,
verificados en el desensamblado del kernelcache de macOS 26.7.1 (25G241, x86_64) sobre una copia de
la base anotada. Las direcciones de función se comprobaron una a una: cada VA citado como función es
inicio de función (el inicio coincide con la dirección citada); los VA de instrucción se citan dentro
de la función verificada cuya cabeza se indica. Los puntos que el desensamblado no fija llevan `?`.

## 1. Lector de `RegistrationInfo+0x3C` en la copia

`AppleBCMWLANCore::createProxInterface` (cabeza `0xffffff800165dcbe`) escribe `word [r12+3Ch] = 40h`
(64, headroom) en `0xffffff800165de8c`, sobre el `RegistrationInfo` de 0x130 bytes que borra con
`bzero` en `0xffffff800165dda7` y pasa a `AppleBCMWLANProximityInterface::start` en
`0xffffff800165dead`. La familia hace dos copias de esa estructura:

| VA | Hecho |
|---|---|
| `0xffffff8002b109f0` | Cabeza de `IOSkywalkEthernetInterface::copyRegistrationInfo`. Exige versión 1 y tamaño ≥ 0x130 (`0xffffff8002b10a08`-`0x10a15`), asigna con el asignador tipado, guarda el bloque en `[[this+0x118]]` (`0xffffff8002b10a26`-`0x10a2d`) y copia 0x130 bytes (`0xffffff8002b10a40`). |
| `0xffffff8002b10a84` / `0xffffff8002b10b46` | Cabezas de las dos sobrecargas de `registerEthernetInterface`; pasan la copia de `+0x118` a `registerNetworkInterface`. |
| `0xffffff8002b0a204` | Cabeza de `IOSkywalkNetworkInterface::registerNetworkInterface`; exige `[RegInfo+4] ≥ 0x108` y versión 1 (`0xffffff8002b0a23c`-`0x250`), asigna un bloque propio, lo colga de `[[this+0xC0]]` (`0xffffff8002b0a28b`-`0x298`) y hace `memmove` de 0x108 bytes que incluye `+0x3C` (`0xffffff8002b0a29e`-`0x2a9`). |

Barrido de desplazamientos 0x3C, 0xFC y 0xC0 sobre todo el código de IOSkywalkFamily, IO80211Family
y AppleBCMWLANCoreMac (19 190 funciones, 1 053 713 instrucciones): un único lector con el patrón
`[this+0xC0]` + `[copia+0x3C]`.

| VA | Hecho |
|---|---|
| `0xffffff8002b0baba` | Cabeza de `IOSkywalkNetworkInterface::getTxDataOffset` (ranura virtual 0x9C0 de la vtable de `IOSkywalkNetworkInterface`, `0xffffff8002b3dc88`). Carga `rax = [this+0xC0]` (la copia), devuelve 0 si es nula o si `[copia]` (versión) es 0, y lee `movzx eax, word [rax+0x3C]` en `0xffffff8002b0bad2`. |
| `0xffffff8002affa8e` | Dentro de `IOSkywalkNetworkBSDClient::start` (cabeza `0xffffff8002aff562`): llama a la ranura 0x9C0 sobre la interfaz y guarda el resultado u16 en `[clienteBSD+0x13Ch]` (`0xffffff8002affa94`); el headroom llega así al cliente BSD. |
| `0xffffff80015d92b4`, `0xffffff80015d9583` | Dentro de `AppleBCMWLANSkywalkInterface::start` (cabeza `0xffffff80015d909a`): dos lecturas de `word [r15+3Ch]` sobre el `RegistrationInfo` original del driver. `?` El enlace de `r15` al segundo argumento no se fijó instrucción a instrucción. |

Conclusión: el consumidor existe. `getTxDataOffset` es la única lectura de `+0x3C` en la copia de
`+0xC0`; con el defecto 6 sin corregir (`+0x3A`) devuelve 0 y el headroom del cliente BSD queda a 0,
así que la corrección del offset no es cosmética. En la copia de `+0x118` no se encontró lector de
`+0x3C`. Sitios que llaman por la ranura 0x9C0 con receptor verificado: `IOSkywalkNetworkBSDClient::start`
(`0xffffff8002affa8e`) e `initRegistrationInfo` (`0xffffff8002b10973`, que siembra `RegInfo+0x28` con
el valor o 0x20 por defecto). `?` Qué campo del `ifnet` consume `clienteBSD+0x13C` no se siguió.

## 2. `IO80211VirtualInterface::free` con objeto a medio inicializar

Cabeza de `init`: `0xffffff80023305f8`. Ramas de fallo (todas devuelven 0 en `r14d`):

| Rama | VA de salida | Estado del objeto en ese punto |
|---|---|---|
| 1: `IO80211SkywalkInterface::init` falla | `0xffffff8002330623` | `[this+0x128]` (bloque de ivars) aún sin escribir: queda 0 si la asignación del objeto llega a cero (ver abiertos). |
| 2: fallo del `IOMallocType` de ivars | `0xffffff800233063f` | `[this+0x128] = 0` escrito en `0xffffff8002330635`. |
| 3: `IOLockAlloc` falla | `0xffffff8002330663` | ivars asignados; `ivars+0x30 = 0`. |
| 4: fallo del segundo `IOMallocType` | `0xffffff8002330683` | `ivars+0x28 = 0`; el cerrojo de `ivars+0x30` queda asignado. |
| 5: `IO80211FlowQueueDatabase::initialize` falla | `0xffffff8002330746` | `ivars+0xE0` (diccionario) asignado; `ivars+0x200 = 0`; `registerInterestedDriver` ya emitido en `0xffffff8002330696`. |

Cabeza de `free`: `0xffffff8002330b8c` (confirmada como ranura 18 de la vtable de
`IO80211AWDLProtocol`, `0xffffff8002456440`, y recalculada desde cinco vtables del barrido).

| Ivar liberado | VA de la guardia | Comprobación |
|---|---|---|
| `[this+0x128]` (bloque de ivars) | `0xffffff8002330b9c`-`0xb9f` | `test`/`jz`: si es nulo salta todo el bloque y va al `free` base. |
| `ivars+0xE0` (diccionario) | `0xffffff8002330bac`-`0xbaf` | `test`/`jz` antes del release (`[vtable+0x28]`). |
| `ivars+0x200` (base de colas de flujo) | `0xffffff8002330bd3`-`0xbd6` | `test`/`jz` antes del release. |
| `ivars+0x208` | `0xffffff8002330bf3`-`0xbfd` | `test`/`jz` antes de la llamada `[vtable+0x8F0]`. |
| Bloque completo | `0xffffff8002330c1e` | `memset_s(ivars, 0x210, 0, 0x210)` antes de `IOFreeTypeImpl`. |

Ninguna liberación de `free` carece de comprobación de nulo. El `free` base,
`IO80211SkywalkInterface::free` (cabeza `0xffffff80022f6184`, cola de la cadena), usa el mismo patrón
`test`/`jz` en las liberaciones revisadas (`0xffffff80022f61d6`, `0xffffff80022f61fd`,
`0xffffff80022f6224`, ...). El llamador de referencia libera en el fallo:
`AppleBCMWLANProximityInterface::withOptions` (cabeza `0xffffff80017159e6`) llama a la ranura de init
(`+0x11C8`, ocupada por `AppleBCMWLANProximityInterface::init`, `0xffffff8001718db0`) y, si devuelve
0, hace release por `[vtable+0x28]` (`0xffffff8001715a49`-`0x1715a59`), con lo que `free` corre sobre
el objeto a medio inicializar.

Conclusión: `free` tolera el objeto a medio inicializar en las cinco ramas: toda liberación está
vigilada y los campos no tocados por `init` sólo son seguros si la memoria del objeto llega a cero.
Residuos sin crash: el cerrojo de `ivars+0x30` y el bloque de `ivars+0x28` no se liberan en `free`
(`?` propietario alternativo no identificado) y la rama 5 deja el registro de driver interesado sin
deshacer (`?` sin limpiador localizado en la cadena de `free`).

## 3. `deregisterEthernetInterface`: argumento y necesidad

Cabeza: `0xffffff8002b10af4`. Reenvía `esi` sin tocarlo a
`IOSkywalkNetworkInterface::deregisterNetworkInterface(uint)` (`0xffffff8002b10b01`, cabeza
`0xffffff8002b09e9c`) y, si el resultado es 0, libera el bloque colgado de `[this+0x118]`
(`0xffffff8002b10b0d`-`0x10b35`). El `uint` argumento no se lee en ninguna de las dos funciones; la
implementación base tampoco lo usa y siempre devuelve 0 (`0xffffff8002b09ff1`).

| Llamador | VA de la llamada | Valor pasado |
|---|---|---|
| `IOSkywalkEthernetInterface::registerEthernetInterface` (variante packet-queue, cabeza `0xffffff8002b10a84`) | `0xffffff8002b10ab5` | 0 (`xor esi,esi` en `0xffffff8002b10ab3`), camino de fallo de `copyRegistrationInfo`. |
| `IOSkywalkEthernetInterface::registerEthernetInterface` (variante logical-link, cabeza `0xffffff8002b10b46`) | `0xffffff8002b10b71` | 0 (`xor esi,esi` en `0xffffff8002b10b6f`), mismo camino. |
| `AppleEthernetRL::stopInterface` (cabeza `0xffffff800295bf2a`) | `0xffffff800295c05f` | 0 (`xor esi,esi` en `0xffffff800295c053`, cola con `jmp`). |

`AppleBCMWLANSkywalkInterface` no aparece entre los llamadores. El desmontaje de referencia de
Broadcom, `AppleBCMWLANCore::destroySkywalkInterface` (cabeza `0xffffff80016aae4e`), llama a la
ranura `+0x600` con `esi=2` (`0xffffff80016aaebc`) y después a la ranura 18, `free`
(`0xffffff80016aaed7`), sin pasar por `deregister`. La limpieza no depende de la llamada explícita:
`IOSkywalkNetworkInterface::free` (cabeza `0xffffff8002b09ffe`) llama a `deregisterNetworkInterface`
incondicionalmente al principio (`0xffffff8002b0a007`), y esa función libera la copia de `+0xC0`, el
`OSOrderedSet` de `+0xC8` y los objetos de `+0x98`/`+0xA0`/`+0xD0`.

Conclusión: un driver que falla después de `registerService` no necesita llamar a
`deregisterEthernetInterface` para la limpieza: el `free` del objeto la cubre. La llamada explícita es
el patrón de `AppleEthernetRL` en `stop`, útil para descolgar la interfaz en vida del objeto; el
argumento es operativamente 0. `?` La intención declarada del parámetro `uint` no es deducible del
binario.

## 4. `checkForWork` de la cola TX completion con acción nula

Creación: `IOSkywalkTxCompletionQueue::initWithPool` (cabeza `0xffffff8002b129cc`) sólo exige pool no
nulo, capacidad ≥ 2 y último argumento 0 (`0xffffff8002b129df`-`0x129f6`); la acción se guarda sin
comprobación por `IOSkywalkPacketQueue::initWithPool` (cabeza `0xffffff8002b08bc8`, llamada por el
offset 0x240 de la ZTV `0xffffff8002b3cc70`), que pasa propietario y acción a la inicialización del
event source: el propietario queda en `+0x18` y la acción en `+0x20`, los offsets que después usa
`enqueueAndCompletePackets`. La acción nula se acepta. Los bits que activan el camino: `+0x28` bit 0
(event source habilitado, lo pone `IOEventSource::enable`, cabeza `0xffffff8000acaa40`, en
`0xffffff8000acaa44`) y `+0xCC` bit 0 (cola inicializada, lo pone `IOSkywalkPacketQueue::initialize`,
cabeza `0xffffff8002b08cce`, en `0xffffff8002b08d85`, y lo quita `finalize` en `0xffffff8002b08e23`).

| VA | Hecho |
|---|---|
| `0xffffff8002b12d00` | Cabeza de `IOSkywalkTxCompletionQueue::initialize`: escribe `[this+0x11C]` y `[this+0x118]` con el mismo valor (`0xffffff8002b12d48`, `0xffffff8002b12d4f`). |
| `0xffffff8002b2db28` | Cabeza de `IOSkywalkTxCompletionQueue::requestEnqueue`. Con el bit 0 del flags (`dl`): `[this+0x118]++` (`0xffffff8002b2db8e`), `signalWorkAvailable` (`0xffffff8002b2db97`) y retorna: es el camino asíncrono que deja pendientes. Sin el bit, llama a `enqueueAndCompletePackets` bajo el gate (`0xffffff8002b2db71`). |
| `0xffffff8002b2da78` | Cabeza de `checkForWork` (ranura 36, offset 0x120, de la vtable de la cola `0xffffff8002b40700`, la misma ranura 36 de `IOEventSource` en `0xffffff80002a5d70`). Si `[0x11C] == [0x118]` sale (`0xffffff8002b2da84`); si no, sincroniza, exige ambos bits y llama a `enqueueAndCompletePackets` con `[this+0x58]` (`0xffffff8002b2daa9`). El despachador es `IOWorkLoop::runEventSources` (cabeza `0xffffff8000ac9c40`), que llama por la ranura 0x120. |
| `0xffffff8002b2dab2` | Cabeza de `enqueueAndCompletePackets`: tras el bit de reentrada `+0x110`, llama a la acción `call [this+0x20]` sin comprobar nulo (`0xffffff8002b2dae7`) con `(propietario, cola, anillo, capacidad-1, refCon)` y completa lo que devuelva con `completePackets` (`0xffffff8002b2db0c`, cabeza `0xffffff8002b12b9e`). |
| `0xffffff8002b2dbb6` | Cabeza de `enqueuePackets`: copia el lote al anillo y llama a `completePackets` en línea (`0xffffff8002b2dc41`); no toca `+0x118` ni llama a la acción. |

Conclusión: sí, `checkForWork` puede alcanzarse con pendientes: `requestEnqueue` asíncrono incrementa
`[0x118]` y señala el work loop, y la pasada siguiente compara, sincroniza y llama a la acción sin
guardia; con acción nula eso es una llamada a la dirección 0 y un pánico. El mismo riesgo corre el
camino síncrono de `requestEnqueue`. En el cableado de etapa 1 la única API usada sobre la cola TX
completion es `enqueuePackets` (vía `rtl_txcomp_enqueue` desde la acción no-op de la TX submission),
que nunca toca `+0x118` ni la acción: con esa única vía la acción nula es inalcanzable y `checkForWork`
sale siempre por la comparación inicial. `?` No se ha localizado ningún llamador de `requestEnqueue`
sobre las colas de `awdl0` ni el punto de la familia que habilitaría el event source (`+0x28`) en el
arranque del datapath; si alguno existiera, la acción nula pasaría de latente a fatal. El arreglo del
defecto 1 (acción no nula en la TX submission) sigue siendo bloqueante por `initWithPool` de la cola
de envío, no por esta vía.

## Puntos que quedan abiertos

| # | Punto | Dónde |
|---|---|---|
| 1 | El cero inicial de la memoria del objeto y de los ivars se apoya en el asignador tipado (`OSObject_typed_operator_new` `0xffffff8000a1cc40` → `kalloc_type_impl_external` `0xffffff8000369980` con máscara 0xD1004 → asignador de montículo `0xffffff80003d4230`); el relleno a cero dentro del asignador de zonas no se siguió instrucción a instrucción. | §2 |
| 2 | Limpieza del registro de driver interesado (`registerInterestedDriver`, `0xffffff8002330696`) cuando la rama 5 de `init` falla. | §2 |
| 3 | Propietario de la liberación del cerrojo `ivars+0x30` y del bloque `ivars+0x28` de `IO80211VirtualInterface`. | §2 |
| 4 | Campo del `ifnet` que consume `clienteBSD+0x13C` tras `getTxDataOffset`. | §1 |
| 5 | Enlace de `r15` al `RegistrationInfo` en las lecturas de `AppleBCMWLANSkywalkInterface::start`. | §1 |
| 6 | Llamador de `requestEnqueue` sobre las colas de `awdl0` y punto de habilitación del event source (`+0x28`) en el arranque del datapath de la familia. | §4 |
| 7 | Intención declarada del parámetro `uint` de `deregisterEthernetInterface`/`deregisterNetworkInterface` (operativamente ignorado; los tres llamadores pasan 0). | §3 |
