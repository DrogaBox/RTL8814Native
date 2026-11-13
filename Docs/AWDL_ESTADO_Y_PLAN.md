# AWDL: estado y plan

Punto de situación a 2026-11-06, antes de la primera prueba con un iPhone. Reemplaza como referencia a
[AWDL_ESTADO_Y_BLOQUEO.md](AWDL_ESTADO_Y_BLOQUEO.md), de 2026-10-30, cuando no había ningún par por el aire. Lo no visto va con `?`.

**Actualización 2026-11-09.** La primera prueba con un iPhone (2026-11-08) está en
[AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md): AirDrop iPhone → Mac funciona de punta a punta (video de 64 MB a
unos 800 KB/s) tras corregir las tramas con envoltura de longitud de iOS 27 (94e83e7) y el modo de presencia (7333d24).
El Mac no descubría al iPhone porque la capa respondía «no soportado» al GET 117 (`AWDL_SYNC_ENABLED`), que wifip2pd lee
una vez al crear su interfaz y usa como permiso para publicar la presencia de los pares; corregido en b492c65 y
comprobado con el par virtual: la entrada de vecino de `awdl0` aparece sola, sin `ndp -s`. Pendiente con el iPhone:
que aparezca en la hoja de AirDrop del Mac y la velocidad Mac → iPhone. Capa cargada: UUID `5A7BC8B5`, md5 `233601f6`.
Referencia nueva para la velocidad: `github.com/Dino-VN/MtkWireless` (AWDL por software con AirDrop a unos 4 MB/s en
MT7921): escalera de tasas hasta MCS 7, Block Ack de recepción y presencia densa durante la transferencia.

**Actualización 2026-11-09, tarde.** Cambios de velocidad sin iPhone, en
[AWDL_RENDIMIENTO_1.md](AWDL_RENDIMIENTO_1.md): la capa contesta el ADDBA del iPhone y reordena, anuncia todos los
slots libres durante una transferencia, y sube la tasa de TX con el reporte CCX por trama del chip (driver UUID
`9B662F1B`, md5 `d06f7b4a`; capa UUID `8FB89DDD`, md5 `cfd4f92e`). Sin instalar todavía; se prueban primero con
`awdlsim --addba` y después en una sola visita del iPhone, junto con la hoja de AirDrop del Mac.

## 1. Lo que corre

macOS 26.7.1 (25G241), T9UH. El router pasó solo del 149 al 153 el 2026-11-06 a las 17:58, así que el caso de la prueba se decide ese día.

| Pieza | Compilación |
|---|---|
| `RTL8814Native.kext` | UUID `DB92193C`, md5 `9440a90b` |
| `RTL8814AirLayer.kext` | UUID `001EF268`, md5 `540b6837` (2026-11-06); `5A7BC8B5`, md5 `233601f6` desde 2026-11-09 |
| Kit de la prueba | `Tools/iphone_test.sh`, `Tools/iphone_frames.py`, commit `253929e` |

`Tools/iphone_test.sh check` da `READY`. El ensayo sin par del mismo día dio 2743 tramas TX, 0 perdidas, la autoprueba de captura correcta y el resumen completo.
Las dos ya están instaladas, y `deploy` hace falta solo si cambia alguna. El script es de bash: con `zsh` da un `NOT READY` falso.

## 2. Lo medido

| Tramo | Resultado | Con qué | Dónde |
|---|---|---|---|
| Creación de `awdl0`, `AWDL ON`, secuencia de canales | Funciona | El propio Mac | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) |
| Sincronía y elección | Dentro de 1 TU, en los dos papeles | Par virtual y A53 | [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §9, [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §10 |
| PSF y MIF al aire | 8,80 PSF/s y 2,83 MIF/s en el 149, en el rango del iPhone | A53 en monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §9 |
| TLV 7 y 17 en las tramas del Mac | Salen desde la capa `EEEE65F6` | A53 en monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §6 |
| RX por el aire | `HEARD` y `peers 1` con PSF y MIF del A53 | A53 de par | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §7 |
| ACK por hardware a la MAC de `awdl0` | 482 tramas, 482 ACK, sin reintentos | Router, unicast | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §13 |
| Tamaño de paquete | `awdl0` en 1428, lo que cabe en el anillo | A53 en monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §14 |
| Posición de los datos en la franja | Guarda de 3 TU al inicio de la franja; en `Data` el primer grupo aparece hacia el TU 2,5 | A53 en monitor | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §7 |
| Modo `Data` del manager | Pasa a `Data` a los 1,3 s de tráfico. Subida TCP de 113 a 507-542 KB/s | Par virtual | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5 |
| Datos retenidos si el par anuncia otro canal | 113 KB/s solo en las franjas 0, 2 y 10 con un par en reposo, y la red de `en2` sin pérdidas | Par virtual con `--rest` | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9 |
| Coste sobre la red de `en2` | 430 tramas/s al router: 0 % de pérdida, 3,7 ms de media, 16 ms como máximo | Router y A53 | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9 |
| AirDrop hacia el Mac | 4 MiB en unos 2 s, archivo idéntico | Puerto de simulación, no por el aire | [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §5 |
| Escaneos | `system_profiler SPAirPortDataType` deja a AWDL 9,3 s callado; el kit ya no lo usa | Registro del sistema | [AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md) §4 |
| `en2` sin asociar | 8,89 PSF/s y 1,90 MIF/s en las franjas 2 y 10, con 3,5 s de silencio cada 60 s por el escaneo de `locationd` en la capa `2E3D2058`; 0,38 s con la `001EF268` (2026-11-07) | A53 en monitor | [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17 |

## 3. Lo que solo responde un par real

- ~~Si el iPhone acepta los PSF y MIF del Mac y lo muestra en AirDrop.~~ Sí (2026-11-08): el Mac aparece y recibe.
- Si el iPhone da ACK a los datos del Mac, y si usa Block Ack. Pide ADDBA hacia el Mac `?` (MtkWireless lo documenta en
  su hardware; en el T9UH no medido).
- Datos por el aire, en cada sentido. El A53 no emite datos: su firmware los rechaza o se cae
  ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §11).
- Si el número de franja del iPhone coincide con el del Mac. Si no coincide, la capa no retiene datos y envía igual
  ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9).
- La velocidad real y el valor de `held`.

## 4. Plan

### 4.1 Hasta la visita

El código queda congelado. Un cambio en la capa pide despliegue, reinicio y otro ensayo, y la prueba rinde más sobre un binario que ya se conoce.
La excepción posible es el silencio de 3 s tras un escaneo sin asociación ([AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) §17). No impide la prueba, pero con el router en el 153 es el caso que aplica. Aplicado en la capa `001EF268` (6741f35): el silencio bajó a 0,38 s y `en2` se vuelve a asociar al primer intento.

El mismo día, antes de que llegue el iPhone:

1. `Tools/iphone_test.sh check`, que debe dar `READY`.
2. Mirar el canal de `en2`, porque el router cambia por su cuenta. `check` dice en qué caso está.
3. `awdlsim` parado y el anuncio BLE del A53 apagado.

### 4.2 La visita

[AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md): 15 a 20 minutos, con `sudo Tools/iphone_test.sh run` en marcha. Si hay tiempo, el A53 queda de testigo con
`telefono/vigia_mac.sh`, porque separa un iPhone que no emite de una trama que se emite y no llega a la capa.

### 4.3 Después, según lo que pase

| Resultado | Siguiente trabajo |
|---|---|
| Ningún `HEARD` | Ver con el testigo si el iPhone emitía y en qué canal. Repetir sin asociar o en el 6. |
| `HEARD` sin `peers 1` | Comparar las tramas guardadas con el rechazo del manager (`BAD OUI`, `Invalid frame`) y con [AWDL_AF_VS_IPHONE.md](AWDL_AF_VS_IPHONE.md). |
| `peers 1`, y el Mac no aparece en el iPhone | Comparar TLV a TLV los PSF y MIF del Mac con los del iPhone en `frames.pcapng`. Se puede seguir sin el iPhone a mano y repetir en otra visita. |
| El Mac aparece, sin `TCP to 8770` | Resolución y datos unicast: `ndp -an`, mDNS en `awdl0_ip.pcap`, y los contadores `sent`, `dropped`, `fail`. |
| Pasa un archivo, en un sentido o en los dos | Sigue §4.4 con las cifras reales. |
| **Resultado real (2026-11-08):** pasa iPhone → Mac; el iPhone no aparece en el Mac | Causa y corrección en [AWDL_RESULTADO_IPHONE_1.md](AWDL_RESULTADO_IPHONE_1.md) §3. Siguiente visita: hoja de AirDrop del Mac y velocidad Mac → iPhone. |

Aunque la prueba falle pronto, los archivos de la corrida bastan para seguir. La carpeta lleva el nombre y las direcciones del iPhone y no va a ningún repositorio.

### 4.4 Con datos por el aire

En este orden, porque cada punto depende de una cifra del anterior:

1. Comprobar la alineación de franjas y `held` con el iPhone, y corregir la retención si hace falta.
2. Que el presupuesto de aire cuente los reintentos. Hoy no lo hace, y un par que pierde tramas todavía puede llenar el aire (`?`, [AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §9).
3. Subir la tasa de datos. A 12 Mb/s el techo es de 568 KB/s ([AWDL_MODO_DATOS.md](AWDL_MODO_DATOS.md) §6), y pasarlo pide elegir otra tasa en la radio.
4. Tiempo compartido por el aire con la red fuera de los canales sociales ([AWDL_MCC_TIEMPO_COMPARTIDO.md](AWDL_MCC_TIEMPO_COMPARTIDO.md) §8), y la secuencia de `Data` en ese caso, sin medir.

### 4.5 Otras versiones de macOS

El objetivo final es macOS 14 en adelante, y todo lo medido es de 25G241. Cada build necesita su disposición de vtables y sus offsets
([AWDL_CARD_CAPS_14_15.md](AWDL_CARD_CAPS_14_15.md), [AWDL_DATA_PATH_PORT_14_15.md](AWDL_DATA_PATH_PORT_14_15.md)). Se hace cuando el camino esté probado con un par real en 26.7.1, para portar
algo que ya se sabe que funciona.
