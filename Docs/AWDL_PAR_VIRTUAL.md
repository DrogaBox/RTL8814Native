# AWDL: par virtual en el mismo Mac

Prueba del lado Mac de AWDL contra un par que ejecuta el protocolo completo (sincronización,
elección, secuencia de canales, TLVs y datos), sin segunda radio ni dispositivo Apple. Responde a una
pregunta: ¿el `IO80211AWDLPeerManager` acepta un par, se sincroniza con él en los dos papeles de la
elección e intercambia datos? Lo no comprobado en hardware va con `?`.

## 1. Cómo funciona

Con el boot-arg `rtlawdl_sim=1`, el driver abre un puerto en el límite de la radio:

- **Escucha**: cada trama AWDL que el AirLayer entrega a `rtlRadioTx80211` se copia a un anillo de 256
  registros, con la marca de tiempo (`mach_absolute_time` en µs, la misma base que `awdlNowUs`) y el
  canal de operación en el momento del envío. La trama sigue saliendo al aire.
- **Inyección**: una trama del par entra por `rtl8814native_awdl_rx`, el mismo camino que usa la
  radio al recibir. Solo entra si el canal de operación coincide con el del par, porque fuera de él la
  radio no la habría oído.

`Tools/awdlsim` ejecuta un nodo `crosswind-awdl` (copia en `Tools/awdlsim/vendor`, MIT, con el cambio
de `PATCHES.md`) entre el anillo y la inyección. El AirLayer y `IO80211Family` no cambian.

| Pieza | Dónde |
|---|---|
| Anillo, dueño, espera, abort | `Source/Driver/AWDLSim.cpp`, `Source/Driver/RTLAwdlSim.h` |
| Copia en TX e inyección por la RX | `Source/Driver/AWDLTx.c` (`rtl8814native_radio_tx_80211`, `rtl8814native_awdl_sim_rx`) |
| Inyección bajo el cerrojo de ciclo de vida y solo en la instancia dueña de la radio | `RTL8814Native::awdlSimInject` |
| Arranque y abort en `stop`/relevo USB | `Source/Driver/RTL8814Native.cpp` |
| Selectores 12-14 del user client | `Source/Driver/RTLUserClient.{hpp,cpp}` |
| Comprobación sin la herramienta | `Tools/test_uc.c`, `sim query/watch/inject` |
| Nodo, respuesta a NS y eco, pcapng | `Tools/awdlsim` |

El puerto también captura para una prueba con un par real (`sudo ./Tools/test_uc sim capture f.pcapng`,
[AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md)): el anillo recibe además lo que la radio oye, con el
RSSI, y el canal del puerto queda en 0, así que los datos usan sus reintentos normales. La única inyección es la
trama de su autotest.

Reglas del puerto:

- Sin el boot-arg, los selectores responden `kIOReturnUnsupported` y la copia en TX cuesta una lectura.
- Los selectores exigen root (`clientHasPrivilege` sobre la tarea dueña de la conexión).
- El cliente que habilita el puerto es su dueño. Solo él lee, inyecta y lo apaga, y su cierre lo apaga.
  Otro cliente (`test_uc status`, la app) no lo toca.
- El anillo y su cerrojo se reservan una vez, al arrancar el driver con el boot-arg, y no se liberan.
- Un relevo de la radio (sueño, reconexión USB) apaga el puerto y despierta al lector con
  `kIOReturnAborted`. La herramienta se vuelve a enganchar hasta cinco veces.

## 2. Despliegue

1. Compilar el driver (`./build.sh`) y la herramienta
   (`cd Tools/awdlsim && cargo build --release`, Rust ≥ 1.88). El AirLayer no cambia.
2. Desplegar el driver y volver a escribir los boot-args de la capa con la variable nueva:

   ```bash
   sudo ./Tools/phase5_deploy.sh "<EFI>" driver
   sudo RTL8814_AWDL=1 RTL8814_AWDL_TX=1 RTL8814_AWDL_SIM=1 ./Tools/phase5_deploy.sh "<EFI>" layer 2
   ```

   `RTL8814_LAYER_SRC`/`RTL8814_LAYER_MD5` apuntan a la capa instalada si no se recompila.
3. Reiniciar. `log show --last boot --predicate 'eventMessage CONTAINS "awdl sim"'` debe mostrar
   `awdl sim port ready (rtlawdl_sim)`.

## 3. Comprobación previa con `test_uc`

Con `awdl0` activa (ventana de AirDrop abierta, `ifconfig awdl0` con `status: active`) y en2 sin
asociar, o asociada en 6, 44 o 149:

```bash
sudo ./Tools/test_uc sim query
sudo ./Tools/test_uc sim watch 149 5
sudo ./Tools/test_uc sim inject 149 <captura owly discovery-ios26...pcap> 20
```

- `watch` lista las PSF y MIF de `awdl0`. El retardo (`lag`) es el tiempo entre la entrega a la radio
  y la lectura.
- `inject` envía PSF del iPhone de la captura con la MAC de origen cambiada (por defecto
  `02:00:00:aa:bb:01`). La capa registra `awdl: rx af 02:00:00:aa:bb:01` en sus primeras tramas, y el
  contador `injected` sube.

## 4. Pruebas con `awdlsim`

Para el registro detallado del manager se añaden a mano a los boot-args, en el `config.plist`:
`awdl_log_flags=0xffffffffffffffff awdl_log_flags_verbose=0xffffffffffffffff awdl_log_flags_config=1
wlan.debug.enable=0xff`. En 25G241 los lee `IO80211AWDLPeerManager::initWithInterface`
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.1). En macOS 10.13 la métrica de
elección acababa en 505-536 tras los 2 primeros segundos; el valor de 26.7.1 es el que imprime la
herramienta (`awdl0 ... m<métrica> c<contador>`).

En 25G241 la elección compara primero la métrica del root y luego su MAC; el contador solo cuenta entre
pares que siguen al mismo root ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.4).
Para S3a basta con `--metric` por encima de la métrica de `M`. El Mac descarta como «Legacy peer» a un
par cuyo root anuncia contador 0, así que `awdlsim` usa un contador aleatorio distinto de cero, y
`--counter 0` no se acepta.

`log stream --predicate 'sender == "IO80211Family" OR sender == "RTL8814AirLayer"'` en paralelo. El
nombre exacto del emisor de las líneas de elección es `?` hasta la primera ejecución. `N` es la MAC del
nodo y `M` la de `awdl0`; las dos aparecen en la salida de la herramienta.

| Prueba | Orden | Pasa cuando |
|---|---|---|
| S1, el nodo oye al Mac | `sudo Tools/awdlsim/target/release/awdlsim` | Antes de 5 s imprime `awdl0 is M` y `PeerUp M` |
| S2, el Mac oye al nodo | la misma | Antes de 10 s la elección del manager deja de mostrar `peers:0` y `N` aparece en sus registros. El evento 47 (`PEER_PRESENCE`, de `removePeer`) durante la prueba significa que el par se cayó: falla |
| S3a, el nodo es maestro | `--metric` mayor que la métrica de `M` (`awdl0 ... m<métrica>` en la salida) | La herramienta muestra `master node`, y el contador `aligned` de la línea `awdl: tx off` de la capa (al cerrar AirDrop) es mayor que 0 |
| S3b, el Mac es maestro | `--metric 1` | La herramienta muestra `master M h1` |
| S4, sincronía | S3a y S3b durante 60 s | `offset max` por debajo de 3072 µs (3 TU) con al menos 300 muestras, sin el aviso `lag > 1 TU` |
| S5, datos | `ping6 -c 20 -i 0.5 fe80::...%awdl0` (la dirección que imprime la herramienta) | Al menos 18 respuestas; suben `ns` y `echo` de la herramienta y `data queued/sent` de la capa |
| S6, tiempo compartido (opcional) | en2 asociada fuera de los canales sociales, `rtlawdl_mcc=1` | La herramienta sigue el canal de las PSF de `awdl0`, ningún `PeerDown` en 120 s, suben `inj_offch`. No se exige el límite de S4 |

Casos límite:

| Caso | Esperado |
|---|---|
| Sin el boot-arg | `unsupported`; la TX y RX AWDL reales sin cambios |
| Sin `sudo` | `not permitted` |
| Ctrl-C y `kill -9` con una lectura en curso | `sim query` muestra `enabled 0`; sin pánico |
| `test_uc status` durante una prueba | El puerto sigue habilitado |
| Segundo `awdlsim` | `another client owns the sim` |
| `kill -STOP` 1 s y `-CONT` | `ovr` sube, el nodo se recupera o da `PeerDown`/`PeerUp` |
| Desenchufar y enchufar, o dormir y despertar | `aborted` en la lectura, reenganche o salida tras 5 intentos; sin pánico |

## 5. Lo que no prueba

- El aire: RF, el ACK por hardware del puerto 1, la temporización real (la marca es la entrega a la
  radio, antes del USB) y el filtrado del chip.
- Los reintentos: las tramas unicast del Mac al nodo no reciben ACK. Las de datos salen con un solo intento
  mientras el puerto de simulación está activo, y las de gestión siguen con 6 (el descriptor MGNT no admite
  límite 0). Ocupan el aire y el pool de gestión durante S5.
- Si un dispositivo Apple acepta las tramas del Mac: el nodo se comporta como owly, cuyas tramas acepta
  el iPhone.
- AirDrop con `sharingd` en los dos extremos. §8 prueba solo el receptor, con un emisor propio y sin
  pasar por el aire, y §10 el emisor del Mac contra un receptor virtual.

Si el manager rechaza al nodo por un TLV que iOS envía y owly no (el 17, capacidades VHT,
[AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4), S2 falla sin que la radio tenga la
culpa. `--capture f.pcapng` guarda los dos sentidos para compararlos con
[AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md).

## 6. Disparo BLE de AirDrop sin iPhone (con el dongle Bluetooth)

`Tools/airdrop_beacon` es una app Android mínima, en Java y sin dependencias, que emite el anuncio que
un emisor de AirDrop envía con la hoja de compartir abierta: datos de fabricante `0x004C`, mensaje
`0x05`, longitud `0x12`, 8 bytes a cero, versión `0x01`, cuatro hashes de 2 bytes (los 16 bits altos
del SHA-256 de cada identificador dado) y un byte a cero
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.2). Cubre la mitad BLE que el par
virtual no prueba: que el `sharingd` del Mac vea un emisor y levante su servidor AirDrop.

Probada en el A53 (Android 16) el 2026-11-03:

- el registro muestra `start: 004c 0512000000000000000001973d00000000000000` y `advertising`, con
  `973d` igual a los 16 bits altos de `SHA-256("test@example.com")`;
- `dumpsys bluetooth_manager` lista el conjunto de la app: anuncio legacy, no conectable, intervalo de
  160 × 0,625 ms (100 ms) y una entrada de datos de fabricante.

Que un Mac lo reciba está sin probar `?`.

```bash
cd Tools/airdrop_beacon
JAVA_HOME=~/toolchains/jdk-17.0.21+9/Contents/Home ~/.gradle/wrapper/dists/gradle-9.6.1-bin/*/gradle-9.6.1/bin/gradle --offline assembleDebug
export ADB_LIBUSB=1
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell pm grant com.drogabox.airdropbeacon android.permission.BLUETOOTH_ADVERTISE
adb shell am start -n com.drogabox.airdropbeacon/.MainActivity --ez start true
adb shell am start -n com.drogabox.airdropbeacon/.MainActivity --ez stop true
```

`--es ids correo@x,+5491100000000` añade hasta cuatro hashes. Solo hacen falta para el modo «Solo
contactos»; cómo normaliza `sharingd` correos y teléfonos antes del hash está sin comprobar `?`.

Prueba con el UB400, con el Mac en AirDrop «Todos» y el Bluetooth activo:

1. `log stream --predicate 'process == "sharingd"'` en el Mac.
2. Iniciar el anuncio en el A53, a menos de un metro.
3. Pasa cuando `sharingd` registra los hashes recibidos (`handleFoundHashes:rssi:`, «In Everyone mode,
   start server», [AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md)) y `awdl0` pasa a
   activa sin abrir la ventana de AirDrop.
4. Con `awdlsim` corriendo a la vez, el nodo virtual ve las tramas que `awdl0` empieza a emitir.

También `log stream --predicate 'eventMessage CONTAINS "AWDL is"'` para ver si AWDL pasa a on, y
PacketLogger para comprobar que el anuncio llega al Bluetooth del Mac
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.5 y §12.8). Con AirDrop en «Todos»,
`sharingd` debería abrir TCP 8770 en `awdl0` (`sudo lsof -nP -iTCP:8770`, §12.7).

Resultado (2026-11-05, 25G241, UB400, con la preferencia de
[AWDL_AIRDROP_BLE_CONTROLADOR.md](AWDL_AIRDROP_BLE_CONTROLADOR.md) §8): con el anuncio del A53 como único
estímulo, sin búsqueda Bonjour ni ventana de AirDrop, el registro del kernel pasa a `AWDL ON` en 1 s,
`wifip2pd` publica `<id>._airdrop._tcp.local` y `sharingd` escucha en `*:8770`. Nada de eso ocurre antes
del anuncio. La recepción en el Mac se midió con `blescan` y el registro, sin PacketLogger.

## 7. Sincronía de la capa con el maestro

Desde esta revisión, `alignClock` calcula el inicio de la ranura con la hora de llegada de la trama
(tomada en el hilo de RX de la radio y guardada en los metadatos de la cola) menos el retardo de TX
que el maestro anuncia (`TTx,PHY − TTx,Target`, +8 y +12 de la cabecera fija). Es la ecuación 1 de
Stute et al. 2018 ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.1). Antes usaba
la hora a la que la thread call vaciaba la cola.

- La línea periódica `awdl: rx af ...` informa `drain lag max`, el mayor retardo entre llegada y vaciado
  de las últimas 256 AF. Las primeras `clock follows` informan `drain` y `sender delay`.
- Las medidas de S3a y S4 dicen si el cambio importa: un `drain` del orden de 1 TU o más explica parte
  del error que S4 mide.

## 8. AirDrop de punta a punta con el puente `utun` (S7)

`awdlsim --tun` crea una interfaz `utunN` (necesita root) con la dirección link-local del nodo y MTU
1428, y la conecta con el nodo:

- Lo que la pila IP del Mac envía por `utunN` desde la dirección del nodo entra al nodo como IPv6 de
  salida y viaja a `awdl0` como datos AWDL. Del ICMPv6 solo pasan eco y descubrimiento de vecinos, así
  que las solicitudes de router y los informes MLD de la pila no salen.
- Lo que el nodo entrega sube por `utunN`. `utun` no tiene NDP, así que el nodo sigue contestando él
  mismo las solicitudes de vecino de `awdl0`.

Al ver la primera PSF, la herramienta imprime las dos direcciones y la orden del emisor.

`awdl0` es `ND6ALT` (§9.3): las respuestas de `sharingd` hacia el nodo no salen sin una entrada de vecino.
Antes de S7, `sudo ndp -s <link-local del nodo>%awdl0 <MAC del nodo>` y `awdlsim --mac <MAC del nodo>
--tun`, con la misma MAC. Si `ndp -s` responde `No such process`, falta la ruta de `awdl0` (§9.3).

`Tools/awdlsim/airdrop_send.py` (Python, solo biblioteca estándar) habla con `sharingd` como lo haría
un emisor que no es de Apple ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §12.7):

- se ata a la dirección del nodo en `utunN` y abre TLS sin verificar el certificado contra
  `[link-local de awdl0]:8770`;
- envía `/Discover`, y en otra conexión `/Ask` (que espera a que el usuario acepte en el Mac) y
  `/Upload`;
- el `/Upload` lleva un archivo `cpio odc` (magic `070707`) comprimido con gzip, con las cabeceras HTTP
  `Content-Type: application/x-cpio` y `TotalBytes`.

`--selftest` comprueba el archivo cpio y el plist sin red. `ditto -x -z` extrae el archivo generado y
sirve para comprobar el formato sin pasar por `sharingd`.

| Prueba | Orden | Pasa cuando |
|---|---|---|
| S7a, descubrimiento | `airdrop_send.py ... --discover-only` con AirDrop del Mac en «Todos» | `/Discover -> 200` con `ReceiverComputerName` |
| S7b, envío | `airdrop_send.py ... archivo.txt`, aceptando en el Mac | `/Ask -> 200`, `/Upload -> 200` y el archivo en `~/Downloads` |

Si falla, la captura `--capture` y el registro de `sharingd` dicen en qué capa.

### 8.1. Resultado (2026-11-05)

S7a y S7b pasan en 25G241 con el UB400, `RTL8814Native` UUID `A40C375B`, `RTL8814AirLayer` 0.32.0 UUID
`36B31970` y AirDrop en «Todos». El anuncio del A53 enciende el servidor (§6).

| Paso | Medido |
|---|---|
| TLS | TLS 1.3, sin certificado de cliente. El nodo no anuncia AirDrop en sus TLV y el Mac acepta la conexión igual. |
| `/Discover` | 200 con `ReceiverComputerName`, `ReceiverMediaCapabilities` e `IsAirDropable: true`, sin `StatusCode`. |
| `/Ask` | 200 unos 3 s después, al aceptar el aviso, con `ReceiverComputerName`, `ReceiverModelName`, `SupportsContactExchange` e `IDSSessionID`. |
| `/Upload` | 200 en 1 s. |
| Archivo | `~/Downloads/prueba_airdrop_s7.txt`, 68 bytes, idéntico al enviado (`cmp`). |

El `/Upload` necesitó dos correcciones, cada una comprobada con un envío:

| Envío | `sharingd` |
|---|---|
| Sin `TotalBytes` | «Missing totalBytes / contentType» y cierra la conexión. El receptor (`sub_100514F90`) exige las dos cabeceras y el emisor de Apple (`sub_100515680`) las envía. |
| `newc` con `TotalBytes` | «Decompression failed», POSIX 5. `ditto -x -z` rechaza el mismo archivo con «bad file format». |
| `odc` con `TotalBytes` | «Decompression succeeded» y el archivo importado. |

`TotalBytes` llevó la longitud del archivo comprimido. Con un solo archivo pequeño, eso no dice si
`sharingd` lo usa para algo más que el progreso `?`.

El registro también dice «Missing preview data» (el `/Ask` no lleva miniatura) y «UPLOAD connection is not
active» al pasar de `/Ask` a `/Upload`. Los dos errores aparecen en el envío que funciona.

Alcance:

- Los paquetes entre `utunN` y `awdl0` usan el puerto de simulación, sin aire ni dispositivo Apple.
- en2 siguió asociado en el canal 161 (`AWDL ON:[infra(161) …]`). Se perdió la mayoría de las tramas del
  nodo por estar fuera de canal (`inj ok` 84 contra `offch` 2108 en 89 s), y aun así TCP entregó todo. Con
  un archivo grande el tiempo sería otro `?`.
- La preferencia BLE sigue vigente tras un reinicio.

Con la capa `1320BA53`, también con en2 en el 161, un archivo de 1 MiB llega idéntico: `/Upload -> 200` en
14 s, unos 75 KB/s. Durante la transferencia no hay `Peer Absence`; vuelven al quedar en reposo (§9.2).

Con la permanencia adaptativa (capa `9CA670C1`), en las mismas condiciones, el archivo de 1 MiB llega
idéntico en unos 5 s, unos 200 KB/s, y no hay `Peer Absence` en reposo salvo tras dos escaneos seguidos
(§9.2). Los tiempos del envío tienen resolución de 1 s.

Con la guarda de 3 TU y la MTU de 1428 en la capa (`8E4C4AE5`, [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §14 y §16), con en2 en el 149:

| Sentido | Medido | Referencia anterior |
|---|---|---|
| Del nodo al Mac, S7b con 1 MiB | Archivo idéntico, `/Upload -> 200` en 1 s | Unas 1,5 MB/s en TCP |
| Del Mac al nodo, TCP con 4 MiB | Datos idénticos, MSS 1356, 113 KB/s | 110-115 KB/s |

La guarda no empeora ninguno de los dos sentidos. En el segundo, la capa encoló unas 3150 tramas, con `bad` en 0 y 21 descartadas por anillo
lleno, que TCP reenvió. Un socket BSD corriente recibe un reset por lo que llega a `awdl0`, de modo que el lado del Mac necesita un
`NWListener` con `includePeerToPeer`, como el de `sharingd`.

Con la capa `43ACC5D0`, que alimenta los contadores del manager, los 4 MiB del Mac al nodo pasan a 507 KB/s y los 4 MiB por AirDrop hacia el Mac
llegan idénticos con `/Upload -> 200` en unos 2 s ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5).

## 9. Resultados en hardware (2026-11-03)

macOS 26.7.1 (25G241), T9UH, `RTL8814Native.kext` md5 `34128876`, `RTL8814AirLayer.kext` md5 `b2b805b4`,
boot-args `rtlawdl=1 rtlawdl_tx=1 rtlawdl_mcc=1 rtlawdl_sim=1`, sin Bluetooth.

### 9.1. Cómo se enciende AWDL sin AirDrop

Sin un controlador Bluetooth el ajuste de AirDrop queda en «Nadie» y no se puede cambiar. AWDL se
enciende igual con una búsqueda Bonjour con pares a pares (`Tools/awdlsim/p2p_browse.swift`): el
registro del kernel pasa a `AWDL ON:[infra(0) …] (6/149/149) [6 0 149 0 0 0 0 0 6 0 149 0 …]` en unos
4 s. Abrir la ventana de AirDrop del Finder también lo enciende
([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md) §6).

```bash
swift Tools/awdlsim/p2p_browse.swift 1800 &
```

En zsh, `log` es un comando interno: los registros se leen con `/usr/bin/log`.

### 9.2. Resultados

| Prueba | Resultado |
|---|---|
| S1 | Pasa. `awdl0 is …` y `PeerUp` en menos de 0,5 s. |
| S2 | Pasa con en2 sin asociar. El estado de AWDL muestra `peers:1` sin ausencias durante minutos. La capa recibió más de 9000 PSF y MIF del nodo, con un retardo de drenaje máximo de 312 µs; envió más de 5000 PSF sin fallos. |
| S2 con en2 en el canal 161 | Falla: el Mac solo pasa por 149 en 2 de 16 slots y llega tarde por el cambio de canal (2,5 ms de media, 4,4 ms de máximo). El 99 % de las tramas del nodo cae fuera de canal y el Mac publica `Peer Absence` cada ~15 s. Es el caso S6. |
| S2 con en2 en el canal 161 y el cambio anticipado (`RTL8814AirLayer` UUID `1320BA53`, 2026-11-05) | Sigue fallando. El cambio actúa (`early 986` en 986 ventanas, `mcc 1969 fail 0`, 2,45 ms de media y 3,9 ms de máximo), pero en reposo el 98,4 % de las tramas del nodo cae fuera de canal (`inj ok` +198 y `offch` +12233 en 515 s) y el Mac publica `Peer Absence` cada ~14,5 s. Llegar tarde no explica todo el problema: con 1 AW faltan tramas más de 6 s (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.1). |
| S2 con en2 en el canal 161 y la permanencia adaptativa (`RTL8814AirLayer` UUID `9CA670C1`, 2026-11-05) | Mejora mucho. Con un par oído en los últimos 30 s la radio se queda el slot entero (`dwell 4`). En 240 s de reposo entra el 12,9 % de las tramas del nodo (`inj ok` +738, `offch` +5006), sin ningún segundo sin tramas. Hubo 1 `Peer Absence` en 8 min 43 s, al terminar dos escaneos seguidos de 3,25 y 3,5 s. Sin el nodo vuelve a `dwell 1`. El coste en la BSS está en `AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.1. |
| S2 con en2 en el canal 161 y slots en el back-op del escaneo (driver UUID `A4BD0F2B`, capa `C9F97715`, 2026-11-05) | Pasa con el par virtual. Seis escaneos forzados de 6,0 a 9,3 s, sin ninguna `Peer Absence`. Durante ellos entra del 7,8 % al 14,2 % de las tramas del nodo, con 1 s seguido sin tramas como máximo (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.2). |
| S3b, el Mac es maestro | Pasa: `master <awdl0> h1`. El nodo sigue al Mac con un desfase de 0 a 1024 µs. |
| S4 con el Mac maestro | Pasa: 237 s, 2397 muestras válidas, desfase máximo 1024 µs (1 TU; 0 en 10 s y 1024 en 226 s). Un solo segundo con el aviso `lag > 1 TU`, retardo máximo 458 µs. |
| S3a, el nodo es maestro (`--metric 600`) | Pasa en el primer segundo: `master node h0 m600` y `awdl0 … follows node h1`. |
| S4 con el nodo maestro | Pasa: 127 s, más de 1300 muestras, desfase máximo 1024 µs, ningún aviso `lag > 1 TU`. Dos veces, a los ~37 s y ~98 s, awdl0 deja de enviar PSF unos 3 s (`rx 0`, `offch` sube 8); parece un escaneo de la infraestructura `?`. |
| S5, primera vez (Mac maestro) | 12 de 15 respuestas, RTT de 4 a 721 ms (media 311 ms). Hace falta una entrada de vecino estática (§9.3). La capa sube los 12 paquetes de datos del nodo (`first data packet up`). |
| S5 (nodo maestro) | Pasa: 20 de 20 respuestas, RTT de 13 a 478 ms (media 221 ms), `ping6 -i 1` (0,5 s pide root). |
| 1 MiB por TCP con presupuesto de aire por AW (driver UUID `2D84BB9B`, capa `8FF43136`, en2 en 149/80, 2026-11-05) | Subida de 110-115 KB/s y bajada de 1526-1957 KB/s, con 30 tramas por ventana en lugar de 12. Sin `Peer Absence`, y ping al router con máximo de 73 ms durante la subida (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.3). |

El RTT alto es el esperado: los datos solo salen en las ventanas del canal 149, 2 de cada 16 slots con la
BSS fuera de ese canal y 3 con la BSS en 149 (§8.3 del documento citado).

### 9.3. Neighbor Discovery en awdl0

`ifconfig -v awdl0` muestra `eflags=…ND6ALT,RESTRICTED_RECV,AWDL…`. Con `ND6ALT` el kernel no envía
Neighbor Solicitations: el contador `neighbor solicitation` de `netstat -s -p icmp6` no cambia
durante un `ping6`, y en la captura no sale ninguna trama de datos. La entrada del vecino la crea el
sistema cuando un servicio resuelve el par, no el `ping6`. Para S5:

```bash
sudo ndp -s fe80::<nodo>%awdl0 <MAC del nodo>
sudo Tools/awdlsim/target/release/awdlsim --mac <MAC del nodo>
```

`--mac` mantiene la MAC entre ejecuciones; sin ella cada `awdlsim` usa una MAC nueva y la entrada
estática queda vieja.

En un arranque awdl0 apareció sin la ruta `fe80::%awdl0/64` (`ping6: UDP connect: No route to
host`). Se añade con `sudo route -n add -inet6 -prefixlen 64 fe80::%awdl0 -interface awdl0`. En el
arranque siguiente estaba. Volvió a faltar el 2026-11-05: `ndp -s` da `No such process`, los SYN llegan al
Mac y su respuesta no sale. Con la ruta y la entrada de vecino, la conexión se establece sin reiniciar nada.
La causa es `?`.

### 9.4. Cambios hechos durante las pruebas

- **Capa, `p2pMulticastTx` y `freeMulticastPacket`.** `IO80211AWDLPeerManager::handleMulticastTimer`
  entrega el multicast guardado con `p2pMulticastTx` y libera con `freeMulticastPacket` lo que no se
  toma. Los cuerpos de `IO80211VirtualInterface` no hacen nada («Function not implemented»).
  `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::p2pMulticastTx` los pasa por su acción de envío; la capa
  los pasa por `txData`. En estas pruebas no se llamó (`mcast batches 0`).
- **Capa, caché de pares.** `IO80211PeerManager::preQueuePacket` descarta lo que va a un par que no
  está en la caché, y `IO80211P2PDataPathManager::addPeerToFWCache` solo lo marca si el ioctl 143
  (`setPEER_CACHE_CONTROL`: operación en +4, 0 añadir y 2 borrar; MAC en +8) tiene éxito. La capa
  responde éxito a 143 y a 164 (`setAWDL_PEER_TRAFFIC_REGISTRATION`), y 8 a 130
  (`getPEER_CACHE_MAXIMUM_SIZE`, el valor por defecto de la familia). En estas pruebas no se llamó a
  143. La capa tampoco confirmaba la operación con el evento 51 (§9.6).
- **`awdlsim`, TLV de capacidades HT.** `IO80211Peer::parseHTCapTLV` solo acepta el cuerpo completo
  del elemento HT de 802.11 (26 bytes) a partir del byte 2 del TLV 7; el de Crosswind tenía 8 bytes.
  Sin HT ni VHT, `IO80211AWDLPeer::hasHTorVHTCaps` es falso y `addPeerToFWCache` no cachea el par
  (`vendor/crosswind-awdl/PATCHES.md`).

Ninguno de los tres cambios es por sí solo la causa de que S5 pasara; el bloqueo era §9.3.

### 9.5. Pendiente

- S5 cada 0,5 s (con `sudo`) y el RTT: los datos solo salen en las ventanas del 149.
- El coste de la subida en la BSS con tiempo compartido: el router pasó a 149 y su canal no se puede fijar
  (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.3).
- Las 3 respuestas perdidas de la primera corrida de S5 son `?`.
- Los huecos de ~3 s sin PSF de awdl0 con el nodo como maestro.
- S6 (tiempo compartido) con en2 fuera de los canales sociales: falta probarlo con un par real por aire, también
  durante un escaneo (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.2).
- El UMI del Mac hacia un par real, y qué hace el par con él (§9.7).

### 9.6. UMI y caché de pares

Las capturas de AirDrop hacia el Mac tienen 0 tramas de acción unicast del Mac. Hay dos causas distintas:

- **Nada pide un UMI.** Con el Mac como receptor, `isInfraRealtimePacketThresholdAllowed` registra
  `noRegistrations:1` y `cachedPeerCount:0` en toda la corrida. Los datos de la capa tampoco fuerzan la caché:
  `txData` no pasa por `IO80211PeerManager::preQueuePacket`.
- **La capa no confirmaba la caché.** Tras el ioctl 143, IO80211 espera el evento 51
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.6). Sin él, el par no llega a cacheado y `handleUmiTimer` no le envía UMI;
  `handleAirDropTrafficReg` espera 3000 ms y `processUnicastAWSF`, 200 ms. Además, el contador de la caché (`CPC`)
  sube con cada alta y solo baja con la confirmación de un borrado, de modo que la caché se llena tras 8 altas.

`awdlsim --umi SEGUNDOS` envía al Mac una MIF unicast con el flag `0x1000` del TLV 12 y la opción 1.
`processUnicastAWSF` la trata como UMI recibido y cachea al par, lo que permite medir esta ruta sin un iPhone.

Una corrida de 60 s en el canal 149, con un UMI cada 5 s y el Mac en reposo:

| Registro de IO80211 | Sin evento 51 (`ded8cf01`) | Con evento 51 (`98eb3a8f`) |
|---|---|---|
| UMI recibidos | 12 | 11 |
| `Peer cache operation timeout` | 12 | 0 |
| `CPC` máximo | 8, y no baja al terminar | 1 |
| `No space to cache peer` | 4 | 0 |
| Altas enviadas / borrados confirmados | 8 / 0 | 11 / 11 |
| Del alta a su confirmación | nunca | 1 a 17 ms |

IO80211 borra al par por inactividad entre 3,4 y 3,6 s después de cada alta. Sin el evento, la baja no descuenta `CPC`.

Regresiones con `--tun --umi 5`, 6 corridas de 1 MiB por TCP en cada sentido, en el 149 y con ping al router:

| | Sin evento 51 ni UMI (3 corridas) | Con evento 51 y UMI (6 corridas) |
|---|---|---|
| Subida | 113-115 KB/s | 109-115 KB/s |
| Bajada | 991-6941 KB/s (una corrida de 0,1 s) | 1098-2004 KB/s |
| Ping durante la subida, media | 6,4-7 ms | 5,4-13,1 ms |
| Ping durante la subida, máximo | 85-87 ms | 71-77 ms en 5 corridas, 446 ms en 1 |
| Ausencias del par | 0 | 0 |
| AirDrop de 1 MiB | idéntico | idéntico |

En esas corridas hubo 79 altas, 79 borrados confirmados, 0 timeouts y `CPC` máximo 1. La corrida de 446 ms tuvo
2 pings por encima de 200 ms, no se repitió y su causa es `?`.

La opción 1 no pide respuesta: `processUnicastAWSF` solo responde si el par tiene `ivars+82`, y al recibir solo lo
activan las opciones de AirPlay, que arrancan otras máquinas de estado. Por eso esa captura sigue con 0 tramas
unicast del Mac. El UMI que envía el Mac se mide con un registro de tráfico (§9.7).

### 9.7. UMI enviado por el Mac

`Tools/awdl_traffic_reg.c` hace con el par el registro de tráfico de AirDrop que hace un envío real: el ioctl 164
(`APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION`, 184 bytes: versión 1 en +0, MAC en +4, longitud y nombre del
servicio `airdrop` en +12 y +16, activo en +168), lo mantiene unos segundos y lo quita. Un `ioctl` de socket sobre
awdl0 responde `EOPNOTSUPP`; el framework IO80211 ata awdl0 en su «compatibility mode» y `Apple80211RawSet` va por
XPC a `wifip2pd`, que exige el entitlement `com.apple.wifip2pd`. Con `amfi_get_out_of_my_way=0x1` basta una firma
ad hoc con ese entitlement (`Tools/awdl_traffic_reg.plist`), sin root. `com.apple.wlan.authentication` no hace
falta: con y sin él, el kernel contesta «Peer not found» cuando el nodo no está.

`handleAirDropTrafficReg` (`0xffffff80022087ac`) solo envía el UMI a un par con `IO80211Peer::is6ECapable`
(`ivars+1272`); a los demás les registra «AirDrop registration ENABLE for legacy peer … skipped UMI».
`parseAwdlDataPathTLVAndTakeAction` (`0xffffff80021b92a0`) toma ese bit del le16 que cierra el TLV 12 cuando el
flag `0x8000` está puesto: son flags extendidos, y `0x800` es la capacidad 6E. El Mac anuncia `0x2d00`; el nodo
anunciaba 0, y `awdlsim --6e` pone el `0x800`. Por código, con un par que no declare ese bit AirDrop no envía
ningún UMI, y no es un fallo de la capa; qué modelos lo declaran no está medido.

Con el par cacheado, `sendRealTimeUMI` avisa «Not safe to send UMI», encola al par y `handleUmiTimer` envía el
ioctl 149 unicast en la siguiente ventana del 149 (slot 10), entre 8 y 433 ms después. Tanto el alta como la baja
del registro envían uno. El 149 unicast trae su propia plantilla, sin los TLV 2, 6, 16 y 21 de servicios y con la
opción 1 en el TLV 12. La capa `98eb3a8f` la guardaba sobre la plantilla de las MIF de difusión, que salieron sin
servicios hasta el siguiente 149 o 225 de difusión. La capa `240c4d75` guarda la plantilla de cada UMI pendiente con
su destino y no toca la de difusión.

Dos corridas de 60 s en el 149 con `--6e` y un registro de 15 s, 15 s después de arrancar el nodo:

| | Capa `98eb3a8f` | Capa `240c4d75` |
|---|---|---|
| «legacy peer» | no | no |
| Del alta a su confirmación (alta / baja del registro) | 6 / 7 ms | 9 / 4 ms |
| «sending UMI#0» en el registro de IO80211 | 2 | 2 |
| MIF unicast del Mac en la captura, 282 bytes, opción 1 | 2 | 1 (el segundo UMI salió 6 s después de acabar el nodo) |
| MIF de difusión de 282 bytes, sin servicios | 14, durante ~1 s tras cada UMI | 0 |
| MIF de difusión de 958 bytes | 197 | 170 |

IO80211 borra al par por inactividad 3,8 a 3,9 s después de cada alta, igual que con los UMI recibidos.

## 10. El Mac como emisor: receptor de AirDrop virtual (S8)

§8 prueba el receptor del Mac con un emisor propio. Esta prueba es la inversa: el nodo se anuncia como receptor de
AirDrop y el `sharingd` del Mac lo descubre, lo resuelve y se conecta a él por `awdl0`.

### 10.1. Cómo anuncia un iPhone su receptor

Medido en la captura del 2026-11-08 (iOS 27). El iPhone no usa mDNS multicast para esto: lleva el servicio dentro de
sus MIF, al final de la trama, en TLV de respuesta de servicio (tipo 2), y marca los nombres en el mapa de bits del TLV
de parámetros de servicio (tipo 6), que va en PSF y MIF.

Cada TLV 2 es un registro DNS: largo del nombre más el tipo (u16), nombre comprimido, tipo (u8), largo de los datos
(u16), dos bytes a cero y los datos. Códigos de compresión: `c0 07` es `_airdrop._tcp.local`, `c0 0a` es `_tcp.local`,
`c0 0c` es `local` y `c0 00` cierra un nombre.

| Registro | Nombre | Datos |
|---|---|---|
| PTR (12) | `c0 07` | `<id>` + `c0 00` |
| SRV (33) | `<id>` + `c0 07` | prioridad 0, peso 0, puerto 8770, `<host>` + `c0 0c` |
| TXT (16) | `<id>` + `c0 07` | `flags=111611` |

`<id>` son 12 dígitos hexadecimales al azar y `<host>` es el mismo nombre del TLV ARPA (16). El TLV 6 lleva tres bytes a
cero, un indicador de actualización (u16), una máscara de 32 bits con los bytes no nulos de un filtro de 256 bits y esos
bytes. Con el receptor de AirDrop activo aparecen dos bits más: el byte 19, bit 6, en todas las instancias
(`_airdrop._tcp.local`), y otro que cambia con el `<id>`. La función de hash no está identificada `?`, así que el nodo
usa un `<id>` que usó el iPhone, cuyo bit (byte 25, bit 6) se leyó en la captura.

El iPhone y el Mac anuncian además, siempre, registros de `_applicationservicepairing._tcp` y `_appsvcprepair._tcp` con
TXT `at=`, `sid=` y `sn=com.apple.sharingd.AirDrop`, y el Mac uno de `_asquic._udp`: es el camino nuevo, por Rapport.

### 10.2. El filtro de `sharingd`

`sharingd` (25G241) abre dos buscadores al mostrar la hoja de AirDrop: uno de servicio de aplicación (Rapport) y uno
Bonjour de `_airdrop._tcp` con TXT. Cada resultado Bonjour pasa por `sub_1003F0B00` (`SDAirDropBonjourBrowser`), que lee
las `flags` del TXT:

- con el filtro de dispositivos de «nearby sharing» activo, exige el bit `0x4000` («Ignoring … since it is missing
  nearby sharing flag»);
- si no, con `SFAirDropUserDefaults.pinPairingAirDrop` activo y el bit `0x10000` en las flags, descarta el resultado:
  «Dropping legacy endpoint … since the device supports PIN pair, so it should be discovered over Rapport».

Las flags del iPhone, 111611 (`0x1B3FB`), llevan el bit `0x10000`. Un iPhone con iOS 27 no entra entonces a la hoja del
Mac por `_airdrop._tcp`: el Mac lo espera por Rapport. Solo un resultado que pasa el filtro recibe la conexión de
descubrimiento (`SDAirDropBonjourDiscoverConnection`): TLS al puerto del SRV y `POST /Discover`.

### 10.3. Montaje

```bash
sudo Tools/awdlsim/target/release/awdlsim --mac <MAC del nodo> --metric 1 --seconds 600 --6e --addba --tun --airdrop
python3 -I Tools/awdlsim/airdrop_recv.py --iface <utun> --addr <link-local del nodo>
```

`--airdrop` añade a los MIF del nodo los tres TLV 2 y los dos bits del TLV 6, con `flags=46075`: las del iPhone sin el
bit `0x10000` (`--airdrop-flags` pone otras). Los tres registros con las flags del iPhone salen byte a byte iguales a los
de la captura (prueba en `Tools/awdlsim/src/svc.rs`). `airdrop_recv.py` escucha en la dirección del nodo sobre el
`utun`: TLS con certificado autofirmado, `/Discover`, `/Ask` (acepta) y `/Upload` (cuenta los bytes), y un respondedor
mDNS para PTR, SRV, TXT y AAAA. El TXT del respondedor lleva `flags=46075` salvo que se pase `--flags`: con otro valor
en `--airdrop-flags` del nodo, el receptor necesita el mismo en `--flags`, o el Mac ve dos TXT distintos (§10.6).

### 10.4. Resultado (2026-11-09)

Capa `8FB89DDD`, driver `9B662F1B`, radio compartida con un BSS en el 153.

| Paso | Con `flags=111611` | Con `flags=46075` |
|---|---|---|
| `wifip2pd`: `Received a new discovery result <MAC> [<id>._airdrop._tcp.local] at -40 dBm` | sí, al abrir la hoja | sí |
| `mDNSResponder`: resultado PTR y TXT por D2D (`D2DServiceFound`) | sí, en 2 ms | sí |
| `sharingd`: conexión a 8770 | no, en 2 minutos | sí |
| Receptor: `TLS TLSv1.3`, `POST /Discover` (3918 bytes, claves `DeviceSupportFlags`, `SenderRecordData`) | — | sí, `200` |
| `sharingd`: `Got DISCOVER response`, `Adding discovered endpoint` | — | sí |
| Hoja de AirDrop del Mac | «Ninguna persona encontrada» | «awdlsim» en «Otros dispositivos» |

El PTR y el TXT los resuelve `wifip2pd` desde los TLV 2, sin tráfico. El Mac manda además preguntas mDNS multicast por
`awdl0` (188 bytes, 3 preguntas), que llegan al nodo; la respuesta del respondedor volvió y `mDNSResponder` la registró.

Queda comprobado en el Mac, con la capa: lectura de los TLV 2 de un par, publicación del resultado, resolución del
nombre y la dirección, conexión TCP y TLS por `awdl0` y el intercambio `/Discover`. No prueba el camino por Rapport, que
es el que usará un iPhone con iOS 27, ni nada por el aire: el nodo no tiene radio.

**Envío de un archivo.** Al soltar un video sobre «awdlsim» el Mac abrió otra conexión y mandó `POST /Hello` (141
bytes: `id`, `featureFlags` 3, `contextType`), `POST /Ask` (9383 bytes, con `SenderIdentityAuthTag` y `Files`) y
`POST /Upload` con cuerpo `chunked`. Un `/Hello` contestado con 404 no detiene al emisor («Receiver does not support
HELLO, skipping»). La familia pasó a `Data` con `handleAirDropTrafficReg: AirDrop registration ENABLE` y la capa llevó la
tasa a 54 Mb/s por la señal. Los datos entraron a ritmo constante, 1 MB cada 4,2 s (unos 240 kB/s), con la radio
compartida con el BSS: 10 MB en 42 s. Después del décimo megabyte no llegó más nada en 50 s, aunque `sharingd` anotó
`Compression finished` y `Finished sending UPLOAD` 11 y 17 s más tarde; a los 2 s de no ver tráfico la familia volvió a
`Idle` (`monitorAWDLDataMode: Tx:0 Rx:0`). La corrida del nodo terminó ahí por su límite de 600 s, así que la subida
no se completó y el motivo del corte queda sin identificar `?`. Un primer intento se perdió porque el receptor se
reinició durante la subida. Para repetirlo: salida del nodo guardada en un archivo, `--seconds` largo, un archivo de
1 o 2 MB primero y `en2` fuera de la red del 153.

**Envío completo (2026-11-09, 21:58).** En la corrida siguiente, con la salida del nodo guardada y 1800 s de límite, el
Mac retomó solo el envío del mismo video en cuanto el receptor volvió a escuchar: `/Hello` (200), `/Ask` (200) y
`/Upload` de 10 961 703 bytes en 49,7 s, 220 kB/s, con `200` al final y `Received UPLOAD response` en `sharingd`. La
radio seguía compartida con el BSS, ahora en el 157. El corte de la corrida anterior, en el último megabyte del mismo
archivo, no se repitió y su causa sigue sin identificar `?`. Queda comprobado el AirDrop del Mac hacia un receptor por
el camino clásico de punta a punta: descubrimiento, `/Discover`, `/Hello`, `/Ask` y `/Upload` sobre `awdl0`. La
velocidad con la radio sin compartir se midió después: 1527 kB/s con `en2` desasociado
([AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md) §5).

### 10.5. El camino por Rapport (2026-11-09, 22:14 a 22:22)

Con la hoja de AirDrop abierta, `rapportd` (25G241) abre dos buscadores Bonjour de `RPPairing`:
`_applicationServicePairing._tcp` y `_appSvcPrePair._tcp`. Junto a sus registros de AirDrop, el iPhone de la captura
del 2026-11-08 anunciaba en sus MIF, en TLV 2, un PTR, un SRV y un TXT de cada uno:

| Registro | Nombre | Datos |
|---|---|---|
| PTR | `_applicationservicepairing` + `c0 0a` | `iPhone` + `c0 00` |
| SRV | `iphone._applicationservicepairing` + `c0 0a` | puerto 5696x, `<host>` + `c0 0c` |
| TXT | igual | `sn=com.apple.sharingd.AirDrop`, `at=<12 hex>`, `sid=<UUID>` |
| PTR, SRV, TXT | lo mismo con `_appsvcprepair` | otro puerto; el TXT añade `dnm=iPhone` |

`c0 0a` es `_tcp.local`. El nombre de la instancia va tal cual en el PTR y en minúsculas en el SRV y el TXT. El iPhone
cambiaba `at` y `sid` varias veces por minuto. En el mapa de bits del TLV 6, estos registros encienden el byte 4 bit 0 y el
byte 5 bit 5, que el Mac también enciende para sus propios registros (los dos tipos de servicio), y el byte 2 bit 3 y el
byte 10 bit 5 (la instancia `iPhone`).

`awdlsim --pairing` añade los seis registros, con la instancia `iPhone` para que valgan esos bits, puertos 56962 y
56963, y un `at` y un `sid` al azar que cambian cada 30 s. Con las flags del iPhone en el TXT de AirDrop:

```bash
sudo Tools/awdlsim/target/release/awdlsim --mac <MAC del nodo> --metric 1 --seconds 1800 --6e --addba --tun --airdrop-flags 111611 --pairing
python3 -I Tools/awdlsim/airdrop_recv.py --iface <utun> --addr <link-local del nodo> --pairing
```

El receptor puede arrancar antes que el nodo: espera al `utun` y a la dirección.

| Paso | Resultado |
|---|---|
| `wifip2pd`: `Received a new discovery result … [iPhone._applicationservicepairing._tcp.local]` y `[iPhone._appsvcprepair._tcp.local]` | sí, junto al de `_airdrop._tcp` |
| `rapportd`: `browse_result_changed … @[awdl0]` en los dos buscadores | sí |
| `rapportd`: `Attempting discovery with`, `Starting PairVerify with` | sí |
| `rapportd`: resolución del host en `awdl0` y TCP al puerto del SRV de `_appsvcprepair` | sí |
| `rapportd`: TLS `Client handshake started` (TLS 1.2 a 1.3) | sí: cuatro ClientHello de 1496 bytes en el nodo |
| PairVerify | no: el nodo no contesta el TLS |

`rapportd` compara cada `at` con su propia identidad (`verifyAuthTag:data:type:` con tipo 6, `sub_1000DB590`) y salta
el PairVerify si coincide («Skipping PairVerify for our own bonjour device»). Un resultado que cambia sin cambiar el `at`
no se vuelve a probar («Changed endpoint ignored (same authTag)»), así que tras un `Connection refused` el siguiente
intento llegó recién con el `at` nuevo. Abrir y cerrar la hoja no reinició la búsqueda.

En la misma corrida, con las flags 111611, `sharingd` registró «Dropping legacy endpoint» y aun así, 5 s después, mandó
`POST /Discover` al puerto 8770 del nodo («Creating service … Sending DISCOVER request»), y el nodo apareció en la hoja.
La causa no son los registros de Rapport: el respondedor mDNS de `airdrop_recv.py` contestaba entonces siempre con
`flags=46075`, y con ese TXT el Mac manda el `/Discover` aunque los TLV 2 del nodo lleven 111611 (§10.6, pruebas B y C).

Queda comprobado, en el Mac y con la capa, todo el camino por Rapport que no depende de la identidad de Apple:
publicación de los registros, los dos buscadores de `rapportd`, la resolución y el TCP con TLS por `awdl0`. Lo que falta
(PairVerify, el PIN y el cifrado) lo hacen el Mac y el iPhone entre ellos, y no lo toca ni el driver ni la capa.
`SFAirDropUserDefaults.pinPairingAirDrop`, la condición del filtro de §10.2, es una propiedad `AirDropFeatureFlag` de
`Sharing.framework` creada con la clave `PINPairingAirDrop` (leído con `dyld_info -disassemble`: la librería solo existe
dentro de la caché compartida de dyld y no hay una base de IDA para ella). `/System/Library/FeatureFlags/Domain/Sharing.plist`
trae esa clave como `FeatureComplete`. `sharingd` consulta además la clave en las preferencias del usuario al filtrar
los resultados Bonjour; el registro de depuración de CFPreferences nombra el dominio:
«found no value for key PINPairingAirDrop in CFPrefsSearchListSource (Domain: com.apple.Sharing …)».
Pruebas con capa `207B418D`, driver `9B662F1B`, `en2` desasociado y el nodo con `--airdrop-flags 111611` sin
`--pairing`. En cada una se reinició `sharingd`, se abrió la ventana de AirDrop de Finder y se leyó 30 s de
`log stream --info --debug` del proceso (el mensaje «Dropping legacy endpoint» es de nivel info y no queda guardado):
| Prueba | Ajuste | TXT del respondedor | «Dropping legacy endpoint» | «Sending DISCOVER request» | «completed discovery» |
|---|---|---|---|---|---|
| A | de fábrica | sin receptor | 1 | 0 | 0 |
| B | `com.apple.sharingd PINPairingAirDrop` = NO | `flags=46075` | 2 | 1 | 1 |
| C | de fábrica | `flags=46075` | 2 | 1 | 1 |
| D | de fábrica | `flags=111611` | 1 | 0 | 0 |
| E | `com.apple.sharingd PINPairingAirDrop` = NO | `flags=111611` | 1 | 0 | 0 |
| F | `com.apple.Sharing PINPairingAirDrop` = NO | `flags=111611` | 0 | 1 | 1 |
- B y C son iguales: el dominio `com.apple.sharingd` no cuenta, y el `/Discover` de esas dos pruebas se debe al TXT
  `flags=46075` del respondedor. Explica el `/Discover` de §10.5.
- D contra F, con todo lo demás igual: con el ajuste apagado en `com.apple.Sharing`, `sharingd` deja de descartar el
  resultado con el bit `0x10000`, manda `POST /Discover` (el receptor contestó `200`) y registra
  `Got DISCOVER response`, `Bonjour service … completed discovery` y `Endpoint discovered`.
```bash
defaults write com.apple.Sharing PINPairingAirDrop -bool NO
killall sharingd
```
Para volver al valor de fábrica: `defaults delete com.apple.Sharing PINPairingAirDrop` y `killall sharingd`. El Mac
quedó con el valor de fábrica después de las pruebas.
Con el ajuste apagado el Mac trata a un equipo que anuncia el emparejamiento por PIN como a un receptor clásico: lo
busca por `_airdrop._tcp` y le habla por el puerto del SRV, sin pasar por Rapport ni por el PIN. Sirve de alternativa
si un iPhone con iOS 27 no aparece en la hoja del Mac por el camino de Rapport.
**Sin reiniciar y con un envío (2026-11-10, 00:06).** Con `sharingd` ya en marcha, el `defaults write` solo bastó: al
abrir la ventana de AirDrop salió «Sending DISCOVER request» sin ningún «Dropping legacy endpoint». Un archivo soltado
sobre el nodo, que seguía anunciando `flags=111611`, pasó completo: `/Hello`, `/Ask` y `/Upload` de 31 459 097 bytes
en 20,15 s (1561 kB/s) con `200`, y `Received UPLOAD response` en `sharingd`. Ningún mensaje de emparejamiento por PIN
en el registro durante el envío.
Sin comprobar `?`: que un iPhone conteste ese `/Discover` y acepte el envío, y si el ajuste cambia lo que el Mac
anuncia o acepta como receptor.
