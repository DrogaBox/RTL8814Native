# AWDL: prueba de recepción con un iPhone de visita

Guion corto para aprovechar la visita de alguien con un iPhone. El objetivo es comprobar la primera pieza
que necesita un par real: que los action frames AWDL del iPhone llegan al chip, pasan por el driver y
llegan a `IO80211AWDLPeerManager`, y que el manager deja de elegir en solitario (`peers:0`). No se
transmite nada salvo que se arranque con `rtlawdl_tx=1` (§6); con TX, la segunda comprobación es que el
iPhone registre este Mac como par AWDL.

Lo que no se ha visto en hardware va con `?`.

## 1. Qué hay en el código

| Pieza | Dónde | Commit |
|---|---|---|
| Desvío en `validate_recv_frame`: toda trama con A3 = `00:25:00:FF:94:73` va al gancho AWDL del ABI de radio, con RSSI (`phy_info.recv_signal_power`) y canal de operación; si hay gancho, el driver la descarta después | `Source/rtl8814au/core/rtw_recv.c`, `Source/Driver/RTLEthernetAdapter.cpp`, `Source/Driver/VendorGlue.cpp` | `ccc4122` |
| Función `rtlRadioAwdlHook` del ABI (sin cambio de versión: las compilaciones anteriores responden `kIOReturnUnsupported`) | `Source/Driver/RTLRadioABI.h`, `Source/Driver/RTL8814Native.cpp` | `ccc4122` |
| `awdl0` engancha la RX al habilitarse y la suelta al deshabilitarse; copia cada trama fuera del hilo de RX y entrega los action frames AWDL a `IO80211VirtualInterface::bpfTapInput(mbuf, 12, hdr, 72)` en la work queue | `Source/AirLayer/AWDLInterface.{h,cpp}` | `3b53d2a` |
| Función `rtlRadioTx80211` del ABI: una trama de gestión cruda por la cola de gestión, en el canal de operación, con el número de secuencia del driver | `Source/Driver/AWDLTx.c` | `4ac3340` |
| Emisor de PSF (cada 110 TU) y MIF (mitad de cada franja) en `awdl0`, con la plantilla del 151 y los TLVs 4, 5 y 18 generados por la capa; reloj de AW propio que se alinea con el TLV 4 de un par que se anuncia como maestro. Solo con `rtlawdl_tx=1` | `Source/AirLayer/AWDLInterface.cpp` | `cceb743` |

Sin `rtlawdl=1` no existe `awdl0`, nadie engancha la RX y el driver se comporta como antes (una
comparación de 6 bytes por trama). Con `rtlawdl=1`, el boot-arg `rtlawdl_rx=0` deja la RX AWDL apagada.

Las tramas de datos AWDL solo se cuentan; su entrega a la `RxCompletionQueue` de `awdl0` no está escrita.

## 2. Preparación, antes de la visita

1. Compilar las dos kexts desde el árbol actual (`./build.sh` y `./build_airlayer.sh`) y anotar los md5 de
   `Build/RTL8814Native.kext/Contents/MacOS/RTL8814Native` y
   `Build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer`.
2. Montar el EFI interno y desplegar las dos, con `rtlawdl=1` (y `rtlawdl_tx=1` para la TX del §6):

   ```bash
   sudo ./Tools/phase5_deploy.sh "<EFI>" driver
   sudo RTL8814_AWDL=1 RTL8814_AWDL_TX=1 ./Tools/phase5_deploy.sh "<EFI>" layer 2
   ```

3. Reiniciar y comprobar en el registro que la RX AWDL quedó enganchada:

   ```bash
   log show --last 10m --predicate 'eventMessage CONTAINS "awdl: rx"'
   ```

   Debe aparecer `awdl: rx attached, af 0 data 0 other 0 dropped 0` tras `awdl: enable`. Si sale
   `awdl: rx hook refused 0xe00002c7`, el `RTL8814Native` cargado es anterior a `ccc4122`.

## 3. Canal

AWDL usa los canales sociales 6 (2,4 GHz) y 44 o 149 (5 GHz)
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md)). El chip escucha en un solo canal:

- **asociado**: el de la red. Sirve si su canal **primario** es 6, 44 o 149. El 2026-10-30 `en2` estaba
  en el 153 a 80 MHz: el bloque incluye el 149, pero el primario es el 153 y las tramas AWDL van a
  20 MHz por el 149 ([AWDL_WINDOW_TIMER.md](AWDL_WINDOW_TIMER.md) §8). Fuera de 6, 44 y 149 la capa no
  transmite ni abre ventanas y lo registra una vez (`radio on channel N, not a social channel`);
- **sin asociación** (`07a41e8`, `ab11c69`): con la TX activa, la capa fija la radio a 20 MHz con
  `rtlRadioSetChannel` y sirve ese canal en cuanto la radio está en él (registro
  `awdl: parking the radio on channel C (was N)`). El canal es el social (6, 44, 149) que más
  franjas ocupa en la secuencia 129 del manager; antes de la primera 129, en un empate o si la 129 no
  trae ninguno, el 149. Sin asociación la secuencia por defecto del manager pone el 6 en las franjas
  0 y 8 ([AWDL_SIN_ASOCIACION_INFRA.md](AWDL_SIN_ASOCIACION_INFRA.md) §3), así que la radio puede
  acabar en el 6. El boot-arg `rtlawdl_ch=6` o `rtlawdl_ch=44` cambia el canal preferido; otro valor
  desactiva la fijación. Tras una desconexión la petición se repite como
  mucho una vez por segundo, y un escaneo vuelve a ese canal al terminar. Para la prueba basta con
  desconectar el Wi-Fi del Mac de la red del router y quitarle la conexión automática, sin tocar el
  router.

Si los peer manager y `wifip2pd` mantienen AWDL activo con la infraestructura sin asociar es `?`. En qué
canal pone un iPhone su AWDL cuando está asociado a otro canal de infraestructura también es `?`; si no
llega nada en 149, repetir con `rtlawdl_ch=6`.

## 4. Durante la visita

1. En el iPhone: desbloqueado, cerca del Mac, Wi-Fi activado, AirDrop en «Todos durante 10 minutos» y la
   hoja de compartir abierta en AirDrop (eso mantiene AWDL activo `?`).
2. En el Mac, abrir AirDrop en el Finder para que `wifip2pd` pida AWDL (petición 120).
3. Dejarlo un minuto y recoger:

   ```bash
   log show --last 5m --predicate 'eventMessage CONTAINS "awdl: rx"' > ~/Desktop/awdl_rx.txt
   ```

   ```bash
   log show --last 5m --predicate 'eventMessage CONTAINS "runElection" OR eventMessage CONTAINS "peers:"' > ~/Desktop/awdl_election.txt
   ```

## 5. Cómo leer el resultado

| Lo que se ve | Lectura | Siguiente paso |
|---|---|---|
| Ninguna línea `awdl: rx af` | El chip no oye AWDL en ese canal, o el filtro de RX lo tira antes del software | Repetir en otro canal social; si sigue sin nada, revisar `RXFLTMAP0`/`RCR` (punto 20 del índice) |
| `awdl: rx af <MAC> rssi <dBm> ch <n>` con la MAC AWDL del iPhone | RX AWDL confirmada de punta a punta en el driver | Mirar la elección |
| RSSI por debajo de −70 | El manager descarta el AF por el umbral de borde ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §1.3) | Acercar el iPhone |
| `runElection` con `peers:1` o más | El manager creó el par a partir del AF inyectado | Con eso queda validada la inyección; lo siguiente es la TX |
| Sigue `peers:0` con AF recibidos y RSSI por encima de −70 | El manager descarta el AF por otro motivo (`BAD OUI in AF`, cabecera) | Buscar `BAD OUI` e `Invalid frame` en el registro |

`awdl: rx other` y `awdl: rx data` también cuentan: son tramas con el BSSID AWDL que no son action frames
AWDL (por ejemplo, datos entre el iPhone y otro dispositivo Apple).

## 6. TX: que el iPhone vea este Mac

Con `rtlawdl_tx=1`, mientras `awdl0` está habilitada y la radio no escanea, `awdl0` emite por broadcast
un PSF cada 110 TU y un MIF por franja, en el canal de la red asociada o, sin asociación, en el canal
fijado del §3. Cada trama lleva la plantilla de sincronía del kernel (ioctl 151) y los TLVs 4, 5 y 18 que
genera la capa, anunciando ese canal en las 16 franjas. Sin plantilla (el manager aún no envió el 151),
mientras se une a una red o hasta que la radio llega al canal fijado no sale nada. Las tramas usan la
tasa de gestión del driver (OFDM 6 Mb/s cuando la radio está fijada) y la categoría 127, que no es robusta, así que con PMF
activo no se les añade BIP (`CATEGORY_IS_NON_ROBUST`, `Source/rtl8814au/include/ieee80211.h:1531`).

Comprobación sin iPhone, para descartar problemas de estabilidad antes de la visita:

```bash
log show --last 10m --predicate 'eventMessage CONTAINS "awdl: tx" OR eventMessage CONTAINS "sync frame template"' > ~/Desktop/awdl_tx.txt
```

Debe aparecer `awdl: sync frame template N bytes`, `awdl: tx on` y, cada 1024 tramas, una línea
`awdl: tx psf ... mif ... fail ...`. Un `fail` creciente con `-> 0xe00002c7` indica un `RTL8814Native`
anterior a `4ac3340`.

Con el iPhone presente:

| Lo que se ve | Lectura |
|---|---|
| `awdl: clock follows <MAC> aw ... next slot ... TU` | El reloj propio se alineó con el iPhone como maestro |
| PSF/MIF con la MAC de `awdl0` en una captura en modo monitor desde otro equipo | La TX llega al aire (`?`: sin captura, solo se ve por el efecto en el iPhone) |

Con la TX activa también corren (`7e454be`, `7c95ead`, `62a2c49`):
- **ventanas**: en cada franja que la secuencia del manager (129) pone en el canal de la radio, la capa
  publica el evento 43 (cierra la anterior y deja correr la elección) y el 41 (abre la nueva), según
  [AWDL_WINDOW_TIMER.md](AWDL_WINDOW_TIMER.md). En el registro: `awdl: channel sequence 16 slots: ...`
  una vez y `awdl: window aw N slot S ch C` en las primeras ventanas. Las franjas no servidas se anuncian
  con canal 0;
- **puerto 1**: toma la MAC de `awdl0` y el BSSID AWDL en modo ad-hoc, para que el hardware confirme
  (ACK) los unicast del iPhone a `awdl0`; mientras está activo el driver acepta datos de otros BSSID
  (`RCR_CBSSID_DATA` a 0, como con AP o mesh). Si se rechaza, aparece `awdl: port 1 refused`.

Limitaciones conocidas:
- el MIF no lleva el TLV 16 (ARPA, nombre de host): si el iPhone lo exige para listar el par es `?`;
- las tramas de datos AWDL recibidas solo se cuentan: no llegan a `awdl0`;
- no hay evento 42 (extensiones de ventana);
- si el tipo ad-hoc del puerto 1 hace emitir algo por sí mismo es `?`
  ([AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §5);
- AirDrop además necesita Bluetooth, que este equipo no tiene operativo.
