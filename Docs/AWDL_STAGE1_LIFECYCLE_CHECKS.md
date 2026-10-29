# Comprobaciones de ciclo de vida: destino del tx data offset, parámetro de `deregister` y `r15` del `start` de Broadcom

Cierra los puntos 4, 5 y 7 de la sección «Puntos que quedan abiertos» de `AWDL_STAGE1_PENDING_CHECKS.md`
(agrupados bajo el #9 de la §8.2 de `AWDL_INDICE.md`): el campo del `ifnet` que consume el tx data
offset tras `getTxDataOffset`, la intención del parámetro `uint` de
`deregisterEthernetInterface`/`deregisterNetworkInterface` y el enlace de `r15` en
`AppleBCMWLANSkywalkInterface::start`. Base: kernelcache de macOS 26.7.1 (25G241, x86_64); lecturas
por IDA headless (`idat -A -S`) sobre una copia de `boot_awdl.kc.i64` en `Build/ida_awdl/task_lifecycle/`.
Los offsets de vtable son relativos al vptr salvo indicación `ZTV+` (celda en el ZTV crudo,
`ZTV+X` = `vptr+(X−0x10)`). Cada afirmación se leyó en el desensamblado o en el pseudocódigo; lo no
visto lleva `?`.

Comprobaciones ejecutadas para este documento, en orden:

| Sondeo | Resultado |
|---|---|
| Cuerpo de `IOSkywalkNetworkBSDClient::start` (`0xffffff8002aff562`–`0xffffff8002affce6`) y de `gatedPrepareNexus` (`0xffffff8002b0066c`–`0xffffff8002b006ed`), desensamblado + pseudocódigo | §1.1, §1.2 |
| Barrido del desplazamiento `13Ch` sobre las funciones del rango `0xffffff8002900000`–`0xffffff8002e00000` y con nombres Skywalk/BSDClient (106 accesos) | §1.1 |
| Barrido de `144h`/`146h` y de `58Ch` sobre funciones `_ifnet*`/`_dlil*`/`*netif*`/nexus y sobre el texto `0xffffff8000400000`–`0xffffff8002600000` | §1.3, §1.4 |
| Pseudocódigo del `start` del cliente BSD, de las tres `initBSDInterfaceParameters`, de `kern_nexus_controller_alloc_net_provider_instance` y de `nx_netif_default_llink_config` | §1.2, §1.3 |
| Pseudocódigo de `ifnet_allocate_extended` (`0xffffff800063f3f0`–`0xffffff800063ff61`) | §1.3, §1.4 |
| Cuerpos completos de las dos `deregister*`, de `stop`, `deregisterLogicalLink` y `destroySkywalkInterface`; celda `vptr+0x600` de las vtables Skywalk/AppleBCMWLAN; xrefs de las `deregister*` | §2 |
| Desensamblado de `AppleBCMWLANSkywalkInterface::start` (`0xffffff80015d909a`–`0xffffff80015d9756`), de `AppleBCMWLANLowLatencyInterface::start` y de `createSkywalkInterface`; barrido de `14E0h` | §3 |

## 1. Punto #4: el tx data offset acaba en `ifnet+0x58C`, no en el cliente BSD

### 1.1 La premisa corregida: `0xffffff8002affa94` escribe en un struct de pila

`IOSkywalkNetworkBSDClient::start` (cabeza `0xffffff8002aff562`) lee el tx data offset por la ranura
`0x9C0` y lo guarda, pero el receptor de la tienda no es el cliente:

| VA | Instrucción | Hecho |
|---|---|---|
| `0xffffff8002affa0c`–`0xffffff8002affa1b` | `lea r13, [rbp+var_278]` / `mov esi, 150h` / `call _bzero` | r13 = cuerpo del struct de pila (0x150 bytes), borrado a cero |
| `0xffffff8002affa20`–`0xffffff8002affa2a` | `mov rax, 15800000002h` / `mov [r13-8], rax` | cabecera del struct: dword `+0` = 2, dword `+4` = 0x158 |
| `0xffffff8002affa7c` | `mov dword ptr [r13+0], 80000000h` | prueba de que r13 no es el cliente: en el cliente `+0` está la vtable |
| `0xffffff8002affa84`–`0xffffff8002affa94` | `mov rdi, [rbx+0E8h]` / `call [rax+9C0h]` / `mov [r13+13Ch], ax` | la ranura `0x9C0` corre sobre la interfaz (rbx = this, `[cliente+0xE8]` = la interfaz desde `0xffffff8002aff696`–`0xffffff8002aff69b`) y el u16 devuelto va al struct de pila |
| `0xffffff8002affa9c`–`0xffffff8002affaa7` | `mov rax, [rbx+0F0h]` / `movzx eax, word ptr [rax+3Eh]` / `mov [r13+13Eh], ax` | el u16 de `RegInfo+0x3E` va al campo hermano; `[cliente+0xF0]` es la copia del `RegistrationInfo` de la interfaz (`0xffffff8002aff73c`–`0xffffff8002aff74d`: `mov rax, [rdi+0C0h]; mov rax, [rax]`) |

El dword del cliente en `+0x13C` existe y es otro campo, un estado que escribe `setPowerStateHandler`:

| VA | Función | Acceso |
|---|---|---|
| `0xffffff8002aff551` | `IOSkywalkNetworkBSDClient::init` | `mov dword ptr [rbx+13Ch], 1` |
| `0xffffff8002b00091` / `0xffffff8002b000e4` / `0xffffff8002b000f9` | `setPowerStateHandler` | escrituras dword 0 / 2 / 1 |
| `0xffffff8002affdb2` / `0xffffff8002affddd` | `ioctlHandler` | `cmp`/`mov eax, dword ptr [rbx+13Ch]` |
| `0xffffff8002b0017a` | `reportInterfaceWakeFlags` | `cmp dword ptr [rbx+13Ch], 0` |

Conclusión parcial: el headroom no llega nunca al objeto cliente; `PENDING_CHECKS` §1 se corrige.
Lo que el `start` rellena es el struct de inicialización del `ifnet`.

### 1.2 El struct es el `ifnet_init_eparams`

El struct de pila (base `rbp-280h`: la cabecera `{2, 0x158}` ocupa sus 8 primeros bytes y el cuerpo de
0x150 bytes arranca en `rbp-278h`) es un `ifnet_init_eparams`: la celda
`vptr+0x8E0` de las vtables de interfaz lo declara (`__ZN26IOSkywalkEthernetInterface26initBSDInterfaceParametersEP18ifnet_init_eparamsPP11sockaddr_dl`,
`0xffffff8002b10bca`; en `__ZTV28AppleBCMWLANSkywalkInterface` la cubre
`IO80211InfraInterface::initBSDInterfaceParameters`, `0xffffff800235ff5a`), y su cuerpo valida la
cabecera: `*(_DWORD *)a2 == 2 && *(_DWORD *)(a2 + 4) == 344` (pseudocódigo de `0xffffff8002b10bca`).
El `start` llama a la celda en `0xffffff8002affacb`–`0xffffff8002affadc` con r13 recargado a la base.

| Campo del eparams | Escritura en `start` | Contenido |
|---|---|---|
| `+0x00`/`+0x04` | `0xffffff8002affa20` | `{2, 0x158}` |
| `+0x08` | `0xffffff8002affa7c` | `0x80000000` (dword; el alloc exige `& 3 == 0`) |
| `+0x20` | `0xffffff8002affa3e` (`[r13+18h]`) | puntero del prefijo BSD (ranura `+0x9E8` de la interfaz, «awdl» en el rol 6) |
| `+0x28` | `0xffffff8002affa42` (`[r13+20h]`) | unit (u32) |
| `+0x30` | `initBSDInterfaceParameters` (`*(_DWORD *)(a2 + 48) = 6`) | tipo 6 (IFT_ETHER) |
| `+0x38` | `0xffffff8002affa54` | `IOSkywalkNetworkBSDClient::if_output` |
| `+0x108` / `+0x118` | `0xffffff8002affa5f` / `0xffffff8002affa6d` | `if_ioctl` / `if_detach` |
| `+0x144` | `0xffffff8002affa94` | **tx data offset** (u16, retorno de la ranura `0x9C0`) |
| `+0x146` | `0xffffff8002affaa7` | u16 de `RegInfo+0x3E` |
| `+0x148` | `0xffffff8002affaec` (`[rbp+var_138]` = `[cliente+0x130]`, con reserva 300/0) | u32 |

El eparams viaja al alloc dentro del struct de pila `var_128` (`v51[1] = &__b` en el pseudocódigo;
`mov [r15+8], r13` en `0xffffff8002affb31`), junto a la `sockaddr_dl` que `initBSDInterfaceParameters`
crea sobre el propio `RegInfo+0x1E` (`*a3 = (char *)v10 + 30`, con el MAC de `RegInfo+0x108`
en `a2+16` y longitud 6 en `a2+24`). `kern_nexus_controller_alloc_net_provider_instance`
(`0xffffff80009a4140`) saca el puntero con `v14 = *(_QWORD *)(a6 + 8)`, comprueba
`*(v14 + 72) == 0` (`+0x48`) y `(*(_BYTE *)(v14 + 8) & 3) == 0`, y llama a
`ifnet_allocate_extended(v14, &interface)` (sitio `0xffffff80009a420d`, citado también por
`AWDL_STAGE1_TXPUSH_LLINK.md` §5).

### 1.3 `ifnet_allocate_extended`: de `eparams+0x144` a `ifp+0x58C`

`_ifnet_allocate_extended` (cabeza `0xffffff800063f3f0`, fin `0xffffff800063ff61`) es el único lector
de `word ptr [reg+144h]`/`[reg+146h]` del barrido sobre funciones `_ifnet*`/`_dlil*`/`*netif*`/nexus
(el tercer hit del barrido, `mov al, [r13+146h]` en `nxp_qset_init`, `0xffffff8002aff256`, es el byte
de otra estructura):

| VA | Instrucción | Hecho |
|---|---|---|
| `0xffffff800063f626` | `movzx r15d, word ptr [rdi+144h]` | lectura del tx data offset |
| `0xffffff800063f62e` | `movzx eax, word ptr [rdi+146h]` | lectura del u16 hermano |
| `0xffffff800063f639` | `mov eax, [rdi+148h]` | lectura del u32 |
| `0xffffff800063f669`–`0xffffff800063f689` | `lea ecx, [rbx-1]` / `cmp ecx, 0FFh` / `test r15b, 7` / `setz` / `jnz` | validación conjunta (pseudocódigo: `strlen(name) >= 0x10 || (v6-1) >= 0xFF || (v7 & 7) != 0` → 22): **el tx data offset debe ser múltiplo de 8** |
| `0xffffff800063f677` / `0xffffff800063fd42` | `mov [rbp+var_84], r15d` / `mov eax, [rbp+var_84]` | el valor llega a la tienda |
| `0xffffff800063fd48` | `mov [r13+58Ch], ax` | **escritura en el `ifnet` nuevo (r13 = el ifp de `dlil_if_acquire`): campo `+0x58C`, u16** |
| `0xffffff800063fd54` | `mov [r13+58Eh], ax` | campo hermano `+0x58E` |
| `0xffffff800063fd62` | `mov [r13+590h], eax` | u32 en `+0x590` |
| `0xffffff800063fce8` | `mov qword ptr [r13+58Ch], 0` | camino de error: el trío se anula |

Calibración del layout con KPIs con nombre: `_ifnet_set_hdrlen` escribe el byte en
`[rdi+0E4h]` (`0xffffff8000641119`) y `_ifnet_set_mtu` el u32 en `[rdi+0E8h]`
(`0xffffff8000641079`), con lo que `if_data` empieza en `0xE0` y `0x58C`/`0x58E`/`0x590` quedan fuera:
es un trío propio del `ifnet` para los data offsets. El nombre declarado de esos campos en
`if_var.h` de xnu no consta ni en el SDK (Kernel.framework) ni en MacKernelSDK; el campo queda fijado
por offset y por su consumidor (`?` sobre el nombre).

### 1.4 El consumidor: `nx_netif_host_output`

El barrido de `58Ch` sobre todo el texto del núcleo da estos lectores del campo:

| VA | Función (cabeza) | Uso |
|---|---|---|
| `0xffffff80009ea796` / `0xffffff80009ea7b6` | `_nx_netif_host_output` (`0xffffff80009ea590`) | dos lecturas u16: `add eax, r9d` y `cmp` contra el límite (`0xffffff80009ea7a1`–`0xffffff80009ea7a8`), luego `lea esi, [r9+r14]` y `cmp` contra `word [rbx+28h] * dword [rbx+20h]` (`0xffffff80009ea7cd`–`0xffffff80009ea7d9`), y `pp_alloc_packet_by_size` (`0xffffff80009ea80c`): el netif reserva en el packet del nexus el tamaño que incluye el tx data offset al convertir el mbuf de la pila |
| `0xffffff80009bd035` | `_fsw_ring_flush` (`0xffffff80009bb8a0`) | misma aritmética de capacidad; exceso ⇒ error `0x22` |
| `0xffffff80009f870d` | `sub_FFFFFF80009F86C0` (el mismo que llama a `netif_transmit`, `AWDL_STAGE1_TXPUSH_LLINK.md` §3) | lee el campo, le suma el byte `[rdx+3Eh]`, comprueba paridad y usa `word [rdx+3Ah]`: aritmética de construcción del packet |
| `0xffffff8000bb2d9d` / `0xffffff8000bb2fe2` | `sub_FFFFFF8000BB2D3B` (sin símbolo) | dos lecturas u16 |
| `0xffffff80009f59c0` / `0xffffff80009f61fd` / `0xffffff80009f6955` | `_nx_netif_mbuf_to_filter_pkt_chain` / `_nx_netif_pkt_to_filter_pkt` / `_nx_netif_pkt_to_pkt` | lecturas de byte del mismo campo |

Conclusión del punto #4: la cadena completa es
`RegInfo (copia de la interfaz) +0x3C` → ranura `0x9C0` (`getTxDataOffset`, `0xffffff8002b0baba`) →
`ifnet_init_eparams+0x144` (pila del `start` del cliente BSD, `0xffffff8002affa94`) →
validación múltiplo de 8 (`0xffffff800063f67e`) → **`ifnet+0x58C` (u16, `0xffffff800063fd48`)** →
`nx_netif_host_output`, que dimensiona los packets de TX con él. El cliente BSD nunca almacena el
valor; su dword `+0x13C` es el estado de `setPowerStateHandler`.

## 2. Punto #7: el `uint` de `deregister` no se lee en ninguna ruta

### 2.1 Los dos cuerpos

`IOSkywalkEthernetInterface::deregisterEthernetInterface(uint)` (cabeza `0xffffff8002b10af4`,
fin `0xffffff8002b10b45`): único uso de registros `mov rbx, rdi` (`0xffffff8002b10afe`); reenvía `esi`
intacto a `IOSkywalkNetworkInterface::deregisterNetworkInterface(uint)` (`0xffffff8002b10b01`), y si
devuelve 0 libera el bloque colgado de `[this+0x118]` con `IOFreeTypeImpl`
(`0xffffff8002b10b22`, `kalloc_type_view_239`) y lo pone a nulo.

`IOSkywalkNetworkInterface::deregisterNetworkInterface(uint)` (cabeza `0xffffff8002b09e9c`,
fin `0xffffff8002b09ffd`): cuerpo completo sin una sola lectura de `esi`:

| VA | Hecho |
|---|---|
| `0xffffff8002b09eba` | `removeNonDefaultLogicalLinks` |
| `0xffffff8002b09ec9`–`0xffffff8002b09ee4` | `lastObject` (celda `+0x1D8`) y `safeMetaCast` a `IOSkywalkLogicalLink` |
| `0xffffff8002b09f0c`–`0xffffff8002b09f29` | bucle `getQueueSet(i)` + `removeSteeringRule` sobre `[enlace+28h]` |
| `0xffffff8002b09f30` | `setInterface(enlace, nulo)` |
| `0xffffff8002b09f41`–`0xffffff8002b09fbb` | release y nulo de `+0xC8`, `+0xA0`, `+0x98`, `+0x90`, `+0xD0` |
| `0xffffff8002b09fd5`–`0xffffff8002b09fee` | `IOFreeTypeImpl` de la copia del `RegInfo` en `+0xC0` (`kalloc_type_view_401`) |
| `0xffffff8002b09ff1` | `xor eax, eax` (retorno 0) — es el propio retorno de la función, no una «implementación base» aparte como decía `PENDING_CHECKS` §3 |

### 2.2 Llamadores: todos los xrefs

| Llamador | Sitio | Valor de `esi` |
|---|---|---|
| `registerEthernetInterface` (variante packet-queue, `0xffffff8002b10a84`) | `0xffffff8002b10ab5` | 0 (`xor esi,esi` en `0xffffff8002b10ab3`, camino de fallo de `copyRegistrationInfo`) |
| `registerEthernetInterface` (variante logical-link, `0xffffff8002b10b46`) | `0xffffff8002b10b71` | 0 (`xor esi,esi` en `0xffffff8002b10b6f`) |
| `AppleEthernetRL::stopInterface` | `0xffffff800295c05f` (cola con `jmp`) | 0 (`xor esi,esi` en `0xffffff800295c053`) |
| `IOSkywalkNetworkInterface::stop` (cabeza `0xffffff8002b09e66`) | `0xffffff8002b09e80` | **sin tocar**: conserva la baja-32 del puntero del proveedor (2º parámetro de `stop`, guardado en rbx en `0xffffff8002b09e6d` para la super-llamada de `0xffffff8002b09e96`) |
| `IOSkywalkNetworkInterface::free` (cabeza `0xffffff8002b09ffe`) | `0xffffff8002b0a007` | **sin tocar**: residuo del llamador de `free` |
| `deregisterLogicalLink` (cabeza `0xffffff8002b0a7d4`) | `0xffffff8002b0a7d9` (`jmp` puro) | el de su llamador; sus 21 referencias son celdas de vtable (datos, todas precedidas de `registerNetworkInterfaceWithLogicalLink` en la celda anterior), sin ninguna llamada directa de código en el KC |

El propio núcleo pasa un residuo no controlado en dos de los cinco sitios: el parámetro es
operativamente muerto.

### 2.3 La llamada de destrucción con `esi=2` es `terminate`

`AppleBCMWLANCore::destroySkywalkInterface(uint)` (cabeza `0xffffff80016aae4e`) llama en
`0xffffff80016aaeb4`–`0xffffff80016aaebc` con `mov esi, 2` a la celda `vptr+0x600`, que no es
`deregister`: es `IOService::terminate(uint)` (`0xffffff8000a937b0`) en
`__ZTV28AppleBCMWLANSkywalkInterface`, `__ZTV25IOSkywalkNetworkInterface`,
`__ZTV26IOSkywalkEthernetInterface` y el resto de la familia, y
`IO80211VirtualInterface::terminate` (`0xffffff8002330c50`) en
`__ZTV30AppleBCMWLANProximityInterface`, `__ZTV24AppleBCMWLANNANInterface`,
`__ZTV28AppleBCMWLANNANDataInterface` y `__ZTV33AppleBCMWLANIO80211APSTAInterface`; después
pasa por la celda `+0x90` (release) en `0xffffff80016aaed7`. La destrucción de referencia no pasa por
`deregister` en ningún punto, como ya adelantaba `PENDING_CHECKS` §3.

### 2.4 Conclusión

El `uint` no se lee en ninguna de las dos funciones ni en ninguna ruta del KC de 25G241: los tres
llamadores directos controlados pasan 0, `stop` y `free` pasan residuo, y la única llamada de
destrucción con valor no nulo (`esi=2`) va a `terminate`. Contrato operativo para un driver: pasar 0;
la limpieza del alta la cubre `free`, que llama a `deregisterNetworkInterface` incondicionalmente al
principio (`0xffffff8002b0a007`). `?` La intención declarada del parámetro no es deducible del
binario: ni el símbolo (`…Ej`, `unsigned int` sin nombre) ni las cabeceras disponibles la fijan.

## 3. Punto #5: `r15` en `AppleBCMWLANSkywalkInterface::start` es el `RegistrationInfo` del llamador

Cabeza `0xffffff80015d909a`; símbolo
`__ZN28AppleBCMWLANSkywalkInterface5startEP16AppleBCMWLANCorePN26IOSkywalkEthernetInterface16RegistrationInfoE`
→ `start(AppleBCMWLANCore *, IOSkywalkEthernetInterface::RegistrationInfo *)`.

| VA | Instrucción | Hecho |
|---|---|---|
| `0xffffff80015d90ae` | `mov r15, rdx` | r15 = 3er registro de argumento = 2º parámetro nominal, el `RegistrationInfo *` (r14 = rsi = `AppleBCMWLANCore *`, rbx = rdi = this) |
| `0xffffff80015d92b4` | `movzx eax, word ptr [r15+3Ch]` / `mov [r12+50h], ax` | primera lectura del headroom, al struct de registro en el bus (`r12 = rbp+bytes`) que va a la llamada de `0xffffff80015d930d` |
| `0xffffff80015d947a` | `mov [rbp+var_C8], r15` | copia del puntero antes de reutilizar el registro |
| `0xffffff80015d94dc` | `movzx r15d, byte ptr [r13+2Ah]` | r15 reutilizado (recuento de colas) |
| `0xffffff80015d9518` | `mov r15, [rbp+var_C8]` | restauración exacta del puntero |
| `0xffffff80015d9583` | `movzx eax, word ptr [r15+3Ch]` / `mov [rdx+28h], ax` | segunda lectura; el valor queda en los ivars (`rdx = [rbx+138h]`) |
| `0xffffff80015d9593` / `0xffffff80015d9597` | `sub [r15+48h], eax` / `sub [r15+4Ch], eax` | **los dos MTU del `RegInfo` se reducen en el headroom antes de registrar** (Broadcom los fija a 0x594 en `0xffffff80017296ff`–`0xffffff8001729709`, el par que la cabecera local llama `AWDL_REGINFO_MTU`) |
| `0xffffff80015d95b4` | `call IO80211InfraInterface::registerInfraEthernetInterface` | rsi = r15: el `RegInfo` ya mutado |

Enlaces de la función y del argumento:

- Método virtual, última celda de la vtable: `__ZTV28AppleBCMWLANSkywalkInterface` en
  `ZTV+0x14F0` (`0xffffff80017e3b18`), misma posición en `__ZTV31AppleBCMWLANLowLatencyInterface`
  (`0xffffff80017e5980`).
- Despachador: `AppleBCMWLANCore::createSkywalkInterface(AppleBCMWLANSkywalkInterface **,
  apple80211SkywalkIfCreateParams_t *, IO80211VirtualInterface *)` (cabeza `0xffffff80016aa874`),
  que en `0xffffff80016aac2f`–`0xffffff80016aac46` arma el `RegInfo` de pila
  (`lea rdx, [rbp+var_160]`), despacha `call qword ptr [rax+14E0h]` con rdi = la interfaz nueva y
  rsi = el core, y comprueba `test al, al`.
- Único llamador directo de código en el KC: `AppleBCMWLANLowLatencyInterface::start`
  (cabeza `0xffffff80015e00bc`), cola con `jmp` en `0xffffff80015e00f6` que pasa su propio 3er
  parámetro en rdx. Su pseudocódigo hace `setInitMacAddress(this, a3 + 44)` con `a3` tipado
  `ether_addr *`: 44 × 6 bytes = `+0x108`, el offset del MAC del `RegInfo` (el mismo que
  `initBSDInterfaceParameters` lee en `RegInfo+264`), lo que confirma la identidad del objeto.

Conclusión: `r15` se toma de `rdx` en la propia cabeza (`0xffffff80015d90ae`) y enlaza el
`RegistrationInfo` que el llamador entrega — el objeto original del llamador, no las copias de la
familia en `iface+0xC0`/`+0x118`, que se crean después dentro de `registerNetworkInterface`. Las dos
lecturas de `+0x3C` de `PENDING_CHECKS` §1 quedan así justificadas instrucción a instrucción, y aparece
un efecto no documentado: la familia muta el `RegInfo` del llamador restando el headroom a sus dos
MTU.

## 4. Correcciones que introduce este recorrido

| Documento | Afirmación anterior | Corrección |
|---|---|---|
| `AWDL_STAGE1_PENDING_CHECKS.md` §1 | «guarda el resultado u16 en `[clienteBSD+0x13Ch]` … el headroom llega así al cliente BSD» | el almacén de `0xffffff8002affa94` va al `ifnet_init_eparams` de pila (`+0x144`); el cliente nunca guarda el valor; su dword `+0x13C` es el estado que escriben `init`/`setPowerStateHandler` (§1.1) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §1, punto abierto #4 | campo del `ifnet` consumidor sin seguir | cerrado: `ifnet+0x58C` (u16), consumido por `nx_netif_host_output` (§1.3, §1.4) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §1, punto abierto #5 | enlace de `r15` sin fijar | cerrado: `r15 = rdx` en `0xffffff80015d90ae`, el `RegistrationInfo` del llamador (§3) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §3 | «la implementación base tampoco lo usa y siempre devuelve 0 (`0xffffff8002b09ff1`)» | `0xffffff8002b09ff1` es el propio retorno de `deregisterNetworkInterface`; no hay segunda implementación (§2.1) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §3, punto abierto #7 | intención del `uint` sin deducir | sigue `?`, ahora con el inventario completo de llamadores y los dos residuos (`stop`, `free`); la llamada de destroy con `esi=2` identificada como `terminate` (§2.2, §2.3) |
| `AWDL_STAGE1_BSDSTART_ORDER.md` §2.4 | «`0xffffff8002affa0c`–`0xffffff8002affab7` \| parámetros del `ifnet` en la pila (`var_128`)» | esa ventana rellena el `ifnet_init_eparams` (base `rbp-280h`, cabecera incluida); el struct `var_128` (`{1, eparams, sockaddr_dl, callback, pools, llink}`) se construye después, en `0xffffff8002affb23`–`0xffffff8002affb79` (§1.2) |

## 5. Aplicación a la etapa 1

- El headroom 64 de `RegInfo+0x3C` (`AWDLInterface.cpp:196`) llega a `ifp+0x58C` sin intervención del
  driver: la familia lo lee por la ranura `0x9C0` durante el `bsd_start` y lo entrega al `ifnet`. La
  validación de `ifnet_allocate_extended` exige múltiplo de 8 (64 cumple); el defecto 6
  (`+0x3A` sin corregir) dejaría el campo a 0.
- `RegInfo+0x3E` viaja al campo hermano `ifp+0x58E` (`0xffffff800063fd54`); la etapa 1 lo deja a 0
  (el `bzero` del registro), igual que la referencia de Proximity, que sólo escribe `+0x3C`.
- Diferencia observada con la referencia: `AppleBCMWLANSkywalkInterface::start` resta el headroom de
  los dos MTU del `RegInfo` antes de registrar (`0xffffff80015d9593`, `0xffffff80015d9597`), y el netif
  añade por su cuenta el tx data offset al dimensionar los packets de salida (§1.4). La etapa 1
  registra MTU 0x594 con headroom 64 sin la resta (`AWDLInterface.cpp:198`). `?` La consecuencia
  exacta en la ruta del kext no está medida: la etapa 1 no atraviesa ese `start` de Broadcom, y el
  ajuste del MTU registrado (payload neto) quedaría por replicar si T3 muestra descartes por tamaño.
- `deregisterEthernetInterface`: la limpieza del alta fallido la cubre `free` (`0xffffff8002b0a007`);
  si se llama a la función explícita (patrón de `AppleEthernetRL` en `stop`), el valor operativamente
  seguro es 0.

## 6. Puntos abiertos tras este documento

| # | Punto | Estado |
|---|---|---|
| 4 | Campo del `ifnet` que consume el tx data offset | **Cerrado**: `ifnet+0x58C` (u16; tienda `0xffffff800063fd48`); consumidor `nx_netif_host_output` (`0xffffff80009ea796`, `0xffffff80009ea7b6`) y `fsw_ring_flush`/`sub_FFFFFF80009F86C0`. `?` nombre declarado en `if_var.h` de xnu, ausente del SDK y de MacKernelSDK: campo fijado por offset y consumidor |
| 5 | Enlace de `r15` en `AppleBCMWLANSkywalkInterface::start` | **Cerrado**: `r15 = rdx` en `0xffffff80015d90ae` (2º parámetro nominal, `RegistrationInfo *` por el símbolo); guardado en `var_C8` (`0xffffff80015d947a`) y restaurado (`0xffffff80015d9518`) antes de la segunda lectura |
| 7 | Intención declarada del `uint` de `deregister*` | **Parcial**: parámetro muerto en 25G241 (sin lecturas en los dos cuerpos; tres llamadores con 0, `stop` y `free` con residuo); la destrucción de referencia usa `terminate(2)`, no `deregister`. `?` intención declarada, no deducible del binario |
| — | Nombre xnu de los campos `ifnet+0x58C`/`+0x58E`/`+0x590` | `?` nuevo (§1.3) |
| — | Consecuencia de no restar el headroom del MTU registrado en la etapa 1 | `?` nuevo (§5) |

## 7. Verificación de direcciones

Cabezas confirmadas con `ida_funcs.get_func(ea).start_ea == ea`:
`0xffffff800063f3f0` (`_ifnet_allocate_extended`), `0xffffff8000640270` (`_ifnet_set_flags`),
`0xffffff8000641070` (`_ifnet_set_mtu`), `0xffffff8000641110` (`_ifnet_set_hdrlen`),
`0xffffff8000641130` (`_ifnet_hdrlen`), `0xffffff8000641090` (`_ifnet_mtu`),
`0xffffff8000641190` (`_ifnet_set_baudrate`), `0xffffff80005de1f0` (`_ifnet_set_promiscuous`),
`0xffffff80009a4140` (`kern_nexus_controller_alloc_net_provider_instance`),
`0xffffff80009faf20` (`nx_netif_default_llink_config`),
`0xffffff80009ea590` (`_nx_netif_host_output`), `0xffffff80009bb8a0` (`_fsw_ring_flush`),
`0xffffff80009f86c0` (`sub_FFFFFF80009F86C0`), `0xffffff8000bb2d3b` (`sub_FFFFFF8000BB2D3B`),
`0xffffff8002aff562` (`IOSkywalkNetworkBSDClient::start`),
`0xffffff8002b0066c` (`gatedPrepareNexus`),
`0xffffff8002b09e66` (`IOSkywalkNetworkInterface::stop`),
`0xffffff8002b09e9c` (`deregisterNetworkInterface`),
`0xffffff8002b09ffe` (`IOSkywalkNetworkInterface::free`),
`0xffffff8002b0a7d4` (`deregisterLogicalLink`),
`0xffffff8002b10a84` / `0xffffff8002b10af4` / `0xffffff8002b10b46`
(`registerEthernetInterface` ×2 y `deregisterEthernetInterface`),
`0xffffff8002b10bca` (`initBSDInterfaceParameters`),
`0xffffff80016aae4e` (`destroySkywalkInterface`),
`0xffffff80016aa874` (`createSkywalkInterface`),
`0xffffff80015d909a` (`AppleBCMWLANSkywalkInterface::start`),
`0xffffff80015d8dd4` (`AppleBCMWLANSkywalkInterface::start(IOService *)`),
`0xffffff80015e00bc` (`AppleBCMWLANLowLatencyInterface::start`).

Citas de instrucción dentro de una cabeza verificada (todas las direcciones de §1–§3; los sitios de
`0xffffff8002affa40`–`0xffffff8002affab7` y `0xffffff80015d909a`–`0xffffff80015d95c9` están dentro de
las cabezas `0xffffff8002aff562` y `0xffffff80015d909a`; `0xffffff800063f626`–`0xffffff800063fd62`
dentro de `0xffffff800063f3f0`; `0xffffff80009ea740`–`0xffffff80009ea80c` dentro de
`0xffffff80009ea590`; `0xffffff80009bd035` dentro de `0xffffff80009bb8a0`; `0xffffff80009f870d`
dentro de `0xffffff80009f86c0`; `0xffffff80016aac2f`–`0xffffff80016aac48` dentro de
`0xffffff80016aa874`; `0xffffff80017296ff`–`0xffffff8001729752` dentro del `start` de
`AppleBCMWLANProximityInterface` verificado por `AWDL_STAGE1_BSDSTART_ORDER.md` §2.1).

Datos leídos: celdas `vptr+0x8E0`/`+0x8E8`/`+0x9C0` de `__ZTV25IOSkywalkNetworkInterface`,
`__ZTV26IOSkywalkEthernetInterface`, `__ZTV28AppleBCMWLANSkywalkInterface` y
`__ZTV30AppleBCMWLANProximityInterface`; celda `vptr+0x600` de las vtables Skywalk/AppleBCMWLAN;
celda `ZTV+0x14F0` de `__ZTV28AppleBCMWLANSkywalkInterface` (`0xffffff80017e3b18`) y de
`__ZTV31AppleBCMWLANLowLatencyInterface` (`0xffffff80017e5980`).
