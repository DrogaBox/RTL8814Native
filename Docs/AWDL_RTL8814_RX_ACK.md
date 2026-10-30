# AWDL en el RTL8814AU: recepción, filtros y ACK para `awdl0`

Qué hace hoy el driver con las tramas AWDL que llegan por el aire, dónde se pierden y qué hay que
programar para que el hardware las acepte y envíe el ACK de los unicast dirigidos a `awdl0`. Complementa
[AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §2 y §6 y corrige dos supuestos de ese
documento.

Fuentes:
- el árbol del driver (`Source/rtl8814au/`);
- el volcado de registros con el driver asociado a un AP (`Docs/regdumps/macos_c896a978_idle.txt`);
- las capturas de iOS 26 de owly (`tests/captures/`, revisión `9a676e1`, 4893 action frames y 4424
  tramas de datos AWDL; ver [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4);
- las notas de winawdl sobre la RTL8852CE.

El comportamiento del MAC que el código no muestra va con `?`; nada de esto se ha probado en hardware.

## 1. Direcciones de las tramas AWDL en el aire

Medido sobre las capturas:

| Tramas | A1 | A3 | ToDS/FromDS | Recuento |
|---|---|---|---|---|
| Action frames AWDL (categoría 127, OUI `00:17:F2`, tipo 8) | broadcast `FF:FF:FF:FF:FF:FF` | `00:25:00:FF:94:73` | 0/0 | 4893 de 4893 |
| Datos, subtipo 0 | unicast (MAC AWDL del destino) | `00:25:00:FF:94:73` | 0/0 | 2256 |
| Datos QoS, subtipo 8, sin A-MSDU | unicast | `00:25:00:FF:94:73` | 0/0 | 2123 |
| Datos, subtipo 0, multicast | multicast | `00:25:00:FF:94:73` | 0/0 | 45 |
| ADDBA (categoría 3, acción 0) | unicast | — | — | 56 |

Todas las tramas AWDL tienen la forma de las de una red ad-hoc: A1 destino, A2 origen y A3 el BSSID
fijo de AWDL. Los action frames de sincronía no van a una dirección multicast sino a broadcast.

## 2. Filtro hardware

### 2.1 Estado vivo

Con el driver asociado a un AP:

| Registro | Valor | Lectura |
|---|---|---|
| `REG_CR` `0x0100` | `0x000206FF` | `MSR` (`REG_CR+2`, `include/hal_com_reg.h:584`) = `0x02`: puerto 0 en modo estación (bits 16-17 = 2), puerto 1 sin enlace (bits 18-19 = 0). Volcado, línea 65 |
| `REG_RCR` `0x0608` | `0xF40060CE` | `AAP`, `AM`, `AB`, `AMF`, `CBSSID_DATA`, `CBSSID_BCN`. Línea 387 |
| `REG_MACID` `0x0610` | MAC de `en2` | Línea 389 |
| `REG_MACID1` `0x0700` / `REG_BSSID1` `0x0708` | `0x87654321` / `0x87654321` | Puerto 1 sin programar. Líneas 449 y 451 |

Los valores de tipo de red son los de `include/rtw_mlme_ext.h:52-56`: 0 sin enlace, 1 ad-hoc, 2
estación, 3 AP.

### 2.2 Action frames

Los action frames pasan el filtro: `RCR_AMF` acepta management, `RCR_AB` broadcast y
`REG_RXFLTMAP0 = 0xFFFF` todos los subtipos ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md)
§6.1). `RCR_CBSSID_BCN` solo afecta a beacons y probe responses.

### 2.3 Datos con BSSID ajeno

`RCR_CBSSID_DATA` está activo en modo estación y compara el BSSID de las tramas de datos. Con el puerto
1 sin programar, las tramas de datos AWDL (A3 = `00:25:00:FF:94:73`, distinto del BSSID del AP) no
casan. Que el hardware compare contra el BSSID de cada puerto activo o solo contra el del puerto 0 es
`?`.

El propio driver no confía en un filtro por puerto: cuando hay un AP, mesh o TDLS, o durante el escaneo
de mejor canal, quita `RCR_CBSSID_DATA` en vez de programar otro BSSID
(`rtw_hal_rcr_set_chk_bssid`, `hal/hal_com.c:3820-3828`). Es el precedente directo para AWDL.

### 2.4 ACK de los unicast a `awdl0`

`awdl0` tiene su propia MAC, distinta de la de `en2`. `RCR_AAP` deja pasar al DMA todo unicast, pero
el ACK por hardware solo sale para tramas cuyo A1 coincide con la MAC de un puerto (o con la MBSSID CAM
si `RCR_ENMBID` está activo, [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §2.2). Sin
ACK, el emisor repite cada trama hasta su límite de reintentos. winawdl lo midió en una RTL8852CE: 85 %
de tramas con el bit de reintento y transferencias atascadas; con la dirección propia en el CAM del
chip, del 0 al 4 % (`refs/winawdl/DESIGN-NOTES.md`, «Acknowledging frames»).

owly obtiene el mismo efecto en Linux uniéndose a una celda ad-hoc con el BSSID AWDL
(`join_ibss` en `crates/owly-radio/src/injection/setup.rs:369`; BSSID en
`crates/owly-awdl/src/lib.rs:169`).

## 3. Recorrido software y dónde se pierde cada trama

La clasificación por tipo está en `validate_recv_frame`: management en `core/rtw_recv.c:2305`, datos
en `:2321` y `validate_recv_data_frame` en `:2354`.

**Action frames AWDL:**
1. Superan `mgt_dispatcher`. El filtro de `core/rtw_mlme_ext.c:1317` solo admite A1 propio o
   broadcast, y el A1 de los action frames AWDL es broadcast (§1). Esto corrige
   [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §6.2, que los daba por perdidos aquí al
   suponerlos multicast.
2. `OnAction` los reparte por categoría. La 127 sí está en la tabla: `RTW_WLAN_CATEGORY_P2P = 0x7f`
   (`include/ieee80211.h:1525`), entrada `OnAction_p2p` en `core/rtw_mlme_ext.c:96`.
3. `OnAction_p2p` (`core/rtw_mlme_ext.c:7398`) los descarta: exige A1 igual a la MAC propia y el OUI
   Wi-Fi Direct `0x506F9A09` (`include/wifi.h:1101`). El broadcast con OUI `00:17:F2` sale sin efecto.

**Datos AWDL** (ToDS/FromDS 0/0):
1. Entran por el caso 0 de `validate_recv_data_frame` (`core/rtw_recv.c:2064`) en
   `sta2sta_data_frame` (`:1174`).
2. En modo estación, la rama de `:1299` exige que el origen sea el BSSID («sa and bssid should always
   be BSSID»): las tramas de un par AWDL se descartan.
3. La rama ad-hoc (`:1204-1224`) también las descartaría: compara A1 con la MAC del adaptador (`en2`,
   no `awdl0`) y A3 con el BSSID del adaptador.

**Unicast de management a la MAC de `awdl0`** (por ejemplo, las ADDBA): mueren en el filtro de A1 de
`mgt_dispatcher` (`core/rtw_mlme_ext.c:1317`), porque esa MAC no es la del adaptador.

## 4. Qué hay que programar

| Pieza | Qué | Por qué |
|---|---|---|
| Puerto 1 | `REG_MACID1` = MAC de `awdl0`, `REG_BSSID1` = `00:25:00:FF:94:73`, tipo de red ad-hoc (`MSR` bits 18-19 = 1) con `rtw_hal_set_msr` (`hal/hal_com.c:2382`, caso `HW_PORT1`) | Respuesta ACK para los unicast a `awdl0`, como la celda ad-hoc de owly. La función de beacon del puerto 1 (`REG_BCN_CTRL_1` `0x0551`) debe quedar apagada para no emitir beacons IBSS `?` |
| RCR | Quitar `RCR_CBSSID_DATA` mientras `awdl0` esté activa | Precedente del propio driver con AP, mesh y TDLS (§2.3). Coste: llegan al software datos de otros BSS del mismo canal, que hay que descartar por A3 |
| Desvío software | En `validate_recv_frame`, antes del `switch` por tipo (`core/rtw_recv.c:2304`), desviar al camino AWDL toda trama con A3 = `00:25:00:FF:94:73` | Es el único punto común a action frames, datos y unicast de management, antes de los tres descartes del §3. Los action frames van a `actionFrameInput`; los datos, tras quitar la cabecera AWDL, a la `RxCompletionQueue` de `awdl0` (encapsulado en curso de análisis) |
| Block Ack | Decidir si se acepta la ADDBA del par (`refs/owly/docs/SPEC.md` AWDL-15) | winawdl pasó de 971 kB/s a 1,7 MB/s al aceptarla; requiere que el hardware conozca el par como emisor de A-MPDU `?` |

Con `CONFIG_CONCURRENT_MODE` desactivado (`include/autoconf.h:113`), la escritura del puerto 1 no tiene
un adaptador que la origine: `rtw_hal_set_macaddr_port`, `rtw_hal_set_bssid` y `rtw_hal_set_msr`
eligen el puerto por `adapter->hw_port`. La capa tendrá que escribir los registros del puerto 1
directamente, o llamar a esas funciones con un adaptador cuyo `hw_port` sea `HW_PORT1`.

## 5. Abiertos

- Si `RCR_CBSSID_DATA` compara contra el BSSID de cada puerto activo o solo contra el del puerto 0
  `?`. Se resuelve en hardware: con el puerto 1 programado y `CBSSID_DATA` activo, ¿llegan los datos
  AWDL?
- Si el tipo ad-hoc del puerto 1 activa por sí mismo la emisión de beacons o la actualización del TSF
  con beacons del mismo BSSID (`DIS_TSF_UDT` en `REG_BCN_CTRL_1`) `?`.
- Si el MAC necesita una entrada de `macid` o del CAM de seguridad para el par antes de enviar el ACK
  de sus A-MPDU `?`.
