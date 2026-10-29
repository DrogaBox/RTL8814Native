# Delta de consistencia — `AWDL_STAGE1_IMPL_SPEC.md` frente a los diez documentos posteriores y el código

Revisión de `AWDL_STAGE1_IMPL_SPEC.md` completa contra `AWDL_STAGE1_DISPATCH_PATH.md`,
`AWDL_STAGE1_IFNET_TO_INTERFACE.md`, `AWDL_STAGE1_REGINFO_AND_GATES.md`, `AWDL_IOCTL_SLOT_MAP.md`,
`AWDL_STAGE1_IFM_CURRENT.md`, `AWDL_STAGE1_STUB_VALUES.md`, `AWDL_STAGE1_IOCTL_130_131.md`,
`AWDL_STAGE1_VIRTUAL_IF_PARENT.md`, `AWDL_STAGE1_SIOCSIFMEDIA.md` y `AWDL_STAGE1_QUEUE_SHAPE.md`, y
contra el código de etapa 1 ya presente en `Source/AirLayer/AWDLInterface.{h,cpp}` y
`Tools/awdl_stage1_hook.patch`. Base: kernelcache de macOS 26.7.1 (25G241, x86_64), IDA headless
(`idat -A -S`) sobre una copia de `boot_awdl.kc.i64`. No se ha modificado la especificación ni ningún
fichero de código.

Comprobaciones puntuales ejecutadas en esta revisión, además de la lectura:

| Comprobación | Resultado |
|---|---|
| Xrefs a `IO80211Controller::getAwdlInterface` (`0xffffff8002293256`) | **0**, ni de código ni de datos |
| Xrefs a `IO80211Controller::setAwdlInterface` (`0xffffff8002299c24`) | **0**, ni de código ni de datos |
| `IOEventSource::init` (`0xffffff8000aca900`) | `this->enabled = true` en el cuerpo |
| `IOSkywalkPacketQueue::initWithPool` (`0xffffff8002b08bc8`) | llama al init de `IOEventSource`; no llama a `disable` |
| `IOSkywalkTxSubmissionQueue::initWithPool` (`0xffffff8002b0caac`) | guard `a8 != nullptr` antes de aceptar; `a8` es la acción (el camino STA pasa refCon nulo y funciona) |
| `git apply --check Tools/awdl_stage1_hook.patch` | aplica limpio sobre el árbol actual (`0`) |

## 1. Estado del código de etapa 1

- `Source/AirLayer/AWDLInterface.h` y `.cpp` están en el árbol y **no se compilan**: `build_airlayer.sh`
  sólo compila `RTLAirLayer.cpp` (`build_airlayer.sh:40`) y enlaza `info.o` + `RTLAirLayer.o`
  (`build_airlayer.sh:66`). El parche que añade su compilación y enlazado (`Tools/awdl_stage1_hook.patch:216`, `:225`) no
  está aplicado, y `RTLAirLayer.cpp` no contiene el override del 94 ni el logger `rtlAirLog`.
- El parche aplica limpio sobre el árbol de trabajo actual, incluidos los cambios de otras sesiones en
  `RTLAirLayer.cpp`, `build_airlayer.sh` e `Info.plist` (comprobado con `git apply --check`).
- Con el código actual, `createDatapath` no puede construir la cola TX submission: se pasa acción
  nula (`AWDLInterface.cpp:140`) y `IOSkywalkTxSubmissionQueue::initWithPool` la exige no nula
  (`0xffffff8002b0caac`); `withPool` devuelve nulo y el arranque falla. Es el defecto bloqueante de
  §4.
- Las referencias `Source/AirLayer/RTLAirLayer.cpp:<línea>` de la especificación son las del árbol
  **sin** el parche. Al aplicarlo, las líneas se desplazan (1 línea en `:17`, 12 en `:57`, 20 en
  `:567`, 4 en `:1449`, 1 en `:1585`, 4 en `:1620` y el bloque de `:1764`); por ejemplo
  `attachInterface` pasa de `:1493` a `:1530` (1 + 12 + 20 + 4). `?` Un barrido de todas las
  referencias no se ha hecho.

## 2. Tabla delta

Convención: «documento posterior» cita el documento y su sección que confirma, corrige o cierra la
afirmación. `?` = no resuelto por los documentos ni por las comprobaciones de esta revisión.

### 2.1 Resumen, flujo y criterios (§0)

| # | § | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 1 | 0.1 paso 1 | La capacidad AWDL ya se publica en `getCARD_CAPABILITIES` (byte `+0x08` bit 0). | §3.2 de la propia especificación; código en `RTLAirLayer.cpp:1776`. | Confirmado |
| 2 | 0.1 paso 2 | El 94 llega al override de `RTLAirInterface`. | `DISPATCH_PATH.md` §1.4 y §3; `IFNET_TO_INTERFACE.md` §5. | Confirmado (ver #26) |
| 3 | 0.1 paso 3-4 | Se crea el objeto, se arranca y `registerService(2)` deja el ifnet hecho. | `VIRTUAL_IF_PARENT.md` §6 (el ifnet y el nombre se componen en `getBSDName`); `QUEUE_SHAPE.md` §7 (el ifnet lo crea el cliente BSD). | Confirmado en mecanismo; el orden exacto sigue en `?` (#90) |
| 4 | 0.1 paso 5 | El handler escribe el nombre en `payload+0x10`; el nombre sale del ifnet. | Código: `AWDLInterface.cpp:200-205` lee `getBSDName` y falla si está vacío. | Confirmado; §12.2 lo contradice (#30) |
| 5 | 0.2 criterio 4 | El 97 devuelve el nombre del padre. | `VIRTUAL_IF_PARENT.md` §7. | Cerrado (no necesita acción del driver) |
| 6 | 0.2 criterio 5 | Ningún evento es condición de aceptación; lo bloqueante es que el registro devuelva 0. | `DISPATCH_PATH.md` §2 (rol 6 y autorización); `STUB_VALUES.md` §4 lo deja en `?`. | Contradicho en la fatalidad (#62) |
| 7 | 0.2 criterio 6 | La etapa no emite tráfico: el datapath queda sin uso. | `QUEUE_SHAPE.md` §8 + `IOEventSource::init` (esta revisión). | **Corregido**: «sin uso» no es «inofensivo»; ver #44 y #68 |

### 2.2 Punto de partida y decisiones (§1–§2)

| # | § | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 8 | 1 (tabla) | Gate, nub/controlador, interfaz STA, stubs y capacidad: estado actual. | Sin cambios en los diez documentos; el código coincide. | Confirmado |
| 9 | 1 (tabla) | El `RegInfo` STA usa pool y 3 colas. | `QUEUE_SHAPE.md` §1: la forma de 3 colas pasa todas las validaciones. | Confirmado |
| 10 | 1 (tabla) | El catálogo `IO80211AWDL_25_6_0.h` no se incluye en el build. | Se mantiene; `AWDLInterface.h:20-22` repite la prohibición de incluirlo junto al catálogo. | Confirmado |
| 11 | 1 (tabla) | El build compila `RTLAirLayer.cpp` y enlaza dos objetos. | El parche añade `AWDLInterface.o`; `git apply --check` = 0. | Confirmado, no compilado (#96) |
| 12 | D1 | El 94 se atiende como override en `RTLAirInterface`; el receptor del cast no estaba verificado. | `DISPATCH_PATH.md` §1.4; `IFNET_TO_INTERFACE.md` §5. | Cerrado |
| 13 | D2 | `awdl0` deriva de `IO80211AWDLProtocol`. | `IOCTL_SLOT_MAP.md` §1: los 63 puentes castean a esa metaclase. | Confirmado |
| 14 | D3 | Declaración nueva por offsets; no usar el catálogo. | `AWDLInterface.h:1-22` lo implementa; `P0.3` sigue pendiente. | Confirmado |
| 15 | D4 | 94 en línea y `registerService(2)` síncrono. | `REGINFO_AND_GATES.md` §5: si el handler corre en el mismo work loop, el síncrono puede bloquear; `?`. | Sigue abierto (#90) |
| 16 | D5 | Semilla `0x80`/`0x80` como desviación deliberada frente al `8` de Broadcom. | `IFM_CURRENT.md` §3; `SIOCSIFMEDIA.md` §7. Además el camino de rol 1 ya escribe `0x8000000080` (`RTLAirLayer.cpp:210`, `:1508`). | Cerrado; la desviación no es nueva (#17) |
| 17 | D5 (matiz) | «La semilla Broadcom `8` no pasaría y el actualizador intermedio no está identificado». | `IFM_CURRENT.md` §3 cierra el valor; `SIOCSIFMEDIA.md` §5 cierra los escritores. | Cerrado |
| 18 | D6 | El 293 devuelve 102 y el daemon lo absorbe. | `STUB_VALUES.md` §4 (no fatal); `IOCTL_130_131.md` §3 (el daemon traga errores por `throws`). | Confirmado |
| 19 | D7 | Boot-arg `rtlawdl`, apagado por defecto. | Implementado en el parche (`Tools/awdl_stage1_hook.patch:105-109`). | Confirmado |

### 2.3 Capacidad y puerta del 94 (§3–§4)

| # | § | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 20 | 3.1–3.2 | Byte `+0x08` bit 0 y bit compañero `+0x07`; NAN cerrado. | Ninguno de los diez documentos lo revisa; sigue vigente. | Confirmado |
| 21 | 4.1 entrada | `gSetHandlerTable[93]`, `len == 32`, `data != NULL`. | `VIRTUAL_IF_PARENT.md` §5 (`cmp req_len, 0x20` en `0xffffff80022886c8`); `DISPATCH_PATH.md` §1.3. | Confirmado |
| 22 | 4.1 ruta WCL | `sendIOUCToWcl`; si WCL no la consume, cae al puente. | `DISPATCH_PATH.md` §1.3: la regla de WCL se aplica en las dos tablas; la caída al puente existe. | Parcialmente cerrado; `?` si hay WCL en este stack |
| 23 | 4.1 puente | `safeMetaCast` a `IO80211InfraProtocol` y slot `+0x1160`. | `DISPATCH_PATH.md` §1.3-§1.4; `IFNET_TO_INTERFACE.md` §5. | Confirmado |
| 24 | 4.1 (matiz) | El offset `+0x1160` es el del slot 556 de `IO80211InfraProtocol`. | `IOCTL_SLOT_MAP.md` §2 asigna el mismo `+0x1160` a `setAWDL_TXCAL_PERIOD` en `IO80211AWDLProtocol`. | Sin contradicción: son dos jerarquías distintas; conviene decirlo en §16 |
| 25 | 4.2 | `isCommandProhibited` se consulta antes del cast y el código devuelve `false` (`RTLAirLayer.cpp:1726`). | `IOCTL_SLOT_MAP.md` §1 atribuye esa consulta a los 63 puentes AWDL, no al del 94; el slot `+0xCC8` es de la interfaz, no del controlador. | **Corregido**: la premisa no está probada para el 94; `?` (T2.2 decide) |
| 26 | 4.2 | El cast podría no aterrizar en `RTLAirInterface`. | `DISPATCH_PATH.md` §1.4: la interfaz es la que emitió la petición; `IFNET_TO_INTERFACE.md` §5. | Cerrado |
| 27 | 4.3 | `req == nullptr` **o `len != 32`** devuelve `kIOReturnBadArgument`. | `VIRTUAL_IF_PARENT.md` §5: la longitud se valida en el handler de la familia, no en el método; la firma del override no recibe `len`. | **Corregido**: la fila de `len` no es implementable en el override |
| 28 | 4.3 | `role == 7` → 102; otros roles → `0xE0000001`. | Código del parche (`Tools/awdl_stage1_hook.patch:88-93`). | Confirmado |
| 29 | 4.3 | «Ya existe» → 17 con el nombre. | Código (`Tools/awdl_stage1_hook.patch:163-167`). | Confirmado |
| 30 | 4.4 | La secuencia toma el nombre del ifnet, «no de un contador propio». | Código: `AWDLInterface.cpp:200-205` usa `getBSDName`; §12.2 compone `"awdl%d", 0`. | **Contradicción interna de la especificación**; el código sigue §4.4 |
| 31 | 4.4 | `_awdlLock` serializa y el nombre se escribe tras publicar. | Código (`Tools/awdl_stage1_hook.patch:159-206`). | Confirmado |

### 2.4 Nacimiento, `RegInfo`, colas y arranque (§5)

| # | § | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 32 | 5.1 | Símbolos exportados y `init` con tipo `0f` sin confirmar en la selección. | `REGINFO_AND_GATES.md` no lo toca; P0.1/P0.4 siguen pendientes. | Sigue abierto |
| 33 | 5.1 | `setAwdlInterface` / `getAwdlInterface` se necesitan para publicar el objeto. | `VIRTUAL_IF_PARENT.md` §1 y §7: el 97 no los consulta; esta revisión: 0 xrefs a ambos en todo el kernelcache. | **Corregido**: no hacen falta (#57) |
| 34 | 5.2 | La declaración nueva encadena sólo `0xE68`–`0x11B8` sobre el catálogo. | `AWDLInterface.h:96-145` añade 50 overrides de slots ya existentes (`0x428`–`0xDC0`) que la especificación no enumera. | **Corregido**: el código las declara; P0.4 no las comprueba (#78) |
| 35 | 5.2 | La cadena no se ha contrastado método a método; P0.3 lo verifica. | Sigue igual. | Sigue abierto |
| 36 | 5.3 `+0x10` | Rol del `RegInfo`, valor 6, `?`. | `REGINFO_AND_GATES.md` §3: `initRegistrationInfo` lo deja a 0 y no hay ningún lector. | Cerrado: campo inerte; 6 es inocuo |
| 37 | 5.3 `+0x14` | `2` palabras de medios, citado como valor de `initRegistrationInfo` (nacimiento §2.2). | `REGINFO_AND_GATES.md` §2: `initRegistrationInfo` escribe **256** en `+0x14`; el 2 es la escritura del camino de rol 1 (`RTLAirLayer.cpp:210`, `:1507`). | **Corregido**: la fuente es el camino STA, no el default |
| 38 | 5.3 `+0x24`/`+0x28` | `0x80`/`0x80`, desviación D5. | `IFM_CURRENT.md` §1-§3; `RTLAirLayer.cpp:210`, `:1508` (`0x8000000080`). | Cerrado; el valor ya está validado por el camino STA |
| 39 | 5.3 `+0x30`/`+0x38` | Prefijo `"awdl"`, unit 0. | `VIRTUAL_IF_PARENT.md` §6: `getBSDName` compone `"%s%u"` con `ifnet_name`/`ifnet_unit`. | Confirmado |
| 40 | 5.3 `+0x3A` | Headroom u16 = 64 (`0xffffff800165de8c`). | `AWDL_INTERFACE_BIRTH.md` §2.2 dice `0x3A`; el camino de rol 1 escribe 64 en `0x3C` (`RTLAirLayer.cpp:212`, `:1509`). | `?` **Discrepancia código-documento**: dos offsets para el mismo valor |
| 41 | 5.3 `+0x40` | `0`; Broadcom 4 o 6. | `REGINFO_AND_GATES.md` §2: ningún lector. | Cerrado; mantener 0 |
| 42 | 5.3 `+0x48`/`+0x4C` | MTU 1428 (`0x594`) en los dos. | Código (`AWDLInterface.cpp:180`). | Confirmado |
| 43 | 5.4 | 3 colas; no verificado que la familia las acepte; plan B = conjunto de Broadcom. | `QUEUE_SHAPE.md` §1, §5, §10: 1 TX submission es el mínimo; la TX completion no la usa el netif; el recuento RX no se valida; ninguna comprobación mira el rol. | Cerrado; el plan B es innecesario, pero la TX submission exige acción no nula (§4) |
| 44 | 5.4 | `enable`/`disable` sólo registran el estado y no habilitan las colas. | `QUEUE_SHAPE.md` §8; esta revisión: `IOEventSource::init` deja `enabled = true` y el código nunca las deshabilita. | **Contradicho** |
| 45 | 5.5 paso 7-9 | `addEventSource` + `registerEthernetInterface` + `registerService(2)`. | Código (`AWDLInterface.cpp:152-154`, `:190`, `:197`); `REGINFO_AND_GATES.md` §5 (`?` work loop). | Parcialmente cerrado; el riesgo de #90 sigue |
| 46 | 5.5 paso 10 | `setRunningState(false)`. | Código (`AWDLInterface.cpp:198`). | Confirmado |
| 47 | 5.5 paso 11 | `setAwdlInterface(awdl)` desde el handler. | `VIRTUAL_IF_PARENT.md` §1/§7 y 0 xrefs. | Innecesario; inocuo (#57) |
| 48 | 5.6 | No hay TX/RX; las colas quedan sin acción. | `QUEUE_SHAPE.md` §8: una cola habilitada con acción nula llama a la dirección 0 en el primer trabajo. | **Corregido**: la TX submission ni se construye con acción nula; el resto nace habilitada (§4) |

### 2.5 Publicación, medios y 97 (§6–§7)

| # | § | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 49 | 6.1 | Rol, MAC, prefijo, unit, `BSD Name`, `IOInterfaceName`, tipo y UUID. | `VIRTUAL_IF_PARENT.md` §6 (nombre desde el ifnet); `IFNET_TO_INTERFACE.md` §1 (cliente BSD). | Confirmado salvo el UUID (`?`) |
| 50 | 6.1 | El orden exige todo publicado antes de la notificación. | `VIRTUAL_IF_PARENT.md` §6; el `registerService` síncrono del código lo mantiene si no bloquea (#90). | Sigue abierto por el bloqueo |
| 51 | 6.2 | `ioreg -r -c RTLAwdInterface -l` con las properties. | Sin cambios; `classNameOverride` sigue en `?`. | Sigue abierto |
| 52 | 6.3 | `ioctl_gifmedia` copia «estado+48/52/56» desde `RegInfo+0x24`/`+0x28`. | `IFM_CURRENT.md` §1: lee `*(this+0xC0)+0x30/+0x34/+0x38`, sembrados en el alta en `0xffffff8002b0a32a`/`0x2b0a332`. | **Corregido** en la procedencia y en los nombres |
| 53 | 6.3 | No verificado que nada reescriba `+0x34` antes de la lectura del daemon. | `SIOCSIFMEDIA.md` §5 y §7: el único escritor posterior es `ioctl_sifmedia` y en `awdl0` falla antes del store; ningún binario de la lista emite `SIOCSIFMEDIA` sobre `awdl0`. | Cerrado; `0x80` se mantiene |
| 54 | 6.3 | `SIOCGIFMEDIA = 0xC02C6938`. | `SIOCSIFMEDIA.md` §1 y §6. | Confirmado |
| 55 | 7.1 | El 97 hace `safeMetaCast` a `IO80211VirtualInterface`, toma `getBSDName` del padre y copia sin NUL. | `VIRTUAL_IF_PARENT.md` §1-§2. | Confirmado |
| 56 | 7.2 req. 1 | El objeto debe ser `IO80211VirtualInterface`. | `VIRTUAL_IF_PARENT.md` §2. | Confirmado |
| 57 | 7.2 req. 2 | `setAwdlInterface()` es obligatorio. | `VIRTUAL_IF_PARENT.md` §3-§7: el padre es `getPrimarySkywalkInterface` (bloque del controlador `+0x188`), escrito sólo por `attachInterface` para rol 1; el 97 nunca consulta `getAwdlInterface`; 0 xrefs. | **Corregido**: requisito innecesario |
| 58 | 7.2 req. 3 | El nombre del padre es el de la interfaz de infraestructura. | `VIRTUAL_IF_PARENT.md` §6: `en2`, ya adjunta en `RTLAirLayer.cpp:1493`. | Confirmado |
| 59 | 7.3 | Un fallo del 97 impediría la señal del semáforo. | `STUB_VALUES.md` §4: no hay rama de aborto documentada (`?`). | Discrepancia de grado; `?` (T4/T5 deciden) |

### 2.6 Companion, eventos, ciclo y fallos (§8–§11)

| # | § | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 60 | 8.1-8.2 | 293 opcional; 102 y el daemon continúa; no hay teardown. | `STUB_VALUES.md` §4; `IOCTL_130_131.md` §3. | Confirmado |
| 61 | 9 | La vía es el user client / `createEventPipe` (slot 440); ningún código previsto. | `DISPATCH_PATH.md` §2: `+0xDC0` es el slot 440, `IO80211VirtualInterface::createEventPipe` ya cubre el rol 6 y exige `com.apple.wifi_awdl.event_monitor`. | Cerrado en el lado kernel |
| 62 | 9 | Un error del registro encadena `sub_100046E30`, que re-crea la interfaz. | `STUB_VALUES.md` §4: no consta rama de aborto; `DISPATCH_PATH.md` §2 no lo trata. | Contradicho por dos documentos |
| 63 | 10.1 | El 94 corre en el hilo del llamador, fuera de gates propios. | `REGINFO_AND_GATES.md` §5-§6: la ruta ioctl corre dentro del `IOCommandGate::runAction` del cliente BSD; la del user client (RawSet, la del 94 si va por ahí) también está gated, pero el gate no se identificó (`?`). | **Corregido**: hay un gate tomado |
| 64 | 10.2 | `_awdlLock` cubre creación, consulta y destrucción. | Código del parche (`:159-206`), que además lo mantiene durante `registerService`. | Confirmado; el bloqueo largo agrava #90 |
| 65 | 10.3 | Ciclo de vida y teardown en `stop` del controlador. | Código del parche (`:196-206`). | Confirmado |
| 66 | 11 | `len != 32` → `0xE00002C2` en el override. | Igual que #27. | **Corregido** |
| 67 | 11 | Fallo de `initWithController` → `terminate()` + release. | El código declara `rtl_vif_init` como `void` (`AWDLInterface.cpp:48`) e ignora su resultado (`:110`). | **Corregido por el código**: la fila no se implementa |
| 68 | 11 | Fallo de pool/colas o registro: quitar las colas de la work queue y `deregisterEthernetInterface`. | El código devuelve `false` sin retirar las colas (`AWDLInterface.cpp:190-194`); el parche hace `terminate()` + release (`:175-179`). | **Corregido por el código**: rollback incompleto |
| 69 | 11 | Sin ifnet o nombre vacío: rollback completo, no escribir el nombre. | Código: comprueba el nombre (`AWDLInterface.cpp:200-205`), pero no desregistra. | Parcial; el ifnet no aparece como caso detectable |

### 2.7 Esqueleto, build y pruebas (§12–§14)

| # | § | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 70 | 12.1 | Cabecera con 27 + 1 + 16 + 63 slots y `_rtlAwdPad[8]`. | `AWDLInterface.h` la reproduce y añade 50 overrides de slots existentes, los `static_assert` de tamaño (`:287-289`) y `rtlAwdlPublish` (`:407`). | **Corregido** (ampliación) |
| 71 | 12.2 | `snprintf(_bsdName, "awdl%d", 0)`; `rtl_txsub_withPool(..., nullptr, nullptr, 0)`. | `AWDLInterface.cpp:200-205` usa `getBSDName` y falla si está vacío; la acción nula del esqueleto la rechaza `initWithPool` (§4). | **Corregido por el código**; §4.4 ya decía lo correcto (#30) y el esqueleto no arranca |
| 72 | 12.2 | `LOG` usa `IOLog` mientras no se exponga el logger. | El parche define `rtlAirLog` (`Tools/awdl_stage1_hook.patch:60-72`) y `AWDLInterface.cpp:23-25` lo usa. | **Corregido** |
| 73 | 12.3 | Parche descrito, no aplicado. | `Tools/awdl_stage1_hook.patch` existe, aplica limpio y añade también las funciones del controlador y el logger. | Confirmado; no aplicado |
| 74 | 13 | Línea 39 y líneas 63-67 de `build_airlayer.sh`. | El árbol actual tiene el generador del gate antes del `clang++`; las líneas citadas ya no coinciden (`:40`, `:63-67`). | **Corregido**: referencias desplazadas; el hunk aplica igual |
| 75 | 14 P0.1 | `check_awdl_symbols.sh` 11/11. | El script existe (`Tools/re/check_awdl_symbols.sh`). | Sigue abierto (no ejecutado aquí) |
| 76 | 14 P0.2 | `check_awdl_header.py` sin discrepancias. | El script existe (`Tools/re/check_awdl_header.py`). | Sigue abierto |
| 77 | 14 P0.3 | 27 métodos entre `0xE60` y `0xE68`; `setMacAddress` en `0xF40`. | Sin comprobar en esta revisión. | Sigue abierto |
| 78 | 14 P0.4 | Sin `__cxa_pure_virtual`; símbolos resueltos. | No cubre los 50 overrides de la cabecera real (#34). | **Corregido**: añadir la comprobación de esos símbolos |
| 79 | 14 T1-T7 | Lista de pruebas. | Sin cambios de contenido; el valor probatorio cambia (§6). | Ver §6 |

### 2.8 Riesgos y anexo (§15–§16)

| # | §15/§16 | Lo que dice la especificación | Documento posterior | Estado |
|---:|---|---|---|---|
| 80 | 1 | Receptor del cast del 94 sin verificar. | `DISPATCH_PATH.md` §1.4; `IFNET_TO_INTERFACE.md` §5. | Cerrado |
| 81 | 2 | Búsqueda de la interfaz para el 97 sin verificar; `setAwdlInterface` podría ser obligatoria. | `VIRTUAL_IF_PARENT.md` §3, §7; 0 xrefs. | Cerrado; la llamada es innecesaria |
| 82 | 3 | Tipo de retorno de `IO80211VirtualInterface::init`. | El código lo declara `void` (`AWDLInterface.cpp:47-48`) y la especificación §12.1 lo asume `bool`. | Sigue abierto; el código pierde el fallo (#67) |
| 83 | 4 | Convención de numeración por slots. | `IFM_CURRENT.md` §1 y `IOCTL_SLOT_MAP.md` usan offsets desde el vptr; la equivalencia `0xE50` = etiqueta 458 se mantiene. | Cerrado con anclaje a offsets; P0.3 lo verifica |
| 84 | 5 | Exportación de `init` tipo `0f` sin confirmar. | Sin cambios. | Sigue abierto |
| 85 | 6 | Forma de 3 colas frente a 4+2 de Broadcom. | `QUEUE_SHAPE.md` §1, §10. | Cerrado |
| 86 | 7 | `RegInfo+0x10`. | `REGINFO_AND_GATES.md` §3. | Cerrado |
| 87 | 8 | `RegInfo+0x40`. | `REGINFO_AND_GATES.md` §2. | Cerrado |
| 88 | 9 | `ifm_current`. | `IFM_CURRENT.md` §3; `SIOCSIFMEDIA.md` §7. | Cerrado |
| 89 | 10 | Señal del semáforo si falla el 97. | `STUB_VALUES.md` §4 (`?`); el 97 no puede fallar en este flujo porque el primario está adjunto (`VIRTUAL_IF_PARENT.md` §6). | Sigue abierto pero deja de ser bloqueante |
| 90 | 11 | `registerService(2)` desde el handler. | `REGINFO_AND_GATES.md` §5: mismo work loop posible; Broadcom lo hace y funciona. | Sigue abierto; promovido |
| 91 | 12 | Metaclass de la subclase. | Sin cambios. | Sigue abierto |
| 92 | 13 | `classNameOverride`. | Sin cambios. | Sigue abierto |
| 93 | 14 | `code` del 293. | Sin cambios (etapa 4). | Sigue abierto |
| 94 | 15 | Coexistencia con el catálogo AWDL. | `AWDLInterface.h:20-22`. | Confirmado |
| 95 | 16 | Ruta WCL del 94. | `DISPATCH_PATH.md` §1.3: hay caída al puente; `?` si hay WCL en este stack. | Parcial |
| 96 | 17 | El esqueleto no se ha compilado. | `git apply --check` = 0; no se ha compilado. | Sigue abierto |
| 97 | 16 (anexo) | `0x11A0` = `setP2P_COEX_PROTECTION`, `0x11A8` = `setPSF_DWELL`, etc. | `STUB_VALUES.md` §3 lee en `+0x11A0`..`+0x11B8` `setLLW_PARAMS`, `setTX_RATE`, `setP2P_COEX_PROTECTION`, `setPSF_DWELL`. | **Contradicho**: diferencia exacta de `0x10` (2 slots); `?` |
| 98 | 16 (anexo) | El slot 570 (`getTX_RATE`, `0x11C0`) queda fuera. | `IOCTL_SLOT_MAP.md` lista 63 filas que acaban en `0x11B8`; el 570 no está entre ellas. | Confirmado en la exclusión; la naturaleza pura sigue en `?` |

## 3. Discrepancias entre documentos posteriores

1. **Fatalidad del registro de eventos.** La especificación (§9) afirma que un error encadena
   `sub_100046E30`, que re-crea la interfaz; `STUB_VALUES.md` §4 dice que no consta rama de aborto y lo
   deja en `?`. `DISPATCH_PATH.md` §2 no resuelve el lado usuario. Lo decide T7.1. **Contradicho.**
2. **Fatalidad del 97.** La especificación (§7.3) lo trata como requisito porque la secuencia estática
   lo sitúa antes de la señal; `STUB_VALUES.md` §4 no documenta aborto. No es una contradicción de
   hecho sino de grado: el 97 no puede fallar con el primario adjunto (`VIRTUAL_IF_PARENT.md` §6).
3. **Nombres del tramo final de los 63 slots.** `IOCTL_SLOT_MAP.md` §2 y `STUB_VALUES.md` §3 asignan
   nombres distintos a `+0x11A0`..`+0x11B8`. La diferencia es exactamente `0x10` (2 slots) en las
   cuatro filas, y el propio `STUB_VALUES.md` §3 avisa de que en su vtable `+0xFC8`/`+0xFD0` siguen
   siendo `_RESERVEDIO80211VirtualInterface14/15`: es la firma de una lectura desde el ZTV en lugar del
   vptr. Bajo esa hipótesis las dos tablas coinciden, pero ninguna lo declara. **`?`** Hasta que P0.3
   lea la vtable desde el vptr, los nombres del tramo final (y el orden de los 4 últimos stubs) no
   están fijados. El resto de los 59 nombres no está afectado por esta diferencia.
4. **Forma de colas y seguridad de la acción nula.** `QUEUE_SHAPE.md` §8 concluye que no hay vía al
   salto a 0 porque «la etapa 1 no habilita las colas»; `IOEventSource::init`
   (`0xffffff8000aca900`) pone `enabled = true` en la construcción, y el código no las deshabilita.
   Además, `IOSkywalkTxSubmissionQueue::initWithPool` rechaza la construcción sin acción, así que el
   escenario de §8 no puede darse con la TX submission: o no hay cola, o hay acción. **Contradicho**
   por el kernel en las dos mitades.
5. **`setAwdlInterface`.** La especificación §7.2 la declara obligatoria; `VIRTUAL_IF_PARENT.md` §7
   demuestra que el 97 no la usa. **Contradicho**; la llamada del código es inocua pero no necesaria.
6. **Nombre de `awdl0`.** §4.4 exige tomarlo del ifnet; §12.2 lo compone con `snprintf`. Es una
   contradicción interna de la especificación; el código resuelve a favor de §4.4.
7. **Headroom.** `AWDL_INTERFACE_BIRTH.md` §2.2 y la especificación §5.3 usan `+0x3A`; el camino de rol
   1, ya en producción, escribe 64 en `+0x3C` (`RTLAirLayer.cpp:212`, `:1509`). Ninguno de los diez
   documentos lo reconcilia. **`?`** Ningún lector de ninguno de los dos aparece en los barridos
   publicados (que sólo cubren `+0x08`, `+0x0C`, `+0x10` y `+0x40`).

## 4. Código de etapa 1 frente a los documentos

| Fichero:línea | Qué hace | Qué muestran los documentos | Estado |
|---|---|---|---|
| `Source/AirLayer/AWDLInterface.cpp:140` | Crea la TX submission con acción y refCon nulos. | `IOSkywalkTxSubmissionQueue::initWithPool` (`0xffffff8002b0caac`) exige acción no nula; `withPool` (`0xffffff8002b0c9b4`) devuelve nulo. El camino STA siempre pasa `txAction` (`RTLAirLayer.cpp:365`). | **Defecto bloqueante**: `_txSubQ` nulo → `createDatapath` falso (`:143-146`) → sin `awdl0` y retorno `0xE00002BC` |
| `Source/AirLayer/AWDLInterface.cpp:152-154` | Añade las tres colas a la work queue. | `QUEUE_SHAPE.md` §8 supone que una cola con acción nula puede existir y que «no se habilitan»; `IOEventSource::init` deja `enabled = true` y `IOSkywalkPacketQueue::initWithPool` (`0xffffff8002b08bc8`) no lo borra. Con la acción nula corregida, las colas quedan habilitadas y el work loop las atiende. | **Defecto secundario**: con acción válida no hay salto a 0, pero la premisa de §8 es falsa |
| `Source/AirLayer/AWDLInterface.cpp:190-194` | Si falla el registro, devuelve `false`. | Especificación §11: quitar las colas de la work queue y desregistrar. | **Defecto**: el rollback no retira las colas |
| `Tools/awdl_stage1_hook.patch:175-179` | Ante fallo de init/arranque, `terminate()` + release; no llama a `stop()` explícitamente. | Especificación §11/§10.3. `?` `IOService::terminate` puede invocar `stop` y llegar a `removeQueues`; no verificado. | `?` |
| `Source/AirLayer/AWDLInterface.cpp:197` | `registerService(kIOServiceSynchronous)` dentro del handler del 94. | `REGINFO_AND_GATES.md` §5: el despacho corre bajo un gate; si el alta necesita ese mismo work loop, se bloquea. Broadcom usa el mismo patrón. | **Riesgo abierto**; T2.2 lo mide |
| `Source/AirLayer/AWDLInterface.cpp:48`, `:110` | `rtl_vif_init` se declara `void` y su resultado se ignora. | Especificación §11 exige rollback si falla el init. | **Defecto**: no hay señal de fallo |
| `Source/AirLayer/AWDLInterface.cpp:270-273` y `AWDLInterface.h:407` | Publica el objeto con `setAwdlInterface`. | 0 xrefs a `set/getAwdlInterface` en el kernelcache; `VIRTUAL_IF_PARENT.md` §7. | **Innecesario**; inocuo |
| `AWDLInterface.h:96-145` | 50 overrides de slots existentes, declarados sin cuerpo local. | No están en la especificación §12.1 ni en P0.4; cada uno debe resolver al símbolo exportado de `IO80211VirtualInterface`. | `?` **Riesgo de enlace**; P0.4 debe enumerarlos |
| `AWDLInterface.h:287-289` | `static_assert` de tamaños `0x128`/`0x130`. | La especificación sólo fija el `0x130` del metaclass. | Confirmado; el build decide |
| `AWDLInterface.cpp:200-205` | Obtiene el nombre con `getBSDName` y falla si está vacío. | §4.4 y `VIRTUAL_IF_PARENT.md` §6. | Correcto; §12.2 está obsoleto |
| `AWDLInterface.cpp:212-228` | `removeQueues` deshabilita, quita del work loop y libera. | Especificación §11; `QUEUE_SHAPE.md` §8. | Correcto si llega a ejecutarse |

## 5. Ediciones de la especificación, en orden

1. **§4.3 y §11**: quitar `len != 32` del contrato del override. La longitud la valida el handler de la
   familia (`0xffffff80022886c8`) antes del puente; el método sólo recibe el puntero.
2. **§4.2**: retirar la frase sobre `isCommandProhibited` como prueba para el 94. La consulta está
   documentada para los 63 puentes AWDL, no para el del 94, y el slot `+0xCC8` es de la interfaz, no
   del controlador. Dejar la conclusión como `?` hasta T2.2.
3. **§5.2 y §12.1**: añadir los 50 overrides de slots ya existentes que la cabecera real declara
   (`0x428`–`0xDC0`) y decir que no desplazan la vtable por ser overrides. Añadir la comprobación de
   enlace de sus símbolos a P0.4.
4. **§5.3**: corregir la fuente de `+0x14 = 2` (camino STA, `RTLAirLayer.cpp:210`/`:1507`, no el
   default 256 de `initRegistrationInfo`); marcar `+0x10` y `+0x40` como campos sin lector; resolver
   `+0x3A` frente a `+0x3C` o marcar los dos como `?`.
5. **§5.4**: sustituir el `?` y el plan B por el resultado de `QUEUE_SHAPE.md`: 1 TX submission es el
   mínimo, la TX completion no la usa el netif, el recuento RX no se valida y ninguna comprobación mira
   el rol. Añadir que la TX submission exige una acción no nula en `initWithPool` y que las colas
   nacen habilitadas: el plan debe pasar una acción no-op (o deshabilitarlas) y no añadirlas como
   event sources mientras no haya drenaje.
6. **§5.5 y §10.1**: dejar D4 como condicional. El 94 corre bajo un gate, identificado como el del
   cliente BSD sólo en la ruta ioctl (`REGINFO_AND_GATES.md` §5-§6); el `registerService` síncrono
   puede esperar a ese mismo work loop.
   T2.2 mide el retorno; si bloquea, la alternativa asíncrona obliga a revisar el criterio del nombre
   en `payload+0x10`.
7. **§7.2**: borrar el requisito 2. El 97 lee `getPrimarySkywalkInterface` (bloque del controlador
   `+0x188`, escrito sólo por `attachInterface` para rol 1) y no consulta `getAwdlInterface`; el
   primario ya está adjunto en `RTLAirLayer.cpp:1493`. `setAwdlInterface` no tiene ninguna referencia
   en el kernelcache.
8. **§6.3**: reemplazar «no verificado» por el cierre de `SIOCSIFMEDIA.md`: el descriptor se siembra en
   el alta (`+0x30` y `+0x34` desde `RegInfo+0x24`/`+0x28`, `0xffffff8002b0a32a`/`0x2b0a332`) y el
   único escritor posterior, `ioctl_sifmedia` (cabeza `0xffffff8002b0affa`), falla antes del store en
   `awdl0`; `0x80` se mantiene.
9. **§9**: añadir el gate real del evento (rol 6 y `com.apple.wifi_awdl.event_monitor`,
   `0xffffff80023347fc`) y resolver la fatalidad con T7.1 en lugar de citar `sub_100046E30` como hecho.
10. **§12.2**: sustituir el `snprintf` por `getBSDName` (el código ya lo hace) y reflejar `rtlAirLog`,
    `removeQueues` y las funciones del controlador del parche.
11. **§13**: actualizar las líneas de `build_airlayer.sh` (el generador del gate desplaza el bloque) y
    anotar que el parche aplica limpio pero no se ha compilado.
12. **§14**: añadir P0.5 (símbolos de los 50 overrides) y T3.5 (subir `awdl0`, provocar tráfico IPv6 y
    comprobar que no hay pánico por la acción nula). Degradar T5.1 a comprobación de regresión: ya no
    decide nada.
13. **§15**: reescribir la lista con los estados de §2.8; quitar 1, 2, 6, 7, 8, 9 y 15 como cerrados;
    promover 11; añadir como riesgos nuevos la acción nula (§4), el rollback incompleto (#68), el
    headroom (#40) y los overrides sin comprobar (#34).
14. **§16**: declarar que los offsets son desde el vptr y no desde el ZTV; añadir la discrepancia de
    `0x10` con `STUB_VALUES.md` §3 y dejar los cuatro últimos nombres en `?` hasta P0.3.

## 6. Pruebas de hardware T1–T7: qué decide cada una ahora

| Prueba | Qué decide hoy |
|---|---|
| T1 (carga y gate) | Que el gate de versión y `rtlawdl` funcionan; T1.2 que el 94 devuelve 102 sin el boot-arg. |
| T2 (capacidad y puerta) | T2.1 la capacidad; **T2.2** el receptor del cast, el retorno en menos de 1 s y, con él, si el `registerService` síncrono se bloquea (#90); T2.3 el camino EEXIST; T2.4 el rol 7. |
| T3 (nacimiento) | T3.1 las properties y el rol; T3.2 MAC, MTU y medios; T3.3 el índice BSD; T3.4 los logs. Si no aparece `awdl0`, T3.4 debe mostrar `awdl: packet queue alloc failed`: es la firma del rechazo de la TX submission con acción nula (§4), anterior a cualquier prueba de datapath. **T3.5 nueva**: subir `awdl0` y provocar tráfico IPv6/ND para comprobar que la acción no-op y las colas habilitadas no provocan pánico. |
| T4 (aceptación) | El filtro de medios, el 97 y el nombre: T4.1–T4.3 confirman que el daemon acepta la interfaz; T4.4 descarta el bucle de re-creación. |
| T5 (medios y 97) | Sólo regresión: `ifm_current = 0x80` y `en2` ya están fijados por los documentos; sirven para detectar una regresión del `RegInfo` o del `attachInterface`. |
| T6 (companion) | Que el 102 del 293 se absorbe sin timeout, sin teardown y sin `llw0`. |
| T7 (monitoreo y descarga) | **T7.1** decide la única discrepancia factual que queda entre documentos (fatalidad del registro de eventos, #62) y la autorización del proceso; T7.2 el desmontaje; T7.3 el ciclo carga/descarga y el rollback de colas (#68). |

Todas siguen siendo necesarias. T5 baja de decisiva a regresión; T2.2, T3.5 y T7.1 suben de valor.

## 7. Pendientes que los documentos no cierran

- `?` Nombres y orden de los cuatro últimos slots puros (#97) hasta leer la vtable desde el vptr.
- `?` Headroom en `+0x3A` o `+0x3C` (#40).
- `?` Tipo de retorno de `IO80211VirtualInterface::init` (#82); el código lo da por `void`.
- `?` Símbolos de los 50 overrides de `AWDLInterface.h` (#34).
- `?` Comportamiento de `registerService(2)` bajo el gate del cliente BSD (#90).
- `?` Si `terminate()` sobre un arranque fallido invoca `stop()` y retira las colas (#68).
- `?` Fatalidad real del registro de eventos en wifip2pd (#62).
- `?` Si existe un WCL en este stack que pueda consumir el 94 (#95).
- `?` `classNameOverride`, metaclass de la subclase y `IOSkywalkNexusUUID` en el IORegistry.
