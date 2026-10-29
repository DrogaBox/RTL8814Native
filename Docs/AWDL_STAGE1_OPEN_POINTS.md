# Puntos abiertos de la etapa 1 de AWDL resueltos en el kernelcache

Resuelve tres de los puntos marcados con `?` en `AWDL_STAGE1_IMPL_SPEC.md` §15 a partir de la base
de datos de desensamblado de `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin 25.6.0), abierta sobre
una copia en un directorio temporal. Todas las direcciones son VA del kernelcache. No se ha
ejecutado nada en hardware: lo que sigue es lectura de código, y lo que el código no fija queda
marcado con `?`.

## 1. Objeto sobre el que se hace `safeMetaCast` en el 94 y slot `+0x1160`

La tabla `gSetHandlerTable[93]` no llama a `apple80211setVIRTUAL_IF_CREATE`: llama al manejador
`setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`), que valida la petición y decide si la envía al WCL o
al puente.

Manejador (`0xffffff80022886a8`):

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff80022886c8` | `cmp dword ptr [rcx+18h], 20h` | exige `req_len == 32` (`+0x18`); si no, devuelve 22 |
| `0xffffff80022886d1`–`0xffffff80022886d8` | `mov rcx,[rcx+20h]` / `test rcx,rcx` / `jz` | exige `req_data` (`+0x20`) no nulo; si no, devuelve 22 |
| `0xffffff80022886dd`–`0xffffff80022886e4` | `mov rax,[rsi+120h]` / `mov rdi,[rax+0D8h]` | `glue = *(iface+0x120)`; `wcl = *(glue+0xD8)` |
| `0xffffff80022886f8`–`0xffffff8002288708` | carga de argumentos | `sendIOUCToWcl(glue, 0x802869C8, 94, data, 32, &consumido)` |
| `0xffffff800228870b` | `call IO80211Glue::sendIOUCToWcl` | ruta WCL |
| `0xffffff8002288710`–`0xffffff8002288714` | `test byte [r15],1` / `jz` | si el WCL no la consume, al puente |
| `0xffffff8002288716`–`0xffffff800228871b` | `cmp eax, 0E082280Fh` / `jz` | si el WCL devuelve `0xE082280F`, al puente |
| `0xffffff800228874c`–`0xffffff800228875c` | `rdi = rsi` (el `IO80211SkywalkInterface*`), `rsi = [req+0x20]` | salto al puente con la **interfaz** y el **payload** |

Puente `apple80211setVIRTUAL_IF_CREATE` (`0xffffff8002268494`):

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff80022684a1`–`0xffffff80022684a9` | `mov rax,[rdi]` / `mov esi,5Eh` / `call qword ptr [rax+0CC8h]` | `isCommandProhibited(94)` **sobre el mismo objeto** |
| `0xffffff80022684af`–`0xffffff80022684b7` | `test eax,eax` / `jz` / `retn` | si devuelve distinto de cero, ese valor es la respuesta |
| `0xffffff80022684b8`–`0xffffff80022684c2` | `lea rsi, IO80211InfraProtocol::gMetaClass` / `call safeMetaCast` | el cast se aplica al **mismo puntero**, sin búsqueda intermedia |
| `0xffffff80022684c7`–`0xffffff80022684ca` | `test rax,rax` / `jz` | si el cast falla, `0xE082280E` |
| `0xffffff80022684cc`–`0xffffff80022684e0` | `mov rax,[r14]` / `mov rax,[rax+1160h]` / `jmp rax` | slot **`+0x1160`** del objeto casteado, con `rdi = objeto`, `rsi = payload` |
| `0xffffff80022684e2` | `mov eax, 0E082280Eh` | retorno cuando el cast no aterriza |

Respondido: **no hay ninguna búsqueda**. El objeto del `safeMetaCast` y el del slot `+0x1160` es el
segundo argumento del manejador, es decir el `IO80211SkywalkInterface*` que la capa de despacho
entrega con la petición; no es el controlador ni la interfaz primaria, y no se consulta
`getAwdlInterface`. El primer argumento del manejador es el `IO80211Controller` (se deduce del
manejador hermano, §3). El slot `+0x1160` figura como `___cxa_pure_virtual` en la vtable de
`IO80211InfraProtocol` (`__ZTV20IO80211InfraProtocol`, `0xffffff80024656a0`; la celda está en `+0x1170` desde el
símbolo, es decir `vptr + 0x1160`), de modo que la llamada aterriza siempre en la implementación de la subclase concreta
(la del controlador Broadcom o `RTLAirInterface`).

`?` Qué objeto concreto entrega el despacho como segundo argumento (la interfaz nombrada en
`apple80211req.ifname` frente a otra referencia del glue) no se ha cerrado en esta pasada: el
manejador no lo comprueba y el puente sólo exige que pase el cast a `IO80211InfraProtocol`.

Consecuencia para la etapa 1: la puerta `isCommandProhibited(94)` se evalúa en
`0xffffff80022684a9` sobre la **interfaz**. `RTLAirInterface` no sobrescribe ese slot (`+0xCC8`),
de modo que responde `IO80211SkywalkInterface::isCommandProhibited` (`0xffffff80022f9bce`), que no
decide nada por sí misma: llama a `getController()` (slot `+0xBA8`, `0xffffff80022f957a`) y
devuelve el resultado del slot `+0xC50` del controlador. Ese slot es puro en
`__ZTV17IO80211Controller` y en el layer lo implementa `RTLAirController::isCommandProhibited`
(`Source/AirLayer/RTLAirLayer.cpp:1726`, slot 394 de `Source/IO80211/IO80211_25_6_0.h:819`), que
devuelve `false`. El 94 no queda bloqueado por esa puerta.

## 2. `sendIOUCToWcl` y el 94: cuándo se cae al puente

`IO80211Glue::sendIOUCToWcl(uint, uint, void*, unsigned long, bool&)` está en
`0xffffff8002194278`.

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002194338`–`0xffffff8002194366` | preparación del bloque | guarda `msg = 0x802869C8`, `code = 94`, `data`, `len = 0x20` |
| `0xffffff8002194366` | `setz byte ptr [r13+2Dh]` | marca el caso `msg == 0x802869C8` |
| `0xffffff800219449c`–`0xffffff80021944a8` | `mov al,[rax+50h]` / `and al,1` / `mov [rcx],al` | escribe en `*consumido` el **bit 0** del resultado del bloque |
| `0xffffff80021945cf` | `____ZN11IO80211Glue13sendIOUCToWcl..._block_invoke` (`0xffffff80021945cf`) | ejecuta la petición en el hilo del glue |
| `0xffffff8002194619`+ | llamada al objeto WCL por `vtable + 0x120` | `(*(...)(v2 + 288))(v2, ...)`, con `v2 = a1[7]` |

El bloque (`0xffffff80021945cf`) llama al objeto WCL por su slot `+0x120` y deja el resultado en la
celda del bloque que `0xffffff800219449c` reduce a un bit; ese bit es lo que el manejador lee en
`0xffffff8002288710`.

Respondido: para un controlador **sin WCL** la petición 94 nunca llega a `sendIOUCToWcl`, porque
`0xffffff80022886e4` carga `*(glue+0xD8)` y `0xffffff80022886eb`–`0xffffff80022886ee` salta
directamente al puente cuando es nulo. Con WCL presente pero sin tomarla, el camino también cae al
puente por dos motivos independientes: el bit de consumo queda a 0
(`0xffffff8002288710`–`0xffffff8002288714`) o la llamada devuelve `0xE082280F`
(`0xffffff8002288716`–`0xffffff800228871b`). Los dos caminos convergen en
`0xffffff8002288738`–`0xffffff800228875c`, que salta al puente.

`?` No se ha identificado la clase del objeto en `glue+0xD8` ni el método concreto del slot
`+0x120`; sólo consta que su bit 0 decide el consumo.

## 3. `getVIRTUAL_IF_PARENT`: cómo se encuentra el padre

`getVIRTUAL_IF_PARENT` está en `0xffffff800227b7ff`.

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff800227b80f`–`0xffffff800227b812` | `mov rax,[rdi]` / `call qword ptr [rax+0C80h]` | llama el slot **`+0xC80`** del **primer argumento** |
| `0xffffff800227b818` | `mov r14, rax` | el resultado es el padre candidato |
| `0xffffff800227b81b`–`0xffffff800227b825` | `safeMetaCast(a2, IO80211VirtualInterface::gMetaClass)` | el segundo argumento debe ser una interfaz virtual |
| `0xffffff800227b82d`–`0xffffff800227b83a` | `eax = 6` / dos `test` / `jz` | si el padre o el cast son nulos, devuelve 6 |
| `0xffffff800227b83c`–`0xffffff800227b844` | `call IO80211SkywalkInterface::getBSDName` | nombre del padre |
| `0xffffff800227b847`–`0xffffff800227b84a` | `strlen` | longitud del nombre |
| `0xffffff800227b84f`–`0xffffff800227b852` | `cmp [rbx+18h], eax` / `jb` | si `req_len < strlen`, devuelve 22 |
| `0xffffff800227b854`–`0xffffff800227b85d` | `memmove([req+0x20], nombre, strlen)` | escribe el nombre en el payload |
| `0xffffff800227b862` | `xor eax,eax` | éxito (0) |

Respondido: el padre sale de una llamada virtual al slot `+0xC80` del primer argumento del
manejador, que es el `IO80211Controller`; en la vtable de `IO80211Controller`
(`__ZTV17IO80211Controller`, celda `+0xC90`) ese slot es
`IO80211Controller::getPrimarySkywalkInterface()` (`0xffffff8002295e44`). No se usa
`getAwdlInterface()` ni el proveedor adjunto del objeto. El nombre que se devuelve es el BSD de la
interfaz primaria (la del rol STA), no el de `awdl0`, y la operación es de sólo lectura sobre el
payload: no crea ni publica nada.

Consecuencia para la etapa 1: el 97 es un manejador de la familia y no requiere código propio.
Sobre `awdl0` responde con el nombre de la interfaz primaria (la STA, `en2` en este sistema)
siempre que `awdl0` pase el cast a `IO80211VirtualInterface` y el controlador tenga interfaz
primaria. `setAwdlInterface()` no interviene en el 97 (punto 2 de §15 de la especificación): la
llamada sigue siendo útil para otros consumidores de `getAwdlInterface()`, pero no es requisito de
este manejador.

## 4. Puntos que siguen abiertos

- El objeto exacto que el despacho entrega como segundo argumento del manejador (§1), más allá de
  que el puente sólo exige el cast a `IO80211InfraProtocol`.
- La clase del objeto WCL en `glue+0xD8` y el método del slot `+0x120` que decide el consumo (§2).
- Ninguna de estas lecturas se ha contrastado en hardware; la comprobación T2 de
  `AWDL_STAGE1_IMPL_SPEC.md` §14 sigue siendo la que decide.
