# AWDL: resultado de la primera prueba con un iPhone

Prueba del 2026-11-08 con un iPhone de visita (iOS 27, sin modo desarrollador, sin cable) frente al Mac con
el T9UH, macOS 26.7.1 (25G241). El guion es [AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md). Las
direcciones del documento salen de la IDB del kernelcache de 25G241 (`boot_awdl.kc.i64`).

Las capturas y los registros de la prueba tienen datos del dueño del iPhone y quedan fuera del repositorio.

## 1. Resultado

| Sentido | Estado | Capa |
|---|---|---|
| iPhone → Mac, foto | Funciona: BLE, mDNS, QUIC/HTTP3 y TCP, `HELLO`, `ASK` con aviso para aceptar, `UPLOAD` | 001EF268 |
| iPhone → Mac, video de 64,3 MB | Funciona: 80 s, unos 800 KB/s sostenidos | C84B286E (94e83e7) |
| iPhone → Mac sin `ndp -s` manual | Funciona con `Tools/iphone_test.sh run` (e287ac3) | 44952481 (7333d24) |
| Mac → iPhone | El iPhone no aparece en la hoja de AirDrop del Mac; causa y corrección en §3, comprobada con un par virtual | 44952481 |

## 2. Defectos encontrados y corregidos

### 2.1 Tramas de datos con envoltura de longitud (94e83e7)

iOS manda sus tramas de datos grandes (QoS data a MCS 0/1, bit A-MSDU a 0) con una cabecera RFC 1042 delante
del SNAP de Apple: `AA AA 03 00 00 00 LL LL`, con `LL LL` igual a la longitud de la MSDU AWDL que sigue. La
capa las descartaba como `bad`. Una captura del A53 durante una subida contó 620 de 1370 tramas del iPhone
al Mac con ese formato, y ninguna llegó a `awdl0`. Con la corrección, la línea de RX de la capa muestra
`wrapped` y el video pasó a unos 800 KB/s sin descartes.

### 2.2 Modo de presencia rechazado (7333d24)

`IO80211AWDLPeerManager::setPresenceMode` (`0xffffff80021cc5a6`) manda el ioctl 200 al driver y guarda el
modo en el byte `+17032` de sus ivars solo si el driver lo acepta. `configure()` pone ese byte a 0 antes, en
`0xffffff80021c6175`. La capa respondía `kIOReturnUnsupported`, así que el modo quedaba en 0. El manager pide
4.

`actionFrameProcessInput` (`0xffffff800220e8b0`) compara la secuencia de AW de cada trama de un par en caché
con la AW del último evento 41 (`+16104`), que llega una vez por franja:

```
window - 2 <= seq <= window + presenceMode    → SyncErrors (peer +280) = 0
fuera de ese rango                            → SyncErrors++ (solo en MI) y línea SYNC_INFO
```

Con el modo en 0, toda trama del iPhone a partir de la segunda AW de la franja contaba como error. El
registro de la familia mostraba `SYNC_INFO ... SyncErrors(N)` subiendo unas ocho veces cada 4 s. Con la capa
44952481 el manager recibe 4, la capa lo acepta y no aparece ninguna línea `SYNC_INFO` con el iPhone.

`rtg` e `isRTG` en esas líneas se refieren a pares de tiempo real (RTG), no a si el par es alcanzable.

### 2.3 Entrada de vecino ausente

El SYN del iPhone llegaba al Mac (`SYN_RCVD`), pero el SYN-ACK no salía hasta cargar a mano
`ndp -s fe80::<EUI-64>%awdl0 <MAC>`. `awdl0` no envía solicitudes de vecino. En el kernelcache nadie llama a
`ifnet_notice_node_presence` (`0xffffff8000643ca0`, 0 referencias); la tabla de vecinos de AWDL la lleva
`wifip2pd` (cadenas `NeighborDiscoveryTable`, `Posted peer presence for %s on %s`). Mientras no se resuelva
el aviso de presencia (§3), `Tools/iphone_test.sh run` carga una entrada permanente por cada fuente que oye
la radio.

## 3. Causa de que el Mac no descubra al iPhone: la entrada de vecino nunca se publica
El iPhone no anuncia su receptor de AirDrop por mDNS multicast: en 15 minutos de captura en el canal 149
mandó una sola trama multicast. Lo anuncia dentro de sus tramas de acción AWDL, en el TLV 2 (respuesta de
servicio): `<id>._airdrop._tcp` con nombre comprimido (`c0 07`) y TXT `flags=111611`, en más de 1700 tramas.
`wifip2pd` arranca `AWDLBrowse _airdrop._tcp.local` al abrir la hoja de AirDrop, pero solo resuelve un
servicio de un par que haya dado por descubierto, y esa misma publicación es la que crea la entrada de vecino
de `awdl0` (§2.3). Las dos faltas tienen el mismo origen.
1. La familia entrega una trama de acción al daemon como evento 48 solo si `actionFrameReport` no marcó
   error (`ctx+0x2C == 0`) y el par quedó con `peerReachability == 1`
   ([AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md) §2). El iPhone llegó a ese estado: las líneas
   `shouldRemovePeerFromNDP ... No Action frames for(...)` de los registros exigen `+120 == 1`
   (`0xffffff80021a9c50`), así que sus tramas sí salieron como evento 48.
2. En `wifip2pd`, el manejador del evento 48 (`sub_10041C470`) publica la presencia del par (tabla de
   vecinos, `ndp` por el socket de rutas, ioctl 415) solo si el byte de la interfaz en el slot
   `[clase+2F8h]` es distinto de 0 y el del slot `[clase+2C8h]` es 0
   ([AWDL_WIFIP2PD_PRESENCE_ONCE.md](AWDL_WIFIP2PD_PRESENCE_ONCE.md) §3.1). El segundo es «Driver interface
   was removed» y no apareció en el registro. El primero se escribe una sola vez, en el inicializador de la
   interfaz (`sub_100416D00`, `0x100417647`), con el testigo 4 de la conformancia `AWDLWiFiDriver` del tipo
   de driver (protocolo `AWDLController`, tipo asociado `Driver`):
   | Tipo de driver | Testigo 4 | Valor |
   |---|---|---|
   | `AppleIO80211Driver` | `sub_1000A93F0` | `RawGet(117)` sin error y `u32 @+8` (`enabled`) distinto de 0 |
   | `AppleBroadcomDriver` | `sub_100003B30` | 0 constante |
3. El GET 117 (`APPLE80211_IOC_AWDL_SYNC_ENABLED`, 12 B) lo responde el driver: `getAWDL_SYNC_ENABLED`
   (`0xffffff800227bc2f`) → `IO80211AWDLPeerManager::getAwdlSyncEnabled` (`0xffffff800221f0f4`) →
   `apple80211RouteInternal(0xC02869C9, 117)` → `apple80211getAWDL_SYNC_ENABLED` (`0xffffff8002263899`) →
   slot `+0xFF0` de la interfaz. El manager solo compara la respuesta con su propio estado y registra
   `enable state mismatch` si difieren. La capa respondía `kIOReturnUnsupported`.
Con esa respuesta el flag del daemon valía 0 para toda la vida de la interfaz: ningún par recibió su
entrada de vecino sin `ndp -s`, nunca se envió el 415 y `pppDone[8829] Posted Peer Presence` no aparece en
ningún registro (solo `Posted Peer Absence`). Coincide con todas las pruebas anteriores, también con el A53
y `awdlsim`, que siempre necesitaron la entrada a mano.
- `getAWDL_SYNC_ENABLED` responde `version 1, flags 0, enabled 1`. El GET del daemon puede llegar antes del
  SET 117 del manager, así que la respuesta no depende del estado: la sincronización corre en la capa.
- La cabecera de 72 B de cada trama de acción llevaba los flags del canal a 0. `IO80211Peer::reportRssi`
  (`0xffffff8002242c4e`, llamada en `actionFrameReport` `0xffffff80021afbb0`) solo actualiza la media de RSSI
  de 5 GHz (`+1188`) con el bit `0x10` y la de 2,4 GHz (`+1180`) con el `0x08`; sin ellos la media se quedaba
  en el valor que `addAWDLPeer` copia de la primera trama. El daemon saca la banda de los mismos flags. La
  capa pone ahora banda, `0x02` (20 MHz) y `0x100` (DFS, canales 52 a 144), como
  `ChanSpecConvToApple80211Channel` (`0xffffff8002198efa`).
Comprobado el 2026-11-09 con la capa `233601f6` cargada y `awdlsim` como par (`--metric 1 --6e`, sin
`ndp -s`): a los pocos segundos de las primeras tramas del nodo, la capa registró `getAWDL_SYNC_ENABLED -> 1`
(cuatro lecturas del daemon tras el arranque), `wifip2pd` publicó `Posted peer presence for <MAC del nodo>
on awdl0`, `ndp -an` mostró la entrada `fe80::` del nodo como `permanent R`, y la familia registró
`pppDone[8829] Posted Peer Presence for <MAC>, uploading 0 packets to stack`. La entrada que
`Tools/iphone_test.sh run` carga por su cuenta (e287ac3) ya no hace falta. Queda por ver con el iPhone si
aparece en la hoja de AirDrop del Mac `?`.
Comprobado después con un receptor virtual ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10): el Mac muestra en su hoja
a un par que anuncia `_airdrop._tcp` en sus TLV 2, pero `sharingd` descarta ese anuncio cuando las flags del TXT llevan
el bit `0x10000`, como las del iPhone (111611), y lo espera por Rapport (`_applicationservicepairing`,
`_appsvcprepair`). El iPhone entra entonces a la hoja por ese otro camino, que sigue sin probar `?`.
- `processServiceResponseTLV` guarda el TLV 2 del iPhone cuando el bit 0 de `+16388` está puesto
  (`mDNSDirectPathEnabled`, por defecto activo: bit 1 de `awdl_mdns_mode`, que vale 3 sin boot-arg).
- La familia publica el evento 47 (`APPLE80211_M_PEER_PRESENCE`) solo con ausencia, desde
  `cleanUpForPeerRemovedFromNDP` y `IO80211PeerManager::removePeer`; su caso `'/'` en
  `reportDataPathEvents` solo reenvía lo que mande el driver. Ningún kext del kernelcache llama a
  `ifnet_notice_node_presence`.
- `shouldStopAFProcessing` (`0xffffff80021b48b8`) salta la lógica de RSSI para los pares con el bit 0 de
  `+49`, que solo escribe `processInfraChangesOfPeer` (`0xffffff80021bca21`, bit 6 del tercer argumento),
  y para los tipos 4 y 8. `PeerType:Cached` en `shouldRemovePeerFromNDP` es la etiqueta por defecto cuando
  el par no es RTG, TimeSync, Sidecar ni AirPlay, no el estado de la caché (`isCached=No` en `SYNC_INFO`).
- `actionFrameReport` solo rellena el aviso de presencia (`ctx+56`, `ctx+92..`) si el par tiene franjas
  comunes (`numOfOverlapSlots`, `+54` de las ivars AWDL, cuenta las franjas con el mismo canal y banda que
  la secuencia propia en `PM+42868`). Las secuencias capturadas coinciden ranura a ranura: iPhone
  `[6 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0]`, Mac `[0 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0]` en reposo.

## 4. El emparejamiento por código de esa noche (leído el 2026-11-11)

Desde iOS 26.2 y macOS 26.2, AirDrop entre dos personas que no se tienen en contactos pide un código de seis
dígitos: el receptor lo muestra, el emisor lo escribe y los dos equipos quedan como conocidos durante 30 días
(documentación de Apple). Es el emparejamiento por PIN de `rapportd`
([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10.5).

El registro guardado de `rapportd` de esa noche (`log show`, categoría `RPPairing`, mensajes de nivel por
defecto) trae la secuencia completa, con el Mac de receptor:

| Hora | Líneas de `rapportd` | Lectura |
|---|---|---|
| 20:42:23 | `Server PairVerify session … activated and ready` | Primera sesión del iPhone contra el Mac |
| 20:42:34 | `New pairing info generated`, `LISTEN: Updated PIN info`, `PairVerify timeout for session` | El iPhone no es conocido y el Mac genera el código |
| 20:42:47 | `Looking for PAKE credential` | Llega el código desde el iPhone |
| 20:42:48 | `Pairing SUCCESS, returns temporary self identities` | Emparejados |
| 20:42:49 | `Saving shared temporary identities`, `Saved contact with identifier`, `Added/updated temporary identity` (2) | El Mac guarda las identidades temporales del iPhone |
| 20:46 a 23:36 | `Server PairVerify succeeded for session` (39), `resolveBonjourCompleted` (8) | Cada envío del iPhone pasa antes por PairVerify, ya con esas identidades |

El camino por Rapport funcionó entonces con el iPhone real y con la capa en el sentido iPhone → Mac.

En el otro sentido el Mac no llegó a intentarlo: 10 líneas `Attempting discovery with`, las 10 seguidas de
`Skipping PairVerify for our own bonjour device`, ninguna `Starting PairVerify with:` y
`Updated browse client with 0 endpoints` las 8 veces. Los registros de servicio del iPhone no llegaban a
`rapportd` (§3).

Estado el 2026-11-11, con macOS 26.7.3 (25G342) y varios reinicios después: el volcado de estado de `rapportd`
(`RPKeychain State`) lista dos identidades `RPIdentity-AdHocPaired` con el nombre enmascarado `iP*ne`, tantas
como guardó esa noche. `sharingd` pide a `rapportd` los equipos con las marcas
`BLE Friends SessionPaired AdHocPaired`, y ese iPhone entra en la última.

Lo que se espera en la próxima prueba, de Mac a iPhone, sin comprobar `?`: `Starting PairVerify with:`,
`Client PairVerify succeeded`, `Updated browse client with 1 endpoints` y el iPhone en la hoja, sin código nuevo
mientras duren las identidades. Con el par virtual está comprobado hasta el ClientHello del TLS. Si las identidades
faltan o vencieron queda el código: lo muestra el iPhone y se escribe en el Mac `?`.
