# Offsets y estructuras sensibles a la versión del stack AWDL (25G241 → macOS 14–26)

Tabla única para el reanálisis por versión del kernelcache. Reúne de todos los `Docs/AWDL_*.md` los
offsets de estructuras y las ranuras de vtable de las que depende la etapa 1 o de las que dependen las
predicciones de las pruebas (T3/T3.5), con un patrón de bytes que localiza cada acceso en la función
dueña para volver a encontrarlo — y medirlo — en un kernelcache de otra versión. Complementa a
[`AWDL_COMPAT_DEPENDENCIES.md`](AWDL_COMPAT_DEPENDENCIES.md) (símbolos importados y asunciones de
comportamiento) y es la «tabla de desplazamientos por build» que
[`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`](AWDL_STAGE1_OUTPUT_HANDLER_LINK.md) §7 anticipaba. Base:
kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64); lecturas por IDA headless (`idat -A -S`)
sobre una copia de `boot_awdl.kc.i64` en `Build/ida_awdl/task_lifecycle/`.

## 0. Convenciones

- **Localización**: toda función se busca por símbolo en el `LC_SYMTAB` del fileset (nunca por VA; los
  VA de este documento cambian en cada versión). El patrón se busca **dentro de la función dueña**.
- **Patrón**: bytes hexadecimales del desensamblado; `??` = comodín de un byte (campos modrm/sib/registro,
  que cambian si el compilador reasigna registros). **El desplazamiento va literal**: si el patrón casa,
  el offset sigue siendo el mismo; si no casa, el offset se movió. Segundo nivel de la receta (§9):
  comodinar el desplazamiento, localizar el acceso igualmente y **medir** el valor nuevo.
- **✓** = bytes leídos en la IDB de 25G241 (sondeos de `task_lifecycle`, `lc_out8`/`lc_out9`);
  **(d)** = patrón derivado del desensamblado citado por el documento de origen, sin lectura de bytes.
- **Riesgo**: qué pasa si el offset se mueve y el gate no lo reajusta — *silencio* (corrupción o
  comportamiento erróneo sin error), *pánico*, *feature perdida* (fallo visible), *análisis* (sólo
  invalida predicciones de las pruebas; el driver no lo consume).
- Los offsets de vtable son relativos al vptr (`ZTV+X` = celda cruda, `vptr+(X−0x10)`).

## 1. `RegistrationInfo` (struct que rellena el driver; la familia lo valida y copia)

El driver escribe este struct (`AWDLInterface.cpp:188-199`); la familia lo valida por versión y tamaño y
hace dos copias (`iface+0x118` y `iface+0xC0`, `AWDL_STAGE1_PENDING_CHECKS.md` §1). Si Apple mueve un
campo **sin subir la versión**, el valor del driver aterriza en el campo equivocado de la copia: fallo
silencioso. La subida de versión o de tamaño mínimo se detecta sola (rechazo del alta, `0xE00002C2`).

| Campo | 25G241 | Lectura de la familia (cabeza) | Patrón (✓/(d)) | Uso | Riesgo |
|---|---|---|---|---|---|
| `+0x00` versión = 1 | 1 | `registerNetworkInterface` `0xffffff8002b0a204` | (d) `cmp [reg],1` previo al `+4` | gate del alta | rechazo visible del alta |
| `+0x04` tamaño ≥ `0x108` | 0x108 | ídem | ✓ `41 81 7C 24 04 08 01 00 00` (`0xffffff8002b0a23c`) | gate del alta | rechazo visible |
| tamaño del struct `0x130` | 0x130 | `copyRegistrationInfo` `0xffffff8002b10a84` (exige ≥ 0x130, `0xffffff8002b10a08`–`0x10a15`) | (d) sin patrón | gate del alta | rechazo visible |
| `+0x3C` headroom (u16) | 64 | `getTxDataOffset` `0xffffff8002b0baba` | ✓ `0F B7 ?? 3C` (`0xffffff8002b0bad2`) | headroom del cliente BSD → `ifp+0x58C` (`AWDL_STAGE1_LIFECYCLE_CHECKS.md` §1) | **silencio**: headroom 0, packets sin reserva |
| `+0x3E` (u16 hermano) | 0 | `IOSkywalkNetworkBSDClient::start` `0xffffff8002aff562` | ✓ `0F B7 ?? 3E` (`0xffffff8002affaa3`) | segundo data offset → `ifp+0x58E` | silencio (campo hermano a 0) |
| `+0x48`/`+0x4C` MTU y MTU máx (u32×2) | 0x594 | `AppleBCMWLANSkywalkInterface::start` `0xffffff80015d909a` (resta el headroom; lector adicional `?`) | ✓ `41 29 ?? 48 41 29 ?? 4C` (`0xffffff80015d9593`, par adyacente) | MTU registrado | silencio: MTU sin descontar el headroom |
| `+0x108` MAC (6 bytes) | — | `initBSDInterfaceParameters` `0xffffff8002b10bca` (memcmp y LLaddr) | ✓ `8B ?? 08 01 00 00` (`0xffffff8002b10c24`, `0xffffff8002b10c4f`) | dirección BSD | silencio: MAC vacía o basura |
| `+0x38` unit (u32) | 0 | vía ranura `+0x9F0` (`0xffffff8002aff82b`) y propiedad `IOInterfaceUnit`; offset interno `?` | (d) sin patrón | nombre/unidad BSD | visible: nombre «awdlN» equivocado |
| `+0x40` flags (u32) | 0 | modelo del disciplinador del `ifclassq` (`AWDL_STAGE1_TXPUSH_LLINK.md` §2); lector interno `?` | (d) sin patrón | disciplina TX | silencio: disciplina distinta |
| `+0x28` (u32 sembrado por la familia) | 0x20 | `initRegistrationInfo` (llama a la ranura `0x9C0` en `0xffffff8002b10973`) | ✓ `41 FF 94 24 C0 09 00 00` (misma ranura `0x9C0`, codificación SIB) | default del campo | silencio |

El `RegInfo` que `AppleBCMWLANSkywalkInterface::start` recibe es mutado por la familia (MTU − headroom,
§3 de `AWDL_STAGE1_LIFECYCLE_CHECKS.md`): un driver no debe reutilizarlo sin rellenarlo de nuevo.

## 2. `ifnet_init_eparams` (struct de pila del `start` del cliente BSD)

Sólo lo tocan la familia y el núcleo; para el driver importa dos veces: la cabecera `{versión, tamaño}`
es el gate que protege todos los campos (si Apple la sube, la base devuelve `0xE00002C2` y el override
de `initBSDInterfaceParameters` de la etapa 2 no corre), y el campo `+0x40` es la vía A del diseño de
etapa 2 (`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md` §5.3).

| Campo | 25G241 | Verificación (cabeza) | Patrón (✓/(d)) | Uso | Riesgo |
|---|---|---|---|---|---|
| `+0x00` versión = 2 | 2 | `ifnet_allocate_extended` `0xffffff800063f3f0` (`0xffffff800063f432`) e `initBSDInterfaceParameters` (`0xffffff8002b10bcf`) | ✓ `83 3F 02` (alloc, sobre rdi) / ✓ `83 3E 02` (initBSD, sobre rsi) | gate de todo el struct | el override deja de correr (feature perdida) |
| `+0x04` tamaño = `0x158` | 0x158 | ídem (`0xffffff800063f440`: `>= 0x158`; initBSD `0xffffff8002b10be8`: `== 0x158`) | ✓ `81 7F 04 58 01 00 00` (alloc) / ✓ `81 7E 04 58 01 00 00` (initBSD) | gate | ídem |
| `+0x08` flags | 0x80000000 | alloc: `& 3 == 0` (pseudocódigo §1.3 de LIFECYCLE) | (d) sin patrón | flags del alta | rechazo visible |
| `+0x38` `if_output` del driver | `IOSkywalkNetworkBSDClient::if_output` | `start` `0xffffff8002aff562` (`0xffffff8002affa54`); **el alta lo sustituye** (`0xffffff800063f882`) | (d) sin patrón | salida inicial | nulo: siempre se sustituye |
| `+0x40` `output` opcional | 0 | `ifnet_allocate_extended`: `[ifp+0x260] = [params+0x40]` (`0xffffff800063f890`) | ✓ `49 89 85 60 02 00 00` (`0xffffff800063f890`) | vía A de la etapa 2 | **silencio**: el output propio no se instala |
| `+0x48` (exigido a 0) | 0 | alloc del proveedor `0xffffff80009a4140` (`0xffffff80009a41f4`) | (d) sin patrón | gate del alta | rechazo visible |
| `+0x144` tx data offset (u16) | RegInfo+0x3C | `start` escribe en `0xffffff8002affa94` (desplazamiento `+0x13C` sobre r13 = base+8); `ifnet_allocate_extended` lee en `0xffffff800063f626` | ✓ `66 41 89 ?? 3C 01 00 00` (sesgo +8) y ✓ `44 0F B7 ?? 44 01 00 00` (`0xffffff800063f626`) | headroom de TX | **silencio**: `ifp+0x58C` a 0 |
| `+0x146` (u16 hermano) | RegInfo+0x3E | ídem (`0xffffff800063f62e`) | ✓ `0F B7 ?? 46 01 00 00` (`0f b7 87 46 01 00 00`, sin REX: el destino es eax) | segundo offset | silencio |
| `+0x148` (u32) | `[cliente+0x130]` o 300/0 | ídem (`0xffffff800063f639`) | (d) sin patrón | tercer campo del trío | silencio |

## 3. `ifnet` (struct BSD; el driver no lo toca en etapa 1, la etapa 2 sí)

Para la etapa 1 estos offsets sólo sustentan predicciones de T3; para la etapa 2 (escritura de
`ifp+0x258`) son de riesgo directo de **corrupción silenciosa** del `ifnet`.

| Campo | 25G241 | Función dueña (cabeza) | Patrón (✓/(d)) | Uso | Riesgo |
|---|---|---|---|---|---|
| `+0x78` `if_refflags` (u32, `& 7 == 2`) | — | `ifnet_enqueue_pkt` `0xffffff80005e65d0` | ✓ `8B ?? 78 83 E0 07 83 F8 02` (`0xffffff80005e65f5`–`0xffffff80005e65fe`) | gate de encolado (T3: sin esto el ND/RS no entra) | **análisis/etapa 2**: ruta TX no arranca |
| `+0xBE` `if_flags` (u16) | IFF_UP | `ifnet_flags` `0xffffff8000640390` (u16 en `+190`, `AWDL_STAGE1_IFUP_PATH.md` §8); gate `test byte [rdi+0BEh], 1` (`0xffffff80005e6600`) | ✓ `F6 ?? BE 00 00 00 01` | `SIOCSIFFLAGS` → subida | análisis |
| `+0xC0` `if_eflags` | IFEF_TXSTART = 0x80 | `ifnet_allocate_extended` `0xffffff800063f3f0` (`0xffffff800063fc0a`); gates de `sub_FFFFFF80005E6110` (`0xffffff80005e65ec`, `0xffffff80005e61b2`) | ✓ `F0 41 81 ?? C0 00 00 00 80 00 00 00` (escritura); ✓ `F6 ?? C0 00 00 00 80` (lecturas) | disparo del hilo de arranque TX | análisis/etapa 2 |
| `+0xC3` (byte, `& 4` IFEF_SKYWALK_NATIVE) | — | hilo `ifnet_start_%s` (test en `0xffffff80005ea8bd`; la cita `0xffffff80005ea8a6` de TXPUSH §8 apunta al `lea` del literal vecino) | ✓ `F6 83 C3 00 00 00 04` | nombre del doorbell | análisis |
| `+0x248` `if_output` (entrada de la pila) | `_dlil_output_handler` | `ifnet_allocate_extended` (`0xffffff800063fc4f`–`0xffffff800063fc56`) | ✓ `49 89 8D 48 02 00 00` (tienda tras el `lea`) | entrada TX de la pila (`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md` §3) | **corrupción silenciosa** si la etapa 2 lo escribe mal |
| `+0x250` `if_start` | `_ifp_if_start` | ídem (`0xffffff800063f89d`) | ✓ `4D 89 BD 50 02 00 00` | purga del hilo de arranque | **pánico** (llamada a puntero equivocado) si la etapa 2 lo corrompe |
| `+0x258` `output_handler` | `_ifnet_enqueue` | `_dlil_output_handler` `0xffffff80005ef500` (`0xffffff80005ef504`, `jmp rax` en `+8`); `ifnet_set_output_handler` `0xffffff80005ef540` (`lock cmpxchg` en `0xffffff80005ef54d`) | ✓ `48 8B ?? 58 02 00 00` ; ✓ `F0 48 0F B1 ?? 58 02 00 00` | destino de todo el TX de la pila; objetivo de la etapa 2 | **corrupción silenciosa / pánico** |
| `+0x260` `output` del alta | 0 (⇒ `_ifnet_enqueue`) | `ifnet_allocate_extended` (`0xffffff800063f890`, `0xffffff800063fc16`) | ✓ `49 8B 85 60 02 00 00` | respaldo de `+0x258` | silencio (vía A) |
| `+0x598` `if_start` por defecto | `_ifp_if_start` | ídem (`0xffffff800063fcf3`) | ✓ `4D 89 B5 98 05 00 00` | restauración | silencio |
| `+0x5A0` `output_handler` por defecto | `_ifnet_enqueue` | ídem (`0xffffff800063fda5`); ancla del `cmpxchg` (`0xffffff80005ef544`) | ✓ `49 89 85 A0 05 00 00` ; ✓ `48 8B ?? A0 05 00 00` | comparador del `cmpxchg` de la etapa 2 | silencio: el CAS fallaría siempre (feature perdida) |
| `+0x300`/`+0x304`/`+0x309` (estado del hilo de arranque) | — | `sub_FFFFFF80005E6110` `0xffffff80005e6110` (`0xffffff80005e61ce`–`0xffffff80005e61e8`) | ✓ `F6 83 00 03 00 00 01` ; ✓ `FF 83 04 03 00 00` ; ✓ `80 BB 09 03 00 00 00` | condiciona el despertar del hilo | análisis/etapa 2 |
| `+0x330` puntero del hilo de arranque | — | `ifnet_attach` (`0xffffff80005e9632`, pánico si nulo tras attach); `assert_wait` (`0xffffff80005eaa0c`) | ✓ `48 83 BB 30 03 00 00 00` ; ✓ `4C 8D BB 30 03 00 00` | despertar de la purga | análisis/etapa 2 |
| `+0x338` `ifclassq` | — | `sub_FFFFFF80005E6110` (`0xffffff80005e614d`); `ifnet_allocate_extended` (`0xffffff800063fe0a`) | ✓ `48 8B B3 38 03 00 00` ; ✓ `49 8B 85 38 03 00 00` | disciplina TX | análisis |
| `+0x58C`/`+0x58E`/`+0x590` (tx/rx data offset + u32) | 64/0/… | `ifnet_allocate_extended` (`0xffffff800063fd48`, `0xffffff800063fd54`, `0xffffff800063fd62`); lector `nx_netif_host_output` (`0xffffff80009ea590`, `0xffffff80009ea796`) | ✓ `66 41 89 ?? 8C 05 00 00` ; ✓ `66 41 89 ?? 8E 05 00 00` ; ✓ `41 89 85 90 05 00 00` | headroom que el netif añade a los packets de TX (`AWDL_STAGE1_LIFECYCLE_CHECKS.md` §1.4) | silencio: packets sin headroom (el campo se llena desde el RegInfo; moverse en el kernel sólo afecta al análisis) |
| `+0x900` cola `netem` empotrada | — | `sub_FFFFFF80005E6110` (`0xffffff80005e6133`) | ✓ `48 83 ?? 00 09 00 00 00` | desvío del primer paquete | análisis (T3.5) |

## 4. `ifclassq` y disciplina (apuntado por `ifp+0x338`)

| Campo | 25G241 | Función dueña (cabeza) | Patrón (✓/(d)) | Uso | Riesgo |
|---|---|---|---|---|---|
| `+0x4C` bit 1 | flag de purga | `sub_FFFFFF80005D5730` | ✓ `F6 43 4C 02` (`0xffffff80005d5782`) | purga del hilo de arranque (`AWDL_STAGE1_STAGE2_PUSH_DESIGN.md` §1) | análisis |
| `+0x68` tabla de la disciplina | fq_codel | `sub_FFFFFF80005E6110` (`0xffffff80005e6158`) | ✓ `4C 8B ?? 68` | selecciona `enqueue` | análisis |
| tabla `+0x18` operación enqueue | `fq_if_enqueue` | ídem (`0xffffff80005e6172`) | ✓ `41 FF 52 18` | encolado real | análisis |
| `+0x5C` = 0x800 | límite | `ifnet_allocate_extended` (`0xffffff800063fe11`) | ✓ `C7 40 5C 00 08 00 00` | tope del disciplinador | análisis |

## 5. `netif_llink`: `nll_state` (`qset+0x3C` de los documentos)

El byte `+0x3C` que los documentos llamaban del «qset» es `netif_llink::nll_state`
(`AWDL_STAGE1_QSET_STATE.md` §1/§2): 1 al crear el enlace, 2 al destruirlo.

| Campo | 25G241 | Función dueña (cabeza) | Patrón (✓/(d)) | Uso | Riesgo |
|---|---|---|---|---|---|
| `+0x3C` = 1 (creación) | 1 | `sub_FFFFFF80009F96E0` (alta del enlace) | ✓ `41 C6 ?? 3C 01` (`0xffffff80009f98ce`) | estado vivo | análisis |
| `+0x3C` = 2 (destrucción) | 2 | `sub_FFFFFF80009F9C60` | ✓ `C6 ?? 3C 02` (`0xffffff80009f9c63`) | estado destruido | análisis |
| `+0x3C` == 2 ⇒ `ENXIO` | — | `kern_netif_queue_tx_dequeue` `0xffffff80009e6120` | ✓ `41 80 79 3C 02` (`0xffffff80009e614e`) | puerta de dequeue (T3.5) | análisis |
| `+0x3C` == 2 en el enqueue | — | `netif_qset_enqueue` `0xffffff80009fa2a0` (`0xffffff80009fa2d3`, `AWDL_STAGE1_IFUP_PATH.md` §8) | (d) sin bytes | segundo rechazo | análisis |

## 6. Ranuras de vtable citadas por los documentos

El rango completo que declara la cabecera de la etapa 1 (27 métodos `0xE68`–`0xF38`, `setMacAddress`
puro `0xF40`, 16 reservadas `0xF48`–`0xFC0`, 63 slots AWDL `0xFC8`–`0x11B8`) lo recomprueba
`Tools/awdl_recheck_vtable.py` contra los volcados de cada versión; aquí van sólo las ranuras con
llamada citada en los documentos y su patrón de acceso. Codificación de una llamada indirecta con
desplazamiento de 32 bits: `FF /2` (`FF 90+rm`, o `41 FF 94 24` con SIB si la base es r12–r15).

| Ranura (vptr) | Método | Sitio citado (cabeza dueña) | Patrón (✓/(d)) | Uso | Riesgo |
|---|---|---|---|---|---|
| `+0x9C0` | `IOSkywalkNetworkInterface::getTxDataOffset` | `IOSkywalkNetworkBSDClient::start` (`0xffffff8002affa8e`) | ✓ `FF ?? C0 09 00 00` | headroom → eparams | **silencio**: headroom 0 |
| `+0x9C0` (variante SIB) | ídem | `initRegistrationInfo` (`0xffffff8002b10973`) | ✓ `41 FF 94 24 C0 09 00 00` | siembra `RegInfo+0x28` | ídem |
| `+0x8E0` | `initBSDInterfaceParameters` | `start` del cliente (`0xffffff8002affadc`) | ✓ `FF ?? E0 08 00 00` | vía A de la etapa 2 (campo `+0x40`) | feature perdida |
| `+0x8E8` | `prepareBSDInterface` | `gatedPrepareNexus` `0xffffff8002b0066c` (`0xffffff8002b00688`) | ✓ `FF ?? E8 08 00 00` | vía B de la etapa 2 (ifnet ya creado) | feature perdida |
| `+0x600` | `terminate(uint)` | `destroySkywalkInterface` `0xffffff80016aae4e` (`0xffffff80016aaebc`, `esi=2`) | ✓ `FF ?? 00 06 00 00` | destrucción de referencia | análisis |
| `+0x5B0` | `IOService::registerService(uint)` | `AppleBCMWLANProximityInterface::start` (`0xffffff800172982a`, `esi=2`) | ✓ `FF ?? B0 05 00 00` | nacimiento del `ifnet` (`AWDL_INTERFACE_BIRTH.md` §2) | sin `ifnet`: feature perdida |
| `+0x5D0` | `IOSkywalkBSDClient::start` | `start` del cliente (`0xffffff8002aff5c8`) | ✓ `FF ?? D0 05 00 00` | arranque de la cadena BSD | análisis |
| `+0x5D8` | `IOSkywalkInterface::stop` (base) | `IOSkywalkNetworkInterface::stop` (`0xffffff8002b09e96`, `jmp`) | ✓ `FF ?? D8 05 00 00` | cola del `stop` | análisis |
| `+0x1D8` | `OSOrderedSet::lastObject` | `deregisterNetworkInterface` (`0xffffff8002b09ec9`) | ✓ `FF ?? D8 01 00 00` | retiro del enlace | análisis |
| `+0x1F8` | `OSOrderedSet::getObject` | `start` del cliente (`0xffffff8002aff884`) | ✓ `FF ?? F8 01 00 00` | enlace por defecto | análisis |
| `+0xE68` | `IO80211VirtualInterface::init` | super-llamadas (`AWDL_STAGE1_SMALL_CHECKS.md` §1.2; `0xffffff8001718e01`) | ✓ `FF ?? 78 0E 00 00` (celda ZTV+`0xE78`) | init de la interfaz virtual | pánico (virtual puro) si el driver declaró mal la celda |
| `ZTV+0x14F0` | `AppleBCMWLANSkywalkInterface::start(core, RegInfo)` | `createSkywalkInterface` (`0xffffff80016aa874`, `0xffffff80016aac40`) | ✓ `FF ?? E0 14 00 00` | despacho del `start` de Broadcom | análisis |

## 7. Ivars del cliente BSD y de la interfaz citados

| Campo | 25G241 | Función dueña (cabeza) | Patrón (✓/(d)) | Uso | Riesgo |
|---|---|---|---|---|---|
| cliente `+0xE8` interfaz | — | `IOSkywalkNetworkBSDClient::start` (`0xffffff8002affa84`; creado en `0xffffff8002aff69b`) | ✓ `48 8B ?? E8 00 00 00` | receptor de todas las ranuras de interfaz | análisis |
| cliente `+0xF0` copia del RegInfo | — | ídem (`0xffffff8002aff74d`; carga de `[iface+0xC0]` en `0xffffff8002aff73c`) | ✓ `48 89 ?? F0 00 00 00` | lectura de `RegInfo+0x3E` | análisis |
| cliente `+0xF8` `__ifnet *` | — | `gatedPrepareNexus` (`0xffffff8002b006a6`) | ✓ `4C 89 ?? F8 00 00 00` | el ifnet llega al cliente | análisis |
| cliente `+0x13C` (dword, estado de `setPowerStateHandler`) | 1 | `init`/`setPowerStateHandler`/`ioctlHandler` (`0xffffff8002aff551`, `0xffffff8002b00091`…) | ✓ `C7 83 3C 01 00 00 01 00 00 00` | wake flags | análisis (no es el headroom: `LIFECYCLE_CHECKS` §1.1) |
| iface `+0xC0` / `+0x118` copias del RegInfo | — | `registerNetworkInterface` (`0xffffff8002b0a292`) / `copyRegistrationInfo` (`0xffffff8002b10a26`) | (d) sin patrón | fuente de todos los campos de §1 | silencio (si se mueve, la copia se pierde) |
| iface `+0xC8` `OSOrderedSet` de enlaces | — | `registerNetworkInterface` (`0xffffff8002b0a39d`) | (d) sin patrón | enlaces lógicos | visible en el alta |

## 8. Funciones ancla (siempre por símbolo; VA de 25G241 sólo como referencia)

| Símbolo | VA 25G241 | Papel en los documentos |
|---|---|---|
| `kern_nexus_controller_alloc_net_provider_instance` | `0xffffff80009a4140` | nacimiento del `ifnet`; exige `params+0x48 == 0` (`BSDSTART_ORDER` §2.5) |
| `nx_netif_validate_llink_config` | `0xffffff80009fad40` | validador del alta (22 en `num_txqs == 0`, `QUEUE_SHAPE` §5) |
| `nx_netif_default_llink_config` | `0xffffff80009faf20` | copia del `llink_init` |
| `kern_netif_queue_tx_dequeue` | `0xffffff80009e6120` | puerta `nll_state`/ENXIO (T3.5) |
| `ifnet_allocate_extended` | `0xffffff800063f3f0` | escritor de los campos de §3 |
| `ifnet_enqueue_pkt` | `0xffffff80005e65d0` | gate de `if_refflags` |
| `sub_FFFFFF80005E6110` (sin símbolo) | `0xffffff80005e6110` | encolado `ifclassq` + despertar del hilo (TXPUSH §4) |
| `_dlil_output_handler` / `ifnet_set_output_handler` | `0xffffff80005ef500` / `0xffffff80005ef540` | cadena `output_handler` (OUTPUT_HANDLER_LINK §3) |
| `IOSkywalkNetworkBSDClient::start` | `0xffffff8002aff562` | ensambla el `ifnet_init_eparams` |
| `IOSkywalkNetworkInterface::getTxDataOffset` | `0xffffff8002b0baba` | lee `RegInfo+0x3C` |
| `IOSkywalkEthernetInterface::initBSDInterfaceParameters` | `0xffffff8002b10bca` | gate `{2, 0x158}` y MAC |
| `AppleBCMWLANCore::createSkywalkInterface` | `0xffffff80016aa874` | despacho del `start` de Broadcom |
| `AppleBCMWLANSkywalkInterface::start` | `0xffffff80015d909a` | lectura de `RegInfo+0x3C` y resta de MTU |

Los 79 símbolos que importa la etapa 1 y los 14 `__asm` explícitos están inventariados en
`AWDL_COMPAT_DEPENDENCIES.md` §1 y los recomprueba `Tools/awdl_recheck_symbols.py`; este documento no
los repite.

## 9. Diseño de consumo por las herramientas de recheck

**Formato de intercambio.** Las tablas §1–§7 se vuelcan a un fichero de datos hermano (p. ej.
`Tools/awdl_offsets_25G241.tsv`) con una fila por acceso: `grupo; estructura; campo; offset; símbolo de
la función dueña; patrón hex (con `??`); longitud del desplazamiento (disp8/disp32); sesgo (0, o +8
para las filas del eparams medidas sobre r13)`. Un fichero por build de referencia; el gate elige el
del build en curso (`VersionGate.h` ya discrimina por `version_major`/`osversion`).

**`awdl_recheck_symbols.py`.** Hoy recorre el `LC_SYMTAB` de los filesets y comprueba los símbolos
importados. Extensión propuesta: para cada fila del fichero, resolver el símbolo dueño en el
kernelcache objetivo (mecanismo ya existente), acotar el rango de la función y buscar el patrón con
tres veredictos: `OK` (el patrón con desplazamiento literal casa ⇒ offset sin cambios), `MOVED` (el
patrón de segundo nivel, con el desplazamiento comodinado, casa ⇒ se reporta el desplazamiento medido y
el valor nuevo para regenerar la fila), `NOT-FOUND` (ninguno de los dos ⇒ relectura manual). No hace
falta desensamblador: el patrón es bytes exactos con comodín por byte (máscara AND). Los patrones con
sesgo (`eparams+0x144` medido como `+0x13C` sobre r13) se comparan contra `offset − sesgo`. Código de
salida coherente con el existente (0 sin deriva, 1 con deriva, 2 de uso).

**`awdl_recheck_vtable.py`.** Hoy compara celda a celda el rango declarado en `AWDLInterface.h`
(`0x428`–`0xE60` verificado; `0xE68`–`0x11B8` con la reserva de los «MISMATCH = rama hermana» ya
documentada). Extensión propuesta: (a) añadir las ranuras de la §6 a la comparación contra los
volcados que ya produce (son ranuras de las mismas vtables); (b) corroboración cruzada con el otro
script: si una ranura se movió en el volcado, las filas de patrón cuyo desplazamiento codifica esa
ranura (`FF ?? C0 09 00 00` para `0x9C0`, etc.) deben dejar de casar — el informe puede marcar esa fila
como «explicada por la deriva de la ranura» en lugar de «no encontrada».

**`awdl_recheck_offsets.py`.** Implementa este diseño sobre `Tools/awdl_offsets_25G241.tsv` (una fila por acceso, patrón con `??`
sobre modrm/sib/registro y desplazamiento literal). Uso: `python3 Tools/awdl_recheck_offsets.py <BootKernelExtensions.kc> [--va-hint]`.
Sin `--va-hint` las filas de funciones sin símbolo se omiten (`SKIPPED`); con él se buscan en la VA de 25G241 y sólo valen contra ese
build. Contra 25G241 el resultado es 61 filas, 61 `OK` con `--va-hint` (48 `OK` y 13 `SKIPPED` sin él); salida 0 sin deriva, 1 con deriva, 2 de uso.

**Correspondencia riesgo→acción.** `MOVED` en una fila de riesgo *silencio* de las §1–§2 (RegInfo y
eparams) bloquea el gate para ese build; `MOVED` en filas de §3–§5 (ifnet/ifclassq/netif_llink) sólo
obliga a regenerar las predicciones de T3; `NOT-FOUND` siempre pide relectura manual en la IDB.

## 10. Patrones verificados byte a byte (25G241)

Leídos con `get_bytes` en la IDB (sondeos `lc_out8`/`lc_out9` de `task_lifecycle`), cubriendo las filas
de mayor riesgo (todas las de riesgo «silencio» y «pánico» de §1–§3): `RegInfo+0x3C`/`+0x3E`
(`0f b7 40 3c`, `0f b7 40 3e`), `RegInfo+0x48/0x4C` (`41 29 47 48 41 29 47 4c`), `RegInfo+0x108`
(`8b 80 08 01 00 00`, `8b 89 08 01 00 00`), `RegInfo+4` (`41 81 7c 24 04 08 01 00 00`),
`eparams+0x144` (`66 41 89 85 3c 01 00 00` en el escritor, `44 0f b7 bf 44 01 00 00` en el lector de
`ifnet_allocate_extended`, `0f b7 87 46 01 00 00` la lectura de `+0x146`), cabecera `{2, 0x158}` (`83 3e 02`, `81 7e 04 58 01 00 00`,
`83 3f 02`, `81 7f 04 58 01 00 00`), `ifp+0x58C/0x58E/0x590` (`66 41 89 85 8c 05 00 00`,
`66 41 89 85 8e 05 00 00`, `41 89 85 90 05 00 00`), `ifp+0x78` (`8b 47 78 83 e0 07 83 f8 02`),
`ifp+0xBE` (`f6 87 be 00 00 00 01`), `ifp+0xC0` (`f6 87 c0 00 00 00 80`, `f0 41 81 8d c0 00 00 00 80 00 00 00`),
`ifp+0x248` (`49 89 8d 48 02 00 00`), `+0x250` (`4d 89 bd 50 02 00 00`), `+0x258` (`48 8b 87 58 02 00 00`,
`f0 48 0f b1 b7 58 02 00 00`), `+0x260` (`49 89 85 60 02 00 00`), `+0x598` (`4d 89 b5 98 05 00 00`),
`+0x5A0` (`49 89 85 a0 05 00 00`, `48 8b 87 a0 05 00 00`), `+0x300/0x304/0x309/0x330`
(`f6 83 00 03 00 00 01`, `ff 83 04 03 00 00`, `80 bb 09 03 00 00 00`, `48 83 bb 30 03 00 00 00`),
`ifp+0x338` (`48 8b b3 38 03 00 00`, `49 8b 85 38 03 00 00`), `ifp+0x900` (`48 83 bf 00 09 00 00 00`),
`ifclassq+0x4C` (`f6 43 4c 02`), `+0x68` (`4c 8b 56 68`), tabla `+0x18` (`41 ff 52 18`),
`+0x5C` (`c7 40 5c 00 08 00 00`), `nll_state` (`41 c6 46 3c 01`, `c6 42 3c 02`, `41 80 79 3c 02`),
ranuras `0x9C0` (`ff 90 c0 09 00 00`, `41 ff 94 24 c0 09 00 00`), `0x8E0` (`ff 90 e0 08 00 00`),
`0x8E8` (`ff 90 e8 08 00 00`), `0x600` (`ff 90 00 06 00 00`), `0x5B0` (`ff 90 b0 05 00 00`),
`0x5D0` (`ff 90 d0 05 00 00`), `0x5D8` (`ff a0 d8 05 00 00`), `0x1D8` (`ff 90 d8 01 00 00`),
`0x1F8` (`ff 90 f8 01 00 00`), `0xE68` (`ff 90 78 0e 00 00`), `0x14E0` (`ff 90 e0 14 00 00`),
cliente `+0xE8`/`+0xF0`/`+0xF8` (`48 8b bb e8 00 00 00`,
`48 89 83 f0 00 00 00`, `4c 89 bb f8 00 00 00`), `+0x13C` (`c7 83 3c 01 00 00 01 00 00 00`),
`ifp+0xC3` (`f6 83 c3 00 00 00 04`). Las mismas 61 secuencias las re-verificó
`Tools/awdl_recheck_offsets.py` leyendo el fichero del kernelcollection (sondeos `lc_out10` para las
cuatro últimas).

## 11. Lo que queda sin verificar

- Los patrones marcados **(d)**: `RegInfo+0x00` (versión), tamaño `0x130` de `copyRegistrationInfo`,
  `eparams+0x08`/`+0x38`/`+0x48`, `netif_qset_enqueue` `cmp [qset+3Ch], 2`
  (`0xffffff80009fa2d3`, citado por `IFUP_PATH`) y los ivars de §7 sin
  patrón. Todos tienen VA citado por el documento de origen y se verifican con la misma receta.
- El lector de la familia para `RegInfo+0x48/0x4C` fuera del `start` de Broadcom y para `RegInfo+0x40`
  (`?` heredado de `TXPUSH_LLINK` §2): los offsets constan, el consumidor no está cerrado.
- El offset interno del `RegInfo+0x38` (unit) dentro de la familia: llega al kernel por la ranura
  `+0x9F0`, no por acceso directo citable.
- Todo lo que deciden las tablas KPI y el cargador de kexts (exportación real de símbolos por versión)
  pertenece a `AWDL_COMPAT_DEPENDENCIES.md` y se verifica sobre el collection, no sobre la IDB.
- En versiones anteriores (Sonoma–15) ningún patrón ha corrido aún: la columna «valor 25G241» es la
  única medida; la primera pasada del script rellena las columnas de las demás versiones.
