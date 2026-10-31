# Secuencia 129: quién la emite, cómo se construyen las 16 ranuras y qué hay en `+0xB88`

Respuestas a tres preguntas sobre macOS 26.7.1 (25G241, x86_64): emisor de la petición 129 en `awdl0`,
construcción de las 16 ranuras y método de la ranura `+0xB88` que consulta `interfaceStateChange`.
Base: copia de la colección de kernel 25G241 y copia de la base de `wifip2pd` de 25G241. Cada
afirmación lleva la VA de la función o instrucción; lo no observado va con `?`.

Convenios: `A = *(manager+0x18)` es el bloque que `IO80211PeerManager::initWithInterface`
(`0xffffff8002246c88`) asigna en `0xffffff8002246cd7` y donde vive el objeto de `+0x560`;
`B = *(manager+0x20)` es el bloque que la puerta consulta en `+0x3EE4`. La documentación previa llama
`ctrl` a `A` e `ivars` a `B`; el campo `+0x560` de `A` es un `IO80211VirtualInterface *`, no el
controlador. Entrada de secuencia: 12 B `{u32 versión/flags, u32 canal, u32 flags}`.

## 1. Quién emite la 129

### 1.1 El kernel, por el camino interno

`IO80211AWDLPeerManager::setChannelSequenceUnconditional` (`0xffffff80021cb69a`) compone el payload de
400 B y lo baja al driver con `IO80211PeerManager::ioctl_internal` (`0xffffff80022524e8`):

- comando `0x81802869C8` en `0xffffff80021cc0b8`, copiado al `ioctl_internal_req` en
  `0xffffff80021cc0c6` y enviado en `0xffffff80021cc0d0`;
- payload: `+0 = 1`, `+4` flags, `+5 = n`, `+6` codificación, `+7` paso, `+8` duplicado, `+9` relleno,
  entradas de 12 B desde `+12`, copiadas de `ivars+42868` (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1);
- supresión por `memcmp` de 0x190 contra `ivars+8` y, si cambió o se fuerza, emisión; después guarda
  la copia en `+8` y publica el mensaje 195.

Ese comando interno es el 129 que ve el driver (`AWDL_CHANNEL_SEQUENCE_CONTROL.md` §5.1: «ioctl interno
129, `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`, 400 B»). Lo disparan `configure`, `enableAWDL`,
`setChannelSequence`, `followPeerChannelSequence` (`0xffffff80021f6e74`), `setRoamChannelSequence`
(`0xffffff80021e5b74`), los eventos de overlay de scan y la reevaluación RTG.

La entrada de userspace es otra: `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE`
(`0xffffff8002288caa`) llama a `IO80211AWDLPeerManager::setAwdlChannelSequence`
(`0xffffff800221cedc`) en `0xffffff8002288cfe`. Esa función valida `n` entre 1 y 32
(`0xffffff800221cef2`–`0xffffff800221cefc`), procesa DFS (`0xffffff800221d0fc`), reenvía el payload
con `apple80211RouteInternal(0x802869C8, 129, …)` (`0xffffff800221d12e`) y fija la estrategia 3
(`0xffffff800221d141`–`0xffffff800221d150`). No tiene llamadores en el kernel: sólo la alcanza un
`RawSet` 129 de userspace.

### 1.2 wifip2pd sólo la lee

En `wifip2pd` 25G241 la 129 aparece dos veces, y las dos son lecturas:

- `sub_1000A97D0` (`0x1000a97d0`) reserva 400 B, los pone a cero (`0x1000a97e4`–`0x1000a9800`), carga
  `edi = 0x81` (`0x1000a9828`) y llama a `sub_10004A440` (`0x1000a983e`). Ese envoltorio carga
  `_Apple80211RawGet_ptr` (`0x10004a456`) y pasa el selector 262 a `sub_10004A4E0`; es `RawGet`, no
  `RawSet`.
- `sub_1000A9900` (`0x1000a9900`) es un getter genérico: `edi = 0x81` en `0x1000a997a`, llamada a
  `sub_1000A65F0`, que reserva y ceroa el búfer, ejecuta la lectura y carga el resultado
  (`0x1000a6698`–`0x1000a66ac`).

El resultado de la lectura se parsea en `sub_1000D5390` (`0x1000d5390`): copia 0x180 B desde `+12`,
lee el número de ranuras en `+5` y recorre entradas de 12 B; `sub_1000D3040` convierte cada canal. Es
consumo para el estado interno y las estadísticas del daemon (`channelSequenceMismatchOn2GCount`,
`channelSequenceMismatchOn5GCount`, `channelSequenceChangedEvent:`), no una escritura.

El envoltorio de escritura existe (`sub_10004A490`, `0x10004a490`, carga `_Apple80211RawSet_ptr` en
`0x10004a4a6`), pero no hay ninguna llamada con 129: las dos únicas apariciones del inmediato `0x81`
en el binario son las lecturas anteriores. Un `RawSet` 129 con el número calculado en registro no
queda cubierto por este barrido `?`; ningún otro daemon se ha analizado `?`.

Conclusión: la 129 que llega a `awdl0` la emite el propio `IO80211AWDLPeerManager` por el camino
interno; `wifip2pd` la lee para informar. No se ha observado un emisor alterno.

## 2. Cómo se construyen las 16 ranuras

### 2.1 Estrategia, estado y tabla

La decisión es la cadena documentada: estrategia en `ivars+4` (`setChannelSequenceStrategy`,
`0xffffff80021ca958`), estado de calendario en `ivars+10468` (`setScheduleState`,
`0xffffff80021d27a0`) y tabla `__ZL20_channelSeqSlotTypes` (`0xffffff80023aa1f0`), indexada con
`estado << 6` (`0xffffff80021f5459`–`0xffffff80021f5460`) y despachada por el `switch` de 21 casos de
`0xffffff80021f5480`. La tabla de saltos usa base relativa al propio `jpt` (`movsxd` + `add` en
`0xffffff80021f5491`–`0xffffff80021f5495`), sin sesgo: el número de caso es el tipo de ranura.

Filas relevantes:

| Estado | Ranuras 0..15 |
|---|---|
| 0 Discovery | `3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7` |
| 3 Idle | `3 3 3 1 1 1 7 7 2 4 3 1 1 1 7 7` |
| 10 RealTimeData + InfraAssist | `9 ×16` |
| 11 RealTimeData + InfraAssist with Social | `9 9 3 9 9 9 9 9 9 9 4 9 9 9 9 9` |
| 29 Infra Priority | `9 9 3 1 1 1 1 9 2 9 3 9 1 1 1 9` |
| 41 High Thruput | `4 ×16` |

### 2.2 Fuentes y flags

`setChannelSequenceByState` (`0xffffff80021f53aa`) escribe 16 entradas de 12 B en `ivars+42868`:

| Tipo | Fuente | Canal / flags |
|---:|---|---|
| 1 | `_zeroChanInfo` (`0xffffff8002488a88`) | canal 0, flags 0 |
| 2 | primario `ivars+17056`/`+17064` | por defecto 6, flags 8 |
| 3 | secundario `ivars+17068`/`+17072` si ≠ 0, si no primario | 149 (o 44), flags 8 |
| 4 | canal AWDL actual `ivars+416`/`+424` | el actual, con sus flags |
| 7 | infraestructura si `addInfraToMinimumChannelSequence` (`0xffffff80021f5ef6`) lo permite | sin asociación, 0 |
| 9 | infraestructura si `infraAllowedForNonRealtimeCases` (`0xffffff80021dc540`); si no, ranura 0 → 6 con flags `0xA`, resto 0 salvo SDB | 6 / `0xA` |

`initParamsToDefaults` (`0xffffff80021c7a40`) deja el maestro en 6 (`0xffffff80021c7ada`,
`0xffffff80021c7aff`), el secundario en 149 (`0xffffff80021c7ae4`) y el alternativo en 44
(`0xffffff80021c7aeb`), con flags 8 (`0xffffff80021c7b06`). `initDefaultChannelSequence`
(`0xffffff80021c79c0`) pone cero en todas las ranuras y después canal 6 con flags 8 en la ranura 0
(`0xffffff80021c7a0e`, `0xffffff80021c7a15`) y copia esa entrada a la 8 (`0xffffff80021c7a29`,
`0xffffff80021c7a36`). El significado de los flags 8 y `0xA` queda en `?`.

### 2.3 Infraestructura sin asociar o link-down

`getInfrastructureState` (`0xffffff80021da84a`) lee `getInfraChannel` y, con canal 0, registra
`"lost infrastructure link (new channel %d)"` (`0xffffff80023d942b`) si había canal previo, o devuelve
0 si nunca lo hubo; no propaga error. En la secuencia:

- el tipo 7 queda en cero porque el canal cacheado es 0;
- el tipo 9 cae al 6 de la ranura 0;
- la secuencia por defecto ya es 6 en las ranuras 0 y 8;
- el tipo 3 usa el secundario 149, que no depende de la infraestructura.

Además, si el `linkState` del vifc vale 1, `interfaceStateChange` sale antes de tocar estado o
secuencia (§3), de modo que la falta de asociación puede no llegar a reevaluar nada.

### 2.4 Secuencias resultantes

Sin asociación, estrategia 1 (`AwdlStateMachine`) y 149 válido:

- Estado 0 Discovery, sin pares:
  `149 149 149 149 0 0 0 0 6 <actual> 149 <actual> 0 0 0 0`.
- Estado 3 Idle, sin pares:
  `149 149 149 0 0 0 0 0 6 <actual> 149 0 0 0 0 0`.
- Estado 10: `6 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0`.
- Estado 11: `6 0 149 0 0 0 0 0 0 0 <actual> 0 0 0 0 0`.
- Estado 41: el canal actual en las 16 ranuras.

Con un par, `setScheduleState` puede tomar el camino de seguir su secuencia: si existe un par cuya
secuencia seguir (`ivars+16248`, o el par de `+16224` con el bit de `+1665`), llama a
`followPeerChannelSequence` (`0xffffff80021f6e74`), que copia las `n` entradas de 12 B del par
(bloque del par, canal en `+924`; detalle en `AWDL_CHANNEL_SEQUENCE_CONTROL.md` §3.6) a
`ivars+42868`, fija el estado 9 (`0xffffff80021f7088`) y emite con
`setChannelSequenceUnconditional(true)`. La secuencia resultante es entonces la del par, no una
construida por la tabla; para un par aparcado en 149 serán 149 en las ranuras que sirve y 0 en el
resto, con la composición exacta dependiente de su TLV 18 `?`. Si no se sigue al par, se aplica la
fila del estado 3.

El emisor no lee ninguna estrategia de userspace: usa su propio ivar `+4` y su estado `+10468`. Las
lecturas de `wifip2pd` (§1.2) sirven para el estado interno y las estadísticas del daemon, no para decidir la
secuencia.

## 3. El objeto de `+0x560` y la ranura `+0xB88`

### 3.1 El objeto es el vifc, no el controlador

La puerta de `interfaceStateChange` (`0xffffff80021d8cc8`), sólo con `B+0x3EE4 != 0`
(`0xffffff80021d8e9d`–`0xffffff80021d8ea8`), hace:

- `mov rax, [rbx+18h]` en `0xffffff80021d8eaa`;
- `mov rdi, [rax+560h]` en `0xffffff80021d8eae`;
- `mov rax, [rdi]` y `call qword ptr [rax+0B88h]` en `0xffffff80021d8eb5`/`0xffffff80021d8eb8`;
- `cmp eax, 1` en `0xffffff80021d8ebe`: si es 1, registra `"Interface link is down, nothing to do.
  Source = %s"` (`0xffffff80021d8ed8`) y sale; cualquier otro valor continúa.

`IO80211PeerManager::initWithInterface` guarda el primer argumento (`IO80211VirtualInterface *`,
`r12` en `0xffffff8002246c9f`) en `[A+0x560]` (`0xffffff8002246e08`), después de asignar `A` en
`0xffffff8002246cd7`. El objeto es, por tanto, el `IO80211VirtualInterface` del peer manager; para
`awdl0`, su subclase `IO80211AWDLProtocol`, y en esta capa `RTLAwdInterface`. No es el controlador:
la vtable del controlador en `+0xB88` es otra (§3.3).

### 3.2 La vtable y el destino real

Los símbolos de vtable de Mach-O llevan las dos palabras de cabecera delante del address point: en
`__ZTV` el primer qword es 0, el segundo 0 y el destructor aparece en `+0x10` (`0xffffff8002472b78`,
`0xffffff800246af00`, `0xffffff800247b4f8`). Por eso el slot `+0xB88` del objeto es el qword
`+0xB98` del símbolo.

| Clase | Símbolo `__ZTV` | Destino en `+0xB88` | Retorno |
|---|---:|---:|---|
| `IO80211VirtualInterface` | `0xffffff8002472b78` | `0xffffff80023333f8` `linkState` | `*(u32 *)(*(this+0x128)+8)` |
| `IO80211AWDLProtocol` | `0xffffff8002456440` | `0xffffff80023333f8` (heredado) | igual |
| `IO80211SkywalkInterface` | `0xffffff800246af00` | `0xffffff80022f982a` `linkState` | 0 |
| `IO80211InfraInterface` | `0xffffff800247b4f8` | `0xffffff80023614a8` `linkState` | `*(u32 *)(*(this+0x128)+0x18)` |
| `IO80211Controller` | `0xffffff8002461148` | `0xffffff80026eccc8` `IOEthernetController::setRealtimeReceiveQueuePacketHandler` | `0xE00002C7` |

`IO80211VirtualInterface::linkState` (`0xffffff80023333f8`) es
`mov rax, [rdi+128h]; mov eax, [rax+8]; ret`; el campo lo escribe `setLinkState`
(`0xffffff8002333427`–`0xffffff8002333471`). Los valores usados por la capa son 1 = down y 2 = up
(`AWDLInterface.cpp`, `AWDL_LINK_DOWN`/`AWDL_LINK_UP`).

### 3.3 Subclases que no lo sobrescriben

`IO80211AWDLProtocol` no define `linkState`: su vtable apunta al mismo `0xffffff80023333f8`, así que
devuelve el campo de `+8`, inicialmente 0 y con el valor que deje `setLinkState`. `RTLAirController`
deriva de `IO80211Controller` y no sobrescribe la ranura: si la puerta se aplicara al controlador,
llamaría a `IOEthernetController::setRealtimeReceiveQueuePacketHandler` (`0xffffff80026eccc8`), que
devuelve `0xE00002C7` y nunca 1; pero el objeto de `+0x560` es el vifc, no el controlador.
`RTLAwdInterface` sí sobrescribe `linkState` (`AWDLInterface.h`, ranura `0xB88`) y devuelve 1 o 2; sin
esa sobrescritura heredaría el cuerpo del kernel y devolvería el campo de `+8`.

Consecuencia práctica: mientras el vifc devuelva 1, `interfaceStateChange` no reevalúa estrategia ni
secuencia; con 2 (o cualquier otro valor) sigue. Por eso la capa pone el enlace en 2 al habilitar.

## 4. Pendientes

- Correspondencia exacta entre el comando interno `0x81802869C8` y el número de petición 129: el campo
  que lo transporta no se ha aislado `?`; la documentación previa lo da por 129.
- Cliente de userspace que, si existe, hace `RawSet` 129 (airportd u otro): no observado `?`.
- Secuencia exacta con un par: depende de su TLV 18 y no se ha capturado `?`.
- Semántica de los flags 8 y `0xA` de las entradas `?`.
- Estado 0 o 3 en un Mac ocioso sin pares según los temporizadores de `setScheduleState` `?`.
