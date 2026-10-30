# Capacidades anunciadas en los TLV de salida del `IO80211AWDLPeerManager` (7, 17, 21 y 12)

De dónde toma el `IO80211AWDLPeerManager` cada valor que emite en los TLV 7 (HT capabilities),
17 (contenedor IEEE 802.11, VHT), 21 (versión AWDL y clase de dispositivo) y 12 (estado del data
path), qué contesta `RTL8814AirLayer` hoy a cada fuente, y qué campos podrían anunciar 40/80 MHz,
guard interval corto, LDPC o más cadenas espaciales de las que la radio recibe de verdad en `awdl0`.
Base: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). Lo no verificado lleva `?`.

## 1. Las tres plantillas y los cuatro builders

Los TLV salen de tres funciones que componen plantillas completas sobre buffers del propio Peer
Manager (PM):

| Plantilla | VA | Uso |
|---|---|---|
| `updateSyncFrameTemplate` | `0xffffff80021dc5b2` | plantilla de 512 B del sync frame (PSF); la baja con el ioctl interno `0x97802869C8` (tipo 151, `AWDL_SYNC_FRAME_TEMPLATE`) |
| `buildMasterIndicationTemplate` | `0xffffff8002211b42` | plantilla de la master indication (UMI) |
| `buildSecondaryMasterIndicationTemplate` | `0xffffff8002210436` | plantilla del master indication secundario |

Las tres invocan a los mismos builders con las mismas cachés: TLV 12 con
`buildAwdlDataPathStateTlv` (`0xffffff8002210e5a`), TLV 7 con `buildEnhDataRateTlv`
(`0xffffff800221126c`), TLV 17 con `buildIEEE80211CntnrTlv` (`0xffffff80022113c4`) y TLV 21 con
`buildAWDLVersionTlv` (`0xffffff8002211846`). Puntos de llamada de cada builder:

| Builder | `updateSyncFrameTemplate` | secundaria | MI |
|---|---|---|---|
| TLV 12 | `0xffffff80021dc6a1` | `0xffffff8002210518`, `0xffffff8002210674` | `0xffffff8002211c77`, `0xffffff8002211d61` |
| TLV 7 | `0xffffff80021dc6e5` | `0xffffff8002210577` | `0xffffff8002211e1c` |
| TLV 17 | `0xffffff80021dc863` | `0xffffff8002210789` | `0xffffff8002211f8b` |
| TLV 21 | `0xffffff80021dc89d` | `0xffffff80022108f6` | `0xffffff80022120fb` |

El hardware confirma la cadencia: tras `enableAWDL` el PM reconstruye la plantilla y baja el
ioctl 151 una vez por segundo ([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)
§tabla; `enableAWDL` llama a `updateSyncFrameTemplate` en `0xffffff80021dd375`).

### Objetos de datos del PM

| Símbolo | Objeto | Contenido relevante |
|---|---|---|
| `sub` = `*(pm+0x18)` | datos de caps/superficie del PM | cache HT `+0x945` (28 B), longitud HT `+0x61C`; flag VHT `+0x620` y cache VHT `+0x961` (14 B); flag HE `+0x621` y cache HE `+0x970` (40 B); cache país `+0x9A4` (3 B); cache BSSID infra `+0x930` (6 B); canal infra `+0x9C0` y flags `+0x9C4` |
| `state` = `*(pm+0x20)` | estado del PM | clase de dispositivo `+0x4008`; contadores y marcas del TLV 12 (`+0xA608`, `+0x3EF8`, `+0x92C0`, `+0x3F04`…) |
| `ctrl_data` = `*(*(sub+0x520)+0x120)` | objeto del controlador | bytes de capability `+0xB37`, `+0xB3F`, `+0xB45` (lectores verificados; escritores no identificados `?`) |

## 2. TLV 7 — HT capabilities: `buildEnhDataRateTlv`

El builder copia un cache; no consulta al driver:

- `0xffffff800221127e`: lee la longitud `_htCapabilityLength` en `sub+0x61C`; si `len+2` excede el
  hueco, salta el TLV (log `54BBh`, `0xffffff80022112af`).
- `0xffffff80022112be`: escribe la palabra de cabecera a 0; `0xffffff80022112d1`: fuente
  `sub+0x947`; `0xffffff80022112dd`: `memmove` de `len` bytes; devuelve `len+2`.

Consecuencia del cálculo de la longitud (ver §2.2): el cuerpo del TLV son los bytes
`sub+0x947..sub+0x946+len`, es decir el cache HT sin sus dos primeros bytes.

### 2.1 Quién llena el cache HT

| Escritor | VA | Petición |
|---|---|---|
| `IO80211PeerManager::getHtCapabilityIE` | `0xffffff8002252a56` | GET interno `0xC02869C9` tipo 154 (`HT_CAPABILITY`), respuesta de 40 B |
| `IO80211PeerManager::setHtCapabilityIE` | `0xffffff800225283a` | SET interno `0x802869C8` tipo 154; llamado sólo por el handler de la familia `setHT_CAPABILITY` (`0xffffff800228912f` desde `0xffffff80022890de`) |

Ambos guardan los 28 bytes de payload de la respuesta (struct `apple80211_ht_capability`: 4 B de
versión + 28 B) en `sub+0x945` (tiendas en `0xffffff8002252b46` y `0xffffff800225291c`) y calculan
la longitud en `sub+0x61C` como índice del último byte no nulo +1 dentro de los 28. Sólo
`getHtCapabilityIE` limpia además el bit 0x10 (HT-Greenfield) de la palabra en `sub+0x947`; el
camino SET no lo hace.

### 2.2 Compuertas de emisión

Los tres llamadores exigen `1 <= _htCapabilityLength <= 26`: con longitud fuera de rango
`updateSyncFrameTemplate` registra `"Invalid _htCapabilityLength %u, skipping buildEnhDataRateTlv"`
y salta el TLV (llamada al builder en `0xffffff80021dc6e5`; mismo check en las otras dos
plantillas). Con un cache vacío (longitud 0) el TLV 7 no se emite.

### 2.3 De dónde sale la respuesta del tipo 154

`IO80211PeerManager::do_ioctl` (`0xffffff800225254a`) enruta por `apple80211RouteInternal` hacia
**la interfaz primaria del controlador** (slot `0xC80` = `getPrimarySkywalkInterface`), con
alternativas `sub+1376` y `sub+1368`. Para el PM de `awdl0` la primaria es `en2` (verificado en
[AWDL_STAGE1_OPEN_POINTS.md](AWDL_STAGE1_OPEN_POINTS.md) §3): **el GET/SET 154 del PM aterriza en
`en2`, no en `awdl0`**.

En la ruta de usuario el handler `getHT_CAPABILITY` (`0xffffff800227c503`, tabla tipo 154 en
`0xffffff800245f028`) exige `len == 32`, prueba primero el glue WCL
(`IO80211Glue::sendIOUCToWcl` con `0xC02869C9`/154) y sin glue cae al puente
`apple80211getHT_CAPABILITY` (`0xffffff8002263d2f`): cast a `IO80211InfraProtocol` y llamada al
slot homónimo; sin cast responde `0xE082280E`. Los puentes VHT y HE tienen la misma forma
(`getVHT_CAPABILITY` `0xffffff800227cee8` con `len == 20`, `getHE_CAPABILITY`
`0xffffff80022818e9` con `len == 40`; set VHT `0xffffff8002289afa`, set HT `0xffffff80022890de`).

## 3. TLV 17 — contenedor IEEE 802.11 (VHT): `buildIEEE80211CntnrTlv`

El builder es un `memmove` puro del buffer que pasa el llamador (`0xffffff80022113c4`; cabecera
escrita por el llamador, cuerpo en `+0x961` abajo). Las tres plantillas le pasan lo mismo:
**los 14 bytes del cache VHT `sub+0x961`**, y sólo si el flag `sub+0x620` bit 0 está puesto
(`updateSyncFrameTemplate`, check en el decompilado; puntos de llamada de §1). Con el flag a 0 el
TLV 17 no se emite.

### 3.1 Quién llena el cache VHT

| Escritor | VA | Petición |
|---|---|---|
| `IO80211PeerManager::getVHtCapabilityIE` | `0xffffff8002252c88` | GET interno `0xC02869C9` tipo 214 (`VHT_CAPABILITY`), respuesta de 20 B; guarda `a2+4..+17` (qword→`+0x961`, dword→`+0x969`, word→`+0x96D`) y pone `sub+0x620 = 1` |
| `IO80211PeerManager::setVHtCapabilityIE` | `0xffffff8002252e66` | SET interno `0x802869C8` tipo 214 (sólo llega desde el handler `setVHT_CAPABILITY` `0xffffff8002289b4b`); mismo store y mismo flag |

La respuesta tipo 214 que se cachea son 14 bytes: VHT Capability Info (4 B), mapa MCS RX (2 B),
RX Highest (2 B), mapa MCS TX (2 B), TX Highest (2 B) y 2 B finales del struct `?` (la partición
exacta de los últimos 2 bytes no se verificó; el orden de los stores sí).

### 3.2 HE: cache presente, sin salida

`getHeCapabilityIE` (`0xffffff8002253062`, GET interno tipo 570, respuesta de 40 B sin prefijo de
versión) guarda el bloque en `sub+0x970` y marca `sub+0x621`, pero **ningún builder de TLV usa el
cache HE en 25.6.0**: `getMyHeCap` (`0xffffff800225f63c`) no tiene llamadores, y los wrappers
`IO80211AWDLPeerManager::get{Ht,VHt,He}CapabilityIE` (`0xffffff80021cdcd8`, `0xffffff80021cddd6`,
`0xffffff80021cddf8`, slots virtuales `+0x210/+0x218/+0x220` de la vtable del PM) y los helpers
`getAwdl*CapabilityIE` (`0xffffff800221fe4a`, `0xffffff800221fe80`, `0xffffff800221feac`) no
tienen llamadores directos ni indirectos encontrados en las funciones `IO80211*`. El SET 570 no
tiene handler (`type 570` en la tabla set = 0). El TLV 17 de este build es sólo VHT.

## 4. TLV 21 — versión AWDL: `buildAWDLVersionTlv`

Escribe 5 bytes: tipo 21 (`0xffffff800221187e`), longitud 2 (`0xffffff8002211881`), **versión
`0xA0` constante** (`0xffffff8002211887`) y byte de clase de dispositivo leído de
`state+0x4008` (`0xffffff800221188f`, store en `0xffffff8002211895`).

La clase de dispositivo no viene del driver:

- `IO80211AWDLPeerManager::initWithInterface` (`0xffffff80021c1540`) escribe `dword 1` en
  `state+0x4008` (`0xffffff80021c292b`): clase 1 = macOS.
- El boot-arg `override_awdl_platform_type` (`PE_parse_boot_argn` en `0xffffff80021c294c`)
  sustituye ese valor si está presente y no nulo (`0xffffff80021c296e`); una tabla de nombres en
  `0xffffff800245B500` (primera entrada `"MacOS"`) valida las clases 1..9.

Nada de la capa participa en el TLV 21.

## 5. TLV 12 — estado del data path: `buildAwdlDataPathStateTlv` + `fillLegacyDataPathPayload`

`buildAwdlDataPathStateTlv` (`0xffffff8002210e5a`; exigencia `hueco > 0x31`, devuelto 50) monta un
TLV de 50 bytes (tipo 12 en `+0`, longitud 47 en `+1..+2`) y delega el cuerpo en
`fillLegacyDataPathPayload` (`0xffffff80022143ca`, firma `(tlv&, bool legacy, u32 canal, bool,
bool)`). Desglose del cuerpo (offsets desde el byte 3 del TLV):

| Cuerpo | Contenido | Fuente | Escritor del cache |
|---|---|---|---|
| +0/+1 | flags u16, base `0x8B03` (`0xffffff80022143de`) o `0x8B23` si bit 5 de `ctrl_data+0xB37` (`0xffffff80022143fb`, `0xffffff8002214404`); `\|0x40` si `state+0xA648 != 0`; `\|0xC0` si bit 0 de `state+0xF624`; `\|0x400` si bit 0 de `state+0x3FA1` y `state+0xA6A4 != 0`; `\|0x2000` si la clase de dispositivo es 2 y bytes `state+0xFB85..0xFB87` lo permiten; `\|0x4000` según bit 1 de un byte tras `ctrl_data+880` `?` | ivares del PM | — |
| +2..+4 | código de país (3 B) | `sub+0x9A4` | `interfaceStateChange` (`0xffffff80021da2bd`, `0xffffff80021da2c3`) copia los 3 B de `state+0x3E9C`; semilla: boot-arg `forcecountry` en `initWithInterface` (escrituras en `state+0x3E9C` verificadas en el decompilado) |
| +5/+6 | u16 | `state+0x429C` | `?` |
| +7..+12 | BSSID de la infraestructura (6 B); sólo si el llamador pasa el flag legacy o bit 0 de `state+0x4050` | `sub+0x930` | `getInfrastructureState` (`0xffffff80021da9b1`) desde `IO80211Controller::getBSSIDData` (`0xffffff8002294e5a`; glue WCL o puente `getBSSID`) |
| +13/+14 | canal de la infraestructura (u16; 0 si bit 13 de `sub+0x9C4`) | `sub+0x9C0` | `reportDataPathEvents`, caso 82 del jump table (`0xffffff80021de15a` y `0xffffff80021de164`, desde el payload del evento +4/+8; los eventos 41/42 del driver) |
| +15..+20 | dirección de la infraestructura (6 B) | `state+0x9160`; si `state+0x9166` no es la dirección nula, `state+0x9166..+0x916B` | `?` |
| +21/+22 | constante 4 | builder | — |
| +23..+26 | u32 canal del llamador | argumento; 0 en la plantilla de sync frame, canal de la UMI en la MI | — |
| +27/+28 | flags extendidas u16: `0x20` fija; bit 11 = bit 7 de `ctrl_data+0xB3F`; bit 13 = bit 0 de `state+0xFF98`; de `fillLegacy`: bits 0/1 (contador `state+0xFA3A`), `0x04/0x10/0x40` según estado interno, `0x80` (reporteros), **`0x100` = bit 7 de `ctrl_data+0xB45`** (el mismo byte que `getHeCapable` `0xffffff800225f3f0` devuelve como "HE capable"), `0x200` (`state+0xF628`), `0x400` (bit 0 de `state+0xFE99`), `0x1000` | ivares del PM | — |
| +29/+30 | u16 | `state+0xD064` o `state+0xFA58` | `?` |
| +31..+34 | u32 | `state+0x917B` | `?` |
| +35..+38 | milisegundos desde la marca `state+0xA608` | `mach_absolute_time` en el builder | — |
| +39..+42 | u32 | `state+0x3EF8` | `?` |
| +43..+46 | contador u32 | `state+0x92C0`, incrementado por cada build cuando el llamador lo pide (MI lo pide, la plantilla de sync no) | — |

El TLV 12 no lleva capacidades PHY: nada de ancho de canal, GI, LDPC ni cadenas.

## 6. Qué contesta `RTL8814AirLayer` hoy a las fuentes anteriores

| Fuente | Ruta hacia la capa | Respuesta actual |
|---|---|---|
| GET 154 (`HT_CAPABILITY`) | `do_ioctl` del PM → interfaz primaria (`en2`) → slot 479 de `RTLAirInterface` | stub heredado `kIOReturnUnsupported` (`Source/IO80211/IO80211_25_6_0_infra_stubs.inc:13`; sin override en `Source/AirLayer/RTLAirLayer.cpp`) |
| SET 154 | igual, slot 557 | stub `kIOReturnUnsupported` (`infra_stubs.inc:91`) |
| GET/SET 214 (`VHT_CAPABILITY`) | igual, slots 482/561 | stubs `kIOReturnUnsupported` (`infra_stubs.inc:16`, `:95`) |
| GET 570 (`HE_CAPABILITY`) | igual, slot 541 | stub `kIOReturnUnsupported` (`infra_stubs.inc:75`); SET 570 no existe en la tabla |
| GET/SET 154/214/570 dirigidos a `awdl0` | puente con cast a `IO80211InfraProtocol` | `RTLAwdInterface` no es `IO80211InfraProtocol` (`Source/AirLayer/AWDLInterface.h:297`): el puente responde `0xE082280E` sin llegar a la capa |
| ioctl interno 151 (plantilla de sync frame) | puente → slot `+0x1118` de `awdl0` | `RTLAwdInterface::setAWDL_SYNC_FRAME_TEMPLATE` responde `kIOReturnSuccess` sin guardar la plantilla (`Source/AirLayer/AWDLInterface.cpp:341-358`, macro `AWDL_ACK`) |
| eventos 41/42 (canal/flags infra del TLV 12) | `reportDataPathEvents` | la capa no los emite (ventanas diferidas, [AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)) |
| `getBSSID` (BSSID infra del TLV 12) | `getBSSIDData` → glue/puente | sin override en la capa; comportamiento heredado sin verificar en runtime `?` |

Resultado: con la capa actual, `_htCapabilityLength` queda a 0 y `sub+0x620` a 0, de modo que
**el PM no emite ni el TLV 7 ni el TLV 17**; el TLV 21 sale siempre (`0xA0`, clase 1) y el TLV 12
sale con canal de infraestructura 0, BSSID 0 (verificado sólo el mecanismo, no en el aire `?`) y
las flags derivadas de ivares internos. Los pares ven un nodo sin HT ni VHT anunciados.

## 7. Campos por los que un driver podría anunciar más de lo que la radio recibe

Hoy el riesgo es cero porque los TLV 7 y 17 se omiten. Si la capa llegara a implementar GET/SET
154/214 (o un glue WCL respondiera por ella), el contenido de la respuesta sale al aire casi
verbatim — el kernel sólo limpia el bit HT-Greenfield en el camino GET. Los campos mentirosos
serían:

| Campo | TLV | Bits en juego |
|---|---|---|
| HT Capability Info (2 B, cuerpo del TLV 7) | 7 | bit 0 LDPC; bits 1-2 ancho (40 MHz); bit 5 SGI 20; bit 6 SGI 40; bits 7-8 STBC |
| A-MPDU Parameters (1 B) | 7 | longitud/exp/intervalo acordados |
| Supported MCS Set (16 B) | 7 | máscara RX de MCS: define cuántas cadenas y tasas el par puede usar contra este nodo |
| VHT Capability Info (4 B, TLV 17) | 17 | bits 2-3 ancho (80/160/80+80); bit 5 SGI 80 |
| Mapas MCS RX/TX (8 B) | 17 | 2 bits por cadena (3 = no soportada) |

El fallo documentado en [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §7 aplica
directamente: con 40 MHz anunciados y la radio sintonizada a 20 MHz, 837 de 838 tramas llegaron
sólo como reintento a MCS 0. En `awdl0` el RTL8814AU opera a 20 MHz en el canal social, de modo
que cualquier bit de ancho 40/80, cualquier SGI de 40/80 y cualquier cadena por encima de lo que
el radio realmente recibe en ese ancho provoca tramas del par que la radio no decodifica. La
capacidad LDPC del RTL8814AU en recepción no está verificada en este repositorio `?`: mientras no
lo esté, el bit 0 del HT Capability Info debe quedar a 0.

## 8. Cómo mantener los valores honestos

1. **Omisión como estado válido**: no implementar GET 154/214/570 deja los TLV 7/17 fuera del
   aire; los pares usan tasas legacy. Es interoperable y seguro; el coste es tasa en las ventanas
   AW.
2. **Si se implementa GET 154** (respuesta de 40 B, 28 B de payload): ancho a 20 MHz (bits 1-2 =
   0), SGI sólo si el driver lo aplica de verdad en TX/RX a 20 MHz, LDPC a 0, máscara MCS limitada
   a las cadenas que la radio recibe en `awdl0` (la misma cifra que `getNSS` ya responde desde el
   estado de la radio, `Source/AirLayer/RTLAirLayer.cpp:988`), A-MPDU coherente con lo que
   negocia el driver. Los bits de ancho deben cuadrar con `getSUPPORTED_CHANNELS`, que hoy
   publica sólo entradas de 20 MHz (`Source/AirLayer/RTLAirLayer.cpp:1011-1044`).
3. **Si se implementa GET 214** (respuesta de 20 B, 14 B cacheados): bits de ancho a 20 MHz (bits
   2-3 = 0), SGI 80 a 0, mapas MCS con 3 (no soportada) en las cadenas por encima de la real. Nota:
   aun con VHT a 20 MHz anunciado, un par que decida enviar VHT usa MCS de una cadena a lo sumo;
   el mapado honesto evita que pruebe más.
4. **Cuidado con el SET**: `setHtCapabilityIE`/`setVHtCapabilityIE` guardan el payload sin
   validar ancho ni limpiar greenfield. Si algún día se aceptan SET 154/214 desde el daemon, hay
   que validar el payload contra el ancho real antes de aceptarlo, o rechazarlos (hoy ya fallan
   con el stub `kIOReturnUnsupported`, que es la respuesta segura).
5. **TLV 21 y TLV 12** no necesitan protección: el 21 no lee nada del driver (§4) y el 12 no
   lleva PHY caps (§5). La única bandera híbrida es el bit `0x100` de las flags extendidas del
   TLV 12, espejo del byte "HE capable" `ctrl_data+0xB45`: como sus escritores no están
   identificados `?`, conviene comprobar en el aire que sale a 0 antes de fiarse de él.
6. **Coherencia con la entrega de plantillas**: la plantilla reconstruida se baja por el ioctl 151
   y la capa la acepta sin guardarla, así que nada de esto llega todavía a la radio
   ([AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)); los valores del TLV
   sólo describen lo que el PM *anunciaría* si las tramas PSF/MIF salieran.

## 9. Abiertos

- Escritores de `ctrl_data+0xB37/0xB3F/0xB45` no encontrados por desplazamiento (posible escritura
  masiva por `memcpy` en la init del objeto `*(ctrl+0x120)`): identidad de esos tres bytes `?`.
- Escritores de `state+0x429C`, `state+0x9160/0x9166`, `state+0xD064`, `state+0x917B`,
  `state+0x3EF8` (campos del TLV 12) `?`.
- Comportamiento real de `getBSSIDData` contra la capa (sin override de `getBSSID`) `?`.
- Partición exacta de los 2 bytes finales del cache VHT (14 B) `?`.
- Los slots virtuales `+0x210/218/220` del PM (getters HT/VHT/HE) no tienen llamador localizado
  en 25.6.0: los caches VHT/HE sólo se llenarían por el camino SET de usuario `?`.
