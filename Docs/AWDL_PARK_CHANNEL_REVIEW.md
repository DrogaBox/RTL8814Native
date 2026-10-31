# Revisión del anclaje de canal AWDL (rtlRadioSetChannel)

Revisión independiente del código fuente del anclaje del radio en un canal social sin asociación:
commits `07a41e8` (lado driver) y `ab11c69` (lado capa AirLayer). Todas las afirmaciones se
verificaron contra el árbol actual (`42c04d3`) y contra el código de la Base en
`Source/rtl8814au`. Los huecos no verificables se marcan `?`.

Nota de alcance: `ab11c69` ya fue enmendado dentro de la misma rama por `2bb1c45` (lectura del
reloj bajo `_txLock`) y `404af9b` (el objetivo del anclaje pasa a ser el canal social que el 129
del gestor repite en más slots, con el preferido de `rtlawdl_ch` antes de un 129, en empates y
cuando el 129 no nombra ninguno). El lado driver de `07a41e8` no cambió (solo `42c04d3` añadió
AWDL data a `rtlRadioTx80211`). La revisión cubre ambos commits como base y el estado actual de
`parkRadio` (sección 2).

## 1. Recorrido del código

Driver (`07a41e8`):

- `rtl8814native_radio_set_channel` (`Source/Driver/AWDLTx.c:111-127`): rechaza canales fuera del
  plan (`rtw_chset_search_ch`, `AWDLTx.c:117`), rechaza linked/joining/under_survey
  (`AWDLTx.c:119`), fija `pmlmeext->tx_rate` a OFDM 6 Mb/s (`update_mgnt_tx_rate`,
  `AWDLTx.c:122`) y encola `rtw_set_chbw_cmd(ch, CHANNEL_WIDTH_20, DONT_CARE, flags 0)`
  (`AWDLTx.c:123-124`). Sin RTW_CMDF_DIRECTLY: solo encola; el trabajo pesado lo hace el hilo de
  comandos.
- `callPlatformFunction` (`Source/Driver/RTL8814Native.cpp:511-519`): `p1` es `const uint32_t *`
  con el canal; mapea `-EINVAL/EBUSY/ENOMEM` a `kIOReturnBadArgument/Busy/NoResources`.
- `rtl_radio_status.idle_channel` = `pmlmeext->cur_channel` solo en estado IDLE
  (`Source/Driver/VendorProbe.c:1202-1203`; el `memset` inicial la deja a 0 en los demás estados).
- ABI: `RTL_RADIO_FN_SET_CHANNEL` (`Source/Driver/RTLRadioABI.h:86`), `idle_channel`
  (`RTLRadioABI.h:154-159`). La cabecera es la misma para ambas kexts
  (`build_airlayer.sh:26` incluye `Source/Driver`): sin cambio de tamaño (`reserved[1]` →
  `idle_channel`), compatible en ambas direcciones.

Capa (`ab11c69` + enmiendas, árbol actual):

- `initWithController` lee `rtlawdl_ch` (144-147 de `Source/AirLayer/AWDLInterface.cpp`); solo
  6/44/149 valen, resto `_parkCh = 0` (sin anclaje).
- `txTick` (`AWDLInterface.cpp:910-924`): si `!st.scanning`, sirve `st.channel` en LINKED o llama
  `parkRadio(st.idle_channel, ...)` en IDLE; en JOINING/SCAN no hace nada.
- `parkRadio` (`AWDLInterface.cpp:1026-1062`, versión de `404af9b`): elige el objetivo — el canal
  social que el 129 repetido en más slots, con desempate a favor de `_parkCh` — bajo `_txLock`,
  y pide el cambio a 1 Hz como máximo (`_parkReqUs`, 1048-1050); `kIOReturnUnsupported` desactiva
  el anclaje (1054-1056); solo sirve cuando `idle_channel` ya es el objetivo (1046-1047).
- `awdlSocial` (`AWDLInterface.cpp:915-918`) filtra 6/44/149; `setRadioTx` reinicia `_parkReqUs`
  en la transición on (`AWDLInterface.cpp:870`).

## 2. Carreras entre `_SetChannel` y scan/join/disconnect/PS (pregunta 1)

**La comprobación de estado vive solo en el que encola.** `check_fwstate(_FW_LINKED |
_FW_UNDER_LINKING | _FW_UNDER_SURVEY)` (`AWDLTx.c:119`) se evalúa al encolar; `rtw_set_chbw_hdl`
(`Source/rtl8814au/core/rtw_mlme_ext.c:16539-16556`) escribe `cur_channel/cur_bwmode/
cur_ch_offset` (`:16553-16555`) y llama `set_channel_bwmode` (`:16556`) **sin reexaminar el
estado MLME**. Entre la comprobación y la ejecución por el hilo de comandos puede entrar un join
o un scan. Consecuencias por caso:

- **Scan empezado después de la comprobación** (`_FW_UNDER_SURVEY` se pone al encolar el
  survey, `Source/rtl8814au/core/rtw_cmd.c:892`): si la orden del survey se encoló antes, el hdl
  de anclaje se ejecuta en mitad del barrido (el hilo de comandos queda libre entre saltos de
  canal, que dispara el `survey_timer`). `site_survey` solo cambia el canal de operación
  (`Source/rtl8814au/core/rtw_mlme_ext.c:14827-14828`), no `cur_channel`, y al terminar
  `survey_done_set_ch_bw` vuelve al valor que el anclaje dejó (149): inofensivo, un dwell
  perdido como mucho.
- **Join empezado después de la comprobación** (`_FW_UNDER_LINKING` se pone al encolar,
  `Source/rtl8814au/core/rtw_ioctl_set.c:96`): la orden de join reescribe `cur_channel` con el
  DSConfig del BSS (`rtw_bss_get_chbw`, `rtw_mlme_ext.c:14194`) y cambia la radio
  (`:14253`); si el hdl de anclaje corre después, en pleno intercambio de auth/assoc, mueve la
  radio a 149 mientras el join espera respuestas en el canal del BSS: la join falla y se reintenta
  (y el anclaje vuelve a 1 Hz). Ventana estrecha (lectura IDLE de `txTick` frente a un connect
  concurrente más retardo del hilo de comandos) pero real. Hallazgo H-A (§6) con el fix.
- **Desconexión**: no toca canal. `rtw_mlmeext_disconnect` (`rtw_mlme_ext.c:12372`) solo suelta
  BSSID/macid, reevalúa RCR (`:12406`) y pone MSR de puerto 0 en STATION (`:12409`);
  `disconnect_hdl` (`:14267`) no cambia canal. `cur_channel` queda en el canal del BSS — exacto
  el caso que el re-park a 1 Hz de `parkRadio` cubre.
- **`survey_done_set_ch_bw` devuelve al canal anclado**: verificado. Sin unión linked/linking
  (`rtw_mi_get_ch_setting_union` ignora interfaces sin `_FW_LINKED|_FW_UNDER_LINKING`,
  `Source/rtl8814au/core/rtw_mi.c:133-135`) y sin P2P en LISTEN (rama `CONFIG_P2P` de
  `survey_done_set_ch_bw`, `rtw_mlme_ext.c:14907-14930`; `wdinfo` está en `P2P_STATE_NONE` en
  este sistema, nadie usa el P2P del vendor), cae en `pmlmeext->cur_channel`
  (`rtw_mlme_ext.c:14934-14938`) y restaura con `set_channel_bwmode` (`:14945`).

**Otros escritores de `cur_channel`** (inventario completo de asignaciones):

| Sitio | ¿Alcanza en reposo AWDL? |
|---|---|
| `rtw_mlme_ext.c:1052` (init, `registrypriv.channel` = 1, `VendorProbe.c:275`) | solo al arrancar |
| `rtw_mlme_ext.c:11314` (`start_create_ibss`) | solo al enlazar |
| `rtw_mlme_ext.c:14194` (join) | solo al enlazar |
| `rtw_mlme_ext.c:16259` (`rtw_join_done_chk_ch`) | solo interfaces AP/mesh ajenas; `CONFIG_CONCURRENT_MODE` no definido (`include/autoconf.h:113`), una sola iface ⇒ el bucle salta |
| `rtw_ap.c:4452/4700/4737` | AP no arrancado en este despliegue (`CONFIG_AP_MODE` sí está, `autoconf.h:127`, pero nadie llama a start_ap) |
| `rtw_mlme_ext.c:16553` (`rtw_set_chbw_hdl`) | es el propio anclaje |
| `rtw_p2p.c` (set_channel_bwmode directos) | ioctls P2P del vendor sin usuarios aquí |

**Escritores de `tx_rate` de gestión**: solo `update_tx_basic_rate` (`rtw_wlan_util.c:3091/3093`)
y `update_wireless_mode` (`:3397/3399`), ambos con llamadores en rutas de enlace únicamente
(`start_create_ibss`/`start_clnt_join` en `rtw_mlme_ext.c:11318/11381`, P2P en `rtw_p2p.c:5311/
5370`). En reposo nadie la toca: el OFDM 6 Mb/s del anclaje persiste hasta el siguiente join
(aunque el BSS sea 11B y la deje en CCK 1M, el re-park posterior la repone).

**PS/IPS/LPS**: `CONFIG_POWER_SAVING` no está definido, así que `CONFIG_IPS` y `CONFIG_LPS` no se
compilan (`include/autoconf.h:92-108`); además el driver fija `power_mgnt = PS_MODE_ACTIVE` e
`ips_mode = IPS_NONE` (`Source/Driver/VendorProbe.c:288-289`). No hay ruta de suspensión interna
que despidan la radio ni reescriban el canal. `CONFIG_AUTOSUSPEND`/BT coexist tampoco
(`autoconf.h:293` bajo `#ifdef`).

**dynamic_chk** (`rtw_dynamic_check_timer_handlder`, `Source/rtl8814au/core/rtw_mlme.c:3427` →
`rtw_dynamic_chk_wk_cmd` → `linked_status_chk`, `rtw_mlme_ext.c:12907`): la vigilancia de beacons
y roaming solo actúa con enlace; el watchdog PHYDM (DIG/CCA) no cambia de canal. `rtw_sreset.c:172`
restaura `cur_channel` tal cual (solo tras un sreset; inofensivo). Ningún `rtw_hal_set_hwreg`
escribe canal: la única vía RF es `set_channel_bwmode`.

**Serialización**: `set_channel_bwmode` toma `setch_mutex` (`rtw_wlan_util.c:626`) alrededor de
`rtw_set_oper_ch` + `rtw_hal_set_chnl_bw` (`:644-658`), así que dos cambios concurrentes (survey
timer frente a hilo de comandos) no dejan la RF a medias; la escritura de `cur_channel` del hdl
(`rtw_mlme_ext.c:16553`) está fuera del cerrojo, pero al correr en el hilo de comandos y solo
afectar lectores posteriores, el riesgo queda dentro de H-A.

## 3. Suficiencia de `set_channel_bwmode` en 149/44/6 desde reposo (pregunta 2)

Cadena completa verificada: `rtw_set_chbw_hdl` → `set_channel_bwmode` (`rtw_wlan_util.c:596`) →
`rtw_hal_set_chnl_bw` (`hal/hal_intf.c:1001`) → `PHY_SetSwChnlBWMode8814`
(`hal/rtl8814a/rtl8814a_phycfg.c:2340`) → `PHY_HandleSwChnlAndSetBW8814A` (`:2222`; rechazo de
canal ilegal en `:2251`) → `phy_SwChnlAndSetBwMode8814A` (`:2078`). Ese cuerpo ejecuta:

- `phy_SwChnl8814A` (`:2102`, cuerpo en `:1883`): cambio de banda por `phy_SwBand8814A`
  (`:1897`, 2.4↔5 GHz), tabla `rFc_area_Jaguar` y síntesis RF en todas las rutas
  (`:1934-1950`), selección de tabla AGC por banda (`:1952` y siguientes).
- `phy_SetBwMode8814A` (`:2106`): BB a 20 MHz.
- **Potencia de TX por canal**: `rtw_hal_set_tx_power_level` (`:2112`) →
  `PHY_SetTxPowerLevel8814` (`:636`) → `phy_set_tx_power_level_by_path` por ruta (`:668`), más
  `odm_clear_txpowertracking_state` (`:2111`).
- IQK: solo si `bNeedIQK` (`:2113-2117`), igual que en cualquier salto de canal de scan/join; no
  falta nada respecto al comportamiento normal del driver.

**Plan de canales**: el registro pide `CONFIG_RTW_CHPLAN` = 0xFF
(`include/drv_conf.h:247`; `VendorProbe.c:326`), que es inválido por
`rtw_is_channel_plan_valid` (`core/rtw_chplan.h:134`) y se ignora en
`hal_com_config_channel_plan` (`hal/hal_com.c:439-445`); cae al plan por defecto
`RTW_CHPLAN_REALTEK_DEFINE` (`hal/rtl8814a/rtl8814a_hal_init.c:1241`,
`hal/hal_com.c:450-451`) = 2G world + 5G FCC1 (`core/rtw_chplan.c:385-386`): 1-13,
36-64, 100-140 (DFS), **149-165 activos** (`rtw_chplan.c:189`). 6, 44 y 149 están y ninguno es
DFS ni pasivo; `rtw_chset_search_ch` (`rtw_mlme_ext.c:828-842`) los acepta.

**DFS/CSA**: `CONFIG_DFS_MASTER` no está definido, así que no hay CAC ni radar-detect como
maestro; `rtw_rfctl_is_tx_blocked_by_ch_waiting` (`rtw_mlme_ext.c:414-417`) solo es verdad con
radar detectado o `csa_ch` pendiente, estados que no se alcanzan en reposo (el CSA del AP
necesita enlace, `rtw_mlme_ext.c:3190-3227`). El emisor crudo ya consulta esa función
(`AWDLTx.c:36-37`); el anclaje no la necesita.

**RCR/MSR en reposo**: con el puerto 1 encendido, `rtl8814native_awdl_port1` mantiene
`RCR_CBSSID_DATA` fuera en **cada** reevaluación — scan enter/done, connect/disconnect
(`hal/hal_com.c:3829` y llamadas en `:11378/:11408`, `core/rtw_mlme_ext.c:12398`, `core/rtw_sreset.c:184`)—
así los data AWDL con BSSID ajeno siguen llegando; `RCR_CBSSID_BCN` queda puesto en reposo
(`hal_com.c:3844-3850`) y solo filtra beacons, no action frames. MSR: puerto 0 en STATION tras
desconexión (`rtw_mlme_ext.c:12409`), puerto 1 en ADHOC por `rtlRadioAwdlPort`
(`Source/Driver/AWDLTx.c:92-94`); bits independientes.

Conclusión: `rtw_set_chbw_cmd(ch, 20 MHz, 0)` basta; no falta RF, BB, potencia por canal, ni
comprobación adicional para RX/TX en 149/44/6.

## 4. TX de gestión sin asociación (pregunta 3)

Funciona; es exactamente el camino de los probe requests de un scan sin asociar:

- `alloc_mgtxmitframe` saca de la cola de gestión, sin dependencia de enlace; `update_mgntframe_attrib`
  (`core/rtw_mlme_ext.c:7616`) pone `mac_id = RTW_DEFAULT_MGMT_MACID` (= 1, `include/drv_types.h:806`),
  `qsel = QSLT_MGNT`, `raid` de modo G y **`rate = pmlmeext->tx_rate`** (`:7663-7665`) — el OFDM
  6 Mb/s que el anclaje fijó. El descriptor lo vuelca directo: `SET_TX_DESC_TX_RATE_8814A`
  (`hal/rtl8814a/usb/rtl8814au_xmit.c:232`).
- El macid 1 es el asiento por defecto de gestión, configurado en la init; no depende de
  stainfo. `update_mgntframe_attrib_addr` (invocado por `rtw_hal_mgnt_xmit`,
  `hal/hal_intf.c:816-820`) busca stainfo del RA y lo deja a NULL sin problema (la
  precedencia: `_issue_probereq` usa el mismo `alloc_mgtxmitframe`/`dump_mgntframe` durante
  scans sin asociar, `core/rtw_mlme_ext.c:8453`).
- Puertas de apagado: `RTW_CANNOT_RUN` en `dump_mgntframe` (`core/rtw_mlme_ext.c:7737`) y el
  propio emisor (`AWDLTx.c:34`); `RTW_CANNOT_TX` (`drv_types.h:1800`) se comprueba en la escritura
  USB (`hal/rtl8814a/usb/usb_ops_linux.c:212`). Definiciones en `drv_types.h:1786/1800`.
- RA: los action frames AWDL llevan A1 broadcast; el emisor desactiva reintentos
  (`AWDLTx.c:54-55`). La extensión de data de `42c04d3` comparte atributos y las mismas
  conclusiones.

## 5. Barridos de airportd sin asociar y cadencia de re-anclaje (pregunta 4)

- **Durante un barrido** no hay TX AWDL ni petición de anclaje: `st.scanning` = 1 hace que
  `txTick` no sirva canal y `parkRadio` no se llame (`AWDLInterface.cpp:912-919`).
- **Al terminar**, `survey_done_set_ch_bw` restaura `cur_channel` (§2): si ya estaba anclado en
  149, la reanudación no necesita petición nueva y ocurre en el tick siguiente, ≤ 1 AW =
  16,384 ms (`AWDLInterface.cpp:737-742`). El reloj y las ventanas se cierran/reabren alrededor
  del barrido (eventos 43/41), sin desalineación (el reloj es propio, `2bb1c45`).
- **Re-anclaje**: solo hace falta cuando algo reescribe `cur_channel` — un join (reescritura en
  `rtw_mlme_ext.c:14194`) o el arranque. En esos casos, una petición por desconexión más
  reintentos a 1 Hz (`parkRadio` 1048-1050) hasta que el hdl la aplica; nunca más de 1 p/s.
- **Duración del hueco**: el barrido usa `SURVEY_TO` = 100 ms por canal
  (`include/rtw_mlme_ext.h:24`); el plan completo son 37 canales (§3), ~3.7 s por barrido más
  saltos. El intervalo con que airportd barre sin asociar no está en este repositorio: `?`. El
  efecto es aritmético: cada barrido silencia AWDL su duración completa; entre barridos el TX
  funciona sin peticiones extra.

## 6. Hallazgos y fixes propuestos (sin aplicar)

**H-A (media-alta) — el hdl no reexamina el estado MLME.** `rtw_set_chbw_hdl`
(`rtw_mlme_ext.c:16539`) aplica el canal aunque entre la comprobación de `AWDLTx.c:119` y la
ejecución haya empezado un join (el peor caso, §2) o un scan (inofensivo). Fix exacto, optativo
para el resto de llamadores (CSA en `rtw_mlme_ext.c:3227`, `rtw_wlan_util.c:2757`):

1. `include/rtw_cmd.h:898` — ampliar el parámetro con semántica:
   ```c
   struct set_ch_parm {
       u8 ch;
       u8 bw;
       u8 ch_offset;
       u8 idle_only; /* RT8814Native: skip if LINKED/UNDER_LINKING/UNDER_SURVEY */
   };
   ```
2. `core/rtw_cmd.c` (`rtw_set_chbw_cmd`, `:2153`): reutilizar el parámetro `flags` con una
   constante nueva (p. ej. `#define RTW_CHBW_IDLE_ONLY 0x80`, distinta de `RTW_CMDF_DIRECTLY/
   WAIT_ACK`) y hacer `set_ch_parm->idle_only = !!(flags & RTW_CHBW_IDLE_ONLY);`
3. `core/rtw_mlme_ext.c:16539` (`rtw_set_chbw_hdl`): antes de escribir:
   ```c
   if (set_ch_parm->idle_only &&
       check_fwstate(&padapter->mlmepriv, _FW_LINKED | _FW_UNDER_LINKING | _FW_UNDER_SURVEY))
       return H2C_SUCCESS; /* estado cambió en vuelo: no tocar */
   ```
4. `Source/Driver/AWDLTx.c:123`: pasar `RTW_CHBW_IDLE_ONLY`.

La capa ya se re-sincroniza sola (`radioStatus` + reintento a 1 Hz), así que el skip no necesita
notificación al llamador.

**H-B (media) — `kIOReturnBadArgument` se reintenta a 1 Hz para siempre y en silencio.** Si el
canal no está en el chset (`AWDLTx.c:117`), `parkRadio` solo da por terminal
`kIOReturnUnsupported` (`AWDLInterface.cpp:1052-1056`); un `BadArgument` (p. ej. chset vacío por
un plan inválido en futuras variantes) repetiría cada segundo sin log. Fix exacto en
`parkRadio`: tratar `kIOReturnBadArgument` como terminal, igual que Unsupported:

```c
} else if (r == kIOReturnUnsupported || r == kIOReturnBadArgument) {
    LOG("awdl: radio cannot park on a channel (0x%x), AWDL only on an associated social channel", r);
    _parkCh = 0;
}
```

(Con el plan REALTEK_DEFINE actual, 6/44/149 existen y el caso no se da hoy; el fix lo hace
inmune a configuraciones futuras.)

**H-C (baja, observación sin fix).** `_parkReqUs` se escribe en `parkRadio` fuera de `_txLock`
(`AWDLInterface.cpp:1050`) y `setRadioTx` lo reinicia dentro (`:870`). Hoy no hay carrera
alcanzable: `setRadioTx(false)` hace `thread_call_cancel_wait` (`:884`) y el reinicio solo ocurre
en `on && !was`, cuando ningún tick está en vuelo. Si se añaden más hilos que llamen a
`setRadioTx`/`parkRadio`, mover el reinicio fuera del cerrojo o el acceso a `parkRadio` dentro.

**H-D (baja, comportamiento aceptado).** Con `404af9b`, si el 129 cambia entre ticks, el objetivo
puede alternar entre dos canales sociales (p. ej. 44↔149); el acelerador de 1 s limita el coste a
un cambio de canal por segundo y `servedMask` se lee del mismo `_seq` bajo el mismo cerrojo, así
que no hay incoherencia interno. Los pares del canal abandonado dejan de oír AWDL hasta volver:
conforme al diseño de radio fija.

**H-E (información).** El primer anclaje desde el arranque parte de `registrypriv.channel` = 1
(`VendorProbe.c:275`): implica un cambio de banda 2.4→5 GHz resuelto dentro de
`phy_SwChnl8814A` (§3); nada más que hacer.

## 7. Verificación del texto de los commits

- `07a41e8` — «while not associated the radio parks … through the command thread» (encola, el hdl
  corre en el hilo de comandos: cierto); «with management frames at OFDM 6 Mb/s» (`AWDLTx.c:122`,
  `rtw_mlme_ext.c:7546`); «later scans return to it» (`survey_done_set_ch_bw`, excepción P2P
  LISTEN inoperante aquí, §2); «idle_channel reports where an idle radio sits»
  (`VendorProbe.c:1202-1203`). «kIOReturnBusy while linked, joining or scanning» (ABI, `:119` y
  mapeo en `RTL8814Native.cpp:511-519`): cierto. Correcto.
- `ab11c69` — «parks it on a social channel (149, or 6/44 with rtlawdl_ch)»: cierto en su
  versión; `404af9b` amplió el objetivo al 129 del gestor (§1). «serves that channel once the
  radio sits there» (`idleCh == target`, `:1046-1047`): cierto. «repeating the request at most
  once a second after a disconnect»: cierto (§2, §5). Correcto con la enmienda posterior.

## 8. Resumen

- El anclaje es suficiente para RX y TX AWDL en 6/44/149 desde reposo: RF, BB, banda, potencia
  por canal y RCR/MSR quedan cubiertos por la vía estándar del vendor (§3).
- La TX de gestión (y la de data de `42c04d3`) funciona sin asociación por el mismo camino que
  los probe requests (§4).
- Los barridos pausan el TX AWDL su duración completa y no exigen re-anclaje; el re-anclaje solo
  aparece tras un join/desconexión, a 1 Hz como máximo (§5).
- Dos hallazgos accionables: H-A (guarda de estado en el hdl, §6) y H-B (BadArgument terminal,
  §6). Ambos con fix propuesto y sin aplicar. IPS/LPS no existen en esta compilación y el
  dynamic check no toca canal (§2).

## 9. Estado tras la revisión

Los números de línea de este documento se refieren al árbol en el momento de la revisión
(`404af9b` más los cambios de `42c04d3`); los commits posteriores los desplazan.

| # | Estado |
|---|---|
| H-A | Aplicado en `8726bd4`: `rtw_set_chbw_cmd` acepta `RTW_CMDF_CHBW_IDLE_ONLY`, que guarda `idle_only` en `set_ch_parm`, y `rtw_set_chbw_hdl` no toca el canal si el radio ya está asociado, uniéndose o escaneando |
| H-B | Aplicado en `9dc71a6`: `parkRadio` deja de pedir el anclaje también ante `kIOReturnBadArgument` y registra el canal y el código |
| H-C, H-D, H-E | Sin cambio |
