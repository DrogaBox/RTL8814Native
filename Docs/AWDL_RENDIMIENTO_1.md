# AWDL: Block Ack de recepción, presencia densa y escalera de tasas

Cambios de 2026-11-09 para la velocidad de AirDrop en los dos sentidos, tomados del diseño de
`github.com/Dino-VN/MtkWireless` 1.0.3 (MT7921, AWDL por software, unos 4 MB/s iPhone → Mac) y adaptados al
RTL8814AU. Probados en unidades fuera del kernel y compilados; falta el aire con un iPhone. Lo no visto va con `?`.

| Pieza | Compilación |
|---|---|
| `RTL8814Native.kext` | UUID `9B662F1B`, md5 `d06f7b4a` |
| `RTL8814AirLayer.kext` | UUID `8FB89DDD`, md5 `cfd4f92e` |

Las dos van juntas: la capa nueva con el driver anterior sigue funcionando, pero sin escalera de tasas (12 Mb/s fijos).

## 1. Punto de partida

- **RX.** El iPhone pide un acuerdo Block Ack (ADDBA, TID 0, ventana 64, A-MSDU permitido) unas 4 veces por segundo
  durante una ráfaga ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §15). La familia nunca lo contesta
  ([AWDL_BLOCK_ACK.md](AWDL_BLOCK_ACK.md) §1), y la capa lo descartaba como `other`. Sin acuerdo el iPhone manda un MPDU
  por acceso al medio: 64 MB a unos 800 KB/s el 2026-11-08.
- **TX.** Los datos AWDL salían a 12 Mb/s fijos, por la cola de gestión, sin ningún dato de vuelta salvo el fin de la
  transferencia USB. El techo teórico es de alrededor de 1,2 MB/s antes de cabeceras y ventanas.
- **Ventanas.** En reposo awdld deja el canal de datos en 2 de 16 slots, y un par solo transmite en los slots que el
  otro anuncia. MtkWireless midió que awdld no cambia esa secuencia durante AirDrop.

## 2. Block Ack de recepción (capa)

**El MAC manda el BlockAck por hardware `?`.** El driver del fabricante no programa ninguna sesión de recepción en
registros, ni siquiera para su propio AP: `add_ba_rsp_hdl` solo envía la respuesta ADDBA como trama de gestión y
habilita su reordenamiento por software (`core/rtw_mlme_ext.c`, `core/rtw_wlan_util.c:3548-3553`), y las únicas
escrituras `HW_VAR_AMPDU_*` son de TX. Aun así, en infraestructura acepta ADDBA del AP (`bAcceptAddbaReq`,
`Source/Driver/VendorProbe.c:328`) y recibe A-MPDU. Un emisor que no recibe BlockAck tras un A-MPDU reintenta cada
subtrama, así que lo más probable es que el MAC lo genere solo. No está comprobado para la dirección del puerto 1 en modo
ad hoc, que es la de `awdl0`; el ACK por trama en esa dirección sí está medido.
Más indicios en el mismo sentido. `REG_RXFLTMAP1` queda solo con el bit 10 (PS-Poll), así que el MAC se queda con
los BlockAck y los BAR y nunca los pasa al host (`hal/rtl8814a/usb/usb_halinit.c:545-552`). `RCR_VHT_DACK` elige
por trama entre ACK y BA como respuesta (`include/hal_com_reg.h:1115`). En Linux, rtw88 (que ya incluye el 8814A)
y rtl8xxxu no tocan el hardware en `IEEE80211_AMPDU_RX_START`: solo devuelven 0. Los registros de la tabla de Block
Ack (`REG_BACAMCMD` 0x654, `REG_BACAMCONTENT` 0x658) no los escribe ningún driver y no tienen campos documentados, así
que no se escribe nada a ciegas. La duda que queda es si el MAC asocia la sesión a un par sin entrada de MACID, que es
el caso de AWDL. La prueba es una captura del A53 en el canal: un BlockAck (subtipo 9) del Mac al iPhone, un SIFS
después de cada ráfaga A-MPDU.

Al host le queda (`Source/AirLayer/AWDLInterface.cpp`, `handleBlockAck`, `rxReorder`):

1. **Respuesta ADDBA.** Estado 0, el mismo token, los bits A-MSDU, política inmediata y TID de la petición, ventana
   de 64 como máximo y el mismo timeout. Va al frente de la cola de TX para la siguiente ventana del par
   (`queueMgmt`): detrás de los datos podía caducar a los 1,2 s. Se rechaza con 37 un Block Ack diferido, un TID mayor
   que 7, un tamaño de buffer 0 o un puerto 1 apagado.
2. **ADDBA repetido.** Si la sesión del mismo par y TID existe y el número inicial cae dentro de la ventana, la sesión
   sigue y solo la ventana toma el tamaño de la nueva petición. Reiniciarla subiría lo retenido antes de la trama que
   el par está por reenviar.
3. **DELBA.** El del par como iniciador (bit 11) cierra su sesión y sube lo retenido en orden. Cuatro DELBA en 10 s
   pasan la respuesta a 37 durante 60 s: si el MAC no mandara el BlockAck, el iPhone cortaría el acuerdo una y otra vez
   y así se vuelve al MPDU suelto de antes.
4. **Ventana de reordenamiento.** Por (par, TID), con número de secuencia de 12 bits y hasta 64 tramas, 8 sesiones.
   Una trama en orden sube enseguida junto con las siguientes que esperaban. Una adelantada espera, una más allá de la
   ventana la desplaza, y una por detrás se descarta. Un hueco de más de 100 ms se salta: lo revisa el hilo de RX con
   cada trama de acción, o `txTick` lo despierta si el par se calla. Las tramas sin QoS, las de grupo y las de una sesión
   inexistente suben directo, como antes. Una sesión no se cierra por inactividad: el par conserva el acuerdo y
   seguiría mandando A-MPDU, que subirían sin ordenar. Termina con el DELBA del par, con un ADDBA nuevo o cuando la
   menos usada de las ocho deja su lugar.

El boot-arg `rtlawdl_ba=0` rechaza todas las peticiones, como antes. Contadores en el log de la capa (`RTLAwdlLog` en
ioreg): `ADDBA from … -> status`, `DELBA from …` y cada 256 tramas de acción `ba req … accept … decline … delba …`.

## 3. Presencia densa (capa)

Una muestra de 250 ms con 10 o más tramas de datos, recibidas más enviadas, cuenta como ocupada. Con dos muestras
ocupadas entre las cuatro últimas (1 s) la capa anuncia en el TLV 18 y atiende también los slots que la secuencia 129 de
la familia deja en 0; ya encendida, cada muestra ocupada la mantiene, y se apaga 2 s después de la última
(`updateDense`, `servedMask`). La regla de las dos muestras es de la capa `AA2AA873`: con una sola, las ráfagas de unas
16 tramas de los reintentos TLS de `rapportd` la encendían y apagaban cada pocos segundos (§5). En `AA2AA873` las dos
muestras tenían que ser seguidas, y eso casi nunca pasa con la presencia densa apagada: los datos solo salen en los slots
2 y 10, separados 524 ms, en ráfagas cortas al principio de cada slot, así que las muestras ocupadas caen una sí y otra
no (§5). La ventana de cuatro muestras es de la capa `ED053DB1`. Los slots que la familia pone en otro canal siguen sin atenderse. No se activa
mientras la radio comparte tiempo con un BSS en un canal no social (`_mccActive`).

En esos slots extra la ventana es de la capa: se envían datos y MIF, pero no se publican los eventos 41 y 43
(`_awPosted`), porque la secuencia de la familia dice 0 ahí y el evento 41 lleva flags que el gestor compara con los
suyos. El boot-arg `rtlawdl_dense=0` lo desactiva. El log marca `dense presence on/off`.

## 4. Escalera de tasas de TX (driver y capa)

**Reporte por trama.** El 8814 devuelve por cada trama con `SPE_RPT` un reporte CCX por C2H: fin por reintentos o por
vida, número de reintentos y tasa final (`include/rtl8814a_cmd.h:118-125`). El despacho C2H ya está compilado
(`Source/Compat/VendorOsDep.c:585-598`), pero `C2HTxFeedbackHandler_8814` solo hacía algo con `CONFIG_XMIT_ACK`, que
está apagado. Ahora cuenta cada reporte (`rtl8814native_awdl_tx_report`, `Source/Driver/AWDLTx.c`).

**ABI.** Dos funciones nuevas en `Source/Driver/RTLRadioABI.h`, sin cambiar `RTL_RADIO_ABI`:
`rtlRadioAwdlRate` fija la tasa de los datos unicast AWDL como índice de descriptor (0x04–0x0b OFDM 6–54 Mb/s,
0x0c–0x13 MCS 0–7; 0 vuelve a 12 Mb/s fijos), y `rtlRadioAwdlTxStat` devuelve los contadores: limpias, con un
reintento, con dos o más, perdidas, tasa final de la última y total. Un driver anterior contesta
`kIOReturnUnsupported` y la capa se queda en 12 Mb/s.

**Descriptor.** Las tramas con tasa fijada llevan `SPE_RPT` y `DATA_RATE_FB_LIMIT` en OFDM 6 Mb/s. La caída de tasa
del hardware en los reintentos queda activa (`DISABLE_FB` en 0), así que una trama que falla en un escalón alto puede
bajar a una tasa menor en sus reintentos `?`: la forma exacta de la caída depende de la tabla `DARFRC` del MAC
(`0x430`–`0x437`) y se ve en la tasa final de los reportes. `update_mgntframe_attrib` ahora pone a 0 `data_tries` y el
campo nuevo, porque las tramas del pool se reutilizan sin limpiar. Antes, en modo simulado, una trama de gestión de la
infraestructura podía heredar el límite de un reintento.

**Escalera** (`updateRate`, desde `txTick`). Escalones OFDM 6, 12, 24, 36, 48, 54 Mb/s y, con
`rtlawdl_ratemax=6`, MCS 7 (65 Mb/s); el tope por defecto es 54 Mb/s. Arranca en 12 Mb/s, la tasa de antes. Reglas:

- **Subir.** 32 tramas limpias en el escalón suben uno; el nuevo queda a prueba hasta juntar otras 32, y una vez
  confirmado necesita otras 32 antes de probar el siguiente.
- **Bajar.** Un fallo durante la prueba baja enseguida y espera 256 × 2^n tramas limpias antes de volver a probar
  (n crece hasta 4 con cada fallo y baja con cada escalón confirmado). En un escalón confirmado, dos fallos sin 128
  limpias entre medio bajan uno.
- **Fallo.** Cuenta como fallo una trama perdida, una con dos o más reintentos, o dos o más con un reintento en el mismo
  tick. Un solo reintento suele ser el par todavía en otro canal.
- **Piso.** Sin ninguna trama confirmada todavía (el par virtual nunca manda ACK) no baja de 12 Mb/s.
- **Contadores.** Se leen mientras salen tramas y 8 ticks después, porque los reportes llegan tarde. Si vuelven a
  cero (un `RTL8814Native` recargado con la capa viva) se toman como base nueva, sin decidir nada.

**Sin reportes: la tasa por la señal.** Medido el 2026-11-09 con la capa `1B702083` y el driver `9B662F1B`: en 984
tramas de datos unicast con `SPE_RPT` el contador de reportes quedó en 0, y el driver recibió 32 paquetes C2H en total
desde el arranque. `REG_TX_RPT_CTRL` (0x4EC) vale 0x3E3, con los bits de habilitación puestos. El árbol del fabricante
trae `CONFIG_XMIT_ACK` apagado para este chip. El RTL8814AU no manda entonces el reporte CCX por trama, y la escalera
no tiene de qué alimentarse. Tras 64 tramas enviadas sin ningún reporte la capa pasa a elegir la tasa por la señal de
los pares (`rateByRssi`): el RSSI de sus tramas, suavizado sobre 8, con un mínimo por escalón de −70 dBm para 24 Mb/s,
−65 para 36, −60 para 48, −55 para 54 y −50 para MCS 7. Subir pide 3 dB más y como mucho un cambio por segundo; bajar
es inmediato; sin par oído en 5 s vuelve a 12 Mb/s. La caída de tasa del hardware en los reintentos cubre la trama para
la que el escalón era demasiado rápido. Si los reportes aparecen, vuelve la escalera. Los umbrales son una estimación
(unos 10 dB sobre la sensibilidad OFDM de cada tasa) `?` y se ajustan con la tasa final que muestre la captura.
`rtlawdl_rate=0` mantiene los 12 Mb/s fijos. El presupuesto de aire por ventana (`AWDL_TX_AIR_US`, 12 ms) ahora cuenta el
costo de cada trama a la tasa actual: antes lo calculaba siempre a 12 Mb/s. El log muestra `tx rate X -> Y Mb/s` con los
contadores.

## 5. Pruebas hechas

- **Fuera del kernel**, con las funciones copiadas tal cual del fuente y AddressSanitizer:
  - reordenamiento, 13 casos: en orden, huecos llenados, duplicados, timeout, el salto de 4095 a 0, desplazamiento de
    ventana, reinicio, tramas sin QoS, ventana llena al revés;
  - `handleBlockAck`, 13 casos: formato de la respuesta, ADDBA repetido con tramas retenidas, reinicio lejano, DELBA,
    rechazos, bloqueo por DELBA y su vencimiento;
  - escalera, 17 casos: subida hasta el tope, prueba fallida con espera, tolerancia a un reintento, primer fallo de
    un escalón confirmado como aviso y segundo como bajada, bajada a 6 Mb/s, piso sin ACK, contadores que vuelven a
    cero, reportes tardíos, driver sin soporte, y el modo por señal con su histéresis.
- **Par virtual.** `awdlsim --addba` pide el acuerdo como un iPhone, comprueba la respuesta y manda sus datos como QoS
  con una de cada ocho tramas intercambiada con la siguiente (`Tools/awdlsim/src/ba.rs`, con sus pruebas).
- **Par virtual con la capa cargada (2026-11-09, capa `6C8A1475`, driver `9B662F1B`).** La radio compartía tiempo con
  un BSS en el 153. El par pidió el acuerdo y recibió `status 0 window 64` en medio segundo; el log de la capa dio
  `ADDBA from … tid 0 win 64 ssn 256 amsdu 1 -> status 0`. La entrada de vecino apareció sola. Por el puente `utun`
  pasaron 445 de 450 ecos de 200 bytes, enviados como QoS con 49 pares intercambiados. El reordenamiento no llegó a
  ejercitarse: los datos empezaron más de 30 s después del acuerdo, y la capa de ese día cerraba una sesión inactiva
  a los 30 s, así que las tramas subieron directo (`total held 0`). De ahí sale la regla de 2.4 de no cerrar por
  inactividad. La presencia densa se prendió y se apagó una vez durante un escaneo del BSS; ahora no se activa
  mientras la radio pertenece a un BSS en otro canal. El log periódico de TX lleva una línea más con la tasa, los
  reportes del chip y el estado de la presencia densa.
- **Repetición con la capa `1B702083` (2026-11-09, 20:45).** Mismo montaje, con los ecos desde el primer segundo. El
  acuerdo se aceptó igual. De 600 ecos volvieron 592, y el par envió 67 pares intercambiados. La capa subió las 592
  tramas (`up 592 bad 0`), retuvo 105 hasta completar el orden (`total held 105`), saltó 7 huecos de más de 100 ms y
  no descartó ninguna por vieja ni duplicada (`old 0 dup 0`). Los 8 ecos perdidos y los 7 huecos serían tramas que el
  puerto de simulación rechazó con la radio fuera del canal (1662 rechazos en la corrida) `?`. Tras una pausa de más de 30 s, una segunda tanda de
  400 ecos (389 de vuelta) siguió entrando en la misma sesión: `total held` pasó de 105 a 174 con `old 0 dup 0`. El
  reordenamiento y la sesión persistente quedan comprobados en el kernel. El chip no mandó ningún reporte de TX
  (§4), así que la tasa quedó en 12 Mb/s en esa corrida.
- **Tasa por la señal con la capa `8FB89DDD` (2026-11-09, 21:20).** Mismo montaje. Tras 64 tramas de datos sin
  reportes la capa escribió `no TX report from the radio for 64 data frames: rate by the peers' signal` y enseguida
  `tx rate 12 -> 54 Mb/s by signal (peers at -40 dBm)`, el RSSI que declara el par virtual. Volvieron 490 de 500 ecos y
  el reordenamiento siguió bien (`up 490, total held 86, old 0 dup 0`). El par virtual oye la copia de cada trama antes
  de salir al aire, así que esta corrida prueba la lógica y que el driver acepta la tasa, no la recepción a 54 Mb/s
  por el aire.
- **AirDrop Mac → par virtual sin BSS (2026-11-09, 22:25, capa `8FB89DDD`).** Con `en2` desasociado (22:25:21) la
  radio quedó estacionada en el 149, sin reparto de tiempo (`mcc` no avanzó), y la presencia densa se activó por
  primera vez en hardware (`dense presence on`). El Mac subió 64 251 343 bytes en 42,07 s, 1527 kB/s, a ritmo parejo de
  principio a fin: 1 MB cada 0,65 s. Con la radio compartida con un BSS en el 157, el mismo montaje había dado
  220 kB/s ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10.4). La capa mandó a 54 Mb/s por la señal (`tx rate 54 Mb/s
  by signal, reports 0`), y en la subida descartó 77 tramas por cola llena y 48 por viejas sobre unas 48 000 enviadas (contadores `dropped
full` y `old` antes y después). No se separó cuánto aporta
  la presencia densa y cuánto la radio sin compartir: haría falta otra corrida con `rtlawdl_dense=0`. El par virtual no
  tiene radio, así que esto mide la planificación de TX del Mac y no la recepción por el aire.
- **La misma transferencia vista por el aire (2026-11-09, 22:30 a 22:34).** Con el A53 en monitor en el 149
  (`telefono/vigia_mac.sh 5745 240`) durante dos subidas más (un APK de 28 MB y un archivo al azar de 30 MB, 1537 y
  1545 kB/s): 91 209 tramas de datos del Mac al par, todas a OFDM 54 Mb/s (`Tools/ba_capture_report.py`), con 45 592
  reintentos, uno por trama. El reintento es del montaje: el par virtual no tiene radio y no da ACK, y con él el driver
  deja un solo reintento (`data_tries = 1` en `Source/Driver/AWDLTx.c`); con dos intentos el hardware no llega a bajar
  la tasa. La tasa que elige la capa sale tal cual al aire. Con un par real que confirma el primer intento, el aire por
  trama sería la mitad. 26 332 de las tramas salieron en el primer AW de su franja y el resto en el resto de la franja.
- **AirDrop par virtual → Mac con Block Ack (2026-11-09, 22:39, capa `8FB89DDD`).** `airdrop_send.py` mandó el
  mismo archivo al azar de 31 457 280 bytes desde el par (`awdlsim --addba`, acuerdo aceptado, una de cada ocho tramas
  intercambiada con la siguiente). `/Upload` terminó con 200 a los 2 s de aceptar, y el archivo de `~/Downloads` tiene
  el mismo SHA-256 que el original. La capa subió 23 911 tramas de datos (`up` de 37 254 a 61 165, `bad 0`), retuvo
  3033 para reordenarlas (`total held` de 4964 a 7997) y no saltó huecos ni descartó tramas viejas o duplicadas
  (`gaps skipped 24`, `old 0 dup 0`, sin cambios). Las tramas del par entran por el puerto de simulación, no por el
  aire, así que esos 2 s miden la ruta de recepción de la capa y la pila, no la radio. Para que `sharingd` abra el
  8770 hizo falta el anuncio BLE del A53 (`Tools/airdrop_beacon`, §6 de [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md)) y
  parar `airdrop_recv.py`, que ocupa el mismo puerto: con él en marcha `sharingd` fallaba con `NWError` 48
  (dirección en uso).
- **Presencia densa con ráfagas cortas.** Terminada la subida, la presencia densa se prendió y apagó cada pocos
  segundos (25 cambios en la corrida) con ráfagas de unas 8 tramas por sentido: los reintentos TLS de `rapportd` hacia
  el par (§10.5 de [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md)). Cada ráfaga pasa el umbral de 10 tramas en 250 ms y deja
  todos los slots libres anunciados durante 2 s. No afecta a una transferencia; cuántas ráfagas así provoca un par real no
  está medido `?`. A las 22:44, tras unos 20 minutos más con el par rotando su `at=` cada 30 s, los cambios iban por
  595.
- **Presencia densa con dos muestras seguidas (2026-11-09, 23:00, capa `AA2AA873`).** Con la regla de §3 cargada,
  `en2` fuera de la red y la radio estacionada en el 149, el Mac subió al par virtual 31 459 142 bytes en 20,58 s,
  1529 kB/s, igual que con la capa anterior. La presencia densa se prendió una vez al empezar la subida y se apagó una
  vez al terminar: 2 cambios en total. En el minuto siguiente `rapportd` abrió 7 conexiones TLS al par y no hubo más
  cambios. La cola de TX descartó 28 tramas por llena durante la subida (`dropped full 28`).
- **UDP sin presencia densa (2026-11-09, 23:04-23:07, capa `AA2AA873`).** UDP de 1300 bytes del Mac al par virtual por
  `awdl0` (sin TCP, sin tramas de vuelta), radio estacionada en el 149: a 500, 1000, 1500 y 2500 paquetes/s y sin
  límite llegaron al par 157, 184, 190, 200 y 200 tramas/s. La presencia densa no se encendió en ninguna de las cinco
  corridas de 8 s (`dense 0 (2 changes)`): con ella apagada solo se atienden los slots 2 y 10, cada ráfaga cabe en una
  muestra de 250 ms y la siguiente llega 524 ms después, nunca en la muestra contigua. Unas 100 tramas por slot
  atendido son el techo de dos slots por periodo. El resto se perdió en la cola de 64 (`dropped full` llegó a
  1 654 417). Arreglo: ventana de cuatro muestras (§3), capa `ED053DB1`, medido en el punto siguiente. La subida de
  AirDrop sí la encendió; que haya sido por los ACK del par virtual, que no se ajusta a los slots, no está comprobado,
  ni si con un iPhone se habría encendido `?`.
- **UDP con la ventana de cuatro muestras (2026-11-09, 23:14-23:15, capa `ED053DB1`).** Mismo barrido, mismo montaje:
  a 500, 1000, 1500 y 2500 paquetes/s y sin límite llegaron 445, 827, 1123, 1125 y 1052 tramas/s, contra 157 a 200 con
  `AA2AA873`. La presencia densa se prendió en cada corrida con menos de 200 tramas enviadas y se apagó al terminarla:
  10 cambios en 5 corridas. El techo de unas 1125 tramas/s (1,46 MB/s de carga útil) aparece desde 1500 paquetes/s; la causa
  más probable parecía el presupuesto de aire por AW; el punto siguiente lo descarta.
- **Presupuesto mayor (2026-11-09, 23:31-23:32, capa `207B418D`, `rtlawdl_air=15000 rtlawdl_lastaw=1`).** Mismo
  barrido: 450, 901, 1107, 1134 y 1139 tramas/s. Con 25 % más de aire por AW y datos también en el último AW del slot
  el techo no se movió, y la radio cortó 729 tandas con un rechazo transitorio (`busy 729` en unas 32 400 tramas). El
  límite con el par virtual es la cola de TX de la radio, no el presupuesto de la capa. El par virtual no devuelve ACK,
  así que cada trama sale dos veces por el aire (captura del A53 de las 22:30); con un par que confirma, el mismo aire
  llevaría el doble de tramas y el presupuesto volvería a contar. Cuánto sube con un iPhone no está medido `?`.
- **Cola de TX llena al recibir.** En algún momento entre las 22:30 y las 22:41 la cola de datos de TX descartó unas
  6900 tramas por llena (`dropped full` de 228 a 7122). Lo más probable es la subida par → Mac de 30 MB en 2 s, en la
  que el Mac devuelve miles de ACK de TCP y la cola de 64 tramas solo se vacía en las ventanas, pero el registro no
  tiene hora y no está comprobado `?`. El archivo llegó entero igual.
- **Revisión.** Una revisión del código contra 802.11-2020, el reordenamiento del fabricante (`check_indicate_seq`) y
  mac80211 dio los arreglos de los puntos 2.1, 2.2, 2.3, la expiración sin tramas y las 8 sesiones. Confirmó correctos
  el formato de los campos, los límites de `out[]`, la aritmética de 12 bits, la propiedad de cada mbuf y el orden de
  los locks. Una segunda revisión, del driver y la escalera, dio los arreglos de la confirmación del escalón, los
  contadores que vuelven a cero, los reportes tardíos y el largo del reporte C2H. Confirmó los campos del
  descriptor en la rama de gestión que toman las tramas AWDL, la conversión de `MRateToHwRate` y que ninguna otra
  ruta pone `SPE_RPT`. Queda abierto que un evento 41 no tenga su 43 si la TX se apaga con una ventana abierta, un
  defecto anterior a estos cambios.

## 6. Lo que muestra la captura del 2026-11-08

`Tools/ba_capture_report.py` lee una captura en modo monitor del A53 (radiotap, pcap o pcapng) y resume por sentido:
ADDBA, DELBA, BAR, BlockAck con su distancia a la trama de datos anterior, ráfagas A-MPDU según el campo de estado
A-MPDU del radiotap, tasas y ACK. Sobre la transferencia iPhone → Mac del 2026-11-08 (`vigia_20261108_211409`):

| | iPhone → Mac | Mac → iPhone |
|---|---|---|
| Tramas de datos | 1371, todas QoS TID 0, ninguna en ráfaga | 559, sin QoS |
| Tasa dominante | VHT MCS 1, 20 MHz (13 Mb/s), el 100 % | OFDM 12 Mb/s, con 4 a 9 y 3 a 6 Mb/s |
| Con bit de reintento | 702 | 43 |
| ADDBA | 9 pedidos, ninguna respuesta | — |

- **Caída de tasa del hardware.** Las tramas del Mac a 9 y 6 Mb/s, con la tasa fija en 12, son la caída de tasa del
  hardware en los reintentos (`DISABLE_FB` en 0). Es lo que usa la escalera de §4.
- **El primer intento del iPhone no se ve.** De las 1337 secuencias distintas del iPhone, 668 aparecen por primera vez
  ya con el bit de reintento: ni el A53 ni, al parecer, el Mac vieron el primer intento. Cuando el A53 sí ve una
  transmisión, el Mac casi siempre la confirma (solo 25 reintentos siguen a una transmisión sin ACK visible). El iPhone
  nunca usa más que MCS 1.
- **Hipótesis `?`.** El iPhone manda el primer intento a 40 u 80 MHz, que un receptor a 20 MHz no decodifica. La capa
  anuncia HT solo de 20 MHz, pero la capacidad VHT implica siempre 80 MHz, y awdl0 está a 20 MHz. Si es así, el iPhone
  pierde un intento por trama y su control de tasa se queda en MCS 1, y eso limita iPhone → Mac tanto como la falta de
  agregación. El A53 no ve ninguna trama de más de 20 MHz de nadie, así que la captura no lo confirma ni lo descarta.
- **Dos salidas.** Poner la radio de AWDL a 80 MHz alrededor del 149 cuando no está asociada, o dejar de anunciar VHT
  y quedarse con HT a 20 MHz (MCS 7, 65 Mb/s). La segunda cambia `getVHT_CAPABILITY` en `RTLAirLayer.cpp`, que es de
  otra sesión.
- **Prueba.** El driver tiene una opción en vivo para el ancho con que la radio libre se estaciona en 5 GHz, 20 MHz
  por defecto (`RTL_OPT_AWDL_BW`, `rtl8814native_awdl_set_park_bw` en `Source/Driver/AWDLTx.c`). Con
  `sudo Tools/test_uc awdlbw 80` el 149 pasa a primario de un bloque de 80 MHz con centro en 155, sin reiniciar. Las
  tramas del Mac siguen saliendo a 20 MHz en el primario, porque el descriptor de gestión nunca pone `DATA_BW`. En la
  visita se compara la tasa y el bit de reintento del iPhone a 20 y a 80 MHz, con el mismo script.
- **Comprobado en el Mac (2026-11-09, 22:41, driver `9B662F1B`, `en2` sin asociar).** `Tools/test_uc awdlbw 80` no
  pide root. Los registros pasan de 20 a 80 MHz: modo de la banda base `0x8AC` bits 1:0 de 0 a 2 (subcanal en los bits
  5:2), `0x483` a `0xA4`, y RF `0x18` de `0x53D95` (canal 149, bits 11:10 en 3, 20 MHz) a `0x5319B` (canal 155, el
  centro, bits 11:10 en 0, 80 MHz). AWDL siguió igual con el par virtual, y un AirDrop Mac → par de 31 459 097 bytes dio
  1539 kB/s, lo mismo que a 20 MHz, con 28 tramas descartadas por cola llena. `awdlbw 20` devuelve RF `0x18` a
  `0x53D95` y los bits 1:0 de `0x8AC` a 0. Queda sin probar lo único que importa del cambio: si un iPhone manda a 40 u
  80 MHz y el Mac lo recibe `?`.

## 7. Pendiente en el aire

Con el par virtual, tras instalar y reiniciar:

```bash
sudo Tools/awdlsim/target/release/awdlsim --mac 02:a5:3e:00:00:01 --metric 1 --seconds 120 --6e --addba --echo
```

Debe imprimir `ADDBA response from awdl0: status 0 window 64`, y el log de la capa `ADDBA from … -> status 0` con
`held` creciendo y `old`/`dup` en 0 mientras pasan los ecos.

Con el iPhone queda por ver:

1. **BlockAck del MAC.** Si los A-MPDU del iPhone reciben BlockAck de verdad (captura del A53 en el canal de la
   prueba) `?`. Si no, aparecen DELBA en serie y la capa vuelve sola a rechazar.
2. **Respuesta sin elementos.** Si el iPhone acepta la respuesta de 33 bytes sin el elemento HT Capabilities; la del
   driver del fabricante tampoco lo lleva `?`.
3. **Escalera en el aire.** Hasta dónde sube y qué tasa final muestran los reportes `?`.
4. **Velocidad** en los dos sentidos, contra los 800 KB/s del 2026-11-08.
