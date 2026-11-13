# AWDL: proyectos externos y lo que aportan

Revisión a 2026-10-30 de las implementaciones abiertas de AWDL publicadas fuera de Apple, para saber
qué código o información sirve al trabajo de `RTL8814AirLayer.kext`. La pregunta concreta es si alguna
levanta el bloqueo descrito en [AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md) (falta de un par
AWDL) y si sus datos confirman o corrigen lo documentado a partir del kernelcache. Lo no comprobado va
con `?`.

## 1. Fuentes revisadas

| Proyecto | Revisión | Licencia | Qué es |
|---|---|---|---|
| `t4t5/owly` | `9a676e1` (2026-10-26) | MIT | AWDL y AirDrop en software para Linux sobre tarjetas no Broadcom (Intel, MediaTek, Realtek). Especificación en `docs/SPEC.md`, capturas anonimizadas de iOS 26 en `tests/captures/` |
| `s-iwaki-d/ESP32Drop` | `92bdc12` (2026-09-07) | tipo ISC (permisiva) | AWDL y AirDrop en un ESP32-S3, solo canal 6; probado contra Mac y iPhone |
| `raph559/winawdl` | `2196fb4` (2026-10-13) | MIT; `radio` GPL-2.0, `awdl` GPL-3.0 | AWDL y AirDrop nativos en Windows con la RTL8852CE en modo monitor; `DESIGN-NOTES.md` con medidas |
| `netmojo-systems/omdrop-awdl` | `46ed6e1` (2026-10-30) | GPL-2.0 | Dieciséis parches a `brcmfmac` que exponen el AWDL del firmware Broadcom de los Mac Apple Silicon bajo Linux |
| `seemoo-lab/owl` | `0ee3263` (2026-10-12) | GPL-3.0 | Implementación AWDL de referencia en C (OWL) |
| `seemoo-lab/opendrop` | `11fe7ba` (2026-08-17) | GPL-3.0 | Capa AirDrop en Python sobre OWL |
| Project Zero, «Over The Air Vol. 2, Pt. 2» (2017) | — | — | Explotación del firmware Broadcom de un iPhone 7 con iOS 10.2 |

Copias locales en `~/re_cache/work/awdl_refs/`.

## 2. Diferencia de arquitectura

Todas las implementaciones anteriores ejecutan el protocolo AWDL completo (sincronización, elección,
secuencia de canales, TLVs, encapsulado de datos) en su propio código. En macOS ese papel lo cumple
`IO80211AWDLPeerManager` dentro de `IO80211Family`
([AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md)); la capa solo aporta radio, eventos de
ventana y entrega de tramas ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md)). Por eso
ningún bloque de protocolo de estos proyectos entra en el kext. Lo reutilizable es:

- un par AWDL que no es un dispositivo Apple (§3);
- tramas reales de iOS 26 para contrastar el formato documentado (§4);
- las soluciones de radio que tuvieron que encontrar sobre hardware no Broadcom, en particular Realtek
  (§5 a §7).

## 3. Par AWDL sin dispositivo Apple

| Opción | Hardware necesario | Banda | Encaje en este proyecto |
|---|---|---|---|
| ESP32Drop | ESP32-S3 con PSRAM (probado en M5Stack StopWatch, núcleo `m5stack:esp32` 3.3.8) | 2,4 GHz, canal 6 fijo | Directo: no necesita otra máquina ni otra tarjeta. AirDrop probado contra un Mac en ambos sentidos (envío a Mac de PNG/JPEG, también de más de 128 KiB) |
| owly | Una segunda máquina Linux con una tarjeta de su lista (p. ej. MT7612U USB, RTL8852BU USB) | 2,4 y 5 GHz según tarjeta | Requiere un segundo equipo; el PC de desarrollo arranca macOS o CachyOS, no ambos, y el T9UH es su única tarjeta |
| winawdl | PC con RTL8852CE y Windows 11 | 2,4 y 5 GHz | No aplicable: tarjeta concreta que el proyecto no tiene |

El ESP32-S3 permanece en el canal 6 y no ejecuta una elección completa: nunca se elige a sí mismo,
se anuncia como hoja con métrica baja (anunciarse como raíz hacía que iOS lo tratase como fuera de la
malla) y sigue al maestro que oye. Sin maestro no transmite: `send_mif` se niega mientras no haya
resultado de elección (`ESP32Drop/src/awdl/core/awdl_tick.h:55`, `awdl_elect.h`, `awdl_window.h:188`).
Además ajusta su reloj a partir de los PSF del maestro (`awdl_gauge.h`, `awdl_port_esp32.cpp`).

Por tanto, frente a un Mac con este driver el ESP32 no sirve para «RX primero». El orden se invierte:

1. El `IO80211AWDLPeerManager` del Mac ya se elige a sí mismo maestro con `peers:0`
   ([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)). Si la capa transmite sus
   PSF/MIF en el canal 6 con tiempos coherentes, el ESP32 los oye, elige al Mac como maestro y empieza a
   emitir MIF.
2. Esa reacción, visible en el puerto serie del ESP32 (`awdl_status.h`, `awdl_diag.h`), valida la TX del
   RTL8814AU sin otro receptor.
3. Solo entonces hay tramas del ESP32 que recibir y la prueba de RX tiene contenido.

Para disponer de una fuente de tramas sin TX propia, la alternativa es una compilación modificada del
ESP32 que se anuncie como maestro cuando no oye a nadie. Cómo reaccionaría el peer manager de macOS a
ese maestro es `?`; iOS lo rechazó según los comentarios de ESP32Drop.

Otras limitaciones: solo canal 6, envío y recepción de AirDrop en compilaciones distintas, y el
descubrimiento de envío usa BLE, que el equipo de desarrollo no tiene operativo. macOS rota su MAC AWDL
cada 100 s aproximadamente (`ESP32AWDL.h`). Que el `IO80211AWDLPeerManager` acepte a un par que solo
atiende el canal 6 es `?` hasta probarlo.

## 4. Contraste con las capturas de iOS 26

`owly/tests/captures/` trae tres sesiones en el canal 44 (descubrimiento, recepción y envío entre un
iPhone con iOS 26 y un nodo owly con `mt7925e`). Las direcciones MAC están sustituidas por
`02:00:00:00:00:0N`, los nombres por `x` y los contenidos desconocidos a cero; las longitudes y los
tiempos se conservan. Un lector propio (cabecera radiotap, cabecera 802.11, cabecera fija de 16 B y
recorrido de TLVs) dio sobre las tres capturas:

| Medida | Valor | Tramas |
|---|---|---|
| Action frames AWDL | 1968 PSF (subtipo 0) y 2925 MIF (subtipo 3) | 4893 |
| TLV 21 (versión) del iPhone | `0xA0`, clase de dispositivo 2 (iOS) | 1462 |
| TLV 21 del nodo owly | `0x34`, clase 1 | 3431 |
| Orden de TLVs, PSF del iPhone | 4, 5, 18, 6, 24, 12, 7, 17, 21 | 705 |
| Orden de TLVs, MIF del iPhone | 4, 5, 18, 24, 12, 7, 17, 16, 21, 6 y varios tipo 2 | 757 |
| Bytes sobrantes tras el último TLV | 0 en todas | 4893 |
| Cabecera de datos tras LLC/SNAP `AA AA 03 00 17 F2 08 00` | `03 04`, secuencia, `00 00`, ethertype `86 DD` | 4424 |

El tipo 17 aparece en todas las tramas del iPhone y no en las de owly; en el kernel lo analiza
`parseAwdlIEEE80211CntnrTLV` ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md)), y según
`winawdl/DESIGN-NOTES.md` es donde el iPhone anuncia sus capacidades VHT. Las capturas no contienen
MIFs con relleno de ceros ni A-MSDU; winawdl sí observó MIFs del iPhone terminados en 8 bytes a cero en
el canal 44. El parser del kernel lo acepta: los ceros pasan como TLVs de tipo 0 y longitud 0 y van al
caso por defecto ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) §4).

### 4.1 Campos del TLV 4 (sync params)

La especificación de owly nombra todos los campos del cuerpo; las capturas confirman sus valores. Con
la convención de [AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) §1.2 (desplazamiento desde el inicio del TLV,
cuerpo en `+3`) la correspondencia es:

| Offset TLV | Nombre en `AWDL_TLV_BODIES.md` | Campo según owly | Valor en las 4893 tramas |
|---:|---|---|---|
| `+3` | `tchan` | canal de la próxima AW | — |
| `+4` | `dc` (duty cycle / duración) | contador descendente en TU hasta el límite de la siguiente franja | siempre ≤ 64 |
| `+6` | `mchan` | canal del maestro | — |
| `+7` | (no listado) | tiempo de guarda | 0 |
| `+8` | campo de sincronía 1 | periodo de AW en TU | 16 |
| `+10` | campo de sincronía 2 | periodo entre action frames en TU | 110 (iPhone también 440 en 133 MIF) |
| `+12` | (no listado) | flags | `0x1800` |
| `+14` | `aw_ext_length` | longitud de AW extendida | 16 |
| `+16` | campo de ventana 1 | longitud de AW común | 16 |
| `+18` | `rem` | TU restantes de la AW actual | — |
| `+20..+23` | `ext byte 1..4` | extensión mínima y máximas de multicast, unicast y action frames | `03 03 03 03` |
| `+24`, `+28` | dword y word «de secuencia» | dirección MAC del maestro (6 B) | siempre una de las MAC sustituidas |
| `+30` | `pm` | modo de presencia (AW por franja) | 4 |
| `+32` | `sn` | número de secuencia de la próxima AW | — |

[AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) §1.2 ya incorpora estos nombres. Las filas `+24`/`+28` corrigen su lectura anterior: en el aire esos
6 bytes son la dirección del maestro. Que el kernel los copie a peer `+600`/`+604` como dirección y no
como secuencia es coherente con ello, pero el uso posterior de esos ivars no se ha revisado `?`. Las
filas `+4`, `+8`, `+10` y `+16` sustituyen los nombres genéricos por los de la especificación; los
valores medidos (16, 110, 16) encajan con un periodo de AW de 16 TU.

### 4.2 Secuencia de canales del iPhone (TLV 18)

En las 1462 tramas del iPhone (versión `0xA0`, clase 2), la cabecera del TLV 18 es siempre la misma:
16 franjas (`count-1 = 15`), codificación 3 (canal + clase operativa, 2 B por franja), `dup` 0,
`step` 3 y relleno `0xFFFF`, con 41 B de longitud. Secuencias observadas (canal por franja 0..15):

| Tramas | Secuencia |
|---:|---|
| 817 | `44 0 44 0 0 0 0 0 6 0 44 0 0 0 0 0` |
| 359 | `44 44 44 0 0 0 0 44 6 44 44 44 0 0 0 44` |
| 153 | `44 44 44 0 0 0 0 0 6 44 44 0 0 0 0 0` |
| 133 | `44 44 44 44 44 44 44 44 6 44 44 44 44 44 44 44` |

- El iPhone anuncia canal 0 en las franjas que no atiende, que es el patrón honesto que exige
  [AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) §7.
- El canal 6 solo aparece en la franja 8, una de cada 16.
- El 44, canal de la sesión capturada, ocupa de 3 a 15 franjas según el tráfico.

Para el RTL8814AU:
- con la radio fija en el 44 o el 149, coincide con el iPhone en varias franjas incluso en reposo;
- con la radio en el 6 (el ESP32-S3, o el T9UH en 2,4 GHz), solo coincide en la franja 8.

Las capturas son de un nodo owly en el 44; con otro par en otro canal, el iPhone elige otro canal
dominante `?`.

## 5. Datos en el aire

[AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) describe la cabecera que Broadcom intercambia
con su firmware (`AA AA 03 … 17 8F 08`, contador de 12 bits con el id del anillo), y
[AWDL_PROTOTYPE_DESIGN.md](AWDL_PROTOTYPE_DESIGN.md) la da por no imitable. Esa cabecera es interna al
bus PCIe; en el aire la trama de datos AWDL es (owly AWDL-13, parche 0006 de omdrop-awdl, y las 4424
tramas medidas en §4):

```
802.11 data (QoS opcional) | AA AA 03 | 00 17 F2 | 08 00 | 03 04 | seq (u16) | 00 00 | ethertype (BE) | IPv6
```

con BSSID `00:25:00:FF:94:73` en todas las tramas AWDL (owly AWDL-1) y secuencia propia por nodo,
incrementada en cada trama. Sin firmware AWDL, la capa o el driver tiene que construir esta
encapsulación al pasar de la cola TX de Skywalk al USB y quitarla en RX antes de `enqueuePackets`. En
qué punto de la etapa 2b ([AWDL_STAGE2B_MBUF_TO_PACKET.md](AWDL_STAGE2B_MBUF_TO_PACKET.md)) llegan los
paquetes ya encapsulados o como Ethernet II es `?`. Los receptores deben aceptar A-MSDU (bit `0x0080`
del control QoS, subtramas alineadas a 4 bytes, owly AWDL-14).

## 6. Radio fija y temporización

owly, winawdl y ESP32Drop no saltan de canal: la radio se queda en uno y el iPhone lo visita en una o
dos franjas de cada 16. Datos publicados:

- Franja = 4 AW de 16 TU = 64 TU; secuencia de 16 franjas (owly `sync.rs`).
- Unicast solo dentro de una AW compartida, con 3 TU de guarda en los bordes; multicast con 16 TU de
  guarda en las franjas 0 y 10, o en cualquier franja servida con 3 TU si la radio es fija (owly
  AWDL-16).
- Un nodo debe anunciar en cada franja solo el canal en el que de verdad estará, y canal 0 en las que
  no atiende; una franja anunciada y no atendida pierde las tramas (owly AWDL-12).
- Reproduciendo las capturas, owly mantiene su calendario a menos de 3 TU del maestro con temporizadores
  del host y marcas de tiempo de llegada, no del chip (owly T-AWDL-3). winawdl hace lo mismo con un
  temporizador de alta resolución de Windows.
- iPhone en reposo: una franja para el canal de su AP, otra para 149, otra para 6 y otra para 44; al
  enviar añade franjas en 44 (winawdl, medida de 2026-10-11). En el canal 6 la transferencia es lenta
  (188 kB/s en winawdl, unos 35 kB/s en el RTL8188FTV de owly).

Para el RTL8814AU esto acota el riesgo de [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) §7: no
hace falta reproducir el salto de canal de Broadcom para interoperar, siempre que la secuencia de canales
que el `IO80211AWDLPeerManager` anuncia coincida con el canal en el que está la radio. Cómo influir en
esa secuencia desde la capa (setter del tipo 18, capacidades anunciadas) es `?`.

## 7. Lecciones de hardware

**winawdl, RTL8852CE** (`DESIGN-NOTES.md`, «Acknowledging frames» y siguientes):

- Si el hardware no envía ACK a la MAC del nodo AWDL, el iPhone reintenta cada trama hasta su límite: un
  85 % de tramas con el bit de reintento y transferencias que se cortan. Con la entrada de direcciones de
  la MAC configurada con la MAC propia, 0-4 %. En el RTL8814AU el equivalente es el segundo puerto
  (`REG_MACID1` 0x0700, `REG_BSSID1` 0x0708) con `RCR_ENMBID`
  ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md)); el T9UH no tiene Bluetooth, así que la
  espera por coexistencia que tuvo que desactivar winawdl no aplica.
- Las capacidades HT anunciadas deben coincidir con el ancho sintonizado: con 40 MHz anunciados y la
  radio en 20 MHz, 837 de 838 tramas llegaron solo como reintento a MCS 0.
- Block Ack: el iPhone pide un acuerdo en TID 0 con ventana 64 una vez por segundo mientras envía;
  aceptarlo pasó de 971 kB/s a 1,7 MB/s y los reintentos de 904 a 47 de unas 20 400 tramas.
- Reintentos de TX: con un solo intento se perdían respuestas TCP; con 7 (lo habitual en una estación)
  no.

**owly, `drivers.toml`**: en radios fijas conviene enviar el unicast sin esperar el ACK del hardware
(`tx_noack`), porque una trama reintentada más allá de la visita del par bloquea a las que van detrás.
Tarjetas Realtek probadas: RTL8822BE, RTL8852AE/BE/CE, RTL8852BU USB y RTL8188FTV USB funcionan;
RTL8821CE no. El RTL8814AU no figura.

**omdrop-awdl, Broadcom**: el firmware BCM4387 crea la interfaz AWDL con `interface_create` (flags
`0x1a`, BSSID explícito, índice 2) y expone los iovars `awdl_peer_op`, `awdl_af_hdr`, `awdl_config` y
`awdl_af_rssi`. El unicast a una MAC sin entrada de par en el firmware se descarta (`FW_TOSSED`); con
`awdl_peer_op ADD` se confirma. Contrastable con [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md): sin
firmware AWDL, el driver tiene que llevar su propia tabla de pares y decidir qué unicast sale. Además, la
primera sesión AWDL tras crear la interfaz no transmite hasta que AWDL se apaga y se enciende (de ahí su
servicio `awdl-warmup`); si el `IO80211AWDLPeerManager` depende de algo parecido es `?`.

## 8. Lo que no aporta

- **OWL y opendrop**: owly y winawdl ya recogen lo útil de OWL (winawdl lo reutiliza tal cual) y
  opendrop es solo la capa AirDrop. Ambos son GPL-3.0, incompatible con enlazarse al código GPL-2.0 de
  Realtek.
- **Project Zero 2017**: trata del firmware BCM4355C0 de iOS 10.2 y menciona AWDL solo de pasada, como
  superficie dirigida por el host. Para el `IO80211AWDLPeerManager` del kernel es más relevante «An
  iOS zero-click radio proximity exploit odyssey» (Project Zero, 2020), no revisado aquí `?`.

## 9. Reutilización y licencias

Ningún fragmento de código de estos proyectos se ha copiado al repositorio. Si se reutilizara alguno
para herramientas de prueba (lector de capturas, réplica de tramas), owly, winawdl (fuera de `radio` y
`awdl`) y ESP32Drop lo permiten con su aviso de licencia.

Las GPL solo imponen condiciones al distribuir. Una compilación aparte para uso propio, que no se
publica ni se entrega a terceros, puede mezclar código GPL-3.0 (OWL, opendrop) con el GPL-2.0 de
Realtek sin incumplirlas; lo que no se puede es publicar ese binario ni el árbol mezclado. El límite
real es técnico, no legal: en macOS el protocolo lo ejecuta `IO80211AWDLPeerManager`, así que un
segundo motor AWDL dentro del kext competiría con él por las mismas tramas y el mismo calendario, y
`omdrop-awdl` son parches a un driver Linux de Broadcom sin equivalente en este hardware. Donde una
compilación privada sí tiene sentido es en herramientas fuera del kext (por ejemplo, un lector de
capturas o un emisor de tramas de prueba).

## 10. Efecto sobre el orden de pruebas

| Paso de [AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md) §4 | Con un ESP32-S3 | Con las capturas de owly |
|---|---|---|
| 1. RX de action frames en el canal social | Solo después del paso 2: el ESP32 calla mientras no tenga maestro (o con una compilación modificada que se anuncie como maestro) | Solo inyección sintética en `actionFrameInput`, sin tiempos reales |
| 2. TX de action frames y NDP | Sí, y va primero: si el ESP32 elige al Mac como maestro, las tramas salen con formato y tiempos válidos | No |
| 3. Eventos 41/42/43 | Parcial: el ESP32 solo se mantiene enganchado si la cadencia de 16 TU del Mac es estable | Valores de referencia de §4.1 |
| 4. Elección y PSF/MIF | Sí: salida de `peers:0` cuando el ESP32 empieza a emitir MIF | No |
| 5. AirDrop | Recepción en el Mac desde el ESP32 `?` (su envío necesita BLE para despertar al receptor) | No |

## 11. Revisión de 2026-11-03

| Proyecto | Revisión | Licencia | Qué es |
|---|---|---|---|
| `Mr-Hafid/Crosswind` | `406272f` (2026-11-02) | GPL-3.0; crates AWDL y AirDrop MIT | Fork renombrado de owly con una app de barra de menús (Tauri) y un crate `crosswind-core` de detección de plataforma |
| `kgp-macPro/AirportItlwm-Tahoe` | `9698dc4` (2026-10-31) | GPL-2.0 | AirportItlwm 1.0.0 para Tahoe sobre la AX210, con `awdl0` creado |

**Crosswind.** `crates/crosswind-awdl/src` es el `owly-awdl` de `9a676e1` con el nombre cambiado: tras
normalizar los nombres, la única diferencia es la longitud del TLV ARPA en un test (8 → 13, por el
nombre de host). Las tres capturas de `tests/captures/` son idénticas byte a byte (mismos SHA-1) a las
de owly, ya contrastadas en §4 y en [AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md). No hay datos de
protocolo nuevos. Lo único nuevo para este trabajo es el informe
`hardware/reports/crosswind-test-rtl8xxxu-1792818572.json`: un Realtek USB `0bda:f179` (RTL8188FU,
driver `rtl8xxxu`, canal 6, modo exclusivo) pasa «phone heard», «shown on the phone» y
«phone -> computer», y falla «computer -> phone». Es otra radio Realtek USB que recibe de un iPhone con
el AWDL en el host; el fallo de envío no trae causa.

**AirportItlwm-Tahoe.** No usa la `IO80211Family` nativa de Tahoe: el destino Tahoe enlaza
`IO80211FamilyLegacy` (`AirportItlwm-Monterey-Info.plist`) y necesita el entorno Ventura restaurado
por los parches de raíz de OCLP-CustoMac (`INSTALL.md`). `awdl0` nace en `setVIRTUAL_IF_CREATE`
(`AirportSTAIOCTL.cpp`): para el rol `APPLE80211_VIF_AWDL` crea un `IO80211P2PInterface`, lo inicia
con prefijo `awdl`, fija el canal de infraestructura a 149/80 MHz, hace `attach(this)` y
`registerService()`. Los `setAWDL_*`/`getAWDL_*` de `AirportVirtualIOCTL.cpp` solo guardan o registran
valores, y `requestPacketTx` tiene comentado el envío para el rol AWDL: no hay sincronización, elección
ni TX AWDL en el driver. El propio proyecto no reclama AirDrop. Lo que documenta como «AWDL/P2P» es
`awdl0` activo, `wifip2pd` enganchado y paquetes mDNS en `awdl0`. Ese camino (familia heredada,
`IO80211P2PInterface`) no existe en la `IO80211Family` de 26.7.1 que usa `RTL8814AirLayer.kext`, donde el
rol 6 pasa por la vía de [AWDL_ROLE6_RE.md](AWDL_ROLE6_RE.md). Sirve de referencia para un backend
itlwm sobre la familia heredada, no para este kext.

## 12. Artículos sobre AWDL y Continuity por BLE (revisión de 2026-11-03)

| Trabajo | Qué aporta aquí |
|---|---|
| Stute, Kreitschmann, Hollick, «One Billion Apples' Secret Sauce», MobiCom 2018 (arXiv 1808.03156) | AWDL en macOS 10.12/10.13 con Broadcom: formato de tramas, elección, sincronía, secuencia de canales, medidas |
| Martin et al., «Handoff All Your Privacy», PoPETs 2019 (arXiv 1904.10600) | Mensajes de Continuity por BLE (Nearby, Handoff, Wi-Fi), banderas que distinguen un Mac de un iPhone |
| Celosia, Cunche, «Discontinued Privacy», PoPETs 2020 (1) | Formato del mensaje AirDrop por BLE y sus hashes |
| hexway, «Apple bleee», 2020 | Captura de un anuncio AirDrop real, de acuerdo con el formato anterior |
| Shkurko, «Магия AirDrop», Habr 993750, 2026-02-09 | Divulgación sin datos de driver: BLE, UWB, `awdl0` en 149, y que Apple adopta perfiles Wi-Fi Aware para que AirDrop funcione con Android. Afirma sin fuentes un intercambio de claves poscuántico en 2026 `?` |
| srpopty, «AWDL协议及漏洞分析», blog, 2020-08-11 | Resumen en chino de MobiCom 2018 y USENIX 2019, sin datos nuevos. Su «métrica = aleatorio + contador» no coincide con `runParentElection` en 25G241 (§12.4) |

Los cuatro describen macOS 10.12-10.15 e iOS 10-13. Lo que sigue va marcado como del artículo salvo
donde se indica la comprobación en el kernelcache 25G241.

### 12.1 AWDL (MobiCom 2018)

- **Reparto de trabajo.** En aquellas versiones `IO80211Family` construía y analizaba las tramas y
  llevaba la máquina de estados; el driver Broadcom hacía lo crítico en tiempo, la sincronía incluida.
  Aquí ese papel lo cumple la capa (`RTLAwdInterface::alignClock`, `txTick`).
- **Cabecera fija.** `TTx,PHY` (+8) y `TTx,Target` (+12), en µs. `TTx,Target` se fija al crear la
  trama y `TTx,PHY` al encolarla; su diferencia aproxima el retardo de TX del emisor. El esclavo estima
  el inicio de la siguiente AW como `T_AW = t_AW·1024 − (TTx,PHY − TTx,Target) + T_Rx`, donde `t_AW`
  son las TU que el TLV 4 da hasta la siguiente AW extendida y `T_Rx` la llegada. Error aceptado: 3 ms
  (métrica de desalineación en `awdl_recv_action_frame` de Broadcom). En 25G241,
  `parseAwdlSyncParamsTlv` lee ambos campos y guarda la diferencia como `sync_contention`
  ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) §4).
  - Comparación con la capa: `alignClock` toma `T_Rx` al drenar la cola de RX (hilo de la thread
    call), no a la llegada, y no descuenta `TTx,PHY − TTx,Target`. La capa pone el mismo valor en los
    dos campos, así que en TX la diferencia es 0. Cuánto retrasa el drenado está sin medir `?`.
- **Elección.** Un nodo empieza con métrica 60, escucha 2 s en los canales sociales y después elige
  una métrica al azar en un rango que depende de la versión de AWDL: 405-436 en v2.x y 505-536 en v3.x.
  Gana la mayor. Sin trama del maestro durante 96 AW (≈1,5 s), otro nodo ocupa su lugar. El TLV 20
  (árbol de sincronía) lista el camino hasta el maestro superior para evitar bucles.
  - El rango de 26.7.1 está sin comprobar `?`. `awdlsim` imprime la métrica que anuncia `awdl0`
    ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §4).
- **RSSI.** Las tramas por debajo del umbral de sincronía de borde, -65 dBm (o -78 con AirPlay), se
  descartan, y las del maestro tienen 5 dB de margen. Coincide con el `rssi_threshold` por defecto de
  owly (-65).
- **Ventanas.** AW de 16 TU, modo de presencia 4, ventanas de extensión de 16 TU, ranura de 64 TU,
  secuencia de 16 entradas con `step` 3 y relleno `0xffff` (64 AW ≈ 1 s). Los MIF siguen la secuencia
  anunciada; los PSF salen en cualquier momento (periodo 110 o 440 TU).
- **TLVs.** 4 sync params, 5 y 24 elección, 18 secuencia, 20 árbol de sincronía, 6 parámetros de
  servicio, 2 respuesta de servicio, 16 ARPA, 12 estado del camino de datos, 7 HT, 17 VHT, 21 versión.
  El 17, que iOS envía y owly no (§4), es el de capacidades VHT. Versión 3.x en macOS 10.13/iOS 11;
  clase de dispositivo 1 macOS, 2 iOS.
- **Direcciones IPv6.** Link-local derivada de la MAC de origen de los AF, sin NDP.
- **Registro.** Los boot-args `awdl_log_flags`, `awdl_log_flags_verbose` y `awdl_log_flags_config`
  (todos `0xffffffffffffffff`, el último `1`) más `wlan.debug.enable=0xff` dan el registro detallado
  del manager. Comprobado en 25G241: `IO80211AWDLPeerManager::initWithInterface` los lee con
  `PE_parse_boot_argn` (`0xffffff80021c1a78`, `0xffffff80021c1acb`, `0xffffff80021c1d7b`), y
  `wlan.debug.enable` lo leen `IO80211_io80211isDebuggable` (`0xffffff8002196b91`) e
  `IO80211Controller::start` (`0xffffff800229204c`). El manager es el genérico, así que también actúan
  con `awdl0` de la capa.

### 12.2 Anuncio BLE de AirDrop (PoPETs 2019 y 2020, hexway)

- **Contenedor.** Estructura AD de datos de fabricante (`0xFF`) con el identificador de Apple
  `0x004C` y uno o varios mensajes Continuity en TLV: tipo de 1 byte, longitud de 1 byte, datos.
- **Mensaje AirDrop, tipo `0x05`, longitud `0x12`.** 8 bytes a cero, versión (1 byte, `0x01`), cuatro
  hashes de 2 bytes y un byte final a cero. Cada hash son los 16 bits altos del SHA-256, sin sal, de un
  correo o teléfono de la cuenta iCloud del emisor. Captura de hexway:
  `000000000000000001123412341234123400`.
  - Esto resuelve el `?` de [AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md) §4: los 18
    bytes típicos que construye `-[WPAWDL startConnectionlessAWDLServiceAdvertisingWithData:]` son
    este cuerpo.
- **Cuándo y desde dónde.** El emisor lo envía al abrir la hoja de compartir. Usa una dirección
  aleatoria no resoluble que no cambia hasta apagar y encender el Bluetooth. Un receptor en «Todos» no
  necesita que los hashes coincidan ([AWDL_AIRDROP_SIN_BLUETOOTH.md](AWDL_AIRDROP_SIN_BLUETOOTH.md):
  «In Everyone mode, start server»).
- **Otros mensajes.** Nearby Info (`0x10`) sale siempre que el Bluetooth está activo; su primer byte de
  datos dice si la Wi-Fi está encendida (`0x1C`) o apagada (`0x18`) en iOS 12. Las banderas BLE separan
  un Mac (solo LE) de un iPhone (LE y BR/EDR).
- **Consecuencia para las pruebas.** Cualquier emisor BLE que publique `0x004C`/`0x05` con ese cuerpo
  debería disparar `handleFoundHashes:rssi:` en el `sharingd` del Mac, con el receptor en «Todos» y el
  dongle UB400 activo: puede ser el A53 con una app de anuncios BLE, o un ESP32. Sin probar `?`. Es la
  mitad BLE de la prueba que el par virtual no cubre. La app es `Tools/airdrop_beacon`
  ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §6).

### 12.3 Foros de desarrolladores de Apple (CoreWLAN y Networking)

- **Hilo 847171** (septiembre de 2026), macOS 27.2 beta en un M4 Pro. Con la descubierta de AirDrop
  activa, el rendimiento de la Wi-Fi cae y la latencia al gateway da picos. `sudo ifconfig awdl0 down`
  los quita, y apagar AirDrop dejando AWDL arriba también. Feedback FB24842970; la única respuesta de
  Apple remite a ese informe.
  - Los picos de en2 con el tiempo compartido (`27284f5`, `abd2dd7`) son, por tanto, un comportamiento
    que también tiene hardware de Apple, no solo esta radio.
- **Hilo 818603** (abril de 2026), M5 Pro con el chip Wi-Fi propio de Apple (N1). El driver se
  identifica como `IO80211_driverkit-1540.16`: en ese hardware IO80211 corre como DriverKit, no como el
  kext `IO80211Family` que usa esta capa.
  - Si macOS lleva ese modelo a todo el hardware, la vía actual dejaría de servir. En 26.7.1 x86_64 la
    familia sigue siendo un kext. Sin más datos `?`.
- **Hilo 45283** («Wi-Fi Fundamentals», revisado en 2021). Solo confirma que AWDL es lo que usan
  `includePeerToPeer` de Network framework y Multipeer Connectivity, y que una radio única cambia de
  canal para atenderlo.

### 12.4 Ataques a AWDL y AirDrop (USENIX Security 2019)

Stute, Narain, Mariotto, Heinrich, Kreitschmann, Noubir y Hollick, «A Billion Open Interfaces for Eve
and Mallory: MitM, DoS, and Tracking Attacks on iOS and macOS Through Apple Wireless Direct Link»,
con sus diapositivas. Es el origen de OpenDrop.

- **Flujo de AirDrop.** El emisor anuncia sus hashes por BLE. Un receptor en «Todos», o con algún hash
  en su agenda, activa AWDL. El emisor busca `_airdrop._tcp` por mDNS sobre AWDL y hace
  `POST /Discover`, `/Ask` y `/Upload` por HTTPS con certificado de cliente. El receptor tiene que
  estar desbloqueado para ser descubierto.
- **Anuncio BLE completo (figura 4).** Flags (`02 01 1b`) y datos de fabricante
  (`17 ff 4c 00 05 12`, 8 ceros, `01`, cuatro hashes, `00`). Se envía como `ADV_NONCONN_IND`. «Devices
  in everyone mode will enable AWDL immediately after receiving any AirDrop BLE advertisement».
  - `Tools/airdrop_beacon` emite ese tipo de anuncio (no conectable y no escaneable, comprobado con
    `dumpsys bluetooth_manager` en el A53). La estructura de flags la pide con `setDiscoverable(true)`
    (API 34+), pero Android pone su propio valor, no `0x1b`. Si sale en el aire y si `sharingd` mira
    los flags está sin comprobar `?`.
- **Conexiones sin autenticar.** AirDrop acepta un emisor con certificado autofirmado y sin registro
  firmado por Apple si el receptor está en «Todos»; así funciona OpenDrop.
- **Elección.** El paper da `cA > cB ∨ (cA = cB ∧ mA > mB)`: contador primero, métrica después. El
  contador empieza al azar y crece mientras el nodo es maestro. MobiCom 2018 habla solo de la métrica,
  y owly (patch `cw-04`) observó en una captura de iOS reciente que la métrica decide primero.
  - **En 25G241 decide la métrica**, comprobado en `IO80211AWDLPeerManager::runParentElection`
    (`0xffffff80021ec0e4`).
    - Entre candidatos con roots distintos gana la mayor métrica del root (peer+579); a igualdad, la
      mayor MAC del root (peer+559).
    - Con el mismo root, el contador del root (peer+571, «RLFC») y después menos saltos (peer+575).
    - El propio nodo es root si su métrica supera la del mejor candidato, o es igual y su MAC es mayor
      o igual.
  - `IO80211AWDLPeer::parseAwdlElectionParamsV2` (`0xffffff80021ba26c`) copia el TLV 24 entero, 43
    bytes, en peer+556, así que esos campos son los del valor del TLV a partir de +0: MAC del root
    (+0), padre (+6), contador del root (+12), saltos (+16), métrica del root (+20) y métrica propia
    (+24). Descarta un TLV del mismo root con un contador menor que el guardado.
  - `IO80211AWDLPeerManager::isPeerSafeForElection` (`0xffffff80021eade6`) deja fuera de la elección,
    con «Discarding Legacy peer», a un par con versión AWDL (peer+73) menor que `0x33` o con contador
    del root 0.
    - El nodo de owly anuncia la versión `0x34`, pero su contador vale siempre 0. Como maestro de sí
      mismo, el Mac lo descartaría. `awdlsim` usa por eso un contador aleatorio distinto de cero
      (`--counter`, `Config::counter`).
- **Correcciones de Apple.**
  - La fuerza bruta de hashes BLE contra «Solo contactos» se corrigió en iOS 12.2.
  - CVE-2019-8612: desincronización, con un maestro falso que da a cada víctima parámetros de
    sincronía distintos.
  - CVE-2019-8567 y CVE-2019-8620: rastreo por MAC y nombre de host.
  - CVE-2018-4368: caída por tramas corruptas.
  - El MitM de AirDrop se corrigió en iOS 13 beta.
  - La desincronización explota que AWDL no autentica las tramas. El kernel actual podría filtrar
    maestros incoherentes `?`.

### 12.5 Wi-Fi y AWDL en Macs reales (hpese.club, 2026-03-25)

«不做背锅侠，Wi-Fi与AWDL的爱恨纠葛», de Lucas, en el blog de ingenieros de HPE Aruba. Mide el efecto de
AWDL en macOS 26 con Macs de Apple. Copia local guardada por el usuario en el escritorio.

- **Estado de AWDL en Tahoe.** `sudo wdutil info` tiene una sección AWDL: MAC, modo de AirDrop,
  `AWDL Enabled`, `Interface Name: awdl0`, `Schedule State: Infra Priority` y la secuencia de canales.
  - Ejemplo con la red en 36++: `36++ 36++ 149++ 0 0 0 0 36++ 6 36++ 149++ 36++ 0 0 0`. El canal
    de la red ocupa la mayoría de las ranuras, 149 a 80 MHz dos y 6 una.
  - Las cadenas `Schedule State` e `Infra Priority` están en el kernelcache 25G241; qué muestra
    `wdutil` con el `awdl0` de la capa está sin probar `?`.
- **Registro.** El artículo filtra la Consola por «AWDL is» para ver `AWDL is on/off`. En 25G241 el
  kernelcache tiene `AWDL is %s`, `AWDL is OFF`, `AWDL is SUSPENDed` y una docena de variantes.
- **Disparo por BLE.** Con el Bluetooth del Mac encendido, abrir AirDrop en un iPhone cercano activa
  AWDL en el Mac, aunque su AirDrop esté apagado («AWDL is On» en el registro). Al apagar el
  Bluetooth del Mac, AWDL pasa a off en unos segundos. Para la prueba de
  [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §6, esto sugiere mirar «AWDL is» además de `sharingd`.
- **Coste en un Mac real.** Flujo UDP cada 1 ms contra un servidor remoto:

  | AWDL | Fondo | Pérdida | Latencia media | Paquetes > 15 ms | Fluctuación máxima |
  |---|---|---|---|---|---|
  | off | — | 0,05 % | 8,1 ms | 1,3 % | 20 ms |
  | on | — | 4,48 % | 38,2 ms | 40,2 % | 240 ms |
  | off | 40 Mb/s | 0,07 % | 9,0 ms | 3,1 % | 22 ms |
  | on | 40 Mb/s | 9,46 % | 69,9 ms | 59,6 % | 422 ms |

  En comparación, el tiempo compartido de la capa dejó picos de ping al gateway de 15,7 ms
  (`abd2dd7`). Las medidas no son comparables directamente (ping local frente a UDP por internet).
- **Comentarios.** Un lector ve `awdl0` activa tras cada reposo en un M5 con 26.5.1. Otro apaga los
  canales 44 y 149 en los AP de su empresa.

### 12.6 Ingeniería inversa automática de protocolos (Kleber, 2024)

Defensa de tesis de Stephan Kleber (Ulm), «Automation of the Reverse Engineering of Unknown Binary
Network Protocols»: segmentación de mensajes (NEMESYS, con refinamiento por PCA) y agrupación de
tipos de mensaje. AWDL es uno de los conjuntos de evaluación junto a DHCP, DNS, NTP o SMB. No aporta
datos de AWDL. Sería aplicable a TLVs sin nombre de capturas propias (por ejemplo, las del
`--capture` de `awdlsim`), pero no se usa aquí.

### 12.7 AirDrop en macOS 26.3 (CISPA, 2026)

Ale Ebrahim y Tippenhauer (CISPA), «Protocol Prying: Systematic Vulnerability Research in the Apple
AirDrop and Android Quick Share Proximity Transfer Protocols». Analizan `sharingd` y
`Sharing.framework` en macOS 15.7.4 y 26.3 y en iOS 18.1 y 26.3. Los artefactos, AIRFUZZ incluido,
están en Zenodo (registro 20442029). Copia local: `~/Downloads/airdrop_research.pdf`.

- **Pila.** BLE/AWDL; IPv6 link-local en `awdl0`; TLS 1.2/1.3 con certificados autofirmados, sin
  verificar el nombre ni pedir certificado de cliente; HTTP/1.1; plist binario; DVZip/gzip; CPIO
  `newc` extraído con BOM. En 25G241 el `/Upload` con `newc` falla («Decompression failed») y con `odc`
  (`070707`) funciona; además exige la cabecera HTTP `TotalBytes`
  ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §8.1).
- **Servicio.** El receptor publica `_airdrop._tcp.local.` por DNS-SD sobre AWDL, con unas 25 claves
  TXT. La clave `flags` es una máscara de bits, por ejemplo `0x3FB`, cuyo bit `0xb` es `SupportsUPP`.
  `sharingd` escucha en TCP **8770** sobre AWDL.
  - «Recepción desactivada» no abre ese puerto.
  - En «Todos» toda la API es accesible sin autenticar.
  - En macOS el modo «Todos» no caduca a los 10 minutos.
- **API.** Siete POST: `/Discover`, `/Hello`, `/Ask`, `/Upload`, `/Exchange`, `/SharedIdentity` y
  `/Error`. `SDAirDropConnection` guarda un hueco por conexión para Discover, Ask y Upload.
  - `/Discover` responde `StatusCode` 100 (Todos), 200 (contacto) o 401 (solo contactos sin
    coincidencia).
  - `/Ask` muestra el aviso al usuario y espera hasta 15 s.
  - `/Upload` va en la misma conexión TLS que `/Ask`, salvo que se anuncie `SupportsUPP`.
  - Una ruta desconocida hace caer `sharingd` (V1, `fatalError` en el enrutador de Swift).
- **Aceptación.** Toda entrega necesita que el usuario acepte, o el mismo Apple ID verificado con
  `SFAppleIDVerifyCertificateChain`. Probaron diez maneras de saltarse el aviso y ninguna funcionó.
- **Consecuencia para las pruebas.** Un emisor que no es de Apple puede entregar un archivo a este Mac
  si puede hablar TCP con el link-local de `awdl0` en el puerto 8770: TLS autofirmado, `/Ask` y
  `/Upload`, aceptando el aviso a mano. Con `awdlsim` falta un puente IP: un `utun` que lleve los
  paquetes del nodo a una pila TCP del Mac, y un cliente como OpenDrop. Es la prueba de AirDrop de punta
  a punta sin iPhone; no está implementada `?`.

### 12.8 Guía de seguridad de Continuity (SEEMOO, reproducida en Zhihu)

Traducción al chino del trabajo de Heinrich, Stute y otros sobre Handoff, el portapapeles universal y
el envío de contraseñas Wi-Fi. Las herramientas están en `seemoo-lab/apple-continuity-tools`.

- **Para depurar con el UB400.** PacketLogger (Additional Tools de Xcode) registra el HCI del Bluetooth
  del Mac en `.pklg`, que Wireshark lee, y Bluetooth Explorer muestra los anuncios BLE cercanos. Sirven
  para comprobar si el anuncio de `Tools/airdrop_beacon` llega al Mac.
- **Otros servicios.** Handoff y el portapapeles usan `_companion-link._tcp.local` sobre AWDL, con
  claves TXT `rpBA`/`rpAD`, después de un anuncio BLE de tipo `0x0c` cifrado con AES-GCM. Necesitan el
  mismo iCloud en los dos equipos.
- **Otro artículo de Zhihu** («一晃13年过去了，苹果的Airdrop依然很坚挺», 2026-02-07). Divulgación sin datos
  nuevos: dice que AWDL usa «normalmente el 44».

## 13. Revisión de 2026-11-08

Búsqueda de novedades sobre AWDL y AirDrop fuera de Apple. Lo que se pudo comprobar en el material
original va sin marca; lo que solo dice un artículo de prensa lleva la fuente.

### 13.1 owly, nueva revisión

`t4t5/owly` (MIT, último push 2026-11-03) absorbió a `omdrop-owl`, cuyo repositorio ahora redirige
ahí. Los documentos `docs/SPEC.md`, `conformance.md`, `implementation.md` y `open-questions.md` están
copiados en `~/re_cache/work/awdl_refs/owly_docs`.

- **Modos.** En el modo compartido el Wi-Fi sigue asociado y el AP tiene que estar en el 6, el 44 o el
  149. Las tramas salen por una interfaz monitor con la MAC de la estación, que es la que da los ACK. El
  nodo anuncia solo las franjas que el teléfono ofrece en ese canal. En el modo exclusivo se baja el
  Wi-Fi, y los ACK los da una celda ad hoc o un AP oculto con el BSSID de AWDL.
- **Hardware según su tabla.** El modo compartido funciona en RTL8851BE, RTL8852AE, RTL8852BE,
  RTL8852CE, MT7925 y AR9287. El exclusivo funciona en Intel 9260 a BE200, en RTL8822BE, RTL8922AE y
  RTL8852BU (USB) y en MT7612U (USB). Fallan RTL8821CE, RTL8821AU, Qualcomm WCN6855/WCN7850 e Intel
  7265/8260/8265. El RTL8814AU no figura.
- **Reglas de `SPEC.md` que tocan a la capa:**
  - AWDL-12: se anuncia en cada franja solo el canal en que se va a estar, y 0 en las que no se
    atienden, porque un par manda datos a toda franja que cree compartida. La capa ya pone 0 en las
    franjas 0 y 8 con `en2` sin asociar ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17).
  - AWDL-16: los unicast van dentro de una AW compartida con 3 TU de guarda en los bordes, lo mismo
    que usa la capa desde `8E4C4AE5`. Los multicast llevan 16 TU de guarda y van en las franjas 0 y 10,
    salvo en un nodo que se queda en un canal, que los manda en cualquier franja que atiende con 3 TU.
  - AWDL-17: un par es válido recién tras un MIF y una TLV de versión con versión y clase distintas de
    cero. Se descarta tras 5 s de silencio, y los datos cuentan como haberlo oído. Es la regla de owly,
    no una medición de iOS. El corte por escaneo de la capa `001EF268` es de 0,38 s.
  - AWDL-18: un SYN de TCP de un par que aún no es válido se retiene hasta 2 s.
- **Rendimiento publicado.** En MT7925 con el Wi-Fi asociado: unos 490 kB/s de subida y 1,3 MB/s de
  bajada (resumen de BestHub, 2026-10-13; no está en el repositorio). La capa da entre 507 y 542 KB/s
  de subida en modo datos ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md)).

### 13.2 AWDL en `brcmfmac` para Macs con T2 (Linux)

Andre Eikmeyer envió el 2026-11-05 una serie de parches a `brcmfmac` (Phoronix, mismo día; el hilo de
la lista no se encontró). Según la carta de presentación, en esos chips el firmware lleva la agenda
AWDL, envía las tramas de acción que le pasa el host y transporta los datos. La elección, el
descubrimiento y AirDrop corren en espacio de usuario, en `t2-drop` (Rust, proyecto kaiT2en). Se activa
con firmware que anuncia `awdl` y acepta `interface_create` versión 3. El BCM4350 de linux-firmware
anuncia `awdl` pero rechaza ese `interface_create`.

Es el mismo reparto que se ve en el iPhone ([AWDL_IOS_27_HOST.md](AWDL_IOS_27_HOST.md) §1), y sirve de
referencia para un puerto sobre otro driver con firmware Broadcom.

### 13.3 Artefacto de CISPA en Zenodo (Airfuzz)

Es el registro 20442029 del artículo del §12.7. Está copiado en
`~/re_cache/work/awdl_refs/airfuzz/artifact-evaluation`; no se ejecutó nada de él. Un informe
comprobado, con 262 citas `archivo:línea` que resuelven y 15 citas textuales que coinciden, quedó en
`~/re_cache/work/awdl_refs/airfuzz/REPORT.md`.

- **No trae AWDL por el aire.** Las pruebas van sobre el `awdl0` de dos equipos Apple. Las tramas AWDL
  de `airfuzz/targets/kernel.py` son sintéticas: van envueltas en Ethernet con ethertype `0x88B7`, sin
  la cabecera fija de AWDL, y los números de TLV están cambiados (`AWDL_TLV_VERSION = 0x14`,
  `AWDL_TLV_SYNC_TREE = 0x11`, líneas 149-150). No sirve como referencia de formato.
- **No trae el PDF.** El directorio `paper/` que nombra su `README.md` no está en el zip.
- **Formato de `/Ask` en iOS 18** (`protocol-docs/RESEARCH_DOCUMENT.md:45-53`):
  - `TransferID` tiene que ser un diccionario `{"id": "<UUID>"}`;
  - `TransferType` tiene que ser `{"files": {}}`;
  - `Items` tiene que ser una lista vacía;
  - `FileBomPath` lleva el prefijo `./`;
  - `/Upload` tiene que ir en la misma conexión TLS.

  Con `TransferID` como texto, `/Ask` devuelve 200 sin mostrar el aviso, y el `/Upload` no llega a
  nada. Con `/Upload` en otra conexión, el iPhone responde con RST (líneas 298-305).
- **DVZip.** `airfuzz/protocol/encoder.py` arma CPIO `odc` (`070707`) con la entrada `.`, el archivo
  `./<nombre>` y `TRAILER!!!`. Lo parte en bloques de `0x10000` bytes y antepone a cada bloque
  comprimido con zlib (nivel 6) su longitud de 4 bytes big-endian (líneas 75-80). Coincide con lo medido
  en 25G241, donde `newc` falla y `odc` funciona ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §8.1).
- **Rutas de `sharingd` en macOS 26.3**
  (`vulnerabilities/V1-airdrop-fatal-assertion/ROOT-CAUSE-ANALYSIS.md:178-186`): `/Discover`,
  `/Hello`, `/Ask`, `/Upload`, `/Exchange`, `/IdentityShare` y `/Error`. Cualquier otra con cuerpo
  termina en `fatalError`. El §12.7 dice `/SharedIdentity`, tomado del artículo; en el binario de 26.3
  la ruta se llama `/IdentityShare`.
- **`StatusCode`** aparece en `airfuzz/targets/macos_local.py:591` junto a
  `ReceiverMediaCapabilities`, lo que respalda la respuesta de `/Discover` del §12.7.

### 13.4 AirDrop en Tahoe con un fork de OCLP (InsanelyMac)

El hilo de kgp (desde 2025-12-04, con notas de 26.7) consigue AirDrop en ambos sentidos solo con una
Broadcom BCM943602CDP. El parche restaura la pila inalámbrica de Apple en el volumen del sistema y pide
AMFIPass 1.4.1 con `-amfipassbeta`, `DisableIoMapper`, `SecureBootModel` desactivado y un
`IOSkywalkFamily` de reemplazo. Con Intel AX210 no lo da por fiable. Es la misma vía que
[AWDL_REF_SIMPLEBCM.md](AWDL_REF_SIMPLEBCM.md): tarjeta Broadcom, no un driver de otro fabricante.

### 13.5 Wi-Fi Aware en lugar de AWDL

Por la orden de interoperabilidad de la UE, Apple tiene que implementar Wi-Fi Aware y dejar AWDL (blog
de Ditto, 2025). No hay fecha para retirar AWDL ni se dice qué pasa con AirDrop. iOS 27.2 sigue trayendo
`IO80211AWDLPeerManager` ([AWDL_IOS_27_HOST.md](AWDL_IOS_27_HOST.md)).

### 13.6 Proyectos que se siguen

- **AirPort_RTW89 (FranciumOS-org).** Sin código nuevo desde el 2026-11-02. La última release es la
  v0.5.0-BETA, del 2026-10-30. La versión con AirDrop del video del 2026-11-04 no está publicada.
- **AirportRTW (souissiak1-maker).** La última es V2.3.1, del 2026-11-04, con el AWDL apagado: sin
  llamadas desde el código de producción ni capacidad anunciada. En Discord se dijo el 2026-11-06 que
  hacía AirDrop; no hay código que lo respalde. Adapta el `AWDLInterface.h` público de este repositorio,
  con crédito en el código.
- **Sin novedades útiles:** AMD_Vanilla para 26.7, y drivers Realtek USB nativos más allá de
  TahoeRealtekWiFi.

### 13.7 AWDL en un fork de itlwm (crazyhulk, AX210)

Un hilo de pcbeta (2084025, 2026-11-08) dice que hay AirDrop en ambos sentidos con una Intel AX210 en Tahoe.
La afirmación es solo del autor: en el hilo, otro usuario con AX210 no lo reprodujo, y AX200, AX201 y AX211
fallan.

- **Sin código publicado.** El tag `awdl` de `crazyhulk/itlwm` apunta a `7204c69d`, el mismo commit que el
  `main` de `laobamac/itlwm`, sin AWDL. La release `awdl-beta` solo trae un adjunto,
  `AirportItlwm.kext.zip` (15 138 781 bytes, SHA-256 `2a027394…78756a40`, x86_64, versión 2.4.0, binario del
  2026-11-08 06:52). `BlueToolFixup-FixA2DP` `ble-beta` es igual: un binario de 12 KB.
- **Lectura en IDA** (copia y base fuera del repositorio, en `~/re_cache/work/awdl_refs/crazyhulk_itlwm`).
  Los símbolos C++ están, y no hay nada tomado de `AWDLInterface.h`.
  - **Control.** `AirportItlwmAWDLInterface` es una `IOSkywalkEthernetInterface` que la familia crea con
    rol 6 (`createAWDLInterface`, `0x6b80`). Atiende las ioctl AWDL (`SYNC_PARAMS`, `SYNC_FRAME_TEMPLATE`,
    `SYNCHRONIZATION_CHANNEL_SEQUENCE`, `ELECTION_*`, `OOB_*`) y arma él mismo PSF y MIF
    (`buildAWDLFrame`). Enlaza `com.apple.iokit.IO80211Family` 1.5, la familia anterior que AirportItlwm
    inyecta en Tahoe, no la nativa de 26.
  - **Datos sin la cola P2P de Skywalk.** El `awdl0` no es la interfaz Skywalk de la familia. Es una
    `IOEthernetInterface` clásica, `AirportItlwmAWDLEthernetInterface` (prefijo `awdl`), conectada con
    `IONetworkController::attachInterface`. `outputAWDL` (`0x13b0`) pasa cada mbuf a un anillo de 32
    (`enqueueData`, `0x6a70`). `flushData` (`0x5bc0`) arma la trama 802.11 de datos: BSSID AWDL, LLC
    `AA AA 03 00 17 F2 08 00`, cabecera `03 04`, secuencia y ethertype. Después la inyecta en crudo en el
    canal del momento; un retorno 35 la deja en cola. Así evita la cola TX de la interfaz P2P de la familia,
    donde `dequeueTxPackets` devuelve null en el trabajo de AirportItlwm para Tahoe (según un comentario de
    r/hackintosh sobre OpenIntelWireless/itlwm#1062; no comprobado aquí).
  - **RX.** `inputActionFrame` (`0x14c0`) pasa los AF con OUI `00 17 F2` tipo 8 al pool de tramas de
    gestión de la familia (`getMgmtFramePool`, `AWDLBufferABI`), con canal, RSSI y hora de recepción del
    host. `syncFromFrame` sigue al maestro. `isSyncMaster` solo es cierto sin maestro o con el propio
    (`0x2fe0`): no hay elección propia.
  - **Tiempo.** Lleva la línea de AW con temporizadores (`scheduleNextAW`, `0x3240`) y la TX a +1 ms del
    instante pedido (`scheduleTx`, `0x57c0`). Asociado en otro canal, cada N AW pide al firmware un
    remain-on-channel al canal AWDL (`maybeListen`, `0x4160`); en el mismo canal no salta.
  - **Boot-args.** `-itlawdl` (arranque), `-itlawdlaw` (línea de AW), `-itlawdllisten`, `-itlawdltx`,
    `-itlawdldata`, `-itlawdlp2p` y `-itlawdlpromisc`. Sin ellos, nada de lo anterior corre.
- **Para este repositorio.** El formato de los datos coincide con el que ya usa la capa, y la ruta nativa
  de 26 no tiene el problema de la cola P2P: la arquitectura no cambia. Para un puerto sobre la familia
  anterior, como itlwm, la `awdl0` clásica es la forma de esquivar esa cola. Que AirDrop complete por ahí no
  está comprobado `?`.
- **Sincronización, comparada con `alignClock` de la capa.**
  - **Lo mismo.** Los dos siguen solo al nodo que se anuncia maestro en su TLV 4, y toman de ahí el contador
    de TU hasta el próximo límite y el número de AW (`syncFromFrame`, `0x29e0`).
  - **La capa hace más.** Resta la demora del emisor (PHY TX menos target TX, cabecera fija +8 y +12). El
    fork solo la publica como `AWDLSyncTxDelayUs`. La capa además elige maestro por la elección v2 de la
    plantilla y lo retiene 2 s.
  - **Rechazo de saltos, que la capa no tiene.** Ya sincronizado, si el número de AW coincide y la fase se
    mueve más de 6000 µs, el fork descarta la trama hasta dos veces seguidas y cuenta `AWDLSyncOutliers`.
    La tercera la acepta. La capa toma cada trama del maestro tal cual.
  - **Hora de recepción del chip, que la capa no usa.** `hostRxUs` (`0x2530`) parte de la marca de tiempo
    del firmware en cada trama y sigue el desfase entre host y chip. Lo deja bajar sin límite, lo sube como
    mucho 20 µs por trama y lo reinicia si salta más de 20 ms. A la hora del host le resta lo que la trama
    tardó de más que ese mínimo, y así quita la demora variable del bus. La capa sella la llegada con
    `awdlNowUs()` en el hilo RX (`AWDLInterface.cpp`, línea 666), después del USB. El descriptor RX del
    RTL8814A trae `TSFL`, los 32 bits bajos del TSF del chip, en el byte 20
    (`GET_RX_STATUS_DESC_TSFL_8814A`, `rtl8814a_recv.h:156`). El driver no lo lee: `pattrib->tsfl` no se
    asigna para este chip. Con `en2` asociado, ese TSF sigue al del AP y salta, de ahí el reinicio a 20 ms.
  - **Estado.** Cuánto varía la demora USB entre el aire y `awdlNowUs()` no está medido `?`. El `drain` que
    registra la capa (32 a 69 µs en las corridas del 11-05 y 11-06) solo cubre del hilo RX a `alignClock`.
    Las dos mejoras son candidatas si el iPhone muestra `Peer Absence` o desalineación. Primero se mediría
    la dispersión de host menos `TSFL`.
