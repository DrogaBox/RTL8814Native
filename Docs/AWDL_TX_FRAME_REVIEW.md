# Revisión byte a byte de los action frames TX de `RTLAwdInterface`

Revisión independiente de `buildAf`, `putChanSeq`, `txTick` y `alignClock`
(`Source/AirLayer/AWDLInterface.cpp`, commits `cceb743` y `7e454be`): cada offset, longitud,
endianness y valor se contrasta con tres referencias: owly (`~/Desktop/AWDLLab/refs/owly`,
`docs/SPEC.md` §3 y `crates/owly-awdl/src/{tlv,channel,sync,frame}.rs`, que interoperan con
iPhones), las notas del parser del kernel (`AWDL_TLV_BODIES.md`, `AWDL_FRAME_WIRE_FORMAT.md`,
`AWDL_TLV5_ELECTION.md`) y el payload del evento 41 (`AWDL_WINDOW_TIMER.md` §5.1,
`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2). Lo no verificable se marca `?`.

No se ha tocado código, ni cargado kext, ni ejecutado `sudo`. Los arreglos se proponen, no se
aplican. No se abrió IDA: todo lo contrastable con los documentos listados está cerrado; los
puntos que exigirían desensamblado quedan como `?` en §6.

Los números de línea `:NNN` se refieren al árbol de trabajo en el momento de la revisión, intermedio
entre `ce0481b` y `ab11c69`, y no coinciden con ningún commit: se localizan por el nombre de la función
citada. El estado de cada hallazgo tras la revisión está en §11.

## 1. Veredicto

Los tres TLVs renderizados (4, 5, 18), la cabecera 802.11, la cabecera fija, el mapeo
plantilla→TLV 5, la aritmética del reloj y el evento 41 son byte a byte correctos frente a las
tres referencias. La PSF construida a mano de §5 reproduce exactamente lo que `buildAf` emite y
las reglas de parseo de owly la aceptan sin errores. No se ha encontrado ningún defecto de
formato en el aire. Hay un defecto de carrera que puede corromper los bytes de fase de una
trama (H1), dos diferencias menores con owly en valores no normalizados (H2, H3) y cinco
observaciones de interop/robustez (H4–H8).

## 2. Cabecera 802.11 y cabecera fija AWDL

`buildAf` `AWDLInterface.cpp:921-938`; constantes `:601-618`, `:620`.

| Campo | Código | Referencia | Estado |
|---|---|---|---|
| FC | `0xD0` en `+0`, `+1..+3` a cero (duración incluida) | `frame.rs:37` `ACTION = 0x00d0` LE; `put_header` escribe duración 0 (`frame.rs:271`) | OK |
| A1 | `ff×6` (`:925`) | difusión, `action_frame` usa `MacAddr::BROADCAST` (`frame.rs:288`) | OK |
| A2 | `_mac` (`:926`) | AWDL-2: la MAC de `awdl0` | OK |
| A3 | `00:25:00:FF:94:73` (`:927`, `:620`) | AWDL-1 y `AWDL_RTL8814_RX_ACK.md` | OK |
| Seq | a cero (`:922`) | la numera la radio (`rtlRadioTx80211`, commit `4ac3340`); owly la pone el emisor, aquí la añade el driver | OK |
| Categoría/OUI/tipo | `7F`, `00 17 F2`, `08` (`:931-933`) | `actionFrameHasCorrectOUI` compara `+1..+4` (`AWDL_FRAME_WIRE_FORMAT.md` §2); owly `frame.rs:195-199` exige lo mismo | OK |
| Versión | `0x10` (`:609`, `:934`) | el kernel sólo exige ≠ 0; owly exige exactamente `0x10` (`AWDL_VERSION_COMPAT`, `frame.rs:66`) | OK |
| Subtipo | `0` PSF / `3` MIF (`:607-608`) | kernel: ramas `==0`/`==3` (§2); owly `frame.rs:80-82` | OK |
| Reservado `+7` | 0 (`:922`) | SPEC AWDL-3 | OK |
| `phy_tx`/`target_tx` | `nowUs` LE32 en `+8`/`+12` (`:936-937`) | owly escribe el mismo valor en ambos (`frame.rs:295-296`); el kernel los lee en `parseAwdlSyncParamsTlv` y, al ser iguales, `sync_contention` queda en 0 (`AWDL_TLV_BODIES.md` §1.3); unidad µs según `AWDL_FRAME_WIRE_FORMAT.md` §5.4 | OK |

Los TLVs empiezan en `+16` de la cabecera fija (`AWDL_FRAME_WIRE_FORMAT.md` §2), que es donde
`findTlv` (`:653-665`) y `alignClock` (`:1004`) los buscan. Cabecera TLV `{u8 type, u16 len LE}`
(`:941-942`, `putLe16`) = `AWDL-4` y §3 del doc de formato.

## 3. TLV 4 (sync params) y secuencia de canales

`buildAf` `AWDLInterface.cpp:940-961`; `putChanSeq` `:636-650`; `AWDL_SYNC_LEN = 33`, `AWDL_CHANSEQ_LEN = 38` (`:614-615`).

| Offset valor | Campo | Código | Kernel (`AWDL_TLV_BODIES.md` §1.2, offsets TLV = valor+3) | owly (`tlv.rs:368-392`) | Estado |
|---:|---|---|---|---|---|
| len | 73 | `:942` | ≥ 39 exigido; `0x2A` de cursor (76) ≥ 42 | test `owls_tlv_lengths` espera 73 | OK |
| 0 | `tchan` | `channel` (`:944`) | `+3`, a `peer+756/+1432` | `own.channel` | OK |
| 1–2 | `dc` | `toSlot` LE (`:945`) | `+4`, ≤ 64 | `next_slot_tu` ∈ [1,64] (`sync.rs:56-58`) | OK; fórmula idéntica a `64 − (tu−base)/1024 mod 64` (`AWDL_WINDOW_TIMER.md` §2) |
| 3 | `mchan` | `channel` (`:946`) | `+6`, sólo traza | `own.master_channel` | OK¹ |
| 4 | guarda | 0 (`:947`) | no se lee | 0 | OK |
| 5–6 | periodo AW | 16 (`:948`) | `+8` → `peer+762`; el aire mide 16 | `AW_PERIOD_TU` = 16 | OK |
| 7–8 | periodo AF | 110 (`:949`) | `+10` → `peer+764`; aire 110 (iPhone 440 en MIF) | `psf_interval_tu` | OK |
| 9–10 | flags | `0x1800` (`:950`) | no se lee; aire `0x1800` | `0x1800` | OK |
| 11–12 | AW ext | 16 (`:951`) | `+14`; ≠ 16 ⇒ `"Zero AWExt"` | `AW_PERIOD_TU` | OK |
| 13–14 | AW común | 16 (`:952`) | `+16` | `AW_PERIOD_TU` | OK |
| 15–16 | rem | `awLeft` LE (`:953`) | `+18` → `peer+904` | rem restante | OK; `16 − tu mod 16` ∈ [1,16] |
| 17–20 | ext ×4 | 3 (`:954`) | `+20..+23`; `+20` → `peer+1452`; aire `03 03 03 03` | `PRESENCE_MODE−1` = 3 | OK |
| 21–26 | master | `e2+3` (TLV 24 valor 0) (`:919,:955`) | `+24..+29` → `peer+600/+604` | `own.election.master` = dirección raíz | OK² |
| 27 | `pm` | 4 (`:956`) | `+30`; 0 ⇒ fallo de parseo | `PRESENCE_MODE` = 4 | OK |
| 28 | reservado | 0 (`:957`) | `+31` no se lee | 0 | OK |
| 29–30 | `sn` | `aw` LE (`:958`) | `+32` → `peer+106`; el kernel enmascara con `0xFFC0` | `own.aw_counter` = AW en curso | OK³ |
| 31–32 | AP alignment | 0 (`:959`) | `+34` no se lee | `own.aw_counter` | H3 |
| 33–70 | chanseq | `putChanSeq` (`:960`) | `tlvCheckBounds`: valor+33 ≤ 0x1F y valor+34 ≤ 3 | `own.sequence.put` | OK |
| 71–72 | relleno | 0 (`:922`) | — | 2 ceros | OK |

¹ `mchan` debería ser el canal del maestro (ioctl 132), no el de la radio; coinciden mientras la
capa siga su propio calendario o a un maestro en su mismo canal (radio fija). Si `alignClock`
siguiera a un maestro en otro canal, el anunciado quedaría corto (H5).
² owly mete en `+21..+26` su `Election.master`, que `parse_v2` toma del offset 0 del TLV 24
(`tlv.rs:160`); el código usa el mismo offset (`e2+3`), y en la plantilla del kernel ese offset
es `root_mac` (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11). Coherente en las tres fuentes.
³ `sn` y el `AwNumber` del evento 41 salen del mismo `aw` (`:913/:958` frente a `:825`), como
exige `AWDL_PSF_MIF_EMISSION.md` §5.1.

### 3.1 Secuencia embebida y TLV 18

`putChanSeq` escribe `{15, 3, 0, 3, FF FF}` y 16 entradas de 2 B (`:640-649`). Contraste:

- Kernel (`AWDL_TLV_BODIES.md` §2.1, offsets TLV): `+3` count−1 (< 0x20), `+4` codificación (3 =
  pares {canal, clase operativa}), `+5` duplicado, `+6` step, `+7..+9` fill, `+9` entradas de 2 B;
  `2·n ≤ len_cursor − 9`. Con len 41: 32 ≤ 44−9 = 35. OK.
- owly (`channel.rs:188-211`): sólo acepta la forma estricta 15/enc/dup 0/step 3/fill `0xffff`;
  entradas opclass `[canal, opclass]` (`channel.rs:52-55`). El código coincide byte a byte y el
  relleno de 3 B dentro del valor (len 41) es el que owly emite (`tlv.rs:410-415`, test espera
  41) y el que miden las capturas del iPhone (41 B, `AWDL_REFERENCIAS_EXTERNAS.md` §4.2). OK.
- `awdlOpClass` (`:634`): 81 en 2,4 GHz, 128 en 5 GHz = `Channel::opclass` (`channel.rs:47-49`,
  0x51/0x80). OK. Slots no servidos `{0, 0}`: mismo patrón que owly (`Sequence::empty`) y que el
  iPhone (canal 0, §4.2). OK (véase H8 sobre la conversión kernel de 128 y de 0).
- Codificación/step/fill fijos en vez de copiados del 129 (`p[6]`/`p[7]` se ignoran en
  `:699-709`): owly rechaza cualquier otra forma, así que fijarlos es lo interoperable; difiere
  del consejo de conservarlos de `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7.3. H6.

## 4. TLV 5 (elección v1) desde el TLV 24 de la plantilla

`buildAf` `AWDLInterface.cpp:963-975`; `findTlv(_tpl, _tplLen, 24, 28)` en `:918`.

El TLV 24 de la plantilla del 151 mide 40 B de valor (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11:
root_mac `+3`, master_mac `+9`, rootLFC `+15`, hopCount `+19`, rootMetric `+23`, selfMetric
`+27`, electionId `+31`, selfLFC `+39`, todos TLV-relativos) y equivale al layout del SPEC
(offsets de valor 0/6/12/16/20/24/28/36).

| Valor TLV 5 | Código | Fuente TLV 24 | owly v1 (`tlv.rs:395-407`) | Capturas (`AWDL_TLV5_ELECTION.md` §3.2/§4) | Estado |
|---:|---|---|---|---|---|
| 0 flags | 0 (`:922`) | — | 0 | siempre 0 | OK |
| 1–2 ID | 0 | — | 0 | siempre 0 | OK |
| 3 distancia | `(u32)(e2+3+16)` (`:968`) | hopCount (valor 16) | `e.height.min(255)` | dist 0 maestro / 1 seguidor | H2 |
| 4 reservado | 0 | — | 0 | 0 | OK |
| 5–10 raíz | `memcpy(e2+3, 6)` (`:969`) | root_mac (valor 0) | `e.master` | MAC raíz | OK |
| 11–14 métrica raíz | `memcpy(e2+3+20, 4)` (`:970`) | rootMetric (valor 20) | `e.master_metric` LE | 525/65/60 | OK |
| 15–18 métrica propia | `memcpy(e2+3+24, 4)` (`:971`) | selfMetric (valor 24) | `e.self_metric` LE | 525/65/60 | OK |
| 19–20 relleno | 0 | — | 2 ceros | `00 00` | OK |

len 21 fijo (`:965`): el aire sólo usa 21 y owly lo emite así. OK. Emitir el 5 junto al 24 es lo
que hacen owly (`put_all`) y el firmware Apple (las capturas lo traen en PSF y MIF); el
receptor que ve el 24 ignora el 5 (SPEC, §3.2). La rama sin TLV 24 (`:972-974`) deja métricas a
0; sólo es alcanzable con una plantilla sin TLV 24, que el manager no produce según §3.11 `?`.

## 5. Plantilla del 151, orden y tamaños

- Lectura del 151: `len` en `+4` y puntero en `+8` (`:671-672`) = struct de 16 B de
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11. Buffer propio de 512 B (`AWDLInterface.h:440`), como el
  del kernel. Copia bajo `_txLock` (`:678-684`). OK.
- La plantilla va íntegra al final de la trama (`:983`): el orden resultante es
  `4, 5, 18, 6, 24, 12, 7, 17, 21, …` — exactamente el orden medido en los PSF de iOS 26 y el que
  prescribe `AWDL_PSF_MIF_EMISSION.md` §4 (insertar 4/5/18 delante del primer TLV de la
  plantilla, sin relleno final). OK.
- `need` (`:906-907`) = 24+16+76+24+44+tplLen = 184+tplLen ≤ 184+512 = 696 < `AWDL_AF_MAX` 1024;
  `bzero(out, need)` cubre exactamente la trama, de modo que los rellenos internos (2 B del TLV 4,
  3 B del TLV 18) quedan a cero sin escritura extra. OK.
- MIF = plantilla del 151 con subtipo 3: le faltan el TLV 16 (ARPA, que el kernel sólo compone en
  la MI primaria, `AWDL_PSF_MIF_EMISSION.md` §3.2) y el TLV 2, y el 6 viaja en posición de PSF.
  Ni el kernel ni owly lo exigen para validar al emisor (AWDL-17 pide MIF + TLV 21 nonzero, y el
  21 viene en la plantilla); el nombre de host no viaja en AF. Ya está marcado `?` en el fuente
  (`:599`). H4.

## 6. Aritmética del reloj y `alignClock`

Constantes (`:601-606`): TU 1024 µs, AW 16 TU, PM 4, slot 64 TU, 16 slots, PSF 110 TU — iguales a
`sync.rs:12-20` y al aire medido.

`txTick` (`:767-868`):

- `awIdx = (now − _clkBaseUs + 500) / 16384` (`:791`): el margen de 500 µs sólo redondea hacia
  arriba un tick que caiga justo antes del borde; `thread_call_enter_delayed` no dispara antes de
  plazo, así que es inofensivo. `aw = (u16)(_clkBaseAw + awIdx)` (`:792`): u16 con desbordamiento,
  como el `AwNumber` del evento 41. OK.
- `slot = (aw / 4) % 16` (`:794`) = `(aw >> 2) & 0xF` de `AWDL_WINDOW_TIMER.md` §2 y = `min(15,
  (awNumber % 64)/4)` del manager (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2). OK.
- PSF cada 110 TU con recuperación de atraso (`:812-816`), igual que `node.rs:566-572`; MIF en la
  tercera AW de cada slot servida (`aw % 4 == 2`, `:807`) = mitad de la franja de owly
  (`node.rs:574-578`, `SLOT_US/2`). OK.
- Ventana: abre con 41 sólo en la primera AW de franja servida (`:803-806`), cierra con 43 en el
  borde de la siguiente (`:799-801`) — la ventana cubre la franja servida completa, como pide
  `AWDL_WINDOW_TIMER.md` §5.3, y el 43 precede al 41. Sin 129 (`_seqValid` falso) todas las
  franjas se sirven (`:876-877`), precedido por el kernel (secuencia de roam con todas las ranuras
  al mismo canal). OK.

`alignClock` (`:994-1023`):

- Lee `dc` en valor+1 y `sn` en valor+29 del TLV 4 (`:1007-1008`), con `t+3+21 == A2` como
  condición de «es su propio maestro» (`:1005`): mismos offsets que el parseo del §3. El guardo
  `toSlot ∈ [1, 64]` (`:1009`) coincide con `next_slot_tu ∈ [1..64]` de owly.
- Realign: `into = 64 − toSlot`; `_clkBaseUs = now − into·1024`; `_clkBaseAw = aw − into/16`
  (`:1015-1017`). Es equivalente a `Clock::update` (`sync.rs:93-97`): owly redondea
  `aw_counter & !3` hacia abajo y el código resta los AWs completos transcurridos; para un
  maestro cuyas franjas empiezan en aw ≡ 0 (mod 4) ambos producen el mismo par (base, AW de
  arranque de franja). OK.

## 7. Evento 41 (y 43)

`txTick` `AWDLInterface.cpp:817-833` y `:839-848`; payload de 36 B (`ev41[36]`, `:782`).

| Offset | Código | `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2 / `AWDL_WINDOW_TIMER.md` §5.1 | Estado |
|---:|---|---|---|
| `+0x00` | `(u16)aw` nativo (`:825`) | `AwNumber` u16; al abrir sólo en `aw % 4 == 0`, es la primera AW de la franja (múltiplo de 4), como exige §4 del doc de ventanas | OK |
| `+0x04` | `memcmp(sync,_mac) ? 2 : 1` u32 (`:826`) | estado 1 Master / 2 Slave; `sync` es el segundo MAC del TLV 24 (valor+6), el criterio `is_master` de owly (`election.rs:62-64`) | OK |
| `+0x08` | `sync` = TLV 24 valor+6 (`:823,:827`) | MAC del maestro seguido (la del 134); en la plantilla del kernel ese campo es `master_mac` (§3.11) | OK |
| `+0x0e` | `top` = TLV 24 valor+0 (`:822,:828`) | MAC del maestro raíz; en la plantilla es `root_mac` | OK |
| `+0x14` | `OSSwapHostToBigInt32((u32)awStart)` (`:829`) | `FwAwStartTime` big-endian; al abrir en `aw%4==0`, `awStart` es el inicio de la primera AW = inicio de franja | OK |
| `+0x18` | `_seqFlags[slot]` del 129, si no `0x8`/`0x10` (`:830-831`) | flags de la entrada del 129 (`+0x08` de la entrada de 12 B, §3.7); los valores por defecto 8 (2,4 GHz) / 16 (5 GHz) son la convención del kernel (`AWDL_TLV_BODIES.md` §2.1) | OK |
| `+0x1c` | `ch` u32 nativo (`:832`) | número de canal | OK |
| `+0x20` | 0 (`bzero`) | flags de ventana (0) | OK |

Evento 43: 40 B a cero (`:840-841`), la forma que omite la rama de CCA (`AWDL_WINDOW_TIMER.md`
§5.3). El índice de franja que el manager deduce del 41 encaja con `slot` (§6). La lectura del 129
(`:699-717`: canal en `+4` y flags en `+8` de cada entrada de 12 B desde `+0x0c`, `n` en `+5`)
coincide con `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7. OK.

## 8. PSF de ejemplo y aceptación por las reglas de owly

Parámetros: A2 `56:0d:25:02:1f:ce`, canal 149, plantilla inventada de 60 B (TLV 24 de 43 B con
raíz = propia MAC, métricas 60; TLV 6 de 12 B; TLV 21 de 5 B), máscara 0xFFFF (todas las franjas
servidas, estado sin 129), reloj con `_clkBaseUs = 0`, `_clkBaseAw = 0` y `nowUs = 6 574 080`
(`0x00645000`). Con eso: `tu = 6420`, `aw = 401`, `toSlot = 44`, `awLeft = 12`, `slot = 4`.

```
000  d0 00 00 00 ff ff ff ff  ff ff 56 0d 25 02 1f ce   FC+dur; A1 difusión; A2
010  00 25 00 ff 94 73 00 00  7f 00 17 f2 08 10 00 00   A3 BSSID; seq=0; 7F,OUI,08,ver 1.0,sub 0,rsvd
020  00 50 64 00 00 50 64 00  04 49 00 95 2c 00 95 00   phy=target=6574080 µs; TLV4 len 73; tchan 149; dc 44; mchan 149; guarda 0
030  10 00 6e 00 00 18 10 00  10 00 0c 00 03 03 03 03   AW 16; AF 110; 1800; ext 16; común 16; rem 12; ext 03×4
040  56 0d 25 02 1f ce 04 00  91 01 00 00 0f 03 00 03   master; pm 4; rsvd; sn 401; AP align 0; seq: 15,enc 3,dup 0,step 3
050  ff ff 95 80 95 80 95 80  95 80 95 80 95 80 95 80   fill FFFF; entradas {149,128}
060  95 80 95 80 95 80 95 80  95 80 95 80 95 80 95 80
070  95 80 00 00 05 15 00 00  00 00 00 00 56 0d 25 02   fin seq; relleno 2; TLV5 len 21; flags/ID/dist/rsvd 0; raíz
080  1f ce 3c 00 00 00 3c 00  00 00 00 00 12 29 00 0f   métricas 60/60; relleno; TLV18 len 41; 15,enc 3,dup 0,step 3
090  03 00 03 ff ff 95 80 95  80 95 80 95 80 95 80 95   fill FFFF; entradas {149,128}
0a0  80 95 80 95 80 95 80 95  80 95 80 95 80 95 80 95
0b0  80 95 80 95 80 00 00 00  18 28 00 56 0d 25 02 1f   fin seq; relleno 3; TLV24 len 40; root
0c0  ce 56 0d 25 02 1f ce 00  00 00 00 00 00 00 00 3c   master; contador 0; distancia 0
0d0  00 00 00 3c 00 00 00 00  00 00 00 00 00 00 00 00   métrica raíz 60; métrica propia 60; electionId 0
0e0  00 00 00 06 09 00 00 00  00 00 00 00 00 00 00 15   selfLFC 0; TLV6 len 9 (ceros)
0f0  02 00 34 01                                       TLV21 len 2: versión 3.4, clase 1
```

244 B en total. Validación contra las reglas de owly (réplica exacta de `frame.rs:139-214`,
`tlv.rs:56-88` y `:308-337`, `channel.rs:188-211` ejecutada en `/tmp/awdl_psf_check/psf_check.py`;
cero errores):

- `frame.rs:parse`: FC management (`0x00D0 & 0x000C == 0`), sin protegido/fragmentos, `fc & 0x00FC
  == 0x00D0` (action), cuerpo en `+24`; `action()`: categoría 127, OUI `00:17:F2`, tipo 8, versión
  `0x10` exacta (`AWDL_VERSION_COMPAT`), subtipo 0 = PSF; `phy_tx`/`target_tx` = `0x645000`.
- `tlvs()`: recorrido exacto, sin cola sobrante — TLVs `4(73), 5(21), 18(41), 24(40), 6(9), 21(2)`
  suman 204 = 244 − 24 − 16.
- `SyncParams::parse` (tlv.rs:109-121): tchan 149, next_tu 44, aw_period 16, af_period 110,
  master `56:0d:25:02:1f:ce`, presence 4, aw_counter 401.
- `Sequence::parse` (channel.rs:188-211): forma estricta 15/3/0/3/0xFFFF aceptada; 16 slots de
  canal 149; el relleno de 3 B se ignora.
- `ElectionParams::parse_v1` (dist 0, raíz propia, métricas 60/60) y `parse_v2` (master/sync
  propias, métricas 60/60): el v2, que llega después del v1, gana en `Advert::parse` igual que en
  el código.
- `parse_version`: (0x34, 1), versión y clase no nulas → con un MIF propio cumpliría AWDL-17.

Las mismas comprobaciones del lado del kernel (`AWDL_FRAME_WIRE_FORMAT.md` §1-4,
`AWDL_TLV_BODIES.md` §1-2): OUI/tipo/versión/subtipo válidos, TLV 4 con 76 B de cursor ≥ 42 y
presencia ≠ 0, `dc` 44 ≤ 64, `aw_ext` = 16, secuencia embebida con count−1 = 15 ≤ 0x1F y
codificación 3 ≤ 3, TLV 18 con cursor 44 ≥ 9 y 2·16 ≤ 44 − 9, TLV 5 con len 21 dentro de los
21..37 que acepta `parseAwdlElectionParamsV1`.

## 9. Hallazgos

| # | Severidad | Dónde | Qué | Arreglo propuesto |
|---|---|---|---|---|
| H1 | menor (corrompe una trama) | `AWDLInterface.cpp:912` | `buildAf` se ejecuta fuera del primer tramo bajo cerrojo de `txTick` (`:835` libera, `:850-853` llama): si `alignClock` realinea el reloj entre el muestreo de `nowUs` (`:784`) y la construcción, `nowUs − _clkBaseUs` puede quedar negativo y, al ser `u64`, desbordar: `tu`, `aw`, `toSlot` y `awLeft` salen basura y la trama lleva una fase falsa. Ocurre cuando la realineación aterriza con `into·1024 µs` menor que la latencia RX (`into = 0`, `toSlot = 64`, es el caso de un PSF del maestro en su propio borde de franja). No medido en hardware `?` | Saturar en `buildAf`: `uint64_t delta = nowUs > _clkBaseUs ? nowUs - _clkBaseUs : 0;` y derivar `tu` de `delta`. Requiere OK del usuario |
| H2 | menor (robustez) | `AWDLInterface.cpp:968` | La distancia se trunca a `u8` (`(uint8_t)u32`): un hop ≥ 256 se anunciaría como 0 (= maestro). owly satura (`height.min(255)`, `tlv.rs:400`). El manager no produce hops así `?` | Saturar a 255 en vez de truncar. Requiere OK |
| H3 | observación | `AWDLInterface.cpp:959` | AP alignment (`+31..+32`) a 0; owly repite ahí `aw_counter` (`tlv.rs:388`). Sin lector en el kernel ni en owly; sin impacto interop conocido `?` | Ninguno necesario |
| H4 | observación | `AWDLInterface.cpp:850-853`, `:599` | El MIF se construye sobre la plantilla del 151: sin TLV 16 (ARPA) ni TLV 2, y con el 6 en posición de PSF, frente al MIF Apple (`…,17,16,21,6,2…`). El kernel y owly lo aceptan; AWDL-17 se cumple igual. Ya marcado `?` en el fuente | Añadir ARPA cuando haya nombre de host disponible |
| H5 | observación | `AWDLInterface.cpp:946` | `mchan` copia el canal de la radio; la fuente canónica es el ioctl 132 (canal del maestro). Equivalentes mientras el maestro esté en el canal de la radio; si se siguiera a un maestro en otro canal, quedaría mal | Copiar el `mchan` del TLV 4 del maestro en `alignClock` |
| H6 | observación | `AWDLInterface.cpp:640-649`, `:699-709` | Codificación 3, step 3 y fill `0xFFFF` fijos; el 129 trae esos valores (`p[6]`/`p[7]`) y `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7.3 pide conservarlos. owly sólo acepta la forma fija y las capturas la usan siempre, así que fijarla es lo interoperable; los otros campos del 129 sí se copian | Ninguno necesario; documenta la desviación de §7.3 |
| H7 | observación | `AWDLInterface.cpp:994-1023` | `alignClock` sigue a cualquier nodo que se anuncie maestro de sí mismo (TLV 4 `+21 == A2`); owly sólo se alinea con su `election.sync` (`node.rs:433-443`) y `AWDL_WINDOW_TIMER.md` §3.2 habla del maestro electo (229/134). Con dos maestros autónomos en el rango, el reloj saltaría entre calendarios | Limitar la realineación a un maestro elegido (p. ej. el primero estable o el del último 229). Requiere OK |
| H8 | `?` fuera de alcance TX | `AWDL_TLV_BODIES.md` §2.1 | `convertOpClassToChannel` del kernel: no consta que acepte 128 (40 MHz) ni el par `{0,0}` con codificación 3; un fallo de conversión invalida el TLV completo (`-1`). Para la TX sólo importa el parseo del par (iPhones: aceptado por owly y por sus propias capturas con codificación 3 y slots a 0); para la RX propia (parseo de las tramas del par por el manager) está sin verificar | Verificar en IDA ambas conversiones antes de confiar la RX a TLVs de pares reales |

## 10. No verificado

- El comportamiento de `convertOpClassToChannel` con 128 y con `{0,0}` (H8).
- La frecuencia real de H1 en hardware (latencia RX del USB frente a `into`).
- Que el manager no produzca hops de elección ≥ 256 (H2) ni plantillas sin TLV 24 (§4).
- Unidad exacta de `phy_tx`/`target_tx` más allá del µs del host que espera `monitorTimeline`
  (`AWDL_FRAME_WIRE_FORMAT.md` §5.4 la da por buena; los docs la marcan `?`).
| # | Estado |
|---|---|
| H1 | Confirmado en el código y corregido en `2bb1c45`: `buildAf` toma la hora bajo `_txLock` y `txTick` la toma después del cerrojo. El mismo desbordamiento existía en `txTick` (`now` muestreado antes de `IOLockLock`) y queda corregido en el mismo commit |
| H2 | Corregido en `28b58e8`: la distancia de la elección v1 se satura a 255 |
| H3, H4, H5, H6 | Sin cambio |
| H7 | Corregido en `28b58e8`: `alignClock` sólo sigue al maestro de sincronía de la elección v2 de la plantilla (valor+6 del TLV 24) cuando es otro nodo; si no, al primer maestro tomado mientras se le oiga en 2 s |
| H8 | Pendiente de IDA |
