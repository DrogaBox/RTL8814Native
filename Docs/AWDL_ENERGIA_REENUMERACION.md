# Energía, re-enumeración del dongle y reinicio

Tres defectos de la capa (`RTL8814AirLayer.kext`) que aparecen cuando el T9UH desaparece y vuelve
(cada suspensión en esta placa, o un desenchufe) o cuando aparece tarde en el arranque, más el orden
de apagado del kernel que los explica. Medido el 2026-11-16 en macOS 26.7.3 (25G342), driver
`CFE7BB9D`; el desensamblado de `airportd` es de 25G342 y el del kernelcache de 25G332 (mismo xnu;
los offsets de IOSkywalkFamily coinciden con las trazas de 25G342). La copia inglesa es
[en/AWDL_ENERGIA_REENUMERACION.md](en/AWDL_ENERGIA_REENUMERACION.md).

## 1. Contexto

- En S3 la placa corta la alimentación del puerto USB: al despertar el T9UH se re-enumera y nace un
  `RTL8814Native` nuevo. El `RTLAirNub` persistente (anclado también a `IOResources`) conserva el
  controlador, `en2`, `awdl0` y `llw0`; `RTLAirController::radioGone` suelta la radio vieja y
  `radioBack` adopta la nueva (`HANDOFF_AWDL.md`, «Sleep/wake with the AirLayer»).
- `en2`, `awdl0` y `llw0` son hijos de energía de la radio (`RTL8814Native`) en el plano IOPower.
  Cuando la radio vieja termina, su `PMstop` los suelta.
- `IOSkywalkNetworkBSDClient` (hijo de energía de cada interfaz) atiende los ioctl BSD.

## 2. Defecto A: el auto-join falla con «driver not available»

**Síntoma.** En un arranque en frío el auto-join de `airportd` aborta con
`error=(37 'driver not available')` en todos los disparadores (`iff_up`, `dark_wake`, `user_wake`,
`screen_off`) durante toda la sesión; unirse a mano desde el menú (`WiFiAgent`, `ASSOC`) sí
funciona. El código 37 es genérico en CoreWiFi (también «Already associated…», «AJ cancelled…»);
solo el texto «driver not available» es el defecto, y ese texto vive en `/usr/libexec/airportd`.

**Mecanismo (IDA, `airportd` x86_64 de 25G342).**

| Función | Dirección | Qué hace |
|---|---|---|
| `-[CWXPCInterfaceContext __allowAutoJoinWithTrigger:reply:]` | 0x1000b6df8 | Devuelve `NSPOSIXErrorDomain` 37 «driver not available» si el nodo de la interfaz tiene el byte +196 a 0 |
| `deviceNodeForName` | 0x10007aadb | Recorre la lista de nodos (`qword_100176190`) por nombre |
| `_initInterface` | 0x10007b3ef | Crea el nodo y pone +196 = (cuarto argumento != 0) |
| `_initAttachedAirPortInterfaces` | — | Llama a `_initInterface` con 1: interfaces presentes al arrancar `airportd` |
| `_initInterface_block_invoke_3` | — | Camino del `KEV_DL_IF_ATTACHED` tardío: pasa un byte capturado (0 en la práctica) |
| `_bsd_80211_event_callback` | 0x1000852e0 | Único otro escritor de +196: el evento 55 (`APPLE80211_M_DRIVER_AVAILABLE`), `available` = u32 en +8; en 0→1 registra «Driver Availability : available. Begin autojoin» y llama a `processDriverAvailabilityForAutojoin` |

**Medición.** Cinco arranques del día: en el único malo (encendido en frío, 16:08) `en2` se adjuntó
0,95 s después de que `airportd` arrancara; en los cuatro buenos `airportd` encontró `en2` ya
presente. La capa no enviaba nunca el evento 55.

**Formato de Broadcom (kernelcache 25G332).** `AppleBCMWLANCore::powerOn` (llamada en
0xffffff8001680ad5) arma 0xF8 bytes a cero, u32 3 en +0, `available` = 1 en +8, `reason`
0xE0821803 en +16, y llama a `IO80211Controller::postMessage(this, staIface, 0x37, buf, 0xF8, true)`;
también desde `powerOff`, `bootChipImage` y `watchdog`. Coincide con
`struct apple80211_driver_available_data` de las cabeceras públicas (184 B; Broadcom envía 248).

**Arreglo** (`1066272`, capa `7C2E196C`). `RTLAirController::setPOWER` envía el evento 55 con ese
formato al encender. Si el nodo ya estaba marcado, `airportd` lo ignora (solo actúa ante un cambio).
**Verificado** en un arranque en frío con el mismo orden malo (`en2` 0,8 s tarde): «Begin autojoin»,
auto-join `STARTED`, `en2` con IP y ningún «driver not available».

## 3. Defecto B: sin radio, todo ioctl a `en2` se duerme para siempre

**Síntoma.** Desenchufar el T9UH en caliente dejó `configd` colgado: el watchdog de espacio de
usuario lo marcó dos veces (`IPConfigurationAgentQueue`, 60 s sin responder) y el sistema quedó
inutilizable hasta reiniciar.

**Traza** (informe `configd_*.userspace_watchdog_timeout.spin`, marcos del kernel simbolizados con
`atos` sobre `/System/Library/Kernels/kernel`, UUID 28267FD5, base 0xffffff8000200000 + offset):
`ioctl` → `ifioctl` → `ifnet_ioctl` → `IOSkywalkNetworkBSDClient::ioctlHandler` (IOSkywalkFamily
+0x4e0e) → `IOCommandGate::runAction` → `IOWorkLoop::sleepGate`, sin volver. Otros dos procesos
estaban parados en el mismo punto.

**Mecanismo (IDA).** `ioctlHandler` duerme en la compuerta mientras el cliente BSD está apagado
(dword en +316 == 0) y el comando exige energía, o mientras otro comando tiene el indicador de
ocupado (+320); `setPowerStateHandler` lo despierta. La tabla `CommandPowerTable` marca
`SIOCSIFFLAGS`, `SIOCADDMULTI` y `SIOCDELMULTI` como comandos con energía; el resto, sin ella cuando
el bit 2 de la configuración está puesto, y con energía en otro caso. Con la radio fuera, `en2` y
`awdl0` quedan sin padre de energía y apagados.

**Arreglo** (`a39e329`, capa `947F9977`). `radioGone` programa un `thread_call` de 5 s; si para
entonces no hay radio (una re-enumeración por suspensión tarda alrededor de 1 s), el root domain
pasa a ser un segundo padre de energía de `en2`, `awdl0` y `llw0` (`anchorPower`), que siguen
encendidos con el sistema despierto y el controlador contesta sin radio. `radioBack` cancela un
temporizador pendiente; el ancla, una vez puesta, queda. **Sin probar** todavía con un desenchufe
real.

## 4. Defecto C: el reinicio se cuelga después de una re-enumeración

**Síntoma.** Reiniciar después de una suspensión, o después de que el dongle se reconectara solo
(19:07:51, «hardware connection lost»), dejaba la máquina colgada en la parte final del apagado,
que no queda en el registro unificado (`logd` ya salió). Los informes `shutdown_stall` existen en
todo apagado desde el 10-11 y no muestran nada de esto: cubren la parte de espacio de usuario.

**Pruebas y lo que descartó cada una.**

| Prueba | Resultado | Conclusión |
|---|---|---|
| Arranque en frío sin el T9UH, reiniciar | Bien | El cuelgue es del driver o de la capa |
| `rtlawdl_llw=0` (sin `llw0`), reiniciar | Bien | Interviene `llw0` |
| `llw0` presente; bajar `llw0`, quitar su IPv6, `SIOCPROTODETACH`/`SIOCPROTODETACH_IN6` a mano; reiniciar | Bien | No discrimina: no hubo re-enumeración en esa sesión |
| `llw0` presente, nada a mano, sin re-enumeración; reiniciar | Bien | `llw0` solo no basta |
| Sesión colgada de las 19:17 | Re-enumeración a las 19:07:51 | El disparador es la re-enumeración |
| Suspender y despertar con la capa vieja; `ioreg -p IOPower` | `en2` y `awdl0` bajo el `RTL8814Native` nuevo; `RTLLlwInterface` fuera, `CurrentPowerState` 0 (deseado 1), su cliente BSD también en 0 | Causa confirmada |

**Mecanismo.** `radioBack` y `anchorPower` volvían a unir al árbol solo `en2` y `awdl0`. Tras la
re-enumeración `llw0` quedaba sin padre y apagado. En el reinicio, `reboot_kernel` (25G332,
0xffffff80008c0ea0) ejecuta, en orden: `IOSystemShutdownNotification(0)` (que entrega
`systemWillShutdown(kIOMessageSystemPagingOff)` al plano de energía vía
`IOPMrootDomain::handlePlatformHaltRestart(6)`), `proc_shutdown`, desmontaje, `if_down_all`
(0xffffff80005def10: `if_down` y desacople de IPv4 e IPv6 de cada `ifnet`, luego `dlil_if_free`) y
`PEHaltRestart`. El desacople de IPv6 de `llw0` llega a `ioctlHandler`, que espera una energía que
no vuelve: la misma espera del defecto B, ahora en el hilo del reinicio.

**Arreglo** (`f5deb52`, capa `4997D361`). `RTLAwdInterface::companion()` expone `llw0`, y
`radioBack` y `anchorPower` lo unen también al nuevo padre. **Verificado**: tras una suspensión
(re-enumeración 20:21:44) `llw0` y su cliente BSD están bajo el `RTL8814Native` nuevo con
`CurrentPowerState` 1, el Wi-Fi vuelve solo y el reinicio siguiente terminó bien.

**Intento descartado.** `4185ed6` desacoplaba los protocolos de `llw0` desde
`RTLLlwInterface::systemWillShutdown` con `kIOMessageSystemPagingOff`. Se basaba en las pruebas a
mano, que no discriminaban, y con `llw0` apagado el desacople de IPv6 esperaría lo mismo. Revertido
en `3a3bb88`.

## 5. Otros hallazgos del mismo día (esta máquina)

- **`PowerTimeoutKernelPanic` de OpenCore y `pf`.** En 25G332/25G342 el parche en línea de
  `IOService::ackTimerTick` no encaja (0xffffff8000ab0250, tamaño 0x6b4) y el respaldo cambia el
  primer `"com.apple\0"` del kernel por `"not.apple"`. Esa cadena (0xffffff8000cd8f00) también la usa
  la comprobación de anclas de `pf` (`sub_FFFFFF80006CC910`, `pf_anchors`/`pf_is_enabled`): con el
  quirk activo `net.filter_state` = `pf` y `net.link.generic.system.enable_netagent` = 0, y
  Network.framework sale del camino Skywalk. Con el quirk apagado, `enable_netagent` = 1 y
  `filter_state` vacío (medido). Efecto sobre la velocidad de AirDrop sin medir (requiere un iPhone).
- **Pánico `now >= *pkt_enq_time` (fq_codel) en la red cableada.** Coincide con desfase del TSC
  entre núcleos: con ForgedInvariant activo se midieron 120–350 ns y un núcleo adelantado distinto
  en cada arranque (también con `-FIPeriodic`); con `-FIOff`, 0 lecturas hacia atrás, también tras
  una suspensión. `-FIOff` se había quitado por error; repuesto, desfase 0 ns medido. No involucra
  a los kexts RTL8814.
- **Despertar inmediato de S3.** Lo provocaba un controlador RGB USB en la controladora XHCI del
  chipset; desconectado, la máquina queda dormida.

## 6. Builds y commits

| Capa | Commit | Contenido | Estado |
|---|---|---|---|
| `7C2E196C` | `1066272` | Evento 55 en `setPOWER` | Verificado |
| `947F9977` | `a39e329` | Ancla al root domain si la radio no vuelve en 5 s | Instalado; desenchufe sin probar |
| `2D9A9F4E` | `4185ed6` | Desacople de protocolos de `llw0` al apagar | Revertido (`3a3bb88`) |
| `4997D361` | `f5deb52` | `llw0` vuelve al árbol de energía en `radioBack` y en el ancla | Verificado (suspensión + reinicio) |

## 7. Abiertos

- Desenchufe en caliente con `4997D361`: `configd` no debería colgarse y, tras 5 s, la capa debería
  registrar «interfaces kept powered by the root domain».
- `llw0` creado después de que se pusiera el ancla queda bajo la radio que exista en ese momento:
  no comprobado sin radio.
- Las direcciones de esta nota son de 25G342 (`airportd`, kernel) y 25G332 (kernelcache); las de
  14.x y 15.x no se miraron.
