# El pre-hook `inputPacket` de `awdl0`: filtro de pares y retención PPP antes de encajar

Cierra el R4 de `AWDL_DATA_PATH_REVIEW.md` y la receta §8.6 de `AWDL_RX_DATA_TO_SKYWALK.md`: cómo
`RTLAwdInterface::deliverData` (`Source/AirLayer/AWDLInterface.cpp`) debe llamar al pre-hook
`IO80211VirtualInterface::inputPacket` de la propia interfaz antes de encajar cada paquete Ethernet en
la `RxCompletionQueue`, igual que hace Broadcom en `AppleBCMWLANPCIeSkywalk::rxCompRingDrain`. Todo lo
citado está verificado en volcados headless de las IDB de los tres builds (25G241, 24H232, 23J631);
lo no verificado lleva `?`. Volcados: `Build/ida_awdl/task_rx_inputpacket/out_{25G241,24H232,23J631}.txt`.

## 1. Resumen

El slot del pre-hook es `+0xB50` en 26 (25G241) y `+0xB48` en 15 (24H232) y 14 (23J631). La
implementación base de la familia (`IO80211VirtualInterface::inputPacket`) aplica para rol 6 el filtro
de pares AWDL sobre la SA de la cabecera Ethernet (tercer argumento, no la etiqueta), descarta sin par,
deja pasar los destinos de grupo con casos especiales, y delega el encaje en
`IO80211PeerManager::skywalkInputPacket`, que es quien puede retener el paquete (bit del `bool*`) a la
espera del ioctl 415. `inputPacket` nunca libera el paquete: el que devuelve `false` lo libera el
llamador, el retenido pasa a ser propiedad de la cola PPP, y el aceptado lo encaja el llamador. El
símbolo está exportado en 15 y 26 pero **no en 14**, así que la única llamada portable es la virtual
por la vtable de la interfaz, que es además la que usa Broadcom.

## 2. Firma y ranura por build

| build | ZTV `__ZTV23IO80211VirtualInterface` | ranura | función | mangled |
|---|---|---|---|---|
| 26 (25G241) | `0xffffff8002472b78` | `+0xB50` (celda `0xffffff80024736d8`) | `0xffffff8002333cac` | `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPbb` |
| 15 (24H232) | `0xffffff80023df348` | `+0xB48` | `0xffffff80022a9742` | idéntico al de 26 |
| 14 (23J631) | `0xffffff80022dbb88` | `+0xB48` | `0xffffff80021dc34e` | `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPb` |

C++ (26 y 15): `uintptr_t inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool)`;
C++ (14): la misma **sin el `bool` final** — 4 parámetros. Registro a registro (verificado en los tres
desensamblados): `rsi` = paquete, `rdx` = etiqueta (`packet_info_tag*`), `rcx` = cabecera Ethernet
(`ether_header*`), `r8` = `bool*` de retención (en 14 es el último argumento), `r9` = `bool` (solo 15/26).
El valor de retorno se prueba sobre `AL` en el llamador (`test al,al`); `true` = continúa, `false` = el
llamador libera. Las 9 referencias de datos a la función en cada build son celdas de vtables de la
familia y de las subclases Broadcom; no hay llamadas directas.

La cabecera de la capa (`Source/AirLayer/AWDLInterface.h:114`, con la firma de 5 argumentos y el
comentario `0xB50`) sigue el layout de 26; la portabilidad del layout a 14/15 (ranura `+0xB48`, un
slot menos) es el trabajo de `AIRLAYER_PORT_14_15.md`.

## 3. La etiqueta `packet_info_tag`: lo que Broadcom llena antes de la llamada

La etiqueta que Broadcom pasa **no es un buffer del llamador**: es el *scratch* por paquete,
`PacketSkywalkScratch` en `[paquete+0x78]` (26: `0xffffff800154a04b`; 24: `0xffffff80014da6f3`;
23: `0xffffff80014849cf`). No hay `memset`: se rellenan campos concretos desde el mensaje de
completado (`BCOMIPCRxCompleteMessageV7*`), con los mismos offsets en los tres builds:

| campo | escritura | 26 | 24 | 23 |
|---|---|---|---|---|
| timestamps (`getRxTimeStamp(tag, msg, getItemSize()==0x28)`) | llamada | `0xffffff800154a06d` | `0xffffff80014da715` | `0xffffff80014849f4` |
| `+0x00` u16 ← `(int8)msg[0x1C]` (**canal**) | store | `0xffffff800154a095` | `0xffffff80014da73d` | `0xffffff8001484a1d` |
| `+0x0C` u32 ← `ratespec2rate(msg u32 en +0x14)` (**rate**) | store | `0xffffff800154a08d` | `0xffffff80014da735` | `0xffffff8001484a14` |
| `+0x18` u8 ← `msg[0x12] >> 5` (**RSSI**) | store | `0xffffff800154a079` | `0xffffff80014da721` | `0xffffff8001484a00` |
| `+0x28` u8 ← bit 24 de `msg[0x1C]` (dword) | store | `0xffffff800154a0a8` | `0xffffff80014da750` | `0xffffff8001484a22` |

`ratespec2rate` (26 `0xffffff800165D3A2`, 24 `0xffffff80015ED6C2`, 23 `0xffffff80015AEB47`) y
`getRxTimeStamp` (26 `0xffffff800154DAF4`, 24 `0xffffff80014DE148`, 23 `0xffffff8001488234`) escriben
los timestamps en campos de la etiqueta no determinados aquí (`?`). El tamaño total de
`packet_info_tag`/`PacketSkywalkScratch` tampoco está establecido; el byte de flags que el llamador
limpia al liberar está en `+0x90` (26/24) y `+0xB8` (23), así que la estructura excede con holgura los
`+0x28` escritos aquí (`?`).

**Corrección a `AWDL_RX_DATA_TO_SKYWALK.md` §6** (y al arreglo propuesto en R4): la descripción «byte 0
marca multicast (bit 0) y `+6..+11` trae la SA» corresponde a la **cabecera Ethernet del tercer
argumento** (`ether_header*`), no a la etiqueta. En los tres builds la familia lee el byte multicast y
la SA del `ether_header*` (`dhost[0]`, `ether+6`), y la etiqueta sólo lleva metadatos de radio (canal,
rate, RSSI, timestamps). `packet_info_tag` no aparece en los tipos locales de ninguna de las tres IDB
(4959 ordinales revisados en 25G241); el layout de arriba es el que se deduce de las escrituras del
llamador.

Los dos sitios de llamada de `rxCompRingDrain`:

1. **Interfaz de datos NaN (rol 10)**: interfaz = `getSkywalkInterface(core, 4)` (26:
   `0xffffff800154a2bf`), `bool* = NULL` (26/24: `xor r8d,r8d` en `0xffffff800154a31f` /
   `0xffffff80014da9c1`; 23: idem en `0xffffff8001484dc5`), llamada en `0xffffff800154a31f` /
   `0xffffff80014da9c7` / `0xffffff8001484dc8`.
2. **AWDL (rol 6)**: interfaz = `getSkywalkInterface(core, 9)` — la interfaz LLW de Broadcom; si es
   nula, `IOLog("%s[%d] ERROR : LLW interface does not e...")` (26: `0xffffff800154a5cc` y cadena en
   `0xffffff80015995E2`; 24: `0xffffff80014dad28`; 23: `0xffffff8001484fea`, línea 5F8). La cabecera
   Ethernet se forma como `getVirtualAddress() + getDataOffset()` del paquete (26:
   `0xffffff800154a624`–`0xffffff800154a642`; 23, patrón idéntico en `0xffffff8001484db0`–`b3`).
   `bool*` = `&var_35`, inicializado a **`0xAA`** en la rama LLC/SNAP (26 `0xffffff800154a19c`, 24
   `0xffffff80014da844`, 23 `0xffffff8001484b35`) o a **`0x00`** en la rama Ethernet cruda (26
   `0xffffff800154a5f1`, 24 `0xffffff80014dac99`, 23 `0xffffff8001484f5e`); el quinto `bool` va a 0 en
   los dos sitios (`xor r9d,r9d`). Llamada: **`0xffffff800154a65c`** (26), `0xffffff80014dad04` (24),
   `0xffffff8001484fc6` (23).

## 4. El `bool*` de retención y el `bool` final

- **`bool*` (4.º argumento)**: `inputPacket` no lo escribe nunca; lo reenvía sin tocar como 7.º
  parámetro de `skywalkInputPacket` (26: `push [rbp+var_58]` en `0xffffff8002333de2`). La rama rol 7
  pasa `NULL` (26: `xor r8d,r8d` en `0xffffff8002333d19`). El llamador lo inicializa antes y prueba el
  bit 0 después de la llamada (26: `test [rbp+var_35],1` en `0xffffff800154a66a`; 24: `cmp …,0` en
  `0xffffff80014dad0e`; 23: `cmp …,0` en `0xffffff8001484fd4`): a 1 ⇒ el paquete quedó **retenido** por
  la pila llamada y el llamador no lo libera ni lo encaja. La escritura real ocurre dentro de
  `skywalkInputPacket` (cuerpo no analizado en ningún build, `?`), que es donde vive la retención para
  el registro de tráfico liberado por el ioctl 415 (`AWDL_PEER_PRESENCE_PPP.md` §2–§4: cola PPP,
  `+0xF30`/`+0xF38`, ageout de 2000 ms).
- **`bool` final (5.º argumento, solo 15/26)**: no condiciona nada dentro de `inputPacket`; se
  reenvía como 8.º parámetro de `skywalkInputPacket` (26: lectura en `0xffffff8002333dce`). Ambos
  llamadores RX de Broadcom pasan 0. Su efecto aguas abajo es `?`.

## 5. Rutas de retorno y propiedad del paquete

`inputPacket` no libera nunca el paquete (sus únicas llamadas: `logRxCompletionPacket` del monitor
`[ctrl+0x40]`, búsqueda de par, `safeMetaCast`, `logDebug`, `incrementNumInputPacketsDropped`,
`skywalkInputPacket`). Tres salidas:

1. **Sin `IO80211PeerManager`** (`[contenedor+0x28]` == 0; 14: `+0x30`) → devuelve **`true`** (26:
   `0xffffff8002333d2c`–`d2f`; 23: epílogo en `0xffffff80021dc3ca`).
2. **`false` (descarte)**: sin par (§6) o caída silenciosa de los destinos especiales (§6). En todos
   los builds el epílogo pone `eax=0` (26: `xor r12d,r12d` en `0xffffff8002333f11`/`f5d`/`e54`). El
   **llamador** libera: contador de descartes `++` (26: `[ [this+0x10]+0x150 ]++`; 24: idem en
   `0xffffff80014dad4b`; 23: `[ [var_50]+0x158 ]++` en `0xffffff800148500c`, celda desplazada, `?`),
   limpia el bit 0x80 del flag del scratch (26/24: `[tag+0x90]`; 23: `[tag+0xB8]`,
   `0xffffff800148501b`) y llama a su callback de liberación con `(pool, paquete)` — pool =
   `[ [contenedor]+0x38 ]`, callback `var_98` = `[[g_0xffffff80015AC170]+0x150]` (26:
   `0xffffff800154a437`/`a4a6`; 24: `0xffffff80014daadf` vía `0xffffff80014dad65`; 23:
   `0xffffff8001485025`).
3. **`true` con bit 0 del `bool*` a 1 (retención)**: el llamador sólo incrementa su contador de
   retenidos (26: `[…+0x148 ]++` en `0xffffff800154a47b`) y pasa al siguiente mensaje del anillo: ni
   libera ni encaja. La propiedad quedó en la cola PPP de espera del 415 (§4); su liberación posterior
   es la de `AWDL_PEER_PRESENCE_PPP.md` §3–§4.
4. **`true` con bit 0 a 0 (pase)**: el llamador obtiene la cola (`getRxCompQueue`, slot `+0xD70` de la
   interfaz; 23: `+0xD78`, `0xffffff8001484c21`) y encaja con `stagePacket(pkt, false, false)`
   (`+0x340`, 26: `0xffffff800154a44c`; 23: `0xffffff8001484d1e`) + `requestEnqueue` al fin del lote;
   si la cola no es de la clase PCIe, libera con el mismo callback.

Despacho por rol dentro de `inputPacket` (rol leído de `[ [this+contenedor]+0x58 ]` dword; 14:
`+0x50`; contenedor = `this+0x120` en 26, `this+0x110` en 15/23): rol 7 → cola a la ranura `+0x230` de
la vtable del PeerManager (26: `jmp rax` en `0xffffff8002333d2a`; 23: `+0x230` en `0xffffff80021dc3d4`;
15: **`+0x228`**, `0xffffff80022a97c0`) — método sin nombre en los volcados (`?`). Rol ≠ 6 → pase
directo a `skywalkInputPacket`, con el 6.º argumento = `rol == 2` (26: `setz` en `0xffffff8002333dc6`;
23: `0xffffff80021dc46a`) y el par de la búsqueda virtual. Rol 6 → filtro completo (§6).

La delegación final en todos los builds es
`IO80211PeerManager::skywalkInputPacket(IO80211NetworkPacket*, IO80211Peer*, packet_info_tag*, ether_header*, bool, bool, bool*, bool)`
(14: sin el `bool` final; 7 parámetros): 26 `0xffffff800225D554` (llamada `0xffffff8002333de7`, con
`rdx` = par de la búsqueda o `[[peerMgr+0x18]+0x530]` si multicast, `0xffffff8002333db1`–`b5`), 24
`0xffffff80021F2FEA` (llamada `0xffffff80022a987d`), 23 `__ZN18IO80211PeerManager18skywalkInputPacketEP20IO80211NetworkPacketP11IO80211PeerP15packet_info_tagP12ether_headerbbPb`
`0xffffff8002143C1E` (llamada `0xffffff80021dc480`).

## 6. El filtro de pares (rol 6) y los destinos especiales

Búsqueda del par por la **SA del `ether_header*`** (`ether+6`): primero la ranura virtual `+0x120` del
PeerManager (26: `0xffffff8002333d4e`; 24: `0xffffff80022a97e4`; 23: `0xffffff80021dc3ec`) y, si role 6
y sigue nulo, `IO80211PeerManager::findPeer(uchar*)` (26 `0xffffff8002250FE0`, llamada
`0xffffff8002333d6d`; 24 `0xffffff80021E6E50`, llamada `0xffffff80022a9803`; 23 `0xffffff80021355F2`,
llamada `0xffffff80021dc40b`). Sin par → descarte con log
`"%s: Dropping %s input packet because no peer was found for %02X:%02X:%02X:%02X:%02X:%02X\n"`
("Unicast"/"Multicast" según `dhost[0]&1`; `logDebug` con máscara `0x800000000000000`, 26:
`0xffffff8002333f2c`, cadena `0xffffff800242EC62`; 24: `0xffffff80022a99c2`; 23:
`0xffffff80021dc5c4`), `incrementNumInputPacketsDropped` sobre el `IO80211AWDLPeerManager`
(26 `0xffffff800221EA84`, llamada `0xffffff8002333f58`; 24 `0xffffff80021B87A0`, llamada
`0xffffff80022a99ee`; 23 `0xffffff8002108762`, llamada `0xffffff80021dc5f0`) y `return false`.

Con par, los destinos de grupo se someten a comprobaciones AWDL extra antes del pase (comparaciones
sobre `dhost`, idénticas en los tres builds; los fallos van al pase, los aciertos a la cadena especial):

- **`33:33:00:00:xx:xx`**: `dhost[0]==0x33` (26 `0xffffff8002333d7e`, 24 `0xffffff80022a9814`, 23
  `0xffffff80021dc41c`), `dhost[1]==0x33` (26 `0xffffff8002333dfb`, 24 `0xffffff80022a9891`, 23
  `0xffffff80021dc494`), `dhost[2]==0` (26 `0xffffff8002333e15`, 24 `0xffffff80022a98ab` vía
  `test edx,edx`, 23 `0xffffff80021dc4ae`), `dhost[3]==0` (26 `0xffffff8002333e19`, 24
  `0xffffff80022a98af`, 23 `0xffffff80021dc4b2`).
- **`33:33:80:00:00:FB` exacto**: `dhost[2]==0x80` (26 `0xffffff8002333e09`, 24
  `0xffffff80022a989f`, 23 `0xffffff80021dc4a2`) + `dhost[3]==0 ∧ dhost[4]==0 ∧ dhost[5]==0xFB` (26
  `0xffffff8002333f65`–`f79`, 24 `0xffffff80022a99fb`–`a13`, 23 `0xffffff80021dc5fd`–`60b`).
- **`01:00:5E:00:xx:xx`**: `dhost[0]==1` (26 `0xffffff8002333d83`, 24 `0xffffff80022a9819`, 23
  `0xffffff80021dc421`), `dhost[1]==0` (26 `0xffffff8002333d8c`, 24 `0xffffff80022a9822`, 23
  `0xffffff80021dc42a`), `dhost[2]==0x5E` (26 `0xffffff8002333d92`, 24 `0xffffff80022a9828`, 23
  `0xffffff80021dc430`), `dhost[3]==0` (mismo punto que arriba).

Cadena especial (tras los casts `safeMetaCast` a `IO80211AWDLPeerManager` y `IO80211AWDLPeer`; ambos
nulos ⇒ `return false`): cualquier condición fallida ⇒ `return false` **sin log ni contador**; todas
cumplidas ⇒ pase a `skywalkInputPacket`.

| comprobación | 26 | 24 | 23 |
|---|---|---|---|
| cast gestor (gMetaClass `0xffffff800248D2E0`/`0xffffff80023F8FE0`/`0xffffff80022EFA10`) | `0xffffff8002333e38` | `0xffffff80022a98c4` | `0xffffff80021dc4c3` |
| cast par (gMetaClass `0xffffff800248D060`/`0xffffff80023F8D60`/`0xffffff80022EF790`) | `0xffffff8002333e4f` | `0xffffff80022a98d7` | `0xffffff80021dc4d6` |
| objeto del mapa del gestor: nulo ⇒ descarta | `[[awdlMgr+0x20]+0xA5C8]` nulo, `0xffffff8002333e7b` | `[[awdlMgr+0x20]+0x7EA8]` nulo, `0xffffff80022a9907`–`e` | `[[awdlMgr+0x20]+0x7E60]` nulo, `0xffffff80021dc506`–`10` |
| dword del objeto == 0 ⇒ descarta | `[[obj]+0x10]+0x24`, `0xffffff8002333e89` | `[obj+0x24]`, `0xffffff80022a991b` | `[[obj]+0x10]+0x24`, `0xffffff80021dc51a` |
| byte del subobjeto del par (`[[awdlPeer+0x20]+0x…`), 0 ⇒ descarta | `+0x6DE` bit 0, `0xffffff8002333e93` | `+0x6AE == 1`, `0xffffff80022a9929` | `+0x6AE == 0`, `0xffffff80021dc528` |
| estado del gestor: bit 3 de `[X+0x168]` puesto y `[X+0x3A8]` ≠ 0 (26: `+0x3B0`), con `X = [[awdlMgr+0x18]+0x518/0x520]+0x120` | `0xffffff8002333eb9`–`f98` | `0xffffff80022a9948`–`a2e` | sin equivalente |
| bit 6 de `[[awdlPeer+0x20]+0x5B0]` (26) / `+0x588` (24/23) ⇒ descarta | `0xffffff8002333fa5` | `0xffffff80022a9a34` | `0xffffff80021dc54a`, solo si `supportAWDLToNANMigration()` ≠ 0 (`0xffffff80020FC522`, llamada `0xffffff80021dc538`) |

En 14 la tercera condición se sustituye por
`IO80211AWDLPeerManager::supportAWDLToNANMigration() ∧ bit 6 de [[awdlPeer+0x20]+0x588]`
(`0xffffff80021dc538`–`55f`); no hay cadena `[awdlMgr+0x18]→+0x518→+0x120/+0x168/+0x3A8`. El byte
`+0x6DE/0x6AE` es el marcador de presencia ya conocido (`AWDL_WIFIP2PD_PRESENCE_ONCE.md`: escritura a 1
cuando el par publica presencia).

En 26, el objeto de `ivars+0xA5C8` es la cola de servicios AWDL registrados localmente: un
`IO80211CommandQueue::withGateAndCapacity(gate, 0x80, 0x100)` creado en
`IO80211AWDLPeerManager::initWithInterface` (`0xffffff80021c2398`, guardado en `0xffffff80021c23a1`), y el
dword `[[obj]+0x10]+0x24` es lo que devuelve `getRegisteredServicesCount` (`0xffffff80021dc598`). Sus
otros usuarios son `updateLocalRequests`, `buildServiceResponseTlvs` y
`buildMasterIndicationTemplate`. La identidad de `+0x7EA8`/`+0x7E60` en 15/14 se presume la misma por
posición `?`.

**Consecuencia para Bonjour.** El mDNS IPv6 (`33:33:00:00:00:FB`) cae en el primer caso y el IPv4
(`01:00:5E:00:00:FB`) en el tercero. Con el pre-hook activo, el mDNS de un par conocido pasa solo si
este Mac tiene al menos un servicio AWDL registrado (lo que ocurre cuando sharingd publica
`_airdrop._tcp` con `IncludeAWDL`, `AWDL_AIRDROP_SIN_BLUETOOTH.md` §3) y el par tiene el bit 0 de
`+0x6DE`; si no, se descarta sin log ni contador. Sin el pre-hook, como hoy, ese tráfico pasa siempre.
Activar el pre-hook antes de comprobar en hardware que el bit de presencia del par se pone puede
cortar el descubrimiento de AirDrop.

## 7. ¿Símbolo o vtable?

`kcsym.py` sobre las symtabs de los tres KC, con controles positivos en 23 (`findPeer`, `logDebug`,
`setLinkHeaderOffset` = `0xf`):

| mangled | 26 (25G241) | 15 (24H232) | 14 (23J631) |
|---|---|---|---|
| `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPbb` | `0xf` (exportado) | `0xf` (exportado) | — |
| `…ether_headerPb` (4 argumentos) | — | — | **MISSING** |

En 26 y 15 un kext puede enlazar el símbolo por nombre; en 14 el nombre no existe en la symtab (con
controles exportados en el mismo KC) y **sólo se puede llamar por la vtable**. Además, la llamada por
superclase calificada (`IO80211VirtualInterface::inputPacket(...)`) emitiría una referencia
indefinida que no se resuelve en 14. La única forma portable de llamar al pre-hook es la virtual por la
propia interfaz, que es exactamente lo que hace Broadcom (`call [rax+0B50h]`/`[rax+0B48h]`).

En el objeto de la capa (`Build/obj_airlayer/AWDLInterface.o`) la celda `+0xB50` de
`__ZTV15RTLAwdInterface` ya referencia la implementación base del kernel: la reloc
`__ZN23IO80211VirtualInterface11inputPacket…` cae en `__DATA,__const+0xB70` = dirección del objeto
`0x8228` = `ZTV+0x10+0xB50` (las relocs vecinas de la sección, `+0xB78` y `+0xB80`, son
`forwardInfraRelayPackets` y `logSkywalkTxReqPacket`, slots 365/366 de la familia). El override
declarado en
`AWDLInterface.h:114` sin cuerpo hace que la celda herede la base; el símbolo de
`RTLAwdInterface::inputPacket` no existe en el objeto. Hoy, pues, una llamada virtual `inputPacket(…)`
sobre `this` ejecuta el filtro de la familia — el mismo comportamiento que tendría Broadcom — y si la
clase llegara a definir el override, la llamada seguiría el despacho de la vtable.

## 8. Código para `RTLAwdInterface::deliverData`

Sustituir el tramo final del bucle de subtramas (`Source/AirLayer/AWDLInterface.cpp`, tras
`rtl_netpkt_setLinkHdrLen(pkt, 14)` y antes de `pkts[n++] = pkt;`) por:

```c
		rtl_netpkt_setLinkHdrLen(pkt, 14);
		/* Pre-hook +0xB50/+0xB48 (IO80211VirtualInterface::inputPacket): the
		 * family applies the AWDL peer filter and may hold the packet for the
		 * presence gate (ioctl 415), exactly as Broadcom at
		 * 0xffffff800154a65c (Docs/AWDL_RX_INPUTPACKET_HOOK.md). The family
		 * reads the addresses from the Ethernet header; the tag only carries
		 * radio metadata (channel +0x00, rate +0x0C, RSSI +0x18 in the
		 * Broadcom scratch), which this path no longer has once the RX meta
		 * is stripped, so it stays zeroed. Called through the vtable: the
		 * symbol is not exported on macOS 14 and the qualified base call
		 * would not link there. */
		uint8_t tag[0x100];
		bzero(tag, sizeof(tag));
		bool held = false;
		uintptr_t ok = inputPacket((IO80211NetworkPacket *)pkt, (packet_info_tag *)tag,
		                           (ether_header *)e, &held, false);
		if (!ok) {
			rtl_pool_free(_rxPool, pkt);
			_rxDataNotUs++;
			continue;
		}
		if (held)
			continue; /* owned now by the PPP wait queue (ioctl 415) */
		pkts[n++] = pkt;
```

Notas del código:

- `inputPacket(...)` sin calificar es la llamada virtual por `this` (despacho por la celda del
  vtable, hoy la base de la familia); coincide con el patrón Broadcom y evita la referencia de símbolo
  que no enlaza en 14.
- El quinto argumento `false` replica lo que Broadcom pasa en sus dos sitios RX (§4).
- La retención (`held`) sólo puede venir de `skywalkInputPacket` (§4); hasta que la clase no
  implemente `+0xF30`/`+0xF38` (`AWDL_PEER_PRESENCE_PPP.md` §3: la base responde "Not Implemented"), un
  paquete retenido no tendría ruta de liberación posterior — riesgo de fuga que se cierra con esos dos
  overrides (etapa de presencia).
- Etiqueta de 0x100 bytes a cero. Cubre los campos que Broadcom escribe (`+0x00`, `+0x0C`, `+0x18`,
  `+0x28`) y deja margen. `skywalkInputPacket` lee la etiqueta hasta `+0x2A` (entre ellos el u16 de
  `+0x22`) y la pasa a `IO80211PeerMonitor::incrementRxExtStats`, cuyas lecturas no están acotadas.
  El tamaño real de `packet_info_tag` es `?` (§3). Las lecturas de campos a cero que hagan
  `logRxCompletionPacket`/`skywalkInputPacket` se presumen inocuas (`?`).
- No activar el pre-hook antes de tener la cadena de presencia: el filtro de grupo del §6 puede
  descartar el mDNS del par. Si se activa antes para probar, conviene hacerlo detrás de un boot-arg. Propagar el
  RSSI y el canal de `injectFrame` (hoy se pierden al entrar en `deliverData`) dejaría la etiqueta
  idéntica a la de Broadcom; requiere pasar ambos valores como parámetros.

## 9. No verificado (`?`)

1. Cuerpo de `IO80211PeerManager::skywalkInputPacket` en los tres builds: escritor real del `bool*`,
   encolado en la cola PPP, significado del `bool` final y lecturas que hace de la etiqueta. VAs: §5.
2. Tamaño total de `packet_info_tag`/`PacketSkywalkScratch` y los offsets de los timestamps que escribe
   `getRxTimeStamp`.
3. Campos que lee `IO80211InterfaceMonitor::logRxCompletionPacket` (sólo corre si `[ctrl+0x40]` ≠ 0)
   de una etiqueta a cero.
4. Método de la ranura `+0x230` (26/23) / `+0x228` (15) de la vtable del PeerManager a la que el rol 7
   transfiere la llamada.
5. Identidad de `+0x7EA8`/`+0x7E60` en 15/14 (en 26, `+0xA5C8` es la cola de servicios registrados, §6).
6. Contador de descartes del llamador en 23: celda `+0x158` sobre `[var_50]` frente a `+0x150` sobre
   `[var_50]+0x10` en 24/26 — desplazamiento de layout no interpretado.
7. Que en hardware el `awdl0` de la capa tenga el rol 6 en `[ [this+0x120]+0x58 ]` (26) — sin él el
   filtro no se aplica (pendiente de las pruebas T).
