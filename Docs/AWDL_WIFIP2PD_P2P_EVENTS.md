# Monitoreo de eventos P2P en wifip2pd (Apple80211RawP2PEventMonitoringInit)

Cadena completa del registro y la clausura de eventos P2P, con las 16 ids de infraestructura como referencia. Direcciones = VA de `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), sesión rea/IDA adjunta 2026-10-28. Lo no visto va como `?`.

## 1) Quién lo crea: `sub_100041B60` (método de `AppleDevice.InterfaceHandle`)

`sub_100041B60` (0x34c bytes, `0x100041b60`) es el punto donde se arma el monitoreo de eventos; opera sobre la clase Swift anidada `_TtCC7CoreP2P11AppleDeviceP33_31FB922BC8C13D419BA8418A9FE51E1A15InterfaceHandle` (ivars vistos: `queue`, `eventBuffer`, `role`, `reference` — `_OBJC_IVAR_$…InterfaceHandle_queue/eventBuffer/role/reference` en `0x100041bbe`, `0x100041c5a`, `0x100041ccd`, `0x100041d7f`).

Secuencia observada:

1. Precondición de cola con `DispatchPredicate.onQueue` (`0x100041c37-0x100041c54`).
2. (Re)asigna el event buffer: `swift_slowAlloc(0x800)` (`0x100041c9b-0x100041ca3`), guarda puntero en `eventBuffer` y `eventBuffer+0x100` (`0x100041ca7-0x100041cb2`) — dos áreas de 256 B.
3. Elección de la vía de registro (`0x100041ccd-0x100041d5c`):
   - si `role == 0` **y** la clase de la cola es exactamente `NSObject` (`NSObject.==`, `0x100041d30`) → **`sub_100041EB0`** con flag = 1 (`0x100041d52`);
   - en cualquier otro caso (`role != 0`, o cola no-NSObject) → **`sub_100041F20`** con flag = 0 (`0x100041d69`).
4. Llamada común con firma `(reference, queue, sub_1000685A0, ctx)` (`0x100041dcf-0x100041df0`), donde `ctx` es un objeto Swift de 0x30 B (`metadata stru_100515C68`, `0x100041d9d`) con campos `[2]=a2`, `[3]=a3`, `[4]=eventBuffer`, `[5]=eventBuffer+0x100` (`0x100041da4-0x100041db8`). `sub_1000685A0` es el callback C que el framework invocará por evento.
5. Sólo en la vía `sub_100041EB0` (flag 1) y sin error: bucle de **16 ids** (`0x100041e42-0x100041e76`): lee el qword `&off_100515C80[4 + i]` (i = 0..15; es decir `0x100515CA0 + 8*i`), verifica `id >= 0` y high32 == 0 (`0x100041e5e`, `0x100041e90`) y llama `Apple80211StartMonitoringEvent` por cada uno (`0x100041e6c`).
   - Los 16 valores (referencia ya documentada, leída de la memoria del IDB en sesión previa — `AWDL_WIFIP2PD_DISCOVERY.md` §3): **1, 11, 59, 2, 3, 125, 126, 127, 9, 66, 74, 78, 82, 215, 55, 160**. No hay string que nombre ninguno de estos ids (no se les pone nombre). Nota: en el archivo, los qwords de `0x100515CA0` están codificados como chained fixups (decodifican a punteros `0x100069160`, `0x100069480`, `0x1004ED910`, …); la lectura exacta de los ids requiere la memoria del IDB y no se re-verificó en esta pasada (`?` sobre el encoding, no sobre la lectura previa).
   - Corrección menor al doc previo: el registro con `Apple80211StartMonitoringEvent` corresponde a la rama `role == 0 && cola NSObject` (flag 1), no a "todo rol 0" sin condición de cola.

Callers de `sub_100041B60`: `sub_1000421E0` (`0x1000422ec`; es la misma función que la cadena de recuperación llama en `0x10004724e` desde `sub_100046E30`), `sub_100049F20` (`0x100049ff0`), `sub_10004A0C0` (`0x10004a15c`). Callers de esos tres: `?` (no revisado en esta pasada).

## 2) La vía P2P: `sub_100041F20` → `sub_100041F40` → bloque ObjC

- `sub_100041F20` (0x18 B, `0x100041f20`): wrapper puro → `sub_100041F40(a1, a2, a3, a4, &unk_100515D48, &Apple80211RawP2PEventMonitoringInit)` (`0x100041f32`). El stub de `Apple80211RawP2PEventMonitoringInit` lo referencia como datos (`a6`). El blob `&unk_100515D48`: en el archivo contiene qwords codificados como chained fixups que decodifican a punteros (`0x100069180`, `0x1000694A0`, `0x1004EC8E0`, …); su significado no queda determinado (`?`).
- `sub_100041F40` (0x9d B, `0x100041f40`): construye un bloque ObjC en pila: `{isa = _NSConcreteStackBlock, flags = 1107296256 (0x42000000), invoke = sub_100041ED0, context = a5}` (`0x100041f68-0x100041f7f`) y captura en `bloque+32` el puntero a función Swift `a3` y en `bloque+40` el objeto `a4` (`0x100041f59-0x100041f5d`). Si `a3 == 0`, el bloque es nil (no hay monitor, `0x100041fb9`). Luego `_Block_copy` (`0x100041f99`) y llama `a6(a1, a2, bloque)` = `Apple80211RawP2PEventMonitoringInit(a1, a2, bloque)` (`0x100041fc2-0x100041fc7`), con `_Block_release` al salir (`0x100041fc7`).
- `sub_100041ED0` (0x47 B, `0x100041ed0`) — **el invoke del bloque, es decir la "clausura que recibe (tipo de evento, datos, longitud)"**: firma observada `(bloque, tipo u32, datos i64, longitud u32)`; lee `fn = *(bloque+32)` (la closure Swift capturada), retiene `*(bloque+40)` (`0x100041ee6-0x100041ef1`) y llama `fn(tipo, datos, longitud)` (`0x100041efe`), liberando después (`0x100041f12`).

## 3) Dónde está el switch de ids de evento

**Ni el callback C ni el bloque distinguen ids.** `sub_1000685A0` (callback C pasado por `sub_100041B60`) y `sub_100041ED0` (invoke del bloque P2P) sólo reenvían `(tipo de evento, datos, longitud)` a la closure Swift que se capturó al registrar. Esa closure Swift — la que hace switch/compare sobre el id y consumiría el evento que libera el semáforo de 30 s del `VIRTUAL_IF_CREATE` — **no quedó localizada en esta pasada** (`?`); coincide con lo ya documentado en `AWDL_WIFIP2PD_REQUESTS.md` ("el que lo consume y llama a `semaphore.signal` queda por localizar"). Las ids que distingue esa closure dependen del llamador de `sub_100041B60`/`sub_100041F20`; para la vía infraestructura el conjunto registrado es el de las 16 ids de arriba; para la vía P2P los ids que acepta el framework en `RawP2PEventMonitoringInit` no están en el binario wifip2pd (`?`).

## 4) Referencia: las 16 ids de infraestructura

Documentadas en `AWDL_WIFIP2PD_DISCOVERY.md` (lectura de memoria del IDB, sesión previa) y confirmadas aquí como el conjunto que `sub_100041B60` registra en la rama `sub_100041EB0`:

| # | id (decimal) |
|---:|---:|
| 1 | 1 |
| 2 | 11 |
| 3 | 59 |
| 4 | 2 |
| 5 | 3 |
| 6 | 125 |
| 7 | 126 |
| 8 | 127 |
| 9 | 9 |
| 10 | 66 |
| 11 | 74 |
| 12 | 78 |
| 13 | 82 |
| 14 | 215 |
| 15 | 55 |
| 16 | 160 |

Sin nombres: no existe string en el binario que los identifique (verificado en el inventario de strings de `AWDL_WIFIP2PD_LOGSTRINGS.md`; no se inventan).
