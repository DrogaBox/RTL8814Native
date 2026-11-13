# El offload de conmutación de canal del firmware del RTL8814A (H2C 0x1C y 0x87)

Cierra la línea abierta en `AWDL_FW_8814A_MCC.md` §8 (último punto: el offload de conmutación
0x1C/0x87 «existe en el firmware, aunque el driver del árbol no lo usa y su latencia real es `?`»)
y el descarte de `AWDL_MCC_TIEMPO_COMPARTIDO.md` §3.3 (el offload se abandonó porque exige
`RegFWOffload == 2`). Documenta qué hacen exactamente los dos handlers de conmutación de canal
del firmware v33.6 (2019-06-11): el H2C `CHNL_SWITCH_OPER_OFFLOAD` 0x1C (`sub_8000812C`) y el
H2C `CHNL_SWITCH_OFFLOAD` 0x87, variante WoWLAN (`sub_8000817C`), con el layout de argumentos,
todas las escrituras de registro y RF que ejecutan, el C2H 0x10 que devuelven y los bucles de
espera que acotan la latencia; y lo compara paso a paso con lo que hace el driver del árbol
(`PHY_HandleSwChnlAndSetBW8814A`) por USB.

Direcciones en hexadecimal sin prefijo = direcciones del firmware (IRAM en 0x80000000, DMEM en
0x80200000, misma convención que `AWDL_FW_8814A_MCC.md`). Lo no verificado se marca `?`. Nada de
esto se ha medido en hardware: la latencia queda pendiente de medición, no de estimación.

**Respuesta corta.** Ambos handlers hacen en el MCU, a velocidad de registro, exactamente la parte
física de una conmutación de canal del 8814A: tabla `fc_area` (BB 0x860), escritura de `RF_CHNLBW`
(registro RF 0x18, retune del sintetizador) en las 4 rutas, selección de tabla AGC por banda
(BB 0x958), TRXPTCL (0x668), `DATA_SC` (0x483), `rRFMOD` (0x8AC), 0x8C4, 0x864, 0x838, 0x848 y los
bits de ancho de RF 0x18 — los mismos registros, máscaras y valores que `phy_SwChnl8814A` +
`phy_SetBwMode8814A` escriben por USB. No recargan potencia TX, no corren IQK, no tocan las tablas
NBI/CSI ni el reloj ADC, y no conmutan banda. Devuelven `C2H_FW_CHNL_SWITCH_COMPLETE` (0x10) con el
canal nuevo en el primer byte de carga y longitud declarada 7. El 0x1C aplica el ancho y los
offsets SC que le llegan en el byte 1; el 0x87 conmuta siempre a 20 MHz y verifica por lectura el
cambio de RF.

## 1. Punto de entrada

El dispatcher de H2C `sub_80005668` (mapa completo en `AWDL_FW_8814A_MCC.md` §5) saca de la cola
DMEM (entradas de 8 bytes `[id][7 args]`, drenadas por `sub_80005834` desde el bucle principal
`sub_800016EC`) y llama a cada handler con el puntero de carga en `a0`:

- id 0x1C → `jal sub_8000812C` en 0x8000574E (con `move a0, a1` en 0x8000574C: `a0` apunta al
  primer byte de carga, no al id);
- id 0x87 → `jal sub_8000817C` en 0x8000580E.

El 0x1D `SINGLE_CHANNELSWITCH_V2` sigue siendo un stub vacío (`jr ra` en 0x80008128).

## 2. Layout de argumentos del H2C 0x1C

`sub_8000812C` lee tres bytes de la carga (`lbu` en 0x80008134-0x8000813A) y los trocea así:

| Byte | Bits | Campo | Consumidor |
|---|---|---|---|
| 0 | 7:0 | canal (el valor que entra en RF 0x18[7:0]) | `sub_800070FC` |
| 1 | 1:0 | ancho (`CHANNEL_WIDTH_20/40/80` = 0/1/2) | ambos |
| 1 | 4:2 | SC de 40 MHz (offset del primario dentro del 40) | `sub_800070B4` |
| 1 | 7:5 | SC de 80 MHz (offset del primario dentro del 80) | `sub_800070B4` |
| 2 | 3:0 | tipo RFE | leído (0x80008144) y pasado a `sub_800070FC`, que lo ignora `?` |

El byte 3 y el resto de la entrada de cola (7 bytes de carga disponibles) no se leen.

Este layout coincide byte a byte con las macros del emisor del árbol
(`include/hal_com_h2c.h:458-462`): `SET_H2CCMD_CH_SW_OPER_OFFLOAD_CH_NUM` byte 0 completo,
`BW_MODE` byte 1 [1:0], `BW_40M_SC` byte 1 [4:2], `BW_80M_SC` byte 1 [7:5],
`RFE_TYPE` byte 2 [3:0]. El driver manda 4 bytes (`ch_sw_h2c_buf[4]`, `hal_com.c:11991-12007`).

## 3. El handler 0x1C paso a paso (`sub_8000812C`)

1. **Parte de canal** — 0x8000814A: `sub_800070FC(a0=canal, a1=ancho, a2=SC40, a3=RFE)`.
   Dentro de `sub_800070FC` solo se usa `a0` (`s0 = a0 & 0xFF` en 0x80007106-0x80007108): ancho,
   SC y RFE se leen pero no afectan a esta mitad. Ver §4.
2. **Parte de ancho** — 0x8000815C: `sub_800070B4(ancho, SC80, SC40)` con
   `a0 = byte1 & 3` (ancho), `a1 = byte1 >> 5` (SC 80), `a2 = (byte1 >> 2) & 7` (SC 40)
   (0x8000814E-0x8000815A). Ver §5.
3. **C2H** — 0x80008160-0x8000816E: compone el registro y llama a `sub_80004CC8`. Ver §7.

No hay ninguna otra ruta: sin verificaciones previas, sin early-return, sin estado.

## 4. `sub_800070FC`: la mitad de canal

Secuencia verificada en el desensamblado (296 bytes):

1. **`fc_area`** (0x80007104-0x80007188): según el canal, escribe BB 0x860
   (`rFc_area_Jaguar`, máscara leída de la DMEM 0x7E4 = 0x1FFE0000) con
   `sub_80006AE4(0x860, 0x1FFE0000, valor)`:

   | Canal | Valor 0x860 | Igual que el driver |
   |---|---|---|
   | 36–48 | 0x494 | `rtl8814a_phycfg.c:1927` |
   | 50–64 | 0x453 | :1929 |
   | 100–116 | 0x452 | :1931 |
   | ≥ 118 | 0x412 | :1933 |
   | resto (2,4 GHz) | 0x96A | :1935 |

2. **Retune del sintetizador, las 4 rutas** (bucle 0x8000718A-0x800071DE, `s1` de 0 a 3): calcula
   el campo `RF_MOD_AG` con la misma tabla que el driver
   (`36–64 → 0x101`, `100–140 → 0x301`, `>140 → 0x501`, resto `0x000`; driver :1939-1946), arma
   `valor = canal | (RF_MOD_AG << 8)` y llama a `sub_80006FE0(ruta, 0x18, máscara 0x703FF, valor)`.
   La máscara sale de la DMEM 0x7EC = 0x000703FF = `BIT18|BIT17|BIT16|BIT9|BIT8|bMaskByte0`, y el
   desplazamiento 8 de `sub_800043B8` sobre la DMEM 0x7E8 = 0x00070300 — idéntico al
   `BitMask`/`BitShift` de `phy_SwChnl8814A` (:1949-1955). **Sí retunea el sintetizador**: el byte
   bajo de RF 0x18 (`RF_CHNLBW`) recibe el canal tal cual.

3. **Tabla AGC por banda** (0x800071E0-0x80007216): BB 0x958 (`rAGC_table_Jaguar2`, máscara 0x1F)
   = 1 para 36–64, 2 para 100–144, 3 para ≥ 149 — los mismos casos del driver (:1958-1963).

Detalle de la maquinaria RF/BB compartida (todos los accesos del handler pasan por aquí):

- **Escritura BB con máscara** `sub_80006AE4(reg, máscara, valor)`: con máscara completa escribe
  32 bits en `0xB8640000 + (reg & 0xFFFF)` (0x80006AFA-0x80006B06); con máscara parcial lee el
  registro con `sub_80006AAC`, mezcla en registros y escribe (0x80006B08-0x80006B34). La base
  0xB8640000 vive en la DMEM 0x4C0 (la ventana BB; `AWDL_FW_8814A_MCC.md` §3).
- **Escritura RF** `sub_80006FE0(ruta, reg, máscara, valor)`: si la máscara no es completa, lee el
  valor viejo con `sub_800068B0`, mezcla (`sub_800043B8` da el desplazamiento del bit más bajo) y
  llama a `sub_80006FA4` (0x80007004-0x80007028); después **gira un retardo ocupado**
  `sub_800046A8(1)` (0x8000702C-0x8000702E) tras cada escritura RF.
- **Lectura RF** `sub_800068B0(ruta, reg)`: lee 32 bits en
  `{0xB8642800, 0xB8642C00, 0xB8643800, 0xB8643C00}[ruta] + reg*4`
  (ventanas en la DMEM 0x778-0x784, inicializadas en la imagen). Son las mismas direcciones BB que
  `phy_RFRead_8814A` usa por USB (`0x2800/0x2c00/0x3800/0x3c00 + RegAddr*4`,
  `rtl8814a_phycfg.c:101-113`).
- **Escritura RF de tres hilos** `sub_80006FA4(ruta, reg, valor)`: escribe
  `(reg << 20) | (valor & 0xFFFFF)` enmascarado con 0x0FFFFFFF en el registro BB
  `*(u16*)(0x80203024 + ruta*4) + 0xB8640000`. La tabla de la DMEM 0x3024 está a cero en la imagen
  y la llena `sub_800068FC` (llamada desde el init en 0x80001CAC) con {0xC90, 0xE90, 0x1890,
  0x1A90} = `rA_LSSIWrite_Jaguar`, `rB_LSSIWrite_Jaguar`, `rC_LSSIWrite_Jaguar2`,
  `rD_LSSIWrite_Jaguar2` — los mismos `rf3wireOffset` que arma el driver
  (`rtl8814a_phycfg.c:259-274`; defines en `Hal8814PhyReg.h:72-82`). El formato de palabra es el
  de `phy_RFWrite_8814A` (:152-155).
- **Retardo** `sub_800046A8(n)`: bucle ocupado de `n × *(DMEM 0x802016A0)` iteraciones
  (0x800046A8-0x800046C2). La unidad la fija en el arranque `sub_800045A4` (0x800045A4-0x8000467A)
  según un valor pedido (10/20/25/40/50/80/100/200 → unidades 2/3/9/5/17/10/34/67): es el ajuste
  de «espera de µs» del firmware, así que una unidad es del orden del µs `?`. Con `n=1` tras cada
  escritura RF el coste añadido es pequeño pero no cuantificable estáticamente `?`.

Recuento de la mitad de canal: 1 escritura BB (0x860) + 4×(1 lectura + 1 escritura RF) +
1 escritura BB (0x958) ≈ 10 operaciones de registro, todas dentro del MCU.

## 5. `sub_800070B4`: la mitad de ancho

Con `(a0=ancho, a1=SC80, a2=SC40)` (0x800070BC-0x800070CA):

1. **TRXPTCL** — `sub_8000693C(ancho)` (0x800070CA): lee 16 bits de 0xB8640668
   (`REG_TRXPTCL_CTL_8814A` = 0x668, punteros en la DMEM 0x78C/0x790), limpia los bits 0/7/8 y
   pone: ancho 0 → ninguno (0x8000695C: máscara 0xFE7F), ancho 1 → bit 7 (0x80006964), ancho 2 →
   bit 8 (0x80006970); escribe byte bajo en 0x668 y alto en 0x669. Es `phy_SetBwRegMac_8814A`
   (:1282-1310) con las mismas máscaras (0xFE7F / bit 7 con 0xFEFF / bit 8 con 0xFF7F); única
   divergencia: el firmware fuerza además el bit 0 a 0 en los anchos 1 y 2 (máscara ~0x181), el
   driver lo conserva `?` (bit 0 de 0x668 sin nombre en el árbol).
2. **`DATA_SC`** — `sub_80006998(ancho, SC80, SC40)` (0x800070D4) calcula el byte SC y el propio
   `sub_800070B4` lo escribe como un byte en 0xB8640483 = `REG_DATA_SC_8814A` (0x483; puntero en la
   DMEM 0x7E0) (0x800070D8-0x800070E4). La tabla de `sub_80006998` es exactamente la de
   `phy_GetSecondaryChnl_8814A` (:1233-1280): con ancho 2 (80 MHz), nibble alto = 0xA si SC80 =
   LOWER (1), 9 si UPPER (2); nibble bajo = 4 (20_LOWEST) con {SC40=1,SC80=1}, 1 (20_UPPER) con
   {SC40=2,SC80=1}, 2 (20_LOWER) con {SC40=1,SC80=2}, 3 (20_UPPERST) con {SC40=2,SC80=2}; con
   ancho 1 (40 MHz) = 1 si SC40 = 2, 2 si SC40 = 1; con ancho 0 devuelve 0. Los enums coinciden
   (`VHT_DATA_SC_*`, `rtw_rf.h:129-141`; `HAL_PRIME_CHNL_OFFSET_*`, `rtw_rf.h:87-89`). El firmware
   añade un mapeo de 20 MHz que el driver no tiene (usa SC80/SC40 aunque el ancho sea 20); es
   inalcanzable con el emisor del árbol, que deja ambos SC a 0 en 20 MHz.
3. **Configuración BB del ancho** — `sub_80006E08(ancho, SC40)` (0x800070E6; dentro, `s0` = SC40):

   | Registro | Máscara (DMEM) | Valor por ancho | Nota |
   |---|---|---|---|
   | 0x8AC `rRFMOD` | 0x003003C3 (0x7B8) | 0x00300200 (20) / 0x00300201 (40) / 0x00300202 (80) | fija [1:0] = ancho, [9:8] = 2, [21:20] = 3, [7:6] = 0. El driver solo escribe [1:0] (`phy_SetBwRegAdc_8814A`, :918-947); los demás campos quedan en el driver como los dejara el init `?` |
   | 0x8C4 | 0x40000000 (0x3DC) | 0 (20/40), 0x40000000 (80) | registro sin nombre en el árbol |
   | 0x8AC [5:2] | 0x3C | SC40 << 2 (40 y 80) | igual que `phy_SetBwMode8814A` :1688/:1697 |
   | 0x838 `rCCAonSec_Jaguar` | 0xF0000000 (0x578) | SC40 << 28 (40 y 80) | el driver no lo toca en su ruta de ancho |
   | 0x864 | 0x001C0000 (0x7C0) | 0x00100000 (20), 0x00080000 (40/80) | `rFPGA0_XB_RFInterfaceOE` reutilizado; el driver no lo toca aquí |
   | 0x848 `rL1PeakTH_Jaguar` | 0x03C00000 (0x7C8) | 0x01800000 (40), 0x01400000 (80) | condicionado a que la lectura de 0x834 (`rBWIndication_Jaguar`) tenga el bit 26 (máscara DMEM 0x4F0 = 0x04000000) (0x80006ED4-0x80006EF6 y 0x80006F74-0x80006F92) `?` |
   | 0xA00 `rCCK_System_Jaguar` | 0x10 | bit 4 = 1 si SC40 = 1, 0 si no (solo 40 MHz) | igual que :1690-1693 (`VHT_DATA_SC_20_UPPER_OF_80MHZ` = 1) |

   No toca 0x82C (`rAGC_table_Jaguar`, la selección AGC por ancho que el driver sí escribe en
   `phy_SetBwRegAgc_8814A` :953-981) ni 0x8C8.
4. **Bits de ancho de RF** — `sub_8000703C(ancho)` (0x800070EC): escribe RF 0x18 máscara 0xC00 en
   las rutas 0 y 1: 20 MHz → 0xC00, 40 MHz → 0x400, 80 MHz → 0x000. Mismos valores que
   `PHY_RF6052SetBandwidth8814A` (`rtl8814a_rf6052.c:43-62`), pero **solo rutas A y B**: C y D
   conservan sus bits de ancho anteriores `?`.

Recuento de la mitad de ancho: 2 (TRXPTCL, DATA_SC) + ~6 BB + 2×(1R+1W) RF ≈ 12 operaciones, más
1 lectura BB (0x834).

## 6. El handler 0x87 (`sub_8000817C`), variante WoWLAN

1. 0x8000818C: `sub_800070FC(canal, 0, 0, byte2)` — canal del argumento, **ancho 0 y SC 0
   forzados** (siempre 20 MHz; coherente con el uso WoWLAN de despertar en el canal base). El byte
   2 se lee pero `sub_800070FC` lo ignora.
2. 0x80008198: `sub_80006A68(0, 0x18, -1)` — lectura completa del RF 0x18 de la ruta 0 (vía
   `sub_800068B0`; `sub_80006A68` en 0x80006A68-0x80006AAC con máscara −1 = lectura directa).
3. 0x8000819C-0x800081AC: si `(lectura & 0xFF) != canal`, repite `sub_800070FC(canal, 0, 0, byte2)`
   una vez. Sin más reintentos ni bucle: máximo dos intentos, sin espera intermedia más allá de los
   retardos internos de cada escritura RF (§4).
4. C2H (§7).

La variante del driver que lo invocaría está en `phy_SwChnl8814A` (:1922-1923): un solo byte
(canal), y solo con `RegFWOffload == 2`. Para AWDL no aporta nada frente al 0x1C: fija 20 MHz y no
acepta SC.

## 7. El C2H 0x10 que devuelven

Ambos handlers componen el registro de 16 bytes en la pila y llaman a `sub_80004CC8`, que lo copia
al anillo de eventos (anillo, índices y bit de desborde descritos en `AWDL_FW_8814A_MCC.md` §6):

| Offset | Valor | Escritura |
|---|---|---|
| +0 | 0x10 (`C2H_FW_CHNL_SWITCH_COMPLETE`) | 0x80008166 / 0x800081B4 |
| +1 | sin escribir (byte de pila previo) `?` | — |
| +2 | canal nuevo (byte 0 del H2C) | 0x8000816C / 0x800081BE |
| +3..+13 | sin escribir `?` | — |
| +14 | 7 (byte bajo del halfword escrito con `sh`) | 0x80008164 / 0x800081B8 |

El byte +14 es el campo de longitud del formato C2H que lee el host: `C2H_PLEN_88XX` está en el
byte 14 (`hal/hal_com_c2h.h:43`; id en +0, seq en +1, carga desde +2, :40-42). Así que el C2H 0x10
declara **7 bytes de carga**, de los cuales solo el primero tiene contenido garantizado (el canal);
los otros seis son residuo de pila `?`. El halfword +14/+15 (7) de los emisores 0x1C/0x87 coincide
con el patrón de otros emisores (C2H_TXBF 0x02 y C2H_BCN_EARLY_RPT 0x1E escriben 1 con un byte de
carga: `sub_800075E4` en 0x80007680-0x80007688, `sub_80002884` en 0x8000289E-0x800028A8).

Consumo por el driver: por USB el C2H llega como paquete RX (`pkt_rpt_type == C2H_PACKET` →
`rtw_hal_c2h_pkt_pre_hdl`, `hal/rtl8814a/usb/usb_ops_linux.c:181-183`) y
`rtw_hal_c2h_pkt_hdr_parse` separa id = buf[0], seq = buf[1], carga = buf+2 y plen = longitud del
paquete − 2 (`hal_intf.c:1177-1190`); la lectura por registros usa el plen del byte 14
(`c2h_evt_read_88xx`, `hal_com.c:1390-1400`). `c2h_handler_8814a` solo atiende
0x02/0x03/0x04/0x0D y devuelve `_FAIL` para el resto (`rtl8814a_cmd.c:725-749`), con lo que el 0x10
cae al genérico: el caso `C2H_FW_CHNL_SWITCH_COMPLETE` (→ `rtw_tdls_chsw_oper_done`) y su
inclusión en «handle directly» (`hal_intf.c:1231-1235` y :1311-1313) solo compilan con
`CONFIG_TDLS && CONFIG_TDLS_CH_SW`. Sin TDLS_CH_SW el C2H 0x10 se descarta sin efecto.

## 8. Lo que el offload NO hace

- **Conmutación de banda**: ni el 0x1C ni el 0x87 tocan `REG_CCK_CHECK_8814A` (0x454) ni ejecutan
  el equivalente a `phy_SwBand8814A`/`PHY_SwitchWirelessBand8814A` (`rtl8814a_phycfg.c:988-1034`).
  Un salto 2,4 ↔ 5 GHz por esta vía quedaría incompleto `?`. Irrelevante para 149 ↔ 153.
- **Recarga de potencia TX**: ninguna escritura a 0x1998 ni a las tablas `rTxAGC_*` (el 0x834/0x838/
  0x848 que escribe el ancho son otros campos). Es la pieza que en el driver cuesta ~252
  escrituras USB (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §4).
- **IQK**: sin acceso a los registros del IQK ni al bloque de calibración (la clase DM 0x44/0x45
  existe en el dispatcher pero no forma parte de esta ruta).
- **Spur/NBI/CSI y reloj ADC**: nada equivalente a `phy_SpurCalibration_8814A` (:1464, solo actúa
  con `rfe_type == 0` en canales concretos y en el resto reinicia NBI/CSI) ni a
  `phy_ADC_CLK_8814A` (:446, solo cut A).
- **Estado del driver**: el firmware no toca `dvobj->oper_*`, `pHalData->current_*`,
  `mlmeextpriv.cur_*` ni los punteros de phydm (`ODM_CMNINFO_BW/CHNL`, enganchados a
  `pHalData->current_channel_bw/current_channel` en `hal/hal_dm.c:443-444`). Quien use el offload
  debe mantener esos estados a mano.
- **Anuncios 802.11**: sin null data PM ni media-status: eso sigue siendo del driver.

## 9. Comparación paso a paso con `PHY_HandleSwChnlAndSetBW8814A` por USB

Punto de partida del driver: `set_channel_bwmode` (`core/rtw_wlan_util.c:596`) pasa el **canal
central** a `rtw_hal_set_chnl_bw` (`hal_intf.c:1001`; «set center channel», `rtw_wlan_util.c:657`)
→ `PHY_SetSwChnlBWMode8814` (`rtl8814a_phycfg.c:2342`) → `PHY_HandleSwChnlAndSetBW8814A` (:2222,
detección de cambios :2249-2273 y early-return en :2277) → `phy_SwChnlAndSetBwMode8814A` (:2078) →
`phy_SwChnl8814A` (:1883) y `phy_SetBwMode8814A` (:1660), y al final potencia TX (:2117-2119,
condicionada a `awdl_keep_txpwr` en este árbol) e IQK solo con `bNeedIQK` (:2121).

Para el caso AWDL de interés (asociado en 153/80, slot en 149/80 y vuelta):

| Paso | Firmware H2C 0x1C | Driver por USB | Equivalencia |
|---|---|---|---|
| Chequeo previo | ninguno | early-return si canal/ancho/SC/centro no cambian (:2249-2273) | el offload reescribe todo; con los mismos valores es inocuo |
| Banda | no | `phy_SwBand8814A` (:1897) — sin efecto 5G↔5G | igual efecto |
| `fc_area` 0x860 | sí, misma tabla | :1926-1935 | idéntico |
| RF 0x18, 4 rutas, máscara 0x703FF | sí (§4, punto 2) | :1937-1956 | idéntico; el byte 0 = canal recibido (ver §10) |
| AGC banda 0x958 | sí, misma tabla | :1958-1963 | idéntico |
| TRXPTCL 0x668 | sí (§5.1) | `phy_SetBwRegMac_8814A` :1666 | idéntico salvo bit 0 |
| `DATA_SC` 0x483 | sí, misma tabla SC | :1670 + `phy_GetSecondaryChnl_8814A` :1233 | idéntico para 40/80 |
| `rRFMOD` 0x8AC | sí, más campos | :918-947 y :1688/:1697 | el firmware fija [21:20]/[9:8]/[7:6] que el driver no toca aquí `?` |
| 0x8C4, 0x864, 0x838, 0x848 | sí (§5.3) | no en esta ruta | extra del firmware `?` |
| 0xA00 (CCK system) | solo 40 MHz | solo 40 MHz (:1690-1693) | idéntico |
| 0x82C (AGC por ancho) | no | `phy_SetBwRegAgc_8814A` :981 | divergencia: el driver sí lo escribe |
| Bits de ancho RF 0x18[11:10] | rutas A y B (§5.4) | las 4 rutas (`rtl8814a_rf6052.c:43-62`) | divergencia C/D `?` |
| Filtros CCK 2,4 GHz | no | :1974-1992 (solo 2,4 GHz) | irrelevante en 5 GHz |
| Spur/NBI + ADC clock | no | fin de `phy_SetBwMode8814A` :1715-1716 | el driver reinicia NBI/CSI; el firmware no |
| Potencia TX | no | :2117-2119 (~252 escrituras) o nada con `awdl_keep_txpwr` | mismo efecto que el guardia local |
| IQK | no | solo con `bNeedIQK` | normalmente no corre |
| Aviso de fin | C2H 0x10 (§7) | ninguno (síncrono) | el offload es asíncrono |

Coste en el host: el offload se reduce a un H2C de buzón (dos escrituras de 32 bits, buzón y
extensión, sin `CONFIG_H2C_EF`, más el sondeo de `REG_HMETFR_8814A` 0x1CC en
`_is_fw_read_cmd_down`, `rtl8814a_cmd.c:28-35` y :99) — 3-5 operaciones USB — frente a las
~280-300 de la ruta completa o ~22 de la variante mismo-bloque del driver
(`AWDL_MCC_TIEMPO_COMPARTIDO.md` §4 y §4.1). Dentro del MCU, la conmutación completa son ~20
operaciones de registro más los seis retardos de escritura RF (§4); la latencia total depende de
esos retardos y del transporte del C2H: sin medir, sin cifra.

## 10. Usar el 0x1C para mover el primario 153 ↔ 149 dentro del mismo bloque de 80 MHz

Lo que el árbol ya tiene:

- `H2C_CHNL_SWITCH_OPER_OFFLOAD = 0x1C` y `H2C_CHNL_SWITCH_OFFLOAD = 0x87` definidos
  (`include/hal_com_h2c.h:46` y :109).
- Emisor del 0x1C: `rtw_hal_ch_sw_oper_offload(padapter, channel, channel_offset, bwmode)`
  (`hal/hal_com.c:11986-12008`), que arma el buffer con las macros de §2 y llama a
  `rtw_hal_fill_h2c_cmd`. Está entero bajo `CONFIG_TDLS_CH_SW`, que no está definido
  (`include/autoconf.h:168`, comentado), dentro de `CONFIG_TDLS`, que tampoco está definido
  (bloque `autoconf.h:161-169` sin `#define CONFIG_TDLS` en el árbol). Su único llamador es el
  TDLS (`core/rtw_tdls.c:1006`). Resultado: **el 0x1C no se envía nunca tal como está el árbol**.
- Transporte: `FillH2CCmd_8814` (`rtl8814a_cmd.c:60`) por los buzones 0x1D0-0x1DF/0x1F0-0x1FF, que
  es el mecanismo que el firmware consume por la cola DMEM (`AWDL_FW_8814A_MCC.md` §4).
- Receptor del C2H 0x10 ya escrito (dormido tras los mismos ifdefs): §7.
- El 0x87 está cableado en `phy_SwChnl8814A` (:1922-1923) tras `RegFWOffload == 2`, pero
  `RegFWOffload` no se asigna en ninguna parte del árbol (solo la declaración en
  `include/hal_data.h:357`; los otros dos usos están muertos: `#if 0` en :668-672 y el de
  `hal/phydm/rtl8814a/phydm_rtl8814a.c:192`, dentro de un bloque `ODM_WIN` que arranca en :124).
  Queda a 0 y esa rama no dispara.

Qué haría falta para el salto 153/80 ↔ 149/80 por 0x1C:

1. Disponer el emisor fuera del TDLS (quitar los ifdefs de `rtw_hal_ch_sw_oper_offload` o añadir
   un llamador propio) y decidir si se espera el C2H 0x10 (habilitando el caso del genérico) o se
   va fire-and-forget.
2. Pasar el **canal central (155) en el byte 0**, no el primario: el firmware escribe ese valor
   tal cual en RF 0x18[7:0] y en la selección de tabla AGC, y la ruta software del driver trabaja
   con `current_channel = centro` (`rtw_hal_set_chnl_bw` recibe `center_ch`,
   `rtw_wlan_util.c:657`). El llamador TDLS pasa el primario (`rtw_tdls.c:1006`) — para 20/40
   centro y primario difieren igual; con la imagen no se puede saber si el TDLS de Realtek contaba
   con otra convención `?`.
3. Pasar en el byte 1: ancho 2 (80 MHz), `BW_80M_SC` = LOWER (1, fijo para el bloque 149-161 con
   centro 155) y `BW_40M_SC` = 2 al ir a 153, 1 al ir a 149. Con eso el firmware escribe
   `DATA_SC` 0xA1 ↔ 0xA4 y 0x838[31:28] 0x2 ↔ 0x1 (los únicos valores que cambian), y reescribe
   idénticos 0x860, RF 0x18 (155), 0x958 (3), 0x668, 0x8AC, 0x8C4, 0x864 y 0x848. El emisor del
   árbol no sirve tal cual: según el ancho rellena un solo campo SC (`BW_40M_SC` a 40 MHz,
   `BW_80M_SC` a 80 MHz, `hal_com.c:11996-12005`), así que a 80 MHz deja `BW_40M_SC` a 0 y el
   firmware no puede distinguir 149 de 153. Hace falta un emisor que llene los dos campos.
4. Mantener el estado del driver a mano en el mismo orden que `set_channel_bwmode`:
   `rtw_set_oper_ch/bw/choffset` (el patrón de `rtw_tdls.c:1012-1015`) y los `pHalData->current_*`
   (canal central 155, ancho 80, SCs), de los que cuelga phydm (`hal_dm.c:443-444`); respetar
   `dvobj->setch_mutex` frente a cualquier otra conmutación.
5. La potencia TX no hace falta recargarla (el centro no cambia; el guardia `awdl_keep_txpwr` del
   árbol ya evita eso en la ruta software) y el IQK no se dispara.
6. Lo que el offload no cubre sigue siendo del driver: null PM=1/PM=0, bloqueo de colas TX
   (`offch_state`) y restauración de phydm en el siguiente tick.
7. No mezclar con la ruta software en vuelo: el firmware no consulta ni actualiza el estado del
   driver, y una conmutación por `set_channel_bwmode` inmediatamente después repetiría el trabajo.

El 0x87 no sirve para esto (ancho 20 fijo, §6). La verificación por lectura del 0x87 (RF 0x18
contra el canal pedido) sugiere que el firmware considera la escritura de RF 0x18 efectiva de
inmediato; el 0x1C no la replica `?`.

## 11. Qué medir en hardware

1. Latencia end-to-end del 0x1C (H2C → C2H 0x10 visible por la ruta USB) para 155/80 con SC40
   alternando 1/2, frente a la medida del driver (`rtlRadioAwdlOffch` ya registra
   «radio to channel ... took N us»): cerrar el coste de los seis retardos de escritura RF (§4).
2. Espectro/tramas mientras el offload conmuta: confirmar que con byte 0 = 155 el enlace de 153/80
   se mantiene y que el primario efectivo pasa a 149 (RX AWDL en el slot).
3. Potencia TX y EVM en 149 y 153 tras conmutaciones repetidas por offload sin recarga (misma
   prueba de `AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.4, ahora con la vía de firmware).
4. Que `c2h_handler_8814a` nunca ve basura en el plen=7 del C2H 0x10 con el parseo por paquete
   (los seis bytes residuales no deben confundir a ningún handler futuro).

Método y artefactos: base de desensamblado del firmware IRAM/DMEM con las tablas de la DMEM como
referencias, lectura de los dos handlers, de `sub_800070FC`/`sub_800070B4` y de toda su cadena de
acceso RF/BB, y contraste directo con el fuente del vendor tree. Volcados y base en
`~/re_cache/ida/task_fw_chsw/`; el desensamblado completo previo sigue en
`~/re_cache/ida/task_fw_mcc/`.
