# Etapa 4 (llw0): cierre de los tres puntos abiertos

Cierra los `?` de `AWDL_ETAPAS_2_A_4.md` §3 y de `AWDL_LLW_COMPANION.md`: quién usa los
ioctls 130/131, qué hace el kernel al recibirlos, quién dispara la destrucción de la
companion y qué celdas de vtable y llamadas a la familia exige una clase `RTLLlwInterface`
mínima. Base: `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`) y el kernelcache
26.7.1 (md5 `f489ff8be80e6013a1bf47561dcaef45`), desensamblado headless sobre copias de las
bases de datos; las originales no se abrieron. Las direcciones son VA del binario indicado y
la cabeza de cada función citada se comprobó contra la base de datos; lo que no se puede
decidir sin ejecución va como `?`.

## 1. Resultado

| Punto | Resultado |
|---|---|
| **1. Ioctls 130/131** | No son peticiones apple80211. `wifip2pd` las emite sobre un socket `AF_INET6`/`SOCK_DGRAM` con `0xC0806982` (`_IOWR('i',130,128)`) y `0xC1206983` (`_IOWR('i',131,288)`). El 130 lo consume el stack IPv6 en `in6_control` (0xffffff80007dd4b0), sin llamada directa a `processBSDCommand` (las indirectas del adjunto no se resolvieron, `?`). El 131 no encaja en el switch de `in6_control`: cae en `ifnet_ioctl` (0xffffff80007de187) y, si el puente no lo intercepta (`?`), llega al `processBSDCommand` de la interfaz, donde la base `IO80211SkywalkInterface::processBSDCommand` (0xffffff80022f6e98) lo rechaza con **45 (ENOTSUP)** y el log `Rejected w/ENOTSUP`; la clase LL no lo rechaza por sí misma. El daemon ve `errno = 45` en el primer ioctl (131) y ya no envía el 130. No rompe AWDL: el 293 no depende de esta pareja y no hay teardown de `awdl0`. |
| **2. Destrucción (op = 0)** | `wifip2pd` no emite nunca `op = 0`: el único `Apple80211RawSet` de tipo 293 (0x10004b3e6, en `sub_10004B300`) construye el payload con `op = 1`; no hay `RawSet` 95 ni otra construcción del 293. La rama de destrucción del kernel existe y se recorrió: `AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE` (0xffffff8001722ef2) → `AppleBCMWLANCore::destroySkywalkInterface` (0xffffff80016aae4e) → `IOService::terminate(2)` (slot +0x600, 0xffffff8000a937b0) → `AppleBCMWLANLowLatencyInterface::free` (slot +0x90, 0xffffff80015e0046) → hueco de la tabla del core a cero. Solo es alcanzable desde un cliente externo, no desde este daemon. |
| **3. Clase mínima** | Es un segundo objeto de la cadena `IO80211VirtualInterface`, creado con la misma secuencia que `awdl0` (init con rol 2 y nombre `"llw"`, `setInterfaceId`, `attach`, `start`, `initRegistrationInfo`, `registerEthernetInterface`, `registerService`), con `getBSDNamePrefix` → `"llw"` y el nombre BSD copiado a payload+8. `awdl0` **no** necesita referencia a la companion para que el 293 devuelva 0; los dos enlaces de la familia son null-safe o de solo escritura. |

## 2. Punto 1 — los ioctls 130/131

### 2.1 Lado daemon: quién los emite

| Elemento | Dirección | Evidencia |
|---|---|---|
| `sub_100058420` | 0x100058420 | Crea `socket(30, 2, 0)` (`sub_1001CFA90`, sitio 0x10005846e): familia 30 = `AF_INET6`, tipo 2 = `SOCK_DGRAM`. Rellena dos requests con forma de `in6_aliasreq`: el primero (288 B) sólo con el nombre en +0 (copiado con `strlcpy(..., 0x10)` desde el `String` de `self+0x10`); el segundo (128 B) añade `sin6_len = 28`/`sin6_family = 30` en +0x10 y la dirección de 16 B en +0x18. |
| `sub_1000586D0` | 0x1000586d0 | Gemela de la anterior: mismos dos ioctls y mismo armado. Sin referencias absolutas en el binario; puede estar en una method list relativa (`?`). |
| `sub_10005F7B0` | 0x10005f7b0 | Thunk: `return sub_100058420(a1)`. |
| `sub_100068EE0` | 0x100068ee0 | Thunk de `sub_10005F7B0`; sus únicas referencias son datos en 0x100517bd8 y 0x10052cb90 (tablas de punteros a funciones). El nombre Swift exacto del protocolo y del método no se recuperó (`?`). |
| Tabla de requests | 0x100493008 | Índice 5 → `0xC0806982`; índice 6 → `0xC1206983`. `sub_1004443B4` (0x1004443b4) sólo indexa la tabla. |
| Ioctl 131 | 0x100058509 | `sub_100058420` llama `ioctl(fd, 0xC1206983, &req)` con el `in6_aliasreq` de 288 B. |
| Ioctl 130 | 0x1000585f8 | Segunda llamada de `sub_100058420`, con el buffer de 128 B. En `sub_1000586D0` el par está en 0x1000587e6 (131) y 0x1000588ee (130). |

La secuencia es **131 y después 130**, no al revés. El destino es el nombre que lleve el
objeto `Socket` en `self+0x10`; el helper sirve para cualquier interfaz y no contiene el
literal `llw0`, así que la afirmación «sobre `llw0`» depende de quién construya ese `Socket`:
no se localizó el inicializador que le pasa el nombre de la companion (`?`). Los literales
`llw0` del binario están en la validación de la creación (`if_nametoindex`, 0x10004b814), no
en este helper. Lo que sí queda cerrado es que no usa el canal apple80211: no hay
`Apple80211RawSet`/`RawGet` en la función, sólo `ioctl` de BSD sobre un socket IPv6.

### 2.2 Lado kernel: qué hace al recibirlos

| Paso | Dirección | Evidencia |
|---|---|---|
| `in6_control` | 0xffffff80007dd4b0 | Sólo 130: `mov ecx, 0C0806982h` en 0xffffff80007ddcee, `cmp r15, rcx` y llamada al helper en 0xffffff80007ddd19. El helper compara 0xC0786982 y 0xC0806982 (dos tamaños del mismo número 130), no el 131. |
| Helper del caso | 0xffffff80007dec30 | Compara `a2` con 3229116802 (`0xC0786982`, tamaño 120) y 3229641090 (`0xC0806982`, tamaño 128); cualquier otro valor dispara el assert de `in6.c:745`. Copia 128 B en ambos casos, comprueba `sin6_family` (byte +0x11) y llama a `in6_ifattach_aliasreq` en 0xffffff80007ded41. |
| `in6_ifattach_aliasreq` | 0xffffff80007e7c00 | `in6_ifattach_prelim` (0xffffff80007e77b0), que exige `if_type == 57` o el bit 15 de `if_flags` (`*(int16*)(ifp+190) < 0`; el nombre de ese bit no se determinó, `?`) y monta `ND_IFINFO`/`nd6_ifattach`/`scope6_ifattach`; después genera el link-local `fe80::` y lo añade con `sub_FFFFFF80007E7FD0` (0xffffff80007e7fd0), que obtiene el IID de `in6_iid_from_hw`. |
| 131 | 0xffffff80007de172 | `in6_control` no lo reconoce y ejecuta el caso por defecto: `ifnet_ioctl(ifp, 30, cmd, arg)` en 0xffffff80007de187. Para una interfaz Skywalk, el `if_ioctl` desemboca en `IO80211SkywalkInterface::processBSDCommand` (0xffffff80022f6e98; el puente exacto `IOSkywalkNetworkBSDClient` no se siguió, `?`), que sólo atiende cuatro constantes apple80211 y devuelve `v38 = 45` (`ENOTSUP`) para el resto, con `IOLog("Rejected w/ENOTSUP: ...")` en la línea 1072. `AppleBCMWLANLowLatencyInterface::processBSDCommand` no rechaza 131 (sus cuatro constantes son otras), pero delega en `IO80211InfraInterface::processBSDCommand` (0xffffff8002360290), que a su vez llama a esta base, y el resultado es 45. |
| Retorno | — | 130: `0` si la interfaz existe y el adjunto termina; `ENXIO` si el nombre no resuelve. 131: `45` antes de salir del `ifnet_ioctl`, de modo que el primer ioctl de la pareja falla y el segundo no se envía. El daemon convierte el `errno` en error Swift (comprobaciones `v14 != -1`/`v14 != 0` en `sub_100058420`, sitios 0x10005850e y 0x1000585fd). |

La pareja no es simétrica: el 130 se resuelve en el stack IPv6 (con la salvedad de las
llamadas indirectas de `sub_FFFFFF80007E7FD0`, que no se resolvieron, `?`) y el 131 baja por
`ifnet_ioctl` hasta el `processBSDCommand` de la interfaz, donde la base lo rechaza con 45.
La clase LL no es la que rechaza: su override devuelve 22 sólo para las cuatro constantes
apple80211 (§5) y delega el 131 en la base, que es la que responde `ENOTSUP`.

### 2.3 Efecto sobre AWDL

- Estos ioctls no están en la ruta del 293 (`sub_10004B300` no los llama) y ningún camino
  observado destruye `awdl0`; el fallo del 131 (45) sólo deja sin la dirección IPv6 que el
  helper pretendía configurar.
- El orden 131 → 130 hace que el error del 131 impida el adjunto del 130: la pareja no llega
  a completarse en una interfaz cuyo `processBSDCommand` no implemente el 131. Para que
  `awdl0`/`llw0` complete la configuración IPv6 haría falta atender ese comando en la clase
  correspondiente; para el 293 no es necesario.
- La creación de la companion sigue siendo opcional: si el 293 falla o no existe, `awdl0`
  permanece aceptada (`AWDL_LLW_COMPANION.md` §2.3).
- Queda `?` si el llamador del helper trata el error como fatal; la función lanza, pero el
  catch no se localizó.

## 3. Punto 2 — destrucción de la companion (op = 0)

### 3.1 Quién la dispara

`wifip2pd` tiene **exactamente dos** llamadas a `Apple80211RawSet` (0x1004457e0):

| Sitio | Tipo | Función | Payload |
|---|---:|---|---|
| 0x100048d95 | 94 (`VIRTUAL_IF_CREATE`) | `sub_100047660` | creación de `awdl0` |
| 0x10004b3e6 | 293 (`COMPANION_SKYWALK_IF_CREATE`) | `sub_10004B300` | `op = 1` (decompilado: `v61[0] = a5`, `v61[1] = 1`); tipo en 0x10004b3be |

No hay otro 293, no hay `RawSet` de tipo 95 (`VIRTUAL_IF_DELETE`) y no se encontró ninguna
construcción del payload con `op = 0`. Por tanto **este build de `wifip2pd` no destruye la
companion**: la rama de destrucción del kernel queda para un cliente externo (herramienta
de diagnóstico o un `RawSet` manual), no para el daemon. El hecho de que `sub_1000586D0` no
tenga referencias absolutas no cambia la conclusión: esa función es IPv6 (§2.1), no el 293.

### 3.2 Qué hace el kernel

| Paso | Dirección | Evidencia |
|---|---|---|
| Handler 293 | 0xffffff8001722ef2 | `op` en payload+4. `op = 0`: si el puntero de companion (`ivars+120`, base `this+0x138`) no es nulo, `destroySkywalkInterface(core, getInterfaceId(companion))` en 0xffffff8001722fe9; si es nulo, devuelve `0xE00002C0` con el log de la línea 3853. `op` distinto de 0/1: no-op que devuelve 0. |
| `destroySkywalkInterface` | 0xffffff80016aae4e | Resuelve la entrada `[ifid]` de la tabla del core (`core+11232`), llama `[obj+0x600](2)` (0xffffff80016aaebc) y `[obj+0x90]()` (0xffffff80016aaed7), y escribe 0 en `core->ifTable[ifid]` (0xffffff80016aaeec). |
| Slot +0x600 | 0xffffff8000a937b0 | `IOService::terminate(unsigned int)` con `2`; el significado exacto de esa opción no se siguió (`?`), pero es la retirada del servicio que precede al `free`. |
| Slot +0x90 | 0xffffff80015e0046 | `AppleBCMWLANLowLatencyInterface::free`: libera el blob de `ivars+0x140` con `IOFreeTypeImpl` y encadena a `AppleBCMWLANSkywalkInterface::free` (0xffffff80015d8ce6). |

Lo que se libera es, por tanto, el objeto LL (con su blob de 0x140) y su entrada en la tabla
del core; la retirada del `ifnet`/entrada de IORegistry ocurre dentro de
`IOService::terminate`/`free` (el detalle instrucción a instrucción no se siguió, `?`). Una
clase propia debe reproducir los dos slots si quiere que `op = 0` no deje el objeto colgado:
`terminate(2)` y `free` (o el equivalente `stop` + `release`), y limpiar su propio puntero.

## 4. Punto 3 — clase `RTLLlwInterface` mínima

### 4.1 Condición de éxito del 293

El slot **+0x1180** de la clase AWDL (declarado en `AWDLInterface.h:275`, hoy con
`AWDL_STUB(...)` → `kIOReturnUnsupported`) debe:

1. Leer `op` en payload+4 y el nombre en payload+8.
2. `op = 1`: si no hay companion, crear el objeto LL y su `ifnet`; copiar el nombre BSD
   (máx. 16 B) en payload+8 y devolver 0. `if_nametoindex("llw0")` en el daemon
   (0x10004b814) es la validación final.
3. `op = 0`: destruir y devolver 0 (o `0xE00002C0` si no hay companion, como Broadcom).
4. Cualquier otro `op`: devolver 0 sin tocar nada.

### 4.2 Llamadas a la familia

La secuencia válida es la que ya usa `awdl0` en `Source/AirLayer/AWDLInterface.cpp`
(`initWithController` + `startWithRegistration`), con rol 2 y prefijo `"llw"`:

| Llamada | Símbolo / dirección | Para `llw0` |
|---|---|---|
| `IO80211VirtualInterface::init(ctrl, &mac, role, name)` | 0xffffff80023305f8 | `role = 2`, `name = "llw"`; fija `IO80211InterfaceRole = "LowLatency"` (tabla de roles, entrada 2) |
| `IO80211SkywalkInterface::setInterfaceId(id)` | 0xffffff80022f950a | id 9, el que usa Broadcom para la LL (`createSkywalkInterface`, 0xffffff80016aa874, params+0x10 = 9) |
| `attach(ctrl)` | slot 0x670 de la base | igual que `awdl0` |
| `IO80211VirtualInterface::start(ctrl)` | 0xffffff80023308aa | arranque de la base; la LL de Broadcom añade `setInitMacAddress` (0xffffff80015e00bc) antes de la base |
| `IOSkywalkEthernetInterface::initRegistrationInfo(reg, 1, 0x130)` | 0xffffff8002b108e2 | versión 1, tamaño 0x130 |
| `registerEthernetInterface(reg, queues, n, txPool, rxPool, flags)` | 0xffffff8002b10a84 | las mismas tres colas de `awdl0` (TX submission, TX completion, RX completion) |
| `registerService(kIOServiceSynchronous)` | `IOService` | publica y crea el `ifnet`; el nombre se lee después con `getBSDName` |
| `IO80211SkywalkInterface::getBSDName()` | 0xffffff80022f5fca | fuente del nombre para payload+8 |
| `setCompanionInterfaceId(awdlId)` | 0xffffff80022f952a | enlace de id; sólo escritura (§4.4) |

`RegistrationInfo` (0x130 B): +0x0C = 3 (subfamilia), +0x10 = 2 (rol, lo fija `init`),
+0x14 = 2 (nmedia), +0x24 = 0x80, +0x28 = 0x80 (media y media actual), +0x30 = puntero a
`"llw"`, +0x38 = 0 (unidad), +0x3C = 64 (headroom), +0x40 = 4 (flags, el `|= 4` que pone
Broadcom en `createSkywalkInterface`), +0x48 = 0x594/0x594 (MTU/máx.), +0x108 = MAC. Son los
mismos offsets que `AWDLInterface.cpp` usa para `awdl0` (`AWDL_REGINFO_*`).

La base mínima es `IO80211VirtualInterface` (no exige los 63 slots puros de
`IO80211AWDLProtocol`); si el compilador la considera abstracta por `OSDeclareAbstractStructors`,
la alternativa es derivar de `IO80211AWDLProtocol` y heredar los stubs de `RTLAwdInterface`.
Esa elección no cambia las celdas de §4.3; la instanciabilidad de la base queda como `?` hasta
compilar.

`IO80211Controller::setAwdlInterface` (0xffffff8002299c24) **no** hace falta para `llw0`: es
el puntero AWDL del controlador, no el mecanismo que crea el `ifnet`; en `awdl0` el nombre
BSD ya existe tras `registerService` y antes de publicar el puntero. Queda `?` si el cliente
BSD de la familia acepta una segunda interfaz virtual del mismo controlador sin un registro
específico de rol 2; la única evidencia estática es la secuencia de `awdl0`.

### 4.3 Celdas de vtable

| Celda | Implementación LL de referencia | ¿Imprescindible para el 293? | Qué hace |
|---|---|---|---|
| +0x9E8 `getBSDNamePrefix` | 0xffffff80015e00fc | **Sí** | Devuelve `"llw"`; sin ella el nombre sería el de la base. Llama antes a `validateDispatchQueue`. |
| +0x14E0 `start` | 0xffffff80015e00bc | **Sí** | `setInitMacAddress` desde el `RegistrationInfo` y `AppleBCMWLANSkywalkInterface::start`. |
| +0x8E0 `initBSDInterfaceParameters` | 0xffffff80015e034a | `?` | La base crea los parámetros; la LL añade el puntero de `ivars+0x140` y `params+0x18 = 8`. Para un `ifnet` Ethernet normal la base puede bastar. |
| +0x930 `setInterfaceEnable` | 0xffffff80015e010e | Recomendada | Base + `reportLinkStatus(this, 3, 0x80)` al habilitar; es el evento visible de `llw0`. |
| +0x910 `processBSDCommand` | 0xffffff80015e03e2 | Opcional | Rechaza 4 constantes apple80211 con 22 y delega el resto en `IO80211InfraInterface::processBSDCommand` (0xffffff8002360290). |
| +0xB38 `logTxPacket` | 0xffffff80015e0268 | Datapath | Logging; no-op admitido. |
| +0xB50 `inputPacket` | 0xffffff80015e02e2 | Datapath | RX real; no-op si no hay tráfico LL. |
| +0xA80 `configureMulticastFilter` | 0xffffff80015e041e | No | No-op (devuelve 0). |
| +0x90 `free` / +0x600 `terminate` | 0xffffff80015e0046 / 0xffffff8000a937b0 | Para `op = 0` | Libera el blob propio y retira el servicio; sin ellos la destrucción deja el objeto vivo. |
| +0xED8 `flushFlowQueues` | base (`IO80211SkywalkInterface`), null-safe | No | Sólo delega si hay companion (§4.4). |

Nota de corrección: la lista de constantes rechazadas por `processBSDCommand` es de **cuatro**
(0xC02069C9, 0xC02869C9, 0x802069C8, 0x802869C8), no de cinco. `0xC02069C8` sólo aparece
como frontera del `cmp edx, 0C02069C8h` / `jg` de 0xffffff80015e03e6, no como igualdad
rechazada; las igualdades que devuelven 22 están en 0xffffff80015e0400 (0xC02069C9) y
0xffffff80015e0408 (0xC02869C9), más 0xffffff80015e03ee y 0xffffff80015e03f6 para las dos
formas `0x8020/0x8028`. Por eso `0xC02069C8` cae en la base. La corrección afecta a
`AWDL_LLW_COMPANION.md` §3.4.

### 4.4 ¿Necesita `awdl0` referencia a la companion?

No para el 293 ni para el daemon:

- `AppleBCMWLANProximityInterface::flushFlowQueues` (0xffffff800172ac9e) lee `ivars+120`,
  comprueba nulidad y sólo entonces delega en `AppleBCMWLANSkywalkInterface::flushFlowQueues`;
  después llama a la base. Es null-safe.
- `IO80211SkywalkInterface::setCompanionInterfaceId` (0xffffff80022f952a) sólo tiene dos
  llamadores (los handlers 293 de Proximity y NAN); `getCompanionInterfaceId`
  (0xffffff80022f951a) **no tiene xrefs**: el id se escribe y nadie lo lee en este KC.
- `IO80211VirtualInterface::setCompanionLowLatencySkywalkInterface` (0xffffff8002334c6e)
  sólo se llama desde `AppleBCMWLANLowLatencyInterface::withProvider`
  (0xffffff80015dfee4, sitio 0xffffff80015dffbf); su getter (0xffffff80023344cc) no tiene
  xrefs. Es un enlace interno de la fábrica de Broadcom, no un requisito del 293. Que el
  símbolo sea enlazable desde un kext sigue siendo `?` (no se comprobó la tabla de
  exportaciones en esta pasada).

Por tanto, la clase AWDL puede guardar el puntero sólo si implementa `flushFlowQueues` de
verdad, y debe hacerlo null-safe; el 293 devuelve 0 y el daemon valida `llw0` sin consultar
ese enlace.

## 5. Correcciones a los documentos previos

| Documento | Corrección |
|---|---|
| `AWDL_LLW_COMPANION.md` §3.4 | `processBSDCommand` rechaza 4 constantes, no 5: `0xC02069C8` sólo es la frontera del `cmp edx, 0C02069C8h` / `jg` de 0xffffff80015e03e6; las igualdades que devuelven 22 son 0xC02069C9 (0xffffff80015e0400) y 0xC02869C9 (0xffffff80015e0408), más las dos formas 0x8020/0x8028, así que `0xC02069C8` se delega en la base. |
| `AWDL_LLW_COMPANION.md` §3.5 / `AWDL_ETAPAS_2_A_4.md` §3.4 | Los ioctls 130/131 no son apple80211. El 130 se resuelve en `in6_control`; el 131, si el puente no lo intercepta (`?`), pasa por `processBSDCommand` y la base lo rechaza con 45 (ENOTSUP). |
| `AWDL_ETAPAS_2_A_4.md` §3.2 | La destrucción por `op = 0` no la emite `wifip2pd` en este build (sólo hay un 293, con `op = 1`); la rama del kernel queda para un cliente externo. |

## 6. Método y límites

- Desensamblado headless sobre copias de las bases de datos de `wifip2pd` y del kernelcache,
  con scripts de sondeo `e1`–`e10` (daemon) y `f1`–`f14` (kernel); las originales no se
  abrieron en escritura.
- Cada dirección citada se comprobó con la cabeza de función correspondiente; los sitios de
  llamada se leyeron en el desensamblado y los `cmp`/`mov` citados son instrucciones reales.
- No hubo ejecución en hardware ni con el kext cargado: el comportamiento en runtime de
  `registerService` para una segunda interfaz virtual y el `catch` del error IPv6 quedan
  como `?`.
- No se tocó código ni otros documentos; este archivo es el único resultado de la tarea.
