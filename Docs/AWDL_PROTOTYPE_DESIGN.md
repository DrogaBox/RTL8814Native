# Diseño mínimo de prototipo AWDL role 6 para RTL8814AirLayer

## Alcance y criterio de evidencia

Análisis estático del BootKernelExtensions.kc de macOS 25.6.0 y los artefactos conservados en `Build/ida_awdl/` y `Docs/re/io80211_25.6.0/`, más lectura de los headers y del código local de la capa. No se hizo build ni se tocó código. Dirección = VA del KC / kext tal como aparece en las tablas. Uso “símbolo global (N_EXT)” para lo que aparece como tipo `0f` en el dump; “local/no exportado” para `0e`; no localizado = ausencia en los dumps preservados, no afirmación universal de que macOS no tenga otro mecanismo o alias. Un símbolo global del KC no es una API estable ni garantiza que una kext de tercero pueda enlazar/usar el ABI con seguridad.

La secuencia de `Apple80211RawSet/RawGet` que sigue a `VIRTUAL_IF_CREATE` continúa **PENDIENTE de verificar en IDA** (xrefs desde la sesión interactiva del usuario); no se usa como premisa para este diseño. No se buscó ni se volvió a llamar al MCP de IDA después de la instrucción de detenerlo.

## 1. Lo que el código actual de la capa ya prueba

`RTL8814AirLayer` es una kext separada. `Source/AirLayer/RTLAirLayer.hpp:44-57` define `RTLAirInterfaceStubs : IO80211InfraProtocol` y `RTLAirInterface : RTLAirInterfaceStubs`; el controlador deriva de los stubs de `IO80211Controller` (la parte inferior del header). `Source/AirLayer/Info.plist:54-72` declara dependencias `com.apple.iokit.IO80211Family` y `com.apple.iokit.IOSkywalkFamily`. `build_airlayer.sh:20-29` compila con `-fapple-kext -fno-rtti -fno-exceptions`, contra los headers locales; el link con `ld -kext` está en líneas 45-60. No hay una dependencia de `AppleBCMWLANCore`.

El camino existente de STA es evidencia útil, no prueba directa de AWDL: `RTLAirInterface::initWithController` llama `init()` y `IO80211SkywalkInterface::init(ctrl, &mac)`, asigna role/ID y hace `attach(ctrl)` (`Source/AirLayer/RTLAirLayer.cpp:313-332`). `startWithRegistration` primero llama `IO80211InfraInterface::start(ctrl)` (`:334-342`), crea pools y queues y usa `IO80211InfraInterface::registerInfraEthernetInterface` (`:360-385`). El inicializador de registro está enlazado por nombre asm a `IOSkywalkEthernetInterface::initRegistrationInfo` (`:124-127`). El controller hace `attachInterface(_iface, this)`, prepara registration info y llama `startWithRegistration` (`:1489-1514`). `Tools/re/check_io80211.sh` contrasta vtables y símbolos de imports contra esos dumps; no comprueba AWDL.

## 2. Inventario de enlaces relevantes

### IO80211Family

Las siguientes filas provienen de `Docs/re/io80211_25.6.0/syms.txt`; la dirección y el estado `0f`/`0e` se conservan. La vtable se extrae además en `Build/ida_awdl/awdl_vtable_probe.log`.

| Clase / función necesaria | Dirección y estado en dump | Declarada en headers de la capa | Lectura |
|---|---|---|---|
| `IO80211AWDLProtocol::C2(OSMetaClass const*)` | `0xffffff8002198c04`, `0f` global | No | Constructor visible global; no se vio `C1`, así que marcar ese overload como no localizado. |
| `IO80211AWDLProtocol::gMetaClass` | `0xffffff800248cde8`, `0f` | No | Global visible; permite que `OSDeclare*`/registro de metaclases pudiera referenciar el objeto si el linker lo resuelve. |
| `IO80211AWDLProtocol::MetaClass::C1/C2` | `0xffffff8002198bc8` / `0xffffff8002198c3c`, `0f` | No | Constructores de metaclass visibles. |
| `IO80211AWDLProtocol` vtable `__ZTV` | `0xffffff8002456440`, `0f` | No | Símbolo visible. En `Build/ida_awdl/class_meta_probe.log:47-57`, el metaclass apunta como padre a `IO80211VirtualInterface`; el constructor de metaclass llama a `OSMetaClass("IO80211AWDLProtocol", &IO80211VirtualInterface::gMetaClass, 0x130)` (`awdl_types_probe.log:52-65`). |
| `IO80211VirtualInterface::C2(OSMetaClass const*)` | `0xffffff800233003e`, `0f` | No | Visible global; el overload `C1` no se encuentra en la lista exportada. |
| `IO80211VirtualInterface::gMetaClass` | `0xffffff800248e478`, `0f` | No | Visible global. Metaclass constructor `0xffffff8002330076`; vtable `__ZTV` en `0xffffff8002472b78`. |
| `IO80211VirtualInterface::init(IO80211Controller*, ether_addr*, unsigned int, char const*)` | `0xffffff80023305f8`, `0f` | No | Visible global. El pseudocódigo conservado llama a `IO80211SkywalkInterface::init`, asigna role, crea locks/estado y abre al controller (`awdl_types_probe.log:88-189`). |
| `IO80211InfraProtocol::C2(OSMetaClass const*)` | `0xffffff80022ca768`, `0f` | Sí, declarado en `Source/IO80211/IO80211_25_6_0.h:595` | Constructor global visible; vtable `0xffffff80024656a0`, `gMetaClass=0xffffff800248dbd0`; clase base en header. |
| `IO80211SkywalkInterface::C1/C2(OSMetaClass const*)` | `0xffffff80022f513c` / `0xffffff80022f511c`, `0f` | Sí, clase declarada `Source/IO80211/IO80211_25_6_0.h:386` | Vtable `0xffffff800246af00`, `gMetaClass=0xffffff800248dfa0`; header local modela clase y size `0x128`. |
| `IO80211SkywalkInterface::init(IOService*, ether_addr*)` | `0xffffff80022f56e2`, `0f` | Sí, `IO80211_25_6_0.h:475` | Ya usada por `RTLAirInterface::initWithController` (`RTLAirLayer.cpp:320`). |
| `IO80211SkywalkInterface::start(IOService*)` | `0xffffff80022f5792`, `0f` | Sí, `IO80211_25_6_0.h:393` | Ya llamado desde el `IO80211InfraInterface::start` base en la implementación local. |
| `IO80211InfraInterface::registerInfraEthernetInterface(...)` | `0xffffff80023683e0`, `0f` | No está declarado como método del header; import asm manual en `RTLAirLayer.cpp:128-130` | Método global visible y ya enlazado/usado por la capa. |
| `IO80211Controller::setAwdlInterface(IO80211VirtualInterface*)` | `0xffffff8002299c24`, `0f` | No | Implementación global, setter de estado de controller; `Build/ida_awdl/kernel.log:1889-1900` muestra que almacena el puntero en `controller-private + 400`. No prueba que esta función por sí sola registre una interfaz o la haga aceptable. |
| `IO80211Controller::getAwdlInterface()` | `0xffffff8002293256`, `0f` | No | Getter global asociado; no hay declaración local recuperada. |
| `IO80211Controller::createVirtualInterface(...)` | dirección `0xffffff800229612e` en `Build/ida_awdl/attach2.log`; símbolo global a confirmar con el inventario exacto | Declaración local en `IO80211_25_6_0.h:868` | El pseudocódigo genérico sólo crea role 7; no es fábrica role 6. |
| `IO80211AWDLProtocol::MetaClass::alloc()` | `0xffffff8002198c6e`, `0f` | No | Visible, pero allocate devuelve la clase AWDL base abstracta/incompleta, no el hijo Broadcom. |
| `IO80211VirtualInterface::MetaClass::alloc()` | `0xffffff80023300a8`, `0f` | No | Visible. Usar `OSObject::allocClassWithName`/metaClass es teóricamente posible si se registran correctamente metaclasses y dependencias, no es una recomendación de instanciar un objeto Apple base directamente. |

**Pure slots:** el vtable de `IO80211AWDLProtocol` (base subyacente de la vtable del mismo nombre; direcciones en `Build/ida_awdl/awdl_vtable_probe.log:47+`) tiene `___cxa_pure_virtual` en slot 490 y slots 507–569; el probe de clases (`Build/ida_awdl/class_meta_probe.log:67-137`) compara la vtable AWDL con `IO80211VirtualInterface` y muestra 63 diferencias puras consecutivas en esos slots. Esos slots deben tener implementación concreta antes de crear una subclase instanciable. La evidencia no trae una tabla nominal de firmas para los slots 507–569 de `IO80211AWDLProtocol`; **no invento sus nombres**. Por separado, `IO80211VirtualInterface` mantiene slot 490 puro (`awdl_vtable_probe.log:1071-1579`, en torno a entrada 490), aunque la clase de interfaz virtual ya implementa la mayoría de su interfaz.

### IOSkywalkFamily

Las filas provienen de `Docs/re/io80211_25.6.0/syms_com.apple.iokit.IOSkywalkFamily.txt`:

| Símbolo | Dirección y estado | Declarado localmente | Resultado |
|---|---|---|---|
| `IOSkywalkEthernetInterface::C1/C2(OSMetaClass const*)` | `0xffffff8002b1069c` / `0xffffff8002b1066c`, `0f` | Sí, clase en `IO80211_25_6_0.h:342` | Constructor visible global. |
| `IOSkywalkEthernetInterface::gMetaClass` | `0xffffff8002b44ba0`, `0f` | No se referencia directamente en el header local | Visible global. |
| `IOSkywalkEthernetInterface` vtable `__ZTV` | `0xffffff8002b3f178`, `0f` | Sí, aproximada en header | Visible; vtable probe guardado. |
| `init(OSDictionary*)` | `0xffffff8002b10820`, `0f` | Sí, local header:346 | Método visible. |
| `initRegistrationInfo(RegistrationInfo*, uint32_t, unsigned long)` | `0xffffff8002b108e2`, `0f` | No como método; la capa lo declara vía asm en `RTLAirLayer.cpp:124-127` | Enlace usado al construir registration info en `RTLAirLayer.cpp:1504`. |
| `registerEthernetInterface(... queues ..., pools ..., flags)` (overload de queues) | `0xffffff8002b10a84`, `0f` | No; el método local usa el helper Infra, no este overload | Visible global. |
| `registerEthernetInterface(... logical link ..., pools ..., flags)` | `0xffffff8002b10b46`, `0f` | No | Visible global; no confundir con el overload que ya invoca la AirLayer. |
| `deregisterEthernetInterface(unsigned int)` | `0xffffff8002b10af4`, `0f` | No | Visible global para rollback/stop. |

`IOSkywalkEthernetInterface` pertenece a IOSkywalkFamily. Por eso `initRegistrationInfo` y los dos overloads de `registerEthernetInterface` se resuelven bajo esa dependencia, mientras que `IO80211InfraInterface::registerInfraEthernetInterface` y `IO80211Controller::setAwdlInterface` pertenecen a IO80211Family. Los dumps muestran las direcciones y clase de símbolos, no los prototipos ABI enteros ni garantía de compatibilidad entre versiones.

### No localizados como exports públicos en los dumps

- `AppleBCMWLANProximityInterface` constructor/metaclass/vtable son del driver Broadcom (las referencias están en `syms_bcm.txt:1302-1309`, vtable referida en `Docs/AWDL_ROLE6_RE.md`, §2). No hay evidencia de que estén en la lista de exportaciones de IO80211Family o IOSkywalkFamily; la AirLayer tampoco declara dependencia sobre un bundle Broadcom.
- `IO80211AWDLProtocol` no está declarado en `Source/IO80211/IO80211_25_6_0.h`; `IO80211VirtualInterface` tampoco. El header sólo avanza clases y declara `IO80211SkywalkInterface`, `IO80211InfraInterface`, `IO80211InfraProtocol` y controller. Eso crea un hueco de ABI/layout y de vtable para ambas clases AWDL.
- Las tablas `syms.txt`/`syms_com.apple.iokit.IOSkywalkFamily.txt` son inventarios extraídos de KC (ver la estructura de type `0f` en `Tools/re/kcsyms.py` y el criterio de validación de `Tools/re/check_io80211.sh`). Que una fila sea `0f` acredita un símbolo global visible en ese dump; no demuestra una API contractual, una export table equivalente a un SDK público o que el linker del kextloader acepte todos los usos.

## 3. ¿Se puede heredar `IO80211AWDLProtocol` desde una kext de tercero?

### Qué apoya la posibilidad de enlace

1. `Source/AirLayer/Info.plist:54-62` declara explícitamente las dependencias sobre `IO80211Family` e `IOSkywalkFamily`. En el modelo de kexts, los imports indefinidos del bundle se resuelven contra símbolos de los bundles dependientes y KPI disponibles. El precedente real es la capa que enlaza `IO80211InfraProtocol`/`IO80211SkywalkInterface` más `initRegistrationInfo` y `registerInfraEthernetInterface`; `Tools/re/check_io80211.sh` compara sus imports relevantes con los símbolos `0f` del KC.
2. `IO80211AWDLProtocol::C2`, `gMetaClass`, metaclass constructors/alloc y vtable aparecen como globals (`0f`) en IO80211Family (`syms.txt`, filas/direcciones de §2). El constructor `OSMetaClass` usa explícitamente `IO80211VirtualInterface::gMetaClass` como parent y `sizeof = 0x130` (`Build/ida_awdl/awdl_types_probe.log:59-65`). `IO80211VirtualInterface::gMetaClass`, ctor `C2` y vtable también están en el mismo dump.
3. IOKit registra las clases de kext por `OSMetaClass`; para un subclase se declara la metaclass derivada con el padre (`OSDeclareDefaultStructors`/`OSDefineMetaClassAndStructors`) y el loader resuelve los símbolos importados usando dependencias de bundle. **Eso es la mecánica general inferida de las macros/código local y de la cadena `OSMetaClass` que se ve en IDA; no está verificado el resultado de un nuevo link para AWDL.** Las dependencias actuales cubren a ambas familias, así que la declaración de dependencias no es el obstáculo evidente.

### Qué impide concluir “viable” ahora

- El mayor bloqueo no es un símbolo faltante demostrado: son las 63 entradas `pure_virtual` del vtable de `IO80211AWDLProtocol` más el slot 490 de `IO80211VirtualInterface`. Una clase derivada con `OSDeclareDefaultStructors` no puede hacerse concreta sin las implementaciones requeridas; los nombres y firmas de gran parte de esos 63 slots no están decodificados en las cabeceras. Una instancia con un slot puro alcanzado puede llamar `__cxa_pure_virtual` y abortar o panicar.
- En `Source/IO80211/IO80211_25_6_0.h` no están declarados los dos layouts ni sus vtables. El header sí contiene los layouts de IOSkywalk/Infra usados por la clase que ya funciona. Copiar una definición parcial de `IO80211AWDLProtocol` con slots omitidos/campos incorrectos puede desalinear la vtable/ivars, aunque el símbolo base enlace.
- No están recuperadas las firmas exactas ni el convenio contractual para añadir `IO80211VirtualInterface` a `IO80211Controller::setAwdlInterface`, conectarlo al peer manager y aceptar sus ioctls. La función setter es visible (`0xffffff8002299c24`) y almacena sólo un puntero; esa escritura no prueba el lifecycle.
- Los símbolos `registerEthernetInterface` sí existen en IOSkywalkFamily; `registerInfraEthernetInterface` existe en IO80211Family. No se conoce una API pública `setAwdlInterface` de interfaz (sólo el método de controller). La secuencia de registro correcta para una clase AWDL distinta no está probada fuera del código Broadcom.

**Respuesta acotada:** a nivel de enlace, los exports observados y las kmod dependencies existentes hacen plausible que se puedan importar las metaclases/constructores base AWDL; no hay evidencia de que falte el símbolo global `IO80211AWDLProtocol::gMetaClass`. Sin embargo, **no se demuestra que sea posible crear un subclass concreto, enlazable y compatible desde la kext externa actual**: se desconocen numerosas firmas puras, layouts y partes del contrato de registro. El código privado Broadcom no es matemáticamente indispensable para toda implementación futura, pero sí es la única implementación de role 6 observada y proporciona las piezas que la evidencia pública no resuelve.

## 4. Diseño de prototipo mínimo (propuesta; no implementar todavía)

### Punto de partida prudente

Hacer primero una compilación de *enlace* separada y desechable del header generado para un nuevo wrapper abstracto que herede `IO80211AWDLProtocol` o, de forma más conservadora, `IO80211VirtualInterface`; que el linker resuelva sólo imports explícitos, sin crear/register/start el objeto. Antes de escribirlo, recuperar las firmas/nombres de slots puros de los dumps y completar/verificar layout/vtable en comparación numérica. No añadir una implementación que finja “éxito” en `setVIRTUAL_IF_CREATE` mientras su interfaz no se pueda inicializar y registrar.

El `IO80211AWDLProtocol` base promete la forma nominal correcta para el protocolo AWDL, pero es la alternativa de mayor trabajo: derive `RTLAirAwdlInterfaceStubs : IO80211AWDLProtocol` que implementaría **todas** las funciones puras heredadas, no sólo handlers apple80211. El prototipo conservador alternativo deriva `RTLAirVirtualInterfaceStubs : IO80211VirtualInterface`, implementa slot 490 y las operaciones AWDL necesarias, pero **no verificado** que IO80211Family/`wifip2pd` acepte un objeto con esa clase nominal en vez de `IO80211AWDLProtocol`.

### Clases candidatas y firmas observables

1. `RTLAirAwdlInterfaceStubs : IO80211AWDLProtocol`: wrapper de firmas puras. Las firmas exactas de la mayoría de los slots 507–569 no están en headers/logs; no se deben adivinar. El único slot puro directamente rotulado en el dump padre es `IO80211VirtualInterface::slot 490`; su nombre de rutina y firma necesitan recuperación antes de declarar el wrapper.
2. `RTLAirAwdlInterface : RTLAirAwdlInterfaceStubs`: además de los slots heredados, propuesto para sobrescribir `init()` y `start(IOService*)`, `stop(IOService*)`, `free()`, funciones de colas/pools/datapath requeridas por callers AWDL, y getters BSD/role. **Firmas compilables observadas para clases actuales** en el header: `bool init()`, `bool start(IOService*)`, `void stop(IOService*)`, `uintptr_t getTxSubQueue(apple80211_wme_ac)`, `getTxCompQueue()`, `getRxCompQueue()`, `getTxPacketPool()`, `getRxPacketPool()`, `enableDatapath()`, `disableDatapath()` (`IO80211_25_6_0.h:386-524`). Para las funciones virtuales exclusivas de AWDL, firmas pendientes de verificar; no afirmar que basta con copiar estas de la interfaz Infra.
3. `RTLAirController`: reutilizar el controller existente y su virtual `setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data*)` (header:685; controller comienza:800). En la implementación, validar request role 6 y el puntero MAC/length; construir la interfaz fuera de un estado publicado parcialmente; completar registro antes de escribir éxito/nombre; hacer rollback ante cada fallo. Esto es pseudocódigo de diseño, aún no hay una implementación de la ruta.
4. Pools y colas Skywalk: reutilizar la estrategia existente (`RTLAirInterface::createDatapath`, `RTLAirLayer.cpp:342-385`) pero validar por separado si una interfaz virtual AWDL precisa colas y flags distintos. El diseño de colas AWDL Broadcom no es equivalente probado a la ruta STA.

### Secuencia candidata de lifecycle

Evidencia de Broadcom: `createProxInterface` en `Build/ida_awdl/kernel.log:1835-1869` hace `withOptions`, guarda instancia, llama un virtual por slot 1648 para asociar al core, llama `IOSkywalkEthernetInterface::initRegistrationInfo`, rellena campos de `RegistrationInfo` y llama al `start` especializado. Evidencia local STA: `initWithController` -> `attach` (`RTLAirLayer.cpp:313-332`); `startWithRegistration` -> `IO80211InfraInterface::start` -> datapath/register (`:334-385`); controller usa `attachInterface` y registration info (`:1489-1514`).

Propuesta (orden a validar, no copiar ciegamente el orden interno Broadcom):

```text
setVIRTUAL_IF_CREATE(req):
    validar req.role == 6, len y datos
    if awdl ya existe: devolver su nombre sólo si el objeto sigue registrado/activo
    obj = OSTypeAlloc(RTLAirAwdlInterface)
    obj.initWithController(this, mac, role=6, awdlPrefix/config)  // firmas/header AWDL aún pendientes
    attachInterface(obj, this)                                   // confirmar doble attach vs init
    reg = almacenamiento persistente del objeto
    obj.initRegistrationInfo(&reg, version, size)
    obj.start(this)                                               // base start sólo si contrato correcto
    crear pools/queues y llamar registerEthernetInterface o
       registerInfraEthernetInterface tras resolver qué ruta aplica a AWDL
    setAwdlInterface(obj)
    publicar/registerService y copiar nombre BSD devuelto
    en cualquier error: deregister -> stop/detach -> release; retornar error
```

**Orden de `init/start/registerEthernetInterface/setAwdlInterface`:** `init` necesariamente precede a cualquiera de los demás (objeto inicializado); el binario Broadcom sí muestra `initRegistrationInfo` antes del `start` especializado, y en su creador el `setAwdlInterface` interno por slot 1648 ocurre antes de construir el registration info (`kernel.log:1843-1869`), aunque ese slot se atribuye provisionalmente a un método del objeto: **la identidad y el momento del setter del controller siguen por confirmar**. La AirLayer actual registra mediante la función integrada `registerInfraEthernetInterface` desde su `startWithRegistration`. No elegir entre ambos registros ni reordenar `setAwdlInterface` como si fuese hecho probado. El orden propuesto de arriba es una hipótesis de construcción segura, no el ciclo exacto de Apple.

### Riesgos que requieren check explícito

- **Vtable/ABI mismatch:** firmas, número y orden de métodos AWDL incompletos; puntero puro llamado desde Family puede ir a `__cxa_pure_virtual`. Este es un riesgo demostrado por los slots en la vtable, el disparador/rama que los alcanza aún no está analizado.
- **Metaclass/dependency resolver:** dependencia de `IO80211Family` e `IOSkywalkFamily` ya declarada, pero el import real de `IO80211AWDLProtocol::gMetaClass` no se ha linkeado/validado. Símbolos del KC pueden cambiar entre builds de macOS; AirLayer ya gatea versiones/builds.
- **RegistrationInfo con vida insuficiente o campos/flags erróneos:** el caller Broadcom aloca datos `0x130` en stack y transmite campos concretos en `kernel.log:1844-1868`; en local la AirLayer persiste `_regInfo` porque el dueño/vida retenida podría ser necesaria (`RTLAirLayer.cpp:1496-1504`). No copiar tamaño/flags sin confirmar el ABI de esa versión.
- **Doble registro / desmontaje incompleto:** register, BSD attach, `registerService`, `setAwdlInterface`, detach y rollback tienen efectos de lifecycle; estado parcial puede dejar `awdl0`, ifnet, colas o punteros del controller colgantes. La función global `setAwdlInterface` sólo asigna el puntero privado, no coordina teardown (`kernel.log:1889-1900`).
- **Datapath/colas inválidos:** IOUserNetwork/Skywalk conserva owners y callbacks, y puede invocar funciones sobre pools/queues inmediatamente. Tipos equivocados, callbacks en orden incorrecto, liberar antes de deregister o apuntar a pools insuficientes puede producir use-after-free, deadlock o kernel panic.
- **Radio:** incluso si aparece `awdl0`, el RTL8814AU debe coordinar canales infraestructura/sociales, sincronía, action frames y multicast AWDL. La interfaz de red por sí misma no satisface esas funciones; Stage 3 de `Docs/ROADMAP.md` sigue abierto.
- **Requests no conocidas:** ningún ioctl post-create puede darse por opcional, implementado o tolerado si devuelve `unsupported` hasta completar los xrefs de wifip2pd. Un setter puede abortar o reintentar aunque otro camino de init observado ignore un error.

## 5. Conclusión

- Evidencia: el KC contiene globales de `IO80211AWDLProtocol`, `IO80211VirtualInterface`, sus metaclasses/vtables y las funciones de inicialización/registro Skywalk; nuestra kext ya declara las dependencias de ambos bundles y enlaza funciones de `IOSkywalkEthernetInterface` y `IO80211InfraInterface` para STA.
- Evidencia: `IO80211AWDLProtocol` hereda de `IO80211VirtualInterface` y su tabla aún presenta decenas de slots puros; la cabecera de la AirLayer no declara ni el layout ni las firmas AWDL. El camino de role 6 de Broadcom construye e inicializa su clase específica, monta Taskmaster y registra Skywalk.
- No demostrado: que la clase AWDL base sea un contrato soportado por terceros, que se pueda instanciar de forma segura con los símbolos globales actuales, que el linker/kextloader acepte todas las referencias OSMetaClass, o qué vtable mínima y registros satisfacen wifip2pd.
- Diseño recomendado: primero completar desde IDA las firmas de los slots puros y la secuencia Apple80211 post-create; después hacer un check de link de metaclass/vtable sin runtime/registro; sólo si pasa, preparar un prototipo aislado y probado en un entorno de recuperación. No devolver éxito de role 6 sólo para fabricar el nombre `awdl0`.

**Veredicto:** los exports visibles hacen plausible el enlace de bases públicas, pero no prueban que la función se pueda implementar usando sólo el contrato genérico. No hay base para decir que el código privado Broadcom sea absolutamente irremplazable; sí hay evidencia concreta de que, con el header y la información que hoy conserva este repo, no es un prototipo seguro/terminado. El blocker demostrado es el contrato AWDL inacabado (slots puros/layout/request lifecycle), no una ausencia probada de todos los símbolos base.

## 6. Pendientes para la sesión IDA del usuario

1. **Parcialmente resuelto** por [`Docs/AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md): `RawGet` type 12/len 21 (`CARD_CAPABILITIES`) precede a `RawSet` type 94/len 32 (`VIRTUAL_IF_CREATE`); en esta última, el rol 6 está en `+0x0C` y la MAC en `+4..+9`. Tras el éxito, `wifip2pd` espera un semáforo hasta 30 s; el handle de rol 6 usa el nombre `awdl`. **PENDIENTE de verificar en IDA:** qué evento libera el semáforo; la identidad de los tipos 97 y 293; y las peticiones `get/setAWDL_*` posteriores, con sus buffers y ramas de error. El type 293 observado en otra función no está atribuido a AWDL.
2. Recuperar nombres/firma de los slots puros de `IO80211AWDLProtocol` (vtable entries 507–569) y el slot 490 aún puro de `IO80211VirtualInterface`, y comparar las implementaciones Broadcom entry by entry.
3. Confirmar por xref cuál es la llamada virtual en slot `+1648` de `createProxInterface` y si/o cuándo el controller recibe `setAwdlInterface`.
4. Hacer una prueba de link sólo después de esos datos, con imports limitados a símbolos `0f` de estas versiones; documentar sin ejecutar en hardware hasta disponer de una ruta de recuperación.
