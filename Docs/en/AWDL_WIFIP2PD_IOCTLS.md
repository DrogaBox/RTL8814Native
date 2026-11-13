# Direct ioctls of wifip2pd (macOS 25.6.0)

Complete inventory of the sites calling `ioctl` in `wifip2pd.x86_64` (md5
`a120cfccfa2783303604ce5be9e1763a`), obtained with rea/IDA attached to the base open in the GUI
(xrefs to the ioctl stub + pseudocode/assembly per site). The request constants were read from the
data table at `0x100493008` of the binary itself (file offset `0x493008`, segment `__TEXT`). The
request names are assigned only when they match numerically the local SDK (`sys/sockio.h`,
`net/if.h`, `net/if_media.h`, `sys/socket.h` of MacOSX.sdk). Anything not seen goes as `?`.

## Single entry point

`wifip2pd` imports a single ioctl symbol: `_$s6Darwin5ioctlys5Int32VAD_SuSvtF` =
`Darwin.ioctl(Int32, UInt, Int) -> Int32`, stub at **`0x10044485c`** (undefined symbol of
libswiftDarwin; verified with `nm`). There is no `_ioctl` as a named function in the IDB
(`get_function_by_name("_ioctl")` fails). All ioctl use of the binary goes through 6 call sites to
this stub (complete xrefs at `0x10044485c`).

## Selection of the request number: table at `0x100493008`

`sub_1004443B4` (**`0x1004443b4`**, 0x1c bytes) is a selector: `return [0x100493008 + idx*8]` for
`idx ≤ 8`, `0` otherwise. The call sites pass the index (immediate or parameter) and the table
returns the constant. Table content (9 entries, qword LE, read from the binary):

| idx | table addr | constant | dir | size | group | num | Name (exact numeric match) |
|---:|---|---|---|---:|---|---:|---|
| 0 | `0x100493008` | `0xC02C6938` | IOWR | 44 | `'i'` | 56 | `SIOCGIFMEDIA` = `_IOWR('i',56,struct ifmediareq)` (sockio.h:113; `ifmediareq` = 44 B per `#pragma pack(4)`, if.h:376) |
| 1 | `0x100493010` | `0x80206910` | IOW | 32 | `'i'` | 16 | `SIOCSIFFLAGS` = `_IOW('i',16,struct ifreq)` (sockio.h:83) |
| 2 | `0x100493018` | `0x8020693C` | IOW | 32 | `'i'` | 60 | `SIOCSIFLLADDR` = `_IOW('i',60,struct ifreq)` (sockio.h:119) |
| 3 | `0x100493020` | `0x8020690C` | IOW | 32 | `'i'` | 12 | `SIOCSIFADDR` = `_IOW('i',12,struct ifreq)` (sockio.h:81) |
| 4 | `0x100493028` | `0x80206919` | IOW | 32 | `'i'` | 25 | `SIOCDIFADDR` = `_IOW('i',25,struct ifreq)` (sockio.h:89) |
| 5 | `0x100493030` | `0xC0806982` | IOWR | 128 | `'i'` | 130 | **unnamed**: group/num match `SIOCGIFMAC` (sockio.h:165) but the header size is 32 (ifreq) and the constant uses 128 → no numeric match, not assigned |
| 6 | `0x100493038` | `0xC1206983` | IOWR | 288 | `'i'` | 131 | **unnamed**: group/num match `SIOCSIFMAC` (sockio.h:166) but size 288 ≠ 32 and dir IOWR ≠ IOW → not assigned |
| 7 | `0x100493040` | `0x80206931` | IOW | 32 | `'i'` | 49 | `SIOCADDMULTI` = `_IOW('i',49,struct ifreq)` (sockio.h:101) |
| 8 | `0x100493048` | `0x80206932` | IOW | 32 | `'i'` | 50 | `SIOCDELMULTI` = `_IOW('i',50,struct ifreq)` (sockio.h:102) |

**SIOCSDRVSPEC / SIOCGDRVSPEC: not used.** `SIOCSDRVSPEC = _IOW('i',123,struct ifdrv)` =
`0x8028697B` and `SIOCGDRVSPEC = _IOWR('i',123,struct ifdrv)` = `0xC028697B` (sockio.h:150-152);
none of the 9 constants is either of the two, and no other ioctl constant appears in the binary.

## The 6 ioctl call sites

### 1) `0x100043cbf` in `sub_100043720` — SIOCGIFMEDIA (interface discovery)

- **Request:** `sub_1004443B4(0)` = `0xC02C6938` (SIOCGIFMEDIA). Seen in assembly `0x100043caf`
  (`xor edi,edi`) + `0x100043cb1` (call) + `0x100043cb9` (`mov rsi, rax`).
- **fd:** `[AppleDevice + 0x18] + 0x10` (`mov rax,[rbp+var_348]; mov r14d,[rax+10h]`,
  `0x100043ca4-0x100043cab`).
- **Structure (44 B, `ifmediareq`):** `ifm_name` @0 filled with the interface name via keypath
  `unk_100460700` and helper `sub_100063940` (`0x100043c75-0x100043c97`); rest zero
  (`movups/movaps xmm0` at `0x100043c5d-0x100043c6e`). Pointer passed: `&buffer`
  (`lea r15,[rbp+root]`, `0x100043c87`; `mov rdx,r15`, `0x100043cbc`).
- **Post-check:** `(ifm_current @+16 & 0xE0) == 0x80` (`0x100043ccc-0x100043cde`) =
  `IFM_TYPE(media) == IFM_IEEE80211` (IFM_NMASK=`0xE0` if_media.h:294, IFM_IEEE80211=`0x80`
  if_media.h:253). If the ioctl fails or the type is not 802.11, it logs `"Ignoring S becau…"`
  (`aIgnoringSBecau`/`aIgnoringSBecau_0` at `0x100043e15`/`0x100043ec7`) and continues the loop.
- **Context:** it iterates an `IOIteratorNext` of IORegistry services reading the property
  `"IOInterfaceName"` (`0x10004397c-0x1000439a7`: literals `667265746E494F49h`/`0EF656D614E656361h`
  = `"IOInterfaceName"`), with strings `"IO80211Virtual…"` (`0x1000438dc`),
  `"WifiAwareDisco…"` (`0x1000438ed`), `"IO80211Interfa…"` (`0x1000438fe`), `"Already discove…"`
  (`0x100043b70`), `"Matched A servic…"` (`0x100043c14`). It is the filter "is this virtual service
  WiFi (802.11)?".

### 2) `0x100046a72` in `sub_100046A30` — generic dispatcher

- `sub_100046A30(idx: UInt32, argp: UnsafeRawPointer)` (0xa9 bytes): takes `os_unfair_lock`
  (`0x100046a54`), reads fd from `*(object+0x10)` (`0x100046a5d`), `request = sub_1004443B4(idx)`
  (`0x100046a64`), `ioctl(fd, request, argp)` (`0x100046a72`), and converts `errno` into a Swift
  error if it returns -1 (`0x100046a7a-0x100046ab4`).
- Direct callers (get_callers): `sub_10004D0D0` (thunk passthrough **with no code callers**; only
  data xref at `0x100514e30`), `sub_100056990` (idx 1), `sub_100057120` (idx 2), `sub_1000578F0`
  (idx 3 or 4), `sub_100057DE0` (idx = parameter, 7 or 8 via its thunks). Each caller builds the
  structs (see below).

### 3) `0x100058509` in `sub_100058420` — request 131 (`0xC1206982`-family, IOWR 288) on an AF_INET6 socket

- **Socket:** `sub_1001CFA90(30, 2, 0)` at `0x10005846e` = `socket(AF_INET6, SOCK_DGRAM, 0)`
  (AF_INET6=30, SOCK_DGRAM=2, socket.h:112/396). fd read from `socket[4]` (offset +0x10) at
  `0x1000584f0-0x1000584f8`.
- **288 B buffer** (`__bzero(&root, 288)`, `0x100058499`): only the interface name @0 is written
  via keypath `unk_10045FE58` + `sub_100063940` (`0x1000584b2-0x1000584d4`). Type/len/data stay 0.
- **Request:** `sub_1004443B4(6)` = `0xC1206983` (`0x1000584f8`), arg `&root` (`0x100058509`).

### 4) `0x1000585f8` in `sub_100058420` — request 130 (`0xC0806982`, IOWR 128) on the same fd

- Only if the previous ioctl returned 0 **and** `*(self+0x10) != 0`
  (`0x10005852b-0x100058535`).
- **128 B buffer** (`memset(v45, 0, 128)`, `0x100058574`): name @0 (keypath `unk_10045FE78` +
  `sub_100063940`, `0x100058587-0x1000585ae`); **dword @16 = 0x1E1C = 7708**
  (`LOWORD(v35[2].value_word) = 7708`, `0x1000585c3`); **16 bytes @24..39 copied from `self+0x20`**
  (`*(_OWORD *)&v35[3].value_word = v32`, `0x1000585db`; `v32 = *(_OWORD *)(a1 + 32)`,
  `0x100058535`).
- **Layout = `apple80211req`** (identical to the one documented for RawGet/RawSet in
  `AWDL_WIFIP2PD_REQUESTS.md`: `+0x10` type, `+0x18` length, `+0x20` data pointer): name[16] @0,
  type @16 = **7708**, val @20 = 0, length+data @24..39 taken from the caller. The apple80211 type
  **7708** is not mapped to a name (`?`).
- **Request:** `sub_1004443B4(5)` = `0xC0806982` (`0x1000585e7`), arg `v45` (`0x1000585f8`).

### 5) `0x1000587e6` in `sub_1000586D0` — same as (3)

Same pattern: `socket(AF_INET6, SOCK_DGRAM, 0)` (`0x100058730`), 288 B buffer with only the name
(keypath `unk_10045FE58`; the name comes from a closure: fn-ptr at `a4+40` called at
`0x100058784`/`0x100058875`), `sub_1004443B4(6)` (`0x1000587d5`), ioctl `0x1000587e6`.

### 6) `0x1000588ee` in `sub_1000586D0` — same as (4)

Same 128 B buffer (name + **7708** @16 (`0x1000588b5`) + 16 B from `self+0x20` @24
(`0x1000588d1`)), `sub_1004443B4(5)` (`0x1000588dd`), ioctl `0x1000588ee`. No direct callers
detected (get_callers empty; `?` via indirect reference).

## Wrappers that build the dispatcher structures (sub_100046A30)

All fill the name @0 with the same keypath `unk_10045FE30` and helper `sub_100063940`.

| Wrapper | idx → request | Structure (32 B ifreq unless noted) | Evidence |
|---|---|---|---|
| `sub_100056990` | 1 → `0x80206910` SIOCSIFFLAGS | `ifflags` (short) @16 = `(ifa_flags from getifaddrs, entry with matching `ifa_name` and `ifa_addr->sa_family == 18 (AF_LINK)`) & 0xFFBE`; then `\| 0x41` (IFF_UP\|IFF_RUNNING) if the bool input ≠ 0, else 0 (down) | `0x100056a2a-0x100056b0a`, ioctl via dispatcher `0x100056b4b` |
| `sub_100057120` | 2 → `0x8020693C` SIOCSIFLLADDR | @16: word `4614` = `0x1206` (byte0 `0x06`, byte1 `0x12` = AF_LINK 18); MAC 6 bytes @18..23 written by `sub_1000574E0` (formats a 0x26 B object) | `0x1000571e7-0x100057287` |
| `sub_1000578F0` | 3/4 → SIOCSIFADDR / SIOCDIFADDR | If the input optional has a value: sockaddr_in @16 (`dword@16 = 528 = 0x0210` → sa_len 0x10, sa_family 2 = AF_INET; sin_addr @20 = low32 of the value) and **idx 3**; if nil, **idx 4** without address | `0x100057985-0x1000579b6` |
| `sub_100057DE0` | idx = 3rd parameter of the caller | @16: sockaddr_dl — word `4628 = 0x1214` (sdl_len 0x14, sdl_family 0x12 = AF_LINK), byte@20 = 6 (sdl_type = IFT_ETHER), byte@22 = 6 (sdl_alen), MAC in sdl_data @24 | `0x100057e88-0x100057f4b` |
| ↳ `sub_10005F7D0` | sets **idx 7** → `0x80206931` SIOCADDMULTI | passthrough with MAC | `0x10005f7f6` |
| ↳ `sub_10005F800` | sets **idx 8** → `0x80206932` SIOCDELMULTI | passthrough with MAC | `0x10005f826` |

Regarding the apple80211 types/IDs the task asks about: in these direct ioctls of
sockets/interface **there is no apple80211 type** (they are ifreq/ifmediareq); the only
apple80211-type id appears in the ioctls 130/131 as the field @16 = **7708** (no known name, `?`),
next to length+data @24..39 taken from the caller object.

## Caller chain (input for T2)

- `sub_100043720` ← `sub_100047660` (`0x100047f09`, `0x100047f2c`) and `sub_10004A980`
  (`0x10004abc3`).
- `sub_100046A30` ← `sub_10004D0D0` (thunk; no code callers, 1 data xref at `0x100514e30`),
  `sub_100056990`, `sub_100057120`, `sub_1000578F0`, `sub_100057DE0`.
- `sub_100058420` ← `sub_10005F7B0` (pure passthrough).
- `sub_1000586D0` ← no direct callers (`?`).
- `sub_100057DE0` ← `sub_10005F7D0` (idx 7), `sub_10005F800` (idx 8).

## Coverage note

`nm` on the binary lists a single ioctl symbol (`_$s6Darwin5ioctlys5Int32VAD_SuSvtF`) and the xrefs
to stub `0x10044485c` cover the 6 sites above; the other ioctl uses go through `sub_100046A30`
(which is one of those 6 sites). There are no ioctl constants outside the `0x100493008` table in
the reviewed pseudocodes.
