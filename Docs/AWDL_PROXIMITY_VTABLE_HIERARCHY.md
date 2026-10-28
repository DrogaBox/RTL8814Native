# Jerarquía AppleBCMWLANProximityInterface ↔ IO80211AWDLProtocol ↔ IO80211VirtualInterface

Cierra el `?` que quedó en `AWDL_KERNEL_ROLE_TABLE.md` §6: por qué `AppleBCMWLANProximityInterface::init` despacha por la vtable de `IO80211AWDLProtocol` en vez de por su propia vtable, y cuál es la relación de herencia real. Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), lecturas del archivo + rea/IDA adjunto. Lo no visto va como `?`.

**Respuesta corta:**

1. **La herencia es real y directa**: `AppleBCMWLANProximityInterface` : `IO80211AWDLProtocol` : `IO80211VirtualInterface` : `IO80211SkywalkInterface` : … La clase Broadcom no "reusa la tabla de otro por conveniencia": es subclase C++/IOKit de `IO80211AWDLProtocol` y su vtable (`0xffffff80017f5b68`) es propia y completa.
2. **El dispatch por la vtable de la base es el codegen de una llamada calificada al miembro virtual de la base bajo el ABI C++ de kexts**: el compilador no la devirtualiza; carga `&__ZTV19IO80211AWDLProtocol` (importado como símbolo externo del kext IO80211Family) desde un slot de `__data` de Broadcom y llama al slot de la `init` de 4 argumentos, que contiene la `init` heredada de `IO80211VirtualInterface` (porque `IO80211AWDLProtocol` no la sobrescribe).

## 1) Cadena de herencia (evidencia: metaclass en el código de inicialización)

No hay RTTI de C++ que consultar (`__ZTI`/`__ZTS`: 0 matches en `syms.txt` y `syms_bcm.txt`; kernel sin RTTI). La jerarquía IOKit queda registrada en el ctor de cada `MetaClass`, que le pasa a `OSMetaClass::OSMetaClass` el `gMetaClass` del padre. Los tres, decompilados con rea/IDA:

| clase | función que registra la metaclase | dirección | `superclass:` literal | `classSize` |
|---|---|---|---|---|
| `AppleBCMWLANProximityInterface` | `__GLOBAL__sub_I_AppleBCMWLANProximityInterface.cpp` (llamada a `OSMetaClass::OSMetaClass` en `0xffffff800172ad0e`) | `0xffffff800172ace5` | `&IO80211AWDLProtocol::gMetaClass` (`0xffffff800248cde8`, syms.txt:12226) | `0x140` |
| `IO80211AWDLProtocol` | `__ZN19IO80211AWDLProtocol9MetaClassC1Ev` | `0xffffff8002198bc8` | `&IO80211VirtualInterface::gMetaClass` (`0xffffff800248e478`, syms.txt:15631) | `0x130` |
| `IO80211VirtualInterface` | `__ZN23IO80211VirtualInterface9MetaClassC1Ev` | `0xffffff8002330002` | `&IO80211SkywalkInterface::gMetaClass` (`0xffffff800248dfa0`, syms.txt:15410) | `0x130` |

```
AppleBCMWLANProximityInterface (0x140)  ← com.apple.driver.AppleBCMWLANCoreMac
  └─ IO80211AWDLProtocol (0x130)        ← kext IO80211Family
       └─ IO80211VirtualInterface (0x130)
            └─ IO80211SkywalkInterface  (0x248dfa0)
                 └─ … (cadena por encima de SkywalkInterface no verificada en esta pasada, ?)
```

Detalles:

- Los tamaños dicen que `IO80211AWDLProtocol` **no agrega ivars** (0x130 → 0x130): es una clase de interfaz/protocolo pura. `AppleBCMWLANProximityInterface` agrega 0x10 de ivars.
- **Los `gMetaClass` leídos del archivo están en cero** (`0xffffff80017fd4e8` y `0xffffff800248cde8`: 5 qwords = 0): las instancias se rellenan al cargar el kext por los `__GLOBAL__sub_I_`; en la imagen en disco la cadena vive en el código de arriba, no en los datos. Por eso el intento de walk por archivo no produce nada y la evidencia es la decompilación de los static-init/`MetaClassC1`.
- Los tamaños de las vtables corroboran herencia simple sin herencias múltiples (una sola `__ZTV` por clase, sin vtables secundarias): 506 → 569 → 572 slots (§3).

## 2) La llamada "padre", instrucción por instrucción

`AppleBCMWLANProximityInterface::init(AppleBCMWLANCore*, ether_addr*, unsigned int, char*)` (`0xffffff8001718db0`, 0x684 bytes, kext Broadcom `com.apple.driver.AppleBCMWLANCoreMac`). Listado de rea/IDA:

```
0xffffff8001718dde:  mov rax, [rdi]                    ; this->vptr (vtable de ProximityInterface)
0xffffff8001718de1:  call qword ptr [rax+88h]          ; virtual this->init() 0-args
                                                       ;   slot +0x88 de la vtable propia = initEv de Proxi (0x1718d72)
0xffffff8001718deb:  mov rax, cs:off_FFFFFF80017DE1C0  ; carga un puntero desde __data de Broadcom
0xffffff8001718df2–dfe: rdi=rbx, rsi=r14, rdx=r13, ecx=r12d, r8=r15   ; los mismos 5 argumentos
0xffffff8001718e01:  call qword ptr [rax+0E78h]        ; ← la llamada que el pseudocódigo muestra como off_FFFFFF80024572B8(...)
0xffffff8001718e07:  test al, al                       ; bool
0xffffff8001718e09:  jz loc_FFFFFF8001719185           ; error → r14d = 0
```

Qué hay detrás de `off_FFFFFF80017DE1C0`, leído del archivo:

- qword en `0xffffff80017de1c0` = `0x0040000002356440` → decodifica a **`0xffffff8002456440` = `__ZTV19IO80211AWDLProtocol`** — la dirección del **símbolo** de la vtable (con header), no el vptr. El slot en sí no tiene nombre propio (cae dentro de un rango `_.str.367+0x9e8`; IDA lo muestra como `off_`).
- `[ZTV + 0xE78]` = `0xffffff80024572b8`, que es `vptr(0x2456450) + 0xE68` — el slot de la **`init` de 4 argumentos** en la vtable de `IO80211AWDLProtocol`, cuyo contenido es `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` (`0xffffff80023305f8`), verificado en el diff de §3.
- El kext Broadcom **importa** los símbolos de la clase base como externos (undefs en `syms_bcm.txt`: `__ZTV19IO80211AWDLProtocol` :8568, `__ZN19IO80211AWDLProtocolC2EPK11OSMetaClass` :7537, `__ZN19IO80211AWDLProtocolD2Ev` :7538, `__ZN19IO80211AWDLProtocol10gMetaClassE` :7536) — patrón estándar de un kext que construye subclases de una clase que vive en otro kext.

**Por qué la vtable de la base y no la propia:** la llamada corresponde a la `init` de la clase base (el despacho apunta al slot de la `init(4 args)` usando la vtable de `IO80211AWDLProtocol`, la clase base inmediata). Con el ABI C++ de kexts de Apple (`-fapple-kext`), las llamadas a funciones miembro virtuales nunca se devirtualizan a `call` directas — ni siquiera las calificadas — y una llamada calificada a la base se emite como carga del literal de vtable de la clase nombrada + `call [ztv + slot]`, que es exactamente lo observado (atribución del mecanismo al flag estándar: inferencia razonable; el codegen observado es hecho). El contraste dentro de la misma función lo confirma:

- llamada **no virtual** cross-kext → `call` directa: `__ZN23IO80211SkywalkInterface14setInterfaceIdEj` en `0xffffff8001718e27`, `__ZN22AppleBCMWLANTaskmaster13withWorkQueueEP16IO80211WorkQueuej` en `0xffffff8001718ea4`;
- llamada **virtual sobre `this`** → vía vptr propio: `call [this->vptr + 0x88]` en `0xffffff8001718de1`;
- llamada **virtual calificada a la base** (cross-kext) → vía ZTV importado de la base: `call [rax+0xE78]` en `0xffffff8001718e01`.

La ortografía exacta en el fuente (`IO80211AWDLProtocol::init(...)` u otra forma equivalente) no es observable; lo observado es el despacho por la vtable de la base en el slot de la init heredada.

## 3) Estructura de las vtables (lectura del archivo con fixups + nombres de syms)

Mapeo archivo: `otool -l` por segmento (el delta `vmaddr−fileoff` varía por segmento: 0x200000 kernel, 0xFC000 en la región `0xffffff80017de000` del kext Broadcom, 0x100000 en la región de IO80211Family — la regla plana `fileoff = VA − 0xffffff8000100000` del doc anterior solo vale dentro de su segmento). Fixups: `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)`, revalidado contra `AirLink`. Headers de las tres vtables: `[0x0, 0x0]` (offset-to-top 0, sin typeinfo — consistente con no-RTTI).

| vtable | símbolo | rango | vptr | slots |
|---|---|---|---|---|
| `__ZTV23IO80211VirtualInterface` | `0xffffff8002472b78` | …`0xffffff8002473b58` | `0xffffff8002472b88` | 506 |
| `__ZTV19IO80211AWDLProtocol` | `0xffffff8002456440` | …`0xffffff8002457618` | `0xffffff8002456450` | 569 |
| `__ZTV30AppleBCMWLANProximityInterface` | `0xffffff80017f5b68` | …`0xffffff80017f6d58` | `0xffffff80017f5b78` | 572 |

El vptr que `withOptions` guarda (`0xffffff80017f5b78`, store en `0xffffff8001715a2a`) = símbolo + 0x10 ✓.

**IO80211AWDLProtocol vs IO80211VirtualInterface** (506 slots comunes): solo 3 overrides reales, todos de identidad — `D1` (+0x0), `D0` (+0x8), `getMetaClass` (+0x38) — y 4 diferencias que son la misma función con forma de fixup distinta (rebase vs bind; verificado decodificando el low32: +0x470 y +0xba8 apuntan a `0xffffff8000bb6c80` y `getController` respectivamente en ambas). **63 slots nuevos**, todos en la cola (+0xFD0–0x11C0), apuntando todos al mismo stub de kernel `0xffffff8000bb59c0` (las primeras 40 verificadas una a una; las 23 restantes asumen el mismo patrón, `?`; identidad del stub: sin nombre en syms.txt, `?` — por el patrón IOKit, cuerpos default/reservados plegados por ICF, inferencia). Es decir: **`IO80211AWDLProtocol` declara la superficie virtual AWDL (comandos apple80211 y slots reservados) con cuerpos default, y no sobrescribe ningún comportamiento de `IO80211VirtualInterface`** — coincide con que no exista ningún símbolo de método de `IO80211AWDLProtocol` en syms.txt más allá de ctor/dtor/metaclass.

**AppleBCMWLANProximityInterface vs IO80211AWDLProtocol** (569 slots comunes): **95 overrides** — identidad (D1/D0/getMetaClass), ciclo de vida IOKit (`init` 0-args +0x88, `free` +0x90, `start` +0x5C0, `stop` +0x5C8, `enable`/`disable` +0x850/+0x858, `setInterfaceEnable` +0x930, `reset` +0xEF8, …), datapath Skywalk (`getRxCompQueue`/`getTxCompQueue`/`getTxSubQueue`/`getTxPacketPool`/`enableDatapath`/`disableDatapath` +0xD70–0xDA0, `flushFlowQueues` +0xED8, `printDataPath` +0xC58, `getLogger` +0xD08, `isChipInterfaceReady` +0xB78, `setPromiscuousModeEnable` +0xA98, `getBSDNamePrefix` +0x9E8, `setMacAddress` +0xF40, `triggerAWDLMulticastTx` +0xF10, `p2pMulticastTx` +0xF18, `sendPendingPacketsToStack` +0xF30, `freePendingPackets` +0xF38) y la superficie de comandos apple80211 AWDL (+0xFC8–0x11C0: `getSSID`, `getCHANNEL`, `getBSSID`, `getSTATE`, `getAWDL_SYNC_PARAMS`, `getAWDL_SYNC_ENABLED`, `getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS`, `getAWDL_ELECTION_ID`, `getAWDL_ELECTION_METRIC`, … — slots que en la vtable de `IO80211AWDLProtocol` eran el stub). Los restantes ~55 diffs siguen el mismo patrón (no listados uno a uno, `?`). **3 slots propios al final**: +0x11C8 = `init(Core*, ether_addr*, unsigned, char*)` (`0x1718db0`), +0x11D0 = `freeResources` (`0x17194c8`, llamada en los caminos de error de `init` en `0xffffff800171917f`/`0xffffff80017192c5` como `call [vptr+0x11D0]`), +0x11D8 = NULL.

Slots clave, las tres vtables lado a lado:

| offset (vptr) | IO80211VirtualInterface | IO80211AWDLProtocol | AppleBCMWLANProximityInterface |
|---|---|---|---|
| +0x088 | `IO80211SkywalkInterface::init()` | ídem | `AppleBCMWLANProximityInterface::init()` (`0x1718d72`) |
| +0xE68 | `IO80211VirtualInterface::init(Controller*, ether_addr*, unsigned, char const*)` (`0x23305f8`) | ídem (heredado) | ídem (heredado) |
| +0x11C8 | — (vtable termina en +0xFC8) | — (termina en +0x11C8) | `AppleBCMWLANProximityInterface::init(Core*, ether_addr*, unsigned, char*)` (`0x1718db0`) |

La `init` especializada de Proxi (la que `withOptions` invoca por el slot +0x11C8 con `a4 = 6`) es un **virtual nuevo de la subclase** (firmas distintas de la `init` de 4 args de `IO80211VirtualInterface`: `AppleBCMWLANCore*` vs `IO80211Controller*`, `char*` vs `char const*`), por eso no pisa el slot +0xE68 y se agrega al final. En los caminos de error de esa init se ve además `mov dword ptr [ivars+0x2B0], 6` (rol 6 guardado en ivars, `0xffffff8001718ec5`) y `setInterfaceId(2)` si la versión del core ≥ 5 (`0xffffff8001718e27`).

Contexto: las otras tres vtables de interfaces virtuales del kext Broadcom tienen el mismo slot +0xE68 apuntando a la `init` de `IO80211VirtualInterface` — `__ZTV33AppleBCMWLANIO80211APSTAInterface` (slot en `0x17f53e0`), `__ZTV24AppleBCMWLANNANInterface` (`0x17f7d00`), `__ZTV28AppleBCMWLANNANDataInterface` (`0x17f9128`) — coherente con los roles 7 y 8–10 del gate de `setVIRTUAL_IF_CREATE`.

## 4) Respuestas

**¿Por qué `AppleBCMWLANProximityInterface::init` despacha por la vtable de `IO80211AWDLProtocol`?** Porque llama a la `init` virtual de su clase base, y el codegen de kext no devirtualiza llamadas virtuales calificadas: emite `mov rax, cs:off_FFFFFF80017DE1C0` (slot de `__data` de Broadcom que contiene el puntero importado a `__ZTV19IO80211AWDLProtocol`) + `call [rax+0xE78]`. Ese slot de la vtable base contiene la `init` heredada de `IO80211VirtualInterface` (`0x23305f8`) porque `IO80211AWDLProtocol` no la sobrescribe. La vtable propia de Proxi (`0x17f5b78`) no interviene en esta llamada por diseño del ABI: una llamada calificada a la base debe resolver por la vtable de la clase nombrada, no por la del objeto. No es reuso casual de tabla: es subclase real llamando a su base a través de la frontera de kext.

**¿Cuál es la relación de herencia real?** `AppleBCMWLANProximityInterface` : `IO80211AWDLProtocol` : `IO80211VirtualInterface` : `IO80211SkywalkInterface` : (`?` por encima). Evidencia triple e independiente: (a) los tres registros de MetaClass con el `gMetaClass` del padre como argumento literal (§1); (b) los imports externos del kext Broadcom a la maquinaria de clase de `IO80211AWDLProtocol` (§2); (c) la estructura de vtables 506 → 569 → 572 con herencia simple, overrides de subclase y 3 slots nuevos al final (§3).

## 5) Método y límites

- Vtables/strings/datos: leídos de `BootKernelExtensions.kc` con mapa de segmentos de `otool -l` (delta por segmento, no regla plana) y decodificación de fixups `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)` revalidada contra `AirLink` en cada pasada.
- Metaclass chain: decompilación rea/IDA de `__GLOBAL__sub_I_AppleBCMWLANProximityInterface.cpp` (`0xffffff800172ace5`), `__ZN19IO80211AWDLProtocol9MetaClassC1Ev` (`0xffffff8002198bc8`), `__ZN23IO80211VirtualInterface9MetaClassC1Ev` (`0xffffff8002330002`) + instrucciones de `AppleBCMWLANProximityInterface::init` (`0xffffff8001718db0`).
- Nombres: `Docs/re/io80211_25.6.0/syms.txt` (IO80211Family/kernel) y `syms_bcm.txt` (Broadcom + externs).
- `?` explícitos: identidad del stub `0xffffff8000bb59c0` (sin símbolo en syms.txt) y de los 23 slots finales de AWDL no impresos; cadena de herencia por encima de `IO80211SkywalkInterface`; ortografía exacta en fuente de la llamada base (solo se observa el codegen); instancias `gMetaClass` en disco en cero (se rellenan en load).
- Los overrides de Proxi se listaron por muestreo (95 en total); los ~55 no listados siguen el patrón documentado (`?` sobre el detalle individual).
