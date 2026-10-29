# Orden de `setInterface` y del nacimiento del `ifnet` en el `bsd_start` de `awdl0`

Cierra el `?` de `AWDL_STAGE1_QUEUE_SHAPE.md` §9/§11 y el punto 11 de la §8.2 de
`AWDL_INDICE.md`: el orden entre `setInterface` (el enlace del cliente BSD) y la creación del
`ifnet` en el arranque del cliente BSD, y el consumidor del retorno 22 (`EINVAL`) del rechazo por
`num_txqs == 0`. Base: kernelcache de macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), IDA headless
(`idat -A -S`) sobre una copia de `boot_awdl.kc.i64` en `Build/ida_awdl/task_bsdstart/`. Todas las
direcciones son VA. Cada afirmación se leyó en el desensamblado; lo no visto lleva `?`.

## 1. Respuesta

**(a) `setInterface` ocurre dentro de `registerEthernetInterface`, antes del `registerService` y
por tanto antes del `ifnet`.** `IOSkywalkLogicalLink::setInterface(enlace, iface)` se ejecuta en
`0xffffff8002b0a38d`, dentro de `IOSkywalkNetworkInterface::registerNetworkInterface` (variante con
enlace lógico). El `ifnet` nace después, dentro de `IOSkywalkNetworkBSDClient::start`
(`kern_nexus_controller_alloc_net_provider_instance` en `0xffffff8002affbb5`), al que sólo se llega
vía el emparejamiento IOKit que dispara el `registerService(2)` del driver
(`0xffffff800172982a` en el camino Broadcom; `kIOServiceSynchronous = 2` según
`Kernel.framework/Headers/IOKit/IOService.h:100` del SDK).

**(b) El 22 del rechazo por `num_txqs == 0` no llega nunca al que llama a
`registerEthernetInterface`.** Hay dos vías y en ninguna llega como código de registro:

| Vía | Dónde se valida | Consumidor del 22 | Qué ve el driver |
|---|---|---|---|
| Registro en núcleo (Broadcom y etapa 1) | `nx_netif_default_llink_config` (`0xffffff80009faf35`) llamada desde `kern_nexus_controller_alloc_net_provider_instance` (`0xffffff80009a4310`), dentro del `start` del cliente BSD | El propio `kern_nexus_controller_alloc_net_provider_instance` lo devuelve (`0xffffff80009a436a`) y `IOSkywalkNetworkBSDClient::start` aborta con cualquier valor no nulo (`0xffffff8002affbd3`) | `registerEthernetInterface` ya devolvió 0; el síntoma es que no hay `ifnet` ni nombre BSD |
| Mensaje `0xE0060105` (sólo DriverKit) | `kern_nexus_netif_llink_add` (`0xffffff80009e6397`) desde `IOSkywalkNetworkBSDClient::message` (`0xffffff8002b00d69`) | `message` lo sustituye por `0xE00002BC` (`0xffffff8002b00d6e`); `addLogicalLink` ni siquiera lee ese retorno: falla por `containsObject` falso y devuelve `0xE00002BC` (`0xffffff8002b0a89a`) | `IOUserNetworkWLAN::_AddLogicalLink_Impl` (`0xffffff8002b07ce8`) se lo pasa al Dext |

La vía del mensaje no interviene en el registro en núcleo: el único emisor de `0xE0060105` es
`IOSkywalkNetworkInterface::addLogicalLink` y su único llamador es el thunk de DriverKit. En el
registro del rol 6, la información del enlace llega al netif dentro del propio alloc del proveedor,
no por mensaje.

## 2. Secuencia completa, paso a paso

### 2.1 Lado driver (referencia Broadcom; la etapa 1 replica este orden)

| VA | Instrucción / llamada | Efecto |
|---|---|---|
| `0xffffff800172973d` | `call IOSkywalkEthernetInterface::registerEthernetInterface` | sitio en `AppleBCMWLANProximityInterface::start` (`0xffffff8001729376`); el array de colas sale de `[rbp+__dst]` |
| `0xffffff8001729742`–`0xffffff800172974b` | `mov r12d,eax` / `test r12d,r12d` / `setz r15b` | el booleano de retorno del `start` de Broadcom es «el registro devolvió 0» |
| `0xffffff8001729752` | `jz loc_1729825` | sólo con registro exitoso se continúa; en fallo se liberan las colas por el slot `+0x148` (`0xffffff8001729784`–`0xffffff8001729820`) y se registra «registerEthernetInterface re…» con el error (`0xffffff8001729874`–`0xffffff800172989b`) |
| `0xffffff800172982a` | `mov esi,2; call [rax+5B0h]` | `IOService::registerService(kIOServiceSynchronous)`; la celda `+0x5B0` es `IOService::registerService(uint)` (`0xffffff8000a93250`) en las tres vtables implicadas (`__ZTV30AppleBCMWLANProximityInterface`, `__ZTV25IOSkywalkNetworkInterface`, `__ZTV25IOSkywalkNetworkBSDClient`); su retorno no se comprueba |

El resultado del registro (0 o error) es lo único que decide ese booleano: un fallo posterior del
cliente BSD no lo toca.

### 2.2 Registro: `registerEthernetInterface` → `registerNetworkInterface`

| VA | Instrucción / llamada | Efecto |
|---|---|---|
| `0xffffff8002b10aa5` | `call copyRegistrationInfo` | valida versión 1 / tamaño `0x130` y publica `IOMACAddress`; fallo ⇒ `deregisterEthernetInterface(0)` y retorno directo (`0xffffff8002b10ab3`–`0xffffff8002b10ab5`) |
| `0xffffff8002b10adf` | `call registerNetworkInterface` (variante con colas) | la `RegistrationInfo` que baja es `*(*(iface+0x118))` (`0xffffff8002b10abe`–`0xffffff8002b10ac5`), la copia que dejó `copyRegistrationInfo` en el contenedor `+0x118` (`AWDL_STAGE1_REGINFO_BROADCOM.md`) |
| `0xffffff8002b0a129`–`0xffffff8002b0a12f` | `isWMMSchedModelCapable` | byte de información `3` (`0xffffff8002b0a11d`) u `11` (`0xffffff8002b0a138`) |
| `0xffffff8002b0a14f` | `IOSkywalkQueueSet::createWithQueues` | nulo ⇒ `0xE00002C2` (`0xffffff8002b0a1bc`) |
| `0xffffff8002b0a176` | `IOSkywalkLogicalLink::createWithQueueSet` | nulo ⇒ `0xE00002C2` (`0xffffff8002b0a1c3`) |
| `0xffffff8002b0a196` | `call registerNetworkInterface` (variante con enlace) | el conjunto y el enlace pierden su referencia de creador al volver (`0xffffff8002b0a1a9`, `0xffffff8002b0a1d4`) |

Variante con enlace (`0xffffff8002b0a204`–`0xffffff8002b0a430`); `getLogicalLinkNexusInfo` en
`0xffffff8002b0a229`:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002b0a23c`–`0xffffff8002b0a279` | `cmp [r12+4],108h` / `cmp [r12],1` / `test r13,r13` / `cmp [r12+14h],400h` / `or rax,rdx` | validación del `RegInfo` y de los pools; fallo ⇒ `0xE00002C2` |
| `0xffffff8002b0a286`–`0xffffff8002b0a2a9` | `IOMallocTypeImpl` / `memmove 108h` | bloque nuevo de `0x108` bytes con la copia del `RegInfo`; el puntero se guarda en la celda referida por `[iface+0xC0]` (`0xffffff8002b0a292`: `mov rcx,[r14+0C0h]; mov [rcx],rax`); sin bloque ⇒ `0xE00002BD` (`0xffffff8002b0a419`) |
| `0xffffff8002b0a2b4` y `0xffffff8002b0a2e7` | celda `+0x9E8` → propiedad `IOInterfaceNamePrefix` | prefijo BSD («awdl» para rol 6) |
| `0xffffff8002b0a2cd` y `0xffffff8002b0a301` | celda `+0x9F0` → propiedad `IOInterfaceUnit` | unit del `RegInfo+0x38`, sólo si ≥ 0 |
| `0xffffff8002b0a34a`–`0xffffff8002b0a375` | bucle sobre los qsets del enlace (`getQueueSet`/`getQueueSetNexusInfo`), `cmp r14b,1` | exige exactamente un conjunto con el bit 0 del byte de información |
| `0xffffff8002b0a37e` | `IOSkywalkLogicalLink::setDefaultLogicalLink` | marca el enlace como por defecto |
| `0xffffff8002b0a38a`–`0xffffff8002b0a38d` | `IOSkywalkLogicalLink::setInterface(enlace, iface)` | **el enlace del cliente BSD queda ligado a la interfaz aquí** |
| `0xffffff8002b0a39d`–`0xffffff8002b0a3b2` | `OSOrderedSet::withCapacity(1)` a `[iface+0xC8]` y `setFirstObject(enlace)` (celda `+0x1B0` de `__ZTV12OSOrderedSet`, `0xffffff800025b750`) | la interfaz guarda el enlace |
| `0xffffff8002b0a3d6`–`0xffffff8002b0a3f5` | `[iface+0x98]` / `[iface+0xA0]` con retain (celda `+0x20`) | los pools del alta quedan en la interfaz |
| `0xffffff8002b0a392` / `0xffffff8002b0a41e` | `xor ebx,ebx` / `mov eax,ebx` | **retorno 0; la función no envía mensaje alguno ni toca el nexus** |

`IOSkywalkLogicalLink::setInterface` (`0xffffff8002b0c6d6`) guarda el puntero en `[enlace+0x18]`
(`0xffffff8002b0c6e4`) y transfiere la propiedad de cada cola de cada qset al pool: con interfaz no
nula `checkInPacketQueue` (`0xffffff8002b0c742`) y `setOwnerId(getRegistryEntryID())`
(`0xffffff8002b0c74a`–`0xffffff8002b0c75e`); con nula, `checkOutPacketQueue` y `setOwnerId(0)`
(`0xffffff8002b0c754`–`0xffffff8002b0c759`).

### 2.3 Emisores del mensaje `0xE00601xx` (barrido de bytes en todos los segmentos)

| Valor | Emisor | VA del sitio |
|---|---|---|
| `0xE0060105` (añadir enlace) | `IOSkywalkNetworkInterface::addLogicalLink` | `0xffffff8002b0a879` (`mov esi, 0E0060105h`) |
| `0xE0060106` (retirar enlace) | `IOSkywalkNetworkInterface::removeLogicalLink` | `0xffffff8002b0a7ad` |
| `0xE0060100` | `IOSkywalkNetworkInterface::reportLinkStatus` | `0xffffff8002b0b521` (`lea r14d, ds:0FFFFFFFFE0060100h[rdx*2]`) |
| `0xE0060102` | `IOSkywalkNetworkInterface::reportEventType` | `0xffffff8002b0a8d7` (`cmp eax, 0E0060102h`) |
| `0xE0060104` | `IOSkywalkNetworkInterface::setAggressiveness` | `0xffffff8002b0b941` |
| `0xE0060107` / `0xE0060108` | `addSteeringRule` / `removeSteeringRule` | `0xffffff8002b0aa12` / `0xffffff8002b0a66e` |

Ninguna función del registro (`registerEthernetInterface`, las dos variantes de
`registerNetworkInterface`) aparece en la lista: el registro en núcleo no envía `0xE0060105`.

### 2.4 `bsd_start`: `IOSkywalkNetworkBSDClient::start` (`0xffffff8002aff562`)

Lo invoca el emparejamiento IOKit con la interfaz como proveedor; la primera instrucción relevante
encadena la base y el resto prepara el terreno del `ifnet`:

| VA | Instrucción / llamada | Efecto |
|---|---|---|
| `0xffffff8002aff5c1`–`0xffffff8002aff5ce` | `lea rax, __ZTV18IOSkywalkBSDClient; call [rax+5D0h]; test al,al` | llamada a la celda de `start` de la vtable base (`ZTV+0x5D0` = vptr `+0x5C0`, la misma posición que ocupa `IOSkywalkNetworkBSDClient::start` en su propia vtable); la implementación de la base es `IOSkywalkBSDClient::start` (`0xffffff8002afd3ce`, símbolo del symtab); falso ⇒ fallo |
| `0xffffff8002aff689`–`0xffffff8002aff6a2` | `safeMetaCast(proveedor, IOSkywalkNetworkInterface::metaClass)` → `[cliente+0xE8]` | el proveedor del nub es la interfaz registrada |
| `0xffffff8002aff6ae`–`0xffffff8002aff6df` | propiedad `IODeferBSDAttach` y `cmp r15,r12` con el booleano verdadero | si vale true, el cliente se niega a arrancar (salta al cierre `0xffffff8002aff8dc`) |
| `0xffffff8002aff6ef`–`0xffffff8002aff739` | propiedad `IOInterfaceUnit` → `[rcx+148h]` | unit numérica |
| `0xffffff8002aff7c7` | `openInterface(iface)` (`0xffffff8002b00252`) | `iface->open(cliente, 0, 0)` por la celda `+0x5D0` (`0xffffff8002b0026f`) |
| `0xffffff8002aff7d4`–`0xffffff8002aff809` | celda `+0x9E8` (prefijo) + `snprintf("%s%u", prefijo, unit)` | **el nombre BSD («awdl0») se compone antes de que exista el `ifnet`** |
| `0xffffff8002aff83f` / `0xffffff8002aff86b` | `setProperty` «BSD Name» / «IOInterfaceName» | el nombre se publica en la interfaz |
| `0xffffff8002aff884`–`0xffffff8002aff8ce` | `[iface+0xC8]` → `OSOrderedSet::getObject(0)` (celda `+0x1F8`) → cast a `IOSkywalkLogicalLink` → bucle de qsets | exige el bit 0 del conjunto por defecto (`0xffffff8002aff8c4`) y lee el bit WMM (`0xffffff8002aff92b`) |
| `0xffffff8002aff939`–`0xffffff8002aff9b9` | `[iface+0x98]` / `[iface+0xA0]` (`+0xB8`) | coherencia de los pools; dos nulos o bits contradictorios ⇒ fallo |
| `0xffffff8002aff9f9` | `registerNexusControllerProvider` | falso ⇒ fallo |
| `0xffffff8002affa0c`–`0xffffff8002affab7` | parámetros del `ifnet` en la pila (`var_128`) | `+0x18` = `prepareNexusCallback` (`0xffffff8002affb47`); `+0x20`/`+0x28` = `getTxPoolHandle`/`getRxPoolHandle` (`0xffffff8002affb55`, `0xffffff8002affb68`); `+0x30` = `getLogicalLinkNexusInfo(enlace)` (`0xffffff8002affb71`–`0xffffff8002affb79`); `if_output`/`if_ioctl`/`if_detach` (`0xffffff8002affa4d`–`0xffffff8002affa6d`) |
| `0xffffff8002affbb5` | `call kern_nexus_controller_alloc_net_provider_instance` | **nacimiento del `ifnet`**; reintento ×50 con `IOSleep(100)` si devuelve 16 (`0xffffff8002affbba`–`0xffffff8002affbcc`) |
| `0xffffff8002affbd3`–`0xffffff8002affbd5` | `test eax,eax; jnz loc_2affc82` | cualquier error del alloc (incluido el 22) ⇒ `0xffffff8002affc82`: `[cliente+0xF8] = 0`, `closeInterface` (`0xffffff8002aff8eb`), `stop` (celda `+0x5C8`, `0xffffff8002aff8f9`) y retorno falso (`0xffffff8002aff8ff`) |
| `0xffffff8002affbdf`–`0xffffff8002affbfa` | `uuid_unparse_upper` → propiedad `IOSkywalkNexusUUID` | sólo en éxito |
| `0xffffff8002affcac` / `0xffffff8002affcb9` | `startIOReporting` / `call [rax+5B0h]` con `esi=0` | reporting y `registerService(kIOServiceAsynchronous)` del propio cliente; retorno verdadero (`0xffffff8002affcbf`) |

### 2.5 Dentro del alloc: validación del enlace y creación del netif

`kern_nexus_controller_alloc_net_provider_instance` (`0xffffff80009a4140`) consume un campo `+0x30`
como `llink_init` (`mov rsi,[r15+30h]`), el mismo offset en que `start` escribió el resultado de
`getLogicalLinkNexusInfo` en `RingVars+0x30`:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff80009a4300`–`0xffffff80009a4307` | `mov rsi,[r15+30h]` / `test rsi,rsi` | el `llink_init` del enlace; nulo ⇒ log «…logical link configuratio…» y `0x16` (22) (`0xffffff80009a4321`–`0xffffff80009a4352`) |
| `0xffffff80009a4310` | `call nx_netif_default_llink_config(netif, llink_init)` | configuración del enlace por defecto |
| `0xffffff80009a4315`–`0xffffff80009a431a` | `mov var_2C,eax` / `test eax,eax; jnz` | el error se conserva |
| `0xffffff80009a436a` / `0xffffff80009a4388`–`0xffffff80009a4390` | `mov r12d,[var_2C]` / `nx_close(nx,1)` | **la función devuelve exactamente el errno (22) del validador** y cierra el nx |

`nx_netif_default_llink_config` (`0xffffff80009faf20`) valida antes de copiar:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff80009faf2d`–`0xffffff80009faf3c` | `mov esi,1` / `call nx_netif_validate_llink_config` / `jnz` | **aquí se aplica la tabla de comprobaciones de `AWDL_STAGE1_QUEUE_SHAPE.md` §5**; el 22 sale sin tocar (`0xffffff80009fb08f`) |
| `0xffffff80009faf74` | `[netif+6F8h] = kalloc(kern_nexus_netif_llink_init)` | copia del `llink_init` |
| `0xffffff80009faf9d`–`0xffffff80009fb007` | `kalloc_ext` + bucle | copia del array de qsets a `[rcx+18h]`, marca «no añadido» en `[rcx+4]` |
| `0xffffff80009fb00b`–`0xffffff80009fb07b` | log «failed to alloc kern_nexu…» | sin memoria ⇒ `0xC` |

El netif lo crea la máquina del proveedor del nexus: el único llamador de `nx_netif_alloc`
(`0xffffff80009dff20`) es el constructor del proveedor `nx_netif_prov_nx_ctor`
(`0xffffff80009defd6`; xref única comprobada). `?` el tramo intermedio entre el alloc del
proveedor y ese constructor no se ha seguido instrucción a instrucción. El `ifnet` ya creado
regresa al cliente por la retrollamada que `start` registró en `RingVars+0x18`:

| VA | Instrucción | Efecto |
|---|---|---|
| `0xffffff8002b00350`–`0xffffff8002b00358` | `kern_nexus_get_context()` / `[ctx+100h] = nexus` | el contexto es el cliente (tercer argumento del alloc, `0xffffff8002affba6`); el manejador queda donde lo lee `message` (`[rbx+100h]` en `0xffffff8002b00d54`) |
| `0xffffff8002b0035f`–`0xffffff8002b0037b` | `runAction(gatedPrepareNexus, ifnet)` en el work loop | entrega bajo cerrojo |
| `0xffffff8002b0067c`–`0xffffff8002b00688` | `iface->prepareBSDInterface(ifnet, 0)` (celda `+0x8E8`) | no nulo ⇒ aborta |
| `0xffffff8002b006a6` | `mov [rbx+0F8h], r15` | **el `__ifnet*` queda en el cliente BSD** |
| `0xffffff8002b006b7` / `0xffffff8002b006ca` | `getPacketTapInfo` (celda `+0x950`) / `bpfattach` | el `ifnet` entra en BPF; `[cliente+146h] = 1` (`0xffffff8002b006cf`) |

## 3. El retorno 22: cadena completa en cada vía

### 3.1 Vía del registro en núcleo (la de `awdl0` y `en2`)

```
nx_netif_validate_llink_config (0xffffff80009fad40)  → 22
nx_netif_default_llink_config      (0xffffff80009faf35)  → 22 sin tocar
kern_nexus_controller_alloc_net_provider_instance (0xffffff80009a4310→0x9a436a) → 22
IOSkywalkNetworkBSDClient::start   (0xffffff8002affbd3)  → fallo: [cliente+0xF8]=0,
                                     closeInterface + stop, retorno falso
```

Consumidor final: el propio `start` del cliente BSD, que distingue sólo «cero / no cero». El 22 no
se transforma ni se propaga como `IOReturn`. En este punto el driver ya recibió 0 de
`registerEthernetInterface` y su `registerService(kIOServiceSynchronous)` ya devolvió; lo único que
revela el rechazo es la ausencia de nombre BSD (`getBSDName` vacío), que la etapa 1 convierte en
fallo de `startWithRegistration` (`Source/AirLayer/AWDLInterface.cpp:218`–`:223`). Broadcom no
llega a comprobarlo: su `start` devuelve el resultado del registro (`setz` en
`0xffffff800172974b`).

### 3.2 Vía del mensaje `0xE0060105` (sólo DriverKit)

```
addLogicalLink                (0xffffff8002b0a879)  messageClients(0xE0060105, enlace, 8)
  └ IOService::messageClients (0xffffff8000a95630)  applyToInterested(gIOGeneralInterest, …)
      └ applier               (0xffffff8000a963c0)  messageClient(tipo, destino, enlace, 8);
                                                    guarda el último retorno no nulo (0xa963e7)
          └ IOService::messageClient (0xffffff8000a953d3–0xa953f7)
                destino->message(tipo, emisor, enlace)  [el enlace viaja en rcx]
                └ IOSkywalkNetworkBSDClient::message caso 0xE0060105 (0xffffff8002b00d4b)
                    kern_nexus_netif_llink_add (0xffffff8002b00d69) → 22
                    ⇒ 0xffffff8002b00d6e: el 22 se sustituye por 0xE00002BC
addLogicalLink ignora el retorno del mensaje (0xffffff8002b0a881 sin test) y decide por
containsObject (0xffffff8002b0a894–0xffffff8002b0a8a2): el handler falló antes de
setLastObject (0xffffff8002b00d90), así que devuelve 0xE00002BC (0xffffff8002b0a89a)
  └ IOUserNetworkWLAN::_AddLogicalLink_Impl (0xffffff8002b07ce8, jmp) → al Dext
```

Detalles citables del mecanismo: `messageClients` llama por la celda `+0x760`
(`IOService::applyToInterested`), guarda el tipo/enlace/opciones en el contexto
(`0xffffff8000a95648`–`0xffffff8000a95653`) y devuelve el último no nulo
(`0xffffff8000a95672`); `messageClient` comprueba que el destino sea un `IOService`
(`0xffffff8000a95396`–`0xffffff8000a953ac`) y encadena `message` con el enlace como tercer
argumento (`0xffffff8000a953e6`: `mov rcx, r15`). En el handler, el enlace llega en `rcx`
(`mov r15, rcx` en `0xffffff8002b00bb3`); con rcx nulo responde `0xE00002C2`
(`0xffffff8002b00dee`). En éxito el handler añade el enlace al conjunto de la interfaz con
`setLastObject` (celda `+0x1B8` de `OSOrderedSet`, `0xffffff8002b00d90`).

El segundo llamador de `kern_nexus_netif_llink_add` está en `0xffffff80005fe2d9`, región BSD del
núcleo entre `_loopattach` (`0xffffff80005f82e0`) y `_rn_refines` (`0xffffff8000612860`), sin
función definida en la IDB, con la petición tomada de pila y en bucle
(`0xffffff80005fe2b0`–`0xffffff80005fe2ec`): la vía nativa/flowswitch, ajena a la familia
IOSkywalk. `?` qué función exacta es.

## 4. Correcciones que introduce este recorrido

- El `?` de `AWDL_STAGE1_QUEUE_SHAPE.md` §9 («viaja como 22 a `kern_nexus_netif_llink_add` y de ahí
  a `IOSkywalkNetworkBSDClient::message`») sólo describe la vía DriverKit. En el registro en
  núcleo el 22 no pasa por `kern_nexus_netif_llink_add` ni por `message`: lo produce y lo consume
  el alloc del proveedor dentro del `start` del cliente BSD.
- El `setInterface` del punto abierto no es el mensaje `0xE0060105`: es la llamada directa de
  `0xffffff8002b0a38d` (registro) o de `0xffffff8002b0a8aa` (`addLogicalLink` tras un alta de
  nexus exitosa). El handler del mensaje sólo la invoca con nulo, en la retirada
  (`0xffffff8002b00dcd`).
- La conclusión de recuentos de `AWDL_STAGE1_QUEUE_SHAPE.md` §1 y §5 sigue en pie: el validador es
  el mismo (`nx_netif_validate_llink_config`); lo que cambia es quién lo invoca (el alloc del
  proveedor en la vía del núcleo) y qué retorna en cada caso.
- El nombre BSD se compone y publica antes del `ifnet` (`0xffffff8002aff7d4`–`0xffffff8002aff86b`
  frente a `0xffffff8002affbb5`); la existencia de la propiedad no implica un `ifnet` vivo: el
  fallo posterior del alloc lo deja todo sin `ifnet` (`[cliente+0xF8] = 0`).

## 5. Aplicación a la etapa 1

- El orden del código (`AWDLInterface.cpp:206` registro → `:215`
  `registerService(kIOServiceSynchronous)` → `:218` lectura del nombre) coincide instrucción a
  instrucción con el camino Broadcom: `setInterface` ya ocurrió dentro del registro; el
  `ifnet` nace durante el `registerService` síncrono.
- Si T3 muestra registro con retorno 0 y sin `awdl0`, el rechazo del validador en el alloc es un
  resultado posible y se distingue en el log del kernel: el alloc registra
  «…logical link configuratio…» (`0xffffff80009a4338`) con nombre de función
  `kern_nexus_controller_alloc_net_provider_instance`, y `kern_nexus_netif_llink_add` registra
  «Invalid llink init params» (`0xffffff80009e63b7`) — este segundo sólo en la vía DriverKit. Las
  cadenas del validador («num_txqs == 0» y compañía) son las de `AWDL_STAGE1_QUEUE_SHAPE.md` §5.
- El rechazo no corrompe estado: el alloc cierra el nx (`nx_close`, `0xffffff80009a4390`) y el
  `start` del cliente llama a su `stop`; un reintento posterior del registro vuelve a pasar por el
  mismo camino completo.
- Sin riesgos nuevos de orden: la parte BSD corre en el hilo del `registerService` síncrono
  (semántica del SDK, `IOService.h:595`), igual que asumía `AWDL_INTERFACE_BIRTH.md` §2.3–§2.4.

## 6. Puntos abiertos

| Punto | Estado |
|---|---|
| Identidad de la función que cubre `0xffffff80005fe2d9` (segundo llamador de `kern_nexus_netif_llink_add`, región nativa/flowswitch) | `?` sin nombre en la IDB y sin símbolo en el volcado; ajena a la familia IOSkywalk |
| Si las cadenas de `nx_netif_validate_llink_config` llegan a un log observable por `log show` en la vía del alloc (el alloc y `llink_add` sí tienen os_log propio) | `?` se confirma en T3 con el kernel ring |
| Mecanismo IOKit exacto que empareja el nub `IOSkywalkNetworkBSDClient` con la interfaz registrada (personalities de la familia) | cubierto por `AWDL_INTERFACE_BIRTH.md` §2.3–§2.4; aquí sólo se verifica que el proveedor del `start` es la interfaz (`0xffffff8002aff689`) |
