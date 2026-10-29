# Valores de stub para la etapa 1: qué pide wifip2pd a `awdl0` y qué devolver

Revisión, en el orden de `AWDL_WIFIP2PD_POST_CREATE.md`, de lo que el daemon envía tras el alta de
`awdl0`, con la ruta de cada petición y el stub recomendado para la etapa 1. El hallazgo principal es
que **la secuencia post-create documentada no contiene ninguna petición que termine en uno de los 63
slots puros de `IO80211AWDLProtocol`**: los slots no están en el camino crítico del daemon mientras
sólo se cree la interfaz.

## 1. La secuencia, con su ruta real

| # | Request | Código | Ruta | ¿Slot AWDL? |
|---|---|---|---|---|
| 1 | `RawGet` `CARD_CAPABILITIES` | 12 | tabla get; sobre `en2`, antes de crear | no |
| 2 | `RawSet` `VIRTUAL_IF_CREATE` | 94 | tabla set → puente → slot `+0x1160` de `IO80211InfraProtocol` | no; crea `awdl0` |
| 3 | espera del semáforo (no es petición) | — | notificación IORegistry | — |
| 4 | `RawGet` `VIRTUAL_IF_PARENT` | 97 | tabla get `getVIRTUAL_IF_PARENT` (`0xffffff800227b7ff`) | no; devuelve el nombre del padre |
| 5 | `RawSet` `COMPANION_SKYWALK_IF_CREATE` | 293 | tabla set `setCOMPANION_SKYWALK_IF_CREATE` (`0xffffff800228b4b8`, `gSetHandlerTable[292]`) | no |
| 6 | ioctls ifnet (`SIOCGIFMEDIA`, `SIOCSIFFLAGS`, `SIOCSIFLLADDR`, `SIOCSIFADDR`, `SIOCADDMULTI`, …) | — | camino BSD/IOSkywalk | no |
| 7 | ioctl `'i'` 131 (`0xC1206983`) e ioctl `'i'` 130 (`0xC0806982`, tipo 7708) | — | `IO80211APIUserClient` | no |
| 8 | monitoreo de eventos (`Apple80211RawP2PEventMonitoringInit` / `…EventMonitoringInit`) | — | user client + `createEventPipe` | no |

Fuentes: `Docs/AWDL_WIFIP2PD_POST_CREATE.md` (tabla de la secuencia y payloads) y
`Docs/AWDL_IOCTL_SLOT_MAP.md` (los 63 slots se alcanzan desde puentes `apple80211get*/set*`, que la
secuencia anterior no invoca). Consecuencia directa para la etapa 1: los stubs
`kIOReturnUnsupported` de los 63 slots no bloquean la creación de `awdl0` ni la salida de la espera de
30 s; lo que decide es el 94 y, después, el 97, el 293, `SIOCGIFMEDIA` y los ioctls 130/131.

## 2. Qué devolver en cada paso

| # | Recomendación | Motivo |
|---|---|---|
| 2 (94) | `kIOReturnSuccess` y `payload+0x10` con el nombre `awdl0` | Es la condición que libera la espera del daemon (`AWDL_WIFIP2PD_DISCOVERY.md`); un fallo deja al daemon sin interfaz y el ciclo se repite |
| 4 (97) | nombre de la interfaz primaria (`en2`) como C-string; `kIOReturnSuccess` | El handler del kernel ya lo hace así (`getPrimarySkywalkInterface`, `AWDL_STAGE1_OPEN_POINTS.md` §3); el stub no hace falta |
| 5 (293) | `102` y no crear `llw0` | La ausencia de la companion sólo produce el log de `AWDL_LLW_COMPANION.md`; el daemon continúa con `awdl0` |
| 6 (`SIOCGIFMEDIA`) | `ifm_current = 0x80` | Exigido por el filtro `(ifm_current & 0xE0) == 0x80` (`AWDL_STAGE1_IFM_CURRENT.md` §1); sale del descriptor de medios, no del `RegistrationInfo` directo |
| 7 (130/131) | `kIOReturnSuccess` con el payload vacío o el eco del `?` | No están documentados como fatales; el daemon no ramifica sobre el contenido en la secuencia leída (`?` en `AWDL_WIFIP2PD_CALLERS.md`) |
| 8 (eventos) | dejar que la familia cree el canal | Sólo exige rol 6 y la autorización `com.apple.wifi_awdl.event_monitor` (`AWDL_STAGE1_DISPATCH_PATH.md` §2) |

## 3. Los 63 slots: qué hace Proximity en la cola de la vtable

Muestreo de las cuatro últimas celdas del vtable concreto `__ZTV30AppleBCMWLANProximityInterface`
(`0xffffff80017f5b68`, vptr `+0x10`), leídas en esta pasada:

| Celda | Método | Implementación | Comportamiento |
|---:|---|---|---|
| `+0x11A0` | `setLLW_PARAMS` | `0xffffff8001726ac8` (2677 B) | configura LLW; reenvía al commander/features del core |
| `+0x11A8` | `setTX_RATE` | `0xffffff8001720c20` (455 B) | fija la tasa de TX; reenvía |
| `+0x11B0` | `setP2P_COEX_PROTECTION` | `0xffffff800172a968` (820 B) | valida estado y reenvía al core |
| `+0x11B8` | `setPSF_DWELL` | `0xffffff800171dfc0` (419 B) | configura dwell de PSF; reenvía |

Estas celdas son offsets del vtable concreto de `AppleBCMWLANProximityInterface`, cuya disposición no
coincide con los offsets de los 63 slots puros de `IO80211AWDLProtocol` de `AWDL_IOCTL_SLOT_MAP.md`
(en este vtable, p. ej., `+0xFC8` y `+0xFD0` siguen siendo `_RESERVEDIO80211VirtualInterface14/15`). Lo
que importa aquí es la forma de los métodos, no su numeración. Ninguno de los cuatro devuelve un ivar
cacheado ni una constante: las cuatro son setters que hablan con el core (commander/features) y pueden
fallar por estado del firmware. `?` El resto de los 63 slots no se ha muestreado en esta pasada; por el
patrón de los cuatro leídos y por
`AWDL_PROXIMITY_OVERRIDES.md`, se espera la misma forma (setters que reenvían, getters que leen el
core o un ivar).

Stub recomendado para los 63: `kIOReturnSuccess` sin tocar el payload, salvo que una prueba de
hardware demuestre que el daemon valida el contenido. `?` Qué getters de los 63 usa el daemon fuera de
la secuencia post-create (p. ej. al abrir AirDrop) no está en las dos listas consultadas.

## 4. Fatales y no fatales

| Petición | ¿Fatal? | Evidencia |
|---|---|---|
| 94 | sí | sin `awdl0` no hay nada que wifip2pd pueda usar; la espera de 30 s expira (`AWDL_WIFIP2PD_DISCOVERY.md`) |
| 12 | sí | sin la capacidad AWDL el daemon no entra en la rama de rol 6 (`AWDL_WIFIP2PD_POST_CREATE.md` fila 1) |
| 97 | no consta (`?`) | el daemon guarda `parentInterfaceName`; no hay rama de aborto documentada |
| 293 | no | la ausencia de `llw0` se registra y la secuencia sigue (`AWDL_LLW_COMPANION.md`) |
| `SIOCGIFMEDIA` | sí | el filtro de medios decide si la interfaz se acepta (`AWDL_STAGE1_IFM_CURRENT.md` §6) |
| 130/131, eventos | no consta (`?`) | `AWDL_WIFIP2PD_CALLERS.md` no documenta rama de aborto |

## 5. Pendiente

| Punto | Búsqueda ejecutada | Resultado |
|---|---|---|
| ¿algún slot de los 63 en la secuencia post-create? | contraste de las dos listas con `AWDL_IOCTL_SLOT_MAP.md` | ninguno |
| comportamiento de los 59 slots restantes | no ejecutada | `?` |
| fatalidad de 97, 130/131 y eventos | lectura de `AWDL_WIFIP2PD_CALLERS.md` | sin rama de aborto documentada (`?`) |
| slot del 293 | lectura de `gSetHandlerTable[292]` | `setCOMPANION_SKYWALK_IF_CREATE` (`0xffffff800228b4b8`) |
