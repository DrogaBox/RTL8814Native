# Despacho de requests apple80211 hacia una interfaz AWDL (macOS 26, kernel 25.6.0)

Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), IDB propia del mismo
archivo en `Build/ida_awdl/task_dispatch/` (copia headless de `~/re_cache/ida/boot.kc.i64`),
símbolos de `Docs/re/io80211_25.6.0/` y lecturas directas del `.kc` con el mapeo ya validado en
[`AWDL_KERNEL_ROLE_TABLE.md`](AWDL_KERNEL_ROLE_TABLE.md) (`fileoff = VA_bajo − 0x100000`, fixups
`VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)`). Direcciones = VA del kernel. Lo no verificado va como `?`.

Cierra la pregunta pendiente de [`AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md): qué
requests apple80211 pueden llegar a una interfaz AWDL y cuáles de ellas se entregan a la superficie
virtual de `IO80211AWDLProtocol` (+0xFC8..+0x11C0 del vptr), que es lo que una interfaz propia de
role 6 tendría que implementar.

## 1. Respuesta corta

1. Hay **cuatro rutas** de despacho, todas terminan en funciones estáticas de `IO80211Family` que
   indexan tablas globales por `req_type − 1`:
   * **User client moderno** (`IO80211SkywalkInterface::performGatedCommandIOUC`, tras el chequeo de
     entitlement `com.apple.wlan.authentication`): tablas `gGetHandlerTable`/`gSetHandlerTable`
     (0xffffff800245eb60 / 0xffffff800245fef0, 586 entradas cada una).
   * **ioctl BSD clásico** (`SIOCGA80211`/`SIOCSA80211`): `performGatedCommandIOCTL` sobre la interfaz
     principal y `IO80211VirtualInterface::handleIoctlGated` sobre una interfaz virtual (awdl0): tablas
     `gGetHandlerTableLegacy`/`gSetHandlerTableLegacy` (0xffffff8002470d50 / 0xffffff8002479060,
     con `gGetHandlerTableLegacyCount` = 0x245 = 581 y `gSetHandlerTableLegacyCount` = 0x1C9 = 457).
   * **Kernel interno** (`IO80211Glue::receiveMessageInternal`, mensajes del glue WCL):
     `apple80211RouteInternal` con `gInternalGetHandlerTable`/`gInternalSetHandlerTable`
     (0xffffff800247cea0 / 0xffffff800247e0f0, 586 entradas; sus filas son directamente los puentes
     `apple80211get/setXXX(IO80211SkywalkInterface*, struct*)`).
   * Los **tipos AWDL específicos** del camino moderno no tocan la vtable: van a un objeto
     `IO80211AWDLPeerManager` (safeMetaCast desde ivar+0x120/+0x28 de la interfaz) y se sirven desde
     el estado del peer manager del kernel.
2. Los puentes `apple80211get/setXXX` llaman primero `IO80211SkywalkInterface::isCommandProhibited(type)`
   por el slot **+0xCC8** y luego hacen una **cascada de `safeMetaCast`** por clases de protocolo
   (`IO80211NoneProtocol`, `IO80211SapProtocol`, `IO80211AWDLProtocol`, `IO80211InfraProtocol`,
   `IO80211NANProtocol`, `IO80211NANDataProtocol`), con un **slot de vtable distinto por clase** y un
   despacho común `call [vptr + reg]`. Si ningún cast tiene éxito devuelven `0xE082280E`.
3. **43 tipos** acaban en un slot de la superficie AWDL (`IO80211AWDLProtocol`, vptr +0xFC8..+0x11C0):
   los 4 generales SSID/CHANNEL/BSSID/STATE (por la cascada) y 39 específicos AWDL/P2P/companion.
   Con la implementación Broadcom (`AppleBCMWLANProximityInterface`) cada slot tiene método propio:
   la correspondencia completa es la tabla del §5. Otros 27 tipos AWDL solo llegan al `PeerManager`.
4. `VIRTUAL_IF_CREATE` (tipo 94) no es un slot AWDL: el handler moderno valida `len == 32`, prueba
   la ruta WCL (`sendIOUCToWcl(0x802869C8, 94, data, 32)`) y si no la consume llama al puente
   `apple80211setVIRTUAL_IF_CREATE`, que castea a **`IO80211InfraProtocol`** y llama el slot
   **+0x1160** del objeto principal = `setVIRTUAL_IF_CREATE` virtual del controlador (slot 556,
   coincide con `Source/IO80211/IO80211_25_6_0.h`).

## 2. Puntos de entrada y despachador

```text
IO80211APIUserClient (externalMethod)                                    [?, variante UC no seguida aquí]
  └─ IO80211SkywalkInterface::performGatedCommandIOUC   0xffffff80022f6e30
       csr_check / isCallingProcessEntitled("com.apple.wlan.authentication")
       └─ apple80211RouteExternal(if, uc, magic, req, a5=1, 0)   0xffffff800236cd2e
            └─ gGetHandlerTable[type-1] / gSetHandlerTable[type-1]      (tablas modernas)

ioctl SIOCGA80211 (0xC02869C9) / SIOCSA80211 (0xC02869C8) en el socket BSD
  ├─ IO80211SkywalkInterface::performGatedCommandIOCTL  0xffffff80022f779a   (interfaz principal)
  └─ IO80211VirtualInterface::handleIoctlGated          0xffffff8002331d20   (awdl0 y demás virtuales)
       └─ apple80211RouteExternal(if, 0, magic, req, a5=0, a6)
            └─ gGetHandlerTableLegacy[type-1] / gSetHandlerTableLegacy[type-1]

IO80211Glue::receiveMessageInternal (mensajes WCL/kernel internos)          0xffffff8002194204
  └─ apple80211RouteInternal(magic, type, if, data)         0xffffff800236cfff
       └─ gInternalGetHandlerTable[type-1] / gInternalSetHandlerTable[type-1]  (= puentes)
```

El struct de request que procesan los handlers (`apple80211req` tal como lo accede el código):
`req_type` en **+0x10**, `req_len` en **+0x18**, `req_data` en **+0x20** — coincide con el buffer
crudo del ioctl descrito en [`AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md). Magics:
GET `0xC02869C9` (3223873993), SET usuario `0xC02869C8`, SET kernel `0x802869C8`. El rango válido
de tipos es 1..586; fuera de rango la ruta moderna loguea `"IOCTL req out of bounds [1, 586]"`.
Una request sin handler en la tabla activa devuelve **102** (y se loguea `[IOC DEBUG] EXTERNAL/INTERNAL`)
sin llegar al driver.

Ambas rutas de usuario pasan antes por `csr_check`/entitlement cuando el ivar del glue lo pide
(byte 0 del glue + bit 0 → `csr_check(4)`); el flag exacto por variante es `?`.

## 3. Anatomía de un handler moderno

GET típico (`getSSID`, tipo 1):

```c
if (WCL glue existe) {
    res = IO80211Glue::sendIOUCToWcl(glue, 0xC02869C9, 1, &buf, 0x28, &consumed);
    if (consumed && res != -528340977) return res;   // WCL se quedó la request
}
res = apple80211getSSID(interfaz, &buf);             // puente
if (res == 0) {  // copyback: min(req_len, buf.len) bytes hacia req_data
    req_len_out = min(req->len, ((u32*)&buf)[1]); memmove(req->data, &buf, req_len_out);
}
```

SET típico (`setVIRTUAL_IF_CREATE`, tipo 94): valida `*(u32*)(req+0x18) == 32` y `req->data != NULL`,
prueba WCL y si no consume llama al puente con `req->data`. Los tipos AWDL modernos (p. ej. 116
`AWDL_SYNC_PARAMS`) validan la longitud (`== 24`), toman el objeto en `*(interfaz+0x120)+0x28`,
hacen `safeMetaCast` a `IO80211AWDLPeerManager::gMetaClass` y llaman
`IO80211AWDLPeerManager::getAwdlSyncParams(pm, data)` — estado del kernel, sin pasar por la vtable
de la interfaz (verificado en el pseudocódigo de `IO80211AWDLPeerManager::getAwdlSyncParams`,
0xffffff800221ee92: lee ivars propios +1368/+1376).

El puente (`apple80211getAWDL_SYNC_PARAMS`, 0xffffff8002263844):

```asm
mov  rax, [rdi]                      ; vptr de la interfaz
mov  esi, 74h                        ; 116 = tipo de request
call qword ptr [rax+0xCC8]           ; IO80211SkywalkInterface::isCommandProhibited(type)
test eax, eax
jnz  fail                            ; != 0 → prohibido
lea  rsi, __ZN19IO80211AWDLProtocol10gMetaClassE
call safeMetaCast(interfaz, IO80211AWDLProtocol::gMetaClass)
test rax, rax
jz   devolver_0xE082280E
mov  rax, [r14]                      ; vptr
mov  rax, [rax+0xFE8]                ; slot getAWDL_SYNC_PARAMS
jmp  rax                             ; (interfaz, data)
```

Algunos puentes (SSID, CHANNEL, …) no castean a una sola clase: encadenan dos o tres `safeMetaCast`
con un slot por clase y un despacho común `call [vptr + r15]` (p. ej. `getSSID`:
NoneProtocol→+0x118, SapProtocol→+0xFC8, AWDLProtocol→+0xFC8). Por eso los tipos generales
también alcanzan la superficie AWDL cuando la request va dirigida a awdl0.

## 4. Tablas modernas completas (ruta user client)

`GET` = `gGetHandlerTable` (216 entradas con handler), `SET` = `gSetHandlerTable` (277).
«len» = igualdad exigida por el handler sobre `req_len` cuando existe; «buf» = tamaño del buffer
que el handler pasa a WCL (= sizeof del struct del puente). Destino «vtable Clase +slot» = el
puente castea a esa clase y llama ese slot; esa dirección solo es la superficie AWDL si el objeto
de la request es la interfaz AWDL y la clase es `IO80211AWDLProtocol`. Un destino «?» = handler sin
puente identificable en su pseudocódigo (maneja la request por otra vía o delega en el driver).

### GET (GET moderna)

| tipo | IOC | handler | len/buf | destino |
|---:|---|---|---|---|
| 1 | `APPLE80211_IOC_SSID` | `getSSID` | buf 0x28 | **vtable AWDL +0xfc8** |
| 2 | `APPLE80211_IOC_AUTH_TYPE` | `getAUTH_TYPE` | `len == 16` / buf 0x10 | cascada IO80211NoneProtocol:0x128 |
| 4 | `APPLE80211_IOC_CHANNEL` | `getCHANNEL` | `len == 16` / buf 0x10 | **vtable AWDL +0xfd0** |
| 5 | `APPLE80211_IOC_POWERSAVE` | `getPOWERSAVE` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xeb0 |
| 7 | `APPLE80211_IOC_TXPOWER` | `getTXPOWER` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0xeb8 |
| 8 | `APPLE80211_IOC_RATE` | `getRATE` | `len == 4` / buf 0x18 | cascada IO80211InfraProtocol:0xec0 |
| 9 | `APPLE80211_IOC_BSSID` | `getBSSID` | `len <6` / buf 0xC | **vtable AWDL +0xfd8** |
| 11 | `APPLE80211_IOC_SCAN_RESULT` | `getSCAN_RESULT` | `len == 2272` / buf 0x8E0 | cascada IO80211NoneProtocol:0x1b8 |
| 12 | `APPLE80211_IOC_CARD_CAPABILITIES` | `getCARD_CAPABILITIES` | `len != 0` / buf 0x1C | cascada IO80211SkywalkInterface:? |
| 13 | `APPLE80211_IOC_STATE` | `getSTATE` | `len == 8` / buf 8 | **vtable AWDL +0xfe0** |
| 14 | `APPLE80211_IOC_PHY_MODE` | `getPHY_MODE` | `len == 12` / buf 0xC | cascada IO80211NoneProtocol:0x1e8 |
| 15 | `APPLE80211_IOC_OP_MODE` | `getOP_MODE` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xec8→IO80211SapProtocol:0xfe0 |
| 16 | `APPLE80211_IOC_RSSI` | `getRSSI` | `len == 52` / buf 0x34 | cascada IO80211InfraProtocol:0xed0 |
| 17 | `APPLE80211_IOC_NOISE` | `getNOISE` | `len == 52` / buf 0x34 | cascada IO80211NoneProtocol:0x218 |
| 19 | `APPLE80211_IOC_POWER` | `getPOWER` | `len == 24` / buf 0x18 | cascada IO80211InfraProtocol:0xeb0 |
| 21 | `APPLE80211_IOC_ASSOCIATE_RESULT` | `getASSOCIATE_RESULT` | `len == 4` / buf 0x10 | cascada IO80211NoneProtocol:0x2d8 |
| 27 | `APPLE80211_IOC_SUPPORTED_CHANNELS` | `getSUPPORTED_CHANNELS` | `len == 4824` / buf 0x12D8 | cascada IO80211InfraProtocol:0xed8 |
| 28 | `APPLE80211_IOC_LOCALE` | `getLOCALE` | `len == 4` / buf 8 | cascada IO80211InfraInterface:? |
| 29 | `APPLE80211_IOC_DEAUTH` | `getDEAUTH` | `len == 16` / buf 0x10 | cascada IO80211NoneProtocol:0x2d8 |
| 32 | `APPLE80211_IOC_RATE_SET` | `getRATE_SET` | `len == 188` / buf 0xBC | cascada IO80211NoneProtocol:0x308 |
| 41 | `APPLE80211_IOC_DTIM_INT` | `getDTIM_INT` | `len == 4` / buf 8 | cascada IO80211NoneProtocol:0x398 |
| 42 | `APPLE80211_IOC_STATION_LIST` | `getSTATION_LIST` | `len == 2056` / buf 0x808 | cascada IO80211SapProtocol:0xfe8 |
| 43 | `APPLE80211_IOC_DRIVER_VERSION` | `getDRIVER_VERSION` | buf 0x188 | cascada IO80211SkywalkInterface:? |
| 44 | `APPLE80211_IOC_HARDWARE_VERSION` | `getHARDWARE_VERSION` | buf ? | ? |
| 46 | `APPLE80211_IOC_RSN_IE` | `getRSN_IE` | `len == 264` / buf 0x108 | cascada IO80211NoneProtocol:0x3e8 |
| 48 | `APPLE80211_IOC_AP_IE_LIST` | `getAP_IE_LIST` | `len == 2056` / buf 0x808 | cascada IO80211NoneProtocol:0x408 |
| 49 | `APPLE80211_IOC_STATS` | `getSTATS` | `len == 20` / buf 0x14 | cascada IO80211InfraInterface:? |
| 50 | `APPLE80211_IOC_ASSOCIATION_STATUS` | `getASSOCIATION_STATUS` | `len == 4` / buf 8 | cascada IO80211NoneProtocol:0x428 |
| 51 | `APPLE80211_IOC_COUNTRY_CODE` | `getCOUNTRY_CODE` | `len >= 3` / buf 8 | cascada IO80211SkywalkInterface:? |
| 52 | `APPLE80211_IOC_DEBUG_FLAGS` | `getDEBUG_FLAGS` | buf ? | ? |
| 55 | `APPLE80211_IOC_GUARD_INTERVAL` | `getGUARD_INTERVAL` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xee0 |
| 57 | `APPLE80211_IOC_MCS` | `getMCS` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xee8 |
| 66 | `APPLE80211_IOC_MCS_INDEX_SET` | `getMCS_INDEX_SET` | `len == 16` / buf 0x10 | cascada IO80211NoneProtocol:0x528 |
| 69 | `APPLE80211_IOC_WOW_PARAMETERS` | `getWOW_PARAMETERS` | `len == 15064` / buf 0x3AD8 | cascada IO80211NoneProtocol:0x558 |
| 70 | `APPLE80211_IOC_WOW_ENABLED` | `getWOW_ENABLED` | `len == 8` / buf 8 | cascada IO80211NoneProtocol:0x568 |
| 73 | `APPLE80211_IOC_STA_IE_LIST` | `getSTA_IE_LIST` | `len == 1040` / buf 0x410 | cascada IO80211SapProtocol:0xff0 |
| 78 | `APPLE80211_IOC_KEY_RSC` | `getKEY_RSC` | `len == 148` / buf 0x94 | cascada IO80211SapProtocol:0xff8 |
| 79 | `APPLE80211_IOC_STA_STATS` | `getSTA_STATS` | `len == 40` / buf 0x28 | cascada IO80211SapProtocol:0x1000→IO80211NANDataProtocol:0xfc8 |
| 82 | `APPLE80211_IOC_CACHE_AGE_THRESH` | `getCACHE_AGE_THRESH` | `len == 4` / buf ? | ? |
| 96 | `APPLE80211_IOC_VIRTUAL_IF_ROLE` | `getVIRTUAL_IF_ROLE` | `len != 4` / buf ? | ? |
| 97 | `APPLE80211_IOC_VIRTUAL_IF_PARENT` | `getVIRTUAL_IF_PARENT` | buf ? | ? |
| 102 | `APPLE80211_IOC_BT_COEX_FLAGS` | `getBT_COEX_FLAGS` | buf ? | ? |
| 103 | `APPLE80211_IOC_CURRENT_NETWORK` | `getCURRENT_NETWORK` | `len == 2272` / buf 0x8E0 | cascada IO80211NoneProtocol:0x778 |
| 106 | `APPLE80211_IOC_RSSI_BOUNDS` | `getRSSI_BOUNDS` | `len == 16` / buf 0x10 | cascada IO80211NoneProtocol:0x7a8 |
| 110 | `APPLE80211_IOC_LAST_BCAST_SCAN_TIME` | `getLAST_BCAST_SCAN_TIME` | `len != 12` / buf ? | ? |
| 115 | `APPLE80211_IOC_POWER_DEBUG_INFO` | `getPOWER_DEBUG_INFO` | `len == 2152` / buf 0x868 | cascada IO80211InfraProtocol:0xef0 |
| 116 | `APPLE80211_IOC_AWDL_SYNC_PARAMS` | `getAWDL_SYNC_PARAMS` | `len == 24` / buf ? | PeerManager::getAwdlSyncParams() |
| 117 | `APPLE80211_IOC_AWDL_SYNC_ENABLED` | `getAWDL_SYNC_ENABLED` | `len == 12` / buf ? | PeerManager::getAwdlSyncEnabled() |
| 118 | `APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `len == 20` / buf ? | PeerManager::getAwdlExtensionParams() |
| 119 | `APPLE80211_IOC_AWDL_SERVICE_PARAMS` | `getAWDL_SERVICE_PARAMS` | `len == 44` / buf ? | ? |
| 120 | `APPLE80211_IOC_AWDL_PEER_SERVICE_REQUEST` | `getAWDL_PEER_SERVICE_REQUEST` | `len == 20` / buf ? | ? |
| 122 | `APPLE80211_IOC_AWDL_ELECTION_ID` | `getAWDL_ELECTION_ID` | `len == 8` / buf ? | PeerManager::getAwdlElectionId() |
| 124 | `APPLE80211_IOC_AWDL_GUARD_TIME` | `getAWDL_GUARD_TIME` | `len == 8` / buf ? | ? |
| 126 | `APPLE80211_IOC_AWDL_ELECTION_METRIC` | `getAWDL_ELECTION_METRIC` | `len == 12` / buf ? | PeerManager::getAwdlElectionMetric() |
| 127 | `APPLE80211_IOC_AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` | `getAWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` | `len == 16` / buf ? | ? |
| 128 | `APPLE80211_IOC_AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT` | `getAWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT` | `len == 12` / buf ? | ? |
| 129 | `APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `len == 400` / buf ? | PeerManager::getAwdlChannelSequence() |
| 130 | `APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE` | `getPEER_CACHE_MAXIMUM_SIZE` | `len == 8` / buf ? | ? |
| 131 | `APPLE80211_IOC_AWDL_OUI` | `getAWDL_OUI` | `len == 8` / buf ? | ? |
| 132 | `APPLE80211_IOC_AWDL_MASTER_CHANNEL` | `getAWDL_MASTER_CHANNEL` | `len == 8` / buf ? | PeerManager::getAwdlMasterChannel() |
| 133 | `APPLE80211_IOC_AWDL_TOP_MASTER` | `getAWDL_TOP_MASTER` | `len == 16` / buf ? | PeerManager::getAwdlTopMaster() |
| 134 | `APPLE80211_IOC_AWDL_SYNC_STATE` | `getAWDL_SYNC_STATE` | `len == 24` / buf ? | PeerManager::getAwdlSyncState() |
| 135 | `APPLE80211_IOC_AWDL_ELECTION_RSSI_THRESHOLDS` | `getAWDL_ELECTION_RSSI_THRESHOLDS` | `len == 20` / buf ? | ? |
| 136 | `APPLE80211_IOC_AWDL_PRESENCE_MODE` | `getAWDL_PRESENCE_MODE` | `len == 8` / buf ? | PeerManager::getAwdlPresenceMode() |
| 138 | `APPLE80211_IOC_AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME` | `getAWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME` | `len == 8` / buf ? | ? |
| 139 | `APPLE80211_IOC_AWDL_MASTER_MODE_SYNC_FRAME_PERIOD` | `getAWDL_MASTER_MODE_SYNC_FRAME_PERIOD` | `len == 8` / buf ? | ? |
| 148 | `APPLE80211_IOC_AWDL_STRATEGY` | `getAWDL_STRATEGY` | `len == 24` / buf ? | PeerManager::getAwdlStrategy() |
| 152 | `APPLE80211_IOC_LOG_FLAGS` | `getLOG_FLAGS` | `len == 24` / buf ? | ? |
| 153 | `APPLE80211_IOC_PEER_STATS` | `getPEER_STATS` | `len == 36` / buf ? | ? |
| 154 | `APPLE80211_IOC_HT_CAPABILITY` | `getHT_CAPABILITY` | `len == 32` / buf 0x20 | cascada IO80211InfraProtocol:0xef8 |
| 155 | `APPLE80211_IOC_AWDL_ELECTION_PARAMS` | `getAWDL_ELECTION_PARAMS` | `len == 56` / buf ? | PeerManager::getAwdlElectionParams() |
| 156 | `APPLE80211_IOC_LINK_CHANGED_EVENT_DATA` | `getLINK_CHANGED_EVENT_DATA` | `len == 32` / buf 0x20 | cascada IO80211NoneProtocol:0xab8 |
| 162 | `APPLE80211_IOC_AWDL_CTL` | `getAWDL_CTL` | `len == 243872` / buf ? | ? |
| 163 | `APPLE80211_IOC_AWDL_SOCIAL_TIME_SLOTS` | `getAWDL_SOCIAL_TIME_SLOTS` | `len == 8` / buf ? | PeerManager::getAwdlSocialTimeSlots() |
| 166 | `APPLE80211_IOC_BEACON_PERIOD` | `getBEACON_PERIOD` | `len == 4` / buf 8 | cascada IO80211NoneProtocol:0xb58 |
| 181 | `APPLE80211_IOC_VHT_MCS_INDEX_SET` | `getVHT_MCS_INDEX_SET` | `len == 8` / buf 8 | cascada IO80211NoneProtocol:0xc48 |
| 195 | `APPLE80211_IOC_MCS_VHT` | `getMCS_VHT` | `len == 20` / buf 0x14 | cascada IO80211InfraProtocol:0xf00 |
| 200 | `APPLE80211_IOC_GAS_RESULTS` | `getGAS_RESULTS` | `len == 6176` / buf 0x1820 | cascada IO80211NoneProtocol:0xd78 |
| 202 | `APPLE80211_IOC_AWDL_BTLE_STATE_PARAMS` | `getAWDL_BTLE_STATE_PARAMS` | `len == 28` / buf ? | PeerManager::getAwdlBtleStateParams() |
| 203 | `APPLE80211_IOC_AWDL_PEER_DATABASE` | `getAWDL_PEER_DATABASE` | `len == 208480` / buf ? | PeerManager::getAwdlPeerDatabase() |
| 205 | `APPLE80211_IOC_AWDL_SECONDARY_MASTER_CHANNEL` | `getAWDL_SECONDARY_MASTER_CHANNEL` | `len == 8` / buf ? | PeerManager::getAwdlSecondaryMasterChannel() |
| 207 | `APPLE80211_IOC_CHANNELS_INFO` | `getCHANNELS_INFO` | `len == 10016` / buf 0x2720 | cascada IO80211InfraProtocol:0xf08 |
| 208 | `APPLE80211_IOC_AWDL_AF_TX_MODE` | `getAWDL_AF_TX_MODE` | `len == 8` / buf ? | PeerManager::getAwdlAfTxMode() |
| 209 | `APPLE80211_IOC_ERROR_STRING` | `getERROR_STRING` | buf ? | ? |
| 210 | `APPLE80211_IOC_ERROR_NO` | `getERROR_NO` | buf ? | ? |
| 213 | `APPLE80211_IOC_AWDL_MIN_RATE` | `getAWDL_MIN_RATE` | `len == 8` / buf ? | ? |
| 214 | `APPLE80211_IOC_VHT_CAPABILITY` | `getVHT_CAPABILITY` | `len == 20` / buf 0x14 | cascada IO80211InfraProtocol:0xf10 |
| 215 | `APPLE80211_IOC_BGSCAN_CACHE_RESULTS` | `getBGSCAN_CACHE_RESULTS` | `len == 2816` / buf 0xB00 | cascada IO80211NoneProtocol:0xe68 |
| 216 | `APPLE80211_IOC_ROAM_PROFILE` | `getROAM_PROFILE` | `len == 384` / buf 0x180 | cascada IO80211InfraProtocol:0xf18 |
| 217 | `APPLE80211_IOC_AWDL_OPER_MODE` | `getAWDL_OPER_MODE` | `len == 8` / buf ? | ? |
| 218 | `APPLE80211_IOC_RESTORE_DEFAULTS` | `getRESTORE_DEFAULTS` | `len == 4` / buf ? | PeerManager::getAwdlRestoreDefaults() |
| 220 | `APPLE80211_IOC_AWDL_ENCRYPTION_TYPE` | `getAWDL_ENCRYPTION_TYPE` | `len == 8` / buf ? | PeerManager::getAwdlEncryptType() |
| 223 | `APPLE80211_IOC_AWDL_STATISTICS` | `getAWDL_STATISTICS` | `len == 3752` / buf ? | PeerManager::getAwdlStatistics() |
| 228 | `APPLE80211_IOC_DBG_GUARD_TIME_PARAMS` | `getDBG_GUARD_TIME_PARAMS` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0xf28 |
| 230 | `APPLE80211_IOC_LEAKY_AP_STATS_MODE` | `getLEAKY_AP_STATS_MODE` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0xf30 |
| 232 | `APPLE80211_IOC_LEAKY_AP_STATS` | `getLEAKY_AP_STATS` | buf ? | ? |
| 233 | `APPLE80211_IOC_AWDL_BLOCK_SET_COMMANDS` | `getAWDL_BLOCK_SET_COMMANDS` | `len == 8` / buf ? | PeerManager::getAwdlBlockSetCommands() |
| 236 | `APPLE80211_IOC_FORCE_SYNC_TO_PEER` | `getFORCE_SYNC_TO_PEER` | `len == 12` / buf ? | ? |
| 237 | `APPLE80211_IOC_COUNTRY_CHANNELS` | `getCOUNTRY_CHANNELS` | `len == 4812` / buf 0x12CC | cascada IO80211InfraProtocol:0xf38 |
| 238 | `APPLE80211_IOC_PRIVATE_MAC` | `getPRIVATE_MAC` | `len == 28` / buf 0x1C | cascada IO80211InfraProtocol:0xf40 |
| 241 | `APPLE80211_IOC_RANGING_ENABLE` | `getRANGING_ENABLE` | buf ? | ? |
| 242 | `APPLE80211_IOC_RANGING_START` | `getRANGING_START` | `len == 1800` / buf ? | ? |
| 244 | `APPLE80211_IOC_AWDL_PREFERRED_CHANNELS` | `getAWDL_PREFERRED_CHANNELS` | `len == 12` / buf ? | PeerManager::getAwdlPreferredChannels() |
| 247 | `APPLE80211_IOC_AWDL_DEV_STATS` | `getAWDL_DEV_STATS` | `len == 124` / buf ? | ? |
| 249 | `APPLE80211_IOC_AWDL_COMMON_CHANNEL` | `getAWDL_COMMON_CHANNEL` | `len == 8` / buf ? | ? |
| 251 | `APPLE80211_IOC_TKO_PARAMS` | `getTKO_PARAMS` | `len == 28` / buf 0x1C | cascada IO80211InfraProtocol:0xf60 |
| 252 | `APPLE80211_IOC_TKO_DUMP` | `getTKO_DUMP` | `len == 504` / buf 0x1F8 | cascada IO80211InfraProtocol:0xf68 |
| 254 | `APPLE80211_IOC_HW_SUPPORTED_CHANNELS` | `getHW_SUPPORTED_CHANNELS` | `len == 4824` / buf ? | ? |
| 255 | `APPLE80211_IOC_BTCOEX_PROFILE` | `getBTCOEX_PROFILE` | `len == 56` / buf 0x38 | puente __Z28apple80211getBTCOEX_PROFILESP23IO80211Skywa |
| 256 | `APPLE80211_IOC_BTCOEX_PROFILE_ACTIVE` | `getBTCOEX_PROFILE_ACTIVE` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xf80 |
| 257 | `APPLE80211_IOC_TRAP_INFO` | `getTRAP_INFO` | `len == 528` / buf 0x210 | cascada IO80211InfraProtocol:0xf88 |
| 258 | `APPLE80211_IOC_THERMAL_INDEX` | `getTHERMAL_INDEX` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xf90 |
| 259 | `APPLE80211_IOC_MAX_NSS_FOR_AP` | `getMAX_NSS_FOR_AP` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xf98 |
| 260 | `APPLE80211_IOC_BTCOEX_2G_CHAIN_DISABLE` | `getBTCOEX_2G_CHAIN_DISABLE` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0xfa0 |
| 261 | `APPLE80211_IOC_POWER_BUDGET` | `getPOWER_BUDGET` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xfa8 |
| 265 | `APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE` | `getOFFLOAD_TCPKA_ENABLE` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xfb0 |
| 266 | `APPLE80211_IOC_RANGING_CAPS` | `getRANGING_CAPS` | `len == 24` / buf ? | ? |
| 268 | `APPLE80211_IOC_NAN_ENABLED` | `getNAN_ENABLED` | `len == 8` / buf ? | ? |
| 270 | `APPLE80211_IOC_NAN_CLUSTER_ID` | `getNAN_CLUSTER_ID` | `len == 12` / buf ? | ? |
| 271 | `APPLE80211_IOC_NAN_DISC_BCN_PERIOD` | `getNAN_DISC_BCN_PERIOD` | `len == 2` / buf ? | ? |
| 272 | `APPLE80211_IOC_NAN_SYNC_BCN_PERIOD` | `getNAN_SYNC_BCN_PERIOD` | `len == 2` / buf ? | ? |
| 273 | `APPLE80211_IOC_NAN_DW_AWAKE_PERIOD` | `getNAN_DW_AWAKE_PERIOD` | `len == 2` / buf ? | ? |
| 274 | `APPLE80211_IOC_NAN_BCN_TX_CONTROL` | `getNAN_BCN_TX_CONTROL` | `len == 2` / buf ? | ? |
| 275 | `APPLE80211_IOC_NAN_HOST_ELECTION` | `getNAN_HOST_ELECTION` | `len == 1` / buf ? | ? |
| 276 | `APPLE80211_IOC_NAN_RSSI_CLOSE` | `getNAN_RSSI_CLOSE` | `len == 1` / buf ? | ? |
| 277 | `APPLE80211_IOC_NAN_RSSI_MIDDLE` | `getNAN_RSSI_MIDDLE` | `len == 1` / buf ? | ? |
| 278 | `APPLE80211_IOC_NAN_RSSI_5G_CLOSE` | `getNAN_RSSI_5G_CLOSE` | `len == 1` / buf ? | ? |
| 279 | `APPLE80211_IOC_NAN_RSSI_5G_MIDDLE` | `getNAN_RSSI_5G_MIDDLE` | `len == 1` / buf ? | ? |
| 280 | `APPLE80211_IOC_NAN_DEVICE_INFO` | `getNAN_DEVICE_INFO` | `len == 48` / buf ? | ? |
| 281 | `APPLE80211_IOC_NAN_MASTER_PREFERENCE` | `getNAN_MASTER_PREFERENCE` | `len == 2` / buf ? | ? |
| 282 | `APPLE80211_IOC_NAN_RANDOM_FACTOR` | `getNAN_RANDOM_FACTOR` | `len == 2` / buf ? | ? |
| 283 | `APPLE80211_IOC_NAN_RANDOM_FACTOR_ROTATION` | `getNAN_RANDOM_FACTOR_ROTATION` | `len == 1` / buf ? | ? |
| 284 | `APPLE80211_IOC_NAN_HOP_COUNT_TO_ANCHOR_MASTER` | `getNAN_HOP_COUNT_TO_ANCHOR_MASTER` | `len == 2` / buf ? | ? |
| 285 | `APPLE80211_IOC_NAN_MASTER_RANK` | `getNAN_MASTER_RANK` | `len == 8` / buf ? | ? |
| 286 | `APPLE80211_IOC_NAN_MAX_PEERS` | `getNAN_MAX_PEERS` | `len == 1` / buf ? | ? |
| 287 | `APPLE80211_IOC_NAN_MASTER_CHANNEL` | `getNAN_MASTER_CHANNEL` | `len == 1` / buf ? | ? |
| 288 | `APPLE80211_IOC_NAN_SECONDARY_MASTER_CHANNEL` | `getNAN_SECONDARY_MASTER_CHANNEL` | `len == 1` / buf ? | ? |
| 289 | `APPLE80211_IOC_NAN_TX_RATE` | `getNAN_TX_RATE` | `len == 2` / buf ? | ? |
| 290 | `APPLE80211_IOC_NAN_WARMUP_TIME` | `getNAN_WARMUP_TIME` | `len == 2` / buf ? | ? |
| 291 | `APPLE80211_IOC_NAN_CURRENT_ANCHOR_MASTER_RECORD` | `getNAN_CURRENT_ANCHOR_MASTER_RECORD` | `len == 10` / buf ? | ? |
| 292 | `APPLE80211_IOC_NAN_LAST_ANCHOR_MASTER_RECORD` | `getNAN_LAST_ANCHOR_MASTER_RECORD` | `len == 12` / buf ? | ? |
| 296 | `APPLE80211_IOC_NAN_SYNC_STATS` | `getNAN_SYNC_STATS` | `len == 20` / buf ? | ? |
| 297 | `APPLE80211_IOC_NAN_HOP_COUNT_LIMIT` | `getNAN_HOP_COUNT_LIMIT` | `len == 1` / buf ? | ? |
| 299 | `APPLE80211_IOC_NAN_CHANNEL_SEQUENCE` | `getNAN_CHANNEL_SEQUENCE` | `len == 243` / buf ? | ? |
| 302 | `APPLE80211_IOC_NAN_PUBLISHED_SERVICE_LIST` | `getNAN_PUBLISHED_SERVICE_LIST` | `len == 258` / buf ? | ? |
| 304 | `APPLE80211_IOC_NAN_SUBSCRIBED_SERVICE_LIST` | `getNAN_SUBSCRIBED_SERVICE_LIST` | `len == 258` / buf ? | ? |
| 305 | `APPLE80211_IOC_NAN_PUBLISHERS_LIST` | `getNAN_PUBLISHERS_LIST` | `len == 65858` / buf ? | ? |
| 308 | `APPLE80211_IOC_NAN_DISCOVERY_RESULT` | `getNAN_DISCOVERY_RESULT` | `len == 2070` / buf ? | ? |
| 309 | `APPLE80211_IOC_NAN_FOLLOW_UP_RECEIVE` | `getNAN_FOLLOW_UP_RECEIVE` | `len == 2064` / buf ? | ? |
| 310 | `APPLE80211_IOC_NAN_SERVICE_DISCOVERY_STATS` | `getNAN_SERVICE_DISCOVERY_STATS` | `len == 44` / buf ? | ? |
| 320 | `APPLE80211_IOC_NAN_ADVERTISERS` | `getNAN_ADVERTISERS` | `len == 2056` / buf ? | ? |
| 321 | `APPLE80211_IOC_NAN_COMMITTED_AVAILABILITY` | `getNAN_COMMITTED_AVAILABILITY` | `len == 2576` / buf ? | ? |
| 323 | `APPLE80211_IOC_NAN_DEVICE_CAPABILITY` | `getNAN_DEVICE_CAPABILITY` | `len == 44` / buf ? | ? |
| 327 | `APPLE80211_IOC_NAN_PEER_DATABASE` | `getNAN_PEER_DATABASE` | `len == 11280` / buf ? | ? |
| 328 | `APPLE80211_IOC_NAN_CTL` | `getNAN_CTL` | `len == 243872` / buf ? | ? |
| 329 | `APPLE80211_IOC_NAN_DP_INFO` | `getNAN_DP_INFO` | `len == 2058` / buf ? | ? |
| 332 | `APPLE80211_IOC_NAN_QOS_PARAMS` | `getNAN_QOS_PARAMS` | `len == 20` / buf ? | ? |
| 336 | `APPLE80211_IOC_HOST_AP_MODE_HIDDEN` | `getHOST_AP_MODE_HIDDEN` | `len == 4` / buf 8 | cascada IO80211SapProtocol:0x1010 |
| 337 | `APPLE80211_IOC_LQM_CONFIG` | `getLQM_CONFIG` | `len == 36` / buf 0x24 | cascada IO80211InfraProtocol:0xfc0 |
| 338 | `APPLE80211_IOC_AWDL_CCA` | `getAWDL_CCA` | `len == 392` / buf ? | PeerManager::getAwdlCCA() |
| 339 | `APPLE80211_IOC_TRAP_CRASHTRACER_MINI_DUMP` | `getTRAP_CRASHTRACER_MINI_DUMP` | `len == 102404` / buf 0x19004 | cascada IO80211InfraProtocol:0xfc8 |
| 343 | `APPLE80211_IOC_AWDL_LOW_LATENCY_STATISTICS` | `getAWDL_LOW_LATENCY_STATISTICS` | `len == 4024` / buf ? | PeerManager::getAwdlLowLatencyStatistics() |
| 344 | `APPLE80211_IOC_LLW_PARAMS` | `getLLW_PARAMS` | `len == 44` / buf ? | PeerManager::getLLWParams() |
| 346 | `APPLE80211_IOC_BEACON_INFO` | `getBEACON_INFO` | `len == 2056` / buf 0x808 | cascada IO80211InfraProtocol:0xfd0 |
| 347 | `APPLE80211_IOC_SOFTAP_PARAMS` | `getSOFTAP_PARAMS` | `len == 28` / buf 0x1C | cascada IO80211SapProtocol:0x1018 |
| 348 | `APPLE80211_IOC_CHIP_POWER_RANGE` | `getCHIP_POWER_RANGE` | `len == 52` / buf 0x34 | cascada IO80211InfraProtocol:0xfd8 |
| 350 | `APPLE80211_IOC_SOFTAP_STATS` | `getSOFTAP_STATS` | `len == 88` / buf 0x58 | cascada IO80211SapProtocol:0x1020 |
| 351 | `APPLE80211_IOC_AWDL_SIDECAR_DIAGNOSTICS` | `getAWDL_SIDECAR_DIAGNOSTICS` | `len == 80` / buf ? | PeerManager::getAwdlSidecarDiagnostics() |
| 353 | `APPLE80211_IOC_NSS` | `getNSS` | `len == 4` / buf 8 | cascada IO80211InfraProtocol:0xfe0 |
| 355 | `APPLE80211_IOC_NAN_LLW_PARAMS` | `getNAN_LLW_PARAMS` | `len == 44` / buf ? | ? |
| 356 | `APPLE80211_IOC_HP2P_CAPS` | `getH` | `len == 8` / buf ? | ? |
| 357 | `APPLE80211_IOC_RLLW_STATS` | `getRLLW_STATS` | `len == 32` / buf ? | ? |
| 359 | `APPLE80211_IOC_HW_ADDR` | `getHW_ADDR` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0xfe8 |
| 361 | `APPLE80211_IOC_HE_MCS_INDEX_SET` | `getHE_MCS_INDEX_SET` | `len == 8` / buf 8 | cascada IO80211NoneProtocol:0x1778 |
| 362 | `APPLE80211_IOC_CHIP_DIAGS` | `getCHIP_DIAGS` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0xff0 |
| 364 | `APPLE80211_IOC_LOWLATENCY_STATISTICS` | `getLOWLATENCY_STATISTICS` | `len == 1248` / buf ? | ? |
| 370 | `APPLE80211_IOC_AWDL_AIRPLAY_STATISTICS` | `getAWDL_AIRPLAY_STATISTICS` | `len == 4032` / buf ? | PeerManager::getAwdlAirplayStatistics() |
| 371 | `APPLE80211_IOC_HP2P_CTRL` | `getH` | `len == 8` / buf ? | ? |
| 372 | `APPLE80211_IOC_BSS_BLACKLIST` | `getBSS_BLACKLIST` | `len == 43` / buf 0x2B | cascada IO80211InfraProtocol:0x1000 |
| 375 | `APPLE80211_IOC_MIMO_STATUS` | `getMIMO_STATUS` | `len == 10` / buf 0xA | cascada IO80211InfraProtocol:0x1010 |
| 376 | `APPLE80211_IOC_CUR_PMK` | `getCUR_PMK` | `len == 92` / buf 0x5C | cascada IO80211InfraProtocol:0x1018 |
| 377 | `APPLE80211_IOC_DYNSAR_DETAIL` | `getDYNSAR_DETAIL` | `len == 11544` / buf 0x2D18 | cascada IO80211InfraProtocol:0x1020 |
| 384 | `APPLE80211_IOC_LQM_METRICS_CRASHTRACER_DATA` | `getLQM_METRICS_CRASHTRACER_DATA` | `len != 0` / buf ? | ? |
| 386 | `APPLE80211_IOC_LQM_SUMMARY` | `getLQM_SUMMARY` | buf ? | ? |
| 387 | `APPLE80211_IOC_COLOCATED_NETWORK_SCOPE_ID` | `getCOLOCATED_NETWORK_SCOPE_ID` | buf 0x30 | cascada IO80211NoneProtocol:0x1918 |
| 389 | `APPLE80211_IOC_USERSPACE_P2P_OPTIONS` | `getUSERSPACE_` | `len != 16` / buf ? | ? |
| 390 | `APPLE80211_IOC_BEACON_SCAN_CACHE_REQ` | `getBEACON_SCAN_CACHE_REQ` | `len != 0` / buf 0x8E0 | cascada IO80211NoneProtocol:0x1948 |
| 393 | `APPLE80211_IOC_CCA` | `getCCA` | `len == 4` / buf 8 | cascada IO80211NoneProtocol:0x1978 |
| 396 | `APPLE80211_IOC_LQM_RING_BUFFER_DATA` | `getLQM_RING_BUFFER_DATA` | `len != 0` / buf ? | ? |
| 397 | `APPLE80211_IOC_RX_RATE` | `getRX_RATE` | `len == 4` / buf 0x18 | cascada IO80211NoneProtocol:0x19b8 |
| 406 | `APPLE80211_IOC_TIMESYNC_INFO` | `getTIMESYNC_INFO` | `len == 256` / buf 0x100 | cascada IO80211InfraProtocol:0x1040 |
| 410 | `APPLE80211_IOC_SENSING_DATA` | `getSENSING_DATA` | buf ? | ? |
| 416 | `APPLE80211_IOC_AWDL_MI_BITMAP` | `getAWDL_MI_BITMAP` | `len == 8` / buf ? | ? |
| 421 | `APPLE80211_IOC_IOR_LEGEND` | `getIOR_LEGEND` | buf ? | ? |
| 423 | `APPLE80211_IOC_COUNTRY_BAND_SUPPORT` | `getCOUNTRY_BAND_SUPPORT` | `len == 4` / buf 8 | cascada IO80211NoneProtocol:0x1b58 |
| 447 | `APPLE80211_IOC_HE_COUNTERS` | `getHE_COUNTERS` | `len == 676` / buf 0x2A4 | cascada IO80211InfraProtocol:0x1078 |
| 449 | `APPLE80211_IOC_WCL_CHANNELS_INFO` | `getWCL_CHANNELS_INFO` | `len == 3206` / buf 0xC86 | cascada IO80211InfraProtocol:0x1080 |
| 457 | `APPLE80211_IOC_RSN_XE` | `getRSN_XE` | `len == 264` / buf 0x108 | cascada IO80211InfraProtocol:0x1088 |
| 466 | `APPLE80211_IOC_ACTIVE_SOCKETS` | `getACTIVE_SOCKETS` | `len == 0` / buf ? | ? |
| 471 | `APPLE80211_IOC_MOVING_NETWORK` | `getMOVING_NETWORK` | `len == 1` / buf 1 | cascada IO80211NoneProtocol:0x1e58 |
| 515 | `APPLE80211_IOC_WIFI_NOISE_PER_ANT` | `getWIFI_NOISE_PER_ANT` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x10b8 |
| 516 | `APPLE80211_IOC_BLOCKED_BANDS` | `getBLOCKED_BANDS` | `len == 4` / buf 4 | cascada IO80211NoneProtocol:0x2108 |
| 524 | `APPLE80211_IOC_FW_CLOCK_INFO` | `getFW_CLOCK_INFO` | `len == 16` / buf 0x10 | cascada IO80211InfraProtocol:0x10c0 |
| 529 | `APPLE80211_IOC_TIMESYNC_STATS` | `getTIMESYNC_STATS` | `len == 40` / buf 0x28 | cascada IO80211InfraProtocol:0x10c8 |
| 552 | `APPLE80211_IOC_DEVICE_TYPE_IN_DHCP_ALLOW` | `getDEVICE_TYPE_IN_DHCP_ALLOW` | `len == 4` / buf 8 | cascada IO80211NoneProtocol:0x2318 |
| 556 | `APPLE80211_IOC_NAN_GCR_LINK_CONDITION` | `getNAN_GCR_LINK_CONDITION` | `len == 24` / buf ? | ? |
| 561 | `APPLE80211_IOC_SMARTCCA_OPMODE` | `getSMARTCCA_OPMODE` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x10d8 |
| 563 | `APPLE80211_IOC_CONNECTION_ID` | `getCONNECTION_ID` | `len == 4` / buf 4 | cascada IO80211InfraInterface:? |
| 567 | `APPLE80211_IOC_NAN_CHANNEL_SEQ_MISMATCH_COUNT` | `getNAN_CHANNEL_SEQ_MISMATCH_COUNT` | `len == 16` / buf ? | ? |
| 569 | `APPLE80211_IOC_NAN_MATCHING_SLOTS_COUNT` | `getNAN_MATCHING_SLOTS_COUNT` | `len == 12` / buf ? | ? |
| 570 | `APPLE80211_IOC_HE_CAPABILITY` | `getHE_CAPABILITY` | `len == 40` / buf 0x28 | cascada IO80211InfraProtocol:0x10e8 |
| 571 | `APPLE80211_IOC_DEVICE_ORIENTATION` | `getDEVICE_ORIENTATION` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x10f0 |
| 572 | `APPLE80211_IOC_ACCESSORY_STATE` | `getACCESSORY_STATE` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x10f8 |
| 573 | `APPLE80211_IOC_P2P_DEVICE_CAPABILITY` | `get` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x1100 |
| 574 | `APPLE80211_IOC_POWERTABLE_VERSION` | `getPOWERTABLE_VERSION` | `len == 1160` / buf 0x488 | cascada IO80211InfraProtocol:0x1108 |
| 581 | `APPLE80211_IOC_NAN_CCA` | `getNAN_CCA` | `len == 300` / buf ? | ? |

### SET (SET moderna)

| tipo | IOC | handler | len/buf | destino |
|---:|---|---|---|---|
| 1 | `APPLE80211_IOC_SSID` | `setSSID` | buf 0x28 | **vtable AWDL +0x10a0** |
| 3 | `APPLE80211_IOC_CIPHER_KEY` | `setCIPHER_KEY` | `len != 148` / buf 0x10 | cascada IO80211NoneProtocol:0x128 |
| 4 | `APPLE80211_IOC_CHANNEL` | `setCHANNEL` | `len != 0` / buf 0x10 | **vtable AWDL +0x10a8** |
| 5 | `APPLE80211_IOC_POWERSAVE` | `setPOWERSAVE` | buf 8 | cascada IO80211InfraProtocol:0x1120 |
| 7 | `APPLE80211_IOC_TXPOWER` | `setTXPOWER` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x1128 |
| 8 | `APPLE80211_IOC_RATE` | `setRATE` | buf 0x18 | cascada IO80211InfraProtocol:0x1130 |
| 10 | `APPLE80211_IOC_SCAN_REQ` | `setSCAN_REQ` | `len != 0` / buf 0x1598 | cascada IO80211NoneProtocol:0x1b0 |
| 19 | `APPLE80211_IOC_POWER` | `setPOWER` | `len == 24` / buf 0x18 | cascada IO80211InfraProtocol:0x1120 |
| 20 | `APPLE80211_IOC_ASSOCIATE` | `setASSOCIATE` | `len != 0` / buf 0x38C | cascada IO80211NoneProtocol:0x250 |
| 22 | `APPLE80211_IOC_DISASSOCIATE` | `setDISASSOCIATE` | buf 8 | cascada IO80211InfraProtocol:0xec8→IO80211SapProtocol:0xfe0 |
| 24 | `APPLE80211_IOC_IBSS_MODE` | `setIBSS_MODE` | buf ? | ? |
| 25 | `APPLE80211_IOC_HOST_AP_MODE` | `setHOST_AP_MODE` | buf ? | ? |
| 26 | `APPLE80211_IOC_AP_MODE` | `setAP_MODE` | buf 8 | cascada IO80211InfraProtocol:0x1140 |
| 29 | `APPLE80211_IOC_DEAUTH` | `setDEAUTH` | `len == 16` / buf 0x10 | cascada IO80211NoneProtocol:0x2e0 |
| 46 | `APPLE80211_IOC_RSN_IE` | `setRSN_IE` | `len == 264` / buf 0x108 | cascada IO80211NoneProtocol:0x3f0 |
| 47 | `APPLE80211_IOC_BACKGROUND_SCAN` | `setBACKGROUND_SCAN` | `len == 6980` / buf 0x1B44 | cascada IO80211NoneProtocol:0x400 |
| 51 | `APPLE80211_IOC_COUNTRY_CODE` | `setCOUNTRY_CODE` | `len == 8` / buf 8 | cascada IO80211SkywalkInterface:? |
| 52 | `APPLE80211_IOC_DEBUG_FLAGS` | `setDEBUG_FLAGS` | buf ? | ? |
| 69 | `APPLE80211_IOC_WOW_PARAMETERS` | `setWOW_PARAMETERS` | `len == 15064` / buf 0x3AD8 | cascada IO80211NoneProtocol:0x560 |
| 70 | `APPLE80211_IOC_WOW_ENABLED` | `setWOW_ENABLED` | buf 8 | cascada IO80211NoneProtocol:0x570 |
| 74 | `APPLE80211_IOC_STA_AUTHORIZE` | `setSTA_AUTHORIZE` | `len == 20` / buf 0x14 | cascada IO80211SapProtocol:0x1048 |
| 75 | `APPLE80211_IOC_STA_DISASSOCIATE` | `setSTA_DISASSOCIATE` | `len == 16` / buf 0x10 | cascada IO80211SapProtocol:0x1050 |
| 76 | `APPLE80211_IOC_STA_DEAUTH` | `setSTA_DEAUTH` | `len == 16` / buf 0x10 | cascada IO80211SapProtocol:0x1058 |
| 77 | `APPLE80211_IOC_RSN_CONF` | `setRSN_CONF` | `len == 164` / buf 0xA4 | cascada IO80211SapProtocol:0x1060 |
| 82 | `APPLE80211_IOC_CACHE_AGE_THRESH` | `setCACHE_AGE_THRESH` | buf ? | ? |
| 85 | `APPLE80211_IOC_IE` | `setIE` | `len == 2068` / buf 0x814 | cascada IO80211InfraProtocol:0x1148 |
| 88 | `APPLE80211_IOC_WOW_TEST` | `setWOW_TEST` | buf 8 | cascada IO80211InfraProtocol:0x1150 |
| 90 | `APPLE80211_IOC_SCANCACHE_CLEAR` | `setSCANCACHE_CLEAR` | buf 0 | cascada IO80211NoneProtocol:0x6b0 |
| 94 | `APPLE80211_IOC_VIRTUAL_IF_CREATE` | `setVIRTUAL_IF_CREATE` | `len == 32` / buf 0x20 | cascada IO80211InfraProtocol:0x1160 |
| 102 | `APPLE80211_IOC_BT_COEX_FLAGS` | `setBT_COEX_FLAGS` | buf ? | ? |
| 107 | `APPLE80211_IOC_ROAM` | `setROAM` | `len == 20` / buf 0x14 | puente __Z24apple80211setROAM_THRESHP23IO80211SkywalkIn |
| 116 | `APPLE80211_IOC_AWDL_SYNC_PARAMS` | `setAWDL_SYNC_PARAMS` | `len == 24` / buf ? | PeerManager::setAwdlSyncParams() |
| 117 | `APPLE80211_IOC_AWDL_SYNC_ENABLED` | `setAWDL_SYNC_ENABLED` | `len == 12` / buf ? | PeerManager::setAwdlSyncEnabled() |
| 118 | `APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `len == 20` / buf ? | PeerManager::setAwdlExtensionParams() |
| 119 | `APPLE80211_IOC_AWDL_SERVICE_PARAMS` | `setAWDL_SERVICE_PARAMS` | `len == 44` / buf ? | ? |
| 120 | `APPLE80211_IOC_AWDL_PEER_SERVICE_REQUEST` | `setAWDL_PEER_SERVICE_REQUEST` | buf ? | PeerManager::setAwdlPeerServiceRequest() |
| 122 | `APPLE80211_IOC_AWDL_ELECTION_ID` | `setAWDL_ELECTION_ID` | `len == 8` / buf ? | PeerManager::setAwdlElectionId() |
| 124 | `APPLE80211_IOC_AWDL_GUARD_TIME` | `setAWDL_GUARD_TIME` | `len == 8` / buf ? | ? |
| 125 | `APPLE80211_IOC_AWDL_BSSID` | `setAWDL_BSSID` | `len == 16` / buf ? | PeerManager::setAwdlBssid() |
| 126 | `APPLE80211_IOC_AWDL_ELECTION_METRIC` | `setAWDL_ELECTION_METRIC` | `len == 12` / buf ? | PeerManager::setAwdlElectionMetric() |
| 127 | `APPLE80211_IOC_AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` | `setAWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` | `len == 16` / buf ? | PeerManager::setAwdlAvailabilityWindowApAlignment() |
| 128 | `APPLE80211_IOC_AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT` | `setAWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT` | `len == 12` / buf ? | ? |
| 129 | `APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `len == 400` / buf ? | PeerManager::setAwdlChannelSequence() |
| 131 | `APPLE80211_IOC_AWDL_OUI` | `setAWDL_OUI` | `len == 8` / buf ? | PeerManager::setAwdlOUI() |
| 132 | `APPLE80211_IOC_AWDL_MASTER_CHANNEL` | `setAWDL_MASTER_CHANNEL` | `len == 8` / buf ? | PeerManager::setAwdlMasterChannel() |
| 133 | `APPLE80211_IOC_AWDL_TOP_MASTER` | `setAWDL_TOP_MASTER` | `len == 16` / buf ? | ? |
| 134 | `APPLE80211_IOC_AWDL_SYNC_STATE` | `setAWDL_SYNC_STATE` | `len == 24` / buf ? | PeerManager::setAwdlSyncState() |
| 135 | `APPLE80211_IOC_AWDL_ELECTION_RSSI_THRESHOLDS` | `setAWDL_ELECTION_RSSI_THRESHOLDS` | `len == 20` / buf ? | PeerManager::setAwdlElectionRssiThresholds() |
| 136 | `APPLE80211_IOC_AWDL_PRESENCE_MODE` | `setAWDL_PRESENCE_MODE` | `len == 8` / buf ? | PeerManager::setAwdlPresenceMode() |
| 138 | `APPLE80211_IOC_AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME` | `setAWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME` | `len == 8` / buf ? | PeerManager::setAwdlSyncFramePacketLifetime() |
| 139 | `APPLE80211_IOC_AWDL_MASTER_MODE_SYNC_FRAME_PERIOD` | `setAWDL_MASTER_MODE_SYNC_FRAME_PERIOD` | `len == 8` / buf ? | ? |
| 148 | `APPLE80211_IOC_AWDL_STRATEGY` | `setAWDL_STRATEGY` | `len == 24` / buf ? | PeerManager::setAwdlStrategy() |
| 152 | `APPLE80211_IOC_LOG_FLAGS` | `setLOG_FLAGS` | `len == 24` / buf ? | ? |
| 153 | `APPLE80211_IOC_PEER_STATS` | `setPEER_STATS` | `len == 36` / buf ? | ? |
| 154 | `APPLE80211_IOC_HT_CAPABILITY` | `setHT_CAPABILITY` | `len == 32` / buf ? | ? |
| 155 | `APPLE80211_IOC_AWDL_ELECTION_PARAMS` | `setAWDL_ELECTION_PARAMS` | `len == 56` / buf ? | PeerManager::setAwdlElectionParams() |
| 157 | `APPLE80211_IOC_GET_DEBUG_INFO` | `setGET_DEBUG_INFO` | `len == 245192` / buf 0x3BDE0 | cascada IO80211SkywalkInterface:? |
| 162 | `APPLE80211_IOC_AWDL_CTL` | `setAWDL_CTL` | `len == 243872` / buf ? | ? |
| 163 | `APPLE80211_IOC_AWDL_SOCIAL_TIME_SLOTS` | `setAWDL_SOCIAL_TIME_SLOTS` | `len == 8` / buf ? | PeerManager::setAwdlSocialTimeSlots() |
| 164 | `APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION` | `setAWDL_PEER_TRAFFIC_REGISTRATION` | `len == 184` / buf ? | PeerManager::setAwdlPeerTrafficRegistration() |
| 167 | `APPLE80211_IOC_AWDL_FORCED_ROAM_CONFIG` | `setAWDL_FORCED_ROAM_CONFIG` | `len == 16` / buf ? | PeerManager::forceRoamToPeersAP() |
| 191 | `APPLE80211_IOC_OFFLOAD_ARP` | `setOFFLOAD_ARP` | buf 0x18 | puente __Z28apple80211setOFFLOAD_ARP_NDPP23IO80211Skywa |
| 192 | `APPLE80211_IOC_OFFLOAD_NDP` | `setOFFLOAD_NDP` | buf 0x48 | cascada IO80211InfraProtocol:0x1178 |
| 193 | `APPLE80211_IOC_OFFLOAD_SCAN` | `setOFFLOAD_SCAN` | buf 0x151C | puente __Z29apple80211setOFFLOAD_SCANNINGP23IO80211Skyw |
| 197 | `APPLE80211_IOC_GAS_REQ` | `setGAS_REQ` | `len == 1812` / buf 0x714 | cascada IO80211InfraProtocol:0x1180 |
| 201 | `APPLE80211_IOC_AWDL_BTLE_PEER_INDICATION` | `setAWDL_BTLE_PEER_INDICATION` | `len == 32` / buf ? | PeerManager::setAwdlBtlePeerIndication() |
| 204 | `APPLE80211_IOC_AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS` | `setAWDL_BTLE_ENABLE_SYNC_WITH_PARAMS` | `len == 20` / buf ? | PeerManager::setAwdlBtleEnableSynchWithParams() |
| 205 | `APPLE80211_IOC_AWDL_SECONDARY_MASTER_CHANNEL` | `setAWDL_SECONDARY_MASTER_CHANNEL` | `len == 8` / buf ? | PeerManager::setAwdlSecondaryMasterChannel() |
| 208 | `APPLE80211_IOC_AWDL_AF_TX_MODE` | `setAWDL_AF_TX_MODE` | `len == 8` / buf ? | PeerManager::setAwdlAfTxMode() |
| 213 | `APPLE80211_IOC_AWDL_MIN_RATE` | `setAWDL_MIN_RATE` | `len == 8` / buf ? | PeerManager::setAwdlMinRate() |
| 214 | `APPLE80211_IOC_VHT_CAPABILITY` | `setVHT_CAPABILITY` | `len == 20` / buf ? | ? |
| 216 | `APPLE80211_IOC_ROAM_PROFILE` | `setROAM_PROFILE` | `len == 384` / buf 0x180 | cascada IO80211InfraProtocol:0x1190 |
| 217 | `APPLE80211_IOC_AWDL_OPER_MODE` | `setAWDL_OPER_MODE` | `len == 8` / buf ? | PeerManager::setAwdlOperatingMode() |
| 219 | `APPLE80211_IOC_AWDL_ENCRYPTION_KEYS` | `setAWDL_ENCRYPTION_KEYS` | `len == 148` / buf ? | PeerManager::setAwdlKey() |
| 220 | `APPLE80211_IOC_AWDL_ENCRYPTION_TYPE` | `setAWDL_ENCRYPTION_TYPE` | `len == 8` / buf ? | PeerManager::setAwdlEncryptType() |
| 223 | `APPLE80211_IOC_AWDL_STATISTICS` | `setAWDL_STATISTICS` | `len == 3752` / buf ? | ? |
| 228 | `APPLE80211_IOC_DBG_GUARD_TIME_PARAMS` | `setDBG_GUARD_TIME_PARAMS` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x1198 |
| 229 | `APPLE80211_IOC_AWDL_AWDL_ADVERTISERS` | `setAWDL_AWDL_ADVERTISERS` | `len == 44` / buf ? | PeerManager::setAwdlAdvertisers() |
| 230 | `APPLE80211_IOC_LEAKY_AP_STATS_MODE` | `setLEAKY_AP_STATS_MODE` | `len == 8` / buf ? | ? |
| 231 | `APPLE80211_IOC_CAPTURE` | `setCAPTURE` | buf ? | ? |
| 232 | `APPLE80211_IOC_LEAKY_AP_STATS` | `setLEAKY_AP_STATS` | buf ? | ? |
| 233 | `APPLE80211_IOC_AWDL_BLOCK_SET_COMMANDS` | `setAWDL_BLOCK_SET_COMMANDS` | `len == 8` / buf ? | PeerManager::setAwdlBlockSetCommands() |
| 236 | `APPLE80211_IOC_FORCE_SYNC_TO_PEER` | `setFORCE_SYNC_TO_PEER` | `len == 12` / buf ? | ? |
| 238 | `APPLE80211_IOC_PRIVATE_MAC` | `setPRIVATE_MAC` | `len == 28` / buf 0x1C | cascada IO80211InfraProtocol:0x11a8 |
| 239 | `APPLE80211_IOC_RESET_CHIP` | `setRESET_CHIP` | `len == 136` / buf 0x88 | cascada IO80211InfraProtocol:0x11b0 |
| 240 | `APPLE80211_IOC_CRASH` | `setCRASH` | `len != 8` / buf 8 | cascada IO80211InfraProtocol:0x11b8 |
| 241 | `APPLE80211_IOC_RANGING_ENABLE` | `setRANGING_ENABLE` | `len != 1800` / buf ? | ? |
| 242 | `APPLE80211_IOC_RANGING_START` | `setRANGING_START` | `len == 1800` / buf ? | ? |
| 245 | `APPLE80211_IOC_LEAKY_AP_SSID_STATS` | `setLEAKY_AP_SSID_STATS` | `len == 1244` / buf ? | ? |
| 251 | `APPLE80211_IOC_TKO_PARAMS` | `setTKO_PARAMS` | buf ? | ? |
| 253 | `APPLE80211_IOC_AWDL_NEARBY_LOG_TRIGGER` | `setAWDL_NEARBY_LOG_TRIGGER` | `len == 8` / buf ? | PeerManager::setAwdlNearbyLogTrigger() |
| 255 | `APPLE80211_IOC_BTCOEX_PROFILE` | `setBTCOEX_PROFILE` | `len == 56` / buf 0x38 | puente __Z28apple80211setBTCOEX_PROFILESP23IO80211Skywa |
| 256 | `APPLE80211_IOC_BTCOEX_PROFILE_ACTIVE` | `setBTCOEX_PROFILE_ACTIVE` | buf 8 | cascada IO80211InfraProtocol:0x11e8 |
| 258 | `APPLE80211_IOC_THERMAL_INDEX` | `setTHERMAL_INDEX` | buf 8 | cascada IO80211InfraProtocol:0x11f0 |
| 260 | `APPLE80211_IOC_BTCOEX_2G_CHAIN_DISABLE` | `setBTCOEX_2G_CHAIN_DISABLE` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x11f8 |
| 261 | `APPLE80211_IOC_POWER_BUDGET` | `setPOWER_BUDGET` | buf 8 | cascada IO80211InfraProtocol:0x1200 |
| 265 | `APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE` | `setOFFLOAD_TCPKA_ENABLE` | buf 8 | cascada IO80211InfraProtocol:0x1208 |
| 268 | `APPLE80211_IOC_NAN_ENABLED` | `setNAN_ENABLED` | `len == 8` / buf ? | ? |
| 270 | `APPLE80211_IOC_NAN_CLUSTER_ID` | `setNAN_CLUSTER_ID` | `len == 12` / buf ? | ? |
| 271 | `APPLE80211_IOC_NAN_DISC_BCN_PERIOD` | `setNAN_DISC_BCN_PERIOD` | `len == 2` / buf ? | ? |
| 272 | `APPLE80211_IOC_NAN_SYNC_BCN_PERIOD` | `setNAN_SYNC_BCN_PERIOD` | `len == 2` / buf ? | ? |
| 273 | `APPLE80211_IOC_NAN_DW_AWAKE_PERIOD` | `setNAN_DW_AWAKE_PERIOD` | `len == 2` / buf ? | ? |
| 274 | `APPLE80211_IOC_NAN_BCN_TX_CONTROL` | `setNAN_BCN_TX_CONTROL` | `len == 2` / buf ? | ? |
| 275 | `APPLE80211_IOC_NAN_HOST_ELECTION` | `setNAN_HOST_ELECTION` | `len == 1` / buf ? | ? |
| 276 | `APPLE80211_IOC_NAN_RSSI_CLOSE` | `setNAN_RSSI_CLOSE` | `len == 1` / buf ? | ? |
| 277 | `APPLE80211_IOC_NAN_RSSI_MIDDLE` | `setNAN_RSSI_MIDDLE` | `len == 1` / buf ? | ? |
| 278 | `APPLE80211_IOC_NAN_RSSI_5G_CLOSE` | `setNAN_RSSI_5G_CLOSE` | `len == 1` / buf ? | ? |
| 279 | `APPLE80211_IOC_NAN_RSSI_5G_MIDDLE` | `setNAN_RSSI_5G_MIDDLE` | `len == 1` / buf ? | ? |
| 281 | `APPLE80211_IOC_NAN_MASTER_PREFERENCE` | `setNAN_MASTER_PREFERENCE` | `len == 2` / buf ? | ? |
| 282 | `APPLE80211_IOC_NAN_RANDOM_FACTOR` | `setNAN_RANDOM_FACTOR` | `len == 2` / buf ? | ? |
| 283 | `APPLE80211_IOC_NAN_RANDOM_FACTOR_ROTATION` | `setNAN_RANDOM_FACTOR_ROTATION` | `len == 1` / buf ? | ? |
| 284 | `APPLE80211_IOC_NAN_HOP_COUNT_TO_ANCHOR_MASTER` | `setNAN_HOP_COUNT_TO_ANCHOR_MASTER` | `len == 2` / buf ? | ? |
| 285 | `APPLE80211_IOC_NAN_MASTER_RANK` | `setNAN_MASTER_RANK` | `len == 8` / buf ? | ? |
| 286 | `APPLE80211_IOC_NAN_MAX_PEERS` | `setNAN_MAX_PEERS` | `len == 1` / buf ? | ? |
| 287 | `APPLE80211_IOC_NAN_MASTER_CHANNEL` | `setNAN_MASTER_CHANNEL` | `len == 1` / buf ? | ? |
| 288 | `APPLE80211_IOC_NAN_SECONDARY_MASTER_CHANNEL` | `setNAN_SECONDARY_MASTER_CHANNEL` | `len == 1` / buf ? | ? |
| 289 | `APPLE80211_IOC_NAN_TX_RATE` | `setNAN_TX_RATE` | `len == 2` / buf ? | ? |
| 290 | `APPLE80211_IOC_NAN_WARMUP_TIME` | `setNAN_WARMUP_TIME` | `len == 2` / buf ? | ? |
| 293 | `APPLE80211_IOC_COMPANION_SKYWALK_IF_CREATE` | `setCOMPANION_SKYWALK_IF_CREATE` | `len == 24` / buf 0x18 | **vtable AWDL +0x1180** |
| 297 | `APPLE80211_IOC_NAN_HOP_COUNT_LIMIT` | `setNAN_HOP_COUNT_LIMIT` | `len == 1` / buf ? | ? |
| 299 | `APPLE80211_IOC_NAN_CHANNEL_SEQUENCE` | `setNAN_CHANNEL_SEQUENCE` | `len == 243` / buf ? | ? |
| 300 | `APPLE80211_IOC_NAN_PUBLISH` | `setNAN_PUBLISH` | `len == 5576` / buf ? | ? |
| 301 | `APPLE80211_IOC_NAN_CANCEL_PUBLISH` | `setNAN_CANCEL_PUBLISH` | `len == 1` / buf ? | ? |
| 303 | `APPLE80211_IOC_NAN_SUBSCRIBE` | `setNAN_SUBSCRIBE` | `len == 3604` / buf ? | ? |
| 306 | `APPLE80211_IOC_NAN_CANCEL_SUBSCRIBE` | `setNAN_CANCEL_SUBSCRIBE` | `len == 1` / buf ? | ? |
| 307 | `APPLE80211_IOC_NAN_FOLLOW_UP_TRANSMIT` | `setNAN_FOLLOW_UP_TRANSMIT` | `len == 3360` / buf ? | ? |
| 311 | `APPLE80211_IOC_NAN_SERVICE_DESCRIPTOR_EXTENSION` | `setNAN_SERVICE_DESCRIPTOR_EXTENSION` | `len == 8` / buf ? | ? |
| 312 | `APPLE80211_IOC_NAN_RANGING_REQUEST` | `setNAN_RANGING_REQUEST` | `len == 32` / buf ? | ? |
| 313 | `APPLE80211_IOC_NAN_RANGING_RESPONSE` | `setNAN_RANGING_RESPONSE` | `len == 1` / buf ? | ? |
| 314 | `APPLE80211_IOC_NAN_RANGING_CANCEL` | `setNAN_RANGING_CANCEL` | `len == 4` / buf ? | ? |
| 315 | `APPLE80211_IOC_NAN_DP_REQ` | `setNAN_DP_REQ` | `len == 2156` / buf ? | ? |
| 316 | `APPLE80211_IOC_NAN_DP_RESP` | `setNAN_DP_RESP` | `len == 2176` / buf ? | ? |
| 317 | `APPLE80211_IOC_NAN_SCHEDULE_UPDATE` | `setNAN_SCHEDULE_UPDATE` | `len == 50` / buf ? | ? |
| 318 | `APPLE80211_IOC_NAN_DP_END` | `setNAN_DP_END` | `len == 20` / buf ? | ? |
| 319 | `APPLE80211_IOC_NAN_DP_CONF` | `setNAN_DP_CONF` | `len == 8` / buf ? | ? |
| 321 | `APPLE80211_IOC_NAN_COMMITTED_AVAILABILITY` | `setNAN_COMMITTED_AVAILABILITY` | `len == 2576` / buf ? | ? |
| 322 | `APPLE80211_IOC_NAN_POTENTIAL_AVAILABILITY` | `setNAN_POTENTIAL_AVAILABILITY` | `len == 2582` / buf ? | ? |
| 324 | `APPLE80211_IOC_NAN_NDC_AVAILABILITY` | `setNAN_NDC_AVAILABILITY` | `len == 158` / buf ? | ? |
| 325 | `APPLE80211_IOC_NAN_IMMUTABLE_AVAILABILITY` | `setNAN_IMMUTABLE_AVAILABILITY` | `len == 146` / buf ? | ? |
| 326 | `APPLE80211_IOC_NAN_RANGING_AVAILABILITY` | `setNAN_RANGING_AVAILABILITY` | `len == 146` / buf ? | ? |
| 328 | `APPLE80211_IOC_NAN_CTL` | `setNAN_CTL` | `len == 243872` / buf ? | ? |
| 330 | `APPLE80211_IOC_NAN_VENDOR_PAYLOAD` | `setNAN_VENDOR_PAYLOAD` | `len == 264` / buf ? | ? |
| 332 | `APPLE80211_IOC_NAN_QOS_PARAMS` | `setNAN_QOS_PARAMS` | `len == 20` / buf ? | ? |
| 333 | `APPLE80211_IOC_NAN_FAST_DISCOVERY` | `setNAN_FAST_DISCOVERY` | `len == 160` / buf ? | ? |
| 334 | `APPLE80211_IOC_RADIO_CHANSEQ_CONTROL` | `setRADIO_CHANSEQ_CONTROL` | `len == 2980` / buf ? | ? |
| 336 | `APPLE80211_IOC_HOST_AP_MODE_HIDDEN` | `setHOST_AP_MODE_HIDDEN` | buf 8 | cascada IO80211SapProtocol:0x1070 |
| 337 | `APPLE80211_IOC_LQM_CONFIG` | `setLQM_CONFIG` | `len == 36` / buf 0x24 | cascada IO80211InfraProtocol:0x1210 |
| 340 | `APPLE80211_IOC_TRAFFIC_REGISTRATION` | `setTRAFFIC_REGISTRATION` | `len == 512` / buf ? | PeerManager::setTrafficRegistration() |
| 343 | `APPLE80211_IOC_AWDL_LOW_LATENCY_STATISTICS` | `setAWDL_LOW_LATENCY_STATISTICS` | `len == 4024` / buf ? | ? |
| 344 | `APPLE80211_IOC_LLW_PARAMS` | `setLLW_PARAMS` | `len == 44` / buf ? | PeerManager::setLLWParams() |
| 345 | `APPLE80211_IOC_DYNAMIC_RSSI_WINDOW_CONFIG` | `setDYNAMIC_RSSI_WINDOW_CONFIG` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x1218 |
| 347 | `APPLE80211_IOC_SOFTAP_PARAMS` | `setSOFTAP_PARAMS` | `len == 28` / buf 0x1C | cascada IO80211SapProtocol:0x1078 |
| 349 | `APPLE80211_IOC_SOFTAP_TRIGGER_CSA` | `setSOFTAP_TRIGGER_CSA` | `len == 24` / buf 0x18 | cascada IO80211SapProtocol:0x1080 |
| 352 | `APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE` | `setSOFTAP_WIFI_NETWORK_INFO_IE` | `len == 36` / buf 0x24 | cascada IO80211SapProtocol:0x1088 |
| 355 | `APPLE80211_IOC_NAN_LLW_PARAMS` | `setNAN_LLW_PARAMS` | `len == 44` / buf ? | ? |
| 358 | `APPLE80211_IOC_BT_COEX_ULOFDMA_DISABLE` | `setBT_COEX_ULOFDMA_DISABLE` | `len == 1` / buf 1 | cascada IO80211NoneProtocol:0x1750 |
| 363 | `APPLE80211_IOC_USB_HOST_NOTIFICATION` | `setUSB_HOST_NOTIFICATION` | `len == 16` / buf 0x10 | cascada IO80211InfraProtocol:0x1220 |
| 365 | `APPLE80211_IOC_DISPLAY_STATE` | `setDISPLAY_STATE` | buf ? | ? |
| 366 | `APPLE80211_IOC_NAN_OOB_AF_TX` | `setNAN_OOB_AF_TX` | `len == 1001` / buf ? | ? |
| 367 | `APPLE80211_IOC_NAN_DATA_PATH_KEEP_ALIVE_IDENTIFIER` | `setNAN_DATA_PATH_KEEP_ALIVE_IDENTIFIER` | `len == 64` / buf ? | ? |
| 368 | `APPLE80211_IOC_SET_MAC_ADDRESS` | `setSET_MAC_ADDRESS` | `len == 6` / buf 6 | cascada IO80211SkywalkInterface:? |
| 371 | `APPLE80211_IOC_HP2P_CTRL` | `setH` | `len == 8` / buf ? | ? |
| 372 | `APPLE80211_IOC_BSS_BLACKLIST` | `setBSS_BLACKLIST` | `len == 43` / buf 0x2B | cascada IO80211InfraProtocol:0x1230 |
| 380 | `APPLE80211_IOC_SET_RANGING_IDENTIFIER` | `setSET_RANGING_IDENTIFIER` | `len == 16` / buf ? | ? |
| 381 | `APPLE80211_IOC_SET_RANGING_FOLLOW_MODE` | `setSET_RANGING_FOLLOW_MODE` | `len == 12` / buf ? | ? |
| 382 | `APPLE80211_IOC_ABORT_SCAN` | `setABORT_SCAN` | `len == 4` / buf 4 | cascada IO80211NoneProtocol:0x18d0 |
| 383 | `APPLE80211_IOC_SET_PROPERTY` | `setSET_PROPERTY` | `len != 0` / buf 0x10 | cascada IO80211InfraProtocol:0x1238 |
| 385 | `APPLE80211_IOC_NAN_DP_LINK_STATUS` | `setNAN_DP_LINK_STATUS` | `len == 12` / buf ? | ? |
| 388 | `APPLE80211_IOC_ROAM_CACHE_UPDATE` | `setROAM_CACHE_UPDATE` | `len == 56` / buf 0x38 | cascada IO80211InfraProtocol:0x1240 |
| 389 | `APPLE80211_IOC_USERSPACE_P2P_OPTIONS` | `setUSERSPACE_` | `len == 16` / buf ? | ? |
| 392 | `APPLE80211_IOC_PM_MODE` | `setPM_MODE` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x1248 |
| 394 | `APPLE80211_IOC_SET_WIFI_ASSERTION_STATE` | `setSET_WIFI_ASSERTION_STATE` | buf 8 | cascada IO80211InfraProtocol:0x1250 |
| 398 | `APPLE80211_IOC_NAN_RESTRICT_POWER_SAVE` | `setNAN_RESTRICT_POWER_SAVE` | `len == 8` / buf ? | ? |
| 399 | `APPLE80211_IOC_REASSOCIATE_WITH_CORECAPTURE` | `setREASSOCIATE_WITH_CORECAPTURE` | `len == 68` / buf 0x44 | cascada IO80211NoneProtocol:0x19e0 |
| 400 | `APPLE80211_IOC_NAN_INFRA_STATUS` | `setNAN_INFRA_STATUS` | `len == 12` / buf ? | ? |
| 403 | `APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE` | `setSOFTAP_EXTENDED_CAPABILITIES_IE` | `len == 18` / buf 0x12 | cascada IO80211SapProtocol:0x1090 |
| 404 | `APPLE80211_IOC_ENABLE_PACKET_TS` | `setENABLE_PACKET_TS` | buf ? | ? |
| 405 | `APPLE80211_IOC_DISABLE_PACKET_TS` | `setDISABLE_PACKET_TS` | buf ? | ? |
| 407 | `APPLE80211_IOC_REALTIME_QOS_MSCS` | `setREALTIME_QOS_MSCS` | buf 8 | cascada IO80211InfraProtocol:0x1258 |
| 408 | `APPLE80211_IOC_SENSING_ENABLE` | `setSENSING_ENABLE` | buf ? | ? |
| 409 | `APPLE80211_IOC_SENSING_DISABLE` | `setSENSING_DISABLE` | buf ? | ? |
| 411 | `APPLE80211_IOC_TX_RATE` | `setTX_RATE` | `len == 16` / buf ? | PeerManager::setAwdlTxRate() |
| 412 | `APPLE80211_IOC_NANPHS_ASSOCIATION` | `setNANPHS_ASSOCIATION` | `len == 39` / buf 0x27 | cascada IO80211NoneProtocol:0x1ab0 |
| 413 | `APPLE80211_IOC_NANPHS_TERMINATED` | `setNANPHS_TERMINATED` | `len == 39` / buf 0x27 | cascada IO80211NoneProtocol:0x1ac0 |
| 415 | `APPLE80211_IOC_POST_PRESENCE_DONE` | `setPOST_PRESENCE_DONE` | `len == 12` / buf ? | PeerManager::setAwdlPostPeerPresenceDone() |
| 416 | `APPLE80211_IOC_AWDL_MI_BITMAP` | `setAWDL_MI_BITMAP` | `len == 8` / buf ? | PeerManager::setAwdlMiBitmap() |
| 417 | `APPLE80211_IOC_AWDL_UMI_TX_PEER` | `setAWDL_UMI_TX_PEER` | `len == 16` / buf ? | PeerManager::setAWDLUMIToPeer() |
| 419 | `APPLE80211_IOC_LTE_RESTRICTED_CHANNELS` | `setLTE_RESTRICTED_CHANNELS` | `len == 136` / buf ? | PeerManager::setLteRestrictedChannels() |
| 420 | `APPLE80211_IOC_NANPH_SOFTAP_CSA` | `setNANPH_SOFTAP_CSA` | `len == 1` / buf ? | PeerManager::setSoftApCSANanPh() |
| 422 | `APPLE80211_IOC_6G_MODE` | `set6G_MODE` | `len == 8` / buf 8 | cascada IO80211NoneProtocol:0x1b50 |
| 448 | `APPLE80211_IOC_GAME_MODE_STATE` | `setGAME_MODE_STATE` | `len == 4` / buf 4 | ? |
| 450 | `APPLE80211_IOC_GATEWAY_ADDRESS` | `setGATEWAY_ADDRESS` | `len == 56` / buf ? | ? |
| 451 | `APPLE80211_IOC_UNICAST_INFRA_PEER_ADDRESS` | `setUNICAST_INFRA_PEER_ADDRESS` | `len == 56` / buf ? | ? |
| 452 | `APPLE80211_IOC_INFRA_PEERS_LOGGING_ENABLED` | `setINFRA_PEERS_LOGGING_ENABLED` | `len == 8` / buf ? | ? |
| 453 | `APPLE80211_IOC_VOICE_IND_STATE` | `setVOICE_IND_STATE` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x12f0 |
| 457 | `APPLE80211_IOC_RSN_XE` | `setRSN_XE` | `len == 264` / buf 0x108 | cascada IO80211InfraProtocol:0x12f8 |
| 459 | `APPLE80211_IOC_MWS_ACCESSORY_POWER_LIMIT_WIFI_ENH` | `setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH` | `len == 6` / buf 6 | cascada IO80211InfraProtocol:0x1300 |
| 469 | `APPLE80211_IOC_OS_FEATURE_FLAGS` | `setOS_FEATURE_FLAGS` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x1320 |
| 470 | `APPLE80211_IOC_DHCP_RENEWAL_DATA` | `setDHCP_RENEWAL_DATA` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x1328 |
| 471 | `APPLE80211_IOC_MOVING_NETWORK` | `setMOVING_NETWORK` | `len == 1` / buf 1 | cascada IO80211NoneProtocol:0x1e60 |
| 472 | `APPLE80211_IOC_BATTERY_POWERSAVE_CONFIG` | `setBATTERY_POWERSAVE_CONFIG` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x1330 |
| 473 | `APPLE80211_IOC_MIMO_CONFIG` | `setMIMO_CONFIG` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x1338 |
| 479 | `APPLE80211_IOC_POWER_PROFILE` | `setPOWER_PROFILE` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x1360 |
| 480 | `APPLE80211_IOC_FASTLANE_PROFILE_STATE` | `setFASTLANE_PROFILE_STATE` | `len == 1` / buf 1 | cascada IO80211NoneProtocol:0x1ef0 |
| 481 | `APPLE80211_IOC_HEARTBEAT` | `setHEARTBEAT` | buf 0 | cascada IO80211InfraProtocol:0x1368 |
| 482 | `APPLE80211_IOC_INTERFACE_SETTING` | `setINTERFACE_SETTING` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x1370 |
| 483 | `APPLE80211_IOC_EXTERNAL_POWER_STATE` | `setEXTERNAL_POWER_STATE` | `len == 1` / buf 1 | cascada IO80211NoneProtocol:0x1f20 |
| 484 | `APPLE80211_IOC_APP_REPORT` | `setAPP_REPORT` | `len == 4` / buf 4 | cascada IO80211NoneProtocol:0x1f30 |
| 485 | `APPLE80211_IOC_APP_STATE` | `setAPP_STATE` | `len == 2` / buf ? | ? |
| 486 | `APPLE80211_IOC_BYPASS_TX_POWER_CAP` | `setBYPASS_TX_POWER_CAP` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x1378 |
| 487 | `APPLE80211_IOC_DPS_SYMPTOMS_INPUT` | `setDPS_SYMPTOMS_INPUT` | `len == 32` / buf ? | ? |
| 488 | `APPLE80211_IOC_FACETIME_WIFICALLING_PARAMS` | `setFACETIME_WIFICALLING_PARAMS` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x1380 |
| 489 | `APPLE80211_IOC_IPV4_PARAMS` | `setIPV4_PARAMS` | `len == 16` / buf 0x10 | cascada IO80211InfraProtocol:0x1388 |
| 490 | `APPLE80211_IOC_PRODUCT_INFO` | `setPRODUCT_INFO` | `len == 300` / buf 0x12C | cascada IO80211NoneProtocol:0x1f90 |
| 496 | `APPLE80211_IOC_WOW_LPAS_CONFIG` | `setWOW_LPAS_CONFIG` | `len == 16` / buf 0x10 | cascada IO80211NoneProtocol:0x1ff0 |
| 497 | `APPLE80211_IOC_EAP_FILTER_CONFIG` | `setEAP_FILTER_CONFIG` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x13b0 |
| 498 | `APPLE80211_IOC_WOW_LOW_POWER_MODE` | `setWOW_LOW_POWER_MODE` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x13b8 |
| 499 | `APPLE80211_IOC_DUAL_POWER_MODE` | `setDUAL_POWER_MODE` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x13c0 |
| 500 | `APPLE80211_IOC_BGSCAN_PARAMS` | `setBGSCAN_PARAMS` | `len == 4848` / buf 0x12F0 | cascada IO80211NoneProtocol:0x2030 |
| 501 | `APPLE80211_IOC_BGSCAN_DYNAMIC_PROFILE_PARAMS` | `setBGSCAN_DYNAMIC_PROFILE_PARAMS` | `len == 24` / buf 0x18 | cascada IO80211NoneProtocol:0x2040 |
| 503 | `APPLE80211_IOC_VIRTUAL_IF_STATE` | `setVIRTUAL_IF_STATE` | `len == 20` / buf 0x14 | ? |
| 507 | `APPLE80211_IOC_CONGESTION_CTRL_IND` | `setCONGESTION_CTRL_IND` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x13d8 |
| 508 | `APPLE80211_IOC_MIS_MAX_STA` | `setMIS_MAX_STA` | `len == 4` / buf 4 | cascada IO80211SapProtocol:0x1098 |
| 509 | `APPLE80211_IOC_STAND_ALONE_MODE_STATE` | `setSTAND_ALONE_MODE_STATE` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x13e0 |
| 510 | `APPLE80211_IOC_IPV6_PARAMS` | `setIPV6_PARAMS` | `len == 164` / buf 0xA4 | cascada IO80211InfraProtocol:0x13e8 |
| 511 | `APPLE80211_IOC_TRIGGER_DISCONNECT` | `setTRIGGER_DISCONNECT` | `len == 1` / buf ? | ? |
| 512 | `APPLE80211_IOC_INFRA_ENUMERATED` | `setINFRA_ENUMERATED` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x13f0 |
| 513 | `APPLE80211_IOC_LMTPC_CONFIG` | `setLMTPC_CONFIG` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x13f8 |
| 514 | `APPLE80211_IOC_TRAFFIC_ENG_PARAMS` | `setTRAFFIC_ENG_PARAMS` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x1400 |
| 516 | `APPLE80211_IOC_BLOCKED_BANDS` | `setBLOCKED_BANDS` | `len == 4` / buf 4 | cascada IO80211NoneProtocol:0x2110 |
| 519 | `APPLE80211_IOC_R1_COEX_ULOFDMA_DISABLE` | `setR1_COEX_ULOFDMA_DISABLE` | `len == 1` / buf 1 | cascada IO80211NoneProtocol:0x2130 |
| 520 | `APPLE80211_IOC_RC2_COEX_ULOFDMA_DISABLE` | `setRC2_COEX_ULOFDMA_DISABLE` | `len == 1` / buf 1 | cascada IO80211NoneProtocol:0x2140 |
| 521 | `APPLE80211_IOC_LE_SCAN_PARAM` | `setLE_SCAN_PARAM` | `len == 20` / buf 0x14 | cascada IO80211InfraProtocol:0x1408 |
| 522 | `APPLE80211_IOC_TIMESYNC_GPIO` | `setTIMESYNC_GPIO` | `len == 24` / buf 0x18 | cascada IO80211InfraProtocol:0x1410 |
| 523 | `APPLE80211_IOC_HOST_CLOCK_INFO` | `setHOST_CLOCK_INFO` | `len == 40` / buf 0x28 | cascada IO80211InfraProtocol:0x1418 |
| 525 | `APPLE80211_IOC_FW_CLOCK_SOURCE` | `setFW_CLOCK_SOURCE` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x1420 |
| 526 | `APPLE80211_IOC_TIMESYNC_TX_POLICY` | `setTIMESYNC_TX_POLICY` | `len == 16` / buf 0x10 | cascada IO80211InfraProtocol:0x1428 |
| 527 | `APPLE80211_IOC_TIMESYNC_RX_POLICY` | `setTIMESYNC_RX_POLICY` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x1430 |
| 528 | `APPLE80211_IOC_TIMESTAMPING_EN` | `setTIMESTAMPING_EN` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x1438 |
| 531 | `APPLE80211_IOC_ROAM_PARAMS` | `setROAM_PARAMS` | `len == 6` / buf 6 | cascada IO80211NoneProtocol:0x21f0 |
| 532 | `APPLE80211_IOC_ROAM_ADAPTIVE_PARAMS` | `setROAM_ADAPTIVE_PARAMS` | `len == 128` / buf 0x80 | cascada IO80211NoneProtocol:0x2200 |
| 536 | `APPLE80211_IOC_SLOWWIFI_PROBE_RESULT` | `setSLOWWIFI_PROBE_RESULT` | `len == 4` / buf ? | ? |
| 537 | `APPLE80211_IOC_SLOWWIFI_USER_INPUT_RESULT` | `setSLOWWIFI_USER_INPUT_RESULT` | `len == 4` / buf ? | ? |
| 538 | `APPLE80211_IOC_SLOWWIFI_SYMPTOMS_TRIGGER` | `setSLOWWIFI_SYMPTOMS_TRIGGER` | `len == 8` / buf ? | ? |
| 539 | `APPLE80211_IOC_MWS_TIME_SHARING_WIFI_ENH` | `setMWS_TIME_SHARING_WIFI_ENH` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x1448 |
| 540 | `APPLE80211_IOC_MWS_WIFI_TYPE_7_BITMAP_WIFI_ENH` | `setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH` | `len == 36` / buf 0x24 | cascada IO80211InfraProtocol:0x1450 |
| 541 | `APPLE80211_IOC_MWS_COEX_BITMAP_WIFI_ENH` | `setMWS_COEX_BITMAP_WIFI_ENH` | `len == 36` / buf 0x24 | cascada IO80211InfraProtocol:0x1458 |
| 542 | `APPLE80211_IOC_MWS_DISABLE_OCL_BITMAP_WIFI_ENH` | `setMWS_DISABLE_OCL_BITMAP_WIFI_ENH` | `len == 36` / buf 0x24 | cascada IO80211InfraProtocol:0x1460 |
| 543 | `APPLE80211_IOC_MWS_RFEM_CONFIG_WIFI_ENH` | `setMWS_RFEM_CONFIG_WIFI_ENH` | `len == 40` / buf 0x28 | cascada IO80211InfraProtocol:0x1468 |
| 544 | `APPLE80211_IOC_MWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH` | `setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH` | `len == 36` / buf 0x24 | cascada IO80211InfraProtocol:0x1470 |
| 545 | `APPLE80211_IOC_MWS_SCAN_FREQ_WIFI_ENH` | `setMWS_SCAN_FREQ_WIFI_ENH` | `len == 40` / buf 0x28 | cascada IO80211InfraProtocol:0x1478 |
| 546 | `APPLE80211_IOC_MWS_SCAN_FREQ_MODE_WIFI_ENH` | `setMWS_SCAN_FREQ_MODE_WIFI_ENH` | `len == 40` / buf 0x28 | cascada IO80211InfraProtocol:0x1480 |
| 547 | `APPLE80211_IOC_MWS_CONDITION_ID_BITMAP_WIFI_ENH` | `setMWS_CONDITION_ID_BITMAP_WIFI_ENH` | `len == 324` / buf 0x144 | cascada IO80211InfraProtocol:0x1488 |
| 548 | `APPLE80211_IOC_MWS_ANTENNA_SELECTION_WIFI_ENH` | `setMWS_ANTENNA_SELECTION_WIFI_ENH` | `len == 20` / buf 0x14 | cascada IO80211InfraProtocol:0x1490 |
| 551 | `APPLE80211_IOC_NDD_REQ` | `setNDD_REQ` | `len == 244` / buf 0xF4 | cascada IO80211InfraProtocol:0x1498 |
| 553 | `APPLE80211_IOC_NAN_CREATE_GCR_SESSION` | `setNAN_CREATE_GCR_SESSION` | `len == 64` / buf ? | ? |
| 554 | `APPLE80211_IOC_NAN_JOIN_GCR_SESSION` | `setNAN_JOIN_GCR_SESSION` | `len == 56` / buf ? | ? |
| 555 | `APPLE80211_IOC_NAN_GCR_RATE` | `setNAN_GCR_RATE` | `len == 24` / buf ? | ? |
| 558 | `APPLE80211_IOC_SDB_ENABLE` | `setSDB_ENABLE` | `len == 16` / buf 0x10 | cascada IO80211InfraProtocol:0x14a8 |
| 559 | `APPLE80211_IOC_NAN_PAIRING_KEYS` | `setNAN_PAIRING_KEYS` | `len == 244` / buf ? | ? |
| 560 | `APPLE80211_IOC_PROTOCOL_CLEANUP_COMPLETE` | `setPROTOCOL_CLEANUP_COMPLETE` | `len == 4` / buf 4 | ? |
| 562 | `APPLE80211_IOC_BTCOEX_EXT_PROFILE` | `setBTCOEX_EXT_PROFILE` | `len == 45` / buf 0x2D | cascada IO80211InfraProtocol:0x14b0 |
| 564 | `APPLE80211_IOC_FILTER_MONITOR_EVENTS` | `setFILTER_MONITOR_EVENTS` | `len == 32` / buf ? | ? |
| 565 | `APPLE80211_IOC_TD_IMMINENT` | `setTD_IMMINENT` | `len == 4` / buf 4 | ? |
| 566 | `APPLE80211_IOC_HAS_BATTERY` | `setHAS_BATTERY` | `len == 8` / buf ? | ? |
| 571 | `APPLE80211_IOC_DEVICE_ORIENTATION` | `setDEVICE_ORIENTATION` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x14b8 |
| 572 | `APPLE80211_IOC_ACCESSORY_STATE` | `setACCESSORY_STATE` | `len == 12` / buf 0xC | cascada IO80211InfraProtocol:0x14c0 |
| 575 | `APPLE80211_IOC_OS_ELIGIBILITY` | `setOS_ELIGIBILITY` | `len == 4` / buf 4 | cascada IO80211InfraProtocol:0x14c8 |
| 577 | `APPLE80211_IOC_NAN_COUNTRY_CODE_SYNCING` | `setNAN_COUNTRY_CODE_SYNCING` | `len == 8` / buf ? | ? |
| 578 | `APPLE80211_IOC_TX_MODE_CONFIG` | `setTX_MODE_CONFIG` | `len == 1` / buf 1 | cascada IO80211InfraProtocol:0x14d0 |
| 579 | `APPLE80211_IOC_NAN_VENDOR_PAYLOAD_COMPACT` | `setNAN_VENDOR_PAYLOAD_COMPACT` | `len == 264` / buf ? | ? |
| 580 | `APPLE80211_IOC_NAN_ADDITIONAL_ATTR` | `setNAN_ADDITIONAL_ATTR` | `len == 264` / buf ? | ? |
| 582 | `APPLE80211_IOC_REAL_TIME_3PARTY_APP` | `setREAL_TIME_3PARTY_APP` | `len == 4` / buf 4 | ? |
| 583 | `APPLE80211_IOC_NAN_REMOVE_MULTICAST_PEER` | `setNAN_REMOVE_MULTICAST_PEER` | `len == 12` / buf ? | ? |
| 584 | `APPLE80211_IOC_NB_RANGING_SESSION` | `setNB_RANGING_SESSION` | `len == 4` / buf 4 | ? |
| 585 | `APPLE80211_IOC_MITIGATE_INTERFERENCE` | `setMITIGATE_INTERFERENCE` | `len == 8` / buf 8 | cascada IO80211InfraProtocol:0x14d8 |
| 586 | `APPLE80211_IOC_NAN_FORCE_CHANNEL_SEQ` | `setNAN_FORCE_CHANNEL_SEQ` | `len == 20` / buf ? | ? |

## 5. Los tipos que llegan a la superficie AWDL (la lista que hay que implementar)

Tabla consolidada: para cada tipo, el slot GET y SET (offset sobre el vptr de la interfaz AWDL),
el método que Broadcom instala en ese slot en `AppleBCMWLANProximityInterface` (vtable
0xffffff80017f5b78), el struct del dato (del nombre mangleado del puente) y la longitud que exige
el handler moderno cuando existe. «PM» = método equivalente del camino moderno en
`IO80211AWDLPeerManager`.

| tipo | IOC | GET slot | método GET (Broadcom) | SET slot | método SET (Broadcom) | struct | len GET/SET | PM GET/SET |
|---:|---|---|---|---|---|---|---|---|
| 1 | `APPLE80211_IOC_SSID` | 0xfc8 | `AppleBCMWLANProximityInterface::getSSID` | 0x10a0 | `AppleBCMWLANProximityInterface::setSSID` | `apple80211_ssid_data` | — / — | — / — |
| 4 | `APPLE80211_IOC_CHANNEL` | 0xfd0 | `AppleBCMWLANProximityInterface::getCHANNEL` | 0x10a8 | `AppleBCMWLANProximityInterface::setCHANNEL` | `apple80211_channel_data` | ==16 / !=0 | — / — |
| 9 | `APPLE80211_IOC_BSSID` | 0xfd8 | `AppleBCMWLANProximityInterface::getBSSID` | — | `—` | `apple80211_bssid_data` | <6 / — | — / — |
| 13 | `APPLE80211_IOC_STATE` | 0xfe0 | `AppleBCMWLANProximityInterface::getSTATE` | — | `—` | `apple80211_state_data` | ==8 / — | — / — |
| 116 | `APPLE80211_IOC_AWDL_SYNC_PARAMS` | 0xfe8 | `AppleBCMWLANProximityInterface::getAWDL_SYNC_PARAMS` | 0x10b0 | `AppleBCMWLANProximityInterface::setAWDL_SYNC_PARAMS` | `apple80211_awdl_sync_params` | ==24 / ==24 | getAwdlSyncParams / setAwdlSyncParams |
| 117 | `APPLE80211_IOC_AWDL_SYNC_ENABLED` | 0xff0 | `AppleBCMWLANProximityInterface::getAWDL_SYNC_ENABLED` | 0x10b8 | `AppleBCMWLANProximityInterface::setAWDL_SYNC_ENABLED` | `apple80211_awdl_sync_enabled` | ==12 / ==12 | getAwdlSyncEnabled / setAwdlSyncEnabled |
| 118 | `APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | 0xff8 | `AppleBCMWLANProximityInterface::getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | 0x10c0 | `AppleBCMWLANProximityInterface::setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | `apple80211_awdl_extension_state_machine_parameter` | ==20 / ==20 | getAwdlExtensionParams / setAwdlExtensionParams |
| 122 | `APPLE80211_IOC_AWDL_ELECTION_ID` | 0x1000 | `AppleBCMWLANProximityInterface::getAWDL_ELECTION_ID` | 0x10c8 | `AppleBCMWLANProximityInterface::setAWDL_ELECTION_ID` | `apple80211_awdl_election_id` | ==8 / ==8 | getAwdlElectionId / setAwdlElectionId |
| 125 | `APPLE80211_IOC_AWDL_BSSID` | — | `—` | 0x10d0 | `AppleBCMWLANProximityInterface::setAWDL_BSSID` | `apple80211_awdl_bssid` | — / ==16 | — / setAwdlBssid |
| 126 | `APPLE80211_IOC_AWDL_ELECTION_METRIC` | 0x1008 | `AppleBCMWLANProximityInterface::getAWDL_ELECTION_METRIC` | 0x10d8 | `AppleBCMWLANProximityInterface::setAWDL_ELECTION_METRIC` | `apple80211_awdl_election_metric` | ==12 / ==12 | getAwdlElectionMetric / setAwdlElectionMetric |
| 129 | `APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 0x1010 | `AppleBCMWLANProximityInterface::getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 0x10e0 | `AppleBCMWLANProximityInterface::setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | `apple80211_awdl_sync_channel_sequence` | ==400 / ==400 | getAwdlChannelSequence / setAwdlChannelSequence |
| 130 | `APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE` | 0x1018 | `AppleBCMWLANProximityInterface::getPEER_CACHE_MAXIMUM_SIZE` | — | `—` | `apple80211_peer_cache_maximum_size` | ==8 / — | — / — |
| 132 | `APPLE80211_IOC_AWDL_MASTER_CHANNEL` | 0x1020 | `AppleBCMWLANProximityInterface::getAWDL_MASTER_CHANNEL` | 0x10e8 | `AppleBCMWLANProximityInterface::setAWDL_MASTER_CHANNEL` | `apple80211_awdl_master_channel` | ==8 / ==8 | getAwdlMasterChannel / setAwdlMasterChannel |
| 134 | `APPLE80211_IOC_AWDL_SYNC_STATE` | 0x1028 | `AppleBCMWLANProximityInterface::getAWDL_SYNC_STATE` | 0x10f0 | `AppleBCMWLANProximityInterface::setAWDL_SYNC_STATE` | `apple80211_awdl_sync_state` | ==24 / ==24 | getAwdlSyncState / setAwdlSyncState |
| 135 | `APPLE80211_IOC_AWDL_ELECTION_RSSI_THRESHOLDS` | 0x1030 | `AppleBCMWLANProximityInterface::getAWDL_ELECTION_RSSI_THRESHOLDS` | 0x10f8 | `AppleBCMWLANProximityInterface::setAWDL_ELECTION_RSSI_THRESHOLDS` | `apple80211_awdl_election_rssi_thresholds` | ==20 / ==20 | — / setAwdlElectionRssiThresholds |
| 136 | `APPLE80211_IOC_AWDL_PRESENCE_MODE` | 0x1038 | `AppleBCMWLANProximityInterface::getAWDL_PRESENCE_MODE` | 0x1100 | `AppleBCMWLANProximityInterface::setAWDL_PRESENCE_MODE` | `apple80211_awdl_presence_mode` | ==8 / ==8 | getAwdlPresenceMode / setAwdlPresenceMode |
| 143 | `APPLE80211_IOC_PEER_CACHE_CONTROL` | — | `—` | 0x1108 | `AppleBCMWLANProximityInterface::setPEER_CACHE_CONTROL` | `apple80211_peer_cache_control` | — / — | — / — |
| 149 | `APPLE80211_IOC_AWDL_OOB_REQUEST` | — | `—` | 0x1110 | `AppleBCMWLANProximityInterface::setAWDL_OOB_REQUEST` | `apple80211_awdl_oob_request` | — / — | — / — |
| 151 | `APPLE80211_IOC_AWDL_SYNC_FRAME_TEMPLATE` | — | `—` | 0x1118 | `AppleBCMWLANProximityInterface::setAWDL_SYNC_FRAME_TEMPLATE` | `apple80211_awdl_sync_frame_template` | — / — | — / — |
| 155 | `APPLE80211_IOC_AWDL_ELECTION_PARAMS` | 0x1040 | `AppleBCMWLANProximityInterface::getAWDL_ELECTION_PARAMS` | 0x1120 | `AppleBCMWLANProximityInterface::setAWDL_ELECTION_PARAMS` | `apple80211_awdl_election_params` | ==56 / ==56 | getAwdlElectionParams / setAwdlElectionParams |
| 158 | `APPLE80211_IOC_AWDL_DEVICE_CAPABILITIES` | 0x1048 | `AppleBCMWLANProximityInterface::getAWDL_DEVICE_CAPABILITIES` | — | `—` | `apple80211_awdl_device_capabilities` | — / — | — / — |
| 164 | `APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION` | — | `—` | 0x1128 | `AppleBCMWLANProximityInterface::setAWDL_PEER_TRAFFIC_REGISTRATION` | `apple80211_awdl_peer_traffic_registration` | — / ==184 | — / setAwdlPeerTrafficRegistration |
| 205 | `APPLE80211_IOC_AWDL_SECONDARY_MASTER_CHANNEL` | 0x1050 | `AppleBCMWLANProximityInterface::getAWDL_SECONDARY_MASTER_CHANNEL` | 0x1130 | `AppleBCMWLANProximityInterface::setAWDL_SECONDARY_MASTER_CHANNEL` | `apple80211_awdl_secondary_master_channel` | ==8 / ==8 | getAwdlSecondaryMasterChannel / setAwdlSecondaryMasterChannel |
| 208 | `APPLE80211_IOC_AWDL_AF_TX_MODE` | 0x1058 | `AppleBCMWLANProximityInterface::getAWDL_AF_TX_MODE` | 0x1138 | `AppleBCMWLANProximityInterface::setAWDL_AF_TX_MODE` | `apple80211_awdl_af_tx_mode` | ==8 / ==8 | getAwdlAfTxMode / setAwdlAfTxMode |
| 213 | `APPLE80211_IOC_AWDL_MIN_RATE` | 0x1060 | `AppleBCMWLANProximityInterface::getAWDL_MIN_RATE` | 0x1140 | `AppleBCMWLANProximityInterface::setAWDL_MIN_RATE` | `apple80211_awdl_min_rate` | ==8 / ==8 | — / setAwdlMinRate |
| 219 | `APPLE80211_IOC_AWDL_ENCRYPTION_KEYS` | — | `—` | 0x1148 | `AppleBCMWLANProximityInterface::setAWDL_ENCRYPTION_KEYS` | `apple80211_key` | — / ==148 | — / setAwdlKey |
| 220 | `APPLE80211_IOC_AWDL_ENCRYPTION_TYPE` | 0x1068 | `AppleBCMWLANProximityInterface::getAWDL_ENCRYPTION_TYPE` | 0x1150 | `AppleBCMWLANProximityInterface::setAWDL_ENCRYPTION_TYPE` | `apple80211_encrypt` | ==8 / ==8 | getAwdlEncryptType / setAwdlEncryptType |
| 225 | `APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST` | — | `—` | 0x1158 | `AppleBCMWLANProximityInterface::setAWDL_OOB_AUTO_REQUEST` | `apple80211_awdl_oob_request` | — / — | — / — |
| 226 | `APPLE80211_IOC_AWDL_TXCAL_PERIOD` | 0x1070 | `AppleBCMWLANProximityInterface::getAWDL_TXCAL_PERIOD` | 0x1160 | `AppleBCMWLANProximityInterface::setAWDL_TXCAL_PERIOD` | `apple80211_awdl_txcal_period` | — / — | — / — |
| 229 | `APPLE80211_IOC_AWDL_AWDL_ADVERTISERS` | — | `—` | 0x1168 | `AppleBCMWLANProximityInterface::setAWDL_AWDL_ADVERTISERS` | `awdl_peer_advert_add` | — / ==44 | — / setAwdlAdvertisers |
| 247 | `APPLE80211_IOC_AWDL_DEV_STATS` | 0x1078 | `AppleBCMWLANProximityInterface::getAWDL_DEV_STATS` | — | `—` | `apple80211_awdl_dev_stats` | ==124 / — | — / — |
| 262 | `APPLE80211_IOC_AWDL_DFSP_CONFIG` | 0x1080 | `AppleBCMWLANProximityInterface::getAWDL_DFSP_CONFIG` | 0x1170 | `AppleBCMWLANProximityInterface::setAWDL_DFSP_CONFIG` | `apple80211_awdl_dfsp_config` | — / — | — / — |
| 263 | `APPLE80211_IOC_AWDL_DFSP_UCSA_CONFIG` | — | `—` | 0x1178 | `AppleBCMWLANProximityInterface::setAWDL_DFSP_UCSA_CONFIG` | `apple80211_awdl_dfsp_ucsa_config` | — / — | — / — |
| 293 | `APPLE80211_IOC_COMPANION_SKYWALK_IF_CREATE` | — | `—` | 0x1180 | `AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE` | `apple80211_companion_skywalk_if_create` | — / ==24 | — / — |
| 338 | `APPLE80211_IOC_AWDL_CCA` | 0x1088 | `AppleBCMWLANProximityInterface::getAWDL_CCA` | — | `—` | `apple80211_awdl_cca` | ==392 / — | getAwdlCCA / — |
| 341 | `APPLE80211_IOC_AWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` | 0x1090 | `AppleBCMWLANProximityInterface::getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` | — | `—` | `apple80211_awdl_cap` | — / — | — / — |
| 342 | `APPLE80211_IOC_AWDL_OOB_SEC_PAYLOAD_REQUEST` | — | `—` | 0x1188 | `AppleBCMWLANProximityInterface::setAWDL_OOB_SEC_PAYLOAD_REQUEST` | `apple80211_awdl_af_sec_payload_request` | — / — | — / — |
| 344 | `APPLE80211_IOC_LLW_PARAMS` | 0x1098 | `AppleBCMWLANProximityInterface::getLLW_PARAMS` | 0x1190 | `AppleBCMWLANProximityInterface::setLLW_PARAMS` | `apple80211_llw_params` | ==44 / ==44 | getLLWParams / setLLWParams |
| 411 | `APPLE80211_IOC_TX_RATE` | — | `—` | 0x1198 | `AppleBCMWLANProximityInterface::setTX_RATE` | `apple80211_tx_rate` | — / ==16 | — / setAwdlTxRate |
| 414 | `APPLE80211_IOC_P2P_COEX_PROTECTION` | — | `—` | 0x11a0 | `AppleBCMWLANProximityInterface::setP2P_COEX_PROTECTION` | `apple80211_p2p_coex_protection` | — / — | — / — |
| 418 | `APPLE80211_IOC_PSF_DWELL` | — | `—` | 0x11a8 | `AppleBCMWLANProximityInterface::setPSF_DWELL` | `apple80211_awdl_psf_dwell_params` | — / — | — / — |
| 432 | `APPLE80211_IOC_AWDL_CONFIG` | — | `—` | 0x11b0 | `AppleBCMWLANProximityInterface::setAWDL_CONFIG` | `apple80211_awdl_config_request` | — / — | — / — |
| 467 | `APPLE80211_IOC_AWDL_RTG_PEER_STATS_CONFIG` | — | `—` | 0x11b8 | `AppleBCMWLANProximityInterface::setAWDL_RTG_PEER_STATS_CONFIG` | `apple80211_rtg_peer_stats_cfg` | — / — | — / — |

Tipos AWDL que **solo** llegan al `PeerManager` (sin slot de superficie, camino moderno únicamente):

| tipo | IOC | PM GET / PM SET |
|---:|---|---|
| 120 | `APPLE80211_IOC_AWDL_PEER_SERVICE_REQUEST` | — / setAwdlPeerServiceRequest |
| 127 | `APPLE80211_IOC_AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` | — / setAwdlAvailabilityWindowApAlignment |
| 131 | `APPLE80211_IOC_AWDL_OUI` | — / setAwdlOUI |
| 133 | `APPLE80211_IOC_AWDL_TOP_MASTER` | getAwdlTopMaster / — |
| 138 | `APPLE80211_IOC_AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME` | — / setAwdlSyncFramePacketLifetime |
| 148 | `APPLE80211_IOC_AWDL_STRATEGY` | getAwdlStrategy / setAwdlStrategy |
| 163 | `APPLE80211_IOC_AWDL_SOCIAL_TIME_SLOTS` | getAwdlSocialTimeSlots / setAwdlSocialTimeSlots |
| 167 | `APPLE80211_IOC_AWDL_FORCED_ROAM_CONFIG` | — / forceRoamToPeersAP |
| 201 | `APPLE80211_IOC_AWDL_BTLE_PEER_INDICATION` | — / setAwdlBtlePeerIndication |
| 202 | `APPLE80211_IOC_AWDL_BTLE_STATE_PARAMS` | getAwdlBtleStateParams / — |
| 203 | `APPLE80211_IOC_AWDL_PEER_DATABASE` | getAwdlPeerDatabase / — |
| 204 | `APPLE80211_IOC_AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS` | — / setAwdlBtleEnableSynchWithParams |
| 217 | `APPLE80211_IOC_AWDL_OPER_MODE` | — / setAwdlOperatingMode |
| 218 | `APPLE80211_IOC_RESTORE_DEFAULTS` | getAwdlRestoreDefaults / — |
| 223 | `APPLE80211_IOC_AWDL_STATISTICS` | getAwdlStatistics / — |
| 233 | `APPLE80211_IOC_AWDL_BLOCK_SET_COMMANDS` | getAwdlBlockSetCommands / setAwdlBlockSetCommands |
| 244 | `APPLE80211_IOC_AWDL_PREFERRED_CHANNELS` | getAwdlPreferredChannels / — |
| 253 | `APPLE80211_IOC_AWDL_NEARBY_LOG_TRIGGER` | — / setAwdlNearbyLogTrigger |
| 340 | `APPLE80211_IOC_TRAFFIC_REGISTRATION` | — / setTrafficRegistration |
| 343 | `APPLE80211_IOC_AWDL_LOW_LATENCY_STATISTICS` | getAwdlLowLatencyStatistics / — |
| 351 | `APPLE80211_IOC_AWDL_SIDECAR_DIAGNOSTICS` | getAwdlSidecarDiagnostics / — |
| 370 | `APPLE80211_IOC_AWDL_AIRPLAY_STATISTICS` | getAwdlAirplayStatistics / — |
| 415 | `APPLE80211_IOC_POST_PRESENCE_DONE` | — / setAwdlPostPeerPresenceDone |
| 416 | `APPLE80211_IOC_AWDL_MI_BITMAP` | — / setAwdlMiBitmap |
| 417 | `APPLE80211_IOC_AWDL_UMI_TX_PEER` | — / setAWDLUMIToPeer |
| 419 | `APPLE80211_IOC_LTE_RESTRICTED_CHANNELS` | — / setLteRestrictedChannels |
| 420 | `APPLE80211_IOC_NANPH_SOFTAP_CSA` | — / setSoftApCSANanPh |

## 6. Tablas legacy (ruta ioctl BSD) y ruta interna

Las tablas legacy (581 GET / 457 SET entradas válidas) repiten el mismo patrón con handlers de
firma más antigua (`(IO80211Controller*, IO80211SkywalkInterface*, apple80211req, bool)`, sin
user client). No contienen ningún tipo AWDL específico (intersección vacía con los tipos
`APPLE80211_IOC_AWDL_*`); los comandos generales (1 SSID, 4 CHANNEL, 9 BSSID, 13 STATE, …) usan
los mismos puentes con cascada, así que sobre awdl0 sí llegan a los slots +0xFC8..+0xFE0.

| tabla | entradas con handler | observación |
|---|---:|---|
| `gGetHandlerTableLegacy` | 99 | tipos 1..581; sin AWDL específicos |
| `gSetHandlerTableLegacy` | 80 | incluye el tipo 94 `setVirtualInterfaceCreate` |
| `gInternalGetHandlerTable` | 245 | filas = puentes `apple80211getXXX` directos |
| `gInternalSetHandlerTable` | 333 | filas = puentes `apple80211setXXX` directos |

El handler legacy del tipo 94 es `setVirtualInterfaceCreate` (mismo comportamiento especial:
validación 32 bytes y ruta al override del controlador; detalle `?`, no decompilado aquí).

La ruta interna (glue) es la que ejercita la superficie AWDL completa: sus tablas apuntan
directamente a los 56 puentes que castean a `IO80211AWDLProtocol`; todos ellos tienen fila en
`gInternalGet/SetHandlerTable` (0 puentes AWDL sin fila).

## 7. Superficie AWDL completa y slots sin request

La vtable de `IO80211AWDLProtocol` añade **64 slots** (vptr **+0xFC8..+0x11C0**,
(0x11C0-0xFC8)/8 + 1 = 64; 505 slots de `IO80211VirtualInterface` + 64 = 569, el tamaño del
símbolo `__ZTV19IO80211AWDLProtocol`), todos con el mismo stub
`0xffffff8000bb59c0` en la clase base (lectura directa de `__ZTV19IO80211AWDLProtocol`; nota: el
recuento de 63 de [`AWDL_PROXIMITY_VTABLE_HIERARCHY.md`](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) §3
contaba desde +0xFD0 y dejaba fuera el primer slot). Broadcom los sobrescribe todos en
`AppleBCMWLANProximityInterface` con métodos propios (lectura de
`__ZTV30AppleBCMWLANProximityInterface`, sin ningún stub residual). Con el mapeo del §5, de los
64 slots de la superficie solo queda uno sin request asociada,
+0x11C0 (`getTX_RATE`); el slot +0x11C8 ya no es superficie AWDL sino extensión de la
subclase Broadcom (su init especializada, ver
[`AWDL_PROXIMITY_VTABLE_HIERARCHY.md`](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) §3):

| slot | método Broadcom | nota |
|---|---|---|
| +0x11c0 | `AppleBCMWLANProximityInterface::getTX_RATE` | el par GET de `TX_RATE` (SET = 411) no existe en ninguna tabla; `?` quién lo llama |
| +0x11c8 | `AppleBCMWLANProximityInterface::init` | no es un comando: es la init especializada de la subclase (ver [`AWDL_PROXIMITY_VTABLE_HIERARCHY.md`](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) §3) |

## 8. Consecuencias para RTL8814AirLayer

1. La creación (tipo 94) llega por el slot **+0x1160 del controlador/interfaz principal** (cast a
   `IO80211InfraProtocol`), no por la superficie AWDL: implementar el override `setVIRTUAL_IF_CREATE`
   (slot 556 en el ABI del proyecto, `Source/IO80211/IO80211_25_6_0.h`) sigue siendo la puerta correcta, con
   `len == 32` y el rol en `data+0x0C` (ya verificado en hardware).
2. Para contestar los comandos AWDL con la forma del despacho Broadcom, una interfaz role 6 propia
   que herede de `IO80211AWDLProtocol` recibiría, por los casts de los puentes, las llamadas de los
   43 tipos del §5 en los slots listados; la tabla da el contrato (slot, struct, longitud).
3. El camino moderno de los tipos AWDL lo resuelve la familia con `IO80211AWDLPeerManager` (que es
   un objeto del propio `IO80211Family`); cómo se vincula ese objeto a una interfaz de un driver
   externo (ivar +0x120/+0x28) no está verificado aquí (`?`).
4. Las requests sin handler en la tabla activa devuelven 102 sin llegar al driver: no basta con
   implementar slots, hay que estar en la tabla correcta para la ruta del cliente (los clientes
   Apple80211 de usuario usan el user client moderno; los ioctls BSD caen en las legacy, que no
   tienen tipos AWDL).

## 9. Método y límites

* Descompilación headless (Hex-Rays) del despachador, 1.377 funciones objetivo de las seis tablas y
  1.156 puentes; desensamblado de puentes para los offsets de vtable (fuente autoritativa: los
  offsets salen de `mov rax,[rax+0xNNN]`/`call [rax+r15]` reales, no de la numeración de Hex-Rays).
* Conteo de tablas y contadores legacy (`0x245`/`0x1C9`) leídos del `.kc` directamente.
* Vtables: lectura del archivo con los fixups revalidados (sanity: entrada del rol 6 → `AirLink`;
  par D1/D0 de la vtable Broadcom en `0xffffff80017f5b78`). Slot→método Broadcom resuelto contra
  `syms_bcm.txt`.
* `?` explícitos: la variante exacta del user client que invoca `performGatedCommandIOUC`; el
  handler legacy del tipo 94 no decompilado; el llamador de `getTX_RATE` (+0x11c0); el vínculo
  `IO80211AWDLPeerManager`↔interfaz de driver externo; los tamaños exactos de los structs solo
  alcanzables por la ruta interna (sin handler moderno que valide longitud).
* Los 56 puentes con cast a `IO80211AWDLProtocol` y sus slots por clase están en
  `Build/ida_awdl/task_dispatch/` (`dispatch2.json`, `final_awdl_surface.txt`, `s4_bridge_disasm.txt`).
