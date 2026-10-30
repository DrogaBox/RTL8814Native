# Temporizador de ventanas AWDL para una radio sin firmware AWDL

Especificación del reloj de franjas que la capa del RTL8814AU tiene que llevar en el host para hacer lo
que en Broadcom hace el firmware: seguir el calendario del maestro, emitir los eventos 41/42/43 y saber
en qué canal estar en cada franja.

No contiene datos nuevos del kernelcache: reúne en un único contrato lo ya verificado en
[AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) (§2 eventos, §3.12 ioctl 229),
[AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) (ioctl 129) y
[AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) (TLV 4). Toma además el modelo de reloj de owly
(`crates/owly-awdl/src/sync.rs`, MIT), que mantiene ese calendario a menos de 3 TU del maestro con
temporizadores del host. Lo no comprobado en hardware va con `?`.

## 1. Unidades

| Magnitud | Valor | Origen |
|---|---|---|
| TU | 1024 µs | 802.11 |
| AW | 16 TU = 16 384 µs | TLV 4 `+8` = 16 en todas las capturas |
| Franja (slot) | 4 AW = 64 TU = 65 536 µs | TLV 4 `+30` (modo de presencia) = 4 |
| Secuencia | 16 franjas = 1 048 576 µs = 2^20 µs | TLV 18 |
| `AwNumber` | cuenta de AW, `u16` con desbordamiento | evento 41 `+0x00`; TLV 4 `+32` |
| `FwAwStartTime` | µs del inicio de la AW, `u32` big-endian | evento 41 `+0x14` |

`monitorTimeline` espera que, entre dos eventos 41, `FwAwStartTime` avance `(ΔAwNumber) << 14`, es
decir, 16 384 µs por AW. Esto confirma que la unidad del firmware es el µs. Tolera hasta 5120 µs de
error antes de registrar «AW start time inaccuracy», y solo registra
([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.3).

## 2. Estado del reloj

Basta con dos valores, como en owly:

- `base_us`: instante del host (en µs, del reloj monotónico) en que empezó la AW `base_aw`.
- `base_aw`: número de esa AW.

De ahí salen, para un instante `t`:

```
aw(t)        = base_aw + floor((t - base_us) / 16384)          (mod 2^16)
slot(t)      = (aw(t) >> 2) & 0xF
slot_start   = base_us + 65536 * floor((t - base_us) / 65536)  (si base_aw es múltiplo de 4)
next_slot_tu = 64 - floor(((t - base_us) / 1024) mod 64)       (lo que va en el TLV 4 +4)
```

El índice de franja coincide con el que calcula el manager:
`min(15, (AwNumber % ((step+1)·n)) / (step+1))`, con `step+1 = 4` y `n = 16`
([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2).

## 3. De dónde sale el calendario

### 3.1 La capa es el maestro

Es el caso con `peers:0` (`runElection` se elige a sí mismo; resultado en hardware en
[AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)). No llega ningún 229 (su
precondición es no ser maestro) y el reloj corre libre: `base_us` = instante de habilitación
(ioctl 117), `base_aw` = 0. La MAC de maestro y la de maestro raíz del evento 41 son la de `awdl0`.

### 3.2 Un par es el maestro

`programChipWithNewMaster` baja el ioctl 229 en cada cambio de maestro, con el último TLV 4 de ese
maestro ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §3.12):

```
límite de la siguiente franja = rx_time(AF del maestro) + dc · 1024 µs
AW que empieza en ese límite   = aw (sn del TLV 4)
```

`rx_time` es la hora de recepción en el reloj de la capa, no el `mactime` del 229. El `mactime`
viene del reloj de quien rellenó la cabecera de 72 B, que en este diseño es la propia capa. La capa
puede usar directamente su propia marca de recepción del último AF de esa MAC. `tx_delay` (diferencia
entre la TX real y la prevista del maestro) se resta para no arrastrar el retraso del emisor `?`
(signo y uso exacto por confirmar con un par real).

Mientras el maestro no cambia, el 229 no se repite. La capa debe reajustar el reloj con cada PSF/MIF
del maestro que reciba, leyendo `+4` (dc) y `+32` (sn) de su TLV 4 antes de inyectarlo. Es lo que hace
owly (`Clock::update`) y lo que hace el firmware de Broadcom. ESP32Drop añade un filtro de fase (PLL)
con los PSF, porque el reloj del ESP32 deriva (`awdl_gauge.h`).

### 3.3 El manager también fija el maestro con 134 y 155

Tras el 229 llegan el 134 (`master`, `op_mode`) y el 155 (parámetros de elección). La MAC del 134 es
la que debe salir en el evento 41 `+0x08`. Si sale otra, `monitorTimeline` registra
«Waiting for Master» o «Master change with no reason» y no corrige nada.

## 4. Qué hacer en cada franja

La secuencia propia la fija el ioctl 129: 16 entradas de 12 B con el canal en `+4`, y canal 0 en las
franjas que no se sirven. Al empezar cada franja:

| Canal de la franja en el 129 | Acción |
|---|---|
| ≠ 0 y es el canal de la radio | Emitir el evento 41, abrir la ventana y permitir TX |
| ≠ 0 y otro canal | Saltar de canal si la capa lo soporta; si no, tratar la franja como no servida. La secuencia anunciada debe llevar 0 ahí ([AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) §7) |
| 0 | Nada: ni evento ni TX |

`monitorTimeline` predice el siguiente `AwNumber` saltando a la siguiente franja con canal distinto
de cero de la secuencia vigente o de la anterior. Por eso la capa **solo debe emitir el 41 en las
franjas servidas**, con `AwNumber` = primera AW de la franja (múltiplo de 4). Si lo emitiera en todas,
el manager no encontraría discontinuidades, pero creería estar en ventana en franjas donde la radio
está en otro canal.

## 5. Eventos

### 5.1 Evento 41 al empezar una franja servida

Payload de 36 B ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2):

| Offset | Valor |
|---:|---|
| `+0x00` | `AwNumber` de la primera AW de la franja (`u16`) |
| `+0x04` | estado de sync (`u32`): 1 si la capa es maestra (§3.1), 2 si sigue a otro maestro (§3.2); 0 = parado, 3 = NeMaster ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2) |
| `+0x08` | MAC del maestro (la del 134, o la de `awdl0` si es maestra) |
| `+0x0e` | MAC del maestro raíz (`root` del 229, o la de `awdl0`) |
| `+0x14` | `FwAwStartTime` = inicio de la franja en µs, **big-endian** |
| `+0x18` | flags del canal (los de la entrada del 129) |
| `+0x1c` | número de canal |
| `+0x20` | flags de ventana (0) |

Efectos en el manager: `inAvailabilityWindow()` pasa a cierto y empieza a pedir paquetes
(`dequeueTxPacket`). Si la franja 0 empieza menos de 64 ms después de la anterior, el manager ignora
el evento («slot 0 start too soon»). Con franjas de 65,5 ms no ocurre, salvo que el temporizador se
adelante.

### 5.2 Evento 42

Opcional. Su única escritura es el contador de `ivars+408` que `dequeueTxPacket` compara con 4. Sin
firmware que extienda ventanas, la capa puede no emitirlo `?`.

### 5.3 Evento 43 al cerrar la ventana

Payload de 40 B; con todo a cero se omite la rama de CCA. Efectos: cierra la ventana (`ivars+772 = 0`)
y, si toca, ejecuta `runElection`. Es el único disparador periódico de la elección aparte del parseo
de los TLV 4 recibidos. Sin el 43 la elección no se reevalúa.

La capa debe cerrar la ventana como tarde al final de la franja (64 TU). La AW común dura 16 TU, y
las extensiones del TLV 4 (`03 03 03 03`, 3 AW) cubren el resto de la franja. Mantener la ventana
abierta toda la franja servida aprovecha que el par está en el canal durante las 4 AW.

## 6. Guardas de TX dentro de la ventana

owly (AWDL-16): unicast solo dentro de una AW compartida, a más de 3 TU de los bordes. Multicast con
16 TU de guarda en las franjas 0 y 10, o en cualquier franja servida con 3 TU si la radio no salta
de canal. La capa aplica estas guardas al vaciar la cola TX tras el evento 41, porque el manager no
las conoce: entrega los paquetes en cuanto `inAvailabilityWindow()` es cierto.

## 7. Precisión necesaria y fuente de tiempo

- El reloj del host basta: owly lo demuestra en Linux con marcas de llegada del host (≤ 3 TU), y
  winawdl en Windows con un temporizador de alta resolución, porque una espera normal de Windows se
  pasa hasta 16 ms.
- En el RTL8814AU hay TSF por puerto (`REG_TSFTR1` para el puerto 1) y marcas TSFT en el radiotap del
  RX ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §3). Usar la marca hardware del RX
  como `rx_time` elimina la latencia del USB en el cálculo de fase `?` (sin medir).
- El temporizador del host en XNU (`thread_call` con deadline) tiene una latencia no medida en este
  equipo `?`. El margen es de 5120 µs para que el manager no registre desfases y de 3 TU para que las
  tramas caigan dentro de la AW del par.

## 8. Orden de implementación

1. Reloj libre (§3.1) + eventos 41/43 en las franjas servidas: el manager deja de estar fuera de
   ventana y pide paquetes (TX de PSF/MIF según el borrador `AWDL_PSF_MIF_EMISSION.md`, en curso).
2. Con un ESP32-S3 cerca: si sigue al Mac como maestro, el reloj libre y la TX son coherentes
   ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §3).
3. Seguimiento de maestro (§3.2) cuando aparezca un par con mejor métrica (un iPhone o un Mac).
