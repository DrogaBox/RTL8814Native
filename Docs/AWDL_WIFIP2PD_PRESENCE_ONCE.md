# El flag «presencia publicada» de `AWDLPeer` y el byte `DataPathState+64` en `wifip2pd`

Cierra los dos puntos de [AWDL_WIFIP2PD_PRESENCE_TRIGGER.md](AWDL_WIFIP2PD_PRESENCE_TRIGGER.md) §7
sobre el Bool que limita la publicación de presencia a una vez por par y el byte `+64` del
`AWDLActionFrame.DataPathState`. Base: `wifip2pd` de 25G241, IDB copiado en `/tmp/awdl_once`
(origen `~/re_cache/ida/wifip2pd_25G241.i64`). Direcciones del binario salvo indicación. Los
nombres de campos y casos salen de los field descriptors de reflection Swift
(`__swift5_fieldmd`), no de supuestos; lo no comprobado va con `?`.

## 1. Respuesta corta

- El Bool es el campo **`peerPresencePosted`** (índice 23, tipo `Bool`) del struct
  `CoreP2P.AWDLPeer` (field descriptor en `0x1004c7898`). Todo acceso pasa por el field offset
  vector del metadata: `movsxd r, dword ptr [metadata+6Ch]` (el slot del campo 23). El offset en
  bytes dentro del struct no es estáticamente visible: el tipo se instancia en runtime (struct
  resiliente) y el código del módulo nunca lo usa como constante.
- Nace a **0** en el init del struct (`0x10016f71f`) y pasa a **1** en la ruta del evento 48
  tras publicar presencia (`0x10041ce49`). El par modificado se persiste en el almacén de pares
  de la interfaz (un `Dictionary`) en `sub_10041FD50` (`0x100420123`), único llamador del setter
  del almacén en todo el binario.
- Vuelve a **0** por tres caminos: el init de cualquier `AWDLPeer` nuevo, el aged-out (el par se
  saca del almacén por clave y se reconstruye), y la recuperación de la interfaz (reset masivo del
  flag en todos los pares del almacén tras el borrado total del NDP). El «Driver interface was
  removed» no toca el flag del par.
- La comparación de `0x100305aa7` (ruta DPResp del iniciador) **no es este flag**: es otro Bool
  dentro de un ivar del objeto de sesión del iniciador, estado separado del almacén de pares
  (corrección de §3.3 del doc de disparadores).
- El byte `DataPathState+64` es el **tag inline de `unicastOptions: UMIOptions?`** (campo 8 del
  field descriptor de `DataPathState`): `0x31` (49) es `nil` y los valores `0x00`–`0x30` son los
  casos del enum `UMIOptions`. No gobierna presencia ni ausencia: decide si el evento 48 activa un
  registro de tráfico.

## 2. El struct `AWDLPeer` y su almacén

Field descriptor de `CoreP2P.AWDLPeer` en `0x1004c7898` (26 campos, records de 12 B con flags 2,
nombres verificados): `logger`, `macAddress`, `lastUpdated`, `numberOfActionFrames`,
`deprecatedElectionParameter`, `electionParameter`, `serviceParameter`,
`lastProcessedServiceUpdateIndicator`, `synchronizationParameter`, `arpa`,
`dataPathStateParameter`, `nanSynchronization`, `version`, `realtimeGroup`, `preferredChannels`,
`advertisedServiceIDPool`, `advertisedServices`, `syncContention`, `mactime`, `average2_4RSSI`,
`average5RSSI`, `average6RSSI`, `reachability`, **`peerPresencePosted`** (`Sb`),
`needsToRecoverTrafficRegistrations` (`Sb`), `needsToRecoverResponderTrafficRegistrations`
(`Sb`). El enum `AWDLPeer.Reachability` (descriptor `0x1004c79e0`) tiene los casos `routable`
(tag 0), `observed` y `notReachable`.

Los tres Bools finales se leen del metadata como `movsxd rax, dword ptr [metadata+slot]` con
slots `+6Ch` (campo 23), `+70h` (24) y `+74h` (25).

El almacén de pares es un `Dictionary` ivar del objeto self de las funciones de presencia
(acceso estándar `[clase+1C0h]` en `0x10040f183`, `0x1004200f1`, `0x100428fb0`, con
`swift_beginAccess` READ en `0x10040f199`, MODIFY en `0x100420110` y `0x10041a6d9`). El lookup se
hace en `sub_10040F120` (`0x10040f1cc` con los comparadores `sub_10013BF40`/`sub_1000270F0`,
elemento por stride con `imul rbx, [metadata-8+48h]` en `0x10040f1f6`); si no existe, construye
un par con el init `sub_10016F4C0` (`0x10040f2b0`). La correspondencia exacta entre el slot
`+1C0h` y un campo del field descriptor de `wifip2pdCore.AWDLInterface` (FD `0x1004d1ad0`, campo
0 `peers`, un `Dictionary`) no cierra por el mapeo del `FieldOffsetVectorOffset` `?`; el uso como
almacén queda acreditado por los cuatro accesos citados y por el remove por clave del §4.2.

## 3. Escritores y lectores de `peerPresencePosted`

Barrido completo: las 91 funciones que referencian el accessor de metadata de `AWDLPeer`
(`0x10016e690`) más los callers directos de los accessors de propiedad (ninguno: el uso es
siempre inline). Accesos al slot `+6Ch`:

| Función | VA | Operación |
|---|---|---|
| `sub_10016F4C0` (init de `AWDLPeer`) | `0x10016f71b`–`0x10016f71f` | `mov byte [peer+FOV], 0`: a cero |
| `sub_10016F350/370/390` (getter/setter/`_modify` de la propiedad) | `0x10016f37b`, `0x10016f39f`, `0x10016f3cb` | accessor estándar; sin callers directos |
| `sub_10041C470` (evento 48) | `0x10041cbc2`–`0x10041cbc6` | lectura: `cmp byte [box+FOV], 0` |
| `sub_10041C470` (evento 48) | `0x10041ce49` | escritura: `mov byte [box+FOV], 1` |
| `sub_100427D70` (aged-out) | `0x100428105`–`0x10042810e` | lectura: `cmp byte [peer+FOV], 1` |
| `sub_100428F70` | `0x100428f80`–`0x100428f84` | escritura: `mov byte [peer+FOV], 0` |
| `sub_10016F7C0` | — | referencia el accessor; no toca el slot `+6Ch` |

### 3.1 La ruta del evento 48 (publicación única)

En `sub_10041C470` (única llamada desde el dispatcher en `0x10041a216`):

1. Dos guardas de la interfaz sobre el self: `[clase+2F8h]` debe ser distinto de 0
   (`0x10041caf0`–`0x10041caff`; si es 0 salta a `0x10041fd35` sin procesar) y `[clase+2C8h]`
   debe ser 0 (`0x10041cb08`–`0x10041cb13`). Son accesos por field offset vector de la clase del
   self; sus nombres no aparecen en ningún field descriptor consultado `?` (los valores 760 y 712
   del doc de disparadores son los mismos offsets en decimal).
2. El box del par se obtiene de `sub_10040F120` (`0x10041cb64`): copia el par del almacén o crea
   uno nuevo (con `peerPresencePosted = 0`).
3. Si el Bool vale 0 (`0x10041cbc6`), llama a `sub_1002823D0` (`0x10041cc69`, publicación en el
   NDP y envío del 415) y escribe 1 (`0x10041ce49`). Si vale 1 salta directo a `0x10041ce87` sin
   publicar.
4. `sub_10041FD50` (`0x10041ced8`) persiste el par: `swift_beginAccess` MODIFY sobre el almacén
   (`0x100420106`–`0x100420110`) y escritura con `sub_1003A7E90` (`0x100420123`; único llamador
   del setter en el binario, verificado por barrido de `E8`). En la misma función se manejan los
   otros dos Bools del par (`needsToRecoverTrafficRegistrations`, slot `+70h`, lectura en
   `0x100420149`) y sus recovery (`sub_100421410`).

### 3.2 Aged-out (`sub_100427D70`)

El aged-out se invoca desde el dispatcher con un par decodificado del evento (`0x10041ab67`) y
desde `sub_1004291D0` (`0x1004292b1`, la limpieza posterior a la recuperación de la interfaz). En
el dispatcher, el caso del jumptable que lo conduce (case 8 en `0x10041a6a5`) hace:

- `swift_beginAccess` MODIFY sobre el almacén (`0x10041a6cc`–`0x10041a6d9`) y remove por la MAC
  del evento con `sub_1003A82F0` (`0x10041a6eb`): el par sale del almacén.
- Si el par existía, se decodifica (`sub_100031330`) y se pasa al aged-out (`0x10041ab67`).

Dentro del aged-out: reconstruye un `AWDLPeer` con el init (`0x1004280d1`) y copia los
`advertisedServices` del viejo (`sub_10035B720`, `0x1004280e6`); luego lee el Bool del par viejo
(`0x100428105`): si valía 1, publica la ausencia con `sub_100282980` (`0x10042817e`, el método de
`"Posted peer absence for %s on %s"`); si valía 0, salta a `0x100428343` sin ausencia. El log
propio es `"Aged out %s with total traffic registrations: %ld, total responder traffic
registrations: %ld"` (formato citado en `0x100428026`) y el de fallo
`"Failed to post peer absence for %s: %@"` (en `0x100428294`).

### 3.3 Reset masivo en la recuperación de la interfaz

En el dispatcher `sub_1004184D0`, el evento de estado de la interfaz (case 4 del switch de
`DriverEvent`, con un subestado en el payload) tiene la rama de interfaz lista:

- `sub_100282E30` (`0x10041b079`): `Dictionary.removeAll` de la tabla de vecinos (el borrado total
  de `sub_1002823D0`/`sub_100282980` del doc de disparadores §2).
- Log de error `"Failed to clear all previous peer presence after interface recovery"` (formato
  cargado en `0x10041b0d3`, `_os_log_impl` en `0x10041b0e9`).
- Si no hubo error: `sub_100428F90` (`0x10041b127`), que itera el almacén completo
  (`swift_beginAccess` en `0x100428fc6`, `_HashTable.startBucket.getter` en `0x100429001`) y
  aplica el callback `sub_100428F70` a cada par: `mov byte [peer+FOV], 0` en `0x100428f84`. Todos
  los `peerPresencePosted` vuelven a 0.
- Después: `sub_1004291D0` (`0x10041b12c`), el Bool de la interfaz a 0
  (`mov byte [self+FOV], 0` en `0x10041b13c` con slot `[clase+2C8h]`) y `sub_1004292F0`
  (`0x10041b142`). Los logs de la misma rama del dispatcher: `"Failed to recover the low latency
  interface %s: %@"` (formato cargado en `0x100419a2a`) y `"Failed to suspend AWDL after
  interface recovery: %@"` (formato cargado en `0x10041aeb7`).

### 3.4 Interface down no toca el flag

La rama `"Driver interface was removed"` (jumptable case 6, writes en `0x100419371`–
`0x100419398`) solo toca los tres Bools de la interfaz: el del slot `2C8h` a 1, el del `2D8h` a 0
y el del `2D0h` a 0. El `peerPresencePosted` de los pares no cambia. El efecto es indirecto: con
el Bool del slot `2C8h` a 1, los guardas de `sub_10041C470` (`0x10041caff`/`0x10041cb13`) abortan
todo el procesado del evento 48 hasta la recuperación.

### 3.5 ¿Cuándo puede repetirse el 415 para un mismo par?

| Suceso | Efecto sobre `peerPresencePosted` | Nuevo 415 posible |
|---|---|---|
| Primer AF del par (par nuevo o recreado) | 0 → publica → 1 | el primero |
| AFs siguientes mientras el par persista | sigue 1 | no |
| Aged-out del par (case 8 del dispatcher, inactividad de registros) | el par se elimina del almacén; se reconstruye con 0 y se publica ausencia si tenía 1 | sí, con el próximo AF |
| Recuperación de la interfaz (subestado «ready») | reset masivo a 0 + `removeAll` del NDP | sí, para todos los pares |
| «Driver interface was removed» | sin cambio (solo los Bools de la interfaz) | no (además los guardas bloquean el 48) |
| Destrucción del box sin remove del almacén | el valor persiste en el almacén | no |

## 4. La comparación de `0x100305aa7` es otro Bool

`sub_100305990` (única llamada desde `sub_1002FF710` en `0x100302802`, gestor de la respuesta de
datapath del iniciador) opera sobre el objeto `r13` que el llamador le pasa:

- `0x100305a38`–`0x100305a3f`: guarda sobre el ivar del self cuyo offset sale de
  `[clase+0D8h]` (`cmp byte [r13+r12], 0`, a 1 en `0x100305adc` tras publicar).
- `0x100305a4b`–`0x100305a58`: `cmp byte [r13+rax+10h], 3` sobre el ivar del self cuyo offset sale
  de `[clase+0D0h]`.
- `0x100305a92`–`0x100305aa7`: `and rax, isaMask` sobre `[r13+0]`, luego
  `mov rax, [clase+0A0h]` y `cmp byte [r13+rax+6], 1`: el byte +6 del ivar (MAC de 6 B en
  `+0..+5`, extraída en `0x100305aca`–`0x100305ad9`). Si vale 1, la función retorna sin publicar
  (`0x100305aaf`); si no, publica (`sub_1002823D0` en `0x100305bc9` y `0x1003063c4`) y pone a 1
  el flag del otro ivar (`0x100305adc`).

Ese Bool vive en el ivar de un objeto clase (offsets del field offset vector de su clase),
no en el box de `AWDLPeer`, y por tanto no es `peerPresencePosted`. La metadata
`$s17InitiatorInstanceC12PeerIdentityVMa` que aparece en `0x1003059c3` es un enum de dos casos
(`pairingKeystoreID`, `pairedDeviceID`, field descriptor `0x1004cfda8`) usado como valor, no el
tipo del ivar testeado: `InitiatorInstance` tiene 30 campos (FD `0x1004cfc30`) y los slots
`0A0h`/`0D8h` (índices 36 y 50 sobre un vector en `+10h`) quedan fuera de rango. La clase exacta
del objeto `r13` no quedó identificada por nombre `?`; el llamador instancia además
`wifip2pdCore.NANPeer` (accessor llamada en `0x1002ff77f`).

Consecuencia práctica: las rutas del evento 48 y de la DPResp llevan cada una su propio flag de
«presencia ya publicada»; el del evento 48 es el que gobierna el 415 principal.

## 5. El byte `DataPathState+64`: `unicastOptions: UMIOptions?`

Field descriptor de `CoreP2P.AWDLActionFrame.DataPathState` en `0x1004c4bcc` (33 campos):
`countryCode`, `socialChannelMap`, `infraBSSID`, `infraChannel`, `infraAddress`, `infraSSID`,
`awdlAddress`, `isUnicast`, **`unicastOptions`** (`Optional` del enum `UMIOptions`),
`unicastPeerCachingIndication`, `portUniqueLogID`, `rootLoopFreeCounter`, `supportsDualBand`,
`isAirPlaySink`, `followChannelSequence`, `supportsAirPlaySolo`,
`supportsUnicastMasterIndication`, `infraRealTimeActive`, `isRangeable`,
`supportsSimultaneousDualBand`, `supportsDFSProxy`, `assistedDiscoveryActive`,
`supportsSocialChannelMap`, `nanConcurrencyEnabled`, `supportsHighEfficiencyRate`,
`isSidekickHub`, `supports6GHz`, `supportsUltraLowLatencyInfra`, `proModeActive`,
`hasChannelSequence`, `awdlOnSince`, `availabilityWindowSequenceNumber`,
`actionFrameUpdateCounter`. El enum `UMIOptions` (FD `0x1004c4d68`, 49 casos):
`legacyAirPlayStart`, `legacyAirPlayStop`, `realtimeAirPlayStart`, `realtimeAirPlayStop`,
`realtimeAirplayStartBackwardsCompatibility`, `recoverAirPlayStart`,
`channelSequenceUpdate`, `channelSequenceChangeRequested`,
`channelSequenceChangeAcknowledged`, `retryDFSAirPlay`, `dfsAirPlayAllowed`,
`airPlayOnDFSDenied`, `chainSwitch`, `multiPeerSteeringCommand`, `multiPeerSteeringFailure`,
`realtimeStartRequest`, `realtimeStopRequest`, `highThroughputStart`, `highThroughputStop`,
`rangingStartRequest`, `rangingStart`, `rangingStop`, `universalControlStart`,
`universalControlStop`, `legacySidecarStart`, `legacySidecarStop`, `sidecarStart`,
`sidecarStop`, `unusedSidecarRequestBackground`, `unusedSidecarRequestForeground`,
`fastDiscovery`, `recovery`, `disconnectedInfrastructureForRetroMode`, `mpRemoteCameraStart`,
`mpRemoteCameraStop`, `edgeKitStart`, `edgeKitStop`, `remoteCameraStart`, `remoteCameraStop`,
`tvRemoteCameraStart`, `tvRemoteCameraStop`, `remoteScreenStart`, `remoteScreenStop`,
`spatialStreamingStart`, `spatialStreamingStop`, `macVirtualDisplayStart`,
`macVirtualDisplayStop`, `continuityRealTimeGroupStart`, `continuityRealTimeGroupStop`.

El enum agota el byte de tag (49 casos, tags 0x00–0x30), así que el `Optional` guarda su caso
`nil` con el tag 49 = **0x31**. Evidencia:

| Función | VA | Prueba |
|---|---|---|
| init por defecto `sub_10010BD60` | `0x10010be20` | `mov byte [rbx+40h], 31h`: el campo nace a `nil` (el resto del layout del init: `+00` word 0, `+02` byte 1, `+04` dword 0, `+08` word 3, `+0A` dword 0, `+0E` word 0, `+10` byte 1, `+14` dword 0, `+18` word 3, `+1A` dword 0, `+1E` word 0, `+20` byte 1, `+28` 16 B a cero, `+36` qword 0 (una `Address`), `+3E` byte 1, `+42` word 0, `+44` byte 1, `+48` dword 0, `+4C` byte 1; los tres últimos campos van por FOV `[metadata+88h/8Ch/90h]` con `storeEnumTagSinglePayload(1,1)` para `awdlOnSince = nil`) |
| Decodable `sub_10010C780` | `0x10010c44f`–`0x10010c4aa` | `decode(UInt16)`, `decode(UInt32)`; bit 0 del u32 → byte `+41h` (`unicastPeerCachingIndication`); `(rawValue & ~1)` → `sub_10011C2B0` (`init(rawValue:)` del enum) → tag en `+40h` |
| `==` (`sub_10011A5B0`) | `0x10011a9bc`–`0x10011a9d2` | ambos `0x31` son iguales entre sí (ambos `nil`); el resto se compara por valor |
| `hash(into:)` (`sub_10010B670`) | `0x10010ba85`–`0x10010bab4` | `0x31` se hashea como `nil` (tag 0, sin payload); cualquier otro valor combina el rawValue `u32` de la tabla `unk_10046BE74` |

### 5.1 Qué hace el byte en la ruta del evento 48

En `sub_10041C470`, después de la publicación:

1. El último `DataPathState` recibido se guarda en el campo `dataPathStateParameter` del par
   (FOV slot `+38h` del metadata, campo 10): `0x10041cee1`. La extracción del opcional se hace
   con `sub_100031390` (`0x10041cefd`) y el test de `nil` con
   `getEnumTagSinglePayload` del value witness del `Optional<DataPathState>` (`0x10041cf04`–
   `0x10041cf24`).
2. Si el `DataPathState` no es `nil`, se lee el byte: `movzx ebx, byte [r15+40h]`
   (`0x10041cf46`), se decodifica el valor (`sub_1000312D0`, `0x10041cf55`) y se compara con
   `0x31` (`cmp bl, 31h` en `0x10041cf5a`).
3. Si el byte **es** `0x31` (`unicastOptions == nil`): no se hace nada más (`jz 0x10041d140`).
4. Si **no** lo es: busca la configuración de registro de tráfico con el mapa de
   `AWDLTrafficRegistrationConfiguration` (`sub_100068EA0`, `0x10041cf73`) y el tag como clave
   (`movzx esi, bl` en `0x10041cf78`; `sub_10014B180` en `0x10041cf89`). Si existe, la activa con
   el testigo de la interfaz y los argumentos `(1, 1)` (llamada indirecta en `0x10041cfc8`).

El mismo byte lo consulta la recuperación de registros de tráfico `sub_100421410` (lectura en
`0x10042179b`, ruta invocada tras cada evento 48 por `sub_10041FD50`).

Semántica: un `UMIOptions` no nulo en el `DataPathState` del par declara la opción unicast
(AirPlay, sidecar, ranging, …) que el par mantiene activa, y el daemon activa el registro de
tráfico correspondiente (que a su vez publica presencia por la ruta de `sub_10040B650` del doc de
disparadores §3.4). Un `unicastOptions` nulo no activa registro. **El byte no participa en la
publicación de presencia ni de ausencia**: ni `sub_1002823D0` ni `sub_100282980` lo miran; su
única puerta es el registro de tráfico.

## 6. Implicaciones para el driver RTL8814

1. Con el flujo del evento 48 correcto, `wifip2pd` emite el 415 una vez por par mientras el par
   persista en su almacén. No hay que esperar un 415 por action frame ni tratar su ausencia como
   fallo de inyección: la segunda publicación del par conocido se corta en `0x10041cbc6`.
2. Tras una recuperación de la interfaz (por ejemplo un watchdog del driver o un reset que el
   daemon ve como «ready»), el daemon borra el NDP y resetea todos los flags: el próximo AF de
   cualquier par vuelve a producir 415. Un reinicio del driver repuebla la presencia sin estado
   residual.
3. El aged-out elimina el par por inactividad de sus registros de tráfico: un par que deja de
   mandar AFs con `UMIOptions` o servicios pierde su flag y repite la secuencia de presencia al
   reaparecer.
4. Para que el daemon active registros de tráfico (y con ellos la ruta de presencia del §3.4 del
   doc de disparadores), el `DataPathState` del AF debe llegar con un `UMIOptions` real: el
   rawValue `u32` del TLV del que decodifica `sub_10010C780` (bit 0 = `unicastPeerCachingIndication`,
   bits 1+ = el `init(rawValue:)` de `UMIOptions` en `sub_10011C2B0`). Un `DataPathState` sin
   esa parte deja el byte en `0x31` y el registro no se activa.
5. Los TLVs deben llegar con el parseo del kernel intacto: si `actionFrameReport` marca error en
   `ctx+0x2C`, el evento 48 no sale y ningún flag del daemon llega a moverse
   ([AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md) §2).

## 7. Pendientes (`?`)

- Nombre exacto de la clase del self de `sub_10041C470`/`sub_1004184D0` y del ivar del almacén de
  pares (slot `[clase+1C0h]`). El FD de `wifip2pdCore.AWDLInterface` (FD `0x1004d1ad0`, campo 0
  `peers`, campo 44 `allServicesRegisteredDuringCurrentSession` — este último alinea exactamente
  con el slot `268h` del aged-out) sugiere la clase, pero el mapeo del `FieldOffsetVectorOffset`
  del nominal descriptor (`0x1004bddf8`, valor 110) no cierra con los slots `1C0h`/`2C8h`/`2F8h`.
- Nombres de los dos Bools de la interfaz (slots `2C8h` y `2F8h` del field offset vector de la
  clase del self).
- `sub_10014B180` (búsqueda de la `AWDLTrafficRegistrationConfiguration` por tag de `UMIOptions`)
  y la tabla de rawValues `unk_10046BE74`: qué rawValue `u32` corresponde a cada caso.
- `sub_1004291D0`/`sub_1004292F0` (limpiezas llamadas dentro de la recuperación de la interfaz) y
  el nombre del caso `DriverEvent` del jumptable que conduce al aged-out (case 8 del
  `0x100418a43`).
- El caso del jumptable del dispatcher con la MAC que alimenta el aged-out: si es un evento
  «traffic registration aged out» del driver o un vencimiento interno del daemon.
