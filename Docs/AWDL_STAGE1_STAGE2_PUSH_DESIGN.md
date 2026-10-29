# Etapa 2: cómo empuja el driver la cola de envío de `awdl0`

Diseño estático del empuje de la cola de envío para la etapa 2. Cierra los `?` de
`Docs/AWDL_STAGE1_STACK_TX_ENTRY.md`: el valor de `ifp+0x78` y su efecto en `if_qflush`, el orden entre el
encolado, el hilo de arranque y una traída desde el driver, y lo que hace la familia IO80211 con su
`if_output`. Todo se ha leído en IDA headless sobre copias de `boot_awdl.kc.i64`; no se ha enlazado,
cargado ni ejecutado nada.

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

Sí hay una vía que evita el `ifclassq`: instalar un `output` propio en el `ifnet` con los símbolos
exportados `ifnet_set_output_handler` (`0xFFFFFF80005EF540`) o `dlil_set_output_handler`
(`0xFFFFFF80005EF4E0`). El primero sustituye `ifp+0x258` (la función a la que salta
`_dlil_output_handler`, `0x5EF50C`) y el segundo sustituye `ifp+0x248` (`if_output`). Con un `output`
propio no se encola en el `ifclassq`, no se despierta el hilo y no hay purga: el driver entrega
directamente a la cola Skywalk.

La familia IO80211 no llama a `packetSubmission` por su cuenta: no hay ninguna llamada directa a
`IOSkywalkPacketQueue::packetSubmission(bool)` (`0xFFFFFF8002B2CD68`) en todo el kernel; la función sólo
aparece como dato en tres tablas virtuales (`0x15ACED8` de
`AppleBCMWLANPCIeSkywalkTxSubmissionQueue`, `0x17E13B8` de `AppleBCMWLANSkywalkTxSubmissionQueue` y
`0x2B3E250` de `IOSkywalkTxSubmissionQueue`). El empuje real es el de la maquinaria genérica: `enable`,
`checkForWork` y `nxp_tx_qset_notify`. El `output` que el cliente BSD registra en
`ifnet_init_eparams` (`IOSkywalkNetworkBSDClient::if_output`, `0xFFFFFF8002B002CE`) descarta el mbuf y
devuelve `0x13`; acaba en `ifp+0x258` (`0x63F882`) y `ifnet_allocate_extended` lo sustituye por
`_ifnet_enqueue` al poner `IFEF_TXSTART` (`0x63FC16`–`0x63FC30`), así que para `awdl0` no queda instalado.

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

| Vía | Símbolo / instrucción | Efecto |
|---|---|---|
| Sustituir el `output` del `ifnet` | `ifnet_set_output_handler` `0xFFFFFF80005EF540`: `lock cmpxchg [ifp+0x258], rsi` contra el valor guardado en `[ifp+0x5A0]` | `_dlil_output_handler` salta al nuevo `output` (`0x5EF50C`) en vez de a `_ifnet_enqueue`; el paquete no entra en el `ifclassq`. |
| Sustituir `if_output` | `dlil_set_output_handler` `0xFFFFFF80005EF4E0`: `lock cmpxchg [ifp+0x248], rsi` contra `_dlil_output_handler` | La pila entra directamente por la función del driver. |
| Restaurar | `ifnet_reset_output_handler` `0xFFFFFF80005EF560`, `dlil_reset_output_handler` `0xFFFFFF80005EF510` | Vuelven a `_ifnet_enqueue` / `_dlil_output_handler`. |
| Banderas `IFEF` | `0xFFFFFF800063FC0A` pone `0x80` (`IFEF_TXSTART`) | No hay un colocador de `if_eflags` para el driver; no es una vía practicable. |

Ambas sustituciones son `cmpxchg` con el valor por defecto como comparando: si el campo ya cambió, la
llamada devuelve `0x10` y no instala nada. Deben hacerse antes de que la interfaz reciba tráfico.
`ifp+0x5A0` guarda el `output` por defecto (`0xFFFFFF80005EF544`, `0xFFFFFF80005EF577`). `?` Los dos
símbolos existen en el kernelcache, pero que un kext pueda enlazarlos depende de la lista de símbolos
exportados de la versión; hay que comprobarlo en el enlace, no en el desensamblado.

## 5. Lo que hace la familia IO80211

| Hecho | Evidencia |
|---|---|
| `packetSubmission(bool)` no tiene llamadas directas: sólo aparece como dato en tres tablas virtuales | `0x2B2CD68` sin xrefs de código; `0x15ACED8`, `0x17E13B8`, `0x2B3E250` |
| Las llamadas son virtuales, por `vtable+0x1D8` | `enable` `0x2B0CCB2`; `checkForWork` `0x2B2C82A`; `notify` `0x9E24F8` |
| `IOSkywalkNetworkBSDClient::if_output` descarta el mbuf y devuelve `0x13` | `0x2B002CE`: `mbuf_freem` (`0x2B002DA`), `eax = 0x13` (`0x2B002E0`) |
| Ese `if_output` se registra en `ifnet_init_eparams+0x38` | `0x2AFFA4D`–`0x2AFFA54` (`[r13+0x30]`, con `r13 = params+8`) |
| `ifnet_allocate_extended` sustituye ese `ifp+0x258` por `_ifnet_enqueue` cuando hay `IFEF_TXSTART` | `0xFFFFFF800063F882` (`[ifp+0x258] = params+0x38`), `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30` |
| El proveedor instala `nx_netif_na_txsync` como callback | referencia de datos en `0xFFFFFF80009E0E9F`; notifica en `0xFFFFFF80009E24F8` |

Para un `awdl0` real de un Wi-Fi Apple, el empuje no viene de IO80211 ni de la capa BSD: viene de la cola
Skywalk, que se sirve cuando el proveedor llama a `nx_netif_na_txsync`, y de `enable`/`checkForWork` en la
maquinaria de `IOEventSource`. El kext BCM sólo aporta la subclase de cola y su `packetSubmission`; no
llama a la base por su cuenta. `?` El hilo concreto del kext que dispara el `txsync` no se puede fijar
desde el kernelcache.

## 6. Diseño propuesto para la etapa 2

| Paso | Acción | Por qué |
|---|---|---|
| 1 | En `initWithController`, después de `registerEthernetInterface` y antes de publicar, instalar un `output` propio con `ifnet_set_output_handler(ifp, rtlAwdOutput)` | Evita el `ifclassq`, el despertar del hilo y la purga; el `cmpxchg` exige hacerlo con el valor por defecto todavía presente. |
| 2 | En `rtlAwdOutput(ifnet, mbuf)`, encolar el paquete en un anillo propio y llamar a `packetSubmission(false)` de la cola de envío | Es la única entrada que llena el anillo Skywalk; el paquete llega a `legacyDequeue` y a la acción. |
| 3 | Poner la acción real de la cola en lugar del no-op de la etapa 1 | Sin acción, `legacyDequeue` no entrega nada a la radio. |
| 4 | Devolver al `ifnet` con `ifnet_reset_output_handler` en `stop`/`terminate` | Restaura `_ifnet_enqueue` antes de liberar el `ifnet`. |

Con este diseño el primer ND de `awdl0` sí llega a la acción: la pila entra por `rtlAwdOutput`, no hay
`ifclassq` ni purga, y `packetSubmission` trae el paquete hasta `legacyDequeue`. Sin el paso 1, el mismo
ND se purga en `if_qflush` antes de que la acción lo vea (`0x5D5796`). `?` La capacidad real del anillo y
el número de paquetes que el driver debe pedir por vuelta dependen de la radio y se ajustan en hardware.

## 7. Correcciones a `AWDL_STAGE1_STACK_TX_ENTRY.md`

| Punto | Corrección |
|---|---|
| §2, paso 3 | `ifp+0x258` no queda con el `output` del cliente (`IOSkywalkNetworkBSDClient::if_output`): `ifnet_allocate_extended` lo sustituye por `_ifnet_enqueue` porque `params+0x40` es cero y hay `IFEF_TXSTART` (`0x63FC16`–`0x63FC30`). |
| §1 | Se confirma que la pila pasa por `_ifnet_enqueue` y el `ifclassq`; lo que no hace es llamar a `packetSubmission`. |
| §5 | Se añade la vía de `ifnet_set_output_handler` / `dlil_set_output_handler` como forma de saltar el `ifclassq`. |

## 8. Verificación de direcciones

Cabezas comprobadas con `ida_funcs.get_func(ea).start_ea == ea`:
`0xFFFFFF800063F3F0`, `0xFFFFFF80005D5730`, `0xFFFFFF80005E5F40`, `0xFFFFFF80005E6110`,
`0xFFFFFF80005EA850`, `0xFFFFFF80005EB0F0`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005EF4E0`,
`0xFFFFFF80005EF500`, `0xFFFFFF80005EF510`, `0xFFFFFF80005EF540`, `0xFFFFFF80005EF560`,
`0xFFFFFF80005E5330`, `0xFFFFFF8002B002CE`, `0xFFFFFF8002B2CD68`, `0xFFFFFF8002B0CC76`,
`0xFFFFFF8002B2C746`, `0xFFFFFF80009E2440`, `0xFFFFFF8002AFF562`.

Citas de instrucción dentro de una cabeza verificada: `0xFFFFFF800063FE0A`–`0xFFFFFF800063FE18`,
`0xFFFFFF800063F882`, `0xFFFFFF800063FC0A`, `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30`,
`0xFFFFFF80005D5750`–`0xFFFFFF80005D5755`, `0xFFFFFF80005D5796`, `0xFFFFFF80005F3352`,
`0xFFFFFF80005E6008`, `0xFFFFFF80005E6172`, `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284`,
`0xFFFFFF80005EAA18`, `0xFFFFFF80005EAA3A`, `0xFFFFFF80005EB1F7`, `0xFFFFFF80005E3CFC`,
`0xFFFFFF80005EF50C`, `0xFFFFFF80005EF544`, `0xFFFFFF80005EF577`, `0xFFFFFF80005EF54D`,
`0xFFFFFF80005EF4ED`, `0xFFFFFF8002AFFA4D`–`0xFFFFFF8002AFFA54`, `0xFFFFFF8002B002DA`,
`0xFFFFFF8002B002E0`, `0xFFFFFF8002B0CCB2`, `0xFFFFFF8002B2C82A`, `0xFFFFFF80009E24F8`.

Datos leídos: tablas virtuales con `packetSubmission` en `0x15ACED8`, `0x17E13B8` y `0x2B3E250`; puntero
a `nx_netif_na_txsync` en `0xFFFFFF80009E0E9F`.
