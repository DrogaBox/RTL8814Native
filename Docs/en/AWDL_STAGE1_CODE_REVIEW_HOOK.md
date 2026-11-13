# Code review — AWDL stage 1 hook applied in `RTLAirLayer`

Static review of the hook applied in the working tree (`Source/AirLayer/RTLAirLayer.cpp`,
`Source/AirLayer/RTLAirLayer.hpp` — parts added by `Tools/awdl_stage1_hook.patch`, which the tree
reproduces verbatim) against `Docs/AWDL_STAGE1_IMPL_SPEC.md` §4–§9 and
`Docs/AWDL_STAGE1_DISPATCH_PATH.md`. The disassembly was not consulted nor was anything executed on
hardware; no file was modified. What the sources do not allow deciding is marked `?`.

Reviewed scope: `setVIRTUAL_IF_CREATE`, `createAwdlInterface`, `destroyAwdlInterface`, the
arming by boot-arg `rtlawdl`, `rtlAirLog`, the `_awdlLock` lock, the teardown in `stop`/`free` of
the controller and the pieces of `AWDLInterface.cpp` the hook invokes, as far as they affect the 94's
contract and the lifecycle.

## 1. Conforming checks

Verified without findings, against the spec and the dispatch route:

- **Payload**: `apple80211_virt_if_create_data` in `AWDLInterface.h:74-81` — exactly 32 B
  (`static_assert`), `f0` @+0x00, `mac[6]` @+0x04, `pad` @+0x0A, `role` u32 @+0x0C, `ifname[16]`
  @+0x10 (§4.1). `IO80211_25_6_0.h:206` only forward-declares it, so the definition is unique.
  The family's handler validates the length; the override can not and should not revalidate it.
- **Role table** (§4.3, §11): role 6 creates; role 7 → `kIOReturnUnsupported` (102); rest →
  `0xE0000001`, same as Broadcom. See A5 for the check order and the log.
- **Already exists**: 17 (EEXIST) with the name written anyway at `payload+0x10`
  (`RTLAirLayer.cpp:1780-1783`), taken from `_bsdName`, which is only filled after a
  `registerService(kIOServiceSynchronous)` with a non-empty BSD name (§4.1, §4.3). On failure paths
  the name is not written (§11, common rule); the buffer arrives zeroed from the daemon.
- **Arming**: without `rtlawdl`, a 94 with role 6 answers 102 (`RTLAirLayer.cpp:585-586`), exactly
  what the stub generated (§4.3 row 2, §10.3 row 1). With the version gate closed the whole kext
  does not boot (`RTLAirNub::start`/`RTLAirController::start`), so no handler exists to answer: structural inertia. `_awdlArmed` is set once in `RTLAirController::start`
  (`:1452-1455`) with `PE_parse_boot_argn` and default value 0.
- **Double creation**: `_awdlLock` covers querying `_awdl`, full creation, publication and
  name write (§4.4, §10.2): two concurrent 94s do not create two objects; the second waits and
  answers 17 with the name. See A1/A2 for the cost of that design.
- **Order of `stop`** (§10.3): `destroyAwdlInterface()` before `quiesce()` and before the
  family's `stop`; in destruction: nulling `_awdl` under lock, `setAwdlInterface(NULL)`, `terminate()`,
  `release()`. The lock is freed in `free()` (`:1624-1627`).
- **Queues and pool** (§5.4): 1 TX submission with a non-null action (`txNoop`, consumes the batch and
  returns it through the TX completion in batches of 64 < capacity 128, returns `count`), 1 TX completion,
  1 RX completion, one pool per direction; they add their event sources to the interface's work queue and
  `removeQueues` is idempotent. Conforming to CODE_FIXES §1-§2.
- **`RegistrationInfo`** (§5.3): all fields match (+0x0C=3, +0x10=6, +0x14=2, +0x24=0x80,
  +0x28=0x80, +0x30="awdl", +0x38=0, +0x3C=64 as u16, +0x40=0, +0x48/+0x4C=0x594, +0x108=MAC of the
  94). Object's own memory, freed in `RTLAwdInterface::free`.
- **Boot sequence** (§5.5 steps 1-10): `init()`, `IO80211VirtualInterface::init(ctrl, mac,
  6, "awdl")`, `setInterfaceId(2)`, `attach(ctrl)`, `IO80211VirtualInterface::start`, RegInfo,
  datapath, `registerEthernetInterface`, `registerService(kIOServiceSynchronous)` (SDK constant 0x2, same as the spec's "registerService(2)"), `setRunningState(false)`.
- **`setAwdlInterface`** (step 11, §4.4): it is called from the handler after boot, not from
  boot. It remains in the code conforming to the delta's decision (CODE_FIXES defect 4 not
  applied; exported symbol, no readers: harmless).
- **Dispatch**: the override is in `RTLAirInterface` (role 1), the only object passing the
  `safeMetaCast` to `IO80211InfraProtocol` of the 94's bridge (dispatch route §1.4, §3); it covers the
  ioctl (legacy) and user client (modern) routes because both end in the same bridge.
- **`rtlAirLog`**: same ring as `airLog`, safe re-formatting (`airLog("%s", buf)`, no format
  injection), truncation consistent with `airLog` itself.
- **Datapath leaks**: every failure branch of `createDatapath`/`startWithRegistration`
  frees queues (via `teardownDatapath`/`stop`) and pools (via `free`); no leak found.

## 2. Findings

### A1 — Lock inversion: `_awdlLock` held across `terminate()` / `runAction`

`Source/AirLayer/RTLAirLayer.cpp:1808-1822` (`destroyAwdlInterface`) and, by the same token, the failure
branch of `createAwdlInterface` (`:1791-1797`).

The 94 executes with the interface's work loop gate closed (dispatch route §1.1: the order
runs on the work loop; §1.2: the user client route enters through the command gate), and
`createAwdlInterface` takes `_awdlLock` inside that context (`:1779`). `destroyAwdlInterface`
takes `_awdlLock` and calls `awdl->terminate()` with the lock held; the family's termination descends
to `RTLAwdInterface::stop` → `teardownDatapath` → `wq->runAction(removeQueues)`, which waits on the work
loop. With a 94 in flight (e.g. unplugging the adapter while wifip2pd brings up `awdl0`):

- `stop` thread: holds `_awdlLock`, waits for the work loop gate;
- 94 thread: holds the work loop gate, waits for `_awdlLock`.

Deadlock (AB-BA): unloading/stopping the controller hangs. The window is narrow but
real on any unload with creation traffic. Spec §10.2 prescribes holding the lock during
**creation** (up to `setAwdlInterface`), not during **destruction**; holding it across
`terminate()` is the code's decision.

```cpp
/* before (destroyAwdlInterface, RTLAirLayer.cpp:1812-1821) */
IOLockLock(_awdlLock);
RTLAwdInterface *awdl = _awdl;
_awdl = nullptr;
if (awdl) {
    rtlAwdlPublish(this, nullptr);
    awdl->terminate();        /* can wait on the work loop with the lock taken */
    awdl->release();
}
IOLockUnlock(_awdlLock);

/* after */
IOLockLock(_awdlLock);
RTLAwdInterface *awdl = _awdl;
_awdl = nullptr;
if (awdl)
    rtlAwdlPublish(this, nullptr);   /* cheap and non-blocking: under lock */
IOLockUnlock(_awdlLock);
if (awdl) {
    awdl->terminate();
    awdl->release();
    LOG("awdl: teardown");
}
```

The failure branch of `createAwdlInterface` (`:1793-1794`) needs the same treatment (releasing the
lock before `terminate`/`release`, or reordering the cleanup outside the critical section). With this
change, `terminate`/`release` stop being under lock; the single-creation invariant is
kept because `_awdl` is nulled inside the critical section and `rtlAwdlPublish(NULL)` stays under
lock (a concurrent `create` cannot interleave between nulling and unpublishing).

Severity: **incorrect behavior** (hangs `stop`/unload). The exact overlap between the
family's `terminate` and the work loop has not been measured; the spec's test T2.2 covers the
gate context (`?` on the exact trigger shape, not on the inversion).

### A2 — `stop` ↔ 94 race: `createAwdlInterface` ignores `_stopping` and the arming is not withdrawn

`Source/AirLayer/RTLAirLayer.cpp:1586-1596` (`stop`), `:1775-1804` (`createAwdlInterface`),
`:1943` (existing use of `_stopping`).

`RTLAirController::stop` calls `destroyAwdlInterface()` at the start, but neither clears `_awdlArmed`
nor sets `_stopping` before; `_stopping` is set after `quiesce()` (`:1574`) and `_awdlArmed` is
never withdrawn. Between the end of `destroyAwdlInterface` and the end of `_iface`'s termination, a
94 in flight (the STA interface stays visible to a user client until its termination concludes) passes
`awdlAllowed()`, sees no `_awdl`, and creates a new `awdl0` on a stopping controller. Nothing
destroys it afterward: `_awdl` stays retained until `free()` (which does not free it, A3), the object hangs
from a stopped controller and can touch freed provider memory.

```cpp
/* before (RTLAirLayer.cpp:1586-1589) */
void RTLAirController::stop(IOService *provider)
{
    destroyAwdlInterface();
    quiesce();

/* after */
void RTLAirController::stop(IOService *provider)
{
    _stopping = true;          /* close the window before teardown */
    destroyAwdlInterface();
    quiesce();
```

and in `createAwdlInterface`, inside the critical section:

```cpp
/* after IOLockLock(_awdlLock); in createAwdlInterface */
if (_stopping) {
    IOLockUnlock(_awdlLock);
    return kIOReturnNotReady;
}
```

(With A1 applied, `_stopping` must be set before `destroyAwdlInterface`, as above, because the
critical section is shortened and a 94 can take the lock right after the unpublishing.)

Severity: **incorrect behavior** (leak and use after free on the unload race).

### A3 — Controller `free()` does not tear down `_awdl`

`Source/AirLayer/RTLAirLayer.cpp:1598-1637`.

The patch's comment promises teardown "in `stop` or `free` of the controller", but `free()` only
frees `_awdlLock` (`:1624-1627`); it does not call `destroyAwdlInterface()` nor release `_awdl`. In the normal
IOKit cycle `stop` precedes `free` and `_awdl` is already NULL, but any path without `stop` (the
A2 race, or a termination that never reaches `stop`) suffices for `free()` to destroy the lock with a
live object hanging from it: leak of the object and of the semantic lock. The teardown is idempotent and
cheap; the defensive call is missing.

```cpp
/* before (RTLAirLayer.cpp:1624-1627) */
if (_awdlLock) {
    IOLockFree(_awdlLock);
    _awdlLock = nullptr;
}

/* after */
destroyAwdlInterface();
if (_awdlLock) {
    IOLockFree(_awdlLock);
    _awdlLock = nullptr;
}
```

Severity: **cosmetic** (defense; no real path without `stop` identified apart from A2).

### A4 — Cause codes collapsed to `0xE00002BC` in creation

`Source/AirLayer/RTLAirLayer.cpp:1791-1797`.

Spec §4.3 prescribes returning "the cause's `IOReturn`" and §11 distinguishes `kIOReturnNoMemory`
(allocation), `kIOReturnNoResources` (pool/queues) and the `IOReturn` received from
`registerEthernetInterface`, reserving `0xE00002BC` for the object's failure. The applied code
condenses `initWithController` and `startWithRegistration` into a single boolean and always answers
`0xE00002BC`, losing the real cause (e.g. the `r` of `rtl_eth_register`, logged but not
returned). The daemon cannot distinguish failures; diagnosis stays in the log ring.

```cpp
/* before (RTLAirLayer.cpp:1791-1797) */
if (!awdl->initWithController(this, mac) || !awdl->startWithRegistration(this)) {
    LOG("awdl: init failed");
    awdl->terminate();
    awdl->release();
    IOLockUnlock(_awdlLock);
    return (IOReturn)0xE00002BC;
}

/* after (minimum: propagate the registration cause) */
IOReturn r = awdl->initWithController(this, mac) ? awdl->startWithRegistration(this)
                                                 : (IOReturn)0xE00002BC;
if (r != kIOReturnSuccess) {
    awdl->terminate();
    awdl->release();
    IOLockUnlock(_awdlLock);
    return r;    /* startWithRegistration switches to returning IOReturn and propagates rtl_eth_register */
}
```

Severity: **incorrect behavior** (minor: generic answer to the daemon; spec §11 asks for it
differentiated).

### A5 — Comment and check order in `setVIRTUAL_IF_CREATE`

`Source/AirLayer/RTLAirLayer.cpp:570-588`.

Two minor deviations, together because they share a fix:

1. The comment asserts that "any other role keeps the generated stub's answer, so a system without
   `rtlawdl` behaves exactly as before", but it is false for roles ∉ {6, 7}: the stub returned 102 and the override returns `0xE0000001` with or without `rtlawdl`. With the NAN capability not advertised
   (`getCARD_CAPABILITIES` leaves `p[13]` bit 0 at zero), no emitter of roles 8-10 is expected, so the deviation is theoretical, but the comment promises inertia the code does not
   give. Spec §4.3 does not order the checks, and moving the arming check to the start meets
   all its rows and additionally the comment.
2. The log `awdl: invalid role %u` is also emitted for role 7, which spec §11 does not classify as
   an invalid role, and for any role ≠ 6 even with `rtlawdl` absent (noise on a system where
   AWDL must be inert).

```cpp
/* before (RTLAirLayer.cpp:581-586) */
if (req->role != 6) {
    LOG("awdl: invalid role %u", req->role);
    return req->role == 7 ? kIOReturnUnsupported : (IOReturn)0xE0000001;
}
if (!_ctrl->awdlAllowed())
    return kIOReturnUnsupported;

/* after */
if (!_ctrl->awdlAllowed())
    return kIOReturnUnsupported;          /* total inertia without rtlawdl */
if (req->role != 6) {
    if (req->role != 7)
        LOG("awdl: invalid role %u", req->role);
    return req->role == 7 ? kIOReturnUnsupported : (IOReturn)0xE0000001;
}
```

Severity: **cosmetic** (imprecise comment + log; no expected emitter for the affected roles).

### A6 — `?` `registerService(kIOServiceSynchronous)` under the work loop gate (D4)

`Source/AirLayer/AWDLInterface.cpp:207`, invoked with `_awdlLock` held from
`RTLAirLayer.cpp:1791`.

Spec §5.5 (D4) and §10.1 mark as `?` whether the synchronous registration can wait on the same work
loop whose gate holds the 94's thread (on the ioctl route the 94 runs on that work loop; on the
user client, under its command gate). If the BSD client needs that same work loop, creation blocks
itself. The code follows the spec to the letter; it is not decidable with the available sources and
T2.2 measures it. If it blocks, the asynchronous alternative forces revisiting the name criterion at
`payload+0x10` (§0.1 step 5).

Tentative severity: **blocks boot** (of `awdl0`, not of the system) — `?`, decided by T2.2.

### A7 — `?` Rollback without `deregisterEthernetInterface` when registration succeeded

`Source/AirLayer/AWDLInterface.cpp:210-215`.

If `registerEthernetInterface` succeeds but the BSD name does not appear, the code calls
`teardownDatapath()` and returns failure; spec §11 also prescribes
`deregisterEthernetInterface` "if it got registered" and `detach`. The code delegates to the later
`terminate()` (family, `RTLAirLayer.cpp:1793`), which may handle the deregistration;
the consulted documents do not state what exactly
`IO80211VirtualInterface::terminate` does in that state, so it is not decidable here. T3 detects it.

Tentative severity: **incorrect behavior** (registration leak if the family does not deregister)
— `?`.

### A8 — `awdlInterface()`: raw pointer without lock or retention

`Source/AirLayer/RTLAirLayer.hpp:183`.

`RTLAwdInterface *awdlInterface() const { return _awdl; }` reads `_awdl` without `_awdlLock` and returns
an unretained pointer; after `destroyAwdlInterface()` it dangles. Today it has no caller
(verified in the tree), but published in the header it invites a later use that would break the
invariant of §10.2. Either remove it, or document its contract (only under `_awdlLock`, without retaining).

Severity: **cosmetic** (no callers today).

### A9 — `STUB_ENTER()` missing in the 94's override

`Source/AirLayer/RTLAirLayer.cpp:577`.

All neighboring handlers call `STUB_ENTER()` for the first-call trace dumped into the
hardware log; the 94's override does not, so the trace will not record 94 usage. Its own
`LOG`s partially compensate, but the T1-T7 trace comparison will have a gap.

```cpp
/* before */
IOReturn RTLAirInterface::setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req)
{
    if (!req)

/* after */
IOReturn RTLAirInterface::setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *req)
{
    STUB_ENTER();
    if (!req)
```

Severity: **cosmetic**.

### A10 — `?` Payload length on the legacy route

The override receives no length, only the pointer to the payload. The modern route requires 32 B (dispatch route §1.3); the legacy
route (`setVirtualInterfaceCreate`) accepts `len < 32` and copies `min(len, 32)` into
its stack `apple80211req`, so `role`/`ifname` could read uninitialized bytes if an
emitter sent less than 32. The daemon always sends 32 (`Docs/AWDL_STRUCTS.md` §3.2) and there is
no defense possible in the driver without changing the slot's signature: not a code defect, registered
as a contract limit. `?` (depends on the initialization of the family's stack `req`).

Tentative severity: **incorrect behavior** only with a hostile/incomplete emitter — `?`.

## 3. Final table

| # | Finding | File:line | Severity | State |
|---|---|---|---|---|
| A1 | `_awdlLock` held across `terminate()`/`runAction`: AB-BA with the 94 under the work loop gate; hangs `stop`/unload | `RTLAirLayer.cpp:1808-1822` (and `:1791-1797`) | incorrect behavior (hangs stop/unload) | decided |
| A2 | `stop`↔94 race: without `_stopping` nor withdrawal of `_awdlArmed`, a 94 in flight recreates `awdl0` on a stopping controller | `RTLAirLayer.cpp:1586-1596`, `1775-1804` | incorrect behavior (leak/use after free) | decided |
| A4 | Cause codes collapsed to `0xE00002BC`; spec §4.3/§11 asks for the cause's `IOReturn` | `RTLAirLayer.cpp:1791-1797` | incorrect behavior (minor) | decided |
| A3 | `free()` does not tear down `_awdl` (only `stop` does); idempotent defense missing | `RTLAirLayer.cpp:1598-1637` | cosmetic (defense) | decided |
| A5 | Comment "exactly as before" false for roles ∉{6,7}; invalid role log for 7 and with `rtlawdl` absent | `RTLAirLayer.cpp:570-588` | cosmetic | decided |
| A6 | `registerService(kIOServiceSynchronous)` under the work loop gate with `_awdlLock` held (spec's D4) | `AWDLInterface.cpp:207` | blocks `awdl0` boot (potential) | `?` T2.2 |
| A7 | Rollback without `deregisterEthernetInterface`/`detach` when registration succeeded and the BSD name is missing | `AWDLInterface.cpp:210-215` | incorrect behavior (potential) | `?` T3 |
| A10 | Legacy route accepts `len < 32`; the override cannot validate length (contract limit) | `RTLAirLayer.cpp:577-588` | incorrect behavior (incomplete emitter only) | `?` |
| A8 | `awdlInterface()` without lock nor retention, without callers | `RTLAirLayer.hpp:183` | cosmetic | decided |
| A9 | `STUB_ENTER()` missing in the 94's override | `RTLAirLayer.cpp:577` | cosmetic | decided |

The decided A1+A2+A3 are resolved together: set `_stopping` at the start of `stop`, bound the critical
section of `destroyAwdlInterface` to the pointer nulling and unpublishing, and call
`destroyAwdlInterface()` at the start of `free()`. A4 and A5 are independent local changes. A6 and A7
stay for tests T2.2 and T3 of the spec's plan.
