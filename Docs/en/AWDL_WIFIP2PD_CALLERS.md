# Callers of wifip2pd's ioctls up to Swift/log evidence

Caller chain of each site of [AWDL_WIFIP2PD_IOCTLS.md](AWDL_WIFIP2PD_IOCTLS.md), climbing to a named Swift function (`$s…`), a recognizable log string or an attributable constant. Symbols: the binary only carries Swift names for types/metadata; most functions stay as `sub_XXXX` (rule: do not invent names). Addresses = VA of the binary. Source: attached rea/IDA (get_callers + procedure_pseudo_code), session 2026-10-28.

## Summary table: request → emitting function → evidence → operation

| Request ioctl | Constant | Emitting function | Evidence (string / symbol / constant, with address) | Attributed operation |
|---|---|---|---|---|
| SIOCGIFMEDIA (56) | `0xC02C6938` | `sub_100043720` (site `0x100043cbf`) | Strings `"IO80211Virtual…"` (`0x1000438dc`), `"WifiAwareDisco…"` (`0x1000438ed`), `"IO80211Interfa…"` (`0x1000438fe`), `"Already discove…"` (`0x100043b70`), `"Matched A servic…"` (`0x100043c14`), `"Ignoring S becau…"` (`0x100043e15`/`0x100043ec7`); IORegistry property `"IOInterfaceName"` (`0x10004397c`); object class `_OBJC_IVAR_$__TtC7CoreP2P11AppleDevice_*` (`0x10004383a`, `0x10004385a`) | Interface discovery/filtering: iterate IORegistry, match the virtual service and check it is 802.11 (`IFM_TYPE==IFM_IEEE80211`, `0x100043ccc-0x100043cde`) |
| ↳ same site | — | `sub_100047660` (calls `sub_100043720` at `0x100047f09`, `0x100047f2c`) | Documented in `AWDL_WIFIP2PD_REQUESTS.md`: `RawGet` type 12 len 21 (`CARD_CAPABILITIES`), `RawSet` type 94 len 32 (`VIRTUAL_IF_CREATE`, role 6/10 at `+0x0C`), 30 s semaphore. Name `"awdl"` armed for role 6 | Handle creation/acquisition per role (includes AWDL, role 6) |
| ↳ same site | — | `sub_10004A980` (calls at `0x10004abc3`) | No own strings visible (`?`); its only caller is `sub_100002048` (`0x100002211`), function adjacent to `start` (0x100001ea0) | `?` (discovery at boot, not tested with strings) |
| SIOCSIFFLAGS (16) | `0x80206910` | `sub_100056990` → `sub_100046A30` (idx 1) | Observed structure: flags `& 0xFFBE` then `| 0x41` (IFF_UP\|IFF_RUNNING) if the bool is 1 (`0x100056b08`, `0x100056b2e`); `getifaddrs` + `sa_family == 18` (`0x100056a2a-0x100056af1`) | Bring the virtual interface up/down. No own strings (`?` Swift name) |
| SIOCSIFLLADDR (60) | `0x8020693C` | `sub_100057120` → `sub_100046A30` (idx 2) | `sockaddr{len? 0x06, family 0x12 = AF_LINK}` @+16 and 6 B MAC @+18..23 (`0x10005725a`), MAC formatted from a 0x26 B object by `sub_1000574E0` (`0x100057242`) | Set the interface's MAC. No own strings (`?`) |
| SIOCSIFADDR (12) / SIOCDIFADDR (25) | `0x8020690C` / `0x80206919` | `sub_1000578F0` → `sub_100046A30` (idx 3 or 4) | `sockaddr_in{sa_len=0x10, sa_family=2 (AF_INET), addr@20}` (`0x100057987-0x100057991`); nil → idx 4 | Set/clear the interface's IPv4 address. No own strings (`?`) |
| 130 IOWR 128 (type 7708) | `0xC0806982` | `sub_100058420` (site `0x1000585f8`) and `sub_1000586D0` (`0x1000588ee`) | `apple80211req` with type **7708** @16 (`0x1000585c3`, `0x1000588b5`), len+data @24..39 from `self+0x20`; socket `AF_INET6/SOCK_DGRAM` (`0x10005846e`/`0x100058730`, 30=AF_INET6 socket.h:396) | apple80211 request via direct ioctl (id 7708 unnamed, `?`). It is the only ioctl route with an apple80211 structure — candidate for "AWDL configuration via direct ioctl", but the id is not verified against the driver (`?`) |
| 131 IOWR 288 | `0xC1206983` | `sub_100058420` (site `0x100058509`) and `sub_1000586D0` (`0x1000587e6`) | 288 B buffer with only the interface name; it runs before and as the 130's gate (`0x10005852b-0x100058535`) | Prior query (GET) on the same interface; exact operation `?` |
| SIOCADDMULTI (49) | `0x80206931` | `sub_100057DE0` (idx 7 via `sub_10005F7D0`) | `sockaddr_dl{sdl_len=0x14, AF_LINK, sdl_type=6 (IFT_ETHER), sdl_alen=6, MAC}` (`0x100057e88-0x100057f4b`); idx fixed at `0x10005f7f6` | Add a multicast address to the interface |
| SIOCDELMULTI (50) | `0x80206932` | `sub_100057DE0` (idx 8 via `sub_10005F800`) | idx fixed at `0x10005f826` | Remove a multicast address |

## Caller chains (level by level, get_callers)

1. **SIOCGIFMEDIA:** `sub_100043720` ← `sub_100047660` (`0x100047f09`, `0x100047f2c`), `sub_10004A980` (`0x10004abc3`).
   - `sub_100047660` ← `sub_100046E30` (`0x1000471be`), `sub_10004D300` (thunk, `0x10004d318`), `sub_100207060` (`0x1002070bb`), `sub_100207160` (`0x1002071b7`).
   - **`sub_100046E30`** (0x4a8) = top with evidence: log `"Attempting recovery for %s[%s] with %s on %s"` (`0x100047156`), role switch with names `"Infrastructure"` (case 0, `0x100046f99`-`0x100046fad`), `"NME"` (case 2, `0x100047034`), `"NAN Data"` (case 3, `0x10004703b-0x10004704f`), `"Low Latency"` (case 4, `0x100047014-0x100047028`), `"WiFi-Aware Discovery"` (`0x10004728c`) and pair `"creation"`/`" failed"` (`0x1000472a6-0x1000472b0`) on the error branch. It calls `sub_100047660` at `0x1000471be` and, if it gets a handle, `sub_1000421E0` (`0x10004724e`). **Operation: handle recovery/creation per interface role (AWDL is role 6).** Its callers: no code callers (dispatch via vtable/witness, `?`).
   - `sub_100207060`/`sub_100207160`: small functions (0xfa/0xfe) without reviewed strings (`?`).
   - `sub_10004A980` ← `sub_100002048` (`0x100002211`); `sub_100002048` is the function adjacent to `start`; without reviewed strings (`?`).
2. **SIOCSIFFLAGS:** `sub_100056990` ← `sub_10005F6B0` (thunk passthrough) ← no code callers (`?`, vtable/witness).
3. **SIOCSIFLLADDR:** `sub_100057120` ← `sub_10005F700` (thunk) ← no code callers (`?`).
4. **SIOCSIFADDR/SIOCDIFADDR:** `sub_1000578F0` ← `sub_10005F760` (thunk) ← no code callers (`?`).
5. **130/131 (type 7708):** `sub_100058420` ← `sub_10005F7B0` (passthrough) ← `sub_100068EE0` (0xb) ← **no code callers** (`?`); `sub_1000586D0` ← no direct callers (`?`). Both with probable indirect dispatch (vtable/witness).
6. **SIOCADDMULTI/SIOCDELMULTI:** `sub_100057DE0` ← `sub_10005F7D0` (idx 7) ← `sub_1000697E0` (0xb) ← no callers (`?`); `sub_10005F800` (idx 8) ← `sub_1000697F0` (0xb) ← no callers (`?`).

## Note on the terminal thunks

`sub_10004D0D0` (the dispatcher's thunk), `sub_100068EE0`, `sub_1000697E0`, `sub_1000697F0` and the bridges `sub_10005F6B0/700/760/7B0` end in chains without code callers: they are Swift vtable/witness entries invoked indirectly. For T2 that means the point where a "named Swift function" takes control is not recoverable by direct callers on this adapter; operation attribution is supported by the structs each wrapper arms and, on the SIOCGIFMEDIA chain, by the strings of `sub_100043720`/`sub_100046E30`.
