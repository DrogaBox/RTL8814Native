# Revisión de código — `AWDLInterface.cpp` / `AWDLInterface.h` (etapa 1)

Nota de verificación: el hallazgo 1 se aplicó (`rtl_vif_start` declarado `bool`). El hallazgo 2 queda cerrado por `AWDL_STAGE1_PENDING_CHECKS.md` §3 (el `free` de la interfaz ya desregistra; la llamada explícita no es necesaria). El hallazgo 6 no procede: `AWDL_STAGE1_LOG_STRINGS_AUDIT.md` confirma que el literal real es `awdl: registerEthernetInterface 0x%x` y la guía de pruebas lo busca así. El hallazgo 3 (orden de celdas de la vtable) sigue abierto y se verifica aparte.

Revisión línea a línea de `Source/AirLayer/AWDLInterface.cpp` (311 líneas) y
`Source/AirLayer/AWDLInterface.h` (412 líneas) contra
[`AWDL_STAGE1_IMPL_SPEC.md`](AWDL_STAGE1_IMPL_SPEC.md) y
[`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md). No se ha modificado ningún fichero de
código ni de build. No se ha usado IDA: el contraste de ABI se hizo contra los volcados de símbolos
de `Docs/re/io80211_25.6.0/` y contra el objeto ya construido
(`Build/obj_airlayer/AWDLInterface.o`, fresco: misma fecha que el `.cpp`; el `.h` es anterior).

Estado de partida verificado en el árbol: los defectos 1 (acción no-op), 2 (rollback), 3
(`rtl_vif_init` bool) y 6 (headroom `+0x3C`) de CODE_FIXES están aplicados; el 4
(`setAwdlInterface`) no está aplicado (decisión documentada en el handoff, «inocuo»); el 5 no
requería cambio de código.

## 0. Método y verificación de partida

| Comprobación | Resultado |
|---|---|
| `nm -u` del objeto construido | 543 indefinidos; **93** de `IO80211VirtualInterface` — coincide con CODE_FIXES §0 |
| Los 93, uno a uno, en `syms.txt` + `syms_com.apple.iokit.IOSkywalkFamily.txt` | **93/93 con tipo `0f`** |
| Los 13 `extern` `__asm` de `AWDLInterface.cpp` contra los volcados | 13/13 con ortografía exacta y tipo `0f` |
| Auditoría de los 543 indefinidos que contienen `IO80211`/`IOSkywalk` | **0 sin `0f`** |
| `__ZTI*`/`__ZTV*` indefinidos | 0 (el build va con `-fno-rtti`; no hay referencias sin resolver de RTTI) |
| Desensamblado de `free`/`stop`/`startWithRegistration` del objeto | véase §3.1 y §2.2 |

Los símbolos Skywalk no están en `syms.txt` (fileset del kernel): ahí
`registerEthernetInterface` aparece con tipo `01` y dirección nula (referencia indefinida del
kernel). Todos están `0f` en el volcado del fileset `IOSkywalkFamily`; las direcciones coinciden
con las citadas en spec §5.1 y CODE_FIXES (p. ej. `initRegistrationInfo` `0xffffff8002b108e2`,
`registerEthernetInterface` `0xffffff8002b10a84`, `withPool` de TX submission
`0xffffff8002b0ca86`).

## 1. Lo revisado sin defecto

Para acotar la tabla final, esto es lo contrastado que **no** produce hallazgo:

- **Nombres mangleados de los `extern`** (`AWDLInterface.cpp:27-61`): los 13 casan carácter a
  carácter. Dos sobrecargas vecinas se evitan correctamente: `enqueuePackets` existe como
  `EPKP15IOSkywalkPacketjj` (`0xffffff8002b2dbb6`, la usada) y como `EP15IOSkywalkPacketjj`
  (`0xffffff8002b2dc6e`, no usada); `registerEthernetInterface` existe como
  `PP20IOSkywalkPacketQueue` (usada) y como `P20IOSkywalkLogicalLink` (no usada). La acción de
  `withPool` TX submission es `unsigned int (*)(OSObject*, IOSkywalkTxSubmissionQueue*,
  IOSkywalkPacket* const*, unsigned int, void*)` según el mangling — la firma de `txNoop` casa.
- **Vtable**: 50 overrides (`:96-145` + `free`/`stop`), 27 métodos (`0xE68`-`0xF38`),
  `setMacAddress` puro (`0xF40`), 16 reservados (`0xF48`-`0xFC0`), 63 slots AWDL
  (`0xFC8`-`0x11B8`). Recuentos correctos; los 63 stubs de `RTLAwdInterface` cubren los 63 slots y
  `static_assert(!__is_abstract(RTLAwdInterface))` (`:406`) fallaría si faltase uno. Los nombres de
  los 27 + 16 reservados están verificados contra el volcado con sus tipos reales, incluidos
  `12setLinkStateE16IO80211LinkStatej` (el enum, como en `:150`), los dos overloads de
  `p2pMulticastTx` en el orden de `:175-176` y la ortografía `33_/34__RESERVED…` de los reservados.
- **Mecanismo de las llamadas al padre**: el objeto no referencia `IO80211VirtualInterface::free`
  ni `::stop`. Con `-fapple-kext`, la llamada cualificada al padre se emite como carga de la celda
  de la vtable exportada de la familia y llamada indirecta: en `RTLAwdInterface::free`,
  `callq *(ZTV de IO80211VirtualInterface + 0xA0)` — la celda de `IOService::free`, que contiene la
  implementación de la familia. No hay despacho dinámico (descartada la recursión infinita) ni
  símbolo sin resolver. El mecanismo es correcto en ambos casos (la familia sobrescriba `free` o
  herede `IOService::free`).
- **Metaclase**: `10gMetaClassE`, `10superClassE`, `9MetaClassC1/C2`, `C2EPK11OSMetaClass`,
  `D0/D1/D2`, `nwEm`, `dlEPvm` y `__ZTV19IO80211AWDLProtocol` — todos `0f` (spec §5.1).
- **Offsets del `RegInfo`** (`:78-89`, `:181-191`): todos según spec §5.3 — `+0x0C`=3,
  `+0x10`=6, `+0x14`=2, `+0x24`/`+0x28`=0x80, `+0x30`=«awdl» (literal con almacenamiento
  permanente), `+0x38`=0, **`+0x3C`=64 u16** (defecto 6 aplicado; el comentario «+0x38 is the BSD
  unit» es correcto), `+0x40`=0, MTU `0x594` en `+0x48` y `+0x4C` (la escritura u64
  `0x59400000594` es correcta en little-endian), MAC 6 B en `+0x108`, tamaño `0x130`.
- **`txNoop`** (`:247-261`): lotes de 64 < capacidad 128, retorno `(IOReturn)count` (consumo total;
  0 pararía la cola según `legacyDequeue`), en éxito los paquetes los completa
  `enqueuePackets` → `completePackets` hacia el pool; en fallo libera el lote con
  `deallocatePacket` sobre el pool correcto (`_txPool`, el mismo de la cola TX submission y de la
  cola TX completion). Es literalmente CODE_FIXES §1.2, y estrictamente mejor que el camino STA
  (`RTLAirLayer.cpp:433-435`), que ignora el retorno de `enqueuePackets`.
- **Rollback y ciclo de vida**: `teardownDatapath` en los dos fallos posteriores a
  `createDatapath` (defecto 2 aplicado, `:201` y `:213`); `removeQueues` es idempotente
  (deshabilita → saca del work loop → libera → pone a nulo, saltando nulos, `:223-239`), de modo
  que `stop()`/`free()` posteriores no repiten trabajo ni doble-liberan; los fallos de
  `createDatapath` previos a `addEventSource` dejan las colas fuera del work loop y `free()`
  (`:278-290`) las libera sin colgantes; `_regInfo` se libera con `IOFree` del mismo tamaño del
  `IOMalloc` y se pone a nulo. No se ha encontrado ningún camino con fuga ni doble liberación,
  asumiendo el rollback del handler (`terminate()` + `release()` en el parche, presente).
- **Estado compartido**: los punteros de colas se escriben antes de `addEventSource` y luego sólo
  se tocan en el work loop (`txNoop`, `removeQueues` vía `runAction`) o cuando ya no pueden estar
  en él (fallos previos, `stop`/`free` tras terminación). El gate del work loop es recursivo
  (cabecera del SDK, `IOWorkLoop.h:301`), por lo que `runAction` desde el hilo del work loop no
  se auto-bloquea; la acción esperada es un puntero a función
  `IOReturn (*)(OSObject*, void*, void*, void*, void*)` (`IOWorkLoop.h:78`) — la firma de
  `removeQueues` casa. `_bsdName` se escribe bajo `_awdlLock` (parche) y se lee bajo el mismo
  lock. `enable`/`disable` (`:294-304`) no tocan estado, como pide spec §5.6.
- **Secuencia de arranque** (`:96-218`): pasos 1-10 de spec §5.5 completos y en orden (`init()` →
  `VI::init(ctrl, ea, 6, "awdl")` → `setInterfaceId(2)` → `attach` → `VI::start` → RegInfo →
  pool+3 colas → `registerEthernetInterface` → `registerService(kIOServiceSynchronous)` →
  `setRunningState(false)` → `getBSDName` con fallo si vacío). El guard `!name || !name[0]` antes
  del `strlcpy` (`:211-216`) es una mejora sobre el esqueleto §12.2, que copiaría
  `getBSDName()` sin comprobar y haría `strlcpy(dst, NULL)` si el nombre no existiera.
- **`LOG`** (`:23-24`): correcto. `airLog` añade él mismo el prefijo `RTL8814AirLayer: ` y el
  `\n` (`RTLAirLayer.cpp:38`); el esqueleto del spec (`"RTL8814AirLayer: " fmt "\n"`) habría
  duplicado el prefijo. El `rtlAirLog` del parche está en el árbol (`RTLAirLayer.cpp:61-70`).
- **Payload 94 y asserts**: `apple80211_virt_if_create_data` de 32 B con `static_assert`
  (`:74-81`), layout de spec §4.1; asserts de tamaños `0x128`/`0x130`/`0x130` (`:287-289`).
- **`setMacAddress`** (`:306`) acepta sin cambiar la MAC y los 63 stubs devuelven
  `kIOReturnUnsupported` (102) — exactamente spec §5.6 y §16 (D6 incluido: la companion 293
  responde 102).

## 2. Hallazgos

### 2.1 `rtl_vif_start` declarado `uintptr_t`; la función real devuelve `bool`

`Source/AirLayer/AWDLInterface.cpp:54-55`, llamada en `:168`. Gravedad: **comportamiento
incorrecto (potencial, `?` en la probabilidad práctica)**.

`IO80211VirtualInterface::start` es un override de `IOService::start`, que es `virtual bool`; la
propia cabecera lo declara `virtual bool start(IOService*) override;` (`AWDLInterface.h:98`). El
tipo de retorno no forma parte del mangling, así que el símbolo enlaza igual, pero el ABI sólo
garantiza el bit `al` para un retorno bool; el resto de `rax` queda sin especificar. El objeto
compila la comprobación como test de los 64 bits:

```asm
000000000000090e	callq	__ZN23IO80211VirtualInterface5startEP9IOService
0000000000000913	cmpq	$0x0, %rax
0000000000000917	jne	0x933
```

Si alguna rama de la implementación de la familia dejara basura en los bits altos de `rax`, el
arranque fallaría de forma espuria (`awdl: base start failed`, el 94 responde `0xE00002BC` y no
hay `awdl0`), sin pánico. La nota del esqueleto §12.1 («se usa `uintptr_t`… no se llaman desde el
kext en la etapa 1») no cubre este caso: este `extern` **sí** se llama, y es el único del fichero
en esa situación junto a `rtl_vif_init` (que sí está bien declarado `bool`, defecto 3 aplicado).

Antes (`:54-55`):

```cpp
uintptr_t rtl_vif_start(void *self, IOService *provider)
	__asm("__ZN23IO80211VirtualInterface5startEP9IOService");
```

Después:

```cpp
bool rtl_vif_start(void *self, IOService *provider)
	__asm("__ZN23IO80211VirtualInterface5startEP9IOService");
```

El cambio no altera el símbolo ni la convención; `:168` no necesita edición. La probabilidad
práctica es baja (`?`: las implementaciones de la familia suelen devolver bool con `rax`
cero-extendido, pero no es verificable sin IDA), y el fallo sería seguro (sin `awdl0`, sin
corrupción).

### 2.2 Falta `deregisterEthernetInterface`/`detach` en el fallo de nombre BSD

`Source/AirLayer/AWDLInterface.cpp:210-215`. Gravedad: **comportamiento incorrecto, sólo en
camino de error**; desviación conocida de la spec §11 (CODE_FIXES §2.2 la deja fuera con `?`).

A estas alturas `registerService(kIOServiceSynchronous)` ya terminó y, si llegó aquí, el registro
 ethernet pudo haber sembrado estado en la familia (colas y pools entregados en el paso 8). El
 rollback del código es `teardownDatapath()` —quita las colas del work loop y las libera— pero no
 desregistra la interfaz ethernet ni hace `detach`; la spec §11 pide «quitar colas de la work
 queue, `deregisterEthernetInterface` (si llegó a registrarse), detach, release». El handler hace
 `terminate()` + `release()`, que disparará `stop()` (idempotente) y `free()`, pero `?` no está
 verificado que la terminación deshaga por sí sola el registro ethernet ni que suelte las
 referencias internas de la familia a colas ya liberadas: si la familia las retiene sin un
 `deregister`, el objeto muere con punteros colgantes en el estado registrado.

Antes (`:211-215`):

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		teardownDatapath();
		return false;
	}
```

Después (forma mínima; `?` el argumento de `deregisterEthernetInterface` —la unidad BSD— no está
fijado en los documentos, y el símbolo `0f` está en `0xffffff8002b10af4`):

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		teardownDatapath();
		/* deregisterEthernetInterface(unit) si el registro llegó a hacerse */
		return false;
	}
```

CODE_FIXES §8 ya lista este punto como abierto; se mantiene como hallazgo porque es la única
desviación real del rollback de §11 que queda en el fichero. En la práctica el camino exige un
fallo extraño (registro exitoso síncrono con nombre vacío), por lo que no bloquea.

### 2.3 `rtl_sky_initRegInfo` declarado `void`; el esqueleto lo declara `bool` y §11 espera rollback

`Source/AirLayer/AWDLInterface.cpp:28-29`, llamada en `:180`. Gravedad: **cosmético** (con `?` en
el tipo real).

El esqueleto §12.1 declara `bool rtl_sky_initRegInfo(...)`; el código lo declara `void` y comenta
que el camino de referencia tampoco mira el resultado. El tipo real no se decide desde los
volcados (`?` el tipo de retorno no va en el mangling). Declarar `void` y no leer el retorno es
seguro en cualquier caso (el problema sería el inverso: declarar `bool` sobre una función `void`
y leerlo). Spec §11 tiene una fila de rollback para «`initRegistrationInfo` falla» que el código
—y el propio esqueleto §12.2— no implementan: los valores pasados (1, 0x130) son los válidos
según §5.3, así que el fallo no es esperado.

Antes (`:28`):

```cpp
void rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
```

Después (alineación con el esqueleto; sin cambio de comportamiento mientras el resultado no se
lea):

```cpp
bool rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
```

### 2.4 `setAwdlInterface`/`rtlAwdlPublish` permanecen (defecto 4 no aplicado)

`Source/AirLayer/AWDLInterface.cpp:56-57` y `:308-311`; `AWDLInterface.h:408-410`. Gravedad:
**cosmético**.

CODE_FIXES §4 pide eliminarlos (0 referencias en el kernelcache; el 97 lee
`getPrimarySkywalkInterface`, no `getAwdlInterface`) y spec §7.2 retiró el requisito. La
decisión documentada en el handoff es no aplicar el defecto 4 («inocuo»), de modo que no es un
defecto nuevo sino una desviación conocida y deliberada de la spec vigente. Nota menor añadida:
el retorno está declarado `IOReturn` y el tipo real del método de la familia no consta en los
volcados (`?`); el parche ignora el valor devuelto en sus dos llamadas, así que un retorno real
`void` sería inofensivo.

### 2.5 Literal de log de registro distinto del esperado por T3.4

`Source/AirLayer/AWDLInterface.cpp:199`. Gravedad: **cosmético**.

El código imprime `awdl: registerEthernetInterface 0x%x`; la prueba T3.4 (spec §14) espera el
literal `awdl: registerEthernetInterface = 0x0` (con `=`, del esqueleto §12.2). Un `log show`
filtrado por la cadena exacta de T3.4 no encontraría la línea. Basta unificar una de las dos
partes.

Antes (`:199`):

```cpp
	LOG("awdl: registerEthernetInterface 0x%x", r);
```

Después:

```cpp
	LOG("awdl: registerEthernetInterface = 0x%x", r);
```

### 2.6 Ramas defensivas de `txNoop` que devuelven 0 dejan la cola parada

`Source/AirLayer/AWDLInterface.cpp:251-252`. Gravedad: **cosmético** (rama inalcanzable).

```cpp
	if (!me || !count || !me->_txCompQ)
		return 0;
```

Según `legacyDequeue` (CODE_FIXES §1.1), devolver 0 pone el bit de parada y deja el lote en el
anillo sin avanzar: los paquetes no se completan nunca y la cola queda estancada. Las tres
condiciones son inalcanzables en el flujo actual: `owner` es siempre `this` (el que se pasó a
`withPool`), la acción sólo se invoca con paquetes pendientes (`count > 0`) y `_txCompQ` sólo se
pone a nulo en `removeQueues` **después** de deshabilitar, sacar y liberar `_txSubQ` — la acción
no puede dispararse en ese estado. Es además el texto literal de CODE_FIXES §1.2, así que no hay
desviación de la spec; se documenta para que una futura edición de la acción no herede el
patrón sin verlo. Sin cambio propuesto (devolver `count` en esas ramas sería peor: claimaría
consumir un lote que no se completó).

### 2.7 `initWithController` sin guard de nulos; ivar `_ctrl` sin lector

`Source/AirLayer/AWDLInterface.cpp:96-103`. Gravedad: **cosmético**.

`initWithController(RTLAirController *ctrl, const uint8_t mac[6])` hace `memcpy(_mac, mac, 6)`
(`:103`) y usa `ctrl` en `rtl_vif_init`/`attach` sin comprobar nulos. El único llamador es el
handler del 94, que ya validó `req` y pasa `this`, de modo que el guard sería defensivo. El ivar
`_ctrl` (`:389`) se asigna y nadie lo lee en este fichero: o se elimina o queda como ancla para
la etapa 2. Sin cambio propuesto.

### 2.8 Supuesto all-or-nothing de `enqueuePackets` en el fallo de lote

`Source/AirLayer/AWDLInterface.cpp:256-258`. Gravedad: **ninguna evidenciada; verificación
pendiente (`?`)**.

La rama de fallo libera el lote completo al pool:

```cpp
		if (rtl_txcomp_enqueue(me->_txCompQ, pkts + done, n, 0) != kIOReturnSuccess)
			for (uint32_t i = 0; i < n; i++)
				rtl_pool_free(me->_txPool, pkts[done + i]);
```

La evidencia de CODE_FIXES §1.1 describe un `enqueuePackets` que valida `0 < count < capacidad`
antes de tocar nada y, en éxito, copia el lote y lo completa bajo su gate — es decir,
todo-o-nada. Si existiera un fallo parcial (acepta una parte y devuelve error), la liberación
devolvería al pool paquetes ya completados y corrompería el recuento del pool. Con los lotes de
64 < 128 la validación siempre pasa, así que los fallos documentados (cola deshabilitada) son
de rechazo completo. Marca `?`: reverificar en el desensamblado si algún día se reutiliza la
acción con lotes cerca de la capacidad.

## 3. Puntos pendientes que la revisión no puede cerrar sin IDA

- **Orden de celdas de la vtable por encima de `0xE60`** (`?`, potencialmente «bloquea el
  arranque» si está mal, no verificado): el objeto verifica **nombres** (93/93 `0f`) pero no el
  **orden** de los 27 métodos (`0xE68`-`0xF38`), `setMacAddress` (`0xF40`), los 16 reservados y
  los 63 AWDL. Si el orden real de la familia difiriera, las celdas de la vtable de
  `RTLAwdInterface` caerían desplazadas y las llamadas de la familia aterrizarían en el método
  equivocado. Es exactamente la comprobación P0.3 de spec §14 (§5.2, §16), todavía sin ejecutar:
  no existe `vt_IO80211VirtualInterface.txt` ni `vt_IO80211AWDLProtocol.txt` en
  `Docs/re/io80211_25.6.0/`. Matices que acotan el riesgo: los 50 overrides están blindados por
  `override` en compilación; y una permutación interna de los 63 stubs AWDL sería neutra en
  etapa 1 (los 63 devuelven el mismo 102), aunque un desplazamiento del tramo `0xE68`-`0xFC0`
  no lo sería. Ejecutar P0.3 sigue siendo condición previa a cargar el kext.
- **Tolerancia de `IO80211VirtualInterface::free` a un `init` base a medias** (`?`, CODE_FIXES
  §3.2): aplica al fallo de `attach` en `:119-122`.
- **Lector de `+0x3C`** (`?`, CODE_FIXES §6.1): la escritura 64 u16 en `+0x3C` sigue el camino de
  referencia `createProxInterface`; el consumidor no está identificado.
- **Vía `checkForWork` de la cola TX completion** (`?`, CODE_FIXES §1.2): sin ruta conocida que
  deje pendientes; el no-op completa dentro de `enqueuePackets`.
- **Retención de la familia tras el registro** (`?`): condiciona la gravedad real de §2.2.
- **Retorno real de `setAwdlInterface` y de `initRegistrationInfo`** (`?`): no alteran el
  comportamiento actual (§2.3, §2.4).

Observación fuera del alcance de estos dos ficheros: `build_airlayer.sh:40-41` compila
`AWDLInterface.cpp` dos veces (línea duplicada en la integración); es inofensivo (el segundo
objeto sobreescribe al primero) pero conviene limpiarlo en su flujo.

## 4. Tabla final

| # | Hallazgo | Dónde | Gravedad | Acción |
|---:|---|---|---|---|
| 1 | `rtl_vif_start` `uintptr_t` sobre retorno `bool`; el objeto testa los 64 bits de `rax` (`cmpq $0x0, %rax`) | `AWDLInterface.cpp:54-55`, `:168` | comportamiento incorrecto (potencial, `?`) | declarar `bool` (1 línea) |
| 2 | Sin `deregisterEthernetInterface`/`detach` en el fallo de nombre; rollback incompleto frente a spec §11 | `AWDLInterface.cpp:210-215` | comportamiento incorrecto (sólo camino de error; documentado en CODE_FIXES §2.2) | añadir el desregistro; `?` argumento unidad sin fijar |
| 3 | Orden de celdas tras `0xE60` sin contrastar con la vtable real (P0.3 pendiente; sin volcado `vt_` de las clases) | `AWDLInterface.h:149-284` | bloquea el arranque **si** está mal (`?` no verificado) | ejecutar P0.3 antes de cargar |
| 4 | `rtl_sky_initRegInfo` `void` frente al `bool` del esqueleto; resultado ignorado | `AWDLInterface.cpp:28-29`, `:180` | cosmético | alinear al esqueleto (opcional) |
| 5 | `setAwdlInterface`/`rtlAwdlPublish` retenidos contra CODE_FIXES §4 / spec §7.2 | `AWDLInterface.cpp:56-57`, `:308-311`; `.h:408-410` | cosmético (decisión documentada) | sin acción |
| 6 | Log de registro sin el `=` que T3.4 espera | `AWDLInterface.cpp:199` | cosmético | unificar literal |
| 7 | Ramas defensivas de `txNoop` con `return 0` (estancarían la cola; inalcanzables) | `AWDLInterface.cpp:251-252` | cosmético | sin acción (documentado) |
| 8 | Sin guards de nulos en `initWithController`; `_ctrl` sin lector | `AWDLInterface.cpp:96-103`, `.h:389` | cosmético | opcional |
| 9 | All-or-nothing de `enqueuePackets` asumido en la liberación del lote fallido | `AWDLInterface.cpp:256-258` | verificación `?` (hoy: sin evidencia de fallo parcial) | reverificar si cambia el uso |

Ningún hallazgo confirmado bloquea el arranque con el árbol actual; el #3 es la condición
pendiente que podría hacerlo, y el #1 es el único defecto de código activo con efecto potencial
en el flujo normal.
