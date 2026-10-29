# Layouts C de los payloads apple80211 que una interfaz AWDL debe contestar (macOS 26, kernel 25.6.0)

Recuperación de los layouts exactos de los structs de datos de las requests apple80211 que alcanzan
la superficie AWDL: el nombre del struct (de la firma mangleada del método Broadcom del slot), el
tamaño total (del chequeo `len == N` del handler moderno del kernel y del tamaño de buffer que el
puente pasa a WCL), y cada campo con offset, ancho, tipo y significado. Cierra el pendiente de
[`AWDL_APPLE80211_STRUCTS.md`](AWDL_APPLE80211_STRUCTS.md), que documentaba 42 nombres de tipos sin
layout.

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`) y `wifip2pd.x86_64`
(md5 `a120cfccfa2783303604ce5be9e1763a`), decompilados con IDA 9.4 headless (`idat -A -S`) sobre
copias de los IDB en `Build/ida_awdl/task_awdl_structs/kc/` y `/p2p/` (volcados: `kc_proxi.txt`,
`kc_pm_get.txt`, `kc_pm_set.txt`, `kc_handlers.txt`, `kc_impl.txt`, `kc_impl2.txt`, `kc_tables.txt`,
`p2p_decomp.txt`, `p2p_disasm_key.txt`). Direcciones = VA del binario indicado (kernel salvo
indicación `w2p`). Lo no verificado va como `?`.

Tres fuentes por struct cuando existen: (a) el handler moderno de `IO80211Family` que valida la
longitud y hace el copyback, (b) el override `AppleBCMWLANProximityInterface` que llena o consume el
struct (slot de vtable, vtable `0xffffff80017f5b78`), (c) el getter/setter equivalente de
`IO80211AWDLPeerManager` (camino moderno), y (d) cuando aplica, el código de `wifip2pd` que arma o
parsea el buffer y los nombres de campo Swift del type dump del daemon
(`Build/ida_awdl/awdl_callers_probe.log`). Los offsets de campo de las secciones 4–6 salen de las
escrituras/lecturas observadas en los decompilados; un campo sin escritura observada va como `?`.

## 1. La petición: `apple80211req` (40 B)

| Offset | Ancho | Campo | Significado |
|---:|---:|---|---|
| +0x00 | 16 | `ifname[16]` | nombre BSD de la interfaz destino (awdl0 para el camino AWDL) |
| +0x10 | 4 | `req_type` u32 | tipo de request (1..586) |
| +0x14 | 4 | `val` u32 | campo auxiliar (0 en las peticiones observadas de wifip2pd) |
| +0x18 | 4 | `req_len` u32 | longitud del buffer de datos |
| +0x20 | 8 | `req_data` ptr | puntero al buffer de datos |

Evidencia: el despachador lee el tipo en `+0x10` y valida `[1, 586]`
(`apple80211RouteExternal`, `0xffffff800236cd2e`: `v13 = *(_DWORD *)(a4 + 16)`, ramas
`gGetHandlerTable[type-1]` / `gSetHandlerTable[type-1]`, base `0xffffff800245eb60` /
`0xffffff800245fef0`); wifip2pd arma la petición con tipo en `+16`, longitud en `+24` y puntero en
`+32` (`sub_100046D30`, `w2p 0x100046d5e`: `*(_DWORD *)(a4 + 16) = 97`,
`*(_DWORD *)(a4 + 24) = len`, `*(_QWORD *)(a4 + 32) = ptr`; ídem `sub_10004B300`:
`LODWORD(v66) = 293`, `DWORD2(v66) = 24`, `v67 = v61`, `w2p 0x10004b3d2`); los handlers leen
`req_len` en `+0x18` y `req_data` en `+0x20` (todos los decompilados de
`Build/ida_awdl/task_awdl_structs/kc_handlers.txt`). Coincide con el buffer crudo del ioctl de
[`AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md).

## 2. Orden de las peticiones de wifip2pd y estado estacionario

El inventario de call sites está cerrado (`AWDL_WIFIP2PD_POST_CREATE.md` §2.0-2.1): `Apple80211RawGet`
tiene exactamente 4 call sites (tres del tipo 12 y uno del tipo 97) y `Apple80211RawSet` 2 (94 y
293); no existen `Apple80211Get`/`Apple80211Set` en el daemon. El orden estático de las peticiones
sobre la interfaz AWDL:

| # | Petición | len | Emisor (VA w2p) | Nota |
|---:|---|---:|---|---|
| 1 | `RawGet` 12 `CARD_CAPABILITIES` | 21 | `0x10004811d`, `0x100048260`, `0x1000483a8` (en `sub_100047660`) | buffer de 21 B en `var_70+4`; rama AWDL (caso 1 del switch `0x100047e85` → `0x100048302`): bit 0 de `data[4]` (`0x1000483b5`: `test byte [rbp+var_70+8], 1`); ramas NAN (casos 2/3): bit 0 de `data[9]` (`0x10004812a`, `0x10004826d`: `test byte [rbp+var_70+0Dh], 1`) |
| 2 | `RawSet` 94 `VIRTUAL_IF_CREATE` | 32 | `0x100048d95` (payload construido en `0x100048c41`–`0x100048d17`) | rol 6 (AWDL) en payload+0x0C; nombre de respuesta en payload+16 |
| 3 | espera de 30 s | — | wait `0x1000491a8` | no es petición; la libera la notificación IORegistry (`AWDL_WIFIP2PD_POST_CREATE.md` §1) |
| 4 | `RawGet` 97 `VIRTUAL_IF_PARENT` | strlen(awdl0) | `sub_100046D30` en `0x100046db9` | respuesta = nombre del padre copiado sin NUL |
| 5 | `RawSet` 293 `COMPANION_SKYWALK_IF_CREATE` | 24 | `sub_10004B300` en `0x10004b3e6` | crea `llw0`; nombre de respuesta en payload+8 (ver §3.4) |
| 6 | ioctls ifnet | — | `sub_100046A30` + wrappers | `SIOCGIFMEDIA` (puerta 802.11), `SIOCSIFFLAGS`, `SIOCSIFLLADDR`, `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI` |
| 7 | ioctls `'i'` 130/131 con tipo 7708 | — | `sub_100058420`/`sub_1000586D0` | no alcanzables por el dispatch apple80211 (`AWDL_WIFIP2PD_POST_CREATE.md` §2.5) |

En estado estacionario el daemon no emite ninguna `RawGet`/`RawSet` adicional (los 6 call sites
listados son todos): solo monitorea eventos, vía `Apple80211StartMonitoringEvent` ×16 (rama
rol 0 + cola NSObject, `sub_100041EB0`, ids 1, 11, 59, 2, 3, 125, 126, 127, 9, 66, 74, 78, 82, 215,
55, 160; `AWDL_WIFIP2PD_P2P_EVENTS.md` §1) o vía `Apple80211RawP2PEventMonitoringInit`
(`sub_100041F20` → `0x100041fc2`) para la vía P2P/AWDL. Los eventos no son requests y no exigen
structs de respuesta.

## 3. Structs del ciclo de vida (los que wifip2pd envía)

### 3.1 `apple80211_capability_data` — tipo 12 GET, 28 B, len `!= 0`

| Offset | Ancho | Campo | Significado |
|---:|---:|---|---|
| +0x00 | 4 | `version` u32 | 1 en la implementación Broadcom |
| +0x04 | 24 | `flags[24]` u8×24 | mapa de bits de capacidades (193 bits usados en Broadcom) |

Handler GET `0xffffff8002279e89`: exige `len != 0` y `data != NULL`, llama a WCL con buffer interno
de 28 B (`sendIOUCToWcl(glue, 0xC02869C9, 12, buf, 0x1C)`) y hace copyback de
`min(req_len, 21)` bytes **desde `buf+4`** (los flags sin la version):
`memmove(req_data, &v13[1], min(req_len, 21))`. Implementación Broadcom
(`AppleBCMWLANCore::getCARD_CAPABILITIES`, `0xffffff8001661076`): escribe `version = 1` en +0
(`*(_DWORD *)a3 = 1`), valor constante 0xBF en +4 (`*(_WORD *)(a3 + 4) = -6545`), 0x40 en +9, 0x41
en +12 (`*(_WORD *)(a3 + 12) = 513`), 0x61 en +6 y decenas de bits condicionados a
`featureFlagIsBitSet(N)` en los bytes +6..+24.

**El bit que wifip2pd exige para AWDL es el bit 0 del byte +0x08 del struct** (flags[4]). El
buffer de la petición es `var_70+4` (`lea r12, [rbp+var_70+4]` en `w2p 0x100048090`, puntero de
datos en `var_80`), de modo que `data[k]` es `var_70+4+k` y, como el copyback de 21 B empieza en
`buf+4` del struct, `data[k]` es el byte `4+k` del struct. La rama AWDL del switch por código de rol
(`0x100047e85`, caso 1 → `0x100048302`) hace su `RawGet` en `0x1000483a8` y prueba
`test byte [rbp+var_70+8], 1` (`w2p 0x1000483b5`) = `data[4]` = **byte +0x08**. Las ramas de los
códigos 2 y 3 (Wi-Fi Aware) hacen el `RawGet` en `0x10004811d`/`0x100048260` y prueban
`test byte [rbp+var_70+0Dh], 1` (`w2p 0x10004812a`/`0x10004826d`) = `data[9]` = **byte +0x0D**,
que es la puerta NAN. En Broadcom el byte +0x0D bit 0 se pone cuando `featureFlagIsBitSet(this, 6)
!= 0` (`0xffffff8001661076`: `if (featureFlagIsBitSet(this, 6) != 0) *(_BYTE *)(a3 + 13) |= 1u;`);
la condición Broadcom que pone el byte +0x08 bit 0 queda como `?`.
Para un kext propio: contestar el GET 12 con `flags[4] |= 0x01` (byte +0x08) es la puerta mínima
para AWDL; `flags[9] |= 0x01` (byte +0x0D) abriría además la de Wi-Fi Aware.

### 3.2 `apple80211_virt_if_create_data` — tipo 94 SET, 32 B, len `== 32`

| Offset | Ancho | Campo | Significado |
|---:|---:|---|---|
| +0x00 | 4 | u32 | enviado por wifip2pd (`mov [rbp+var_C0], r14d`, `w2p 0x100048c5f`); sin lectura observada en Broadcom `?` |
| +0x04 | 6 | `mac[6]` | MAC solicitada para la interfaz (wifip2pd la escribe byte a byte en `w2p 0x100048cd9`–`0x100048d17`) |
| +0x0A | 2 | pad | 0 (el buffer se pone a cero con `movaps` en `w2p 0x100048c44`) |
| +0x0C | 4 | `role` u32 | rol de interfaz virtual: 6 = AWDL (`w2p 0x100048cc8`), 9 = `?` (`0x100048c66`), 0xA = `?` (`0x100048c97`) |
| +0x10 | 16 | `ifname[16]` | **salida**: nombre BSD creado, escrito por el driver |

Handler SET `0xffffff80022886a8`: `len == 32`, `data != NULL`, ruta WCL
(`sendIOUCToWcl(glue, 0x802869C8, 94, data, 32)`) y, si no la consume, puente
`apple80211setVIRTUAL_IF_CREATE` (cast a `IO80211InfraProtocol`, slot +0x1160;
`AWDL_APPLE80211_DISPATCH.md` §4). Implementación Broadcom
(`AppleBCMWLANCore::setVIRTUAL_IF_CREATE`, `0xffffff80016788f4`): lee `role` en `+0x0C`
(`v4 = *(_DWORD *)(a2 + 12)`; 6 → interfaz Proximity/AWDL, 7 → APSTA, 8..10 →
`createNANInterface`), pasa `a2+4` como `ether_addr*` de la MAC a
`AppleBCMWLANIO80211APSTAInterface::withOptions` (y el equivalente en la rama 6 vía
`createProxInterface`), y en el éxito limpia y escribe el nombre de salida:
`*(_QWORD *)(a2 + 24) = 0; *(_QWORD *)(a2 + 16) = 0; strlcpy((char *)(a2 + 16), bsdname, 0x10)`.
wifip2pd lee el nombre desde payload+16 y continúa (`AWDL_WIFIP2PD_POST_CREATE.md` §1.4).

### 3.3 `VIRTUAL_IF_PARENT` — tipo 97 GET (sin struct: respuesta = C-string)

El handler `getVIRTUAL_IF_PARENT` (`0xffffff800227b7ff`) no usa struct: obtiene el padre de la
interfaz virtual (`safeMetaCast` a `IO80211VirtualInterface`), toma el nombre BSD del padre
(`IO80211SkywalkInterface::getBSDName`) y exige `req_len >= strlen(nombre)`; copia `strlen(nombre)`
bytes **sin NUL** en `req_data` (`memmove(*(void **)(a4 + 32), BSDName, v10)`). wifip2pd envía
`len = strlen(awdl0)` y parsea buscando `'\0'` con tope en la longitud ofrecida
(`sub_100046D30`, `w2p 0x100046dd8`). Respuesta esperada sobre awdl0: `en0` (el nombre del padre).

### 3.4 `apple80211_companion_skywalk_if_create` — tipo 293 SET, 24 B, len `== 24`

| Offset | Ancho | Campo | Significado |
|---:|---:|---|---|
| +0x00 | 4 | `code` u32 | código de companion que wifip2pd propaga de su llamador (`v61[0] = a5`, `w2p 0x10004b34d`); el kernel nunca lo lee y su valor en el camino AWDL queda `?`; tipo auxiliar Swift `apple80211_companion_skywalk_if_create_code` = `UInt32` rawValue (`awdl_callers_probe.log:1666`) |
| +0x04 | 4 | `op` u32 | 1 = crear (`v61[1] = 1`, `w2p 0x10004b352`); 0 = destruir; cualquier otro valor = no-op que devuelve 0 |
| +0x08 | 16 | `name[16]` | **salida**: nombre BSD de la interfaz companion; se escribe solo en el éxito de la creación, también cuando la interfaz ya existía |

Handler SET `0xffffff800228b4b8`: `len == 24`, `data != NULL`, ruta WCL con 0x18 y fallback al
puente. Implementación Broadcom (`AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE`,
`0xffffff8001722ef2`; semántica verificada en
[`AWDL_LLW_COMPANION.md`](AWDL_LLW_COMPANION.md) §3.2): lee `op` en `+0x04`
(`v3 = *(_DWORD *)(a2 + 4)`) y nunca lee `code` en `+0x00`. `op == 1` crea la interfaz Skywalk
de baja latencia vía `createSkywalkInterface` (sitio `0xffffff8001723068`) y en el éxito escribe
el nombre en payload+8; si la companion ya existe (puntero en ivars+120), escribe igualmente el
nombre BSD de la existente en payload+8 y devuelve 0 con el log
`"AWDL companion skywalk interface %s already exists for %s"`; si la creación falla, devuelve el
error sin escribir payload+8. `op == 0` destruye vía `destroySkywalkInterface` (sitio
`0xffffff8001722fe9`); sin companion devuelve `0xE00002C0` con el log
`"Deletion failed, NAN companion skywalk interface does not exists for %s"`.
**Cualquier otro valor de `op` es un no-op que devuelve 0.** El nombre se escribe solo en el
éxito de la creación: `__strncpy_chk((void *)(a2 + 8), bsdname, 0x10)`. **El nombre de respuesta
se devuelve en payload+8, no en `req.ifname`**: wifip2pd lo lee con `String.init(cString:)` sobre
el mismo offset (`v62` en `rbp-0x70` = `v61+8` tras `v61` en `rbp-0x78`, `w2p 0x10004b40a`) y
valida con `if_nametoindex("llw0")` (inmediato `813132908` = `0x30776C6C`, `w2p 0x10004b814`) si
el rol del handle es 1, o `if_nametoindex("llw1")` (`829910124` = `0x31776C6C`,
`w2p 0x10004b862`) si es 2. Esto corrige el detalle de `AWDL_WIFIP2PD_POST_CREATE.md` §2.4 que
atribuía la respuesta al campo `ifname` de la petición (sobre awdl0 va el nombre del padre como
dirección de la request; el nombre creado vuelve en payload+8). El campo `ifname` de la petición
(req+0) lleva `awdl0`.

## 4. Superficie AWDL con slot de vtable: structs por tipo

Tabla resumen. «len» = igualdad exigida por el handler moderno (leída de
`Build/ida_awdl/task_awdl_structs/kc_handlers.txt`; coincide con `AWDL_APPLE80211_DISPATCH.md` §4);
«—» = sin fila en `gGet/gSetHandlerTable` (entrada 0 verificada en `kc_tables.txt` para los tipos
143, 149, 151, 158, 225, 262, 263, 341, 342, 414, 418, 432, 467): esas requests devuelven 102 por
el camino moderno y solo son alcanzables por la ruta interna de glue
(`AWDL_APPLE80211_DISPATCH.md` §6).

| tipo GET/SET | IOC | struct | len GET/SET | slot GET/SET (vptr) | método GET Broadcom | PM GET/SET |
|---|---|---|---|---|---|---|
| 1/1 | SSID | `apple80211_ssid_data` | copyback especial / copyback | +0xFC8 / +0x10A0 | `getSSID` `0xffffff800171a70e` | — |
| 4/4 | CHANNEL | `apple80211_channel_data` | ==16 / !=0 | +0xFD0 / +0x10A8 | stub `0xffffff800171a752` | — |
| 9/— | BSSID | `apple80211_bssid_data` | >=6 / — | +0xFD8 / — | stub error `0xffffff800171a702` | — |
| 13/— | STATE | `apple80211_state_data` | ==8 / — | +0xFE0 / — | `getSTATE` `0xffffff8001719944` | — |
| 116/116 | AWDL_SYNC_PARAMS | `apple80211_awdl_sync_params` | ==24 / ==24 | +0xFE8 / +0x10B0 | `0xffffff800171b1b8` | `0xffffff800221ee92` / `0xffffff800221b73c` |
| 117/117 | AWDL_SYNC_ENABLED | `apple80211_awdl_sync_enabled` | ==12 / ==12 | +0xFF0 / +0x10B8 | `0xffffff800171b092` | `0xffffff800221f0f4` / `0xffffff800221b842` |
| 118/118 | AWDL_EXTENSION_STATE_MACHINE_PARAMETERS | `apple80211_awdl_extension_state_machine_parameter` | ==20 / ==20 | +0xFF8 / +0x10C0 | `0xffffff800171be78` | `0xffffff800221ed76` / `0xffffff800221bc44` |
| 122/122 | AWDL_ELECTION_ID | `apple80211_awdl_election_id` | ==8 / ==8 | +0x1000 / +0x10C8 | `0xffffff800171c456` | `0xffffff800221f446` / `0xffffff800221c898` |
| —/125 | AWDL_BSSID | `apple80211_awdl_bssid` | — / ==16 | — / +0x10D0 | — | `0xffffff800221f654` / `0xffffff800221ca42` |
| 126/126 | AWDL_ELECTION_METRIC | `apple80211_awdl_election_metric` | ==12 / ==12 | +0x1008 / +0x10D8 | `0xffffff800171cd70` | `0xffffff800221f528` / `0xffffff80021cb342` |
| 129/129 | AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE | `apple80211_awdl_sync_channel_sequence` | ==400 / ==400 | +0x1010 / +0x10E0 | `0xffffff800172421a` | `0xffffff800221f7bc` / `0xffffff800221cedc` |
| 130/— | PEER_CACHE_MAXIMUM_SIZE | `apple80211_peer_cache_maximum_size` | ==8 / — | +0x1018 / — | `0xffffff80017233b6` | `0xffffff800221f62c` (getAwdlMaxPeers) |
| 132/132 | AWDL_MASTER_CHANNEL | `apple80211_awdl_master_channel` | ==8 / ==8 | +0x1020 / +0x10E8 | `0xffffff800171f1e6` | `0xffffff800221faf6` / `0xffffff800221d4de` |
| 134/134 | AWDL_SYNC_STATE | `apple80211_awdl_sync_state` | ==24 / ==24 | +0x1028 / +0x10F0 | `0xffffff800171b528` | `0xffffff800221fd48` / `0xffffff800221e3d4` |
| 135/135 | AWDL_ELECTION_RSSI_THRESHOLDS | `apple80211_awdl_election_rssi_thresholds` | ==20 / ==20 | +0x1030 / +0x10F8 | `0xffffff800171d2e4` | — / `0xffffff800221e2f4` |
| 136/136 | AWDL_PRESENCE_MODE | `apple80211_awdl_presence_mode` | ==8 / ==8 | +0x1038 / +0x1100 | `0xffffff800171bd12` | `0xffffff800221fdc0` / `0xffffff800221e806` |
| —/143 | PEER_CACHE_CONTROL | `apple80211_peer_cache_control` | — / — | — / +0x1108 | — | — |
| —/149 | AWDL_OOB_REQUEST | `apple80211_awdl_oob_request` | — / — | — / +0x1110 | — | — |
| —/151 | AWDL_SYNC_FRAME_TEMPLATE | `apple80211_awdl_sync_frame_template` | — / — | — / +0x1118 | — | — |
| 155/155 | AWDL_ELECTION_PARAMS | `apple80211_awdl_election_params` | ==56 / ==56 | +0x1040 / +0x1120 | `0xffffff800171ce5a` | `0xffffff800221f68c` / `0xffffff800221cb66` |
| 158/— | AWDL_DEVICE_CAPABILITIES | `apple80211_awdl_device_capabilities` | — / — | +0x1048 / — | `0xffffff800171be38` | — |
| —/164 | AWDL_PEER_TRAFFIC_REGISTRATION | `apple80211_awdl_peer_traffic_registration` | — / ==184 | — / +0x1128 | — | `0xffffff800220d6ae` / `0xffffff800220946a` |
| 205/205 | AWDL_SECONDARY_MASTER_CHANNEL | `apple80211_awdl_secondary_master_channel` | ==8 / ==8 | +0x1050 / +0x1130 | `0xffffff800171ec10` | `0xffffff800221fbd2` / `0xffffff800221d314` |
| 208/208 | AWDL_AF_TX_MODE | `apple80211_awdl_af_tx_mode` | ==8 / ==8 | +0x1058 / +0x1138 | `0xffffff8001719b62` | `0xffffff800221c220` / `0xffffff800221c32e` |
| 213/213 | AWDL_MIN_RATE | `apple80211_awdl_min_rate` | ==8 / ==8 | +0x1060 / +0x1140 | `0xffffff8001728a3c` | `0xffffff8002220748` / `0xffffff800222065e` |
| —/219 | AWDL_ENCRYPTION_KEYS | `apple80211_key` | — / ==148 | — / +0x1148 | — | — / `0xffffff800221d63e` |
| 220/220 | AWDL_ENCRYPTION_TYPE | `apple80211_encrypt` | ==8 / ==8 | +0x1068 / +0x1150 | `0xffffff800171f900` | `0xffffff800221d774` / `0xffffff800221d6f2` |
| —/225 | AWDL_OOB_AUTO_REQUEST | `apple80211_awdl_oob_request` | — / — | — / +0x1158 | — | — |
| 226/226 | AWDL_TXCAL_PERIOD | `apple80211_awdl_txcal_period` | — / — | +0x1070 / +0x1160 | `0xffffff8001728cae` | `0xffffff800221c08a` / `0xffffff800221c10c` (ruta interna tipo 226) |
| —/229 | AWDL_AWDL_ADVERTISERS | `awdl_peer_advert_add` | — / ==44 | — / +0x1168 | — | — / `0xffffff800221c19e` |
| 247/— | AWDL_DEV_STATS | `apple80211_awdl_dev_stats` | ==124 / — | +0x1078 / — | `0xffffff800171b698` | `0xffffff800221dbfc` (ruta interna 247) |
| 262/262 | AWDL_DFSP_CONFIG | `apple80211_awdl_dfsp_config` | — / — | +0x1080 / +0x1170 | `0xffffff8001728fba` | — |
| —/263 | AWDL_DFSP_UCSA_CONFIG | `apple80211_awdl_dfsp_ucsa_config` | — / — | — / +0x1178 | — | — |
| 293/293 | COMPANION_SKYWALK_IF_CREATE | `apple80211_companion_skywalk_if_create` | — / ==24 | — / +0x1180 | — | — |
| 338/— | AWDL_CCA | `apple80211_awdl_cca` | ==392 / — | +0x1088 / — | `0xffffff800171ba8e` | `0xffffff800221dc36` |
| 341/— | AWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES | `apple80211_awdl_cap` | — / — | +0x1090 / — | `0xffffff800171ed38` | — |
| —/342 | AWDL_OOB_SEC_PAYLOAD_REQUEST | `apple80211_awdl_af_sec_payload_request` | — / — | — / +0x1188 | — | — |
| 344/344 | LLW_PARAMS | `apple80211_llw_params` | ==44 / ==44 | +0x1098 / +0x1190 | `0xffffff800172753e` | `0xffffff800221e004` / `0xffffff800221e086` |
| —/411 | TX_RATE | `apple80211_tx_rate` | — / ==16 | — / +0x1198 | — | — / `0xffffff800221e112` |
| —/414 | P2P_COEX_PROTECTION | `apple80211_p2p_coex_protection` | — / — | — / +0x11A0 | — | — |
| —/418 | PSF_DWELL | `apple80211_awdl_psf_dwell_params` | — / — | — / +0x11A8 | — | — |
| —/432 | AWDL_CONFIG | `apple80211_awdl_config_request` | — / — | — / +0x11B0 | — | — / `0xffffff800221c9c0` (ruta interna 432) |
| —/467 | AWDL_RTG_PEER_STATS_CONFIG | `apple80211_rtg_peer_stats_cfg` | — / — | — / +0x11B8 | — | — |

Los tipos generales 1/4/9/13 llegan a los slots por la cascada de `safeMetaCast` de los puentes
(`AWDL_APPLE80211_DISPATCH.md` §3); el resto, por el puente específico con cast a
`IO80211AWDLProtocol`.

### 4.1 Campos observados por struct

Los offsets siguientes salen de las escrituras/lecturas sobre el parámetro struct en los
decompilados citados. «—» en significado = no determinado.

**`apple80211_ssid_data`** (40 B; handler GET 1 `0xffffff8002279821`, buffer WCL 0x28, copyback
`min(req_len, ssid_len)`)
- `+0x00` u32 `version` — no escrito por el método Broadcom `?` (el handler lo copia tal cual del buffer interno)
- `+0x04` u32 `ssid_len` — longitud del SSID (`*(_DWORD *)(a2 + 4) = strlen(...)`, `0xffffff800171a70e`)
- `+0x08` 32 `ssid[32]` — SSID + NUL; el método Broadcom AWDL devuelve el string fijo `"awdl"`
  (bytes leídos en `0xffffff8001797376`: `awdl\0`), `getSSID` `0xffffff800171a70e`
- `setSSID` Broadcom = stub que devuelve 102 (`0xffffff8001715a70`); el handler SET 1
  (`0xffffff8002286f15`) copia `req_len` bytes del request al buffer del puente.

**`apple80211_channel_data`** (16 B; handler GET 4 `0xffffff80022799c1` len==16; SET 4
`0xffffff80022871b1` len!=0)
- `+0x00` u32 `version` — no rellenado por Broadcom AWDL `?`
- `+0x04` u32 `channel.version` — `apple80211_channel` (Swift: `{version, channel, flags}`,
  metadata `w2p $s3__C18apple80211_channelVMa 0x10000f5f0`)
- `+0x08` u32 `channel.channel` — `setCHANNEL` Proxi lo lee de ahí (`*(_DWORD *)(a2 + 8)`,
  `0xffffff800171e8d8`); el GET del driver principal lo llena (`ChanSpecConvToApple80211Channel(..., a2 + 4)`,
  `AppleBCMWLANCore::getCHANNEL 0xffffff8001660eb2`)
- `+0x0C` u32 `channel.flags`
- `getCHANNEL` del ProximityInterface es un stub que devuelve 0 sin tocar el struct
  (`0xffffff800171a752`).

**`apple80211_bssid_data`** (12 B; handler GET 9 `0xffffff8002279ce7`, len >= 6, copyback de
`min(req_len, 6)` bytes desde struct+4 — solo la MAC)
- `+0x00` u32 `version`
- `+0x04` 6 `bssid` ether_addr — `AppleBCMWLANCore::getBSSID 0xffffff800166105a` escribe ahí
  (`IO80211BssManager::getCurrentBSSID(..., a2 + 4)`)
- `+0x0A` 2 pad
- `getBSSID` del ProximityInterface = stub que devuelve `0xE00002C1` (`0xffffff800171a702`):
  el GET 9 sobre awdl0 falla con la implementación Broadcom.

**`apple80211_state_data`** (8 B; handler GET 13 `0xffffff8002279fb1` len==8)
- `+0x00` u32 `version`
- `+0x04` u32 `state` — el método Broadcom escribe 4 (`*(_DWORD *)(a2 + 4) = 4`,
  `getSTATE 0xffffff8001719944`, log `"getSTATE %u"`); valor 4 `?` (probable estado operativo)

**`apple80211_awdl_sync_params`** (24 B; len ==24 en ambos sentidos, handlers
`0xffffff800227bbcd`/`0xffffff800228883b`)
- `+0x00` u32 `version` — el PM getter lo pone a 0 (`*a2 = 0`, `0xffffff800221ee92`)
- `+0x04` u32 `aw_cmn_length` — chequeo `== 0` → error `"Invalid aw_cmn_length"`
  (`getAWDL_SYNC_PARAMS 0xffffff800171b1b8`, línea de log 1802)
- `+0x08` u32 — sin nombre; valor u16 del iovar del chip extendido a u32 (desensamblado
  `0xffffff800171b395`–`0xffffff800171b3ee`: `movzx word [rbp+var_50]` → store en a2+8)
- `+0x0C` u32 `aw_ext_length` — chequeo `== 0` → `"Invalid aw_ext_length"` (línea 1811)
- `+0x10` u32 `action_frame_period` — chequeo `== 0` → `"Invalid action_frame_period"` (línea 1806)
- `+0x14` u32 — cache del ivar Proxi +0x29C (`a2[5] = v16[167]`; `setAWDL_SYNC_PARAMS
  0xffffff800171ddee` lo persiste desde el request)
- El setter del PM aplica la convención «-1 = mantener el valor actual» sobre +4..+16 y guarda en
  sus ivars +62940/+62944/+62948/+62952 (`setAwdlSyncParams 0xffffff800221b73c`); el getter del PM
  vuelca esos mismos ivars y además re-ruta la request al driver
  (`apple80211RouteInternal(0xC02869C9, 116, ...)`, `getAwdlSyncParams 0xffffff800221ee92`).

**`apple80211_awdl_sync_enabled`** (12 B; len ==12, handlers `0xffffff800227bc2f`/`0xffffff800228889d`)
- `+0x00` u32 `version`
- `+0x04` u32 `flags`
- `+0x08` u32 `enabled` — el PM compara con el bit 1 del ivar +16096
  (`getAwdlSyncEnabled 0xffffff800221f0f4`); Proxi escribe `+8`
  (`getAWDL_SYNC_ENABLED 0xffffff800171b092`); nombres de los tres campos del type dump Swift
  (`awdl_callers_probe.log:1345-1348`)

**`apple80211_awdl_extension_state_machine_parameter`** (20 B; len ==20, handlers
`0xffffff800227bc91`/`0xffffff80022888ff`)
- `+0x00` u32 `version`
- `+0x04` u32 (valor efectivo u8) — ivar PM +17036 (`getAwdlExtensionParams 0xffffff800221ed76`:
  `v12[17036] = *((_BYTE *)a2 + 4)`)
- `+0x08` u32 (u8) — ivar PM +17035
- `+0x0C` u32 (u8) — ivar PM +17037
- `+0x10` u32 (u8) — ivar PM +17038
- El setter del PM trata -1 como «mantener» sobre los cuatro (`setAwdlExtensionParams
  0xffffff800221bc44`).

**`apple80211_awdl_election_id`** (8 B; len ==8, handlers `0xffffff800227bd8a`/`0xffffff8002288a72`)
- `+0x00` u32 `version`
- `+0x04` u32 `election_id` — ivar PM +37259 (`getAwdlElectionId 0xffffff800221f446`:
  `*((_DWORD *)a2 + 1) = *(_DWORD *)(... + 37259)`); el valor efectivo cabe en u16 (el método
  Proxi copia solo 16 bits: `*(_DWORD *)(a2 + 4) = *(unsigned __int16 *)&v4[1]`,
  `0xffffff800171c456`)

**`apple80211_awdl_bssid`** (16 B; len ==16 solo SET, handler `0xffffff8002288b36`)
- `+0x00` u32 `version`
- `+0x04` 6 `bssid` ether_addr — comparado con la dirección actual (`memcmp(a2+4, ...)`,
  `setAwdlBssid 0xffffff800221ca42`) y logueado byte a byte por Proxi
  (`setAWDL_BSSID 0xffffff800172181a`, bytes +4..+9)
- `+0x0A` 6 — segunda MAC de 6 B comparada/logueada por el PM (`memcmp(__s1, a2 + 10, 6)`,
  `0xffffff800221ca42`); significado `?` (sin nombre en logs)
- `+0x10` pad

**`apple80211_awdl_election_metric`** (12 B; len ==12, handlers `0xffffff800227be4e`/`0xffffff8002288b98`)
- `+0x00` u32 `version`
- `+0x04` u32 `metric` — ivar PM +37255 (`getAwdlElectionMetric 0xffffff800221f528`);
  Proxi lo persiste en su ivar +563 (`setAWDL_ELECTION_METRIC 0xffffff800171cf76`)
- `+0x08` u8 `flag` — si != 0 el PM mantiene su métrica actual en vez de la del request
  (`setAwdlElectionMetric 0xffffff80021cb342`: `if (*(_BYTE *)(a2 + 8) != 0) a2[1] = ivar+37255`);
  nombre `?`
- `+0x09` 3 pad

**`apple80211_awdl_sync_channel_sequence`** (400 B; len ==400, handlers
`0xffffff800227bf70`/`0xffffff8002288caa`)
- `+0x00` u32 `version`
- `+0x04` u8 — `?`
- `+0x05` u8 `count` — número de entradas de canal; el PM valida el rango
  (`setAwdlChannelSequence 0xffffff800221cedc`: `(unsigned __int8)(a2[5] - 33) > 0xDF` → error
  `"AWDL channel sequence length invalid %u (max = 32)"`, líneas de log con `a2[5]`)
- `+0x06` u8 `dfs` — almacenado en el ivar PM +42866 (`a2[6]`)
- `+0x07` u8 — ivar PM +42867 (`?`)
- `+0x08` u8 — ivar PM +42848 (`?`)
- `+0x09` u8 — ivar PM +42856 (`?`); cuando != 0 el PM además copia un u32 del propio struct
  (`*((_DWORD *)a2 + 99) = *(_DWORD *)(v10 + 4)`, `getAwdlChannelSequence 0xffffff800221f7bc`)
- `+0x0A` 6 pad
- `+0x10` u16 `entries[]` — secuencia de canales; el PM los imprime como `0x%04x` y hace
  `memmove(pm + 8, a2, 0x190)` del bloque completo (`setAwdlChannelSequence`); Proxi llena desde
  `a2+16`/`a2+12` (`getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE 0xffffff800172421a`)
- `+0x18C` u32 — `?` (campo en +396 tocado por el getter del PM)
- (400 = 0x190, el tamaño completo que el PM persiste)

**`apple80211_peer_cache_maximum_size`** (8 B; len ==8 GET, handler `0xffffff800227bfd5`)
- `+0x00` u32 `version`
- `+0x04` u32 `size` — `_maxPeerCacheSize` en el PM (`getAwdlMaxPeers 0xffffff800221f62c`);
  Proxi escribe 0 (`getPEER_CACHE_MAXIMUM_SIZE 0xffffff80017233b6`)

**`apple80211_awdl_master_channel`** (8 B; len ==8, handlers `0xffffff800227c033`/`0xffffff8002288d0f`)
- `+0x00` u32 `version`
- `+0x04` u32 `channel` — ivares PM +17060 (get, `0xffffff800221faf6`) y +17040 (set,
  `0xffffff800221d4de`)

**`apple80211_awdl_sync_state`** (24 B; len ==24, handlers `0xffffff800227c111`/`0xffffff8002288e21`)
- `+0x00` u32 `version` — el setter del PM lo fuerza a 1 (`setAwdlSyncState 0xffffff800221e3d4`)
- `+0x04` u32 `state` — ivar PM +17028 (`getAwdlSyncState 0xffffff800221fd48`); el Proxi setter
  rechaza modos no soportados (`setAWDL_SYNC_STATE 0xffffff800171e164`, `"unsupported mode %u"`)
- `+0x08` u32 — escrito a 0 por Proxi get (`0xffffff800171b528`); `?`
- `+0x0C` u16 — ivar PM +37241 (`?`)
- `+0x10` u32 — leído por Proxi set (`v8 = *(_DWORD *)(a2 + 16)`); `?`
- `+0x14` u8 — leído por Proxi set (`v7 = *(_BYTE *)(a2 + 20)`); `?`
- `+0x15` 3 pad

**`apple80211_awdl_election_rssi_thresholds`** (20 B; len ==20, handlers
`0xffffff800227c1d5`/`0xffffff8002288e83`)
- `+0x00` u32 `version`
- `+0x04` s8 — umbral 1 (Proxi set lo copia a ivar +567: `a2[4]` sobre `_BYTE*`, `0xffffff800171d16a`;
  get Proxi: sign-extend de un dword del chip, `0xffffff800171d2e4`)
- `+0x08` s8 — umbral 2 (ivar +570)
- `+0x0C` s8 — umbral 3 (ivar +571)
- `+0x10` s8 — umbral 4 (ivar +569)
- `+0x11` 3 pad
- Semántica por umbral `?` (cuatro sbytes sin nombres en logs).

**`apple80211_awdl_presence_mode`** (8 B; len ==8, handlers `0xffffff800227c261`/`0xffffff8002288ee5`)
- `+0x00` u32 `version`
- `+0x04` u32 `mode` — limitado a <= 0x3F (`setAwdlPresenceMode 0xffffff800221e806`:
  `if (*((_DWORD *)a2 + 1) <= 0x3Fu)`); ivar PM +17032 (u8 bajo)

**`apple80211_awdl_election_params`** (56 B; len ==56, handlers `0xffffff800227c387`/`0xffffff8002289140`)
- `+0x00` u32 `version` — el setter del PM lo fuerza a 1 (`setAwdlElectionParams 0xffffff800221cb66`)
- `+0x04` 12 — sin escritura observada `?`
- `+0x10` u8 — desde el TLV `awdlElectionParamsTlv` byte 0 (Proxi get `0xffffff800171ce5a`)
- `+0x12` u16 — TLV bytes 1-2
- `+0x14` u8 — TLV byte 25
- `+0x16` u32 — TLV bytes 15-18
- `+0x1A` u16 — TLV bytes 19-20
- `+0x1C` u32 — TLV bytes 21-24
- `+0x20` u32 — TLV bytes 3-6
- `+0x24` u32 `election_id` — el PM lo compara con su ivar +37259 (`getAwdlElectionParams`:
  `a2[9]` sobre `unsigned __int16*` = dword @36)
- `+0x28` 16 resto `?` (56 − 40)
- Nombres de los campos `?` (el TLV `awdlElectionParamsTlvV2` aparece como tipo en
  `___FUNCTION__._ZN22IO80211AWDLPeerManager26setElectionParamsToNANPeerEP23awdlElectionParamsTlvV2`,
  `syms.txt:2779`, sin layouts).

**`apple80211_awdl_device_capabilities`** (tamaño `?` >= 20; sin len — tipo 158 sin fila en tablas)
- `+0x00` u32 `version` `?`
- `+0x08` u32 — constante 10 (`a2[2] = 10`, `getAWDL_DEVICE_CAPABILITIES 0xffffff800171be38`)
- `+0x0C` u32 (valor u16) — del estado del chip +142
- `+0x10` u32 (valor u16) — del estado del chip +140

**`apple80211_awdl_secondary_master_channel`** (8 B; len ==8, handlers
`0xffffff800227cc7b`/`0xffffff80022899d4`)
- `+0x00` u32 `version`
- `+0x04` u32 `channel` — ivar PM +17072 (get, `0xffffff800221fbd2`) / +17044 (set,
  `0xffffff800221d314`); valor efectivo u8

**`apple80211_awdl_af_tx_mode`** (8 B; len ==8, handlers `0xffffff800227cdaa`/`0xffffff8002289a36`)
- `+0x00` u32 `version`
- `+0x04` u32 `mode` — ivar PM +37144 (`getAwdlAfTxMode 0xffffff800221c220`); Proxi escribe 0 por
  defecto (`getAWDL_AF_TX_MODE 0xffffff8001719b62`)

**`apple80211_awdl_min_rate`** (8 B; len ==8, handlers `0xffffff800227ce86`/`0xffffff8002289a98`)
- `+0x00` u32 `version`
- `+0x04` u8 `rate` — ivar PM +42417 (`setAwdlMinRate 0xffffff800222065e`); Proxi lo lee como u8
  (`setAWDL_MIN_RATE 0xffffff8001728a48`)
- `+0x05` 3 pad

**`apple80211_key`** (148 B; len ==148 solo SET, handler `0xffffff8002289c8b`)
- Layout clásico de llave Apple80211; observado: el PM loguea los bytes +92..+97
  (`setAwdlKey 0xffffff800221d63e`) y Proxi lo convierte con `mapAppleKeyToBcomKey(dst, a2)` a un
  buffer de 0xA4 (`setAWDL_ENCRYPTION_KEYS 0xffffff800171f480`). Desglose de campos `?` (sin
  escrituras individuales en los decompilados; el struct estándar tiene version/longitud/índice/
  datos/RSC, pero no se verificó aquí).

**`apple80211_encrypt`** (8 B; len ==8, handlers `0xffffff800227d210`/`0xffffff8002289cf0`)
- `+0x00` u32 `version`
- `+0x04` u32 `encrypt_type` — Proxi escribe 0 por defecto (`getAWDL_ENCRYPTION_TYPE
  0xffffff800171f900`); `?` catálogo de valores

**`apple80211_awdl_txcal_period`** (8 B; sin len en tablas modernas — la ruta es la interna tipo
226, `setAwdlTxCalPeriod 0xffffff800221c10c` / `getAwdlTxCalPeriod 0xffffff800221c08a`)
- `+0x00` u32 `version`
- `+0x04` u32 `period` — `*((_DWORD *)a2 + 1)` logueado por el setter del PM

**`awdl_peer_advert_add`** (44 B; len ==44 solo SET, handler `0xffffff8002289eb1`)
- `+0x00` u16 `aw_counter`
- `+0x02` u16 `tx_counter`
- `+0x04` u16 `tx_delay`
- `+0x06` u16 `period_tu`
- `+0x08` u16 `aw_cmn_length`
- `+0x0A` u16 `aw_ext_length`
- `+0x0C` u16 `aw_period`
- `+0x0E` i8 `rssi`
- `+0x0F` u8 `guard_time`
- `+0x10` u8 `presence_mode`
- `+0x11` u8 `age`
- `+0x12` u8 `dist_top`
- `+0x13` 1 pad
- `+0x14` u32 `ms`
- `+0x18` u32 `self_metrics`
- `+0x1C` u32 `top_master_metrics`
- `+0x20` 6 `top_master` ether_addr
- `+0x26` 6 `addr` ether_addr
- `+0x2C` 2 pad
- Campos del type dump Swift (`awdl_callers_probe.log:1298-1317`); el alineado natural (u32 @20)
  cierra exactamente en 44. Proxi copia los primeros 40 bytes como qwords + el dword @40
  (`setAWDL_AWDL_ADVERTISERS 0xffffff800171c932`: `v6..v10 = a2[0..4]`,
  `*((_DWORD *)v5 + 11) = *((_DWORD *)a2 + 10)`).

**`apple80211_awdl_dev_stats`** (124 B; len ==124 GET, handler `0xffffff800227d848`)
- `+0x00` u32 `version` `?` (el PM getter solo re-ruta la request: `apple80211RouteInternal(..., 247, ...)`,
  `getAwdlDevStats 0xffffff800221dbfc`)
- `+0x04` u32×29 — contador/estadísticas copiados del estado del chip (Proxi get
  `0xffffff800171b698`: escrituras `*(v18 + 4..72)`, y sumas de dos fuentes del chip en
  +12/+16/+20/+24/+28/+52/+68)
- `+0x4C` u16 — `?` (`+76`)
- `+0x4E` u16 — `?` (`+78`)
- `+0x54`..`+0x73` — `?` (sin escrituras observadas)
- `+0x74` u32 — `?` (`+116`)

**`apple80211_awdl_dfsp_config`** (tamaño `?` >= 22; tipos 262 sin fila en tablas)
- `+0x00` u16 `version` `?`
- `+0x02` u16, `+0x04` u16, `+0x06` u16, `+0x08` u8, `+0x09` u8, `+0x0C` u16, `+0x0E` u32,
  `+0x12` u16, `+0x14` u16 — inicializados a 0 y rellenados por Proxi
  (`getAWDL_DFSP_CONFIG 0xffffff8001728fba`); `setAWDL_DFSP_CONFIG 0xffffff8001728dd0` fuerza
  `+0x0C |= 0xD000` al activar el modo (`"Enabled DFS Proxy Mode."`); nombres `?`

**`apple80211_awdl_cca`** (392 B; len ==392 GET, handler `0xffffff800227f76c`)
- `+0x00` u32 `version`
- `+0x04` u16 `channel` — validado <= 0x10 y != 0 por Proxi (`getAWDL_CCA 0xffffff800171ba8e`)
- `+0x1E` u8.. — tabla de resultados CCA por canal (el PM recorre `LOBYTE(a2[v11/2 + 15])` con
  índice de canal, `getAwdlCCA 0xffffff800221dc36`)
- resto de offsets `?`

**`apple80211_awdl_cap`** (tamaño `?`; tipo 341 sin fila en tablas)
- `+0x00` u16 `version` `?`
- `+0x02` u16 — copiado del estado del chip (`getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES
  0xffffff800171ed38`)
- `+0x04` u8 — `?`

**`apple80211_llw_params`** (44 B; len ==44, handlers `0xffffff800227f903`/`0xffffff800228c58b`)
- `+0x00` u32 `version`
- `+0x04` u32 — base de la conversión a iovar (`v9 = (_DWORD *)(a2 + 4)`,
  `setLLW_PARAMS 0xffffff8001726ac8`); `?`
- `+0x08` u16 — `?` (copiado a iovar+4)
- `+0x0A` u8 — `?` (iovar+8/+15)
- `+0x0B` u8 `enable` — `*(_BYTE *)(a2 + 11)` decide la rama de set (`0xffffff8001726ac8`) y el get
  lo escribe como bool del subcmd (`getLLW_PARAMS 0xffffff800172753e`: `*(_BYTE *)(a2 + 11) =
  (*(DWORD *)v28 + 2) != 0`)
- `+0x0E` u16 — `?` (iovar+6)
- `+0x10` u16 — `?` (iovar+8)
- `+0x12` u16 — campo de bits (set descompone bits 1/2/4 y >>3 hacia iovar; get los recompone)
- `+0x14` u8 — nibble bajo hacia iovar+13 (`*(_BYTE *)(a2 + 20) & 0xF`)
- `+0x15` u8, `+0x16` u8 — hacia iovar+16/+17
- `+0x18` u8 — campo de bits (get: `v36 = v28[7] & 1 | *(_BYTE *)(a2 + 24) & 0xFE`)
- `+0x19`..`+0x2B` — `?`
- La ruta de fondo es el subconjunto hp2p (`runHP2PCommand`, logs `"hp2p get/set iovar failed"`,
  `"Unexpected hp2p subcmd resp id"`).

**`apple80211_tx_rate`** (16 B; len ==16 solo SET, handler `0xffffff800228d772`)
- `+0x00` u32 `version`
- `+0x04` u32 — `?` (Proxi `setTX_RATE 0xffffff8001720c20`: `v13[1] = *(_DWORD *)(a2 + 4)`)
- `+0x08` u16 — `?`
- `+0x0C` u16 — `?` (escrito también por `getTX_RATE 0xffffff8001720ade`)
- `+0x0E` 2 pad
- El PM solo re-ruta (`setAwdlTxRate 0xffffff800221e112`, tipo 411 interno).

**`apple80211_awdl_peer_traffic_registration`** (184 B = 0xB8; len ==184 solo SET, handler
`0xffffff8002289465`)
- `+0x00` u32 `version` — validado (`setAwdlPeerTrafficRegistration 0xffffff800220946a`:
  `"Rejecting Traffic Reg with invalid version: %d"`)
- `+0x04` 6 `mac` ether_addr — peer afectado; si es la dirección nula se resuelve por otro camino
  (`memcmp(a2 + 4, &null_address, 6)`, `IO80211PeerManager::findPeer(a2 + 4)`)
- `+0x0A` u16 — `?` (copiado como `LOWORD(v166)` en la rama airplay)
- `+0x0C` u32 `uuid_len` — obligatorio != 0 (`"UUID Has length == 0, incorrect, failing traffic reg"`)
- `+0x10` char `uuid[]` — string de servicio; el PM hace `strncmp` contra `rtmode`, `catspolicy`,
  `CARPLAY_UUID`, `FACETIME_UUID`, `wifid-assisted-discovery`, `data-assisted-discovery`,
  `ranging-responder`, `ranging-Intiator`, `airplay-sink`, `avconference`, `WiFiCallingUUID`,
  `A2CB4344-1CC7-47EF-8D25-FB68B047620F`, `D2DMigration`, `timeSync`, `airplay-connectivity-check`,
  `copresence`, `airdrop`, `ensemble`, `MPRemoteCamera`, `EdgeKit`, `spatialStreaming`,
  `airplay`, `sidecar`, `MacVirtualDisplay` (todos en `setAwdlPeerTrafficRegistration`)
- `+0x8F` u8 — puesto a 0 por el PM (`a2[143] = 0`)
- `+0x90` u32, `+0x94` u32 — `?` (`*((_DWORD *)a2 + 36)` / `+37`)
- `+0xA8` u32 `enable` — `*((_DWORD *)a2 + 42)`; decide alta/baja del RTG (`!= 0` → registrar)
- `+0xAC` u32 — `*((_DWORD *)a2 + 43)`; `?` (logueado junto al uuid)
- `+0xB0` u16 — `*((_WORD *)a2 + 88)`; `?`
- `+0xB2` u16 — `*((_WORD *)a2 + 89)`; `?`
- `+0xB4` 4 resto `?`
- El PM persiste los 184 B en su ivar +16644 (`memmove((char *)pm + 16644, a2, 0xB8)`); Proxi
  reconoce `CARPLAY_UUID`/`airplay`/`sidecar`/`airplay-connectivity-check` y procesa `enable`
  (`setAWDL_PEER_TRAFFIC_REGISTRATION 0xffffff800171fcaa`).

**`apple80211_companion_skywalk_if_create`** — ver §3.4.

## 5. Structs solo PeerManager (camino moderno, sin slot de superficie)

| tipo GET/SET | IOC | struct | len GET/SET | PM GET / PM SET |
|---|---|---|---|---|
| —/120 | AWDL_PEER_SERVICE_REQUEST | `apple80211_peer_service_request` | ==20(&&data) / 20? | `0xffffff800222be4a` / `0xffffff8002218d98` |
| 127/127 | AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT | `apple80211_awdl_availability_window_ap_alignment` | ==16 / ==16 | `0xffffff800221f78a` / `0xffffff800221cc4c` |
| 128/128 | AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT | `apple80211_awdl_sync_frame_ap_alignment` | ==12 / ==12 | `0xffffff800221f7b0` / `0xffffff800221cd78` |
| 119/119 | AWDL_SERVICE_PARAMS | `apple80211_service_parameters` | ==44 / ==44 | `0xffffff800221f43e` / `0xffffff800221c7f4` |
| 131/131 | AWDL_OUI | `apple80211_awdl_oui` | ==8 / ==8 | `0xffffff800221fac6` / `0xffffff800221d25c` |
| 133/133 | AWDL_TOP_MASTER | `apple80211_awdl_top_master` | ==16 / ==16 | `0xffffff800221fce4` / `0xffffff800221e2e8` |
| 138/138 | AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME | `apple80211_awdl_sync_frame_packet_lifetime` | ==8 / ==8 | `0xffffff800221fe1e` / `0xffffff800221e8b6` |
| 139/139 | AWDL_MASTER_MODE_SYNC_FRAME_PERIOD | `apple80211_awdl_master_sync_frame_period` | ==8 / ==8 | `0xffffff800221fe34` / `0xffffff800221e956` |
| 148/148 | AWDL_STRATEGY | `apple80211_awdl_strategy` | ==24 / ==24 | `0xffffff800221feee` / `0xffffff800221c840` |
| 163/163 | AWDL_SOCIAL_TIME_SLOTS | `apple80211_awdl_social_time_slots` | ==8 / ==8 | `0xffffff800221bfb0` / `0xffffff800221bf26` |
| —/167 | AWDL_FORCED_ROAM_CONFIG | `apple80211_awdl_forced_roam_config` | — / ==16 | — / `0xffffff800221c968` |
| 201/— | AWDL_BTLE_PEER_INDICATION | `apple80211_awdl_btle_peer_indication_data` | ==32 / — | — / `0xffffff800221c400` |
| 202/— | AWDL_BTLE_STATE_PARAMS | `apple80211_awdl_btle_state_params` | ==28 / — | `0xffffff800221c004` |
| 203/— | AWDL_PEER_DATABASE | `apple80211_awdl_peer_database` | ==208480 / — | `0xffffff800221ff6c` |
| 204/— | AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS | `apple80211_awdl_btle_enable_synch_params` | — / ==20 | — / `0xffffff800221c656` |
| 217/217 | AWDL_OPER_MODE | `apple80211_awdl_oper_mode` | ==8 / ==8 | `0xffffff800221eb7a` / `0xffffff800221eb90` |
| 218/— | RESTORE_DEFAULTS | `apple80211_awdl_restore_defaults` | ==4 / — | `0xffffff800221ecf0` |
| 223/— | AWDL_STATISTICS | `apple80211_awdl_statistics` | ==3752 / — | `0xffffff800221d7f6` |
| 233/233 | AWDL_BLOCK_SET_COMMANDS | `apple80211_awdl_block_set_commands` | ==8 / ==8 | `0xffffff800221dd1e` / `0xffffff800221e266` |
| 244/— | AWDL_PREFERRED_CHANNELS | `apple80211_awdl_preferred_channels` | ==12 / — | `0xffffff800221f1fa` |
| —/253 | AWDL_NEARBY_LOG_TRIGGER | `apple80211_awdl_nearby_log_trigger_data` | — / ==8 | — / `0xffffff800221a10c` |
| —/340 | TRAFFIC_REGISTRATION | `apple80211_traffic_registration` | — / ==512 | — / `0xffffff800220c40a` |
| 249/— | AWDL_COMMON_CHANNEL | `apple80211_awdl_common_channel` | ==8 / — | `0xffffff800221fcce` |
| 343/— | AWDL_LOW_LATENCY_STATISTICS | `apple80211_awdl_low_latency_statistics` | ==4024 / — | `0xffffff800221dda0` |
| 351/— | AWDL_SIDECAR_DIAGNOSTICS | `apple80211_awdl_sidecar_diagnostics` | ==80 / — | `0xffffff800221df50` |
| 370/— | AWDL_AIRPLAY_STATISTICS | `apple80211_p2p_airplay_ext_stats` | ==4032 / — | `0xffffff800221de3a` |
| 415/— | POST_PRESENCE_DONE | `apple80211_awdl_ppp_done` | — / ==12 | — / `0xffffff800221c978` |
| 416/416 | AWDL_MI_BITMAP | `apple80211_awdl_mi_bitmap` | ==8 / ==8 | `0xffffff80022207be` / `0xffffff800222075e` |
| 417/— | AWDL_UMI_TX_PEER | `apple80211_awdl_UMI_data` | — / ==16 | — / `0xffffff8002219fca` |
| 419/— | LTE_RESTRICTED_CHANNELS | `apple80211_lte_restricted_channels` | — / ==136 | — / `0xffffff800221e9ea` |
| 420/— | NANPH_SOFTAP_CSA | `apple80211_nanph_softap_csa` | — / ==1 | — / `0xffffff8002219f8c` |
| 162/162 | AWDL_CTL | `apple80211_awdl_ctl` | ==243872 / ==243872 | `0xffffff80021d4e32` / `0xffffff80021cde94` |

Notas de layout de los que aportan campos observados:

**`apple80211_peer_service_request`** (20 B de parte fija + payload; type dump Swift
`awdl_callers_probe.log`)
- `+0x00` u32 `version`
- `+0x04` 6 `peer` ether_addr
- `+0x0A` u8 `delete_request`
- `+0x0B` u8 `aw_offset`
- `+0x0C` u32 `token`
- `+0x10` u16 `data_len`
- `+0x12` u16 `expect_fragmented_response`
- (Swift añade el campo `data` flexible detrás; el handler moderno exige `len == 20` con
  `data != NULL`, `0xffffff800227bd3e`)

**`apple80211_awdl_availability_window_ap_alignment`** (16 B)
- `+0x00` u32 `version`
- `+0x04` u32 — ivar PM +37200 (`getAwdlAvailabilityWindowApAlignment 0xffffff800221f78a`;
  el setter trata -1 como «mantener», `0xffffff800221cc4c`); `?` nombre
- `+0x08` u32 — ivar PM +37196; `?` nombre
- `+0x0C` u32 — 0 en el getter

**`apple80211_awdl_sync_frame_ap_alignment`** (12 B)
- `+0x00` u32 `version`; `+0x04` u32 `?`; `+0x08` u32 `?` (métodos PM sin campos observados en el
  digest; `getAwdlSyncFrameApAlignment 0xffffff800221f7b0`, `setAwdlSyncFrameApAlignment
  0xffffff800221cd78`)

**`apple80211_service_parameters`** (44 B; AWDL_SERVICE_PARAMS, 119)
- `+0x00` u32 `version` `?`
- `+0x04` u16 — ivar PM +43656 (`setAwdlPeerServiceParams 0xffffff800221c7f4`)
- `+0x08` u32 — ivar PM +43660
- `+0x0C` u64 — `?`
- `+0x14` u64 — `?`
- `+0x1C` u64 — `?`
- `+0x24` u64 — ivar PM +43688
- (44 = 0x2C)

**`apple80211_awdl_oui`** (8 B)
- `+0x00` u32 `version`
- `+0x04` 3 `oui[3]` — ivares PM +42424..+42426 (`getAwdlOUI 0xffffff800221fac6` /
  `setAwdlOUI 0xffffff800221d25c`)
- `+0x07` 1 pad

**`apple80211_awdl_top_master`** (16 B)
- `+0x00` u32 `version`
- `+0x04` u32 — `?` (escrito por `getAwdlTopMaster 0xffffff800221fce4`)
- `+0x08` u16 `election_id` — ivar PM +37235 (`*(_WORD *)(a2 + 8) = *(_WORD *)(v2 + 37235)`)
- `+0x0A` 6 pad

**`apple80211_awdl_sync_frame_packet_lifetime`** (8 B)
- `+0x00` u32 `version`; `+0x04` u32 `lifetime` — ivar PM +42420
  (`getAwdlSyncFramePacketLifetime 0xffffff800221fe1e` / `set... 0xffffff800221e8b6`)

**`apple80211_awdl_master_sync_frame_period`** (8 B)
- `+0x00` u32 `version`; `+0x04` u32 `period` — ivar PM +62952
  (`getAwdlMasterSyncFramePeriod 0xffffff800221fe34`)

**`apple80211_awdl_strategy`** (24 B; Swift `{version, strategy: awdlStrategy(UInt32), param, lastStateChange: UInt64}`, metadata `w2p $s3__C24apple80211_awdl_strategyVMa 0x10000d160`)
- `+0x00` u32 `version`
- `+0x04` u32 `strategy` — ivar PM +10464 (`getAwdlStrategy 0xffffff800221feee`)
- `+0x08` u32 `param` — ivar PM +10468
- `+0x10` u64 `lastStateChange` — `*(_QWORD *)(a2 + 16) = v7`
- `+0x18` 4 pad

**`apple80211_awdl_social_time_slots`** (8 B; Swift `{version, enabled: UInt8}`)
- `+0x00` u32 `version`; `+0x04` u8 `enabled` — ivar PM +42660
  (`getAwdlSocialTimeSlots 0xffffff800221bfb0`); `+0x05` 3 pad

**`apple80211_awdl_forced_roam_config`** (16 B; SET 167 `0xffffff80022894ca`, len==16 &&data)
- `+0x00` u32 `version` `?`
- `+0x04` 6 `peer` ether_addr — consumido por `forceRoamToPeersAP(addr, u8, u8)`
  (`0xffffff80021f94ac`, vía `setAwdlForcedRoamConfig 0xffffff800221c968`)
- `+0x0A` 6 `?`

**`apple80211_awdl_btle_peer_indication_data`** (32 B; SET 201 `0xffffff8002289910`, len==32)
- `+0x00` u32 `version` `?`
- `+0x04` u32 `state` — `v6 = *(_DWORD *)(a2 + 4)` (`setAwdlBtlePeerIndication 0xffffff800221c400`)
- `+0x08` 6 `peer` ether_addr — `findPeer(a2 + 8)`
- `+0x0E` 18 resto `?`

**`apple80211_awdl_btle_state_params`** (28 B; GET 202 `0xffffff800227cbb4`, len==28)
- `+0x00` u32 `version` `?`
- `+0x04` u32 — `?` (`getAwdlBtleStateParams 0xffffff800221c004`)
- `+0x08` u16 — ivar PM +37220
- `+0x0A` u16 — ivar PM +43656
- `+0x0C` u32 — ivar PM +16272
- `+0x10` u64 — `?`
- `+0x18` u32 — ivar PM +16284
- `+0x1C` 4 pad

**`apple80211_awdl_peer_database`** (208480 B; GET 203 `0xffffff800227cc16`)
- `+0x00` u32 `version` `?`
- `+0x04` u32 — valor de estado (100 cuando hay límite, `getAwdlPeerDatabase 0xffffff800221ff6c`)
- `+0x327C8` (206408) u32 / `+0x327CC` u16 — cabecera del primer bloque de peer (ivares PM +37216/+37220)
- `+0x32810` (206416) — área de 0x810 B por bloque (`bzero(a2 + 206416, 0x810)`); stride de peer
  `?` (2064 B)
- `+0x329D0` (206913) u8 / `+0x33000` (208461)..`+0x33018` (208472) — campos de cola del volcado
  (ivares PM +16392, +16156, +42568, +16178, +64373, +64396)
- Estructura de peer individual `?` (el método copia bloques y campos sueltos; sin nombres).

**`apple80211_awdl_btle_enable_synch_params`** (20 B; SET 204 `0xffffff8002289972`, len==20)
- `+0x00` u32 `version`; `+0x04` u32 `enable` — ivar PM +16288 como bool
  (`setAwdlBtleEnableSynchWithParams 0xffffff800221c656`); `+0x08` 12 `?`

**`apple80211_awdl_oper_mode`** (8 B; Swift `{version, mode}`, metadata `w2p $s3__C25apple80211_awdl_oper_modeVMa 0x10000cee0`)
- `+0x00` u32 `version`; `+0x04` u32 `mode` — ivar PM +768 (`getAwdlOperatingMode
  0xffffff800221eb7a`); el setter valida el catálogo (`"Unrecognized AWDL operation mode %i"`,
  `0xffffff800221eb90`; tabla de nombres `awdlOperModeDesc` en `syms.txt:6114`)

**`apple80211_awdl_statistics`** (3752 B; GET 223 `0xffffff800227d272`) — volcado íntegro del bloque
de estadísticas del PM: `memmove(a2, pm + 43792, 0xEA8)` (`getAwdlStatistics 0xffffff800221d7f6`);
contenido `?` (blob de ivares).

**`apple80211_awdl_block_set_commands`** (8 B; 233)
- `+0x00` u32 `version`; `+0x04` u32 `mask` `?` (PM re-ruta internamente, `0xffffff800221dd1e`/`e266`)

**`apple80211_awdl_preferred_channels`** (12 B; Swift `{version, preferred: UInt16, master: UInt16, awdl_info_ext: {secondary_preferred: UInt16}}`, metadata `w2p $s3__C34apple80211_awdl_preferred_channelsVMa 0x10000d140`)
- `+0x00` u32 `version`
- `+0x04` u16 `preferred` — `?` (el getter del PM escribe el canal social: 6/11/1/0 en `a2[4]` sobre `_WORD*`)
- `+0x06` u16 `master` — `?`
- `+0x08` u16 `secondary_preferred` — `?`
- `+0x0A` 2 pad
- (`getAwdlPreferredChannels 0xffffff800221f1fa`)

**`apple80211_awdl_nearby_log_trigger_data`** (8 B; SET 253 `0xffffff800228a630`, len==8)
- `+0x00` u32 `version`; `+0x04` u16 `trigger` — ivar PM +64088
  (`setAwdlNearbyLogTrigger 0xffffff800221a10c`); `+0x06` 2 pad

**`apple80211_traffic_registration`** (512 B; SET 340 `0xffffff800228c4a7`) — versión validada por
`setTrafficRegistration 0xffffff800220c40a` (`"Rejecting Traffic Reg with invalid version"`);
campos `?`.

**`apple80211_awdl_common_channel`** (8 B; GET 249 `0xffffff800227d8d4`)
- `+0x00` u32 `version`; `+0x04` u32 `channel` — ivar PM +420 (`getAwdlCommonChannel 0xffffff800221fcce`)

**`apple80211_awdl_low_latency_statistics`** (4024 B; GET 343 `0xffffff800227f89e`) — `memmove(a2,
pm + 47608, 0xFB8)` (`getAwdlLowLatencyStatistics 0xffffff800221dda0`); contenido `?` (el type dump
Swift lo descompone en `configs` + `traffic` con `counts` de 14 u64 e histogramas de tuples
`Double`: `awdl_callers_probe.log:1339-1341`, `1699-1712`).

**`apple80211_awdl_sidecar_diagnostics`** (80 B; GET 351 `0xffffff800227fca6`) — `memmove(a2,
pm + 6658, 0x50)` + campos dinámicos desde vtable del sidecar (`a2[5]`, `a2[6]`, `a2 + 7`,
`getAwdlSidecarDiagnostics 0xffffff800221df50`); desglose `?`.

**`apple80211_p2p_airplay_ext_stats`** (4032 B; GET 370 `0xffffff8002280217`) — `memmove(a2,
pm + 53368, 0xFC0)` y reset del origen (`getAwdlAirplayStatistics 0xffffff800221de3a`); contenido `?`.

**`apple80211_awdl_ppp_done`** (12 B; Swift `{version: UInt32, peerMac: ether_addr}`, metadata
`w2p $s3__C24apple80211_awdl_ppp_doneVMa 0x100007700`)
- `+0x00` u32 `version`; `+0x04` 6 `peerMac` — `findPeer(a2 + 4)`
  (`setAwdlPostPeerPresenceDone 0xffffff800221c978`); `+0x0A` 2 pad

**`apple80211_awdl_mi_bitmap`** (8 B; 416)
- `+0x00` u32 `version`; `+0x04` u32 `bitmap` — `adjustMiBitmap(bitmap, false, 1)`
  (`setAwdlMiBitmap 0xffffff800222075e`; getter `0xffffff80022207be`, ivar PM +42760)

**`apple80211_awdl_UMI_data`** (16 B; Swift `{version, umiOption: UInt32, peer: ether_addr}`, metadata `w2p $s3__C24apple80211_awdl_UMI_dataVMa 0x10000cf50`)
- `+0x00` u32 `version`; `+0x04` u32 `umiOption` — `sendRealTimeUMI(peer, umiOption, 0)`
  (`setAWDLUMIToPeer 0xffffff8002219fca`); `+0x08` 6 `peer` — `findPeer(a2 + 8)`; `+0x0E` 2 pad

**`apple80211_lte_restricted_channels`** (136 B; SET 419 `0xffffff800228da8e`)
- `+0x00` u32 `version` `?`
- `+0x04` u16 `channels[64]` — `memmove(pm + 62620, a2 + 4, 2 * count)`
- `+0x84` u32 `count` — ivar PM +62748 (`setLteRestrictedChannels 0xffffff800221e9ea`)

**`apple80211_nanph_softap_csa`** (1 B; SET 420 `0xffffff800228daf3`, len==1 &&data) — `u8` en +0;
`setSoftApCSANanPh 0xffffff8002219f8c` rechaza si no hay HostAP/AWDL concurrency
(`"HostAP/AWDLConcurrency invalid."`).

**`apple80211_awdl_restore_defaults`** (4 B; GET 218 `0xffffff800227d1ae`) — `u32` en +0 sin
escritura observada (`getAwdlRestoreDefaults 0xffffff800221ecf0`); `?`.

**`apple80211_awdl_ctl`** (len exigida 243872; buffer interno de la ruta 0x3B8A0 = 244128 B, GET
`0xffffff800227c6f9`: `memmove(v6, v9, 0x3B8A0)`)
- `+0x04` char `cmd[128]` — nombre de parámetro (`strnlen(a2+4, 0x80)`); catálogo de nombres en
  `setAwdlCtl 0xffffff80021cde94`: `awdl_bonjour_offload`, `awdlMaxBW`, `multicastTxOffset`,
  `UMITxOffset`, `guardTime`, `awdl_min_rate`, `channelSequenceStrategy`, `scheduleState`,
  `awdlCCSanityFailureEnabled`, `customRTGInfraChannel`, `force6GChannel`, … (más de 60)
- `+0x88` u64 `value` — `*(_QWORD *)(a2 + 136)`
- `+0x3B7D4` (243860) u32 `out` — acumulador de salida (`+= v39` en el get)
- `+0x3B7E0` (243872) u64 `callback` — debe ser puntero de kernel (`>= 0xFFFFFFFFFFFFF000` y
  `>> 39 == 0x1FFFFFF`)
- `+0x3B7E8` (243880) u32 `tag` — `== 1` exigido por ambos métodos
- `+0x3B7EC` (243884) u32 — `?`
- Los campos a partir de +243872 quedan fuera de la `len == 243872` exigida al cliente: los rellena
  la propia ruta interna con el buffer de 0x3B8A0 (`?` mecánica exacta).

## 6. Slots de la superficie AWDL sin request alcanzable

Los tipos 143 (`PEER_CACHE_CONTROL`), 149 (`AWDL_OOB_REQUEST`), 151
(`AWDL_SYNC_FRAME_TEMPLATE`), 158 (`AWDL_DEVICE_CAPABILITIES`), 225
(`AWDL_OOB_AUTO_REQUEST`), 262/263 (`AWDL_DFSP_*`), 341 (`AWDL_CAP`), 342
(`AWDL_OOB_SEC_PAYLOAD_REQUEST`), 414 (`P2P_COEX_PROTECTION`), 418 (`PSF_DWELL`), 432
(`AWDL_CONFIG`) y 467 (`AWDL_RTG_PEER_STATS_CONFIG`) tienen entrada 0 en `gGetHandlerTable` y
`gSetHandlerTable` (leída en `kc_tables.txt`: `type 143 slot ffffff800245efd0 -> 0`, etc.). Un
cliente de usuario que los envíe recibe 102 sin llegar al driver. Los slots Broadcom
correspondientes se ejercen por la ruta interna (glue WCL → puentes → slots; los puentes tienen
fila en `gInternalGet/SetHandlerTable`, `AWDL_APPLE80211_DISPATCH.md` §6) o internamente por el
propio driver. Campos observados en Proxi para los que aportan algo:

- `apple80211_awdl_oob_request` (`setAWDL_OOB_REQUEST 0xffffff8001726474` y
  `setAWDL_OOB_AUTO_REQUEST 0xffffff8001727c0e`): `+0x04` u32, `+0x08` u16, `+0x14` u16, `+0x16`
  u32, `+0x1A` u16, `+0x28` u16 `len`, `+0x30` u64 `ptr` a datos (el Proxi hace
  `memmove(dst, *v11, v11[1])` con puntero+longitud tomados del struct); tamaño `?`.
- `apple80211_awdl_sync_frame_template` (`setAWDL_SYNC_FRAME_TEMPLATE 0xffffff800171da80`):
  `+0x04` u32 (0 = `?`), `+0x08` u64 `ptr`; tamaño `?`.
- `apple80211_p2p_coex_protection` (`setP2P_COEX_PROTECTION 0xffffff800172a968`): `+0x04` u8
  `len1` + datos en `+0x05`; `+0x4A` (74) u8 `len2` + datos en `+0x4B`; tamaño `?`.
- `apple80211_awdl_af_sec_payload_request` (`setAWDL_OOB_SEC_PAYLOAD_REQUEST 0xffffff8001727d56`):
  `+0x02` u16, `+0x04` u32, `+0x08` u64 `ptr`; tamaño `?`.
- `apple80211_rtg_peer_stats_cfg` (`setAWDL_RTG_PEER_STATS_CONFIG 0xffffff800171fb6c`): `+0x01`
  u8, `+0x02` u16, `+0x04` 6 `peer` ether_addr, `+0x0A` u8, `+0x0C` u32; tamaño `?`.
- `apple80211_peer_cache_control` (`setPEER_CACHE_CONTROL 0xffffff80017235ce`): opcode `?` con
  validación `"invalid opcode %u"` y `memmove` de 0x1D0 B; tamaño `?`.
- `apple80211_awdl_config_request` (`setAWDL_CONFIG 0xffffff80017215e2`): `+0x04` u32 (se le aplica
  `| 3`); tamaño `?`.
- `apple80211_awdl_psf_dwell_params` (`setPSF_DWELL 0xffffff800171dfc0`): sin campos observados
  (ruta a commander); el equivalente del PM recibe `psfDwell` por valor
  (`setPSFDwellE8psfDwell`, `syms.txt:13780`); tamaño `?`.
- `apple80211_awdl_dfsp_ucsa_config` (`setAWDL_DFSP_UCSA_CONFIG 0xffffff800172919c`): sin campos
  observados; tamaño `?`.

## 7. Definiciones C

Los tamaños con `static_assert` tienen el `len == N` verificado en el handler moderno o
equivalencia exacta con el buffer del puente. Los campos `fN`/`pad` con `?` = significado no
determinado; los offsets sí están verificados salvo indicación.

```c
#include <stdint.h>
#include <stddef.h>
#include <sys/_types/_u_int16_t.h>

struct ether_addr { uint8_t octet[6]; };

/* 1. Cabecera de petición (ioctl SIOCGA80211/SIOCSA80211, _IOWR('i',200/201,40)) */
struct apple80211req {
    char     ifname[16];     /* +0x00: interfaz destino                    */
    uint32_t req_type;       /* +0x10: tipo 1..586                         */
    uint32_t val;            /* +0x14: auxiliar (0 en wifip2pd)            */
    uint32_t req_len;        /* +0x18: longitud del buffer de datos        */
    void    *req_data;       /* +0x20: buffer                              */
};
_Static_assert(sizeof(struct apple80211req) == 40, "apple80211req");

/* 3.1 CARD_CAPABILITIES (GET 12) */
struct apple80211_capability_data {
    uint32_t version;        /* +0x00: 1 en Broadcom                       */
    uint8_t  flags[24];      /* +0x04: flags[4] bit0 = puerta AWDL, flags[9] bit0 = puerta NAN */
};
_Static_assert(sizeof(struct apple80211_capability_data) == 28, "capability_data");
/* copyback del handler: min(req_len, 21) bytes desde &flags[0]            */

/* 3.2 VIRTUAL_IF_CREATE (SET 94) */
struct apple80211_virt_if_create_data {
    uint32_t f0;             /* +0x00: enviado por wifip2pd, lectura ?     */
    uint8_t  mac[6];         /* +0x04: MAC solicitada                      */
    uint8_t  pad0[2];        /* +0x0A                                      */
    uint32_t role;           /* +0x0C: 6=AWDL, 9/0xA=?, 7=APSTA, 8..10=NAN */
    char     ifname[16];     /* +0x10: salida, nombre BSD creado           */
};
_Static_assert(sizeof(struct apple80211_virt_if_create_data) == 32, "virt_if_create_data");

/* 3.4 COMPANION_SKYWALK_IF_CREATE (SET 293) */
struct apple80211_companion_skywalk_if_create {
    uint32_t code;           /* +0x00: codigo companion                    */
    uint32_t op;             /* +0x04: 1=crear, resto=borrar (?)           */
    char     name[16];       /* +0x08: salida, nombre BSD (llw0/llw1)      */
};
_Static_assert(sizeof(struct apple80211_companion_skywalk_if_create) == 24, "companion");

/* 4. SSID (GET/SET 1) */
struct apple80211_ssid_data {
    uint32_t version;        /* +0x00                                      */
    uint32_t ssid_len;       /* +0x04                                      */
    uint8_t  ssid[32];       /* +0x08: "awdl\0" en Broadcom AWDL           */
};
_Static_assert(sizeof(struct apple80211_ssid_data) == 40, "ssid_data");

/* 4. CHANNEL (GET/SET 4) */
struct apple80211_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04                                      */
    uint32_t flags;          /* +0x08                                      */
};
struct apple80211_channel_data {
    uint32_t version;        /* +0x00: ? en Broadcom AWDL (stub)           */
    struct apple80211_channel channel; /* +0x04                            */
};
_Static_assert(sizeof(struct apple80211_channel_data) == 16, "channel_data");

/* 4. BSSID (GET 9) */
struct apple80211_bssid_data {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr bssid; /* +0x04                                      */
    uint8_t  pad0[2];        /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_bssid_data) == 12, "bssid_data");

/* 4. STATE (GET 13) */
struct apple80211_state_data {
    uint32_t version;        /* +0x00                                      */
    uint32_t state;          /* +0x04: 4 en Broadcom AWDL                  */
};
_Static_assert(sizeof(struct apple80211_state_data) == 8, "state_data");

/* 4. AWDL_SYNC_PARAMS (GET/SET 116) */
struct apple80211_awdl_sync_params {
    uint32_t version;        /* +0x00                                      */
    uint32_t aw_cmn_length;  /* +0x04                                      */
    uint32_t f8;             /* +0x08: ?                                   */
    uint32_t aw_ext_length;  /* +0x0C                                      */
    uint32_t action_frame_period; /* +0x10                                  */
    uint32_t f20;            /* +0x14: cache ivar Proxi +0x29C             */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_params) == 24, "awdl_sync_params");

/* 4. AWDL_SYNC_ENABLED (GET/SET 117) */
struct apple80211_awdl_sync_enabled {
    uint32_t version;        /* +0x00                                      */
    uint32_t flags;          /* +0x04                                      */
    uint32_t enabled;        /* +0x08                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_enabled) == 12, "awdl_sync_enabled");

/* 4. AWDL_EXTENSION_STATE_MACHINE_PARAMETERS (GET/SET 118) */
struct apple80211_awdl_extension_state_machine_parameter {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: valor efectivo u8                   */
    uint32_t f8;             /* +0x08: valor efectivo u8                   */
    uint32_t f12;            /* +0x0C: valor efectivo u8                   */
    uint32_t f16;            /* +0x10: valor efectivo u8                   */
};
_Static_assert(sizeof(struct apple80211_awdl_extension_state_machine_parameter) == 20, "ext_params");

/* 4. AWDL_ELECTION_ID (GET/SET 122) */
struct apple80211_awdl_election_id {
    uint32_t version;        /* +0x00                                      */
    uint32_t election_id;    /* +0x04: valor efectivo u16                  */
};
_Static_assert(sizeof(struct apple80211_awdl_election_id) == 8, "election_id");

/* 4. AWDL_BSSID (SET 125) */
struct apple80211_awdl_bssid {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr bssid; /* +0x04                                      */
    struct ether_addr f10;   /* +0x0A: segunda MAC, ?                      */
};
_Static_assert(sizeof(struct apple80211_awdl_bssid) == 16, "awdl_bssid");

/* 4. AWDL_ELECTION_METRIC (GET/SET 126) */
struct apple80211_awdl_election_metric {
    uint32_t version;        /* +0x00                                      */
    uint32_t metric;         /* +0x04                                      */
    uint8_t  flag;           /* +0x08: !=0 => PM mantiene su metrica       */
    uint8_t  pad0[3];        /* +0x09                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_election_metric) == 12, "election_metric");

/* 4. AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE (GET/SET 129) */
struct apple80211_awdl_sync_channel_sequence {
    uint32_t version;        /* +0x00                                      */
    uint8_t  f4;             /* +0x04: ?                                   */
    uint8_t  count;          /* +0x05                                      */
    uint8_t  dfs;            /* +0x06                                      */
    uint8_t  f7;             /* +0x07: ?                                   */
    uint8_t  f8;             /* +0x08: ?                                   */
    uint8_t  f9;             /* +0x09: ?                                   */
    uint8_t  pad0[6];        /* +0x0A                                      */
    uint16_t entries[];      /* +0x10: canales (flexible; bloque = 0x190)  */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_channel_sequence) == 16, "chanseq hdr");

/* 4. PEER_CACHE_MAXIMUM_SIZE (GET 130) */
struct apple80211_peer_cache_maximum_size {
    uint32_t version;        /* +0x00                                      */
    uint32_t size;           /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_peer_cache_maximum_size) == 8, "pcms");

/* 4. AWDL_MASTER_CHANNEL (GET/SET 132) */
struct apple80211_awdl_master_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_master_channel) == 8, "master_channel");

/* 4. AWDL_SYNC_STATE (GET/SET 134) */
struct apple80211_awdl_sync_state {
    uint32_t version;        /* +0x00: 1 al escribir                       */
    uint32_t state;          /* +0x04                                      */
    uint32_t f8;             /* +0x08: ?                                   */
    uint16_t f12;            /* +0x0C: ?                                   */
    uint16_t pad0;           /* +0x0E                                      */
    uint32_t f16;            /* +0x10: ?                                   */
    uint8_t  f20;            /* +0x14: ?                                   */
    uint8_t  pad1[3];        /* +0x15                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_state) == 24, "sync_state");

/* 4. AWDL_ELECTION_RSSI_THRESHOLDS (GET/SET 135) */
struct apple80211_awdl_election_rssi_thresholds {
    uint32_t version;        /* +0x00                                      */
    int8_t   t1;             /* +0x04: sbyte en hueco de 4 (PM signext)    */
    uint8_t  pad0[3];        /* +0x05                                      */
    int8_t   t2;             /* +0x08                                      */
    uint8_t  pad1[3];        /* +0x09                                      */
    int8_t   t3;             /* +0x0C                                      */
    uint8_t  pad2[3];        /* +0x0D                                      */
    int8_t   t4;             /* +0x10                                      */
    uint8_t  pad3[3];        /* +0x11                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_election_rssi_thresholds) == 20, "rssi_thr");

/* 4. AWDL_PRESENCE_MODE (GET/SET 136) */
struct apple80211_awdl_presence_mode {
    uint32_t version;        /* +0x00                                      */
    uint32_t mode;           /* +0x04: <= 0x3F                             */
};
_Static_assert(sizeof(struct apple80211_awdl_presence_mode) == 8, "presence_mode");

/* 4. AWDL_ELECTION_PARAMS (GET/SET 155) */
struct apple80211_awdl_election_params {
    uint32_t version;        /* +0x00: 1 al escribir                       */
    uint8_t  f4[12];         /* +0x04: ?                                   */
    uint8_t  f16;            /* +0x10                                      */
    uint8_t  pad17;          /* +0x11                                      */
    uint16_t f18;            /* +0x12                                      */
    uint8_t  f20;            /* +0x14                                      */
    uint8_t  pad21;          /* +0x15                                      */
    uint32_t f22;            /* +0x16 (no alineado)                        */
    uint16_t f26;            /* +0x1A                                      */
    uint32_t f28;            /* +0x1C                                      */
    uint32_t f32;            /* +0x20                                      */
    uint32_t election_id;    /* +0x24                                      */
    uint8_t  f40[16];        /* +0x28: ?                                   */
} __attribute__((packed));   /* u32 en +0x16 sin alinear en el kernel      */
_Static_assert(sizeof(struct apple80211_awdl_election_params) == 56, "election_params");

/* 4. AWDL_SECONDARY_MASTER_CHANNEL (GET/SET 205) */
struct apple80211_awdl_secondary_master_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04: valor efectivo u8                   */
};
_Static_assert(sizeof(struct apple80211_awdl_secondary_master_channel) == 8, "sec_master");

/* 4. AWDL_AF_TX_MODE (GET/SET 208) */
struct apple80211_awdl_af_tx_mode {
    uint32_t version;        /* +0x00                                      */
    uint32_t mode;           /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_af_tx_mode) == 8, "af_tx_mode");

/* 4. AWDL_MIN_RATE (GET/SET 213) */
struct apple80211_awdl_min_rate {
    uint32_t version;        /* +0x00                                      */
    uint8_t  rate;           /* +0x04                                      */
    uint8_t  pad0[3];        /* +0x05                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_min_rate) == 8, "min_rate");

/* 4. AWDL_ENCRYPTION_TYPE (GET/SET 220) */
struct apple80211_encrypt {
    uint32_t version;        /* +0x00                                      */
    uint32_t encrypt_type;   /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_encrypt) == 8, "encrypt");

/* 4. AWDL_TXCAL_PERIOD (GET/SET 226, ruta interna) */
struct apple80211_awdl_txcal_period {
    uint32_t version;        /* +0x00                                      */
    uint32_t period;         /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_txcal_period) == 8, "txcal_period");

/* 4. AWDL_AWDL_ADVERTISERS (SET 229) */
struct awdl_peer_advert_add {
    uint16_t aw_counter;         /* +0x00                                  */
    uint16_t tx_counter;         /* +0x02                                  */
    uint16_t tx_delay;           /* +0x04                                  */
    uint16_t period_tu;          /* +0x06                                  */
    uint16_t aw_cmn_length;      /* +0x08                                  */
    uint16_t aw_ext_length;      /* +0x0A                                  */
    uint16_t aw_period;          /* +0x0C                                  */
    int8_t   rssi;               /* +0x0E                                  */
    uint8_t  guard_time;         /* +0x0F                                  */
    uint8_t  presence_mode;      /* +0x10                                  */
    uint8_t  age;                /* +0x11                                  */
    uint8_t  dist_top;           /* +0x12                                  */
    uint8_t  pad0;               /* +0x13                                  */
    uint32_t ms;                 /* +0x14                                  */
    uint32_t self_metrics;       /* +0x18                                  */
    uint32_t top_master_metrics; /* +0x1C                                  */
    struct ether_addr top_master;/* +0x20                                  */
    struct ether_addr addr;      /* +0x26                                  */
};
_Static_assert(sizeof(struct awdl_peer_advert_add) == 44, "peer_advert_add");

/* 4. AWDL_CCA (GET 338) */
struct apple80211_awdl_cca {
    uint32_t version;        /* +0x00                                      */
    uint16_t channel;        /* +0x04: <= 0x10                             */
    uint8_t  rest[386];      /* +0x06: tabla CCA desde +0x1E; desglose ?   */
};
_Static_assert(sizeof(struct apple80211_awdl_cca) == 392, "awdl_cca");

/* 4. LLW_PARAMS (GET/SET 344) */
struct apple80211_llw_params {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t f8;             /* +0x08: ?                                   */
    uint8_t  f10;            /* +0x0A: ?                                   */
    uint8_t  enable;         /* +0x0B                                      */
    uint8_t  pad0[2];        /* +0x0C                                      */
    uint16_t f14;            /* +0x0E: ?                                   */
    uint16_t f16;            /* +0x10: ?                                   */
    uint16_t f18;            /* +0x12: campo de bits                       */
    uint8_t  f20;            /* +0x14: nibble bajo                         */
    uint8_t  f21;            /* +0x15: ?                                   */
    uint8_t  f22;            /* +0x16: ?                                   */
    uint8_t  pad1;           /* +0x17                                      */
    uint8_t  f24;            /* +0x18: campo de bits                       */
    uint8_t  rest[19];       /* +0x19: ?                                   */
};
_Static_assert(sizeof(struct apple80211_llw_params) == 44, "llw_params");

/* 4. TX_RATE (SET 411) */
struct apple80211_tx_rate {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t f8;             /* +0x08: ?                                   */
    uint16_t f12;            /* +0x0C: ?                                   */
    uint16_t pad0;           /* +0x0E                                      */
};
_Static_assert(sizeof(struct apple80211_tx_rate) == 16, "tx_rate");

/* 4. AWDL_PEER_TRAFFIC_REGISTRATION (SET 164) */
struct apple80211_awdl_peer_traffic_registration {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr mac;   /* +0x04: peer (nula = otro criterio)         */
    uint16_t f10;            /* +0x0A: ?                                   */
    uint32_t uuid_len;       /* +0x0C: != 0 obligatorio                    */
    char     uuid[127];      /* +0x10: nombre de servicio (strncmp)        */
    uint8_t  f143;           /* +0x8F: el PM lo pone a 0                   */
    uint32_t f144;           /* +0x90: ?                                   */
    uint32_t f148;           /* +0x94: ?                                   */
    uint8_t  rest[16];       /* +0x98..+0xA7: sin escrituras observadas ?  */
    uint32_t enable;         /* +0xA8                                      */
    uint32_t f172;           /* +0xAC: ?                                   */
    uint16_t f176;           /* +0xB0: ?                                   */
    uint16_t f178;           /* +0xB2: ?                                   */
    uint8_t  rest2[4];       /* +0xB4                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_peer_traffic_registration) == 184, "traffic_reg");

/* 5. AWDL_PEER_SERVICE_REQUEST (GET/SET 120) */
struct apple80211_peer_service_request {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr peer;  /* +0x04                                      */
    uint8_t  delete_request; /* +0x0A                                      */
    uint8_t  aw_offset;      /* +0x0B                                      */
    uint32_t token;          /* +0x0C                                      */
    uint16_t data_len;       /* +0x10                                      */
    uint16_t expect_fragmented_response; /* +0x12                          */
    /* sigue el payload flexible (Swift: campo `data`)                       */
};
_Static_assert(sizeof(struct apple80211_peer_service_request) == 20, "peer_service_request");

/* 5. AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT (GET/SET 127) */
struct apple80211_awdl_availability_window_ap_alignment {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ivar PM +37200; -1 = mantener       */
    uint32_t f8;             /* +0x08: ivar PM +37196                      */
    uint32_t f12;            /* +0x0C: 0 en el getter                      */
};
_Static_assert(sizeof(struct apple80211_awdl_availability_window_ap_alignment) == 16, "aw_ap_align");

/* 5. AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT (GET/SET 128) */
struct apple80211_awdl_sync_frame_ap_alignment {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint32_t f8;             /* +0x08: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_frame_ap_alignment) == 12, "sf_ap_align");

/* 5. AWDL_SERVICE_PARAMS (GET/SET 119) */
struct apple80211_service_parameters {
    uint32_t version;        /* +0x00: ?                                   */
    uint16_t f4;             /* +0x04                                      */
    uint16_t pad0;           /* +0x06                                      */
    uint32_t f8;             /* +0x08                                      */
    uint64_t f12;            /* +0x0C                                      */
    uint64_t f20;            /* +0x14                                      */
    uint64_t f28;            /* +0x1C                                      */
    uint64_t f36;            /* +0x24                                      */
} __attribute__((packed));   /* u64 en +0x0C sin alinear en el kernel      */
_Static_assert(sizeof(struct apple80211_service_parameters) == 44, "service_parameters");

/* 5. AWDL_OUI (GET/SET 131) */
struct apple80211_awdl_oui {
    uint32_t version;        /* +0x00                                      */
    uint8_t  oui[3];         /* +0x04                                      */
    uint8_t  pad0;           /* +0x07                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_oui) == 8, "awdl_oui");

/* 5. AWDL_TOP_MASTER (GET/SET 133) */
struct apple80211_awdl_top_master {
    uint32_t version;        /* +0x00                                      */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t election_id;    /* +0x08                                      */
    uint8_t  pad0[6];        /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_top_master) == 16, "top_master");

/* 5. AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME (GET/SET 138) */
struct apple80211_awdl_sync_frame_packet_lifetime {
    uint32_t version;        /* +0x00                                      */
    uint32_t lifetime;       /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_sync_frame_packet_lifetime) == 8, "psf_lifetime");

/* 5. AWDL_MASTER_MODE_SYNC_FRAME_PERIOD (GET/SET 139) */
struct apple80211_awdl_master_sync_frame_period {
    uint32_t version;        /* +0x00                                      */
    uint32_t period;         /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_master_sync_frame_period) == 8, "msfp");

/* 5. AWDL_STRATEGY (GET/SET 148) */
struct apple80211_awdl_strategy {
    uint32_t version;        /* +0x00                                      */
    uint32_t strategy;       /* +0x04                                      */
    uint32_t param;          /* +0x08                                      */
    uint32_t pad0;           /* +0x0C                                      */
    uint64_t lastStateChange;/* +0x10                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_strategy) == 24, "awdl_strategy");

/* 5. AWDL_SOCIAL_TIME_SLOTS (GET/SET 163) */
struct apple80211_awdl_social_time_slots {
    uint32_t version;        /* +0x00                                      */
    uint8_t  enabled;        /* +0x04                                      */
    uint8_t  pad0[3];        /* +0x05                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_social_time_slots) == 8, "social_time_slots");

/* 5. AWDL_FORCED_ROAM_CONFIG (SET 167) */
struct apple80211_awdl_forced_roam_config {
    uint32_t version;        /* +0x00: ?                                   */
    struct ether_addr peer;  /* +0x04                                      */
    uint8_t  f10[6];         /* +0x0A: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_forced_roam_config) == 16, "forced_roam");

/* 5. AWDL_BTLE_STATE_PARAMS (GET 202) */
struct apple80211_awdl_btle_state_params {
    uint32_t version;        /* +0x00: ?                                   */
    uint32_t f4;             /* +0x04: ?                                   */
    uint16_t f8;             /* +0x08                                      */
    uint16_t f10;            /* +0x0A                                      */
    uint32_t f12;            /* +0x0C                                      */
    uint64_t f16;            /* +0x10                                      */
    uint32_t f24;            /* +0x18                                      */
} __attribute__((packed));   /* sin redondeo a 8 (sizeof = 28)             */
_Static_assert(sizeof(struct apple80211_awdl_btle_state_params) == 28, "btle_state");

/* 5. AWDL_BTLE_PEER_INDICATION (SET 201) */
struct apple80211_awdl_btle_peer_indication_data {
    uint32_t version;        /* +0x00: ?                                   */
    uint32_t state;          /* +0x04                                      */
    struct ether_addr peer;  /* +0x08                                      */
    uint8_t  rest[18];       /* +0x0E: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_btle_peer_indication_data) == 32, "btle_peer_ind");

/* 5. AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS (SET 204) */
struct apple80211_awdl_btle_enable_synch_params {
    uint32_t version;        /* +0x00                                      */
    uint32_t enable;         /* +0x04                                      */
    uint8_t  rest[12];       /* +0x08: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_btle_enable_synch_params) == 20, "btle_sync");

/* 5. AWDL_OPER_MODE (GET/SET 217) */
struct apple80211_awdl_oper_mode {
    uint32_t version;        /* +0x00                                      */
    uint32_t mode;           /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_oper_mode) == 8, "oper_mode");

/* 5. AWDL_BLOCK_SET_COMMANDS (GET/SET 233) */
struct apple80211_awdl_block_set_commands {
    uint32_t version;        /* +0x00                                      */
    uint32_t mask;           /* +0x04: ?                                   */
};
_Static_assert(sizeof(struct apple80211_awdl_block_set_commands) == 8, "block_set");

/* 5. AWDL_PREFERRED_CHANNELS (GET 244) */
struct apple80211_awdl_info_ext {
    uint16_t secondary_preferred;    /* type dump Swift                    */
};
struct apple80211_awdl_preferred_channels {
    uint32_t version;        /* +0x00                                      */
    uint16_t preferred;      /* +0x04                                      */
    uint16_t master;         /* +0x06                                      */
    struct apple80211_awdl_info_ext awdl_info_ext; /* +0x08                */
    uint16_t pad0;           /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_preferred_channels) == 12, "pref_chan");

/* 5. AWDL_NEARBY_LOG_TRIGGER (SET 253) */
struct apple80211_awdl_nearby_log_trigger_data {
    uint32_t version;        /* +0x00                                      */
    uint16_t trigger;        /* +0x04                                      */
    uint8_t  pad0[2];        /* +0x06                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_nearby_log_trigger_data) == 8, "nearby_log");

/* 5. AWDL_COMMON_CHANNEL (GET 249) */
struct apple80211_awdl_common_channel {
    uint32_t version;        /* +0x00                                      */
    uint32_t channel;        /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_common_channel) == 8, "common_channel");

/* 5. POST_PRESENCE_DONE (SET 415) */
struct apple80211_awdl_ppp_done {
    uint32_t version;        /* +0x00                                      */
    struct ether_addr peerMac; /* +0x04                                    */
    uint8_t  pad0[2];        /* +0x0A                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_ppp_done) == 12, "ppp_done");

/* 5. AWDL_MI_BITMAP (GET/SET 416) */
struct apple80211_awdl_mi_bitmap {
    uint32_t version;        /* +0x00                                      */
    uint32_t bitmap;         /* +0x04                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_mi_bitmap) == 8, "mi_bitmap");

/* 5. AWDL_UMI_TX_PEER (SET 417) */
struct apple80211_awdl_UMI_data {
    uint32_t version;        /* +0x00                                      */
    uint32_t umiOption;      /* +0x04                                      */
    struct ether_addr peer;  /* +0x08                                      */
    uint8_t  pad0[2];        /* +0x0E                                      */
};
_Static_assert(sizeof(struct apple80211_awdl_UMI_data) == 16, "umi_data");

/* 5. LTE_RESTRICTED_CHANNELS (SET 419) */
struct apple80211_lte_restricted_channels {
    uint32_t version;        /* +0x00: ?                                   */
    uint16_t channels[64];   /* +0x04                                      */
    uint32_t count;          /* +0x84                                      */
};
_Static_assert(sizeof(struct apple80211_lte_restricted_channels) == 136, "lte_restricted");

/* 5. NANPH_SOFTAP_CSA (SET 420) */
typedef uint8_t apple80211_nanph_softap_csa;    /* len == 1               */

/* 5. AWDL_CTL (GET/SET 162; buffer interno 0x3B8A0) */
struct apple80211_awdl_ctl {
    uint32_t version;        /* +0x00: ?                                   */
    char     cmd[128];       /* +0x04: nombre de parametro                 */
    uint8_t  pad0[4];        /* +0x84                                      */
    uint64_t value;          /* +0x88 (136)                                */
    uint8_t  mid[243716];    /* +0x90..+0x3B7D3: area de trabajo ?         */
    uint32_t out;            /* +0x3B7D4 (243860): acumulador de salida    */
    uint8_t  pad1[8];        /* +0x3B7D8                                   */
    void    *callback;       /* +0x3B7E0 (243872): ptr kernel exigido      */
    uint32_t tag;            /* +0x3B7E8 (243880): == 1                    */
    uint32_t f243884;        /* +0x3B7EC: ?                                */
};
_Static_assert(sizeof(struct apple80211_awdl_ctl) == 243888, "awdl_ctl");
/* NOTA: la len exigida al cliente es 243872; los campos callback/tag/f    */
/* quedan detras y los rellena la ruta interna (buffer 0x3B8A0).           */
```

## 8. Método y límites

- Decompilación headless (Hex-Rays, `idat -A -S`) sobre copias de los IDB en
  `Build/ida_awdl/task_awdl_structs/kc/` (kernel) y `/p2p/` (wifip2pd); volcados:
  `kc_proxi.txt` (64 métodos ProximityInterface), `kc_pm_get.txt`/`kc_pm_set.txt` (101 métodos de
  `IO80211AWDLPeerManager`), `kc_handlers.txt` (95 handlers modernos GET/SET resueltos desde las
  tablas), `kc_impl.txt`/`kc_impl2.txt` (implementaciones Broadcom y despachador), `kc_tables.txt`
  (2×586 entradas con VA de slot), `p2p_decomp.txt` (7 funciones del daemon), `p2p_disasm_key.txt`
  (flujo del gate 12 y del payload 94), `p2p_jumptable.txt` (tabla de roles de `sub_100047660`).
- Los offsets de campo salen de escrituras/lecturas del parámetro struct en los decompilados; los
  nombres, de los strings de error de Broadcom, del type dump Swift de wifip2pd
  (`Build/ida_awdl/awdl_callers_probe.log`) y de los nombres mangleados. La extracción u16→u32 del
  bloque de `getAWDL_SYNC_PARAMS` está verificada en desensamblado (`0xffffff800171b395`–
  `0xffffff800171b3ee`).
- `?` explícitos: significado de los campos marcados; el campo +0 de `apple80211_virt_if_create_data`
  (no lo lee Broadcom); el desglose interno de `apple80211_key`, `apple80211_awdl_dev_stats`,
  `apple80211_awdl_statistics`, `apple80211_awdl_low_latency_statistics`,
  `apple80211_p2p_airplay_ext_stats`, `apple80211_awdl_peer_database`,
  `apple80211_traffic_registration` y los structs de la sección 6; la mecánica exacta del buffer
  0x3B8A0 de `AWDL_CTL`; los tamaños de los structs sin fila en tablas (sección 6); el catálogo de
  valores de `apple80211_encrypt` y `awdl_oper_mode` (este último con tabla de nombres en
  `syms.txt:6114` sin decompilar); el vínculo del `IO80211AWDLPeerManager` con una interfaz de
  driver externo (`AWDL_APPLE80211_DISPATCH.md` §8.3).
- Los tamaños marcados con `_Static_assert` están apoyados por el `len == N` del handler moderno.
  Tres structs del kernel llevan campos sin alineado natural (u32 en +0x16 en
  `election_params`, u64 en +0x0C en `service_parameters`, u64 en +0x10 sin redondeo a 8 en
  `btle_state_params`): se declaran `packed`; el resto usa alineado natural (`awdl_peer_advert_add`
  coloca `ms` en +0x14 alineado). En `apple80211_awdl_bssid` el handler exige 16 y el struct es 16;
  para el kext basta respetar `len == 16` y las lecturas del PM.
- No se modificó ningún archivo del repositorio fuera de `Docs/AWDL_STRUCTS.md` y los ficheros de
  trabajo de `Build/ida_awdl/task_awdl_structs/`.
