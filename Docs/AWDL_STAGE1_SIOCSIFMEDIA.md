# `SIOCSIFMEDIA` sobre `awdl0`: ¿puede cambiar `ifm_current` antes de la lectura de wifip2pd?

Cierra el punto abierto de `AWDL_STAGE1_IFM_CURRENT.md` §3: si algo emite `SIOCSIFMEDIA` sobre
`awdl0` entre el registro (`ifm_current = RegInfo+0x28 = 0x80`) y el `SIOCGIFMEDIA` de wifip2pd que
filtra `(ifm_current & 0xE0) == 0x80`. Base: kernelcache de macOS 26.7.1 (25G241, x86_64), IDA
headless (`idat -A -S`) sobre una copia de `boot_awdl.kc.i64`, y los binarios del mismo sistema para
el lado usuario. Todas las direcciones del núcleo son VA; las de usuario, VA del slice x86_64.

## 1. Constantes: corrección del punto de partida

Compilado contra el SDK de este mismo sistema (`sizeof(struct ifreq) = 32`,
`sizeof(struct ifmediareq) = 44`):

| Comando | Valor | Uso |
|---|---|---|
| `SIOCSIFMEDIA` | `0xC0206937` | fija el medio de una interfaz |
| `SIOCGIFMEDIA` | `0xC02C6938` | lee el medio y la lista soportada |
| `SIOCGIFXMEDIA` | `0xC02C6948` | lee el medio extendido |

`0x80206937` no es un comando y no aparece en el texto del kernelcache (barrido de todos los heads
de código) ni en ninguno de los binarios analizados en §6. El núcleo compara contra `0xC0206937` en
`0xffffff8002b0ad8a` (`cmp edx, 0C0206937h`), no contra `0x80206937`.

Apariciones de `0xC0206937` en el núcleo:

| VA | Función | Instrucción |
|---|---|---|
| `0xffffff80026f79f2` | `IONetworkInterface::performCommand` | `mov edx, 0C0206937h` (interfaces `IONetworkInterface`, no Skywalk) |
| `0xffffff80026f7aa1` | `IONetworkInterface::performGatedCommand` | `mov eax, 0C0206937h` (ídem) |
| `0xffffff8002b0ad8a` | `IOSkywalkNetworkInterface::processBSDCommand` | `cmp edx, 0C0206937h` (camino de `awdl0`) |

## 2. Camino del ioctl en el núcleo para una interfaz de rol 6

`awdl0` es una `IO80211VirtualInterface`; su `ifnet` pertenece a un `IOSkywalkNetworkBSDClient`, así
que el ioctl no pasa por `IONetworkInterface`.

| VA | Instrucción / hecho |
|---|---|
| `0xffffff8002b002e6` | `IOSkywalkNetworkBSDClient::if_ioctl`, cabeza; `ifnet_softc` en `0xffffff8002b002f9` |
| `0xffffff8002b0030f` / `0xffffff8002b0032c` | carga `[client+90h]+0x1C8` y salta: es `IOCommandGate::runAction` (clase cerrada en `AWDL_STAGE1_REGNETINFO_AND_GATES` §6) |
| `0xffffff8002affce6` | `IOSkywalkNetworkBSDClient::ioctlHandler`; en `0xffffff8002affe97` llama `call qword ptr [rax+910h]` |
| `0xffffff8002331b36` | `IO80211VirtualInterface::processBSDCommand`; salta al slot `+0xE80` (`handleIoctl`) |
| `0xffffff8002331c88` | `IO80211VirtualInterface::handleIoctl` |
| `0xffffff8002331d08`–`0xffffff8002331d1b` | sólo los cuatro comandos apple80211 (`0x802069C8`, `0x802869C8`, `0xC02069C9`, `0xC02869C9`) van a `handleIoctlGated`; **cualquier otro, `SIOCSIFMEDIA` incluido, salta a `IO80211SkywalkInterface::processBSDCommand`** |
| `0xffffff80022f6e98` | `IO80211SkywalkInterface::processBSDCommand`; los comandos que no son apple80211 van al slot `+0x910` de la vtable de `IOSkywalkEthernetInterface`: `call qword ptr [rax+920h]` en `0xffffff80022f71cc`, con `rax = __ZTV26IOSkywalkEthernetInterface` (`0xffffff8002b3f178`) |
| `0xffffff8002b1102c` | `IOSkywalkEthernetInterface::processBSDCommand`; no es multicast, lladdr ni `SIOCSIFFLAGS`, así que en `0xffffff8002b1108a` salta al slot `+0x910` de `IOSkywalkNetworkInterface` |
| `0xffffff8002b0ad4a` | `IOSkywalkNetworkInterface::processBSDCommand`; `cmp edx, 0C0206937h` en `0xffffff8002b0ad8a`; salto a `ioctl_sifmedia` en `0xffffff8002b0ad9d` |

No hay ninguna comprobación de rol, de subfamilia ni de entitlement en esta cadena: el rol 6 no se
consulta en ningún punto, y `handleIoctl` reenvía el comando sin filtrar.

La única puerta de privilegios de `_ifioctl` (`0xffffff80005d62d0`) en esta zona es la de
`proc_suser` en `0xffffff80005d6cd6`, y sólo se aplica a `0x8040693E`, `0x8078693E` y `0x8080693E`:
las tres comparaciones están en `0xffffff80005d6cb6`, `0xffffff80005d6cc0` y `0xffffff80005d6cca` y
saltan al bloque común de `0xffffff80005d6cd3`. `0xC0206937` no está entre ellas y llega a
`ifnet_ioctl` sin comprobación de credenciales.

## 3. `ioctl_sifmedia`: cuándo escribe `descriptor+0x34`

La cabeza real es `0xffffff8002b0affa`; `0xffffff8002b0b184` (que `AWDL_STAGE1_IFM_CURRENT.md` citaba
como la función) es el store dentro de ella.

| VA | Instrucción | Hecho |
|---|---|---|
| `0xffffff8002b0b00b` | `mov rax, [rdi+0C0h]` | descriptor de medios |
| `0xffffff8002b0b069` | `call qword ptr [rax+948h]` | `getSupportedMediaArray` rellena la tabla |
| `0xffffff8002b0b06f`–`0xffffff8002b0b0ac` | `test eax` / `jnz` / `mov r14d, 5` | si el slot falla, libera y devuelve 5 (EIO) **sin escribir** |
| `0xffffff8002b0b07f` | `mov ecx, [r13+10h]` | medio pedido: `ifreq+0x10` |
| `0xffffff8002b0b085`–`0xffffff8002b0b093` | bucle | el medio pedido debe estar en la tabla |
| `0xffffff8002b0b0c0`–`0xffffff8002b0b0c7` | `mov rdx, [rbx+0C0h]` / `mov edx, [rdx+34h]` | lee el `ifm_current` actual para el fallback enmascarado |
| `0xffffff8002b0b152` | `call qword ptr [rax+940h]` | `handleChosenMedia(medio)` |
| `0xffffff8002b0b160` | `call qword ptr [rcx+788h]` | `errnoFromReturn` sobre el resultado |
| `0xffffff8002b0b169`–`0xffffff8002b0b16b` | `test eax` / `jnz` | si el mapeo no es 0, **no escribe** |
| `0xffffff8002b0b17d`–`0xffffff8002b0b184` | bajo `[descriptor+0x10]` | `mov [rax+34h], r13d`: escribe el medio elegido |

Escribir exige, por tanto: que `getSupportedMediaArray` (slot `+0x948`) devuelva 0, que el medio
pedido esté en esa tabla, que `handleChosenMedia` (slot `+0x940`) devuelva 0 y que
`errnoFromReturn` (slot `+0x788`) lo mapee a 0. En `__ZTV23IO80211VirtualInterface`
(`0xffffff8002472b78`) esos tres slots son `IO80211SkywalkInterface::getSupportedMediaArray`
(`0xffffff80022f8e5e`), `handleChosenMedia` (`0xffffff80022f8f5c`) y `errnoFromReturn`
(`0xffffff80022f997c`), sin sobrescritura en la subclase.

## 4. Por qué ese store es inalcanzable en `awdl0` con este driver

| VA | Hecho |
|---|---|
| `0xffffff80022f8e5e` | `IO80211SkywalkInterface::getSupportedMediaArray`: toma el controlador de `[ivars+0x120]+0x30` y llama su slot `+0x890` |
| `0xffffff80022f8f24`–`0xffffff80022f8f32` | si el slot devuelve nulo: `IOLog("not valid dictionary")` y `mov r14d, 0E00002F0h`; devuelve error |
| `0xffffff80026f4d38` | `IONetworkController::copyMediumDictionary`: copia la propiedad `IOMediumDictionary` (`getMediumDictionary`, `0xffffff80026f4d20`, cadena en `0xffffff80026ffc43`) |
| `0xffffff80026f4fd0` | `IONetworkController::publishMediumDictionary`: única función que crea esa propiedad |
| `0xffffff8002461148` | `__ZTV17IO80211Controller`: el slot `+0x890` sigue siendo `copyMediumDictionary` (`0xffffff80026f4d38`) y el `+0x8F0` sigue siendo `selectMedium` (`0xffffff80026f4d94`); la clase no los sobrescribe |
| — | xrefs a `IONetworkController::publishMediumDictionary` (`0xffffff80026f4fd0`) en todo el kernelcache: todas son celdas de vtable (datos), **ninguna llamada de código**, así que la propiedad `IOMediumDictionary` no se publica en ningún sitio; `Source/AirLayer` no menciona `IONetworkMedium` ni `publishMediumDictionary`; la única llamada del proyecto es la de `RTLEthernetAdapter` (`Source/Driver/RTLEthernetAdapter.cpp`, un `IOEthernetController` del kext base), objeto distinto del controlador de `awdl0` |
| `0xffffff80022f8f5c` | `IO80211SkywalkInterface::handleChosenMedia`: llama el slot `+0x890` del controlador (`0xffffff80022f8f7a`); si es nulo devuelve `0xE00002F0` (`0xffffff80022f8fb6`); si el medio no está, ídem (`0xffffff80022f8fbe`); si está, llama el slot `+0x8F0` (`0xffffff80022f8fab`) |
| `0xffffff80026f4d94` | `IONetworkController::selectMedium`: `mov eax, 0E00002C7h` y `retn`, es decir `kIOReturnUnsupported` |

Consecuencia: en `awdl0` el primer slot ya falla (no existe `IOMediumDictionary`), `ioctl_sifmedia`
devuelve 5 en `0xffffff8002b0b0ac` y nunca llega a `0xffffff8002b0b184`. Aunque un driver futuro
publicara el diccionario, `selectMedium` seguiría devolviendo `0xE00002C7` porque `RTLAirController`
no lo sobrescribe.

## 5. Otros escritores del campo después del registro

Barrido de stores `mov [reg+34h], …` con una carga de `[x+0C0h]` en las diez instrucciones previas,
sobre todas las funciones `IO80211*`/`IOSkywalk*` de ambos rangos: sólo dos.

| VA | Función | Hecho |
|---|---|---|
| `0xffffff8002b0a332` | `IOSkywalkNetworkInterface::registerNetworkInterface` | `RegInfo+0x28` → `+0x34`, durante el alta |
| `0xffffff8002b0b184` | `IOSkywalkNetworkInterface::ioctl_sifmedia` | sólo tras las condiciones de §3 |

No hay ningún escritor en las rutas de estado de enlace, rol o notificación, y el campo se lee bajo
el lock del descriptor en `ioctl_gifmedia` (`0xffffff8002b0afd5`).

## 6. Lado usuario: quién emite `SIOCSIFMEDIA`

Barrido de los dos valores (`0xC0206937` y `0xC02C6938`) y desensamblado de los aciertos, en el
slice x86_64 de la misma build (25G241):

| Binario | Constantes | Función / dirección | Efecto |
|---|---|---|---|
| `/usr/libexec/wifip2pd` | **sólo** `0xC02C6938` | tabla de ioctls en `0x100493008` (9 entradas); accesor `0x1004443b4`; `call` en `0x100043cb1`; `ioctl` en `0x100043cbf`; filtro `(ifm_current & 0xE0) == 0x80` en `0x100043cd2`–`0x100043cde` | **lee** medios; no puede fijarlos |
| `/usr/libexec/configd` | `0xC0206937` (×2) y `0xC02C6948` | `0x100059d90`, implementación de `_SCNetworkInterfaceSetMediaOptions` (cadena en `0x10006ca93`); `ioctl(SIOCGIFXMEDIA)` en `0x100059f10` (petición calculada en `0x100059f01` como `0xC0206937+0xC0011`); `ioctl(SIOCSIFMEDIA)` en `0x10005a0e4` (`movl $0xc0206937,%esi` en `0x10005a0da`); error `"ioctl(SIOCSIFMEDIA) failed"` | **fija** medios, genérico por nombre BSD; sin cadenas `awdl` |
| `IPConfiguration` | sólo `0xC02C6938` | `movl $0xc02c6938,%esi` en `0x51689`, `ioctl` en `0x51696` | lee medios |
| `/usr/libexec/airportd` | ninguna | — | — |
| `/usr/libexec/symptomsd` | ninguna | — | — |
| `/usr/libexec/UserEventAgent` | ninguna | — | — |
| `networkd` | no existe en 26.7.1 | — | — |

La tabla de wifip2pd en `0x100493008` contiene `0xC02C6938` (índice 0), `0x80206910`, `0x8020693C`,
`0x8020690C`, `0x80206919`, `0xC0806982`, `0xC1206983`, `0x80206931` y `0x80206932`: `SIOCSIFMEDIA`
no está. Ninguno de los binarios listados importa `_SCNetworkInterfaceSetMediaOptions` (configd la
implementa; wifip2pd importa `SCNetworkInterfaceGetBSDName`, no la setter).

## 7. Respuesta

**No: `ifm_current` de `awdl0` no puede cambiar del `0x80` sembrado antes de que wifip2pd lo lea.**

1. El único escritor posterior al alta es `ioctl_sifmedia`, y en `awdl0` falla antes del store
   (`0xffffff8002b0b0ac`) porque el controlador no publica `IOMediumDictionary`
   (`0xffffff80022f8f24`) y `selectMedium` es el de `IONetworkController` (`0xffffff80026f4d94`).
2. Ningún proceso de la lista emite `SIOCSIFMEDIA` sobre `awdl0`: wifip2pd no tiene el comando en su
   tabla (`0x100493008`), configd es el único que lo emite (`0x10005a0e4`) pero como SPI genérica
   sobre un `SCNetworkInterface`, sin código ni cadena de `awdl`, y ninguno de los demás binarios
   contiene la constante.
3. Aun invocando la SPI de configd con `awdl0`, el ioctl fallaría en el núcleo por (1), de modo que
   el valor seguiría siendo `0x80`.

Quedan en `?`: la prueba en hardware (T5.1 sigue siendo la comprobación real) y el caso de un driver
futuro que publique `IOMediumDictionary` y sobrescriba `selectMedium`; en ese escenario
`SIOCSIFMEDIA` sí podría escribir `+0x34`, pero no es el estado de la etapa 1. Tampoco se han
buscado emisores fuera de los binarios pedidos.

## 8. Búsquedas ejecutadas

- Barrido de todos los heads de código del kernelcache por `80206937`: ninguno; por `0xC0206937`:
  las tres funciones de §1.
- Desensamblado completo de `if_ioctl`, `ioctlHandler`, `processBSDCommand` (virtual, Skywalk,
  Ethernet y Network), `handleIoctl`, `ioctl_sifmedia`, `getSupportedMediaArray`,
  `handleChosenMedia`, `copyMediumDictionary`, `getMediumDictionary`, `selectMedium` y `_ifioctl`.
- Xrefs a `ioctl_sifmedia` (sólo `IOSkywalkNetworkInterface::processBSDCommand`, `0xffffff8002b0ad9d`);
  xrefs a `publishMediumDictionary` (`0xffffff80026f4fd0`): todas de datos (celdas de vtable),
  ninguna llamada de código en todo el kernelcache; cadenas `IOMediumDictionary` (sólo get/publish).
- Lectura de las celdas de `__ZTV17IO80211Controller` (`0xffffff8002461148`) y
  `__ZTV23IO80211VirtualInterface` (`0xffffff8002472b78`) en `+0x788`, `+0x890`, `+0x8F0`, `+0x940` y
  `+0x948`; comprobación de cabeza de función para `_ifioctl`, `if_ioctl`, `ioctlHandler`,
  `processBSDCommand` (cuatro clases), `handleIoctl`, `ioctl_sifmedia`, `ioctl_gifmedia`,
  `getSupportedMediaArray`, `handleChosenMedia`, `copyMediumDictionary`, `getMediumDictionary` y
  `selectMedium` (todas cabezas reales).
- Barrido de stores a `+34h` con contexto `[x+0C0h]`; barrido de llamadas al slot `+0x970`.
- Binarios: búsqueda de los patrones de 4 bytes de `0xC0206937` y `0xC02C6938` en los slices
  x86_64, desensamblado de los aciertos y comprobación de importaciones.
