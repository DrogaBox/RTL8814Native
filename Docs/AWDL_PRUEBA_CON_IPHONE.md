# AWDL: prueba de recepción con un iPhone de visita

Guion corto para aprovechar la visita de alguien con un iPhone. El objetivo es comprobar la primera pieza
que necesita un par real: que los action frames AWDL del iPhone llegan al chip, pasan por el driver y
llegan a `IO80211AWDLPeerManager`, y que el manager deja de elegir en solitario (`peers:0`). No se
transmite nada: la TX AWDL aún no existe, así que el iPhone no verá a este Mac.

Lo que no se ha visto en hardware va con `?`.

## 1. Qué hay en el código

| Pieza | Dónde | Commit |
|---|---|---|
| Desvío en `validate_recv_frame`: toda trama con A3 = `00:25:00:FF:94:73` va al gancho AWDL del ABI de radio, con RSSI (`phy_info.recv_signal_power`) y canal de operación; si hay gancho, el driver la descarta después | `Source/rtl8814au/core/rtw_recv.c`, `Source/Driver/RTLEthernetAdapter.cpp`, `Source/Driver/VendorGlue.cpp` | `ccc4122` |
| Función `rtlRadioAwdlHook` del ABI (sin cambio de versión: las compilaciones anteriores responden `kIOReturnUnsupported`) | `Source/Driver/RTLRadioABI.h`, `Source/Driver/RTL8814Native.cpp` | `ccc4122` |
| `awdl0` engancha la RX al habilitarse y la suelta al deshabilitarse; copia cada trama fuera del hilo de RX y entrega los action frames AWDL a `IO80211VirtualInterface::bpfTapInput(mbuf, 12, hdr, 72)` en la work queue | `Source/AirLayer/AWDLInterface.{h,cpp}` | `3b53d2a` |

Sin `rtlawdl=1` no existe `awdl0`, nadie engancha la RX y el driver se comporta como antes (una
comparación de 6 bytes por trama). Con `rtlawdl=1`, el boot-arg `rtlawdl_rx=0` deja la RX AWDL apagada.

Las tramas de datos AWDL solo se cuentan; su entrega a la `RxCompletionQueue` de `awdl0` no está escrita.

## 2. Preparación, antes de la visita

1. Compilar las dos kexts desde el árbol actual (`./build.sh` y `./build_airlayer.sh`) y anotar los md5 de
   `Build/RTL8814Native.kext/Contents/MacOS/RTL8814Native` y
   `Build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer`.
2. Montar el EFI interno y desplegar las dos, con `rtlawdl=1`:

   ```bash
   sudo ./Tools/phase5_deploy.sh "<EFI>" driver
   sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh "<EFI>" layer 2
   ```

3. Reiniciar y comprobar en el registro que la RX AWDL quedó enganchada:

   ```bash
   log show --last 10m --predicate 'eventMessage CONTAINS "awdl: rx"'
   ```

   Debe aparecer `awdl: rx attached, af 0 data 0 other 0 dropped 0` tras `awdl: enable`. Si sale
   `awdl: rx hook refused 0xe00002c7`, el `RTL8814Native` cargado es anterior a `ccc4122`.

## 3. Canal

El chip escucha en el canal de la red a la que está asociado; no hay todavía forma de fijar otro canal
desde la capa. AWDL usa los canales sociales 6 (2,4 GHz) y 44 o 149 (5 GHz)
([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md)). Para la prueba:

- asociar el Mac a una red del router en el canal 149 (o 44, o 6 en 2,4 GHz), cambiando el canal del
  router si hace falta;
- en qué canal pone un iPhone su AWDL cuando está asociado a otro canal de infraestructura es `?`; si no
  llega nada en 149, repetir en 6.

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
