# Capacidad 29 (doble banda): quién más la lee antes de activarla

La build de prueba del AirLayer `3699d28e` añade `p[7] |= 0x20` en
`RTLAirController::getCARD_CAPABILITIES` (capacidad 29, byte del kernel `+0xB37`, bit `0x20`;
en userspace, tras quitar la versión de 4 bytes, byte 3 bit `0x20`). Sin ese bit, el gestor AWDL
deja la secuencia sólo en el canal 6 (`AWDL_SECUENCIA_CON_INFRA.md` §4.2). Este documento
recoge qué más consume el bit en 25G241, para saber si puede tener efectos laterales como los
de la capacidad 75 (SAE), que cambió la negociación de seguridad con el router de transición.
Lo no verificado se marca `?`.

## 1. Kernel

Todos los lectores de `+0xB37 & 0x20` del kernel collection son del gestor AWDL
(`AWDL_CARD_CAPS_RESTANTES.md` §`+0xB37`, verificados en IDA):

| Lector | VA | Efecto con el bit |
|---|---|---|
| `IO80211AWDLPeerManager::configureValidAWDLChannels` | `0xffffff80021cab1e` | conserva los sociales de 5 GHz (149/44) en `+0x4294` |
| `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` | `0xffffff8002205f0c` | permite un social de 5 GHz para el descubrimiento asistido |
| `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80022143fb` | flags base del TLV 12: `0x8B23` en vez de `0x8B03` |

Ningún lector está en la ruta de infraestructura (`IO80211InfraInterface`, asociación, roaming,
escaneo). El accesor genérico `IO80211Controller::isCardCapabilitiesSet` no tiene llamadores
directos.

## 2. Userspace

- **airportd** (`/usr/libexec/airportd`): un barrido del desensamblado buscando pruebas
  `[x+7], 20h` (la misma convención de desplazamiento con la que el barrido sí encuentra la
  puerta AWDL de wifip2pd en `[x+8], 1`) no encontró candidatos. El barrido es textual: una
  prueba hecha sobre el byte ya cargado en un registro no aparecería `?`. Sus cadenas «DualBand»
  (`__defaultDualBandSingleAPRoamingProfile_*`, `CWRoamParamsTypeDualBand`, «Forcing dual-band
  roam profile because BT audio is in use») describen perfiles de roaming según el entorno de
  puntos de acceso, sin relación demostrada con la capacidad de la tarjeta `?`.
- **CoreWiFi**: tampoco hay pruebas de ese bit en la imagen extraída del shared cache;
  `supports5GHzNetworks` es una propiedad de cada resultado de escaneo (`-[CWFScanResult
  supports5GHzNetworks]`), no de la tarjeta.
- **wifip2pd** (`/usr/libexec/wifip2pd`): las únicas pruebas sobre la copia de capacidades son la
  puerta de AWDL (`sub_100047660`, `0x1000483b5`, byte 4 bit 0) y dos de bit 0 en
  `sub_1002E5440`/`sub_1002E5570`. Su `supportsDualBand` es un campo de la información de
  interfaz de Wi-Fi Aware (NAN): los inicializadores que lo usan
  (`initWithInterfaceName:…:supportsDualBand:…`, referencias en `sub_1002E9470` y
  `sub_1003C3C40`) arman servicios Wi-Fi Aware (`WiFiAwarePublishServiceSpecificInfo`). Con NAN
  apagado por la capacidad 72, esa ruta no corre. De dónde toma el valor no se siguió `?`.

### 2.1 Consultas por número de capacidad

`airportd` también consulta capacidades por valor: `-[CWInterface capabilities]` devuelve el
conjunto de números y el llamador prueba `containsObject:` con un literal NSNumber. Así es la
puerta SAE de `-[CWXPCInterfaceContext __allowJoinCandidate:trigger:reply:]` (`0x1000bbf66`):
`capabilities` en `0x1000bbfc4`, `containsObject:` con el literal 75 de `__objc_intobj`
(`0x10015df00`, referenciado desde `0x1000bbfd8`).

- **airportd**: la sección `__objc_intobj` (`0x10015d618`–`0x10015e938`) no contiene ningún
  literal 29, y ninguna de las 28 funciones que llaman al selector `capabilities` usa un
  inmediato 29. Los números que esas funciones consultan son 0, 20, 32, 50, 75, 81, 88, 90, 94,
  100, 105, 107, 109, 113, 144, 146 y 165. Nadie pregunta por el 29.
- **wifip2pd**: no tiene sección `__objc_intobj` ni referencias al selector `capabilities`; sólo
  usa las pruebas de bit de §2.
- CoreWLAN, CoreWiFi y demás clientes de `-[CWInterface capabilities]` (sharingd, agentes de la
  barra de menús) no se barrieron por valor: la imagen de CoreWiFi tallada del shared cache no
  conserva bien las funciones `?`.

## 3. Veredicto

Con lo encontrado, la capacidad 29 sólo cambia comportamiento AWDL: sociales de 5 GHz en la
secuencia, descubrimiento asistido y un bit del TLV 12. No aparece ningún consumidor en la ruta
de asociación, escaneo o roaming de la infraestructura, a diferencia de la capacidad 75, que
`airportd` usa para elegir la seguridad. Además el bit es veraz (la 8814AU es doble banda), y
todo Mac con radio de 5 GHz lo anuncia.

Riesgo que queda: un consumidor en userspace fuera de `airportd` y `wifip2pd` que pregunte por
el 29 por valor (§2.1) `?`. Si
tras el arranque con `3699d28e` la asociación al 153/80 cambia, la vuelta es reinstalar
`65266e39`.
