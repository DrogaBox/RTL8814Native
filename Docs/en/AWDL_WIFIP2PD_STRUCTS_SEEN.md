# Structures observed before ioctl / RawSet in wifip2pd

Offset/width/field-value tables for each structure the binary builds in memory before a direct
ioctl or an `Apple80211RawGet/RawSet` call. Only offsets seen written (or read) in
pseudocode/assembly; anything not observed goes as `?`. Source: rea/IDA attached 2026-10-28;
`wifip2pd.x86_64` md5 `a120cfccfa2783303604ce5be9e1763a`. Struct names only when there is evidence
of import/use (`$s3__C…` metadata accessor) or numeric match with the SDK.

## 1) `apple80211req`-style request (RawGet/RawSet and ioctls 130)

Metadata: `$s3__C13apple80211reqVMa` at `0x10000d4e0`. Documented layout
(`AWDL_WIFIP2PD_REQUESTS.md`): `+0x10` type, `+0x18` length, `+0x20` data pointer.

Use A — `RawGet/RawSet(handle, req)` (sub_100046D30/sub_100047660/sub_10004B300, doc):
| Offset | Width | Content |
|---:|---:|---|
| +0x10 | 4 | apple80211 request type (12, 94, 97, 293 depending on the site) |
| +0x18 | 4 | length (21, 0x20, param, 24) |
| +0x20 | 8 | pointer to the data buffer |
| +0x00..+0x0F | 16 | `?` (not reviewed in this pass; probably ifname) |

Use B — ioctl 130 (`0xC0806982`, IOWR 128) in `sub_100058420` (`0x100058574`-`0x1000585db`) and
`sub_1000586D0` (`0x100058858`-`0x1000588d1`): a **128 B** buffer zeroed, then:
| Offset | Width | Content |
|---:|---:|---|
| 0x00 | 16 | ifname (keypath `unk_10045FE78` + helper `sub_100063940`) |
| 0x10 | 4 | **0x1E1C = 7708** (apple80211 type; unnamed) |
| 0x14 | 4 | 0 |
| 0x18 | 16 | 16 B copied from `self+0x20` (length+data per layout A) |
| 0x28..0x7F | 88 | 0 |

Use C — ioctl 131 (`0xC1206983`, IOWR 288) in `sub_100058420` (`0x100058499`) and
`sub_1000586D0` (`0x10005875b`): a **288 B** buffer zeroed with only ifname @0 (keypath
`unk_10045FE58`); type/length/data = 0.

## 2) `ifreq` (32 B) — 4 variants via `sub_100046A30` (dispatcher)

| Variant | Observed writes | Addresses |
|---|---|---|
| SIOCSIFFLAGS (`sub_100056990`) | 0x00: ifname (16); 0x10: short flags = `(ifa_flags from getifaddrs, AF_LINK) & 0xFFBE`, then `\| 0x41` (IFF_UP\|IFF_RUNNING) if up, else 0 | `0x100056b08`, `0x100056b2e` |
| SIOCSIFLLADDR (`sub_100057120`) | 0x00: ifname; 0x10: word `0x1206` (byte0=0x06, byte1=0x12=AF_LINK); 0x12: MAC 6 B (formatted by `sub_1000574E0`) | `0x10005725a`, `0x100057242` |
| SIOCSIFADDR (`sub_1000578F0`) | 0x00: ifname; 0x10: dword `0x210` (sa_len 0x10, sa_family 2=AF_INET); 0x14: IPv4 (low32 of the optional) | `0x100057987`-`0x100057991` |
| SIOCADDMULTI/SIOCDELMULTI (`sub_100057DE0`) | 0x00: ifname; 0x10: sockaddr_dl — word `0x1214` (sdl_len 0x14, sdl_family 0x12), byte@0x14: 6 (sdl_type=IFT_ETHER), byte@0x16: 6 (sdl_alen), 0x18: MAC (sdl_data) | `0x100057e88`-`0x100057f4b` |
| SIOCDIFADDR (`sub_1000578F0`) | 0x00: ifname; rest 0 (no address) | `0x100057985` |

## 3) `ifmediareq` (44 B, `#pragma pack(4)`) — SIOCGIFMEDIA in `sub_100043720`

Metadata: `$s3__C10ifmediareqVMa` at `0x10000fa60`.
| Offset | Width | Content |
|---:|---:|---|
| 0x00 | 16 | ifm_name (keypath `unk_100460700` + `sub_100063940`) |
| 0x10 | 4 | ifm_current — what the kernel returns; post-check `(x & 0xE0)==0x80` |
| 0x14..0x2B | 24 | 0 (zeroed at `0x100043c5d-0x100043c6e`) |

## 4) `VIRTUAL_IF_CREATE` buffer (RawSet type 94, 32 B)

Documented in `AWDL_WIFIP2PD_REQUESTS.md` (buffer `var_C0` of `sub_100047660`):
| Offset | Width | Content |
|---:|---:|---|
| +0 | 4 | u32 (role of the request, unspecified) |
| +4 | 6 | MAC (from arguments) |
| +0x0C | 4 | **role: 6 (AWDL) or 0x0A** |
| rest | 20 | `?` |

## 5) Swift objects of the event pipeline (`sub_100041B60`)

| Object | Size | Content seen |
|---|---:|---|
| eventBuffer (ivar `eventBuffer`) | 0x800 | `swift_slowAlloc(0x800)` (`0x100041c9b`); two 0x100 areas (base and base+0x100 stored in ivars +0/+8) — functions of the areas `?` |
| ctx passed to the registration path | 0x30 | `[2]=a2`, `[3]=a3` (Swift closure), `[4]=eventBuffer`, `[5]=eventBuffer+0x100` (`0x100041da4-0x100041db8`) |
| P2P ObjC block (`sub_100041F40`) | 0x28 | `{isa=_NSConcreteStackBlock, flags=0x42000000, invoke=sub_100041ED0, ctx=&unk_100515D48, cap0=closure fn (a3) @+32, cap1=object (a4) @+40}` |
| MAC formatting object (`sub_100057120`/`sub_100057DE0`) | 0x26 | qword[2]=6, qword[3]=12, bytes 32-37 = MAC (5-6 bytes of a packed value a2) — `0x1000571e7-0x10005721b` |

## 6) Kernel structures referenced (metadata accessor present, direct use `?`)

`$s3__C5ifreqVMa` `0x10000d480`, `$s3__C9in6_ifreqVMa` `0x10000d440`, `$s3__C12in6_aliasreqVMa`
`0x10000d360`, `$s3__C10ifmediareqVMa` `0x10000fa60`, `$s3__C13apple80211reqVMa` `0x10000d4e0`.
The `in6_*`/`ifmedia` ones were not seen built at the reviewed sites except ifmediareq (`?` for
in6).
