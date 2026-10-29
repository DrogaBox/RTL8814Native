# Ioctls `'i'` 130 (`0xC0806982`) y 131 (`0xC1206983`): ruta real y qué necesita el kext

Cierra los puntos abiertos de `AWDL_STAGE1_STUB_VALUES.md` §2/§5 sobre los dos ioctls que wifip2pd
emite tras crear `awdl0`. Base: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64); IDB
anotada. Todas las direcciones son VA. Corrige el rótulo de la fila 7 de la tabla de la secuencia
post-create, que los atribuía a `IO80211APIUserClient`: **no** van por el user client.

## 1. Cómo los emite el daemon (lado wifip2pd)

De `AWDL_WIFIP2PD_POST_CREATE.md` §2.5 y `AWDL_WIFIP2PD_CALLERS.md`: no es el user client. `sub_100058420`
abre un socket `AF_INET6/SOCK_DGRAM` (`sub_1001CFA90(30,2,0)`) y emite dos `ioctl(fd, cmd, buf)` BSD:

| Orden | Comando | Búfer | Contenido |
|---|---|---|---|
| 1 (puerta GET) | `0xC1206983` (`'i'` 131, IOWR 288) | 288 B a cero | sólo ifname `awdl0` @0 (`0x100058509`) |
| 2 (SET) | `0xC0806982` (`'i'` 130, IOWR 128) | 128 B a cero | ifname @0; `u16 @16 = 7708` (`0x1E1C`, `0x1000585c3`); 16 B @24..39 del `apple80211req` (`{val_len u32, val ptr}`) del llamador (`0x1000585db`) |

El SET sólo se ejecuta si el GET devuelve 0 y `*(a1+16) != 0` (`0x10005852b`). El método Swift es
`throws`: si el GET falla, no hay SET y el error se absorbe con `errno`. El número `'i'` 130/131 se
construye en el daemon, no viaja al kernel como tal; lo que cruza es el `apple80211req` del búfer.

## 2. Qué los atiende en el kernel (sobre `awdl0`)

Un ioctl BSD sobre `awdl0` entra por `IO80211VirtualInterface::handleIoctl` (`0xffffff8002331c88`). Ahí
sólo se desvían a `handleIoctlGated` las cuatro constantes canónicas `SIOCxA80211` (`0x802069C8`,
`0x802869C8`, `0xC02069C9`, `0xC02869C9`); **cualquier otro comando cae a**
`IO80211SkywalkInterface::processBSDCommand` (`0xffffff8002331d1b` → `0xffffff80022f6e98`). `0xC0806982`
y `0xC1206983` no son ninguna de las cuatro, así que van por esa rama.

`IO80211SkywalkInterface::performGatedCommandIOCTL` (`0xffffff80022f779a`) **no** filtra por número de
ioctl ni rechaza los no canónicos:

| VA | Hecho |
|---|---|
| `0xffffff80022f77b8` | Si `*(*(this+0x120)+0x30) == 0` devuelve 22; si no, `csr_check(4)` condicional y `isCallingProcessEntitled("com.apple.wlan.authentication")`. |
| `0xffffff80022f779a`+ | Copia el búfer a un `apple80211req` en pila (`v14..v18`); el `u16` de tipo del daemon queda en `req+0x10` (`v16 = a3[2]`). |
| coerción | `v13 = 0xC02869C9`; si `(cmd & 0xFFF7FFFF) == 0x802069C8`, `v13 = 0x802869C8`. El número original se descarta: **siempre** llama `apple80211RouteExternal(this, 0, v13, &req, 0, flag)`. |
| despacho | `apple80211RouteExternal` (`0xffffff800236cd2e`) indexa la tabla por el id interno `req+0x10`, validado `[1, 586]` moderno / `[1, 457]` legacy (`AWDL_STAGE1_DISPATCH_PATH.md` §1.3). |

## 3. Resultado

- **131 (GET)**: el búfer no fija tipo, así que el id interno es 0. `0 < 1` → fuera de rango →
  `IOLog("IOCTL req out of bounds")` y devuelve **102**.
- **130 (SET)**: el id interno es **7708**. `7708 > 586` → fuera de rango en las dos ramas → **102**.
- Ninguno de los dos indexa la tabla de manejadores, así que **no se alcanza ningún slot puro de
  `IO80211AWDLProtocol`** (`AWDL_IOCTL_SLOT_MAP.md`). El 7708 no corresponde a ningún método apple80211.

El GET (131) ya devuelve no-cero, de modo que el SET (130) ni siquiera se emite; el daemon traga el
error por su `throws`. Coincide con que `AWDL_WIFIP2PD_CALLERS.md` no documenta rama de aborto: el par
131/130 **no es fatal** para la creación de `awdl0`.

## 4. Qué necesita el kext en la etapa 1

**Nada.** `RTLAwdInterface` no debe sobrescribir `handleIoctl` para estos comandos: el comportamiento
heredado (caída a `performGatedCommandIOCTL` → `RouteExternal` → 102 por id fuera de rango) es
exactamente lo que hace el sistema real, y el daemon continúa su secuencia. Implementar un manejador
para el tipo 7708 sería desviarse del kernel de Broadcom sin que el daemon lo exija.

## 5. Pendiente

| Punto | Resultado |
|---|---|
| semántica del tipo 7708 (qué configuraría si existiera) | `?`; no es un id apple80211 de `[1, 586]`, no hay manejador |
| propiedad que alimenta el ifname (keypaths `unk_10045FE58`/`unk_10045FE78`) | `?` (descriptores no resueltos) |
| `sub_1000586D0` (variante con nombre de closure) | sin callers de código (`?`) |
