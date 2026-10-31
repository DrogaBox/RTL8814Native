# AWDL sin asociación de infraestructura

Qué hace `IO80211AWDLPeerManager` cuando la interfaz primaria no está asociada: qué caminos apagan
AWDL, qué secuencia de canales construye sin canal de infraestructura, qué consultas lanza sobre la
primaria y qué hace wifip2pd en ese estado. Base: colección de kernel 25G241 (macOS 26.7.1, x86_64)
sobre una copia de la base anotada; binario `wifip2pd` de 25G241. Cada afirmación lleva la VA de la
función o de la instrucción; lo no observado va con `?`.

Convenios: `ivars` es `*(manager+0x20)` y `ctrl` es `*(manager+0x18)` (el `IO80211Controller`).
`ctrl+2496` (0x9C0) es el indicador de asociación, `ctrl+2492` (0x9BC) el `apple80211_channel_data`
de la infraestructura (canal en `+4`) y `ctrl+2500` (0x9C4) sus flags (bit `0x2000` = 6 GHz).

## 1. Estado de la infraestructura en el controlador

- `IO80211AWDLPeerManager::associated()` (`0xffffff80021c6410`) devuelve
  `*(ctrl+0x9C0) != 0`; la comparación es `cmp dword ptr [rax+9C0h], 0` en `0xffffff80021c6418`.
  No tiene xrefs directos en el binario; el lector es la propia función `?`.
- `initParamsToDefaults` (`0xffffff80021c7a40`) deja el canal y los flags a cero:
  `mov [rax+9C4h], ecx` en `0xffffff80021c7b46` y `mov [rax+9BCh], rcx` en `0xffffff80021c7b53`.
- `getInfrastructureState` (`0xffffff80021da84a`) reescribe ambos campos desde la lectura del
  controlador: `mov [rax+9C4h], ecx` en `0xffffff80021dad08` y `mov [rax+9BCh], rcx` en
  `0xffffff80021dad11`.

## 2. ¿Apaga algo AWDL la falta de asociación?

### 2.1 Puerta de link-down al entrar en `interfaceStateChange`

`IO80211AWDLPeerManager::interfaceStateChange` (`0xffffff80021d8cc8`), sólo con
`ivars+0x3EE4 != 0` (`cmp` en `0xffffff80021d8ea1`, si no salta a `0xffffff80021d8f0c`), comprueba el
objeto de interfaz en `ctrl+0x560`: carga su vtable y llama a la ranura `+0xB88`
(`0xffffff80021d8eaa`–`0xffffff80021d8eb8`). Si el retorno **es** 1 (`cmp eax, 1` en
`0xffffff80021d8ebe`; el `jnz` de `0xffffff80021d8ec1` salta a `0xffffff80021d8f51` con cualquier otro
valor) registra `"Interface link is down, nothing to do. Source = %s"` (`0xffffff80021d8ed8`), pone
`r12b = 1` y salta al final (`0xffffff80021d8f04`–`0xffffff80021d8f07`) sin tocar estrategia,
servicios ni AWDL. El método de la ranura `+0xB88` y si ese objeto es la primaria o la interfaz de
infraestructura quedan en `?`.

### 2.2 Estrategia `AwdlFollowsInfra` (0)

La rama `ivars+4 == 0` de `interfaceStateChange` (`0xffffff80021d9c01` en adelante):

- Si `*(ctrl+0x9C0) != 0` (`0xffffff80021d9c09`) y `infraAllowedForNonRealtimeCases()` es cierto
  (`0xffffff80021d9c19`), vuelve a la decisión de activación (`0xffffff80021d9bc8`).
- Si no hay asociación o la infraestructura no sirve, con AWDL ya activo
  (`test byte ptr [rcx+3EE0h], 2` en `0xffffff80021d9c26`) registra
  `"In legacy infrastructure mode with no infra connection, waiting briefly before turning off
  AWDL"` (`0xffffff80021d9c5f`) y, si `infraAllowedForNonRealtimeCases()` sigue a 0
  (`0xffffff80021d9c87`), `"… with unsupported infra connection …"` (`0xffffff80021d9c9e`) y salta a
  `0xffffff80021da400` (`"can't enable awdl!"`). Ese camino **no llama a `disableAWDL`**: deja de
  activar y devuelve 0.
- `infraAllowedForNonRealtimeCases` (`0xffffff80021dc540`) exige `*(ctrl+0x9C0) != 0`
  (`0xffffff80021dc54b`); sin asociación devuelve 0.

### 2.3 `monitorAWDLState`: el único camino que desactiva por falta de infraestructura

`IO80211AWDLPeerManager::monitorAWDLState` (`0xffffff80021ed724`) tiene un bloque que primero
intenta volver a `AwdlStateMachine` y, si no puede usar la infraestructura, desactiva AWDL:

- Transición: `setChannelSequenceStrategy(0, 5, false)` en `0xffffff80021ee3ae` (las condiciones de
  entrada están en `AWDL_CHANNEL_STRATEGY.md` §6: `airplaySink != 0` en `0xffffff80021ee25e`,
  `airplaySoloModeEnabled == 0` en `0xffffff80021ee323`, estrategia 1 en `0xffffff80021ee330`,
  sin sockets activos en `0xffffff80021ee352`; la composición exacta del predicado auxiliar queda
  en `?`).
- Con canal de infraestructura y `infraAllowedForNonRealtimeCases()` cierto (`0xffffff80021ee3c7`),
  sin descubrimiento asistido (`0xffffff80021ee3dc`) y sin el bit `0x2000` en `ctrl+0x9C4`
  (`0xffffff80021ee3e9`), copia el canal de la infraestructura al maestro, llama a
  `programMasterChannels` (`0xffffff80021ee433`) y a `setScheduleState(estado, 0x14, true)`
  (`0xffffff80021ee44f`).
- En el resto de los casos construye el motivo: `"infra is on DFS "` (bit 0 de `ivars+0x3ED8`,
  `0xffffff80021eecd0`), `"not associated "` (`*(ctrl+0x9C0) == 0`, `0xffffff80021eece2`),
  `"Infra Not allowed with AWDL "` (`0xffffff80021eecf9`), `"Asst Disc On "`
  (`ivars+0xFA3A`, `0xffffff80021eed16`) y `"on6G "` (bit 0x2000 de `ctrl+0x9C4`,
  `0xffffff80021eed21`), registra `"Disabling AWDL in FollowsInfra mode because  %s"`
  (`0xffffff80021eed35`) y llama a `disableAWDL(this, 0)` en `0xffffff80021ee769`.

Consecuencia: sin asociación, ese camino apaga AWDL con el motivo `"not associated "` sólo si antes
se cumplen las condiciones de la transición (AirPlay sink, servicios AirPlay, sin sockets y enlaces
ociosos). Un `awdl0` sin servicios AirPlay no las cumple; que en la práctica no se alcance queda en
`?` por depender del predicado auxiliar sin nombrar.

### 2.4 `setScheduleState`: aviso y secuencia por defecto, sin apagar

`IO80211AWDLPeerManager::setScheduleState` (`0xffffff80021d27a0`) comprueba
`*(ctrl+0x9C0) == 0` en `0xffffff80021d2ed3`; sin asociación registra
`"WARNING: In FollowsInfra mode without an infrastructure connection for %u ms"`
(`0xffffff80021d2f45`, con la marca de tiempo `ivars+0xA5F8`) y llama a
`initDefaultChannelSequence` (`0xffffff80021d2f65`). No desactiva.

### 2.5 `doEnable` y `enableAWDL`

`doEnable` (`0xffffff80021c5216`) consulta `*(ctrl+0x9C0)` en `0xffffff80021c53a4` sólo para decidir
si ejecuta `monitorInfraTraffic` (`0xffffff80021c53b0`); después llama a `enableInfraMonitoring`
(`0xffffff80021c53b8`) y habilita igual. `enableAWDL` (`0xffffff80021dcf58`) no lee `ctrl+0x9C0`
en absoluto.

### 2.6 Elección, sincronización y ventanas no dependen de la asociación

Barrido de instrucciones con desplazamiento `+9C0h`, `+9BCh` y `+9C4h` en el rango
`0xffffff80021c0000`–`0xffffff8002260000`: no aparecen lecturas en `enableAWDL`
(`0xffffff80021dcf58`), `setAwdlSyncEnabled` (`0xffffff800221b842`), `shouldRunElection`
(`0xffffff80021e3cca`), `runElection` (`0xffffff80021e3e7c`), `monitorTimeline`
(`0xffffff80021e11ca`) ni `sanityCheckAWStart` (`0xffffff80021e21de`). `shouldRunElection` sólo lee
`ivars+10307`, `ivars+16096`, `ivars+10468`, `ivars+10306`, `ivars+16020`, `ivars+16128` y la marca
`ivars+42472`; `runElection`, `monitorTimeline` y `sanityCheckAWStart` no contienen ninguna de las
tres referencias. La elección y el calendario de ventanas siguen, por tanto, sin canal de
infraestructura; `sanityCheckAWStart` sólo compara la estrategia con 1 (`?` el resto de su gate).
Lectores por un alias distinto del registro base no quedarían cubiertos por el barrido `?`.

## 3. Secuencia de canales sin canal de infraestructura

### 3.1 Canales por defecto: maestro 6, maestro secundario 149

`initParamsToDefaults` (`0xffffff80021c7a40`):

| Instrucción | Campo | Valor |
|---|---|---|
| `0xffffff80021c7ada`/`0xffffff80021c7ae4` | `ivars+17040`/`+17044` | `0x00000006` / `0x95` (149) |
| `0xffffff80021c7aeb` | `ivars+17048` | `0x2C` (44) |
| `0xffffff80021c7af5`/`0xffffff80021c7aff` | `ivars+17056`/`+17060` | versión 1, **canal 6** |
| `0xffffff80021c7b06` | `ivars+17064` | flags 8 |
| `0xffffff80021c7b14`–`0xffffff80021c7b27` | `ivars+17068`… | copia del maestro a la ranura secundaria |

`configureValidAWDLChannels` (`0xffffff80021caa40`) valida 6 (`0xffffff80021cae41`), 149
(`0xffffff80021caf62`, store en `0xffffff80021caf85`) y 44 (`0xffffff80021cb074`, store en
`0xffffff80021cb097`), y fija la máscara de canales soportados en `ivars+0x3ED8 = 0x100`
(`0xffffff80021cb0ad`). `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`) pone como maestro el
canal `ivars+17040` (6) y como secundario el 149, o el 44 si el 149 no es válido, con flags 8.

Por tanto **el canal maestro por defecto es 6; 149 es el maestro secundario**. La elección de 149 o
44 depende del código de país; sin 5 GHz válido el secundario queda a 0 y las ranuras de tipo 3 caen
al primario (6).

### 3.2 Secuencia por defecto

`initDefaultChannelSequence` (`0xffffff80021c79c0`): rellena las `n` ranuras con `_zeroChanInfo`,
escribe la ranura 0 con `0x600000001` (`0xffffff80021c7a0e`) y flags 8
(`0xffffff80021c7a15`), y copia esa entrada a la ranura 8 (`0xffffff80021c7a29`,
`0xffffff80021c7a36`). Resultado: **ranuras 0 y 8 en canal 6, las otras 14 en canal 0**.

### 3.3 Tabla por estado

`setChannelSequenceByState` (`0xffffff80021f53aa`) indexa `__ZL20_channelSeqSlotTypes`
(`0xffffff80023aa1f0`) con `estado << 6` (`0xffffff80021f5459`–`0xffffff80021f5460`), lee el tipo de
la ranura (`0xffffff80021f547c`) y despacha por una tabla de saltos de 21 casos con base relativa al
propio `jpt` (`movsxd` + `add` en `0xffffff80021f5491`–`0xffffff80021f5495`), es decir **sin sesgo**:
el número de caso es el tipo de ranura. Filas leídas de la tabla:

| Estado | Ranuras 0..15 |
|---|---|
| 0 Discovery | 3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7 |
| 3 Idle | 3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7 |
| 10 RealTimeData+InfraAssist | 9 ×16 |
| 11 …with Social | 9 9 3 9 9 9 9 9 9 9 4 9 9 9 9 9 |
| 29 Infra Priority | 9 9 3 1 1 1 1 9 2 9 3 9 1 1 1 9 |
| 41 High Thruput | 4 ×16 |

Fuentes por tipo en `setChannelSequenceByState` (`0xffffff80021f53aa`): 1 → `_zeroChanInfo`
(`0xffffff80021f549a`); 2 → primario `ivars+17056`/`+17064`; 3 → secundario `ivars+17068`/`+17072`
si el canal es distinto de 0, si no primario; 4 → canal AWDL actual `ivars+416`/`+424`; 7 → canal de
infraestructura sólo si `addInfraToMinimumChannelSequence` (`0xffffff80021f5ef6`) lo permite, y con
`ctrl+0x9BC == 0` la ranura queda en cero; 9 → infraestructura si
`infraAllowedForNonRealtimeCases` (`0xffffff80021dc540`), y si no: ranura 0 → canal 6 con flags
`0xA`, resto → canal 6 sólo con SDB activo, estado en el conjunto de bits y `ivars+65193` bit 0, en
otro caso cero.

Sin canal de infraestructura y con 149 válido, las secuencias quedan:

- Estado 0: `149 149 149 149 0 0 0 0 6 <canal actual> 149 <canal actual> 0 0 0 0`.
- Estado 3: `149 149 149 0 0 0 0 0 6 <actual> 149 0 0 0 0 0`.
- Estado 10: `6 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0`.
- Estado 11: `6 0 149 0 0 0 0 0 0 0 <actual> 0 0 0 0 0`.
- Estado 41: el canal actual en las 16 ranuras.
- Sin `configure`/estrategia 1: la secuencia por defecto de §3.2 (6 en 0 y 8, cero en el resto).

### 3.4 Petición 129

`setAwdlChannelSequence` (`0xffffff800221cedc`) valida la longitud (`a2[5]` entre 1 y 32,
`0xffffff800221cef2`–`0xffffff800221cefc`), procesa la parte DFS (`0xffffff800221d0fc`), reenvía el
payload al driver con `apple80211RouteInternal(0x802869C8, 129, iface, a2)` (`0xffffff800221d12e`) y,
si el driver acepta, fija la estrategia con `setChannelSequenceStrategy(3, 3, true)`
(`esi=3` en `0xffffff800221d141`, `edx=3` en `0xffffff800221d146`, `ecx=1` en
`0xffffff800221d14b`), copia longitud, codificación, paso y duplicado a `ivars+0xA744`, `+0xA772`,
`+0xA773` y `+0xA760` (`0xffffff800221d155`–`0xffffff800221d18f`), las entradas a `ivars+0xA774`
(`0xffffff800221d1a3`–`0xffffff800221d1cc`) y los 400 B a `ivars+8`
(`0xffffff800221d1d1`–`0xffffff800221d1dd`). Es decir: **con la petición 129 el gestor no calcula
ninguna secuencia; adopta la de userspace, la reenvía y congela la estrategia en
`AwdlUserConfigured`**. La secuencia concreta con la infraestructura ausente la decide quien emite la
129 `?`.

## 4. Consultas sobre la interfaz primaria y errores

### 4.1 Lado kernel

`getInfrastructureState` (`0xffffff80021da84a`) no emite peticiones apple80211: llama a
`IO80211Controller::getInfraChannel` con un `apple80211_channel_data` en pila, y si el canal es
distinto de 0 completa BSSID (`getBSSIDData`), SSID (`getSSIDData`), periodo de baliza y DTIM
(`getBeaconPeriod`, `getDTIMInt`). Con canal 0:

- si el canal cacheado anterior era distinto de 0, registra `"lost infrastructure link (new channel
  %d)"` (`0xffffff80023d942b`, dentro de la misma función), borra BSSID y periodos y devuelve 1
  (estado cambiado);
- si nunca hubo canal de infraestructura, devuelve 0.

Ninguno de los dos casos propaga error ni aborta AWDL. Llamadores:
`interfaceStateChange` (`0xffffff80021d9030`) y `reportDataPathEvents` (`0xffffff80021de938`); el
evento concreto que dispara la segunda llamada queda en `?`.

### 4.2 wifip2pd

El binario `wifip2pd` de 25G241 tiene base de datos propia. Sus peticiones conocidas son
`RawGet` 12 (`CARD_CAPABILITIES`), `RawSet` 94 (`VIRTUAL_IF_CREATE`), `RawSet` 293 y `RawGet` 97; las
tres primeras van sobre el handle de la interfaz AWDL que crea `sub_100047660`
(`0x100047660`), y los 16 eventos de infraestructura se registran sólo para el handle de rol 0 en
`sub_100041B60` (`0x100041b60`). En este barrido no aparece ninguna petición `CHANNEL`, `STATE`,
`BSSID` ni `CURRENT_NETWORK` lanzada sobre la primaria `?`; la información de infraestructura llega
por eventos, no por sondeo. Tampoco se ha encontrado una puerta que aborte la creación de `awdl0`
cuando la primaria no está asociada `?`.

## 5. wifip2pd: cadenas y manejadores de infraestructura

| Cadena | VA | Manejador |
|---|---:|---|
| `Updated Infra Channel` | `0x100448c20` | `sub_100142BE0` (`0x100142be0`) |
| `Association Status` | `0x100448c60` | `sub_100142BE0` |
| `infraDisconnected` | `0x100449550` | `sub_10015CD80`, `sub_10015CE70`, `sub_10015CF30`, `sub_10015D020`, `sub_10015D0F0`, `sub_10015DB10` |
| `Infra association started on channel %s` | `0x1004a24e0` | `sub_1003925D0` |
| `Infra association failed on %s with status code %u (duration: %s)` | `0x1004a2440` | `sub_100392D90` |
| `Infra association complete` | `0x1004a2420` | `sub_1003932D0` |
| `Infra association ready` | `0x1004a2590` | `sub_1003973F0` |
| `Failed to start monitoring for events on infra interface: %@` | `0x1004a2850` | `sub_100395BC0` |
| `Failed to dispatch event %s because the InfrastructureWiFiInterface was deallocated…` | `0x1004a2890` | `sub_10038FF80` |

Son manejadores y reporteros de eventos de infraestructura; ninguna de estas cadenas aparece en la
ruta de creación de `awdl0` (`sub_100047660`, sin referencias a infraestructura). Si alguna de ellas
cambia el comportamiento de AWDL queda en `?`.

## 6. Consecuencias para el prototipo

- Sin asociación y con estrategia 1, el gestor no apaga AWDL: la elección y el calendario de ventanas
  siguen, y la secuencia contiene 149 en las ranuras de tipo 3, 6 en las de tipo 2 y 0 en las de
  tipo 7 (estado 0: cuatro 149 al principio, 6 en la ranura 8, resto 0 salvo el canal actual).
- La secuencia por defecto (ranuras 0 y 8 en canal 6, resto 0) es la que aparece cuando no hay
  estrategia 1 o cuando `setScheduleState` no encuentra infraestructura.
- El canal maestro por defecto es 6; 149 es el maestro secundario. Una radio fija en 149 debe
  anunciar 149 en las ranuras que sirve y 0 en las demás, no 6 en la ranura 0.
- El único apagado por falta de infraestructura está en `monitorAWDLState` y exige el escenario
  AirPlay con servicios, sin sockets y enlaces ociosos; el aviso de `interfaceStateChange` y el de
  `setScheduleState` no apagan.
- La puerta de link-down de `interfaceStateChange` (`ctrl+0x560`, ranura `+0xB88`, con
  `ivars+0x3EE4 != 0`) deja sin efecto cualquier cambio de estado si el objeto consultado devuelve 1;
  conviene saber qué devuelve la capa propia en ese slot `?`.
- `selectChannelsToUseForAWDL` (`0xffffff80021dc2f2`) copia además el maestro secundario (149) al
  canal AWDL actual `ivars+416` cuando es válido, así que las ranuras de tipo 4 también caen en 149.
  Sin petición 129, `RTLAwdInterface::servedMask` sirve las 16 ranuras; con una 129 que sólo ponga
  el 6 (la secuencia por defecto de §3.2), una radio fijada en 149 no sirve ninguna y no transmite.

## 7. Pendientes

- Método de la ranura `+0xB88` del objeto `ctrl+0x560` y si es la primaria o la infraestructura.
- Predicado auxiliar del bloque de `monitorAWDLState` que precede al apagado (composición exacta).
- Si el escenario sin servicios AirPlay alcanza alguna vez ese apagado.
- Peticiones de wifip2pd sobre la primaria distintas de las cuatro verificadas, y si algún error de
  la primaria detiene la creación de `awdl0`.
- Emisor de la petición 129 con la infraestructura ausente y contenido de la secuencia que envía.
