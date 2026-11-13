# P2P event monitoring in wifip2pd (Apple80211RawP2PEventMonitoringInit)

Full chain of the P2P event registration and closure, with the 16 infrastructure ids as reference.
Addresses = VA of `wifip2pd.x86_64` (md5 `a120cfccfa2783303604ce5be9e1763a`), rea/IDA attached
session 2026-10-28. Anything not seen goes as `?`.

## 1) Who creates it: `sub_100041B60` (method of `AppleDevice.InterfaceHandle`)

`sub_100041B60` (0x34c bytes, `0x100041b60`) is the point where the event monitoring is set up; it
operates on the nested Swift class
`_TtCC7CoreP2P11AppleDeviceP33_31FB922BC8C13D419BA8418A9FE51E1A15InterfaceHandle` (ivars seen:
`queue`, `eventBuffer`, `role`, `reference` — `_OBJC_IVAR_$…InterfaceHandle_queue/eventBuffer/role/reference`
at `0x100041bbe`, `0x100041c5a`, `0x100041ccd`, `0x100041d7f`).

Observed sequence:

1. Queue precondition with `DispatchPredicate.onQueue` (`0x100041c37-0x100041c54`).
2. (Re)assigns the event buffer: `swift_slowAlloc(0x800)` (`0x100041c9b-0x100041ca3`), stores the
   pointer in `eventBuffer` and `eventBuffer+0x100` (`0x100041ca7-0x100041cb2`) — two 256 B areas.
3. Choice of the registration path (`0x100041ccd-0x100041d5c`):
   - if `role == 0` **and** the queue class is exactly `NSObject` (`NSObject.==`, `0x100041d30`) →
     **`sub_100041EB0`** with flag = 1 (`0x100041d52`);
   - in any other case (`role != 0`, or non-NSObject queue) → **`sub_100041F20`** with flag = 0
     (`0x100041d69`).
4. Common call with signature `(reference, queue, sub_1000685A0, ctx)` (`0x100041dcf-0x100041df0`),
   where `ctx` is a 0x30 B Swift object (`metadata stru_100515C68`, `0x100041d9d`) with fields
   `[2]=a2`, `[3]=a3`, `[4]=eventBuffer`, `[5]=eventBuffer+0x100` (`0x100041da4-0x100041db8`).
   `sub_1000685A0` is the C callback the framework will invoke per event.
5. Only on the `sub_100041EB0` path (flag 1) and without error: loop of **16 ids**
   (`0x100041e42-0x100041e76`): reads the qword `&off_100515C80[4 + i]` (i = 0..15; i.e.
   `0x100515CA0 + 8*i`), verifies `id >= 0` and high32 == 0 (`0x100041e5e`, `0x100041e90`) and calls
   `Apple80211StartMonitoringEvent` for each one (`0x100041e6c`).
   - The 16 values (reference already documented, read from the IDB memory in a previous session —
     `AWDL_WIFIP2PD_DISCOVERY.md` §3): **1, 11, 59, 2, 3, 125, 126, 127, 9, 66, 74, 78, 82, 215,
     55, 160**. There is no string naming any of these ids (they are not given names). Note: in the
     file, the qwords of `0x100515CA0` are encoded as chained fixups (they decode to pointers
     `0x100069160`, `0x100069480`, `0x1004ED910`, …); the exact read of the ids requires the IDB
     memory and was not re-verified in this pass (`?` on the encoding, not on the previous read).
   - Minor correction to the previous doc: the registration with `Apple80211StartMonitoringEvent`
     corresponds to the branch `role == 0 && NSObject queue` (flag 1), not to "any role 0"
     without a queue condition.

Callers of `sub_100041B60`: `sub_1000421E0` (`0x1000422ec`; it is the same function the recovery
chain calls at `0x10004724e` from `sub_100046E30`), `sub_100049F20` (`0x100049ff0`),
`sub_10004A0C0` (`0x10004a15c`). Callers of those three: `?` (not reviewed in this pass).

## 2) The P2P path: `sub_100041F20` → `sub_100041F40` → ObjC block

- `sub_100041F20` (0x18 B, `0x100041f20`): pure wrapper →
  `sub_100041F40(a1, a2, a3, a4, &unk_100515D48, &Apple80211RawP2PEventMonitoringInit)`
  (`0x100041f32`). The stub of `Apple80211RawP2PEventMonitoringInit` is referenced as data (`a6`).
  The blob `&unk_100515D48`: in the file it contains qwords encoded as chained fixups that decode to
  pointers (`0x100069180`, `0x1000694A0`, `0x1004EC8E0`, …); its meaning is not determined (`?`).
- `sub_100041F40` (0x9d B, `0x100041f40`): builds an ObjC block on the stack:
  `{isa = _NSConcreteStackBlock, flags = 1107296256 (0x42000000), invoke = sub_100041ED0, context = a5}`
  (`0x100041f68-0x100041f7f`) and captures in `block+32` the Swift function pointer `a3` and in
  `block+40` the object `a4` (`0x100041f59-0x100041f5d`). If `a3 == 0`, the block is nil (no
  monitor, `0x100041fb9`). Then `_Block_copy` (`0x100041f99`) and calls `a6(a1, a2, block)` =
  `Apple80211RawP2PEventMonitoringInit(a1, a2, block)` (`0x100041fc2-0x100041fc7`), with
  `_Block_release` on exit (`0x100041fc7`).
- `sub_100041ED0` (0x47 B, `0x100041ed0`) — **the block invoke, i.e. the "closure receiving (event
  type, data, length)"**: observed signature `(block, u32 type, i64 data, u32 length)`; reads
  `fn = *(block+32)` (the captured Swift closure), retains `*(block+40)`
  (`0x100041ee6-0x100041ef1`) and calls `fn(type, data, length)` (`0x100041efe`), releasing
  afterward (`0x100041f12`).

## 3) Where the event id switch is

**Neither the C callback nor the block distinguishes ids.** `sub_1000685A0` (C callback passed by
`sub_100041B60`) and `sub_100041ED0` (invoke of the P2P block) only forward `(event type, data,
length)` to the Swift closure captured at registration. That Swift closure — the one doing
switch/compare over the id and that would consume the event releasing the 30 s semaphore of the
`VIRTUAL_IF_CREATE` — **was not located in this pass** (`?`); consistent with what is already
documented in `AWDL_WIFIP2PD_REQUESTS.md` ("the one that consumes it and calls
`semaphore.signal` remains to be located"). The ids that closure distinguishes depend on the
caller of `sub_100041B60`/`sub_100041F20`; for the infrastructure path the registered set is the 16
ids above; for the P2P path the ids the framework accepts in `RawP2PEventMonitoringInit` are not in
the wifip2pd binary (`?`).

## 4) Reference: the 16 infrastructure ids

Documented in `AWDL_WIFIP2PD_DISCOVERY.md` (IDB memory read, previous session) and confirmed here
as the set `sub_100041B60` registers on the `sub_100041EB0` branch:

| # | id (decimal) |
|---:|---:|
| 1 | 1 |
| 2 | 11 |
| 3 | 59 |
| 4 | 2 |
| 5 | 3 |
| 6 | 125 |
| 7 | 126 |
| 8 | 127 |
| 9 | 9 |
| 10 | 66 |
| 11 | 74 |
| 12 | 78 |
| 13 | 82 |
| 14 | 215 |
| 15 | 55 |
| 16 | 160 |

No names: there is no string in the binary identifying them (verified in the string inventory of
`AWDL_WIFIP2PD_LOGSTRINGS.md`; none is invented).
