# El bloque de capacidades de tarjeta (`*(ctrl+0x120)+0xB34..+0xB4B`) en 25G241: lectores y comparación con la capa RTL

## 1. Resumen

El GET 12 (`CARD_CAPABILITIES`) del driver se copia íntegro en el objeto de tarjeta del
controlador y todos los lectores del kernel acceden a esa copia por desplazamiento directo.
Este documento lista cada byte y bit del bloque con lector en el kernel collection de
macOS 26.7.1 (build 25G241, `BootKernelExtensions.kc` md5 `f489ff8be80e6013a1bf47561dcaef45`),
qué puerta cada uno, y lo compara con lo que `RTLAirController::getCARD_CAPABILITIES`
(`Source/AirLayer/RTLAirLayer.cpp`, sólo lectura) fija hoy.

Conclusión: de todos los bits que el kernel lee, el único ausente con efecto funcional
sobre AWDL con asociación a infraestructura es **el bit `0x20` de `p[7]` (`+0xB37`,
capacidad 29, doble banda)**, ya documentado en `AWDL_SECUENCIA_CON_INFRA.md` §4.2/§9.
Los bytes `+0xB3F` (p[15], 6E) y `+0xB47` (p[23], Limited SDB/ranging) quedan aquí
cerrados: con la capa actual a 0, todas las rutas 6 GHz, pro-mode, DPS y ranging quedan
desactivadas, que es el comportamiento correcto para una radio de una banda sin 6E; la
única consecuencia de `+0xB47` es cosmética (Broadcom pone su bit 4 incondicionalmente y
el bit del TLV 30 de ranging sale a 0).

## 2. El objeto y cómo se llena

- `IO80211Controller::loadCardCapabilities` (`0xffffff800229a202`): escribe la respuesta
  en `*(*(this+0x120)) + 0xB30` — en el decompilado, `*(this+288) + 2864`. Primero
  intenta el puente WCL: `IO80211Glue::sendIOUCToWcl(0xC02869C9, 12, dst, 0x1C)`;
  si falla o devuelve `0xE082280F` (`-528340977`), llama a
  `apple80211getCARD_CAPABILITIES(iface, dst)` y registra
  `"loadCardCapabilities status=<0x%x> "`. La copia es de 28 bytes:
  versión u32 en `+0xB30..+0xB33` y los 24 bytes de capacidades en `+0xB34..+0xB4B`.
- `apple80211getCARD_CAPABILITIES` (`0xffffff80022629e6`): llama la celda 24 de la
  vtable de la interfaz (con el tipo 12; sólo sigue si devuelve 0), obtiene el
  controlador por la celda 21 y llama su celda `+0xCD0` (slot 410) con `(iface, buf)` —
  es decir, el `getCARD_CAPABILITIES` del driver concreto. La salida del driver queda
  tal cual en `+0xB30..+0xB4B`.
- `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800229a2b2`): el accesor
  `byte *(card + n/8 + 2868)` con bit `n & 7` (`2868 = 0xB34`). No tiene llamadores
  directos en el kernel collection: los lectores acceden al bloque directamente.
- Ruta de usuariospace: `getCARD_CAPABILITIES` (`0xffffff8002279e89`, tabla GET) llena un
  buffer local de 28 bytes por el mismo doble camino (WCL o driver) y hace
  `memmove(req, buf+4, min(req_len, 21))`: el strip de la versión de 4 bytes que
  desplaza el mapa en userspace (`Docs/ARCHITECTURE.md` «AWDL gate FOUND»).

Mapa de bytes: `+0xB34+k` es `p[4+k]` del `apple80211_capability_data` del driver, y
contiene las capacidades `8k..8k+7` (bit n = capacidad n). `p[0..3]` es la versión u32
en `+0xB30`.

La capa RTL debe seguir rellenando la versión en `p[0] = 1` y los 24 bytes
`p[4..27]`; cualquier byte que deje a 0 es un bit que el kernel ve claro.

## 3. Método

Barrido de los desplazamientos de 4 bytes `0xB30..0xB50` sobre el kernel collection
completo (826 segmentos, 1465 candidatos), filtrado por filesets WiFi
(IO80211Family/AppleBCMWLAN*/IOSkywalkFamily), verificación de la cadena de base de cada
lectura (`pm+0x18 → +0x520 → +0x120` en el gestor AWDL, `iface+0x128 → +0x168 → +0x120`
en `IO80211InfraInterface`, `peer → gestor → +0x120` en `IO80211Peer`) para excluir los
ivares homónimos de `IO80211Peer`/`IO80211PeerManager`/`state`, y decompilado de las
funciones lectoras. Los VAs citados son cabezas de instrucción verificadas contra el
análisis de la base. Los sitios con patrón en `strncmp`/`jmp`/constantes de log se
descartaron como falsos positivos.

## 4. Lectores byte a byte

«cap n» = capacidad n según el mapa del accesor. «RTL» = valor actual del byte en
`RTLAirController::getCARD_CAPABILITIES`. Las direcciones de función son los inicios.

### `+0xB34` (p[4], capacidades 0–7) — RTL `0x6F`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` (`0xffffff80021c54ea`) | `0xffffff80021c56d8` | `& 0x04` (cap 2) | Si está claro: log `"ERROR: invalid card capabilities "` y sigue (no aborta). La capa lo tiene puesto (0x6F ∧ 0x04). |

Resto del byte sin lectores. Broadcom fija la base `0x6F` igual que la capa (más
`| 0x80` con `shouldSupportTethering`).

### `+0xB35` (p[5], capacidades 8–15) — RTL `0xE6`

Sin lectores del bloque en el kernel. Broadcom añade `| 0x08` con el feature flag 118.

### `+0xB36` (p[6], capacidades 16–23) — RTL `0x61`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211InfraInterface::updateLinkSpeed` (`0xffffff8002360f36`) | `0xffffff80023610cb`, `0xffffff8002361211`, `0xffffff8002361327` | byte completo; bits 2 (cap 18) y 3 (cap 19) | Eligen la tabla VHT MCS de 40 MHz (`VHT_MCS_RATE_TABLE_40MHZ`) y de 20 MHz para la velocidad que reporta la interfaz. Con la capa actual (bits 2/3 claros) no se eligen; sólo afecta a la velocidad informada, no al aire. |

Broadcom pone el bit 3 (0x08) si la palabra `ivars+1038` tiene `0x20` y el bit 2 (0x04)
si tiene `0x40` (informe del firmware), sobre la misma base `0x61`.

### `+0xB37` (p[7], capacidades 24–31) — RTL `0x05`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211VirtualInterface::attachToBpf` (`0xffffff8002330386`) | `0xffffff80023303c6` | `& 0x01` (cap 24) | Si está puesto, `IOSkywalkNetworkInterface::bpfAttach(if, 12, 0x48)` (tap DLT 12) sobre la interfaz virtual (awdl0); las ramas P2P (`peerToPeerAttachToBpf`, roles 6/8/9/10) corren antes sin condición. La capa lo tiene puesto. |
| `IO80211InfraInterface::prepareBSDInterface` (`0xffffff800235ff64`) | `0xffffff800236003a` | `& 0x01` (cap 24) | Ídem en la interfaz de infraestructura (rol 1), tras crear el `IO80211AssociationJoinSnapshot`. La capa lo tiene puesto. |
| `IO80211AWDLPeerManager::configureValidAWDLChannels` (`0xffffff80021caa40`) | `0xffffff80021cab1e` | `& 0x20` (cap 29, doble banda) | Sin él: qword `+0x4294` (social de 5 GHz y alternativo) a 0 con el log `"Dual band not supported, so assuming 2.4GHz is the basic and should be supported, initialize 5GHz channels to zero"`; la secuencia queda sólo con el canal 6. Documentado y observado en hardware (`AWDL_SECUENCIA_CON_INFRA.md` §4.2). La capa NO lo tiene: única ausencia crítica. |
| `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` (`0xffffff8002205b94`) | `0xffffff8002205f0c` | `& 0x20` (cap 29) | Sin él no elige social de 5 GHz (149 → 44 → canal maestro) para el registro de tráfico de descubrimiento asistido. |
| `IO80211AWDLPeerManager::fillLegacyDataPathPayload` (`0xffffff80022143ca`) | `0xffffff80022143fb` | `& 0x20` (cap 29) | Base de los flags u16 del TLV 12: `0x8B23` con el bit, `0x8B03` sin él. |
| `IO80211AWDLPeerManager::printDataPath` (`0xffffff80022239d4`) | `0xffffff8002223aa2` | byte completo | Volcado de depuración. |

Broadcom: `| 0x20` con feature flag 28 (doble banda); `| 0x01` en la rama AWDL
(flag 2 == 0, la misma rama que `p[8]` bit 0, ver `Docs/ARCHITECTURE.md`
«AWDL gate FOUND»); `| 0x80` con flag 27; `| 0x04` de base. La capa fija `0x04 | 0x01`.

### `+0xB38` (p[8], capacidades 32–39) — RTL `0x01`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| wifip2pd (userspace) | `sub_100047660` (`AWDL_STRUCTS.md` §1: `0x1000483b5`) | bit 0 (cap 32) | Puerta de creación de `awdl0` (`VIRTUAL_IF_CREATE`). La capa lo tiene puesto. |

Ningún lector de este byte en el kernel: los bits 1/2 (caps 33/34) que Broadcom pone
con los flags 21+fw y 27 no tienen consumidor hallado en el kernel collection. Los
accesos `[obj + i*4 + 0xB38]` de `IO80211PeerManager` (`resetTxPathHealthCheck`,
`interfaceMonitor`, `preQueuePacket`, …) son contadores propios del gestor de pares, no
el bloque de tarjeta: sin cadena `+0x120`, descartados.

### `+0xB39` (p[9], capacidades 40–47) — RTL `0x40`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5804` | bit 7 (cap 47) con `+0xB3C` bit 7 | Si AMBOS están claros: log `"Dynamic country code not supported on this device"` y `sub+0x6FC = 0` (`sub` = `*(this+0x18)`; `initParamsToDefaults` la pone a 1 en `0xffffff80021c7a95` y `NANLocaleManager::configure` también a 1 en `0xffffff800229f4ea`, y las dos `configure` la bajan a 0 con la misma condición). Es el flag de código de país aprendido de un par: su único lector, `IO80211PeerManager::setCountryCodeState` (`0xffffff800225b5e6`, llamado desde `configureValidAWDLChannels` y `NANLocaleManager::configure`), sólo con la celda ≠ 0 y el país del host vacío o `X…` pasa al estado 2, «pedir el código de país a un par P2P». La capa los tiene claros: con un país válido por GET 8 la rama no se toma; sin país, AWDL no lo aprende de un iPhone. |
| `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` (`0xffffff800225b736`) | `0xffffff800225b74c`, `0xffffff800225b755` | bit 7 (cap 47) o `+0xB3C` bit 7 (cap 71) | Devuelve 1 si cualquiera de los dos está puesto. |
| `IO80211NANLocaleManager::configure` (`0xffffff800229f4d2`) | `0xffffff800229f502`, `0xffffff800229f50b` | bits 7 (caps 47/71) | La misma condición de código de país dinámico, camino NAN. |
| `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80023612f3` | bit 3 (cap 43) | Elige `VHT_MCS_RATE_TABLE_80MHZ`. Broadcom lo pone con informe de firmware (`ivars+1024 >= 128` y `ivars+1066 & 0x20`). Claros en la capa: cosmético. |

Broadcom añade bits a este byte con los flags 1 (`| 0x01`) y 29 (`| 0x80`), y `| 0x20`
bajo otra condición no detallada, sobre la base `0x40` que la capa copia.

### `+0xB3A` (p[10], capacidades 48–55) — RTL `0x14`

Sin lectores del bloque en el kernel. Las puertas de este byte están en userspace:
Apple80211IsAuthSelAllowed/_getMFPCaps leen los bits 0/4 (`Docs/PHASE5_IO80211.md` §432,
cap 52 = PMF; la capa añadió `0x10` por eso) y la base `0x04` de Broadcom es
incondicional. Broadcom añade `| 0x08` (cap 50) en la rama AWDL y `| 0x20`/`| 0x40` con
flags 39/46.

### `+0xB3B` (p[11], capacidades 56–63) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c585e` | `& 0x02` (cap 57, RSDB) | Si está: log `"RSDB is supported on this device"` y `state+0xA728 = 1`. Ese estado alimenta `buildRangingTlv` (bit `0x08` del TLV 30, `0xffffff8002212790`) y la contabilidad de `updateChanOutageTime`. Claros en la capa: correcto para una sola radio. |
| `IO80211NANPeerManager::initDefaultNANParams` (`0xffffff80022ad378`) | `0xffffff80022ad3cd` | `& 0x02` (cap 57) | Sin él: `"SDB not supported, switching to single map"` en NAN. |
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5905` | bit 7 (cap 63, CLM) | Si está: log `"CHANNEL CLM RESTRICTED CAPABILITY is supported on this device"` y `state+0xA72A = 1`. |
| `IO80211NANLocaleManager::isCardChannelCLMRestrictedCapable` (`0xffffff80022a009c`) | `0xffffff80022a00b9` | `>> 7` (cap 63) | Getter. |
| `IO80211NANLocaleManager::isChannelValid` (`0xffffff800229fb72`) | `0xffffff800229fbcf` | bit 7 (cap 63) | Con el bit claro el canal se da por válido para este filtro; con el bit puesto se exige que las propiedades no traigan ni radarDFS (`0x1`) ni CLM restringido (`0x100`), y si lo traen: `"Channel %d is invalid. CLM restricted is %s and Radar DFS is %s"`. |
| `IO80211InfraInterface::updateChanOutageTime` (`0xffffff8002362cf4`) | `0xffffff8002362d21` | `& 0x02` (cap 57) | Lee el bit RSDB para el recuento de cortes por canal; el uso exacto posterior no se ha seguido `?`. |

### `+0xB3C` (p[12], capacidades 64–71) — RTL `0x01`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5957` | `& 0x08` (cap 67, DFS proxy) | Si está: log `"DFS Proxy (AWDL on DFS) is supported on this device"`, `state+0x4019 = 1`, `state+0x401A = 1` y una llamada vía `state+0x12C0`. La capa lo tiene claro: sin proxy de firmware, activarlo fingiría una capacidad que no se puede cumplir. |
| `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80022ad468` | `& 0x08` (cap 67) | Ídem en NAN (`nan+26 = 1`). |
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c580d` | bit 7 (cap 71) | Con `+0xB39` bit 7: código de país dinámico no soportado (ver `+0xB39`). |
| `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff800225b755` | `>> 7` (cap 71) | Segunda mitad del getter. |

### `+0xB3D` (p[13], capacidades 72–79) — RTL `0x0A`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c58b3` | `& 0x40` (cap 78, concurrencia) | Si está: log `"Concurrency supported"` y `state+0xD066 = 1`. Los lectores de `state+0xD066` no se han seguido `?`; con la capa clara no se activa. |
| wifip2pd (userspace) | `0x10004812a`, `0x10004826d` | bit 0 (cap 72, NAN) | Puerta NAN/NAN Data. La capa lo deja claro a propósito. |
| airportd (userspace) | — | bit 3 (cap 75, SAE) | `"WPA3-SAE security not supported for this platform"` si falta (`Docs/PHASE5_IO80211.md` §432). La capa lo tiene puesto (`0x08`). |

### `+0xB3E` (p[14], capacidades 80–87) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211InfraInterface::createLinkQualityMonitor` (`0xffffff8002366426`) | `0xffffff8002366555` | `& 0x08` (cap 83) | Sin él: `"LinkQualityMonitor not supported"` y `0xE00002C7` — no se crea el monitor de calidad de enlace de la asociación infra (síntomas slow-wifi). Sólo diagnóstico; inofensivo para AWDL. |

Broadcom fija varios bits de este byte con flags 88/25/etc. (`| 0x80`, `| 0x40`,
`| 0x10`, `| 0x08`, `| 0x04`, `| 0x02`, `| 0x01`).

### `+0xB3F` (p[15], capacidades 88–95) — RTL `0x00` — el byte «6E»

Bit 7 (cap 95, AWDL 6E):

| Lector | VA | Efecto |
|---|---|---|
| `IO80211PeerManager::is6ECapable` (`0xffffff800225f8fa`) | `0xffffff800225f910` | Devuelve el bit 7 (`>> 7`). |
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5a39` | Byte ≠ 0: log `"supported AWDL 6E"`. |
| `IO80211AWDLPeerManager::buildAwdlDataPathStateTlv` (`0xffffff8002210e5a`) | `0xffffff8002210f14` | `>> 7` → bit 11 de las flags extendidas del TLV 12. |
| `IO80211AWDLPeerManager::buildPrefChanTlv` (`0xffffff8002211416`) | `0xffffff800221144a`, `0xffffff80022115f5` | El TLV 33 (canal preferido 6 GHz) sólo se construye si el byte es negativo (bit 7 puesto). |

Bit 6 (cap 94, 6E infra):

| Lector | VA | Efecto |
|---|---|---|
| `IO80211PeerManager::isInfra6ECapable` (`0xffffff800225f91c`) | `0xffffff800225f932` | `& 0x40`. |
| `IO80211AWDLPeerManager::enableAWDL` (`0xffffff80021dcf58`) | `0xffffff80021dcfce` | Fija el timeout de espera en 35000 ms con el bit, 25000 ms sin él. |
| `IO80211AWDLPeerManager::monitorAWDLState` (`0xffffff80021ed724`) | `0xffffff80021ed766` | Ídem (35000/25000 ms). |
| `IO80211AWDLPeerManager::getScanTimeout` (`0xffffff80021f3524`) | `0xffffff80021f353a` | Ídem. |
| `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80022ad456` | Byte del estado NAN. |

Bits 4 y 3 (caps 92/91, DPS):

| Lector | VA | Efecto |
|---|---|---|
| `IO80211PeerMonitor::checkForDPS` (`0xffffff800237e7f4`) | `0xffffff800237fe49` | `& 0x10`: sin él la recuperación DPS se salta (`"DPS recovery disabled"`). |
| `IO80211PeerMonitor::recordDPSSymptomsInput` (`0xffffff800237ff60`) | `0xffffff800238016f` | `& 0x08`: sin él no se publica el mensaje `"DPS Symptoms trigger posted message to userspace"`. |

Byte completo ≠ 0 (cualquier bit del byte activa estas rutas): el grupo 6G/pro-mode y los
registros de tráfico del gestor AWDL — `updatePref6GChannel` (`0xffffff80021c8882`,
`0xffffff80021c88b3`), `findCommon6GChannel` (`0xffffff80021fa150`, `0xffffff80021fa183`),
`is6GChannelALlowedForP2P` (`0xffffff80021f5fc0`, `0xffffff80021f5fe8`),
`is6GChanSeqAllowed` (`0xffffff80022379dc`, `0xffffff80022379f2`),
`isProModeAllowed` (`0xffffff8002238248`, `0xffffff800223827d`),
`shouldEnterProMode` (`0xffffff800223840a`, `0xffffff8002238435`),
`enteringProMode` (`0xffffff800223878c`, `0xffffff80022387c5`),
`monitorProModeState` (`0xffffff80021f1804`, `0xffffff80021f185b`),
`processPeerProModeTlv` (`0xffffff8002236dc8`, `0xffffff8002236dfa`),
`processPeerProModeUMIStatus` (`0xffffff80021e9760`, `0xffffff80021e9790`),
`notifyPeerRemovedFromCache` (`0xffffff8002238b34`, `0xffffff8002238b6b`),
`setHighThruputState` (`0xffffff80021faa08`, `0xffffff80021faa7a`),
`setAWDLMaxBandwidth` (`0xffffff80021cde02`, `0xffffff80021cde35`),
`removePeerFromRealTimeGroup` (`0xffffff80021e8186`, `0xffffff80021e8778`),
`processAirDropEnd` (`0xffffff80021bd44e`, `0xffffff80021bd48e`),
`doMinimalAFProcessing` (`0xffffff80021b3ea6`, `0xffffff80021b4117`),
`actionFrameReport` (`0xffffff80021af6be`, `0xffffff80021af973`),
`handleAirDropTrafficReg` (`0xffffff80022087ac`, `0xffffff8002208e27`),
`setAwdlPeerTrafficRegistration` (`0xffffff800220946a`, `0xffffff800220bdb8`),
`handleMigrationTrafficReg` (`0xffffff8002206608`, `0xffffff8002206709`),
`handleAirPlaySinkTrafficReg` (`0xffffff8002207ee0`, `0xffffff8002207f4b`),
`isAirPlaySinkEnabled` (`0xffffff8002207fb4`, `0xffffff8002207fca`),
`airplayStatsPostMetrics` (`0xffffff80022044d2`, `0xffffff800220456e`),
`awdlStatsCollectCachedPeersStats` (`0xffffff8002224a1a`, `0xffffff8002224acc`),
`rtgStatsPostMetrics` (`0xffffff800222a438`, `0xffffff800222a4eb`),
`submitRTGMetricstoCoreAnalytics` (`0xffffff800222f444`, `0xffffff800222f9bc`) — todos
con la cadena `pm+0x18 → +0x520 → +0x120` verificada. La polaridad exacta de cada rama
no se ha seguido una a una `?`; el patrón común es tratar el byte ≠ 0 como
«capacidad 6E presente».

Con la capa a 0 todo el grupo queda apagado, que es lo correcto: la 8814AU no tiene
6 GHz y fingir 6E activaría rutas de secuencia 6G y pro-mode que la radio no puede
servir. No conviene poner ni siquiera los bits 3/4 (DPS): cualquier bit de este byte
enciende el grupo byte ≠ 0. Broadcom pone bits de este byte con feature flags 76/93 y
campos del firmware; las condiciones exactas están en el decompilado de
`AppleBCMWLANCore::getCARD_CAPABILITIES` (`0xffffff8001661076`) y no se copian aquí
porque ninguna puerta del kernel distingue entre ellos (todas testean byte ≠ 0 o los
bits 6/7).

### `+0xB40` (p[16], capacidades 96–103) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211InfraInterface::createLinkQualityMonitor` | `0xffffff8002366a4e` | `& 0x20` (cap 101) | `"LQM-WiFi: slow-wifi Extended CCA capabilities supported : [%hhu]"`: activa la medición CCA extendida del monitor. Diagnóstico; inofensivo. |

### `+0xB41` (p[17], capacidades 104–111) — RTL `0x00`

Sin lectores del bloque en el kernel. Broadcom lo llena con flags 105/106/107/108/109/35
y el informe de compilación del firmware (`v5 <= 0x10`).

### `+0xB42` (p[18], capacidades 112–119) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c59e4` | `& 0x01` (cap 112) | `"Per Peer Stats are supported on this device"` y `state+0xA729 = 1`. Estadísticas por par; inofensivo claro. |

### `+0xB43` (p[19], capacidades 120–127) — RTL `0x00`

Sin lectores del bloque en el kernel (los candidatos eran tablas de salto y
constantes de log). Broadcom lo llena con flags 50/32 y el informe del firmware
(`fw+240` bits 0/1/2 → `0x40/0x10/0x20`).

### `+0xB44` (p[20], capacidades 128–135) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c6373` | `& 0x40` (cap 134) y la variable de `enable_awdl_leaf` | Con ambos: `"Device prefers leaf-only operation"` y `state+0x102B0 = 1` (modo Leaf; sin ellos se escribe 0, `0xffffff80021c63be`). La variable vale 1 por defecto (`0xffffff80021c631c`) y el boot-arg sólo puede apagarla, así que basta el bit. Con la capa clara: `+0x102B0 = 0`. |

### `+0xB45` (p[21], capacidades 136–143) — RTL `0x00` — el byte «HE»

Bit 7 (cap 143, HE/Wi-Fi 6):

| Lector | VA | Efecto |
|---|---|---|
| `IO80211PeerManager::getHeCapable` (`0xffffff800225f3f0`) | `0xffffff800225f406` | Devuelve `>> 7`. |
| `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff800221470a` | Byte ≠ 0 → bit `0x100` de las flags extendidas del TLV 12 (corrección: el lector testea el byte completo, no sólo el bit 7; con el byte a 0 da igual). |

Bit 6 (cap 142, estadísticas de datos): `getLastUnicastRxLinkActivity`
(`0xffffff800225ef14`, `0xffffff800225ef2f`), `getLastUnicastTxLinkActivity`
(`0xffffff800225f4e6`, `0xffffff800225f501`), `getDataPathPeerStats`
(`0xffffff800225fb7c`, `0xffffff800225fb9f`), `updateCumDataStats`
(`0xffffff8002243178`, `0xffffff8002243196`), `updateIntervalDataStats`
(`0xffffff80022431e6`, `0xffffff8002243204`), `clearIntervalDataStats`
(`0xffffff800224323a`, `0xffffff800224325f`), `logDataStats`
(`0xffffff8002244dbe`, `0xffffff8002244ec4`), `getLastQueuePacket`
(`0xffffff8002245b90`, `0xffffff8002245bb3`), `getCumSlotDataStats`
(`0xffffff80021aead4`, `0xffffff80021aeaf7`), `getIntervalSlotDataStats`
(`0xffffff80021aeb3a`, `0xffffff80021aeb5d`), `handleMulticastTimer`
(`0xffffff80021c38c4`, `0xffffff80021c3989` y `0xffffff80021c3a45`),
`handleEnsembleTrafficReg` (`0xffffff8002202960`, `0xffffff8002203a10` y
`0xffffff8002203f52`), `logRealTimePrintStats` (`0xffffff8002238cd4`,
`0xffffff8002238d54`), `updateTxRxLatency` de `IO80211InfraInterface`
(`0xffffff8002367aa8`, `0xffffff8002367b9e`): todos `& 0x40`, gates de recuento y
métricas. Inofensivo claro.

Broadcom llena el byte con flags 114/115/116/117 (bits 1–5) y 67 (bit 7, HE). La capa
lo deja a 0 y es lo honesto: la 8814AU no es 802.11ax.

### `+0xB46` (p[22], capacidades 144–151) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211RangingManagerExt::getRangingCaps` (`0xffffff8002348958`) | `0xffffff80023489df` | `& 0x10` (cap 148) | Tras el ioctl interno 266, pone la casilla 146 de `apple80211_ranging_capabilities_t` (fine timing) sólo con el bit. Inofensivo claro. |
| `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80022ad443` | `>> 7` (cap 151) | Byte del estado NAN. |

### `+0xB47` (p[23], capacidades 152–159) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c5a83` | `& 0x20` (cap 157, Limited SDB) | Si está: `"Limited SDB is supported on this device"` y `state+0x103FC = 1`; si no, `state+0xFFC4 = 0`. Cierra el `?` de `AWDL_SECUENCIA_CON_INFRA.md` §12: el byte ajusta el estado SDB limitado, no toca ranuras de la secuencia. |
| `IO80211AWDLPeerManager::buildRangingTlv` (`0xffffff80022126f8`) | `0xffffff8002212790` | `& 0x10` (cap 156) | Bit `0x10` del TLV 30 (ranging). Broadcom pone este bit SIEMPRE (`p[23] |= 0x10` incondicional, y `| 0x40` si `ivars+32189 & 1`); el bit 5 (`0x20`, Limited SDB) no lo pone nunca su GET. Con la capa a 0 el TLV 30 saldría sin el bit `?` (el builder hoy no emite TLV 30 con las plantillas actuales). |

### `+0xB48` (p[24], capacidades 160–167) — RTL `0x00`

| Lector | VA | Test | Efecto |
|---|---|---|---|
| `IO80211AWDLPeerManager::configure` | `0xffffff80021c63c4`, `0xffffff80021c63d6`, `0xffffff80021c63e8` | bits 1/2/3 (caps 161/162/163) | Bit 1 limpia `state+0x401A` (el flag del DFS proxy de cap 67); bit 2 pone `state+0x103A0 = 1` (`0xffffff80021c63e1`; sin el bit no se escribe y queda el 0 de `initParamsToDefaults`, `0xffffff80021c83ac`); bit 3 escribe `state+0x102B1` = 1 o 0 (`0xffffff80021c63fc` / `0xffffff80021c63f3`). Antes de los tres tests, `configure` escribe el flag Leaf en `state+0x102B0` (§`+0xB44`, `0xffffff80021c63be`). Lectores en la tabla siguiente. |

Lectores de las celdas que fija `p[24]` (barrido de operandos `[reg+0x103A0]` y
`[reg+0x102B1]` en todo el kernel collection; no hay accesos a `+0x102F1`, que era una
lectura errónea de `+0x102B1`):

| Celda | Lector | VA | Uso | Con la capa (bit claro) |
|---|---|---|---|---|
| `state+0x103A0` (cap 162) | `reportDataPathEvents`, caso 127 | `0xffffff80021dded7` | Con la celda a 1, una asociación infra iniciada por driver/firmware suspende AWDL (`"Infra assoc started by driver/fw, suspend AWDL"`, `setAwdlSuspendedMode`) o pasa a `setRoamChannelSequence`. | A 0: el evento 127 no hace nada. |
| `state+0x103A0` | `reportDataPathEvents` | `0xffffff80021df09b`, `0xffffff80021df610`, `0xffffff80021df6f8` | Sólo argumento del log `"DeviceCannotRoamDuringAWDL %d RoamChanSeqInEffect %d …"`, que da nombre a la celda. | Log con 0. |
| `state+0x103A0` | `addPeerToRealTimeGroup` | `0xffffff80021e9346`, `0xffffff80021e938f` | A 1 desactiva el steering P2P y lo aplaza (`"Delaying the steering !"`). | A 0: no fuerza el aplazamiento (deciden las demás condiciones). |
| `state+0x103A0` | `monitorAWDLState` | `0xffffff80021eda2e` | A 1, con `+0x103A1` puesto, mide el tiempo desde la asociación para roaming (`"Time since infra assoc for roam"`). | A 0: rama saltada. |
| `state+0x102B1` (cap 163) | `gatherInfraStats` (llamado desde `monitorInfraTraffic`) | `0xffffff80021efdfc`, `0xffffff80021effc7`, `0xffffff80021f0121` | Fuente de los contadores de tráfico infra: a 1, un objeto que devuelve el controlador por su ranura vtable `+3200` (Broadcom); a 0, los canales IOReporter del `IO80211InterfaceMonitor` del controlador (`IfTxPkBe`, `IfTxPkBk`, `IfTxPkVo`, `IfTxPkVi`, …). | A 0: lee el `InterfaceMonitor`. Si la capa no alimenta esos canales, AWDL ve el tráfico infra a 0 `?`. |

`state+0x102B0` (modo Leaf, §`+0xB44`) lo leen `setElectionMetricsToDefault`,
`setAwdlElectionMetric`, `setElectionMetricForAirplaySink` y
`setElectionMetricForNonAirplaySink`; no depende de `p[24]`.
| `IO80211AWDLPeerManager::isProMode5Enabled` (`0xffffff8002236d76`) | `0xffffff8002236d9e` | `& 0x10` (cap 164) | Habilita el pro-mode 5. Broadcom lo pone junto a `p[15]` bit 7 (flag 93). Inofensivo claro. |

### `+0xB49`, `+0xB4A`, `+0xB4B` (p[25..27], capacidades 168–191) — RTL `0x00`

Sin lectores en el kernel. Broadcom tampoco pone nunca nada: sus qwords de puesta a
cero cubren `p[20..27]` y no hay stores posteriores. Iguales en ambas capas.

## 5. Puertas de userspace (referencia)

Las que no pasan por el bloque `+0xB34` pero consumen el mismo GET (vía
`getCARD_CAPABILITIES` `0xffffff8002279e89` con el strip de 4 bytes): wifip2pd AWDL =
userspace byte 4 bit 0 = `p[8]` bit 0; wifip2pd NAN = byte 9 bit 0 = `p[13]` bit 0
(`AWDL_STRUCTS.md` §1, `Docs/ARCHITECTURE.md` «AWDL gate FOUND»); airportd SAE =
capacidad 75 = `p[13]` bit 3 (`Docs/PHASE5_IO80211.md` §432); PMF = userspace byte 6
bits 0/4 = `p[10]` (`Docs/PHASE5_IO80211.md` §432-433). La capa cubre las cuatro como
conviene (AWDL sí, NAN no, SAE sí, PMF sí).

## 6. Qué falta en `RTLAirController::getCARD_CAPABILITIES` y qué importa

Estado actual (leído del árbol de trabajo de la otra sesión): `p[4..5] = 0xE66F`,
`p[6] = 0x61`, `p[7] = 0x05`, `p[8] = 0x01`, `p[9] = 0x40`, `p[10] = 0x14`,
`p[12] = 0x01`, `p[13] = 0x0A`, resto 0.

| Bit | Lectores que lo piden | Veredicto para AWDL con infra |
|---|---|---|
| `p[7]` bit `0x20` (cap 29, doble banda) | `configureValidAWDLChannels` `0xffffff80021cab1e`, `handleAssistedDiscoveryTrafficReg` `0xffffff8002205f0c`, `fillLegacyDataPathPayload` `0xffffff80022143fb` | **CRÍTICO y único.** Sin él no hay social de 5 GHz: secuencia sólo con el 6, asistencia de descubrimiento sin 5 GHz, TLV 12 con base `0x8B03`. Ya observado en hardware; el parche `~/re_cache/work/dualband_cap.patch` (`p[7] |= 0x20`) lo cubre. |
| `p[6]` bits `0x04/0x08` (caps 18/19) y `p[9]` bit `0x08` (cap 43) | `updateLinkSpeed` | Opcional. Sólo eligen tablas VHT MCS 40/20/80 para la velocidad reportada de la asociación. Añadirlos haría la velocidad más fiel en 40/80 MHz; sin ellos la velocidad informada cae a la tabla base. No toca el aire ni AWDL. |
| `p[14]` bit `0x08` (cap 83) y `p[16]` bit `0x20` (cap 101) | `createLinkQualityMonitor` | Opcional/diagnóstico. Sin cap 83 la infra no crea el monitor de calidad de enlace (síntomas slow-wifi); con la capa actual simplemente no hay LQM. |
| `p[12]` bit `0x08` (cap 67, DFS proxy) | `configure` `0xffffff80021c5957` | Dejarlo claro. Fingir proxy DFS activa `state+0x4019/0x401A` y una ruta que espera que el firmware atienda AWDL sobre canales radar; la 8814AU no lo hace. Con infra no-DFS (153) no falta. |
| `p[11]` bits `0x02` (cap 57, RSDB) y `0x80` (cap 63, CLM) | `configure`, `initDefaultNANParams`, `isChannelValid`, `updateChanOutageTime`, `buildRangingTlv` | Dejarlos claros. Una sola radio no es RSDB; CLM es una restricción regional adicional. |
| `p[15]` (todo el byte, caps 88–95) | grupo 6E/pro-mode/DPS (§`+0xB3F`) | Dejarlo a 0. La 8814AU no tiene 6 GHz; cualquier bit del byte enciende además el grupo byte ≠ 0 (6G/pro-mode). El coste de dejarlo claro es sólo el par de timeouts 25000/35000 ms y los síntomas DPS. |
| `p[21]` (caps 136–143, HE y stats) | `getHeCapable`, `fillLegacyDataPathPayload`, gates de stats `& 0x40` | Dejarlo a 0. HE sería falso; el bit 142 sólo recorta métricas. |
| `p[22]` bit `0x10` (cap 148) y `p[23]` bit `0x10` (cap 156) | `getRangingCaps`, `buildRangingTlv` | Opcional. Broadcom anuncia ranging siempre (cap 156 incondicional); sin él el TLV 30 sale sin el bit y userspace no ve fine timing. Inofensivo para AirDrop/AirPlay. |
| `p[23]` bit `0x20` (cap 157, Limited SDB) | `configure` `0xffffff80021c5a83` | Dejarlo claro: ni siquiera el GET de Broadcom lo pone. |
| `p[24]` (caps 160–167) | `configure`, `isProMode5Enabled` | Dejarlo claro. Bit 2 (cap 162) es «el dispositivo no puede hacer roaming durante AWDL»: puesto, una asociación iniciada por el firmware suspende AWDL y el steering P2P se aplaza; la 8814AU no tiene esa limitación. Bit 3 (cap 163) sólo elige de dónde lee AWDL los contadores de tráfico infra (§`+0xB48`). Bit 1 es el DFS proxy, bit 4 el pro-mode 5. |
| `p[13]` bit `0x40` (cap 78, concurrencia) | `configure` `0xffffff80021c58b3` | Dejarlo claro: sólo afecta a la concurrencia SoftAP+AWDL (`AWDL_CAP78_CONCURRENCIA.md`). |

En resumen: con el parche de doble banda (`p[7] |= 0x20`) el bloque queda completo para
AWDL con asociación a infraestructura. Todo lo demás que falta es cosmético
(velocidad informada, ranging, métricas), diagnóstico (LQM, DPS) o deliberadamente
claro (6E, pro-mode, DFS proxy, RSDB, HE).

## 7. Correcciones y verificaciones a documentos previos

- `AWDL_CAPS_ADVERTISED.md` §8 (TLV 12, flags extendidas): «`0x100` = bit 7 de
  `ctrl_data+0xB45`» — el lector real (`fillLegacyDataPathPayload`,
  `0xffffff800221470a`) testea el byte completo ≠ 0; sólo `getHeCapable`
  (`0xffffff800225f406`) usa el bit 7 aislado. Con el byte a 0 el resultado práctico no
  cambia.
- `AWDL_SECUENCIA_CON_INFRA.md` §12, abiertos: los bytes de capacidad `+0xB3F` y
  `+0xB47` leídos en `configure` quedan cerrados (§`+0xB3F` y §`+0xB47` de este
  documento): bit 7 de `p[15]` = AWDL 6E (sólo log en `configure`, más el grupo 6G),
  bit 6 = 6E infra (timeouts 35000/25000 ms), bit 5 de `p[23]` = Limited SDB
  (`state+0x103FC`).
- `AWDL_SECUENCIA_CON_INFRA.md` §9, fila «Doble banda»: la VA de la puerta
  `0xffffff80021cab1e` se re-verifica como cabeza de instrucción.

## 8. Abiertos

- Cerrados: `state+0xD066` (cap 78, en `AWDL_CAP78_CONCURRENCIA.md`), `state+0x103A0` y
  `state+0x102B1` (bits 2/3 de `p[24]`, §`+0xB48`) y `sub+0x6FC` (§`+0xB39`).
- Si la capa alimenta los canales IOReporter `IfTxPk*`, `IfTxBy*`, `IfRxPkts` e `IfRxByts` del `InterfaceMonitor`
  que `gatherInfraStats` lee con la cap 163 clara. Los únicos escritores del monitor son
  `IO80211InterfaceMonitor::recordOutputPacket(__mbuf *)` (desde
  `IO80211VirtualInterface::dequeueTxPackets`), `recordOutputPacket(apple80211_wme_ac,int,int)`
  y `recordInputPacket(int,int)` (desde los homónimos de `IO80211SkywalkInterface`; el de salida es la ranura
  virtual 358) y `recordInterfaceTxRxCounts` (desde
  `IO80211SkywalkInterface::updateInterfaceDataStats`, ranura 453). El código de la capa no
  llama a ninguno; si la familia lo hace en la ruta infra de la RTL no se siguió (llamadas
  por vtable) `?`. Si nadie los alimenta, AWDL ve tráfico infra nulo en
  `monitorInfraTraffic` `?`.
- Polaridad rama a rama del grupo byte ≠ 0 de `+0xB3F` (30+ sitios listados en §4):
  patrón común verificado, ramas individuales sin seguir `?`.
- Semántica exacta de `isAirPlaySinkEnabled` (`0xffffff8002207fb4`) leyendo `+0xB3F`:
  el símbolo sugiere la puerta del sink; el contenido leído es el byte 6E `?`.
- Bits de `p[6]`/`p[8]`/`p[9]` que `updateLinkSpeed` no usa (0,1,4–7 de p[6]) y el resto
  de `p[17]`–`p[19]`: sin lector en el kernel; sólo Broadcom los escribe según feature
  flags `?`.
- El campo `+0xB50` del objeto de tarjeta (dword, leído por `enableAWDL` en
  `0xffffff80021dd07e` con `& 0x4000000` → `state+0x422E`) queda fuera del bloque: la
  copia del GET es de 28 bytes (`+0xB30..+0xB4B`) y ese campo lo escribe otra ruta `?`.
