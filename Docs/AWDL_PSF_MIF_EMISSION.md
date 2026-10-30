# Emisión de PSF y MIF: contrato para una capa driver sin firmware AWDL

Este documento fija, función por función y byte por byte, qué peticiones entrega `IO80211AWDLPeerManager`
a un driver para que éste transmita los action frames periódicos de AWDL (el sync frame, **PSF**, y el
master indication frame, **MIF**), dónde debe insertar el driver los TLVs 4 (sync params), 5 (elección
v1) y 18 (secuencia de canales) que el kernel no construye, de dónde sale cada campo de esos TLVs, con
qué cadencia y cabecera 802.11 se transmiten, y qué eventos debe devolver el driver después de
transmitir. Cierra con la lista ordenada de lo que la capa del RTL8814AU debe construir por trama.
Complementa `AWDL_PEERMANAGER_DRIVER_ABI.md` (§3.11 ya describía la plantilla del 151), 
`AWDL_FIRMWARE_DUTIES.md` (lado Broadcom: iovars `awdl_payload`, `awdl_oob_af`, `awdl_oob_af_auto`),
`AWDL_FRAME_WIRE_FORMAT.md`, `AWDL_TLV_BODIES.md` y `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.2.

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, 25G241, Darwin 25.6.0, md5
`f489ff8be80e6013a1bf47561dcaef45`), copia de la IDB anotada `~/re_cache/ida/boot_awdl.kc.i64` en
`Build/ida_awdl/task_psf_mif/`, descompilación y desensamblado de las funciones citadas (salidas
`s1_out.txt`–`s5_out.txt` de esa carpeta). Direcciones = VA del kernelcache, leídas en esa IDB. Lo no
verificado se marca `?`.

## 1. Resumen

El kernel sostiene **dos plantillas** y las entrega al driver por caminos distintos:

| Plantilla | Constructor | Peticiones de entrega | Subtipo en el aire |
|---|---|---|---|
| Sync frame (PSF) | `updateSyncFrameTemplate` `0xffffff80021dc5b2` | ioctl interno **151** | `0` |
| Master indication (MI) | `buildMasterIndicationTemplate` `0xffffff8002211b42` | ioctl interno **149** (unicast = UMI; broadcast = MIF inmediato) e ioctl interno **225** (MIF permanente por AW); **342** para la MI secundaria (SDB) | `3` |

Ninguna de las dos plantillas contiene el TLV 4 (sync params), el TLV 5 (elección v1) ni el TLV 18
(secuencia de canales): no hay llamada a ningún constructor de esos tipos en ninguna de las dos
funciones (§3), y no existe símbolo de constructor para ellos en el fileset. El kernel tampoco monta
la cabecera de 802.11 ni la cabecera fija de 16 B: la plantilla que viaja en las peticiones empieza
directamente en el primer TLV. Todo eso es trabajo del driver/firmware, que en el aire produce el
orden medido en las capturas de iOS 26 (`AWDL_REFERENCIAS_EXTERNAS.md` §4): PSF `4,5,18,6,24,12,7,17,21`
y MIF `4,5,18,24,12,7,17,16,21,6,2,…` — exactamente los TLVs del driver delante de los TLVs del
kernel, en el orden en que cada constructor los apila.

El TLV 16 (ARPA) **sí lo construye el kernel** dentro de `buildMasterIndicationTemplate` (§3.2): el
driver no debe añadirlo por su cuenta.

## 2. Peticiones que entregan plantillas al driver

### 2.1 Tipo 151 — plantilla del sync frame (PSF)

Único emisor en todo el fileset: `updateSyncFrameTemplate` (el barrido del qword `0x97802869C8` sobre
el fileset de IO80211Family, segmento `0xffffff8002183600`–`0xffffff800239f200`, da un solo hit,
`0xffffff80021dc97b`; la petición se forma con `mov rax, 97802869C8h` en `0xffffff80021dc97b` y
`call IO80211PeerManager::ioctl_internal` `0xffffff80022524e8` en `0xffffff80021dc996`).

```c
struct awdl_sync_frame_template_req {  // 16 B
    uint32_t reserved;                 // +0x00 = 0
    uint32_t length;                   // +0x04  bytes válidos de la plantilla
    uint64_t template;                 // +0x08  puntero al buffer de 512 B
};
```

El buffer se compone en pila (512 B, `secure_memset(..., 0xAA, 0x200)`, `mov esi, 0AAh` en
`0xffffff80021dc5f1` y la llamada en `0xffffff80021dc5f6`), se emite sólo si `buildServiceParamTlv`
devolvió `< 0x200` y `length` es la suma de los TLVs apilados. El parámetro `bool` del símbolo
(`updateSyncFrameTemplate(bool)`) no participa en el cuerpo `?`. El setter de userspace para el tipo
151, `setAwdlSyncFrameTemplate` (`0xffffff800221d254`), es un stub que devuelve 0: el tipo 151 sólo
tiene sentido manager→driver.

El driver debe copiar exactamente `length` bytes: lo que sigue en el buffer del kernel es relleno
`0xAA`. La plantilla se re-emite en cada cambio de estado que afecte a sus TLVs (27 sitios de llamada
de `updateSyncFrameTemplate` listados en §3.1) y el driver debe reemplazar su copia en cada 151.

### 2.2 Tipo 149 — MI unicast (UMI) y MIF inmediato

Único emisor del qword `0x95802869C8` en el fileset (hit `0xffffff8002213f22`):
`IO80211AWDLPeerManager::sendUnicastMI(peer, options, options2, u8 *bssSteeringData)`
(`0xffffff8002213c0a`). Llamada ensamblada en `0xffffff8002213f22`/`0xffffff8002213f32`. Su único
llamador es `handleUmiTimer` (`0xffffff80021c3254`, xref en `0xffffff80021c3562`).

Payload de 56 B (reconstruido de la descompilación de `0xffffff8002213c0a`; corrige el layout de
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.10, ver §9):

```c
struct awdl_mi_request {          // 56 B, compatible con apple80211_awdl_oob_request
    uint32_t reserved0;           // +0x00 = 0
    uint8_t  dst[6];              // +0x04  MAC del peer (peer+88..93)
    uint16_t pad0;                // +0x0a
    uint32_t offset_us;           // +0x0c  = 0 (sólo el 225 lo usa)
    uint32_t mi_bitmap;           // +0x10  = 0 (sólo el 225 lo usa)
    uint16_t option;              // +0x14  6 = normal, 14 = UMIOnDwell
    uint8_t  self_bssid[6];       // +0x16  ivars+37204..37209
    uint32_t reserved1;           // +0x1c = 0
    uint32_t reserved2;           // +0x20 = 0
    uint16_t template_len;        // +0x28  ivars+10248
    uint8_t  pad1[6];             // +0x2a
    uint64_t template;            // +0x30  ivars+9048 (1200 B)
};
```

Antes de emitir, `sendUnicastMI` reconstruye la plantilla MI con
`buildMasterIndicationTemplate(this, ivars+9048, 1200, 1, options, options2, …, bssSteeringData, peer)`
(llamada en `0xffffff8002213cec`) y guarda la longitud en `ivars+10248`. Precondiciones: AWDL
conectado (`+16096` bit 1), dentro de ventana (`+772` bit 0) y ≥ 10 ms desde el último UMI al mismo
peer (`peer_ivars+1632`; `getUMIInterval` `0xffffff8002213bfe` devuelve el literal 10). La opción 14
(UMIOnDwell) sólo si el dwell PSF está activo (`+16943`), la capacidad `PSF_DWELL_UMI` (`+16579`) está
declarada, el peer no se descubrió en 5 GHz y el último dwell fue hace ≤ 109 ms.

**La misma petición con `dst = FF:FF:FF:FF:FF:FF` es el MIF inmediato**: `updatePrimaryPayloadMI`
(§2.3) reutiliza este tipo para el disparo "send now" del temporizador MI del kernel.

### 2.3 Tipo 225 — plantilla MI permanente (MIF por AW) y 149 "send now"

`IO80211AWDLPeerManager::updatePrimaryPayloadMI(reason, sendNow, …)` (`0xffffff80021fb36a`) es el
segundo consumidor de `buildMasterIndicationTemplate` (xref `0xffffff80021fb3cf`) y el único emisor
del tipo 225. Reconstruye la plantilla MI en `ivars+9048` (1200 B) salvo que `reason == 17` y
`ivars+10248 != 0`, y emite un ioctl cuya mitad baja es `0x802869C8` (`mov dword ptr [rsi],
802869C8h` en `0xffffff80021fb60e`, llamada a `ioctl_internal` en `0xffffff80021fb617`) y cuyo tipo
va en la mitad alta escrita por separado:

- **149** ("send now"): `mov dword ptr [rbp+var_80+4], 95h` en `0xffffff80021fb533` — se toma cuando
  `reason == 17` o `sendNow != 0`.
- **225** ("template to fw"): `mov dword ptr [rbp+var_80+4], 0E1h` en `0xffffff80021fb546` — se toma
  cuando `reason != 17` y `sendNow == 0`.

Payload: la misma estructura de 56 B de §2.2 con `dst = FF:FF:FF:FF:FF:FF` (`v38 =
0xFFFFFFFF00000000`, `v39 = 0xFFFF` en la descompilación) y, sólo en el camino del 225, dos campos
adicionales:

- `+0x0c uint32_t offset_us` — desfase de transmisión dentro de la ventana, en µs, calculado con
  `read_random`: si el bit `+64058` está activo, `100·(rnd & 0xF)`; si el bit `+65193` está activo,
  `100·(rnd & 0x3F) + 5000`; si no, `100·(rnd8 + 10·(+744)) + 5000`. Traza
  `"Sending MI template size %u; miBitmap 0x%X at offset %uus"`.
- `+0x10 uint32_t mi_bitmap` — `ivars+42760` (bitmap primario de MI; `setMiBitmap`
  `0xffffff80021fb2f8` lo reescribe y fuerza un envío inmediato llamando a `updatePrimaryPayloadMI`
  con `reason = 17`, es decir, por el 149).

El error lo registra como `"ERROR: could not send Master Indication frame %s: %s"` con `%s` =
`"template to fw"` (225) o `"now"` (149). En el lado Broadcom el tipo 225 es el iovar
`awdl_oob_af_auto` — «petición permanente en vez de una transmisión», repetida por el firmware en
cada AW (`AWDL_FIRMWARE_DUTIES.md` §3.3) — y el tipo 149 es `awdl_oob_af`.

Semántica resultante para el driver: **149 = transmitir ahora** (unicast → UMI al peer; broadcast →
MIF en la ventana actual); **225 = almacenar plantilla + bitmap + desfase y repetir el MIF en cada
availability window**, con el desfase indicado, hasta que llegue una petición nueva.

### 2.4 Tipo 342 — MI secundaria (SDB)

`updateSecondaryPayloadMI` (`0xffffff80022141ae`) reconstruye la MI con
`buildSecondaryMasterIndicationTemplate` **sobre el mismo buffer `ivars+9048`** (1200 B) y emite el
qword `0x156802869C8` (hit único `0xffffff8002214304`) con un payload de 16 B:

```c
struct awdl_sec_mi_request {      // 16 B
    uint16_t pad;                 // +0x00
    uint16_t template_len;        // +0x02  ivars+10248
    uint32_t primary_bitmap;      // +0x04  ivars+42768
    uint64_t template;            // +0x08  ivars+9048
};
```

Llamado por `updateBroadcastMI` cuando la capacidad `SEC_PAYLOAD` (`+16576`) está declarada, el byte
`ctrl+1674` bit 0 está a 0 y `sendNow == 0`. Como el buffer es compartido, una MI secundaria destruye
los bytes de la primaria en el kernel: el driver debe copiar la plantilla en el momento de la
petición, no retener el puntero.

### 2.5 Tabla de emisores (barrido exhaustivo)

Barrido de los qwords de `ioctl_internal` sobre todo el segmento de texto del fileset
(`0xffffff8002183600`–`0xffffff800239f200`, 2 210 816 B):

| Tipo | Qword | Hits | Emisor |
|---:|---|---|---|
| 149 | `0x95802869C8` | 1 | `sendUnicastMI` (`0xffffff8002213f22`); el 149 de `updatePrimaryPayloadMI` se forma con dos almacenes de 32 bits (`0xffffff80021fb533` + `0xffffff80021fb60e`) y no aparece como qword |
| 151 | `0x97802869C8` | 1 | `updateSyncFrameTemplate` (`0xffffff80021dc97b`) |
| 225 | `0xE1802869C8` | — | `updatePrimaryPayloadMI` (`0xffffff80021fb546`), ídem |
| 342 | `0x156802869C8` | 1 | `updateSecondaryPayloadMI` (`0xffffff8002214304`) |
| 418 | `0x1A2802869C8` | 1 | `setPSFDwell` (`0xffffff80021f4f79`) |
| 116 | `0x74802869C8` | 2 | `setSyncFramePeriod` (`0xffffff80021ed0e5`) e `interfaceStateChange` (`0xffffff80021da0df`) |
| 132 | `0x84802869C8` | 2 | `configure` (`0xffffff80021c6010`) y `programMasterChannels` (`0xffffff80021d4a11`) |
| 129 | `0x81802869C8` | 1 | `setChannelSequenceUnconditional` (`0xffffff80021cc0b8`) |

Además del 116 de `setSyncFramePeriod`, `interfaceStateChange` re-emite los sync params por su cuenta
(`mov rax, 74802869C8h` en `0xffffff80021da0df`, con la traza `"Configuring the Sync Params"`).

## 3. Composición de las plantillas en el kernel

### 3.1 Plantilla PSF — `updateSyncFrameTemplate` (`0xffffff80021dc5b2`)

Orden verificado de apilado (cada TLV `{u8 type, u16 len, cuerpo}`; VAs de las llamadas leídas en el
desensamblado):

| # | TLV | Constructor | Llamada | Condición |
|---:|---:|---|---|---|
| 1 | 6 service params | `buildServiceParamTlv` | `0xffffff80021dc613` | siempre (gate de emisión) |
| 2 | 24 elección v2 | `buildAWDLElectionParamsVer2` | `0xffffff80021dc675` | siempre |
| 3 | 12 datapath state | `buildAwdlDataPathStateTlv` | `0xffffff80021dc6a1` | siempre (variante no-SDB, `false, 0, v8, false`) |
| 4 | 7 enhanced data rate | `buildEnhDataRateTlv` | `0xffffff80021dc6e5` | `1 ≤ ctrl+1564 ≤ 0x19` (longitud HT) |
| 5 | 30 ranging | `buildRangingTlv` | `0xffffff80021dc7e7` | `ivars+42545` bit 0 |
| 6 | — RTG | `buildRealTimeGroupTlv` | `0xffffff80021dc821` | `ivars+64418` o `ivars+65477` bit 0 |
| 7 | 17 contenedor 802.11 | `buildIEEE80211CntnrTlv` | `0xffffff80021dc863` | `ctrl+1568` bit 0; 14 B de `ctrl+2401` |
| 8 | 21 versión | `buildAWDLVersionTlv` | `0xffffff80021dc89d` | siempre |
| 9 | 20 sync tree | `buildSyncTreeTlv` | `0xffffff80021dc8ce` | `ivars+10308` bit 0 |
| 10 | 23 NAN sync | `buildNanSyncTlv` | `0xffffff80021dc8f2` | siempre llamada; devuelve 0 sin NAN (§3.4) |
| 11 | 33 pref chan | `buildPrefChanTlv` | `0xffffff80021dc911` | siempre llamada; puede devolver 0 (§3.4) |

**No hay ningún constructor del TLV 4, del TLV 5 ni del TLV 18**, y no se escribe la cabecera fija de
16 B: el primer byte del buffer de 512 B es el `type` del TLV 6.

Los 27 sitios de llamada de `updateSyncFrameTemplate` (xrefs verificadas): `disableAWDL`,
`interfaceStateChange` (`0xffffff80021da05e`), `dfsProxyStateMachine` ×3, `forceSdbModeONForRealTimeGroup`,
`enableAWDL` (`0xffffff80021dd375`), `reportDataPathEvents` (`0xffffff80021e0a60`), `dynSdbStateMachine`,
`runElection` (`0xffffff80021e44b1`), `setElectionParamsToNANPeer`, `monitorNearbyLogTrigger`,
`enterQuietMode`, `exitQuietMode`, `updateActionFrameReasonRTGState`, `enterAssistedDiscoveryFollowMode`,
`dynSdbForceSDBModeOn`, `updateLocalRequests` ×2, `updateRangingState`, `setAwdlNearbyLogTrigger`,
`setAwdlSyncEnabled` (`0xffffff800221b933`), `setAwdlSyncState` (`0xffffff800221e4d8`), `disableSdbMode`,
`limitedSdbForceSDBModeOn` y los gestores de ranging (`IO80211RangingManager`/`IO80211RangingManagerExt`,
9 sitios). Es decir: cada cambio de maestro, de estrategia, de SDB, de ranging o de servicios
re-genera la plantilla y baja un 151 nuevo.

### 3.2 Plantilla MI — `buildMasterIndicationTemplate` (`0xffffff8002211b42`)

Firma (del símbolo): `(u8 *buf, u32 len, bool ensemble, u32 options, bool addProMode, bool
addAirDrop, IO80211AWDLPeerManager *?, IO80211AWDLPeer *peer)`; exige `buf != null` y `len ≥ 0x101`.
El camino primario (MI de MIF/UMI) entra con `ensemble = 0` desde `updatePrimaryPayloadMI`
(`ivars+9048`, 1200 B, resto de argumentos a 0) y desde `sendUnicastMI` (con `ensemble = 1`,
`options`, `options2`, datos de bss steering y el peer).

Orden verificado en el camino `ensemble = 0`:

| # | TLV | Constructor | Llamada | Condición |
|---:|---:|---|---|---|
| 1 | 24 elección v2 | `buildAWDLElectionParamsVer2` | `0xffffff8002211b81` | siempre |
| 2 | 12 datapath state | `buildAwdlDataPathStateTlv` | `0xffffff8002211d61` | siempre (variante no-SDB) |
| 3 | 30 ranging | `buildRangingTlv` | `0xffffff8002211d89` | `ivars+42545` |
| 4 | — RTG | `buildRealTimeGroupTlv` | `0xffffff8002211dc0` | `ivars+64418` o `ivars+65477` |
| 5 | 23 NAN sync | `buildNanSyncTlv` | `0xffffff8002211de0` | llamada siempre; 0 sin NAN |
| 6 | 7 enhanced data rate | `buildEnhDataRateTlv` | `0xffffff8002211e1c` | `1 ≤ ctrl+1564 ≤ 0x19` |
| 7 | 20 sync tree | `buildSyncTreeTlv` | `0xffffff8002211f46` | `ivars+10308` |
| 8 | 17 contenedor 802.11 | `buildIEEE80211CntnrTlv` | `0xffffff8002211f8b` | `ctrl+1568` bit 0 |
| 9 | 33 pref chan | `buildPrefChanTlv` | `0xffffff8002211faf` | llamada siempre; puede devolver 0 |
| 10 | **16 ARPA (IPv6)** | inline + `IO80211ServiceRequestDescriptor::copyDataOnly` | `0xffffff800221202d` / `0xffffff8002212053` | descriptor `_rarpv6Service` (`ivars+42448`) no nulo y con datos; traza `"    -- added arpa ip6 tlv (dlen %u) -> %u"` (string `0xffffff80023dfbe3`) |
| 11 | 21 versión | `buildAWDLVersionTlv` | `0xffffff80022120fb` | siempre |
| 12 | 6 service params | `buildServiceParamTlv` | `0xffffff8002212136` | `ctrl+1674` bit 0 = 0 o `SEC_PAYLOAD` (`+16576`) = 0 |
| 13 | 2 service responses | `buildServiceResponseTlvs` | `0xffffff80022121c5` | `ivars+16389` bit 0 |

El TLV 16 se escribe con `type = 16` (`mov byte ptr [rsi-4], 10h` en `0xffffff800221202d`), longitud
`dlen+1`, primer byte del cuerpo `3`, y a continuación los datos del descriptor de servicio
(`copyDataOnly`). Si el tamaño excede el umbral se registra
`"ERROR: %s:%d skipping ivars->_rarpv6Service TLV, tlv %u exceeds threshold %u"`. Si el payload MI
desborda, se fuerza el bit `0x20` de flags del TLV 6 (`mov byte ptr [rax+10h], 1` en
`0xffffff8002212191` sobre el flag, traza `"MI payload is overflowed"`, string
`0xffffff80023dfc8e`).

En el camino `ensemble = 1` (sólo usado por `sendUnicastMI` con opciones de ensemble) el orden
cambia: 24, RTG (`0xffffff8002211c22`, traza `"Ensemble UMI TLV"`, string `0xffffff80023dfafb`), 12
(`0xffffff8002211c77`, variante SDB con `options`), TLV 29 bss steering (`buildMultiPeerBssSteeringTlv`,
`0xffffff8002211cb7`, sólo con datos), ProMode (`0xffffff8002211ce7`, bit `addProMode`), AirDrop
(`0xffffff8002211d0d`, bit `addAirDrop`) y salida tras `buildPrefChanTlv` (`0xffffff8002211d34`)
si `options & 0x100 == 0`.

**Conclusión sobre el TLV 16 (ARPA): lo compone el kernel en la MI primaria, entre el 33 y el 21.
Una capa driver sin firmware no debe añadir un TLV 16 propio** — en el aire aparece en esa posición
(`AWDL_REFERENCIAS_EXTERNAS.md` §4: MIF `…,17,16,21,6,2…`) y no en el PSF, coherente con que el
constructor sólo existe en la plantilla MI.

### 3.3 TLVs que sólo puede rendir el driver

- **TLV 4 (sync params)** y **TLV 18 (secuencia de canales)**: ninguno de los dos constructores
  llama a nada que los genere; el barrido de símbolos del fileset no contiene ningún
  `build*` para los tipos 4, 5 ni 18 (los únicos constructores de elección/árbol/canal son
  `buildAWDLElectionParamsVer2`, `buildSyncTreeTlv` y `buildPrefChanTlv`). El kernel sólo los
  **parsea** en recepción (`parseAwdlChanSeqFromChanSeqTLV` `0xffffff80021b5a08`,
  `parseAwdlChanSeqFromSyncParamsTLV` `0xffffff80021b8196`, según `AWDL_TLV_BODIES.md`).
- **TLV 5 (elección v1)**: ídem; el parser de recepción existe (`parseAwdlElectionParamsV1`, citado
  en `AWDL_FRAME_WIRE_FORMAT.md` §4) pero no hay constructor. En el aire el iPhone lo envía en PSF y
  MIF en segunda posición, de modo que lo rinde el driver/firmware con los datos del ioctl 155 (§5.2).

### 3.4 Constructores que pueden no rendir nada

- `buildNanSyncTlv` (`0xffffff8002211146`): devuelve 0 salvo que el maestro vigente tenga un TLV NAN
  sync cacheado (lo copia del peer maestro y le incrementa la longitud; traza
  `"HC %d from master's Nsync tlv mismatch, should be %d"`). Explica que las capturas del iPhone no
  muestren el TLV 23.
- `buildPrefChanTlv` (`0xffffff8002211416`): devuelve 0 si el byte `netif+2879` no es negativo
  (indicador 6 GHz), si no hay canal de infraestructura convertible a clase operativa, o si el
  espacio restante no alcanza; escribe `{type 33, len 7+…, flags, canal+clase}`. Explica la ausencia
  del TLV 33 en las capturas.

## 4. Orden en el aire y punto de inserción

Con lo anterior, la trama que el driver monta es:

```
802.11 (FC, dur, A1, A2, A3) | 7F | 00 17 F2 | 08 | ver | subtipo | rsvd | phy_tx_time(4) | target_tx_time(4) |
[TLV 4 sync params] [TLV 5 election v1] [TLV 18 chan seq] | <plantilla del kernel, TLVs en su orden> | (sin relleno)
```

Contraste con las capturas de iOS 26 (`AWDL_REFERENCIAS_EXTERNAS.md` §4, 4893 tramas):

| Trama | Orden medido | Driver | Kernel |
|---|---|---|---|
| PSF | `4, 5, 18, 6, 24, 12, 7, 17, 21` | 4, 5, 18 | 6, 24, 12, 7, 17, 21 (§3.1; ranging/RTG/20/23/33 no activos en esa sesión) |
| MIF | `4, 5, 18, 24, 12, 7, 17, 16, 21, 6, 2, …` | 4, 5, 18 | 24, 12, 7, 17, 16, 21, 6, 2 (§3.2; 16 entre 33 y 21, 33 y 23 a cero) |

La correspondencia es exacta si el driver inserta **TLV 4, TLV 5 y TLV 18 — en ese orden — delante
del primer TLV de la plantilla recibida** (tipo 6 en el PSF, tipo 24 en la MI) y no toca el resto.
El subtipo de la cabecera fija (`+6`) es `0` para la plantilla del 151 y `3` para la de la MI; el
kernel no lo indica en ninguna petición: lo deduce el driver de qué plantilla está transmitiendo.

## 5. Fuente de cada campo de los TLVs del driver

### 5.1 TLV 4 — sync params

Layout del cuerpo en `AWDL_TLV_BODIES.md` §1.2 (42 B mínimos contando la cabecera TLV, con la
secuencia de canales embebida a partir de `+36`). Fuente kernel de cada campo:

| Offset | Campo | Fuente para el driver |
|---:|---|---|
| `+3` | `tchan` (canal de la próxima AW) | ejecución propia del 129 (ranura siguiente) |
| `+4` | `dc` (TU hasta el límite de franja) | calendario propio del driver |
| `+6` | `mchan` | ioctl **132** (`setAWDL_MASTER_CHANNEL`, 8 B, canal en `+4`; ioctl **205** para el secundario) |
| `+7` | guarda | 0 (valor del aire) |
| `+8` | periodo de AW (TU) | ioctl **116** `+0x08` (`aw_period`, ivar `+62940`) |
| `+10` | periodo entre action frames | ioctl **116** `+0x10` (`sync_frame_period`, ivar `+62952`); el kernel fija 110 (§6.1) |
| `+12` | flags (`0x1800` en el aire) | sin lector en el kernel; origen `?` |
| `+14` | `aw_ext_length` | ioctl **116** `+0x0c` (`ext_length`, ivar `+62948`) |
| `+16` | longitud de AW común | ioctl **116** `+0x04` (`aw_length`, ivar `+62944`) |
| `+18` | `rem` (TU restantes) | calendario propio del driver |
| `+20..+23` | ext bytes 1–4 | ioctl **118** (`setAWDL_EXTENSION_PARAMETERS`, 20 B → ivars `+17035..+17038`; el orden exacto de los cuatro bytes respecto de `+20/+23/+21/+22` `?`) |
| `+24..+29` | MAC del maestro | ioctl **134** `+0x08` (master) / ioctl **155** `+0x16` (root MAC) |
| `+30` | `pm` (modo de presencia) | ioctl **136** (`setAWDL_PRESENCE_MODE`, `u32 ≤ 0x3F`) |
| `+31` | reservado | 0 |
| `+32` | `sn` (n.º de la próxima AW) | contador propio del driver; debe ser coherente con el `AwNumber` del evento 41 (`monitorTimeline` compara) |
| `+34` | alineación con el AP | sin lector `?` |
| `+36..` | secuencia embebida | misma codificación que el TLV 18 (§5.3), parseada por `parseAwdlChanSeqFromSyncParamsTLV` `0xffffff80021b8196`; un fallo ahí invalida el TLV 4 completo. Las capturas del aire no permiten fijar si viaja con entradas completas o en forma mínima `?`; réplica del TLV 18 es la opción conservadora |

Los valores de 116/136/118 los programa userspace (wifip2pd/airportd) a través de los slots del
manager (`setAwdlSyncParams` `0xffffff800221b73c` guarda `+62940/+62944/+62948/+62952`; la petición
de userspace del tipo 116 es de 24 B y el payload interno que llega al driver es el de 20 B de
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.4, verificado aquí en `0xffffff80021ed024`: `{0, aw_length,
aw_period, ext_length, sync_frame_period}` con `LODWORD(v12) = +62940`, `HIDWORD(v11) = +62944`,
`HIDWORD(v12) = +62948`, `LODWORD(v13) = a2`).

### 5.2 TLV 5 — elección v1

Los parámetros los entrega el ioctl **155** (`setAWDL_ELECTION_PARAMS`, 36 B:
`op_mode +0x10`, `election_id u16 +0x12`, `hop_count +0x14`, `root_mac[6] +0x16`, `root_metric +0x1c`,
`self_metric +0x20`; emitido por `programChipWithNewMaster` `0xffffff80021ec4b6` y
`initFWElectionParams` `0xffffff80021ed200` en cada cambio de maestro y al habilitar). El cuerpo del
TLV 5 en el aire no está fijado en este documento (el kernel lo parsea con
`parseAwdlElectionParamsV1`, sin constructor propio) `?`; la correspondencia campo a campo con el
payload del 155 es la vía de render, y el propio kernel emite el TLV 24 (v2) ya montado con esos
mismos datos.

### 5.3 TLV 18 — secuencia de canales

Copia directa del ioctl **129** (`setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`, 400 B; layout en
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7 y `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1): `+3 = n−1`
(`num_channels−1`, byte), `+4 = codificación` (`unknown6`/`+42866`), `+5 = flags de duplicado`,
`+6 = step`, `+7..8 = fill u16`, entradas de 2 B (`+9` en adelante): canal y, con codificación 3,
clase operativa. Las entradas con canal 0 = ranura no servida (patrón de radio fija,
`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7). El formato de 12 B del 129 es interno; en el aire cada
ranura ocupa 2 B (`AWDL_TLV_BODIES.md` §2).

## 6. Cuándo y cómo se transmiten

### 6.1 PSF: periodo 110 contratado por 116/418

- El kernel entrega la plantilla por el 151 y contrata la cadencia por el **116**:
  `setSyncFramePeriod(period, force)` (`0xffffff80021ed024`) emite el 116 con
  `sync_frame_period = period` en `+0x10`; el driver debe repetir la plantilla del 151 cada ese
  periodo, alineado con el calendario compartido (mismos condicionantes que
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §5.4).
- El valor 110 lo fija el propio kernel: `setPSFDwell` (`0xffffff80021f4a2a`) calcula los pares de
  dwell `{110, 110}` (`mov rax, 6E0000006Eh` en `0xffffff80021f4c14` y `0xffffff80021f4c89`) o
  `{440, 440}` (`0x1B8000001B8`) según estado, los baja por el **418** (`mov rax, 1A2802869C8h` en
  `0xffffff80021f4f79`, llamada en `0xffffff80021f4f89`, payload de 16 B con los dos dwell de 4 B —
  trazas `"Setting PSFDwell to {%ums %ums}"`) y, si el dwell quedó activo, llama a
  `setSyncFramePeriod(this, 110, 0)` (`mov esi, 6Eh` en `0xffffff80021f501e`, llamada en
  `0xffffff80021f5025`). Las trazas del kernel imprimen la unidad como `ms`; la especificación de
  owly mide 110 TU en el TLV 4 (`+10`) — el mismo número en ambas escalas, unidad no confirmada `?`.
- El fallo del 418 desactiva el dwell (`ivars+16943 = 0`); el éxito lo activa y refresca `+16956..71`.
  El canal de dwell para el MI lo contrata `configureMITxOnDwellChannel` (`0xffffff80022140a4`) con
  el **432** (`awdl_config`, qword `0x1B0802869C8`) y un `u32` = `(canal != 0) << 7`: el bit `0x80`
  de `awdl_config` es «TX de MI en el canal de dwell» (trazas `"awdl_config %d, …"`).
- El re-envío del 116 también ocurre en `interfaceStateChange` (`0xffffff80021da0df`) y en cada
  `programChipWithNewMaster` (xrefs de `0xffffff80021ed024`: dos sitios en `0xffffff80021ec51e` /
  `0xffffff80021ecbd1`, `setScheduleState` ×2, `reevaluateRTGConfiguration`, `runPiggybackScanStateMachine`
  y `setPSFDwell`).

### 6.2 MIF: dentro de cada availability window

- El kernel mantiene su propio temporizador MI (`handleMasterIndicationTimer` `0xffffff80021c3014`,
  registrado en `initWithInterface` `0xffffff80021c1540` con `lea rcx, handleMasterIndicationTimer`
  en `0xffffff80021c20bf`; ídem el temporizador UMI en `0xffffff80021c213d`). Cada disparo ejecuta
  `updateBroadcastMI(this, 2, 0, 1)` — razón 2, `sendNow = 1` — que pasa por
  `updatePrimaryPayloadMI` (llamada en `0xffffff80021dcab3`) y sale por el **149 con dst broadcast**:
  un MIF «ahora». El intervalo de rearme del temporizador no se ha rastreado `?`; la cadencia
  efectiva en el aire es un MIF por AW (las capturas miden 2925 MIF frente a 1968 PSF en la misma
  sesión, y Broadcom repite `awdl_oob_af_auto` «en cada AW»).
- El camino **225** («template to fw») es la forma permanente: el driver almacena plantilla, bitmap y
  desfase y repite el MIF en cada ventana con el desfase indicado (§2.3). Una capa que sólo
  implemente el 149 broadcast funciona si lo atiende en cada disparo del kernel; el 225 es el modo
  offload.
- `updateBroadcastMI` (`0xffffff80021dca18`) sólo emite con AWDL conectado (`+16096` bit 1) y
  respetando el contador de peticiones en vuelo (`+17004` ≤ `+17008`); con razón 2 y bitmap
  `0x05050505` llama además a `adjustMiBitmap(0xFFFFFFFF, true, 2)`. Sus ~35 xrefs (los mismos
  cambios de estado que regeneran el PSF, más `setChannelSequenceUnconditional` `0xffffff80021cc1da`
  tras cada 129) confirman que **cada cambio relevante baja una plantilla MI nueva**.

### 6.3 UMI

`sendUnicastMI` (§2.2) con dst = MAC del peer, una vez por ventana y peer como máximo (≥ 10 ms de
separación, `getUMIInterval` = 10), dentro de la ventana (`+772`). `handleUmiTimer`
(`0xffffff80021c3254`) recorre la cola de peers con UMI pendiente y se salta un peer si su canal
(`peer+924`) o sus flags (`peer+928 & 0x2018`) no coinciden con la ventana actual (`ivars+42828` /
`ivars+42832`), o si el peer no está en la caché (`peer+600/+601`): la decisión de a qué peer se le
manda UMI en cada ventana es del kernel, no del driver.

### 6.4 Canal, tasa y cabecera 802.11

- **Canal**: ninguna petición de plantilla (151/149/225) lleva canal. El frame sale en el canal que
  la radio ocupa al transmitir, es decir, la ranura vigente de la secuencia del 129 que el propio
  driver ejecuta, o el canal de dwell si el bit `0x80` de `awdl_config` está activo (§6.1).
  Para que el anuncio sea honesto, el canal de transmisión debe coincidir con lo que el TLV 18
  declara para esa ranura (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §7).
- **Cabecera 802.11** (la construye el driver; el kernel no la toca):
  - A1: broadcast `FF:FF:FF:FF:FF:FF` para PSF y MIF (en el 149/225 el kernel pasa exactamente ese
    dst para el camino broadcast, y Broadcom fija `awdl_af_hdr = FF×6 7F 00 17 F2` —
    `AWDL_FIRMWARE_DUTIES.md` §3.3 — para «todo action frame AWDL que transmite»); la MAC del peer
    para el UMI (dst del 149).
  - A2: MAC propia de `awdl0` (la fijada por el tipo 125/`cur_etheraddr`; en el payload 149/225
    viaja además `self_bssid`).
  - A3: BSSID AWDL `00:25:00:FF:94:73` — valor medido en todas las tramas AWDL
    (`AWDL_REFERENCIAS_EXTERNAS.md` §5) y programado en la interfaz por userspace con el tipo 125
    (`setAWDL_BSSID`); el kernel lo reenvía en `self_bssid` (`ivars+37204..37209`) de cada 149/225.
  - Cuerpo: categoría `0x7F`, OUI `00:17:F2`, tipo `0x08`, versión ≠ 0, subtipo (0 PSF / 3 MI),
    reservado, `phy_tx_time` y `target_tx_time` (§6.5), y a continuación los TLVs.
- **Tasa**: el kernel no contrata ninguna tasa en las peticiones de plantilla. Broadcom inicializa
  `2g_mrate`/`5g_mrate` a 12 Mbps en la creación del chip interface (`AWDL_FIRMWARE_DUTIES.md` §3.1)
  y expone `awdl_min_rate` (tipo 213). La tasa de PSF/MIF es política del driver `?` (12 Mbps es el
  único valor con soporte en los binarios analizados).

### 6.5 `phy_tx_time` y `target_tx_time`

Campos `+8`/`+12` de la cabecera fija (`AWDL_FRAME_WIRE_FORMAT.md` §2). `parseAwdlSyncParamsTlv`
(`0xffffff80021b4ba6`) los lee: si ambos son no nulos y `+8 > +12`, la diferencia se guarda como
`sync_contention` (peer `+752`) y ambos se pasan a `monitorPeersTimeline`
(`AWDL_TLV_BODIES.md` §1.3). A cero no rompen el parseo, pero desactivan esa medida. Un driver debe
rellenarlos con su propio reloj de microsegundos (tiempo de TX real y previsto) para que la medición
de contención tenga sentido; la unidad exacta que espera el aire es la de owly (µs) `?`.

## 7. Realimentación que espera el kernel después de transmitir

- **Eventos 41/42/43** (obligatorios, vía `reportDataPathEvents`, celda `+0x298`): payloads de 36/4/40
  B documentados en `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2/§2.4/§2.5. El 41 (`AwNumber`, maestro, top
  master, `FwAwStartTime` big-endian, canal, flags) es el que abre la ventana (`ivars+772`) y habilita
  el tirón de TX y los UMI; el 42 pone a cero/actualiza el contador `ivars+408`; el 43 la cierra y
  dispara la elección. El `AwNumber` y el canal del 41 deben ser coherentes con el `sn`/`tchan` que
  el driver rinde en el TLV 4: `monitorTimeline` compara ambos contra la secuencia (sólo registra
  discontinuidades, no corrige — `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §6).
- **Estado de TX por trama: no existe.** `reportDataPathEvents` no tiene rama para ningún
  «TX status»; la única confirmación de una transmisión de plantilla es el retorno síncrono del
  ioctl (0 = entregado). En Broadcom la completion de un OOB AF llega como `WLC_E_ACTION_FRAME_COMPLETE`
  (60) y el kext la publica como evento de userspace 69 (`AWDL_FIRMWARE_DUTIES.md` §4.2), cadena que
  no consume el manager. Un driver propio no debe inventar un evento de TX: el kernel no lo espera.
- **Evento 195 (`AWDL_CHAN_SEQ_CHANGED`)**: **lo publica el manager, no el driver**. Tras un 129 con
  éxito, `setChannelSequenceUnconditional` emite el mensaje 195 a la interfaz con los 400 B de la
  secuencia (vía la celda `+2840` de la interfaz; `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1 y
  `AWDL_EVENTS.md` §3). Para el driver es transparente.

## 8. Qué debe construir la capa RTL8814 por trama (lista ordenada)

**Al recibir el 151 (plantilla PSF):** copiar `length` bytes de la plantilla (TLVs `6, 24, 12, 7, …,
17, 21, …` en el orden de §3.1) y registrar el periodo del último 116 (`+0x10`).

**Al recibir 149/225 (plantilla MI):** copiar `template_len` bytes de `template` inmediatamente (el
buffer `ivars+9048` se reutiliza para la MI secundaria, §2.4); con 225, almacenar además
`offset_us` y `mi_bitmap`. Con 342, lo mismo para la MI secundaria (SDB).

**Por cada PSF (repetición del sync frame, cada `sync_frame_period` del 116, alineada al calendario
compartido):**

1. Cabecera 802.11: A1 `FF:FF:FF:FF:FF:FF`, A2 MAC de `awdl0`, A3 `00:25:00:FF:94:73`; tasa según
   política (12 Mbps de referencia Broadcom `?`).
2. Cabecera fija de 16 B: `7F | 00 17 F2 | 08 | ver≠0 | subtipo 0 | rsvd | phy_tx_time | target_tx_time`
   (reloj µs propio).
3. **TLV 4** (sync params, §5.1): periodo/longitudes/ext del 116, `pm` del 136, ext bytes del 118,
   `mchan` del 132, maestro del 134/155, `tchan`/`dc`/`rem`/`sn` del calendario propio, secuencia
   embebida = TLV 18.
4. **TLV 5** (elección v1, §5.2) con los datos del 155.
5. **TLV 18** (secuencia, §5.3) con los datos del 129: canal real en las ranuras atendidas, 0 en las
   demás.
6. Copia íntegra de la plantilla del 151 recibida (sin insertar nada entre sus TLVs).
7. Sin relleno final (el parser lo tolera, no lo necesita).

**Por cada MIF (una vez por availability window; en el desfase del 225 si se aceptó el modo auto, o
al recibir el 149 broadcast del temporizador MI):** ídem 1–7 con subtipo `3` y la plantilla MI del
149/225 (TLVs `24, 12, …, 17, 16, 21, 6, 2…`; el 16 ARPA ya viene dentro). No añadir TLV 16 propio.

**Por cada UMI (149 unicast, dentro de la ventana actual):** ídem con A1 = dst del 149 y subtipo `3`,
tan pronto como sea posible dentro de la ventana (el kernel ya limitó el ritmo a 10 ms por peer).

**Transmitir en el canal de la ranura vigente** (o el de dwell si `awdl_config` bit `0x80`), de modo
que canal de TX y TLV 18 coincidan.

**Tras cada ventana: eventos 41/42/43** con los payloads de `AWDL_PEERMANAGER_DRIVER_ABI.md` §2,
coherentes con el `sn`/`tchan` anunciados y con el canal realmente usado. Nada más: sin evento de
TX por trama; el 195 lo genera el manager.

## 9. Correcciones a notas previas

- `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.10 coloca `template_len` en `+0x20` y `template` en `+0x28` del
  payload del 149; la descompilación de `sendUnicastMI` (`0xffffff8002213c0a`: `LOWORD(v40)` = len,
  `v41` = ptr, con `v40`/`v41` en los offsets +0x28/+0x30 del payload) y la lectura simétrica del
  handler Broadcom (`request+0x28` = len, `request+0x30` = puntero, `AWDL_STRUCTS.md` §
  `apple80211_awdl_oob_request`) fijan el layout correcto: `template_len` en `+0x28` y `template` en
  `+0x30`. La longitud total de 56 B no cambia.
- `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11 describe la plantilla del 151 con los constructores 8
  (`buildAWDLVersionTlv`), 10 (`buildNanSyncTlv`) y 11 (`buildPrefChanTlv`) como «siempre»; son
  llamadas incondicionales pero los dos últimos pueden rendir 0 bytes (§3.4), lo que explica las
  capturas del aire sin TLV 23 ni 33.
- `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.2 atribuye al firmware Broadcom los TLVs 4 y 18 y deja
  implícito que el TLV 5 y el 16 también nacen fuera del kernel: el 5 sí (sin constructor en el
  kernel), pero el 16 lo compone `buildMasterIndicationTemplate` en la MI primaria (§3.2).
- Nuevo dato: `interfaceStateChange` emite también el 116 por su cuenta (`0xffffff80021da0df`), y el
  bit `0x80` de `awdl_config` (tipo 432) es el flag «TX de MI en el canal de dwell»
  (`configureMITxOnDwellChannel` `0xffffff80022140a4`).

## 10. Abiertos

- Unidad del periodo del 116/`sync_frame_period` (110): las trazas del kernel dicen `ms`, owly mide
  110 TU en el TLV 4 `+10` `?`.
- Contenido exacto del cuerpo del TLV 5 en el aire y su mapeo byte a byte con el payload del 155 `?`.
- Cerrado: en las capturas de iOS 26 el TLV 4 ocupa 73 B en las 4893 tramas (iPhone y nodo owly),
  es decir 33 B de campos + la secuencia embebida completa (6 B de cabecera + 16 franjas × 2 B en
  codificación 3) + 2 B de relleno. El driver debe rendir la secuencia completa dentro del TLV 4.
- Orden interno de los cuatro ext bytes del 118 respecto de las posiciones `+20/+23/+21/+22` del
  TLV 4 `?`.
- Origen de los flags `0x1800` del `+12` del TLV 4 y del campo `+34` (alineación AP) `?`.
- Tasa de transmisión de PSF/MIF: el kernel no la contrata; 12 Mbps es sólo el init de Broadcom `?`.
- Intervalo de rearme del temporizador MI/UMI del kernel (registrados en `initWithInterface`, rearme
  no rastreado) `?`.
- Semántica fina de `mi_bitmap` (`ivars+42760`, bits por ranura según `setMiBitmap`/`adjustMiBitmap`)
  en el payload del 225 `?`.
