# Revisión del despliegue de la etapa 1 (deploy y construcción)

Fecha: 2026-10-29. Revisión estática, sin tocar el EFI ni el árbol: no se ejecutó el despliegue y no
se modificó archivo alguno salvo este informe. Ámbito: `Tools/phase5_deploy.sh`, `build_airlayer.sh`,
`build.sh` y `Tools/phase5_efi_check.py`. La opción revisada es `RTL8814_AWDL=1`, que añade el
boot-arg `rtlawdl=1`; existe solo en el despliegue — `build.sh` (RTL8814Native) no contiene ninguna
referencia a AWDL, y `build_airlayer.sh` compila la vía AWDL de forma incondicional pero inerte sin
el boot-arg, de modo que la activación exige una acción explícita de despliegue y otra de arranque.

Método: los casos marcados «medido» se reprodujeron con el intérprete del shebang (bash 3.2,
`set -euo pipefail`) y con PlistBuddy sobre un plist de prueba, sin acceso al EFI; «?» marca lo no
verificable sin escribir en el EFI.

## Estado tras la verificación

Aplicadas en `Tools/phase5_deploy.sh`: incidencias 1 (con una precisión: el filtro de boot-args original, `^rtlair(step|awdl)?=`, no coincidía con `rtlawdl=` y no lo eliminaba, de modo que un redeploy sin la opción acumulaba el token en lugar de quitarlo; el patrón ahora es `^(rtlair(step)?|rtlawdl)=`, y la forma documentada es `sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh ...`), 2, 3 y 6. Medido el filtro nuevo sobre `rtlair=2 rtlawdl=1 keepsyms=1 -v` (resultado `keepsyms=1 -v`), sobre una lista solo con tokens `rtlair*` y sobre lista vacía (sin abortar). Quedan sin aplicar 4, 5, 7, 8, 9 y 10 (baja gravedad).

## Incidencias

| # | Incidencia | Evidencia | Gravedad | Corrección propuesta |
|---|---|---|---|---|
| 1 | La forma natural `RTL8814_AWDL=1 sudo ./Tools/phase5_deploy.sh …` no propaga la variable: sudo la descarta del entorno (`env_reset`, ?) y, al no llegar, el filtro de la línea 70 **quita** `rtlawdl` de boot-args. Un redeploy corriente desarma AWDL sin ningún aviso específico (solo sería detectable leyendo la línea `boot-args:` final) | `Tools/phase5_deploy.sh:3,8,70,73` | alta | Documentar la forma que sí propaga: `sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh "<EFI>" layer 2`; añadir aviso al desarmar (§Fragmentos, 1) |
| 2 | Los fuentes por defecto apuntan a builds históricos: driver 4.22 (`36ea1ac8…`) y layer 0.30 pre-AWDL (`d84740a6…`), mientras `Build/` contiene 4.35 (`d96b50e1…`) y layer 0.32 con AWDL (`535c1767…`), y el EFI desplegado corre 4.34 con un layer posterior. Los comandos documentados sin variables **degradan el EFI en silencio**: el chequeo final no marca 4.22/0.30 como superseded, así que no aparece ningún «!». Medido: ambos MD5 por defecto existen y coinciden con sus pines — el problema es el desfase, no la integridad | `Tools/phase5_deploy.sh:26-29`; `Tools/phase5_efi_check.py:19-81` | media | Por defecto tomar `Build/` y derivar el pin del artefacto recién construido (§Fragmentos, 2); un pin explícito sigue ganando |
| 3 | `set_rtlair` aborta el script (tras el backup, sin escribir nada) cuando todos los boot-args son tokens `rtlair*`: `grep -v` sin líneas seleccionadas devuelve 1, `pipefail` lo propaga al `$( )` y `set -e` corta (medido: `args="rtlair=2"` → salida 1 sin mensaje). También corta si la clave `boot-args` no existe: `Print` de PlistBuddy devuelve 1 (medido) | `Tools/phase5_deploy.sh:69-70` | media | `\|\| true` en el `Print` y en el `grep` (§Fragmentos, 3) |
| 4 | La restauración no está documentada: solo se menciona la ruta del backup; el script no ofrece vuelta atrás y no hay ningún comando de restore en README.md ni en Docs/PHASE5_IO80211.md | `Tools/phase5_deploy.sh:12,38` (sin más menciones) | media | Añadir al encabezado el comando único de la §Restauración |
| 5 | El chequeo final corre con `‖ true`: el script acaba en 0 aunque el chequeo haya encontrado problemas (build superseded, ficheros `._`, boot-args fuera de `NVRAM→Delete`, orden de Kernel→Add); los «!» se imprimen pero el estado de salida no los refleja | `Tools/phase5_deploy.sh:116`; `Tools/phase5_efi_check.py:162` | baja | `python3 Tools/phase5_efi_check.py "$EFI" \|\| echo "WARNING: phase5_efi_check reported problems (see above)"` |
| 6 | Sin comprobación de root: ejecutado sin sudo, el script crea el backup local y muere después con un error de permisos crudo en la primera escritura del EFI | `Tools/phase5_deploy.sh:17-44` | baja | `[ "$(id -u)" = 0 ] \|\| { echo "run with sudo (it writes to the EFI)"; exit 1; }` tras la línea 18 |
| 7 | En `layer` el kext se sustituye antes de validar que RTL8814Native está en Kernel→Add: si falta, el EFI queda con la capa nueva instalada y sin entrada en config (OpenCore la ignora; el backup permite volver) | `Tools/phase5_deploy.sh:85-88` | baja | Mover la validación con `kext_index` (86-101) antes del `install_kext` de la línea 85 |
| 8 | `kext_index` da por terminada la lista ante una entrada con `BundlePath` vacío (el `while bp="$(…)"` usa la cadena vacía como fin): un falso «no está» añadiría una entrada duplicada. Caso límite: todas las entradas que escribe este script llevan BundlePath | `Tools/phase5_deploy.sh:61` | baja | `while $PB -c "Print :Kernel:Add:$i:BundlePath" "$CFG" 2>/dev/null | IFS= read -r bp; do` |
| 9 | El build del layer no imprime el MD5 del binario, que es justo el valor que el pin del despliegue espera; la fricción invita a desplegar el default desfasado de la incidencia 2 | `build_airlayer.sh:72` | baja | Añadir `md5 -q "$KEXT/Contents/MacOS/RTL8814AirLayer"` al cierre |
| 10 | `ARCH` solo llega al enlazador; la compilación usa el arch del anfitrión. Inofensivo con el objetivo fijo x86_64; un futuro `ARCH=arm64` fallaría tarde, en `ld` | `build_airlayer.sh:15,39-40,64`; `build.sh:15,271` | informativa | Pasar `-arch "$ARCH"` a clang/clang++ en ambos scripts |

## Fragmentos detallados

1. Aviso al desarmar `rtlawdl` (insertar en `set_rtlair`, tras la línea 73):
   ```bash
   case " $args " in *" rtlawdl="*) [ "${RTL8814_AWDL:-0}" = 1 ] || \
       echo "note: rtlawdl removed (use: sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh ...)";; esac
   ```
2. Defaults al artefacto recién construido (sustituir las líneas 26-29; el MD5 derivado deja de
   desfarse y el pin explícito por entorno sigue teniendo prioridad):
   ```bash
   DRIVER_SRC="${RTL8814_DRIVER_SRC:-Build/RTL8814Native.kext}"
   LAYER_SRC="${RTL8814_LAYER_SRC:-Build/RTL8814AirLayer.kext}"
   DRIVER_MD5="${RTL8814_DRIVER_MD5:-$(md5 -q "$DRIVER_SRC/Contents/MacOS/RTL8814Native")}"
   LAYER_MD5="${RTL8814_LAYER_MD5:-$(md5 -q "$LAYER_SRC/Contents/MacOS/RTL8814AirLayer")}"
   ```
3. Filtrado de boot-args tolerante a listas sin resto y a la clave ausente (sustituir las líneas 69-70):
   ```bash
   args="$($PB -c "Print :NVRAM:Add:$GUID:boot-args" "$CFG" 2>/dev/null || true)"
   new="$(printf '%s\n' "$args" | tr ' ' '\n' | { grep -vE '^rtlair(step|awdl)?=' || true; } | tr '\n' ' ' | sed 's/ *$//')"
   ```

## Verificado correcto

- **Backup antes de cualquier escritura en el EFI**: `config.plist` y los dos kexts presentes se
  copian a `Backups/efi_phase5_<fecha>_<pid>/` (líneas 38-44) antes de `install_kext` (52-53) y del
  `Set` de boot-args (74). Dentro de `install_kext` el orden es verificar MD5 → borrar → copiar.
- El bucle de backup `[ -d … ] && cp -R …` parece el fallo clásico de `set -e` en un cuerpo de
  bucle y no lo es: medido con bash 3.2 (`set -ec`), la ausencia de uno de los dos kexts no aborta
  el script.
- **Fusión de boot-args**: el filtro solo elimina `rtlair=`, `rtlairstep=` y `rtlawdl=`; el resto de
  tokens se conservan (medido: `rtlair=2 keepsyms=1 -v` → `keepsyms=1 -v` más los re-añadidos).
  Reejecutar con el mismo entorno no duplica tokens, porque el filtro los quita antes de
  re-añadirlos.
- **Idempotencia**: `driver` y `layer` repetidos producen el mismo estado del EFI (rm+cp con MD5
  verificado; la entrada de Kernel→Add solo se añade si `kext_index` no la encuentra; los boot-args
  resultantes son deterministas). Los backups acumulan un directorio por ejecución con fecha y PID,
  sin colisiones.
- **`rtlawdl` no se activa por accidente**: solo el valor literal `RTL8814_AWDL=1` lo añade
  (cualquier otro valor, incluida la ausencia, lo omite o lo quita); `rtlair` se valida a {1,2} y
  `rtlairstep` a {1-4}; `inert` nunca lo añade; y el kext vuelve a comprobarlo en runtime
  (`PE_parse_boot_argn("rtlawdl", …)`, `RTLAirLayer.cpp:1453-1457`) después de la puerta de versión.
  El paso `driver` no toca boot-args: un redeploy del driver conserva `rtlawdl` y `rtlair`.
- Rutas con espacios entrecomilladas en todas las expansiones (funciona con «/Volumes/NO NAME») y
  guardia de disco interno antes de escribir (32-36), que rechaza un EFI en disco externo o USB.
- `set -euo pipefail` presente en los tres scripts; parámetros tratados con `${3:?}`/`${4:-}`;
  `plutil -lint` tras las escrituras corta con backup disponible; un fallo de `codesign` no aborta
  el build (aviso explícito) en ninguno de los dos scripts.
- `build.sh` no contiene ninguna referencia a AWDL ni a `RTL8814_AWDL`: el driver nativo no cambia
  con la opción. Versiones consistentes: KMOD `0.32.0` (`build_airlayer.sh:50`) =
  `CFBundleVersion` de `Source/AirLayer/Info.plist` (medido); el native mantiene 0.1.0 en ambos.
- El build del layer es regenerativo completo (`rm -rf` de objetos y kext) y termina con la
  tabla de builds conocidos regenerada (`airlayer_gate.py gen`), que corta el build si la cabecera
  o la tabla no cuadran.
- `Tools/phase5_efi_check.py` es de solo lectura y cubre: build superseded, ficheros `._`, orden y
  Enabled en Kernel→Add, capa en config sin kext y en Kexts sin config, `rtlair` sin capa, y
  `boot-args` fuera de `NVRAM→Delete`.

## Restauración

Con el EFI montado y desde la raíz del repo, un solo comando devuelve el estado previo al último
despliegue (el backup contiene `config.plist` y los kexts que existieran; restaurar `config.plist`
revierte también los boot-args, `rtlair`/`rtlawdl` incluidos; el borrado previo elimina un kext
añadido por el despliegue que no estuviera en el backup):

```bash
sudo sh -c 'B="Backups/efi_phase5_<fecha>_<pid>"; E="<EFI>"; cp "$B/config.plist" "$E/EFI/OC/config.plist" && rm -rf "$E/EFI/OC/Kexts/RTL8814Native.kext" "$E/EFI/OC/Kexts/RTL8814AirLayer.kext" && cp -R "$B"/RTL8814*.kext "$E/EFI/OC/Kexts/"'
python3 Tools/phase5_efi_check.py "<EFI>"
```

Tras reiniciar, conviene verificar que los boot-args restaurados han tomado efecto
(`nvram boot-args`); el chequeo ya avisa si `boot-args` no figura en `NVRAM→Delete`.
