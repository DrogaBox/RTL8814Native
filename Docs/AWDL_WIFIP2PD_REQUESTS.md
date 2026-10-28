# Peticiones Apple80211 de wifip2pd (macOS 25.6.0)

Resultado de seguir los xrefs en `Build/ida_awdl/wifip2pd.i64` (`wifip2pd.x86_64`, md5 `a120cfccfa2783303604ce5be9e1763a`) con IDA. Cierra el pendiente que dejaban `AWDL_ROLE6_RE.md` y `AWDL_PROTOTYPE_DESIGN.md` sobre la secuencia post-create. Las direcciones son VA del binario. Lo que no está probado se marca como tal.

## Superficie Apple80211 del daemon

`wifip2pd` llama muy pocas funciones del framework. Stubs en `0x1004457xx`:

| Función | Stub | Quién la llama |
|---|---|---|
| `Apple80211Open` | `0x1004457d4` | `sub_1000383C0` |
| `Apple80211RawGet` | `0x1004457da` | `sub_100046D30` (wrapper genérico) y `sub_100047660` (3 sitios: `0x10004811d`, `0x100048260`, `0x1000483a8`) |
| `Apple80211RawSet` | `0x1004457e0` | `sub_100047660` (`0x100048d95`) y `sub_10004B300` (`0x10004b3e6`) |
| `Apple80211StartMonitoringEvent` | `0x1004457e6` | `sub_100041B60`, 16 eventos de una tabla en `0x100515c84` |
| `Apple80211EventMonitoringHalt` | `0x1004457ce` | `sub_100042120` |

No se encontraron `Apple80211Set/Get` por nombre. Todo pasa por `RawGet/RawSet` con una estructura de petición.

## Formato de la petición (verificado en `sub_100046D30` y `sub_100047660`)

Estructura que se pasa a `RawGet/RawSet(handle, req)`:

| Offset | Campo |
|---:|---|
| `+0x10` | tipo de request (id apple80211) |
| `+0x18` | longitud (u32) |
| `+0x20` | puntero a los datos |

## `sub_100047660`: obtener o crear el handle de una interfaz por rol

Busca un `InterfaceHandle` por rol y nombre de interfaz padre. Si no existe, lo crea. El rol selecciona una rama por tabla de saltos en `0x10004992c`; en la rama del rol 6 arma el nombre `"awdl"`.

Secuencia que hace para crear:

1. `RawGet` tipo **12**, longitud **0x15 (21)**. Se repite en tres ramas. Coincide con `CARD_CAPABILITIES`, que es la petición que ya sabíamos que usa como puerta (AWDL gate).
2. `RawSet` tipo **0x5E = 94**, longitud **0x20 = 32**: es `VIRTUAL_IF_CREATE`. Datos en el buffer (`var_C0`): un u32 en `+0`, seis bytes en `+4..+9` (la MAC, cargada de argumentos de la función) y el **rol en `+0x0C`**, que vale **6** (AWDL) o **0x0A** según la rama. Coincide con lo observado en hardware.
3. Si `RawSet` devuelve error, se loguea y se lanza la excepción. No se reintenta.
4. Si devuelve 0, el daemon **espera un semáforo con timeout de 30 s** (`DispatchTime.now() + 30`). El semáforo se guarda por rol y lo libera un callback de eventos del driver.

Consecuencia para el prototipo: después de contestar el `VIRTUAL_IF_CREATE`, el driver debe **emitir un evento** que `wifip2pd` reciba. Sin él, el daemon espera 30 s por intento. Qué evento exacto desbloquea el semáforo **no está verificado**: el callback es `sub_1000685A0` → `sub_100041FE0`, que solo reenvía `(tipo de evento, datos, longitud)` a una clausura; el que lo consume y llama a `semaphore.signal` queda por localizar.

## `sub_100046D30`: `RawGet` genérico con cadena de salida

Arma la petición con tipo **97** (`+0x10`), longitud pasada por parámetro y puntero al buffer, llama `RawGet` y devuelve la cadena C leída (hasta la longitud dada). Qué id apple80211 es el 97 **no está verificado**; por el uso parece una lectura de nombre. Su único caller directo es `sub_100068C20`.

## `sub_10004B300`: `RawSet` tipo 293

`RawSet` tipo **293**, longitud **24**, seguido de `if_nametoindex`. No se atribuye a AWDL.

## Eventos

`sub_100041B60` registra 16 eventos con `Apple80211StartMonitoringEvent` solo para el handle de rol 0 (infraestructura). Para los demás roles usa otro registro (`sub_100041F20`) y no ejecuta ese bucle. La tabla de 16 ids está en `0x100515c84` y no se transcribió aquí.

## Qué falta

1. Identificar el evento que libera el semáforo del rol 6 y qué campos del evento lee `wifip2pd`.
2. Mapear los tipos 97 y 293 a ids apple80211.
3. Seguir las peticiones posteriores sobre el handle AWDL ya creado (`get/setAWDL_*`): `sub_100047660` solo gestiona el handle; las peticiones de configuración salen de otras funciones que usan el wrapper `sub_100046D30` vía `sub_100068C20`.
