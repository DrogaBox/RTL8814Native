# AWDL: prueba con un iPhone de visita

Guion para aprovechar la visita de alguien con un iPhone: 15 a 20 minutos, con el iPhone siempre en manos
de su dueño. Es la primera prueba con un par real. Lo medido hasta ahora usa el par virtual
([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md)) y, por el aire, solo PSF de un A53 hacia el Mac.

La visita tiene tres preguntas, en este orden:

1. ¿El iPhone llega al Mac? Sus tramas pasan por el chip y el driver, y `IO80211AWDLPeerManager` lo da de
   alta (`peers:1`).
2. ¿El Mac llega al iPhone? El iPhone acepta sus PSF y MIF y lo muestra en AirDrop.
3. ¿Pasa un archivo por el aire, en cada sentido?

Aunque falle la primera, la captura de lo que emite el iPhone sirve para seguir después de la visita.

Lo que no se ha visto en hardware va con `?`.

## 1. Qué hay en el código

| Pieza | Dónde | Commit |
|---|---|---|
| Desvío en `validate_recv_frame`: toda trama con A3 = `00:25:00:FF:94:73` va al gancho AWDL del ABI de radio, con RSSI y canal de operación | `Source/rtl8814au/core/rtw_recv.c`, `Source/Driver/RTLEthernetAdapter.cpp`, `Source/Driver/VendorGlue.cpp` | `ccc4122` |
| `awdl0` entrega cada action frame a `IO80211VirtualInterface::bpfTapInput` en la work queue | `Source/AirLayer/AWDLInterface.{h,cpp}` | `3b53d2a` |
| Emisor de PSF y MIF, reloj de AW, ventanas, puerto 1 | `Source/AirLayer/AWDLInterface.cpp` | `cceb743`, `7e454be`, `7c95ead`, `62a2c49` |
| Datos por `awdl0` en los dos sentidos (§7) | `Source/AirLayer/AWDLInterface.cpp`, `Source/Driver/AWDLTx.c` | `5b686a8`, `5630d49`, `847e46c` |
| Tiempo compartido con la red asociada | [AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) | `8bc4690`, `e166043` |
| Captura de las tramas de los dos sentidos en la radio, sin alterar su comportamiento (§2) | `Source/Driver/AWDLSim.cpp`, `Tools/test_uc.c` | `208e50a` |
| Comprobación, recogida y resumen de la prueba | `Tools/iphone_test.sh`, `Tools/iphone_frames.py` | `208e50a`, `253929e` |
| MTU de `awdl0`, guarda de 3 TU en la franja | `Source/AirLayer/AWDLInterface.cpp` | `e9a103e`, `e181cba` |
| Contadores del manager y retención de datos si el par anuncia otro canal ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md)) | `Source/AirLayer/AWDLInterface.cpp` | `70ad06a`, `ca6be5c` |

## 2. Preparación, antes de la visita

Compilación de la primera visita (la actual está en §8): `RTL8814Native` UUID `DB92193C`, md5 `9440a90b`, y
`RTL8814AirLayer` UUID `001EF268`, md5 `540b6837`. Las dos se instalan juntas. El identificador del disco cambia entre
arranques y un pendrive también se monta como `NO NAME`, así que `deploy` busca la partición EFI del disco
interno. El 2026-11-05 era `disk2s1`, y `disk0s1` es una partición Linux.

```bash
sudo Tools/iphone_test.sh deploy
```

Después de ese paso hace falta un reinicio. La comprobación no pide root ni cambia nada:

```bash
Tools/iphone_test.sh check
```

Termina en `READY` o en `NOT READY`, con cada falta marcada `MISSING`. El script es de bash: llamado con `zsh` da un `NOT READY` falso. Revisa la versión de macOS, los UUID
cargados frente a `Build/`, los boot-args, Bluetooth y la preferencia BLE, AirDrop en «Todos», `awdl0`, los
fallos de TX, el canal de `en2` y el espacio libre.

Ensayo sin iPhone, una vez tras el reinicio:

```bash
sudo Tools/iphone_test.sh run
```

Pasa si aparece `[+] capture self-test: RX path ok`, `tx` sube en las líneas periódicas, `lost` queda en 0, el
resumen del final lista las tramas y `en2` sigue funcionando. La captura solo necesita `rtlawdl_sim=1`, que ya
está.

El autotest corre al empezar cada captura: inyecta una trama por la entrada de RX y comprueba que llega al
anillo. Esa trama no va al archivo; la capa la cuenta como `other` y registra `awdl: rx other 02:00:00:aa:bb:fe`.
El tramo del chip a esa entrada se comprobó el 2026-11-05 con PSF del A53 (`rx af` +256 en el 149). El 2026-11-06
el A53 recuperó el root: envía PSF y MIF, el manager llega a `peers:1`, y también sirve de monitor para ver lo que
emite el Mac ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md)). El ensayo con ese par y la captura en marcha quedó hecho
ese día: `HEARD`, `IO80211 peers 1` y las tramas en `frames.pcapng` (§7 de esa nota).

Para la visita:

- `awdlsim` parado: `check` lo exige, porque ocupa el mismo puerto y envía datos con un solo intento.
- Sin el anuncio BLE del A53, para que no despierte a `sharingd` por su cuenta. La actualización del teléfono
  dejó la app sin instalar; no necesita root, y `adb install` del APK de `Tools/airdrop_beacon` la devuelve.
- La carpeta que deja cada corrida lleva el nombre y las direcciones del iPhone: no va al repositorio.

## 3. Canal

AWDL usa los canales sociales 6 (2,4 GHz) y 44 o 149 (5 GHz)
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md)). El chip escucha en un solo canal.

El 2026-11-05, con país `AR`, el manager del Mac usa `(6/149/149)` y en reposo la secuencia
`[149 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0]`. Las capturas de iOS 26 de owly son del 44. Que un iPhone en
Argentina elija el 149 es lo esperable por el mismo país, pero es `?`.

`check` dice en qué caso está `en2`. El router cambia de canal por su cuenta, así que el caso se mira ese día.

| `en2` | Radio | Qué hacer |
|---|---|---|
| Asociada con primario 149, 44 o 6 | Se queda en ese canal, sin saltos. Es la configuración de la recepción del A53 (149, 2026-11-05). | Seguir asociada. |
| Asociada en otro canal | Tiempo compartido: va a los canales sociales en sus franjas ([AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md)). Sin probar con un par real por el aire. Medido con el A53 en el 153: 1,96 PSF/s y 1,91 MIF/s en el 149, frente a 8,9 y 2,9 sin tiempo compartido ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §5). | Desconectar `en2` de la red desde el menú de Wi-Fi. `en1` da internet si tiene enlace. |
| Sin asociar | La capa fija la radio a 20 MHz en el canal social con más franjas en la secuencia 129 del manager, o en el 149 si no hay. Registro: `awdl: parking the radio on channel C (was N)`. | Es el caso de partida si no se oye nada asociado. Medido con el A53: 8,89 PSF/s y 1,90 MIF/s en las franjas 2 y 10, y un silencio de 3,5 s por minuto ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17). |

Sin asociación, la secuencia por defecto pone el 6 en las franjas 0 y 8
([AWDL_SIN_ASOCIACION_INFRA.md](AWDL_SIN_ASOCIACION_INFRA.md) §3), así que la radio puede acabar en el 6.
Forzar otro canal preferido requiere un reinicio y solo actúa sin asociación:

```bash
sudo Tools/iphone_test.sh channel 6
```

`channel 44` y `channel 149` hacen lo mismo con esos canales, y `channel auto` lo quita. Cada uno reescribe los
boot-args de la capa con los flags actuales y guarda copia en `Backups/`.

## 4. Durante la visita

Antes de que llegue: `Tools/iphone_test.sh check` termina en `READY`. Luego:

```bash
sudo Tools/iphone_test.sh run
```

La corrida crea `~/Desktop/iphone_<fecha>/`, evita que el Mac se duerma y no usa la red. En pantalla aparecen
las tramas oídas (`HEARD <MAC> rssi ... ch ...`), los cambios de `IO80211 peers` y los avisos de `sharingd`.

Durante la corrida, `system_profiler SPAirPortDataType` no se usa: dispara dos escaneos seguidos, 3,25 s y 6 s, con AWDL callado todo ese tiempo, más que los 6 s tras los que el manager deja de contar con un par
(medido el 2026-11-06, tres veces, con el mismo resultado). `check` y `run` ya no lo llaman, y `SPBluetoothDataType` no escanea. El escaneo periódico del sistema, uno de 2,75 s cada 5 minutos con `en2` en el 149, queda por debajo de esos 6 s. Con `en2` sin asociar, el de `locationd` deja 3,5 s de silencio cada 60 s, también por debajo ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17).

En el iPhone, en manos de su dueño:

1. Wi-Fi y Bluetooth encendidos, Compartir Internet apagado.
2. Ajustes > General > AirDrop > «Todos durante 10 minutos».
3. Desbloqueado, a menos de un metro del dongle. Una captura de pantalla o una foto sin importancia, Compartir,
   AirDrop, y la hoja abierta mientras dura la prueba.

En el Mac: una ventana de Finder en AirDrop.

| Paso | Qué mirar |
|---|---|
| A. Un minuto con la hoja abierta | `sharingd: BLE device found`, `HEARD` y `IO80211 peers 1`, en ese orden `?` |
| B. iPhone a Mac | El Mac aparece en la hoja. Tocarlo, aceptar en el Mac, y el archivo llega a `~/Downloads`. |
| C. Mac a iPhone | El iPhone aparece en la ventana de Finder. Arrastrar `envio_chico.txt` de la carpeta de la corrida, aceptar en el iPhone, y después `envio_1mb.txt`. |
| D. Ctrl-C | El resumen sale en pantalla. Se repite con `Tools/iphone_test.sh report <carpeta>`. |

Si la prueba falla, dejarla correr cinco minutos con la hoja abierta antes de cortar: lo que emite el iPhone
queda en `frames.pcapng`.

Con el A53 a mano, `telefono/vigia_mac.sh 5745 900` lo deja de testigo en monitor durante 15 minutos, sin Wi-Fi mientras tanto, y al
terminar lista las tramas de datos. Separa un iPhone que no emite datos de una trama que se emite y no llega a la capa
([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §12).

Con permiso del dueño, el iPhone puede estar además con un cable USB al Mac, tras tocar una vez «Confiar». `check` dice
entonces `iPhone on USB and trusted`, y `run` toma también el lado del iPhone: su registro unificado al hacer Ctrl-C
(`log collect --device`) y el IP de sus interfaces (`rvictl`). Son servicios de lockdown, del mismo tipo que usa
Finder: no piden Modo desarrollador ni imagen de desarrollador. La confianza se revoca después en Ajustes > General >
Transferir o restablecer > Restablecer localización y privacidad. Recoger el registro tarda uno o dos minutos, y el
iPhone sigue en el cable hasta que `run` imprime la carpeta. Que `rvictl` muestre el `awdl0` del iPhone no se comprobó `?`.

| Archivo | Contenido |
|---|---|
| `frames.pcapng` | Tramas AWDL de los dos sentidos en la radio (802.11, `tcpdump -r` las lee); dirección, canal y RSSI en el comentario de cada paquete, con el reloj mach. `capture.txt` da la equivalencia con la hora. |
| `awdl0_ip.pcap` | Tráfico IP de `awdl0`: mDNS y el TLS del puerto 8770. |
| `system.log` | Líneas AWDL del kernel, `sharingd`, `wifip2pd` y `bluetoothd`. |
| `ring.txt`, `ring_final.txt` | Registro propio de la capa (`RTLAwdlLog`). Sus líneas no están en el registro unificado. |
| `state_start.txt`, `state_end.txt` | Interfaces, vecinos y descargas al empezar y al terminar. |
| `iphone.logarchive`, `iphone_system.log` | Solo con el iPhone por USB: su registro desde un minuto antes de `run`, y las líneas de `wifip2pd`, `sharingd`, `bluetoothd`, el dext de Wi-Fi y todo lo que diga AWDL. Llevan datos del dueño y quedan fuera del repositorio. |
| `iphone_ip.pcap` | Solo con el iPhone por USB: IP de sus interfaces, en PKTAP (`tcpdump -k I` muestra la interfaz). |

## 5. Cómo leer el resultado

| Lo que se ve | Lectura | Siguiente paso |
|---|---|---|
| Sin `BLE device found` | `bluetoothd` no pasó el anuncio del iPhone a `sharingd`, o el iPhone no anuncia | Cerrar y abrir la hoja. Comprobar «Todos» en los dos lados. |
| BLE sí, ningún `HEARD`, `tx` sube | El chip no oye AWDL en ese canal, o el iPhone está en otro | Desconectar `en2` (§3), sin reiniciar. Si sigue igual, `channel 6` y reinicio: el iPhone también pasa por el 6 `?`. |
| `HEARD`, `rssi` bajo −70 | El manager descarta el AF por el umbral de borde ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §1.3) | Acercar el iPhone al dongle. |
| `HEARD`, sigue `peers 0`, `rssi` bien | El manager rechaza el AF | Buscar `BAD OUI` e `Invalid frame` en `system.log`. Las tramas quedan para comparar con [AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md). |
| `peers 1`, el Mac no sale en el iPhone | El Mac oye pero el iPhone no lo acepta, o no le llega | `unicast` del iPhone en el resumen: si es 0, no se dirige al Mac. Comparar los TLV propios y los del iPhone en `frames.pcapng`. |
| `peers 1`, `Peer Absence` constantes | El par se cae entre ventanas | Ver el canal de `en2` (§3) y el `dwell` en `ring.txt`. |
| El Mac sale en el iPhone, sin `TCP to 8770` | Falla la resolución o los datos unicast | `state_end.txt` (`ndp -an`), mDNS en `awdl0_ip.pcap`, y `awdl: first data packet up` en `ring.txt`. |
| `TCP to 8770` sin terminar la transferencia | Datos con pérdidas o TLS rechazado | Registro de `sharingd`, y `sent`, `dropped` y `fail` de la línea `awdl: tx psf`. |

Las líneas `awdl: rx af <MAC> rssi ...` del anillo solo salen para las primeras 16 tramas, y luego una línea
cada 256 action frames. Una ráfaga corta no se nota ahí, pero sí en `frames.pcapng`.

## 6. TX: que el iPhone vea este Mac

Con `rtlawdl_tx=1`, mientras `awdl0` está habilitada y la radio no escanea, `awdl0` emite por broadcast
un PSF cada 110 TU y un MIF por franja. Cada trama lleva los TLV 4, 5 y 18 que genera la capa, más la plantilla
de sincronía del kernel (ioctl 151) en los PSF y la plantilla MI (peticiones 149 y 225) en los MIF. Sin plantilla
no sale nada. Las tramas van a OFDM 6 Mb/s con la categoría 127, que no es robusta, así que con PMF activo no se
les añade BIP (`CATEGORY_IS_NON_ROBUST`, `Source/rtl8814au/include/ieee80211.h:1531`).

Sin iPhone, `check` muestra la última línea `awdl: tx psf ... mif ... fail ...`: un `fail` distinto de 0 aparece
como `MISSING`.

| Lo que se ve con el iPhone | Lectura |
|---|---|
| `awdl: clock follows <MAC> aw ... next slot ... TU` en `ring.txt` | El reloj propio se alineó con el iPhone como maestro |
| El Mac aparece en la hoja del iPhone | La TX llega al aire y el iPhone la acepta. No hay otra prueba de la TX por el aire: el Mac no tiene una segunda radio para capturarse. |

Con la TX activa también corren las ventanas (eventos 43 y 41 en cada franja que la secuencia 129 pone en el
canal de la radio; las no servidas se anuncian con canal 0) y el puerto 1, que toma la MAC de `awdl0` y el BSSID
AWDL en modo ad-hoc para que el hardware confirme los unicast (`awdl: port 1 refused` si se rechaza).

## 7. Datos por `awdl0`

- **RX** (`5b686a8`): cada trama de datos AWDL recibida (también las subtramas A-MSDU) con el SNAP de
  Apple sube a `awdl0` como Ethernet II si va a la MAC de `awdl0` o a una dirección de grupo. En el
  registro, una vez: `awdl: first data packet up from <MAC> ethertype 86dd`.
- **TX** (`5630d49`, `847e46c`): cada paquete que la pila manda por `awdl0` se convierte en trama de datos AWDL
  (sin QoS, SNAP de Apple) y espera en un anillo de 64 tramas hasta la ventana de una franja servida. Cada AW
  tiene 12 ms de aire a OFDM 12 Mb/s. Las que esperan más de 1,2 s se descartan. En el registro, una vez:
  `awdl: first data frame to <MAC> ethertype 86dd len N`, y en la línea periódica
  `data queued Q sent S dropped full F old O bad B fail X`.

Limitaciones conocidas:

- el chip reconoce los ACK a la MAC del puerto 1, medido contra un router ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §13). Falta
  verlo con un iPhone, que salta de canal y puede usar Block Ack. Si una trama no recibe ACK, salen 7 copias en unos 2 ms;
- la capa descarta los paquetes IP de más de 1428 bytes. Desde el arreglo del §14 de [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) `awdl0` arranca en 1428, y `check` y `run` avisan si no. En
  `ring.txt`, el contador `bad` delata un paquete perdido;
- el Mac pasa a `Data` con tráfico y envía en 15 franjas de 16, con un techo de 568 KB/s a 12 Mb/s, y retiene los datos en las franjas en las que el par anunció otro canal
  ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5, §6 y §9). `report` cuenta los pasos a `Data`, y la línea periódica del registro trae `held N`;
- un iPhone descarta los datos de una SA que su manager no tiene como par
  ([AWDL_RX_DATA_TO_SKYWALK.md](AWDL_RX_DATA_TO_SKYWALK.md) §6): los datos solo pasan si antes acepta los PSF y
  MIF de este Mac;
- un iPhone sin 6E no recibe el UMI de AirDrop del Mac, por diseño de IO80211 (`AWDL_PAR_VIRTUAL.md` §9.7);
- el coste de un par que pierde tramas no está medido ([AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) §8.3);
- no hay evento 42 (extensiones de ventana);
- `awdl0_ip.pcap` depende de que BPF vea `awdl0` `?`: si `tcpdump.txt` trae un error, el resto de la corrida
  sigue sin él.
La primera visita (2026-11-08) dejó un AirDrop del iPhone al Mac de 64 MB a unos 800 KB/s, y el iPhone no apareció en
la hoja del Mac. Lo que cambió desde entonces:
| Cambio | Dónde |
|---|---|
| El Mac publica solo la presencia del par: ya no hace falta `ndp -s` | `b492c65` |
| Block Ack de recepción, presencia densa y tasa de TX por la señal | [AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md) |
| Ancho de 80 MHz para AWDL, como opción en vivo (`awdlbw`) | [AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md) §6 |
| AirDrop del Mac a un receptor virtual completo, 1527 kB/s con la radio sola | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10 |
| Por qué `sharingd` descarta el anuncio clásico de un iPhone, y el ajuste que lo evita | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10.2 y §10.6 |
Cargado: driver `9B662F1B` (md5 `d06f7b4a`) y capa `207B418D` (md5 `ba4926eb`) con `rtlawdl_air=15000` y
`rtlawdl_lastaw=1`. Un `sudo Tools/iphone_test.sh deploy` sin variables, más un reinicio, devuelve los valores por
defecto.
- `Tools/iphone_test.sh check` termina en `READY`.
- `en2` desconectada de la red: la radio queda fija en el 149 y la presencia densa solo actúa así.
- `awdlsim` y `airdrop_recv.py` parados. El receptor ocupa el puerto 8770 y `sharingd` no puede escuchar ahí.
- Sin el anuncio BLE del A53.
- `defaults read com.apple.Sharing PINPairingAirDrop` contesta que no existe: el ajuste está de fábrica.
```bash
sudo Tools/iphone_test.sh run
```
En otra terminal, el registro en vivo de `sharingd` y `rapportd`. Los mensajes de nivel info, entre ellos
«Dropping legacy endpoint», no quedan guardados y solo se ven así:
```bash
/usr/bin/log stream --info --debug --predicate 'process == "sharingd" OR process == "rapportd"' > ~/Desktop/sharing_vivo.log
```
Con el A53 de testigo en el 149 durante 15 minutos: `telefono/vigia_mac.sh 5745 900`.
| Paso | Qué hacer | Qué mirar |
|---|---|---|
| 1. Mac a iPhone | iPhone desbloqueado con AirDrop en «Todos durante 10 minutos». Ventana de AirDrop de Finder abierta un minuto. | El iPhone aparece en la ventana. |
| 2. Envío | `envio_chico.txt`, después `envio_1mb.txt` y un archivo de 30 a 60 MB. | Aceptación en el iPhone y tiempo del archivo grande. |
| 3. iPhone a Mac | Un video de 50 MB o más. | Tiempo, contra los 800 KB/s del 2026-11-08. |
| 4. 80 MHz | `Tools/test_uc awdlbw 80`, repetir el paso 3, y `Tools/test_uc awdlbw 20` al terminar. | Tiempo con cada ancho. |
| 5. Ctrl-C | En `run` y en el `log stream`. | Resumen en pantalla. |
El Mac y el iPhone del 2026-11-08 quedaron emparejados por código esa noche y el Mac conserva las identidades
([AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md) §4); duran 30 días. Con otro iPhone, o si vencieron,
el primer envío pide un código de seis dígitos: lo muestra el receptor y se escribe en el emisor.
Camino esperado en el paso 1, en `rapportd`: `Starting PairVerify with:`, `Client PairVerify succeeded` y
`Updated browse client with 1 endpoints`. `Tools/iphone_test.sh run` los anuncia en pantalla.
Si el iPhone no aparece en el paso 1, `sharing_vivo.log` dice en qué punto se quedó:
| Lo que trae | Lectura | Siguiente paso |
|---|---|---|
| `Client PairVerify failed` o `PairVerify error` | El emparejamiento guardado no sirvió | Mandar una foto del iPhone al Mac: si el Mac muestra un código, escribirlo en el iPhone, y repetir el paso 1 `?`. |
| Ni «Dropping legacy endpoint» ni «Sending DISCOVER request» | El anuncio de AirDrop del iPhone no llegó a `sharingd` | `wifip2pd`: `Received a new discovery result` en `system.log`; `peers 1` y `HEARD` en la corrida (§5). |
| «Dropping legacy endpoint» y, en `rapportd`, `Attempting discovery with` y `Starting PairVerify with` | El Mac vio el anuncio clásico, lo descartó y busca al iPhone por Rapport; el PairVerify no termina | Guardar el registro. Probar el ajuste de abajo. |
| «Dropping legacy endpoint» y nada de `rapportd` | Los registros `_applicationservicepairing` y `_appsvcprepair` del iPhone no llegaron | `wifip2pd` en `system.log` y los TLV 2 del iPhone en `frames.pcapng`. Probar el ajuste de abajo. |
El ajuste hace que el Mac trate al iPhone como a un receptor clásico (comprobado con el par virtual; con un iPhone
`?`):
```bash
defaults write com.apple.Sharing PINPairingAirDrop -bool NO
killall sharingd
```
Cerrar y abrir la ventana de AirDrop. En el registro en vivo deben salir «Sending DISCOVER request» y
«Got DISCOVER response». Al terminar la visita, de vuelta al valor de fábrica:
```bash
defaults delete com.apple.Sharing PINPairingAirDrop
killall sharingd
```
En `ring_final.txt`, de la capa:
| Línea | Qué dice |
|---|---|
| `awdl: ADDBA from <MAC> … -> status 0` | El iPhone pidió Block Ack y la capa aceptó. |
| `awdl: ba req … accept … decline … delba …, total held …` | Sesiones y tramas reordenadas. Varios `delba` seguidos: el iPhone no recibe los BlockAck del chip `?`. |
| `awdl: dense presence on` / `off` | La presencia densa se enciende al empezar un envío y se apaga 2 s después del final. |
| `awdl: tx rate … by signal (peers at … dBm)` | La tasa elegida por la señal del iPhone. |
| `awdl: tx psf … data queued … dropped full … held … busy …` | `busy` son rechazos pasajeros de la radio; `dropped full`, tramas perdidas por el anillo lleno. |
Con la captura del A53:
```bash
python3 -I Tools/ba_capture_report.py <vigia.pcap>
```
Muestra si el Mac contesta con BlockAck los A-MPDU del iPhone, a qué tasa llegan las tramas del Mac (54 Mb/s con el
par virtual, sin comprobar en recepción `?`), los reintentos y el ancho que usa el iPhone con `awdlbw` en 20 y en 80.
