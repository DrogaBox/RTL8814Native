# wifip2pd tras VIRTUAL_IF_CREATE: liberación del semáforo y secuencia completa de peticiones

Cierra los dos pendientes que dejaban `AWDL_WIFIP2PD_REQUESTS.md` (§"Qué falta"), `AWDL_WIFIP2PD_DISCOVERY.md` (§"Pendiente") y `AWDL_ROLE6_RE.md` (§6): qué libera exactamente la espera de 30 s tras el `RawSet` tipo 94, y la lista ordenada de todo lo que `wifip2pd` le pide a la interfaz AWDL una vez que existe. Base: `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), análisis con IDA 9.4 headless (Hex-Rays) sobre copias de `wifip2pd.i64` en `Build/ida_awdl/task_p2p_sequence/`, más lecturas directas del `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`) y de su IDB existente para la parte del kernel. Lo no verificado va como `?`. Las direcciones son VA del binario indicado.

## Parte 1 — Qué libera el semáforo de 30 s

**Resultado: la espera la libera la notificación de IORegistry (`IOServiceMatched`), no un evento Apple80211 del driver.** El evento de la notificación recorre el descubridor `sub_100043720`, y cuando el nuevo servicio casa el rol (string `AirLink` para AWDL) llama a `sub_100046BD0`, que es el método Swift que ejecuta `semaphore.signal()`. La ruta de eventos Apple80211 (`sub_1000685A0` → `sub_100041FE0`) no toca el semáforo en ningún punto (verificado por xrefs: `sub_100046BD0` solo tiene 3 callers, todos dentro de `sub_100043720`).

### 1.1 Registro de la notificación — `sub_100042BB0` (init de `CoreP2P.AppleDevice`, `0x100042bb0`)

| Paso | Dirección | Evidencia |
|---|---|---|
| `IONotificationPortCreate(kIOMainPortDefault)` | `0x10004323a` | resultado guardado en `self+144` (ivar `deviceNotificationPort`, string en `0x1004aa8d0`) |
| `IONotificationPortSetDispatchQueue(port, qword_100556DF0)` | `0x100043275` | la cola global de CoreP2P (la misma que usa `InterfaceHandle` para roles ≠ 1, ver `sub_1000383C0`) |
| `IOServiceAddMatchingNotification(port, "IOServiceMatched", dict, callback, self, &iterador)` | `0x100043314` | literal `"IOServiceMatched"` en `0x100446040`; callback = `callback` (`0x100045500`); iterador guardado en `self+304` (ivar `deviceAddedNotificationIterator`, string en `0x1004aa8f0`) |
| `IOServiceAddMatchingNotification(port, "IOServiceTerminate", dict, sub_100045FC0, self, &iterador)` | `0x1000433a3` | iterador en `self+312` (`deviceRemovedNotificationIterator`, string `0x1004aa8f0`+); solo se registra si el primero devolvió 0 |

El diccionario de matching es un literal de **una sola entrada**: `{"IOPropertyExistsMatch": "IOInterfaceName"}` (construido en `sub_10002E6C0` desde la tabla en `0x1005149A8`: clave = string grande de 21 bytes `"IOPropertyExistsMatch"` (`0x100446022`), valor = small-string `"IOInterfaceName"`). Es decir: la notificación dispara por **cualquier servicio de IORegistry que tenga la propiedad `IOInterfaceName`**; el filtrado fino lo hace el descubridor. El consumidor de `IOServiceTerminate` (`sub_100045520`) limpia el handle del servicio que desaparece y **no señaliza el semáforo**.

Cadena del callback: `callback` (`0x100045500`, 17 bytes) → `sub_100045FE0` (`0x100045fe0`, retain/ejecutar/release) → drenaje del iterador → `sub_100043720(self, iterator)`. El drenaje inicial arranca en `sub_10004A980` (`0x10004abc3`, arranque de `AppleDevice`, que además drena los dos iteradores guardados en `self+152`/`self+156`); `sub_100047660` también re-drena (`0x100047f09`, `0x100047f2c`) antes de crear.

### 1.2 El descubridor `sub_100043720` (`0x100043720`, 0x1da8 bytes) — por servicio nuevo

1. `IOIteratorNext` (`0x100043820`) y lectura de `IOInterfaceName` vía `IORegistryEntryCreateCFProperty` (`0x1000439a7`); si falta → log `"Matched a service but unable to query its interface name"` (`0x100494ac0`).
2. Si el nombre ya está en el set de descubiertos: actualiza el `io_service` del `InterfaceHandle` existente (`IOObjectRetain`/`IOObjectRelease` en `0x100043abd`/`0x100043ab4`) y log `"Already discovered %s"`.
3. Filtro 802.11: `SIOCGIFMEDIA` (`0x100043cbf`) y check `(ifm_current & 0xE0) == 0x80` (`0x100043ccc`) → si no, `"Ignoring %s because its not a WiFi interface"` (`0x100494b00`).
4. Índice BSD por `getifaddrs` con filtro `AF_LINK` (18) (`sub_100046B00` + thunk `sub_100046AE0`, que lee `sdl_index` en `sockaddr_dl+2`); si no hay → `"Ignoring %s because no interface index found"` (`0x100494b30`).
5. Lectura del rol: primero `IO80211VirtualInterfaceRole`, si no `IO80211InterfaceRole` (`0x100043fbe`, `0x10004404b`).
6. Comparación del string de rol con inmediatos small-string verificados en el desensamblado (`0x1000440a6`–`0x1000441db` y `0x10004523a`–`0x100045266`):

| String (inmediato) | Código interno (`v99`/`v248`) |
|---|---:|
| `Infrastructure` (`0x7274736172666E49`/`0xEE00657275746375`) | 0 |
| `AirLink` (`0x6B6E694C726941`/`0xE700000000000000`) | 1 |
| `WiFi-Aware Discovery` (20 chars) y `WiFi-Aware Discovery+Data` (25 chars) | 2 |
| `WiFi-Aware Data` (`0x6177412D69466957`/`0xEF61746144206572`) | 3 |
| `LowLatency` (`0x6E6574614C776F4C`/`0xEA00000000007963`) | 4 (sin handle, solo señal) |
| `SoftAP` (`0x504174666F53`/`0xE600000000000000`) | ignorado |

   El string de rol que produce el kernel para el rol 6 es `AirLink` (cerrado en `AWDL_KERNEL_ROLE_TABLE.md` §2-3), así que **el camino AWDL es el código 1**.

7. Con match 0–3: crea el `InterfaceHandle` con `sub_1000383C0` (`0x100044236`) — `Apple80211Open` (`0x100038459`) + `Apple80211BindToInterfaceWithService` (`0x100038543`, args: handle, índice BSD, nombre como `NSString`, 0) + logger con subsistema `com.apple.awdl` para el código 1 (`com.apple.nan` para 2/3). Si la creación **falla** → log `"Failed to bind to %s[%s]: %@"` y **señala igualmente** (sitio `0x1000445a3`).
8. Con match 1 (AirLink) y handle creado: log `"Found new interface %s with role %s"`, guarda `parentInterfaceName` en el handle (ver §2.3, `RawGet` 97), ejecuta el caso del código 1 (limpieza de preferencias legacy, `sub_100049A60`, log `"%s when removing %s from legacy preferences"`) y **señala en `0x1000451f2`**.
9. Con match 4 (`LowLatency`): **señala directamente en `0x100045297`** (`mov esi, 4` en `0x100045281`) sin crear handle. Sin match → `"Unable to find role for interface: %s"` (`0x100494b60`) sin señal.

### 1.3 El señalador — `sub_100046BD0` (`0x100046bd0`, 0x160 bytes)

Método Swift que recibe `(self, rol u8, nombre, valor)`: busca el semáforo del rol en el **diccionario `self+176`** (`swift_beginAccess` sobre `a1+176`), lo retiene y ejecuta `while OS_dispatch_semaphore.signal() != 0` — la llamada al stub `_$sSo21OS_dispatch_semaphoreC8DispatchE6signalSiyF` está en **`0x100046d03`** y repite mientras devuelva ≠ 0 (despierta a todos los esperadores). Si el rol no tiene entrada en el diccionario, no hace nada. Los tres call sites (`0x1000445a3`, `0x1000451f2`, `0x100045297`) pasan el código de rol del match (`v248`) o 4.

### 1.4 El esperador — `sub_100047660` (`0x100047660`, 0x22ca bytes)

- Busca primero un handle existente con mismo rol y nombre de padre; si no existe y ya hay semáforo del rol en el diccionario (caso del código 4 con creación en curso), espera sobre ese semáforo en bucle (`OS_dispatch_semaphore.wait(timeout:)` en `0x100047a66`; timeout → log `"Timed out while waiting for the low latency interface with parent %s"` escrito en `0x100048beb`, string `0x1004939a0`).
- Si no hay nada: `dispatch_semaphore_create(0)` en **`0x100047ad2`** e inserción en el diccionario `self+176` (`lea rdi, [rbx+0xB0]` en `0x100047ae1`, clave = código de rol) vía `sub_100028560`; luego salta por la **tabla de ramas en `0x10004992C`** indexada por el código de rol (12 entradas). Los prefijos de nombre por código se construyen con inmediatos: código 0 → `en`, **código 1 → `awdl`** (`mov edi, 0x6C647761` en `0x100047c99`), 2 → `nan`, 3 → `ir` (`?` string de 2 chars, sentido sin confirmar), 4 → `llw`.
- Rama AWDL (código 1): `RawGet` tipo 12 como puerta (3 sitios en el binario: `0x10004811d`, `0x100048260`, `0x1000483a8`), construcción del buffer de 32 bytes (`0x100048c41`–`0x100048d60`: u32 @+0 desde el argumento empaquetado `a5`, MAC 6 bytes @+4..+9 desde `a5`/pila, **rol @+0x0C = 6** en `0x100048cc8`; las ramas de otros códigos escriben **9** en `0x100048c66` y **0xA** en `0x100048c97`), `RawSet` tipo 94 len 32 en **`0x100048d95`** (`mov dword [var_E0], 0x5E` en `0x100048d6d`, `mov dword [var_E0+8], 0x20` en `0x100048d77`).
- Éxito → `DispatchTime.now()` (`0x10004912f`) + **30 s** (`mov qword [r12], 0x1E` en `0x10004913b`, enum `.seconds`) → `OS_dispatch_semaphore.wait(timeout:)` en **`0x1000491a8`**. Timeout → comparación `DispatchTimeoutResult.==` (`0x1000491d0`) → log `"Timed out while waiting for interface %s"` escrito en **`0x10004974b`** (string `0x100493910`) y error. Señal → continúa y devuelve el handle.
- Las ramas de error del create señalan el propio semáforo antes de salir (sitios `0x100048560`, `0x100048630`, `0x1000490c0`, mismos bucles `signal() != 0`) para no colgar a otros esperadores.

### 1.5 Consecuencia para el prototipo

Para que `wifip2pd` acepte `awdl0` en &lt;30 s, el kext debe: (1) contestar `VIRTUAL_IF_CREATE` con éxito escribiendo el nombre BSD en `request+16` (payload), (2) registrar la interfaz en IORegistry con `IOInterfaceName` (para que el match `IOPropertyExistsMatch` dispare), `IO80211VirtualInterfaceRole = "AirLink"` y un índice BSD real (`if_nametoindex`), y (3) responder `SIOCGIFMEDIA` con `IFM_TYPE == IFM_IEEE80211`. No hace falta emitir ningún evento Apple80211 para liberar la espera. La interfaz companion `llw0` (rol interno 4) se acepta por la misma vía con `IO80211InterfaceRole = "LowLatency"` (ver §2.4).

## Parte 2 — Secuencia de peticiones sobre la interfaz AWDL creada

### 2.0 Superficie Apple80211 (inventario cerrado por `nm` + xrefs en el IDB)

El daemon importa exactamente 9 símbolos Apple80211: `Apple80211Open` (`0x1004457d4`), `Apple80211BindToInterfaceWithService` (`0x1004457c2`, único caller `sub_1000383C0` en `0x100038543`), `Apple80211Close` (`0x1004457c8`, en `sub_1000383C0` y `sub_100038700`), `Apple80211RawGet` (`0x1004457da`), `Apple80211RawSet`, `Apple80211StartMonitoringEvent` (`0x1004457e6`), `Apple80211RawEventMonitoringInit`, `Apple80211RawP2PEventMonitoringInit` (estos dos últimos solo como punteros de datos, no call sites) y `Apple80211EventMonitoringHalt`. **No existe `Apple80211Get` ni `Apple80211Set`**: todo Get/Set pasa por `RawGet`/`RawSet` o por los ioctls directos de §2.5. `RawSet` tiene exactamente 2 call sites: `0x100048d95` (tipo 94) y `0x10004b3e6` (tipo 293); `RawGet` 4: los tres de tipo 12 + `0x100046db9` (tipo 97). La estructura de petición es `apple80211req`: `ifname[16] @+0`, tipo u32 `@+16`, val u32 `@+20`, longitud u32 `@+24`, puntero a datos `@+32` (verificado en `sub_100046D30`: `*(_DWORD *)(a4 + 16) = 97`, `*(_DWORD *)(a4 + 24) = len`, `*(_QWORD *)(a4 + 32) = ptr`).

### 2.1 Orden verificado (estático) y estado

| # | Petición | Tipo / len | Emisor (dirección) | Atribución |
|---|---|---|---|---|
| 1 | `RawGet` **12** = `CARD_CAPABILITIES` (verificado contra `gGetHandlerTable[11]` del kernel) | 12 / 21 | `sub_100047660` en `0x10004811d`/`0x100048260`/`0x1000483a8` | Puerta previa a la creación en las ramas de rol; en hardware ya observada (`Docs/ARCHITECTURE.md`) |
| 2 | `RawSet` **94** = `VIRTUAL_IF_CREATE` (kernel: `gSetHandlerTable[93]`) | 94 / 32 | `sub_100047660` en `0x100048d95` | rol 6 (AWDL) @payload+0x0C; prefijo `awdl`; respuesta: nombre BSD escrito por el kernel @payload+16 |
| 3 | Espera 30 s del semáforo (no es petición) | — | wait `0x1000491a8` | Liberada por la notificación IORegistry (§Parte 1) |
| 4 | `RawGet` **97** = `VIRTUAL_IF_PARENT` (kernel: `gGetHandlerTable[96]` = `__ZL20getVIRTUAL_IF_PARENT…`) | 97 / len del nombre | wrapper `sub_100046D30` (`0x100046db9`), invocado como closure `sub_100068C20` desde el descubridor (`sub_100043720`, dentro de `String.init(unsafeUninitializedCapacity: 16, …)` en `0x100045300`–`0x1000453ff`) | Entrada: nombre de la nueva interfaz; respuesta: C-string con el **nombre del padre**, guardado en `InterfaceHandle.parentInterfaceName` |
| 5 | `RawSet` **293** = `COMPANION_SKYWALK_IF_CREATE` (kernel: `gSetHandlerTable[292]` = `__ZL30setCOMPANION_SKYWALK_IF_CREATE…`) | 293 / 24 | `sub_10004B300` (`0x10004b3e6`), llamado desde `sub_100047660` en **`0x100048061`** (caso 4 del switch `0x100047E85` = código interno 4, prefijo `llw`) | Crea la interfaz companion de baja latencia: ifname = nombre del padre (`awdl0`), payload = `{u32 código, u32 1}` (24 B; el u32 viene del llamador; el resto del struct `?`); respuesta: el kernel sobrescribe el campo ifname con el nombre creado y `sub_10004B300` lo lee con `String.init(cString:)`; luego `if_nametoindex("llw0")` si el handle padre tiene rol 1 (constante `813132908` = `"llw0"`) o `if_nametoindex("llw1")` si rol 2 (`829910124` = `"llw1"`). Error → log `"Failed to create low latency interface for %s because %@"` / `"Failed to create low latency interface because parent %s was not found"` |
| 6 | Ioctls ifnet estándar (ver `AWDL_WIFIP2PD_IOCTLS.md`) | — | `sub_100046A30` + wrappers | `SIOCGIFMEDIA` (gate 802.11 del descubridor), `SIOCSIFFLAGS` up/down, `SIOCSIFLLADDR` (MAC), `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI`; los thunks `sub_10005F6B0/700/760/7B0` y `sub_100068ED0/EE0/97C0/97D0/97E0/97F0` son entradas de witness tables Swift (dos copias idénticas en `0x100517b98` y `0x10052cb50`, sin callers de código) |
| 7 | ioctl `'i'` 131 = `0xC1206983` (IOWR 288) + ioctl `'i'` 130 = `0xC0806982` (IOWR 128) con **tipo 7708** | — | `sub_100058420` (sitios `0x100058509`/`0x1000585f8`) y `sub_1000586D0` (`0x1000587e6`/`0x1000588ee`), vía witness (`sub_10005F7B0` ← `sub_100068EE0`, data-xrefs en `0x100517bd8`/`0x10052cb90`) | Ver §2.6 |
| 8 | Monitoreo de eventos (no es petición de configuración) | — | `sub_100041B60` (`Apple80211StartMonitoringEvent` ×16, solo rol 0 + cola NSObject) o `Apple80211RawP2PEventMonitoringInit` / `Apple80211RawEventMonitoringInit` (vía `sub_100041F20`) | Documentado en `AWDL_WIFIP2PD_P2P_EVENTS.md` |

El encadenado Swift que dispara todo esto sigue siendo el de `AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md`: `sub_10036C2B0` ("No AWDL interface found. Creating AWDL interface", string `0x1004a1350`) → `sub_1003654A0` → init `AWDLInterface` `sub_100416D00` → companion `sub_10041B1F0`. Los callers directos de `sub_100047660` siguen siendo `sub_100046E30` (recovery, `0x1000471be`), thunk `sub_10004D300`, y `sub_100207060`/`sub_100207160` (sin callers de código, dispatch por witness `?`).

### 2.2 Verificación cruzada con las tablas del kernel (nombres de los tipos)

Las dos tablas de dispatch de IO80211Family, `__ZL16gGetHandlerTable` (`0xffffff800245eb60`) y `__ZL16gSetHandlerTable` (`0xffffff800245fef0`), se indexan como `tabla[id - 1]` (verificado en `apple80211RouteExternal`, `0xffffff800236cd2e`: índice `(unsigned)(req_type - 1)` y cotas `[1, 586]`). Comprobaciones leídas del KC:

| Slot | Contenido | Conclusión |
|---|---|---|
| `gGetHandlerTable[11]` (`0x…245ebb8`) | `__ZL20getCARD_CAPABILITIES…` | tipo **12 = CARD_CAPABILITIES** |
| `gGetHandlerTable[96]` (`0x…245ee60`) | `__ZL20getVIRTUAL_IF_PARENT…` | tipo **97 = VIRTUAL_IF_PARENT** |
| `gSetHandlerTable[93]` (`0x…24601d8`) | `__ZL20setVIRTUAL_IF_CREATE…` | tipo **94 = VIRTUAL_IF_CREATE** |
| `gSetHandlerTable[292]` (`0x…2460810`) | `__ZL30setCOMPANION_SKYWALK_IF_CREATE…` | tipo **293 = COMPANION_SKYWALK_IF_CREATE** |

Esto atribuye los tipos 97 y 293 que quedaban sin nombre y confirma el 12 y el 94 ya conocidos.

### 2.3 `RawGet` 97 — lectura del nombre del padre

`sub_100046D30` (`0x100046d30`) arma la petición: ifname @0 desde un `String` de `InterfaceHandle` (keypath `unk_10045FE08` + `sub_100063940`, que hace `strlcpy(buf, chars, 16)`), tipo 97 @16, longitud @24 y puntero @32 tomados del `String` de entrada, llama `RawGet(handle.reference, req)` y devuelve el buffer leído como C-string (búsqueda de `'\0'` hasta la longitud). En el descubridor se usa como closure de `String.init(unsafeUninitializedCapacity: 16, initializingUTF8With:)` y el resultado se guarda en `InterfaceHandle.parentInterfaceName` — semántica de "dado `awdl0`, dime tu interfaz padre (`en0`)".

### 2.4 `RawSet` 293 — creación de la companion de baja latencia

Payload (24 bytes, buffer `v61`): `{u32 = a5 (código de companion, `?` semántica exacta — el type dump del binario tiene `apple80211_companion_skywalk_if_create_code` con `rawValue: UInt32`), u32 = 1, 16 bytes restantes sin inicializar/vistos en 0}`. El campo ifname de la petición lleva el nombre del **padre** (`awdl0`); el kernel responde sobrescribiendo ese mismo campo con el nombre creado, que el daemon convierte en `String` y valida con `if_nametoindex` contra los literales empaquetados `"llw0"` (`0x30776c6c`) para handle de rol 1 (AWDL) y `"llw1"` (`0x31776c6c`) para rol 2 (WiFi-Aware). Por tanto, para AWDL el nombre esperado de la companion es **`llw0`**, que coincide con el prefijo `llw` de la rama del código interno 4 (`0x100047c70`).

### 2.5 Los ioctls `'i'` 130/131 con tipo 7708 — estructura y por qué el kernel los rechaza

`sub_100058420` (`0x100058420`): `socket(AF_INET6, SOCK_DGRAM, 0)` (`sub_1001CFA90(30,2,0)`), buffer de **288 B** a cero con solo ifname @0 (keypath `unk_10045FE58`) → `ioctl(fd, 0xC1206983, buf)` (`0x100058509`). Solo si devuelve 0 y `*(a1+16) != 0`: buffer de **128 B** a cero con ifname @0 (keypath `unk_10045FE78`), **`u16 @16 = 7708` (`0x1E1C`)** (`LOWORD` en `0x1000585c3`; los bytes @18..23 quedan 0, de modo que el u32 de tipo leído por el kernel vale 7708) y **16 bytes @24..39 copiados del objeto llamador `a1+32`** (`0x1000585db`) = `{val_len u32, val ptr}` según el layout `apple80211req` → `ioctl(fd, 0xC0806982, buf)` (`0x1000585f8`). Errores → throw Swift con `errno`. `sub_1000586D0` es la variante con el nombre proveniente de una closure (fn-ptr en `a4+40`); sin callers de código (`?`).

Análisis del lado kernel (KC `f489ff8b…`, IDB `BootKernelExtensions`):

- La puerta de ioctls de las interfaces IO80211 es `IO80211VirtualInterface::handleIoctlGated` (`0xffffff8002331d20`) / `IO80211SkywalkInterface::performGatedCommandIOCTL` (`0xffffff80022f779a`). Solo aceptan las constantes canónicas **`_IOWR('i',200,32)` = `0xC02069C8`, `_IOWR('i',201,32)` = `0xC02069C9`, `_IOW('i',200,32)` = `0x802069C8`, `_IOWR('i',201,40)` = `0xC02869C9` y `_IOW('i',200,40)` = `0x802869C8`**; cualquier otra constante devuelve **`0xE00002C7`** (`kIOReturnUnsupported`). Las constantes del daemon (`'i'`,130/128 y `'i'`,131/288) **no están en ese conjunto**.
- Aun si la petición llegara a `apple80211RouteExternal` (`0xffffff800236cd2e`), el id se valida contra `[1, 586]` (rama moderna, `IOLog("IOCTL req out of bounds [1, 586]")`) o `< 458` (rama legacy, `gGet/SetHandlerTableLegacy`); **7708 y 0 quedan fuera en ambas** → devuelve 102 (`kIOReturnUnsupported`).

Conclusión: el par 131/130 con tipo 7708 **no puede ser servido por el dispatch apple80211 de IO80211Family** tal como está escrito; en un macOS real, o lo atiende otra capa (passthrough de xnu de `SIOCGIFMAC`/`SIOCSIFMAC` — números 130/131 "deprecated" en `sockio.h` — u otro handler `?`), o simplemente falla y el daemon lo absorbe por su manejo de errores Swift (el método es `throws` y el fallo del GET impide el SET). Para el kext esto significa: **no hace falta implementar nada para el tipo 7708 en el override apple80211**; si algún día llega un ioctl `'i'` 130/131 a la if_ioctl de la interfaz del kext, el comportamiento del sistema real sería rechazarlo (`0xE00002C7`) salvo camino legacy (`?`).

### 2.6 Qué debe contestar la interfaz del kext (resumen operativo)

1. `CARD_CAPABILITIES` (12) — respuesta de capacidades (byte de AWDL que ya abre la puerta).
2. `VIRTUAL_IF_CREATE` (94) con rol 6 — crear `awdl0`, escribir el nombre BSD en el payload @+16, publicar en IORegistry `IOInterfaceName` + `IO80211VirtualInterfaceRole = "AirLink"` y exponer `SIOCGIFMEDIA` 802.11 con índice BSD vivo: eso libera el semáforo (Parte 1).
3. `VIRTUAL_IF_PARENT` (97) — devolver en el mismo buffer el nombre de la interfaz padre (C-string ≤ 16 B).
4. `COMPANION_SKYWALK_IF_CREATE` (293) — crear `llw0` asociada a `awdl0`, escribir su nombre en el campo ifname de la petición; el daemon lo valida con `if_nametoindex`.
5. Ioctls ifnet: `SIOCGIFMEDIA` (802.11), `SIOCSIFFLAGS`, `SIOCSIFLLADDR`, `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI`.
6. Monitoreo de eventos: no bloquea la creación, pero `Apple80211StartMonitoringEvent` (rol 0) y las vías P2P (`RawP2PEventMonitoringInit`) deben devolver 0 para no encadenar errores de recuperación (`sub_100046E30`).

## Método y límites

- IDA 9.4 headless (`idat -A -S`) sobre copias del IDB en `Build/ida_awdl/task_p2p_sequence/part1` y `/part2` (scripts `p1_probe*.py`, `p2_probe*.py`, salidas `p1_out*.txt`, `p2_out*.txt`); kernel: copia del IDB en `task_p2p_sequence/kern_ioc` (`k_probe*.py`, `k_out*.txt`) y lectura directa de bytes del `.kc` para las tablas y los literales.
- El orden de la §2.1 es el que sostiene la estructura estática (callers + switches + literales) más el ciclo de vida ya documentado; la observación en hardware de cada paso sigue pendiente.
- Quedan `?`: semántica exacta del u32 del payload de 293 y del resto del struct de 24 B; el nombre de la propiedad que alimenta el ifname de los ioctls 130/131 (keypaths `unk_10045FE58`/`unk_10045FE78`, descriptores no resueltos); el protocolo Swift exacto de las witness tables (`$s7CoreP2P14AWDLWiFiDriverP`/`$s7CoreP2P14AWDLControllerP` candidatos por los strings del binario); el mapa completo de los códigos internos 5–11 de la tabla `0x10004992C`; el string de prefijo `ir` del código 3; y la ruta real de `'i'` 130/131 en xnu fuera de IO80211Family.
