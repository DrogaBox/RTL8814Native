# Estructuras observadas antes de ioctl / RawSet en wifip2pd

Tablas offset/ancho/valor-campo para cada estructura que el binario arma en memoria antes de un ioctl directo o una llamada `Apple80211RawGet/RawSet`. Sólo offsets vistos escritos (o leídos) en pseudocódigo/assembly; lo no observado va como `?`. Fuente: rea/IDA adjunto 2026-10-28; `wifip2pd.x86_64` md5 `a120cfccfa2783303604ce5be9e1763a`. Nombres de struct sólo cuando hay evidencia de importación/uso (`$s3__C…` metadata accessor) o coincidencia numérica con el SDK.

## 1) Petición estilo `apple80211req` (RawGet/RawSet y ioctls 130)

Metadata: `$s3__C13apple80211reqVMa` en `0x10000d4e0`. Layout documentado (`AWDL_WIFIP2PD_REQUESTS.md`): `+0x10` tipo, `+0x18` longitud, `+0x20` puntero a datos.

Uso A — `RawGet/RawSet(handle, req)` (sub_100046D30/sub_100047660/sub_10004B300, doc):
| Offset | Ancho | Contenido |
|---:|---:|---|
| +0x10 | 4 | tipo de request apple80211 (12, 94, 97, 293 según el sitio) |
| +0x18 | 4 | longitud (21, 0x20, param, 24) |
| +0x20 | 8 | puntero al buffer de datos |
| +0x00..+0x0F | 16 | `?` (no revisado en esta pasada; probable ifname) |

Uso B — ioctl 130 (`0xC0806982`, IOWR 128) en `sub_100058420` (`0x100058574`-`0x1000585db`) y `sub_1000586D0` (`0x100058858`-`0x1000588d1`): buffer de **128 B** a cero, luego:
| Offset | Ancho | Contenido |
|---:|---:|---|
| 0x00 | 16 | ifname (keypath `unk_10045FE78` + helper `sub_100063940`) |
| 0x10 | 4 | **0x1E1C = 7708** (tipo apple80211; sin nombre) |
| 0x14 | 4 | 0 |
| 0x18 | 16 | 16 B copiados de `self+0x20` (longitud+datos según el layout A) |
| 0x28..0x7F | 88 | 0 |

Uso C — ioctl 131 (`0xC1206983`, IOWR 288) en `sub_100058420` (`0x100058499`) y `sub_1000586D0` (`0x10005875b`): buffer de **288 B** a cero con sólo ifname @0 (keypath `unk_10045FE58`); tipo/longitud/datos = 0.

## 2) `ifreq` (32 B) — 4 variantes vía `sub_100046A30` (despachador)

| Variante | Escrituras observadas | Direcciones |
|---|---|---|
| SIOCSIFFLAGS (`sub_100056990`) | 0x00: ifname (16); 0x10: short flags = `(ifa_flags de getifaddrs, AF_LINK) & 0xFFBE`, luego `\| 0x41` (IFF_UP\|IFF_RUNNING) si up, si no 0 | `0x100056b08`, `0x100056b2e` |
| SIOCSIFLLADDR (`sub_100057120`) | 0x00: ifname; 0x10: word `0x1206` (byte0=0x06, byte1=0x12=AF_LINK); 0x12: MAC 6 B (formateada por `sub_1000574E0`) | `0x10005725a`, `0x100057242` |
| SIOCSIFADDR (`sub_1000578F0`) | 0x00: ifname; 0x10: dword `0x210` (sa_len 0x10, sa_family 2=AF_INET); 0x14: IPv4 (low32 del opcional) | `0x100057987`-`0x100057991` |
| SIOCADDMULTI/SIOCDELMULTI (`sub_100057DE0`) | 0x00: ifname; 0x10: sockaddr_dl — word `0x1214` (sdl_len 0x14, sdl_family 0x12), byte@0x14: 6 (sdl_type=IFT_ETHER), byte@0x16: 6 (sdl_alen), 0x18: MAC (sdl_data) | `0x100057e88`-`0x100057f4b` |
| SIOCDIFADDR (`sub_1000578F0`) | 0x00: ifname; resto 0 (sin dirección) | `0x100057985` |

## 3) `ifmediareq` (44 B, `#pragma pack(4)`) — SIOCGIFMEDIA en `sub_100043720`

Metadata: `$s3__C10ifmediareqVMa` en `0x10000fa60`.
| Offset | Ancho | Contenido |
|---:|---:|---|
| 0x00 | 16 | ifm_name (keypath `unk_100460700` + `sub_100063940`) |
| 0x10 | 4 | ifm_current — lo que el kernel devuelve; post-check `(x & 0xE0)==0x80` |
| 0x14..0x2B | 24 | 0 (a cero en `0x100043c5d-0x100043c6e`) |

## 4) Buffer de `VIRTUAL_IF_CREATE` (RawSet tipo 94, 32 B)

Documentado en `AWDL_WIFIP2PD_REQUESTS.md` (buffer `var_C0` de `sub_100047660`):
| Offset | Ancho | Contenido |
|---:|---:|---|
| +0 | 4 | u32 (rol de la petición, no especificado) |
| +4 | 6 | MAC (de argumentos) |
| +0x0C | 4 | **rol: 6 (AWDL) o 0x0A** |
| resto | 20 | `?` |

## 5) Objetos Swift del pipeline de eventos (`sub_100041B60`)

| Objeto | Tamaño | Contenido visto |
|---|---:|---|
| eventBuffer (ivar `eventBuffer`) | 0x800 | `swift_slowAlloc(0x800)` (`0x100041c9b`); dos áreas de 0x100 (base y base+0x100 guardadas en ivars +0/+8) — funciones de las áreas `?` |
| ctx pasado a la vía de registro | 0x30 | `[2]=a2`, `[3]=a3` (closure Swift), `[4]=eventBuffer`, `[5]=eventBuffer+0x100` (`0x100041da4-0x100041db8`) |
| Bloque ObjC P2P (`sub_100041F40`) | 0x28 | `{isa=_NSConcreteStackBlock, flags=0x42000000, invoke=sub_100041ED0, ctx=&unk_100515D48, cap0=closure fn (a3) @+32, cap1=objeto (a4) @+40}` |
| Objeto de formateo de MAC (`sub_100057120`/`sub_100057DE0`) | 0x26 | qword[2]=6, qword[3]=12, bytes 32-37 = MAC (5-6 bytes de un valor empaquetado a2) — `0x1000571e7-0x10005721b` |

## 6) Estructuras del kernel referenciadas (metadata accessor presente, uso directo `?`)

`$s3__C5ifreqVMa` `0x10000d480`, `$s3__C9in6_ifreqVMa` `0x10000d440`, `$s3__C12in6_aliasreqVMa` `0x10000d360`, `$s3__C10ifmediareqVMa` `0x10000fa60`, `$s3__C13apple80211reqVMa` `0x10000d4e0`. Las de `in6_*`/`ifmedia` no se vieron armadas en los sitios revisados salvo ifmediareq (`?` para in6).
