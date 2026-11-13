# Compartición de tiempo del radio: BSS de infraestructura y AWDL en el RTL8814AU

¿Puede el árbol vendor (`Source/rtl8814au`) compartir el radio entre una BSS de infraestructura en
un canal no social (ejemplo: 5 GHz canal 153 a 80 MHz) y AWDL en el canal 149 a 20 MHz, sirviendo
los slots AWDL mientras la estación permanece asociada, como hace un Mac real? Análisis basado
solo en el fuente del vendor tree; nada de esto se ha medido en hardware. Las rutas de fichero son
relativas a `Source/rtl8814au/`. Lo no verificado se marca `?`.

## 1. Veredicto

Sí, por vía software. El árbol ya contiene casi todo el mecanismo necesario — es el mismo que el
back-op del scan enlazado (§5): null data con PM=1, conmutación de canal y ancho, y null data con
PM=0 al volver. El coste de una conmutación se estima en 10–15 ms (§4), dominado por la recarga
de la tabla de potencia TX (~250 escrituras USB): un slot de 4 AW (65,5 ms) aguanta la ida y la
vuelta con ~35–45 ms al aire; un AW solo (16,4 ms) no basta ni para una conmutación.

El MCC de firmware (`CONFIG_MCC_MODE`, `hal/hal_mcc.c`) existe en el árbol y es portable con
esfuerzo moderado, pero depende de que el firmware 8814A implemente los H2C MCC, cosa sin
verificar (§2). La vía manual por slots no depende del firmware.

Nota útil del caso concreto: el primario 153 a 80 MHz tiene canal central 155, así que el bloque
de 80 MHz del AP cubre 149, 153, 157 y 161. Con AWDL en 149 a 80 MHz el centro sigue en 155: el
driver no reescribe el canal de RF y la conmutación se reduce al cambio de subcanal primario
(§4.1), unas 22 operaciones si se omite la recarga de potencia, que con el mismo centro es
redundante.

## 2. MCC: `CONFIG_MCC_MODE`, `hal_mcc.c` y los H2C de firmware

**Existe, pero no está compilado.**

- El código completo está en `hal/hal_mcc.c`, entero bajo `#ifdef CONFIG_MCC_MODE`
  (`hal/hal_mcc.c:15`), con su cabecera `include/rtw_mcc.h`. Esta versión no tiene
  `core/rtw_mcc.c`: las 21 funciones exportadas declaradas en `include/rtw_mcc.h:241-308`
  (`rtw_hal_mcc_*`, `rtw_mcc_cmd_hdl`, `rtw_set_mcc_duration_cmd`, …) están implementadas en
  `hal/hal_mcc.c`.
- Puntos de enganche en el core, todos condicionados: `core/rtw_p2p.c:3450,3615,4524`,
  `core/rtw_cmd.c:3299,5689`, `core/rtw_mlme_ext.c:7634,8071,12948,14942,15221,15523,16402`,
  `core/rtw_tdls.c:197`, además de `rtw_ap.c`/`rtw_xmit.c`. Parámetros de módulo ya presentes:
  `rtw_en_mcc`, `rtw_mcc_duration` (`os_dep/linux/os_intfs.c:871,881,888,897`).
- `CONFIG_MCC_MODE` no está definido en `include/autoconf.h`. La restricción está en
  `include/drv_conf.h:41-57`: exige `CONFIG_CONCURRENT_MODE` y falla la compilación si se define
  `CONFIG_BT_COEXIST` o `CONFIG_TDLS` — ninguno de los dos está definido, así que no hay
  conflicto. `CONFIG_CONCURRENT_MODE` tampoco está definido (`autoconf.h:113` comentado).
- `build.sh` no compila `hal/hal_mcc.c` (la lista `VENDOR_SOURCES` no lo menciona), aunque sí
  compila todos los core que lo llamarían (`core/rtw_mi.c`, `rtw_cmd.c`, `rtw_mlme_ext.c`,
  `rtw_p2p.c`, `rtw_xmit.c`).

**MCC es firmware-offload en esta generación, y el camino genérico ya llega al 8814A:**

- H2C: `include/hal_com_h2c.h:37-45` — `H2C_MCC_LOCATION=0x10`, `H2C_RQT_TSF=0x15`,
  `H2C_MCC_MACID_BITMAP=0x16`, `H2C_MCC_CTRL_V2=0x17`, `H2C_MCC_CTRL=0x18`,
  `H2C_MCC_TIME_SETTING=0x19`, `H2C_MCC_IQK_PARAM=0x1A`. `CONFIG_MCC_MODE_V2` selecciona la vía
  de ICs nuevos (exige IQK en firmware: aviso en `hal/hal_mcc.c:1277`); la vía V1 usa
  `H2C_MCC_CTRL` (`hal/hal_mcc.c:2152`).
- `hal/rtl8814a/` no tiene ni una línea de MCC (cero referencias en `rtl8814a_cmd.c` ni
  `rtl8814a_hal_init.c`; `HW_VAR_MCC` no existe en todo el árbol). Pero el transporte es
  genérico: `hal/hal_mcc.c` envía H2C por `rtw_hal_fill_h2c_cmd`
  (`hal_mcc.c:765,1703,1777,1855,1899,2044,2152,2953`), que en 8814A es `FillH2CCmd_8814`
  (`rtl8814a_hal_init.c:4501`); el C2H de respuesta cae del handler propio de 8814A al genérico
  (`c2h_handler_8814a` devuelve `_FAIL` para ids desconocidos, `rtl8814a_cmd.c:725-750`, y
  `c2h_handler` despacha `C2H_MCC=0x17` → `rtw_hal_mcc_c2h_handler`,
  `hal/hal_intf.c:1241-1244`); y la página reservada se descarga por
  `_rtw_hal_set_fw_rsvd_page` (`hal/hal_com.c:10823`, gancho MCC en `10992-11002`), que
  `rtl8814_download_rsvd_page` ya invoca (`rtl8814a_cmd.c:410`).

**La dependencia real es el firmware.** El comentario de cabecera de los H2C comunes
(`include/hal_com_h2c.h:21`) lista «88e, 8723b, 8812, 8821, 92e use the same FW code base» — el
8814A no está en esa lista y lleva sus propias imágenes (`array_mp_8814a_fw_ap/nic`,
`hal/rtl8814a/hal8814a_fw.c:29,5319`). La inspección del blob (`AWDL_FW_8814A_MCC.md`) cierra la
pregunta: el firmware v33.6 que baja el driver manda los H2C 0x10 y 0x15–0x1A a su ruta de
error y no emite ningún C2H 0x17, así que la vía MCC de firmware no existe con esta imagen.

**Diferencia con el modo concurrente.** `CONFIG_CONCURRENT_MODE` + `core/rtw_mi.c`
(`rtw_mi_status` en `rtw_mi.c:274`, uniones de canal en `include/rtw_mi.h:120-122`) es salto de
canal puro en software del driver; MCC es alternancia programada por el firmware con sincronía
TSF, descarga de los null data (`rtw_hal_mcc_issue_null_data`, `hal/hal_mcc.c:3568`), mapa de
MACID y IQK en firmware. Para el caso AWDL, el plan A es la vía manual (§7); la vía MCC queda
condicionada a verificar el firmware.

## 3. Ruta software: null data con PM, conmutación y retorno

### 3.1 El null data con PM

`_issue_nulldata` (`core/rtw_mlme_ext.c:9521`) construye una trama Null Data: subtype
`WIFI_DATA_NULL` (`set_frame_sub_type`, :9591), `SetToDs` para STA (:9566), el bit PM es el bit
12 del FC (`SetPwrMgt`, :9572-9573; `_PWRMGT_` = `BIT(12)`, `include/wifi.h:269`), addr3 = BSSID
del AP (`get_my_bssid(&pmlmeinfo->network)`, :9583-9585), sin cifrar, a 20 MHz, por la cola de
gestión (`update_mgntframe_attrib`, :9552; `qsel = QSLT_MGNT`, :7632). Con confirmación usa
`dump_mgntframe_and_wait_ack` (:9598-9599), sin ella `dump_mgntframe` (:9601).

`issue_nulldata` (`core/rtw_mlme_ext.c:9616`) resuelve `da == NULL` a la MAC del AP desde
`pmlmeinfo->network` (:9628-9629) y reintenta `try_cnt` veces; `wait_ms == 0` es
fire-and-forget sin esperar confirmación.

El envoltorio multi-interfaz es `rtw_ps_annc` (`core/rtw_mlme_ext.c:14955`): por cada STA
asociada emite `issue_nulldata(iface, NULL, ps, 3, 500)` (:14980) — tres intentos con hasta
500 ms de espera, apto para scan, demasiado para un slot.

### 3.2 LPS de firmware frente a anuncio software

Son dos cosas distintas y complementarias. El LPS del firmware (`rtl8814_set_FwPwrMode_cmd`,
`hal/rtl8814a/rtl8814a_cmd.c:261`, H2C `H2C_SET_PWR_MODE` en :332; modo 0 activo, 1 LPS, 2 WMMPS;
`smart_ps` 0/1 = PS-Poll, 2 = NullData, :314-319) mantiene la radio en el canal casa: el firmware
genera sus nulls/PS-Poll y despierta con las balizas/DTIM. El null PM=1 del driver es el anuncio
puntual de «salgo del canal» que el AP debe registrar antes de que el radio desaparezca; el
PM=0 al volver es el aviso inverso. `LPS_Leave` está en `core/rtw_pwrctrl.c:1246` y por defecto
LPS está apagado (`CONFIG_POWER_SAVING` no definido; `os_dep/linux/os_intfs.c:91`).

### 3.3 Conmutación de canal y ancho

- `set_channel_bwmode` (`core/rtw_wlan_util.c:596`): calcula canal central y
  `chnl_offset80` (:606-615), toma `dvobj->setch_mutex` (:616), pausa TX solo en CAC DFS
  (compilado fuera: `CONFIG_DFS_MASTER` no definido), actualiza `dvobj->oper_channel/bw/offset`
  vía `rtw_set_oper_ch/bw/choffset` (:641-643), contempla backup de IQK bajo MCC/TDLS (:645-655)
  y llama a `rtw_hal_set_chnl_bw` (:657).
- `rtw_set_chbw_cmd` (`core/rtw_cmd.c:2153`) encapsula lo anterior como comando del hilo de
  comandos; la extensión local `RTW_CMDF_CHBW_IDLE_ONLY` (:2176, `include/rtw_cmd.h:49`) llega a
  `rtw_set_chbw_hdl` (`core/rtw_mlme_ext.c:16539`), que es no-op si el fwstate tiene
  `_FW_LINKED|_FW_UNDER_LINKING|_FW_UNDER_SURVEY` (:16551-16553) y si no escribe
  `pmlmeext->cur_channel/offset/bwmode` y llama a `set_channel_bwmode` (:16558-16563). Es la vía
  del aparcamiento actual cuando no hay asociación.
- `rtw_hal_set_chnl_bw` (`hal/hal_intf.c:1001`) calcula `cch_20/40/80` (:1005-1029) y despacha al
  handler de 8814A `PHY_SetSwChnlBWMode8814` (`hal/rtl8814a/rtl8814a_phycfg.c:2340`) →
  `PHY_HandleSwChnlAndSetBW8814A` (:2222; sale temprano si canal, ancho, offset y centro no
  cambian, :2249-2269) → `phy_SwChnlAndSetBwMode8814A` (:2078) → `phy_SwChnl8814A` (:1883) y
  `phy_SetBwMode8814A` (:1660), seguidos de la recarga de potencia TX (:2108-2112) y del IQK
  solo si `pHalData->bNeedIQK` (:2113-2117). No hay `H2C_SET_CHANNEL` ni media-status-rpt en
  esta cadena (el offload `H2C_CHNL_SWITCH_OFFLOAD` exige `RegFWOffload == 2`, :1922-1923).

### 3.4 El retorno

`set_channel_bwmode` no manda null nunca; el PM=0 lo emite el llamador. Precedentes:
`rtw_back_opch` (`core/rtw_mlme_ext.c:15021`: despierta MACID + `rtw_ps_annc(adapter, 0)`), el
back-op del scan (:15387), el fin de scan (:15529) y el keep-alive (:13051, :13089). Entre los
llamadores de `set_channel_bwmode` están la restauración de scan (`survey_done_set_ch_bw`,
`core/rtw_mlme_ext.c:14885`), el back-op (:15384), el join (`rtw_join_done_chk_ch`, :16328), el
keep-alive (:13040, :13059), sreset (`core/rtw_sreset.c:172`), arranque de AP, DFS, P2P, TDLS y
CSA.

## 4. Coste de una conmutación en 8814A

**Retardos bloqueantes en la ruta: cero (verificado).** No hay `mdelay`/`udelay`/`msleep` en
`hal/rtl8814a/rtl8814a_phycfg.c` (incluidas `phy_SwChnl8814A`, `phy_SetBwMode8814A` y la
escritura RF directa :87-210), ni en `rtl8814a_rf6052.c`, `rtl8814a_dm.c` ni `hal_intf.c`. La
escritura RF del 8814A es una escritura BB directa al `rf3wire` (:155), sin bucles de espera.
Todo el coste es, pues, tráfico USB de control.

**Recuento de escrituras por conmutación 153/80 → 149/20 (4 rutas RF, `NumTotalRFPath`, :1937):**

| Trabajo | Escrituras | Dónde |
|---|---|---|
| Cambio de canal | 1 BB (`rFc_area`) + 4×(1 lectura + 1 escritura `RF_CHNLBW`) + 1 BB (AGC 0x958) ≈ 10 ops | `phy_SwChnl8814A` :1922-1964 (RF :1953-1955, AGC :1959-1963) |
| Cambio de ancho | TRXPTCL 0x8a0 (1R+1W), DATA_SC, ADC bw, AGC bw, casos 40/80 MHz, 4 rutas RF (4R+4W), spur NBI ≈ 21-22 ops | `phy_SetBwMode8814A` :1660-1717; `PHY_RF6052SetBandwidth8814A` (`rtl8814a_rf6052.c:43-62`); `phy_SpurCalibration_8814A` :1464 |
| Recarga de potencia TX | 62 rates × 4 rutas ≈ 252 escrituras BB a 0x1998 (doble para MGN_1M) | :2108-2112 → `PHY_SetTxPowerLevel8814` (:636) → `phy_set_tx_power_level_by_path` (`hal/hal_com_phycfg.c:3044-3081`), `PHY_SetTxPowerIndex_8814A` (`rtl8814a_phycfg.c:750-758`) |

Total ≈ 280-300 operaciones USB por conmutación; el ~90 % es la recarga de potencia.

**Transporte USB.** `rtw_write32` → `usbctrl_vendorreq`
(`os_dep/linux/usb_ops_linux.c:29-215`): `usb_control_msg` síncrono por EP0, serializado por
`usb_vendor_req_mutex` (:70), reintentos limitados y timeout de 500 ms solo en fallo. La latencia
real por operación no está en el código: 10-100 µs por vendor request es una estimación habitual.

**Estimación total por conmutación: ≈ 3-30 ms, punto medio 10-15 ms** (estimación; la parte
probada es el recuento de operaciones y la ausencia de retardos). El IQK no corre por
conmutación: `bNeedIQK` solo lo fija `HW_VAR_DO_IQK` (`hal/hal_com.c:12158-12162`) desde TDLS,
connect/scan, CAC, arranque de AP o sreset; el retrigger periódico de IQK del watchdog está
desactivado (`#if 0`, `hal/phydm/halrf/halphyrf_ce.c:1159`) y `halrf_watchdog` no hace IQK ni
LCK (`hal/phydm/halrf/halrf.c:1330-1341`). Si se forzara un IQK completo serían ~100 ms típicos y
~300 ms en el peor caso (delays LOK 1 ms y WBIQK 10 ms por shot, `hal/phydm/halrf/rtl8814a/halrf_iqk_8814a.c:219-405`,
`hal/phydm/halrf/halrf_iqk.h:30-31`) — otra razón para no dispararlo por slot.

**Frente al presupuesto AWDL** (AW = 16 TU = 16,384 ms; slot de 4 AW = 65,5 ms):

- Una conmutación ≈ 2/3 de un AW. Ida + vuelta ≈ 20-30 ms de los 65,5 ms del slot: cabe, con
  ~35-45 ms al aire. Conmutar más de una vez por slot desborda el presupuesto.
- La phydm se entera del canal nuevo por los punteros `ODM_CMNINFO_CHNL/BW`
  (`hal/hal_dm.c:443-444`) y actúa en el siguiente tick del watchdog (DIG, CCA, CFO), sin
  bloquear la conmutación.
- Palanca de optimización clara: la recarga de potencia (≈ 252 escrituras) es el 90 % del coste;
  omitirla o acortarla cuando el canal destino ya está cargado reduciría el switch a ~2-4 ms.

### 4.1 Variante dentro del bloque de 80 MHz: 149/80 en vez de 149/20

Si AWDL usa el 149 a 80 MHz en lugar de a 20 MHz, la ida y la vuelta no cambian el canal de RF:

- `set_channel_bwmode` calcula el centro con `rtw_get_center_ch` (`core/rtw_wlan_util.c:549-581`):
  149, 153, 157 y 161 a 80 MHz dan siempre 155 (:564-565). El `chnl_offset80` sale de comparar
  centro y primario (:608-615): LOWER para 149 y para 153. Solo cambia el offset de 40 MHz, que es
  LOWER para 149 y UPPER para 153.
- `rtw_hal_set_chnl_bw` recibe el centro (`core/rtw_wlan_util.c:657`, "set center channel"), y
  `PHY_HandleSwChnlAndSetBW8814A` solo marca `bSwChnl` si `current_channel` cambia
  (`hal/rtl8814a/rtl8814a_phycfg.c:2249-2255`). Con el centro en 155 no hay escritura de
  `RF_CHNLBW` ni de AGC por canal; queda `bSetChnlBW` por el cambio de `nCur40MhzPrimeSC`
  (:2258-2266), es decir, `phy_SetBwMode8814A` (≈ 22 operaciones).
- La recarga de potencia (:2110-2112) se llama con `pHalData->current_channel`, que sigue en 155:
  escribe los mismos valores que ya hay. Omitirla cuando `current_channel` no cambió es
  equivalente, y deja la conmutación en las ≈ 22 operaciones del ancho (≈ 1-3 ms con la misma
  estimación por operación del §4).
- TX a 20 MHz dentro de 80 MHz: el descriptor elige el subcanal de 20 según el primario y los
  offsets (`hal/rtl8814a/rtl8814a_xmit.c:473-489`), así que las tramas salen en el primario de cada
  momento. Que la ruta de gestión que usa AWDL pase por esa misma asignación es `?`.
- El iPhone anuncia `{44,128}` en sus slots de 5 GHz (`AWDL_AF_VS_IPHONE.md` §3): la clase 128 es
  80 MHz, así que el 149 a 80 MHz coincide con lo que hace un par de Apple en el 149.

Con el router en un canal de otro bloque (por ejemplo 36 a 80 MHz), la conmutación vuelve a ser la
completa del §4.

## 5. El mecanismo existente más cercano: el back-op del scan enlazado

Es, tal cual, el ciclo null → salir → volver → null que AWDL necesita, con la única diferencia de
que el dwell en casa lo mide el scan y no un horario de slots.

**Definiciones.** `enum ss_backop_flag`: `SS_BACKOP_EN` (BIT0, «backop when linked»),
`SS_BACKOP_EN_NL` (BIT1), `SS_BACKOP_PS_ANNC` (BIT4), `SS_BACKOP_TX_RESUME` (BIT5) en
`include/rtw_mlme_ext.h:186-192`. Estado en `struct ss_res` (`pmlmeext->sitesurvey_res`,
`include/rtw_mlme_ext.h:194-239`): `backop_ms` (:221), `scan_cnt`, `scan_cnt_max`,
`backop_time`. Máquina de estados `enum SCAN_STATE` (`include/rtw_mlme_ext.h:160-182`).

**Flujo** (`sitesurvey_cmd_hdl`, `core/rtw_mlme_ext.c:15156`; el timer `survey_timer_hdl`
:13160-13188 re-encola el comando):

1. `SCAN_START` (:15223-15254): drena las colas hardware con `HW_VAR_CHECK_TXBUF`
   (`hal/rtl8814a/rtl8814a_hal_init.c:3739`: fija `REG_RETRY_LIMIT` SRL=LRL=1 y sondea las
   páginas FIFO cada 10 ms **hasta 2000 ms**), duerme los MACID usados
   (`rtw_hal_macid_sleep_all_used`, :15240) y anuncia PS con `rtw_ps_annc(padapter, 1)` (:15236),
   esperando 50 ms («delay 50ms to protect nulldata(1)»).
2. `SCAN_PROCESS`: `site_survey` (:14815) conmuta con `set_channel_bwmode` (:14822) a 20 MHz por
   canal, lanza probereqs y duerme `scan_ch_ms`; el decisión de back-op vive en
   `rtw_scan_backop_decision` (:14470): back-op solo si hay interfaces enlazadas y la política
   lleva `SS_BACKOP_EN`; tras `scan_cnt >= scan_cnt_max` → `SCAN_BACKING_OP` (:14747-14757).
3. `SCAN_BACKING_OP` (:15355-15405): vuelve al canal casa con `set_channel_bwmode` (:15384),
   restaura MSR, despierta MACID y, con `SS_BACKOP_PS_ANNC`, emite `rtw_ps_annc(padapter, 0)`
   (:15387-15389); con `SS_BACKOP_TX_RESUME` reprograma TX (`rtw_mi_os_xmit_schedule`, :15401).
   En `SCAN_BACK_OP` (:15400-15406) espera `backop_ms` (o `scan_abort`) y vuelve a
   `SCAN_LEAVING_OP`.
4. `SCAN_COMPLETE` (:15489-15553): `survey_done_set_ch_bw` (:14885), restauración de phydm,
   `rtw_ps_annc(padapter, 0)` (:15529), `rtw_rx_ampdu_apply`.

**Valores por defecto.** `backop_ms = RTW_BACK_OP_CH_MS` = 400 ms y
`scan_cnt_max = RTW_SCAN_NUM_OF_CH` = 3 (`include/rtw_mlme_ext.h:300-307`; inicialización en
`core/rtw_mlme_ext.c:1081-1082`; editable por debugfs, `core/rtw_debug.c:1451-1489`). Dwell por
canal `SURVEY_TO` = 100 ms.

**Matiz importante.** La política de una STA pura enlazada es
`/*SS_BACKOP_EN|*/SS_BACKOP_PS_ANNC | SS_BACKOP_TX_RESUME` (`core/rtw_mlme_ext.c:1073`): el
`SS_BACKOP_EN` está comentado, así que una STA asociada no hace back-op periódico — hace una
pasada completa fuera de canal con un null PM=1 al principio y PM=0 al final. El ciclo periódico
BACKING_OP↔BACK_OP solo está activo cuando hay un AP/mesh arriba (política con `SS_BACKOP_EN`,
:1075). Reutilizar el mecanismo para AWDL significa, en esencia, reactivar ese ciclo con
`backop_ms` como dwell en casa.

**TX/RX durante el fuera de canal.** TX se bloquea en software:
`rtw_is_xmit_blocked`/`rtw_xmit_ac_blocked` (`core/rtw_xmit.c:5713-5764`) cortan cuando
`rfctl->offch_state != OFFCHS_NONE` (:5740) o el estado de scan no está en
{`SCAN_DISABLE`, `SCAN_BACK_OP`} (:5747-5755); el tasklet de xmit lo consulta en
`select_and_dequeue_pending_xmitbuf` (:5604). RX: `hw_var_set_mlme_sitesurvey`
(`hal/hal_com.c:11353`) cierra `REG_RXFLTMAP2` (se descartan tramas de datos), ajusta RCR
(`rtw_hal_rcr_set_chk_bssid`) y desactiva la actualización de TSF, restaurando al terminar; el
IGI baja en scan (`sitesurvey_set_igi`, :15038) y los AMPDU se restringen (`rtw_rx_ampdu_apply`,
:15234).

**Entradas y guardas.** `rtw_sitesurvey_cmd` (`core/rtw_cmd.c:855-913`) exige
`pmlmepriv->lock` (:851-853), saca el LPS con `rtw_lps_ctrl_wk_cmd(LPS_CTRL_SCAN)` (:865) y arma
el watchdog `mlme_set_scan_to_timer` con `rtw_scan_timeout_decision` (:899-901). Las
transiciones de canal fuera de operación están serializadas por `rfctl->offch_mutex`
(`sitesurvey_set_offch_state`, :15145-15171).

## 6. Riesgos

1. **Bajo — el AP no expulsa por una ausencia de 65 ms.** `linked_status_chk`
   (`core/rtw_mlme_ext.c:12907`) corre cada 2 s en el worker de comandos
   (`core/rtw_cmd.c:3257`; `core/rtw_mlme.c:3302`). `chk_ap_is_alive` (:12728-12751) considera
   silencio solo si los deltas de tramas de datos, balizas y proberesp dirigidas a la estación
   son todos cero en la ventana de 2 s (las balizas se cuentan en `core/rtw_recv.c:2020`); con
   `rx_chk_limit = 4` (`core/rtw_sta_mgt.c:315-319`) el `receive_disconnect`
   (:13093-13102, :11560-11582) llega tras ~5 chequeos silenciosos consecutivos (~10 s), y el
   sondeo por null (`link_count_limit = 7`, ~16 s) aún más tarde. 65 ms es el 0,6 % de una sola
   ventana. `?`: cómo reacciona el firmware a DTIM perdidos cuando LPS estuviera activo.
2. **Medio — `rtw_leave_opch` puede bloquear segundos dentro de su mutex.**
   (`core/rtw_mlme_ext.c:14993-15018`, bajo `rfctl->offch_mutex`): `HW_VAR_CHECK_TXBUF`
   sondea hasta 2000 ms y el null de `rtw_ps_annc` son 3 intentos × 500 ms → peor caso ~3,5 s,
   incompatible con un slot de 65,5 ms si se ejecuta en línea. El tick debe usar una variante
   ligera (null fire-and-forget, sin drenado de FIFO; ver §7).
3. **Alto — un scan concurrente roba el canal y apaga la RX de datos.**
   `hw_var_set_mlme_sitesurvey` (`hal/hal_com.c:11353`) pone `REG_RXFLTMAP2 = 0` (todo dato se
   descarta) y barre canales con `set_channel_bwmode` (:14822); el core vendor no protege nada
   por sí solo. En el kext las compuertas existentes lo mitigan, pero cualquier ruta nueva
   (airportd escaneando, wifip2pd) debe respetar `offch_mutex` y el estado de slots.
4. **Medio — el keep-alive activo puede rebotar la radio a mitad de slot.** La rama
   :13020-13060 de `linked_status_chk` detecta `oper_ch != cur_channel`, guarda el canal
   respaldado, conmuta al canal casa, emite probereq + nulls (con esperas) y restaura el canal
   respaldado (:13036-13059). Si un chequeo cae dentro de un slot AWDL, intercalado con el tick,
   rompe la temporización. Hay que guardarle el paso mientras el slot está activo.
5. **Bajo — LPS apagado por defecto; si se habilita, `rtw_leave_opch` no sale de LPS.** Con
   `CONFIG_POWER_SAVING` sin definir, `power_mgnt = PS_MODE_ACTIVE` y
   `bLeisurePs = _FALSE` (`os_dep/linux/os_intfs.c:91`; `core/rtw_pwrctrl.c:2204-2205`). La ruta
   de scan sí sale del LPS (`LPS_CTRL_SCAN`, `core/rtw_cmd.c:865` → `core/rtw_pwrctrl.c:3333`
   del handler); `rtw_leave_opch` no tiene llamada equivalente. `?` si el firmware en LPS
   respondería en el canal equivocado durante la conmutación.
6. **Bajo — sreset y los `cur_*`.** Un colgado de TX con `REG_TXDMA_STATUS != 0` o sin
   completarse en >4 s dispara `sreset_reset` (`hal/rtl8814a/rtl8814a_sreset.c:30-67`;
   `rtl8814a_hal_init.c:4487`), y la restauración usa `pmlmeext->cur_*`
   (`core/rtw_sreset.c:172`). Por eso el tick no debe pasar por `rtw_set_chbw_hdl`
   (:16551 no-op enlazado; :16558-16560 machacaría `cur_*` con 149/20 y corrompería la
   restauración): conmutar con `set_channel_bwmode` directamente.
7. **Medio — triple estado de canal que puede divergir.** `pHalData->current_*`
   (`rtl8814a_phycfg.c:2275-2303`), `dvobj->oper_*` (`rtw_wlan_util.c:641-643`) y
   `mlmeextpriv.cur_*` lo escriben caminos distintos. El tick debe pasar los triples exactos —
   salida (149, DONT_CARE, 20 MHz), retorno (153, UPPER, 80 MHz) — y `set_channel_bwmode`
   mantiene `oper_*` coherente por sí solo. El early-return de
   `PHY_HandleSwChnlAndSetBW8814A` (:2249-2269) hace la operación idempotente si nada cambia.
8. **Nota de diseño — la recarga de potencia por conmutación.** ≈ 252 escrituras USB por switch
   (§4). Omitirla por slot es una apuesta sobre la precisión de los límites de regulación por
   canal (149 y 153 comparten banda pero no entradas de tabla), no una necesidad estructural:
   hay que medir (§8).
9. **RX en 149: el gancho está bien; el riesgo está en los filtros y en el scan.** El hook local
   `validate_recv_frame` (`core/rtw_recv.c:2310-2319`) consume antes de la validación toda
   trama no-CTRL con A3 = BSSID AWDL y la entrega a la ABI del radio; sin gancho no cambia
   nada. Con el puerto 1 activo, `RCR_CBSSID_DATA` se mantiene a cero
   (`hal/hal_com.c:3829`) y `RCR_CBSSID_BCN` activo mientras la STA está enlazada (:3844-3850):
   las balizas ajenas las filtra el hardware y las action frames AWDL pasan. Las tramas de
   infraestructura que lleguen durante el slot se pierden en el aire — es el coste esperado del
   tiempo compartido. El backlog USB de RX no se ve afectado: `usb_read_port`/`usb_recv_tasklet`
   (`os_dep/linux/usb_ops_linux.c:748-1060`) no tienen flush por cambio de canal.

## 7. Propuesta

### 7.1 Qué reutilizar

Un par de funciones nuevas en el vendor tree que repliquen el esqueleto del back-op sin pasar por
el comando de scan:

- **`awdl_slot_leave(adapter)`** (ida al slot):
  1. Marcar fuera de canal: poner `rfctl->offch_state` a un valor propio del tick (o reusar
     `OFFCHS_BACKING_OP`) bajo `rfctl->offch_mutex` — `rtw_is_xmit_blocked`
     (`core/rtw_xmit.c:5740`) bloqueará el TX software solo.
  2. Anunciar PS con un null ligero: `issue_nulldata(iface, NULL, 1, 1, 0)` — fire-and-forget,
     el patrón de :13087. Nada de `rtw_ps_annc` (3 × 500 ms) ni de `HW_VAR_CHECK_TXBUF`
     (hasta 2000 ms): las tramas en vuelo hacia el AP terminan solas durante la conmutación.
  3. Conmutar: `set_channel_bwmode(adapter, 149, HAL_PRIME_CHNL_OFFSET_LOWER, CHANNEL_WIDTH_80)`
     cuando el canal casa está en el bloque 149-161 a 80 MHz (§4.1); si no,
     `set_channel_bwmode(adapter, 149, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20)`. Toma
     `setch_mutex`, actualiza `oper_*`, no toca `cur_*` ni `offch_mutex`.
  4. Servir el slot (TX mgmt/datos y RX AWDL ya implementados por la ABI del radio).
- **`awdl_slot_return(adapter)`** (vuelta a casa):
  1. `set_channel_bwmode(adapter, 153, HAL_PRIME_CHNL_OFFSET_UPPER, CHANNEL_WIDTH_80)`.
  2. `issue_nulldata(iface, NULL, 0, 1, 0)` (PM=0) — precedente :13089 y `rtw_back_opch`
     (:15021).
  3. Liberar `offch_state` a `OFFCHS_NONE` bajo `offch_mutex` y reprogramar TX
     (`rtw_mi_os_xmit_schedule`, precedente :15401).

Ambas deben ejecutarse en el hilo de comandos o en un workitem propio (no en el contexto del
tick si este debe seguir midiendo horarios), y respetar `offch_mutex` frente a scan/ROC y
`setch_mutex` frente a cualquier otra conmutación (únicos usuarios actuales: TDLS chsw,
compilado fuera).

### 7.2 Enganche desde el tick

Nueva llamada de la ABI radio (familia `RTL_RADIO_FN_AWDL_*`), p. ej.
`RTL_RADIO_FN_AWDL_SLOT(ch, offset, bw, enter)` o la pareja `LEAVE`/`RETURN`, invocada por el
tick AWDL existente en los bordes de los slots servidos del 129 cuando hay asociación en
infraestructura. El aparcamiento actual (`RTW_CMDF_CHBW_IDLE_ONLY`) queda para el caso sin
asociación; con asociación, el tick conmuta 153/80 ↔ 149/20 por slot en lugar de aparcar. Un
boot-arg (p. ej. `rtlawdl_mcc=0`) para desactivar el tiempo compartido y quedarse en modo
monocanal. El keep-alive de `linked_status_chk` (:13020-13060) debe saltarse la rama de
restauración mientras el tick tiene el canal fuera (guardia sobre `offch_state`, que esa rama ya
podría consultar).

### 7.3 Optimización del coste

Con la estimación de §4, la recarga de potencia es el 90 % del switch. Opciones, en orden de
intrusión: (a) medir primero sin tocar nada; (b) recargar solo las rates activas en slot (los
AWDL van a OFDM 6/12); (c) cachear por canal: omitir la recarga si el canal destino ya se cargó
hace menos de N slots. La (b) es local a `PHY_SetTxPowerLevel8814`/`phy_set_tx_power_level_by_path`
(`hal/hal_com_phycfg.c:3044-3081`).

### 7.4 Vía MCC de firmware, condicional

Compilar `CONFIG_MCC_MODE + CONFIG_CONCURRENT_MODE` y añadir `hal/hal_mcc.c` a `build.sh` es el
camino corto a la alternancia con TSF y null data descargados al firmware — pero solo si el
firmware 8814A responde a los H2C MCC. No responde (§2, `AWDL_FW_8814A_MCC.md` §7): la
prueba en hardware ya no hace falta y la vía manual de §7.1-7.2 es la única.

## 8. Qué medir en hardware

1. **Latencia real de conmutación.** Marcas de tiempo alrededor de `set_channel_bwmode` en ida y
   vuelta (ida 153/80→149/20 y vuelta), con y sin recarga de potencia: cerrar la estimación de
   10-15 ms de §4 y el peso de las ~252 escrituras.
2. **Salud del enlace de infraestructura durante slots repetidos.** Ping a 10 ms y tráfico hacia
   el AP durante minutos con slots de 4 AW servidos periódicamente: RTT, jitter, % de pérdida,
   y asociación estable durante horas (riesgo 1).
3. **Contadores de keep-alive.** Deltas de datos/balizas/proberesp que ve `chk_ap_is_alive`
   con y sin AWDL activo, para confirmar que ninguna ventana de 2 s queda en silencio total.
4. **Que el IQK no se dispara** tras conmutaciones repetidas 153↔149 (`bNeedIQK` permanece a 0)
   y que la potencia TX medida en cada canal no degrada; si se prueba la omisión de la recarga,
   comparar potencia y EVM con y sin ella en 149 y 153.
5. **RX AWDL con enlace activo.** Contadores de AF/PSF/MIF del slot con un iPhone de referencia
   mientras `en2` está asociado, comparados con la sesión sin asociar (af 0 sin pares en la
   prueba del 2026-10-30): confirmar que el tiempo compartido no cuesta recepción dentro del
   slot.
6. **Colas TX.** Frames retenidos por el bloqueo software de `offch_state` y su latencia total
   (backlog al volver), y verificar que `HW_VAR_CHECK_TXBUF` no se invoca por slot (riesgo 2).

Red en el primario 157 a 80 MHz (bloque 149–161, mismo bloque que el social 149, así que sólo se mueve
el primario y sin recarga de potencia), AirLayer `3699d28e` con capacidad 29, sin pares.
| Driver | Ida al 149 | Vuelta a la BSS | Media / máximo (1024 PSF) | Fallos | Ping al gateway |
|---|---|---|---|---|---|
| `d5d828f7` (espera fija de 2 ms tras el null) | 4357 y 4355 µs | 2389 y 2415 µs | 3381 / 5407 µs (1464 conmutaciones) | 0 | 20/20, 0,62 ms |
| `69dfea33` (`96e80cb`, espera acotada a URB y MGQ) | 2522 y 2646 µs | 2367 y 2361 µs | 2459 / 4668 µs (1445 conmutaciones) | 0 | 20/20, 0,63 ms |
Con `d5d828f7`, tras 2 h 14 min: 26.257 conmutaciones sin fallo, máximo 8928 µs, media 3394 µs, 30/30
pings. La estimación de 10–15 ms del §4 queda muy por encima de lo medido en el caso de mismo bloque;
el caso con cambio de bloque (recarga de potencia) sigue sin medir `?`.

Ping al gateway cada 100 ms y `networkQuality` simultáneo, con la red en 157/80:
| Prueba | Estado AWDL | Capacidad bajada / subida | Ping | > 5 ms | Máximo |
|---|---|---|---|---|---|
| Carga, AWDL en reposo | 31 (149 en 2 de 16 franjas) | 762 / 604 Mbps | 900/900 | 39 | 6,7 ms |
| Carga, ventana AirDrop recién abierta | 45 → 3 → 31 | 772 / 605 Mbps | 600/600 | 39 | 7,1 ms |
| 10 min sin carga, AirDrop reabierto cada 2 min | 24/45/3/31 | — | 6000/6000 | 4 | 6,6 ms |
Los picos de 5–7 ms se concentran en los primeros ~14 s tras abrir AirDrop (estado 45, el 149 en 10
de 16 franjas) y cuando coinciden con carga. No hubo pérdidas ni caída de capacidad medible. En los
10 minutos: 2886 conmutaciones más sin fallo (media 2462 µs, máximo 5594 µs).
Datos por `awdl0`: las 6 tramas descartadas por antigüedad son las que macOS manda por mDNS al
arrancar, mientras la secuencia sólo tiene el 6 o está en reposo. Con la red en 5 GHz la capa sólo
sirve las franjas del social de esa banda (149), no las del 6, y una trama que espera más de 1,2 s
se descarta. En los 10 minutos con cinco aperturas de AirDrop el contador no subió (6 → 6).

Corrección (2026-11-01): `networkQuality` corrió sin `-I` y sale por la primera ruta por defecto,
que es `en1` (cable), no `en2`. La columna de capacidad no mide el enlace Wi-Fi: 762/604 Mbps
coincide con `networkQuality -I en1` del 01-11 (769/604 Mbps). Para el ping no quedó registrada la
interfaz; la correlación de los picos con el estado 45 apunta a `en2`, sin confirmar `?`.
Medición forzada por `en2` (`networkQuality -I en2`, `ping -b en2`), red en 161/80, −43/−79 dBm,
650 Mbps MCS 7, `awdl0` levantado y sin AirDrop abierto:
| Prueba | Capacidad bajada / subida | Latencia en reposo | Ping al gateway |
|---|---|---|---|
| `en2` | 310 / 145 Mbps | 90 ms | 0 % pérdida, base 1–2 ms, ráfagas de 12–70 ms cada 5–6 s |
| `en2`, `awdl0` bajado (`ifconfig awdl0 down`) | 321 / 208 Mbps | 48 ms | base 1–3 ms, sin ráfagas: 0 de 300 sobre 20 ms en 90 s |
| `en1` (referencia) | 769 / 604 Mbps | 48 ms | — |
Con `awdl0` bajado desaparecen las ráfagas periódicas y la subida pasa de 145 a 208 Mbps; la bajada
no cambia (310 → 321 Mbps, con la tasa en 527–650 Mbps, MCS 6–7). Las ráfagas de 12–70 ms vienen
del tiempo compartido con AWDL, aunque no hubiera AirDrop abierto: con `rtlawdl_mcc=1`
`mccChannel` sirve la primera franja de cada tramo del 149 y `txTick` deja la radio allí la franja
entera (4 AW × 16 TU = 64 TU, 65,5 ms); con el 149 en 2 de 16 franjas son unos 131 ms fuera de la
BSS por periodo de 1,05 s (12,5 %), y el 12 % de los pings cada 250 ms pasó de 20 ms. Las ráfagas
cada 5–6 s son el batido entre el intervalo del ping y el periodo de 16 franjas.

Con `0caa86e` (`rtlawdl_mccaw`, 1 AW por defecto: la radio vuelve a la BSS tras el primer AW de la
franja si no queda dato, UMI ni MIF pendiente), AirLayer md5 `c3489552`, mismo enlace (161/80,
−44/−79 dBm, 650 Mbps MCS 7), `awdl0` levantado y sin AirDrop abierto:
| Prueba | Antes (franja entera) | Con 1 AW |
|---|---|---|
| Ping al gateway cada 250 ms | 12 % sobre 20 ms, máximo 70 ms | 0 de 480 sobre 20 ms, 15 de 480 sobre 10 ms, máximo 15,7 ms |
| `networkQuality -I en2` bajada | 310 Mbps | 304–333 Mbps (4 corridas) |
| `networkQuality -I en2` subida | 145 Mbps | 151–233 Mbps (4 corridas) |
La subida varía entre corridas tanto como la diferencia medida antes con `awdl0` bajado (145 frente
a 208 Mbps), así que esa diferencia no es significativa con una sola corrida por caso. En los
primeros ~10 s de medición tras el arranque hubo dos cortes de 2,7 y 3,5 s (pings encolados que
salen juntos), como el visto al bajar `awdl0`; ninguno en los 3 min siguientes.

Origen de los cortes: son escaneos, no AWDL. Un escaneo forzado con CoreWLAN
(`scanForNetworks`, 7,5 s) produce dos cortes de 3,4 s separados por un solo ping normal. Asociado,
el driver no vuelve a la BSS durante el escaneo: `init_mlme_ext_priv` deja `SS_BACKOP_EN` comentado
en los flags de STA (`mlmeext_assign_scan_backop_flags_sta`, `rtw_mlme_ext.c`), así que recorre los
31 canales seguidos a `SURVEY_TO` (100 ms) cada uno, unos 3,1 s fuera más las conmutaciones. Además
`setWCL_SCAN_REQ` (`RTLAirLayer.cpp`) ignora la lista de canales de la petición y cada petición
escanea todo; dos peticiones seguidas dan los dos cortes. Con `SS_BACKOP_EN` los valores del
fabricante (`RTW_SCAN_NUM_OF_CH` 3, `RTW_BACK_OP_CH_MS` 400) dejan como máximo ~300 ms fuera y
alargan cada escaneo a unos 7,5 s, por debajo del tope de 10 s del sondeo de `RTLAirController::poll`.

Con `dba50e6` (`SS_BACKOP_EN` en STA), driver md5 `5f5cf71b`, AirLayer `c3489552`, medido sobre
`en2` (161/80, −44/−76 dBm):
| Caso | Antes | Con back-op |
|---|---|---|
| Un escaneo (`scanForNetworks`) | 1 corte de 3,4 s (o 2 con dos peticiones) | 9,5 s de ciclo: cortes de 250–494 ms cada ~0,9 s, 1–2 ms entre ellos |
| Arranque, airportd pide escaneos de 2–3 canales seguidos | cortes de 3,4 s | ~75 s de ciclo (8 escaneos de 9,5 s, 4 ms entre uno y otro) |
| Sin escaneo (90 s) | — | 0 de 360 pings sobre 20 ms, máximo 18 ms |
El corte máximo (~0,5 s) supera los ~300 ms de 3 × `SURVEY_TO`: se suman el vaciado de cola
(`HW_VAR_CHECK_TXBUF`), los 50 ms de protección del null PM=1 y las conmutaciones. Ningún corte
pasa de 0,5 s, pero el tiempo total fuera por escaneo sube de 3,4 a ~5 s. Las peticiones de
airportd traen 2, 3 o 21 canales (anillo `RTLAirLayerLog`, «WCL scan request, N channels») y cada
una recorre los 31; respetar esa lista dejaría una petición de 3 canales en ~0,3 s fuera.
Una segunda petición pegada a la anterior (21 canales) terminó por el tope de 2,75 s del sondeo
sin corte visible en el ping, lo que indica que el driver no la llegó a escanear `?`.

Con `ef52dda` (driver md5 `d91214e5`, AirLayer `5d175719`) cada petición escanea sólo sus canales
de 2,4 y 5 GHz. Las marcas de banda de las entradas (+8) quedan confirmadas en el anillo:
`0x8a` en 2,4 GHz (`APPLE80211_C_FLAG_2GHZ` 0x8), `0x92` en 5 GHz (0x10) y `0x112` en 5 GHz DFS
(0x100 además).
| Caso | Peticiones de airportd | Ping por `en2` |
|---|---|---|
| Arranque | 8 de 2–3 canales (9/10/36, 40/44/48, 149/153/157, 161/165/52, 56/60/64, 100/104/108, 112/116/132, 136/140) | un corte de 370–435 ms por petición, cada ~2,5 s (mínimo del sondeo enlazado de `poll`); antes, ~75 s de ciclo |
| `scanForNetworks` | 11 canales de 2,4 GHz y 21 de 5 GHz: todo el plan | 9,3 s de ciclo con cortes de hasta 475 ms, 0 % pérdida; igual que antes, porque pide todos los canales |
Cada corte de ~0,4 s para 3 canales son los 3 × `SURVEY_TO` más el vaciado de cola, los 50 ms del
null PM=1 y las conmutaciones. Hubo un único corte de 3,5 s en
`en2` poco después de bajar `awdl0` (14 pings encolados que salieron juntos), que no se repitió en
los 60 s siguientes; su causa no está aislada `?`. `ifconfig en2` y `ifconfig awdl0` devuelven
`SIOCGIFXMEDIA: Input/output error`.

### 8.1 Permanencia adaptativa (2026-11-05)

Con 1 AW por franja y un par presente, IO80211 elimina al par aunque el cambio de canal llegue a
tiempo. `IO80211AWDLPeer::monitorPeer` (`0xffffff80021a9294`) llama a `shouldRemovePeerFromNDP`
(`0xffffff80021a9c50`). Para un par corriente en caché, la eliminación ocurre si pasan más de
6000 ms desde su última action frame, o si no hay franjas solapadas durante más de 2000 ms. Otros
tipos de par usan otros límites. Después, `setPeerNotReachable` lleva a
`cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`), que publica `Peer Absence`.

Medido con el par virtual y la capa `1320BA53`: 60 de 67 ausencias llegan tras un hueco de 5 s o
más sin tramas del nodo, casi todas de 6-7 s, y los huecos de 7-8 s se repiten cada ~14,5 s, igual
que las ausencias. Falta confirmar cuál de las dos condiciones se cumple, porque no se leyó
directamente; la correlación apunta a la de 6 s `?`.

Volver siempre a la franja entera devolvería el coste medido arriba a cualquier equipo con AWDL
activo, aunque no tenga ningún par cerca. Por eso la permanencia ahora depende de los pares: 1 AW
sin pares, y la franja entera durante 30 s desde la última action frame de otro equipo.
`rtlawdl_mccaw=1..4` sigue fijando el valor. La línea periódica `tx psf` informa el valor en uso
(`dwell`).

Capa md5 `7165bacc`, UUID `9CA670C1`, enlace en 161/80, nodo de `awdlsim` sin conexión por aire,
ping al gateway cada 200 ms:
| Medida | Sin nodo (`dwell 1`) | Con nodo (`dwell 4`) |
|---|---|---|
| Pings sobre 20 ms | 5 de 300 | 34 de 300 |
| Pings sobre 50 ms | 0 de 300 | 16 de 300 |
| Latencia media y máxima | 2,8 y 26 ms | 7,6 y 75 ms |
| Pérdida | 0 de 300 | 0 de 300 |

Con el nodo presente, en 240 s de reposo entra el 12,9 % de sus tramas (`inj ok` +738, `offch`
+5006), sin ningún segundo sin tramas. Antes entraba el 1,6 %. Hubo 1 `Peer Absence` en 8 min 43 s,
frente a una cada ~14,5 s. Llegó a las 13:23:27, al terminar dos escaneos seguidos de 3,25 y 3,5 s.
Un escaneo suelto de 2,75 s no causó ninguna. La relación con los escaneos se deduce de los tiempos
y de los 6 s del límite `?`. Otra corrida de 300 pings con el nodo, que incluyó esos escaneos, llegó
a 47 de 300 sobre 20 ms y a un máximo de 616 ms.

Los 5 pings sobre 20 ms sin nodo contrastan con los 0 de 480 de `c3489552` (ping cada 250 ms, sin
cambio anticipado). La causa de esa diferencia no está aislada `?`.

### 8.2 Slots durante el back-op del escaneo (2026-11-05)

Escaneo que provocó la ausencia de §8.1, reconstruido del registro:
| Hora | Hecho |
|---|---|
| 13:23:20,613 | `configd` publica de nuevo el IPv6 de `en2` |
| 13:23:20,618 | `airportd` pide un escaneo completo (`handleIPStatusNotification`, «Full band scan request on successful association and IP acq.») |
| 13:23:20,622 | Empieza la petición de 11 canales |
| 13:23:23,879 | Empieza la de 21 canales, 5 ms después de acabar la anterior |
| 13:23:26,999 | `Peer Absence`, 6,38 s después de empezar |
| 13:23:27,381 | Termina el escaneo |

El nodo no recibió ninguna PSF ni entró ninguna trama suya entre los segundos 402 y 407 de su registro.
Con la radio asociada, `txTick` no servía ningún slot mientras durara un escaneo. El escaneo completo
apareció a las 13:14:38, las 13:23:20 y las 13:31:34; con tres muestras no se puede afirmar un periodo.
La causa de que `configd` vuelva a publicar no se investigó `?`.

Un escaneo asociado ya vuelve a la BSS 400 ms cada 3 canales (`SCAN_BACK_OP`, `RTW_BACK_OP_CH_MS`). El
cambio usa esos retornos:
- `rtl_radio_status.scanning` vale `RTL_RADIO_SCAN_BACKOP` (2) durante el retorno; sigue siendo distinto
  de 0, así que quien lo lee como booleano no cambia.
- `rtlRadioAwdlOffch` acepta ir a otro canal en ese estado, y sigue devolviendo `kIOReturnBusy` en el
  resto del escaneo.
- `SCAN_BACK_OP` no pasa a `SCAN_LEAVING_OP` mientras `offch_state` sea `OFFCHS_AWDL`.
  `sitesurvey_close_awdl_backop` cierra la ventana bajo `offch_mutex`, el mismo mutex del cambio de
  canal, así que un slot no puede empezar entre la comprobación y el cambio de estado. Si tras 500 ms
  más la radio sigue fuera, el propio escaneo la devuelve.
- `txTick` sirve slots en el retorno solo si oyó a un par en los últimos 30 s. Sin pares, los escaneos
  no cambian. Tampoco cambia nada si la BSS está en un canal social.

Driver md5 `e8559aca`, UUID `A4BD0F2B`; capa md5 `ade1eb88`, UUID `C9F97715`; enlace en 161/80, nodo de
`awdlsim` sin conexión por aire. Seis escaneos forzados con `scanForNetworks`, de 6,0 a 9,3 s, 30 s
aparte. Cinco de ellos con 75 pings al gateway cada 200 ms:
| Escaneo | Duración | Tramas del nodo que entran | Segundos seguidos sin tramas | Pings sobre 200 ms | Máximo | `Peer Absence` |
|---|---|---|---|---|---|---|
| 1 | 9,3 s | 9,3 % | 1 | 16 de 75 | 544 ms | 0 |
| 2 | 9,3 s | 7,8 % | 1 | 15 de 75 | 453 ms | 0 |
| 3 | 6,0 s | 14,2 % | 0 | 6 de 75 | 421 ms | 0 |
| 4 | 9,3 s | 9,2 % | 0 | 15 de 75 | 474 ms | 0 |
| 5 | 8,8 s | 8,8 % | 1 | 10 de 75 | 437 ms | 0 |

El sexto escaneo corrió mientras empezaba un envío por AirDrop: `/Discover` y `/Ask` funcionaron durante
el escaneo, y el archivo de 1 MiB llegó idéntico. El escaneo terminó antes de `/Upload`, así que la subida
no se probó con un escaneo en curso.

Ninguna ausencia en unos 3 min con seis escaneos, cada uno más largo que el límite de 6 s o igual. Antes
de este cambio solo se midió el escaneo natural de §8.1, de 6,76 s, que sí provocó una ausencia; estos
escaneos forzados no se corrieron con el código anterior. Los máximos de ping son del mismo orden que los
475 ms medidos en §8 con `ef52dda` en un ciclo de 9,3 s, aunque aquella medición fue sin par.

La línea `tx psf` a los 6 min de arrancar el nodo da `mcc 1432 fail 0`, máximo 4860 µs y media 2455 µs: ningún cambio de canal rechazado, tampoco al cerrarse un retorno. En esos 6 min no hubo ninguna `Peer Absence`.

No probado: recepción real por aire durante el retorno. El nodo entrega sus tramas por el puerto de
simulación, que solo comprueba el canal; el receptor real está además condicionado por los ajustes de
`rtw_phydm_func_for_offchannel`, activos durante todo el escaneo `?`.

### 8.3 Ventanas de datos (2026-11-05)

Los datos AWDL tenían un cupo fijo de 4 tramas por AW, sin importar su tamaño: una ACK de TCP gastaba lo
mismo que una trama llena. Ahora cada AW tiene un presupuesto de 12 ms de aire, de sus 16,4 ms, a 12 Mb/s;
cada trama cuesta 160 µs más su longitud, y la cola pasa de 32 a 64 tramas. El límite del driver sigue
siendo la mitad del pool de gestión, que corta las rachas largas de tramas cortas.

El nodo virtual no tiene radio y nunca devuelve el ACK 802.11, así que cada trama unicast agotaba sus 6
intentos. Eso no pasa con un par que responde, y falseaba el coste en la BSS. Con el puerto de simulación
activo, las tramas de datos salen ahora con un solo intento (`data_tries`). Sin simulación siguen los 6.

Mediciones con 1 MiB por TCP sobre el enlace virtual y 100 pings al router cada 200 ms en cada corrida. Las
filas con presupuesto usan la capa md5 `2d7229d7`, UUID `8FF43136`; la de 1 intento, el driver md5
`f2750e21`, UUID `2D84BB9B`. El router del proveedor pasó solo de 161 a 149 a las 13:57:43, con el mismo
BSSID, y la fila del cupo en 149 se midió a las 13:59.

| Capa, canal de la BSS, intentos, corridas | Subida (Mac → nodo) | Bajada (nodo → Mac) | Ping en la subida: media, máximo | Pings > 20 ms / > 200 ms en la subida |
|---|---|---|---|---|
| Cupo de 4 tramas, 161/80, 6, 1 | 27 KB/s | 59 KB/s | sin medir | sin medir |
| Cupo de 4 tramas, 149/80, 6, 3 | 44-46 KB/s | 409-485 KB/s | 31-38 ms, 257 ms | 31-33 / 3-4 |
| Presupuesto de 12 ms, 149/80, 6, 3 | 110-115 KB/s | 1523-2011 KB/s | 124-183 ms, 991 ms | 41-43 / 27-31 |
| Presupuesto de 12 ms, 149/80, 1, 3 | 110-115 KB/s | 1526-1957 KB/s | 5-6 ms, 73 ms | 7-8 / 0 |

En las filas de 149, 0 `Peer Absence`; en las dos con presupuesto, un archivo de 1 MiB por AirDrop llegó
idéntico.

**Velocidad.** Las capturas del nodo miden el efecto directamente: con cupo salen 4 tramas por AW y 12 por
ventana; con presupuesto salen 10 por AW y 30 por ventana, con tramas de 1468 bytes. En 149, con el mismo
canal, la subida crece 2,5 veces, en la misma proporción. Los reintentos no cambian la velocidad.

Las ACK cortas llegan ahora a unas 50 por AW, y la bajada en 149 se multiplica por unas 4. Las dos filas
comparten canal, así que el cupo también limitaba al otro extremo.

**Número de ventanas.** La secuencia que anuncia el Mac, en sus PSF, depende del canal de la BSS:

| BSS | Secuencia anunciada | Slots con datos en las capturas | Tramas/s calculadas con cupo |
|---|---|---|---|
| 161 | `0 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0` | 2 y 10 | 23 |
| 149 | `149 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0` | 0, 2 y 10 | 34 |

Con la BSS en 149, el slot de infraestructura también es 149 y sirve como tercera ventana: 3 cada 16 slots,
con separaciones de 2, 6 y 8. El cálculo da 34 tramas/s con cupo y 86 con presupuesto, que coincide con lo
medido en 149. Con la BSS en 161 son 2 ventanas y, por proporción, la subida con presupuesto debería rondar
los 75 KB/s; eso último no se midió.

La secuencia también cambia con el estado de IO80211. Al llegar la baliza BLE, `setScheduleState` registra
`BonjourServiceAdded`, y las dos corridas con presupuesto muestran lo mismo en los PSF (BSS en 149):

| Estado | Duración | Slots con 149 |
|---|---|---|
| Discovery | 3 s | 0-3 y 9-11 |
| Idle | 6 s | 0-2 y 9-10 |
| Low Power | el resto, también durante los envíos | 0, 2 y 10 |

**Aviso repetido, resuelto.** El log mostraba una vez por segundo `mismatch of channel sequence with Firmware,
force to program Low Power`. `sanityCheckAWStart` (`0xffffff80021e21de`) cuenta en `state+0xFB48` cada evento de
inicio de AW cuyo canal o flags no coincide con lo esperado, y `monitorAWDLState` (`0xffffff80021ee79c`) imprime
el aviso si ese contador no es 0. La causa estaba en la capa: el evento 41 ponía los flags del canal en `+0x18`,
que es la versión de `apple80211_channel`, y dejaba en 0 `+0x20`, que son los flags. Por eso los bits `0x2018`
no coincidían nunca. Ahora el evento lleva la versión y los flags de la entrada de la secuencia
([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2).

Con el arreglo y la BSS en 149, el aviso pasa de 60 por minuto a ninguno en 6 minutos con tráfico. `monitorAWDLState`
siguió activo, con 346 líneas de estado en ese tiempo. La subida no cambia (113-115 KB/s), el ping durante la
subida tampoco (máximo 85-87 ms, 8-10 de 100 por encima de 20 ms), no hay ausencias de par y el AirDrop de 1 MiB
llega idéntico. El estado sigue llegando a Low Power con la misma secuencia: el aviso no era lo que lo mantenía
ahí, porque `setScheduleState` se ejecuta igual con el contador en 0.

El efecto del error iba más allá del aviso. `handleUmiTimer` (`0xffffff80021c3254`) compara el canal y los flags
de la ventana actual con los de la secuencia de cada par y, si no coinciden, salta el UMI de ese par. Con los
flags en 0, el código no llega a enviar un UMI en una ventana con canal. Con el nodo virtual no se observa la
diferencia: la captura tiene 0 tramas de acción unicast del Mac tanto antes como después del arreglo, también
durante un AirDrop. Esas 0 tramas tenían otras causas; con un registro de tráfico y el nodo declarando 6E, el UMI
sale (`AWDL_PAR_VIRTUAL.md` §9.6 y §9.7). Con un par real no está probado.

**Coste en la BSS.** Entre las dos filas con presupuesto solo cambian los intentos: el ping máximo baja de 991
a 73 ms y los pings por encima de 200 ms desaparecen. Una trama sin ACK con 6 intentos ocupa unos 11 ms y una
con ACK unos 1,2 ms (cálculo, no medición). A 80 tramas/s eso es un 88 % del aire contra un 10 %, y con cupo
era un 35 %, parecido al 31-33 % de pings por encima de 20 ms de esa fila.

No probado:

- El coste en la BSS con tiempo compartido, y el presupuesto con la BSS fuera de los canales sociales. El canal
  del router no se puede fijar; en 149 no hay cambios de canal.
- Un par real al que se le pierden tramas: usa hasta los 6 intentos. La fila de 6 intentos es el peor caso
  de un par que nunca responde con ACK, pero sigue contestando por TCP, algo que un par real no haría.
- Velocidades mayores que 12 Mb/s: el nodo virtual no permite juzgarlas.
- `mccChannel` en modo 1 sirve solo el primer slot de cada racha de 149. Si AirDrop con un iPhone real pide
  rachas largas, los datos quedarían limitados por eso. No se cambió a ciegas: una secuencia solo de 149
  servida entera perdería la BSS.
- Qué secuencia produce AirDrop con un par real. `handleAirDropTrafficReg` envía `sendRealTimeUMI` y no usa
  el modo High Thruput: `initiateHighThruputMode` solo se llama desde `handleMigrationTrafficReg`.
