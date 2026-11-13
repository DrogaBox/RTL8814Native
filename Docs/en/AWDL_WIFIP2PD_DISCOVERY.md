# How wifip2pd detects the created AWDL interface

Complements `AWDL_WIFIP2PD_REQUESTS.md`, which left pending what releases the 30 s wait after `VIRTUAL_IF_CREATE`. Analysis with IDA over `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), plus a live check with `ioreg`.

## Result

The wait is **not released by an Apple80211 event of the driver**. It is released by `wifip2pd`'s **IORegistry interface discoverer**, when an interface with the expected role property appears in the registry.

## Verified chain

1. `sub_100047660` (`0x100047660`) does `RawSet` type 94 (`VIRTUAL_IF_CREATE`) and, if it returns 0, waits on a `DispatchSemaphore` (30 s) saved in a per-role dictionary (`self+176`).
2. `sub_100046BD0` (`0x100046bd0`) looks up that semaphore by role, saves a value and does `signal()` in a loop while there are waiters. It is called from three points of `sub_100043720`: `0x1000445a3`, `0x1000451f2`, `0x100045297`.
3. `sub_100043720` (`0x100043720`, size `0x1da8`) is the discoverer: it receives an IOKit iterator (`IOIteratorNext`, `IOObjectRetain/Release`) and, per service, reads registry properties with `IORegistryEntryCreateCFProperty`.

## What `sub_100043720` reads from each new service

| Key | Use |
|---|---|
| `IOInterfaceName` (`0x100043990` and onward) | BSD name of the interface |
| `IO80211VirtualInterfaceRole` (string at `0x100446640`) | role, tried first |
| `IO80211InterfaceRole` (string at `0x100446660`) | role, alternative if the former is missing |

The role value is converted to a Swift `String` (`swift_dynamicCast`) and compared with these strings, in this order (`0x1000440a6`–`0x1000441db`):

| String | Internal code (`r14b`) |
|---|---:|
| `Infrastructure` | 0 |
| `AirLink` | 1 |
| `WiFi-Aware Discovery` | 2 |
| `WiFi-Aware Discovery+Data` | 2 (no own visible assignment) |
| `WiFi-Aware Data` | 3 |

If none matches it goes to `0x100045227`. On match it creates an `InterfaceHandle` (`sub_1000383C0`, which calls `Apple80211Open`).

Discoverer log messages, useful for live diagnosis: `Ignoring %s because its not a WiFi interface` (`0x100494b00`), `Ignoring %s because no interface index found` (`0x100494b30`), `Unable to find role for interface: %s` (`0x100494b60`), `Found new interface…`, `Failed to bind to…`.

## Live check

`ioreg -l` shows our `RTLAirInterface` already publishes `"IO80211InterfaceRole" = "Infrastructure"`. So that property exists on our interfaces without extra code and the discoverer already recognizes it.

## Consequences for the prototype

- For `wifip2pd` to consider the AWDL interface created, the object must appear in IORegistry with `IOInterfaceName` and a role property whose value is one of the strings above, and it must have an interface index (BSD, `if_nametoindex`). Answering `VIRTUAL_IF_CREATE` with success without publishing that interface leaves the daemon waiting 30 s.
- The role string corresponding to AWDL **was not verified**. `AirLink` is the candidate by exclusion (there is no other string that fits), but confirming it requires seeing what Broadcom writes in `IO80211VirtualInterfaceRole` for its `AppleBCMWLANProximityInterface`.
- It is also unverified whether `IO80211VirtualInterface::init` (base class, exported) writes that property by itself, the way `IO80211InfraInterface` does with `IO80211InterfaceRole`. If it does, a subclass would inherit it.

## Pending

1. In `BootKernelExtensions.i64`, look up the references to `IO80211VirtualInterfaceRole` to see who writes it and with what value for role 6.
2. Confirm the AWDL role string.
3. Table of 16 event ids (`0x100515ca0`, for infrastructure): `1, 11, 59, 2, 3, 125, 126, 127, 9, 66, 74, 78, 82, 215, 55, 160`. It was read from the IDB's memory. It is the set `sub_100041B60` registers with `Apple80211StartMonitoringEvent` only for the role 0 handle; the other roles use `Apple80211RawP2PEventMonitoringInit` (`sub_100041F20`). Each id's name is not verified.
