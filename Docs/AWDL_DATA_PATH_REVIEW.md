# Revisión de la ruta de datos AWDL: `AWDLInterface` (RX/TX) y `AWDLTx` (radio)

Búsqueda independiente de defectos en la ruta de datos nueva: `deliverData` (RX 802.11→Ethernet),
`txData`/`queueData`/`sendData`/`flushRadioTx`/`txTick`/`setRadioTx`/`free` (TX de datos y su anillo) en
`Source/AirLayer/AWDLInterface.cpp`/`.h` (HEAD: `5b686a8`, `5630d49`, `f2969a4`, `9dc71a6`) y
`rtl8814native_radio_tx_80211`/`rtl8814native_radio_awdl_port`/`rtl8814native_radio_set_channel` en
`Source/Driver/AWDLTx.c` (`42c04d3`, `0eb77c2`, `8726bd4`). Método: lectura línea a línea contra la ruta
STA de referencia (`Source/AirLayer/RTLAirLayer.cpp`, `txAction`/`rxFrames`), el formato en el aire de
owly (`~/Desktop/AWDLLab/refs/owly`, `frame.rs`), los docs `AWDL_RX_DATA_TO_SKYWALK.md`,
`AWDL_DATA_ENCAP.md`, `AWDL_STAGE1_IFUP_PATH.md`, `AWDL_STAGE3_EVENT_EMITTERS.md` y la spec del trabajo
(`AWDL_TX_DATOS_SPEC.md`), y el árbol vendor (`rtw_xmit.c`, `rtw_mlme_ext.c`, `wifi.h`, `rtw_xmit.h`,
`RTL8814Native.cpp`). Línea = HEAD de trabajo. Lo comprobado solo estáticamente (sin IDA ni hardware)
va con `?`.

## 1. Resumen de hallazgos

| # | Severidad | Dónde | Defecto |
|---|---|---|---|
| R1 | Alta | `AWDLInterface.cpp:736` | Uso tras liberación de `_rxCompQ` (y de `_rxPool` en la variante con teardown de pools) cuando `stop()` corre mientras el drain RX está repartiendo datos |
| R2 | Alta (`?`) | `AWDLInterface.cpp:920` | Ciclo de espera `thread_call_cancel_wait` (tick) contra la puerta del controlador que `postMessage` necesita; interbloqueo si `disable`/`stop` corren en el hilo que sirve esa puerta |
| R3 | Media | `AWDLInterface.cpp:1257` | `_txDataOff` queda pegado a `true` para siempre: ni `enable` ni `setRadioTx(true)` lo rearman |
| R4 | Media | `AWDLInterface.cpp:692` | `deliverData` omite el pre-hook `inputPacket` (+0xB50): sin filtro de pares ni retención del ioctl 415 |
| R5 | Media/Baja | `AWDLInterface.cpp:1186-1191` | Seq AWDL de 12 bits con semilla no monotónica: salto hacia atrás tras expulsión del asiento; contradice la spec propia y owly (u16) |
| R6 | Baja | `AWDLInterface.cpp:333` | `preQueuePacketCheckQuietMode` condicionado a `_txOn`; Broadcom lo llama por cada paquete encolado |
| R7 | Baja | `AWDLInterface.cpp:563` | `_rxHooked` leído fuera de `_rxLock` en el drain (carrera benigna, pero invalida el gate del R1) |
| R8 | Baja | `AWDLInterface.cpp:369,379` | `thread_call_free` silenciosamente fallido si la última release llega desde el propio drain |
| R9 | Baja | `AWDLTx.c:68-69` | `pmlmeext->mgnt_seq` incrementado sin cerradura desde dos hilos |
| R10 | Baja | `AWDLTx.c:111-117` | RMW de `MSR` (puerto 1) sin serializar contra `rtw_hal_set_msr` del MLME |
| R11 | Informativa | `AWDLInterface.cpp:652` | Copia de 2048 B en pila (`deliverData`) más `injectFrame`: ~3 KB de pila por trama de datos |
| R12 | `?` verificar | `AWDLInterface.cpp:680-687` | Convención de longitud de subtrama A-MSDU: el parser sigue owly (sin los 14 B de cabecera), IEEE 802.11 define longitud = MSDU + 14 |

## 2. Hallazgos con escenario y arreglo

### R1 (Alta) — Uso tras liberación de la cola RX de `awdl0` durante el teardown

`deliverData` encola en `_rxCompQ` sin retención (`AWDLInterface.cpp:736`), leyendo el puntero del
objeto. La cola vive en `removeQueues` (`:291-307`), que `disable()`/`release()` y pone a `nullptr`
dentro de `teardownDatapath` (`:348-355`) desde `stop()` (`:357-363`).

El drain (`rxDrainEntry`, `:551-569`) corre en un thread call aparte, no en la work queue: retiene el
objeto (`:545`), pero nada serializa su trabajo con `removeQueues`. La única guarda es
`if (me->_rxHooked)` (`:563`), leída sin cerradura y además de tipo check-then-act: una trama ya
sacada de la cadena cuando el hook se desactiva entra en `injectFrame`→`deliverData` y puede estar
entre `rtl_pool_alloc` y `rtl_rxcomp_enqueue` cuando `removeQueues` hace `q->release()`.

Escenario: AirDrop activo (tramas de datos RX fluyendo a ~10-50 por ventana) y `ifconfig awdl0 down`
seguido de destrucción de la interfaz (la cadena `detachVirtualInterface→terminate→stop` es síncrona,
`AWDL_PEERMANAGER_TEARDOWN.md`). `setRadioRx(false)` desactiva el hook y vacía la cola pendiente
(`flushRadioRx`), pero el drain ya sacó su cadena: `deliverData` encola sobre la cola liberada → pánico
en kernel. La ventana es pequeña (decenas de µs por trama) pero se repite con cada trama en vuelo.

El pool `_rxPool` no lo toca `removeQueues` (solo las colas), así que la ventana letal es la de
`_rxCompQ`; `free()` (`:392-394`) liberaría de nuevo los punteros ya anulados por `removeQueues` (eso
sí está cubierto con `OSSafeReleaseNULL` sobre el campo a `nullptr`).

Arreglo (el menor): retener la cola alrededor del encaje, con la misma cerradura que el teardown ya
toma. Por ejemplo: en `deliverData`, bajo `_rxLock`, copiar `void *q = _rxCompQ;` más
`((OSObject *)q)->retain()` si no es `nullptr` y `_rxHooked` sigue a `true`; soltar tras el enqueue.
En `removeQueues`, tomar `_rxLock` antes de `disable()`+`release()` y anular el campo dentro. Así el
enqueue sobre una cola ya liberada es imposible y el drain ve o el estado viejo o `nullptr`.
Alternativa más limpia: llevar el reparto de datos a la work queue con `runAction` (el patrón que ya
usa `bpfTapAction`, `:631-633`), que serializa con `removeQueues` por construcción; el coste es una
copia menos directa y pasar el `mbuf` como argumento de la acción.

### R2 (Alta, `?`) — Ciclo `thread_call_cancel_wait` ↔ `postMessage` en la puerta del controlador

`setRadioTx(false)` (`:920`) llama `thread_call_cancel_wait(_txTick)` y espera a que el tick termine.
El tick publica los eventos 41/43 con `postMessage` fuera de `_txLock` (`:1032`, `:1036`), y según
`AWDL_STAGE3_EVENT_EMITTERS.md` §1.2 esa llamada virtual entra en `reportDataPathEvents` y pasa por la
puerta del controlador (`IONetworkController::executeCommand`), que en su forma no-embebida **espera**
a que la puerta quede libre.

El ciclo se cierra si quien llama `setRadioTx(false)` es a su vez el hilo que sirve esa puerta, o un
hilo cuyo avance condiciona al portador de la puerta:

- `disable(unsigned)` llega desde `ioctlHandler` (`SIOCSIFFLAGS` → `adjustInterfaceEnable`,
  `AWDL_STAGE1_IFUP_PATH.md` §4); si el ioctl del netif Skywalk corre serializado en el work loop de la
  interfaz —que es la misma work loop donde vive la puerta del controlador (`?`, sin verificar en IDA)—,
  `cancel_wait` bloquea el hilo que atiende la puerta mientras el tick espera la puerta para publicar
  el evento 43/41 que coincide en el instante del `down`.
- `stop()` llega desde la cadena síncrona `terminate→stop` del teardown; si esa cadena corre con la
  puerta tomada (`?`), mismo ciclo con la radio entera bloqueada (cada ioctl posterior se cuelga).

Escenario concreto: ventana que abre/cierra cada 4 AW (~65 ms); un `ifconfig awdl0 down` que caiga
mientras el tick está dentro de `postMessage` y con la puerta ocupada por otro ioctl → los dos hilos
se esperan mutuamente y `en2`/`awdl0` quedan sin atención de puerta.

Arreglo: no esperar nunca desde `setRadioTx`. El patrón ya existe en el drain RX: `txTickEntry`
retiene el objeto al entrar y libera al salir (`retain` antes de `thread_call_enter`, `release` al
final de `txTick`), y `setRadioTx(false)` se limita a `_txOn = false` bajo `_txLock` +
`thread_call_cancel(_txTick)` (sin wait). `free()` ya no necesita el invariante «stop esperó al tick»:
la retención por invocación garantiza que el tick no toque el objeto después del último `release`, y
`thread_call_free` puede fallar en silencio igual que hoy (R8). Con eso el `down` nunca bloquea al
que atiende la puerta.

### R3 (Media) — `_txDataOff` sin rearme

`sendData` fija `_txDataOff = true` ante `kIOReturnBadArgument` (`:1254-1259`) y nada lo vuelve a
`false`: ni `setRadioTx(true)` (la transición `on && !was` de `:897-904` reinicia el reloj pero no este
flag), ni `enable`, ni un cambio de driver en caliente. Tras un solo rechazo (driver anterior a AWDL
data TX, o una trama que el driver rechace por longitud/`frame[1]`), todo el TX de datos de `awdl0`
queda muerto hasta recargar el kext, aunque la causa desaparezca.

Arreglo: en la transición de encendido (`setRadioTx`, bloque `if (on && !was)`), añadir
`_txDataOff = false;` bajo `_txLock`. El caso driver-viejo se reintenta una vez por `enable`, que es
el coste correcto.

### R4 (Media) — `deliverData` no llama a `inputPacket` (+0xB50)

El comentario (`:643-644`) lo declara: «The peer filter of IO80211VirtualInterface::inputPacket (§6)
is not applied». Pero el contrato documentado (`AWDL_RX_DATA_TO_SKYWALK.md` §6 y §8.6;
`AWDL_DATA_ENCAP.md` §7.5) dice que Broadcom llama al slot +0xB50 antes de encajar y que **la familia
no filtra nada que el driver encole sin esa llamada**: sin ella llegan a la pila tramas de nodos no
emparejados, no se aplican los casos especiales `33:33:00:00:xx:xx` / `01:00:5E:00:xx:xx`, y los
paquetes retenidos a la espera del ioctl 415 (registro de tráfico) no existen — wifip2pd ve los datos
antes de que exista el servicio registrado.

Escenario: un vecino con AWDL a la vista (no emparejado) envía tramas de datos al grupo o a la MAC de
`awdl0`: la pila las procesa como si el par existiera; y la ventana de privacidad que el 415
implementa desaparece.

Arreglo: tras `rtl_netpkt_setLinkHdrLen` (`:730`) y antes de añadir a `pkts[]`, construir una etiqueta
RX de 12 B (bit 0 = multicast, `+6..+11` = SA) y llamar al slot +0xB50 de la interfaz con
`(pkt, etiqueta, frame, &flag, 0)` como Broadcom en `0xffffff800154a65c`; retorno 0 → `rtl_pool_free`
y `_rxDataNotUs++`. Es una llamada virtual de la propia interfaz, sin dependencias nuevas. Si se
prefiere mantener la omisión para la primera fase, dejarlo registrado en `AWDL_TX_DATOS_SPEC.md`/el
índice como decisión y no como pendiente implícito.

### R5 (Media/Baja) — Secuencia AWDL: 12 bits y semilla hacia atrás

`queueData` (`:1186-1191`): `seq = e->seq & 0xFFF` y la semilla de un asiento reciclado es
`(uint16_t)(now >> 4)` (`:1187`), truncada a 12 bits al usarse. Dos desviaciones:

1. owly mantiene un contador u16 por nodo que envuelve en 65536 (`frame.rs`, `awdl_seq.wrapping_add`),
   y la spec propia (`AWDL_TX_DATOS_SPEC.md` §3.2) decía «primera trama 0» con envoltura a `0x0FFF`;
   el commit `f2969a4` cambió la semilla a no nula sin actualizar la spec.
2. Tras expulsión del asiento (16 destinos LRU) y reaparición del destino, la secuencia **salta hacia
   atrás** a un valor derivado del reloj. Si el receptor Apple usa el campo para deduplicar/reordenar
   (semántica marcada `?` en `AWDL_DATA_ENCAP.md` §9.2), un retroceso puede costar descartes
   transitorios; con envoltura a 4096 además el ciclo es 16× más corto que el de owly.

Arreglo mínimo: contador u16 completo monotónico por asiento (`e->seq = (uint16_t)(e->seq + 1)` sin la
máscara en el uso, manteniendo la semilla solo para el caso recién creado) o, si se conserva el nibble
alto a 0 por decisión, al menos sembrar con `(uint16_t)(awdlNowUs() >> 4)` completo y envolver a
65536. Requiere confirmar con captura qué espera el receptor (`?` ya anotado en la spec).

### R6 (Baja) — Notificación de quiet mode condicionada a `_txOn`

`txData` (`:333`): `void *pm = me->_txOn && len >= 14 ? ...`. Con `rtlawdl_tx=0` (o antes de
`enable`), la cola de envío sigue entregando paquetes de la pila y `queueData` los descarta, pero
`preQueuePacketCheckQuietMode` no se llama: el gestor permanece en quiet mode y su filtrado de datos
queda desalineado con lo que Broadcom hace por paquete encolado (`AWDL_TX_HOOKS_POR_PAQUETE.md` §2,
d97c680). Arreglo: quitar `_txOn &&` de la condición (la llamada es barata y no toca la radio).

### R7 (Baja) — `_rxHooked` sin cerradura en el drain

`rxDrainEntry` lee `me->_rxHooked` fuera de `_rxLock` (`:563`). Carrera benigna por sí misma (peor
caso: liberar una trama inyectable), pero es el gate que R1 convierte en UAF. Se cierra con el arreglo
de R1 (leer y retener bajo `_rxLock`).

### R8 (Baja) — `thread_call_free` puede fallar en `free()`

`free()` (`:369`, `:379`) llama `thread_call_free` ignorando el retorno. Si la última release llega
desde `rxDrainEntry` (caso normal de descarga con tráfico), `free()` corre **en el hilo del propio
drain** con la thread call aún marcada «running»: `thread_call_free` devuelve `false` y la estructura
se fuga (una vez, en descarga). Con el arreglo de R2 (retención por invocación también en el tick) el
mismo patrón aplica a `_txTick`. No es corriente ni corruptor; registrarlo y, si se quiere cerrar,
usar `thread_call_cancel` + aceptar la fuga solo si devuelve `false`, o posponer el `thread_call_free`
a un trabajo diferido.

### R9 (Baja) — `mgnt_seq` compartido sin cerradura

`rtl8814native_radio_tx_80211` hace `SetSeqNum` + `pmlmeext->mgnt_seq++` (`AWDLTx.c:68-69`) desde el
tick AWDL (thread call) mientras los hilos de comando STA hacen lo mismo. Sin cerradura, dos tramas
pueden salir con el mismo número. Hoy es inofensivo: el descriptor renumera las no-QoS con
`HWSEQ_EN` (comentario `:71-74`), y ambas rutas emiten no-QoS. Deja de serlo si algún día se envía
QoS por aquí. Arreglo: incremento con `atomic_fetch_add` o con el cerrojo que ya protege la cola de
comandos.

### R10 (Baja) — RMW de `MSR` del puerto 1 no serializado

`rtl8814native_radio_awdl_port` hace read-modify-write de `MSR` bits 3:2 (`AWDLTx.c:111-117`) sin la
serialización que usa `rtw_hal_set_msr` del MLME. Un cambio de modo del STA concurrente (link up/down)
puede pisar los bits del puerto 1 o ser pisado por ellos: el efecto es el mismo que el ya documentado
en la revisión del parking (pérdida de ACKs hasta el próximo `enable`/`disable`). Con concurrent off
las ventanas son estrechas. Arreglo: reutilizar el mismo cerrojo que el MLME usa para `MSR`, o
releer-verificar tras escribir y repetir una vez.

### R11 (Informativa) — Presión de pila en la ruta RX

`deliverData` copia la trama a `uint8_t f[2048]` en pila (`:652`) y añade `sub[16]` (512 B) y
`pkts[16]` (128 B); la cadena `rxDrainEntry→injectFrame→deliverData` suma ~3 KB desde el hilo del
thread call. Con las pilas de kernel de 16 KB de macOS x86_64 hay margen; con pilas de 8 KB (otras
plataformas/`?`) quedaría justo. Si se portara, la variante por `runAction` de R1 además saca la
copia a la pila de la work queue.

### R12 (`?`) — Convención de longitud de la subtrama A-MSDU

El parser (`:680-688`) sigue owly: la longitud BE16 de cada subtrama cuenta **solo el cuerpo** (SNAP +
AWDL head + payload, `16 + payload`; `frame.rs:344`), y el avance es `14 + sl + pad`
(`frame.rs:246`). IEEE 802.11 define esa longitud **incluyendo** los 14 bytes de la subtrama
(`MSDU + 14`). Si un iPhone emite con la convención IEEE, cada subtrama se entrega con 14 bytes de
más y la caminata se desincroniza desde la segunda subtrama (las tramas de un solo MSDU no QoS, la
mayoría del tráfico temprano, no están afectadas). owly podría haber normalizado a su propia
convención: la fuente primaria es una captura de iPhone. Verificación propuesta: replay por
`deliverData` de una trama A-MSDU capturada (el extractor de `AWDLLab` ya reconstruye registros;
hace falta una captura de datos, no solo AF) o prueba con el peer ESP32 emitiendo ambas
convenciones. Arreglo defensivo barato mientras tanto: si el SNAP de +14 no casa pero el de
`+14+sl-14` sí, reinterpretar con `sl -= 14` y seguir la caminata con la convención detectada.

## 3. Comprobaciones que salieron limpias

Para no repetir el trabajo en la próxima pasada:

- **Formato TX en el aire** (`queueData` `:1199-1216`): FC `0x08` no-QoS como owly `frame.rs:313`
  (`data_frame`), A1=DA `+4`, A2=SA `+10`, A3=BSSID AWDL `+16`, SNAP `AA AA 03 00 17 F2 08 00`,
  `03 04` en `+8/+9`, seq u16 **LE** en `+10` del cuerpo, `00 00`, ethertype **BE** recalado de
  `eth[12..13]`, payload `+16`. La trama 802.11 va sin QoS y sin A-MSDU (como owly). El hardware
  renumera la secuencia 802.11 (`AWDLTx.c:71-74`).
- **Aritmética del anillo** (`:1155-1217`): `len ≤ 1442` (14 + MTU 1428), trama máx. `24+16+1428 =
  1468` = tamaño exacto de `AwdlTxFrame.frame`; `sizeof(_txRing->frame)` no desreferencia; drop-oldest
  correcto; el re-encolado de `sendData` tras `Busy` (`:1262-1272`) no pisa tramas vivas (la ventana
  ocupada tras retroceder el head no alcanza al último encolado: `head-1` está fuera de
  `[head, head+count)` con `count ≤ 31`).
- **Copias con `offsetof`** (`:1236`, `:1266`): `6 + len ≤ 1474 ≤ sizeof(AwdlTxFrame)` (1476 con
  padding). Seguras.
- **Edad de trama** (`:1240`): aritmética u32 con envoltura correcta (diferencia módulo 2³², edad
  1,2 s ≪ periodo de envoltura de ~71,6 min).
- **RX `deliverData` límites** (`:652-688`): la trama llega ≤ 2048 del hook (`:511`); `len-4 ≤ 2044`;
  el walk A-MSDU acota `off+14+sl ≤ len` antes de cortar y el `memcmp` del SNAP tras exigir
  `sl ≥ 16` (cortocircuito correcto); `elen ≤ 2044` cabe en el paquete del pool (2048) y se re-verifica
  contra `getBufferSize` (`:714`); sin lecturas fuera del buffer de pila en ningún orden de
  cortocircuito (el único roce, `f[22]` con `len < 24` en `:667`, está cubierto por el `len < hdr`
  final).
- **Contratos Skywalk**: `prepare` antes de tocar el buffer (`:713`, con los pánicos 0.15/0.16
  documentados); `enqueuePackets` con `n < capacidad` (trozos de 64 < 128 en `txData` `:339-341`;
  `n ≤ 16` < 128 en `deliverData` `:736`); los paquetes se completan exactamente una vez (o se
  devuelven al pool en cada error: `:342-343`, `:715-717`, `:725-728`, `:738-740`); la acción devuelve
  `count` (IFUP §5) y solo devuelve 0 en el guardia de `_txCompQ` nulo, con las colas ya muertas.
- **Cerraduras de TX**: la radio solo se llama con `_txLock` suelta (`sendAf` `:1135`, `sendData`
  `:1245`); `postMessage` fuera de la cerradura (`:1030-1039`); `buildAf` lee `_tpl`/reloj bajo
  cerradura (`:1292-1296`), herencia de la corrección H1; `servedMask` y la secuencia del 129 bajo
  cerradura; invariante del rearmado del tick verificado (`cancel_wait` en toda transición a off
  garantiza que `thread_call_enter` de un enable posterior programa de verdad).
- **Límites del driver**: `len ≤ 1536-48 = 1488` (`MAX_XMIT_EXTBUF_SZ - TXDESC_OFFSET`,
  `rtw_xmit.h:89/200` y `rtl8814bu_hal.h:27`) ≥ 1468 del anillo y ≥ 1024 de los AF; el gate de la
  mitad del pool (`AWDLTx.c:49`) consulta `free_xframe_ext_cnt`, que es exactamente el contador que
  `alloc_mgtxmitframe → rtw_alloc_xmitframe_ext` consume (`rtw_mlme_ext.c:7496-7527`,
  `rtw_xmit.c:3608-3618`); el fallo del buffer ext queda cubierto por el `NULL` de
  `_alloc_mgtxmitframe`. La máscara `frame[1] & 0x43` rechaza To/From DS, more-frag y protegido; el
  BMC bit quita los reintentos del multicast (`:75-77`); rate OFDM 6/12 en el BSSID AWDL aunque el
  STA esté en 2,4 GHz (`:60-63`).
- **`setRadioTx`**: `cancel_wait` antes de soltar el puerto 1 (`:920-923`); `flushRadioTx` al final;
  el reloj se reinicia solo en la transición; `_port1On` solo queda a `true` si el puerto respondió,
  y `queueData` exige puerto 1 para unicast (`:1169-1173`).
- **RX hook/cola de mbufs**: límite 64, copia con `mbuf_copyback` antes de encolar, kick solo en el
  primer elemento, retención del objeto mientras el drain vive (`:545-548`, `:568`); sin fugas de
  mbufs en ningún retorno de `radioRxHook`/`rxDrainEntry`/`injectFrame`/`deliverData`.
- **Park y secuencia**: `parkRadio` y el 129 leen `_seq` bajo `_txLock`; el fracaso definitivo solo
  con `Unsupported`/`BadArgument` (`9dc71a6`); `RTW_CMDF_CHBW_IDLE_ONLY` re-verifica el MLME en el
  hilo de comandos (`8726bd4`).

## 4. Pendientes de verificación externa

1. R2: confirmar en IDA en qué hilo/lock corre `ioctlHandler` (SIOCSIFFLAGS) y la cadena síncrona
   `terminate→stop` de la interfaz role 6, y si esa work loop es la que sirve la puerta del
   controlador que `postMessage` espera (`AWDL_STAGE3_EVENT_EMITTERS.md` §1.2).
2. R12: convención de longitud A-MSDU de un iPhone real (captura de datos).
3. R5: semántica del campo seq en el receptor Apple (ya `?` en `AWDL_DATA_ENCAP.md` §9.2).
4. R11: tamaño de pila del hilo de thread call en el arranque objetivo (16 KB asumido).

## 5. Estado tras la verificación

Las líneas de las secciones anteriores son las del árbol previo al arreglo.

| # | Estado | Detalle |
|---|---|---|
| R1 | Corregido | `stop()` espera con `thread_call_cancel_wait` al drain RX (y al tick) después de `setRadioRx(false)` y antes de `teardownDatapath`; una llamada pendiente cancelada suelta su referencia. `stop` corre en el hilo de terminación de IOKit sin la puerta del controlador (`AWDL_PEERMANAGER_TEARDOWN.md`, paso 6), así que la espera no se cruza con `postMessage`. |
| R2 | Corregido | IDA (25G241): `IOSkywalkNetworkBSDClient::ioctlHandler` (`0xffffff8002affce6`) reentra en su propia puerta si no está en ella y llama a `adjustInterfaceEnable` (`0xffffff8002b006ee`, sitios `0xffffff8002affed5` y `0xffffff8002afffcf`) con esa puerta tomada, entre las llamadas de dormir y despertar de `this+0x90` (ranuras `+0x1E0` y `+0x1E8`). Que esa puerta sea la del controlador queda sin comprobar; el arreglo no depende de ello: `setRadioTx(false)` ya no espera (solo `thread_call_cancel` bajo `_txLock`), cada llamada del tick pendiente o en curso retiene el objeto (`armTick`, como el drain RX) y `_tickBusy` impide dos ticks a la vez cuando un `enter` llega con un tick aún en marcha; ese tick se rearma solo si encuentra `_txOn`. |
| R3 | Corregido | `setRadioTx(true)` pone `_txDataOff = false` en la transición a encendido. |
| R4 | Diferido | Omisión deliberada de la primera fase, anotada en el comentario de `deliverData` y en `AWDL_PRUEBA_CON_IPHONE.md` §7: sin el pre-hook `+0xB50` llega a la pila más tráfico, no menos, y la retención del ioctl 415 no existe. Se revisa tras la prueba con un par. |
| R5 | No es defecto | Broadcom codifica `seq = (contador & 0xFFF) \| (AC << 12)` con un contador por destino (`AWDL_DATA_ENCAP.md` §7, punto 3); con AC 0 coincide con el código. owly escribe el u16 sin máscara, pero la referencia es el driver de Apple. La semilla por reloj tras expulsión se mantiene. |
| R6 | Se mantiene | Con TX apagado no hay ventanas ni envío: sacar al gestor de quiet mode no cambia nada en el aire. |
| R7 | Cerrado con R1 | La espera de `stop()` deja sin efecto la lectura sin cerradura. |
| R8 | Sin cambio | Se registra; con R2 el tick sigue el mismo patrón de referencia que el drain. |
| R9, R10 | Sin cambio | Inofensivos hoy por `HWSEQ_EN` y con concurrent mode apagado. |
| R11 | Sin cambio | Informativa. |
| R12 | No es defecto | IEEE 802.11 define la longitud de la subtrama A-MSDU como la longitud del MSDU, sin los 14 B de DA, SA y longitud; el parser y owly siguen esa convención. |
