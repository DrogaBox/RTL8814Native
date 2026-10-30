# Offsets sensibles a la versión del stack AWDL: macOS 14.8.9 y 15.8.2 (23J631 / 24H232)

Port de la tabla de 25G241 ([`AWDL_VERSION_SENSITIVE_OFFSETS.md`](AWDL_VERSION_SENSITIVE_OFFSETS.md))
a los kernelcaches de macOS 14.8.9 (23J631, Darwin 23.6.0) y macOS 15.8.2 (24H232, Darwin 24.6.0),
ambos x86_64. Cada fila de las tablas fuente (§1–§7 del documento base) se releyó en el kernelcache
correspondiente: se localizó el mismo acceso por símbolo de la función dueña (o, para las funciones
sin símbolo, por la función equivalente delimitada por los mismos vecinos), se leyó el offset o la
ranura reales y se derivó un patrón de bytes que lo casa. Los datos de intercambio quedan en
[`Tools/awdl_offsets_23J631.tsv`](../Tools/awdl_offsets_23J631.tsv) y
[`Tools/awdl_offsets_24H232.tsv`](../Tools/awdl_offsets_24H232.tsv), con el mismo formato y orden de
filas que `Tools/awdl_offsets_25G241.tsv`.

Verificación mecánica con `Tools/awdl_recheck_offsets.py --tsv --va-hint` contra cada kernelcache:

| Build | Filas | OK | Ausentes explicadas |
|---|---|---|---|
| 23J631 | 61 | 58 | 2 `NOT-FOUND` + 1 `MOVED` de ruido sobre fila ausente (§6) |
| 24H232 | 61 | 60 | 1 `NOT-FOUND` (§6) |

El `--va-hint` usa la columna de VA de cada TSV, que ya apunta a sitios del propio build: los ficheros
son autocontenidos. Lecturas en IDA headless (`idat -A -S`) sobre copias de `boot_23J631.kc.i64` y de
una IDB reconstruida desde `boot_24H232.kc` en un directorio de trabajo propio.

## 1. Convenciones

- `=` = CONFIRMADO (mismo valor y patrón equivalente); `C` = CAMBIADO (valor nuevo medido); `AUSENTE`
  = el acceso ya no existe en la función dueña (motivo en §6).
- En las filas de vtable, el valor es la ranura relativa al vptr; en las super-llamadas que cargan la
  ZTV directamente (`stop`, `init` virtual) el patrón codifica la celda ZTV (`ranura + 0x10`). La
  tabla de 25G241 ya mezclaba ambas convenciones en esas dos filas; se mantiene tal cual.
- Los VA citados son cabezas de instrucción leídas en las IDB de cada build; cambian entre versiones
  y solo se dan como testimonio del hallazgo.

## 2. Tabla comparativa

### §1 `RegistrationInfo`

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x04` tamaño ≥ `0x108` (gate del alta) | 0x108 | `=` 0x108; dueño renombrado a `registerNetworkInterfaceWithLogicalLink` y codificación `81 7E 04` (C de símbolo/codificación) | `=` (dueño y codificación de 25G241) |
| `+0x3C` headroom (`getTxDataOffset`) | 0x3c | `=` | `=` |
| `+0x3E` (lectura del `start` del cliente) | 0x3e | `=` | `=` |
| `+0x48`/`+0x4C` par MTU (resta Broadcom) | 0x48/0x4C | `=` | `=` |
| `+0x108` MAC | 0x108 | `=` | `=` |

### §2 `ifnet_init_eparams`

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x00` versión == 2 (initBSD / alloc) | 2 | `=` | `=` |
| `+0x04` tamaño `0x158` (initBSD ==, alloc ≥) | 0x158 | `=` | `=` |
| `+0x144` tx data offset (escritura del `start`) | 0x144 | `=` 0x144; codificación SIB nueva (C de codificación) | `=` |
| `+0x144` tx data offset (lectura del alloc) | 0x144 | `=` 0x144; sin REX (C de codificación) | `=` |
| `+0x146` (lectura del alloc) | 0x146 | `=` | `=` |

El struct `ifnet_init_eparams` (versión 2, tamaño 0x158) está intacto en las tres versiones: la puerta
que protege el override de etapa 2 no se mueve.

### §3 `ifnet`

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x78` `if_refflags` (& 7 == 2) | 0x78 | `=` | `=` |
| `+0xBE` `if_flags` (IFF_UP) | 0xbe | `=` | `=` |
| `+0xC0` IFEF_TXSTART (gates) | 0xc0 | `=` | `=` |
| `+0xC0` IFEF_TXSTART (escritura del alta) | 0xc0 | `=` 0xc0; codificación SIB (C de codificación) | `=` |
| `+0xC3` IFEF_SKYWALK_NATIVE | 0xc3 | `=` (mismo hilo de arranque sin símbolo) | `=` |
| `+0x248` `if_output` = `_dlil_output_handler` | 0x248 | C → **0x1C0** | C → **0x228** |
| `+0x250` `if_start` = `_ifp_if_start` | 0x250 | C → **0x1C8** | C → **0x230** |
| `+0x258` `output_handler` (salto de la pila) | 0x258 | C → **0x1D0** | C → **0x238** |
| `+0x258` `output_handler` (cmpxchg de instalación) | 0x258 | C → **0x1D0** | C → **0x238** |
| `+0x260` `output` del alta (escritura) | 0x260 | C → **0x1D8** | C → **0x240** |
| `+0x260` `output` del alta (lectura de respaldo) | 0x260 | C → **0x1D8** | C → **0x240** |
| `+0x598` `if_start` por defecto | 0x598 | AUSENTE (tienda fusionada con la de `+0x250`, un solo store a 0x1C8) | C → **0x578** |
| `+0x5A0` `output_handler` por defecto (escritura y ancla del cmpxchg) | 0x5a0 | C → **0x510** | C → **0x580** |
| `+0x300` bit 1 (hilo en marcha) | 0x300 | C → **0x270** | C → **0x2E0** |
| `+0x304` contador de arranque | 0x304 | C → **0x274** | C → **0x2E4** |
| `+0x309` byte de puerta | 0x309 | C → **0x279** | C → **0x2E9** |
| `+0x330` puntero del hilo de arranque (attach y `assert_wait`) | 0x330 | C → **0x2A0** | C → **0x310** |
| `+0x338` `ifclassq` (helper y alta) | 0x338 | C → **0x2A8** | C → **0x318** |
| `+0x58C`/`+0x58E`/`+0x590` tx/rx data offset + u32 | 0x58c/0x58e/0x590 | C → **0x4FC/0x4FE/0x500** | C → **0x56C/0x56E/0x570** |
| `+0x900` cola netem empotrada | 0x900 | AUSENTE (fuera del helper; sucesor ?) | AUSENTE (ídem ?) |

Los campos que la etapa 1/T3 solo predice (gates `+0x78`, `+0xBE`, `+0xC0`, `+0xC3`) no se mueven;
todo el bloque de salida y de estado del hilo de arranque se reorganiza en cada versión.

### §4 `ifclassq` y disciplina

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x4C` bit 1 de purga | 0x4c | `=` (dueño ahora con símbolo: `_if_qflush`) | `=` |
| `+0x68` tabla de la disciplina | 0x68 | C de dueño: la carga vive en `_fq_if_enqueue_classq` (0x68 intacto) | C de dueño (ídem) |
| tabla `+0x18` operación enqueue | `call [tabla+0x18]` | C: llamada directa `E8` al `_fq_if_enqueue_classq` desde el helper | C (ídem) |
| `+0x5C` límite 0x800 | 0x5c | `=` | `=` |

El despacho por tabla de disciplina desaparece del helper de encolado: el kernel llama
`_fq_if_enqueue_classq` directamente y la indirección quedó dentro (23J631 `+0x4d`
`0xffffff800078edbd`; 24H232 `+0x4f` `0xffffff80007688ef`, ambos `mov rsi, [rdi+68h]`).

### §5 `netif_llink`

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x3C` `nll_state` = 1 / = 2 | 0x3c | `=` (dueño real: `_nx_netif_llink_add`; los patrones casan intactos) | `=` |
| `+0x3C` == 2 ⇒ ENXIO (dequeue) | 0x3c | `=` 0x3C; codificación `80 /5` (C de codificación) | `=` (ídem) |

### §6 Ranuras de vtable citadas

| Ranura (vptr) | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `IOSkywalkNetworkInterface::getTxDataOffset` | 0x9C0 | C → **0x9A8** | C → **0x9B8** |
| `IOSkywalkEthernetInterface::initBSDInterfaceParameters` | 0x8E0 | C → **0x8D0** | `=` 0x8E0 |
| `IOSkywalkNetworkInterface::prepareBSDInterface` | 0x8E8 | C → **0x8D8** | `=` 0x8E8 |
| `IOService::terminate(uint)` | 0x600 | `=` | `=` |
| `IOService::registerService(uint)` | 0x5B0 | `=` | `=` |
| `IOSkywalkBSDClient::start` | 0x5D0 | `=` | `=` |
| `IOSkywalkInterface::stop` (super por ZTV `+0x5D8`) | 0x5D8 | `=` | `=` |
| `OSOrderedSet::lastObject` | 0x1D8 | C: renombrado a **`getLastObject` en 0x1E0** y la llamada salió de `deregisterNetworkInterface` a `IOSkywalkLogicalLink::setInterface` | `=` 0x1D8 (y sigue en `deregisterNetworkInterface`) |
| `OSOrderedSet::getObject` (en el `start` del cliente) | 0x1F8 | AUSENTE del `start` (la ranura existe; el enlace por defecto se obtiene por otra vía ?) | `=` 0x1F8 |
| `IO80211VirtualInterface::init` (super por ZTV `+0xE78`) | 0xE68 | C → **0xDF0** (celda ZTV `+0xE00`) | C → **0xE60** (celda ZTV `+0xE70`) |
| `AppleBCMWLANSkywalkInterface::start(core, RegInfo)` | 0x14E0 | C: despacho desvirtualizado, `createSkywalkInterface` llama directo (`E8` en `0xffffff80015ff8de`) | C → **0x1478** (sigue virtual) |

Las ranuras `0x8E0`/`0x8E8` muestran deriva no monótona: 23J631 las corre 0x10 hacia abajo y 24H232
las devuelve al valor de 25G241. Ninguna ranura puede deducirse de la versión contigua.

### §7 Ivars del cliente BSD

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0xE8` interfaz | 0xe8 | `=` | `=` |
| `+0xF0` copia del RegInfo | 0xf0 | `=` | `=` |
| `+0xF8` puntero del `ifnet` | 0xf8 | `=` | `=` |
| `+0x13C` estado de wake (`init` escribe 1) | 0x13c | C → **`+0x120`**: el `init` pone a cero la qword (`mov qword [rbx+120h], 0` en `0xffffff800285d192`) | C → **`+0x120`** (ídem, `0xffffff80029797b4`) |

## 3. Mapa de desplazamientos del `ifnet` (25G241 → 23J631 → 24H232)

| Campo | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `if_output` (entrada de la pila) | 0x248 | 0x1C0 | 0x228 |
| `if_start` | 0x250 | 0x1C8 | 0x230 |
| `if_start` por defecto | 0x598 | (fusionado en `if_start`) | 0x578 |
| `output_handler` (destino del TX de la pila) | 0x258 | 0x1D0 | 0x238 |
| `output` del alta (`eparams+0x40`) | 0x260 | 0x1D8 | 0x240 |
| `output_handler` por defecto (ancla del CAS) | 0x5A0 | 0x510 | 0x580 |
| tx data offset / hermano / u32 | 0x58C / 0x58E / 0x590 | 0x4FC / 0x4FE / 0x500 | 0x56C / 0x56E / 0x570 |
| estado del hilo de arranque (bit/contador/puerta) | 0x300 / 0x304 / 0x309 | 0x270 / 0x274 / 0x279 | 0x2E0 / 0x2E4 / 0x2E9 |
| puntero del hilo de arranque (wait channel) | 0x330 | 0x2A0 | 0x310 |
| `ifclassq` | 0x338 | 0x2A8 | 0x318 |
| cola netem empotrada | 0x900 | ? | ? |

## 4. Qué significa para la etapa 2 (hook de salida de AirLayer)

**El mecanismo de la vía A sigue vivo.** La puerta `{versión 2, tamaño 0x158}` de
`ifnet_init_eparams` y el campo `params+0x40` no cambian en 23J631 ni en 24H232 (§2), y las validaciones
del `RegistrationInfo` (tamaño mínimo 0x108, headroom `+0x3C`, MTU `+0x48/0x4C`, MAC `+0x108`)
tampoco (§1): el alta con headroom 64 y la entrega del `output` propio por `initBSDInterfaceParameters`
siguen siendo aceptadas por la familia en las tres versiones. En 23J631 la función que lleva el gate
`0x108` se renombró a `registerNetworkInterfaceWithLogicalLink`; el gate opera igual.

**Las celdas que la familia despacha se movieron y no linealmente.** El override de
`initBSDInterfaceParameters` que la etapa 2 instala en la interfaz Skywalk de la capa solo recibe la
llamada si la celda de la vtable de la clase coincide con la ranura que `IOSkywalkNetworkBSDClient::start`
invoca: 0x8E0 en 25G241, **0x8D0 en 23J631**, 0x8E0 en 24H232. Lo mismo aplica a `prepareBSDInterface`
(vía B): 0x8E8 → **0x8D8** → 0x8E8. Un binario compilado con celdas fijas de 25G241 pierde el override
en 23J631 (feature perdida silenciosa: la llamada cae en la implementación base). El gate por build
debe seleccionar estas dos celdas antes que ninguna otra.

**El slot por el que corre cada paquete se reasentó dos veces.** `ifp+0x258` (`output_handler`, el
`jmp` de `_dlil_output_handler`) pasa a `+0x1D0` en 23J631 y `+0x238` en 24H232; su ancla de
comparación para el CAS de instalación (`ifnet_set_output_handler` y el handler por defecto que la
etapa 2 compara) pasa de `+0x5A0` a `+0x510` y `+0x580`. Cualquier escritura o comparación directa
sobre el `ifnet` con offsets fijos de 25G241 cae en un campo vecino: corrupción silenciosa o pánico
(`if_start` es llamada por el hilo de purga). El código de la etapa 2 no debe llevar estos offsets
incrustados; debe leerlos de la tabla del build en curso.

**Las predicciones T3/T3.5 se regeneran por build.** El trío de headroom que el netif añade a los
packets (`+0x58C/0x58E/0x590`) vive en `+0x4FC/0x4FE/0x500` (23J631) y `+0x56C/0x56E/0x570` (24H232);
el `ifclassq` enlazado pasa de `+0x338` a `+0x2A8` y `+0x318`; el estado del hilo de arranque
(`+0x300/0x304/0x309`) a `+0x270/0x274/0x279` y `+0x2E0/0x2E4/0x2E9`; el puntero del hilo (wait
channel del `assert_wait`) de `+0x330` a `+0x2A0` y `+0x310`. La cola netem `+0x900` desaparece del
helper de encolado en ambas versiones y no se localizó su sucesor (?): la predicción T3.5 de desvío
del primer paquete queda sin ancla en 14/15.

**Super-llamada de init de la interfaz AWDL.** La celda ZTV que el `init` de la interfaz virtual usa
para la super-llamada (patrón de las 14 celdas `__asm` de la etapa 1) se mueve de `ZTV+0xE78`
(ranura 0xE68) a `ZTV+0xE00` (0xDF0) en 23J631 y `ZTV+0xE70` (0xE60) en 24H232. La tabla de celdas
`Tools/awdl_recheck_vtable.py` debe extenderse con estas tres posiciones.

**Ruido descartado.** Los valores «MOVED» plausibles de la primera pasada mecánica que la lectura en
IDA refutaron: 0x230 en `_ifnet_attach` de 23J631 (era un `cmp` de puntero vecino; el hilo real está
en 0x2A0), 0x998/0x9a8 de los SIB de `initRegistrationInfo` (primera llamada indirecta de la función,
no `getTxDataOffset`), 0xff30bd89/0xb70f44a2/0x4fc24/0xba/0x3a884/0xd40 (primeras coincidencias del
segundo pase con el desplazamiento comodinado), y 0x1d0/0x238 de las filas `+0x260`/`+0x598` (los
stores corresponden a otros campos del bloque reasentado).

## 5. Cambios de símbolos y de forma entre builds

- `registerNetworkInterface` (4 argumentos + flags) existe con ese nombre en 25G241 y 24H232; en
  23J631 el par de overloads se renombra (`registerNetworkInterface` con `IOSkywalkPacketQueue**` y
  `registerNetworkInterfaceWithLogicalLink`). La resolución por nombre debe aceptar ambos.
- `OSOrderedSet::lastObject` → `getLastObject` en 23J631 (ranura 0x1D8 → 0x1E0); 24H232 vuelve al
  nombre y ranura previos. La llamada que en 25G241/24H232 está en
  `IOSkywalkNetworkInterface::deregisterNetworkInterface` vive en 23J631 dentro de
  `IOSkywalkLogicalLink::setInterface`.
- El helper de encolado/despertar (sin símbolo, entre `_ifnet_enqueue` y `_ifnet_enqueue_pkt`) llama
  directamente a `_fq_if_enqueue_classq` en 23J631/24H232; la tabla de disciplina se consulta dentro
  de esa función. Las filas del helper de la tabla 25G241 (`sub_FFFFFF80005E6110`) apuntan en los TSV
  nuevos a los equivalentes `sub_FFFFFF8000685460` (23J631) y `sub_FFFFFF80006470D0` (24H232), y el
  hilo de arranque a `sub_FFFFFF800068B140` / `sub_FFFFFF800064B200`.
- `IOSkywalkNetworkBSDClient::init` ya no escribe el dword de wake a 1 en `+0x13C`: pone a cero la
  qword `+0x120` (23J631 y 24H232). Los escritores con símbolo (`setPowerStateHandler`,
  `ioctlHandler`) no existen como tales en 23J631 (?).
- `_if_qflush` y `_nx_netif_llink_add` — funciones que la tabla 25G241 trataba como `sub_` — tienen
  símbolo en el symtab de las tres versiones; los TSV nuevos los usan como dueños.

## 6. Filas ausentes y puntos abiertos

| Fila | Builds | Estado |
|---|---|---|
| `ifnet+0x598` `if_start` por defecto | 23J631 | La tienda fusionada con la de `+0x250` en `0x1C8`; en 24H232 sigue existiendo (`+0x578`) |
| `ifnet+0x900` cola netem | 23J631 y 24H232 | Fuera del helper de encolado; sucesor no localizado (?) |
| `OSOrderedSet::getObject` desde el `start` del cliente | 23J631 | El `start` ya no recorre el conjunto de enlaces; la ranura 0x1F8 existe y `deregisterNetworkInterface` de 24H232 sí llama a `lastObject` (0x1D8, `0xffffff8002983b74`) y `getObject` (0x1F8, `0xffffff8002983bc6`) |
| Escritores del ivar de wake del cliente BSD | 23J631 | `init` pone a cero `+0x120`; el equivalente de `setPowerStateHandler` no aparece con ese nombre (?) |

## 7. Consumo por las herramientas

El gate elige el TSV del build en curso (la columna final de cada fichero lleva el VA del sitio en ese
build, de modo que `--va-hint` funciona contra su propio kernelcache). Regenerar una fila tras un
fallo `MOVED`: comodinar el desplazamiento, medir en la IDB del build, reescribir patrón y offset, y
repetir `python3 Tools/awdl_recheck_offsets.py <kc> --tsv <tsv> --va-hint` hasta `OK` o hasta
clasificar la fila como AUSENTE en §6. Las filas AUSENTE mantienen el patrón de 25G241 y quedan como
`NOT-FOUND`/`MOVED` esperados; la columna de campo las marca con `[AUSENTE …]`.
