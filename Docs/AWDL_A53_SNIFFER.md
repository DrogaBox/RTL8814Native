# AWDL: el A53 como monitor y como par por el aire

Mediciones del 2026-11-06 con un Samsung A53 (SM-A536E, kernel 5.10.237, firmware S620, root en memoria) y el
T9UH con el driver `DB92193C`. Las secciones 2 a 5 son con la capa `0BBDEE68`; de la 6 a la 8, con la `EEEE65F6`. Es la primera comprobación de la TX del
Mac con un receptor ajeno al propio Mac. Lo que no se ha medido va con `?`.

Resultado en una frase: las tramas del Mac salen al aire en los canales y las franjas que anuncia, y su reloj es
coherente; con tiempo compartido salen pocas, con la capa `EEEE65F6` ya llevan las TLV 7 y 17, y el Mac sigue el reloj
de un par sin dejar de emitir MIF.

## 1. Montaje

El driver `scsc_wlan` del teléfono admite `NL80211_IFTYPE_MONITOR` y entrega radiotap. Esto corrige la idea
anterior de que el A53 no podía capturar. Las fuentes de los módulos no están en el repositorio.

| Paso | Orden | Nota |
|---|---|---|
| 1 | `svc wifi disable`, `ip link set wlan0 down` | `wlan0` sigue `UP` tras `svc wifi disable` por el escaneo continuo de Android |
| 2 | `nlmon newmon 0 mon0`, `ip link set mon0 up` | Herramienta propia de nl80211, sin libnl. El firmware acepta la vif (ifnum 3) incluso con el STA asociado |
| 3 | `insmod awdl6.ko ifname=mon0 freq=5745` | Para otro canal: `rmmod` e `insmod` |
| 4 | `tcpdump -i mon0 -y IEEE802_11_RADIO -w x.pcap -U` | `tcpdump` viene en `/system/bin` |
| 5 | `ip link set mon0 down`, `nlmon delif mon0`, `rmmod awdl6`, `ip link set wlan0 up`, `svc wifi enable` | Se asocia de nuevo en unos 10 s, sin reiniciar |

`NL80211_CMD_SET_CHANNEL` no sirve. Con otra interfaz activa, cfg80211 responde `EBUSY`. Con `wlan0` abajo,
`slsi_set_monitor_channel` envía la petición por la vif de `wlan0`, que está cerrada, y da `EINVAL` tras
`No cfm(0x2147)`. El módulo `awdl6` llama a `slsi_mlme_configure_monitor_mode` sobre la vif de `mon0`, y el firmware
responde éxito.

Sin canal fijado, `mon0` solo oye lo que sintoniza el STA o el escaneo: 53 tramas en 15 s entre 2412 y 5785 MHz.
Con el canal fijado oyó 1370 tramas en 19,4 s.

**Pérdida del monitor.** Los dos puntos de acceso cercanos con intervalo de 100 TU dan 190 de 190 y 187 de 190
beacons esperados. Una trama del Mac que no aparece en la captura muy probablemente no salió por ese canal.

**Límites.**

- No hay FCS en la captura, así que no se distinguen tramas dañadas.
- El monitor y la TX como par no conviven: la TX usa la vif del STA asociado.
- El root se pierde con un reinicio completo o un pánico. Cualquier módulo nuevo tiene ese coste.
- Un hilo de TX debe detenerse en el primer error y no enviar tramas de datos por la ruta de gestión. El árbol del kernel
  no reproduce la estructura `slsi_dev` del driver cargado (§8).

## 2. El A53 como par: Mac escuchando

Módulo `awdl5`: PSF de 293 bytes y MIF de 307 con TLV 7 de 28 bytes, métrica 300 y dirección `02:a5:3e:00:00:01`,
por `slsi_mlme_send_frame_mgmt` en el canal del STA (5765 MHz, canal 153). Los campos de tiempo son fijos, de modo
que no es un par coherente en el tiempo.

| Medida | Valor |
|---|---|
| Rondas en 60 s | 432 PSF y 432 MIF, firmware sin fallos |
| Capa | `rx af` llega a 512, RSSI -31 y -32, `clock follows 02:a5:3e:00:00:01` |
| `AWDLStateDump` | `peers:1` de 09:10:46 a 09:12:22, `peers:0` antes y después |
| `Posted Peer Absence` | 1, a las 09:11:20, unos 7 s tras la última trama |
| `cached` | 0 |

Es la primera vez que un par por el aire llega a `peers:1`. Antes solo había `Peer Absence`, con PSF sueltos sin
MIF. `cached:0` no indica un fallo: ese contador sube con una petición de caché del manager, que aquí no se
produjo `?`.

El registro de la capa muestra los PSF con `ch 153` y los MIF con `ch 149`, aunque el teléfono solo emitió en el
153. Es el canal en que estaba la radio, y el reparto por tipo de trama lo causan los campos de tiempo fijos (§7).

**Pánico anterior de `awdl5`.** `/proc/reset_summary` conserva la traza: `__cfi_check_fail [scsc_wlan]` desde
`peer_thread [awdl5]`. `__init` es `__nocfi` y el cuerpo de un kthread no lo es, de modo que la llamada indirecta
entre módulos dispara CFI. Se resuelve con `noinline __nocfi` en la función que envía. `pstore` está vacío en este
teléfono.

## 3. El Mac visto por el A53

Estado del Mac: `en2` asociada en el canal 153 a 80 MHz, tiempo compartido, `Low Power`, sin par.

| Canal | Duración | Tramas de `awdl0` | Tramas de `en2` |
|---|---|---|---|
| 149 (5745 MHz) | 19,4 s | 73 (37 PSF, 36 MIF) | 12 |
| 153 (5765 MHz) | 14,3 s | 0 | 94 |
| 6 (2437 MHz) | 14,2 s | 0 | 0 |

Las tramas de `en2` en el 153 demuestran que esa captura oía al Mac. Las de `en2` en el 149 son 12 reintentos de un
mismo Null con PM=1, una sola vez en 19 s. No se sabe si salió en el 149 o si es señal del 153 contiguo `?`.

- **Tasa y potencia.** Todas a 6 Mb/s, RSSI entre -41 y -37 con los dos equipos cerca.
- **Secuencia anunciada.** `[0 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0]`.
- **Franjas.** Los 36 MIF caen en el primer AW de las franjas 2 y 10, las del 149. De los 37 PSF, 36 salen en el
  último AW de las franjas 1 y 9, unas 5,4 TU antes que el MIF, por el cambio anticipado de canal. El otro cae en
  la franja 10.
- **Rejilla de AW.** `aw_seq` avanza 32 por cada 512 TU, es decir, 16 TU por AW.
- **`tx_counter`.** Es 64 en los MIF y 5 o 6 en casi todos los PSF. Coincide con el tiempo real hasta el siguiente
  límite de franja, con error de menos de 1 TU.
- **Marca de tiempo frente al reloj del teléfono.** Deriva de 35 ppm, residuo con desviación de 119 µs y máximo de
  788 µs en 73 tramas.

**Número de secuencia.** Avanza 4 por cada par PSF y MIF oído (2970, 2971, 2974, 2975...). El contador es del hardware y
lo comparten `en2` y `awdl0`. La captura del 153 da el resto: Null con PM=1 (3690) y, 26 ms después, Null con PM=0 (3693),
es decir, los dos avisos de salida y regreso del tiempo compartido alrededor del PSF y el MIF (3691 y 3692). No se pierde
ninguna trama.

## 4. Frente a iOS 26

Referencia: las capturas de owly en el canal 44, sin tiempo compartido. Las cifras del iPhone son de dos capturas
distintas, separadas por una barra.

| Campo | iPhone | Mac por el aire | Lectura |
|---|---|---|---|
| Cabecera de TLV 4: AW, AF, indicadores, extensión | `1000 6e00 0018 1000 1000` | Igual | Coincide |
| Mín. y máx. de extensión, presencia | `03030303`, `0400` | Igual | Coincide |
| `tx_counter` | Tiempo a la siguiente franja | Igual | Coincide |
| AW en que sale el MIF | Segundo o tercero de la franja | Primero; tercero con un par oído (§7) y en el 149 sin par (§10) | Válido según la marca; sin probar con un iPhone `?` |
| AW restante en el MIF | 0 | 16 | Coherente con salir al inicio del AW |
| `target_tx` frente a `phy_tx` | `phy_tx` unos 82 µs después | Iguales | owly también los iguala y funciona con iPhone |
| TLV 7 y 17 | Presentes | **Ausentes** | Corregido en la capa `EEEE65F6` (§6) |
| TLV 21 | `a002` | `a001` | Versión con clase de dispositivo distinta `?` |
| PSF por segundo | 8,7 / 7,6 | **1,96** | Consecuencia del tiempo compartido |
| MIF por segundo | 3,0 / 4,6 | **1,91** | Consecuencia del tiempo compartido |

## 5. Qué implica para la prueba con un iPhone

**Asociarse en un canal no social reduce mucho lo emitido.** La lectura de la radio del ensayo del 2026-11-05, en el
149 y sin tiempo compartido (`mcc 0`), dio 8,9 PSF/s y 2,9 MIF/s en 44,9 s, la cadencia del iPhone, confirmada por el aire en el §9. En el 153 el aire
da 1,96 y 1,91: una sola ventana de 149 cada 512 TU. La recomendación de desconectar `en2` en ese caso
([AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md) §3) tiene ahora una cifra detrás. La cadencia sin asociación
se midió después (§17).

**El A53 sirve para comprobar el despliegue sin nadie más.**

1. Comprobar las TLV 7 y 17 tras cargar la capa `EEEE65F6`: hecho (§6).
2. `sudo Tools/iphone_test.sh run` con el par activo: una trama real entra en la captura: hecho (§7).
3. `en2` desconectada: medir la cadencia sin asociación: hecho (§17).

Una captura de este tipo lleva el nombre del Mac en las TLV 2 y 16: no va al repositorio.

## 6. Con la capa `EEEE65F6`

Mismo estado de `en2` (153 a 80 MHz, tiempo compartido), captura de 30 s en 5745 MHz tras reiniciar.

| TLV | En el aire | Frente a lo esperado |
|---|---|---|
| 7 | `00 00 2c 00 1f ff 00 00` | Idéntica a [AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) §10 |
| 17 | `bf 0c 00 00 80 01 fd ff 00 00 fd ff 00 00` | Idéntica |

- **Tramas.** 56 PSF de 326 bytes y 56 MIF de 986, las dos TLV en todas: 1,94 por segundo de cada una, sin cambios.
- **Orden de TLV.** PSF `4 5 18 6 24 12 7 17 21` y MIF `4 5 18 24 12 7 17 16 21 6 2...`, el mismo que el iPhone de
  owly.
- **Registro.** `check` da READY. `Invalid _htCapabilityLength` aparece solo hasta las 09:30:06, durante el arranque, y
  no después. `getVHtCapabilityIE Unable` no aparece en los últimos 8 minutos.
- **`en2`.** Ping al router sin pérdida, 1,8 ms de media.
- **Par A53, 40 s.** 313 rondas sin fallos, `rx af` llega a 512, 7 volcados con `peers:1`, `cached:0` y una
  `Peer Absence`, igual que con la capa anterior. Ping por `en2` tras la corrida: 2,0 ms.

Sigue sin probarse si un iPhone exige estas TLV, y no se ha repetido la entrega de AirDrop con el par virtual sobre esta
capa `?`.

## 7. Ensayo de la captura con el A53 de par

Corrida de `sudo Tools/iphone_test.sh run` con `en2` en el 153 (tiempo compartido) y dos pares sucesivos de 40 s: `awdl5`
con los campos de tiempo fijos del §2 y `awdl7`, el mismo envío con TLV 4 y las dos marcas calculadas en cada trama a
partir del reloj del teléfono.

**El kit recoge una trama real.** La consola marcó `HEARD 02:a5:3e:00:00:01 rssi -32 ch 153 action len 293` y
`IO80211 peers 1`, con `lost 0`. `frames.pcapng` guarda las tramas de ida y vuelta, y `report` da las TLV del par y del
Mac, el reparto por canal y las líneas de IO80211. Era lo único del kit que no se había visto.

| Medida | `awdl5`, reloj fijo | `awdl7`, reloj que avanza |
|---|---|---|
| Enviadas por el teléfono | 312 PSF y 312 MIF | 313 PSF y 313 MIF |
| Recibidas en el Mac | 307 PSF y 223 MIF (85 %) | 300 PSF y 307 MIF (97 %) |
| `ch` de las recibidas | PSF: 153. MIF: 149, salvo 1 | 541 en el 153 y 66 en el 149, igual en PSF y MIF |
| TX del Mac durante el par | 8,83 PSF/s y **0,03 MIF/s** | 2,82 PSF/s y **1,90 MIF/s** |
| Franjas de los MIF del Mac | 1 solo MIF | 2 y 10, tercer AW en 75 de 76 |
| Posición del Mac menos la del teléfono | No aplica | Mediana -0,5 TU, entre -0,9 y -0,1 (p10 y p90) |
| `AWDLStateDump` | `peers:1`, `cached:0` | `peers:1`, `cached:0` |

Sin par, en la misma corrida, el Mac emite unos 2 PSF/s y 2 MIF/s.

**Por qué se cortan los MIF con `awdl5`.** Las plantillas fijas dicen `aw 6`, siguiente franja en 18 TU, en el PSF y
`aw 0`, en 64 TU, en el MIF. `alignClock` acepta las dos, porque el par se anuncia como maestro, de modo que cada trama
recibida devuelve el reloj atrás y el Mac da vueltas entre las franjas 0 y 2. `txTick` emite un MIF solo si
`aw / AWDL_PM != _lastMifSlot`, y la franja 2 nunca cambia de número: sale un MIF y ninguno más. Con `awdl7` el contador
avanza y los MIF siguen a la misma cadencia que sin par. Un maestro con reloj coherente no produce este corte.

**Lectura de `ch`.** Es el canal en que está la radio, no el de la emisión. Con `awdl5`, cada MIF llega con el Mac en la
franja 2 (149) y cada PSF con el Mac todavía en el 153. Con `awdl7` el reparto ya no depende del tipo de
trama: el 11 % de lo recibido llega en el 149, que ocupa 2 de 16 franjas. El Mac oye en el 149 tramas emitidas en el 153
contiguo, con pérdida mayor: 71 % de los MIF de `awdl5` frente al 98 % de los PSF.

**Lo que no se debe leer aquí.**

- Los 8,83 PSF/s de `awdl5` no son una mejora de cadencia: el Mac queda atrapado en las franjas que sirven el 149.
- Con par oído, el MIF sale en el tercer AW (`dwell 4` en el registro), como el iPhone, y no en el primero del §3. En el
  149 sale en el tercero también sin par (§10), de modo que el par no explica por sí solo el cambio.
- La elección no refleja a quién se sigue. El Mac anuncia métrica 514 y se declara maestro, el A53 anuncia 300, y aun así
  la capa sigue al A53 porque es el primero que se presenta como maestro. En las capturas de owly el iPhone anuncia 525, que
  gana, y quien lo sigue es la pila Linux de owly, con métrica 60 (§10). Esos dos casos se midieron por el aire con tramas del
  A53 (§10). Falta ver a un iPhone seguidor, que ninguna captura muestra `?`.
- El A53 emite solo en el 153, no sigue la secuencia del Mac y no envía datos. `cached` sigue en 0.

## 8. Tramas dirigidas al Mac

Módulo `awdl8`, con el reloj de `awdl7`. A las 09:57:31 el punto de acceso pasó del 153 al 149 con el mismo BSSID: `en2` se
reasoció en 9 s y desde entonces no hay tiempo compartido (`mcc` no avanza). El canal del teléfono durante estas tandas no
se comprobó `?`. La captura ya había terminado, así que solo hay contadores: `rx af` se registra cada 256 tramas y da
un intervalo.

| Modo | Destino | Origen | Enviadas | Aumento de `rx af` |
|---|---|---|---|---|
| 1 | `awdl0` | `02:a5:3e:00:00:01`, ajena al teléfono | 1092 | 34 048, unas 31 veces cada trama |
| 3 | `awdl0` | La del teléfono | 470 | Entre 257 y 767 |
| 4 | Una dirección inexistente | La del teléfono | 470 | Menos de 256 |

- **El chip entrega lo dirigido a `awdl0`** y filtra lo dirigido a otra dirección.
- **El chip confirma esa dirección.** El ACK va a la dirección de origen, y el chip del teléfono solo lo toma para la suya.
  Con un origen ajeno el firmware repite cada trama hasta el límite; con el propio, las repeticiones desaparecen. El estado de
  transmisión del firmware lo confirma trama a trama (§11).

**Datos: prueba fallida, repetida de forma controlada en el §11.** El modo 2 enviaba un Neighbor Solicitation y un Echo de IPv6 como datos
de AWDL (control de trama `0x08`) por `slsi_mlme_send_frame_mgmt`, una pareja cada 8 rondas de 128 ms, la primera en la
ronda 4. El teléfono guardó el kmsg de ese arranque en `/data/log/dumpstate_lastkmsg_*_KP.log.gz`, que es un zip pese a la
extensión, y `/proc/reset_summary` da la pila del pánico:

| Desde el inicio | Hecho |
|---|---|
| Hasta 0,58 s | Cinco PSF y cinco MIF, sin error en el registro |
| Unos 0,64 s | Según el calendario del módulo, tocan las primeras tramas de datos |
| 0,649 s | `WLBT FW failure`: el firmware cae con error de nivel 8 y código `0x4` |
| 0,661 s | `No cfm(0x2110) for req(0x2010)`: el módulo esperaba la confirmación de un `MLME_SEND_FRAME_REQ` |
| 0,827 s | `wifi@1.0-service` cierra `wlan0` y empieza la recuperación |
| 9,55 s | Primer `0x800a` de 10 (`INVALID_VIRTUAL_INTERFACE_INDEX`): el firmware ya recargado no tiene esa vif |
| 12,39 s | `wlan0` se abre de nuevo |
| 12,69 s | `wifi@1.0-service` cierra `wlan0` otra vez, y `hip4_deinit` destruye los wakelocks |
| 12,70 s | `BUG_ON(!timer->function)` en `timer.c:973`, dentro del hilo `awdl8`: pánico del kernel y root perdido |

Son dos fallos distintos.

- **El firmware cayó al llegar las primeras tramas de datos.** Es una coincidencia de tiempos: ningún registro nombra la
  trama en curso. El §11 lo reproduce con una sola trama de datos. Queda sin leer en qué punto del firmware `?`.
- **El pánico del kernel lo causó el módulo.** Su hilo siguió enviando tras el primer error. `wakeup_source_remove` deja `timer.function = NULL`, y `scsc_wifi_transmit_frame` usó
  ese wakelock de TX ya destruido a través de `pm_wakeup_ws_event`. En ese instante `wifi@1.0-service` seguía dentro de
  `hip4_deinit`. El `0x800a` no fue la causa, solo el módulo enviando a una vif que ya no existía.

**Segundo pánico: el primer intento de arreglo.** Para cerrar esa carrera, `awdl7` pasó a tomar `start_stop_mutex`. Al
cargarlo, el kernel entró en pánico 54 µs después de arrancar el hilo, en el primer `mutex_lock` de `send_one`
(`osq_lock`, `UBSAN Trap: Out-of-bounds`), y se perdió el root otra vez. El módulo tomaba el mutex en `sdev+0x398` y el
`scsc_wlan.ko` real, extraído de `vendor_boot`, lo tiene en `sdev+0x3C8`. La diferencia de 0x30 bytes es un `work_struct`:
`dev.h` agrega `chipset_logging_work` solo si `SCSC_SEP_VERSION >= 12`, y el Makefile de los módulos no define esa macro. Por
eso `device_state` (`+0x284`), `sdev` y `vif_mutex` coinciden y los módulos anteriores funcionaban.

Estado actual de `awdl7` y `awdl8`: `send_one` vuelve a la versión anterior, sin `start_stop_mutex`, y el hilo se detiene en
el primer error, lo que habría evitado el primer pánico. El modo 2 se eliminó. Si Android cierra `wlan0` durante una corrida
sin error previo, la carrera sigue abierta, también en `awdl5`. Un offset de `slsi_dev` usado por un módulo se comprueba antes
contra el `scsc_wlan.ko` real.

`awdl7` en esta forma se cargó en 5745 MHz con `en2` en el 149: 312 PSF y 312 MIF en 40 s, 0 fallos, descarga limpia y root
intacto. El registro de la capa llegó a `rx af` 36608 desde 36352, el único salto visible por la granularidad de 256, y
`rx data` sigue en 0. Sin `run` en marcha no se leyeron `peers` ni el reloj en esta corrida `?`. `awdl8` volvió a
cargarse para el §11. El teléfono puede volver a 2,4 GHz al arrancar, donde el Mac no lo oye: el BSSID se comprueba antes de cargar.

En el Mac no llegó nada: `rx data 0`, 0 paquetes de entrada en `awdl0` y ninguna entrada nueva en `ndp`. `check` sigue en
READY y `en2` responde. Queda sin probar por el aire la recepción de datos de AWDL y la respuesta del Mac a un par `?`. La
primera trama de datos real será la del iPhone.

## 9. Cadencia en el 149, sin tiempo compartido

Monitor en 5745 MHz durante 29,65 s, sin par, con `en2` en el 149 a 80 MHz y `mcc` sin avanzar en el registro de la capa.

| Medida | Con tiempo compartido (§4) | En el 149 | iOS 26 (§4) |
|---|---|---|---|
| PSF por segundo | 1,96 | **8,80** | 8,7 / 7,6 |
| MIF por segundo | 1,91 | **2,83** | 3,0 / 4,6 |

- La cadencia del registro se confirma por el aire. La relación MIF/PSF es 84/261 = 0,32 en la captura y 329/1024 = 0,32 en
  la capa.
- Los PSF salen cada 112 TU de mediana (p10 96, p90 112). El máximo de 224 TU corresponde a un solo PSF no oído.
- La secuencia avanza +1 en 341 de 344 saltos y +2 en los otros 3. Desaparecen el +4 y los Null del §3, porque no hay
  ventanas que abrir.
- Las TLV 7 y 17 siguen presentes en PSF y MIF, en el mismo orden.

Esta cadencia depende del canal que elija el punto de acceso, que ya cambió una vez (§8). Si vuelve al 153, vuelve el
tiempo compartido. `Tools/iphone_test.sh check` muestra el canal.

## 10. Elección por el aire en el 149

Cuatro corridas del A53 como par, con `Tools/iphone_test.sh run` en marcha, `en2` en el 149 y sin tiempo compartido. El par
emite PSF y MIF cada 110 ms. Cambian la métrica que anuncia, el maestro que declara y el contador de la TLV 24. El Mac
anuncia métrica 512 en todas. En las capturas de owly, 525 es la métrica del iPhone, que siempre es maestro, y 60 es `METRIC_INIT` de la pila
Linux de owly, que lo sigue. Es de esperar, entonces, que el Mac sea seguidor de un iPhone, porque 512 pierde frente a 525. El
caso de la métrica 60 no imita a un iPhone.

| Medida | Métrica 300, se declara maestro | 525, contador fijo | 525, contador que avanza | 60, declara maestro al Mac |
|---|---|---|---|---|
| Duración y tramas del par | 40 s, 313 + 313 | 40 s, 313 + 313 | 60 s, 469 + 469 | 40 s, 313 + 313 |
| Tramas recibidas según `rx af` | 626 de 626 | 626 de 626 | 938 de 938 | 626 de 626 |
| Maestro que anuncia el Mac | El propio | El par durante 11 s, luego el propio | El par durante toda la corrida | El propio |
| Cambio de maestro tras la primera trama | No hay | 0,196 s | 0,399 s | No hay |
| PSF por segundo del Mac durante la corrida | 8,89 | 8,87 | 8,88 | 8,87 |
| MIF por segundo del Mac durante la corrida | 2,85 | 2,87 | 2,85 | 2,85 |
| Mac menos par, mediana en TU | -0,3 | -0,4 | -0,4 | -6,7 |

**Lectura.**

- Un par con métrica 525 gana la elección. El registro del Mac muestra `programChipWithNewMaster` con `HC 1 met 525`, y el Mac
  pasa a anunciar al par como maestro en TLV 4 y TLV 24. La cadencia no cambia.
- El contador tiene que avanzar. La TLV 24 lleva en +12 el contador del maestro superior y en +36 el del emisor. El núcleo llama
  RLFC al primero. Con un valor fijo, el Mac abandona al maestro a los 11 s, con el par aún emitiendo, y registra
  `STALE RLFC`. Con un contador que avanza cada 2,097 s, sigue al par los 60 s, sin ningún aviso.
- Mientras sigue al par, el Mac copia en +12 el contador del maestro, deja quieto el suyo en +36 y reprograma el chip con cada
  valor nuevo. Como maestro, su contador avanza cada 3,146 s, unos 3072 TU, y vuelve a avanzar al recuperar ese papel.
- Al callar el par, el Mac vuelve a ser maestro a los 3,7 s. `peers` sigue en 1 hasta entre 101 y 106 s después de la última
  trama en las tres corridas medidas, y entonces pasa a 0. `cached` no sale de 0.
- Un par seguidor no altera al Mac: sigue siendo maestro, y la diferencia entre relojes se queda en -6,7 TU (p10 -7,5, p90
  -5,9), sin alinearse. Es el valor de esta corrida, porque los dos relojes son independientes.
- La elección del núcleo y el reloj de la capa deciden por separado. Con métrica 300, el núcleo mantiene al Mac como maestro y el
  reloj sigue al par a menos de 1 TU, porque `alignClock` acepta a un par que se declara maestro (§7).
- En el 149 el MIF sale en el tercer AW también con `peers` en 0: `aw % 4 == 2` en 44 de 44 y 45 de 45 MIF previos a dos
  corridas. Con el par de métrica 525 son 165 de 171, y los otros 6 salen en el segundo AW.

El par no se detuvo ni dio errores en ninguna corrida, y el teléfono conservó el root.

**Lo que esto no prueba.** De iOS, las tramas solo toman los valores de métrica, y no vienen de una pila AWDL. Un iPhone puede exigir
algo más. `rx data` sigue en 0, de modo que la recepción de datos por el aire sigue sin probar (§11).

## 11. Datos desde el A53: el firmware no los emite

Ninguna vía probada saca del teléfono una trama de datos AWDL, así que el A53 no puede probar la recepción de datos en el Mac.

**Método.** El módulo `awdl10` envía desde `__init`, sin hilo, de modo que una caída del firmware no deja nada enviando. El estado
de cada trama se lee de `MLME_FRAME_TRANSMISSION_IND` con un kprobe de tracefs en `slsi_rx_frame_transmission_ind`. Los offsets
salen del `scsc_wlan.ko` real: `skb->data` en +0xD8, `host_tag` en +0xC y `transmission_status` en +0xE. En el Mac se leen `Ipkts`
de `awdl0` y el contador `data` de la capa.

**Control con `awdl8`, 24 PSF y 24 MIF por modo.**

| Modo | Destino | Origen | Estado de las 48 tramas |
|---|---|---|---|
| 3 | `awdl0` | La del teléfono | `SUCCESSFUL` |
| 4 | Una dirección inexistente | La del teléfono | `RETRY_LIMIT` |
| 1 | `awdl0` | `02:a5:3e:00:00:01`, ajena al teléfono | `RETRY_LIMIT` |

El T9UH confirma con ACK lo dirigido a `awdl0`. Esto reemplaza la deducción del §8.

**Trama de datos.** Control de trama `0x08` (`0x88` en una fila), BSSID de AWDL y un Echo de IPv6 al enlace local de `awdl0`, en el
formato que espera la capa.

| Vía | Tipo de mensaje | Resultado |
|---|---|---|
| `wlan0`, `MLME_SEND_FRAME` en 802.11 | 0 | Rechazada, `-22` |
| Igual | 1 EAP, 4 ARP, 5 DHCP | Aceptada, 3 de cada tipo, sin aviso de transmisión |
| Igual | 8 `ANY_OTHER` | Aceptada, sin aviso de transmisión en 5 tramas leídas de 46 |
| Igual, `0x88` | 8 `ANY_OTHER` | Aceptada, 3 tramas, sin aviso de transmisión |
| Igual | `0x10` `ACTION` | El firmware cae con la primera: `WLBT FW PANIC 0x04`, subcódigo `0x189c` |
| `mon0`, en 5745 MHz | 8 y `0x10` | Rechazada, `-22`, igual que un PSF de control |

`MA_UNITDATA` no se probó, porque en `tx.c` solo lleva 802.3 y el firmware arma la cabecera a partir de la vif, sin lugar para
el BSSID de AWDL. En el Mac, `Ipkts` siguió en 0 durante toda la serie y la capa contó `data 0` después de las primeras 41 tramas
con `ANY_OTHER`.

- La caída del §8 es reproducible: basta una trama de datos con `ACTION`. Enviada desde `__init` no hay pánico del kernel, el
  teléfono no reinicia, conserva el root y a los 38 s estaba asociado otra vez.
- La falta de aviso no prueba por sí sola que la trama no saliera, aunque coincide con que no llegara nada al Mac.

**Lado del Mac, sin probar por el aire.** Nada de lo leído bloquea estas tramas. En vivo, `RCR` vale `0xF400608E`, con
`CBSSID_DATA` apagado, `RXFLTMAP2` vale `0xFFFF`, y el puerto 1 está en ad-hoc con la MAC de `awdl0` y el BSSID de AWDL. En
`validate_recv_frame`, ninguna comprobación descarta la trama antes del gancho de AWDL. La primera trama de datos real será la del
iPhone, y la capa la registra como `awdl: first data packet up`.

## 12. El A53 como testigo: datos del Mac y tramas de un iPhone

**Datos del Mac por el aire.** Con el teléfono en monitor en 5745 MHz, `ping6 -c 16 -i 1 ff02::1%awdl0`. Es la primera vez que
se ven por el aire tramas de datos de esta pila. Es multicast, así que no mide el ACK, que se mide en el §13.

| Medida | Resultado |
|---|---|
| Tramas oídas / enviadas según la capa | 16 / 16 (`sent` pasa de 1 a 17) |
| Control de trama, reintentos | `0x0800`, bit de reintento en 0 |
| Direcciones | A1 `33:33:00:00:00:01`, A2 `awdl0`, A3 el BSSID de AWDL |
| Encabezado | SNAP `aa aa 03 00 17 f2 08 00`, `03 04`, secuencia que avanza de 1 en 1, `86dd` |
| Velocidad | 12 Mb/s |
| Posición | 15 a menos de 1 TU del inicio de la franja, y 1 a 16 TU, al inicio del segundo AW |

Las 13 tramas `old` del contador son anteriores a esta prueba y no aumentaron. Los relojes de la franja son los del propio Mac: una
trama al mismo inicio de la franja puede llegar antes de que un par que salta de canal ya escuche. Es una duda sin medir `?`, que
el §16 desarrolla.

**Tramas de un iPhone.** El módulo `awdl11` emite el PSF de 327 bytes y el MIF de un iPhone de `receive-ios26.x`, con las TLV 4, 5,
6, 7, 12, 17, 18, 21 y 24 originales. Solo cambian la dirección, el canal 44 por el 149, el reloj y el contador de la TLV 24. El
TLV 2 se elimina, porque la captura lo trae anonimizado en ceros, y por eso no puede probarse la parte de servicios. En 40 s salen
313 + 313 sin fallos, y el Mac sigue a ese maestro desde el primer cuadro hasta 3,7 s después del último.

Una corrida igual con las tramas de `awdl9`, justo después, da los mismos mensajes del núcleo, `wifip2pd`, `sharingd`, `rapportd`
y `mDNSResponder`, una vez normalizados números y direcciones: 495 y 509 líneas, sin ningún mensaje propio de una de las dos,
salvo el único `topMasterChanged`, que difiere en la cola. IO80211 acepta estas TLV sin queja, pero tampoco reacciona de otra forma: sin
TLV 2 no hay nada a lo que conectarse.

**Para una prueba real.** `telefono/vigia_mac.sh` deja al teléfono de testigo en un canal fijo mientras dura la prueba. Sirve para
separar los dos casos que el Mac no distingue por sí solo: un iPhone que no emite datos y una trama de datos que se emite y no llega
a la capa. El teléfono se queda sin Wi-Fi mientras tanto.

## 13. ACK de los unicast enviados con la MAC del puerto 1

La capa daba por supuesto que el chip reconoce los ACK dirigidos a la MAC de `awdl0`, que es la del puerto 1. Para medirlo, el Mac
manda UDP cada 20 ms a una dirección de enlace local con una entrada estática de `ndp` en `awdl0`, y el teléfono escucha en
5745 MHz. El destino cambia en cada corrida.

| Destino | Tramas distintas | Copias por trama | Con bit de reintento | ACK oídos hacia `awdl0` |
|---|---|---|---|---|
| Teléfono en monitor, que no responde | 475 | 7 en 469 de ellas | 2841 de 3315 | 0 |
| Teléfono asociado, con monitor encima | 95 | 7 en 72 de ellas | 514 de 602 | 0 |
| Router de la red | 482 | 1 en todas | 0 de 482 | 482 |

**El chip reconoce esos ACK.** El router responde por hardware a su dirección aunque el BSSID sea ajeno. En las 482 tramas, lo
siguiente que se oye es un ACK a `awdl0`, y el T9UH no repite ninguna. El descriptor de TX no fija el puerto
(`SET_TX_DESC_PORT_ID_8814A` no se usa en el árbol), y aun así la comparación funciona con la MAC del puerto 1.

**Sin ACK, cada trama agota sus intentos.** Salen 7 copias, el límite de 6 reintentos más la primera, y de la primera a la última
pasan 1,8 ms de mediana y 4,8 ms como máximo. Las pocas tramas con menos copias no prueban nada: el monitor pudo perderse alguna,
o la captura pudo cortarlas.

**El teléfono asociado no sirve de par para esto.** Sigue habiendo 7 copias en una ventana de unos 2 s con el monitor activo, así
que su chip no da ACK a estas tramas o el ACK no llega. La causa es desconocida `?`: puede ser el BSSID ajeno o el ahorro de energía.
Tampoco se habría oído a sí mismo. Por eso el destino que decide la medición es el router.

Lo que esta medición no cubre: un par que salta de canal, la agregación A-MPDU con Block Ack y el ACK de un iPhone, que siguen sin
probar `?`.

## 14. Defecto: la capa descarta los paquetes IP de 1429 a 1484 bytes

`ifconfig awdl0` da MTU 1484, y el anillo de TX admite 1428 (`AwdlTxFrame.frame`, `AWDLInterface.h:517`). `queueData` cuenta lo que
no cabe como `bad` y no devuelve ningún error a la pila, así que el emisor no se entera.

Medido con UDP unicast, 5 envíos por tamaño y el teléfono en monitor. Cada paquete que sale se oye 7 veces, porque nadie responde.

| Tamaño IP | Tramas oídas |
|---|---|
| 1348 | 35 |
| 1428 | 35 |
| 1429 | 0 |
| 1448 | 0 |
| 1484 | 0 |
| 1485, que la pila fragmenta | Solo el último fragmento, de 61 bytes: 35 |

`bad` pasa de 0 a 20 en esa corrida, los 4 tamaños por 5 envíos.

**Efecto en TCP.** El SYN del Mac anuncia MSS 1424 y, en las capturas de owly, el del iPhone anuncia 1440. Un segmento completo con
marcas de tiempo llega entonces a 1484 bytes IP, y todo segmento completo del Mac al iPhone se perdería. Los segmentos cortos sí
pasan, de modo que una conexión puede abrirse y quedarse parada en el primer envío grande. La detección de agujero negro de
PMTU está activa (`net.inet.tcp.pmtud_blackhole_detection`, MSS 1200) y podría rescatarla tras varios reenvíos, con un coste en
tiempo sin medir `?`.

**Por qué no apareció antes.** El puente del par virtual usa una interfaz `utun` con MTU 1428 (`AWDL_PAR_VIRTUAL.md` §8): el Mac ve un
MSS de 1368 y nunca supera el límite. Los ping multicast tampoco lo muestran, porque la pila los fragmenta a 1280.

**Con MTU 1428 el defecto desaparece.** `sudo ifconfig awdl0 mtu 1428` toma efecto al momento, sin recompilar ni reiniciar. La misma
medición da MSS 1368 en el SYN y ya no pierde nada: los tamaños 1348 y 1428 salen enteros, y los mayores salen fragmentados, a 1424
bytes más el resto. La capa encola y envía 59 paquetes, los 50 de UDP y 9 SYN, y `bad` sigue en 20. El MSS de owly, 1390, transfiere
con iPhone, y 1368 está cerca de ese valor.

El cambio no se deshace con el mismo comando: `ifconfig awdl0 mtu 1484` responde `Invalid argument`, de modo que el sistema admite como
máximo 1428, el valor registrado en `RegInfo`, aunque la interfaz arranque en 1484. Sin el arreglo de abajo, es de esperar que un reinicio vuelva a
dejar 1484 `?`.

**Origen del 1484.** `IO80211VirtualInterface::peerToPeerConfigureIfnet` (IDA, 25G241) corre dentro de `prepareBSDInterface` y llama a
`ifnet_set_mtu` con 1500 si el controlador es `AppleBCMWLANCore` y con 1484 si no, por encima del valor de `RegInfo`. Es el único sitio
de la familia que da ese valor a una interfaz AWDL.

**Arreglo en la capa, comprobado al arrancar.** `RTLAwdInterface::prepareBSDInterface` llama a la función de la familia y deja la MTU en
lo que cabe en el anillo, 1428. Tras un arranque con esa capa, `ifconfig awdl0` da 1428 sin que nadie lo pida y el registro dice
`prepareBSDInterface 0x0, MTU 1428 -> 0`. `Tools/iphone_test.sh run` sigue poniendo el valor y avisa si no lo logra.

**Alcance de un arreglo.** Ampliar solo el anillo no basta: `rtl8814native_radio_tx_80211` limita la trama a `MAX_XMIT_EXTBUF_SZ`
(1536) menos el descriptor, y una trama con 1484 bytes IP ocupa 1524. La recepción no tiene ese tope: `AWDL_RX_FRAME_MAX` es 2048,
y el tamaño de 1526 bytes de un iPhone queda sin medir `?`.

## 15. Block Ack y secuencia de canales en las capturas de owly

**Block Ack no es requisito para transferir.** El iPhone pide ADDBA en TID 0, con ventana 64 y A-MSDU permitido, unas 4 veces por
segundo en cada ráfaga. Ninguna de las tres capturas trae una respuesta, y aun así los datos pasan.

| Captura | Sentido | Tramas | Tipo | Reintentos |
|---|---|---|---|---|
| `receive` | iPhone a owly | 1986 | QoS, TID 0, sin A-MSDU | 154 |
| `receive` | owly a iPhone | 1868 | Sin QoS, cortas | 0 |
| `send` | owly a iPhone | 346 | Sin QoS, 1450 bytes IP | 0 |

La capa hace lo mismo que owly: una trama de categoría 3 se cuenta como `other` y se libera. `deliverData` acepta QoS sin A-MSDU y
con A-MSDU. Queda sin medir lo que aportaría aceptar la ADDBA con este chip `?`.

**La secuencia del Mac cabe en la del iPhone.** Hay coincidencia en reposo, con el canal de cada captura:

| Equipo | Canal común en las franjas | Otro canal |
|---|---|---|
| iPhone en reposo | 0, 2 y 10 | 6 en la franja 8 |
| owly en reposo | 0, 2 y 10 | Ninguno |
| Mac medido, en el 149 | 0, 2 y 10 | Ninguno |

Por eso el Mac no emite datos cuando el iPhone está en el 6. Con tráfico, el iPhone ensancha su secuencia, hasta ocupar todas las
franjas salvo la 8, y owly la copia en menos de 1 s. En `receive`, 1979 de las 1986 tramas caen en una franja en la que los dos están
en el mismo canal. Queda por ver si el manager del Mac ensancha también la suya `?`. Si no lo hace, los datos pasan solo en 3 de 16
franjas: funciona, pero más lento.

## 16. Riesgo: los datos del Mac salen en el borde de la franja

La capa envía lo que tiene en cola en cada tick de AW, y solo en las franjas servidas: 3 de 16 en reposo. Casi todo lo que se genera
entre franjas sale entonces en el primer tick de la franja siguiente. La posición se estima con el reloj de la TLV 4 del emisor, con
un error de unos pocos TU.

| Emisor | Primeras copias | En los primeros TU |
|---|---|---|
| Mac, unicast al router | 482 | 391 en los primeros 4 TU |
| Mac, multicast (§12) | 16 | 15 en el primer TU |
| iPhone a owly, en franjas en que owly acaba de cambiar de valor en su secuencia | 355 | 6 en los primeros 3 TU |
| iPhone a owly, sin ese cambio | 1477 | Reparto parejo: unas 25 por TU |

Las tres franjas que el Mac sirve en reposo, la 0, la 2 y la 10, vienen después de una en la que el iPhone anuncia 0, de modo que es
el caso en que más puede estar llegando de otro canal. La regla AWDL-16 de owly pide 3 TU de guarda en los bordes para unicast y, a un
nodo de canal fijo, también para multicast. El Mac no la cumple en el borde inicial. Sin ACK, una trama agota sus 7 copias en unos 2 ms
(§13), y una multicast sale una sola vez.

Nada de esto prueba que esas tramas se pierdan: el tiempo que el iPhone tarda en cambiar de canal no está medido `?`. Si se pierden,
los síntomas serían muchos reintentos en unicast y un mDNS del Mac que el iPhone no ve.

**Cambio aplicado.** Al inicio de una franja, `txTick` no envía datos si faltan más de 500 µs para los 3 TU (`AWDL_DATA_GUARD_TU`), y
vuelve a armarse para ese instante. Las tramas de gestión no cambian. El presupuesto de 12 ms que empieza a los 3 TU sigue terminando
dentro del AW de 16,4 ms.

Medido con multicast y `telefono/mcast.sh`: 390 tramas, antes 15 de 16 en el primer TU.

| TU de la franja | 0 | 1 | 2 | 3 | 4 | 5 a 14 | 15 | 16 o más |
|---|---|---|---|---|---|---|---|---|
| Tramas | 1 | 11 | 208 | 135 | 5 | 0 | 6 | 24 |

El grupo se movió a los 2 y 3 TU. El error del reloj no permite separar esos dos valores, de modo que la medición es compatible con los
3 TU pedidos, sin demostrar que ninguna trama salga antes. Las 12 de los TU 0 y 1 quedan sin explicar `?`. Las que aparecen desde el TU 15
son las encoladas durante la guarda, que salen en el tick del AW siguiente.

Medido con unicast al router y `telefono/ack_ap.sh`: 494 tramas, una copia de cada una, ningún bit de reintento y un ACK justo después de
las 494.

| TU de la franja | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 a 14 | 15 | 16 o más |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Tramas | 0 | 6 | 89 | 113 | 104 | 85 | 51 | 3 | 0 | 17 | 26 |

Unicast se reparte más que multicast porque cada trama espera el ACK de la anterior. La guarda no afecta al reconocimiento de los ACK.
Sigue sin medirse si esto mejora algo con un par que salta de canal `?`.

## 17. Cadencia con `en2` sin asociar

Es el punto 3 del §5. Monitor en 5745 MHz, sin par, 2026-11-06, con `en2` recién desconectada del 153 y `mcc` sin avanzar. El manager anuncia
`[6 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0]` en `Low Power`, y la capa deja la radio en el 149 (registro `parking the radio on channel 149`).

| Medida | Sin asociar, 119,8 s | Sin asociar, 29,6 s | En el 149 asociada (§9) |
|---|---|---|---|
| PSF por segundo, fuera de los silencios | **8,89** | 8,81 | 8,80 |
| MIF por segundo, fuera de los silencios | **1,90** | 1,88 | 2,83 |
| PSF y MIF por segundo, con todo | 8,36 y 1,79 | 7,76 y 1,65 | 8,80 y 2,83 |
| Silencios de más de 0,5 s | 2, de 3,56 y 3,59 s | 1, de 3,52 s | ninguno en esa captura |

- **Menos MIF, por menos franjas.** La TLV 18 en el aire pone el 149 solo en las franjas 2 y 10. Las franjas 0 y 8, del 6 en la secuencia del manager, se anuncian con canal 0 porque la radio no va al 6. Un iPhone
  en reposo comparte entonces 2 de sus 3 franjas (§15), frente a las 3 del caso asociado en el 149.
- **Los silencios tienen causa.** Sin asociación, `locationd` pide cada 60 s un escaneo de los canales 1, 6 y 11. `airportd` lo da por terminado en 0,5 s y `rtlRadioScanCopy` llega 500 ms después de `rtlRadioScanStart`, y lo que se
  ve en el aire dura 3,5 s. La capa suma los otros 3 s: `infraTouch` marca 3000 ms de silencio de infraestructura (`kInfraQuietMs`, `RTLAirLayer.cpp`), y `txTick` solo sirve a una radio sin asociar con `!infraBusy()`. Son 0,5 s más 3 s, lo
  medido. Es también la explicación probable de los huecos de unos 3 s por minuto que ya se veían con el par virtual sin asociar.
- **Qué implica.** Los 3,5 s quedan por debajo de los 6 s tras los que el manager del Mac deja de contar con un par. Cuánto tolera un iPhone no está medido `?`. Durante esos 3 s la radio escucha pero no emite PSF, MIF ni datos, y los
  datos que esperan más de 1,2 s se descartan, así que una transferencia se frenaría una vez por minuto `?`.
- **Cambio aplicado (capa `001EF268`, commit 6741f35).** El silencio protege de pedir un cambio de canal justo antes de una asociación. Si la radio ya está en el canal de aparcamiento no hace falta pedir nada: `txTick` sigue
  sirviendo y `parkRadio` solo retiene la petición mientras `infraBusy()`. Medido el 2026-11-07, 119,8 s en 5745 MHz con `en2` sin asociar: 8,81 PSF/s y 1,89 MIF/s, ningún silencio de más de 0,5 s y dos de 0,38 s a las
  18:19:22 y 18:20:23, a 60 s uno del otro, que son los escaneos de `locationd`. `en2` se volvió a asociar al 153 al primer intento (18:23:05) y el ping al router dio 0 % de pérdida.

El router pasó solo del 149 al 153 ese mismo día a las 17:58, sin que nadie lo tocara. Con el router en el 153, este es el caso que aplica a la prueba con un iPhone ([AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md) §3).
