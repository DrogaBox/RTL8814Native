# Mapa de lectores de capacidades de tarjeta (`APPLE80211_IOC_CARD_CAPABILITIES`) en macOS 14.8.9 (23J631) y macOS 15.8.2 (24H232)

## 1. Resumen

El presente documento porta el mapa exhaustivo de lectores del bloque de capacidades de tarjeta (`APPLE80211_IOC_CARD_CAPABILITIES`) desde macOS 26.7.1 (build 25G241, documentado en `Docs/AWDL_CARD_CAPS_RESTANTES.md`) hacia macOS 15.8.2 (build 24H232) y macOS 14.8.9 (build 23J631), analizando los kernel collections x86_64 correspondientes mediante desensamblado estático e inspección de instrucciones.

Principales conclusiones:
1. **Arquitectura de almacenamiento y centralización:**
   - En macOS 14 (23J631) **no existe** un buffer centralizado de capacidades dentro de `IO80211Controller`. Cada subsistema (`IO80211AWDLPeerManager`, `IO80211InfraInterface`) ejecuta su propio ioctl interno (`CARD_CAPABILITIES = 12`) y almacena una copia local en sus ivars o subestructuras. En `IO80211AWDLPeerManager::configure`, la copia de 28 bytes se guarda en `sub + 0xB18` (versión u32) y `sub + 0xB1C..+0xB33` (capacidades `p[4..27]`).
   - En macOS 15 (24H232) el bloque se centraliza con `IO80211Controller::loadCardCapabilities` e `isCardCapabilitiesSet`, en `*(controller + 0x120) + 0xB20` (versión) y `+0xB24..+0xB3B` (capacidades).
   - En macOS 26 (25G241), la estructura del controlador creció desplazando la base a `*(controller + 0x120) + 0xB30..+0xB4B` (`+0xB30` versión, `+0xB34..+0xB4B` capacidades).
2. **Desplazamiento constante (delta):**
   - Entre 25G241 y 24H232: **`offset_24H232 = offset_25G241 - 0x10`** (delta exacto de `-0x10`, p. ej. `p[4]` pasa de `+0xB34` a `+0xB24`; `p[7]` de `+0xB37` a `+0xB27`; `p[15]` de `+0xB3F` a `+0xB2F`; `p[21]` de `+0xB45` a `+0xB35`).
   - Entre 25G241 y 23J631 (para los lectores de AWDL): **`offset_23J631 = offset_25G241 - 0x18`** (delta exacto de `-0x18`, p. ej. `p[4]` en `+0xB1C`; `p[7]` en `+0xB1F`; `p[15]` en `+0xB27`).
3. **Comportamiento funcional de compuertas clave:**
   - **Doble banda (`p[7]` bit `0x20`, capacidad 29):** exactamente los mismos 4 lectores en las tres versiones (`configureValidAWDLChannels`, `handleAssistedDiscoveryTrafficReg`, `fillLegacyDataPathPayload` y `printDataPath`, este último sólo para un log), con idéntico efecto. Sin este bit, el canal social 5 GHz queda en 0 y AWDL opera exclusivamente en el canal 6.
   - **SoftAP + AWDL concurrency (`p[13]` bit `0x40`, capacidad 78):** idéntico en las tres versiones a través de `configure`, alimentando el flag de estado para `validateForHostapAWDLConcurrency` y `notifyHostapState`.
   - **Compuertas puras de userspace:** `p[8]` bit 0 (AWDL, `wifip2pd`), `p[13]` bit 3 (SAE, `airportd`) y capacidad 72 (NAN, `wifip2pd`) carecen de lectores en el kernel collection en las tres versiones.
   - **Extensiones HE (`p[21]`, `+0xB45`):** ausentes por completo en macOS 14. En macOS 15 existen 13 de las 18 filas de 25G241 (`getHeCapable`, métricas en `IO80211Peer`); las 5 restantes, de métricas AWDL, son de macOS 26.
   - **Limited SDB (`p[23]` bit `0x20`), ProMode 5 (`p[24]` bit `0x10`) y `p[24]` bits 1..3 en `configure`:** exclusivos de macOS 26; no existen en macOS 14 ni en macOS 15.

---

## 2. Arquitectura de almacenamiento y llenado de capacidades

### macOS 26.7.1 (25G241)
- El controlador centraliza la copia en `*(controller + 0x120) + 0xB30` (versión u32) y `+0xB34..+0xB4B` (24 bytes de capacidades `p[4..27]`).
- Llenado: `IO80211Controller::loadCardCapabilities` (`0xffffff800229a202`) solicita 28 bytes al driver mediante `apple80211getCARD_CAPABILITIES` (o WCL `sendIOUCToWcl(0xC02869C9, 12, ...)`).
- Accesor: `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800229a2b2`) consulta `byte *(card + n/8 + 2868)` con máscara `1 << (n & 7)` (`2868 = 0xB34`).

### macOS 15.8.2 (24H232)
- El controlador centraliza la copia en `*(controller + 0x120) + 0xB20` (versión u32) y `+0xB24..+0xB3B` (24 bytes de capacidades `p[4..27]`).
- Llenado: `IO80211Controller::loadCardCapabilities` (`0xffffff800222cdf0`) pide primero el GET por el puente WCL (`IO80211Glue::sendIOUCToWcl(0xC02869C9, 12, …)` con destino `*(this + 0x120) + 0xB20` y longitud `0x18`) y, si WCL no lo atiende, llama a `apple80211getCARD_CAPABILITIES` con el mismo destino `+0xB20`.
- Accesor: `IO80211Controller::isCardCapabilitiesSet` (`0xffffff800222cea0`) consulta `byte *(card + n/8 + 0xB24)` (`0xffffff800222ceb8`).
- Relación de desplazamientos: **`offset_24H232 = offset_25G241 - 0x10`**.

### macOS 14.8.9 (23J631)
- **Inexistencia de buffer centralizado en el controlador:** `IO80211Controller::loadCardCapabilities` y `isCardCapabilitiesSet` no existen.
- **Copia en `IO80211AWDLPeerManager`:** durante `IO80211AWDLPeerManager::configure` (`0xffffff80020b55e6`), el gestor llama a `IO80211PeerManager::ioctl_internal` con `0xC02869C9` y tipo 12 (`CARD_CAPABILITIES`) y destino `sub + 0xB18` de la subestructura `sub = *(this + 0x18)`: versión u32 en `+0xB18` y capacidades `p[4..27]` en `+0xB1C..+0xB33` (la primera prueba que sigue lee `sub + 0xB1C & 4`). Todos los lectores internos del subsistema AWDL acceden a través de esta subestructura (`pm + 0x18 -> sub + 0xB1C + k`).
- **Copia en `IO80211InfraInterface`:** durante `initCapabilities` (`0xffffff8002204bec`), se pide el GET (`apple80211RouteInternal(0xC02869C9, 12, …)` o `apple80211getCARD_CAPABILITIES`, según el boot-arg `io80211.isNeedToUseIocExternalLegacyRouting`) y se copian 16 bytes (`p[4..19]`) a `ivars + 28..43` (`*(this + 0x118) + 28`). Los métodos de infraestructura (`updateLinkSpeed`) leen de estos ivars propios.
- **Copia en `IO80211VirtualInterface`:** `attachToBpf` (`0xffffff80021d886c`) hace su propio GET (la misma elección entre `apple80211RouteInternal` y `apple80211getCARD_CAPABILITIES`) sobre un buffer local para evaluar `p[7] & 1` `?` (el bit concreto no se releyó).
- Relación de desplazamientos para el stack AWDL: **`offset_23J631 = offset_25G241 - 0x18`**.

---

## 3. Tabla de lectores en macOS 15.8.2 (24H232)

Se verificaron los 95 sitios de lectura identificados en macOS 26.7.1 (25G241). De ellos, 81 coinciden 1:1 por símbolo y desplazamiento (`delta = -0x10`), mientras que 14 corresponden a sitios refactorizados, ausentes o funciones no existentes en macOS 15.

| 25G241 VA | Función | 24H232 VA | Desplazamiento | Máscara | Mismo efecto (sí/no/?) | Nota |
|---|---|---|---|---|:---:|---|
| `0xffffff80021aeaf7` | `IO80211AWDLPeer::getCumSlotDataStats` | `0xffffff800214e6c9` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff80021aeb5d` | `IO80211AWDLPeer::getIntervalSlotDataStats` | `0xffffff800214e72f` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff80021af973` | `IO80211AWDLPeer::actionFrameReport` | `0xffffff800214f579` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff80021b4117` | `IO80211AWDLPeer::doMinimalAFProcessing` | `0xffffff800215396a` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021bd48e` | `IO80211AWDLPeer::processAirDropEnd` | `0xffffff800215c2d6` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c3989` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Ausente en macOS 15: timer multicast no comprueba métricas HE |
| `0xffffff80021c3a45` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Ausente en macOS 15: timer multicast no comprueba métricas HE |
| `0xffffff80021c56d8` | `IO80211AWDLPeerManager::configure` | `0xffffff8002163f0c` | `+0xB24` | `& 0x04` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c5804` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164035` | `+0xB29` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c580d` | `IO80211AWDLPeerManager::configure` | `0xffffff800216403e` | `+0xB2C` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c585e` | `IO80211AWDLPeerManager::configure` | `0xffffff800216408f` | `+0xB2B` | `& 0x02` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c58b3` | `IO80211AWDLPeerManager::configure` | `0xffffff80021640e4` | `+0xB2D` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c5905` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164136` | `+0xB2B` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c5957` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164188` | `+0xB2C` | `& 0x08` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c59e4` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164215` | `+0xB32` | `& 0x01` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c5a39` | `IO80211AWDLPeerManager::configure` | `0xffffff800216426a` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c5a83` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Ausente en macOS 15: capacidad Limited SDB (+0xB47 / +0xB37) no existe |
| `0xffffff80021c6373` | `IO80211AWDLPeerManager::configure` | `0xffffff8002164a90` | `+0xB34` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff80021c63c4` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Ausente en macOS 15: configure no lee p[24] (+0xB48 / +0xB38) |
| `0xffffff80021c63d6` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Refactorizado en macOS 15: DeviceCannotRoamDuringAWDL gobernado por +0xB34 & 0x40 (Leaf Mode) |
| `0xffffff80021c63e8` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Refactorizado en macOS 15: DeviceCannotRoamDuringAWDL gobernado por +0xB34 & 0x40 (Leaf Mode) |
| `0xffffff80021c88b3` | `IO80211AWDLPeerManager::updatePref6GChannel` | `0xffffff8002166e21` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021cab1e` | `IO80211AWDLPeerManager::configureValidAWDLChannels` | `0xffffff8002169030` | `+0xB27` | `& 0x20` | sí | Idéntico (delta -0x10) |
| `0xffffff80021cde35` | `IO80211AWDLPeerManager::setAWDLMaxBandwidth` | `0xffffff800216c0f5` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021dcfce` | `IO80211AWDLPeerManager::enableAWDL` | `0xffffff800217a4ee` | `+0xB2F` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff80021e8778` | `IO80211AWDLPeerManager::removePeerFromRealTimeGroup` | `0xffffff8002184f69` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021e9790` | `IO80211AWDLPeerManager::processPeerProModeUMIStatus` | `0xffffff8002185d58` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021ed766` | `IO80211AWDLPeerManager::monitorAWDLState` | `0xffffff8002189b96` | `+0xB2F` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff80021f185b` | `IO80211AWDLPeerManager::monitorProModeState` | `0xffffff800218d9cb` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021f353a` | `IO80211AWDLPeerManager::getScanTimeout` | `0xffffff800218f5d6` | `+0xB2F` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff80021f5fe8` | `IO80211AWDLPeerManager::is6GChannelALlowedForP2P` | — | — | — | no | Refactorizado en macOS 15 en isValid6GChannelForP2P (sin lectura directa de tarjeta) |
| `0xffffff80021fa183` | `IO80211AWDLPeerManager::findCommon6GChannel` | `0xffffff8002195c59` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80021faa7a` | `IO80211AWDLPeerManager::setHighThruputState` | `0xffffff8002196514` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002203a10` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Ausente en macOS 15: handleEnsembleTrafficReg no evalúa capacidad de tarjeta |
| `0xffffff8002203f52` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Ausente en macOS 15: handleEnsembleTrafficReg no evalúa capacidad de tarjeta |
| `0xffffff800220456e` | `IO80211AWDLPeerManager::airplayStatsPostMetrics` | `0xffffff800219fc9c` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002205f0c` | `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` | `0xffffff80021a16a1` | `+0xB27` | `& 0x20` | sí | Idéntico (delta -0x10) |
| `0xffffff8002206709` | `IO80211AWDLPeerManager::handleMigrationTrafficReg` | `0xffffff80021a1e8c` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002207f4b` | `IO80211AWDLPeerManager::handleAirPlaySinkTrafficReg` | `0xffffff80021a2d27` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002207fca` | `IO80211AWDLPeerManager::isAirPlaySinkEnabled` | `0xffffff80021a2da6` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002208e27` | `IO80211AWDLPeerManager::handleAirDropTrafficReg` | `0xffffff80021a3b94` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff800220bdb8` | `IO80211AWDLPeerManager::setAwdlPeerTrafficRegistration` | `0xffffff80021a67fd` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002210f14` | `IO80211AWDLPeerManager::buildAwdlDataPathStateTlv` | `0xffffff80021ab94f` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff800221144a` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80021abe7a` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80022115f5` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80021abffb` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002212790` | `IO80211AWDLPeerManager::buildRangingTlv` | — | — | — | no | Ausente en macOS 15: buildRangingTlv sólo comprueba bit 0x08 (RSDB), no bit 0x10 de ranging |
| `0xffffff80022143fb` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80021aebc5` | `+0xB27` | `& 0x20` | sí | Idéntico (delta -0x10) |
| `0xffffff800221470a` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80021aee8f` | `+0xB35` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002223aa2` | `IO80211AWDLPeerManager::printDataPath` | `0xffffff80021bceb6` | `+0xB27` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002224acc` | `IO80211AWDLPeerManager::awdlStatsCollectCachedPeersStats` | `0xffffff80021bde56` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff800222a4eb` | `IO80211AWDLPeerManager::rtgStatsPostMetrics` | — | — | — | no | Ausente en macOS 15: función no existe en 24H232 |
| `0xffffff800222f9bc` | `IO80211AWDLPeerManager::submitRTGMetricstoCoreAnalytics` | `0xffffff80021c6e0c` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002236d9e` | `IO80211AWDLPeerManager::isProMode5Enabled` | — | — | — | no | Ausente en macOS 15: función no existe en 24H232 (introducida en macOS 26) |
| `0xffffff8002236dfa` | `IO80211AWDLPeerManager::processPeerProModeTlv` | `0xffffff80021cd720` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80022379f2` | `IO80211AWDLPeerManager::is6GChanSeqAllowed` | `0xffffff80021ce248` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff800223827d` | `IO80211AWDLPeerManager::isProModeAllowed` | `0xffffff80021ceada` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002238435` | `IO80211AWDLPeerManager::shouldEnterProMode` | `0xffffff80021cec6f` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80022387c5` | `IO80211AWDLPeerManager::enteringProMode` | `0xffffff80021ced73` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002238b6b` | `IO80211AWDLPeerManager::notifyPeerRemovedFromCache` | `0xffffff80021cf117` | `+0xB2F` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff8002238d54` | `IO80211AWDLPeerManager::logRealTimePrintStats` | — | — | — | no | Ausente en macOS 15: función no comprueba capacidad HE |
| `0xffffff8002243196` | `IO80211Peer::updateCumDataStats` | `0xffffff80021d93a6` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff8002243204` | `IO80211Peer::updateIntervalDataStats` | `0xffffff80021d9414` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff800224325f` | `IO80211Peer::clearIntervalDataStats` | `0xffffff80021d946f` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff8002244ec4` | `IO80211Peer::logDataStats` | `0xffffff80021db1e1` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff8002245bb3` | `IO80211Peer::getLastQueuePacket` | `0xffffff80021dbc1f` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff800225b74c` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff80021f1204` | `+0xB29` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff800225b755` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff80021f120d` | `+0xB2C` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff800225ef2f` | `IO80211PeerManager::getLastUnicastRxLinkActivity` | `0xffffff80021f473b` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff800225f406` | `IO80211PeerManager::getHeCapable` | `0xffffff80021f4be2` | `+0xB35` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff800225f501` | `IO80211PeerManager::getLastUnicastTxLinkActivity` | `0xffffff80021f4cd9` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff800225f910` | `IO80211PeerManager::is6ECapable` | `0xffffff80021f50e4` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff800225f932` | `IO80211PeerManager::isInfra6ECapable` | `0xffffff80021f5106` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff800225fb9f` | `IO80211PeerManager::getDataPathPeerStats` | `0xffffff80021f5349` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff800229a2ca` | `IO80211Controller::isCardCapabilitiesSet` | `0xffffff800222ceb8` | `+0xB24` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff800229f502` | `IO80211NANLocaleManager::configure` | `0xffffff8002231cc6` | `+0xB29` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff800229f50b` | `IO80211NANLocaleManager::configure` | `0xffffff8002231ccf` | `+0xB2C` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff800229fbcf` | `IO80211NANLocaleManager::isChannelValid` | `0xffffff80022322d3` | `+0xB2B` | `byte ≠ 0` | sí | Idéntico (delta -0x10) |
| `0xffffff80022a00b9` | `IO80211NANLocaleManager::isCardChannelCLMRestrictedCapable` | `0xffffff80022327c1` | `+0xB2B` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff80022ad3cd` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff800223e289` | `+0xB2B` | `& 0x02` | sí | Idéntico (delta -0x10) |
| `0xffffff80022ad443` | `IO80211NANPeerManager::initDefaultNANParams` | — | — | — | no | Ausente en macOS 15: nan+12514 fijado a 0 constante en 24H232 |
| `0xffffff80022ad456` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff800223e2fc` | `+0xB2F` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff80022ad468` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff800223e30e` | `+0xB2C` | `& 0x08` | sí | Idéntico (delta -0x10) |
| `0xffffff80023303c6` | `IO80211VirtualInterface::attachToBpf` | `0xffffff80022a5ec2` | `+0xB27` | `& 0x01` | sí | Idéntico (delta -0x10) |
| `0xffffff80023489df` | `IO80211RangingManagerExt::getRangingCaps` | `0xffffff80022be485` | `+0xB36` | `& 0x10` | sí | Idéntico (delta -0x10) |
| `0xffffff800236003a` | `IO80211InfraInterface::prepareBSDInterface` | `0xffffff80022d66f7` | `+0xB27` | `& 0x01` | sí | Idéntico (delta -0x10) |
| `0xffffff80023610cb` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d79e8` | `+0xB26` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002361211` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d7b30` | `+0xB26` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff80023612f3` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d7c09` | `+0xB29` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002361327` | `IO80211InfraInterface::updateLinkSpeed` | `0xffffff80022d7c38` | `+0xB26` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002362d21` | `IO80211InfraInterface::updateChanOutageTime` | `0xffffff80022d9603` | `+0xB2B` | `byte completo` | sí | Idéntico (delta -0x10) |
| `0xffffff8002366555` | `IO80211InfraInterface::createLinkQualityMonitor` | `0xffffff80022dcf91` | `+0xB2E` | `& 0x08` | sí | Idéntico (delta -0x10) |
| `0xffffff8002366a4e` | `IO80211InfraInterface::createLinkQualityMonitor` | `0xffffff80022dd4a5` | `+0xB30` | `& 0x20` | sí | Idéntico (delta -0x10) |
| `0xffffff8002367b9e` | `IO80211InfraInterface::updateTxRxLatency` | `0xffffff80022de606` | `+0xB35` | `& 0x40` | sí | Idéntico (delta -0x10) |
| `0xffffff800237fe49` | `IO80211PeerMonitor::checkForDPS` | `0xffffff80022f6192` | `+0xB2F` | `& 0x10` | sí | Idéntico (delta -0x10) |
| `0xffffff800238016f` | `IO80211PeerMonitor::recordDPSSymptomsInput` | `0xffffff80022f64ab` | `+0xB2F` | `& 0x08` | sí | Idéntico (delta -0x10) |

---

## 4. Tabla de lectores en macOS 14.8.9 (23J631)

Se verificaron los 95 sitios de lectura respecto a macOS 14.8.9 (23J631). De ellos, 51 mapean directamente 1:1 en el bloque de `IO80211AWDLPeerManager` (`delta = -0x18`), y 44 corresponden a capacidades no implementadas en macOS 14 (HE, Limited SDB, Leaf Mode, ProMode 5, p[24]), al buffer centralizado inexistente o a lectores que consultan ivars propios de infraestructura / stack local.

| 25G241 VA | Función | 23J631 VA | Desplazamiento | Máscara | Mismo efecto (sí/no/?) | Nota |
|---|---|---|---|---|:---:|---|
| `0xffffff80021aeaf7` | `IO80211AWDLPeer::getCumSlotDataStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff80021aeb5d` | `IO80211AWDLPeer::getIntervalSlotDataStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff80021af973` | `IO80211AWDLPeer::actionFrameReport` | `0xffffff80020a12e0` | `+0xB27` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021b4117` | `IO80211AWDLPeer::doMinimalAFProcessing` | `0xffffff80020a5615` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021bd48e` | `IO80211AWDLPeer::processAirDropEnd` | `0xffffff80020ae05c` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c3989` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff80021c3a45` | `IO80211AWDLPeerManager::handleMulticastTimer` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff80021c56d8` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b585d` | `+0xB1C` | `& 0x04` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c5804` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5971` | `+0xB21` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c580d` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b597a` | `+0xB24` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c585e` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b59bd` | `+0xB23` | `& 0x02` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c58b3` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5a04` | `+0xB25` | `& 0x40` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c5905` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5a48` | `+0xB23` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c5957` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5a8c` | `+0xB24` | `& 0x08` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c59e4` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5b0b` | `+0xB2A` | `& 0x01` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c5a39` | `IO80211AWDLPeerManager::configure` | `0xffffff80020b5b52` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021c5a83` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Ausente en macOS 14: Limited SDB no existía en macOS 14 |
| `0xffffff80021c6373` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Ausente en macOS 14: Leaf Mode no existía en macOS 14 |
| `0xffffff80021c63c4` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Ausente en macOS 14: capacidades 161..163 no se leen en macOS 14 |
| `0xffffff80021c63d6` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Ausente en macOS 14: capacidades 161..163 no se leen en macOS 14 |
| `0xffffff80021c63e8` | `IO80211AWDLPeerManager::configure` | — | — | — | no | Ausente en macOS 14: capacidades 161..163 no se leen en macOS 14 |
| `0xffffff80021c88b3` | `IO80211AWDLPeerManager::updatePref6GChannel` | `0xffffff80020b87ab` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021cab1e` | `IO80211AWDLPeerManager::configureValidAWDLChannels` | `0xffffff80020bb433` | `+0xB1F` | `& 0x20` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021cde35` | `IO80211AWDLPeerManager::setAWDLMaxBandwidth` | `0xffffff80020be84f` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021dcfce` | `IO80211AWDLPeerManager::enableAWDL` | `0xffffff80020cc224` | `+0xB27` | `& 0x40` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021e8778` | `IO80211AWDLPeerManager::removePeerFromRealTimeGroup` | `0xffffff80020d6299` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021e9790` | `IO80211AWDLPeerManager::processPeerProModeUMIStatus` | `0xffffff80020d6ec4` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021ed766` | `IO80211AWDLPeerManager::monitorAWDLState` | `0xffffff80020dab10` | `+0xB27` | `& 0x40` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021f185b` | `IO80211AWDLPeerManager::monitorProModeState` | `0xffffff80020de5d3` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021f353a` | `IO80211AWDLPeerManager::getScanTimeout` | `0xffffff80020e0162` | `+0xB27` | `& 0x40` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021f5fe8` | `IO80211AWDLPeerManager::is6GChannelALlowedForP2P` | — | — | — | no | Ausente en macOS 14: no consulta el bloque directamente |
| `0xffffff80021fa183` | `IO80211AWDLPeerManager::findCommon6GChannel` | `0xffffff80020e65e7` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80021faa7a` | `IO80211AWDLPeerManager::setHighThruputState` | `0xffffff80020e6eb7` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002203a10` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff8002203f52` | `IO80211AWDLPeerManager::handleEnsembleTrafficReg` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800220456e` | `IO80211AWDLPeerManager::airplayStatsPostMetrics` | `0xffffff80020f75d6` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002205f0c` | `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` | `0xffffff80020f10f9` | `+0xB1F` | `& 0x20` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002206709` | `IO80211AWDLPeerManager::handleMigrationTrafficReg` | `0xffffff80020f1908` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002207f4b` | `IO80211AWDLPeerManager::handleAirPlaySinkTrafficReg` | — | — | — | no | Refactorizado en macOS 14: delega en isAirPlaySinkEnabled (+0xB27) |
| `0xffffff8002207fca` | `IO80211AWDLPeerManager::isAirPlaySinkEnabled` | `0xffffff80020f27d2` | `+0xB27` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002208e27` | `IO80211AWDLPeerManager::handleAirDropTrafficReg` | `0xffffff80020f3616` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800220bdb8` | `IO80211AWDLPeerManager::setAwdlPeerTrafficRegistration` | — | — | — | no | Refactorizado en macOS 14: delega en isAirPlaySinkEnabled (+0xB27) |
| `0xffffff8002210f14` | `IO80211AWDLPeerManager::buildAwdlDataPathStateTlv` | `0xffffff80020fba01` | `+0xB27` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800221144a` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80020fbf16` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80022115f5` | `IO80211AWDLPeerManager::buildPrefChanTlv` | `0xffffff80020fc07b` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002212790` | `IO80211AWDLPeerManager::buildRangingTlv` | — | — | — | no | Ausente en macOS 14: ranging TLV no consulta p[23] |
| `0xffffff80022143fb` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80020fed86` | `+0xB1F` | `& 0x20` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800221470a` | `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff8002223aa2` | `IO80211AWDLPeerManager::printDataPath` | `0xffffff800210ce82` | `+0xB1F` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002224acc` | `IO80211AWDLPeerManager::awdlStatsCollectCachedPeersStats` | `0xffffff800210dd7e` | `+0xB27` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800222a4eb` | `IO80211AWDLPeerManager::rtgStatsPostMetrics` | — | — | — | no | Ausente en macOS 14: función no existe en 23J631 |
| `0xffffff800222f9bc` | `IO80211AWDLPeerManager::submitRTGMetricstoCoreAnalytics` | `0xffffff8002116be0` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002236d9e` | `IO80211AWDLPeerManager::isProMode5Enabled` | — | — | — | no | Ausente en macOS 14: función no existe en 23J631 |
| `0xffffff8002236dfa` | `IO80211AWDLPeerManager::processPeerProModeTlv` | `0xffffff800211d022` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80022379f2` | `IO80211AWDLPeerManager::is6GChanSeqAllowed` | `0xffffff800211db22` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800223827d` | `IO80211AWDLPeerManager::isProModeAllowed` | `0xffffff800211e39c` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002238435` | `IO80211AWDLPeerManager::shouldEnterProMode` | `0xffffff800211e51d` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80022387c5` | `IO80211AWDLPeerManager::enteringProMode` | `0xffffff800211e605` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002238b6b` | `IO80211AWDLPeerManager::notifyPeerRemovedFromCache` | `0xffffff800211e99f` | `+0xB27` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff8002238d54` | `IO80211AWDLPeerManager::logRealTimePrintStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff8002243196` | `IO80211Peer::updateCumDataStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff8002243204` | `IO80211Peer::updateIntervalDataStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800224325f` | `IO80211Peer::clearIntervalDataStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff8002244ec4` | `IO80211Peer::logDataStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff8002245bb3` | `IO80211Peer::getLastQueuePacket` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800225b74c` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff800214204e` | `+0xB21` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800225b755` | `IO80211PeerManager::isCardPeerCountryCodeLearningCapable` | `0xffffff8002142057` | `+0xB24` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800225ef2f` | `IO80211PeerManager::getLastUnicastRxLinkActivity` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800225f406` | `IO80211PeerManager::getHeCapable` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800225f501` | `IO80211PeerManager::getLastUnicastTxLinkActivity` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800225f910` | `IO80211PeerManager::is6ECapable` | `0xffffff800214587c` | `+0xB27` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800225f932` | `IO80211PeerManager::isInfra6ECapable` | `0xffffff8002145890` | `+0xB27` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800225fb9f` | `IO80211PeerManager::getDataPathPeerStats` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800229a2ca` | `IO80211Controller::isCardCapabilitiesSet` | — | — | — | no | Ausente en macOS 14: no existe buffer centralizado de tarjeta en IO80211Controller |
| `0xffffff800229f502` | `IO80211NANLocaleManager::configure` | `0xffffff8002176744` | `+0xB21` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800229f50b` | `IO80211NANLocaleManager::configure` | `0xffffff800217674d` | `+0xB24` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff800229fbcf` | `IO80211NANLocaleManager::isChannelValid` | `0xffffff8002176e76` | `+0xB23` | `byte ≠ 0` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80022a00b9` | `IO80211NANLocaleManager::isCardChannelCLMRestrictedCapable` | `0xffffff8002176f3b` | `+0xB23` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80022ad3cd` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff80021808c7` | `+0xB23` | `& 0x02` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80022ad443` | `IO80211NANPeerManager::initDefaultNANParams` | — | — | — | no | Ausente en macOS 14: parámetros NAN inicializados sin leer estos desplazamientos |
| `0xffffff80022ad456` | `IO80211NANPeerManager::initDefaultNANParams` | — | — | — | no | Ausente en macOS 14: parámetros NAN inicializados sin leer estos desplazamientos |
| `0xffffff80022ad468` | `IO80211NANPeerManager::initDefaultNANParams` | `0xffffff8002180928` | `+0xB24` | `byte completo` | sí | Idéntico (delta -0x18 en subestructura AWDL) |
| `0xffffff80023303c6` | `IO80211VirtualInterface::attachToBpf` | — | — | — | no | Refactorizado en macOS 14: attachToBpf consulta ioctl_internal(CARD_CAPABILITIES) en buffer local |
| `0xffffff80023489df` | `IO80211RangingManagerExt::getRangingCaps` | — | — | — | no | Ausente en macOS 14: no consulta el bloque de capacidades de tarjeta |
| `0xffffff800236003a` | `IO80211InfraInterface::prepareBSDInterface` | — | — | — | no | Ausente en macOS 14: prepareBSDInterface no accede directamente a la tarjeta |
| `0xffffff80023610cb` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactorizado en macOS 14: consulta ivars propios de interfaz (ivars+28..43) |
| `0xffffff8002361211` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactorizado en macOS 14: consulta ivars propios de interfaz (ivars+28..43) |
| `0xffffff80023612f3` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactorizado en macOS 14: consulta ivars propios de interfaz (ivars+28..43) |
| `0xffffff8002361327` | `IO80211InfraInterface::updateLinkSpeed` | — | — | — | no | Refactorizado en macOS 14: consulta ivars propios de interfaz (ivars+28..43) |
| `0xffffff8002362d21` | `IO80211InfraInterface::updateChanOutageTime` | — | — | — | no | Ausente en macOS 14: no lee el bloque de tarjeta en esta ruta |
| `0xffffff8002366555` | `IO80211InfraInterface::createLinkQualityMonitor` | — | — | — | no | Ausente en macOS 14: LQM no lee capacidades de tarjeta en macOS 14 |
| `0xffffff8002366a4e` | `IO80211InfraInterface::createLinkQualityMonitor` | — | — | — | no | Ausente en macOS 14: LQM no lee capacidades de tarjeta en macOS 14 |
| `0xffffff8002367b9e` | `IO80211InfraInterface::updateTxRxLatency` | — | — | — | no | Ausente en macOS 14: capacidad HE (+0xB45) no existía en macOS 14 |
| `0xffffff800237fe49` | `IO80211PeerMonitor::checkForDPS` | — | — | — | no | Refactorizado en macOS 14: DPS evalúa peer+2679 en lugar del bloque de tarjeta del controlador |
| `0xffffff800238016f` | `IO80211PeerMonitor::recordDPSSymptomsInput` | — | — | — | no | Refactorizado en macOS 14: DPS evalúa peer+2679 en lugar del bloque de tarjeta del controlador |

---

## 5. Lectores en macOS 14 o macOS 15 sin contraparte en el documento de 25G241

Durante el barrido exhaustivo de las bases de macOS 14 y 15 se identificó un lector perteneciente a `IO80211AWDLPeer` que no figuraba en las tablas de `Docs/AWDL_CARD_CAPS_RESTANTES.md`:

### `IO80211AWDLPeer::parseAwdlPrefChanTLV`
- **24H232 VA:** `0xffffff8002155c73` (`cmp byte ptr [rdi+0B2Fh], 0`) — `+0x0B2F` (p[15], byte 6E).
- **23J631 VA:** `0xffffff80020a797d` (`cmp byte ptr [rdi+0B27h], 0`) — `+0x0B27` (p[15], byte 6E).
- **25G241 VA:** `0xffffff80021b64fe` (`cmp byte ptr [rdi+0B3Fh], 0`) — `+0x0B3F` (p[15], byte 6E).
- **Comportamiento:** Comprueba el bit 7 del byte 6E (`p[15] < 0`, capacidad 95, AWDL 6E). Si el host no tiene soporte 6E, omite el procesamiento del TLV 33 (Preferred Channel TLV para bandas de 6 GHz). La compuerta existe en las tres versiones con idéntico efecto.

No se detectaron lectores adicionales legítimos del bloque de tarjeta en ninguno de los kernel collections analizados (los restantes accesos a desplazamientos `0xB20..0xB3B` corresponden a ivars de `IO80211PeerManager`, `IO80211Controller` o `AppleBCMWLAN*`).

---

## 6. Puertas cuyo significado o mecanismo cambia entre versiones

### 1. `p[7]` bit `0x20` (capacidad 29, doble banda)
- **25G241:** `+0xB37 & 0x20`. 4 lectores: `configureValidAWDLChannels` (`0xffffff80021cab1e`), `handleAssistedDiscoveryTrafficReg` (`0xffffff8002205f0c`), `fillLegacyDataPathPayload` (`0xffffff80022143fb`) y `printDataPath` (`0xffffff8002223aa2`).
- **24H232:** `+0xB27 & 0x20`. Mismos 4 lectores: `0xffffff8002169030`, `0xffffff80021a16a1`, `0xffffff80021aebc5`, `0xffffff80021bceb6`.
- **23J631:** `+0xB1F & 0x20`. Mismos 4 lectores: `0xffffff80020bb433`, `0xffffff80020f10f9`, `0xffffff80020fed86`, `0xffffff800210ce82`.
- **Efecto:** Totalmente idéntico en las tres versiones. Sin este bit, el qword `+0x4294` (canales sociales 5 GHz 44 y 149) se inicializa a cero con el log `"Dual band not supported, so assuming 2.4GHz is the basic and should be supported, initialize 5GHz channels to zero"`, limitando la secuencia únicamente al canal 6. Es la compuerta crítica que la capa RTL activa con `p[7] |= 0x20`.

### 2. `p[8]` bit 0 (capacidad 32, AWDL)
- **25G241, 24H232 y 23J631:** Cero lectores en el kernel collection.
- **Mecanismo:** Es una compuerta consumida exclusivamente en espacio de usuario por el daemon `wifip2pd` (`VIRTUAL_IF_CREATE`). La capa RTL lo fija incondicionalmente (`p[8] = 0x01`).

### 3. `p[13]` bit 3 (capacidad 75, SAE / WPA3) y capacidad 72 (NAN)
- **25G241, 24H232 y 23J631:** Cero lectores en kernel para ambas capacidades.
- **Mecanismo:** La capacidad 75 (SAE) es consumida por el daemon `airportd` para habilitar WPA3-Personal. La capacidad 72 (NAN) es consumida por `wifip2pd`. La capa RTL fija SAE (`0x08`) y deja NAN en cero.

### 4. `p[13]` bit `0x40` (capacidad 78, concurrencia SoftAP + AWDL)
- **25G241:** `+0xB3D & 0x40` en `configure` (`0xffffff80021c58b3`), registra `"Concurrency supported"` y pone `state + 0xD066 = 1`.
- **24H232:** `+0xB2D & 0x40` en `configure` (`0xffffff80021640e4`), registra `"Concurrency supported"` y pone `state + 43286 = 1` (`0xA916`).
- **23J631:** `+0xB25 & 0x40` en `configure` (`0xffffff80020b5a04`), registra `"Concurrency supported"` y pone `state + 42806 = 1` (`0xA736`).
- **Efecto:** Idéntico en las tres versiones. El flag de estado resultante es consultado exclusivamente por `validateForHostapAWDLConcurrency` y `notifyHostapState`. Gobierna la coexistencia entre SoftAP y la interfaz AWDL; no afecta a la asociación a infraestructura como cliente.

### 5. `p[24]` bits 1 a 3 (capacidades 161, 162 y 163)
- **25G241:** `configure` (`0xffffff80021c63c4`, `0xffffff80021c63d6`, `0xffffff80021c63e8`) lee `+0xB48` bits 1, 2 y 3:
  - Bit 1 (cap 161): limpia `state + 0x401A` (flag DFS proxy).
  - Bit 2 (cap 162): fija `state + 0x103A0 = 1` (`"DeviceCannotRoamDuringAWDL"`), suspendiendo AWDL o forzando secuencias de canal si el firmware inicia roaming.
  - Bit 3 (cap 163): fija `state + 0x102B1`, seleccionando si `gatherInfraStats` consulta contadores de `InterfaceMonitor` (0) o la ranura vtable `+3200` del controlador (1).
- **24H232:** `p[24]` no se lee en absoluto en `configure`. El flag `DeviceCannotRoamDuringAWDL` (`state + 52104`, leído en el caso 127 de `reportDataPathEvents` igual que `state + 0x103A0` en 25G241) se activa en `configure` cuando `p[20] & 0x40` (`+0xB34 & 0x40`, Leaf Mode) está puesto y la variable de `enable_awdl_leaf` no es 0. Esa variable vale 1 por defecto y el boot-arg sólo puede apagarla. La misma rama pone a 1 el word `state + 51864` (`257`), es decir, los equivalentes de `+0x102B0` (Leaf) y `+0x102B1` (fuente de contadores infra) de 25G241, y limpia `state + 16418`; que `gatherInfraStats` lea `state + 51865` no se releyó `?`. En 25G241 la rama Leaf sigue existiendo (`0xffffff80021c6373`, `+0xB44 & 0x40`, log «Device prefers leaf-only operation») pero sólo fija `+0x102B0`; los otros dos flags pasaron a `p[24]` bits 2 y 3. Con la capa RTL (`p[20] = 0`) ninguna de las dos rutas se activa.
- **23J631:** `p[24]` no se lee. No existe el mecanismo de suspensión de AWDL por roaming ni la selección de fuentes alternativas para contadores infra.

### 6. Byte 6E (`p[15]`: `+0xB3F` en 25 / `+0xB2F` en 24 / `+0xB27` en 23)
- **25G241 y 24H232:** El grupo pro-mode y secuencias 6 GHz contiene los mismos métodos de control. Bit 7 gobierna `is6ECapable` y `parseAwdlPrefChanTLV`; bit 6 gobierna `isInfra6ECapable` y timeouts de scan (35000 vs 25000 ms). En 25G241, bits 3 y 4 gobernaban además DPS (`checkForDPS`, `recordDPSSymptomsInput`).
- **23J631:** Los métodos centrales de secuencias 6 GHz y pro-mode en `AWDLPeerManager` están presentes en `+0xB27`. Sin embargo, las funciones de DPS (`checkForDPS`) leen el estado interno `peer + 2679` en lugar del bloque de capacidades de tarjeta. Adicionalmente, `handleAirPlaySinkTrafficReg` y `setAwdlPeerTrafficRegistration` en 23J631 no leen la tarjeta directamente, sino que invocan la función auxiliar `isAirPlaySinkEnabled` (la cual lee `+0xB27`).
- **Comportamiento en la capa RTL:** Mantener el byte en cero es la configuración correcta para RTL8814AU (radio 2.4/5 GHz sin 6 GHz).

### 7. Byte HE (`p[21]`: `+0xB45` en 25 / `+0xB35` en 24 / ausente en 23)
- **25G241:** 18 filas del mapa leen `+0xB45`. Bit 7 en `getHeCapable`, byte != 0 en `fillLegacyDataPathPayload` (bit `0x100` en TLV 12), y bit 6 (`& 0x40`) en múltiples métodos de métricas y contadores de datos.
- **24H232:** 13 de esas filas existen con `+0xB35`. Byte completo en `getHeCapable` (`0xffffff80021f4be2`), byte != 0 en `fillLegacyDataPathPayload` (`0xffffff80021aee8f`), y bit 6 en los métodos de datos de `IO80211Peer`, `IO80211PeerManager` e `IO80211InfraInterface`. Las 5 filas restantes (`handleMulticastTimer` x2, `handleEnsembleTrafficReg` x2, `logRealTimePrintStats`) no incluían la comprobación de tarjeta en macOS 15.
- **23J631:** Cero lectores en kernel. La capacidad 802.11ax (HE) no existía en macOS 14.

### 8. Byte `p[23]` (Limited SDB y Ranging)
- **25G241:** `configure` lee `+0xB47 & 0x20` para activar Limited SDB (`state + 0x103FC = 1`), y `buildRangingTlv` lee `+0xB47 & 0x10` para el TLV 30 de ranging.
- **24H232 y 23J631:** Ni Limited SDB ni la prueba de bit `0x10` en `buildRangingTlv` existen. En 24H232, `buildRangingTlv` únicamente evalúa el flag RSDB (`v5 + 32776 == 1` -> bit `0x08`).

---

## 7. Puntos abiertos

1. **Alimentación de canales `InterfaceMonitor` en macOS 15:** En 24H232, `gatherInfraStats` consulta los canales de `InterfaceMonitor` (`IfTxPkBe`, `IfTxPkBk`, `IfTxPkVo`, `IfTxPkVi`, etc.) de forma predeterminada (salvo que se configure Leaf Mode). Resta verificar dinámicamente si el subsistema Skywalk de macOS 14/15 actualiza estos contadores durante el tráfico normal de una interfaz virtual de infraestructura sobre drivers de terceros `?`.
2. **Polaridad de ramas secundarias en pro-mode (`+0xB2F` / `+0xB27`):** Se verificó que cualquier byte distinto de cero activa las rutas pro-mode y secuencias 6 GHz en las 3 versiones; las ramas individuales internas de pro-mode permanecen sin trazar exhaustivamente `?` dado que la capa RTL fija el byte en 0x00.

