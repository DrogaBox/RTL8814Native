# AWDL: estado alcanzado y bloqueo por falta de un par Apple

Punto de situación a 2026-10-30. El trabajo de AWDL en `RTL8814AirLayer.kext` llegó hasta donde se puede
llegar sin un segundo dispositivo que hable AWDL. Lo que sigue necesita un par real y el proyecto no
dispone de ninguno. Este documento fija qué está hecho, qué queda detenido, por qué no hay sustituto y
qué pruebas se ejecutan en cuanto haya un dispositivo Apple disponible.

Equipo de desarrollo: macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), `RTL8814Native.kext` 4.34,
`RTL8814AirLayer.kext` 0.32.0, TP-Link T9UH (RTL8814AU), `rtlair=2 rtlawdl=1`. Lo no visto va con `?`.

## 1. Estado alcanzado

Validado en hardware (detalle en [AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)):

| Hito | Resultado |
|---|---|
| `awdl0` creada y publicada por la capa (etapa 1) | Interfaz presente; `wifip2pd` la encuentra y no recrea |
| `IO80211AWDLPeerManager::configure` (etapa 3) | Termina: `configure done!`, `_enabled = TRUE` |
| `interfaceStateChange` con el ioctl 116 | Pasa; ya no queda `_stateFlags = 0x20` |
| `enableAWDL` (155, 129, 117) | `AWDL ON:[infra(149) ...]`; la petición 120 devuelve 0 |
| Búsquedas de servicios en `wifip2pd` | Registra `_airplay-p2p._tcp`, `_raop._tcp` y `_airplay._tcp` |
| Elección en solitario | `runElection` con `peers:0` y MAC propia como maestro, una vez por segundo |
| Estabilidad | Sin pánicos ni cuelgues durante el arranque y las pruebas |

Los dieciocho setters de la superficie AWDL responden éxito sin tocar la radio (macro `AWDL_ACK` en
`Source/AirLayer/AWDLInterface.{h,cpp}`). Commits: `42553ba` (reconocimiento de setters) y `39185f9`
(nota de resultados). Kext desplegado: md5 `647f1ab25c3aa2c32a252b2e60371d8b`.

## 2. Qué queda detenido

Todo lo que sigue exige un par AWDL en el aire para poder verificarse; sin él no hay forma de saber si
una implementación es correcta.

| Pendiente | Por qué necesita un par | Documento |
|---|---|---|
| Eventos 41/42/43 (inicio de ventana de disponibilidad, inicio y fin de extensión) | Emitirlos sin radio que respalde las ventanas hace que `inAvailabilityWindow()` devuelva cierto y el manager empiece a tirar de TX; la cadencia real de `AwNumber` y `FwAwStartTime` es `?` | [AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md) §4, [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2 |
| TX real de action frames y de datos por el USB (etapas 2, 2a, 2b) | Sin receptor no hay manera de comprobar que la trama sale con el formato y el canal correctos; `Tools/awdl_stage2_output_hook.patch` no se aplicó | [AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md), [AWDL_STAGE2B_MBUF_TO_PACKET.md](AWDL_STAGE2B_MBUF_TO_PACKET.md) |
| RX de action frames de pares y entrega a `actionFrameInput` | No hay tramas AWDL que recibir; el filtro de recepción del chip se confirma con tráfico real | [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §6 |
| Elección, PSF y MIF contra un par | Con `peers:0` el manager nunca sale de la elección en solitario | [AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md) |
| Temporización de las ventanas con el RTL8814AU | Precisión de timers de host frente a la cadencia AWDL: solo se mide con un par sincronizado | [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) §7 |
| Qué getters consulta `wifip2pd` al abrir AirDrop | Punto 12 del [índice](AWDL_INDICE.md) §8.2; lo cierra un par real | [AWDL_STAGE1_STUB_VALUES.md](AWDL_STAGE1_STUB_VALUES.md) |

Sin bloqueo por par pero abiertos (fallos no fatales que el manager registra y sigue): lectura 341
(`awdl_cap`), `getAWDL_SYNC_ENABLED`, `setAWDL_OOB_AUTO_REQUEST`, 414 (`P2P_COEX_PROTECTION`), métrica de
elección 65, capacidades HT/VHT y los tipos 293 y 575 de `wifip2pd`. Son limpieza de bajo valor mientras
no haya ventanas.

## 3. Por qué no hay sustituto

- **No hay dispositivos Apple en el proyecto.** AWDL es un protocolo propietario de Apple; sin un iPhone,
  iPad o Mac no hay contraparte.
- **El teléfono Android disponible (A53, SCSC S620) no sirve de par.** No hay soporte AWDL conocido en
  Android, y el chip probablemente no ofrece modo monitor para capturar; Wi-Fi Aware/NAN es un protocolo
  distinto y solo quedó como pista de otro trabajo.
- **AirDrop añade una dependencia más.** El descubrimiento de AirDrop pasa por Bluetooth y este equipo no
  tiene Bluetooth operativo (el Finder muestra «Bluetooth: desactivado» y la visibilidad en «nadie»).
  Sin Bluetooth, la petición 120 sale igual (se vio en las pruebas), pero la ventana de AirDrop no lista
  pares. Otros servicios que usan AWDL (AirPlay entre pares) quizá no dependan de él `?`.
- **No se valida en vacío.** Un emisor de eventos incoherente deja al par «no sincronizado» (desviación
  ≥ 6554 µs módulo 2^20 µs); sin un par real, ni la cadencia ni la desviación son medibles.

## 4. Qué se necesita y qué se hace al disponer de ello

Dispositivo: cualquier iPhone, iPad o Mac con AWDL activo (AirDrop, AirPlay entre pares, Sidecar). Un Mac
es el par más cómodo: permite observar su propia `awdl0` con `tcpdump -i awdl0` (previsto, no probado
`?`). Para AirDrop completo, un adaptador Bluetooth funcional en el equipo de desarrollo.

Orden previsto de pruebas con el par presente:

1. **RX primero.** Poner el chip en el canal social (6 o 149) y comprobar que llegan action frames del par
   a `RxCompletionQueue` (E2.4 de [AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md)). Es la prueba de menor
   riesgo y confirma el valor vivo de `RXFLTMAP0` (punto 20 del índice).
2. **TX de action frames y de NDP.** Aplicar el gancho de salida de la etapa 2 y verificar en el par que
   las tramas salen (E2.3). Cierra la confirmación de TX por C2H o `CONFIG_XMIT_ACK`.
3. **Eventos 41/42/43.** Emitirlos con el temporizador de ventanas una vez que RX y TX funcionen, y medir
   `AwNumber`/`FwAwStartTime` contra el par.
4. **Elección y PSF/MIF.** Comprobar que el manager abandona `peers:0`, que `PEER_PRESENCE` (47) aparece y
   que `wifip2pd` registra pares.
5. **AirDrop y AirPlay.** Con Bluetooth disponible, probar el descubrimiento y una transferencia.

Los parches y el procedimiento de despliegue siguen vigentes: `Tools/awdl_stage3_configure.patch`,
`Tools/awdl_stage3b_sync.patch` y `Tools/awdl_stage3c_acks.patch`, desplegados con
`RTL8814_AWDL=1 Tools/phase5_deploy.sh <EFI> layer 2`.

## 5. Trabajo de WiFi que no depende de AWDL

Mientras no haya par, el resto del driver sigue siendo trabajo útil y verificable con el equipo y el
router actuales (ver `Docs/ROADMAP.md`): WPA3-SAE (pasos 4-5: 4-way con KDF-SHA256 y AES-CMAC,
protección BIP/PMF; probable con un hotspot solo-SAE), retransmisiones de subida (≈36 % de reintentos en
el AP; instrumentar ACK/BlockAck), roaming en 2,4 GHz y soporte de macOS 14/15. Sleep/wake ya se probó y
no figura como pendiente.
