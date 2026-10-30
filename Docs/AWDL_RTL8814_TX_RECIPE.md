# AWDL en el RTL8814AU: cómo transmitir PSF, MIF y datos de `awdl0`

Receta concreta, sacada del árbol del driver, para emitir por el aire las tramas AWDL: los action
frames de sincronía (PSF/MIF) y las tramas de datos de `awdl0` hacia un par. Complementa
[AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §4 (ruta de management y campos del
descriptor) y [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) (direcciones y ACK). Formato de las
tramas: [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4 y §5. Lo no comprobado en
hardware va con `?`.

## 1. El modelo: la inyección del modo monitor del vendor

`rtw_monitor_xmit_entry` (`Source/rtl8814au/core/rtw_xmit.c:4445`) emite una trama 802.11 completa
construida por el llamante, sea de management o de datos:

1. `alloc_mgtxmitframe` (cuerpo en `_alloc_mgtxmitframe`, `core/rtw_mlme_ext.c:7496`) reserva un
   `xmit_frame` de la cola de management.
2. Pone a cero la cabecera y copia la trama en `buf_addr + TXDESC_OFFSET`.
3. Atributos según el tipo de la trama:
   - datos: `update_monitor_frame_attrib` (`core/rtw_mlme_ext.c:7555`), con tasa de 24 Mbit/s si
     A1 o A3 son broadcast;
   - management: `update_mgntframe_attrib` (`:7616`).
4. `retry_ctrl = FALSE`, `pktlen` y `last_txcmdsz` = longitud, y número de secuencia tomado de la
   cabecera.
5. `dump_mgntframe` (`:7734`) → `rtw_hal_mgnt_xmit` → descriptor y pipe USB.

La función es código Linux: recibe un `sk_buff` y está bajo una guarda de `LINUX_VERSION_CODE`. Sirve
de modelo; la capa necesita su propia entrada que haga los mismos pasos a partir de un `mbuf` o de un
buffer propio.

## 2. Qué no se toca por el camino

| Punto | Comportamiento | Cita |
|---|---|---|
| Direcciones | `update_mgntframe_attrib_addr` solo copia A1/A2 de la trama a `ra`/`ta`; no las reescribe. A2 puede ser la MAC de `awdl0` y A3 el BSSID `00:25:00:FF:94:73` | `core/rtw_mlme_ext.c:7691` |
| Broadcast | El bit `BMC` del descriptor sale de `IS_MCAST(pattrib->ra)`: un PSF/MIF a `FF:FF:FF:FF:FF:FF` se marca solo | `hal/rtl8814a/usb/rtl8814au_xmit.c:54`, `:88-89` |
| Número de secuencia | Sin QoS lo pone el hardware (`HWSEQ_EN`) | `rtl8814au_xmit.c:111-117` |
| PMF del port | `rtl8814_wpa2_pmf_protect_tx` solo cifra management con A1 = BSSID del AP y categoría robusta; los AF AWDL (broadcast, categoría 127), las respuestas ADDBA a un par (A1 ≠ AP) y las tramas de datos salen intactos | `Source/Driver/WPA2Supplicant.c:607` |
| Cifrado | `encrypt = _NO_PRIVACY_` en ambos helpers | `core/rtw_mlme_ext.c:7555`, `:7616` |

## 3. Lo que hay que fijar a mano

### 3.1 Tasa

- **Management** (PSF/MIF, ADDBA): `pmlmeext->tx_rate`, que vale 6 Mbit/s OFDM en 5 GHz y 1 Mbit/s CCK
  en 2,4 GHz ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §4.1). Se cambia con
  `update_mgnt_tx_rate` (`core/rtw_mlme_ext.c:7546`). En el canal 6 conviene 6 Mbit/s OFDM: un PSF de
  unos 200 B a 1 Mbit/s ocupa más de 1,6 ms, y el par solo pasa una franja de cada 16 en ese canal
  (ESP32Drop midió que el CCK a 1 Mbit/s llena casi toda esa franja).
- **Datos**: `update_monitor_frame_attrib` fija `MGN_VHT1SS_MCS9` con `bwmode = 20 MHz`. VHT MCS 9 a
  20 MHz no es válido en 802.11ac con 1 flujo, así que hay que sobrescribir `pattrib->rate` tras la
  llamada. Mientras la capa no anuncie HT/VHT ([AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) §6),
  lo coherente es una tasa legacy (24 Mbit/s, la que usa el propio camino para broadcast) o HT MCS 7 a
  20 MHz `?`.

### 3.2 Reintentos

El descriptor tiene un límite de reintentos de 6 bits (`DATA_RETRY_LIMIT`,
`include/rtl8814a_xmit.h:189`). La rama de management lo activa siempre: 6 con `retry_ctrl = TRUE` y
**12** con `FALSE` (`rtl8814au_xmit.c:259-263`). Que `FALSE` no signifique «sin reintentos» importa
porque un unicast reintentado más allá de la visita del par bloquea la cola (owly, `tx_noack`).
Opciones:

- unicast de datos: `retry_ctrl = TRUE` (6), cerca de los 7 de winawdl;
- si se quiere un único intento, escribir un límite propio en el descriptor; el valor que equivale a
  «sin reintento» en este MAC no está comprobado `?`;
- broadcast (PSF/MIF): el MAC no espera ACK de una trama de grupo, así que el límite es irrelevante
  `?`.

### 3.3 ACK de vuelta

Para que un unicast a un par se dé por entregado, el MAC tiene que reconocer el ACK dirigido a A2
(la MAC de `awdl0`). Es el mismo requisito que el ACK en recepción: el puerto 1 con
`REG_MACID1` = MAC de `awdl0` ([AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §4). Sin él, cada
unicast agotaría sus reintentos aunque el par lo reciba `?`.

## 4. Las tramas

**PSF / MIF** (management, subtipo action):

```
FC 0xD0 0x00 | dur 0 | A1 FF:FF:FF:FF:FF:FF | A2 MAC awdl0 | A3 00:25:00:FF:94:73 | seq (HW)
7F | 00 17 F2 | 08 | versión | subtipo (0 PSF, 3 MIF) | 00 | phy_tx (u32) | target_tx (u32) | TLVs
```

Los TLV salen de la plantilla del ioctl 151 más los TLV 4 y 18 que compone la capa
([AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) §5.2; detalle de emisión en
`AWDL_PSF_MIF_EMISSION.md`, en curso). `phy_tx` y `target_tx` son µs del reloj de la capa: el instante
previsto de la franja y el de la entrega a la cola. El kernel del receptor calcula con ellos
`sync_contention` ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) §5).

**Datos de `awdl0`** (de la cola TX de Skywalk, etapa 2b):

```
FC 0x08 0x00 (o 0x88 con QoS) | A1 MAC del par | A2 MAC awdl0 | A3 00:25:00:FF:94:73 | seq
AA AA 03 | 00 17 F2 | 08 00 | 03 04 | seq AWDL (u16 LE) | 00 00 | ethertype | IPv6
```

Quién añade la cabecera AWDL antes de que el paquete llegue al driver es la pregunta abierta del
borrador `AWDL_DATA_ENCAP.md`. `IO80211AWDLPeerManager::addAwdlSnapHeader` (`0xffffff8002216cd0`)
construye una cabecera 802.3 + SNAP con el campo `+10` a cero, que en el aire lleva la secuencia: el
driver tendría que rellenarla `?`.

## 5. Momento de emisión

El descriptor no lleva instante de emisión. La capa encola dentro de la ventana que marca su propio
temporizador ([AWDL_WINDOW_TIMER.md](AWDL_WINDOW_TIMER.md) §5 y §6) y puede usar `REG_TXPAUSE` como
compuerta de colas ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §4.4). La latencia entre
el encolado y la salida por USB no está medida `?`.
