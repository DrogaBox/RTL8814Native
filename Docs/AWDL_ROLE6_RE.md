# AWDL role 6: qué crea AppleBCMWLANCore y qué exige IO80211Family

## Alcance y fuentes

Análisis estático de los IDB del BootKernelExtensions.kc y de `wifip2pd` incluidos en `Build/ida_awdl/`, más símbolos/vtables extraídos para macOS 25.6.0 en `Docs/re/io80211_25.6.0/`. Direcciones abajo son direcciones virtuales del kernel/imagen indicada, no offsets de archivo. El log previo de hardware y el mapa de estado están en `Docs/ARCHITECTURE.md` y `Docs/ROADMAP.md`. «Observado» significa que la condición/call aparece en pseudocódigo o símbolos; no sustituye una prueba en hardware de la capa RTL8814.

## 1. Camino de llamada y roles

1. `IO80211Family` registra la función estática `setVIRTUAL_IF_CREATE` en `IO80211Controller` (`Build/ida_awdl/flow.log`, `0xffffff80022886a8`). Requiere longitud de request 32 (`a4+24 == 32`) y un puntero de datos no nulo. Si el puente WCL no intercepta/falla de la forma especial `-528340977`, llama a `apple80211setVIRTUAL_IF_CREATE(a2)`. Esa función es el puente al override virtual del controlador; no hay en el pseudocódigo una regla que filtre el rol 6.
2. El override Broadcom `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`Build/ida_awdl/core.log`, `0xffffff80016788f4`; implementación también en el tramo de `Build/ida_awdl/kernel.log`) lee el rol como dword en `request+12`:

   ```text
   role 8..10 -> createNANInterface(request)
   role 6     -> si prox ya existe, éxito; si no, createProxInterface(request)
                al éxito escribe getBSDName() en request+16 (máx 16 bytes)
   role 7     -> construir AppleBCMWLANIO80211APSTAInterface y start(prefix="ap")
                si ya existe la interfaz, -536870190
   otro       -> -536870911
   ```

   El campo nombre empieza en `+16`; el mismo handler pone a cero los dos qwords allí antes de copiar el nombre. En caso de que el objeto AWDL ya exista, devuelve de nuevo su nombre. El handler role 7 usa su propio slot de estado, distinto al puntero de proximidad.
3. La fábrica genérica `IO80211Controller::createVirtualInterface` (`Build/ida_awdl/attach2.log`, `0xffffff800229612e`) retorna nulo salvo `role == 7`; para 7 aloca `IO80211VirtualInterface`, ejecuta `init(controller, mac, 7, "ap")` y libera si falla. Por tanto **rol 7 es el AP virtual genérico**, no AWDL; **rol 6 llega al override de `setVIRTUAL_IF_CREATE` del driver**. No se infiere que el dispatcher le asigne otra semántica al 6.
4. `IO80211Controller::attachVirtualInterface` (`Build/ida_awdl/kernel.log`, `0xffffff80022960aa`) usa la fábrica virtual, adjunta el objeto al controlador y lo devuelve al llamador; es la ruta genérica y no la llamada observada en `createProxInterface`. AWDL usa el camino especializado del driver descrito en §3.

Pseudocódigo resumido:

```text
Family setter(request[32]):
    if WCL does not consume request:
        apple80211setVIRTUAL_IF_CREATE(controller, request)

AppleBCMWLANCore setter(request):
    switch request.role:
      case 6: create/reuse proximity interface; request.name = BSD name
      case 7: create/start APSTA interface (prefix ap)
      case 8..10: create NAN interface
      default: unsupported role
```

## 2. Jerarquía, vtable y qué es “obligatorio”

- `AppleBCMWLANProximityInterface` es una clase concreta cuyo padre inmediato es **`IO80211AWDLProtocol`**: el constructor `withOptions` (símbolo `__ZN30AppleBCMWLANProximityInterface11withOptions...`, `0xffffff80017159e6`) usa su metaclass, construye `IO80211AWDLProtocol` con `AppleBCMWLANProximityInterface::gMetaClass` y fija la vtable `__ZTV30AppleBCMWLANProximityInterface` (`syms_bcm.txt:4086`, vtable en `0xffffff80017f5b68`). Su `init` especializado invoca por slot `+4552` (`0x11c8`) con argumentos `core, MAC, 6, name`, y libera el objeto si falla (`Build/ida_awdl/flow.log`, 0xffffff80017159e6).
- El vtable de `IO80211InfraProtocol` muestra en slots 413/414 los métodos `IO80211SkywalkInterface::init(IOService*, ether_addr*)` y `isInterfaceEnabled`, y en 461 `IO80211InfraInterface::setLinkState`; el hijo AWDL reemplaza al menos parte de la interfaz de protocolo. La vtable de `AppleBCMWLANSkywalkInterface` identifica el `start(IOService*)`/`stop(IOService*)` genérico en slots 184/185, y el start especializado de Skywalk en slot 668 (`vt_AppleBCMWLANSkywalkInterface.txt:185-186, 668-669`).
- La lista de símbolos de la clase (Docs/re/io80211_25.6.0/syms_bcm.txt:1302-1445) demuestra implementaciones de `init`, `start`, `stop`, `free`, `enable/disable`, `enableDatapath/disableDatapath`, colas/pools (`getTxSubQueue`, `getTxPacketPool`, `getTxCompQueue`, `getRxCompQueue`), y handlers AWDL (`set/getAWDL_SYNC_*`, channel sequence, presence, election, encryption, etc.). Es una muestra de sobrescrituras/funciones de la clase; para afirmar exactamente todas las ranuras sobrescritas se requeriría comparar numéricamente cada entrada de ambas vtables. **No verificado aquí**.
- **No se puede derivar de los binarios una lista mínima de métodos virtuales “obligatorios” que IO80211Family valide para admitir rol 6.** La request no presenta una comprobación de clase/vtable para AWDL: el éxito llega del override y de que el objeto se registre correctamente en Skywalk. Lo probado es lo que Broadcom implementa y el camino genérico del rol 7, no un contrato mínimo del rol 6.

## 3. Ciclo de vida y registro de `awdl0`

1. `AppleBCMWLANCore::createProxInterface` (`Build/ida_awdl/kernel.log`, `0xffffff800165dcbe`) crea el objeto solo si el slot de AWDL está vacío; si ya existe no duplica. Llama a `AppleBCMWLANProximityInterface::withOptions(core, MAC, ..., name)` y guarda la referencia en estado privado del core.
2. `withOptions` hace la asignación tipada de 320 bytes y ejecuta `init(core, MAC, 6, name)`. Ese init (`Build/ida_awdl/flow.log`, `0xffffff8001718db0`) llama al `init()` de la clase, asigna sus ivars, llama al init de Skywalk/infra, asigna interface ID (`2` en la rama habitual; `0xffffffff` si el valor de versión/config observado es menor que 5), configura callback de canal y aloca `AppleBCMWLANTaskmaster` con work queue. Falla si no puede crear taskmaster. Configura tiempos/valores por defecto y conserva los objetos de gestión/transmisión requeridos. Son datos privados, no una receta ABI para copiar.
3. El creador vincula el protocolo al core por el slot virtual `+1648` y llama a `IOSkywalkEthernetInterface::initRegistrationInfo`; construye un registro Skywalk y llama a `AppleBCMWLANProximityInterface::start(interface, core, RegistrationInfo*)` (`0xffffff8001729376`). Si el IDB interpreta esos slots como nombres equivalentes: se ven en vtable `AppleBCMWLANSkywalkInterface` (`start` slot 668) y la ruta de infra que parte de `IO80211SkywalkInterface`.
4. El `start` especializado primero arranca la capa Skywalk base; obtiene el `AppleBCMWLANBusSkywalk` del provider y construye/adjunta el descriptor de datapath y colas al bus. Registra las colas de TX/RX completion y submission enumeradas en el descriptor mediante `IOSkywalkPacketQueue::set...`; finalmente `IOSkywalkEthernetInterface::registerEthernetInterface(this)`. Ante error deshace registrations/enable y retorna fallo (`Build/ida_awdl/flow.log`, función desde `0xffffff8001729376`, especialmente llamadas `registerEthernetInterface`, y `kernel.log` creador desde `0xffffff800165dcbe`).
5. Al tener éxito, `setVIRTUAL_IF_CREATE` consulta `IO80211SkywalkInterface::getBSDName` y copia el nombre al ABI de la request (`core.log`, `0xffffff80016788f4`). El nombre `awdl0` se apoya en el método de la clase `getBSDNamePrefix` (`syms_bcm.txt:1309`) y en el resultado reportado en el análisis previo de hardware (`Docs/ARCHITECTURE.md`, 2026-10-27 AWDL stage 2). No aparece en el pseudocódigo de init un literal que permita probar por sí solo el `0` final; la convención/prefijo es evidencia de clase, nombre final corroborado por estado previo.
6. El controlador guarda además la interfaz en `IO80211Controller::setAwdlInterface` (`Build/ida_awdl/kernel.log`, `0xffffff8002299c24`), que escribe un puntero en estado del controlador. La familia y AWDL peer manager pueden entonces asociar peers/multicast con ese objeto.
7. De red/IORegistry: el start registra la interfaz Ethernet/Skywalk, que es el paso que publica el netif al stack. **No se recuperó en la evidencia estática aquí un listado exacto de propiedades IORegistry ni todos los collares de BSD attach/naming**; no los invento. El registro por sí solo tampoco prueba que el datapath transmita/reciba frames válidos.

## 4. `wifip2pd` y las requests apple80211

**Demostrado:** `Build/ida_awdl/probe.log` identifica el binario analizado como `Build/ida_awdl/wifip2pd.x86_64`. Su función `sub_10036C2B0` (pseudocódigo alrededor de `probe.log:49140-49290`) busca/consulta su interfaz AWDL; si no la encuentra emite “No AWDL interface found. Creating AWDL interface” y dispara una tarea de creación (`sub_1003654A0`). La observación de hardware documentada en `Docs/ARCHITECTURE.md` 2026-10-27 confirma el request `APPLE80211_IOC_VIRTUAL_IF_CREATE` (type 94, longitud 32) con rol 6 hacia el driver. No hay una llamada `Apple80211RawSet` atribuida por xref a esa tarea de creación dentro del pseudocódigo retenido; por eso, la conexión exacta Swift → ioctl de creación queda **no verificada en esta muestra**.

El IDB de `wifip2pd` contiene strings/símbolos de tipos para requests AWDL, y el IDB Broadcom aporta métodos para sus handlers. Esto prueba que esos métodos existen en el driver, pero no quién los llama ni que todos sean parte del primer arranque. La tabla evita convertir nombres de métodos en llamadas observadas:

| Request/capacidad | ID/handler observable | Estado de prueba y respuesta Broadcom |
|---|---|---|
| `VIRTUAL_IF_CREATE` | `APPLE80211_IOC_VIRTUAL_IF_CREATE`, type 94, 32 bytes; request.role `+12`, name `+16` | **Llamada comprobada en log hardware**; rol 6 crea/reusa y copia BSD name, éxito solo tras completar start/registro (§1, §3). |
| `AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | type numérico no recuperado; clase tiene get `0xffffff800172421a` y set `0xffffff8001724436` | **No verificado** que `wifip2pd` lo envíe al crear awdl0; handler construye secuencia de canales/comandos según símbolos `syms_bcm.txt:1402-1405`. No asumir éxito nulo. |
| `AWDL_SYNC_PARAMS`, `AWDL_SYNC_ENABLED`, `AWDL_SYNC_STATE` | tipos numéricos no recuperados; get/set symbols `syms_bcm.txt:1343-1345, 1370-1374` | **No verificado** en la secuencia de creación; Broadcom lee/actualiza el estado de sincronía y envía trabajo a su taskmaster (detalle exacto de cada validación no revisado). |
| `AWDL_PRESENCE_MODE`, `AWDL_DEVICE_CAPABILITIES`, `AWDL_ELECTION_*`, `AWDL_BSSID`, `AWDL_ENCRYPTION_*` | IDs no recuperados; setters/getters de la clase `syms_bcm.txt:1348-1363, 1381-1383, 1395, 1399` | Los handlers están presentes, pero **no verificado** qué requests envía `wifip2pd` luego de creación ni cuáles son indispensables para que la interfaz se registre. |
| Estadísticas, peer cache, low-latency / traffic registration | Símbolos `getAWDL_DEV_STATS`, `getAWDL_CCA`, `getPEER_CACHE_*`, `setAWDL_PEER_TRAFFIC_REGISTRATION`, etc. en `syms_bcm.txt:1346-1347, 1384, 1400-1401` | Presencia en Broadcom, no prueba de uso/orden por `wifip2pd`; no hay ID o resultado atribuible con la evidencia preservada. |

El probe de `wifip2pd` contiene un `Apple80211RawSet` en pseudocódigo `sub_10004B300` (`probe.log` ~41452), con `type = 293`, `len = 24`, y maneja el error; el mismo cuerpo prueba `if_nametoindex` de nombres codificados después del setter. Sin símbolos/descodificación segura del key path y sin xref a la rama AWDL principal, **no se atribuye a AWDL ni se etiqueta como un ID apple80211**. IDs de requests AWDL aparte del type 94 permanecen no verificados.

En consecuencia, no es posible contestar con rigor la petición “lista completa de IDs que wifip2pd usa después y respuesta mínima Broadcom por cada uno” con el pseudocódigo guardado: el IDB de Broadcom ofrece handlers pero no la secuencia del cliente; el extracto IDA de `wifip2pd` no expone el flujo de todos los RawGet/RawSet. Esa lista se mantiene como **pendiente**, no se completa mediante inferencia.

## 5. Conclusión: ¿puede hacerlo RTL8814AirLayer sin Broadcom?

- Una subclase local de `IO80211ControllerV2` puede recibir la request si implementa el override `setVIRTUAL_IF_CREATE` (en nuestro ABI está declarado en `Source/IO80211/IO80211_25_6_0.h`, slot 556). El precedente de hardware es que al activar `CARD_CAPABILITIES` byte `+8` bit 0, `wifip2pd` deja de rechazar el hardware por capacidad y envía rol 6; con el stub actual, el kernel devuelve `0xe00002c7` (`Docs/ARCHITECTURE.md`, entradas 0.27).
- **Un `awdl0` superficial no equivale a interfaz AWDL aceptada/útil.** El camino Broadcom depende de la clase privada `AppleBCMWLANProximityInterface`/`IO80211AWDLProtocol`, estado/taskmaster, handler de requests, registro de múltiples colas Skywalk con el bus Broadcom y envío/recepción de frames AWDL. La vtable de esa clase no es exportada como API de construcción para una kext independiente; el setter de IO80211Family solo entrega la request, no construye esos recursos por rol 6.
- Podría explorarse una implementación independiente que registre su propia interfaz compatible con los contratos públicos/ABI disponibles y controle un datapath, pero **no está demostrado** que macOS acepte esa clase distinta para el registro de AWDL/peer manager ni que baste implementar una selección de virtual methods. No es correcto afirmar que sea absolutamente imposible sin Broadcom; sí que no puede realizarse correctamente solo con `setVIRTUAL_IF_CREATE` devolviendo éxito y una cadena `awdl0`.
- Las piezas que no se pueden reconstruir como contrato portable desde este binario: formato/ABI completo de los parámetros de registro del bus, protocolo privado de colas/owners, integración de AWDL peer manager y el algoritmo/firmware para sincronía y channel sequence. El pseudocódigo muestra la ruta Broadcom y sus llamadas, no sus especificaciones completas. El hardware RTL8814 además necesitaría coexistencia/time-slicing y acción AWDL real; tener un BSD interface no demuestra soporte de radio.

**Resumen honesto:** se puede aceptar sintácticamente la IOC con un override propio; no hay prueba de que se pueda crear una interfaz role-6 funcional usando únicamente la jerarquía genérica expuesta. Una reimplementación de datapath/estado sería investigación y trabajo sustancial, con integración privada no verificada. Hasta verificar registro, requests posteriores, enlace Skywalk y tráfico AWDL en hardware, Stage 2 sigue bloqueado.
