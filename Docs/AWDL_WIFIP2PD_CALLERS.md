# Callers de los ioctls de wifip2pd hasta evidencia Swift/log

Cadena de llamadores de cada sitio de [AWDL_WIFIP2PD_IOCTLS.md](AWDL_WIFIP2PD_IOCTLS.md), subiendo hasta una función con nombre Swift (`$s…`), un log string reconocible o una constante atribuible. Símbolos: el binario sólo trae nombres Swift para tipos/metadata; la mayoría de las funciones quedan como `sub_XXXX` (regla: no inventar nombres). Direcciones = VA del binario. Fuente: rea/IDA adjunto (get_callers + procedure_pseudo_code), sesión 2026-10-28.

## Tabla resumen: request → función que lo emite → evidencia → operación

| Request ioctl | Constante | Función emisora | Evidencia (string / símbolo / constante, con dirección) | Operación atribuida |
|---|---|---|---|---|
| SIOCGIFMEDIA (56) | `0xC02C6938` | `sub_100043720` (sitio `0x100043cbf`) | Strings `"IO80211Virtual…"` (`0x1000438dc`), `"WifiAwareDisco…"` (`0x1000438ed`), `"IO80211Interfa…"` (`0x1000438fe`), `"Already discove…"` (`0x100043b70`), `"Matched A servic…"` (`0x100043c14`), `"Ignoring S becau…"` (`0x100043e15`/`0x100043ec7`); propiedad IORegistry `"IOInterfaceName"` (`0x10004397c`); clase del objeto `_OBJC_IVAR_$__TtC7CoreP2P11AppleDevice_*` (`0x10004383a`, `0x10004385a`) | Descubrimiento/filtrado de interfaz: iterar IORegistry, matchear el servicio virtual y comprobar que es 802.11 (`IFM_TYPE==IFM_IEEE80211`, `0x100043ccc-0x100043cde`) |
| ↳ mismo sitio | — | `sub_100047660` (llama a `sub_100043720` en `0x100047f09`, `0x100047f2c`) | Documentado en `AWDL_WIFIP2PD_REQUESTS.md`: `RawGet` tipo 12 len 21 (`CARD_CAPABILITIES`), `RawSet` tipo 94 len 32 (`VIRTUAL_IF_CREATE`, rol 6/10 en `+0x0C`), semáforo 30 s. Nombre `"awdl"` armado para rol 6 | Creación/obtención del handle por rol (incluye AWDL, rol 6) |
| ↳ mismo sitio | — | `sub_10004A980` (llama en `0x10004abc3`) | Sin strings propios visibles (`?`); su único caller es `sub_100002048` (`0x100002211`), función contigua a `start` (0x100001ea0) | `?` (descubrimiento en el arranque, no probado con strings) |
| SIOCSIFFLAGS (16) | `0x80206910` | `sub_100056990` → `sub_100046A30` (idx 1) | Estructura observada: flags `& 0xFFBE` luego `| 0x41` (IFF_UP\|IFF_RUNNING) si el bool es 1 (`0x100056b08`, `0x100056b2e`); `getifaddrs` + `sa_family == 18` (`0x100056a2a-0x100056af1`) | Subir/bajar la interfaz virtual (up/down). Sin strings propias (`?` nombre Swift) |
| SIOCSIFLLADDR (60) | `0x8020693C` | `sub_100057120` → `sub_100046A30` (idx 2) | `sockaddr{len? 0x06, family 0x12 = AF_LINK}` @+16 y MAC 6 B @+18..23 (`0x10005725a`), MAC formateada de un objeto 0x26 B por `sub_1000574E0` (`0x100057242`) | Fijar la MAC de la interfaz. Sin strings propias (`?`) |
| SIOCSIFADDR (12) / SIOCDIFADDR (25) | `0x8020690C` / `0x80206919` | `sub_1000578F0` → `sub_100046A30` (idx 3 o 4) | `sockaddr_in{sa_len=0x10, sa_family=2 (AF_INET), addr@20}` (`0x100057987-0x100057991`); nil → idx 4 | Configurar/borrar la dirección IPv4 de la interfaz. Sin strings propias (`?`) |
| 130 IOWR 128 (tipo 7708) | `0xC0806982` | `sub_100058420` (sitio `0x1000585f8`) y `sub_1000586D0` (`0x1000588ee`) | `apple80211req` con tipo **7708** @16 (`0x1000585c3`, `0x1000588b5`), len+datos @24..39 desde `self+0x20`; socket `AF_INET6/SOCK_DGRAM` (`0x10005846e`/`0x100058730`, 30=AF_INET6 socket.h:396) | Petición apple80211 por ioctl directo (id 7708 sin nombre, `?`). Es la única vía ioctl con estructura apple80211 — candidato a "configuración AWDL por ioctl directo", pero el id no está verificado contra el driver (`?`) |
| 131 IOWR 288 | `0xC1206983` | `sub_100058420` (sitio `0x100058509`) y `sub_1000586D0` (`0x1000587e6`) | Buffer 288 B con sólo el nombre de interfaz; se ejecuta antes y como puerta del 130 (`0x10005852b-0x100058535`) | Consulta previa (GET) sobre la misma interfaz; operación exacta `?` |
| SIOCADDMULTI (49) | `0x80206931` | `sub_100057DE0` (idx 7 vía `sub_10005F7D0`) | `sockaddr_dl{sdl_len=0x14, AF_LINK, sdl_type=6 (IFT_ETHER), sdl_alen=6, MAC}` (`0x100057e88-0x100057f4b`); idx fijado en `0x10005f7f6` | Agregar dirección multicast a la interfaz |
| SIOCDELMULTI (50) | `0x80206932` | `sub_100057DE0` (idx 8 vía `sub_10005F800`) | idx fijado en `0x10005f826` | Quitar dirección multicast |

## Cadenas de callers (nivel por nivel, get_callers)

1. **SIOCGIFMEDIA:** `sub_100043720` ← `sub_100047660` (`0x100047f09`, `0x100047f2c`), `sub_10004A980` (`0x10004abc3`).
   - `sub_100047660` ← `sub_100046E30` (`0x1000471be`), `sub_10004D300` (thunk, `0x10004d318`), `sub_100207060` (`0x1002070bb`), `sub_100207160` (`0x1002071b7`).
   - **`sub_100046E30`** (0x4a8) = tope con evidencia: log `"Attempting recovery for %s[%s] with %s on %s"` (`0x100047156`), switch de rol con nombres `"Infrastructure"` (case 0, `0x100046f99`-`0x100046fad`), `"NME"` (case 2, `0x100047034`), `"NAN Data"` (case 3, `0x10004703b-0x10004704f`), `"Low Latency"` (case 4, `0x100047014-0x100047028`), `"WiFi-Aware Discovery"` (`0x10004728c`) y par `"creation"`/`" failed"` (`0x1000472a6-0x1000472b0`) en la rama de error. Llama `sub_100047660` en `0x1000471be` y, si obtiene handle, `sub_1000421E0` (`0x10004724e`). **Operación: recuperación/creación del handle de interfaz por rol (AWDL es el rol 6).** Sus callers: sin callers de código (dispatch por vtable/witness, `?`).
   - `sub_100207060`/`sub_100207160`: funciones pequeñas (0xfa/0xfe) sin strings revisados (`?`).
   - `sub_10004A980` ← `sub_100002048` (`0x100002211`); `sub_100002048` es la función contigua a `start`; sin strings revisados (`?`).
2. **SIOCSIFFLAGS:** `sub_100056990` ← `sub_10005F6B0` (thunk passthrough) ← sin callers de código (`?`, vtable/witness).
3. **SIOCSIFLLADDR:** `sub_100057120` ← `sub_10005F700` (thunk) ← sin callers de código (`?`).
4. **SIOCSIFADDR/SIOCDIFADDR:** `sub_1000578F0` ← `sub_10005F760` (thunk) ← sin callers de código (`?`).
5. **130/131 (tipo 7708):** `sub_100058420` ← `sub_10005F7B0` (passthrough) ← `sub_100068EE0` (0xb) ← **sin callers de código** (`?`); `sub_1000586D0` ← sin callers directos (`?`). Ambos con dispatch indirecto probable (vtable/witness).
6. **SIOCADDMULTI/SIOCDELMULTI:** `sub_100057DE0` ← `sub_10005F7D0` (idx 7) ← `sub_1000697E0` (0xb) ← sin callers (`?`); `sub_10005F800` (idx 8) ← `sub_1000697F0` (0xb) ← sin callers (`?`).

## Nota sobre los thunks terminales

`sub_10004D0D0` (thunk del despachador), `sub_100068EE0`, `sub_1000697E0`, `sub_1000697F0` y los puentes `sub_10005F6B0/700/760/7B0` terminan en cadenas sin callers de código: son entradas de vtable/witness Swift invocadas indirectamente. Para T2 eso significa que el punto donde una "función con nombre Swift" toma el control no es recuperable por callers directos en este adaptador; la atribución de operación queda soportada por los structs que cada wrapper arma y, en la cadena SIOCGIFMEDIA, por los strings de `sub_100043720`/`sub_100046E30`.
