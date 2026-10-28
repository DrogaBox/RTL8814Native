# Cómo wifip2pd detecta la interfaz AWDL creada

Complementa `AWDL_WIFIP2PD_REQUESTS.md`, que dejó pendiente qué libera la espera de 30 s tras `VIRTUAL_IF_CREATE`. Análisis con IDA sobre `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), más una comprobación en vivo con `ioreg`.

## Resultado

La espera **no la libera un evento Apple80211 del driver**. La libera el **descubridor de interfaces por IORegistry** de `wifip2pd`, cuando aparece en el registro una interfaz con la propiedad de rol esperada.

## Cadena verificada

1. `sub_100047660` (`0x100047660`) hace `RawSet` tipo 94 (`VIRTUAL_IF_CREATE`) y, si devuelve 0, espera un `DispatchSemaphore` (30 s) guardado en un diccionario por rol (`self+176`).
2. `sub_100046BD0` (`0x100046bd0`) busca ese semáforo por rol, guarda un valor y hace `signal()` en bucle mientras queden esperando. Se llama desde tres puntos de `sub_100043720`: `0x1000445a3`, `0x1000451f2`, `0x100045297`.
3. `sub_100043720` (`0x100043720`, tamaño `0x1da8`) es el descubridor: recibe un iterador de IOKit (`IOIteratorNext`, `IOObjectRetain/Release`) y, por cada servicio, lee propiedades del registro con `IORegistryEntryCreateCFProperty`.

## Qué lee `sub_100043720` de cada servicio nuevo

| Clave | Uso |
|---|---|
| `IOInterfaceName` (`0x100043990` y siguientes) | nombre BSD de la interfaz |
| `IO80211VirtualInterfaceRole` (string en `0x100446640`) | rol, se intenta primero |
| `IO80211InterfaceRole` (string en `0x100446660`) | rol, alternativa si falta la anterior |

El valor del rol se convierte a `String` de Swift (`swift_dynamicCast`) y se compara con estas cadenas, en este orden (`0x1000440a6`–`0x1000441db`):

| Cadena | Código interno (`r14b`) |
|---|---:|
| `Infrastructure` | 0 |
| `AirLink` | 1 |
| `WiFi-Aware Discovery` | 2 |
| `WiFi-Aware Discovery+Data` | 2 (sin asignación propia visible) |
| `WiFi-Aware Data` | 3 |

Si ninguna coincide se va a `0x100045227`. Con la coincidencia se crea un `InterfaceHandle` (`sub_1000383C0`, que llama `Apple80211Open`).

Mensajes de log del descubridor, útiles para diagnosticar en vivo: `Ignoring %s because its not a WiFi interface` (`0x100494b00`), `Ignoring %s because no interface index found` (`0x100494b30`), `Unable to find role for interface: %s` (`0x100494b60`), `Found new interface…`, `Failed to bind to…`.

## Comprobación en vivo

`ioreg -l` muestra que nuestra `RTLAirInterface` ya publica `"IO80211InterfaceRole" = "Infrastructure"`. O sea, esa propiedad existe en nuestras interfaces sin código extra y el descubridor ya la reconoce.

## Consecuencias para el prototipo

- Para que `wifip2pd` considere creada la interfaz AWDL, el objeto debe aparecer en IORegistry con `IOInterfaceName` y una propiedad de rol cuyo valor sea una de las cadenas de arriba, y debe tener índice de interfaz (BSD, `if_nametoindex`). Responder `VIRTUAL_IF_CREATE` con éxito sin publicar esa interfaz deja al daemon esperando 30 s.
- La cadena de rol que corresponde a AWDL **no está verificada**. `AirLink` es el candidato por exclusión (no hay otra cadena que encaje), pero falta confirmarlo viendo qué escribe Broadcom en `IO80211VirtualInterfaceRole` para su `AppleBCMWLANProximityInterface`.
- Tampoco está verificado si `IO80211VirtualInterface::init` (clase base, exportada) escribe esa propiedad por sí misma, como `IO80211InfraInterface` hace con `IO80211InterfaceRole`. Si lo hace, una subclase la heredaría.

## Pendiente

1. En `BootKernelExtensions.i64`, buscar las referencias a `IO80211VirtualInterfaceRole` para ver quién la escribe y con qué valor para el rol 6.
2. Confirmar la cadena de rol de AWDL.
3. Tabla de 16 ids de eventos (`0x100515ca0`, para la infraestructura): `1, 11, 59, 2, 3, 125, 126, 127, 9, 66, 74, 78, 82, 215, 55, 160`. Se leyó de la memoria del IDB. Es el conjunto que `sub_100041B60` registra con `Apple80211StartMonitoringEvent` solo para el handle de rol 0; los demás roles usan `Apple80211RawP2PEventMonitoringInit` (`sub_100041F20`). Los nombres de cada id no están verificados.
