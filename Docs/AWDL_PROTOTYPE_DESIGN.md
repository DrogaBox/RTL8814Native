# Diseño por etapas del prototipo AWDL (role 6) para RTL8814AirLayer

Sustituye la versión anterior de este documento. Aquella planteaba el prototipo como una hipótesis
de enlace condicionada a pendientes sin cerrar (qué liberaba la espera de 30 s tras
`VIRTUAL_IF_CREATE`, qué peticiones hacía `wifip2pd` después, cuál era la puerta AWDL, qué eran los
tipos 97 y 293). Todos esos puntos quedaron cerrados en la serie de documentos de análisis de
`Docs/`, y este documento convierte esos resultados en un plan de implementación por etapas: para
cada etapa se listan las peticiones apple80211, las properties de IORegistry y los eventos que el
kext debe proporcionar, con las direcciones del kernel y del daemon que fijan cada requisito. Lo no
verificado va como `?`.

## 0. Base, convenciones y método

Documentos de origen (verificados, md5 del kernel `f489ff8be80e6013a1bf47561dcaef45`, del daemon
`a120cfccfa2783303604ce5be9e1763a`):

| Documento | Qué aporta al diseño |
|---|---|
| [`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) | Liberación del semáforo de 30 s, secuencia completa de peticiones del daemon, tipos 97 y 293 |
| [`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) | Rutas de despacho, tablas modernas/legacy/internas, superficie AWDL (+0xFC8..+0x11C0) y slots |
| [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) | Layouts C de los payloads (puerta, create, companion y structs por tipo) |
| [`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) | Nacimiento de `awdl0` en el kernel: properties, unit, ifnet, orden `registerService` |
| [`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) | Datapath Skywalk del role 6: colas, pools, MTU, TX/RX |
| [`AWDL_EVENTS.md`](AWDL_EVENTS.md) | Canales de eventos, emisores y qué consume `wifip2pd` |
| [`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) | Companion `llw0`: emisión del 293, consecuencias de su fallo, veredicto para la etapa 1 |

Convenciones: las direcciones son VA del binario indicado (`w2p` = `wifip2pd.x86_64`); `?` = no
verificado. Las direcciones citadas en este documento se re-comprobaron con IDA 9.4 headless sobre
copias de los IDB en `Build/ida_awdl/task_awdl_design/kc/` y `/p2p/` (scripts `verify_kc.py` /
`verify_p2p.py`, salidas `verify_out.txt`; detalle en §8).

## 1. Hechos verificados que fijan el diseño

### 1.1 La puerta AWDL es el bit 0 del byte +0x08 de `CARD_CAPABILITIES`

`wifip2pd` abre cada rama de rol con `RawGet` tipo 12 (`CARD_CAPABILITIES`, buffer de 21 B;
sitios `w2p 0x10004811d`, `0x100048260`, `0x1000483a8`). La rama AWDL (caso 1 del switch
`0x100047E85`) prueba el bit 0 de `data[4]` (`test byte [rbp+var_70+8],1` en `w2p 0x1000483b5`);
como el copyback del handler moderno (`gGetHandlerTable[11]` = `0xffffff800245ebb8` → handler
`getCARD_CAPABILITIES` `0xffffff8002279e89`) entrega `min(req_len, 21)` bytes desde el byte 4 del
struct `apple80211_capability_data` (28 B: `version` u32 @+0x00, `flags[24]` @+0x04), `data[4]` es
el **byte +0x08 del struct**. Las ramas NAN (casos 2/3) prueban `data[9]` (`w2p 0x10004812a`,
`0x10004826d`) = **byte +0x0D**, que es la puerta de Wi-Fi Aware, no la de AWDL. En Broadcom el
byte +0x0D bit 0 lo pone `AppleBCMWLANCore::getCARD_CAPABILITIES` (`0xffffff8001661076`) bajo
`featureFlagIsBitSet(6)`; la condición que pone el +0x08 bit 0 en Broadcom no quedó identificada
(`?`) — irrelevante para el diseño: el kext debe poner ese bit deliberadamente.
([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.1.)

### 1.2 La espera de 30 s la libera IORegistry, no ningún evento Apple80211

Tras el `RawSet` 94 con éxito, el creador por rol (`sub_100047660`, `w2p 0x100047660`) espera en
`OS_dispatch_semaphore.wait(timeout:)` (`w2p 0x1000491a8`) con `DispatchTime.now() + 30 s`
(`mov qword [r12], 0x1E` en `w2p 0x10004913b`). La espera la libera la notificación
**`IOServiceMatched`**: el daemon registra `IOServiceAddMatchingNotification` con el diccionario
`{"IOPropertyExistsMatch": "IOInterfaceName"}` (`w2p 0x100043314`), y el descubridor
(`sub_100043720`) filtra cada servicio nuevo por: (a) propiedad `IOInterfaceName` presente;
(b) `SIOCGIFMEDIA` con `(ifm_current & 0xE0) == 0x80` (`IFM_IEEE80211`; ioctl en `w2p 0x100043cbf`,
filtro en `w2p 0x100043ccc`); (c) índice BSD vivo vía `getifaddrs`/`AF_LINK`; (d) rol leído de
`IO80211VirtualInterfaceRole` (o `IO80211InterfaceRole`): el string `AirLink` casa el código
interno 1 y señala el semáforo del rol (`call sub_100046BD0` en `w2p 0x1000451f2`; señal
`w2p 0x100046d03`). La ruta de eventos Apple80211 (`sub_1000685A0` → `sub_100041FE0`) no toca el
semáforo en ningún punto. Si la creación del handle falla, se señala igualmente (`w2p 0x1000445a3`).
([`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §1.)

### 1.3 `VIRTUAL_IF_CREATE` (tipo 94): rol 6, payload de 32 B, nombre en payload+16

`gSetHandlerTable[93]` (`0xffffff80024601d8`) apunta al handler `setVIRTUAL_IF_CREATE`
(`0xffffff80022886a8`), que exige `len == 32` y `data != NULL`, prueba la ruta WCL y cae al puente
`apple80211setVIRTUAL_IF_CREATE`, que hace `safeMetaCast` a `IO80211InfraProtocol` y llama el slot
**+0x1160 (slot 556)** del objeto — en Broadcom lo implementa
`AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`): lee el rol en `payload+0x0C`
(`0xffffff800167891c`), despacha 6 → `createProxInterface` (`0xffffff800165dcbe`, call en
`0xffffff8001678bdd`), 7 → APSTA, 8..10 → NAN; si la interfaz ya existe devuelve 17 y salta igual
al bloque común; y escribe el nombre BSD con `strlcpy(payload+16, getBSDName(), 0x10)`
(`call _strlcpy` en `0xffffff8001678a2b`). El payload que arma el daemon: u32 @+0 (enviado, lectura
`?`), MAC 6 B @+0x04, rol u32 @+0x0C (6 = AWDL; el daemon también usa 9 y 0xA `?`), nombre de
salida @+0x10. ([`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §3.2,
[`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.1.)

### 1.4 Nacimiento de `awdl0`: el nombre sale del `RegistrationInfo`, no de un contador de roles

Cadena Broadcom (`createProxInterface` → `withOptions` → `init` → `attach` → `start`):
`AppleBCMWLANProximityInterface::withOptions(core, payload+4, "awdl")`
(`0xffffff80017159e6`), `init(core, mac, 6, "awdl")` (`0xffffff8001718db0`) que llama
`IO80211VirtualInterface::init` (`0xffffff80023305f8`) y publica `IO80211VirtualInterfaceRole =
"AirLink"` vía `setInterfaceRole(6)` (`0xffffff80022f60f0`; tabla de claves `0xffffff800246bdd8[5]`,
valores `0xffffff800246bd88[5]`), `attach` por el slot +0x670, y `start(core, RegInfo)`
(`0xffffff8001729376`): registro de colas con el bus, MTU 0x594 en `RegInfo+0x48/+0x4C`
(`mov rax, 0x59400000594` en `0xffffff80017296ff`, `mov [r15+0x48], rax` en `0xffffff8001729709`) y
`IOSkywalkEthernetInterface::registerEthernetInterface` (`0xffffff8002b10a84`; `initRegistrationInfo`
`0xffffff8002b108e2` valida versión 1 / tamaño 0x130), que publica `IOMACAddress`,
`IOInterfaceNamePrefix` y `IOInterfaceUnit` (`registerNetworkInterface` `0xffffff8002b0a204`).
Sobre el éxito, **`registerService(2)`** síncrono (`mov esi,2` en `0xffffff8001729825`,
`call [rax+0x5B0]` en `0xffffff800172982a`): dentro corre `IOSkywalkNetworkBSDClient::start`
(`0xffffff8002aff562`), que compone `"awdl0"` con `snprintf("%s%u", prefijo, unit)`, publica
`"BSD Name"` e **`IOInterfaceName`** (primera publicación: es el momento en que dispara el matching
de `wifip2pd`), crea el ifnet (`kern_nexus_controller_alloc_net_provider_instance` →
`ifnet_allocate_extended` → `prepareBSDInterface`, que vuelve a publicar `IOInterfaceName` en
`0xffffff80022f63eb` → `bpfattach` → `ifnet_attach`) y termina con su propio `registerService(0)`.
De vuelta, `setVIRTUAL_IF_CREATE` copia el nombre a payload+16 y devuelve 0; cuando el daemon
recibe el 0, el ifnet ya existe y la notificación está en camino o entregada.

El unit es **el valor del `RegistrationInfo+0x38` que escribe el driver** (0 para `awdl`, 1 para el
AP), no el contador global por rol `intfRoleToNumberMap` (que solo alimenta el nombre de pipe).
Prefijo en `RegInfo+0x30` (puntero al literal `"awdl"` en `0xffffff8001797376`).
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2–§5.)

### 1.5 Superficie AWDL y despacho

Cuatro rutas llegan a las tablas de `IO80211Family`, indexadas por `tipo − 1` con rango `[1, 586]`
(`apple80211RouteExternal`, `0xffffff800236cd2e`): user client moderno (`gGetHandlerTable`
`0xffffff800245eb60` / `gSetHandlerTable` `0xffffff800245fef0`), ioctl BSD clásico
(`gGetHandlerTableLegacy` `0xffffff8002470d50` / `gSetHandlerTableLegacy` `0xffffff8002479060`,
sin tipos AWDL específicos), ruta interna de glue (`gInternalGet/SetHandlerTable`
`0xffffff800247cea0` / `0xffffff800247e0f0`) y los tipos AWDL modernos que no tocan la vtable sino
`IO80211AWDLPeerManager`. Los puentes que sirven la superficie AWDL llaman primero
`isCommandProhibited` (slot +0xCC8), hacen cascada de `safeMetaCast` por clase de protocolo y
despachan el slot correspondiente; sin cast exitoso devuelven `0xE082280E`; request sin fila en la
tabla activa devuelve **102** sin llegar al driver.

La superficie AWDL son los slots **vptr +0xFC8..+0x11C0** (64; `__ZTV19IO80211AWDLProtocol`;
vptr Broadcom `0xffffff80017f5b78`). En la base todos llevan el mismo stub; Broadcom los
sobrescribe todos (verificado en esta pasada: +0xFC8 → `getSSID` `0xffffff800171a70e`, +0x1180 →
`setCOMPANION_SKYWALK_IF_CREATE` `0xffffff8001722ef2`, +0x11C0 → `getTX_RATE`,
+0x11C8 → la `init` especializada de la subclase). Los slots puros que una subclase instanciable
debe cubrir: **490 (`setMacAddress(ether_addr&)`, propio de `IO80211VirtualInterface`) y 507–569**
([`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md)); el dispatch añade que también +0x11C0 (`getTX_RATE`)
lleva stub en la base ([`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §7) —
discrepancia menor de recuento entre ambos documentos (`?`), sin efecto sobre la conclusión: la
subclase debe implementar toda la superficie, no solo los tipos que interesan.

Los 43 tipos con slot, struct y longitud están cerrados en
[`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §5 y
[`AWDL_STRUCTS.md`](AWDL_STRUCTS.md) §4; otros 27 tipos AWDL solo llegan al `PeerManager`
(§5 de dispatch). Consecuencia importante: **`wifip2pd` no emite ningún GET/SET AWDL en estado
estacionario** — sus 4 call sites de `RawGet` (tres del tipo 12 y uno del 97) y 2 de `RawSet` (94 y
293) son todos (`AWDL_STRUCTS.md` §2) — de modo que la superficie la ejercita la ruta interna de
glue y otros clientes, no el daemon.
([`AWDL_APPLE80211_DISPATCH.md`](AWDL_APPLE80211_DISPATCH.md) §2–§7.)

### 1.6 Datapath Skywalk del role 6

La interfaz no crea colas: las pide al bus con el descriptor `skywalkInterfaceRegisterParams_t`
(12 qwords; conteo TX en un byte, mapa AC u32[], arrays de salida de colas y pools, headroom u16,
logger) y el bus (`AppleBCMWLANPCIeSkywalk::registerSkywalkInterface`, `0xffffff8001549528`) crea y
devuelve **4 colas TX submission (una por WME AC, mapa {0,1,2,3}), 1 TX completion, 1 RX completion
y 1 multicast opcional**, todas sobre **un único pool compartido** (Broadcom: 4864 paquetes × 2048
B, `maxBuffersPerPacket = 1`, flags = 1; `allocSkywalkCommonResources`, `0xffffff8001548d2a`). TX:
`dequeuePackets` (`0xffffff80015420f0`) reparte por rol; RX: `rxCompRingDrain`
(`0xffffff8001549dea`) entrega en la RxCompletionQueue registrada, sin copias. MTU efectivo:
**0x594 (1428) útiles tras headroom 64** (`RegInfo+0x48/+0x4C`). La familia no habilita las colas:
el driver llama `enableDatapath`/`disableDatapath` (`0xffffff8001729afc` / `0xffffff8001729ec6`)
desde `enable(unsigned)`/`disable(unsigned)`.
([`AWDL_PROXIMITY_DATAPATH.md`](AWDL_PROXIMITY_DATAPATH.md) §2–§10, §14.)

### 1.7 Eventos: canal, consumo y qué es bloqueante

Canal principal: event pipe de `IO80211Family` — `postMessageSync`
(`0xffffff80022f8abc`) escribe pares (id apple80211, payload ≤ 0x1000 B) con cabecera de 48 B en el
anillo (`IO80211IOUCEventPipe::writePipe`, `0xffffff80022a7e7c`) y notifica con
`sendAsyncResult64`; los nombres canónicos están en `gAppleMIndexToString`
(`0xffffff8002483e80`; verificados 46/47/48/65/195/243 en esta pasada). Los frames AWDL viajan
además como taps BPF (DLT 0xC). `wifip2pd` registra el monitoreo con
`Apple80211RawP2PEventMonitoringInit` (rol 1) o `Apple80211RawEventMonitoringInit` + 16×
`Apple80211StartMonitoringEvent` (rol 0, ids 1, 2, 3, 9, 11, 55, 59, 66, 74, 78, 82, 125, 126, 127,
160, 215) y consume en el switch maestro `sub_1000388D0` (`w2p 0x1000388d0`). **Ningún evento es
condición de aceptación de `awdl0`** (el semáforo lo libera IORegistry, §1.2); lo bloqueante es que
el **registro del monitoreo devuelva 0** — un error encadena recuperación (`sub_100046E30`,
`w2p 0x100046e30`) que re-crea la interfaz. En operación el daemon consume sobre `awdl0` los ids
**46** (sync state), **47** (peer presence), **48** (action frames RX), **65/142/149/243**
(estadísticas), **195** (chan seq), 68/172/182/200/234/235; los ids 41/42/43, 64, 69, 77, 86, 88,
94, 135, 170, 183, 204 que emite Broadcom los descarta.
([`AWDL_EVENTS.md`](AWDL_EVENTS.md) §1–§5.)

### 1.8 Companion `llw0` (tipo 293): opcional en la etapa 1

El `RawSet` 293 (`COMPANION_SKYWALK_IF_CREATE`; `gSetHandlerTable[292]` `0xffffff8002460810` →
handler `0xffffff800228b4b8`; payload 24 B: `code` u32 @+0, `op` u32 @+4 (1 = crear), nombre de
salida @+8) solo se emite al crear la interfaz de rol interno 4 (`LowLatency`, prefijo `llw`)
contra un padre ya creado y aceptado: único caller `w2p 0x100048061` (caso 4 del creador por rol),
emisor `sub_10004B300` (`w2p 0x10004b300`, `RawSet` en `w2p 0x10004b3e6`). Si falla, `wifip2pd`
registra dos errores y **continúa**: el init de `AWDLInterface` absorbe el fallo, no hay teardown
de `awdl0`, no hay `RawSet` 95 ni reintento; la única espera asociada (30 s, `w2p 0x100048b13`) se
produce solo si el kext responde éxito sin crear nada. El daemon valida el resultado con
`if_nametoindex("llw0")` (inmediato `0x30776C6C` en `w2p 0x10004b814`). En Broadcom la
implementación (`0xffffff8001722ef2`) crea una interfaz Skywalk propia con prefijo `"llw"`, rol 2
(que publica `IO80211InterfaceRole = "LowLatency"`, tabla `0xffffff800246bd88[1]` verificada) e id 9.
([`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §1–§4.)

## 2. Punto de partida en el repo

`RTL8814AirLayer` ya es una kext separada con dependencias declaradas sobre
`com.apple.iokit.IO80211Family` e `IOSkywalkFamily` (`Source/AirLayer/Info.plist:54-72`), compilada
`-fapple-kext` contra los headers locales (`build_airlayer.sh:20-29`), con gateo de versiones
(`Source/AirLayer/VersionGate.h`, `KnownBuilds.inc`). El camino STA existente demuestra el
esqueleto reutilizable: `RTLAirInterface : RTLAirInterfaceStubs : IO80211InfraProtocol`
(`Source/AirLayer/RTLAirLayer.hpp:44-57`), init/attach/start/pools/queues y registro vía
`IO80211InfraInterface::registerInfraEthernetInterface` con `initRegistrationInfo` enlazado por asm
(`Source/AirLayer/RTLAirLayer.cpp:124-130`, `:313-385`, `:1489-1514`); el controller ya declara la
virtual `setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data*)` en el header local
(`Source/IO80211/IO80211_25_6_0.h:685`, slot 556 = +0x1160). La capa además ya contesta el
`RawGet` 12 en hardware ([`AWDL_WIFIP2PD_POST_CREATE.md`](AWDL_WIFIP2PD_POST_CREATE.md) §2.1, con
referencia a [`ARCHITECTURE.md`](ARCHITECTURE.md)); el requisito nuevo de la etapa 1 es el valor
del byte +0x08 de esa respuesta.

## 3. Etapa 1 — `awdl0` creada y aceptada por `wifip2pd`, sin pares

Objetivo: cargar el kext con soporte infraestructura intacto y que `wifip2pd` cree su handle de
AWDL sin timeout, sin pares y sin datapath real. Criterio de aceptación: log del daemon
`"Found new interface awdl0 with role AirLink"` y ausencia de
`"Timed out while waiting for interface awdl0"` (`w2p 0x10004974b`); `awdl0` visible con índice BSD
y entrada IORegistry completa.

### 3.1 Peticiones que debe contestar el kext

| # | Petición | Contrato exacto | Referencia |
|---:|---|---|---|
| 1 | `RawGet` **12** `CARD_CAPABILITIES` | `len != 0`; respuesta con `flags[4] bit0 = 1` (byte **+0x08** del struct de 28 B) y bit0 de `flags[9]` (byte +0x0D, puerta NAN) a 0; copyback `min(len, 21)` desde struct+4 | §1.1; dispatch §4 (GET 12); structs §3.1 |
| 2 | `RawSet` **94** `VIRTUAL_IF_CREATE` | `len == 32`; rol u32 @+0x0C == 6; MAC @+0x04..+0x09; crear la interfaz; escribir nombre BSD en **payload+0x10** (`strlcpy`, 16 B); devolver 0; si ya existía, devolver 17 escribiendo el nombre igualmente | §1.3; structs §3.2; birth §2.1 |
| 3 | `RawGet` **97** `VIRTUAL_IF_PARENT` | `req_len >= strlen(padre)`; copiar `strlen` bytes **sin NUL** del nombre del padre (`en0`); el daemon envía `len = strlen(awdl0)` y busca `'\0'` con ese tope | POST_CREATE §2.3; structs §3.3 (handler `0xffffff800227b7ff`) |
| 4 | Monitoreo de eventos | `Apple80211RawP2PEventMonitoringInit` (awdl0) y, si procede, `Apple80211RawEventMonitoringInit` + 16× `Apple80211StartMonitoringEvent` deben **devolver 0** | EVENTS §4.1, §5 |
| 5 | Ioctls ifnet | `SIOCSIFFLAGS` (up/down), `SIOCSIFLLADDR` (MAC), `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI` — el camino estándar de ifnet | POST_CREATE §2.6; structs §2 |

Cualquier otra request puede quedar sin fila/handler: el despacho devuelve 102 sin llegar al driver
(§1.5), y en estado estacionario el daemon no emite nada más (structs §2).

### 3.2 Properties de IORegistry y orden de registro (lo que dispara `IOServiceMatched`)

Sobre la entry del objeto interfaz, antes de que el matching pueda dispararse:

1. `IO80211VirtualInterfaceRole = "AirLink"` — por `setInterfaceRole(6)`
   (`0xffffff80022f60f0`; clave `0xffffff800246bdd8[5]`, valor `0xffffff800241dbc0`), o publicada
   directamente si no se usa la init de la familia. Birth §4.
2. `IOMACAddress` — 6 B, la MAC del payload+4 (en Broadcom la publica `copyRegistrationInfo`
   dentro de `registerEthernetInterface`). Birth §4.
3. `IOInterfaceNamePrefix = "awdl"` (RegInfo+0x30) y `IOInterfaceUnit = 0` (RegInfo+0x38,
   `OSNumber` 32 b). Birth §3.2, §4.
4. Ifnet con prefijo `awdl`, unit 0, `IFT_ETHER`, MAC del payload, `ifnet_attach` hecho e índice
   BSD vivo (el descubridor lo comprueba con `getifaddrs`/`AF_LINK`, §1.2).
5. **`IOInterfaceName = "awdl0"`** — la propiedad del matching. En Broadcom la publica el BSD
   client dentro del `registerService(2)` síncrono (`0xffffff800172982a`) y la republish
   `prepareBSDInterface` (`0xffffff80022f63eb`); en un kext propio sin ese driver el orden mínimo
   correcto es: properties + ifnet attach + `IOInterfaceName` + `IOInterfaceUnit` + rol **antes**
   de `registerService` (birth §7.3). `registerService` síncrono (opciones 2, como Broadcom) deja
   el attach completo antes de devolver el control al handler.
6. `IOInterfaceType = 6` (`IFT_ETHER`), `"BSD Name" = "awdl0"`, `IOSkywalkNexusUUID` — las
   publica el BSD client en Broadcom; un kext propio publica las equivalentes que apliquen. Birth §4.

### 3.3 `SIOCGIFMEDIA`: la palabra de medios que pasa el filtro

El descubridor exige `(ifm_current & 0xE0) == 0x80` (`IFM_IEEE80211`; ioctl en `w2p 0x100043cbf`,
filtro en `w2p 0x100043ccc`). `SIOCGIFMEDIA` = `0xC02C6938` (variante legacy `0xC0286938`); en
Broadcom responde `IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`) leyendo las
semillas `ifm_active = 0x80` / `ifm_current = 8` / `ifm_status = 0` del `RegistrationInfo+0x24`,
semillas con las que **el filtro no pasaría** — en un sistema real `ifm_current` es actualizado
antes de la lectura del daemon, actualizador no identificado (`?`,
[`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §6.3). Para el kext la consecuencia no
depende de ese hueco: la `if_ioctl` propia de `awdl0` debe responder `SIOCGIFMEDIA`/
`OSIOCGIFMEDIA` con `ifm_current` e `ifm_active` de tipo `IFM_IEEE80211` (p. ej. `0x80`),
`ifm_status` con `IFM_AVALID` cuando proceda y una lista de palabras 802.11.

### 3.4 `RegistrationInfo` y datapath mínimo de registro

`IOSkywalkEthernetInterface::RegistrationInfo` de **0x130 B**, versión 1
(`initRegistrationInfo`, `0xffffff8002b108e2`, valida exactamente 1/0x130), con los valores
Broadcom del nacimiento como referencia ABI:
`+0x0C = 3` (subfamilia), `+0x14 = 2` (número de palabras de medios), `+0x24 = 0x80`
(`IFM_IEEE80211`) y `+0x28 = 8`, `+0x30 = "awdl"`, `+0x38 = 0` (unit), `+0x3A = 64` (u16),
`+0x40 = 4` o `6`, `+0x48/+0x4C = 0x594` (MTU), `+0x108 = MAC` (6 B).
([`AWDL_INTERFACE_BIRTH.md`](AWDL_INTERFACE_BIRTH.md) §2.2, §3; datapath §2, §3.6.)

Para registrarse contra Skywalk hacen falta las colas del §1.6: **4 TX submission (una por AC,
mapa {0,1,2,3}), 1 TX completion, 1 RX completion**, multicast opcional, y **un pool compartido**
(`maxBuffersPerPacket = 1`, flags = 1; los tamaños Broadcom — 4864×2048, capacidades 512/1024 —
son defaults ligados a su firmware; un driver propio puede elegir otros respetando los invariantes:
paquetes del mismo pool que la cola, lotes `enqueuePackets` menores que la capacidad). El registro
es `IOSkywalkEthernetInterface::registerEthernetInterface` (`0xffffff8002b10a84`); la capa RTL usa
hoy el wrapper Infra con 3 colas, y para role 6 el camino observado es el registro directo con el
conjunto completo. Las colas deben quedar habilitables por `enable`/`disable` de la interfaz
(patrón `IO80211WorkQueue::addEventSource` + `IOEventSource` enable/disable +
`requestDequeue/requestEnqueue(0,0)`, datapath §8).

### 3.5 Clase de la interfaz y door de creación

- Puerta: el override `setVIRTUAL_IF_CREATE` (slot 556 / +0x1160) sobre el objeto alcanzable por el
  `safeMetaCast` a `IO80211InfraProtocol` del puente (en Broadcom aterriza en
  `AppleBCMWLANCore::setVIRTUAL_IF_CREATE`, `0xffffff80016788f4`). En la capa, `RTLAirController`
  ya declara la virtual en el header local; verificar que su jerarquía de stubs la haga castable a
  `IO80211InfraProtocol` (`?` — el cast exacto del objeto receptor no se re-verificó aquí).
- Clase de `awdl0`: subclase propia de `IO80211AWDLProtocol` (jerarquía Broadcom:
  `AppleBCMWLANProximityInterface : IO80211AWDLProtocol : IO80211VirtualInterface :
  IO80211SkywalkInterface : IOSkywalkEthernetInterface : IOSkywalkNetworkInterface`). Es
  imprescindible heredar de `IO80211AWDLProtocol` (no de `IO80211VirtualInterface`) para que los
  `safeMetaCast` de los puentes AWDL la alcancen; el coste es implementar los slots puros de §1.5
  (490 y 507–569) aunque sea con stubs devolviendo 102/`0xE00002C7` en los tipos que no interesan
  todavía. Derivar de `IO80211VirtualInterface` a secas **no** recibiría las requests AWDL y no
  está verificado que la familia lo acepte (`?`).
- Validación del 94 en el override: `len == 32`, rol 6, MAC presente; una sola interfaz AWDL
  (Broadcom devuelve 17 si ya existe, con el nombre escrito igualmente); rollback completo
  (deregister → stop → detach → release) ante cualquier fallo antes de escribir el nombre; escribir
  el nombre y devolver 0 solo cuando el ifnet y las properties estén publicados.

### 3.6 Lo que queda fuera de la etapa 1

- **Tipo 293 / `llw0`: sin implementar.** El dispatch devuelve 102 y `wifip2pd` lo absorbe sin
  tocar `awdl0` (§1.8). Alternativas peores: devolver éxito sin crear nada cuesta 30 s por intento;
  implementarlo es trabajo de la etapa 4.
- **Ioctls `'i'` 130/131 con tipo 7708:** el despacho de `IO80211Family` no puede servirlos
  (`handleIoctlGated` `0xffffff8002331d20` / `performGatedCommandIOCTL` `0xffffff80022f779a` solo
  aceptan las constantes 200/201 y devuelven `0xE00002C7`); nada que implementar
  (POST_CREATE §2.5).
- **Emisión de eventos de operación** (46/47/48/195/…): no condicionan la aceptación. Solo el
  registro del monitoreo debe devolver 0 (§1.7).

## 4. Etapa 2 — sincronización y elección (pares detectables, sin datapath)

Objetivo: que la interfaz participe en la sincronía AWDL (máquina de estados de sync/elección) y
exponga estado coherente, requisito previo a que los pares aparezcan.

- **Superficie AWDL por slots.** Contrato cerrado por tipo en dispatch §5 y structs §4: los
  generales 1 (SSID), 4 (CHANNEL), 9 (BSSID), 13 (STATE) llegan por cascada a +0xFC8..+0xFE0, y
  los específicos 116, 117, 118, 122, 125, 126, 129 (400 B), 130, 132, 134, 135, 136, 143, 149,
  151, 155, 158, 164 (184 B), 205, 208, 213, 219, 220, 225, 226, 229 (44 B), 247, 262, 263, 293,
  338, 341, 342, 344, 411, 414, 418, 432, 467 tienen slot GET/SET con struct y longitud definidos.
  Los solo-PeerManager (120, 127, 131, 133, 138, 139, 148, 163, 167, 201, 202, 203, 204, 217, 218,
  223, 233, 244, 249, 253, 340, 343, 351, 370, 415, 416, 417, 419, 420) los resuelve la familia
  contra su propio estado. Quién los dispara: no `wifip2pd` (structs §2) sino la ruta interna de
  glue y otros clientes; el calendario concreto de esa etapa es `?`.
- **Peer manager.** `IO80211SkywalkInterface::start` (`0xffffff80022f5792`) crea uno propio para
  rol ≠ 2; cómo se vincula ese objeto de la familia a una interfaz de un driver externo
  (ivars +0x120/+0x28) no está verificado (`?`, dispatch §8.3) — es el abierto principal de la
  etapa.
- **Eventos a emitir** (los que alimentan estado en el daemon): **46** `AWDL_SYNC_STATE_CHANGED`
  (16 B: u32 estado @0 — 1 master, 2 slave, 3 non-election master, 0 down), **47**
  `PEER_PRESENCE` (0x44 B: MAC @0, u32 @8), **195** `AWDL_CHAN_SEQ_CHANGED`, y estadísticas
  65/142/149/243. El helper de la familia es `postSyncStateChanged`
  (`0xffffff8002332054`, id 46, len 0); la emisión propia va por `postMessage` → `postMessageSync`.
- **Trabajo de radio** (no cubierto por los docs de despacho): canales sociales, ventana de
  disponibilidad y elección con TLVs de sync — la parte genuinamente nueva en el RTL8814; las
  piezas Broadcom equivalentes (Taskmaster, Commander, Tx Power Manager, defaults de la init en
  `0xffffff8001718db0`) marcan la forma. Referencia complementaria:
  [`AWDL_FIRMWARE_DUTIES.md`](AWDL_FIRMWARE_DUTIES.md).

## 5. Etapa 3 — action frames (descubrimiento de pares)

- **RX por evento 48** `APPLE80211_M_AWDL_ACTION_FRAME_RX`: payload `frameLen + 72` con la
  cabecera `apple80211_act_frm_hdr` (72 B) @0 y el frame crudo @72; emisor de referencia
  `IO80211AWDLPeerManager::postAFToP2pd` (`0xffffff800220fc2c`, id 48 vía el virtual +0xB18). El
  case 48 del daemon exige `len >= 0x48` y decodifica el header de 72 B antes del frame
  (EVENTS §4.2). Canal paralelo: tap BPF con DLT 0xC (`bpfTapInput` `0xffffff8002331070`; en
  Broadcom lo ejercita `handleActionFrame_rx` `0xffffff800171795c`).
- **Presencia de pares:** evento **47** con el payload de 0x44 B (§4.2 del daemon: valida MAC @0 y
  u32 @8 == 1; semántica exacta del u32 `?`).
- **TX de action frames:** la vía Broadcom es firmware-interna y no pasa por una request
  apple80211 identificable en los documentos fuente (`?`) — decisión de diseño abierta: o la
  familia lo pide por la ruta interna (tipo 120 `AWDL_PEER_SERVICE_REQUEST` u otros), o el kext
  necesita un mecanismo propio.
- **Requests de pares asociadas:** 229 (`awdl_peer_advert_add`, 44 B), 164 (traffic registration,
  184 B), 122 (election id), 126 (election metric), 129 (chan sequence, 400 B) — mismas tablas de
  dispatch §5/structs §4.

## 6. Etapa 4 — datapath y baja latencia

- **TX real por los 4 AC:** consumir de las TxSubmissionQueue, clasificar por destino (Broadcom:
  flow queues por MAC, máx. 16, atadas a flow rings de firmware — `dequeuePackets`
  `0xffffff80015420f0`; el header SNAP Broadcom es específico de su firmware, no imitable) y
  devolver paquetes por la TX completion. Multicast AWDL: par original+duplicado para el segundo
  canal social (`stashMulticastPacketPair`), cola multicast y `triggerAWDLMulticastTx`/`p2pMulticastTx`
  (datapath §9).
- **RX real:** entrega en la RxCompletionQueue registrada (`enqueuePackets`), filtros VLAN/SNAP y
  headroom 64; pendientes y flush (`stagePacket`/`freePendingPackets`/`flushPackets`,
  `sendPendingPacketsToStack` `0xffffff800172a716`) (datapath §10–§11).
- **`llw0` (tipo 293):** solo si se quiere el datapath de baja latencia. Contrato cerrado: payload
  24 B con `op` @+4 (1 = crear), nombre de salida @+8, prefijo `llw`, rol que publique
  `IO80211InterfaceRole = "LowLatency"`, y garantía de que `if_nametoindex("llw0")` resuelva
  (LLW §3.2–§4). Destrucción por `op = 0` (valores distintos de 0/1 son no-op que devuelven 0).
- **MTU 0x594 + headroom 64** ya fijados en la etapa 1 se vuelven funcionales aquí.

## 7. Riesgos y preguntas abiertas

- **Slots puros / vtable:** una subclase instanciable de `IO80211AWDLProtocol` debe implementar los
  slots 490 y 507–569 (y, según dispatch §7, también +0x11C0) — los layouts y firmas están en
  [`AWDL_PURE_SLOTS.md`](AWDL_PURE_SLOTS.md) y [`AWDL_STRUCTS.md`](AWDL_STRUCTS.md); llamar un slot
  sin implementar aborta (`__cxa_pure_virtual`). El recuento fino 570 (+0x11C0) discrepa entre
  ambos documentos (`?`).
- **Cast del controlador:** el 94 llega por `safeMetaCast` a `IO80211InfraProtocol` con slot
  +0x1160; el objeto receptor exacto (controlador frente a interfaz principal) y la castabilidad de
  `RTLAirController` quedan por verificar (`?`).
- **`ifm_current` de Broadcom:** el actualizador entre nacimiento y lectura del daemon no está
  identificado (`?`, birth §6.3); el diseño lo esquiva exigiendo la respuesta correcta de
  `SIOCGIFMEDIA` en la `if_ioctl` propia (§3.3).
- **Vínculo PeerManager ↔ interfaz externa** (etapa 2) y **vía de TX de action frames** (etapa 3):
  abiertos (`?`).
- **Compatibilidad entre builds de macOS:** las direcciones y offsets de este documento son del KC
  25.6.0; la capa ya gatea builds, y cada salto de versión exige re-verificar tablas y slots.
- **Hardware:** ninguna conclusión implica prueba en RTL8814; las etapas se validan en ese orden
  precisamente para acotar el radio de un fallo.

## 8. Re-verificación de direcciones de este documento

Pasada de confirmación con IDA 9.4 headless (`idat -A -S`) sobre copias frescas de los IDB en
`Build/ida_awdl/task_awdl_design/kc/` (`BootKernelExtensions.i64`) y `/p2p/` (`wifip2pd.i64`);
scripts `verify_kc.py`/`verify_p2p.py`, salidas `verify_out.txt` de cada directorio, logs
`run.log`. Confirmado en esta pasada: los cuatro slots de tabla (12/97/94/293 con sus handlers
`0xffffff8002279e89`, `0xffffff800227b7ff`, `0xffffff80022886a8`, `0xffffff800228b4b8`), los
nombres de todas las funciones del kernel citadas, los slots de la vtable Broadcom +0xFC8, +0xFD0,
+0xFD8, +0xFE0, +0xFE8, +0x10A0, +0x1180, +0x11B8, +0x11C0, +0x11C8, los literales `"awdl"`
(`0xffffff8001797376`) y las parejas de roles (`AirLink`/`IO80211VirtualInterfaceRole` índice 5,
`LowLatency`/`IO80211InterfaceRole` índice 1), las entradas 46/47/48/65/195/243 de
`gAppleMIndexToString`, la lectura del rol (`0xffffff800167891c`), el `strlcpy` a payload+16
(`0xffffff8001678a2b`), el `registerService(2)` (`0xffffff8001729825`+`0xffffff800172982a`), la
escritura del MTU 0x594 (`0xffffff80017296ff`–`0xffffff8001729709`), y en el daemon los call sites
de `Apple80211RawGet/RawSet` (`0x10004811d`, `0x100048260`, `0x1000483a8`, `0x100046db9`,
`0x100048d95`, `0x10004b3e6`), el `signal` (`0x100046d03`), las esperas de 30 s (`0x1000491a8`,
`0x100048b13`), el filtro de medios (`0x100043ccc`, máscara `0xE0` en `0x100043cd6`), las pruebas
de puerta (`0x1000483b5` byte +8; `0x10004812a`/`0x10004826d` byte +0x0D), los roles 6/9/0xA
(`0x100048cc8`/`0x100048c66`/`0x100048c97`), el tipo 94/len 32 (`0x100048d6d`/`0x100048d77`), los
timeouts 0x1E (`0x10004913b`, `0x100048aa6`), las señales (`0x1000451f2`, `0x100045281`) y el
literal `"llw0"` (`0x10004b814`). No verificado aquí y citado solo por su documento de origen:
todo lo marcado `?` en el texto.
