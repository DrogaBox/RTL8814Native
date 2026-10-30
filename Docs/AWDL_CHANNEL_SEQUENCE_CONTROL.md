# Control de la secuencia de canales AWDL: quién la decide, qué la alimenta y qué puede hacer una radio que no salta

Cómo decide `IO80211AWDLPeerManager` la secuencia de canales que se anuncia en el aire (TLV 18 y la
secuencia incrustada en el TLV 4), qué entradas la alimentan, cómo se baja al driver y qué pasa si el
driver rechaza o ignora la orden. Cierra con el mínimo que la capa del RTL8814AU debe implementar para
que la secuencia anunciada sea honesta. Complementa `AWDL_PEERMANAGER_SOFTWARE.md` §6,
`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7 y `AWDL_TLV_BODIES.md`.

Fuente: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0), copia de la IDB anotada
`~/re_cache/ida/boot_awdl.kc.i64`, descompilación de las funciones citadas. Todas las VA
son de instrucción o dato leídas en esa IDB. Los offsets de ivars pertenecen a esta build. Lo no
verificado se marca `?`.

## 1. Resumen

La secuencia de 16 ranuras que se anuncia en el aire **no la construye IO80211Family**: las plantillas
de PSF y UMI del kernel (`updateSyncFrameTemplate`, `buildMasterIndicationTemplate`) no contienen el
TLV 4 (sync params) ni el TLV 18 (chan seq); en Broadcom esos dos TLVs los compone el firmware a
partir del estado programado. El kernel decide el **contenido** de la secuencia y lo baja al driver
con el ioctl interno 129 (`setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`, 400 B), más los canales
sociales con los 132/205; el driver es quien la ejecuta (dwell por ranura) y quien la traduce a TLVs.

La decisión del kernel es una cadena de tres niveles: una **estrategia** (ivar `+4`: `AwdlFollowsInfra`,
`AwdlStateMachine`, `AwdlFixed`, `AwdlUserConfigured`), un **estado de calendario** (`scheduleState`,
elegido por `setScheduleState`) y una **tabla estática** `__ZL20_channelSeqSlotTypes[estado][ranura]`
que asigna a cada ranura uno de 21 tipos, cada tipo con su fuente de canal (cero = no servido, canal
social, canal de infraestructura, canal actual, canal RT, …). La secuencia resultante vive en
`ivars+42868` (16 entradas de 12 B) y sale por el 129.

Una radio que no salta de canal puede operar con secuencia honesta sin tocar IO80211Family: el driver
recibe el 129 y es él quien rinde el TLV 18; basta con que anuncie su canal fijo en las ranuras que
atiende y canal 0 en las demás, con el modo de presencia del TLV 4 coherente. El kernel ya tiene
precedentes de secuencias de un solo canal: la secuencia por defecto pone canal 6 en las ranuras 0 y 8
y cero en el resto, y durante un roam todas las ranuras se rellenan con el mismo canal.

## 2. Dónde vive la secuencia propia

Bloque de ivars AWDL (`*(manager+0x20)`):

| Ivar | Campo |
|---:|---|
| `+42864` | `u16` longitud codificada = `n · (step+1) · (dup+1)` (la escribe `setChannelSequenceUnconditional`) |
| `+42866` | codificación (0 = canal suelto, 3 = pares canal/clase operativa) |
| `+42867` | step count (sub-ventanas por ranura) |
| `+42848` | duplicate `?` nombre |
| `+42856` | canal de relleno (`getChanSeqFillChan`, `0xffffff8002234ff8`) |
| `+42820` | `n`, número de ranuras declaradas (0..32) |
| `+42868` | entradas de 12 B: `{u32 versión/flags, u32 canal, u32 flags}` — el canal es la dword de `+42872`. La máquina de estados rellena las 16 primeras; el bucle de copia del 129 itera hasta `+42820`, que puede llegar a 32 con una secuencia de userspace |
| `+43252` | copia de la secuencia anterior (384 B; la guardan `setScheduleState` y `followPeerChannelSequence` con `real_mode_bootstrap_end`/memmove de 0x180) |
| `+8` | copia de los últimos 400 B enviados al driver (memcmp de 0x190 para suprimir reenvíos) |

`setMyChanSeq` (`0xffffff8002235302`) escribe una entrada: copia 12 B a `+42868+12·i` y, si la entrada
marca 6 GHz (bit `0x20` de `+42877`) y `is6GChannelALlowedForP2P` lo rechaza, la sustituye por
`_channel6Info` (con SDB activo) o `_zeroChanInfo`. Único llamador:
`IO80211RealTimePeerManager::updateAWDLChannelSequenceWithRtChannelSequence` (`0xffffff800234c05e`).
La misma guardia de 6 GHz corre al final de cada ranura en `setChannelSequenceByState`.

Globales de relleno (datos):

| Dirección | Símbolo | Contenido |
|---|---|---|
| `0xffffff8002488a88` | `__ZL13_zeroChanInfo` | `{1, 0, 0}` — **canal 0 = ranura no servida** |
| `0xffffff8002488a90` | `dword_FFFFFF8002488A90` | la dword de banda de `_zeroChanInfo` (0) |
| `0xffffff80023aaff0` | `__ZL13_channel6Info` | `{1, 6, 10}` — canal 6 con flags `0xA` `?` semántica del bit 1 |

Referencias leídas en `0xffffff80021f54ab`, `0xffffff80021f549e`, `0xffffff80021f5bb0` (dentro de
`setChannelSequenceByState`) y en `0xffffff80021c79d8`/`0xffffff80021c79e2` (dentro de
`initDefaultChannelSequence`).

## 3. Quién decide el contenido: estrategia → estado → tabla

### 3.1 Estrategia

`setChannelSequenceStrategy(nueva, razón, forzar)` (`0xffffff80021ca958`) guarda la estrategia en el
ivar `+4` del bloque AWDL. Los nombres de la tabla `0xffffff800245B6D0`:

| Valor | Nombre |
|---:|---|
| 0 | `AwdlFollowsInfra` |
| 1 | `AwdlStateMachine` |
| 2 | `AwdlFixed` |
| 3 | `AwdlUserConfigured` |

Llamadores (xrefs verificados): `configure` (`0xffffff80021c54ea`, fija 1 con razón 0),
`setAwdlCtl` (`0xffffff80021cde94`, la fija desde la petición de userspace con razón 3 y relanza
`setScheduleState` con razón `0x15` y `programMasterChannels`), `interfaceStateChange`
(`0xffffff80021d8cc8`, en `0xffffff80021d900a`), `monitorAWDLState` (`0xffffff80021ed724`, en
`0xffffff80021ee3ae`), `setAwdlChannelSequence` (fija 3 al aceptar una secuencia de userspace),
`setAwdlAutoMode` y `setAwdlOperatingMode` (fijan 1, razón 6), y `enableAWDL`
(`0xffffff80021dcf58`): si hay servicios AWDL válidos (`+42568`), elige 0 o 1 (razón 4) según
`+16289` y los contadores `+42660`/`+42588` `?` condición exacta del valor 0/1.

### 3.2 Estado de calendario

`setScheduleState(scheduleState, razones, forzar)` (`0xffffff80021d27a0`) es el orquestador. Salidas
tempranas verificables: el gate `+42560` (se activa cuando `setAwdlSyncParams` completa con éxito un
ioctl 116, ver §4), AWDL habilitado y conectado (`+16096` bits 0 y 1), sin disable en curso (`+414`),
y los modos de exclusión SDB en progreso (`+16505`, `+66560`). Si hay un par cuya secuencia seguir
(`+16248` o el peer de `+16224` con el bit de `+1665`), llama a `followPeerChannelSequence` y no toca
la tabla. Si no, aplica uno de tres caminos (líneas de la descompilación):

1. `setChannelSequenceByState(estado)` (`0xffffff80021f53aa`, llamada interna) — estrategias 1/0 con
   tabla por estado;
2. `initDefaultChannelSequence()` (`0xffffff80021c79c0`) + `setChannelSequence(forzar)`
   (`0xffffff80021f7f78`) — camino por defecto;
3. `followPeerChannelSequence(peer)` — modo seguir.

Los nombres de los 56 estados están en la tabla `0xffffff8002459C20` (`StringForAwdlState`,
`0xffffff80021a8b9e`). Los que aparecen en la descompilación de `setScheduleState` y en la tabla de
ranuras: 0 Discovery, 1 Discovery+InfraStream, 3 Idle, 5 Data, 9 RealTimeData, 10/11
RealTimeData+InfraAssist, 12–21 variantes Split Infra, 23 IdleAndRealTimeData, 24/25/26/27/28 Scan,
29 Infra Priority, 30/31 Infra BE RealTime, 43 Ranging, 45/47/48 AWDL Fast Discovery, 49–52 ProMode,
53 Long Discovery, 54 Ultra Low Latency Infra, 55 RTG AirDrop. El estado vigente vive en `+10468`.

### 3.3 La tabla de tipos de ranura

`__ZL20_channelSeqSlotTypes` en `0xffffff80023aa1f0` (referencia leída en la instrucción
`0xffffff80021f5475` de `setChannelSequenceByState`): una dword por ranura, 16 ranuras por estado.
El índice es directo: `estado << 6` (`shl rax, 6` en `0xffffff80021f545c`) más `ranura·4`, y el
`switch` de `0xffffff80021f5480` tiene 21 casos (tipos 0..20). Hay una fila por cada uno de los 56
estados. Filas relevantes:

| Estado | Ranuras 0..15 (tipo) |
|---|---|
| 0 Discovery | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` |
| 3 Idle | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` |
| 5 Data | `0 ×16` |
| 9 RealTimeData | `0 ×16` |
| 10 RealTimeData + InfraAssist | `9 ×16` |
| 11 RealTimeData + InfraAssist with Social | `9 9 3 9 9 9 9 9 9 9 4 9 9 9 9 9` |
| 12 RealTimeData + Split Infra Legacy | `12 12 12 12 5 5 5 5 12 12 12 12 6 6 6 6` |
| 14 RealTimeData + Split Infra + Same DFS | `12 5 5 5 5 5 5 5 5 12 5 5 5 5 5 5` |
| 29 Infra Priority | `9 9 3 1 1 1 1 9 2 9 3 9 1 1 1 9` |
| 41 High Thruput | `4 ×16` (canal AWDL actual en todas las ranuras) |
| 45 AWDL Fast Discovery | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` |

El tipo 0 significa «no reescribir esta ranura». Las filas enteras a 0 son las de los estados 5 Data,
9 RealTimeData, 28 Piggyback Scan + Data, 32 Forced, 39 Sidecar Start, 40 RtMode Start, 44
RealTimeGroup y 55 RTG AirDrop. En ellos el kernel no recalcula la secuencia y anuncia lo que ya haya en
`+42868`, que es justo donde dejan su resultado `followPeerChannelSequence`, `setMyChanSeq` (RTG) y la
secuencia de userspace.

### 3.4 Fuentes por tipo de ranura

`setChannelSequenceByState` (`0xffffff80021f53aa`) recorre 16 ranuras (`+42868` a paso 12) y aplica el
tipo leído de la tabla. Tipos verificados en el `switch`:

| Tipo | Fuente |
|---:|---|
| 0 | no se escribe (ranura intacta) |
| 1 | `_zeroChanInfo` — **canal 0** |
| 2 | canal social primario (`+17056`) |
| 3 | secundario si `+17072 ≠ 0`, si no primario |
| 4 | canal actual AWDL (`+416`/`+424`) |
| 5/6 | pares `+17080/+17088` y `+17092/+17100` `?` semántica (canales de presencia/asistencia) |
| 7 | canal de infraestructura (controlador `+2492`/`+2500`) si `addInfraToMinimumChannelSequence` (`0xffffff80021f5ef6`) lo permite; si no, cero |
| 8 | infra si `+42568 ≠ 0` (contador de servicios AirPlay `?`), si no cero |
| 9 | infra si `infraAllowedForNonRealtimeCases`; si no: ranura 0 → infra si `+16089` y hay canal de infra, si no canal 6; ranuras > 0 → canal 6 sólo con SDB activo, bit `+65193`, estado ≤ 0x30 y el bitset `0x1A00000000000`; en los demás casos cero |
| 10 | canal guardado en `+64008` si `+64012 ≠ 0`; si no, par social `+17056`/`+17068` |
| 11/14 | como el 10 pero con límite de 1 s (marcas `+64040`/`+64048` medidas con `absolutetime_to_nanoseconds`) y caída al canal de infra antes que al social |
| 12 | par `+440/+448` `?` semántica |
| 13 | mezcla de la secuencia de descubrimiento (`+64008..+64028`) con los pares sociales; marca bitmaps de coincidencia en `+64032`/`+64036` |
| 15 | par `+452/+460` `?` semántica |
| 16 | canal RT `+65404` si `+65408 ≠ 0`, si no canal actual |
| 17/18/19/20 | mezclas canal RT / infra / social / canal 6 según SDB, DFS y `infraAllowedForNonRealtimeCases` |

Tras cada ranura: si la entrada marca 6 GHz y no se permite, se sustituye por `_channel6Info` (ranura
0 o SDB activo) o `_zeroChanInfo`.

### 3.5 Secuencia por defecto

`initDefaultChannelSequence` (`0xffffff80021c79c0`): pone `_zeroChanInfo` en las `n` ranuras de
`+42820` y después escribe a mano la ranura 0 = canal 6 (`0x600000001`, banda `8`) y copia esa entrada
a la ranura 8 (`+42964`). Es el equivalente en kernel del patrón del aire: canal social en 1–2 ranuras
de 16 y canal 0 en el resto (owly AWDL-12/16, `AWDL_REFERENCIAS_EXTERNAS.md` §6).

### 3.6 Seguir la secuencia de un par

`followPeerChannelSequence` (`0xffffff80021f6e74`): llamado desde
`IO80211AWDLPeer::updateAwdlChanSeq` (`0xffffff80021ab7e4`, dos xrefs), `parseAWDLRangingTLV`,
`parseAwdlDataPathTLVAndTakeAction`, `setScheduleState`, `setAirPlayState` y
`enterAssistedDiscoveryFollowMode`. Copia las `n` entradas de 12 B del par — leídas del bloque del par
a partir de `peer+920` (canal en `+924`), con el mismo formato de tres dwords — a `+42868`, guarda la
anterior en `+43252`, aplica la guardia 6 GHz, fija `+10468 = 9` (RealTimeData) o solapa un scan
pendiente, y llama a `setChannelSequenceUnconditional(true)`. Con RTG activo
(`+64418`) en lugar de seguir reprograma la configuración RTG (`reevaluateRTGConfiguration`).

### 3.7 Roam: el precedente de «todas las ranuras iguales»

`setRoamChannelSequence(canal)` (`0xffffff80021e5b74`): levanta `+66465` (roam en curso) y rellena
**todas** las ranuras de `+42868` con el mismo canal (con guardia 6 GHz), después
`setChannelSequenceUnconditional(true)`. Demostración de que el contrato admite una secuencia
constante de un solo canal sin cambio alguno en el kernel. El estado 41 High Thruput hace lo mismo
por tabla: tipo 4 (canal AWDL actual) en las 16 ranuras (§3.3).

## 4. Entradas que alimentan la secuencia

| Entrada | Camino verificado |
|---|---|
| Estrategia (ivar `+4`) | `configure`, `setAwdlCtl`, `interfaceStateChange`, `monitorAWDLState`, `setAwdlChannelSequence` (userspace ⇒ 3), `setAwdlAutoMode`/`setAwdlOperatingMode` (⇒ 1), `enableAWDL` (§3.1) |
| Estado de calendario (`scheduleState`) | `setScheduleState` `0xffffff80021d27a0`; la tabla `__ZL20_channelSeqSlotTypes` elige fuentes por ranura |
| Tabla estática | `0xffffff80023aa1f0` (§3.3) |
| Canales sociales | `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`, llamada desde `interfaceStateChange`): elige entre 6/44/149 según el canal maestro `+17044` y el secundario `+17048`; máscara en `+17052` (bit0=6, bit1=44, bit2=149); infos en `+17056`/`+17068` |
| Canal de infraestructura | controlador `+2492` (canal) / `+2500` (flags; `0x2000` bloquea la elección de canal 6/RT `?` nombre, `0x800/0x400/4` = clase de ancho, `0x10` = `?`), usada por los tipos 7/8/9/10/11/14/17/19 |
| Secuencia de userspace | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` (tipo 129) desde `setAWDL_...` handler `0xffffff8002288cfe` → `setAwdlChannelSequence` (`0xffffff800221cedc`): valida `n ≤ 32`, procesa DFS, reenvía el 129 al driver, guarda la secuencia como propia y fija estrategia 3 |
| Secuencia de un par | TLV 18/32/4 parseado → `updateAwdlChanSeq` → `followPeerChannelSequence` (§3.6) |
| RTG / AirPlay | `IO80211RealTimePeerManager::updateAWDLChannelSequenceWithRtChannelSequence` → `setMyChanSeq`; `setRealTimePeerSession` y `resolveAWDLChannelConflict` (xrefs verificados al emisor del 129) |
| Roam | `setRoamChannelSequence` (§3.7) |
| Modo de presencia | `setAwdlPresenceMode` (`0xffffff800221e806`, tipo 136 desde userspace, exige valor ≤ 0x3F) y `setPresenceMode` (`0xffffff80021cc5a6`, ioctl interno `0x88802869C8` con tipo 136): ambos dejan el modo en `+17032` y lo bajan al driver con el 136. No escribe ninguna ranura de `+42868`; su efecto sobre la secuencia es indirecto (las fuentes 5/6 y el render que hace el firmware del TLV 4) `?` |
| Parámetros de extensión | `setAwdlExtensionParams` (`0xffffff800221bc44`, tipo 118 desde userspace; −1 = «usa el actual») y `setAwdlExtensionParameters` (`0xffffff80021cc670`, ioctl interno `0x76802869C8` tipo 118): 4 bytes en `+17035..+17038` (los ext bytes 1–4 del TLV 4, `03 03 03 03` en el aire) |
| SDB | flags `+43636`: `setChannelSequenceUnconditional` les suma `0x10` (SDB ON) o `0x20` (OFF) según `dynSdbForceChanSeq` (`0xffffff80021f8128`), el estado `+16512`, y los bits `+16501/+16503/+16504`; los eventos 64/160 (`SDB_MODE_CHANGED`) reconstruyen plantilla y MI (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.6) |
| Sync params (gate) | `setAwdlSyncParams` (`0xffffff800221b73c`, tipo 116 desde userspace): guarda periodo/longitud/ext (`+62940/+62944/+62948/+62952`) y activa el gate `+42560` sin el cual `setScheduleState` y `setChannelSequence` no actúan |
| País / DFS | `countryCodeStateChanged` (celda 42, `0xffffff800220dca4`); `processAWDLDFSChannelSequence` (`0xffffff800221a466`) corre antes de cada emisión del 129 y en `setAwdlChannelSequence`; `APPLE80211_M_AWDL_DFS_CSA` tratado en `reportDataPathEvents` |
| `awdl_cap` | lectura `0x155C02869C9` en `enableAWDL`: decide `SEC_PAYLOAD`/`CCA_STATS`/`PSF_DWELL_*`, no altera las ranuras |
| Boot-args | `initWithInterface` lee `awdl_allow_40`, `awdl_max_bw`, `awdl_election_id`, … (`AWDL_PEERMANAGER_SOFTWARE.md` §2.2); cuál de ellos condiciona las fuentes de canal de la tabla no se ha rastreado ranura a ranura `?` |
| 6 GHz | guardia global `is6GChannelALlowedForP2P` en `setMyChanSeq`, `setRoamChannelSequence`, `followPeerChannelSequence` y al final de cada ranura de `setChannelSequenceByState` |

## 5. Bajada al driver y vuelta del aire

### 5.1 El ioctl 129 y la emisión

`setChannelSequenceUnconditional(forzar)` (`0xffffff80021cb69a`):

1. Compone el payload de 400 B: `+0 = 1` (versión), `+4` = flags de `+43636` con los bits SDB del §4,
   `+5 = n`, `+6` codificación, `+7` step, `+8` dup, `+9` fill, entradas de 12 B desde `+12`
   copiadas de `+42868` (mismo layout que `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.7). Detecta si hay
   canales 2,4 GHz (canal 1–14 con bit 13 de flags a 0) y marca `+63023`.
2. Fija la longitud codificada `+42864 = n·(step+1)·(dup+1)`; si el resultado `u16` alcanza 513
   (`(len-1) >= 0x200`), registra el assert y no emite.
3. Supresión: `memcmp` de 0x190 contra `+8`; si no cambió y no se fuerza, no emite.
4. `processAWDLDFSChannelSequence` y emisión con `ioctl_internal` y el comando `0x81802869C8`
   (tipo 129) — la descompilación muestra `v95 = 0x81802869C8` antes de `ioctl_internal`.
5. Éxito: guarda la copia en `+8`, `updateBroadcastMI`, y si la secuencia cambió:
   `updateMulticastQueues`, refresco de pares cacheados (`_updateCachedPeers`), publicación del
   mensaje 195 a la interfaz con los 400 B (vía la vtable `+2840` de la interfaz
   `*(manager+24)+1376` `?` nombre simbólico del mensaje) y rearme del timer `+10376`. Fallo: ver §6.

La emisión la disparan (xrefs del §3): `configure`, `enableAWDL`, `setChannelSequence` (gate
`+42560` y AWDL arriba), `followPeerChannelSequence`, `setRoamChannelSequence`, los eventos 198
(overlay de scan 6 GHz, en `reportDataPathEvents`) y la reevaluación RTG, entre otros.

Los canales sociales se bajan aparte en `programMasterChannels` (`0xffffff80021d494a`): 132
(`0x84802869C8`, canal `+17060`) y 205 (`0xCD802869C8`, canal `+17072`), tras
`updateFWSocialChanListWithPSFSuppressionInfo` (`0xffffff800220db4e`) si procede.

### 5.2 Quién construye los TLVs del aire

- Las plantillas del kernel no llevan ni el TLV 4 ni el TLV 18: `updateSyncFrameTemplate`
  (`0xffffff80021dc5b2`) compone los TLVs 6, 24, 12 (data path state, `buildAwdlDataPathStateTlv`
  `0xffffff8002210e5a`), 7, ranging, RTG, 17, 21, 20, NAN sync y 33 (pref chan,
  `buildPrefChanTlv` `0xffffff8002211416`); `buildMasterIndicationTemplate`
  (`0xffffff8002211b42`) añade 29 (BSS steering), ProMode y AirDrop. En ninguna de las dos hay
  constructor del tipo 4 ni del 18.
- El lado Broadcom confirma el reparto: `AppleBCMWLANProximityInterface::buildAWDLChanSequence`
  (`0xffffff80017258bc`) convierte el `apple80211_awdl_sync_channel_sequence` del 129 al formato del
  firmware (`len-1`, codificación, dup, step, fill como `u16` `0xFFFF` si vale 255, y entradas de
  **2 B**; con codificación 3 calcula el ChanSpec con `getChanSpec` y lo escribe big-endian con
  `ROL2(x,8)`; las ranuras con canal 0 quedan a cero). Detalle propio de Broadcom: si la ranura 0
  llega a 0, la fuerza a canal 6 («chan_sequence[0]==0, forcing to 6»). El TLV 4 lo compone el
  firmware con el periodo del 116, la presencia del 136 y los ext bytes del 118 — la plantilla del
  151 no lleva temporización ni presencia (`AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11), de modo que ese
  es el único sitio donde puede nacer `?`.
- El kernel sólo **lee** esas dos formas en recepción: `parseAwdlChanSeqFromChanSeqTLV`
  (`0xffffff80021b5a08`), `parseAwdlChanSeqFromChanSeqExtTLV` (`0xffffff80021b5e84`) y
  `parseAwdlChanSeqFromSyncParamsTLV` (`0xffffff80021b8196`) según `AWDL_TLV_BODIES.md`.

### 5.3 Ejecución por ranura

El kernel no ordena saltos de canal por ranura: entrega la secuencia completa (129) y el
driver/firmware hace el dwell; los eventos 41/42/43 de `reportDataPathEvents`
(`0xffffff80021dd8f4`) devuelven la línea temporal. En la rama del evento 43, tras cerrar la ventana,
si `+17032 ≠ 0` el manager llama a `IO80211Controller::setCurrentChannel`. La descompilación solo
muestra el controlador como argumento (`?` si la convención oculta otro), y no hay un canal por
ranura calculado antes de la llamada: reafirma el canal que el controlador ya considera actual, no
programa un salto. El índice de
ranura que el manager usa del evento 41 es `min(15, (AwNumber % ((+42867+1)·+42820))/(+42867+1))`
(fórmula de `AWDL_PEERMANAGER_DRIVER_ABI.md` §2.2, escrita en `+16128`; `getCurrentAWSlot`
`0xffffff800223508a` lo devuelve).

### 5.4 Lectura de vuelta

`getAwdlChannelSequence` (`0xffffff800221f7bc`) pregunta al driver primero (ioctl de lectura
`0xC02869C9`, tipo 129); si el driver responde `0xE00002C7` (unsupported), rellena la respuesta desde
sus ivars; si responde bien, compara codificación (`+42866`), longitud (`+42820`) y step (`+42867`)
entre «FW» y «Family» y registra `"Parameter mismatch ..."` si difieren. Es el único mecanismo de
coherencia entre lo programado y lo que el driver dice tener.

## 6. Qué pasa si el driver rechaza o ignora

- **Rechazo del 129**: `setChannelSequenceUnconditional` registra
  `"Couldn't set channel sequence! ..."`, pone a cero la copia `+8` (el próximo intento re-emite) y
  llama a `updateBroadcastMI`. Además, tras toda emisión intentada — con éxito o con fallo, es el
  mismo bloque del epílogo — el calendario se reprograma con `setAWDLStateStop` +
  `setAWDLStateStart(estado, awdlDutyCycle)`. No reintenta en línea: el vigilante
  `monitorAWDLState` (`0xffffff80021ed724`, xref en `0xffffff80021edfcc`) llama a
  `recoverFromSetChanSeqFailure` (`0xffffff80021f3710`), que limpia `+16912` y relanza
  `setScheduleState` (reconstruye la secuencia y vuelve a emitir el 129) si hay AirPlay/RTG activo.
- **Rechazo del 132**: `programMasterChannels` registra `"Unable to set master channel ..."` y aborta
  sin emitir el 205. Fallo del 205: sólo registro.
- **Ignorar en silencio** (responder 0 sin aplicar): el manager da la secuencia por programada (copia
  `+8`, evento 195). La discrepancia sólo se manifiesta como trazas: `monitorTimeline` compara el
  canal y el `AwNumber` de cada evento 41 con la secuencia y registra discontinuidades sin
  resincronizar (`AWDL_PEERMANAGER_DRIVER_ABI.md` §2.3), y la comparación FW/Family del §5.4 sólo se
  ejecuta si alguien hace un `get`. Ninguna de las dos corrige el anuncio: **quien rinde el TLV 18 es
  el driver**, así que una secuencia ignorada se convierte en un anuncio mentiroso sin ningún log en
  el kernel que lo delate.

## 7. Mínimo para que el RTL8814 anuncie una secuencia honesta

La premisa del escenario es una radio que no puede seguir saltando de canal por sí sola (si el T9UH
bajo esta pila puede o quiere es cuestión abierta `?`); el contrato lo permite porque el render de
los TLVs es tarea del driver. Mínimo verificado contra el código:

1. **Aceptar y registrar los ioctls**: 129 (secuencia, 400 B según §5.1/`AWDL_PEERMANAGER_DRIVER_ABI.md`
   §3.7), 132/205 (canales sociales), 136 (presencia, `u32 ≤ 0x3F`), 118 (ext bytes, 20 B), 116
   (periodos; su éxito activa el gate `+42560` que desbloquea la máquina) y 117 (enable, único con
   revert). Responder 0 a todos salvo política contraria expresa.
2. **Contestar la lectura del 129** (`0xC02869C9`) con lo mismo que se aceptó por escritura; si se
   prefiere no implementar, devolver `0xE00002C7` y el manager usará su copia — la coherencia deja de
   poder verificarse desde fuera.
3. **Render honesto del TLV 18** a partir del 129 recibido: canal real de la radio en cada ranura que
   el driver vaya a atender, **canal 0 en las que no**, conservando codificación (si vale 3, el par
   {canal, clase operativa} debe corresponder al ancho realmente sintonizado; winawdl midió el coste
   de anunciar 40 MHz y estar en 20 MHz), step y fill. Remitir a los precedentes: el firmware
   Broadcom fuerza la ranura 0 a 6 si llega a cero; el kernel admite secuencias constantes (roam) y
   de dos ranuras (default).
4. **Modo de presencia del TLV 4 coherente con el TLV 18**: las ranuras con AW en el byte `pm`
   (`+30` del TLV 4) deben ser exactamente las que el TLV 18 anuncia con canal; el driver construye
   ambos TLVs, y los ext bytes salen del 118. Un canal anunciado en ranuras no atendidas pierde las
   tramas del par (owly AWDL-12).
5. **Eventos 41/42/43** alineados con el calendario propio (§5.3 y `AWDL_PEERMANAGER_DRIVER_ABI.md`
   §2): sin el 41 el manager no entra en ventana; las discrepancias de `AwNumber`/canal sólo producen
   logs, así que la honestidad del render no puede delegarse en la validación del kernel.
6. **No suprimir el 129 respondiendo 0 sin aplicarlo**: si la secuencia programada no puede
   ejecutarse, reescribirla en el render (punto 3) y no fingir aceptación; la única reacción del
   kernel a un fallo declarado es reconstruir y reprogramar (`recoverFromSetChanSeqFailure`), que es
   el camino deseable.

Con 1–6, la secuencia que el `IO80211AWDLPeerManager` «anuncia» (su copia `+42868`, el evento 195 y
los TLVs reales del aire) difiere sólo en las ranuras que el driver declara no servidas con canal 0 —
exactamente el patrón que las implementaciones de radio fija (owly, winawdl, ESP32Drop) usan para
interoperar sin saltar de canal.

## 8. Abiertos

- Semántica de las fuentes de canal `+17080/+17088`, `+17092/+17100`, `+440/+448`, `+452/+460`
  (tipos 5/6/12/15) y de los bits del par `+2500` del controlador `?`.
- Qué boot-args de `initWithInterface` (`awdl_allow_40`, `awdl_max_bw`, …) condicionan en la práctica
  las fuentes de la tabla de ranuras `?`.
- Condiciones exactas con que `enableAWDL` elige entre `AwdlFollowsInfra` (0) y `AwdlStateMachine`
  (1) al arrancar `?`.
- Si algún camino del kernel construye alguna vez el TLV 18 fuera del driver (no se ha encontrado
  constructor; el tipo 33 pref chan y el 12 data path state son los únicos TLVs de canal construidos
  en software) `?`.
- Unidad y semántica del byte de flags `0xA` de `_channel6Info` (bit 1) `?`.
