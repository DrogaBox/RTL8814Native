# SimpleBCM como referencia: AirPortBrcmNIC sobre Sequoia/Tahoe sin el IOSkywalkFamily del sistema

Revisión del proyecto abierto SimpleBCM (github.com/laobamac/SimpleBCM, clon en
`~/re_cache/work/ref_simplebcm`, commit `624fca1`, 2026-10-31). Lectura de código únicamente;
nada del repositorio se compila, carga ni ejecuta. Las rutas `ref_simplebcm/…` son relativas a ese
clon. Estado de verificación: todo lo citado con `ref_simplebcm/…:línea` se leyó directamente en el
código; lo que depende de binarios que el repositorio no trae (los kexts de Apple) se marca `?`.

## 1. Qué es

Un plugin de Lilu (kext `cn.laobamac.SimpleBCM`, personalities sobre `IOResources`,
`ref_simplebcm/SimpleBCM/Info.plist:21-41`) más un conjunto de kexts inyectados por OpenCore.
Un único binario de SimpleBCM.kext sirve Sequoia (Darwin 24) y Tahoe (Darwin 25)
(`ref_simplebcm/README.en.md:48`); la selección de ABI se hace en tiempo de ejecución por
`getKernelVersion()`. El objetivo es que el AirPortBrcmNIC de Apple (Broadcom PCI) funcione en
Sequoia/Tahoe con la API Wi-Fi nativa (menú Wi-Fi, CoreWLAN/airportd), sin tocar el volumen del
sistema: los kexts van por OC `Kernel→Add` y el sistema IOSkywalkFamily se excluye por OC
`Kernel→Block`.

Orden de carga (`ref_simplebcm/README.en.md:41`): **Lilu → SimpleBCM → IOSkywalkFamily →
IO80211FamilyLegacy → AirPortBrcmNIC** (del sistema elegido). El fragmento de configuración lo
genera `ref_simplebcm/Tools/configure.py:57-66`, y la entrada de bloqueo es:

```
{ Identifier: "com.apple.iokit.IOSkywalkFamily", Strategy: "Exclude", MinKernel 24.0.0, MaxKernel 25.99.99 }
```
(`ref_simplebcm/Tools/configure.py:64-66`; el comentario dice "Allow injected IOSkywalk").

## 2. Los kexts acompañantes

Fijados por commit y hash SHA-256 en `ref_simplebcm/Tools/bootstrap.py:17-33`:

| Kext | Paquete | Origen |
|---|---|---|
| `IOSkywalkFamily.kext` | `IOSkywalkFamily-v1.2.0.zip` | OCLP `payloads/Kexts/Wifi/` |
| `IO80211FamilyLegacy.kext` (con PlugIn `AirPortBrcmNIC.kext`) | `IO80211FamilyLegacy-v1.0.0.zip` | OCLP |
| `AirPortBrcmNIC-Tahoe.kext` | `AirPortBrcmNIC-Tahoe-v1.0.0.zip` | OCLP |
| `AirportBrcmFixup` (opcional) | `v2.2.1` | acidanthera |

Los números v1.2.0/v1.0.0 son versiones de paquete de OCLP, no `CFBundleVersion` de Apple; la
procedencia exacta (qué build de macOS dio cada binario) no consta en el repositorio `?`.
`NOTICE.md:14-15` lo declara: OCLP suministra los archivos, y los componentes de Apple conservan
sus términos. `Docs/ABI.json` guarda UUID y SHA-256 de las cuatro imágenes (p. ej. Legacy
`uuid 4c07538b…`, `sha256 ac2dc15b…`; Skywalk `uuid 035a9ad9…`; los dos binarios AirPort comparten
UUID `e4678feb…` con distinto sha256: el "Tahoe" es el mismo binario con parches `?`,
`Docs/ABI.json`, imágenes `AirPortSequoia` y `AirPort`).

Estos kexts de Apple son el *IO80211Family "legacy"* de los Intel modernos: el que vive en
`/System/Library/Extensions/IO80211FamilyLegacy.kext` con `IO80211Controller` de 5-arg ioctl
(`apple80211_ioctl_get(IO80211Interface*, IO80211VirtualInterface*, IO80211SkywalkInterface*, void*)`,
`ref_simplebcm/Docs/ABI.json`, guards de Legacy). Ya enlaza contra clases de IOSkywalkFamily, pero
con el layout de la época en que Apple lo compiló `?`.

## 3. Por qué hay que bloquear el IOSkywalkFamily del sistema

El kext inyectado declara el mismo bundle id `com.apple.iokit.IOSkywalkFamily`; dos kexts con el
mismo identificador no pueden convivir, y OC `Block → Exclude` retira el del sistema (mecánica
exacta del Exclude sobre un fileset del KC no cubierta por el repositorio `?`). La razón de fondo
es de ABI C++: `IO80211FamilyLegacy` y `AirPortBrcmNIC` están compilados contra los ivars/vtables
de IOSkywalkFamily de su época; enlazarlos contra el IOSkywalkFamily de Tahoe corrompería los
objetos (mismos símbolos `__ZN22IOSkywalkInterface…`, layouts distintos). Es el mismo fenómeno
medido aquí entre 23/24 y 25: las clases Skywalk crecen +0x10 y se recolocan campos
(`Docs/AIRLAYER_PORT_14_15.md:81-87`).

El resto de la familia moderna (`IO80211Family` dentro de `BootKernelExtensions.kc`) **no** se
bloquea; en un arranque SimpleBCM queda enlazando contra el Skywalk inyectado, con layouts
antiguos bajo código Tahoe `?`. No rompe nada ahí porque ningún driver instancia esas clases en un
hackintosh sin Broadcom Apple; el punto importa para la pregunta 3.1 de este documento.

## 4. Qué parchea SimpleBCM.kext con Lilu

`Bridge::initialize` elige la ABI por versión de kernel y registra callback de carga para tres
kexts (`ref_simplebcm/SimpleBCM/Bridge.cpp:635-653`; tabla `KextInfo` con rutas de disco en
`:91-101`). El callback `Bridge::patchKext` (`:564-633`) hace tres cosas por kext:

1. **Validación por bytes antes de tocar nada**: cada imagen se contrasta con `ByteGuard`
   (offset + 32 bytes + máscara) en `ref_simplebcm/SimpleBCM/Profiles.hpp`. 72 guards para
   `IO80211FamilyLegacy` (`:19-92`), 1 para `IOSkywalkFamily` (`:110-112`), 10 para cada
   `AirPortBrcmNIC` con variantes Sequoia (`:114-125`) y Tahoe (`:131-142`). Si falla:
   `reject("…BinaryMismatch")` y el puente queda desarmado (`:568-571, :599-602, :618-620`).
   El `PatchState` publicado en el IORegistry es el OR de bits 1/2/4 por kext parcheado; 7 = listo
   (`:597, :611, :627, :655-657`).
2. **Resolución por símbolo demangleado completo**, nunca offsets fijos: los punteros que el
   puente necesita salen de `patcher.solveSymbol` (`:572-579`):
   - `IO80211Controller::apple80211_ioctl_get/set` (5 args),
   - `IO80211Interface::getController`, `IO80211VirtualInterface::getController`,
   - `IO80211Interface::getBSDName`, `IO80211VirtualInterface::getBSDName`,
     `IO80211SkywalkInterface::getBSDName`,
   - `IO80211Interface::performCommand(IONetworkController*, unsigned long, void*, void*)`.
3. **Rutas (detours) con `routeMultipleLong`** (`:585-591`):
   - `IO80211Interface::newUserClient` y `IO80211VirtualInterface::newUserClient` →
     `newClassicClient`/`newVirtualClient` (`:206-218`): si `type==0` y el puente está listo,
     sustituyen el user client de la familia por el propio.
   - `IO80211Interface::IO80211InterfacePostMessage`,
     `IO80211VirtualInterface::IO80211InterfacePostMessage` y
     `IO80211InfraInterface::postMessageInternal` → `postClassic`/`postVirtual`/`postInfra`
     (`:438-451`): copian cada evento a una cola propia antes de llamar al original.
   - En el Skywalk inyectado, solo
     `IOSkywalkEthernetInterface::newUserClient` → `newSkywalkClient`, que además exige
     `metaCast("IO80211SkywalkInterface")` (`:220-225, :598-611`).

Además, **reescritura de los `call` a `_copyin`/`_copyout`** dentro de la familia legacy y del
driver (`patchCopies`, `:532-562`; tabla `CopySite`, `Profiles.hpp:93-108,126-129,143-146`;
14 sitios en Legacy, 2 en cada AirPort). Verifica que el destino real tras seguir `E9`/`jmp [rip]`
hasta 3 saltos (`followBranch`, `:515-530`) coincide con el símbolo del kernel `_copyin`/`_copyout`
resuelto por nombre, y reescribe el desplazamiento para apuntar a `bridgeCopyIn`/`bridgeCopyOut`
(`:143-155`). Con eso, todo acceso a memoria de usuario que la familia haga mientras el puente
tiene en marcha un comando se redirige a una "sombra" en kernel: `Bridge::command` apunta
`request.data` al buffer sombra y registra el rango por hilo (`CopyContext`, `:453-482`), de modo
que los handlers de la familia que hacen `copyin/copyout` sobre el payload leen/escriben el buffer
traducido sin saberlo. La lista de sitios adaptados, con el símbolo de cada uno, está en
`ref_simplebcm/Docs/ABI.json`/`Docs/Control-Codecs.json` (`adaptedCopySites`): entre otros,
`getAwdlCtl`/`setAwdlCtl` (copia de entrada y salida), `setDebugInfo`, `setRangingEnable`,
`setRangingStart`, `setRangingIdentifier`, `getRangingResults`, `getRangingCapabilities`, y los
`IO80211Controller::copyIn/copyOut` genéricos.

Los dos sitios del AirPort son `AirPort_BrcmNIC::wlIocCommon(apple80211req*, OSObject*, bool, bool)`
(`Control-Codecs.json`, imágenes AirPort/AirPortSequoia: `+0x70` copyin, `+0x1fe` copyout).

## 5. El user client propio y la traducción de structs

`IO80211APIUserClient` (`ref_simplebcm/SimpleBCM/UserClient.cpp:13-64` en la cabecera,
implementación `UserClient.cpp`) reimplementa el contrato del user client de Apple para los
clientes nativos de Sequoia/Tahoe (CoreWLAN/airportd/CoreWiFi):

- Exige el entitlement `com.apple.wlan.authentication` (`UserClient.cpp:37`); y
  `com.apple.private.wifi.IO80211.get_pmk` para la petición 0x178/376 (PMK, `:301`).
- Selectores 0-8: `Command`, `UseIOUC`, `Subscribe`, `MonitorStart`, `MonitorStop`,
  `UseEventPipe`, `LargeCommand`, `DriverKitSupported`, `MapUserBuffer`
  (`WirelessABI.hpp:86-97`). `Command`/`LargeCommand` llevan paquete
  `CommandHeader(8) + buffer(0x3C00 / 0x3C000) + Request(0x28)`
  (`WirelessABI.hpp:17-22,114-117,365-369`; despacho en `UserClient.cpp:165-168,243-400`).
  Las constantes de ioctl son `GetCommand 0xC02869C9` y `SetCommand 0x802869C8`
  (`WirelessABI.hpp:17-18`).
- Monitor por anillo compartido: `EventRing` con cabecera `EventHeader` de 0x30
  (`WirelessABI.hpp:300-310,318-363`), memoria `kIOMemoryKernelUserShared`
  (`UserClient.cpp:402-434`), aviso por `sendAsyncResult64` con
  `EventNotification 0xE00002D5` (`WirelessABI.hpp:23`; `UserClient.cpp:489-493`).
- Filtro de eventos bitmap de 0x20 bytes (`WirelessABI.hpp:284-292`), cargado con la petición
  `filterMonitorEvents` de la ABI activa (0 en Sequoia, 0x234=564 en Tahoe, `:57-61`) `?`
  (número tomado del código; sin verificar en el desensamblado de 25G241).

La ABI activa es una tabla de cinco tamaños (`WirelessABI.hpp:50-65`):

| Campo | Sequoia (24) | Tahoe (25) |
|---|---|---|
| `scanResultSize` | 0x8D8 | 0x8E0 |
| `capabilitySize` | 19 | 21 |
| `associateSize` | 0x384 | 0x38C |
| `joinStatusSize` | 0x1CC | 0x1D4 |
| `filterMonitorEvents` | 0 | 0x234 |

Las traducciones concretas (tamaños "legacy" de la familia vs "native" del userspace) están
documentadas en `ref_simplebcm/Docs/Control-Codecs.json`:

- Petición 12 (CARD_CAPABILITIES): legacy 12 B → nativa 19/21 B, rellenando con ceros; el bit de
  "scan completion" es 0x40 del byte 5 del buffer (`ScanCompletionCapabilityByte = 5`,
  `WirelessABI.hpp:36-38`; `expandCapabilities`, `:151-161`). `Control-Codecs.json:562` lo rotula
  como capacidad 46 (= 5·8 + 6, contando desde el inicio del buffer, versión incluida).
- Petición 11 (scan result): legacy 1164 B (IE hasta 1024) → nativa 2264/2272 B (IE hasta 2048);
  cabecera común 140 B, longitud de IE en 138 (`scanResultTransfer`;
  `prepareScanResult`/`expandScanResult`, `WirelessABI.hpp:129-149`).
- Petición 20 (associate): legacy 548 B → nativa 900/908 B, con tabla campo a campo
  (`associateTransfer`; `prepareAssociate`, `WirelessABI.hpp:229-251`).
- Petición 216 (roam profiles): bandas legacy 124 B vs nativas 128 B, dos bandas
  (`roamProfileTransfer`; `runRoamProfiles`, `UserClient.cpp:211-241`).
- Evento 9 (join/association done): reconstruye el estado nativo; en Tahoe el estado va en
  +0x1B8/+0x1BC, el resultado en +0x1C4 y el BSSID en +0x1CC; en Sequoia en +0x08/+0x0C y +0
  (`encodeJoinStatus`, `WirelessABI.hpp:210-227`; lectura desde handlers de la familia en
  `captureJoinEvent`, `Bridge.cpp:273-295`, que reconsulta las peticiones 50, 21 y 9).
- Evento 0x37 (driver available): legacy 0xB8 B → nativo 0xF8 B; disponible se sintetiza desde el
  estado de energía (petición 19, `encodePowerAvailability`, `WirelessABI.hpp:188-208`) y se
  re-reproduce a monitores tardíos (`copyAvailability`, `Bridge.cpp:423-436`).
- `nativeCaller`: el llamador nativo (`Apple80211RawGet`, UUID de imagen `A7A35846…`) pasa
  `&req_val` con longitud 4 cuando `req_data` es NULL (`Control-Codecs.json`, `nativeCaller`).

Identificación de endpoints que el puente atiende: la interfaz debe resolver a un controlador
`metaCast("AirPort_BrcmNIC")` y el proveedor PCI debe ser vendor 0x14E4 con device-id en
{0x4331, 0x4353, 0x43BA, 0x43A3, 0x43A0, 0x4357, 0x43B1, 0x43B2} (`Bridge.cpp:157-204`). Todo lo
demás (otros drivers, otros controladores) pasa por los originales.

## 6. Cómo sube `awdl0`

El plugin no contiene código AWDL: no crea interfaces, no hablan peer managers. `awdl0` sale del
conjunto Apple intacto: `AirPortBrcmNIC` declara las capacidades de la tarjeta y la
`IO80211FamilyLegacy` contiene la ruta de interfaz virtual y el peer manager de su época `?`
(binario no inspeccionado). Lo que el repositorio acredita es que los `ioctls`/eventos AWDL del
userspace de Sequoia/Tahoe siguen el mismo camino que el resto de apple80211 y que el puente los
deja pasar traducidos:

- `getAwdlCtl`/`setAwdlCtl` de la familia legacy tienen sitios `copyin`/`copyout` adaptados
  (`Control-Codecs.json`, `adaptedCopySites`; números de petición no constan en el repo `?`).
- `getVirtualIfRole` (petición 96) está entre los GET escalares soportados
  (`Profiles.hpp:18`; `Control-Codecs.json`, `scalarGetRequests`).
- El diagnóstico del proyecto consulta `ifconfig awdl0`, `netstat -I awdl0` y logs de `wifip2pd`
  (subsystem `com.apple.awdl`), `sharingd` (categoría AirDrop) y `mDNSResponder`
  (`ref_simplebcm/Tools/collect.py:34-41`): el stack espera `awdl0` operativa con los mismos
  daemons que este proyecto ya documentó.

## 7. Respuestas para este proyecto

### 7.1 ¿Puede `RTL8814AirLayer.kext` convivir en el mismo arranque con SimpleBCM?

**No.** Son arranques alternativos, no coexistencia. Razones:

1. **Bloqueo de IOSkywalkFamily.** En un arranque SimpleBCM el único `com.apple.iokit.IOSkywalkFamily`
   es el inyectado v1.2.0, con layouts C++ de su época. `RTL8814AirLayer` declara dependencia
   `com.apple.iokit.IOSkywalkFamily 1.0` (`Source/AirLayer/Info.plist:60-61`) y su binario está
   compilado contra los tamaños/offsets de 25.6.0 (`build_airlayer.sh:5,25-26`; cabeceras
   `Source/IO80211/IO80211_25_6_0.h`). Cargarlo contra el Skywalk antiguo daría desplazamientos de
   ivar y slots de vtable equivocados: el tipo exacto de corrupción que `Docs/AIRLAYER_PORT_14_15.md:81-87`
   mide entre versiones (+0x10 por clase en 25). Resolución de símbolos no protegería: los nombres
   `__ZN22IOSkywalk*` existen en ambas épocas.
2. **La familia moderna del KC queda rota de paso.** `IO80211Family` (en `BootKernelExtensions.kc`)
   también enlaza IOSkywalkFamily; con el fileset excluido y sustituido, sus clases pasan a apoyar
   en layouts antiguos `?`. Hoy no hay problema porque nadie instancia esas clases en ese arranque;
   `RTL8814AirLayer` sería el primero.
3. **Choques de espacio de nombres.** `IO80211FamilyLegacy` define clases con los mismos nombres
   que la familia del KC (`IO80211Controller`, `IO80211Interface`…) bajo bundle id distinto
   (`com.apple.iokit.IO80211FamilyLegacy`); que ambas convivan es precisamente la configuración
   SimpleBCM y funciona por resolución por-dependencia `?` (no hay volcado de ese arranque). Pero
   `RTLAirController : IO80211Controller` (moderno) y `AirPort_BrcmNIC : IO80211Controller` (legacy)
   serían dos jerarquías paralelas con metaclases homónimas; OSMetaClass registra por nombre
   global `?`. Riesgo adicional, no bloqueante por sí solo.
4. **Lo que sí conviviría**: `RTL8814Native.kext` solo depende de IONetworkingFamily/IOUSBHostFamily
   y KPIs (`Source/Driver/Info.plist:302-316`): se carga igual en un arranque SimpleBCM. Los hooks
   de SimpleBCM no le afectan (su `identify` exige `AirPort_BrcmNIC` y PCI 0x14E4,
   `Bridge.cpp:181-198`). Pero sin AirLayer no hay `awdl0`, y SimpleBCM no aporta AWDL para un
   adaptador que no es Broadcom.
5. **Los contratos ioctl coinciden, no chocan.** `SetCommand 0x802869C8` / `GetCommand 0xC02869C9`
   (`WirelessABI.hpp:17-18`) son el par que este proyecto ya identificó como token del interno
   0x802869C8 con el tipo en la dword alta (`AWDL_PEERMANAGER_SOFTWARE.md`). Si algún día se
   quisiera un puente estilo SimpleBCM para la capa RTL, el userspace hablaría con las mismas
   constantes.

### 7.2 Técnicas reutilizables para soportar macOS 14 y 15 con la capa propia

- **Tabla de ABI por Darwin con un binario único** (`userABIForKernel`, `WirelessABI.hpp:63-65`;
  `PluginConfiguration` con `KernelVersion::Sequoia..Tahoe`, `SimpleBCM.cpp:72-82`): mismo patrón
  que la puerta por build planeada aquí (KnownBuilds/VersionGate). Aplicable a los tamaños de
  payload apple80211 que la capa procesa, que cambian por versión (los cinco tamaños de la §5 son
  una medida independiente de esos cambios).
- **Validación por bytes en carga, no offline** (`validate` + `ByteGuard` con máscara,
  `Bridge.cpp:484-513`): la idea equivale a los guards de `Docs/AWDL_VERSION_SENSITIVE_OFFSETS.md`
  (patrones de bytes verificados), pero ejecutada en `patchKext` antes de confiar en un offset.
  Para la capa RTL, el análogo es validar en tiempo de `start` los bytes de 25.6.0 en los que se
  apoyan los offsets de vtable/celdas antes de instalar hooks, y desarmar (como `reject()`) si el
  kernel no es el esperado.
- **Resolución por símbolo completo con verificación de destino** (`solveSymbol` +
  `followBranch`): exactamente la regla ya adoptada de "resolver por símbolo en vez de offsets
  fijos" para 14/15; el añadido útil es la verificación de que el símbolo resuelto cae dentro de la
  imagen y que el sitio a parchear es el `call`/`E9` esperado (`patchCopies`, `:532-562`).
- **Traducción de structs por sombra con `copyin`/`copyout` interceptados**: si algún día el
  userspace de una versión cambia el tamaño de un struct que la capa procesa por referencia, se
  puede traducir en un buffer sombra sin tocar cada handler; el mecanismo `CopyContext` por hilo
  (`:453-482`) es el detalle que evita traducir copias ajenas.
- **Replay de disponibilidad y conversión de eventos** (`availability[]`, `copyAvailability`;
  `encodeJoinStatus`): los eventos 9/0x37 con layouts por versión son directamente comparables con
  la familia de eventos que la etapa 1 emite; útil como referencia cruzada de tamaños nativos por
  versión.
- **Lo que NO conviene copiar**: sustituir el IOSkywalkFamily. Para 14/15 la capa se compila contra
  las cabeceras de cada versión (como ya está hecho para 25.6.0); el enfoque SimpleBCM existe
  porque su backend es un binario Apple cerrado de otra época, y su precio es perder el Skywalk
  del sistema. La capa RTL nativa no tiene esa restricción.

### 7.3 Qué revela sobre lo que AirDrop exige del lado Wi-Fi

- **Nada nuevo en la superficie AWDL**: el mismo `wifip2pd`/`sharingd`/`mDNSResponder` sobre
  `awdl0` que este proyecto documentó (`AWDL_AIRDROP_SIN_BLUETOOTH.md`; `Tools/collect.py:38-40`
  vigila exactamente esos tres procesos). El userspace de Sequoia/Tahoe no estrena una ruta AWDL
  por Skywalk: los controles AWDL siguen siendo peticiones apple80211 sobre la interfaz
  (`getAwdlCtl`/`setAwdlCtl` adaptados en el puente), y la creación de la interfaz virtual sigue
  siendo cosa de la familia (`getVirtualIfRole`, petición 96). Refuerza la apuesta de la etapa 1:
  responder `VIRTUAL_IF_CREATE` y los eventos es suficiente superficie.
- **`CARD_CAPABILITIES` es corto y versionado**: 21 bytes en Tahoe (19 en Sequoia, 12 en la era
  legacy) según `capabilityTransfer` y `capabilitySize`. Los índices que la capa RTL ya escribe
  (byte 8 bit 0x01 AWDL, byte 7 bit 0x01, byte 7 bit 0x20 banda dual —
  `HANDOFF_AWDL.md` §3.2 y `AWDL_SECUENCIA_CON_INFRA.md`) caben en ese array; el byte 5 bit 0x40 del
  buffer (el `p[5]` de la capa) que SimpleBCM enciende para anunciar "scan completion events" en interfaces clásicas
  (`expandCapabilities`, `WirelessABI.hpp:151-161`) es un bit más del mismo mapa `?` (semántica
  tomada del código, no verificada contra Apple).
- **El cliente nativo tolera respuestas mínimas**: `Apple80211RawGet` con `req_data` NULL pide
  salida de 4 bytes (`nativeCaller`), y el user client codifica los GET escalares como un `value`
  de 4 bytes copiado al buffer (`encodeScalarResult`, `WirelessABI.hpp:119-127`; lista
  `ScalarGetRequests`, `Profiles.hpp:18`). Los stubs de la capa que devuelven scalars deben
  escribir esos 4 bytes en `req.value`, no en `req.data`.
- **La disponibilidad se anuncia con evento 0x37 y se re-replica**: el estado "driver available"
  deriva del estado de energía (petición 19) con razón `0xE0821803`
  (`encodePowerAvailability`, `WirelessABI.hpp:188-208,34`) — misma constante que la capa ya vio
  como "no disponible" en getters (`Docs/PHASE5_IO80211.md`, getRATE). Para que un cliente vea el
  adaptador "listo" aunque el monitor empiece tarde, hace falta retener el último evento de
  disponibilidad por interfaz (`availability[]`/`copyAvailability`, `Bridge.cpp:61-66,423-436`).
- **PMK protegida**: la petición 0x178/376 exige entitlement propio
  (`com.apple.private.wifi.IO80211.get_pmk`, `UserClient.cpp:301`); la capa RTL no debe responder
  esa petición sin filtro equivalente si algún día la expone.

## 8. No verificado / abierto

- Procedencia exacta (build de macOS) de cada binario OCLP; versiones `CFBundleVersion` de los
  kexts inyectados `?` (los binarios no están en el repositorio fuente).
- Mecánica del `Block → Exclude` de OC sobre un fileset del KC, y estado real de la
  `IO80211Family` moderna del KC en ese arranque `?`.
- Números de petición de `getAwdlCtl`/`setAwdlCtl` y de la petición 564 (`filterMonitorEvents` de
  Tahoe) contra el desensamblado de 25G241 `?`.
- Comportamiento de OSMetaClass ante metaclases homónimas en dos familias cargadas a la vez `?`.
