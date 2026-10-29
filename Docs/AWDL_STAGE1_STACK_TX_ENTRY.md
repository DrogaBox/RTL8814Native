# Entrada TX de la pila en `awdl0`: de `dlil_output` al `ifclassq` y a las colas Skywalk

Cierra los dos `?` de `Docs/AWDL_STAGE1_TXPUSH_LLINK.md` §10: qué función recibe el mbuf que la pila BSD
envía por `awdl0`, si el `ifclassq` está en ese camino y qué llega a `IOSkywalkTxSubmissionQueue` y a
`kern_netif_queue_tx_dequeue`. Todo se ha leído en IDA headless sobre copias de `boot_awdl.kc.i64`; no se
ha enlazado, cargado ni ejecutado nada.

## 1. Respuesta

La pila no llama a `ifnet_enqueue_pkt`. El mbuf entra por `ifp->if_output`, que
`ifnet_allocate_extended` fija en `_dlil_output_handler` (`0xFFFFFF800063FC4F`–`0xFFFFFF800063FC56`), y
`_dlil_output_handler` (`0xFFFFFF80005EF500`) salta al puntero de `ifp+0x258` (`0x5EF504`, `jmp rax` en `0x5EF50C`).
Ese puntero es `_ifnet_enqueue` (`0xFFFFFF80005E5ED0`), instalado por el mismo alta
(`0xFFFFFF800063FC22`–`0xFFFFFF800063FC30`).

`ifnet_enqueue` construye una lista de paquetes y llama a `sub_FFFFFF80005E5F40` (`0xFFFFFF80005E5F16`),
que a su vez llama a `sub_FFFFFF80005E6110` (`0xFFFFFF80005E6008`). Es la misma función del `ifclassq`
que ya estaba documentada: desvío por `[ifp+0x900]` (`0x5E6133`), encolado en `fq_if_enqueue`
(`0x5E6172`) y despertar del hilo de arranque (`0x5E61B2`–`0x5E6284`).

Por tanto **sí**, el `ifclassq` está en el camino de salida de la pila para este `ifnet`, y el desvío por
`[ifp+0x900]` y la purga de `_ifp_if_start` son relevantes. `[ifp+0x900]` es la cabeza de una cola `netem`
**empotrada** en el `ifnet`: `sub_5E6110` pasa `&ifp+0x900` a `_netem_enqueue` (`0xFFFFFF80005E62A3`,
`0xFFFFFF8000718340`), y con la cola vacía el primer paquete toma el camino del `ifclassq`.

Nada de ese camino llega a las colas Skywalk. `IOSkywalkTxSubmissionQueue::packetSubmission`
(`0xFFFFFF8002B2CD68`) sólo se invoca desde `enable` (`0xFFFFFF8002B0CCB2`), `checkForWork`
(`0xFFFFFF8002B2C82A`) y `nxp_tx_qset_notify` (`0xFFFFFF80009E24F8`). `nxp_tx_qset_notify` se llama
desde `nx_netif_na_txsync` (`0xFFFFFF80009E2440`), y esa función sólo aparece como **dato** en
`0xFFFFFF80009E0E9F` (puntero instalado en una tabla de callbacks); es la vía del proveedor, no la de la
pila.

## 2. La entrada TX de la pila

| Paso | Instrucción | Función | Hecho |
|---|---|---|---|
| 1 | `0xFFFFFF800063FC4F`–`0xFFFFFF800063FC56` | `ifnet_allocate_extended` `0xFFFFFF800063F3F0` | `[ifp+0x248] = _dlil_output_handler` (`if_output`). |
| 2 | `0xFFFFFF80005EF504`, `0xFFFFFF80005EF50C` | `_dlil_output_handler` `0xFFFFFF80005EF500` | `rax = [ifp+0x258]` y `jmp rax` con el mbuf. |
| 3 | `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30` | `ifnet_allocate_extended` | `[ifp+0x260] = [ifp+0x258] = _ifnet_enqueue` (si `+0x260` era cero). |
| 4 | `0xFFFFFF80005E5ED8`–`0xFFFFFF80005E5F16` | `_ifnet_enqueue` `0xFFFFFF80005E5ED0` | Recorre la cadena `[m+8]` y suma `[m+0x60]`; llama a `sub_FFFFFF80005E5F40` con la lista. |
| 5 | `0xFFFFFF80005E6008` | `sub_FFFFFF80005E5F40` `0xFFFFFF80005E5F40` | Llama a `sub_FFFFFF80005E6110` por paquete. |
| 6 | `0xFFFFFF80005E6133` | `sub_FFFFFF80005E6110` | `[ifp+0x900] != 0` → `_netem_enqueue` (`0x5E6343`); si no, sigue al `ifclassq`. |
| 7 | `0xFFFFFF80005E6172` | `sub_FFFFFF80005E6110` | `call [ifclassq+0x68]+0x18` = `fq_if_enqueue` (`0x713020`). |
| 8 | `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284` | `sub_FFFFFF80005E6110` | Despierta el hilo de arranque (`&ifp+0x330`). |

`ifnet_enqueue_pkt` (`0xFFFFFF80005E65D0`) **no** es la entrada de la pila: sus llamadores son
`nx_netif_host_output` (`0xFFFFFF80009EABD5`, `0xFFFFFF80009EAC03`), `netif_transmit`
(`0xFFFFFF80009EB0B7`), `sub_FFFFFF80009F86C0` (`0xFFFFFF80009F8E8B`), `netif_qset_enqueue`
(`0xFFFFFF80009FA3D7`), `sub_FFFFFF80009BB780` (`0x9BB80D`), `sub_FFFFFF80009F4F60` (`0x9F506E`) y la
región `0x606849`/`0x606899`. Es la vía de **inyección del proveedor** (y de `tx_push`), no la de
`dlil_output`.

## 3. Qué llega a las colas Skywalk

| Entrada a `packetSubmission` | Instrucción | Origen |
|---|---|---|
| `enable` | `0xFFFFFF8002B0CCB2` | `IOSkywalkTxSubmissionQueue::enable`; en la etapa 1 corre al habilitar las colas, con la cola vacía. |
| `checkForWork` | `0xFFFFFF8002B2C82A` | `IOEventSource`; sólo con el contador `+0x214` cambiado. |
| `notify` | `0xFFFFFF80009E24F8` | `nx_netif_na_txsync` → `[provider+0x90]` (`nxp_tx_qset_notify`); la invoca el proveedor. |

Ninguna de las tres se ejecuta desde `dlil_output`, `ifnet_enqueue`, `sub_5E5F40` ni `sub_5E6110`. La
secuencia que sí llega a la acción es la traída del §4 de `AWDL_STAGE1_TXPUSH_LLINK.md`:
`packetSubmission` (`0x2B2CD68`) → `refillTxRingForNetworking` (`0x2B0D3E0`) → `kern_netif_queue_tx_dequeue`
(`0x2B0D41F` → `0x9E6120`) → `legacyDequeue` (`0x2B2C860`).

## 4. El hilo de arranque en este camino

El paso 8 despierta el hilo `sub_FFFFFF80005EA850` (creado en `ifnet_attach`, `0x5E9693`), que llama a
`ifp->if_start` (`0x5EB1F7`). Para esta interfaz `if_start` es `_ifp_if_start`
(`AWDL_STAGE1_TXPUSH_LLINK.md` §7): comprueba `IFEF_TXSTART` (`0x5E3CE9`), toma `if_snd`
(`0x5E3CF4`) y salta a `if_qflush` (`0x5E3CFC`).

`if_qflush` (`0x5D5730`) tiene una salida temprana: si `[ifp+0x78] & 7 == 0` no purga
(`0x5D5750`–`0x5D5755`); si no, llama a la operación de purga del `ifclassq` (`[ifq->ops+0x30]`,
`0x5D5796`). Con `awdl0` encolando por `fq_codel`, la purga recorre las colas de flujo y descarta el ND.

## 5. ¿Llega el primer ND a `txNoop`?

| # | Paso | Evidencia |
|---|---|---|
| 1 | La pila entrega el mbuf a `_dlil_output_handler` | `ifp+0x248`, `0x63FC56`; `0x5EF504`. |
| 2 | `_ifnet_enqueue` arma la lista | `0x5E5ED0`, llamada en `0x5E5F16`. |
| 3 | `sub_5E5F40` → `sub_5E6110` | `0x5E6008`. |
| 4 | `[ifp+0x900] == 0` → `fq_if_enqueue` | `0x5E6133`, `0x5E6172`; `+0x900` es una cola empotrada que arranca vacía. `?` El valor en ejecución no se puede fijar sin hardware. |
| 5 | Se enciende el hilo de arranque | `0x5E61B2`–`0x5E6284`; hilo creado en `0x5E9693`. |
| 6 | El hilo llama a `_ifp_if_start` | `0x5EB1F7`; `0x5E3CE0`. |
| 7 | `_ifp_if_start` purga | `0x5E3CFC`; `0x5D5796` (si `[ifp+0x78]&7 != 0`). |
| 8 | Nadie llama a `packetSubmission` después del encolado | Sólo `enable`, `checkForWork` y `notify`, y ninguna corre en este camino. |

Respuesta: **no**. El ND entra en el `ifclassq`, el encolado despierta el hilo de arranque y el hilo ejecuta
`_ifp_if_start`, que purga la cola. La acción de la cola (`txNoop`) no se ejecuta; la instrucción que
podría dispararla es `kern_netif_queue_tx_dequeue` en `refillTxRingForNetworking` (`0x2B0D41F`), y sólo se
alcanza desde `packetSubmission`, que la etapa 1 no vuelve a llamar.

Condición que decide el detalle observable: si `[ifp+0x78] & 7 == 0`, `if_qflush` no purga y el paquete
queda en el `ifclassq`; si no, se descarta. En los dos casos la acción no se ejecuta. `?` El valor de
`ifp+0x78` en `awdl0` no se puede fijar sin hardware.

## 6. Qué tiene que hacer el driver en la etapa 2

| Necesidad | Qué llamar | Evidencia |
|---|---|---|
| Traer paquetes a la acción | `IOSkywalkTxSubmissionQueue::packetSubmission(bool)` sobre cada cola de envío | `0x2B2CD68`; `legacyDequeue` en `0x2B2C860`; acción con `call r12` en `0x2B2C8DE` |
| Alternativa del proveedor | `nxp_tx_qset_notify` (`[provider+0x90]`) vía `nx_netif_na_txsync` | `0x9E2440`, `0x9E24F8` |
| Evitar la purga | drenar antes de que el hilo de arranque ejecute `_ifp_if_start`, o no encolar en el `ifclassq` | `0x5E6284`, `0x5EB1F7`, `0x5E3CFC` |

La etapa 2 no puede cambiar `if_start`: el campo `ifp+0x250` lo fija `ifnet_allocate_extended` y el nexus
exige `einit+0x48 == 0` (`0x9A41F4`), de modo que siempre queda `_ifp_if_start`. La vía practicable es que
el driver, en su propio workloop y después de cada encolado, llame a `packetSubmission` antes de que el
hilo de arranque llegue a `if_qflush`; `?` si la familia garantiza ese orden no se puede fijar con el
desensamblado y es lo que la prueba de hardware debe medir.

## 7. Correcciones a `AWDL_STAGE1_TXPUSH_LLINK.md`

| Punto | Corrección |
|---|---|
| §10, paso 2 | `[ifp+0x900]` no es un puntero a un objeto externo: es una cola `netem` empotrada en el `ifnet` (`&ifp+0x900` es el argumento de `_netem_enqueue`, `0x5E62A3`). Vacía en el alta, el primer paquete va al `ifclassq`. |
| §4, "la traída" | La traída no es el único camino a la acción por ser la única puerta: además, la pila **sí** pasa por el `ifclassq`; lo que no hace es llamar a `packetSubmission`. |
| §1 | Se confirma: `ifnet_enqueue_pkt` no interviene en la salida de la pila; su papel es la inyección del proveedor (`nx_netif_host_output`, `netif_transmit`, `tx_push`). |

## 8. Verificación de direcciones

Cabezas comprobadas con `ida_funcs.get_func(ea).start_ea == ea`:
`0xFFFFFF80005E5ED0`, `0xFFFFFF80005E5F40`, `0xFFFFFF80005E6110`, `0xFFFFFF80005E65D0`,
`0xFFFFFF80005EF500`, `0xFFFFFF80005F32B0`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005D5730`,
`0xFFFFFF800063F3F0`, `0xFFFFFF8002B0CC76`, `0xFFFFFF8002B2CD68`, `0xFFFFFF8002B2C746`,
`0xFFFFFF8002B0D3E0`, `0xFFFFFF8002B2C860`, `0xFFFFFF80009E2440`, `0xFFFFFF80009E6120`,
`0xFFFFFF80009E5510`, `0xFFFFFF80009EA590`, `0xFFFFFF80009F86C0`, `0xFFFFFF80009FA2A0`,
`0xFFFFFF80009F4F60`, `0xFFFFFF80009BB780`, `0xFFFFFF8000713020`, `0xFFFFFF8000718340`,
`0xFFFFFF80005EA850`, `0xFFFFFF80005EB0F0`, `0xFFFFFF80009A4140`, `0xFFFFFF80005E8D20`.

Citas de instrucción dentro de una cabeza verificada: `0xFFFFFF800063FC4F`–`0xFFFFFF800063FC56`,
`0xFFFFFF800063FC16`–`0xFFFFFF800063FC30`, `0xFFFFFF80005EF504`, `0xFFFFFF80005EF50C`, `0xFFFFFF80005E5ED8`–`0xFFFFFF80005E5F16`,
`0xFFFFFF80005E6008`, `0xFFFFFF80005E6133`, `0xFFFFFF80005E6172`, `0xFFFFFF80005E61B2`–`0xFFFFFF80005E6284`,
`0xFFFFFF80005E62A3`, `0xFFFFFF80005E6343`, `0xFFFFFF80005E3CE9`, `0xFFFFFF80005E3CF4`,
`0xFFFFFF80005E3CFC`, `0xFFFFFF80005D5750`–`0xFFFFFF80005D5755`, `0xFFFFFF80005D5796`,
`0xFFFFFF80005EB1F7`, `0xFFFFFF80005E9693`, `0xFFFFFF80009E24F8`, `0xFFFFFF8002B0CCB2`,
`0xFFFFFF8002B2C82A`, `0xFFFFFF8002B2C8DE`, `0xFFFFFF8002B0D41F`, `0xFFFFFF80009A41F4`,
`0xFFFFFF80009EABD5`, `0xFFFFFF80009EAC03`, `0xFFFFFF80009EB0B7`, `0xFFFFFF80009F8E8B`,
`0xFFFFFF80009FA3D7`, `0xFFFFFF80009F506E`.

Datos leídos: puntero a `_ifnet_enqueue` en `ifp+0x258`/`ifp+0x260` (`0x63FC29`, `0x63FC30`); puntero a
`_dlil_output_handler` en `ifp+0x248` (`0x63FC56`); referencia a `nx_netif_na_txsync` en
`0xFFFFFF80009E0E9F`.
