# Formato en el aire de los action frames AWDL tal como los parsea el kernel

Este documento describe la trama AWDL que `IO80211AWDLPeerManager` acepta y parsea: qué bytes hay
delante de los TLVs, qué comprueba el kernel y qué campos debe rellenar el driver en cada
transmisión. Complementa `AWDL_PEERMANAGER_DRIVER_ABI.md` (§1 trata la inyección por
`actionFrameInput` y el `IO80211Buffer`) y `AWDL_FIRMWARE_DUTIES.md`. Se documenta únicamente lo que
se puede leer en el código; lo no confirmado se marca `?`.

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
IDB `Build/ida_awdl/task_awdl_pm_abi/kc.i64`. Direcciones VA del kernelcache.

Funciones de referencia:

| Función | Dirección | Papel |
|---|---|---|
| `IO80211AWDLPeerManager::actionFrameInput` | `0xffffff800220e1ae` | entrada del buffer |
| `IO80211AWDLPeerManager::isValidAF` | `0xffffff800220fa64` | longitudes mínimas |
| `IO80211AWDLPeerManager::actionFrameHasCorrectOUI` | `0xffffff8002210200` | cabecera fija |
| `IO80211AWDLPeerManager::actionFrameProcessInput` | `0xffffff800220e8b0` | alta de peer y reparto |
| `IO80211AWDLPeer::actionFrameReport` | `0xffffff80021af6be` | parseo de TLVs |
| `IO80211AWDLPeer::parseAwdlSyncParamsTlv` | `0xffffff80021b4ba6` | TLV de sync params |
| `IO80211AWDLPeer::parseAwdlSyncTreeTLV` | `0xffffff80021b56d0` | TLV de árbol |
| `IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqTLV` | `0xffffff80021b5a08` | TLV de secuencia |
| `IO80211AWDLPeer::parseAwdlChanSeqFromChanSeqExtTLV` | `0xffffff80021b5e84` | TLV de secuencia extendida |
| `IO80211AWDLPeer::parseAwdlChanSeqFromSyncParamsTLV` | `0xffffff80021b8196` | secuencia dentro de sync params |

## 1. Qué contiene el `IO80211Buffer`

El buffer que se pasa a `actionFrameInput` **no** empieza en el `frame control` de 802.11: empieza en
el octeto de **categoría** de la acción. La prueba está en `actionFrameHasCorrectOUI`
(`0xffffff8002210200`), que lee el cursor del buffer y compara:

```c
v5[1] == 0x00 && v5[2] == 0x17 && v5[3] == 0xF2 && v5[4] == 0x08 && v5[5] != 0
```

es decir, OUI en `+1..+3`, tipo en `+4` y versión en `+5`; `v5[0]` (la categoría) no se comprueba.
La cabecera de 802.11 y el FCS quedan fuera del buffer: los consume la radio antes de entregar la
trama a la pila (`?` sobre si el hardware los elimina o el driver debe recortarlos; en el camino de
Broadcom el cuerpo copiado al buffer ya viene sin ellos).

Longitudes exigidas antes de mirar nada más:

- `isValidAF` (`0xffffff800220fa64`) exige `hdrLen > 0x47` (72 B de `apple80211_act_frm_hdr`, el
  metadato, no la trama) y un cursor con `ptr` y `len` no nulos.
- `actionFrameHasCorrectOUI` exige `len >= 0x10`.
- `actionFrameReport` (`0xffffff80021af6be`) exige `hdrLen >= 0x48` y `len >= 0x10`, y a partir de
  ahí **fija el inicio de los TLVs en `buf + 16`** (`v323[0] = v7 + 16`, `v292 = v8 - 16`).

## 2. Cabecera fija de 16 B

Los 16 primeros bytes del buffer son la cabecera fija de la acción AWDL; los TLVs empiezan
exactamente en `+16`.

| Offset | Tamaño | Campo | Comprobado en el kernel |
|---:|---:|---|---|
| `+0` | 1 | categoría (`0x7F`, acción *vendor specific*) | no se comprueba (`?` el valor exigido; 0x7F es el habitual) |
| `+1` | 3 | OUI `00:17:F2` | sí: `actionFrameHasCorrectOUI` compara los tres octetos |
| `+4` | 1 | tipo `0x08` (AWDL) | sí: comparación con `0x08` |
| `+5` | 1 | versión | sí: debe ser distinto de 0 |
| `+6` | 1 | subtipo de AF: `0` o `3` | sí: `actionFrameReport` ramifica `== 3` y `== 0`; otros valores no incrementan contador y se tratan como "no UMI" (`v300 = 0`) |
| `+7` | 1 | reservado (`?`) | no se lee |
| `+8` | 4 | `phy_tx_time` (`?` nombre y unidades) | no se lee en las funciones analizadas |
| `+12` | 4 | `target_tx_time` (`?` nombre y unidades) | no se lee en las funciones analizadas |

Notas verificadas:

- El único byte de la cabecera fija que el parser consulta es `+6`. Todo lo demás entre `+7` y `+15`
  es opaco para el kernel: no se valida ni se usa para nada (`?` sobre si el firmware de Broadcom lo
  rellena; en el camino de Broadcom el contenido proviene de la propia trama recibida).
- El subtipo `0` y el `3` se cuentan por separado en los ivars del peer (`+160`, `+164`, `+168`), y
  combinados con el canal (cabecera de metadatos `+16`, es decir el byte de flags del
  `apple80211_channel` del `apple80211_act_frm_hdr`) deciden si el AF se considera UMI; el detalle de
  esa combinación queda `?` porque depende de la interpretación del canal en el header de metadatos.
- `+5` (versión) sólo se exige distinto de cero. Un driver puede emitir siempre el mismo valor; el
  kernel no compara contra una constante.

## 3. Formato TLV

Los TLVs se recorren con `IO80211TLVIterator<IO80211TLVHeader<unsigned char, unsigned short, 0, 1, 0>>`
(plantilla visible en `actionFrameReport`, `findTLVCursorForType<...>`). La cabecera es, por tanto:

```c
struct awdl_tlv_header {     // 3 B
    uint8_t  type;           // +0
    uint16_t len;            // +1  orden de máquina (little-endian en x86_64)
};                           // el cuerpo empieza en +3
```

- `type` es de 8 bits y `len` de 16; el iterador avanza `3 + len` y `tlvCheckBounds`
  (`IO80211AWDLPeer::tlvCheckBounds`) valida que `3 + len` no salga del cursor antes de despachar.
- Un TLV cuyo `3 + len` exceda el resto del buffer provoca el registro
  `"tlv parse error for Peer ... tlvType[%4u] tlvSize[%4lu] tlvlen[%4lu]"` y el abandono del parseo
  (`?` sobre si además descarta el AF completo).
- Las longitudes que escribe el manager en su propia plantilla (ioctl 151) usan el mismo formato, de
  modo que la plantilla del kernel y los TLVs del aire son intercambiables byte a byte.

## 4. Tipos de TLV que el parser reconoce

`actionFrameReport` recorre los TLVs y despacha con un `switch` de 34 casos en
`0xffffff80021b03ed`. Justo antes, en `0xffffff80021b03ce`, se ejecuta `add r15d, -2`, por lo que
**la etiqueta del `switch` es el tipo menos 2**. Los casos con su tipo real en el aire (decimal) y
el destino de cada uno:

| Tipo | Destino del caso | Efecto |
|---:|---|---|
| 2 | `processServiceResponseTLV` | respuesta de servicio |
| 4 | traza `logDebug` y salto a `0xffffff80021b0f02` | sync params (el parseo real es `parseAwdlSyncParamsTlv` `0xffffff80021b4ba6`, `?` sobre la condición exacta) |
| 5 | `parseAwdlElectionParamsV1` | parámetros de elección v1 |
| 6 | llamada virtual `+0x38` y `OSMetaClass::getClassName` | service params (`?` sobre el consumidor) |
| 7 | `IO80211Peer::parseHTCapTLV` | capacidades HT |
| 12 | se salta (ya leído antes por `findTLVCursorForType(12)`) | estado de datapath |
| 16 | salto a `0xffffff80021b0e3d` (`shouldLog` y traza) | ARPA (`?` nombre) |
| 17 | `parseAwdlIEEE80211CntnrTLV` | contenedor IEEE 802.11 |
| 18 | se salta (ya leído antes por `findTLVCursorForType(18)`) | secuencia de canales (`parseAwdlChanSeqFromChanSeqTLV` `0xffffff80021b5a08`) |
| 20 | `parseAwdlSyncTreeTLV` `0xffffff80021b56d0` | árbol de sincronía |
| 21 | `parseAwdlVersionTlv` | versión de AWDL |
| 23 | `parseNanSyncTlv` | sincronía NAN |
| 29 | sólo traza `logDebug` | `?` |
| 30 | `parseAWDLRangingTLV` | ranging |
| 31 | `parseAWDLRealTimeGroupTLV` (también buscado antes con `findTLVCursorForType(31)`) | grupo de tiempo real |
| 32 | `parseAwdlChanSeqFromChanSeqExtTLV` `0xffffff80021b5e84` (también buscado antes con `findTLVCursorForType(32)`) | secuencia de canales extendida |
| 33 | `parseAwdlPrefChanTLV` | canal preferido |
| 34 | `parseAwdlProModeTLV` | Pro Mode |
| 35 | `parseAwdlAirDropTLV` | AirDrop |

Los tipos 3, 8–11, 13–15, 19, 22 y 24–28 caen en el caso por defecto (`0xffffff80021b0cb2`): el
iterador avanza al siguiente TLV sin error. El tipo 24 (elección v2, el que construye el manager en
su plantilla) aparece ahí, por lo que su parseo se hace por otra ruta (`?`).

Búsquedas previas al recorrido con `findTLVCursorForType<...>`, con el tipo real en `r8d`:
`18` en `0xffffff80021afc5b`, `32` en `0xffffff80021afcdf`, `12` en `0xffffff80021affca` y `31` en
`0xffffff80021b006d`.

Con esta corrección la numeración coincide con la de los constructores del manager (ioctl 151):
6 service params, 7 enhanced data rate, 17 contenedor IEEE 802.11, 20 sync tree y 24 elección v2.
El tipo del TLV en el aire es exactamente el byte `type` de la cabecera. El parser tolera tipos
desconocidos: los ignora y sigue con el siguiente.

## 5. Campos que el driver debe rellenar por transmisión

Por cada action frame emitido:

1. **Cabecera fija de 16 B**: categoría en `+0`, OUI `00:17:F2` en `+1..+3`, `0x08` en `+4` y versión
distinta de cero en `+5`. Son los cuatro campos que el kernel valida; un valor incorrecto descarta
el AF con `"BAD OUI in AF"` y volcado del frame completo. El subtipo `+6` debe ser `0` o `3` para que
el AF se contabilice como AF normal o como UMI.
2. **TLVs a partir de `+16`**, cada uno con `{u8 type, u16 len}` y cuerpo de `len` bytes. El buffer no
debe contener el `frame control` ni el FCS.
3. **Tiempos relativos a la ventana de disponibilidad**: la cabecera fija no lleva ninguno de los
marcadores temporales que el manager usa para validar la línea temporal. Esos valores viajan fuera
de la trama: el arranque de ventana se publica por el evento 41 (`FwAwStartTime` big-endian en el
payload) y el periodo por el ioctl 116. Un driver sin firmware debe, por tanto, mantener su propio
reloj de ventana y derivar de él (a) el instante de repetición de la plantilla del 151 y (b) el
`AwNumber`/`FwAwStartTime` que reporta en el evento 41.
4. **Campo `target_tx_time`/`phy_tx_time` (`+8`/`+12`)**: el kernel no los lee (`?` su semántica), así
que pueden ir a cero sin afectar al parseo; sí pueden afectar a la sincronía fina entre nodos
reales, extremo que no se puede confirmar desde este binario.

## 6. Resumen operativo

- El buffer que se inyecta empieza en la categoría de la acción; los 16 B siguientes son cabecera
  fija y el TLV número uno empieza en `+16`.
- El kernel sólo valida categoría/OUI/tipo/versión (`+1..+5`) y el subtipo (`+6`); el resto de la
  cabecera fija es opaco.
- La cabecera TLV es de 3 B y la longitud es de 16 bits en orden de máquina.
- Ningún campo temporal de la trama se valida; la línea temporal se contrata por eventos e ioctls.
- Los tipos de TLV del aire coinciden con los del constructor del manager; el `switch` del parser
  trabaja sobre `tipo - 2`.
- Todo lo marcado `?` (semántica de `phy_tx_time`/`target_tx_time`, ruta del tipo 24 y el uso del
  subtipo combinado con el canal) queda abierto.
