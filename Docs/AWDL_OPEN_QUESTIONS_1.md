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
| `IO80211AWDLPeer::doMinimalAFProcessing` | `0xffffff80021b3ea6` | ruta corta de AF: HT cap, sync params y elección v2 |
| `IO80211AWDLPeer::parseAwdlElectionParamsV2` | `0xffffff80021ba26c` | TLV de elección v2 (tipo 24) |
| `IO80211AWDLPeer::parseAwdlElectionParamsV1` | `0xffffff80021b50ec` | TLV de elección v1 (tipo 5, vía `switch`) |
| `IO80211AWDLPeer::monitorPeersTimeline` | `0xffffff80021b86ec` | desviación de la línea temporal del par |
| `IO80211AWDLPeer::calculatePeerTimeDiff` | `0xffffff80021ba5a6` | fórmula de la desviación |
| `IO80211AWDLPeerManager::getCurrentAWSeqNum` | `0xffffff800223507a` | AW actual (ivars +16104) |
| `IO80211AWDLPeerManager::getCurrentAWSlot` | `0xffffff800223508a` | slot actual (ivars +16128) |
| `IO80211AWDLPeerManager::buildAWDLElectionParamsVer2` | `0xffffff8002210a3c` | construcción del TLV 24 en las plantillas |

## 1. TLV de parámetros de elección v2 (tipo 24): ruta de parseo y cuerpo

### 1.1 La ruta: `doMinimalAFProcessing`, no el `switch`

El tipo 24 cae en el caso por defecto del `switch` (`0xffffff80021b0cb2`, que sólo recupera un
registro y salta a `0xffffff80021b133b` para continuar el recorrido). Su parseo no está en el
recorrido de TLVs sino en la llamada que `actionFrameReport` hace a
`IO80211AWDLPeer::doMinimalAFProcessing` (único llamante, en `0xffffff80021b013f`). Esa función es la que hace el parseo fino de tres TLVs ya localizados: HT cap
(`IO80211Peer::parseHTCapTLV`, llamada en `0xffffff80021b4257`), sync params
(`parseAwdlSyncParamsTlv`, llamada en `0xffffff80021b448e`) y elección v2
(`parseAwdlElectionParamsV2`, llamada en `0xffffff80021b42ae`). `parseAwdlElectionParamsV2` no tiene
ningún otro llamante en el binario; `parseAwdlSyncParamsTlv` tiene dos (`actionFrameReport`
`0xffffff80021b0f3a` por el caso 4 del `switch`, y `doMinimalAFProcessing` `0xffffff80021b448e`).

Condiciones que rodean la llamada a `parseAwdlElectionParamsV2` (desmontaje `0xffffff80021b4264`
.. `0xffffff80021b42ae`):

- bit 0 del byte `+0x57` del `actionFrameReportContext` a 1 (flag de UMI; `actionFrameReport` lo
  fija/limpia en el procesado del TLV de estado de datapath, coherente con la traza
  `"parsing the  RTG TLV in UMI"` de la misma zona) `?` condición exacta de fijado;
- longitud del TLV (`*(u16*)(tlv+1)`) `>= 0x28` (`cmp ax, 0x28` en `0xffffff80021b426c`) y el
  registro de comparación igual a `0x28` (`cmp r13d, 0x28` en `0xffffff80021b429a`);
- caben 43 B (`0x2B`) desde el cursor (`cmp rax, 0x2A` en `0xffffff80021b4290`, salto en `0xffffff80021b4294`).

`?` cómo obtiene `doMinimalAFProcessing` el cursor del TLV 24 (búsqueda propia con
`findTLVCursorForType`/`getTlvPtrForType` o cursor heredado del contexto): se detalla al cerrar el
análisis de la función.

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

- `memcmp` previo del MAC padre del manager (`+37237`) contra la MAC de origen del AF (contexto
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
la tabla; la longitud debe ser exactamente 40 y el TLV aparece en AFs tipo UMI.

## 2. `IO80211AWDLPeer::monitorPeersTimeline`: phy_tx_time, target_tx_time y el dword +48

### 2.1 Entrada

Única llamada: `parseAwdlSyncParamsTlv`, en `0xffffff80021b508a` (descompilado, línea final de la
función):

```c
IO80211AWDLPeer::monitorPeersTimeline(peer, tlv, v18, v17, *(uint32 *)(hdr + 48));
```

con `v18 = *(u32*)(buf+8)` (cabecera fija `+8`, `phy_tx_time`), `v17 = *(u32*)(buf+12)`
(`target_tx_time`) y el dword `+48` (`+0x30`) de la cabecera de metadatos de 72 B. Firma mangled:
`(const IO80211BufferCursor&, uint, uint, uint)`.

### 2.2 El cálculo (`calculatePeerTimeDiff`, `0xffffff80021ba5a6`)

Unidades: **microsegundos** en el dominio del reloj de firmware del receptor (el mismo del
`FwAwStartTime` del evento 41); 1 TU = 1024 µs (`<< 10`); el periodo completo de la línea temporal
es 2^20 µs = 1024 TU (16 ventanas de 64 TU).

State del manager que usa:

- `currAwNum` = retorno de la celda 95 de la vtable del manager (offset `+0x2F8`),
  `getCurrentAWSeqNum` (`0xffffff800223507a`), que lee el u32 en ivars AWDL `+16104`;
- `currAwStartFwTime` = `byteswap(*(u32*)(ivars+796))` (almacenado **big-endian**) menos **3500 µs**
  de guarda. Ambos valores los prepara `monitorPeersTimeline` antes de llamar: celda 95 en
  `0xffffff80021b8769`, `bswap` en `0xffffff80021b877c` y `add r13d, 0FFFFF254h` (−3500) en
  `0xffffff80021b877f`; la llamada a `calculatePeerTimeDiff` está en `0xffffff80021b87a5`.

Fórmula verificada en el descompilado (`tlv` = puntero al TLV de sync params con cabecera):

```c
peerNextAwNum = pm + sn - sn % pm;             // pm = tlv[30] (u8), sn = *(u16*)(tlv+32)
v8 = (currAwStartFwTime - 3500)
     + (peerNextAwNum - currAwNum) * *(u16*)(tlv+8) << 10;   // *(u16*)(tlv+8) = AW en TU
v9 = (*(u16*)(tlv+4) << 10)                     // dc (tlv+4) en TU → µs
     + target_tx_time (buf+12)
     + afReceiveFwTime (hdr+48)
     - phy_tx_time (buf+8);
d  = |v8 - v9|;  r = d & 0xFFFFF;               // módulo 2^20 µs
return (r < 0x80001) ? r : r - 0x100000;        // centrado: [−0x7FFFF, +0x80000]
```

Lecturas: `pm` (TLV+30, presencia), `sn` (TLV+32, número de secuencia del par — el log lo llama
`afAwNum`), `*(u16*)(tlv+8)` = duración de la ventana de disponibilidad en TU (campo `+8`, ivar del
peer `+762`), `*(u16*)(tlv+4)` = `dc` (TLV+4, ivar `+758`; el log lo imprime como `downCounter`
multiplicado por 1024). El término `afReceiveFwTime − phy_tx_time` es el tiempo de vuelo del frame
(recepción menos emisión física); `target_tx_time` es el sello del emisor. La comparación enfrenta
el inicio previsto de la ventana del par (v8, en el reloj propio) contra el instante de emisión del
AF corregido con el vuelo y `dc` (v9); el signo se pierde (`d` es un valor absoluto), así que la
desviación mide distancia, no adelanto ni retraso `?` (así se lee del desmontaje; la intención del
diseñador no es verificable).

El cursor con menos de 42 B o nulo produce `"%s::%s[%u] Error: Invalid sync params\n"` (fuente
línea 4185) y diferencia 0.

### 2.3 Uso de la diferencia en `monitorPeersTimeline` (`0xffffff80021b86ec`)

- Guarda de entrada: cursor del TLV ≥ `0x2A` (42 B) y no nulo; en caso contrario traza
  `"%s::%s() Peer %02X:...: Invalid sync params\n"` y no se toca el peer.
- `peer+192` (dword) = diferencia devuelta (µs, con el centrado modular descrito).
- Umbral: `0x199A` = **6554 µs** (`cmp r15d, 199Ah` en `0xffffff80021b87c3` y `0xffffff80021b8c59`) (≈ 6,55 ms = 1/10 de la ventana de 65,536 ms):
  - Con |shift| ≥ 6554 y bit 0 del byte `peer+1667` a 1 (estaba sincronizado): traza
    `"%s::%s() Peer ...: currAwNum = %d, currAwStartFwTime = %u, afReceiveFwTime = %u, afAwNum = %d, downCounter = %d"`
    (con `afAwNum = sn` y `downCounter = dc << 10`), traza
    `"%s::%s() Peer ...: timeline is shifted by %8d[usec] (%3d[AWs] & %3d[msec]) relative to current timeline"`
    (AWs = shift/65536; ms = resto/1000), marca `mach_absolute_time()` en `peer+176`, muestrea el
    tiempo transcurrido (`now − peer+184`, convertido a segundos) en el histograma `_syncStats`
    (10 bins) y traza `"%s::%s() Peer ...: Became UnSynced\n"`.
  - Con |shift| < 6554 y el bit a 0 (no estaba sincronizado): marca `mach_absolute_time()` en
    `peer+184`, muestrea `now − peer+176` en el histograma `unk_FFFFFF8002488750` (10 bins) y traza
    `"%s::%s() Peer ...: became synced\n"`.
- Siempre: muestreo booleano en el histograma `unk_FFFFFF80024889A8` (2 bins) con 1.0 si
  |shift| < 6554 y 0.0 en caso contrario, y `bit 0 de peer+1667 = (|shift| < 6554)`.

Consecuencias resumidas: el bit `peer+1667.0` es el estado "sincronizado con este par";
`peer+192` guarda la última desviación en µs; `peer+176`/`peer+184` son las marcas de los últimos
cambios de estado. `?` consumidores del bit `+1667` fuera de esta función.

### 2.4 Qué debe rellenar un driver sin firmware AWDL

- `hdr+48` (`afReceiveFwTime`): el kernel lo lee **sin conversión de orden** (acceso directo
  `*(u32*)(hdr+48)`); el productor de Broadcom escribe ahí el valor ya convertido del evento de RX
  (`_byteswap_ulong`), es decir, en orden nativo. Un driver propio debe escribir en `+48` la marca
  de recepción del frame en **µs y orden nativo (little-endian)**, en el mismo dominio de reloj que
  el `FwAwStartTime` que reporta por el evento 41 (que `monitorTimeline` almacena big-endian en
  `ivars+796`; verificación del escritor `?` al cerrar el análisis).
- `buf+8` (`phy_tx_time`) y `buf+12` (`target_tx_time`): viajan en el aire; los escribe el emisor.
  Para que un par aparezca sincronizado el par debe cumplir
  `target_tx_time + (afReceiveFwTime − phy_tx_time) + dc·1024 ≡ (currAwStartFwTime − 3500) +
  (peerNextAwNum − currAwNum)·AW_TU·1024` (módulo 2^20 µs) con error < 6554 µs. Con los tres campos
  a cero el parseo de TLVs no se ve afectado (lo dicho en `AWDL_FRAME_WIRE_FORMAT.md` §5.4 sigue
  siendo cierto), pero `monitorPeersTimeline` igualmente se ejecuta y la desviación resultante
  marcará el par como **no sincronizado** (`peer+1667.0 = 0`, traza `Became UnSynced`): **corrección**
  al doc anterior, que sugería que no había consecuencia alguna. Un driver que quiera pares
  "sincronizados" debe rellenar los tres campos con valores coherentes entre sí y con el
  `FwAwStartTime` del evento 41.
- `sync_contention` (`peer+752`): sólo se calcula si `phy_tx_time` y `target_tx_time` son distintos
  de cero y `phy_tx_time > target_tx_time` (`parseAwdlSyncParamsTlv`); con ceros no se registra
  contención.

## 3. Bytes sin nombre: sync params `+21..+23` y secuencia de canales `+5..+8`

Pendiente `?`: el análisis se interrumpió antes de cerrar este apartado. Pista sin verificar: la
traza de `parseAwdlSyncParamsTlv` imprime los bytes de extensión con el formato `ext:%u/%u/%u/%u`,
lo que sugiere cuatro longitudes de extensión de ventana consecutivas a partir de `+20`.
