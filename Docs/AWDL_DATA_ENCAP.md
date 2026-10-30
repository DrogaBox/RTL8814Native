# Encapsulación de datos en el datapath de `awdl0` (TX, RX y multicast)

Formato exacto del paquete en los dos extremos del datapath de datos de la interfaz role 6: qué recibe
la acción de la cola de envío Skywalk, quién inserta la encapsulación AWDL en Broadcom, qué espera la
cola de recepción, y qué debe construir y quitar el driver RTL8814 al cruzar hacia USB. Base:
kernelcache `BootKernelExtensions.kc` (macOS 26.7.1, build 25G241, Darwin 25.6.0, md5
`f489ff8be80e6013a1bf47561dcaef45`), lectura del desensamblado y de volcados de vtable sobre una copia
de la base de datos anotada del kernelcache en `Build/ida_awdl/task_dataencap/`. Direcciones = VA del
KC. Lo no observado va con `?`. Complementa `AWDL_PROXIMITY_DATAPATH.md` (§9–§10) y
`AWDL_STAGE2B_MBUF_TO_PACKET.md`; corrige tres detalles de aquel doc que se señalan en §5 y §8.

## 1. Resumen

1. **TX**: la acción de la `IOSkywalkTxSubmissionQueue` de `awdl0` recibe un paquete del pool cuyo
   frame es **Ethernet II puro** (DA, SA, ethertype, payload) colocado en `buffer + dataOffset`, con
   `dataOffset` = headroom del pool (64). Ninguna pieza de `IO80211Family`, `IOSkywalkFamily` ni del
   `IO80211AWDLPeerManager` añade cabecera antes de la acción: la acción es el primer consumidor
   familiar del paquete.
2. **La encapsulación AWDL la inserta el host**, en la ruta role 6 de
   `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets` — no el firmware. La trama que sale por
   el flow ring hacia el firmware ya lleva el SNAP; el firmware sólo añade después la cabecera MAC
   802.11 (`?` interior del firmware). El contador de secuencia es un u16 por flow queue (por
   destino y AC), en el host.
3. **RX**: la `RxCompletionQueue` de `awdl0` recibe **Ethernet II tras el desensamblado**: el host
   PCIe convierte la trama 802.11 en crudo cuando el mensaje la trae así (flag 0x2: recorte de
   cabecera 24/26 + FCS), detecta el SNAP de Apple en +14, quita **16 bytes** (SNAP + `03 04` +
   secuencia + `00 00` + ethertype) y recompone el ethertype en +12. Todo frame que no lleva el SNAP
   de Apple se desvía a la interfaz LLW (id 9): con OUI 0 quita 8 bytes (RFC 1042) y con ethertype
   directo exige ≥ 0x600.
4. **Multicast**: en Broadcom un multicast de `awdl0` no sale directo a la radio. La cola lo duplica
   (`duplicatePacket`), marca el par con el flag 0x80 y lo guarda en el `IO80211AWDLPeerManager`
   (dos arrays de 16: original y duplicado). El temporizador multicast del manager los libera en la
   ventana AW oportuna llamando a `p2pMulticastTx` de la interfaz, que realimenta
   `dequeuePackets` (el flag 0x80 los convierte en «transmitibles»: se quita el flag, se inserta el
   SNAP y entran en la flow queue del DA).
5. **El driver RTL8814** debe: en TX construir `802.11 + AA AA 03 00 17 F2 08 00 | 03 04 | seq | 00
   00 | ethertype` desde el Ethernet II del paquete, con contador por destino; en RX hacer la
   conversión inversa (802.11→Ethernet II, quitar 16 bytes, recomponer ethertype) y entregar
   Ethernet II en la cola de recepción. El campo de longitud 802.3 que usa Broadcom internamente no
   existe en el aire y no debe reproducirse.

## 2. El frame en el aire y el formato interno Broadcom

En el aire (owly AWDL-13 y las capturas de `AWDL_REFERENCIAS_EXTERNAS.md` §5):

```
802.11 data (QoS opcional) | AA AA 03 | 00 17 F2 | 08 00 | 03 04 | seq (u16) | 00 00 | ethertype (BE) | IPv6
```

El formato interno host↔firmware de Broadcom es el mismo cuerpo precededido de una pseudo-cabecera
Ethernet de 14 bytes en la que el campo de tipo es una **longitud 802.3**:

| offset | contenido |
|---:|---|
| +0..+6 | DA |
| +6..+12 | SA |
| +12..+14 | longitud 802.3 = `bswap16(longitud total − 14)` (sólo en el formato interno; no existe en el aire) |
| +14..+22 | SNAP `AA AA 03 00 17 F2 08 00` (LLC + OUI Apple 00:17:F2 + protocolo 0x0800) |
| +22..+24 | `03 04` |
| +24..+26 | secuencia: bits 0–11 contador por destino, bits 12–15 AC de la cola de envío |
| +26..+28 | `00 00` |
| +28..+30 | ethertype real (BE, p. ej. 86 DD) |
| +30.. | payload |

La cabecera 802.11 no existe en este formato: la añade el firmware al transmitir y llega en la trama
RX cuando el mensaje lo indica (§5). En el host el campo `seq` se escribe como u16 little-endian y el
firmware copia el cuerpo tal cual (`?` copia literal no verificada); las capturas externas no
explicitan el orden de bytes.

Las dos constantes Broadcom:

- `0x0008F2170003AAAA` — los 8 bytes del SNAP `AA AA 03 00 17 F2 08 00` empaquetados como
  inmediato de 64 bits little-endian (OUI Apple `00:17:F2`, protocolo SNAP 0x0800). Escrito en TX en
  `0xffffff80015432fd` (`mov rax, 8F2170003AAAAh`) → `0xffffff8001543307`. El patrón `… 17 8F 08`
  citado en `AWDL_PROXIMITY_DATAPATH.md` §9 era una errata: el byte es F2.
- `0x08F21700` — los 4 bytes comparados en RX (`00 17 F2 08`: OUI Apple + primer byte del protocolo
  0x0800), en `0xffffff800154a238` (`cmp eax, 8F21700h`). El valor `0x08F20800` citado en
  `AWDL_PROXIMITY_DATAPATH.md` §10 era una errata de transcripción del mismo dato.

## 3. TX: el paquete que recibe la acción (pregunta 1)

La acción es `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(OSObject*, IO80211NetworkPacket**,
unsigned, void*)` (`0xffffff80015420f0`, slot +0x310 de la cola). El kernel la alimenta por la vía
normal (`packetSubmission` → `kern_netif_queue_tx_dequeue` → conversión mbuf→paquete, trazada en
`AWDL_STAGE2B_MBUF_TO_PACKET.md` §2) y Broadcom además la llama directamente desde `p2pMulticastTx`
(§6). Por cada paquete:

1. `safeMetaCast` a `AppleBCMWLANPCIeSkywalkPacket` y comprobación de interfaz habilitada/chip listo
   (errores `0xE00002D8`/`0xE0000001`; rama de rol en `0xffffff80015423d3`: `bt eax(0x640), rol` para
   roles 6/7/9/10, `cmp r15d, 0Ah` en `0xffffff800154244e`, `cmp r15d, 9` en `0xffffff8001542458`,
   `cmp r15d, 6` en `0xffffff8001542468`).
2. Lectura del frame: `getHeadroom` (`0xffffff8001542486`, llamada directa a
   `IOSkywalkNetworkPacket::getHeadroom`) y dirección base del buffer por el slot +0x1F0 del paquete
   (`0xffffff8001542495`; en la vtable de `AppleBCMWLANPCIeSkywalkPacket` esa celda es
   `getVirtualAddress`, `0xffffff8001548498`). El frame queda en `base + headroom`
   (`0xffffff800154249f`: `lea r12, [rax+rcx]`), y el código lee DA en +0 (`0xffffff80015424c7`) y
   ethertype en +12 (`0xffffff80015432a7`): **el frame que la acción recibe es Ethernet II**.
3. El headroom es el del pool: 64, el valor constante que la interfaz puso en el descriptor de
   registro (+0x50) y que `registerSkywalkInterface` copió al tracker del pool
   (`AWDL_PROXIMITY_DATAPATH.md` §3–§4). El kernel publica ese mismo valor como `txDataOffset`:
   `IOSkywalkNetworkInterface::getTxDataOffset` (`0xffffff8002b0baba`) devuelve el u16 en
   `RegistrationInfo+0x3C` (`movzx eax, word ptr [rax+3Ch]` en `0xffffff8002b0bad2`). El offset
   exacto que usa el kernel al copiar el mbuf sigue sin trazarse a instrucción (`?`,
   `AWDL_STAGE2B_MBUF_TO_PACKET.md` §9.3); el contrato observable es «frame en `base+dataOffset`».
4. **Sin cabeceras añadidas**: ninguna pieza de la familia toca el paquete antes de la acción (la
   acción es el primer código familiar que lo consume; el mbuf del stack ya traía la cabecera
   Ethernet desde dlil, `AWDL_STAGE2B_MBUF_TO_PACKET.md` §2). El `logSkywalkTxReqPacket` del slot
   +0x0B60 y el PeerManager actúan después de la inserción del SNAP.

Estado del paquete en la entrada de la acción: `dataOffset` = 64, `length` = longitud del frame
Ethernet II, link header offset = el que fijara el kernel (`?`; el código Broadcom lo sobrescribe en
la inserción, §4). Con el headroom de 64 queda sitio para anteponer 16 bytes sin segunda copia: es
exactamente lo que la inserción hace bajando el headroom a 48.

## 4. TX: inserción del SNAP y contador de secuencia (pregunta 2)

**Quién**: el host, en la ruta role 6 de `dequeuePackets`. El firmware recibe la trama ya
encapsulada (la `AppleBCMWLANPCIeFlowQueue::enqueuePacket` ocurre después de la escritura del SNAP,
`0xffffff80015433ca`) y sólo añade la cabecera 802.11 (`?` interior del firmware). No hay segunda
inserción en ningún otro punto del camino host.

`IO80211Family` contiene `IO80211AWDLPeerManager::addAwdlSnapHeader(mbuf*, u16)`
(`0xffffff8002216cd0`), que monta la misma cabecera interna (MAC destino y origen, longitud 802.3,
la constante `0x0008F2170003AAAA` y el resto a partir de ivars del manager). En 25G241 no tiene
ninguna referencia, ni de código ni de datos: es código muerto. La familia no encapsula ningún
paquete de `awdl0`; lo hace quien transmite.

Ruta role 6 (tras las validaciones de §3):

- **Unicast** (bit 0 de DA a 0, testeado en `0xffffff80015424aa`): se construye la clave de flow
  queue `{ifId, fAc, DA}` (`0xffffff80015424c7`–`0xffffff80015424e1`; `fAc` = u32 en ivars+0xB8 de
  la cola, el AC de esta cola de envío) y se salta a `0xffffff8001543228`.
- **Multicast** (bit 0 de DA a 1): no se transmite aquí; se duplica y se guarda en el stash del
  PeerManager (§6). Excepción: si el paquete ya lleva el flag 0x80 (re-alimentado por
  `p2pMulticastTx`, §6) se limpia (`and al, 7Fh` en `0xffffff800154321f`, escritura en
  `0xffffff8001543225`) y sigue como unicast.

Secuencia de inserción para unicast (todas las cabezas verificadas en el desensamblado):

| VA | instrucción | efecto |
|---|---|---|
| `0xffffff8001543267` | `call [vtable+140h]` | `getDataLength` → L (longitud del frame Ethernet II) |
| `0xffffff8001543271` | `add r15b, 0F0h` | nuevo headroom = headroom − 16 |
| `0xffffff800154327c` | `call [vtable+1F0h]` | `getVirtualAddress` (base del buffer) |
| `0xffffff8001543294`–`0xffffff80015432ad` | lecturas del frame viejo | guarda DA+SA[0:2] (+0), SA[2:6] (+8) y ethertype (+12) |
| `0xffffff80015432d0` | `call setHeadroom` | `IOSkywalkNetworkPacket::setHeadroom(headroom−16)`: el frame pasa a empezar 16 bytes antes |
| `0xffffff80015432d5`–`0xffffff80015432e7` | escrituras | copia DA+SA (12 bytes) al nuevo inicio |
| `0xffffff80015432ec`–`0xffffff80015432f7` | `add eax,2; rol ax,8` | escribe en +12 la longitud 802.3 `bswap16(L+2)` |
| `0xffffff80015432fd`/`0xffffff8001543307` | `mov rax, 8F2170003AAAAh` | SNAP de 8 bytes en +14 |
| `0xffffff800154330c` | `mov word ptr […+16h], 403h` | `03 04` en +22 |
| `0xffffff8001543314` | `mov […+1Ah], 0` | `00 00` en +26 |
| `0xffffff800154331a`–`0xffffff8001543321` | `mov […+1Ch], ax` | ethertype original en +28 |
| `0xffffff800154332f`–`0xffffff8001543339` | contador | lee e incrementa el u16 en +0x8A del struct interno de la flow queue |
| `0xffffff800154334d`–`0xffffff8001543358` | `and ecx,0FFFh; shl eax,0Ch; or` | secuencia en +24 = `(contador & 0xFFF) \| (fAc << 12)` (usa el valor previo al incremento) |
| `0xffffff8001543368` | `call [vtable+148h]` | `setDataOffset(headroom−16)` |
| `0xffffff8001543378` | `call [vtable+138h]` | `setDataLength(L+16)` |
| `0xffffff8001543384` | `call setLinkHeaderOffset` | link header offset = headroom−16 |
| `0xffffff80015433b7` | `call [iface vtable+0B60h]` | `logSkywalkTxReqPacket(pkt, scratch, frame, fAc, isUnicast)` |
| `0xffffff80015433ca` | `call enqueuePacket` | `AppleBCMWLANPCIeFlowQueue::enqueuePacket(fq, pkt, true)` |

Sin flow queue disponible → `0xE00002C0` (`0xffffff80015433d4`). Tras la inserción: `dataOffset` =
48, `length` = L+16, link header offset = 48.

**Contador de secuencia**: vive en el struct interno de cada `AppleBCMWLANPCIeFlowQueue` (el objeto
de 32 bytes guarda en su campo +0x18 un bloque creado en la fábrica
`withProviderAndQueueInfo`, `0xffffff8001553f30`, que inicializa el contador a 0 en +0x8A y el id de
flow ring en +0x88 a 0xFFFF). Como las flow queues se indexan por `{interfaz, AC, DA}`
(`findOrCreateFlowQueue`, `0xffffff8001542006`; hasta 16 por cola, contador de ocupación en
ivars+0xA8), el contador es **por nodo y por AC**, coincidiendo con la observación externa de
secuencias propias por nodo (`AWDL_REFERENCIAS_EXTERNAS.md` §5). Los 4 bits altos del campo `seq`
llevan el AC (el id de la cola de envío); qué hace un receptor Apple con ese nibble es `?`.

La notificación del slot +0x0B60 es `IO80211VirtualInterface::logSkywalkTxReqPacket`
(`0xffffff800233218a`): busca el par por DA, actualiza su marca de tiempo, opcionalmente tapa a BPF
y, para role 6, llama a `IO80211AWDLPeerManager::preQueuePacketCheckQuietMode`. No condiciona el
encaje. (El doc `AWDL_PROXIMITY_DATAPATH.md` §9 citaba este aviso en el slot +0x918; el desensamblado
muestra +0x0B60, y +0x918 es `processInterfaceCommand`.)

## 5. RX: el formato que espera la RxCompletionQueue (pregunta 3)

`AppleBCMWLANPCIeSkywalk::rxCompRingDrain` (`0xffffff8001549dea`) procesa cada mensaje del anillo de
completado RX. Campos del mensaje: tipo 18 en +0, ifId en +1, longitud u16 en +0x0E, data offset u16
en +0x10, flags u16 en +0x12 (`movzx r12d, word ptr [rax+12h]` en `0xffffff800154a1b6`). El puntero
de frame se forma como `getVirtualAddress() + getHeadroom() + dataOffset` del mensaje
(`0xffffff800154a1a3`, `0xffffff800154a1bf`, `0xffffff800154a1fa`): el headroom del pool (64) más el
offset que anuncia el firmware; qué coloca el firmware en los bytes intermedios es `?`.

Ruta role 6 (`0xffffff800154a19c` en adelante):

1. **Longitud mínima**: `cmp word ptr [rbp+var_58], 1Ch` en `0xffffff800154a1a8`; < 28 → descarte
   «Too small (len %d)» (`0xffffff800154a4c5`). 28 = 14 (pseudo-Ethernet) + 8 (SNAP) + 2 (`03 04`) +
   2 (seq) + 2 (`00 00`); el ethertype real queda en +28..+30.
2. **Conversión 802.11→Ethernet** si el mensaje trae el flag 0x2 (`test r12b, 2` en
   `0xffffff800154a1df` → bloque en `0xffffff800154a51d`): exige protocolo 802.11 versión 0
   (`test byte ptr [rax+1], 3` en `0xffffff800154a520`), frame de datos (`and dl, 0Ch; cmp dl, 8` en
   `0xffffff800154a52e`–`0xffffff800154a534`), longitud ≥ 24+2·QoS+4 (`lea edx, ds:1Ch[rcx*2]` en
   `0xffffff800154a540`); con QoS = bit 7 del frame control (`0xffffff800154a53a`) la cabecera mide
   24 o 26 (`lea ecx, ds:18h[rcx*2]` en `0xffffff800154a550`); construye la cabecera Ethernet dentro
   de la propia cabecera 802.11 — DA = addr1 (`0xffffff800154a569`/`0xffffff800154a570`), SA = addr2
   (`0xffffff800154a559`/`0xffffff800154a562`) —, avanza el offset `hdrLen−14`
   (`add ecx, 0FFFFFFF2h` en `0xffffff800154a579`) y recorta `(hdrLen−14)+4` de la longitud
   (`add eax, 0FFFFFFFCh` en `0xffffff800154a58a`): los 4 bytes son el **FCS**, no una etiqueta VLAN.
   (El doc `AWDL_PROXIMITY_DATAPATH.md` §10 describía esta rama como «VLAN opcional (strip 4)»; el
   desensamblado muestra conversión 802.11→Ethernet con recorte de FCS.)
3. **SNAP**: con el frame ya en formato Ethernet, comprueba `AA AA 03` en +14..+17
   (`0xffffff800154a20f`–`0xffffff800154a22d`) y lee el dword en +17 (`0xffffff800154a233`):
   - **`0x08F21700`** (OUI Apple + `08`): trama AWDL. Copia DA+SA[0:2] (+0..+8) a +16 y SA[2:6]
     (+8..+12) a +24 (`0xffffff800154a248`–`0xffffff800154a251`), avanza el headroom +16
     (`0xffffff800154a25c`) y recorta 16 de la longitud (`0xffffff800154a263`), `setHeadroom`
     (`0xffffff800154a271`). Resultado: frame Ethernet II `[DA][SA][ethertype][payload]`, con el
     ethertype de +28 de la trama pre-strip recalado en +12, longitud −16 y `dataOffset` +16.
   - **OUI distinto de 00**: descarte «BAD OUI … Ethertype = 0x%04x»
     (`0xffffff800154a713`–`0xffffff800154a78f`).
   - **OUI 00** (RFC 1042): localiza la interfaz LLW con `getSkywalkInterface(core, 9)` — slot 632
     (+0x13C0) de `AppleBCMWLANCore`, `AppleBCMWLANCore::getSkywalkInterface(uint)`
     (`0xffffff80016b0c66`) — (`0xffffff800154aa4b`); si no existe, «LLW interface is null».
     Corre el header 8 bytes abajo (`0xffffff800154aa59`–`0xffffff800154aa67`: +0→+8, +4→+12),
     avanza headroom +8 y recorta 8 (`0xffffff800154aa72`–`0xffffff800154aa86`): el protocolo del
     SNAP (p. ej. 0x0800) queda como ethertype en +12. El paquete se desvía a la interfaz LLW
     (r13 = interfaz 9, `0xffffff800154aa56`).
4. **Sin SNAP**: exige ethertype ≥ 0x600 (`cmp word ptr [r12+0Ch], 600h` en `0xffffff800154a5b3`)
   y desvía también a la interfaz LLW (`0xffffff800154a5c1`–`0xffffff800154a5e1`; sin LLW,
   «LLW interface does not exist», `0xffffff800154a680`).

Es decir: **sólo las tramas con el SNAP de Apple alcanzan la cola de `awdl0`**; todo lo demás que
llegue por el camino de datos AWDL (OUI 0 o ethertype directo) va a `llw0`.

Cola final (común, `0xffffff800154a5f1` en adelante; se repite en `0xffffff800154a3a8`–
`0xffffff800154a3d2` antes del staging): `setDataOffset(offset)` (slot +0x148 del paquete,
`0xffffff800154a603`), `setDataLength(length)` (slot +0x138, `0xffffff800154a613`),
`setLinkHeaderOffset(offset)` (llamada directa, `0xffffff800154a61f`); pre-hook de la interfaz
**`IO80211VirtualInterface::inputPacket`** (slot +0x0B50, llamada en `0xffffff800154a65c`) con
`(pkt, etiqueta RX, puntero al frame, &flag, 0)`: para role 6 busca el par por SA y descarta las
tramas sin par (contador `incrementNumInputPacketsDropped`), con casos especiales para
`33:33:00:00:00:FB` y `01:00:5E::/16`; retorno 0 → descarte, flag bit 0 → consumido por el hook,
otro → encaje. (El doc `AWDL_PROXIMITY_DATAPATH.md` §10 citaba el hook en el slot +0x8F8; el
desensamblado muestra +0x0B50, y +0x8F8 es `getBSDInterface`.) Después: `setRXTimestamp`
(`0xffffff800154a3fb`), y entrega con
**`AppleBCMWLANPCIeSkywalkRxCompletionQueue::stagePacket(pkt, false, false)`** (slot +0x340 de la
cola, llamada en `0xffffff800154a44c`; habilitada según `IOEventSource::isEnabled`, slot +0x160,
`0xffffff800154a406`), no con `enqueuePackets`. Al agotar el lote: `requestEnqueue(0, 0)` (slot
+0x298) sobre la cola RX de cada una de las 12 interfaces (`0xffffff800154abd1`, tras
`getSkywalkInterface(core, i)` en `0xffffff800154aba5` y `getRxCompQueue` +0xD70 en
`0xffffff800154abb9`), `flushPackets` (+0x330) si falla (`0xffffff800154abef`) y
`scheduleRxCompleteLegacy` para el relleno (`0xffffff800154acfc`).

Formato en la entrada de la cola: **Ethernet II** en `buffer + dataOffset`, `linkHeaderOffset` =
`dataOffset`, longitud = frame completo. La conversión a mbuf para el ifnet BSD la hace la familia a
partir de ese punto. El driver RTL puede usar la misma pareja que la capa STA ya emplea
(`enqueuePackets` +0x2A0/+0x2A8) o el patrón Broadcom (`stagePacket` + `requestEnqueue`); ambos son
puntos de entrada de la misma cola (`?` equivalencia interna no trazada).

## 6. Multicast: duplicado, stash y ventanas (pregunta 4)

Rama multicast de role 6 en `dequeuePackets` (bit 0 de DA a 1, `0xffffff8001543020`):

1. Si el paquete ya lleva el flag 0x80 en `pkt+0x78 → +0x28` (`mov al, [rdx+28h]` en
   `0xffffff8001543020`, `js` hacia `0xffffff800154320b`): se limpia el flag y se transmite
   (inserción del SNAP de §4 y flow queue del DA). Este es el camino de re-alimentación.
2. Si no: `safeMetaCast` de la interfaz a `IO80211VirtualInterface` (`0xffffff8001543040`; sin
   éxito → `0xE00002C0`, «Invalid Virtual Interface for AWDL !»), se pone el flag 0x80 al original
   (`or byte ptr [r12+28h], 80h` en `0xffffff8001543066`), se duplica con
   `AppleBCMWLANPCIeSkywalkPacketPool::duplicatePacket` (`0xffffff800154307e`; pool en ivars+0x18 de
   la cola) y se marca también el duplicado (`0xffffff8001543090`–`0xffffff8001543094`); si no hay
   duplicado se registra y se sigue igual («Could not dup multicast packet for 2nd soc channel»).
   Después `IO80211VirtualInterface::stashMulticastPacketPair(if, original, dup)` (llamada directa
   en `0xffffff80015435be`) y el paquete se da por consumido (ni cola ni liberación).

**Dónde vive el stash**: `IO80211VirtualInterface::stashMulticastPacketPair` (`0xffffff8002334e20`)
sólo reenvía al gestor de pares de la interfaz (vtable +0x318) →
`IO80211AWDLPeerManager::stashMulticastPacketPair` (`0xffffff8002213b16`): dos arrays de 16 entradas
en los ivars del manager — originales en +65816 (contador u32 en +65944) y duplicados en +65952
(contador en +66080); con los arrays llenos el paquete sobrante se libera por
`freeMulticastPacket` de la interfaz (slot +0xF08). La variante base
`IO80211PeerManager::stashMulticastPacketPair` (`0xffffff800225f940`) es un «Not Implemented».

**Quién los transmite**: `IO80211AWDLPeerManager::handleMulticastTimer` (`0xffffff80021c38c4`),
temporizador del manager: si el tiempo restante de la AW actual (`awRemainingTime`) alcanza el umbral
(en ivars+748), saca un array — los duplicados cuando ivars+16128 vale 8, los originales en caso
contrario — y llama a `p2pMulticastTx` de la **interfaz** (slot +0xF18 = `p2pMulticastTx(pkt**, uint)`;
únicos puntos de llamada hallados: `0xffffff80021c39a0` y `0xffffff80021c3a5c`), liberando los
sobrantes con `freeMulticastPacket` (slot +0xF08). Una variante con contador llamada por el flag 0x40
de un objeto colgando del manager (slot +0xF20, `0xffffff80021c3abc`/`0xffffff80021c3adc`) cae en la
base `IO80211VirtualInterface::p2pMulticastTx(uint, bool)` (`0xffffff8002334d98`), que registra
«Function not implemented»: en Broadcom la vía operativa es la de arrays. Los picos de llamada
salieron de un barrido de llamadas con desplazamientos +0xF18/+0xF20/+0xF28 en los tramos
0xffffff8001500000–0xffffff8001800000 y 0xffffff8002100000–0xffffff8002400000.

**El retorno al datapath**: `AppleBCMWLANProximityInterface::p2pMulticastTx` (`0xffffff800172a6d6`)
reenvía al slot +0x368 de la primera cola de envío →
`AppleBCMWLANPCIeSkywalkTxSubmissionQueue::p2pMulticastTx` (`0xffffff8001541d64`) →
`dequeuePackets(this, this, paquetes, n, 0)`: los paquetes stashados, con el flag 0x80, se tratan
como unicast (limpieza de flag, SNAP, flow queue por DA) y salen por el flow ring en la ventana en
curso. La versión de la clase base Broadcom (`0xffffff80015d774a`) es «NOT IMPLEMENTED»: sólo la
subclase PCIe la implementa.

**Vía alternativa del manager** (modelo mbuf): el mismo temporizador puede en su lugar marcar
petición TX (`reqTx = 31` en ivars+2504 y `IO80211PeerManager::requestTxPacket`,
`0xffffff800224dc34`, que avisa al objeto en ivars+0x520 por su slot virtual 398 — nombre `?`, y el
arrastre hasta el driver no se ha trazado `?`); el driver entonces tira con
`dequeueTxPacket(TxPacketRequest*)` (`0xffffff800221675e` en AWDL; valida estado 5, modo de datos y
lock del datapath, y rellena la petición: 8 cadenas de mbuf en +24..+88 con contadores en +8..+24)
→ `IO80211PeerManager::dequeueTxPacket` (`0xffffff8002250554`) → `getMulticastPackets` por la propia
vtable (+0x168, `0xffffff800225076b`) y `getUnicastPackets` (+0x170, `0xffffff80022507bb`). Los
multicast salen de una cola de mbuf del manager (objeto en ivars+0x530, dequeue por slot +0x1C8 en
`0xffffff800224fe4e`), gated por el bit 0x10 de ivars+2504. Quién alimenta esa cola de mbufs es `?`.

**Qué necesita la familia del driver para el multicast**: el stash y el temporizador son de la
familia y funcionan sin cooperación del driver; lo que sí debe proporcionar una interfaz de terceros
que reutilice el `IO80211AWDLPeerManager` son los overrides de interfaz que Broadcom implementa y la
familia deja vacíos: `p2pMulticastTx(pkt**, uint)` (slot +0xF18; la base
`0xffffff8002334d6a` no hace nada), `freeMulticastPacket` (slot +0xF08; debe devolver los paquetes al
pool) y, si se quiere el disparo manual, `triggerAWDLMulticastTx` (slot +0xF10; en Broadcom patea la
flow queue multicast `findExistingFlowQueue(ifId | 0xFFFF0000)`, `0xffffff8001541cfe`). Las marcas
0x80 sobre `pkt+0x78 → +0x28` son convención interna del paquete PCIe; un driver propio puede usar
sus propios marcajes siempre que su `p2pMulticastTx` distinga los paquetes re-alimentados.

## 7. Lo que debe construir y quitar el driver RTL8814

**TX (acción de la cola de envío de `awdl0`)**. Entrada: paquete del pool propio con frame
**Ethernet II** en `base + dataOffset` (64) y `length` = frame. El driver debe:

1. Leer DA (+0), SA (+6), ethertype BE (+12) y payload (+14).
2. Construir la trama USB: cabecera MAC 802.11 de datos (addr1 = DA, addr2 = SA, addr3 = BSSID AWDL
   `00:25:00:FF:94:73` según las capturas externas; QoS/flags exactos `?`) seguida del cuerpo
   `AA AA 03 00 17 F2 08 00 | 03 04 | seq (u16) | 00 00 | ethertype (BE) | payload`.
3. `seq = (contador & 0xFFF) | (AC << 12)`, con un contador u16 **por destino** (Broadcom: por flow
   queue `{interfaz, AC, DA}`, inicializado a 0 y usado antes de incrementarse, de modo que la
   primera trama de un destino lleva 0). El nibble alto con el AC es el diseño Broadcom; replicarlo
   es la opción conservadora (`?` semántica en el receptor Apple).
4. No reproducir el campo de longitud 802.3 de +12: es un artefacto del formato interno
   host↔firmware de Broadcom y desaparece al sustituir la cabecera Ethernet por la 802.11.
5. Multicast: o replicar el modelo stash de Broadcom (duplicar, guardar por canal y transmitir en
   las ventanas), o — sin firmware — transmitir el multicast en las franjas de guarda que indica la
   secuencia de canales; mientras no haya pares, el stash puede aplazarse (etapa 3 de
   `AWDL_ETAPAS_2_A_4.md`).

**RX (entrega a la RxCompletionQueue)**. Entrada: trama 802.11 con FCS desde USB. El driver debe
hacer en software lo que Broadcom reparte entre firmware y host:

1. Validar frame de datos, protocolo 0; longitud de cabecera 24/26 según el bit QoS; DA = addr1,
   SA = addr2 (así lo hace la conversión host de Broadcom, §5.2).
2. Si el cuerpo (tras la cabecera 802.11) empieza por `AA AA 03`: leer 4 bytes en +17 del cuerpo;
   si valen `00 17 F2 08` (constante 0x08F21700) es trama AWDL → quitar los 16 bytes del cuerpo
   (SNAP + `03 04` + seq + `00 00` + ethertype), tomar el ethertype que estaba en +28 del cuerpo y
   dejar `[DA][SA][ethertype][payload]`. Si el OUI es distinto de 00 → descartar («BAD OUI»). Si el
   OUI es 00 → quitar 8 bytes (RFC 1042) promoviendo el protocolo del SNAP a ethertype y dirigir el
   paquete a `llw0` (id 9) cuando exista.
3. Sin SNAP: sólo con ethertype ≥ 0x600, y también hacia `llw0`. No hay tratamiento VLAN en el
   camino Broadcom; si algún día se necesita, sería añadido del driver (`?`).
4. Entregar un paquete del pool con ese Ethernet II en `base + dataOffset`,
   `setDataOffsetAndLength(offset, longitud)`, `setLinkHeaderOffset(offset)` y encolar en la
   RxCompletionQueue (`enqueuePackets` por lotes, como ya hace la capa STA, o `stagePacket` +
   `requestEnqueue(0,0)` por lote como Broadcom). Longitud mínima de trama bien formada antes del
   recorte: 30 (14+16) más payload; Broadcom sólo exige 28.
5. Antes de encolar, el driver llama a `IO80211VirtualInterface::inputPacket` (slot +0x0B50), como
   Broadcom en `0xffffff800154a65c`. Ahí la familia aplica el filtro de pares (SA desconocida →
   descarte) y retiene los paquetes de un par hasta que `wifip2pd` envía el ioctl 415. Los
   retenidos vuelven por los slots +0xF30/+0xF38 de la interfaz, que tiene que implementar el
   driver ([AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §3 y §4). La familia no filtra
   nada que el driver encole sin esa llamada. El contador de secuencia se descarta en RX: no
   requiere estado.

## 8. Verificación de direcciones

Cabezas comprobadas contra la tabla de funciones de la base de datos (inicio de función == dirección
citada): `0xffffff80015420f0` (`dequeuePackets`), `0xffffff8001542006` (`findOrCreateFlowQueue`),
`0xffffff8001541c14` (`freeMulticastPacket` de la cola), `0xffffff8001541cfe`
(`triggerAWDLMulticastTx` de la cola), `0xffffff8001541d64` (`p2pMulticastTx` de la cola),
`0xffffff80015479cc` (`duplicatePacket`), `0xffffff8001549dea` (`rxCompRingDrain`; rango
0xffffff8001549dea–0xffffff800154ad67), `0xffffff800225f940` / `0xffffff8002213b16`
(`stashMulticastPacketPair` base/AWDL), `0xffffff8002216a2e` / `0xffffff8002216a4e`
(`getMulticastPackets`/`getUnicastPackets` AWDL), `0xffffff800221675e` (`dequeueTxPacket` AWDL),
`0xffffff8002250554` (`dequeueTxPacket` base), `0xffffff800224fe4e` (`getMulticastPackets` base),
`0xffffff800224dc34` (`requestTxPacket`), `0xffffff8002333cac` (`inputPacket`),
`0xffffff800233218a` (`logSkywalkTxReqPacket`), `0xffffff8002334d6a`/`0xffffff8002334d98`
(`p2pMulticastTx` base de `IO80211VirtualInterface`, dos firmas), `0xffffff8002334e20`
(`stashMulticastPacketPair`), `0xffffff8002334e44` (`signalMulticastPackets`), `0xffffff8002334cfa`
(`handleMulticastTransmit`), `0xffffff8002334c8a` (`modifyUnicastPacket`), `0xffffff800172a6d6`
(`p2pMulticastTx` de Proxi), `0xffffff800172a3dc` (`triggerAWDLMulticastTx` de Proxi),
`0xffffff800172a6f6` (`freeMulticastPacket` de Proxi), `0xffffff8001553f30`
(`AppleBCMWLANPCIeFlowQueue::withProviderAndQueueInfo`), `0xffffff8002b0baba` (`getTxDataOffset`).
El rango completo de `dequeuePackets` (0xffffff80015420f0–0xffffff8001543b4d) y de `rxCompRingDrain`
está volcado en `Build/ida_awdl/task_dataencap/dis_txsub_dequeue.txt` y `dis_rxdrain.txt`.

Vtables leídas celda a celda: `__ZTV15IOSkywalkPacket` (0xffffff8002b3b510: +0x138
`setDataLength`, +0x140 `getDataLength`, +0x148 `setDataOffset`, +0x150 `getDataOffset`, +0x158
`setDataOffsetAndLength`, +0x178 `getDataVirtualAddress`), vtable de
`AppleBCMWLANPCIeSkywalkPacket` (0xffffff80015ae548: +0x1F0 `getVirtualAddress`
0xffffff8001548498, +0x1F8 `getBufferSize`, +0x200 `setPktEnqueueTime`), vtable de
`AppleBCMWLANCore` (0xffffff80017e97e8: slot 632, +0x13C0 `getSkywalkInterface(uint)`
0xffffff80016b0c66), vtable de `AppleBCMWLANPCIeSkywalkRxCompletionQueue` (slot 104, +0x340
`stagePacket(pkt,bool,bool)` 0xffffff8001546cc8; slot 44, +0x160 `IOEventSource::isEnabled`) y de
`AppleBCMWLANProximityInterface` (slots +0x0B50 `inputPacket`, +0x0B60 `logSkywalkTxReqPacket`,
+0x0D70 `getRxCompQueue`, +0x0F08 `freeMulticastPacket`, +0x0F10 `triggerAWDLMulticastTx`, +0x0F18
`p2pMulticastTx(pkt**,uint)`).

Datos derivados verificados instrucción a instrucción (los que sostienen las tablas de §4 y §5):
escritura del SNAP `0xffffff80015432fd`/`0xffffff8001543307`, `03 04` `0xffffff800154330c`, ceros
`0xffffff8001543314`, ethertype `0xffffff8001543321`, longitud 802.3 `0xffffff80015432f7`,
contador `0xffffff800154332f`–`0xffffff8001543339`, secuencia `0xffffff800154334d`–
`0xffffff8001543358`, stash `0xffffff8001543066`/`0xffffff800154307e`/`0xffffff8001543090`/
`0xffffff80015435be`, flag 0x80 re-alimentado `0xffffff8001543020`/`0xffffff800154321f`/
`0xffffff8001543225`, longitud mínima RX `0xffffff800154a1a8`, flag 0x2 `0xffffff800154a1df`,
conversión 802.11 `0xffffff800154a51d`–`0xffffff800154a58a`, SNAP check `0xffffff800154a20f`–
`0xffffff800154a23d` (constante `0xffffff800154a238`), strip 16 `0xffffff800154a248`–
`0xffffff800154a271`, BAD OUI `0xffffff800154a713`–`0xffffff800154a78f`, OUI 0 `0xffffff800154aa40`–
`0xffffff800154aa86`, ethertype ≥ 0x600 `0xffffff800154a5b3`, desvío LLW `0xffffff800154a5c1`–
`0xffffff800154a5e1`, cola final `0xffffff800154a5f1`–`0xffffff800154a65c` y `0xffffff800154a3a8`–
`0xffffff800154a406`, `stagePacket` `0xffffff800154a44c`, cierre de lote `0xffffff800154ab9c`–
`0xffffff800154abef` y `0xffffff800154acfc`. Llamadas con desplazamientos de vtable +0xF18/+0xF20
desde `handleMulticastTimer`: `0xffffff80021c39a0`, `0xffffff80021c3a5c`, `0xffffff80021c3abc`,
`0xffffff80021c3adc`.

## 9. Puntos abiertos (`?`)

1. Interior del firmware Broadcom: construcción de la cabecera 802.11 en TX (QoS, tasas) y qué
   coloca en los bytes entre el headroom del pool y el frame en RX (campo data offset del mensaje).
2. Orden de bytes del campo `seq` en el aire y semántica del nibble alto (AC en Broadcom) para un
   receptor Apple; las capturas externas registran un u16 creciente por nodo.
3. Objeto en ivars+0x520 del manager que recibe el aviso de `requestTxPacket` (slot virtual 398) y
   quién llama a `dequeueTxPacket` en Broadcom; el flag 0x40 (byte +2885 de ese objeto) que decide
   entre la variante por contador y la por arrays de `handleMulticastTimer`.
4. Quién alimenta la cola de mbufs multicast del manager (ivars+0x530) que drena
   `getMulticastPackets`.
5. Comportamiento completo de `inputPacket` para los DA especiales `33:33:00:00:00:FB` y
   `01:00:5E::/16` (rama trazada sólo a grandes rasgos).
6. Equivalencia interna entre entregar por `stagePacket`+`requestEnqueue` (Broadcom) y por
   `enqueuePackets` directo (capa STA RTL): ambos son puntos de entrada de la misma cola, pero la
   maquinaria común no se ha volcado.
7. Flags exactos de la cabecera 802.11 que exige un par Apple en TX (QoS, A-MSDU) — remite a las
   capturas de `AWDL_REFERENCIAS_EXTERNAS.md` §5.
