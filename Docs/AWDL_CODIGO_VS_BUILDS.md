# Compatibilidad del código AWDL de RTL8814Native frente a compilaciones de macOS

Evaluación del código de la capa AWDL (`Source/AirLayer/AWDLInterface.cpp` y `Source/AirLayer/AWDLInterface.h`), validado originalmente sobre macOS 26.7.1 (build 25G241), frente al Kernel Collection de macOS 26.7.2 (build 25G332) y distancia respecto a las versiones anteriores macOS 14.8.9 (build 23J631, Sonoma) y macOS 15.8.2 (build 24H232, Sequoia), todas para arquitectura x86_64.

---

## 1. Resumen y veredicto por build

| Compilación | Versión SO | Veredicto | Resumen |
|---|---|---|---|
| **25G241** | macOS 26.7.1 | Línea base | Código original validado. 131/131 símbolos OK, 157/157 ranuras de vtable OK, 61/61 desplazamientos OK. |
| **25G332** | macOS 26.7.2 | **Funciona tal cual** | 131/131 símbolos OK (0 faltantes). 157/157 ranuras de vtable OK (0 desajustes). 48/61 desplazamientos OK y 0 movidos; las 13 filas de funciones sin símbolo del kernel, por búsqueda del patrón en todo el kernel (12 con el mismo número de coincidencias que en 25G241, 1 genérica `?`). Tokens 129/132/205, payloads de eventos 41/43, puerta de doble banda y offsets de presencia/PPP 100 % idénticos. |
| **24H232** | macOS 15.8.2 | **Requiere binario y cabecera separados** | 129/131 símbolos OK (faltan `flushFlowQueues` y `signalMulticastPackets`). Desplazamiento acumulado de -0x20 en vtable (`sendPendingPacketsToStack` a `0xF10` vs `0xF30`; `inputPacket` a `0xB48` vs `0xB50`; `setMacAddress` pura a `0xCF8` vs `0xF40`). Bloque de capacidades a `ctrl+0x120 + 0xB20` (delta -0x10). Offsets de presencia desplazados. |
| **23J631** | macOS 14.8.9 | **Requiere binario y cabecera separados** | 121/131 símbolos OK (10 ausentes). Desplazamiento masivo de vtable (-0x78 en `IO80211VirtualInterface`, -0xA0 en métodos AWDL: `getSSID` a `0xF28` vs `0xFC8`). Firma de `inputPacket` con 4 argumentos (sin booleano final). Tamaño de objeto de interfaz `0x120` (en vez de `0x130`) `?`. Sin buffer centralizado de capacidades en el controlador (AWDL lee en `sub+0xB18`). Offsets de presencia desplazados. |

---

## 2. Tablas comparativas por herramienta

### 2.1 Símbolos importados y requeridos (`Tools/awdl_recheck_symbols.py`)

Cotejo de los 131 requerimientos extraídos de las fuentes (`AWDLInterface.cpp`, `AWDLInterface.h`, `RTLAirLayer.cpp`): 37 etiquetas de ensamblador directo (`__asm`) y 94 métodos virtuales de `IO80211VirtualInterface` resueltos en tiempo de carga (`setMacAddress` pura se omite).

| Requerimiento / Símbolo | 25G241 | 25G332 | 23J631 | 24H232 | Nota |
|---|---|---|---|---|---|
| **Requerimientos totales analizados** | 131 | 131 | 131 | 131 | Requerimientos de la capa AirLayer |
| **Símbolos definidos (OK)** | 131 | 131 | 121 | 129 | Estado de enlace |
| **Símbolos ausentes (MISSING)** | 0 | 0 | 10 | 2 | Métodos inexistentes en la versión |
| `IO80211VirtualInterface::flushFlowQueues` | OK | OK | MISSING | MISSING | Ranura `0xED8` en 26; purga de colas por MAC de par no existe en 14/15 |
| `IO80211VirtualInterface::signalMulticastPackets` | OK | OK | MISSING | MISSING | Ranura `0xF28` en 26; señalización multicast no existe en 14/15 |
| `IO80211VirtualInterface::getPeerDataStats` | OK | OK | MISSING | OK | Ranura `0xEA8` en 26 (`0xEA0` en 15); introducida en macOS 15 |
| `IO80211VirtualInterface::clearPeerDataStats` | OK | OK | MISSING | OK | Ranura `0xEB0` en 26 (`0xEA8` en 15); introducida en macOS 15 |
| `IO80211VirtualInterface::getPeerSlotDataStats` | OK | OK | MISSING | OK | Ranura `0xEA0` en 26 (`0xE98` en 15); introducida en macOS 15 |
| `IO80211VirtualInterface::setAwdlCurrentChannelSequenceIndex` | OK | OK | MISSING | OK | Ranura `0xEB8` en 26 (`0xEB0` en 15); introducida en macOS 15 |
| `IO80211VirtualInterface::findPeer` | OK | OK | MISSING | OK | Ranura `0xCD0` en 26 (`0xCC8` en 15); no existe como virtual en 14 |
| `IO80211Controller::postMessage` (firma AirLayer) | OK | OK | MISSING | OK | Firma no exportada en 14; en AWDL se usa la de la vif (`0xB18`) |
| `IO80211FaultReporter::allocWithParams` | OK | OK | MISSING | OK | Constructor ausente en 14; en 14 existe método directo en slot 436 |
| `IO80211InfraInterface::registerInfraEthernetInterface` | OK | OK | MISSING | OK | Alta de interfaz ethernet en 14 usa `registerEthernetInterface` |
| Símbolos directos `extern "C"` de `AWDLInterface.cpp` (27) | OK | OK | OK | OK | Todos los enlaces directos (Skywalk, colas, bpf) existen en los cuatro builds |

---

### 2.2 Ranuras de vtable (`Tools/awdl_recheck_vtable.py`)

Cotejo de ranuras declaradas en `Source/AirLayer/AWDLInterface.h` contra los volcados de vtable (`vt_AppleBCMWLANProximityInterface.txt` y `vt_IO80211VirtualInterface.txt`). En `IO80211AWDLProtocol` las 63 celdas AWDL son puras (`___cxa_pure_virtual`); `AppleBCMWLANProximityInterface` implementa todas las celdas concretas.

| Ranura declarada en cabecera | Método | 25G241 | 25G332 | 23J631 | 24H232 | Nota sobre la deriva |
|---|---|---|---|---|---|---|
| `0x428` | `configureReport` | 0x428 OK | 0x428 OK | 0x428 OK | 0x428 OK | Idéntico en los 4 builds |
| `0x5C0` | `start` | 0x5C0 OK | 0x5C0 OK | 0x5C0 OK | 0x5C0 OK | Idéntico en los 4 builds |
| `0x600` | `terminate` | 0x600 OK | 0x600 OK | 0x600 OK | 0x600 OK | Idéntico en los 4 builds |
| `0x8E8` | `prepareBSDInterface` | 0x8E8 OK | 0x8E8 OK | 0x8D8 | 0x8E8 OK | En 14 desplazado por menor número de métodos previos |
| `0x910` | `processBSDCommand` | 0x910 OK | 0x910 OK | 0x900 | 0x910 OK | Idem |
| `0x930` | `setInterfaceEnable` | 0x930 OK | 0x930 OK | 0x920 | 0x928 | En 25 se insertó `setRxFlowSteering` en `0x928` |
| `0x9B8` | `getFeatureFlags` | 0x9B8 OK | 0x9B8 OK | 0x9A0 | 0x9B0 | Delta -0x8 en 15 |
| `0xB08` | `createPeerManager` | 0xB08 OK | 0xB08 OK | 0xAF0 | 0xB00 | Delta -0x8 en 15 |
| `0xB18` | `postMessage` | 0xB18 OK | 0xB18 OK | 0xB00 | 0xB10 | Delta -0x8 en 15 |
| `0xB48` / `0xB50` | `inputPacket` | **0xB50** OK | **0xB50** OK | **0xB48** | **0xB48** | En 14 y 15 está en `0xB48`. En 14 la firma tiene 4 args (sin bool) |
| `0xB88` | `linkState` | 0xB88 OK | 0xB88 OK | 0xB88 OK | 0xB88 OK | Eliminación de `setLinkState` en 25 neutralizó el delta |
| `0xE60` / `0xE68` | `init` | **0xE68** OK | **0xE68** OK | **0xDF0** | **0xE60** | Inicio de métodos propios de la interfaz virtual |
| `0xCF8` / `0xF40` | `setMacAddress` | **0xF40** pura | **0xF40** pura | no pura | **0xCF8** pura | En 15 es virtual pura en `0xCF8`; en 26 se trasladó a `0xF40` |
| `0xF10` / `0xF30` | `sendPendingPacketsToStack` | **0xF30** OK | **0xF30** OK | **0xE80** | **0xF10** | En 15 delta -0x20; en 14 delta -0xB0 |
| `0xF18` / `0xF38` | `freePendingPackets` | **0xF38** OK | **0xF38** OK | **0xE88** | **0xF18** | En 15 delta -0x20; en 14 delta -0xB0 |
| `0xF28` / `0xFC8` | `getSSID` (inicio AWDL) | **0xFC8** OK | **0xFC8** OK | **0xF28** | **0xFC8** OK | Inicio de ranuras AWDL; en 14 arranca en `0xF28` (-0xA0) |
| `0x10B0` | `setAWDL_SYNC_PARAMS` | 0x10B0 OK | 0x10B0 OK | 0x1010 | 0x10B0 OK | Ranura AWDL ioctl 116; en 14 delta -0xA0 |
| `0x10E0` | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 0x10E0 OK | 0x10E0 OK | 0x1040 | 0x10E0 OK | Ranura AWDL ioctl 129; en 14 delta -0xA0 |
| `0x10E8` | `setAWDL_MASTER_CHANNEL` | 0x10E8 OK | 0x10E8 OK | 0x1048 | 0x10E8 OK | Ranura AWDL ioctl 132; en 14 delta -0xA0 |
| `0x1130` | `setAWDL_SECONDARY_MASTER_CHANNEL` | 0x1130 OK | 0x1130 OK | 0x1090 | 0x1130 OK | Ranura AWDL ioctl 205; en 14 delta -0xA0 |
| `0x11B8` | `setAWDL_RTG_PEER_STATS_CONFIG` | 0x11B8 OK | 0x11B8 OK | ausente | 0x11B8 OK | Última celda AWDL en 15 y 26; no existe en 14 |
| **Resumen ProxInterface** | 157 ranuras de cabecera | **157 OK** | **157 OK** | 22 OK, 117 desvío | 23 OK, 132 desvío | 25G332 coincide al 100 %; 14 y 15 exigen remapeo |

---

### 2.3 Desplazamientos sensibles de versión (`Tools/awdl_recheck_offsets.py`)

Cotejo de las 61 filas de patrones e instrucciones de `Tools/awdl_offsets_25G241.tsv` en los cuatro kernel collections. En 25G332 la herramienta da 48 OK, 0 movidas y 13 omitidas: las 13 son funciones sin símbolo de `com.apple.kernel`, que la herramienta sólo resuelve con `--va-hint` sobre el mismo build. Buscado cada uno de esos patrones en todo el código del kernel, 12 aparecen el mismo número de veces en 25G332 que en 25G241, sólo desplazados (p. ej. `IFEF_TXSTART` 5 y 5, `+0x330` 2 y 2, `nll_state = 2` 7 y 7); el de `ifclassq+0x68` es demasiado genérico para concluir (1262 frente a 1255) `?`. Las columnas de 14 y 15 salen de `Tools/awdl_offsets_23J631.tsv` y `awdl_offsets_24H232.tsv`.

| Grupo / Estructura | Campo | 25G241 | 25G332 | 23J631 | 24H232 | Nota sobre la deriva |
|---|---|---|---|---|---|---|
| `reginfo/RegistrationInfo` | `+0x04` tamaño >= 0x108 | `0x4` | `0x4` | `0x4` | `0x4` | Idéntico |
| `reginfo/RegistrationInfo` | `+0x3C` headroom (`getTxDataOffset`) | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Idéntico |
| `reginfo/RegistrationInfo` | `+0x3E` lectura start | `0x3e` | `0x3e` | `0x3e` | `0x3e` | Idéntico |
| `reginfo/RegistrationInfo` | `+0x48/+0x4C` par MTU | `0x48` | `0x48` | `0x48` | `0x48` | Idéntico |
| `reginfo/RegistrationInfo` | `+0x108` MAC | `0x108` | `0x108` | `0x108` | `0x108` | Idéntico |
| `eparams/ifnet_init_eparams` | `+0x00` version == 2 | `0x0` | `0x0` | `0x0` | `0x0` | Idéntico |
| `eparams/ifnet_init_eparams` | `+0x04` tamaño == 0x158 | `0x158` | `0x158` | `0x158` | `0x158` | Idéntico |
| `eparams/ifnet_init_eparams` | `+0x144` tx data offset | `0x144` | `0x144` | `0x144` | `0x144` | Idéntico en estructura |
| `eparams/ifnet_init_eparams` | `+0x146` lectura alloc | `0x146` | `0x146` | `0x146` | `0x146` | Idéntico |
| `ifnet/ifnet` | `+0x78` `if_refflags` (& 7 == 2) | `0x78` | `0x78` | `0x78` | `0x78` | Idéntico |
| `ifnet/ifnet` | `+0xBE` `if_flags` (gate `IFF_UP`) | `0xbe` | `0xbe` | `0xbe` | `0xbe` | Idéntico |
| `ifnet/ifnet` | `+0xC0` `IFEF_TXSTART` (gate enqueue) | `0xc0` | `0xc0` | `0xc0` | `0xc0` | Idéntico |
| `ifnet/ifnet` | `+0xC0` `IFEF_TXSTART` (gate hilo) | `0xc0` | `0xc0` | `0xc0` | `0xc0` | Idéntico |
| `ifnet/ifnet` | `+0xC0` `IFEF_TXSTART` (escritura) | `0xc0` | `0xc0` | `0xc0` | `0xc0` | Idéntico |
| `ifnet/ifnet` | `+0xC3` `IFEF_SKYWALK_NATIVE` | `0xc3` | `0xc3` | `0xc3` | `0xc3` | Idéntico |
| `ifnet/ifnet` | `+0x248` `if_output` (`_dlil_output_handler`) | `0x248` | `0x248` | **`0x1c0`** | **`0x228`** | Delta -0x88 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x250` `if_start` (`_ifp_if_start`) | `0x250` | `0x250` | **`0x1c8`** | **`0x230`** | Delta -0x88 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x258` `output_handler` (salto pila) | `0x258` | `0x258` | **`0x1d0`** | **`0x238`** | Delta -0x88 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x258` `output_handler` (CAS) | `0x258` | `0x258` | **`0x1d0`** | **`0x238`** | Delta -0x88 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x260` output alta (escritura) | `0x260` | `0x260` | **`0x1d8`** | **`0x240`** | Delta -0x88 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x260` output alta (lectura) | `0x260` | `0x260` | **`0x1d8`** | **`0x240`** | Delta -0x88 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x598` `if_start` por defecto | `0x598` | `0x598` | `0x598` | **`0x578`** | Delta -0x20 en 15 |
| `ifnet/ifnet` | `+0x5A0` handler por defecto (escritura) | `0x5a0` | `0x5a0` | **`0x510`** | **`0x580`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x5A0` handler por defecto (ancla) | `0x5a0` | `0x5a0` | **`0x510`** | **`0x580`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x300` bit 1 (hilo en marcha) | `0x300` | `0x300` | **`0x270`** | **`0x2e0`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x304` contador de arranque | `0x304` | `0x304` | **`0x274`** | **`0x2e4`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x309` byte de puerta | `0x309` | `0x309` | **`0x279`** | **`0x2e9`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x330` hilo de arranque (pánico) | `0x330` | `0x330` | **`0x2a0`** | **`0x310`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x330` hilo de arranque (`assert_wait`) | `0x330` | `0x330` | **`0x2a0`** | **`0x310`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x338` `ifclassq` (hilo arranque) | `0x338` | `0x338` | **`0x2a8`** | **`0x318`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x338` `ifclassq` (alta) | `0x338` | `0x338` | **`0x2a8`** | **`0x318`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x58C` tx data offset (escritura) | `0x58c` | `0x58c` | **`0x4fc`** | **`0x56c`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x58E` hermano (escritura) | `0x58e` | `0x58e` | **`0x4fe`** | **`0x56e`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x590` u32 (escritura) | `0x590` | `0x590` | **`0x500`** | **`0x570`** | Delta -0x90 en 14; -0x20 en 15 |
| `ifnet/ifnet` | `+0x900` cola netem empotrada | `0x900` | `0x900` | `0x900` | `0x900` | Idéntico |
| `ifclassq/ifclassq` | `+0x4C` bit 1 de purga | `0x4c` | `0x4c` | `0x4c` | `0x4c` | Idéntico |
| `ifclassq/ifclassq` | `+0x68` tabla de la disciplina | `0x68` | `0x68` | `0x68` | `0x68` | Idéntico |
| `ifclassq/disciplina` | tabla+`0x18` operación enqueue | `0x18` | `0x18` | `0x18` | `0x18` | Idéntico |
| `ifclassq/ifclassq` | `+0x5C` límite 0x800 | `0x5c` | `0x5c` | `0x5c` | `0x5c` | Idéntico |
| `llink/netif_llink` | `+0x3C` `nll_state = 1` (alta) | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Idéntico |
| `llink/netif_llink` | `+0x3C` `nll_state = 2` (destrucción) | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Idéntico |
| `llink/netif_llink` | `+0x3C == 2` puerta ENXIO | `0x3c` | `0x3c` | `0x3c` | `0x3c` | Idéntico |
| `vtable/IOSkywalkNetworkInterface` | `+0x9C0` `getTxDataOffset` | `0x9c0` | `0x9c0` | **`0x9a8`** | **`0x9b8`** | Delta -0x18 en 14; -0x8 en 15 |
| `vtable/IOSkywalkEthernetInterface` | `+0x8E0` `initBSDInterfaceParameters` | `0x8e0` | `0x8e0` | **`0x8d0`** | `0x8e0` | Delta -0x10 en 14 |
| `vtable/IOSkywalkNetworkInterface` | `+0x8E8` `prepareBSDInterface` | `0x8e8` | `0x8e8` | **`0x8d8`** | `0x8e8` | Delta -0x10 en 14 |
| `vtable/IOService` | `+0x600` `terminate` | `0x600` | `0x600` | `0x600` | `0x600` | Idéntico |
| `vtable/IOService` | `+0x5B0` `registerService` | `0x5b0` | `0x5b0` | `0x5b0` | `0x5b0` | Idéntico |
| `vtable/IOSkywalkBSDClient` | `+0x5D0` `start` | `0x5d0` | `0x5d0` | `0x5d0` | `0x5d0` | Idéntico |
| `vtable/IOSkywalkInterface` | `+0x5D8` `stop` | `0x5d8` | `0x5d8` | `0x5d8` | `0x5d8` | Idéntico |
| `vtable/OSOrderedSet` | `+0x1D8` `lastObject` | `0x1d8` | `0x1d8` | **`0x1e0`** | `0x1d8` | `getLastObject` en 14 |
| `vtable/OSOrderedSet` | `+0x1F8` `getObject` | `0x1f8` | `0x1f8` | `0x1f8` | `0x1f8` | Idéntico |
| `vtable/IO80211VirtualInterface` | `+0xE68` `init` (super) | `0xe68` | `0xe68` | **`0xdf0`** | **`0xe60`** | Inicio de métodos de interfaz virtual |
| `vtable/AppleBCMWLANSkywalkInterface` | `+0x14E0` `start` | `0x14e0` | `0x14e0` | `0x14e0` | **`0x1478`** | Delta de slots BCM en 15 |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0xE8` interfaz | `0xe8` | `0xe8` | `0xe8` | `0xe8` | Idéntico |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0xF0` copia RegInfo | `0xf0` | `0xf0` | `0xf0` | `0xf0` | Idéntico |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0xF8` puntero `ifnet` | `0xf8` | `0xf8` | `0xf8` | `0xf8` | Idéntico |
| `bsdclient/IOSkywalkNetworkBSDClient` | `+0x13C` estado power/wake | `0x13c` | `0x13c` | **`0x120`** | **`0x120`** | Delta -0x1C en 14 y 15 |
| **Resumen de coincidencias con 25G241** | 61 accesos | **61 OK** | **48 OK + 12 por patrón + 1 `?`** | 20 OK, 27 mov/nf | 28 OK, 20 mov/nf | 25G332 no presenta deriva alguna |

---

## 3. Comprobaciones manuales en 25G332 y cotejo entre versiones

### 3.1 Base del bloque de capacidades de tarjeta (`CARD_CAPABILITIES`)

En macOS 26 y 15 el controlador almacena la respuesta de 28 bytes del ioctl 12 (`CARD_CAPABILITIES`) en un bloque indexado; en macOS 14 el controlador no almacena este bloque y cada cliente solicita o cachea sus propios campos.

- **macOS 26.7.2 (25G332):**
  - Función: `IO80211Controller::loadCardCapabilities` (`0xffffff800229f4f6`).
  - Escritura: almacena en `*(this+0x120) + 0xB30` (versión u32) y `+0xB34..+0xB4B` (24 bytes de capacidades `p[4..27]`).
  - Cabezas de instrucción:
    - `0xffffff800229f52a`: `mov ecx, 0B30h`
    - `0xffffff800229f52f`: `add rcx, [r14+120h]`
    - `0xffffff800229f53e`: `mov r8d, 1Ch`
    - `0xffffff800229f563`: `mov esi, 0B30h`
    - `0xffffff800229f568`: `add rsi, [r14+120h]`
  - Comportamiento idéntico al de 25G241 (`0xffffff800229a202`).
- **macOS 15.8.2 (24H232):**
  - Función: `IO80211Controller::loadCardCapabilities` (`0xffffff800222cdf0`).
  - Escritura: almacena en `*(this+0x120) + 0xB20` (delta constante de -0x10 respecto a macOS 26).
- **macOS 14.8.9 (23J631):**
  - No existe `loadCardCapabilities` en `IO80211Controller`.
  - En su lugar, `IO80211AWDLPeerManager::configure` (`0xffffff80020b55e6`) emite directamente el ioctl 12 (`0xC02869C9`) y almacena los 28 bytes en su propia subestructura interna en `sub + 0xB18` (`sub = *(this+0x18)`), evaluando `sub+0xB1C` (`p[4]`). `IO80211InfraInterface::initCapabilities` (`0xffffff8002204bec`) almacena 16 bytes en sus ivars locales `+28..43`.

---

### 3.2 Puerta de doble banda (`p[7]` bit `0x20` / capacidad 29)

El bit `0x20` del byte de capacidad correspondiente (`p[7]` en el buffer del driver; byte 3 tras decapar la cabecera de versión de 4 bytes) es imprescindible para que el gestor AWDL admita canales sociales en la banda de 5 GHz (149 y 44).

| Compilación | Función lectora | VA cabeza de instrucción | Instrucción verificada | Efecto si el bit está en 0 |
|---|---|---|---|---|
| **25G241** | `configureValidAWDLChannels` | `0xffffff80021cab1e` | `test byte ptr [rax+0B37h], 20h` | Anula canales 5 GHz en `+0x4294`; log *"Dual band not supported..."* |
| **25G332** | `configureValidAWDLChannels` | `0xffffff80021cfd06` | `test byte ptr [rax+0B37h], 20h` | Idéntico a 25G241 |
| **25G332** | `handleAssistedDiscoveryTrafficReg` | `0xffffff800220b0f4` | `test byte ptr [rax+0B37h], 20h` | Deniega social de 5 GHz en descubrimiento asistido |
| **25G332** | `fillLegacyDataPathPayload` | `0xffffff80022195e3` | `test byte ptr [rsi+0B37h], 20h` | Flags del TLV 12: `0x8B03` en vez de `0x8B23` |
| **24H232** | `configureValidAWDLChannels` | `0xffffff8002169030` | `test byte ptr [rax+0B27h], 20h` | Idéntico efecto en `+0xB27` (delta -0x10) |
| **23J631** | `configureValidAWDLChannels` | `0xffffff80020bb433` | `test byte ptr [rax+0B1Fh], 20h` | Idéntico efecto en `sub+0xB1F` (`sub = *(this+0x18)`) |

---

### 3.3 Tokens 129 / 132 / 205 y disposición de la secuencia

- **Token 129 (`APPLE80211_IOC_AWDL_SYNC_CHANNEL_SEQUENCE`):**
  - Valor: `0x81802869C8`.
  - Construido y emitido en `IO80211AWDLPeerManager::setChannelSequenceUnconditional`:
    - En 25G332: `0xffffff80021d12a0` (`mov rax, 81802869C8h`).
    - En 25G241: `0xffffff80021cc0b8`.
    - En 24H232: `0xffffff800216a44a`.
    - En 23J631: `0xffffff80020bc6cc`.
  - Tamaño de carga: 400 bytes (`0x190` en `0xffffff80021d08d4` y comprobación con `memcmp` de `0x190` bytes en `0xffffff80021d0ae9`).
  - Disposición de la cabecera y ranuras (16 entradas de 12 bytes):
    - `+0x00`: versión (1)
    - `+0x04`: flags SDB (`0x10` ON, `0x20` OFF)
    - `+0x05`: número de ranuras (`n = 16`)
    - `+0x06`: codificación (`0` o `1`)
    - `+0x07`: step
    - `+0x08`: dup
    - `+0x09`: fill
    - `+0x0C + 12*i`: entrada de ranura `i`: `+0` versión (u32), `+4` canal (u32), `+8` flags (u32).
    - Copia en bucle verificada en 25G332 en `0xffffff80021d096e` (`mov [r14+rcx+0Ch], rdi`) con avance de 12 bytes (`add rcx, 0Ch` en `0xffffff80021d099b`).
  - La estructura es 100 % coincidente en las 4 versiones.
- **Token 132 (`setAWDL_MASTER_CHANNEL`):**
  - Valor: `0x84802869C8`. Emitido en `programMasterChannels`:
    - En 25G332: `0xffffff80021d9bf9` (`mov rax, 84802869C8h`).
    - En 24H232: `0xffffff80021728c7`.
    - En 23J631: `0xffffff80020c456c`.
  - Carga: `apple80211_awdl_master_channel` (8 bytes).
- **Token 205 (`setAWDL_SECONDARY_MASTER_CHANNEL`):**
  - Valor: `0xCD802869C8`. Emitido en `programMasterChannels`:
    - En 25G332: `0xffffff80021d9c74` (`mov rax, 0CD802869C8h`).
    - En 24H232: `0xffffff8002172942`.
    - En 23J631: `0xffffff80020c4608`.
  - Carga: `apple80211_awdl_secondary_master_channel` (8 bytes).

---

### 3.4 Payloads de eventos 41 y 43

Procesados en `IO80211AWDLPeerManager::reportDataPathEvents`:
- En 25G332: punto de entrada en `0xffffff80021e2adc`.
- En 25G241: punto de entrada en `0xffffff80021dd8f4`.
- En 24H232: punto de entrada en `0xffffff800217adb8`.
- En 23J631: punto de entrada en `0xffffff80020ccaaa`.

#### Evento 41 (`APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`)
- Carga útil: 36 bytes (`0x24`).
- Campos leídos en `reportDataPathEvents` y validados en `monitorTimeline`:
  - `+0x00` (`uint16_t`): número de AW (`AwNumber`).
  - `+0x04` (`uint32_t`): estado de sincronización (`1` maestro, `2` par maestro).
  - `+0x08` (6 bytes): dirección MAC del maestro de sincronización.
  - `+0x0E` (6 bytes): dirección MAC del maestro raíz (*top master*).
  - `+0x14` (`uint32_t`): `FwAwStartTime` en microsegundos, formato **big-endian**.
  - `+0x18` (`uint32_t`): flags del canal de la ranura.
  - `+0x1C` (`uint32_t`): número de canal servido.
  - `+0x20` (`uint32_t`): flags de ventana (0).
- Despacho a `monitorTimeline` en 25G332: cabeza de instrucción en `0xffffff80021e4d25`.
- Coincidencia: 100 % en las 4 versiones.

#### Evento 43 (`APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_END`)
- Carga útil: 40 bytes (`0x28`).
- Acciones en `reportDataPathEvents`:
  - En 25G332:
    - Evalúa disparo de elección: `0xffffff80021e40c8` (`call shouldRunElection`), `0xffffff80021e40d4` (`call runElection`).
    - Cierre de disponibilidad de ventana: `0xffffff80021e40f6` (`mov byte ptr [rdi+4280h], 0`).
    - Lectura de contadores CCA si la bandera en `+0x10` es 1 (`0xffffff80021e411d: cmp word ptr [r14+10h], 1`).
    - Base de tiempo en `+0x18` convertida a escala de 0,1 µs (`0xffffff80021e412f: lea r9d, [rax+rax*4]`).
  - Una carga llena de ceros (como envía `AWDLInterface.cpp`) cierra la ventana y activa `runElection` sin procesar contadores CCA en las 4 versiones.

---

### 3.5 Quince ítems de presencia y PPP

Cotejo exhaustivo de las ranuras, ivars y umbrales definidos en `Tools/awdl_presence_offsets.tsv`. Todas las direcciones corresponden a cabezas de instrucción comprobadas en desensamblado.

| Ítem | Tipo | 25G241 (valor / VA) | 25G332 (valor / VA) | 24H232 (valor / VA) | 23J631 (valor / VA) | Nota |
|---|---|---|---|---|---|---|
| `pm_llavalid` | ivar_offset | `0xFF1C` (`0x21d9b39`) | **`0xFF1C`** (`0xffffff80021ded21`) | `0xC70C` (`0xffffff8002177870`) | `0xC4A0` (`0xffffff80020c93db`) | Base ivars AWDL = `this+0x20` |
| `pm_bonjourTriggersAWDL` | ivar_offset | `0xA718` (`0x21d9b0a`) | **`0xA718`** (`0xffffff80021decf2`) | `0x7FF8` (`0xffffff8002177841`) | `0x7FA8` (`0xffffff80020c93b1`) | Guarda del bloque LLA |
| `pm_stateflags` | ivar_offset | `0x3EE0` (`0x21a9339`) | **`0x3EE0`** (`0xffffff80021ae521`) | `0x3EE8` (`0xffffff800214928b`) | `0x3EC8` (`0xffffff800209b197`) | Flags de modo quieto/asociación |
| `pm_llatimer` | ivar_offset | `0x28A0` (`0x21c209b`) | **`0x28A0`** (`0xffffff80021c7283`) | `0x3ED8` (`0xffffff8002160c24`) | `0x3EB8` (`0xffffff80020b2891`) | `IO80211TimerSource` para LLA |
| `pm_channelSequenceStrategy` | ivar_offset | `0x4` (`0x21d9bc2`) | **`0x4`** (`0xffffff80021dedaa`) | `0x4` (`0xffffff80021778f9`) | `0x4` (`0xffffff80020c9460`) | 1 `AwdlStateMachine`, 0 `FollowsInfra` |
| `pm_field_A66C` | ivar_offset | `0xA66C` (`0x21c16d7`) | **`0xA66C`** (`0xffffff80021c68bf`) | `0x7F4C` (`0xffffff8002160290`) | `0x7F04` (`0xffffff80020b1df7`) | Campo en modo manual |
| `peer_pppdone_flag` | ivar_offset | `0x6DE` (`0x21a9705`) | **`0x6DE`** (`0xffffff80021ae8ed`) | `0x6AE` (`0xffffff8002149648`) | `0x6AE` (`0xffffff800209b538`) | Base ivars peer = `this+0x20` |
| `peer_earlyreturn_timestamp` | ivar_offset | `0x310` (`0x21a9342`) | **`0x310`** (`0xffffff80021ae52a`) | `0x2E8` (`0xffffff8002149294`) | `0x2E8` (`0xffffff800209b1a0`) | Marca de retorno anticipado |
| `vt_awdlpeer_monitorPeer` | vtable_slot | `0x180` (`0x245a110`) | **`0x180`** (`0xffffff8002460110`) | **`0x178`** (`0xffffff80023c9aa8`) | `0x180` (`0xffffff80022c9ed0`) | Celda `__ZTV15IO80211AWDLPeer` |
| `vt_vif_inputPacket` | vtable_slot | `0xB50` (`0x24736d8`) | **`0xB50`** (`0xffffff80024796d8`) | **`0xB48`** (`0xffffff80023dfea0`) | **`0xB48`** (`0xffffff80022dc6e0`) | Celda `__ZTV23IO80211VirtualInterface` |
| `vt_vif_sendPendingPacketsToStack` | vtable_slot | `0xF30` (`0x2473ab8`) | **`0xF30`** (`0xffffff8002479ab8`) | **`0xF10`** (`0xffffff80023e0268`) | **`0xE80`** (`0xffffff80022dca18`) | Despacho de paquetes retenidos |
| `vt_vif_freePendingPackets` | vtable_slot | `0xF38` (`0x2473ac0`) | **`0xF38`** (`0xffffff8002479ac0`) | **`0xF18`** (`0xffffff80023e0270`) | **`0xE88`** (`0xffffff80022dca20`) | Descarte por caducidad |
| `call_pppDone_to_sendPendingPacketsToStack` | call_site | `0xF30` (`0x21bfc77`) | **`0xF30`** (`0xffffff80021c4e5f`) | `0xF10` (`0xffffff800215eb51`) | `0xE80` (`0xffffff80020b06eb`) | `call [rax+X]` en `pppDone` |
| `call_ageout_to_freePendingPackets` | call_site | `0xF38` (`0x21aa984`) | **`0xF38`** (`0xffffff80021afb6c`) | `0xF18` (`0xffffff800214a8e2`) | `0xE88` (`0xffffff800209c84e`) | `call [rax+X]` en `ageoutPacketsWaitingForPPP` |
| `ageout_threshold_ms` | inmediato | `0x7D1` (`0x21aa8cf`) | **`0x7D1`** (`0xffffff80021afab7`) | `0x7D1` (`0xffffff800214a82d`) | `0x7D1` (`0xffffff800209c78b`) | Umbral de 2001 ms |

---

## 4. Lista de cambios exactos que requeriría cada build

### 4.1 Para macOS 26.7.2 (25G332)
**Ningún cambio de código.**
- `Source/AirLayer/AWDLInterface.cpp`: 0 cambios necesarios.
- `Source/AirLayer/AWDLInterface.h`: 0 cambios necesarios.
- La compilación binaria existente de AirLayer funciona directamente sobre 25G332.
- A nivel del sistema de autorización de builds del repositorio (`Tools/airlayer_builds.txt`, `Source/AirLayer/KnownBuilds.inc`, `VersionGate.h`), únicamente correspondería dar de alta el identificador `25G332` en la tabla de kernels autorizados si se requiere validación por lista blanca.

---

### 4.2 Para macOS 15.8.2 (24H232)
1. **Cabecera dedicada (`AWDLInterface_15.h`)**:
   - Ajustar el desplazamiento de las ranuras de vtable de `IO80211VirtualInterface`:
     - `createPeerManager`: `0xB00` (en vez de `0xB08`).
     - `inputPacket`: `0xB48` (en vez de `0xB50`).
     - `getP2PSkywalkPeerMgr`: `0xCB8` (en vez de `0xCC0`).
     - `setMacAddress`: método virtual puro en la ranura `0xCF8` (en vez de `0xF40`).
     - `init`: `0xE60` (en vez de `0xE68`).
     - `handleIoctl`: `0xE78` (en vez de `0xE80`).
     - `setAwdlCurrentChannelSequenceIndex`: `0xEB0` (en vez de `0xEB8`).
     - `sendPendingPacketsToStack`: `0xF10` (en vez de `0xF30`).
     - `freePendingPackets`: `0xF18` (en vez de `0xF38`).
   - Retirar las declaraciones de métodos virtuales ausentes en 15:
     - `flushFlowQueues(ether_addr*)` (ranura `0xED8` en 26).
     - `signalMulticastPackets(unsigned int, unsigned int)` (ranura `0xF28` en 26).
     - `p2pMulticastTx(unsigned int, bool)` (ranura `0xF20` en 26).
2. **Desplazamiento del bloque de capacidades de tarjeta**:
   - Acceso al bloque de tarjeta en `IO80211Controller`: base en `*(controller + 0x120) + 0xB20` (en lugar de `+0xB30`).
   - Bit de doble banda (`capacidad 29`) en `+0xB27` bit `0x20` (en lugar de `+0xB37`).
3. **Desplazamiento en vtable de `IO80211AWDLPeer`**:
   - Ranura de `monitorPeer` en `0x178` (en lugar de `0x180`).
4. **Constantes de presencia e ivars**:
   - Compilar con los offsets de ivars de 24H232 (`LLAValid = 0xC70C`, `bonjourTriggersAWDL = 0x7FF8`, `stateflags = 0x3EE8`, `llatimer = 0x3ED8`, `field_A66C = 0x7F4C`, `peer_pppdone_flag = 0x6AE`, `peer_earlyreturn_timestamp = 0x2E8`).

---

### 4.3 Para macOS 14.8.9 (23J631)
1. **Cabecera dedicada (`AWDLInterface_14.h`) y binario independiente**:
   - Tamaño de instancia de las clases derivadas: `0x120` (en lugar de `0x130` en macOS 26).
   - Reubicación masiva de ranuras de vtable:
     - Bloque `IO80211VirtualInterface` desplazado -0x78 bytes (arranca en `0xDF0` en lugar de `0xE68`).
     - `sendPendingPacketsToStack` en ranura `0xE80` (en lugar de `0xF30`).
     - `freePendingPackets` en ranura `0xE88` (en lugar de `0xF38`).
     - Bloque `IO80211AWDLProtocol` desplazado -0xA0 bytes (arranca en ranura `0xF28` con `getSSID`, en lugar de `0xFC8`).
   - Retirar los métodos de vtable ausentes (`flushFlowQueues`, `getPeerDataStats`, `clearPeerDataStats`, `getPeerSlotDataStats`, `signalMulticastPackets`, `setAwdlCurrentChannelSequenceIndex`, `findPeer`, `setAWDL_CONFIG`).
   - Retirar la declaración pura de `setMacAddress(ether_addr&)` de `IO80211AWDLProtocol` (no es pura en 14).
2. **Adaptación de firmas de llamadas**:
   - Método `inputPacket`: adaptar la llamada a la firma de 4 argumentos `(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*)`, suprimiendo el quinto argumento `bool` introducido en macOS 15/26.
   - Enlace de infraestructura (`RTLAirLayer.cpp`): sustituir `registerInfraEthernetInterface` por `registerEthernetInterface`, prescindir de `allocWithParams` de `IO80211FaultReporter` y adaptar el envío de mensajes de control al controlador.
3. **Ruta de capacidades de tarjeta**:
   - El controlador no mantiene el bloque `+0xB30`. El kext debe responder al ioctl interno 12 (`CARD_CAPABILITIES`) en la interfaz virtual / controlador con el bit `0x20` de `p[7]` activo para que `IO80211AWDLPeerManager::configure` lo copie a `sub+0xB18` (`sub+0xB1F` bit `0x20`) y mantenga los canales de 5 GHz.
4. **Constantes de presencia e ivars**:
   - Compilar con los offsets de 23J631 (`LLAValid = 0xC4A0`, `bonjourTriggersAWDL = 0x7FA8`, `stateflags = 0x3EC8`, `llatimer = 0x3EB8`, `field_A66C = 0x7F04`, `peer_pppdone_flag = 0x6AE`, `peer_earlyreturn_timestamp = 0x2E8`).

---

## 5. Puntos abiertos

1. **Autorización en el gate de versiones**: Si bien el código y la ABI de 25G332 son 100 % idénticos a 25G241, el módulo de control de compilaciones (`VersionGate.h`) comprobará la versión exacta del kernel `?`. Se debe registrar `25G332` en `Tools/airlayer_builds.txt` para habilitar la carga normal del kext en dicha versión.
2. **Comportamiento dinámico de tiempo compartido (MCC) en 14/15**: Las secuencias 129 y los eventos 41/43 son idénticos a nivel de datos y estructura, pero los tiempos de respuesta de la máquina de estados de sincronización sobre radios sin firmware AWDL nativo no han sido medidos en hardware bajo los planificadores de hilos de Sonoma y Sequoia `?`.
3. **Pares Wi-Fi Aware / NAN en 14**: En 23J631 `wifip2pd` presenta dependencias adicionales respecto a la inicialización de colas de paquetes `mbuf` legacy `?`. No afecta a AirDrop ni al camino AWDL estándar de AirLayer.
