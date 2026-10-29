# errno del descarte de TX y estado de enlace visto por `wifip2pd`

Cierra las dos primeras subpreguntas del punto 10 de `AWDL_INDICE.md` §8.2: el errno que la pila ve cuando
`packetSubmission` devuelve `0xE00002D8` (cola deshabilitada) y el consumidor de `state+113`/`if_link_state`
en `wifip2pd`. Las otras dos subpreguntas del punto 10 (llamada del `ifclassq` a `tx_push` y vía de alta del
`netif_llink`) se reevalúan en §7. Base: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64) y el
binario `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`, el mismo de los documentos
`AWDL_WIFIP2PD_*`), ambos analizados headless (`idat -A -S`) sobre copias de las bases de datos anotadas
(`boot_awdl.kc.i64` y `wifip2pd.i64`). Las direcciones del kernel son VA; las del daemon son sus
direcciones de imagen. Cada dirección citada como cabeza se comprobó con
`ida_funcs.get_func(ea).start_ea == ea`; las citas de instrucción son líneas del desensamblado o de la
decompilación. Los nombres de constantes IOReturn/errno/flags se toman de las cabeceras del SDK
(`IOReturn.h`, `sys/errno.h`, `net/if.h`). Lo que el análisis no fija va con `?`.

## 1. Respuesta

1. **`errnoFromReturn` da 16 (`EBUSY`) para ese código.** `IOService::errnoFromReturn` (`0xffffff8000a95ab0`) mapea
   `0xE00002D8` (`kIOReturnNotReady`) al errno 16 (`EBUSY`): el caso está en el grupo cuyo manejador
   ejecuta `mov eax, 10h` (`0xffffff8000a95ae9`). La conversión la aplica `nxp_queue_tx_push`, que
   convierte el retorno del slot `+0x1D0` con la celda `+0x788` de la vtable (`jmp rcx` en
   `0xffffff8002aff52a`). `?` Qué llamador ve ese errno: `nxp_queue_tx_push` sólo se alcanza por el camino de
   puerto virtual (`netif_vp_na_txsync`), no por el `ifclassq` del envío BSD; el tramo de §3 desde
   `sub_FFFFFF80005E6110` hacia arriba no está demostrado (§3).
2. **wifip2pd no consume ni el `if_link_state` de notificación ni `state+113`.** El descubridor de
   interfaces (`sub_100043720`) sólo lee `IOInterfaceName`, la propiedad de rol y el índice BSD; no hay
   ningún consumidor del evento `APPLE80211_M_LINK_CHANGED` (id 4, ausente de la tabla registrada) ni de
   mensajes de ruta que porte estado de enlace. Lo único que se le aproxima es un lector del estado
   `IFF_UP|IFF_RUNNING` de la entrada BSD por nombre de interfaz vía `getifaddrs` (`sub_100056790`,
   predicado `(~ifa_flags & 0x41) == 0` sobre la entrada `AF_LINK`), expuesto como propiedad Swift;
   `?` el extremo de esa cadena (los thunks se referencian desde datos de keypath, no desde código) no se
   ha recorrido hasta el punto de decisión (§6).
3. **Todo el «link state» que wifip2pd produce es modelo propio.** Los literales de cambio de enlace
   («WiFi powered on, setting link up on %s», «WiFiAwareStateHandler link state change to %s») se emiten
   desde el estado de alimentación WiFi y de `nw_path` de Network.framework, no desde el `if_link_state`
   del sistema (§6.3).

## 2. `errnoFromReturn`

La función es `__ZN9IOService15errnoFromReturnEi`, cabeza `0xffffff8000a95ab0`, fin
`0xffffff8000a95b66`. Es un método virtual de `IOService`: la celda `+0x788` de la vtable vale
`0xffffff8000a95ab0` tanto en `__ZTV9IOService` (`0xffffff80002ad4e0`; qword en `0xffffff80002adc78`,
es decir ZTV+0x798 = vptr+0x788) como en `__ZTV25IOSkywalkNetworkInterface`
(`0xffffff8002b3d2b8`, misma celda). Estructura verificada:

1. Pase directo de la banda errno: `mov eax, esi` (`0xffffff8000a95ab4`), `and eax, 3FFFh`
   (`0xffffff8000a95ab6`), `lea ecx, [rax+0C000h]` (`0xffffff8000a95abb`), `cmp ecx, esi`
   (`0xffffff8000a95ac1`); si son iguales devuelve `rtn & 0x3FFF`. Es decir, los valores
   `0xC000..0xFFFF` devuelven `rtn - 0xC000` sin tabla.
2. Resto: switch de 49 casos con sesgo: `lea eax, [rsi+1FFFFD43h]` (`0xffffff8000a95ac7`),
   `cmp eax, 30h` / `ja default` (`0xffffff8000a95acd`/`0xffffff8000a95ad0`), tabla de saltos
   `jpt_FFFFFF8000A95AE0` en `0xffffff8000a95b68`, `jmp rax` (`0xffffff8000a95ae0`). Los índices
   válidos cubren los IOReturn comunes `0xE00002BD`-`0xE00002ED`.
3. **Caso `0xE00002D8`** (decimal con signo `-536870184`): manejador `0xffffff8000a95ae9`,
   `mov eax, 10h; pop rbp; retn` → **16 = `EBUSY`**. El mismo grupo cubre `-536870187`
   (`0xE00002D5`, `kIOReturnBusy`), `-536870185` (`0xE00002D7`), `-536870165` y `-536870163`.
4. Default (`0xffffff8000a95b0c`): `mov eax, esi; test esi, esi; jz …; mov eax, 5` — 0 si el argumento
   es 0, si no **5 = `EIO`**.

Contrastes que confirman la lectura de la tabla (mismo switch, casos vecinos):

| IOReturn | Constante (`IOReturn.h`) | Caso (decimal) | Retorno | Manejador |
|---|---|---:|---|---|
| `0xE00002D8` | `kIOReturnNotReady` | `-536870184` | 16 `EBUSY` | `0xffffff8000a95ae9` (`mov eax, 10h`) |
| `0xE00002D5` | `kIOReturnBusy` | `-536870187` | 16 `EBUSY` | mismo grupo |
| `0xE00002DB` | `kIOReturnNoSpace` | `-536870181` | 28 `ENOSPC` | `0xffffff8000a95b3c` (`mov eax, 1Ch`) |
| `0xE00002C2` | `kIOReturnBadArgument` | `-536870206` | 22 `EINVAL` | `0xffffff8000a95af7` (`mov eax, 16h`) |
| `0xE00002E2` | `kIOReturnNotPermitted` | `-536870174` | 1 `EPERM` | `0xffffff8000a95b20` (`mov eax, 1`) |
| `0xE00002D6` | `kIOReturnTimeout` | `-536870186` | 60 `ETIMEDOUT` | `0xffffff8000a95b5f` (`mov eax, 3Ch`) |

Los tres primeros son los fallos que documenta `AWDL_STAGE1_IFUP_PATH.md` §5 para las colas; el de
`kIOReturnNotPermitted` es el que devuelve `createEventPipe` sin entitlement (`AWDL_STAGE1_DISPATCH_PATH.md`).

## 3. La cadena que convierte el error

| Paso | VA | Instrucción o hecho |
|---|---|---|
| `packetSubmission` rechaza la cola | `0xffffff8002b2cb3f` | `test byte ptr [rdi+28h], 1`; `jz 0xffffff8002b2cd5c` (`0xffffff8002b2cb43`) |
| carga del código de error | `0xffffff8002b2cd5c` | `mov r14d, 0E00002D8h`; `jmp 0xffffff8002b2cc3b` (epílogo, `0xffffff8002b2cd62`) |
| `nxp_queue_tx_push` llama al slot 58 | `0xffffff8002aff506` | `call qword ptr [rax+1D0h]` con `rax = [r12]` (vptr de la cola); cabeza de la función `__ZL17nxp_queue_tx_pushP19kern_nexus_providerP10kern_nexusPvPyPjS5_` en `0xffffff8002aff4d1` |
| conversión con la celda `+0x788` | `0xffffff8002aff50c`-`0xffffff8002aff52a` | `mov rcx, [r13+0]`; `mov rcx, [rcx+788h]`; `mov esi, eax`; `jmp rcx` — tail call a `errnoFromReturn` con `(contexto, IOReturn)` |
| registro de `tx_push` en el proveedor | `0xffffff8002aff1cc` | `mov [rax+58h], r8` tras `lea r8, nxp_queue_tx_push` (`0xffffff8002aff1c5`), dentro de la tabla pasada a `IOSkywalkBSDClient::registerNexusControllerProvider` (`0xffffff8002aff1d9`) |

Las filas anteriores están verificadas hasta la conversión en `nxp_queue_tx_push`. Lo que une esa conversión
con `sub_FFFFFF80005E6110` (`ifnet_enqueue`) **no** está demostrado: la operación de encolado del
`ifclassq` (`call qword ptr [r10+18h]`, `0xffffff80005e6172`) apunta a `fq_if_enqueue`, no a la tabla del
proveedor, de modo que el errno 16 no sube por esa vía. Resultado verificado: `errnoFromReturn` convierte
`0xE00002D8` en **16 (`EBUSY`)**; `?` el sitio que lo recibe cuando la cola está deshabilitada.

## 4. `state+113` en el kernel

El escritor es `IO80211SkywalkInterface::setInterfaceEnable`
(`__ZN23IO80211SkywalkInterface18setInterfaceEnableEb`, cabeza `0xffffff80022f99ce`):

- `mov rax, [rdi+120h]` (`0xffffff80022f99e9`) y `mov [rax+71h], r12b` (`0xffffff80022f99f0`):
  escribe el byte **113 (0x71)** del objeto de estado en el ivar `+0x120` con el argumento booleano.
- `IO80211VirtualInterface::setInterfaceEnable` (`0xffffff8002331ba6`) lo invoca en
  `0xffffff8002331bf0`, y después de los BPF termina con la llamada por slot `+0xE70`
  (`call qword ptr [rax+0E70h]`, `0xffffff8002331c5e`) que `AWDL_STAGE1_IFUP_PATH.md` §2.1 atribuye a
  `setLinkState` (nombre del slot `?` en este análisis).
- La misma función llama a `IO80211MacAddressAgent::setInterfaceEnable(bool)`
  (`0xffffff80022f9a33`) y crea los IOReporters tras `checkPropertyExists("IOInterfaceName")`
  (`0xffffff80022f9a5c`, `0xffffff80022f9a68`).

`state+113` es, por tanto, un ivar de kernel sin vía directa a espacio de usuario; el equivalente visible
del lado BSD es el estado de la interfaz (`IFF_RUNNING`, `if_link_state`). `?` Los lectores de
`state+113` dentro del kernel siguen sin buscar (mantiene el `?` de `AWDL_STAGE1_IFUP_PATH.md` §2.1).

## 5. wifip2pd: búsquedas ejecutadas

Sobre `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), copia de la base `wifip2pd.i64`:

| Canal | Búsqueda | Resultado |
|---|---|---|
| eventos Apple80211 | dwords de la tabla de registro de `sub_100041B60` (`0x100515ca0`) | `1, 0xb, 0x3b, 2, 3, 0x7d, 0x7e, 0x7f, …` (los 16 ids de `AWDL_WIFIP2PD_DISCOVERY.md` §Pendiente 3). **No contiene el id 4**; el id 1 es `APPLE80211_M_POWER_CHANGED` según la tabla de `AWDL_EVENTS.md` §3.1 |
| cadenas | 128 coincidencias de `link/iff_/io80211/iointerface/bsdname/ifindex/operational/sioc/route` | ninguna corresponde a lectura del estado de enlace del sistema; las de enlace son del modelo propio (§6.3) |
| nombres | 324 coincidencias de `link/monitor` | tipos Swift `WiFiInterfaceLinkState`, `LinkStatsUpdate`, monitores de red/pasarela; sin símbolo de `if_link_state` |
| imports | `getifaddrs` (8 llamadas en 7 funciones), `sysctl` (`sub_1001CEB40`, `sub_1001D1930`), `socket` (sólo el envoltorio `sub_1001CFA90`), `IORegistryEntryCreateCFProperty` (sólo el descubridor `sub_100043720` y `sub_100045520`), `IOServiceAddMatchingNotification` (`sub_100042BB0`) | detallados en §6 |
| notificaciones | `IOServiceAddInterestNotification`, `CFNotificationCenterAddObserver`, `IORegistryEntrySearchCFProperty` | **no importadas** |
| `notify_register_dispatch` | 6 llamadas (`sub_10018AFD0` ×2, `sub_10018B4E0`, `sub_1002D5F80`, `sub_100309050`, `sub_100435C80`) | notificaciones Darwin de propósito `?`; ninguna asociada a interfaz en las cadenas circundantes |
| descubridor | referencias de cadena de `sub_100043720` (`0x100043720`-`0x1000454c8`) | 11 en total: `IOInterfaceName`, `IO80211VirtualInterfaceRole`, `IO80211InterfaceRole`, el formato MAC y literales de log; **ninguna de estado de enlace** — coherente con `AWDL_WIFIP2PD_DISCOVERY.md` |
| sysctl | MIB construidos en pila | `sub_1001CEB40`: `{4,17,0,0,2,4}`; `sub_1001D1930`: `{4,17,0,0,1,0}` — familia `CTL_NET`/`PF_ROUTE` (sysctl de tabla de rutas, comentada como tal en `sys/socket.h`); nombres exactos `NET_RT_*` `?`. Son volcados de rutas (el `GatewayMonitor` lleva el ivar `routingSocket`, `0x100548d88`), no estado de enlace |

## 6. wifip2pd: hallazgos

### 6.1 El lector de `IFF_UP|IFF_RUNNING` por nombre

`sub_100056790`: recorre `getifaddrs`, compara cada `ifa_name` con un `String` Swift capturado en el
contexto (`+16`/`+24`) y, sobre la entrada con `ifa_addr->sa_family == 18` (`AF_LINK`), devuelve
`(~ifa_flags & 0x41) == 0` — es decir, exige `IFF_UP (0x1)` e `IFF_RUNNING (0x40)` simultáneos
(valores de `net/if.h`). Devuelve 2 si no encuentra la entrada o `getifaddrs` falla. El mismo predicado
aparece en la closure genérica `sub_100056970` (`(~*(u8 *)(a1+16) & 0x41) == 0`), que el generic runner
`sub_100046B00` ejecuta con familia `18` y metadatos del enum **`WiFiInterfaceLinkState`**
(`sub_1000568D0`): ese enum se computa a partir del estado de flags de la entrada BSD.

El runner genérico `sub_100046B00` también lo usan el descubridor (`0x100043d44`) y los sitios de
`if_nametoindex` (`0x10004b722`, `0x10004b9fd`), todos con nombre de interfaz capturado.

### 6.2 La cadena de keypath, sin extremo cerrado

Los thunks concretos `sub_10005F690` (estado), `sub_10005F6B0` (escritor de flags), `sub_10005F6D0`
(MAC), `sub_10005F730` (IPv4) y `sub_10005F790` (IPv6) sólo se referencian desde **datos** de keypath
Swift: `0x100517ba0`/`0x10052cb58` para el primero y `0x100517ba8`/`0x10052cb60` para el segundo. El
escritor genérico (`sub_100056B90`, equivalente `sub_100056990`) reconstruye un valor de flags
`flags & 0xFFBE` y le suma `0x41` o `0` según un booleano, y lo publica por el keypath `0x10045FE30`:
flags sintetizados, no leídos del sistema en ese punto. `?` El tipo concreto que evalúa esos keypath y
el lugar de decisión que consume el predicado de §6.1 no se han recorrido: los referenciadores son
estructuras de keypath, no llamadas directas, y la cadena Swift se pierde en despacho genérico.

### 6.3 Los «link state» que produce son modelo propio

| Literal | VA | Función | Origen del estado |
|---|---|---|---|
| `WiFi powered on, setting link up on %s` / `WiFi powered off, setting link down on %s` | `0x1004a7990` / `0x1004a7900` | `sub_10041B7F0` (`0x10041b8c9` / `0x10041b9ce`) | evento de alimentación WiFi; llama a `sub_10027D480(1/0)` y al forzado de estado (`sub_10040EB90`), no a ninguna lectura del `ifnet` |
| `WiFiAwareStateHandler link state change to %s for interface %s with MAC %s` (`Link Up`/`Link Dow[n]`) | `0x1004a3620` | `sub_1003BB3E0` (`0x1003bb53e`) | el estado y el MAC llegan como parámetros del llamador (`sub_1003C2770`, `sub_1003BB2F0`); es notificación del modelo interno |
| `Path became %s with ipv4: %s, ipv6: %s` + `setLinkEventWithIntegerReason:…forInterface:` | `selRef 0x100544060` | `sub_1000506A0` (`0x1000512a0`, `0x100051356`) | `nw_path_get_status` de Network.framework sobre la interfaz de la sesión (`NetworkConnectivityMonitor` de `InternetSharingRequesterSession`); usa `getifaddrs` sólo para resolver direcciones IPv4/IPv6 del nombre y publica el evento con `objc_msgSend(qword_100548580, …)` |
| `Forcing new link state because of mismatch` | `0x1004a6a90` | `sub_10040EB90` (`0x10040eda9`) | conciliación del modelo interno, no lectura del `ifnet` |

Ninguna de estas rutas consume el `if_link_state` de notificación del kernel ni `state+113`.

## 7. Consecuencias para la etapa 1

- Para T3.5: el mapeo `0xE00002D8` → 16 (`EBUSY`) queda fijado en `errnoFromReturn`; el sitio que recibe
  ese errno cuando la cola está deshabilitada es `?` (el camino del `ifclassq` no pasa por
  `nxp_queue_tx_push`, §3). El síntoma esperado (cero TX sin panic) no cambia.
- El riesgo 5 de `AWDL_STAGE1_IFUP_PATH.md` §7 (wifip2pd exigiría enlace activo) queda sin consumidor
  identificado: el descubridor no mira el enlace (§5), no se registra el evento `LINK_CHANGED` (id 4), y
  el único lector del estado de flags BSD es el genérico de §6.1 con su extremo en `?`. Si en hardware el
  daemon tratara `awdl0` como degradada por el enlace, la vía a registrar es la cadena de keypath de §6.2.
- Del punto 10 del índice queda abierto: la vía de alta del `netif_llink` y el tramo del `ifclassq` hasta la
  acción del driver (`AWDL_STAGE1_QSET_STATE.md` §10), que se reevalúan aparte.

## 8. Puntos abiertos

| Punto | Estado |
|---|---|
| Consumidor final del predicado `IFF_UP\|IFF_RUNNING` de `sub_100056790` (tipo y decisión tras los keypath `0x100517ba0`/`0x10052cb58`) | `?` Los referenciadores son datos de keypath Swift; cadena no recorrida hasta el final |
| Propósito de las 6 llamadas a `notify_register_dispatch` | `?` Sin asociación visible a interfaz |
| Destino de `setLinkEventWithIntegerReason:…` (objeto `qword_100548580`, inicializado por `sub_100051680`) | `?` Clase del receptor no identificada |
| Lectores kernel de `state+113` (byte `+0x71` del objeto en ivar `+0x120`) | `?` No buscados; hereda el `?` de `AWDL_STAGE1_IFUP_PATH.md` §2.1 |
| Tramo del `ifclassq` hasta la acción del driver | `?` Sin salto a `tx_push` en esa vía; se reevalúa aparte |
| Vía de alta del `netif_llink` en el camino del kext | `?` Sin cambios (`AWDL_STAGE1_QSET_STATE.md` §10) |

## 9. Verificación de direcciones

Cabezas comprobadas con `ida_funcs.get_func(ea).start_ea == ea` (kernelcache):
`0xffffff8000a95ab0` (`__ZN9IOService15errnoFromReturnEi`), `0xffffff8002aff4d1`
(`__ZL17nxp_queue_tx_pushP19kern_nexus_providerP10kern_nexusPvPyPjS5_`), `0xffffff80005e6110`,
`0xffffff80005e5f40`,
`0xffffff8002b2cb2a` (`packetSubmission`), `0xffffff80022f99ce`
(`__ZN23IO80211SkywalkInterface18setInterfaceEnableEb`), `0xffffff8002331ba6`
(`__ZN23IO80211VirtualInterface18setInterfaceEnableEb`).

Citas de instrucción dentro de una cabeza verificada (kernelcache): `0xffffff8000a95ab4`-`0xffffff8000a95ae0`,
`0xffffff8000a95ae9`, `0xffffff8000a95af7`, `0xffffff8000a95b0c`, `0xffffff8000a95b20`,
`0xffffff8000a95b3c`, `0xffffff8000a95b5f`, `0xffffff8000a95b68` (tabla de saltos, leída como datos),
`0xffffff8002b2cb3f`, `0xffffff8002b2cb43`, `0xffffff8002b2cd5c`, `0xffffff8002b2cd62`,
`0xffffff8002aff506`, `0xffffff8002aff50c`-`0xffffff8002aff52a`, `0xffffff8002aff1c5`,
`0xffffff8002aff1cc`, `0xffffff8002aff1d9`, `0xffffff80005e614d`, `0xffffff80005e6158`,
`0xffffff80005e6172`, `0xffffff80005e6176`, `0xffffff80005e61a2`, `0xffffff80005e61a8`,
`0xffffff80005e6291`, `0xffffff80005e6008`, `0xffffff80022f99e9`, `0xffffff80022f99f0`,
`0xffffff80022f9a33`, `0xffffff80022f9a5c`, `0xffffff80022f9a68`, `0xffffff8002331bf0`,
`0xffffff8002331c5e`.

Celdas de vtable leídas como datos: `0xffffff80002adc78` (`__ZTV9IOService`+0x798) y
`0xffffff8002b3da50` (`__ZTV25IOSkywalkNetworkInterface`+0x798), ambas `0xffffff8000a95ab0`.

Cabezas verificadas en wifip2pd: `sub_100043720`, `sub_10004D460`, `sub_1000506A0`, `sub_100056790`,
`sub_1000568D0`, `sub_100056970`, `sub_100056990`, `sub_100056B90`, `sub_100056D80`, `sub_100056FE0`,
`sub_1000576F0`, `sub_100057830`, `sub_100057B60`, `sub_10005F690`, `sub_10005F6B0`, `sub_10005F6D0`,
`sub_10005F730`, `sub_10005F790`, `sub_100068ED0`, `sub_100069090`, `sub_1001CEB40`, `sub_1001CFA90`,
`sub_1001D1930`, `sub_100286E10`, `sub_1003BB3E0`, `sub_10040EB90`, `sub_1004151A0`, `sub_10041B7F0`.

Referencias de datos citadas: tabla de eventos `0x100515ca0`; ivar `routingSocket` `0x100548d88`;
keypath `0x10045FE30`; referenciadores de thunks `0x100517ba0`, `0x100517ba8`, `0x10052cb58`,
`0x10052cb60`; literales `0x1004a3620`, `0x1004a6a00`, `0x1004a6a90`, `0x1004a7900`, `0x1004a7930`,
`0x1004a7990`; `selRef` `0x100544060` (usado en `0x1000512a0`, `0x100051356`).
