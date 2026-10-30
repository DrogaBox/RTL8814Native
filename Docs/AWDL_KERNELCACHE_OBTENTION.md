# Obtención del kernel collection x86_64 para las revisiones por versión (macOS 14–26)

Guía práctica para conseguir el kernel collection de arranque de macOS 14, 15 y 26.x **en x86_64 y
sin instalar esas versiones**, abrirlo en IDA y producir con los scripts del repo los volcados que
consumen `Tools/re/` y la puerta de versiones. El flujo de referencia es el del kernelcache de
25.6.0 (macOS 26.7.1, build 25G241), volcado en `Docs/re/io80211_25.6.0/`.

Convenciones:

- Todo lo que no lleva `?` está confirmado por los fuentes del repo o reproducido sobre la máquina
  de trabajo (macOS 26.7.1, build 25G241, x86_64). Los `?` son pasos y rutas de instaladores o
  imágenes de restauración que esta máquina no tiene presentes: verificar al ejecutarlos.
- Este trabajo exige x86_64. Los kernelcaches de Apple silicon son arm64e y no sirven ni para las
  revisiones ni para los volcados.
- Tras cualquier actualización de macOS los volcados caducan: los slots de vtable se mueven entre
  builds (`PHASE5_IO80211.md`).

## 1. Qué archivo hay que conseguir

| Archivo (sistema instalado) | Papel | En 25.6.0 |
|---|---|---|
| `/System/Library/KernelCollections/BootKernelExtensions.kc` | Colección de arranque: `com.apple.kernel` + los kexts Apple. Aquí viven `IO80211Family`, `IOSkywalkFamily`, `IONetworkingFamily`, `corecapture`, `AppleBCMWLANCoreMac` | 67.584.000 B, md5 `f489ff8be80e6013a1bf47561dcaef45`, sha256 `21d6da1c4c3e7b72dddf5144da8a4c4b48e294249a16018a5006b61bb2155ccc` |
| `/System/Library/KernelCollections/SystemKernelExtensions.kc` | Colección de sistema: `WLANDriver` y resto de kexts auxiliares | 369 MB; las revisiones AWDL actuales no lo necesitan |

Confirmado sobre la máquina de trabajo (26.7.1):

- Ambos archivos existen en esa ruta y son legibles sin `sudo` (`-rwxr-xr-x root wheel`).
- Los directorios `.kext` de `/System/Library/Extensions` solo traen `Info.plist`: el código está
  en las colecciones (`PHASE5_IO80211.md`).
- `BootKernelExtensions.kc` es un Mach-O de tipo fileset plano, sin capa IMG4: los scripts del repo
  lo leen directamente.

Versiones objetivo y sus majores Darwin (la puerta compara el major: `GateLogic.h`):

| macOS | Darwin | builds | Instalador Intel |
|---|---|---|---|
| 14 (Sonoma) | 23 | 23Xxx | sí: 14.8.9 (23J631), §2.4 |
| 15 (Sequoia) | 24 | 24Xxx | sí: 15.8.2 (24H232), §2.4 |
| 26 (Tahoe) | 25 | 25Xxx | sí: 26.7.2 (25G332), §2.4; esta máquina ejecuta 26.7.1 x86_64, build 25G241 |

Distribución Intel frente a Apple silicon: en Intel la colección de arranque es el fileset x86_64
de `KernelCollections` (lo que usa este repo). En Apple silicon las mismas rutas existen sobre el
volumen de sistema sellado (?) pero contienen arm64e, y los IPSW de restauración traen además un
`kernelcache.release.*` arm64 (?): ninguna de las dos formas sirve para este trabajo.

## 2. Fuentes sin instalar la versión

### 2.1 Copiar de un sistema que ya ejecute la versión (vía confirmada para 25.6.0)

Es la vía que produjo el artefacto de referencia: el IDB de
`Build/ida_awdl/BootKernelExtensions.i64` se creó desde el archivo de sistema de una máquina con
25G241 y el md5 coincide con el de `/System/Library/KernelCollections/BootKernelExtensions.kc`
(`AWDL_BKE_ATTACH.md` §1). Basta copiar de cualquier Mac que ejecute la versión objetivo:

```
md5 /System/Library/KernelCollections/BootKernelExtensions.kc
cp /System/Library/KernelCollections/BootKernelExtensions.kc ~/re_cache/ida/boot_<build>.kc
```

La copia de trabajo de 25.6.0 vive en `~/re_cache/ida/boot.kc`; IDA solo se usa sobre copias
(regla de la sesión de ingeniería inversa).

### 2.2 App «Install macOS» (vía no verificada en esta máquina: no hay instalador presente)

1. Descargar el instalador completo de la versión objetivo. Los flags existen en esta máquina:

   ```
   softwareupdate --list-full-installers
   softwareupdate --fetch-full-installer --full-installer-version 14.7.1
   ```

   Qué versiones antiguas sigue ofreciendo el catálogo de Apple: `?`. Alternativas: el
   `InstallAssistant.pkg` de descarga directa (?) o la CLI mist (?) — ninguna instalada aquí.
2. La app trae el contenido en `Contents/SharedSupport/SharedSupport.dmg` (?).
3. Montar en solo lectura (flag `-readonly` confirmado en el help de `hdiutil attach`):

   ```
   hdiutil attach -readonly "/Applications/Install macOS Sonoma.app/Contents/SharedSupport/SharedSupport.dmg"
   ```

4. El volumen montado contiene el paquete completo `InstallAssistant.pkg` y el directorio de
   recuperación `com.apple.recovery.boot/` (?).
5. Expandir el paquete y localizar la colección dentro del payload:

   ```
   pkgutil --expand InstallAssistant.pkg ./ia
   ```

   `--expand` está documentado en el help de esta máquina; `--expand-full` (que además extrae el
   payload) no aparece en ese help: `?`. Dentro del payload expandido, la colección debería estar
   en `System/Library/KernelCollections/BootKernelExtensions.kc` (?) y el build en
   `System/Library/CoreServices/SystemVersion.plist` (?). Si `--expand` deja el `Payload` sin
   extraer, hace falta descomprimir pbzx/cpio a mano (?).
6. La imagen de recuperación (`BaseSystem`) trae una colección reducida (?): no sirve para las
   revisiones; usar la del payload de instalación completa.

### 2.3 Imágenes de restauración (IPSW / OTA)

- IPSW de Apple silicon: `kernelcache.release.*` arm64e. Fuera de alcance.
- IPSW de restauración de Macs Intel con chip T2: existen por modelo (?) y deberían llevar un
  kernelcache x86_64 (?); se descargan con la CLI `ipsw` (?). `ipsw` y `pyimg4` no están
  instalados en esta máquina (?).
- Actualizaciones OTA de Intel vía `ipsw download ota` (?).
- Solo esta ruta necesita desempaquetar IMG4/im4m (`pyimg4` o equivalente, ?): el KC de un sistema
  instalado o de un instalador es un Mach-O fileset plano.

### 2.4 `InstallAssistant.pkg` del catálogo de actualizaciones (vía confirmada para 14, 15 y 26)

Reproducida el 2026-10-30 en la máquina de trabajo para 14.8.9, 15.8.2 y 26.7.2, sin instalar
ninguna de ellas y sin `sudo`:

1. El catálogo público
   `https://swscan.apple.com/content/catalogs/others/index-26-15-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-mountainlion-lion-snowleopard-leopard.merged-1.sucatalog.gz`
   lista cada instalador completo como un producto con un `InstallAssistant.pkg` en
   `swcdn.apple.com`. El `.dist` de cada producto da versión y build. Tamaños descargados:
   13.654.790.295 B (14.8.9), 15.664.531.445 B (15.8.2) y 18.389.893.051 B (26.7.2).
2. `xar -xf InstallAssistant.pkg SharedSupport.dmg` saca la imagen sin expandir el resto del
   paquete. Se monta con `hdiutil attach -readonly -nobrowse`.
3. La imagen contiene un zip de asset (más de 1 GB) y el sistema va en sus trozos
   `AssetData/payloadv2/payload.NNN`, en formato Apple Archive. El zip no lleva la colección suelta:
   en `AssetData/boot/` solo hay los manifiestos `BootKernelExtensions.kc.*.im4m` y, en 14.8.9,
   un `kernelcache.release.mac15j` de 27.835.689 B que este flujo no usa.
4. `aa` no lee de la entrada estándar, así que cada trozo se vuelca a un archivo temporal con
   `unzip -p` y se lista con `aa list -i`. El trozo que contiene
   `System/Library/KernelCollections/BootKernelExtensions.kc` se extrae con
   `aa extract -i <trozo> -d extracted -include-path <ruta>`. `SystemVersion.plist` se busca igual.
   Los scripts usados están en `~/re_cache/kc/extract_kc.sh` y `find_kc_chunk.sh`.
5. El resultado es el mismo Mach-O fileset plano que en un sistema instalado. IDA lo abre entero.

| Build | macOS | `BootKernelExtensions.kc` | md5 | sha256 |
|---|---|---|---|---|
| 23J631 | 14.8.9 | 62.783.488 B | `2ab1c3ac3db747642bf4cd6ac73e12f5` | `3f5dcde57f2d0ce27b9c9abf3d1e3d2c36b33cea9ae0a7eb36bf9c4282ed8cd2` |
| 24H232 | 15.8.2 | 66.125.824 B | `bcf4c23aa5f52f69951aea31c044f5de` | `ddd9016e225a61ebfd4f04b02a00937262f26690c57a928613e08506978c81be` |
| 25G332 | 26.7.2 | 67.600.384 B | `b9da5eafcd90e439ae7c55c1ca8ddf33` | `96c12f421a8e99580b5015dfd39877df38ecde9b9e056b8ccd28f15847bca35c` |

El build sale del `ProductBuildVersion` del `SystemVersion.plist` extraído del mismo payload.

## 3. Identidad del artefacto

Antes de volcar nada, registrar build, tamaño, md5 y sha256 de la copia:

```
ls -l <copia.kc>
md5 <copia.kc>
shasum -a 256 <copia.kc>
```

El build se lee con `sw_vers -buildVersion` en la máquina origen o, en un payload de instalador, en
su `SystemVersion.plist` (§2.4); las dos vías están confirmadas. Falta verificar que el KC de un
instalador coincide con el de un sistema instalado del mismo build `?`. Se comprueba comparando el
md5 de 25G332 de la tabla del §2.4 con el de `/System/Library/KernelCollections/` después de
actualizar esta máquina a 26.7.2. Ejemplo de fila de registro (la que usa el
HANDOFF para 25.6.0):

| Build | macOS | Archivo | Tamaño | md5 |
|---|---|---|---|---|
| 25G241 | 26.7.1 | `BootKernelExtensions.kc` | 67.584.000 B | `f489ff8be80e6013a1bf47561dcaef45` |

## 4. Abrir el collection en IDA (sin partirlo)

El flujo del repo no parte el KC: IDA 9.x lo abre entero como Mach-O fileset (base del kernel
`0xffffff8000200000`, tamaño de módulo `0x3fe506c`; `AWDL_BKE_ATTACH.md`). Regla operativa: IDA
headless solo sobre copia del IDB o del KC, nunca sobre el original.

La mecánica del fileset, por si una herramienta exige un Mach-O por kext: cada
`LC_FILESET_ENTRY` (cmd `0x80000035`) trae la vmaddr, el file offset y el nombre de la entrada, y
los load commands propios de esa entrada empiezan en su file offset; dentro de cada entrada,
`LC_SEGMENT_64` (cmd `0x19`) da vm/vs y fo/fs de cada segmento. Enumeración verificada sobre el KC
de esta máquina (mismo recorrido que hace `Tools/re/kcsyms.py`):

```python
import struct, sys
kc = open(sys.argv[1], 'rb').read()
def lcs(off):
    ncmds = struct.unpack_from('<I', kc, off + 16)[0]; p = off + 32
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from('<II', kc, p); yield cmd, p, cs; p += cs
for cmd, p, cs in lcs(0):
    if cmd == 0x80000035:
        vm, fo, eo = struct.unpack_from('<QQI', kc, p + 8)
        name = kc[p + eo:p + cs].split(b'\0')[0].decode()
        print(name, hex(vm), hex(fo))
```

El corte físico de una entrada sería `kc[fo : fo+fs]` de sus segmentos (?), y herramientas
externas de split (?) no están instaladas aquí. No hace falta para nada del flujo del repo: cada
entrada lleva su propia tabla de símbolos y los scripts la leen in situ.

## 5. Volcados de símbolos (formato del repo)

`Tools/re/kcsyms.py <kc> <bundle-id>` vuelca los segmentos (a stderr) y la symtab de una entrada
del fileset. Los seis volcados de 25.6.0 se reproducen byte a byte con estos comandos (verificado
por md5 sobre esta máquina):

```
mkdir -p Docs/re/io80211_<darwin>.<delta> && cd Docs/re/io80211_<darwin>.<delta>
KC=/ruta/a/BootKernelExtensions.kc
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.kernel                        > syms_com.apple.kernel.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.iokit.IONetworkingFamily      > syms_com.apple.iokit.IONetworkingFamily.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.iokit.IOSkywalkFamily         > syms_com.apple.iokit.IOSkywalkFamily.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.iokit.IO80211Family           > syms.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.driver.corecapture            > syms_com.apple.driver.corecapture.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.driver.AppleBCMWLANCoreMac    > syms_bcm.txt
```

Formato de línea: `<VA hex 16> <tipo nlist> <nombre>`. Tipos observados: `0f` símbolo definido
externo, `0e` definido local, `01` indefinido externo. Una línea con VA `0000000000000000` es una
referencia indefinida de ese módulo, no una definición (`Tools/awdl_recheck_symbols.py`). Verificar
completitud: kcsyms.py anuncia `SYMTAB <n>` por stderr y el volcado debe tener `n` líneas (en
25.6.0, `syms.txt` tiene exactamente 20001).

Consumo de los volcados:

| Herramienta | Volcados que pide |
|---|---|
| `Tools/awdl_recheck_symbols.py` | los cinco `syms*.txt` del sistema + familia (`kernel`, `IONetworkingFamily`, `IOSkywalkFamily`, `corecapture`, `syms.txt`); con los cinco, 25.6.0 da 128/128 OK |
| `Tools/re/check_io80211.sh <dump_dir>` | glob `syms*.txt` del directorio, excluye el tipo `01` |
| `Tools/re/vt.py` | todos los `syms_*.txt` del directorio de trabajo |

El conjunto de 25.6.0 incluye además `classes.txt` (lista de nombres de clase, origen no
documentado en los scripts: ?).

## 6. Volcados de vtables

`Tools/re/vt.py <Clase>` se ejecuta desde el directorio de volcados (necesita los `syms_*.txt` ahí
y `c++filt` en el PATH) y escribe `vt_<Clase>.txt` con una línea por celda:
`<índice> 0x<offset> <nombre demangleado>`.

- Los punteros del KC usan chained fixups: los 30 bits bajos son el desplazamiento del destino
  desde la vmaddr más baja del KC (`0xffffff8000100000`, `__HIB`), no desde `__TEXT`
  (`0x...200000`) — `PHASE5_IO80211.md`. Esa base es una constante de 25.6.0 dentro de `vt.py`.
  `otool -l` da el mismo par `__HIB 0xffffff8000100000` / `__TEXT 0xffffff8000200000` en los KC
  de 23J631, 24H232, 25G241 y 25G332, así que la constante vale para 14, 15 y 26.
- La ruta del KC está fija dentro de `vt.py` (`/System/Library/KernelCollections/
  BootKernelExtensions.kc`): tal cual solo analiza el KC del sistema en marcha; para una copia de
  otra versión habría que apuntar esa constante a la copia (edición de código: pendiente de OK).
- El juego de vtables de 25.6.0: la cadena de 7 clases de la puerta (`IOSkywalkInterface`,
  `IOSkywalkNetworkInterface`, `IOSkywalkEthernetInterface`, `IO80211SkywalkInterface`,
  `IO80211InfraInterface`, `IO80211InfraProtocol`, `IO80211Controller`), sus padres SDK
  (`IOService`, `IOEthernetController`) y las implementaciones Apple que nombran los slots puros
  (`AppleBCMWLANCore`, `AppleBCMWLANInfraProtocol`, `AppleBCMWLANSkywalkInterface`).

## 7. Header y puerta de versiones

Cadena completa para una versión nueva (lo no marcado está confirmado en los scripts):

1. Volcados de símbolos (§5) y de vtables (§6) en `Docs/re/io80211_<darwin>.<delta>/`.
2. Header slot-exact: `python3 Tools/re/gen_io80211.py <dump_dir> Source/IO80211/IO80211_<build>.h`.
   Los tamaños de instancia (`SIZES`) los toma del `OSMetaClass(name, super, size)` del KC y en el
   script están los de 25.6.0 a mano: para otra versión hay que medirlos de nuevo en IDA y
   actualizarlos (edición de código: pendiente de OK).
3. Verificación estática: `Tools/re/check_io80211.sh <dump_dir>` (vtables de un stub contra las de
   Apple, imports contra los volcados) y
   `python3 Tools/awdl_recheck_symbols.py <los cinco volcados>`.
4. Puerta: `python3 Tools/airlayer_gate.py gen` regenera `Source/AirLayer/KnownBuilds.inc`
   (no se edita a mano) desde el header y `Tools/airlayer_builds.txt`. La constante `HEADER` del
   script apunta hoy a `IO80211_25_6_0.h`: otra versión exige su header (edición de código:
   pendiente de OK).
5. Validación en hardware: la puerta compara en runtime el major Darwin y, por clase, existencia,
   tamaño de instancia y superclase (`GateLogic.h`), y además exige que el build esté en la lista
   validada. Un build nuevo se prueba con el boot-arg `rtlairtrust=<build>` (`airlayer_gate.py
   status` lo imprime) y solo tras pasar las pruebas se añade con
   `Tools/airlayer_gate.py learn --yes`, que escribe la línea `build <build> <versión> validated
   <fecha>` en `Tools/airlayer_builds.txt` y regenera el `.inc`.

El md5 del KC no entra en la puerta: es trazabilidad del artefacto que produjo los volcados (como
la fila de 25G241 en `HANDOFF_AWDL.md`). Anotarlo junto al directorio de volcados.

## 8. Registro recomendado por versión

```
Docs/re/io80211_<darwin>.<delta>/
  syms.txt                                   com.apple.iokit.IO80211Family
  syms_com.apple.kernel.txt                  com.apple.kernel
  syms_com.apple.iokit.IOSkywalkFamily.txt   com.apple.iokit.IOSkywalkFamily
  syms_com.apple.iokit.IONetworkingFamily.txt com.apple.iokit.IONetworkingFamily
  syms_com.apple.driver.corecapture.txt      com.apple.driver.corecapture
  syms_bcm.txt                               com.apple.driver.AppleBCMWLANCoreMac
  vt_<Clase>.txt                             una por clase del §6
  KC.md5 / KC.sha256                         identidad del artefacto origen
```

Checklist por versión: copia del KC con build/md5/sha256 registrados → seis volcados con el conteo
de `SYMTAB` cuadrado → `vt_*.txt` → header + `check_io80211.sh` + `awdl_recheck_symbols.py` →
solo después de probar en hardware, `learn --yes`.
