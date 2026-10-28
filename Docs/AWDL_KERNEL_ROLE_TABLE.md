# Tabla de roles de interfaz virtual en el kernel (macOS 26 / BootKernelExtensions)

Quién lee/escribe las tablas de strings de roles que encontraste en `0xffffff800246bda0` y `0xffffff800246be00`, el mapeo rol↔nombre, y la cadena completa que hace que la interfaz AWDL termine con la property `IO80211VirtualInterfaceRole = AirLink`. Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`, base módulo `0xffffff8000200000`), rea/IDA adjunto + lecturas del archivo. Lo no visto va como `?`. Tus anclas no se repiten; se usan como coordenadas.

## 1) Las tablas reales y quién las usa

**Las direcciones que encontraste son cortes internos de dos tablas más grandes**, cuyas bases son los literales que el código usa (por eso no hay xrefs a `0x246bda0` ni a `0x246be00`):

- **Tabla de nombres**: base `0xffffff800246bd88`, 10 entradas × 8 B (0x246bd88–0x246bdd7), indexada como `[role - 1]`.
- **Tabla de claves**: base `0xffffff800246bdd8`, 10 entradas × 8 B (0x246bdd8–0x246be27), indexada como `[role - 1]`. Tu tabla de 5 en `0x246be00` son las entradas de roles 6–10; las de roles 1–5 están en `0x246bdd8–0x246bdf8`.

**El lector/escritor es `IO80211SkywalkInterface::setInterfaceRole(unsigned int)`** (`__ZN23IO80211SkywalkInterface16setInterfaceRoleEj`, **`0xffffff80022f60f0`**, 0x94 bytes). Pseudocódigo observado (direcciones de cada rama):

- `0xffffff80022f6101`: guarda el rol crudo en `ivars + 88` (`*(*(this+36) + 88) = role`).
- `0xffffff80022f6104–0xffffff80022f6109`: `v3 = role - 1`; si `v3 > 9` (rol 0 o ≥ 11) usa literales de error: clave `"IO80211InterfaceRole"`, valor `"Uknown"` (`0xffffff80022f6125`, `0xffffff80022f612c`).
- `0xffffff80022f6114`: **`nombre = off_FFFFFF800246BD88[role-1]`** (la tabla de nombres).
- `0xffffff80022f611f`: **`clave = off_FFFFFF800246BDD8[role-1]`** (la tabla de claves).
- `0xffffff80022f6139`: **`setPropertyHelper(this, clave, nombre)`** — la escritura de la property.
- `0xffffff80022f616c–0xffffff80022f617f`: cachea `OSString::withCString(nombre)` en `ivars + 184` (eso es lo que devuelve `getInterfaceRoleStr`, `0xffffff80022f5ee8`, que delega leyendo ese ivar).

La función vecina `IO80211SkywalkInterface::getInterfaceRoleCounter(SkywalkInterfaceRole)` (**`0xffffff80022f5eb6`**, 0x22 bytes) confirma el rango del enum interno: `if (role <= 10) return intfRoleToNumberMap[role]++;` — un mapa global de contadores por rol (dirección del mapa: símbolo `intfRoleToNumberMap`, VA exacta no extraída, `?`).

## 2) Tabla de nombres completa (verificada desde el archivo + tus observaciones del IDB)

Archivo del `.kc`: los qwords son fixups encadenados; decodifiqué con `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)` (base = vmaddr mínimo `__HIB`). Validación cruzada: la entrada `0x246bdb0` decodifica a `0xffffff800241dbc0` = tu string `AirLink` exacto, y `0x246be00` → `0xffffff800241dbc8` = tu `IO80211VirtualInterfaceRole` exacto.

| rol (`SkywalkInterfaceRole`) | entrada nombres | string | VA del string | Fuente |
|---:|---|---|---|---|
| 1 | `0x246bd88` | `Infrastructure` | `0xffffff800241db9c` | archivo |
| 2 | `0x246bd90` | `LowLatency` | `0xffffff800241dc2a` | archivo |
| 3 | `0x246bd98` | `Uknown` | `0xffffff800241dc35` | archivo |
| 4 | `0x246bda0` | `Uknown` | `0xffffff800241dc35` | archivo + tu lista |
| 5 | `0x246bda8` | `Uknown` | `0xffffff800241dc35` | archivo + tu lista |
| **6** | `0x246bdb0` | **`AirLink`** | `0xffffff800241dbc0` | archivo + tu xref |
| 7 | `0x246bdb8` | `SoftAP` | `0xffffff800241dc23` | archivo + tu lista |
| 8 | `0x246bdc0` | `WiFi-Aware Discovery` | `0xffffff800241dbe4` | archivo + tu lista |
| 9 | `0x246bdc8` | `WiFi-Aware Discovery+Data` | `0xffffff800241dbf4` | archivo + tu lista |
| 10 | `0x246bdd0` | `WiFi-Aware Data` | `0xffffff800241dc0d` | archivo + tu lista |

**Tabla de claves** (misma indexación `role-1`, base `0xffffff800246bdd8`): roles **1–5** → `IO80211InterfaceRole` (`0xffffff800241dbab`; verificado desde el archivo en las entradas de roles 1, 2 y 5 = `0x246bdd8`, `0x246bde0`, `0x246bdf8`; roles 3–4 por leer, `?`); roles **6–10** → `IO80211VirtualInterfaceRole` (`0xffffff800241dbc8`; tus 5 entradas en `0x246be00–0x246be20`). El literal `"IO80211InterfaceRole"` que aparece en el camino de error de `setInterfaceRole` es el mismo string del pool.

**Detalle relevante para el gate AWDL:** el nombre de la property cambia con el rol — para roles 1–5 es `IO80211InterfaceRole` y para roles 6–10 es `IO80211VirtualInterfaceRole`. Una interfaz AWDL (rol 6) publica `IO80211VirtualInterfaceRole = AirLink` en su registry entry; una de rol ≤ 5 (p. ej. la de baja latencia, rol 2) publica `IO80211InterfaceRole = LowLatency`.

## 3) El índice 2 de tu ventana = rol 6 = AWDL, sin conversión de enumeración

No hay función de mapeo entre el rol apple80211 de `VIRTUAL_IF_CREATE` y el índice de la tabla: **la enumeración interna `SkywalkInterfaceRole` usa la misma numeración** que el rol apple80211 (6 = AWDL/AirLink). Evidencia de la cadena sin conversión, punta a punta:

1. `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`): lee el rol crudo de `*(request+12)` (`0xffffff800167891c`); roles 8–10 → `createNANInterface` (`0xffffff8001678952`), rol **6** → `AppleBCMWLANCore::createProxInterface` (llamada en `0xffffff8001678be2`), rol 7 → `AppleBCMWLANIO80211APSTAInterface::withOptions` (`0xffffff8001678c26`), resto → error `"Invalid virtual interface role 0x%X name %s"` (`0xffffff8001678b79`/`0xffffff8001678ba7`).
2. `AppleBCMWLANProximityInterface::withOptions(AppleBCMWLANCore*, ether_addr*, char*)` (**`0xffffff80017159e6`**, 0x8a bytes): aloca 320 B (`OSObject_typed_operator_new`, `0xffffff8001715a0e`), fija la vtable de la clase `off_FFFFFF80017F5B78` (`0xffffff8001715a2a`) y llama el **slot +4552 (0x11C8)** de esa vtable con `(self, core, mac, **6**, name)` (`0xffffff8001715a51`: `a4: 6` literal). El slot es la `init` especializada.
3. `AppleBCMWLANProximityInterface::init(AppleBCMWLANCore*, ether_addr*, unsigned int role, char*)` (**`0xffffff8001718db0`**, 0x684 bytes): llama la init padre con los mismos 5 argumentos vía la llamada renderizada como `off_FFFFFF80024572B8(...)` en `0xffffff8001718e09` (slot de vtable cuyo target no pude re-verificar desde el archivo: el fixup decodificado cae en `0xffffff8002305f8`, que no es entrada de función conocida — `?`; el IDB tiene el fixup aplicado). El resto de la init (interfaceId 2 si versión ≥ 5, Taskmaster, boot-args `awdl_*`) coincide con `AWDL_ROLE6_RE.md` §3.
4. `IO80211VirtualInterface::init(IO80211Controller*, ether_addr*, unsigned int role, char const*)` (**entrada `0xffffff80023305f8`**, 0x20c bytes; el xref al `kalloc_type_view_341` en `0xffffff8002330629` está dentro): en `0xffffff800233064b` llama **`IO80211SkywalkInterface::setInterfaceRole(this, role)`** — sin transformar el valor. No escribe property aquí (solo ivars BPF/locks/FlowQueueDatabase y boot-arg `wlan.entitlement`).
5. `setInterfaceRole(6)` (ver §1) → índice `6-1=5` en `0x246bdb0` → **`AirLink`**, clave `0x246bdd8+5*8 = 0x246be00` → **`IO80211VirtualInterfaceRole`** → `setPropertyHelper`.

Sobre xrefs de código a las tablas y al string: el código referencia los literales `0xffffff800246bd88` y `0xffffff800246bdd8` (bases reales), no `0x246bda0`/`0x246be00`; el string `0xffffff800241dbc8` solo se alcanza a través de las entradas de la tabla de claves (coincide con tus xrefs de datos). La única referencia de código directa a `0xffffff800241dbc8` encontrada en esta pasada: ninguna además de las entradas (`?` no se descarta otra vía indirecta).

## 4) Otros datos del entorno de la tabla

- `IO80211Controller::createVirtualInterface(ether_addr*, unsigned int)` = `__ZN17IO80211Controller17createVirtualInterfaceEP10ether_addrj` en **`0xffffff800229612e`** y `attachVirtualInterface` en `0xffffff80022960aa` (coincide con `AWDL_ROLE6_RE.md`); `get_function_by_name` con el mangled no resolvió (IDA no la tiene con ese nombre exacto), dirección tomada de `Docs/re/io80211_25.6.0/syms.txt:11134`. No se decompilaron en esta pasada (`?` su relación exacta con `AppleBCMWLANCore::createProxInterface`).
- Vtables en el mismo entorno de datos: `__ZTV17IO80211Controller` en `0xffffff8002461148`, `__ZTV23IO80211VirtualInterface` en `0xffffff8002472b78` (syms.txt:18527/18599). Las tablas de roles (`0x246bd88`/`0x246bdd8`) viven en la misma región de datos de IO80211Family.
- `IO80211SkywalkInterface::init(IOService*, ether_addr*)` = `0xffffff80022f56e2`; `getInterfaceRole` = `0xffffff80022f5ed8`; `getInterfaceRoleStr` = `0xffffff80022f5ee8` (forwarder: devuelve el OSString cacheado en `ivars+184`, llamando el slot +328 del objeto interno — `0xffffff80022f5ef3`/`0xffffff80022f5f03`).

## 5) Método y límites

- Direcciones de funciones: `syms.txt` del proyecto + `procedure_address`/`procedure_pseudo_code` del adaptador rea/IDA (upstream ida-pro-mcp 1.30.0, base `BootKernelExtensions.kc`).
- Tablas y strings: leídos del propio `.kc` con el mapeo `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)` para los fixups (validado contra tus dos anclas); las entradas de claves roles 3–4 (`0x246bde8`, `0x246bdf0`) y el target del slot `0xffffff80024572B8` quedan `?`.
- `withOptions`/`init`/`setInterfaceRole`/`IO80211VirtualInterface::init` decompilados completos; `createProxInterface` (`0xffffff800165dcbe`, doc previa) y `createVirtualInterface` no decompilados en esta pasada.

## 6) Cierre de los `?` de §2 y §3 (lectura directa del archivo, 2026-10-28)

Ambos puntos verificados releyendo `BootKernelExtensions.kc` desde el archivo (md5 re-verificado `f489ff8be80e6013a1bf47561dcaef45`), con el mapeo ya validado: `fileoff = VA − 0xffffff8000100000` y fixups `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)`. Control de la pasada: la entrada de nombres del rol 6 (`0x246bdb0`) decodifica a `AirLink` y los strings en `0x231dbc0`/`0x231dbc8` se leen exactos, igual que tus observaciones del IDB.

### Claves de los roles 3 y 4 — la tabla de claves queda completa y uniforme

| rol | entrada (VA) | fileoff | qword crudo | low32 → VA del string | string |
|---:|---|---|---|---|---|
| 3 | `0xffffff800246bde8` | `0x236bde8` | `0x004000000231dbab` | `0x231dbab` → `0xffffff800241dbab` | `IO80211InterfaceRole` |
| 4 | `0xffffff800246bdf0` | `0x236bdf0` | `0x004000000231dbab` | `0x231dbab` → `0xffffff800241dbab` | `IO80211InterfaceRole` |

Mismo qword byte a byte que las entradas de roles 1, 2 y 5 (`0x246bdd8`, `0x246bde0`, `0x246bdf8`, releídas como control en esta pasada: idénticas). **No queda ninguna entrada sin leer: roles 1–5 → `IO80211InterfaceRole` (`0xffffff800241dbab`), roles 6–10 → `IO80211VirtualInterfaceRole` (`0xffffff800241dbc8`); el corte del nombre de property es exactamente por clase de rol, sin excepciones.**

### Target del slot `off_FFFFFF80024572B8` — confirmado por fixup directo, no por inferencia

- Qword crudo en `0xffffff80024572b8` (fileoff `0x23572b8`): **`0x00400000022305f8`** → low32 `0x22305f8` → **VA `0xffffff80023305f8`** — cae exactamente en la entrada conocida de `IO80211VirtualInterface::init` (§3.4), no en un punto medio de función. Los primeros bytes en el fileoff decodificado son un prólogo x86_64 estándar (`55 48 89 e5 41 57 41 56 53 48 83 ec 18 41 89 cf`), coherente con una entrada de función de 5 argumentos.
- El símbolo en esa dirección es `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` (`Docs/re/io80211_25.6.0/syms.txt:15779`).
- Confirmación independiente del IDB: `xrefs` de rea/IDA hacia `0xffffff80023305f8` lista `0xffffff80024572b8` como referencia de datos, junto a otros 8 slots equivalentes (`0xffffff80017f53e0`, `0xffffff80017f69e0`, `0xffffff80017f7d00`, `0xffffff80017f9128` en `__const` del kext Broadcom; `0xffffff800246f1e8`, `0xffffff80024739f0`, `0xffffff8002477dc0`, `0xffffff800247ad30` en datos de IO80211Family). La consulta inversa (`xrefs` al slot) devolvió vacía en el adaptador; la referencia se observa desde el lado de la función.

**Conclusión:** la llamada de `0xffffff8001718e09` dentro de `AppleBCMWLANProximityInterface::init` ejecuta **`IO80211VirtualInterface::init` (`0xffffff80023305f8`)**, que en `0xffffff800233064b` llama `setInterfaceRole(this, role)` con el rol sin transformar. Contexto adicional: el slot `0xffffff80024572b8` cae dentro de `__ZTV19IO80211AWDLProtocol` (símbolo en `0xffffff8002456440`, extendiéndose hasta `__ZTVN19IO80211AWDLProtocol9MetaClassE` en `0xffffff8002457618`), a +0xE78 del símbolo; su valor (la init heredada de `IO80211VirtualInterface`, no una override de `IO80211AWDLProtocol`) indica que esa clase no pisa `init`, y el despacho por slot de vtable en vez de call directa es el estilo de compilación kext. Por qué la init de `AppleBCMWLANProximityInterface` usa precisamente la vtable de `IO80211AWDLProtocol` y no su propia vtable no se verificó (relación de herencia exacta no vista en esta pasada, `?`). Con esto la cadena de §3 queda cerrada punta a punta con evidencia de archivo: rol 6 → `AppleBCMWLANProximityInterface::init` → (slot `0x24572b8`) → `IO80211VirtualInterface::init` → `setInterfaceRole` → `IO80211VirtualInterfaceRole = AirLink`.

Nota sobre §3.3: el decode previo registrado allí (`0xffffff8002305f8`) salió del mapeo viejo, anterior a fijar la base `0xffffff8000100000`; la lectura cruda de esta pasada da exactamente `0xffffff80023305f8`.
