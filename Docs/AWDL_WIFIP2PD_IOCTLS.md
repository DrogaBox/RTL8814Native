# Ioctls directos de wifip2pd (macOS 25.6.0)

Inventario completo de los sitios que llaman a `ioctl` en `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), obtenido con rea/IDA adjunto a la base abierta en GUI (xrefs al stub ioctl + pseudocódigo/assembly por sitio). Las constantes de request se leyeron de la tabla de datos en `0x100493008` del propio binario (offset de archivo `0x493008`, segmento `__TEXT`). Los nombres de los requests sólo se asignan cuando coinciden numéricamente con el SDK local (`sys/sockio.h`, `net/if.h`, `net/if_media.h`, `sys/socket.h` de MacOSX.sdk). Lo no visto va como `?`.

## Punto de entrada único

`wifip2pd` importa un único símbolo ioctl: `_$s6Darwin5ioctlys5Int32VAD_SuSvtF` = `Darwin.ioctl(Int32, UInt, Int) -> Int32`, stub en **`0x10044485c`** (símbolo undefined de libswiftDarwin; verificado con `nm`). No existe `_ioctl` como función nombrada en el IDB (`get_function_by_name("_ioctl")` falla). Todo el uso de ioctl del binario pasa por 6 call sites a este stub (xrefs completas en `0x10044485c`).

## Selección del número de request: tabla en `0x100493008`

`sub_1004443B4` (**`0x1004443b4`**, 0x1c bytes) es un selector: `return [0x100493008 + idx*8]` para `idx ≤ 8`, `0` si no. Los call sites pasan el índice (inmediato o parámetro) y la tabla devuelve la constante. Contenido de la tabla (9 entradas, qword LE, leído del binario):

| idx | dir tabla | constante | dir | size | grupo | num | Nombre (match numérico exacto) |
|---:|---|---|---|---:|---|---:|---|
| 0 | `0x100493008` | `0xC02C6938` | IOWR | 44 | `'i'` | 56 | `SIOCGIFMEDIA` = `_IOWR('i',56,struct ifmediareq)` (sockio.h:113; `ifmediareq` = 44 B por `#pragma pack(4)`, if.h:376) |
| 1 | `0x100493010` | `0x80206910` | IOW | 32 | `'i'` | 16 | `SIOCSIFFLAGS` = `_IOW('i',16,struct ifreq)` (sockio.h:83) |
| 2 | `0x100493018` | `0x8020693C` | IOW | 32 | `'i'` | 60 | `SIOCSIFLLADDR` = `_IOW('i',60,struct ifreq)` (sockio.h:119) |
| 3 | `0x100493020` | `0x8020690C` | IOW | 32 | `'i'` | 12 | `SIOCSIFADDR` = `_IOW('i',12,struct ifreq)` (sockio.h:81) |
| 4 | `0x100493028` | `0x80206919` | IOW | 32 | `'i'` | 25 | `SIOCDIFADDR` = `_IOW('i',25,struct ifreq)` (sockio.h:89) |
| 5 | `0x100493030` | `0xC0806982` | IOWR | 128 | `'i'` | 130 | **sin nombre**: grupo/num coinciden con `SIOCGIFMAC` (sockio.h:165) pero el size del header es 32 (ifreq) y el constante usa 128 → no coincide numéricamente, no se asigna |
| 6 | `0x100493038` | `0xC1206983` | IOWR | 288 | `'i'` | 131 | **sin nombre**: grupo/num coinciden con `SIOCSIFMAC` (sockio.h:166) pero size 288 ≠ 32 y dir IOWR ≠ IOW → no se asigna |
| 7 | `0x100493040` | `0x80206931` | IOW | 32 | `'i'` | 49 | `SIOCADDMULTI` = `_IOW('i',49,struct ifreq)` (sockio.h:101) |
| 8 | `0x100493048` | `0x80206932` | IOW | 32 | `'i'` | 50 | `SIOCDELMULTI` = `_IOW('i',50,struct ifreq)` (sockio.h:102) |

**SIOCSDRVSPEC / SIOCGDRVSPEC: no se usan.** `SIOCSDRVSPEC = _IOW('i',123,struct ifdrv)` = `0x8028697B` y `SIOCGDRVSPEC = _IOWR('i',123,struct ifdrv)` = `0xC028697B` (sockio.h:150-152); ninguna de las 9 constantes es ninguna de las dos, y ninguna otra constante ioctl aparece en el binario.

## Los 6 sitios de llamada ioctl

### 1) `0x100043cbf` en `sub_100043720` — SIOCGIFMEDIA (descubrimiento de interfaz)

- **Request:** `sub_1004443B4(0)` = `0xC02C6938` (SIOCGIFMEDIA). Visto en assembly `0x100043caf` (`xor edi,edi`) + `0x100043cb1` (call) + `0x100043cb9` (`mov rsi, rax`).
- **fd:** `[AppleDevice + 0x18] + 0x10` (`mov rax,[rbp+var_348]; mov r14d,[rax+10h]`, `0x100043ca4-0x100043cab`).
- **Estructura (44 B, `ifmediareq`):** `ifm_name` @0 llenado con el nombre de interfaz vía keypath `unk_100460700` y helper `sub_100063940` (`0x100043c75-0x100043c97`); resto cero (`movups/movaps xmm0` en `0x100043c5d-0x100043c6e`). Puntero pasado: `&buffer` (`lea r15,[rbp+root]`, `0x100043c87`; `mov rdx,r15`, `0x100043cbc`).
- **Post-check:** `(ifm_current @+16 & 0xE0) == 0x80` (`0x100043ccc-0x100043cde`) = `IFM_TYPE(media) == IFM_IEEE80211` (IFM_NMASK=`0xE0` if_media.h:294, IFM_IEEE80211=`0x80` if_media.h:253). Si el ioctl falla o el tipo no es 802.11, loguea `"Ignoring S becau…"` (`aIgnoringSBecau`/`aIgnoringSBecau_0` en `0x100043e15`/`0x100043ec7`) y sigue el bucle.
- **Contexto:** itera un `IOIteratorNext` de servicios IORegistry leyendo la propiedad `"IOInterfaceName"` (`0x10004397c-0x1000439a7`: literal `667265746E494F49h`/`0EF656D614E656361h` = `"IOInterfaceName"`), con strings `"IO80211Virtual…"` (`0x1000438dc`), `"WifiAwareDisco…"` (`0x1000438ed`), `"IO80211Interfa…"` (`0x1000438fe`), `"Already discove…"` (`0x100043b70`), `"Matched A servic…"` (`0x100043c14`). Es el filtro "¿este servicio virtual es WiFi (802.11)?".

### 2) `0x100046a72` en `sub_100046A30` — despachador genérico

- `sub_100046A30(idx: UInt32, argp: UnsafeRawPointer)` (0xa9 bytes): toma `os_unfair_lock` (`0x100046a54`), lee fd de `*(objeto+0x10)` (`0x100046a5d`), `request = sub_1004443B4(idx)` (`0x100046a64`), `ioctl(fd, request, argp)` (`0x100046a72`), y convierte `errno` en error Swift si devuelve -1 (`0x100046a7a-0x100046ab4`).
- Llamadores directos (get_callers): `sub_10004D0D0` (thunk passthrough **sin callers de código**; única xref de datos en `0x100514e30`), `sub_100056990` (idx 1), `sub_100057120` (idx 2), `sub_1000578F0` (idx 3 o 4), `sub_100057DE0` (idx = parámetro, 7 u 8 vía sus thunks). Los structs los arma cada llamador (ver abajo).

### 3) `0x100058509` en `sub_100058420` — request 131 (`0xC1206982`-familia, IOWR 288) sobre socket AF_INET6

- **Socket:** `sub_1001CFA90(30, 2, 0)` en `0x10005846e` = `socket(AF_INET6, SOCK_DGRAM, 0)` (AF_INET6=30, SOCK_DGRAM=2, socket.h:112/396). fd leído de `socket[4]` (offset +0x10) en `0x1000584f0-0x1000584f8`.
- **Buffer 288 B** (`__bzero(&root, 288)`, `0x100058499`): sólo se escribe el nombre de interfaz @0 vía keypath `unk_10045FE58` + `sub_100063940` (`0x1000584b2-0x1000584d4`). Tipo/len/datos quedan en 0.
- **Request:** `sub_1004443B4(6)` = `0xC1206983` (`0x1000584f8`), arg `&root` (`0x100058509`).

### 4) `0x1000585f8` en `sub_100058420` — request 130 (`0xC0806982`, IOWR 128) sobre el mismo fd

- Sólo si el ioctl anterior devolvió 0 **y** `*(self+0x10) != 0` (`0x10005852b-0x100058535`).
- **Buffer 128 B** (`memset(v45, 0, 128)`, `0x100058574`): nombre @0 (keypath `unk_10045FE78` + `sub_100063940`, `0x100058587-0x1000585ae`); **dword @16 = 0x1E1C = 7708** (`LOWORD(v35[2].value_word) = 7708`, `0x1000585c3`); **16 bytes @24..39 copiados de `self+0x20`** (`*(_OWORD *)&v35[3].value_word = v32`, `0x1000585db`; `v32 = *(_OWORD *)(a1 + 32)`, `0x100058535`).
- **Layout = `apple80211req`** (idéntico al documentado para RawGet/RawSet en `AWDL_WIFIP2PD_REQUESTS.md`: `+0x10` tipo, `+0x18` longitud, `+0x20` puntero a datos): nombre[16] @0, tipo @16 = **7708**, val @20 = 0, longitud+datos @24..39 tomados del llamador. El tipo apple80211 **7708** no está mapeado a nombre (`?`).
- **Request:** `sub_1004443B4(5)` = `0xC0806982` (`0x1000585e7`), arg `v45` (`0x1000585f8`).

### 5) `0x1000587e6` en `sub_1000586D0` — igual que (3)

Mismo patrón: `socket(AF_INET6, SOCK_DGRAM, 0)` (`0x100058730`), buffer 288 B con sólo el nombre (keypath `unk_10045FE58`; el nombre proviene de una closure: fn-ptr en `a4+40` llamada en `0x100058784`/`0x100058875`), `sub_1004443B4(6)` (`0x1000587d5`), ioctl `0x1000587e6`.

### 6) `0x1000588ee` en `sub_1000586D0` — igual que (4)

Mismo buffer de 128 B (nombre + **7708** @16 (`0x1000588b5`) + 16 B de `self+0x20` @24 (`0x1000588d1`)), `sub_1004443B4(5)` (`0x1000588dd`), ioctl `0x1000588ee`. Sin callers directos detectados (get_callers vacío; `?` vía referencia indirecta).

## Wrappers que arman las estructuras del despachador (sub_100046A30)

Todos llenan el nombre @0 con el mismo keypath `unk_10045FE30` y helper `sub_100063940`.

| Wrapper | idx → request | Estructura (32 B ifreq salvo indicación) | Evidencia |
|---|---|---|---|
| `sub_100056990` | 1 → `0x80206910` SIOCSIFFLAGS | `ifflags` (short) @16 = `(ifa_flags de getifaddrs, entrada con `ifa_name` igual y `ifa_addr->sa_family == 18 (AF_LINK)`) & 0xFFBE`; luego `\| 0x41` (IFF_UP\|IFF_RUNNING) si el bool de entrada ≠ 0, sino 0 (baja) | `0x100056a2a-0x100056b0a`, ioctl vía dispatcher `0x100056b4b` |
| `sub_100057120` | 2 → `0x8020693C` SIOCSIFLLADDR | @16: word `4614` = `0x1206` (byte0 `0x06`, byte1 `0x12` = AF_LINK 18); MAC 6 bytes @18..23 escritos por `sub_1000574E0` (formatea un objeto de 0x26 B) | `0x1000571e7-0x100057287` |
| `sub_1000578F0` | 3/4 → SIOCSIFADDR / SIOCDIFADDR | Si el opcional de entrada tiene valor: sockaddr_in @16 (`dword@16 = 528 = 0x0210` → sa_len 0x10, sa_family 2 = AF_INET; sin_addr @20 = low32 del valor) y **idx 3**; si es nil, **idx 4** sin dirección | `0x100057985-0x1000579b6` |
| `sub_100057DE0` | idx = 3er parámetro del llamador | @16: sockaddr_dl — word `4628 = 0x1214` (sdl_len 0x14, sdl_family 0x12 = AF_LINK), byte@20 = 6 (sdl_type = IFT_ETHER), byte@22 = 6 (sdl_alen), MAC en sdl_data @24 | `0x100057e88-0x100057f4b` |
| ↳ `sub_10005F7D0` | fija **idx 7** → `0x80206931` SIOCADDMULTI | passthrough con MAC | `0x10005f7f6` |
| ↳ `sub_10005F800` | fija **idx 8** → `0x80206932` SIOCDELMULTI | passthrough con MAC | `0x10005f826` |

Respecto a los tipos/IDs apple80211 que pide la tarea: en estos ioctls directos de sockets/interfaz **no hay tipo apple80211** (son ifreq/ifmediareq); el único id tipo-apple80211 aparece en los ioctls 130/131 como el campo @16 = **7708** (sin nombre conocido, `?`), junto a longitud+datos @24..39 tomados del objeto llamador.

## Cadena de llamadores (insumo para T2)

- `sub_100043720` ← `sub_100047660` (`0x100047f09`, `0x100047f2c`) y `sub_10004A980` (`0x10004abc3`).
- `sub_100046A30` ← `sub_10004D0D0` (thunk; sin callers de código, 1 xref de datos en `0x100514e30`), `sub_100056990`, `sub_100057120`, `sub_1000578F0`, `sub_100057DE0`.
- `sub_100058420` ← `sub_10005F7B0` (passthrough puro).
- `sub_1000586D0` ← sin callers directos (`?`).
- `sub_100057DE0` ← `sub_10005F7D0` (idx 7), `sub_10005F800` (idx 8).

## Nota de cobertura

`nm` sobre el binario lista un solo símbolo ioctl (`_$s6Darwin5ioctlys5Int32VAD_SuSvtF`) y las xrefs al stub `0x10044485c` cubren los 6 sitios anteriores; los demás usos de ioctl pasan por `sub_100046A30` (que es uno de esos 6 sitios). No hay constantes ioctl fuera de la tabla `0x100493008` observadas en los pseudocódigos revisados.
