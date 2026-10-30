# Soporte de hardware RTL8814AU para AWDL según el código del driver

Inventario de lo que el RTL8814AU puede hacer (según los registros y el código que el kext compila
hoy) para cada obligación que [`AWDL_FIRMWARE_DUTIES.md`](AWDL_FIRMWARE_DUTIES.md) atribuye al
firmware Broadcom y clasifica en su §6 para un port sin firmware AWDL. Responde seis preguntas:
segundo MAC/puerto concurrente, TSF y temporización, TX de action frames, velocidad de cambio de
canal, RX de action frames de peers desconocidos, y una tabla final que mapea cada deber del §6 de
`AWDL_FIRMWARE_DUTIES.md` a lo que la ruta RTL8814 ofrece, con feasible / parcial / missing y los
huecos concretos.

Base de código: el árbol Realtek vendido en `Source/rtl8814au/` (74 ficheros compilados por
[`build.sh`](../build.sh) `:37-56` y `:99-123`), el pegamento macOS en
`Source/Compat/VendorOsDep.c` y `Source/Driver/`. Las citas `fichero:línea` son relativas a la raíz
del repo. Lo no confirmado en el código va con `?`; ninguna conclusión implica prueba en hardware
RTL8814 de una secuencia AWDL. Complementa a [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
(§4 «Trabajo de radio») con el lado hardware.

## 1. Resumen corto

1. **Segundo MAC: el hardware sí; el driver de este repo, no.** El MAC 8814A tiene cinco pares de
   registros MAC/BSSID (puertos 0-4) y una MBSSID CAM de 8 entradas; pero la compilación actual es
   de una sola interfaz (`CONFIG_CONCURRENT_MODE` comentado en `Source/rtl8814au/include/autoconf.h:113`,
   `CONFIG_IFACE_NUMBER = 1` en `Source/rtl8814au/include/drv_conf.h:361`) y el código de puertos 1-4
   solo se compila bajo `CONFIG_CONCURRENT_MODE` (`Source/rtl8814au/hal/hal_com.c:3373-3386`,
   `Source/rtl8814au/hal/hal_com.c:3445-3461`). Para AWDL basta tratarlo como frames de management
   con A1/A3 arbitrarios en la cola mgnt, sin necesidad de abrir un segundo puerto.
2. **TSF: lectura y escritura directas por puerto** (`REG_TSFTR` 0x560 / `REG_TSFTR1` 0x568,
   `Source/rtl8814au/include/hal_com_reg.h:383-384`; escritura con función dedicada en
   `Source/rtl8814au/hal/hal_com.c:11797-11834`). **No hay TBTT/early interrupt utilizable en USB**:
   la máscara de interrupciones se programa a 0 (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:1947-1984`)
   y los bits de beacon/TBTT existen en el mapa pero nadie los activa. Las ventanas de
   disponibilidad se programarán con temporizadores software del host.
3. **TX de action frames: hoy funciona como frame mgnt estándar** — cola `QSLT_MGNT` (0x12,
   `Source/rtl8814au/include/hal_com.h:262`), tasa fija en el descriptor
   (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:232-236`), retry 6/12
   (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:259-263`). **No existe TX programada** (ni
   timestamp ni canal por frame): enviar a una hora concreta exige combinación de
   `REG_TXPAUSE` (`Source/rtl8814au/include/hal_com_reg.h:350`) + temporizador software, y enviar
   en otro canal exige cambiar de canal antes (§5).
4. **Cambio de canal: síncrono, del orden de decenas de ms sin IQK.** La cadena
   `set_channel_bwmode` → `rtw_hal_set_chnl_bw` → `phy_SwChnlAndSetBwMode8814A` reprograma RF
   (4 paths), tabla AGC, filtros CCK y tablas de potencia, y solo ejecuta IQK si se le pide
   (`HW_VAR_DO_IQK`): el escaneo lo salta y avanza a ~100 ms por canal en hardware real
   (`Docs/ARCHITECTURE.md:113`); el join lo pide (`Source/rtl8814au/core/rtw_mlme_ext.c:14250-14257`).
5. **RX de action frames de peers desconocidos: el hardware ya los acepta** (RCR con AAP/AM/AMF y
   `RXFLTMAP0 = 0xFFFF` verificados en vivo, `Docs/regdumps/macos_c896a978_idle.txt:387`,
   `:393`); quien los descarta es el core vendido en software (`mgt_dispatcher` exige A1 = MAC
   propia o broadcast, `Source/rtl8814au/core/rtw_mlme_ext.c:1317-1331`). El modo monitor del
   vendor core existe pero el port macOS lo dejó en stub (`Source/Compat/VendorOsDep.c:1594-1598`).
6. La tabla del §7 clasifica cada deber: la parte (a) del §6.1 de `AWDL_FIRMWARE_DUTIES.md` es
   feasible; (b) timing periódico es parcial (todo va a software, sin ayuda hardware de timing);
   (c) trabajo por frame es feasible con retenciones software, salvo la precisión temporal del TX
   programado y las estadísticas de CCA.

## 2. Segundo MAC/puerto concurrente

### 2.1 Registros de puerto del MAC 8814A

- El driver define cinco puertos hardware: `enum _hw_port { HW_PORT0..HW_PORT4 }`
  (`Source/rtl8814au/include/drv_types.h:1393-1399`).
- Cada puerto tiene par MAC/BSSID: `REG_MACID` 0x0610 / `REG_BSSID` 0x0618 (puerto 0,
  `Source/rtl8814au/include/hal_com_reg.h:418-419`), `REG_MACID1` 0x0700 / `REG_BSSID1` 0x0708
  (`:506-507`), `REG_MACID2` 0x1620 / `REG_BSSID2` 0x1628 (`:518-519`), `REG_MACID3` 0x1630 /
  `REG_BSSID3` 0x1638 (`:521-522`), `REG_MACID4` 0x1640 / `REG_BSSID4` 0x1648 (`:524-525`).
- La selección del registro por puerto está en `_get_macaddr_reg` / `_get_bssid_reg`
  (`Source/rtl8814au/hal/hal_com.c:3373-3386` y `:3445-3461`): los puertos 2-4 solo se referencian
  bajo `CONFIG_RTL8814A` **y** `CONFIG_CONCURRENT_MODE`; el setter `rtw_hal_set_macaddr_port`
  (`:3394-3411`) y `rtw_hal_set_bssid` (`:3465-3480`) escriben el par del puerto del adaptador.
- El segundo puerto tiene infraestructura de beacon y TSF propias: `REG_BCN_CTRL_1` 0x0551
  (`Source/rtl8814au/include/rtl8814a_spec.h:359`), `REG_TXPKTBUF_BCNQ1_BDNY` programado en el init
  USB (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:201-202`) y `REG_TSFTR1` 0x568
  (`Source/rtl8814au/include/hal_com_reg.h:384`).
- La conmutación del registro compartido seleccionable por puerto se hace por
  `rtw_enter_protsel_port` → `REG_PORT_CTRL_SEL` (`Source/rtl8814au/hal/hal_com.c`), usada p. ej.
  por `rtw_hal_set_tsf_update` (`Source/rtl8814au/hal/hal_com.c:3482-3510`).

### 2.2 MBSSID CAM y filtro de BSSID

- La MBSSID CAM tiene 8 entradas gestionadas por software
  (`TOTAL_MBID_CAM_NUM = 8`, `Source/rtl8814au/include/drv_types.h:1004`), escritas por los
  registros `REG_MBIDCAMCFG_1/2` 0x0628/0x062C (`Source/rtl8814au/include/hal_com_reg.h:421-422`)
  con el caso `HW_VAR_MBSSID_CAM_WRITE/CLEAR` de `SetHwReg`
  (`Source/rtl8814au/hal/hal_com.c:12187-12205`) y el número de APs en `REG_MBID_NUM` 0x0552
  (`Source/rtl8814au/include/hal_com_reg.h:373`, escritura en
  `Source/rtl8814au/hal/hal_com.c:3012-3016`).
- `RCR_ENMBID` (bit 24) significa «responder ACK solo a los frames cuyo A1 casee con las entradas
  de la MBSSID CAM» (`Source/rtl8814au/include/hal_com_reg.h:1117`); `rtw_hal_set_macaddr_mbid`
  escribe la MAC del adaptador en una entrada (`Source/rtl8814au/hal/hal_com.c:3298-3322`) y
  `rtw_mbid_cam_enable` añade el bit al RCR (`:3018-3022`).
- Sin MBSSID CAM, el filtro de BSSID son los bits `RCR_CBSSID_BCN` (bit 7) y `RCR_CBSSID_DATA`
  (bit 6) del RCR (`Source/rtl8814au/include/hal_com_reg.h:1134-1135`), que comparan contra el
  `REG_BSSID` del puerto; `rtw_hal_rcr_set_chk_bssid` los activa/desactiva según estado MLME
  (`Source/rtl8814au/hal/hal_com.c:3827-3857`) y el valor de arranque USB es
  `RCR_APM|AM|AB|CBSSID_DATA|CBSSID_BCN|APP_ICV|AMF|HTC_LOC_CTRL|APP_MIC|APP_PHYST_RXFF`
  (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:450`).

### 2.3 Estado del port y huecos

- La build actual es de interfaz única: `CONFIG_CONCURRENT_MODE` comentado
  (`Source/rtl8814au/include/autoconf.h:113`), `CONFIG_IFACE_NUMBER = 1`
  (`Source/rtl8814au/include/drv_conf.h:357-363`), `CONFIG_MI_WITH_MBSSID_CAM` solo con
  `CONFIG_IFACE_NUMBER > 2` (`:409-417`), y el 8814A está excluido por comentario de
  `CONFIG_HWMPCAP_GEN2` (`:380`, la generación de 8822B/8821C/8822C; el 8814B usa Address CAM de
  128 entradas según `:382`). Con una interfaz, `HW_VAR_MAC_ADDR` escribe por puerto y no por CAM
  (`Source/rtl8814au/hal/hal_com.c:12160-12165`).
- El tree soporta oficialmente multi-interfaz en otros chips (errores de compilación para
  `> 3` interfaces GEN1, `Source/rtl8814au/include/drv_conf.h:397-399`); para 8814A la clasificación
  de capacidad multi-puerto del driver quedó sin definir. Que el puerto 1 funcione de verdad con el
  path USB (cola de beacon 1, IRQ `BCNDOK1`, etc.) no está probado ni en este repo ni en hardware
  `?` — los bits de interrupción por puerto existen (`UHIMR_BCNDOK1`/`BCNDMAINT1`,
  `Source/rtl8814au/include/hal_com_reg.h:970-976`) pero toda la máscara está a cero (§3.3).
- **Veredicto para AWDL:** un segundo MAC con BSSID propio es hardware-feasible, pero no es
  necesario: los frames AWDL son management con A1 multicast/A3 no-BSS que la cola mgnt emite con
  A1/A3 arbitrarios sin registrarse en ningún filtro de BSSID (§4). El punto donde un segundo
  puerto importaría es la concurrencia con la estación infra (§7, deber «caché por peer» y
  coexistencia de canal): el canal es un recurso único del chip (§5) y cualquier simultaneidad
  2.4/5 GHz es time-shared por software, como en Broadcom `?`.

## 3. TSF y temporización

### 3.1 Lectura y escritura de TSF

- `REG_TSFTR` 0x0560 (puerto 0) y `REG_TSFTR1` 0x0568 (puerto 1),
  `Source/rtl8814au/include/hal_com_reg.h:383-384`. La lectura es un `rtw_read32` directo; el
  módulo de radio-measurement la usa para generar intervalos aleatorios
  (`Source/rtl8814au/core/rtw_rm_fsm.c:591`).
- La escritura está implementada en `rtw_hal_correct_tsf`
  (`Source/rtl8814au/hal/hal_com.c:11797-11834`): para el puerto 0 desactiva `EN_BCN_FUNCTION` en
  `REG_BCN_CTRL` 0x0550, escribe los 8 bytes en `REG_TSFTR`/`REG_TSFTR+4` y lo reactiva; para el
  puerto 1 usa `REG_BCN_CTRL_1` y `REG_TSFTR1`. Se alcanza por `HW_VAR_CORRECT_TSF`
  (`Source/rtl8814au/hal/hal_com.c:12351-12354`); la variante con sync por hardware
  (`CONFIG_HAS_HW_VAR_CORRECT_TSF`) solo existe para 8814B
  (`Source/rtl8814au/include/hal_ic_cfg.h:513`, dentro del bloque `CONFIG_RTL8814B` que termina en
  `:521`), y para 8814A no está definida.
- El valor que se escribe viene del seguimiento software de TSF: `update_TSF` copia el campo TSF
  del beacon a `pmlmeext->TSFValue` (`Source/rtl8814au/core/rtw_wlan_util.c:3584-3597`) y
  `correct_TSF` lo baja al hardware alineándolo al beacon interval
  (`Source/rtl8814au/hal/hal_com.c:11836-11863`: `tsf = TSFValue - mod64(TSFValue, bcn_interval*1024) - 1024`).
- El TSF propio también se actualiza por hardware con cada beacon recibido si `EN_BCN_FUNCTION`
  está activo y `DIS_TSF_UDT` limpio (`REG_BCN_CTRL`, init en
  `Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:3056` y `:3090`); el comando
  `EN_HW_UPDATE_TSF` lo re-habilita desde el cmd thread
  (`Source/rtl8814au/core/rtw_cmd.c:2447` y `:5657-5658`).

### 3.2 Temporizadores hardware

- El mapa de registros expone cinco temporizadores `REG_TC0_CTRL..REG_TC4_CTRL` 0x0150-0x0160 y
  `REG_TCUNIT_BASE` 0x0164 (`Source/rtl8814au/include/rtl8814a_spec.h:103-110`), y
  `REG_TIMER0/TIMER1` 0x0584/0x0588 (`Source/rtl8814au/include/hal_com_reg.h:388-389`). En el
  árbol compilado no hay ninguna referencia a los TC (`?` solo firmware); los únicos usos de
  `REG_TIMER0` son la selección de fuente de reloj en `hw_port0_tsf_sync_sel`
  (`Source/rtl8814au/hal/hal_com.c:11593-11630`), que está bajo `CONFIG_HW_P0_TSF_SYNC`
  (`Source/rtl8814au/include/hal_ic_cfg.h:499`, bloque 8814B) y no se compila para 8814A.
- Todo el timing periódico del driver es software: timers del compat (p. ej. el timer de
  `periodic_tsf_update_end`, `Source/rtl8814au/hal/hal_com.c:3670-3679`) y el cmd thread. Para las
  ventanas de disponibilidad AWDL la consecuencia es directa: align con el TSF del firmware
  Broadcom se sustituye por leer `REG_TSFTR` y disparar un timer del host; la precisión dependerá
  del despachador de timers del XNU y de la cola de TX `?` (sin constante de tolerancia en el
  código, igual que en Broadcom — `AWDL_FIRMWARE_DUTIES.md` §6.2).

### 3.3 Interrupciones TBTT/early: existen en el mapa, no se usan

- El 8814AU es USB y funciona por polling de URBs: `rtl8814au_init_default_value` deja
  `IntrMask[0] = 0` e `IntrMask[1] = 0` con **todos** los bits comentados, incluidos
  `IMR_BCNDOK0`, `IMR_BCNDMAINT0`, `IMR_TXBCN0OK`, `IMR_TXBCN0ERR`, `IMR_GTINT3`, `IMR_GTINT4`
  (`Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c:1947-1984`); la máscara se escribe tal cual en
  `REG_HIMR0/REG_HIMR1` (`:257-259`).
- Los bits existen: `UHIMR_TXBCNOK` (bit 25), `UHIMR_TXBCNERR` (26), `UHIMR_BCNDOK0..3`,
  `UHIMR_BCNDMAINT0..3`, `UHIMR_TSF_BIT32_TOGGLE` (24) y los temporizadores generales
  `UHIMR_GTINT3` (27) / `UHIMR_GTINT4` (28)
  (`Source/rtl8814au/include/hal_com_reg.h:962-1006`). Los registros de early beacon
  `REG_DRVERLYINT` 0x0558 y `REG_BCNDMATIM` 0x0559 están definidos
  (`Source/rtl8814au/include/rtl8814a_spec.h:363-364`) y sin escritores en el código `?`; la
  configuración `CONFIG_INTERRUPT_BASED_TXBCN` está comentada
  (`Source/rtl8814au/include/autoconf.h:129-136`).
- La proximidad al TBTT solo se regula con `REG_TBTT_PROHIBIT` 0x0540 (setup/hold en unidades de
  32 µs, `Source/rtl8814au/include/rtl8814a_spec.h:342-355`, init en
  `Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:3066-3073`), que prohibe TX cerca del TBTT
  pero no avisa de él. `REG_INT_MIG` 0x0304 (migración de interrupciones,
  `Source/rtl8814au/include/rtl8814a_spec.h:171`) solo se usa para cargar CPU, no para timing
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_dm.c:120-124`).
- **Consecuencia AWDL:** no hay interrupción hardware para «empieza la ventana»: el plan de
  `AWDL_FIRMWARE_DUTIES.md` §6.2 se ejecuta con un temporizador software anclado al TSF leído por
  vendor request. Habilitar `UHIMR_GTINT3/GTINT4` + `REG_TIMER0/1` como temporizador hardware
  sería una vía nativa, pero en este árbol no hay precedente de uso en USB `?`.

## 4. TX de management/action frames

### 4.1 Ruta actual de un frame de management

1. Los constructores MLME (`issue_auth`, `issue_action`, ...) allocan un `xmit_frame` de
   `MGNT_FRAMETAG` con `alloc_mgtxmitframe`
   (`Source/rtl8814au/core/rtw_mlme_ext.c:7548-7581`).
2. `update_mgntframe_attrib` fija `qsel = QSLT_MGNT`, `mac_id = RTW_DEFAULT_MGMT_MACID`,
   `rate = pmlmeext->tx_rate`, `retry_ctrl = TRUE` y sin cifrado
   (`Source/rtl8814au/core/rtw_mlme_ext.c:7616-7689`). La tasa por defecto es 6 Mbit/s OFDM en
   5 GHz y 1 Mbit/s CCK en 2.4 GHz (`Source/rtl8814au/core/rtw_mlme_ext.c:1062-1064`), cambiable
   con `update_mgnt_tx_rate` (`:7546-7552`) — el equivalente driver-side de `awdl_min_rate`.
3. `dump_mgntframe` (`:7734-7743`) llama `rtw_hal_mgnt_xmit`
   (`Source/rtl8814au/hal/hal_intf.c:816-831`), que calcula direcciones RA/TA y encola
   `rtl8814au_mgnt_xmit` → `rtw_dump_xframe` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:897-899`).
4. `update_txdesc` rellena el descriptor de 32 bytes y `rtw_dump_xframe` lo escribe al FIFO por el
   pipe USB (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:403`).

### 4.2 Campos del descriptor TX relevantes para AWDL

- Cola: `SET_TX_DESC_QUEUE_SEL` con `pattrib->qsel`
  (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:108`); MACID y rate ID en
  `:105-106`. Secuencia por hardware cuando no hay QoS (`:110-114`).
- Rama `MGNT_FRAMETAG` (`:229-272`): `USE_RATE = 1` y `TX_RATE = MRateToHwRate(pattrib->rate)`
  (`:232-236`) — tasa fija, sin rate adaptation para mgnt; límite de retry 6 si `retry_ctrl` o 12
  si no (`:259-263`); bit de informe de ACK `SPE_RPT` bajo `CONFIG_XMIT_ACK` (`:266-272`), que en
  esta build está **comentado** (`Source/rtl8814au/include/autoconf.h:63`): con ello
  `dump_mgntframe_and_wait_ack_timeout` degrada a enviar y dormir 50 ms
  (`Source/rtl8814au/core/rtw_mlme_ext.c:7795-7808`). Para AWDL esto significa que la
  confirmación de TX del estilo `awdl_oob_af` (evento 60/69 de Broadcom) no tiene equivalente hoy
  `?` — habría que activar `CONFIG_XMIT_ACK` o usar los reportes C2H.
- Macro-campos definidos en `Source/rtl8814au/include/rtl8814a_xmit.h`: `TX_RATE`
  (`:184`), `DATA_RETRY_LIMIT` (`:189`), `RTS_RATE` (`:190`), `DATA_BW` (`:198`), `NAV_USE_HDR`
  (`:177`), checksum del descriptor (`rtl8814a_cal_txdesc_chksum`,
  `Source/rtl8814au/hal/rtl8814a/rtl8814a_xmit.c:238-253`).
- El frame sale cifrado o no según `rtl8814a_fill_txdesc_sectype`, pero esa rama solo aplica a
  `DATA_FRAMETAG` (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:124-129`); un mgnt se
  envía sin cifrado por defecto (PMF se añade por software en el port con
  `rtl_pmf_protect_mgnt`, `Source/rtl8814au/core/rtw_mlme_ext.c:7761`).

### 4.3 Cola y pipe USB

- Colas del MAC: `QSLT_BK` 0x2, `QSLT_BE` 0x0, `QSLT_VI` 0x5, `QSLT_VO` 0x7, `QSLT_BEACON` 0x10,
  `QSLT_HIGH` 0x11, `QSLT_MGNT` 0x12, `QSLT_CMD` 0x13
  (`Source/rtl8814au/include/hal_com.h:256-263`).
- El mapeo cola→pipe USB del vendor: con 3 bulk OUT (los del TP-Link Archer T9UH: 0x05/0x06/0x08,
  `Docs/ARCHITECTURE.md:20`), `_ThreeOutPipeMapping` manda MGT (índice 5), BCN (4), HIGH (6) y
  TXCMD (7) al pipe 0 — el bulk OUT alto — y los AC al 1/2
  (`Source/rtl8814au/hal/hal_com.c:1136-1181`, MGT→RtOutPipe[0] en `:1166` y `:1179`).
- En el port macOS, `ffaddr2pipehdl` usa `Queue2Pipe[addr]` con fallback a `RtOutPipe[0]`
  (`Source/Compat/VendorOsDep.c:112-135`). El mgnt AWDL compartiría pipe con los AC de mayor
  prioridad `?` (la prioridad real la fija el EDCA del MAC, no el pipe).

### 4.4 Qué no existe: TX programada y canal por frame

- El descriptor no tiene campo de canal ni de tiempo de emisión: los campos de banda
  (`DATA_BW`, `RTS_SC`) solo se escriben en la ruta NDPA de beamforming
  (`Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:237-256`) y el frame sale por el canal
  actual del RF (`pmlmeext->cur_channel` es el que gobierna el setup, §5).
- El único control temporal de TX del MAC es `REG_TXPAUSE` 0x0522
  (`Source/rtl8814au/include/hal_com_reg.h:350`) vía `HW_VAR_TXPAUSE`
  (`Source/rtl8814au/core/rtw_wlan_util.c:644-647` lo usa el proxy DFS): pausar/reanudar colas
  completas, útil como compuerta de una ventana de disponibilidad pero sin granularidad de frame.
- La transmisión en un instante preciso se emula en software: encolar en la cola mgnt y abrir
  TXPAUSE justo antes; el retardo real será el del despachador del host más el vaciado de la cola
  `?`. No hay equivalente driver-side de `awdl_oob_af` (TX dentro de la próxima AW) ni de
  `awdl_oob_af_auto` (repetición por AW): ambas serían colas software propias que consulten la
  ventana actual antes de dequear, tal como anticipa `AWDL_FIRMWARE_DUTIES.md` §6.3.

## 5. Cambio de canal

### 5.1 Cadena completa

`set_channel_bwmode` (`Source/rtl8814au/core/rtw_wlan_util.c:596-665`, bajo mutex
`setch_mutex`) → `rtw_hal_set_chnl_bw` (`Source/rtl8814au/hal/hal_intf.c:1001-1037`) →
`PHY_SetSwChnlBWMode8814` registrado como handler (`Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:4457`)
→ `phy_SwChnlAndSetBwMode8814A` (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:2078-2143`),
ejecución **síncrona** en el hilo llamador (en USB no hay work item; los callbacks de timer
`PHY_SwChnlTimerCallback8814A` están bajo `#if 0`, `:1992-2019`).

### 5.2 Qué toca (y qué no)

- **Cambio de banda**: `phy_SwBand8814A` lee `REG_CCK_CHECK` 0x0800 bit 7 para saber la banda
  actual y llama `PHY_SwitchWirelessBand8814A` solo si cambia
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:989-1013`). Esa función apaga el clock gate de
  CCK/OFDM (`:1163-1167`), selecciona tabla AGC 0x958[4:0], reprograma el pinmux RFE de las 4
  antenas (`:1017-1137`, `:1200+`), rTxPath y rCCK_RX, y conmuta `REG_CCK_CHECK`
  (`:1168-1197`).
- **Canal**: `phy_SwChnl8814A` (`:1883-1978`) escribe `rFc_area_Jaguar` (BB), el registro RF
  `RF_CHNLBW_Jaguar` en **todos** los paths RF (`:1946-1962`), la tabla AGC por banda (`0x958[4:0]`,
  `:1964-1971`) y los filtros CCK TX DFIR si 2.4 GHz (`:1973-1977`). El kfree de ganancia BB por
  canal solo actualiza estado (`:1907-1922`, la llamada real a `phydm_ConfigKFree` está en
  `#if 0`).
- **Tablas de potencia**: tras el switch, `odm_clear_txpowertracking_state` +
  `rtw_hal_set_tx_power_level` reprograman los índices de potencia de las 4 paths para el canal
  nuevo (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:2109-2111` → `PHY_SetTxPowerLevel8814`
  `:636-668` → `phy_set_tx_power_level_by_path`,
  `Source/rtl8814au/hal/hal_com_phycfg.c:3044`).
- **IQK**: solo si `bNeedIQK` fue marcado con `HW_VAR_DO_IQK`
  (`Source/rtl8814au/hal/hal_com.c:12148-12156`) se llama `halrf_iqk_trigger`
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c:2112-2117` →
  `Source/rtl8814au/hal/phydm/halrf/halrf.c:1694`). El IQK 8814A respaldo MAC/BB/RF y hace LOK +
  IQK TX/RX por path con esperas activas: `ODM_delay_ms(1)` por LOK (más polling de hasta 10 ms)
  y `WBIQK_delay = 10` ms (`Source/rtl8814au/hal/phydm/halrf/rtl8814a/halrf_iqk_8814a.c:217-260`,
  `:342`; constantes en `Source/rtl8814au/hal/phydm/halrf/halrf_iqk.h:30-31`),
  `ODM_delay_ms` = `rtw_mdelay_os` (espera ocupada,
  `Source/rtl8814au/hal/phydm/phydm_interface.c:613-622`). El join pide IQK alrededor del cambio
  (`Source/rtl8814au/core/rtw_mlme_ext.c:14250-14257`), el arranque IBSS también
  (`:11313-11336`); **el escaneo no lo pide** (`set_channel_bwmode` directo en
  `:14828` sin `HW_VAR_DO_IQK`).

### 5.3 Coste observado en hardware

- El escaneo completa canales a ~100 ms de dwell total por canal con el cambio incluido
  (`Docs/ARCHITECTURE.md:113`), y el walk de canales se verificó por lectura directa de
  `RF_CHNLBW` (`Docs/ARCHITECTURE.md:131`) — es decir, el switch sin IQK es de un orden de
  magnitud menor que el dwell `?` (sin medición aislada del propio switch).
- El RCR cae durante el escaneo para aceptar beacons de cualquier BSSID y se restaura después
  (`Docs/ARCHITECTURE.md:130`); ese patrón (`rtw_hal_rcr_set_chk_bssid`, §2.2) es el mismo que
  usaría la ventana AWDL para escuchar canales sociales sin perder la estación infra `?`.
- **Consecuencia AWDL:** saltar por la secuencia `awdl_chan_seq` es viable con la cadena actual
  mientras el IQK se mantenga fuera del camino caliente (como en el escaneo); un salto 2.4 ↔ 5 GHz
  añade el switch de banda completo. La cadencia AWDL (64-128 TU) deja margen razonable `?`; los
  canales sociales AWDL son 6/44/149, todos sin DFS `?` (149 es U-NII-3), con lo que el proxy DFS
  de Broadcom (`awdl_dfsp_cfg`) no tiene por qué intervenir.

## 6. RX de action frames de peers desconocidos

### 6.1 Filtro hardware: ya los acepta

- Semántica del RCR (`Source/rtl8814au/include/hal_com_reg.h:1110-1139`): `RCR_AMF` (bit 13)
  acepta management, `RCR_AM` (bit 2) multicast, `RCR_AB` (bit 3) broadcast, `RCR_AAP` (bit 1)
  acepta todo unicast, `RCR_CBSSID_BCN/DATA` (bits 7/6) comparan BSSID solo para beacon/probe-rsp
  y data. Los subtipos de management pasan por la máscara `REG_RXFLTMAP0` 0x0620 (un bit por
  subtipo; el subtipo action = 13).
- Estado vivo asociado a un AP: `RCR = 0xF40060CE` (AAP/AM/AB/AMF/CBSSID_DATA/CBSSID_BCN
  activos) y `RXFLTMAP0 = 0xFFFF` / `RXFLTMAP1 = 0xFFFFFFFF` — el hardware entrega los frames de
  management multicast, incluidos los action frames de cualquier emisor
  (`Docs/regdumps/macos_c896a978_idle.txt:387`, `:393-394`; mismo RCR documentado en
  `Docs/ARCHITECTURE.md:1078`). `RXFLTMAP0` no lo escribe el driver en modo normal (solo el modo
  monitor y MP, §6.3): el valor 0xFFFF es el default de reset del chip `?` (coherente: no hay
  escritor).
- Los action frames AWDL van con A1 broadcast (4893 de 4893 en capturas de iOS 26,
  [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §1), A2 = MAC del peer (sin `sta_info`) y A3 =
  `00:25:00:FF:94:73`: los bits CBSSID no los tocan (solo aplican a beacon/data) y `RCR_AB|AMF` los
  deja pasar al RX DMA. Las tramas de datos AWDL sí chocan con `RCR_CBSSID_DATA`
  ([AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §2.3).

### 6.2 Filtrado software: aquí se pierden hoy

- La cadena RX: `recv_func` → `recv_func_prehandle` → `validate_recv_frame` → para management
  `validate_recv_mgnt_frame` (`Source/rtl8814au/core/rtw_recv.c:2305-2307`, definición
  `:1969-2032`, que busca `psta` por A2 sin descartar si es NULL) → `mgt_dispatcher`
  (`Source/rtl8814au/core/rtw_mlme_ext.c:1259`).
- `mgt_dispatcher` descarta todo frame cuyo A1 no sea la MAC propia o broadcast
  (`Source/rtl8814au/core/rtw_mlme_ext.c:1317-1331`). Los action frames AWDL, con A1 broadcast,
  pasan; los unicast de management a la MAC de `awdl0` (p. ej. ADDBA) mueren aquí.
- `OnAction` despacha por categoría contra `OnAction_tbl` e ignora las categorías desconocidas
  (`Source/rtl8814au/core/rtw_mlme_ext.c:7463-7483`). La categoría 127 sí está:
  `RTW_WLAN_CATEGORY_P2P = 0x7f` (`Source/rtl8814au/include/ieee80211.h:1525`) →
  `OnAction_p2p` (`Source/rtl8814au/core/rtw_mlme_ext.c:96`, `:7398`), que descarta la trama AWDL
  porque exige A1 propio y el OUI Wi-Fi Direct `0x506F9A09`. Recorrido completo y punto de desvío en
  [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §3 y §4.
- Para TX/RX de control (RXFLTMAP1) el driver solo toca el bit de RX BAR
  (`Source/rtl8814au/hal/hal_com.c:12385-12398`).

### 6.3 Modo monitor: existe en el vendor core, stub en el port

- Entrada en monitor: `hw_var_set_opmode` con `_HW_STATE_MONITOR_` guarda el RCR y los
  RXFLTMAP, escribe `RCR_AAP|APM|AM|AB|APWRMGT|ADF|ACF|AMF|APP_PHYST_RXFF|APPFCS|ACRC32|AICV` y
  pone `RXFLTMAP0/1/2 = 0xFFFF` (`Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c:3160-3196`),
  con restauración al salir (`:3198-3212`).
- En ese estado `recv_func` manda **todo** frame a `recv_frame_monitor`
  (`Source/rtl8814au/core/rtw_recv.c:4377-4385`), que antepone cabecera radiotap — con el campo
  TSFT incluido — y llama `rtw_recv_monitor`
  (`Source/rtl8814au/core/rtw_recv.c:4176-4216`; radiotap en
  `Source/rtl8814au/core/rtw_recv.c:3854+`, bit `IEEE80211_RADIOTAP_TSFT` en `:3937`).
- En Linux `rtw_recv_monitor` entrega el skb crudo con `ETH_P_80211_RAW`
  (`Source/rtl8814au/os_dep/linux/recv_linux.c:643-686`). **En el port macOS el port dejó la
  función como stub que devuelve `_FAIL`** (`Source/Compat/VendorOsDep.c:1594-1598`): en monitor
  los frames se descartan tras el radiotap.
- Alternativa sin monitor: interceptar en `mgt_dispatcher`/`validate_recv_mgnt_frame` los action
  frames con la categoría/OUI AWDL y entregarlos antes de la comprobación de A1 (el propio port ya
  hace ganchos de este estilo ahí: PMF/SAE en `Source/rtl8814au/core/rtw_recv.c:1972-1993` y
  logging de auth en `Source/rtl8814au/core/rtw_mlme_ext.c:1277-1296`).

### 6.4 Entrega hacia arriba y RSSI por frame

- Hoy lo único que sale del core hacia la pila es tráfico de datos convertido a Ethernet:
  `rtw_os_recv_indicate_pkt` intercepta EAPOL para el supplicant interno y el resto lo entrega por
  `rtl_netif_rx` a `en2` (`Source/Compat/VendorOsDep.c:1506-1523`). Los frames de management nunca
  llegan a IOKit: no existe aún el canal equivalente al evento 75 de Broadcom
  (`WLC_E_ACTION_FRAME_RX`, `AWDL_FIRMWARE_DUTIES.md` §4.2) ni el tap BPF del diseño
  (`AWDL_PROTOTYPE_DESIGN.md` §5) `?` (punto de integración AirLayer por decidir).
- El RSSI por frame está disponible para un filtro estilo `awdl_af_rssi`: el estado PHY del RX
  descriptor lo parsea `odm_phy_status_query` (`Source/rtl8814au/core/rtw_recv.c:4785`) y
  llena `attrib->phy_info` (`Source/rtl8814au/include/rtw_recv.h:204`) con
  `rx_mimo_signal_strength[i]` por path (`Source/rtl8814au/hal/phydm/phydm_phystatus.c:973`).
- El propio descriptor expone rate RX, seq, encrypt, `physt` y tipo de reporte
  (`Source/rtl8814au/hal/rtl8814a/rtl8814a_rxdesc.c:20-62`; llamada del port en
  `Source/Compat/VendorOsDep.c:498`).

## 7. Tabla: deberes AWDL (`AWDL_FIRMWARE_DUTIES.md` §6) frente a la ruta RTL8814

| Deber (§ de `AWDL_FIRMWARE_DUTIES.md`) | Qué ofrece la ruta RTL8814 | Estado | Huecos concretos |
|---|---|---|---|
| **6.1 (a)** Configuración pura: guardar y devolver (SSID/STATE/BSSID/CHANNEL, capabilities, min rate, presence mode, election id/métrica, …) | No requiere hardware: ivars de AirLayer con los structs de `AWDL_STRUCTS.md`. `AWDL_MIN_RATE` tiene análogo real (`update_mgnt_tx_rate`, `rtw_mlme_ext.c:7546`); el resto es almacén. | **feasible** | Ninguno para la parte almacén; cada valor solo «actúa» vía (b)/(c). |
| **6.2 (b1)** Availability windows: `aw_period`/`aw_len`/`aw_ext_len`/`adv_period`, eventos de arranque de ventana (fw 96) | TSF legible/escribible por puerto (`hal_com_reg.h:383-384`, `hal_com.c:11797`); `REG_TXPAUSE` como compuerta TX (`hal_com_reg.h:350`); timer software del host. Sin interrupción TBTT/early utilizable en USB (`usb_halinit.c:1947-1984`) y sin equivalente del evento 96. | **parcial** | Máquina de ventanas 100 % software (timer + estado); jitter del scheduler del host no medido `?`; GTINT3/GTINT4+`REG_TIMER0/1` como timer hardware es una opción sin precedente en USB en este árbol `?`. |
| **6.2 (b2)** Elección de maestro (election id, métrica, umbrales RSSI; rol resultante, fw 97) | RSSI por frame disponible (`rtw_recv.c:4785`, `phydm_phystatus.c:973`); RX de sync frames feasible con los cambios del §6; la máquina es software puro (o `IO80211AWDLPeerManager`, como anticipa `AWDL_FIRMWARE_DUTIES.md` §1.5). | **feasible** | Depende de abrir el RX de action frames de peers desconocidos (§6.2/6.3); el estado de rol se alimenta localmente (sin evento 97). |
| **6.2 (b3a)** Salto de canal según `awdl_chan_seq` (≤ 160 B codificados) | Cadena síncrona de cambio de canal probada a ~100 ms/canal en escaneo (`ARCHITECTURE.md:113`), IQK saltado en caliente (`rtw_mlme_ext.c:14828`), switch de banda completo cuando 2.4↔5 GHz (`rtl8814a_phycfg.c:989-1197`). | **feasible** (con reserva de timing) | Precisión del instante de salto ligada al timer software `?`; sin calibración en el camino caliente, cada entrada en 5 GHz reusa IQK de banda `?`. |
| **6.2 (b3b)** TX periódica del sync frame (`awdl_payload` + `awdl_psf_dwell`) | Frame mgnt con tasa fija y retry bajo (§4.2), cola `QSLT_MGNT`, sin offload; repetición por software. | **parcial** | No hay TX programada ni repetición hardware: cada PSF es un encolado software anclado al timer de (b1); precisión ±jitter `?`. |
| **6.3 (c1)** Cabecera AF TX (`awdl_af_hdr`) y filtro RX por RSSI (`awdl_af_rssi`) | Construcción SNAP/cabecera local en software (ya contemplada en `AWDL_PROTOTYPE_DESIGN.md` §6); RSSI por frame en `attrib->phy_info`. | **feasible** | El filtro por RSSI y la cabecera son código AirLayer nuevo; el RX debe abrirse primero (§6.2). |
| **6.3 (c2)** Caché por peer para decidir transmibilidad (`awdl_peer_op`, contadores `peeropdrop`/`noawchansw`) | Todo software: tabla propia o `sta_info`; límites de MACID/CAM ampliísimos para 4 peers típicos AWDL (`MACID_NUM_SW_LIMIT 32`, `SEC_CAM_ENT_NUM_SW_LIMIT 32`, `drv_conf.h:443-444`). | **feasible** | Lógica de retención/descarte por ventana es nueva; no hay offload. |
| **6.3 (c3)** TX OOB puntual (`awdl_oob_af`, confirmación por eventos 60/69), repetición automática (`awdl_oob_af_auto`, `awdl_afs_pload`) | Encolado mgnt inmediato disponible (§4); confirmación de TX sin `CONFIG_XMIT_ACK` no existe (`autoconf.h:63`; fallback a sleep de 50 ms, `rtw_mlme_ext.c:7795-7808`). | **parcial** | Sin confirmación real de TX/ACK hasta activar `CONFIG_XMIT_ACK` o leer los reportes C2H `?`; repetición por AW = requeue software. |
| **6.3 (c4)** Cifrado del datapath (`wsec_key`/`wsec`) | CAM de seguridad de 32 entradas (`TOTAL_CAM_ENTRY`, `hal_com_reg.h:836`) escrita por `rtw_sec_write_cam_ent` (`hal_com.c:2576-2628`); instalación por cmd (`rtw_setstakey_cmd`, `rtw_cmd.c:1701`). | **feasible** | Las claves de grupo AWDL por peer y su rotación son lógica nueva `?`; el SW encryption del core ya existe como fallback (`uc_swdec_pending_queue`, `rtw_recv.c:4400-4420`). |
| Extras del firmware: proxy DFS (`awdl_dfsp_cfg/ucsa`, fw 80), CCA por canal (`cca_get_stats`), `awdl_stats` | `CONFIG_DFS` compilado (`autoconf.h:85`) y `HW_VAR_TXPAUSE` con radar detect en `set_channel_bwmode` (`rtw_wlan_util.c:626-647`), pero es DFS maestro propio, no proxy; PhyDM CCX/NHM existe en el tree pero no está en la lista de compilación (`build.sh:99-123` no incluye `phydm_ccx.c`). | **missing** (proxy DFS) / **missing** (CCA/stats) | Canales sociales AWDL no requieren DFS `?`; las estadísticas tipo `awdl_stats`/`cca_get_stats` tendrían que habilitar módulos PhyDM adicionales o contarse en software. |

## 8. Método y límites

- Búsqueda dirigida en el árbol vendido (`Source/rtl8814au/`) por símbolos: `MBID/MBSSID`, `TSF`,
  `TBTT`, `HIMR/IMR`, `RCR/RXFLTMAP`, `SwChnl/IQK`, `mgnt_xmit`, `QSLT`, `monitor/radiotap`,
  `correct_TSF`, `TXPAUSE`; lectura de las funciones citadas completas y de los condicionales de
  compilación (`autoconf.h`, `drv_conf.h`, `hal_ic_cfg.h`) para saber qué código está realmente
  compilado en este kext.
- Los valores vivos de RCR/RXFLTMAP y el timing de escaneo provienen de mediciones ya documentadas
  del propio proyecto (`Docs/ARCHITECTURE.md`, `Docs/regdumps/`), no de nuevas pruebas.
- `?` principales: valor default de `RXFLTMAP0` (no hay escritor en modo normal; el valor vivo es
  0xFFFF), funcionamiento real del puerto 1 en el path USB (BCNQ1, IRQ de puerto), precisión
  temporal de timers del host frente a la cadencia AWDL, categoría y OUI exactos del action frame
  AWDL frente a `OnAction_tbl`, viabilidad de `GTINT3/GTINT4` como timer hardware en el 8814AU,
  y el punto de integración de la entrega de action frames hacia AirLayer/BPF.
- Ninguna conclusión implica prueba en hardware RTL8814 de una secuencia AWDL.
