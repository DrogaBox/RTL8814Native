# Chequeos pequeños de etapa 1: retorno de `IO80211VirtualInterface::init` y `req` de pila de la ruta legacy del 94

Cierra los puntos 7 y 8 del §8.2 de `AWDL_INDICE.md`: el tipo de retorno real de
`IO80211VirtualInterface::init` (el `?` #82 de `AWDL_STAGE1_SPEC_DELTA.md`, defecto 3 de
`AWDL_STAGE1_CODE_FIXES.md`) y la inicialización del `req` de pila en `setVirtualInterfaceCreate`
(hallazgo A10 de `AWDL_STAGE1_CODE_REVIEW_HOOK.md`). Base: kernelcache de macOS 26.7.1
(25G241, x86_64); lecturas por IDA headless (`idat -A -S`) sobre una copia de `boot_awdl.kc.i64`.
Los offsets de vtable son relativos al vptr salvo indicación `ZTV+` (celda en el ZTV crudo,
`ZTV+X` = `vptr+(X−0x10)`).

Comprobaciones ejecutadas para este documento, en orden:

| Sondeo | Resultado |
|---|---|
| Cuerpo completo de `IO80211VirtualInterface::init` (`0xffffff80023305f8`), de `setVirtualInterfaceCreate` (`0xffffff8002357f33`) y del handler moderno del 94 (`0xffffff80022886a8`), desensamblado + pseudocódigo | §1.1, §2.1 |
| Nombres con la firma `4initEP17IO80211ControllerP10ether_addr` y referencias de código directas al `init` base | 0 llamadores directos en todo el KC; sólo el símbolo base y dos datos `kalloc_type_view` |
| Celdas que apuntan a `init` (9) y propietario de cada una; contenido de `0xffffff80017DE1C0` | §1.2 |
| Referencias a los dos `init` de `AppleBCMWLANProximityInterface` y sitios de super-llamada por la celda del ZTV de `IO80211AWDLProtocol` | §1.3 |
| Cuerpo de `AppleBCMWLANProximityInterface::withOptions` | §1.3 |

## 1. Punto #7: tipo de retorno de `IO80211VirtualInterface::init`

### 1.1 El cuerpo: sólo el byte bajo queda definido

Firma por el símbolo: `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc`,
cabeza `0xffffff80023305f8`, función `0xffffff80023305f8`–`0xffffff8002330804`. Todas las salidas
convergen en el epílogo `0xffffff800233079b` (cookie de pila) y retornan por
`0xffffff80023307ab`.

| VA | Instrucción / línea | Hecho |
|---|---|---|
| `0xffffff8002330608` | `mov r14, rsi` | r14 = 2º argumento (el `IO80211Controller*` según el símbolo); no se reescribe hasta `0xffffff8002330740` (sólo se lee en `0xffffff8002330690` y `0xffffff80023306a1`) |
| `0xffffff8002330740` | `mov r14b, 1` | rama de éxito: se escribe **sólo el byte bajo**; los bits 8-31 de r14d conservan el residuo del puntero del controlador |
| `0xffffff8002330743`–`0xffffff8002330746` | `test rax, rax` ; `jnz 0xffffff800233079b` | el éxito exige `IO80211FlowQueueDatabase::initialize` no nulo (guardado en ivars+0x200 en `0xffffff8002330739`) |
| `0xffffff8002330767`, `0xffffff8002330787`, `0xffffff8002330798`, `0xffffff80023307c7`, `0xffffff80023307df`, `0xffffff80023307f0` | `xor r14d, r14d` | las seis ramas de fallo (base de colas de flujo, `super::init`, ivars de `IOMallocTypeImpl`, `_bpfLock`, `_bpfListHead`, diccionario de procesos denegados) dejan el valor a 0 |
| `0xffffff80023307ab` | `mov eax, r14d` | retorno: en éxito eax = bajo-32 del puntero del controlador con el byte bajo a 1; en fallo eax = 0; los bits 32-63 de rax quedan a cero por la escritura de 32 bits |

El pseudocódigo reproduce lo mismo: `LOBYTE(v6) = 1;` es la única escritura de la rama de éxito,
cada fallo hace `LODWORD(v6) = 0;`, y la función termina en `return (unsigned int)v6;` con v6
inicializado al 2º argumento — el descompilador muestra el residuo del puntero en los bits altos.

Consecuencia de código: bajo un contrato de retorno de 4 u 8 bytes el compilador tendría que dejar
eax (o rax) definido en la rama de éxito; dejar ahí el residuo del puntero del controlador sólo es
válido si el tipo declarado en la propia familia es de **1 byte**. El binario garantiza
exactamente `al`: 1 en éxito, 0 en fallo. Un lector de 64 bits dependería de bits no especificados
(en fallo, un residuo no nulo en bits 32-63 daría un falso éxito); el contrato exigible es leer el
byte.

### 1.2 El slot virtual: celda `vptr+0xE68`, sin ningún override en el KC

`__ZTV23IO80211VirtualInterface` está en `0xffffff8002472b78` y la celda `ZTV+0xE78`
(= `vptr+0xE68`) contiene `0xffffff80023305f8`, el `init` de 4 argumentos. Las celdas vecinas
cierran el alineamiento: `ZTV+0xE70` = `IO80211SkywalkInterface::syncDPSStats`
(`0xffffff80022fa22e`) y `ZTV+0xE80` = `IO80211VirtualInterface::setLinkState`
(`0xffffff8002333408`).

Nueve vtables llevan la misma celda apuntando a la implementación base:

| ZTV | Celda | Contenido |
|---|---|---|
| `__ZTV23IO80211VirtualInterface` | `0xffffff80024739f0` | `0xffffff80023305f8` |
| `__ZTV19IO80211AWDLProtocol` | `0xffffff80024572b8` | `0xffffff80023305f8` |
| `__ZTV18IO80211SapProtocol` | `0xffffff800246f1e8` | `0xffffff80023305f8` |
| `__ZTV22IO80211NANDataProtocol` | `0xffffff8002477dc0` | `0xffffff80023305f8` |
| `__ZTV18IO80211NANProtocol` | `0xffffff800247ad30` | `0xffffff80023305f8` |
| `__ZTV33AppleBCMWLANIO80211APSTAInterface` | `0xffffff80017f53e0` | `0xffffff80023305f8` |
| `__ZTV30AppleBCMWLANProximityInterface` | `0xffffff80017f69e0` | `0xffffff80023305f8` |
| `__ZTV24AppleBCMWLANNANInterface` | `0xffffff80017f7d00` | `0xffffff80023305f8` |
| `__ZTV28AppleBCMWLANNANDataInterface` | `0xffffff80017f9128` | `0xffffff80023305f8` |

Ninguna la sobreescribe: el `init` de 4 argumentos es un **método virtual** de
`IO80211VirtualInterface` en el slot `0xE68` y no existe override suyo en todo el kernelcache; toda
instancia llega a la implementación base. El slot coincide con el que la cabecera local declara
(`AWDLInterface.h:149`, `virtual bool init(...) /* 0xE68 */`), coherente con el chequeo de orden de
vtables ya cerrado sin discrepancias.

### 1.3 Los consumidores: todos leen `al`

Cadena de nacimiento de la interfaz Proximity, con el consumo del retorno en cada nivel:

| Nivel | Direcciones | Consumo |
|---|---|---|
| `AppleBCMWLANProximityInterface::withOptions` (`0xffffff80017159e6`) | asigna con `OSObject_typed_operator_new` (`0xffffff8001715a09`), fija el vptr (`0xffffff8001715a23`–`0xffffff8001715a2a`), despacha `call qword ptr [rax+11C8h]` (`0xffffff8001715a49`, el init virtual propio de Proximity, celda `0xffffff80017f6d40`; el rol 6 viaja en ecx, `0xffffff8001715a41`) | `test al, al` en `0xffffff8001715a4f`; en fallo `call qword ptr [rax+28h]` (`0xffffff8001715a59`) y devolución de nulo |
| `AppleBCMWLANProximityInterface::init(AppleBCMWLANCore *,ether_addr *,uint,char *)` (`0xffffff8001718db0`) | `mov rax, cs:off_FFFFFF80017DE1C0` (`0xffffff8001718deb`; su contenido es `0xffffff8002456440`, `vtable for'IO80211AWDLProtocol`) y `call qword ptr [rax+0E78h]` (`0xffffff8001718e01`, la celda `__ZTV19IO80211AWDLProtocol+0xE78` = `init`) | `test al, al` en `0xffffff8001718e07`; `jz 0xffffff8001719185` → `xor r14d, r14d` y retorno 0 |
| `IO80211VirtualInterface::init` (`0xffffff80023305f8`) | `call IO80211SkywalkInterface::init` en `0xffffff800233061c` | `test al, al` en `0xffffff8002330621`, `jz` a la rama de fallo |

El propio init de Proximity devuelve con el mismo patrón de byte: `setnz r14b`
(`0xffffff8001719236`) y `mov eax, r14d` (`0xffffff80017192df`). El pseudocódigo tipa los tres
niveles como booleanos (cast `unsigned __int8` en las dos llamadas por celda y `bool` en las
firmas recuperadas).

### 1.4 Conclusión

El tipo de retorno real de `IO80211VirtualInterface::init(IO80211Controller *, ether_addr *,
uint32_t, const char *)` es **`bool` de 1 byte**: el cuerpo sólo define `al` (1 en éxito, 0 en
fallo; `0xffffff8002330740`, `0xffffff80023307ab`), los tres consumidores observados lo leen con
`test al, al`, y la celda virtual `0xE68` apunta a esta implementación en las nueve vtables del KC
sin override. El código de etapa 1 ya está conforme: `rtl_vif_init` declarado `bool`
(`AWDLInterface.cpp:47`) y comprobado (`AWDLInterface.cpp:114`), cabecera con el slot
(`AWDLInterface.h:149`) — el defecto 3 de `AWDL_STAGE1_CODE_FIXES.md` queda validado por el
binario, y con él se cierra el `?` #82 de `AWDL_STAGE1_SPEC_DELTA.md`.

`?` Los creadores que despachan la celda `0xE68` por vptr fuera de la cadena Proximity/withOptions
(APSTA, NAN, NANData, Sap, NANProtocol) no se han barrido; es irrelevante para el tipo, porque la
implementación es única en las nueve vtables.

## 2. Punto #8: el `req` de pila de la ruta legacy del 94

### 2.1 La función y su buffer

La entrada 93 de la tabla legacy es `setVirtualInterfaceCreate`:
`_gSetHandlerTableLegacy` en `0xffffff8002479060`, entrada [93] en `0xffffff8002479348`, contenido
`0xffffff8002357f33` (`dq offset __ZL25setVirtualInterfaceCreateP17IO80211ControllerP23IO80211SkywalkInterfaceP13apple80211reqb`),
función `0xffffff8002357f33`–`0xffffff8002358034`.

| VA | Instrucción / línea | Hecho |
|---|---|---|
| `0xffffff8002357f52`–`0xffffff8002357f60` | `xor eax, eax` + 4 × `mov [rbp+var_38/var_40/var_48/var_50], rax` | los 32 bytes del req de pila (`_QWORD v12[4] // [rsp+10h] [rbp-50h]` en el pseudocódigo, que lo muestra como `memset(v12, 0, sizeof(v12));`) se ponen a cero **antes** de copiar nada |
| `0xffffff8002357f64`, `0xffffff8002357f6e` | `mov ecx, [rdx+18h]` ; `jz` → retorno 22 | len del req externo; len 0 → 22 sin tocar nada |
| `0xffffff8002357f7a`, `0xffffff8002357f81` | `mov rsi, [rdx+20h]` ; `jz` → 22 | puntero del payload; nulo → 22 |
| `0xffffff8002357f8a`–`0xffffff8002357f93` | `cmp ecx, 20h` ; `mov r12d, 20h` ; `cmovb r12d, ecx` | n = min(len, 32) |
| `0xffffff8002357f97`–`0xffffff8002357f9e` | `lea rdx, [rbp+var_50]` ; `mov rcx, r12` ; `call IO80211Controller::copyIn` | copia n bytes del payload apuntado por req+0x20 al req de pila |
| `0xffffff8002357fc7`, `0xffffff8002357fda` | `mov r8d, 20h` ; `call IO80211Glue::sendIOUCToWcl(..., 802869C8h, 5Eh, &v12, 20h, &consumed)` | la sonda WCL recibe siempre longitud 0x20, con la marca de consumo a 0 (`0xffffff8002357fbe`) y probada en `0xffffff8002357fdf` |
| `0xffffff8002357fed`–`0xffffff8002357ff4` | `lea rsi, [rbp+var_50]` ; `call apple80211setVIRTUAL_IF_CREATE` | el puente recibe (interfaz, puntero al req de pila) — **sin argumento de longitud** |
| `0xffffff80022886c8`–`0xffffff80022886cc` | `cmp dword ptr [rcx+18h], 20h` ; `jnz` → 22 | la exigencia len == 32 vive en el handler moderno `setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`), que la ruta legacy **no** atraviesa: el único filtro propio de la legacy es len ≠ 0 |
| `0xffffff8002357ffd`–`0xffffff800235800b` | `mov rdx, [r14+20h]` ; `mov rcx, r12` ; `call IO80211Controller::copyOut` | copia de vuelta al emisor de la misma n |

### 2.2 Conclusión

El «req de pila» es el payload de 32 bytes (`apple80211_virt_if_create_data`, layout de
`AWDL_STRUCTS.md` §3.2) y **nace íntegramente a cero**: la inicialización cubre los 32 bytes
(`0xffffff8002357f52`–`0xffffff8002357f60`) y precede a la copia. Las únicas longitudes en juego
son n = min(len, 32) para `copyIn`/`copyOut` y 0x20 para la sonda WCL; el buffer no lleva campo de
longitud propio.

Con `len < 32`, los bytes que la copia no rellena permanecen a cero: `role` (u32 en +8) e `ifname`
(char[16] en +0x10) leen valores deterministas — bytes del emisor hasta len y ceros desde ahí —,
nunca memoria sin inicializar. La premisa de A10 se confirma (la legacy acepta len < 32 y llega al
puente sin el check de 0x20, `0xffffff8002357ff4`), pero la sospecha de bytes no inicializados se
desvanece: la puesta a cero es la defensa de la familia. La gravedad de A10 baja de
«comportamiento incorrecto (sólo emisor incompleto)» a inofensiva para el driver: un rol truncado
no será 6 y el override de etapa 1 lo rechaza por rol (`AWDL_STAGE1_SPEC_DELTA.md` #28), y un
nombre vacío no publica nombre. El límite de contrato que A10 registra se mantiene: el puente no
recibe longitud, así que el override no puede detectar la truncación.

`?` Qué emisores reales usan `len < 32` no se ha barrido; el daemon envía 32
(`AWDL_STRUCTS.md` §3.2, citado por A10), con lo que el caso queda en emisor hipotético.

## 3. Puntos abiertos tras este documento

| # | Punto | Estado |
|---:|---|---|
| 7 | Tipo de retorno de `IO80211VirtualInterface::init` (`AWDL_INDICE.md` §8.2; #82 de SPEC_DELTA) | **Cerrado**: `bool` de 1 byte (§1.4); código ya conforme (defecto 3 aplicado). `?` residuo: creadores por vptr fuera de la cadena Proximity no barridos, sin efecto sobre el tipo |
| 8 | Inicialización del `req` de pila legacy con `len < 32` (A10) | **Cerrado**: 32 bytes a cero antes del `copyIn`; n = min(len, 32); el puente no recibe longitud (§2.2). A10 baja a inofensivo. `?` residuo: emisores reales con `len < 32` (el daemon envía 32) |
