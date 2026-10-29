# Enlace de los `output_handler` del `ifnet` en 26.7.1

Ámbito: los símbolos `ifnet_set_output_handler`, `ifnet_reset_output_handler`, `dlil_set_output_handler` y
`dlil_reset_output_handler`, que el diseño de la etapa 2 usaba para saltar el `ifclassq` de `awdl0`. Se
responden tres preguntas: si están exportados a kexts, qué alternativa existe sin símbolo y cómo se detecta
su ausencia en tiempo de carga. Todo se ha leído sobre copias de la base de datos y sobre el propio kernel
collection; no se ha enlazado, cargado ni ejecutado nada.

## 1. Respuesta

| Pregunta | Respuesta |
|---|---|
| ¿Están exportados a kexts en 26.7.1? | **No.** Los cuatro existen en la tabla de símbolos del kernel como globales definidos (tipo `0x0f`), pero **ninguno** figura en las nueve listas KPI de `__LINKINFO,__symbolsets`; ningún kext del collection los importa ni los define, y no tienen ninguna referencia de código. |
| ¿Qué entrada de `OSBundleLibraries` los aporta? | **Ninguna.** `com.apple.kpi.bsd`, `com.apple.kpi.private` y `com.apple.kpi.unsupported` no los contienen. Añadir `private` aportaría, entre otros, `_ifnet_allocate_extended` y `_ifnet_enqueue`, pero no los cuatro. |
| ¿Puede el kext instalar su propio `output` sin ellos? | **Sí**, por dos métodos virtuales ya declarados en la cabecera: `initBSDInterfaceParameters` (slot 284, llamada `[vptr+0x8E0]`) para poner el campo `+0x40` del `ifnet_init_eparams`, o `prepareBSDInterface` (slot 285, llamada `[vptr+0x8E8]`) para escribir `ifp+0x258` con `cmpxchg`. El campo `+0x38` no sirve: el alta lo sustituye. |
| ¿Se puede detectar la ausencia en carga? | No hay API exportada: `kmod_find_symbol` no existe, y `_kext_request`, `_kmod_get_info` y `OSKext::lookupKextWithIdentifier` existen pero no están en ninguna lista KPI. Sólo quedan la importación débil con el símbolo de reserva `_gOSKextUnresolved` o el gate por build que ya usa `VersionGate.h`. |

La consecuencia inmediata es que el paso 1 del diseño de la etapa 2 (`ifnet_set_output_handler` en
`initWithController`) no enlaza en esta versión. El paso debe sustituirse por una escritura propia en
`ifp+0x258` o por el campo `+0x40` de los parámetros, ambas dentro de un método virtual que la familia ya
invoca.

## 2. Tablas de exportación del kernel collection

### 2.1 Qué tabla decide el enlace

| Tabla | Localización | Contenido | ¿Decide el enlace de un kext? |
|---|---|---|---|
| Símbolos del kernel | `com.apple.kernel`, `LC_SYMTAB` | 23.790 entradas; las definidas externas son de tipo `0x0f` | No por sí sola |
| Listas KPI | `__LINKINFO,__symbolsets`, VA `0xFFFFFF8003400000`, fichero `0x3300000`, `0x4D6D4` bytes | plist binario con `SymbolsSets` y `WeakRefFallbackSymbol` | **Sí**: es la tabla de exportación que resuelve `OSBundleLibraries` |
| Importaciones de los kexts | `LC_SYMTAB` de cada fileset | 88.653 entradas `N_UNDF`; 9 débiles | — |
| Definiciones de los kexts | `LC_SYMTAB` de cada fileset | 204 filesets | — |

El `LC_SYMTAB` del kernel no es la tabla que usa el cargador para un kext: las listas KPI del plist son
las que llevan `CFBundleIdentifier` y la lista `Symbols` de cada colección. Un símbolo puede estar en la
tabla del kernel y no ser enlazable desde un kext, que es exactamente lo que ocurre con los cuatro. En
este collection los 204 filesets llevan `LC_SYMTAB` y ninguno lleva `LC_DYLD_INFO_ONLY` ni
`LC_DYLD_CHAINED_FIXUPS`: la importación de un kext queda como símbolo `N_UNDF` en esa tabla, que es donde
se ha buscado, no en un trie de exportación ni en una cadena de fixups.

### 2.2 Los cuatro símbolos

| Símbolo | VA | Tipo | Sección | ¿En alguna lista KPI? | ¿Importado por algún kext? | ¿Definido por algún kext? |
|---|---|---|---|---|---|---|
| `_ifnet_set_output_handler` | `0xFFFFFF80005EF540` | `0x0f` | 1 | No | No | No |
| `_ifnet_reset_output_handler` | `0xFFFFFF80005EF560` | `0x0f` | 1 | No | No | No |
| `_dlil_set_output_handler` | `0xFFFFFF80005EF4E0` | `0x0f` | 1 | No | No | No |
| `_dlil_reset_output_handler` | `0xFFFFFF80005EF510` | `0x0f` | 1 | No | No | No |

El barrido de importaciones recorre los 204 filesets y sus 88.653 símbolos `N_UNDF`; ninguno de los cuatro
aparece. El de definiciones recorre las entradas `0x0f` de todos los filesets; sólo `com.apple.kernel` los
define. Las cuatro funciones tampoco tienen referencias de código en todo el collection: no las llama ni
el propio kernel.

### 2.3 Las nueve listas KPI

| Lista | Símbolos | ¿Alguno de los cuatro? |
|---|---|---|
| `com.apple.kpi.bsd` | 876 | No |
| `com.apple.kpi.iokit` | 2.528 | No |
| `com.apple.kpi.libkern` | 1.010 | No |
| `com.apple.kpi.mach` | 77 | No |
| `com.apple.kpi.dsep` | 12 | No |
| `com.apple.kpi.unsupported` | 255 | No |
| `com.apple.kpi.private` | 1.739 | No |
| `com.apple.kpi.kasan` | 0 | No |
| `com.apple.kpi.kcov` | 0 | No |

La comprobación se ha hecho sobre los nombres exactos con guion bajo inicial, que es como los guarda el
plist. No hay ninguna entrada con `output_handler` en ninguna de las nueve listas.

### 2.4 Símbolos vecinos que sí se pueden enlazar

| Símbolo | Lista | Uso en esta ruta |
|---|---|---|
| `_ifnet_find_by_name` | `com.apple.kpi.bsd` | Obtener el `ifnet` por nombre, si no se tiene el puntero del hook. |
| `_ifnet_set_mtu`, `_ifnet_set_eflags` | `com.apple.kpi.bsd` | Los usa la propia familia en `prepareBSDInterface`. |
| `_bpfattach` | `com.apple.kpi.bsd` | Lo llama `gatedPrepareNexus` en `0x2B006CA`. |
| `_ifnet_allocate_extended`, `_ifnet_enqueue` | `com.apple.kpi.private` | Sólo si se construyera un `ifnet` propio; no es el caso. |
| `_gOSKextUnresolved` | `com.apple.kpi.libkern` | Símbolo de reserva para referencias débiles. |

El `Info.plist` del kext declara `com.apple.kpi.bsd`, `iokit`, `libkern` y `mach`; no declara `private`.
Ninguna de esas dependencias aporta los cuatro `output_handler`.

## 3. Semántica de las cuatro funciones

| Función | Instrucciones | Efecto |
|---|---|---|
| `ifnet_set_output_handler` (`0xFFFFFF80005EF540`) | `0x5EF544` `mov rax,[rdi+5A0h]`; `0x5EF54D` `lock cmpxchg [rdi+258h], rsi`; `0x5EF556` `setnz` | Instala el puntero sólo si `[ifp+0x258]` sigue siendo el valor guardado en `[ifp+0x5A0]`; si no, devuelve `0x10`. |
| `ifnet_reset_output_handler` (`0xFFFFFF80005EF560`) | `0x5EF570`–`0x5EF57E`: lee `[ifp+0x258]`, carga `[ifp+0x5A0]` y repite el `cmpxchg` | Restaura el valor guardado; devuelve el valor anterior. |
| `dlil_set_output_handler` (`0xFFFFFF80005EF4E0`) | `0x5EF4E4` `lea rax,_dlil_output_handler`; `0x5EF4ED` `lock cmpxchg [rdi+248h], rsi` | Sustituye `ifp+0x248` (el `if_output` de entrada de la pila), no `ifp+0x258`; sólo si sigue siendo `_dlil_output_handler`. |
| `dlil_reset_output_handler` (`0xFFFFFF80005EF510`) | `0x5EF520`–`0x5EF527` | Restaura `ifp+0x248` a `_dlil_output_handler`. |

El camino normal de la pila es `_dlil_output_handler` (`0xFFFFFF80005EF500`): en `0x5EF504` carga
`[ifp+0x258]` y en `0x5EF50C` salta a él. Por eso `ifnet_set_output_handler` cambia el destino del salto sin
tocar el `if_output` de `+0x248`; `dlil_set_output_handler` cambia la entrada misma. Las dos vías evitan el
`ifclassq`. La primera es la que se puede reproducir con una escritura directa en `ifp+0x258`.

Los cuatro símbolos no tienen ninguna referencia de código en el collection: no se usan dentro del propio
kernel y ninguna lista KPI los expone en esta versión.

## 4. `ifnet_allocate_extended`: por qué `params+0x38` no sobrevive

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF800063F49E`–`0x63F4A2` | Carga `[params+0x38]` en `[rbp+var_78]`. |
| `0xFFFFFF800063F4A6`–`0x63F4AA` | Carga `[params+0x40]` en `[rbp+var_A8]`. |
| `0xFFFFFF800063F4B1`–`0x63F4B5` | Carga `[params+0x48]` en `[rbp+var_60]`. |
| `0xFFFFFF800063F752` | `lea r15,_ifp_if_start`; los bytes `4c 8d 3d 87 45 fa ff` dan `0xFFFFFF80005E3CE0`, no la dirección que muestra el desensamblador en esa línea. |
| `0xFFFFFF800063F882` | `[ifp+0x258] = [params+0x38]`. |
| `0xFFFFFF800063F890` | `[ifp+0x260] = [params+0x40]`. |
| `0xFFFFFF800063F89D` | `[ifp+0x250] = r15` (`_ifp_if_start`). |
| `0xFFFFFF800063FC00` | `cmp qword [ifp+0x250],0`; si es cero, no entra en el bloque. |
| `0xFFFFFF800063FC0A` | `lock or dword [ifp+0xC0],0x80`: pone `IFEF_TXSTART`. |
| `0xFFFFFF800063FC16`–`0x63FC30` | `rax = [ifp+0x260]`; si es cero, `rax = _ifnet_enqueue`; `[ifp+0x258] = rax`. |
| `0xFFFFFF800063FDA5` | `[ifp+0x5A0] = [ifp+0x258]`: guarda el valor por defecto para el `reset`. |
| `0xFFFFFF800063FCF3` | `[ifp+0x598] = r14`: guarda el `if_start` por defecto. |
| `0xFFFFFF80009A41F4` | El nexus rechaza el alta si `[params+0x48] != 0`. |
| `0xFFFFFF80009A420D` | El nexus llama a `ifnet_allocate_extended` con esos parámetros. |

Como el nexus exige `params+0x48 == 0`, el alta instala `_ifp_if_start` en `ifp+0x250`; el puntero no es
nulo, de modo que `0x63FC00` entra en el bloque, pone `IFEF_TXSTART` y sustituye `ifp+0x258` por
`[ifp+0x260]` o por `_ifnet_enqueue`. El campo `+0x38`, que
`IOSkywalkNetworkBSDClient::start` rellena con `IOSkywalkNetworkBSDClient::if_output`, no sobrevive.

El valor que queda en `ifp+0x258` con los parámetros actuales es `_ifnet_enqueue` (`0xFFFFFF80005E5ED0`),
porque `params+0x40` está a cero. `ifp+0x5A0` guarda ese mismo valor y `ifp+0x598` guarda `_ifp_if_start`.
Ese es el estado sobre el que trabajaría la etapa 2.

## 5. Los métodos virtuales que sí se pueden usar

### 5.1 `start` y `initBSDInterfaceParameters`

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF8002AFFA18`–`0x2AFFA1B` | `bzero(params+8, 0x150)`: deja a cero, entre otros, `+0x40` y `+0x48`. |
| `0xFFFFFF8002AFFA4D` | `[params+0x38] = IOSkywalkNetworkBSDClient::if_output` (`0x2B002CE`). |
| `0xFFFFFF8002AFFADC` | `call [vptr+0x8E0]`: `initBSDInterfaceParameters(params, &lladdr)`. |
| `0xFFFFFF8002AFFBB5` | `kern_nexus_controller_alloc_net_provider_instance(...)`, con `params` en el bloque de inicialización. |
| `0xFFFFFF8002B10BCA` | `IOSkywalkEthernetInterface::initBSDInterfaceParameters`: valida `version == 2` (`0x2B10BCF`) y `len == 0x158` (`0x2B10BE8`); si no, devuelve `0xE00002C2`. |
| `0xFFFFFF8002B10C15`–`0x2B10D0A` | Escribe MAC (`+0x10`/`+0x18`), dirección de broadcast (`+0x128`/`+0x130`), `ether_demux` (`+0xD8`), `ether_add_proto` (`+0xE0`), `ether_del_proto` (`+0xE8`), `ether_check_multi` (`+0xF0`), `ether_frameout` (`+0xF8`) y los campos `+0x2C`/`+0x30`. No toca `+0x38` ni `+0x40`. |
| `0xFFFFFF80022F635C` | `IO80211SkywalkInterface::initBSDInterfaceParameters`: llama a la implementación base (`0x22F6373`, `[vtable+0x8F0]`) y, si `[this+0x120]->[+0x58]` vale 1 o 2 (`0x22F637F`–`0x22F638E`), pone `[params+0xB] |= 0x40` (`0x22F6390`). |

La implementación base no escribe los dos campos que interesan; la de IO80211 tampoco. Un `override` en la
clase del AWDL puede llamar a la implementación heredada y, después, escribir el campo `+0x40` del
`ifnet_init_eparams`. Con ese valor no nulo, `ifnet_allocate_extended` lo copia a `ifp+0x260` y luego a
`ifp+0x258` en `0x63FC30`, que es exactamente el destino que se busca.

### 5.2 `prepareBSDInterface`

| Instrucción | Hecho |
|---|---|
| `0xFFFFFF8002B0066C` | `IOSkywalkNetworkBSDClient::gatedPrepareNexus(ifnet)`. |
| `0xFFFFFF8002B00688` | `call [vptr+0x8E8]` con `(ifnet, 0)`: `prepareBSDInterface`. |
| `0xFFFFFF8002B006A6` | Guarda el `ifnet` en `[this+0xF8]`; el `bpfattach` viene después (`0x2B006CA`). |
| `0xFFFFFF8002331ADA` | `IO80211VirtualInterface::prepareBSDInterface`: llama a la base (`0x2331AE4`), y si el `attach` a bpf falla, registra el error. |
| `0xFFFFFF80022F63B6` | `IO80211SkywalkInterface::prepareBSDInterface`: nombre BSD, MTU 0x5DC para el tipo 2, `ifnet_set_eflags`. |

El hook recibe el `ifnet` ya creado, dentro del alta del proveedor y antes de que la interfaz se publique.
Es el punto natural para escribir `ifp+0x258` con el valor propio y guardar el anterior para restaurarlo en
`stop`. La llamada es virtual sobre el objeto de interfaz, que es el mismo que se registró en la etapa 1;
la cabecera ya declara el método como virtual (`Source/IO80211/IO80211_25_6_0.h:289`, `:351` y `:403`).

### 5.3 Diseño para la etapa 2

| Opción | Dónde se instala | Ventaja | Riesgo |
|---|---|---|---|
| A. `initBSDInterfaceParameters` | Campo `+0x40` de los parámetros, antes del alta | La base valida `version`/`len`; si cambia la versión o el tamaño, devuelve error antes de escribir | Depende del desplazamiento `+0x40`, de que `if_start` siga siendo no nulo y de que el layout no cambie con la misma versión/tamaño (`?`) |
| B. `prepareBSDInterface` | `ifp+0x258`, después del alta y antes de publicar | Acceso directo al `ifnet`; no depende del `ifnet_init_eparams` | Depende de `+0x258`/`+0x5A0`; sin validación de diseño |
| C. Importación fuerte de los símbolos | — | Es la forma documentada | No enlaza en 26.7.1: no están en KPI |
| D. Importación débil | — | El kext carga aunque falte | En 26.7.1 el puntero cae en la función de pánico, no en la función real |

La opción B reproduce lo que hace `ifnet_set_output_handler` sin llamarlo:

```
uintptr_t old = *(uintptr_t *)((char *)ifp + 0x5A0);
if (__sync_val_compare_and_swap((uintptr_t *)((char *)ifp + 0x258), old, (uintptr_t)rtlAwdOutput) != old)
        return 0x10;
```

y la restauración es `*(uintptr_t *)((char *)ifp + 0x258) = old`. El `cmpxchg` contra `[ifp+0x5A0]` es
el mismo que usa la función original en `0x5EF54D`; si el campo ya cambió, se devuelve `0x10` y no se
instala nada.

La opción A deja el mismo efecto al alta: `[params+0x40] = rtlAwdOutput` y el `ifnet` nace con ese
`output`, sin necesidad de escribir el `ifnet` a mano. Es preferible si se quiere que el `ifnet` ya esté
completo cuando lo vea el cliente BSD; la B es preferible si se quiere evitar depender del layout de
`ifnet_init_eparams`.

## 6. Detección en tiempo de carga

| Vía | ¿Existe en 26.7.1? | ¿Está en alguna lista KPI? | Veredicto |
|---|---|---|---|
| `kmod_find_symbol` | No aparece ni en la tabla de símbolos ni en las cadenas | — | No usable |
| `_kmod_get_info` | Sí, `0xFFFFFF8000489850` | No | No usable |
| `_kext_request` | Sí, `0xFFFFFF8000A516A0` | No | No usable |
| `OSKext::lookupKextWithIdentifier` | Sí, `0xFFFFFF8000A39FB0` | No | No usable |
| `_OSKextLookupSymbol` | No | — | No usable |
| `_gOSKextUnresolved` | Sí, dato en `0xFFFFFF800025CD78` | Sí, `com.apple.kpi.libkern` | Sólo como reserva de referencias débiles |

No hay ninguna función exportada, entre las API de `OSKext` y `kmod`, que permita a un kext preguntar por
otro símbolo. `kext_request` es la interfaz de peticiones al daemon de kexts, no un buscador de símbolos,
y tampoco está exportada.

La vía que sí existe es la importación débil. El plist de `__LINKINFO,__symbolsets` declara
`WeakRefFallbackSymbol = _gOSKextUnresolved`; el dato de `0xFFFFFF800025CD78` vale
`0xFFFFFF8000BB5F1A`, que es `_kext_weak_symbol_referenced`, y esa función ejecuta el pánico
«A kext referenced an unresolved weak symbol @%s:%d» (cadena en `0xFFFFFF8000CD26A5`). El collection tiene
9 importaciones débiles (`N_UNDF` con `n_desc & 0x40`), de modo que el cargador sí sabe resolverlas. Una
referencia débil a uno de los cuatro no quedaría a cero: según el símbolo de reserva del plist, quedaría
apuntando a la función de pánico, así que la comprobación en ejecución debe comparar el puntero con ese
valor antes de llamarlo (`?` si el cargador escribe la dirección del dato o su valor).

En 26.7.1 los cuatro no están exportados, de modo que la importación débil no sirve para llamarlos: el
gate tiene que elegir la vía sin símbolo. Para las versiones Sonoma–26 el gate por build es el que ya
existe en `Source/AirLayer/VersionGate.h` (`version_major`, `osversion` y tamaños de `OSMetaClass`); no
necesita ninguna búsqueda de símbolos. La importación débil sólo tiene sentido como red de seguridad para
que el kext cargue en un build donde el símbolo falte, no como mecanismo de llamada.

## 7. Riesgos y cuestiones abiertas

| Riesgo | Probabilidad | Consecuencia | Mitigación |
|---|---|---|---|
| Desplazamiento `ifp+0x258`/`ifp+0x5A0` distinto en otro build | Alta | Corrupción del `ifnet` | Tabla de desplazamientos por build en el gate; sólo 26.7.1 está medido |
| Desplazamiento `params+0x40` distinto en otro build | Alta | El `output` no se instala o se escribe otro campo | La base valida `version`/`len`; aun así, tabla por build |
| El callback del nexus no se ejecuta antes del primer tráfico | Baja | El primer paquete toma el `ifclassq` | El hook corre dentro del alta, antes de publicar; `?` el detalle interno de `nx_create` |
| Otro componente reemplaza el `output` después | Baja | El paquete vuelve al `ifclassq` | No hay referencias a los cuatro símbolos en el collection; `?` en builds futuros |
| La importación débil se enlaza a la dirección del dato y no a su valor | Media | La comprobación de disponibilidad falla | `?` el mecanismo exacto; no usar la importación débil para llamar en 26.7.1 |
| El `awdl0` de un Wi-Fi Apple sigue otra ruta | Baja | El diseño no aplica a esa interfaz | La interfaz del AWDL usa el mismo `IOSkywalkNetworkBSDClient::start`; `?` la de Apple |

## 8. Verificación de direcciones

Cabezas comprobadas (la dirección citada es el inicio de la función):
`0xFFFFFF80005EF4E0`, `0xFFFFFF80005EF510`, `0xFFFFFF80005EF540`, `0xFFFFFF80005EF560`,
`0xFFFFFF80005EF500`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005E5ED0`, `0xFFFFFF800063F3F0`,
`0xFFFFFF80009A4140`, `0xFFFFFF8002AFF562`, `0xFFFFFF8002B002CE`, `0xFFFFFF8002B10BCA`,
`0xFFFFFF80022F635C`, `0xFFFFFF8002331ADA`, `0xFFFFFF80022F63B6`, `0xFFFFFF8002B0066C`,
`0xFFFFFF8002B00340`, `0xFFFFFF8000BB5F1A`, `0xFFFFFF8000A516A0`, `0xFFFFFF8000A39FB0`,
`0xFFFFFF8000489850`.

Citas de instrucción dentro de una cabeza verificada: `0xFFFFFF80005EF544`, `0xFFFFFF80005EF54D`,
`0xFFFFFF80005EF556`, `0xFFFFFF80005EF570`–`0xFFFFFF80005EF57E`, `0xFFFFFF80005EF4E4`,
`0xFFFFFF80005EF4ED`, `0xFFFFFF80005EF520`–`0xFFFFFF80005EF527`, `0xFFFFFF80005EF504`,
`0xFFFFFF80005EF50C`, `0xFFFFFF800063F49E`–`0xFFFFFF800063F4B5`, `0xFFFFFF800063F752`,
`0xFFFFFF800063F882`, `0xFFFFFF800063F890`, `0xFFFFFF800063F89D`, `0xFFFFFF800063FC00`,
`0xFFFFFF800063FC0A`, `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30`, `0xFFFFFF800063FDA5`,
`0xFFFFFF800063FCF3`, `0xFFFFFF80009A41F4`, `0xFFFFFF80009A420D`, `0xFFFFFF8002AFFA18`–`0x2AFFA1B`,
`0xFFFFFF8002AFFA4D`, `0xFFFFFF8002AFFADC`, `0xFFFFFF8002AFFBB5`, `0xFFFFFF8002B10BCF`,
`0xFFFFFF8002B10BE8`, `0xFFFFFF8002B10C15`–`0xFFFFFF8002B10D0A`, `0xFFFFFF80022F6373`,
`0xFFFFFF80022F637F`–`0xFFFFFF80022F638E`, `0xFFFFFF80022F6390`, `0xFFFFFF8002B00688`,
`0xFFFFFF8002B006A6`, `0xFFFFFF8002B006CA`, `0xFFFFFF8002331AE4`.

Datos: `_gOSKextUnresolved` en `0xFFFFFF800025CD78` vale `0xFFFFFF8000BB5F1A`; la cadena del pánico está
en `0xFFFFFF8000CD26A5`; el plist de listas KPI está en `__LINKINFO,__symbolsets`, VA
`0xFFFFFF8003400000`, fichero `0x3300000`, `0x4D6D4` bytes.
