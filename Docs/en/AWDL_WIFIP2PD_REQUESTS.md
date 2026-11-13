# Apple80211 requests of wifip2pd (macOS 25.6.0)

Result of following the xrefs in `Build/ida_awdl/wifip2pd.i64` (`wifip2pd.x86_64`, md5 `a120cfccfa2783303604ce5be9e1763a`) with IDA. It closes the pending items left by `AWDL_ROLE6_RE.md` and `AWDL_PROTOTYPE_DESIGN.md` on the post-create sequence. Addresses are VAs of the binary. Whatever is not proven is marked as such.

## Apple80211 surface of the daemon

`wifip2pd` calls very few functions of the framework. Stubs at `0x1004457xx`:

| Function | Stub | Who calls it |
|---|---|---|
| `Apple80211Open` | `0x1004457d4` | `sub_1000383C0` |
| `Apple80211RawGet` | `0x1004457da` | `sub_100046D30` (generic wrapper) and `sub_100047660` (3 sites: `0x10004811d`, `0x100048260`, `0x1000483a8`) |
| `Apple80211RawSet` | `0x1004457e0` | `sub_100047660` (`0x100048d95`) and `sub_10004B300` (`0x10004b3e6`) |
| `Apple80211StartMonitoringEvent` | `0x1004457e6` | `sub_100041B60`, 16 events of a table at `0x100515c84` |
| `Apple80211EventMonitoringHalt` | `0x1004457ce` | `sub_100042120` |

No `Apple80211Set/Get` was found by name. Everything goes through `RawGet/RawSet` with a request structure.

## Request format (verified in `sub_100046D30` and `sub_100047660`)

Structure passed to `RawGet/RawSet(handle, req)`:

| Offset | Field |
|---:|---|
| `+0x10` | request type (apple80211 id) |
| `+0x18` | length (u32) |
| `+0x20` | pointer to the data |

## `sub_100047660`: obtaining or creating an interface handle per role

It looks up an `InterfaceHandle` by role and parent interface name. If it does not exist, it creates it. The role selects a branch by jump table at `0x10004992c`; on the role 6 branch it arms the name `"awdl"`.

Sequence it runs to create:

1. `RawGet` type **12**, length **0x15 (21)**. Repeated in three branches. It matches `CARD_CAPABILITIES`, the request already known to be used as the gate (AWDL gate).
2. `RawSet` type **0x5E = 94**, length **0x20 = 32**: it is `VIRTUAL_IF_CREATE`. Data in the buffer (`var_C0`): a u32 at `+0`, six bytes at `+4..+9` (the MAC, loaded from the function's arguments) and the **role at `+0x0C`**, which is **6** (AWDL) or **0x0A** depending on the branch. It matches what was observed on hardware.
3. If `RawSet` returns an error, it is logged and the exception is thrown. No retry.
4. If it returns 0, the daemon **waits on a semaphore with a 30 s timeout** (`DispatchTime.now() + 30`). The semaphore is saved per role and is released by a driver event callback.

Consequence for the prototype: after answering the `VIRTUAL_IF_CREATE`, the driver must **emit an event** that `wifip2pd` receives. Without it, the daemon waits 30 s per attempt. Which exact event unlocks the semaphore is **not verified**: the callback is `sub_1000685A0` → `sub_100041FE0`, which only forwards `(event type, data, length)` to a closure; who consumes it and calls `semaphore.signal` remains to be located.

## `sub_100046D30`: generic `RawGet` with output string

It arms the request with type **97** (`+0x10`), the length passed by parameter and the pointer to the buffer, calls `RawGet` and returns the read C-string (up to the given length). Which apple80211 id the 97 is **not verified**; by usage it looks like a name read. Its only direct caller is `sub_100068C20`.

## `sub_10004B300`: `RawSet` type 293

`RawSet` type **293**, length **24**, followed by `if_nametoindex`. Not attributed to AWDL.

## Events

`sub_100041B60` registers 16 events with `Apple80211StartMonitoringEvent` only for the role 0 handle (infrastructure). For the other roles it uses another registration (`sub_100041F20`) and does not run that loop. The table of 16 ids is at `0x100515c84` and was not transcribed here.

## What is missing

1. Identify the event releasing the role 6 semaphore and which event fields `wifip2pd` reads.
2. Map types 97 and 293 to apple80211 ids.
3. Trace the later requests over the already created AWDL handle (`get/setAWDL_*`): `sub_100047660` only manages the handle; configuration requests come from other functions using the wrapper `sub_100046D30` via `sub_100068C20`.
