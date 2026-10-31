# El firmware del RTL8814A y los comandos H2C de MCC

Cierra el `?` de `AWDL_MCC_TIEMPO_COMPARTIDO.md` §2: si el firmware que el driver descarga al
RTL8814AU implementa los H2C del modo MCC (`hal_com_h2c.h`: `H2C_MCC_LOCATION` 0x10,
`H2C_MCC_RQT_TSF` 0x15, `H2C_MCC_MACID_BITMAP` 0x16, `H2C_MCC_CTRL_V2` 0x17, `H2C_MCC_CTRL`
0x18, `H2C_MCC_TIME_SETTING` 0x19, `H2C_MCC_IQK_PARAM` 0x1A) y el `C2H_MCC` 0x17. Se inspecciona
el blob estáticamente, sin hardware. Las direcciones del firmware se dan como desplazamientos
dentro de las secciones extraídas (la base de datos de desensamblado usa IRAM en 0x80000000 y
DMEM en 0x80200000). Lo no verificado se marca `?`.

**Respuesta corta: no.** El dispatcher de H2C del firmware ignora 0x10 y 0x15–0x1A (caen a la
ruta por defecto, que solo enciende un bit de error) y ninguna ruta emite un `C2H_MCC` de MCC.
La vía `CONFIG_MCC_MODE` de `hal_mcc.c` quedaría muerta con este blob; la ruta manual por slots
de `AWDL_MCC_TIEMPO_COMPARTIDO.md` §7 es la única viable sin cambiar de firmware.

## 1. Qué imagen baja el driver y cómo

`FirmwareDownload8814A` (`Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c`) toma
`array_mp_8814a_fw_nic` (línea 734; solo usa `array_mp_8814a_fw_wowlan` si `bUsedWoWLANFw`), lee
el header con las macros `GET_FIRMWARE_HDR_*_3081` (64 bytes) y copia por IDMA (`IDDMADownLoadFW_3081`)
la sección DMEM (desde el offset 64) a `OCPBASE_DMEM_3081` (0x00200000) y la sección IRAM (al final
del fichero) a `OCPBASE_IMEM_3081` (0x00000000), cada una con 8 bytes de suma de comprobación al
final (`FW_CHKSUM_DUMMY_SZ`). El arranque del MCU espera `DMEM_CHKSUM_OK`/`IMEM_CHKSUM_OK` en
`REG_8051FW_CTRL_8814A` (0x80) y `CPU_DL_READY` (BIT15). El nombre del registro hereda el "8051";
el código del firmware no lo es (§3).

## 2. La imagen extraída

| Campo del header (64 B) | Valor |
|---|---|
| Firma | 0x8814 |
| Categoría / función | 0x00 / 0x00 |
| Versión / subversión / índice | 33 / 6 / 0 |
| Fecha | 2019-06-11 21:15 |
| Uso de memoria (byte 24) | 0x08 (solo IRAM, sin boot loader propio) |
| `TOTAL_DMEM_SZ` (offset 36) | 0x1698 (5784 B) |
| `IRAM_SZ` (offset 48) | 0xF3F8 (62456 B) |
| Tamaño total | 68320 = 64 + (5784+8) + (62456+8) ✓ |
| Palabras 32/56/60 | 0x80200000 / 0x80121000 / 0x80000000 |

Las palabras 0x80200000 y 0x80000000 coinciden con las del firmware de la misma generación de
RTL8822B (`rtw8822b_fw.bin` de linux-firmware, v30 del 2020), cuyo header es idéntico campo a
campo; los dos blobs no comparten ni un bloque de código de 64 bytes, así que la comparación con
el 8822B no aporta handlers, solo confirma el formato de imagen.

Volcados: `~/re_cache/ida/task_fw_mcc/fw_nic.bin`, `fw_nic_dmem.bin` (5792 B), `fw_nic_iram.bin`
(62464 B) y `fw8814.i64` (base de desensamblado, 425 funciones; ~12 KB de IRAM siguen sin
decodificar, `?`, pero el camino H2C/C2H completo está dentro de lo analizado).

## 3. El procesador es un MIPS pequeño con código MIPS16e

No es un 8051: no hay ni un `MOV DPTR,#imm16` hacia el espacio de registros ni los patrones
propios del 8051 en 62 KB. Lo que sí hay:

- Tablas de la DMEM llenas de punteros kseg0 (`0x8000xxxx`) con el bit 0 a 1 — el bit ISA de
  MIPS16 — apuntando dentro del IRAM (vectores de excepción en DMEM 0x08–0x40, y tablas de
  tareas en 0x5C, 0x84, 0xB8, 0xCC, 0xE0, 0xF4, 0x110, 0x158, 0x24C, 0x26C, 0x2A8, 0x2BC,
  0x314, 0x398).
- El NOP de MIPS16e (halfword 0x6500, LE `00 65`) entre los halfwords más frecuentes.
- Accessors de registro con base variable: la DMEM 0x4BC guarda 0xB8600000 y la 0x4C0 guarda
  0xB8640000; los accessors genéricos `sub_80006AAC` (lectura: `base + (reg & 0xFFFF)`),
  `sub_80006AE4` (escritura con máscara de campo) y `sub_80004478`/`sub_800058F0` (por bytes)
  suman el offset a esa base. La escritura de `0x8000A640` elige 0xB8600000 cuando
  `reg & 0xFF00` es 0x0000 o 0x1000 y 0xB8640000 en el resto, así que 0xB86010F8 (§5, §6) es el
  registro MAC 0x10F8 `?` (sin nombre en el árbol del driver).
- Los descriptores de la DMEM son pares (offset, base) que casan con registros reales del árbol
  del driver: (0x550,B864)=`REG_BCN_CTRL`, 0x551=`REG_BCN_CTRL_1`, 0x554=`REG_MBSSID_BCN_SPACE`,
  0x558=`REG_DRVERLYINT`, 0x6A8=`REG_BCN_PSR_RPT`, 0x4A4=`REG_ARFR5`, 0x44C=`REG_ARFR1`,
  0x1C0=`REG_MCUTST_1`, 0x1C7=`REG_MCUTST_WOWLAN`, 0x1000=`REG_SYS_CFG3` y (0x80,B860)=
  `REG_8051FW_CTRL`.

Además de B8600000/B8640000 hay ventanas B8660000, B8700000 y B8780000, separadas 0xA0000 y
0x80000 igual que `OCPBASE_RPTBUF`/`RXBUF`/`TXBUF` del driver (0x18660000/0x18700000/0x18780000):
son el informe, la RX y la TX buffer vistas por el MCU. `?` el mapeo exacto B8600000↔0x18600000.
Si el chip lleva además un 8051 que solo corre ROM (FWDL y cola), nada en esta imagen lo muestra.

## 4. El camino H2C: cola en DMEM, no lectura del mailbox

El driver escribe el comando en el mailbox de siempre (`FillH2CCmd_8814`, `rtl8814a_cmd.c:60`:
`REG_HMEBOX_0..3` = 0x1D0–0x1DF con id en el byte 0 y 3 bytes de carga, extensión en
0x1F0–0x1FF) y espera que el hardware baje el bit de `REG_HMETFR` (0x1CC).

El firmware cargado no lee ninguno de esos registros: ni inmediatos 0x1A0–0x1FF en las 425
funciones, ni descriptores (0x1D0–0x1DF y 0x1F0–0x1FF no aparecen en la DMEM), ni palabras
absolutas 0xB86x01D0/0xB86x01A0. Lo que sí hay:

- Una cola de comandos en la DMEM: entradas de 8 bytes `[id][7 args]` a partir de
  0x802017C7 + 8·i (la base de datos del firmware está en 0x802016B0), con índices head
  (0x80201807) y tail (0x80201808), reiniciados por el init `sub_80001614` y `sub_8000172C`.
- El drenador `sub_80005834`, parte del bucle principal `sub_800016EC`, saca la entrada cuyo id
  tiene y llama al dispatcher `sub_80005668` con (id, args).
- Quién llena la cola no está en la imagen: el productor es el propio hardware (DMA del mailbox
  a la cola) o el ROM del MCU `?`. Es coherente con que el hardware borre el bit de HMETFR que
  el driver espera, y con que esta generación (8192F/8822B) use un H2C "por paquetes".

## 5. El dispatcher `sub_80005668`: mapa completo de ids

Árbol de comparaciones sobre el byte de id (direcciones = IRAM). "Default" = `loc_8000581C`:
pone BIT0 en 0xB86010F8 (puntero en la DMEM 0x4E4; el mismo registro donde el remitente C2H
pone BIT9 cuando su anillo está lleno, §6) y no hace nada más. `loc_8000582C` = salida
silenciosa, sin bit de error. Antes del árbol, los ids 0x40–0x5F van todos a la clase DM
(`(id − 0x40) & 0xFF < 0x20` → `loc_8000B2DC`).

| Id (driver) | Handler | Nota |
|---|---|---|
| 0x00 `RSVD_PAGE` | 0x800054E4 | posiciones de plantillas (probe rsp, ps-poll, null, QoS null) |
| 0x01 `MEDIA_STATUS_RPT` | 0x80005528 | |
| 0x0B | 0x80001040 | id 8814A sin equivalente en `hal_com_h2c.h` `?` |
| 0x14 | salida silenciosa | |
| **0x10 y 0x15–0x1B** | **default** | **incluye los siete ids MCC (0x1B no es MCC pero cae igual)** |
| 0x1C `CHNL_SWITCH_OPER_OFFLOAD` | 0x8000812C | canal/ancho por `sub_800070FC`/`0x800070B4` + C2H 0x10 |
| 0x1D `SINGLE_CHANNELSWITCH_V2` | 0x80008128 | cuerpo vacío (`jr ra`) |
| 0x1E | 0x80000FE4 | emite el C2H 0x20 |
| 0x1F | 0x80000E80 | config por campos de registro |
| 0x20 `SET_PWR_MODE` | 0x80005500 | LPS/SmartPS del firmware |
| 0x21, 0x23, 0x25 | salida silenciosa | `PS_TUNING_PARA`, `P2P_LPS_PARAM`, `PS_SCAN_ENABLE` aceptados sin efecto |
| 0x22, 0x26 | default | |
| 0x24 `P2P_PS_OFFLOAD` | 0x8000116C | |
| 0x27 `INACTIVE_PS` | 0x80001130 | |
| 0x2C `DEFAULT_PORT_ID` | 0x80001060 → 0x800059C0 | contexto por puerto (multi-port) |
| 0x40–0x4B (clase DM) | 0x8000B2DC → `jr` por la tabla DMEM 0x110 (índice `id − 0x40`) | 0x40 `MACID_CFG`→0xABB0, 0x41 `TXBF`→0x77FC, 0x42 `RSSI_SETTING`→0xB108, 0x43 `AP_REQ_TXRPT`→0xA7FC, 0x46 `RA_MASK_3SS`→0xB2B4, 0x47 `RA_PARA_ADJUST`→0xAE7C, 0x48 `DYNAMIC_TX_PATH`→0xAAFC, 0x49 `FW_TRACE_EN`→0xB260, 0x4B→0xB020 (seguido de 0xA7FC `?`); 0x44, 0x45 `IQ_CALIBRATION` y 0x4A apuntan a la salida |
| 0x4C–0x5F | salida silenciosa de la clase DM | (incluye `REQ_PER_RPT` 0x4E) |
| 0x60/0x61 BT | 0x80004974 / 0x80004A18 | |
| 0x62–0x68 BT | 0x4A04 / 0x49F0 / 0x4934 / 0x4708 / 0x49DC / 0x49AC / 0x4998 | |
| 0x69 `BT_WIFI_CTRL` | 0x80004A2C | |
| 0x6A/0x6B | default | (sin BT_FW_PATCH ni BTC_WL_PORT_ID) |
| 0x6C–0x71 BT | 0x4984 / 0x473C / 0x47F4 / 0x4830 / 0x4880 / 0x4860 | |
| 0x87 `CHNL_SWITCH_OFFLOAD` (WoWLAN) | 0x8000817C | canal + verificación + C2H 0x10 |
| 0xC5 `BCNHWSEQ` | 0x800053EC | el que el driver usa para el seq HW de baliza |
| resto (0x02–0x0A, 0x0C–0x13, 0x28–0x2B, 0x2D–0x3F, ≥0x72 sin 0x87/0xC5) | default | |

Los nombres de la clase DM son los del enum del driver para ese id; la función de cada handler
no se analizó (`?`). Lo que importa aquí no cambia: los ids MCC no llegan a ningún handler.

## 6. El C2H: anillo en DMEM y ids emitidos

El remitente común es `sub_80004CC8`: marca BIT2 en la palabra de estado 0x8020173C
(0x802016B0 + 0x8C) y copia 16 bytes al anillo de eventos en 0x802016B0 + 0x15A + 16·i (índice
de lectura en 0x80201D0A, de escritura en 0x80201D0B, 80 entradas, vuelta en 0x50). Si el anillo
está lleno no copia nada y pone BIT9 (0x200) en 0xB86010F8: es un aviso de desborde, no la
notificación normal. El formato del buffer de cada llamante es el clásico
[id][seq][len][payload...]. `?` cómo viaja la ranura al host (en USB el driver recibe los C2H
como paquetes por la ruta de RX).

Ids emitidos por los 12 llamantes de `sub_80004CC8` (byte 0 del buffer, constante salvo donde se
indica):

| Id | Nombre en el driver | Llamante |
|---|---|---|
| 0x02 | `C2H_TXBF` | `sub_800075E4` |
| 0x04 | `C2H_AP_REQ_TXRPT` | función en 0x8000A9A0 (sin límites en la base) |
| 0x09 | `C2H_BT_INFO` | `sub_80008910` |
| 0x0A | — `?` | `sub_800088DC` |
| 0x0B | `C2H_BT_MP_INFO` | `sub_80008878` |
| 0x10 | `C2H_FW_CHNL_SWITCH_COMPLETE` | `sub_8000812C` (H2C 0x1C) y `sub_8000817C` (H2C 0x87) |
| 0x15 | `C2H_MAILBOX_STATUS` | función en 0x8000886E (sin límites en la base) |
| 0x1E | `C2H_BCN_EARLY_RPT` | `sub_80002884` |
| 0x20 | — `?` | `sub_80000FE4` (H2C 0x1E) |
| 0x21 | — `?` | `sub_80000F0E` (lleva 0x478/0x484/0x4C8/0x4D4 y estado interno) |
| varios | 0x00 `C2H_DBG`, 0x0C `C2H_RA_RPT` y otros tomados de estructuras `?` | wrapper `sub_8000A684` (8 llamantes, rutas de la clase DM) |

Ninguno escribe 0x17 como id constante; los ids del wrapper que salen de estructuras no se
siguieron (`?`).

**El 0x17 que aparece en el firmware no es un C2H.** El H2C 0x62 (`FORCE_BT_TXPWR`) llama
`sub_80004A04` → `sub_800089E0(0x17, 1, args)`. Ese compositor no usa el anillo: tras un
conmutador por id (0x11, 0x1B, 0x23, 0x28, 0x30 tocan banderas en 0x80201B90/91) y una
comprobación de `*(u16 *)0x80203240 & 0x2AA`, llama `sub_80008760`, que escribe el id en
0xB8003400, la longitud en 0xB8003401 y la carga desde 0xB8003402, y dispara con BIT0 de
0xB8003408 (punteros en la DMEM 0x8D4–0x8E0). Es otra ventana, fuera de los registros MAC, y
solo la usan los handlers BT (0x62–0x68, 0x6C, ids 0x11, 0x17, 0x1B, 0x23, 0x28, 0x30, 0x34,
0x38): con toda probabilidad el buzón hacia el núcleo BT `?`. Con MCC compilado,
`rtw_hal_mcc_c2h_handler` no recibiría nunca un C2H 0x17. El driver 8814A
(`c2h_handler_8814a`, `rtl8814a_cmd.c:725`) solo maneja 0x02/0x03/0x04/0x0D por su cuenta; el
resto va al genérico (`hal_intf.c:1242` es la rama de `C2H_MCC`).

## 7. Respuesta directa a la pregunta del MCC

| Comando | Id | ¿Lo implementa el firmware 33.6? | Evidencia |
|---|---|---|---|
| `H2C_MCC_LOCATION` | 0x10 | No | dispatcher: default (`loc_8000581C`) |
| `H2C_MCC_RQT_TSF` | 0x15 | No | default |
| `H2C_MCC_MACID_BITMAP` | 0x16 | No | default |
| `H2C_MCC_CTRL_V2` | 0x17 | No | default |
| `H2C_MCC_CTRL` | 0x18 | No | default |
| `H2C_MCC_TIME_SETTING` | 0x19 | No | default |
| `H2C_MCC_IQK_PARAM` | 0x1A | No | default |
| `C2H_MCC` | 0x17 | No (el único 0x17 es un mensaje del buzón BT, no un C2H) | §6 |

Compilar `CONFIG_MCC_MODE + CONFIG_CONCURRENT_MODE` y añadir `hal/hal_mcc.c` no tendría efecto
funcional: los siete H2C serían tragados por el default (con el bit de error encendido en
0xB86010F8) y jamás llegaría el C2H 0x17 con el estado de MCC. La prueba barata en hardware que
propuso `AWDL_MCC_TIEMPO_COMPARTIDO.md` §7.4 queda respondida sin gastarla.

## 8. Qué ofrece el firmware que sí importa para AWDL

- **Conmutación de canal en firmware**: H2C 0x1C (`CHNL_SWITCH_OPER_OFFLOAD`, args con canal,
  ancho y offset empaquetados) y 0x87 (variante WoWLAN, con lectura de verificación del canal
  nuevo), ambos cerrando con `C2H_FW_CHNL_SWITCH_COMPLETE` (0x10). Es el "switch offload"
  que `AWDL_MCC_TIEMPO_COMPARTIDO.md` §3.3 descartó por `RegFWOffload == 2`; existe en el
  firmware, aunque el driver del árbol no lo usa y su latencia real es `?` (medir antes de
  predecir). El 0x1D, en cambio, es un stub vacío.
- **LPS/SmartPS** (H2C 0x20): el firmware genera él solo los null/PS-Poll de ahorro sobre las
  plantillas de `RSVD_PAGE` (0x00). Sirve para el anuncio PM en el canal casa, no para un
  horario de slots: el apagado/encendido por slot sigue siendo tarea del driver.
- **P2P_PS_OFFLOAD** (0x24) y **DEFAULT_PORT_ID** (0x2C) con conmutación de contexto por puerto
  (`sub_800059C0`): el esqueleto multi-port existe, con `?` en si sincroniza TSF por puerto.
- **IQK en firmware** (0x45), MACID_CFG (0x40), RSSI_SETTING (0x42), AP_REQ_TXRPT (0x43),
  RA_MASK_3SS (0x46), DYNAMIC_TX_PATH (0x48), BCNHWSEQ (0xC5).
- **Beacon early interrupt**: el firmware programa `REG_DRVERLYINT` (0x558, descriptor en la
  DMEM 0x400) y emite `C2H_BCN_EARLY_RPT` (0x1E); el mecanismo de aviso temprano de baliza está
  disponible para sincronizar slots contra el DTIM del AP.
- **TSF**: el firmware toca sus propios TSF (0x560–0x57F aparecen en descriptores, p. ej.
  0x578–0x57A junto a `REG_BCN_CTRL`), pero no ofrece al host ninguna lectura de TSF sin MCC:
  `H2C_RQT_TSF` (0x15) no está manejado. El host sigue leyendo sus registros TSF como hasta
  ahora (`AWDL_RTL8814_HW_SUPPORT.md`).

## 9. Veredicto

El firmware que baja el T9UH (v33.6, 2019-06-11) no implementa el bloque MCC de esta generación:
los siete H2C (0x10, 0x15–0x1A) caen al default con bit de error y ninguna ruta emite un
`C2H_MCC`: el único 0x17 es un mensaje del buzón BT. Para el tiempo compartido AWDL mientras la estación está
asociada, la vía de firmware MCC queda descartada con este blob; siguen en pie la ruta manual por
slots (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §7.1–7.3) y, como línea futura condicionada a medir, el
offload de conmutación 0x1C/0x87 que el firmware sí trae.

Método y artefactos: extracción del array C a binario, troceo por el layout del propio driver,
base de desensamblado MIPS/MIPS16e de IRAM y DMEM con las tablas de la DMEM convertidas en
xrefs, y lectura del dispatcher, la cola y el remitente C2H. Todo queda en
`~/re_cache/ida/task_fw_mcc/` (`fw8814.i64`, `all_disasm.txt`, `fw_nic*.bin`, logs de cada pase)
y fuera del repositorio.
