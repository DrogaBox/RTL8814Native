# Auditoría de las cadenas de log de la etapa 1

Contraste entre los patrones que `AWDL_STAGE1_TEST_LOG_PATTERNS.md` espera y las cadenas que el
código emite de verdad. Fuentes auditadas: `Source/AirLayer/AWDLInterface.cpp` (completo, sus 14
llamadas `LOG`), las partes AWDL de `Source/AirLayer/RTLAirLayer.cpp` (handler del 94, creación y
teardown de `awdl0`, arranque con `rtlawdl`), la puerta de carga (`Source/AirLayer/VersionGate.h`
con `GateLogic.h` y `KnownBuilds.inc`) y el camino del log (`airLog`/`rtlAirLog`). Ningún archivo
auditado fue modificado.

## 1. Veredictos y convenciones

Los literales se comprobaron contra el árbol actual (commit `439c713`). Los números de línea de las tablas corresponden a un árbol anterior a la acción inerte de las colas de completion y se han desplazado unas líneas en `AWDLInterface.cpp` (p. ej. `born` en `:225`, `enable`/`disable` en `:319`/`:327`); los textos no cambiaron.

| Veredicto | Significado |
|---|---|
| VERBATIM | el código emite el literal exacto que el doc busca |
| OTRO TEXTO | el camino emite log, con texto distinto del buscado |
| NO EMITE | ninguna llamada de log en ese camino |
| wifip2pd | el emisor del literal es el binario del daemon, no el kext; fuera de las fuentes auditadas |

- `?` marca lo que estas fuentes no cierran: todo lo relativo al binario de `wifip2pd` y los
  valores interpolados en runtime (nombres BSD, códigos de error).
- «Línea» cita el árbol actual (`file:line`); se audita la format string de la llamada, y el valor
  interpolado se indica aparte cuando importa.
- Se verifica además la nota §1 del doc de patrones sobre el prefijo, y la vigencia de sus citas
  de línea.

## 2. Prefijo del macro (nota §1 del doc de patrones: confirmada)

- `LOG` en `AWDLInterface.cpp:24` apunta a `rtlAirLog` (`RTLAirLayer.cpp:62-70`), que formatea el
  buffer y llama a `airLog("%s", buf)` sin añadir nada.
- `airLog` (`RTLAirLayer.cpp:31-56`) guarda el buffer tal cual en el anillo `RTLAirLayerLog`
  (200 líneas, `RTLAirLayer.cpp:25-26`) del nodo `RTL8814Native`; el prefijo `RTL8814AirLayer: `
  solo se añade en la rama `IOLog` (`RTLAirLayer.cpp:38`), canal no observable con `log show` en
  este sistema.
- Conclusión: los `grep` del doc de patrones sin prefijo son correctos para el anillo.

## 3. Patrones del kernel

| Prueba | Patrón del doc | Cadena real del código | Línea | Veredicto |
|---|---|---|---|---|
| T1.1 (frag. 1) | `RTLAirGate = "open-validated: ..."` | propiedad `RTLAirGate` = `snprintf("%s: %s", gateVerdictName(v), why)` | `VersionGate.h:57` y `:81` | VERBATIM (condicionado, ver abajo) |
| T1.1 (frag. 2) | `gate open-validated` | `LOG("gate %s: %s", gateVerdictName(v), why)` | `VersionGate.h:108` | VERBATIM (condicionado, ver abajo) |
| T1.2 | `?` log del handler del 94 con `rtlawdl=0` (retorno 102) | — | `RTLAirLayer.cpp:586-587` | NO EMITE: con `rtlawdl=0` (`awdlAllowed()`, `RTLAirLayer.hpp:182`) el handler retorna `kIOReturnUnsupported` en silencio |
| T1.2 (candidata) | `Failed to create interface %s on %s with error %@` | — | — | wifip2pd; el kext no la emite; aplicabilidad al camino AWDL del daemon sigue `?` |
| T1.3 | `awdl: born %s` → `awdl: born awdl0` | `LOG("awdl: born %s", _bsdName)` | `AWDLInterface.cpp:217` | VERBATIM el formato; el valor interpolado lo compone la familia (`?` hasta hardware, esperado `awdl0` por prefijo+unidad 0 del RegInfo) |
| T2.2 | `awdl: create role 6` | — | — | NO EMITE: esa cadena no existe; el éxito del 94 loguea `awdl: created %s` (`RTLAirLayer.cpp:1809`) |
| T2.2 | `awdl: registerEthernetInterface 0x%x` → `0x0` | `LOG("awdl: registerEthernetInterface 0x%x", r)` | `AWDLInterface.cpp:199` | VERBATIM el formato; el `0x0` sale de `kIOReturnSuccess` en el camino correcto |
| T2.3 | `?` caso duplicado (retorno 17) | — | `RTLAirLayer.cpp:1787-1791` | NO EMITE: el camino duplicado copia el nombre a `out` y retorna `EEXIST` sin log |
| T2.4 | `?` rechazo del rol 7 (retorno 102) | `LOG("awdl: invalid role %u", req->role)` → `awdl: invalid role 7` | `RTLAirLayer.cpp:583` | OTRO TEXTO (resuelve el `?`: sí hay log, con este texto) |
| T3.4 | `awdl: registerEthernetInterface 0x%x` | idem T2.2 | `AWDLInterface.cpp:199` | VERBATIM; confirmado que no produce el `=` que el spec §14 T3.4 escribía |
| T3.4 | `awdl: born %s` | idem T1.3 | `AWDLInterface.cpp:217` | VERBATIM |
| T3.4 | `awdl: init() failed` | idem | `AWDLInterface.cpp:105` | VERBATIM |
| T3.4 | `awdl: base init failed` | idem | `AWDLInterface.cpp:115` | VERBATIM |
| T3.4 | `awdl: attach failed` | idem | `AWDLInterface.cpp:120` | VERBATIM |
| T3.4 | `awdl: packet pool alloc failed` | idem | `AWDLInterface.cpp:144` | VERBATIM |
| T3.4 | `awdl: packet queue alloc failed` | idem | `AWDLInterface.cpp:152` | VERBATIM |
| T3.4 | `awdl: no work queue` | idem | `AWDLInterface.cpp:157` | VERBATIM |
| T3.4 | `awdl: base start failed` | idem | `AWDLInterface.cpp:169` | VERBATIM |
| T3.4 | `awdl: reginfo alloc failed` | idem | `AWDLInterface.cpp:174` | VERBATIM |
| T3.4 | `awdl: datapath failed` | idem | `AWDLInterface.cpp:194` | VERBATIM |
| T3.4 | `awdl: no bsd name` | idem | `AWDLInterface.cpp:212` | VERBATIM |
| T5.2 | `?` log del handler del 97 en el kernel | — | `RTLAirLayer.hpp:99-100` | NO EMITE: el kext no declara handler del 97 (única override de la familia VIRTUAL_IF: `setVIRTUAL_IF_CREATE`); la petición la resuelve la familia sin log del kext |
| T7.2 | `awdl: born %s` (ausencia esperada) | idem T1.3 | `AWDLInterface.cpp:217` | VERBATIM |
| T7.2 | `awdl: enable 0x%x` / `awdl: disable 0x%x` | `LOG("awdl: enable 0x%x", opts)` / `LOG("awdl: disable 0x%x", opts)` | `AWDLInterface.cpp:311` y `:319` | VERBATIM; las citas del doc (`:296` y `:302`) quedaron desfasadas por el commit de colas (`e50d7cf`) |
| T7.3 | `awdl: registerEthernetInterface 0x0` y `awdl: born awdl0` | idem T2.2 / T1.3 | `AWDLInterface.cpp:199` y `:217` | VERBATIM el formato; conteo 5+5 sujeto a los valores interpolados (`?` hasta hardware) |

Condiciones del T1.1: la puerta solo decide una vez, en la primera `airGateOpen()` (`VersionGate.h:91-111`),
que se alcanza con el boot-arg `rtlair` armado (`RTLAirLayer.cpp:1392-1396`). Con estructura
coincidente y build validado el veredicto es `open-validated` (`GateLogic.h:69,120`); el build en
curso (25G241) es la única entrada de la lista validada (`KnownBuilds.inc:16-17`). Con estructura
distinta los textos serían `closed-structure` / `closed-unknown-build` (`GateLogic.h:72-73`). La
propiedad `RTLAirGate` es un par del nodo `RTL8814Native` (no una línea del anillo), pero el
`ioreg` exploratorio del doc la muestra igualmente junto a `RTLAirGateBuild` (`VersionGate.h:82`).

## 4. Patrones de wifip2pd (emisor: el daemon, no el kext)

Ninguno de estos literales aparece en el código del kext; todos proceden del binario `wifip2pd`
citado en los documentos de RE. El kext no puede emitirlos ni impedirlos; las expectativas de
presencia/ausencia del doc dependen del daemon y del hardware.

| Prueba | Literal | Veredicto |
|---|---|---|
| T1.2 | `Failed to create interface %s on %s with error %@` | wifip2pd; aplicabilidad al camino AWDL `?` |
| T1.2/T1.3/T4.2/T5.3 | `Timed out while waiting for interface %s` | wifip2pd |
| T4.1 | `Found new interface %s with role %s` | wifip2pd |
| T4.3 | `Ignoring %s because its not a WiFi interface` | wifip2pd |
| T4.3 | `Ignoring %s because no interface index found` | wifip2pd |
| T4.3 | `Matched a service but unable to query its interface name` | wifip2pd |
| T4.3 | `Unable to find role for interface: %s` | wifip2pd |
| T4.3 | `Failed to bind to %s[%s]: %@` | wifip2pd |
| T4.4/T7.1/T7.3 | `No AWDL interface found. Creating AWDL interface` | wifip2pd |
| T5.3 | `?` error de padre tras el 97 | sigue `?`: ninguna fuente lo da |
| T6.1 | `Creating companion low latency interface` | wifip2pd |
| T6.1 | `Failed to create low latency interface for %s because %@` | wifip2pd |
| T6.1 | `Failed to create interface %s on %s with error %@` (par `Low Latency`) | wifip2pd |
| T6.1 | `Failed to create the companion low latency interface for AWDL: %@` | wifip2pd |
| T6.1 | `Registering for Power State changes from wifid` | wifip2pd |
| T6.1 | `Ignoring request to create AWDL companion low latency interface since it is already present: %s` | wifip2pd |
| T6.1 | `Failed to create low latency interface because parent %s was not found` | wifip2pd |
| T6.1/T6.3 | `Timed out while waiting for the low latency interface with parent %s` | wifip2pd |
| T6.1 | `Failed to find the interface name returned when creating the low latency interface for %s` | wifip2pd |
| T7.1 | `?` literales del registro de monitoreo | sigue `?` (`AWDL_WIFIP2PD_P2P_EVENTS.md`) |
| T7.2 | `?` limpieza del handle en `IOServiceTerminate` | sigue `?` |
| T7.2 | `Attempting recovery for %s[%s] with %s on %s` | wifip2pd |
| T7.3 | `Already discovered %s` | wifip2pd |
| T7.3 | `Failed to recover the low latency interface %s: %@` | wifip2pd |

## 5. LOG que el código emite y el doc de patrones no menciona

`AWDLInterface.cpp` no aporta ninguno: sus 14 llamadas están todas cubiertas. Los siguientes son
del kext, en el camino AWDL y la puerta:

| Cadena (formato) | Línea | Cuándo sale | Nota |
|---|---|---|---|
| `awdl: invalid role %u` | `RTLAirLayer.cpp:583` | 94 con rol distinto de 6 | El doc lo buscaba como `?` sin literal (§9 fila 3); el texto real es este |
| `awdl: alloc failed` | `RTLAirLayer.cpp:1795` | `OSTypeAlloc` sin objeto en `createAwdlInterface` | Fallo del 94 no cubierto por las filas T3.4 |
| `awdl: init failed` | `RTLAirLayer.cpp:1799` | `initWithController` o `startWithRegistration` fallan | Distinto de `awdl: base init failed` de `AWDLInterface.cpp:115` |
| `awdl: created %s` | `RTLAirLayer.cpp:1809` | 94 con rol 6 exitoso | Único log de éxito del 94; el doc no lo lista (T1.2/T2.2/T4.1 podrían usarlo) |
| `awdl: teardown` | `RTLAirLayer.cpp:1830` | `destroyAwdlInterface` (stop o fin) | Relevante para T7.2: huella de la descarga en el anillo |
| `gate %s: %s` | `VersionGate.h:108` | primera `airGateOpen()` | Estaba como fragmento `?` (§9 fila 1); texto real `gate <veredicto>: <razón>` |
| `version gate closed, staying out (en2 is unaffected)` | `RTLAirLayer.cpp:1397` | puerta cerrada | Cambia la expectativa de T1.1 si el sistema no valida |
| `start 0.32, rtlair=%u rtlairstep=%u rtlawdl=%u` | `RTLAirLayer.cpp:1457` | arranque del controlador | Eco del `rtlawdl`; útil de triage para T1.2/T1.3 |
| `@%llu stub %s` | `RTLAirLayer.cpp:131` | primera llamada de cada stub, volcado por el hilo de flush | En el primer 94 aparece `@<ms> stub setVIRTUAL_IF_CREATE`; trazado genérico, no AWDL |
| `not armed (boot-arg rtlair=1, or 2 with data path), staying out` | `RTLAirLayer.cpp:1393` | nub sin `rtlair` | Adyacente a T1: sin `rtlair` no hay puerta ni `awdl0` |

El resto de las llamadas `LOG` de `RTLAirLayer.cpp` (camino STA/WCL: escaneo, asociación, join,
roam, CoreCapture) están fuera del alcance AWDL de esta auditoría.

## 6. Vigencia de las citas de línea del doc de patrones

Todas las citas de `AWDLInterface.cpp` del doc (105, 115, 120, 144, 152, 157, 169, 174, 194, 199,
212, 217) siguen exactas. Las únicas desfasadas son las de `enable`/`disable` en §8 T7.2
(`:296` y `:302` frente a las reales `:311` y `:319`, por el commit de colas `e50d7cf`). Los
literales en sí son correctos en ambos casos.

## 7. Resolución de las filas `?` del doc de patrones (§9)

| # | Prueba | Estado tras la auditoría |
|---|---|---|
| 1 | T1.1 | Resuelto: propiedad `RTLAirGate` (`VersionGate.h:81`, texto `"<verdict>: <why>"` de `:57`) y línea `gate <verdict>: <why>` (`:108`). Con estructura coincidente y build 25G241 el veredicto es `open-validated` (`GateLogic.h:69`; `KnownBuilds.inc:16-17`) |
| 2 | T1.2 | Sigue `?`: epílogo de error del caso AWDL en el binario `wifip2pd` |
| 3 | T1.2 / T2.4 | Medio resuelto: rol 7 sí loguea (`awdl: invalid role 7`, `RTLAirLayer.cpp:583`); `rtlawdl=0` no loguea nada (`RTLAirLayer.cpp:586-587`) |
| 4 | T2.2 | Resuelto: `awdl: create role 6` no existe; el éxito loguea `awdl: created %s` (`RTLAirLayer.cpp:1809`) |
| 5 | T2.3 | Resuelto: el caso duplicado retorna `EEXIST` sin log (`RTLAirLayer.cpp:1787-1791`) |
| 6 | T5.2 | Resuelto: no hay handler del 97 en el kext (`RTLAirLayer.hpp:99-100`), luego ningún log del kext en ese camino |
| 7 | T5.3 | Sigue `?`: closure del `RawGet` 97 en el binario `wifip2pd` |
| 8 | T6.1 | Sigue `?`: recuento de las «2 líneas», solo cierra en hardware |
| 9 | T7.1 | Sigue `?`: literales en `AWDL_WIFIP2PD_P2P_EVENTS.md` |
| 10 | T7.2 | Sigue `?`: limpieza del handle en el binario `wifip2pd` |
