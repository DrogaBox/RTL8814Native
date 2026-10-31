# Entrega de tramas de datos AWDL recibidas a la `RxCompletionQueue` de `awdl0`

Cómo debe entregar un driver una trama de datos AWDL recibida en el aire para que la pila la vea como
paquete Ethernet: el formato exacto en el aire (spec owly, AWDL-13/14), la conversión 802.11→Ethernet
que hace el host Broadcom como referencia, el contrato de setters del paquete Skywalk con sus nombres
mangled, las tres vías de encolado en la cola de recepción con firmas y flags, y qué símbolos puede
enlazar un kext externo. Base: kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241,
Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`), volcados y decompilaciones Hex-Rays sobre una
copia de la base de datos anotada en `Build/ida_awdl/task_rx_data/` (`probe*.txt`, `probe*.py`), más el
desensamblado previo de `Build/ida_awdl/task_dataencap/dis_rxdrain.txt`. Referencia del protocolo en el
aire: `~/Desktop/AWDLLab/refs/owly` (SPEC §3.5 y `crates/owly-awdl/src/frame.rs`). Direcciones = VA del
KC. Lo no observado va con `?`. Complementa `AWDL_DATA_ENCAP.md` (TX, RX y multicast) y
`AWDL_PROXIMITY_DATAPATH.md` §10; corrige dos detalles de vtable de aquel doc que se señalan en §4 y §5.

## 1. Resumen

1. En el aire una trama de datos AWDL es 802.11 de datos de **tres direcciones** (24 bytes, o 26 con
   QoS) con `To DS`/`From DS` a 0 y BSSID `00:25:00:ff:94:73`; el cuerpo es SNAP Apple
   `AA AA 03 00 17 F2 08 00` + cabecera AWDL de datos de 8 bytes (`03 04`, secuencia u16 LE, `00 00`,
   ethertype BE `86 DD`) + IPv6. La variante QoS con el bit A-MSDU (`0x0080` del QoS control) lleva
   subtramas con sus propias direcciones y longitudes (owly AWDL-14).
2. La pila espera **Ethernet II en `buffer + dataOffset`**: DA, SA, ethertype, payload. Broadcom recibe
   del firmware la trama cruda con el flag 0x2 del mensaje de completado y el host
   (`AppleBCMWLANPCIeSkywalk::rxCompRingDrain`) la convierte a Ethernet reutilizando los 14 bytes de la
   cabecera 802.11, detecta el SNAP Apple, quita 16 bytes y recala el ethertype a +12. Un driver sin
   firmware hace esa conversión en software.
3. El paquete se prepara con `IOSkywalkPacketBufferPool::allocatePacket` (del pool de la propia
   interfaz), `IOSkywalkPacket::prepare` (cola nula), copia del frame, `setDataOffsetAndLength` y
   `setLinkHeaderLength(14)`; el encaje final es
   `IOSkywalkRxCompletionQueue::enqueuePackets(IOSkywalkPacket const*, uint, uint)` por lotes **con
   `n < capacidad` (estricto)** y flags a 0. Broadcom usa en su lugar `stagePacket` + `requestEnqueue(0,0)`
   al fin del lote: dos entradas de la misma cola.
4. La familia encadena los kern packets, completa cada paquete (`completeWithQueue(queue, 2, 0)`), toca
   el BPF de entrada si está activo, avisa a la action de la cola y entrega con
   `kern_netif_queue_rx_enqueue(netif, cadena, n, 1)`: a partir de ahí el ifnet BSD recibe un mbuf con
   la cabecera Ethernet.
5. Los símbolos que un kext externo necesita para este camino (setters del paquete, alocación del
   pool, encolado de las dos colas de completado, factoría de cola) están **exportados** en las symtabs
   del KC: 31 de los 35 comprobados con `kcsym.py` (tipo `0xf`); los 4 restantes son helpers internos
   (`gatedEnqueue`, `getNextPacket`, `getSourceQueue`, `getFreeSpace`) y no exportan.

## 2. La trama de datos AWDL en el aire (owly, AWDL-13/14)

Spec: `docs/SPEC.md` §3.5 (`AWDL-13` en la línea 299, `AWDL-14` en la 310). Implementación:
`crates/owly-awdl/src/frame.rs`. Constantes: `fc::DATA = 0x0008`, `fc::QOS_DATA = 0x0088` (líneas
38-39), `HEADER_LEN = 24` (52), `DATA_HEADER_LEN = 16` (56), `MAX_AMSDU_SUBFRAMES = 16` (59),
`LLC_SNAP = AA AA 03 00 17 F2 08 00` (73), `DATA_HEAD = 0x0403` (74), `ETHERTYPE_IPV6 = 0x86dd` (75);
`BSSID = 00:25:00:ff:94:73` (`lib.rs:169`), `OUI = 00:17:f2` (`lib.rs:172`).

Cabecera MAC (tres direcciones; `parse_header`, `frame.rs:127-136`; test golden, `frame.rs:387-410`):

| offset | tamaño | contenido |
|---:|---:|---|
| 0 | 2 | frame control: `0x0008` datos o `0x0088` datos QoS; `To DS`/`From DS` (bits 8-9) **a 0**; descartar con `PROTECTED` (`0x4000`), `MORE_FRAGMENTS` (`0x0400`) o fragmento ≠ 0 (`frame.rs:151`) |
| 2 | 2 | duration |
| 4 | 6 | addr1 = **destino** (receptor) |
| 10 | 6 | addr2 = **origen** (transmisor) |
| 16 | 6 | addr3 = BSSID AWDL `00:25:00:ff:94:73` (`frame.rs:160`) |
| 22 | 2 | sequence control (12 bits de secuencia en los altos) |
| 24 | 2 | **sólo QoS**: QoS control; bit A-MSDU = `0x0080` (`frame.rs:164-167`) |

Cuerpo de un MSDU (no A-MSDU: sin los 2 bytes de QoS control; con A-MSDU: cuerpo de cada subtrama;
`msdu()`, `frame.rs:253-266`; `put_msdu`, `frame.rs:301-308`):

| offset del MSDU | tamaño | contenido |
|---:|---:|---|
| 0 | 8 | LLC/SNAP `AA AA 03` + OUI `00 17 F2` + protocolo `08 00`. owly sólo valida `AA AA 03` y `08 00` (`frame.rs:257`, «The OUI isn't [checked]»); Broadcom exige además el OUI completo (§3) |
| 8 | 2 | head `03 04` (u16 LE `0x0403`) |
| 10 | 2 | secuencia AWDL u16 **LE**, por nodo, incrementada por trama; las subtramas de un A-MSDU llevan números consecutivos (`frame.rs:304`, `345`) |
| 12 | 2 | relleno `00 00` |
| 14 | 2 | **ethertype real BE** (`0x86DD`) |
| 16 | n | payload IPv6 |

Byte de oro (`frame.rs:391-400`): `… 30 12 | aa aa 03 00 17 f2 08 00 | 03 04 07 00 00 00 86 dd | IPv6`.

Variante A-MSDU (`msdus()`, `frame.rs:228-251`; `amsdu_frame()`, `frame.rs:326-348`): tras el QoS
control, cada subtrama es destino (6) + origen (6) + **longitud BE16** (valor = `16 + longitud del
payload`, `frame.rs:344`) + el cuerpo de arriba; entre subtramas (no tras la última) relleno a múltiplo
de 4 (`pad = (4 − (14+len) % 4) % 4`, `frame.rs:246`); máximo 16 subtramas por trama (AWDL-14, límite
de `owl-08`). owly transmite las tramas de un solo MSDU **sin QoS** (`fc::DATA`,
`frame.rs:313`) y los A-MSDU con QoS y TID 0 (`frame.rs:334-335`); un receptor debe aceptar ambos
subtipos (`frame.rs:157`).

**Reconstrucción Ethernet** (`msdus()`+`msdu()`, `frame.rs:228-266`; `node.rs:483`): por cada MSDU,
`DA` = addr1 (no A-MSDU) o el destino de la subtrama; `SA` = addr2 (no A-MSDU) o el origen de la
subtrama; `ethertype` = el BE16 de +14 del MSDU; payload = +16. El frame es `[DA][SA][ethertype][payload]`.
owly además filtra `ethertype == 0x86dd` y `DA == propia || multicast` (`node.rs:483`).

## 3. El formato que espera la cola RX (referencia Broadcom, 25G241)

`AppleBCMWLANPCIeSkywalk::rxCompRingDrain(AppleBCMWLANByteRing*, void*, uint)` (`0xffffff8001549dea`;
inicio de función verificado en la IDB). Por mensaje del anillo de completado: longitud u16 en +0x0E,
data offset u16 en +0x10, flags u16 en +0x12 (`movzx r12d, word ptr [rax+12h]` en
`0xffffff800154a1ba`). El puntero del frame se forma sumando al paquete el headroom actual
(`getHeadroom()`, `0xffffff800154a1a3`) el data offset del mensaje (`add r15b, r13b` en
`0xffffff800154a1bf`), fijándolo con `setHeadroom(headroom + msgOff)` (`0xffffff800154a1fa`) y leyendo
`getVirtualAddress() + ese valor` (`0xffffff800154a205`–`0xffffff800154a20b`); más adelante lo deja
consistente con el contrato normal: `setDataOffset(headroom + msgOff)` (`0xffffff800154a603`) y frame
= `getVirtualAddress() + getDataOffset()` (`0xffffff800154a62a`–`0xffffff800154a645`). Qué coloca el
firmware entre el headroom del pool y el frame es `?`.

Ruta role 6 (los VA están dentro de `rxCompRingDrain`; volcado completo en
`Build/ida_awdl/task_dataencap/dis_rxdrain.txt`):

1. Longitud mínima: `< 28` → descarte «Too small (len %d)» (`cmp word ptr [rbp+var_58], 1Ch` en
   `0xffffff800154a1a8`). 28 = 14 (pseudo-Ethernet) + 8 (SNAP) + `03 04` + seq + `00 00`; el ethertype
   real va en +28..+30 del formato interno.
2. **Conversión 802.11→Ethernet** si el flag `0x2` está en el mensaje (`test r12b, 2` en
   `0xffffff800154a1df`, bloque en `0xffffff800154a51d`–`0xffffff800154a58a`): exige versión 802.11 0
   (`test byte ptr [rax+1], 3`, `0xffffff800154a520`), trama de datos (`and dl, 0Ch; cmp dl, 8`,
   `0xffffff800154a52e`), longitud ≥ 24+2·QoS+4 (`0xffffff800154a540`); con el bit QoS del frame
   control (`shr cl, 7`, `0xffffff800154a53a`) la cabecera mide 24 o 26 (`0xffffff800154a550`).
   Construye la Ethernet dentro de la propia cabecera: **DA = addr1** (`0xffffff800154a569`/
   `0xffffff800154a570`), **SA = addr2** (`0xffffff800154a559`/`0xffffff800154a562`), avanza el offset
   `hdrLen−14` (`0xffffff800154a579`) y recorta `(hdrLen−14)+4` de la longitud (`0xffffff800154a58a`),
   donde los 4 son el FCS. La trama queda `[DA][SA][longitud 802.3][SNAP 8][03 04][seq][00 00][ethertype][payload]`
   (formato interno host↔firmware de Broadcom; el campo de longitud de +12 no existe en el aire).
3. **SNAP Apple**: comprueba `AA AA 03` en +14 (`0xffffff800154a20f`–`0xffffff800154a22d`) y el dword
   en +17 contra `0x08F21700` = bytes `00 17 F2 08` (`cmp eax, 8F21700h` en `0xffffff800154a238`):
   - Coincide (AWDL): copia DA+SA a +16/+24, avanza headroom +16 (`add cl, 10h` en
     `0xffffff800154a25c`), recorta 16 de la longitud (`0xffffff800154a263`) y `setHeadroom`
     (`0xffffff800154a271`). Resultado: **Ethernet II `[DA][SA][ethertype][payload]`**, con el
     ethertype de +28 recalado en +12, `dataOffset` +16 y longitud −16.
   - OUI ≠ 0: descarte «BAD OUI … Ethertype = 0x%04x» (`0xffffff800154a713`).
   - OUI 0 (RFC 1042): quita 8 bytes promoviendo el protocolo del SNAP a ethertype y desvía a la
     interfaz LLW (id 9, `getSkywalkInterface(core, 9)`, `0xffffff800154aa4b`). Sin SNAP: exige
     ethertype ≥ 0x600 (`cmp word ptr [r12+0Ch], 600h` en `0xffffff800154a5b3`) y también LLW. **Sólo
     las tramas con el SNAP Apple alcanzan la cola de `awdl0`.**
4. setters finales (§4), pre-hook `inputPacket` (§6), `setRXTimestamp`
   (`__ZN23AppleBCMWLANPCIeSkywalk14setRXTimestampEPvP29AppleBCMWLANPCIeSkywalkPacketP23IO80211SkywalkInterfaceb`
   en `0xffffff800154a3fb`) y encaje con `stagePacket(pkt, false, false)` (`0xffffff800154a44c`,
   `call [rax+340h]`).
5. Al agotar el lote, para cada interfaz 0..11 (`getSkywalkInterface(core, i)` por el slot 632
   (`+0x13C0`) de `AppleBCMWLANCore`, `0xffffff800154aba5`): `getRxCompQueue` (slot `+0xD70`,
   `0xffffff800154abb9`) y `requestEnqueue(0, 0)` (`call [rax+298h]` en `0xffffff800154abd1`);
   `flushPackets` (`call [rax+330h]` en `0xffffff800154abef`) si falla; `scheduleRxCompleteLegacy`
   (`0xffffff800154acfc`) rellena el anillo.

No hay tratamiento A-MSDU en este camino del host: la subtrama no se separa y el resultado sería un
frame Ethernet cuyo payload empieza por el destino/origen/longitud de la subtrama. Que el firmware
Broadcom desagregue los A-MSDU antes del mensaje de completado es `?`. owly los desagrega en software
(`frame.rs:228-251`) y AWDL-14 lo declara obligatorio para el receptor.

## 4. El contrato del paquete Skywalk

Convención: las llamadas `call [vptr+X]` del código leen la celda `ZTV+X+0x10`. Vtables verificadas
celda a celda (`Build/ida_awdl/task_rx_data/probe.txt` y `probe2.txt`): `__ZTV15IOSkywalkPacket`
`0xffffff8002b3b510`, `__ZTV22IOSkywalkNetworkPacket` `0xffffff8002b3ba68`,
`__ZTV29AppleBCMWLANPCIeSkywalkPacket` `0xffffff80015ae548`,
`__ZTV26IOSkywalkRxCompletionQueue` `0xffffff8002b3e890`,
`__ZTV40AppleBCMWLANPCIeSkywalkRxCompletionQueue` `0xffffff80015ad9c8`,
`__ZTV25IOSkywalkPacketBufferPool` `0xffffff8002b3b1c8`. Corrección a `AWDL_DATA_ENCAP.md` §8: en la
vtable de la familia, `setDataLength` está en la celda `+0x148` (no `+0x138`) y `getDataVirtualAddress`
en `+0x188` (no `+0x178`); los offsets de subclase citados allí (`+0x1F0`/`+0x1F8`/`+0x200` del paquete
PCIe) corresponden a celdas `+0x200`/`+0x208`/`+0x210`.

Slots usados por `rxCompRingDrain` sobre el paquete (todas las llamadas verificadas en el desensamblado
y resueltas contra `__ZTV29AppleBCMWLANPCIeSkywalkPacket`):

| llamada | celda ZTV | objetivo |
|---|---|---|
| `[vptr+0x138]` en `0xffffff800154a613` | `+0x148` | `__ZN15IOSkywalkPacket13setDataLengthEj` — `setDataLength(uint)`, `0xffffff8002b2c454` |
| `[vptr+0x148]` en `0xffffff800154a603` | `+0x158` | `__ZN15IOSkywalkPacket13setDataOffsetEt` — `setDataOffset(ushort)`, `0xffffff8002b2c5c0` |
| `[vptr+0x150]` en `0xffffff800154a639` | `+0x160` | `__ZNK15IOSkywalkPacket13getDataOffsetEv` — `0xffffff8002b2c5e2` |
| `[vptr+0x1F0]` en `0xffffff800154a62a` | `+0x200` | `__ZN29AppleBCMWLANPCIeSkywalkPacket17getVirtualAddressEv` — `0xffffff8001548498` |

Semántica decompilada (`probe2.txt`, `probe3.txt`):

- `IOSkywalkPacket::setDataLength(uint)` (`0xffffff8002b2c454`): sólo con **un buffer**
  (`getPacketBufferCount() == 1`); delega en `IOSkywalkPacketBuffer::setDataLength(buffer, uint)`
  (`0xffffff8002b2c602`), que rechaza `len + offset_del_buffer > tamaño del segmento` con `0xE00002C2`.
  Con más de un buffer devuelve `0xE00002C7`.
- `setDataOffset(ushort)` (`0xffffff8002b2c5c0`): exige ≥ 1 buffer (si no `0xE00002F0`); delega en
  `IOSkywalkPacketBuffer::setDataOff`.
- `setDataOffsetAndLength(ushort, uint)` (`0xffffff8002b2c538`,
  `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj`): la variante completa — valida
  `bufferSize >= offset` y `len + offset ≤ bufferSize × nº de buffers` (si no `0xE00002C2`) y reparte
  por buflets con `IOSkywalkPacketBuffer::setDataOffAndLen`. Es la que usa la capa RTL
  (`Source/AirLayer/RTLAirLayer.cpp:194`). Es el camino seguro con `maxBuffersPerPacket = 1` y también
  con buflets.
- `getDataVirtualAddress()` (`0xffffff8002b06518`, `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv`):
  devuelve **dirección virtual del segmento + `memorySegmentOffset` del primer buffer**, sin sumar el
  `dataOffset`: el frame está en `getDataVirtualAddress() + getDataOffset()`.
- `prepare(IOSkywalkPacketQueue*, ulonglong, uint)` (`0xffffff8002b0658a`,
  `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj`): si el byte +88 ya tiene el bit 2 devuelve
  0 (ya preparado); con cola no nula lee su dirección; llama al virtual `prepareWithQueue` (vptr+392 →
  celda `ZTV+0x198`: para un paquete de familia, `IOSkywalkPacket::prepareWithQueue(IOSkywalkPacketQueue*, uint, uint)`,
  `0xffffff8002b065d6`; para el paquete PCIe, `IO80211NetworkPacket::prepareWithQueue`,
  `0xffffff8002350f9a`). Un paquete recién alocado **necesita `prepare` antes de tocar el
  buffer**: sin él `getDataVirtualAddress` falla (pánico 0.15 documentado en
  `Source/AirLayer/RTLAirLayer.cpp:456-460`).
- Métodos directos (no virtuales) de `IOSkywalkNetworkPacket`: `setLinkHeaderLength(uchar)`
  `0xffffff8002b07180` (`__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh`, usado por la capa RTL
  con 14), `setLinkHeaderOffset(uint)` `0xffffff8002b071a8` (`__ZN22IOSkywalkNetworkPacket19setLinkHeaderOffsetEj`,
  usado por Broadcom en `0xffffff800154a61f` con el mismo valor del `dataOffset`) y
  `setHeadroom(uchar)` `0xffffff8002b07158`.

Alocación (`__ZTV25IOSkywalkPacketBufferPool`; la vtable de la familia no expone la alocación como
virtual de nivel superior, son métodos directos/virtuales propios):

- `allocatePacket(IOSkywalkPacket**, uint)` (`0xffffff8002b058ee`,
  `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj`): envoltorio que llama al
  virtual `allocatePacket(uint, IOSkywalkPacket**, uint)` (celda `ZTV+0x140`,
  `0xffffff8002b2bed2`) con `count = 1`. El segundo uint son las opciones del `kern_pbufpool_alloc`
  (la capa RTL pasa `1` = nosleep, `Source/AirLayer/RTLAirLayer.cpp:198` y `:250`).
- Variantes en lote: `allocatePackets(uint, uint*, IOSkywalkPacket**, uint)` `0xffffff8002b2bf7c`;
  `deallocatePacket(IOSkywalkPacket*)` `0xffffff8002b2c0de`;
  `deallocatePackets(IOSkywalkPacket**, uint)` `0xffffff8002b2c128`.

## 5. Encolado en la `IOSkywalkRxCompletionQueue`

Celdas de la subclase PCIe (`__ZTV40AppleBCMWLANPCIeSkywalkRxCompletionQueue`, offsets ZTV; la celda
X equivale a `vptr+X-0x10`):

| celda ZTV | VA | método |
|---|---|---|
| `+0x2A8` | `0xffffff8001546d18` | `__ZN40AppleBCMWLANPCIeSkywalkRxCompletionQueue14requestEnqueueEPvj` (override PCIe de `requestEnqueue(void*, uint)`) — accesible como `call [vptr+0x298]` (`0xffffff800154abd1`) |
| `+0x2B0` | `0xffffff8002b2d766` | familia: `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` — `enqueuePackets(IOSkywalkPacket const*, uint, uint)` |
| `+0x2B8` | `0xffffff8002b2d810` | familia: `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEP15IOSkywalkPacketjj` — variante que recorre una **cadena** enlazada con `setNextPacket` |
| `+0x328` | `0xffffff80015468b8` | Broadcom: `enqueuePackets(OSObject*, IO80211NetworkPacket**, uint, void*)` |
| `+0x340` | `0xffffff80015466fe` | Broadcom: `flushPackets(void)` |
| `+0x350` | `0xffffff8001546cc8` | Broadcom: `stagePacket(IO80211NetworkPacket*, bool, bool)` |
| `+0x358` | `0xffffff8001546852` | Broadcom: `freePendingPackets(IO80211NetworkPacket**, uint)` |

Las tres vías de encaje (`probe2.txt`, `probe4.txt`, `probe6.txt`):

1. **`enqueuePackets(IOSkywalkPacket const**, uint n, uint opts)`** (`0xffffff8002b2d766`): la vía de
   la capa RTL (`Source/AirLayer/RTLAirLayer.cpp:202` y `:477`, lotes de 64 con `opts = 0`).
   Requisitos: `pkts != 0`, `n != 0` y **`n < capacidad` (comparación estricta** contra el campo
   `+172` de la cola; 256 en la capa STA, `Source/AirLayer/RTLAirLayer.cpp:380`; 128 en la cola de
   `awdl0` de la etapa 1, `Source/AirLayer/AWDLInterface.cpp:189`). Errores:
   `0xE00002C2` (nulo/cero/capacidad), `0xE00002D8` (sin work loop), `0xE00002D7` (deshabilitada o no
   inicializada), `0xE00002D5` (byte `+280` bit 0 puesto). Con éxito devuelve 0. El `opts` **no llega**
   al helper de red en 25G241 (sólo se reenvía en la ruta KPipe): sin efecto observable
   (`?` semántica prevista).
2. **Variante encadenada** `enqueuePackets(IOSkywalkPacket*, uint, uint)` (`0xffffff8002b2d810`):
   recorre la cadena `getNextPacket` hasta `n` paquetes, construye la cadena kern, tapa BPF si procede,
   llama a `kern_netif_queue_rx_enqueue(netif, cadena, n, 1)` y a la action de la cola. Enlazar los
   paquetes requiere `setNextPacket` (`__ZN15IOSkywalkPacket13setNextPacketEPS_`, exportado).
3. **Modelo Broadcom** (lo que hace `rxCompRingDrain`): por paquete
   `stagePacket(pkt, false, false)` (`0xffffff8001546cc8` — sólo engancha el paquete en la lista
   pendiente de la cola: ancla en ctx+72, cabeza en ctx+80, sin entrega) y, al fin del lote,
   `requestEnqueue(0, 0)`: la familia (`0xffffff8002b2d6d8`,
   `__ZN26IOSkywalkRxCompletionQueue14requestEnqueueEPvj`) con el bit 0 del segundo argumento a 0 hace
   `gatedEnqueue(this, 0)` bajo cerrojo si la cola está habilitada (drena lo pendiente); con bit 0 a 1
   sólo incrementa contadores y `signalWorkAvailable`. `flushPackets` libera lo pendiente si
   `requestEnqueue` falla.

Qué hace la familia al encajar (`enqueuePacketsForNetworking`,
`__ZN26IOSkywalkRxCompletionQueue27enqueuePacketsForNetworkingEPKP15IOSkywalkPacketjj`,
`0xffffff8002b0f33c` — interna pero exportada, §7): por paquete, lee la cadena kern
(`kern_packet_get_next` implícito vía `getSourceQueue`/qword +40 del paquete), llama
`completeWithQueue(queue, 2, 0)` (vptr+408 → celda `ZTV+0x1A8` de `IOSkywalkPacket`,
`__ZN15IOSkywalkPacket17completeWithQueueEP20IOSkywalkPacketQueuejj`), `disposePacket` (vptr+480),
tapa `bpf_tap_packet_in` si la cola lo tiene activo (byte `+308` bit 0), suma bytes y al final avisa a
la action de la cola (vtable +616) y entrega todo con
**`kern_netif_queue_rx_enqueue(netif, cadena, n, 1)`**: ahí nace el mbuf que ve el ifnet BSD. No hay
ningún examen del contenido del frame en toda la familia: lo que entra por esta cola debe traer ya la
cabecera Ethernet en `buffer + dataOffset`.

## 6. `inputPacket`: el pre-hook de la interfaz (filtro de pares y retención PPP)

Antes de encajar, Broadcom llama al slot `+0xB50` de la interfaz (`call [rax+0B50h]` en
`0xffffff800154a65c`) = `IO80211VirtualInterface::inputPacket` (`0xffffff8002333cac`), con
`(paquete, origen, etiqueta RX, &flag, 0)`. La etiqueta es un buffer cuyo byte 0 marca multicast (bit
0) y cuyo offset +6..+11 trae la **SA**; para role 6 la familia busca el par por esa SA
(`findPeer(manager, etq+6)`) y, si no existe, descarta («Dropping … because no peer was found for
%02X:…», `incrementNumInputPacketsDropped`), con casos especiales para los DA `33:33:00:00:xx:xx`,
`33:33:80:00:00:FB` `?` y `01:00:5E:00:xx:xx` que consultan estado del par AWDL antes de encajar;
para role 2 el filtro no aplica. Un paquete retenido a la espera del ioctl 415 vuelve al datapath por
los slots `+0xF30`/`+0xF38` de la interfaz (`AWDL_PEER_PRESENCE_PPP.md` §3-§4;
`AWDL_PROXIMITY_DATAPATH.md` §11). La llamada es parte del diseño Broadcom; la familia no filtra nada
que el driver encole sin ella — omitirla deja pasar tramas de pares desconocidos.

## 7. Símbolos enlazables por un kext externo

Resultado de `kcsym.py` (symtabs de `LC_SYMTAB`/`LC_FILESET_ENTRY` del KC 25G241; `0xf` = externo
definido en su sección). Las VAs de la primera columna están verificadas en la IDB; el símbolo
indefinido de un kext se resuelve por nombre.

| símbolo mangled | VA 25G241 | uso |
|---|---|---|
| `__ZN15IOSkywalkPacket13setDataLengthEj` | `0xffffff8002b2c454` | longitud (1 buffer) |
| `__ZN15IOSkywalkPacket13setDataOffsetEt` | `0xffffff8002b2c5c0` | data offset (u16) |
| `__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj` | `0xffffff8002b2c538` | offset+longitud por buflets |
| `__ZN15IOSkywalkPacket10setDataOffEx` | — (`0xf`) | data off interno (i64) |
| `__ZN15IOSkywalkPacket16setDataOffAndLenExm` | `0xffffff8002b2c4ac` | variante i64/u64 |
| `__ZNK15IOSkywalkPacket13getDataLengthEv` | — (`0xf`) | longitud |
| `__ZNK15IOSkywalkPacket13getDataOffsetEv` | — (`0xf`) | data offset |
| `__ZNK15IOSkywalkPacket21getDataVirtualAddressEv` | `0xffffff8002b06518` | base del buffer (sin dataOffset) |
| `__ZNK15IOSkywalkPacket20getPacketBufferCountEv` | — (`0xf`) | nº de buflets |
| `__ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj` | `0xffffff8002b0658a` | preparar paquete nuevo |
| `__ZN15IOSkywalkPacket17completeWithQueueEP20IOSkywalkPacketQueuejj` | — (`0xf`) | lo llama la familia |
| `__ZN15IOSkywalkPacket13setNextPacketEPS_` | — (`0xf`) | encadenar variante 2 de §5 |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderLengthEh` | `0xffffff8002b07180` | longitud de cabecera de enlace (14) |
| `__ZN22IOSkywalkNetworkPacket19setLinkHeaderOffsetEj` | `0xffffff8002b071a8` | offset de cabecera de enlace |
| `__ZN22IOSkywalkNetworkPacket11setHeadroomEh` | `0xffffff8002b07158` | headroom (Broadcom RX) |
| `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` | `0xffffff8002b2d766` | **encaje RX por lotes** |
| `__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEP15IOSkywalkPacketjj` | `0xffffff8002b2d810` | variante encadenada |
| `__ZN26IOSkywalkRxCompletionQueue14requestEnqueueEPvj` | `0xffffff8002b2d6d8` | kick (modelo stage) |
| `__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j` | `0xffffff8002b0ee04` | crear la cola (la usa la etapa 1, `Source/AirLayer/AWDLInterface.cpp:48`) |
| `__ZN26IOSkywalkRxCompletionQueue12initWithPoolE…` (misma lista de tipos) | `0xffffff8002b0eeae` | init equivalente (celda `ZTV+0x2C0`) |
| `__ZN26IOSkywalkRxCompletionQueue27enqueuePacketsForNetworkingEPKP15IOSkywalkPacketjj` | `0xffffff8002b0f33c` | helper interno, exportado |
| `__ZN26IOSkywalkRxCompletionQueue22enqueuePacketsForKPipeEPKP15IOSkywalkPacketjj` | — (`0xf`) | helper interno, exportado |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj` | `0xffffff8002b058ee` | alocar 1 paquete |
| `__ZN25IOSkywalkPacketBufferPool14allocatePacketEjPP15IOSkywalkPacketj` | `0xffffff8002b2bed2` | alocar n (virtual, celda `ZTV+0x140`) |
| `__ZN25IOSkywalkPacketBufferPool15allocatePacketsEjPjPP15IOSkywalkPacketj` | `0xffffff8002b2bf7c` | lote con contador real |
| `__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket` | `0xffffff8002b2c0de` | devolver paquete (en errores) |
| `__ZN25IOSkywalkPacketBufferPool17deallocatePacketsEPP15IOSkywalkPacketj` | `0xffffff8002b2c128` | lote |
| `__ZN25IOSkywalkPacketBufferPool12initWithNameEPKcP8OSObjectjPKNS_11PoolOptionsE` | — (`0xf`) | crear el pool |
| `__ZN21IOSkywalkPacketBuffer13setDataLengthEj` | `0xffffff8002b2c602` | longitud de buffer |
| `__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj` | — (`0xf`) | devolución TX (ya usada) |
| `__ZN26IOSkywalkTxSubmissionQueue25refillTxRingForNetworkingEjjb` | `0xffffff8002b0d3e0` | TX: relleno de anillo (referencia; `kern_netif_queue_tx_dequeue`) |

No exportados (comprobados MISSING con `kcsym.py`): `__ZN26IOSkywalkRxCompletionQueue13gatedEnqueueEPv`,
`__ZNK15IOSkywalkPacket13getNextPacketEv`, `__ZN15IOSkywalkPacket13getSourceQueueEv`,
`__ZNK20IOSkywalkPacketQueue9getFreeSpaceEv`. El vtable de la familia del pool no tiene celda de
alocación: `allocatePacket` se enlaza por nombre directo (como ya hace la capa RTL,
`Source/AirLayer/RTLAirLayer.cpp:198`).

## 8. Receta para el driver RTL8814AU (RX de datos AWDL → `awdl0`)

El hook de radio ya entrega al kext la trama cruda con A3 = BSSID AWDL (`ccc4122`); las tramas de
**datos** que lleguen por él se procesan así (todo en software, sin firmware):

1. Validar: versión 0, subtipo datos (QoS o no), `To DS`/`From DS` = 0, sin `PROTECTED` ni
   fragmentación; BSSID en addr3 = `00:25:00:ff:94:73` (owly AWDL-1, `lib.rs:169`).
2. Cabecera: 24 bytes, +2 si el bit QoS (`0x0080` del frame control). Si el QoS control trae el bit
   A-MSDU (`0x0080` del QoS control), separar subtramas (destino 6, origen 6, longitud BE16 = 16+n,
   cuerpo, relleno a 4 entre subtramas, máximo 16 — AWDL-14); el host Broadcom no lo hace (§3), es
   responsabilidad del driver.
3. Por MSDU: exigir `AA AA 03` en +0 y, conservador, el dword `00 17 F2 08` en +3 (Broadcom,
   `0xffffff800154a238`; owly no comprueba el OUI, `frame.rs:257`); ethertype = BE16 en +14; payload =
   +16; descartar las tramas SNAP con OUI ≠ `00:17:F2` (Broadcom las manda a `llw0`; sin SNAP sólo con
   ethertype ≥ 0x600).
4. Construir Ethernet II: DA = addr1 o destino de subtrama; SA = addr2 o origen de subtrama;
   `[DA][SA][ethertype][payload]`. La secuencia AWDL de +10 del MSDU se descarta (sin estado).
5. Paquete: `allocatePacket(pool_rx, &pkt, 1)` (nosleep) → `prepare(pkt, nullptr, 0, 0)` → copia del
   frame → `setDataOffsetAndLength(pkt, 0, len)` (offset 0 vale; el frame es
   `getDataVirtualAddress() + getDataOffset()`) → `setLinkHeaderLength(pkt, 14)`. Errores →
   `deallocatePacket`. (Secuencia exacta ya operativa en la capa STA:
   `Source/AirLayer/RTLAirLayer.cpp:441-478`.)
6. Recomendado, como Broadcom: `inputPacket(pkt, origen, etiqueta, &flag, 0)` (§6) para el filtro de
   pares y la retención PPP; si no hay par, devolver el paquete al pool.
7. Encaje: lotes por `enqueuePackets(rxCompQ, pkts, n, 0)` con `n < 128` (capacidad de la cola de
   `awdl0` de la etapa 1, `Source/AirLayer/AWDLInterface.cpp:189`; chunks de 64 como la capa STA), o el
   modelo Broadcom `stagePacket` + `requestEnqueue(0,0)` + `flushPackets`. Fallo del encaje →
   `deallocatePacket` de todo el lote. La cola debe estar habilitada y con event source añadido al work
   queue (ya hecho en la etapa 1, `Source/AirLayer/AWDLInterface.cpp:201`).
8. En TX el simétrico ya está documentado (`AWDL_DATA_ENCAP.md` §7): del Ethernet II del paquete a
   `802.11 + SNAP + 03 04 + seq + 00 00 + ethertype`, contador por destino.

## 9. No verificado (`?`)

1. Si el firmware Broadcom desagrega A-MSDU antes del mensaje de completado (el host no lo hace, §3).
2. Semántica prevista del `opts` de `enqueuePackets` (aceptado sin efecto observable en la ruta
   networking de 25G241) y del byte `+280` bit 0 que produce `0xE00002D5`.
3. Los DA especiales de `inputPacket`: la rama `33:33:80:00:00:FB` exacta frente al comodín
   `33:33:00:00:xx:xx` quedó trazada a nivel de comprobaciones de bytes (`probe3.txt`), sin nombre de
   protocolo; y qué entidad llena la etiqueta RX de Broadcom (en el driver RTL la rellena el propio
   kext).
4. Qué coloca el firmware Broadcom entre el headroom del pool y el frame en RX (campo data offset del
   mensaje) — irrelevante para un driver sin firmware.
5. Toda la prueba en hardware RTL8814 (etapa pendiente; el lado STA de esta receta ya corre en `en2`).
