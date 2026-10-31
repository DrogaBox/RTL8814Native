# Qué action frames AWDL llegan al daemon como evento 48 y cuáles se consumen en silencio

Cierra el interrogante E3 de `AWDL_ETAPAS_2_A_4.md` §5 («condición UMI de subtipo + canal que decide
qué AF llega al daemon como 48») y el último pendiente de `AWDL_WIFIP2PD_PRESENCE_TRIGGER.md` §7.
Respuesta corta: **la puerta no es el subtipo ni el canal**. Un AF se publica como evento 48
(`postAFToP2pd`) si y sólo si, al final de `actionFrameProcessInput`, el contexto no lleva código de
error (`ctx+0x2C == 0`) y el par ha quedado con `peerReachability == 1` (`priv+0x78 == 1`). Todo lo
demás (cabecera, OUI, RSSI, anillo, límite de tasa, parseo de TLVs) sólo decide si el frame llega a
esa puerta o se consume en silencio (con volcado al tap BPF en algunos casos).

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, build 25G241, md5
`f489ff8be80e6013a1bf47561dcaef45`), copia del IDB anotado en `/tmp` durante la sesión. Todas las
direcciones son VA del kernelcache, leídas del desensamblado o del pseudocódigo de Hex-Rays. Los
convencionalismos (vtable, `priv = *(peer+0x20)`, cabecera de 72 B) son los de
`AWDL_PEERMANAGER_DRIVER_ABI.md`. Lo no confirmado va con `?`.

## 1. El camino completo

```
bpfTapInput (dlt 12)                                   0xffffff8002331070  (ABI §1.5)
  └─ IO80211AWDLPeerManager::actionFrameInput          0xffffff800220e1ae  (celda 71, +0x238)
       └─ [anillo 104 B] actionFrameProcessThread      0xffffff80021c3ebc  (defecto, bit 0 de +16880)
            └─ actionFrameProcessInput                 0xffffff800220e8b0
                 ├─ isValidAF / actionFrameHasCorrectOUI   0xffffff800220fa64 / 0xffffff8002210200
                 ├─ findPeer / addAWDLPeer             0xffffff800220ec86 / 0xffffff800220eceb
                 ├─ IO80211AWDLPeer::actionFrameReport 0xffffff80021af6be
                 │    ├─ extractVersionTlvForLegacy…   0xffffff80021b3114 (llamada 0xffffff80021afc2d)
                 │    ├─ doMinimalAFProcessing         0xffffff80021b3ea6 (llamada 0xffffff80021b013f)
                 │    └─ shouldStopAFProcessing        0xffffff80021b48b8 (llamada 0xffffff80021b0160)
                 └─ postAFToP2pd                       0xffffff800220fc2c (única xref: 0xffffff800220f94c)
```

`postAFToP2pd` tiene un único llamador en todo el binario: `actionFrameProcessInput` en
`0xffffff800220f94c` (xref verificada). No hay ruta alternativa hacia el evento 48.

## 2. La puerta del evento 48

En `actionFrameProcessInput`, tras la llamada a `actionFrameReport` (`0xffffff800220ed31`):

| Paso | Dirección | Efecto |
|---|---|---|
| `cmp dword [ctx+0x2C], 0` | `0xffffff800220ed36`, salto `0xffffff800220ed42` | ≠ 0 ⇒ `xor r15d,r15d` y retorno 0 (`0xffffff800220ed44`-`0xffffff800220ed47`): AF consumido en silencio, sin `SYNC_INFO` ni evento. El valor de retorno en `eax` de `actionFrameReport` no se consulta en ningún punto. |
| comprobación de par | `0xffffff800220f927` | sin par (`v86 == 0`, p. ej. `addAWDLPeer` falló) ⇒ retorno sin evento. |
| `cmp dword [priv+0x78], 1` | `0xffffff800220f930`-`0xffffff800220f939` | `peerReachability != 1` ⇒ salta a la liberación del par (`0xffffff800220f761`) sin evento. |
| llamada `postAFToP2pd(this, buf, hdr)` | `0xffffff800220f94c` | única publicación. Después libera el par (`0xffffff800220f768`, vtable+0x28) y retorna 0. |

`ctx` es el `actionFrameReportContext` de 0x98 B que `actionFrameProcessInput` construye en pila
(ceroed en `0xffffff800220e9b4`; campos en `0xffffff800220ec40`-`0xffffff800220ec79`: buffer +0x00,
cabecera +0x08, hdrLen +0x10). `actionFrameReport` escribe `ctx+0x2C = 1` (o códigos 0x12–0x21) en
sus rutas de error; el éxito lo deja a 0. El snapshot de `priv+120` en `ctx+0x58`
(`0xffffff80021b01d8`/`0xffffff80021b2d80`) es informativo, no participa en la puerta.

`peerReachability` (`priv+0x78`, dword) — el doc `AWDL_PEERMANAGER_SOFTWARE.md` §3 lo llama «modo de
maestro +120»; el nombre real del campo es el enum `peerReachability`:

| Valor | Escritor | Dirección de la escritura |
|---:|---|---|
| 1 (routable) | `setPeerRoutable` | `0xffffff80021aaf2d` (`mov qword [rax+78h], 1`) |
| 1 | `extractVersionTlvForLegacyPeerProcessing` (OS 8/4 o flag `priv+0x31` bit 0) | `0xffffff80021b3243` |
| 2 (observed/legacy) | `extractVersionTlvForLegacyPeerProcessing` (`isLegacyPeerNotRoutable`) | `0xffffff80021b334d` |
| 3 (not reachable) | `setPeerNotReachable` | `0xffffff80021aa702` |
| genérico | `setPeerReachability(v)` | `0xffffff80021ab180` (sin xrefs directas: llamada virtual `?`) |
| 0 | estado inicial (peer creado con `IOMallocZeroData` vía `addAWDLPeer` `0xffffff800220fe4a` → `withAddressAndManager` `0xffffff80021a8dac`) | — |

## 3. Qué hace `postAFToP2pd` (`0xffffff800220fc2c..0xffffff800220fdcb`)

1. `len = getLength(buf)` (vtable+304) y `body = getBytesNoCopy(buf)` (vtable+344);
   `IOMallocZeroData(len + 72)`; fallo ⇒ log `Could not allocate AR BUFFER for AF message` (líneas
   19654/19655) y **no se publica nada**.
2. Copia la cabecera de 72 B (`apple80211_act_frm_hdr`) y detrás el cuerpo completo del buffer:
   el payload del evento es `{hdr 72 B ‖ cuerpo del AF}`.
3. Publica con la ranura +0xB18 (celda 355) de la vtable de la interfaz AWDL
   (`*(*(manager+24)+1376)`), que en `__ZTV23IO80211VirtualInterface` es
   `IO80211VirtualInterface::postMessage(uint, void*, ulong, bool)` (`0xffffff8002333192`),
   argumentos `(48, buffer, len+72, 0)` — el canal que consume `wifip2pd` como `DriverEvent.AWDL` etiqueta 1
   (`AWDL_WIFIP2PD_PRESENCE_TRIGGER.md` §3.1; `AWDL_EVENTS.md` §4.2).
4. Incrementa el contador IOReport `fAWDLActionFrames` (chID `0x4966416650325064` «IfAfP2Pd»);
   fallo ⇒ log `%s::%s(%d) Failed to inc %s`.

## 4. Tabla de condiciones

### 4.1 Descartes de entrada (`actionFrameInput` y válidos de cabecera)

| Condición | Dirección | Efecto |
|---|---|---|
| AWDL apagado (`+16096` bit 1 = 0) o disable en curso (`+414` bit 0 = 1), camino con hilo | `0xffffff800220e37f`, `0xffffff800220e388` | contadores «IfAfIOFF», `sendToBpfTap` (`0xffffff800220e493`), retorno del buffer (`0xffffff800220e49e`); sin evento |
| ídem, dentro de `actionFrameProcessInput` (camino sin hilo) | `0xffffff800220e9bd`, `0xffffff800220e9c6` | retorno 0 inmediato (`0xffffff800220e9d3`); sin evento |
| anillo lleno (`(head+1) & mask == tail`) | `0xffffff800220e4b5`-`0xffffff800220e4c9` | flag de desbordamiento `+16868 = 1` (`0xffffff800220e4cf`), log `Out of Space in AF Queue` (19292, `0xffffff800220e50b`), arma el timer del hilo (`0xffffff800220e541`-`0xffffff800220e55d`), tap BPF y buffer devuelto; sin evento |
| `isValidAF` falla: `hdrLen <= 0x47`, cursor nulo o OUI incorrecto | `0xffffff800220fa64` (llamada del camino con hilo tras `0xffffff800220e4c9`; llamada inline del camino sin hilo en `0xffffff800220eb4e`) | logs `Invalid frame/header, headerLen %lu` / `frame len %lu … sizeof 72` / `BAD OUI in AF`; en `actionFrameInput`: log `Invalid AF` (19320, `0xffffff800220e87d`) + tap BPF; retorno 1 de `actionFrameProcessInput` (`0xffffff800220eb53`-`0xffffff800220eb58`) = «sólo tap». Sin evento |
| OUI/type/version: `body[1..3] == 00:17:F2`, `body[4] == 0x08`, `body[5] != 0` | `0xffffff8002210200` (comparaciones dentro de `0xffffff8002210200..0xffffff8002210435`, instrucción exacta `?`) | único filtro de contenido del frame; `body[0]` (categoría) y `body[6]` (subtipo) **no se comprueban** |
| `hdrLen >= 0x49` al encolar en el anillo | `0xffffff800220e8a5` (`ud1`), igual en el drenaje (`0xffffff80021c46ed`, fin de función `?`) | trap deliberado del kernel: un driver que pase `hdrLen > 72` tumba el sistema |
| asserts de entrada de `actionFrameProcessInput` (buf nulo 19686, `getLength()==0` 19690, hdr nulo 19694, sin workspace 19699) | `0xffffff800220ea00`, `0xffffff800220ea30`, `0xffffff800220ea5d`, `0xffffff800220ea8a` | retorno 0; sin evento |

### 4.2 Descartes por RSSI en `actionFrameProcessInput`

| Condición | Dirección | Efecto |
|---|---|---|
| par conocido, `hdr+0x2C` (RSSI, con signo) < `ivars+10268` (umbral de borde, −70/−78) | comparación `0xffffff800220ecb4` (`jge` pasa) | libera el par (`0xffffff800220ecc3`) y retorno 0; sin evento |
| par conocido con bit 0 de `priv+0x0C` a 1 | `0xffffff800220eca6` | salta el filtro de RSSI |
| emisor desconocido con RSSI < `ivars+10268` | `0xffffff800220ecd6` (`jl`) | retorno 0 sin llamar a `addAWDLPeer` (`0xffffff800220eceb`); sin evento |
| `addAWDLPeer` devuelve 0 | `0xffffff800220ecf7` | log `AF NA [%3d]` (`0xffffff800220ee70`, snprintf en `0xffffff800220ee84`), notificación al RangingManager (`0xffffff800220eebc`); sin evento |

Los valores de `+10268` (+10272/+10276) los fija `determineAndSetEdgeRssiThreshold`
(`0xffffff80021e0f5e`; −70/−78, −65/−78, −45/−58; `AWDL_PEER_PRESENCE_PPP.md` §5.1).

### 4.3 Descartes dentro de `actionFrameReport` (`ctx+0x2C ≠ 0`)

Todos terminan en el retorno sin evento; el llamador sólo distingue cero de no cero
(`0xffffff800220ed36`), aunque el código escrito varía:

| Código | Dirección de la escritura | Causa (log/línea) |
|---|---|---|
| 1 | `0xffffff80021af9f4` | **límite de tasa**: ≥ 33 AFs (`0x21`, comprobado en `0xffffff80021af9e3`/`0xffffff80021afa00`) del mismo par dentro de una ventana de 1024 ms (`0x400`, `0xffffff80021af9db`; contador `priv+296`, marca `priv+288`); al caducar la ventana, log `Received %d Action Frames from peer … in 1 second. Bad Peer` (2226) |
| 1 | `0xffffff80021afde9` | «bad chan seq tlv, getCursorSize()» (2299): TLV 18 presente pero `isValidChanSeqTlv` (`0xffffff8002214b76`) falla |
| 1 | `0xffffff80021afe9a` | «bad chan seq ext tlv» (2316): TLV 32 con SDB activo (byte con signo del controlador +2879) inválido |
| 1 | `0xffffff80021b016b` | error de parseo señalado por `didAFPayloadChange` (`out-bool` en `0xffffff80021b0165`), `doMinimalAFProcessing == 2` (`0xffffff80021b0147`), o TLV 12 más largo que el frame (`0xffffff80021affa3`) |
| 1 | `0xffffff80021b1913` | «serviceParam Bad ssth length» (TLV 6 de servicios malformado) |
| 0x12 | `0xffffff80021b19ca` | «Error: updating sync param…» |
| 1 (×5) | `0xffffff80021b1ace`, `0xffffff80021b1ae8`, `0xffffff80021b1b02`, `0xffffff80021b1b46`, `0xffffff80021b1b60` | errores `[BssSteering]` («multiple») |
| 0x17 | `0xffffff80021b1f92` | «Error: parsing the NSync TLV» |
| 0x1D | `0xffffff80021b1ff7` | «[BssSteering] [ERROR] tlvParsing» |
| 0x1F / 0x1E / 0x15 | `0xffffff80021b217a`, `0xffffff80021b21d1`, `0xffffff80021b22e9` | «Error: parsing the version TLV» (TLV 21) |
| 0x21 | `0xffffff80021b2292` | «Error: parsing pref channel fr…» |
| 0x12 | `0xffffff80021b2575` | «Error: parsing ch sequence fro…» (TLV 4 de sync params) |

Además, los asserts iniciales de `actionFrameReport` (ctx nulo 2132, buffer nulo 2140, cursor
inválido 2168, `hdrLen < 0x48` 2188, cuerpo de 16 B sin TLVs 2203) retornan 0 **sin** escribir
`ctx+0x2C`: el frame continúa hacia la puerta del 48. Es decir, un AF válido pero sin ningún TLV de
un par enrutable sí se publica (cuerpo vacío detrás de la cabecera). `?` sobre si algún productor
real genera frames sin TLVs.

### 4.4 Pérdida de elegibilidad (`peerReachability` deja de ser 1)

| Condición | Dirección | Efecto |
|---|---|---|
| par RTG activo (`priv+1689` bit 0, estado `priv+1698` = 1/2) cuyos AFs dejan de traer TLV de RTG durante ≥ 5001 ms (`0x1389`) desde `priv+1744` | logs 3192/3198 y llamadas en el bloque `0xffffff80021b1d40`-`0xffffff80021b1db3` | `removePeerFromRealTimeGroup` (`0xffffff80021b1da6`) + `setPeerNotReachable` (`0xffffff80021b1dae` ⇒ `priv+120 = 3`, `cleanUpForPeerRemovedFromNDP`, que alimenta el evento 47) + `removeAllIfOnlyPassivePeers`; los siguientes AFs del par no se publican hasta re-enrutable |
| ídem con 2001–5000 ms | log «…no RTG TLV for 2 Seconds, trying to recover» (3210) | intento de recuperación con `sendRealTimeUMI` (`0xffffff80022020e8`); sin cambio de reachability |
| RTG pasivo (estado 3) sin TLV de RTG | log 3188 | sólo `removePeerFromRealTimeGroup`; la reachability no cambia aquí |

### 4.5 Condiciones que deciden `peerReachability == 1` (vía `setPeerRoutable`)

`setPeerRoutable` (`0xffffff80021aad6c`) escribe 1 en `0xffffff80021aaf2d` **antes** de sus puertas;
las dos condiciones siguientes sólo deciden si se llega a esa escritura, y la tercera sólo si se
imprime el log de «postPeerPresence»:

| Condición | Dirección | Efecto |
|---|---|---|
| burbuja de RSSI: par no visto en 5 GHz dual-band (`isDualBandPeerDiscoveredOn5GHz` `0xffffff80021b8cd4`) con RSSI < `ivars+10276` (−45/−58) | `0xffffff80021aad6c` (lectura del umbral en pseudocódigo, instrucción `?`) | log «Not Routable … outside bubble» y **no** se escribe el 1 (el par se queda en 0/2/3) |
| guarda de publicación (`waitForPostingPeer` `0xffffff80021b801c`): con fastDiscovery (`PM+65193` bit 0 o `priv+1756` bit 0), menos de `ndpPostingGuardTimemSec` (`PM+65216`; 128 ms por defecto) desde el primer AF (`priv+736`), salvo `priv+1757` bit 0 | escritura `0xffffff80021aaf2d` sólo si la guarda pasa | el primer AF de un par nuevo normalmente NO publica; hay que esperar la guarda |
| `LLAValid` (`PM+65308` bit 0) y `AWDLOn` (`PM+16096` bit 1) | gate tras la escritura (lectura de `PM+0xFF1C` en `0xffffff80021aaf47`, log 4050 «Cannot post peer presence … (LLAValid %d AWDLOn %d)») | si faltan, sólo se cambia el log: sale el 4050 en vez de «postPeerPresence for…» (4057 o la variante con `msecSinceAwdlOn`). `setPeerRoutable` no hace nada más en ninguna de las dos ramas: `priv+120` ya vale 1 y el 48 se emite igual |

Llamadores de `setPeerRoutable`: `shouldStopAFProcessing` (`0xffffff80021b4958` camino tvOS/watchOS/
flag y `0xffffff80021b4a11` camino normal), `monitorPeerRssi` (`0xffffff80021aa432`) y
`peerDiscoveredOnBothSide` (`0xffffff80021bcda3`).

### 4.6 `shouldStopAFProcessing` (`0xffffff80021b48b8`): afina pero no decide el 48

| Condición | Dirección | Efecto |
|---|---|---|
| error de parseo (`didAFPayloadChange` `0xffffff80021b8ee2` escribe el `out-bool`) | `0xffffff80021b0165` → `0xffffff80021b016b` | **sí descarta**: `ctx+0x2C = 1` (TLV 12 o TLV 33 malformados, log 3640/3652) |
| retorno distinto de 0 («stop») | retorno en `0xffffff80021b017a`-`0xffffff80021b017c` | **no descarta**: `actionFrameReport` retorna 1 rellenando `ctx+0x3C..0x58` (`0xffffff80021b0188`-`0xffffff80021b01e1`) pero `ctx+0x2C` sigue a 0, así que `actionFrameProcessInput` continúa y el 48 se evalúa igual; sólo se salta el bloque final del informe (FW cache, actualización de la caché del par, `X+0xA8`) |
| condición del «stop»: par ya publicado al NDP (`X+0xA8 != 0`, `X = *(peer+0x18)`) y (estado final ≠ 1 o payload sin cambiar) | lectura del flag `0xffffff80021b4b4a`; cómputo `0xffffff80021b4b54`-`0xffffff80021b4b5e`; sin publicar ⇒ retorno 0 con log «Not Posted Peer to NDP so do not stop processing action frames.» (3620) | el «payload sin cambiar» viene de `didAFPayloadChange`, que devuelve el bit 0 de `priv+88` (`?` instrucción exacta; lo marca `parseAwdlDataPathTLVAndTakeAction`) |
| contador de AFs sin estado 1 (`priv+124`) y contadores de reachability del manager (`ivars+42800 + 4*estado`) | `0xffffff80021b49fb`, `0xffffff80021b4b37`/`0xffffff80021b4b3f` | sólo estadística |
| par tvOS (OS 8) o watchOS (OS 4) o flag `priv+0x31` bit 0 | `0xffffff80021b4943`-`0xffffff80021b4949` | se salta la lógica RSSI y va directo a `setPeerRoutable` (si estado ≠ 1) y retorno 0 |
| estado 1: seguimiento de RSSI fiable (umbral −60/−55/`ivars+10272` según versión del par) | `0xffffff80021b4981`-`0xffffff80021b49f6` | marca `priv+128`/`priv+136` para la caducidad posterior; sin efecto sobre el 48 |
| estado 3 (recuperación): RSSI de la cabecera (`hdr+0x2C`) contra `ivars+10276` si canal ≤ 14 o `ivars+10284` si > 14 (`getGoodRssiThreshold5G`, −55 por defecto: `initParamsToDefaults` lo escribe con el qword `0xFFFFFFC9FFFFFFD3` en `ivars+0x2828` en `0xffffff80021c7cf3`, y ningún otro acceso con desplazamiento `0x2828` o `0x282C` en las funciones AWDL de IO80211Family lo escribe; lo leen `isPeerRoutable`, `shouldStopAFProcessing` y `determinePeerReachability`); «NO ROUTE -> ROUTABLE» | `0xffffff80021b4a1f`-`0xffffff80021b4a54` | única condición de canal de todo el camino: elige el umbral, no prohibe canales |

### 4.7 Primer AF de un par nuevo (`ctx+0x3A`)

`actionFrameProcessInput` pone a cero el contexto completo con `bzero(ctx, 0x98)`
(`0xffffff800220e9b4`; el contexto es `rbp-0xC8`) y solo escribe `ctx+0x3A = 1` en un sitio:
`mov [rbp+var_8E], 1` en `0xffffff800220ecf0`, justo después de `addAWDLPeer`
(`0xffffff800220eceb`). El bit marca, por tanto, que el par lo ha creado este mismo AF. En
`actionFrameReport` controla dos bloques:
| Prueba | Salto si 0 | Bloque que solo corre en el AF que crea el par |
|---|---|---|
| `test byte ptr [rbx+3Ah], 1` en `0xffffff80021afc1d` | `jz 0xffffff80021afc3a` | `extractVersionTlvForLegacyPeerProcessing` (`0xffffff80021afc2d`); si devuelve 0 salta a `0xffffff80021b016b`, que pone `ctx+0x2C = 1` y el AF se descarta |
| `test byte ptr [rbx+3Ah], 1` en `0xffffff80021afe3b` | `jz 0xffffff80021aff14` | procesado del TLV 32 (escrituras en `0xffffff80021afe50` y `0xffffff80021afe5b`) |
Consecuencia: las escrituras de `peerReachability` 1 y 2 de `extractVersionTlvForLegacyPeerProcessing`
(§2) solo ocurren con el primer AF de cada par, y si ese primer AF no supera la extracción del TLV de
versión el par queda creado pero ese AF no llega a la puerta del 48.

## 5. Subtipo, canal y estado de sync: qué papel tienen de verdad

- **Subtipo (`body[6]`)**: la única comprobación en todo el camino es `body[6] == 3` en
  `actionFrameReport` (tras el límite de tasa; bloque conocido sólo por pseudocódigo, instrucciones
  exactas `?`) y sólo alimenta contadores por banda: MIF → `priv+156`
  (2,4 GHz) / `priv+160` (5 GHz); PSF (0) → `priv+164` / `priv+168`; otros subtipos no cuentan.
  También etiqueta el log `SYNC_INFO` («MI » si `body[6] != 0`, «PSF » si 0,
  comparación `0xffffff800220f215`) y alimenta el bloque de regeneración de plantilla MI
  (condición `MIF | otro flag` en el LABEL_420 de `actionFrameReport`, dirección `?`). Ni
  `isValidAF` ni la puerta del 48 miran el subtipo: un PSF de un par enrutable se publica igual que
  un MIF. La hipótesis «sólo UMIs» de `AWDL_ETAPAS_2_A_4.md` §2.4 queda corregida por esta tabla.
- **Canal**: viaja en `hdr+0x10` (lo rellena el driver en la cabecera de 72 B). Se guarda en
  `priv+1432` junto al mactime byteswapeado en `priv+912` (coherente con
  `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.12), separa los contadores por banda (§5) y elige el umbral de
  RSSI en la recuperación 3→1 (`0xffffff80021b4a3d`). Ningún canal está prohibido ni exigido.
- **Estado de sync**: la única condición es AWDL habilitado (`+16096` bit 1). Estar dentro de la
  ventana (`ivars+772` bit 0) sólo abre el bloque de contabilidad `SYNC_INFO` (errores de secuencia
  en `priv+280`, CoreCapture a partir de 16 errores y 50001 ms, `selfDiagnosticsReport` en
  `0xffffff800220f9f8`); no condiciona la publicación. El evento 48 sale también fuera de ventana.

## 6. Dedup y tasa, reparto kernel/daemon

- **Kernel**: no hay dedup del evento 48. Cada AF de un par con `peerReachability == 1` y sin error
  en `ctx+0x2C` vuelve a publicarse, incluidos los repetidos con payload idéntico (el «stop» de
  `shouldStopAFProcessing` sólo recorta el informe interno). El único control de tasa es el de 33
  AFs / 1024 ms por par (`0xffffff80021af9f4`) y el anillo de entrada (lleno ⇒ tap sin proceso,
  `0xffffff800220e4b5`).
- **Daemon**: la dedup real es suya — publica presencia una vez por par (flag propio del struct
  `AWDLPeer`, `AWDL_WIFIP2PD_PRESENCE_TRIGGER.md` §3.1) y los 415 repetidos no entregan nada nuevo
  (§6.2 del mismo doc). Por eso el kernel puede permitirse publicar cada AF.

## 7. Implicaciones para el driver RTL8814

1. Para que `wifip2pd` vea pares basta inyectar por `bpfTapInput(m, 12, &hdr, 72)` (`ABI §1.5`) con
   RSSI real en `hdr+0x2C`: por encima de `+10268` el AF crea/actualiza el par; el encadenado
   `setPeerRoutable` (con su guarda de 128 ms y la burbuja de −45/−58 en 2,4 GHz) hace el resto y a
   partir de ahí cada AF genera su 48.
2. Un driver que ponga `hdr+0x2C = 0` pasa siempre el filtro de borde (`0xffffff800220ecb4`, firma
   con signo) y la burbuja de `setPeerRoutable`; los umbrales dejan de filtrar nada. Para imitar el
   comportamiento de Broadcom hay que bajar el RSSI real.
3. El canal en `hdr+0x10` debe ser coherente con el canal real de captura: sólo se usa para
   contadores y umbrales, pero un valor falso degrada la recuperación 3→1 (`0xffffff80021b4a3d`) y
   las estadísticas.
4. La tasa natural de PSF/MIF (uno por ventana de 16 TU ≈ cada 15–30 ms por par) está muy por
   debajo del límite de 33 AFs/1024 ms: el límite sólo alcanzaría a un driver que reinyectara en
   bucle.
5. Los frames descartados por OUI/longitud salen por el tap BPF de `awdl0` en vez del evento 48:
   `tcpdump -i awdl0` los muestra incluso cuando `wifip2pd` no recibe nada — útil para separar
   fallos de inyección de fallos de reachability.

## 8. Pendientes (`?`)

- Instrucciones exactas de: el bloque de contadores por subtipo/banda de `actionFrameReport`
  (pseudocódigo tras `0xffffff80021af9f4`), el bit 0 de `priv+88` como retorno de
  `didAFPayloadChange`, y el segundo `ud1`
  del hilo (`0xffffff80021c46ed` es el límite de función, no la instrucción).
