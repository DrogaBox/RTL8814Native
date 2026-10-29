# Estado del conjunto de colas y de `if_refflags`/`if_eflags` antes del primer envío de `awdl0`

Traza, sobre el kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), de los dos estados que la
pila consulta antes de entregar un paquete a la cola de envío: el byte `+0x3C` que compara
`netif_qset_enqueue` y los campos `+120` y `+192` que comprueba `ifnet_enqueue_pkt`. Determina quién los
escribe durante el alta de la interfaz y si el primer ND/RS de `awdl0` llega a `nxp_queue_tx_push` con las
colas que habilita `RTLAwdInterface::enable` (commit `e50d7cf`). No se ha modificado código, parche ni
especificación. Base: IDA headless (`idat -A -S`) sobre copias de `boot_awdl.kc.i64`. Todas las
direcciones son VA; cada una citada como cabeza se comprobó con
`ida_funcs.get_func(ea).start_ea == ea` y las citas de instrucción se marcan como tales. Lo que el
desensamblado no fija va con `?`.

## 1. Respuesta

1. **`qset+0x3C` no es un campo del conjunto de colas, sino `netif_llink::nll_state`.** El objeto que
   `netif_qset_enqueue` mira es el `netif_llink` (`qset->nqs_llink`), no el `netif_qset`. Se escribe **1**
   al crear el enlace (`0xffffff80009F98CE`) y **2** al destruirlo (`0xffffff80009F9C63`). Sólo el valor 2
   hace que `netif_qset_enqueue` libere la cadena y devuelva 6 (`0xffffff80009FA2D3`). Un `awdl0` vivo
   tiene 1: la puerta no se cierra nunca.
2. **`netif_qset_enqueue` no está en el camino del ND/RS.** Su único llamador es `dp_flow_tx_process`
   (`0xffffff80009BA430`, llamada en `0xffffff80009BAF05`), el procesado de TX de una entrada de flujo
   (`_flow_entry_alloc`, `0xffffff80009CAEAE`). Corrige lo que decía `AWDL_STAGE1_IFUP_PATH.md` §4, que
   la situaba entre `ifnet_enqueue_pkt` y `nxp_queue_tx_push`.
3. **`ifnet+0x78` (120) es `if_refflags`.** Vale **1** (`IFRF_EMBRYONIC`) al asignar la interfaz
   (`0xffffff800063FE18`), **0x0A** al adjuntarla (`0xffffff80005E9F3D`) y **4** al separarla
   (`0xffffff80005EB58D`). La comprobación `(& 7) == 2` de `ifnet_enqueue_pkt` es, por tanto,
   "interfaz adjunta y ni embrionaria ni separada" (`IFRF_ATTACHED`).
4. **`ifnet+0xC0` (192) es `if_eflags`.** `ifnet_eflags` (`0xffffff8000640630`) devuelve ese dword. El bit
   que exige `ifnet_enqueue_pkt` es **`IFEF_TXSTART` (0x80)**, puesto por `ifnet_allocate_extended` en
   `0xffffff800063FC0A` cuando el descriptor de alta deja una rutina de arranque de TX no nula.
5. **Los dos campos los pone IOSkywalkFamily, no la etapa 1.** La cadena es
   `IOSkywalkNetworkBSDClient::start` (`0xffffff8002AFF562`) →
   `kern_nexus_controller_alloc_net_provider_instance` (`0xffffff80009A4140`), que llama a
   `ifnet_allocate_extended` (`0xffffff80009A420D`) y a `ifnet_attach` (`0xffffff80009A42DF`). El
   descriptor que arma `start` deja `pre_enqueue` a cero y escribe `0x80000000` en el dword de flags
   (`0xffffff8002AFFA7C`), lo que hace que `ifnet_allocate_extended` instale `ifp_if_start`
   (`0xffffff80005E3CE0`) y active `IFEF_TXSTART`.
6. **Con `e50d7cf`, el primer ND/RS sí llega a `nxp_queue_tx_push` y a la acción no-op.** Pasan
   `IFEF_TXSTART`, `if_refflags == 2`, `IFF_UP` y `IOEventSource::enabled` de la cola. La comprobación del
   `nll_state` no interviene en esta ruta.
7. **La etapa 1 no deja ninguno de esos estados sin poner:** no escribe ninguno de los tres campos y no
   necesita hacerlo; lo único que aporta es la habilitación de las colas, que sí realiza.

## 2. El byte `+0x3C` del conjunto: `nll_state` del `netif_llink`

Los asertos cuya cadena aparece en el binario identifican el objeto y el campo:

| Literal | VA | Contenido |
|---|---|---|
| `sub_FFFFFF80009F9310` | `0xffffff80009F9321`, `0xffffff80009F94E4`, `0xffffff80009F9514` | `qset->nqs_llink->nll_state == NETIF_LLINK_STATE_DESTROYED` y `drvq->nq_qset->nqs_llink->nll_state == ...` |

Es decir, `+0x3C` es `nll_state` del enlace, al que el conjunto apunta en `nqs_llink`. Escritores y
lectores encontrados:

| Función | VA | Hecho |
|---|---|---|
| `sub_FFFFFF80009F96E0` | `0xffffff80009F96E0` | Asigna el `netif_llink`; al final escribe **1** en `+0x3C` (`0xffffff80009F98CE`). La llama `nx_netif_llink_add` (`0xffffff80009F9590`). |
| `sub_FFFFFF80009F9C60` | `0xffffff80009F9C60` | Destruye el enlace; al principio escribe **2** en `+0x3C` (`0xffffff80009F9C63`). La llaman `nx_netif_llink_remove` (`0xffffff80009FA1B0`) y el deshacer de `nx_netif_llink_add` en caso de fallo. |
| `netif_qset_enqueue` | `0xffffff80009FA2A0` | `cmp byte ptr [rdx+3Ch], 2` (`0xffffff80009FA2D3`); si es igual, `pp_free_packet_chain` y retorno 6 (`ENXIO`). |
| `kern_netif_queue_tx_dequeue` | `0xffffff80009E6120` | `if (*((_BYTE *)v8 + 60) == 2) return 6;` con el mismo objeto; es la puerta del lado de drenaje del driver. |

Valores: **1** mientras el enlace está publicado y **2** cuando se destruye. El 0 sólo existe entre la
asignación (memoria a cero) y la escritura del 1, antes de publicar el enlace. `?` No se ha hecho un barrido
exhaustivo de escrituras a `+0x3C` en todo el binario; en las funciones del `netif_llink` sólo aparecen las
dos anteriores.

Consecuencia: en un `awdl0` dado de alta, el enlace está en 1 y `netif_qset_enqueue` no devuelve `ENXIO`
por este motivo. El 2 sólo aparece tras `nx_netif_llink_remove`, que corresponde al desmontaje de la
interfaz, no al primer paquete.

## 3. `netif_qset_enqueue` no está en el camino del ND/RS

La función tiene **una sola referencia**, una llamada directa desde `dp_flow_tx_process`
(`0xffffff80009BAF05`). `dp_flow_tx_process` se registra como procesador de TX de una entrada de flujo en
`_flow_entry_alloc` (`0xffffff80009CAEAE`).

| Paso | VA | Hecho |
|---|---|---|
| `dp_flow_tx_process` | `0xffffff80009BA430` | Procesa la cadena de una entrada de flujo y llama a `netif_qset_enqueue` con el par `{llink, ifnet}`. |
| `netif_qset_enqueue` | `0xffffff80009FA2A0` | Comprueba `nll_state`, contabiliza y **llama a `ifnet_enqueue_pkt`** (`0xffffff80009FA3D7`), es decir, inyecta en la cola de salida del `ifnet`. |

El envío normal de la pila sigue otro camino:

| Paso | VA | Hecho |
|---|---|---|
| `ifnet_enqueue` | `0xffffff80005E5ED0` | Punto de entrada KPI; termina en `sub_FFFFFF80005E5F40`. |
| `sub_FFFFFF80005E5F40` | `0xffffff80005E5F40` | Repite las dos comprobaciones de estado (`if_refflags & 7 == 2`, `IFF_UP`) y pasa a `sub_FFFFFF80005E6110`. |
| `sub_FFFFFF80005E6110` | `0xffffff80005E6110` | Llama a la operación `+0x18` de la tabla apuntada por `ifclassq+0x68` (el `ifclassq` está en `ifp+0x338`, es decir `+824`), con el `ifclassq` como primer argumento. |
| `dlil_ifclassq_setup` | `0xffffff80005E8C80` | Instala el `ifclassq` en el `ifnet`; el enlace guarda ese mismo objeto en su `+0x20` (`0xffffff80009F96E0`). |

Por tanto, el `+0x3C` que la prueba T3.5 podía encontrar delante **no pertenece a esta ruta**. La
corrección afecta a `AWDL_STAGE1_IFUP_PATH.md` §4 y a su riesgo 4: el único estado de este tipo que el
primer ND/RS atraviesa es el `nll_state` del enlace, y vale 1.

## 4. `ifnet+0x78` (120): `if_refflags`

La identificación no es nominal: `ifnet_detach` usa el literal `!(ifp->if_refflags & IFRF_EMBRYONIC)` al
comprobar el bit 0 de ese dword (`0xffffff80005EB581`), y `ifnet_set_interface_state`
(`0xffffff8000640770`) y `ifnet_get_interface_state` (`0xffffff80006407B0`) devuelven 6 (`ENXIO`) si
`(*(u32 *)(ifp+120) & 7) != 2`.

| Momento | VA | Valor escrito | Significado |
|---|---|---|---|
| `ifnet_allocate_extended` | `0xffffff800063FE18` | `1` | `IFRF_EMBRYONIC`: existe pero no está adjunta. |
| `ifnet_attach` | `0xffffff80005E9F33` / `0xffffff80005E9F3D` | `0x10000000A` (qword) | `+120 = 0x0A` (`ATTACHED` + bit 3) y `+124 = 1`. La comprobación posterior `and eax, 7` (`0xffffff80005E9F53`) da 2. |
| `ifnet_detach` | `0xffffff80005EB58D` | `(anterior & ~7) | 4` | `IFRF_DETACHED`: deja de estar adjunta. |

Confirmación independiente: `dlil_attach_flowswitch_nexus` (`0xffffff80005E0060`) exige
`*(u32 *)(ifp+120) == 0x0A` para adjuntar el nexus de flujo; cualquier otro valor lo trata como "no
attached" y sale sin hacer nada.

Lectores en la ruta que interesa:

| Función | VA | Comprobación |
|---|---|---|
| `ifnet_enqueue_pkt` | `0xffffff80005E65F5`–`0xffffff80005E65FE` | `mov eax, [rdi+78h]; and eax, 7; cmp eax, 2`; si no, libera y devuelve 6. |
| `sub_FFFFFF80005E5F40` | `0xffffff80005E5F40` | La misma condición al principio, antes de mirar `IFF_UP`. |
| `ifnet_set_interface_state` / `get` | `0xffffff8000640770` / `0xffffff80006407B0` | Devuelven 6 si no está adjunta. |
| `ifnet_detach` | `0xffffff80005EB430` | Exige el bit 1 y rechaza el 4. |

## 5. `ifnet+0xC0` (192): `if_eflags`

`ifnet_eflags` (`0xffffff8000640630`) devuelve `*(u32 *)(ifp+192)`. El bit que exige
`ifnet_enqueue_pkt` es `IFEF_TXSTART`:

| Escritor / lector | VA | Hecho |
|---|---|---|
| `ifnet_allocate_extended` | `0xffffff800063FC0A` | `lock or dword ptr [r13+0C0h], 80h` cuando la rutina de arranque de TX quedó no nula. El mismo valor se comprueba acto seguido en `0xffffff800063FD1F` con el aserto `ifp->if_eflags & IFEF_TXSTART`. |
| `ifnet_attach` | `0xffffff80005E9284` | `test byte ptr [rbx+0C0h], 80h; jnz`: la rama de alta con salida (la que instala el arranque de TX) sólo se toma si el bit está puesto. |
| `ifnet_enqueue_pkt` | `0xffffff80005E65EC` | `test byte ptr [rdi+0C0h], 80h`; si falta, libera y devuelve 6. |
| `nd6_na_output` / `nd6_ns_output` | `0xffffff8000815984` / `0xffffff80008164BE` | Comprueban el mismo bit antes de entregar el ND/NA. |
| `ifnet_set_eflags` | `0xffffff80006403B0` | Puede poner o quitar el bit; no interviene en el alta de `awdl0`. |

## 6. Quién fija los estados durante el alta

La secuencia, con las llamadas verificadas:

| Paso | VA | Hecho |
|---|---|---|
| `RTLAwdInterface::start` | `AWDLInterface.cpp` | Llama a `rtl_vif_start` y después a `rtl_eth_register` con las tres colas. |
| `IO80211VirtualInterface::start` | `0xffffff80023308AA` | Sólo llama a `IO80211SkywalkInterface::start`; no registra la interfaz ni escribe estos campos. |
| `IOSkywalkEthernetInterface::registerEthernetInterface` | `0xffffff8002B10A84` | Copia el `RegistrationInfo` y registra la interfaz con el array de colas; la variante con enlace lógico es `0xffffff8002B10B46`. `?` El reparto exacto entre las dos variantes de `registerNetworkInterface` (`0xffffff8002B0A0D6` con colas, `0xffffff8002B0A204` con enlace) no se ha decompilado entero. |
| `IOSkywalkNetworkBSDClient::start` | `0xffffff8002AFF562` | Arma el descriptor de `ifnet`, registra el proveedor del nexus y llama a `kern_nexus_controller_alloc_net_provider_instance` (`0xffffff8002AFFBB5`). |
| descriptor de `ifnet` | `0xffffff8002AFFA7C` | Escribe `0x80000000` en el dword de flags del descriptor (`einit+8`); el `bzero` previo de `0x150` bytes deja `pre_enqueue` (`einit+0x48`) y el resto a cero. |
| `kern_nexus_controller_alloc_net_provider_instance` | `0xffffff80009A4140` | Exige `*(qword *)(einit+0x48) == 0` (`0xffffff80009A41F4`) y `(*(u8 *)(einit+8) & 3) == 0` (`0xffffff80009A41FF`). |
| `ifnet_allocate_extended` | llamada en `0xffffff80009A420D`; función `0xffffff800063F3F0` | Con flags = `0x80000000` y `pre_enqueue` nulo toma la rama `v5 < 0`, instala `ifp_if_start` y activa `IFEF_TXSTART` (`0xffffff800063FC0A`). Si no fuera así devolvería 22 y `awdl0` no existiría. |
| `nx_create` | llamada en `0xffffff80009A4251` | Crea el nexus del netif. |
| `nx_netif_default_llink_config` | llamada en `0xffffff80009A4310`; función `0xffffff80009FAF20` | Guarda la configuración del enlace por defecto en `netif+0x6F8`; **no** crea el enlace todavía. |
| `ifnet_attach` | llamada en `0xffffff80009A42DF`; función `0xffffff80005E8D20` | Escribe `if_refflags = 0x0A` (`0xffffff80005E9F3D`). |
| `kern_nexus_netif_llink_add` | `0xffffff80009E6360` | Da de alta el `netif_llink`; la creación (`0xffffff80009F96E0`) deja `nll_state = 1` (`0xffffff80009F98CE`). |

Sobre el último paso: `kern_nexus_netif_llink_add` tiene dos vías de entrada. La documentada es el mensaje
`0xE0060105` de `IOSkywalkNetworkBSDClient::message` (`0xffffff8002B00BA6`, llamada en
`0xffffff8002B00D69`); el `addLogicalLink` que envía ese mensaje (`0xffffff8002B0A7DE`, envío en
`0xffffff8002B0A879`) sólo lo usa la ruta DriverKit `IOUserNetworkWLAN::_AddLogicalLink_Impl`
(`0xffffff8002B07CED`), no la del kext. La otra es una llamada directa desde el código del nexus en
`0xffffff80005FE2D9`, dentro de una región sin símbolo de función. `?` No se ha fijado cuál de las dos
ejecuta el alta en el camino de `awdl0`; en cualquier caso ocurre durante el alta del proveedor o poco
después, antes de que la interfaz se publique, y el primer ND/RS es muy posterior.

## 7. Habilitación de las colas por la etapa 1

`RTLAwdInterface::enable` (`AWDLInterface.cpp:317`) llama a `q->enable()` sobre un `IOEventSource *`, es
decir, por tabla virtual. La celda `+0x160` de `__ZTV26IOSkywalkTxSubmissionQueue` (`0xffffff8002B3E068`)
no es `IOEventSource::enable` (`0xffffff8000ACAA40`), sino
`IOSkywalkTxSubmissionQueue::enable` (`0xffffff8002B0CC76`); la `+0x168` es
`IOSkywalkTxSubmissionQueue::disable` (`0xffffff8002B0CCD6`). Lo mismo ocurre en las otras dos:

| Cola | Vtable | `enable` en `+0x160` | `disable` en `+0x168` |
|---|---|---|---|
| TX submission | `0xffffff8002B3E068` | `0xffffff8002B0CC76` | `0xffffff8002B0CCD6` |
| TX completion | `0xffffff8002B406F0` | `0xffffff8002B12B2C` | `0xffffff8002B12B58` |
| RX completion | `0xffffff8002B3E890` | `0xffffff8002B0EFE0` | `0xffffff8002B0F040` |

`IOSkywalkTxSubmissionQueue::enable` toma el gate, y sólo si la cola no está ya habilitada llama a
`IOEventSource::enable` (por `off_FFFFFF8002B3CDD0`, que vale `0xffffff8000ACAA40`) y después ejecuta una
acción de la cola: si hay `netif_queue` en `+0x78` y el bit 2 de `+0x322` está a cero, una de las celdas
altas de la vtable; si no hay `netif_queue`, otra; si hay `netif_queue` y el bit está puesto, ninguna. `?`
El nombre exacto de esas dos celdas no se ha fijado; lo que importa para esta traza es que `+0x28` queda a
1. La operación es idempotente. La comprobación de la cola habilitada en el envío es la primera
instrucción útil de
`IOSkywalkTxSubmissionQueue::packetSubmission` (`0xffffff8002B2CB2A`):
`test byte ptr [rdi+28h], 1` (`0xffffff8002B2CB3F`), con salto al retorno `0xE00002D8` si está a cero.

## 8. Puerta por puerta para el primer ND/RS

| # | Puerta | VA | Comprobación | Valor en `awdl0` | Resultado |
|---|---|---|---|---|---|
| 1 | Salida ND6 | `0xffffff80008164BE`, `0xffffff8000815984` | `if_eflags & IFEF_TXSTART` | `0x80` puesto en el alta | Pasa |
| 2 | `ifnet_enqueue` / `sub_5E5F40` | `0xffffff80005E5ED0` / `0xffffff80005E5F40` | `if_refflags & 7 == 2` | `0x0A` | Pasa |
| 3 | `sub_5E5F40` / `ifnet_enqueue_pkt` | `0xffffff80005E6600` | `IFF_UP` (`+0xBE` bit 0) | Puesto por `ifconfig awdl0 up` | Pasa |
| 4 | `ifclassq` | `0xffffff80005E6110` | Enlazado al `netif_llink` | Configurado en el alta | Pasa; `?` el salto exacto del `ifclassq` al `tx_push` del proveedor no se ha recorrido |
| 5 | `nxp_queue_tx_push` | `0xffffff8002AFF4D1` | Operación `tx_push` del proveedor, registrada en `+0x58` de su tabla (`0xffffff8002AFF1C5`) | Registrada | Se ejecuta |
| 6 | `packetSubmission` | `0xffffff8002B2CB3F` | `IOEventSource::enabled` (`+0x28`) | Habilitado por `enable()` de la etapa 1 | Pasa |
| 7 | `legacyDequeue` → acción | `0xffffff8002B2C860` → `+0x20` de la cola | Acción no nula y `+0xCC` | `txNoop` (`AWDLInterface.cpp:255`) | Se ejecuta |
| — | `netif_qset_enqueue` | `0xffffff80009FA2A0` | `nll_state == 2` | No está en esta ruta; el enlace está en 1 | No aplica |

Conclusión original (corregida): se daba por hecho que el primer ND/RS llegaba a la acción no-op con las
colas habilitadas. Ese salto del `ifclassq` a `tx_push` no existe: el encolado despierta el hilo de
arranque del `ifnet`, cuyo `if_start` es `_ifp_if_start` y purga el `ifclassq`; la acción sólo se ejecuta si
la familia golpea la cola después del encolado. El análisis completo, con las filas 4, 5 y 7 de la tabla
anterior invalidadas, está en `AWDL_STAGE1_TXPUSH_LLINK.md` §1 y §10. Lo fijado aquí sobre `if_refflags`,
`if_eflags` y `nll_state` sigue vigente.

## 9. Lo que la etapa 1 deja sin poner

| Estado | ¿Lo pone la etapa 1? | ¿Hace falta? |
|---|---|---|
| `if_refflags` (`+120`) | No | No: lo pone `ifnet_attach` en el alta. |
| `if_eflags` / `IFEF_TXSTART` (`+192`) | No | No: lo pone `ifnet_allocate_extended` en el alta. |
| `nll_state` (`+0x3C` del enlace) | No | No: lo pone la creación del enlace. |
| `netif_queue` de cada cola (`+0x78`) y `+0xCC` | No | No: los pone la inicialización del conjunto (`nxp_qset_init`, `nxp_queue_init`, `IOSkywalkPacketQueue::initialize`) al registrar el proveedor. |
| `IOEventSource::enabled` (`+0x28`) de las tres colas | **Sí** | **Sí**: sin él `packetSubmission` devuelve `0xE00002D8`. |

La sobreescritura de `enable` sigue dejando fuera `IO80211VirtualInterface::setInterfaceEnable` y, con él,
`state+113` y el `IO80211MacAddressAgent`. Eso no afecta a ninguna de las puertas de esta ruta; `?` si
algún consumidor de `wifip2pd` lo exige queda como estaba en `AWDL_STAGE1_IFUP_PATH.md` §2.1.

## 10. Puntos abiertos

| Punto | Estado |
|---|---|
| Llamada del `ifclassq` a la operación `tx_push` del proveedor | Cerrado por `AWDL_STAGE1_TXPUSH_LLINK.md` §1-§4: no existe ese salto; `tx_push` sólo se usa en el camino de puerto virtual. |
| Vía de alta del `netif_llink` en el camino del kext (mensaje `0xE0060105` o llamada del nexus en `0xffffff80005FE2D9`) | `?` Las dos existen; la del mensaje sólo se ha visto salir de la ruta DriverKit. |
| Otras escrituras de `+0x3C` fuera de las funciones del `netif_llink` | `?` No se ha hecho barrido exhaustivo; en el ciclo de vida del enlace sólo aparecen 1 y 2. |
| Significado del bit 8 de `if_refflags` (0x0A en el alta) | `?` No se ha identificado el nombre; la comprobación de `ifnet_enqueue_pkt` sólo mira los tres bits bajos. |
| Momento exacto del alta del enlace respecto a `start` | `?` Se sabe que es dentro del alta del proveedor; no se ha fijado la instrucción que la dispara. |

## 11. Verificación de direcciones

Cabezas comprobadas con `ida_funcs.get_func(ea).start_ea == ea`:
`0xffffff80009FA2A0`, `0xffffff80009F9590`, `0xffffff80009F96E0`, `0xffffff80009F9C60`,
`0xffffff80009FA1B0`, `0xffffff80009E6360`, `0xffffff80009E6120`, `0xffffff80009BA430`,
`0xffffff80009A4140`, `0xffffff80009FAF20`, `0xffffff80009CA840`, `0xffffff80005E65D0`, `0xffffff80005E5ED0`,
`0xffffff80005E5F40`, `0xffffff80005E6110`, `0xffffff80005E8D20`, `0xffffff80005EB430`,
`0xffffff8000640630`, `0xffffff8000640770`, `0xffffff80006407B0`, `0xffffff800063F3F0`,
`0xffffff80006403B0`, `0xffffff80005E3CE0`, `0xffffff80005E0060`, `0xffffff8002AFF562`,
`0xffffff8002B00BA6`, `0xffffff8002B10A84`, `0xffffff8002B0A204`, `0xffffff8002B0A7DE`,
`0xffffff8002B0CC76`, `0xffffff8002B12B2C`, `0xffffff8002B0EFE0`, `0xffffff8002B2CB2A`,
`0xffffff8002B2C860`, `0xffffff80023308AA`, `0xffffff8000815DD0`, `0xffffff8000815380`.

Citas de instrucción dentro de una cabeza verificada: `0xffffff80009FA2D3` (en
`0xffffff80009FA2A0`), `0xffffff80009F98CE` (en `0xffffff80009F96E0`), `0xffffff80009F9C63` (en
`0xffffff80009F9C60`), `0xffffff80009BAF05` (en `0xffffff80009BA430`), `0xffffff80005E65EC`,
`0xffffff80005E65F5`–`0xffffff80005E65FE`, `0xffffff80005E6600` (en `0xffffff80005E65D0`),
`0xffffff800063FE18`, `0xffffff800063FC0A`, `0xffffff800063FD1F` (en `0xffffff800063F3F0`),
`0xffffff80005E9F33`–`0xffffff80005E9F53`, `0xffffff80005E9284` (en `0xffffff80005E8D20`),
`0xffffff80005EB581`, `0xffffff80005EB58D` (en `0xffffff80005EB430`), `0xffffff80009A41F4`,
`0xffffff80009A41FF`, `0xffffff80009A420D`, `0xffffff80009A4251`, `0xffffff80009A42DF`,
`0xffffff80009A4310` (en `0xffffff80009A4140`), `0xffffff8002AFFA7C`, `0xffffff8002AFFAAF`,
`0xffffff8002AFFBB5` (en `0xffffff8002AFF562`), `0xffffff8002B00D69` (en `0xffffff8002B00BA6`),
`0xffffff8002B0A879` (en `0xffffff8002B0A7DE`), `0xffffff8002B2CB3F` (en `0xffffff8002B2CB2A`),
`0xffffff80009CAEAE` (en `0xffffff80009CA840`), `0xffffff80008164BE` (en `0xffffff8000815DD0`),
`0xffffff8000815984` (en `0xffffff8000815380`).

Vtables leídas como datos: `0xffffff8002B3E068`, `0xffffff8002B406F0`, `0xffffff8002B3E890`; el puntero
global `0xffffff8002B3CDD0` vale `0xffffff8000ACAA40` (`IOEventSource::enable`).
