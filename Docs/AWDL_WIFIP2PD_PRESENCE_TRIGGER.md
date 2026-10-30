# Disparadores de la publicación de presencia en `wifip2pd` (ioctl 415)

Qué evento del par hace que el daemon publique la presencia de un par AWDL y libere su cola PPP con
el ioctl 415 (`APPLE80211_IOC_POST_PRESENCE_DONE`). Complementa [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md)
§2, que localizó los dos emisores del 415 y dejó abierto qué dispara cada ruta. Cierra el punto
pendiente nº 1 de su §6. Base: `wifip2pd` de 25G241 (binario md5 `a120cfccfa2783303604ce5be9e1763a`).
Direcciones del binario salvo indicación. Lo no comprobado va con `?`.

## 1. Respuesta corta

El disparador primario es la llegada de un **action frame AWDL del par**: el kernel lo entrega al
daemon como evento 48 (`AWDL_ACTION_FRAME_RX`), el daemon lo decodifica como `DriverEvent.AWDL`
etiqueta 1 y publica la presencia del transmisor **una sola vez por par** (flag propio del par).
Tres rutas secundarias dan de alta o republican la presencia dentro del flujo de datos: datapath
establecido en el lado publicador, respuesta de datapath en el lado iniciador, y registro de tráfico
(set y recovery). Las cuatro convergen en el método de la tabla de vecinos que llama al método de
protocolo que envía el 415.

## 2. La tabla de vecinos y el método de publicación

`sub_1002823D0` es un método de `CoreP2P.WiFiInterface.NeighborDiscoveryTable` (metadata del tipo en
`0x100287ac0`; la de `NeighborDiscoveryTable.Entry` en `0x100287bb0`, símbolo mangled
`$s7CoreP2P13WiFiInterfaceC22NeighborDiscoveryTableV5EntryVMa`). La tabla es un diccionario
`MAC → Int?` (getter genérico `_$sSDyq_Sgxcig` en `0x100444b3e`; setter `_$sSDyq_Sgxcis` en
`0x100444b44`; `Dictionary.removeValue(forKey:)` en `0x100444ae4`; `_NativeDictionary._insert` en
`0x100445204`; búsqueda de hueco propia `sub_1002875B0`, llamada desde `0x100282807` y `0x100282873`).

- **Publicación de presencia** (`sub_1002823D0`): si la MAC **no tiene entrada** (etiqueta exterior
  del `Int??` devuelto por el getter igual a 1), llama al testigo 17 de la conformancia del protocolo
  — el método que envía el 415, `sub_1000CA0F0` con envío en `0x1000ca412` o `sub_1000D6000` con
  envío en `0x1000d6229` (AWDL_PEER_PRESENCE_PPP.md §2) — e inserta la entrada con valor cero/nulo
  (`0x10028293d`). Log de éxito: `"Posted peer presence for %s on %s"` (formato en `0x10049a020`,
  citado en `0x10028272e`, `_os_log_impl` en `0x100282748`). Si la entrada existe, no publica y solo
  garantiza que siga presente. Con la MAC de 6 B copiada a `0x100282574`-`0x1002825a0` desde el
  primer argumento `?` (leído del pseudocódigo).
- **Publicación de ausencia** (`sub_100282980`): sin entrada o valor ≤ 0 no hace nada; con valor
  igual a 1 borra la clave (`removeValue(forKey:)` en `0x100282bef`), llama al testigo 18 y registra
  `"Posted peer absence for %s on %s"` (citado en `0x100282d99`); con valor > 1 lo decrementa con el
  setter (`0x100282b79`). El `Int?` funciona como contador de referencias `?` (nadie de las rutas
  trazadas escribe conteos positivos; véase §7).
- **Borrado total** (`sub_100282E30`, `Dictionary.removeAll`): lo invocan las creaciones de interfaz
  (`0x1003ab880`, `0x1003ac420`, `0x1003aeee0` — log `"Failed to initially clear the peer presence
  table because %@"` en `0x1003b13cf` —, `0x1004158a0`, `0x100416d00`, `0x10041b1f0`) y el
  despachador de eventos del driver `sub_1004184D0`.

## 3. Las cuatro rutas

### 3.1 Action frames AWDL (evento 48): la ruta principal

Cadena: `sub_1004182B0` (entrada del manejador de eventos del driver; log `"Failed to dispatch event
%s because the AWDLInterface was deallocated before the event was received from the driver"` en
`0x100418449`) → `sub_1004184D0`, que conmuta sobre el enum `DriverEvent` con
`swift_getEnumCaseMultiPayload`.

- Caso 1 de `DriverEvent` = `DriverEvent.AWDL`. Dentro, caso 1 de `DriverEvent.AWDL`: decodifica una
  `CoreP2P.AWDLActionFrame.Header` (metadata `$s7CoreP2P15AWDLActionFrameV6HeaderVMa`, citada en
  `0x10041a229`) con `sub_100031330`; si el resultado es éxito (`and eax, 0C0000000h` /
  `cmp eax, 40000000h` en `0x10041a1f0`–`0x10041a1f5`), llama a `sub_10041C470` en `0x10041a216`.
  El caso 0 de `DriverEvent.AWDL` es otro evento (log `"Realtime mode updated %{bool}d"` en
  `0x100418a9f`); los casos 2 y siguientes no se trazaron `?`.
- Corresponde al evento 48 del kernel: la tabla de eventos de `AWDL_ETAPAS_2_A_4.md` asigna el 48
  (`len >= 0x48`, cabecera `apple80211_act_frm_hdr` de 72 B) a `DriverEvent.AWDL` etiqueta 1.

`sub_10041C470` (`0x10041c470`), con la cabecera del AF:

1. Dos guardas sobre `self` (la interfaz): el byte del ivar cuya posición da la entrada 760 de la
   tabla de offsets de ivars debe ser distinto de 0, y el de la entrada 712 debe ser 0
   (`sub_10041C470`, líneas del guard en el pseudocódigo; nombres de los Bools sin identificar `?`).
2. Gancho de estadísticas `sub_10040F120` con un campo de 4 B de la cabecera (2 + 2 bytes).
3. Localiza o construye el `AWDLPeer` del transmisor (caja con la metadata del tipo; MAC de 6 B con
   `sub_100172140`).
4. **Condición de publicación**: el Bool del struct del par (campo leído con el offset de la
   metadata, en caja) igual a 0. Entonces publica vía `sub_1002823D0` (llamada en `0x10041cc69`) y
   pone el Bool a 1 (la escritura no está localizada `?`); con fallo registra `"Failed to set the peer presence for %s: %@"` (formato en
   `0x1004a7f90`, citado en `0x10041cd97`). Si ya estaba a 1, no repite la publicación: por eso cada
   AF de un par conocido no genera un 415 nuevo.
5. Después llama a `sub_10041FD50` (recuperación de registros de tráfico, que desemboca en §3.4) y,
   si el AF lleva un `AWDLActionFrame.DataPathState` cuyo byte en `+64` difiere de 49 (0x31), busca
   la configuración de registro de tráfico (`sub_10014B180` con el mapa de
   `AWDLTrafficRegistrationConfiguration` de `sub_100068EA0`) y la activa con el testigo de la
   interfaz con argumentos (1, 1). Semántica exacta del byte y del 0x31 sin cerrar `?`.

### 3.2 Datapath establecido, lado publicador

`sub_10028F280` es la corrutina del gestor de datapath del publicador (sin llamadores directos: es
una función async; su tabla de funciones de reanudación vive en `0x10052e680`–`0x10052e6c0`, la
referencian `sub_10028F280` en `0x100293941` y `sub_100299AD0` en `0x10029a832`, y la reanudación
`sub_100298610` en `0x10052e6a8` es quien la retoma). Cadenas del ciclo: `"%@ received a new
datapath request from %s"` en `0x10028ffbc`, `"%@ datapath established with %s"` en `0x100290237`,
`"%@ received datapath confirmation with status %s"` en `0x100290cdb` y el selector
`publishDataConfirmedForHandle:localInterfaceIndex:serviceSpecificInfo:` `?` (VA del `objc_msgSend`
no capturado). Publica en dos sitios:

| Sitio | Contexto |
|---|---|
| `0x10029148e` | tras el establecimiento del datapath |
| `0x1002943f5` | tras la confirmación de publicación de datos |

Fallo: `"Failed to post peer presence for %s because %@"` (formato en `0x10049a960`, citado en
`0x1002915bc`). No se vio guardia previa en ninguno de los dos sitios `?`.

### 3.3 Respuesta de datapath, lado iniciador

`sub_1002FF710` es el gestor de datapath del iniciador (`"%@ received datapath response with status
%s from responder data address: %s [NMI: %s]"` en `0x10030305c`; `"NAN-DPResp: Peer %s, Self
gtk_required: %{bool}d, Peer gtk_required: %{bool}d"` en `0x10030344b` y `0x100303637`). Durante el
proceso de la respuesta llama a `sub_100305990` en `0x100302802` tras construir la MAC
(`sub_1003047A0`).

`sub_100305990` (`0x100305990`) opera sobre el `InitiatorInstance` del self (metadata
`$s17InitiatorInstanceC12PeerIdentityVMa`, llamada en `0x1003059c3`); la identidad del par —MAC de
6 B seguida de un Bool en `+6`— vive en el ivar cuyo offset saca de la clase en `+0xA0` de la tabla
de offsets. **Condición**: si el Bool en `+6` vale 1 (cmp en `0x100305aa7`), la presencia ya se
publicó y retorna; si no, publica (llamadas a `sub_1002823D0` en `0x100305bc9` y `0x1003063c4`),
escribe 1 en un Bool propio de la sesión y registra el fallo con el mismo formato `0x10049a960`
(citado en `0x100305cef`).

### 3.4 Registro de tráfico

El ejecutor es `sub_1002A7790` (`"Successfully %s traffic registration for %s to %s"` en
`0x10049b300`/`0x1002a7fa9`; `"Successfully updated traffic registration options for %s to %s"` en
`0x10049b380`/`0x1002a7aa0`; `"Failed to %s traffic registration for %s to %s because %@"` en
`0x10049b340`/`0x1002a7d32`; llamado por `sub_1002B63F0`, a su vez desde `sub_1002A6FF0`). Con la
operación de tipo *set* (byte igual a 1) y con la comprobación de clase de servicio
`sub_1002A2630` distinta de 0, llama a `sub_10040B650` en `0x1002a7923`; con operación de borrado
pasa a `sub_10040CD80` (ausencia, §5).

`sub_10040B650` (`0x10040b650`):

- **Condición**: el `uniqueIdentifier` del servicio de registro no es
  `AWDLTrafficRegistrationServiceAirPlayConnectivity` (dos comparaciones contra la constante global
  del mismo nombre hacen retorno temprano) y su clase, según `sub_10014A980`, es 2. La semántica de
  la clase 2 (parece corresponder a servicios AWDL) sin cerrar `?`.
- Publica vía `sub_1002823D0` en `0x10040c454`. Cadenas: `"Bypass conflict check traffic
  registration %s to %s for watchdog recovery"` en `0x1004a6bb0`/`0x10040bff6`; `"EdgeKit is not
  supported"` en `0x1004a6b90`/`0x10040c298`; si la publicación falla, deshace con `"Will clear
  traffic registration for %s to %s because failed to post peer presence on %s: %@"` en
  `0x1004a6b30`/`0x10040c63b`.
- Otros dos llamadores: `sub_100410830` (llamada en `0x1004108dd`; cadena de contexto
  `"P2P Controller.queryNANDataPathState for XPC Session: %@"` en `0x10036930b`, vía
  `sub_100368C60` ← `sub_10036E130` — consultas XPC del P2P Controller `?`) y `sub_10042A000`
  (llamada en `0x10042a3f4`; `"Attempting to recover traffic registration for %s to %s"` en
  `0x1004a82c0`/`0x10042a33d` y `"Successfully recovered traffic registration for %s to %s"` en
  `0x1004a8280`/`0x10042a7c2`; su llamador `sub_100444050` cuelga de `sub_10041FD50`, invocada en
  §3.1 paso 5 tras cada action frame).

## 4. Resumen

| Ruta | Disparador | Condición | Llamada a `sub_1002823D0` | Log propio (formato) |
|---|---|---|---|---|
| §3.1 | Evento 48 (AF AWDL) → `DriverEvent.AWDL` tag 1 → `AWDLActionFrame.Header` correcta | 2 Bools de la interfaz (ivars 712/760) y Bool "publicado" del `AWDLPeer` igual a 0 | `0x10041cc69` | `"Failed to set the peer presence for %s: %@"` `0x1004a7f90` |
| §3.2 | Datapath establecido / datos publicados, lado publicador | ninguna vista `?` | `0x10029148e`, `0x1002943f5` | `"Failed to post peer presence for %s because %@"` `0x10049a960` |
| §3.3 | Respuesta de datapath (DPResp), lado iniciador | `PeerIdentity+6` distinto de 1 (`0x100305aa7`) | `0x100305bc9`, `0x1003063c4` | `"Failed to post peer presence for %s because %@"` `0x10049a960` |
| §3.4 | Registro de tráfico SET y recovery (también tras cada AF, §3.1) | clase de servicio 2 y no AirPlayConnectivity | `0x10040c454` | `"Bypass conflict check traffic registration %s to %s for watchdog recovery"` `0x1004a6bb0` |

El log de éxito lo emite siempre la tabla de vecinos: `"Posted peer presence for %s on %s"`
(`0x10049a020`). Los dos emisores del 415 registran además `"Post peer presence for %s"`
(`0x100495b30`) en `0x1000ca2fa` y `0x1000d615c`.

## 5. La ausencia, para contraste

`sub_100282980` (§2) la disparan: `sub_100298E00` (cierre de datapath del lado publicador;
`"Failed to post peer absence for %s because %@"` en `0x1002992e1`), `sub_10030D630` (lado
iniciador; en `0x10030db93`), `sub_10040CD80` (borrado de un registro de tráfico;
`"Failed to clear peer %s on %s: %@"` en `0x10040d267`, con la misma clase de servicio 2 como
condición) y `sub_100427D70` (vencimiento por inactividad: `"Aged out %s with total traffic
registrations: %ld, total responder traffic registrations: %ld"` en `0x100428026` y
`"Failed to post peer absence for %s: %@"` en `0x100428294`).

## 6. Implicaciones para el driver RTL8814

1. El flujo completo depende del evento 48: sin AF entregados al daemon no hay presencia, ni 415, ni
   liberación de la cola PPP de `IO80211AWDLPeer`. Coherente con el orden del plan: desvío RX e
   inyección de AF (pasos 2–3) antes que nada.
2. La publicación se repite por ruta: una vez por par por action frame, más una por sesión de
   datapath y una por registro de tráfico. El kernel lo tolera porque cada 415 solo libera la cola
   PPP del par (`setAwdlPostPeerPresenceDone` `0xffffff800221c978` → `pppDone(true)`).
   `pppDone(true)` solo recorre la cola si no está vacía, y tras el primer 415 el flag `+1758` del
   par ya vale 1, así que `IO80211AWDLPeer::skywalkInputPacket` deja de retener paquetes
   ([AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §2 y §3). Los 415 repetidos no entregan
   nada nuevo.
3. La condición de una sola publicación por par en §3.1 (Bool en la estructura del par) es estado
   del daemon: el driver no debe asumir un 415 por cada AF.

## 7. Pendiente

- Nombres de los dos Bools de la interfaz (entradas 712 y 760 de la tabla de offsets de ivars de la
  clase de §3.1) y del flag "presencia publicada" del struct `AWDLPeer` `?`.
- Quién escribe conteos positivos en el diccionario del NDP (los ~33 usuarios del setter genérico
  `0x100444b44` incluyen `sub_10040AEC0`, que toca un diccionario de
  `AWDLTrafficRegistrationConfiguration`/`P2PTimer` por UUID en los ivars 616/632/864 de la
  interfaz, no el NDP) `?`.
- Detalle del byte de `AWDLActionFrame.DataPathState` que activa el registro de tráfico y por qué el
  valor 49 lo excluye `?`.
- Qué AFs llegan al daemon como evento 48 (condición UMI de subtipo + canal): sigue abierto en
  `AWDL_ETAPAS_2_A_4.md` (punto E3).
