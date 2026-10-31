# AirDrop con controladores Bluetooth de terceros (macOS 26.7.1, 25G241)

Pregunta: con un controlador USB Bluetooth de terceros —Broadcom BCM20702 con
BrcmPatchRAM, Intel AX200/AX210, MediaTek MT7925, CSR, Realtek RTL8761B, todos
expuestos a bluetoothd a través de BlueToolFixup—, ¿es suficiente para el
descubrimiento AirDrop, más allá del requisito de «Bluetooth encendido» ya
documentado en `AWDL_AIRDROP_SIN_BLUETOOTH.md`?

Análisis estático de `/usr/libexec/sharingd` y del slice x86_64 de
`/usr/sbin/bluetoothd` (binario universal) de 25G241. Las VAs son absolutas,
base `0x100000000` en ambos binarios. Lo no verificado se marca `?`.

## 1. Respuesta corta

Ni sharingd ni bluetoothd contienen una comprobación de fabricante, versión HCI
o «capacidad Continuity» que deniegue el sondeo o anuncio LE a un cliente como
wirelessproxd. Los gates verificados son:

1. En sharingd, el estado del controlador (`bluetoothState == 5` o `== 10`) como
   parte de `isAirDropReady`; sin él el modo descubrible colapsa a `Off`
   (ya documentado). Nada más: el escáner y el anunciador no vuelven a
   consultar al controlador.
2. En bluetoothd, capacidades LE estándar: LE soportado, máscara de estados LE
   (scan activo, tipos de anuncio) y el número de instancias de anuncio
   disponibles. Todo controlador BLE 4.0 pasa los dos primeros; el tercero
   condiciona los anuncios multi-instancia, no el sondeo ni la publicidad de
   una instancia.

Los mecanismos específicos de Apple (lista de vendors para filtrado en
offload, match tables, buffers AOP, VSC por familia de chipset) se degradan a
«sin offload» cuando el controlador no los responde; no rechazan el servicio.
La consecuencia práctica: cualquier controlador BLE 4.0+ funcional debería
sostener el descubrimiento AirDrop; los 5.x (Intel AX200/AX210, MT7925,
RTL8761B si su firmware implementa extended advertising) conservan además la
multi-instancia. Para el BCM20702 la ventaja no está garantizada: la política
por board-id de los Mac Intel es Broadcom, pero un dongle de terceros entra por
el transporte 4000, que encuadra los VSC en la familia SRS (§3.3); cuál de las
dos prevalece no quedó trazado `?`.

## 2. sharingd: del «Bluetooth encendido» al escáner y al anunciador

Cadena verificada función por función (direcciones de sharingd):

1. `-[SDStatusMonitor isAirDropReady]` (`0x10014BA09`): conjunción ya
   documentada; el término Bluetooth es
   `-[SDStatusMonitor bluetoothEnabledIncludingRestricted]` (`0x100151082`):
   `SFDeviceIsVirtualMachine()` verdadero acepta siempre; con controlador exige
   `bluetoothState == 5` (poweredOn) o `== 10` (estado restringido `?`);
   sin controlador cae a `SFIsBluetoothEnabled` (mismo estado 5).
   `-[SDStatusMonitor bluetoothEnabled]` (`0x1001510F5`) exige 5 a secas.
   El monitor es una instancia de `CBAdvertiser` (clase privada de
   CoreBluetooth, asignada en `-[SDStatusMonitor installBluetoothMonitor]`
   `0x100155B29`) observada por `-[SDStatusMonitor bluetoothStateChanged]`
   (`0x100155E5A`, tabla de nombres de estado 0–10). En sharingd no existe
   `CBCentralManager` ni `CBPeripheralManager` (única referencia CoreBluetooth:
   `CBController`/`CBAdvertiser`); el sondeo BLE de AirDrop no toca
   CoreBluetooth directamente.
2. `-[SDStatusMonitor newDiscoverableMode]` (`0x10014BC3B`): `Off` si
   `isAirDropReady` es falso (sin Bluetooth la UI sólo ofrece «Nadie»).
3. `-[SDAirDropServer updateServerState]` (`0x10018927C`) →
   `-[SDAirDropServer startProximityScanner]` (`0x100189F43`): los gates son
   usuario de consola, desbloqueo previo, pantalla y modo descubrible; no hay
   ninguna consulta adicional de Bluetooth.
4. Escáner: `-[SDAirDropBLEController
   startScanningWithIdentifier:deviceUpdatesHandler:]` (`0x100381550`; init
   `0x10037F820` → `sub_10037F590`; arranque `sub_100380CC0`): en su arranque
   no hay comprobaciones de soporte LE, características del controlador,
   Continuity ni fabricante `?` (máquina de concurrencia Swift, revisada sin
   hallar gates). El sondeo real se ordena sobre `WPAWDL` (WirelessProximity):
   `-[SDAirDropServer awdlDidUpdateState:]` (`0x100189127`) sólo con
   `state == 3` llama `startConnectionlessAWDLServiceScanning`; el fallo llega
   por `awdl:failedToStartScanningWithError:` (`0x10018921A`).
5. Anunciador (emisor): `-[SDBonjourBrowser advertiseHashes]` (`0x10004F27A`)
   exige `WPAWDL.state == 3` y `enableAirDropAdvertising` (`0x100153A86`);
   el fallo del anuncio sólo deja registro (`0x10004F10B`) y el browse sigue.
6. `SFBLEScanner`/`SFBLEAdvertiser`/`SFBLEClient`/`SFBLEConnection` (referencias
   de clase en `0x10076D258`–`0x10076D270`): sus usuarios en sharingd son
   `SDAutoUnlockSessionManager`, `SDNearbyAgent`, `SDSetupAgent` y
   `SDRemoteInteractionAgent`; ninguna ruta AirDrop los usa.
7. `WPAWDL` habla por XPC con `com.apple.wirelessproxd`; el estado 3 lo produce
   ese daemon, que es el cliente Continuity de bluetoothd. Sus gates internos
   quedan fuera de este análisis (binario en el shared cache, no tallado aquí)
   `?`.

Conclusión sharingd: entre «Bluetooth poweredOn» y el escáner/anunciador
corriendo sólo media el estado del `CBAdvertiser` (5/10) y el estado 3 de
`WPAWDL`. No hay checks de LE, vendor ni Continuity.

## 3. bluetoothd: qué porta el sondeo y el anuncio LE

### 3.1 Capacidades LE estándar (los únicos gates de negación)

- LE soportado: si el controlador no declara LE, el registro de SMP falla
  (`sub_1002261C9`, `0x1002261C9`; «LE not supported, can't register SMP.»,
  cadena `0x10094499C`, error 147). Sin LE no hay BLE en absoluto.
- Máscara de estados LE: el comando estándar HCI `LE_Read_Supported_States`
  se procesa en `sub_1001728A1` (`0x1001728A1`,
  «initCmdReadLeSupportedStatesCB») que guarda la máscara en
  `qword_100B30EC8` vía `sub_100216E28` (`0x100216E28`).
- Escaneo activo: `sub_1002182A0` (`0x1002182A0`) exige el bit `0x20` de la
  máscara; sin él devuelve error 1330 («Local device does not support active
  scanning.», cadena `0x100941077`, xref `0x1002182E6`). El scan pasivo no
  tiene gate propio.
- Tipos de anuncio: `sub_100218870` (`0x100218870`, entrada de
  `LE_StartAdvertising`) exige, según el tipo pedido, los bits `0x1`
  (non-connectable), `0x2` (discoverable), `0x4` (connectable) y `0x8`
  (direct) de la misma máscara; sin el bit devuelve 1330 con «Device does not
  support … advertising.» (cadenas `0x10094119B`, `0x1009411C7`,
  `0x1009411F8`, `0x10094122A`). Los cuatro estados son obligatorios en todo
  controlador BLE 4.0, así que estos gates no discriminan fabricantes.

### 3.2 Instancias de anuncio (multi-adv)

- En `sub_100218870`: pedir `numOfInstances > 1` exige
  `sub_1001F7321()` (`0x1001F7321`, devuelve `byte_100B2FE20`) mayor que 0 y
  un `numOfInstances` menor o igual; con contador 0 y `n != 1` devuelve error
  101 con el registro «numOfInstances can not be greater than 1, using regular
  advertising» (cadena `0x100941157`, xref `0x1002189FA`): pese al texto, el
  código rechaza la petición, no recurre a una instancia única `?` (no se
  encontró ruta de reintento con `n = 1`).
- `byte_100B2FE20` cuenta las instancias registradas en `byte_100B2FE22[]` por
  `sub_1001F7675` (`0x1001F7675`), con tope `byte_100B294D0`, fijado por
  `sub_1001F79D0` (`0x1001F79D0`) con el valor del objeto de política de chip.
- El tope lo calcula el slot `+3976` de la vtable del objeto de política
  (`sub_100357A9C`, `0x100357A9C`) según el campo familia `+820`: familias
  < 2000 (Broadcom de Mac) 4–16 según familia; familias 5000–5999 y 6000: 8;
  familia 2000–3999 (ACI): `4 * slot2328 + 4` o `slot2328 + 5`. Con familia 0
  (objeto ausente) devuelve 0 y no hay multi-instancia.
- Cómo se llena el contador con un controlador que no es Broadcom no quedó
  trazado de extremo a extremo `?`: la vía Broadcom usa los VSC
  `_BCM_*MultipleAdvInstance` (implementaciones `sub_10019D64A` y siguientes,
  tabla `0x100AADF50`); la vía estándar «BT Spec advertising sets» (extended
  advertising de LE 5.0) existe — `initCoreStack` (`sub_1005100C6`,
  `0x1005100C6`) registra «enableBTSpecAdvertisingSets=%d … maxAdvHandles=%d»
  y el arranque extended registra «Stack supports %d advertising instances»
  (cadena `0x100A31460`, xref `0x1006A779A`, con sustituto 1 si el contador es
  0) `?`.
- La publicidad que sirve a wirelessproxd llega por
  `-[CBStackBLEAdvertiserBTStack shouldAdvertiseForWPDaemon:advData:advInstanceType:]`
  (`0x1000359E1`, datos del fabricante de hasta 28 bytes) y usa sesiones
  separadas de una instancia (conectable, no conectable, contacto, object
  discovery; ivars en `0x100B16718`–`0x100B16788`); la quinta sesión
  multi-instancia es sólo del camino SWUP/Maestro
  (`-[CBStackBLEAdvertiserBTStack _updateMultiInstancesAdvertising]`
  `0x100037985`). Qué pide exactamente wirelessproxd no se verificó `?`.

### 3.3 Familia de chipset, VSC y offloads

- bluetoothd elige la familia de chip por el modelo del Mac, no por el id de
  fabricante HCI del controlador: `sub_100393F90` (`0x100393F90`) construye el
  objeto de política (`qword_100B3ABB0`, constructor `sub_100339650`,
  `0x100339650`) con familias 21/22/23/24 (tablas de board-id pasadas a
  `MGIsDeviceOneOfType`, con los nombres cifrados; no se pudieron listar los
  modelos `?`), 2000 (variable de entorno `BT_CHIP=ACI BT`), 5000/5001
  (boot-arg `bt_pcie_srs`), 3000 y 6000; el fallback es la familia 1. En esta
  máquina (board-id `Mac-27AD2F918AE68F61`, SMBIOS MacPro7,1) la familia
  corresponde a una del rango Mac, es decir 1–1999 `?`.
- El dispatcher de VSC `sub_1001E348C` (`0x1001E348C`) reparte por el valor de
  chipset fijado en `dword_100B1CCFC` (escritor `sub_1001E3726`,
  `0x1001E3726`, admite 1–6000): 1–1999 → familia BCM, 2000–3999 → ACI,
  4000–5999 → SRS, 6000 → ACI; cualquier otro valor registra «Invalid chipset
  %d» (cadena `0x100937168`) y deja el daemon sin VSC.
- El chipset llega de dos sitios: el objeto de política (`policy+820`, vía
  `sub_100378C0A` desde `initCoreStack`) o el transporte: la inicialización de
  la pila (`sub_100158C5F`, `0x100158C5F`) trata explícitamente el «Third
  Party USB Bluetooth Dongle» (transporte 4000; getter `sub_10016057B`,
  selector `sub_1001605B8`) y si su transporte falla registra «Transport layer
  initialization failed for Third Party USB Bluetooth Dongle. Switch to
  internal Bluetooth module» (cadena `0x10091EA63`) y reintenta con el módulo
  interno. Con un dongle de terceros funcionando, el chipset 4000 lo encuadra
  en la familia SRS de VSC.
- Consecuencia con un controlador no Broadcom bajo política de familia
  Broadcom (los Mac de Intel): los VSC Broadcom que bluetoothd envía para las
  offloads reciben error del controlador y cada característica queda
  deshabilitada (el bloque de registro «BT chip support for … : %s / hw …»,
  región `0x100410D65`–`0x1004121B2`); el sondeo y el anuncio estándar siguen.
  No se halló ningún camino que deniegue el servicio por VSC fallidos.
- «LE Feature Vendor List» (`sub_100411E38`, `0x100411E38`; flag
  `byte_100B3B9A8`): bit de hardware del slot `+3112` (`sub_10033C0C6`,
  falso de serie para familias 22–24), override por defaults del grupo `LE`
  clave `FeatureVendorList` y log «LE Feature Vendor List is %s (hw %s)»
  (`0x1009CA370`). El override sólo puede desactivar (con hardware 0 el flag
  queda 0). El flag y el de `IncludeHiddenLEFeatureVendors`
  (`sub_100412069`, `0x100412069`) no tienen consumidores en código — sólo la
  referencia desde la vtable de getters de capacidad (`0x100ABBEE0`) — es
  decir, informativos, sin efecto sobre scan/adv.
- Filtro de scan por fabricante (`LEManufacturerScanFilter`,
  `sub_100410D65`, `0x100410D65`): soporte hardware (slot `+2840`) Y feature
  flag `BluetoothFeatures/LEManufacturerScanFilter`; con ambos, el offload
  usa VSC (`_BCM_LEMetaAddManufacturerScanFilter`, impl `sub_1001AE1C4`).
  Sin hardware, el filtrado corre en el host; no hay denegación.
- Filtrado de anuncios no-Apple: `sub_10075F2DA` (`0x10075F2DA`) aplica las
  «enhanced rules» (base de datos de OUI/reglas) sólo cuando el fabricante del
  payload es distinto de 76 = `0x4C` (Apple); los anuncios Apple pasan sin
  reglas.
- El escáner de proximidad (`-[CBStackBLEScannerBTStack _scanRequestCreate]`,
  `0x10005FB68`) registra los tipos de descubrimiento (2, 3, 5, 6, 7, 8, 9,
  10, 15, 16, 17, 18, 19, 21, 22, 24, 26, 27, entre ellos los Apple de
  Continuity y proximidad) y añade offloads (buffers AOP del slot
  `+2744`, MiLo `+2760`, umbral de intervalo `+2736`, targetCore del slot
  `+672`) sólo si el objeto de política declara soporte; sin ellos el request
  se crea igual y el scan funciona sin offload.

### 3.4 Lo que no hay

- Ninguna comparación contra `0x000F` (Broadcom) ni `0x004C` (Apple) que
  permita o deniegue scan/adv; las únicas comparaciones con `0x4C` verificadas
  son el filtro de reglas para datos de fabricante no-Apple (§3.3). `?`
- Ninguna comprobación de versión HCI que deniegue scan o adv `?` (no se
  hallaron strings ni caminos con ese propósito; el gate LE existe como bit de
  features, §3.1).

## 4. Evaluación por familia de controlador

Los datos de cada controlador (id de fabricante HCI, versión Bluetooth,
soporte extended advertising) son de especificación pública, no verificados
contra el binario ni contra hardware `?`; los gates citados son los de §3.

| Gate (bluetoothd) | BCM20702 + BrcmPatchRAM | Intel AX200/AX210 | MT7925 | CSR8510 | RTL8761B |
|---|---|---|---|---|---|
| LE soportado (SMP, §3.1) | sí (BT 4.0) `?` | sí (5.2/5.3) `?` | sí (5.4) `?` | sí (4.0) `?` | sí (5.0) `?` |
| Scan activo, bit 0x20 (§3.1) | pasa | pasa | pasa | pasa | pasa |
| Tipos de anuncio, bits 0x1–0x8 (§3.1) | pasan | pasan | pasan | pasan | pasan |
| Anuncios de 1 instancia (sesiones Continuity, §3.2) | sí | sí | sí | sí | sí |
| Multi-instancia (§3.2) | probable: VSC Broadcom presentes en el firmware BrcmPatchRAM, encajan con la familia BCM de la política `?` | probable: extended advertising nativo llena el contador `?` | probable: extended advertising nativo `?` | no: sin extended adv ni VSC → contador 0 → error 101 si algún cliente pide `n > 1` | depende del firmware: extended adv `?` |
| VSC de offload (§3.3) | familia BCM = su familia nativa `?` | fallan (sin efecto funcional) | fallan (sin efecto funcional) | fallan (sin efecto funcional) | fallan (sin efecto funcional) |
| «LE Feature Vendor List» / vendor filters (§3.3) | sin efecto (flag sin consumidores) | igual | igual | igual | igual |

Lectura por familia:

- **BCM20702 con BrcmPatchRAM** — es la única familia cuyo fabricante coincide
  con la política Broadcom que bluetoothd aplica en Macs de Intel; si esa
  política prevalece sobre el transporte 4000 del dongle (§3.3, `?`) y el
  firmware cargado por BrcmPatchRAM implementa los VSC multi-instancia de
  Broadcom, conserva además las offloads y la multi-instancia. Aun sin eso pasa
  todos los gates de §3.1 y §3.2 de una instancia, como el resto.
- **Intel AX200/AX210 y MT7925** — fabricante distinto (`0x0002` Intel,
  MediaTek `?`); los VSC Broadcom fallan y las offloads se desactivan, pero el
  sondeo, el anuncio de una instancia y —vía extended advertising estándar de
  LE 5.x— presumiblemente la multi-instancia quedan operativos `?`.
- **CSR8510** — BT 4.0 sin extended advertising ni VSC: scan activo y anuncios
  de una instancia funcionan; cualquier petición multi-instancia recibiría el
  error 101 de §3.2.
- **RTL8761B** — igual que los 5.x si su firmware implementa extended
  advertising; sin ello, cae al caso CSR `?`.

En todos los casos, los requisitos de sharingd (§2) se cumplen igual: el
estado «poweredOn» que BlueToolFixup expone a través de bluetoothd es lo único
que el daemon de compartir consulta.

## 5. Cadena completa de gates, en orden

1. sharingd `isAirDropReady` (`0x10014BA09`) ← `bluetoothState` 5/10 del
   `CBAdvertiser` (`0x100151082`).
2. sharingd `newDiscoverableMode` (`0x10014BC3B`) → `Off` si 1 falla.
3. sharingd `updateServerState` (`0x10018927C`) → `startProximityScanner`
   (`0x100189F43`), sin gates de Bluetooth adicionales.
4. sharingd/WirelessProximity: `WPAWDL.state == 3`
   (`awdlDidUpdateState:` `0x100189127` escáner; `advertiseHashes`
   `0x10004F27A` anunciador); el estado lo produce wirelessproxd `?`.
5. bluetoothd: LE soportado (`sub_1002261C9`), estados LE (`sub_1001728A1` →
   `qword_100B30EC8`), scan activo bit `0x20` (`sub_1002182A0`), tipos de
   anuncio bits `0x1`–`0x8` (`sub_100218870`).
6. bluetoothd: multi-instancia (`sub_1001F7321` > 0 y `n` ≤ contador;
   tope `byte_100B294D0` = `sub_100357A9C` según familia del board-id).
7. bluetoothd: familia de VSC por chipset (`sub_1001E348C`), política por
   board-id (`sub_100393F90`) o transporte de dongle 4000
   (`sub_100158C5F`); los VSC fallidos degradan offloads, no deniegan
   el servicio.

## 6. Pendientes `?`

- Qué pide exactamente wirelessproxd al anunciador (sesiones separadas de 1
  instancia o multi-instancia): requiere tallar wirelessproxd del shared
  cache; decide si el error 101 de multi-instancia afecta a AirDrop/Continuity.
- Si la vía «BT Spec advertising sets» llena `byte_100B2FE20` en controladores
  5.x no Broadcom (trazado incompleto entre el arranque extended y el registro
  de handles).
- Familias 21–24 ↔ modelos Mac concretos: las tablas de board-id están
  cifradas (hashes de 16 bytes ante `MGIsDeviceOneOfType`).
- Significado exacto del estado 10 del `CBAdvertiser` («restringido»).
- Ids de fabricante HCI y soporte extended advertising de MT7925 y RTL8761B, y
  el inventario de VSC Broadcom que carga BrcmPatchRAM: datos de
  especificación pública y de las herramientas de la comunidad, no verificados
  aquí.
- Internos de wirelessproxd: gates propios de Continuity (si los hay) fuera de
  bluetoothd.
