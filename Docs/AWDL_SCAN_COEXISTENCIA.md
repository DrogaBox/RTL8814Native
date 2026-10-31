# AWDL: coexistencia del escaneo de infraestructura con awdl0

Pregunta: con `awdl0` levantado y AWDL activo, ¿qué limita los escaneos de infraestructura que
airportd/WCL piden al driver? Fuente: kernelcache 25G241 (md5 `f489ff8be80e6013a1bf47561dcaef45`),
x86_64, desmontado sobre una copia de la base anotada; cada dirección citada se verificó como inicio
de función en esa copia. Lo no verificado se marca `?`. Complementa a `AWDL_CHANNEL_SEQUENCE_CONTROL.md`
(quien rinde los TLV 4/18) y `AWDL_PEERMANAGER_SOFTWARE.md` (elección y temporizadores).

## 1. Resumen

El escaneo de infraestructura no lo rechaza el driver: lo filtra la familia antes de llegar a él y,
una vez en marcha, es el `IO80211AWDLPeerManager` quien se aparta. Las capas que intervienen, en orden:

1. `IO80211ScanManager` valida, deduplica y consulta la política AWDL/NAN; puede devolver `EBUSY`.
2. WCL (capa común del kernel, clases `WCLScanManager*`) encola, rechaza si hay otro scan en curso o
   actividad incompatible (join, IP, reassoc, BT, señal baja) y convierte la petición al formato del driver.
3. El driver (Broadcom u otro) recibe el scan y lo ejecuta a su ritmo; la familia no lo mide por canal.
4. El peer manager se entera por `scanStarted`/`scanDone` (y por eventos del firmware en Broadcom),
   superpone el scan sobre su secuencia de canales (ranuras a cero) o lo sube como *piggyback scan* al
   firmware, y restaura el modo datos al terminar.

Para un driver de terceros la consecuencia práctica: el scan llega igual con AWDL activo (ya filtrado
por las puertas anteriores), y el límite real del hardware mono-canal lo pone el propio driver, que
debe intercalar el retorno al canal AWDL entre canales de scan y comunicar los huecos en los TLV 4/18.

## 2. Ruta de una petición de scan

airportd emite el comando apple80211 de scan (tipo 10) sobre la interfaz de infraestructura; el puente
del kernel es `setSCAN_REQ` (`0xffffff80022874aa`), con la ruta alternativa de user client
`setScanRequest` (`0xffffff800235650c`), que copia al kernel `min(len, 5528)` bytes.

`setSCAN_REQ` (`0xffffff80022874aa`):

1. `IO80211Controller::getPrimaryInterfaceScanManager` → sin manager devuelve 6.
2. `IO80211ScanManager::isScanAllowedByP2P` (`0xffffff80022e1402`); distinto de 0 corta aquí.
3. Reenvío primero a WCL: `IO80211Glue::sendIOUCToWcl(glue, 0x802869C8, 10, scan_data, 5528)`.
   `IO80211SkywalkInterface::routeIoctlToWcl` (`0xffffff80022f9bf6`) devuelve `0xE082280F` si no hay
   glue (`iface+0x120 → +0xD8`) o si WCL no reclama el comando.
4. Si WCL no lo reclamó (`0xE082280F`), cae al puente directo `apple80211setSCAN_REQ`
   (`0xffffff8002267ac8`): comprueba la ranura +0xCC8 de la interfaz con el tipo 10
   (`IO80211SkywalkInterface::isCommandProhibited`, `0xffffff80022f9bce`, que delega en la ranura
   +0xC50 del controller), exige `safeMetaCast` a `IO80211NoneProtocol` y salta a su ranura +0x1B0.
   Sin ancestro `IO80211NoneProtocol` devuelve `0xE082280E`. La jerarquía está cerrada por los
   constructores de metaclase: `IO80211NoneProtocol::MetaClass` (`0xffffff8002186332`) desciende de
   `OSObject` (tamaño 0x10) y `IO80211InfraProtocol::MetaClass` (`0xffffff80022ca72c`) desciende de
   `IO80211InfraInterface` (tamaño 0x130): un driver `IO80211InfraProtocol` no pasa el cast a
   `NoneProtocol`, de modo que su scan entra por la vía WCL (§7).
5. Errores del envío: `0xE0820447` → `scanDone(9, ...)`, `0xE0820448` → `scanDone(7, ...)`,
   otros → `scanDone(8, code)`.

`IO80211ScanManager::startScan` (`0xffffff80022e16a8`) añade, antes del envío:

- `validateScanData` (`0xffffff80022e107a`): `ssid_len` (+0x10) < 33, `num_channels` (+0x44) < 401 y
  `num_networks` (+0x1314) < 11; si no, 22 (BADARG).
- Deduplicación: `searchForPendingAndClearPid` (`0xffffff80022e0be2`) busca un scan pendiente del mismo
  pid; si es de hace <21 s y los 5528 bytes son idénticos, el nuevo se cuelga como esperador del
  anterior (coalescing); si supera 21 s, se borra el nodo caducado.
- Nodos: `_allocScanReqNode` (`0xffffff80022e0a8e`) copia el `apple80211_scan_data` (5528 B) o el
  `apple80211_scan_multiple_data` (5352 B) en un nodo de 10936 B; máximo 400 canales por petición.
- `IO80211Controller::scanStarted` (`0xffffff8002294794`) avisa a los peer managers.
- Genera un UUID, lo escribe como cadena en `scan_data+0x154C` y lo guarda en el controller.

El scan de tipo múltiple (`fastScanMultiple`, `0xffffff80022e0df4`) consulta primero la caché de
resultados (`getCachedScanResults`) y sólo encola nodo si la caché no responde; `fastScan`
(`0xffffff80022e09f2`) es pura caché.

## 3. Las puertas AWDL de la familia

### 3.1 isScanAllowedByP2P (`0xffffff80022e1402`)

- NAN: si existe `IO80211NANPeerManager`, consulta `isInfraScanAllowed(flags bit 0x20)` y tres bits de
  estado; fallarlos devuelve 16 (el log dice "returning EBUSY").
- AWDL: llama a `isScanDisallowedByAwdl` (§3.2). Si AWDL o NAN bloquean y `scanType != 3`, log
  `"IO80211ScanManager::startScan AWDL %d, NAN %d returning EBUSY"` y devuelve 16.
- Si `scanType` es 5 o 6, reescribe el tipo en `scan_data+0x34`: pasa a 2, y a 1 cuando el canal
  pedido (`scan_data+0x48..`, entradas de 12 B) resulta DFS según
  `IO80211AWDLPeerManager::isDFSChannelInfo`.

### 3.2 isScanDisallowedByAwdl (`0xffffff80022e111e`)

Obtiene el `IO80211AWDLPeerManager` por `safeMetaCast` desde `controller→+0x190→+0x120→+0x28`.
Acepta `apple80211_scan_data` o `apple80211_scan_multiple_data` (+512/+528/+5336). Lee `scanType`
(+0x34 / +0x200), número de canales (+0x44 / +0x210) y flags (+0x1308 / +0x14D8). Con el peer manager
presente calcula, sobre los ivars (+0x20) del manager:

- `RTModeScan` = bits +0x3F1C.0 / +0x3F32.0 / +0xFBA2.0 / +0x40D8.0 y `scanType != 5` `?` (ivars sin
  nombre simbólico; por el log de abajo, política de modo real-time).
- `NonSteering` = `bssSteeringInProgress() != 0` o (+0x3EE0 bit1 y +0x4200.0), con `scanType != 6`.
- `assistDisc` = +0xFA3A.0; `HTMode` = +0x3F30.0.
- Estado `AWDL-restore` (+0xFF20) distinto de 0 corta con "permitido" (log "AWDL-restore state:%d").
- Sólo si (`RTModeScan` ∨ `NonSteering` ∨ `assistDisc` ∨ `HTMode`) aplica la restricción: prohibido
  salvo que `flags & 0x20` esté puesto, o `scanType == 6`, o (`deviceAllows2GOnlyScan()` y todos los
  canales de la lista son ≤ 14 (2,4 GHz; recorre las entradas de 12 B comparando el dword del canal)).
  Fuera de esos modos el scan simplemente continúa.
- El log de decisión (`0xffffff80022e13a8`, línea 1148) imprime
  `"InfraScanAllowed %d (RTModeScan %d NonSteering %d assistDisc %d HTMode %d RTModeNeeded %d Immin %d
  ScanType %d Flags %llu ScanOn2GOnly %d DevAllows2G %d)"`.

El bit 0x20 de los flags es pues el marcador "scan compatible con AWDL en modo real-time": los scans
que el propio peer manager lanza lo llevan (`scanForPeersBSSIDOnDFSChannel` (`0xffffff80021f043a`)
construye un `apple80211_scan_data` con `flags |= 0x28`, `scanType` 5, un canal, y lo mete por
`startScan`; reintenta a los 200 ms si falla).

### 3.3 Otras

- `isOpModeScanFriendly` (`0xffffff80022e1dac`): lee `OP_MODE` por WCL (`0xC02869C9` sub 0xF) o, si
  WCL no responde, con `apple80211getOP_MODE`, y devuelve falso si el modo tiene puestos los bits
  0x1800000000. `startScan` rechaza entonces con "Rejecting scan request because of interface's
  operational mode", y `WCLScanManager::sendRequest` tiene su propia variante del mismo mensaje.
  Un driver que anuncie esos bits en `OP_MODE` se queda sin scans.
- WCL mantiene además un rechazo por scan en curso: `WCLScanManager::handleScanRequest`
  (`0xffffff800219c882`) devuelve 16 con "scan in progress rejecting" cuando ya hay petición en
  progreso, y `scanRequestHandler` (`0xffffff800219a4a6`) valida longitud 5528 y sanea con
  `IO80211ScanSanitizer::validateAndSanitizeScanData` (`0xffffff80022d976a`).

## 4. WCL: actividad incompatible y ritmo

`WCLScanManager::isScanAllowedByOtherActivity` (`0xffffff800219b9c2`), llamado desde
`sendRequest` (`0xffffff800219b672`), rechaza el scan (con su código y un motivo en la máscara del
llamador) si:

- GAS manager ocupado (16), NDD ocupado (`0xE0820442`), join manager ocupado (`0xE0822401`).
- Resolución de IP en curso (`0xE0820442`), salvo que lleve >10 s en ese estado.
- Reasociación de alta prioridad con menos de 3 s (0xBB7 ms).
- Roaming por RSSI bajo en curso, salvo scan `LOW_LATENCY` (flag 0x20 en `+0x1310` de la petición WCL).
- Supresión por pérdida de enlace o por deautenticación (`0xE0820442`).
- Asociado esperando IP (`0xE0820447`); BT ocupado con una llamada (`0xE0820447`); zona de señal baja
  (RSSI ≥ −90 y por debajo del umbral de roam, `0xE0820447`).

`sendRequest` convierte la petición con `WCLScanRequest::getScanRequestForDriver`
(`0xffffff80022fe1de`) → `IO80211ScanRequest::getScanRequestForDriver` (`0xffffff80022eb05e`) y la
manda al controller como comando de user client 441 con 5456 bytes.

`updateScanTimeout` (`0xffffff800219c1b4`): el timeout del scan es 20000 ms; con AWDL activo, ≥100
canales (u16 en +0x44) y el canal de la primera entrada (+0x4C) fuera de 1..20 pasa a 60000 ms con el log `"Long scan while AWDL is enabled, giving more time (60000)"`; un
flag de dispositivo (+0x40) añade 10000 ms. `timeoutScan` (`0xffffff800219a15c`) dispara el evento 6
de la FSM al vencer. Ese es el único límite temporal del lado anfitrión; nada en la familia mide
canal a canal.

## 5. AppleBCMWLAN con AWDL encendido

- `AppleBCMWLANCore::setWCL_SCAN_REQ` (`0xffffff80016758ae`): recibe el `apple80211ScanRequest`
  (5456 B; nº de canales en +0x54, entradas de 12 B con el canal en +0x5C). Si alguna entrada es de
  2,4 GHz (canal ≤ 13), llama a `checkBackToBackHostScans` (`0xffffff8001675906`), que sólo acumula
  contadores de telemetría de scans consecutivos (<3 s de ventana): no rechaza nada.
- `AppleBCMWLANScanAdapter::startScan` (`0xffffff800174929e`): rechaza el escan con `0xE00002D5`
  ("Action frame in progress. Rejecting escan request!") si hay trama de acción en vuelo; aplica el
  *home/away time* del request (+0x8) con `setScanHomeAwayTime` (`0xffffff80017490ea`, log
  "Setting Scan Home away time to %u", antes de cada scan); MAC aleatoria de scan si el bit +0x4 lo
  pide; y lanza `startEventScan` si el modo es 2.
- `startEventScan` (`0xffffff80017493dc`, builder `fillScanParams2`): construye el escan del firmware
  con dwell activo = `req+0x48` (40 ms por defecto, 20 ms si se está asociado), dwell pasivo =
  `req+0x4C` (110 ms por defecto), convierte cada canal a chanspec de 2 bytes con `getChanSpec` y, si
  la petición no trae canales, usa una lista por defecto de 13 chanspecs (`kDefaultScanList`). No hay
  lotes del lado anfitrión: el escan entero va en un comando y el firmware reporta por eventos
  (`handleEventScanStart` (`0xffffff800174fc30`) valida dos "slices" de canales del evento;
  `handleEventScanAdd`; `eventScanComplete` (`0xffffff800174cb06`)).
- El firmware es quien conoce AWDL (ver `AWDL_FIRMWARE_DUTIES.md`): el driver recibe
  `WLC_E_AWDL_SCAN_START`/`WLC_E_AWDL_SCAN_DONE` en `AppleBCMWLANProximityInterface::handleEvent`
  (`0xffffff8001715ece`) y registra "AWDL turned off while in a roam scan"/"OOB scan" en
  `doSetSyncState` (`0xffffff8001719e38`). El retorno al canal propio entre canales de scan lo hace el
  firmware `?` (no aparece lógica de intercalado en el host más allá del home/away time).
- `AppleBCMWLANCore::isScanAllowed` (`0xffffff8001675ada`): suprime scans con el bit 0x20 de
  `+0x10419` ("Scans suppressed"), con BT en llamada y en zona de señal baja; llamado desde
  `isScanResumeAllowed`.

## 6. El peer manager: aviso, reacción y efecto sobre los pares

Aviso genérico (independiente del driver): `IO80211Controller::scanStarted` (`0xffffff8002294794`) y
`scanDone` (`0xffffff8002294848`) invocan la ranura +0xB90 de hasta tres objetos peer manager
(AWDL en `+0x190`, NAN en `+0x3B0`, y el que da `+0xC80`) con `(scanSource, started/ended,
scan_data, status)`. En AWDL aterriza en:

`IO80211AWDLPeerManager::setScanningState` (`0xffffff800221924e`), que con infraestructura conectada y
AWDL encendido (bit 1 de +0x16096) y sin `refuse_scans` (ivar +0x37192, ajustable por
`setAwdlCtl` (`0xffffff80021cde94`), que también expone `awdlRtScanAllowed`,
`sibCoexAWDLScanChanSeqActive` y dwell de niveles bajos tipo `lvl1_scans_max_dwell_time`):

- Al empezar: si el scan es 2,4 GHz puro (todos los canales ≤ 11, `usePiggybackScan`
  (`0xffffff8002219902`)) y el driver lo soporta (ivar +0x4 == 1 `?`), marca el flag 0x4 en los flags
  del scan, copia los 5528 B en `+0x10488` y arma el *piggyback scan* (el firmware lo ejecuta dentro
  de sus propias ventanas; `reportDataPathEvents` maneja su máquina de estados y al completar lanza el
  scan real por `ioctl_internal` y restaura estado). Si no, entra en estado de secuencia 24 (o 49 con
  el flag +0x65432): `overlayScanRequestOnRealTimeChannelSequence`
  (`0xffffff80021e71c2`) pone a cero (`zeroChanInfo`) hasta seis parejas de ranuras de la secuencia
  real-time para hacer hueco al scan; en modo SDB con scan 2,4 GHz y tres ranuras no-2G consecutivas
  no toca nada.
- Al terminar: "Done with Scan, going back to data mode" → `setScheduleState(5, 1, true)`, restaura el
  modo de presencia guardado (+0x17033/+0x17034) y drena los handlers DFS proxy
  (`dfspScanDoneHandler`, `0xffffff80021fd838`) y BSS steering.
- Además notifica por la vía de eventos del driver `reportDataPathEvents` (`0xffffff80021dd8f4`):
  código 125 "Wifi Infra scan request. 2.4G: %d 5G: %d", 126 "WiFi infra scan complete notification
  from FW", 137 "Roam Scan Started by FW", 243 "Wifi Infra scan add request. 6G channels added %d" y
  '>' "Received pscan begin from driver" (estado 28). Son los eventos que el firmware Broadcom emite;
  un driver de terceros `?` no tiene hoy un canal equivalente salvo `scanStarted`/`scanDone`.

Otras piezas: `scanDisabledInRTMode` (`0xffffff80021f81c4`) resume cuándo el modo real-time prohíbe
escanear; `handleAwdlScanTimer` (`0xffffff80021c2d68`) lanza los scans propios del manager (DFS proxy,
`scanType` 5; BSS steering, `scanType` 6); `updateSibCoexAWDLScanChanSeqActiveState`
(`0xffffff80021cb59c`) activa el overlay para coexistencia con sibling P2P al escanear 5 GHz;
`followPeerChannelSequence` (`0xffffff80021f6e74`) aplica el overlay también al seguir la secuencia de
un par si hay scan pendiente (contador `+0x2472` del contenedor), y si no fija estado 9.

Efecto sobre los pares: la secuencia efectiva (con las ranuras a cero del overlay) es la que se
renderiza en los TLV 4 y 18, que el driver emite (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §"fallos y
plantillas"); los pares ven, pues, disponibilidad reducida durante el scan y nada de anuncio explícito
de "ausencia" `?` (no se encontró ningún TLV ni mensaje de unavailability asociado a scan).

## 7. Lo que recibe un controlador de terceros

Dos formatos según la vía:

1. **Vía WCL (la de un driver `IO80211InfraProtocol`, el caso del kext AirLayer).** El puente
   `apple80211setWCL_SCAN_REQ` (`0xffffff800226bee2`, tabla de descriptores de comandos en
   `0xffffff800247eeb0`, junto a `setWCL_ASSOCIATE` y demás puentes `WCL_*`) comprueba
   `isCommandProhibited(441)` (ranura +0xCC8), exige cast a `IO80211InfraProtocol` y salta a su ranura
   **+0x118** con un `apple80211ScanRequest` de 5456 bytes: home/away time en +0x8, tipo de scan para
   el firmware en +0x40 (1 activo, 2 pasivo, 3 alta precisión, 4 bajo consumo; 5+ inválidos), flags de
   byte en +0x44 (0x1 low-power scan core, 0x2 baja prioridad, 0x4 resultados offchannel, 0x20 sonda
   6 GHz, 0x40 FILS, ...), dwell activo +0x48 y pasivo +0x4C, nº de canales +0x54 y entradas de 12 B
   desde +0x58 (banda, canal, flags: 0x8 2G, 0x10 5G, 0x20 6G) hasta 400. La conversión completa desde
   el scan original está en `IO80211ScanRequest::getScanRequestForDriver` (`0xffffff80022eb05e`), con
   bits de política del WCL (`WCLScanRequest::getScanRequestForDriver`, `0xffffff80022fe1de`): baja
   prioridad automática si puede abortar un roam, paso a low-power scan core si se espera IP, etc.
2. **Vía directa (sólo ancestros de `IO80211NoneProtocol`).** `apple80211setSCAN_REQ`
   (`0xffffff8002267ac8`), ranura +0x1B0 con el `apple80211_scan_data` de 5528 bytes intacto.

El `apple80211_scan_data` (5528 B) que la familia valida y comparte con el peer manager:

| Offset | Contenido |
|---|---|
| +0x00..+0x0F | filtro BSSID de scan dirigido (el peer manager escribe ahí BSSID y canal del par) `?` |
| +0x10 / +0x14 | longitud del SSID (<33) y SSID |
| +0x34 | `scanType`: 1 activo, 2 pasivo, 3 exento del gate P2P, 5 y 6 reservados a scans del propio AWDL |
| +0x44 | número de canales (<401) |
| +0x48 | entradas de 12 B: banda (dword), canal (dword), flags (0x8 2G / 0x10 5G / 0x20 6G) |
| +0x1308 | flags u64: 0x2 baja prioridad, 0x8 resultados offchannel, 0x20 "permitido con AWDL"/LOW_LATENCY, 0x100 sonda 6 GHz, 0x200 FILS, 0x800 RNR, 0x1000 sólo low-power scan core |
| +0x1314 | número de redes (<11) |
| +0x154C | UUID del scan, escrito por `startScan` como cadena |

No hay ningún campo de dwell por canal en este formato: el dwell lo elige el driver (Broadcom recibe
40/110 ms por defecto en su formato propio). Resultados: el driver termina y la familia
(`IO80211ScanManager::scanDone`, `0xffffff80022e2fa2`, disparada por `postMessage` desde
`IO80211InfraInterface::postMessage` (`0xffffff80023614b8`)) vuelca los resultados con
`queryScanResult` (`0xffffff80022e2e90`; con el scan aún en curso devuelve `0xE0820445`
"Scan is still in progress, query on the next scan done") a la caché, y airportd los lee después con
`getSCAN_RESULT` (`0xffffff8002279dbc`). Nadie cronometra canales individuales: la única presión
temporal es el timeout WCL (§4), el `ABORT_SCAN` de airportd (puente del selector 381) y la deduplica
de 21 s para peticiones nuevas del mismo pid.

## 8. Campos que permiten volver al canal AWDL entre canales

- El scan es asíncrono por contrato: `setSCAN_REQ`/441 vuelve en el acto y los resultados llegan al
  final. Entre el arranque y `scanDone` el driver hace lo que quiera con el canal; la familia sólo
  impone los timeout del §4 (decenas de segundos) y el bit de "scan en curso" del WCL.
- El bit 0x20 del flags de scan (y la restricción a 2,4 GHz que le acompaña) marca los scans que la
  política AWDL ya declaró compatibles: para esos, la familia ni siquiera exige rapidez extra
  (`isScanDisallowedByAwdl` los deja pasar siempre).
- Las entradas de canal traen banda y flags por canal, de modo que el driver puede ordenar la lista y
  agrupar los canales que comparten desplazamiento respecto de su canal AWDL.
- El home/away time (tipo 446, puente `apple80211setWCL_SET_SCAN_HOME_AWAY_TIME`
  (`0xffffff800226c041`), ranura +0x12E8 de `IO80211InfraProtocol`) es el mecanismo previsto para
  decirle al driver cuánto tiempo volver al canal propio durante el scan; Broadcom lo aplica por scan
  (`setScanHomeAwayTime`). Si airportd lo emite también frente a un driver de terceros, el kext puede
  usarlo como semilla de su propio intercalado `?` (el stub del kext hoy lo ignora).
- El `scanType` y el UUID (+0x154C) permiten distinguir scans internos de AWDL (5/6) de los de
  airportd y correlacionar inicio/fin.

## 9. Recomendación para un driver USB de un solo canal

1. Mantener el scan por la vía WCL: responder 0 a `isCommandProhibited` (ranura +0xCC8) para los
   tipos 10, 441 y 446 y recibir el `apple80211ScanRequest` en la ranura +0x118, como hace hoy
   `RTLAirInterface::setWCL_SCAN_REQ`. No hay que implementar el puente `NoneProtocol` (+0x1B0).
2. No rechazar nunca un scan por estar AWDL activo: para eso están `isScanDisallowedByAwdl` y WCL,
   que ya filtran (o fuerzan 2,4 GHz, o exigen el flag 0x20). Un rechazo local duplicaría la política
   y rompería los scans que la familia ya dio por compatibles.
3. Autopacing: partir la lista de canales en tandas cortas (p. ej. dwell de 20-40 ms por canal) y
   volver al canal AWDL (el de aparcamiento o el de la ventana servida siguiente) entre tandas y
   durante cada ventana AWDL abierta. El margen lo dan los 20 s (60 s con AWDL y ≥100 canales) del
   timeout WCL y el `ABORT_SCAN` como límite duro.
4. Coherencia con los pares: mientras la radio esté fuera del canal AWDL, renderizar esas ranuras como
   canal 0 (unserved) en los TLV 4/18, exactamente lo que el overlay del peer manager hace con
   `zeroChanInfo`. Así el anuncio en el aire coincide con la realidad del radio.
5. Avisar al peer manager del ciclo del scan: `scanStarted`/`scanDone` ya lo notifican por la ranura
   +0xB90 si el driver pasa por el camino estándar de la familia (y el `M_SCAN_DONE` que el kext emite
   hoy llega a `IO80211ScanManager::scanDone`); conviene mantenerlo para que el manager entre y salga
   de los estados 24/49/5 aunque el firmware no genere eventos 125/126.
6. Para los scans 2,4 GHz puros, imitar el piggyback: son el caso que la familia marca con el flag 0x4
   y trata como inofensivo para AWDL; pueden ejecutarse completos sin proteger ventanas.

## 10. Verificación

- Todas las direcciones citadas se comprobaron como inicio de función contra la copia del IDB del
  kernelcache 25G241 (`md5` del KC `f489ff8be80e6013a1bf47561dcaef45`); los desensamblados y
  descompilados de soporte quedan en `Build/ida_awdl/task_scan_coexist/` (`p1_family_gate/out1..out9`).
- Marcados con `?`: la semántica exacta de los ivars de modo real-time (+0x3F1C, +0x3F32, +0xFBA2,
  +0x40D8, +0xFA3A, +0x3F30, +0xFF20), el filtro BSSID de los primeros 16 bytes, el soporte del
  *piggyback scan* por driver (ivar +0x4), el intercalado de home channel dentro del firmware
  Broadcom, la existencia de un canal de eventos 125/126 para drivers de terceros y el uso que haría
  airportd del tipo 446 frente a un driver no Broadcom.
