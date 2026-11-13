# Puerto de la secuencia de canales AWDL a macOS 14.8.9 (23J631) y macOS 15.8.2 (24H232)

Documento de referencia para el soporte de la secuencia de canales de Apple Wireless Direct Link (AWDL) en versiones anteriores de macOS, tomando como origen la implementación documentada para macOS 26.7.1 (25G241, Darwin 25.6.0) en `Docs/AWDL_SECUENCIA_CON_INFRA.md`, `Docs/AWDL_SECUENCIA_129_EMISOR.md`, `Docs/AWDL_CHANNEL_STRATEGY.md`, `Docs/AWDL_CARD_CAPS_14_15.md`, `Docs/AWDL_DATA_PATH_PORT_14_15.md` y `Docs/AWDL_SECUENCIAS_OBSERVADAS.md`.

Las direcciones virtuales (VA) de código citadas son cabezas de instrucción en las colecciones de kernel x86_64; las tablas de tipos de franja, los gates de doble banda, los tokens y las funciones citadas se comprobaron en el desensamblado, y los desplazamientos de ivars del §2 que no llevan VA propia se toman de las lecturas de esas funciones `?`. Colecciones (`boot_awdl.kc.i64`, `boot_24H232.kc.i64`, `boot_23J631.kc.i64`). Las bases de objeto observadas son `pm = *(this + 0x18)` (controlador base `IO80211Controller`/`IO80211PeerManager`) y `awdlIvars = *(this + 0x20)` (gestor específico `IO80211AWDLPeerManager`).

---

## 1. Resumen ejecutivo

El modelo de consulta de infraestructura y construcción de la secuencia de canales de 16 ranuras es arquitectónicamente idéntico en macOS 14.8.9 (23J631), macOS 15.8.2 (24H232) y macOS 26.7.1 (25G241):
1. **Modelo de tiro invariante**: El gestor nunca recibe el canal de infraestructura mediante *push*; lo consulta activamente mediante el token WCL IOUC `0xC02869C9` (tipo 4, 16 bytes) llamando a `IO80211Controller::getInfraChannel` / `apple80211getCHANNEL`.
2. **Tokens e ioctls idénticos**: Todos los números mágicos e inmediatos de 64 bits son 100% idénticos en las tres versiones: token de consulta WCL `0xC02869C9`, token de enrutamiento interno `0x802869C8`, petición 129 (`AWDL_SYNC_CHANNEL_SEQUENCE`, `0x81802869C8`), petición 132 (`AWDL_MASTER_CHANNEL`, `0x84802869C8`), petición 205 (`AWDL_SECONDARY_MASTER_CHANNEL`, `0xCD802869C8`), petición 116 (`AWDL_SYNC_PARAMETERS`, `0x74802869C8`), petición 117 (`AWDL_SYNC_ENABLED`, `0x75802869C8`) y petición 341 (`GET awdl_cap`, `0x155C02869C9`).
3. **Payload 129 de 400 bytes (`0x190`)**: Estructura de cabecera y 16 ranuras de 12 bytes idéntica, con supresión de cambios mediante `memcmp(v6, prev_seq, 0x190)`.
4. **Variaciones de layout interno**: Los campos internos de `pm` y `awdlIvars` experimentan desplazamientos sistemáticos entre compilaciones:
   - En `pm`: los campos sufren desplazamientos de `-0x10` a `-0x38` bytes en 15.8.2 y de `-0x12` a `-0x40` bytes en 14.8.9 respecto a 26.7.1.
   - En `awdlIvars`: la zona de canales maestros y sociales se desplaza `+0x08` bytes en 15.8.2 y `-0x38` bytes en 14.8.9. El buffer de 16 ranuras reside en `+0xA774` (26.7.1), `+0x8054` (15.8.2) y `+0x8004` (14.8.9).
5. **Diferencias funcionales concretas**:
   - **Gate de doble banda (`configureValidAWDLChannels`)**: En 14.8.9 el bloque de capacidades de tarjeta reside embebido localmente en `pm + 0xB18` (evaluado en `[pm + 0xB1F] & 0x20`), sin existir `IO80211Controller::isCardCapabilitiesSet`. En 15.8.2 se accede mediante `*(ctrl + 0x120) + 0xB27 & 0x20`, y en 26.7.1 mediante `*(ctrl + 0x120) + 0xB37 & 0x20`.
   - **Símbolo estático `__ZL13_channel6Info`**: Presente en 26.7.1 (`0xffffff80023aaff0`) y 15.8.2 (`0xffffff8002317fc0`) con `{1, 6, 0xA}`. En 14.8.9 este símbolo está ausente y fue sustituido directamente por constantes inmediatas en código (`mov rax, 600000001h` y `mov dword ptr [...], 0Ah`).
   - **Tabla de tipos de ranura `__ZL20_channelSeqSlotTypes`**: Posee 54 filas (estados 0..53) en 14.8.9 y 15.8.2, frente a 56 filas en 26.7.1. El estado 38 (*Time Sync Retro*) fue introducido en macOS 26, provocando un desplazamiento de +1 en los índices de estado a partir de 38 (p. ej. *High Thruput* es estado 40 en 14/15 y 41 en 26; *AWDL Fast Discovery* es estado 44 en 14/15 y 45 en 26). Asimismo, el estado 55 (*RTG AirDrop*) fue añadido al final en macOS 26.
   - **Fila 4 (*Low Power*)**: Es la única fila distinta entre 14.8.9 y 15.8.2. En 14.8.9 las ranuras 0 y 9 utilizan los tipos 3 y 4 (`3 1 3 1 1 1 7 7 2 4 3 1 1 1 7 7`), mientras que en 15.8.2 y 26.7.1 evolucionaron a los tipos 9 y 7 (`9 1 3 1 1 1 7 7 2 7 3 1 1 1 7 7`), permitiendo inserción dinámica de infraestructura y fallback a canal 6.

---

## 2. Tabla comparativa de ivars observados

| Campo / Significado | Base | 25G241 (ref) | 23J631 (14.8.9) | 24H232 (15.8.2) | Delta 23 vs 25 | Delta 24 vs 25 |
|---|---|---|---|---|---|---|
| Puntero `pm` (`ctrl`) | `this` | `+0x18` | `+0x18` | `+0x18` | 0 | 0 |
| Puntero `awdlIvars` | `this` | `+0x20` | `+0x20` | `+0x20` | 0 | 0 |
| `channelConfigured` | `pm` | `+0x548` (1352) | `+0x530` (1328) | `+0x538` (1336) | -0x18 (-24 B) | -0x10 (-16 B) |
| `beacon_period` | `pm` | `+0x54C` (1356) | `+0x534` (1332) | `+0x53C` (1340) | -0x18 (-24 B) | -0x10 (-16 B) |
| `dtim_period` | `pm` | `+0x550` (1360) | `+0x538` (1336) | `+0x540` (1344) | -0x18 (-24 B) | -0x10 (-16 B) |
| `vifc` (interfaz virtual) | `pm` | `[pm+0x560]` | `[pm+0x548]` | `[pm+0x550]` | -0x18 (-24 B) | -0x10 (-16 B) |
| Flag sucio `countryCode` | `pm` | `+0x6F8` (1784) | `+0x6D8` (1752) | `+0x6E8` (1768) | -0x20 (-32 B) | -0x10 (-16 B) |
| Flag 6 GHz en `pm` | `pm` | `+0x72E` (1838) | `+0x6FE` (1790) | `+0x71E` (1822) | -0x30 (-48 B) | -0x10 (-16 B) |
| Bloque capacidades tarjeta | `pm`/`ctrl` | `*(ctrl+0x120)+0xB30` | `pm+0xB18` (embebido) | `*(ctrl+0x120)+0xB20` | n/a (local) | -0x10 (-16 B) |
| Bit doble banda (0x20) | `pm`/`ctrl` | `*(ctrl+0x120)+0xB37` | `pm+0xB1F` | `*(ctrl+0x120)+0xB27` | n/a (local) | -0x10 (-16 B) |
| BSSID data | `pm` | `+0x930` (2352) | `+0x91E` (2334) | `+0x920` (2336) | -0x12 (-18 B) | -0x10 (-16 B) |
| SSID length | `pm` | `+0x938` (2360) | `+0x924` (2340) | `+0x928` (2344) | -0x14 (-20 B) | -0x10 (-16 B) |
| Caché `countryCode` | `pm` | `+0x9A0` (2464) | `+0x968` (2408) | `+0x968` (2408) | -0x38 (-56 B) | -0x38 (-56 B) |
| Infra canal info `{v, ch, fl}` | `pm` | `+0x9BC/0x9C0/0x9C4` | `+0x97C/0x980/0x984` | `+0x984/0x988/0x98C` | -0x40 (-64 B) | -0x38 (-56 B) |
| Canal AWDL actual `{v, ch, fl}` | `awdlIvars` | `+0x1A0/0x1A4/0x1A8` | `+0x1A0/0x1A4/0x1A8` | `+0x1A0/0x1A4/0x1A8` | 0 | 0 |
| Timer / re-arm store | `awdlIvars` | `+0x1D8` (472) | `+0x1D8` (472) | `+0x1D8` (472) | 0 | 0 |
| Gate ioctl `+0x300 == 1` | `awdlIvars` | `+0x300` (768) | `+0x300` (768) | `+0x300` (768) | 0 | 0 |
| `scheduleState` | `awdlIvars` | `+0x28E4` (10468) | `+0x28B4` (10420) | `+0x28CC` (10444) | -0x30 (-48 B) | -0x18 (-24 B) |
| Mirror `chanSwitchCapable` | `awdlIvars` | `+0x3ECC` (16076) | `+0x3E9C` (16028) | `+0x3EB4` (16052) | -0x30 (-48 B) | -0x18 (-24 B) |
| Infra DFS / allowed bytes | `awdlIvars` | `+0x3ED8` / `+0x3ED9` | `+0x3EA4` / `+0x3EA5` | `+0x3EC0` / `+0x3EC1` | -0x34 (-52 B) | -0x18 (-24 B) |
| Gate sync off (bit 1) | `awdlIvars` | `+0x3EE0 bit 1` | `+0x3EC8 bit 1` | `+0x3EE8 bit 1` | -0x18 (-24 B) | +0x08 (+8 B) |
| Link-down gate | `awdlIvars` | `+0x3EE4` (16100) | `+0x3ECC` (16076) | `+0x3EEC` (16108) | -0x18 (-24 B) | +0x08 (+8 B) |
| CarPlay active flag | `awdlIvars` | `+0x3F04` (16132) | `+0x3EE8` (16104) | `+0x3F0C` (16140) | -0x1C (-28 B) | +0x08 (+8 B) |
| Peer derive-to-follow | `awdlIvars` | `+0x3F60` / `+0x3F78` | `+0x3F60` | `+0x3F68` / `+0x3F80` | variable | +0x08 (+8 B) |
| `chanSwitchCapable` | `awdlIvars` | `+0x3FA1` (16289) | `+0x3F89` (16265) | `+0x3FA9` (16297) | -0x18 (-24 B) | +0x08 (+8 B) |
| Mirror 2 `chanSwitchCapable` | `awdlIvars` | `+0x3FA8` (16296) | `+0x3F90` (16272) | `+0x3FB0` (16304) | -0x18 (-24 B) | +0x08 (+8 B) |
| Restore infra guardado | `awdlIvars` | `+0x40F5` / `+0x40F8` | `+0x40C5` / `+0x40C8` | `+0x40FD` / `+0x4100` | -0x30 (-48 B) | +0x08 (+8 B) |
| Canales sociales `{6, 149, 44}` | `awdlIvars` | `+0x4290/94/98` | `+0x4258/5C/60` | `+0x4298/9C/A0` | -0x38 (-56 B) | +0x08 (+8 B) |
| Máscara sociales (u16) | `awdlIvars` | `+0x429C` (17052) | `+0x4264` (17008) | `+0x42A4` (17060) | -0x38 (-56 B) | +0x08 (+8 B) |
| Master primario `{v, ch, fl}` | `awdlIvars` | `+0x42A0/A4/A8` | `+0x4268/6C/70` | `+0x42A8/AC/B0` | -0x38 (-56 B) | +0x08 (+8 B) |
| Master secundario `{v, ch, fl}` | `awdlIvars` | `+0x42AC/B0/B4` | `+0x4274/78/7C` | `+0x42B4/B8/BC` | -0x38 (-56 B) | +0x08 (+8 B) |
| Destino lista de canales | `awdlIvars` | `+0x42D4` (17108) | `+0x429C` (17052) | `+0x42DC` (17116) | -0x38 (-56 B) | +0x08 (+8 B) |
| `numChanSpecs` (u16) | `awdlIvars` | `+0x42E0` (17120) | `+0x42A8` (17064) | `+0x42E8` (17128) | -0x38 (-56 B) | +0x08 (+8 B) |
| Umbral inactividad (2000 ms) | `awdlIvars` | `+0x9144` (37188) | `+0x69E8` (27112) | `+0x6A2C` (27180) | -0x275C | -0x2718 |
| Command queue / `lockMap` | `awdlIvars` | `+0xA5C8` (42440) | `+0x7E60` (32352) | `+0x7EA8` (32424) | -0x2768 | -0x2720 |
| `airplaySink` | `awdlIvars` | `+0xA648` (42568) | `+0x7EE0` (32480) | `+0x7F28` (32552) | -0x2768 | -0x2720 |
| `airPlayServices` | `awdlIvars` | `+0xA650` (42576) | `+0x7EE8` (32488) | `+0x7F30` (32560) | -0x2768 | -0x2720 |
| `rdLink` / `companionLink` | `awdlIvars` | `+0xA654` / `+0xA658` | `+0x7EEC` / `+0x7EF0` | `+0x7F34` / `+0x7F38` | -0x2768 | -0x2720 |
| `nonAirPlayServices` | `awdlIvars` | `+0xA65C` (42588) | `+0x7EF4` (32496) | `+0x7F3C` (32572) | -0x2768 | -0x2720 |
| `airplaySoloModeEnabled` | `awdlIvars` | `+0xA6A4` (42660) | `+0x7F34` (32564) | `+0x7F84` (32644) | -0x2770 | -0x2720 |
| Byte encoding de ranuras | `awdlIvars` | `+0xA772` (42866) | `+0x8002` (32770) | `+0x8052` (32850) | -0x2770 | -0x2720 |
| Buffer ranuras secuencia (129) | `awdlIvars` | `+0xA774` (42868) | `+0x8004` (32772) | `+0x8054` (32852) | -0x2770 | -0x2720 |
| Respaldo secuencia previa | `awdlIvars` | `+0xA8F4` (43252) | `+0x8184` (33156) | `+0x81D4` (33236) | -0x2770 | -0x2720 |
| `dynSdbForceChanSeq` | `awdlIvars` | `+0xAA74` (43636) | `+0x8304` (33540) | `+0x8354` (33620) | -0x2770 | -0x2720 |
| `wifidOnly` | `awdlIvars` | `+0xF638` (63032) | `+0xC9F8` (51704) | `+0xBEB8` (48824) | -0x2C40 | -0x3780 |
| Bit assisted discovery | `awdlIvars` | `+0xFA3A bit 0` | `+0x7FB8 bit 0` | `+0xC2BA bit 0` | variable | -0x3780 |

---

## 3. §3: GET del canal de infraestructura

| Item | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Nota |
|---|---|---|---|---|---|
| A1 `getInfrastructureState` | VA `0xffffff80021da84a`<br>Callers: `0x21d9030` (`interfaceStateChange`), `0x21de938` (`reportDataPathEvents`) | VA `0xffffff80020ca152`<br>Callers: `0x20c890c` (`interfaceStateChange`), `0x20cd6e6` (`reportDataPathEvents`) | VA `0xffffff8002178616`<br>Callers: `0x2176e4d` (`interfaceStateChange`), `0x217bb69` (`reportDataPathEvents`) | no | Mismos llamadores e idéntico flujo de consulta; difieren los offsets de destino en `pm` y `awdlIvars`. |
| A1.1 WCL IOUC `getInfraChannel` | Token `0xC02869C9`<br>Tipo 4, 16 B<br>Call `0x21da898`<br>Bridge `0x229520e` | Token `0xC02869C9`<br>Tipo 4, 16 B<br>Call `0x20ca1bd`<br>Bridge `0x216ca92` | Token `0xC02869C9`<br>Tipo 4, 16 B<br>Call `0x2178664`<br>Bridge `0x222795c` | si | Token `0xC02869C9`, tipo 4, búfer de 16 B y método de fallback `apple80211getCHANNEL` idénticos. |
| A1.2 `getBSSIDData` | Tipo 9, 12 B<br>Call `0x21da98f`<br>Destino `pm+0x930` | Tipo 9, 12 B<br>Call `0x20ca30d`<br>Destino `pm+0x91E` | Tipo 9, 12 B<br>Call `0x2178730`<br>Destino `pm+0x920` | no | Consulta tipo 9 idéntica; almacén desplazado en `pm` (`+0x91E` en 14, `+0x920` en 15 vs `+0x930` en 26). |
| A1.3 `getSSIDData` | Tipo 1, 40 B (0x28)<br>Call `0x21da9e5`<br>Longitud `pm+0x938` | Tipo 1, 40 B<br>Call `0x20ca37f`<br>Longitud `pm+0x924` | Tipo 1, 40 B<br>Call `0x2178786`<br>Longitud `pm+0x928` | no | Consulta tipo 1 idéntica; longitud en `pm+0x924` (14) y `pm+0x928` (15) vs `pm+0x938` (26). |
| A1.4 `getBeaconPeriod` & `getDTIMInt` | Beacon tipo 0xA6 (8 B) en `pm+0x54C`<br>DTIM tipo 0x29 (8 B) en `pm+0x550` | Beacon tipo 0xA6 en `pm+0x534`<br>DTIM tipo 0x29 en `pm+0x538` | Beacon tipo 0xA6 en `pm+0x53C`<br>DTIM tipo 0x29 en `pm+0x540` | no | Consultas WCL idénticas; campos de `pm` desplazados `-0x18` en 14 y `-0x10` en 15 respecto a 26. |
| A1.5 Bloque compare/store infra | Almacén `pm+0x9BC/0x9C0/0x9C4`<br>Máscara cambio banda: `0x2018`<br>Restore: `+0x40F5/+0x40F8` | Almacén `pm+0x97C/0x980/0x984`<br>Máscara cambio banda: `0x2018`<br>Restore: `+0x40C5/+0x40C8` | Almacén `pm+0x984/0x988/0x98C`<br>Máscara cambio banda: `0x2018`<br>Restore: `+0x40FD/+0x4100` | no | Máscara `0x2018` y lógica idénticas; campos desplazados `-0x40` en 14 y `-0x38` en 15. |
| A2 `interfaceStateChange` | VA `0xffffff80021d8cc8`<br>vtable `0x245a868` slot 55 (+0x1B8)<br>Celda `0x245aa20` | VA `0xffffff80020c85a2`<br>vtable `0x22ca618` slot 55 (+0x1B8)<br>Celda `0x22ca7d0` | VA `0xffffff8002176af0`<br>vtable `0x23ca1e8` slot 55 (+0x1B8)<br>Celda `0x23ca3a0` | no | Slot virtual 55 (+0x1B8) idéntico; VAs de función y celdas difieren por compilación. |
| A2.1 Gate link-down | Gate `awdlIvars+0x3EE4 != 0`<br>vifc `[pm+0x560]`<br>vtable slot `+0xB88`, `cmp eax, 1`<br>Check en `0x21d8ea1` | Gate `awdlIvars+0x3ECC != 0`<br>vifc `[pm+0x548]`<br>vtable slot `+0xB88`, `cmp eax, 1`<br>Check en `0x20c8773` | Gate `awdlIvars+0x3EEC != 0`<br>vifc `[pm+0x550]`<br>vtable slot `+0xB88`, `cmp eax, 1`<br>Check en `0x2176cc1` | no | Mismo descarte con log `"Interface link is down, nothing to do"`. Confirma `AWDL_DATA_PATH_PORT_14_15.md` §5.3. |
| A3 `reportDataPathEvents` | VA `0xffffff80021dd8f4`<br>Despacha Sources: 9, 11, 5, 6<br>Llama `getInfrastructureState` | VA `0xffffff80020ccaaa`<br>Despacha Sources: 9, 11, 5, 6<br>Llama `getInfrastructureState` | VA `0xffffff800217adb8`<br>Despacha Sources: 9, 11, 5, 6<br>Llama `getInfrastructureState` | si | Conjunto idéntico de fuentes {9, 11, 5, 6} y refresco de infraestructura previo a Source 5 en `0x20cd6e6` (14) y `0x217bb69` (15). |
| A4 Setters de infra muertos | `setInfraChannel` (`0x2254aee`)<br>`setInfraChannelInfo` (`0x225edec`)<br>`setInfraChannelSpec` (`0x21fc626`)<br>`vif::setInfraChannel` (`0x2330a0c`)<br>0 xrefs | `setInfraChannel` (`0x213abc2`)<br>`setInfraChannelInfo` (`0x2144daa`)<br>`setInfraChannelSpec` (`0x20e8a68`)<br>`vif::setInfraChannel` (`0x21d90e6`)<br>0 xrefs | `setInfraChannel` (`0x21ea73c`)<br>`setInfraChannelInfo` (`0x21f45fc`)<br>`setInfraChannelSpec` (`0x2198112`)<br>`vif::setInfraChannel` (`0x22a64c0`)<br>0 xrefs | si | En las tres versiones existen los cuatro símbolos como stubs muertos sin referencias activas ni presencia en vtables. |
| A5 Tabla `StateChangedSource` | VA `0xffffff800245d450`<br>15 cadenas: 0 Init, 1 BSSIDChanged, 2 Mdns, ..., 14 AWDL exiting | VA `0xffffff80022ccb10`<br>15 cadenas en idéntico orden y semántica | VA `0xffffff80023ccc28`<br>15 cadenas en idéntico orden y semántica | si | Tabla de 15 cadenas de diagnóstico para transiciones en `interfaceStateChange` idéntica. |

---

## 4. §4: Estrategia y buffers maestros

| Item | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Nota |
|---|---|---|---|---|---|
| B1 `configureValidAWDLChannels` | VA `0xffffff80021caa40`<br>Callers: `0x21c5b9b`, `0x21d9996`<br>Seguida por update6G, find6G, selectChannels | VA `0xffffff80020bb342`<br>Callers: `0x20b5be8`, `0x20c923a`<br>Seguida por update6G, find6G, selectChannels | VA `0xffffff8002168f52`<br>Callers: `0x2164300`, `0x21776cd`<br>Seguida por update6G, find6G, selectChannels | si | Misma topología de llamadas desde `configure` e `interfaceStateChange` y misma secuencia posterior. |
| B2 Gate doble banda | Check: `0x21cab1e`<br>`*(ctrl+0x120)+0xB37 & 0x20`<br>Pone a cero qword `+0x4294` | Check: `0x20bb433`<br>`[pm+0xB1F] & 0x20`<br>Pone a cero qword `+0x425C` | Check: `0x2169030`<br>`*(ctrl+0x120)+0xB27 & 0x20`<br>Pone a cero qword `+0x429C` | no | En 14.8.9 el bloque reside en `pm+0xB18` (sin indirección ni `isCardCapabilitiesSet`). En 15.8.2 el offset es `0xB27`. Ambos limpian 149 y 44. |
| B3 `getCountryCode` | VA `0xffffff8002295416`<br>Caché `pm+0x9A0/0x9A4`<br>Dirty flag `pm+0x6F8` | VA `0xffffff800216cc1e`<br>Caché `pm+0x968/0x96C`<br>Dirty flag `pm+0x6D8` | VA `0xffffff8002227b64`<br>Caché `pm+0x968/0x96C`<br>Dirty flag `pm+0x6E8` | no | Misma lógica de caché en `pm`; campos desplazados. |
| B4 Ioctl lista canales (207/379) | Token `0xC02869C9`<br>Req 207 (379 si `pm+0x72E != 0`)<br>Destino `+0x42D4`, u16 `+0x42E0` | Token `0xC02869C9`<br>Req 207 (379 si `pm+0x6FE != 0`)<br>Destino `+0x429C`, u16 `+0x42A8` | Token `0xC02869C9`<br>Req 207 (379 si `pm+0x71E != 0`)<br>Destino `+0x42DC`, u16 `+0x42E8` | no | Búfer destino desplazado; tokens 207/379, tamaño bzero (`0x2720`) y preservación de defaults (149/44) en fallo idénticos. |
| B5 `isValidChannel` (5-arg) | VA `0xffffff80021d1d4c`<br>Bandas 8 y 16 (36..183)<br>Tabla ETSI `0x2462350` (39 países)<br>Regla "IL", bits 0x1..0x100 | VA `0xffffff80020c2104`<br>Bandas 8 y 16 (36..183)<br>Tabla ETSI `0x22d0640` (39 países)<br>Regla "IL", bits 0x1..0x100 | VA `0xffffff800216ffb4`<br>Bandas 8 y 16 (36..183)<br>Tabla ETSI `0x23d1900` (39 países)<br>Regla "IL", bits 0x1..0x100 | si | Filtrado regulatorio, 39 entradas ETSI, regla de restricción para Israel y bits devueltos 100% idénticos. |
| B6 Canales sociales guardados | 2G (6) en `+0x4290`<br>5G (149) en `+0x4294`<br>5G Alt (44) en `+0x4298` | 2G (6) en `+0x4258`<br>5G (149) en `+0x425C`<br>5G Alt (44) en `+0x4260` | 2G (6) en `+0x4298`<br>5G (149) en `+0x429C`<br>5G Alt (44) en `+0x42A0` | no | Desplazamientos: `-0x38` en 14, `+0x08` en 15 respecto a 26. Mismos valores por defecto (6, 149, 44) y validación. |
| B7 Byte encoding de ranuras | Condición `caps & 0x38` y `dynSdbForceChanSeq` (`+0xAA74 != 1`)<br>Escribe 1 en `+0xA772` | Condición `caps & 0x38` y `dynSdbForceChanSeq` (`+0x8304 != 1`)<br>Escribe 1 en `+0x8002` | Condición `caps & 0x38` y `dynSdbForceChanSeq` (`+0x8354 != 1`)<br>Escribe 1 en `+0x8052` | no | Condición binaria idéntica; campos desplazados en `awdlIvars`. |
| B8 Bytes infra DFS / allowed | Word default `0x0100`<br>`+0x3ED8` (DFS), `+0x3ED9` (allowed)<br>Máscara DFS 1, flags `0x2018` | Word default `0x0100`<br>`+0x3EA4` (DFS), `+0x3EA5` (allowed)<br>Máscara DFS 1, flags `0x2018` | Word default `0x0100`<br>`+0x3EC0` (DFS), `+0x3EC1` (allowed)<br>Máscara DFS 1, flags `0x2018` | no | Validación en dos pasos idéntica; word default `0x0100` emitido en los tres casos. |
| B9 `initParamsToDefaults` | VA `0xffffff80021c7a40`<br>Qword `0x9500000006` en `+0x4290`<br>44 en `+0x4298`, flags 8 en `+0x42A8`<br>Idle 2000 en `+0x9144`<br>Mirror switch en `+0x3FA8` | VA `0xffffff80020b7950`<br>Qword `0x9500000006` en `+0x4258`<br>44 en `+0x4260`, flags 8 en `+0x4270`<br>Idle 2000 en `+0x69E8`<br>Mirror switch en `+0x3F90` | VA `0xffffff8002165fdc`<br>Qword `0x9500000006` en `+0x4298`<br>44 en `+0x42A0`, flags 8 en `+0x42B0`<br>Idle 2000 en `+0x6A2C`<br>Mirror switch en `+0x3FB0` | no | Siembra idéntica de canales maestros (6, 149, 44, flags 8) y umbral de inactividad de 2000 ms. |
| B10 `_zeroChanInfo` / `_channel6Info` | `_zeroChanInfo` `0x2488a88` `{1,0,0}`<br>`_channel6Info` `0x23aaff0` `{1,6,0xA}` | `_zeroChanInfo` `0x22ee6a8` `{1,0,0}`<br>`_channel6Info`: **AUSENTE** | `_zeroChanInfo` `0x23f66f8` `{1,0,0}`<br>`_channel6Info` `0x2317fc0` `{1,6,0xA}` | no | En 14.8.9 no existe símbolo estático para canal 6; el compilador emitió inmediatos `0x600000001` y `0xA` en el código. |
| C1 `selectChannelsToUseForAWDL` | VA `0xffffff80021dc2f2`<br>Primario `+0x42A0..+0x42A8`<br>Secundario `+0x42AC..+0x42B4`<br>Máscara u16 `+0x429C`<br>Actual `+0x1A0`, re-arm `+0x1D8` | VA `0xffffff80020cb4c8`<br>Primario `+0x4268..+0x4270`<br>Secundario `+0x4274..+0x427C`<br>Máscara u16 `+0x4264`<br>Actual `+0x1A0`, re-arm `+0x1D8` | VA `0xffffff800217984e`<br>Primario `+0x42A8..+0x42B0`<br>Secundario `+0x42B4..+0x42BC`<br>Máscara u16 `+0x42A4`<br>Actual `+0x1A0`, re-arm `+0x1D8` | no | Reglas de bits en máscara (bit0 master, bit1 44, bit2 149) y offsets `+0x1A0` / `+0x1D8` idénticos. |
| C2 `setAwdlMasterChannel` (132/205) | VAs `0x221d4de` / `0x221d314`<br>Gate `awdlIvars+0x300 == 1`<br>Token `0x802869C8` con 132 y 205 | VAs `0x2107326` / `0x2107178`<br>Gate `awdlIvars+0x300 == 1`<br>Token `0x802869C8` con 132 y 205 | VAs `0x21b7466` / `0x21b72b8`<br>Gate `awdlIvars+0x300 == 1`<br>Token `0x802869C8` con 132 y 205 | si | Gate `+0x300 == 1`, token RouteInternal `0x802869C8` y números de ioctl interno 132 y 205 idénticos. |
| C3 Modo 0 en `monitorAWDLState` | Llama `setStrategy(0, 5, false)`<br>Copia infra a primario y 0 a secundario<br>Llama `programMasterChannels` y `setScheduleState(state, 0x14, true)` | Llama `setStrategy(0, 5, false)`<br>Copia infra a primario y 0 a secundario<br>Llama `programMasterChannels` y `setScheduleState(state, 0x14, true)` | Llama `setStrategy(0, 5, false)`<br>Copia infra a primario y 0 a secundario<br>Llama `programMasterChannels` y `setScheduleState(state, 0x14, true)` | si | Mismo comportamiento de sustitución condicional hacia `FollowsInfra` ante cuatro condiciones de negocio. |
| C4 Modo 0 en `interfaceStateChange` | Bloque `0x21d9a44 - 0x21d9aa4`<br>Copia infra si `strategy == 0` | Bloque `0x20c9324`<br>Copia infra si `strategy == 0` | Bloque `0x2177785`<br>Copia infra si `strategy == 0` | si | Mismo bloque de sustitución ejecutado tras `selectChannelsToUseForAWDL`. |
| C5 5 condiciones retorno modo 0 | `airplaySink != 0` (`+0xA648`)<br>`soloMode == 0` (`+0xA6A4`)<br>`strategy == 1`<br>`nonAirPlay == 0 && airPlay != 0`<br>Inactividad vs 2000 (`+0x9144`) | `airplaySink != 0` (`+0x7EE0`)<br>`soloMode == 0` (`+0x7F34`)<br>`strategy == 1`<br>`nonAirPlay == 0 && airPlay != 0`<br>Inactividad vs 2000 (`+0x69E8`) | `airplaySink != 0` (`+0x7F28`)<br>`soloMode == 0` (`+0x7F84`)<br>`strategy == 1`<br>`nonAirPlay == 0 && airPlay != 0`<br>Inactividad vs 2000 (`+0x6A2C`) | no | Las cinco condiciones lógicas son idénticas; campos desplazados. |
| C6 Elección estrategia en `interfaceStateChange` | Evalúa `airplaySink` (`+0xA648`), `chanSwitchCapable` (`+0x3FA1`), `soloMode` (`+0xA6A4`), `nonAirPlay` (`+0xA65C`)<br>Llama `setStrategy` (reason 4)<br>Escritor único `+0x3FA1=1` en `configure` | Evalúa `airplaySink` (`+0x7EE0`), `chanSwitchCapable` (`+0x3F89`), `soloMode` (`+0x7F34`), `nonAirPlay` (`+0x7EF4`)<br>Llama `setStrategy` (reason 4)<br>Escritor único `+0x3F89=1` en `configure` | Evalúa `airplaySink` (`+0x7F28`), `chanSwitchCapable` (`+0x3FA9`), `soloMode` (`+0x7F84`), `nonAirPlay` (`+0x7F3C`)<br>Llama `setStrategy` (reason 4)<br>Escritor único `+0x3FA9=1` en `configure` | no | Misma bifurcación hacia `Solo mode` y único punto de escritura en `configure`. |

---

## 5. §5: Construcción de ranuras y emisión 129/132/205

| Item | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Nota |
|---|---|---|---|---|---|
| C7 Estado de calendario | Token 116 `0x74802869C8`<br>`channelConfigured` en `pm+0x548`<br>Sync off gate `+0x3EE0 bit 1`<br>Estado 0/3/5<br>`enableAWDL` no toca estrategia | Token 116 `0x74802869C8`<br>`channelConfigured` en `pm+0x530`<br>Sync off gate `+0x3EC8 bit 1`<br>Estado 0/3/5<br>`enableAWDL` no toca estrategia | Token 116 `0x74802869C8`<br>`channelConfigured` en `pm+0x538`<br>Sync off gate `+0x3EE8 bit 1`<br>Estado 0/3/5<br>`enableAWDL` no toca estrategia | no | Token 116, token 117 (`0x75802869C8`), GET 341 (`0x155C02869C9`) y lógica de estado idénticos. |
| C8 Contadores de servicios | `getvalidAWDLServices` (`0x21da6de`)<br>Contadores `+0xA650..+0xA65C`<br>Callback `_countAirPlayServices`<br>Strings "ip6","arpa","airplay","raop"<br>CarPlay flag `+0x3F04 bit 0` | `getvalidAWDLServices` (`0x20c9fe6`)<br>Contadores `+0x7EE8..+0x7EF4`<br>Callback `_countAirPlayServices`<br>Strings "ip6","arpa","airplay","raop"<br>CarPlay flag `+0x3EE8 bit 0` | `getvalidAWDLServices` (`0x21784aa`)<br>Contadores `+0x7F30..+0x7F3C`<br>Callback `_countAirPlayServices`<br>Strings "ip6","arpa","airplay","raop"<br>CarPlay flag `+0x3F0C bit 0` | no | Regla de conteo, filtrado por tipo `0x21/0x0C` y bypass por CarPlay idénticos. |
| D1 `setChannelSequenceByState` | VA `0xffffff80021f53aa`<br>Tabla `0x23aa1f0` (56 filas)<br>Switch 21 casos sin bias (`0x21f5498`) | VA `0xffffff80020e1648`<br>Tabla `0x223d1d0` (54 filas)<br>Switch 21 casos sin bias (`0x20e1730`) | VA `0xffffff8002190fca`<br>Tabla `0x2317240` (54 filas)<br>Switch 21 casos sin bias (`0x21910b2`) | no | Switch de 21 casos sin sesgo idéntico. Tabla con 54 filas en 14/15 vs 56 en 26. |
| D2 Tipos de ranura 1, 2, 3, 4 | Tipo 1: `_zeroChanInfo`<br>Tipo 2: Primario `+0x42A0`<br>Tipo 3: Secundario `+0x42AC` (o prim)<br>Tipo 4: Actual `+0x1A0`<br>Guard 6G en `+0xA77D bit 0x20` | Tipo 1: `_zeroChanInfo`<br>Tipo 2: Primario `+0x4268`<br>Tipo 3: Secundario `+0x4274` (o prim)<br>Tipo 4: Actual `+0x1A0`<br>Guard 6G en `+0x800D bit 0x20` | Tipo 1: `_zeroChanInfo`<br>Tipo 2: Primario `+0x42A8`<br>Tipo 3: Secundario `+0x42B4` (o prim)<br>Tipo 4: Actual `+0x1A0`<br>Guard 6G en `+0x805D bit 0x20` | no | Semántica idéntica de selección de canal; canal actual conservado en `+0x1A0`. |
| D3 Tipo de ranura 7 | VA `0x21f5ef6`<br>3 condiciones: `infraAllowed`, sin flag `0x2000`, AirPlay count > 0 | VA `0x20e22ac`<br>3 condiciones: `infraAllowed`, sin flag `0x2000`, AirPlay count > 0 | VA `0x2191ca4`<br>3 condiciones: `infraAllowed`, sin flag `0x2000`, AirPlay count > 0 | si | Misma evaluación de las 3 condiciones en `addInfraToMinimumChannelSequence`. Si fallan, emite `_zeroChanInfo`. |
| D4 Tipo de ranura 8 | Gate `airplaySink != 0` en `+0xA648`<br>Carga infra o `_zeroChanInfo` | Gate `airplaySink != 0` en `+0x7EE0`<br>Carga infra o `_zeroChanInfo` | Gate `airplaySink != 0` en `+0x7F28`<br>Carga infra o `_zeroChanInfo` | no | Gate de AirPlay Sink idéntico; ivar desplazado. |
| D5 Tipo de ranura 9 | Infra si permitida; sino ranura 0 `{1,6,0xA}` (`_channel6Info`); resto canal 6 si SDB en `{45,47,48}` | Infra si permitida; sino ranura 0 `{1,6,0xA}` (inmediatos); resto canal 6 si SDB en `{44,46,47}` | Infra si permitida; sino ranura 0 `{1,6,0xA}` (`_channel6Info`); resto canal 6 si SDB en `{44,46,47}` | no | En 14.8.9 constantes inmediatas en código. Máscara SDB `{44,46,47}` en 14/15 vs `{45,47,48}` en 26 por el desfase de estados. |
| D6 `followPeerChannelSequence` | VA `0x21f6e74`<br>Copia de `peer+920` a `+0xA774`<br>Salva previa en `+0xA8F4`<br>Estado 9, 129 forzado<br>Gate `+0x3F78 bit 0` | VA `0x20e3094`<br>Copia de `peer+920` a `+0x8004`<br>Salva previa en `+0x8184`<br>Estado 9, 129 forzado<br>Gate `+0x3F60` | VA `0x2192a32`<br>Copia de `peer+920` a `+0x8054`<br>Salva previa en `+0x81D4`<br>Estado 9, 129 forzado<br>Gate `+0x3F80` | no | Adopción de la secuencia peer de 192 B idéntica; buffers desplazados en `awdlIvars`. |
| E1 `setChannelSequenceUnconditional` (129) | VA `0xffffff80021cb69a`<br>Sitio payload `0x21cb710`<br>Búfer 400 B (`0x190`)<br>Memcmp vs previa `+0xA8F4`<br>Token 129 `0x81802869C8`<br>Mensaje 195 | VA `0xffffff80020bbee6`<br>Sitio payload `0x20bbf46`<br>Búfer 400 B (`0x190`)<br>Memcmp vs previa `+0x8184`<br>Token 129 `0x81802869C8`<br>Mensaje 195 | VA `0xffffff8002169b06`<br>Sitio payload `0x2169b74`<br>Búfer 400 B (`0x190`)<br>Memcmp vs previa `+0x81D4`<br>Token 129 `0x81802869C8`<br>Mensaje 195 | no | Estructura de payload de 400 B, supresión `memcmp`, token `0x81802869C8` y mensaje 195 idénticos. |
| E2 `programMasterChannels` (132/205) | VA `0xffffff80021d494a`<br>Token 132 `0x84802869C8` (`+0x42A4`)<br>Token 205 `0xCD802869C8` (`+0x42B0`) | VA `0xffffff80020c44a8`<br>Token 132 `0x84802869C8` (`+0x426C`)<br>Token 205 `0xCD802869C8` (`+0x4278`) | VA `0xffffff8002172800`<br>Token 132 `0x84802869C8` (`+0x42AC`)<br>Token 205 `0xCD802869C8` (`+0x42B8`) | no | Tokens inmediatos 132 y 205 idénticos; leídos de los ivars maestros correspondientes. |
| E3 `setAwdlChannelSequence` (userspace) | VA `0xffffff800221cedc`<br>Rango n 1..32, filtro DFS<br>Token RouteInternal + 129<br>Estrategia 3 (reason 3, force true) | VA `0xffffff8002106db4`<br>Rango n 1..32, filtro DFS<br>Token RouteInternal + 129<br>Estrategia 3 (reason 3, force true) | VA `0xffffff80021b6ece`<br>Rango n 1..32, filtro DFS<br>Token RouteInternal + 129<br>Estrategia 3 (reason 3, force true) | si | Validación de rango hasta 32 canales, ioctl interno 129 y fijación forzada de estrategia 3 idénticos. |
| E4 Callers de emisión 129 | `configure`, `enableAWDL`, `setChannelSequence`, `followPeerChannelSequence`, `setRoamChannelSequence`, scan overlays | `configure`, `enableAWDL`, `setChannelSequence`, `followPeerChannelSequence`, `setRoamChannelSequence`, scan overlays | `configure`, `enableAWDL`, `setChannelSequence`, `followPeerChannelSequence`, `setRoamChannelSequence`, scan overlays | si | Puntos de emisión centralizados en `setChannelSequenceUnconditional` idénticos. |
| E5 Inventario tokens ioctl | Tokens: `0xC02869C9`, `0x802869C8`, `0x81802869C8`, `0x84802869C8`, `0xCD802869C8`, `0x74802869C8`, `0x75802869C8`, `0x155C02869C9`; reqs 207/379 | Mismos tokens inmediatos de 64 bits y números de request | Mismos tokens inmediatos de 64 bits y números de request | si | 100% idénticos en valor, representación y semántica en las tres versiones de macOS. |

---

## 6. §6: Escenario real y variantes

| Escenario / Comportamiento | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | same | Detalle técnico |
|---|---|---|---|---|---|
| Infraestructura en canal no DFS (p. ej. 153/80 MHz) | Genera 7 ranuras en 149 (flags `0x10`), hasta 4 en 153 (`0x410`), 1 en 6 (`0x8`) y resto en 0 | Mismo patrón de 16 ranuras | Mismo patrón de 16 ranuras | si | Con servicios AirPlay activos, coexisten primarios de 20 MHz (social 149) y de 80 MHz (infra 153). |
| Infraestructura en canal DFS (p. ej. 52..144) | `isValidChannel` marca bit 0x1 (radarDFS). Byte allowed se pone a 0. Ranuras 7 y 9 descartan infra. Log `"infra is on DFS"`. | Idéntico descarte de infra | Idéntico descarte de infra | si | El stack desactiva la inyección de infraestructura en las 16 ranuras y prohíbe el modo 0 (`FollowsInfra`). |
| Fallback a canal 6 en ranura 0 (sin infra) | Carga `{1, 6, 0xA}` desde `__ZL13_channel6Info` | Carga `{1, 6, 0xA}` desde inmediatos `0x600000001` y `0xA` | Carga `{1, 6, 0xA}` desde `__ZL13_channel6Info` | si | Semántica idéntica de asegurar canal social 2.4 GHz en ranura 0 si no hay infraestructura disponible. |
| Ranura 4 (*Low Power*) sin servicios | Emite tipo 9 en ranura 0 y tipo 7 en ranura 9 | Emite tipo 3 en ranura 0 y tipo 4 en ranura 9 | Emite tipo 9 en ranura 0 y tipo 7 en ranura 9 | no | En 14.8.9 usa canales sociales fijos; en 15.8.2 y 26.7.1 conmuta dinámicamente según estado de infra. |
| Supresión `memcmp(0x190)` de emisión 129 | Compara 400 bytes con buffer previo. Si no hay cambios, omite emisión ioctl y log. | Compara 400 bytes con buffer previo. | Compara 400 bytes con buffer previo. | si | Evita sobrecargar el bus USB/PCIe cuando la secuencia calculada no ha variado. |
| Presencia de par remoto (`followPeer`) | Sobrescribe buffer con secuencia del par (192 B) y fuerza estado 9 y emisión 129. | Idéntica adopción forzada | Idéntica adopción forzada | si | La secuencia local se subordina enteramente a la del dispositivo maestro del enlace directo. |

---

## 7. Tabla de tipos de ranura `__ZL20_channelSeqSlotTypes` y switch de estados

### 7.1 Dimensiones y símbolos

- **macOS 26.7.1 (25G241)**:
  - VA tabla: `0xffffff80023aa1f0`.
  - Tamaño: 56 filas de 16 dwords (64 bytes por fila = 3584 bytes = `0xE00`).
  - Delimitador superior: `__ZL13_channel6Info` en `0xffffff80023aaff0`.
  - Nombres de estados (`__ZL18scheduleStateNames`): VA `0xffffff8002459c20`.
- **macOS 15.8.2 (24H232)**:
  - VA tabla: `0xffffff8002317240`.
  - Tamaño: 54 filas de 16 dwords (64 bytes por fila = 3456 bytes = `0xD80`).
  - Delimitador superior: `__ZL13_channel6Info` en `0xffffff8002317fc0`.
  - Nombres de estados (`__ZL18scheduleStateNames`): VA `0xffffff80023c95e0`.
- **macOS 14.8.9 (23J631)**:
  - VA tabla: `0xffffff800223d1d0`.
  - Tamaño: 54 filas de 16 dwords (64 bytes por fila = 3456 bytes = `0xD80`).
  - Delimitador superior: `__ZL19_psfTansmissionMode` en `0xffffff800223df50` (`__ZL13_channel6Info` ausente).
  - Nombres de estados (`__ZL18scheduleStateNames`): VA `0xffffff80022c9a08`.

### 7.2 Jump table y ausencia de sesgo en `setChannelSequenceByState`

En las tres versiones analizadas, la selección de la acción a tomar por cada ranura se implementa mediante una instrucción indirecta `jmp rax` basada en una tabla de saltos (*jump table*) indexada directamente por el tipo de ranura:
- **25G241**: Switch en `0xffffff80021f5498`, tabla `jpt_FFFFFF80021F5498` en `0xffffff80021f5e90`.
- **24H232**: Switch en `0xffffff80021910b2`, tabla `jpt_FFFFFF80021910B2` en `0xffffff8002191c40`.
- **23J631**: Switch en `0xffffff80020e1730`, tabla `jpt_FFFFFF80020E1730` en `0xffffff80020e2248`.

En los tres casos:
- Número de casos: exactamente 21 casos (rango 0..20).
- Sesgo (*bias*): 0 (`lowcase = 0`). Las etiquetas del descompilador y las entradas de la jump table corresponden de forma directa e idéntica a los números de tipo de ranura (slot types 0 a 20).

### 7.3 Volcado comparativo de estados clave

A continuación se comparan las 16 ranuras de los estados representativos solicitados:

| Estado | Nombre de estado | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | ¿Coincide? |
|---|---|---|---|---|---|
| **0** | Discovery | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` | **si** (idéntica) |
| **1** | Discovery + Infra Stream | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | **si** (idéntica) |
| **3** | Idle | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | **si** (idéntica) |
| **4** | Low Power | `9 1 3 1 1 1 7 7 2 7 3 1 1 1 7 7` | `3 1 3 1 1 1 7 7 2 4 3 1 1 1 7 7` | `9 1 3 1 1 1 7 7 2 7 3 1 1 1 7 7` | **no** (ranuras 0 y 9) |
| **24** | Scan + Data | `3 3 3 3 3 1 1 1 2 4 3 4 4 1 1 1` | `3 3 3 3 3 1 1 1 2 4 3 4 4 1 1 1` | `3 3 3 3 3 1 1 1 2 4 3 4 4 1 1 1` | **si** (idéntica) |
| **31** | Infra BE RT + No Cached Peers + LP | `9 1 3 1 1 1 1 1 2 1 3 1 1 1 1 1` | `9 1 3 1 1 1 1 1 2 1 3 1 1 1 1 1` | `9 1 3 1 1 1 1 1 2 1 3 1 1 1 1 1` | **si** (idéntica) |
| **40** | High Thruput (14/15) / RtMode Start (26) | `0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0` | `4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4` | `4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4` | Desfase (+1 en 26) |
| **41** | High Thruput (26) | `4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4` | `12 12 12 12 5 5 5 5 12 ... 12` | `12 12 12 12 5 5 5 5 12 ... 12` | Desfase (+1 en 26) |
| **44 / 45** | AWDL Fast Discovery | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` (45) | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` (44) | `3 3 3 3 3 3 9 9 2 4 3 4 4 4 9 9` (44) | **si** (idéntica bajo mapeo) |

### 7.4 Análisis del desfase estructural por estados 38 y 55

La comparación automatizada byte a byte de las tablas de 54 filas (14.8.9 y 15.8.2) frente a la de 56 filas (26.7.1) revela:
1. **Filas 0 a 37**: Son 100% byte-idénticas entre 15.8.2 y 26.7.1. Entre 14.8.9 y 15.8.2 la única discrepancia en este bloque es la fila 4.
2. **Inserción de estado 38 (*Time Sync Retro*) en macOS 26**:
   - En macOS 26.7.1 el estado 38 corresponde a *Time Sync Retro* con la secuencia `3 1 1 1 3 1 1 1 3 1 1 1 3 1 1 1`.
   - Este estado no existe en macOS 14.8.9 ni en macOS 15.8.2.
   - Como consecuencia, todos los estados subsiguientes (38 a 53 en 14/15) están desplazados exactamente en +1 posición en macOS 26 (pasan a ser los estados 39 a 54).
3. **Equivalencia con desfase**:
   - Filas 38..53 de 15.8.2 coinciden al 100% (0 bytes de discrepancia) con las filas 39..54 de 26.7.1.
   - Filas 38..53 de 14.8.9 coinciden al 100% con las filas 38..53 de 15.8.2.
4. **Inserción de estado 55 (*RTG AirDrop*) en macOS 26**:
   - En macOS 26.7.1 se agregó al final el estado 55 (*RTG AirDrop*), inicializado con 16 ceros (`0 ×16`).

### 7.5 Discrepancia en la Fila 4 (*Low Power*)

La fila 4 es la única fila funcionalmente distinta entre macOS 14.8.9 y las versiones posteriores:
- **macOS 14.8.9 (23J631)**:
  `State 4:  3  1  3  1  1  1  7  7  2  4  3  1  1  1  7  7`
  - Ranura 0 es tipo 3 (canal secundario maestro 44/149 o primario).
  - Ranura 9 es tipo 4 (canal AWDL actual `+0x1A0`).
- **macOS 15.8.2 (24H232) y macOS 26.7.1 (25G241)**:
  `State 4:  9  1  3  1  1  1  7  7  2  7  3  1  1  1  7  7`
  - Ranura 0 se actualizó a tipo 9 (inyección de infraestructura si está permitida, con fallback a canal social 6).
  - Ranura 9 se actualizó a tipo 7 (inyección de infraestructura condicional a servicios AirPlay).

Esta modificación en macOS 15 otorga al estado *Low Power* la capacidad de mantener enlace dinámico con el canal de infraestructura asociado sin forzar la permanencia estática en el canal social secundario o actual.

### 7.6 Resolución de tipos de ranura en `setChannelSequenceByState`

| Tipo | Semántica de resolución | 25G241 (26.7.1) | 23J631 (14.8.9) | 24H232 (15.8.2) | ¿Coincide? |
|---|---|---|---|---|---|
| **0** | No-op / ranura deshabilitada | No escribe en ranura | No escribe en ranura | No escribe en ranura | **si** |
| **1** | Ranura apagada | Escribe `_zeroChanInfo` `{1, 0, 0}` | Escribe `_zeroChanInfo` `{1, 0, 0}` | Escribe `_zeroChanInfo` `{1, 0, 0}` | **si** |
| **2** | Primario maestro | Lee `+0x42A0` (canal) y `+0x42A8` (flags) | Lee `+0x4268` (canal) y `+0x4270` (flags) | Lee `+0x42A8` (canal) y `+0x42B0` (flags) | **si** (ivars desplazados) |
| **3** | Secundario maestro (fallback primario) | Lee secundario `+0x42AC`; si canal == 0, salta a primario | Lee secundario `+0x4274`; si canal == 0, salta a primario | Lee secundario `+0x42B4`; si canal == 0, salta a primario | **si** (ivars desplazados) |
| **4** | Canal AWDL actual | Lee canal actual en `+0x1A0` y flags en `+0x1A8` | Lee canal actual en `+0x1A0` y flags en `+0x1A8` | Lee canal actual en `+0x1A0` y flags en `+0x1A8` | **si** (mismo offset) |
| **7** | Infraestructura para servicios AirPlay | Llama a `addInfraToMinimumChannelSequence()`. Si true, copia infra `pm`; si false, escribe `_zeroChanInfo`. | Mismo despacho y evaluación de 3 condiciones | Mismo despacho y evaluación de 3 condiciones | **si** |
| **8** | AirPlay Sink | Si `airplaySink != 0` (`+0xA648`), usa infra; sino `_zeroChanInfo` | Si `airplaySink != 0` (`+0x7EE0`), usa infra; sino `_zeroChanInfo` | Si `airplaySink != 0` (`+0x7F28`), usa infra; sino `_zeroChanInfo` | **si** (ivars desplazados) |
| **9** | Infraestructura general / fallback 6 | Si `infraAllowed`, usa infra. Si no y ranura 0, `{1, 6, 0xA}`. Si no y resto, canal 6 si SDB activo en estados específicos. | Misma regla; ranura 0 usa inmediatos `0x600000001` y `0xA` en lugar de símbolo estático. | Misma regla; usa `__ZL13_channel6Info`. | **si** (semántica idéntica) |

En las tres versiones, después de programar cada ranura se aplica una guardia de 6 GHz: si la tarjeta no tiene capacidades P2P en 6 GHz (`test byte ptr [...], 20h`), el flag `0x2000` es retirado de la ranura.

---

## 8. Implicaciones para la capa de controlador (`RTLAirLayer` / `RTL8814Native`)

Para que una misma extensión o capa de emulación AWDL opere sin modificaciones binarias en macOS 14.8.9, macOS 15.8.2 y macOS 26.7.1, deben implementarse las siguientes adaptaciones en el driver:

### 8.1 Publicación del bloque de capacidades de tarjeta (`getCARD_CAPABILITIES`)

El stack de Apple decide si habilita los canales sociales de 5 GHz (149 y 44) evaluando el bit `0x20` del bloque de capacidades de tarjeta dentro de `configureValidAWDLChannels`:
- **macOS 14 (Darwin 23)**: El bloque se ubica directamente en el objeto controlador base en `pm + 0xB18`. El chequeo se ejecuta sobre `[pm + 0xB1F] & 0x20`. En esta versión no existe la función `IO80211Controller::isCardCapabilitiesSet`.
- **macOS 15 (Darwin 24)**: El bloque se obtiene desreferenciando el puntero en `ctrl + 0x120` y comprobando el offset `0xB27`: `*(ctrl + 0x120) + 0xB27 & 0x20`.
- **macOS 26 (Darwin 25)**: El bloque se ubica en el offset `0xB37`: `*(ctrl + 0x120) + 0xB37 & 0x20`.

**Regla para el driver**:
Al responder a la petición WCL de capacidades o al inyectar el bloque de tarjeta en el controlador, la capa de compatibilidad debe asegurar que el bit `0x20` esté activo en el byte correspondiente según la versión mayor de Darwin en ejecución (`version_major == 23 ? 0xB1F : (version_major == 24 ? 0xB27 : 0xB37)`). Si este bit está a 0, el stack imprime `"Dual band not supported, so assume cannot use 5G"` y anula inmediatamente los canales 149 y 44, confinando AWDL exclusivamente a 2.4 GHz (canal 6).

### 8.2 Honestidad en el GET del canal de infraestructura (`getInfraChannel`)

Dado que el gestor AWDL no cuenta con un setter activo para recibir el canal de infraestructura desde el espacio de usuario o desde eventos de enlace (todos los métodos `setInfraChannel*` son código muerto comprobado), el único canal que el gestor conoce es el que responde el driver a la petición WCL `0xC02869C9` (tipo 4, 16 bytes):
- Si el driver reporta canal 0 cuando `en2` está asociado, el gestor asume pérdida de enlace infra y deja sin infraestructura las ranuras de tipo 7 y tipo 9 `?`.
- Si el driver reporta un canal DFS (52..144), el gestor activa el flag `radarDFS` y desactiva la inyección de infraestructura (§6).
- Para operar con infraestructura en 153/80 MHz, el driver debe devolver fielmente `{version: 1, channel: 153, flags: 0x410}`.

### 8.3 Recepción y despacho del ioctl 129 (`AWDL_SYNC_CHANNEL_SEQUENCE`)

El formato del payload emitido por `setChannelSequenceUnconditional` es estrictamente idéntico en las tres versiones:
- Tamaño de estructura: 400 bytes (`0x190`).
- Cabecera:
  - `+0`: Versión (u32 = 1).
  - `+4`: SDB flags (u8, `0x10` o `0x20`).
  - `+5`: Conteo de ranuras (u8 = 16).
  - `+6`: Encoding (u8, 1 si banda dual/SDB, 0 si canal único).
  - `+7`: Step (u8).
  - `+8`: Duplicate count (u8).
  - `+9`: Fill (u8).
- Ranuras (`+12..+203`): 16 entradas consecutivas de 12 bytes `{u32 version; u32 channel; u32 flags}`.

El driver puede procesar el ioctl 129 con la misma rutina en macOS 14, 15 y 26, sin necesidad de distinguir estructuras de payload diferentes.

### 8.4 Canales maestros primario y secundario (ioctls 132 y 205)

La notificación de los canales maestros hacia el firmware (`programMasterChannels`) emplea los mismos tokens en las tres versiones:
- Petición 132 (`0x84802869C8`): Canal maestro primario (habitualmente canal 6 o canal de infraestructura en modo 0).
- Petición 205 (`0xCD802869C8`): Canal maestro secundario (habitualmente canal 149 o 44).

La capa RTL los recibe en las ranuras 132 y 205 de la vtable de la interfaz AWDL (desplazadas en 14 y 15, ver `AWDL_CODIGO_VS_BUILDS.md` §2.2).

---

## 9. Puntos abiertos

1. **Impacto en firmware de la diferencia en la fila 4 (Low Power)**: En macOS 14.8.9 el estado *Low Power* no inyecta infraestructura dinámica en las ranuras 0 y 9. Queda por verificar en pruebas de campo si dispositivos en reposo bajo macOS 14 experimentan mayor latencia de descubrimiento con AirDrop frente a macOS 15/26.
2. **Compatibilidad del modo 0 con interfaces virtuales bridged**: Si `en2` forma parte de un bridge (`bridge0`), el código de `getInfrastructureState` contiene una rama especial de fallback que invoca `apple80211getCHANNEL` directamente sobre el dispositivo miembro. Debe verificarse si esta rama afecta el filtrado de tramas en el driver RTL.
3. **Persistencia del valor `dynSdbForceChanSeq`**: En tarjetas sin capacidades SDB completas, se debe determinar si forzar `dynSdbForceChanSeq = 0` estabiliza la alternancia entre canal 6 y canal 149 evitando pérdidas de sincronismo en el temporizador TSF.

---

## 10. Resumen de verificación
- Tablas `__ZL20_channelSeqSlotTypes`: 56 filas en 25G241 y 54 en 23J631 y 24H232; filas 0, 1, 3, 4, 24, 31,
  40/41 y 44/45 comprobadas en las tres colecciones, y `switch` de 21 casos sin sesgo en las tres.
- VAs de código de las tablas de §3 a §5: cabezas de instrucción en su colección.
