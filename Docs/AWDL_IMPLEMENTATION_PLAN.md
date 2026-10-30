# Plan de implementación AWDL para la capa RTL8814

Pasos numerados en orden de dependencia para llevar `RTL8814AirLayer.kext` + `RTL8814Native.kext`
desde el estado de la etapa 1 (`awdl0` creada y aceptada por `wifip2pd`, sin radio AWDL) hasta un
datapath AWDL completo: puerto 1, recepción y desvío, inyección de action frames, plantillas, reloj
de franjas, emisión de PSF/MIF, datos, multicast y Block Ack.

Base documental: `AWDL_RTL8814_RX_ACK.md`, `AWDL_RTL8814_TX_RECIPE.md`, `AWDL_WINDOW_TIMER.md`,
`AWDL_PSF_MIF_EMISSION.md`, `AWDL_DATA_ENCAP.md`, `AWDL_BLOCK_ACK.md`,
`AWDL_CHANNEL_SEQUENCE_CONTROL.md`, `AWDL_CAPS_ADVERTISED.md`, `AWDL_PEERMANAGER_DRIVER_ABI.md`
(§0–§3), `AWDL_ETAPAS_2_A_4.md`, `AWDL_STAGE2B_MBUF_TO_PACKET.md`, `AWDL_ESTADO_Y_BLOQUEO.md` e
`AWDL_INDICE.md` §8, más `AWDL_PEER_PRESENCE_PPP.md` para la entrega RX. Líneas de código
verificadas sobre el árbol de trabajo del 2026-10-30 (`8ca31c5`). `Source/AirLayer/RTLAirLayer.cpp`
tiene cambios de otra sesión sin commitear, así que sus números de línea pueden moverse al
integrarlos. Lo no respaldado por un documento o línea citada lleva `?`.

Convenciones del plan:

- Cada paso lleva un **boot-arg propio que lo mantiene desactivado por defecto**, comprobado con
  `PE_parse_boot_argn` en el arranque que corresponda (patrón de `rtlawdl` en
  `Source/AirLayer/RTLAirLayer.cpp:1453-1457`). El orden de activación es acumulativo: ningún paso
  exige el boot-arg del siguiente.
- «La capa» = `RTL8814AirLayer.kext` (`Source/AirLayer/`); «la radio» = `RTL8814Native.kext`
  (`Source/rtl8814au/`, `Source/Driver/`). El enlace entre ambos son las funciones de plataforma de
  `Source/Driver/RTLRadioABI.h` (`RTL_RADIO_FN_ABI` :24, `RTL_RADIO_FN_MAC` :31,
  `RTL_RADIO_FN_DATA_HOOK` :41, `RTL_RADIO_FN_TX` :42) y `Source/Driver/RTL8814Native.cpp`
  (:443, :486, :500, :504).
- Los literales de log de la capa salen por `rtlAirLog` (`Source/AirLayer/RTLAirLayer.cpp:61-62`,
  visible en el anillo `RTLAirLayerLog` del nodo `RTL8814Native`, `ioreg`); los de la radio, por los
  canales `JL_*` de `Source/Driver/RTLJoinLog.h` (:32, :37, :42) vía `rtl8814native_jlog`
  (`Source/rtl8814au/include/rtw_mlme_ext.h:1322`).
- Herramienta de replay de capturas: `~/Desktop/AWDLLab/tools/awdl_af_extract.py` (registros
  `AWAF` con origen, canal, RSSI, subtipo y cuerpo desde el byte de categoría, listos para
  `bpfTapInput`) y `awdl_pcap.py` (resumen de PSF/MIF, TLV 4 y orden de TLVs); capturas fuente
  `refs/owly/tests/captures/`, revisión `9a676e1` (`AWDL_REFERENCIAS_EXTERNAS.md` §4). Ambas viven
  en el repositorio privado del laboratorio (`HANDOFF_AWDL.md`, apartado del laboratorio).

## 0. Punto de partida

- `awdl0` nace por el handler del 94 (`Source/AirLayer/RTLAirLayer.cpp:577-589`) y se registra con
  tres colas sin datapath (`Source/AirLayer/AWDLInterface.cpp:142-174, 176-229`); la acción TX es
  `txNoop` (`AWDLInterface.cpp:257-271`), que consume el lote por la cola de completado.
- Los 18 setters AWDL responden éxito sin efecto (macro `AWDL_ACK`,
  `Source/AirLayer/AWDLInterface.h:314-338`, cuerpos en `AWDLInterface.cpp:341-365`); los GET y el
  resto responden `kIOReturnUnsupported` (`AWDL_STUB`, `AWDLInterface.h:340-394`).
- El kernel de la familia ya instancia el `IO80211AWDLPeerManager` para rol 6 y ha completado
  `configure`, `interfaceStateChange` (116) y `enableAWDL` (155/129/117) en hardware, con la
  elección en solitario cada segundo (`AWDL_ESTADO_Y_BLOQUEO.md` §1); nada llega a la radio porque
  los setters no la tocan.
- La radio entrega a la capa el tráfico de datos de `en2` por el gancho `RTL_RADIO_FN_DATA_HOOK`
  (`RTLAirLayer.cpp:1308-1315`, `Source/Driver/RTL8814Native.cpp:500`) con frames Ethernet; no
  existe canal para frames 802.11 crudos ni para TX de management.

## 1. Puerto 1: MAC de `awdl0`, BSSID AWDL y filtro de datos

**Qué.** Programar el puerto 1 del MAC en modo ad-hoc con la MAC de `awdl0` y el BSSID AWDL
`00:25:00:FF:94:73`, y retirar `RCR_CBSSID_DATA` mientras `awdl0` esté activa, para que el
hardware acepte y reconozca (ACK) las tramas AWDL.

**Dónde.**

- Radio, nueva función de programación del puerto 1 (modelo `rtw_hal_set_msr`, caso `HW_PORT1`,
  `Source/rtl8814au/hal/hal_com.c:2382-2396`: bits 18-19 de `REG_CR` = 1) y escritura de
  `REG_MACID1` `0x0700` / `REG_BSSID1` `0x0708` (volcado vivo con ambos a `0x87654321`:
  `AWDL_RTL8814_RX_ACK.md` §2.1). Con `CONFIG_CONCURRENT_MODE` desactivado
  (`Source/rtl8814au/include/autoconf.h:113`) ninguna llamada existente escribe el puerto 1: la
  función nueva escribe los registros directamente `?` (o llama a `rtw_hal_set_macaddr_port` /
  `rtw_hal_set_bssid` / `rtw_hal_set_msr` con un adaptador cuyo `hw_port` sea `HW_PORT1`,
  `AWDL_RTL8814_RX_ACK.md` §4).
- Radio, retirada de `RCR_CBSSID_DATA`: precedido por `rtw_hal_rcr_set_chk_bssid`
  (`Source/rtl8814au/hal/hal_com.c:3820-3828`), que ya la quita con AP, mesh, TDLS y escaneo. Añadir
  la condición «`awdl0` activa» al mismo sitio o en la llamada que el paso activa.
- Enlace: nueva entrada en `Source/Driver/RTLRadioABI.h` (propuesta: `RTL_RADIO_FN_AWDL_PORT`,
  parámetros MAC de `awdl0` + BSSID + activar/desactivar) `?` (nombre y firma por decidir), y su
  despacho en `Source/Driver/RTL8814Native.cpp` (patrón de :500).
- Capa: almacenar el BSSID del 125 y la MAC de creación. `setAWDL_BSSID` hoy es `AWDL_ACK` puro
  (`Source/AirLayer/AWDLInterface.h:315`, cuerpo `AWDLInterface.cpp:347`); añadir un campo `_bssid[6]`
  en `RTLAwdInterface` (`AWDLInterface.h:397-402`) y la MAC ya está en `_mac` (rellenada por
  `initWithController`, `AWDLInterface.cpp:98-126`). Tras almacenar ambos, llamar a la nueva función
  de plataforma.

**Qué sirve.** El 125 (`setAWDL_BSSID`) que `wifip2pd`/`airportd` bajan al crear `awdl0`, y la MAC
del 94. Es prerrequisito del ACK hardware de todo unicast a `awdl0` (datos y ADDBA) y de la
recepción de datos con A3 ajeno (`AWDL_RTL8814_RX_ACK.md` §2.3-§2.4).

**Prueba sin par Apple.** Volcado de registros en vivo (patrón `Docs/regdumps/`): `REG_CR` bits
18-19 = 1, `REG_MACID1`/`REG_BSSID1` con los valores programados, `REG_RCR` sin `CBSSID_DATA`;
log `LOG("awdl: port1 ...")` en la capa. RX real: con un segundo adaptador en modo monitor
inyectando un frame de datos de las capturas owly (A3 `00:25:00:FF:94:73`), comprobar en un canal
`JL_*` nuevo que el frame llega al software en lugar de descartarse por BSSID.

**Riesgo.** El camino USB del puerto 1 (ACK de salida, `REG_BCN_CTRL_1` apagado para no emitir
beacons IBSS) no está probado `?` (`AWDL_RTL8814_RX_ACK.md` §4-§5); si el tipo ad-hoc del puerto 1
activase beacons o TSF update, habría que enmascararlo. Coste de quitar `CBSSID_DATA`: llegan
datos de otros BSS del canal, descartados después por A3 (paso 2).

**Boot-arg.** `rtlawdlport=1`.

## 2. Desvío RX en `validate_recv_frame`

**Qué.** Antes de la clasificación por tipo, sacar del camino normal de la radio toda trama cuyo
A3 sea el BSSID AWDL y entregarla a la capa como frame 802.11 crudo con metadatos (RSSI, tasa,
mactime, canal). Es el único punto común a action frames, datos y management unicast a la MAC de
`awdl0`, antes de los tres descartes conocidos (`AWDL_RTL8814_RX_ACK.md` §3-§4: `mgt_dispatcher`
exige A1 propia o broadcast en `Source/rtl8814au/core/rtw_mlme_ext.c:1317-1319`;
`OnAction_p2p` en `Source/rtl8814au/core/rtw_mlme_ext.c:7398`; la rama estación de
`sta2sta_data_frame` exige «sa and bssid should always be BSSID» en
`Source/rtl8814au/core/rtw_recv.c:1299-1303`).

**Dónde.**

- Radio, `Source/rtl8814au/core/rtw_recv.c` en `validate_recv_frame` (:2208): cortocircuito
  inmediatamente antes del `switch` por tipo (:2304). Comparar `GetAddr3Ptr(ptr)` con el BSSID
  programado en el paso 1 y, si coincide, consumir el frame por el camino AWDL (retorno `_FAIL`
  hacia el llamador de :4248 sin tocar `validate_recv_data_frame` :2354 ni
  `validate_recv_mgnt_frame`).
- Radio, canal de entrega: el frame llega como `union recv_frame` dentro del tasklet RX
  (`usb_recv_tasklet`, `Source/Compat/VendorOsDep.c:677`; `recvbuf2recvframe` :433). Convertirlo a
  mbuf y entregarlo por una nueva función de plataforma (propuesta: `RTL_RADIO_FN_AWDL_HOOK`,
  simétrica a `RTL_RADIO_FN_DATA_HOOK` de :41, con una struct de metadatos {rssi, rate, mactime,
  canal}) `?` (nombre y firma por decidir). Origen de cada campo en el punto de desvío:
  - **RSSI en dBm**: `pattrib->phy_info.recv_signal_power` (`s8`). `pre_recv_entry`
    (`core/rtw_recv.c:4870`) llama a `rx_query_phy_status` (:4911) antes de `rtw_recv_entry`, así que
    ya está relleno al llegar a `validate_recv_frame`. El 8814A va por
    `phydm_rx_physts_1st_type` (`hal/phydm/phydm_phystatus.c:1145`, rama `ODM_IC_11AC_SERIES` de
    :3278): en OFDM vale `(pwdb_all & 0x7f) − 110` y en CCK `phydm_get_cck_rssi(lna, vga)`. El
    informe de PHY va en todas las tramas porque el RCR de arranque lleva `RCR_APP_PHYST_RXFF`
    (`hal/rtl8814a/usb/usb_halinit.c:450`). Si `pattrib->physt` vale 0, no hay medida, y el 0 en
    `+0x2c` desactivaría el filtro de borde: mejor descartar la trama o poner −100 `?`.
  - **Tasa**: `pattrib->data_rate`, índice `DESC_RATE*` de 7 bits (`rtl8814a_rxdesc.c:55`).
  - **`mactime`**: el parser de descriptores del 8814A no lee el TSF. La macro
    `GET_RX_STATUS_DESC_TSFL_8814A` (dword 5, `include/rtl8814a_recv.h:156`) existe y habría que
    añadir la lectura. De qué puerto es ese TSF no está comprobado `?`.
  - **Canal**: `pattrib->ch` no se rellena en el 8814A; hay que tomar el canal actual de la radio.

  El manager solo usa `+0x2c` (RSSI): `+0x0c` canal, `+0x28` tasa y `+0x30` `mactime` no los lee
  (`AWDL_PEERMANAGER_DRIVER_ABI.md` §1.3). Viajan dentro de la cabecera de 72 B del evento 48, y
  si el daemon los usa no está trazado `?`.
- Capa, recepción: cola propia bajo `_awdlLock`/`_rxLock` (`RTLAirLayer.cpp:1485`, patrón de
  `rxHook`/`rxDrainEntry` :1255-1292), con hilo de drenaje propio o el mismo `_rxDrain` extendido.
  Clasificación por tipo en la capa: categoría 127 → paso 3; frame de datos con SNAP Apple → paso
  7; management unicast (ADDBA) → paso 9.

**Qué sirve.** Es la puerta de entrada de todo el RX AWDL: action frames que el manager convierte
en eventos 48 hacia `wifip2pd`, TLVs que alimentan la sincronía, datos de `awdl0` y ADDBA/DELBA/BAR.

**Prueba sin par Apple.** Replay de capturas: extraer los action frames de las capturas iOS 26 con
`awdl_af_extract.py` e inyectarlos por el nuevo gancho desde un adaptador monitor (o un peer
ESP32-S3 real, `AWDL_ESTADO_Y_BLOQUEO.md` §3); contador de frames desviados en canal `JL_*` nuevo y
`LOG("awdl: rx ...")` en la capa. Negativa: frames de datos del BSSID de la infraestructura (A3
distinto) no deben desviarse.

**Riesgo.** Sin `RCR_CBSSID_DATA` (paso 1) el desvío por A3 es el único filtro de datos; la
comparación por frame en el tasklet añade coste mínimo pero está en la ruta caliente de RX `?`.
Disciplina de cerrojos: reutilizar el patrón `rxHook` (nunca tocar Skywalk bajo el cerrojo de la
radio, comentario de `RTLAirLayer.cpp:1249-1254`).

**Boot-arg.** `rtlawdlrx=1` (exige `rtlawdlport`).

## 3. Inyección de action frames en el PeerManager

**Qué.** Entregar cada action frame AWDL recibido al `IO80211AWDLPeerManager` por la puerta
soportada `IO80211VirtualInterface::bpfTapInput(mbuf*, 12, &hdr, 72)`
(`AWDL_PEERMANAGER_DRIVER_ABI.md` §1.5: sobrecarga `0xffffff8002330e7a`, dibuja del `mngPool` del
controlador, 256 × 2048 B, que existe porque la capa no sobrescribe el slot 438).

**Dónde.**

- Capa, nuevo método de `RTLAwdInterface` (p. ej. `injectActionFrame(const uint8_t *body, size_t
  len, const rtl_awdl_rx_meta *)` en `Source/AirLayer/AWDLInterface.h`, junto a los métodos
  privados :396-409). Construye:
  - `struct apple80211_act_frm_hdr` de exactamente 72 B (más de 72 provoca el `ud1` deliberado del
    manager, `AWDL_PEERMANAGER_DRIVER_ABI.md` §1.1): MAC de origen en `+0x00` (A2 del frame),
    canal en `+0x0c` (`apple80211_channel`, 12 B), tasa en `+0x28`, **RSSI dBm con signo en
    `+0x2c`** (comparado con el umbral de borde de `ivars+10268`, −70 o −78 según el juego que
    elija `determineAndSetEdgeRssiThreshold`, `AWDL_PEER_PRESENCE_PPP.md` §5.1; a 0 pasa siempre el
    filtro, `AWDL_PEERMANAGER_DRIVER_ABI.md` §1.3), `mactime` en `+0x30`; resto a cero.
  - Un mbuf con el cuerpo desde el byte de categoría (`7F 00 17 F2 08 …`).
  - Llamada: `awdl0->bpfTapInput(m, 12, &hdr, 72)` y `mbuf_freem(m)` después (el mbuf sigue siendo
    del llamante, §1.5).
- La referencia al manager no hace falta: `bpfTapInput` lo resuelve internamente.

**Qué sirve.** Alimentación del manager: alta de pares, parseo TLV a TLV
(`AWDL_PEERMANAGER_DRIVER_ABI.md` §1.6) y, para UMIs, el evento 48 que consume `wifip2pd`
(`AWDL_ETAPAS_2_A_4.md` §2.4).

**Prueba sin par Apple.** Dos niveles, ya definidos como E3.2/E3.3 (`AWDL_ETAPAS_2_A_4.md` §2.6):
inyección sintética de un frame válido de las capturas (el manager lo parsea; si es UMI emite el
48, observable con `log show --predicate 'process == "wifip2pd"' | grep -E 'nan_event:'`) y frame
con OUI distinto (descarte con literal del kernel `BAD OUI in AF`). Con el ESP32-S3: los PSF/MIF
que emite al electedarse maestro deben producir pares en el daemon.

**Riesgo.** `hdrLen` y RSSI son los dos fallos silenciosos: cabecera de 73 B trapea el kernel y un
RSSI fingido a 0 desactiva el filtro de borde. Ambos quedan cubiertos por asserts propios antes de
la llamada `?` (diseño).

**Boot-arg.** `rtlawdlaf=1` (exige `rtlawdlrx`).

## 4. Almacén de plantillas y parámetros en la capa

**Qué.** Sustituir los `AWDL_ACK` por almacenamiento real: cada setter guarda su payload en la capa
para que los pasos 5 y 6 puedan construir TLVs y calendario. Los setters afectados, con su tipo
interno (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3) y su estado actual:

| Tipo | Slot (`AWDLInterface.h`) | Payload interno | Estado hoy |
|---:|---|---|---|
| 116 | `setAWDL_SYNC_PARAMS` :322 | 20 B `{0, aw_length, aw_period, ext_length, sync_frame_period}` (§3.4) | ACK |
| 117 | `setAWDL_SYNC_ENABLED` :323 | 12 B, `enabled` en `+8` (§3.2); único cuyo fallo revierte el manager | ACK |
| 118 | `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` :335 | 4 bytes de extensión (§4, entrada «Parámetros de extensión» de `AWDL_CHANNEL_SEQUENCE_CONTROL.md`) | ACK |
| 129 | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` :325 | 400 B: `+5` n, `+6` codificación, `+7` step, entradas de 12 B desde `+12` (§3.7) | ACK |
| 132/205 | `setAWDL_MASTER_CHANNEL` :317 / `setAWDL_SECONDARY_MASTER_CHANNEL` :330 | 8 B, canal en `+4` (§3.8) | ACK |
| 134 | `setAWDL_SYNC_STATE` :321 | 24 B: master en `+8`, op_mode en `+0x10` (§3.5) | ACK |
| 135 | `setAWDL_ELECTION_RSSI_THRESHOLDS` :318 | 20 B, umbral de borde del firmware en `+16` (−70 o −78; `AWDL_PEER_PRESENCE_PPP.md` §5.1) | ACK |
| 136 | `setAWDL_PRESENCE_MODE` :375 (stub) | `u32 ≤ 0x3F` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §4) | **stub** → pasar a ACK+store |
| 151 | `setAWDL_SYNC_FRAME_TEMPLATE` :328 | 16 B: `+4` length, `+8` puntero a plantilla de 512 B (§3.11, `AWDL_PSF_MIF_EMISSION.md` §2.1) | ACK |
| 149/225 | `setAWDL_OOB_REQUEST` :377 (stub) | 56 B: dst `+4`, option `+0x14`, self_bssid `+0x16`, `offset_us` `+0x0c` y `mi_bitmap` `+0x10` (sólo 225), `template_len` `+0x28`, `template` `+0x30` (`AWDL_PSF_MIF_EMISSION.md` §2.2-§2.3) | **stub** → pasar a ACK+store |
| 155 | `setAWDL_ELECTION_PARAMS` :327 | 36 B: op_mode `+0x10`, election_id `+0x12`, hop_count `+0x14`, root_mac `+0x16`, métricas `+0x1c/+0x20` (§3.6) | ACK |
| 229 | `setAWDL_AWDL_ADVERTISERS` :333 | 44 B del calendario del maestro: aw, dc, tx_delay, period, presence_mode, mactime, root/master MAC (§3.12) | ACK |

**Dónde.** Cuerpos en `Source/AirLayer/AWDLInterface.cpp` (bloque `AWDL_ACK` :341-365 y stubs
:340-394). Estado nuevo en `RTLAwdInterface` (`AWDLInterface.h:396-409`): copias de la plantilla PSF
(512 B) y MI (1200 B, **copiadas en el momento de la petición**: el buffer `ivars+9048` del kernel
se reutiliza para la MI secundaria del 342, `AWDL_PSF_MIF_EMISSION.md` §2.4), sync params, chan seq
(400 B), canales sociales, presencia, extensión, elección, sync state y advertisers. La lectura del
129 (`0xC02869C9`) debe contestar lo almacenado o `0xE00002C7` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md`
§7 punto 2) `?` — el slot GET es `getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` :352.

**Qué sirve.** Todos los ioctls internos de la tabla §3.1 del ABI; es la base del render de los TLVs
4/5/18 (paso 6), del calendario (paso 5) y del tirón de TX.

**Prueba sin par Apple.** Sin par: repetir la secuencia observada en hardware de
`AWDL_STAGE3_CONFIGURE_RESULTS.md` (116 → 117 → 155/129/117 de `enableAWDL`), con `LOG` de
longitudes y valores clave en cada store, y un contador accesible por `ioreg` `?` (diseño del
volcado). Coherencia: re-petir el 151 un segundo después (el manager lo re-emite en cada cambio de
estado, 27 sitios, `AWDL_PSF_MIF_EMISSION.md` §3.1) y verificar que la copia se reemplaza.

**Riesgo.** Bajo: los setters ya se invocan y se aceptan; sólo cambia el cuerpo. El 149/225 es el
único con layout nuevo para la capa (56 B, corregido en `AWDL_PSF_MIF_EMISSION.md` §9).

**Boot-arg.** `rtlawdltpl=1`.

## 5. Reloj de franjas en el host y eventos 41/43

**Qué.** Un calendario de slots en la capa (spec completa en `AWDL_WINDOW_TIMER.md`): `base_us` +
`base_aw`, reloj libre como maestro (§3.1), alineación al maestro elegido con el 229 almacenado
(§3.2) y con los TLV 4 recibidos, y emisión de los eventos 41 (apertura, sólo en franjas servidas)
y 43 (cierre, dispara la elección) por `reportDataPathEvents` (celda `+0x298`).

**Dónde.**

- Capa, clase nueva (p. ej. `Source/AirLayer/AWDLSlotClock.{h,cpp}`) con el estado de §2 del doc de
  ventanas y la tabla de la secuencia del 129 almacenada (paso 4): canal por franja de
  `Source/rtl8814au` — no; de la entrada `+12·i+4` del 129 `?` (fórmula de índice del manager:
  `min(15, (AwNumber % ((step+1)·n)) / (step+1))`, `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2).
- Emisión: capturar el manager sobrescribiendo `createPeerManager` (slot `0xB08`, declarado en
  `AWDLInterface.h:110`, hoy heredado) y guardando el resultado `?` (el enlace de la factoría
  heredada es el punto abierto 15 del índice; alternativa sin override: `getP2PSkywalkPeerMgr`,
  slot `0xCC0` de :142). Los eventos salen por `(*(pm->vptr + 0x298))(pm, 41, data, 36)` o por
  `postMessage` (slot `0xB18`, :111) — ambas vías listadas en `AWDL_ETAPAS_2_A_4.md` §2.4.
- Temporización: `thread_call` con deadline (patrón `_poll`/`_rxDrain`,
  `RTLAirLayer.cpp:1486-1487`). Franjas servidas = entradas del 129 cuyo canal coincide con el
  canal de la radio; el resto se tratan como no servidas (canal 0) `?` (radio fija sin salto,
  `AWDL_WINDOW_TIMER.md` §4; el salto de canal del 8814AU queda fuera de este plan).
- Payload del 41 (36 B, `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2): `AwNumber` u16 `+0`, estado de sync
  u32 `+4` (1 maestro / 2 esclavo, valores de `printState`), MAC del maestro `+8` (la del 134
  almacenado), MAC raíz `+0x0e` (la del 155 `+0x16` o la propia), `FwAwStartTime` u32 **big-endian**
  `+0x14`, flags `+0x18`, canal `+0x1c`, flags de ventana `+0x20` = 0. Payload del 43 (40 B) con
  `+0x10 = 0` para omitir la rama CCA (§2.5). El 42 es opcional y no se emite (§5.2 del doc de
  ventanas).
- El `sn` del TLV 4 y el `AwNumber` del 41 deben ser coherentes (`monitorTimeline` compara y sólo
  registra, `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.3; tolerancia 5120 µs).

**Qué sirve.** Eventos 41/43 (y el estado del 46 tras el 134, vía `postSyncStateChanged` `?` —
queda para después; el manager publica el 46 él mismo con el 134 aceptado `?`). Sin el 41 el
manager nunca entra en ventana (`ivars+772`) ni tira de TX (`AWDL_PEERMANAGER_DRIVER_ABI.md` §5.2).

**Prueba sin par Apple.** E3.4 (`AWDL_ETAPAS_2_A_4.md` §2.6): emitir los eventos y verificar la
ausencia de las trazas de desincronización (`AW Number discontinuity`, `AW start time inaccuracy`,
`slot 0 start too soon` — literales de §2.3 del ABI) en `log show` del kernel; cadencia medida con
marcas de la capa (una apertura por franja servida, 65,536 µs entre franjas). Con el ESP32-S3: si
sigue al Mac como maestro, el reloj libre y la TX son coherentes
(`AWDL_WINDOW_TIMER.md` §9; `AWDL_REFERENCIAS_EXTERNAS.md` §3).

**Riesgo.** Precisión de los temporizadores del host sin medir en este equipo (`?`,
`AWDL_WINDOW_TIMER.md` §7); el guardia «slot 0 start too soon» (64 ms) castiga adelantos del timer;
los defectos de concurrencia A1/A7 de la revisión de etapa 1 (`AWDL_STAGE1_CODE_REVIEW_HOOK.md`)
elevan su probabilidad con timers nuevos `?`.

**Boot-arg.** `rtlawdlev=1` (exige `rtlawdltpl`).

## 6. Emisión de PSF/MIF con los TLVs 4, 5 y 18 del driver

**Qué.** Transmitir el sync frame (PSF, subtipo 0) cada `sync_frame_period` del 116 y el master
indication (MIF, subtipo 3) una vez por availability window (y el UMI del 149 unicast), con la
cabecera 802.11, la cabecera fija de 16 B y los TLVs 4, 5 y 18 rendidos por la capa delante de la
plantilla del kernel (contrato completo en `AWDL_PSF_MIF_EMISSION.md` §4, §8).

**Dónde.**

- Capa, constructor de tramas (nuevo, p. ej. en el mismo archivo del reloj o en `AWDLInterface.cpp`):
  - Cabecera 802.11: FC `0xD0 0x00`, dur 0, A1 `FF:FF:FF:FF:FF:FF` (MAC del peer para el UMI), A2 =
    MAC de `awdl0` (`_mac`), A3 = `00:25:00:FF:94:73` (`AWDL_RTL8814_TX_RECIPE.md` §4).
  - Cabecera fija: `7F | 00 17 F2 | 08 | ver ≠ 0 | subtipo | 00 | phy_tx (u32) | target_tx (u32)`,
    ambos del reloj de µs de la capa (§6.5 del doc de emisión).
  - **TLV 4** (sync params, §5.1): periodos del 116 almacenado, `pm` del 136, ext bytes del 118,
    `mchan` del 132, maestro del 134/155, `tchan`/`dc`/`rem`/`sn` del reloj del paso 5, secuencia
    embebida = réplica del TLV 18 (73 B medidos en las capturas, §10 del doc de emisión).
  - **TLV 5** (elección v1, §5.2): render desde el 155 almacenado; el cuerpo exacto del aire queda
    abierto `?`.
  - **TLV 18** (secuencia, §5.3): copia renderizada del 129 almacenado — canal real en las ranuras
    atendidas, 0 en las demás, codificación/step/fill conservados
    (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7 punto 3).
  - Copia íntegra de la plantilla almacenada del 151 (PSF) o del 149/225 (MI; el TLV 16 ARPA ya
    viene dentro, no añadir uno propio).
- Capa → radio: nueva función de plataforma (propuesta: `RTL_RADIO_FN_AWDL_TX`, mbuf con la trama
  802.11 completa) `?` (firma por decidir).
- Radio, transmisión: réplica del patrón de inyección del vendor (`rtw_monitor_xmit_entry`,
  `Source/rtl8814au/core/rtw_xmit.c:4445`, adaptada de sk_buff a mbuf `?`): `alloc_mgtxmitframe`
  (`Source/rtl8814au/core/rtw_mlme_ext.c:7496`), copia a `buf_addr + TXDESC_OFFSET`,
  `update_mgntframe_attrib` con `update_mgntframe_attrib_addr` intacto (A2/A3 no se reescriben,
  :7691), tasa fijada con `update_mgnt_tx_rate` (:7546) — 6 Mbit/s OFDM en el canal 6 y 12 Mbps de
  referencia Broadcom en 5 GHz `?` (`AWDL_PSF_MIF_EMISSION.md` §6.4) —, `retry_ctrl = FALSE` (12,
  `Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c:259-263`; irrelevante en broadcast `?`),
  `dump_mgntframe` (`Source/rtl8814au/core/rtw_mlme_ext.c:7734`). No usar
  `update_monitor_frame_attrib` sin corregir: fija `MGN_VHT1SS_MCS9` a 20 MHz, tasa inválida
  (`:7592`; `AWDL_RTL8814_TX_RECIPE.md` §3.1).

**Qué sirve.** Los tipos 151 (repetición contratada por el 116), 149/225 (MIF por AW, UMI) y la
mitad TX del contrato del manager (`AWDL_PSF_MIF_EMISSION.md` §6). Sin evento de TX por trama: el
kernel no lo espera (§7 del doc de emisión).

**Prueba sin par Apple.** Segundo adaptador en modo monitor capturando el canal social: verificar
el orden en el aire contra las capturas (PSF `4,5,18,6,24,12,…`, MIF `4,5,18,24,12,…,16,21,…`,
`AWDL_PSF_MIF_EMISSION.md` §4) con `awdl_pcap.py`. Con el ESP32-S3: el ESP32 sólo emite tras elegir
un maestro; si se sincroniza con los PSF recibidos de la capa, la TX es válida
(`AWDL_ESTADO_Y_BLOQUEO.md` §3). El `AwNumber`/`sn` anunciados deben cuadrar con los eventos 41 del
paso 5.

**Riesgo.** Latencia entre encolado y salida USB sin medir `?` (`AWDL_RTL8814_TX_RECIPE.md` §5);
la ventana la marca el temporizador del paso 5 y el encolado debe respetar las guardas de TX
(unicast a > 3 TU de los bordes, `AWDL_WINDOW_TIMER.md` §6). El PMF del port no toca estos frames
(A1 broadcast y categoría 127, `AWDL_RTL8814_TX_RECIPE.md` §2).

**Boot-arg.** `rtlawdlpsf=1` (exige `rtlawdlev`).

## 7. Datos de `awdl0`: TX (Ethernet II → 802.11 + SNAP) y RX (strip de 16 bytes)

**Qué.** Mover el tráfico real de `awdl0`: en TX, convertir el frame Ethernet II que entrega la pila
en trama 802.11 de datos con el SNAP AWDL y secuencia por destino; en RX, hacer la conversión
inversa y entregar Ethernet II en la `RxCompletionQueue` de `awdl0` (`AWDL_DATA_ENCAP.md` §1 y §7).

**Dónde, TX.**

- La cola de envío no admite inyección del driver: el patrón con soporte es la llamada directa a la
  acción con paquetes del pool (`AWDL_STAGE2B_MBUF_TO_PACKET.md` §1 y §4). El frame del stack llega
  al handler `output` del ifnet, que la capa instala:
  - Override de `prepareBSDInterface` (slot `0x8E8`, declarado en `AWDLInterface.h:104`, hoy
    heredado) y escritura del handler en `params+0x40`, o bien el slot `0x8E0`
    (`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`; decisión entre ambos `?`).
  - Nuevo `rtlAwdOutput(__ifnet *, __mbuf *)` con la secuencia de §5 del doc 2b:
    `IOSkywalkPacketBufferPool::allocatePacket` (símbolo `0xFFFFFF8002B058EE`) → `prepare` → copia
    del mbuf a `base + 64` → `setDataOffsetAndLength(64, len)` → `mbuf_freem` → llamada directa a la
    acción (la firma de `txNoop`, `AWDLInterface.cpp:257`). Retorno errno: 0 = aceptado.
- La acción real (sustituta de `txNoop`): por paquete, leer DA/SA/ethertype del frame Ethernet II,
  construir `802.11 (A1=DA, A2=SA, A3=BSSID AWDL) + AA AA 03 00 17 F2 08 00 | 03 04 | seq | 00 00 |
  ethertype | payload` (`AWDL_DATA_ENCAP.md` §7), con `seq = (contador & 0xFFF) | (AC << 12)` y un
  contador u16 **por destino** (el nibble alto es el diseño Broadcom; replicarlo es lo conservador
  `?`), sin el campo de longitud 802.3 del formato interno. Completar por la cola de completado TX
  (mecánica de `txNoop`, tandas de 64).
- Radio: misma vía del paso 6 pero con atributos de datos: tasa legacy (24 Mbit/s o HT MCS7 a
  20 MHz `?`; nunca VHT MCS9 a 20 MHz), `retry_ctrl = TRUE` (6 reintentos) para unicast
  (`AWDL_RTL8814_TX_RECIPE.md` §3.1-§3.2), bit `BMC` para multicast (`rtl8814au_xmit.c:88-89`).

**Dónde, RX.**

- La capa recibe del paso 2 los frames de datos crudos; validar frame de datos, protocolo 0,
  cabecera 24/26 según el bit QoS, DA = addr1, SA = addr2 (`AWDL_DATA_ENCAP.md` §5.2); comprobar
  `AA AA 03` en `+14` y el dword `+17` contra `00 17 F2 08`; si es AWDL, quitar 16 bytes y recomponer
  el ethertype en `+12` (`AWDL_DATA_ENCAP.md` §5.3). OUI distinto de 00 → descartar; OUI 00 o sin
  SNAP → camino de `llw0`, fuera de este plan (etapa 4, `AWDL_ETAPAS_2_A_4.md` §3).
- Entrega: paquete del pool RX con `rtl_pkt_prepare`/copia/`setDataOffsetAndLength` y
  `rtl_rxcomp_enqueue` por tandas — el patrón ya en producción en `rxFrames`
  (`Source/AirLayer/RTLAirLayer.cpp:445-482`), con `setLinkHeaderLength(14)`.
- Antes de encolar cada paquete, llamar a `inputPacket` (slot `+0x0B50`, declarado en
  `AWDLInterface.h:114`, cuerpo de la familia). Retorno 0: descartar. `consumed = 1`: el paquete
  queda en la cola de espera PPP del par y no se toca. Otro valor: encolar. La familia no filtra
  nada de lo que se encole sin esta llamada (`AWDL_PEER_PRESENCE_PPP.md` §3-§4).
- Overrides nuevos de `sendPendingPacketsToStack` (`+0xF30`) y `freePendingPackets` (`+0xF38`),
  declarados en `AWDLInterface.h:178-180`. Los de la familia solo registran «Not Implemented». El
  primero recibe los paquetes retenidos cuando `wifip2pd` manda el ioctl 415 y los encola en la
  `RxCompletionQueue`. El segundo recibe los que superan 2 s en espera o los de un par eliminado y
  los devuelve al pool. Sin ellos ningún paquete de un par nuevo llega a la pila.

**Qué sirve.** El datapath de datos completo de `awdl0` (etapa 2 de `AWDL_ETAPAS_2_A_4.md` §1); en
RX alimenta la `RxCompletionQueue` registrada y en TX sirve tanto la llamada directa del `output`
como el tirón del manager si llega a usarse.

**Prueba sin par Apple.** E2.1-E2.5 (`AWDL_ETAPAS_2_A_4.md` §1.6): `ifconfig awdl0 up` + tráfico
IPv6/ND sin pánico; `tcpdump -i awdl0` para el RX; TX verificable en modo monitor (la trama sale
con SNAP y secuencia creciente por destino, contraste con `awdl_pcap.py`); transferencia real con
el ESP32-S3 (ESP32Drop valida primero la TX, `AWDL_ESTADO_Y_BLOQUEO.md` §3).

**Riesgo.** El handler corre en el hilo de la pila: si la radio exige el work loop, saltar con
`IOWorkLoop::runAction` reteniendo el paquete (`?` decisión, `AWDL_STAGE2B_MBUF_TO_PACKET.md` §5
paso 7); offset de conversión del kernel no trazado (64 elegido por la capa, §9.3); el headroom de
64 del `RegistrationInfo` (`AWDLInterface.cpp:198`) es el que deja sitio para anteponer 16 bytes.
Reserva del headroom en la ruta RX de la radio sin precedente `?`
(`AWDL_ETAPAS_2_A_4.md` §1.5).

**Boot-arg.** `rtlawdldata=1` (exige `rtlawdlrx` y `rtlawdlport`).

## 8. Multicast de `awdl0`

**Qué.** Que los multicast IP (mDNS, `33:33:…`, `01:00:5E:…`) salgan en las ventanas y entren por
RX. En Broadcom el stash y el temporizador son de la familia y exigen tres overrides de interfaz
que hoy quedan en la base (`AWDL_DATA_ENCAP.md` §6): `p2pMulticastTx(IO80211NetworkPacket**, uint)`
(slot `+0xF18`, base `0xffffff8002334d6a` no hace nada), `freeMulticastPacket` (`+0xF08`, debe
devolver al pool) y `triggerAWDLMulticastTx` (`+0xF10`, opcional).

**Dónde.**

- Capa: overrides de `p2pMulticastTx`/`freeMulticastPacket` (declarados en
  `AWDLInterface.h:173-179`) que alimenten la acción TX directamente (patrón `p2pMulticastTx` de
  Broadcom, `0xffffff8001541d64` → `dequeuePackets`, `AWDL_STAGE2B_MBUF_TO_PACKET.md` §4) y
  devuelvan los paquetes al pool. El stash (`stashMulticastPacketPair`, `handleMulticastTimer`) y
  los marcajes 0x80 funcionan sin cooperación del driver mientras los overrides distingan los
  paquetes re-alimentados `?` (convención de marca propia, §6 del doc de encapsulación).
- Alternativa sin stash (más simple, sin firmware que extienda ventanas): transmitir el multicast
  directamente en las franjas servidas con las guardas de multicast (16 TU en las franjas 0 y 10, o
  3 TU en cualquier franja servida sin salto de canal, `AWDL_WINDOW_TIMER.md` §6) `?` (elección de
  diseño; el doc la admite, `AWDL_DATA_ENCAP.md` §7 punto 5).
- RX: los multicast A1 llegan por el desvío del paso 2 y se entregan como datos del paso 7; los DA
  especiales `33:33:00:00:…`, `33:33:80:00:00:FB` y `01:00:5E:00:…` los filtra `inputPacket`, que
  la capa llama en el paso 7 (`AWDL_PEER_PRESENCE_PPP.md` §3).

**Qué sirve.** TX multicast de la cola de envío (rama del bit 0 de DA) y RX multicast; ninguno
condiciona un ioctl concreto — es parte del datapath.

**Prueba sin par Apple.** `ping6 -m`/mDNS entre `awdl0` y el ESP32-S3 `?` (soporte multicast en el
peer ESP32 sin verificar); contador de paquetes stashados/entregados en `LOG`; en monitor, la trama
multicast sale dentro de la franja servida y con el bit `BMC` del descriptor `?` (verificación del
bit requiere instrumentación del descriptor).

**Riesgo.** Arrays de stash limitados a 16 entradas por lado (los sobrantes se liberan con
`freeMulticastPacket`): un override que no libere agota el pool. Los dos overrides base son
«Function not implemented» — sin ellos el multicast de `awdl0` no sale nunca.

**Boot-arg.** `rtlawdlmc=1` (exige `rtlawdldata`).

## 9. Block Ack: ADDBA, DELBA/BAR y reordenado

**Qué.** La familia no hace Block Ack (handlers `BLOCK_ACK` muertos, `AWDL_BLOCK_ACK.md` §2) ni
reordena (§5): la capa responde la ADDBA del par por sí misma, procesa DELBA y BAR, y entrega en
orden a la `RxCompletionQueue` con un buffer de reordenado por (par, TID). winawdl midió
971 kB/s → 1,7 MB/s al aceptar la ADDBA (TID 0, ventana 64).

**Dónde.**

- Intercepción: los frames de categoría 3 (`RTW_WLAN_CATEGORY_BACK`,
  `Source/rtl8814au/include/ieee80211.h:1511`) llegan por el desvío del paso 2; la capa los
  clasifica antes del camino de datos. Precedente de parseo en la radio: `OnAction_back`
  (`Source/rtl8814au/core/rtw_mlme_ext.c:3677`) con `process_addba_req` (:3747) y los tres logs
  `JL_BA` ya presentes (:3734-3740).
- Capa, respuesta ADDBA: construir la trama de respuesta (categoría 3, acción 1) aceptando TID 0 y
  ventana 64, y transmitirla por la vía del paso 6 (unicast al par, mgnt) `?` (cuerpo de la
  respuesta por componer a partir del request del par y de `AWDL_REFERENCIAS_EXTERNAS.md` §7).
- Capa, DELBA (acción 2) y BAR (control 0x94): cerrar/avanzar la sesión por (par, TID); emitir el
  Block Ack de respuesta `?`. Si el MAC del 8814AU puede sostener la sesión en hardware, dejarlo al
  hardware `?` (`AWDL_RTL8814_RX_ACK.md` §5, CAM/macid del par sin verificar).
- Reordenado: buffer por (par, TID), ventana 64, expiración, entrega en orden; modelo del árbol del
  driver: `struct recv_reorder_ctrl` (`Source/rtl8814au/include/rtw_recv.h:83`) y
  `recv_indicatepkts_in_order` (`Source/rtl8814au/core/rtw_recv.c:3266`, con
  `check_indicate_seq` :3146 y `enqueue_reorder_recvframe` :3201) — código Linux a portar `?`.
  El reordenado se intercala antes de la entrega del paso 7.
- `setPEER_CACHE_CONTROL` (slot `0x1108`, stub `AWDLInterface.h:376`): seguir devolviendo 0; uso
  opcional del opcode (`+4`) y del flag ampdu (`+44`, siempre 1 en la familia, `AWDL_BLOCK_ACK.md`
  §4) como interruptor por par de la sesión BA `?`.

**Qué sirve.** ADDBA/DELBA/BAR entrantes del par (ningún ioctl ni evento los transporta: viajan en
action frames); sin A-MPDU en TX hasta tener acuerdo (`AWDL_BLOCK_ACK.md` §6-§7).

**Prueba sin par Apple.** Replay: las capturas contienen 56 ADDBA; extraerlas con el extractor e
inyectarlas (paso 3) para ver la respuesta en modo monitor. Con el ESP32-S3: si el peer emite ADDBA
`?` (soporte sin verificar) y el throughput con/without BA se mide con iperf sobre `awdl0` `?`.
Sin BA, el camino de datos del paso 7 debe seguir funcionando (los frames no agregados llegan
igual).

**Riesgo.** Es el paso con más piezas sin cerrar: hardware BA del 8814AU `?`, cuerpo exacto de la
respuesta `?`, port del reordenador. Último del orden precisamente porque todo lo anterior es
verificable sin él; sin BA el par retransmite hasta su límite (coste de tasa, no de corrección).

**Boot-arg.** `rtlawdlba=1` (exige `rtlawdldata`).

## 10. Grafo de dependencias

```
P1 puerto 1 + RCR ──► P2 desvío RX ──► P3 inyección AF ──┐
        ▲                        │                       │
        │                        │                       ▼
P4 almacén de plantillas ────────┼──► P5 reloj + eventos 41/43 ──► P6 PSF/MIF TX
                                 │                                    │
                                 │                                    ▼
                                 └──────────────► P7 datos TX/RX ◄────┘ (vía radio)
                                                          │
                                                          ▼
                                                    P8 multicast
                                                          │
                                                          ▼
                                                    P9 Block Ack
```

- P1 depende de una fracción de P4 (guardar el 125 y la MAC); su núcleo es de la radio y se puede
  construir primero con la MAC/BSSID fijados por el boot-arg `?`.
- P3 y P7 consumen el desvío de P2; P6 y P7 consumen el almacén de P4 y el reloj de P5; P9 consumen
  P2 (intercepción), P6 (vía TX de mgnt) y P7 (entrega en orden).
- `AWDL_ETAPAS_2_A_4.md` §4 sitúa lo mismo en etapas: P1-P2 = prerrequisitos de radio de la etapa 2;
  P4-P6 = etapa 3 (sincronía y action frames); P7-P8 = etapa 2 (datapath); P9 = cierre de la nota de
  Block Ack. El orden de este plan prioriza desbloquear RX antes que TX porque RX es la prueba de
  menor riesgo con par disponible (`AWDL_ESTADO_Y_BLOQUEO.md` §4).

## 11. Orden de pruebas en hardware (sin par Apple)

1. **Registros** (P1): volcado vivo de `REG_CR`/`REG_MACID1`/`REG_BSSID1`/`REG_RCR` (patrón
   `Docs/regdumps/`).
2. **RX sintética** (P2-P3): replay de los 4893 action frames de las capturas con el extractor
   AWDLLab; pares visibles en `wifip2pd` (`nan_event:`) y descarte `BAD OUI in AF` en la negativa.
3. **Plantillas** (P4): log de stores en la secuencia de habilitación ya observada
   (`AWDL_STAGE3_CONFIGURE_RESULTS.md`).
4. **Eventos** (P5): cadencia del 41/43 sin trazas de `monitorTimeline`; con el ESP32-S3 cerca,
   coherencia de maestro.
5. **TX de PSF/MIF** (P6): monitor en el canal social + contraste de orden de TLVs con
   `awdl_pcap.py`; el ESP32-S3 se sincroniza con los PSF de la capa.
6. **Datos** (P7): ND/IPv6 local sin pánico, `tcpdump -i awdl0`, transferencia con el ESP32-S3.
7. **Multicast** (P8): mDNS/ping6 multicast en `awdl0`.
8. **BA** (P9): replay de las 56 ADDBA de las capturas y medición de throughput con el ESP32-S3 `?`.

Estado de partida de estas pruebas: ninguna ejecutada; con par Apple siguen pendientes además las
de `AWDL_ESTADO_Y_BLOQUEO.md` §4, que este plan no sustituye.

## 12. Interrogantes propios del plan (`?`)

| # | Punto | Cierre |
|---:|---|---|
| 1 | Firma y nombre de las nuevas funciones de plataforma (`AWDL_PORT`, `AWDL_HOOK`, `AWDL_TX`) | Diseño de `RTLRadioABI.h` antes de P1/P2/P6 |
| 2 | Vía de programación del puerto 1 (registros directos frente a llamadas con `hw_port = HW_PORT1`) y efecto del tipo ad-hoc sobre beacons/TSF | Prueba de P1; `AWDL_RTL8814_RX_ACK.md` §5 |
| 3 | ~~Campo exacto de RSSI/tasa/mactime que la radio entrega con el frame crudo~~ | Cerrado en el paso 2: `recv_signal_power`, `data_rate`, TSFL del descriptor sin leer aún; queda abierto el valor para tramas sin informe de PHY |
| 4 | Captura del puntero del manager (`createPeerManager` heredado frente a `getP2PSkywalkPeerMgr`) | P5; punto 15 de `AWDL_INDICE.md` §8.2 |
| 5 | Cuerpo exacto del TLV 5 y su mapeo con el 155 | P6; `AWDL_PSF_MIF_EMISSION.md` §10 |
| 6 | Tasa de PSF/MIF (12 Mbps de referencia frente a 6 Mbit/s OFDM en 2,4 GHz) y de datos (legacy 24 frente a HT MCS7) | Medición en P6/P7 |
| 7 | Instalación del `output` propio (slot `0x8E0` frente a `0x8E8`) | P7; `AWDL_STAGE1_OUTPUT_HANDLER_LINK.md` |
| 8 | Contexto de hilo de la acción TX (pila frente a work loop) | P7; `AWDL_STAGE2B_MBUF_TO_PACKET.md` §9.5 |
| 9 | Sesión BA en hardware del 8814AU y cuerpo de la ADDBA response | P9; `AWDL_BLOCK_ACK.md` §8 |
| 10 | Soporte multicast del peer ESP32-S3 | Prueba de P8 |
