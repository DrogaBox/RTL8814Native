# Creación y publicación del `IO80211AWDLPeerManager` para una `awdl0` de driver externo (rol 6)

Cierra los puntos abiertos 14 y 15 del índice (§8.2): cómo el kernel crea y publica el
`IO80211AWDLPeerManager` para una interfaz virtual rol 6 fabricada por un kext de terceros, dónde
queda el puntero y cómo lo recupera ese kext. Direcciones del KC 25.6.0 (25G241) salvo indicación;
las de 14.8.9/15.8.2 van en §9.

Resumen de la cadena (direcciones verificadas sobre el IDB, desensamblado o pseudocódigo; §10):

1. El 94 (`VIRTUAL_IF_CREATE`) termina en el driver, que fabrica la vif y la registra
   (§6). La creación del manager **no** ocurre en el 94.
2. Tras el registro, el `start` de la vif ejecuta `IO80211SkywalkInterface::start`
   (`0xffffff80022f5792`), que llama el **slot virtual `+0xB08`** (`createPeerManager`) en
   `0xffffff80022f5bca` y publica el resultado en **`ivars+0x28`** de la vif
   (`0xffffff80022f5bd0`–`0xffffff80022f5bd8`).
3. La factoría heredada `IO80211VirtualInterface::createPeerManager` (`0xffffff80023308c2`) elige
   por rol: 6 → `IO80211AWDLPeerManager::withInterface` (`0xffffff80021c14bc`), y registra la vif
   en el controlador en `*(ctrl+0x120)+0x190`.
4. Los puentes del despacho apple80211 recuperan el manager con
   `safeMetaCast(*(*(if+0x120)+0x28), &IO80211AWDLPeerManager::gMetaClass)` (§5).
5. El kext externo obtiene el puntero por las vías del §7; la que propone el plan (paso 5) vía
   `getP2PSkywalkPeerMgr` (`+0xCC0`) **no sirve** (§5.3).

## 1. Objetos y ivars implicados

Tres bloques de estado; los dos primeros cuelgan del ivar `+0x120` de sus dueños:

- **Ivars de `IO80211SkywalkInterface`** (0xF0 B, aloca `initIvars` `0xffffff80022f520c` con
  `IOMallocTypeImpl` y los deja en `this+0x120`; `initIvars` los borra con `memset_s` si ya
  existían). Campos con nombre:
  | Offset | Contenido | Evidencia |
  |---|---|---|
  | `+0x00` | byte de entitlement (`wlan.entitlement`) | `PE_parse_boot_argn` del `start` escribe `*(ivars)` (pseudocódigo; dirección exacta sin volcar, `?`) |
  | `+0x18` | `OSDictionary` (capacidad 5) | `start`, rama final |
  | `+0x28` | **puntero al PeerManager** | store `0xffffff80022f5bd8`; leído por los puentes (§5) |
  | `+0x30` | `IO80211Controller*` | `start`: `safeMetaCast(provider, IO80211Controller::gMetaClass)`; getter `getController` `0xffffff80022f957a` |
  | `+0x58` | rol (u32) | `setInterfaceRole` `0xffffff80022f60f0`: `*(ivars+88) = a2` + properties |
  | `+0x5C` | interfaceId (u32) | argumento de los logs del `start` |
  | `+0x68` | interfaz padre (rol 2 hereda su manager) | `start`, rama `role == 2` |
  | `+0x80` | `CCPipe` del interface log | `start` |
  | `+0xB8` | `OSString` del rol | `setInterfaceRole` |
  | `+0xC8` | `IO80211FlowQueueDatabase` | `start`, tras crear el manager |
  - El byte `+0x00` no se siguió instrucción a instrucción: offset tomado del pseudocódigo
    (`?` en su desensamblado fino).

- **Ivars de `IO80211Controller`** en `ctrl+0x120` (mismo patrón; el alloc no se siguió, `?`).
  Tabla de interfaces virtuales por rol, escrita por `createPeerManager`:
  | Offset | Contenido |
  |---|---|
  | `+0x190` (400) | la vif rol 6 (AWDL) |
  | `+0x198` (408) | la vif rol 7 (PeerManager genérico) |
  | `+0x3B0` (944) / `+0x3B8` (952) | las vif NAN (roles 8/9 y 10) |

- **Ivars del manager**: el objeto C++ son 40 B (`OSObject_typed_operator_new(&…_ktv, 40)` en
  `withInterface`); el estado vive en `this+0x18` (base, de `IO80211PeerManager::initWithInterface`
  `0xffffff8002246c88`) y `this+0x20` (AWDL, >66 KB, de `IO80211AWDLPeerManager::initWithInterface`
  `0xffffff80021c1540`; detalle en `AWDL_PEERMANAGER_SOFTWARE.md` §2.2). Backpointers escritos por
  la base: `+0x558` (1368) = `IO80211SkywalkInterface` — **nullptr** en el camino AWDL, la base se
  llama con `a3 = 0` desde `IO80211AWDLPeerManager::initWithInterface` (call sin volcar, `?`) — y
  `+0x560` (1376) = la `IO80211VirtualInterface`.

## 2. La factoría heredada: `IO80211VirtualInterface::createPeerManager` (slot `+0xB08`)

`0xffffff80023308c2` (celda `__ZTV23IO80211VirtualInterface+0xB08` = `0xffffff8002473690`).
Desensamblado verificado:

1. Lee el rol: `r14d = *(*(this+0x120)+0x58)` en `0xffffff80023308e0`.
2. Toma la MAC propia con el slot `+0xCF8` (`getSelfMacAddr`, `0xffffff80022f841c`) en
   `0xffffff80023308ee` y la deja en pila (6 B) para pasarla a la factoría.
3. Reparto por rol (`0xffffff80023308ff`–`0xffffff80023309d8`):
   - 6 → `IO80211AWDLPeerManager::withInterface(this, &mac)` (`0xffffff80021c14bc`), llamada en
     `0xffffff8002330957`. **Único xref de código de la factoría en todo el KC** (la única forma
     en que el kernel instancia el manager AWDL).
   - 7 → `IO80211PeerManager::withInterface(this, 0, …)` (llamada en `0xffffff8002330926`) con el
     log `"IO80211VirtualInterface::createPeerManager for role %d\n"` en `0xffffff800233091c`.
   - 8/9 → `IO80211NANPeerManager::withInterface`; 10 → `IO80211NANDataInterfacePeerManager::
     withInterface`.
   - Cualquier otro rol → devuelve 0.
4. Si la factoría devolvió manager, llama `getController` (slot `+0xBA8`,
   `0xffffff80022f957a`) y ejecuta `*(*(ctrl+0x120) + {0x190|0x198|0x3B0|0x3B8}) = this` — guarda
   **la interfaz** (no el manager) en la tabla del controlador; para el rol 6 el store es
   `mov ecx, 190h` en `0xffffff8002330974` y `mov [rax+rcx], rbx` en `0xffffff80023309df`.
5. Devuelve el manager (o 0). **No escribe `ivars+0x28`**: esa publicación la hace el `start` (§3).

`IO80211AWDLPeerManager::withInterface` (`0xffffff80021c14bc`): alloc tipado de 40 B, vtable
`0xffffff800245a868`, registro en `gMetaClass` (`0xffffff800248d2e0`) y `initWithInterface`
(`0xffffff80021c1540`); si el init devuelve false hace `release` y devuelve nullptr (el `start`
de la vif fallará, §4).

## 3. Publicación: `IO80211SkywalkInterface::start` escribe `ivars+0x28`

`0xffffff80022f5792`. Orden verificado:

1. Guard de doble arranque al entrar (primer enunciado del `start`; dirección exacta sin volcar,
   `?`): si `*(ivars+0x28) != 0` →
   `panic("\"Calling start twice in a row is not allowed\\n\" @%s:%d", "IO80211SkywalkInterface.cpp", 387)`.
   El manager se crea exactamente una vez, aquí.
2. `ivars+0x30 = safeMetaCast(provider, IO80211Controller::gMetaClass)` — el provider del `start`
   de la vif es el controlador que la registró.
3. Rama por rol:
   - `role == 2` (P2P): hereda el manager de la interfaz padre —
     `*(ivars+0x28) = *(*( *(ivars+0x68) + 0x120 ) + 0x28)`, con log
     `"Assigning peerManager %p for interfaceid %d, from ivars->parentInterface (%p) role %d"`.
   - resto de roles (incluido el 6): log `"Creating peer manager for %s, ifId %d, role %d"` en
     `0xffffff80022f5bbe` y **`call qword ptr [rax+0xB08h]` en `0xffffff80022f5bca`**; el resultado
     se publica con `mov rcx,[r12+120h]; mov [rcx+28h],rax` (`0xffffff80022f5bd0`–`0xffffff80022f5bd8`).
4. Si el manager es 0 → `jz` a `0xffffff80022f5de5` con log
   `"ERROR: Family Skywalk Interface start peer manager init fail"`: el `start` de la vif falla y
   la interfaz no llega arriba.
5. Sólo después crea la `IO80211FlowQueueDatabase` (`ivars+0xC8`).

Único llamador real del slot `+0xB08` entre los filesets con dependencia posible sobre
IO80211Family (§10): los otros cuatro sitios con el mismo desplazamiento golpean el slot `0xB08`
de **otras clases** — `AppleBCMWLANCore::watchdog` `0xffffff80016053b8`/`0xffffff8001605ae0` y
`AppleBCMWLANCore::getTRAP_INFO` `0xffffff8001674607` (`getTrapInfo` de su provider, según el
literal de log «ivars->fProvider->getTrapInfo») y `AppleSkywalkAVBLegacyEthernet::setupAVB`
`0xffffff80029cea3e` (sobre `AppleSkywalkAVBEthernetInterface`, getters AVB de colas realtime) —,
no de la interfaz. Consecuencia: en el camino Broadcom, la creación del manager también nace del
`start` de la vif con la factoría heredada — la vtable de `AppleBCMWLANProximityInterface` apunta
a la base en `+0xB08` (celda `0xffffff80017f5080` → `0xffffff80023308c2`).

## 4. Cuándo ocurre respecto al `VIRTUAL_IF_CREATE`

1. El 94 llega al puente del despacho (moderno `setVIRTUAL_IF_CREATE` `0xffffff80022886a8`; ioctl
   BSD vía `setVirtualInterfaceCreate` `0xffffff8002357f33`); ambos prueban primero la ruta WCL
   (`IO80211Glue::sendIOUCToWcl(0x802869C8, 0x5E, data, 32)`) y si no la consumen llaman
   `apple80211setVIRTUAL_IF_CREATE` (`0xffffff8002268494`), que hace `safeMetaCast` a
   `IO80211InfraProtocol::gMetaClass` y llama el slot virtual `+0x1160` del objeto principal.
2. El driver responde en ese slot: fabrica su vif rol 6 y la lleva arriba con
   `IO80211Controller::attachVirtualInterface(&vif, mac, rol, register=true)`
   (`0xffffff80022960aa`): slot `+0xDD8` `createVirtualInterface` (la base `0xffffff800229612e`
   **sólo sabe crear el rol 7 «ap»** vía `IO80211VirtualInterface::gMetaClass.alloc`; para el 6 el
   driver debe sobreescribirlo y devolver su propia clase), slot `+0x670` `attach`
   (`0xffffff800233023a`, plano `kIO80211PlaneName`) y, si el bool lo pide, `registerService`
   (slot `+0x5B0`).
3. El `registerService` dispara el matching de IOKit y el `start` de la vif con el controlador
   como provider: ahí corre §3. El orden de este tramo coincide con `AWDL_INTERFACE_BIRTH.md` §6.
4. El manager existe y está publicado en `ivars+0x28` antes de que el daemon hable con `awdl0`
   (`AWDL_WIFIP2PD_POST_CREATE.md`); los ioctls AWDL del daemon encuentran el manager por la ruta
   del §5.

## 5. Cómo recupera el kernel (y el driver) el puntero

### 5.1 Los puentes apple80211 (camino de los 27+43 tipos AWDL)

Patrón idéntico en los puentes get/set con cast a `IO80211AWDLPeerManager` (118 xrefs al
`gMetaClass` `0xffffff800248d2e0` en el KC: unos 95 puentes get/set y el resto consumidores propios
de la familia — `inputPacket`, `ScanManager`, `InfraInterface`… —; §10). Muestra verificada:

- `getAWDL_SYNC_PARAMS` `0xffffff800227bbcd` (ioctl 116) y `setAWDL_SYNC_ENABLED`
  `0xffffff800228889d` (ioctl 117):
  1. `a2` = la interfaz por la que entró el comando (la vif `awdl0` en las rutas descritas por
     `AWDL_APPLE80211_DISPATCH.md` §2);
  2. `v6 = *(*(a2+0x120)+0x28)`;
  3. si es nullptr → devuelve **45**; si `a2` es nullptr → 45; si el `len` del payload no cuadra → 22;
  4. `safeMetaCast(v6, &IO80211AWDLPeerManager::gMetaClass)`: si falla → **19**;
  5. si castea, llama el método del manager (`getAwdlSyncParams`/`setAwdlSyncEnabled`, estado del
     kernel, sin pasar por la vtable de la interfaz).
- `IO80211Controller::getInterfaceActiveBitmap` (`0xffffff800229a8c6`) recorre la otra dirección:
  vif desde `*(ctrl+0x120)+0x190` (y `+0x3B0` para NAN), manager desde `*(vif+0x120)+0x28`.
- `IO80211VirtualInterface::inputPacket` (`0xffffff8002333cac`) también castea a
  `IO80211AWDLPeerManager` (ruta RX; `AWDL_PEER_PRESENCE_PPP.md` §3).

### 5.2 Getter directo de la familia

`IO80211SkywalkInterface::getPeerManager` (`0xffffff80022f5fa8`): `return *(ivars+0x28)`. **Sin
ninguna xref** en el KC (no está en ninguna vtable de las tres clases consultadas ni lo llama
código alguno: helper sin consumidores, probablemente inlined en las ramas vivas). Está exportado
en los tres builds (§9), de modo que un kext puede enlazarlo y llamarlo sobre su vif.

### 5.3 `getP2PSkywalkPeerMgr` (slot `+0xCC0`): no sirve para obtener el manager

- Bases: `IO80211VirtualInterface::getP2PSkywalkPeerMgr` `0xffffff8002334c1a` y
  `IO80211SkywalkInterface::getP2PSkywalkPeerMgr` `0xffffff80022f8ab4` — **ambas `return 0`**.
- Único llamador del slot en el KC: `AppleBCMWLANBusInterfacePCIe::start` en
  `0xffffff800155a68c` (`call qword ptr [rax+0xCC0h]`); con resultado ≠ 0 reporta fallo de init
  (`reportInitFailure(..., 0x83A)`). Con las bases de la familia el valor es siempre 0 y la
  comprobación pasa; la clase exacta del objeto receptor (`rbx`) no se determinó (`?`).
- Corrige el plan (paso 5): la «alternativa sin override: `getP2PSkywalkPeerMgr`, slot `0xCC0`»
  no da el manager en 25.6.0. La lectura real sin override es la del §7.2.

## 6. Qué falla si la subclase no sobreescribe nada

| Pieza | Si se hereda la base | Evidencia |
|---|---|---|
| `createPeerManager` (`+0xB08`) | **Funciona**: para rol 6 la base construye el `IO80211AWDLPeerManager` real, registra la vif en `*(ctrl+0x120)+0x190` y devuelve el puntero. Es el camino Broadcom (§3). No hace falta enlazar ningún símbolo del manager. | `0xffffff80023308c2`; celda Broadcom `0xffffff80017f5080` |
| `getP2PSkywalkPeerMgr` (`+0xCC0`) | Funciona (devuelve 0 y nadie de la familia lo usa para entregar el manager; el único consumidor del árbol espera 0). Sobreescribirlo no aporta nada. | §5.3 |
| Override de `+0xB08` que devuelve 0 | El `start` de la vif falla («peer manager init fail»); `awdl0` no llega arriba. | `0xffffff80022f5bdc`–`0xffffff80022f5de5` |
| Override de `+0xB08` que devuelve un objeto que no castee a `IO80211AWDLPeerManager` | El arranque sigue, pero los puentes devuelven 19 para todos los tipos AWDL; el daemon no configura nada. | §5.1 |
| Override de `+0xB08` que devuelve un manager distinto del publicado | No hay segunda publicación: si el override no es el que el `start` guarda, los puentes no lo ven (leen `ivars+0x28`, no el retorno). | §3, §5.1 |
| `createVirtualInterface` (`+0xDD8`) heredada con rol 6 | Devuelve 0 (`if (a3 != 7) return 0`); el `attachVirtualInterface` falla y no hay vif. Para el rol 6 la subclase del controlador debe sobreescribirlo. | `0xffffff800229612e` |

## 7. Vías de captura del puntero para el kext externo

1. **Override del slot `+0xB08`** (`createPeerManager`, declarado en `AWDLInterface.h:110`):
   el override llama la implementación de la base (o directamente
   `IO80211AWDLPeerManager::withInterface(this, &mac)`, exportado en los tres builds) y guarda el
   retorno antes de devolverlo. Es lo que propone el paso 5 del plan; la publicación del kernel
   (`ivars+0x28`) queda intacta porque el `start` guarda lo que el override devuelve.
2. **Lectura directa sin override**: tras el `start`, `manager = *(*(vif+0x120)+0x28)`. Los ivars
   pertenecen a la familia; la vif del kext sólo necesita leer su propio ivar `+0x120`. Equivale
   a llamar el símbolo exportado `IO80211SkywalkInterface::getPeerManager` (`0xffffff80022f5fa8`).
   Elimina la necesidad de sobreescribir el slot y evita la condición de carrera de orden
   `start`↔captura: el valor no existe hasta que el `start` termina, así que cualquier lector debe
   esperar a ese punto (mismo aviso de orden que el paso 5 ya anota).
3. No hay vía por `getP2PSkywalkPeerMgr` (§5.3) ni por la factoría aislada: `withInterface` no
   publica nada por sí sola, y su único llamador en el kernel es la base de `+0xB08`.

El `IVarsLock`/concurrencia de esa lectura no está en el KC (campo sin cerrojo propio en las
ramas vistas) `?`.

## 8. Liberación

- `IO80211SkywalkInterface::free` (`0xffffff80022f6184`) hace `release` de los ivars `+0x18`,
  `+0x30`, `+0x38`, `+0x40`, `+0x48`, `+0xA8`, `+0xB8`, `+0xC8`, `+0xD8` y borra el bloque
  (0xF0 B), pero **no aparece el release de `ivars+0x28`** (índice `v4[5]` ausente en la lista) `?`:
  o el manager se libera en otra pata (p. ej. el propio manager al morir la vif, o la ruta de
  destrucción del controlador), o la familia lo deja al desmontaje del kext. No seguido (`?`).
- `IO80211VirtualInterface::terminate` (`0xffffff8002330c50`) y `stop` (`0xffffff8002330cee`)
  liberan los ivars propios `this+0x128` (`_bpfListHead` en `+0x28` de ese bloque, no el manager).

## 9. Compatibilidad entre builds

Direcciones exportadas (volcados `Docs/re/io80211_{25.6.0,23J631,24H232}/syms.txt`, tipo `0f` =
entry IO80211Family):

| Símbolo | 25.6.0 (25G241) | 14.8.9 (23J631) | 15.8.2 (24H232) |
|---|---|---|---|
| `IO80211VirtualInterface::createPeerManager` | `0xffffff80023308c2` | `0xffffff80021d8fbe` | `0xffffff80022a6376` |
| `IO80211VirtualInterface::getP2PSkywalkPeerMgr` | `0xffffff8002334c1a` | `0xffffff80021dd3f2` | `0xffffff80022aa68a` |
| `IO80211SkywalkInterface::createPeerManager` | `0xffffff80022f7be6` | `0xffffff80021a2072` | `0xffffff8002265092` |
| `IO80211SkywalkInterface::getP2PSkywalkPeerMgr` | `0xffffff80022f8ab4` | `0xffffff80021a2cda` | `0xffffff8002265f66` |
| `IO80211SkywalkInterface::getPeerManager` | `0xffffff80022f5fa8` | `0xffffff80021a0a3c` | `0xffffff8002263490` |
| `IO80211AWDLPeerManager::withInterface` | `0xffffff80021c14bc` | `0xffffff80020b1bd8` | `0xffffff8002160078` |

- Slots (volcados `vt_IO80211VirtualInterface.txt` y `vt_IO80211SkywalkInterface.txt`): **25.6.0 y
  23J631 = `+0xB08`/`+0xCC0`; 24H232 = `+0xB00`/`+0xCB8`** en ambas clases (un slot menos por
  delante). El override del `+0xB08` del plan es `+0xB00` en 15.8.2 — mismo ajuste que ya recoge
  `AIRLAYER_PORT_14_15.md` para el resto de clases.
- La vtable de `AppleBCMWLANProximityInterface` de 23J631 hereda la base en `+0xB08`/`+0xCC0`
  (`vt_AppleBCMWLANProximityInterface.txt`); la de 24H232 sin verificar aquí `?`.
- Los offsets de ivars de esta nota (`+0x120`, `+0x28`, `+0x58`, `+0x190`…) son del 25.6.0. Las
  instancias de 14/15 tienen tamaños iguales entre sí pero distintos de Tahoe
  (`AIRLAYER_PORT_14_15.md`), y el desplazamiento de ivars conocida en 14/15 no cubre estos
  campos: re-verificar los stores/loads citados (§2, §3, §5.1) sobre los kernelcaches
  23J631/24H232 antes de fijar constantes por build `?`.

## 10. Método y límites

- IDA 9.4 headless (`idat -A -S`) sobre copias de `~/re_cache/ida/boot_awdl.kc.i64` en `/tmp`
  (`boot_awdl_copy.i64`, `boot_awdl_copy2.i64`); decompilación de 31 funciones y desensamblado de
  los sitios clave; escaneo de bytes de los patrones de llamada `call [reg+0xB08/0xCC0/0xBA8/0xDD8]`
  sobre el `__text` de `IO80211Family`, `AppleBCMWLANCoreMac`, `AppleBCMWLANBusInterfacePCIeMac`,
  `IOSkywalkFamily`, `corecapture` y `AppleSkywalkAVB` (4/1/84/2 sitios para los cuatro primeros
  slots; los hits de 0xB08 fuera de `IO80211Family` se clasificaron uno a uno como slots de otras
  clases). El kernel base no enlaza contra IO80211Family, así que no hay llamadores fuera de estos
  filesets `?` (dirección de dependencia asumida, no verificada volcado a volcado). Xrefs de
  símbolos y de `gMetaClass` de la base de xrefs del IDB (completas).
- Direcciones citadas de primera mano en esta nota: todas las de §1–§8 y §5.3. Tomadas de
  documentos previos sin re-verificar aquí: ninguna (las de `withInterface`/`initWithInterface`
  y el contenido de `initWithInterface` amplían `AWDL_PEERMANAGER_SOFTWARE.md` §2, cuya factoría y
  registro coinciden con lo observado).
- `?` explícitos: desensamblado fino del byte `+0x00` de los ivars y del guard del `start`;
  alloc de los ivars del controlador; clase del objeto receptor del `call [rax+0xCC0]` en el bus
  PCIe; release del manager en la destrucción; cerrojo de la lectura `ivars+0x28`; offsets de
  ivars en 23J631/24H232.
