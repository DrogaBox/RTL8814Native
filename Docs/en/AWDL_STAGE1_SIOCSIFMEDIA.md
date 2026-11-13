# `SIOCSIFMEDIA` on `awdl0`: can `ifm_current` change before the wifip2pd read?

Closes the open point of `AWDL_STAGE1_IFM_CURRENT.md` §3: whether something issues `SIOCSIFMEDIA`
on
`awdl0` between the registration (`ifm_current = RegInfo+0x28 = 0x80`) and the `SIOCGIFMEDIA` of
wifip2pd that filters `(ifm_current & 0xE0) == 0x80`. Base: kernelcache of macOS 26.7.1 (25G241,
x86_64), IDA
headless (`idat -A -S`) over a copy of `boot_awdl.kc.i64`, and the binaries of the same system for
the user side. All kernel addresses are VAs; user ones are VAs of the x86_64 slice.

## 1. Constants: correction of the starting point

Compiled against the SDK of this same system (`sizeof(struct ifreq) = 32`,
`sizeof(struct ifmediareq) = 44`):

| Command | Value | Use |
|---|---|---|
| `SIOCSIFMEDIA` | `0xC0206937` | sets the media of an interface |
| `SIOCGIFMEDIA` | `0xC02C6938` | reads the media and the supported list |
| `SIOCGIFXMEDIA` | `0xC02C6948` | reads the extended media |

`0x80206937` is not a command and does not appear in the text of the kernelcache (sweep of all code
heads) nor in any of the binaries analyzed in §6. The kernel compares against `0xC0206937` at
`0xffffff8002b0ad8a` (`cmp edx, 0C0206937h`), not against `0x80206937`.

Occurrences of `0xC0206937` in the kernel:

| VA | Function | Instruction |
|---|---|---|
| `0xffffff80026f79f2` | `IONetworkInterface::performCommand` | `mov edx, 0C0206937h` (`IONetworkInterface` interfaces, not Skywalk) |
| `0xffffff80026f7aa1` | `IONetworkInterface::performGatedCommand` | `mov eax, 0C0206937h` (same) |
| `0xffffff8002b0ad8a` | `IOSkywalkNetworkInterface::processBSDCommand` | `cmp edx, 0C0206937h` (path of `awdl0`) |

## 2. Ioctl path in the kernel for a role 6 interface

`awdl0` is an `IO80211VirtualInterface`; its `ifnet` belongs to an `IOSkywalkNetworkBSDClient`, so
the ioctl does not go through `IONetworkInterface`.

| VA | Instruction / fact |
|---|---|
| `0xffffff8002b002e6` | `IOSkywalkNetworkBSDClient::if_ioctl`, head; `ifnet_softc` at `0xffffff8002b002f9` |
| `0xffffff8002b0030f` / `0xffffff8002b0032c` | loads `[client+90h]+0x1C8` and jumps: it is `IOCommandGate::runAction` (class closed in `AWDL_STAGE1_REGNETINFO_AND_GATES` §6) |
| `0xffffff8002affce6` | `IOSkywalkNetworkBSDClient::ioctlHandler`; at `0xffffff8002affe97` calls `call qword ptr [rax+910h]` |
| `0xffffff8002331b36` | `IO80211VirtualInterface::processBSDCommand`; jumps to slot `+0xE80` (`handleIoctl`) |
| `0xffffff8002331c88` | `IO80211VirtualInterface::handleIoctl` |
| `0xffffff8002331d08`–`0xffffff8002331d1b` | only the four apple80211 commands (`0x802069C8`, `0x802869C8`, `0xC02069C9`, `0xC02869C9`) go to `handleIoctlGated`; **any other, `SIOCSIFMEDIA` included, jumps to `IO80211SkywalkInterface::processBSDCommand`** |
| `0xffffff80022f6e98` | `IO80211SkywalkInterface::processBSDCommand`; non-apple80211 commands go to slot `+0x910` of the `IOSkywalkEthernetInterface` vtable: `call qword ptr [rax+920h]` at `0xffffff80022f71cc`, with `rax = __ZTV26IOSkywalkEthernetInterface` (`0xffffff8002b3f178`) |
| `0xffffff8002b1102c` | `IOSkywalkEthernetInterface::processBSDCommand`; not multicast, lladdr nor `SIOCSIFFLAGS`, so at `0xffffff8002b1108a` it jumps to slot `+0x910` of `IOSkywalkNetworkInterface` |
| `0xffffff8002b0ad4a` | `IOSkywalkNetworkInterface::processBSDCommand`; `cmp edx, 0C0206937h` at `0xffffff8002b0ad8a`; jump to `ioctl_sifmedia` at `0xffffff8002b0ad9d` |

There is no role, subfamily or entitlement check in this chain: role 6 is not consulted
anywhere, and `handleIoctl` forwards the command unfiltered.

The only privilege gate of `_ifioctl` (`0xffffff80005d62d0`) in this zone is the one of
`proc_suser` at `0xffffff80005d6cd6`, and it only applies to `0x8040693E`, `0x8078693E` and
`0x8080693E`:
the three comparisons are at `0xffffff80005d6cb6`, `0xffffff80005d6cc0` and `0xffffff80005d6cca`
and
jump to the common block of `0xffffff80005d6cd3`. `0xC0206937` is not among them and reaches
`ifnet_ioctl` without credential checks.

## 3. `ioctl_sifmedia`: when it writes `descriptor+0x34`

The real head is `0xffffff8002b0affa`; `0xffffff8002b0b184` (which `AWDL_STAGE1_IFM_CURRENT.md`
cited
as the function) is the store inside it.

| VA | Instruction | Fact |
|---|---|---|
| `0xffffff8002b0b00b` | `mov rax, [rdi+0C0h]` | media descriptor |
| `0xffffff8002b0b069` | `call qword ptr [rax+948h]` | `getSupportedMediaArray` fills the table |
| `0xffffff8002b0b06f`–`0xffffff8002b0b0ac` | `test eax` / `jnz` / `mov r14d, 5` | if the slot fails, frees and returns 5 (EIO) **without writing** |
| `0xffffff8002b0b07f` | `mov ecx, [r13+10h]` | requested media: `ifreq+0x10` |
| `0xffffff8002b0b085`–`0xffffff8002b0b093` | loop | the requested media must be in the table |
| `0xffffff8002b0b0c0`–`0xffffff8002b0b0c7` | `mov rdx, [rbx+0C0h]` / `mov edx, [rdx+34h]` | reads the current `ifm_current` for the masked fallback |
| `0xffffff8002b0b152` | `call qword ptr [rax+940h]` | `handleChosenMedia(media)` |
| `0xffffff8002b0b160` | `call qword ptr [rcx+788h]` | `errnoFromReturn` over the result |
| `0xffffff8002b0b169`–`0xffffff8002b0b16b` | `test eax` / `jnz` | if the mapping is not 0, **does not write** |
| `0xffffff8002b0b17d`–`0xffffff8002b0b184` | under `[descriptor+0x10]` | `mov [rax+34h], r13d`: writes the chosen media |

Writing therefore requires: `getSupportedMediaArray` (slot `+0x948`) returning 0, the requested
media being in that table, `handleChosenMedia` (slot `+0x940`) returning 0 and
`errnoFromReturn` (slot `+0x788`) mapping it to 0. In `__ZTV23IO80211VirtualInterface`
(`0xffffff8002472b78`) those three slots are `IO80211SkywalkInterface::getSupportedMediaArray`
(`0xffffff80022f8e5e`), `handleChosenMedia` (`0xffffff80022f8f5c`) and `errnoFromReturn`
(`0xffffff80022f997c`), with no override in the subclass.

## 4. Why that store is unreachable on `awdl0` with this driver

| VA | Fact |
|---|---|
| `0xffffff80022f8e5e` | `IO80211SkywalkInterface::getSupportedMediaArray`: takes the controller from `[ivars+0x120]+0x30` and calls its slot `+0x890` |
| `0xffffff80022f8f24`–`0xffffff80022f8f32` | if the slot returns null: `IOLog("not valid dictionary")` and `mov r14d, 0E00002F0h`; returns error |
| `0xffffff80026f4d38` | `IONetworkController::copyMediumDictionary`: copies the `IOMediumDictionary` property (`getMediumDictionary`, `0xffffff80026f4d20`, string at `0xffffff80026ffc43`) |
| `0xffffff80026f4fd0` | `IONetworkController::publishMediumDictionary`: the only function creating that property |
| `0xffffff8002461148` | `__ZTV17IO80211Controller`: slot `+0x890` is still `copyMediumDictionary` (`0xffffff80026f4d38`) and `+0x8F0` is still `selectMedium` (`0xffffff80026f4d94`); the class does not override them |
| — | xrefs to `IONetworkController::publishMediumDictionary` (`0xffffff80026f4fd0`) in the whole kernelcache: all are vtable cells (data), **no code call**, so the `IOMediumDictionary` property is published nowhere; `Source/AirLayer` mentions neither `IONetworkMedium` nor `publishMediumDictionary`; the only project call is the one of `RTLEthernetAdapter` (`Source/Driver/RTLEthernetAdapter.cpp`, an `IOEthernetController` of the base kext), a different object from the controller of `awdl0` |
| `0xffffff80022f8f5c` | `IO80211SkywalkInterface::handleChosenMedia`: calls slot `+0x890` of the controller (`0xffffff80022f8f7a`); if null it returns `0xE00002F0` (`0xffffff80022f8fb6`); if the media is not there, same (`0xffffff80022f8fbe`); if it is, calls slot `+0x8F0` (`0xffffff80022f8fab`) |
| `0xffffff80026f4d94` | `IONetworkController::selectMedium`: `mov eax, 0E00002C7h` and `retn`, i.e. `kIOReturnUnsupported` |

Consequence: on `awdl0` the first slot already fails (no `IOMediumDictionary` exists),
`ioctl_sifmedia` returns 5 at `0xffffff8002b0b0ac` and never reaches `0xffffff8002b0b184`. Even if a
future driver published the dictionary, `selectMedium` would keep returning `0xE00002C7` because
`RTLAirController`
does not override it.

## 5. Other writers of the field after registration

Sweep of stores `mov [reg+34h], …` with a load of `[x+0C0h]` in the ten previous instructions,
over all `IO80211*`/`IOSkywalk*` functions of both ranges: only two.

| VA | Function | Fact |
|---|---|---|
| `0xffffff8002b0a332` | `IOSkywalkNetworkInterface::registerNetworkInterface` | `RegInfo+0x28` → `+0x34`, during registration |
| `0xffffff8002b0b184` | `IOSkywalkNetworkInterface::ioctl_sifmedia` | only after the conditions of §3 |

There is no writer on the link-state, role or notification paths, and the field is read under
the descriptor lock in `ioctl_gifmedia` (`0xffffff8002b0afd5`).

## 6. User side: who issues `SIOCSIFMEDIA`

Sweep of the two values (`0xC0206937` and `0xC02C6938`) and disassembly of the hits, on the
x86_64 slice of the same build (25G241):

| Binary | Constants | Function / address | Effect |
|---|---|---|---|
| `/usr/libexec/wifip2pd` | **only** `0xC02C6938` | ioctl table at `0x100493008` (9 entries); accessor `0x1004443b4`; `call` at `0x100043cb1`; `ioctl` at `0x100043cbf`; filter `(ifm_current & 0xE0) == 0x80` at `0x100043cd2`–`0x100043cde` | **reads** media; cannot set them |
| `/usr/libexec/configd` | `0xC0206937` (×2) and `0xC02C6948` | `0x100059d90`, implementation of `_SCNetworkInterfaceSetMediaOptions` (string at `0x10006ca93`); `ioctl(SIOCGIFXMEDIA)` at `0x100059f10` (request computed at `0x100059f01` as `0xC0206937+0xC0011`); `ioctl(SIOCSIFMEDIA)` at `0x10005a0e4` (`movl $0xc0206937,%esi` at `0x10005a0da`); error `"ioctl(SIOCSIFMEDIA) failed"` | **sets** media, generic by BSD name; no `awdl` strings |
| `IPConfiguration` | only `0xC02C6938` | `movl $0xc02c6938,%esi` at `0x51689`, `ioctl` at `0x51696` | reads media |
| `/usr/libexec/airportd` | none | — | — |
| `/usr/libexec/symptomsd` | none | — | — |
| `/usr/libexec/UserEventAgent` | none | — | — |
| `networkd` | does not exist on 26.7.1 | — | — |

The wifip2pd table at `0x100493008` contains `0xC02C6938` (index 0), `0x80206910`, `0x8020693C`,
`0x8020690C`, `0x80206919`, `0xC0806982`, `0xC1206983`, `0x80206931` and `0x80206932`:
`SIOCSIFMEDIA` is not there. None of the listed binaries imports
`_SCNetworkInterfaceSetMediaOptions` (configd implements it; wifip2pd imports
`SCNetworkInterfaceGetBSDName`, not the setter).

## 7. Answer

**No: `ifm_current` of `awdl0` cannot change from the seeded `0x80` before wifip2pd reads it.**

1. The only writer after the registration is `ioctl_sifmedia`, and on `awdl0` it fails before the
   store
   (`0xffffff8002b0b0ac`) because the controller does not publish `IOMediumDictionary`
   (`0xffffff80022f8f24`) and `selectMedium` is the `IONetworkController` one
   (`0xffffff80026f4d94`).
2. No process of the list issues `SIOCSIFMEDIA` on `awdl0`: wifip2pd does not have the command in
   its table (`0x100493008`), configd is the only one issuing it (`0x10005a0e4`) but as a generic
   SPI over an `SCNetworkInterface`, with no `awdl` code or string, and none of the other binaries
   contains the constant.
3. Even invoking the configd SPI with `awdl0`, the ioctl would fail in the kernel per (1), so the
   value would remain `0x80`.

`?` remain: the hardware test (T5.1 is still the real check) and the case of a future driver that
publishes `IOMediumDictionary` and overrides `selectMedium`; in that scenario
`SIOCSIFMEDIA` could indeed write `+0x34`, but it is not the stage 1 state. Emitters outside the
requested binaries were not searched either.

## 8. Searches executed

- Sweep of all code heads of the kernelcache for `80206937`: none; for `0xC0206937`:
  the three functions of §1.
- Full disassembly of `if_ioctl`, `ioctlHandler`, `processBSDCommand` (virtual, Skywalk,
  Ethernet and Network), `handleIoctl`, `ioctl_sifmedia`, `getSupportedMediaArray`,
  `handleChosenMedia`, `copyMediumDictionary`, `getMediumDictionary`, `selectMedium` and `_ifioctl`.
- Xrefs to `ioctl_sifmedia` (only `IOSkywalkNetworkInterface::processBSDCommand`,
  `0xffffff8002b0ad9d`);
  xrefs to `publishMediumDictionary` (`0xffffff80026f4fd0`): all data (vtable cells),
  no code call in the whole kernelcache; `IOMediumDictionary` strings (only get/publish).
- Reading of the cells of `__ZTV17IO80211Controller` (`0xffffff8002461148`) and
  `__ZTV23IO80211VirtualInterface` (`0xffffff8002472b78`) at `+0x788`, `+0x890`, `+0x8F0`, `+0x940`
  and
  `+0x948`; function head check for `_ifioctl`, `if_ioctl`, `ioctlHandler`,
  `processBSDCommand` (four classes), `handleIoctl`, `ioctl_sifmedia`, `ioctl_gifmedia`,
  `getSupportedMediaArray`, `handleChosenMedia`, `copyMediumDictionary`, `getMediumDictionary` and
  `selectMedium` (all real heads).
- Sweep of stores to `+34h` with context `[x+0C0h]`; sweep of calls to slot `+0x970`.
- Binaries: search of the 4-byte patterns of `0xC0206937` and `0xC02C6938` in the
  x86_64 slices, disassembly of the hits and import checks.
