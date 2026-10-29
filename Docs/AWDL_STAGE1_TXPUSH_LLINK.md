# Del `ifclassq` al hilo de arranque, el `if_start` del netif y el barrido de `+0x3C`

Cierra los tres puntos abiertos de `Docs/AWDL_STAGE1_QSET_STATE.md` §10 y corrige la conclusión de la
primera versión de este documento: el encolado del `ifclassq` **sí** despierta al hilo de arranque del
`ifnet`, pero ese hilo no notifica al proveedor: ejecuta `if_start`, que para esta interfaz es
`_ifp_if_start`, y `_ifp_if_start` purga la cola de envío. Todo se ha leído en IDA headless sobre copias de
`boot_awdl.kc.i64`; no se ha enlazado, cargado ni ejecutado nada.

## 1. Respuesta

No existe un salto del `ifclassq` a `nxp_queue_tx_push`. La operación de encolado del `ifclassq`
(`+0x18` de la tabla de `pktsched`) es `fq_if_enqueue`, que sólo encola en una cola de flujo del
disciplinador `fq_codel` y no cruza a la tabla del proveedor. `nxp_queue_tx_push` se llama únicamente
desde el camino de **puerto virtual** (`netif_vp_na_txsync`, `0xFFFFFF80009F4D7F`), no desde el envío de la
pila BSD.

El encolado **sí** tiene un disparo: `sub_FFFFFF80005E6110`, después de encolar, comprueba
`if_eflags & IFEF_TXSTART` (`0xFFFFFF80005E61B2`) y, si el hilo de arranque existe
(`[ifp+0x330] != 0`) y no está en marcha, incrementa `[ifp+0x304]` y despierta el canal `&ifp+0x330`
(`0xFFFFFF80005E6284`). Ese hilo es `sub_FFFFFF80005EA850`, creado en `ifnet_attach`
(`kernel_thread_start_priority(sub_5EA850, ifp, -1, &ifp+0x330)` en `0xFFFFFF80005E9693`); su bucle
`sub_FFFFFF80005EB0F0` llama a `ifp->if_start` en `0xFFFFFF80005EB1F7`.

El `if_start` de esta interfaz es `_ifp_if_start` (`0xFFFFFF80005E3CE0`), no una rutina de la familia
Skywalk: el campo `ifp+0x250` se carga en `ifnet_allocate_extended` desde `ifnet_init_eparams+0x48`
(`0xFFFFFF800063F4B5`) y, si ese campo es cero, se pone `_ifp_if_start` (`0xFFFFFF800063F752`,
almacenado en `0xFFFFFF800063F89D`). El `IOSkywalkNetworkBSDClient::start` deja ese campo a cero:
`bzero(params, 0x150)` en `0xFFFFFF8002AFFA1B`, cabecera versión 2/len `0x158` en `0xFFFFFF8002AFFA20`,
y ninguna escritura a `params+0x48`; el constructor de `IOSkywalkEthernetInterface`
(`0xFFFFFF8002B10BCA`) tampoco lo escribe, y el nexus exige que valga cero (`0xFFFFFF80009A41F4`).

`_ifp_if_start` comprueba `IFEF_TXSTART` y hace `if_qflush(ifp, ifp->if_snd)`
(`0xFFFFFF80005E3CF4`–`0xFFFFFF80005E3CFC`); `if_qflush` (`0xFFFFFF80005D5730`) llama a la operación de
purga del `ifclassq` (`[ifq->ops+0x30]`, `0xFFFFFF80005D5796`). Es decir: el disparo del encolado
**purga** el paquete, no lo entrega a la acción de la cola.

El único camino que llega a `legacyDequeue` y a la acción es la traída explícita: `enable`
(`0xFFFFFF80002B0CCB2`), `checkForWork` (`0xFFFFFF80002B2C82A`) y `nxp_tx_qset_notify`
(`[provider+0x90]`, llamado en `0xFFFFFF80009E24F8`). Ninguna de las tres se dispara después del
encolado con el código de la etapa 1.

Los otros dos puntos quedan cerrados como sigue. El `netif_llink` no se crea con el mensaje
`0xE0060105`: ése sólo lo envía `IOSkywalkNetworkInterface::addLogicalLink` (`0xFFFFFF8002B0A7DE`),
cuya única llamada es la ruta DriverKit `IOUserNetworkWLAN::_AddLogicalLink_Impl` (`0xFFFFFF8002B07CE8`).
La única llamada a `kern_nexus_netif_llink_add` (`0xFFFFFF80009E6360`) está en `0xFFFFFF80005FE2D9`,
dentro de la región de prólogo `0xFFFFFF80005FCD20` (`create_netif_provider_and_instance`), y se ejecuta
después de `kern_nexus_controller_alloc_net_provider_instance` (`0xFFFFFF80009A4140`, llamada en
`0xFFFFFF80005FDF42`), es decir, después de `ifnet_allocate_extended` (`0x9A420D`) y de `ifnet_attach`
(`0x9A42DF`). El barrido completo de `+0x3C` sobre el texto del kernel encuentra 1494 accesos; sólo dos
escriben el byte del `netif_llink`: `0xFFFFFF80009F98CE` (valor 1, en `sub_FFFFFF80009F96E0`) y
`0xFFFFFF80009F9C63` (valor 2, en `sub_FFFFFF80009F9C60`).

## 2. El encolado en el `ifclassq`

| Paso | Instrucción | Función | Qué comprueba o hace |
|---|---|---|---|
| Entrada | `0xFFFFFF80005E65EC`–`0xFFFFFF80005E65FE` | `ifnet_enqueue_pkt` `0xFFFFFF80005E65D0` | `if_refflags & 7 == 2` (`0x05E65EC`) y `if_eflags` (`0x05E65F5`); si pasa, llama a `sub_FFFFFF80005E6110`. |
| Desvío | `0xFFFFFF80005E6133` | `sub_FFFFFF80005E6110` | Si `[ifp+0x900] != 0`, salta a `0xFFFFFF80005E62A3` (`_netem_enqueue`, `0xFFFFFF80005E6343`) y no toca el `ifclassq`. |
| Encolado | `0xFFFFFF80005E6172` | `sub_FFFFFF80005E6110` | `call qword ptr [r10+18h]`, con `r10 = [ifclassq+0x68]`; es la operación `enqueue` de la disciplina. |
| Disciplina | `0xFFFFFF800070E9C0` | `ifclassq_enqueue` | Envoltura de la misma llamada (`+0x18` de `[ifclassq+0x68]`). |
| Selección | `0xFFFFFF800070DC80` | `ifclassq_setup` | Elige el tipo por `if_output_sched_model` (`ifp+0x340`); con modelo 0 instala el tipo 7 o el 8, ambos `fq_codel`. |
| Tablas | `0xFFFFFF80011D6CF8` (tipo 7) y `0xFFFFFF80011D6D50` (tipo 8) | datos | `+0x18` = `fq_if_enqueue` `0xFFFFFF8000713020` en las dos. |
| Encolado real | `0xFFFFFF8000713020` | `fq_if_enqueue` | Encola en una cola de flujo; su única llamada indirecta es `[rdi+0xF8]` (`0x71321A`), interna del disciplinador. No toca la tabla del proveedor. |
| Disparo | `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284` | `sub_FFFFFF80005E6110` | Si `if_eflags & 0x80` (`IFEF_TXSTART`), `[ifp+0x300]&1 == 0`, `[ifp+0x309] == 0` y `[ifp+0x330] != 0`: `inc [ifp+0x304]` y despierta `&ifp+0x330`. |

`if_output_sched_model` no lo fija la etapa 1: el `RegistrationInfo` de `Source/AirLayer/AWDLInterface.cpp`
deja `AWDL_REGINFO_FLAGS` (`+0x40`) a cero, y con modelo 0 el tipo es 7 u 8; en ambos casos la operación es
la misma.

El desvío por `+0x900` no se ha visto escrito para el `ifnet` en el alta: las escrituras de `+0x900`
localizadas en la región del nexus (`0xFFFFFF80009FE66F`, `0xFFFFFF80009FE70F`) son de una matriz de
entradas de `0x1200` bytes (`add rdi, 1200h`), no del bloque del `ifnet`. Para `awdl0` se toma el camino
del `ifclassq`. `?` El valor de `ifp+0x900` en tiempo de ejecución no se puede fijar sin hardware.

## 3. Por qué no hay salto a `tx_push`

La tabla de inicialización del proveedor es de 0x60 bytes y se construye en `0xFFFFFF8002AFF15E`–
`0xFFFFFF8002AFF1D9`:

| Desplazamiento | Contenido |
|---|---|
| `+0x00` | 2 (versión) |
| `+0x28` | `nxp_qset_init` `0xFFFFFF8002AFF1E4` |
| `+0x30` | `nxp_qset_fini` `0xFFFFFF8002AFF299` |
| `+0x38` | `nxp_queue_init` |
| `+0x40` | `nxp_queue_fini` |
| `+0x48` | `nxp_tx_qset_notify` `0xFFFFFF8002AFF3E2` |
| `+0x58` | `nxp_queue_tx_push` `0xFFFFFF8002AFF4D1` |

El kernel copia esa tabla en su propio `kern_nexus_provider` con base `+0x48`: `nxp_qset_init` (`+0x28`)
aparece en el proveedor en `+0x70` (`call [r13+70h]` en `0xFFFFFF80009F99BE`) y `nxp_queue_init` (`+0x38`)
en `+0x80` (`0xFFFFFF80009F9A0E` y `0xFFFFFF80009F9A8D`). Por tanto `notify` queda en `+0x90` y `tx_push`
en `+0xA0`, y las dos llamadas aparecen juntas en `nx_netif_na_txsync` (`0xFFFFFF80009E2440`):

| Instrucción | Llamada | Qué es |
|---|---|---|
| `0xFFFFFF80009E24E9`–`0xFFFFFF80009E24F8` | `rax = [rdi+0x90]; call rax` | `nxp_tx_qset_notify` sobre el proveedor (`rdi = [nexus+0x18]`). |
| `0xFFFFFF80009E2545` | `call qword ptr [rdi+0xA0h]` | `nxp_queue_tx_push`, sólo cuando los flags lo piden. |

En todo el texto sólo hay dos `call qword ptr [reg+0A0h]` en código de red: `0xFFFFFF80009E2545` (aquí) y
`0xFFFFFF80009F4D7F` (`netif_vp_na_txsync`, camino de puerto virtual). El envío de la pila BSD no pasa por
`netif_transmit` (`0xFFFFFF80009E5510`): sus llamadores son `nx_netif_host_output` (`0xFFFFFF80009EB0B7`),
`fsw_ring_flush` (`0xFFFFFF80009BDA76`) y `sub_FFFFFF80009F86C0` (`0xFFFFFF80009F8F27`).

## 4. La traída desde el `ifclassq`

| Paso | Instrucción | Función | Condición o efecto |
|---|---|---|---|
| Disparo 1 | `0xFFFFFF8002B0CCB2` | `IOSkywalkTxSubmissionQueue::enable` `0xFFFFFF8002B0CC76` | Tras `IOEventSource::enable`, si `netif_queue` (`+0x78`) no es nulo y `(+0x142)&4 == 0`, llama a `packetSubmission(false)`. |
| Disparo 2 | `0xFFFFFF8002B2C82A` | `checkForWork` `0xFFFFFF8002B2C746` | Sólo si cambió el contador `+0x214`. |
| Disparo 3 | `0xFFFFFF80009E24F8` | `nx_netif_na_txsync` → `nxp_tx_qset_notify` | Por cada cola llama `[vtable+0x1D8](cola, 1)`: el mismo `packetSubmission(bool)`. |
| Relleno | `0xFFFFFF8002B2CED4` | `packetSubmission(bool)` `0xFFFFFF8002B2CD68` | Con `(+0x142)&2` usa `getPacketListForNetworking`; si no, `refillTxRingForNetworking`. Después, `legacyDequeue`. |
| Traída | `0xFFFFFF8002B0D41F` | `refillTxRingForNetworking` `0xFFFFFF8002B0D3E0` | `kern_netif_queue_tx_dequeue`; sólo 0 y 35 son éxito; otro código devuelve `0xE00002B4`. |
| Puerta | `0xFFFFFF80009E614E` | `kern_netif_queue_tx_dequeue` `0xFFFFFF80009E6120` | `byte [llink+0x3C] == 2` devuelve 6 (`ENXIO`); además tope de puerta (`nx_netif_doorbell_max_dequeue`), control de flujo y `ifclassq_dequeue`. |
| Salida | `0xFFFFFF8002B2C860` | `legacyDequeue` | Llama a la acción de la cola; es el punto donde se ejecuta `txNoop` en la etapa 1. |

`refillTxRingForNetworking` no llama a `tx_push`: convierte cada `kern_packet` en `IOSkywalkPacket`, lo
produce en el `IOSkywalkPacketTable` y, al final, llama a `IOSkywalkPacketQueue::packetSubmission(uint,uint)`
(`0xFFFFFF8002B0D53E`), que sólo actualiza contadores (`0xFFFFFF8002B09280`). La acción se ejecuta por el
`legacyDequeue` posterior.

## 5. El alta del `netif_llink`

| Vía | Evidencia | Veredicto |
|---|---|---|
| Mensaje `0xE0060105` | `IOSkywalkNetworkInterface::addLogicalLink` `0xFFFFFF8002B0A7DE` lo envía con el valor 3758489861 (`0xE0060105`); su único llamador es `IOUserNetworkWLAN::_AddLogicalLink_Impl` `0xFFFFFF8002B07CE8` (`jmp` en `0xFFFFFF8002B07CED`). | Sólo DriverKit. No es la vía de un kext normal. |
| Llamada directa | `kern_nexus_netif_llink_add` `0xFFFFFF80009E6360` tiene una sola referencia de código: `0xFFFFFF80005FE2D9`, en la región de prólogo `0xFFFFFF80005FCD20` (no reconocida como función por la IDB; prólogo `push rbp`/`mov rbp,rsp`/`sub rsp,3D8h` en `0x5FCD20`–`0x5FCD2D`). | Es la única creación posible. |

Orden dentro de la región `0x5FCD20`:

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF80005FDF42` | `kern_nexus_controller_alloc_net_provider_instance` `0xFFFFFF80009A4140`. |
| `0xFFFFFF80009A420D` | Dentro de la anterior: `ifnet_allocate_extended` `0xFFFFFF800063F3F0`. |
| `0xFFFFFF80009A42DF` | Dentro de la anterior: `ifnet_attach` `0xFFFFFF80005E8D20`. |
| `0xFFFFFF80005FDFCE` | `nx_find` del nexus recién creado. |
| `0xFFFFFF80005FE2D9` | `kern_nexus_netif_llink_add`, después de construir la configuración de enlace (bucle con paso `0x510`). |

Respecto a `start`: `IOSkywalkNetworkBSDClient::start` `0xFFFFFF8002AFF562` llama a
`kern_nexus_controller_alloc_net_provider_instance` en `0xFFFFFF8002AFFBB5`; el alta del enlace ocurre
después de esa llamada, ya con el `ifnet` creado y adjunto. La instrucción exacta que entra en la región
`0x5FCD20` no la fija el desensamblado: la región no tiene referencias directas y el par
`{0xFFFFFF80005FCD20, 0xFFFFFF80005FE5B0}` de `0xFFFFFF80011D5F70` no tiene xrefs ni punteros en datos.
`nx_netif_compat_init` (`0xFFFFFF80009E64B0`) sólo registra `nx_netif_compat_prov_s`
(`0xFFFFFF80011DF7E0`). Queda con `?` el llamador de `0x5FCD20`; lo que sí queda fijado es que, cuando
corre, el enlace se crea después de `ifnet_allocate_extended` y de `ifnet_attach`.

## 6. El hilo de arranque del `ifnet`

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF80005E9632` | `ifnet_attach`: `cmp qword [ifp+0x330], 0`; si no es cero, panic (`ifp->if_start_thread == THREAD_NULL`). |
| `0xFFFFFF80005E9668` | `cmp qword [ifp+0x250], 0`; si es cero, panic (`ifp->if_start != NULL`). |
| `0xFFFFFF80005E9693`–`0xFFFFFF80005E96A5` | `kernel_thread_start_priority(sub_FFFFFF80005EA850, ifp, -1, &ifp+0x330)`. |
| `0xFFFFFF80005EA8A6` | El hilo se llama `ifnet_start_%s`; si `[ifp+0xC3] & 4` (`IFEF_SKYWALK_NATIVE`), `skywalk_doorbell_%s_tx` (`0xFFFFFF80005EA8CA`). |
| `0xFFFFFF80005EAA0C`–`0xFFFFFF80005EAA18` | `assert_wait(&ifp+0x330, 0)`. |
| `0xFFFFFF80005EAA3A`–`0xFFFFFF80005EAA44` | `thread_block_parameter(sub_FFFFFF80005EB0F0, ifp)`. |
| `0xFFFFFF80005EB1F7`–`0xFFFFFF80005EB1FE` | Bucle del hilo: `rax = [ifp+0x250]; call rax`. |

Quién lo despierta:

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF80005E61B2` | `test byte [ifp+0xC0], 80h` (`IFEF_TXSTART`). |
| `0xFFFFFF80005E61CE` | `test byte [ifp+0x300], 1`; si está puesto, no despierta. |
| `0xFFFFFF80005E61DB` | `inc dword [ifp+0x304]` (contador de peticiones de arranque). |
| `0xFFFFFF80005E61E1` | `cmp byte [ifp+0x309], 0`; si el hilo ya está en el bucle, no despierta. |
| `0xFFFFFF80005E61EE` | `cmp qword [ifp+0x330], 0`; sin hilo, no hay nada que despertar. |
| `0xFFFFFF80005E6222`–`0xFFFFFF80005E6284` | Calcula el índice de la `waitq` a partir de `&ifp+0x330` y llama a `sub_FFFFFF80003CD190` sobre ese canal. |

`ifnet_start` (`0xFFFFFF80005E5060`) repite la misma secuencia (`0xFFFFFF80005E5083`–`0xFFFFFF80005E513F`),
de modo que también despierta el hilo cuando `IFF_UP` cambia.

El canal es el mismo en el que el hilo hace `assert_wait`: `&ifp+0x330`. La rutina `sub_FFFFFF80003CD190`
resuelve el canal por hash en la tabla de `0xFFFFFF800021B400`/`0xFFFFFF800021B3F8` y entra en
`sub_FFFFFF80003CBC90`; no es un `thread_call`.

## 7. El `if_start` de esta interfaz es `_ifp_if_start`

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF800063F4B5` | `ifnet_allocate_extended`: `var_60 = [params+0x48]`. |
| `0xFFFFFF800063F740`–`0xFFFFFF800063F752` | Si `params+0x48 == 0`, `r15 = _ifp_if_start`; si no, se usa el valor del parámetro. |
| `0xFFFFFF800063F89D` | `[ifp+0x250] = r15` (`if_start`). |
| `0xFFFFFF8002AFFA1B` | `IOSkywalkNetworkBSDClient::start`: `bzero(params, 0x150)`. |
| `0xFFFFFF8002AFFA20`–`0xFFFFFF8002AFFA2A` | Cabecera del `ifnet_init_eparams`: versión 2, longitud `0x158`. |
| `0xFFFFFF8002B10BCA` | `IOSkywalkEthernetInterface::initBSDInterfaceParameters`: escribe `+0xD8`–`+0xF8`, `+0x128`, `+0x130`, `+0x2C`, `+0x30` y `+0x140`; no escribe `+0x48`. |
| `0xFFFFFF80022F635C` | `IO80211SkywalkInterface::initBSDInterfaceParameters`: delega en la anterior y sólo pone un flag en `params+0xB`. |
| `0xFFFFFF80009A41F4` | `kern_nexus_controller_alloc_net_provider_instance` exige `params+0x48 == 0`; el nexus no admite un `if_start` propio. |
| `0xFFFFFF80005E3CE9` | `_ifp_if_start`: `test byte [ifp+0xC0], 80h`; sin `IFEF_TXSTART` no hace nada. |
| `0xFFFFFF80005E3CF4`–`0xFFFFFF80005E3CFC` | `rsi = [ifp+0x338]` (`if_snd`) y `jmp _if_qflush`. |
| `0xFFFFFF80005D5796` | `if_qflush`: `call qword ptr [rax+30h]` con `rax = [ifq+0x68]`; es la operación de **purga** del `ifclassq`. |

Consecuencia: el hilo de arranque no notifica al proveedor ni llama a `packetSubmission` ni a
`kern_netif_queue_tx_dequeue`. Si se ejecuta con el paquete ya encolado, lo descarta.

## 8. Barrido de escrituras de `+0x3C`

Se recorrieron todas las instrucciones del texto `0xFFFFFF8000400000`–`0xFFFFFF8002600000` con
desplazamiento `+3Ch`:

| Métrica | Valor |
|---|---|
| Accesos totales a `+3Ch` | 1494 |
| Escrituras a alguna estructura | 563 |
| Escrituras en funciones del `netif_llink` | 2 |
| Escrituras al byte del `netif_llink` | 2 |

Las dos escrituras del enlace:

| Instrucción | Función | Valor |
|---|---|---|
| `0xFFFFFF80009F98CE` | `sub_FFFFFF80009F96E0` (inicialización) | 1 |
| `0xFFFFFF80009F9C63` | `sub_FFFFFF80009F9C60` (destrucción) | 2 |

El resto escribe el `+0x3C` de otras estructuras; por ejemplo `0xFFFFFF800064035B` y `0xFFFFFF8000640368`
(`ifnet_set_flags`, byte propio del `ifnet`), `0xFFFFFF8000739DC9` (`inp_get_soprocinfo`),
`0xFFFFFF800076355B`/`0xFFFFFF80007636A7` (`sub_FFFFFF8000763430`). Ninguna de las funciones del
`netif_llink` escribe otro valor. Queda con `?` cualquier copia por bloque (`memcpy`/`bzero`) que pudiera
alcanzar el byte sin un desplazamiento literal; no se ha encontrado ninguna en el ciclo de vida del enlace.

## 9. Correcciones a los documentos anteriores

| Documento | Afirmación anterior | Corrección |
|---|---|---|
| `AWDL_STAGE1_QSET_STATE.md` §10 | «No se ha localizado la instrucción» del salto a `tx_push`. | No hay tal salto: el `ifclassq` encola en `fq_if_enqueue`; `tx_push` sólo se usa en `netif_vp_na_txsync`. |
| `AWDL_STAGE1_QSET_STATE.md` §9 | «`nxp_qset_init`/`nxp_queue_init` … al registrar el proveedor». | Se ejecutan al crear el `netif_llink` (`nx_netif_llink_ext_init_queues`, `0x9F9960`, llamadas `+0x70` y `+0x80`), no al registrar la tabla del proveedor. |
| `AWDL_STAGE1_IFUP_PATH.md` §4 y riesgo 4 | El primer ND/RS llega a `txNoop` con las colas habilitadas. | Habilitar no basta: hace falta una traída (`packetSubmission`/`checkForWork`/`notify`) **después** del encolado. `enable()` la hace antes, con la cola vacía. |
| Primera versión de este documento, §1/§8 | «El encolado no despierta a nadie; el paquete se queda en el `ifclassq`». | El encolado **sí** despierta el hilo de arranque (`0x5E6284`), pero ese hilo ejecuta `_ifp_if_start`, que purga la cola: el paquete se descarta, no se queda encolado, y tampoco llega a `txNoop`. |
| `AWDL_STAGE1_QSET_STATE.md` §10 | «Vía de alta del `netif_llink` … las dos existen». | El mensaje `0xE0060105` es sólo DriverKit; la única creación es la llamada directa de `0x5FE2D9`. |

## 10. Tabla final: ¿el primer ND de `awdl0` llega a `txNoop`?

| # | Paso | Condición | ¿Fijado por el desensamblado? |
|---|---|---|---|
| 1 | `ifnet_enqueue_pkt` acepta el paquete | `if_refflags & 7 == 2` e `if_eflags` correctos | Sí; los fija `ifnet_attach` y `ifnet_allocate_extended`. |
| 2 | Se toma el camino del `ifclassq` | `[ifp+0x900] == 0` | `?` El bloque del `ifnet` no se ha visto escrito en el alta; el valor en ejecución no se puede fijar sin hardware. |
| 3 | `fq_if_enqueue` lo encola | Disciplina 7/8 y cola de flujo con espacio | Sí. |
| 4 | Se despierta el hilo de arranque | `IFEF_TXSTART`, `[ifp+0x300]&1 == 0`, `[ifp+0x309] == 0`, `[ifp+0x330] != 0` | Sí; el hilo lo crea `ifnet_attach` en `0x5E9693`. |
| 5 | El hilo ejecuta `if_start` | `ifp+0x250` = `_ifp_if_start` | Sí: `params+0x48 == 0` y se aplica el valor por defecto. |
| 6 | `if_start` purga | `IFEF_TXSTART` puesto → `if_qflush(ifp, if_snd)` | Sí. |
| 7 | Alguien trae el paquete a la acción | `packetSubmission`/`checkForWork`/`notify` después del encolado | **No**: con la etapa 1 no se invoca ninguna de las tres después. |

Respuesta: **no, el primer ND no llega a `txNoop`**. El encolado despierta el hilo de arranque, pero ese
hilo ejecuta `_ifp_if_start`, que purga el `ifclassq`; no hay notificación al proveedor ni traída. La
acción sólo se ejecutaría si algo llamase a `packetSubmission`/`checkForWork`/`notify` después del
encolado, y el código de la etapa 1 no lo hace.

La única condición que decide el detalle observable en T3.5 es el paso 7: si el workloop de la familia
emite un `checkForWork` (o el proveedor un `notify`) antes de que el hilo de arranque purgue, el paquete
llega a `txNoop` y el contador sube; si no, se purga sin pasar por la acción. Ese es el punto que la
prueba de hardware tiene que distinguir; el resto de la cadena está fijado por el desensamblado.

## 11. Verificación de direcciones

Cabezas comprobadas con `ida_funcs.get_func(ea).start_ea == ea`:
`0xFFFFFF80005E6110`, `0xFFFFFF80005E65D0`, `0xFFFFFF800070E9C0`, `0xFFFFFF800070E9D0`,
`0xFFFFFF800070DC80`, `0xFFFFFF8000713020`, `0xFFFFFF8002B0CC76`, `0xFFFFFF8002B0CCD6`,
`0xFFFFFF8002B2CD68`, `0xFFFFFF8002B0D3E0`, `0xFFFFFF8002B0D57C`, `0xFFFFFF8002B2CB2A`,
`0xFFFFFF80009E6120`, `0xFFFFFF8002B2C860`, `0xFFFFFF8002B2C746`, `0xFFFFFF80009F49C0`,
`0xFFFFFF8002AFF4D1`, `0xFFFFFF8002AFF1E4`, `0xFFFFFF80009E6360`, `0xFFFFFF80009A4140`,
`0xFFFFFF8002AFF562`, `0xFFFFFF8002B0A7DE`, `0xFFFFFF8002B07CE8`, `0xFFFFFF80009F9590`,
`0xFFFFFF80009F96E0`, `0xFFFFFF80009F9C60`, `0xFFFFFF80009FA2A0`, `0xFFFFFF80005E8D20`,
`0xFFFFFF800063F3F0`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005D5730`, `0xFFFFFF80009E64B0`,
`0xFFFFFF80009E64F0`, `0xFFFFFF80009FAD40`, `0xFFFFFF80009FAF20`, `0xFFFFFF80009E5510`,
`0xFFFFFF80009E2440`, `0xFFFFFF80009E3800`, `0xFFFFFF8002AFF3E2`, `0xFFFFFF8002B09280`,
`0xFFFFFF80005EA850`, `0xFFFFFF80005EB0F0`, `0xFFFFFF80005E5060`, `0xFFFFFF80003CD190`,
`0xFFFFFF8002B10BCA`, `0xFFFFFF80022F635C`, `0xFFFFFF80009FE5B0`, `0xFFFFFF8000718340`.

Región sin cabeza reconocida: `0xFFFFFF80005FCD20` (prólogo `push rbp`/`mov rbp,rsp`/`sub rsp,3D8h`).

Citas de instrucción dentro de una cabeza o región verificada: `0xFFFFFF80005E6133`,
`0xFFFFFF80005E6172`, `0xFFFFFF80005E61B2`, `0xFFFFFF80005E61CE`, `0xFFFFFF80005E61DB`,
`0xFFFFFF80005E61E1`, `0xFFFFFF80005E61EE`, `0xFFFFFF80005E6222`–`0xFFFFFF80005E6284`,
`0xFFFFFF80005E62A3`, `0xFFFFFF80005E6343`, `0xFFFFFF80005E65EC`–`0xFFFFFF80005E65FE`,
`0xFFFFFF80005E9632`, `0xFFFFFF80005E9668`, `0xFFFFFF80005E9693`–`0xFFFFFF80005E96A5`,
`0xFFFFFF80005E3CE9`, `0xFFFFFF80005E3CF4`–`0xFFFFFF80005E3CFC`, `0xFFFFFF80005EA8A6`,
`0xFFFFFF80005EA8CA`, `0xFFFFFF80005EAA0C`–`0xFFFFFF80005EAA18`, `0xFFFFFF80005EAA3A`,
`0xFFFFFF80005EB1F7`–`0xFFFFFF80005EB1FE`, `0xFFFFFF80005E5083`–`0xFFFFFF80005E513F`,
`0xFFFFFF800063F4B5`, `0xFFFFFF800063F740`–`0xFFFFFF800063F752`, `0xFFFFFF800063F89D`,
`0xFFFFFF8002AFFA1B`, `0xFFFFFF8002AFFA20`, `0xFFFFFF8002AFFACB`, `0xFFFFFF8002AFFADC`,
`0xFFFFFF80009A41F4`, `0xFFFFFF80009E24E9`–`0xFFFFFF80009E24F8`, `0xFFFFFF80009E2545`,
`0xFFFFFF8002B0CCB2`, `0xFFFFFF8002B2C82A`, `0xFFFFFF8002B2CED4`, `0xFFFFFF8002B0D41F`,
`0xFFFFFF8002B0D526`, `0xFFFFFF8002B0D53E`, `0xFFFFFF80009E614E`, `0xFFFFFF80009F99BE`,
`0xFFFFFF80009F9A0E`, `0xFFFFFF80009F9A8D`, `0xFFFFFF80009F4D7F`, `0xFFFFFF80005FDF42`,
`0xFFFFFF80005FDFCE`, `0xFFFFFF80005FE2D9`, `0xFFFFFF80009A420D`, `0xFFFFFF80009A42DF`,
`0xFFFFFF8002AFFBB5`, `0xFFFFFF80009F98CE`, `0xFFFFFF80009F9C63`, `0xFFFFFF800064035B`,
`0xFFFFFF8000640368`, `0xFFFFFF8000739DC9`, `0xFFFFFF800076355B`, `0xFFFFFF80007636A7`,
`0xFFFFFF80005D5796`, `0xFFFFFF80009FE66F`, `0xFFFFFF80009FE70F`.

Datos leídos: tablas de disciplina `0xFFFFFF80011D6CF8` y `0xFFFFFF80011D6D50` (`+0x18` = `0x713020`);
par `{0xFFFFFF80005FCD20, 0xFFFFFF80005FE5B0}` en `0xFFFFFF80011D5F70`; descriptor
`nx_netif_compat_prov_s` en `0xFFFFFF80011DF7E0`; puntero a `_ifp_if_start` en `0xFFFFFF800063F752`.
