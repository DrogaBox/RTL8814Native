# AirDrop sin Bluetooth (macOS 26.7.1, 25G241)

Pregunta: con un controlador Bluetooth ausente o apagado, ¿puede este Mac recibir
archivos de un iPhone (receptor en «Todos» 10 minutos) y puede descubrir al iPhone?
Análisis estático de `/usr/libexec/sharingd`, `Sharing.framework` y
`WirelessProximity.framework` del dyld shared cache x86_64h de 25G241. Las VAs de
sharingd son absolutas (base 0x100000000); las de Sharing y WirelessProximity son las
VAs del shared cache. Lo no verificado se marca `?`.

## 1. Estado del hardware en esta máquina

`system_profiler SPBluetoothDataType`: `Address: NULL`, `State: Off`, `Chipset:
BCM_4350C2`, `Firmware Version: v0 c0`. No hay radio BLE operativa.

## 2. Verificación de preparación (receptor)

El árbitro es `-[SDStatusMonitor isAirDropReady]` (`0x10014BA09`). Devuelve la
conjunción:

```
enableAirDropReceiving && bluetoothEnabledIncludingRestricted && wirelessEnabled
&& !wirelessCarPlay && !isClarityBoardEnabled && !isMulticastAdvertisementsDisabled
```

Si falla, registra «AirDrop not ready: wifi=%d, bluetooth=%d, carplay=%d,
receivingEnabled=%d, isVirtualMachine=%d isMulticastAdvertisementsDisabled=%d».

- `-[SDStatusMonitor enableAirDropReceiving]` (`0x100153AC4`): defaults
  `EnableAirDropReceiving` (predeterminado verdadero salvo accesorio de audio) y
  `isAirDropAvailable` (`SFDeviceSupportsAirDrop`).
- `-[SDStatusMonitor isMulticastAdvertisementsDisabled]` (`0x1001514E3`): defaults
  `NoMulticastAdvertisements`.
- `-[SDStatusMonitor bluetoothEnabledIncludingRestricted]` (`0x100151082`): si
  `SFDeviceIsVirtualMachine()` devuelve verdadero, acepta siempre. Si el
  `CBController` existe, exige `bluetoothState == 5` (poweredOn) o `== 10`
  (estado privado «restringido pero permitido» `?`); cualquier otro valor
  (unsupported, poweredOff, unauthorized) lo rechaza. Sin `CBController`, cae a
  `SFIsBluetoothEnabled` de Sharing (`0x7ff818EC06AF`), que exige el mismo estado 5
  y falso ante error. Con el controlador fantasma (Address NULL, firmware v0) el
  estado no puede ser poweredOn, de modo que este término es falso y
  `isAirDropReady` devuelve falso.

Consecuencia en la UI: `-[SDStatusMonitor newDiscoverableMode]` (`0x10014BC3B`)
devuelve `Disabled` si AirDrop no está permitido y **`Off` si `isAirDropReady` es
falso**; con Contactos Only exige cuenta Apple (`appleIDAccountState == 1`) y no
modo educativo («Discoverable mode off: appleIDAccountState=%@ eduModeEnabled=%d»).
El selector de modos disponibles (`sub_1002FC870`, Swift, disponible para
Panel de Control/Ajustes) reduce la lista a `[off]` cuando `isAirDropReady` es falso:
**sin Bluetooth la UI solo puede fijar «Nadie»**; no es que Finder lo grise, es que
las opciones Contactos/Todos dejan de estar disponibles.

El modo guardado se lee en `-[SDStatusMonitor reconciledDiscoverableMode]`
(`0x10014BE74`): `NSUserDefaults objectForKey:` con la clave
`kSFOperationDiscoverableModeKey` (`"DiscoverableMode"`, constante de Sharing en
`0x7ff842553018`); si no hay valor, `-[SDStatusMonitor defaultDiscoverableMode]`
(`0x10014B93C`) devuelve Contactos Only. Registra «Reconciled discoverable mode:
%@ (%@)». Los valores constantes son «Disabled», «Contacts Only», «Everyone»
(cadenas en Sharing; el valor exacto guardado para apagado es «Disabled» `?`).

## 3. Servidor de recepción: cuándo publica `_airdrop._tcp`

`-[SDAirDropServer updateServerState]` (`0x10018927C`) gobierna el servidor
(receptor). Primero `thisIsTheFinder` (en Finder el flujo va por
`SDAirDropInformation`). Requiere: usuario de consola presente
(`currentConsoleUser`), equipo desbloqueado alguna vez (`deviceWasUnlockedOnce`),
pantalla encendida (`_screenOn`) y modo descubrible distinto de Off
(`discoverableByContactsOnly || discoverableByEveryone`). Sin eso, registra
«Scanning mode off: console=%d, discoverable=%d, screenOn=%d, unlockedOnce=%d» y
llama a `stop`.

Con el gate cumplido, dos caminos:

1. **Disparo automático** si `-[SDStatusMonitor showMeInWormhole]`
   (`0x100153DF4`, defaults `ShowMeInWormhole`, falso por defecto) y
   `-[SDServerBrowser isAirDropEnabled]` (`0x10013A7A3`, verdadero si hay alguna
   sesión de browse AirDrop activa, p. ej. la ventana AirDrop de Finder) o si es
   máquina virtual, o `enableDemoMode`. Registra «Auto-trigger server» y llama a
   `startServer` **sin esperar Bluetooth**.
2. **Camino normal**: `-[SDAirDropServer startProximityScanner]` (`0x100189F43`)
   y registra «Scanning mode Contacts Only/Everyone». El escáner:
   - `-[SDAirDropBLEController startScanningWithIdentifier:deviceUpdatesHandler:]`
     (`0x100381550`, Swift) — sondeo BLE propio del daemon;
   - crea `WPAWDL` (`initWithDelegate:queue:`, WirelessProximity) y, cuando su
     `state == 3` (`-[SDAirDropServer awdlDidUpdateState:]` `0x100189127`, registra
     «BTLE scanner %@»), llama `startConnectionlessAWDLServiceScanning`.

La detección de un emisor llega por `-[SDAirDropServer awdl:foundDevice:rssi:]`
(`0x1001890A5`) → recorta 8 bytes (4 hashes cortos de 2 bytes: `airdropHash1..4`,
`bleRSSI`) → `-[SDAirDropServer handleFoundHashes:rssi:]` (`0x100188DCA`):

- Todos: «In Everyone mode, start server»;
- Contactos Only: `-[SDStatusMonitor contactsContainsShortHashes:]` compara contra
  la agenda; «Hashes %@ exist in contacts, start server» o «Hashes %@ not
  recognized».

Entonces `-[SDAirDropServer stopScanningAndStartServer]` (`0x100188D82`) →
`startTimers` → `-[SDAirDropServer startServer]` (`0x10018A4AC`) →
`-[SDAirDropServer start]` (`0x10018A5A6`, «AirDrop server started») →
`-[SDAirDropServer startHTTPServer:]` (`0x100189BC5`, servidor `_CFHTTPServer` con
SSL, «AirDrop server scheduled on port %@»; sirve `/Discover`, `/Ask`, `/Upload`).

La publicación Bonjour corre en la máquina de estados Swift `sub_1003E0020`
(«AirDrop Bonjour server setting up / ready / waiting with error %@ / stopped /
failed»), que lanza el arranque («Starting AirDrop Bonjour Server %s - legacy
%{bool}d» en `sub_1003DE270`; reinicio para pares antiguos «Restarting AirDrop
Bonjour Server for legacy peer %s» en `sub_1003E1660`) y espera a que AWDL esté
arriba («AWDL not yet available for serviceName %s» en `sub_1003F1150` y
`sub_1003F1A40`). La publicación real es `-[SDAirDropPublisher publish]`
(`0x100073840`): `CFNetServiceCreate(kCFAllocatorDefault, "local", <tipo>, <nombre>,
<puerto>)` con tipo `_airdrop._tcp.` o `_airdrop-alt._tcp.` (cadena en
`0x10064B515`/`0x10064B528`, elegido por `sub_1001A7872`) y
`CFNetServiceRegisterWithOptions` con opciones `0x100801`
(kDNSServiceFlagsAutoTrigger | kDNSServiceFlagsForce | **kDNSServiceFlagsIncludeAWDL**
`0x100000`) más `0x200000` cuando no se registra en todas las interfaces
(`registerAllInterfaces`; semántica exacta del bit 0x200000 en registro `?`).
`-[SDAirDropPublisher publishCallBack:]` (`0x1000731F4`) registra «Bonjour
advertise %@ started», reintenta («Retrying Bonjour advertise in 3 seconds...») y
«Bonjour advertise failed (%ld|%d)». Dos temporizadores apagan el servidor:
`awdlTimeoutFired:` (`0x100188462`) y `bluetoothTimeoutFired:` (`0x1001884E9`),
ambos registran «AirDrop server idle timeout» y llaman a `stop` + `updateServerState`.

**Respuesta: la publicación de `_airdrop._tcp` en awdl0 sí está condicionada al
disparador BLE** (los hashes cortos del anuncio del emisor) en el camino normal.
Sin Bluetooth el escáner no puede arrancar (`awdl:failedToStartScanningWithError:`
`0x10018921A`) y el servidor no arranca.

El «Auto-trigger server» (`ShowMeInWormhole` con un browse activo, o
`EnableDemoMode`) **tampoco lo salva**: está dentro del gate de `updateServerState`,
que exige `discoverableByContactsOnly || discoverableByEveryone`. Esos métodos
(`0x100188D62`, `0x100188D42`) comparan `_discoverableMode` del servidor, que viene
de `-[SDStatusMonitor updateDiscoverableMode]` (`0x10014C2AF`) →
`newDiscoverableMode`, y este devuelve `Off` cuando `isAirDropReady` es falso (§2).
Sin Bluetooth el gate falla con «Scanning mode off: … discoverable=0 …» antes de
mirar `ShowMeInWormhole`. La única vía en el código que salta la falta de Bluetooth
es `SFDeviceIsVirtualMachine()` verdadero: acepta el Bluetooth en
`bluetoothEnabledIncludingRestricted` y además dispara el auto-servidor. El modo
legado `DisableAWDL` (§5) cambia la interfaz, pero no el gate de Bluetooth.

## 4. Lado emisor: descubrimiento de receptores

La ventana AirDrop o la hoja de compartir abren una sesión de browse:
`-[SDServerBrowser startAirDropBrowsing:bundleID:sessionID:helperConnection:delayBonjour:]`
(`0x100139E45`) → `-[SDServerBrowser startAirDropBrowsing:connection:sessionID:]`
(`0x10013A165`) → `-[SDBonjourBrowser start]` (`0x10004FB30`, «SDBonjourBrowser:
start requested») → `-[SDBonjourBrowser updateBrowserState]` (`0x10004F497`).

- Interfaz: `-[SDBonjourBrowser getInterface:]` (`0x10004EE3B`) devuelve el índice
  de `awdl0` calculado por `sub_100123A50` (`getifaddrs`, primera interfaz IPv6
  `sa_family == 30` cuyo nombre case-insensitive es `awdl0`, `if_nametoindex`),
  o `0` (todas) si `browseAllInterfaces`. Sin `awdl0` registra «Bonjour browser
  failed to find awdl0 interface» (y «Bonjour resolver failed to find awdl0
  interface» en el resolver, `sub_100608341`).
- Sondeo: `DNSServiceBrowse` sobre la conexión compartida
  (`DNSServiceCreateConnection`) con opciones `kDNSServiceFlagsShareConnection`
  (`0x4000`) | `kDNSServiceFlagsThresholdOne` (`0x2000000`, modo 1) o
  `ThresholdFinder` (`0x4000000`, modo 2) | **`kDNSServiceFlagsIncludeAWDL`**
  (`0x100000`, para AirDrop salvo `enableDemoMode`). Registros «AirDrop bonjour
  browser setting up / ready / waiting with error %@ / failed with error %@ /
  cancelled» (`sub_1003F7510`) y «Bonjour discovered %@ PTR/TXT record over %@ in
  %d ms».
- Anuncio BLE paralelo: `-[SDBonjourBrowser advertiseHashes]` (`0x10004F27A`)
  exige `WPAWDL.state == 3` y `-[SDStatusMonitor enableAirDropAdvertising]`
  (`0x100153A86`, que niega el default `DisableAirDropAdvertising`), toma
  `-[SDStatusMonitor myShortHashesForAirDrop]` (`0x100152B30`) y llama
  `startConnectionlessAdvertisingWithData:` (`0x10004F16D`); «BTLE advertising
  hashes %@» o «No email or phone number hashes to advertise». El estado del
  anunciador llega por `awdlDidUpdateState:` (`0x10004F00D`, «BTLE advertiser %@»).

**Respuesta: el browse no espera respuesta BLE**: `updateBrowserState` arranca el
`DNSServiceBrowse` en awdl0 sin más condiciones (`shouldStart` = `startCalled` y
usuario de consola). El fallo del anuncio BLE (`awdl:failedToStartAdvertisingWithError:`
`0x10004F10B`) solo se registra («SDBonjourBrowser:
failedToStartAdvertisingWithError %@») y el browse continúa. El anuncio BLE del
emisor existe para despertar al receptor iOS: ese dependencia es de iOS y no puede
verificarse aquí `?` — si el iPhone ya está en su ventana de «Todos» de 10 minutos,
su receptor está despierto de antemano, pero si iOS exige un anuncio BLE del emisor
para subir AWDL, un Mac emisor sin Bluetooth no lo despertaría `?`.

La capa BLE real está en WirelessProximity: `WPAWDL`
(`-[WPAWDL startConnectionlessAWDLServiceAdvertisingWithData:]` `0x7ff905E9DD69`
construye el cuerpo del anuncio, longitud < 0x17 = 23 bytes, 18 bytes típicos,
compatible con el formato Apple 0x004C tipo 0x05 con los hashes cortos `?`;
`+[WPAWDL hashEmail:]` `0x7ff905E9DA52` y `+[WPAWDL
generateDataFromEmails:]` `0x7ff905E9DB64` generan los hashes de correos/teléfonos)
habla por XPC con `com.apple.wirelessproxd` (cadenas del propio framework;
también `com.apple.wiproxd.btpairing.connect`). Sin radio, las peticiones fallan y
llegan `advertisingFailedToStart` (`0x7ff905E9E606`) / `scanningFailedToStart`
(`0x7ff905E9E7E7`) en lugar de los eventos `foundDevice`.

## 5. Claves de defaults y modos de depuración

Lector común en sharingd: `sub_1001A768E` (`0x1001A768E`) =
`CFPreferencesCopyAppValue` con dominio **`com.apple.Sharing`** y, si no existe,
**`com.apple.NetworkBrowser`**; el lector booleano es `sub_1001A7589`
(`0x1001A7589`). Claves observadas:

| Clave | Predeterminado | Efecto | Función |
|---|---|---|---|
| `EnableAirDropReceiving` | verdadero | receptividad habilitada | `0x100153AC4` |
| `NoMulticastAdvertisements` | falso | deshabilita `isAirDropReady` | `0x1001514E3` |
| `ShowMeInWormhole` | falso | «Auto-trigger server» con browse activo | `0x100153DF4` |
| `DisableAWDL` | falso | modo legado: usa `en0` en vez de `awdl0` y quita IncludeAWDL | `sub_100123014` `0x100123014` |
| `DisableAirDropAdvertising` | falso | anuncio BLE del emisor fuera | `0x100153A86` |
| `BrowseAllInterfaces` | `EnableDemoMode` o VM | browse/registro en todas las interfaces (índice 0) | `sub_1001A7514` `0x1001A7514` |
| `EnableDemoMode` | falso | demo: quita IncludeAWDL, browse/registro ampliado, auto-servidor | flags y `updateServerState` |
| `DiscoverableMode` | (ausente → Contacts Only) | «Disabled»/«Contacts Only»/«Everyone» en el dominio del daemon `?` | `0x10014BE74` |

`DisableAWDL` es el interruptor del AirDrop «por red normal»: con él,
`-[SDBonjourBrowser getInterface:]` y `-[SDAirDropPublisher publish]` y
`-[SDAirDropClient createSession]` dejan de usar `awdl0` (el índice pasa a `en0`,
`sub_100123A50`) y el registro pierde `IncludeAWDL`; el descubrimiento pasa a ser
Bonjour multicast por la Wi-Fi de infraestructura.

Feature flags internos de Sharing (tabla registrada por `sub_7FF819102BB0` en
`0x7ff819102BB0`, consultados vía `+[SFFeatureFlagAdaptor
featureEnabledForDomain:feature:]` `0x7ff818F4CFC5`, que delega en el framework
FeatureFlags `?`): `DisableAirDrop`, `DisableTLS`, `DisableAirDropAdvertising`,
`DisableAutoAccept`, `EnableDemoMode`, `BrowseAllInterfaces`,
`RegisterAllInterfaces`, `EveryoneModeBanner`, `AirDropPrivacyImprovements`,
`AirDropWalkAway`, `AirDropSystemLevelProgress`, `AirDropMoveToApp`,
`AskToAirDrop`, `AirDropContactShare`, `BoopToAction`, `BoopToAirDrop`,
`BoopToCollaborate`, `BoopShockwave`, `BoopShockwaveVariant`,
`BoopInteractionTypeOverride`, `ShockwaveHapticsPlayerStartsEarly`,
`NearFieldController`, `AirDropFinalALPN`, `PINPairingAirDrop`,
`ShareSheetSolarium`, `AirDropPrivateContactDiscovery`,
`A2ARangingAssumeClose`, `A2ARangingAssumeFar`, `QUICIdleTimeout`,
`VerboseWalkAway`, `NearFieldSharingEnabled`, `CellularUsageEnabled`,
`AirDropSensitivePreviewOverride`, `AirDropSensitiveContentOverride`,
`AirDropShowTransferIdentifierUI`, `AlwaysAutoAccept`,
`OverrideReceivedFilesPathiOS`. Con `DisableAirDrop` activo, AirDrop queda
deshabilitado por completo (estado `Disabled` en `newDiscoverableMode` vía
`isAirDropAllowed`).

## 6. Registro: cadenas para `log show`

Subsistema: `com.apple.airdrop` (aparece también `com.apple.airdrop_pro` `?`).
Cadena por decisión:

| Decisión | Cadena | Función |
|---|---|---|
| No listo (sin BT) | «AirDrop not ready: wifi=%d, bluetooth=%d, carplay=%d, receivingEnabled=%d, isVirtualMachine=%d isMulticastAdvertisementsDisabled=%d» | `isAirDropReady` `0x10014BA09` |
| Modo colapsado a Off | «Discoverable mode off: appleIDAccountState=%@ eduModeEnabled=%d» | `newDiscoverableMode` `0x10014BC3B` |
| Modo leído | «Reconciled discoverable mode: %@ (%@)» | `0x10014BE74` |
| Modo cambiado | «Discoverable mode changed to %@» | `0x100188AD9` |
| Gate del servidor | «Scanning mode off: console=%d, discoverable=%d, screenOn=%d, unlockedOnce=%d» | `updateServerState` `0x10018927C` |
| Camino escáner | «Scanning mode Contacts Only» / «Scanning mode Everyone» | `updateServerState` |
| Auto-servidor | «Auto-trigger server» | `updateServerState` |
| Finder | «Finder entered AirDrop» / «Finder exited AirDrop» | `finderAirDropEnabled:` `0x100188A00` |
| Escáner BLE | «BTLE scanner %@» (estados 0–3) | `0x100189127` |
| Anunciador BLE | «BTLE advertiser %@» | `0x10004F00D` |
| Hashes recibidos | «In Everyone mode, start server» / «Hashes %@ exist in contacts, start server» / «Hashes %@ not recognized» | `handleFoundHashes:rssi:` `0x100188DCA` |
| Sin hashes propios | «No email or phone number hashes to advertise» | `advertiseHashes` `0x10004F27A` |
| Fallo BLE emisor | «SDBonjourBrowser: failedToStartAdvertisingWithError %@» | `0x10004F10B` |
| Servidor arriba | «AirDrop server started» / «AirDrop server scheduled on port %@» | `0x10018A5A6` / `0x100189BC5` |
| Publicación | «Starting AirDrop Bonjour Server %s - legacy %{bool}d» / «Bonjour advertise %@» / «Bonjour advertise %@ started» / «Retrying Bonjour advertise in 3 seconds...» / «Bonjour advertise failed (%ld|%d)» | `sub_1003DE270`, `publish` `0x100073840`, `publishCallBack:` `0x1000731F4` |
| Estado publicación | «AirDrop Bonjour server setting up / ready / waiting with error %@ / stopped / failed with error %@» | `sub_1003E0020` |
| AWDL pendiente | «AWDL not yet available for serviceName %s» | `sub_1003F1150`, `sub_1003F1A40` |
| Sin awdl0 | «Bonjour browser failed to find awdl0 interface» | `getInterface:` `0x10004EE3B` |
| Browse | «SDBonjourBrowser: start requested» / «AirDrop bonjour browser ready» / «Bonjour discovered %@ PTR over %@ in %d ms» | `0x10004FB30`, `sub_1003F7510` |
| Apagado | «AirDrop server idle timeout» / «AirDrop Bonjour server stopped» | `0x100188462`, `0x1001884E9`, `sub_1003E0020` |

Sugerencias de consulta:

```
log show --last 30m --predicate 'subsystem == "com.apple.airdrop"' --info
log show --last 30m --predicate 'process == "sharingd"' --info
```

## 7. Respuestas a las preguntas

1. **¿Refusa AirDrop sin Bluetooth?** No lo deshabilita del todo: el paneo sigue
   existiendo y el browse (rol emisor) funciona. Lo que colapsa es la
   descubribilidad propia: `isAirDropReady` falso (término
   `bluetoothEnabledIncludingRestricted`) obliga a `newDiscoverableMode` a `Off`
   y la UI solo ofrece «Nadie». Cada intento registra «AirDrop not ready: ...».
2. **¿Publica `_airdrop._tcp` en awdl0?** No. La publicación exige
   `stopScanningAndStartServer`, que solo llega tras un `handleFoundHashes:rssi:`
   (BLE), y el auto-servidor (`ShowMeInWormhole`, `EnableDemoMode`) queda detrás
   del mismo gate de modo descubrible, que sin Bluetooth vale `Off` (§3). Solo
   `SFDeviceIsVirtualMachine()` verdadero abre las dos puertas.
3. **¿El emisor necesita respuesta BLE antes de navegar AWDL?** No: el browse
   (`DNSServiceBrowse` con `IncludeAWDL` sobre el índice de `awdl0`) arranca sin
   esperar BLE; el anuncio BLE fallido solo deja registro. La restricción BLE
   está en el receptor, no en el emisor.
4. **¿Puede el iPhone descubrir a este Mac?** No sin Bluetooth: el servidor de
   recepción no llega a publicarse (§3), ni con `ShowMeInWormhole`.
5. **¿Puede este Mac descubrir al iPhone?** El browse sobre awdl0 es viable si
   `awdl0` existe con IPv6; el iPhone en «Todos» 10 min tiene su receptor
   despierto. La duda es si iOS exige el anuncio BLE del emisor para mantener
   AWDL arriba durante el envío `?`.

## 8. Prueba práctica de visita del iPhone

Requisitos:

1. `awdl0` creado y con dirección IPv6 link-local (etapa 3 del proyecto: ya
   verificado en hardware); `en2` asociado y AWDL sirviendo ventanas.
2. Recepción habilitada: defaults `EnableAirDropReceiving` verdadero (por
   defecto) y `NoMulticastAdvertisements` ausente o falso.
3. **Bluetooth LE operativo en el Mac.** Sin él `isAirDropReady` es falso, el modo
   descubrible queda en `Off` aunque se escriba «Everyone» en los defaults, y ni
   el escáner BLE ni el auto-servidor arrancan (§3). Es un requisito previo de la
   prueba, al mismo nivel que `awdl0`.
4. Modo «Todos» en el Mac (con Bluetooth la UI ya lo ofrece).
5. Pantalla encendida, sesión desbloqueada, usuario de consola presente.
6. El iPhone envía desde la hoja de compartir: su anuncio BLE despierta el
   escáner del Mac («In Everyone mode, start server») y el Mac publica en awdl0.
   En sentido inverso, el Mac navega awdl0 sin BLE, pero el receptor iOS espera,
   con toda probabilidad, el mismo disparador BLE del emisor `?`.

Qué mirar en los registros:

- «AirDrop not ready: ... bluetooth=0» — confirma el gate de Bluetooth.
- «Scanning mode Everyone» o «Auto-trigger server» — qué camino tomó
  `updateServerState`.
- «AirDrop server started», «AirDrop server scheduled on port ...» — servidor
  HTTP arriba.
- «Starting AirDrop Bonjour Server ... - legacy 0» y «Bonjour advertise ...» —
  publicación; «AirDrop Bonjour server waiting with error ...» indicaría AWDL
  caído o interfaz ausente.
- «Bonjour browser failed to find awdl0 interface» — si `awdl0` no existe o no
  tiene IPv6 al abrir la ventana de AirDrop.
- En el lado emisor, «BTLE advertiser ...» y
  «SDBonjourBrowser: failedToStartAdvertisingWithError ...» — fallo esperado del
  anuncio BLE; el browse debe continuar igualmente.

## 9. Pendientes `?`

- Valor exacto guardado para el modo Off en `DiscoverableMode` («Disabled» vs
  «Off») y el dominio exacto del daemon (`com.apple.sharingd`): verificar en una
  máquina con Bluetooth o con `defaults read`.
- Significado del estado 10 de `CBController` («restricted») en
  `bluetoothEnabledIncludingRestricted`.
- Mecanismo de resolución de las feature flags (framework FeatureFlags; el thunk
  de `SFFeatureFlagAdaptor` apunta a una importación fuera de las secciones
  talladas).
- Si iOS exige el anuncio BLE del emisor para subir/mantener AWDL durante un
  envío (no verificable sin captura iOS de referencia; las capturas de AWDLLab
  registran los anuncios Apple 0x004C tipo 0x05 del emisor, coherentes con el
  rol despertador `?`).
- Longitud exacta y layout del payload de `WPAWDL
  startConnectionlessAWDLServiceAdvertisingWithData:` (18 bytes construidos en
  `0x7ff905E9DD69`; compatibilidad con el formato 0x05 asumida por la longitud
  y los hashes, no comparada byte a byte con las capturas).
