# RegInfo +0x10/+0x40 en el lado Broadcom

Cierre del lado Broadcom de los campos `+0x10` y `+0x40` del `IOSkywalkEthernetInterface::RegistrationInfo`
(0x130 bytes) que la familia copia a `*(this+0x118)` vía `copyRegistrationInfo`
(`0xffffff8002b109f0`). Complementa `AWDL_STAGE1_REGINFO_AND_GATES.md` (lado familia) y
`AWDL_INTERFACE_BIRTH.md` §2.2 (camino de rol 6). Leído sobre el kernelcache de macOS 26.7.1
(25G241, Darwin 25.6.0, x86_64). Cada VA citado es cabeza de función o instrucción vista en el
desensamblado de esta sesión; los `?` marcan lo que el desensamblado no fija.

## 1. Corrección: `*(this+0x118)` es un contenedor, no la copia del RegInfo

`AWDL_STAGE1_REGINFO_AND_GATES.md` §2 describe `*(this+0x118)` como la copia del RegInfo. El
desensamblado completo de `copyRegistrationInfo` corrige el detalle: el ivar `+0x118` apunta a un
**contenedor** alocado aparte (liberado con `kalloc_type_view_90` en `free`), cuyo primer qword es el
puntero a la copia de 0x130 bytes (alocada con `kalloc_type_view_142`):

| VA | Función | Instrucción | Hecho |
|---|---|---|---|
| `0xffffff8002b10a1a` | `copyRegistrationInfo` (`0xffffff8002b109f0`) | `lea rdi, ...kalloc_type_view_142` → `call _IOMallocTypeImpl` | aloca la copia (0x130) |
| `0xffffff8002b10a26`–`0xffffff8002b10a2d` | ídem | `mov rcx, [rbx+118h]` → `mov [rcx], rax` | guarda la copia en `*(holder)` |
| `0xffffff8002b10a40` | ídem | `call _memmove` | copia 0x130 bytes |
| `0xffffff8002b10868` | `IOSkywalkEthernetInterface::free` (`0xffffff8002b1085e`) | `mov r14, [rdi+118h]` | toma el holder |
| `0xffffff8002b10896` | ídem | `mov rdi, [r14+10h]` → `call [rax+28h]` | libera el objeto en `holder+0x10` (no es RegInfo) |
| `0xffffff8002b111f6`, `0xffffff8002b112c1` | `ioctl_multicast` (`0xffffff8002b1116a`) | `mov [rax+18h], 0` / `mov [r12+10h], r15` | `holder+0x18` = contador de multicast, `holder+0x10` = `OSData*` con la lista |

Consecuencia: los accesos a `holder+0x10` y `holder+0x18` de `free` e `ioctl_multicast` que el doc
anterior dejó como lectores dudosos del RegInfo son campos propios del contenedor (un `OSData*` de
multicast y su recuento). Ninguno toca los campos `+0x10`/`+0x40` de la copia del RegInfo.

## 2. Lo que fija la familia

`initRegistrationInfo` (`0xffffff8002b108e2`) hace `bzero(RegInfo+8, 0x128)`
(`0xffffff8002b1091a`–`0xffffff8002b10923`) y escribe: qword `0x00000001300000001` en `+0`
(`0xffffff8002b10928`–`0xffffff8002b10932`), `+0x8` por el slot `+0x9B0` de la vtable de
`IOSkywalkNetworkInterface` con defecto 2 (`0xffffff8002b10958`), `+0xC` por el slot `+0x9B8`
(`0xffffff8002b10966`), `+0x14 = 0x100` (`0xffffff8002b10935`), `+0x24 = 0x22`
(`0xffffff8002b10969`), `+0x28` por el slot `+0x9C0` con defecto 0x20 (`0xffffff8002b10985`),
`+0x30` por el slot `+0x9F8` con defecto `"en"` (`0xffffff8002b109a1`), `+0x38` desde
`getBSDUnitNumber` (`0xffffff8002b109ad`), `+0x44` por el slot `+0x998` con defecto `0x2E`
(`0xffffff8002b109c5`) y `+0x48`/`+0x4C` por el slot `+0x988` con defecto `0x5DC`
(`0xffffff8002b109dd`–`0xffffff8002b109e0`). En ese cuerpo no se escribe `+0x10` ni `+0x40`: el
`bzero` los deja a cero. `copyRegistrationInfo` copia los 0x130 sin alterarlos y sólo deriva
`IOMACAddress` de `copia+0x108` (slot `+0x288`, `0xffffff8002b10a66`).

## 3. Escritores Broadcom del RegInfo: `+0x10`

Todas las xrefs a `initRegistrationInfo` cuyo llamador es Broadcom construyen el RegInfo en pila con
el mismo patrón (`bzero` de 0x130 + `initRegistrationInfo`). Inventariado completo de los
constructores Broadcom (xrefs verificadas esta sesión):

| Constructor | Cabeza | Sitio `initRegistrationInfo` | Escribe `+0x10` |
|---|---|---|---|
| `AppleBCMWLANCore::createNANMgmtDataInterface` | `0xffffff800165d4dc` | `0xffffff800165d675` | no |
| `AppleBCMWLANCore::createNANDataInterface` | `0xffffff800165d878` | `0xffffff800165da11` | no |
| `AppleBCMWLANCore::createProxInterface` (rol 6) | `0xffffff800165dcbe` | `0xffffff800165ddc7` | no |
| `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (rol 1, `"ap"`) | `0xffffff80016788f4` | `0xffffff8001678cc0` | no |
| `AppleBCMWLANCore::createSkywalkInterface` | `0xffffff80016aa874` | `0xffffff80016aaabc` | **sólo rol 1** |
| `AppleEthernetRL::startInterface` | `0xffffff800293fe60` | `0xffffff800293feb3` | `?` (fuera del objetivo AWDL, no inspeccionado) |
| `IOUserNetworkEthernet::updateRegistrationInfo` | `0xffffff8002b15b40` | `0xffffff8002b15b5a` | `+0x10 = 1` (`0xffffff8002b15c15`) |

Único escritor AWDL-relevante: en `createSkywalkInterface`, el rol 1 escribe `RegInfo+0x10 = 1`
(`mov [rbp+var_150], 1` en `0xffffff80016aab8d`; la base del RegInfo es `var_160`, `lea r15,
[rbp+var_160]` en `0xffffff80016aaa98`, por lo que `var_150` = `RegInfo+0x10`). El rol 2
(`AppleBCMWLANLowLatencyInterface`) no lo escribe. El camino de rol 6 (`createProxInterface`) y el
camino del 94 para la primaria (`setVIRTUAL_IF_CREATE`) lo dejan a cero.

Los cuatro `start` Broadcom que reciben el RegInfo como tercer argumento y lo pasan a
`registerEthernetInterface` no lo modifican:

| `start` | Cabeza | RegInfo recibido en | Escrituras sobre el RegInfo |
|---|---|---|---|
| `AppleBCMWLANIO80211APSTAInterface::start` | `0xffffff8001702b8e` | `rdx` → `r15` → `var_D8`, pasado intacto (`mov rsi, [rbp+var_D8]` en `0xffffff8001702fbf`) | ninguna |
| `AppleBCMWLANProximityInterface::start` | `0xffffff8001729376` | `rdx` → `r15` (`0xffffff800172938a`) | sólo `+0x48 = 0x59400000594` (`0xffffff80017296ff`–`0xffffff8001729709`, si RegInfo no nulo) |
| `AppleBCMWLANNANInterface::start` | `0xffffff800172b120` | `rdx` → `r15` (`0xffffff800172b134`) | ninguna |
| `AppleBCMWLANNANDataInterface::start` | `0xffffff8001745480` | `rdx` → `r15` (`0xffffff8001745494`) | ninguna |

Advertencia de lectura: en esos cuatro `start` abundan los stores `[r12+10h]`/`[r12+40h]`, pero la
base `r12` son descriptores de pila inicializados con `0xAAAA...` (`lea r12, [rbp+var_C0]` en
`0xffffff8001702c83`, `0xffffff800172b26d`, `0xffffff80017455d1`, `0xffffff80017294b6`): el
descriptor de registro del bus y el contenedor de colas, no el RegInfo.

## 4. Escritores Broadcom del RegInfo: `+0x40`

Los seis constructores Broadcom inspeccionados escriben un mapa de bits de energía. Valor semilla,
composición y resultado:

| Constructor | Semilla | Composición | Resultado |
|---|---|---|---|
| `createProxInterface` (rol 6) | 0 (`mov [r12+40h], r14d`, `0xffffff800165de48`) | `checkPropertyTrue(core, "IOPMResetPowerStateOnWake")` (`0xffffff800165de6b`): falso → `|4`, verdadero → 6 (`mov eax,6` / `cmovz eax,ecx`, `0xffffff800165de7a`–`0xffffff800165de7f`; store `0xffffff800165de94`) | **6 o 4** |
| `setVIRTUAL_IF_CREATE` (rol 1, 94) | 0 (`0xffffff8001678d45`) | misma propiedad (`0xffffff8001678d6e`), mismo patrón (`0xffffff8001678d7c`–`0xffffff8001678d81`); store `0xffffff8001678d97` | **6 o 4** |
| `createNANMgmtDataInterface` | 4 (`0xffffff800165d701`) | propiedad verdadera → `or var_190, 2` (`0xffffff800165d733`) | 4 o 6 |
| `createNANDataInterface` | 4 (`0xffffff800165da9d`) | ídem (`0xffffff800165dacc`) | 4 o 6 |
| `createSkywalkInterface` | 0 (`0xffffff80016aaaf7`) | propiedad → `+2` (`0xffffff80016aab26`); boot-arg `wlan.skywalk.legacy-power-off-mode` → `+4` (`shl ecx,2` en `0xffffff80016aab56`); rol 2 → `+4` más (`0xffffff80016aac1f`–`0xffffff80016aac22`) | 0–10 |
| `IOUserNetworkEthernet::updateRegistrationInfo` | — | valor fijo 8 en `+0x40` y 1 en `+0x10` (`0xffffff8002b15bb2`, `0xffffff8002b15c15`) | 8 |

Diferencia rol 6 frente a primaria: **ninguna en `+0x40`**. Tanto el camino de rol 6
(`createProxInterface`) como el de la primaria por el 94 (`setVIRTUAL_IF_CREATE`) escriben 6 si
`IOPMResetPowerStateOnWake` es verdadera en el provider y 4 si no. `createSkywalkInterface` (roles 1
y 2, no lo usa rol 6) añade los bits del boot-arg y del rol. `?` El significado semántico de los
bits 1 y 2 (energía, por los nombres de la propiedad y del boot-arg) se infiere de esos nombres; el
efecto real no aparece en el rango barrido (§5).

## 5. Barrido de lectores de `RegInfo+0x10` y `+0x40`

Patrón: carga `mov reg, [x+118h]` seguida en 12 instrucciones de un acceso `[reg+10h]` o
`[reg+40h]`, sobre 26 207 funciones en tres rangos: Broadcom
(`0xffffff800153f600`–`0xffffff8001593e90` y `0xffffff80015b9600`–`0xffffff8001766fb0`),
`IO80211*` (`0xffffff8002200000`–`0xffffff8002480000`) y `IOSkywalk*`
(`0xffffff8002a00000`–`0xffffff8002c00000`). Resultado: 36 coincidencias, todas falsos positivos:

- `AppleBCMWLANConfigManager::copyWlanPwrDutyCycleTable` (`0xffffff80016f060e`) y seis funciones de
  `AppleBCMWLANBonjourOffload` (`0xffffff80017115a6`, `0xffffff8001711a6a`, `0xffffff80017130ba`,
  `0xffffff8001713744`, `0xffffff80017145e6`): tienen su propio ivar en `+0x118` (otros objetos);
  los accesos `+0x10` son campos o locks propios.
- `IO80211LQMData::createDeltaLQMData` (`0xffffff800233cb4e`),
  `IO80211PeerMonitor::incrementTxExtStats` (`0xffffff800237d798`): ivars propios.
- `IOSCSITargetDevice` (`0xffffff8002a54c92`, `0xffffff8002a55610`, `0xffffff8002a557b2`): locks.
- `IOSkywalkEthernetInterface::free` (`0xffffff8002b1085e`) e `ioctl_multicast`
  (`0xffffff8002b1116a`): campos del contenedor de §1, no del RegInfo.
- `IOSkywalkEthernetInterface::initBSDInterfaceParameters` (`0xffffff8002b10bca`) y
  `prepareBSDInterface` (`0xffffff8002b10e58`): el store `+0x10` del patrón va sobre otro registro
  base, no sobre el holder; `?` base exacta no trazada instrucción a instrucción.
- `IOUserNetworkRxSubmissionQueue::replenishQueues` (`0xffffff8002b1a30c`) y cuatro funciones de
  `IOSurfaceRootUserClient` (`0xffffff8002b9b934`, `0xffffff8002b9c01a`, `0xffffff8002b9c10c`,
  `0xffffff8002b9c2f8`, `0xffffff8002b9c5fa`): clases ajenas con ivar propio en `+0x118`.

Ningún acceso del barrido alcanza `copia+0x10` ni `copia+0x40`. El consumidor de la copia es
`IOSkywalkNetworkInterface::registerNetworkInterface` (`0xffffff8002b0a0d6`), que la recibe del
holder (`mov rsi, [rax]` en `0xffffff8002b10ac5`, vía `registerEthernetInterface`
`0xffffff8002b10a84`) y sólo lee los offsets documentados (`+0x24`, `+0x28`, `+0x30`, `+0x38`,
`+0x4C`, `+0x108`; `AWDL_INTERFACE_BIRTH.md` §2.5). `?` El barrido de esta sesión cubre la
desreferencia simple (`holder+0x10/+0x40`); la doble desreferencia (`*(holder)+0x10/+0x40`) no se
repitió aquí y queda cubierta por la decompilación de las cuatro funciones de registro en
`AWDL_STAGE1_REGINFO_AND_GATES.md` §2, que no halló lecturas.

## 6. Conclusión

**Un driver que deja `RegInfo+0x10` y `RegInfo+0x40` a cero no cambia nada observable: no.**

- `+0x10` no tiene ningún escritor en el camino de rol 6 ni en el del 94 de la primaria
  (`setVIRTUAL_IF_CREATE`); el único escritor AWDL-relevante es `createSkywalkInterface` para el rol
  1, y ningún lector aparece en `IO80211Family`, `IOSkywalkFamily` ni `AppleBCMWLAN*` (§5). La copia
  viaja íntegra a los dos depósitos (holder de `IOSkywalkEthernetInterface` e `ivars+0xC0` de
  `IOSkywalkNetworkInterface`) y ahí muere.
- `+0x40` se escribe (6/4 en rol 6 y primaria; mapa de bits de energía en el resto), pero ningún
  lector aparece en el barrido de §5 ni en los consumidores de la copia. Los valores 4/6 de Broadcom
  no alteran ninguna ruta verificada del nacimiento, el registro BSD, el alta en IORegistry ni el
  despacho apple80211 de `awdl0`.
- Para la etapa 1: los valores `?` de `AWDL_STAGE1_IMPL_SPEC.md` §15.7–8 quedan resueltos como
  **0 en ambos campos**, con lo que el RegInfo del driver replica el comportamiento observable del
  camino Broadcom (que también arranca con ambos a cero por el `bzero` de `initRegistrationInfo`).

## 7. Búsquedas ejecutadas

| Punto | Búsqueda | Resultado |
|---|---|---|
| constructores | xrefs a `initRegistrationInfo` y a ambas `registerEthernetInterface` | 7 llamadores Broadcom + 2 de familia (§3) |
| escritores `+0x10` | desensamblado completo de los 6 constructores y 4 `start` | sólo `createSkywalkInterface` rol 1 y `IOUserNetworkEthernet` (§3) |
| escritores `+0x40` | ídem | mapa de bits en los 6 constructores; ningún `start` (§4) |
| lectores | barrido ventana 12 tras `mov reg,[x+118h]`, 26 207 funciones, 3 rangos | 36 hits, todos de otros objetos (§5) |
| contenedor `+0x118` | desensamblado de `copyRegistrationInfo`, `free`, `ioctl_multicast`, `deregisterEthernetInterface` | holder con copia en `+0x00`, `OSData*` en `+0x10`, contador en `+0x18` (§1) |
