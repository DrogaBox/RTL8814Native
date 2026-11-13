# wifip2pd after VIRTUAL_IF_CREATE: semaphore release and full request sequence

It closes the two pending items left by `AWDL_WIFIP2PD_REQUESTS.md` (§"What is missing"), `AWDL_WIFIP2PD_DISCOVERY.md` (§"Pending") and `AWDL_ROLE6_RE.md` (§6): what exactly releases the 30 s wait after the `RawSet` type 94, and the ordered list of everything `wifip2pd` asks of the AWDL interface once it exists. Base: `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), analysis with IDA 9.4 headless (Hex-Rays) over copies of `wifip2pd.i64` in `Build/ida_awdl/task_p2p_sequence/`, plus direct reads of the `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`) and of its existing IDB for the kernel part. Anything unverified goes as `?`. Addresses are VAs of the indicated binary.

## Part 1 — What releases the 30 s semaphore

**Result: the wait is released by the IORegistry notification (`IOServiceMatched`), not by an Apple80211 event of the driver.** The notification's event walks the discoverer `sub_100043720`, and when the new service matches the role (string `AirLink` for AWDL) it calls `sub_100046BD0`, the Swift method executing `semaphore.signal()`. The Apple80211 event route (`sub_1000685A0` → `sub_100041FE0`) never touches the semaphore (verified by xrefs: `sub_100046BD0` has only 3 callers, all inside `sub_100043720`).

### 1.1 Notification registration — `sub_100042BB0` (init of `CoreP2P.AppleDevice`, `0x100042bb0`)

| Step | Address | Evidence |
|---|---|---|
| `IONotificationPortCreate(kIOMainPortDefault)` | `0x10004323a` | result saved at `self+144` (ivar `deviceNotificationPort`, string at `0x1004aa8d0`) |
| `IONotificationPortSetDispatchQueue(port, qword_100556DF0)` | `0x100043275` | CoreP2P's global queue (the same `InterfaceHandle` uses for roles ≠ 1, see `sub_1000383C0`) |
| `IOServiceAddMatchingNotification(port, "IOServiceMatched", dict, callback, self, &iterator)` | `0x100043314` | literal `"IOServiceMatched"` at `0x100446040`; callback = `callback` (`0x100045500`); iterator saved at `self+304` (ivar `deviceAddedNotificationIterator`, string at `0x1004aa8f0`) |
| `IOServiceAddMatchingNotification(port, "IOServiceTerminate", dict, sub_100045FC0, self, &iterator)` | `0x1000433a3` | iterator at `self+312` (`deviceRemovedNotificationIterator`, string `0x1004aa8f0`+); only registered if the first returned 0 |

The matching dictionary is a literal with a **single entry**: `{"IOPropertyExistsMatch": "IOInterfaceName"}` (built in `sub_10002E6C0` from the table at `0x1005149A8`: key = large string whose literal's `count` is **`0x15` = 21 bytes**, exactly `len("IOPropertyExistsMatch")`, string at `0x100446020`; value = small-string `"IOInterfaceName"`, exact decoding. Unresolved detail `?`: the key's fixup pointer resolves to `0x100446000`, 0x20 before the string's start; the identification rests on the exact length and on it being the only plausible 21-byte IOKit key in the binary). That is: the notification fires for **any IORegistry service having the property `IOInterfaceName`**; fine filtering is the discoverer's job. The `IOServiceTerminate` consumer (`sub_100045520`) clears the disappearing service's handle and **does not signal the semaphore**.

Callback chain: `callback` (`0x100045500`, 17 bytes) → `sub_100045FE0` (`0x100045fe0`, retain/run/release) → iterator drain → `sub_100043720(self, iterator)`. The initial drain starts at `sub_10004A980` (`0x10004abc3`, boot of `AppleDevice`, which also drains the two iterators saved at `self+152`/`self+156`); `sub_100047660` also re-drains (`0x100047f09`, `0x100047f2c`) before creating.

### 1.2 The discoverer `sub_100043720` (`0x100043720`, 0x1da8 bytes) — per new service

1. `IOIteratorNext` (`0x100043820`) and read of `IOInterfaceName` via `IORegistryEntryCreateCFProperty` (`0x1000439a7`); if missing → log `"Matched a service but unable to query its interface name"` (`0x100494ac0`).
2. If the name is already in the discovered set: updates the existing `InterfaceHandle`'s `io_service` (`IOObjectRetain`/`IOObjectRelease` at `0x100043abd`/`0x100043ab4`) and logs `"Already discovered %s"` (string `0x100494be0`).
3. 802.11 filter: `SIOCGIFMEDIA` (`0x100043cbf`) and check `(ifm_current & 0xE0) == 0x80` (`0x100043ccc`) → if not, `"Ignoring %s because its not a WiFi interface"` (`0x100494b00`).
4. BSD index via `getifaddrs` with filter `AF_LINK` (18) (`sub_100046B00` + thunk `sub_100046AE0`, which reads `sdl_index` at `sockaddr_dl+2`); if none → `"Ignoring %s because no interface index found"` (`0x100494b30`).
5. Role read: first `IO80211VirtualInterfaceRole`, else `IO80211InterfaceRole` (`0x100043fbe`, `0x10004404b`).
6. Role string comparison against small-string immediates verified in the disassembly (`0x1000440a6`–`0x1000441db` and `0x10004523a`–`0x100045266`):

| String (immediate) | Internal code (`v99`/`v248`) |
|---|---:|
| `Infrastructure` (`0x7274736172666E49`/`0xEE00657275746375`) | 0 |
| `AirLink` (`0x6B6E694C726941`/`0xE700000000000000`) | 1 |
| `WiFi-Aware Discovery` (20 chars) and `WiFi-Aware Discovery+Data` (25 chars) | 2 |
| `WiFi-Aware Data` (`0x6177412D69466957`/`0xEF61746144206572`) | 3 |
| `LowLatency` (`0x6E6574614C776F4C`/`0xEA00000000007963`) | 4 (no handle, signal only) |
| `SoftAP` (`0x504174666F53`/`0xE600000000000000`) | ignored |

   The role string the kernel produces for role 6 is `AirLink` (closed in `AWDL_KERNEL_ROLE_TABLE.md` §2-3), so **the AWDL path is code 1**.

7. With match 0–3: it creates the `InterfaceHandle` with `sub_1000383C0` (`0x100044236`) — `Apple80211Open` (`0x100038459`) + `Apple80211BindToInterfaceWithService` (`0x100038543`, args: handle, BSD index, name as `NSString`, 0) + logger with subsystem `com.apple.awdl` for code 1 (`com.apple.nan` for 2/3). If creation **fails** → log `"Failed to bind to %s[%s]: %@"` (string `0x100494b90`) and **it signals anyway** (site `0x1000445a3`).
8. With match 1 (AirLink) and handle created: log `"Found new interface %s with role %s"` (string `0x100494bb0`), saves `parentInterfaceName` in the handle (see §2.3, `RawGet` 97, closure referenced at `0x100044728`), runs code 1's case (legacy preferences cleanup, `sub_100049A60`, log `"%s when removing %s from legacy preferences"`) and **signals at `0x1000451f2`**.
9. With match 4 (`LowLatency`): **it signals directly at `0x100045297`** (`mov esi, 4` at `0x100045281`) without creating a handle. Without match → `"Unable to find role for interface: %s"` (`0x100494b60`) without signal.

### 1.3 The signaler — `sub_100046BD0` (`0x100046bd0`, 0x160 bytes)

Swift method receiving `(self, role u8, name, value)`: it looks up the role's semaphore in the **dictionary `self+176`** (`swift_beginAccess` over `a1+176`), retains it and runs `while OS_dispatch_semaphore.signal() != 0` — the stub call `_$sSo21OS_dispatch_semaphoreC8DispatchE6signalSiyF` is at **`0x100046d03`** and repeats while returning ≠ 0 (wakes all waiters). If the role has no dictionary entry, it does nothing. The three call sites (`0x1000445a3`, `0x1000451f2`, `0x100045297`) pass the match's role code (`v248`) or 4.

### 1.4 The waiter — `sub_100047660` (`0x100047660`, 0x22ca bytes)

- It first looks for an existing handle with same role and parent name; if none exists and the role's semaphore is already in the dictionary (code 4's case with creation in progress), it waits on that semaphore in a loop (`OS_dispatch_semaphore.wait(timeout:)` at `0x100047a66`; timeout → log `"Timed out while waiting for the low latency interface with parent %s"` written at `0x100048beb`, string `0x1004939a0`).
- If there is nothing: `dispatch_semaphore_create(0)` at **`0x100047ad2`** and insertion into dictionary `self+176` (`lea rdi, [rbx+0xB0]` at `0x100047ae1`, key = role code) via `sub_100028560`; then it jumps through the **branch table at `0x10004992C`** indexed by the role code (12 entries). Name prefixes per code are built with immediates: code 0 → `en`, **code 1 → `awdl`** (`mov edi, 0x6C647761` at `0x100047c99`), 2 → `nan`, 3 → `ir` (`?` 2-char string, meaning unconfirmed), 4 → `llw`.
- AWDL branch (code 1): `RawGet` type 12 as the gate (3 sites in the binary: `0x10004811d`, `0x100048260`, `0x1000483a8`), construction of the 32-byte buffer (`0x100048c41`–`0x100048d60`: u32 @+0 from packed argument `a5`, 6-byte MAC @+4..+9 from `a5`/stack, **role @+0x0C = 6** at `0x100048cc8`; other codes' branches write **9** at `0x100048c66` and **0xA** at `0x100048c97`), `RawSet` type 94 len 32 at **`0x100048d95`** (`mov dword [var_E0], 0x5E` at `0x100048d6d`, `mov dword [var_E0+8], 0x20` at `0x100048d77`).
- Success → `DispatchTime.now()` (`0x10004912f`) + **30 s** (`mov qword [r12], 0x1E` at `0x10004913b`, enum `.seconds`) → `OS_dispatch_semaphore.wait(timeout:)` at **`0x1000491a8`**. Timeout → `DispatchTimeoutResult.==` comparison (`0x1000491d0`) → log `"Timed out while waiting for interface %s"` written at **`0x10004974b`** (string `0x100493910`) and error. Signal → continues and returns the handle.
- The create's error branches signal their own semaphore before exiting (sites `0x100048560`, `0x100048630`, `0x1000490c0`, same `signal() != 0` loops) so as not to hang other waiters.

### 1.5 Consequence for the prototype

For `wifip2pd` to accept `awdl0` in &lt;30 s, the kext must: (1) answer `VIRTUAL_IF_CREATE` with success writing the BSD name at `request+16` (payload), (2) register the interface in IORegistry with `IOInterfaceName` (so the `IOPropertyExistsMatch` match fires), `IO80211VirtualInterfaceRole = "AirLink"` and a real BSD index (`if_nametoindex`), and (3) answer `SIOCGIFMEDIA` with `IFM_TYPE == IFM_IEEE80211`. No Apple80211 event needs to be emitted to release the wait. The companion interface `llw0` (internal role 4) is accepted the same way with `IO80211InterfaceRole = "LowLatency"` (see §2.4).

## Part 2 — Request sequence over the created AWDL interface

### 2.0 Apple80211 surface (inventory closed by `nm` + xrefs in the IDB)

The daemon imports exactly 9 Apple80211 symbols: `Apple80211Open` (`0x1004457d4`), `Apple80211BindToInterfaceWithService` (`0x1004457c2`, only caller `sub_1000383C0` at `0x100038543`), `Apple80211Close` (`0x1004457c8`, in `sub_1000383C0` and `sub_100038700`), `Apple80211RawGet` (`0x1004457da`), `Apple80211RawSet` (`0x1004457e0`), `Apple80211StartMonitoringEvent` (`0x1004457e6`), `Apple80211RawEventMonitoringInit`, `Apple80211RawP2PEventMonitoringInit` (these last two only as data pointers, not call sites) and `Apple80211EventMonitoringHalt` (`0x1004457ce`). **There is no `Apple80211Get` nor `Apple80211Set`**: every Get/Set goes through `RawGet`/`RawSet` or through the direct ioctls of §2.5. `RawSet` has exactly 2 call sites: `0x100048d95` (type 94) and `0x10004b3e6` (type 293); `RawGet` 4: the three of type 12 + `0x100046db9` (type 97). The request structure is `apple80211req`: `ifname[16] @+0`, u32 type `@+16`, u32 val `@+20`, u32 length `@+24`, data pointer `@+32` (verified in `sub_100046D30`: `*(_DWORD *)(a4 + 16) = 97`, `*(_DWORD *)(a4 + 24) = len`, `*(_QWORD *)(a4 + 32) = ptr`).

### 2.1 Verified (static) order and state

| # | Request | Type / len | Emitter (address) | Attribution |
|---|---|---|---|---|
| 1 | `RawGet` **12** = `CARD_CAPABILITIES` (verified against kernel `gGetHandlerTable[11]`) | 12 / 21 | `sub_100047660` at `0x10004811d`/`0x100048260`/`0x1000483a8` | Gate before creation on the role branches; already observed on hardware (`Docs/ARCHITECTURE.md`) |
| 2 | `RawSet` **94** = `VIRTUAL_IF_CREATE` (kernel: `gSetHandlerTable[93]`) | 94 / 32 | `sub_100047660` at `0x100048d95` | role 6 (AWDL) @payload+0x0C; prefix `awdl`; answer: BSD name written by the kernel @payload+16 |
| 3 | 30 s semaphore wait (not a request) | — | wait `0x1000491a8` | Released by the IORegistry notification (§Part 1) |
| 4 | `RawGet` **97** = `VIRTUAL_IF_PARENT` (kernel: `gGetHandlerTable[96]` = `__ZL20getVIRTUAL_IF_PARENT…`) | 97 / name length | wrapper `sub_100046D30` (`0x100046db9`), invoked as closure `sub_100068C20` from the discoverer (`sub_100043720`, closure referenced at `0x100044728` for `String.init(unsafeUninitializedCapacity: 16, …)`) | Input: new interface's name; answer: C-string with the **parent's name**, saved in `InterfaceHandle.parentInterfaceName` |
| 5 | `RawSet` **293** = `COMPANION_SKYWALK_IF_CREATE` (kernel: `gSetHandlerTable[292]` = `__ZL30setCOMPANION_SKYWALK_IF_CREATE…`) | 293 / 24 | `sub_10004B300` (`0x10004b3e6`), called from `sub_100047660` at **`0x100048061`** (case 4 of switch `0x100047E85` = internal code 4, prefix `llw`) | Creates the low latency companion interface: ifname = parent's name (`awdl0`), payload = `{u32 code, u32 1}` (24 B; the u32 comes from the caller; rest of struct `?`); answer: the kernel overwrites the ifname field with the created name and `sub_10004B300` reads it with `String.init(cString:)`; then `if_nametoindex("llw0")` if the parent handle has role 1 (immediate at `0x10004b814`, constant `813132908` = `"llw0"`) or `if_nametoindex("llw1")` if role 2 (`0x10004b862`, `829910124` = `"llw1"`). Error → log `"Failed to create low latency interface for %s because %@"` (`0x100494cf0`) / `"Failed to create low latency interface because parent %s was not found"` (`0x100494ca0`) |
| 6 | Standard ifnet ioctls (see `AWDL_WIFIP2PD_IOCTLS.md`) | — | `sub_100046A30` + wrappers | `SIOCGIFMEDIA` (discoverer's 802.11 gate), `SIOCSIFFLAGS` up/down, `SIOCSIFLLADDR` (MAC), `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI`; the thunks `sub_10005F6B0/700/760/7B0` and `sub_100068ED0/EE0/97C0/97D0/97E0/97F0` are Swift witness table entries (two identical copies at `0x100517b98` and `0x10052cb50`, no code callers) |
| 7 | ioctl `'i'` 131 = `0xC1206983` (IOWR 288) + ioctl `'i'` 130 = `0xC0806982` (IOWR 128) with **type 7708** | — | `sub_100058420` (sites `0x100058509`/`0x1000585f8`) and `sub_1000586D0` (`0x1000587e6`/`0x1000588ee`), via witness (`sub_10005F7B0` ← `sub_100068EE0`, data-xrefs at `0x100517bd8`/`0x10052cb90`) | See §2.5 |
| 8 | Event monitoring (not a configuration request) | — | `sub_100041B60` (`Apple80211StartMonitoringEvent` ×16, role 0 only + NSObject queue) or `Apple80211RawP2PEventMonitoringInit` / `Apple80211RawEventMonitoringInit` (via `sub_100041F20`) | Documented in `AWDL_WIFIP2PD_P2P_EVENTS.md` |

The Swift chain triggering all this is still `AWDL_WIFIP2PD_INTERFACE_LIFECYCLE.md`'s: `sub_10036C2B0` ("No AWDL interface found. Creating AWDL interface", string `0x1004a1350`) → `sub_1003654A0` → init `AWDLInterface` `sub_100416D00` → companion `sub_10041B1F0`. The direct callers of `sub_100047660` are still `sub_100046E30` (recovery, `0x1000471be`), thunk `sub_10004D300`, and `sub_100207060`/`sub_100207160` (no code callers, dispatch via witness `?`).

### 2.2 Cross-check with the kernel tables (names of the types)

The two dispatch tables of IO80211Family, `__ZL16gGetHandlerTable` (`0xffffff800245eb60`) and `__ZL16gSetHandlerTable` (`0xffffff800245fef0`), are indexed as `table[id - 1]` (verified in `apple80211RouteExternal`, `0xffffff800236cd2e`: index `(unsigned)(req_type - 1)` and bounds `[1, 586]`). Checks read from the KC:

| Slot | Content | Conclusion |
|---|---|---|
| `gGetHandlerTable[11]` (`0x…245ebb8`) | `__ZL20getCARD_CAPABILITIES…` | type **12 = CARD_CAPABILITIES** |
| `gGetHandlerTable[96]` (`0x…245ee60`) | `__ZL20getVIRTUAL_IF_PARENT…` | type **97 = VIRTUAL_IF_PARENT** |
| `gSetHandlerTable[93]` (`0x…24601d8`) | `__ZL20setVIRTUAL_IF_CREATE…` | type **94 = VIRTUAL_IF_CREATE** |
| `gSetHandlerTable[292]` (`0x…2460810`) | `__ZL30setCOMPANION_SKYWALK_IF_CREATE…` | type **293 = COMPANION_SKYWALK_IF_CREATE** |

This attributes types 97 and 293 which were unnamed and confirms the known 12 and 94.

### 2.3 `RawGet` 97 — reading the parent's name

`sub_100046D30` (`0x100046d30`) arms the request: ifname @0 from an `InterfaceHandle`'s `String` (keypath `unk_10045FE08` + `sub_100063940`, which does `strlcpy(buf, chars, 16)`), type 97 @16, length @24 and pointer @32 taken from the input `String`, calls `RawGet(handle.reference, req)` and returns the read buffer as a C-string (search for `'\0'` up to the length). In the discoverer it is used as the closure of `String.init(unsafeUninitializedCapacity: 16, initializingUTF8With:)` and the result is saved in `InterfaceHandle.parentInterfaceName` — semantics of "given `awdl0`, tell me your parent interface (`en0`)".

### 2.4 `RawSet` 293 — creation of the low latency companion

Payload (24 bytes, buffer `v61`): `{u32 = a5 (companion code, `?` exact semantics — the binary's type dump has `apple80211_companion_skywalk_if_create_code` with `rawValue: UInt32`), u32 = 1, remaining 16 bytes uninitialized/seen at 0}`. The request's ifname field carries the **parent's** name (`awdl0`); the kernel answers by overwriting that same field with the created name, which the daemon converts to `String` and validates with `if_nametoindex` against the packed literals `"llw0"` (`0x30776c6c`) for role 1 handle (AWDL) and `"llw1"` (`0x31776c6c`) for role 2 (WiFi-Aware). Therefore, for AWDL the expected companion name is **`llw0`**, matching the `llw` prefix of internal code 4's branch (`0x100047c70`).

### 2.5 The `'i'` ioctls 130/131 with type 7708 — structure and why the kernel rejects them

`sub_100058420` (`0x100058420`): `socket(AF_INET6, SOCK_DGRAM, 0)` (`sub_1001CFA90(30,2,0)`), **288 B** buffer zeroed with only ifname @0 (keypath `unk_10045FE58`) → `ioctl(fd, 0xC1206983, buf)` (`0x100058509`). Only if it returns 0 and `*(a1+16) != 0`: **128 B** buffer zeroed with ifname @0 (keypath `unk_10045FE78`), **`u16 @16 = 7708` (`0x1E1C`)** (`LOWORD` at `0x1000585c3`; bytes @18..23 stay 0, so the u32 type read by the kernel is 7708) and **16 bytes @24..39 copied from the calling object `a1+32`** (`0x1000585db`) = `{val_len u32, val ptr}` per the `apple80211req` layout → `ioctl(fd, 0xC0806982, buf)` (`0x1000585f8`). Errors → Swift throw with `errno`. `sub_1000586D0` is the variant with the name coming from a closure (fn-ptr at `a4+40`); no code callers (`?`).

Kernel-side analysis (KC `f489ff8b…`, IDB `BootKernelExtensions`):

- The ioctl gate of IO80211 interfaces is `IO80211VirtualInterface::handleIoctlGated` (`0xffffff8002331d20`) / `IO80211SkywalkInterface::performGatedCommandIOCTL` (`0xffffff80022f779a`). They only accept the canonical constants **`_IOWR('i',200,32)` = `0xC02069C8`, `_IOWR('i',201,32)` = `0xC02069C9`, `_IOW('i',200,32)` = `0x802069C8`, `_IOWR('i',201,40)` = `0xC02869C9` and `_IOW('i',200,40)` = `0x802869C8`**; any other constant returns **`0xE00002C7`** (`kIOReturnUnsupported`). The daemon's constants (`'i'`,130/128 and `'i'`,131/288) **are not in that set**.
- Even if the request reached `apple80211RouteExternal` (`0xffffff800236cd2e`), the id is validated against `[1, 586]` (modern branch, `IOLog("IOCTL req out of bounds [1, 586]")`) or `< 458` (legacy branch, `gGet/SetHandlerTableLegacy`); **7708 and 0 are out in both** → it returns 102 (`kIOReturnUnsupported`).

Conclusion: the 131/130 pair with type 7708 **cannot be served by IO80211Family's apple80211 dispatch** as written; on a real macOS, either another layer serves it (xnu passthrough of `SIOCGIFMAC`/`SIOCSIFMAC` — "deprecated" numbers 130/131 in `sockio.h` — or another handler `?`), or it simply fails and the daemon absorbs it through its Swift error handling (the method is `throws` and the GET's failure prevents the SET). For the kext this means: **nothing needs implementing for type 7708 in the apple80211 override**; if an ioctl `'i'` 130/131 ever reaches the kext interface's if_ioctl, the real system's behavior would be to reject it (`0xE00002C7`) except via the legacy path (`?`).

### 2.6 What the kext's interface must answer (operational summary)

1. `CARD_CAPABILITIES` (12) — capabilities answer (the AWDL byte already opening the gate).
2. `VIRTUAL_IF_CREATE` (94) with role 6 — create `awdl0`, write the BSD name in the payload @+16, publish in IORegistry `IOInterfaceName` + `IO80211VirtualInterfaceRole = "AirLink"` and expose `SIOCGIFMEDIA` 802.11 with a live BSD index: that releases the semaphore (Part 1).
3. `VIRTUAL_IF_PARENT` (97) — return in the same buffer the parent interface's name (C-string ≤ 16 B).
4. `COMPANION_SKYWALK_IF_CREATE` (293) — create `llw0` associated with `awdl0`, write its name in the request's ifname field; the daemon validates it with `if_nametoindex`.
5. ifnet ioctls: `SIOCGIFMEDIA` (802.11), `SIOCSIFFLAGS`, `SIOCSIFLLADDR`, `SIOCSIFADDR`/`SIOCDIFADDR`, `SIOCADDMULTI`/`SIOCDELMULTI`.
6. Event monitoring: it does not block creation, but `Apple80211StartMonitoringEvent` (role 0) and the P2P paths (`RawP2PEventMonitoringInit`) must return 0 to avoid chaining recovery errors (`sub_100046E30`).

## Method and limits

- IDA 9.4 headless (`idat -A -S`) over copies of the IDB in `Build/ida_awdl/task_p2p_sequence/part1` and `/part2` (scripts `p1_probe*.py`, `p2_probe*.py`, outputs `p1_out*.txt`, `p2_out*.txt`); kernel: copy of the IDB in `task_p2p_sequence/kern_ioc` (`k_probe*.py`, `k_out*.txt`) and direct byte reads of the `.kc` for the tables and literals.
- §2.1's order is the one the static structure sustains (callers + switches + literals) plus the already documented lifecycle; hardware observation of each step stays pending.
- Remaining `?`: exact semantics of the 293 payload's u32 and of the rest of the 24 B struct; the name of the property feeding the 130/131 ioctls' ifname (keypaths `unk_10045FE58`/`unk_10045FE78`, unresolved descriptors); the exact Swift protocol of the witness tables (`$s7CoreP2P14AWDLWiFiDriverP`/`$s7CoreP2P14AWDLControllerP` candidates per the binary's strings); the full map of internal codes 5–11 of table `0x10004992C`; code 3's `ir` prefix string; and the real route of `'i'` 130/131 in xnu outside IO80211Family.
