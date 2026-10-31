# Offsets de presencia y PPP del stack AWDL: macOS 14.8.9 (23J631) y 15.8.2 (24H232)

Port de cada offset de ivars, ranura de vtable, entrada de tabla de handlers y umbral que citan
[`AWDL_PRESENCE_PREREQS.md`](AWDL_PRESENCE_PREREQS.md), [`AWDL_PEER_PRESENCE_PPP.md`](AWDL_PEER_PRESENCE_PPP.md)
y [`AWDL_PEERMANAGER_CREATION.md`](AWDL_PEERMANAGER_CREATION.md) para 25G241 (macOS 26.7.1) a los
kernelcaches de 23J631 y 24H232, ambos x86_64. Cada fila se resolvió primero por símbolo en los tres
builds y después se leyó la instrucción o la celda equivalente en el desensamblado de la misma función.
Los datos de intercambio quedan en [`Tools/awdl_presence_offsets.tsv`](../Tools/awdl_presence_offsets.tsv)
(columnas `name, kind, va_25G241, value_25G241, va_23J631, value_23J631, va_24H232, value_24H232, note`).
Lo no comprobado va con `?`. Las 101 filas con build del TSV (34 en 25G241, 33 en 23J631 más la
ausencia de `getController`, 34 en 24H232) se re-verificaron una a una contra los tres IDB: cabeza de
instrucción con el inmediato o desplazamiento citado, celda igual a `ZTV + 0x10 + ranura` con el
método esperado, y entrada de `gSetHandlerTable` igual al handler.

## 1. Método

- Volcados de símbolos por build (`Docs/re/io80211_{25.6.0,23J631,24H232}/syms.txt`): las 29 funciones
  de la cadena existen con el mismo nombre mangleado en las tres versiones; ninguna fila se resolvió
  por parecido de bytes.
- Desensamblado instrucción a instrucción de esas funciones y lectura de celdas (`__ZTV15IO80211AWDLPeer`,
  `__ZTV11IO80211Peer`, `__ZTV17IO80211Controller`, `__ZTV23IO80211VirtualInterface`, `gSetHandlerTable`,
  `gSetHandlerTableLegacy`) con IDA 9.4 headless (`idat -A -S`) sobre copias de los tres IDB en `/tmp`.
- Ninguna fila depende de tablas de salto: los accesos citados son cargas/stores con desplazamiento
  directo, llamadas por celda o comparaciones de inmediato. La única tabla indexada cercana (nombres de
  modo en la log de `interfaceStateChange`, `cmp rax, 3; jmp [tabla+rax*8]`) se revisó y no afecta a
  ninguna fila. Cada ranura de vtable se leyó de la celda (`celda = ZTV + 0x10 + ranura`), nunca por
  recuento.
- Los VA citados son cabezas de instrucción o celdas de cada build; cambian entre versiones y solo
  acreditan el hallazgo.

## 2. Resumen de deltas

| Bloque | Campo | 25G241 | 23J631 | 24H232 |
|---|---|---|---|---|
| Ivars AWDL del manager (`this+0x20`) | `LLAValid` | `+0xFF1C` | `+0xC4A0` | `+0xC70C` |
|  | `AWDLOn`/stateFlags | `+0x3EE0` | `+0x3EC8` | `+0x3EE8` |
|  | `_bonjourTriggersAWDL` | `+0xA718` | `+0x7FA8` | `+0x7FF8` |
|  | timer LLA | `+0x28A0` | `+0x3EB8` | `+0x3ED8` |
|  | `channelSequenceStrategy` (dword) | `+0x4` | `+0x4` | `+0x4` |
|  | campo acompañante del modo manual | `+0xA66C` | `+0x7F04` | `+0x7F4C` |
| Ivars AWDL del par (`this+0x20`) | flag PPP done | `+0x6DE` | `+0x6AE` | `+0x6AE` |
|  | marca del retorno temprano | `+0x310` | `+0x2E8` | `+0x2E8` |
| Vtable de `IO80211AWDLPeer` | `monitorPeer` | `+0x180` | `+0x180` | `+0x178` |
| Vtable de la vif | `inputPacket` | `+0xB50` | `+0xB48` | `+0xB48` |
|  | `sendPendingPacketsToStack` | `+0xF30` | `+0xE80` | `+0xF10` |
|  | `freePendingPackets` | `+0xF38` | `+0xE88` | `+0xF18` |
|  | `createPeerManager` | `+0xB08` | `+0xB08` | `+0xB00` |
|  | `getP2PSkywalkPeerMgr` | `+0xCC0` | `+0xCC0` | `+0xCB8` |
|  | `handleIoctl` | `+0xE80` | `+0xE08` | `+0xE78` |
|  | `getController` | `+0xBA8` | **ausente** | `+0xBA8` |
| Vtable de `IO80211Controller` | `executeCommand` | `+0x850` | `+0x850` | `+0x850` |
| Ivars de la vif (`IO80211SkywalkInterface`) | puntero de ivars (`this`) | `+0x120` | `+0x110` | `+0x110` |
|  | peer manager | `+0x28` | `+0x30` | `+0x28` |
|  | controlador | `+0x30` | `+0x38` | `+0x30` |

La deriva vuelve a ser no monótona: `sendPendingPacketsToStack`/`freePendingPackets` caen 0xB0 en
23J631 y recuperan 0x70 en 24H232 sin volver al valor de 25G241; `monitorPeer`, `createPeerManager`,
`getP2PSkywalkPeerMgr` e `inputPacket` solo se mueven en 24H232. Ninguna ranura puede deducirse de la
versión contigua.

## 3. Ivars del `IO80211AWDLPeerManager`

En los tres builds el bloque AWDL cuelga de `this+0x20` (`mov r13, [rbx+20h]` en 25G241,
`lea rax, [r15+20h]`/`mov rcx, [r15+20h]` en 23J631 y 24H232): la base no se movió; se movieron los
campos dentro del bloque.

### 3.1 `LLAValid` (byte; `PM+65308` en 25G241)

| Escritor | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `interfaceStateChange`, store 1 | `0xffffff80021d9b39` | `0xffffff80020c93db` | `0xffffff8002177870` |
| `interfaceStateChange`, store 0 | `0xffffff80021d9b44` | `0xffffff80020c93e7` | `0xffffff800217787b` |
| `monitorAWDLState`, copia de `isLLAConfigured` | `0xffffff80021ed932` | `0xffffff80020dad48` | `0xffffff8002189d67` |
| `monitorAWDLState`, re-test del bit | `0xffffff80021ee228` | `0xffffff80020db269` | `0xffffff800218a6a3` |
| `initParamsToDefaults`, store 0 | `0xffffff80021c818f` | `0xffffff80020b8095` | `0xffffff8002166728` |

El bloque LLA de `interfaceStateChange` sigue íntegro en 14/15: mismas cadenas
(`"ERROR: enableLLAMonitoring  as n..."`, `"NOT-ENABLING-AWDL"`, `"%s: Enabling AWDL due to %s\n"`),
misma llamada a `isLLAConfigured` (23J631 `0xffffff80020c93ce`, 24H232 `0xffffff8002177863`) y mismos
inmediatos.

### 3.2 `_bonjourTriggersAWDL` (dword)

La guarda del bloque LLA la prueba `cmp dword ptr [ivars+X], 0` en `interfaceStateChange`:
`0xffffff80021d9b0a` (`+0xA718`), `0xffffff80020c93b1` (`+0x7FA8`), `0xffffff8002177841` (`+0x7FF8`).
Escritores, con el mismo reparto por build que en 25G241:

| Escritor | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `initWithInterface` default 1 | `0xffffff80021c15d6` | `0xffffff80020b1cf9` | `0xffffff8002160192` |
| `initWithInterface` boot-arg `awdl_enable` → 0 | `0xffffff80021c1668` | `0xffffff80020b1d88` | `0xffffff8002160221` |
| `initWithInterface` `awdl_oper_mode=1` → 0 | `0xffffff80021c16d1` | `0xffffff80020b1df1` | `0xffffff800216028a` |
| `setAwdlCtl` (payload, log `"bonjourTriggersAWDL"`) | `0xffffff80021d032a` | `0xffffff80020c0aa7` | `0xffffff800216e5e4` |
| `setAwdlOperatingMode` (dos sitios) | `0xffffff800221ec92` / `0xffffff800221ecc5` | `0xffffff8002108a7c` / `0xffffff8002108a55` | `0xffffff80021b8998` / `0xffffff80021b89cb` |

Los boot-args `awdl_enable` y `awdl_oper_mode` siguen presentes con el mismo nombre en los tres
(`lea rdi, "awdl_enable"` / `"awdl_oper_mode"` en el slice de `initWithInterface`).

### 3.3 `AWDLOn`/stateFlags (dword)

| Sitio | 25G241 (`+0x3EE0`) | 23J631 (`+0x3EC8`) | 24H232 (`+0x3EE8`) |
|---|---|---|---|
| `monitorPeer`, `test byte, 0Ch` (QUIET/FOR ASSOCIATION) | `0xffffff80021a9339` | `0xffffff800209b197` | `0xffffff800214928b` |
| `enableAWDL`, `(x & ~6) \| 2` | `0xffffff80021dd5a9` | `0xffffff80020cc6e6` | `0xffffff800217aa65` |
| `setAwdlSyncState`, `x & ~2` | `0xffffff800221e49d` | `0xffffff800210803b` | `0xffffff80021b8255` |
| `setAwdlSyncState`, `x & ~4` | `0xffffff800221e52e` | `0xffffff8002108101` | `0xffffff80021b831b` |
| `setAwdlSyncState`, write final `(x & ~6) \| 2` | `0xffffff800221e6ec` | `0xffffff8002108254` | `0xffffff80021b84a2` |
| `setAwdlSyncState`, test bit 0 (CONFIGURED) | `0xffffff800221e41b` | `0xffffff8002107fc7` | `0xffffff80021b81df` |
| `enterQuietMode`, `(x & ~6) \| 4` | `0xffffff80021f42b5` | `0xffffff80020e0e5e` | `0xffffff8002190127` |
| `exitQuietMode`, `x & ~6` | `0xffffff80021faf7e` | `0xffffff80020e7432` | `0xffffff80021969f2` |
| `exitQuietMode`, `(x & ~6) \| 2` | `0xffffff80021fb085` | `0xffffff80020e7511` | `0xffffff8002196b00` |
| `setAwdlSuspendedMode`, `x \| 8` | `0xffffff80021e5cc9` | `0xffffff800210848f` | `0xffffff80021827a1` |

La decisión de activación de `interfaceStateChange` conserva la forma: `cmp dword ptr [rcx+4], 0`
(25G241 `0xffffff80021d9bc2`, 23J631 `0xffffff80020c9460`, 24H232 `0xffffff80021778f9`), test de
fuente == 3 (`cmp r12d, 3` en 23J631/24H232; el registro de 25G241 era `r15d`) y suspensión con
`test al, 8` sobre el byte bajo del campo (`0xffffff80021d9bd7` / `0xffffff80020c9476` /
`0xffffff800217790e`).

### 3.4 Timer LLA, `channelSequenceStrategy` y campo acompañante

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| Store del `IO80211TimerSource` en `initWithInterface` | `+0x28A0`, `0xffffff80021c209b` | `+0x3EB8`, `0xffffff80020b2891` | `+0x3ED8`, `0xffffff8002160c24` |
| Reload en `interfaceStateChange` (enable +0x150) | `0xffffff80021d9b8a` / `0xffffff80021d9b99` | `0xffffff80020c942b` / `0xffffff80020c943a` | `0xffffff80021778c5` / `0xffffff80021778d4` |
| Rearme `setTimeoutMS(0x1F4)` (celda +0x1D0) | `0xffffff80021d9bb6` | `0xffffff80020c9453` | `0xffffff80021778ed` |
| `channelSequenceStrategy`: `cmp dword [ivars+4], 0` (decisión) | `0xffffff80021d9bc2` | `0xffffff80020c9460` | `0xffffff80021778f9` |
| `channelSequenceStrategy`: default 1 (`AwdlStateMachine`) / manual 3 (`AwdlUserConfigured`) | `0xffffff80021c15d3` / `0xffffff80021c16c8` | `0xffffff80020b1cf6` / `0xffffff80020b1de8` | `0xffffff800216018f` / `0xffffff8002160281` |
| Campo acompañante (a 0 en modo manual) | `+0xA66C`, `0xffffff80021c16d7` | `+0x7F04`, `0xffffff80020b1df7` | `+0x7F4C`, `0xffffff8002160290` |

En `handleAwdlLLATimer` las celdas del timer no se movieron: `+0x218` (cancelTimeout),
`+0x158` (disable) y `+0x1D0` (setTimeoutMS) con las tres cadenas de log idénticas en los tres builds
(25G241 `0xffffff80021c2f4b`/`c2f5f`/`c2fb0`/`c2fc8`/`c2ffd`; 23J631 `0xffffff80020b3801`/`b3815`/
`b386d`/`b3887`/`b38c3`; 24H232 `0xffffff8002161a9b`/`61aaf`/`61b00`/`61b18`/`61b4d`).

## 4. Ivars del `IO80211AWDLPeer`

El bloque AWDL del par también cuelga de `this+0x20` (`mov rax, [rbx+20h]` antes de cada test) y los
dos campos citados bajan:

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| Flag PPP done: guarda del ageout en `monitorPeer` | `+0x6DE`, `test byte, 1`, `0xffffff80021a9705` | `+0x6AE`, `cmp byte, 0`, `0xffffff800209b538` | `+0x6AE`, `cmp byte, 0`, `0xffffff8002149648` |
| Flag PPP done: store del bool en `pppDone` | `0xffffff80021bfb86` | `0xffffff80020b05fa` | `0xffffff800215ea60` |
| Flag PPP done: test en `skywalkInputPacket` | `0xffffff80021bf9c5` | `0xffffff80020b0445` | `0xffffff800215e8a7` |
| Marca del retorno temprano en `monitorPeer` (tras `test 0Ch`) | `+0x310`, `0xffffff80021a9342` | `+0x2E8`, `0xffffff800209b1a0` | `+0x2E8`, `0xffffff8002149294` |

`pppDone` conserva el `IOMallocZeroData(0x200)` y la log `"Posted Peer Presence for ..."` en los tres
(25G241 `0xffffff80021bfb3b`/`bfd43`; 23J631 `0xffffff80020b05af`/`b07b7`; 24H232
`0xffffff800215ea15`/`ec1d`). `setAwdlPostPeerPresenceDone` sigue haciendo `findPeer(data+4)` +
`safeMetaCast` + `pppDone(true)` (25G241 `0xffffff800221c980`/`c98f`/`c9a1`; 23J631
`0xffffff800210692a`/`6939`/`694b`; 24H232 `0xffffff80021b69fe`/`6a0d`/`6a1f`).

## 5. Ranuras de vtable y celdas llamadas

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| Celda `monitorPeer` en `__ZTV15IO80211AWDLPeer` | `0xffffff800245a110`, ranura `+0x180` | `0xffffff80022c9ed0`, `+0x180` | `0xffffff80023c9aa8`, `+0x178` |
| Llamada en `_doMonitoring` | `0xffffff800224f720` `[rax+180h]` | `0xffffff8002134ce0` `[rax+180h]` | `0xffffff80021e556e` `[rax+178h]` |
| Celda `inputPacket` (paquete) en `__ZTV23IO80211VirtualInterface` | `0xffffff80024736d8`, `+0xB50` | `0xffffff80022dc6e0`, `+0xB48` | `0xffffff80023dfea0`, `+0xB48` |
| Celda `sendPendingPacketsToStack` | `0xffffff8002473ab8`, `+0xF30` | `0xffffff80022dca18`, `+0xE80` | `0xffffff80023e0268`, `+0xF10` |
| Celda `freePendingPackets` | `0xffffff8002473ac0`, `+0xF38` | `0xffffff80022dca20`, `+0xE88` | `0xffffff80023e0270`, `+0xF18` |
| `pppDone` llama la celda de entrega | `0xffffff80021bfc77` `[rax+0F30h]` | `0xffffff80020b06eb` `[rax+0E80h]` | `0xffffff800215eb51` `[rax+0F10h]` |
| `ageoutPacketsWaitingForPPP` llama la celda de liberación | `0xffffff80021aa984` `[rax+0F38h]` | `0xffffff800209c84e` `[rax+0E88h]` | `0xffffff800214a8e2` `[rax+0F18h]` |
| `flushPacketsWaitingForPPP` llama la celda de liberación | `0xffffff80021bff87` | `0xffffff80020b09bd` | `0xffffff800215ee47` |
| Umbral del ageout (`cmp edx, 7D1h`) | `0xffffff80021aa8cf` | `0xffffff800209c78b` | `0xffffff800214a82d` |
| Celda `createPeerManager` | `0xffffff8002473690`, `+0xB08` | `0xffffff80022dc6a0`, `+0xB08` | `0xffffff80023dfe58`, `+0xB00` |
| Llamada desde `start` de la vif | `0xffffff80022f5bca` | `0xffffff80021a06eb` | `0xffffff80022630b2` |
| Celda `getP2PSkywalkPeerMgr` | `0xffffff8002473848`, `+0xCC0` | `0xffffff80022dc858`, `+0xCC0` | `0xffffff80023e0010`, `+0xCB8` |
| Celda `handleIoctl` | `0xffffff8002473a08`, `+0xE80` | `0xffffff80022dc9a0`, `+0xE08` | `0xffffff80023e01d0`, `+0xE78` |

Las implementaciones base de `sendPendingPacketsToStack`/`freePendingPackets` siguen siendo los stubs
de solo `IOLog("... Not Implemented")` en 14/15: 25G241 `0xffffff8002334e68`/`0xffffff8002334e88`,
23J631 `0xffffff80021dd636`/`0xffffff80021dd656`, 24H232 `0xffffff80022aa87e`/`0xffffff80022aa89e`.
Sin override, los paquetes retenidos se pierden igual que en 25G241.

## 6. El ioctl 415: tabla de handlers y comprobación de longitud

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `gSetHandlerTable[414]` (cmd 415, celda) | `0xffffff8002460be0` | `0xffffff80022cf1e0` | `0xffffff80023d0250` |
| Handler (`setPOST_PRESENCE_DONE`) | `0xffffff800228d968` | `0xffffff800216790b` | `0xffffff800222038e` |
| Comprobación de longitud `cmp dword [req+18h], 0Ch` | `0xffffff800228d973` | `0xffffff8002167916` | `0xffffff8002220399` |
| Tail-call a `setAwdlPostPeerPresenceDone` | `0xffffff800228d9b9` | `0xffffff800216795c` | `0xffffff80022203df` |
| `gSetHandlerTableLegacy[414]` | 0 | 0 | 0 |

La entrada legacy para el 415 está vacía en los tres builds: el 415 solo llega por la tabla moderna.
La firma del handler cambia por build (25G241 `(Controller, SkywalkInterface, APIUserClient, req)`,
23J631 `(Controller, VirtualInterface, InfraInterface, req)`, 24H232 `(Controller, SkywalkInterface,
req)`), pero el cuerpo es idéntico: longitud 12, datos no nulos, manager desde los ivars de la
interfaz, `safeMetaCast` a `IO80211AWDLPeerManager` (45 / 13h / 2Dh como códigos de error iguales).

## 7. Puntero del peer manager y del controlador en la vif

| Fila | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| Puntero de ivars de la vif (`getPeerManager` carga) | `+0x120`, `0xffffff80022f5fac` | `+0x110`, `0xffffff80021a0a40` | `+0x110`, `0xffffff8002263494` |
| Peer manager dentro de los ivars (`getPeerManager` segundo load) | `+0x28`, `0xffffff80022f5fb3` | `+0x30`, `0xffffff80021a0a47` | `+0x28`, `0xffffff800226349b` |
| Store del manager en `start` | `0xffffff80022f5bd8` | `0xffffff80021a06f9` | `0xffffff80022630c0` |
| Store del controlador en `start` (`safeMetaCast(provider, IO80211Controller)`) | `+0x30`, `0xffffff80022f583f` | `+0x38`, `0xffffff80021a0393` | `+0x30`, `0xffffff8002262d27` |
| Celda `getController` en la vtable de la vif | `0xffffff8002473730`, `+0xBA8` | **ausente** | `0xffffff80023dff00`, `+0xBA8` |
| Celda `executeCommand` en `__ZTV17IO80211Controller` | `0xffffff80024619a8`, `+0x850` → `0xffffff80026f51e0` | `0xffffff80022cfc58`, `+0x850` → `0xffffff80025156ee` | `0xffffff80023d0f50`, `+0x850` → `0xffffff8002630c98` |
| `handleIoctl` llama `executeCommand` | `0xffffff8002331cf6` `[r10+850h]` | `0xffffff80021da579` `[rax+850h]` | `0xffffff80022a77d6` `[r10+850h]` |

En 23J631 `getController` dejó de estar en la vtable de la vif (la ranura `+0xBA8` la ocupa
`updateLinkParameters`) y `handleIoctl` lee el controlador directamente de los ivars:
`mov rax, [rcx+110h]; mov rdi, [rax+38h]` en `0xffffff80021da55a`–`0xffffff80021da561`. Los cuatro
códigos de ioctl que filtra `handleIoctl` (`0x802069C8`, `0x802869C8`, `0xC02869C9`, `0xC02069C9`,
con `0xC02069C8` de frontera) son los mismos en los tres builds; los demás ioctls siguen cayendo en
`processBSDCommand` (tail `jmp` en `0xffffff8002331d1b` / `0xffffff80021da58e` / `0xffffff80022a77fb`).

## 8. Qué no se movió

- Base de ivars AWDL de manager y par: `this+0x20` en los tres.
- `ivars+4` (`channelSequenceStrategy`) del manager.
- Celdas del timer (`+0x150`, `+0x158`, `+0x1D0`, `+0x218`) y el periodo `0x1F4` ms.
- Umbral del ageout `0x7D1` (2001 ms).
- Longitud del 415 (12) y el tamaño de lote `0x200` del array de paquetes en `pppDone`.
- Ranura `+0x850` de `IO80211Controller` (`executeCommand`).
- Los cuatro códigos de ioctl de `handleIoctl` y el reparto gate/`processBSDCommand`.
- Las cadenas de log del bloque LLA y del ciclo PPP (mismos literales en las tres versiones).

## 9. Cambios de forma que afectan al port

- **`setPOST_PRESENCE_DONE` cambia de firma en las tres versiones** (§6): el gate por build no puede
  fijar el índice del argumento de la interfaz; la ruta fiable sigue siendo la entrada de
  `gSetHandlerTable` y la longitud 12 del payload.
- **`inputPacket` en 23J631 pierde el `bool` final** y la vtable añade un overload nuevo
  `inputPacket(__mbuf*, packet_info_tag*)` en la ranura `+0xE98` (celda `0xffffff80022dca30`,
  implementación `0xffffff80021dd5b0`). En 24H232 la firma de paquete vuelve a llevar el `bool` final
  y no existe el overload de mbuf. El desvío RX de la capa debe llamar la ranura `+0xB48` con la firma
  del build en 14/15.
- **`IO80211PeerManager::skywalkInputPacket` cambia de firma en 23J631** (un `bool` menos al final);
  el resto de la cadena RX por `skywalkInputPacket` del par no se movió.
- **`getController` queda fuera de la vtable de la vif en 23J631** (§7): el controlador se lee de
  `ivars+0x38` en ese build y de la ranura `+0xBA8` en 25G241/24H232.
- El ivar del workqueue que `start` toma del controlador (slot `+0xC68` en 25G241) se reasenta en
  `ivars+0x38` de la vif en 25G241/24H232 y fuera del rango verificado en 23J631 `?`.

## 10. Filas ausentes y puntos abiertos

| Fila | Build | Estado |
|---|---|---|
| Ranura `getController` en la vtable de la vif | 23J631 | Ausente; el acceso es directo por ivar (§7) |
| Entrada 415 de `gSetHandlerTableLegacy` | los tres | 0; el 415 no circula por la tabla legacy |
| Emisores de las fuentes 0/1/3/8/10/12/13 de `StateChangedSource` | — | Pendiente de los docs fuente, sin cambios en esta pasada |
| Cerrojo de la ruta `removePeer` → `flushPacketsWaitingForPPP` | 25G241 | Cerrado en `AWDL_PRESENCE_PREREQS.md` §3.2 (sin data-path lock, tras `unlockDataPath`); no es una fila de offsets |

## 11. Consumo por el gate

El TSV se alimenta del gate por build igual que `Tools/awdl_offsets_23J631.tsv` y
`Tools/awdl_offsets_24H232.tsv`: la columna de VA de cada fila apunta a un sitio del propio build, de
modo que una re-verificación mecánica puede localizar la instrucción y comparar el valor. El orden de
selección del gate para la capa de presencia debe ser: base de ivars de la vif (`+0x120`/`+0x110`),
offset del manager (`+0x28`/`+0x30`), ranura `inputPacket` (`+0xB50`/`+0xB48`), ranuras PPP
(`+0xF30`/`+0xE80`/`+0xF10` y `+0xF38`/`+0xE88`/`+0xF18`), ranura `monitorPeer`
(`+0x180`/`+0x178`) y después los campos del manager (`LLAValid`, `AWDLOn`, `_bonjourTriggersAWDL`,
timer, campo acompañante). Las filas sin cambio (`ivars+4`, `+0x850`, umbral, longitud) no necesitan
selección.
