# Enlace del AirLayer contra las clases AWDL de IO80211Family

Este documento fija qué símbolos de `IO80211VirtualInterface`, `IO80211AWDLPeerManager`,
`IO80211AWDLProtocol` y clases anexas exporta IO80211Family en el kernelcollection de arranque,
por dónde puede enlazarlos un kext de terceros, qué importa hoy `RTL8814AirLayer.kext`, qué
añadiría la etapa 2 (reutilizar el `IO80211AWDLPeerManager` del kernel) y qué fallbacks quedan si
un build futuro deja de exportarlos. Complementa `AWDL_PEERMANAGER_DRIVER_ABI.md` (el contrato de
bytes del manager) y `AWDL_PEERMANAGER_SOFTWARE.md` (qué calcula).

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin 25.6.0, md5
`f489ff8be80e6013a1bf47561dcaef45`): volcado de la tabla de símbolos del fileset entry
`com.apple.iokit.IO80211Family` y del entry `com.apple.kernel`, más cotejo de cada dirección citada
contra el IDB en `Build/ida_awdl/task_awdl_linkage/kc.i64` (volcados y script de verificación en
`Build/ida_awdl/task_awdl_linkage/`). Todas las direcciones son VA del kernelcache sin deslizar.
Los offsets y la composición de la tabla pertenecen a esta build. Lo no confirmado se marca `?`.

---

## 0. Resumen

- **Todo lo que la etapa 2 necesita está exportado con nombre** en la tabla del entry
  `com.apple.iokit.IO80211Family`: vtables `__ZTV`, `gMetaClass`, constructores `C1`/`C2`,
  factorías (`withInterface`, `createPeerManager`) y los métodos del contrato
  (`actionFrameInput`, `bpfTapInput`, `reportDataPathEvents`, `dequeueTxPacket`,
  `dequeueTxPackets`, `sendToBpfTap`). No hay que resolver nada por dirección para el camino
  directo.
- La vía de enlace es la que el AirLayer ya usa: `OSBundleLibraries` →
  `com.apple.iokit.IO80211Family` (`1.0`), ya declarado en `Source/AirLayer/Info.plist`. El
  cargador de kexts resuelve los undefineds contra la tabla del entry al cargar; ninguna dirección
  de IO80211Family queda horneada en el binario.
- Verificación empírica: los 230 símbolos IO80211/CC que importa hoy el binario construido
  (`Build/RTL8814AirLayer.kext`) aparecen en esas tablas; ninguno AWDL todavía.
- Riesgo real: IO80211Family no es KPI y la composición de la tabla cambia entre builds (hoy
  4 433 entradas del entry ya son privadas). Los fallbacks de §3 sólo aplican si un build futuro
  privatiza o elimina un símbolo; el fallo de resolución impide la carga del kext, no provoca
  fallo en ejecución.

---

## 1. Qué exporta el entry y por dónde se enlaza

### 1.1 Mecanismo

El KC de arranque es un Mach-O `MH_FILESET` con un entry por kext. Cada entry lleva su propia
tabla de símbolos en `__LINKEDIT`; el entry `com.apple.iokit.IO80211Family` ocupa
`__TEXT` `0xffffff8002183000`–`0xffffff8002450000` y `__DATA` desde `0xffffff8002450000`, con
20 001 entradas repartidas así por byte de tipo:

| Tipo | Significado | Entradas |
|---|---|---:|
| `0x0f` | `N_SECT\|N_EXT`: **exportadas**, enlazables por kexts | 11 951 |
| `0x1e` | `N_PEXT\|N_SECT`: privadas (sólo localización interna) | 4 433 |
| `0x0e` | `N_SECT` locales | 2 614 |
| `0x01` | stubs hacia otros kexts (valor 0; p.ej. `CCPipe::withOwnerNameCapacity` y `CCStream::withPipeAndName`, que define el entry `com.apple.driver.corecapture`) | 1 003 |

Un kext declara la dependencia con la clave `OSBundleLibraries` → `com.apple.iokit.IO80211Family`
(versión `1.0`) y el cargador resuelve cada undefined contra las entradas `0x0f` de esa tabla. La
lista `BootKernelExtensions.kc.elides` no incluye IO80211Family: nada del entry está elidido.

Que `0x0f` = enlazable no es una suposición: los 230 undefineds IO80211/CC del binario AirLayer
construido aparecen en las tablas (225 en el entry IO80211Family, todos `0x0f`; los 5 restantes en
`com.apple.driver.corecapture`), y ese binario carga en 25G241 (build validada en
`KnownBuilds.inc`).

Las clases modelo de Apple (`AppleBCMWLANProximityInterface`, `AppleBCMWLANCore`,
`AppleBCMWLANInfraProtocol`) viven en otro entry (`com.apple.driver.AppleBCMWLANCoreMac`) y no
hacen falta para el enlace; son sólo la referencia de comportamiento.

### 1.2 Símbolos por clase (direcciones verificadas)

Símbolos exportados con nombre propio de cada clase (incluye métodos `const`):
`IO80211AWDLPeerManager` 995, `IO80211PeerManager` (base) 413, `IO80211AWDLPeer` 307,
`IO80211VirtualInterface` 179, `IO80211Buffer` 42, `IO80211AWDLProtocol` 15. Selección de los
relevantes para la etapa 2:

`IO80211AWDLPeerManager` (vtable en `0xffffff800245a858`; el `vptr` de un objeto vivo apunta a
`__ZTV+0x10`, es decir `0xffffff800245a868`):

| Símbolo | Dirección |
|---|---|
| `__ZTV22IO80211AWDLPeerManager` | `0xffffff800245a858` |
| `IO80211AWDLPeerManager::gMetaClass` | `0xffffff800248d2e0` |
| `IO80211AWDLPeerManager::withInterface(IO80211VirtualInterface*, ether_addr*)` | `0xffffff80021c14bc` |
| `IO80211AWDLPeerManager::actionFrameInput(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff800220e1ae` |
| `IO80211AWDLPeerManager::reportDataPathEvents(unsigned, void*, size_t)` | `0xffffff80021dd8f4` |
| `IO80211AWDLPeerManager::dequeueTxPacket(TxPacketRequest*)` | `0xffffff800221675e` |
| `IO80211AWDLPeerManager::enableAWDL()` | `0xffffff80021dcf58` |
| `IO80211AWDLPeerManager::IO80211AWDLPeerManager()` (C1/C2) | `0xffffff80021c145c` / `0xffffff80021c148c` |
| `IO80211AWDLPeerManager::MetaClass::MetaClass()` (C1/C2) | `0xffffff80021c12f0` / `0xffffff80021c13ce` |
| `__ZTVN22IO80211AWDLPeerManager9MetaClassE` | `0xffffff800245ab98` |

`IO80211VirtualInterface`:

| Símbolo | Dirección |
|---|---|
| `__ZTV23IO80211VirtualInterface` | `0xffffff8002472b78` |
| `IO80211VirtualInterface::gMetaClass` | `0xffffff800248e478` |
| `IO80211VirtualInterface::IO80211VirtualInterface(OSMetaClass*)` (C2) | `0xffffff800233003e` |
| `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff8002331070` |
| `IO80211VirtualInterface::bpfTapInput(mbuf*, unsigned, void*, size_t)` | `0xffffff8002330e7a` |
| `IO80211VirtualInterface::dequeueTxPackets(TxPacketRequest*)` | `0xffffff80023313b2` |
| `IO80211VirtualInterface::sendToBpfTap(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff8002331100` |
| `IO80211VirtualInterface::peerToPeerConfigureIfnet()` | `0xffffff8002331218` |
| `IO80211VirtualInterface::createPeerManager()` | `0xffffff80023308c2` |

`IO80211AWDLProtocol` y clases anexas:

| Símbolo | Dirección |
|---|---|
| `__ZTV19IO80211AWDLProtocol` | `0xffffff8002456440` |
| `IO80211AWDLProtocol::gMetaClass` | `0xffffff800248cde8` |
| `IO80211AWDLProtocol::IO80211AWDLProtocol(OSMetaClass*)` (C2) | `0xffffff8002198c04` |
| `IO80211AWDLProtocol::~IO80211AWDLProtocol()` (D0/D1/D2) | `0xffffff800239ef2a` / `0xffffff800239ef24` / `0xffffff8002198c24` |
| `IO80211AWDLProtocol::MetaClass::alloc()` | `0xffffff8002198c6e` |
| `__ZTV18IO80211PeerManager` (base) | `0xffffff800245d070` |
| `IO80211PeerManager::gMetaClass` (base) | `0xffffff800248d508` |
| `IO80211PeerManager::withInterface(IO80211VirtualInterface*, IO80211SkywalkInterface*)` | `0xffffff8002246c16` |
| `__ZTV15IO80211AWDLPeer` | `0xffffff8002459f80` |
| `IO80211AWDLPeer::gMetaClass` | `0xffffff800248d060` |
| `IO80211AWDLPeer::withAddressAndManager(unsigned char*, IO80211AWDLPeerManager*)` | `0xffffff80021a8dac` |
| `__ZTV13IO80211Buffer` | `0xffffff8002473e80` |
| `IO80211Buffer::gMetaClass` | `0xffffff800248e4d0` |

Símbolos de creación que el AirLayer **ya importa hoy** (también `0x0f`):

| Símbolo | Dirección |
|---|---|
| `IO80211SkywalkInterface::createPeerManager()` | `0xffffff80022f7be6` |
| `IO80211Controller::createVirtualInterface(ether_addr, unsigned)` | `0xffffff800229612e` |
| `IO80211Controller::attachVirtualInterface(IO80211VirtualInterface**, ether_addr, unsigned char, bool)` | `0xffffff80022960aa` |

### 1.3 Vtables y fixups

La convención de ranuras es la de `AWDL_PEERMANAGER_DRIVER_ABI.md`: `vptr = __ZTV + 0x10` y la
celda *n* vive en `vptr + 8n`. Verificado sobre la vtable del manager leyendo el IDB (los fixups
encadenados del KC ya aplicados): `ZTV+0x10+0x238` contiene `0xffffff800220e1ae`
(`actionFrameInput`), `+0x298` contiene `0xffffff80021dd8f4` (`reportDataPathEvents`) y `+0x2A8`
contiene `0xffffff800221675e` (`dequeueTxPacket`). En el KC en disco esos punteros van codificados
como fixups encadenados (base `__HIB` `0xffffff8000100000`); en la memoria del kernel en vivo son
VA directos, de modo que la lectura `*(void**)objeto → +0x10 + 8n` funciona sin decodificación.

Ejemplos de símbolos **no** exportados (locales `0x0e`, sin enlace posible): los destructores de la
metaclase (`IO80211AWDLProtocol::MetaClass::~` en `0xffffff8002198bfa`/`0xffffff8002198ca2`), el
estático `__ZL23IO80211AWDLProtocol_ktv` (`0xffffff8002492ec0`) y los inicializadores
`__GLOBAL__sub_I_*`. Ninguno hace falta para las rutas de la etapa 2.

---

## 2. Imports actuales del AirLayer y necesidades de la etapa 2

### 2.1 Hoy (binario 0.32, build 25G241)

La tabla de undefineds del binario construido tiene 800 entradas; 230 son de IO80211Family y
corecapture, resueltas así:

- 225 contra el entry `com.apple.iokit.IO80211Family`, todas `0x0f`: la práctica totalidad de
  `IO80211Controller` (~130 métodos, `gMetaClass`, `C2`/`D2`, `_RESERVED*`), ~100 de
  `IO80211SkywalkInterface` (incluidos `createPeerManager`, `createPeer`, `getP2PSkywalkPeerMgr`,
  `reportDataPathEvents`), el trío de subclasificación de `IO80211InfraProtocol` (`gMetaClass`,
  `C2(OSMetaClass*)`, `D2`), ~90 de `IO80211InfraInterface`, más `__ZTV21IO80211InfraInterface` y
  `__ZTV23IO80211SkywalkInterface`.
- 5 contra `com.apple.driver.corecapture` (`CCPipe`, `CCStream`, `CCFaultReporter`; dos de ellos
  llegan además como stubs `0x01` en la tabla de IO80211Family).
- El resto: IOSkywalkFamily (85), KPI del kernel (`OSMetaClass`, `IORegistryEntry`, `IOWorkLoop`,
  `getMetaClassWithName` incluido, usado por el gate de versiones) y KPI bsd (mbuf, etc.).

Ningún undefined es hoy de una clase AWDL (`IO80211AWDL*`, `IO80211VirtualInterface`): 0.

### 2.2 Extras de la etapa 2

Todos verificados `0x0f`; los marcados «ya» ya están enlazados:

| Papel | Símbolo | Dirección |
|---|---|---|
| Crear la interfaz virtual AWDL | `IO80211Controller::createVirtualInterface` | ya (`0xffffff800229612e`) |
| Adjuntarla | `IO80211Controller::attachVirtualInterface` | ya (`0xffffff80022960aa`) |
| Crear el manager del kernel en esa interfaz | `IO80211AWDLPeerManager::withInterface(IO80211VirtualInterface*, ether_addr*)` | `0xffffff80021c14bc` |
| Factoría que heredan las interfaces AWDL (llama a `withInterface`) | `IO80211VirtualInterface::createPeerManager` | `0xffffff80023308c2` |
| Variante de la clase Skywalk (otra ranura, no crea el manager AWDL) | `IO80211SkywalkInterface::createPeerManager` | ya (`0xffffff80022f7be6`) |
| Cast seguro del manager leído de la interfaz | `IO80211AWDLPeerManager::gMetaClass` (para `safeMetaCast`/`OSDynamicCast`) | `0xffffff800248d2e0` |
| Cast de interfaces | `IO80211VirtualInterface::gMetaClass` | `0xffffff800248e478` |
| Inyección de action frames | `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*, unsigned, void*, size_t)` | `0xffffff8002331070` |
| Pool de buffers para esa inyección | `__ZTV13IO80211Buffer` + `IO80211Buffer::gMetaClass` | `0xffffff8002473e80` / `0xffffff800248e4d0` |
| Subclasificar el protocolo AWDL (patrón `IO80211InfraProtocol` actual) | `IO80211AWDLProtocol::gMetaClass` + `C2(OSMetaClass*)` + `D2` | `0xffffff800248cde8` / `0xffffff8002198c04` / `0xffffff8002198c24` |
| Llamada directa a la entrada de AF (alternativa a `bpfTapInput`) | `IO80211AWDLPeerManager::actionFrameInput` | `0xffffff800220e1ae` |
| Eventos de datapath 41/42/43 (si no van por `postMessage`) | `IO80211AWDLPeerManager::reportDataPathEvents` | `0xffffff80021dd8f4` |

No hacen falta: `C1`/`C2` del manager (lo crea `withInterface`), `IO80211AWDLPeer::withAddressAndManager`
(el manager da de alta los peers él solo, §1.6 del doc de ABI) ni `dequeueTxPacket` (el tirón lo
inicia Skywalk sobre la interfaz).

---

## 3. Fallbacks en runtime

Con la tabla actual no hacen falta; quedan aquí para builds futuros donde un símbolo aparezca como
`0x1e`/`0x0e` o desaparezca. Todos los de esta sección son del entry `com.apple.kernel` (KPI
libkern, `0x0f`) y por tanto estables de una forma en que IO80211Family no lo es:

| Primitiva | Símbolo | Dirección |
|---|---|---|
| Metaclass por nombre | `OSMetaClass::getMetaClassWithName(const OSSymbol*)` | `0xffffff8000a1bef0` (ya importado por el gate) |
| Instanciación por nombre | `OSMetaClass::allocClassWithName(const char*)` | `0xffffff8000a1c070` |
| Instanciación por nombre | `OSMetaClass::allocClassWithName(const OSSymbol*)` | `0xffffff8000a1bfc0` |
| Cast por nombre (equivalente runtime de `OSDynamicCast` sin `gMetaClass` enlazado) | `OSMetaClassBase::metaCast(const char*)` | `0xffffff8000a1aec0` |
| Cast entre metaclasses | `OSMetaClassBase::safeMetaCast` | `0xffffff8000a1ab30` |
| Comprobación de cast por nombre | `OSMetaClass::checkMetaCastWithName(const char*, const OSMetaClassBase*)` | `0xffffff8000a1c140` |
| Fingerprint del gate | `OSMetaClass::getClassSize()` / `getSuperClass()` / `getClassName()` | `0xffffff8000a1b200` / `0xffffff8000a1c1e0` / `0xffffff8000a1b1d0` |

Combinación posible sin un solo símbolo de clase AWDL: `getMetaClassWithName("IO80211AWDLPeerManager")`
→ `allocClassWithName` o la ranura `alloc` de su metaclase → llamada a métodos por ranura de vtable
leída del objeto vivo (`vptr+0x10+8n`, punteros ya resueltos en memoria), con `metaCast` para
verificar el tipo antes de cada uso.

Riesgos:

- **Índices de ranura cambian entre builds** (los offsets +0x238/+0x298/+0x2A8 son de 25G241); el
  gate de builds existente (`VersionGate.h` + `KnownBuilds.inc`, única validada 25G241) es la
  mitigación, ampliable con el fingerprint de tamaños de las clases AWDL.
- **Sin comprobación de tipos ni firmas**: una firma equivocada corrompe o trapea (recordatorio:
  `actionFrameInput` ejecuta un `ud1` deliberado si `hdrLen > 0x48`).
- **Privatización silenciosa**: un símbolo pasado a `0x1e` no da síntoma en ejecución; rompe la
  resolución del kext y el kext no carga. Conviene re-verificar la tabla (volcado del entry) tras
  cada update, igual que se re-verifican las vtables.
- **El fallback de vtable no cubre la creación**: sin `withInterface`/`createVirtualInterface`
  exportados no hay forma limpia de construir el manager ni la interfaz; sólo quedaría reutilizar
  los objetos que el propio sistema cree (`?` sin explorar).

---

## 4. Tabla resumen

«ST» = tabla de símbolos del fileset entry indicado (tipo `0x0f` salvo nota); «IDB» = dirección
cotejada además contra el IDB del KC; «bin» = importado hoy por `Build/RTL8814AirLayer.kext`.

| Símbolo | Exportado | Dirección | Evidencia |
|---|---|---|---|
| `__ZTV22IO80211AWDLPeerManager` | sí | `0xffffff800245a858` | ST-IO80211Family + IDB |
| `__ZN22IO80211AWDLPeerManager10gMetaClassE` | sí | `0xffffff800248d2e0` | ST-IO80211Family + IDB |
| `IO80211AWDLPeerManager::withInterface` | sí | `0xffffff80021c14bc` | ST + IDB |
| `IO80211AWDLPeerManager::actionFrameInput` | sí | `0xffffff800220e1ae` | ST + IDB + celda 71 vtable |
| `IO80211AWDLPeerManager::reportDataPathEvents` | sí | `0xffffff80021dd8f4` | ST + celda 83 vtable |
| `IO80211AWDLPeerManager::dequeueTxPacket` | sí | `0xffffff800221675e` | ST + celda 85 vtable |
| `IO80211AWDLPeerManager::enableAWDL` | sí | `0xffffff80021dcf58` | ST |
| `IO80211AWDLPeerManager` C1/C2 | sí | `0xffffff80021c145c`/`0xffffff80021c148c` | ST |
| `__ZTV23IO80211VirtualInterface` | sí | `0xffffff8002472b78` | ST + IDB |
| `IO80211VirtualInterface::gMetaClass` | sí | `0xffffff800248e478` | ST + IDB |
| `IO80211VirtualInterface::C2(OSMetaClass*)` | sí | `0xffffff800233003e` | ST |
| `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*,…)` | sí | `0xffffff8002331070` | ST + IDB |
| `IO80211VirtualInterface::bpfTapInput(mbuf*,…)` | sí | `0xffffff8002330e7a` | ST |
| `IO80211VirtualInterface::dequeueTxPackets` | sí | `0xffffff80023313b2` | ST + IDB |
| `IO80211VirtualInterface::sendToBpfTap` | sí | `0xffffff8002331100` | ST |
| `IO80211VirtualInterface::createPeerManager` | sí | `0xffffff80023308c2` | ST + IDB |
| `__ZTV19IO80211AWDLProtocol` | sí | `0xffffff8002456440` | ST + IDB |
| `IO80211AWDLProtocol::gMetaClass` | sí | `0xffffff800248cde8` | ST + IDB |
| `IO80211AWDLProtocol::C2(OSMetaClass*)` / `D2` | sí | `0xffffff8002198c04` / `0xffffff8002198c24` | ST |
| `__ZTV18IO80211PeerManager` (base) | sí | `0xffffff800245d070` | ST + IDB |
| `__ZTV15IO80211AWDLPeer` | sí | `0xffffff8002459f80` | ST + IDB |
| `IO80211AWDLPeer::withAddressAndManager` | sí | `0xffffff80021a8dac` | ST |
| `__ZTV13IO80211Buffer` | sí | `0xffffff8002473e80` | ST |
| `IO80211SkywalkInterface::createPeerManager` | sí (ya) | `0xffffff80022f7be6` | ST + IDB + bin |
| `IO80211Controller::createVirtualInterface` | sí (ya) | `0xffffff800229612e` | ST + bin |
| `IO80211Controller::attachVirtualInterface` | sí (ya) | `0xffffff80022960aa` | ST + bin |
| `IO80211AWDLProtocol::MetaClass::~` (D1/D0) | no (local `0x0e`) | `0xffffff8002198bfa`/`0xffffff8002198ca2` | ST |
| `__ZL23IO80211AWDLProtocol_ktv` | no (local `0x0e`) | `0xffffff8002492ec0` | ST |
| `OSMetaClass::getMetaClassWithName` | sí (ya, kernel) | `0xffffff8000a1bef0` | ST-kernel + IDB + bin |
| `OSMetaClass::allocClassWithName(const char*)` | sí (kernel) | `0xffffff8000a1c070` | ST-kernel |
| `OSMetaClassBase::metaCast(const char*)` | sí (kernel) | `0xffffff8000a1aec0` | ST-kernel |
| `OSMetaClassBase::safeMetaCast` | sí (kernel) | `0xffffff8000a1ab30` | ST-kernel |
| `OSMetaClass::checkMetaCastWithName(const char*,…)` | sí (kernel) | `0xffffff8000a1c140` | ST-kernel |
| `OSMetaClass::getClassSize/getSuperClass/getClassName` | sí (kernel) | `0xffffff8000a1b200`/`0xffffff8000a1c1e0`/`0xffffff8000a1b1d0` | ST-kernel |
