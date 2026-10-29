# Ruta de despacho de apple80211 y creación del canal de eventos

Traza, sobre el kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), cómo llega una petición `apple80211` al
manejador del código 94 y qué objeto viaja como interfaz, y localiza el lado kernel del monitoreo
de eventos. Todas las direcciones son VA. Lo que el desensamblado no fija queda marcado con `?`.

## 1. Ruta de una petición hasta el manejador

### 1.1 Entrada por ioctl BSD (`SIOCSA80211`/`SIOCGA80211`)

| Paso | VA | Detalle |
|---|---|---|
| Entrada | `0xffffff80022f6e98` `IO80211SkywalkInterface::processBSDCommand(__ifnet *, unsigned, void *)` | El segundo argumento (`__ifnet *`) sólo aparece en la firma: el cuerpo no lo lee y despacha sobre `this`. `?` Cómo obtiene el glue el puntero `this` a partir del `ifnet` no se ha cerrado aquí. |
| Gating | — | La orden se ejecuta en el work loop de la interfaz; el cuerpo llama a `performGatedCommandIOCTL` en `0xffffff80022f70e8`. |
| Validación | `0xffffff80022f779a` `IO80211SkywalkInterface::performGatedCommandIOCTL(this, cmd, data)` | Devuelve 22 si `*(*(this+0x120)+0x30) == 0`; ejecuta `csr_check(4)` si el glue lo pide y `isCallingProcessEntitled("com.apple.wlan.authentication")`. |
| Armado | `0xffffff80022f779a` | Copia el `ifreq` a un `apple80211req` en pila (`v14..v18`) y llama a `apple80211RouteExternal`. |
| Despacho | `0xffffff80022f78ad` | `apple80211RouteExternal(this, 0, cmd, &req, 0, flag)`: `a5 = 0`, es decir tablas **legacy**. |

### 1.2 Entrada por `IO80211APIUserClient`

| Paso | VA | Detalle |
|---|---|---|
| Método externo | `0xffffff80023526ae` `IO80211APIUserClient::_runDriverCommandHelper(IOExternalMethodArguments *)` | Camino normal; el de buffer grande es `0xffffff80023537d8` `_runLargeBufferCommandHelper`. |
| Interfaz | `0xffffff8002352bfd` | `performGatedCommandIOUC(*(apiUC+0xD8+8), cmd, data, apiUC)`: la interfaz sale del estado del **user client** (la que abrió el cliente), no de una búsqueda. En el camino de buffer grande, `0xffffff8002353fb5`. |
| Thunk | `0xffffff80022f6e26` `IO80211SkywalkInterface::processBSDCommandGated(this, cmd, data, apiUC)` | Sólo reenvía a `performGatedCommandIOUC`. |
| Validación | `0xffffff80022f6e30` `performGatedCommandIOUC` | Mismas comprobaciones que la ruta ioctl y `apple80211RouteExternal(this, apiUC, cmd, req, 1, 0)` en `0xffffff80022f6e8d`: `a5 = 1`, tablas **modernas**. |

### 1.3 `apple80211RouteExternal`

`0xffffff800236cd2e` `apple80211RouteExternal(IO80211SkywalkInterface *iface, IO80211APIUserClient *uc, unsigned code, apple80211req *req, bool moderno, bool flag)`:

| VA | Hecho |
|---|---|
| `0xffffff800236cd2e`+ | `ctrl = (*(*iface + 0xBA8))(iface)` (`getController()`); si es nulo, devuelve 102. |
| — | Si `*(uint32_t *)(req + 0x10) == -1` (`APPLE80211_IOC_CARD_SPECIFIC`), llama `(*(*ctrl + 0xC98))(ctrl, iface, code, req, moderno)` y no toca las tablas. |
| `0xffffff800236ce27` | Tabla **moderna** de SET: `gSetHandlerTable` (`0xffffff800245fef0`), entradas de 8 bytes, índice `code - 1`, rango válido `[1, 586]` (fuera de rango: `IOLog` y 102). La de GET es `gGetHandlerTable`, con el mismo rango. |
| `0xffffff800236ce2e` | Carga de la entrada: `mov rax, [gSetHandlerTable + rax*8]`. |
| — | Tablas **legacy** (`a5 == 0`): `gSetHandlerTableLegacy` (`0xffffff8002479060`, rango `[1, 457]`) y `gGetHandlerTableLegacy` (`0xffffff8002470d50`, rango `[1, 581]`). |
| — | Firma de llamada. Moderna, en SET y en GET: `handler(ctrl, iface, apiUC, req)`. Legacy: `handler(ctrl, iface, req, flag)`. Entrada nula: 102. |
| `0xffffff80024601d8` | Entrada 93 de `gSetHandlerTable`: `setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`), firma `(IO80211Controller *, IO80211SkywalkInterface *, IO80211APIUserClient *, apple80211req *)`. El código de la petición es 94 y el índice es `code - 1`. |
| `0xffffff8002479348` | Entrada 93 de `gSetHandlerTableLegacy`: `setVirtualInterfaceCreate` (`0xffffff8002357f33`). Exige `req+0x18 != 0` y `req+0x20 != 0` (si no, 22), copia `min(len, 32)` bytes con `IO80211Controller::copyIn`, aplica la misma regla de WCL que el manejador moderno (`IO80211Glue::sendIOUCToWcl(glue, 0x802869C8, 0x5E, …)` y, si no hay WCL o lo rechaza, `apple80211setVIRTUAL_IF_CREATE(iface)`), y con éxito devuelve los 32 bytes con `copyOut`. La entrada 93 de `gGetHandlerTableLegacy` es nula. |

### 1.4 Qué objeto es la interfaz

Los dos caminos terminan en el mismo puente, `apple80211setVIRTUAL_IF_CREATE`, con la misma
interfaz como argumento. Respondido para los dos caminos: **la interfaz sobre la que se emitió la petición**, es decir `this`
en `processBSDCommand` (ioctl) y el puntero guardado en el user client en `+0xD8+8` (API), no la
interfaz primaria del controlador y no un resultado de buscar `apple80211req.ifname`. El
controlador sólo entra como primer argumento (`getController()`, slot `+0xBA8`) y como destino del
camino `CARD_SPECIFIC` (slot `+0xC98`).

`?` Que el `this` del ioctl sea siempre la interfaz dueña del `ifnet` que recibió la orden no se ha
verificado instrucción por instrucción en el glue; el cuerpo de `processBSDCommand` no lo comprueba.

## 2. Monitoreo de eventos: lado kernel

### 2.1 Creación del canal

| VA | Hecho |
|---|---|
| `0xffffff800235256e` `IO80211APIUserClient::extEventMonitorInit` | Método externo de inicio del monitoreo: registra `enter` y salta a `IO80211APIUserClient::createEventPipe` (`0xffffff80023525a5`). |
| `0xffffff8002355702` `IO80211APIUserClient::createEventPipe` | Toma el `IO80211CommandGate` de `*(apiUC+0xD8)+0x10` y llama a su slot `+0x128`, que es `IO80211CommandGate::runActionBlock`, con el bloque `createEventPipe_block_invoke`. Con éxito marca el byte `+0x180` del estado del cliente; en todo caso marca `+0x171`. |
| `0xffffff80023557e6` (bloque `0xffffff80023557d0`) | El bloque toma la interfaz de `*(apiUC+0xD8)+8` y salta a su slot `+0xDC0` (440). |
| vtables | `+0xDC0` es `IO80211SkywalkInterface::createEventPipe` (`0xffffff80022f6bf2`) en `IO80211SkywalkInterface` e `IO80211VirtualInterface::createEventPipe` (`0xffffff80023347fc`) en `IO80211VirtualInterface`. El 440 de `Docs/AWDL_STAGE1_IMPL_SPEC.md` §9 es correcto. |
| `0xffffff80023347fc` `IO80211VirtualInterface::createEventPipe(apiUC)` | **Ya cubre el rol 6**: si `*(*(this+0x120)+0x58) == 6` exige la autorización `com.apple.wifi_awdl.event_monitor`. Los roles 8 y 9 exigen `com.apple.wifi_nan.event_monitor`. Si la autorización falta, registra `"user client from %s not authorized to monitor events on %s"` y devuelve `0xE00002E2`; en otro caso salta a la implementación base (`0xffffff8002334863`). |
| `0xffffff80022f6bf2` `IO80211SkywalkInterface::createEventPipe(apiUC)` | Sobre el contenedor de canales `*(*(this+0x120)+0xA8)`: lee la cuenta por su método en el byte `+0x130` y exige menos de `0x18` (si no, `0xE0000001`); crea `IO80211IOUCEventPipe::withClient` (`0xffffff80022a71d8`) y lo añade por el método en el byte `+0x1B8` del contenedor (fallo de reserva: `0xE00002BC`). Ese `+0x1B8` es un desplazamiento en bytes de otra clase y no tiene relación con el slot 440 de la interfaz. |
| `0xffffff80023525e6` `IO80211APIUserClient::extQueryUseIOUCEventPipe` | Lee el boot-arg `wlan.useIOUCEventPipe` y devuelve si debe usarse el canal IOUC. |

### 2.2 Qué hace falta en el driver

Nada específico. `RTLAwdInterface` no sobrescribe el slot `+0xDC0`, así que hereda
`IO80211VirtualInterface::createEventPipe`, y la llamada es virtual sobre la interfaz que abrió el
user client, sin depender de la clase concreta. La creación del canal, el límite de 24 y la
comprobación de autorización los resuelve la familia. Los únicos requisitos son que el proceso que
abre el user client tenga `com.apple.wifi_awdl.event_monitor`, que el rol en `*(this+0x120)+0x58`
sea 6, y que `awdl0` exista y esté publicada antes de que `wifip2pd` abra su cliente.

## 3. Consecuencias para la etapa 1

- El manejador del 94 recibe la interfaz del **ioctl o del user client**, así que el `safeMetaCast` a
  `IO80211InfraProtocol` sólo puede tener éxito si la petición se emite sobre una interfaz cuyo
  objeto sea `RTLAirInterface` (rol 1). Una petición dirigida a `awdl0` no llegaría a ese puente.
- La ruta ioctl usa tablas legacy y la del user client tablas modernas. El 94 tiene manejador en
  las dos (`setVirtualInterfaceCreate` y `setVIRTUAL_IF_CREATE`) y ambos acaban en el mismo puente,
  así que el override de `setVIRTUAL_IF_CREATE` en la interfaz infra cubre los dos caminos. La ruta
  legacy acepta longitudes menores que 32 (copia `min(len, 32)`); la moderna exige 32.
- El canal de eventos no necesita código nuevo: basta con que `awdl0` esté creada y con la
  autorización del proceso. El fallo de autorización se ve como `0xE00002E2` en el log de la familia.

## 4. Puntos abiertos

- Resolución del `this` a partir del `ifnet` en `processBSDCommand` (§1.1).
