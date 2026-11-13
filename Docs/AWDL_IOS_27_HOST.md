# AWDL en iOS 27.2: el lado del host en un iPhone 11

Lectura estática del IPSW `iPhone12,1_27.2_24B119_Restore.ipsw` (iPhone 11, placa `n104`), hecha el 2026-11-08 con IDA 9.4
sin ejecutar nada. Sirve para saber qué parte de lo leído en macOS 26.7.1 vale también para el iPhone de la prueba.
Las direcciones son de las copias extraídas que se describen en el §6.

## 1. Dónde está el Wi-Fi

- **El kernelcache no lo trae.** `kernelcache.release.iphone12b` tiene 243 kexts y ninguno es `IO80211Family` ni un driver
  de Broadcom. Están Skywalk, PCIe y Bluetooth, y de AWDL solo cuatro strings de red (`awdl_rtobase`, entre otros).
- **El driver es un dext.** `/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext` (5,5 MB) habla con el
  chip, el BCM4378, y lleva su firmware en `Firmware/C-4378__s-A0` a `C-4378__s-B1` (`moana.trx`, `.clmb`, `.txcb`). En AWDL
  casi todo son iovars hacia el firmware: `awdl_sync_params`, `awdl_peer_op`, `awdl_election_tree`, `awdl_aftxmode`,
  `awdl_min_rate`, `awdl_stats`, este último con contadores como `lostmaster` y `misalign`.
- **El gestor de pares está en `IO80211DriverKit`.** El dext enlaza
  `/System/DriverKit/System/Library/PrivateFrameworks/IO80211DriverKit.framework`, que solo existe dentro de la caché dyld de
  DriverKit, en el cryptex SystemOS. Esa biblioteca trae `IO80211AWDLPeerManager` con símbolos C++ y la ruta de fuente
  `IO80211_driverkit/IO80211Family/AWDL/IO80211AWDLPeerManager.cpp`.
- **`wifip2pd`** está en `/usr/libexec`, en la imagen del sistema, y depende de `WiFiPeerToPeer`, `CoreWiFi` e `IO80211`,
  que están en la caché dyld principal.

## 2. Es el mismo gestor que en macOS

`IO80211AWDLPeerManager::monitorAWDLDataMode` (`0x180b92734`) hace lo mismo que la versión de 25G241 descrita en
[AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md):

- Lee `getUnicastPacketsOutputCount` y `getUnicastPacketsInputCount`. Les resta la pasada anterior, guardada en
  `ivars+16312` y `ivars+16308`. Si el contador bajó, registra `current Tx count (%u) is less than last count` y toma el
  valor nuevo como diferencia.
- Si hubo TX o hay pares cacheados (`getAwdlCachedPeerCount`), y TX más RX llega a `ivars+16020`, o la suma guardada de la
  AW llega a `ivars+16028`, llama a `setDataMode(6, 0)` y deja `ivars+16032` en 16.
- Sin tráfico durante `16 × byte(ivars+16992) × (ivars+16032 + ivars+16024)` ms pasa a `setScheduleState(3, 6, 0)`.

`initParamsToDefaults` deja `ivars+16020` en 8, `ivars+16024` en 8, `ivars+16028` en 4 e `ivars+16032` en 0 (`0x180b79680`
a `0x180b7969c`). Son los mismos valores que en 25G241, donde los campos están en `ivars+16060`, `16064`, `16068` y
`16072`, 40 bytes más arriba. Los umbrales del modo datos del iPhone son los mismos que ya alimenta la capa.

La condición de iOS acepta también pares cacheados sin TX propio (`v12 != 0 || getAwdlCachedPeerCount() != 0`). Si la
versión de macOS tiene la misma alternativa no se revisó `?`.

## 3. Cuándo manda el iPhone una UMI

`IO80211AWDLPeerManager::handleUmiTimer` (`0x180b73bd4`) recorre los pares que esperan un camino de datos:

- Solo actúa dentro de una AW (`ivars+772` bit 0). Si no, registra `Not sending UMI as we are not in an AW`.
- Llama a `setDataMode(16, 1)`.
- El par tiene que estar en el canal actual: compara `seq2ChanInfo` del par con el canal en curso (`isSameChannel`). La
  secuencia que el iPhone usa es la que anuncia el Mac en la TLV 18.
- No repite si la última UMI fue hace 9 ms o menos (`time since last UMI`).
- No envía si el par no está cacheado en el firmware (`Not sending UMI because peer ... is not cached`).
- Arma las opciones con `getOptionsForUMI` y `getExtOptionsForUMI`, y llama a `sendUnicastMI`.

Para la prueba: con `en2` sin asociar, la TLV 18 del Mac pone el 149 solo en las franjas 2 y 10
([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17). El iPhone solo puede mandar su UMI al Mac en las franjas donde las dos
secuencias coinciden.

## 4. Lo que no responde

- **Cuánto silencio tolera el iPhone.** La sincronización y el seguimiento de las AW están en el firmware del BCM4378, y el
  dext solo pasa iovars. El firmware no se analizó. A nivel de gestor el código es el mismo que en el Mac, donde un par que
  calla sigue en `peers` entre 101 y 106 s ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §10). Eso no dice cuándo el firmware
  del iPhone pierde la sincronía `?`.
- **El valor de `awdl_min_rate`.** `setMinRate` (`0x180bac7e4`) lo manda al firmware y lo guarda en `ivars+42385`. Quién lo
  llama y con qué valor no se revisó `?`.
- **`wifip2pd`.** El texto `Considering aging out peer ...` (`sub_1004D5390`) usa `NANPeerServiceIdentifier`: es de NAN, no
  de AWDL.

## 5. Qué cambia

- Lo leído del gestor de macOS sirve como modelo del iPhone: modo datos, UMI, caché de pares y estados. Los
  desplazamientos de `ivars` difieren y hay que tomarlos de esta copia.
- Lo que sí es distinto es el firmware. Un Mac con Broadcom y un iPhone comparten el gestor, pero el T9UH no tiene firmware
  AWDL, y lo que en ellos hace el chip lo hace la capa.

## 6. Cómo se obtuvo

Todo queda fuera del repositorio, en `~/re_cache/ios/n104_24B119`.

1. El kernelcache se saca del IPSW, se quita la envoltura IM4P (DER, carga `bvx2`) y se descomprime con `lzfse`.
2. La imagen del sistema (`141-08279-215.dmg.aea`) y el cryptex SystemOS (`141-10128-227.dmg.aea`) vienen cifrados con AEA.
   Los datos de autenticación de cada uno traen `com.apple.wkms.fcs-key-url` y `com.apple.wkms.fcs-response`. La URL, de acceso
   público, da una clave privada P-256. Con ella, HPKE (P-256, HKDF-SHA256, AES-256-GCM, modo base) abre `wrapped-key`, y
   `aea decrypt -key-value base64:...` descifra la imagen.
3. Las dos imágenes son APFS y se montan con `hdiutil attach -readonly -nobrowse`.
4. `IO80211DriverKit` se extrae de `System/DriverKit/System/Library/dyld/dyld_shared_cache_arm64e` con
   `dyld_shared_cache_extract_dylibs_progress` de `dsc_extractor.bundle` (Xcode, plataforma iPhoneOS).
5. Las IDB son `~/re_cache/ida/ios_io80211dk_24B119.i64` y `~/re_cache/ida/ios_wifip2pd_24B119.i64`.
