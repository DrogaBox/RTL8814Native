# La secuencia de canales AWDL con infraestructura asociada: el caso en2 en 153 (80 MHz, centro 155)

Qué canal de infraestructura aprende `IO80211AWDLPeerManager`, de qué petición del driver lo obtiene,
cómo lo mezcla con los canales sociales al construir las 16 ranuras que baja con el ioctl 129
(`setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`) y qué cambia con un par presente. El escenario de
referencia es el equipo de pruebas: `en2` asociado a un BSS de 5 GHz con primario 153, ancho 80 MHz,
centro 155, de modo que el bloque de 80 MHz 149–161 contiene el canal social 149
(`AWDL_WINDOW_TIMER.md` §8). Complementa `AWDL_CHANNEL_SEQUENCE_CONTROL.md` (máquina de la secuencia)
y `AWDL_CHANNEL_STRATEGY.md` (estrategia y campos); corrige dos puntos de ambos (§11).

Fuente: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0), copia de la IDB anotada
`~/re_cache/ida/boot_awdl.kc.i64` en `~/re_cache/ida/task_seq_infra/`, descompilación y desmontaje de
las funciones citadas, más un barrido global de llamadas `E8/E9` y de celdas de vtable para los
puntos declarados muertos. Las VA son de instrucción o dato de esa IDB. Los offsets de ivars del
gestor AWDL se refieren a `*(this+0x20)` y los del objeto base a `*(this+0x18)` (en adelante `pm`).
Lo no verificado se marca `?`.

## 1. Resumen

Con infraestructura en 153/80 y sin pares, el gestor baja con el 129 una secuencia de 16 ranuras
donde conviven **dos primarios de 20 MHz distintos del mismo bloque de 80 MHz**: el social 149 (con
flags `0x10`, 5 GHz a 20 MHz) en siete ranuras y el 153 de la infraestructura (con flags `0x410`,
5 GHz a 80 MHz) en hasta cuatro, más una ranura de 6 (flags `0x8`) y el resto a canal 0. El 149 no
se deriva del 153: es el valor por defecto de `initParamsToDefaults` validado contra la lista de
canales que el driver responde a la petición 207; el 153 es, verbatim, lo que el driver responde al
GET de canal (tipo 4, `getCHANNEL`). Las ranuras de infraestructura solo se rellenan si además hay al
menos un servicio AirPlay registrado (`addInfraToMinimumChannelSequence`); sin servicios, las ranuras
de infra van a canal 0. Con un par presente la secuencia propia se sustituye por la del par
(`followPeerChannelSequence`) y deja de depender de la infraestructura.

## 2. Objetos y estructuras

| Dato | Valor |
|---|---|
| `apple80211_channel` | 12 B `{u32 version; u32 channel; u32 flags}` |
| `apple80211_channel_data` | 16 B `{u32 version; apple80211_channel}` |
| Entrada de ranura (129 e ivar) | 12 B `{u32 version; u32 channel; u32 flags}` en `awdlIvars+0xA774` (+42868), flags en `+0xA77C` |
| `_zeroChanInfo` | `0xffffff8002488a88` = `{1, 0, 0}` (ranura no servida) |
| `_channel6Info` | `0xffffff80023aaff0` = `{1, 6, 0xA}` (0x2 = 20 MHz, 0x8 = 2,4 GHz) |
| Info maestra primaria | `awdlIvars+0x42A0/0x42A4/0x42A8` = `{version, canal, flags}` |
| Info maestra secundaria | `awdlIvars+0x42AC/0x42B0/0x42B4` |
| Canal de infraestructura en `pm` | versión `pm+0x9BC`, canal `pm+0x9C0` (+2496), flags `pm+0x9C4` (+2500) |
| Canales sociales crudos | `+0x4290` (2G, +17040), `+0x4294` (5G, +17044), `+0x4298` (5G alt, +17048) |
| Máscara de sociales | `+0x429C` (u16): bit0 = 6, bit1 = 44, bit2 = 149 (`selectChannelsToUseForAWDL`) |
| Estado de calendario | `awdlIvars+0x28E4` (+10468); estrategia en `awdlIvars+4` |

Flags de canal observados en 25G241: **0x8 = banda de 2,4 GHz, 0x10 = banda de 5 GHz, 0x2000 =
6 GHz** (construcción en `selectChannelsToUseForAWDL` `0xffffff80021dc303`: canal < 15 → 8; canal
36..183 → `16 * ((chan-36) < 0x82)`; y guardia de 6 GHz por ranura en `setChannelSequenceByState`,
byte `+0xA77D` bit `0x20` = bit `0x2000` del dword). El ancho lo codifican los bits que decodifica el
log de `setInfraChannelSpec` (`0xffffff80021fc626`): `0x800` = 160 MHz, `0x400` = 80 MHz, `0x4` =
40 MHz, resto 20 MHz (0x2). Ambas convenciones coinciden con las que usa la capa RTL:
`Source/AirLayer/RTLAirLayer.cpp:264` (`chanFlags`: banda 5G → 0x10, 2,4G → 0x8; ancho 80 → 0x400,
40 → 0x4, resto 0x2). Para en2 en 153/80 el canal queda `{1, 153, 0x410}`.

## 3. De dónde sale el canal de infraestructura: modelo de tiro

No hay ninguna ruta que empuje el canal al gestor: `getInfrastructureState` lo **consulta** al
driver cada vez que el gestor se entera de algo.

### 3.1 `getInfrastructureState` (`0xffffff80021da84a`–`0x21dae6d`)

Llamadores verificados: `interfaceStateChange` (`0xffffff80021d9030`) y `reportDataPathEvents`
(`0xffffff80021de938`). Antes de llamarla, `interfaceStateChange` guarda el par previo
(pm+0x9BC/0x9C4) para comparar (`0xffffff80021d9013`, `0x21d901a`).

Pasos verificados en la función:

1. `IO80211Controller::getInfraChannel(apple80211_channel_data *)` (`0xffffff800229520e`, llamada en
   `0x21da898`). El overload hace `safeMetaCast(*(ctrl+0x120)+0x188, IO80211SkywalkInterface)` y:
   - `IO80211Glue::sendIOUCToWcl(glue, 0xC02869C9, 4, out, 0x10, …)` — **petición 4 =
     `APPLE80211_IOC_CHANNEL`**, 16 B, por el IOUC a WCL del interface primario;
   - si falla (o `0xE00002C7`), fallback directo al puente `apple80211getCHANNEL(interface)`, que es
     el slot `getCHANNEL` del driver. En la pila RTL responde `RTLAirInterface::getCHANNEL`
     (`Source/AirLayer/RTLAirLayer.cpp:928`).
   El log de la familia lo llama por su nombre: `"Get of APPLE80211_IOC_CHANNEL failed"`.
2. Si el canal ≠ 0: `getPrimarySkywalkInterface` (celda +0xC80 del controlador, llamada en
   `0x21da980`) y `IO80211Controller::getBSSIDData(ctrl, iface, out)` (`0xffffff8002294e5a`,
   llamada en `0x21da98f`): `sendIOUCToWcl(0xC02869C9, 9, out, 0xC, …)` — **petición 9 =
   `APPLE80211_IOC_BSSID`** — con fallback `apple80211getBSSID`. BSSID a `pm+0x930`.
3. `IO80211Controller::getSSIDData` (`0xffffff8002294c0c`, llamada en `0x21da9e5`):
   `sendIOUCToWcl(0xC02869C9, 1, out, 0x28, …)` — **petición 1 = `APPLE80211_IOC_SSID`** — con
   fallback `apple80211getSSID`. Longitud a `pm+0x938` y bytes vía
   `IO80211PeerManager::setInfraSsidBytes` (`0xffffff800225ee3a`, llamada en `0x21daa78`).
4. `IO80211Controller::getBeaconPeriod(apple80211_beacon_period_data *)` (`0xffffff8002294f60`,
   llamada en `0x21dab95`; IOUC `0xC02869C9` tipo `0xA6` = 166, 8 B, fallback
   `apple80211getBEACON_PERIOD`) → `pm+0x54C`; `getDTIMInt` (`0xffffff800229501e`, llamada en
   `0x21dabb9`, fallback `apple80211getDTIM_INT`) → `pm+0x550` = dtim · beacon.
5. Comparación con lo almacenado: si cambia el canal **o cambian los bits de banda `0x2018`** del
   dword de flags (2,4/5/6 GHz), reprograma `pm+0x9BC` = `{version, canal}` nuevo y `pm+0x9C4` =
   flags nuevos, con log `"infra channel %u(0x%X) -> %u(0x%X)"`; guarda el canal para el
   restablecimiento en `awdlIvars+0x40F5` (bit) y `+0x40F8`. Devuelve 1 si hay infraestructura, 0 si
   no (`"lost infrastructure link"`).

En resumen: **el canal de infraestructura del gestor es exactamente lo que el driver responde al GET
de canal del interface primario**, con los flags sin modificar. Para el RTL8814 en 153/80:
`getCHANNEL` responde `{1, 1, 153, 0x410}` y el gestor almacena canal 153, flags `0x410`.

### 3.2 Quién dispara la consulta (eventos de enlace)

`interfaceStateChange(StateChangedSource)` (`0xffffff80021d8cc8`) es la ranura 55 (`+0x1B8`) de la
vtable de `IO80211AWDLPeerManager` (vptr `0xffffff800245a868`, celda `0xffffff800245aa20`). Los
nombres de los `source` están en `0xffffff800245d450`; los usados en esta cadena: 1 `BSSIDChanged`,
5 `Interface`, 6 `Reset`, 8 `Channel Switch Complete`, **9 `SetInfraChannel`**, 10 `Master Channel
Changed`, 11 `Secondary Master Channel Changed`.

- `reportDataPathEvents` (`0xffffff80021dd8f4`, el consumidor del pipe de eventos del driver) llama a
  `interfaceStateChange` con source **9** (`0x21de14c`, `0x21df49f`), **11** (`0x21de77c`), **5**
  (`0x21de7fb`) y **6** (`0x21df883`), y llama a `getInfrastructureState` directamente en
  `0x21de938`. Los sucesos de asociación/roam/cambio de BSS del driver llegan así.
- El resto de llamadores verificados de la ranura 55 son internos del gestor: `doEnable`
  (`0x21c53d6`), `disableAWDL` (`0x21c7696`), `setAwdlCtl` (`0x21d0124`),
  `exitAwdlSuspendedMode`, `addPeerToRealTimeGroup`, `updateLocalRequests`, `setAwdlSyncEnabled`,
  `setAwdlSocialTimeSlots`, `setAwdlMasterChannel` (source 10) y `setAwdlSecondaryMasterChannel`
  (source 11); `IO80211PeerManager::monitorCountryCode` también la invoca.

### 3.3 Entradas muertas en 25G241

Las cuatro funciones que «empujan» el canal de infraestructura existen con cuerpo completo pero no
tienen ni una llamada directa (barrido global de `E8/E9`) ni una celda de vtable que las apunte
(barrido de punteros crudos en todo el kernelcache; la vtable del gestor, materializada entre
`0xffffff800245a868` y la ranura 100, no las contiene):

| Función | VA | Lo que haría |
|---|---|---|
| `IO80211PeerManager::setInfraChannel(apple80211_channel *)` | `0xffffff8002254aee` | notifica source 9 (celda +440) y almacena canal/flags en pm+0x9C0/0x9C4 |
| `IO80211PeerManager::setInfraChannelInfo(apple80211_channel *)` | `0xffffff800225edec` | almacena pm+0x9BC (qword) y pm+0x9C4 |
| `IO80211AWDLPeerManager::setInfraChannelSpec(uint, apple80211_channel_data *)` | `0xffffff80021fc626` | si no había canal, lo toma del argumento o del GET de canal; decodifica el ancho en el log |
| `IO80211VirtualInterface::setInfraChannel(apple80211_channel *)` | `0xffffff8002330a0c` | notifica source 9 al gestor de la interfaz virtual y almacena |

Consecuencia: el refresco vivo del canal de infraestructura es exclusivamente el tiro de §3.1. El
detalle de `setInfraChannelSpec` conserva valor de especificación (su log fija la semántica de los
bits `0x800/0x400/0x4` del ancho), pero no forma parte del camino de 25G241.

## 4. Estrategia y buffers maestros (`+0x42A0..0x42B4`)

### 4.1 Elección de estrategia

- `configure` (`0xffffff80021c54ea`) fija, en este orden: `chanSwitchCapable` (byte `+0x3FA1`) = 1
  incondicional (`0xffffff80021c5b5c`; único escritor en todo el kernelcache), estrategia 1
  (`AwdlStateMachine`, razón 0, force) en `0x21c5b75` y `airplaySoloModeEnabled` (`+0xA6A4`) = 1 en
  `0x21c5b7e`.
- `interfaceStateChange` (rama `0x21d8f76`–`0x21d900a`): solo con `airplaySink` (`+0xA648`) ≠ 0 pide
  estrategia — 0 (`AwdlFollowsInfra`) si el bit 0 de `+0x3FA1` es 0; 1 si es 1 y además
  `airplaySoloModeEnabled ≠ 0` o `nonAirPlayServices` (`+0xA65C`) ≠ 0 (llamada en `0x21d900a`).
- `monitorAWDLState` (`0xffffff80021ed724`) vuelve a 0 (razón 5) en `0x21ee3ae` bajo las cinco
  condiciones de `AWDL_CHANNEL_STRATEGY.md` §6.
- Corrección respecto de `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.1: `enableAWDL`
  (`0xffffff80021dcf58`) **no** llama a `setChannelSequenceStrategy` (0 apariciones en su cuerpo);
  la única selección 0/1 vive en las dos funciones anteriores.

En una máquina sin AirPlay sink (el caso del RTL8814 con wifip2pd `?`) la estrategia queda en 1 de
forma permanente.

### 4.2 Validación de los canales sociales: `configureValidAWDLChannels` (`0xffffff80021caa40`)

Llamada desde `configure` (`0xffffff80021c5b9b`) y desde `interfaceStateChange`
(`0xffffff80021d9996`), justo antes de `updatePref6GChannel` (`0x21d9a2f`),
`findCommon6GChannel` (`0x21d9a37`) y `selectChannelsToUseForAWDL` (`0x21d9a3f`). Pasos:

1. País: `IO80211Controller::getCountryCode` (`0xffffff8002295416`) si no está cacheado en
   `pm+0x9A0/0x9A4`; flag de caché en `pm+0x6F8`.
2. Gate de doble banda: bit `0x20` de `*(ctrl+0x120)+0xB37` (`0xffffff80021cab1e`). Es el mapa de
   capacidades de la tarjeta: `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800229a2ca`) lee
   el bit `n` en `*(ctrl+0x120) + 0xB34 + n/8`, y `+0xB34` corresponde a `p[4]` del
   `apple80211_capability_data` que devuelve `getCARD_CAPABILITIES`; `+0xB37` bit `0x20` es,
   entonces, `p[7]` bit `0x20` (capacidad 29). `AppleBCMWLANCore::getCARD_CAPABILITIES` lo pone
   cuando su feature flag 28 está activo. Sin él, `configureValidAWDLChannels` escribe 0 en el qword
   `+0x4294` (sociales de 5 GHz y alternativo a 0) con el log `"Dual band not supported, so assuming
   2.4GHz is the basic and should be supported, initialize 5GHz channels to zero"`, y no hay canal
   social de 5 GHz: solo 6 en la secuencia.
3. Pide al driver la **lista de canales no-DFS de 40 MHz o más**: `ioctl_internal` con token
   `0xC02869C9` y petición **207** (`0xCF`; **379** si el byte `pm+0x72E` marca 6 GHz), respuesta de
   10016 B en `awdlIvars+0x69F4`, copiada a `+0x42D4`; `numChanSpecs` (u16) en `+0x42E0`. Si la
   petición falla, registra `"Unable to get non DFS 40MHz channels ret %d"` y **conserva** los valores
   de `initParamsToDefaults` (149 y 44); si la cuenta de canales es inválida, pone los dos a 0
   (`"cannot use 5G"`).
4. Valida con `isValidChannel(apple80211_channels_info *, chan, banda, máscara, uint *out)`
   (`0xffffff80021d1d4c`): exige banda 8 para 2,4 GHz y 16 para 5 GHz (36..183); rechaza 149 en
   países ETSI (tabla `ETSICountryList`, 39 entradas) y los canales ≥ 65 en el país `IL` (comparación
   de bytes 'I'/'L'); recorre la lista del driver y
   devuelve en `out` los bits de capacidad de la entrada: **`0x1` radarDFS, `0x2` passive,
   `0x4` indoorRestricted, `0x8` soporta 40 MHz, `0x10` soporta 80 MHz, `0x20` soporta 160 MHz,
   `0x100` CLM restringido**; con máscara ≠ 0 exige que todos los bits marcados estén presentes.
5. Con la lista: `+0x4290` se valida con banda 8 (si falla, log `"...not a valid channel defaulting
   to 6"`; no se ha visto store del 6 en 25G241 `?` — el valor ya es 6 por defecto); `+0x4294` con
   banda 16 y, si falla, se prueba 149: válido → `+0x4294 = 149` (`0x21caf85`), inválido → 0
   (`0x21caf91`, `"Disallowing Channel 149, Country Code"`); `+0x4298` con banda 16 y, si falla, se
   prueba 44: válido → `+0x4298 = 44` (`0x21cb097`), inválido → 0 (`0x21cb0a3`).
6. Si el social 5 GHz válido tiene capacidades `& 0x38` (40/80/160) y `+0xAA74 ≠ 1`
   (`dynSdbForceChanSeq`), fija la codificación del 129 a **1** en `+0xA772`.
7. Canal de infraestructura: si `pm+0x9C0 ≠ 0` y no es 6 GHz (`0x2000`), primero
   `isValidChannel(lista, canal, flags & 0x18, 1)` — el `1` exige radarDFS: si pasa,
   `+0x3ED8` = 1 (**canal DFS**, `0x21cb0fd`); si no, reintenta con `flags & 0x2018` y máscara 0: si
   también falla, **`+0x3ED9` = 0** (canal no permitido para AWDL, `0x21cb24e`). El estado por
   defecto lo pone el store de palabra `mov word [x+0x3ED8], 0x100` en `0x21cb0ad`: bit DFS a 0 y
   **bit «permitido» (`+0x3ED9`) a 1**.

Los valores de partida los fija `initParamsToDefaults` (`0xffffff80021c7a40`): qword
`0x9500000006` en `0x21c7ae4` (es decir, `+0x4290 = 6`, `+0x4294 = 149`) y `+0x4298 = 44` en
`0x21c7aeb`. Para un país no-ETSI con driver dual-band, en 153/80 no se modifica nada: sociales
**6 / 149 / 44**, y el 153 queda permitido y no-DFS.

### 4.3 `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`): los buffers maestros

Llamada desde `interfaceStateChange` (`0x21d9a3f`) tras `updatePref6GChannel` (`0x21d9a2f`) y
`findCommon6GChannel` (`0x21d9a37`):

- Info maestra primaria `+0x42A0/0x42A4/0x42A8` = `{1, (u8)+0x4290, banda}` — con los valores por
  defecto, `{1, 6, 0x8}`.
- Info maestra secundaria `+0x42AC/0x42B0/0x42B4` = `{1, +0x4294 (o +0x4298 si aquella es 0), banda}`,
  o `_zeroChanInfo` si ambas son 0 — en el escenario, `{1, 149, 0x10}`.
- Máscara `+0x429C`: bit0 (canal 6) siempre que haya maestro; bit1 si la secundaria es 44; bit2 si
  `+0x4294 == 149`. En el escenario: `0x5`.
- Las infos maestras llevan **solo la banda, sin bit de ancho**: el social se anuncia a 20 MHz.
- Rearma el canal AWDL actual `+0x1A0/0x1A8` a la secundaria si el actual ya no coincide
  (`0x21dc4a0`–`0x21dc4f5`): semilla `{1, 149, 0x10}`.

`setAwdlMasterChannel` (`0xffffff800221d4de`) y `setAwdlSecondaryMasterChannel`
(`0xffffff800221d314`) permiten a userspace reescribir `+0x4290`/`+0x4294`; con `awdlIvars+0x300 == 1`
reenvían además el 132/205 al driver (`apple80211RouteInternal(0x802869C8, 132, …)`) y solo adoptan
el valor si el driver acepta.

### 4.4 Modo `AwdlFollowsInfra` (estrategia 0)

Dos sitios copian el canal de infraestructura **encima** de la info maestra primaria:

- `monitorAWDLState`, tras la transición a 0 (`0x21ee3b3`–`0x21ee454`): con canal ≠ 0,
  `infraAllowedForNonRealtimeCases()` cierto, sin descubrimiento asistido (`+0xFA3A` bit 0) y sin
  6 GHz, copia pm+0x9BC → `+0x42A0`, pm+0x9C4 → `+0x42A8`, `_zeroChanInfo` → `+0x42AC`, 0 →
  `+0x42B4`; llama `programMasterChannels` (`0x21ee433`) y `setScheduleState(estado vigente,
  0x14, true)`. Si algo falla: `"Disabling AWDL in FollowsInfra mode because …"` y `disableAWDL`.
- `interfaceStateChange`, tras `selectChannelsToUseForAWDL`, si la estrategia es 0 (bloque
  `0x21d9a44`–`0x21d9aa8`): la misma copia.

Efecto: en modo 0 el «canal social» pasa a ser el propio canal de infraestructura (153/80) y el
132 lo programa así; los tipos 2/3 de `setChannelSequenceByState` heredan ese valor. Este modo exige
el escenario AirPlay de §4.1, de modo que no es el camino por defecto del RTL8814.

## 5. Construcción de las 16 ranuras

### 5.1 El estado de calendario que se aplica

En `interfaceStateChange`, tras la consulta de infraestructura y `selectChannelsToUseForAWDL`:

1. Envía los sync params si falta (ioctl 116, token `0x74802869C8`, montado en `0x21da0df`); su éxito
   activa el gate `+0xA640` que `setScheduleState`/`setChannelSequence` exigen.
2. Reprograma 132/205 si el canal cambió o `channelConfigured` (byte `pm+0x548`) está a 0:
   `programMasterChannels` en `0x21da04c`, y deja `channelConfigured = (132 OK) && (plantilla OK)`.
3. Elige el estado: con estrategia 1 y sync inactivo (`+0x3EE0` bit 1 a 0) el estado es
   `5 * dataIntensiveServiceActive()` (llamada en `0x21da198`), y `dataIntensiveServiceActive`
   (`0xffffff80021dcb46`) devuelve **0 constante** en 25G241 → **estado 0, Discovery**. Con
   estrategia 0: estado 3 (Idle), o 5 si el source es 2 (Mdns). Con sync activo conserva el estado
   vigente `+0x28E4`.
4. Con estado 5 llama `setDataMode(8, 1)` (`0x21da200`); en los demás casos
   `setScheduleState(estado, 8, true)` (`0x21da214`), que para las estrategias 1/0 reconstruye la
   secuencia vía `setChannelSequenceByState(estado)` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.2; la
   llamada interior está en `setScheduleState`, línea del byState en 996 del decompilado).
5. Puerta de habilitación: con estrategia 1 exige LLA configurada o source 3
   (`isLLAConfigured` en `0x21d9b2c`) y ausencia de suspensión; con estrategia 0 exige además canal
   de infraestructura ≠ 0 (`0x21d9c09`) e `infraAllowedForNonRealtimeCases()` (`0x21d9c19`). Después
   llama `enableAWDL` (`0x21da336`), que lee `awdl_cap` (ioctl GET tipo 341, `0x155C02869C9`),
   emite el 129 por primera vez (`setChannelSequenceUnconditional(1)`) y activa con el 117
   (`0x75802869C8`).

### 5.2 Fuentes por tipo de ranura (verificado en el `switch` de `setChannelSequenceByState`)

`setChannelSequenceByState` (`0xffffff80021f53aa`) lee `__ZL20_channelSeqSlotTypes[estado][ranura]`
(`0xffffff80023aa1f0`, una dword por ranura, filas de 64 B) y aplica:

| Tipo | Fuente |
|---:|---|
| 0 | no reescribe la ranura |
| 1 | `_zeroChanInfo` — canal 0 |
| 2 | info maestra primaria (`+0x42A0`, flags en `+0x42A8`) |
| 3 | secundaria si `+0x42B0 ≠ 0`, si no primaria |
| 4 | canal AWDL actual `+0x1A0/0x1A8` |
| 7 | canal de infraestructura (pm+0x9BC/0x9C4) **verbatim** si `addInfraToMinimumChannelSequence()` lo permite; si no, canal 0 |
| 8 | canal de infraestructura si `airplaySink` (`+0xA648`) ≠ 0; si no, canal 0 |
| 9 | canal de infraestructura si `infraAllowedForNonRealtimeCases()`; si no: ranura 0 → infra si `+0x3ED9` y hay canal, si no canal 6 (`0x600000001`, flags 10); resto → canal 6 solo con SDB activo y estados concretos, si no canal 0 |
| 5/6, 10–20 | fuentes auxiliares (pares de presencia, RT, DFS, mezclas) — `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.4 |

Tras cada ranura corre la guardia 6 GHz (bit `0x2000` de los flags de la entrada y
`is6GChannelALlowedForP2P`): inoperante para 149 y 153.

**`addInfraToMinimumChannelSequence`** (`0xffffff80021f5ef6`–`0x21f5f8e`), la condición del tipo 7,
devuelve la conjunción de tres cosas verificadas en el desmontaje:

1. `infraAllowedForNonRealtimeCases()` (`0xffffff80021dc540`, llamada en `0x21f5f57`): canal de
   infraestructura ≠ 0 **y** bit `+0x3ED9` a 1 (lo pone `configureValidAWDLChannels`, §4.2.7);
   si el canal es DFS (`+0x3ED8`) exige además un par AirDrop/RT activo.
2. Flags de infraestructura sin `0x2000` (no 6 GHz) — `and [pm+0x9C4], 0x2000` en `0x21f5f4d`.
3. **Recuento de servicios AirPlay ≠ 0**: `IO80211CommandQueue::lockMap` con
   `_countAirPlayServices` (`0xffffff80021e0cee`) como callback y acumulador local a cero
   (`0x21f5f1e`–`0x21f5f69`); el callback incrementa el acumulador por descriptor de petición de
   servicio de tipo 0x21/0x0C, con cadena `"ip6"`, sin `"arpa"` y con `"airplay"` o `"raop"`.

Sin servicios AirPlay registrados, el tipo 7 queda a canal 0 aunque la infraestructura esté
asociada y permitida. (Los servicios nonAirPlay de wifip2pd no cuentan para este recuento.)

### 5.3 El 129

`setChannelSequenceUnconditional` (`0xffffff80021cb69a`) compone 400 B: `+0` versión 1, `+4` flags
de `+0xAA74` con los bits SDB (`0x10` ON / `0x20` OFF según `dynSdbForceChanSeq` y el estado),
`+5` n (16), `+6` codificación (`+0xA772`, 0 u 1 según §4.2.6), `+7` step, `+8` dup, `+9` fill, y
las 16 entradas de 12 B copiadas de `+0xA774`. Supresión por `memcmp` de 0x190 contra la copia en
`ivar+8`; `processAWDLDFSChannelSequence` antes; emisión con `ioctl_internal` y token
`0x81802869C8` (petición 129). El driver es quien ejecuta el dwell y quien rinde los TLVs 4 y 18
del aire (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.2). El 132 (`0x84802869C8`) lleva el canal de
`+0x42A4` y el 205 (`0xCD802869C8`) el de `+0x42B0` (`programMasterChannels`,
`0xffffff80021d494a`).

## 6. Respuesta concreta: en2 en 153/80, sin pares

Entradas verificadas para el escenario: canal de infraestructura `{1, 153, 0x410}` (§3.1 y
`chanFlags` de la capa); sociales 6/149/44 sin cambios (§4.2); infos maestras `{1, 6, 0x8}` y
`{1, 149, 0x10}` (§4.3); estrategia 1 (AwdlStateMachine, §4.1); estado Discovery (0), §5.1; fila de
Discovery leída hoy en la tabla: `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7`.

| Ranura | Tipo | Canal | Flags | Nota |
|---:|---:|---:|---:|---|
| 0–3 | 3 | **149** | `0x10` | secundaria (`+0x42B0` = 149) |
| 4–5 | 1 | **0** | `0x0` | no servida |
| 6–7 | 7 | **153** `0x410` **o 0** | — | infra si hay ≥1 servicio AirPlay registrado (§5.2); si no, canal 0 |
| 8 | 2 | **6** | `0x8` | maestra primaria |
| 9 | 4 | **149** | `0x10` | canal AWDL actual (semilla; evolución posterior `?`) |
| 10 | 3 | **149** | `0x10` | secundaria |
| 11 | 4 | **149** | `0x10` | canal AWDL actual |
| 12–13 | 1 | **0** | `0x0` | no servida |
| 14–15 | 7 | **153** `0x410` **o 0** | — | igual que 6–7 |

Totales: sin servicios AirPlay → 149 en 7 ranuras (0–3, 9, 10, 11), 6 en 1 (ranura 8), canal 0 en 8
(4, 5, 6, 7, 12, 13, 14, 15). Con un servicio AirPlay registrado → 149 en 7, **153 en 4** (6, 7, 14,
15), 6 en 1, canal 0 en 4 (4, 5, 12, 13). El byte de flags global del 129 (`+4`) depende del estado
SDB (0x10/0x20) y la codificación (`+6`) vale 1 si el driver reportó 149 con 40/80/160 MHz en la
lista 207 (0 si no).

Variantes que cambian la fila: país ETSI → `+0x4294 = 0`, las ranuras de tipo 3 caen al 6 y la
máscara pierde el bit 2; driver sin el bit de doble banda (`+0xB37` bit `0x20`) → ídem. **Este es el
caso actual de la capa RTL**: `RTLAirController::getCARD_CAPABILITIES`
(`Source/AirLayer/RTLAirLayer.cpp`) deja `p[7] = 0x05`, sin el bit `0x20`, así que hoy el gestor no
tiene social de 5 GHz y las ranuras de tipo 3 resuelven al 6. Concuerda con el aparcamiento en el 6
observado en hardware el 2026-10-30, aunque el primer 129 registrado (6 en las ranuras 0 y 8, el resto
a 0) no coincide con ninguna fila de la tabla y parece ser la secuencia inicial, previa a la
reconstrucción por estado `?`. El log de cada cambio del 129 (`83977cf`) lo aclarará; con
`airplaySink ≠ 0` y las condiciones de `monitorAWDLState`, el modo 0 sustituye **también** las
ranuras sociales por 153/0x410 (§4.4) y el 132 programa 153.

## 7. ¿149 o 153 dentro del mismo bloque de 80 MHz?

Sí conviven: el gestor no elige uno de los dos, los usa para papeles distintos y no deriva nunca uno
del otro. El 149 es un **social**: valor por defecto de `initParamsToDefaults`, validado contra la
lista de canales del driver y el país, y anunciado con flags de banda sin ancho (`0x10`, 20 MHz).
El 153 es la **infraestructura**: copia literal del GET de canal de `en2` con sus flags `0x410`
(80 MHz), válida solo en las ranuras del tipo 7 y solo con servicios AirPlay. En el mismo bloque de
80 MHz (centro 155) ambos son primarios de 20 MHz diferentes: una radio sintonizada con primario 153
no decodifica las tramas AWDL de 20 MHz con primario 149 `?` (comportamiento estándar 802.11, no
medido en el RTL8814AU; `AWDL_WINDOW_TIMER.md` §8). Por eso una radio fija en 153 solo cubre las
ranuras de infraestructura (si las hay) y pierde las siete de 149; con el router en primario 149 las
dos poblaciones coinciden.

## 8. Qué cambia con un par presente

- Cualquier TLV 18/32/4 de un par llega por `IO80211AWDLPeer::updateAwdlChanSeq`
  (`0xffffff80021ab7e4`, llamada desde `actionFrameReport` `0x21b1520` y
  `doMinimalAFProcessing` `0x21b45e8`) a `followPeerChannelSequence`
  (`0xffffff80021f6e74`): copia las 16 entradas de 12 B del par (bloque `peer+920`, canal en
  `+924`) a `+0xA774`, guarda la anterior en `+0xA8F4`, fija el estado 9 (RealTimeData) y emite el
  129 forzado. La secuencia propia deja de depender de la infraestructura.
- `setScheduleState` deriva al modo seguir cuando hay par marcado (`+0x3F78` bit 0 o el par de
  `+0x3F60` con el bit de `peer+0x681`): llama `followPeerChannelSequence` y no toca la tabla.
- El par iPhone de las capturas anuncia `{44,128}` en sus ranuras de 5 GHz y `{6,81}` en una de
  2,4 GHz (`AWDL_AF_VS_IPHONE.md` §3): clases operativas, no flags — el render en el aire es del
  driver que emite, no del gestor.
- Los estados 5/9 (Data/RealTimeData) no reescriben la tabla (fila a 0): el contenido queda el que
  dejó el seguimiento del par (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.3).

## 9. Bits de capacidad del driver que alteran el resultado

| Lectura del driver | Efecto sobre la secuencia |
|---|---|
| `getCHANNEL` (petición 4, §3.1) | canal y flags de las ranuras de infraestructura y del compare de banda `0x2018`; es la única fuente del 153 |
| Lista de canales (petición 207/379, §4.2) | valida 149/44 por país; DFS del 153 (`+0x3ED8`); permiso AWDL (`+0x3ED9`); sin 149 válido desaparece de los sociales; bits `0x38` → codificación 1 |
| `awdl_cap` (petición 341, `enableAWDL`) | no altera ranuras; activa SEC_PAYLOAD/CCA_STATS/PSF_DWELL_MI (`+0x40C0..`) |
| `getRsdbCaps` (en `configure`) | estado SDB (`+0x4084/+0x4088/+0x4064`); no cambia ranuras, sí el byte `+4` del 129 y los estados 9/10 |
| Doble banda (`*(ctrl+0x120)+0xB37` bit `0x20` = `p[7]` bit `0x20` de `getCARD_CAPABILITIES`) | sin ella, `+0x4294 = 0`: secuencia solo con 6 y canal 0 |
| Bytes de capacidad de la interfaz primaria (`+0xB3F` bit 7 = 6E en `configure`, bit 6 en `enableAWDL`; `+0xB47` bit `0x20` Limited SDB) | no tocan ranuras; ajustan la lista 207/379 y temporizadores `?` |
| `chanSwitchCapable` (`+0x3FA1`) | no viene del driver: lo fija `configure` a 1; condiciona la rama de estrategia de `interfaceStateChange` y el bit `0x400` del TLV de capacidades |
| `awdlIvars+0x300` = 1 | `setAwdlMasterChannel`/`setAwdlSecondaryMasterChannel` reenvían 132/205 al driver y adoptan su respuesta |

## 10. Qué debe hacer un driver de una sola radio con esa secuencia

1. **`getCHANNEL` honesto** (petición 4): canal primario real y flags correctos
   (`0x10 | 0x400` para 153/80). Es lo único que el gestor sabe de la infraestructura; un flags
   erróneo rompe la detección de cambios de banda (`0x2018`) y la validación del §4.2.7.
2. **Capacidad de doble banda**: `p[7]` bit `0x20` en `getCARD_CAPABILITIES` (§4.2.2). Sin ella no
   hay social de 5 GHz aunque la radio lo soporte; en la capa RTL falta hoy.
3. **Lista 207 honesta**: si el driver no declara 149 válido, el gestor retira el social de 5 GHz
   (`+0x4294 = 0`) y la presencia 5 GHz se reduce a las ranuras de infraestructura. Debe incluir el
   bloque del canal de infraestructura con sus bits de soporte de ancho si se quiere la
   codificación con clase operativa.
4. **Aceptar 129/132/205 y rendir el TLV 18 por ranura**: canal real de la radio en las ranuras
   servidas, canal 0 en el resto, clase operativa derivada de los flags de cada entrada (el render
   actual de la capa fija `{ch,128}`/`{ch,81}` — `Source/AirLayer/AWDLInterface.cpp`, `awdlOpClass`/`putChanSeq`).
   La entrada de infraestructura (153, `0x410`) y la social (149, `0x10`) son primarios distintos:
   si la radio no salta de canal, anunciar 0 en las ranuras que no atiende.
5. **No fingir aceptación** del 129 sin aplicarlo: quien valida es el driver; el kernel solo
   reconstruye y reprograma si se declara el fallo (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §6–7).
6. **Eventos 41/43** solo en las ranuras servidas, con el canal de la entrada del 129
   (`AWDL_WINDOW_TIMER.md` §5).
7. Para convivir sin saltos con infraestructura en 153: convencer al gestor de que 149 no es
   atendible no basta (lo seguirá anunciando); la vía limpia es el router con primario 149, o
   aceptar la pérdida de las siete ranuras sociales de 5 GHz.

## 11. Correcciones a documentos previos

- `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.1: `enableAWDL` no elige estrategia (§4.1); su supuesto
  «elige 0 o 1 según `+16289` y los contadores» corresponde a la rama de `interfaceStateChange`.
- Ídem §3.4, tipo 7: la condición completa es la triple conjunción de §5.2 (el recuento de
  servicios AirPlay y el rechazo de 6 GHz no estaban documentados), y el tipo 8 depende de
  `airplaySink` (`+0xA648`), no de un «contador de servicios».
- Ídem §4, fila «Canales sociales»: los campos crudos son `+0x4290` (maestro), `+0x4294`
  (secundario) y `+0x4298` (alternativo); la máscara de `+0x429C` no es `bit0=6, bit1=44, bit2=149`
  tal cual, sino: bit1 solo si la secundaria elegida es 44 y bit2 solo si `+0x4294 == 149`.
- `AWDL_CHANNEL_STRATEGY.md` §8.1: con estrategia 0, `interfaceStateChange` sí permite habilitar
  AWDL cuando hay infraestructura y está permitida (gate `0x21d9bc2` → `0x21d9c01` →
  `infraAllowedForNonRealtimeCases`); lo que aborta es la habilitación sin infraestructura
  (`"waiting briefly before turning off AWDL"`).
- La semántica de los flags: `0x8`/`0x10`/`0x2000` son las bandas 2,4/5/6 GHz (no bits de ancho); el
  ancho vive en `0x2/0x4/0x400/0x800` (20/40/80/160 MHz), según `setInfraChannelSpec`,
  `isValidChannel` y `initParamsToDefaults`.

## 12. Abiertos

- Evolución posterior del canal AWDL actual `+0x1A0/0x1A8` (tipo 4) tras los eventos 41/43 `?`.
- Si algún escritor repone `+0x4290 = 6` al fallar la validación (el log promete «defaulting to 6»
  pero no se ha visto el store; el valor por defecto ya es 6) `?`.
- Bytes de capacidad `+0xB3F` (bit 6 y 6E) y `+0xB47` (bit 0x20, Limited SDB) leídos en `configure`:
  son `p[15]` y `p[23]` de `getCARD_CAPABILITIES` por el mismo mapa del §4.2.2; su efecto no se ha
  seguido `?`.
- Tipo exacto del IOUC de `getDTIMInt` (fallback `apple80211getDTIM_INT` verificado; el tipo
  esperado es 167 `?`).
- Comportamiento real con wifip2pd: qué servicios registra el daemon en el mapa de peticiones
  (`_countAirPlayServices` solo cuenta `"airplay"`/`"raop"`) y si algún camino activa
  `airplaySink` en esta pila `?`.
