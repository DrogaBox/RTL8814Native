# Conversión de clase operativa y parser del TLV 18 (cierre de H8)

Cierra el H8 de `AWDL_TX_FRAME_REVIEW.md`: el comportamiento exacto de `convertOpClassToChannel`
del kernel, la cadena de llamadas desde el parser del TLV 18 (secuencia de canales), el efecto de
un fallo de conversión, las codificaciones 0/1/3 y la aceptación de un par que anuncia sólo
algunos slots servidos. Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, 25G241,
Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`), base de datos anotada
`~/re_cache/ida/boot_awdl.kc.i64` (copia en `Build/ida_awdl/task_tlv18/kc.i64`). Lo no observado
se marca `?`. Complementa `AWDL_TLV_BODIES.md` (§2), al que corrige en dos puntos (§4 y §3.2).

## 1. Respuestas en corto

| Pregunta | Respuesta |
|---|---|
| ¿Clases operativas aceptadas? | Exactamente las 24 de `operClassTable`: 81–84, 115–134 (`0xffffff80023ab450`). Cualquier otro byte de clase (incluido 0) ⇒ conversión fallida ⇒ TLV 18 rechazado. |
| ¿128 y `{0,0}`? | 128 sí (entrada 17, 80 MHz, centros `{42,58,106,122,138,155}`). `{0,0}` sí: canal 0 no llega a convertirse (`0xffffff80021b5ba1`). |
| ¿Fallo de conversión? | El parser devuelve `-1` y registra `"fail convert op class, op class 0x%X"`. En la pasada principal (`actionFrameReport`) la secuencia del par no se aplica y la trama no se descarta; en la pasada mínima (`doMinimalAFProcessing`) la secuencia truncada sí llega a aplicarse (§3.4, rareza). |
| ¿Par parcial a 149 aceptado? | Sí (§5): slots `{149,128}` válidos, `{0,0}` sin efecto; el solape y el canal común los calcula el manager por entrada (máscara de banda `0x2018`), y el canal 0 nunca cuenta como solape. |

## 2. `convertOpClassToChannel`

Símbolo `_convertOpClassToChannel` (`0xffffff800223ca18`), sin clase (función plana exportada).
Firma observada:

```c
int convertOpClassToChannel(u8 opClass, u64 chanBitmap, u32 centerBitmap,
                            char *outChan, u8 *outBW, u32 *outBand);
```

- Devuelve `0` si la clase existe en la tabla y `0xE00002BC` (`kIOReturnError`) si no
  (`mov eax, 0E00002BCh` en `0xffffff800223ca5c`). No hay más códigos de error.
- Busca la entrada recorriendo `operClassTable` con paso 70 y comparando el byte 0
  (`cmp dil, [rbx+r11]` en `0xffffff800223ca34`; 24 entradas, `cmp r10, 18h` en
  `0xffffff800223ca56`). Con `chanBitmap = 0` (sin bits) sigue con la siguiente entrada
  (`0xffffff800223ca40`–`0xffffff800223ca4d`).
- Salidas: `outChan = entry.channels[bit]` (`mov al, [rax+r14]; mov [rcx], al` en
  `0xffffff800223ca66`–`6a`, con `rax = 0xffffff80023ab456 = operClassTable+6`), `outBW =
  entry.bw` (`0xffffff800223ca6c`), `outBand = *(u16*)(entry+4)` (`0xffffff800223ca73`).
- Con `centerBitmap` con bits (sólo relevante para 80/160 MHz) sustituye `outChan` por el centro
  correspondiente de las tablas `bw80MHz` (`0xffffff80023abae0`), `bw160MHz5g`
  (`0xffffff80023abbf0`), `bw40MHz6g` (`0xffffff80023abb00`), `bw80MHz6g` (`0xffffff80023abb60`)
  y `bw160MHz6g` (`0xffffff80023abbb0`). El parser AWDL la llama siempre con `centerBitmap = 0`,
  así que el canal de salida queda sin tocar.

### 2.1 `operClassTable` (24 entradas × 70 bytes, `0xffffff80023ab450`)

Layout por entrada: `+0` clase, `+1` ancho (0=20, 1=40, 2=80, 3=160), `+4..+5` banda u16
(`0x2000` = 2,4 GHz, `0x1000` = 5 GHz, `0x4000` = 6 GHz), `+6..+69` lista de 64 canales.
Volcado verificado byte a byte:

| Entrada @VA | Clase | BW | Banda | channels[0..] (no nulos) |
|---|---:|---:|---:|---|
| `...3ab450` | 81 | 0 | 0x2000 | 1–13 |
| `...3ab496` | 82 | 0 | 0x2000 | 14 |
| `...3ab4dc` | 83 | 1 | 0x2000 | 1–9 |
| `...3ab522` | 84 | 1 | 0x2000 | 5–13 |
| `...3ab568` | 115 | 0 | 0x1000 | 36 40 44 48 |
| `...3ab5ae` | 116 | 1 | 0x1000 | 36 44 |
| `...3ab5f4` | 117 | 1 | 0x1000 | 40 48 |
| `...3ab63a` | 118 | 0 | 0x1000 | 52 56 60 64 |
| `...3ab680` | 119 | 1 | 0x1000 | 52 60 |
| `...3ab6c6` | 120 | 1 | 0x1000 | 56 64 |
| `...3ab70c` | 121 | 0 | 0x1000 | 100–144 |
| `...3ab752` | 122 | 1 | 0x1000 | 100–140 (centros) |
| `...3ab798` | 123 | 1 | 0x1000 | 104–144 (centros) |
| `...3ab7de` | 124 | 0 | 0x1000 | 149 153 157 161 |
| `...3ab824` | 125 | 0 | 0x1000 | 149 153 157 161 165 |
| `...3ab86a` | 126 | 1 | 0x1000 | 149 157 |
| `...3ab8b0` | 127 | 1 | 0x1000 | 153 161 |
| `...3ab8f6` | 128 | 2 | 0x1000 | 42 58 106 122 138 155 (centros 80) |
| `...3ab93c` | 129 | 3 | 0x1000 | 50 114 (centros 160) |
| `...3ab982` | 130 | 2 | 0x1000 | 42 58 106 122 138 155 |
| `...3ab9c8` | 131 | 0 | 0x4000 | 1, 5, … 233 (6 GHz, paso 4) |
| `...3aba0e` | 132 | 1 | 0x4000 | 3, 11, … 227 (centros 40 de 6 GHz) |
| `...3aba54` | 133 | 2 | 0x4000 | 7, 23, … 215 (centros 80 de 6 GHz) |
| `...3aba9a` | 134 | 3 | 0x4000 | 15, 47, … 207 (centros 160 de 6 GHz) |

El ancho sale además de `_convertOpClassToBandNBW` (`0xffffff800223cc49`, misma tabla, sólo
`+1`/`+4`). Las tablas de ancho `bw*` usan el canal de salida como clave y un bitmap de 4 u 8
bits (3.er argumento) para elegir el centro; con el bitmap a 0 no modifican nada.

### 2.2 Hermanas y llamadores

- `_convertChannelToOpClass` (`0xffffff800223d15d`): envoltorio de
  `convertChannelToOpClassWithBand` que deduce la banda del canal (`(u8)(ch-1) < 14` ⇒ añade
  `0x1000` a la base `0x1000`, es decir 2,4 GHz). Sin xrefs en este kernelcache `?`.
- `_convertChannelToOpClassWithBand` (`0xffffff800223cc82`): conversión inversa (canal+ancho+banda
  → clase + bitmaps); la usa `buildPrefChanTlv` (`0xffffff8002211516`, `0xffffff80022117b8`) para
  emitir el TLV 33 del manager, y `isChannelOpClassAvailableWithBand` (`0xffffff800223d193`).
- `_convertOpClassToNanChannel` (`0xffffff800223d06c`): delega en
  `convertOpClassToChannel` salvo para las clases 131–134 (tabla NAN de 6 GHz
  `nan6GoperClassTable`, `0xffffff80023abc10`); sin papel AWDL.
- Llamadores AWDL de `convertOpClassToChannel`: sólo `parseAwdlChanSeqFromChanSeqTLV`
  (`0xffffff80021b5bed`) y `parseAwdlPrefChanTLV` (`0xffffff80021b67cc`); el resto son NAN
  (`IO80211NANPeerManager::calculateNANChannelUtilization`, `0xffffff80022b3a24`) y la propia
  `convertOpClassToNanChannel` (`0xffffff800223d0a3`).

## 3. Cadena de llamadas del TLV 18 y efecto de un fallo

### 3.1 Recorrido de una trama AWDL recibida

`IO80211AWDLPeerManager::actionFrameProcessInput` (`0xffffff800220ed31` toma el par y lo retiene)
llama a `IO80211AWDLPeer::actionFrameReport` (`0xffffff80021af6be`), que hace **dos pasadas** sobre
los TLVs de la misma trama:

1. `doMinimalAFProcessing` (`0xffffff80021b3ea6`, llamada en `0xffffff80021b013f`, antes del
   bucle principal — el retorno al bucle está en `0xffffff80021b1387` hacia `0xffffff80021b033f`).
   Recorre los TLVs con los **tipos crudos** (`switch (v18)` sobre el byte 0 del TLV) y trata
   4, 18, 32, 24 (elección v2), HT caps…; cada caso está encapsulado por el bit 0 del byte
   `+0x57` del contexto (p. ej. `test byte ptr [r12+57h], 1` en `0xffffff80021b414b`). Semántica
   exacta de ese indicador `?` (se pone a 0 en `actionFrameReport` cuando no hay UMI,
   `0xffffff80021b02a0` región; el bit gates también la elección v2 y el TLV 12 según
   `AWDL_OPEN_QUESTIONS_1.md`).
2. Bucle principal: `tlvCheckBounds` por TLV (`0xffffff80021b03ad`) y `switch` con sesgo −2
   (`add r15d, 0FFFFFFFEh` en `0xffffff80021b03ce`). El descriptor del salto en la base de datos
   da 34 casos, tabla en `0xffffff80021b2eb0` (4 B por entrada, objetivo = base + desplazamiento)
   y `lowcase = 2`: el índice de la tabla es tipo − 2 y los objetivos salen del volcado del
   `switch_info`. Las etiquetas de caso del desensamblado citan el **tipo real** (índice + 2):
   caso 18 = TLV 18 (`0xffffff80021b0bf7`, tabla índice 16), caso 32 = TLV 32
   (`0xffffff80021b0b37`, índice 30), caso 33 = TLV 33 pref-chan (`0xffffff80021b0c77`, índice
   31), caso 4 = sync params (`0xffffff80021b0606`, índice 2). Esto re-verifica la nota de
   `AWDL_TLV_BODIES.md` sobre el sesgo −2 y resuelve la correspondencia de casos.

El caso del TLV 18 en el bucle principal (`0xffffff80021b0bf7`, index 16 de la tabla): si la
variable `var_120` ya está a 1 (un TLV 32 apareció antes) el 18 se ignora (`0xffffff80021b0bfa`
–`0xffffff80021b0c0f`); si no, exige cursor ≥ 9 (`cmp r10, 9` en `0xffffff80021b0cc5`) y buffer
reservado (`cmp [rbp+var_A0], 0` en `0xffffff80021b0ccf`) y llama a
`parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b0cf2`). En caso de éxito pone `var_148 = 1`
(`0xffffff80021b0d01`) y `var_120 = 0` (`0xffffff80021b0d07`); los tres indicadores se inicializan
a 0 antes del bucle (`0xffffff80021b02b5`/`02bf`/`02c9`).

Al final del bucle (`LABEL_291`, puerta en `0xffffff80021b14ec`) se aplica **una** de las
secuencias parseadas con `updateAwdlChanSeq` (`0xffffff80021b1520` para TLV 18/32,
`0xffffff80021b15ff` para la variante embebida en sync params, puerta `var_144` en
`0xffffff80021b15de`):

- TLV 18 o TLV 32 con parseo válido → buffer de 392 B propio (`var_148 | var_120`).
- Si no, y sync params trajo secuencia embebida válida → su buffer.
- Si ninguna, no se llama.

Un fallo de `updateAwdlChanSeq` (≠ 0) sólo registra `"Error: updating ch sequence from %d,
error: %d"` (línea 3161, `0xffffff80021b1953`) y continúa; el pase del TLV se anota en el campo
`+0x2C` del contexto (`0xffffff80021b19ca`) y la trama no se descarta.

### 3.2 Límites previos

- `tlvCheckBounds` (`0xffffff80021aefa0`), casos 18 y 32 (índices 18/32 de la tabla sin sesgo,
  `lowcase = 0`): mínimo 6 y máximo 70 bytes de cursor (`mov edi, 6; mov esi, 46h` en
  `0xffffff80021aefdf`). Con cursor = longitud total del TLV (cabecera de 3 B incluida), el
  máximo 70 deja `9 + 2·n ≤ 70` ⇒ **n ≤ 30** en la práctica.
- `isValidChanSeqTlv` del manager (`0xffffff8002214b76`): cursor ≥ 9, longitud del TLV ≥ 6,
  `count−1 ≤ 0x1F` y `count−1 ≤ (payload−3−2)/2` (`0xffffff8002214bc9`–`...4bdc`); es la
  comprobación gruesa del lado del manager.
- Dentro del parser: `count−1 < 0x20` (`cmp dil, 20h` en `0xffffff80021b5aac`), `2·n ≤ len−9`
  (`0xffffff80021b5a9a`) y `len ≠ 9` (`0xffffff80021b5aa5`). Violación ⇒
  `"Error: Assert failure"` (líneas 6186/6206, `0xffffff80021b5d6c`/`0xffffff80021b5d8f`) y `-1`.

### 3.3 El parser, byte a byte

`IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqTLV` (`0xffffff80021b5a08`). Estructura del TLV
(cabecera de 3 B incluida): `+3` = n−1, `+4` = codificación, `+5` = dup, `+6` = step,
`+7..+8` = fill u16 LE, `+9` = n entradas de 2 B. Copia la cabecera al `awdl_chan_seq_info2`
de 392 B (`+0..+5`, `0xffffff80021b5b02`–`1e`) y por cada entrada escribe 12 B en `info2+8+12·i`:
`{u32 1, u32 canal, u32 flags}`.

- **Codificación 3** (`cmp byte ptr [r9+1], 3` en `0xffffff80021b5b61`): byte 0 = canal en el
  aire, byte 1 = clase operativa. Si el canal es distinto de 0 (`cmp byte ptr [r15], 0` en
  `0xffffff80021b5ba1`) llama a `convertOpClassToChannel(clase, 1, 0, &canal_out, &bw_out,
  &banda_out)` (`mov esi, 1; xor edx, edx` en `0xffffff80021b5bda`–`df`, `call` en
  `0xffffff80021b5bed`): bitmap de canales `1` (primer bit) y bitmap de centro `0`, es decir
  **el canal de la entrada no participa en la conversión**; la conversión sólo aporta ancho y
  banda. Devuelto ≠ 0 ⇒ `"fail convert op class, op class 0x%X\n"` (línea 6268,
  `0xffffff80021b5ddf`–`07`) y `-1`.
- **Cualquier otra codificación** (`jnz 0xffffff80021b5c74`): sólo el byte 0 (canal); el byte 1
  se ignora salvo en la copia cruda. Sin llamada a conversión.
- Flags de la entrada (rama 3, `0xffffff80021b5c1e`–`67`): `0x2000` si `banda_out == 0x4000`;
  si no, `0x10` si `canal_out ∈ [36,165]` (`lea edx, [rcx-24h]; cmp dl, 82h` en
  `0xffffff80021b5c2f`–`3a`), `8` si `canal_out ∈ [1,14]` (`dec cl; cmp cl, 0Eh` en
  `0xffffff80021b5c3c`–`43`), y `0` en otro caso; se añade el ancho: `bw_out ∈ [1,3]` ⇒
  `dword_FFFFFF80023AA014[bw−1]` = {4, 1024, 2048} = {40 MHz, 80 MHz, 160 MHz}
  (`0xffffff80021b5c49`–`62`); `bw_out = 0` (20 MHz) ⇒ `2`. Flags de la rama sin codificación 3
  (`0xffffff80021b5cb8`–`d8`), sólo por canal del aire: 0 → 0; [1,14] → 8; [15,35] → 0;
  [36,165] → 0x10; [166,255] → 0.
- El canal almacenado en la entrada es **siempre el byte del aire**, nunca el convertido: la
  clase operativa sólo decide si la entrada se acepta y qué flags lleva. No hay comprobación de
  que el canal pertenezca a la clase: `{36,128}` o `{6,128}` se aceptan con las flags de 5 GHz
  de la clase 128 (`0xffffff80023ab8f6`, centros 80).
- Copia cruda de los 2 B de cada entrada al bloque privado del par (`[priv+0x558+2i]` y
  `+0x559+2i`, `0xffffff80021b5d09`–`48`; 0x558 = 1368, como ya documentaba
  `AWDL_TLV_BODIES.md` §2.1).
- Éxito: 0 tras n entradas (`xor eax, eax` en `0xffffff80021b5d68`).

### 3.4 Qué hace un fallo de conversión con el TLV, el par y la trama

- **TLV**: el parseo entero devuelve `-1` en el punto del fallo; las entradas ya escritas en el
  `awdl_chan_seq_info2` (y la cabecera) quedan escritas pero el destino depende de la pasada.
- **Pasada principal**: `var_148` sigue a 0 ⇒ `updateAwdlChanSeq` no se llama por este TLV
  (§3.1). La secuencia anterior del par permanece intacta. La trama **no se descarta**: el
  error se registra por el manager (`"Error: parsing ch sequence from 18, error: -1"`,
  `0xffffff80021b251a`–`5b`) y el resto de TLVs se procesa con normalidad. Si la misma trama
  trae TLV 32 válido, manda el 32 (indicador `var_120`); si trae sync params con secuencia
  embebida válida, manda la embebida.
- **Pasada mínima** (`doMinimalAFProcessing`): el indicador de "TLV 18 visto" se pone a 1
  **antes** de probar el resultado (`mov cl, 1; mov [rbp+var_A0], rcx` en
  `0xffffff80021b41c6`–`c8`, el `test eax, eax` está en `0xffffff80021b41cf`), y al final del
  recorrido se llama a `updateAwdlChanSeq` igualmente (`0xffffff80021b45cb`→`0xffffff80021b45e8`),
  con un buffer de 392 B recién reservado con `IOMallocZeroData` (cero-filled). Por tanto, con el
  bit `ctx+0x57` activo, un fallo de conversión **sí aplica** una secuencia recortada: las
  entradas previas a la defectuosa conservan su canal y el resto quedan a canal 0. Un fallo de
  aserto (cursor corto) escribe sólo la cabecera a cero (n = 0) y `updateAwdlChanSeq` sale por
  `-2` (`totalSlots = 0`, §3.4): el par deja de aportar slots de solape hasta el siguiente AF
  válido. Rareza observada, no reproducida en hardware `?`; su alcance real depende del
  significado exacto de `ctx+0x57` (`?`).
- **Par**: sin fallo, `updateAwdlChanSeq` (`0xffffff80021ab7e4`) copia cabecera y entradas al
  bloque privado del par: `priv+0x38C` = codificación, `+0x38D` = min(n,16)
  (`cmovb` en `0xffffff80021ab8aa`–`b2`), `+0x38E` = step, `+0x38F` = dup, `+0x394` = fill u16,
  entradas en `+0x398` (`{u32 1, u32 canal}`) y `+0x3A0` (flags) (`0xffffff80021ab96c`–`9a`).
  Retornos: `-1` sin buffer o n > 0x20 (`0xffffff80021ab89e`), `-2` si
  `totalSlots = (dup+1)·min(n,16)·(step+1)` cae en [513, 65023]
  (`imul`/`mov [priv+0x38A]` en `0xffffff80021ab93c`–`46`, `add esi, 0FFFFFDFFh; cmp edx, 0FE00h;
  jb -2` en `0xffffff80021ab94d`–`62`: válido si ≤ 512; el borde ≥ 65024 por módulo 2^16 es
  inalcanzable con dup/step ≤ 255 y n ≤ 32 salvo combinaciones extremas `?`), `-3` si el canal
  de la entrada 0 es 0 (`cmp dword ptr [priv+39Ch], 0` en `0xffffff80021abbfe`, `mov r12d,
  0FFFFFFFDh` en `0xffffff80021abd5e`): con `-3` las entradas ya se copiaron y los efectos
  posteriores ya corrieron, sólo se saltan el cálculo de potencias de dos
  (`priv+0x59C/0x5A0/0x5A4/0x5A8`) y la notificación
  `cachedPeerHasChangedChannelSequence` (`0xffffff80021abd57`).

## 4. Codificaciones 0, 1 y 3: bytes por slot y uso de step/fill

En el **TLV 18** (y en el TLV 32, que no usa esta función) cada slot ocupa 2 bytes desde `+9`,
haga lo que haga la codificación; el índice avanza de 2 en 2 (`add rbx, 2` en
`0xffffff80021b5d57`):

| Codificación | byte 0 | byte 1 | Flags de la entrada |
|---|---|---|---|
| 3 | canal en el aire | clase operativa (debe estar en `operClassTable` si canal ≠ 0) | banda+ancho de la **clase** (§3.3) |
| 0, 1, 2, … | canal en el aire | ignorado | por canal: 0 → 0; 1–14 → 8; 15–35 → 0; 36–165 → 0x10; 166–255 → 0 |

La codificación **no** se valida (sólo se compara con 3), y `dup` (`+5`), `step` (`+6`) y `fill`
(`+7..8`) se copian verbatim al `info2` y de ahí al par (`priv+0x38E/0x38F/0x394`). Su único uso
observado: `totalSlots = (dup+1)·min(n,16)·(step+1)` en `priv+0x38A`, con la sanidad del −2
(§3.4), y el cálculo de potencias de dos de `priv+0x59C/0x5A0` para la planificación
(`0xffffff80021abc0b`–`c6e`). No se vio el parser expandir la secuencia por `step+1`: la
expansión lógica queda en manos del consumidor, y el solape por slots (§5) compara entradas por
índice 1:1, de modo que ambas secuencias (la del par y la propia del manager) deben compartir la
misma convención de `step` para alinear. El lector del fill almacenado del par (`priv+0x394`) no
apareció en los recorridos `?`.

La **variante embebida en sync params** (`parseAwdlChanSeqFromSyncParamsTLV`,
`0xffffff80021b8196`; sub-TLV desde `+36` del TLV 4) tiene semánticas distintas y **no llama a
`convertOpClassToChannel`**: codificación 0 = 1 byte por slot (canal, flags por canal);
codificación 1 = u16 LE con marca de banda (`& 0x300 == 0x100` ⇒ 6 GHz, canal = byte−2;
`== 0x200` ⇒ canal = byte+2 si `(byte & 0xFE) < 0xDE`; si no, byte bajo); codificaciones 2 y 3 =
pares {canal, ignorado} con flags por canal, sin conversión de clase (`0xffffff80021b8470`
región del bucle `while (v15 != 1)`). Esto corrige la lectura de `AWDL_TLV_BODIES.md` §2.1, que
atribuía al TLV 18 genérico la conversión "en ancho y banda" sin distinguir ambas variantes y sin
especificar el bitmap constante.

## 5. Par que anuncia 149 (o 6) sólo en algunos slots y 0 en el resto

- **Aceptación**: sí. Las entradas `{149,128}` pasan la conversión (clase 128 en la tabla) y se
  almacenan como `{1, 149, 0x410}` (5 GHz + 80 MHz); con clase 124 serían `{1, 149, 0x12}`.
  Las `{0,0}` no se convierten y se almacenan como `{1, 0, flags}`. El TLV de 16 slots mide
  41 B ≤ 70 y cumple todos los límites; `totalSlots = 1·16·4 = 64 ≤ 512`.
- Rareza: las flags de un slot `{0,0}` **no se ponen a cero**: se calculan con los restos de la
  última conversión de la misma TLV (`var_31/32/38` de pila, inicializados a 0 en
  `0xffffff80021b5a2c`–`32` y reescritos sólo por `convertOpClassToChannel`): tras un
  `{149,128}` heredan `0x410`; si ningún slot se convirtió antes valen `0x2` (0 de banda | 2 de
  20 MHz). Como el canal es 0, no afecta al solape ni al voto de canal (que exigen canal ≠ 0),
  pero sí entra en la comparación de cambio `& 0x2018` de `updateAwdlChanSeq`
  (`0xffffff80021ab8ea`–`fe`).
- Si la **primera** entrada de la secuencia es `{0,0}`, `updateAwdlChanSeq` devuelve `-3`
  (§3.4): la secuencia se almacena igualmente pero el par no notifica el cambio al manager
  (sin `cachedPeerHasChangedChannelSequence`). Las secuencias reales empiezan por slots
  servidos, así que en la práctica no ocurre.
- **Solape por slots**: `IO80211AWDLPeer::updateNumOfOverlapSlots` (`0xffffff80021abec2`,
  llamada desde `updateAwdlChanSeq` en `0xffffff80021abaea` con `priv_manager+0xA774` como
  referencia, `add rsi, 0A774h` en `0xffffff80021abae0` — la misma dirección que escribe
  `setMyChanSeq` con los ioctls 129/130, `0xffffff8002235302`) cuenta las entradas i donde
  `canal_par[i] == canal_propio[i] ≠ 0` y `(flags_par[i] ^ flags_propios[i]) & 0x2018 == 0`
  (`mov edi, [priv+39Ch+12i]; cmp edi, [rsi+rdx+4]` en `0xffffff80021abeef`–`fa`; `xor edi,
  [priv+3A0h+12i]; test edi, 2018h` en `0xffffff80021abf04`–`0b`). La máscara `0x2018` = banda
  2,4/5/6 GHz (8/0x10/0x2000): los bits de ancho (2/4/0x400/0x800) no participan. Consecuencia
  directa para la capa RTL: las entradas del 129 propio deben llevar el bit de banda correcto
  (8 en 2,4 GHz, 0x10 en 5 GHz), si no el solape sale 0 aunque el canal coincida.
- **Canal común**: `calculateHighestAllowedChannel` (`0xffffff80021ab1fa`) recorre las entradas
  del par buscando 6 con `flags & 0x2018 == 8`, 44 y 149 con `== 0x10`
  (`cmp r10d, 6/2Ch/95h` en `0xffffff80021ab278`–`84`; máscara en `0xffffff80021ab250`) y guarda
  el mejor en `priv+0x42` (`0xffffff80021ab5be`), con prioridad 149 > 44 > 6 y dos excepciones
  de "infra de latencia ultra baja en 44" que lo rebajan a 6 (`0xffffff80021ab2fb`–`338`). Si
  ninguna entrada es 6/44/149 registra `"Couldn't find Heighest Allowed Channel"` y devuelve 0:
  el par no empuja el canal común. Con 149 presente, `updateAWDLCommonChannel`
  (`0xffffff80022154de`, llamado en `0xffffff80021abad7` con límite de 1 s, §"Period 1000 ms")
  invoca `calculateAWDLCommonChannel` (`0xffffff800222b64e`), que vota con el `priv+0x42` de
  todos los pares: 149 por defecto (`mov al, 95h` en `0xffffff800222bad1`), 44 si algún par lo
  reporta y `isValidChannel(44, 0x10)` lo permite (`0xffffff800222ba3f`–`a84`), 6 si algún par
  lo reporta (`0xffffff800222b908`), y al final recorta entre el piso 6 y el techo
  `priv+0x42B0` (`0xffffff800222bb28`–`3e`). Un iPhone que sólo sirve 149 en parte de sus slots
  deja el canal común en 149 si no hay pares que pidan 6 o 44.
- `followPeerChannelSequence` (`0xffffff80021f6e74`) copia la secuencia del par a la planificación
  del manager sólo para pares AirPlay/ranging (`priv+1665` bit 0 y similares, llamadas desde
  `updateAwdlChanSeq` en `0xffffff80021abbf5`); sustituye los canales 6 GHz no permitidos por
  `_zeroChanInfo`/`_channel6Info`.

## 6. Consecuencias para la capa RTL (TX)

- La forma actual de `putChanSeq` — `{15, 3, 0, 3, FF FF}` con pares `{149,128}` y `{0,0}`
  (clase 81 en 2,4 GHz) — es **aceptada íntegra** por el parser del kernel: 128 y 81 están en la
  tabla, y los slots a 0 no se convierten. El H8 de `AWDL_TX_FRAME_REVIEW.md` queda cerrado en
  su dos dudas: 128 sí se acepta (80 MHz, banda 0x1000) y `{0,0}` sí (sin conversión).
- Lo único que rechazaría el TLV entero en la pasada principal es un par con canal ≠ 0 y clase
  fuera de la tabla (p. ej. `{149, 0}`): el parser devuelve `-1` y la secuencia del par no se
  actualiza. Emitir siempre una clase de la tabla §2.1 (81–84, 115–134) y `{0,0}` para los
  slots no servidos.
- El canal del aire manda: no hace falta que pertenezca a la clase anunciada. Aun así,
  announce `{149,128}` (como owly y el iPhone) para que las flags del receptor (0x410) y las
  propias del 129 coincidan en banda.
- En recepción, un par real con slots `{0,0}` se integra sin problema: solape y canal común
  funcionan por entrada; el bit de banda del 129 propio es lo crítico (§5).

## 7. Pendientes

- Significado exacto del bit 0 de `ctx+0x57` (el gate de `doMinimalAFProcessing`) y, con él, si
  la aplicación de secuencias truncadas tras un fallo de conversión (§3.4) es alcanzable con
  tramas reales.
- Lector del fill del par (`priv+0x394`) y escritor de `priv+0x42B0` (techo del recorte del canal
  común).
- Por qué `_convertChannelToOpClass` no tiene xrefs (¿sólo exportada para kexts clientes?).
- Que el borde `totalSlots ≥ 65024` del chequeo del −2 sea alcanzable (dup/step extremos).

## 8. Referencias

- `AWDL_TLV_BODIES.md` (§2, TLV 18 y su corrección en este documento), `AWDL_TX_FRAME_REVIEW.md`
  (H8), `AWDL_CHANNEL_SEQUENCE_CONTROL.md` (129/130 y plantillas), `AWDL_PEERMANAGER_DRIVER_ABI.md`
  (formato interno de 12 B y eventos 41/43), `AWDL_CHANNEL_STRATEGY.md` (canal común y
  airplaySink), `AWDL_OPEN_QUESTIONS_1.md` (`doMinimalAFProcessing`).
- Volcados y scripts de la sesión: `Build/ida_awdl/task_tlv18/` (`out1..out15.txt`, `d1..d6.py`).
