# TLV 5 — election parameters v1: cuerpo en el aire, parser del kernel y render desde el ioctl 155

Este documento fija el cuerpo on-air del TLV de elección v1 (tipo 5), el parser que el kernel le
aplica en recepción y la correspondencia campo a campo con el payload del ioctl 155
(`setAWDL_ELECTION_PARAMS`, 36 B) que el driver RTL8814 debe convertir en bytes. Cierra el `?` de
`AWDL_PSF_MIF_EMISSION.md` §5.2 y complementa `AWDL_TLV_BODIES.md` (tipos 4 y 18),
`AWDL_FRAME_WIRE_FORMAT.md` §4 (ruta del tipo 5 en el `switch`) y `AWDL_OPEN_QUESTIONS_1.md` §1
(TLV v2, tipo 24). Lo no confirmado se marca `?`.

Fuentes: capturas `~/Desktop/AWDLLab/refs/owly/tests/captures/{discovery,receive,send}-ios26.x-mt7925e-ch44-1.pcap`
(4 893 TLVs 5 decodificados, sólo lectura) y kernelcache `BootKernelExtensions.kc` (macOS 26,
Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`) sobre copia del IDB anotado. Los números de
trama son por captura, contando todos los registros del pcap. La spec externa de owly
(`refs/owly/docs/SPEC.md` §3.2, «Election Parameters (5)») coincide byte a byte con lo medido.

**Numeración de tipos**: la etiqueta del `switch` de `actionFrameReport` es el tipo menos 2
(`AWDL_TLV_BODIES.md`); el TLV 5 corresponde al caso 3 del pseudocódigo, que IDA etiqueta
`jumptable FFFFFF80021B03ED case 5` en `0xffffff80021b0a63` (la etiqueta del desensamblado ya
incluye el sesgo), con la llamada al parser en `0xffffff80021b0a77`
(`AWDL_FRAME_WIRE_FORMAT.md` §4).

## 1. Cuerpo en el aire

TLV completo de **24 B**: cabecera de 3 B (`05` tipo, longitud u16 LE = 21 = `15 00`) y cuerpo de
21 B. Los offsets de la tabla cuentan desde el inicio del TLV (cabecera incluida); el cuerpo
empieza en `+3`. Los enteros multibyte son little-endian.

| Offset TLV | Offset cuerpo | Tamaño | Campo (owly SPEC §3.2) | Valores medidos (4 893 TLVs) |
|---:|---:|---:|---|---|
| `+0` | `+0` | 1 | tipo | `0x05` |
| `+1` | `+1` | 2 | longitud | `21` (0x15) en los 4 893 |
| `+3` | `+0` | 1 | flags | `0` en los 4 893 |
| `+4` | `+1` | 2 | election ID | `0` en los 4 893 |
| `+6` | `+3` | 1 | distancia al maestro raíz | `0` en 1 462 (maestros), `1` en 3 431 (seguidores) |
| `+7` | `+4` | 1 | reservado | `0` en los 4 893 |
| `+8` | `+5` | 6 | MAC del maestro raíz | la MAC raíz de la sesión (§4) |
| `+14` | `+11` | 4 | métrica del maestro raíz | `525` (0x20D) en 4 868, `65` (0x41) en 25 |
| `+18` | `+15` | 4 | métrica propia | `525` en 1 462 (maestros), `60` (0x3C) en 3 431 (seguidores) |
| `+22` | `+19` | 2 | relleno | `00 00` en los 4 893 |

Patrones de las capturas (una sesión por pcap, canal 44, mt7925e):

- **Maestro** (`dist = 0`): la MAC raíz es la propia y `root_metric == self_metric`
  (p. ej. `discovery` trama 3: emisor `02:00:00:00:00:02`, dist 0, raíz
  `02:00:00:00:00:02`, 525/525).
- **Seguidor** (`dist = 1`): la MAC raíz es la del maestro, `root_metric` es la métrica que el
  maestro anuncia y `self_metric` es la propia (p. ej. `discovery` trama 1: emisor
  `02:00:00:00:00:03`, dist 1, raíz `02:00:00:00:00:02`, 525/60).
- En `send` hay 25 tramas de `02:00:00:00:00:04` con raíz `02:00:00:00:00:01` y `root_metric = 65`
  (tramas 1866–1898, 1946–1947, 2025, 2078, 2096, 2134): el seguidor sigue un maestro con métrica
  65, superior a su propia 60. En el kernel una raíz anunciada «gana» con métrica mayor
  (`AWDL_OPEN_QUESTIONS_1.md` §1, `forceElection bec of better Root`), coherente con
  `owly SPEC` AWDL-9 (subir la métrica para ganar).
- El TLV 5 viaja en **segunda posición** (tras el TLV 4) tanto en PSF (subtipo 0) como en MIF
  (subtipo 3): conteos por captura en §4.

## 2. Parser del kernel: `parseAwdlElectionParamsV1`

Función: `IO80211AWDLPeer::parseAwdlElectionParamsV1` (`0xffffff80021b50ec..0xffffff80021b5215`,
0x129 B), única llamada desde el caso 3 del `switch` (`0xffffff80021b0a77`).

```c
int parseAwdlElectionParamsV1(IO80211AWDLPeer *this, const IO80211BufferCursor *tlv);
```

### 2.1 Comprobaciones de entrada

- Exige `tlv->getCursorSize() >= 0x18` (24 B = cabecera de 3 B + cuerpo de 21 B;
  `cmp qword ptr [rsi+8], 18h` en `0xffffff80021b50f9`); con cursor menor o con puntero de datos
  nulo (salto en `0xffffff80021b5120`) va a `0xffffff80021b515b`:
  `logDebug("... Error: Assert failure", "parseAwdlElectionParamsV1", línea de fuente 5844)` y
  retorno sin tocar el peer.
- `tlvCheckBounds` (`0xffffff80021aefa0`, llamada desde `0xffffff80021b03ad` antes del `switch`)
  aplica al tipo 5 un mínimo de **21** y un máximo de **37** sobre la longitud del TLV (cuerpo,
  sin cabecera): `case 5u: v9 = 21; LODWORD(v10) = 37;`. Un cuerpo de 21 B (el del aire) pasa;
  cuerpos de 22..37 B se aceptan y los bytes sobrantes no se leen.

### 2.2 Gate del caso 5 en `actionFrameReport`

El caso del `switch` sólo llama al parser si el bit 0 del byte `+87` (`+0x57`) del contexto de
`actionFrameReport` está a 1 (`test byte ptr [rbx+57h], 1` en `0xffffff80021b0a66`). La misma
prueba protege otros casos del `switch` (`0xffffff80021b0450`, `0xffffff80021b05b0`,
`0xffffff80021b0621`, `0xffffff80021b070b`, `0xffffff80021b0c24`), así que no es exclusiva del
tipo 5. Escritores del byte en la misma función:

- `0xffffff80021aff76`: se pone a 1 tras el parseo de la cabecera.
- `0xffffff80021afff4`: se pone a 0 si `getUnicastOptionsFromDataPathTLV` devuelve distinto de 0
  sobre el cursor que `findTLVCursorForType(..., 0x0C)` busca en `0xffffff80021affca` (TLV 12,
  datapath state).
- `0xffffff80021b0097`: se pone a 0 en una rama que sigue a la búsqueda del TLV 31
  (`findTLVCursorForType(..., 0x1F)` en `0xffffff80021b006d`); la condición exacta del salto no se
  ha trazado `?`.

En las capturas PSF y MIF llevan el TLV 12, así que la condición se cumple. Si un AF sin TLV 12
hace fallar `getUnicastOptionsFromDataPathTLV` queda `?`. La regla de owly («un nodo ignora el
tipo 5 si el par también envía tipo 24») no aparece en esta ruta de parseo `?`; el kernel parsea
v1 y v2 por rutas independientes (v2 en `doMinimalAFProcessing`, `AWDL_OPEN_QUESTIONS_1.md` §1).

### 2.3 Campos leídos y destino

Offsets del cursor relativos al inicio del TLV (el cursor incluye la cabecera; el cuerpo empieza
en `+3`). El bloque destino son los ivars privados del peer (`*(peer+0x20)`), con las direcciones
de store exactas de `0xffffff80021b51b8..0xffffff80021b51ec`:

| Lectura | Ivar destino | Store | Campo |
|---|---|---|---|
| `tlv+4` u16 | `+612` (store de 4 B con extensión de ceros) | `0xffffff80021b51cd` | election ID (cuerpo `+1`) |
| `tlv+6` u8 | `+628` | `0xffffff80021b51c2` | distancia (cuerpo `+3`) |
| `tlv+8` u32 y `tlv+12` u16 | `+606` y `+610` | `0xffffff80021b51ec` / `0xffffff80021b51e5` | MAC raíz (cuerpo `+5..+10`) |
| `tlv+14` u32 | `+632` | `0xffffff80021b51b8` | métrica raíz (cuerpo `+11`) |
| `tlv+18` u32 | `+624` | `0xffffff80021b51d7` | métrica propia (cuerpo `+15`) |

No se leen el byte de flags (`cuerpo +0`), el reservado (`+4`) ni el relleno (`+19..+20`).

### 2.4 Semántica de actualización

El parser compara primero la MAC raíz almacenada (`+606`, 6 B) con la del TLV
(`memcmp` en `0xffffff80021b5141`, 6 B):

- **MAC distinta**: `+700 = 1` (`0xffffff80021b514e`) y reescritura completa de
  `+606/+610/+612/+624/+628/+632`.
- **MAC igual**: `+700 = 0` (`0xffffff80021b5194`); se reescribe sólo si cambió la métrica raíz
  (`cmp` en `0xffffff80021b51a5`) o la distancia (`0xffffff80021b51b2`). Un cambio sólo en el
  election ID no dispara reescritura.
- El retorno es la métrica raíz (la leída del TLV o la almacenada si no hubo cambios); el caso 5
  del `switch` no la consume.

`IO80211AWDLPeer::getElectionParams` (`0xffffff80021bf124`) devuelve `*(peer+0x20)+556`, el bloque
del TLV v2, no este bloque v1. Lectores de `+606..+632` y de la marca `+700` fuera del propio
parser: no localizados en esta pasada `?` (bloque «forma antigua» según `AWDL_OPEN_QUESTIONS_1.md` §1).

## 3. Render desde el ioctl 155

### 3.1 Payload del 155 (36 B, re-verificado por stores crudos)

`programChipWithNewMaster` (`0xffffff80021ec4b6`) monta el payload del 155 (cmd
`0x9B802869C8`) con estos stores (`0xffffff80021eca24..0xffffff80021eca6a`; igual en
`initFWElectionParams` `0xffffff80021ed200`, descompilado en la pasada):

| Payload | Ivar del manager | Campo |
|---|---|---|
| `+0x00..+0x07` | — | `0`; `dword +0x08 = 1` («valid») |
| `+0x10` u8 | `+37263` | op_mode; un barrido del desplazamiento `0x918F` en IO80211Family solo da lectores (`electionParamsChanged`, `programChipWithNewMaster`, `initFWElectionParams`), ningún escritor directo `?` |
| `+0x12` u16 | `+37259` | election ID |
| `+0x14` u8 | `+37247` | hop count: distancia del par + 1 (`setElectionParamsToPeer` `0xffffff80021ea50e` lee el TLV v2 del par en `+575` y suma 1); 1 si el maestro forzado `+37472` difiere de la MAC propia y 0 si coincide (`setElectionParamsToForcedPeer` `0xffffff80021ea6f8`); `setElectionParamsToSelf` `0xffffff80021ea494` lo pone a 0 (`mov dword ptr [rax+917Fh], 0` en `0xffffff80021ea4ea`), así que un nodo que vuelve a ser maestro anuncia distancia 0 |
| `+0x16` 6 B | `+37231..+37236` | MAC raíz (propia si se es maestro — `setElectionParamsToSelf` `0xffffff80021ea494` copia `+37216` — o la del par maestro) |
| `+0x1c` u32 | `+37251` | métrica raíz (la del par, `+579`, o la propia `+37255`) |
| `+0x20` u32 | `+37255` | métrica propia |

Esto corrobora la struct de `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.6 y corrige el layout compacto de
24 B que recoge `AWDL_PEERMANAGER_SOFTWARE.md` §3 para el mismo ioctl. Nota: el helper
`setAwdlElectionParams` (`0xffffff800221cb66`, llamado desde `0xffffff8002289191`) escribe
`dword +0x00 = 1` (¿versión?, `?`) antes de rutear el 155, mientras que las dos rutas internas
dejan `+0x00..+0x07` a 0; los campos del render (`+0x10..+0x23`) son idénticos en ambas.

### 3.2 Correspondencia aire ↔ 155

Cada campo del cuerpo en el aire tiene contrapartida directa en el payload del 155, en el mismo
orden relativo; la diferencia es que el payload añade el byte `op_mode` delante y el aire añade el
byte reservado `+4` y 2 B de relleno al final:

| Aire (offset TLV) | Tamaño | Fuente 155 |
|---:|---:|---|
| `+3` flags | 1 | sin contrapartida clara: en el aire `0`; candidato el byte `+0x10` (op_mode) `?` |
| `+4` election ID | 2 | `+0x12` (u16 LE) |
| `+6` distancia | 1 | `+0x14` (hop count; aire: 0 maestro, 1 seguidor) |
| `+7` reservado | 1 | `0` (pad del `+0x15` del payload) |
| `+8` MAC raíz | 6 | `+0x16..+0x1B` |
| `+14` métrica raíz | 4 | `+0x1c` (u32 LE) |
| `+18` métrica propia | 4 | `+0x20` (u32 LE) |
| `+22` relleno | 2 | `00 00` |

### 3.3 Receta de render para la capa RTL8814

Por cada PSF/MIF (y UMI), insertar tras el TLV 4 un TLV de 24 B:

```
05 15 00                      ; tipo 5, len 21 (u16 LE)
<flags>                       ; 0 (byte +0x10 del 155 como alternativa, ?)
<election_id:2 LE>            ; 155 +0x12
<hop>                         ; 155 +0x14
00                            ; reservado
<root_mac:6>                  ; 155 +0x16
<root_metric:4 LE>            ; 155 +0x1c
<self_metric:4 LE>            ; 155 +0x20
00 00                         ; relleno
```

- Longitud fija 21 en el len; el parser acepta 21..37 pero el aire sólo usa 21.
- Los tres campos del medio viajan ya coherentes en el 155: el hop llega con el +1 del salto
  respecto del par, y la métrica raíz es la que el maestro anunció (vía v2 del par en
  `setElectionParamsToPeer`), de modo que el render no recalcule nada.
- `self_metric` y `root_metric` observados: 60 por defecto en seguidores (owly AWDL-8) y 525/65 en
  maestros; el kernel da la vuelta a la comparación habitual (métrica mayor gana,
  `AWDL_OPEN_QUESTIONS_1.md` §1).
- Si el maestro vigente es el propio nodo, el 155 llega con raíz = MAC propia, hop = 0 y
  `root_metric == self_metric` (patrón de las tramas con `dist = 0` de §4).

## 4. Matriz de las capturas

Conteos de TLVs 5 por captura, emisor, rol y subtipo (0 = PSF, 3 = MIF/UMI; maestro y seguidor
emiten ambos):

| Captura | Emisor | dist | raíz | root/self | PSF | MIF | Total |
|---|---|---:|---|---|---:|---:|---:|
| `discovery` | `02:00:00:00:00:02` (maestro) | 0 | sí misma | 525/525 | 296 | 101 | 397 |
| `discovery` | `02:00:00:00:00:03` (local, seguidor) | 1 | `:02` | 525/60 | 303 | 519 | 822 |
| `receive` | `02:00:00:00:00:01` (maestro) | 0 | sí misma | 525/525 | 312 | 190 | 502 |
| `receive` | `02:00:00:00:00:02` (local, seguidor) | 1 | `:01` | 525/60 | 367 | 630 | 997 |
| `send` | `02:00:00:00:00:03` (maestro) | 0 | sí misma | 525/525 | 97 | 466 | 563 |
| `send` | `02:00:00:00:00:04` (local, seguidor) | 1 | `:03` | 525/60 | 584 | 1003 | 1587 |
| `send` | `02:00:00:00:00:04` (local, seguidor) | 1 | `:01` | **65**/60 | 9 | 16 | 25 |

Total 4 893 (1 968 en PSF, 2 925 en MIF/UMI). Los tres maestros se reparten los 1 462 TLVs con
`dist = 0`; los 3 431 con `dist = 1` son los seguidores. Valores constantes en las tres sesiones:
len 21, flags 0, election ID 0, reservado 0, relleno `00 00`. La etiqueta `local` de cada sesión es
la del toml de replay del propio proyecto owly (`crates/owly-radio/tests/replay.rs`).

## 5. Abiertos

- Byte de flags del aire (`cuerpo +0`): si es una copia del `op_mode` del 155 (`+0x10`, ivar
  `+37263`) o un 0 fijo del firmware. El escritor de `ivars+37263` no apareció en las funciones
  de elección revisadas (`programChipWithNewMaster` sólo lo lee) `?`.
- Consumidores del bloque v1 del peer (`+606..+632`) y de la marca `+700`: sólo se ha visto el
  propio parser; el peer expone hacia fuera el bloque v2 (`+556`) `?`. Los demás accesos con
  desplazamiento `0x2BC` en IO80211Family son de otras clases (`IO80211P2PSteeringManager`,
  `IO80211LQMData`, `IO80211P2PSupervisor`, `IO80211Peer::print`), no del bloque privado del par.
- Regla de owly «ignora el tipo 5 si el par envía el 24»: sin equivalente en la ruta de parseo;
  si existe, se aplicaría a nivel de decisión de elección, no de parseo `?`.
- Por qué los maestros de las capturas anuncian métrica propia 525 (y 65 en el caso de `:01`):
  dinámica de elección no cubierta por este documento; el kernel sólo compara métricas mayores como
  «mejor raíz» (`?` sobre el origen de esos valores).
