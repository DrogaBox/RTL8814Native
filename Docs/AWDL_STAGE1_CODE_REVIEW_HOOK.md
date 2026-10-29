# Revisión de código — hook etapa 1 AWDL aplicado en `RTLAirLayer`

Revisión estática del hook aplicado en el árbol de trabajo (`Source/AirLayer/RTLAirLayer.cpp`,
`Source/AirLayer/RTLAirLayer.hpp` — partes añadidas por `Tools/awdl_stage1_hook.patch`, que el árbol
reproduce textualmente) contra `Docs/AWDL_STAGE1_IMPL_SPEC.md` §4–§9 y
`Docs/AWDL_STAGE1_DISPATCH_PATH.md`. No se consultó el desensamblado ni se ejecutó nada sobre
hardware; no se modificó ningún fichero. Lo que las fuentes no permiten decidir se marca `?`.

Alcance revisado: `setVIRTUAL_IF_CREATE`, `createAwdlInterface`, `destroyAwdlInterface`, el armado
por boot-arg `rtlawdl`, `rtlAirLog`, la cerradura `_awdlLock`, el desmontaje en `stop`/`free` del
controlador y las piezas de `AWDLInterface.cpp` que el hook invoca, en cuanto afectan al contrato del
94 y al ciclo de vida.

## 1. Comprobaciones conformes

Verificado sin hallazgo, contra la spec y la ruta de despacho:

- **Payload**: `apple80211_virt_if_create_data` en `AWDLInterface.h:74-81` — 32 B exactos
  (`static_assert`), `f0` @+0x00, `mac[6]` @+0x04, `pad` @+0x0A, `role` u32 @+0x0C, `ifname[16]`
  @+0x10 (§4.1). `IO80211_25_6_0.h:206` sólo lo declara adelantado, así que la definición es única.
  La longitud la valida el handler de la familia; el override no puede ni debe revalidarla.
- **Tabla de roles** (§4.3, §11): rol 6 crea; rol 7 → `kIOReturnUnsupported` (102); resto →
  `0xE0000001`, igual que Broadcom. Ver A5 para el orden de comprobaciones y el log.
- **Ya existe**: 17 (EEXIST) con el nombre escrito igualmente en `payload+0x10`
  (`RTLAirLayer.cpp:1780-1783`), tomado de `_bsdName`, que sólo se rellena tras un
  `registerService(kIOServiceSynchronous)` con nombre BSD no vacío (§4.1, §4.3). En los caminos de
  fallo no se escribe el nombre (§11, regla común); el buffer llega a cero desde el daemon.
- **Armado**: sin `rtlawdl`, un 94 con rol 6 responde 102 (`RTLAirLayer.cpp:585-586`), exactamente
  lo que el stub generaba (§4.3 fila 2, §10.3 fila 1). Con la puerta de versión cerrada el kext
  completo no arranca (`RTLAirNub::start`/`RTLAirController::start`), luego no existe handler que
  conteste: inercia estructural. `_awdlArmed` se fija una vez en `RTLAirController::start`
  (`:1452-1455`) con `PE_parse_boot_argn` y valor por defecto 0.
- **Doble creación**: `_awdlLock` cubre consulta de `_awdl`, creación completa, publicación y
  escritura del nombre (§4.4, §10.2): dos 94 concurrentes no crean dos objetos; el segundo espera y
  responde 17 con el nombre. Ver A1/A2 por el coste de ese diseño.
- **Orden de `stop`** (§10.3): `destroyAwdlInterface()` antes de `quiesce()` y antes del `stop` de la
  familia; en la destrucción: anular `_awdl` bajo lock, `setAwdlInterface(NULL)`, `terminate()`,
  `release()`. El lock se libera en `free()` (`:1624-1627`).
- **Colas y pool** (§5.4): 1 TX submission con acción no nula (`txNoop`, consume el lote y lo
  devuelve por la TX completion en lotes de 64 < capacidad 128, devuelve `count`), 1 TX completion,
  1 RX completion, un pool por dirección; añaden sus event sources a la work queue de la interfaz y
  `removeQueues` es idempotente. Conforme a CODE_FIXES §1-§2.
- **`RegistrationInfo`** (§5.3): todos los campos coinciden (+0x0C=3, +0x10=6, +0x14=2, +0x24=0x80,
  +0x28=0x80, +0x30="awdl", +0x38=0, +0x3C=64 como u16, +0x40=0, +0x48/+0x4C=0x594, +0x108=MAC del
  94). Memoria propia del objeto, liberada en `RTLAwdInterface::free`.
- **Secuencia de arranque** (§5.5 pasos 1-10): `init()`, `IO80211VirtualInterface::init(ctrl, mac,
  6, "awdl")`, `setInterfaceId(2)`, `attach(ctrl)`, `IO80211VirtualInterface::start`, RegInfo,
  datapath, `registerEthernetInterface`, `registerService(kIOServiceSynchronous)` (constante 0x2 del
  SDK, igual al «registerService(2)» de la spec), `setRunningState(false)`.
- **`setAwdlInterface`** (paso 11, §4.4): se llama desde el handler tras el arranque, no desde el
  arranque. Permanece en el código conforme a la decisión del delta (CODE_FIXES defecto 4 no
  aplicado; símbolo exportado, sin lectores: inocuo).
- **Despacho**: el override está en `RTLAirInterface` (rol 1), el único objeto que pasa el
  `safeMetaCast` a `IO80211InfraProtocol` del puente del 94 (ruta de despacho §1.4, §3); cubre las
  rutas ioctl (legacy) y user client (moderna) porque ambas acaban en el mismo puente.
- **`rtlAirLog`**: mismo anillo que `airLog`, re-formateo seguro (`airLog("%s", buf)`, sin inyección
  de formato), truncado coherente con el propio `airLog`.
- **Fugas en el datapath**: todas las ramas de fallo de `createDatapath`/`startWithRegistration`
  liberan colas (vía `teardownDatapath`/`stop`) y pools (vía `free`); no se ha encontrado fuga.

## 2. Hallazgos

### A1 — Inversión de cerraduras: `_awdlLock` retenido a través de `terminate()` / `runAction`

`Source/AirLayer/RTLAirLayer.cpp:1808-1822` (`destroyAwdlInterface`) y, por reflejo, la rama de
fallo de `createAwdlInterface` (`:1791-1797`).

El 94 se ejecuta con el gate del work loop de la interfaz cerrado (ruta de despacho §1.1: la orden
se ejecuta en el work loop; §1.2: la ruta user client entra por el command gate), y
`createAwdlInterface` toma `_awdlLock` dentro de ese contexto (`:1779`). `destroyAwdlInterface`
toma `_awdlLock` y llama `awdl->terminate()` con el lock retenido; la terminación de la familia baja
a `RTLAwdInterface::stop` → `teardownDatapath` → `wq->runAction(removeQueues)`, que espera al work
loop. Con un 94 en vuelo (p. ej. desenchufar el adaptador mientras wifip2pd levanta `awdl0`):

- hilo de `stop`: retiene `_awdlLock`, espera el gate del work loop;
- hilo del 94: retiene el gate del work loop, espera `_awdlLock`.

Bloqueo mutuo (AB-BA): la descarga/detención del controlador cuelga. La ventana es estrecha pero
real en cualquier descarga con tráfico de creación. La spec §10.2 prescribe retener el lock durante
la **creación** (hasta `setAwdlInterface`), no durante la **destrucción**; retenerlo a través de
`terminate()` es decisión del código.

```cpp
/* antes (destroyAwdlInterface, RTLAirLayer.cpp:1812-1821) */
IOLockLock(_awdlLock);
RTLAwdInterface *awdl = _awdl;
_awdl = nullptr;
if (awdl) {
    rtlAwdlPublish(this, nullptr);
    awdl->terminate();        /* puede esperar el work loop con el lock tomado */
    awdl->release();
}
IOLockUnlock(_awdlLock);

/* después */
IOLockLock(_awdlLock);
RTLAwdInterface *awdl = _awdl;
_awdl = nullptr;
if (awdl)
    rtlAwdlPublish(this, nullptr);   /* barato y sin bloqueo: bajo lock */
IOLockUnlock(_awdlLock);
if (awdl) {
    awdl->terminate();
    awdl->release();
    LOG("awdl: teardown");
}
```

La rama de fallo de `createAwdlInterface` (`:1793-1794`) necesita el mismo tratamiento (soltar el
lock antes de `terminate`/`release`, o reordenar la limpieza fuera de la sección crítica). Con este
cambio, `terminate`/`release` dejan de estar bajo lock; el invariantes de creación única se
conserva porque `_awdl` se anula dentro de la sección crítica y `rtlAwdlPublish(NULL)` sigue bajo
lock (un `create` concurrente no puede intercalarse entre anular y despublicar).

Severidad: **comportamiento incorrecto** (cuelga `stop`/descarga). El solape exacto entre el
`terminate` de la familia y el work loop no se ha medido; el experimento T2.2 de la spec cubre el
contexto de gate (`?` en la forma exacta del disparador, no en la inversión).

### A2 — Carrera `stop` ↔ 94: `createAwdlInterface` ignora `_stopping` y el armado no se retira

`Source/AirLayer/RTLAirLayer.cpp:1586-1596` (`stop`), `:1775-1804` (`createAwdlInterface`),
`:1943` (uso existente de `_stopping`).

`RTLAirController::stop` llama `destroyAwdlInterface()` al principio, pero ni limpia `_awdlArmed`
ni marca `_stopping` antes; `_stopping` lo fija después `quiesce()` (`:1574`) y `_awdlArmed` no se
retira nunca. Entre el final de `destroyAwdlInterface` y el final de la terminación de `_iface`, un
94 en vuelo (la interfaz STA sigue vista por un user client hasta que su terminación concluye) pasa
`awdlAllowed()`, no ve `_awdl`, y crea un `awdl0` nuevo sobre un controlador en detención. Nada lo
destruye después: `_awdl` queda retenido hasta `free()` (que no lo libera, A3), el objeto queda
colgado de un controlador detenido y puede tocar memoria liberada del proveedor.

```cpp
/* antes (RTLAirLayer.cpp:1586-1589) */
void RTLAirController::stop(IOService *provider)
{
    destroyAwdlInterface();
    quiesce();

/* después */
void RTLAirController::stop(IOService *provider)
{
    _stopping = true;          /* cerrar la ventana antes de desmontar */
    destroyAwdlInterface();
    quiesce();
```

y en `createAwdlInterface`, dentro de la sección crítica:

```cpp
/* después de IOLockLock(_awdlLock); en createAwdlInterface */
if (_stopping) {
    IOLockUnlock(_awdlLock);
    return kIOReturnNotReady;
}
```

(Con A1 aplicado, `_stopping` debe fijarse antes de `destroyAwdlInterface`, como arriba, porque la
sección crítica se acorta y un 94 puede tomar el lock justo después de la despublicación.)

Severidad: **comportamiento incorrecto** (fuga y uso tras liberación en la carrera de descarga).

### A3 — `free()` del controlador no desmonta `_awdl`

`Source/AirLayer/RTLAirLayer.cpp:1598-1637`.

El comentario del parche promete desmontaje «en `stop` o `free` del controlador», pero `free()` sólo
libera `_awdlLock` (`:1624-1627`); no llama `destroyAwdlInterface()` ni suelta `_awdl`. En el ciclo
IOKit normal `stop` precede a `free` y `_awdl` ya es NULL, pero basta un camino sin `stop` (la
carrera A2, o una terminación que no alcance `stop`) para que `free()` destruya el lock con un
objeto vivo colgado de él: fuga del objeto y del lock semántico. El desmontaje es idempotente y
barato; falta la llamada de defensa.

```cpp
/* antes (RTLAirLayer.cpp:1624-1627) */
if (_awdlLock) {
    IOLockFree(_awdlLock);
    _awdlLock = nullptr;
}

/* después */
destroyAwdlInterface();
if (_awdlLock) {
    IOLockFree(_awdlLock);
    _awdlLock = nullptr;
}
```

Severidad: **cosmético** (defensa; no se ha identificado un camino real sin `stop` aparte de A2).

### A4 — Códigos de causa colapsados a `0xE00002BC` en la creación

`Source/AirLayer/RTLAirLayer.cpp:1791-1797`.

La spec §4.3 prescribe devolver «el `IOReturn` de la causa» y §11 distingue `kIOReturnNoMemory`
(asignación), `kIOReturnNoResources` (pool/colas) y el `IOReturn` recibido de
`registerEthernetInterface`, reservando `0xE00002BC` para el fallo del objeto. El código aplicado
condensa `initWithController` y `startWithRegistration` en un único booleano y responde siempre
`0xE00002BC`, perdiendo la causa real (p. ej. el `r` de `rtl_eth_register`, registrado en el log
pero no devuelto). El daemon no puede distinguir fallos; el diagnóstico queda en el anillo de log.

```cpp
/* antes (RTLAirLayer.cpp:1791-1797) */
if (!awdl->initWithController(this, mac) || !awdl->startWithRegistration(this)) {
    LOG("awdl: init failed");
    awdl->terminate();
    awdl->release();
    IOLockUnlock(_awdlLock);
    return (IOReturn)0xE00002BC;
}

/* después (mínimo: propagar la causa del registro) */
IOReturn r = awdl->initWithController(this, mac) ? awdl->startWithRegistration(this)
                                                 : (IOReturn)0xE00002BC;
if (r != kIOReturnSuccess) {
    awdl->terminate();
    awdl->release();
    IOLockUnlock(_awdlLock);
    return r;    /* startWithRegistration pasa a devolver IOReturn y propaga rtl_eth_register */
}
```

Severidad: **comportamiento incorrecto** (menor: respuesta genérica al daemon; la spec §11 lo pide
diferenciado).

### A5 — Comentario y orden de comprobaciones en `setVIRTUAL_IF_CREATE`

`Source/AirLayer/RTLAirLayer.cpp:570-588`.

Dos desvíos menores, juntos porque comparten fix:

1. El comentario afirma que «cualquier otro rol conserva la respuesta del stub generado, de modo que
   un sistema sin `rtlawdl` se comporta exactamente igual que antes», pero es falso para roles
   ∉ {6, 7}: el stub devolvía 102 y el override devuelve `0xE0000001` con o sin `rtlawdl`. Con la
   capacidad NAN no anunciada (`getCARD_CAPABILITIES` deja `p[13]` bit 0 a cero) no se espera emisor
   de roles 8-10, así que el desvío es teórico, pero el comentario promete inercia que el código no
   da. La spec §4.3 no ordena las comprobaciones, y mover el check de armado al principio cumple
   todas sus filas y además el comentario.
2. El log `awdl: invalid role %u` se emite también para el rol 7, que la spec §11 no clasifica como
   rol inválido, y para cualquier rol ≠ 6 incluso con `rtlawdl` ausente (ruido en un sistema donde
   AWDL debe ser inerte).

```cpp
/* antes (RTLAirLayer.cpp:581-586) */
if (req->role != 6) {
    LOG("awdl: invalid role %u", req->role);
    return req->role == 7 ? kIOReturnUnsupported : (IOReturn)0xE0000001;
}
if (!_ctrl->awdlAllowed())
    return kIOReturnUnsupported;

/* después */
if (!_ctrl->awdlAllowed())
    return kIOReturnUnsupported;          /* inercia total sin rtlawdl */
if (req->role != 6) {
    if (req->role != 7)
        LOG("awdl: invalid role %u", req->role);
    return req->role == 7 ? kIOReturnUnsupported : (IOReturn)0xE0000001;
}
```

Severidad: **cosmético** (comentario impreciso + log; sin emisor esperado para los roles afectados).

### A6 — `?` `registerService(kIOServiceSynchronous)` bajo el gate del work loop (D4)

`Source/AirLayer/AWDLInterface.cpp:207`, invocado con `_awdlLock` retenido desde
`RTLAirLayer.cpp:1791`.

La spec §5.5 (D4) y §10.1 marcan como `?` si el registro síncrono puede esperar al mismo work loop
cuyo gate retiene el hilo del 94 (en la ruta ioctl el 94 corre en ese work loop; en la user client,
bajo su command gate). Si el cliente BSD necesita ese mismo work loop, la creación se bloquea a sí
misma. El código sigue la spec al pie de la letra; no es decidible con las fuentes disponibles y lo
mide T2.2. Si bloquea, la alternativa asíncrona obliga a revisar el criterio del nombre en
`payload+0x10` (§0.1 paso 5).

Severidad tentativa: **bloquea arranque** (de `awdl0`, no del sistema) — `?`, decide T2.2.

### A7 — `?` Rollback sin `deregisterEthernetInterface` cuando el registro llegó a triunfar

`Source/AirLayer/AWDLInterface.cpp:210-215`.

Si `registerEthernetInterface` triunfa pero el nombre BSD no aparece, el código llama
`teardownDatapath()` y devuelve fallo; la spec §11 prescribe también
`deregisterEthernetInterface` «si llegó a registrarse» y `detach`. El código delega en el
`terminate()` posterior (familia, `RTLAirLayer.cpp:1793`), que puede encargarse del desregistro;
no consta en los documentos consultados qué hace exactamente
`IO80211VirtualInterface::terminate` en ese estado, así que no es decidible aquí. Lo detecta T3.

Severidad tentativa: **comportamiento incorrecto** (fuga de registro si la familia no desregistra)
— `?`.

### A8 — `awdlInterface()`: puntero crudo sin lock ni retención

`Source/AirLayer/RTLAirLayer.hpp:183`.

`RTLAwdInterface *awdlInterface() const { return _awdl; }` lee `_awdl` sin `_awdlLock` y devuelve
un puntero sin retener; tras `destroyAwdlInterface()` es colgante. Hoy no tiene ningún llamador
(verificado en el árbol), pero publicado en la cabecera invita a un uso posterior que rompería el
invariante del §10.2. O se elimina, o se documenta su contrato (sólo bajo `_awdlLock`, sin retener).

Severidad: **cosmético** (sin llamadores hoy).

### A9 — `STUB_ENTER()` ausente en el override del 94

`Source/AirLayer/RTLAirLayer.cpp:577`.

Todos los handlers vecinos llaman `STUB_ENTER()` para la traza de primera llamada que se vuelca en
el log de hardware; el override del 94 no lo hace, así que la traza no registrará el uso del 94. Los
`LOG` propios lo compensan parcialmente, pero la comparación de traces T1-T7 quedará con un hueco.

```cpp
/* antes */
IOReturn RTLAirInterface::setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req)
{
    if (!req)

/* después */
IOReturn RTLAirInterface::setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req)
{
    STUB_ENTER();
    if (!req)
```

Severidad: **cosmético**.

### A10 — `?` Longitud del payload en la ruta legacy

El override no recibe la longitud, sólo el puntero al payload. La ruta moderna exige 32 B (ruta de
despacho §1.3); la legacy (`setVirtualInterfaceCreate`) acepta `len < 32` y copia `min(len, 32)` a
su `apple80211req` de pila, con lo que `role`/`ifname` podrían leer bytes no inicializados si un
emisor enviara menos de 32. El daemon siempre envía 32 (`Docs/AWDL_STRUCTS.md` §3.2) y no hay
defensa posible en el driver sin cambiar la firma del slot: no es defecto del código, se registra
como límite del contrato. `?` (depende de la inicialización del `req` de pila de la familia).

Severidad tentativa: **comportamiento incorrecto** sólo ante un emisor hostil/incompleto — `?`.

## 3. Tabla final

| # | Hallazgo | Fichero:línea | Severidad | Estado |
|---|---|---|---|---|
| A1 | `_awdlLock` retenido a través de `terminate()`/`runAction`: AB-BA con el 94 bajo el gate del work loop; cuelga `stop`/descarga | `RTLAirLayer.cpp:1808-1822` (y `:1791-1797`) | comportamiento incorrecto (cuelga detención/descarga) | decidido |
| A2 | Carrera `stop`↔94: sin `_stopping` ni retirada de `_awdlArmed`, un 94 en vuelo recrea `awdl0` sobre un controlador en detención | `RTLAirLayer.cpp:1586-1596`, `1775-1804` | comportamiento incorrecto (fuga/uso tras liberación) | decidido |
| A4 | Códigos de causa colapsados a `0xE00002BC`; la spec §4.3/§11 pide el `IOReturn` de la causa | `RTLAirLayer.cpp:1791-1797` | comportamiento incorrecto (menor) | decidido |
| A3 | `free()` no desmonta `_awdl` (sólo `stop` lo hace); falta defensa idempotente | `RTLAirLayer.cpp:1598-1637` | cosmético (defensa) | decidido |
| A5 | Comentario «exactamente igual que antes» falso para roles ∉{6,7}; log de rol inválido para el 7 y con `rtlawdl` ausente | `RTLAirLayer.cpp:570-588` | cosmético | decidido |
| A6 | `registerService(kIOServiceSynchronous)` bajo el gate del work loop con `_awdlLock` retenido (D4 de la spec) | `AWDLInterface.cpp:207` | bloquea arranque de `awdl0` (potencial) | `?` T2.2 |
| A7 | Rollback sin `deregisterEthernetInterface`/`detach` cuando el registro triunfó y falta el nombre BSD | `AWDLInterface.cpp:210-215` | comportamiento incorrecto (potencial) | `?` T3 |
| A10 | Ruta legacy acepta `len < 32`; el override no puede validar longitud (límite del contrato) | `RTLAirLayer.cpp:577-588` | comportamiento incorrecto (sólo emisor incompleto) | `?` |
| A8 | `awdlInterface()` sin lock ni retención, sin llamadores | `RTLAirLayer.hpp:183` | cosmético | decidido |
| A9 | `STUB_ENTER()` ausente en el override del 94 | `RTLAirLayer.cpp:577` | cosmético | decidido |

Los decididos A1+A2+A3 se resuelven juntos: fijar `_stopping` al inicio de `stop`, acotar la sección
crítica de `destroyAwdlInterface` a la anulación del puntero y la despublicación, y llamar
`destroyAwdlInterface()` al inicio de `free()`. A4 y A5 son cambios locales independientes. A6 y A7
quedan para las pruebas T2.2 y T3 del plan de la spec.
