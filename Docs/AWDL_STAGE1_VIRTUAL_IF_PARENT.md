# `VIRTUAL_IF_PARENT` (97) sobre `awdl0`: de dónde sale `en2` y qué tiene que existir antes

Cierra la suposición de `AWDL_STAGE1_STUB_VALUES.md` §2, fila 4: que el núcleo responde el 97 con el
nombre de la interfaz primaria sin código propio. Base: kernelcache de macOS 26.7.1 (25G241, Darwin
25.6.0, x86_64), IDA headless (`idat -A -S`) sobre una copia de `boot_awdl.kc.i64`. Todas las
direcciones son VA y cada fila se leyó en el desensamblado; lo que no quedó fijado lleva `?`.

## 1. El manejador y su entrada en la tabla

| VA | Hecho |
|---|---|
| `0xffffff800245eb60` | `__ZL16gGetHandlerTable`, tabla de gets modernos (símbolo presente en el IDB). |
| `0xffffff800236cdab` | `lea rcx, __ZL16gGetHandlerTable` en `apple80211RouteExternal`. |
| `0xffffff800236ce2e` | `mov rax, ds:(...) [rcx+rax*8]`, carga indexada de la tabla de gets. |
| `0xffffff800236ce43` | `call rax`: el manejador recibe `rdi` = controlador, `rsi` = interfaz, `rcx` = `apple80211req`. |
| `0xffffff800245ee60` | celda `gGetHandlerTable+0x300` = índice 96 = código 97; contiene `0xffffff800227b7ff`. |
| `0xffffff800227b7ff` | cabeza de `__ZL20getVIRTUAL_IF_PARENTP17IO80211ControllerP23IO80211SkywalkInterfaceP20IO80211APIUserClientP13apple80211req`. |

El manejador es un solo bloque de 0x76 bytes: no vuelve a entrar en `apple80211RouteExternal` ni
consulta `getAwdlInterface`.

## 2. Qué lee para construir el nombre

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff800227b809`–`0xffffff800227b80c` | `mov rbx, rcx` / `mov r15, rsi` | guarda el `apple80211req` y la interfaz |
| `0xffffff800227b80f`–`0xffffff800227b812` | `mov rax,[rdi]` / `call qword ptr [rax+0C80h]` | slot **`+0xC80` del controlador** |
| `0xffffff800227b818` | `mov r14, rax` | padre candidato |
| `0xffffff800227b81b`–`0xffffff800227b825` | `safeMetaCast(rsi, IO80211VirtualInterface::gMetaClass)` | la interfaz que pide debe ser virtual |
| `0xffffff800227b82d` | `mov eax, 6` | valor de fallo |
| `0xffffff800227b832`–`0xffffff800227b83a` | dos `test`/`jz` | sin padre o sin cast → 6 |
| `0xffffff800227b83f` | `call IO80211SkywalkInterface::getBSDName` | nombre del objeto padre |
| `0xffffff800227b847`–`0xffffff800227b84a` | `strlen` | longitud |
| `0xffffff800227b84f`–`0xffffff800227b852` | `cmp [rbx+18h], eax` / `jb` | si `req_len < strlen` → 22 (EINVAL) |
| `0xffffff800227b856`–`0xffffff800227b85d` | `memmove(*(req+0x20), nombre, strlen)` | el destino es el puntero guardado en `req+0x20`; copia `strlen` bytes sin terminador |
| `0xffffff800227b862` | `xor eax, eax` | éxito |

No se lee `+0x128`, ni un puntero a padre en la interfaz, ni `req+0` (`ifname`). La única entrada es
el controlador del primer argumento, que el despacho obtuvo con `getController()` de la interfaz
(slot `+0xBA8`, `0xffffff800236cd62`).

## 3. Slot `+0xC80` = `IO80211Controller::getPrimarySkywalkInterface`

| VA | Hecho |
|---|---|
| `0xffffff8002461dd8` | celda `__ZTV17IO80211Controller` (`0xffffff8002461148`) `+0xC90`, es decir vptr`+0xC80`; contiene `0xffffff8002295e44`. |
| `0xffffff8002295e44` | cabeza de `IO80211Controller::getPrimarySkywalkInterface` (slot 400 del header). |
| `0xffffff8002295e48` | `mov rax, [rdi+120h]`: bloque privado del controlador. |
| `0xffffff8002295e4f` | `mov rdi, [rax+188h]`: el padre guardado en ese bloque. |
| `0xffffff8002295e5e` | `safeMetaCast` a `IO80211SkywalkInterface::gMetaClass`; si el campo es nulo, devuelve nulo. |

Función de siete instrucciones: sin lock, sin refcount, sin creación perezosa. El padre es
exactamente el puntero guardado en `*(controlador+0x120)+0x188`.

## 4. Quién escribe `+0x188` del bloque del controlador

El bloque se reserva en `IO80211Controller::init` y se guarda en `[controlador+0x120]`
(`0xffffff8002292926` / `0xffffff800229292b`); el campo se pone a cero en `0xffffff8002292984`.

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002295fce` | cabeza de `__ZN17IO80211Controller15attachInterfaceEP8OSObjectP9IOService` | única escritura no nula |
| `0xffffff8002295fe8` | `safeMetaCast(anObject, IO80211SkywalkInterface::gMetaClass)` | sólo interfaces Skywalk |
| `0xffffff8002295ffb` | `call qword ptr [rax+998h]` | `IONetworkController::prepare` (`0xffffff80026f3b88`) |
| `0xffffff8002296001` | `mov rax, [r15+120h]` | bloque del controlador |
| `0xffffff8002296008` | `cmp qword ptr [rax+188h], 0` / `jnz` | **primera puerta**: no reemplaza un primario existente |
| `0xffffff8002296012` | `mov rcx, [rbx+120h]` | bloque de la interfaz |
| `0xffffff8002296019` | `cmp dword ptr [rcx+58h], 1` | **segunda puerta**: rol 1 |
| `0xffffff800229601f` | `mov [rax+188h], r14` | guarda el objeto interfaz |

`+0x58` del bloque de la interfaz es el rol: lo escribe `IO80211SkywalkInterface::setInterfaceRole`
(`0xffffff80022f60f0`, store en `0xffffff80022f6101`) y lo lee `getInterfaceRole`
(`0xffffff80022f5ed8`, `[ivars+88]`).

`IO80211Controller::detachInterface` (`0xffffff800229604a`) es el único borrado: compara el objeto
con `[rcx+188h]` en `0xffffff8002296071` y escribe cero en `0xffffff8002296097`; si no coincide,
sólo registra `"interface is not primary interface"`.

Un barrido de todos los stores a `[reg+188h]` en funciones `IO80211*`/`IOSkywalk*` de ambos rangos
dio cinco aciertos; los otros tres (`0xffffff800232b60f`, `0xffffff800235f43a`,
`0xffffff8002370284`) escriben en objetos distintos (NAN, `IO80211InfraInterface`, monitor). Para el
bloque del controlador sólo existen el cero de `init` y el store de `attachInterface`.

## 5. Quién llama a `attachInterface`

Las referencias a `0xffffff8002295fce` son dos y ambas de datos: la celda de vtable
`0xffffff8002461f20` (`__ZTV17IO80211Controller+0xDD8` = vptr`+0xDC8`) y `0xffffff80017ea5c0`.
No hay ninguna referencia de código.

| VA | Hecho |
|---|---|
| `0xffffff80016aa874` | `AppleBCMWLANCore::createSkywalkInterface` |
| `0xffffff80016aaa38` | `AppleBCMWLANSkywalkInterface::withProvider(...)` devuelve la interfaz en `r13` |
| `0xffffff80016aaa84`–`0xffffff80016aaa8a` | `rsi = r13`, `rdx = rbx`, `call qword ptr [rax+0DC8h]`: el único llamador del slot |
| `0xffffff80016aaa90` | `test al, al` y aborta la creación si el attach falló |

Dentro de la familia nadie lo llama: `IO80211Controller::createVirtualInterface`
(`0xffffff800229612e`) sólo atiende rol 7 (`"ap"`, slot `+0xE68`) y no toca `+0x188`; el 94
(`setVIRTUAL_IF_CREATE` `0xffffff80022886a8`, puente `0xffffff8002268494`) valida `req_len == 32` en
`0xffffff80022886c8`, habla con el WCL y llama al slot 94 y al de `IO80211InfraProtocol`, sin
referencia alguna al primario.

## 6. Nuestro controlador sí lo llama

`RTLAirLayer.cpp:1487-1493` crea `RTLAirInterface` con `initWithController` (que fija
`rtl_sky_setRole(this, kRoleSta)` con `kRoleSta = 1`, líneas 239 y 324) y **antes de arrancarla**
llama `attachInterface(_iface, this)`, que resuelve a
`__ZN17IO80211Controller15attachInterfaceEP8OSObjectP9IOService`; el binario construido lo lleva
como referencia externa (`nm -u build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer`). El
arranque real (`startWithRegistration`) viene después, en la línea 1512.

Consecuencia: desde ese instante `*(controlador+0x120)+0x188` es el objeto `en2`, y el nombre que
devuelve `getBSDName` (`0xffffff80022f5fca`) es `"en2"`: la función compone `"%s%u"` con
`ifnet_name`/`ifnet_unit` en `ivars+0x10` (`0xffffff80022f602b`) y lo cachea tras el flag
`[ivars+1]` (`0xffffff80022f5fdc`).

## 7. Respuesta

**Sí: un `awdl0` recién creado devuelve `en2` para el 97 sin ninguna acción de `RTLAwdInterface`.**
La condición no está en la interfaz virtual sino en el controlador: que `+0x188` ya tenga el objeto
primario. En este driver lo tiene porque `RTLAirLayer.cpp:1493` llama a `attachInterface` con la
interfaz STA. `RTLAwdInterface` no debe ni puede fijarlo:

- `attachInterface` rechaza rol distinto de 1 (`0xffffff8002296019`) y `awdl0` es rol 6;
- la primera puerta (`0xffffff8002296008`) impide reemplazar un primario ya presente;
- `awdl0` nunca llama a `attachInterface` (sólo a `IOService::attach`).

Lo que sí debe cumplir `RTLAwdInterface` es lo que ya hace `rtl_vif_init`: que el bloque de la
interfaz apunte al controlador en `+0x30` (`getController`, `0xffffff80022f957a`, lee
`[ivars+0x30]`) y que el objeto pase el cast a `IO80211VirtualInterface`.

| Condición | Efecto sobre el 97 |
|---|---|
| Camino normal de arranque (paso ≥ 4, `en2` adjunta) | 0 y `"en2"` |
| Controlador sin primaria adjunta (paso 3, o `awdl0` creado por otra vía) | `+0x188` nulo → 6 (ENXIO) |
| `detachInterface` sobre `en2` (`0xffffff8002296097`) | vuelve a 6 |
| `en2` sin ifnet o sin nombre cacheado | 0 con longitud 0; el nombre se compone en el primer `getBSDName` (`?` el instante exacto, no medido en hardware) |
| `awdl0` (rol 6) intentando entrar | rechazado por `0xffffff8002296008` y `0xffffff8002296019` |

Para la etapa 1 no hace falta stub del 97. Si la comprobación en hardware devolviera 6, la causa
estaría en el attach del controlador (o en un `detachInterface` previo), no en el
`RegistrationInfo` de `awdl0`.

## 8. Búsquedas ejecutadas y pendientes

Ejecutadas: celda de `gGetHandlerTable` para el código 97; desensamblado completo de
`getVIRTUAL_IF_PARENT`, `getPrimarySkywalkInterface`, `getBSDName`, `attachInterface`,
`detachInterface`, `setInterfaceRole`, `getInterfaceRole`, `createVirtualInterface`; barrido de
stores a `+188h` en funciones `IO80211*`/`IOSkywalk*`; xrefs a `attachInterface`; barrido de
`[reg+0xDC8]` en `0xffffff8001500000`–`0xffffff8002400000`.

Quedan en `?`: el valor efectivo en hardware (lo decide la comprobación T2), el instante en que se
cachea el nombre de `en2` respecto de la creación de `awdl0`, y el comportamiento de
`AppleBCMWLANCore::createSkywalkInterface` fuera de esta ruta, que no se revisó porque no se ejecuta
con `RTLAirController`.
