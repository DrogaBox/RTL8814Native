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
