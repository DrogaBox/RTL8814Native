# De un ioctl BSD sobre `en2` o `awdl0` a `apple80211RouteExternal`: qué objeto es la interfaz

Cierra el punto abierto de `AWDL_STAGE1_DISPATCH_PATH.md` §4: cómo llega un
`SIOCSA80211`/`SIOCGA80211` emitido sobre un nombre BSD hasta el despacho de apple80211 y qué objeto
viaja como interfaz. Base: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). IOSkywalkFamily
forma parte del mismo kernelcache, así que el camino se lee completo. Todas las direcciones son VA.

## 1. Del `ifnet` al objeto: IOSkywalkFamily

| VA | Hecho |
|---|---|
| `0xffffff8002b002e6` `IOSkywalkNetworkBSDClient::if_ioctl(__ifnet *, unsigned long, void *)` | Función `if_ioctl` del `ifnet`. Obtiene el `IOSkywalkNetworkBSDClient` con `ifnet_softc(ifp)` (`0xffffff8002b002f9`) y salta (`0xffffff8002b0032c`) al slot `+0x1C8` del objeto en `client+0x90`, pasándole el objeto en `client+0x108`, el `ifnet`, el comando y los datos. |
| `0xffffff8002affce6` `IOSkywalkNetworkBSDClient::ioctlHandler(__ifnet *, unsigned, void *)` | Manejador que corre bajo ese mecanismo. Toma la interfaz de `client+0xE8`, comprueba su estado de energía (slot `+0x870`) y llama a su slot `+0x910` en `0xffffff8002affe97` con `(iface, ifp, cmd, data)`. |
| vtables | `+0x910` es `processBSDCommand` en toda la jerarquía: `IOSkywalkNetworkInterface` (`0xffffff8002b3d2b8`), `IO80211SkywalkInterface`, `IO80211InfraInterface` / `IO80211InfraProtocol` e `IO80211VirtualInterface`. |

La interfaz sale del `softc` del `ifnet` (el cliente BSD que IOSkywalkFamily crea para esa interfaz),
no de una búsqueda por nombre. Cada `ifnet` tiene su cliente y cada cliente su interfaz.

## 2. `en2`: interfaz de infraestructura (`RTLAirInterface`)

`RTLAirInterface` hereda de `IO80211InfraProtocol` y no sobrescribe `processBSDCommand`. En la vtable de
`IO80211InfraProtocol` (`0xffffff80024656a0`) el slot `+0x910` es `IO80211InfraInterface::processBSDCommand`.

| VA | Hecho |
|---|---|
| `0xffffff8002360290` `IO80211InfraInterface::processBSDCommand` | Llama al método base en `0xffffff800236038f`. |
| `0xffffff80022f6e98` `IO80211SkywalkInterface::processBSDCommand` | Llama a `performGatedCommandIOCTL(this, cmd, data)` en `0xffffff80022f70e8`. |
| `0xffffff80022f779a` `IO80211SkywalkInterface::performGatedCommandIOCTL` | Arma el `apple80211req` y llama `apple80211RouteExternal(this, 0, cmd, &req, 0, flag)` en `0xffffff80022f78ad` (tablas legacy). |

Interfaz que recibe el despacho: `RTLAirInterface`.

## 3. `awdl0`: interfaz virtual (`RTLAwdInterface`)

`RTLAwdInterface` no sobrescribe `processBSDCommand` ni `handleIoctl`, así que usa los de
`IO80211VirtualInterface`. Este camino **no** pasa por `IO80211SkywalkInterface::processBSDCommand` para
las peticiones apple80211.

| VA | Hecho |
|---|---|
| `0xffffff8002331b36` `IO80211VirtualInterface::processBSDCommand` | Salta (`0xffffff8002331b47`) al slot `+0xE80` de `this`, que es `handleIoctl`. |
| `0xffffff8002331c88` `IO80211VirtualInterface::handleIoctl(__ifnet *, unsigned long, void *)` | Para `0x802069C8`, `0x802869C8` (`SIOCSA80211`), `0xC02069C9` y `0xC02869C9` (`SIOCGA80211`) obtiene el controlador con `getController()` (slot `+0xBA8`) y ejecuta `handleIoctlGated` a través del slot `+0x850` del controlador, con `this` como objetivo. Cualquier otro comando va a `IO80211SkywalkInterface::processBSDCommand` (`0xffffff8002331d1b`). |
| `0xffffff8002331d20` `IO80211VirtualInterface::handleIoctlGated` | Devuelve 22 si `getController()` es nulo (`0xffffff8002331d47`) y 6 si el bit 0 de `*(this+0x128)+0x52` está activo. Si no, copia el `ifreq` a un `apple80211req` en pila y llama `apple80211RouteExternal(this, 0, cmd, &req, 0, flag)` en `0xffffff8002331e56` (tablas legacy). Con éxito devuelve al llamador los campos `+0x14` y `+0x18`. |

Interfaz que recibe el despacho: `RTLAwdInterface`. Este camino no hace las comprobaciones de
`performGatedCommandIOCTL` (`csr_check` y autorización `com.apple.wlan.authentication`).

## 4. ¿Se lee `apple80211req.ifname` (`req+0`)?

No para elegir la interfaz. En los dos caminos el `ifreq` se copia al `apple80211req` y el nombre
queda en `req+0`. `apple80211RouteExternal` (`0xffffff800236cd2e`) despacha sobre su primer argumento:
`getController()` por el slot `+0xBA8` y las tablas por código. `?` No se han revisado los manejadores
uno a uno; si alguno leyera `req+0`, lo haría después del despacho y no cambiaría la interfaz.

## 5. Resumen para la etapa 1

| Pregunta | Respuesta | Evidencia |
|---|---|---|
| ¿Cómo se pasa del `ifnet` a la interfaz? | `ifnet_softc` da el cliente BSD de IOSkywalkFamily, y su campo `+0xE8` es la interfaz | `0xffffff8002b002f9`, `0xffffff8002affe97` |
| Interfaz para un ioctl sobre `en2` | `RTLAirInterface`, vía `IO80211InfraInterface::processBSDCommand` y `performGatedCommandIOCTL` | `0xffffff800236038f`, `0xffffff80022f78ad` |
| Interfaz para un ioctl apple80211 sobre `awdl0` | `RTLAwdInterface`, vía `handleIoctl` y `handleIoctlGated` | `0xffffff8002331b47`, `0xffffff8002331e56` |
| ¿Se busca la interfaz por nombre? | No | §1, §4 |

Implicación para la etapa 1: un 94 emitido sobre `en2` llega al manejador legacy 94
(`setVirtualInterfaceCreate`, `AWDL_STAGE1_DISPATCH_PATH.md` §1.3) con `RTLAirInterface`, que supera el
`safeMetaCast` a `IO80211InfraProtocol` del puente. Uno emitido sobre `awdl0` llega con `RTLAwdInterface`,
el cast falla y no alcanza el slot `+0x1160`. Las peticiones apple80211 sobre `awdl0` necesitan que el
controlador exista (si no, 22) y que el bit 0 de `*(this+0x128)+0x52` esté a cero (si no, 6).
