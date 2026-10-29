# Preguntas abiertas del parseo AWDL (I): elección v2, línea temporal y campos sin nombre

Este documento responde tres preguntas que quedaron abiertas en `AWDL_FRAME_WIRE_FORMAT.md` y
`AWDL_TLV_BODIES.md`: por dónde entra el TLV de parámetros de elección v2 (tipo 24 en el aire, que
cae en el caso por defecto del `switch` de `actionFrameReport`), qué hace
`IO80211AWDLPeer::monitorPeersTimeline` con los dos tiempos de la cabecera fija del action frame y
con el dword `+48` de la cabecera de metadatos, y cómo se llaman los bytes aún sin nombre del TLV de
sync params (`+21..+23`) y de la secuencia de canales (`+5..+8`). Lo no confirmado se marca `?`.

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
IDB `Build/ida_awdl/task_awdl_open1/kc.i64` (copia de la de `task_awdl_pm_abi`). Direcciones VA del
kernelcache. Recordatorio: el `switch` de `actionFrameReport` (`0xffffff80021af6be`) opera sobre
`type - 2` (resta en `0xffffff80021b03ce`), así que la etiqueta 22 del `switch` es el tipo 24 en el
aire; los desplazamientos de TLV citados aquí cuentan desde el inicio de la cabecera TLV de 3 B,
como en `AWDL_TLV_BODIES.md`.

Funciones de referencia nuevas:

| Función | Dirección | Papel |
|---|---|---|
| `IO80211AWDLPeer::doMinimalAFProcessing` | `0xffffff80021b3ea6` | segundo recorrido de TLVs: sync params, elección v2, HT cap, chan seq |
| `IO80211AWDLPeer::parseAwdlElectionParamsV2` | `0xffffff80021ba26c` | TLV de elección v2 (tipo 24) |
| `IO80211AWDLPeer::parseAwdlElectionParamsV1` | `0xffffff80021b50ec` | TLV de elección v1 (tipo 5, vía `switch`) |
| `IO80211AWDLPeer::monitorPeersTimeline` | `0xffffff80021b86ec` | desviación de la línea temporal del par |
| `IO80211AWDLPeer::calculatePeerTimeDiff` | `0xffffff80021ba5a6` | fórmula de la desviación |
| `IO80211AWDLPeerManager::getCurrentAWSeqNum` | `0xffffff800223507a` | AW actual (ivars +16104) |
| `IO80211AWDLPeerManager::getCurrentAWSlot` | `0xffffff800223508a` | slot actual (ivars +16128) |
| `IO80211AWDLPeerManager::buildAWDLElectionParamsVer2` | `0xffffff8002210a3c` | construcción del TLV 24 en las plantillas |
| `IO80211AWDLPeer::updateAwdlChanSeq` | `0xffffff80021ab7e4` | aplica la secuencia de canales del par |
| `IO80211AWDLPeerManager::reportDataPathEvents` | `0xffffff80021dd8f4` | eventos 41/42/43: número de AW y slot |
| `IO80211AWDLPeerManager::monitorTimeline` | `0xffffff80021e11ca` | contraste del `FwAwStartTime` del evento 41 |
| `IO80211AWDLPeerManager::logAWStart` | `0xffffff80021e1b42` | log de arranque de ventana y copia del tiempo |
| `IO80211AWDLPeerManager::getAwStartFwTimeUsec` | `0xffffff8002234f5e` | lector de `ivars+796` (con byteswap) |

## 1. TLV de parámetros de elección v2 (tipo 24): ruta de parseo y cuerpo

### 1.1 La ruta: `doMinimalAFProcessing`, no el `switch`

El tipo 24 cae en el caso por defecto del `switch` de `actionFrameReport`
(`0xffffff80021b0cb2`, que sólo recupera un registro y salta a `0xffffff80021b133b` para continuar
el recorrido). Su parseo está en el segundo recorrido que `actionFrameReport` delega:
`IO80211AWDLPeer::doMinimalAFProcessing(const actionFrameReportContext*)`, llamado en
`0xffffff80021b013f` (única llamada del binario), justo después del bloque del TLV de grupo de
tiempo real. `actionFrameReport` comprueba el retorno en `0xffffff80021b0144`: un valor 2 salta a
`0xffffff80021b016b` y descarta el AF; otros valores continúan hacia `shouldStopAFProcessing`.

`doMinimalAFProcessing` (`0xffffff80021b3ea6`, 0xa11 B) reconstruye dos cursores a partir del
contexto: el del cuerpo (puntero desde la celda `+0x168` de la vtable del `IO80211Buffer` guardado
en el contexto — celda que no figura en la tabla de `AWDL_PEERMANAGER_DRIVER_ABI.md` — y longitud
desde `+0x130`, `getLength`) y el de la cabecera de metadatos (contexto `a2[3]`/`a2[4]`, que
`parseAwdlSyncParamsTlv` recibe como tercer argumento). Recorre los TLVs desde `buf+16` con el mismo
`IO80211TLVIterator<IO80211TLVHeader<unsigned char, unsigned short, 0, 1, 0>>` y despacha por el
**tipo en crudo** (sin la resta de 2 del primer `switch`): 4 (sync params), 7 (HT cap), 17
(contenedor IEEE 802.11), 18 (secuencia de canales), 24 (elección v2) y 32 (secuencia extendida);
los demás tipos sólo avanzan el iterador.

Condición y validaciones de la llamada a `parseAwdlElectionParamsV2` (desmontaje
`0xffffff80021b4264` .. `0xffffff80021b42ae`):

1. bit 0 del byte `+0x57` del `actionFrameReportContext` a 1 (flag de UMI; `actionFrameReport` lo
   fija y lo limpia alrededor del procesado del TLV de estado de datapath, coherente con la traza
   `"parsing the  RTG TLV in UMI"` de la misma zona) `?` condición exacta de fijado;
2. longitud del TLV (`*(u16*)(tlv+1)`) `>= 0x28` (`cmp ax, 0x28` en `0xffffff80021b426c`);
3. caben 43 B (`0x2B`) desde el cursor (`cmp rax, 0x2A` en `0xffffff80021b4294`);
4. longitud **exactamente 40** (`cmp r13d, 0x28` en `0xffffff80021b429a`).

El fallo de 2 o 4 va a `0xffffff80021b4742`: traza
`"%s:%u Error Invalid length : %lu\n"` (línea de fuente 3918) y retorno 2, que `actionFrameReport`
convierte en descarte del AF completo. Es decir: en un UMI, un TLV de elección v2 malformado no se
ignora silenciosamente como los tipos desconocidos, sino que aborta el frame.

El mismo recorrido parsea los otros TLVs con estas validaciones (todas en `doMinimalAFProcessing`):

- tipo 4: cursor ≥ `0x2A` (42 B), `len >= 0x27` (39) y el byte `tlv+36` (cabecera de la secuencia de
  canales embebida) `<= 0x1F`; si no, `"%s:%u Error Invalid length : %u\n"` (3903); si
  `parseAwdlSyncParamsTlv` devuelve 0, `"Unable to parse Sync Parameters from ..."`, línea 3898.
- tipo 7: `len >= 29`, sub-cursor `{tlv+5, len-2}`; llamada a `IO80211Peer::parseHTCapTLV`
  (`0xffffff80021b4257`).
- tipo 18: `len >= 6` (fallo 3941) y llamada a `parseAwdlChanSeqFromChanSeqTLV`.
- tipo 32: llamada a `parseAwdlChanSeqFromChanSeqExtTLV` (sólo con el flag SDB del manager activo).
- tipo 17: llamada a `parseAwdlIEEE80211CntnrTLV`; retorno negativo ⇒ `"Error Parameter : %lu"`.
- Además registra el descubrimiento del par (`"New Peer ... discovered and msecSinceAwdlOn"`, línea
  3881; `"discovered and (numSyncActionFrameRx: %u, postedtoNDP:%s..."`, 3885) y marca
  `mach_absolute_time()` en el ivar del peer `+736`.

`parseAwdlElectionParamsV1` (`0xffffff80021b50ec`), en cambio, sí se invoca desde el primer
`switch` (caso del tipo 5, llamada en `0xffffff80021b0a77`) y trabaja con un cuerpo de 24 B
(`cursor >= 0x18`), con destino a los ivars del peer `+606..+632` (root MAC `+606`, `+610`,
`+612`, `+624`, `+628`, `+632`) — forma antigua, sin relación con el bloque `+556` de la v2.

### 1.2 Validaciones dentro de `parseAwdlElectionParamsV2`

`0xffffff80021ba26c`:

1. El cursor (que incluye la cabecera TLV de 3 B) debe medir al menos `0x2B` (43 B). Si no, traza
   `"%s::%s Invalid AwdlElectionParamsV2\n"` y retorno.
2. El campo de longitud debe valer **exactamente 40** (`*(u16*)(tlv+1) != 40` ⇒ traza
   `"%s::%s Invalid SyncTreeTLV Length\n"` — texto heredado de la v1 — y retorno). Es decir: TLV
   total 43 B, cuerpo de 40 B. El límite coincide con el que impone el llamante (§1.1).

### 1.3 Cuerpo del TLV (40 B de cuerpo, 43 B totales)

El constructor del manager `buildAWDLElectionParamsVer2` (`0xffffff8002210a3c`) escribe el mismo
layout que el parser lee; los offsets de la tabla son desde el inicio del TLV (el cuerpo empieza en
`+3`), todos los campos multi-byte en **orden little-endian** (se leen y se escriben con accesos
nativos, sin byteswap):

| TLV+ | Cuerpo+ | Tamaño | Campo | Destino en el peer (ivars) |
|---:|---:|---:|---|---|
| `+0` | — | 1 | tipo (24) | — |
| `+1` | — | 2 | longitud (40) | validada, no almacenada |
| `+3` | `+0` | 6 | MAC raíz (root) | `+559` (comparada con la raíz del manager `+37231`) |
| `+9` | `+6` | 6 | MAC del padre (parent) | `+565` (dentro del bloque copiado) |
| `+15` | `+12` | 4 | `rootLFC` (frame count del root) | `+571` |
| `+19` | `+16` | 4 | `hopCount` | `+575` |
| `+23` | `+20` | 4 | `rootMetric` | `+579` |
| `+27` | `+24` | 4 | `selfMetric` | `+583` |
| `+31` | `+28` | 4 | `electionId` | `+587` |
| `+35` | `+32` | 4 | a cero (el constructor escribe el `electionId` con un store de 8 B zero-extendido) `?` | — |
| `+39` | `+36` | 4 | `selfLFC` | `+591` (el constructor lo toma de ivars `+37560`) |

El parser copia el TLV completo (43 B, cabecera incluida) en el ivar del peer `+556` con
`IO80211BufferCursor::copyFrom`; por eso los campos del cuerpo quedan en `+559`, `+571`, `+575`,
`+579`, `+583` y `+587`. **Corrección** a `AWDL_PEERMANAGER_SOFTWARE.md` §8: el bloque de 6 B en
`+559` no es "electionId de 6 B" sino la **MAC raíz** — la evidencia es el `memcmp` de 6 B contra
la MAC raíz del manager (`+37231`) tanto en `parseAwdlElectionParamsV2` como en la comparación de
`runParentElection`; el `electionId` de 32 bits está en `+587` (mismo offset que la doc ya citaba).

Comparaciones y consecuencias en `parseAwdlElectionParamsV2`:

- `memcmp` previo de la MAC padre del manager (`+37237`) contra la MAC de origen del AF (contexto
  `+88`, 6 B): marca si el emisor es el padre electoral actual.
- Mismo root que el almacenado (`tlv+3` == peer `+559`) y `rootLFC` nuevo (`tlv+15`) **menor** que
  el almacenado (`+571`): retorno sin tocar nada (frame viejo del mismo root).
- Si los 43 B son idénticos a la copia previa (`+556`, `memcmp` de `0x2B`): nada que forzar; sólo
  se refresca `+105`.
- Si son distintos: se copian a `+556` y, si el emisor es el padre actual, traza
  `"%s:%u forceElection bec Parent changed election params\n"` (línea de fuente 5903) y se fuerza
  elección; si el emisor no es el padre, se fuerza sólo si el root anunciado "gana" al del manager
  (`rootMetric` del peer `+579` > `rootMetric` del manager `+37251`, o igualdad con MAC raíz mayor
  por `memcmp`), con la traza `"%s:%u forceElection bec of better Root\n"` (5923); si el peer ya
  apunta al mismo root que el manager pero el hop count del manager supera en más de 1 al anunciado
  (`+37247 > peer+575 + 1`), se renuncia con `"%s:%u NO forceElection bec of HC change  \n"` (5915).
- Al forzar: flag del manager `+10306` (elección reprogramada, la que atiende `runElection`) a 1 y
  peer `+105` = (MAC raíz del peer == MAC raíz del manager).

Para un driver que emite election params v2: cuerpo de 40 B, little-endian, con los ocho campos de
la tabla; la longitud debe ser exactamente 40 y el TLV se procesa en AFs tipo UMI.

## 2. `IO80211AWDLPeer::monitorPeersTimeline`: phy_tx_time, target_tx_time y el dword +48

### 2.1 Entrada

Única llamada: `parseAwdlSyncParamsTlv`, en `0xffffff80021b508a`:

```c
IO80211AWDLPeer::monitorPeersTimeline(peer, tlv, v18, v17, *(uint32 *)(hdr + 48));
```

con `v18 = *(u32*)(buf+8)` (cabecera fija `+8`, `phy_tx_time`), `v17 = *(u32*)(buf+12)`
(`target_tx_time`) y el dword `+48` (`+0x30`) de la cabecera de metadatos de 72 B. Firma mangled:
`(const IO80211BufferCursor&, uint, uint, uint)`. La llamada está al final de
`parseAwdlSyncParamsTlv` y sólo se alcanza cuando el parseo ha superado sus validaciones (cursores
no nulos, presencia no nula); las salidas tempranas (assert de la línea 3399 y
`"Zero presence Mode"`) no llegan a llamarla. Se ejecuta tanto por la ruta del `switch` como por la
de `doMinimalAFProcessing`, independientemente de que `+8`/`+12` valgan cero.

### 2.2 El cálculo (`calculatePeerTimeDiff`, `0xffffff80021ba5a6`)

Unidades: **microsegundos** en el dominio del reloj de firmware del receptor (el mismo del
`FwAwStartTime` del evento 41); 1 TU = 1024 µs (`<< 10`); el periodo completo de la línea temporal
es 2^20 µs = 1024 TU (16 ventanas de 64 TU), y la aritmética intermedia es de 32 bits.

Estado del manager que usa:

- `currAwNum` = retorno de la celda 95 de la vtable del manager (offset `+0x2F8`, llamada en
  `0xffffff80021b8769`), `getCurrentAWSeqNum` (`0xffffff800223507a`, lectura de `ivars+16104` en
  `0xffffff8002235082`);
- `currAwStartFwTime` = `byteswap(*(u32*)(ivars+796))` (carga en `0xffffff80021b8771`, `bswap` en
  `0xffffff80021b877c`) menos **3500 µs** de guarda (`add r13d, 0FFFFF254h` en
  `0xffffff80021b877f`).

Fórmula verificada en el descompilado (`tlv` = puntero al TLV de sync params con cabecera):

```c
peerNextAwNum = pm + sn - sn % pm;             // pm = tlv[30] (u8), sn = *(u16*)(tlv+32), todo u16
v8 = (currAwStartFwTime - 3500)
     + *(u16*)(tlv+8) * (peerNextAwNum - currAwNum) << 10;   // *(u16*)(tlv+8) = AW en TU
v9 = (*(u16*)(tlv+4) << 10)                     // dc (tlv+4) en TU → µs
     + target_tx_time (buf+12)
     + afReceiveFwTime (hdr+48)
     - phy_tx_time (buf+8);
d  = |v8 - v9|;  r = d & 0xFFFFF;               // módulo 2^20 µs
return (r < 0x80001) ? r : r - 0x100000;        // centrado: [−0x7FFFF, +0x80000] µs
```

Lecturas: `pm` (TLV+30, presencia), `sn` (TLV+32, número de secuencia del par — el log lo llama
`afAwNum`), `*(u16*)(tlv+8)` = duración de la ventana de disponibilidad en TU (campo `+8`, ivar del
peer `+762`), `*(u16*)(tlv+4)` = `dc` (TLV+4, ivar `+758`; el log lo imprime como `downCounter`
multiplicado por 1024). El término `afReceiveFwTime − phy_tx_time` es el tiempo de vuelo del frame
(recepción menos emisión física); `target_tx_time` es el sello del emisor. La comparación enfrenta
el inicio previsto de la ventana del par (v8, en el reloj propio) contra el instante de emisión del
AF corregido con el vuelo y `dc` (v9); el signo se pierde (`d` es un valor absoluto), así que la
desviación mide distancia, no adelanto ni retraso `?` (así se lee del desmontaje; la intención del
diseño no es verificable). Como la reducción final es módulo 2^20 µs y el producto va multiplicado
por 1024, la desviación sólo depende de la posición del frame dentro del periodo de 16 ventanas.

El cursor con menos de 42 B o nulo produce `"%s::%s[%u] Error: Invalid sync params\n"` (fuente
línea 4185) y diferencia 0.

### 2.3 Uso de la diferencia en `monitorPeersTimeline` (`0xffffff80021b86ec`)

- Guarda de entrada: cursor del TLV ≥ `0x2A` (42 B) y no nulo (comparación en
  `0xffffff80021b8728`); en caso contrario traza
  `"%s::%s() Peer %02X:...: Invalid sync params\n"` y no se toca el peer.
- `peer+192` (dword) = diferencia devuelta (µs, con el centrado modular descrito; store en
  `0xffffff80021b87b3`).
- Umbral: `0x199A` = **6554 µs** (≈ 6,55 ms ≈ 1/10 de la ventana de 65,536 ms); el valor absoluto y
  la comparación están en `0xffffff80021b87bc` (`neg`/`cmovs`) y `0xffffff80021b87c3`
  (`cmp r15d, 199Ah`):
  - Con |shift| ≥ 6554 y bit 0 del byte `peer+1667` a 1 (estaba sincronizado): traza
    `"%s::%s() Peer ...: currAwNum = %d, currAwStartFwTime = %u, afReceiveFwTime = %u, afAwNum = %d, downCounter = %d"`
    (con `afAwNum = sn` y `downCounter = dc << 10`), traza
    `"%s::%s() Peer ...: timeline is shifted by %8d[usec] (%3d[AWs] & %3d[msec]) relative to current timeline"`
    (AWs = shift/65536; ms = resto/1000), marca `mach_absolute_time()` en `peer+176` (instante en
    que dejó de estar sincronizado), muestrea el tiempo transcurrido (`now − peer+184`, es decir la
    duración del estado "sincronizado" que termina, convertida a segundos) en el histograma
    `_syncStats` (10 bins) y traza `"%s::%s() Peer ...: Became UnSynced\n"`.
  - Con |shift| < 6554 y el bit a 0 (no estaba sincronizado): marca `mach_absolute_time()` en
    `peer+184`, muestrea `now − peer+176` (duración del estado "no sincronizado") en el histograma
    `unk_FFFFFF8002488750` (10 bins) y traza `"%s::%s() Peer ...: became synced\n"`.
- Siempre: muestreo booleano en el histograma `unk_FFFFFF80024889A8` (2 bins) con 1.0 si
  |shift| < 6554 y 0.0 en caso contrario, y `bit 0 de peer+1667 = (|shift| < 6554)`.

Consecuencias resumidas: el bit `peer+1667.0` es el estado "sincronizado con este par" (el
constructor del peer lo inicializa a 0); `peer+192` guarda la última desviación en µs; `peer+176` y
`peer+184` son las marcas de cuándo quedó no sincronizado y cuándo sincronizado. `?` consumidores
del bit `+1667` fuera de esta función y del init del peer (ninguno localizado en las funciones
analizadas).

### 2.4 De dónde salen `currAwNum` y `currAwStartFwTime`

- `ivars+16104` (`currAwNum`): el manejador del evento 41
  (`APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`) dentro de `reportDataPathEvents` hace
  `ivars+16104 = *(u32*)(payload+0)` (store en `0xffffff80021dfbb5`); el `AwNumber` debe ir en los
  16 bits bajos `?` sobre los 16 bits altos. Del mismo número deriva el slot: con
  `n = ivars+42820` (canales de la secuencia) y `s = ivars+42867`,
  `slot = (awNum % ((s+1)·n)) / (s+1)` con tope 15, escrito en `ivars+16128` (`0xffffff80021e0226`).
- `currAwStartFwTime` (`ivars+796`): **la escribe `monitorTimeline`**. Al terminar (etiqueta común
  del final, stores en `0xffffff80021e18ee` .. `0xffffff80021e191f`) copia el payload del evento 41
  **en bruto** a `ivars+776..811` (`payload+0` → `+776`, `payload+8` → `+784`, `payload+16` →
  `+792`, `payload+24` → `+800`, `payload+32` → `+808`); así `FwAwStartTime` (`payload+20`,
  **big-endian**) queda en `+796` tal cual lo entregó el driver. Los lectores le aplican
  `_byteswap_ulong`: `monitorPeersTimeline` (`0xffffff80021b8771`: carga `[ivars+31Ch]` y `bswap`
  en `0xffffff80021b877c`), `getAwStartFwTimeUsec` (`0xffffff8002234f5e`, lectura en
  `0xffffff8002234f66`) y el propio `monitorTimeline` (`0xffffff80021e126a`).
- `monitorTimeline` además valida el reporte: con el número de AW recibido y el slot calculado
  deduce un arranque esperado (`stored + slots·16384 µs`, cada slot = 16 TU = 16384 µs, producto
  `<< 14`) y si la desviación contra el `FwAwStartTime` recibido alcanza `0x1401` (5121 µs) registra
  `"%s::%s() Detected AW start time inaccuracy: expetedAwStartFwTime = %u, currAwStartFwTime = %u,
  delta = %d [usec]\n"` (línea de fuente 32402); también detecta saltos de número de AW sin cambio
  de maestro (`"Detected AW Number discontinuity..."`). Es decir, el reloj de ventanas que el driver
  reporta debe avanzar de forma continua y coherente con la secuencia de canales programada.
- `logAWStart` (`0xffffff80021e1b42`) re-guarda el valor ya convertido (`byteswap(payload+20)`) en
  `ivars+42668` (`0xffffff80021e1b8f`) y emite el log canónico
  `"start:/Start:%u.%03u[fw:%u.%03u.%03u] sn=%03u/%02u ch=%3u ... [RLFC:%u HC:%u Rmet:%u SMet:%u
  SLFC:%u] nC:%u R:%u O:%u NR:%u pkt %u"` (RLFC = `+37243`, HC = `+37290`, Rmet = `+37294`,
  SMet = `+37298`, SLFC = `+37560`).

### 2.5 Qué debe rellenar un driver sin firmware AWDL

- `hdr+48` (`afReceiveFwTime`): el kernel lo lee **sin conversión de orden** (acceso directo
  `*(u32*)(hdr+48)`); el productor de Broadcom escribe ahí el valor ya convertido del evento de RX
  (`_byteswap_ulong`), es decir, en orden nativo. Un driver propio debe escribir en `+48` la marca
  de recepción del frame en **µs y orden nativo (little-endian)**, en el mismo dominio de reloj que
  el `FwAwStartTime` que reporta por el evento 41 (que se almacena big-endian y se lee con byteswap).
- `buf+8` (`phy_tx_time`) y `buf+12` (`target_tx_time`): viajan en el aire; los escribe el emisor.
  Para que un par aparezca sincronizado debe cumplir
  `target_tx_time + (afReceiveFwTime − phy_tx_time) + dc·1024 ≡ (currAwStartFwTime − 3500) +
  (peerNextAwNum − currAwNum)·AW_TU·1024` (módulo 2^20 µs) con error < 6554 µs. Un emisor coherente
  estampa `target_tx_time` con el inicio de su ventana de disponibilidad y `phy_tx_time` con el
  instante real de TX, ambos en su reloj fw `?` (deducción de la fórmula; ninguna constante del
  binario lo confirma).
- Con los tres campos a cero el parseo de TLVs no se ve afectado (lo dicho en
  `AWDL_FRAME_WIRE_FORMAT.md` §5.4 sigue siendo cierto), pero `monitorPeersTimeline` igualmente se
  ejecuta y la desviación resultante marcará el par como **no sincronizado** (`peer+1667.0 = 0`,
  traza `Became UnSynced` en el primer AF con TLV de sync params): **corrección** al doc anterior,
  que sugería que no había consecuencia alguna. Un driver que quiera pares "sincronizados" debe
  rellenar los tres campos con valores coherentes entre sí y con el `FwAwStartTime` del evento 41.
- `sync_contention` (`peer+752`): sólo se calcula si `phy_tx_time` y `target_tx_time` son distintos
  de cero y `phy_tx_time > target_tx_time` (`parseAwdlSyncParamsTlv`); con ceros no se registra
  contención. Es el único efecto de `+8`/`+12` fuera de `monitorPeersTimeline`.

## 3. Bytes sin nombre: sync params `+21..+23` y secuencia de canales `+5..+8`

### 3.1 Sync params `+20..+23` ("ext")

La única evidencia de nombres en el binario es la traza de `parseAwdlSyncParamsTlv` (condicionada a
`shouldLog(0x10000)`):

```c
"   sn=%u ext(%u/%u/%u/%u) dc=%u rem=%u tchan=%u mchan=%u pm=%u sync_contention:%u\n"
```

que imprime los cuatro bytes "ext" en el orden **TLV+20, TLV+23, TLV+21, TLV+22** (y confirma los
nombres del resto: `sn`=+32, `dc`=+4, `rem`=+18, `tchan`=+3, `mchan`=+6, `pm`=+30). Comportamiento
verificado:

- `TLV+20` se copia al ivar del peer `+1452`; `TLV+21`, `TLV+22` y `TLV+23` no se almacenan en
  ningún ivar.
- Ningún consumidor de `peer+1452` ha aparecido en las funciones analizadas (ni en los volcados de
  las tareas previas del PeerManager) `?`.
- Los nombres de los cuatro bytes quedan `?`: el manager no construye nunca un TLV de sync params
  (la plantilla de master indication de `updateSyncFrameTemplate` `0xffffff80021dc5b2` sólo lleva
  los TLVs 6, 24, 12, 7±, 30±, 31±, 17±, 21, 20±, 23 y 33), así que en el camino de Broadcom estos
  bytes los genera el firmware y el kernel se limita a mostrarlos.

### 3.2 Secuencia de canales `+5..+8`

Los cuatro bytes siguen vivos tras el parseo. En `parseAwdlChanSeqFromChanSeqTLV`
(`0xffffff80021b5a08`) se copian al `awdl_chan_seq_info2` de salida: `TLV+5` → `info2+2`,
`TLV+6` → `info2+3` y `*(u16*)(TLV+7)` → `info2+4` (junto a `count` en `info2+0` y `encoding`
`TLV+4` en `info2+1`).

`updateAwdlChanSeq` (`0xffffff80021ab7e4`) los aplica al estado del peer con aritmética explícita:

- `peer+908` = encoding (`info2+1`); `peer+909` = `min(count, 16)` (recorte a 16 entradas);
- `peer+911` = `TLV+5`; `peer+910` = `TLV+6`; `peer+916` (u16) = `TLV+7..8` sin transformación;
- **periodo en slots** = `(TLV+5 + 1) · min(count,16) · (TLV+6 + 1)`, escrito en `peer+906` (u16),
  y exigido en el rango `[1, 512]`: fuera de ese rango la función devuelve −2 sin aplicar nada;
- las entradas (12 B: `{u32 1, u32 canal, u32 flags}` desde `info2+8`) se comparan con la copia
  previa (`peer+924`/`peer+928`, máscara de flags `0x2018`) y se recopian a `peer+920..`.

El rol de `TLV+6` queda además confirmado en el propio manager: su copia (`ivars+42867`, campo
`+0x07` del struct del ioctl 129) determina cuántas ventanas dura cada canal en el reparto de slots
del evento 41 (`slot = (awNum % ((s+1)·n)) / (s+1)`, con `n = ivars+42820`): es decir,
**`TLV+6` = número de ventanas por canal menos 1** (el paso de la secuencia), y **`TLV+5` multiplica
el periodo completo** (`TLV+5 + 1` repeticiones). `TLV+7..8` no participa en ninguna aritmética
verificada `?`.

Los nombres canónicos del protocolo (`duplicate count` para `+5`, `step count` para `+6`, `fill
channel` para `+7..8`) provienen de los desectores públicos de AWDL y no son verificables desde este
binario `?`; lo verificado es la aritmética de arriba. La variante embebida en sync params repite la
misma estructura cuatro posiciones después: en `parseAwdlChanSeqFromSyncParamsTLV`
(`0xffffff80021b8196`) la cabecera {`count−1`, `encoding`, `+38`, `+39`, u16 `+40`} está en
`TLV+36..41`, y el byte `TLV+36` es el que `doMinimalAFProcessing` exige `<= 0x1F`.

## 4. Resumen operativo

- El TLV 24 (elección v2) se parsea en `doMinimalAFProcessing` (`0xffffff80021b3ea6`), el segundo
  recorrido que `actionFrameReport` dispara en `0xffffff80021b013f`, y sólo si el AF es UMI
  (contexto `+0x57` bit 0) y la longitud vale exactamente 40; un TLV malformado descarta el frame
  completo (`"Error Invalid length"`, retorno 2).
- El cuerpo v2 son 40 B little-endian: MAC raíz (+0), MAC padre (+6), rootLFC (+12), hopCount (+16),
  rootMetric (+20), selfMetric (+24), electionId (+28), selfLFC (+36); el parser lo copia íntegro en
  peer `+556` y fuerza reelección cuando cambia lo anunciado por el padre o aparece un root mejor.
- `monitorPeersTimeline` convierte `phy_tx_time` (buf+8), `target_tx_time` (buf+12) y `hdr+48`
  (marca de recepción en µs nativas) en una desviación de línea temporal módulo 2^20 µs, guardada en
  `peer+192`; con desviación ≥ 6554 µs el par queda "no sincronizado" (bit `peer+1667.0` a 0, con
  trazas e histogramas). Un driver sin firmware debe estampar los tres tiempos de forma coherente
  con su `FwAwStartTime` del evento 41 si quiere pares sincronizados; a cero, el par queda
  permanentemente no sincronizado.
- El estado que alimenta la línea temporal viaja por el evento 41: `reportDataPathEvents` escribe
  `ivars+16104` (número de AW, payload+0) y `ivars+16128` (slot), y `monitorTimeline` copia el
  payload en bruto a `ivars+776..811`, de modo que el `FwAwStartTime` big-endian de `payload+20`
  queda en `ivars+796`, que es el valor que `monitorPeersTimeline` usa (con byteswap) como arranque
  de la ventana actual; el reporte debe ser continuo y preciso (desviaciones ≥ 5121 µs se registran
  como "AW start time inaccuracy").
- De los bytes sin nombre: `+20..+23` de sync params sólo se imprimen (orden `+20, +23, +21, +22`)
  y `+20` va a `peer+1452` sin consumidor conocido; `+5..+8` de la secuencia de canales son los
  factores del periodo (`+5+1` repeticiones, `+6+1` ventanas por canal, producto ≤ 512 slots) y un
  u16 copiado tal cual (`+7..8`).
