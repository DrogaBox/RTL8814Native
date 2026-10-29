# Revisión del guion de pruebas de hardware T1–T7

Objeto: `~/Desktop/GUION_PRUEBAS_AWDL.md` (fichero local, fuera del repo, no modificado).
Contraste contra `Docs/AWDL_STAGE1_IMPL_SPEC.md` §14, `Docs/AWDL_STAGE1_TEST_LOG_PATTERNS.md`,
`Docs/AWDL_STAGE1_LOG_STRINGS_AUDIT.md`, `Tools/phase5_deploy.sh`, `build_airlayer.sh` y el código
vigente de `Source/AirLayer`. Revisión estática: nada se ejecutó del guion ni se cargó nada.
Las líneas del guion se citan como `GUION:<n>`; las medidas hechas en esta máquina (25G241,
2026-10-29) se marcan «medido». Veredicto global: el guion es coherente con el spec §14 en
estructura, rollback y esperados de daemon, pero contiene tres errores que bloquearían las pruebas
(canal del kernel, literal inexistente del 94 y el `log` builtin de zsh), un mecanismo `rtlawdl=1`
que ya existe y no se usa, y omites las filas T2.1–T2.4, T3.3 y T7.1 del spec.

## Estado tras la verificación

Verificados y aplicados al guion: H1 (`log` es un builtin de zsh, medido con `zsh -fc 'type log'`), H2 (anillo por `ioreg -l`), H3, H5–H17. H6 confirmado: el plist de `wifip2pd` no declara `KeepAlive`. Precisión sobre H4: el filtro de boot-args original no coincidía con `rtlawdl=` (patrón `^rtlair(step|awdl)?=`), de modo que no lo quitaba; corregido en `Tools/phase5_deploy.sh` (commit `4021fb9`), y la forma documentada es `sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh ...`. El guion revisado se mantiene fuera del repo.

## 1. Hallazgos

| # | Paso | Gravedad | Problema | Evidencia | Corrección propuesta |
|---|---|---|---|---|---|
| H1 | Todos los `log show` (GUION:46, 63, 83, 107) | Bloqueante | El guion dice «cada bloque `bash` se pega tal cual» (GUION:3), pero la shell por defecto de macOS 26 es zsh y `log` resuelve al builtin del módulo zsh/log: `log show ...` falla con `zsh:log: N: too many arguments` antes de llegar a `/usr/bin/log` | Medido con `/bin/zsh -f`: `type -a log` → «log is a shell builtin» + «log is /usr/bin/log»; `log show --last 1m --predicate ...` → error del builtin | Prefijar todas las llamadas: `/usr/bin/log show ...` (o `command log show ...`). Alternativa: pegar los bloques en `bash` |
| H2 | T2/T3 (GUION:63-65) | Bloqueante | `log show --last 2m --predicate 'process == "kernel"' \| grep -i "awdl:"` no devolverá nada: en 26.7.1 el `IOLog` del kext no llega a `log show`; las líneas van al anillo `RTLAirLayerLog` del nodo `RTL8814Native` | `Source/AirLayer/RTLAirLayer.cpp:20-24` (comentario y mecanismo), `:38` (`airLog` escribe IOLog y el anillo); `Source/AirLayer/AWDLInterface.cpp:24` (`LOG` → `rtlAirLog`). Medido: el contenido del anillo solo es grep-able con `ioreg -l`; sin `-l` ioreg trunca el valor (0 coincidencias de `gate open-validated` sin `-l`) | `ioreg -r -c RTL8814Native -d 1 -l \| grep -E 'awdl: (registerEthernetInterface\|created\|born\|enable\|disable\|teardown)'` — leer cuanto antes (200 líneas, se sobreescriben) |
| H3 | T2/T3 esperado (GUION:67) | Bloqueante | `awdl: create role 6` no existe: el éxito del 94 emite `awdl: created %s` | `Docs/AWDL_STAGE1_LOG_STRINGS_AUDIT.md` §3 fila T2.2 y §7 fila 4 (NO EMITE); `Source/AirLayer/RTLAirLayer.cpp:1809` | Esperado del 94: `awdl: registerEthernetInterface 0x0` (`AWDLInterface.cpp:207`), `awdl: created awdl0` (`RTLAirLayer.cpp:1809`) y `awdl: born awdl0` (`AWDLInterface.cpp:225`) |
| H4 | T1.2/T1.3 (GUION:49-51) | Mayor | «Para activar `rtlawdl=1` hay que añadirlo a los boot-args … extender `phase5_deploy.sh` o editar a mano»: el script ya lo soporta con la variable `RTL8814_AWDL=1`; además, todo despliegue SIN esa variable borra `rtlawdl=` de los boot-args | `Tools/phase5_deploy.sh:8` (uso), `:70` (la regex de limpieza incluye `rtlair(step\|awdl)?=`), `:73` (añade `rtlawdl=1`) | `cd ~/Desktop/RTL8814Native && sudo RTL8814_AWDL=1 RTL8814_LAYER_SRC=Build/RTL8814AirLayer.kext RTL8814_LAYER_MD5=$(md5 -q Build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer) ./Tools/phase5_deploy.sh "<EFI interna>" layer 2` y reiniciar. Nota en el guion: si se redespliega sin `RTL8814_AWDL=1`, T2+ vuelven a `rtlawdl=0` |
| H5 | Antes de empezar, punto 1 (GUION:8-11) | Mayor | Obsoleto: el parche ya está integrado en el árbol y `build_airlayer.sh` ya compila y enlaza `AWDLInterface.cpp`; el kext con AWDL existe construido | `build_airlayer.sh:40` (compila `AWDLInterface.cpp`) y `:67` (lo enlaza); `Source/AirLayer/RTLAirLayer.cpp:577-589` (el handler del 94 ya está en el árbol); `Build/obj_airlayer/AWDLInterface.o` y `Build/RTL8814AirLayer.kext` presentes (medido, md5 `535c1767…`) | Reducir el punto a: el árbol ya integra el parche y la compilación; basta ejecutar P0 y desplegar. Queda la cautela de que P0 debe recompilar antes de desplegar |
| H6 | T1.2 y T2/T3, disparador (GUION:43-46, 56) | Mayor | «Provocar que el demonio la emita reiniciándolo» asume respawn automático: el plist de launchd de `wifip2pd` no tiene KeepAlive; es on-demand por `MachServices`, así que tras `killall` puede quedar parado y el 94 no se emite | Medido: `/System/Library/LaunchDaemons/com.apple.wifip2pd.plist` solo declara `MachServices`, `ProcessType`, `EnableTransactions/PressuredExit` — sin KeepAlive | Tras el `killall`, comprobar `ps aux \| grep '[w]ifip2pd'`; si no está, despertar el servicio (alternar el Wi-Fi o cerrar/abrir AirDrop; `?` qué cliente exacto lo relanza) o reiniciar. El create corre al arrancar el daemon («No AWDL interface found. Creating AWDL interface», patrones §5 T4.4) |
| H7 | Todos los `log show` del daemon (GUION:46, 83) | Mayor | Sin `--info --debug` los mensajes de wifip2pd pueden no aparecer (el nivel no está documentado); `tail -40` no filtra por los literales | `Docs/AWDL_STAGE1_TEST_LOG_PATTERNS.md` §1 (comandos con `--info --debug`, ventana 3 m) y su nota sobre el spec T4.1 | `/usr/bin/log show --last 3m --info --debug --predicate 'process == "wifip2pd"' \| grep -E '<patrones constantes>'`; si los campos interpolados salen `<private>`, repetir con `sudo /usr/bin/log show --private …` (patrones §1) |
| H8 | Orden global | Mayor | No hay paso de verificación del Wi-Fi normal tras cada reinicio: el despliegue cambia el kext (md5 nuevo) y todo el guion presupone `rtlair=2` vivo antes de tocar AWDL | `Docs/AWDL_STAGE1_IMPL_SPEC.md:1200-1201` («con el gate abierto y `rtlair` en el nivel que ya usa el sistema»); rollback disponible en `Tools/phase5_deploy.sh:106-108` | Paso nuevo tras T1.1 (y tras el redespliegue de H4): comprobar asociación y tráfico por `en2` (p. ej. `ifconfig en2 \| head -5` + ping a la pasarela); si falla, `sudo ./Tools/phase5_deploy.sh "<EFI interna>" inert` antes de continuar |
| H9 | T3.5 (GUION:71-78) | Mayor | Riesgo de bucle de arranque no anotado: con `rtlawdl=1` persistente en el `config.plist`, tras un pánico el arranque siguiente vuelve a crear `awdl0` (el daemon la crea por ciclo, patrones §8); y `ping6 -I awdl0` necesita un `inet6 fe80` en la interfaz, que no está garantizado | Patrones §8 T7.3 (un create por ciclo de carga); `ping6` requiere dirección fuente en la interfaz (`?` que awdl0 la reciba al nacer) | Añadir: antes del ping, `ifconfig awdl0 inet6 \| head -3`; si hay pánico y se repite al arrancar, quitar `rtlawdl` desde Recovery/OCon el `config.plist` o redesplegar sin `RTL8814_AWDL=1` antes de dejar el sistema en reposo |
| H10 | T6 (GUION:83-87) | Mayor | «Dos líneas de `Failed to create low latency interface`» da por bueno un recuento que los documentos no cierran (hay tres cadenas candidatas); faltan los presentes/complementos y la espera de 60 s del spec | Patrones §7 (tres literales T6.1) y §9 fila 8 («el recuento del spec no cierra»); `Docs/AWDL_STAGE1_IMPL_SPEC.md:1270` («esperar 60 s») | Marcar el recuento como `?`; grep de las tres: `Failed to create low latency interface for`, `Failed to create interface` (par `Low Latency`), `Failed to create the companion low latency interface for AWDL`; presentes `Creating companion low latency interface` y `Registering for Power State changes from wifid`; ausentes `because parent`, `Ignoring request to create AWDL companion`, `Timed out while waiting for the low latency interface`; esperar 60 s tras T4 |
| H11 | T7 (GUION:95-103) | Mayor | Falta T7.1 (registro del monitoreo): es la prueba que decide la última discrepancia factual entre documentos y el guion la omite | `Docs/AWDL_STAGE1_SPEC_DELTA.md:286` (fila T7.1); `Docs/AWDL_STAGE1_IMPL_SPEC.md:1278` | Tras T4: `/usr/bin/log show … \| grep -cE 'No AWDL interface found'` → 1 (sin re-creación) y buscar los literales del registro de eventos según `AWDL_WIFIP2PD_P2P_EVENTS.md` (literal exacto `?`) |
| H12 | T1.1 (GUION:36-41) | Menor | Solo comprueba la propiedad; no la línea del anillo ni la rama cerrada que detiene el arranque | `VersionGate.h:81` (propiedad), `:108` (`gate <veredicto>: <razón>`); `GateLogic.h:69,72-73` (`open-validated` / `closed-structure` / `closed-unknown-build`); `RTLAirLayer.cpp:1397` (`version gate closed, staying out (en2 is unaffected)`); `KnownBuilds.inc:16` (25G241 validado). Medido en el sistema vivo: `RTLAirGate = "open-validated: build 25G241 validated, structure matches"` | Añadir `ioreg -r -c RTL8814Native -d 1 -l \| grep -E 'RTLAirGate\|gate '` y la regla: si el veredicto no es `open-validated`, parar y rollback `inert` |
| H13 | P0 (GUION:15-25) | Menor | Faltan P0.1, P0.2 y la parte del P0.4 sin `__cxa_pure_virtual`; el recuento nm 93 es correcto (medido: 93) y la cita a CODE_FIXES §5 es correcta | `Docs/AWDL_STAGE1_IMPL_SPEC.md:1203-1211`; `Tools/re/check_awdl_symbols.sh` y `Tools/re/check_awdl_header.py` existen (medido); `Docs/AWDL_STAGE1_CODE_FIXES.md` §5 (receta de los 93) | Añadir: `Tools/re/check_awdl_symbols.sh` → 11/11 tipo `0f`; `Tools/re/check_awdl_header.py` → sin discrepancias; `nm Build/obj_airlayer/AWDLInterface.o \| grep -c __cxa_pure_virtual` → 0. P0.3 (orden de vtable) quedó cerrado estáticamente (commit `dd81ec8`); citarlo en lugar de repetirlo |
| H14 | T2/T3 triage de fallo (GUION:68-69) | Menor | Solo lista 2 de las cadenas de fallo del nacimiento y omite las 2 del handler del 94 | Patrones §4 (diez literales + grep combinado, `Docs/AWDL_STAGE1_TEST_LOG_PATTERNS.md:106-110`); auditoría §5 (`awdl: alloc failed` en `RTLAirLayer.cpp:1795`, `awdl: init failed` en `:1799`, distinto de `awdl: base init failed`) | Sustituir por el grep combinado del doc de patrones §4 más `awdl: (alloc failed\|init failed\|invalid role\|created)` |
| H15 | T7 (GUION:97-103) | Menor | `kextunload`/`kextload` sobre un kext del KC de arranque (OpenCore) puede ser rechazado; falta huella de descarga y comprobación de estado; la expectativa «sin pánico» convive con defectos revisados del hook | Identificador correcto: `Source/AirLayer/Info.plist:10`; sin librerías no-Apple (`Info.plist:54-72`). `?` comportamiento de kextunload con kexts del KC de arranque. Defectos conocidos: `Docs/AWDL_STAGE1_CODE_REVIEW_HOOK.md`. Huellas: `RTLAirLayer.cpp:1830` (`awdl: teardown`), `AWDLInterface.cpp:319`/`:327` (`enable`/`disable`) | Antes: `kextstat \| grep -i rtl` (kextstat existe en 26.7.1 y delega en `kmutil showloaded`, medido). Tras el unload: buscar `awdl: teardown` en el anillo. Si `kextunload` rechaza, usar reinicio como ciclo; para la carga, si `sudo kextload <bundle>` responde «already loaded», probar `sudo kmutil load -p <bundle>` (kmutil presente, medido) |
| H16 | Si algo falla (GUION:107-108) | Menor | `log show --last 5m > fallo.log` no contiene las líneas del kext (canal kernel no existe, H2) y no recoge el estado de carga | Deriva de H2 | Añadir: `ioreg -r -c RTL8814Native -d 1 -l > ~/Desktop/fallo_ioreg.log`; `ioreg -r -c RTLAwdInterface -l > ~/Desktop/fallo_awdl.log` si nació; `kextstat \| grep -i rtl`. El resto (informe de pánico, rollback `inert`, `Backups/efi_phase5_*`) es correcto: `Tools/phase5_deploy.sh:9,38-44,106-108` |
| H17 | T4 (GUION:83-87) | Menor | «Sin `Timed out`» y «una sola creación» sin comando de recuento; el `%s` de `Timed out while waiting for interface` interpola el rol (`AWDL`), no `awdl0`, así que grep por la parte constante | Patrones §5 T4.2 y T4.4 | `/usr/bin/log show … \| grep -cE 'Timed out while waiting for interface'` → 0 y `grep -cE 'No AWDL interface found'` → 1 |

## 2. Pasos correctos

- **P0, build** (GUION:18): `./build_airlayer.sh` produce el objeto y el kext; el `nm -u … | grep -c`
  con 93 es correcto (medido) y la cita a `AWDL_STAGE1_CODE_FIXES.md` §5, verba.
- **Despliegue** (GUION:30): env `RTL8814_LAYER_SRC`/`RTL8814_LAYER_MD5` y el par `layer 2` casan con
  el script (`Tools/phase5_deploy.sh:28-29`, `:83`: `rtlair=2` = join + data); el md5 se calcula al
  vuelo, así que no queda hash pegado. La cautela del montaje interno sobra pero no daña: el script
  lo exige (`:33-36`), y la copia de seguridad es automática (`:38-44`).
- **T1.1** (GUION:39-41): `ioreg -l | grep -i RTLAirGate` es válido y el esperado
  `open-validated` es el correcto para 25G241 (medido en el sistema vivo; KnownBuilds.inc:16).
- **T1.2, fondo** (GUION:49): «fallo de creación sin espera de 30 s» es coherente con los patrones
  §2 (T1.2) y con el 102 silencioso del handler con `rtlawdl=0` (`RTLAirLayer.cpp:586-587`); no hay
  que esperar literal del kernel en ese camino.
- **T2/T3, ioreg e ifconfig** (GUION:56-60): `ioreg -r -c RTLAwdInterface -l` usa el nombre de clase
  correcto (`AWDLInterface.cpp:94`) y con `-l`. MTU 1428 y medios 802.11 son los del código
  (`AWDLInterface.cpp:198` escribe `0x594` dos veces; `:192-193` media/`ifm_current` 0x80).
- **T3.5, sintaxis** (GUION:74): `sudo ifconfig awdl0 up; ping6 -c 3 -I awdl0 ff02::1` es correcta
  (ping6 acepta `-I interface`, medido) y la justificación no-op está en el código
  (`AWDLInterface.cpp:255-269`; colas habilitadas en `:317-331`).
- **T4, literales** (GUION:83-87): `Found new interface awdl0 with role AirLink`, sin `Ignoring
  awdl0` y sin `llw0` son los esperados correctos (patrones §5; T6.2 del spec).
- **T5.1** (GUION:90-93): correcto como regresión —el delta §6 baja T5 a regresión—; solo reservar
  que el rendering exacto de `ifconfig -v` para el media 0x80 está sin verificar (`?`).
- **T7, identificador y rollback** (GUION:100, 108): `com.DrogaBox.driver.RTL8814AirLayer` es el
  bundle id real (`Info.plist:10`); el rollback `inert` y los `Backups/efi_phase5_*` existen en el
  script; guardar el informe de `/Library/Logs/DiagnosticReports/` es lo debido dado el riesgo
  declarado de T3.5.

## 3. Cobertura frente al spec §14 (lo que el guion no cubre)

- **T2.1** (capacidad, byte `+0x08` bit 0): sin sonda no hay comando de usuario para el `RawGet` 12;
  `Tools/test_uc.c` solo habla con el user client del driver nativo (telemetría/scan/join). Cubrir
  «por efecto»: el daemon solo emite el 94 si el bit está publicado.
- **T2.2, retorno < 1 s**: es la prueba del `registerService` síncrono (delta §6, riesgo #90 del
  spec §15); el anillo no marca tiempos. Aproximar con los timestamps del `log show` del daemon y
  anotarlo como `?`.
- **T2.3** (duplicado → 17) y **T2.4** (rol 7 → 102 + `awdl: invalid role 7`,
  `RTLAirLayer.cpp:583`): requieren emitir el 94 manualmente; sin sonda en el repo no son
  ejecutables. Anotar como no cubiertas o dejarlas para una sonda propia.
- **T3.3** (`if_nametoindex` > 0): implícito en `ifconfig awdl0`; opcional `netstat -I awdl0 -n`.
- **T5.2/T5.3**: sin comando de usuario para el 97 (el spec lo marca `?`); el kext tampoco loguea
  en ese camino (auditoría §3, NO EMITE).
- **T6.2** (`ifconfig llw0` no existe): cubierto de refilón por «sin `llw0`» en T4; conviene paso
  explícito tras los 60 s de H10.
- **T7.1**: ver H11.

## 4. Nota final

Este documento es el único fichero modificado; el guion revisado y el resto del árbol quedan intactos.
