# AppleSunriseWLAN: el driver MediaTek de Apple en macOS 26.7.3

Leído el 2026-11-23 en IDA sobre la copia x86_64 del dext instalado en 25G342. Nada se ejecutó: no hay
una placa MediaTek en esta máquina. IDB: `~/re_cache/ida/sunrise_25G342/AppleSunriseWLAN.i64`.

## 1. Qué es

- Ruta: `/System/Library/DriverExtensions/com.apple.AppleSunriseWLAN.dext`, firmado por Apple, parte
  del volumen de sistema sellado. `DTPlatformBuild` 25G325.
- Binario universal x86_64 + arm64e, con símbolos C y C++.
- El código es el driver MediaTek "gen4m" (nombres `aisFsm`, `StaRec`, `cnm`, `heRlm`, `wlanoid*`)
  adaptado por Apple a DriverKit e IO80211: fuentes `MTK/chips/mt7922`, `MTK/chips/mt7961`,
  `MTK/os/dale`, `Apple/AppleSunriseWLAN.cpp`.
- Trae su propia interfaz AWDL (`AppleSunriseWLANAWDLInterface`, rol "AirLink") y un motor de datos
  AWDL (`awdlDataEngine*`, `awdlPeer*`, `awdlRaFillPerChInfo`).
- Firmware en la carpeta `IZUBA/` del bundle: solo archivos MT7932 (`IZUBA_WIFI_MT7932_patch_mcu_1_2_hdr.bin`,
  `IZUBA_WIFI_RAM_CODE_MT7932_1_2_idxlog.bin`, `IZUBA_W7932_2.bin`, EEPROM, PPR, tablas de potencia,
  `wifi.cfg`).

## 2. Con qué hardware se engancha

- Personalidad principal: `IOProviderClass` = `IOPCIDevice`, `IOPCIMatch` =
  `0x792214c3 0x792314c3 0x793214c3` (MediaTek 7922, 7923 y 7932). No depende de un kext propio.
- `mtk_pci_probe` copia el device ID al identificador de chip (`prChipInfo` +48): 0x7922, 0x7923 o
  0x7932. Un device ID 0x0616 se trata como 0x7922, aunque no figura en `IOPCIMatch`.
- El cliente HAL (`AppleSunriseHALClient`, servicio `/IOResources/AppleSunriseHALDevice`, kext
  `com.apple.driver.AppleSunrise`) no existe en esta instalación x86. El código lo trata como opcional:
  "(Non-fatal) Failed to create HAL client".

## 3. El bloqueo para una placa 7922 o 7923

- Los nombres de firmware salen del identificador de chip: `mt7922ConstructPatchName` arma
  `WIFI_MT%x_patch_mcu_%x_%x_hdr.bin` con ese identificador; la RAM sigue
  `WIFI_RAM_CODE_MT%x_%x_%x_idxlog.bin`. `kalFirmwareImageMapping` antepone `IZUBA/IZUBA_`.
- `AppleSunriseWLANFirmwareLoader::LoadFirmware` solo lee del propio bundle
  (`OSBundle::loadResource`) y valida cada archivo con `IOFileValidationRequestImageVerify` contra
  `fileValidationPlatformConfigSHA_array`: SHA-256, uno por archivo. Los 18 archivos de `IZUBA/`
  coinciden con entradas de esa tabla; no hay firmware 7922 ni 7923.
- Consecuencia: con una placa 7922 el dext sin cambios buscaría `IZUBA/IZUBA_WIFI_MT7922_...`, que
  no está en el bundle ni en la tabla de confianza. Para que cargue haría falta un dext modificado
  (archivos y hashes), es decir, salir del volumen sellado y volver a firmarlo. No se probó.

## 4. AWDL: diferencias con Broadcom

| Punto | AppleBCMWLAN (Broadcom) | AppleSunriseWLAN (MediaTek) |
|---|---|---|
| Datos del par que usa del peer cache | bytes 16 (HT), 44, 45 (VHT), 60 (HE), 68 (ancho), 69 y siguientes (secuencia) | bytes 4, 8-15, 44, 45, 60 y la lista de canales; no usa el byte 68 |
| Capacidades HE del par | elemento fijo de 24 bytes: 40/80 MHz en 5 GHz, un flujo | `heRlmReqFillAWDLHeCapIE` → `heRlmFillHeCapIE`, el mismo constructor que la red normal |
| 160 MHz | no | sí: bit 0x08 del conjunto de anchos y mapa MCS de 160 cuando el ancho máximo del BSS AWDL (`cnmGetBssMaxBw`) es 160 |
| Ancho por ranura | clase de ancho del byte 68 en el TLV 18 | clase de operación de cada ranura (`awdlRaFillPerChInfo` → `regGetBw`) |
| EHT (Wi-Fi 7) en AWDL | no | no |

## 5. Para qué sirve

- Referencia de un driver AWDL completo escrito por Apple para un chip que no es Broadcom, con
  símbolos y en x86_64: estación AWDL, programación del par, ventanas y ancho por canal.
- Respuesta a la pregunta de 160 MHz en AWDL: Apple lo permite en su ruta MediaTek, no en la Broadcom.
- Posible camino de Wi-Fi nativo para placas MediaTek 7922 en hackintosh, con el bloqueo del §3.

## 6. Arranque: qué exige antes del firmware

Leído el 2026-11-23 (`Start_Impl`, `driverInitAsync`, `_glueInit`, `pci_notify_attach`, `LoadFirmware`).

- Sin comprobación de modelo de Mac, board-id, ACPI ni subsystem ID en el camino de arranque. Las
  únicas puertas de hardware son el match PCI, `IOPCIDevice::Open` con la capacidad PCIe (0x10) y el
  registro de chip `WCIR_CHIP_ID` (`halVerifyChipID`, BAR0+0x1008).
- La tabla interna de `pci_check_match` acepta 14c3:0616 además de 7922/7923/7932, pero `IOPCIMatch`
  no incluye 0616: una placa RZ616 con ese ID necesita además cambiar la personalidad.
- Cliente HAL (`IOService::Create("AppleSunriseHALClient")`, servicio de kernel
  `com.apple.driver.AppleSunrise`): si falla con `kIOReturnDeviceError` el arranque aborta; con otro
  código sigue con el cliente en NULL. No se comprobó qué código devuelve en x86 sin ese kext.
- Sin HAL no se lee `module-instance` de `/arm-io/wlan`, y `populateVendorInfo` (0x10039fa0c) usa ese
  `OSString` sin comprobar NULL (verificado). El boot-arg `mtk.wlan.platform=<nombre>` escribe un
  `OSString` en ese mismo campo (ivars+4728, deducido de la aritmética del offset en `Start_Impl`), lo
  que evitaría ese NULL.
- Firmware: `LoadFirmware` (0x10005aac0) lee el boot-arg `ioimageloader.debug.csr-access`; si no es
  0, se salta `IOFileValidationRequestImageVerify` y usa el archivo tal cual (verificado). Los archivos
  igual tienen que estar dentro del bundle del dext, así que una placa 7922 sigue necesitando una copia
  del dext con el firmware 7922 agregado y firmada de nuevo, pero ya no hace falta tocar la tabla SHA.
- No se comprobó si un dext recibe esos boot-args en una máquina sin SIP desactivado, ni qué hace
  `panic()` dentro de un dext (proceso o sistema completo).

Fuente del detalle: nota de trabajo `~/re_cache/work/awdl_refs/notes/sunrise_start_impl.md`; los
puntos marcados como verificados se comprobaron en el desensamblado.

## 7. macOS 27 (27.2.1, 26B131)

Comparación por símbolos, cadenas y plists (sin IDA) de la imagen de sistema del IPSW UniversalMac
26B131 contra 25G342; detalle en `~/re_cache/work/awdl_refs/notes/macos27_survey.md`, archivos en
`~/re_cache/macos27/26B131/extract/`.

- Todos los dexts de Wi-Fi y Bluetooth de 27 son solo arm64e (comprobado con `lipo`): AppleSunriseWLAN,
  AppleSunriseBluetooth, AppleBCMWLAN, AppleCentauri*. La copia x86_64 de 26.x es la última.
- AppleSunriseWLAN 27: mismo `IOPCIMatch`, misma carpeta `IZUBA/` con los mismos 18 nombres (8 archivos
  con contenido nuevo), +279/−142 símbolos. Cambios AWDL: entra `setAGGRESSIVE_EDCA` y la cola de
  paquetes pendientes; sale `setLowLatencyTrafficState`. Mismos 57 identificadores `AWDL_*`; sin EHT,
  320 MHz ni MLO en AWDL.
- IO80211DriverKit 27 (caché DriverKit, extraída con `dscx`, IDB
  `~/re_cache/ida/macos27_26B131/IO80211DriverKit.i64`, 14 795 funciones con nombre) comparado con
  IO80211Family 26.7.3: mismas tres consultas HT/VHT/HE (GET 154, 214, 570), mismos bits 143 (HE) y 95
  (6 GHz), `channelSequenceMinimumBW` sigue tope 80 MHz, clase de ancho del par sigue 1-4 y el peer
  cache control no cambia de layout. Sin EHT en AWDL.
- Nuevo en 27 (verificado): `IO80211AWDLPeerManager::sendAggresiveEdca(bool)` (0x18082E4F0) manda SET
  ioctl 607 (0x25F, `APPLE80211_IOC_AGGRESSIVE_EDCA`, int de 4 bytes) si la capacidad 179 está puesta;
  lo dispara `setAirDropState` (0x18082E01C) con un mínimo de 30 s entre activaciones, y un timer lo
  apaga. En 26.7.3 no existe ni la función ni el nombre del ioctl.
- `IO80211BufferPool::withOptions(opts)` es una sobrecarga nueva que llama a la de siempre con
  `false`; la versión con `bool` sigue existiendo (corrige lo que decía la primera lectura por
  importaciones).
- Daemons: `wifip2pd` crece ~31 % con una política `AWDLDiscoveryTimeout` (apaga AWDL en Macs con la
  pantalla apagada pasado un umbral, con excepciones por servicio); `sharingd` pierde las operaciones
  de ranging AWDL. Solo cadenas, sin descompilar.
- Siguen siendo universales (x86_64 + arm64e) en 27: `bluetoothd`, `wifianalyticsd`, `wifivelocityd`.
- AppleSunriseBluetooth (también x86_64 + arm64e en 25G342) engancha PCI `0x792a14c3 0x792b14c3
  0x793b14c3`: el Bluetooth del chip de Apple es una función PCI propia. En las placas MT7922 comunes
  el Bluetooth va por USB, así que este dext no las tomaría (deducido de los IDs, no probado).
- El índice de dortania lista un KDK para 26B131; no se descargó.

## 8. Pendiente

- Ver si existe un dispositivo con device ID 0x7932 fuera de Apple.
- Código de error de `IOService::Create("AppleSunriseHALClient")` sin el kext de kernel.
- No se probó cargar el dext en ninguna máquina.
