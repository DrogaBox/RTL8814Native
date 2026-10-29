# Índice de los documentos AWDL

Inventario de los 63 documentos `Docs/AWDL_*.md` a fecha de 2026-10-29, agrupados por tema, con el
resumen en una línea y el estado de cada uno. Base del estado: el título, el primer párrafo y las
secciones de cierre o de puntos abiertos de cada documento, más `HANDOFF_AWDL.md` (raíz del repo,
sección 4, que recoge los `?` deduplicados).

Convención de estado:

- **Cerrado y verificado** — las afirmaciones clave están contrastadas en el desensamblado o en el
  binario citado; no queda ningún punto que bloquee el uso del documento.
- **Cerrado con `?`** — verificado, con interrogantes residuales de campo o de detalle anotados en
  el propio documento; la convención del repo es marcar con `?` todo lo no visto en las fuentes.
- **Superado por X** — el documento conserva valor parcial, pero otro documento cierra su hueco o
  corrige sus números; se cita cuál.

## 1. Comportamiento del daemon (`wifip2pd`)

| Documento | Contenido | Estado |
|---|---|---|
| [AWDL_WIFIP2PD_REQUESTS.md](AWDL_WIFIP2PD_REQUESTS.md) | Superficie Apple80211 del daemon: los cuatro tipos que emite al crear `awdl0` (12, 94, 97, 293) y el formato de la petición | Cerrado y verificado; los emisores del resto de tipos los cierran [AWDL_REQUEST_ISSUERS.md](AWDL_REQUEST_ISSUERS.md) y [AWDL_APPLE80211_DISPATCH.md](AWDL_APPLE80211_DISPATCH.md) |
| [AWDL_WIFIP2PD_DISCOVERY.md](AWDL_WIFIP2PD_DISCOVERY.md) | La espera de 30 s tras el 94 no la libera un evento del driver, sino el descubridor de IORegistry | Cerrado y verificado |
| [AWDL_WIFIP2PD_POST_CREATE.md](AWDL_WIFIP2PD_POST_CREATE.md) | Qué libera el semáforo (`IOServiceMatched`) y la lista ordenada de toda la secuencia post-create | Cerrado y verificado |
| [AWDL_WIFIP2PD_IOCTLS.md](AWDL_WIFIP2PD_IOCTLS.md) | Los seis sitios `ioctl` del daemon y las nueve constantes de su tabla de requests | Cerrado y verificado; la operación de los ioctls 130/131 sin nombre la cierra [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) |
| [AWDL_WIFIP2PD_CALLERS.md](AWDL_WIFIP2PD_CALLERS.md) | Cadena de llamadores de cada sitio ioctl hasta una función Swift con nombre, un log o una constante | Cerrado y verificado; `?` en los thunks witness (despacho indirecto) |
| [AWDL_WIFIP2PD_STRUCTS_SEEN.md](AWDL_WIFIP2PD_STRUCTS_SEEN.md) | Estructuras que el daemon arma en memoria antes de cada ioctl o RawGet/RawSet, offset a offset | Cerrado y verificado |
| [AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md](AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md) | Ciclo de vida del objeto `AWDLInterface`: descubrimiento, creación, configuración y ausencia de destrucción | Cerrado y verificado; los tramos que dejaba `?` los cierran [AWDL_WIFIP2PD_POST_CREATE.md](AWDL_WIFIP2PD_POST_CREATE.md) y [AWDL_LLW_COMPANION.md](AWDL_LLW_COMPANION.md) |
| [AWDL_WIFIP2PD_P2P_EVENTS.md](AWDL_WIFIP2PD_P2P_EVENTS.md) | Registro del monitoreo de eventos P2P y las 16 ids de infraestructura | Cerrado y verificado; su punto abierto (dónde está el switch de ids) lo cierra [AWDL_EVENTS.md](AWDL_EVENTS.md) |
| [AWDL_WIFIP2PD_LOGSTRINGS.md](AWDL_WIFIP2PD_LOGSTRINGS.md) | Inventario de cadenas de log AWDL del daemon, agrupadas por tema | Cerrado como inventario; funciones consumidoras con `?` |

## 2. Superficie del kernel: despacho, vtables, structs y ciclo de vida

| Documento | Contenido | Estado |
|---|---|---|
| [AWDL_ROLE6_RE.md](AWDL_ROLE6_RE.md) | Primer análisis del rol 6: qué crea `AppleBCMWLANCore` y qué exige IO80211Family | Cerrado y verificado; fundamento de los documentos posteriores de despacho y nacimiento |
| [AWDL_KERNEL_ROLE_TABLE.md](AWDL_KERNEL_ROLE_TABLE.md) | Tablas rol↔nombre y cómo el rol 6 publica `IO80211VirtualInterfaceRole = AirLink` | Cerrado y verificado |
| [AWDL_PROXIMITY_VTABLE_HIERARCHY.md](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) | Herencia real `AppleBCMWLANProximityInterface` ↔ `IO80211AWDLProtocol` ↔ `IO80211VirtualInterface` y la llamada calificada al miembro de la base | Cerrado y verificado; los números de overrides los corrige [AWDL_PROXIMITY_OVERRIDES.md](AWDL_PROXIMITY_OVERRIDES.md) |
| [AWDL_PROXIMITY_OVERRIDES.md](AWDL_PROXIMITY_OVERRIDES.md) | Diff completo de las vtables 572/569 sin muestreo y disponibilidad de los símbolos que una subclase necesita | Cerrado y verificado; reemplaza los números del documento de jerarquía |
| [AWDL_VTABLE_SLOTS.md](AWDL_VTABLE_SLOTS.md) | Inventario slot a slot de `IO80211VirtualInterface` e `IO80211AWDLProtocol` desde los volcados guardados | Cerrado en su alcance; la columna Proximity que dejó `?` la completan [AWDL_PURE_SLOTS.md](AWDL_PURE_SLOTS.md) y [AWDL_PROXIMITY_OVERRIDES.md](AWDL_PROXIMITY_OVERRIDES.md) |
| [AWDL_PURE_SLOTS.md](AWDL_PURE_SLOTS.md) | Los 64 slots puros (490 y 507–569) resueltos con la vtable de Broadcom | Cerrado y verificado |
| [AWDL_IOCTL_SLOT_MAP.md](AWDL_IOCTL_SLOT_MAP.md) | Mapa de los 63 slots puros a los puentes `apple80211get*/set*` que los invocan | Cerrado y verificado |
| [AWDL_APPLE80211_DISPATCH.md](AWDL_APPLE80211_DISPATCH.md) | Las cuatro rutas de despacho y los 43 tipos que llegan a la superficie AWDL (+0xFC8..+0x11C0) | Cerrado y verificado |
| [AWDL_APPLE80211_STRUCTS.md](AWDL_APPLE80211_STRUCTS.md) | Nombres y tipos de parámetro de las firmas AWDL, sin layouts | Superado por [AWDL_STRUCTS.md](AWDL_STRUCTS.md), que cierra los layouts campo a campo |
| [AWDL_STRUCTS.md](AWDL_STRUCTS.md) | Layouts C completos de todos los payloads apple80211 que una interfaz AWDL debe contestar | Cerrado y verificado; campos sin escritura observada en `?` |
| [AWDL_INTERFACE_BIRTH.md](AWDL_INTERFACE_BIRTH.md) | Del handler del 94 a `awdl0` publicada: nombre BSD, unit, ifnet, properties y orden `registerService` | Cerrado y verificado |
| [AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) | Colas, pools, TX/RX y `getBSDNamePrefix` del rol 6 en el camino Broadcom | Cerrado y verificado; la reconstrucción fina del registro lleva `?` puntual |
| [AWDL_LLW_COMPANION.md](AWDL_LLW_COMPANION.md) | Companion `llw0` (tipo 293): emisión, consecuencias del fallo y veredicto: prescindible en etapa 1 | Cerrado y verificado para etapa 1; el campo `code` del payload queda en `?` (tabla §3) |
| [AWDL_EVENTS.md](AWDL_EVENTS.md) | Eventos kernel→userspace sobre `awdl0`, canales de entrega y qué consume el daemon | Cerrado y verificado |
| [AWDL_REQUEST_ISSUERS.md](AWDL_REQUEST_ISSUERS.md) | Quién emite cada request AWDL: el `IO80211AWDLPeerManager` del kernel y `IO80211.framework`; airportd el resto | Cerrado y verificado |

## 3. Protocolo en el aire, PeerManager, firmware y hardware RTL8814

| Documento | Contenido | Estado |
|---|---|---|
| [AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) | Trama AWDL en el aire tal como la parsea el kernel: cabeceras, validaciones y TLVs | Cerrado y verificado; `?` sobre quién retira la cabecera 802.11 y el FCS |
| [AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) | Cuerpos de los TLV sync params (tipo 4) y secuencia de canales (tipo 18) | Cerrado y verificado; ampliado por [AWDL_OPEN_QUESTIONS_1.md](AWDL_OPEN_QUESTIONS_1.md) |
| [AWDL_OPEN_QUESTIONS_1.md](AWDL_OPEN_QUESTIONS_1.md) | TLV de elección v2 (tipo 24), `monitorPeersTimeline` y los bytes aún sin nombre | Cerrado y verificado; los nombres canónicos del protocolo no son verificables desde el binario |
| [AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md) | Qué calcula `IO80211AWDLPeerManager` en software y qué exige al driver que tiene debajo | Cerrado y verificado |
| [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) | Contrato binario driver↔PeerManager: inyección de AF, eventos 41/42/43, ioctls internos y tirón de TX | Cerrado con `?` en su núcleo; constructores de TLV 6/8/10/11 y contenido exacto del tirón TX sin nombrar (etapa 2, tabla §3) |
| [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) | Qué delega Broadcom en el firmware para el rol 6: iovars, comandos y suscripción de eventos | Cerrado y verificado como inventario; unidad del guard time y mapas finos de payload en `?` |
| [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) | Qué ofrece el RTL8814AU para cada obligación del firmware: feasible / parcial / missing | Cerrado como inventario estático; ninguna conclusión está probada en hardware |

## 4. Etapa 1: especificación, implementación y revisiones

| Documento | Contenido | Estado |
|---|---|---|
| [AWDL_PROTOTYPE_DESIGN.md](AWDL_PROTOTYPE_DESIGN.md) | Plan por etapas (1–4) convertido de hipótesis a plan sobre hallazgos verificados | Cerrado como plan; los riesgos de §7 los cierran los documentos posteriores salvo los de etapas 2 y 3 (tabla §3 de este índice) |
| [AWDL_STAGE1_IMPL_SPEC.md](AWDL_STAGE1_IMPL_SPEC.md) | Especificación de la etapa 1: gate de capacidad, override del 94, clase de `awdl0`, rollback y pruebas T1–T7 | Cerrada y actualizada con el delta; nada ejecutado aún en hardware |
| [AWDL_STAGE1_SPEC_DELTA.md](AWDL_STAGE1_SPEC_DELTA.md) | Revisión de consistencia de la spec contra diez documentos y el código; halla el defecto bloqueante de la acción nula | Cerrado y verificado; sus ediciones están aplicadas a la spec |
| [AWDL_STAGE1_CODE_FIXES.md](AWDL_STAGE1_CODE_FIXES.md) | Antes/después de los seis defectos de la spec, verificados en el desensamblado | Cerrado y verificado; defectos 1, 2, 3 y 6 aplicados al código, 4 descartado como inocuo, 5 sin cambio |
| [AWDL_STAGE1_CODE_REVIEW_IFACE.md](AWDL_STAGE1_CODE_REVIEW_IFACE.md) | Revisión línea a línea de `AWDLInterface.{h,cpp}` contra la spec y CODE_FIXES | Cerrado y verificado; los hallazgos quedaron aplicados, cerrados por [AWDL_STAGE1_PENDING_CHECKS.md](AWDL_STAGE1_PENDING_CHECKS.md) o descartados, y el orden de la vtable cerrado por volcado (0 discrepancias) |
| [AWDL_STAGE1_CODE_REVIEW_HOOK.md](AWDL_STAGE1_CODE_REVIEW_HOOK.md) | Revisión del hook aplicado en `RTLAirLayer` (hallazgos A1–A10) | Cerrado como revisión; A6 y A7 quedan para las pruebas T2.2 y T3, y A10 sigue en `?` |
| [AWDL_STAGE1_OPEN_POINTS.md](AWDL_STAGE1_OPEN_POINTS.md) | Cierra tres puntos de la spec §15: cast del 94, caída sin WCL y padre del 97 | Cerrado y verificado en lo esencial; §4 deja dos puntos (tabla §3 de este índice) |
| [AWDL_STAGE1_PENDING_CHECKS.md](AWDL_STAGE1_PENDING_CHECKS.md) | Cierra cuatro puntos: lector de `RegInfo+0x3C`, `vi_free` a medio inicializar, dereg opcional y `checkForWork` | Cerrado y verificado en lo esencial; siete puntos finos propios (tabla §3) |
| [AWDL_STAGE1_STUB_VALUES.md](AWDL_STAGE1_STUB_VALUES.md) | La secuencia post-create no toca ningún slot puro; stub recomendado petición a petición | Cerrado y verificado; corregido por [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) (fila 7) y [AWDL_STAGE1_VIRTUAL_IF_PARENT.md](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) (fila 4) |
| [AWDL_STAGE1_QUEUE_SHAPE.md](AWDL_STAGE1_QUEUE_SHAPE.md) | 1 TX submission + 1 TX completion + 1 RX completion basta; sólo el bit WMM exige 4 colas | Cerrado y verificado; su `?` de orden lo cierra [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md) |
| [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md) | `setInterface` ocurre dentro del registro, antes del `ifnet`; el 22 del validador no llega al driver | Cerrado y verificado |
| [AWDL_STAGE1_TXPUSH_LLINK.md](AWDL_STAGE1_TXPUSH_LLINK.md) | No hay salto del `ifclassq` a `tx_push`; el hilo de arranque ejecuta `_ifp_if_start`, que purga; el primer ND/RS sólo llega a `txNoop` si la familia golpea la cola después del encolado (a decidir en T3.5) | Cerrado y verificado; `[ifp+0x900]` y el paso final `?` por hardware |
| [AWDL_STAGE1_STACK_TX_ENTRY.md](AWDL_STAGE1_STACK_TX_ENTRY.md) | La pila entra por `if_output` = `_dlil_output_handler` → `_ifnet_enqueue` → `ifclassq`; nada llama a `packetSubmission` después, así que el ND no llega a `txNoop` y la etapa 2 debe empujar la cola | Cerrado y verificado; `ifp+0x78` y el orden con el hilo de arranque `?` por hardware |
| [AWDL_STAGE1_STAGE2_PUSH_DESIGN.md](AWDL_STAGE1_STAGE2_PUSH_DESIGN.md) | `ifp+0x78` (`if_refflags`) vale 1 y `if_qflush` purga; el orden con el hilo de arranque es indeterminado; la vía de diseño es sustituir el `output` del `ifnet` con `ifnet_set_output_handler`, que evita el `ifclassq` | Verificado; enlace del símbolo, orden y anillo `?` por hardware |
| [AWDL_STAGE1_OUTPUT_HANDLER_LINK.md](AWDL_STAGE1_OUTPUT_HANDLER_LINK.md) | Los cuatro `*_output_handler` no están en ninguna lista KPI: no enlazan desde un kext; el `output` propio entra por `params+0x40` en `initBSDInterfaceParameters` (slot `0x8E0`) o por `cmpxchg` en `ifp+0x258` desde `prepareBSDInterface` (slot `0x8E8`); sin API de detección en carga | Verificado en lo esencial; exportaciones leídas del plist KPI `?` por reconfirmar en otra versión |
| [AWDL_STAGE1_LIFECYCLE_CHECKS.md](AWDL_STAGE1_LIFECYCLE_CHECKS.md) | Tx data offset: `RegInfo+0x3C` → `eparams+0x144` → `ifnet+0x58C` (no el cliente BSD); `r15` de `AppleBCMWLANSkywalkInterface::start` es el `RegInfo` del llamador; el `uint` de `deregister*` es un parámetro muerto | Cerrado y verificado; nombres xnu de los campos y efecto de no restar el headroom del MTU `?` |
| [AWDL_VERSION_SENSITIVE_OFFSETS.md](AWDL_VERSION_SENSITIVE_OFFSETS.md) | Tabla de offsets de `RegInfo`, `ifnet_init_eparams`, `ifnet`, `ifclassq`, `netif_llink`, ranuras de vtable e ivars, con patrón de bytes por fila y diseño de consumo por los scripts de recheck y `Tools/awdl_recheck_offsets.py` (tabla `Tools/awdl_offsets_25G241.tsv`) | Verificado byte a byte en las filas de mayor riesgo (§10); filas `(d)` y versiones anteriores a 26 `?` |
| [AWDL_STAGE1_SMALL_CHECKS.md](AWDL_STAGE1_SMALL_CHECKS.md) | `IO80211VirtualInterface::init` devuelve `bool` de 1 byte; el `req` de pila de la ruta legacy del 94 nace a cero | Cerrado y verificado |
| [AWDL_STAGE1_IFUP_PATH.md](AWDL_STAGE1_IFUP_PATH.md) | Qué ejecuta `ifconfig awdl0 up` y el primer paquete: las colas nacen deshabilitadas y el ND/RS no llega al no-op | Cerrado y verificado; su §4 lo corrige [AWDL_STAGE1_QSET_STATE.md](AWDL_STAGE1_QSET_STATE.md) |
| [AWDL_STAGE1_QSET_STATE.md](AWDL_STAGE1_QSET_STATE.md) | `qset+0x3C` es `nll_state`; `if_refflags`/`if_eflags` los pone IOSkywalkFamily; el análisis del primer ND/RS lo corrige [AWDL_STAGE1_TXPUSH_LLINK.md](AWDL_STAGE1_TXPUSH_LLINK.md) | Cerrado y verificado; cinco puntos finos propios (§10) |
| [AWDL_STAGE1_ERRNO_TXPATH.md](AWDL_STAGE1_ERRNO_TXPATH.md) | `errnoFromReturn` mapea el rechazo por cola deshabilitada (`0xE00002D8`) a errno 16 (`EBUSY`); wifip2pd no consume `if_link_state` ni `state+113` (lector de `IFF_UP` e `IFF_RUNNING` vía `getifaddrs` con extremo keypath en `?`) | Cerrado en sus dos preguntas; cadena keypath del daemon, lectores kernel de `state+113` y tramo `dlil_ifclassq_setup` en `?` (§8) |
| [AWDL_STAGE1_IFM_CURRENT.md](AWDL_STAGE1_IFM_CURRENT.md) | `ifm_current` sale del descriptor de medios sembrado por `registerNetworkInterface` desde `RegInfo+0x28` (0x80) | Cerrado y verificado; su punto abierto lo cierra [AWDL_STAGE1_SIOCSIFMEDIA.md](AWDL_STAGE1_SIOCSIFMEDIA.md) |
| [AWDL_STAGE1_SIOCSIFMEDIA.md](AWDL_STAGE1_SIOCSIFMEDIA.md) | Nada puede cambiar `ifm_current` de `awdl0` antes de la lectura del daemon | Cerrado y verificado; T5.1 queda como comprobación de regresión |
| [AWDL_STAGE1_VIRTUAL_IF_PARENT.md](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) | El 97 lo responde la familia con `getPrimarySkywalkInterface`; sin stub propio | Cerrado y verificado |
| [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) | Los ioctls 130/131 van por socket BSD y caen fuera de rango → 102; no son fatales | Cerrado y verificado |
| [AWDL_STAGE1_DISPATCH_PATH.md](AWDL_STAGE1_DISPATCH_PATH.md) | Rutas ioctl y user client hasta el puente del 94; `createEventPipe` exige entitlement | Cerrado y verificado; su §4 (el `this` desde el `ifnet`) la cierra [AWDL_STAGE1_IFNET_TO_INTERFACE.md](AWDL_STAGE1_IFNET_TO_INTERFACE.md) |
| [AWDL_STAGE1_IFNET_TO_INTERFACE.md](AWDL_STAGE1_IFNET_TO_INTERFACE.md) | `ifnet_softc` entrega el cliente BSD; cadenas completas de `en2` y `awdl0` hasta `RouteExternal` | Cerrado y verificado |
| [AWDL_STAGE1_REGINFO_AND_GATES.md](AWDL_STAGE1_REGINFO_AND_GATES.md) | Flag del 6 (bit `+0x52` del bloque `+0x128`), `RegInfo+0x10`/`+0x40` sin lectores y gate del 94 | Cerrado y verificado; el comportamiento de `registerService(2)` bajo el gate lo decide T2.2 |
| [AWDL_STAGE1_REGINFO_BROADCOM.md](AWDL_STAGE1_REGINFO_BROADCOM.md) | Lado Broadcom de `+0x10`/`+0x40` y corrección del contenedor de `+0x118` | Cerrado y verificado |

## 5. Pruebas y logs

| Documento | Contenido | Estado |
|---|---|---|
| [AWDL_STAGE1_TEST_LOG_PATTERNS.md](AWDL_STAGE1_TEST_LOG_PATTERNS.md) | Literales exactos que cada prueba T1–T7 debe buscar en cada canal de log | Cerrado como documento; los literales dependientes de valores en runtime quedan en su tabla §9 (`?`) |
| [AWDL_STAGE1_LOG_STRINGS_AUDIT.md](AWDL_STAGE1_LOG_STRINGS_AUDIT.md) | Contraste entre los patrones esperados y las cadenas que el código emite de verdad | Cerrado y verificado |
| [AWDL_STAGE1_RUNBOOK_REVIEW.md](AWDL_STAGE1_RUNBOOK_REVIEW.md) | Revisión del guion de pruebas de hardware T1–T7 (hallazgos H1–H17) | Cerrado y verificado; hallazgos aplicados al guion, que vive fuera del repo |
| [AWDL_STAGE1_DEPLOY_REVIEW.md](AWDL_STAGE1_DEPLOY_REVIEW.md) | Revisión del despliegue y la construcción (incidencias 1–10) | Cerrado como revisión; incidencias 1, 2, 3 y 6 aplicadas, 4, 5, 7, 8, 9 y 10 sin aplicar (gravedad baja) |

## 6. Compatibilidad entre versiones y herramientas

| Documento | Contenido | Estado |
|---|---|---|
| [AWDL_KEXT_LINKAGE.md](AWDL_KEXT_LINKAGE.md) | Qué símbolos AWDL exporta IO80211Family, por dónde enlaza un kext de terceros y qué fallbacks quedan | Cerrado y verificado para 25.6.0; re-verificar la tabla tras cada build; el fallback sin factorías de §3 queda sin explorar (etapa 2) |
| [AWDL_COMPAT_DEPENDENCIES.md](AWDL_COMPAT_DEPENDENCIES.md) | Inventario de todo lo que el código AWDL presupone del OS, con la receta más barata de recomprobación por versión (14–26) | Cerrado como inventario; las verificaciones por versión quedan por ejecutar cuando haya kernelcaches de 14/15 |
| [AWDL_KERNELCACHE_OBTENTION.md](AWDL_KERNELCACHE_OBTENTION.md) | Cómo conseguir el kernel collection x86_64 de macOS 14/15/26 sin instalar la versión y producir los volcados | Guía con pasos en `?` para 14/15 (rutas de instaladores; verificar al ejecutar) |
| [AWDL_BKE_ATTACH.md](AWDL_BKE_ATTACH.md) | Registro de la sesión de adjunto del adaptador al kernel collection y sus limitaciones operativas | Cerrado (registro histórico de sesión) |

## 7. Orden de lectura para alguien nuevo

1. `HANDOFF_AWDL.md` (raíz del repo, en inglés): estado, reglas y Artefactos vigentes.
2. [AWDL_PROTOTYPE_DESIGN.md](AWDL_PROTOTYPE_DESIGN.md): el plan por etapas que ordena todo lo demás.
3. Lado daemon: [AWDL_WIFIP2PD_REQUESTS.md](AWDL_WIFIP2PD_REQUESTS.md) →
   [AWDL_WIFIP2PD_DISCOVERY.md](AWDL_WIFIP2PD_DISCOVERY.md) →
   [AWDL_WIFIP2PD_POST_CREATE.md](AWDL_WIFIP2PD_POST_CREATE.md) →
   [AWDL_WIFIP2PD_IOCTLS.md](AWDL_WIFIP2PD_IOCTLS.md) con
   [AWDL_WIFIP2PD_CALLERS.md](AWDL_WIFIP2PD_CALLERS.md) y
   [AWDL_WIFIP2PD_STRUCTS_SEEN.md](AWDL_WIFIP2PD_STRUCTS_SEEN.md) →
   [AWDL_LLW_COMPANION.md](AWDL_LLW_COMPANION.md).
4. Superficie del kernel: [AWDL_KERNEL_ROLE_TABLE.md](AWDL_KERNEL_ROLE_TABLE.md) →
   [AWDL_PROXIMITY_VTABLE_HIERARCHY.md](AWDL_PROXIMITY_VTABLE_HIERARCHY.md) y
   [AWDL_PURE_SLOTS.md](AWDL_PURE_SLOTS.md) →
   [AWDL_APPLE80211_DISPATCH.md](AWDL_APPLE80211_DISPATCH.md) →
   [AWDL_IOCTL_SLOT_MAP.md](AWDL_IOCTL_SLOT_MAP.md) →
   [AWDL_STRUCTS.md](AWDL_STRUCTS.md) →
   [AWDL_INTERFACE_BIRTH.md](AWDL_INTERFACE_BIRTH.md) →
   [AWDL_PROXIMITY_DATAPATH.md](AWDL_PROXIMITY_DATAPATH.md) →
   [AWDL_EVENTS.md](AWDL_EVENTS.md) →
   [AWDL_REQUEST_ISSUERS.md](AWDL_REQUEST_ISSUERS.md).
5. Protocolo y hardware: [AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) →
   [AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) →
   [AWDL_OPEN_QUESTIONS_1.md](AWDL_OPEN_QUESTIONS_1.md) →
   [AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md) →
   [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) →
   [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) →
   [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md).
6. Etapa 1: [AWDL_STAGE1_IMPL_SPEC.md](AWDL_STAGE1_IMPL_SPEC.md) primero, y después los cierres en
   este orden: [AWDL_STAGE1_DISPATCH_PATH.md](AWDL_STAGE1_DISPATCH_PATH.md) e
   [AWDL_STAGE1_IFNET_TO_INTERFACE.md](AWDL_STAGE1_IFNET_TO_INTERFACE.md) (despacho),
   [AWDL_STAGE1_QUEUE_SHAPE.md](AWDL_STAGE1_QUEUE_SHAPE.md) y
   [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md) (colas y arranque BSD),
   [AWDL_STAGE1_IFM_CURRENT.md](AWDL_STAGE1_IFM_CURRENT.md) y
   [AWDL_STAGE1_SIOCSIFMEDIA.md](AWDL_STAGE1_SIOCSIFMEDIA.md) (medios),
   [AWDL_STAGE1_VIRTUAL_IF_PARENT.md](AWDL_STAGE1_VIRTUAL_IF_PARENT.md) y
   [AWDL_STAGE1_IOCTL_130_131.md](AWDL_STAGE1_IOCTL_130_131.md) (secuencia post-create),
   [AWDL_STAGE1_STUB_VALUES.md](AWDL_STAGE1_STUB_VALUES.md),
   [AWDL_STAGE1_OPEN_POINTS.md](AWDL_STAGE1_OPEN_POINTS.md) y
   [AWDL_STAGE1_PENDING_CHECKS.md](AWDL_STAGE1_PENDING_CHECKS.md),
   [AWDL_STAGE1_REGINFO_AND_GATES.md](AWDL_STAGE1_REGINFO_AND_GATES.md) con
   [AWDL_STAGE1_REGINFO_BROADCOM.md](AWDL_STAGE1_REGINFO_BROADCOM.md),
   [AWDL_STAGE1_IFUP_PATH.md](AWDL_STAGE1_IFUP_PATH.md) y
   [AWDL_STAGE1_QSET_STATE.md](AWDL_STAGE1_QSET_STATE.md) (subida), y por último las revisiones:
   [AWDL_STAGE1_SPEC_DELTA.md](AWDL_STAGE1_SPEC_DELTA.md),
   [AWDL_STAGE1_CODE_FIXES.md](AWDL_STAGE1_CODE_FIXES.md),
   [AWDL_STAGE1_CODE_REVIEW_IFACE.md](AWDL_STAGE1_CODE_REVIEW_IFACE.md) y
   [AWDL_STAGE1_CODE_REVIEW_HOOK.md](AWDL_STAGE1_CODE_REVIEW_HOOK.md).
7. Pruebas: [AWDL_STAGE1_TEST_LOG_PATTERNS.md](AWDL_STAGE1_TEST_LOG_PATTERNS.md) →
   [AWDL_STAGE1_LOG_STRINGS_AUDIT.md](AWDL_STAGE1_LOG_STRINGS_AUDIT.md) →
   [AWDL_STAGE1_RUNBOOK_REVIEW.md](AWDL_STAGE1_RUNBOOK_REVIEW.md) y
   [AWDL_STAGE1_DEPLOY_REVIEW.md](AWDL_STAGE1_DEPLOY_REVIEW.md).
8. Compatibilidad: [AWDL_KEXT_LINKAGE.md](AWDL_KEXT_LINKAGE.md) →
   [AWDL_COMPAT_DEPENDENCIES.md](AWDL_COMPAT_DEPENDENCIES.md) →
   [AWDL_KERNELCACHE_OBTENTION.md](AWDL_KERNELCACHE_OBTENTION.md). `AWDL_BKE_ATTACH.md` es
   un registro de sesión y se lee sólo como contexto histórico.

Los inventarios intermedios (`AWDL_APPLE80211_STRUCTS.md`, `AWDL_VTABLE_SLOTS.md`) y las revisiones
ya absorbidas se pueden saltar en la primera lectura y consultar sólo como referencia.

## 8. Interrogantes (`?`) que siguen abiertos en todos los documentos

Recogida de las secciones «puntos abiertos»/«pendientes» de cada documento, deduplicada; sustituye
a la sección 4 de `HANDOFF_AWDL.md`, de la que corrige dos puntos ya cerrados (el `this` de
`processBSDCommand`, cerrado por `AWDL_STAGE1_IFNET_TO_INTERFACE.md`, y los valores de
`RegInfo+0x10`/`+0x40`, cerrados funcionalmente al demostrar que no tienen lector en
`AWDL_STAGE1_REGINFO_AND_GATES.md` §3 y `AWDL_STAGE1_REGINFO_BROADCOM.md` §3).

### 8.1 Los que decide el hardware de la etapa 1 (pruebas T1–T7)

| # | Punto abierto | Documento | Cierre |
|---:|---|---|---|
| 1 | Comportamiento de `registerService(kIOServiceSynchronous)` bajo el gate del work loop del 94: posible interbloqueo (hallazgo A6) | `AWDL_STAGE1_IMPL_SPEC.md` §15.1; `AWDL_STAGE1_SPEC_DELTA.md` (#90); `AWDL_STAGE1_REGINFO_AND_GATES.md` §5; `AWDL_STAGE1_CODE_REVIEW_HOOK.md` A6 | Prueba T2.2 (medir el retorno del 94) |
| 2 | Rollback cuando el registro triunfó pero falta el nombre BSD; si `terminate()` sobre un arranque fallido retira las colas (hallazgo A7, #68) | `AWDL_STAGE1_CODE_REVIEW_HOOK.md` A7; `AWDL_STAGE1_SPEC_DELTA.md` §7 | Pruebas T3 y T7.3 |
| 3 | Fatalidad real del registro del monitoreo de eventos en wifip2pd (#62) | `AWDL_STAGE1_SPEC_DELTA.md` §6; `AWDL_STAGE1_IMPL_SPEC.md` T7.1 | Prueba T7.1 |
| 4 | Propiedades finales en el IORegistry: `classNameOverride` de la subclase y `IOSkywalkNexusUUID` | `AWDL_STAGE1_SPEC_DELTA.md` §7 | Prueba T3.1 (`ioreg`) |

### 8.2 Los que cierra un chequeo en el desensamblado (IDA)

| # | Punto abierto | Documento | Cierre |
|---:|---|---|---|
| 5 | Clase del objeto WCL en `glue+0xD8` y el método del slot `+0x120` que decide si el 94 se consume; si existe un WCL que lo consuma en este stack (#95) | `AWDL_STAGE1_OPEN_POINTS.md` §4; `AWDL_STAGE1_SPEC_DELTA.md` §7 | Identificar la clase y el método del slot en el KC |
| 6 | Llamador de `requestEnqueue` sobre las colas de `awdl0` y punto de habilitación del event source (`+0x28`) en el arranque del datapath: con acción nula pasaría de latente a fatal | `AWDL_STAGE1_PENDING_CHECKS.md` §4 | Localizar el llamador en la familia; T3.5 lo confirma en hardware |
| 7 | ~~Tipo de retorno real de `IO80211VirtualInterface::init` (#82); el código lo da por `bool`~~ | `AWDL_STAGE1_SPEC_DELTA.md` §7; `AWDL_STAGE1_CODE_FIXES.md` §3 | Leer el manejo del retorno en el `init` de la familia. Cerrado por [AWDL_STAGE1_SMALL_CHECKS.md](AWDL_STAGE1_SMALL_CHECKS.md) §1 |
| 8 | ~~Longitud inicial del `req` de pila en la ruta legacy del 94 cuando `len < 32` (hallazgo A10)~~ | `AWDL_STAGE1_CODE_REVIEW_HOOK.md` A10 | Leer la inicialización del `req` en `setVirtualInterfaceCreate` Cerrado por [AWDL_STAGE1_SMALL_CHECKS.md](AWDL_STAGE1_SMALL_CHECKS.md) §2 |
| 9 | Puntos finos del ciclo de vida: cero-inicial del asignador tipado, limpieza de `registerInterestedDriver` si falla la rama 5 de `init`, propietario del cerrojo `ivars+0x30`, campo del `ifnet` que consume `clienteBSD+0x13C`, enlace de `r15` en `AppleBCMWLANSkywalkInterface::start`, intención del parámetro `uint` de `deregisterEthernetInterface` | `AWDL_STAGE1_PENDING_CHECKS.md` §«Puntos que quedan abiertos» | Chequeos dirigidos en el KC, uno por punto (parcial: #4 `ifnet+0x58C`, #5 `r15 = rdx` y #7 parámetro muerto, cerrados por [AWDL_STAGE1_LIFECYCLE_CHECKS.md](AWDL_STAGE1_LIFECYCLE_CHECKS.md)) |
| 10 | Camino de TX: errno que ve la pila al descartarse el paquete (mapeo de `0xE00002D8` en `errnoFromReturn`), consumidor de `state+113`/`if_link_state` en wifip2pd, llamada del `ifclassq` a `tx_push`, vía de alta del `netif_llink` en el camino del kext | `AWDL_STAGE1_IFUP_PATH.md` §8; `AWDL_STAGE1_QSET_STATE.md` §10; errno 16 (`EBUSY`) y búsquedas en wifip2pd cerrados en `AWDL_STAGE1_ERRNO_TXPATH.md` §1-§6 | Recorrer la cadena keypath de wifip2pd y el llenado de la tabla del `ifclassq` (`dlil_ifclassq_setup`); alta del `netif_llink` |
| 11 | ~~Orden entre `setInterface` y la creación del `ifnet` en `bsd_start`; quién consume el retorno 22 del rechazo por `num_txqs == 0`~~ | `AWDL_STAGE1_QUEUE_SHAPE.md` §11 | Cerrado por [AWDL_STAGE1_BSDSTART_ORDER.md](AWDL_STAGE1_BSDSTART_ORDER.md): `setInterface` en `0xffffff8002b0a38d` (dentro del registro, antes del `registerService`); el `ifnet` en `0xffffff8002affbb5` (dentro del `start` del cliente BSD); el 22 del núcleo lo consume el alloc del proveedor y el de la vía DriverKit se convierte en `0xE00002BC` |
| 12 | Qué getters de los 63 slots consulta el daemon fuera de la secuencia post-create (p. ej. al abrir AirDrop) | `AWDL_STAGE1_STUB_VALUES.md` §3/§5 | Leer el switch maestro de eventos/requests del daemon; confirmado con un par Apple real en hardware |
| 13 | Constructores de TLV sin nombre (datapath state, ranging, real-time group, version, NAN sync, pref chan) y contenido exacto del tirón de TX | `AWDL_PEERMANAGER_DRIVER_ABI.md` §3.11 y §4 | Identificar y decompilar los constructores (etapa 2) |
| 14 | Vínculo del `IO80211AWDLPeerManager` del kernel con la interfaz de un driver externo (ivars `+0x120`/`+0x28`) | `AWDL_APPLE80211_DISPATCH.md` §8.3; `AWDL_PROTOTYPE_DESIGN.md` §7 | Trazar la creación y publicación del manager para rol 6 (etapa 2) |
| 15 | Qué objetos AWDL puede instanciar un kext externo y qué ocurre si la instanciación se rechaza; el fallback de vtable no cubre la creación sin factorías exportadas | `AWDL_KEXT_LINKAGE.md` §3 | Explorar `withInterface`/`createPeerManager` y probar la carga (etapa 2) |
| 16 | Vía de TX de action frames en la etapa 3: ruta interna (tipo 120 u otros) o mecanismo propio del kext | `AWDL_PROTOTYPE_DESIGN.md` §5 y §7 | Seguir el emisor del evento 48 hacia atrás hasta el driver (etapa 3) |
| 17 | Semántica del u32 `@+8` del evento 47 (`PEER_PRESENCE`) | `AWDL_PROTOTYPE_DESIGN.md` §5 | Leer el case 47 del daemon y el validador de la familia |
| 18 | Valor del campo `code` (payload+0) del tipo 293 en el camino AWDL y su semántica (el kernel no lo lee) | `AWDL_LLW_COMPANION.md` §1.1 y §5; `AWDL_STRUCTS.md` §3.4 | Trazar el quinto argumento del creador por rol en `sub_100047660` |

### 8.3 Los que cierra una prueba en el hardware RTL8814

| # | Punto abierto | Documento | Cierre |
|---:|---|---|---|
| 19 | Unidad real del guard time (factor ×10 observado sin confirmación), precisión de los timers software del host frente a la cadencia AWDL y viabilidad de `GTINT3`/`GTINT4` como timer hardware en USB | `AWDL_FIRMWARE_DUTIES.md` §7; `AWDL_RTL8814_HW_SUPPORT.md` §7 (filas b1/b3a/b3b) | Medición sobre el 8814AU |
| 20 | Valor vivo por defecto de `RXFLTMAP0`, confirmación de TX (C2H/`CONFIG_XMIT_ACK`), operación del puerto 1 en el path USB y punto de integración de la entrega de action frames hacia AirLayer/BPF | `AWDL_RTL8814_HW_SUPPORT.md` §6.2–§6.4 y §8 | Registro en vivo y prueba de RX/TX de action frames |
| 21 | Toda la clasificación feasible/parcial/missing de `AWDL_RTL8814_HW_SUPPORT.md` §7 | `AWDL_RTL8814_HW_SUPPORT.md` §7 | Las etapas 2–4 del plan, en hardware |

### 8.4 Los que cierra la propia ejecución de la guía (compatibilidad)

| # | Punto abierto | Documento | Cierre |
|---:|---|---|---|
| 22 | Rutas de instaladores Intel de macOS 14/15, volumen sellado en Apple silicon y derivación de la base del KC en otra versión | `AWDL_KERNELCACHE_OBTENTION.md` §1, §2 y §6 | Ejecutar la guía sobre una imagen de cada versión |
