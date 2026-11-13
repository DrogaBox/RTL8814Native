# AWDL: rendimiento, segunda parte (bomba de datos y A-MPDU de salida)

Estado: cargado y medido con el par virtual el 2026-11-11 (§7): el camino en software pasa de 1,5 MB/s a
unos 7,5 MB/s a 20 MHz y unos 19 MB/s a 80 MHz. En el aire el MAC emite
los agregados (§8); que un iPhone los reciba no está probado. Lo que falta está en §6. Con la radio
compartida con un BSS hay un reparto de slots nuevo y un defecto del driver corregido: 15,5 MB/s con `en2`
asociado (§9). El retorno al canal de AWDL durante una búsqueda de redes sin
asociación está cargado y medido con el par virtual: el corte de 4 s de cada búsqueda completa pasa a
huecos de 0,25 a 0,6 s (§10). La radio dejó de recibir una vez tras una asociación, con causa sin
demostrar y una protección cargada (§11).

Continúa `AWDL_RENDIMIENTO_1.md`. Archivos: `Source/AirLayer/AWDLInterface.cpp`,
`Source/AirLayer/AWDLBaOrig.h`, `Source/Driver/AWDLTx.c`, `Source/Driver/RTLRadioABI.h`,
`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c`.

## 1. Qué limitaba el envío

Medido el 2026-11-09 con el par virtual: unos 1135 frames/s del Mac al par, 1,5 MB/s, sin importar cuánto
se ofreciera. Las causas, leídas en el código:

| Causa | Dónde | Efecto |
|---|---|---|
| Los datos salían una vez por AW | `txTick` llamaba a `sendData` en cada frontera de AW (16 TU) | Un paquete que llegaba a mitad de un AW esperaba al siguiente; una radio llena cortaba el lote hasta el siguiente AW |
| Un frame por acceso al medio, a 54 Mb/s como mucho | `AWDLTx.c` mandaba cada frame por la cola de gestión con tasa fija | A 54 Mb/s un frame de 1400 bytes cuesta unos 370 µs con su ACK: el presupuesto de aire de un AW da unos 30 |
| 16 buffers para datos | `NR_XMIT_EXTBUFF / 2` | Tope de frames en vuelo |
| Anillo de 64 frames | `AWDL_TX_RING` | Ráfagas mayores se perdían antes de salir |

La referencia pública ForbiddenItlwm (Intel AX211, 2026-11-10) obtiene unos 20 MB/s con Block Ack de salida
y A-MPDU a 80 MHz. Es la pieza que faltaba aquí; la compuerta que retiene los datos mientras el par está en
otro canal ya existía (`peerOnChannel`).

## 2. La bomba de datos

`txTick` sigue corriendo una vez por AW, pero ya no envía: deja escrito lo que los datos pueden hacer en ese
AW y llama a `pumpData`.

| Campo | Significado |
|---|---|
| `_pumpOk` | Hay ventana de datos: slot servido, ventana abierta, fuera de la guarda del inicio, y no es el último AW salvo `rtlawdl_lastaw=1` |
| `_pumpSlot`, `_pumpCh` | Slot y canal de ese AW |
| `_pumpEndUs` | Fin: la frontera del AW, o 3 ms antes del fin del slot en el último AW |
| `_pumpAir` | Aire que queda en el AW, en µs. Empieza en `rtlawdl_air` con cada AW |
| `_pumpStall` | El AW ya no da más (aire gastado, o el par está en otro canal) |

`pumpData` envía del anillo mientras `_pumpOk`, antes de `_pumpEndUs` y con aire. Se llama desde tres sitios:

- `txTick`, al empezar cada AW.
- `queueData`, cuando entra un paquete con la ventana abierta y nadie está enviando.
- Ella misma, 1 ms después, cuando la radio devolvió ocupado.

Solo hay un emisor a la vez (`_sendBusy`), así que los frames conservan el orden. La radio se llama sin el
candado tomado. `rtlawdl_pump=0` deja solo la llamada de `txTick`, que es el comportamiento anterior.

El anillo pasa de 64 a 256 frames. El driver pasa de 32 a 128 buffers de gestión y los datos pueden usar
todos menos 16.

## 3. A-MPDU de salida

### 3.1 Acuerdo de Block Ack (capa, `AWDLBaOrig.h`)

Un A-MPDU necesita un acuerdo de Block Ack con el receptor. La capa lo pide ella: la familia no lo hace.

| Paso | Regla |
|---|---|
| Pedido | 16 frames unicast a un par en 1 s. ADDBA request: TID 0, ventana 64, inmediato, sin A-MSDU, sin timeout. Va a la cabeza del anillo |
| Sin respuesta | Se repite a los 500 ms, 3 veces. Después, frames sueltos durante 60 s |
| Respuesta | Estado 0, inmediato, TID 0 y ventana mayor que 0 abren la sesión. Otra cosa: frames sueltos 60 s |
| DELBA del par | Cierra la sesión; frames sueltos 10 s |
| Sesión sin uso | A los 10 s se vuelve a pedir, con la numeración siguiendo |
| Vigilancia | 64 frames o más enviados en 2 s sin que llegue ningún frame de datos del par: la sesión se da por rota, se manda DELBA y quedan frames sueltos 60 s. `rtlawdl_aggwd=0` la apaga |

La vigilancia existe porque el RTL8814AU no informa del resultado de lo que transmite (medido el
2026-11-09: ningún informe CCX). Un tráfico real de AirDrop siempre trae frames de vuelta.

Hasta 4 pares. Una sesión viva no se desaloja mientras haya otra entrada.

### 3.2 Frames de una sesión

Con la sesión abierta, los datos unicast a ese par salen como QoS data (subtipo 8, TID 0). El número de
secuencia se toma al entregar el frame a la radio y solo avanza si la radio lo aceptó. Así un frame que
caduca en el anillo o que la radio rechaza no deja un hueco, y el receptor no espera un número que no va a
llegar. El número del primer frame QoS es el que anunció el ADDBA.

Los frames de grupo, los de gestión y los de pares sin sesión salen como antes.

### 3.3 Driver

Función nueva de la ABI de radio, `rtlRadioAwdlAgg`, con `struct rtl_radio_awdl_agg`: tasa (índice de
descriptor, 0 apaga), ancho máximo, intervalo de guarda corto, MPDU por A-MPDU, densidad; devuelve el ancho
al que está sintonizada la radio. `RTL_RADIO_ABI` no cambia: una capa nueva con un driver viejo recibe
`kIOReturnUnsupported` y no pide sesiones.

En `rtl8814native_radio_tx_80211`, un frame de datos AWDL unicast que es QoS data, con una tasa puesta:

- conserva el número de secuencia de su cabecera (`SET_TX_DESC_SEQ`, sin `HWSEQ_EN`);
- va a la cola de su TID (BE para TID 0) y no a la de gestión;
- lleva `AGG_ENABLE`, `MAX_AGG_NUM`, `AMPDU_DENSITY`, la tasa, `DATA_BW` y `DATA_SC`.

Es el mismo conjunto de campos que el árbol del fabricante pone a los datos de la estación con
`ampdu_en`. El frame sigue saliendo por el camino de frames de gestión (`dump_mgntframe`), con el MAC ID de
gestión.

### 3.4 Tasa

VHT de dos flujos, elegida por la señal de los pares como hace `rateByRssi`. Baja en el acto, sube con 3 dB
de margen, y se vuelve a dar a la radio cada segundo. Un canal más ancho pide 3 dB más por cada duplicación.

| MCS | Mb/s a 20 MHz | Mb/s a 80 MHz | RSSI mínimo a 20 MHz |
|---|---|---|---|
| 0 | 13 | 58 | sin mínimo |
| 1 | 26 | 117 | -76 dBm |
| 2 | 39 | 175 | -73 dBm |
| 3 | 52 | 234 | -70 dBm |
| 4 | 78 | 351 | -66 dBm |
| 5 | 104 | 468 | -62 dBm |
| 7 | 130 | 585 | -58 dBm |
| 8 | 156 | 702 | -54 dBm |

Los umbrales son estimaciones, como los de la escalera de frames sueltos. La caída de tasa en los reintentos
queda activada en el descriptor.

El ancho lo decide dónde está sintonizada la radio. Aparcada a 20 MHz, los A-MPDU van a 20 MHz. Para 80 MHz:
`Tools/test_uc awdlbw 80`.

El coste de aire de un frame de sesión es 40 µs más sus bits a la tasa de sesión, contra 160 µs más sus bits
a la tasa de la escalera para un frame suelto.

## 4. Argumentos de arranque

| Argumento | Por defecto | Efecto |
|---|---|---|
| `rtlawdl_pump` | 1 | 0: los datos solo salen al empezar cada AW |
| `rtlawdl_agg` | 1 | 0: sin sesiones de Block Ack de salida |
| `rtlawdl_aggwd` | 1 | 0: no cerrar una sesión por silencio (pruebas de un solo sentido) |
| `rtlawdl_aggmax` | 31 | MPDU por A-MPDU, 1 a 31 |
| `rtlawdl_aggden` | 6 | Densidad de A-MPDU, 0 a 7 |
| `rtlawdl_aggbw` | 80 | Ancho máximo: 20, 40 u 80 |

`Tools/phase5_deploy.sh` los acepta en `RTL8814_AWDL_EXTRA` y los quita en cada despliegue de la capa.

## 5. Pruebas hechas (fuera del kernel)

| Prueba | Qué cubre | Resultado |
|---|---|---|
| `Tools/baorig_test.cpp` | Máquina de estados del acuerdo, frames ADDBA y DELBA, conversión a QoS, vigilancia, desalojo | Pasa con ASan y UBSan |
| `pumptest.cpp` (función `pumpData` extraída tal cual) | Orden, presupuesto de aire, numeración sin huecos tras un rechazo, fin de ventana, candados equilibrados, radio nunca llamada con el candado | Pasa |
| `hbatest.cpp` (`handleBlockAck` extraída) | Respuesta ADDBA con token correcto e incorrecto, estado 37, DELBA del par | Pasa |
| `cargo test` de `Tools/awdlsim` | El par virtual acepta el ADDBA del Mac y lee su QoS data en orden | Pasa |
| Compilación | Driver y capa | Sin errores |

Las tres pruebas con funciones extraídas están en `~/re_cache/work/awdl_tests` (`bash run.sh`).

## 6. Sin comprobar

Las cuatro primeras filas se comprobaron el 2026-11-11 (§7 y §8); de la tercera, la emisión y la agregación,
no la tasa. El resto sigue sin comprobar:

| Qué | Cómo |
|---|---|
| La bomba no rompe lo que andaba | Envío de un archivo al par virtual, comparar con 1,5 MB/s |
| El pedido de sesión y la numeración | `awdlsim` imprime `ADDBA request from awdl0` y `from awdl0: sessions N qos N gaps N`; `gaps` debe quedar en 0 |
| La radio emite A-MPDU a tasa VHT | Captura del A53 en modo monitor: tasa, ancho y bit de agregación de los frames del Mac |
| El MAC del RTL8814AU agrega con el MAC ID de gestión y tasa fija | La misma captura. No hay documentación del chip que lo asegure |
| El MAC procesa los BlockAck dirigidos a la dirección del puerto 1 | Solo con un par real: iPhone |
| El iPhone acepta el ADDBA y el QoS data en AWDL | iPhone. ForbiddenItlwm lo hace con un iPhone 11 |
| Los umbrales de señal | Captura con el iPhone |
| Cola BE compartida con los datos de la estación durante el tiempo compartido | Sin medir. Con la radio aparcada no hay tráfico de estación |

El par virtual no tiene radio: no manda BlockAck. En el aire, los A-MPDU hacia él se reintentan y se
descartan. Lo que el par virtual mide es el camino en software, tomado antes de la antena.

## 7. Medido en el kernel con el par virtual (2026-11-11)

Driver `B74035FC`, capa `92DFA1D0`, macOS 26.7.3 (25G342), `rtlawdl_air=15000 rtlawdl_lastaw=1`. Par virtual
`awdlsim --addba --tun`. Carga: TCP desde `awdl0` hacia un sumidero en el `utun` del par
(`~/re_cache/work/awdl_tests/tcpflood.py`), que trae ACK de vuelta como una transferencia de AirDrop.

| Radio | Antes (2026-11-09) | Ahora | Notas |
|---|---|---|---|
| Aparcada en el 149, 20 MHz | 1,5 MB/s, unas 1135 tramas/s | 7,3 a 7,7 MB/s, unas 5450 tramas/s | Sesión a MCS 8, 156 Mb/s |
| Aparcada en el 149, 80 MHz (`awdlbw 80`) | sin medir | 18,5 a 19,4 MB/s | Sesión a MCS 8, 702 Mb/s; 191 MB en 16 s contando los cortes |
| Compartida con un BSS en el 153 | unos 240 kB/s (AirDrop) | 785 kB/s | Solo 2 de 16 slots llevan datos: la presencia densa no se activa con la radio compartida |

Los valores con la radio aparcada son los de los tramos sin corte. Con `en2` desasociado el sistema busca
redes cada 13 s y la radio deja el canal unos 4 s; en esos tramos no pasa nada, ni datos ni las líneas de
estado del par virtual. Los cortes coinciden al segundo con las ráfagas de `scan` de `airportd`.

Lo que quedó comprobado:

- El pedido de sesión sale y el par lo acepta: `ADDBA request from awdl0: token 2 ssn 4042 -> accepted`.
- La numeración: `from awdl0: sessions 3 qos 199723 gaps 1`. El único hueco apareció durante uno de los cortes
  de búsqueda de redes.
- Los bytes recibidos por el sumidero son los enviados en las tres corridas TCP.
- Contadores de la capa en una corrida: `A-MPDU frames 55627 at 156 Mb/s, sessions asked 5 on 2 refused 0
  unanswered 1`, `busy 11893`, `pump runs 12452`.
- El enlace de estación asoció con los kexts nuevos: ping al router sin pérdida.

Defecto encontrado y corregido en el código (capa `5EC1F0A9`, compilada, sin cargar): con el anillo lleno,
cada paquete nuevo desalojaba la cabeza, y ahí estaba el pedido ADDBA. Bajo una inundación UDP de 54000
paquetes/s la capa anotó tres pedidos y al par no llegó ninguno. Ahora un frame de gestión en la cabeza se queda y se
pierde el paquete nuevo. Con TCP el pedido salió siempre.

La inundación UDP de un solo sentido no sirve para medir la sesión: sin frames de vuelta la vigilancia la
cierra a los 2 s (§3.1). Con frames sueltos dio unas 890 tramas/s con los cortes incluidos, sin cambio
apreciable frente a las 1135 de antes: ahí manda la radio, que devuelve ocupado.

Estas cifras son del camino en software, tomado antes de la antena. El par virtual no manda BlockAck. Que el
chip emita de verdad A-MPDU a esa tasa y que un iPhone los reciba sigue en §6.

Para la prueba con el iPhone: con `en2` asociado en un canal que no es social, la radio se comparte y el
techo es el de la tercera fila. Para los valores de las dos primeras hay que desasociar `en2`. Esa tercera
fila se midió bajo el defecto de §9.3.

## 8. En el aire: el MAC emite los agregados (2026-11-11)

Testigo: el teléfono en modo monitor a 20 MHz en el 149 (`~/Desktop/a53/telefono/vigia_mac.sh`).

### 8.1 Lo que el teléfono no oye

El teléfono no decodificó ningún frame QoS de la sesión, en tres montajes: a 80 MHz con MCS 8, a 80 MHz con
tasa baja y con la radio aparcada a 20 MHz con MCS 0 (unos 13000 frames enviados). Tampoco decodificó los
datos de estación del Mac hacia el router: de 11201 paquetes oyó 9176 RTS del Mac y 9479 BlockAck del
router, y ningún frame de datos. Sí informa frames agregados de otras estaciones del canal. Sus capturas no
dicen entonces si el Mac emite los frames de la sesión.

Para bajar la tasa de la sesión el par virtual declara menos señal (`awdlsim --rssi`). Con -80 dBm el Mac no
instala la entrada de vecino del par y no hay ruta hacia él; con -72 dBm lo acepta.

### 8.2 La prueba: CTS-to-self

`RTL_OPT_AWDL_PROT` (`Tools/test_uc awdlprot 0|1|2`: nada, RTS/CTS, CTS-to-self) hace que el MAC proteja los
frames de la sesión. La protección va a tasa OFDM y el teléfono la oye; su campo Duration dice cuánto dura
lo que viene detrás.

Driver `70AB93CF`, capa `5EC1F0A9`, `awdlprot 2`, `en2` asociado en el 153 (la radio conserva los 80 MHz en
el 149, que está en el mismo bloque), par a -72 dBm, 14 s de TCP:

| Dato | Valor |
|---|---|
| Frames QoS entregados a la radio | 6769 |
| CTS oídos con RA igual a la dirección de `awdl0` | 944, a 6 Mb/s, todos dentro de la transferencia |
| Duration de esos CTS | 820 entre 3000 y 3200 µs (3184 el valor repetido); el resto repartido entre 200 y 3000 µs |
| Separación entre CTS seguidos | mediana 3,42 ms |

Lectura:

- El MAC saca los frames de la sesión por la cola de datos y los pone en el aire: no hay CTS-to-self sin un
  frame detrás.
- Los agrega. A los 58,5 Mb/s de VHT MCS 0 de dos flujos a 80 MHz, 3184 µs son unos 15 MPDU de 1468 bytes; un
  solo MPDU dura unos 250 µs. Las duraciones menores son agregados más cortos.
- El CTS lleva la dirección de `awdl0`, no la de la estación.
- 6769 frames en 944 CTS dan 7,2 por CTS. Es coherente con que cada agregado salga dos veces, porque el par
  virtual no manda BlockAck; no se midió.

Sin comprobar: que un receptor decodifique esos PPDU VHT, y el BlockAck de vuelta. Eso pide un par real.

### 8.3 Arreglo del anillo, comprobado

Con la capa `5EC1F0A9`, bajo una inundación UDP de 54000 paquetes/s el pedido ADDBA llegó al par
(`ADDBA request from awdl0: token 2 ssn 1068 -> accepted`). La sesión llevó 986 frames y la vigilancia la
cerró: `Block Ack session ... carried data and nothing came back: closed, single frames for 60 s`.

## 9. Radio compartida con un BSS (2026-11-11)

`en2` asociado en el 153 a 80 MHz, par virtual a -40 dBm, `rtlawdl_mcc=1`. El 149 está en el mismo bloque de
80 MHz que el BSS, así que la radio conserva el ancho y solo mueve el primario. Carga: la de §7.

### 9.1 Reparto de slots durante una transferencia

Fuera de una transferencia la capa atiende el primer slot de cada tramo del canal social en la secuencia de
la familia: 2 slots de 16. La presencia densa no se activaba con la radio compartida.

Ahora la presencia densa también se activa con tiempo compartido. Mientras dura, la capa atiende los slots
que la secuencia da al canal y los libres, en tramos de `rtlawdl_mccrun` slots como mucho (3 por defecto, 0
deja el comportamiento anterior), y deja al BSS el slot que sigue a cada tramo. Los slots que la secuencia da
a otro canal no se toman (`Source/AirLayer/AWDLShare.h`, prueba en `Tools/share_test.cpp`).

| Secuencia de la familia | Slots atendidos |
|---|---|
| En reposo: `153 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0` | 12 de 16 |
| Con datos: `149×6 153 153 6 149×5 153 153` | 9 de 16, en cuatro visitas |

### 9.2 Medido

Capa `2FF5140E`; driver `70AB93CF` en las tres primeras filas y `CEBC43AE`, con el arreglo de §9.3, en la
cuarta. Ping al router por `en2` cada 0,2 s durante la transferencia.

| Montaje | TCP hacia el par | Ping al router |
|---|---|---|
| Capa anterior `5EC1F0A9`, 2 slots | 785 kB/s | sin pérdida, media 28 ms, máximo 421 ms (con el par a -72 dBm) |
| Reparto nuevo, 9 slots | 1499 kB/s | sin pérdida, media 81 ms, máximo 218 ms |
| Reparto nuevo y el bit de §9.3 borrado a mano | 15,3 MB/s en 14 s (15,7 a 16,5 MB/s pasados los 2 primeros) | sin pérdida, media 77 ms, máximo 206 ms |
| Reparto nuevo y driver `CEBC43AE` | 15,5 MB/s en 14 s (15,8 a 16,6 MB/s pasados los 2 primeros) | sin pérdida, media 68 ms, máximo 206 ms |

Sin transferencia, con la capa nueva: ping sin pérdida, media 6 ms, máximo 53 ms.

Con el driver `CEBC43AE` el BSS estaba en el 157, en el mismo bloque de 80 MHz. La numeración de la sesión
quedó sin huecos: `from awdl0: sessions 1 qos 159758 gaps 0`.

El BSS con tráfico propio, driver `CEBC43AE`: una descarga HTTPS de 90 MB por `en2`.

| Montaje | Descarga por `en2` | TCP hacia el par |
|---|---|---|
| Descarga sola | 26,8 MB/s | |
| Descarga durante la transferencia | 9,1 MB/s | 15,1 MB/s en 16 s |

### 9.3 Defecto: el MAC ID de gestión dormido fuera del canal del BSS

El reparto nuevo daba 4,5 veces más slots y la velocidad solo se duplicó. Lo que se vio:

- Con la sesión abierta, una inundación UDP sacó 1152 frames QoS por segundo, el mismo número segundo tras
  segundo (y 864 en alguno): múltiplos de 288. La capa anterior daba unos 575 por segundo, deducido
  de sus 785 kB/s. Son unos 300 frames por visita al canal social, dure la visita uno o tres slots.
- `REG_MACID_SLEEP` (0x4D4) valía 3 en 45 de 60 lecturas tomadas durante la inundación: los MAC ID 0 y 1
  dormidos. En 41 de ellas la cola de baja prioridad no tenía páginas libres y a la cola pública le quedaban
  0x16 de 0x776. Con el registro en 0 las páginas estaban libres.

Causa: `rtl8814native_radio_awdl_offch` duerme todos los MAC ID en uso al salir del canal del BSS
(`rtw_hal_macid_sleep_all_used`). Los frames de una sesión llevan el MAC ID de gestión (1) en una cola AC
(§3.3). Lectura de las cifras: el chip los retenía hasta la vuelta al canal del BSS, y en cada visita
entraban los que caben en el FIFO de transmisión y en los buffers del driver. Los frames de sincronización y
los datos sueltos van por la cola de gestión y salían en su slot con el mismo MAC ID dormido, y por eso no
se notó antes. Con la radio aparcada no hay MAC ID dormido.

Prueba en caliente, sin recompilar: un bucle en espacio de usuario que lee 0x4D4 y, cuando vale 3, escribe 1
(`Tools/test_uc peek` y `poke`), unos milisegundos después de cada salida. Con el bucle andando, la misma
transferencia dio la tercera fila de §9.2. En una corrida el bucle terminó antes que la transferencia: la
velocidad pasó de 15,8 MB/s a 1489 kB/s dentro de la misma conexión.

Arreglo (`Source/Driver/AWDLTx.c`): después de dormir los MAC ID en uso se despierta el de gestión. Los datos
de la estación llevan el MAC ID del AP y siguen dormidos. Driver `CEBC43AE`, cargado y medido (§9.2).

Los 785 kB/s de §7 y la prueba de §8.2 se hicieron con este defecto. Los CTS-to-self de §8.2 salen
duplicados en los cuatro canales del bloque, así que esa prueba no dice en qué momento salió cada agregado.

### 9.4 Sin comprobar

| Qué | Cómo |
|---|---|
| Un par real con su propia secuencia | iPhone: sus slots pueden no coincidir con los tramos atendidos |
| `rtlawdl_mccrun` distinto de 3 | Repetir §9.2 con 2 y con 5 |

## 10. Búsqueda de redes sin asociación (2026-11-11)

Cargado: driver `02AC5E67`, capa `F877A0D8`. Medido con el par virtual (§10.3). Lo que falta está en §10.4.
Cargado después: driver `06F390BF`, que solo vuelve al canal de AWDL si además hubo datos (§10.5).

### 10.1 El corte

Con `en2` desasociado el sistema busca redes cada 13 s y AWDL calla unos 4 s cada vez (§7). El escaneo del
driver se queda 100 ms en cada canal (`SURVEY_TO`) y solo vuelve a su canal entre tandas cuando hay una
asociación: cada 3 canales, 400 ms en el canal del BSS (`RTW_SCAN_NUM_OF_CH`, `RTW_BACK_OP_CH_MS`). Sin
asociación recorre toda la lista de una vez. La capa deja de atender las ventanas mientras el estado es de
escaneo.

### 10.2 El cambio

| Dónde | Qué |
|---|---|
| `rtw_scan_backop_decision` | Sin asociación, el escaneo vuelve también entre tandas si la radio está aparcada en un canal social, el puerto de AWDL está activo y llegó un frame de AWDL en los últimos 10 s. Se decide una vez por escaneo, al encolarlo, y el plazo del escaneo cuenta las vueltas |
| `SCAN_BACKING_OP` | Sin asociación la vuelta es al canal aparcado, el mismo al que vuelve el fin del escaneo, con los MAC ID despiertos |
| `rtl8814native_radio_tx_80211` | Durante un escaneo sin asociación, fuera de esas vueltas, los frames de AWDL se rechazan con ocupado: los datos esperan en el anillo de la capa y no salen por un canal ajeno |
| `rtl_radio_status` | `scanning` vale `RTL_RADIO_SCAN_BACKOP` también en esas vueltas, con `idle_channel` puesto |
| `RTLAwdInterface::txTick` | En esas vueltas la capa atiende las ventanas como con la radio aparcada |

`Tools/test_uc awdlscan <segundos>` cambia los 10 s; con 0 el escaneo vuelve a ser como antes.

El escaneo dura más cuando hay un par cerca: 400 ms más por cada 3 canales. Sin pares cerca no cambia.

### 10.3 Medición (2026-11-11, par virtual)

`en2` desasociado, radio aparcada en el 149 a 20 MHz, carga TCP de `awdl0` al par virtual con la recepción
muestreada cada 50 ms. Hueco: tiempo seguido sin recibir nada, de 150 ms o más, pasados los 2 primeros
segundos. La búsqueda completa se pide por CoreWLAN (`scanForNetworks`), como el menú Wi-Fi; el driver la
hace en dos escaneos seguidos. `awdlscan` se cambió en caliente, sin reiniciar.

| | `awdlscan 0` (como antes) | `awdlscan 10` |
|---|---|---|
| Duración de la búsqueda completa | 3,5 a 3,8 s (1,2 s + 2,2 s en el driver) | 7,8 s (2,5 s + 4,8 s) |
| Hueco en cada búsqueda | uno de 3,9 a 4,0 s | varios de 0,25 a 0,46 s (mediana 0,36 a 0,41 s); uno de 0,6 s en 4 búsquedas |
| TCP medio en 60 s | 6,2 MB/s, una búsqueda cada 14 s | 6,0 MB/s, una búsqueda cada 18 s |
| Redes en la lista, sin carga, 4 búsquedas alternadas | 10, 10, 10, 10 | 10, 10, 10, 9 |
| Redes en la lista, con carga, primera tanda | 10, 10 | 8, 8, 8, 9 |
| Redes en la lista, con carga, 5 búsquedas alternadas | 9, 10, 9, 8, 8 | 10, 9, 9, 8, 8 |
| Búsqueda dirigida de `networksetup -setairportnetwork` | encuentra la red en 4 s | encuentra la red en 9 s |

El caudal medio casi no cambia: la radio pasa fuera del canal de AWDL el mismo tiempo, repartido en tramos
de 300 ms. Lo que cambia es que ya no hay 4 s seguidos de silencio.

La lista de redes cambia sola con el tiempo, igual en los dos modos (de 10 a 8 redes en 2 minutos en la
tanda alternada): el cambio no la altera. `networksetup` sin contraseña encuentra la red en los dos modos
y falla igual al unirse (-3900); la asociación con las claves guardadas solo sale del menú Wi-Fi.

Asociación desde el menú Wi-Fi con el par virtual en marcha: la red se une (WPA3 de transición, canal 157 a
80 MHz) en 1,6 s desde que `airportd` recibe el pedido; después, ping normal por `en2` y 15,7 MB/s de TCP
por AWDL con la radio compartida. El pedido sale cuando termina la búsqueda que abrió el menú, que con el
par cerca dura 7,8 s en vez de 3,5 s: desde el clic hasta que empieza la asociación pasaron unos 7 s. Ese
es el costo visible del cambio. El driver de §10.5 lo acota.

Sin nadie que pida una búsqueda completa, lo que el sistema hizo solo con `en2` desasociado fue una búsqueda
de `locationd` cada 60 s en 3 canales: 0,35 s en el driver, un hueco de 0,41 s, 7,2 MB/s en 180 s. Esa
búsqueda es una sola tanda y no tiene vuelta intermedia. La cadencia de 13 s de §7 no apareció en esta
sesión.

### 10.4 Sin comprobar

| Qué | Cómo |
|---|---|
| Que sin pares la búsqueda dure lo de antes | No medido. Con el par en reposo la vuelta no se arma y dura 3,4 s (§10.5) |
| Un par real | Con un iPhone falta ver que sus frames lleguen en las vueltas y que la transferencia siga |

### 10.5 Solo con datos

Driver `06F390BF`, cargado. Un par que solo mantiene sus frames de sincronía ya no alarga la búsqueda: la
vuelta al canal de AWDL exige, además de un frame de AWDL en los últimos 10 s, datos de AWDL en ese mismo
plazo. Cuenta como dato un frame de datos recibido de un par o uno unicast enviado, o en espera, hacia un
par. Los datos multicast propios no cuentan. `awdlscan` fija los dos plazos a la vez.

Medido el 2026-11-11 con el par virtual, `en2` desasociado y la radio aparcada en el 149. La primera tanda
se hizo con la radio sin recepción (§11): valen sus duraciones, no las redes encontradas. La segunda, con
el driver `CFE7BB9D` y la radio sana.

| Situación | Radio sin recepción | Radio sana |
|---|---|---|
| Par en reposo | 3,5 a 3,8 s | 3,4 s en el driver, 10 búsquedas, ninguna vuelta |
| Con carga TCP | 7,8 s (6,1 s la que empezó con la carga); huecos de hasta 0,51 s | 7,8 s, 2 búsquedas; huecos de 0,15 a 0,51 s, mediana 0,41 s; TCP medio 5,2 MB/s en 60 s |
| 13 a 14 s después de la carga | 3,5 s | 3,5 s, 2 búsquedas, ninguna vuelta |
| Redes en la lista | no vale | 10 a 12 en reposo, 8 a 9 con carga |

Con carga la lista trajo menos redes en esta tanda: faltaron las de los canales 3 y 11. En §10.3 esa
diferencia no aparecía. Dos búsquedas no alcanzan para decidirlo.

Recién desasociado, el sistema buscó solo cada 13 s (la cadencia de §7) durante unos 2 minutos y medio;
cada búsqueda duró 3,4 s en el driver.

Asociación desde el menú Wi-Fi con el par en reposo: la búsqueda que abrió el menú duró 0,28 s, el pedido
de asociación salió con el clic, 1,5 s después de abrirlo, y la red quedó unida 1,5 s más tarde. No hubo
la espera de 7 s de §10.3.

Sin ningún par no se midió. Con el par en reposo el driver sigue el mismo camino: la vuelta no se arma.

## 11. La radio dejó de recibir tras una asociación (2026-11-11)

Visto una vez, con driver `06F390BF` y capa `F877A0D8`. Causa no demostrada. Hay una protección cargada
desde el arranque de las 16:26 del mismo día: driver `CFE7BB9D`.

### 11.1 Lo visto

| Hora | Hecho |
|---|---|
| 15:22:53 | Asociación automática al arrancar: BSS en el 157 a 80 MHz, sin pares de AWDL |
| después | La capa lleva la radio a los slots del 149 y la devuelve (`radio to channel 149`, `radio to channel 0`) |
| 15:22:57 | Último escaneo con resultados (5 redes) |
| 15:23:31 | El driver da el enlace por caído (`link_off`), sin pedido de desconexión |
| desde 15:23:34 | Todos los escaneos devuelven 0 redes; `rx_frames` del driver quieto en 543 |

En ese estado:

| Qué | Valor |
|---|---|
| Contadores de recepción del MAC (`0x664`, por tipo) | Quietos: ni tramas buenas ni con error |
| Contador de CCA de la banda base (`0xF08`) | Sigue contando, unas 230 por segundo; falsas alarmas (`0xF48`) unas 70 por segundo, contra 1 con la radio sana |
| Habilitaciones (`0x100`, RCR, `0x522`, RF `0x00`) | Normales: recepción habilitada, transmisión sin pausa, RF en modo recepción |
| FIFO de recepción (`0x284`) | Vacía y en reposo |
| Transmisión | Las escrituras por USB siguen saliendo; no se comprobó en el aire |
| Apagar y encender el Wi-Fi, cambiar el ancho aparcado | No lo recuperan |
| Reinicio | Recupera; con el mismo driver la radio siguió bien después |

El par virtual siguió funcionando porque no usa el aire. Desde el 2026-11-10 no hay en el registro de
`airportd` otro escaneo con 0 resultados antes de las 15:23.

De 218 registros del MAC, la banda base y el RF leídos en el estado sin recepción y con la radio sana, los
38 que difieren son los que cambian con el canal, el ancho y la asociación. Los de la compensación de I/Q
de recepción (`0xC10`, `0xE10`, `0x1810`, `0x1A10`) no se leyeron en ese estado. Con la radio sana, sin
asociación y en el 149 a 20 MHz, los cuatro valen `0x00000100`.

### 11.2 Lo leído en el código

| Dónde | Qué |
|---|---|
| `odm_txpowertracking_callback_thermal_meter` | En el watchdog (hilo de comandos, cada 2 s), si la temperatura del chip se movió 8 unidades o más, recalibra I/Q (`do_iqk_8814a`). En el 8814A no hace LCK |
| La misma función | Espera a que no haya un escaneo (`is_scan_in_process`): la calibración no convive con cambios de canal |
| `rtl8814native_radio_awdl_offch` | El salto a un slot de AWDL cambia el canal desde el hilo de la capa, fuera del hilo de comandos, con `offch_mutex` y `setch_mutex`. El watchdog no toma ninguno de los dos |
| `rtl8814native_radio_set_channel` | El canal aparcado sin asociación va por la cola de comandos: no se cruza con el watchdog |

Nada impide entonces que una calibración por temperatura corra mientras la capa salta de canal. Que eso
fuera lo ocurrido a las 15:23 no está demostrado: no quedó registro de la calibración.

### 11.3 El cambio (cargado)

La calibración por temperatura toma `offch_mutex` mientras dura, y si la radio está fuera en un slot de AWDL
no corre: el valor térmico de referencia no se actualiza y el watchdog siguiente lo intenta otra vez.
`Tools/test_uc awdlcal` muestra cuántas calibraciones corrieron y cuántas se aplazaron.

Con el driver cargado, a los 3 minutos del arranque (red asociada en el 157, par virtual recién arrancado) el
contador marcaba 1 calibración corrida y 0 aplazadas. A los 10 minutos, después de una transferencia de
60 s sin asociación, seguía igual. La calibración por temperatura corre entonces en los primeros minutos
tras un arranque, con la red asociada. El fallo de §11.1 ocurrió en el primer minuto tras un
arranque: es compatible con la sospecha y no la demuestra.

Prueba larga el mismo día: 62 minutos con la red asociada, los primeros 16 con carga TCP por AWDL al par
virtual (tres tandas de 5 minutos a 15,4, 15,7 y 15,8 MB/s; huecos de hasta 1,0 s, de 3,4 a 9,5 s sumados
por tanda). El enlace no cayó, la recepción siguió y el contador quedó en 1 y 0. El medidor térmico (RF
`0x42`, bits 15:10) pasó de 43 a 44 con la carga, con una lectura de 45: lejos de las 8 unidades que
disparan la calibración. Con el chip ya caliente la carga no la provoca; queda limitada al calentamiento
tras el arranque, y la protección solo puede verse actuar ahí.

### 11.4 Sin comprobar

| Qué | Cómo |
|---|---|
| Una calibración aplazada por un slot de AWDL | No se vio: 0 aplazadas en 80 minutos. Solo puede darse en los primeros minutos tras un arranque |
| Que la radio no vuelva a quedarse sin recepción | Uso normal con el driver nuevo; el fallo se vio una sola vez |
| Los registros de I/Q si se repite | `regdump.sh` ampliado con `0xC10`, `0xE10`, `0x1810`, `0x1A10` |
