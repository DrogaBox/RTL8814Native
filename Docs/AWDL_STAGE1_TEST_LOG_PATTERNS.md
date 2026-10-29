# Patrones de log de las pruebas T1–T7 de la etapa 1

Doc de apoyo de `AWDL_STAGE1_IMPL_SPEC.md` §14: fija los literales exactos que cada prueba de
hardware debe buscar en los logs, para que nadie tenga que adivinar cadenas en el momento de la
prueba. Los literales proceden exclusivamente de estos documentos y código, citados por archivo:

- `Docs/AWDL_WIFIP2PD_LOGSTRINGS.md`
- `Docs/AWDL_WIFIP2PD_DISCOVERY.md`
- `Docs/AWDL_WIFIP2PD_POST_CREATE.md`
- `Docs/AWDL_LLW_COMPANION.md`
- `Docs/AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md`
- `Source/AirLayer/AWDLInterface.cpp` (llamadas `LOG`, citadas por línea)

Todo literal que ninguna de esas fuentes dé verbatim queda marcado `?` y se recoge en la tabla
final §9; no se inventa ninguna cadena. Los ids de prueba (T1.1–T7.3) y lo esperado son los de
`AWDL_STAGE1_IMPL_SPEC.md` §14; este documento solo añade los patrones de log.

## 1. Canales de log y forma de los comandos

Dos canales según el emisor:

- **wifip2pd** (daemon, espacio de usuario): log unificado del sistema.

  ```
  log show --last 3m --info --debug --predicate 'process == "wifip2pd"' | grep -E '<patrón>'
  ```

- **kernel** (kext `RTL8814AirLayer`): `IOLog` no llega a `log show` en 26.7.1 (comentario de
  `Source/AirLayer/RTLAirLayer.cpp:20-24`); las líneas de `LOG` de `AWDLInterface.cpp` y del resto
  del kext caen en el anillo `RTLAirLayerLog` del nodo IORegistry `RTL8814Native` (200 líneas como
  máximo, se sobreescriben; leer cuanto antes tras la acción):

  ```
  ioreg -r -c RTL8814Native -d 1 | grep -E '<patrón>'
  ```

Notas comunes:

- En el anillo `ioreg` las líneas van sin prefijo (`awdl: born awdl0`); por `IOLog` llevarían el
  prefijo `RTL8814AirLayer: `, canal que en este sistema no es observable con `log show`.
- Los literales de wifip2pd son format strings de `os_log`: `%s`/`%@` se sustituyen en runtime
  (p. ej. `Found new interface %s with role %s` → `Found new interface awdl0 with role AirLink`;
  el string `AirLink` para el rol 6 está cerrado en `AWDL_KERNEL_ROLE_TABLE.md`, citado por
  `AWDL_WIFIP2PD_POST_CREATE.md` §1.2). Los `grep` usan solo las partes constantes del literal,
  para no depender de la interpolación.
- Si los campos interpolados se muestran como `<private>`, repetir con
  `sudo log show --private ...` (mismo predicado).
- Filas **ausente**: mismo comando con `grep -cE`; la salida esperada es `0`.
- La ventana `--last 3m` debe cubrir toda la acción de la prueba; ampliar si la prueba dura más.
- El logger del camino AirLink usa el subsistema `com.apple.awdl`
  (`AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 7); alternativa de predicado:
  `--predicate 'subsystem == "com.apple.awdl"'`.
- El `log show` del spec §14 T4.1 usa `--last 2m` sin `--info --debug`; aquí se usa una ventana de
  3 m con `--info --debug` porque el nivel de estos mensajes no está documentado.

## 2. T1 — Carga y gate

| Prueba | Literal (verbatim) | Fuente | Proceso | Esperado | Comando |
|---|---|---|---|---|---|
| T1.1 | `?` — literal de la puerta de carga; el spec §14 T1.1 solo da fragmentos con elipsis (`RTLAirGate = "open-validated: ..."` y `gate open-validated`) | — (ver §9, fila 1) | kernel | presente | `ioreg -r -c RTL8814Native -d 1 \| grep -iE 'gate'` (exploratorio) |
| T1.2 | `?` — literal del fallo de creación del 94 en el daemon; el spec §14 T1.2 lo deja `?`. Candidata: `Failed to create interface %s on %s with error %@`, que `AWDL_LLW_COMPANION.md` §2.1 da verbatim pero verificada solo para el epílogo del caso 4 (companion) | `AWDL_LLW_COMPANION.md` §2.1 (sitio `0x100048fb5`) — aplicabilidad `?` (ver §9, fila 2) | wifip2pd | presente (`?`) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create interface'` |
| T1.2 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | wifip2pd | **ausente** — las ramas de error del create señalan el semáforo antes de salir, sin espera de 30 s (`AWDL_WIFIP2PD_POST_CREATE.md` §1.4, último punto) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T1.2 | `?` — log del handler del 94 con `rtlawdl=0` (retorno 102) | — (ver §9, fila 3) | kernel | `?` | `ioreg -r -c RTL8814Native -d 1` (inspección del anillo) |
| T1.3 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | wifip2pd | **ausente** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T1.3 | `awdl: born %s` → `awdl: born awdl0` | `Source/AirLayer/AWDLInterface.cpp:217` | kernel | presente (mismo patrón que T3.4) | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: born'` |

## 3. T2 — Capacidad y puerta

T2.1 no tiene componente de log: la comprobación es el copyback del `RawGet` 12 (byte `+0x08`
bit 0 a 1, byte `+0x0D` bit 0 a 0).

| Prueba | Literal (verbatim) | Fuente | Proceso | Esperado | Comando |
|---|---|---|---|---|---|
| T2.2 | `?` — `awdl: create role 6`, citado en el spec §14 T2.2; sin verbatim en las fuentes permitidas (el handler del 94 está fuera de `AWDLInterface.cpp`) | — (ver §9, fila 4) | kernel | presente (`?`) | `ioreg -r -c RTL8814Native -d 1 \| grep -iE 'create'` (exploratorio) |
| T2.2 | `awdl: registerEthernetInterface 0x%x` → `awdl: registerEthernetInterface 0x0` | `Source/AirLayer/AWDLInterface.cpp:199` | kernel | presente; su aparición es la evidencia del retorno 0 en menos de 1 s | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: registerEthernetInterface'` |
| T2.3 | `?` — sin literal documentado para el caso duplicado; la observación es el retorno 17 y el mismo nombre en `+0x10` | — (ver §9, fila 5) | kernel | `?` | — |
| T2.4 | `?` — sin literal documentado para el rechazo del rol 7 (APSTA fuera de alcance); la observación es el retorno 102 | — (ver §9, fila 3) | kernel | `?` | — |

## 4. T3 — Nacimiento

T3.1–T3.3 no tienen componente de log (`ioreg -r -c RTLAwdInterface -l`, `ifconfig awdl0`,
`if_nametoindex("awdl0")`).

| Prueba | Literal (verbatim) | Fuente | Proceso | Esperado | Comando |
|---|---|---|---|---|---|
| T3.4 | `awdl: registerEthernetInterface 0x%x` → `awdl: registerEthernetInterface 0x0` | `Source/AirLayer/AWDLInterface.cpp:199` | kernel | presente. Nota: el spec §14 T3.4 escribe `awdl: registerEthernetInterface = 0x0` con un `=` que el código no produce; la forma real es sin `=` | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: registerEthernetInterface'` |
| T3.4 | `awdl: born %s` → `awdl: born awdl0` | `Source/AirLayer/AWDLInterface.cpp:217` | kernel | presente | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: born'` |

Literales de fallo del nacimiento, todos de `Source/AirLayer/AWDLInterface.cpp`, todos del proceso
kernel y todos con **ausencia esperada** en un nacimiento correcto (cualquiera presente indica el
camino de fallo que lo impidió):

| Prueba | Literal (verbatim) | Línea |
|---|---|---|
| T3.4 | `awdl: init() failed` | `AWDLInterface.cpp:105` |
| T3.4 | `awdl: base init failed` | `AWDLInterface.cpp:115` |
| T3.4 | `awdl: attach failed` | `AWDLInterface.cpp:120` |
| T3.4 | `awdl: packet pool alloc failed` | `AWDLInterface.cpp:144` |
| T3.4 | `awdl: packet queue alloc failed` | `AWDLInterface.cpp:152` |
| T3.4 | `awdl: no work queue` | `AWDLInterface.cpp:157` |
| T3.4 | `awdl: base start failed` | `AWDLInterface.cpp:169` |
| T3.4 | `awdl: reginfo alloc failed` | `AWDLInterface.cpp:174` |
| T3.4 | `awdl: datapath failed` | `AWDLInterface.cpp:194` |
| T3.4 | `awdl: no bsd name` | `AWDLInterface.cpp:212` |

Comando único para las diez filas anteriores (salida vacía = éxito):

```
ioreg -r -c RTL8814Native -d 1 | grep -E 'awdl: (init\(\) failed|base init failed|attach failed|packet pool alloc failed|packet queue alloc failed|no work queue|base start failed|reginfo alloc failed|datapath failed|no bsd name)'
```

## 5. T4 — Aceptación por el daemon

Todas las filas son de wifip2pd (la columna de proceso se omite por eso).

| Prueba | Literal (verbatim) | Fuente | Esperado | Comando |
|---|---|---|---|---|
| T4.1 | `Found new interface %s with role %s` → `Found new interface awdl0 with role AirLink` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 8 (string `0x100494bb0`) | presente | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Found new interface'` |
| T4.2 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | **ausente**. Nota: el `%s` interpola el nombre de rol (`AWDL`), no `awdl0` (`AWDL_LLW_COMPANION.md` §2.2); la forma del spec `Timed out while waiting for interface awdl0` no es la que produce el binario — de ahí que el grep use solo la parte constante | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T4.3 | `Ignoring %s because its not a WiFi interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 3 (string `0x100494b00`); también `AWDL_WIFIP2PD_DISCOVERY.md` | **ausente** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'because its not a WiFi interface'` → `0` |
| T4.3 | `Ignoring %s because no interface index found` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 4 (string `0x100494b30`); también `AWDL_WIFIP2PD_DISCOVERY.md` | **ausente** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'because no interface index found'` → `0` |
| T4.3 (compl.) | `Matched a service but unable to query its interface name` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 1 (string `0x100494ac0`) | ausente (complemento: falta `IOInterfaceName` en el registro) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'unable to query its interface name'` → `0` |
| T4.3 (compl.) | `Unable to find role for interface: %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 9 (string `0x100494b60`); también `AWDL_WIFIP2PD_DISCOVERY.md` | ausente (complemento: ninguna cadena de rol casó) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Unable to find role for interface'` → `0` |
| T4.3 (compl.) | `Failed to bind to %s[%s]: %@` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 7 (string `0x100494b90`) | ausente (complemento: fallo de `Apple80211Open`/bind) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Failed to bind to'` → `0` |
| T4.4 | `No AWDL interface found. Creating AWDL interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 (string `0x1004a1350`); también `AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md` §1 | presente exactamente una vez por ciclo de carga; más de una = bucle de re-creación | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found'` → `1` |

Comando conjunto de barrido para T4 (presente y ausentes a la vez):

```
log show --last 3m --info --debug --predicate 'process == "wifip2pd"' | grep -E "Found new interface|Timed out while waiting|Ignoring |Unable to find role|Failed to bind|No AWDL interface found|unable to query"
```

## 6. T5 — Medios y 97

T5.1 y T5.2 no tienen log en el camino documentado: T5.1 es la lectura de `ifm_current` por
`SIOCGIFMEDIA` y T5.2 el copyback del `RawGet` 97 (nombre del padre sin NUL).

| Prueba | Literal (verbatim) | Fuente | Proceso | Esperado | Comando |
|---|---|---|---|---|---|
| T5.2 | `?` — log del handler del 97 en el kernel, si lo hay | — (ver §9, fila 6) | kernel | `?` | — |
| T5.3 | `Timed out while waiting for interface %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.4 (string `0x100493910`) | wifip2pd | **ausente** | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for interface'` → `0` |
| T5.3 | `?` — literal de "error de padre" en el daemon tras el 97: ningún documento de la lista da una cadena para el fallo del 97 | — (ver §9, fila 7) | wifip2pd | `?` | — |

## 7. T6 — Companion opcional (decisión D6: el kext no implementa el 293)

Con D6 el dispatch moderno devuelve 102 para el 293 y el fallo es inmediato, sin espera
(`AWDL_LLW_COMPANION.md` §2.1 y §4). T6.2 no tiene log (`ifconfig llw0`).

| Prueba | Literal (verbatim) | Fuente | Proceso | Esperado | Comando |
|---|---|---|---|---|---|
| T6.1 | `Creating companion low latency interface` | `AWDL_LLW_COMPANION.md` §1.3 paso 4 (string `0x1004a36b0`, sitio `0x10041b48b`) | wifip2pd | presente (precede al intento de creación) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Creating companion low latency interface'` |
| T6.1 | `Failed to create low latency interface for %s because %@` → `Failed to create low latency interface for awdl0 because ...`; el `%@` es el código devuelto por el `RawSet` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 fila 5 (string `0x100494cf0`); también `AWDL_LLW_COMPANION.md` §1.1 | wifip2pd | presente (fallo del `RawSet` 293) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create low latency interface for'` |
| T6.1 | `Failed to create interface %s on %s with error %@`; el primer `%s` lleva el nombre de rol `Low Latency` | `AWDL_LLW_COMPANION.md` §2.1 (string `aFailedToCreate_1`, sitio `0x100048fb5`) | wifip2pd | presente (`?` sobre la pareja exacta: el spec §14 T6.1 cuenta "2 líneas" y qué dos sean no cierra con los documentos; ver §9, fila 8) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create interface'` |
| T6.1 | `Failed to create the companion low latency interface for AWDL: %@` | `AWDL_LLW_COMPANION.md` §2.3 (string `0x1004a75c0`, sitio `0x100417ead`) | wifip2pd | presente (el init de `AWDLInterface` absorbe el error y continúa) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to create the companion low latency interface for AWDL'` |
| T6.1 | `Registering for Power State changes from wifid` | `AWDL_LLW_COMPANION.md` §2.3 | wifip2pd | presente — confirma que el init continuó tras el fallo, coherente con `awdl0` intacta | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Registering for Power State changes from wifid'` |
| T6.1 | `Ignoring request to create AWDL companion low latency interface since it is already present: %s` | `AWDL_LLW_COMPANION.md` §1.3 paso 4 (string `0x1004a7a00`) | wifip2pd | **ausente** (no hay companion previa en etapa 1) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Ignoring request to create AWDL companion'` → `0` |
| T6.1 | `Failed to create low latency interface because parent %s was not found` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 fila 5 (string `0x100494ca0`); también `AWDL_LLW_COMPANION.md` §1.1 | wifip2pd | **ausente** (el padre `awdl0` existe) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'because parent'` → `0` |
| T6.1/T6.3 | `Timed out while waiting for the low latency interface with parent %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 fila 5 (string `0x1004939a0`); `AWDL_WIFIP2PD_LOGSTRINGS.md` §6; `AWDL_LLW_COMPANION.md` §2.2 (sitio `0x100048beb`) | wifip2pd | **ausente** — la espera de 30 s de la companion solo corre si el 293 responde éxito sin nombre verificable (`AWDL_LLW_COMPANION.md` §2.2); el rechazo de D6 es camino rápido. La forma corta del spec T6.3 (`Timed out while waiting for the low latency interface`) es el prefijo constante de esta string | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Timed out while waiting for the low latency interface'` → `0` |
| T6.1 (compl.) | `Failed to find the interface name returned when creating the low latency interface for %s` | `AWDL_WIFIP2PD_LOGSTRINGS.md` §6 (string `0x100493940`) | wifip2pd | ausente (complemento: solo el camino "éxito sin nombre verificable" lo produce, evitado con D6) | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Failed to find the interface name returned'` → `0` |

## 8. T7 — Monitoreo y descarga

| Prueba | Literal (verbatim) | Fuente | Proceso | Esperado | Comando |
|---|---|---|---|---|---|
| T7.1 | `?` — literales del registro de monitoreo (`Apple80211StartMonitoringEvent` / `Apple80211RawP2PEventMonitoringInit`): están en `AWDL_WIFIP2PD_P2P_EVENTS.md`, que no está en la lista de fuentes permitida de este documento | — (ver §9, fila 9) | wifip2pd | `?` | — |
| T7.1 | `No AWDL interface found. Creating AWDL interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 (string `0x1004a1350`) | wifip2pd | **ausente** tras la primera creación: una sola vez en total; reaparecer = re-creación de `awdl0` | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found'` → `1` |
| T7.2 | `?` — literal de la limpieza del handle al recibir `IOServiceTerminate`: sin log documentado (`AWDL_WIFIP2PD_POST_CREATE.md` §1.1 describe la limpieza sin señalizar ni string conocida) | — (ver §9, fila 10) | wifip2pd | `?` | — |
| T7.2 | `awdl: born %s` | `Source/AirLayer/AWDLInterface.cpp:217` | kernel | **ausente** tras la descarga (no debe reaparecer sin una nueva carga) | `ioreg -r -c RTL8814Native -d 1 \| grep -cE 'awdl: born'` |
| T7.2 | `Attempting recovery for %s[%s] with %s on %s` | `AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md` §4.4 (string `0x100047156`) | wifip2pd | `?` diagnóstico: si aparece, la descarga disparó recuperación del daemon en lugar de una limpieza limpia | `log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Attempting recovery for'` |
| T7.2 | `awdl: enable 0x%x` / `awdl: disable 0x%x` | `Source/AirLayer/AWDLInterface.cpp:296` y `:302` | kernel | `?` diagnóstico (el momento en que la familia las invoca no está documentado) | `ioreg -r -c RTL8814Native -d 1 \| grep -E 'awdl: (enable\|disable)'` |
| T7.3 | `awdl: registerEthernetInterface 0x0` y `awdl: born awdl0` | `Source/AirLayer/AWDLInterface.cpp:199` y `:217` | kernel | presente exactamente una vez por ciclo de carga (5 ciclos → 5 y 5); un doble registro en el mismo ciclo es defecto | `ioreg -r -c RTL8814Native -d 1 \| grep -cE 'awdl: registerEthernetInterface'` → `5` |
| T7.3 | `No AWDL interface found. Creating AWDL interface` | `AWDL_WIFIP2PD_POST_CREATE.md` §2.1 (string `0x1004a1350`) | wifip2pd | presente una vez por ciclo de carga | `log show --last 5m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'No AWDL interface found'` → `5` |
| T7.3 | `Already discovered %s` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.2 paso 2 (string `0x100494be0`) | wifip2pd | `?` diagnóstico: acumulación entre ciclos indicaría estado residual del daemon | `log show --last 5m --info --debug --predicate 'process == "wifip2pd"' \| grep -cE 'Already discovered'` |
| T7.3 | `Failed to recover the low latency interface %s: %@` | `AWDL_LLW_COMPANION.md` §2.3 (string `0x1004a7b70`, sitio `0x100419a2a`) | wifip2pd | `?` diagnóstico (no debe dispararse la recuperación de la companion en los ciclos) | `log show --last 5m --info --debug --predicate 'process == "wifip2pd"' \| grep -E 'Failed to recover the low latency interface'` |

## 9. Literales sin fuente verbatim (`?`)

Ningún literal fue inventado; los `?` del spec §14 se heredan y se amplían con lo que las fuentes
permitidas de este documento no cubren.

| # | Prueba | Literal buscado | Por qué es `?` | Dónde se resolvería |
|---|---|---|---|---|
| 1 | T1.1 | Literal completo de la puerta de carga (`RTLAirGate = "open-validated: ..."` y la línea `gate open-validated`; el spec §14 T1.1 solo da fragmentos con elipsis) | Ninguna fuente permitida lo da verbatim | Código de la puerta en `Source/AirLayer/RTLAirLayer.cpp`, fuera de la lista de fuentes de este documento |
| 2 | T1.2 | Literal del fallo de creación del 94 en wifip2pd (el propio spec §14 T1.2 lo marca `?`) | La candidata `Failed to create interface %s on %s with error %@` (`AWDL_LLW_COMPANION.md` §2.1) está verificada solo para el epílogo del caso 4 (companion), no para el camino AWDL | Epílogo de error del caso 1 (AWDL) de `sub_100047660` en `wifip2pd` |
| 3 | T1.2 / T2.4 | Log del handler del 94 con `rtlawdl=0` o con rol 7 (retornos 102) | Sin literal en las fuentes permitidas | Handler del 94 en `Source/AirLayer/RTLAirLayer.cpp` |
| 4 | T2.2 | `awdl: create role 6` (el spec §14 T2.2 lo cita) | Sin verbatim en las fuentes permitidas (el handler del 94 no está en `AWDLInterface.cpp`) | Handler del 94 en `Source/AirLayer/RTLAirLayer.cpp` |
| 5 | T2.3 | Log del caso duplicado del 94 (retorno 17) | Sin literal documentado en ninguna fuente de la lista | Handler del 94 en `Source/AirLayer/RTLAirLayer.cpp` |
| 6 | T5.2 | Log del handler del 97 en el kernel | Sin literal documentado | Handler `getVIRTUAL_IF_PARENT` del kernel; o sonda propia |
| 7 | T5.3 | Literal del "error de padre" en el daemon tras el 97 | Ningún documento de la lista documenta un log para el fallo del 97 | Closure del `RawGet` 97 en el descubridor de `wifip2pd` (`sub_100046D30` / `sub_100043720`) |
| 8 | T6.1 | Qué dos líneas exactas son las "(2 líneas)" del spec §14 T6.1 | Las tres candidatas están citadas verbatim en la tabla §7; el recuento del spec no cierra con ellas sin ambigüedad | Prueba en hardware: contar las líneas reales |
| 9 | T7.1 | Literales del resultado del registro de monitoreo de eventos | Están en `AWDL_WIFIP2PD_P2P_EVENTS.md`, no incluido en la lista de fuentes permitida de este documento | `AWDL_WIFIP2PD_P2P_EVENTS.md` |
| 10 | T7.2 | Literal de la limpieza del handle al recibir `IOServiceTerminate` | `AWDL_WIFIP2PD_POST_CREATE.md` §1.1 documenta la limpieza sin log conocido | Consumidor de `IOServiceTerminate` en `wifip2pd` (`sub_100045520`) |
