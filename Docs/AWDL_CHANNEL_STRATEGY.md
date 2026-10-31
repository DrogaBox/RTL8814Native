# La estrategia de secuencia de canales del `IO80211AWDLPeerManager`

Todo lo citado está verificado en el kernelcache de 26.7.1 (25G241, x86_64); cada VA es la
instrucción exacta de la base anotada. Este documento nombra los cuatro campos que gobiernan
`ivars+4` (la pregunta abierta de `AWDL_PRESENCE_PREREQS.md` §1.5) y cierra las dos condiciones
locales que quedaban en `?`: las de `monitorAWDLState` antes de la vuelta a `AwdlFollowsInfra`
y la rama de `"Transitioning to Solo mode"` de `interfaceStateChange`.

Los desplazamientos `ivars+…` se refieren al bloque de ivars del peer manager, `*(this+0x20)`.

## 1. `ivars+4` = `channelSequenceStrategy`

Recapitulación de `AWDL_PRESENCE_PREREQS.md` §1.5 con el mecanismo completo.

`IO80211AWDLPeerManager::setChannelSequenceStrategy(channelSequenceStrategy,
channelSequenceStrategyReasons, bool)` (`0xffffff80021ca958`–`0x21caa40`):

- Lee el valor actual (`0xffffff80021ca96e`) e imprime siempre
  `"setChannelSequenceStrategy"` línea `0x28e1` con
  `"set channelSequenceStrategy %s => %s (reason %d) force=%d"` (`0xffffff80023d6d32`,
  llamada en `0xffffff80021ca99f`); los nombres salen de la tabla `0xffffff800245b6d0`
  (`AwdlFollowsInfra` 0, `AwdlStateMachine` 1, `AwdlFixed` 2, `AwdlUserConfigured` 3,
  verificados en `0xffffff800245b6d0`..`0x6e8`), con `"unknown"` si el índice pasa de 3
  (`0xffffff80021ca971`). La misma tabla la usan el log de entrada de `interfaceStateChange`
  (`0xffffff80021d8dca`), un log posterior de la misma función (`0xffffff80021d9ed4`),
  `setChannelSequenceUnconditional` (`0xffffff80021cbb37`), `setAwdlCtl` (`0xffffff80021cffe2`),
  `setScheduleState` (`0xffffff80021d2d06`) y `printState` (`0xffffff80022226fc`); el ayudante
  `StringForAwdlStrategy(channelSequenceStrategy)` (`0xffffff80021a8b88`) usa una copia en
  `0xffffff8002459c00`. `printDataPath` imprime el valor numérico:
  `"... %u _channelSequenceStrategy %u"` (`0xffffff80023e794a`, llamada en `0xffffff8002223b80`).
- Solo escribe si `force` o si el valor actual no es ya el pedido ni 3
  (`0xffffff80021ca9d5`–`0x21ca9ec`): `AwdlUserConfigured` es pegajoso sin `force`.
- Si `dword ivars+0x4008 == 8` (`0xffffff80021ca9ee`; `0x4008` es la clase de dispositivo según
  `AWDL_CAPS_ADVERTISED.md`, que la compara con 2 en `0xffffff800221445a` — el significado del 8 `?`),
  el tránsito hacia o desde `AwdlFollowsInfra` ajusta el mapa MI con `adjustMiBitmap(uint, bool, uint)`
  (`0xffffff80021caa1f`–`0x21caa2d`): entrar a 0 → máscara `0x05050505` (`esi` en `0xffffff80021caa02`),
  salir de 0 → máscara `0xffffffff` (`esi` en `0xffffff80021caa16`).
- Almacena en `0xffffff80021caa32`.

Escritores completos (el barrido de `[reg+4]` con base en el bloque de ivars solo añade lectores;
la escritura pasa por `setChannelSequenceStrategy` o es directa):

| Sitio | VA | Valor (reason, force) |
|---|---|---|
| `initWithInterface` (por defecto) | `0xffffff80021c15d3` (vía §1.5) | 1 |
| `initWithInterface` con `awdl_oper_mode=1` | `0xffffff80021c16c8` | 3, directo |
| `initChannelSequenceStrategy(strategy)` | `0xffffff80021c2d04` | argumento, directo |
| `setAwdlManualMode` | `0xffffff80021c2d12` | 3, directo |
| `configure` | `0xffffff80021c5b75` | 1 (0, sí) |
| `setAwdlCtl`, clave `"channelSequenceStrategy"` | `0xffffff80021cffba` | valor del ctl (3, sí) |
| `setAwdlChannelSequence` | `0xffffff800221d150` | 3 (3, sí) |
| `setAwdlAutoMode` | `0xffffff800221e976` | 1 (6, sí) |
| `setAwdlOperatingMode`, modo 1 | `0xffffff800221ec89` | 3, directo |
| `setAwdlOperatingMode`, otro modo | `0xffffff800221ecbc` | 1 (6, sí) |
| `interfaceStateChange` | `0xffffff80021d900a` | 0 o 1 (4, no) — §7 |
| `monitorAWDLState` | `0xffffff80021ee3ae` | 0 (5, no) — §6 |

Lectores del valor (`[reg+4]` exacto con base ivars): `setChannelSequenceUnconditional`
(`0xffffff80021cbb28`), `setAwdlCtl` para el log (`0xffffff80021cffd4`), `setScheduleState`
(`0xffffff80021d28a2`, `0x21d2cf5`, `0x21d2e34`, `0x21d2f8d`, `0x21d30e7`), `getAwdlCtl`
(`0xffffff80021d5491`, `0x21d6317`, `0x21d633d`), la entrada de `interfaceStateChange`
(`0xffffff80021d8dc0`, nombre en el log), la rama de estrategia (`0xffffff80021d8fa7`),
la puerta de `enableAWDL` (`0xffffff80021d9bc2`: con estrategia 0 no se activa) y la
recalibración de canales en modo `AwdlFollowsInfra` (`0xffffff80021d9a48`, `jnz` con el valor),
dos logs posteriores (`0xffffff80021d9ec0`, `0x21da179`), `sanityCheckAWStart`
(`0xffffff80021e2a6e`, compara con 1), `monitorAWDLState` (`0xffffff80021ee330` y, revisado a mano,
`0x21ee4aa`: con 0 no toma la rama de activación), `exitQuietMode` (`0xffffff80021fae4e`,
compara con 0),
`updateLocalRequests` (`0xffffff8002212c9d`), `setScanningState` (`0xffffff8002219615`),
`usePiggybackScan` (`0xffffff8002219914`), `getAwdlChannelSequence` (`0xffffff800221faaa`),
`printState` (`0xffffff80022226f2`) y `printDataPath` (`0xffffff8002223b78`).
El barrido lineal usa seguimiento de registro por función y pierde los usos cuyo camino pasa
por una llamada intercalada (p. ej. `0x21ee4aa` se recuperó a mano del volcado); el conjunto de
escritores no depende de ese límite porque el barrido de los cuatro desplazamientos grandes es
global y completo.

## 2. `ivars+0xA648` = `airplaySink` (dword; copia de capacidad en `+0xA64C`)

IO80211Family nombra el campo: `setAwdlCtl` imprime `"airplaySink : %u -> %u"`
(`0xffffff80023d0f35`, llamada en `0xffffff80021d06cd`) y
`"User configured airplaySink %u -> %u"` (`0xffffff80023d1605`, llamada en `0xffffff80021d06f4`);
`getAwdlCtl` lo rinde como `"airplaySink = %u"` (`0xffffff80021d69e4`). Otros logs lo citan:
`"Restored, airplaySink:%d airplaySinkCap:%d"` (`0xffffff80023ed11b`, en `restoreAirPlaySink`
`0xffffff8002208008`), `"AirPlay/Sidecar sender, disbale AirPlay sink. airplaySink:%d
airplaySinkCap:%d"` (`0xffffff80023ed162`, en `disableAirPlaySink` `0xffffff8002201e2b`),
`"sidecar starts. Turn off AirPlay sink support (airplaySink:%d airplaySinkCap:%d)"`
(`0xffffff80023dbaed`, en `setSidecarChanseq` `0xffffff800222d30d`), `"%s _airplaySink:%u"`
(`0xffffff80023db952`), `"ivars->_airplaying:%u _airplaySink:%u, is24GhzChanSeq:%u
coexPolicyEnable:%u"` (`0xffffff80023dafe3`), `"%s UltraLowLatencyRealTime -
airdropServiceCount=%d numAirPlayServices=%d airplaySink=%d"` (`0xffffff80023d76db`, llamada en
`0xffffff80021d332a` de `setScheduleState`) y `"dualband %u airplaySink %u ..."`
(`0xffffff80023c8b1e`).

Semántica observada: el Mac actúa como receptor AirPlay (sink) sobre la interfaz AWDL; el getter
`isAirPlaySink()` (`0xffffff8002234f04`) devuelve `dword != 0` (`cmp` en `0xffffff8002234f0c`,
`setnz al` en `0xffffff8002234f13`). `+0xA64C` es `airplaySinkCap`, el valor
configurado por el usuario que sobrevive a los apagados temporales.

Escritores:

| Sitio | VA | Operación |
|---|---|---|
| `setAwdlCtl`, clave `"airplaySink"` (petición 162) | `0xffffff80021d06bf` | `[0xA648] = valor`; después `[0xA64C] = valor` (`0x21d071e`) y `[0xA648] = valor` otra vez (`0x21d0724`) |
| `disableAirPlaySink` | `0xffffff8002201e18` | `[0xA648] = 0` si no era 0; `updateBroadcastMI(0xF, false, true)` (`0x2201e68`) |
| `restoreAirPlaySink` | `0xffffff8002207ffd` | `[0xA648] = [0xA64C]`; `updateBroadcastMI(0xF, false, cambió)` (`0x2208042`) |
| `setSidecarChanseq` (arranque de sidecar con bit 0 de `+0x3F33` a 0) | `0xffffff800222d490` | `[0xA648] = 1` |

El par disable/restore lo usa `setSidecarChanseq` (`0xffffff800222d33e` llama a
`disableAirPlaySink` cuando el bit 0 de `+0x3F33` ya está a 1, con el log
`"sidecar starts. Turn off AirPlay sink support"`; al parar el sidecar llama a
`restoreAirPlaySink` dos veces, `0xffffff800222d436` y `0x222d43e`).

Lectores (barrido global; función — VA — papel):

- `interfaceStateChange` `0xffffff80021d8f76` — puerta de toda la decisión de estrategia (§7).
- `monitorAWDLState` `0xffffff80021ee25e` — primera condición de la vuelta a `AwdlFollowsInfra` (§6).
- `evaluateQuietMode` `0xffffff80021da69a` — si es ≠ 0 pone `ivars+0xA66C = 0` (sin modo silencioso).
- `configure` `0xffffff80021c62d7` — junto a `_maxPeerCacheSize == 4` fuerza solo (§4).
- `enableAWDL` `0xffffff80021dd52b` — si es 0 llama a `setLogPolicy(1)` (`0x21dd537`).
- `setScheduleState` `0xffffff80021d3318` (log), `0x21d3645`, `0x21d36d4`, `0x21d3719`,
  `0x21d37c6` — con 0 sustituyen el presupuesto fijo `0x6E` (110) por `byte ivars+0x4288 * 110`.
- `reevaluateRTGConfiguration` `0xffffff80021d4d19` — mismo patrón del presupuesto `0x6E`.
- `monitorElectionMetric` `0xffffff80021ed552` — elige `setElectionMetricForAirplaySink` si ≠ 0.
- `processUnicastAWSF` `0xffffff80021baa59`, `0x21bafb6`, `0x21bc62b` — gates de
  `realTimeCannotUseInfra` y del registro de tráfico del par.
- `handleUmiTimer` `0xffffff80021c3526`, `jumpStartDFSProxy` `0xffffff80021c3d0a` — gates de UMI
  y del arranque del proxy DFS (con airplaySink ≠ 0 el jumpstart sigue; con 0 imprime el error
  `"ERROR: DFSProxy Jumpstart reques…"`, llamada en `0xffffff80021c3d13`).
- `determineAirPlaySoloMode` `0xffffff80021d8041`, `setAirPlayPeer` `0xffffff80021fc79c`,
  `calculateDataChannelsForAirplaySolo` `0xffffff80021f9e75`,
  `calculateDataChannelsForAirplaySoloDFS` `0xffffff80021f9062` y `0x21f90a6` —
  cálculo de canales de datos AirPlay.
- `setChannelSequenceByState` `0xffffff80021f5930` — en el caso 8 elige el canal de la
  infraestructura (`[this+0x18]+0x9BC/0x9C4`) si ≠ 0, o el canal cero si es 0.
- `recoverAWDLPeerTrafficRegistrationSession` `0xffffff80021d864d` — gate de la recuperación UMI.
- `handleAirPlayEnsembleTrafficReg` `0xffffff80021ffe81`, `0x21fff3f`, `0x22000a2`;
  `handleAirPlayToOdeonTrafficReg` `0xffffff80022019c9`;
  `handleSideCarEnsembleTrafficReg` `0xffffff8002204a53`;
  `setAwdlPeerTrafficRegistration` `0xffffff800220a9f2`, `0x220b9ce`, `0x220bddc`, `0x220bebf`;
  `airplayStatsPostMetrics` `0xffffff800220459f`; `notifyThreadCoexEvent` `0xffffff8002201372`;
  `buildRealTimeGroupTlv` `0xffffff8002210d99`; `updateLocalRequests` `0xffffff8002212c94`,
  `0x221370f`; `cachedPeerHasChangedInfra` `0xffffff8002215bec`;
  `cachedPeerChangedRealTimeState` `0xffffff8002215dca`;
  `preQueuePacketCheckQuietMode` `0xffffff80022164a2`; `clearAwdlStatistics`
  `0xffffff800221e24d`; `updateRTGPeersRoleMetricsInfo` `0xffffff80022320d3`;
  `getAwdlPeerDatabase` (byte) `0xffffff80022200f6`;
  puente `setAWDL_STATISTICS` `0xffffff8002289dc9`; `setSidecarChanseq` (log)
  `0xffffff800222d2fb`; `fillLegacyDataPathPayload` `0xffffff8002214413` (bit `0x40` del TLV de
  capacidades, ver `AWDL_CAPS_ADVERTISED.md`); `restoreAirPlaySink` (comparación con la sombra)
  `0xffffff8002207ff2`; `setAwdlCtl` (valor viejo del log) `0xffffff80021d06b5`; `getAwdlCtl`
  `0xffffff80021d61cb`, `0x21d69d7`, `0x21d6a00`.

## 3. `ivars+0x3FA1`, bit 0 — `chanSwitchCapable` (el getter se llama `supportsTrackingTopMaster`)

El bit 0 del byte `0x3FA1` tiene dos nombres del propio binario:

- El log de entrada de `interfaceStateChange`
  `"%s::%s Enter [reason %s] state 0x%x%s%s%s%s%s %s %s %s soloEnabled %u chanSwitchCapable %u
  RoamInProgress %d"` (`0xffffff80023d2260`, llamada en `0xffffff80021d8e7d`) lo imprime como
  `chanSwitchCapable`: el valor apilado en `0xffffff80021d8e51` es `byte [ivars+0x3FA1] & 1`
  (`0xffffff80021d8e14`). En el mismo log, `soloEnabled` es `dword [ivars+0xA6A4]` (apilado en
  `0x21d8e53`, leído en `0x21d8e06`) y `RoamInProgress` es el bit 0 de `ivars+0x4200`
  (`0x21d8e20`).
- El método `IO80211AWDLPeerManager::supportsTrackingTopMaster()` (símbolo real del binario,
  `0xffffff80021e3bf6`) devuelve el mismo bit (`0xffffff80021e3bfe`, `and al, 1`).

Único escritor en toda la familia: `configure` pone el byte a 1
(`0xffffff80021c5b5c`). Ninguna otra instrucción del kernelcache lo escribe; en particular ni
`reset` ni `awdlChipReset` ni `initParamsToDefaults` lo limpian (el barrido de `+3FA1` solo
encuentra el store de `configure`). Tras la primera `configure` el bit queda a 1 de forma
permanente.

Escrituras espejo: `configure` copia el bit a `byte ivars+0x3ECC` (`0xffffff80021c5b92`) y
`initParamsToDefaults` lo copia a `dword ivars+0x3FA8` (`0xffffff80021c7f54`).

Lectores: `interfaceStateChange` (log de entrada y rama de estrategia, §7), `setScheduleState`
`0xffffff80021d3a22` (para el estado 0xA cuenta `nonAirPlayServices` solo con el bit a 1, tras
comprobar `ivars+0x3FA8 == 0` en `0x21d3a05`), `reportDataPathEvents` `0xffffff80021e058a`
(con el bit a 1 compara 6 bytes de `ivars+0x916F` antes de copiar la configuración de canal,
`0x21e05ab`), `setElectionMetricForNonAirplaySink` `0xffffff80022163a2`, `configure`
(espejo `0x3ECC`), `initParamsToDefaults` (espejo `0x3FA8`) y `fillLegacyDataPathPayload`
`0xffffff800221443e` — junto a `airplaySoloModeEnabled != 0` añade el bit `0x400` al TLV de
capacidades (`0x2214450`, ver `AWDL_CAPS_ADVERTISED.md`).

## 4. `ivars+0xA6A4` = `airplaySoloModeEnabled` (dword)

Nombrado por IO80211Family: `setAwdlCtl` imprime `"airplaySoloModeEnabled : %u -> %u"`
(`0xffffff80023d0ea1`, llamada en `0xffffff80021d05b1`); `getAwdlCtl` lo rinde como
`"airplaySoloModeEnabled = %u"` (`0xffffff80021d6815`); `setAwdlSocialTimeSlots` imprime
`"%s::%s new mode %u old mode %u"` (`0xffffff800221bf57`) y `setElectionMetricForAirplaySink`
`"%s:%u: ATV Solo:%u Airplaying:%u met:%u…"` (`0xffffff800221609f`, el campo como `ATV Solo`).

Escritores:

| Sitio | VA | Operación |
|---|---|---|
| `configure` | `0xffffff80021c5b7e` | `= 1` incondicional, tras `setChannelSequenceStrategy(1, 0, true)` |
| `configure`, final | `0xffffff80021c630b` | `= 1` si la variable global `_maxPeerCacheSize` (`__ZL17_maxPeerCacheSize`, leída en `0xffffff80021c62ab`) vale 4 y `airplaySink != 0`, con log `"Forcing Solo Mode (PC Size %d)"` (`0xffffff80023d0744`, llamada `0x21c6302`) |
| `setAwdlCtl`, clave `"airplaySoloModeEnabled"` (petición 162) | `0xffffff80021d05a3` | `= dword bajo del valor` |
| `setAwdlSocialTimeSlots` (petición 163) | `0xffffff800221bf8d` | `= byte +4` del payload (`apple80211_awdl_social_time_slots::enabled`, `AWDL_STRUCTS.md` §5) si difiere; al cambiar notifica por la celda de vtable `+0x1B8` con el argumento 13 (`0xffffff800221bf99`) |

Lectores: `interfaceStateChange` (log de entrada `0xffffff80021d8e06`, rama de estrategia
`0xffffff80021d8f8f`, argumento del log de Solo `0xffffff80021d8fc9`), `monitorAWDLState`
`0xffffff80021ee323` (debe ser 0 para volver a `AwdlFollowsInfra`, §6), `monitorAWDLStats`
`0xffffff80021cda93` (fija el bit 3 del dword de estadísticas `ivars+0xAB14`:
`[0xAB14] = ([0xAB14] & ~8) + solo*8`, `0x21cda9d`–`0x21cdaa9`), `calculateChannelsForDataMode`
`0xffffff80021f740d` (con el par en modo AirPlay `peer+0x99` y solo ≠ 0 deriva a
`calculateDataChannelsForAirplaySolo`, `0x21f743f`), `setElectionMetricForAirplaySink`
`0xffffff8002216085` (log) y `0x2216106` (métrica de elección de nodo sink), `fillLegacyDataPathPayload`
`0xffffff8002214447` (bit `0x400` junto al bit 0 de `0x3FA1`), `getAwdlSocialTimeSlots`
`0xffffff800221bff4` (devuelve el byte bajo), y los renders/valores viejos de `setAwdlCtl`
(`0xffffff80021d0599`) y `getAwdlCtl` (`0xffffff80021d618f`, `0x21d6808`, `0x21d6831`).

## 5. `ivars+0xA65C` = `nonAirPlayServices` y `ivars+0xA650` = `airPlayServices` (contadores)

`getvalidAWDLServices()` (`0xffffff80021da6de`–`0x21da849`) los recalcula en cada llamada a
partir del mapa de descriptores de petición de servicio `ivars+0xA5C8` mediante
`IO80211CommandQueue::lockMap`, pasando como acumulador la dirección del propio campo:
`&ivars[0xA65C]` con `_countNonAirPlayServices` (`0xffffff80021da715`),
`&ivars[0xA650]` con `_countAirPlayServices` (`0xffffff80021da73f`), `&ivars[0xA654]` con
`_countRDLinkServices` (`0xffffff80021da769`), `&ivars[0xA658]` con
`_countCompanionLinkServices` (`0xffffff80021da793`) y `&ivars[0xF638]` con
`_countWifidOnlyServices` (`0xffffff80021da7bd`). Antes pone a cero `0xF638`
(`0x21da6eb`), `0xA650` y `0xA658` (`0x21da6f7`, `0x21da6fe`); `0xA65C` y `0xA654` no se ponen
a cero, de modo que los callbacks deben asignar (mecánica exacta del acumulador `?`). Los callbacks filtran
descriptores por tipo (byte `+0x38` ∈ {0x21, 0x0C}), presencia de `"ip6"`, ausencia de `"arpa"`
y cadenas `"airplay"`/`"raop"` (visible en `_countAirPlayServices`, `0xffffff80021e0cee`;
la variante nonAirPlay en `0xffffff80021f7b71` es el complemento).

Valor devuelto (`0xffffff80021da836`): si el bit 0 de `ivars+0x3F04` (CarPlay activo) está a 1,
`wifidOnlyServices` (`0xF638`, con el log `"CarPlay is active so allow wifid…"`, llamada en
`0xffffff80021da825`); si no, `nonAirPlayServices + airPlayServices`.

Escritor: solo `getvalidAWDLServices`. Las altas y bajas que mueven el recuento llegan por las
peticiones de servicio que llenan el mapa `0xA5C8` (`setAwdlPeerServiceRequest`, celda de vtable
`0xffffff800245aa30`; `deleteWifidServiceRequest`; `updateLocalRequests`), emitidas por
wifip2pd/wifid. Llamadores de `getvalidAWDLServices`: `disableAWDL` (`0x21c764a`),
`interfaceStateChange` (`0x21d8f66`, justo antes de la rama de estrategia), `evaluateQuietMode`
(`0x21da691`), `isRemoteScreenRTGSyncElectionEnabled` (`0x21eab2b`),
`isContinuityRealTimeGroupRTGSyncElectionEnabled` (`0x21eac0e`), `monitorAWDLState`
(`0x21ee071`), `buildRealTimeGroupTlv` (`0x2210d75`), `updateLocalRequests` (`0x2212ae6`),
`validServiceExceptRDLink` (`0x2214863`) y `AWDLStateDump` (`0x222afdb`).

Lectores de `0xA65C`: la rama de estrategia de `interfaceStateChange` (`0xffffff80021d8f98`,
`0x21d8fc2` y `0x21d9da9`), `evaluateQuietMode` (`0xffffff80021da6bd`), `monitorAWDLState`
(`0xffffff80021ee07a` y `0x21eefda`), `AWDLStateDump` (`0xffffff800222affc`), `updateLocalRequests`
(`0xffffff8002212ca3`). Logs que los imprimen: `"…nonAirPlayServices(%u)"` del log de Solo
(`0xffffff80023d23b3`), `"%s::%s(): [reason: %s] %s (state 0x%x… numservices:%u[AirPlay:%u,
NonAirplay:%u](valid:%u)…"` (`0xffffff80023d2889`, llamada en `0x21d9db0`, que carga
`0xA650` en `0x21d9da2` y `0xA65C` en `0x21d9da9`), `"%s::%s AWDL is still enabled due to active
sockets, but no services[nonAirplay:%u Airplay %u]"` (`0xffffff80023d6b0d`, llamada en
`0x21eefe5`), `"%s[%d] : Active Sockets %s ValidSvc %d NumAirplay %d AFHandlePending %d
TimeOfLastAFHandleRequest %d ms"` (`0xffffff80023d6740`, llamada en `0x21ee128`; `ValidSvc` es
el retorno de `getvalidAWDLServices` y `NumAirplay` el campo `0xA650`, cargado en
`0xffffff80021ee085`) y `"%s[%d]: BonJourTrig %d ValidSvc %d RTApp %d TSReq %d HasActAirDrop %d
SocketsActive %d"` (`0xffffff80023d69be`, llamada en `0x21ee67e`).

`evaluateQuietMode` (`0xffffff80021da688`) define con estos campos la variable
`ivars+0xA66C`: vale 1 solo si `airplaySink == 0` y `airPlayServices != 0` y
`nonAirPlayServices == 0` (`0x21da6a3`–`0x21da6c6`), y en ese caso llama a
`freeInfraMulticastPackets`.

## 6. `monitorAWDLState`: las condiciones de la vuelta a `AwdlFollowsInfra`

`IO80211AWDLPeerManager::monitorAWDLState()` (`0xffffff80021ed724`–`0x21ef069`) corre desde
`interfaceMonitor` (`0xffffff80021ed628`, celda de vtable `0xffffff800245a9c8`), el vigilante
periódico. El bloque que decide la transición:

1. `airplaySink != 0`: `0xffffff80021ee25e`, si es 0 salta a `0x21ee459` (sigue el resto del
   monitor sin transición). Entre ambas direcciones, y solo con los bits `0xF62D` bit 0 a 0 y
   `0xF62E` bit 0 a 1 (`0x21ee26b`–`0x21ee27f`), retira la capa NAN de AirDrop si no hay sockets
   activos y los enlaces llevan ociosos más del umbral (log `"AirDrop not active, removing NAN
   ov…"`, `0xffffff80021ee2b2`).
2. `airplaySoloModeEnabled == 0`: `0xffffff80021ee323`, si es ≠ 0 salta a `0x21ee459`.
3. Estrategia actual `AwdlStateMachine` (1) y `r14b`: `0xffffff80021ee330` (`setz al`),
   `and al, r14b` en `0xffffff80021ee337`, salto a `0x21ee459` si el conjunto no vale 1.
4. `!activeSockets(false)` y `r15b`: llamada en `0xffffff80021ee352`, `xor al, 1` en
   `0xffffff80021ee357`, `and al, r15b` en `0xffffff80021ee359`; si no vale 1 la función acaba
   (epílogo en `0xffffff80021eeca2`, tras `clearAirPlayingState` y `monitorLimitedSdb`).
5. Con todo cierto: log `"%s::%s(): Transitioning back to InfraAssist mode"`
   (`0xffffff80023d6854`, llamada en `0xffffff80021ee387`) y
   `setChannelSequenceStrategy(0, 5, false)` en `0xffffff80021ee3ae` (argumentos `esi=0`,
   `edx=5`, `ecx=0` en `0x21ee3a5`–`0x21ee3ac`).

Las dos condiciones locales que `AWDL_PRESENCE_PREREQS.md` §1.5 dejaba como `?`:

- **`r14b`** = (`dword ivars+0xA65C == 0`) AND (`dword ivars+0xA650 != 0`):
  `nonAirPlayServices == 0` (`0xffffff80021ee07a`, `setz [rbp+var_39]` en `0x21ee081`) y
  `airPlayServices != 0` (`0xffffff80021ee085`, `setnz [rbp+var_3B]` en `0x21ee08f`); la
  conjunción se forma en `0xffffff80021ee25a` (`and r14b, [rbp+var_3B]`) y se carga en
  `0x21ee1ff`/`0x21ee213`. Es decir: solo quedan servicios AirPlay registrados.
- **`r15b`** = (µs desde el último TX unicast > umbral) AND (µs desde el último RX unicast >
  umbral): `getLastUnicastTxLinkActivity` en `0xffffff80021edfe6` y
  `getLastUnicastRxLinkActivity` en `0xffffff80021ee01f`, conversión ns→µs (multiplicador
  `0x431BDE82D7B634DB`, desplazamiento total 82) en `0x21ee012`–`0x21ee018` y
  `0x21ee049`–`0x21ee04c`, comparaciones contra `dword ivars+0x9144`
  (`0xffffff80021ee054`, `setb [rbp+var_48]` en `0x21ee060` para TX y `setb [rbp+var_3C]` en
  `0x21ee06a` para RX), conjunción en `0xffffff80021ee256`. El umbral `ivars+0x9144` solo lo
  escribe `initParamsToDefaults` con `2000` (0x7D0) en `0xffffff80021c7e1d`: 2 ms sin actividad
  unicast en ambas direcciones.

Efecto colateral inmediato de la transición (`0xffffff80021ee3b3`–`0x21ee454`): con
`[this+0x18]+0x9C0 != 0` y `infraAllowedForNonRealtimeCases()` cierto (`0x21ee3c7`), sin
descubrimiento asistido (`byte 0xFA3A` bit 0 a 0, `0x21ee3dc`) y sin el bit 0x2000 de
`[this+0x18]+0x9C4` (`0x21ee3e9`; `r12d` se cargó en `0x21ee346`), copia el canal de la
infraestructura a los buffers de secuencia maestra (`[ctrl+0x9BC]` → `ivars+0x42A0`,
`[ctrl+0x9C4]` → `ivars+0x42A8`, `_zeroChanInfo` → `ivars+0x42AC`, dword
`0xffffff8002488a90` → `ivars+0x42B4`; los mismos buffers que `setChannelSequenceByState`
consume en `0xffffff80021f5900`), llama a `programMasterChannels()` (`0x21ee433`) y a
`setScheduleState(ivars[0x28E4], 0x14, true)` (`0x21ee44f`). Las ramas fallidas desembocan en el
log de diagnóstico `"infra is on DFS "`/`"not associated "`/`"Infra Not allowed with AWDL "`
(`0xffffff80021eecc5`–`0x21eed2d`).

Consecuencia de quedar en 0: `monitorAWDLState` ya no toma su rama de activación
(`cmp dword [ivars+4], 0; jz` en `0xffffff80021ee4aa` → camino de recuento
`_countNonATVCachedPeers` en `0x21ee4e2`) y `interfaceStateChange` no llega a `enableAWDL`
(§7): la interfaz no se reactiva sola.

## 7. `interfaceStateChange`: la rama de `"Transitioning to Solo mode"`

`IO80211AWDLPeerManager::interfaceStateChange(StateChangedSource)`
(`0xffffff80021d8cc8`–`0x21da65d`). Antes de decidir llama a `evaluateQuietMode()`
(`0xffffff80021d8f5e`) y a `getvalidAWDLServices()` (`0xffffff80021d8f66`), de modo que los
contadores de §5 están recién recalculados. La rama completa:

```
0xffffff80021d8f76  cmp dword [ivars+0xA648], 0     ; airplaySink
0xffffff80021d8f7d  jz  0xffffff80021d900f          ; == 0: sin llamada, estrategia intacta
0xffffff80021d8f83  xor r15d, r15d                  ; 0 = AwdlFollowsInfra
0xffffff80021d8f86  test byte [ivars+0x3FA1], 1     ; chanSwitchCapable
0xffffff80021d8f8d  jz  0xffffff80021d8ffd          ; == 0: llamar con 0
0xffffff80021d8f8f  cmp dword [ivars+0xA6A4], 0     ; airplaySoloModeEnabled
0xffffff80021d8f96  jnz 0xffffff80021d8fa1          ; != 0: Solo
0xffffff80021d8f98  cmp dword [ivars+0xA65C], 0     ; nonAirPlayServices
0xffffff80021d8f9f  jz  0xffffff80021d900f          ; ambos a 0: sin llamada
0xffffff80021d8fa1  mov r15d, 1                     ; 1 = AwdlStateMachine
0xffffff80021d8fa7  cmp dword [ivars+4], 1          ; ¿ya está en AwdlStateMachine?
0xffffff80021d8fab  jz  0xffffff80021d8ffd          ; ya está: llamada sin log
0xffffff80021d8fdc  log "Transitioning to Solo mode.  SoloModeEnabled(%u),
                         nonAirPlayServices(%u)"
                    ; r9d = dword 0xA6A4 (carga en 0x21d8fc9)
                    ; pila = dword 0xA65C (carga en 0x21d8fc2)
0xffffff80021d900a  setChannelSequenceStrategy(r15d, 4, false)   ; reason 4
```

Resumen: la llamada ocurre solo con `airplaySink != 0`; el valor pedido es 0 si
`chanSwitchCapable` es 0, y 1 si lo es y además `airplaySoloModeEnabled != 0` o
`nonAirPlayServices != 0`; con `chanSwitchCapable` a 1 y ambos contadores a 0 la estrategia no
se toca. Con la estrategia ya en 1 el log de Solo se suprime (`0x21d8fab`) pero la llamada
sigue (inocua: `setChannelSequenceStrategy` no reescribe el mismo valor).

El motivo (`reason`) 4 corresponde al cambio de estado de la interfaz; el emisor del evento es
el `StateChangedSource` de `AWDL_PRESENCE_PREREQS.md` §1.4 (`doEnable`, `disableAWDL`,
`reportDataPathEvents`, `exitAwdlSuspendedMode`, `handleAwdlLLATimer`, …).

La misma función es quien activa AWDL: la puerta de `enableAWDL` exige estrategia distinta de 0
(`cmp dword [ivars+4], 0; jz` en `0xffffff80021d9bc2`) y vuelve a leer los
contadores para su log con `numservices` (`0x21d9da2`–`0x21d9da9`).

## 8. Condiciones finales: caída a `AwdlFollowsInfra` y regreso a `AwdlStateMachine`

### 8.1 Cómo un `awdl0` externo acaba en `AwdlFollowsInfra` (0)

Dos caminos, ambos con `force=false` (la estrategia 3 no se pisa, pero 1→0 no está protegida):

1. **Vía `interfaceStateChange` (reason 4), solo antes de la primera `configure`.**
   Requiere `airplaySink != 0` y bit 0 de `0x3FA1` a 0. Tras `configure` el bit vale 1 y no
   existe escritor que lo limpie (§3), así que este camino queda cerrado en cuanto la interfaz
   se configura.
2. **Vía `monitorAWDLState` (reason 5), el camino real post-configure.** Se necesitan las cinco
   condiciones simultáneas de §6:
   - `airplaySink != 0` (alguien envió la petición 162 con la clave `airplaySink`, o arrancó un
     sidecar);
   - `airplaySoloModeEnabled == 0` (nadie la activó por 162/163/`configure` — o se apagó por 163);
   - estrategia actual 1;
   - `nonAirPlayServices == 0` con `airPlayServices != 0` (solo quedan servicios AirPlay
     registrados en el mapa `0xA5C8`);
   - sin sockets activos y más de `ivars[0x9144]` µs (2000 por defecto) sin TX ni RX unicast.

   La condición `airPlayServices != 0` es la clave protectora: un `awdl0` sin ningún servicio
   registrado (`0xA650 == 0` y `0xA65C == 0`) nunca baja a 0 por este camino, y la rama de
   `interfaceStateChange` tampoco lo toca (con `chanSwitchCapable==1` y ambos contadores a 0 no
   hay llamada). La caída exige un escenario AirPlay: sink activado, solo desactivado, servicios
   AirPlay presentes, sin otros servicios, sin sockets y enlaces ociosos.

Con la estrategia en 0, ni `interfaceStateChange` (puerta en `0x21d9bc2`) ni `monitorAWDLState`
(puerta en `0x21ee4aa`) vuelven a activar AWDL: es el bloqueo descrito en
`AWDL_PRESENCE_PREREQS.md` §1.5.

### 8.2 Qué devuelve la interfaz a `AwdlStateMachine` (1)

1. **`configure`** — `setChannelSequenceStrategy(1, 0, true)` en `0xffffff80021c5b75`, con
   `force=true`: gana incluso sobre `AwdlUserConfigured`. `configure` corre desde `doEnable`
   (tabla `0xffffff800245aaa8`; petición 117 `SET_AWDL_SYNC_ENABLED` y 134
   `SET_AWDL_SYNC_STATE` vía `setAwdlSyncEnabled`/`setAwdlSyncState`), desde ciertas fuentes de
   `interfaceStateChange`, desde `setAwdlBtleEnableSynchWithParams`, `getAwdlRestoreDefaults`
   (`GET RESTORE_DEFAULTS`), `reportDataPathEvents` y `exitAwdlSuspendedMode` (alcanzado desde
   `setAwdlOperatingMode` (217), `handleSuspendTimer`, `setAwdlAutoMode` y
   `reportDataPathEvents`).
2. **`interfaceStateChange` (reason 4)** — con `airplaySink != 0`, `chanSwitchCapable == 1`
   (siempre cierto tras `configure`) y `airplaySoloModeEnabled != 0` o `nonAirPlayServices != 0`
   (`0xffffff80021d900a`). En la práctica: activar solo (162/163) o registrar cualquier servicio
   no AirPlay y esperar un cambio de estado de la interfaz.
3. **Petición 162 (`AWDL_CTL`), clave `"channelSequenceStrategy"`** — `setChannelSequenceStrategy(valor, 3, true)`
   en `0xffffff80021cffba`: manda el valor que se pida, incluido 1.
4. **Petición 217 (`SET_AWDL_OPER_MODE`) con modo ≠ 1** — `setChannelSequenceStrategy(1, 6, true)`
   en `0xffffff800221ecbc` (modo 1 escribe 3 directo en `0x221ec89`). `setAwdlAutoMode`
   (`0xffffff800221e976`) haría lo mismo pero no tiene xrefs en 25G241 `?`.
5. **Petición 129 (`AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`)** — pone 3 (`AwdlUserConfigured`),
   no 1 (`0xffffff800221d150`); sirve para sacar la interfaz de 0 a costa de congelar la
   secuencia.

## 9. Peticiones userland que tocan los campos

Índice de la tabla moderna `0xffffff800245fef0` = petición − 1 (verificado contra las parejas
conocidas 94/93, 117/116, 129/128, 134/133):

| Petición | Puente (índice) | Efecto |
|---|---|---|
| 162 `AWDL_CTL` | `setAWDL_CTL` `0xffffff80022892d1` (161) | `airplaySink` (+ sombra `airplaySinkCap`), `airplaySoloModeEnabled`, `channelSequenceStrategy` (reason 3, force), y el resto del catálogo de claves de `setAwdlCtl` |
| 163 `AWDL_SOCIAL_TIME_SLOTS` | `setAWDL_SOCIAL_TIME_SLOTS` `0xffffff8002289403` (162) | `airplaySoloModeEnabled = payload+4` |
| 148 `AWDL_STRATEGY` | `setAWDL_STRATEGY` `0xffffff8002288ff4` (147) | NO toca `ivars+4`: `setAwdlStrategy` escribe `apple80211_awdl_strategy+4` en `ivars+0x28E0` (`0xffffff800221c850`) |
| 217 `AWDL_OPER_MODE` | `setAWDL_OPER_MODE` `0xffffff8002289c29` (216) | modo 1 → estrategia 3 directa; otro modo → 1 (reason 6) |
| 129 `AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `0xffffff8002288caa` (128) | estrategia 3 (reason 3) |
| 117 `AWDL_SYNC_ENABLED` / 134 `AWDL_SYNC_STATE` | `0xffffff800228889d` (116) / `0xffffff8002288e21` (133) | llegan a `configure` → estrategia 1 (reason 0, force) y `airplaySoloModeEnabled = 1` |

Coincide con `AWDL_APPLE80211_DISPATCH.md` (162/163) y `AWDL_REQUEST_ISSUERS.md` (163 solo CW =
CoreWiFi `?`). Que wifip2pd envíe `AWDL_CTL` con la clave `airplaySink` o
`AWDL_SOCIAL_TIME_SLOTS` en una máquina Apple no consta en los docs de wifip2pd de este
repositorio `?`.

## 10. Tablas auxiliares

- Nombres de estrategia: `0xffffff800245b6d0` (copia en `0xffffff8002459c00` para
  `StringForAwdlStrategy`, `0xffffff80021a8b88`).
- La tabla contigua `0xffffff800245b6f0` (`Infra Assist (Legacy)`, `Split Mode`, `Server Mode`,
  `Client Mode`) NO es de motivos: la usa `airplayModeToString(AirPlayMode_t)`
  (`0xffffff80021f1615`) y `printState`. Los `reason` observados de
  `channelSequenceStrategyReasons` son solo números: 0 `configure`, 3 `setAwdlCtl`/
  `setAwdlChannelSequence`, 4 `interfaceStateChange`, 5 `monitorAWDLState`,
  6 `setAwdlOperatingMode`/`setAwdlAutoMode`.
- Campos de servicios contiguos: `+0xA650` `airPlayServices`, `+0xA654` rdLink,
  `+0xA658` companionLink, `+0xA65C` `nonAirPlayServices`, `+0xF638` wifidOnly.
- `ivars+0x9144`: umbral de ociosidad unicast en µs, 2000 por defecto
  (`initParamsToDefaults` `0xffffff80021c7e1d`); sin otros escritores.
- `ivars+0x28B0` bit 0: `AFHandlePending` (log `0xffffff80023d6740`); lo escriben
  `actionFrameInput` (`0xffffff800220e54a`, `0x220e751`), `actionFrameProcessThread`
  (`0xffffff80021c4628`) y lo limpian `enableAWDL` (`0xffffff80021dd065`),
  `actionFrameProcessThread` (`0x21c4039`) y `actionFrameQueueCleanup` (`0x21c86cf`).

## 11. Pendientes

- Mecánica exacta del acumulador de `_countNonAirPlayServices`/`_countRDLinkServices`
  (asignación frente a suma) `?`.
- Significado del gate `dword ivars+0x4008 == 8` en `setChannelSequenceStrategy`
  (`0xffffff80021ca9ee`) y de las máscaras MI `0x05050505`/`0xffffffff` `?`.
- Si wifip2pd/airportd emiten `AWDL_CTL` (162) con `airplaySink` o `AWDL_SOCIAL_TIME_SLOTS`
  (163) en hardware Apple `?`.
- `setAwdlAutoMode` (`0xffffff800221e976`) sin xrefs de código en 25G241 `?`.
- El barrido de desplazamientos es lineal con seguimiento de registro por función; los usos cuyo
  único camino pasa por una llamada intercalada pueden faltar (caso `0x21ee4aa`, recuperado a
  mano). Los escritores, en cambio, salen de un barrido global completo.
