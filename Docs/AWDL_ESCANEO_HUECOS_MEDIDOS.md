# AWDL: huecos de datos durante los escaneos de infraestructura (medido)

Estado al 2026-11-17. Registro de las mediciones y los arreglos que redujeron el corte de datos de
AWDL/llw0 durante el escaneo de 3 canales de 2,4 GHz que `locationd` pide cada minuto, con el T9UH
asociado al BSS en el canal 157 a 80 MHz y AWDL en el canal 149 (dentro del mismo bloque, centro 155).

El análisis de la familia (qué escaneos deja pasar con `awdl0` activo y qué recibe el controlador)
está en [AWDL_SCAN_COEXISTENCIA.md](AWDL_SCAN_COEXISTENCIA.md). Este documento cubre solo el lado del
driver y de la capa, con números medidos en este equipo.

## 1. Resumen

| Ronda | Driver | Capa | Commits | Corte por canal escaneado | Radio fuera de AWDL por canal | Perdidos (1000/s, 150 s) |
|---|---|---|---|---|---|---|
| Inicial (sin vuelta entre canales) | — | 22CA690A | 88a5a99 | un solo corte de 661 ms | — | 0 de 70000 |
| Vuelta al BSS tras cada canal | AF498CFF | 22CA690A | 25532e2 | 400-460 ms (hasta 428 en el de 3 canales) | — | 0 |
| Vuelta que espera una ranura AWDL | 49869709 | 22CA690A | 28ee1d9 | ~400 ms, ritmo fijo de 524 ms | ~395 ms | 0 |
| Registro de pasos del escaneo | D8DA23E4 | 22CA690A | 151712c | ~405 ms | ~395 ms | 0 |
| Nulls sin esperas fijas de 50 ms | 1404B22B | 22CA690A | 49c5e24 | ~334 ms | ~230 ms | 0 |
| Visita contada a los 50 ms, todas las ranuras en la vuelta | 3905A4BF | A5580BF1 | 522b00c, 19451d5 | 240-334 ms | ~246 ms | 1 |
| Registro de tiempos del HAL | 620F4316 | A5580BF1 | 7f99bde | 238-334 ms | ~246 ms | 0 |
| Potencia TX: solo CCK/OFDM en la vuelta | 83DAC0F6 | A5580BF1 | baafe4c, 98ee2e9 | 187-334 ms | ~214 ms | 3 |
| Aviso inmediato al escaneo y cola tras el escaneo | C23C9B14 | 84830A1D | 284515c, e0e11cc | 203-217 ms | ~181 ms | 0 |
| Salto AWDL solo con el primario, caché de potencia | C8A320FA | 84830A1D | 9b16b0c | 203 ms | ~165 ms | 0 |
| Ventana abierta a mitad de ranura, sondeo de 2 ms | C8A320FA | 004CF04C | c5e26d8 | 157-176 ms (232 si el escaneo empieza en una ranura del BSS) | ~157 ms | 0 |

El corte se mide como el mayor intervalo sin datagramas de vuelta en `llwchan` (§2). Desde 88a5a99
las pérdidas quedaron entre 0 y 3 por corrida (las 3 de 83DAC0F6 en una sola racha de 3 ms, sin
estudiar): lo que queda es latencia.

## 2. Cómo se mide

1. `awdlsim` como par virtual en el mismo Mac (la dirección `fd00::` sale del enlace local de `llw0`,
   el sufijo es el mismo):

   ```
   sudo ifconfig llw0 inet6 fd00::<sufijo de fe80 de llw0> prefixlen 64 alias
   sudo Tools/awdlsim/target/release/awdlsim --plain --mac 06:11:22:33:44:55 --seconds 1800
   ```

2. Stream de 150 s a 1000 datagramas/s de 1200 bytes, y después el registro del escaneo:

   ```
   d=Logs/pruebas_awdl/<nombre>_<fecha> && mkdir -p $d
   Tools/llwchan/build/llwchan -r 1000 -l 1200 -s 150 06:11:22:33:44:55 > $d/r1000.txt 2>&1
   ./Tools/test_uc scanlog > $d/scanlog.txt 2>&1
   ```

3. Desde 81a697c cada línea `stall` de `llwchan` trae el uptime de su principio y su fin en ms, el
   mismo reloj (`clock_get_uptime`) que usa `test_uc scanlog`. Con eso un corte se cruza al
   milisegundo con los pasos del escaneo. Antes se comparaba la hora del día con `kern.boottime` y el
   desfase quedaba en ~1,5 s con error de decenas de ms: esa comparación llevó a una conclusión falsa
   (§4.9).

El registro del escaneo (`RTLScanLog.h`, selector 15) guarda 480 eventos: estados del escaneo,
ejecuciones del handler, canal y dwell, esperas del buffer TX, cambios AWDL (`SL_OFFCH`, durante el
escaneo y hasta 1 s después de `SCAN_DISABLE`), pasos dentro de `BACKING_OP` (`SL_MARK`) y las partes
del cambio de canal del HAL (`SL_PHY`: canal, ancho, potencia TX, IQK).

## 3. Reparto del tiempo fuera de AWDL por canal (C8A320FA)

| Parte | Tiempo |
|---|---|
| Salida: `LEAVING_OP` → `PROCESS` (buffer TX 1,7 + estados 4,5) | ~6 ms |
| Cambio al canal del escaneo: canal 11,75 + ancho 6,3-6,7 + potencia 2-9 | 20-27 ms |
| Dwell | 100 ms |
| Vuelta al BSS (155, 80 MHz): canal 10,9 + ancho 7 + potencia 0-7 | 18-25 ms |
| Resto de `BACKING_OP` (MSR, registro de survey, IGI, MAC IDs, null PM) | 3,7 ms |
| Espera de la capa hasta el cambio a 149 (tick de AW) | 7-16 ms |
| Cambio a 149 (solo el primario) | 0,7 ms |

La potencia TX de 2,4 GHz pisa las entradas OFDM de 5 GHz de la tabla, así que la vuelta reescribe
las 32 entradas OFDM salvo cuando el canal 2,4 tiene los mismos valores (canal 6 en este equipo).

## 4. Arreglos, en orden

### 4.1 Anillo TX y presencia densa (capa 22CA690A, 88a5a99)

A 1000/s se perdía el 2,13 %: el anillo TX de `awdl0` (256 tramas) se desbordaba al empezar el stream
y durante el escaneo, porque el escaneo apagaba la presencia densa. El escaneo ya no la apaga y el
anillo pasó a 1024 tramas. Resultado: 0 de 70000 perdidos, con un hueco de 661 ms por escaneo.

### 4.2 Vuelta al BSS tras cada canal (driver AF498CFF, 25532e2)

Un escaneo asociado volvía al BSS solo cada `RTW_SCAN_NUM_OF_CH` = 3 canales, así que el escaneo de 3
canales no tenía ninguna vuelta: el hueco de 661 ms. Con datos AWDL en curso el driver vuelve después
de cada canal. Hueco máximo 661 → 428 ms.

### 4.3 Vuelta que espera una ranura AWDL (driver 49869709, 28ee1d9)

La vuelta termina cuando empezó en ella una ranura AWDL y pasaron 100 ms (tope 600 ms). No mejoró:
cortes de ~400 ms, uno cada 524 ms exactos. La causa no se encontró sobre el papel; de ahí el
registro de pasos.

### 4.4 Registro de pasos y nulls sin esperas (drivers D8DA23E4 y 1404B22B, 151712c y 49c5e24)

El registro mostró ~395 ms fuera de AWDL por canal, 150 de ellos en esperas fijas de 50 ms: sin
`CONFIG_XMIT_ACK`, `dump_mgntframe_and_wait_ack` es enviar y dormir 50 ms, más el temporizador
`SCAN_PS_ANNC_WAIT`. En modo de pasos el null sale sin esperar el ack (para PM 1 se espera solo a que
salga) y se salta `PS_ANNC_WAIT`. Fuera de AWDL: 395 → 230 ms; corte 405 → 334 ms.

### 4.5 Visita contada a los 50 ms y todas las ranuras en la vuelta (driver 3905A4BF + capa A5580BF1)

Una vuelta cuya primera visita AWDL era la cola de una ranura (32 ms) contaba como servida y el corte
abarcaba dos canales (596 ms). El driver cuenta la visita solo si duró al menos 50 ms. En la vuelta la
capa sirve 15 de 16 ranuras (`AWDL_SHARE_SLOTS`) en vez del patrón de 3 con una para el BSS. Cortes
240-334 ms, ninguno de dos canales.

### 4.6 Potencia TX en la vuelta (driver 83DAC0F6, baafe4c)

`SL_PHY` mostró que los 80 ms de la vuelta eran canal 10,9 + ancho 7 + potencia TX 62 ms:
`PHY_SetTxPowerIndex_8814A` hace una escritura USB a `0x1998` (0,25 ms) por velocidad y cadena, 62
velocidades × 4 cadenas = 248 escrituras. En los canales del escaneo solo se escriben CCK/OFDM
(`phy_check_under_survey_ch`), así que las entradas HT/VHT siguen valiendo para el BSS.
`PHY_SetTxPowerLevel8814` recuerda canal y ancho de su última carga completa y, durante un escaneo,
una carga para el mismo par escribe solo CCK/OFDM. Cualquier otro escritor de una entrada HT/VHT borra
ese recuerdo. Vuelta 80 → 26 ms.

El primer build de este cambio (C712A868) dio panic al arrancar: `build.sh` no recompiló los archivos
que incluyen `hal_data.h` y el kext mezcló dos formas de `HAL_DATA_TYPE` (`dm->adapter` NULL en
`phydm_nbi_enable`). 98ee2e9 hace que `build.sh` vigile también las cabeceras de `Source/rtl8814au`;
después de cualquier cambio de cabecera se borra `Build/obj` antes de compilar.

### 4.7 Aviso inmediato al escaneo y cola tras el escaneo (driver C23C9B14 + capa 84830A1D)

- La vuelta preguntaba por su fin con un temporizador de 50 ms, así que la radio seguía en el BSS
  23-30 ms después de terminar la visita AWDL. Al contar la visita, el driver arma el temporizador a
  1 ms (`set_survey_timer(pmlmeext, 1)`). Ahora `LEAVING_OP` empieza 0,006 ms después.
- Al terminar el escaneo la capa volvía al patrón normal, cuya primera ranura es del BSS, y el último
  canal cortaba ~334 ms. Durante 262 ms (`AWDL_SCAN_TAIL_US`) después de ver terminar un escaneo con la
  radio asociada, la capa sirve las ranuras como en una vuelta. Último canal 334 → 203 ms.

### 4.8 Salto AWDL con solo el primario y caché de potencia (driver C8A320FA, 9b16b0c)

- Un salto AWDL entre 149 y 157 dentro del bloque de 80 MHz mantiene índice de canal (155) y ancho,
  pero `phy_SetBwMode8814A` reescribía todos los registros de ancho: 7 ms por salto. Con
  `awdl_keep_txpwr` y el índice sin cambiar se escriben solo `REG_DATA_SC` y `0x8ac[5:2]` (y el bit
  CCK a 40 MHz) en `phy_SetBwPrimary8814A`. Salto 7 → 0,7 ms. Vale también fuera de los escaneos.
- `PHY_SetTxPowerIndex_8814A` guarda el índice escrito por cadena y velocidad
  (`txpwr_idx_cache`, se borra en `rtl8814au_hal_init`) y durante un escaneo no reescribe una entrada
  que ya lo tiene. 1M se escribe siempre porque su escritura abre la tabla.

Fuera de AWDL 181 → 165 ms, pero el corte siguió en 203 ms.

### 4.9 Ventana a mitad de ranura y sondeo de 2 ms (capa 004CF04C, c5e26d8)

Con los uptimes de §2.3 los cortes terminan 33,5-34 ms después de `AWDL radio to 149`, en cada vuelta.
Una comparación anterior con la hora del día había ubicado esos ~30 ms antes de salir de AWDL: era
error del desfase de relojes.

Causa: `txTick` abre la ventana de datos (`_awOpen`) solo al empezar una ranura servida, y la vuelta
del escaneo llega a mitad de ranura; los datos esperaban a la ranura siguiente. Ahora, si la radio
entra tarde a una ranura servida, se abre una ventana propia por lo que queda de la ranura, sin los
eventos 41/43 de la familia. Además, mientras la radio está en un canal del escaneo con un par cerca,
`txTick` corre cada 2 ms (`AWDL_SCAN_POLL_US`) para que el cambio a 149 siga a `BACK_OP` sin esperar
al borde del siguiente AW (7-16 ms).
Medido el 2026-11-17 19:26 (`Logs/pruebas_awdl/scan_midslot_20261117`, 0 de 150000 perdidos): la capa
cambia a 149 entre 1,7 y 2,1 ms después de `BACK_OP` (antes 7-16) y los datos vuelven 0,14 ms después
del cambio (antes 34). Cortes por canal: 157-176 ms en el segundo y el tercero, 176-203 en el
primero. Cuando el escaneo empieza durante una ranura del BSS el primer corte suma esa ranura: 232
ms, con los datos parados 67 ms antes del inicio del escaneo. El tercero lleva unos 14 ms más que el
segundo: la vuelta de `COMPLETE` reescribe la potencia OFDM (7 ms) y `DISABLE` agrega 3,8.

### 4.10 Vuelta que termina con la visita contada (driver 03DDCBC5, 09c98e5)

Medido el 2026-11-17 19:42 con 3F4D71D7 + 004CF04C (`Logs/pruebas_awdl/scan_bf_20261117`, 0 de
150000 perdidos). Primer escaneo: 183, 156 y 171 ms. Segundo escaneo: 216, 217 y 171 ms, con los dos
primeros cortes separados por solo 46 ms. En el registro, la vuelta del canal 1 llegó a 149 a 54 ms
del fin de una ranura AWDL. La visita contó al terminar la ranura (59 ms después de `BACK_OP`), pero
el piso de 100 ms de la vuelta (`RTL_AWDL_SCAN_STEP_MIN_MS`) dejó la radio en el canal del BSS hasta el
siguiente sondeo de 50 ms. Ese piso venía de cuando la visita se contaba al empezar la ranura. Desde
4.5 una visita cuenta recién cuando duró 50 ms y la radio volvió al BSS, así que el piso ya no
protegía nada y se quitó: la vuelta termina apenas la visita cuenta.

Medido el 2026-11-18 22:36 con 03DDCBC5 + 004CF04C. En una primera pasada de 150 s no hubo escaneos de
locationd: el último del registro fue unos 15 minutos antes (`scan_nomin_20261118`). En la segunda se
forzaron dos escaneos completos con CoreWLAN (`~/re_cache/work/awdl_tests/fullscan 2 40`,
`Logs/pruebas_awdl/scan_nomin_full_20261118`): 15,8 y 16,1 s cada uno, 6 de 150000 perdidos en una
sola racha de 6 ms. En 65 canales de escaneo hubo un corte por canal, con una mediana de 152 ms, un
máximo de 172 y un caso de 203 a mitad del primer escaneo. No hubo ningún par de cortes pegados
(ninguna separación menor a 150 ms). El anillo del registro (480 eventos) no alcanza para un escaneo
completo, así que el corte de 203 ms quedó sin registro.

Los tiempos de `test_uc scanlog` son los 32 bits bajos del uptime en µs y dan la vuelta cada
4294,97 s. Para compararlos con los de llwchan hay que sumarles k × 4294967,296 ms.

### 4.11 Corte al empezar cada stream (capa BF91B982, 1fe292d)

No es del escaneo, pero aparece en todas las corridas: unos 180 ms después de empezar el stream
llega un corte de unos 465 ms, luego pasan 59 ms con datos y viene otro de 203 ms, sin pérdidas.
Mientras está asociada a un canal que no es social, la capa reparte la radio entre el BSS y AWDL
(`mccChannel`). Sin tráfico sirve solo la primera ranura de cada tramo de 149 de la secuencia: en el
registro del driver se ve una visita a 149 de unos 23 ms cada 524 ms. El modo denso (`updateDense`),
que suma ranuras a AWDL, se encendía con dos muestras ocupadas de 250 ms, y solo contaba tramas ya
movidas. Las tramas que el stream encolaba esperaban la ranura servida siguiente (el corte de 465 ms),
y la segunda muestra ocupada llegaba después del corte de 203 ms. Ahora 32 tramas encoladas para TX
dentro de una muestra (`AWDL_DENSE_QUEUED`), con un par oído en los últimos 2 s
(`AWDL_DENSE_PEER_US`), encienden el modo denso en el tick siguiente (un AW). Las ráfagas de unas 16
tramas para las que se pensó la regla de dos muestras quedan por debajo.

Medido el 2026-11-19 08:09 con 03DDCBC5 + BF91B982 (`Logs/pruebas_awdl/stream_start_20261119`): cinco
streams de 15 s a 1000/s separados por 8 s, cada uno desde reposo (el registro de la capa muestra
cinco encendidos y apagados del modo denso). Ninguno tuvo corte al empezar: el round trip máximo del
primer segundo fue de 71-83 ms (antes 465), sin pérdidas (0 de 75000). En marcha el máximo queda en
unos 72 ms, la ranura que el reparto deja al BSS. El segundo stream tuvo un escaneo de locationd:
181, 157 y 171 ms.

### 4.12 Solo las tasas OFDM básicas en un canal de 2,4 GHz (driver 8B1B744C, 035bd41)

Con el BSS en 5 GHz la tasa de gestión es OFDM 6 Mb/s (`update_mgnt_tx_rate` quita 11b por encima del
canal 14). En un canal de 2,4 GHz del escaneo la radio solo transmite las probe requests (a 6 Mb/s) y
las respuestas de control (ACK) a las tasas OFDM básicas, 6/12/24. Sin embargo, el canal de escaneo
cargaba las ocho tasas OFDM en los cuatro caminos, y la vuelta tenía que reescribir esas 32 entradas,
unos 7 ms. Ahora `PHY_SetTxPowerIndex_8814A` salta las demás tasas OFDM cuando se cumplen cuatro
condiciones: está cargando un nivel, el canal es de escaneo (`phy_check_under_survey_ch`), el canal
es ≤ 14 y la tasa de gestión no es CCK. Las entradas saltadas conservan los valores del canal del
BSS, y la caché de potencia no las toca. CCK se sigue cargando completo: solo lo usa 2,4 GHz, así que
sus valores se repiten de un escaneo a otro y la caché ya los salta. Fuera de un escaneo, o con el BSS
en 2,4 GHz (gestión en CCK), no cambia nada. Ahorro esperado: unas 20 escrituras de 0,22 ms en cada
sentido, entre 4 y 9 ms por canal de 2,4 GHz. Sin instalar ni medir. El build preparado es 290C9734 (d699814): suma al registro del escaneo el cambio de banda (parte 5) y fc_area más el canal RF (parte 6), para repartir los 11-12 ms de cada cambio de canal.

### 4.13 locationd pide un escaneo pasivo (sin cambiar todavía)

En el registro del sistema, el escaneo de locationd de cada minuto llega como `scanType=2`. La
familia escribe `Passive scan` y `Scan type to FW 2`. Verificado en IDA (25G241,
`IO80211ScanRequest::getScanRequestForDriver`): el tipo va en `apple80211ScanRequest` +64 (1 activo,
2 pasivo; los valores válidos son 1-4), los canales en +88 y la cuenta en +84. La capa lee solo los
canales, así que el driver escanea en modo activo (dos probe requests por canal). Para respetar el
modo pasivo haría falta pasar +64 desde `setWCL_SCAN_REQ` (`RTLAirLayer.cpp`, archivo de otra
sesión) y poner `scan_mode = SCAN_PASSIVE` en `rtl8814native_trigger_scan`. Con el escaneo pasivo
no se transmite nada en el canal, y ni siquiera habría que cargar la potencia TX. Un dwell de 100 ms
con beacons cada 102,4 TU pierde a veces un AP.

Hecho en d4ec33c (driver 4606114B + capa 23D23B09, sin instalar ni medir). `setWCL_SCAN_REQ` lee
+64 y pasa `RTL_RADIO_SCAN_PASSIVE` como tercer parámetro de `RTL_RADIO_FN_SCAN_START`. Un driver
viejo lo ignora y sigue escaneando en modo activo. `rtl8814native_trigger_scan` pone
`scan_mode = SCAN_PASSIVE` y marca pasivos los canales de la lista. `site_survey` le pasa a la HAL,
en `rtl8814native_scan_no_txpwr`, el canal que no manda ninguna probe request, y el cambio a ese
canal salta `rtw_hal_set_tx_power_level`. Se pasa el número de canal y no una bandera: un salto
AWDL que espera `setch_mutex` mientras tanto pone otro canal y sigue cargando su potencia. Este
build incluye también 4.12 y las partes 5/6 del registro (cambio de banda y canal RF).

### 4.14 Escaneo pasivo medido, con el BSS en el canal 52 (driver 4606114B + capa 23D23B09)

Instalado y medido el 2026-11-19. Antes de la prueba el router cambió por su cuenta del canal 157 al
52 a 80 MHz (centro 58). AWDL sigue en el 149, que ya no cae en el bloque del BSS, así que cada salto
AWDL es un cambio de canal completo. El dongle arrancó además en USB 2 (`usb_speed=2`,
`switch_issued=yes`, `ss_finalized=no`), con el mismo puerto y el mismo cable que en las pruebas
anteriores en USB 3. Los datos están en `Logs/pruebas_awdl/passive_20261119/`.

- llwchan 300 s a 1000/s de 1200 bytes: 0 perdidos de 300000. Mediana de ida y vuelta 30,5 ms, p95
  146 ms, máximo 241 ms. Hay un corte de 157-176 ms cada ~262 ms durante toda la prueba (1133 cortes),
  con o sin escaneo. Con el BSS en el 157 (corrida de la capa BF91B982, 4.11) la mediana era 0,09 ms y
  los cortes periódicos, de ~72 ms.
- Un salto AWDL de vuelta al canal 58 (registro de escaneo): canal 3,1 ms, ancho 6,7 ms y potencia
  TX 8,0 ms, unos 18 ms en total. La caché de potencia no ayuda porque el 58 y el 149 tienen
  entradas distintas.
- Los dos escaneos de locationd de la prueba (11:56:57 y 11:58:00, 3 canales, `Passive scan`) dieron
  tres cortes cada uno: 235, 188 y 242 ms, y 204, 187 y 242 ms.
- Desglose de un escaneo pasivo (12:00:49): del inicio a `PROCESS` pasan 105 ms (aviso de ahorro de
  energía 50,7 + 50,1 ms). En el canal 1: banda 8,5 ms, canal RF 2,5 ms, canal en total 11,8 ms y
  ancho 6,2 ms. En el 6 y el 11: canal 3,4 ms y ancho 6,2-6,7 ms. En ninguno de los tres se carga la
  potencia TX. El dwell es de 100 ms en cada canal. La vuelta al 58 tarda banda 7,9 ms, canal 10,9 ms
  y ancho 6,8 ms; la potencia TX tarda 0,01 ms porque las entradas del BSS siguen en su lugar. Desde
  el inicio del escaneo hasta volver al BSS pasan 463 ms, con los tres canales seguidos.

El modo pasivo funciona: no salen probe requests ni se carga la potencia en el canal escaneado. El
corte grande ahora es el del reparto con el BSS en otro bloque: el ancho (6,2-6,8 ms) se programa en
cada cambio aunque no cambie, y la potencia TX (8 ms) se recarga en cada salto AWDL.

### 4.15 Ancho sin reprogramar si no cambian banda ni ancho (driver F9ED9F6A, db67bb2)

`PHY_HandleSwChnlAndSetBW8814A` pide programar el ancho cada vez que cambia el centro, aunque el
ancho siga igual: un canal de escaneo de 20 MHz al siguiente, o un salto AWDL entre los bloques del 58
y del 155 a 80 MHz. `phy_SetBwMode8814A` escribe entonces el MAC 0x668, el BB 0x8ac[1:0] y
0x82c[15:12], y hace cuatro lectura-escritura de RF 0x18[11:10]; todo eso depende solo de banda y
ancho. La HAL guarda en `bw_regs_applied` la banda y el ancho con los que escribió esos registros (se
borra en cada inicio de la HAL) y los salta si no cambiaron. El subcanal, el reloj del ADC y el spur
siguen el canal como antes. Ninguna otra parte del driver escribe esos bits; la calibración LC toca
RF 0x18 entero pero lo restaura. Compilado limpio (132 archivos), instalado el 2026-11-19 (copia de 4606114B en `efi_backups/20261119_pre_F9ED9F6A`).

Medido el 2026-11-19 (`Logs/pruebas_awdl/bwskip_20261119/`): sin cambio. llwchan 300 s dio la
misma mediana de 30,5 ms, cortes de 158,6 ms cada ~262 ms y 17 perdidos de 300000 en una sola racha
de 17 ms. El registro muestra la causa: el salto AWDL va al 149 a 20 MHz y el BSS está a 80 MHz, así
que el ancho sí cambia en cada ida y vuelta (6,2-6,8 ms cada vez). En el escaneo con tráfico, la
primera vuelta al BSS recargó la potencia entera (57 ms); las siguientes tardaron 0,01 ms. No se
comprobó qué borró las entradas antes de esa vuelta.

### 4.16 Salto AWDL al ancho del BSS en otro bloque de 5 GHz (driver 38DDEC4D, ba20ad6)

Si el canal AWDL cae en otro bloque de 5 GHz que vale al ancho del BSS (`rtw_chset_is_chbw_valid`),
`rtl8814native_radio_awdl_offch` salta a ese bloque con el mismo ancho, con el canal AWDL como
primario. Con el BSS en el 52 a 80 MHz, el 149 va a 80 MHz con centro 155. Banda y ancho no cambian,
así que la HAL salta los registros de ancho (4.15) y solo carga la potencia del índice nuevo. Las
tramas AWDL siguen con su propio ancho (`rtl_awdl_agg_cfg`) en el primario, como cuando el BSS estaba
en el 157 y el salto quedaba dentro de su bloque. Compilado y subido; instalado el 2026-11-19 (copia de F9ED9F6A en `efi_backups/20261119_pre_38DDEC4D`), sin medir.

### 4.17 Caché de potencia fuera del escaneo y log de los saltos AWDL (driver 67AF395A)

El caché de escrituras de potencia (4.8) solo saltaba una entrada sin cambios con `_FW_UNDER_SURVEY`.
Los 8 ms de potencia del salto 58 ↔ 149 (4.14) salieron del log de partes, que solo escribe durante
un escaneo, así que se midieron con el caché activo. Fuera del escaneo cada salto AWDL y cada vuelta
reescribían la tabla entera (~248 escrituras; la recarga completa medida es de 57 ms). No está
medido en el salto porque el log no escribía ahí. Toda escritura TXAGC pasa por
`PHY_SetTxPowerIndex_8814A`, también la del seguimiento de potencia de phydm, y el caché se borra en
cada init de la HAL, así que ahora vale siempre. El log de partes (`SL_PHY`) escribe también mientras
`offch_state` es `OFFCHS_AWDL`: la ida y la vuelta de cada visita AWDL. Sobre 38DDEC4D; instalado el
2026-11-19 (copia de 38DDEC4D en `efi_backups/20261119_pre_67AF395A`).

Medido el 2026-11-19 con 38DDEC4D + 67AF395A (BSS en 52/80, AWDL en 149; llwchan contra awdlsim,
1000/s de 1200 bytes, 300 s; `Logs/pruebas_awdl/txpwrcache_20261119/`):

- llwchan: RTT mediana 30,5 → 0,72 ms, p95 106 ms, p99 118 ms. Cortes cada 262 ms de 117 ms
  (p10 116,7, p90 117,9; antes 157-176), 1124 cortes, 23 de más de 150 ms (escaneos). Perdidos
  4 de 300000.
- Cada salto AWDL, en los dos sentidos, tarda ~55 ms: canal RF 2,5 (canal en total 3,1-3,2), ancho
  3,25-3,5 y potencia TX 48 ms (mediana de 35 saltos a 58 y 34 a 155). El ancho ya no reprograma los
  registros de ancho (4.15-4.16); quedan sub-canal, reloj ADC y spur (155/80 es canal de spur con
  `rfe_type=1`). La potencia es la tabla entera (OFDM y todas las HT/VHT de 1-3 SS): con el caché
  activo siguen ~200 escrituras porque entre 58 y 155 cambian casi todas las entradas.
- Del inicio del salto a 58 al inicio del salto a 155 pasan 58,3 ms (mediana): el salto ocupa ~55 y
  al BSS le quedan ~3 ms de su ranura de 65,5 ms. Del inicio del salto a 155 al siguiente a 58 pasan
  203,8 ms, de los que ~149 quedan en el 149. Ciclo de 262 ms.

### 4.18 Visita AWDL con potencia solo OFDM (driver F31FDF84)

El escaneo ya carga solo CCK/OFDM en sus canales y deja las HT/VHT con los valores del BSS
(`txpwr_legacy_only`, 4.10). La visita AWDL no lo hacía. Ahora `PHY_SetTxPowerLevel8814` carga solo
OFDM mientras `offch_state` es `OFFCHS_AWDL`, en la ida y en la vuelta, si las HT/VHT tienen una
carga completa al mismo ancho (`txpwr_ht_ch`/`txpwr_ht_bw`). En el 149, las tramas de acción AWDL,
los ACK y los CTS salen a tasas OFDM con la potencia del 149; los datos HT/VHT salen con los índices
del canal del BSS. Opción en caliente `RTL_OPT_AWDL_FULLPWR` (`test_uc awdlfullpwr 1`) para volver a
la carga completa y comparar en la misma sesión. Esperado: potencia ~7 ms por salto (32 entradas
OFDM, menos las iguales), salto ~14 ms. Compilación limpia (132 archivos); instalado el
2026-11-19 (copia de 67AF395A en `efi_backups/20261119_pre_F31FDF84`).

Medido el 2026-11-19 con F31FDF84 + capa 23D23B09, BSS en 52/80, AWDL en 149, solo OFDM (llwchan
contra awdlsim, 1000/s de 1200 bytes, 300 s; `Logs/pruebas_awdl/ofdmonly_20261119/`):

- Potencia TX por salto 48 → 8,0 ms (mediana de 40 saltos en cada sentido): las 32 entradas OFDM
  cambian todas entre 58 y 155. Salto completo ~14,5 ms (canal 3,1-3,2, ancho 3,25-3,5, potencia 8).
- El ciclo de la capa no cambia (58,8 ms del lado del BSS, 203,6 del lado AWDL), pero al BSS le
  quedan ~44 ms de su ranura en vez de ~3, y al 149 ~189 ms en vez de ~149.
- llwchan: RTT mediana 0,72 → 0,10 ms, p95 106 → 65 ms, p99 118 → 76 ms. Los cortes periódicos
  bajan de 117 ms a menos de 100 (llwchan solo lista los de más de 100 ms: quedaron 12, todos de
  escaneos); el p99 de 76 ms cuadra con el radio fuera del 149, 14,5 + 44 + 14,5 ms. Perdidos 3 de
  300000.

La corrida de comparación con la tabla entera (`test_uc awdlfullpwr 1`, 120 s) no sirve: el router
movió el BSS de 52/80 a 157/80 a las 13:57:35, justo al empezar (5 s sin respuesta y luego cortes
de 335-685 ms mientras se reasociaba). La referencia de la tabla entera es la medida de 67AF395A
(4.17), con el mismo código y el BSS en 52. Con el BSS ya en 157 (salto dentro del bloque, 0,6-0,7
ms, sin potencia), 60 s: 0 perdidos, RTT mediana 0,10 ms, corte máximo 72,9 ms. Es decir, con solo
OFDM el salto a otro bloque queda casi igual que el salto dentro del bloque: lo que resta es la
ranura del BSS (65,5 ms), que decide la capa.

### 4.19 Adelanto del salto según los últimos saltos (capa 02F6CDEA)

La capa arranca el salto al canal AWDL antes de la ranura servida: adelanto = peor salto + 1,5 ms,
entre 3 y 8 ms. El peor salto era el de toda la sesión (`_mccUsMax`, 35,9 ms en la sesión del
2026-11-19 tras reiniciar), así que el adelanto quedaba siempre en el tope de 8 ms. En la corrida
de 4.18 los saltos de ida empiezan 6,2-7,1 ms antes del borde de la ranura (fase de cada `HAL set`
contra la de las vueltas, `scanlog_ofdm.txt`): 8 ms menos ~1,5 de demora del timer. Con saltos de
14,5 ms, el radio llega al 149 unos 8 ms después de empezada la ranura. Con el BSS en 157 (salto de
0,7 ms) pasa lo contrario: el radio sale 6,5 ms antes de lo necesario y ese tiempo se le quita al
BSS.

Cambio: el adelanto sale del peor de los últimos 8 saltos hacia AWDL (`_mccLeaveUs`), más 1,5 ms,
entre 3 y 20 ms. Esperado: con el BSS en otro bloque, adelanto ~16 ms y el radio en el 149 al
empezar la ranura, a cambio de ~8 ms menos para el BSS (~44 → ~36 ms); en el mismo bloque, adelanto
de 3 ms y ~5 ms más para el BSS. Instalada el 2026-11-19; sin medir, necesita el BSS fuera del bloque del 149 para verse.

El contador `early` de la línea `awdl: tx psf ...` de `RTLAwdlLog` no sirve para comprobarlo: esa
línea se corta a 191 caracteres y el número queda truncado (se lee `early 1` o `early `).

Medido el 2026-11-19 14:30 con F31FDF84 + 02F6CDEA, BSS en 157/80 (mismo bloque que el 149; saltos
de 0,6-0,75 ms en el HAL, ~1,9 ms medidos por la capa), llwchan contra awdlsim, 1000/s de 1200
bytes, 300 s (`Logs/pruebas_awdl/lead_20261119/`):
- Inicio a inicio de los `HAL set`: lado AWDL 198,85 ms de mediana (198,2-200,0) para 3 ranuras
  de 196,6, lado BSS 63,16 ms (62,0-64,5) de una ranura de 65,5. El radio sale del BSS ~2,3 ms antes
  del borde. Con la capa 23D23B09 y el mismo bloque (14:06, sin datos, 1 ranura de 8) salía ~6,7 ms
  antes (72,2 ms para una ranura de 65,5): el BSS recupera ~4,4 ms por ciclo.
- llwchan sin cambio frente a 23D23B09 con el BSS en 157 (4.18): 0 perdidos de 300000, RTT mediana
  0,09 ms, p95 59,3, p99 69,5 ms. Dos cortes de más de 100 ms (203 y 155 ms), juntos a los 151 s:
  un escaneo.
- La mitad del cambio con el BSS en otro bloque (adelanto de ~16 ms, radio en el 149 al empezar la
  ranura) sigue sin medir.

## 5. Estado y pendientes

- En OPENCORE (2026-11-19): driver 37F6A47A (2270100B + potencia de OFDM 6/12/24 y la tasa de datos en la ida de la visita AWDL, plan §4, ff5f58b, sin medir; copia de 2270100B en `efi_backups/20261119_pre_37F6A47A`; 2270100B = EAB0ABE7 + salto de prueba `test_uc testhop`, 940c888; copia de EAB0ABE7 en `efi_backups/20261119_pre_2270100B`; EAB0ABE7 = C78046E2 + salto por firmware H2C 0x1C opcional del plan §3, 41bfff3, apagado al arrancar; copia de C78046E2 en `efi_backups/20261119_pre_EAB0ABE7`; antes C78046E2 = F31FDF84 de 4.18 + prueba de escrituras USB, copia de F31FDF84 en `efi_backups/20261119_pre_C78046E2`) + capa 02F6CDEA (4.19, 4cdc868; copia de 23D23B09 en `efi_backups/20261119_pre_02F6CDEA`), medida solo con el BSS en el mismo bloque. Antes, driver 4606114B (4.12-4.14, d4ec33c) y antes de eso: driver 03DDCBC5 (09c98e5, vuelta sin piso de 100 ms, sobre 3F4D71D7 = e20e1ce: C8A320FA más la corrección de un NULL del
  código original en `beamforming_get_report_frame`, que tiró un panic al arrancar), capa BF91B982 (1fe292d), guardados en `efi_backups/20261119_pre_4606114B_23D23B09`. Copias de cada paso en
  `~/re_cache/work/efi_backups/` y builds en `~/re_cache/work/llw_plain/stage_*`. El pen tiene
  83DAC0F6 + A5580BF1 (sincronizado antes de estos cambios).
- Los inicios de los cortes de un escaneo van separados por 524,0 ms exactos (32 AW): la salida del
  escaneo sigue atada al fin de una visita AWDL de la vuelta, que la capa decide por ranuras.
- Quedan sin tocar: cambio de banda ~8 ms en cada sentido, potencia TX en cada salto AWDL con
  el BSS fuera del bloque del 149 (48 ms con la tabla entera, 8 ms con solo OFDM desde 4.18; se
  podría bajar a OFDM 6/12/24), y el dwell de 100 ms. El ancho repetido tiene arreglo preparado
  (4.15 + 4.16: F9ED9F6A instalado y medido sin cambio porque el salto AWDL iba a 20 MHz; 38DDEC4D salta a 80 MHz, instalado sin medir). El modo pasivo de locationd quedó instalado y medido (4.14). El corte de ~465 ms al
  empezar cada stream quedó resuelto (4.11, capa BF91B982, instalada y medida).
- Una sesión real de Continuity Camera pone a AWDL en modo real-time, que retiene los escaneos de
  infraestructura el primer minuto ([AWDL_SCAN_COEXISTENCIA.md](AWDL_SCAN_COEXISTENCIA.md)); no se
  comprobó qué banderas pone la cámara.
- Los datos crudos están en `Logs/pruebas_awdl/scan_*_20261117/` (fuera del repositorio).
