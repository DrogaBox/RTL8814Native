# `SIOCGIFMEDIA` en una interfaz virtual de rol 6: de dónde sale `ifm_current`

Cierra el punto 15.9 de `AWDL_STAGE1_IMPL_SPEC.md`: qué valor devuelve `SIOCGIFMEDIA` en
`ifmediareq+0x10` (`ifm_current`) para una `IO80211VirtualInterface` de rol 6, y si algo lo cambia
entre el alta y la lectura de wifip2pd. Base: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). Todas las
direcciones son VA.

## 1. `ioctl_gifmedia` instrucción por instrucción

`IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`):

| VA | Instrucción | Hecho |
|---|---|---|
| `0xffffff8002b0aea2` | `mov r12d, [rcx+20h]` | longitud del buffer de usuario |
| `0xffffff8002b0aeb2` | `mov rax, [rdi+0C0h]` | descriptor de medios de la interfaz |
| `0xffffff8002b0aeb9` | `mov rax, [rax]` | objeto interno del descriptor |
| `0xffffff8002b0aec5` | `mov r13d, [rax+14h]` | recuento de medios |
| `0xffffff8002b0aee0` | `call _IOMallocZeroData_external` | tabla temporal de medios |
| `0xffffff8002b0af01` | `call qword ptr [rcx+948h]` | `getSupportedMediaArray(unsigned *, unsigned *)` rellena la tabla |
| `0xffffff8002b0af99` | `call _copyout` | copia la tabla al `ifm_ulist` del usuario |
| `0xffffff8002b0afb1` | `mov [rbx+20h], r13d` | `ifm_count` |
| `0xffffff8002b0afb5` | `mov rax, [r14+0C0h]` | descriptor de medios |
| `0xffffff8002b0afbc` / `0xffffff8002b0afe2` | `lck_mtx_lock` / `lck_mtx_unlock` sobre `[rax+10h]` | lectura bajo el lock del descriptor |
| `0xffffff8002b0afcc` → `0xffffff8002b0afcf` | `mov ecx, [rax+30h]` → `mov [rbx+1Ch], ecx` | `ifm_active` |
| `0xffffff8002b0afd2` → `0xffffff8002b0afd5` | `mov ecx, [rax+34h]` → `mov [rbx+10h], ecx` | **`ifm_current`** |
| `0xffffff8002b0afd8` → `0xffffff8002b0afdb` | `mov ecx, [rax+38h]` → `mov [rbx+18h], ecx` | `ifm_status` |

Respuesta a (1): el store en `ifmediareq+0x10` se alimenta de `*(*(this+0xC0)+0x34)`, un u32 del
descriptor de medios de la interfaz, leído bajo el lock del descriptor. `ifm_active` sale de `+0x30`
y `ifm_status` de `+0x38` del mismo objeto.

## 2. Escritores de `+0x34`

Barrido de almacenes `mov [reg+34h], …` en las funciones `IOSkywalk*` relacionadas con medios y
registro. Sólo hay dos sobre el descriptor `*(this+0xC0)`:

| VA | Función | Hecho |
|---|---|---|
| `0xffffff8002b0a31e` → `0xffffff8002b0a32a` | `IOSkywalkNetworkInterface::registerNetworkInterface(RegistrationInfo const *, IOSkywalkLogicalLink *, …)` | `RegInfo+0x24` → descriptor `+0x30` (`ifm_active`) |
| `0xffffff8002b0a32d` → `0xffffff8002b0a332` | ídem | `RegInfo+0x28` → descriptor `+0x34` (`ifm_current`) |
| `0xffffff8002b0b184` | `IOSkywalkNetworkInterface::ioctl_sifmedia(__ifnet *, void *)` | sólo en respuesta a un `SIOCSIFMEDIA` |

La copia ocurre durante el registro de la interfaz, antes de `registerService`. Después, el valor
sólo cambia si alguien emite `SIOCSIFMEDIA` sobre la interfaz. En IO80211Family no hay escritores
del campo; la búsqueda de tiendas a `[reg+34h]` precedidas de una carga de `[x+0C0h]` en funciones
`IO80211*` no encontró ninguno.

## 3. Valor para la etapa 1

Con la semilla `RegInfo+0x28 = 0x80`, `SIOCGIFMEDIA` devuelve `ifm_current = 0x80` desde el registro
hasta el primer `SIOCSIFMEDIA`. `(0x80 & 0xE0) == 0x80` cumple el filtro de wifip2pd. Con la semilla
`8` de Broadcom el filtro fallaría para esta lectura (`8 & 0xE0 == 0`), lo que confirma la desviación D5
de `AWDL_STAGE1_IMPL_SPEC.md`. `ifm_active` toma `RegInfo+0x24`, que la etapa 1 fija también en `0x80`.

`?` No se ha comprobado si wifip2pd, airportd u otro proceso emite `SIOCSIFMEDIA` sobre `awdl0`
antes de su lectura; la prueba T5.1 de `AWDL_STAGE1_IMPL_SPEC.md` §14 (`SIOCGIFMEDIA` sobre `awdl0`)
lo resuelve en hardware.
