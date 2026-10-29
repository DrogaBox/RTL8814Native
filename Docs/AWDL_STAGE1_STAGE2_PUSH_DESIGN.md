# Etapa 2: cómo empuja el driver la cola de envío de `awdl0`

Diseño estático del empuje de la cola de envío para la etapa 2. Cierra los `?` de
`Docs/AWDL_STAGE1_STACK_TX_ENTRY.md`: el valor de `ifp+0x78` y su efecto en `if_qflush`, el orden entre el
encolado, el hilo de arranque y una traída desde el driver, y lo que hace la familia IO80211 con su
`if_output`. Se corrige además el paso 1 del diseño, que usaba símbolos no enlazables desde un kext, y se
sustituye por los dos métodos virtuales que la familia ya invoca. Todo se ha leído en IDA headless sobre
copias de `boot_awdl.kc.i64`; no se ha enlazado, cargado ni ejecutado nada.

## 1. Respuesta

`ifp+0x78` vale **1** en un `ifnet` recién creado: lo escribe `ifnet_allocate_extended` en
`0xFFFFFF800063FE18` (`mov dword ptr [r13+78h], 1`), justo después de inicializar el `ifclassq`
(`0xFFFFFF800063FE11`, `[ifp+0x338]`). No se ha encontrado otro escritor en el barrido de las funciones del
`ifnet` en `0x5E8000`–`0x5F0000` y `0x63F000`–`0x641000` (incluye `ifnet_attach`, `ifnet_set_flags` y
`ifnet_allocate_extended`). `if_qflush` prueba
`byte [ifp+0x78], 7` (`0xFFFFFF80005D5750`–`0xFFFFFF80005D5755`): con 1, la prueba no es cero y **purga**
(`0xFFFFFF80005D5796`). La purga del §7 de `AWDL_STAGE1_TXPUSH_LLINK.md` ocurre, con una condición añadida: `if_qflush` sólo llama a la
operación de purga si `byte [ifclassq+0x4C] & 2` está activo (`0xFFFFFF80005D5786`–`0xFFFFFF80005D5796`); con el
`ifclassq` habilitado (`IFEF_TXSTART`) se cumple. `?` El valor en ejecución de ese bit se confirma en hardware.

El orden no permite una traída segura desde el driver. `ifnet_enqueue` encola y despierta el hilo de
arranque (`0xFFFFFF80005E6284`) en la misma llamada; el hilo corre en otro hilo del kernel
(`sub_FFFFFF80005EA850`, creado en `0x5E9693`) y ejecuta `_ifp_if_start` → `if_qflush`. No hay ninguna
barrera entre la vuelta de `ifnet_enqueue` y la purga: quien gane es decisión del planificador. `?` El
desensamblado no fija el orden; sólo el hardware lo decide.

La vía que evita el `ifclassq` no son los símbolos del `output`: `ifnet_set_output_handler`
(`0xFFFFFF80005EF540`), `ifnet_reset_output_handler` (`0xFFFFFF80005EF560`),
`dlil_set_output_handler` (`0xFFFFFF80005EF4E0`) y `dlil_reset_output_handler` (`0xFFFFFF80005EF510`)
existen en la tabla del kernel, pero no figuran en ninguna de las nueve listas KPI de
`__LINKINFO,__symbolsets` y ningún kext del collection los importa, de modo que no enlazan desde un kext
(`Docs/AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`). La vía utilizable son dos métodos virtuales que
`IOSkywalkNetworkBSDClient::start` ya invoca sobre el objeto de interfaz:

| Método | Celda | Llamada | Efecto |
|---|---|---|---|
| `initBSDInterfaceParameters` | `0x8E0` | `0xFFFFFF8002AFFADC` | Escribe `params+0x40`; el alta lo copia a `ifp+0x260` (`0x63F890`) y después a `ifp+0x258` (`0x63FC30`) al poner `IFEF_TXSTART` (`0x63FC0A`). |
| `prepareBSDInterface` | `0x8E8` | `0xFFFFFF8002B00688` | Recibe el `ifnet` ya creado y adjunto; permite `cmpxchg` en `ifp+0x258` contra `[ifp+0x5A0]`, donde el alta guarda el valor instalado (`0x63FDA5`). |

La primera es la vía principal: deja el `output` instalado desde el alta y el `ifnet` completo cuando lo ve
el cliente BSD. La segunda es la alternativa: llega después de `_ifnet_attach` (`0x9A42DF`) y antes de
`bpfattach` (`0x2B006CA`), y no depende del layout de `ifnet_init_eparams`. Ninguna de las dos necesita un
símbolo del kernel.

La familia IO80211 no llama a `packetSubmission` por su cuenta: no hay ninguna llamada directa a
`IOSkywalkPacketQueue::packetSubmission(bool)` (`0xFFFFFF8002B2CD68`) en todo el kernel; la función sólo
aparece como dato en tres tablas virtuales (`0x15ACED8` de
`AppleBCMWLANPCIeSkywalkTxSubmissionQueue`, `0x17E13B8` de `AppleBCMWLANSkywalkTxSubmissionQueue` y
`0x2B3E250` de `IOSkywalkTxSubmissionQueue`). El empuje real es el de la maquinaria genérica: `enable`,
`checkForWork` y `nxp_tx_qset_notify`. El `output` que el cliente BSD registra en
`ifnet_init_eparams` (`IOSkywalkNetworkBSDClient::if_output`, `0xFFFFFF8002B002CE`) descarta el mbuf y
devuelve `0x13`; acaba en `ifp+0x258` (`0x63F882`) y `ifnet_allocate_extended` lo sustituye por
`_ifnet_enqueue` al poner `IFEF_TXSTART` (`0x63FC16`–`0x63FC30`), así que para `awdl0` no queda instalado.
Con el campo `+0x40` escrito desde la celda `0x8E0`, esa sustitución toma el valor propio en lugar de
`_ifnet_enqueue` (`0x63FC16`–`0x63FC29`).

## 2. `ifp+0x78`: valor y efecto

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF800063FE0A`–`0xFFFFFF800063FE11` | `ifnet_allocate_extended`: `rax = [ifp+0x338]` (el `ifclassq`); `[rax+0x5C] = 0x800`. |
| `0xFFFFFF800063FE18` | `[ifp+0x78] = 1`. |
| `0xFFFFFF80005D5750`–`0xFFFFFF80005D5755` | `if_qflush`: `test byte [ifp+0x78], 7`; si es cero, vuelve sin purgar. |
| `0xFFFFFF80005D5796` | `if_qflush`: `call qword ptr [rax+30h]` con `rax = [ifq+0x68]`; purga. |
| `0xFFFFFF80005F3352` | `dlil_output`: compara `dword [ifp+0x78]` con `0x0A`; con 1 no toma ese camino. |

El campo vale 1 y sus tres bits bajos son `001`; la comprobación de `if_qflush` usa sólo esos tres bits,
así que la purga se ejecuta. `ifp+0x78` corresponde a `if_refflags` (valor 1 = embrionario en el alta; el alta en `ifnet_attach` lo pasa a
«adjunto»): en ambos estados los tres bits bajos no son cero, de modo que el resultado de la prueba no cambia. `?` El escritor
concreto en `ifnet_attach` no se ha localizado. Además `?` el nombre del campo no lo fija el desensamblado: se escribe una sola vez
en el alta y se compara con `0x0A` en `dlil_output`; lo que importa para esta decisión es el valor 1 y la
máscara `& 7`.

## 3. El orden entre encolado, hilo de arranque y traída

| Paso | Instrucción | Hecho |
|---|---|---|
| 1 | `0xFFFFFF80005E6008` | `sub_5E5F40` llama a `sub_5E6110` (encolado). |
| 2 | `0xFFFFFF80005E6172` | `fq_if_enqueue` deja el paquete en el `ifclassq`. |
| 3 | `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284` | Mismo hilo: `inc [ifp+0x304]` y despierta `&ifp+0x330`. |
| 4 | `0xFFFFFF80005EAA18`, `0x5EAA3A` | Otro hilo: `assert_wait(&ifp+0x330, 0)` y `thread_block_parameter`. |
| 5 | `0xFFFFFF80005EB1F7` | Bucle del hilo: `call [ifp+0x250]`. |
| 6 | `0xFFFFFF80005E3CFC` | `_ifp_if_start` → `if_qflush` → purga. |

No hay ninguna sincronización que garantice que una llamada a `packetSubmission` desde el driver ocurra
antes del paso 6: el paso 3 sólo despierta, y el paso 4 corre cuando el planificador le da CPU. `?` Por
tanto el orden es indeterminado y no sirve como diseño. `packetSubmission` tampoco saca el paquete del
`ifclassq`: lo que hace es llenar el anillo de la cola Skywalk con `kern_netif_queue_tx_dequeue`
(`0x2B0D41F`), y `if_qflush` purga el `ifclassq` por su cuenta.

## 4. Vías para evitar el `ifclassq`

| Vía | Punto de entrada e instrucciones | Efecto |
|---|---|---|
| A. Campo `+0x40` del `ifnet_init_eparams` (principal) | `initBSDInterfaceParameters`, celda `0x8E0`, llamada en `0xFFFFFF8002AFFADC`; el alta lee el campo en `0x63F4A6`, lo copia a `ifp+0x260` en `0x63F890` y a `ifp+0x258` en `0x63FC30` | `_dlil_output_handler` salta a la función propia (`0x5EF50C`); el paquete no entra en el `ifclassq`. |
| B. Escritura en `ifp+0x258` (alternativa) | `prepareBSDInterface`, celda `0x8E8`, llamada en `0xFFFFFF8002B00688`, después de `_ifnet_attach` (`0x9A42DF`) y antes de `bpfattach` (`0x2B006CA`) | El mismo salto, con un `cmpxchg` contra `[ifp+0x5A0]` (`0x63FDA5`). |
| C. `ifnet_set_output_handler` / `dlil_set_output_handler` | `0xFFFFFF80005EF540` / `0xFFFFFF80005EF4E0` | Descartada: no están en las listas KPI y ningún kext los importa; no enlazan (`Docs/AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`). |
| D. Banderas `IFEF` | `0xFFFFFF800063FC0A` pone `0x80` (`IFEF_TXSTART`) | Descartada: no hay colocador de `if_eflags` para el driver y el alta ya la pone al ver `if_start` no nulo. |

El campo `+0x38` del `ifnet_init_eparams` no sirve: `IOSkywalkNetworkBSDClient::start` lo rellena con
`IOSkywalkNetworkBSDClient::if_output` (`0x2AFFA4D`–`0x2AFFA54`) y el alta lo sustituye por `ifp+0x260`
en `0x63FC30` porque `IFEF_TXSTART` está activo. El campo que sobrevive es `+0x40`. `?` El desensamblado
fija el desplazamiento `+0x40`, no el nombre del campo en `ifnet_init_eparams`.

El bloque `0x63FC00`–`0x63FC30` sólo se ejecuta con `ifp+0x250` no nulo. El nexus exige `params+0x48 == 0`
(`0x9A41F4`), de modo que el alta instala `_ifp_if_start` en `ifp+0x250` (`0x63F89D`) y el bloque se ejecuta
siempre; no depende de ningún campo que el driver pueda dejar a cero.

En la vía A, el valor que queda en `ifp+0x5A0` es la función propia, no `_ifnet_enqueue`: `0x63FDA5` copia
`[ifp+0x258]` después de la sustitución. Por eso la vía B no debe comparar contra `_ifnet_enqueue` sino
contra `[ifp+0x5A0]`, que es lo que hace `ifnet_set_output_handler` en `0x5EF544`–`0x5EF54D`. Las dos vías
son alternativas; aplicadas a la vez, la B reescribe el mismo puntero y no cambia nada.

## 5. Lo que hace la familia IO80211

| Hecho | Evidencia |
|---|---|
| `packetSubmission(bool)` no tiene llamadas directas: sólo aparece como dato en tres tablas virtuales | `0x2B2CD68` sin xrefs de código; `0x15ACED8`, `0x17E13B8`, `0x2B3E250` |
| Las llamadas son virtuales, por `vtable+0x1D8` | `enable` `0x2B0CCB2`; `checkForWork` `0x2B2C82A`; `notify` `0x9E24F8` |
| `IOSkywalkNetworkBSDClient::if_output` descarta el mbuf y devuelve `0x13` | `0x2B002CE`: `mbuf_freem` (`0x2B002DA`), `eax = 0x13` (`0x2B002E0`) |
| Ese `if_output` se registra en `ifnet_init_eparams+0x38` | `0x2AFFA4D`–`0x2AFFA54` (`[r13+0x30]`, con `r13 = params+8`) |
| `ifnet_allocate_extended` sustituye ese `ifp+0x258` por `_ifnet_enqueue` cuando hay `IFEF_TXSTART` | `0xFFFFFF800063F882` (`[ifp+0x258] = params+0x38`), `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30` |
| El proveedor instala `nx_netif_na_txsync` como callback | referencia de datos en `0xFFFFFF80009E0E9F`; notifica en `0xFFFFFF80009E24F8` |

Celdas `0x8E0`/`0x8E8` de las tablas virtuales implicadas (lectura en `__ZTV` + `0x8F0` / `+0x8F8`):

| Clase | Celda `0x8E0` | Celda `0x8E8` |
|---|---|---|
| `IO80211AWDLProtocol` (`0xFFFFFF8002456440`) | `IO80211SkywalkInterface::initBSDInterfaceParameters` (`0x22F635C`) | `IO80211VirtualInterface::prepareBSDInterface` (`0x2331ADA`) |
| `AppleBCMWLANProximityInterface` (`0xFFFFFF80017F5B68`) | `0x22F635C` | `0x2331ADA` |
| `AppleBCMWLANSkywalkInterface` (`0xFFFFFF80017E2628`) y `AppleBCMWLANInfraProtocol` (`0xFFFFFF80017DECA8`) | `IO80211InfraInterface::initBSDInterfaceParameters` (`0x235FF5A`) | `IO80211InfraInterface::prepareBSDInterface` (`0x235FF64`) |
| `AppleBCMWLANLowLatencyInterface` (`0xFFFFFF80017E4490`) | `AppleBCMWLANLowLatencyInterface::initBSDInterfaceParameters` (`0x15E034A`) | `0x235FF64` |
| `IOSkywalkEthernetInterface` (`0xFFFFFF8002B3F178`) | `IOSkywalkEthernetInterface::initBSDInterfaceParameters` (`0x2B10BCA`) | `IOSkywalkEthernetInterface::prepareBSDInterface` (`0x2B10E58`) |

Ninguna de ellas escribe `params+0x38` ni `params+0x40`, ni `ifp+0x258`. `IO80211SkywalkInterface` sólo
añade `[params+0xB] |= 0x40` (`0x22F6390`) sobre el cuerpo base; `IO80211InfraInterface` delega en ese
mismo cuerpo y devuelve su resultado (`0x235FF5A`), y su `prepareBSDInterface` (`0x235FF64`) añade `bpfAttach`, propiedades
estáticas y MTU, sin tocar el `output`; `AppleBCMWLANLowLatencyInterface` escribe `params+0x10` y
`params+0x18` (`0x15E034A`) además de llamar al de Infra. La sobreescritura de la celda `0x8E0` es, por
tanto, un patrón que la propia familia usa (Infra y LowLatency), pero instalar un `output` propio por el
campo `+0x40` no lo hace ninguna clase de Apple. `?` Que la maquinaria Skywalk tolere un `ifp+0x258`
distinto de `_ifnet_enqueue` en un `ifnet` de proveedor no se puede fijar desde el kernelcache; se
comprueba en ejecución.

Para un `awdl0` real de un Wi-Fi Apple, el empuje no viene de IO80211 ni de la capa BSD: viene de la cola
Skywalk, que se sirve cuando el proveedor llama a `nx_netif_na_txsync`, y de `enable`/`checkForWork` en la
maquinaria de `IOEventSource`. El kext BCM sólo aporta la subclase de cola y su `packetSubmission`; no
llama a la base por su cuenta. `?` El hilo concreto del kext que dispara el `txsync` no se puede fijar
desde el kernelcache.

## 6. Diseño propuesto para la etapa 2

| Paso | Acción | Por qué |
|---|---|---|
| 1 | Declarar en `RTLAwdInterface` la sobreescritura `uintptr_t initBSDInterfaceParameters(ifnet_init_eparams *, sockaddr_dl **) override` (celda `0x8E0`) | La base ya declara el método (`Source/IO80211/IO80211_25_6_0.h:403`); una sobreescritura no añade celda ni desplaza las siguientes. |
| 2 | En el cuerpo, llamar primero a `IO80211SkywalkInterface::initBSDInterfaceParameters(params, lladdr)` y, sólo si devuelve 0, escribir `*(void **)((char *)params + 0x40) = (void *)&rtlAwdOutput` | El alta copia `+0x40` a `ifp+0x260` (`0x63F890`) y a `ifp+0x258` (`0x63FC30`); si la base falla, no hay `ifnet` que preparar. |
| 3 | En `rtlAwdOutput(__ifnet *, __mbuf *)`, encolar el mbuf en el anillo propio del proveedor y notificar la cola de envío con `packetSubmission(false)` | `packetSubmission` no toma el mbuf: hace que `legacyDequeue` traiga lo que hay en el anillo y lo entregue a la acción. La firma es la de `if_output` (`0x2B002CE`, que devuelve `0x13`). |
| 4 | Poner la acción real de la cola en lugar del no-op de la etapa 1 | Sin acción, `legacyDequeue` no entrega nada a la radio. |
| 5 | Alternativa a 1–2: sobreescribir `prepareBSDInterface(__ifnet *, unsigned int)` (celda `0x8E8`) y hacer `cmpxchg` en `ifp+0x258` contra `[ifp+0x5A0]`, guardando el valor anterior | No depende de `ifnet_init_eparams`; el `ifnet` ya está adjunto (`0x9A42DF`) y todavía no publicado. |
| 6 | En `stop`/`terminate`, devolver el valor guardado con un `cmpxchg` si se usó la vía B | No hay símbolo de reset; `ifnet_detach_final` reinicia `ifp+0x248`, `+0x258` y `+0x260` a `_ifp_if_output` (`0x5E2C2F`–`0x5E2C44`) cuando el `ifnet` se recicla. `?` La vida del `ifnet` respecto a `stop` no se puede fijar desde el kernelcache. |

`RTLAwdInterface` deriva de `IO80211AWDLProtocol` (`Source/AirLayer/AWDLInterface.h:297`), que deriva de
`IO80211VirtualInterface` y ésta de `IO80211SkywalkInterface` (`Source/IO80211/IO80211_25_6_0.h:386`). La
cabecera local ya redeclara `prepareBSDInterface` en `IO80211VirtualInterface`
(`Source/AirLayer/AWDLInterface.h:104`); `initBSDInterfaceParameters` sólo está declarada en la base
(`Source/IO80211/IO80211_25_6_0.h:403`). Declarar la segunda en `RTLAwdInterface` no añade celda:
reemplaza el cuerpo de la celda `0x8E0` y deja las demás en su sitio.

No se instala nada en `initWithController`: en ese punto sólo existe el registro de la interfaz
(`registerEthernetInterface`), y el `ifnet` nace después, dentro del `start` del cliente BSD que dispara
`registerService`. El primer punto en que se puede escribir es `initBSDInterfaceParameters`; el último,
`prepareBSDInterface`.

Con este diseño el primer ND de `awdl0` sí llega a la acción: la pila entra por `rtlAwdOutput`, no hay
`ifclassq` ni purga, y `packetSubmission` trae el paquete hasta `legacyDequeue`. Sin el paso 1, el mismo
ND se purga en `if_qflush` antes de que la acción lo vea (`0x5D5796`). `?` La capacidad real del anillo y
el número de paquetes que el driver debe pedir por vuelta dependen de la radio y se ajustan en hardware.

## 7. Correcciones a `AWDL_STAGE1_STACK_TX_ENTRY.md`

| Punto | Corrección |
|---|---|
| §2, paso 3 | `ifp+0x258` no queda con el `output` del cliente (`IOSkywalkNetworkBSDClient::if_output`): `ifnet_allocate_extended` lo sustituye por `_ifnet_enqueue` porque `params+0x40` es cero y hay `IFEF_TXSTART` (`0x63FC16`–`0x63FC30`). |
| §1 | Se confirma que la pila pasa por `_ifnet_enqueue` y el `ifclassq`; lo que no hace es llamar a `packetSubmission`. |
| §5 | `ifnet_set_output_handler` / `dlil_set_output_handler` no son una vía: no enlazan. La vía es el campo `+0x40` del `ifnet_init_eparams` desde la celda `0x8E0`, con la escritura en `ifp+0x258` desde la celda `0x8E8` como alternativa. |
| Diseño anterior (§6, paso 1) | Instalaba el `output` en `initWithController` con un símbolo inexistente y antes de que existiera el `ifnet`; se sustituye por la sobreescritura de `initBSDInterfaceParameters` y el campo `+0x40`. |

## 8. Verificación de direcciones

Cabezas comprobadas con `ida_funcs.get_func(ea).start_ea == ea`:
`0xFFFFFF800063F3F0`, `0xFFFFFF80005D5730`, `0xFFFFFF80005E5F40`, `0xFFFFFF80005E6110`,
`0xFFFFFF80005EA850`, `0xFFFFFF80005EB0F0`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005EF4E0`,
`0xFFFFFF80005EF500`, `0xFFFFFF80005EF510`, `0xFFFFFF80005EF540`, `0xFFFFFF80005EF560`,
`0xFFFFFF80005E5330`, `0xFFFFFF8002B002CE`, `0xFFFFFF8002B2CD68`, `0xFFFFFF8002B0CC76`,
`0xFFFFFF8002B2C746`, `0xFFFFFF80009E2440`, `0xFFFFFF8002AFF562`, `0xFFFFFF80022F635C`,
`0xFFFFFF8002331ADA`, `0xFFFFFF80022F63B6`, `0xFFFFFF800235FF5A`, `0xFFFFFF800235FF64`,
`0xFFFFFF80015E034A`, `0xFFFFFF8002B10BCA`, `0xFFFFFF8002B10E58`,
`0xFFFFFF80009A4140`, `0xFFFFFF80005E8D20`, `0xFFFFFF8002B00654`.

Citas de instrucción dentro de una cabeza verificada: `0xFFFFFF800063FE0A`–`0xFFFFFF800063FE18`,
`0xFFFFFF800063F882`, `0xFFFFFF800063FC0A`, `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30`,
`0xFFFFFF80005D5750`–`0xFFFFFF80005D5755`, `0xFFFFFF80005D5796`, `0xFFFFFF80005F3352`,
`0xFFFFFF80005E6008`, `0xFFFFFF80005E6172`, `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284`,
`0xFFFFFF80005EAA18`, `0xFFFFFF80005EAA3A`, `0xFFFFFF80005EB1F7`, `0xFFFFFF80005E3CFC`,
`0xFFFFFF80005EF50C`, `0xFFFFFF80005EF544`, `0xFFFFFF80005EF577`, `0xFFFFFF80005EF54D`,
`0xFFFFFF80005EF4ED`, `0xFFFFFF8002AFFA4D`–`0xFFFFFF8002AFFA54`, `0xFFFFFF8002B002DA`,
`0xFFFFFF8002B002E0`, `0xFFFFFF8002B0CCB2`, `0xFFFFFF8002B2C82A`, `0xFFFFFF80009E24F8`,
`0xFFFFFF8002AFFADC`, `0xFFFFFF8002AFFB31`, `0xFFFFFF8002AFFBB5`, `0xFFFFFF80009A41DE`,
`0xFFFFFF80009A41F4`, `0xFFFFFF80009A420D`, `0xFFFFFF80009A42DF`, `0xFFFFFF800063F4A6`,
`0xFFFFFF800063F89D`, `0xFFFFFF800063F890`, `0xFFFFFF800063FDA5`, `0xFFFFFF80005E9668`,
`0xFFFFFF80005E9693`, `0xFFFFFF8002B00688`, `0xFFFFFF8002B006CA`, `0xFFFFFF80005E2C2F`–`0xFFFFFF80005E2C44`,
`0xFFFFFF80022F6390`, `0xFFFFFF80015E034A`.

Datos leídos: tablas virtuales con `packetSubmission` en `0x15ACED8`, `0x17E13B8` y `0x2B3E250`; puntero
a `nx_netif_na_txsync` en `0xFFFFFF80009E0E9F`; celdas `0x8F0`/`0x8F8` de `__ZTV19IO80211AWDLProtocol`
(`0xFFFFFF8002456440`), `__ZTV30AppleBCMWLANProximityInterface` (`0xFFFFFF80017F5B68`),
`__ZTV28AppleBCMWLANSkywalkInterface` (`0xFFFFFF80017E2628`), `__ZTV25AppleBCMWLANInfraProtocol`
(`0xFFFFFF80017DECA8`), `__ZTV31AppleBCMWLANLowLatencyInterface` (`0xFFFFFF80017E4490`) y
`__ZTV26IOSkywalkEthernetInterface` (`0xFFFFFF8002B3F178`).
