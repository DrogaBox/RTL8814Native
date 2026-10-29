# Cuerpos de TLV AWDL: sync params (tipo 4) y secuencia de canales (tipo 18)

Este documento fija los campos que el kernel lee dentro de dos TLVs de los action frames AWDL y a
qué ivar del peer va cada uno. Complementa `AWDL_FRAME_WIRE_FORMAT.md` (cabecera fija de 16 B,
cabecera TLV de 3 B y lista de tipos) y `AWDL_PEERMANAGER_DRIVER_ABI.md` (contrato driver↔manager).
Lo no confirmado se marca `?`.

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
IDB `Build/ida_awdl/task_awdl_pm_abi/kc.i64`. Direcciones VA del kernelcache; las de instrucción se
citan como el punto de lectura dentro de la función descompilada.

**Numeración de tipos**: el `switch` de `actionFrameReport` (`0xffffff80021af6be`) opera sobre
`type - 2` (resta en `0xffffff80021b03ce`). Por tanto el tipo real en el aire es la etiqueta + 2, y
la correspondencia queda: sync params = **4**, chan seq = **18** (`0x12`), sync tree = **20**
(`0x14`), election params v2 = **24** (`0x18`). Esto corrige la lectura ingenua de las etiquetas del
descompilado.

## 1. TLV tipo 4 — sync params

Función: `IO80211AWDLPeer::parseAwdlSyncParamsTlv` (`0xffffff80021b4ba6`, 0x546 B).

### 1.1 Entrada y comprobaciones

```c
int IO80211AWDLPeer::parseAwdlSyncParamsTlv(IO80211Peer *peer,
                                            IO80211BufferCursor *tlv,   // a2
                                            IO80211BufferCursor *hdr,   // a3 (apple80211_act_frm_hdr, 72 B)
                                            IO80211BufferCursor *buf);  // a4 (cuerpo del AF)
```

- Se exige `tlv->len >= 0x2A` (42 B contando desde el inicio del TLV); si no, los punteros se anulan
  y la función sale por el assert de `"Error: Assert failure"` (línea 3399 del fuente): retorno 0.
- Se exige `hdr->len >= 0x48` (72 B) y `buf->len >= 0x10`; en caso contrario se dispara el trap de
  `bounded_ptr` (`"The range of valid memory is too large..."`). Es decir, un AF con cabecera de
  metadatos incompleta no se puede parsear.
- El byte de presencia (`+30`) debe ser distinto de cero; con cero se registra
  `"Zero presence Mode. SyncParams Parse failure"` y se devuelve 0 sin tocar el peer.

### 1.2 Campos del cuerpo

Los desplazamientos son relativos al puntero de TLV que recibe la función, que incluye la cabecera:
el cuerpo empieza en `+3` (así se explica que el primer campo útil esté en `+3` y que el último
`uint16_t` leído esté en `+32`, dentro de los 42 B exigidos) `?` sobre la convención exacta de
desplazamiento que usa el llamante.

| Offset | Tipo | Campo (log del kernel) | Destino / efecto |
|---:|---|---|---|
| `+0` | 1 | tipo de TLV (4) | no se comprueba aquí; lo usa el iterador |
| `+1` | 2 | longitud (`len`) | no se comprueba aquí |
| `+3` | 1 | `tchan` (canal de trabajo) | copiado a peer `+756` y a peer `+1432` |
| `+4` | 2 | `dc` (duty cycle / duración) | peer `+758` |
| `+6` | 1 | `mchan` (canal maestro) | sólo se imprime en la traza (no se almacena `?`) |
| `+8` | 2 | campo de sincronía 1 | peer `+762` |
| `+10` | 2 | campo de sincronía 2 | peer `+764` |
| `+14` | 2 | `aw_ext_length` | peer `+768`; debe valer 16, si no se marca `"Zero AWExt"` en peer `+770` y se registra la traza |
| `+16` | 2 | campo de ventana 1 | peer `+772` |
| `+18` | 2 | `rem` (restante de la ventana) | peer `+904` |
| `+20` | 1 | `ext byte 1` | peer `+1452` |
| `+21` | 1 | `ext byte 2` | sólo traza `?` |
| `+22` | 1 | `ext byte 3` | sólo traza `?` |
| `+23` | 1 | `ext byte 4` | sólo traza `?` |
| `+24` | 4 | dword de secuencia | peer `+600` |
| `+28` | 2 | word de secuencia | peer `+604` |
| `+30` | 1 | `pm` (presence mode) | peer `+748`; cero ⇒ fallo de parseo (ver 1.1) |
| `+32` | 2 | `sn` (número de secuencia) | peer `+106` (se copia además el valor anterior enmascarado con `0xFFC0` a `+108`, y si cambia se guarda `mach_absolute_time()` en `+112`) |

Además, tras fijar `peer+784 = mach_absolute_time()` (marca del último AF) e incrementar el contador
`peer+732`, se llama a `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`) para extraer la
secuencia de canales que viaja dentro de este mismo TLV; un fallo ahí registra
`"Error: parsing ch sequence %d"` y devuelve 0.

### 1.3 Uso de la cabecera fija del AF

Dentro de esta función se leen dos `uint32_t` de la cabecera fija del frame (`buf+8` y `buf+12`, es
decir `phy_tx_time` y `target_tx_time` en la terminología de `AWDL_FRAME_WIRE_FORMAT.md`):
`v18 = *(u32*)(buf+8)` y `v17 = *(u32*)(buf+12)`. Si ambos son distintos de cero y
`v18 > v17`, la diferencia se guarda en peer `+752` (que el log imprime como `sync_contention`).
También se pasan a `monitorPeersTimeline(peer, tlv, v18, v17, *(u32*)(hdr+48))` junto con el dword
`+48` de la cabecera de metadatos. Un driver debe, por tanto, rellenar `+8`/`+12` con valores
monótonos coherentes si quiere que esa comprobación de contención tenga sentido; con ambos a cero la
diferencia no se calcula y no se registra contención (`?` sobre la unidad exacta).

## 2. TLV tipo 18 — secuencia de canales

Función: `IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b5a08`). El tipo 18 es la
forma "clásica" de la secuencia; la forma extendida llega por el tipo 32
(`parseAwdlChanSeqFromChanSeqExtTLV`, `0xffffff80021b5e84`) y la variante embebida en sync params
por `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`). El resultado se vuelca en un
`awdl_chan_seq_info2` (`bounded_ptr`, 392 B como mínimo) que después aplica `updateAwdlChanSeq`
(`0xffffff80021ab7e4`).

### 2.1 Cuerpo en el aire

Offsets desde el inicio del TLV (cabecera de 3 B incluida), según el descompilado de
`0xffffff80021b5a08`:

| Offset | Tamaño | Campo | Uso |
|---:|---:|---|---|
| `+0` | 1 | tipo (18) | iterador |
| `+1` | 2 | longitud | el cursor debe medir al menos 9 B |
| `+3` | 1 | `count - 1` | `n = byte + 1`; se exige `byte < 0x20` (máximo 32 entradas) y `2·n <= len_cursor - 9` |
| `+4` | 1 | codificación | `3` = pares {canal, clase operativa}; cualquier otro valor = canal suelto |
| `+5` | 1 | flags de duplicado (`?` nombre) | copiado al `info2` |
| `+6` | 1 | step count (`?` nombre) | copiado al `info2` |
| `+7` | 2 | canal de relleno (`?` nombre) | copiado como `u16` al `info2` |
| `+9` | `2·n` | entradas de **2 B** | ver abajo |

Cada entrada ocupa **2 bytes**, sea cual sea la codificación (el índice avanza `+= 2`):

- Codificación `3`: byte 0 = número de canal; byte 1 = clase operativa, que se convierte con
  `convertOpClassToChannel` en ancho y banda. Si la conversión falla se registra
  `"fail convert op class, op class 0x%X"` y la función devuelve `-1`.
- Otra codificación: byte 0 = número de canal; la banda se deduce del número (`< 15` ⇒ flag `8`,
  2,4 GHz; `36..165` ⇒ flag `16`, 5 GHz; `0` ⇒ slot vacío con flags `0`).

Además, los 2 B crudos de cada entrada se copian al bloque de ivars del peer, en
`*(peer+32) + 1368 + 2·i`.

### 2.2 Conversión al formato interno

En el `awdl_chan_seq_info2` de salida se escribe la cabecera `{n, codificación, flags, step,
u16 relleno}` en `+0..+5` y, desde `+8`, una entrada de 12 B por canal con el mismo formato que
el ioctl 129: `{u32 1, u32 canal, u32 flags}`. El formato de 12 B es por tanto **interno del
kernel**: en el aire cada slot ocupa 2 B.

Errores: un cursor de menos de 9 B, una longitud que no cubre `2·n` B o `n > 32` registran
`"Error: Assert failure"` y devuelven `-1`.

## 3. Resumen

- El tipo real es el de la etiqueta del `switch` más 2: sync params 4, chan seq 18, sync tree 20,
  election v2 24.
- Sync params ocupa 42 B contados desde el inicio del TLV y transporta canal de trabajo, duty cycle,
  longitud de la extensión (debe ser 16), presencia, número de secuencia y dos campos de la
  cabecera fija (`+8` y `+12`) que sólo se usan para medir contención de sincronía.
- La secuencia de canales viaja como hasta 32 entradas de 2 B a partir de `+9` (canal y, con
  codificación 3, clase operativa); el kernel la expande a entradas internas de 12 B.
- Todo lo marcado `?` (convención exacta de desplazamiento del cursor, unidad de los tiempos de la
  cabecera fija, semántica de los bytes `+21..+23` de sync params y nombres de `+5..+8` de la secuencia) queda abierto.
