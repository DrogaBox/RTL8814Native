# Prerrequisitos de la presencia AWDL: LLAValid, AWDLOn y el contexto de los slots PPP

Cierra los tres pendientes de [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §6: qué activa
`LLAValid` (`PM+65308`), quién escribe `AWDLOn` (`PM+16096`) y en qué hilo/cerrojo corren
`monitorPeer` y el ioctl 415 cuando llaman a los slots +0xF30/+0xF38. Base: KC 25G241
(macOS 26.7.1) en IDA. Lo no comprobado va con `?`.

## 1. LLAValid (`PM+65308`, byte en `ivars+0xFF1C`)

### 1.1 Qué significa

Es el resultado de `IO80211AWDLPeerManager::isLLAConfigured` (`0xffffff80021dc508`): si la interfaz
virtual (`ivars base+1376`) no es nula, devuelve
`IOSkywalkNetworkInterface::isLinkLocalAddressConfigured(ifnet)` (`0xffffff8002b0bebe`); si es nula,
registra `"isLLAConfigured virtual interface is NULL"` y devuelve 0. El comprobador recorre la lista
de direcciones del `ifnet` con `ifnet_get_address_list_family(..., 0x1E)` (AF_INET6) y acepta la
primera con `out_addr[8] == 0xFE && out_addr[9] < 0xC0` (prefijo fe80::/10); con el `ifnet` nulo
devuelve 1. El `ifnet` se saca de `ivars+8` de la interfaz Skywalk (`0xffffff8002b0bebe`).

No hay ningún evento IPv6 enganchado: el valor se sondea. Los tres escritores:

| Escritor | Dirección | Valor | Guarda |
|---|---|---|---|
| `initParamsToDefaults` (`0xffffff80021c7a40`) | `0xffffff80021c818f` | 0 | ninguna (inicio) |
| `interfaceStateChange` (`0xffffff80021d8cc8`) | `0xffffff80021d9b39` / `0xffffff80021d9b44` | 1 / 0 | `_bonjourTriggersAWDL != 0` (`ivars+0xA718`, test en `0xffffff80021d9b0a`) **y** `getvalidAWDLServices() != 0` |
| `monitorAWDLState` (`0xffffff80021ed724`) | `0xffffff80021ed932` | byte = `isLLAConfigured()` | ninguna (copia directa en cada tick) |

Los tres escriben el byte completo con 0/1, de modo que "bit 0 de `PM+65308`" es el byte entero.
Un barrido de todos los accesos con desplazamiento `0xFF1C` en IO80211Family no da más escritores;
los lectores son `setPeerRoutable` (`0xffffff80021aaf47`), `isLLAValid` (`0xffffff8002236d6c`) y
`monitorAWDLState` (`0xffffff80021ee228`).

### 1.1.1 `ivars+0xA718` = `_bonjourTriggersAWDL`

El nombre sale de las logs `"bootarg override ivars->_bonjourTriggersAWDL -> %u"`
(`initWithInterface`, `0xffffff80021c168d` y `0xffffff80021c16f8`) y `"bonjourTriggersAWDL"`
(`setAwdlCtl`, `0xffffff80021d0338`). Escritores (barrido completo del desplazamiento `0xA718`):

| Función | Dirección | Valor |
|---|---|---|
| `initWithInterface` | `0xffffff80021c15d6` | 1 (por defecto, junto con `ivars+4 = 1`) |
| `initWithInterface` | `0xffffff80021c1668` | 0 si existe el boot-arg `awdl_enable` |
| `initWithInterface` | `0xffffff80021c16d1` | 0 si `awdl_oper_mode=1` (además `ivars+4 = 3`, `ivars+0xA66C = 0`) |
| `setAwdlManualMode` (`0xffffff80021c2d0a`) | `0xffffff80021c2d1b` | 0 (`ivars+4 = 3`); sin xrefs de código |
| `setAwdlAutoMode` (`0xffffff800221e95e`) | `0xffffff800221e97f` | 1; sin xrefs de código |
| `setAwdlOperatingMode` (`0xffffff800221eb90`, desde `setAWDL_OPER_MODE`) | `0xffffff800221ec92` / `0xffffff800221ecc5` | modo 1 → 0 (manual); modo 2 → `setAwdlSuspendedMode` sin tocarlo; otro → 1 y `exitAwdlSuspendedMode` |
| `setAwdlCtl` | `0xffffff80021d032a` | dword del payload (log `"bonjourTriggersAWDL %u -> %u"`) |

Lectores: `interfaceStateChange` (`0xffffff80021d9b0a`), `monitorAWDLState` (`0xffffff80021ee4c0`,
`0xffffff80021ee691`), `updateLocalRequests` (`0xffffff8002212acb`), `getAwdlCtl` y la propia
`setAwdlCtl` (`0xffffff80021d0320`). Sin boot-args y sin `AWDL_OPER_MODE` manual el campo vale 1, de
modo que el bloque LLA de `interfaceStateChange` corre en el modo automático por defecto.

### 1.2 `handleAwdlLLATimer`: el sondeo de 500 ms

`IO80211AWDLPeerManager::handleAwdlLLATimer` (`0xffffff80021c2ee6`) es el callback del timer
guardado en `ivars AWDL+10400` (`0x28A0`), creado en `initWithInterface`
(`0xffffff80021c2080`: `IO80211TimerSource::allocWithParams(2, this, workqueue, handleAwdlLLATimer)`,
almacenado en `0xffffff80021c209b`). El workqueue es el del controlador: se obtiene con la celda
+0xC68 del objeto en `ivars base+1312` (`0xffffff80021c203b`). Tres ramas:

- Si `PM+16096 & 2` (AWDLOn ya activo): registra `"AWDL is enable, cancel monitor timer"` y llama a
  la celda +0x218 (`IOTimerEventSource::cancelTimeout`) y a la +0x158
  (`IOTimerEventSource::disable`) de `__ZTV18IO80211TimerSource`. El timer no se libera.
- Si `isLLAConfigured()`: registra `"LLA is configured so go ahead and enable AWDL"`, hace el mismo
  `cancelTimeout` + `disable` y llama a la celda +440 de la propia vtable (`interfaceStateChange`)
  con fuente **7** (`Enable`).
- Si no: registra `"WARNING: LLA still not configured so continue monitoring"` y rearma el timer con
  `setTimeoutMS(500)` (celda +0x1D0).

El timer solo está armado mientras no hay LLA: `interfaceStateChange` lo activa y lo arma en la rama
de error (§1.3) y `handleAwdlLLATimer` lo desactiva al conseguir el LLA o al ver AWDL ya activo.

### 1.3 El bloque LLA de `interfaceStateChange`

En el cuerpo común (`0xffffff80021d8cc8`), tras la log de entrada:

1. Si `_bonjourTriggersAWDL == 0` o `getvalidAWDLServices() == 0`, se salta el bloque.
2. Llama a `isLLAConfigured()`. Con 0: escribe `PM+65308 = 0` (`0xffffff80021d9b44`), registra
   `"ERROR: enableLLAMonitoring as no IPv6 LLA, Source = %s"` (`0xffffff80021d9b5c`), activa el
   timer LLA (celda +0x150 = `IOTimerEventSource::enable`, llamada en `0xffffff80021d9b99`) y lo
   arma con 500 ms (celda +0x1D0 = `setTimeoutMS` con `esi=0x1F4`,
   `0xffffff80021d9bb1`–`0xffffff80021d9bb6`).
3. Con 1: escribe `PM+65308 = 1` (`0xffffff80021d9b39`) y sigue.
4. Decisión de activación (`0xffffff80021d9bc2`–`0xffffff80021d9be6`): sale de inmediato si
   `ivars+4 == 0` (`cmp dword ptr [rcx+4], 0` en `0xffffff80021d9bc2`); si no,
   `enable = (LLAValidAcabaDePonerse | fuente == 3) & !(AWDLOn & 8)`; el test de fuente es
   `cmp r15d, 3` en `0xffffff80021d9bc8` y el de suspensión `test al, 8` en
   `0xffffff80021d9bd7`. Sin activación y sin servicios/AirPlay/sidecar, sale
   (`"NOT-ENABLING-AWDL"`); con activación termina llamando a `enableAWDL`
   (xref en `0xffffff80021da336`, tras la log `"Enabling AWDL due to %s"`).

`monitorAWDLState`, además de la copia de `0xffffff80021ed932`, re-comprueba el bit más adelante
(`test byte ptr [rcx+0FF1Ch], 1` en `0xffffff80021ee228`): con LLAValid a 0 y servicios presentes
registra `"No IPv6 Address on AWDL interface ! Request IPv6"` (rama que sigue a `0xffffff80021ee228`).

### 1.4 `StateChangedSource`: enum completo y emisores verificados

`IO80211PeerManager::stringForInterfaceStateChangedSource` (`0xffffff8002254562`) indexa la tabla
de cadenas en `0xffffff800245d450` (0..14, >14 = `"unknown"`):

| Valor | Nombre | Emisor verificado (inmediato en el emisor) |
|---|---|---|
| 0 | `Init` | ninguno visto `?` |
| 1 | `BSSIDChanged` | ninguno visto `?` |
| 2 | `Mdns` | `updateLocalRequests` `0xffffff8002212951` y `0xffffff8002212f97` |
| 3 | `setSyncEnabled` | ninguno visto; aparece solo como caso especial en la decisión de `interfaceStateChange` (`0xffffff80021d9bc8`) `?` |
| 4 | `Debug` | `setAwdlCtl` `0xffffff80021d011f` |
| 5 | `Interface` | `reportDataPathEvents` `0xffffff80021de7fb` |
| 6 | `Reset` | `reportDataPathEvents` `0xffffff80021df883`; `exitAwdlSuspendedMode` `0xffffff80021e5f76` |
| 7 | `Enable` | `doEnable` `0xffffff80021c53d1`; `disableAWDL` `0xffffff80021c7691`; `handleAwdlLLATimer` (llamada final a la celda +440 con 7) |
| 8 | `Channel Switch Complete` | ninguno visto `?` |
| 9 | `SetInfraChannel` | `IO80211PeerManager::setInfraChannel` `0xffffff8002254b03`; `IO80211VirtualInterface::setInfraChannel` `0xffffff8002330a2c`; `reportDataPathEvents` `0xffffff80021de147` y `0xffffff80021df49a` |
| 10 | `Master Channel Changed` | ninguno visto `?` |
| 11 | `Secondary Master Channel Changed` | `monitorCountryCode` `0xffffff800225b3b6`; `reportDataPathEvents` `0xffffff80021de77c` |
| 12 | `Grace Period expired` | ninguno visto `?` |
| 13 | `AirPlay Solo Request` | ninguno visto `?` |
| 14 | `AWDL exiting suspension` | `exitAwdlSuspendedMode` `0xffffff80021e5ff2` |

La máscara `0x4A20` de `updateHTandVHTCapsForRSDB` (`bt` en `0xffffff80021d9ac3`) selecciona las
fuentes {5, 9, 11, 13, 14}; la fuente 6 produce la log `"Recovering Reset due to watchdog"`
(`0xffffff80021d9b01`); 11 y 14 resetean la configuración de canales.

Para el RTL8814: `reportDataPathEvents` es la vía del driver (eventos 41/42/43) y las fuentes que
emite (5, 6, 9, 11) entran todas por el bloque LLA cuando hay servicios válidos.

## 2. AWDLOn (`PM+16096`, dword en `ivars+0x3EE0`)

### 2.1 Mapa de bits

De la log de entrada de `interfaceStateChange` (`0xffffff80021d8d1c`–`0xffffff80021d8da9`) y de
`configure` (`0xffffff80021c54ea`):

| Bit | Máscara | Nombre (cadena de la log) |
|---|---|---|
| 0 | 0x1 | `CONFIGURED` (`test r14b, 1`) |
| 1 | 0x2 | `SYNC-ACTIVE` (`test r14b, 2`; el bit que `reportDataPathEvents` llama `isAwdlOn`) |
| 2 | 0x4 | `QUIET` (lo pone `enterQuietMode`); la log imprime `QUIET` con `test r14b, 0Ch`, es decir, también con el bit 3 |
| 3 | 0x8 | `FOR ASSOCIATION` (`test r14b, 8`): suspensión por asociación infra; lo pone `setAwdlSuspendedMode` |
| 4 | 0x10 | configure en curso (`"Configure is already in progress"`, rama de `interfaceStateChange`; `configure` lo pone en `0xffffff80021c5652` y lo limpia en `0xffffff80021c5ede`) |
| 5 | 0x20 | segundo flag de `configure` (`0xffffff80021c57eb`); es el único que conserva `failToEnable` `?` |

Bits 0xE apagados = `OFF` en la misma log (`test r14b, 0Eh`).

### 2.2 Escritores

| Función | Dirección(es) | Operación |
|---|---|---|
| `enableAWDL` (`0xffffff80021dcf58`) | `0xffffff80021dd5a3`–`0xffffff80021dd5a9` | `(x & ~6) \| 2`: limpia QUIET, activa SYNC-ACTIVE |
| `setAwdlSyncState` (`0xffffff800221e3d4`) | `0xffffff800221e49d` | `x & ~2` (apaga SYNC-ACTIVE antes de reconfigurar) |
| | `0xffffff800221e52e` | `x & ~4` (limpia QUIET tras el primer ioctl 117 correcto) |
| | `0xffffff800221e6e6`–`0xffffff800221e6ec` | `(x & ~6) \| 2` (reactiva SYNC-ACTIVE tras el segundo ioctl 117) |
| `reset` (`0xffffff80021f2a20`) | `0xffffff80021f2f86` | `x = 0` (pone el dword entero a cero); la log `"state %x"` inmediata imprime 0 |
| `awdlChipReset` (`0xffffff80021f24d0`) | `0xffffff80021f266f` | `x = 0` |
| `disableAWDL` (`0xffffff80021c6ae8`) | `0xffffff80021c721e`–`0xffffff80021c7221` | `x & ~2` (si SYNC-ACTIVE estaba a 0, sale con `"AWDL already disabled"`, `0xffffff80021c71f6`) |
| | `0xffffff80021c72a9`–`0xffffff80021c72bd` | `x = (x & ~arg) & ~4`: limpia QUIET y los bits que pida el argumento (máscara; el bit 3 se consulta en `0xffffff80021c723a`), luego `setEnableState(bit1)` (`0xffffff80021c72c3`–`0xffffff80021c72cd`) |
| `configure` (`0xffffff80021c54ea`) | `0xffffff80021c5652`, `0xffffff80021c57eb`, `0xffffff80021c5ede`, `0xffffff80021c625d`–`0xffffff80021c6268` | `x \| 0x10`, `x \| 0x20`, `x & ~0x10`; final `(x & ~0x11) + 1`: pone CONFIGURED y limpia el bit 4 |
| `failToEnable` (`0xffffff80021c50de`) | `0xffffff80021c50eb` | `x & 0x20` |
| `setAWDLMaxBandwidth` (`0xffffff80021cde02`) | `0xffffff80021cde76` | `x & ~1` (quita CONFIGURED) |
| `setAwdlSyncEnabled` (`0xffffff800221b842`) | `0xffffff800221ba80` | `x & ~6` |
| `reportDataPathEvents` (`0xffffff80021dd8f4`) | `0xffffff80021df3bb`–`0xffffff80021df3be` | `x & ~2` |
| `setAwdlSuspendedMode` (`0xffffff80021e5c7a`) | `0xffffff80021e5cc6`–`0xffffff80021e5cc9` | `x \| 8` (si no estaba ya) |
| `clearAWDLSuspendState` (`0xffffff80021c777a`) | `0xffffff80021c7793`–`0xffffff80021c7796` | `x & ~8` |
| `exitAwdlSuspendedMode` (`0xffffff80021e5e76`) | `0xffffff80021e5e8e` | `x & ~8` |
| `enterQuietMode` (`0xffffff80021f408e`) | `0xffffff80021f42af`–`0xffffff80021f42b5` | `(x & ~6) \| 4`, luego `setEnableState(false)` |
| `exitQuietMode` (`0xffffff80021fad1c`) | `0xffffff80021faf7e`; `0xffffff80021fb07f`–`0xffffff80021fb085` | `x & ~6`; `(x & ~6) \| 2` |
| `initParamsToDefaults` (`0xffffff80021c7a40`) | — | no toca 16096 (solo `PM+65308 = 0` en `0xffffff80021c818f`) |

La lista sale de un barrido de todos los accesos con desplazamiento `0x3EE0` en IO80211Family.
`enableAWDL` tiene un único xref de código: `interfaceStateChange` en `0xffffff80021da336`. AWDLOn
bit 1 se activa en tres sitios: `enableAWDL`, `setAwdlSyncState` tras el segundo ioctl 117 correcto y
`exitQuietMode` (llamado desde `exitAwdlSuspendedMode`, `initiateHighThruputMode` y otros). La
primera activación tras `configure` pasa por `interfaceStateChange → enableAWDL`, con las fuentes
de la tabla §1.4 (7 directo, o cualquier fuente con `LLAValid` recién puesto y sin suspensión).
`reset` y `awdlChipReset` lo apagan todo; `disableAWDL` conserva CONFIGURED y QUIET salvo máscara
del argumento (el valor que pasan `doDisable` (`0xffffff80021c69cc`) y el resto de llamadores no
está trazado `?`).

### 2.3 `setAwdlSyncState` paso a paso

`IO80211AWDLPeerManager::setAwdlSyncState` (`0xffffff800221e3d4`), cuando el estado pedido es 0
(Stopped) o el bit CONFIGURED está activo (`0xffffff800221e41b` test del bit 0; condición en la
rama `a2[1] == 0 || (x & 1)`):

1. `x & ~2` (`0xffffff800221e49d`), marca de tiempo en `ivars+42472`, `resetElectionState`,
   `updateBroadcastMI`, `updateSyncFrameTemplate(true)`.
2. `apple80211RouteInternal(0x802869C8, 117, ...)` (`0xffffff800221e510`–`0xffffff800221e51a`);
   con éxito `x & ~4` (`0xffffff800221e52e`).
3. ioctl **134** (sync state) con el payload (`0xffffff800221e556`); con éxito
   `ivars+17028 = a2[1]`.
4. `initFWElectionParams` y segundo ioctl **117** (`0xffffff800221e6c0`–`0xffffff800221e6cc`); con
   éxito `(x & ~6) | 2` (`0xffffff800221e6e6`–`0xffffff800221e6ec`) y `setEnableState(bit1)`
   (`0xffffff800221e794`).

Es decir: el campo baja y sube alrededor de los ioctls 117/134; el estado final del bit 1 depende
del éxito del rearmado del firmware, no del argumento.

## 3. Hilo, workloop y cerrojos de `monitorPeer` y del ioctl 415

### 3.1 La cadena de `monitorPeer`

`IO80211AWDLPeer::monitorPeer` (`0xffffff80021a9294`, celda +0x180 de la vtable
`__ZTV15IO80211AWDLPeer @ 0xffffff8002459f80`; la misma celda existe en la vtable base
`__ZTV11IO80211Peer @ 0xffffff800245c738`, valor en `0xffffff800245c8c8`) tiene un único llamador
en todo el kernelcache: `_doMonitoring` (`0xffffff800224f70b`), que ejecuta
`*(vtable+0x180)(peer, ctx)` en `0xffffff800224f720`, escribe `*ctx = 0` antes y devuelve 12 si el
byte queda a 1 (es el resultado de `shouldRemovePeerFromSystem`, escrito por `monitorPeer`).

`_doMonitoring` lo invoca `IO80211PeerManager::interfaceMonitor` (`0xffffff800224e658`) mediante
`IO80211CommandQueue::map(coladeComandos, _doMonitoring, this, &ctx, {0, 0, _addToFlush}, 0xFFFFFFFF)`
(llamada en `0xffffff800224e943`; `coladeComandos` = `ivars base+1344`). El `ctx` es un buffer local
de 112 bytes puesto a cero; solo se usa su byte 0. Tras el `map`, desbloquea el data path y llama a
`removeFlushedPeers`. Los dos accesos previos a las celdas +0x180 de los objetos en
`ivars base+0x530`/`+0x538` (`0xffffff800224e836`, `0xffffff800224e895`) pasan el mismo ctx; en las
vtables de `IO80211TimerSource`/`WCLTimerSource` esa celda es `IOEventSource::_RESERVEDIOEventSource1`
(`0xffffff8000bb7540`), de modo que el llenado del ctx queda sin verificar `?` (para la cola PPP es
irrelevante: el byte lo escribe `monitorPeer`).

La cadena temporal completa:

1. `_interfaceMonitorTimer` en `ivars base+0x718` (1816), creado en
   `IO80211PeerManager::initWithInterface` (`0xffffff800224789b`–`0xffffff80022478b3`:
   `allocWithParams(2, this, workqueue, handleMonitorTimer)`) sobre el workqueue del controlador
   (`ivars base+0x528`).
2. `handleMonitorTimer` (`0xffffff80022482d4`) llama a la celda +352 (+0x160) = `interfaceMonitor`
   y, si devuelve distinto de 0, rearma el timer de `ivars base+1816` con un periodo con jitter
   derivado de `ivars+1840` (mínimo ~101 ms).
3. `interfaceMonitor` (base) comprueba sus guardas, llama a `lockDataPath`
   (`0xffffff800224db2a`: celda +0x118 del objeto en `ivars base+120`; el desbloqueo es la celda
   +0x120), hace el `map` de §3.1 y desbloquea.
4. `IO80211AWDLPeerManager::interfaceMonitor` (`0xffffff80021ed628`, celda +0x160) corre cuando la
   base devuelve 1 y `ivars+16100 == 0` (reset de chip no en curso) y ejecuta la suite
   `monitor*`, entre ellas `monitorAWDLState`.

Por tanto, `monitorPeer`, `ageoutPacketsWaitingForPPP` y la llamada al slot +0xF38 corren en el
**workqueue del controlador** (contexto de workloop, serializado con el resto del trabajo del
driver) **con el cerrojo del data path tomado** (`lockDataPath`, mismo objeto/celda que usa
`reset`, cuya rama de fallo registra `"lockDataPath failed"`).

### 3.2 El camino del ioctl 415

`setPOST_PRESENCE_DONE` (`0xffffff800228d968`) exige longitud de petición 12 y datos no nulos, saca
el manager de la interfaz (`safeMetaCast` a `IO80211AWDLPeerManager`) y llama a
`setAwdlPostPeerPresenceDone` (`0xffffff800221c978`), que hace `findPeer(data+4)` +
`safeMetaCast` a `IO80211AWDLPeer` + `pppDone(peer, true)`. Ninguna de las dos funciones toma
cerrojo propio.

El contexto lo pone el enrutador de peticiones:

- `apple80211RouteExternal` (`0xffffff800236cd2e`) indexa `gSetHandlerTable` con `cmd-1` y llama al
  handler directamente (la entrada del 415 es `0xffffff8002460be0`), sin gate ni workloop en la
  propia función.
- Sus tres llamadores son gated: `IO80211SkywalkInterface::performGatedCommandIOUC`
  (`0xffffff80022f6e30`), `IO80211SkywalkInterface::performGatedCommandIOCTL`
  (`0xffffff80022f779a`, xref `0xffffff80022f78ad`) y `IO80211VirtualInterface::handleIoctlGated`
  (`0xffffff8002331d20`).
- El camino BSD del 415: `IO80211VirtualInterface::handleIoctl` (`0xffffff8002331c88`) compara el
  código con los cuatro ioctls apple80211 (`0x802069C8`, `0x802869C8`, `0xC02069C9`,
  `0xC02869C9`). Con uno de ellos obtiene el controlador (celda +0xBA8 =
  `IO80211SkywalkInterface::getController`) y llama a su celda +0x850, que en
  `__ZTV17IO80211Controller` es `IONetworkController::executeCommand` (`0xffffff80026f51e0`), con
  `handleIoctlGated` como acción (referencia en `0xffffff8002331cdc`). `handleIoctlGated` rehace la
  `apple80211req` y llama a `apple80211RouteExternal` directamente (`0xffffff8002331e56`). Los demás
  ioctls saltan a `processBSDCommand` (`0xffffff8002331d1b`), que pasa por
  `performGatedCommandIOCTL` (`0xffffff80022f70e8`). En los dos casos el handler corre bajo el
  **command gate del workloop del controlador**, en el hilo que hizo el ioctl (wifip2pd).

Comparación de contextos para el driver RTL8814: +0xF30 llega por el gate del controlador desde un
ioctl; +0xF38 llega desde `ageoutPacketsWaitingForPPP` (`0xffffff80021aa984`) en el workqueue con el
data-path lock, y desde `flushPacketsWaitingForPPP` (`0xffffff80021bff87`) en los contextos de
borrado de peer (`removePeer` `0xffffff80022177ee`, `cleanUpForPeerRemovedFromNDP`) — el cerrojo de
esa última ruta no está trazado `?`. La implementación de ambos slots debe ser segura en workloop y
no dormir con el data-path lock tomado `?`.

### 3.3 Qué se libera tras +0xF30/+0xF38

Los tres llamadores construyen el array igual: `IOMallocZeroData(512)` = 64 punteros (en `pppDone`,
`0xffffff80021bfb3b`), lo llenan por lotes de 64 con `dequeuePPPWaitPacketFromHead` y llaman al slot
(+0xF30 en `0xffffff80021bfc77` para `pppDone`; +0xF38 en `0xffffff80021aa984` para `ageout` y
`0xffffff80021bff87` para `flush`). Después:

- `pppDone` hace `IOFreeData` del array en todos los caminos (`jmp _IOFreeData` en
  `0xffffff80021bfd93`).
- `ageout` (`IOFreeData` al cierre de su bucle) y `flush` (`IOFreeData` final, decompilado de
  `0xffffff80021bfe44`) liberan igualmente solo el array.

Los paquetes de la cola no se tocan en ninguna de las tres funciones: su propiedad pasa al slot.
Con los slots heredados de `IO80211VirtualInterface` (`0xffffff8002334e68`/`0xffffff8002334e88`,
solo `IOLog("… Not Implemented")`) el array se devuelve al allocator pero los paquetes se pierden
(sin entrega y sin devolución al pool). Esto cierra la duda del §6 de
[AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md): el array lo libera siempre el llamador; lo
que filtra es la cola de paquetes si los slots no están sobrescritos.

Fallo de reserva: si `IOMallocZeroData` falla, los tres usan `globalPacketArray` (estática) como
array y `pppDone`/`flush` terminan pasándola por `IOFreeData` `?` (liberación de un objeto global
en el camino de error; no verificado en runtime).

### 3.4 Guardas de `monitorPeer` relevantes para la cola PPP

- Retorno temprano si `PM+16096 & 0xC` (QUIET o FOR ASSOCIATION): solo actualiza la marca
  `peer ivars+0x310` y sale sin tocar la cola (`test byte ptr [rcx+3EE0h], 0Ch` en
  `0xffffff80021a9339`, retorno en `0xffffff80021a9349`).
- `ageoutPacketsWaitingForPPP` solo se llama si el flag PPP del par está a 0
  (`test byte ptr [rax+6DEh], 1` en `0xffffff80021a9705`; llamada en `0xffffff80021a9711`).
  Una vez recibido el 415 (`pppDone(true)` puso `peer ivars+1758 = 1` en `pppDone`), el ageout deja
  de correr para ese par. Esto corrige la tabla §3 de
  [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md): el ageout no es incondicional.
- El umbral del ageout es `>= 2001 ms` (`cmp edx, 0x7D1` equivalente: `if (ms < 0x7D1) break`, en
  el decompilado de `0xffffff80021aa7ca`), no 2000 ms exactos.

## 4. Resumen para la interfaz AWDL del RTL8814

1. `LLAValid` exige una dirección fe80::/10 en el `ifnet` de `awdl0` y sondeo activo: sin IPv6 en
   la interfaz, `handleAwdlLLATimer` reintenta cada 500 ms y `interfaceStateChange` rearma el timer
   en cada invocación con servicios válidos. En el kernel la falta de LLA no bloquea al par:
   `setPeerRoutable` escribe `priv+120 = 1` antes de mirar `LLAValid`/`AWDLOn`
   (`0xffffff80021aaf2d`) y esos dos bits solo eligen entre la log 4050 «Cannot post peer presence»
   y la log «postPeerPresence»; el bloqueo real es que AWDL no se activa (punto 2).
2. La primera activación de `AWDLOn` bit 1 pasa por `interfaceStateChange → enableAWDL`
   (después la reponen `setAwdlSyncState` y `exitQuietMode`); las fuentes del driver
   (`reportDataPathEvents`, 5/6/9/11) son caminos válidos si hay servicios AWDL válidos y
   `_bonjourTriggersAWDL != 0`, que es el valor por defecto sin los boot-args `awdl_enable` ni
   `awdl_oper_mode=1`.
3. +0xF30 corre bajo el command gate del controlador (`executeCommand`, hilo del ioctl 415); +0xF38 corre en el
   workqueue del controlador con el data-path lock (ageout) o en el borrado del par (flush). Ambos
   reciben un array de 64 punteros que el llamador libera siempre después.

## 5. Pendiente

- Emisores de las fuentes 0, 1, 3, 8, 10, 12 y 13 de `StateChangedSource` (§1.4).
- Significado de `ivars+4` (1 por defecto, 3 en modo manual), que corta la decisión de activación
  (§1.3).
- El cerrojo de la ruta `removePeer` → `flushPacketsWaitingForPPP` (§3.2).
- Las llamadas a las celdas +0x180 de los objetos en `ivars base+0x530/0x538` dentro de
  `interfaceMonitor` (§3.1) y el significado del bit 5 de `PM+16096` que fija `configure` (§2.1).
