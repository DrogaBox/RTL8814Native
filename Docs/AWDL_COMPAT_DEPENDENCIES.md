# Dependencias de compatibilidad del código AWDL

Inventario de todo lo que el código AWDL de la etapa 1 presupone del sistema operativo, con la
versión de referencia (macOS 26.7.1, build 25G241, Darwin 25.6.0, `BootKernelExtensions.kc`
md5 `f489ff8be80e6013a1bf47561dcaef45`) y la receta más barata para recomprobar cada supuesto en
un kernelcache nuevo (objetivo: macOS 14–26).

Alcance del código: `Source/AirLayer/AWDLInterface.cpp`, `Source/AirLayer/AWDLInterface.h` y las
partes AWDL de `Source/AirLayer/RTLAirLayer.cpp`/`.hpp` (override del 94, boot-arg `rtlawdl`,
creación/destrucción de `awdl0`, bits de capacidad). La justificación de cada fila cita el
documento `Docs/AWDL_*` que la cierra y, cuando procede, la sección de
[`AWDL_STAGE1_IMPL_SPEC.md`](AWDL_STAGE1_IMPL_SPEC.md).

Convenciones:

- `?` = no cerrado por los fuentes; la afirmación hereda el `?` del documento de origen.
- Las VAs citadas (p. ej. `0xffffff8002b0caac`) son de 25.6.0 y pudren en cada versión: en un
  kernelcache nuevo toda función se localiza por símbolo, nunca por dirección.
- Las tablas de comprobación del fileset usan la tabla de símbolos de `LC_SYMTAB` por
  `LC_FILESET_ENTRY` (receta `kcsym.py`, volcados de referencia en
  `Docs/re/io80211_25.6.0/syms.txt`).
- No hay offsets de ivar fijados a mano en el código AWDL propio: los ivars `_awdl`,
  `_awdlLock`, `_awdlArmed` (`RTLAirLayer.hpp:195-197`) son C++ normal. Los offsets de ivar que
  aparecen en los documentos (p. ej. `*(ctrl+0x120)+0x188` para el 97) pertenecen a la familia,
  no a este código.

Las dos últimas columnas de cada tabla son: cómo puede cambiar el supuesto entre versiones de
macOS, y la verificación más barata sobre un kernelcache (o, cuando el constante vive en
userspace, sobre el binario correspondiente).

## 1. Símbolos importados

### 1.1 Símbolos `__asm` explícitos

Los 14 símbolos que `AWDLInterface.cpp` importa por nombre mangleado. Entre paréntesis, el gemelo
del camino STA en `RTLAirLayer.cpp`, que comparte el mismo riesgo y la misma comprobación.

| Envoltorio | Símbolo mangleado | Declaración | Uso | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|---|
| `rtl_sky_initRegInfo` | `__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm` | `AWDLInterface.cpp:29` (`RTLAirLayer.cpp:139`) | Valida versión/size del `RegistrationInfo` y deja los defaults antes del relleno manual (`:188`) | Cambio de firma o de tamaño validado; retirada del método | Tabla de símbolos del fileset IOSkywalkFamily (tipo externo definido); desensamblar la función por símbolo para leer los literales de versión (1) y size (`0x130`) |
| `rtl_sky_setId` | `__ZN23IO80211SkywalkInterface14setInterfaceIdEj` | `:31` (`RTLAirLayer.cpp:146`) | Fija el id de interfaz BSD 2 (`AWDL_IFACE_ID`) antes de `attach` (`:118`) | Cambio de firma; retirada | Symtab del fileset IO80211Family |
| `rtl_sky_getWorkQueue` | `__ZN23IO80211SkywalkInterface12getWorkQueueEv` | `:33` (`RTLAirLayer.cpp:148`) | Work loop del objeto para añadir/quitar las tres colas como event sources (`:163-170`, teardown `:234`, `:273`) | Cambio del tipo devuelto o retirada; si deja de ser la misma cola que usa la familia, el orden con el gate se rompe | Symtab; desensamblar para confirmar qué cola devuelve |
| `rtl_sky_getBSDName` | `__ZN23IO80211SkywalkInterface10getBSDNameEv` | `:35` | Nombre BSD compuesto por el cliente BSD tras el `registerService` síncrono (`:218`); vacío ⇒ fallo (`:219-223`) | Retirada | Symtab del fileset IO80211Family |
| `rtl_pool_withName` | `__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE` | `:37` (`RTLAirLayer.cpp:153`) | Crea los pools `rtlawdl:tx`/`rtlawdl:rx` (`:149-150`) | Cambio de firma o del layout de `PoolOptions` (§2.4) | Symtab del fileset IOSkywalkFamily |
| `rtl_txsub_withPool` | `__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j` | `:40` (`RTLAirLayer.cpp:156`) | Cola TX submission con acción no nula `txNoop` (`:155`); `initWithPool` rechaza acción nula | La firma codifica el tipo del puntero de acción: cualquier cambio de firma del action rompe el mangleo; validaciones nuevas en `initWithPool` | Symtab; desensamblar `IOSkywalkTxSubmissionQueue::initWithPool` |
| `rtl_txcomp_withPool` | `__ZN26IOSkywalkTxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j` | `:43` (`RTLAirLayer.cpp:159`) | Cola TX completion con acción idle `rtlAwdIdleAction` (`:157`) | Ídem | Symtab; ídem |
| `rtl_rxcomp_withPool` | `__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j` | `:46` (`RTLAirLayer.cpp:162`) | Cola RX completion (`:158`) | Ídem | Symtab; ídem |
| `rtl_vif_init` | `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` | `:49` | Init de la base: publica el rol (6 → `AirLink`) y el prefijo antes de `attach` (`:114`) | Cambio de firma; renumeración de la tabla rol→nombre (§3) | Symtab; desensamblar `init` y la tabla de nombres de rol |
| `rtl_txcomp_enqueue` | `__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` | `:51` | `txNoop` devuelve los paquetes por la TX completion (`:264`); exige `count < capacidad` (`:261-262`) | Validaciones nuevas o límites distintos | Symtab; desensamblar `enqueuePackets` |
| `rtl_pool_free` | `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `:53` (`RTLAirLayer.cpp:200`) | Devolución al pool cuando `enqueuePackets` falla (`:266`) | Retirada | Symtab |
| `rtl_vif_start` | `__ZN23IO80211VirtualInterface5startEP9IOService` | `:55` | Arranque base (PMinit/joinPMtree, peer manager, agente MAC) antes del registro (`:176`) | Cambio de firma; comportamiento nuevo que exija pasos previos | Symtab; desensamblar `start` |
| `rtl_ctrl_setAwdlInterface` | `__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface` | `:57` | Publica/retira el objeto en el controlador (`:337`; `RTLAirLayer.cpp:1806`, `:1823`) | Sin xrefs en 25.6.0 (`?`, spec §7.2): el kernel no lo exige hoy y puede desaparecer en cualquier versión sin que nada del sistema lo eche de menos; el único consumidor sería `getAwdlInterface` | Symtab del fileset IO80211Family; si falta, retirar `rtlAwdlPublish` del código |
| `rtl_eth_register` | `__ZN26IOSkywalkEthernetInterface25registerEthernetInterfaceEPKNS_16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_j` | `:60` | Alta del datapath: publica MAC, prefijo, unit y medios (`:206`). El camino STA usa el distinto `registerInfraEthernetInterface` (`RTLAirLayer.cpp:142`) | Cambio de firma (nº de colas, flags) o de validaciones (recuento 1..256, TX submission forzada a 1 o 4) | Symtab; desensamblar `registerEthernetInterface`/`initWithQueues` |

### 1.2 Símbolos que resuelve el enlazador por herencia

No van por `__asm`, pero el loader exige cada uno en tiempo de carga; el fallo es de enlace, no de
ejecución.

| Grupo | Símbolos | Referencia | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|
| Metaclass y structores de la base | `__ZN19IO80211AWDLProtocol11gMetaClassE`, `__ZN19IO80211AWDLProtocolC2EP11OSMetaClass`, `__ZN19IO80211AWDLProtocolD2Ev` (los genera `OSDefineMetaClassAndStructors`, `AWDLInterface.cpp:94`) | spec §5.1, [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §2.2 | Renombrado de la clase o cambio de mangleo | Symtab del fileset IO80211Family |
| Llamadas directas a la base | `__ZN23IO80211VirtualInterface4freeEv`, `__ZN23IO80211VirtualInterface5stopEP9IOService` (`AWDLInterface.cpp:283`, `:297`) | código | Ídem | Symtab |
| Implementaciones heredadas que rellenan las celdas 0x428–0xDC0 | ~48 símbolos de `IO80211VirtualInterface` (`AWDLInterface.h:96-145`); en 25.6.0 los 93 símbolos de la clase están exportados con tipo `0f` | [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §5; spec P0.5 | Cualquier celda que pierda implementación en la familia deja `__cxa_pure_virtual` o undefined en la vtable del objeto | `nm -u` del objeto compilado + symtab del fileset (comprobación P0.5) |
| IOKit/xnu | `IOWorkLoop`, `IOLock`, `thread_call`, `PE_parse_boot_argn`, `IOMalloc`/`IOFree` | código | API estables de xnu en 14–26 (riesgo bajo) | Cabeceras del SDK de cada versión |

## 2. Offsets y tamaños fijados

### 2.1 `RegistrationInfo` de `awdl0`

Struct de `IOSkywalkEthernetInterface` que el código rellena byte a byte. Los offsets son el
contrato implícito con `initRegistrationInfo`, `registerEthernetInterface` y `copyRegistrationInfo`.

| Campo | Offset/valor | Código | Justificación | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|---|
| Tamaño total | `0x130` | `AWDLInterface.cpp:78` | spec §5.3; [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2, §3.4 | El struct puede crecer o encoger: la validación interna dejaría de pasar | Desensamblar `initRegistrationInfo`: literales de versión (1) y size |
| Versión | `1` (3.er argumento) | `:188` | spec §5.3 | El validador puede exigir otra versión | Ídem |
| Subfamilia | `+0x0C = 3` (`IFNET_SUBFAMILY_WIFI`) | `:79`, `:189` | spec §5.3 | Desplazamiento del campo o semántica nueva | Desensamblar `initRegistrationInfo`/`copyRegistrationInfo` (lecturas) |
| Rol | `+0x10 = 6` | `:80`, `:190` | spec §5.3; sin lector en 25.6.0 (`?`, [`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §3) | Campo sin lector hoy: valor inocuo, pero una versión nueva puede empezar a leerlo con otra semántica | Barrido de lectores de `+0x10` en el fileset nuevo |
| Nº de palabras de medios | `+0x14 = 2` | `:81`, `:191` | spec §5.3 (camino STA; `initRegistrationInfo` escribe 256 en 25.6.0) | Desplazamiento | Desensamblar `initRegistrationInfo` (la escritura 256 delata el campo) |
| `ifm_active` | `+0x24 = 0x80` (`IFM_IEEE80211`) | `:82`, `:192` | spec §5.3; nacimiento §2.2 | Desplazamiento | Desensamblar la siembra del descriptor de medios (`0xffffff8002b0a32a` en 25.6.0, por símbolo) |
| `ifm_current` | `+0x28 = 0x80` | `:83`, `:193` | spec §5.3 (D5; nacimiento §6.1–§6.3); el filtro del daemon exige `(ifm_current & 0xE0) == 0x80`; el actualizador intermedio no está identificado (`?`) | Desplazamiento del campo, o un escritor nuevo que reemplace la semilla antes de que la lea el daemon | Desensamblar la siembra y `ioctl_sifmedia`; comprobar en el sistema nuevo quién reescribe `+0x28` entre el alta y la lectura |
| Prefijo BSD | `+0x30 = &"awdl"` | `:84`, `:194` | spec §5.3; nacimiento §3.1 | Desplazamiento; el literal debe apuntar a memoria viva mientras exista el ifnet | Desensamblar `registerNetworkInterface` (publica `IOInterfaceNamePrefix`) |
| Unit | `+0x38 = 0` | `:85`, `:195` | spec §5.3; nacimiento §3.1 | Desplazamiento | Ídem (publica `IOInterfaceUnit`) |
| Headroom | `+0x3C = 64` (u16) | `:86`, `:196` | spec §5.3; [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §6.1 (`createProxInterface` escribe 64 en `+0x3C`; `+0x3A` es la mitad alta del unit). Lector `getTxDataOffset` (verificado en 25.6.0) | Desplazamiento; el valor 64 es precedente Broadcom, no un requisito leído por el kernel | Lector identificado en 25.6.0: `IOSkywalkNetworkInterface::getTxDataOffset` lee `u16 [copia+0x3C]` y el cliente BSD lo guarda en `+0x13C` (`AWDL_STAGE1_PENDING_CHECKS.md` §1); en un kernelcache nuevo, desensamblar `getTxDataOffset` por símbolo |
| Flags | `+0x40 = 0` | `:87`, `:197` | spec §5.3; sin lector en 25.6.0 (`?`, REGINFO_AND_GATES §2) | Ídem que `+0x10` | Barrido de lectores |
| MTU / MTU máx. | `+0x48`/`+0x4C` = `0x594` (1428) | `:88`, `:198` | spec §5.3; nacimiento §2.4 paso 4 (`0xffffff80017296ff`) | Es un valor propio del driver (precedente Broadcom); el kernel no lo fija | Desensamblar el camino del 94 de Broadcom; `ifconfig awdl0` en el sistema nuevo |
| MAC | `+0x108`, 6 B | `:89`, `:199` | spec §5.3; nacimiento §2.2; [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2 | Desplazamiento | Desensamblar `copyRegistrationInfo` (publica `IOMACAddress`) |

### 2.2 `apple80211_virt_if_create_data` (payload del 94)

Declaración en `AWDLInterface.h:74-81`; el código la consume en `RTLAirLayer.cpp:577-588`.

| Campo | Offset | Código | Justificación | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|---|
| `f0` (u32, sin leer) | `+0x00` | `AWDLInterface.h:75` | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2; lectura `?` | wifip2pd puede añadir campos delante y desplazar todo | Desensamblar el handler del 94 y el emisor de wifip2pd (userspace, no kernelcache) |
| `mac[6]` | `+0x04` | `AWDLInterface.h:76`; `RTLAirLayer.cpp:588` | §3.2 | Ídem | Ídem |
| `pad0[2]` | `+0x0A` | `AWDLInterface.h:77` | §3.2 | Ídem | Ídem |
| `role` (u32) | `+0x0C` | `AWDLInterface.h:78`; `RTLAirLayer.cpp:582` | §3.2 | Ídem | Ídem |
| `ifname[16]` (salida) | `+0x10` | `AWDLInterface.h:79`; `RTLAirLayer.cpp:588` | §3.2; el nombre lo escribe el handler con `strlcpy(payload+16, getBSDName(), 0x10)` | Ídem | Ídem |
| Tamaño `32` B | `static_assert` | `AWDLInterface.h:81` | §3.2; el handler de la familia exige `len == 32` y la vía legacy copia `min(len, 32)` (spec §4.1) | El handler puede exigir otra longitud | Desensamblar el handler del 94: literales de longitud (32 en 25.6.0) en ambas tablas de dispatch |

### 2.3 Vtables

Convención: offset desde el vptr (`vptr = ZTV + 0x10`). El anclaje completo está en
`AWDLInterface.h:3-19` y spec §5.2; los `static_assert` de `AWDLInterface.h:287-289` sólo protegen
la compilación contra la cabecera 25.6.0, no contra un kernel nuevo.

| Bloque | Offsets | Código | Justificación | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|---|
| Base declarada (`IO80211SkywalkInterface`) | hasta `0xE60` | `Source/IO80211/IO80211_25_6_0.h` (heredado) | spec §5.2 | Un método nuevo en la familia desplaza todo lo que viene después | Volcar `__ZTV23IO80211SkywalkInterface` del fileset nuevo |
| 27 métodos de `IO80211VirtualInterface` (`init` a `freePendingPackets`) | `0xE68`–`0xF38` | `AWDLInterface.h:149-180` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) filas 463–489; contrastada celda a celda con `__ZTV23IO80211VirtualInterface` en 25.6.0 (0 discrepancias, `AWDL_STAGE1_CODE_REVIEW_IFACE.md`) | Inserción o borrado de métodos entre `syncDPSStats` y `setMacAddress` desplaza los 63 slots AWDL | Volcado de `__ZTV23IO80211VirtualInterface`: contar 27 celdas entre `syncDPSStats` y `setMacAddress` |
| `setMacAddress` (puro) | `0xF40` | `AWDLInterface.h:181` | [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) fila 490 | Puede dejar de ser puro o moverse | En el volcado ZTV, la celda debe apuntar a `__cxa_pure_virtual` |
| 16 reservadas | `0xF48`–`0xFC0` | `AWDLInterface.h:182-197` | VTABLE_SLOTS filas 491–506 | La familia puede consumir reservadas con métodos reales | Volcado ZTV |
| 63 slots puros AWDL | `0xFC8`–`0x11B8` | `AWDLInterface.h:206-284` | PURE_SLOTS; spec §16. Contrastados por nombre y posición con `__ZTV30AppleBCMWLANProximityInterface` en 25.6.0: 63 de 63 sin discrepancias, incluidas las cuatro últimas (`0x11A0`–`0x11B8`) | Cambio del conjunto puro o del orden | Volcado de la vtable de una clase concreta que los implemente (en 25.6.0 `AppleBCMWLANProximityInterface`) en `0xFC8`–`0x11B8`; las 63 deben coincidir por nombre con la cabecera |
| 48 overrides de alineación | `0x428`–`0xDC0` | `AWDLInterface.h:96-145` | [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §5 | Cada desplazamiento aguas arriba rompe todas las celdas siguientes: el objeto nacería con una vtable que no es la de la familia | `nm -u` del objeto + volcado ZTV (P0.4/P0.5); `createEventPipe` debe seguir en `0xDC0` (slot 440; `+0x1B8` es offset interno del pipe, no slot — HANDOFF §3.3) |
| Tamaños de clase | `0x128`/`0x130`/`0x130`, pad `0x8` | `AWDLInterface.h:199`, `:287-289` | Metaclass 0x130 verificada contra el kernel collection (cabecera `:16`) | Campos ivar nuevos en la familia agrandan el objeto: el pad y los asserts dejan de reflejar la realidad | El literal de size que pasa el constructor de `OSMetaClass` de `IO80211VirtualInterface`/`IO80211AWDLProtocol` en el fileset nuevo |
| Slot del 94 en `IO80211InfraProtocol` | `+0x1160` (slot 556) | `RTLAirLayer.hpp:99-100`; `RTLAirLayer.cpp:577` | `IO80211_25_6_0.h:685`; el puente castea a `IO80211InfraProtocol` y llama `+0x1160` (HANDOFF §3.2) | Un método nuevo en la jerarquía infra desplaza el slot: el cast aterrizaría en el método equivocado | Volcado ZTV de la jerarquía infra + desensamblar el puente `apple80211setVIRTUAL_IF_CREATE` (por símbolo) |

### 2.4 Colas y pool

| Supuesto | Valor | Código | Justificación | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|---|
| Layout de `IOSkywalkPacketBufferPool::PoolOptions` | 8 campos, 40 B (`rtl_awdl_pool_options`) | `AWDLInterface.cpp:65-74` | Paridad con el camino STA (`RTLAirLayer.cpp:196-204`); sin documento dedicado (`?`) | Struct interno de la familia: campos nuevos lo agrandan y `withName` puede leer otros | Desensamblar `withName`/`initWithPool`: qué offsets del options se leen |
| Tamaños del pool | 256/256/2048/1, `poolFlags = 1` | `:142-148` | Camino STA + spec §5.4; `poolFlags = 1` = nosleep (`?`) | Presupuesto de memoria; el flag puede cambiar de significado | Desensamblar el cliente del pool (mapeo flag→`kern_pbufpool` flags) |
| Capacidad de cola y lote | 128; lotes de 64 | `:155-158`, `:261-267` | [`AWDL_STAGE1_QUEUE_SHAPE.md`](AWDL_STAGE1_QUEUE_SHAPE.md) §1/§5/§10 (spec §5.4); `enqueuePackets` exige `count < capacidad` | Límites validados distintos | Desensamblar `enqueuePackets` y `initWithQueues` |
| Forma de colas | 1 TX submission + 1 TX completion + 1 RX completion | `:205-206` | QUEUE_SHAPE: mínimo que la familia acepta; el recuento RX no se valida y nada mira el rol (`?` no probado en hardware, spec §15.6) | La familia puede volver a exigir 4 TX (WMM) o validar por rol | Desensamblar `registerEthernetInterface`/`initWithQueues` |
| Acción no nula en TX submission | `txNoop` obligatorio | `:155` | [`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md) §1 (`initWithPool` `0xffffff8002b0caac` la rechaza) | La validación puede desaparecer (inocuo) o endurecerse | Desensamblar `initWithPool` por símbolo |
| Colas nacen deshabilitadas | `enable()`/`disable()` las conmutan | `:300-331` | `IOSkywalkPacketQueue::initWithPool` limpia `enabled` y `packetSubmission` rechaza cola deshabilitada (commits `4569206`, `e50d7cf`; spec §5.4) | La semántica del flag puede invertirse o el nacimiento puede cambiar a habilitado | Desensamblar `IOSkywalkPacketQueue::initWithPool` |
| `registerService(kIOServiceSynchronous)` | síncrono dentro del handler | `:215` | spec §5.5 (D4, condicional `?`: el gate del 94 puede ser el mismo work loop y bloquear) | El flag IOKit es estable; lo que puede cambiar es el gate bajo el que corre el 94 | Identificar el gate del 94 en el kc nuevo ([`AWDL_STAGE1_REGINFO_AND_GATES.md`](AWDL_STAGE1_REGINFO_AND_GATES.md) §5–§6); en hardware, T2.2 mide el retorno |

### 2.5 `apple80211_capability_data` (puerta del tipo 12)

| Campo | Offset/valor | Código | Justificación | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|---|
| Bit AWDL | byte `+0x08` bit 0 (`p[8]`), con el bit compañero `+0x07` bit 0 | `RTLAirLayer.cpp:1883-1884` | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1/§7; spec §3; el copyback es `min(req_len, 21)` desde `+0x04`, así que `data[4]` = `+0x08` | El copyback, el tamaño (21/28 B) o el bit probado por el daemon pueden moverse | Desensamblar el handler del tipo 12 (copyback) en el kc; el bit probado vive en wifip2pd (binario de cada versión, no kernelcache) |
| Puerta NAN cerrada | byte `+0x0D` bit 0 a 0 | `RTLAirLayer.cpp:1885-1887` (sin acción sobre `p[13]` bit 0) | spec §3 | Un bit NAN encendido cambia la rama de wifip2pd | Ídem |

## 3. Códigos de request, roles, ioctls y boot-args

| Valor | Significado | Código | Justificación | Cómo puede cambiar | Verificación más barata |
|---|---|---|---|---|---|
| Request 94 `VIRTUAL_IF_CREATE` (RawSet, entrada 93 de la tabla set) | Creación de la interfaz virtual | `RTLAirLayer.cpp:577` (override), `:1453` | spec §4.1; [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2; [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4 | Renumeración, retirada o cambio de payload | Localizar `apple80211RouteExternal` (tablas legacy y moderna) y la entrada 93; symtab de `setVirtualInterfaceCreate` |
| Request 12 `CARD_CAPABILITIES` (RawGet) | Puerta de capacidad que dispara el 94 | `RTLAirLayer.cpp:1863-1892` | spec §3 | Ídem | Tabla get, entrada 11 |
| Request 97 `VIRTUAL_IF_PARENT` (RawGet, entrada 96) | El daemon pregunta el padre tras crear `awdl0`; lo atiende la familia, sin código propio | sin código (spec §7) | spec §7.1–§7.2 | Ídem; si falla, la señal del semáforo del daemon queda en `?` (spec §7.3) | Tabla get, entrada 96 |
| Request 293 companion `llw0` (RawSet) | Deliberadamente sin respuesta: el dispatch devuelve 102 y el daemon lo absorbe | sin código (D6) | spec §8; [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4 | Si el daemon deja de tolerar el 102, la etapa 4 tiene que crear `llw0` | Tabla set, entrada 292; log del daemon en el sistema nuevo |
| Rol 6 = AWDL (`AirLink`) | Valor enviado a `rtl_vif_init` y al `RegInfo`, y exigido al payload | `AWDLInterface.cpp:92`, `:114`, `:190`; `RTLAirLayer.cpp:582` | [`AWDL_KERNEL_ROLE_TABLE.md`](AWDL_KERNEL_ROLE_TABLE.md); spec §4.1 | La tabla rol→nombre del kernel puede renumerar o renombrar | Desensamblar `IO80211VirtualInterface::init` → `setInterfaceRole` y su tabla; `strings` del fileset para `"AirLink"` |
| Rol 7 → 102 (APSTA); roles 8–10 NAN (Broadcom); resto → `0xE0000001` | Rechazo ordenado de roles no AWDL | `RTLAirLayer.cpp:584` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1; spec §4.3 | Ídem | Ídem |
| Id de interfaz 2 | `AWDL_IFACE_ID`, vía `rtl_sky_setId` | `AWDLInterface.cpp:91`, `:118` | nacimiento §2.3 (Broadcom: 2 si la versión del core ≥ 5); la familia no lo valida (`?`) | Convención Broadcom, no un requisito del kernel | Desensamblar el `setInterfaceId` que hace Broadcom en el camino del 94 |
| Unit 0 + prefijo `"awdl"` → `awdl0` | Nombre final del ifnet | `AWDLInterface.cpp:194-195` | nacimiento §3.1 | Colisión de nombres si otra pila ocupa el prefijo | `ifconfig`/`ioreg` en el sistema nuevo |
| Medios `0x80` (`IFM_IEEE80211`) y filtro `(ifm_current & 0xE0) == 0x80` | Lo que el descriptor de medios debe reportar para que el daemon acepte la interfaz | `AWDLInterface.cpp:192-193` | nacimiento §6.1–§6.3; el filtro es de wifip2pd | `IFM_IEEE80211` es de `<net/if_media.h>` (estable); el filtro vive en wifip2pd | `SIOCGIFMEDIA` sobre `awdl0`; desensamblar el filtro en el binario de wifip2pd de cada versión |
| Ioctls `SIOCGIFMEDIA` `0xC02C6938`, `OSIOCGIFMEDIA` `0xC0286938`, `SIOCSIFMEDIA` `0xC0206937` | El código no los emite; depende de que `ioctl_sifmedia` siga fallando antes del store en `awdl0`, para que `0x80` sobreviva | sin código | spec §6.3; [`AWDL_STAGE1_SIOCSIFMEDIA.md`](AWDL_STAGE1_SIOCSIFMEDIA.md) §5/§7 | Los números son BSD-estables; el comportamiento de `ioctl_sifmedia` (fallo sin `IOMediumDictionary`) puede cambiar | `ioctl` de prueba sobre `awdl0`; desensamblar `ioctl_sifmedia` |
| Entitlement `com.apple.wifi_awdl.event_monitor` | Puerta de la familia en `createEventPipe` (`0xE00002E2` sin él); el registro del monitoreo del daemon depende de que la vía devuelva 0 | sin código | spec §9; [`AWDL_STAGE1_DISPATCH_PATH.md`](AWDL_STAGE1_DISPATCH_PATH.md) §2 | Cambio de string o de puerta | `strings` del fileset IO80211Family; desensamblar `createEventPipe` |
| `EEXIST` (17), `0xE0000001`, `0xE00002BC`, `kIOReturnUnsupported`, `kIOReturnBadArgument` | Códigos de retorno del handler | `RTLAirLayer.cpp:584`, `:1790`, `:1803`; spec §4.3, §11 | Los `kIOReturn*` son IOKit (estables); `EEXIST` es errno (estable); `0xE0000001` es convención Broadcom, no un valor del sistema | Bajo; un cambio sólo afectaría al parseo del daemon | — |
| Boot-arg `rtlawdl` | Arma la vía AWDL (inerte sin él) | `RTLAirLayer.cpp:1453-1457` | spec §2 D7 | Nombre propio del kext; no depende del SO. `PE_parse_boot_argn` es KPI estable | Código |
| Boot-args `rtlair`/`rtlairstep`/`rtlairtrust`/`rtlairnogate` | Precondición: sin `rtlair` el controlador no arranca y el 94 no existe | `RTLAirLayer.cpp:1444-1452`; `VersionGate.h` | spec §14; práctica previa del layer | Ídem | Código |

## 4. Notas de alcance

- Los `static_assert` de `AWDLInterface.h` (`:81`, `:287-289`, `:405-406`) sólo contrastan contra
  la cabecera 25.6.0; contra un kernel nuevo valen exclusivamente las comprobaciones de las
  columnas derechas.
- Los constantes que viven en wifip2pd (bit de capacidad probado, filtro de medios, secuencia
  post-create, tolerancia al 102 del 293) no se comprueban en un kernelcache: requieren el binario
  del daemon de cada versión.
- Abiertos que arrastran los fuentes (`?`): lectura de `RegInfo+0x10` y `+0x40`, consumidor del
  campo `ifnet` que recibe el headroom desde `clienteBSD+0x13C`, registro de
  una subclase de terceros bajo `IO80211AWDLProtocol` (spec §15.10), forma de 3 colas sin probar
  en hardware (spec §15.6), bloqueo del `registerService` síncrono bajo el gate del 94 (D4),
  campo `f0` del payload del 94, aceptación del event pipe para una interfaz de driver externo
  (spec §9).
- El `?` de `IO80211VirtualInterface::init` en la selección de símbolos de
  [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2 está cerrado para 25.6.0 (tipo `0f`
  confirmado, spec §15.8); en un kernelcache nuevo vuelve a ser la primera comprobación de la
  lista: symtab del fileset antes de compilar.
