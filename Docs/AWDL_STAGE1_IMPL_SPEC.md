# Especificación de implementación — etapa 1 de AWDL en RTL8814AirLayer

Documento de implementación de la **etapa 1** del plan de [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
§3: `wifip2pd` ve la capacidad AWDL, pide `VIRTUAL_IF_CREATE` con el rol 6, la interfaz virtual
`awdl0` nace y queda publicada en el IORegistry con rol `AirLink`, y el daemon deja de esperar sus
30 s. La companion `llw0` (tipo 293) queda fuera del alcance obligatorio y se especifica como
opción.

Para cada pieza se fija: el método o handler de AirLayer que se añade o sobrescribe, el punto del
código actual donde engancha (referencias `Source/AirLayer/<fichero>:<línea>` de sólo lectura), el
layout exacto del payload y los códigos de retorno, los símbolos de IO80211Family que se llaman
(todos exportados, [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1–§2), el orden y el bloqueo, el
tratamiento de fallos y las pruebas. No se ha modificado ningún fichero de código, de build ni de
herramientas para escribir este documento.

Convenciones:

- Direcciones sin prefijo = VA del kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build
  25G241, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`); `w2p` = VA de
  `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`).
- `?` = no verificado en `Docs/` ni en el código. Toda afirmación sin cita de sección o de
  `fichero:línea` lleva `?`.
- Numeración de slots: [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) numera desde la primera celda
  virtual con `slot 2 = vptr+0` (offset = `8 × (slot − 2)`); los comentarios de
  `Source/IO80211/IO80211_25_6_0.h` usan `offset = 8 × slot`. Las dos convenciones difieren en 2
  posiciones; las dos describen la misma vtable (el «slot 460» de
  [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) está en el offset `0xE50`, que es la etiqueta 458
  de la cabecera). En este documento manda **el offset**; los números de slot se citan con la
  convención del documento de origen y la equivalencia se precisa en §5.2.

## 0. Resumen, flujo y criterio de aceptación

### 0.1 Flujo de la etapa 1

| Paso | Qué ocurre | Dónde se implementa |
|---:|---|---|
| 1 | `wifip2pd` lee `CARD_CAPABILITIES` (tipo 12) sobre la interfaz de infraestructura y encuentra el bit AWDL. | Sin cambios: ya está en `RTLAirController::getCARD_CAPABILITIES` (`Source/AirLayer/RTLAirLayer.cpp:1755`), byte `+0x08` bit 0 en `:1776`. |
| 2 | El daemon crea el handle por rol, emite `RawSet` 94 `VIRTUAL_IF_CREATE` (32 B, rol 6, MAC en `+0x04`) y espera 30 s. | Nuevo override `RTLAirInterface::setVIRTUAL_IF_CREATE` (§4), sobre el stub actual de `RTLAirInterfaceStubs`. |
| 3 | El kext crea el objeto `awdl0`, lo adjunta al controlador y lo arranca con su `RegistrationInfo`. | Nueva clase `RTLAwdInterface : IO80211AWDLProtocol` (§5). |
| 4 | El arranque registra la interfaz en Skywalk, el cliente BSD de la familia crea el ifnet y publica `IOInterfaceName`; `registerService(2)` deja todo hecho antes de volver al handler. | `RTLAwdInterface::startWithRegistration` + `IOSkywalkEthernetInterface::registerEthernetInterface` + `registerService(kIOServiceSynchronous)` (§5.5, §6). |
| 5 | El handler escribe el nombre BSD en `payload+0x10` y devuelve 0. | `RTLAirInterface::setVIRTUAL_IF_CREATE` (§4.4). |
| 6 | La notificación `IOServiceMatched` de `wifip2pd` encuentra `IOInterfaceName`, `IO80211VirtualInterfaceRole = "AirLink"` y un índice BSD vivo; libera el semáforo. | Consecuencia de los pasos 3–5 (§6). |

### 0.2 Criterio de aceptación

1. En el log del daemon aparece `Found new interface awdl0 with role AirLink` (literal
   `0x100494bb0`, [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.2 paso 8) y
   **no** aparece `Timed out while waiting for interface awdl0` (`0x100493910`, escrito en
   `w2p 0x10004974b`, §1.4).
2. `ioreg -r -c RTLAwdInterface -l` muestra, en la entrada del objeto interfaz, las properties de
   §6.2 con los valores indicados.
3. Existe un ifnet `awdl0` con índice BSD distinto de cero (`if_nametoindex("awdl0") > 0`), MAC
   igual a la del payload del 94, MTU 1428 (`0x594`) y medios 802.11.
4. `RawGet` 97 `VIRTUAL_IF_PARENT` devuelve el nombre de la interfaz padre sin error (§7).
5. Ningún evento Apple80211 es condición de aceptación: lo bloqueante es que el registro del
   monitoreo devuelva 0 ([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1.7,
   [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §4.1).
6. La etapa no emite tráfico ni mantiene pares: el datapath de `awdl0` queda sin uso (§5.6).

## 1. Punto de partida en el repositorio

| Pieza existente | Estado | Referencia |
|---|---|---|
| Gate de versión | Abre sólo si la estructura y el build casan; `rtlairtrust`/`rtlairnogate` como escapes. `airGateOpen()` se evalúa en el arranque del controlador. | `Source/AirLayer/VersionGate.h:91-113`, `Source/AirLayer/RTLAirLayer.cpp:1402` |
| Nub y controlador | `RTLAirNub : IOService` publica la `IO80211WorkQueue`; `RTLAirController : IO80211Controller` (no es `IO80211InfraProtocol`). | `Source/AirLayer/RTLAirLayer.hpp:34-42`, `:127-131`, `Source/AirLayer/RTLAirLayer.cpp:1402-1530` |
| Interfaz de infraestructura | `RTLAirInterface : RTLAirInterfaceStubs : IO80211InfraProtocol`, con `IO80211SkywalkInterface::init(ctrl, mac)`, rol 1, id 1, `attach(ctrl)`. | `Source/AirLayer/RTLAirLayer.hpp:50-120`, `Source/AirLayer/RTLAirLayer.cpp:311-331` |
| Stubs de la superficie infra | `setVIRTUAL_IF_CREATE` existe ya como stub que devuelve `kIOReturnUnsupported`. | `Source/IO80211/IO80211_25_6_0_infra_stubs.inc:90`, declaración `Source/IO80211/IO80211_25_6_0.h:685` (slot 556, offset `0x1160`) |
| Registro de la interfaz STA | `RegInfo` de `0x130` B, `IO80211InfraInterface::start`, `createDatapath` con pool y 3 colas, `registerInfraEthernetInterface`, `registerService(kIOServiceAsynchronous)`. | `Source/AirLayer/RTLAirLayer.cpp:334-348`, `:350-388`, `:1502-1513` |
| Capacidad AWDL | Ya se publica el byte `+0x08` bit 0 (y el bit compañero `+0x07` bit 0) y se deja cerrada la puerta NAN (`+0x0D` bit 0). | `Source/AirLayer/RTLAirLayer.cpp:1749-1794` |
| Cabeza de serie AWDL | Existe un catálogo de firmas, **no** una declaración ABI: omite los slots 2–489 y 491–506; su propia cabecera lo advierte. No se incluye en el build actual. | `Source/IO80211/IO80211AWDL_25_6_0.h:1-6`, `:60-73`, `:75-141` |
| Build | Compila `RTLAirLayer.cpp` y enlaza `info.o` + `RTLAirLayer.o`. | `build_airlayer.sh:39`, `:63-67` |

## 2. Decisiones de diseño

| # | Decisión | Motivo | Estado |
|---:|---|---|---|
| D1 | La puerta del 94 se implementa como **override en `RTLAirInterface`** (objeto `IO80211InfraProtocol`), no en el controlador. | El puente castea a `IO80211InfraProtocol` y llama el slot `+0x1160`; el controlador no deriva de ese protocolo. El objeto receptor exacto del cast no está verificado (`?`). | [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1.3, §3.5; `Source/AirLayer/RTLAirLayer.hpp:50`, `Source/IO80211/IO80211_25_6_0.h:595` |
| D2 | `awdl0` se implementa como subclase de **`IO80211AWDLProtocol`** y no de `IO80211VirtualInterface` a secas. | La etapa 2 necesita que los `safeMetaCast` a `IO80211AWDLProtocol` alcancen el objeto; derivar de la base no recibiría las requests AWDL. | §3.5 del diseño; [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) |
| D3 | La declaración de clases se hace en un **fichero nuevo** con la cadena completa por offsets; no se usa el catálogo disperso. | Con el catálogo actual los índices C++ no coinciden con la vtable real (faltan los 27 métodos `0xE68`–`0xF38` antes del slot 490 y los 16 reservados después), y el objeto nacería con la vtable desplazada 27 posiciones. | `Source/IO80211/IO80211AWDL_25_6_0.h:1-6`; [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) filas 463–506 |
| D4 | El 94 se atiende **en línea** en el handler, sin thread call ni espera, y `registerService(2)` es síncrono. | Es lo que hace Broadcom (`registerService(2)` en `0xffffff800172982a`) y deja el ifnet creado antes de devolver el nombre; el daemon no necesita ninguna petición más. | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 paso 6, §5 |
| D5 | El `RegistrationInfo` de `awdl0` se rellena con prefijo `awdl`, unit 0, MTU `0x594` y medios `0x80`/`0x80`. | El filtro del daemon exige `(ifm_current & 0xE0) == 0x80`; la semilla Broadcom `8` no pasaría y el actualizador intermedio no está identificado (`?`). | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §6.1–§6.3; §3.3 del diseño |
| D6 | El tipo 293 no se implementa en la etapa 1: el dispatch devuelve 102 y el daemon lo absorbe. | El fallo no contamina `awdl0`: no hay teardown ni reintento, y la espera de 30 s sólo aparece si el kext contesta éxito sin crear nada. | [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4 |
| D7 | Toda la vía se habilita con un boot-arg nuevo (`rtlawdl=1`), apagado por defecto. | Permite comparar A/B contra el comportamiento actual (102) sin recargar el kext y acota el radio de un fallo en el arranque. | Práctica del propio layer: `rtlair` en `Source/AirLayer/RTLAirLayer.cpp:1402-1445` |

## 3. Pieza A — Puerta de capacidad (`RawGet` 12, `CARD_CAPABILITIES`)

### 3.1 Contrato

`wifip2pd` abre la rama AWDL con `RawGet` tipo 12, buffer de 21 B (`w2p 0x1000483a8`), y prueba el
bit 0 de `data[4]` (`w2p 0x1000483b5`). El handler moderno hace copyback de `min(req_len, 21)` bytes
desde el byte 4 del struct de 28 B, de modo que `data[4]` es el **byte `+0x08`**
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1; [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
§1.1). El byte `+0x0D` bit 0 es la puerta NAN y debe quedar a 0.

Layout ([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §7, definición C):

```c
struct apple80211_capability_data {
    uint32_t version;        /* +0x00: 1 */
    uint8_t  flags[24];      /* +0x04: flags[4] bit0 = AWDL, flags[9] bit0 = NAN */
};
```

### 3.2 Estado actual y cambio necesario

`RTLAirController::getCARD_CAPABILITIES` (`Source/AirLayer/RTLAirLayer.cpp:1755-1794`) ya hace lo
que la etapa 1 necesita:

- `p[0..3] = 1`, `p[4..5] = 0xE66F`, `p[6] = 0x61`, `p[7] = 0x04`, `p[9] = 0x40`, `p[10] = 0x04`,
  `p[12..13] = 0x0201` (`:1761-1769`). En la numeración de `flags`, `p[4]` es `flags[0]` y `p[8]`
  es `flags[4]`.
- `p[7] |= 0x01` y **`p[8] |= 0x01`** (`:1775-1776`): la puerta AWDL (byte `+0x08` del struct) y el
  bit compañero que Broadcom enciende junto a ella ([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1, nota
  de `AppleBCMWLANCore::getCARD_CAPABILITIES`).
- `p[13]` (byte `+0x0D`) no recibe el bit 0: `p[13] |= 0x08` en `:1781-1783` enciende el bit 3
  (capacidad 75 de usuario, WPA3/SAE), no la puerta NAN. La puerta NAN queda cerrada, como exige
  §3.1 del diseño.

**Cambio de código: ninguno.** La pieza se limita a comprobar que una modificación futura no borre
el bit al reordenar el bloque. Se propone añadir un comentario de una línea sobre `:1776` que
nombre el tipo 12 y la sección, sin tocar el valor.

### 3.3 Prueba

| Comprobación | Esperado |
|---|---|
| Carga del kext con el gate abierto | `RTLAirGate = "open-validated: ..."` en el nodo `RTL8814Native` y línea `gate open-validated` en el log |
| Petición 12 sobre `en2` antes de crear nada | Devuelve 0 y el byte 4 del copyback (byte `+0x08`) vale `0x01` |
| Byte `+0x0D` | Bit 0 a 0 (NAN cerrado) |
| Log del daemon antes del 94 | Sin `Unable to find role for interface: ...` (`0x100494b60`) atribuible a la falta de capacidad |

`?` No se conoce un literal del daemon que registre explícitamente «capacidad AWDL presente»; la
prueba positiva es que emita el 94 (paso siguiente).

## 4. Pieza B — Puerta de creación (`RawSet` 94, `VIRTUAL_IF_CREATE`)

### 4.1 Contrato del handler

| Paso | Contrato | Referencia |
|---|---|---|
| Entrada | `gSetHandlerTable[93]` → handler `setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`); exige `len == 32` y `data != NULL`. | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2; [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4 |
| Ruta WCL | Intenta `sendIOUCToWcl(glue, 0x802869C8, 94, data, 32)`; si WCL no la consume, cae al puente. | ídem |
| Puente | `safeMetaCast` a `IO80211InfraProtocol` y llamada al slot **`+0x1160`** (slot 556). Sin cast → `0xE082280E`. | §1.3 del diseño |
| Payload | 32 B: `u32 f0 @+0x00` (lectura `?`), `mac[6] @+0x04`, `pad[2] @+0x0A`, `role u32 @+0x0C`, `ifname[16] @+0x10` (salida). | [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2, §7 |
| Rol | 6 = AWDL. Broadcom: 7 → APSTA, 8..10 → NAN, otro → `0xE0000001` + log `Invalid virtual interface role 0x%X name %s`. | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1 |
| Ya existe | Devuelve 17 y **escribe igualmente** el nombre en `+0x10` (bloque compartido). | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1, §5 |
| Éxito | Cero en `+0x10..+0x1F`, `strlcpy(payload+16, getBSDName(), 0x10)` y retorno 0. | §1.3 del diseño |

### 4.2 Punto de enganche

`RTLAirInterface` ya hereda la declaración del slot 556 y su stub:

- Declaración: `Source/IO80211/IO80211_25_6_0.h:685`, dentro de
  `class IO80211InfraProtocol : public IO80211InfraInterface` (`:595`).
- Stub actual: `Source/IO80211/IO80211_25_6_0_infra_stubs.inc:90` (vía
  `RTLAirInterfaceStubs`, `Source/AirLayer/RTLAirLayer.hpp:45-52`).
- El objeto está adjunto al controlador (`attachInterface(_iface, this)`,
  `Source/AirLayer/RTLAirLayer.cpp:1493`) y registrado (`registerService()` en `:1513`).

El override se añade a `RTLAirInterface` (declaración nueva en `RTLAirLayer.hpp`, junto a las demás
WCL de `:56-99`) y se implementa en `RTLAirLayer.cpp`. El parche mínimo está en §12.3.

Antes del cast, los puentes de la superficie AWDL consultan `isCommandProhibited` (slot `+0xCC8`);
el código actual devuelve `false` (`Source/AirLayer/RTLAirLayer.cpp:1726`), de modo que el 94 no queda
bloqueado por esa puerta.

`?` El puente podría castear el objeto que devuelve el controlador por otra vía (interfaz
principal, `getAwdlInterface()`); no está verificado cuál. Si el cast no aterriza en el objeto
`RTLAirInterface`, el handler devuelve `0xE082280E` y el daemon no crea nada: la prueba T2 de §14 lo
detecta de inmediato. Tampoco está verificado que la ruta WCL (`sendIOUCToWcl(…, 94, …)`) no consuma
la request antes de llegar al puente en este controlador (`?`); si la consumiera, el override no se
ejecutaría y el daemon seguiría sin interfaz.

### 4.3 Validación y códigos de retorno del override

| Condición | Retorno | Efecto lateral |
|---|---|---|
| `req == nullptr` o `len != 32` | `kIOReturnBadArgument` (`0xE00002C2`) | Log `awdl: create bad len`; nada más |
| `role == 6` y `rtlawdl` apagado o gate cerrado | `kIOReturnUnsupported` (`0xE00002C7`, 102) | Comportamiento actual del sistema; el daemon registra el fallo y sigue |
| `role` distinto de 6 y distinto de 7 | `0xE0000001` | Log `awdl: invalid role %u` (Broadcom usa el mismo valor, §2.1) |
| `role == 7` | `kIOReturnUnsupported` | APSTA queda fuera de la etapa 1 |
| Ya existe una interfaz AWDL | `17` (EEXIST) | `strlcpy(req->ifname, bsdName, 16)`; no se crea otra |
| Creación o arranque falla | El `IOReturn` de la causa (`0xE00002BC` si falla la creación del objeto, `?` otros) | Rollback completo (§11); **no** se escribe el nombre |
| Éxito | `0` | Nombre en `+0x10` y `setAwdlInterface()` ya hecho |

### 4.4 Secuencia del override (implementación)

```text
RTLAirController::createAwdlInterface(mac, out):
  IOLockLock(_awdlLock)
  si _awdl != NULL:
      strlcpy(out, _awdl->bsdName(), 16)
      IOLockUnlock; return 17
  awdl = OSTypeAlloc(RTLAwdInterface)
  si !awdl: unlock; return kIOReturnNoMemory
  si !awdl->initWithController(this, mac, 6, "awdl"): liberar; unlock; return 0xE00002BC
  si !awdl->startWithRegistration(this): rollback; unlock; return 0xE00002BC
  _awdl = awdl
  rtl_ctrl_setAwdlInterface(this, awdl)           /* IO80211Controller::setAwdlInterface */
  strlcpy(out, awdl->bsdName(), 16)               /* "awdl0" */
  IOLockUnlock
  return 0
```

El nombre se obtiene del ifnet ya creado (el cliente BSD de la familia lo compone dentro de
`registerService(2)`), no de un contador propio: el unit sale de `RegInfo+0x38` y el prefijo de
`RegInfo+0x30` ([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §3.1).

## 5. Pieza C — Nacimiento y clase de `awdl0`

### 5.1 Jerarquía y símbolos

Cadena real del objeto (verificada en [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 y
[`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2):

```text
RTLAwdInterface : IO80211AWDLProtocol : IO80211VirtualInterface : IO80211SkywalkInterface
                : IOSkywalkEthernetInterface : IOSkywalkNetworkInterface : IOSkywalkInterface
```

Símbolos que la etapa 1 necesita (exportados con tipo `0f`, salvo la salvedad indicada al pie):

| Uso | Símbolo (mangled) | Dirección | Fuente |
|---|---|---|---|
| Construir el objeto | `IO80211AWDLProtocol::gMetaClass`, `C2(OSMetaClass*)`, `D2` | `0xffffff800248cde8`, `0xffffff8002198c04`, `0xffffff8002198c24` | [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §2.2; `Tools/re/check_awdl_symbols.sh:18-20` |
| Init con rol y nombre | `IO80211VirtualInterface::init(IO80211Controller*, ether_addr*, unsigned, char const*)` | `0xffffff80023305f8` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) fila 463; §2.3 del nacimiento |
| Publicar el rol | `IO80211SkywalkInterface::setInterfaceRole(unsigned)` | `0xffffff80022f60f0` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §4 |
| Id de interfaz | `IO80211SkywalkInterface::setInterfaceId(unsigned)` | `0xffffff80022f950a` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.3 (`setInterfaceId(2)` en Broadcom) |
| Arranque de la base | `IO80211VirtualInterface::start(IOService*)` | `0xffffff80023308aa` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) fila 186 |
| `RegInfo` | `IOSkywalkEthernetInterface::initRegistrationInfo(RegistrationInfo*, unsigned, unsigned long)` | `0xffffff8002b108e2` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2 |
| Registro Skywalk | `IOSkywalkEthernetInterface::registerEthernetInterface(RegistrationInfo const*, IOSkywalkPacketQueue**, unsigned, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*, unsigned)` | `0xffffff8002b10a84` | [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 paso 4; `Tools/re/check_awdl_symbols.sh:29` |
| Queues y pool | `IOSkywalkPacketBufferPool::withName`, `IOSkywalkTxSubmissionQueue::withPool`, `IOSkywalkTxCompletionQueue::withPool`, `IOSkywalkRxCompletionQueue::withPool` | ver `Source/AirLayer/RTLAirLayer.cpp:140-150` | ya enlazados por el camino STA |
| Publicar el objeto en el controlador | `IO80211Controller::setAwdlInterface(IO80211VirtualInterface*)` / `getAwdlInterface()` | `0xffffff8002299c24` / `0xffffff8002293256` | `Tools/re/check_awdl_symbols.sh:24-25`; [`ARCHITECTURE.md`](ARCHITECTURE.md):1275; [`AWDL_ROLE6_RE.md`](AWDL_ROLE6_RE.md):54 |

`?` `IO80211VirtualInterface::init` no figura en la selección de
[`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2 (la lista es una selección, no el inventario
completo: 179 símbolos de la clase). Su dirección y tipo `0f` sí aparecen en
[`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) fila 463; la comprobación previa P0 de §14 debe
confirmar el tipo antes de compilar.

### 5.2 Declaración de la vtable: por qué hace falta un fichero nuevo

`Source/IO80211/IO80211AWDL_25_6_0.h` es un catálogo de firmas, no una declaración ABI: su cabecera
lo dice (`:1-6`) y omite los slots 2–489 y 491–506. Con esa declaración, cada `virtual` nueva recibe
el siguiente índice C++ disponible, de modo que el slot 490 (`setMacAddress`) quedaría en el offset
`0xE68` en lugar de `0xF40` y los 63 slots AWDL caerían 27 posiciones por debajo: el objeto nacería
con una vtable que no es la de la familia y cualquier llamada del kernel aterrizaría en el método
equivocado.

La declaración nueva se escribe por **offsets** (`offset = 8 × slot` con la convención de
`IO80211_25_6_0.h`), encadenando:

| Bloque | Offsets | Contenido | Fuente |
|---|---|---|---|
| `IO80211SkywalkInterface` | hasta `0xE60` | ya declarado en `Source/IO80211/IO80211_25_6_0.h:386-471`; el último método declarado es `syncDPSStats` | [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) fila 462 |
| `IO80211VirtualInterface` | `0xE68`–`0xF38` (27 métodos) | `init(ctrl, mac, u32, name)`, `setLinkState`, `forwardPacket`, `handleIoctl`, `addPeerToCache`, `deletePeerFromCache`, `handleDebugCmd`, `getPeerSlotDataStats`, `getPeerDataStats`, `clearPeerDataStats`, `setAwdlCurrentChannelSequenceIndex`, `isOutputFlowControlled`, `setOutputFlowControlled`, `clearOutputFlowControlled`, `flushFlowQueues`, `configureAQMOutput`, `attachToBpf`, `configureIfnet`, `reset`, `dupAndTransmitMcastPacket`, `freeMulticastPacket`, `triggerAWDLMulticastTx`, `p2pMulticastTx` (×2), `signalMulticastPackets`, `sendPendingPacketsToStack`, `freePendingPackets` | filas 463–489 |
| `IO80211VirtualInterface` | `0xF40` | `setMacAddress(ether_addr&)`, **puro** | fila 490; [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) |
| `IO80211VirtualInterface` | `0xF48`–`0xFC0` (16) | `_RESERVEDIO80211VirtualInterface0..15` | filas 491–506 |
| `IO80211AWDLProtocol` | `0xFC8`–`0x11B8` (63) | slots 507–569, todos puros; incluye `setCOMPANION_SKYWALK_IF_CREATE` (562) | [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md); filas 507–569 |

**Equivalencia de numeración**: las etiquetas de `Source/IO80211/IO80211_25_6_0.h:458-460`
(`getLastTxTimeStamp` = 458, `getLastRxTimeStamp` = 459, `syncDPSStats` = 460) y los slots 460–462 de
[`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) describen los mismos tres métodos en los offsets
`0xE50`, `0xE58` y `0xE60`; la diferencia es sólo la convención. El último método declarado por la
cabecera queda en `0xE60`, de modo que el primer método nuevo (`init`) va en `0xE68` y los 27
métodos de `IO80211VirtualInterface` ocupan `0xE68`–`0xF38`. `?` La cadena completa no se ha
contrastado método a método con el volcado de la vtable: la comprobación P0.3 de §14 verifica el
recuento y el límite antes de compilar.

**Regla de compilación**: el fichero nuevo declara `IO80211VirtualInterface` e `IO80211AWDLProtocol`
y **no puede incluirse junto a `IO80211AWDL_25_6_0.h`** (definiciones duplicadas de la misma clase).
El catálogo existente no se modifica ni se incluye.

### 5.3 `RegistrationInfo` de `awdl0`

Struct de `0x130` B, versión 1, validado por `initRegistrationInfo`
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2, §3.4). Valores para la etapa 1:

| Offset | Ancho | Valor | Origen |
|---:|---:|---|---|
| `+0x00` | 4 | `version = 1` | `initRegistrationInfo` |
| `+0x0C` | 4 | `3` (`IFNET_SUBFAMILY_WIFI`) | nacimiento §2.2 (`0xffffff800165de3c`) |
| `+0x10` | 4 | `6` (`?` el rol del `RegInfo`; el camino STA usa 1) | `Source/AirLayer/RTLAirLayer.cpp:1506`; `?` |
| `+0x14` | 4 | `2` (número de palabras de medios) | nacimiento §2.2 (`0xffffff800165ddcc`) |
| `+0x24` | 4 | `0x80` (`IFM_IEEE80211`) | nacimiento §2.2; diseño §3.4 |
| `+0x28` | 4 | **`0x80`** (`ifm_current`; Broadcom pone `8`) | desviación deliberada D5; nacimiento §6.3 |
| `+0x30` | 8 | puntero al literal `"awdl"` | nacimiento §3.1 |
| `+0x38` | 4 | `0` (unit) | nacimiento §3.1 |
| `+0x3A` | 2 | `64` (u16, headroom) | nacimiento §2.2 (`0xffffff800165de8c`) |
| `+0x40` | 4 | `0` (el STA usa 0; Broadcom 4 o 6 según una property) | `Source/AirLayer/RTLAirLayer.cpp:1510`; `?` |
| `+0x48` / `+0x4C` | 4 / 4 | `0x594` (MTU 1428) | nacimiento §2.4 paso 4 (`0xffffff80017296ff`) |
| `+0x108` | 6 | MAC del `payload+0x04` del 94 | nacimiento §2.2; [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2 |

Precedente de relleno en el código: `Source/AirLayer/RTLAirLayer.cpp:1502-1511` (mismo struct para
el rol 1). El `RegInfo` de `awdl0` es propio del objeto y sobrevive hasta su `free()`
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.4 nota sobre la retención del puntero).

### 5.4 Colas y pool

Broadcom registra 4 colas TX submission (una por AC, mapa `{0,1,2,3}`), 1 TX completion, 1 RX
completion y una multicast opcional, con un pool compartido ([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
§3.4; [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) §2–§10). La etapa 1 **no
transmite ni recibe**: el datapath no se usa. Para minimizar superficie se registra la misma forma
que ya funciona en el camino STA —1 TX submission, 1 TX completion, 1 RX completion, un pool por
dirección— con los invariantes documentados (lote de `enqueuePackets` menor que la capacidad; los
paquetes de la cola salen de su pool). `?` No está verificado que la familia acepte 3 colas para
una interfaz virtual de rol 6; Broadcom usa 6. Si el registro falla en T3, la alternativa es pasar
el conjunto completo de §3.4 del diseño sin acciones de datapath.

Cada cola se añade a la work queue con `addEventSource` y queda sin acción asociada: no hay
`txAction` ni drenaje de RX en la etapa 1. `enable`/`disable` sólo registran el estado, sin
habilitar ni deshabilitar las colas. El patrón de registro está en
`Source/AirLayer/RTLAirLayer.cpp:374-388` y `:515-535`.

### 5.5 Secuencia de arranque (orden exacto)

```text
initWithController(ctrl, mac, role=6, name="awdl"):
  1. init()                                   /* IOService */
  2. IO80211VirtualInterface::init(ctrl, &ea, 6, "awdl")
        -> publica IO80211VirtualInterfaceRole = "AirLink"
  3. setInterfaceId(2)                        /* Broadcom: 2 si la versión del core >= 5 */
  4. attach(ctrl)                             /* IOService::attach; entry colgada, sin registrar */

startWithRegistration(ctrl):
  5. IO80211VirtualInterface::start(this)     /* base: PMinit/joinPMtree, peer manager, MAC agent */
  6. initRegistrationInfo(reg, 1, 0x130) + relleno de §5.3
  7. crear pool + 3 colas, addEventSource en la work queue
  8. registerEthernetInterface(reg, queues, 3, txPool, rxPool, 0)
        -> publica IOMACAddress, IOInterfaceNamePrefix, IOInterfaceUnit y el estado de medios
  9. registerService(kIOServiceSynchronous)
        -> dentro: el cliente BSD de la familia compone "awdl0", publica "BSD Name" e
           IOInterfaceName, crea el ifnet y hace ifnet_attach
 10. setRunningState(false)
```

El paso 11 (`setAwdlInterface(awdl)` sobre el controlador) lo hace el handler que invoca el arranque,
no el arranque mismo (§4.4): así el puntero sólo se publica cuando el objeto ya está arrancado.

Los pasos 2–4 siguen [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.3; los pasos 5–10,
§2.4 y §5. `registerService(2)` deja el ifnet creado antes de devolver el control al handler
(§5 del nacimiento): cuando `setVIRTUAL_IF_CREATE` escribe el nombre y devuelve 0, `awdl0` ya existe
y la notificación está en camino o entregada.

### 5.6 Lo que la etapa 1 **no** hace

- No hay TX ni RX: las colas quedan sin acción útil y `enable`/`disable` sólo registran el estado.
- No se crea peer manager propio ni se procesan action frames ([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md)
  §4–§5).
- No se emiten eventos 46/47/48/195 ([`AWDL_EVENTS.md`](AWDL_EVENTS.md) §2–§5).
- `setMacAddress(ether_addr&)` (slot 490) acepta y no cambia la MAC: la MAC ya quedó fijada en el
  `RegInfo` y en el ifnet.

## 6. Pieza D — Publicación en IORegistry, orden y `SIOCGIFMEDIA`

### 6.1 Quién publica qué

Todas las properties van sobre la entry del **objeto interfaz**, no sobre el controlador
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §4):

| Property | Valor esperado | La escribe | Momento |
|---|---|---|---|
| `IO80211VirtualInterfaceRole` | `"AirLink"` | `setInterfaceRole(6)` (tabla de roles, índice 5) | paso 2, antes de registrar |
| `IOMACAddress` | 6 B (MAC del 94) | `registerEthernetInterface` → `copyRegistrationInfo` | paso 8 |
| `IOInterfaceNamePrefix` | `"awdl"` | `registerNetworkInterface` desde `RegInfo+0x30` | paso 8 |
| `IOInterfaceUnit` | `0` (OSNumber 32 b) | ídem, desde `RegInfo+0x38` | paso 8 |
| `"BSD Name"` | `"awdl0"` | cliente BSD de la familia | dentro del paso 9 |
| `IOInterfaceName` | `"awdl0"` | cliente BSD; la republica `prepareBSDInterface` | dentro del paso 9, antes de `ifnet_attach` |
| `IOInterfaceType` | `6` (`IFT_ETHER`) | cliente BSD | final del paso 9 |
| `IOSkywalkNexusUUID` | UUID del nexus | cliente BSD | final del paso 9 |

**Regla de orden**: rol, MAC, prefijo, unit, ifnet adjunto y `IOInterfaceName` deben existir **antes**
de que la notificación pueda dispararse; en esta implementación eso se cumple porque todo ocurre
dentro de `registerService(2)` síncrono del paso 9, salvo el rol (paso 2). Un `registerService`
asíncrono dejaría el nombre sin escribir cuando el handler devuelve 0 y el daemon podría leer un
`payload+0x10` vacío (`?` cuánto tarda el matching en ese caso).

### 6.2 Verificación en el IORegistry

```text
ioreg -r -c RTLAwdInterface -l
  +-o RTLAwdInterface  <class RTLAwdInterface, ...>
      "IO80211VirtualInterfaceRole" = "AirLink"
      "IOMACAddress" = <...>
      "IOInterfaceNamePrefix" = "awdl"
      "IOInterfaceUnit" = 0
      "IOInterfaceName" = "awdl0"
      "BSD Name" = "awdl0"
      "IOInterfaceType" = 6
```

`?` El nombre exacto de la clase en el IORegistry es el de la metaclase propia
(`RTLAwdInterface`); no se ha verificado que la familia no imponga un `classNameOverride`
([`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) fila 319: `classNameOverride`, slot 319).

### 6.3 `SIOCGIFMEDIA`

El descubridor exige `(ifm_current & 0xE0) == 0x80` (`SIOCGIFMEDIA = 0xC02C6938`; ioctl en
`w2p 0x100043cbf`, filtro en `w2p 0x100043ccc`). `ioctl_gifmedia` de la familia copia
`ifm_active ← estado+48`, `ifm_current ← estado+52`, `ifm_status ← estado+56`, y esos campos se
siembran desde `RegInfo+0x24` y `RegInfo+0x28`
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §3.2 paso 5, §6.2). Con `+0x24 = +0x28 = 0x80`
la respuesta pasa el filtro sin escribir ninguna `if_ioctl` propia. La variante legacy
`OSIOCGIFMEDIA = 0xC0286938` sigue el mismo camino.

`?` No está verificado que ningún componente reescriba `estado+52` entre el registro y la lectura
del daemon; si lo hiciera con un valor sin tipo 802.11, el filtro fallaría y `awdl0` se ignoraría con
`Ignoring %s because its not a WiFi interface` (`0x100494b00`). La prueba T5 de §14 lo detecta.

## 7. Pieza E — `VIRTUAL_IF_PARENT` (tipo 97)

### 7.1 Contrato

`RawGet` 97 sobre `awdl0`: el handler `getVIRTUAL_IF_PARENT` (`0xffffff800227b7ff`,
`gGetHandlerTable[96]`) obtiene el padre de la interfaz virtual (`safeMetaCast` a
`IO80211VirtualInterface`), toma su nombre BSD (`IO80211SkywalkInterface::getBSDName`), exige
`req_len >= strlen(nombre)` y copia `strlen` bytes **sin NUL** en `req_data`
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.3). El daemon envía `len = strlen("awdl0")` y parsea
buscando `'\0'` con ese tope (`sub_100046D30`, `w2p 0x100046db9`).

### 7.2 Implementación

**No se añade código**: el handler es de la familia y funciona con cualquier
`IO80211VirtualInterface` adjunta. Los requisitos caen sobre la pieza C:

1. El objeto debe ser realmente `IO80211VirtualInterface` (por eso D2 y D3); un objeto
   `IO80211InfraProtocol` a secas no pasa el `safeMetaCast` y el handler devuelve `0xE082280E`.
2. El objeto debe estar publicado en el controlador con `setAwdlInterface()`
   ([`ARCHITECTURE.md`](ARCHITECTURE.md):1275; [`AWDL_ROLE6_RE.md`](AWDL_ROLE6_RE.md):54), que es
   el puntero con el que la familia asocia la interfaz AWDL a su controlador. `?` No está verificado
   si el handler 97 busca la interfaz por la lista de adjuntas o por `getAwdlInterface()`; la
   llamada cubre el segundo caso y es inocua para el primero.
3. El nombre del padre es el del controlador / interfaz de infraestructura (`en2` en este sistema,
   `en0` en la referencia del documento).

Respuesta esperada en el buffer de 16 B: `en2` (3 bytes) o el nombre que tenga la interfaz de
infraestructura, sin NUL y con el resto intacto.

### 7.3 Riesgo y prueba

`?` No está verificado que el daemon señalice el semáforo si el `RawGet` 97 falla; la secuencia
estática lo sitúa antes de la señal (`0x100044728` → `0x1000451f2`). Por eso la etapa 1 lo trata
como requisito. La prueba T5 verifica indirectamente: ausencia de timeout y presencia de
`Found new interface awdl0 with role AirLink`.

## 8. Pieza F — Companion `llw0` (tipo 293), opcional

### 8.1 Contrato, si se implementa

`RawSet` 293 (`gSetHandlerTable[292]` → `0xffffff800228b4b8`), 24 B, emitido una sola vez por
`sub_10004B300` (`w2p 0x10004b3e6`) contra el handle del padre ya aceptado
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.4; [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4):

| Offset | Campo | Semántica |
|---:|---|---|
| `+0x00` | `code` u32 | propagado del llamador; el kernel no lo lee; valor `?` |
| `+0x04` | `op` u32 | `1` = crear; `0` = destruir; otro = no-op que devuelve 0 |
| `+0x08` | `name[16]` | salida: nombre BSD de la companion (`llw0`), escrito sólo en éxito |

Precedente kernel: crear una interfaz Skywalk con prefijo `llw`, rol 2 (que publica
`IO80211InterfaceRole = "LowLatency"`), id 9, y escribir el nombre en `payload+8`
([`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §3.2–§3.3). El daemon valida con
`if_nametoindex("llw0")` (`w2p 0x10004b814`).

### 8.2 Decisión para la etapa 1 (D6)

No se implementa: el dispatch devuelve **102** al no haber fila útil para la interfaz y el daemon lo
absorbe. Consecuencias verificadas en [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4:

- dos líneas de error en el log del daemon (`Failed to create low latency interface for %s ...`,
  `0x100494cf0`, y el epílogo `0x100048fb5`) y el init de `AWDLInterface` continúa;
- **no** hay teardown de `awdl0`, ni `RawSet` 95, ni reintento;
- no aparece la espera de 30 s específica de `llw`, que sólo corre si el kext contesta éxito sin
  nombre verificable (`w2p 0x100048b13`, timeout `0x100048beb`);
- contestar éxito sin crear nada es peor: cuesta 30 s por intento.

La alternativa (implementar `llw0`) pertenece a la etapa 4 del diseño (§6).

## 9. Pieza G — Registro del monitoreo de eventos

`wifip2pd` registra el monitoreo por dos vías y **aborta al primer error**
([`AWDL_EVENTS.md`](AWDL_EVENTS.md) §4.1):

- rol 1 (`awdl0`): `Apple80211RawP2PEventMonitoringInit` (`sub_100041F20`);
- rol 0: `Apple80211RawEventMonitoringInit` + 16 × `Apple80211StartMonitoringEvent` (ids 1, 2, 3,
  9, 11, 55, 59, 66, 74, 78, 82, 125, 126, 127, 160, 215).

Ningún evento es condición de aceptación; lo bloqueante es que el registro devuelva 0: un error
encadena la recuperación `sub_100046E30`, que **re-crea la interfaz**
([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1.7). La vía es el user client / event pipe
de la familia (`createEventPipe`, slot 440; `postMessageSync` en `0xffffff80022f8abc`).

**Cambio de código: ninguno previsto.** `?` No está verificado que el event pipe de la familia
acepte una interfaz de un driver externo sin más; si el registro falla en T7, la recuperación se
manifiesta como re-creación de `awdl0` y hay que revisar el user client, no el handler 94.

## 10. Orden, bloqueo y ciclo de vida

### 10.1 Contexto de ejecución

| Camino | Contexto | Regla |
|---|---|---|
| `setVIRTUAL_IF_CREATE` | contexto del user client de apple80211 (hilo del llamador), fuera de cualquier gate propio | crea y arranca en línea; no usa thread calls ni esperas |
| `registerService(2)` | dentro del handler; ejecuta el cliente BSD de forma síncrona | no debe llamarse con el gate de la work queue cerrado por el mismo hilo (`?` el despacho concreto del handler) |
| `enable`/`disable` | work queue (`runAction`) | no tocan el estado AWDL |
| `stop`/`free` del controlador | hilo de terminación de IOKit | desmontan `awdl0` antes de liberar el controlador |

El código ya documenta el riesgo inverso: `postMessage` debe ejecutarse dentro del gate
(`Source/AirLayer/RTLAirLayer.cpp:1288-1305`) y el hook de RX no puede tomarlo bajo el lock de la
radio (`:1213-1222`). La creación de `awdl0` no participa de ninguno de los dos: no emite eventos y
no toca la radio.

### 10.2 Cerraduras

- `_awdlLock` (nuevo `IOLock`, asignado en `RTLAirController::start` junto a `_lock`/`_rxLock`,
  `Source/AirLayer/RTLAirLayer.cpp:1443-1449`): serializa creación, consulta del «ya existe» y
  destrucción. Se toma **antes** de mirar `_awdl` y se suelta después de escribir el nombre y
  `setAwdlInterface()`.
- `_lock` existente: no se usa para la vía AWDL; ya protege la fase de scan/join.
- `_rxLock`: no se toca.

### 10.3 Ciclo de vida

| Evento | Acción |
|---|---|
| `rtlawdl=0` o gate cerrado | el handler devuelve 102; no se crea nada |
| Primer 94 con rol 6 | crear, arrancar, publicar, guardar puntero |
| Segundo 94 con rol 6 | devolver 17 con el nombre |
| `stop(IOService*)` del controlador | `setAwdlInterface(NULL)`; `_awdl->terminate()`; `OSSafeReleaseNULL(_awdl)`; liberar `_awdlLock` en `free()` |
| Fallo a mitad de creación | rollback de §11 antes de devolver el error |
| Descarga del kext | el ifnet desaparece con el servicio; el daemon recibe `IOServiceTerminate` y limpia el handle sin señalizar (`sub_100045520`, [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.1) |

## 11. Manejo de fallos y rollback

| Punto de fallo | Acción | Retorno al daemon | Log esperado |
|---|---|---|---|
| `len != 32` o puntero nulo | nada | `0xE00002C2` | `awdl: create len %u` |
| Rol no soportado | nada | `0xE0000001` (roles ≠ 6,7) o `0xE00002C7` (rol 7) | `awdl: invalid role %u` |
| `OSTypeAlloc` falla | nada | `kIOReturnNoMemory` | `awdl: alloc failed` |
| `initWithController` falla (init, rol, attach) | `terminate()` + release | `0xE00002BC` | `awdl: init failed` |
| `initRegistrationInfo` falla | parar colas, detach, release | `0xE00002BC` | `awdl: reginfo failed` |
| Pool o colas fallan | ídem | `kIOReturnNoResources` | `awdl: queues failed` |
| `registerEthernetInterface` falla | quitar colas de la work queue, `deregisterEthernetInterface` (si llegó a registrarse), detach, release | el `IOReturn` recibido | `awdl: register 0x%x` |
| `registerService(2)` no crea el ifnet | rollback completo; **no** escribir el nombre | `0xE00002BC` | `awdl: no bsd name` |
| Nombre vacío tras el arranque | rollback completo | `0xE00002BC` | `awdl: empty name` |
| Fallo en `stop` del controlador | continuar el desmontaje aunque falle el paso; nunca dejar `_awdl` apuntando a memoria liberada | — | `awdl: teardown` |

Regla común: el nombre se escribe en `payload+0x10` **sólo** cuando el ifnet y las properties están
publicados; cualquier camino anterior deja `ifname` a cero (el buffer llega a cero desde el daemon,
[`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2).

## 12. Esqueleto C++

Ficheros nuevos propuestos: `Source/AirLayer/AWDLInterface.h` y `Source/AirLayer/AWDLInterface.cpp`.
El parche de enganche en ficheros existentes va en §12.3 y **no se aplica** en esta etapa.

### 12.1 `Source/AirLayer/AWDLInterface.h`

```cpp
/*
 * RTL8814AirLayer — etapa 1 de AWDL: declaración de la cadena de vtables de
 * IO80211VirtualInterface / IO80211AWDLProtocol y de la clase RTLAwdInterface.
 *
 * La cadena se declara completa y anclada a offsets (offset = 8 * slot, la
 * convención de Source/IO80211/IO80211_25_6_0.h). No debe incluirse junto a
 * Source/IO80211/IO80211AWDL_25_6_0.h: ese catálogo omite slots y sus índices
 * C++ no coinciden con la vtable real.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL_AWDL_INTERFACE_H
#define RTL_AWDL_INTERFACE_H

#include "RTLAirLayer.hpp"

/* ---- Estructuras AWDL usadas sólo como puntero en la vtable ---- */
struct apple80211_ssid_data;
struct apple80211_channel_data;
struct apple80211_bssid_data;
struct apple80211_state_data;
struct apple80211_awdl_sync_params;
struct apple80211_awdl_sync_enabled;
struct apple80211_awdl_extension_state_machine_parameter;
struct apple80211_awdl_election_id;
struct apple80211_awdl_election_metric;
struct apple80211_awdl_sync_channel_sequence;
struct apple80211_peer_cache_maximum_size;
struct apple80211_awdl_master_channel;
struct apple80211_awdl_sync_state;
struct apple80211_awdl_election_rssi_thresholds;
struct apple80211_awdl_presence_mode;
struct apple80211_awdl_election_params;
struct apple80211_awdl_device_capabilities;
struct apple80211_awdl_secondary_master_channel;
struct apple80211_awdl_af_tx_mode;
struct apple80211_awdl_min_rate;
struct apple80211_awdl_txcal_period;
struct apple80211_awdl_dev_stats;
struct apple80211_awdl_dfsp_config;
struct apple80211_awdl_cca;
struct apple80211_awdl_cap;
struct apple80211_llw_params;
struct apple80211_awdl_bssid;
struct apple80211_peer_cache_control;
struct apple80211_awdl_oob_request;
struct apple80211_awdl_sync_frame_template;
struct apple80211_awdl_peer_traffic_registration;
struct awdl_peer_advert_add;
struct apple80211_awdl_dfsp_ucsa_config;
struct apple80211_companion_skywalk_if_create;
struct apple80211_awdl_af_sec_payload_request;
struct apple80211_p2p_coex_protection;
struct apple80211_awdl_psf_dwell_params;
struct apple80211_awdl_config_request;
struct apple80211_rtg_peer_stats_cfg;

/* ---- Payloads del ciclo de vida (Docs/AWDL_STRUCTS.md §7) ---- */
struct apple80211_virt_if_create_data {
    uint32_t f0;             /* +0x00: enviado por wifip2pd, lectura ?        */
    uint8_t  mac[6];         /* +0x04: MAC solicitada                         */
    uint8_t  pad0[2];        /* +0x0A                                         */
    uint32_t role;           /* +0x0C: 6 = AWDL                                */
    char     ifname[16];     /* +0x10: salida, nombre BSD creado              */
};
static_assert(sizeof(apple80211_virt_if_create_data) == 32, "virt_if_create_data");

/* Opciones de pool: misma forma que Source/AirLayer/RTLAirLayer.cpp:196-204. */
struct rtl_pool_options {
    uint32_t packetCount;
    uint32_t bufferCount;
    uint32_t bufferSize;
    uint32_t maxBuffersPerPacket;
    uint32_t reserved0;
    uint32_t poolFlags;
    void    *dmaSpec;
    uint64_t reserved1;
};

/* ---- Símbolos de IO80211Family / IOSkywalkFamily usados por la etapa 1 ---- */
extern "C" {
bool rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
    __asm("__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm");
void rtl_sky_setId(void *iface, uint32_t id)
    __asm("__ZN23IO80211SkywalkInterface14setInterfaceIdEj");
void *rtl_sky_getWorkQueue(void *iface)
    __asm("__ZN23IO80211SkywalkInterface12getWorkQueueEv");
void *rtl_pool_withName(const char *name, OSObject *owner, uint32_t type, const void *opts)
    __asm("__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE");
void *rtl_txsub_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                         void *action, void *refCon, uint32_t opts)
    __asm("__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j");
void *rtl_txcomp_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                          void *action, void *refCon, uint32_t opts)
    __asm("__ZN26IOSkywalkTxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
void *rtl_rxcomp_withPool(void *pool, uint32_t cap, uint32_t flags, OSObject *owner,
                          void *action, void *refCon, uint32_t opts)
    __asm("__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
bool rtl_vif_init(void *self, IO80211Controller *ctrl, ether_addr *mac, uint32_t role,
                  const char *name)
    __asm("__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc");
uintptr_t rtl_vif_start(void *self, IOService *provider)
    __asm("__ZN23IO80211VirtualInterface5startEP9IOService");
IOReturn rtl_ctrl_setAwdlInterface(IO80211Controller *ctrl, void *awdl)
    __asm("__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface");
void *rtl_ctrl_getAwdlInterface(IO80211Controller *ctrl)
    __asm("__ZN17IO80211Controller16getAwdlInterfaceEv");
IOReturn rtl_eth_register(void *self, void *regInfo, void **queues, uint32_t n,
                          void *txPool, void *rxPool, uint32_t flags)
    __asm("__ZN26IOSkywalkEthernetInterface25registerEthernetInterfaceEPKNS_16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_j");
}

/* ---- Declaración de la superficie que falta en IO80211_25_6_0.h ---- */
/* El último método declarado por IO80211SkywalkInterface es syncDPSStats
 * (offset 0xE60). A partir de ahí, esta cadena reproduce la vtable real. */
class IO80211VirtualInterface : public IO80211SkywalkInterface {
    OSDeclareAbstractStructors(IO80211VirtualInterface)
public:
    /* Overrides de slots ya existentes: no desplazan la vtable. */
    virtual void free() override;
    virtual void stop(IOService *) override;
    using IO80211SkywalkInterface::init;   /* evita ocultar init() de IOService */
    virtual bool init(IO80211Controller *, ether_addr *, uint32_t, const char *);   /* 0xE68 */
    virtual uintptr_t setLinkState(uint32_t, uint32_t);                            /* 0xE70 */
    virtual uintptr_t forwardPacket(void *);                                       /* 0xE78 */
    virtual uintptr_t handleIoctl(void *, unsigned long, void *);                   /* 0xE80 */
    virtual uintptr_t addPeerToCache(unsigned char *);                             /* 0xE88 */
    virtual uintptr_t deletePeerFromCache(unsigned char *);                        /* 0xE90 */
    virtual uintptr_t handleDebugCmd(void *);                                      /* 0xE98 */
    virtual uintptr_t getPeerSlotDataStats(ether_addr *, bool, void *);            /* 0xEA0 */
    virtual uintptr_t getPeerDataStats(ether_addr *, bool, void *);                /* 0xEA8 */
    virtual uintptr_t clearPeerDataStats(ether_addr *, bool);                      /* 0xEB0 */
    virtual uintptr_t setAwdlCurrentChannelSequenceIndex(uint32_t);                /* 0xEB8 */
    virtual uintptr_t isOutputFlowControlled();                                    /* 0xEC0 */
    virtual uintptr_t setOutputFlowControlled();                                   /* 0xEC8 */
    virtual uintptr_t clearOutputFlowControlled();                                 /* 0xED0 */
    virtual uintptr_t flushFlowQueues(ether_addr *);                               /* 0xED8 */
    virtual uintptr_t configureAQMOutput();                                        /* 0xEE0 */
    virtual uintptr_t attachToBpf();                                               /* 0xEE8 */
    virtual uintptr_t configureIfnet();                                            /* 0xEF0 */
    virtual uintptr_t reset();                                                     /* 0xEF8 */
    virtual uintptr_t dupAndTransmitMcastPacket(void *, unsigned char *,
                                                unsigned char *);                  /* 0xF00 */
    virtual uintptr_t freeMulticastPacket(void *);                                 /* 0xF08 */
    virtual uintptr_t triggerAWDLMulticastTx(uint32_t, unsigned char);             /* 0xF10 */
    virtual uintptr_t p2pMulticastTx(void **, uint32_t);                           /* 0xF18 */
    virtual uintptr_t p2pMulticastTx(uint32_t, bool);                              /* 0xF20 */
    virtual uintptr_t signalMulticastPackets(uint32_t, uint32_t);                  /* 0xF28 */
    virtual uintptr_t sendPendingPacketsToStack(void **, uint32_t);                /* 0xF30 */
    virtual uintptr_t freePendingPackets(void **, uint32_t);                       /* 0xF38 */
    virtual uintptr_t setMacAddress(ether_addr &) = 0;                             /* 0xF40: PURO */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface0();                         /* 0xF48 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface1();                         /* 0xF50 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface2();                         /* 0xF58 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface3();                         /* 0xF60 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface4();                         /* 0xF68 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface5();                         /* 0xF70 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface6();                         /* 0xF78 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface7();                         /* 0xF80 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface8();                         /* 0xF88 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface9();                         /* 0xF90 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface10();                        /* 0xF98 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface11();                        /* 0xFA0 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface12();                        /* 0xFA8 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface13();                        /* 0xFB0 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface14();                        /* 0xFB8 */
    virtual uintptr_t _RESERVEDIO80211VirtualInterface15();                        /* 0xFC0 */
private:
    uint8_t _rtlAwdPad[0x8];   /* metaclass 0x130: 0x128 de Skywalk + 0x8 */
};

class IO80211AWDLProtocol : public IO80211VirtualInterface {
    OSDeclareAbstractStructors(IO80211AWDLProtocol)
public:
    virtual IOReturn getSSID(apple80211_ssid_data *) = 0;                          /* 0xFC8 */
    virtual IOReturn getCHANNEL(apple80211_channel_data *) = 0;
    virtual IOReturn getBSSID(apple80211_bssid_data *) = 0;
    virtual IOReturn getSTATE(apple80211_state_data *) = 0;
    virtual IOReturn getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) = 0;
    virtual IOReturn getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) = 0;
    virtual IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        apple80211_awdl_extension_state_machine_parameter *) = 0;
    virtual IOReturn getAWDL_ELECTION_ID(apple80211_awdl_election_id *) = 0;
    virtual IOReturn getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) = 0;
    virtual IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        apple80211_awdl_sync_channel_sequence *) = 0;
    virtual IOReturn getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size *) = 0;
    virtual IOReturn getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) = 0;
    virtual IOReturn getAWDL_SYNC_STATE(apple80211_awdl_sync_state *) = 0;
    virtual IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(
        apple80211_awdl_election_rssi_thresholds *) = 0;
    virtual IOReturn getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) = 0;
    virtual IOReturn getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) = 0;
    virtual IOReturn getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities *) = 0;
    virtual IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(
        apple80211_awdl_secondary_master_channel *) = 0;
    virtual IOReturn getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) = 0;
    virtual IOReturn getAWDL_MIN_RATE(apple80211_awdl_min_rate *) = 0;
    virtual IOReturn getAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) = 0;
    virtual IOReturn getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) = 0;
    virtual IOReturn getAWDL_DEV_STATS(apple80211_awdl_dev_stats *) = 0;
    virtual IOReturn getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) = 0;
    virtual IOReturn getAWDL_CCA(apple80211_awdl_cca *) = 0;
    virtual IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap *) = 0;
    virtual IOReturn getLLW_PARAMS(apple80211_llw_params *) = 0;
    virtual IOReturn setSSID(apple80211_ssid_data *) = 0;
    virtual IOReturn setCHANNEL(apple80211_channel_data *) = 0;
    virtual IOReturn setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *) = 0;
    virtual IOReturn setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *) = 0;
    virtual IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        apple80211_awdl_extension_state_machine_parameter *) = 0;
    virtual IOReturn setAWDL_ELECTION_ID(apple80211_awdl_election_id *) = 0;
    virtual IOReturn setAWDL_BSSID(apple80211_awdl_bssid *) = 0;
    virtual IOReturn setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *) = 0;
    virtual IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        apple80211_awdl_sync_channel_sequence *) = 0;
    virtual IOReturn setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *) = 0;
    virtual IOReturn setAWDL_SYNC_STATE(apple80211_awdl_sync_state *) = 0;
    virtual IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(
        apple80211_awdl_election_rssi_thresholds *) = 0;
    virtual IOReturn setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *) = 0;
    virtual IOReturn setPEER_CACHE_CONTROL(apple80211_peer_cache_control *) = 0;
    virtual IOReturn setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *) = 0;
    virtual IOReturn setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *) = 0;
    virtual IOReturn setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) = 0;
    virtual IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(
        apple80211_awdl_peer_traffic_registration *) = 0;
    virtual IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(
        apple80211_awdl_secondary_master_channel *) = 0;
    virtual IOReturn setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) = 0;
    virtual IOReturn setAWDL_MIN_RATE(apple80211_awdl_min_rate *) = 0;
    virtual IOReturn setAWDL_ENCRYPTION_KEYS(apple80211_key *) = 0;
    virtual IOReturn setAWDL_ENCRYPTION_TYPE(apple80211_encrypt *) = 0;
    virtual IOReturn setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *) = 0;
    virtual IOReturn setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period *) = 0;
    virtual IOReturn setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add *) = 0;
    virtual IOReturn setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config *) = 0;
    virtual IOReturn setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config *) = 0;
    virtual IOReturn setCOMPANION_SKYWALK_IF_CREATE(
        apple80211_companion_skywalk_if_create *) = 0;                             /* 0x1180 */
    virtual IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(
        apple80211_awdl_af_sec_payload_request *) = 0;
    virtual IOReturn setLLW_PARAMS(apple80211_llw_params *) = 0;
    virtual IOReturn setTX_RATE(apple80211_tx_rate *) = 0;
    virtual IOReturn setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection *) = 0;
    virtual IOReturn setPSF_DWELL(apple80211_awdl_psf_dwell_params *) = 0;
    virtual IOReturn setAWDL_CONFIG(apple80211_awdl_config_request *) = 0;
    virtual IOReturn setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg *) = 0;
};
static_assert(sizeof(IO80211AWDLProtocol) == 0x130, "AWDLProtocol KC metaclass size");

/* ---- Clase concreta de la etapa 1 ---- */
class RTLAwdInterface : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(RTLAwdInterface)
public:
    bool initWithController(RTLAirController *ctrl, const uint8_t mac[6]);
    bool startWithRegistration(RTLAirController *ctrl);
    void stop(IOService *provider) override;
    void free() override;
    const char *bsdName() const { return _bsdName; }

    uintptr_t setMacAddress(ether_addr &) override { return 0; }
    uintptr_t enable(unsigned int opts) override;
    uintptr_t disable(unsigned int opts) override;

    /* Superficie AWDL: stubs de la etapa 1 (102 / kIOReturnUnsupported). */
#define AWDL_STUB(name, type) IOReturn name(type *) override { return kIOReturnUnsupported; }
    AWDL_STUB(getSSID, apple80211_ssid_data)
    AWDL_STUB(getCHANNEL, apple80211_channel_data)
    /* ... una línea por cada uno de los 63 slots 507-569; los nombres y los
     * offsets están en el anexo §16 y en AWDL_PURE_SLOTS.md ... */
#undef AWDL_STUB
    IOReturn setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create *) override
    { return kIOReturnUnsupported; }   /* D6: la companion es etapa 4 */

private:
    RTLAirController *_ctrl;
    void *_txPool, *_rxPool;
    void *_txSubQ, *_txCompQ, *_rxCompQ;
    uint8_t *_regInfo;
    uint8_t _mac[6];
    char _bsdName[16];

    bool createDatapath();
    static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);
};
#endif /* RTL_AWDL_INTERFACE_H */
```

> `?` Las firmas de los 27 métodos de `0xE68`–`0xF38` se declaran con los tipos documentados en
> [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md); el tipo de retorno no está codificado en el
> nombre mangleado y se usa `uintptr_t` salvo en `init` (booleano). Sólo importan para la
> alineación de la vtable: no se llaman desde el kext en la etapa 1.

### 12.2 `Source/AirLayer/AWDLInterface.cpp`

```cpp
/*
 * RTL8814AirLayer — etapa 1 de AWDL: creación de awdl0 (rol 6).
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "AWDLInterface.h"
#include <IOKit/IOLib.h>

/* Mientras airLog() siga siendo static en RTLAirLayer.cpp (§12.2, notas). */
#define LOG(fmt, ...) IOLog("RTL8814AirLayer: " fmt "\n", ##__VA_ARGS__)

OSDefineMetaClassAndStructors(RTLAwdInterface, IO80211AWDLProtocol)

/* Mismos offsets que el camino STA (Source/AirLayer/RTLAirLayer.cpp:207-214). */
#define AWDL_REGINFO_SIZE       0x130
#define AWDL_REGINFO_SUBFAMILY  0x0c
#define AWDL_REGINFO_ROLE       0x10
#define AWDL_REGINFO_NMEDIA     0x14
#define AWDL_REGINFO_MEDIA      0x24    /* u32 ifm_active  */
#define AWDL_REGINFO_MEDIA_CUR  0x28    /* u32 ifm_current */
#define AWDL_REGINFO_PREFIX     0x30
#define AWDL_REGINFO_UNIT       0x38
#define AWDL_REGINFO_HEADROOM   0x3a
#define AWDL_REGINFO_FLAGS      0x40
#define AWDL_REGINFO_MTU        0x48
#define AWDL_REGINFO_MAC        0x108

bool RTLAwdInterface::initWithController(RTLAirController *ctrl, const uint8_t mac[6])
{
    _ctrl = ctrl;
    memcpy(_mac, mac, 6);
    if (!init()) {
        LOG("awdl: init() failed");
        return false;
    }
    ether_addr ea;
    memcpy(ea.octet, mac, 6);
    if (!rtl_vif_init(this, ctrl, &ea, 6, "awdl")) {   /* publica el rol AirLink */
        LOG("awdl: IO80211VirtualInterface::init failed");
        return false;
    }
    rtl_sky_setId(this, 2);                            /* Broadcom: id 2 para AWDL */
    if (!attach(ctrl)) {
        LOG("awdl: attach failed");
        return false;
    }
    return true;
}

bool RTLAwdInterface::createDatapath()
{
    rtl_pool_options o;
    bzero(&o, sizeof(o));
    o.packetCount = 256;
    o.bufferCount = 256;
    o.bufferSize = 2048;
    o.maxBuffersPerPacket = 1;
    o.poolFlags = 1;
    _txPool = rtl_pool_withName("rtlawdl:tx", this, 1, &o);
    _rxPool = rtl_pool_withName("rtlawdl:rx", this, 1, &o);
    if (!_txPool || !_rxPool)
        return false;
    _txSubQ = rtl_txsub_withPool(_txPool, 128, 0, this, nullptr, nullptr, 0);
    _txCompQ = rtl_txcomp_withPool(_txPool, 128, 0, this, nullptr, nullptr, 0);
    _rxCompQ = rtl_rxcomp_withPool(_rxPool, 128, 0, this, nullptr, nullptr, 0);
    if (!_txSubQ || !_txCompQ || !_rxCompQ)
        return false;
    IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
    if (!wq)
        return false;
    wq->addEventSource((IOEventSource *)_txSubQ);
    wq->addEventSource((IOEventSource *)_txCompQ);
    wq->addEventSource((IOEventSource *)_rxCompQ);
    return true;
}

bool RTLAwdInterface::startWithRegistration(RTLAirController *ctrl)
{
    if (!rtl_vif_start(this, ctrl)) {
        LOG("awdl: base start failed");
        return false;
    }
    _regInfo = (uint8_t *)IOMalloc(AWDL_REGINFO_SIZE);
    if (!_regInfo)
        return false;
    bzero(_regInfo, AWDL_REGINFO_SIZE);
    rtl_sky_initRegInfo(this, _regInfo, 1, AWDL_REGINFO_SIZE);
    *(uint32_t *)(_regInfo + AWDL_REGINFO_SUBFAMILY) = 3;
    *(uint32_t *)(_regInfo + AWDL_REGINFO_ROLE) = 6;          /* ? valor exacto */
    *(uint32_t *)(_regInfo + AWDL_REGINFO_NMEDIA) = 2;
    *(uint32_t *)(_regInfo + AWDL_REGINFO_MEDIA) = 0x80;      /* IFM_IEEE80211 */
    *(uint32_t *)(_regInfo + AWDL_REGINFO_MEDIA_CUR) = 0x80;  /* D5: pasa el filtro */
    *(const char **)(_regInfo + AWDL_REGINFO_PREFIX) = "awdl";
    *(uint32_t *)(_regInfo + AWDL_REGINFO_UNIT) = 0;
    *(uint16_t *)(_regInfo + AWDL_REGINFO_HEADROOM) = 64;
    *(uint32_t *)(_regInfo + AWDL_REGINFO_FLAGS) = 0;
    *(uint64_t *)(_regInfo + AWDL_REGINFO_MTU) = 0x59400000594ULL;  /* +0x48 y +0x4C */
    memcpy(_regInfo + AWDL_REGINFO_MAC, _mac, 6);

    if (!createDatapath())
        return false;
    void *queues[3] = { _txSubQ, _txCompQ, _rxCompQ };
    IOReturn r = rtl_eth_register(this, _regInfo, queues, 3, _txPool, _rxPool, 0);
    LOG("awdl: registerEthernetInterface = 0x%x", r);
    if (r != kIOReturnSuccess)
        return false;

    /* Síncrono: el cliente BSD compone "awdl0", publica IOInterfaceName y
     * crea el ifnet antes de volver (Docs/AWDL_INTERFACE_BIRTH.md §5). */
    registerService(kIOServiceSynchronous);
    setRunningState(false);
    /* Mismo prefijo y unit que el RegInfo: "awdl" + 0. */
    snprintf(_bsdName, sizeof(_bsdName), "awdl%d", 0);
    LOG("awdl: born %s", _bsdName);
    return true;
}

IOReturn RTLAwdInterface::removeQueues(OSObject *owner, void *, void *, void *, void *)
{
    RTLAwdInterface *me = (RTLAwdInterface *)owner;
    IOEventSource *qs[3] = { (IOEventSource *)me->_txSubQ, (IOEventSource *)me->_txCompQ,
                             (IOEventSource *)me->_rxCompQ };
    for (int i = 0; i < 3; i++)
        if (qs[i])
            qs[i]->disable();
    return kIOReturnSuccess;
}

void RTLAwdInterface::stop(IOService *provider)
{
    IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
    if (wq)
        wq->runAction(&RTLAwdInterface::removeQueues, this);
    IO80211VirtualInterface::stop(provider);
}

void RTLAwdInterface::free()
{
    OSSafeReleaseNULL(*(OSObject **)&_txSubQ);
    OSSafeReleaseNULL(*(OSObject **)&_txCompQ);
    OSSafeReleaseNULL(*(OSObject **)&_rxCompQ);
    OSSafeReleaseNULL(*(OSObject **)&_txPool);
    OSSafeReleaseNULL(*(OSObject **)&_rxPool);
    if (_regInfo) {
        IOFree(_regInfo, AWDL_REGINFO_SIZE);
        _regInfo = nullptr;
    }
    IO80211VirtualInterface::free();
}

uintptr_t RTLAwdInterface::enable(unsigned int)
{
    LOG("awdl: enable");
    return kIOReturnSuccess;
}

uintptr_t RTLAwdInterface::disable(unsigned int)
{
    LOG("awdl: disable");
    return kIOReturnSuccess;
}
```

Notas de implementación:

- La MAC se guarda en el ivar `_mac` del objeto al construirlo (`memcpy(_mac, mac, 6)` en
  `initWithController`) y se copia al `RegInfo` en el paso 6. `?` No se ha verificado un getter
  público de la MAC en la base; el camino STA la conserva en `RTLAirController::_mac`
  (`Source/AirLayer/RTLAirLayer.hpp:187`).
- El nombre `awdl0` se compone con prefijo y unit fijos (los mismos que el `RegInfo`) porque
  `getBSDName()` de la familia lee el ifnet y puede no estar disponible hasta que el cliente BSD
  termina; el handler sólo escribe el nombre y no lo usa para nada más. `?` Si la familia rechaza
  el registro con 3 colas, se pasa al conjunto de 4+2 de §5.4.
- `registerService(kIOServiceSynchronous)` sustituye al `kIOServiceAsynchronous` del camino STA
  (`Source/AirLayer/RTLAirLayer.cpp:345`): es la diferencia que garantiza que el ifnet exista antes
  de devolver el nombre (D4).
- `LOG` es el `airLog` de `RTLAirLayer.cpp`, declarado `static` en esa unidad (`:26-58`). Para que el
  fichero nuevo escriba en el anillo `RTLAirLayerLog` hace falta exponer un `rtlAirLog()` no estático
  desde `RTLAirLayer.cpp` (una línea; §12.3). Mientras no se haga, el ejemplo usa `IOLog` directamente
  y las líneas `awdl:` no aparecen en el anillo del nodo `RTL8814Native`.
- El ivar `_mac[6]` se añade a la clase (`uint8_t _mac[6];`), junto a `_bsdName`.

### 12.3 Parche de enganche (descrito, no aplicado)

`Source/AirLayer/RTLAirLayer.hpp`, dentro de `RTLAirInterface` (junto a `:98`):

```cpp
    /* Puerta de VIRTUAL_IF_CREATE (tipo 94, slot 556). */
    IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req) override;
```

`Source/AirLayer/RTLAirLayer.hpp`, dentro de `RTLAirController` (junto a `:141`), con
`class RTLAwdInterface;` declarada antes de la clase del controlador:

```cpp
    bool awdlAllowed() const { return _awdlArmed; }
    RTLAwdInterface *awdlInterface() const { return _awdl; }
    IOReturn createAwdlInterface(const uint8_t mac[6], char out[16]);
    void destroyAwdlInterface();
```

Miembros nuevos del controlador (privados, junto a `:151-186`): `RTLAwdInterface *_awdl;
IOLock *_awdlLock; bool _awdlArmed;`.

`Source/AirLayer/RTLAirLayer.cpp`, implementación del override:

```cpp
IOReturn RTLAirInterface::setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req)
{
    if (!req)
        return kIOReturnBadArgument;
    if (req->role != 6) {
        LOG("awdl: invalid role %u", req->role);
        return req->role == 7 ? kIOReturnUnsupported : (IOReturn)0xE0000001;
    }
    if (!_ctrl->awdlAllowed())
        return kIOReturnUnsupported;          /* comportamiento actual (102) */
    return _ctrl->createAwdlInterface(req->mac, req->ifname);
}
```

Para que las líneas `awdl:` lleguen al anillo `RTLAirLayerLog`, `Source/AirLayer/RTLAirLayer.cpp`
expone el logger de su unidad (junto a `:58`): `void rtlAirLog(const char *fmt, ...) { airLog(fmt, ...); }`
y el fichero nuevo usa esa función en lugar de `IOLog`. Sin este cambio, el esqueleto compila y
funciona, pero sus trazas sólo van al log del kernel.

Y en `RTLAirController::createAwdlInterface()`, la secuencia de §4.4 con `_awdlLock`; en
`RTLAirController::stop()` (`Source/AirLayer/RTLAirLayer.cpp:1547`), `destroyAwdlInterface()` antes
de `_iface->terminate()`; en `free()` (`:1558`), `IOLockFree(_awdlLock)`; en `start()` (`:1402`),
`PE_parse_boot_argn("rtlawdl", &v, sizeof(v)); _awdlArmed = v != 0;` y `_awdlLock = IOLockAlloc();`.

## 13. Cambio en `build_airlayer.sh` (descrito, no aplicado)

Un solo cambio, en dos líneas del mismo bloque:

1. **Línea 39** (`build_airlayer.sh:39`), la invocación de compilación de `RTLAirLayer.cpp`: una
   línea más, idéntica salvo el fichero de entrada (`Source/AirLayer/AWDLInterface.cpp`) y el objeto
   de salida (`"$OBJ/AWDLInterface.o"`). No cambian las banderas.

2. **Línea 66** (`build_airlayer.sh:63-67`), la lista de objetos del enlazado: añadir
   `"$OBJ/AWDLInterface.o"` junto a `"$OBJ/RTLAirLayer.o"`.

   ```diff
        -o "$KEXT/Contents/MacOS/RTL8814AirLayer" \
   -    "$OBJ/info.o" "$OBJ/RTLAirLayer.o" \
   +    "$OBJ/info.o" "$OBJ/RTLAirLayer.o" "$OBJ/AWDLInterface.o" \
   ```

No hace falta tocar `CFLAGS`/`CXXFLAGS` (`build_airlayer.sh:20-29`): `-ISource/IO80211` y
`-ISource/AirLayer` ya cubren las inclusiones, y el fichero nuevo no necesita banderas propias.
`?` No se ha compilado el esqueleto; la primera compilación real debe confirmar que la declaración
encadenada no colisiona con `IO80211_25_6_0.h` (no incluye el catálogo AWDL, §5.2).

## 14. Lista de comprobación de pruebas

Todas las pruebas se ejecutan con el gate abierto y `rtlair` en el nivel que ya usa el sistema; la
vía AWDL se controla con `rtlawdl`.

### P0 — Comprobación previa (sin cargar el kext)

| # | Comprobación | Esperado |
|---:|---|---|
| P0.1 | `Tools/re/check_awdl_symbols.sh` | 11/11 símbolos con tipo `0f` |
| P0.2 | `Tools/re/check_awdl_header.py` | Sin discrepancias en los slots puros 490 y 507–569 |
| P0.3 | Límite `0xE60 → 0xE68` de §5.2 contra el volcado de `__ZTV23IO80211VirtualInterface` | 27 métodos entre `syncDPSStats` y `setMacAddress`; `setMacAddress` en `0xF40` |
| P0.4 | `nm`/tabla del binario construido | `IO80211VirtualInterface::init`, `setAwdlInterface` y `registerEthernetInterface` resueltos, sin `__cxa_pure_virtual` en la vtable del objeto |

### T1 — Carga y gate

| # | Acción | Esperado |
|---:|---|---|
| T1.1 | Cargar `RTL8814AirLayer.kext` | `RTLAirGate = "open-validated: ..."`; log `gate open-validated` |
| T1.2 | Con `rtlawdl=0`, forzar la petición 94 | El handler devuelve 102; el daemon registra el fallo de creación y **no** espera 30 s ([`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §4; `?` el literal concreto) |
| T1.3 | Con `rtlawdl=1`, repetir | Sin timeout; `awdl0` presente |

### T2 — Capacidad y puerta

| # | Acción | Esperado |
|---:|---|---|
| T2.1 | Petición 12 sobre `en2` | Byte 4 del copyback (`+0x08`) con bit 0 a 1; byte 9 (`+0x0D`) bit 0 a 0 |
| T2.2 | Petición 94 con rol 6 | Log `awdl: create role 6`; retorno 0 en menos de 1 s |
| T2.3 | Repetir el 94 | Retorno 17 y el mismo nombre en `+0x10` |
| T2.4 | Petición 94 con rol 7 | Retorno 102 (APSTA fuera de alcance) |

### T3 — Nacimiento

| # | Acción | Esperado |
|---:|---|---|
| T3.1 | Tras T2.2, `ioreg -r -c RTLAwdInterface -l` | Properties de §6.2 completas |
| T3.2 | `ifconfig awdl0` | MAC del payload, MTU 1428, medios 802.11 |
| T3.3 | `if_nametoindex("awdl0")` | Distinto de 0 |
| T3.4 | Log del kernel | `awdl: registerEthernetInterface = 0x0`, `awdl: born awdl0` |

### T4 — Aceptación por el daemon

| # | Acción | Esperado |
|---:|---|---|
| T4.1 | `log show --last 2m --predicate 'process == "wifip2pd"'` | `Found new interface awdl0 with role AirLink` |
| T4.2 | Ídem | Sin `Timed out while waiting for interface awdl0` |
| T4.3 | Ídem | Sin `Ignoring awdl0 because its not a WiFi interface` ni `... no interface index found` |
| T4.4 | Ídem | Sin bucle de re-creación (`No AWDL interface found. Creating AWDL interface` una sola vez) |

### T5 — Medios y 97

| # | Acción | Esperado |
|---:|---|---|
| T5.1 | `ioctl(SIOCGIFMEDIA)` sobre `awdl0` | `ifm_current & 0xE0 == 0x80` |
| T5.2 | `RawGet` 97 sobre `awdl0` | Nombre del padre (`en2`) sin NUL |
| T5.3 | Log del daemon tras T5.2 | Sin error de padre; sin timeout |

`?` No se ha identificado un comando de usuario estándar para emitir el 97 aislado; la prueba se
apoya en el efecto (T4) y en una sonda propia si se añade.

### T6 — Companion opcional

| # | Acción | Esperado con D6 |
|---:|---|---|
| T6.1 | Tras T4, esperar 60 s | `Failed to create low latency interface ...` (2 líneas) y `awdl0` intacta |
| T6.2 | `ifconfig llw0` | No existe |
| T6.3 | Log del daemon | Sin `Timed out while waiting for the low latency interface` |

### T7 — Monitoreo y descarga

| # | Acción | Esperado |
|---:|---|---|
| T7.1 | Registro del monitoreo | Devuelve 0; sin re-creación de `awdl0` |
| T7.2 | Descargar el kext | `awdl0` desaparece; sin pánico; el daemon limpia el handle |
| T7.3 | Repetir carga/descarga 5 veces | Sin fugas visibles, sin doble `registerService`, sin pánico |

## 15. Riesgos y puntos abiertos

1. **Receptor del cast del 94 (`?`)**: el puente castea a `IO80211InfraProtocol`; no se ha
   verificado que el objeto sea `RTLAirInterface` y no otra interfaz o el controlador
   ([`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §3.5, §7).
2. **Búsqueda de la interfaz para el 97 (`?`)**: si el handler usa `getAwdlInterface()`, la llamada
   a `setAwdlInterface()` es obligatoria; si usa la lista de adjuntas, es inocua. Sin verificar.
3. **Tipo de retorno de `IO80211VirtualInterface::init` (`?`)**: el nombre mangleado no lo codifica;
   el esqueleto usa `bool`.
4. **Convención de numeración (`?`)**: `IO80211_25_6_0.h:458-460` y
   [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) filas 460–462 usan convenciones distintas para los
   mismos offsets; mezclarlas al escribir la declaración desplazaría la vtable. Se ancla todo a
   offsets y la comprobación P0.3 verifica el recuento (27 métodos, `0xE60 → 0xE68`).
5. **Exportación de `IO80211VirtualInterface::init` (`?`)**: tipo `0f` confirmado en el volcado, pero
   ausente de la selección de [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1.2; P0.1/P0.4 lo
   confirman.
6. **Forma de colas aceptada por la familia (`?`)**: 3 colas (camino STA) frente a las 4+2 de
   Broadcom; si el registro falla, se pasa al conjunto completo de §5.4.
7. **Valor de `RegInfo+0x10` (`?`)**: el camino STA escribe 1; para rol 6 no hay referencia.
8. **Valor de `RegInfo+0x40` (`?`)**: el STA escribe 0; Broadcom 4 o 6 según una property.
9. **`ifm_current` (`?`)**: la semilla `0x80` pasa el filtro, pero no se ha verificado que nada la
   reescriba antes de la lectura del daemon (§6.3; [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md)
   §6.3).
10. **Señal del semáforo si falla el 97 (`?`)**: la secuencia estática lo sitúa antes de la señal;
    no se ha verificado el camino de error de la closure.
11. **`registerService(2)` desde el handler (`?`)**: no se ha verificado si el despacho apple80211
    ya tiene tomado el gate de la work queue; si lo tuviera, el arranque del cliente BSD podría
    bloquearse. Mitigación: T2.2 mide el tiempo de retorno y T3.4 confirma el nombre.
12. **Metaclass de la subclase (`?`)**: `OSDefineMetaClassAndStructors` necesita `gMetaClass` y
    `C2` de `IO80211AWDLProtocol` (exportados, [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §2.2);
    no se ha verificado el registro de una subclase de terceros en esa jerarquía.
13. **`classNameOverride`** (slot 319, [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md)): podría
    cambiar el nombre visible en el IORegistry; no verificado.
14. **Semántica del `code` del 293 (`?`)**: el kernel no lo lee; queda documentado para la etapa 4.
15. **Coexistencia con el catálogo AWDL**: la declaración nueva y
    `Source/IO80211/IO80211AWDL_25_6_0.h` no pueden incluirse juntas; el catálogo no se modifica.
16. **Ruta WCL del 94 (`?`)**: si `sendIOUCToWcl` consume la request antes del puente, el override
    no se ejecuta; no verificado para este controlador.
17. **Build**: el esqueleto no se ha compilado; la primera compilación puede revelar colisiones de
    firmas en los 27 métodos de `0xE68`–`0xF38` (`?`).

## 16. Anexo — Tabla de slots de la etapa 1

`off` = offset desde el vptr; `slot` = numeración de [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md)
(`slot = off/8 + 2`). Los métodos de `0xFC8`–`0x11B8` son puros y en la etapa 1 devuelven
`kIOReturnUnsupported`; `setMacAddress` (`0xF40`) también es puro y devuelve 0.

| off | slot | Método | Tratamiento etapa 1 |
|---:|---:|---|---|
| `0xE68` | 463 | `init(ctrl, mac, role, name)` | se llama en `initWithController` |
| `0xE70`–`0xF38` | 464–489 | 26 métodos de `IO80211VirtualInterface` | heredados de la familia, sin override |
| `0xF40` | 490 | `setMacAddress(ether_addr&)` | override: acepta, no cambia |
| `0xF48`–`0xFC0` | 491–506 | `_RESERVEDIO80211VirtualInterface0..15` | heredados, sin override |
| `0xFC8` | 507 | `getSSID` | stub 102 |
| `0xFD0` | 508 | `getCHANNEL` | stub 102 |
| `0xFD8` | 509 | `getBSSID` | stub 102 |
| `0xFE0` | 510 | `getSTATE` | stub 102 |
| `0xFE8`–`0x1010` | 511–516 | sync params/enabled/extension/election id/metric/chan seq | stubs 102 |
| `0x1018`–`0x1040` | 517–522 | peer cache max, master channel, sync state, rssi thresholds, presence, election params | stubs 102 |
| `0x1048`–`0x1070` | 523–528 | device caps, secondary master, af tx mode, min rate, encryption type, txcal | stubs 102 |
| `0x1078`–`0x1098` | 529–533 | dev stats, dfsp, cca, af sec payload caps, llw params | stubs 102 |
| `0x10A0`–`0x10C8` | 534–540 | setSSID…setAWDL_BSSID | stubs 102 |
| `0x10D0`–`0x1108` | 541–547 | election metric…peer cache control | stubs 102 |
| `0x1110`–`0x1138` | 548–553 | oob request, sync template, election params, traffic reg, secondary master, af tx mode | stubs 102 |
| `0x1140`–`0x1168` | 554–559 | min rate, encryption keys, encryption type, oob auto, txcal, advertisers | stubs 102 |
| `0x1170`–`0x1198` | 560–565 | dfsp, dfsp ucsa, **companion (562, `0x1180`)**, oob sec, llw params, tx rate | stubs 102 / D6 |
| `0x11A0`–`0x11B8` | 566–569 | p2p coex, psf dwell, awdl config, rtg peer stats | stubs 102 |

El slot 570 (`getTX_RATE`, `0x11C0`) queda fuera de la declaración: su naturaleza pura discrepa
entre [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) y [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md)
§7 (`?`), y la etapa 1 no lo necesita. `?` La tabla presupone la comprobación P0.3.

## 17. Referencias

- [`AWDL_PROTOTYPE_DESIGN.md`](AWDL_PROTOTYPE_DESIGN.md) §1, §3, §7 — hechos y etapa 1.
- [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2–§7 — nacimiento, properties, orden, medios.
- [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §1, §3, §7 — payloads y definiciones C.
- [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1–§2 — semáforo y secuencia.
- [`AWDL_EVENTS.md`](AWDL_EVENTS.md) §4–§5 — monitoreo.
- [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §3–§4 — companion y veredicto.
- [`AWDL_KEXT_LINKAGE.md`](AWDL_KEXT_LINKAGE.md) §1–§2 — símbolos exportados.
- [`AWDL_VTABLE_SLOTS.md`](AWDL_VTABLE_SLOTS.md) — tabla completa de slots.
- [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) — slots puros y firmas.
- [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §4 — tablas de dispatch.
- `Source/AirLayer/RTLAirLayer.cpp`, `.hpp`, `Source/IO80211/*`, `build_airlayer.sh` — código actual.
