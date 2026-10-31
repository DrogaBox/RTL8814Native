# AWDL en RTL8814: TX crudo de action frames y puerto 1 (solo fuentes)

Ámbito: `Source/rtl8814au` y `Source/Driver` leídos directamente, sin desensamblado. Objetivo:
cómo transmitir una trama de gestión 802.11 cruda (action, categoría 127, A1 broadcast, A3
`00:25:00:FF:94:73`) en el canal actual con el radio asociado a un AP, cómo programar el puerto 1
hardware directamente y dónde enganchar la RX de esas tramas. Configuración vigente:
`CONFIG_CONCURRENT_MODE` desactivado (`Source/rtl8814au/include/autoconf.h:113`), un solo
adaptador con `hw_port = HW_PORT0` (`Source/rtl8814au/os_dep/linux/usb_intf.c:1320`).

## 1. Transmisión de una trama de gestión cruda

### 1.1 Funciones exactas

La receta completa ya existe en el árbol: `rtl_sae_issue_auth` (`Source/Driver/SAEAuth.c:17-75`)
construye y emite un frame de autenticación crudo. La cadena para una trama action AWDL es la
misma con otro cuerpo:

1. `alloc_mgtxmitframe(&padapter->xmitpriv)` (7529) — envoltorio de `_alloc_mgtxmitframe`
   (`Source/rtl8814au/core/rtw_mlme_ext.c:7496`): `rtw_alloc_xmitframe_ext` + `rtw_alloc_xmitbuf_ext`
   (7511), fija `frame_tag = MGNT_FRAMETAG`, `pxmitbuf` y `buf_addr = pxmitbuf->pbuf`
   (7519-7521). Declaración en `Source/rtl8814au/include/rtw_mlme_ext.h:675`.
2. `update_mgntframe_attrib(padapter, pattrib)` (`Source/rtl8814au/core/rtw_mlme_ext.c:7616`,
   declaración `rtw_mlme_ext.h:884`).
3. Escritura del header en `pmgntframe->buf_addr + TXDESC_OFFSET` (patrón
   `Source/Driver/SAEAuth.c:40-54`; el buffer se pone a cero en
   `WLANHDR_OFFSET + TXDESC_OFFSET` antes de escribir).
4. `pattrib->last_txcmdsz = pattrib->pktlen` (`Source/Driver/SAEAuth.c:70`).
5. `dump_mgntframe(padapter, pmgntframe)` (`Source/rtl8814au/core/rtw_mlme_ext.c:7734`).

`dump_mgntframe` aplica primero el hook PMF propio del árbol (`rtl_pmf_protect_mgnt`,
`Source/rtl8814au/core/rtw_mlme_ext.c:7723-7732`; sobre tramas AWDL no cifradas no altera nada ?)
y llama `rtw_hal_mgnt_xmit` (`Source/rtl8814au/hal/hal_intf.c:816`), que copia `ra`/`ta` del
header al attrib (`update_mgntframe_attrib_addr`, `rtw_mlme_ext.c:7691-7715`) y llama
`hal_func.mgnt_xmit`, asignado a `rtl8814au_mgnt_xmit` para USB
(`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:2043`). Esa función delega en
`rtw_dump_xframe` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:369,897-899`): genera el
descriptor TX (`update_txdesc`, 403), calcula la cola USB con `rtw_get_ff_hwaddr` (415) y entrega
el buffer con `rtw_write_port` (427). Con `qsel = QSLT_MGNT (0x12)`, `rtw_get_ff_hwaddr` devuelve
`MGT_QUEUE_INX` (`Source/rtl8814au/core/rtw_xmit.c:4359,4390-4392`).

`rtw_hal_mgnt_xmit` inserta además BIP si hay clave BIP instalada
(`Source/rtl8814au/hal/hal_intf.c:822-829`); en un enlace STA normal sin PMF activo no entra.

### 1.2 Campos de tasa y reintento que fija `update_mgntframe_attrib`

`Source/rtl8814au/core/rtw_mlme_ext.c:7616-7689`:

| Campo | Valor | Línea |
|---|---|---|
| `hdrlen` | 24 | 7628 |
| `priority` | 7 | 7630 |
| `mac_id` | `RTW_DEFAULT_MGMT_MACID` = 1 (`Source/rtl8814au/include/drv_types.h:806`) | 7631 |
| `qsel` | `QSLT_MGNT` = 0x12 (`Source/rtl8814au/include/hal_com.h:262`) | 7632 |
| `rate` | `pmlmeext->tx_rate` | 7672 |
| `raid` | `rtw_get_mgntframe_raid` según CCK/OFDM | 7667-7671 |
| `encrypt` / `bswenc` | `_NO_PRIVACY_` / `_FALSE` | 7674-7675 |
| `qos_en` / `ht_en` / `sgi` | `_FALSE` | 7677-7681 |
| `bwmode` | `CHANNEL_WIDTH_20` | 7679 |
| `seqnum` | `pmlmeext->mgnt_seq` | 7683 |
| `retry_ctrl` | `_TRUE` | 7685 |
| `mbssid` | 0 | 7687 |

La tasa base es 6 Mbps OFDM cuando el canal actual es > 14 (o el modo no incluye 11B); 1 Mbps CCK
en caso contrario (`Source/rtl8814au/core/rtw_mlme_ext.c:1062-1064`). `update_mgnt_tx_rate`
(7546-7552) la cambia globalmente. El descriptor TX de la rama `MGNT_FRAMETAG`
(`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:226-274`) fija `USE_RATE=1`,
`TX_RATE = MRateToHwRate(pattrib->rate)` (232), activa el límite de reintentos y pone
`DATA_RETRY_LIMIT` a 6 si `retry_ctrl == _TRUE`, 12 si no (259-263). Con `CONFIG_XMIT_ACK`
desactivado (`Source/rtl8814au/include/autoconf.h:63`) no se pide informe TX (bloque
`CONFIG_XMIT_ACK` de 266-274 muerto).

El emisor debe incrementar la secuencia: SAE usa `SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
pmlmeext->mgnt_seq++` (`Source/Driver/SAEAuth.c:45-46`). Para AWDL conviene un contador propio
para no interferir con la numeración de la gestión de infraestructura.

Variante con espera de ACK: `dump_mgntframe_and_wait_ack` (`rtw_mlme_ext.c:7809`, timeout 500 ms
en 7812) exige `CONFIG_XMIT_ACK`; sin él cae a `dump_mgntframe` + `rtw_msleep_os(50)`
(7803-7804). Una trama broadcast (A1 `ff:ff:ff:ff:ff:ff`) no recibe ACK 802.11, así que la
variante correcta para AWDL es `dump_mgntframe`. `dump_mgntframe_and_wait` (7746-7774) espera
por el completion USB vía `submit_ctx`, no por ACK de radio.

### 1.3 Trama suministrada por otro kext

El ABI radio (`Source/Driver/RTLRadioABI.h`, `RTL_RADIO_ABI = 2`, línea 20) tiene la función TX
de gestión: `RTL_RADIO_FN_TX_80211` (`rtlRadioTx80211`, 66) recibe la trama completa (header y
cuerpo, sin FCS) en p1 y la longitud en p2. La implementa `rtl8814native_radio_tx_80211`
(`Source/Driver/AWDLTx.c:21-61`): valida tipo de gestión y estado del radio, `alloc_mgtxmitframe`,
copia los bytes tal cual a `buf_addr + TXDESC_OFFSET`, fija la secuencia del driver (50-51),
desactiva reintentos si A1 es group-addressed (53-55) y llama `dump_mgntframe` (59). El kernel
vendor no analiza el frame en la ruta TX, solo lo recorta a `last_txcmdsz`; la categoría 127 nunca
pasa por ninguna tabla de action frames en TX. La TX de datos (`RTL_RADIO_FN_TX`, 42) sigue siendo
solo Ethernet. El RX AWDL es `RTL_RADIO_FN_AWDL_HOOK` (53), que registra un `rtl_radio_awdl_rx_fn`
(55-56) que recibe los bytes crudos (header incluido, FCS retirado, 47-51).

El despacho `callPlatformFunction` está en `Source/Driver/RTL8814Native.cpp:431-536`: el caso AWDL
hook en 504-507, `RTL_RADIO_FN_AWDL_PORT` (puerto 1) en 508-510, `RTL_RADIO_FN_SET_CHANNEL` en
511-519 y `RTL_RADIO_FN_TX_80211` en 520-529. La capa AirLayer lo usa desde `RTLAwdInterface`
(`Source/AirLayer/AWDLInterface.cpp`).

### 1.4 Hilo y cerrojos

- `dump_mgntframe` no duerme: la asignación va con los spinlocks de `xmit_priv` y
  `rtw_write_port` envía el URB. El propio `rtw_hal_mgnt_xmit` advierte
  «[IMPORTANT] This function would be run in interrupt context»
  (`Source/rtl8814au/hal/hal_intf.c:814`).
- El emisor SAE vivo corre en `sae_work`, un `work_struct` encolado con `rtl_schedule_work`
  (`Source/Driver/SAEJoin.c:591,615,632,651`): contexto de proceso (workqueue), sin
  retención de cerrojos del vendor tree al llamar a `dump_mgntframe`. Ese es el patrón a
  replicar: copiar los bytes del kext llamador en el mgntframe y despachar el envío en un
  workitem propio; la llamada `callPlatformFunction` del kext AWDL no debe encolar la trama al
  USB directamente ?
- `dump_mgntframe_and_wait_ack` sí duerme (mutex `ack_tx_mutex` + `rtw_sctx_wait`,
  `rtw_mlme_ext.c:7789-7796`): prohibido desde timers o contexto atómico.
- No hay mutex en la ruta TX mgnt: la exclusión mutua es la cola de frames/buffers. Si el kext
  puede llamar en concurrencia, la serialización es tarea del glue ?

## 2. Programación directa del puerto 1

### 2.1 Mapa de registros

| Registro | Dirección | Definición |
|---|---|---|
| `MSR` | 0x0102 (`REG_CR + 2`) | `Source/rtl8814au/include/hal_com_reg.h:116,584` |
| `REG_BCN_CTRL` / `REG_BCN_CTRL_1` | 0x0550 / 0x0551 | 371-372 |
| `REG_TSFTR` / `REG_TSFTR1` | 0x0560 / 0x0568 | comentario en `hal_com.c:4132-4133` |
| `REG_RCR` | 0x0608 | `hal_com_reg.h:413` |
| `REG_MACID` / `REG_BSSID` | 0x0610 / 0x0618 | 418-419 |
| `REG_MACID1` / `REG_BSSID1` | 0x0700 / 0x0708 | 506-507 (espejo 8814A: `Source/rtl8814au/include/rtl8814a_spec.h:433-434`) |

Bits: tipo de red del puerto 1 = `REG_CR` BIT[19:18] = bits 3:2 del byte `MSR`
(comentario en `Source/rtl8814au/hal/hal_com.c:2407`); valores `_HW_STATE_ADHOC_` = 0x01
(`Source/rtl8814au/include/rtw_mlme_ext.h:53`, igual a `MSR_ADHOC`,
`hal_com_reg.h:667`). `RCR_CBSSID_DATA` = BIT(6) y `RCR_CBSSID_BCN` = BIT(7)
(`hal_com_reg.h:1134-1135`). En `REG_BCN_CTRL_1`, `EN_TXBCN_RPT` = BIT(2),
`EN_BCN_FUNCTION` = BIT(3), `DIS_TSF_UDT` = BIT(4) (`hal_com_reg.h:1533-1534,1540`): el valor
0x10 de los volcados en vivo equivale a DIS_TSF_UDT solo, sin función beacon ni informe TX.

### 2.2 Por qué los registros del puerto 1 quedan libres

Todo escritor del vendor tree pasa por `adapter->hw_port`, que con concurrent off vale
`HW_PORT0` siempre (`usb_intf.c:1320`; el `HW_PORT1` de
`Source/rtl8814au/os_dep/linux/os_intfs.c:3142` solo lo toca `rtw_drv_add_vir_if`, 3111,
inexistente sin concurrent mode). La excepción es el glue AWDL: `rtl8814native_radio_awdl_port`
escribe los registros del puerto 1 directamente (sección 2.5). En concreto:

- `REG_MACID1`/`REG_BSSID1`: solo los alcanzan `_get_macaddr_reg`/`_get_bssid_reg` bajo
  `CONFIG_CONCURRENT_MODE` (`Source/rtl8814au/hal/hal_com.c:3376-3391` y 3448-3463), y
  `rtw_hal_set_macaddr_port`/`rtw_hal_set_bssid` (3397-3419, 3468-3482) usan el puerto del
  adaptador.
- `REG_BCN_CTRL_1`: `hw_var_set_bcn_func` solo lo elige con concurrent + `HW_PORT1`
  (`hal_com.c:11254-11256`); `rtw_hal_correct_tsf` toca `REG_BCN_CTRL_1`/`REG_TSFTR1` solo
  cuando el parámetro es `HW_PORT1` (11801-11832), y con concurrent off nadie lo pasa.
- Los bits 3:2 de `MSR`: `rtw_hal_set_msr` caso `HW_PORT1` (2406-2410), no alcanzable con
  `hw_port = HW_PORT0`.
- `hw_var_port_switch` (intercambio completo de puertos) está bajo
  `CONFIG_RUNTIME_PORT_SWITCH`, que solo se define con concurrent mode
  (`Source/rtl8814au/include/autoconf.h:115`).

Por eso los volcados muestran `REG_BCN_CTRL_1 = 0x10` en operación normal (HANDOFF_AWDL.md,
estado del 2026-10-30; commit `ab5e120`).

### 2.3 Helpers de escritura desde el glue C

- Directos: `rtw_write8`/`rtw_write16`/`rtw_write32` (macros sobre `_rtw_write8/16/32`,
  declaradas en `Source/rtl8814au/include/rtw_io.h:328-330`, macros 391/425; símiles de lectura
  `rtw_read8/16/32`). Patrón de escritura de MAC: bucle de 6 bytes sobre el registro base
  (`hal_com.c:3415-3416` para MACID, 3478-3479 para BSSID).
- Alto nivel: `rtw_hal_set_hwreg` (`Source/rtl8814au/hal/hal_intf.c:622`) con
  `HW_VAR_MEDIA_STATUS` → `rtw_hal_set_msr` (`hal_com.c:12152-12155`, 2385),
  `HW_VAR_BSSID` → `rtw_hal_set_bssid` (`hal_com.c:12171-12172`, 3468),
  `HW_VAR_RCR` → `hw_var_rcr_config` (`hal_com.c:12174-12175`, 3680: escribe `REG_RCR` en 3687 y
  actualiza la caché `hal_data->ReceiveConfig` en 3689).

**Precaución**: los helpers de alto nivel operan sobre `adapter->hw_port` (puerto 0 aquí).
Para tocar el puerto 1 hay dos caminos: escrituras directas con `rtw_write8/16/32` a 0x700,
0x708 y 0x102, o alterar temporalmente `adapter->hw_port` a `HW_PORT1` alrededor de la llamada
a `rtw_hal_set_hwreg`. El segundo toca estado compartido del adaptador y compite con el MLME; el
primero es el que usa el propio árbol para el puerto 1 (buces de 6 bytes). Los bits de `MSR` se
mantienen con read-modify-write: `val8 = rtw_read8(adapter, MSR) & 0x03; val8 |= net_type << 2;`
(replicar `hal_com.c:2408-2410`). Para poner ad-hoc en el puerto 1: `_HW_STATE_ADHOC_ << 2` =
0x04 en los bits altos, conservando los bits 1:0 del puerto 0.

Para `RCR_CBSSID_DATA` conviene `rtw_hal_rcr_clear(adapter, RCR_CBSSID_DATA)`
(`hal_com.c:3735-3749`): lee `HW_VAR_RCR`, limpia el bit y escribe solo si cambia, de modo que
la caché `ReceiveConfig` queda coherente; una escritura cruda de `REG_RCR` desincronizaría esa
caché.

### 2.4 Qué restaura los registros en desconexión y escaneo

- **Desconexión**: `rtw_mlmeext_disconnect` (`Source/rtl8814au/core/rtw_mlme_ext.c:12372`)
  llama `HW_VAR_MLME_DISCONNECT` (12403), `HW_VAR_BSSID` con dirección nula (12404),
  `rtw_hal_rcr_set_chk_bssid(MLME_STA_DISCONNECTED)` (12406) y `Set_MSR(_HW_STATE_STATION_)`
  (12409). Todo va por el puerto del adaptador (`HW_PORT0`): el puerto 1 no se toca. `MACID1`,
  `BSSID1`, los bits 3:2 de `MSR` y `BCN_CTRL_1` sobreviven a la desconexión.
- **Escaneo**: `hw_var_set_mlme_sitesurvey` (`Source/rtl8814au/hal/hal_com.c:11353`, alcanzado
  por `HW_VAR_MLME_SITESURVEY`, 12323-12324) reescribe `REG_RXFLTMAP2` (11376/11405) y el RCR
  con `rtw_hal_rcr_set_chk_bssid` para `MLME_SCAN_ENTER`/`MLME_SCAN_DONE` (11378/11408). El
  puerto 1 no se toca.
- **La excepción es el RCR**, registro global único compartido por ambos puertos:
  `rtw_hal_rcr_set_chk_bssid` (`hal_com.c:3751`) recalcula `RCR_CBSSID_DATA`/`RCR_CBSSID_BCN`
  a partir del estado MI en cada transición MLME: conectando (14243 en
  `Source/rtl8814au/core/rtw_mlme_ext.c`), conectado (12589), desconectado (12516, 14115),
  inicio/fin de escaneo (`hal_com.c:11378`, 11408). Mientras `rtl8814native_awdl_port1` vale 1,
  la misma función mantiene `RCR_CBSSID_DATA` limpio por su cuenta (`hal_com.c:3829-3830`) y
  `rtlRadioAwdlPort` la invoca al activar y desactivar el puerto (`Source/Driver/AWDLTx.c:100`):
  con el puerto en marcha no hace falta re-aplicar el clear tras cada evento. Con el puerto
  apagado, un clear manual (`rtw_hal_rcr_clear`, 3735-3749) se repone en la siguiente
  transición (el `|=` de `RCR_CBSSID_DATA` en 3833 y la escritura final en 3861).
- `MACID1`/`BSSID1`/`BCN_CTRL_1`/bits de `MSR` del puerto 1: ningún escritor restaurador existe
  en esta configuración (sección 2.2); su valor persiste hasta que el glue lo cambie.

### 2.5 Alcance del puerto 1 en este diseño

La TX AWDL ya no exige asociación: sin red, la capa AirLayer aparca el radio en el canal de
reposo con `RTL_RADIO_FN_SET_CHANNEL` (`rtlRadioSetChannel`, `RTLRadioABI.h:84`;
`rtl8814native_radio_set_channel`, `Source/Driver/AWDLTx.c:111-127`, canal a 20 MHz vía
`rtw_set_chbw_cmd` y tasa de gestión a 6 Mb/s OFDM). El canal queda visible en
`rtl_radio_status.idle_channel` (`RTLRadioABI.h:157-159`) y `txTick` solo emite cuando el radio
está en un canal social (`Source/AirLayer/AWDLInterface.cpp:899-902` y 913-926). Con el radio
asociado (puerto 0, canal infra), la TX AWDL sale por la cola MGNT del puerto 0 en el canal
actual; el puerto 1 no participa en la TX.

El papel del puerto 1 es que los unicast que los pares devuelvan a `awdl0` reciban ACK
hardware: `RTL_RADIO_FN_AWDL_PORT` (`rtlRadioAwdlPort`, `RTLRadioABI.h:74`) programa
`REG_MACID1` con la MAC de `awdl0`, `REG_BSSID1` con `00:25:00:FF:94:73` y el tipo ad-hoc en los
bits 3:2 de `MSR` (`Source/Driver/AWDLTx.c:87-93`); `REG_BCN_CTRL_1` queda intacto a 0x10. La
capa lo activa al empezar a transmitir y lo desactiva al parar
(`RTLAwdInterface::setRadioTx`, `Source/AirLayer/AWDLInterface.cpp:855-891`: llamada con la MAC
en 746, con NULL en 754). Mientras está activo, `rtw_hal_rcr_set_chk_bssid` mantiene
`RCR_CBSSID_DATA` limpio para que las tramas AWDL no se descarten por no pertenecer al BSSID
infra (sección 2.4). La mecánica de filtrado y ACK de hardware se desarrolla en
`AWDL_RTL8814_RX_ACK.md`; aquí solo constan los mecanismos de escritura y restauración.

## 3. Gancho RX en `validate_recv_frame`

### 3.1 Punto exacto (ya implementado)

`validate_recv_frame` está en `Source/rtl8814au/core/rtw_recv.c:2213`. El gancho existe y ocupa
las líneas 2309-2318, inmediatamente antes del `switch (type)` de la línea 2319:

- Condición: `u.hdr.len >= WLAN_HDR_A3_LEN` (24 bytes, `Source/rtl8814au/include/wifi.h:29`),
  `type != WIFI_CTRL_TYPE`, y `_rtw_memcmp(GetAddr3Ptr(ptr), rtl_awdl_bssid, ETH_ALEN)` —
  `GetAddr3Ptr` = `pbuf + 16` (`wifi.h:437`), `rtl_awdl_bssid = {00,25,00,FF,94,73}`
  (`Source/rtl8814au/core/rtw_recv.c:27`).
- Llamada: `rtl8814native_awdl_rx(adapter->pnetdev, ptr, precv_frame->u.hdr.len,
  pattrib->phy_info.recv_signal_power, rtw_get_oper_ch(adapter))` (2313-2315). Si devuelve
  distinto de cero, `retval = _FAIL` con `goto exit` (2316-2317) y la trama se libera en
  `recv_func_prehandle` tras el retorno (`rtw_recv.c:4263-4266`); si devuelve cero, sigue al
  switch normal (mgnt → `validate_recv_mgnt_frame`, 1974).
- La copia la hace el destinatario: `rtl8814native_awdl_rx` está en
  `Source/Driver/VendorGlue.cpp:235-241` y despacha a `RTLEthernetAdapter::awdlInput`
  (`Source/Driver/RTLEthernetAdapter.cpp:242`), que entrega los bytes al hook del kext AWDL
  registrado por `RTL_RADIO_FN_AWDL_HOOK`; «The bytes are only valid during the call: the hook
  copies what it keeps» (`RTLRadioABI.h:49-50`).

Para copiar «toda trama cuyo A3 sea el BSSID AWDL», este es el punto: A3 está disponible sin
analizar nada (offset 16), el tipo no importa aún y el hook corre antes de cualquier filtrado
software.

### 3.2 ¿Está intacto el header 802.11?

Sí. La cadena USB es `recvbuf2recvframe`
(`Source/rtl8814au/hal/rtl8814a/usb/usb_ops_linux.c:105`): `rtl8814_query_rx_desc_status`
(144) llena el attrib desde el descriptor, y `rtw_os_alloc_recvframe` (168) hace que `rx_data`
apunte a `pbuf + shift_sz + drvinfo_sz + RXDESC_SIZE`, es decir, al primer byte del header
802.11. `validate_recv_frame` corre dentro de `recv_func_prehandle`
(`Source/rtl8814au/core/rtw_recv.c:4237`, llamada en 4263), **antes** de `decryptor` (primera
llamada de `recv_func_posthandle`, 4274 y 4287): el header es el crudo de la onda. Las tramas
action AWDL no van cifradas, y el FCS ya está retirado: con `CONFIG_RX_PACKET_APPEND_FCS`
definido (`autoconf.h:252`) el RCR de inicio incluye `RCR_APPFCS`
(`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:449-458`) y el driver resta
`IEEE80211_FCS_LEN` de `pkt_len` en `usb_ops_linux.c:163-166`. Los bytes que el hook recibe son
los del frame completo sin FCS.

Durante un escaneo `hw_var_set_mlme_sitesurvey` escribe `REG_RXFLTMAP2` con
`value_rxfltmap2` (`hal_com.c:11376`). En esta configuración `CONFIG_FIND_BEST_CHANNEL` está
activo (`autoconf.h:142`, bajo `CONFIG_AP_MODE` de la línea 127), así que el valor es 0xFFFF
(11364) y las tramas de gestión, incluida la acción, siguen llegando durante el escaneo; la
rama que lo pone a 0 (11367) no se compila. Fuera de escaneo, los arranques de modo AP lo
escriben a 0xFFFF (`hal_com.c:3137,3257`) y `hw_var_set_mlme_disconnect` a 0x0000 (11322): con
0xFFFF el subtipo action (0xD) se acepta.

### 3.3 Campos de `rx_pkt_attrib` disponibles en ese punto

`struct rx_pkt_attrib` en `Source/rtl8814au/include/rtw_recv.h:151-211`.

- **RSSI**: `pattrib->phy_info.recv_signal_power` (`Source/rtl8814au/include/cmn_info/rtw_sta_info.h:235`,
  campo `recv_signal_power` de `phydm_phyinfo_struct` (225), incrustado como `phy_info` en
  `rtw_recv.h:204`). Lo rellena `rx_query_phy_status`
  (`Source/rtl8814au/core/rtw_recv.c:4718`) a través de PHYDM, que asigna
  `recv_signal_power = rx_power` con el comentario «RSSI in dB»
  (`Source/rtl8814au/hal/phydm/phydm_phystatus.c:1985,2802`). Se procesa antes de
  `validate_recv_frame`: `pre_recv_entry` llama `rx_query_phy_status` (4926) y después
  `rtw_recv_entry` (4927), que pasa por `recv_func` → `recv_func_prehandle` (4263). Es el único
  dato que el peer manager necesita (AWDL_PEERMANAGER_DRIVER_ABI §1: umbral en +0x2c).
- **Canal**: no hay canal en el attrib para 8814A USB — `rtl8814_query_rx_desc_status`
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_rxdesc.c:20-60`) no rellena `pattrib->ch`
  (`rtw_recv.h:194`, «RX channel»), que queda a 0. El gancho usa
  `rtw_get_oper_ch` (canal operativo del dvobj, `Source/rtl8814au/core/rtw_wlan_util.c:415-417`),
  correcto mientras el radio no salta de canal, y por tanto la cola AWDL no puede saber de qué
  canal venía una trama si el canal cambió en el intervalo ?
- **TSF**: `pattrib->tsfl` (`rtw_recv.h:200`) existe pero no se llena en 8814A (el parser de
  descriptor no lo toca); solo lo consume el constructor radiotap de monitor
  (`rtw_recv.c:3949-3956`). En `validate_recv_frame` no hay mactime por paquete. Para un
  mactime hardware hay que leer el contador TSF: `rtw_hal_get_tsftr_by_port(adapter, port)`
  (`Source/rtl8814au/hal/hal_com.c:11891`), que para 8814A selecciona el puerto en
  `REG_MBSSID_BCN_SPACE + 3` (0x554) bits [30:28] (11928-11932) y lee `REG_TSFTR` (0x0560,
  11934-11936).
  Es una lectura del contador actual, no del instante de recepción ?
- Extra: `pattrib->data_rate` sí viene del descriptor (`rtl8814a_rxdesc.c:55`).

## 4. Resumen operativo

| Acción | Mecanismo | Citas |
|---|---|---|
| TX cruda de action AWDL | `rtlRadioTx80211`: `alloc_mgtxmitframe` → copiar bytes a `buf_addr+TXDESC_OFFSET` → secuencia del driver → `dump_mgntframe` | AWDLTx.c:21-61; SAEAuth.c:33-73; rtw_mlme_ext.c:7496,7616,7734 |
| Tasa/reintentos | `attrib.rate = pmlmeext->tx_rate` (6M OFDM en 5 GHz), `retry_ctrl=TRUE` → RETRY_LIMIT 6 | rtw_mlme_ext.c:1062,7672,7685; rtl8814au_xmit.c:232,259-263 |
| Contexto de envío | workitem (patrón `sae_work`); sin sleeps en `dump_mgntframe` | SAEJoin.c:591,615,632,651; hal_intf.c:814 |
| Port 1 ad-hoc | `rtlRadioAwdlPort`: `REG_MACID1` 0x700, `REG_BSSID1` 0x708 y `MSR` (0x102) bits 3:2 = 01b por `rtw_write8`; en marcha solo mientras awdl0 transmite (`setRadioTx`) | AWDLTx.c:77-102; AWDLInterface.cpp:855-891; hal_com.c:2406-2410; hal_com_reg.h:506-507,584 |
| Canal de reposo | `rtlRadioSetChannel`: radio aparcado a 20 MHz sin asociar, visible en `idle_channel` | RTLRadioABI.h:84,157-159; AWDLTx.c:111-127 |
| `REG_BCN_CTRL_1` | no tocar (0x10 = DIS_TSF_UDT) | hal_com_reg.h:1540 |
| `RCR_CBSSID_DATA` | limpio mientras `rtl8814native_awdl_port1` = 1 (`rtw_hal_rcr_set_chk_bssid` lo recalcula); con el puerto off, clear manual que se repone en cada transición MLME | hal_com.c:3735-3749,3751,3829; AWDLTx.c:100; rtw_mlme_ext.c:12516,12589 |
| RX AWDL | hook en `validate_recv_frame` 2309-2318 (A3 = 00:25:00:FF:94:73), copia en el hook | rtw_recv.c:27,2309-2318 |
| Datos RX | RSSI `phy_info.recv_signal_power`; canal `rtw_get_oper_ch`; sin TSF por paquete | rtw_recv.c:2314,4718; hal_com.c:11891 |
